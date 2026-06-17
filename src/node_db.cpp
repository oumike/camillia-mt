#include "node_db.h"
#include "utf8_utils.h"
#include <Preferences.h>

NodeDB Nodes;

static const uint32_t kNodePersistMinMs = 10000;  // throttle hot-path NVS writes

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

    // Load persisted nodes directly into the array (bypasses save to avoid
    // triggering NVS writes during boot). Open read-write so first boot can
    // create the namespace without logging a noisy NOT_FOUND error.
    Preferences p;
    p.begin("nodes", false);
    uint32_t ids[MAX_NODES] = {};
    int n = (int)(p.getBytes("ids", ids, sizeof(ids)) / sizeof(uint32_t));
    for (int i = 0; i < n && _count < MAX_NODES; i++) {
        char key[12]; nodeKey(key, ids[i]);
        size_t blobLen = p.getBytesLength(key);
        if (blobLen == 0) continue;

        NodeBlob b = {};
        if (blobLen == sizeof(NodeBlob)) {
            if (p.getBytes(key, &b, sizeof(b)) != sizeof(b)) continue;
        } else if (blobLen == sizeof(NodeBlobV1)) {
            NodeBlobV1 v1 = {};
            if (p.getBytes(key, &v1, sizeof(v1)) != sizeof(v1)) continue;
            utf8util::copyTruncate(b.longName, sizeof(b.longName), v1.longName);
            utf8util::copyTruncate(b.shortName, sizeof(b.shortName), v1.shortName);
            b.latI = v1.latI; b.lonI = v1.lonI; b.alt = v1.alt;
            b.battPct = v1.battPct; b.voltage = v1.voltage;
            memcpy(b.pubKey, v1.pubKey, 32);
            b.chanIdx = v1.chanIdx;
            b.flags = v1.flags;
        } else {
            continue;
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
    p.end();
    Serial.printf("[nodedb] loaded %d node(s) from NVS\n", _count);
}

// ── Persistence helpers ───────────────────────────────────────

void NodeDB::_save(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (!e || !e->nodeId) return;

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
    Preferences p; p.begin("nodes", false);
    p.putBytes(key, &b, sizeof(b));
    p.end();
}

void NodeDB::_saveIds() {
    uint32_t ids[MAX_NODES];
    int n = 0;
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId) ids[n++] = _nodes[i].nodeId;
    Preferences p; p.begin("nodes", false);
    p.putBytes("ids", ids, n * sizeof(uint32_t));
    p.end();
}

void NodeDB::clearPersisted() {
    Preferences p; p.begin("nodes", false);
    p.clear();
    p.end();

    // Keep runtime state consistent with storage immediately.
    memset(_nodes, 0, sizeof(_nodes));
    _count = 0;
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

NodeEntry *NodeDB::upsert(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (e) return e;

    uint32_t evictedId = 0;
    if (_count >= MAX_NODES) {
        _sort();
        evictedId = _nodes[_count - 1].nodeId;
        // Delete the evicted node's blob from NVS
        char evKey[12]; nodeKey(evKey, evictedId);
        Preferences p; p.begin("nodes", false); p.remove(evKey); p.end();
        e = &_nodes[_count - 1];
    } else {
        e = &_nodes[_count++];
    }
    memset(e, 0, sizeof(*e));
    e->nodeId = nodeId;
    snprintf(e->shortName, sizeof(e->shortName), "%04X", nodeId & 0xFFFF);
    snprintf(e->longName,  sizeof(e->longName),  "!%08x", nodeId);

    _saveIds();   // add new / remove evicted from the index
    _save(nodeId); // write initial blob so load() never finds an orphaned ID
    e->lastPersistMs = millis();
    return e;
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
    qsort(_nodes, _count, sizeof(NodeEntry), cmpNodes);
}

// ── Update methods ────────────────────────────────────────────

void NodeDB::updateFromPacket(const MeshPacket &pkt) {
    NodeEntry *e = upsert(pkt.hdr.from);
    e->lastHeardMs = pkt.rxMs;
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
    e->hops = (hopStart > hopLimit) ? (hopStart - hopLimit) : 0;
    // Don't save on every packet — only on meaningful data changes below.
}

void NodeDB::updateUser(uint32_t nodeId, const UserInfo &u) {
    NodeEntry *e = upsert(nodeId);
    bool changed = false;
    if (u.longName[0] && strncmp(e->longName, u.longName, sizeof(e->longName) - 1) != 0) {
        utf8util::copyTruncate(e->longName, sizeof(e->longName), u.longName);
        e->hasName = true;
        changed = true;
    }
    if (u.shortName[0] && strncmp(e->shortName, u.shortName, sizeof(e->shortName) - 1) != 0) {
        utf8util::copyTruncate(e->shortName, sizeof(e->shortName), u.shortName);
        e->hasName = true;
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

bool NodeDB::setFavorite(uint32_t nodeId, bool favorite) {
    NodeEntry *e = find(nodeId);
    if (!e || !e->nodeId) return false;
    if (e->favorite == favorite) return false;

    e->favorite = favorite;
    _save(nodeId);
    e->lastPersistMs = millis();
    return true;
}
