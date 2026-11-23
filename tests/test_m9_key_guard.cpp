// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/m9_key_guard.h"
#include "test_support.h"

#include <string>
#include <vector>

// The M9 keyboard controller, simulated: it scans the matrix every 10 ms with no
// debounce and latches a key byte into its single slot on each released-to-
// pressed edge. The driver reads that slot every 15 ms and runs each byte
// through the guard.
struct Stroke {
    uint8_t key;
    uint32_t down;
    uint32_t hold;
    bool chatter;   // the contact opens and recloses for one scan as the key comes up
};

static bool contactClosed(const Stroke &s, uint32_t t) {
    if (t < s.down) return false;
    const uint32_t up = s.down + s.hold;
    if (t < up) return true;
    return s.chatter && t >= up + 10 && t < up + 20;
}

// staleReads: the slot is not cleared by the first read, so the next poll sees
// the same byte again (the double read that the old burst drain hit).
static std::string run(const std::vector<Stroke> &strokes, uint32_t until, bool staleReads = false) {
    M9KeyGuard guard;
    std::vector<bool> closed(strokes.size(), false);
    uint8_t slot = 0;
    uint8_t staleSlot = 0;
    std::string typed;
    for (uint32_t t = 0; t < until; ++t) {
        if (t % 10 == 0) {
            for (size_t i = 0; i < strokes.size(); ++i) {
                const bool c = contactClosed(strokes[i], t);
                if (c && !closed[i]) slot = strokes[i].key;
                closed[i] = c;
            }
        }
        if (t % 15 == 0) {
            uint8_t k = slot;
            slot = 0;
            bool fromStale = false;
            if (!k && staleReads && staleSlot) {
                k = staleSlot;
                fromStale = true;
            }
            staleSlot = (staleReads && k && !fromStale) ? k : 0;
            if (k && m9KeyGuardAccept(guard, k, 1000 + t)) typed += (char)k;
        }
    }
    return typed;
}

static std::string type(const char *text, uint32_t gapMs, uint32_t holdMs, bool chatter,
                        bool staleReads = false) {
    std::vector<Stroke> strokes;
    uint32_t t = 7;
    for (const char *p = text; *p; ++p, t += gapMs) strokes.push_back({(uint8_t)*p, t, holdMs, chatter});
    return run(strokes, t + 400, staleReads);
}

int main() {
    // Slow, deliberate presses that chatter on the way up: one letter each, and
    // letters typed twice on purpose are kept.
    CHECK(type("hello", 450, 180, true) == "hello");
    CHECK(type("bookkeeper", 450, 200, true) == "bookkeeper");
    CHECK(type("deliberate", 600, 220, true) == "deliberate");

    // Fast typing with chatter on every key.
    CHECK(type("committee", 140, 60, true) == "committee");

    // A read that returns the key a second time.
    CHECK(type("abc", 200, 80, false, true) == "abc");

    // Pauses between words do not stretch the window into swallowing doubles.
    {
        std::vector<Stroke> s;
        uint32_t t = 7;
        const char *text = "see you soon";
        for (const char *p = text; *p; ++p) {
            if (p > text && p[0] != ' ' && p[-1] == ' ') t += 1200;
            s.push_back({(uint8_t)*p, t, 60, false});
            t += 160;
        }
        CHECK(run(s, t + 400) == "see you soon");
    }

    // Non-character keys use the flat 70 ms window, so quick list steps still
    // all land, while an immediate re-read of one is dropped.
    {
        M9KeyGuard g;
        const uint8_t down = 0xB6;
        CHECK(m9KeyGuardAccept(g, down, 1000));
        CHECK(!m9KeyGuardAccept(g, down, 1015));
        CHECK(m9KeyGuardAccept(g, down, 1100));
        CHECK(m9KeyGuardAccept(g, down, 1200));
    }

    // A destination button chattering on release is one press (a repeated
    // Messages closes the channel drawer it just opened), while a deliberate
    // second press still lands.
    CHECK(run({{0x81, 7, 180, true}}, 800) == "\x81");
    CHECK(run({{0x81, 7, 120, false}, {0x81, 450, 120, false}}, 900) == "\x81\x81");

    return test::report();
}
