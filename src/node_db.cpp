#include "node_db.h"
#include "utf8_utils.h"
#include "config_io.h"   // sdBegin()
#include <Preferences.h>
#include "storage.h"
#include <nvs.h>
#include <esp_heap_caps.h>
#include <time.h>

NodeDB Nodes;

static const uint32_t kNodePersistMinMs = 10000;  // throttle hot-path NVS writes
static const uint32_t kNodePersistNoSpaceRetryMs = 120000;
static bool s_nodePersistBlockedNoSpace = false;
static uint32_t s_nodePersistRetryAtMs = 0;

// Entries held back for settings.
//
// Node data is a cache — re-learned from the mesh within minutes — while
// settings are not, so the cache is what yields when the partition is tight.
// Without this the node table simply wins the race for space: on the 16 KB NVS
// a Launcher install runs under, a busy mesh fills the partition with node
// blobs and every later config or channel write fails silently.
//
// MUST stay above ~170. nvs_get_stats() counts the compaction page in
// free_entries, and NVS never spends that page: at free_entries == 126 the
// partition is full in practice and even a one-byte write fails with
// NOT_ENOUGH_SPACE. So the usable figure is (free_entries - 126). A settings
// save needs ~44 of those (an ~884-byte config blob is ~29 entries, the channel
// blob ~15) and the superseded copies linger until a compaction, so the floor is
// 126 + 44, and this leaves room for consecutive saves on top.
//
// Lowering this to 128 "to match the settings footprint" is what reintroduced
// the original bug — it left two usable entries and settings stopped persisting
// on Launcher installs.
static const size_t   kNvsSettingsReserveEntries = 224;
static const uint32_t kNvsHeadroomRecheckMs      = 5000;

// Free entries across the whole NVS partition (all namespaces share it).
// Returns SIZE_MAX when stats are unavailable, so an unknown state never
// blocks writes that would otherwise have succeeded.
static size_t nvsFreeEntries() {
    nvs_stats_t st = {};
    if (nvs_get_stats(nullptr, &st) != ESP_OK) return SIZE_MAX;
    return st.free_entries;
}

static bool nodePersistHasHeadroom() {
    static uint32_t lastCheckMs = 0;
    static bool     lastResult  = true;
    const uint32_t now = millis();
    if (lastCheckMs != 0 && (uint32_t)(now - lastCheckMs) < kNvsHeadroomRecheckMs) {
        return lastResult;
    }
    lastCheckMs = now ? now : 1;
    const size_t freeEntries = nvsFreeEntries();
    const bool ok = (freeEntries >= kNvsSettingsReserveEntries);
    if (!ok && lastResult) {
        Serial.printf("[nodedb] NVS down to %u free entries; pausing node persistence "
                      "so settings stay writable\n", (unsigned)freeEntries);
    }
    lastResult = ok;
    return ok;
}

static bool nodePersistCanWrite() {
    if (!nodePersistHasHeadroom()) return false;
    if (!s_nodePersistBlockedNoSpace) return true;
    if ((int32_t)(millis() - s_nodePersistRetryAtMs) >= 0) {
        s_nodePersistBlockedNoSpace = false;
        return true;
    }
    return false;
}

static void nodePersistMarkWriteFailure(const char *key, size_t wanted, size_t wrote) {
    if (!s_nodePersistBlockedNoSpace) {
        Serial.printf("[nodedb] NVS full; pausing node persistence (%s wrote=%u need=%u)\n",
                      key ? key : "?",
                      (unsigned)wrote,
                      (unsigned)wanted);
    }
    s_nodePersistBlockedNoSpace = true;
    s_nodePersistRetryAtMs = millis() + kNodePersistNoSpaceRetryMs;
}

// ── NVS layout ────────────────────────────────────────────────
// Namespace : "nodes"
// Key "ids" : blob of uint32_t[n]  — list of known nodeIds
// Key "n_XXXXXXXX" : NodeBlob for that nodeId (hex, 8 chars → 10-char key)

struct NodeBlobV1 {
    char    longName[40];
    char    shortName[5];
    int32_t latI, lonI, alt;
    float   battPct, voltage;
    uint8_t pubKey[32];
    uint8_t chanIdx;
    // bit 0 = hasPosition, 1 = hasName, 2 = hasPubKey, 3 = hasTelemetry, 4 = favorite
    uint8_t flags;
};

struct NodeBlob {
    char    longName[40];
    char    shortName[5];
    int32_t latI, lonI, alt;
    float   battPct, voltage;
    float   chUtil, airUtil;
    float   temperatureC, humidityPct, pressureHpa;
    uint8_t pubKey[32];
    uint8_t chanIdx;
    // bit 0 = hasPosition, 1 = hasName, 2 = hasPubKey, 3 = hasTelemetry,
    // bit 4 = favorite, 5 = hasDeviceTelemetry, 6 = hasEnvironmentTelemetry
    uint8_t flags;
};

static void nodeKey(char *buf, uint32_t id) {
    snprintf(buf, 12, "n_%08x", id);
}

// ── Init ──────────────────────────────────────────────────────

