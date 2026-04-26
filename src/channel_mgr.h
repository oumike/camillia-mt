#pragma once
// Channel manager for mesh chat/LIVE views, line wrapping, and ACK tracking.
#include <Arduino.h>
#include "config.h"
#include "mesh_proto.h"

struct DisplayLine {
    char     text[MSG_CHARS + 1];
    uint16_t color;
    uint32_t packetId;    // 0 = not a sent message
    enum AckState : uint8_t { NONE, PENDING, ACKED, ACKED_RELAY, NAKED, TX_FAILED } ack;
};

struct Channel {
    const char  *name;        // points into CHANNEL_KEYS[i].name
    DisplayLine *lines;       // PSRAM circular buffer
    int          count;       // total lines ever added
    int          scrollOff;   // 0 = latest at top
    bool         unread;
    bool         active;      // has been allocated
};

struct PendingAck {
    uint32_t packetId;
    uint32_t sentMs;
    int      chanIdx;
    int      lineIdx;         // which line in channel buffer holds this message
    uint32_t destNodeId;      // intended recipient (0xFFFFFFFF = broadcast)
    bool     active;
};

class ChannelMgr {
public:
    void    init();

    Channel &get(int idx)       { return _chans[idx]; }
    int      activeIdx() const  { return _active; }
    void     setActive(int idx);
    void     clearChannel(int idx);
    void     nextChannel();
    void     prevChannel();

    // Add a rendered message to a channel. Word-wraps to MSG_CHARS.
    // prefix: e.g. "14:32 [ABCD] " — prepended to first line only
    // Returns the line index of the first added line.
    int addMessage(int chanIdx, const char *prefix, const char *text,
                   uint16_t color, uint32_t packetId = 0);

    void setAckState(uint32_t packetId, DisplayLine::AckState state);
    // Determine ACKED vs ACKED_RELAY by comparing fromNodeId to stored destNodeId
    void setAckStateFrom(uint32_t packetId, uint32_t fromNodeId);
    bool expireAcks();   // call from loop(); returns true when any ACK state changed

    // Retrieve a display line for rendering (0=newest visible at current scroll).
    // Returns nullptr if out of range.
    const DisplayLine *getLine(int chanIdx, int row) const;

    // Build and transmit a text message on a mesh channel.
    // chanIdx: 0..MESH_CHANNELS-1, or -1 to use current active channel.
    bool sendText(uint32_t myNodeId, const char *text, bool okToMqtt = false,
                  int chanIdx = -1);

    // Send a NODEINFO_APP packet. Broadcasts on LongFast by default.
    // Pass toNodeId for a unicast reply (e.g. responding to want_response).
    bool sendNodeInfo(uint32_t myNodeId, const char *longName, const char *shortName,
                      uint32_t toNodeId = 0xFFFFFFFF);

    // Broadcast a POSITION_APP packet on LongFast with the given coordinates.
    bool sendPosition(uint32_t myNodeId, int32_t latI, int32_t lonI, int32_t alt);

private:
    Channel    _chans[MAX_CHANNELS];
    PendingAck _pending[MAX_PENDING_ACK];
    int        _active = 0;

    void _wordWrap(int chanIdx, const char *prefix, const char *text,
                   uint16_t color, uint32_t packetId);
    void _pushLine(Channel &ch, const char *text, uint16_t color,
                   uint32_t packetId, DisplayLine::AckState ack);
};

extern ChannelMgr Channels;
