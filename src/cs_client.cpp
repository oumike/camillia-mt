#include "cs_client.h"

#include <string.h>

namespace csc {

static bool reached(uint32_t nowMs, uint32_t atMs) { return (int32_t)(nowMs - atMs) >= 0; }

void Client::begin(Mode mode, uint32_t serverId, bool manual, const uint32_t *chanIds, int nChans,
                   const ChannelState *saved, uint32_t nowMs) {
    *this = Client();
    _mode = mode;
    _server = serverId;
    _manual = serverId != 0 && manual;
    _nChans = nChans > MAX_LOCAL ? MAX_LOCAL : nChans;
    for (int i = 0; i < _nChans; i++) {
        _chanIds[i] = chanIds[i];
        if (saved) _state[i] = saved[i];
    }
    _nextDiscoverMs = nowMs;
    _nextRoundMs = nowMs;
    _roundDue = (mode == MODE_AUTO);
}

void Client::setMode(Mode m, uint32_t nowMs) {
    _mode = m;
    if (m == MODE_OFF) {
        _roundActive = _roundDue = false;
        _waiting = WAIT_NONE;
    } else if (m == MODE_AUTO && !_roundActive) {
        _roundDue = true;
        _nextRoundMs = nowMs;
    } else if (m == MODE_MANUAL && !_roundActive) {
        _roundDue = false;
    }
    _nextDiscoverMs = nowMs;
}

void Client::resetStates() {
    for (int i = 0; i < MAX_LOCAL; i++) _state[i] = ChannelState{0, 0};
    _statesDirty = true;
}

void Client::setServer(uint32_t nodeId, bool manual, uint32_t nowMs) {
    _server = nodeId;
    _manual = nodeId != 0 && manual;
    _hops = 0xFF;
    _haveChannels = false;
    memset(_serverHas, 0, sizeof(_serverHas));
    _roundActive = false;
    _waiting = WAIT_NONE;
    _unanswered = 0;
    _unreachableNoticed = false;
    _roundDue = (_mode == MODE_AUTO) && nodeId != 0;
    _nextRoundMs = nowMs;
    _nextDiscoverMs = nowMs;
    resetStates();
}

void Client::clearServer(uint32_t nowMs) { setServer(0, false, nowMs); }

bool Client::serverHasChannel(int chanIdx) const {
    return chanIdx >= 0 && chanIdx < _nChans && _serverHas[chanIdx];
}

void Client::pushNotice(Notice n) {
    if (_noticeLen < (int)(sizeof(_notices) / sizeof(_notices[0]))) _notices[_noticeLen++] = n;
}

Notice Client::takeNotice() {
    if (!_noticeLen) return NOTICE_NONE;
    Notice n = _notices[0];
    memmove(_notices, _notices + 1, sizeof(Notice) * (size_t)(_noticeLen - 1));
    _noticeLen--;
    return n;
}

bool Client::takeStatesDirty() {
    bool d = _statesDirty;
    _statesDirty = false;
    return d;
}

void Client::answered() {
    _unanswered = 0;
    if (_unreachableNoticed) {
        _unreachableNoticed = false;
        pushNotice(NOTICE_REACHABLE);
    }
}

void Client::timedOut(uint32_t nowMs) {
    // A missed BATCH is usually a collision, not a dead server: ask the same
    // channel again before giving up on the round.
    if (_waiting == WAIT_REQUEST && _retries < REQUEST_RETRIES) {
        _retries++;
        _waiting = WAIT_NONE;
        _waitUntilMs = nowMs;
        return;
    }
    _waiting = WAIT_NONE;
    _roundActive = false;
    if (++_unanswered >= UNANSWERED_NOTICE && !_unreachableNoticed) {
        _unreachableNoticed = true;
        pushNotice(NOTICE_UNREACHABLE);
    }
    // Never forget the server: just try again on the normal schedule.
    _roundDue = (_mode == MODE_AUTO);
    _nextRoundMs = nowMs + AUTO_INTERVAL_MS;
}

void Client::onAnnounce(uint32_t from, uint8_t hopsTravelled, const csp::Announce &a, uint32_t nowMs) {
    if (_mode == MODE_OFF || from == 0) return;

    bool matches[MAX_LOCAL] = {};
    bool any = false;
    for (int i = 0; i < _nChans; i++) {
        if (!_chanIds[i]) continue;
        for (int k = 0; k < a.count; k++) {
            if (a.ch[k].id == _chanIds[i]) { matches[i] = true; any = true; }
        }
    }

    if (_server == 0) {
        if (!any) return;   // a server with none of our channels is no use to us
        setServer(from, false, nowMs);
        pushNotice(NOTICE_FOUND);
    } else if (from != _server) {
        return;             // first one wins; a manual server is never replaced
    }

    memcpy(_serverHas, matches, sizeof(_serverHas));
    _haveChannels = true;
    _hops = hopsTravelled > MAX_HOPS ? MAX_HOPS : hopsTravelled;
    answered();
    if (_waiting == WAIT_DISCOVER) _waiting = WAIT_NONE;   // the round carries on in poll()
}

int Client::onBatch(uint32_t from, int chanIdx, const csp::BatchHeader &h, const csp::Item *items,
                    uint8_t n, uint32_t nowMs) {
    if (from != _server || _waiting != WAIT_REQUEST || chanIdx != _waitingChan) return 0;
    answered();

    ChannelState &st = _state[chanIdx];
    const ChannelState before = st;
    bool first = (h.flags & csp::FLAG_FIRST) != 0;
    if (h.epoch != st.epoch) {
        st.epoch = h.epoch;
        st.cursor = 0;
        first = true;
    }
    if (first) {
        _gapInBatch = false;
        if (n) st.cursor = items[n - 1].seq;
    } else if (!_gapInBatch && n) {
        if (items[0].seq == st.cursor + 1) st.cursor = items[n - 1].seq;
        else _gapInBatch = true;   // a packet went missing: stop here, re-ask from the cursor
    }
    if (st.epoch != before.epoch || st.cursor != before.cursor) _statesDirty = true;

    if (h.flags & csp::FLAG_LAST) finishRequest((h.flags & csp::FLAG_MORE) != 0, nowMs);
    return n;
}

void Client::finishRequest(bool more, uint32_t nowMs) {
    _waiting = WAIT_NONE;
    _retries = 0;
    if (more) {
        _waitingMore = true;          // same channel again, outside the 15-minute limit
        _waitUntilMs = nowMs + MORE_DELAY_MS;
        return;
    }
    _waitingMore = false;
    _waitUntilMs = nowMs;
    if (++_queuePos >= _queueLen) {   // round complete
        _roundActive = false;
        _roundDue = (_mode == MODE_AUTO);
        _nextRoundMs = nowMs + AUTO_INTERVAL_MS;
    }
}

bool Client::checkNow(uint32_t nowMs, const char **why) {
    const char *reason = nullptr;
    if (_mode == MODE_OFF) reason = "off";
    else if (_server == 0) reason = "no server";
    else if (_roundActive) reason = "busy";
    else if (_checkedOnce && !reached(nowMs, _lastCheckMs + CHECK_COOLDOWN_MS)) reason = "cooldown";
    if (why) *why = reason;
    if (reason) return false;
    _checkedOnce = true;
    _lastCheckMs = nowMs;
    startRound(nowMs);
    return true;
}

bool Client::holdOwnTx(uint32_t nowMs) const {
    return _waiting != WAIT_NONE && !reached(nowMs, _waitingSinceMs + HOLD_OWN_TX_MS);
}

void Client::startRound(uint32_t nowMs) {
    _roundActive = true;
    _retries = 0;
    _roundDue = false;
    _queueBuilt = false;
    _queuePos = _queueLen = 0;
    _waitingMore = false;
    _waitUntilMs = nowMs;
}

void Client::buildQueue(const std::function<Anchor(int chanIdx)> &anchorFor) {
    _queueLen = 0;
    for (int i = 0; i < _nChans; i++) {
        if (!_serverHas[i]) continue;
        _queue[_queueLen] = i;
        _anchors[_queueLen] = anchorFor ? anchorFor(i) : Anchor{0, 0};
        _queueLen++;
    }
    _queuePos = 0;
    _queueBuilt = true;
}

uint8_t Client::requestHops() const {
    if (_hops == 0xFF) return MAX_HOPS;
    return _hops + 1 > MAX_HOPS ? MAX_HOPS : (uint8_t)(_hops + 1);
}

bool Client::poll(uint32_t nowMs, const std::function<Anchor(int chanIdx)> &anchorFor, Send &out) {
    if (_mode == MODE_OFF) return false;

    if (_server == 0) {
        if (!reached(nowMs, _nextDiscoverMs)) return false;
        _nextDiscoverMs = nowMs + DISCOVERY_INTERVAL_MS;
        out.to = 0xFFFFFFFF;
        out.chanIdx = -1;
        out.hopLimit = DISCOVERY_HOPS;
        out.len = csp::encodeDiscover(out.payload, sizeof(out.payload));
        return out.len > 0;
    }

    if (_waiting != WAIT_NONE) {
        if (reached(nowMs, _waitingSinceMs + REPLY_TIMEOUT_MS)) timedOut(nowMs);
        return false;
    }

    if (!_roundActive) {
        if (!(_roundDue && reached(nowMs, _nextRoundMs))) return false;
        startRound(nowMs);
    }

    if (!_haveChannels) {           // learn the server's channels first
        _waiting = WAIT_DISCOVER;
        _waitingSinceMs = nowMs;
        out.to = _server;
        out.chanIdx = -1;
        out.hopLimit = MAX_HOPS;
        out.len = csp::encodeDiscover(out.payload, sizeof(out.payload));
        return out.len > 0;
    }

    if (!_queueBuilt) buildQueue(anchorFor);
    if (_queuePos >= _queueLen) {   // server shares none of our channels any more
        _roundActive = false;
        _roundDue = (_mode == MODE_AUTO);
        _nextRoundMs = nowMs + AUTO_INTERVAL_MS;
        return false;
    }
    if (!reached(nowMs, _waitUntilMs)) return false;

    const int chan = _queue[_queuePos];
    const ChannelState &st = _state[chan];
    csp::Request r{st.epoch, st.cursor, _anchors[_queuePos].from, _anchors[_queuePos].packetId};
    out.to = _server;
    out.chanIdx = chan;
    out.hopLimit = requestHops();
    out.len = csp::encodeRequest(r, out.payload, sizeof(out.payload));
    _waiting = WAIT_REQUEST;
    _waitingChan = chan;
    _waitingSinceMs = nowMs;
    _gapInBatch = false;
    return out.len > 0;
}

uint32_t displayEpoch(uint32_t nowUnix, bool clockSet, uint32_t ageSec) {
    if (!clockSet || ageSec == csp::AGE_UNKNOWN || ageSec > nowUnix) return 0;
    return nowUnix - ageSec;
}

}  // namespace csc
