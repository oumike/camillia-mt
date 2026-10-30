#include "../src/node_evict.h"

#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static NodeEvictKey k(uint32_t heard, uint16_t rank = 0, bool named = false) {
    NodeEvictKey e;
    e.lastHeardMs = heard;
    e.staleRank = rank;
    e.hasName = named;
    return e;
}

// The longer silence goes first, named or not.
static void testRecency() {
    CHECK(nodeEvictBefore(k(1000, 0, true), k(5000), 10000));
    CHECK(!nodeEvictBefore(k(5000), k(1000, 0, true), 10000));
}

// Not heard since boot is older than anything heard since.
static void testUnheardFirst() {
    CHECK(nodeEvictBefore(k(0, 0, true), k(1), 0xF0000000u));
    CHECK(!nodeEvictBefore(k(1), k(0, 0, true), 0xF0000000u));
}

// Among nodes loaded from NVS, the order saved before the reboot decides.
static void testStaleRank() {
    CHECK(nodeEvictBefore(k(0, 200, true), k(0, 3, false), 100));
    CHECK(!nodeEvictBefore(k(0, 3, false), k(0, 200, true), 100));
}

// millis() wrapped: a node heard just after the wrap is the newer one, though
// its stamp is the smaller number.
static void testWrap() {
    const uint32_t beforeWrap = 0xFFFFF000u;
    const uint32_t afterWrap  = 0x00000100u;
    const uint32_t now        = 0x00000200u;
    CHECK(nodeEvictBefore(k(beforeWrap), k(afterWrap), now));
    CHECK(!nodeEvictBefore(k(afterWrap), k(beforeWrap), now));
}

// A tie goes to the unnamed node, and is not a strict preference otherwise.
static void testTie() {
    CHECK(nodeEvictBefore(k(500, 0, false), k(500, 0, true), 900));
    CHECK(!nodeEvictBefore(k(500, 0, true), k(500, 0, false), 900));
    CHECK(!nodeEvictBefore(k(500, 0, true), k(500, 0, true), 900));
    CHECK(nodeEvictBefore(k(0, 7, false), k(0, 7, true), 900));
}

int main() {
    testRecency();
    testUnheardFirst();
    testStaleRank();
    testWrap();
    testTie();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
