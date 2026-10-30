#include "../src/node_name_cache.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

// No storage: everything is a quiet no-op, the board-could-not-allocate case.
static void testNoStorage() {
    NodeNameCache c;
    c.begin(nullptr, 16);
    c.remember(5, "AB", "Alpha");
    NodeIdentity out;
    CHECK(!c.take(5, out));
    CHECK(c.count() == 0 && c.capacity() == 0);
}

// A filed name comes back once, and a re-eviction replaces rather than duplicates.
static void testRememberTake() {
    NodeIdentity slots[4];
    NodeNameCache c;
    c.begin(slots, 4);
    NodeIdentity out;
    CHECK(!c.take(5, out));
    c.remember(5, "AB", "Alpha");
    c.remember(5, "CD", "Charlie");
    CHECK(c.count() == 1);
    CHECK(c.take(5, out) && out.nodeId == 5);
    CHECK(strcmp(out.shortName, "CD") == 0 && strcmp(out.longName, "Charlie") == 0);
    CHECK(!c.take(5, out));
    CHECK(c.count() == 0);
}

// Nothing worth keeping is not kept.
static void testIgnored() {
    NodeIdentity slots[4];
    NodeNameCache c;
    c.begin(slots, 4);
    c.remember(0, "AB", "Alpha");
    c.remember(6, "", "");
    c.remember(7, nullptr, nullptr);
    CHECK(c.count() == 0);
    c.remember(8, "", "Only long");
    CHECK(c.count() == 1);
}

// Full ring: the name filed longest ago is the one dropped.
static void testRing() {
    NodeIdentity slots[3];
    NodeNameCache c;
    c.begin(slots, 3);
    c.remember(1, "A", "a");
    c.remember(2, "B", "b");
    c.remember(3, "C", "c");
    c.remember(4, "D", "d");
    CHECK(c.count() == 3);
    NodeIdentity out;
    CHECK(!c.take(1, out));
    CHECK(c.take(2, out) && c.take(3, out) && c.take(4, out));
}

// Over-long input is cut without splitting a UTF-8 sequence.
static void testTruncate() {
    NodeIdentity slots[2];
    NodeNameCache c;
    c.begin(slots, 2);
    c.remember(9, "AB\xC3\xA9\xC3\xA9", "x");   // 6 bytes into a 5-byte field
    NodeIdentity out;
    CHECK(c.take(9, out));
    CHECK(strcmp(out.shortName, "AB\xC3\xA9") == 0);
}

static void testForgetClear() {
    NodeIdentity slots[4];
    NodeNameCache c;
    c.begin(slots, 4);
    c.remember(1, "A", "a");
    c.remember(2, "B", "b");
    c.forget(1);
    NodeIdentity out;
    CHECK(!c.take(1, out));
    CHECK(c.count() == 1);
    c.clear();
    CHECK(c.count() == 0);
}

int main() {
    testNoStorage();
    testRememberTake();
    testIgnored();
    testRing();
    testTruncate();
    testForgetClear();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
