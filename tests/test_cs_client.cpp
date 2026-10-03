// Host test for src/cs_client.cpp: the camillia chat server client's
// decisions (discovery, scheduling, cursors, notices). Build and run with
// tests/run.sh.
// test-deps: cs_proto.cpp sha256_portable.cpp
#include "../src/cs_client.h"

#include <stdio.h>
#include <string.h>

using namespace csc;

static int g_fail = 0;
static int g_run = 0;

static void ok(bool cond, const char *what) {
    g_run++;
    if (!cond) {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}

static const uint32_t ME_IDS[3] = {0x1111, 0, 0x3333};   // local channels 0 and 2 in use
static const uint32_t SERVER = 0xC5C5C5C5;
static const uint32_t MIN = 60UL * 1000;

static csp::Type typeOf(const Send &s) {
    csp::Type t = csp::DISCOVER;
    csp::peekType(s.payload, s.len, t);
    return t;
}

static Anchor anchorFor(int chanIdx) { return Anchor{0xA000u + (uint32_t)chanIdx, 0xB000u + (uint32_t)chanIdx}; }

static csp::Announce announce(const char *shortName, std::initializer_list<uint32_t> ids) {
    csp::Announce a{};
    strncpy(a.shortName, shortName, 4);
    for (uint32_t id : ids) a.ch[a.count++].id = id;
    return a;
}

static void batch(Client &c, uint32_t from, int chanIdx, uint32_t epoch, uint8_t flags,
                  std::initializer_list<uint32_t> seqs, uint32_t nowMs, int *returned = nullptr) {
    csp::BatchHeader h{epoch, flags, 0, (uint8_t)seqs.size()};
    csp::Item items[16] = {};
    int n = 0;
    for (uint32_t s : seqs) { items[n].seq = s; items[n].from = 0x77; items[n].packetId = s; n++; }
    int r = c.onBatch(from, chanIdx, h, items, (uint8_t)n, nowMs);
    if (returned) *returned = r;
}

// A client with SERVER known and its channels learned (hops 2, channels 0 and 2).
static void knownServer(Client &c, Mode mode, uint32_t nowMs = 0) {
    ChannelState saved[3] = {{7, 40}, {0, 0}, {0, 0}};
    c.begin(mode, SERVER, false, ME_IDS, 3, saved, nowMs);
    c.onAnnounce(SERVER, 2, announce("RiCs", {0x1111, 0x3333}), nowMs);
}

static void testOffModeSendsNothing() {
    Client c;
    c.begin(MODE_OFF, 0, false, ME_IDS, 3, nullptr, 0);
    Send s;
    bool any = false;
    for (uint32_t t = 0; t <= 120 * MIN; t += MIN) any |= c.poll(t, anchorFor, s);
    ok(!any, "off: nothing sent in 2 h");
}

static void testNoServerDiscoversAtBootAndHourly() {
    Client c;
    c.begin(MODE_AUTO, 0, false, ME_IDS, 3, nullptr, 0);
    Send s;
    ok(c.poll(0, anchorFor, s), "discover at boot");
    ok(s.to == 0xFFFFFFFF && s.chanIdx == -1 && s.hopLimit == DISCOVERY_HOPS && typeOf(s) == csp::DISCOVER,
       "boot discover is a broadcast on the discovery channel, hop 3");
    bool early = false;
    for (uint32_t t = MIN; t < 60 * MIN; t += MIN) early |= c.poll(t, anchorFor, s);
    ok(!early, "no second discover within the hour");
    ok(c.poll(60 * MIN, anchorFor, s) && typeOf(s) == csp::DISCOVER, "discover again at 60 min");
}

static void testAdoptsFirstMatchingAnnounce() {
    Client c;
    c.begin(MODE_AUTO, 0, false, ME_IDS, 3, nullptr, 0);
    c.onAnnounce(0xAAAA, 1, announce("NOPE", {0x9999}), 0);
    ok(c.serverId() == 0, "announce without a matching channel ignored");
    ok(c.takeNotice() == NOTICE_NONE, "no notice for an ignored announce");
    c.onAnnounce(0xBBBB, 1, announce("GOOD", {0x9999, 0x3333}), 0);
    ok(c.serverId() == 0xBBBB && !c.manual(), "first matching announce adopted, not manual");
    ok(c.takeNotice() == NOTICE_FOUND, "found notice");
    ok(c.serverHasChannel(2) && !c.serverHasChannel(0), "server channels matched by id");
    c.onAnnounce(0xCCCC, 0, announce("LATE", {0x1111}), 0);
    ok(c.serverId() == 0xBBBB, "a later announce does not replace the server");
}

static void testManualServerUnreachableNoticeOnceNeverReplaced() {
    Client c;
    c.begin(MODE_AUTO, 0, false, ME_IDS, 3, nullptr, 0);
    c.setServer(0xDEAD, true, 0);
    ok(c.manual(), "manual flag set");
    c.onAnnounce(0xBEEF, 0, announce("OTHR", {0x1111}), 0);
    ok(c.serverId() == 0xDEAD, "manual server not replaced by another announce");

    Send s;
    uint32_t t = 0;
    int notices = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        ok(c.poll(t, anchorFor, s) && s.to == 0xDEAD && typeOf(s) == csp::DISCOVER && s.hopLimit == MAX_HOPS,
           "unicast discover to the manual server, hop 7");
        t += REPLY_TIMEOUT_MS;
        c.poll(t, anchorFor, s);          // registers the timeout
        if (c.takeNotice() == NOTICE_UNREACHABLE) notices++;
        t += AUTO_INTERVAL_MS;            // next automatic attempt
    }
    ok(notices == 1, "unreachable notice raised exactly once after 3 misses");
    ok(c.serverId() == 0xDEAD, "manual server kept after misses");
    c.onAnnounce(0xDEAD, 1, announce("DEAD", {0x1111}), t);
    ok(c.takeNotice() == NOTICE_REACHABLE, "reachable notice when it answers again");
}

