#include "dm_mgr.h"
#include "live_util.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "node_db.h"
#include "channel_mgr.h"
#include "hal/display.h"
#include "debug_flags.h"
#include "config.h"
#include "utf8_utils.h"
#include <esp_heap_caps.h>
#include <string.h>
#if HAS_SD_CARD
#include <SD.h>
#endif

DmMgr DMs;

static void *allocDmStorage(size_t bytes) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return malloc(bytes);
#else
    void *storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage) return storage;
    return malloc(bytes);
#endif
}

static void addLiveDmLine(const char *text, uint16_t color = TFT_DARKGREY) {
    char prefix[12];
    liveBuildPrefix(prefix, sizeof(prefix));
    Channels.addMessage(CHAN_ANN, prefix, text, color);
}

// ── init ──────────────────────────────────────────────────────
void DmMgr::init() {
    memset(_convs, 0, sizeof(_convs));
    memset(_pendingTx, 0, sizeof(_pendingTx));
    _count = 0;
    loadAll();
}

// ── find ──────────────────────────────────────────────────────
DmConv *DmMgr::find(uint32_t nodeId) {
    for (int i = 0; i < _count; i++)
        if (_convs[i].nodeId == nodeId) return &_convs[i];
    return nullptr;
}

DmConv *DmMgr::findOrCreate(uint32_t nodeId, const char *shortName) {
    DmConv *c = find(nodeId);
    if (c) return c;
    if (_count >= MAX_DM_CONVS) return nullptr;

    c = &_convs[_count++];
    memset(c, 0, sizeof(DmConv));
    c->nodeId    = nodeId;
    c->rxChanIdx = -1;
    utf8util::copyTruncate(c->shortName, sizeof(c->shortName), shortName ? shortName : "????");
    c->unread = false;
    c->unreadCount = 0;

    c->lines = (DmLine *)allocDmStorage(MAX_DM_LINES * sizeof(DmLine));
    if (c->lines)
        memset(c->lines, 0, MAX_DM_LINES * sizeof(DmLine));
    return c;
}

// ── getByRank ─────────────────────────────────────────────────
DmConv *DmMgr::getByRank(int idx) {
    if (idx < 0 || idx >= _count) return nullptr;
    return &_convs[idx];
}

// ── hasUnread / markRead ──────────────────────────────────────
bool DmMgr::hasUnread() const {
    for (int i = 0; i < _count; i++)
        if (_convs[i].unread) return true;
    return false;
}

int DmMgr::unreadMessageCount() const {
    int total = 0;
    for (int i = 0; i < _count; i++) {
        total += (int)_convs[i].unreadCount;
    }
    return total;
}

void DmMgr::markRead(uint32_t nodeId) {
    DmConv *c = find(nodeId);
    if (c) {
        c->unread = false;
        c->unreadCount = 0;
    }
}

bool DmMgr::deleteConversation(uint32_t nodeId) {
    int idx = -1;
    for (int i = 0; i < _count; i++) {
        if (_convs[i].nodeId == nodeId) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;

    // Remove persisted transcript if present.
#if HAS_SD_CARD
    char path[40];
    snprintf(path, sizeof(path), "%s/%08X.bin", "/camillia/dms", nodeId);
    if (SD.exists(path)) {
        SD.remove(path);
    }
#endif

    if (_convs[idx].lines) {
        free(_convs[idx].lines);
        _convs[idx].lines = nullptr;
    }

    for (int i = idx; i < _count - 1; i++) {
        _convs[i] = _convs[i + 1];
    }
    if (_count > 0) {
        memset(&_convs[_count - 1], 0, sizeof(DmConv));
        _count--;
    }

    for (int i = 0; i < MAX_DM_PENDING_TX; i++) {
        if (_pendingTx[i].active && _pendingTx[i].nodeId == nodeId) {
            _pendingTx[i].active = false;
        }
    }

    return true;
}

void DmMgr::clearAll(bool clearPersisted) {
    for (int i = 0; i < _count; i++) {
        if (_convs[i].lines) {
            free(_convs[i].lines);
            _convs[i].lines = nullptr;
        }
    }
    memset(_convs, 0, sizeof(_convs));
    _count = 0;
    memset(_pendingTx, 0, sizeof(_pendingTx));

#if HAS_SD_CARD
    if (!clearPersisted) return;

    static const char *kPersistDmDir = "/camillia/dms";
    File dir = SD.open(kPersistDmDir);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f) {
            String fp = String(kPersistDmDir) + "/" + f.name();
            f.close();
            SD.remove(fp.c_str());
            f = dir.openNextFile();
        }
        dir.close();
    }
    if (SD.exists(kPersistDmDir)) {
        (void)SD.rmdir(kPersistDmDir);
    }
