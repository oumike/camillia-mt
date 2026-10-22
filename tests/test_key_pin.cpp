#include "../src/key_pin.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static void key(uint8_t out[32], uint8_t seed) {
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(seed + i);
}

// First key is trusted; the same key again is a no-op.
static void testFirstAndSame() {
    KeyPinTable t;
    uint8_t a[32]; key(a, 1);
    CHECK(t.offer(5, nullptr, a, false, 100) == KEY_OFFER_FIRST);
    CHECK(t.offer(5, a, a, false, 200) == KEY_OFFER_SAME);
    CHECK(t.pendingCount() == 0);
}

// A different key is parked once, warned once, and the latest offer wins.
static void testConflict() {
    KeyPinTable t;
    uint8_t a[32], b[32], c[32];
    key(a, 1); key(b, 50); key(c, 99);
    CHECK(t.offer(5, a, b, false, 100) == KEY_OFFER_NEW_CONFLICT);
    CHECK(t.offer(5, a, b, true, 150) == KEY_OFFER_PENDING);
    const KeyConflict *k = t.find(5);
    CHECK(k && memcmp(k->key, b, 32) == 0 && k->signedByNewKey && k->atMs == 150);
    // Re-offered unsigned: the signed flag is not lost.
    CHECK(t.offer(5, a, b, false, 160) == KEY_OFFER_PENDING);
    CHECK(t.find(5)->signedByNewKey);

    CHECK(t.offer(5, a, c, false, 200) == KEY_OFFER_NEW_CONFLICT);
    k = t.find(5);
    CHECK(k && memcmp(k->key, c, 32) == 0 && !k->signedByNewKey);
    CHECK(t.pendingCount() == 1);
}

// Accept hands back the key and clears the conflict.
static void testAccept() {
    KeyPinTable t;
    uint8_t a[32], b[32];
    key(a, 1); key(b, 50);
    t.offer(5, a, b, false, 100);
    KeyConflict out = {};
    CHECK(t.take(5, out));
    CHECK(out.nodeId == 5 && memcmp(out.key, b, 32) == 0);
    CHECK(!t.find(5));
    CHECK(!t.take(5, out));
    // Once b is the held key, offering it again is SAME.
    CHECK(t.offer(5, b, b, false, 200) == KEY_OFFER_SAME);
}

// Reject drops it and stays quiet about that key -- but not another one.
static void testReject() {
    KeyPinTable t;
    uint8_t a[32], b[32], c[32];
    key(a, 1); key(b, 50); key(c, 99);
    CHECK(!t.reject(5));
    t.offer(5, a, b, false, 100);
    CHECK(t.reject(5));
    CHECK(!t.find(5));
    CHECK(t.offer(5, a, b, true, 200) == KEY_OFFER_REJECTED);
    CHECK(!t.find(5));
    // The same key from a different node is that node's conflict.
    CHECK(t.offer(6, a, b, false, 200) == KEY_OFFER_NEW_CONFLICT);
    CHECK(t.offer(5, a, c, false, 300) == KEY_OFFER_NEW_CONFLICT);
    t.clear();
    CHECK(t.offer(5, a, b, false, 400) == KEY_OFFER_NEW_CONFLICT);
}

// Full table: the stalest offer goes, wrap-safe across a millis() rollover.
static void testEviction() {
    KeyPinTable t;
    uint8_t a[32], b[32];
    key(a, 1); key(b, 50);
    const uint32_t base = 0xFFFFFF00u;
    for (int i = 0; i < KEY_PIN_MAX_CONFLICTS; i++) {
        CHECK(t.offer(100 + i, a, b, false, base + (uint32_t)i * 16) == KEY_OFFER_NEW_CONFLICT);
    }
    CHECK(t.pendingCount() == KEY_PIN_MAX_CONFLICTS);
    t.offer(100, a, b, false, 0x10);   // refresh the oldest, after the rollover
    CHECK(t.offer(200, a, b, false, 0x20) == KEY_OFFER_NEW_CONFLICT);
    CHECK(t.find(100) && !t.find(101) && t.find(200));
    CHECK(t.pendingCount() == KEY_PIN_MAX_CONFLICTS);
}

// Losing the held key (node forgotten) drops a conflict against it.
static void testDrop() {
    KeyPinTable t;
    uint8_t a[32], b[32];
    key(a, 1); key(b, 50);
    t.offer(5, a, b, false, 100);
    t.drop(5);
    CHECK(!t.find(5));
    t.offer(5, a, b, false, 100);
    CHECK(t.offer(5, nullptr, b, false, 200) == KEY_OFFER_FIRST);
    CHECK(!t.find(5));
    CHECK(!t.find(0));
}

// Accept/Reject act only on the key the user was shown: a different offer that
// replaced it while the dialog was open is neither installed nor rejected.
static void testExpectedKey() {
    KeyPinTable t;
    uint8_t a[32], b[32], c[32];
    key(a, 1); key(b, 50); key(c, 99);
    t.offer(5, a, b, false, 100);
    uint8_t shown[32]; memcpy(shown, t.find(5)->key, 32);
    CHECK(t.offer(5, a, c, false, 200) == KEY_OFFER_NEW_CONFLICT);   // attacker swaps it
    KeyConflict out = {};
    CHECK(!t.take(5, out, shown));
    CHECK(!t.reject(5, shown));
    CHECK(t.find(5) && memcmp(t.find(5)->key, c, 32) == 0);           // still pending
    // Reviewed again, the key now shown is the one acted on.
    CHECK(t.take(5, out, c));
    CHECK(memcmp(out.key, c, 32) == 0 && !t.find(5));
    t.offer(5, a, b, false, 300);
    CHECK(t.reject(5, b));
    CHECK(t.offer(5, a, b, false, 400) == KEY_OFFER_REJECTED);
}

// An evicted node's key survives in RAM, once, and newest wins over oldest.
static void testEvicted() {
    KeyPinTable t;
    uint8_t a[32], b[32], out[32];
    key(a, 1); key(b, 50);
    CHECK(!t.takeEvicted(5, out));
    t.rememberEvicted(5, a);
    t.rememberEvicted(5, b);                       // re-evicted: replaced, not duplicated
    CHECK(t.evictedCount() == 1);
    CHECK(t.takeEvicted(5, out) && memcmp(out, b, 32) == 0);
    CHECK(!t.takeEvicted(5, out));

    for (int i = 0; i < KEY_PIN_MAX_EVICTED + 1; i++) t.rememberEvicted(100 + i, a);
    CHECK(t.evictedCount() == KEY_PIN_MAX_EVICTED);
    CHECK(!t.takeEvicted(100, out));               // oldest dropped
    CHECK(t.takeEvicted(101, out) && t.takeEvicted(100 + KEY_PIN_MAX_EVICTED, out));

    // The restore-then-offer sequence NodeDB::updateUser runs: a differing key
    // from a node that was pushed out is a conflict, a matching one is quiet.
    t.rememberEvicted(7, a);
    CHECK(t.takeEvicted(7, out));
    CHECK(t.offer(7, out, b, false, 10) == KEY_OFFER_NEW_CONFLICT);
    t.rememberEvicted(8, a);
    CHECK(t.takeEvicted(8, out));
    CHECK(t.offer(8, out, a, false, 10) == KEY_OFFER_SAME);

    t.rememberEvicted(9, a);
    t.clear();
    CHECK(t.evictedCount() == 0);
}

int main() {
    testExpectedKey();
    testEvicted();
    testFirstAndSame();
    testConflict();
    testAccept();
    testReject();
    testEviction();
    testDrop();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
