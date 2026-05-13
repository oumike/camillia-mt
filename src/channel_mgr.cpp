#include "channel_mgr.h"
#include "live_util.h"
#include "mesh_radio.h"
#include "lgfx_tdeck.h"
#include "debug_flags.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include <SD.h>
#include <stdlib.h>

namespace {
constexpr const char *kChanPersistDir = "/camillia/channels";
constexpr int kPersistMaxLines = 100;

static void channelPersistPath(int chanIdx, char *out, size_t outLen) {
    snprintf(out, outLen, "%s/ch%d.log", kChanPersistDir, chanIdx);
}
}

ChannelMgr Channels;

static DisplayLine *allocChannelLines() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return (DisplayLine *)malloc(MAX_MSG_LINES * sizeof(DisplayLine));
#else
    DisplayLine *lines = (DisplayLine *)heap_caps_malloc(
        MAX_MSG_LINES * sizeof(DisplayLine),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (lines) return lines;
    Serial.println("[chanmgr] PSRAM alloc failed, falling back to DRAM");
    return (DisplayLine *)malloc(MAX_MSG_LINES * sizeof(DisplayLine));
#endif
}

static void addLiveTxLine(const char *text, uint16_t color = TFT_DARKGREY) {
    char prefix[12];
    liveBuildPrefix(prefix, sizeof(prefix));
    Channels.addMessage(CHAN_ANN, prefix, text, color);
}

void ChannelMgr::init() {
    memset(_pending, 0, sizeof(_pending));
    for (int i = 0; i < MAX_CHANNELS; i++) {
        _chans[i].name      = CHANNEL_KEYS[i].name;
        _chans[i].count     = 0;
        _chans[i].scrollOff = 0;
        _chans[i].unread    = false;
        _chans[i].active    = true;
        _chans[i].lines = allocChannelLines();
        if (!_chans[i].lines)
            Serial.printf("[chanmgr] alloc failed for ch%d\n", i);
    }
    addMessage(CHAN_ANN, "", "Live channel ready", TFT_DARKGREY, 0);
    setActive(0);
}

void ChannelMgr::setActive(int idx) {
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    _active = idx;
    _chans[idx].unread    = false;
    _chans[idx].scrollOff = 0;   // snap to latest-at-top
}

void ChannelMgr::clearChannel(int idx) {
    if (idx < 0 || idx >= MAX_CHANNELS) return;
    _chans[idx].count = 0;
    _chans[idx].scrollOff = 0;
    _chans[idx].unread = false;
}

void ChannelMgr::clearAllMessages(bool clearPersisted) {
    memset(_pending, 0, sizeof(_pending));
    for (int i = 0; i < MAX_CHANNELS; i++) {
        clearChannel(i);
    }

#if HAS_SD_CARD
    if (!clearPersisted) return;

    for (int chanIdx = 0; chanIdx < MESH_CHANNELS; chanIdx++) {
        char path[48];
        channelPersistPath(chanIdx, path, sizeof(path));
        if (SD.exists(path)) SD.remove(path);
    }
    if (SD.exists(kChanPersistDir)) {
        (void)SD.rmdir(kChanPersistDir);
    }
    _persistDirReady = false;
#else
    (void)clearPersisted;
#endif
}

void ChannelMgr::nextChannel() { setActive((_active + 1) % MAX_CHANNELS); }
void ChannelMgr::prevChannel() { setActive((_active + MAX_CHANNELS - 1) % MAX_CHANNELS); }

void ChannelMgr::_pushLine(int chanIdx, Channel &ch, const char *text, uint16_t color,
                            uint32_t packetId, DisplayLine::AckState ack) {
    int idx = ch.count % MAX_MSG_LINES;
    DisplayLine &dl = ch.lines[idx];
    strncpy(dl.text, text, MSG_CHARS);
    dl.text[MSG_CHARS] = '\0';
    dl.color    = color;
    dl.packetId = packetId;
    dl.ack      = ack;
    ch.count++;

    if (_persistReady && !_persistLoading && chanIdx >= 0 && chanIdx < MESH_CHANNELS) {
        _persistChannel(chanIdx, ch);
    }
}

