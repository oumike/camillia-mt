// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which key bytes from the ThinkNode M9's keyboard controller are new presses.
// Pure, so tests/test_m9_key_guard.cpp can drive it; keyboard.cpp feeds it every
// byte it reads. Ported from wadamesh's M9KeyGuard.h (wadamesh #625, #671).
//
// The controller scans its matrix every 10 ms with no debounce on the matrix
// keys and latches a key byte on every released-to-pressed edge, letters on the
// press itself. Two things therefore deliver one press twice:
//
//  - A read soon after a key can return that same key again, before the
//    controller has cleared its one slot.
//  - A contact that chatters as a slow key comes up is a second edge, so the
//    same byte arrives again roughly one hold time later: 100-250 ms on a
//    deliberate press.
//
// Nothing on the wire says when a key went up, so the guard goes by time:
//
//  - The same printable character again within the window is the same press.
//    The window follows the typing pace: three fifths of the usual gap between
//    two different characters, clamped to 70-250 ms (250 until a pace is
//    known). A chatter repeat comes after about one hold time and a letter
//    typed twice on purpose ("ll") after about one whole keystroke, which is
//    longer, at any pace.
//  - A pause between words is not a keystroke: a gap longer than twice the
//    pace counts as twice the pace, so pauses barely move the window.
//  - Arrows, Delete, Enter and the function keys keep a flat 70 ms: lists are
//    stepped with quick taps and a held Delete repeats every 100 ms.

#include <stdint.h>

struct M9KeyGuard {
    uint8_t  lastKey = 0;
    uint32_t lastMs  = 0;
    uint32_t paceMs  = 0;   // smoothed gap between two different characters; 0 = unknown
};

static constexpr uint32_t kM9GuardMinMs     = 70;
static constexpr uint32_t kM9GuardMaxMs     = 250;
static constexpr uint32_t kM9GuardDefaultMs = 250;
static constexpr uint32_t kM9PaceIdleMs     = 1500;  // a longer gap is a pause, not the pace
static constexpr uint32_t kM9PacePauseRatio = 2;     // a gap counts as at most this many paces

inline bool m9KeyIsChar(uint8_t key) { return key >= 0x20 && key <= 0x7E; }

inline uint32_t m9KeyGuardWindow(const M9KeyGuard &g) {
    if (!g.paceMs) return kM9GuardDefaultMs;
    uint32_t w = g.paceMs * 3 / 5;
    if (w < kM9GuardMinMs) w = kM9GuardMinMs;
    if (w > kM9GuardMaxMs) w = kM9GuardMaxMs;
    return w;
}

// True when `key`, read at `nowMs`, is a new press. A dropped repeat does not
// move the timing: the window runs from the last accepted key, so a run of
// bounces cannot stretch it.
inline bool m9KeyGuardAccept(M9KeyGuard &g, uint8_t key, uint32_t nowMs) {
    const uint32_t gap = nowMs - g.lastMs;
    if (g.lastKey && key == g.lastKey) {
        const uint32_t window = m9KeyIsChar(key) ? m9KeyGuardWindow(g) : kM9GuardMinMs;
        if (gap < window) return false;
    } else if (g.lastKey && m9KeyIsChar(key) && m9KeyIsChar(g.lastKey) && gap < kM9PaceIdleMs) {
        if (!g.paceMs) {
            g.paceMs = gap;
        } else {
            const uint32_t cap  = g.paceMs * kM9PacePauseRatio;
            const uint32_t step = gap < cap ? gap : cap;
            g.paceMs = (g.paceMs * 3 + step) / 4;
        }
    }
    g.lastKey = key;
    g.lastMs  = nowMs;
    return true;
}
