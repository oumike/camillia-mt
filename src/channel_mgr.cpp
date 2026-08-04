#include "channel_mgr.h"
#include "mqtt_bridge.h"
#include "live_feed.h"
#include "live_util.h"
#include "mesh_radio.h"
#include "hal/display.h"
#include "debug_flags.h"
#include "battery_util.h"
#include "utf8_utils.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "storage.h"
#include <time.h>
#include <stdlib.h>

namespace {
constexpr const char *kChanPersistDir = "/camillia/channels";
constexpr int kPersistMaxLines = 100;
constexpr int kLiveHistoryMaxLines = 50;

static int channelHistoryLimit(int chanIdx) {
    if (chanIdx == CHAN_LIVE) return kLiveHistoryMaxLines;
    return MAX_MSG_LINES;
}

static void channelPersistPath(int chanIdx, char *out, size_t outLen) {
    snprintf(out, outLen, "%s/ch%d.log", kChanPersistDir, chanIdx);
}

static inline bool isUtf8Continuation(uint8_t b) {
    return (b & 0xC0u) == 0x80u;
}

// Returns a byte count <= maxBytes that does not split a UTF-8 sequence.
static int utf8TakeNoSplit(const char *text, int pos, int textLen, int maxBytes) {
    if (!text || maxBytes <= 0 || pos >= textLen) return 0;
    int take = min(textLen - pos, maxBytes);
    while (take > 0 && (pos + take) < textLen
           && isUtf8Continuation((uint8_t)text[pos + take])) {
        take--;
    }
    if (take <= 0) take = min(1, textLen - pos);  // fallback for malformed input
    return take;
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

void ChannelMgr::init() {
    memset(_pending, 0, sizeof(_pending));
    for (int i = 0; i < MAX_CHANNELS; i++) {
        _chans[i].name      = CHANNEL_KEYS[i].name;
        _chans[i].count     = 0;
        _chans[i].scrollOff = 0;
        _chans[i].revision  = 0;
        _chans[i].unread    = false;
        _chans[i].lines = allocChannelLines();
        _chans[i].active = (_chans[i].lines != nullptr);
        if (!_chans[i].lines) {
            Serial.printf("[chanmgr] alloc failed for ch%d\n", i);
        }
    }
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
    _chans[idx].revision++;
}

void ChannelMgr::clearAllMessages(bool clearPersisted) {
    memset(_pending, 0, sizeof(_pending));
    for (int i = 0; i < MAX_CHANNELS; i++) {
        clearChannel(i);
    }

#if HAS_FILE_STORAGE
    if (!clearPersisted) return;

    for (int chanIdx = 0; chanIdx < MESH_CHANNELS; chanIdx++) {
        char path[48];
        channelPersistPath(chanIdx, path, sizeof(path));
        if (storageFs().exists(path)) storageFs().remove(path);
    }
    if (storageFs().exists(kChanPersistDir)) {
        (void)storageFs().rmdir(kChanPersistDir);
    }
    _persistDirReady = false;
#else
    (void)clearPersisted;
#endif
}

size_t ChannelMgr::releaseBuffers() {
    size_t freed = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (_chans[i].lines) {
            free(_chans[i].lines);
            _chans[i].lines = nullptr;
            freed += MAX_MSG_LINES * sizeof(DisplayLine);
        }
        _chans[i].count     = 0;
        _chans[i].scrollOff = 0;
        _chans[i].revision++;
        _chans[i].unread    = false;
        _chans[i].active    = false;
    }
    // Pending ACK entries point at line slots we just freed; drop them.
    memset(_pending, 0, sizeof(_pending));
    return freed;
}

void ChannelMgr::restoreBuffers() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (_chans[i].lines) continue;
        _chans[i].lines     = allocChannelLines();
        _chans[i].active    = (_chans[i].lines != nullptr);
        _chans[i].count     = 0;
        _chans[i].scrollOff = 0;
        _chans[i].revision++;
    }
    // Repopulate scrollback from SD if persistence is available (no-op otherwise).
    loadPersisted();
}

void ChannelMgr::nextChannel() { setActive((_active + 1) % MAX_CHANNELS); }
void ChannelMgr::prevChannel() { setActive((_active + MAX_CHANNELS - 1) % MAX_CHANNELS); }