void NodeDB::init() {
    memset(_nodes, 0, sizeof(_nodes));
    _count = 0;
    _sortDirty = true;

    // Load persisted nodes directly into the array (bypasses save to avoid
    // triggering NVS writes during boot). Open read-write so first boot can
    // create the namespace without logging a noisy NOT_FOUND error.
    Preferences p;
    p.begin("nodes", false);
    static uint32_t ids[MAX_NODES];   // static: 1 KB at MAX_NODES=250, see _saveIds()
    memset(ids, 0, sizeof(ids));
    bool staleIds = false;
    int n = (int)(p.getBytes("ids", ids, sizeof(ids)) / sizeof(uint32_t));
    for (int i = 0; i < n && _count < MAX_NODES; i++) {
        char key[12]; nodeKey(key, ids[i]);
        if (!p.isKey(key)) {
            staleIds = true;
            continue;
        }

        NodeBlob b = {};
        size_t blobLen = p.getBytes(key, &b, sizeof(b));
        if (blobLen == sizeof(NodeBlob)) {
            // loaded as current blob format above
        } else {
            NodeBlobV1 v1 = {};
            blobLen = p.getBytes(key, &v1, sizeof(v1));
            if (blobLen != sizeof(NodeBlobV1)) {
                staleIds = true;
                continue;
            }
            utf8util::copyTruncate(b.longName, sizeof(b.longName), v1.longName);
            utf8util::copyTruncate(b.shortName, sizeof(b.shortName), v1.shortName);
            b.latI = v1.latI; b.lonI = v1.lonI; b.alt = v1.alt;
            b.battPct = v1.battPct; b.voltage = v1.voltage;
            memcpy(b.pubKey, v1.pubKey, 32);
            b.chanIdx = v1.chanIdx;
            b.flags = v1.flags;
        }

        NodeEntry *e = &_nodes[_count++];
        memset(e, 0, sizeof(*e));
        e->nodeId = ids[i];
        utf8util::copyTruncate(e->longName, sizeof(e->longName), b.longName);
        utf8util::copyTruncate(e->shortName, sizeof(e->shortName), b.shortName);
        e->latI = b.latI; e->lonI = b.lonI; e->alt = b.alt;
        e->battPct = b.battPct; e->voltage = b.voltage;
        e->chUtil = b.chUtil; e->airUtil = b.airUtil;
        e->temperatureC = b.temperatureC;
        e->humidityPct = b.humidityPct;
        e->pressureHpa = b.pressureHpa;
        memcpy(e->pubKey, b.pubKey, 32);
        e->chanIdx      = b.chanIdx;
        e->hasPosition  = (b.flags & 1) != 0;
        e->hasName      = (b.flags & 2) != 0;
        e->hasPubKey    = (b.flags & 4) != 0;
        e->hasTelemetry = (b.flags & 8) != 0;
        e->favorite     = (b.flags & 16) != 0;
        e->hasDeviceTelemetry = (b.flags & 32) != 0;
        e->hasEnvironmentTelemetry = (b.flags & 64) != 0;
        // Backward compatibility for older blobs that only exposed a single telemetry bit.
        if (e->hasTelemetry && !e->hasDeviceTelemetry && !e->hasEnvironmentTelemetry) {
            e->hasDeviceTelemetry = true;
        }
        e->hasTelemetry = e->hasDeviceTelemetry || e->hasEnvironmentTelemetry;
        e->lastHeardMs  = 0;  // unknown after reboot
        e->lastPosMs    = 0;  // unknown after reboot
        e->lastPersistMs = 0;
    }

    if (staleIds) {
        // Heal an out-of-sync id index (missing node blobs) so future boots
        // don't spam NOT_FOUND lookups for stale keys.
        static uint32_t validIds[MAX_NODES];
        int validCount = 0;
        for (int i = 0; i < _count && validCount < MAX_NODES; i++) {
            if (_nodes[i].nodeId != 0) validIds[validCount++] = _nodes[i].nodeId;
        }
        size_t wanted = (size_t)validCount * sizeof(uint32_t);
        size_t wrote = p.putBytes("ids", validIds, wanted);
        if (wrote != wanted) {
            nodePersistMarkWriteFailure("ids", wanted, wrote);
        } else {
            Serial.printf("[nodedb] compacted stale ids (%d valid)\n", validCount);
        }
    }
    p.end();
    Serial.printf("[nodedb] loaded %d node(s) from NVS\n", _count);
}

// ── Persistence helpers ───────────────────────────────────────

void NodeDB::_save(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (!e || !e->nodeId) return;
    if (!nodePersistCanWrite()) return;

    NodeBlob b = {};
    utf8util::copyTruncate(b.longName, sizeof(b.longName), e->longName);
    utf8util::copyTruncate(b.shortName, sizeof(b.shortName), e->shortName);
    b.latI = e->latI; b.lonI = e->lonI; b.alt = e->alt;
    b.battPct = e->battPct; b.voltage = e->voltage;
        b.chUtil = e->chUtil; b.airUtil = e->airUtil;
        b.temperatureC = e->temperatureC;
        b.humidityPct = e->humidityPct;
        b.pressureHpa = e->pressureHpa;
    memcpy(b.pubKey, e->pubKey, 32);
    b.chanIdx = (uint8_t)e->chanIdx;
    b.flags = (e->hasPosition  ? 1 : 0) | (e->hasName      ? 2 : 0)
            | (e->hasPubKey    ? 4 : 0) | (e->hasTelemetry ? 8 : 0)
            | (e->favorite     ? 16 : 0)
            | (e->hasDeviceTelemetry ? 32 : 0)
            | (e->hasEnvironmentTelemetry ? 64 : 0);

    char key[12]; nodeKey(key, nodeId);
    Preferences p;
    if (!p.begin("nodes", false)) return;
    size_t wrote = p.putBytes(key, &b, sizeof(b));
    p.end();
    if (wrote != sizeof(b)) {
        nodePersistMarkWriteFailure(key, sizeof(b), wrote);
    }
}

void NodeDB::_saveIds() {
    // Static, not stack: at MAX_NODES=250 this is 1 KB, and _saveIds() runs from
    // upsert() on the packet path — 1/8th of the Arduino loop task's 8 KB stack.
    static uint32_t ids[MAX_NODES];
    int n = 0;
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId) ids[n++] = _nodes[i].nodeId;
    if (!nodePersistCanWrite()) return;
    Preferences p;
    if (!p.begin("nodes", false)) return;
    size_t wanted = n * sizeof(uint32_t);
    size_t wrote = p.putBytes("ids", ids, wanted);
    p.end();
    if (wrote != wanted) {
        nodePersistMarkWriteFailure("ids", wanted, wrote);
    }
}

bool NodeDB::releaseNvsForSettings() {
    if (nvsFreeEntries() >= kNvsSettingsReserveEntries) return false;

    // Only worth doing if this cache is actually holding anything. Clearing an
    // empty namespace frees nothing and reports a recovery that did not happen —
    // which is misleading precisely when the partition is full for some other
    // reason and the log is what you are reading to find out why.
    const size_t before = nvsFreeEntries();
    Preferences p;
    if (!p.begin("nodes", false)) return false;
    const bool hadAny = p.isKey("ids");
    if (!hadAny) { p.end(); return false; }

    // Saturated with node blobs. Drop the persisted cache wholesale rather than
    // evicting node by node: the RAM copy loaded at init() stays intact so the
    // UI is unchanged, the mesh refills it, and the headroom gate above now
    // stops it before it can crowd settings out again.
    p.clear();
    p.end();
    const size_t after = nvsFreeEntries();
    Serial.printf("[nodedb] cleared persisted node cache to free NVS for settings "
                  "(%u -> %u entries free)\n", (unsigned)before, (unsigned)after);
    return true;
}

void NodeDB::clearPersisted() {
    Preferences p; p.begin("nodes", false);
    p.clear();
    p.end();

    // Keep runtime state consistent with storage immediately. The neighbor
    // graph is only ever RAM, but it describes the nodes just dropped, so it
    // goes with them rather than outliving the table it points into.
    memset(_nodes, 0, sizeof(_nodes));
#if FEATURE_DISCOVERY
    memset(_neighbors, 0, sizeof(_neighbors));
#endif
    _count = 0;
    _sortDirty = true;
}

