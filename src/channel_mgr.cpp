#include "channel_mgr.h"
#include "live_util.h"
#include "mesh_radio.h"
#include "lgfx_tdeck.h"
#include "debug_flags.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"

ChannelMgr Channels;

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
        // Allocate line buffer in PSRAM
        _chans[i].lines = (DisplayLine *)heap_caps_malloc(
            MAX_MSG_LINES * sizeof(DisplayLine),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_chans[i].lines) {
            // Fallback to regular heap with fewer lines
            _chans[i].lines = (DisplayLine *)malloc(
                100 * sizeof(DisplayLine));
            Serial.printf("[chanmgr] PSRAM alloc failed for ch%d, using DRAM\n", i);
        }
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

void ChannelMgr::nextChannel() { setActive((_active + 1) % MAX_CHANNELS); }
void ChannelMgr::prevChannel() { setActive((_active + MAX_CHANNELS - 1) % MAX_CHANNELS); }

void ChannelMgr::_pushLine(Channel &ch, const char *text, uint16_t color,
                            uint32_t packetId, DisplayLine::AckState ack) {
    int idx = ch.count % MAX_MSG_LINES;
    DisplayLine &dl = ch.lines[idx];
    strncpy(dl.text, text, MSG_CHARS);
    dl.text[MSG_CHARS] = '\0';
    dl.color    = color;
    dl.packetId = packetId;
    dl.ack      = ack;
    ch.count++;
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
    int  prefixLen = strlen(prefix);
    int  textLen   = strlen(text);
    int  pos       = 0;          // position in text
    bool firstLine = true;
    const int CONT_INDENT = 2;   // spaces for continuation lines

    // Preserve existing behavior for empty message bodies.
    if (textLen == 0) {
        if (prefixLen > 0) snprintf(line, sizeof(line), "%.*s", prefixLen, prefix);
        else line[0] = '\0';
        DisplayLine::AckState ack = packetId ? DisplayLine::PENDING : DisplayLine::NONE;
        _pushLine(ch, line, color, packetId, ack);
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

        if (firstLine) {
            snprintf(line, sizeof(line), "%.*s%.*s", prefixLen, prefix, take, text + pos);
        } else {
            snprintf(line, sizeof(line), "%*s%.*s", CONT_INDENT, "", take, text + pos);
        }

        // ACK indicator on first line of sent messages
        DisplayLine::AckState ack = (firstLine && packetId) ?
            DisplayLine::PENDING : DisplayLine::NONE;

        _pushLine(ch, line, color, firstLine ? packetId : 0, ack);

        pos += take;
        if (pos < textLen && text[pos] == ' ') pos++; // skip space after wrap
        firstLine = false;
    }
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
            // Find the line in the channel and update its ack state
            Channel &ch = _chans[_pending[i].chanIdx];
            for (int j = 0; j < min(ch.count, MAX_MSG_LINES); j++) {
                if (ch.lines[j].packetId == packetId) {
                    ch.lines[j].ack = state;
                    break;
                }
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
        int firstLine = addMessage(txChan, prefix, text, TFT_RED, packetId);
        if (firstLine >= 0) {
            Channel &ch = _chans[txChan];
            int idx = firstLine % MAX_MSG_LINES;
            if (ch.lines[idx].packetId == packetId) {
                ch.lines[idx].ack = DisplayLine::TX_FAILED;
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

bool ChannelMgr::sendPosition(uint32_t myNodeId, int32_t latI, int32_t lonI, int32_t alt) {
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
                              uint32_t toNodeId) {
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

    // Never set want_response on NODEINFO — broadcasts with want_response=true cause
    // every receiver to respond simultaneously, creating collisions.  Plai and official
    // Meshtastic firmware both broadcast NODEINFO with want_response=false.
    bool isUnicast = (toNodeId != 0xFFFFFFFF);
    uint8_t proto[256], cipher[256];
    size_t protoLen = encodeNodeInfo(myNodeId, longName, shortName,
                                     mac, proto, sizeof(proto), false);
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