void ChannelMgr::_pushLine(int chanIdx, Channel &ch, const char *text, uint16_t color,
                            uint32_t packetId, DisplayLine::AckState ack,
                            uint32_t epoch, uint32_t senderNodeId) {
    if (!ch.lines) return;
    int idx = ch.count % MAX_MSG_LINES;
    DisplayLine &dl = ch.lines[idx];
    utf8util::copyTruncate(dl.text, sizeof(dl.text), text);
    dl.color        = color;
    dl.packetId     = packetId;
    dl.ack          = ack;
    dl.epoch        = epoch;
    dl.senderNodeId = senderNodeId;
    ch.count++;
    ch.revision++;

    if (_persistReady && !_persistLoading && chanIdx >= 0 && chanIdx < MESH_CHANNELS) {
        _persistChannel(chanIdx, ch);
    }
}

int ChannelMgr::addMessage(int chanIdx, const char *prefix, const char *text,
                            uint16_t color, uint32_t packetId, bool trackAck,
                            uint32_t senderNodeId) {
    if (chanIdx < 0 || chanIdx >= MAX_CHANNELS) return -1;
    if (!_chans[chanIdx].lines) return -1;
    int firstLine = _chans[chanIdx].count;
    // Snapshot the wall-clock time once per logical message so every wrapped
    // line shares the same date for chat date markers.
    time_t nowEpoch = time(nullptr);
    uint32_t epoch = (nowEpoch >= 1700000000) ? (uint32_t)nowEpoch : 0;
    _wordWrap(chanIdx, prefix, text, color, packetId, trackAck, epoch, senderNodeId);
    if (chanIdx != _active) _chans[chanIdx].unread = true;
    return firstLine;
}

void ChannelMgr::_wordWrap(int chanIdx, const char *prefix, const char *text,
                             uint16_t color, uint32_t packetId, bool trackAck,
                             uint32_t epoch, uint32_t senderNodeId) {
    Channel &ch = _chans[chanIdx];
    if (!ch.lines) return;
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
        DisplayLine::AckState ack = (packetId && trackAck)
            ? DisplayLine::PENDING
            : DisplayLine::NONE;
        _pushLine(chanIdx, ch, line, color, packetId, ack, epoch, senderNodeId);
        return;
    }

    while (pos < textLen) {
        int lineStart = firstLine ? prefixLen : CONT_INDENT;
        int avail     = MSG_CHARS - lineStart;
#if defined(DEVICE_TDECK)
        // T-Deck chat is full-width; keep only a small safety margin for
        // proportional glyphs near the right edge.
        avail -= 2;
#endif
        if (avail <= 0) avail = 1;

        int remaining = textLen - pos;
        int take = utf8TakeNoSplit(text, pos, textLen, avail);

        // Don't break in the middle of a word if possible
        if (take < remaining && take > 0) {
            int bp = take;
            while (bp > 0 && text[pos + bp] != ' ' && text[pos + bp - 1] != ' ')
                bp--;
            if (bp > 0) take = utf8TakeNoSplit(text, pos, textLen, bp);
        }

        // Avoid wasting wrap width on trailing spaces.
        while (take > 0 && text[pos + take - 1] == ' ') {
            take--;
        }
        if (take <= 0) {
            while (pos < textLen && text[pos] == ' ') pos++;
            continue;
        }

        if (segCount < MAX_WRAP_LINES) {
            segPos[segCount] = (uint16_t)pos;
            segLen[segCount] = (uint16_t)take;
            segCount++;
        }

        pos += take;
        while (pos < textLen && text[pos] == ' ') pos++; // skip all spaces after wrap
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
        DisplayLine::AckState ack = (logicalFirst && packetId && trackAck)
            ? DisplayLine::PENDING
            : DisplayLine::NONE;
        _pushLine(chanIdx, ch, line, color, packetId, ack, epoch, senderNodeId);
    }
}

void ChannelMgr::beginPersistence() {
#if HAS_FILE_STORAGE
    _persistReady = true;
#else
    _persistReady = false;
#endif
}

