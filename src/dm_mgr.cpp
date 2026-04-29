#include "dm_mgr.h"
#include "live_util.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "node_db.h"
#include "channel_mgr.h"
#include "lgfx_tdeck.h"
#include "debug_flags.h"
#include "config.h"
#include <esp_heap_caps.h>
#include <string.h>
#include <SD.h>

DmMgr DMs;

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
    strncpy(c->shortName, shortName ? shortName : "????", sizeof(c->shortName) - 1);
    c->unread = false;

    // Allocate line buffer from PSRAM with regular heap fallback
    c->lines = (DmLine *)heap_caps_malloc(MAX_DM_LINES * sizeof(DmLine),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!c->lines)
        c->lines = (DmLine *)malloc(MAX_DM_LINES * sizeof(DmLine));
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

void DmMgr::markRead(uint32_t nodeId) {
    DmConv *c = find(nodeId);
    if (c) c->unread = false;
}

// ── _sort: insertion sort by lastMs descending ────────────────
void DmMgr::_sort() {
    for (int i = 1; i < _count; i++) {
        DmConv tmp = _convs[i];
        int j = i - 1;
        while (j >= 0 && _convs[j].lastMs < tmp.lastMs) {
            _convs[j + 1] = _convs[j];
            j--;
        }
        _convs[j + 1] = tmp;
    }
}

// ── _pushLine: append one rendered line ───────────────────────
void DmMgr::_pushLine(DmConv &c, const char *text, uint16_t color) {
    if (!c.lines) return;
    int idx = c.count % MAX_DM_LINES;
    strncpy(c.lines[idx].text, text, DM_LINE_LEN);
    c.lines[idx].text[DM_LINE_LEN] = '\0';
    c.lines[idx].color = color;
    c.count++;
}