#else
    (void)clearPersisted;
#endif
}

// ── _sort: favorites first, then lastMs descending ────────────
void DmMgr::_sort() {
    auto isFavoriteConv = [](const DmConv &conv) -> bool {
        NodeEntry *n = Nodes.find(conv.nodeId);
        return n && n->favorite;
    };

    auto shouldComeBefore = [&](const DmConv &a, const DmConv &b) -> bool {
        bool aFav = isFavoriteConv(a);
        bool bFav = isFavoriteConv(b);
        if (aFav != bFav) return aFav;
        if (a.lastMs != b.lastMs) return a.lastMs > b.lastMs;
        return a.nodeId < b.nodeId;
    };

    for (int i = 1; i < _count; i++) {
        DmConv tmp = _convs[i];
        int j = i - 1;
        while (j >= 0 && shouldComeBefore(tmp, _convs[j])) {
            _convs[j + 1] = _convs[j];
            j--;
        }
        _convs[j + 1] = tmp;
    }
}

// ── _pushLine: append one rendered line ───────────────────────
void DmMgr::_pushLine(DmConv &c, const char *text, uint16_t color,
                      uint32_t packetId, DmLine::AckState ack,
                      uint32_t epoch) {
    if (!c.lines) return;
    int idx = c.count % MAX_DM_LINES;
    utf8util::copyTruncate(c.lines[idx].text, sizeof(c.lines[idx].text), text);
    c.lines[idx].color = color;
    c.lines[idx].packetId = packetId;
    c.lines[idx].ack = ack;
    c.lines[idx].epoch = epoch;
    c.count++;
}

// ── addMessage ────────────────────────────────────────────────
void DmMgr::addMessage(uint32_t nodeId, const char *shortName,
                        const char *prefix, const char *text, uint16_t color,
                        bool markUnread, int chanIdx, uint32_t packetId) {
    DmConv *c = findOrCreate(nodeId, shortName);
    if (!c) return;

    if (shortName && shortName[0]) {
        utf8util::copyTruncate(c->shortName, sizeof(c->shortName), shortName);
    }

    utf8util::copyTruncate(c->lastText, sizeof(c->lastText), text);
    c->lastMs = millis();
    if (markUnread) {
        c->unread = true;
        if (c->unreadCount < 0xFFFF) c->unreadCount++;
    }
    if (chanIdx >= 0 && chanIdx < MESH_CHANNELS) c->rxChanIdx = chanIdx;

    // Store the full message (prefix + body) in a single DmLine. The UI label
    // uses LVGL's wrap mode to break the text to the real pane pixel width.
    char full[DM_LINE_LEN + 1];
    int written = snprintf(full, sizeof(full), "%s%s",
                           prefix ? prefix : "",
                           text ? text : "");
    if (written < 0) written = 0;

    time_t nowEpoch = time(nullptr);
    uint32_t epoch = (nowEpoch >= 1700000000) ? (uint32_t)nowEpoch : 0;

    _pushLine(*c, full, color, packetId,
              packetId ? DmLine::PENDING : DmLine::NONE,
              epoch);

    c->scrollOff = 0;  // jump to latest on new message
    saveConv(c);       // save before _sort() — sort may move the struct in the array
    _sort();
}