static void testAutoBootSync() {
    Client c;
    ChannelState saved[3] = {{7, 40}, {0, 0}, {0, 0}};
    c.begin(MODE_AUTO, SERVER, false, ME_IDS, 3, saved, 0);
    Send s;
    ok(c.poll(0, anchorFor, s) && s.to == SERVER && typeOf(s) == csp::DISCOVER && s.hopLimit == MAX_HOPS,
       "persisted server with unknown channels: unicast discover first");
    c.onAnnounce(SERVER, 2, announce("RiCs", {0x1111, 0x3333}), 1000);
    ok(c.poll(1000, anchorFor, s), "request after announce");
    csp::Request r{};
    ok(typeOf(s) == csp::REQUEST && csp::decodeRequest(s.payload, s.len, r), "it is a REQUEST");
    ok(s.to == SERVER && s.chanIdx == 0 && s.hopLimit == 3, "channel 0 first, hop = hops + 1");
    ok(r.epoch == 7 && r.cursor == 40 && r.anchorFrom == 0xA000 && r.anchorId == 0xB000,
       "carries saved epoch/cursor and the anchor");
    ok(!c.poll(2000, anchorFor, s), "waits for the reply");
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST | csp::FLAG_LAST, {41, 42}, 3000);
    ok(c.poll(3000, anchorFor, s) && s.chanIdx == 2 && typeOf(s) == csp::REQUEST, "then channel 2");
}

static void testMoreReasksAfter30s() {
    Client c;
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST | csp::FLAG_LAST | csp::FLAG_MORE, {41}, 1000);
    ok(!c.poll(1000 + MORE_DELAY_MS - 100, anchorFor, s), "not before 30 s");
    ok(c.poll(1000 + MORE_DELAY_MS, anchorFor, s) && s.chanIdx == 0, "same channel again at 30 s");
    csp::Request r{};
    csp::decodeRequest(s.payload, s.len, r);
    ok(r.cursor == 41, "with the advanced cursor");
}

static void testAutoResyncEvery15Min() {
    Client c;
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST | csp::FLAG_LAST, {}, 1000);
    c.poll(1000, anchorFor, s);
    batch(c, SERVER, 2, 3, csp::FLAG_FIRST | csp::FLAG_LAST, {}, 2000);   // round done at 2000
    ok(!c.poll(2000 + AUTO_INTERVAL_MS - 1000, anchorFor, s), "no round before 15 min");
    ok(c.poll(2000 + AUTO_INTERVAL_MS, anchorFor, s) && typeOf(s) == csp::REQUEST, "round at 15 min");
}

static void testManualModeNoAutoSync() {
    Client c;
    knownServer(c, MODE_MANUAL);
    Send s;
    bool any = false;
    for (uint32_t t = 0; t <= 60 * MIN; t += MIN) any |= c.poll(t, anchorFor, s);
    ok(!any, "manual-only: nothing sent on its own, boot included");
    const char *why = nullptr;
    uint32_t t = 61 * MIN;
    ok(c.checkNow(t, &why), "check now accepted");
    ok(c.poll(t, anchorFor, s) && typeOf(s) == csp::REQUEST, "check now sends a request");
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST | csp::FLAG_LAST, {}, t);
    c.poll(t, anchorFor, s);
    batch(c, SERVER, 2, 3, csp::FLAG_FIRST | csp::FLAG_LAST, {}, t);
    ok(!c.checkNow(t + 4 * MIN, &why) && why && strcmp(why, "cooldown") == 0, "second check within 5 min refused");
    ok(c.checkNow(t + CHECK_COOLDOWN_MS, &why), "accepted again at 5 min");
}

