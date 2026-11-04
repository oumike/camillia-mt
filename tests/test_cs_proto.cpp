// Host test for src/cs_proto.cpp (vendored from camillia-chat-server). Build
// and run with tests/run.sh. The server repo holds the full suite; these pin
// the wire facts the client relies on.
// test-deps: sha256_portable.cpp
#include "../src/cs_proto.h"

#include <stdio.h>
#include <string.h>
#include <vector>

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

static void testWireFixtures() {
    const uint8_t request[] = {
        1, 3, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xFF, 0xFF, 0xFF,
        0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01,
    };
    const Request q{0x11223344, UINT32_MAX, 0x12345678, 0x01020304};
    uint8_t out[64];
    ok(encodeRequest(q, out, sizeof(out)) == sizeof(request)
       && !memcmp(out, request, sizeof(request)), "request uses little-endian wire fields");
    for (size_t n = 0; n < sizeof(request); n++) {
        Request parsed{};
        ok(!decodeRequest(request, n, parsed), "every truncated request is rejected");
        memset(out, 0xA5, sizeof(out));
        ok(encodeRequest(q, out, n) == 0 && out[n] == 0xA5,
           "request encoder respects every short capacity");
    }
    memcpy(out, request, sizeof(request));
    out[sizeof(request)] = 0;
    Request parsed{};
    ok(!decodeRequest(out, sizeof(request) + 1, parsed), "request rejects trailing bytes");
    out[0] = 2;
    ok(!decodeRequest(out, sizeof(request), parsed), "request rejects another version");
    out[0] = 1; out[1] = 2;
    ok(!decodeRequest(out, sizeof(request), parsed), "request rejects another message type");

    Type type = DISCOVER;
    for (uint8_t t = 1; t <= 4; t++) {
        const uint8_t header[] = {1, t};
        ok(peekType(header, sizeof(header), type) && type == static_cast<Type>(t),
           "known wire message types are recognized");
    }
    const uint8_t unknown[] = {1, 5};
    ok(!peekType(unknown, 2, type) && !peekType(unknown, 1, type)
       && !peekType(nullptr, 0, type), "unknown or incomplete headers are rejected");
}