// ── addMessage ────────────────────────────────────────────────
void DmMgr::addMessage(uint32_t nodeId, const char *shortName,
                        const char *prefix, const char *text, uint16_t color,
                        bool markUnread, int chanIdx) {
    DmConv *c = findOrCreate(nodeId, shortName);
    if (!c) return;

    if (shortName && shortName[0])
        strncpy(c->shortName, shortName, sizeof(c->shortName) - 1);

    strncpy(c->lastText, text, DM_LINE_LEN);
    c->lastText[DM_LINE_LEN] = '\0';
    c->lastMs = millis();
    if (markUnread) c->unread = true;
    if (chanIdx >= 0 && chanIdx < MESH_CHANNELS) c->rxChanIdx = chanIdx;

    // Combine prefix + text then word-wrap at DM_LINE_LEN
    char full[512];
    int prefixLen = prefix ? strlen(prefix) : 0;
    snprintf(full, sizeof(full), "%s%s", prefix ? prefix : "", text);
    int len = strlen(full);
    static constexpr int MAX_WRAP_LINES = 64;
    char *wrapped = (char *)heap_caps_malloc(MAX_WRAP_LINES * (DM_LINE_LEN + 1),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wrapped)
        wrapped = (char *)malloc(MAX_WRAP_LINES * (DM_LINE_LEN + 1));
    int wrappedCount = 0;

    auto wrappedLine = [&](int idx) -> char * {
        return wrapped + (idx * (DM_LINE_LEN + 1));
    };

    if (len == 0) {
        _pushLine(*c, "", color);
        c->scrollOff = 0;
        _sort();
        return;
    }

    int pos = 0;
    bool firstLine = true;
    while (pos < len) {
        int remain = len - pos;
        int take   = (remain <= DM_LINE_LEN) ? remain : DM_LINE_LEN;

        // Try to break at a word boundary
        if (take < remain && full[pos + take] != ' ' && full[pos + take - 1] != ' ') {
            int bp = take - 1;
            while (bp > 0 && full[pos + bp] != ' ') bp--;
            if (bp > 0) take = bp + 1;
        }

        char lineBuf[DM_LINE_LEN + 1];
        if (firstLine) {
            strncpy(lineBuf, full + pos, take);
            lineBuf[take] = '\0';
        } else {
            // Indent continuation lines to align under the message text
            int indent = (prefixLen < DM_LINE_LEN) ? prefixLen : 0;
            int msgTake = take;
            if (indent + msgTake > DM_LINE_LEN) msgTake = DM_LINE_LEN - indent;
            snprintf(lineBuf, sizeof(lineBuf), "%*s%.*s", indent, "", msgTake, full + pos);
        }

        // Trim trailing spaces
        int end = (int)strlen(lineBuf) - 1;
        while (end >= 0 && lineBuf[end] == ' ') lineBuf[end--] = '\0';

        if (wrapped && wrappedCount < MAX_WRAP_LINES) {
            char *dst = wrappedLine(wrappedCount);
            strncpy(dst, lineBuf, DM_LINE_LEN);
            dst[DM_LINE_LEN] = '\0';
            wrappedCount++;
        } else if (!wrapped) {
            // Low-memory fallback: keep chat usable even if wrapping cache can't be allocated.
            _pushLine(*c, lineBuf, color);
        }

        pos += take;
        while (pos < len && full[pos] == ' ') pos++;
        firstLine = false;
    }

    // Conversation view is newest-at-top; reverse insertion keeps wrapped
    // messages readable from top to bottom.
    for (int i = wrappedCount - 1; i >= 0; i--) {
        _pushLine(*c, wrappedLine(i), color);
    }

    if (wrapped) free(wrapped);

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

    uint32_t packetId = esp_random() ^ millis();
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

    // getByRank() sorts NodeDB in place; reacquire pointer after remap loop.
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

    // Prefer PKI if we have the recipient's Curve25519 public key
    debugLogMessages("[dm] node=%s  hasPubKey=%d\n",
                     node ? "found" : "null", node ? (int)node->hasPubKey : -1);

    // Match modern Meshtastic behavior for direct messages:
    // no legacy channel fallback for TEXT DMs. If peer pubkey is unknown,
    // request NODEINFO and fail fast.
    bool usePki = node && node->hasPubKey;

    if (!usePki) {
        DmConv *conv = find(toNodeId);
        char who[16];
        liveNodeLabelWithHint(resolvedToNodeId, conv ? conv->shortName : nullptr, who, sizeof(who));
        char live[72];
        snprintf(live, sizeof(live), "T DM ER noPK %s t%08X", who, resolvedToNodeId);
        addLiveDmLine(live, TFT_RED);

        const uint32_t now = millis();
        if (!node || now - node->lastSentInfoMs > 5000) {
            NodeEntry *me = Nodes.find(myNodeId);
            const char *myLong = (me && me->longName[0]) ? me->longName : MY_LONG_NAME;
            const char *myShort = (me && me->shortName[0]) ? me->shortName : MY_SHORT_NAME;
            bool reqOk = Channels.sendNodeInfo(myNodeId, myLong, myShort, resolvedToNodeId, true);
            if (node && reqOk) node->lastSentInfoMs = now;
            debugLogMessages("[dm] no pubkey for !%08X, requested NODEINFO (%s)\n",
                             resolvedToNodeId, reqOk ? "sent" : "failed");
        } else {
            debugLogMessages("[dm] no pubkey for !%08X, NODEINFO request throttled\n",
                             resolvedToNodeId);
        }
        return false;
    }

    if (usePki) {
        hdr.channel = 0; // PKI marker (channel 0 = not a channel-key hash)
        usedPki = true;
        debugLogMessages("[dm] using PKI path\n");
        if (!encryptPki(packetId, myNodeId, node->pubKey, proto, protoLen, cipher)) {
            debugLogMessages("[dm] sendDm FAIL: PKI encrypt failed\n");
            addLiveDmLine("T DM ER pki", TFT_RED);
            return false;
        }
        payloadLen = protoLen + 12; // ciphertext + tag(8) + extraNonce(4)
        debugLogMessages("[dm] PKI encrypt OK, payloadLen=%u\n", (unsigned)payloadLen);
    }

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
        debugLogMessages("[dm] track tx: slot=%d req=%08X to=!%08X mode=%s pkiNoChannel=%d legacyNoChan=%d\n",
                         slot, packetId, resolvedToNodeId,
                         usedPki ? "PKI" : "CHAN",
                 (node && node->hasPubKey) ? (int)node->pkiNoChannel : -1,
                 (node && node->hasPubKey) ? (int)node->legacyDmNoChannel : -1);
    }

    debugLogMessages("[dm] TX OK\n");
    {
        DmConv *conv = find(toNodeId);
        char who[16];
        liveNodeLabelWithHint(resolvedToNodeId, conv ? conv->shortName : nullptr, who, sizeof(who));
        char live[72];
        snprintf(live, sizeof(live), "T DM PKI %s %08X t%08X",
                 who, packetId, resolvedToNodeId);
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
        addMessage(toNodeId, conv->shortName, prefix, text, TFT_WHITE, false);
    }
    return true;
}