// ── sendDm ────────────────────────────────────────────────────
bool DmMgr::sendDm(uint32_t myNodeId, uint32_t toNodeId, const char *text) {
    debugLogMessages("[dm] sendDm called: to=!%08X  text='%.30s'\n", toNodeId, text);

    if (!Radio.isReady()) {
        debugLogMessages("[dm] sendDm FAIL: radio not ready\n");
        addLiveDmLine("T DM ER radio", TFT_RED);
        return false;
    }

    uint32_t packetId = nextMeshPacketId();
    uint8_t  proto[256];
    uint8_t  cipher[280]; // 256 proto + 12 PKI overhead + margin

    // Conversation IDs can become stale across reboots/resets. If we have a fresher
    // node with the same short name, send to that node ID instead.
    uint32_t resolvedToNodeId = toNodeId;
    DmConv *convHint = find(toNodeId);
    NodeEntry *initialNode = Nodes.find(toNodeId);
    uint32_t bestHeardMs = initialNode ? initialNode->lastHeardMs : 0;
    if (convHint && convHint->shortName[0]) {
        for (int i = 0; i < Nodes.count(); i++) {
            NodeEntry *cand = Nodes.getByRank(i);
            if (!cand || !cand->shortName[0]) continue;
            if (strncmp(cand->shortName, convHint->shortName, sizeof(convHint->shortName) - 1) != 0)
                continue;
            if (cand->lastHeardMs > bestHeardMs) {
                resolvedToNodeId = cand->nodeId;
                bestHeardMs = cand->lastHeardMs;
            }
        }
    }
    if (resolvedToNodeId != toNodeId) {
        debugLogMessages("[dm] remap destination by shortName '%s': !%08X -> !%08X\n",
                         convHint ? convHint->shortName : "????", toNodeId, resolvedToNodeId);
    }

    // Nodes.getByRank() sorts in place; reacquire a stable pointer after remap.
    NodeEntry *node = Nodes.find(resolvedToNodeId);

    size_t protoLen = encodeTextMessageUnicast(text, myNodeId, resolvedToNodeId, proto, sizeof(proto));
    debugLogMessages("[dm] protoLen=%u\n", (unsigned)protoLen);
    if (protoLen == 0) {
        debugLogMessages("[dm] sendDm FAIL: encode failed\n");
        return false;
    }

    MeshHdr hdr = {};
    hdr.to    = resolvedToNodeId;
    hdr.from  = myNodeId;
    hdr.id    = packetId;
    hdr.flags = (1 << 3) |  // want_ack
                (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                ((MESH_HOP_LIMIT & 0x07) << 5);
    hdr.relay_node = (uint8_t)(myNodeId & 0xFF);

    size_t payloadLen = 0;
    bool usedPki = false;
    int usedChanIdx = -1;
    uint8_t usedHash = 0;

    // Direct DM interop policy: require PKI. Channel-key "legacy DM" paths are
    // often rejected by modern Meshtastic nodes as NO_CHANNEL.
    debugLogMessages("[dm] node=%s  hasPubKey=%d\n",
                     node ? "found" : "null", node ? (int)node->hasPubKey : -1);
    if (!node || !node->hasPubKey) {
        debugLogMessages("[dm] sendDm FAIL: recipient pubkey missing\n");
        addLiveDmLine("T DM ER noPK", TFT_RED);

        static uint32_t sLastNodeInfoReqNode = 0;
        static uint32_t sLastNodeInfoReqMs = 0;
        uint32_t now = millis();
        if (resolvedToNodeId != sLastNodeInfoReqNode || (now - sLastNodeInfoReqMs) > 5000) {
            (void)Channels.sendNodeInfo(myNodeId,
                                        MY_LONG_NAME,
                                        MY_SHORT_NAME,
                                        resolvedToNodeId,
                                        true,
                                        false);
            sLastNodeInfoReqNode = resolvedToNodeId;
            sLastNodeInfoReqMs = now;
        }
        return false;
    }

    hdr.channel = 0; // PKI marker (channel 0 = not a channel-key hash)
    usedHash = 0;
    debugLogMessages("[dm] using PKI path\n");
    if (!encryptPki(packetId, myNodeId, node->pubKey, proto, protoLen, cipher)) {
        debugLogMessages("[dm] sendDm FAIL: PKI encrypt failed\n");
        addLiveDmLine("T DM ER pki", TFT_RED);
        return false;
    }
    usedPki = true;
    payloadLen = protoLen + 12; // ciphertext + tag(8) + extraNonce(4)
    debugLogMessages("[dm] PKI encrypt OK, payloadLen=%u\n", (unsigned)payloadLen);

    debugLogMessages("[dm] transmitting: frameLen=%u\n", (unsigned)(sizeof(MeshHdr) + payloadLen));
    uint8_t frame[sizeof(MeshHdr) + 280];
    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, payloadLen);

    if (!Radio.transmit(frame, sizeof(MeshHdr) + payloadLen)) {
        debugLogMessages("[dm] sendDm FAIL: radio TX failed\n");
        addLiveDmLine("T DM ER tx", TFT_RED);
        return false;
    }

    // Track this DM TX so ROUTING_APP ACK/NAK handling can react based on
    // whether this packet used PKI or channel-key encryption.
    {
        int slot = -1;
        int oldest = 0;
        for (int i = 0; i < MAX_DM_PENDING_TX; i++) {
            if (!_pendingTx[i].active) {
                slot = i;
                break;
            }
            if (_pendingTx[i].sentMs < _pendingTx[oldest].sentMs)
                oldest = i;
        }
        if (slot < 0) slot = oldest;
        _pendingTx[slot] = { packetId, resolvedToNodeId, millis(), usedPki, true };
    }

    debugLogMessages("[dm] TX OK\n");
    {
        DmConv *conv = find(toNodeId);
        char who[16];
        liveNodeLabelWithHint(resolvedToNodeId, conv ? conv->shortName : nullptr, who, sizeof(who));
        char live[72];
        if (usedPki) {
            snprintf(live, sizeof(live), "T DM PKI %s %08X", who, packetId);
        } else {
            snprintf(live, sizeof(live), "T DM CHAN %s %08X c%d h%02X t%08X",
                     who, packetId, usedChanIdx, usedHash, resolvedToNodeId);
        }
        addLiveDmLine(live, TFT_WHITE);
    }

    // Add outgoing message to local conversation (not marked unread)
    DmConv *conv = find(toNodeId);
    debugLogMessages("[dm] addMessage conv=%s\n", conv ? "found" : "NULL - msg won't show!");
    if (conv) {
        char timePrefix[12];
        liveBuildPrefix(timePrefix, sizeof(timePrefix));
        char prefix[24];
        snprintf(prefix, sizeof(prefix), "%s<me> ", timePrefix);
        addMessage(toNodeId, conv->shortName, prefix, text, TFT_WHITE,
                   false, -1, packetId);
    }
    return true;
}

