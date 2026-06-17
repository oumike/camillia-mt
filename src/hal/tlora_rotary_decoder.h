#pragma once

#include <stdint.h>
#include "config.h"

#if defined(DEVICE_TLORA_PAGER_TFT)

enum class TloraRotaryAction : uint8_t {
    None = 0,
    ScrollUp,
    ScrollDown,
    Click,
};

class TloraRotaryDecoder {
public:
    void reset(uint8_t pinAActiveLow, uint8_t pinBActiveLow, unsigned long nowMs);

    TloraRotaryAction poll(
        uint8_t pinAActiveLow,
        uint8_t pinBActiveLow,
        bool clickEdge,
        unsigned long nowMs,
        unsigned long &lastScrollMs);

private:
    uint8_t process(uint8_t pinAActiveLow, uint8_t pinBActiveLow);

    uint8_t _state = 0;
    bool _initialized = false;

    bool _pendingClick = false;
    unsigned long _pendingClickMs = 0;
    unsigned long _lastClickSeenMs = 0;
    unsigned long _lastClickEmitMs = 0;
    unsigned long _lastMoveEmitMs = 0;
};

#endif