int ChannelMgr::addMessage(int chanIdx, const char *prefix, const char *text,
                            uint16_t color, uint32_t packetId) {
    if (chanIdx < 0 || chanIdx >= MAX_CHANNELS) return -1;
    int firstLine = _chans[chanIdx].count;
    _wordWrap(chanIdx, prefix, text, color, packetId);
    if (chanIdx != _active) _chans[chanIdx].unread = true;
    return firstLine;
}

void ChannelMgr::_wordWrap(int chanIdx, const char *prefix, const char *text,
                             uint16_t color, uint32_t packetId) {
    Channel &ch = _chans[chanIdx];
    char line[MSG_CHARS + 1];
    static constexpr int MAX_WRAP_LINES = 64;
    uint16_t segPos[MAX_WRAP_LINES];
    uint16_t segLen[MAX_WRAP_LINES];
    int segCount = 0;
    int  prefixLen = strlen(prefix);
    int  textLen   = strlen(text);
    int  pos       = 0;          // position in text
    bool firstLine = true;
    const int CONT_INDENT = 2;   // spaces for continuation lines

    // Preserve existing behavior for empty message bodies.
    if (textLen == 0) {
        if (prefixLen > 0) {
            int copy = min(prefixLen, MSG_CHARS);
            memcpy(line, prefix, copy);
            line[copy] = '\0';
        } else {
            line[0] = '\0';
        }
        DisplayLine::AckState ack = packetId ? DisplayLine::PENDING : DisplayLine::NONE;
        _pushLine(chanIdx, ch, line, color, packetId, ack);
        return;
    }

    while (pos < textLen) {
        int lineStart = firstLine ? prefixLen : CONT_INDENT;
        int avail     = MSG_CHARS - lineStart;
        if (avail <= 0) avail = 1;

        int remaining = textLen - pos;
        int take = (remaining <= avail) ? remaining : avail;

        // Don't break in the middle of a word if possible
        if (take < remaining && take > 0) {
            int bp = take;
            while (bp > 0 && text[pos + bp] != ' ' && text[pos + bp - 1] != ' ')
                bp--;
            if (bp > 0) take = bp;
        }

        if (segCount < MAX_WRAP_LINES) {
            segPos[segCount] = (uint16_t)pos;
            segLen[segCount] = (uint16_t)take;
            segCount++;
        }

        pos += take;
        if (pos < textLen && text[pos] == ' ') pos++; // skip space after wrap
        firstLine = false;
    }

    // UI is newest-at-top; push wrapped lines in reverse so each long message
    // still reads top-down (first line first, continuation lines below).
    for (int i = segCount - 1; i >= 0; i--) {
        int w = 0;
        if (i == 0) {
            int pCopy = min(prefixLen, MSG_CHARS);
            if (pCopy > 0) {
                memcpy(line + w, prefix, pCopy);
                w += pCopy;
            }
        } else {
            int pad = min(CONT_INDENT, MSG_CHARS);
            for (int s = 0; s < pad; s++) line[w++] = ' ';
        }

        int room = MSG_CHARS - w;
        int copy = min((int)segLen[i], room);
        if (copy > 0) {
            memcpy(line + w, text + segPos[i], copy);
            w += copy;
        }
        line[w] = '\0';

        bool logicalFirst = (i == 0);
        DisplayLine::AckState ack = (logicalFirst && packetId)
            ? DisplayLine::PENDING
            : DisplayLine::NONE;
        _pushLine(chanIdx, ch, line, color, packetId, ack);
    }
}

void ChannelMgr::beginPersistence() {
#if HAS_SD_CARD
    _persistReady = true;
#else
    _persistReady = false;
#endif
}

void ChannelMgr::_persistChannel(int chanIdx, const Channel &ch) {
#if HAS_SD_CARD
    if (!_persistDirReady) {
        (void)SD.mkdir("/camillia");
        (void)SD.mkdir(kChanPersistDir);
        _persistDirReady = true;
    }

    char path[48];
    channelPersistPath(chanIdx, path, sizeof(path));

    // Rewrite bounded snapshot to keep storage and restore behavior predictable.
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;

    int stored = min(ch.count, MAX_MSG_LINES);
    if (stored <= 0) {
        f.close();
        return;
    }
    int keep = min(stored, kPersistMaxLines);
    int oldest = ch.count - stored;
    int first = ch.count - keep;
    if (first < oldest) first = oldest;

    for (int lineIdx = first; lineIdx < ch.count; lineIdx++) {
        const DisplayLine &dl = ch.lines[lineIdx % MAX_MSG_LINES];
        char text[MSG_CHARS + 1];
        strncpy(text, dl.text, MSG_CHARS);
        text[MSG_CHARS] = '\0';
        for (int i = 0; text[i]; i++) {
            if (text[i] == '\n' || text[i] == '\r') text[i] = ' ';
        }

        f.printf("%04X|%08lX|%u|%s\n",
                 (unsigned)dl.color,
                 (unsigned long)dl.packetId,
                 (unsigned)dl.ack,
                 text);
    }
    f.close();
#else
    (void)chanIdx;
    (void)ch;
#endif
}

