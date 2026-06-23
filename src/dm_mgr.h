#pragma once
// Direct-message storage, rendering model, send path, and SD persistence.
#include <Arduino.h>
#include "config.h"

#define MAX_DM_CONVS    16
// One DmLine holds one full logical message (prefix + body). The UI label uses
// LVGL's wrap mode to break the text to the actual pane pixel width, so we no
// longer pre-wrap to a character grid (which produced awkward, mismatched
// breaks against the real font width).
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define MAX_DM_LINES    24   // RAM-limited (no PSRAM)
#define DM_LINE_LEN    200
#else
#define MAX_DM_LINES    60   // PSRAM-backed; covers typical DM history
#define DM_LINE_LEN    240   // covers max Meshtastic text payload + prefix
#endif

#define MAX_DM_PENDING_TX 12

struct DmLine {
    char     text[DM_LINE_LEN + 1];
    uint16_t color;
    uint32_t packetId;  // 0 = not a locally-sent DM line
    enum AckState : uint8_t { NONE, PENDING, ACKED, ACKED_RELAY, NAKED, TX_FAILED } ack;
};

struct DmConv {
    uint32_t nodeId;
    char     shortName[5];
    int      count;           // total lines pushed (may exceed MAX_DM_LINES)
    int      scrollOff;       // 0 = latest visible at top
    DmLine  *lines;           // circular buffer [MAX_DM_LINES]
    char     lastText[DM_LINE_LEN + 1];  // most recent message (list preview)
    uint32_t lastMs;
    bool     unread;          // true if there are messages not yet viewed
    uint16_t unreadCount;     // unread message count for this conversation
    int      rxChanIdx;       // channel index last message was received on (-1 = unknown)
};

class DmMgr {
public:
    void      init();

    DmConv   *find(uint32_t nodeId);
    DmConv   *findOrCreate(uint32_t nodeId, const char *shortName);
    DmConv   *getByRank(int idx);     // favorites first, then most recently messaged
    int       count() const { return _count; }
    bool      hasUnread() const;
    int       unreadMessageCount() const;
    void      markRead(uint32_t nodeId);
    bool      deleteConversation(uint32_t nodeId);
    void      clearAll(bool clearPersisted = true);

    // Add a message (word-wrapped) to a conversation.
    // markUnread: set to false for outgoing or seed messages.
    // chanIdx: channel index the message was received on (-1 for outgoing/unknown).
    // packetId: set for locally-sent DM lines so routing ACK/NAK can recolor them.
    void addMessage(uint32_t nodeId, const char *shortName,
                    const char *prefix, const char *text, uint16_t color,
                    bool markUnread = false, int chanIdx = -1,
                    uint32_t packetId = 0);

    // Build and transmit a unicast DM. Adds outgoing message to conversation.
    bool sendDm(uint32_t myNodeId, uint32_t toNodeId, const char *text);

    // Apply routing ACK/NAK result to the matching DM TX context.
    // Returns true when requestId matched a tracked DM transmit.
    bool handleRoutingResult(uint32_t fromNodeId, uint32_t requestId, uint32_t errorReason);

    // Scroll-aware line fetch for rendering.
    // visibleRow: 0 = newest line at top. visibleRows = total rows available.
    const DmLine *getLine(const DmConv *conv, int visibleRow, int visibleRows) const;

    // Persistence (SD card)
    void saveConv(const DmConv *c);
    void loadAll();

private:
    struct PendingTx {
        uint32_t packetId;
        uint32_t nodeId;
        uint32_t sentMs;
        bool     usedPki;
        bool     active;
    };

    DmConv _convs[MAX_DM_CONVS];
    int    _count = 0;
    PendingTx _pendingTx[MAX_DM_PENDING_TX];

    void _sort();
    void _pushLine(DmConv &c, const char *text, uint16_t color,
                   uint32_t packetId, DmLine::AckState ack);
    void _setAckState(uint32_t packetId, DmLine::AckState state);
};

extern DmMgr DMs;