void DmMgr::_setAckState(uint32_t packetId, DmLine::AckState state) {
    if (!packetId) return;
    for (int ci = 0; ci < _count; ci++) {
        DmConv &c = _convs[ci];
        if (!c.lines) continue;
        int stored = min(c.count, MAX_DM_LINES);
        for (int i = 0; i < stored; i++) {
            if (c.lines[i].packetId == packetId) {
                c.lines[i].ack = state;
            }
        }
    }
}

bool DmMgr::handleRoutingResult(uint32_t fromNodeId, uint32_t requestId, uint32_t errorReason) {
    int match = -1;

    // Prefer exact match by packet ID + recipient.
    for (int i = 0; i < MAX_DM_PENDING_TX; i++) {
        if (!_pendingTx[i].active) continue;
        if (_pendingTx[i].packetId == requestId && _pendingTx[i].nodeId == fromNodeId) {
            match = i;
            break;
        }
    }

    // Fallback: packet ID-only match.
    if (match < 0) {
        for (int i = 0; i < MAX_DM_PENDING_TX; i++) {
            if (!_pendingTx[i].active) continue;
            if (_pendingTx[i].packetId == requestId) {
                match = i;
                break;
            }
        }
    }

    if (match < 0) return false;

    const bool usedPki = _pendingTx[match].usedPki;
    _pendingTx[match].active = false;

    if (errorReason == 0) {
        _setAckState(requestId, DmLine::ACKED);
    } else {
        _setAckState(requestId, DmLine::NAKED);
    }

    NodeEntry *n = Nodes.find(fromNodeId);
    if (!n) return true;

    if (errorReason == 6) { // NO_CHANNEL
        if (usedPki) {
            // PKI failed for this node; fall back to channel-key for next DM.
            n->pkiNoChannel = true;
            debugLogMessages("[dm] NAK NO_CHANNEL on PKI DM to !%08X -> disable PKI for node\n", fromNodeId);
        } else {
            // Channel-key failed; allow PKI retry if peer pubkey exists.
            n->pkiNoChannel = false;
            debugLogMessages("[dm] NAK NO_CHANNEL on channel DM to !%08X -> allow PKI retry\n", fromNodeId);
        }
    } else if (errorReason == 0) {
        // ACK: stay on the mode that worked.
        n->pkiNoChannel = usedPki ? false : true;
    }

    return true;
}