int NodeDB::clearNonFavorites() {
    // Drop the removed nodes' blobs one key at a time rather than p.clear()-ing
    // the namespace and re-saving the survivors. Clearing first would open a
    // window in which the favorites exist only in RAM, and _save() is a no-op
    // whenever node persistence is paused (NVS starved, see
    // nodePersistCanWrite()) — so the rewrite could silently fail to put them
    // back. This is also exactly how upsert() retires an evicted node.
    //
    // One begin()/end() around the whole sweep: up to MAX_NODES keys go in a
    // single handle rather than reopening the namespace per node.
    Preferences p;
    const bool nvsOpen = p.begin("nodes", false);

    // Compact in place: survivors keep their relative order, which _sort() would
    // redo anyway, and the tail is zeroed so the slots read as empty.
    int kept = 0, removed = 0;
    for (int i = 0; i < _count; i++) {
        if (!_nodes[i].nodeId) continue;
        if (_nodes[i].favorite) {
            if (kept != i) _nodes[kept] = _nodes[i];
            kept++;
        } else {
            if (nvsOpen) {
                char key[12]; nodeKey(key, _nodes[i].nodeId);
                p.remove(key);
            }
            removed++;
        }
    }
    if (nvsOpen) p.end();
    if (!removed) return 0;

    memset(&_nodes[kept], 0, sizeof(_nodes[0]) * (size_t)(_count - kept));
    _count = kept;
#if FEATURE_DISCOVERY
    // Same rationale as clearPersisted(): the graph describes nodes that no
    // longer exist. A favorites-only subgraph is not worth rebuilding — the
    // mesh refills it from the next NeighborInfo broadcast.
    memset(_neighbors, 0, sizeof(_neighbors));
#endif
    _sortDirty = true;

    // Survivors' blobs were never touched, so only the index needs rewriting.
    // If this is the write that persistence is currently refusing, the index
    // just keeps listing ids whose blobs are gone — which load() already heals
    // on the next boot (see the staleIds pass in init()).
    _saveIds();
    Serial.printf("[nodedb] cleared %d non-favorite node(s), kept %d favorite(s)\n",
                  removed, kept);
    return removed;
}

bool NodeDB::remove(uint32_t nodeId) {
    if (!nodeId) return false;
    int idx = -1;
    for (int i = 0; i < _count; i++) {
        if (_nodes[i].nodeId == nodeId) { idx = i; break; }
    }
    if (idx < 0) return false;

    // One key, not a namespace clear: every other node's blob has to survive
    // untouched. Same retirement upsert() performs on an evicted node, and the
    // same reasoning as clearNonFavorites() above.
    Preferences p;
    if (p.begin("nodes", false)) {
        char key[12];
        nodeKey(key, nodeId);
        p.remove(key);
        p.end();
    }

    // Compact in place. Relative order among the survivors does not matter —
    // _sort() rebuilds the ranking — but the tail has to stay contiguous,
    // because _count is what every walk of the table trusts.
    for (int i = idx; i < _count - 1; i++) _nodes[i] = _nodes[i + 1];
    memset(&_nodes[_count - 1], 0, sizeof(_nodes[0]));
    _count--;
    _sortDirty = true;

    // The surviving blobs were not touched, so only the index needs rewriting —
    // and if persistence is currently refusing writes, a stale index listing an
    // id whose blob is gone is already healed on the next boot by the staleIds
    // pass in init().
    _saveIds();
    Serial.printf("[nodedb] removed node !%08X (%d left)\n",
                  (unsigned)nodeId, _count);
    return true;
}

void NodeDB::saveAll() {
    _saveIds();
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId) _save(_nodes[i].nodeId);
}

// ── Core operations ───────────────────────────────────────────

NodeEntry *NodeDB::find(uint32_t nodeId) {
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId == nodeId) return &_nodes[i];
    return nullptr;
}

// ── Node CSV schema ──────────────────────────────────────────────────────────
// Shared by the SD archive writer and the web CSV export (see node_db.h). Built
// on every board, including those with no SD slot, because the export works
// regardless of whether archiving is possible.

// Quote a field for CSV: wrap in double quotes and double any embedded quote.
// Node long names are user-set and routinely contain commas and emoji.
static void nodeCsvQuote(const char *in, char *out, size_t outLen) {
    if (!out || outLen < 3) return;
    size_t o = 0;
    out[o++] = '"';
    for (const char *p = in ? in : ""; *p && o < outLen - 2; p++) {
        if (*p == '"' && o < outLen - 3) out[o++] = '"';
        out[o++] = *p;
    }
    out[o++] = '"';
    out[o] = '\0';
}

const char *nodeCsvHeader() {
    return "lastHeardEpoch,nodeId,shortName,longName,hops,snr,latI,lonI,alt,"
           "battPct,voltage,chUtil,airUtil,tempC,humidityPct,pressureHpa,"
           "chanIdx,favorite,hasPosition,hasTelemetry,pubKey";
}

void nodeCsvFormatEntry(const NodeEntry &e, char *out, size_t outLen) {
    if (!out || outLen == 0) return;

    // lastHeardMs is a millis() stamp; convert to wall clock when the clock is
    // set so the record stays meaningful across reboots. 0 = unknown.
    const time_t nowEpoch = time(nullptr);
    const bool   clockOk  = (nowEpoch > 1700000000);
    const uint32_t nowMs  = millis();
    long lastHeardEpoch = 0;
    if (clockOk && e.lastHeardMs != 0 && nowMs >= e.lastHeardMs) {
        lastHeardEpoch = (long)(nowEpoch - (time_t)((nowMs - e.lastHeardMs) / 1000UL));
    }

    char shortQ[16], longQ[96];
    nodeCsvQuote(e.shortName, shortQ, sizeof(shortQ));
    nodeCsvQuote(e.longName,  longQ,  sizeof(longQ));

    // The public key is expensive to reacquire (needs a fresh NODEINFO), so
    // preserve it too — it is what makes an exported record useful for PKI.
    char pubHex[65];
    pubHex[0] = '\0';
    if (e.hasPubKey) {
        for (int i = 0; i < 32; i++) snprintf(pubHex + i * 2, 3, "%02x", e.pubKey[i]);
    }

    snprintf(out, outLen,
             "%ld,!%08lx,%s,%s,%u,%.2f,%ld,%ld,%ld,"
             "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%s",
             lastHeardEpoch,
             (unsigned long)e.nodeId,
             shortQ, longQ,
             (unsigned)e.hops, e.snr,
             (long)e.latI, (long)e.lonI, (long)e.alt,
             e.battPct, e.voltage, e.chUtil, e.airUtil,
             e.temperatureC, e.humidityPct, e.pressureHpa,
             e.chanIdx,
             e.favorite ? 1 : 0,
             e.hasPosition ? 1 : 0,
             e.hasTelemetry ? 1 : 0,
             pubHex);
}

// Reader half of nodeCsvQuote(). See node_db.h for the contract.
const char *nodeCsvParseField(const char *p, char *out, size_t outLen) {
    if (out && outLen > 0) out[0] = '\0';
    if (!p) return nullptr;

    size_t o = 0;
    const size_t cap = (out && outLen > 0) ? outLen - 1 : 0;

    if (*p == '"') {
        p++;
        while (*p) {
            if (*p == '"') {
                // A doubled quote is one literal quote; a lone one ends the
                // field. This is the only place the two can be told apart.
                if (p[1] == '"') { if (o < cap) out[o++] = '"'; p += 2; continue; }
                p++;
                break;
            }
            if (o < cap) out[o++] = *p;
            p++;
        }
    } else {
        while (*p && *p != ',') {
            if (o < cap) out[o++] = *p;
            p++;
        }
    }

    if (out && outLen > 0) out[o] = '\0';
    if (*p == ',') return p + 1;
    return nullptr;   // last field on the line
}