void ChannelMgr::_persistChannel(int chanIdx, const Channel &ch) {
#if HAS_FILE_STORAGE
    if (!ch.lines) return;
    if (!_persistDirReady) {
        (void)storageFs().mkdir("/camillia");
        (void)storageFs().mkdir(kChanPersistDir);
        _persistDirReady = true;
    }

    char path[48];
    channelPersistPath(chanIdx, path, sizeof(path));

    // Rewrite bounded snapshot to keep storage and restore behavior predictable.
    storageFs().remove(path);
    File f = storageFs().open(path, FILE_WRITE);
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
        utf8util::copyTruncate(text, sizeof(text), dl.text);
        for (int i = 0; text[i]; i++) {
            if (text[i] == '\n' || text[i] == '\r') text[i] = ' ';
        }

        f.printf("%04X|%08lX|%u|%08lX|%08lX|%s\n",
                 (unsigned)dl.color,
                 (unsigned long)dl.packetId,
                 (unsigned)dl.ack,
                 (unsigned long)dl.epoch,
                 (unsigned long)dl.senderNodeId,
                 text);
    }
    f.close();
#else
    (void)chanIdx;
    (void)ch;
#endif
}

void ChannelMgr::loadPersisted() {
#if HAS_FILE_STORAGE
    if (!_persistReady) return;

    _persistLoading = true;
    for (int chanIdx = 0; chanIdx < MESH_CHANNELS; chanIdx++) {
        if (!_chans[chanIdx].lines) continue;
        char path[48];
        channelPersistPath(chanIdx, path, sizeof(path));
        if (!storageFs().exists(path)) continue;

        int totalLines = 0;
        {
            File countFile = storageFs().open(path, FILE_READ);
            if (!countFile) continue;
            while (countFile.available()) {
                int c = countFile.read();
                if (c == '\n') totalLines++;
            }
            countFile.close();
        }
        int skipLines = max(0, totalLines - kPersistMaxLines);

        File f = storageFs().open(path, FILE_READ);
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
                _pushLine(chanIdx, _chans[chanIdx], text, color, 0,
                          DisplayLine::NONE, 0, 0);
                continue;
            }

            *sep2 = '\0';
            char *sep3 = strchr(sep2 + 1, '|');
            if (!sep3) continue;
            *sep3 = '\0';

            uint32_t packetId = (uint32_t)strtoul(rest, nullptr, 16);
            uint8_t ackRaw = (uint8_t)strtoul(sep2 + 1, nullptr, 10);
            if (ackRaw > DisplayLine::TX_FAILED) ackRaw = DisplayLine::NONE;

            // Format may be any of:
            //   color|packetId|ack|text                    (legacy, no epoch)
            //   color|packetId|ack|epoch|text              (no sender)
            //   color|packetId|ack|epoch|sender|text       (current)
            const char *rest2 = sep3 + 1;
            uint32_t epoch = 0;
            uint32_t senderNodeId = 0;
            const char *text = rest2;
            char *sep4 = strchr((char *)rest2, '|');
            if (sep4) {
                *sep4 = '\0';
                epoch = (uint32_t)strtoul(rest2, nullptr, 16);
                const char *afterEpoch = sep4 + 1;
                char *sep5 = strchr((char *)afterEpoch, '|');
                if (sep5) {
                    *sep5 = '\0';
                    senderNodeId = (uint32_t)strtoul(afterEpoch, nullptr, 16);
                    text = sep5 + 1;
                } else {
                    text = afterEpoch;
                }
            }
            if (!text[0]) continue;

            _pushLine(chanIdx, _chans[chanIdx], text, color,
                      packetId, (DisplayLine::AckState)ackRaw, epoch, senderNodeId);
        }

        f.close();
    }
    _persistLoading = false;
#endif
}

