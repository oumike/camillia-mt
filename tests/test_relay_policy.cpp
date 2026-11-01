#include "../src/relay_policy.h"

#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

using namespace relay;

static void testRoles() {
    CHECK(roleRebroadcasts(ROLE_CLIENT));
    CHECK(roleRebroadcasts(ROLE_CLIENT_BASE));
    CHECK(roleRebroadcasts(5 /*TRACKER*/));
    CHECK(!roleRebroadcasts(ROLE_CLIENT_MUTE));
    CHECK(!roleRebroadcasts(ROLE_CLIENT_HIDDEN));

    CHECK(isRouterRole(ROLE_ROUTER));
    CHECK(isRouterRole(ROLE_ROUTER_LATE));
    CHECK(isRouterRole(ROLE_CLIENT_BASE));
    CHECK(!isRouterRole(ROLE_CLIENT));
    CHECK(!isRouterRole(3 /*ROUTER_CLIENT, retired*/));
    CHECK(!isRouterRole(4 /*REPEATER*/));
}

// LongFast (SF11, 250 kHz): 8.192 ms symbols, so 4.5 * 8.192 + 7.6 = 44.46 ms.
static void testSlotTime() {
    CHECK(slotTimeMsec(11, 250.0f, false) == 44);
    CHECK(slotTimeMsec(7, 250.0f, false) == 9);      // ShortFast: 0.512 ms symbols
    CHECK(slotTimeMsec(12, 125.0f, false) == 155);   // 32.768 ms symbols
    CHECK(slotTimeMsec(0, 250.0f, false) == 0);
    CHECK(slotTimeMsec(11, 0.0f, false) == 0);
    // 2.4 GHz, SF8 812.5 kHz: 0.315 ms symbols, (8 + 19/32) * 0.315 + 7.6.
    CHECK(slotTimeMsec(8, 812.5f, true) == 10);
}

static void testCwSize() {
    CHECK(cwSize(-20.0f) == kCwMin);
    CHECK(cwSize(10.0f) == kCwMax);
    CHECK(cwSize(-5.0f) == 5);        // 15 * 5 / 30 + 3, integer
    CHECK(cwSize(-40.0f) == kCwMin);  // clamped
    CHECK(cwSize(25.0f) == kCwMax);
    CHECK(cwSize(-4.9f) == 5);        // truncated toward zero, as map(long) is
}

static void testDelays() {
    const uint32_t slot = 44;
    // The ordinary window starts after the 16 slots reserved for ROUTER.
    CHECK(relayDelayMsec(10.0f, slot, 0) == 16 * slot);
    CHECK(relayDelayMsec(10.0f, slot, 255) == (16 + 255) * slot);
    CHECK(relayDelayMsec(10.0f, slot, 256) == 16 * slot);          // wraps the draw
    CHECK(relayDelayMsec(-20.0f, slot, 0xFFFFFFFFu) == (16 + 7) * slot);
    // A weak copy waits less than a strong one at the same draw, past the
    // weak window's size.
    CHECK(relayDelayMsec(-20.0f, slot, 100) < relayDelayMsec(10.0f, slot, 100));

    // The late window is past the end of every ordinary draw at that SNR.
    for (int snr = -20; snr <= 10; snr += 5) {
        const uint32_t late = lateWindowMsec((float)snr, slot);
        const uint32_t worst = relayDelayMsec((float)snr, slot, (1u << cwSize((float)snr)) - 1);
        CHECK(late > worst);
    }
    CHECK(lateWindowMsec(10.0f, slot) == (16 + 256) * slot);
}

static void testDupe() {
    CHECK(onDuplicateHeard(ROLE_CLIENT, false, false) == DupeAction::Cancel);
    CHECK(onDuplicateHeard(ROLE_CLIENT, true, false) == DupeAction::Cancel);
    CHECK(onDuplicateHeard(5 /*TRACKER*/, false, false) == DupeAction::Cancel);
    CHECK(onDuplicateHeard(ROLE_CLIENT_BASE, false, false) == DupeAction::Cancel);
    CHECK(onDuplicateHeard(ROLE_CLIENT_BASE, true, false) == DupeAction::MoveLate);
    CHECK(onDuplicateHeard(ROLE_ROUTER_LATE, false, false) == DupeAction::MoveLate);
    CHECK(onDuplicateHeard(ROLE_ROUTER, false, false) == DupeAction::Keep);
    // The sender's own retransmission is not a relay by anyone else.
    CHECK(onDuplicateHeard(ROLE_CLIENT, false, true) == DupeAction::Keep);
    CHECK(onDuplicateHeard(ROLE_CLIENT_BASE, true, true) == DupeAction::Keep);
}

static PrevRelay prev(bool unique, bool fav, bool hasRole, uint8_t role) {
    PrevRelay p;
    p.unique = unique;
    p.favorite = fav;
    p.hasRole = hasRole;
    p.role = role;
    return p;
}

static void testHopLimit() {
    const PrevRelay favBase = prev(true, true, true, ROLE_CLIENT_BASE);
    // The one case that keeps the hop: base relaying a favorited router-type
    // node's relay, not on the first hop.
    CHECK(!shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 2, favBase));
    CHECK(!shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 1, prev(true, true, true, ROLE_ROUTER)));
    CHECK(!shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 1, prev(true, true, true, ROLE_ROUTER_LATE)));

    CHECK(shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 3, favBase));    // first hop
    CHECK(shouldDecrementHopLimit(ROLE_CLIENT, 3, 2, favBase));         // we are no router
    CHECK(shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 2, prev(false, true, true, ROLE_CLIENT_BASE)));
    CHECK(shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 2, prev(true, false, true, ROLE_CLIENT_BASE)));
    CHECK(shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 2, prev(true, true, false, ROLE_CLIENT_BASE)));
    CHECK(shouldDecrementHopLimit(ROLE_CLIENT_BASE, 3, 2, prev(true, true, true, ROLE_CLIENT)));
}

int main() {
    testRoles();
    testSlotTime();
    testCwSize();
    testDelays();
    testDupe();
    testHopLimit();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
