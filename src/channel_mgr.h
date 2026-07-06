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
    uint32_t epoch;       // wall-clock seconds when this line was added (0 = unknown)
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
                   uint16_t color, uint32_t packetId = 0,
                   bool trackAck = false);

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
                  int chanIdx = -1, uint32_t replyId = 0, uint32_t emoji = 0);

    // Send a NODEINFO_APP packet. Broadcasts on LongFast by default.
    // Pass toNodeId for a unicast reply (e.g. responding to want_response).
    // For unicast, wantResponse=true can be used to request peer NODEINFO.
    bool sendNodeInfo(uint32_t myNodeId, const char *longName, const char *shortName,
                      uint32_t toNodeId = 0xFFFFFFFF, bool wantResponse = false,
                      bool unusedCompat = false);

    // Broadcast a POSITION_APP packet on LongFast with the given coordinates.
    bool sendPosition(uint32_t myNodeId, int32_t latI, int32_t lonI, int32_t alt,
                      bool unusedCompat = false);

    // Send a unicast POSITION_APP request to a peer (empty payload + want_response).
    // Peer should reply with its current Position broadcast.
    bool sendPositionRequest(uint32_t myNodeId, uint32_t toNodeId);

    // Broadcast TELEMETRY_APP device metrics on LongFast.
    bool sendTelemetryDevice(uint32_t myNodeId, bool okToMqtt = false);

    // Broadcast TELEMETRY_APP environment metrics on LongFast.
    bool sendTelemetryEnvironment(uint32_t myNodeId,
                                  float temperatureC, float humidityPct, float pressureHpa,
                                  bool okToMqtt = false);

    // Broadcast NEIGHBORINFO_APP on LongFast.
    bool sendNeighborInfo(uint32_t myNodeId,
                          uint32_t nodeBroadcastIntervalS,
                          const NeighborEdgeInfo *neighbors,
                          size_t neighborCount,
                          bool okToMqtt = false);

    // Clear all channel messages in memory and optionally remove persisted logs.
    void clearAllMessages(bool clearPersisted = true);

    // Cardputer (no PSRAM): free all channel line buffers to reclaim DRAM for
    // the Wi-Fi stack while web config runs; restoreBuffers() reallocates them
    // and reloads persisted history. releaseBuffers() returns bytes freed.
    // Safe to leave freed: addMessage()/getLine() no-op when lines == nullptr.
    size_t releaseBuffers();
    void   restoreBuffers();

    // SD-backed persistence for mesh chat channels (0..MESH_CHANNELS-1).
    void beginPersistence();
    void loadPersisted();

private:
    Channel    _chans[MAX_CHANNELS];
    PendingAck _pending[MAX_PENDING_ACK];
    int        _active = 0;
    bool       _persistReady = false;
    bool       _persistLoading = false;
    bool       _persistDirReady = false;

    void _wordWrap(int chanIdx, const char *prefix, const char *text,
                   uint16_t color, uint32_t packetId, bool trackAck,
                   uint32_t epoch);
    void _pushLine(int chanIdx, Channel &ch, const char *text, uint16_t color,
                   uint32_t packetId, DisplayLine::AckState ack,
                   uint32_t epoch);
    void _persistChannel(int chanIdx, const Channel &ch);
};

extern ChannelMgr Channels;