static void testDiscoverCompatibilityAndBounds() {
    const uint8_t wire[] = {1, 1, 2, 0x44, 0x33, 0x22, 0x11, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint32_t ids[] = {0x11223344, UINT32_MAX};
    uint8_t out[64];
    ok(encodeDiscover(out, sizeof(out), ids, 2) == sizeof(wire)
       && !memcmp(out, wire, sizeof(wire)), "discovery advertises the channel IDs on the wire");
    uint32_t got[10] = {};
    uint8_t n = 99;
    bool hasList = true;
    ok(decodeDiscover(wire, sizeof(wire), got, n, hasList)
       && hasList && n == 2 && got[0] == ids[0] && got[1] == ids[1],
       "discovery decodes channel IDs");
    ok(decodeDiscover(wire, 2, got, n, hasList) && !hasList && n == 0,
       "legacy two-byte discovery has no channel list");
    const uint8_t emptyList[] = {1, 1, 0};
    ok(decodeDiscover(emptyList, sizeof(emptyList), got, n, hasList) && hasList && n == 0,
       "explicit empty channel list is distinguished from legacy discovery");
    for (size_t len = 0; len < sizeof(wire); len++) {
        if (len == 2) continue;
        ok(!decodeDiscover(wire, len, got, n, hasList), "truncated discovery list is rejected");
    }
    const uint8_t tooMany[] = {1, 1, 11};
    ok(!decodeDiscover(tooMany, sizeof(tooMany), got, n, hasList) && n == 0,
       "oversized discovery count cannot overrun channel storage");
    ok(encodeDiscover(out, sizeof(out), ids, 11) == 0, "oversized discover encode rejected");
    memcpy(out, wire, sizeof(wire)); out[sizeof(wire)] = 0;
    ok(!decodeDiscover(out, sizeof(wire) + 1, got, n, hasList), "discovery rejects trailing bytes");
    for (size_t cap = 0; cap < sizeof(wire); cap++) {
        memset(out, 0xA5, sizeof(out));
        ok(encodeDiscover(out, cap, ids, 2) == 0 && out[cap] == 0xA5,
           "discovery cannot overwrite a short output");
    }
}

static void testAnnounceMalformedAndGolden() {
    const uint8_t wire[] = {1, 2, 4, 'R', 'i', 'C', 's', 1,
                           0x44, 0x33, 0x22, 0x11, 3, 'M', 'e', 's'};
    Announce a{};
    ok(decodeAnnounce(wire, sizeof(wire), a) && !strcmp(a.shortName, "RiCs")
       && a.count == 1 && a.ch[0].id == 0x11223344 && !strcmp(a.ch[0].name, "Mes"),
       "announce golden fixture decodes");
    uint8_t out[64];
    ok(encodeAnnounce(a, out, sizeof(out)) == sizeof(wire)
       && !memcmp(out, wire, sizeof(wire)), "announce matches golden wire bytes");
    for (size_t n = 0; n < sizeof(wire); n++) {
        Announce parsed{};
        ok(!decodeAnnounce(wire, n, parsed), "truncated announce rejected");
        memset(out, 0xA5, sizeof(out));
        ok(encodeAnnounce(a, out, n) == 0 && out[n] == 0xA5,
           "announce output stays within capacity");
    }
    for (size_t offset : {2u, 7u, 12u}) {
        std::vector<uint8_t> bad(wire, wire + sizeof(wire));
        bad[offset] = 255;
        Announce parsed{};
        ok(!decodeAnnounce(bad.data(), bad.size(), parsed),
           "oversized server name, channel count, or channel name rejected");
    }
    a.count = 11;
    ok(encodeAnnounce(a, out, sizeof(out)) == 0, "too many announcement channels rejected");
}

static void testBatchMalformedAndLimits() {
    const uint8_t wire[] = {
        1, 4, 7, 0, 0, 0, 9, 0, 0, 0, 0, 1,
        5, 0, 0, 0, 0x44, 0x33, 0x22, 0x11,
        9, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 2, 'h', 'i',
    };
    BatchHeader h{};
    Item item{};
    uint8_t count = 0;
    ok(decodeBatch(wire, sizeof(wire), h, &item, 1, count) && count == 1
       && h.epoch == 7 && h.flags == (FLAG_FIRST | FLAG_LAST) && h.serverTime == 0
       && item.seq == 5 && item.from == 0x11223344 && item.packetId == 9
       && item.ageSec == AGE_UNKNOWN && !strcmp(item.text, "hi"),
       "batch golden fixture decodes all fields");
    uint8_t out[512];
    ok(encodeBatch(h, &item, 1, out, sizeof(out)) == sizeof(wire)
       && !memcmp(out, wire, sizeof(wire)), "batch matches golden wire bytes");
    for (size_t n = 0; n < sizeof(wire); n++) {
        Item parsed{};
        BatchHeader parsedHeader{};
        uint8_t parsedCount = 0;
        ok(!decodeBatch(wire, n, parsedHeader, &parsed, 1, parsedCount),
           "truncated batch rejected");
        memset(out, 0xA5, sizeof(out));
        ok(encodeBatch(h, &item, 1, out, n) == 0 && out[n] == 0xA5,
           "batch encoder respects every short capacity");
    }
    ok(!decodeBatch(wire, sizeof(wire), h, &item, 0, count),
       "batch refuses more items than the caller can hold");
    memcpy(out, wire, sizeof(wire)); out[sizeof(wire)] = 0;
    ok(!decodeBatch(out, sizeof(wire) + 1, h, &item, 1, count), "batch rejects trailing bytes");
    memcpy(out, wire, sizeof(wire)); out[28] = 201;
    ok(!decodeBatch(out, sizeof(wire), h, &item, 1, count), "oversized message length rejected");

    Item items[2] = {};
    items[0].textLen = 200;
    memset(items[0].text, 'x', 200);
    const size_t n = encodeBatch(h, items, 1, out, sizeof(out));
    ok(n == 229 && decodeBatch(out, n, h, &item, 1, count)
       && item.textLen == 200 && item.text[200] == '\0'
       && !memcmp(item.text, items[0].text, 200), "maximum text fits and is NUL terminated");
    ok(encodeBatch(h, items, 2, out, sizeof(out)) == 0,
       "batch still obeys the radio limit with a larger caller buffer");
    items[0].textLen = 201;
    ok(encodeBatch(h, items, 1, out, sizeof(out)) == 0, "overlong text is not silently truncated");
    const size_t empty = encodeBatch(h, nullptr, 0, out, sizeof(out));
    ok(empty == 12 && decodeBatch(out, empty, h, nullptr, 0, count) && count == 0,
       "empty batch is a valid reply");
}

int main() {
    testRequestRoundTrip();
    testBatchFirstLast();
    testChannelId();
    testAnnounceTenFits();
    testWireFixtures();
    testDiscoverCompatibilityAndBounds();
    testAnnounceMalformedAndGolden();
    testBatchMalformedAndLimits();
    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