// ── getLine ───────────────────────────────────────────────────
const DmLine *DmMgr::getLine(const DmConv *conv, int visibleRow, int visibleRows) const {
    if (!conv || !conv->lines) return nullptr;
    if (visibleRow < 0 || visibleRow >= visibleRows) return nullptr;
    int total = (conv->count < MAX_DM_LINES) ? conv->count : MAX_DM_LINES;
    int oldest = conv->count - total;
    int newest = conv->count - 1 - conv->scrollOff;
    if (newest < oldest) return nullptr;
    int absIdx = newest - visibleRow;
    if (absIdx < oldest || absIdx >= conv->count) return nullptr;
    return &conv->lines[absIdx % MAX_DM_LINES];
}

// ── SD persistence ───────────────────────────────────────────
// File format: /camillia/dms/XXXXXXXX.bin
//   Header: magic(4) + nodeId(4) + shortName(5) + count(4) + numLines(4) + rxChanIdx(4) = 25 bytes
//   Body:   numLines × DmLine entries, written oldest → newest

static const char *kDmDir = "/camillia/dms";
// Magic was bumped when on-disk DmLine.text width changed to support full
// Meshtastic-sized payloads in one record. Older "CMDM"/"CMDN" files used
// shorter records (no epoch field) and are skipped on load (transcripts are
// not migrated).
static const uint32_t DM_MAGIC = 0x434D444F;  // "CMDO"
struct PersistDmLine {
    char     text[DM_LINE_LEN + 1];
    uint16_t color;
    uint32_t epoch;   // wall-clock seconds when added (0 = unknown)
};

void DmMgr::saveConv(const DmConv *c) {
#if !HAS_SD_CARD
    (void)c;
    return;
#else
    if (!c || !c->lines || c->count == 0) return;

    SD.mkdir("/camillia");
    SD.mkdir(kDmDir);

    char path[40];
    snprintf(path, sizeof(path), "%s/%08X.bin", kDmDir, c->nodeId);

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        debugLogMessages("[dm] saveConv FAIL: can't open %s\n", path);
        return;
    }

    // Header: magic(4) + nodeId(4) + shortName(5) + count(4) + numLines(4) + rxChanIdx(4) = 25 bytes
    uint32_t magic = DM_MAGIC;
    uint32_t nid   = c->nodeId;
    int32_t  cnt   = c->count;
    int32_t  nLines = (c->count < MAX_DM_LINES) ? c->count : MAX_DM_LINES;
    int32_t  rxCh  = c->rxChanIdx;

    f.write((const uint8_t *)&magic, 4);
    f.write((const uint8_t *)&nid, 4);
    f.write((const uint8_t *)c->shortName, 5);
    f.write((const uint8_t *)&cnt, 4);
    f.write((const uint8_t *)&nLines, 4);
    f.write((const uint8_t *)&rxCh, 4);

    // Lines: write in order from oldest to newest
    int startIdx = (c->count <= MAX_DM_LINES) ? 0 : (c->count % MAX_DM_LINES);
    size_t written = 0;
    for (int i = 0; i < nLines; i++) {
        int idx = (startIdx + i) % MAX_DM_LINES;
        PersistDmLine pl = {};
        utf8util::copyTruncate(pl.text, sizeof(pl.text), c->lines[idx].text);
        pl.color = c->lines[idx].color;
        pl.epoch = c->lines[idx].epoch;
        written += f.write((const uint8_t *)&pl, sizeof(PersistDmLine));
    }

    f.flush();
    f.close();
    debugLogMessages("[dm] saved %s: %d lines (%u bytes)\n", path, (int)nLines, (unsigned)(25 + written));