const DisplayLine *ChannelMgr::getLine(int chanIdx, int row) const {
    if (chanIdx < 0 || chanIdx >= MAX_CHANNELS) return nullptr;
    const Channel &ch = _chans[chanIdx];
    if (!ch.lines) return nullptr;
    int total = ch.count;
    if (total == 0) return nullptr;
    // row 0 = newest visible line at the top; larger rows are older lines.
    int stored = min(total, min(MAX_MSG_LINES, channelHistoryLimit(chanIdx)));
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
            if (!ch.lines) return;
            bool updated = false;
            for (int j = 0; j < min(ch.count, MAX_MSG_LINES); j++) {
                if (ch.lines[j].packetId == packetId) {
                    ch.lines[j].ack = state;
                    updated = true;
                }
            }
            if (updated) ch.revision++;
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
            if (_pending[i].destNodeId == 0xFFFFFFFF) {
                setAckState(packetId, DisplayLine::ACKED_RELAY);
            } else {
                bool isDirect = (_pending[i].destNodeId == fromNodeId);
                setAckState(packetId, isDirect ? DisplayLine::ACKED : DisplayLine::ACKED_RELAY);
            }
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
            // After a short settle window, move to a softer relay-style status.
            // Avoid showing hard-delivered green when no explicit routing ACK exists.
            if (ageMs > kBroadcastConfirmMs) {
                setAckState(_pending[i].packetId, DisplayLine::ACKED_RELAY);
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
                          int chanIdx, uint32_t replyId, uint32_t emoji) {
    if (!Radio.isReady()) {
        liveFeedAddLine("T TXT B NR", TFT_RED);
        return false;
    }

    int txChan = (chanIdx >= 0 && chanIdx < MESH_CHANNELS) ? chanIdx : _active;
    if (txChan < 0 || txChan >= MESH_CHANNELS) return false;
    if (_active != txChan) setActive(txChan);

    uint32_t packetId = nextMeshPacketId();
    uint8_t  proto[256], cipher[256];

    uint32_t bitfield = okToMqtt ? 0x01 : 0;
    size_t protoLen = encodeTextMessage(text, proto, sizeof(proto), bitfield, replyId, emoji);
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
        liveFeedAddLine("T TXT B ER", TFT_RED);
        addMessage(txChan, prefix, text, TFT_RED, packetId, false, myNodeId);
        Channel &ch = _chans[txChan];
        for (int j = 0; j < min(ch.count, MAX_MSG_LINES); j++) {
            if (ch.lines[j].packetId == packetId) {
                ch.lines[j].ack = DisplayLine::TX_FAILED;
            }
        }
        return false;
    }

    // Mirror our own outgoing message to MQTT when this channel is uplink-enabled
    // (published once here, not on the reliability retry below).
    if (okToMqtt && CHANNEL_KEYS[txChan].uplinkEnabled) {
        mqttBridgePublishRaw(hdr, cipher, protoLen, txName);
    }

    // Reliability nudge for broadcast text: send one additional copy with the
    // same from:id shortly after the first TX. Receivers dedupe by from:id, so
    // this boosts delivery when the first frame is lost without duplicating chat.
    {
        const uint16_t retryDelayMs = (uint16_t)(18 + (packetId & 0x0F));
        delay(retryDelayMs);
        (void)Radio.transmit(frame, frameLen);
    }

    {
        char live[56];
        snprintf(live, sizeof(live), "T TXT B c%d %08X",
                 txChan, packetId);
        liveFeedAddLine(live, TFT_WHITE);
    }

    // Add to display first so ACK updates always have a visible line to target.
    int firstLine = addMessage(txChan, prefix, text, TFT_WHITE, packetId, true, myNodeId);

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
    uint32_t packetId = nextMeshPacketId();
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
        liveFeedAddLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}

bool ChannelMgr::sendPositionRequest(uint32_t myNodeId, uint32_t toNodeId) {
    if (!Radio.isReady()) return false;
    if (toNodeId == 0 || toNodeId == 0xFFFFFFFF) return false;

    uint8_t proto[32], cipher[32];
    size_t protoLen = encodePositionRequest(proto, sizeof(proto));
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0]; // LongFast
    uint32_t packetId = nextMeshPacketId();
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    uint8_t frame[sizeof(MeshHdr) + 32];
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

    bool ok = Radio.transmit(frame, sizeof(hdr) + protoLen);
    debugLogMessages("[position] request to !%08X %s\n", toNodeId, ok ? "OK" : "FAILED");
    {
        char dst[16];
        liveNodeLabel(toNodeId, dst, sizeof(dst), true);
        char live[64];
        snprintf(live, sizeof(live), "T POS U %s %08X %s",
                 dst, packetId, ok ? "OK" : "ER");
        liveFeedAddLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}

// Current Unix epoch for Telemetry.time, or 0 when the wall clock isn't synced
// (matches the >= 1700000000 validity convention used for chat timestamps).
static uint32_t telemetryEpochNow() {
    time_t now = time(nullptr);
    return (now >= 1700000000) ? (uint32_t)now : 0;
}

bool ChannelMgr::sendTelemetryDevice(uint32_t myNodeId, bool okToMqtt) {
    if (!Radio.isReady()) return false;

    uint8_t proto[80], cipher[80];
    uint32_t bitfield = okToMqtt ? 0x01 : 0;
    size_t protoLen = encodeTelemetryDevice(batteryReadPercent(), batteryReadVoltage(),
                                            Radio.channelUtilPercent(), Radio.airUtilTxPercent(),
                                            (uint32_t)(millis() / 1000UL),
                                            telemetryEpochNow(),
                                            proto, sizeof(proto), bitfield);
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0]; // LongFast
    uint32_t packetId = nextMeshPacketId();
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    uint8_t frame[sizeof(MeshHdr) + 80];
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
    {
        char live[56];
        snprintf(live, sizeof(live), "T TEL D %08X %s",
                 packetId, ok ? "OK" : "ER");
        liveFeedAddLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}