bool DmMgr::handleRoutingResult(uint32_t fromNodeId, uint32_t requestId, uint32_t errorReason) {
    int match = -1;
    bool usedIdOnlyFallback = false;
    int activeTx = 0;
    for (int i = 0; i < MAX_DM_PENDING_TX; i++) {
        if (_pendingTx[i].active) activeTx++;
    }

    debugLogMessages("[dm] routing result: from=!%08X req=%08X err=%lu activeTx=%d\n",
                     fromNodeId, requestId, (unsigned long)errorReason, activeTx);

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
                usedIdOnlyFallback = true;
                break;
            }
        }
    }

    if (match < 0) {
        debugLogMessages("[dm] routing result unmatched: from=!%08X req=%08X err=%lu\n",
                         fromNodeId, requestId, (unsigned long)errorReason);
        return false;
    }

    const bool usedPki = _pendingTx[match].usedPki;
    const uint32_t trackedNodeId = _pendingTx[match].nodeId;
    const uint32_t ageMs = millis() - _pendingTx[match].sentMs;
    debugLogMessages("[dm] routing match: slot=%d kind=%s trackedTo=!%08X age=%lums mode=%s\n",
                     match,
                     usedIdOnlyFallback ? "req-only" : "exact",
                     trackedNodeId,
                     (unsigned long)ageMs,
                     usedPki ? "PKI" : "CHAN");

    _pendingTx[match].active = false;

    NodeEntry *nTracked = Nodes.find(trackedNodeId);
    NodeEntry *nFrom = Nodes.find(fromNodeId);
    NodeEntry *n = nTracked ? nTracked : nFrom;
    if (!n) {
        debugLogMessages("[dm] routing match had no node entry for !%08X\n", fromNodeId);
        return true;
    }

    NodeEntry *aliasNode = nullptr;
    if (nTracked && nFrom && nTracked != nFrom) {
        if (nTracked->shortName[0] && nFrom->shortName[0] &&
            strncmp(nTracked->shortName, nFrom->shortName, sizeof(nTracked->shortName) - 1) == 0) {
            aliasNode = nFrom;
            debugLogMessages("[dm] routing alias: tracked=!%08X from=!%08X short=%s\n",
                             trackedNodeId, fromNodeId, nTracked->shortName);
        } else {
            debugLogMessages("[dm] routing id mismatch: tracked=!%08X from=!%08X\n",
                             trackedNodeId, fromNodeId);
        }
    }

    if (errorReason == 6) { // NO_CHANNEL
        if (usedPki) {
            // If the peer already rejected legacy channel DMs, keep trying PKI
            // to avoid bouncing between two guaranteed-fail modes.
            if (n->legacyDmNoChannel && n->hasPubKey) {
                n->pkiNoChannel = false;
                debugLogMessages("[dm] NAK NO_CHANNEL on PKI DM to !%08X but legacy DM rejected -> keep PKI\n", fromNodeId);
            } else {
                n->pkiNoChannel = true;
                debugLogMessages("[dm] NAK NO_CHANNEL on PKI DM to !%08X -> disable PKI for node\n", fromNodeId);
            }
            if (aliasNode) {
                aliasNode->pkiNoChannel = n->pkiNoChannel;
                aliasNode->legacyDmNoChannel = n->legacyDmNoChannel;
            }
        } else {
            // Modern firmware can reject legacy channel-encrypted DMs with
            // NO_CHANNEL; stick to PKI after observing this once.
            n->legacyDmNoChannel = true;
            n->pkiNoChannel = false;
            n->chanIdx = -1;
            n->hasChanHash = false;
            n->chanHash = 0;
            if (aliasNode) {
                aliasNode->legacyDmNoChannel = true;
                aliasNode->pkiNoChannel = false;
                aliasNode->chanIdx = -1;
                aliasNode->hasChanHash = false;
                aliasNode->chanHash = 0;
            }
            debugLogMessages("[dm] NAK NO_CHANNEL on channel DM to !%08X -> mark legacy DM rejected, clear channel hash, prefer PKI\n", fromNodeId);
        }
    } else if (errorReason == 0) {
        // ACK: stay on the mode that worked.
        n->pkiNoChannel = usedPki ? false : true;
        n->legacyDmNoChannel = false;
        if (aliasNode) {
            aliasNode->pkiNoChannel = n->pkiNoChannel;
            aliasNode->legacyDmNoChannel = false;
        }
    }

    debugLogMessages("[dm] routing apply: applyNode=!%08X from=!%08X err=%lu mode=%s -> pkiNoChannel=%d legacyNoChan=%d\n",
                     n->nodeId,
                     fromNodeId,
                     (unsigned long)errorReason,
                     usedPki ? "PKI" : "CHAN",
                     (int)n->pkiNoChannel,
                     (int)n->legacyDmNoChannel);

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
static const uint32_t DM_MAGIC = 0x434D444D;  // "CMDM"

void DmMgr::saveConv(const DmConv *c) {
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
        written += f.write((const uint8_t *)&c->lines[idx], sizeof(DmLine));
    }

    f.flush();
    f.close();
    debugLogMessages("[dm] saved %s: %d lines (%u bytes)\n", path, (int)nLines, (unsigned)(25 + written));
}

void DmMgr::loadAll() {
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
        char shortName[5] = {};
        int32_t count, numLines, rxChanIdx;

        f.read((uint8_t *)&nodeId, 4);
        f.read((uint8_t *)shortName, 5);
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
            DmLine line;
            if (f.read((uint8_t *)&line, sizeof(DmLine)) != sizeof(DmLine)) break;
            int idx = i % MAX_DM_LINES;
            c->lines[idx] = line;
        }
        c->count = count;

        // Set lastText from the newest line
        if (numLines > 0) {
            int newest = (count - 1) % MAX_DM_LINES;
            strncpy(c->lastText, c->lines[newest].text, DM_LINE_LEN);
            c->lastText[DM_LINE_LEN] = '\0';
        }

        debugLogMessages("[dm] loaded %08X: %d lines\n", nodeId, (int)numLines);

        f.close();
        f = dir.openNextFile();
    }
    dir.close();

    _sort();
    debugLogMessages("[dm] loadAll done: %d conversations\n", _count);
}