// ── Evicted-node archive ─────────────────────────────────────────────────────
// See the contract in node_db.h. Eviction runs on the packet path, so it only
// memcpys the doomed record into this ring; nodeArchiveFlush() does the SD I/O
// from the main loop.

// User preference, mirrored from RhinoConfig::nodeArchiveEnabled by the main
// loop so node_db never has to reach into the config object.
static bool s_archEnabled = false;   // opt-in; mirrored from config by the main loop
void nodeArchiveSetEnabled(bool enabled) { s_archEnabled = enabled; }
bool nodeArchiveIsEnabled() { return s_archEnabled; }

#if HAS_SD_CARD
static const char *kArchivePath = "/camillia/nodes_archive.csv";
static const char *kArchiveDir  = "/camillia";

// 8 slots is deep insulation: an eviction needs a packet from a node not already
// in a full table, and the loop drains the whole queue in one file open, so in
// practice depth never exceeds 1-2.
static const int kArchiveQueueLen = 8;
static NodeEntry s_archQueue[kArchiveQueueLen];
static int      s_archHead = 0, s_archTail = 0, s_archCount = 0;
static uint32_t s_archWritten = 0, s_archDropped = 0;
static bool     s_archNoSdLogged = false;
// Mounting is expensive when no card is present (the pager probes several bus
// profiles with delays). Back off between attempts so an eviction burst on a
// card-less device cannot stall the main loop repeatedly.
static const uint32_t kArchiveSdRetryMs = 60000;
static uint32_t s_archSdRetryAtMs = 0;

int      nodeArchivePending() { return s_archCount; }
uint32_t nodeArchiveWritten() { return s_archWritten; }
uint32_t nodeArchiveDropped() { return s_archDropped; }
bool     nodeArchiveSlotExists() { return true; }
const char *nodeArchiveFilePath() { return kArchivePath; }

// Card presence without forcing a mount probe: sdCardMounted() reports the
// cached state, so rendering the web page stays cheap.
bool nodeArchiveAvailable() { return sdCardMounted(); }

static void archiveQueue(const NodeEntry &e) {
    if (e.nodeId == 0) return;
    if (!s_archEnabled) return;   // user opted out; evicted nodes just drop
    if (s_archCount >= kArchiveQueueLen) {
        // Full (SD missing or wedged): drop the oldest so the newest eviction
        // still lands. Preserving recent history beats stalling on stale.
        s_archTail = (s_archTail + 1) % kArchiveQueueLen;
        s_archCount--;
        s_archDropped++;
    }
    s_archQueue[s_archHead] = e;
    s_archHead = (s_archHead + 1) % kArchiveQueueLen;
    s_archCount++;
}

static void archiveDiscardAll() {
    s_archDropped += (uint32_t)s_archCount;
    s_archCount = 0;
    s_archHead = s_archTail = 0;
}

void nodeArchiveFlush() {
    if (s_archCount <= 0) return;
    if (!s_archEnabled) { archiveDiscardAll(); return; }

    const uint32_t nowMs = millis();
    if (s_archSdRetryAtMs != 0 && (int32_t)(nowMs - s_archSdRetryAtMs) < 0) {
        archiveDiscardAll();   // still inside the no-card backoff window
        return;
    }

    if (!sdBegin()) {
        s_archSdRetryAtMs = nowMs + kArchiveSdRetryMs;
        if (!s_archNoSdLogged) {
            s_archNoSdLogged = true;
            Serial.println("[nodedb] archive: no SD card - evicted nodes are not preserved");
        }
        archiveDiscardAll();
        return;
    }
    s_archSdRetryAtMs = 0;
    s_archNoSdLogged = false;

    storageFs().mkdir(kArchiveDir);
    const bool needHeader = !storageFs().exists(kArchivePath);
    File f = storageFs().open(kArchivePath, FILE_APPEND);
    if (!f) {
        Serial.println("[nodedb] archive: open failed - dropping queued nodes");
        archiveDiscardAll();
        return;
    }

    if (needHeader) {
        // archivedEpoch is this writer's own context column; the rest is the
        // shared node schema.
        String hdr = "archivedEpoch,";
        hdr += nodeCsvHeader();
        f.println(hdr);
    }

    const time_t nowEpoch = time(nullptr);
    const bool   clockOk  = (nowEpoch > 1700000000);

    // Drain the whole queue inside one open/close — SD writes are the expensive
    // part and evictions can arrive in bursts when the mesh is busy.
    while (s_archCount > 0) {
        char rec[512];
        nodeCsvFormatEntry(s_archQueue[s_archTail], rec, sizeof(rec));

        char line[544];
        snprintf(line, sizeof(line), "%ld,%s", clockOk ? (long)nowEpoch : 0L, rec);
        f.println(line);

        s_archTail = (s_archTail + 1) % kArchiveQueueLen;
        s_archCount--;
        s_archWritten++;
    }

    f.close();
}

// ── Archived-node index ──────────────────────────────────────────────────────
// See the contract in node_db.h.

// Allocated on first use rather than declared as an array: at the PSRAM cap this
// is ~30 KB, which must come out of external RAM. Kept once allocated — the
// alternative is re-taking a 30 KB block every time the Nodes screen opens.
static ArchivedNode *s_archIndex = nullptr;
static int  s_archIndexCount = 0;
static bool s_archIndexTruncated = false;

