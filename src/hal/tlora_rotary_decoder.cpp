#include "tlora_rotary_decoder.h"

#if defined(DEVICE_TLORA_PAGER_TFT)

namespace {
constexpr uint8_t DIR_NONE = 0x00;
constexpr uint8_t DIR_CW = 0x10;
constexpr uint8_t DIR_CCW = 0x20;

constexpr uint8_t R_START = 0x0;
constexpr uint8_t R_CCW_BEGIN = 0x1;
constexpr uint8_t R_CW_BEGIN = 0x2;
constexpr uint8_t R_START_M = 0x3;
constexpr uint8_t R_CW_BEGIN_M = 0x4;
constexpr uint8_t R_CCW_BEGIN_M = 0x5;

// Ben Buxton half-step table (same core algorithm as Rotary.cpp with HALF_STEP).
// This emits on both 00 and 11 phases for higher sensitivity in both directions.
constexpr uint8_t kTransitionTable[6][4] = {
    {R_START_M,                      R_CW_BEGIN,      R_CCW_BEGIN,    R_START},
    {static_cast<uint8_t>(R_START_M | DIR_CCW), R_START,         R_CCW_BEGIN,    R_START},
    {static_cast<uint8_t>(R_START_M | DIR_CW),  R_CW_BEGIN,      R_START,        R_START},
    {R_START_M,                      R_CCW_BEGIN_M,   R_CW_BEGIN_M,   R_START},
    {R_START_M,                      R_START_M,       R_CW_BEGIN_M,   static_cast<uint8_t>(R_START | DIR_CW)},
    {R_START_M,                      R_CCW_BEGIN_M,   R_START_M,      static_cast<uint8_t>(R_START | DIR_CCW)},
};

constexpr unsigned long kMoveDebounceMs = 2;
constexpr unsigned long kClickQuietMs = 70;
constexpr unsigned long kClickExpireMs = 800;
constexpr unsigned long kClickEdgeDebounceMs = 80;
constexpr unsigned long kClickEmitMinIntervalMs = 280;
} // namespace

void TloraRotaryDecoder::reset(uint8_t pinAActiveLow, uint8_t pinBActiveLow, unsigned long nowMs) {
    _state = R_START;
    _initialized = true;
    _pendingClick = false;
    _pendingClickMs = 0;
    _lastClickSeenMs = 0;
    _lastClickEmitMs = 0;
    _lastMoveEmitMs = 0;

    // Prime the state machine with the current pin state.
    process(pinAActiveLow, pinBActiveLow);
    _lastMoveEmitMs = nowMs;
}

uint8_t TloraRotaryDecoder::process(uint8_t pinAActiveLow, uint8_t pinBActiveLow) {
    const uint8_t pinState = static_cast<uint8_t>(((pinBActiveLow & 0x01) << 1) | (pinAActiveLow & 0x01));
    _state = kTransitionTable[_state & 0x0F][pinState];
    return static_cast<uint8_t>(_state & 0x30);
}

TloraRotaryAction TloraRotaryDecoder::poll(
    uint8_t pinAActiveLow,
    uint8_t pinBActiveLow,
    bool clickEdge,
    unsigned long nowMs,
    unsigned long &lastScrollMs) {
    if (!_initialized) {
        reset(pinAActiveLow, pinBActiveLow, nowMs);
    }

    if (clickEdge && (nowMs - _lastClickSeenMs >= kClickEdgeDebounceMs)) {
        _pendingClick = true;
        _pendingClickMs = nowMs;
        _lastClickSeenMs = nowMs;
    }

    const uint8_t dir = process(pinAActiveLow, pinBActiveLow);

    if (dir == DIR_CW || dir == DIR_CCW) {
        if (nowMs - _lastMoveEmitMs >= kMoveDebounceMs) {
            _lastMoveEmitMs = nowMs;
            lastScrollMs = nowMs;
            // LilyGo maps clockwise to "up" and counter-clockwise to "down".
            return (dir == DIR_CW) ? TloraRotaryAction::ScrollUp : TloraRotaryAction::ScrollDown;
        }
    }

    if (_pendingClick && (nowMs - lastScrollMs >= kClickQuietMs)) {
        _pendingClick = false;
        if (nowMs - _lastClickEmitMs < kClickEmitMinIntervalMs) {
            return TloraRotaryAction::None;
        }
        _lastClickEmitMs = nowMs;
        return TloraRotaryAction::Click;
    }

    if (_pendingClick && (nowMs - _pendingClickMs > kClickExpireMs)) {
        _pendingClick = false;
    }

    return TloraRotaryAction::None;
}

#endif