void ChannelMgr::loadPersisted() {
#if HAS_SD_CARD
    if (!_persistReady) return;

    _persistLoading = true;
    for (int chanIdx = 0; chanIdx < MESH_CHANNELS; chanIdx++) {
        char path[48];
        channelPersistPath(chanIdx, path, sizeof(path));
        if (!SD.exists(path)) continue;

        int totalLines = 0;
        {
            File countFile = SD.open(path, FILE_READ);
            if (!countFile) continue;
            while (countFile.available()) {
                int c = countFile.read();
                if (c == '\n') totalLines++;
            }
            countFile.close();
        }
        int skipLines = max(0, totalLines - kPersistMaxLines);

        File f = SD.open(path, FILE_READ);
        if (!f) continue;

        char line[192];
        int lineNo = 0;
        while (f.available()) {
            size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            if (n == 0) continue;
            lineNo++;
            if (lineNo <= skipLines) continue;

            while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
                line[n - 1] = '\0';
                n--;
            }
            if (n == 0) continue;

            char *sep1 = strchr(line, '|');
            if (!sep1) continue;
            *sep1 = '\0';
            uint16_t color = (uint16_t)strtoul(line, nullptr, 16);

            char *rest = sep1 + 1;
            char *sep2 = strchr(rest, '|');
            if (!sep2) {
                // Backward-compatible format: color|text
                const char *text = rest;
                if (!text[0]) continue;
                _pushLine(chanIdx, _chans[chanIdx], text, color, 0, DisplayLine::NONE);
                continue;
            }

            *sep2 = '\0';
            char *sep3 = strchr(sep2 + 1, '|');
            if (!sep3) continue;
            *sep3 = '\0';

            uint32_t packetId = (uint32_t)strtoul(rest, nullptr, 16);
            uint8_t ackRaw = (uint8_t)strtoul(sep2 + 1, nullptr, 10);
            if (ackRaw > DisplayLine::TX_FAILED) ackRaw = DisplayLine::NONE;
            const char *text = sep3 + 1;
            if (!text[0]) continue;

            _pushLine(chanIdx, _chans[chanIdx], text, color,
                      packetId, (DisplayLine::AckState)ackRaw);
        }

        f.close();
    }
    _persistLoading = false;
#endif
}

const DisplayLine *ChannelMgr::getLine(int chanIdx, int row) const {
    const Channel &ch = _chans[chanIdx];
    int total = ch.count;
    if (total == 0) return nullptr;
    // row 0 = newest visible line at the top; larger rows are older lines.
    int stored = (total < MAX_MSG_LINES) ? total : MAX_MSG_LINES;
    int oldest = total - stored;
    int newest = total - 1 - ch.scrollOff;
    if (newest < oldest) return nullptr;
    int lineIdx = newest - row;
    if (lineIdx < oldest || lineIdx >= total) return nullptr;
    return &ch.lines[lineIdx % MAX_MSG_LINES];
}

void ChannelMgr::setAckState(uint32_t packetId, DisplayLine::AckState state) {
    // Find pending ack entry
    for (int i = 0; i < MAX_PENDING_ACK; i++) {
        if (_pending[i].active && _pending[i].packetId == packetId) {
            _pending[i].active = false;
            // Update every wrapped line for this packet so ACK color/status
            // applies to the full message block, not just the first segment.
            Channel &ch = _chans[_pending[i].chanIdx];
            int chanIdx = _pending[i].chanIdx;
            for (int j = 0; j < min(ch.count, MAX_MSG_LINES); j++) {
                if (ch.lines[j].packetId == packetId) {
                    ch.lines[j].ack = state;
                }
            }
            if (_persistReady && !_persistLoading && chanIdx >= 0 && chanIdx < MESH_CHANNELS) {
                _persistChannel(chanIdx, ch);
            }
            return;
        }
    }
}