static void testCheckNowReasons() {
    Client c;
    const char *why = nullptr;
    c.begin(MODE_OFF, SERVER, false, ME_IDS, 3, nullptr, 0);
    ok(!c.checkNow(0, &why) && strcmp(why, "off") == 0, "off");
    c.begin(MODE_AUTO, 0, false, ME_IDS, 3, nullptr, 0);
    ok(!c.checkNow(0, &why) && strcmp(why, "no server") == 0, "no server");
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    ok(!c.checkNow(1000, &why) && strcmp(why, "busy") == 0, "busy during a round");
}

static void testGapStopsCursor() {
    Client c;
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    int r1 = 0, r2 = 0, r3 = 0;
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST, {1, 2, 3}, 100, &r1);
    batch(c, SERVER, 0, 7, 0, {4, 5}, 200, &r2);
    batch(c, SERVER, 0, 7, csp::FLAG_LAST | csp::FLAG_MORE, {8, 9}, 300, &r3);   // 6-7 lost
    ok(c.state(0).cursor == 5, "cursor stops before the gap");
    ok(r1 + r2 + r3 == 7, "every received item is returned for posting");
    ok(c.poll(300 + MORE_DELAY_MS, anchorFor, s), "re-request after MORE");
    csp::Request r{};
    csp::decodeRequest(s.payload, s.len, r);
    ok(r.cursor == 5, "re-request starts after the gap");
}

static void testBatchItemsReturnedEvenOnGap() {
    Client c;
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST, {1}, 100);
    int r = 0;
    batch(c, SERVER, 0, 7, csp::FLAG_LAST, {5, 6}, 200, &r);
    ok(r == 2, "gap packet's items still returned");
}

static void testNewEpochResetsCursor() {
    Client c;
    knownServer(c, MODE_AUTO);   // saved {7, 40}
    Send s;
    c.poll(0, anchorFor, s);
    batch(c, SERVER, 0, 9, csp::FLAG_FIRST | csp::FLAG_LAST, {1, 2}, 100);
    ok(c.state(0).epoch == 9 && c.state(0).cursor == 2, "new epoch adopted, cursor from the batch");
    ok(c.takeStatesDirty(), "state change flagged for saving");
}

static void testIgnoresBatchFromOtherNodeOrUnmatchedChannel() {
    Client c;
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    c.takeStatesDirty();
    batch(c, 0x1234, 0, 7, csp::FLAG_FIRST | csp::FLAG_LAST, {41}, 100);
    batch(c, SERVER, 1, 7, csp::FLAG_FIRST | csp::FLAG_LAST, {41}, 100);   // channel 1 not shared
    ok(c.state(0).cursor == 40 && !c.takeStatesDirty(), "no state change");
    ok(!c.poll(200, anchorFor, s), "still waiting: those did not answer the request");
}

static void testEmptyBatchCountsAsAnswered() {
    Client c;
    knownServer(c, MODE_AUTO);
    Send s;
    c.poll(0, anchorFor, s);
    batch(c, SERVER, 0, 7, csp::FLAG_FIRST | csp::FLAG_LAST, {}, 100);
    ok(c.poll(100, anchorFor, s) && s.chanIdx == 2, "empty LAST batch moves on to the next channel");
    ok(c.takeNotice() == NOTICE_NONE, "no unreachable notice");
}

static void testSetServerResetsStates() {
    Client c;
    knownServer(c, MODE_AUTO);
    c.takeStatesDirty();
    c.setServer(0x4444, true, 0);
    ok(c.state(0).epoch == 0 && c.state(0).cursor == 0 && c.state(2).cursor == 0, "states reset");
    ok(c.takeStatesDirty(), "reset flagged for saving");
    c.clearServer(0);
    ok(c.serverId() == 0 && !c.manual(), "cleared");
}

static void testDisplayEpochFromAge() {
    ok(displayEpoch(1000, true, 100) == 900, "now - age");
    ok(displayEpoch(1000, true, csp::AGE_UNKNOWN) == 0, "unknown age → 0");
    ok(displayEpoch(1000, false, 100) == 0, "clock not set → 0");
    ok(displayEpoch(50, true, 100) == 0, "age beyond the epoch → 0");
}

int main() {
    testOffModeSendsNothing();
    testNoServerDiscoversAtBootAndHourly();
    testAdoptsFirstMatchingAnnounce();
    testManualServerUnreachableNoticeOnceNeverReplaced();
    testAutoBootSync();
    testMoreReasksAfter30s();
    testAutoResyncEvery15Min();
    testManualModeNoAutoSync();
    testCheckNowReasons();
    testGapStopsCursor();
    testBatchItemsReturnedEvenOnGap();
    testNewEpochResetsCursor();
    testIgnoresBatchFromOtherNodeOrUnmatchedChannel();
    testEmptyBatchCountsAsAnswered();
    testSetServerResetsStates();
    testDisplayEpochFromAge();
    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
