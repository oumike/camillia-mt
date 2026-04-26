#pragma once
// Direct-message storage, rendering model, send path, and SD persistence.
#include <Arduino.h>
#include "config.h"

#define MAX_DM_CONVS    16
#define MAX_DM_LINES   200
#define DM_LINE_LEN     53   // LCD_W / CHAR_W = 320/6

struct DmLine {
    char     text[DM_LINE_LEN + 1];
    uint16_t color;
};

struct DmConv {
    uint32_t nodeId;
    char     shortName[5];
    int      count;           // total lines pushed (may exceed MAX_DM_LINES)
    int      scrollOff;       // 0 = latest visible
    DmLine  *lines;           // PSRAM-allocated circular buffer [MAX_DM_LINES]
    char     lastText[DM_LINE_LEN + 1];  // most recent message (list preview)
    uint32_t lastMs;
    bool     unread;          // true if there are messages not yet viewed
    int      rxChanIdx;       // channel index last message was received on (-1 = unknown)
};

class DmMgr {
public:
    void      init();

    DmConv   *find(uint32_t nodeId);
    DmConv   *findOrCreate(uint32_t nodeId, const char *shortName);
    DmConv   *getByRank(int idx);     // 0 = most recently messaged
    int       count() const { return _count; }
    bool      hasUnread() const;
    void      markRead(uint32_t nodeId);

    // Add a message (word-wrapped) to a conversation.
    // markUnread: set to false for outgoing or seed messages.
    // chanIdx: channel index the message was received on (-1 for outgoing/unknown).
    void addMessage(uint32_t nodeId, const char *shortName,
                    const char *prefix, const char *text, uint16_t color,
                    bool markUnread = false, int chanIdx = -1);

    // Build and transmit a unicast DM. Adds outgoing message to conversation.
    bool sendDm(uint32_t myNodeId, uint32_t toNodeId, const char *text);

    // Scroll-aware line fetch for rendering.
    // visibleRow: 0 = top of screen. visibleRows = total rows available.
    const DmLine *getLine(const DmConv *conv, int visibleRow, int visibleRows) const;

    // Persistence (SD card)
    void saveConv(const DmConv *c);
    void loadAll();

private:
    DmConv _convs[MAX_DM_CONVS];
    int    _count = 0;

    void _sort();
    void _pushLine(DmConv &c, const char *text, uint16_t color);
};

extern DmMgr DMs;