static bool archIndexEnsure() {
    if (s_archIndex) return true;
    const size_t bytes = sizeof(ArchivedNode) * NODE_ARCHIVE_INDEX_MAX;
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
    s_archIndex = (ArchivedNode *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    // Fallback covers the boards with no PSRAM, where the cap is small enough to
    // come out of internal heap. A failure here is not an error: the screen just
    // shows the live half, which is what it did before archived rows existed.
    if (!s_archIndex) s_archIndex = (ArchivedNode *)malloc(bytes);
    if (!s_archIndex) Serial.println("[nodedb] archive index alloc failed - live nodes only");
    return s_archIndex != nullptr;
}

// A record is nodeCsvFormatEntry()'s 512-byte budget plus this writer's
// archivedEpoch column, so a line never exceeds the archive writer's own 544.
static const size_t kArchScanLineMax = 576;
static const size_t kArchScanChunk   = 512;
// Hard ceiling on how much of the file one scan reads, independent of how many
// entries it accepts. The index cap alone does not bound the work: rows skipped
// as duplicates or as currently-live keep the scan going, so an archive holding
// ten thousand re-evictions of the same handful of nodes would be read end to
// end. This is what keeps opening the screen cheap on a pathological file.
//
// Scaled with the index: filling 512 entries needs far more of the file in hand
// than filling 24, and a budget that stopped first would put the "+" back on a
// count this is meant to make exact.
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
static const size_t kArchScanMaxBytes = 512 * 1024;
#else
static const size_t kArchScanMaxBytes = 64 * 1024;
#endif

// Returns false to stop the scan.
typedef bool (*ArchiveLineFn)(const char *line, void *ctx);

// Walks the archive newest-line-first. The file is append-ordered, so reading
// it backwards is reading it newest-first, and stopping early costs only the
// chunks actually read -- which is the whole point: the file has no upper size.
//
// completedOut reports whether the scan reached the start of the file rather
// than being stopped by fn.
static bool archiveScanTail(ArchiveLineFn fn, void *ctx, bool *completedOut) {
    if (completedOut) *completedOut = false;
    // sdCardMounted() reads cached state. Deliberately not sdBegin(): probing
    // for a card that is not there is slow (the pager walks several bus
    // profiles), and this runs from a screen-open path.
    if (!sdCardMounted()) return false;
    if (!storageFs().exists(kArchivePath)) return false;

    File f = storageFs().open(kArchivePath, FILE_READ);
    if (!f) return false;

    size_t pos = (size_t)f.size();
    if (pos == 0) { f.close(); if (completedOut) *completedOut = true; return true; }

    // One transient allocation rather than statics: this is ~1.6 KB that would
    // otherwise sit in DRAM for the life of the device to serve a screen the
    // user opens occasionally, and it is too much to put on the loop stack.
    char *buf = (char *)malloc(kArchScanChunk + 2 * kArchScanLineMax);
    if (!buf) { f.close(); return false; }
    char *chunk = buf;
    char *tail  = buf + kArchScanChunk;                     // head of a line continuing before pos
    char *line  = buf + kArchScanChunk + kArchScanLineMax;  // assembled line
    size_t tailLen = 0;
    size_t bytesRead = 0;
    bool stopped = false;

    while (pos > 0 && !stopped) {
        if (bytesRead >= kArchScanMaxBytes) { stopped = true; break; }
        const size_t n = (pos >= kArchScanChunk) ? kArchScanChunk : pos;
        pos -= n;
        bytesRead += n;
        if (!f.seek(pos)) break;
        if (f.read((uint8_t *)chunk, n) != n) break;

        int end = (int)n;   // exclusive end of the not-yet-consumed part of chunk
        for (int i = (int)n - 1; i >= 0; i--) {
            if (chunk[i] != '\n') continue;
            const size_t segLen = (size_t)(end - (i + 1));
            if (segLen + tailLen < kArchScanLineMax) {
                memcpy(line, chunk + i + 1, segLen);
                memcpy(line + segLen, tail, tailLen);
                size_t len = segLen + tailLen;
                while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
                line[len] = '\0';
                if (len > 0 && !fn(line, ctx)) stopped = true;
            }
            tailLen = 0;
            end = i;
            if (stopped) break;
        }
        if (stopped) break;

        if ((size_t)end + tailLen < kArchScanLineMax) {
            memmove(tail + end, tail, tailLen);
            memcpy(tail, chunk, (size_t)end);
            tailLen += (size_t)end;
        } else {
            tailLen = 0;   // a line longer than any this writer produces: drop it
        }
    }

    // Whatever is left in `tail` once pos reaches 0 is the file's first line.
    // Emitted rather than assumed to be the header: archiveParseIndexRow()
    // rejects the header on its own (the id column will not parse as hex), and
    // dropping it here unconditionally would instead silently lose the oldest
    // record of any file that has no header row.
    if (!stopped && tailLen > 0) {
        memcpy(line, tail, tailLen);
        size_t len = tailLen;
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
        line[len] = '\0';
        if (len > 0) fn(line, ctx);
    }
    free(buf);
    f.close();
    if (completedOut) *completedOut = !stopped;
    return true;
}

// upsert() seeds every new entry with names derived from its id -- longName
// "!%08x", shortName "%04X" -- and only a real NODEINFO replaces them. hasName
// is what tells the two apart in RAM, but the CSV schema has no column for it,
// so a record read back cannot distinguish "named Fred" from "never announced".
//
// Reconstructing the placeholder and comparing is exact, needs no new column,
// and stays correct for archives written by earlier firmware. Without it the
// Nodes screen showed a bare hex id where a name belongs -- for most archived
// nodes, since the least-recently-heard entries eviction picks are precisely the
// ones that never sent a NODEINFO.
static bool archiveNameIsPlaceholder(const char *name, uint32_t nodeId, bool isLongName) {
    if (!name || !name[0]) return false;
    char ph[48];
    if (isLongName) snprintf(ph, sizeof(ph), "!%08x", (unsigned)nodeId);
    else            snprintf(ph, sizeof(ph), "%04X", (unsigned)(nodeId & 0xFFFF));
    return strcmp(name, ph) == 0;
}

// Fields 0-4 of an archive row: archivedEpoch, lastHeardEpoch, nodeId,
// shortName, longName. Rejects the header line, which fails the id parse.
static bool archiveParseIndexRow(const char *line, ArchivedNode &out) {
    memset(&out, 0, sizeof(out));
    char fld[64];
    const char *p = line;

    p = nodeCsvParseField(p, fld, sizeof(fld));
    if (!p) return false;
    out.archivedEpoch = strtol(fld, nullptr, 10);

    p = nodeCsvParseField(p, fld, sizeof(fld));
    if (!p) return false;
    out.lastHeardEpoch = strtol(fld, nullptr, 10);

    p = nodeCsvParseField(p, fld, sizeof(fld));
    if (!p) return false;
    out.nodeId = (uint32_t)strtoul((fld[0] == '!') ? fld + 1 : fld, nullptr, 16);
    if (out.nodeId == 0) return false;   // junk, or the header row

    p = nodeCsvParseField(p, out.shortName, sizeof(out.shortName));
    if (!p) return false;
    nodeCsvParseField(p, out.longName, sizeof(out.longName));

    // Cleared rather than kept, because this struct exists to be displayed: an
    // empty field is what makes a row fall through to the next real name, and
    // finally to the id only when the node genuinely never announced one.
    if (archiveNameIsPlaceholder(out.longName, out.nodeId, true))   out.longName[0] = '\0';
    if (archiveNameIsPlaceholder(out.shortName, out.nodeId, false)) out.shortName[0] = '\0';
    return true;
}

// The whole row, back into a NodeEntry. Mirrors nodeCsvFormatEntry() field for
// field; the two must be edited together.
static bool archiveParseFullRow(const char *line, NodeEntry &out, long *archivedEpochOut) {
    memset(&out, 0, sizeof(out));
    char fld[80];
    const char *p = line;

    auto next = [&](void) -> bool {
        if (!p) { fld[0] = '\0'; return false; }
        p = nodeCsvParseField(p, fld, sizeof(fld));
        return true;
    };

    if (!next()) return false;
    if (archivedEpochOut) *archivedEpochOut = strtol(fld, nullptr, 10);

    if (!next()) return false;
    const long lastHeardEpoch = strtol(fld, nullptr, 10);

    if (!next()) return false;
    out.nodeId = (uint32_t)strtoul((fld[0] == '!') ? fld + 1 : fld, nullptr, 16);
    if (out.nodeId == 0) return false;

    if (!p) return false;
    p = nodeCsvParseField(p, out.shortName, sizeof(out.shortName));
    if (!p) return false;
    p = nodeCsvParseField(p, out.longName, sizeof(out.longName));

    if (!next()) return false; out.hops         = (uint8_t)strtoul(fld, nullptr, 10);
    if (!next()) return false; out.snr          = strtof(fld, nullptr);
    if (!next()) return false; out.latI         = (int32_t)strtol(fld, nullptr, 10);
    if (!next()) return false; out.lonI         = (int32_t)strtol(fld, nullptr, 10);
    if (!next()) return false; out.alt          = (int32_t)strtol(fld, nullptr, 10);
    if (!next()) return false; out.battPct      = strtof(fld, nullptr);
    if (!next()) return false; out.voltage      = strtof(fld, nullptr);
    if (!next()) return false; out.chUtil       = strtof(fld, nullptr);
    if (!next()) return false; out.airUtil      = strtof(fld, nullptr);
    if (!next()) return false; out.temperatureC = strtof(fld, nullptr);
    if (!next()) return false; out.humidityPct  = strtof(fld, nullptr);
    if (!next()) return false; out.pressureHpa  = strtof(fld, nullptr);
    if (!next()) return false; out.chanIdx      = (int)strtol(fld, nullptr, 10);
    if (!next()) return false; out.favorite     = (strtol(fld, nullptr, 10) != 0);
    if (!next()) return false; out.hasPosition  = (strtol(fld, nullptr, 10) != 0);
    if (!next()) return false; out.hasTelemetry = (strtol(fld, nullptr, 10) != 0);
    next();   // pubKey is the last field, so this returns null -- expected
    if (strlen(fld) == 64) {
        bool ok = true;
        for (int i = 0; i < 32 && ok; i++) {
            char byteHex[3] = { fld[i * 2], fld[i * 2 + 1], '\0' };
            char *endp = nullptr;
            unsigned long v = strtoul(byteHex, &endp, 16);
            if (endp != byteHex + 2) { ok = false; break; }
            out.pubKey[i] = (uint8_t)v;
        }
        out.hasPubKey = ok;
        if (!ok) memset(out.pubKey, 0, sizeof(out.pubKey));
    }

    // Not columns in the schema, so they are inferred rather than read.
    //
    // The placeholder strings are deliberately left in place here, unlike in the
    // index above: this NodeEntry goes back into the live table on restore, and
    // upsert() would have put exactly those there. Only the claim is corrected,
    // so a restored node that never announced still reads as unnamed and the
    // first real NODEINFO replaces the placeholder as usual.
    out.hasName = (out.longName[0] != '\0')
                  && !archiveNameIsPlaceholder(out.longName, out.nodeId, true);
    if (out.hasTelemetry) {
        out.hasDeviceTelemetry = (out.voltage != 0.0f || out.battPct != 0.0f
                                  || out.chUtil != 0.0f || out.airUtil != 0.0f);
        out.hasEnvironmentTelemetry = (out.temperatureC != 0.0f || out.humidityPct != 0.0f
                                       || out.pressureHpa != 0.0f);
    }
    // hops survives, but hasHops does not: the schema has no column for it, and
    // guessing true would tell Discovery this is a confirmed direct neighbor.

    // Put lastHeardMs back on the millis() timeline so a restored node sorts by
    // real recency instead of landing at the bottom. Only possible with a set
    // clock and a stamp that is neither in the future nor older than uptime.
    const time_t nowEpoch = time(nullptr);
    if (lastHeardEpoch > 0 && nowEpoch > 1700000000 && (long)nowEpoch >= lastHeardEpoch) {
        const unsigned long ageMs = (unsigned long)((long)nowEpoch - lastHeardEpoch) * 1000UL;
        const uint32_t nowMs = millis();
        out.lastHeardMs = (ageMs < nowMs) ? (uint32_t)(nowMs - ageMs) : 1;
    }
    return true;
}

static bool archiveIndexRowFn(const char *line, void *) {
    if (!s_archIndex) return false;
    ArchivedNode a;
    if (!archiveParseIndexRow(line, a)) return true;   // junk or header: keep going
    // Came back since it was archived -- it belongs to the live list, not here.
    if (Nodes.find(a.nodeId)) return true;
    // Newest-first means the first record seen for an id is the one to keep, so
    // repeated evictions of the same node collapse to one row.
    for (int i = 0; i < s_archIndexCount; i++) {
        if (s_archIndex[i].nodeId == a.nodeId) return true;
    }
    s_archIndex[s_archIndexCount++] = a;
    return s_archIndexCount < NODE_ARCHIVE_INDEX_MAX;
}

void nodeArchiveIndexClear() {
    s_archIndexCount = 0;
    s_archIndexTruncated = false;
}

bool nodeArchiveIndexBuild() {
    nodeArchiveIndexClear();
    if (!archIndexEnsure()) return false;
    bool completed = false;
    if (!archiveScanTail(archiveIndexRowFn, nullptr, &completed)) {
        nodeArchiveIndexClear();
        return false;
    }
    // Stopped short only ever means the cap was reached with file left to read.
    s_archIndexTruncated = !completed;
    return true;
}

int  nodeArchiveIndexCount() { return s_archIndexCount; }
bool nodeArchiveIndexTruncated() { return s_archIndexTruncated; }

const ArchivedNode *nodeArchiveIndexAt(int i) {
    if (!s_archIndex || i < 0 || i >= s_archIndexCount) return nullptr;
    return &s_archIndex[i];
}

namespace {
struct ArchiveLoadCtx {
    uint32_t   wantId;
    NodeEntry *out;
    long      *archivedEpoch;
    bool       found;
};
}

static bool archiveLoadRowFn(const char *line, void *ctxv) {
    ArchiveLoadCtx *c = (ArchiveLoadCtx *)ctxv;
    ArchivedNode a;
    if (!archiveParseIndexRow(line, a)) return true;
    if (a.nodeId != c->wantId) return true;
    c->found = archiveParseFullRow(line, *c->out, c->archivedEpoch);
    return !c->found;   // first (newest) match wins
}

bool nodeArchiveLoadFull(uint32_t nodeId, NodeEntry &out, long *archivedEpochOut) {
    if (nodeId == 0) return false;
    ArchiveLoadCtx ctx = { nodeId, &out, archivedEpochOut, false };
    if (!archiveScanTail(archiveLoadRowFn, &ctx, nullptr)) return false;
    return ctx.found;
}

bool nodeArchiveClear() {
    // Queue first, and unconditionally: these are evictions from the table that
    // is being wiped, so they must not survive to be written back. Not counted
    // as dropped — nothing was lost, it was deleted on purpose.
    s_archCount = 0;
    s_archHead = s_archTail = 0;
    nodeArchiveIndexClear();

    if (!sdCardMounted()) return false;
    if (!storageFs().exists(kArchivePath)) return true;   // already gone
    const bool removed = storageFs().remove(kArchivePath);
    Serial.printf("[nodedb] archive clear: %s\n", removed ? "removed" : "REMOVE FAILED");
    return removed;
}

#else   // !HAS_SD_CARD

// Boards with no SD slot do not expose the removable-card archive, so the
// queue, its 1.3 KB of RAM, and the CSV writer are compiled out.
static inline void archiveQueue(const NodeEntry &) {}
void     nodeArchiveFlush() {}
int      nodeArchivePending() { return 0; }
uint32_t nodeArchiveWritten() { return 0; }
uint32_t nodeArchiveDropped() { return 0; }
bool     nodeArchiveSlotExists() { return false; }
bool     nodeArchiveAvailable() { return false; }
const char *nodeArchiveFilePath() { return nullptr; }

bool nodeArchiveIndexBuild() { return false; }
void nodeArchiveIndexClear() {}
int  nodeArchiveIndexCount() { return 0; }
bool nodeArchiveIndexTruncated() { return false; }
const ArchivedNode *nodeArchiveIndexAt(int) { return nullptr; }
bool nodeArchiveLoadFull(uint32_t, NodeEntry &, long *) { return false; }
bool nodeArchiveClear() { return false; }

#endif  // HAS_SD_CARD

NodeEntry *NodeDB::upsert(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (e) return e;

    uint32_t evictedId = 0;
    if (_count >= MAX_NODES) {
        _sort();
        // Favorites are never evicted, however stale. _sort() places them first,
        // so the tail is normally already a non-favorite; scanning back only
        // does real work when favorites fill the end of the table.
        int victim = -1;
        for (int i = _count - 1; i >= 0; i--) {
            if (!_nodes[i].favorite) { victim = i; break; }
        }
        if (victim < 0) {
            // Every slot is favorited: there is genuinely no room, and taking
            // one would break the guarantee above. Refuse rather than evict.
            return nullptr;
        }
        evictedId = _nodes[victim].nodeId;
        // Preserve the least-recently-heard node before its slot is reused.
        // Queue only — the SD write happens off the packet path.
        archiveQueue(_nodes[victim]);
        // Delete the evicted node's blob from NVS
        char evKey[12]; nodeKey(evKey, evictedId);
        Preferences p; p.begin("nodes", false); p.remove(evKey); p.end();
        e = &_nodes[victim];
    } else {
        e = &_nodes[_count++];
    }
    memset(e, 0, sizeof(*e));
    e->nodeId = nodeId;
    snprintf(e->shortName, sizeof(e->shortName), "%04X", nodeId & 0xFFFF);
    snprintf(e->longName,  sizeof(e->longName),  "!%08x", nodeId);
    // A new node landed in the table (appended, or written over an evicted
    // slot). Either way the ordering the last sort produced no longer holds.
    _sortDirty = true;

    _saveIds();   // add new / remove evicted from the index
    _save(nodeId); // write initial blob so load() never finds an orphaned ID
    e->lastPersistMs = millis();
    return e;
}

NodeEntry *NodeDB::at(int idx) {
    if (idx < 0 || idx >= _count) return nullptr;
    return &_nodes[idx];
}

NodeEntry *NodeDB::getByRank(int rank) {
    _sort();
    if (rank < 0 || rank >= _count) return nullptr;
    return &_nodes[rank];
}

static int cmpNodes(const void *a, const void *b) {
    const NodeEntry *na = (const NodeEntry *)a;
    const NodeEntry *nb = (const NodeEntry *)b;
    if (!na->nodeId) return  1;
    if (!nb->nodeId) return -1;
    if (na->favorite && !nb->favorite) return -1;
    if (!na->favorite && nb->favorite) return  1;
    if (na->hasName && !nb->hasName) return -1;
    if (!na->hasName && nb->hasName) return  1;
    if (na->lastHeardMs > nb->lastHeardMs) return -1;
    if (na->lastHeardMs < nb->lastHeardMs) return  1;
    return 0;
}

void NodeDB::_sort() {
    if (!_sortDirty) return;
    qsort(_nodes, _count, sizeof(NodeEntry), cmpNodes);
    _sortDirty = false;
}

// ── Update methods ────────────────────────────────────────────

void NodeDB::updateFromPacket(const MeshPacket &pkt) {
    NodeEntry *e = upsert(pkt.hdr.from);
    if (!e) return;   // table full of favorites
    e->lastHeardMs = pkt.rxMs;
    _sortDirty = true;   // recency is a ranking key
    e->snr         = pkt.snr;
    // Routing ACK/NAK can arrive on a fallback channel and should not drive
    // future DM channel selection.
    bool isRoutingAckOrNak = (pkt.portnum == ROUTING_APP && pkt.requestId != 0);
    if (!isRoutingAckOrNak && pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) {
        e->chanIdx = pkt.chanIdx;
        // Channel hash 0 is PKI marker, not a channel-key hash.
        if (pkt.hdr.channel != 0) {
            e->chanHash = pkt.hdr.channel;
            e->hasChanHash = true;
        }
    }
    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    uint8_t hopStart = (pkt.hdr.flags >> 5) & 0x07;
    // hop_start == 0 is "this sender never told us", not "zero hops away" —
    // older firmware and anything arriving over MQTT looks like that. Leave the
    // previous reading in place rather than overwriting a known distance with a
    // guess, and only claim a direct neighbor (hopLimit == hopStart) when the
    // packet actually carried the field.
    if (hopStart > 0) {
        e->hops = (hopStart > hopLimit) ? (uint8_t)(hopStart - hopLimit) : 0;
        e->hasHops = true;
    }
    // Don't save on every packet — only on meaningful data changes below.
}

// ── Neighbor reports ─────────────────────────────────────────────────────────
#if FEATURE_DISCOVERY

bool NodeDB::_neighborExpired(const NeighborReport &r, uint32_t nowMs) {
    if (r.nodeId == 0) return true;
    // A reporter that did not advertise an interval is held to our own floor;
    // the alternative is either expiring it instantly or keeping it forever.
    uint32_t intervalS = r.intervalS ? r.intervalS : (uint32_t)NEIGHBORINFO_MIN_INTERVAL_S;
    // 2x the interval in ms, clamped so a hostile or garbled interval cannot
    // overflow the comparison into "never expires".
    uint64_t lifetimeMs = (uint64_t)intervalS * 2000ULL;
    if (lifetimeMs > 0xFFFFFFFFULL) lifetimeMs = 0xFFFFFFFFULL;
    return (uint32_t)(nowMs - r.updatedMs) > (uint32_t)lifetimeMs;
}

void NodeDB::updateNeighbors(uint32_t reporterId, const NeighborInfoPayload &n) {
    uint32_t id = n.nodeId ? n.nodeId : reporterId;
    if (id == 0) return;

    const uint32_t now = millis();

    // Existing report for this node, else a free or expired slot, else the
    // stalest one. Refreshing in place keeps one chatty node from filling the
    // table with copies of itself.
    NeighborReport *slot = nullptr;
    for (int i = 0; i < MAX_NEIGHBOR_REPORTS; i++) {
        if (_neighbors[i].nodeId == id) { slot = &_neighbors[i]; break; }
    }
    if (!slot) {
        for (int i = 0; i < MAX_NEIGHBOR_REPORTS; i++) {
            if (_neighbors[i].nodeId == 0 || _neighborExpired(_neighbors[i], now)) {
                slot = &_neighbors[i];
                break;
            }
        }
    }
    if (!slot) {
        slot = &_neighbors[0];
        for (int i = 1; i < MAX_NEIGHBOR_REPORTS; i++) {
            if ((uint32_t)(now - _neighbors[i].updatedMs) >
                (uint32_t)(now - slot->updatedMs)) {
                slot = &_neighbors[i];
            }
        }
    }

    memset(slot, 0, sizeof(*slot));
    slot->nodeId = id;
    slot->updatedMs = now;
    slot->intervalS = n.nodeBroadcastIntervalS;
    for (int i = 0; i < (int)n.neighborCount && slot->count < MESH_NEIGHBOR_MAX; i++) {
        uint32_t neighborId = n.neighbors[i].nodeId;
        if (neighborId == 0 || neighborId == id) continue;
        float q4 = n.neighbors[i].snr * 4.0f;
        if (q4 > 127.0f) q4 = 127.0f;
        if (q4 < -128.0f) q4 = -128.0f;
        slot->ids[slot->count] = neighborId;
        slot->snrQ4[slot->count] = (int8_t)(q4 >= 0.0f ? (q4 + 0.5f) : (q4 - 0.5f));
        slot->count++;
    }
}

const NeighborReport *NodeDB::neighborReportAt(int idx) const {
    if (idx < 0 || idx >= MAX_NEIGHBOR_REPORTS) return nullptr;
    const NeighborReport &r = _neighbors[idx];
    if (r.nodeId == 0 || _neighborExpired(r, millis())) return nullptr;
    return &r;
}

int NodeDB::neighborReportCount() const {
    const uint32_t now = millis();
    int live = 0;
    for (int i = 0; i < MAX_NEIGHBOR_REPORTS; i++) {
        if (_neighbors[i].nodeId != 0 && !_neighborExpired(_neighbors[i], now)) live++;
    }
    return live;
}

int NodeDB::clearNeighbors() {
    const int dropped = neighborReportCount();
    memset(_neighbors, 0, sizeof(_neighbors));
    return dropped;
}

#endif  // FEATURE_DISCOVERY

void NodeDB::updateUser(uint32_t nodeId, const UserInfo &u) {
    NodeEntry *e = upsert(nodeId);
    if (!e) return;   // table full of favorites
    bool changed = false;
    if (u.longName[0] && strncmp(e->longName, u.longName, sizeof(e->longName) - 1) != 0) {
        utf8util::copyTruncate(e->longName, sizeof(e->longName), u.longName);
        e->hasName = true;
        _sortDirty = true;   // named nodes rank above unnamed ones
        changed = true;
    }
    if (u.shortName[0] && strncmp(e->shortName, u.shortName, sizeof(e->shortName) - 1) != 0) {
        utf8util::copyTruncate(e->shortName, sizeof(e->shortName), u.shortName);
        e->hasName = true;
        _sortDirty = true;
        changed = true;
    }
    if (u.hasPubKey && memcmp(e->pubKey, u.pubKey, 32) != 0) {
        memcpy(e->pubKey, u.pubKey, 32);
        e->hasPubKey = true;
        e->pkiNoChannel = false;
        e->legacyDmNoChannel = false;
        changed = true;
    }
    if (changed) {
        _save(nodeId);
        e->lastPersistMs = millis();
    }
}

void NodeDB::updatePosition(uint32_t nodeId, const PositionInfo &pos) {
    NodeEntry *e = upsert(nodeId);
    if (!e) return;   // table full of favorites
    uint32_t now = millis();
    e->lastPosMs = now;
    bool hadPosition = e->hasPosition;
    bool hasNewPosition = (pos.latI != 0 || pos.lonI != 0);
    bool changed = (e->latI != pos.latI) || (e->lonI != pos.lonI) || (e->alt != pos.alt);
    e->latI = pos.latI; e->lonI = pos.lonI; e->alt = pos.alt;
    e->hasPosition = hasNewPosition;
    bool firstValidPosition = hasNewPosition && !hadPosition;
    if (changed && (firstValidPosition || (now - e->lastPersistMs >= kNodePersistMinMs))) {
        _save(nodeId);
        e->lastPersistMs = now;
    }
}

void NodeDB::updateTelemetry(uint32_t nodeId, const TelemetryInfo &t) {
    if (!t.valid) return;
    NodeEntry *e = upsert(nodeId);
    if (!e) return;   // table full of favorites
    bool changed = false;

    if (t.hasDeviceMetrics) {
        if ((e->battPct != t.battPct) || (e->voltage != t.voltage)
            || (e->chUtil != t.chUtil) || (e->airUtil != t.airUtil)
            || !e->hasDeviceTelemetry) {
            changed = true;
        }
        e->battPct = t.battPct;
        e->voltage = t.voltage;
        e->chUtil = t.chUtil;
        e->airUtil = t.airUtil;
        e->hasDeviceTelemetry = true;
    }

    if (t.hasEnvironmentMetrics) {
        if ((e->temperatureC != t.temperatureC)
            || (e->humidityPct != t.humidityPct)
            || (e->pressureHpa != t.pressureHpa)
            || !e->hasEnvironmentTelemetry) {
            changed = true;
        }
        e->temperatureC = t.temperatureC;
        e->humidityPct = t.humidityPct;
        e->pressureHpa = t.pressureHpa;
        e->hasEnvironmentTelemetry = true;
    }

    e->hasTelemetry = e->hasDeviceTelemetry || e->hasEnvironmentTelemetry;
    uint32_t now = millis();
    if (changed && (now - e->lastPersistMs >= kNodePersistMinMs)) {
        _save(nodeId);
        e->lastPersistMs = now;
    }
}

bool NodeDB::persist(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (!e || !e->nodeId) return false;
    _save(nodeId);
    e->lastPersistMs = millis();
    return true;
}

bool NodeDB::setFavorite(uint32_t nodeId, bool favorite) {
    NodeEntry *e = find(nodeId);
    if (!e || !e->nodeId) return false;
    if (e->favorite == favorite) return false;

    e->favorite = favorite;
    _sortDirty = true;   // favorites sort to the top
    _save(nodeId);
    e->lastPersistMs = millis();
    return true;
}