void ChannelMgr::setAckStateFrom(uint32_t packetId, uint32_t fromNodeId) {
    for (int i = 0; i < MAX_PENDING_ACK; i++) {
        if (_pending[i].active && _pending[i].packetId == packetId) {
            bool isDirect = (_pending[i].destNodeId == 0xFFFFFFFF) ||
                            (_pending[i].destNodeId == fromNodeId);
            setAckState(packetId, isDirect ? DisplayLine::ACKED : DisplayLine::ACKED_RELAY);
            return;
        }
    }
}

bool ChannelMgr::expireAcks() {
    uint32_t now = millis();
    bool changed = false;
    static constexpr uint32_t kBroadcastConfirmMs = 1500;
    for (int i = 0; i < MAX_PENDING_ACK; i++) {
        if (!_pending[i].active) continue;
        uint32_t ageMs = now - _pending[i].sentMs;
        if (_pending[i].destNodeId == 0xFFFFFFFF) {
            // Channel text is broadcast; many peers do not return explicit routing ACKs.
            // After a short settle window, treat successful RF TX as confirmed for UI status.
            if (ageMs > kBroadcastConfirmMs) {
                setAckState(_pending[i].packetId, DisplayLine::ACKED);
                changed = true;
            }
        } else {
            if (ageMs > ACK_TIMEOUT_MS) {
                setAckState(_pending[i].packetId, DisplayLine::NAKED);
                changed = true;
            }
        }
    }
    return changed;
}

bool ChannelMgr::sendText(uint32_t myNodeId, const char *text, bool okToMqtt,
                          int chanIdx) {
    if (!Radio.isReady()) {
        addLiveTxLine("T TXT B NR", TFT_RED);
        return false;
    }

    int txChan = (chanIdx >= 0 && chanIdx < MESH_CHANNELS) ? chanIdx : _active;
    if (txChan < 0 || txChan >= MESH_CHANNELS) return false;
    if (_active != txChan) setActive(txChan);

    uint32_t packetId = esp_random() ^ millis();
    uint8_t  proto[256], cipher[256];

    uint32_t bitfield = okToMqtt ? 0x01 : 0;
    size_t protoLen = encodeTextMessage(text, proto, sizeof(proto), bitfield);
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[txChan];
    const char *txName = ck.name_buf[0] ? ck.name_buf : ck.name;
    uint8_t effectiveKeyLen = (ck.keyLen == 1 && ck.key[0] == 0x00) ? 0 : ck.keyLen;
    debugLogMessages("[tx] ch%d name='%s' keyLen=%u effectiveKeyLen=%u hash=0x%02X\n",
                     txChan, txName ? txName : "", ck.keyLen, effectiveKeyLen, ck.hash);
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    // Build MeshHdr
    uint8_t frame[sizeof(MeshHdr) + 256];
    MeshHdr hdr = {};
    hdr.to      = 0xFFFFFFFF;     // broadcast
    hdr.from    = myNodeId;
    hdr.id      = packetId;
    hdr.channel = CHANNEL_KEYS[txChan].hash;
    hdr.flags   = (1 << 3) |      // want_ack
                  (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                  ((MESH_HOP_LIMIT & 0x07) << 5);
    hdr.relay_node = (uint8_t)(myNodeId & 0xFF);
    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, protoLen);
    size_t frameLen = sizeof(hdr) + protoLen;

    char timePrefix[12];
    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "%s<me> ", timePrefix);

    if (!Radio.transmit(frame, frameLen)) {
        addLiveTxLine("T TXT B ER", TFT_RED);
        addMessage(txChan, prefix, text, TFT_RED, packetId);
        Channel &ch = _chans[txChan];
        for (int j = 0; j < min(ch.count, MAX_MSG_LINES); j++) {
            if (ch.lines[j].packetId == packetId) {
                ch.lines[j].ack = DisplayLine::TX_FAILED;
            }
        }
        return false;
    }

    {
        char live[56];
        snprintf(live, sizeof(live), "T TXT B c%d %08X",
                 txChan, packetId);
        addLiveTxLine(live, TFT_WHITE);
    }

    // Add to display first so ACK updates always have a visible line to target.
    int firstLine = addMessage(txChan, prefix, text, TFT_WHITE, packetId);

    // Register ACK tracking
    for (int i = 0; i < MAX_PENDING_ACK; i++) {
        if (!_pending[i].active) {
            _pending[i] = { packetId, millis(), txChan, firstLine, 0xFFFFFFFF, true };
            break;
        }
    }

    return true;
}

