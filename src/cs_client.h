#pragma once
// camillia chat server client: every decision the node makes about the chat
// server -- when to look for one, when to ask it for missed messages, how far
// it has caught up, and what to tell the user -- with no radio, clock or UI in
// it. main_lvgl.cpp feeds it packets and the time, and sends what it returns.
// Pure C++ so tests/run.sh can drive it (tests/test_cs_client.cpp).
//
// Spec: camillia-chat-server docs/superpowers/specs/2026-10-02-camillia-chat-server-design.md §6.
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include "cs_proto.h"

namespace csc {

enum Mode : uint8_t { MODE_OFF = 0, MODE_AUTO = 1, MODE_MANUAL = 2 };

constexpr uint32_t AUTO_INTERVAL_MS      = 15UL * 60 * 1000;
constexpr uint32_t MORE_DELAY_MS         = 30UL * 1000;
constexpr uint32_t CHECK_COOLDOWN_MS     = 5UL * 60 * 1000;
constexpr uint32_t DISCOVERY_INTERVAL_MS = 60UL * 60 * 1000;
constexpr uint32_t REPLY_TIMEOUT_MS      = 60UL * 1000;
constexpr int      UNANSWERED_NOTICE     = 3;
constexpr int      MAX_LOCAL             = 10;
constexpr uint8_t  DISCOVERY_HOPS        = 3;
constexpr uint8_t  MAX_HOPS              = 7;

struct ChannelState { uint32_t epoch, cursor; };   // persisted per local channel
struct Anchor { uint32_t from, packetId; };        // {0,0} = none

// One packet for the caller to transmit on port 256.
struct Send {
    uint32_t to;
    int      chanIdx;    // local channel index, -1 = the camillia-cs discovery channel
    uint8_t  hopLimit;
    uint8_t  payload[csp::MAX_PAYLOAD];
    size_t   len;
};

enum Notice : uint8_t { NOTICE_NONE, NOTICE_FOUND, NOTICE_UNREACHABLE, NOTICE_REACHABLE };

class Client {
public:
    // chanIds[i] = csp::channelId of local channel i, 0 = slot unused.
    // saved may be null (all channels start at {0,0}).
    void begin(Mode mode, uint32_t serverId, bool manual, const uint32_t *chanIds, int nChans,
               const ChannelState *saved, uint32_t nowMs);
    void setMode(Mode m, uint32_t nowMs);
    void setServer(uint32_t nodeId, bool manual, uint32_t nowMs);   // resets every ChannelState
    void clearServer(uint32_t nowMs);                                // resets every ChannelState

    void onAnnounce(uint32_t from, uint8_t hopsTravelled, const csp::Announce &a, uint32_t nowMs);
    // Returns the number of items the caller should post (all of them, n). The
    // channel's cursor only advances across contiguous sequence numbers.
    int  onBatch(uint32_t from, int chanIdx, const csp::BatchHeader &h, const csp::Item *items,
                 uint8_t n, uint32_t nowMs);
    // why (on false): "off", "no server", "busy", "cooldown".
    bool checkNow(uint32_t nowMs, const char **why);

    // At most one packet per call. anchorFor(i) is asked once per channel when a
    // round starts: the newest message the node already holds on channel i.
    bool poll(uint32_t nowMs, const std::function<Anchor(int chanIdx)> &anchorFor, Send &out);

    uint32_t serverId() const      { return _server; }
    bool     manual() const        { return _manual; }
    Mode     mode() const          { return _mode; }
    uint8_t  hopsToServer() const  { return _hops; }   // 0xFF = unknown
    bool     serverHasChannel(int chanIdx) const;
    const ChannelState &state(int chanIdx) const { return _state[chanIdx]; }
    bool     takeStatesDirty();
    Notice   takeNotice();

private:
    enum Waiting : uint8_t { WAIT_NONE, WAIT_DISCOVER, WAIT_REQUEST };

    void resetStates();
    void answered();
    void timedOut(uint32_t nowMs);
    void startRound(uint32_t nowMs);
    void buildQueue(const std::function<Anchor(int chanIdx)> &anchorFor);
    void finishRequest(bool more, uint32_t nowMs);
    void pushNotice(Notice n);
    uint8_t requestHops() const;

    Mode         _mode = MODE_OFF;
    uint32_t     _server = 0;
    bool         _manual = false;
    uint8_t      _hops = 0xFF;
    uint32_t     _chanIds[MAX_LOCAL] = {};
    int          _nChans = 0;
    ChannelState _state[MAX_LOCAL] = {};
    bool         _statesDirty = false;

    bool     _haveChannels = false;               // server's ANNOUNCE seen since it was set
    bool     _serverHas[MAX_LOCAL] = {};

    uint32_t _nextDiscoverMs = 0;
    uint32_t _nextRoundMs = 0;
    bool     _roundDue = false;                   // a round should start once possible
    bool     _roundActive = false;
    bool     _queueBuilt = false;
    int      _queue[MAX_LOCAL] = {};
    Anchor   _anchors[MAX_LOCAL] = {};
    int      _queueLen = 0, _queuePos = 0;
    uint32_t _waitUntilMs = 0;
    bool     _waitingMore = false;

    Waiting  _waiting = WAIT_NONE;
    int      _waitingChan = -1;
    uint32_t _waitingSinceMs = 0;
    bool     _gapInBatch = false;

    bool     _checkedOnce = false;
    uint32_t _lastCheckMs = 0;

    int      _unanswered = 0;
    bool     _unreachableNoticed = false;
    Notice   _notices[4] = {};
    int      _noticeLen = 0;
};

// Wall-clock seconds a replayed message was heard: nowUnix - ageSec. 0 when the
// age is unknown, the node's clock is not set, or the age reaches before 1970.
uint32_t displayEpoch(uint32_t nowUnix, bool clockSet, uint32_t ageSec);

}  // namespace csc
