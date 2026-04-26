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
    if (chanIdx >= 0) c->rxChanIdx = chanIdx;

    // Combine prefix + text then word-wrap at DM_LINE_LEN
    char full[512];
    int prefixLen = prefix ? strlen(prefix) : 0;
    snprintf(full, sizeof(full), "%s%s", prefix ? prefix : "", text);
    int len = strlen(full);

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

        _pushLine(*c, lineBuf, color);

        pos += take;
        while (pos < len && full[pos] == ' ') pos++;
        firstLine = false;
    }

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

    size_t protoLen = encodeTextMessage(text, proto, sizeof(proto));
    debugLogMessages("[dm] protoLen=%u\n", (unsigned)protoLen);
    if (protoLen == 0) {
        debugLogMessages("[dm] sendDm FAIL: encode failed\n");
        return false;
    }

    MeshHdr hdr = {};
    hdr.to    = toNodeId;
    hdr.from  = myNodeId;
    hdr.id    = packetId;
    hdr.flags = (1 << 3) |  // want_ack
                (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                ((MESH_HOP_LIMIT & 0x07) << 5);
    hdr.relay_node = (uint8_t)(myNodeId & 0xFF);

    size_t payloadLen = 0;
    bool usedPki = false;

    // Prefer PKI if we have the recipient's Curve25519 public key
    NodeEntry *node = Nodes.find(toNodeId);
    debugLogMessages("[dm] node=%s  hasPubKey=%d\n",
                     node ? "found" : "null", node ? (int)node->hasPubKey : -1);

    if (node && node->hasPubKey) {
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
    } else {
        // Fall back to channel-key encryption
        DmConv *conv = find(toNodeId);
        int chanIdx = (conv && conv->rxChanIdx >= 0) ? conv->rxChanIdx : 0;
        const ChannelKey &ck = CHANNEL_KEYS[chanIdx];
        hdr.channel = ck.hash;
        debugLogMessages("[dm] using chan-key path: chanIdx=%d (%s) keyLen=%d hash=0x%02X\n",
                 chanIdx, ck.name, ck.keyLen, ck.hash);

        if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                            proto, cipher, protoLen)) {
            debugLogMessages("[dm] sendDm FAIL: encrypt failed\n");
            addLiveDmLine("T DM ER enc", TFT_RED);
            return false;
        }
        payloadLen = protoLen;
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

    debugLogMessages("[dm] TX OK\n");
    {
        DmConv *conv = find(toNodeId);
        char who[16];
        liveNodeLabelWithHint(toNodeId, conv ? conv->shortName : nullptr, who, sizeof(who));
        char live[64];
        snprintf(live, sizeof(live), "T DM %s %s %08X",
                 usedPki ? "PKI" : "CHAN",
                 who,
                 packetId);
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