bool ChannelMgr::sendTelemetryEnvironment(uint32_t myNodeId,
                                          float temperatureC, float humidityPct, float pressureHpa,
                                          bool okToMqtt) {
    if (!Radio.isReady()) return false;

    uint8_t proto[96], cipher[96];
    uint32_t bitfield = okToMqtt ? 0x01 : 0;
    size_t protoLen = encodeTelemetryEnvironment(temperatureC, humidityPct, pressureHpa,
                                                 telemetryEpochNow(),
                                                 proto, sizeof(proto), bitfield);
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0]; // LongFast
    uint32_t packetId = nextMeshPacketId();
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    uint8_t frame[sizeof(MeshHdr) + 96];
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
    {
        char live[56];
        snprintf(live, sizeof(live), "T TEL E %08X %s",
                 packetId, ok ? "OK" : "ER");
        liveFeedAddLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}

bool ChannelMgr::sendNeighborInfo(uint32_t myNodeId,
                                  uint32_t nodeBroadcastIntervalS,
                                  const NeighborEdgeInfo *neighbors,
                                  size_t neighborCount,
                                  bool okToMqtt) {
    if (!Radio.isReady()) return false;

    uint8_t proto[224], cipher[224];
    uint32_t bitfield = okToMqtt ? 0x01 : 0;
    size_t protoLen = encodeNeighborInfo(myNodeId,
                                         nodeBroadcastIntervalS,
                                         neighbors,
                                         neighborCount,
                                         proto,
                                         sizeof(proto),
                                         bitfield);
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0]; // LongFast
    uint32_t packetId = nextMeshPacketId();
    if (!encryptPayload(packetId, myNodeId, ck.key, ck.keyLen,
                        proto, cipher, protoLen)) return false;

    uint8_t frame[sizeof(MeshHdr) + 224];
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
    {
        char live[64];
        snprintf(live, sizeof(live), "T NBR B %08X %s",
                 packetId, ok ? "OK" : "ER");
        liveFeedAddLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}

bool ChannelMgr::sendNodeInfo(uint32_t myNodeId,
                              const char *longName, const char *shortName,
                              uint32_t toNodeId, bool wantResponse,
                              bool unusedCompat) {
    (void)unusedCompat;
    if (!Radio.isReady()) return false;

    // Rate-limit broadcast NODEINFO to at most once per 15s. The periodic
    // announce, request replies, and manual sends all funnel through here, so
    // this is the single chokepoint that keeps a device from flooding the mesh.
    // Unicast replies to a specific requester are not throttled.
    bool isUnicast = (toNodeId != 0xFFFFFFFF);
    static uint32_t sLastNodeInfoBroadcastMs = 0;
    if (!isUnicast && sLastNodeInfoBroadcastMs != 0 &&
        (uint32_t)(millis() - sLastNodeInfoBroadcastMs) < 15000UL) {
        return false;
    }

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
    bool reqReply = isUnicast && wantResponse;
    uint8_t proto[256], cipher[256];
    size_t protoLen = encodeNodeInfo(myNodeId, longName, shortName,
                                     mac, proto, sizeof(proto), reqReply);
    if (protoLen == 0) return false;

    // Always send NODEINFO on LongFast (index 0) for maximum visibility
    const ChannelKey &ck = CHANNEL_KEYS[0];
    uint32_t packetId = nextMeshPacketId();
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
    // Start the 15s broadcast window only once a broadcast actually goes out,
    // so a failed transmit doesn't lock out the next attempt.
    if (ok && !isUnicast) sLastNodeInfoBroadcastMs = millis();
    {
        char dst[16];
        liveNodeLabel(toNodeId, dst, sizeof(dst), true);
        char live[64];
        snprintf(live, sizeof(live), "T NOD %s %s %s",
                 isUnicast ? "U" : "B",
                 dst,
                 ok ? "OK" : "ER");
        liveFeedAddLine(live, ok ? TFT_DARKGREY : TFT_RED);
    }
    return ok;
}