#endif
}

void DmMgr::loadAll() {
#if !HAS_SD_CARD
    return;
#else
    // Ensure DM storage directories exist before opening to avoid noisy VFS errors.
    SD.mkdir("/camillia");
    if (!SD.exists(kDmDir)) {
        SD.mkdir(kDmDir);
        debugLogMessages("[dm] loadAll: created %s\n", kDmDir);
        return;
    }

    File dir = SD.open(kDmDir);
    if (!dir || !dir.isDirectory()) {
        debugLogMessages("[dm] loadAll: no %s directory\n", kDmDir);
        return;
    }

    File f = dir.openNextFile();
    while (f) {
        if (f.isDirectory() || f.size() < 25) {
            f.close();
            f = dir.openNextFile();
            continue;
        }

        // Read header
        uint32_t magic;
        f.read((uint8_t *)&magic, 4);
        if (magic != DM_MAGIC) {
            debugLogMessages("[dm] loadAll: bad magic in %s\n", f.name());
            f.close(); f = dir.openNextFile(); continue;
        }

        uint32_t nodeId;
        char persistedShortName[5] = {};
        char shortName[5] = {};
        int32_t count, numLines, rxChanIdx;

        f.read((uint8_t *)&nodeId, 4);
        f.read((uint8_t *)persistedShortName, 5);
        utf8util::copyTruncateBytes(shortName,
                        sizeof(shortName),
                        (const uint8_t *)persistedShortName,
                        sizeof(persistedShortName));
        f.read((uint8_t *)&count, 4);
        f.read((uint8_t *)&numLines, 4);
        f.read((uint8_t *)&rxChanIdx, 4);

        debugLogMessages("[dm] loading %08X (%s): count=%d numLines=%d\n",
                 nodeId, shortName, (int)count, (int)numLines);

        if (numLines <= 0 || numLines > MAX_DM_LINES) {
            f.close(); f = dir.openNextFile(); continue;
        }

        DmConv *c = findOrCreate(nodeId, shortName);
        if (!c || !c->lines) { f.close(); f = dir.openNextFile(); continue; }

        c->rxChanIdx = rxChanIdx;

        // Read lines directly into the circular buffer
        for (int i = 0; i < numLines; i++) {
            PersistDmLine pl = {};
            if (f.read((uint8_t *)&pl, sizeof(PersistDmLine)) != sizeof(PersistDmLine)) break;
            DmLine line = {};
            utf8util::copyTruncate(line.text, sizeof(line.text), pl.text);
            line.color = pl.color;
            line.packetId = 0;
            line.ack = DmLine::NONE;
            line.epoch = pl.epoch;
            int idx = i % MAX_DM_LINES;
            c->lines[idx] = line;
        }
        c->count = count;

        // Set lastText from the newest line
        if (numLines > 0) {
            int newest = (count - 1) % MAX_DM_LINES;
            utf8util::copyTruncate(c->lastText, sizeof(c->lastText), c->lines[newest].text);
        }

        debugLogMessages("[dm] loaded %08X: %d lines\n", nodeId, (int)numLines);

        f.close();
        f = dir.openNextFile();
    }
    dir.close();

    _sort();
    debugLogMessages("[dm] loadAll done: %d conversations\n", _count);
#endif
}