bool ChannelMgr::sendPosition(uint32_t myNodeId, int32_t latI, int32_t lonI, int32_t alt,
                              bool unusedCompat) {
    (void)unusedCompat;
    if (!Radio.isReady()) return false;

    uint8_t proto[64], cipher[64];
    size_t protoLen = encodePosition(latI, lonI, alt, proto, sizeof(proto));
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0]; // always LongFast
    uint32_t packetId = esp_random() ^ millis();
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    uint8_t frame[sizeof(MeshHdr) + 64];
    MeshHdr hdr = {};
    hdr.to      = 0xFFFFFFFF;
    hdr.from    = myNodeId;
    hdr.id      = packetId;
    hdr.channel = ck.hash;
    hdr.flags   = (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                  ((MESH_HOP_LIMIT & 0x07) << 5);
    hdr.relay_node = (uint8_t)(myNodeId & 0xFF);
    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, protoLen);

    bool ok = Radio.transmit(frame, sizeof(hdr) + protoLen);
    debugLogMessages("[position] transmit %s\n", ok ? "OK" : "FAILED");
    {
        char live[56];
        snprintf(live, sizeof(live), "T POS B %08X %s",
                 packetId, ok ? "OK" : "ER");
        addLiveTxLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}

bool ChannelMgr::sendNodeInfo(uint32_t myNodeId,
                              const char *longName, const char *shortName,
                              uint32_t toNodeId, bool wantResponse,
                              bool unusedCompat) {
    (void)unusedCompat;
    if (!Radio.isReady()) return false;

    // Build a MAC address consistent with myNodeId.
    // Meshtastic derives nodeId from mac[2..5], so those bytes must match.
    // Keep the real OUI in bytes [0..1] and pack the node ID into [2..5].
    uint8_t realMac[6];
    esp_read_mac(realMac, ESP_MAC_WIFI_STA);
    uint8_t mac[6] = {
        realMac[0], realMac[1],
        (uint8_t)(myNodeId >> 24),
        (uint8_t)(myNodeId >> 16),
        (uint8_t)(myNodeId >>  8),
        (uint8_t)(myNodeId      )
    };

    // Never set want_response on broadcast NODEINFO — that would trigger
    // a reply storm. For unicast, allow requesting peer NODEINFO.
    bool isUnicast = (toNodeId != 0xFFFFFFFF);
    bool reqReply = isUnicast && wantResponse;
    uint8_t proto[256], cipher[256];
    size_t protoLen = encodeNodeInfo(myNodeId, longName, shortName,
                                     mac, proto, sizeof(proto), reqReply);
    if (protoLen == 0) return false;

    // Always send NODEINFO on LongFast (index 0) for maximum visibility
    const ChannelKey &ck = CHANNEL_KEYS[0];
    uint32_t packetId = esp_random() ^ millis();
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    uint8_t frame[sizeof(MeshHdr) + 256];
    MeshHdr hdr = {};
    hdr.to      = toNodeId;
    hdr.from    = myNodeId;
    hdr.id      = packetId;
    hdr.channel = ck.hash;
    hdr.flags   = (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                  ((MESH_HOP_LIMIT & 0x07) << 5);
    hdr.relay_node = (uint8_t)(myNodeId & 0xFF);
    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, protoLen);

    debugLogMessages("[nodeinfo] %s to !%08X  proto=%u bytes\n",
                     isUnicast ? "unicast" : "broadcast", toNodeId, (unsigned)protoLen);

    bool ok = Radio.transmit(frame, sizeof(hdr) + protoLen);
    debugLogMessages("[nodeinfo] transmit %s\n", ok ? "OK" : "FAILED");
    {
        char dst[16];
        liveNodeLabel(toNodeId, dst, sizeof(dst), true);
        char live[64];
        snprintf(live, sizeof(live), "T NOD %s %s %s",
                 isUnicast ? "U" : "B",
                 dst,
                 ok ? "OK" : "ER");
        addLiveTxLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}
