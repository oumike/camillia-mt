// Host test for src/cs_proto.cpp (vendored from camillia-chat-server). Build
// and run with tests/run.sh. The server repo holds the full suite; these pin
// the wire facts the client relies on.
// test-deps: sha256_portable.cpp
#include "../src/cs_proto.h"

#include <stdio.h>
#include <string.h>

using namespace csp;

static int g_fail = 0;
static int g_run = 0;

static void ok(bool cond, const char *what) {
    g_run++;
    if (!cond) {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}

static void testRequestRoundTrip() {
    uint8_t buf[MAX_PAYLOAD];
    Request r{7, 42, 0xA1B2C3D4, 99};
    size_t n = encodeRequest(r, buf, sizeof buf);
    Request back{};
    ok(n == 18, "request is 18 bytes");
    ok(decodeRequest(buf, n, back), "request decodes");
    ok(back.epoch == 7 && back.cursor == 42 && back.anchorFrom == 0xA1B2C3D4 && back.anchorId == 99,
       "request fields survive");
}

static void testBatchFirstLast() {
    Item items[2] = {};
    items[0].seq = 5; items[0].from = 1; items[0].packetId = 2; items[0].ageSec = 60;
    items[0].textLen = 2; memcpy(items[0].text, "hi", 2);
    items[1].seq = 6; items[1].ageSec = AGE_UNKNOWN;
    BatchHeader h{9, FLAG_FIRST | FLAG_LAST, 0, 2};
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeBatch(h, items, 2, buf, sizeof buf);
    BatchHeader hb{};
    Item out[4];
    uint8_t got = 0;
    ok(n > 0 && decodeBatch(buf, n, hb, out, 4, got), "batch decodes");
    ok(hb.flags == (FLAG_FIRST | FLAG_LAST), "FIRST|LAST flags survive");
    ok(got == 2 && out[0].seq == 5 && out[1].seq == 6, "items survive in order");
    ok(out[1].ageSec == AGE_UNKNOWN, "unknown age survives");
    ok(strcmp(out[0].text, "hi") == 0, "text survives");
}

static void testChannelId() {
    const uint8_t psk[1] = {0x01};
    ok(channelId("LongFast", psk, 1) == 0xf989c0d1u, "LongFast/AQ== channel id");
}

static void testAnnounceTenFits() {
    Announce a{};
    strcpy(a.shortName, "WXYZ");
    a.count = 10;
    for (int i = 0; i < 10; i++) {
        a.ch[i].id = 0x1000u + (uint32_t)i;
        strcpy(a.ch[i].name, "ElevenChars");
    }
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeAnnounce(a, buf, sizeof buf);
    ok(n > 0 && n <= MAX_PAYLOAD, "announce of 10 channels fits 231 bytes");
}

int main() {
    testRequestRoundTrip();
    testBatchFirstLast();
    testChannelId();
    testAnnounceTenFits();
    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
