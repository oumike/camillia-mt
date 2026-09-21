// Host test for src/admin_client.cpp.
//
// The session is testable without a radio because Hooks injects the transport
// and the caller supplies `now` -- so the rules that matter (a write is always
// preceded by a read, a destructive command needs `confirm` and a fresh key, a
// timeout never demotes) are checked here rather than on a mesh.
// test-deps: admin_proto.cpp
#include "../src/admin_client.h"

#include <stdio.h>
#include <string.h>
#include <vector>

using namespace AdminClient;
using namespace AdminProto;

static int g_fail = 0, g_run = 0;
static void ok(bool cond, const char *what) {
    g_run++;
    if (!cond) { g_fail++; printf("  FAIL  %s\n", what); }
}

// ── A fake transport ─────────────────────────────────────────────────────────
struct Sent {
    uint32_t id;
    Transport via;
    std::vector<uint8_t> payload;
};

struct Fake {
    std::vector<Sent> sent;
    bool rf = true;
    bool mqtt = false;
    uint32_t nextId = 0x1000;
    bool sendFails = false;
};

static uint32_t fakeSend(uint32_t, Transport via, const uint8_t *p, size_t n, void *ctx) {
    Fake *f = (Fake *)ctx;
    if (f->sendFails) return 0;
    Sent s;
    s.id = f->nextId++;
    s.via = via;
    s.payload.assign(p, p + n);
    f->sent.push_back(s);
    return s.id;
}
static bool fakeRf(uint32_t, void *ctx)  { return ((Fake *)ctx)->rf; }
static bool fakeMqtt(void *ctx)          { return ((Fake *)ctx)->mqtt; }
static uint32_t fakeEpoch(void *)        { return 1700000000; }

static Hooks hooksFor(Fake &f) {
    Hooks h{};
    h.send = fakeSend;
    h.rfRecent = fakeRf;
    h.mqttUp = fakeMqtt;
    h.epochNow = fakeEpoch;
    h.ctx = &f;
    return h;
}

// Builds a get_config_response carrying a LoRa block, plus a session passkey.
static std::vector<uint8_t> loraResponse(const uint8_t *key, size_t keyLen) {
    uint8_t block[64];
    size_t bn = writeVarintField(block, sizeof(block), 0, 8, 3);      // hop_limit = 3
    bn = writeVarintField(block, sizeof(block), bn, 10, 27);          // tx_power = 27
    bn = writeStringField(block, sizeof(block), bn, 900, "unknown");  // a field we do not know

    uint8_t cfg[96];
    const size_t cn = writeBytesField(cfg, sizeof(cfg), 0, CONFIG_LORA, block, bn);

    uint8_t msg[160];
    size_t mn = writeBytesField(msg, sizeof(msg), 0, GET_CONFIG_RESPONSE, cfg, cn);
    if (key) mn = writeBytesField(msg, sizeof(msg), mn, SESSION_PASSKEY, key, keyLen);
    return std::vector<uint8_t>(msg, msg + mn);
}

static bool transcriptHas(Session &s, const char *needle) {
    for (int i = 0; i < s.lineCount(); i++) {
        if (strstr(s.line(i)->text, needle)) return true;
    }
    return false;
}

// ── tests ────────────────────────────────────────────────────────────────────
static void testOpenAndHelp() {
    Fake f; Session s;
    ok(s.open(0xdeadbeef, hooksFor(f)), "session opens");
    ok(s.isOpen(), "session reports open");
    s.submit("help", 1000);
    ok(transcriptHas(s, "Danger"), "help prints the command table");
    s.submit("whoami", 1000);
    ok(transcriptHas(s, "deadbeef"), "whoami names the peer");
    s.close();
    ok(!s.isOpen(), "session closes");
}

static void testReadSends() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("info", 1000);
    ok(f.sent.size() == 1, "info sends one packet");
    Response r;
    ok(decodeResponse(f.sent[0].payload.data(), f.sent[0].payload.size(), r)
       && r.field == GET_DEVICE_METADATA_REQUEST, "info is a device metadata request");
    ok(f.sent[0].via == TRANSPORT_RF, "auto picks rf when the node was heard on air");
    s.close();
}

static void testAutoPrefersMqttWhenNoRf() {
    Fake f; f.rf = false; f.mqtt = true;
    Session s; s.open(1, hooksFor(f));
    s.submit("info", 1000);
    ok(f.sent.size() == 1 && f.sent[0].via == TRANSPORT_MQTT,
       "auto falls back to mqtt when rf is stale");
    s.close();
}

static void testMqttRefusedWhenBridgeDown() {
    Fake f; f.mqtt = false;
    Session s; s.open(1, hooksFor(f));
    s.submit("transport mqtt", 1000);
    s.submit("info", 1000);
    ok(f.sent.empty(), "mqtt-only refuses to send with the bridge down");
    ok(transcriptHas(s, "bridge is down"), "and says why");
    s.close();
}

static void testWriteIsReadModifyWrite() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("set lora hop_limit 5", 1000);

    ok(f.sent.size() == 1, "a write sends the GET first");
    Response r;
    decodeResponse(f.sent[0].payload.data(), f.sent[0].payload.size(), r);
    ok(r.field == GET_CONFIG_REQUEST && r.varint == CONFIG_LORA,
       "the first packet is get_config(lora)");
    ok(transcriptHas(s, "fetching lora config"), "the read is announced, not hidden");

    // The remote answers with the block plus a session key.
    const uint8_t key[8] = { 1,2,3,4,5,6,7,8 };
    auto resp = loraResponse(key, sizeof(key));
    s.onAdminReply(f.sent[0].id, resp.data(), resp.size(), TRANSPORT_RF, 2000);

    ok(f.sent.size() == 2, "the SET follows the GET");
    Response r2;
    decodeResponse(f.sent[1].payload.data(), f.sent[1].payload.size(), r2);
    ok(r2.field == SET_CONFIG, "the second packet is set_config");
    ok(r2.hasPasskey && !memcmp(r2.passkey, key, 8), "the SET carries the session key");

    // The spliced block must keep tx_power and the unknown field.
    int wt2; const uint8_t *b2; size_t b2len; uint64_t v2;
    ok(findField(r2.payload, r2.payloadLen, CONFIG_LORA, wt2, b2, b2len, v2),
       "set_config carries the lora block");
    uint64_t hop = 0; const uint8_t *tmp; size_t tmpLen; int tw;
    ok(findField(b2, b2len, 8, tw, tmp, tmpLen, hop) && hop == 5, "hop_limit became 5");
    uint64_t txp = 0;
    ok(findField(b2, b2len, 10, tw, tmp, tmpLen, txp) && txp == 27, "tx_power survived");
    const uint8_t *unk; size_t unkLen; uint64_t uv;
    ok(findField(b2, b2len, 900, tw, unk, unkLen, uv) && unkLen == 7
       && !memcmp(unk, "unknown", 7), "the unknown field survived the write");
    s.close();
}

static void testFailedReadNeverBecomesAWrite() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("set lora hop_limit 5", 1000);
    ok(f.sent.size() == 1, "GET sent");
    // The read times out rather than answering.
    s.service(1000 + 31000);
    ok(f.sent.size() == 1, "a timed-out read never turns into a write");
    ok(transcriptHas(s, "timeout"), "the timeout is reported");
    s.close();
}

static void testDestructiveNeedsConfirmAndKey() {
    Fake f; Session s; s.open(1, hooksFor(f));

    s.submit("reboot 5", 1000);
    ok(f.sent.empty(), "reboot does not send on the first line");
    ok(s.awaitingConfirm(), "reboot arms a confirmation");
    ok(transcriptHas(s, "confirm"), "the confirmation is spelled out");

    // Wrong answer cancels.
    s.submit("yes", 1100);
    ok(f.sent.empty() && !s.awaitingConfirm(), "anything but `confirm` cancels");

    // With no cached key, confirm is refused rather than sending unauthenticated.
    s.submit("reboot 5", 1200);
    s.submit("confirm", 1300);
    ok(f.sent.empty(), "a destructive command refuses without a fresh session key");

    // Mint a key with a read, then it goes.
    s.submit("info", 1400);
    ok(f.sent.size() == 1, "info sent");
    const uint8_t key[8] = { 9,9,9,9,9,9,9,9 };
    uint8_t resp[64];
    size_t rn = writeVarintField(resp, sizeof(resp), 0, GET_DEVICE_METADATA_RESPONSE, 1);
    rn = writeBytesField(resp, sizeof(resp), rn, SESSION_PASSKEY, key, sizeof(key));
    s.onAdminReply(f.sent[0].id, resp, rn, TRANSPORT_RF, 1500);

    s.submit("reboot 5", 1600);
    s.submit("confirm", 1700);
    ok(f.sent.size() == 2, "a confirmed reboot with a fresh key sends");
    Response r;
    decodeResponse(f.sent[1].payload.data(), f.sent[1].payload.size(), r);
    ok(r.field == REBOOT_SECONDS && r.varint == 5, "reboot carries the delay");
    ok(r.hasPasskey, "reboot carries the session key");
    s.close();
}

static void testUnauthorizedNakClosesSession() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("info", 1000);
    s.onRouting(f.sent[0].id, 33, TRANSPORT_RF, 1100);
    ok(!s.isOpen(), "a NOT_AUTHORIZED nak closes the session");
    ok(errorMeansUnauthorized(33) && errorMeansUnauthorized(37),
       "33 and 37 are the unauthorized pair");
    ok(!errorMeansUnauthorized(3), "a timeout is not unauthorized");
}

static void testAckClosesAWrite() {
    Fake f; Session s; s.open(1, hooksFor(f));
    // Mint a key, then a keyed write.
    s.submit("info", 1000);
    const uint8_t key[8] = { 1,1,1,1,1,1,1,1 };
    uint8_t resp[64];
    size_t rn = writeVarintField(resp, sizeof(resp), 0, GET_DEVICE_METADATA_RESPONSE, 1);
    rn = writeBytesField(resp, sizeof(resp), rn, SESSION_PASSKEY, key, sizeof(key));
    s.onAdminReply(f.sent[0].id, resp, rn, TRANSPORT_RF, 1100);

    s.submit("fav add !abcd1234", 1200);
    ok(f.sent.size() == 2, "fav add sends");
    ok(s.busy(), "the write is outstanding");
    s.onRouting(f.sent[1].id, 0, TRANSPORT_RF, 1300);
    ok(!s.busy(), "a routing ack completes a write");
    ok(transcriptHas(s, "ack"), "the ack is shown");
    s.close();
}

static void testBusyRefusesASecondLine() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("info", 1000);
    s.submit("owner", 1010);
    ok(f.sent.size() == 1, "a second command is refused while one is in flight");
    ok(transcriptHas(s, "busy"), "and says so");
    s.close();
}

static void testTranscriptRingAndRevision() {
    Fake f; Session s; s.open(1, hooksFor(f));
    const uint32_t r0 = s.revision();
    for (int i = 0; i < kMaxLines + 10; i++) s.submit("whoami", 1000);
    ok(s.lineCount() == kMaxLines, "the transcript is bounded");
    ok(s.revision() > r0, "the revision advances for polling");
    s.close();
}

// notify(): what the favourites sweep uses to tell an open session that the
// peer it is talking to has stopped accepting us. The operator has to find that
// out among their command receipts, not from the next write failing.
static void testNotifyLandsInTheTranscript() {
    Fake f; Session s;
    // Refused before there is anywhere to put it: a sweep runs whether or not
    // anyone has a terminal open, so this is the ordinary case, not an edge.
    s.notify(LINE_ERR, "dropped on the floor");
    ok(s.lineCount() == 0, "notify on a closed session is a no-op");

    s.open(1, hooksFor(f));
    const uint32_t before = s.revision();
    s.notify(LINE_ERR, "this node no longer accepts administration from us");
    ok(transcriptHas(s, "no longer accepts"), "notify reaches the transcript");
    ok(s.revision() > before, "notify bumps the revision so both UIs repaint");
    s.close();
}

// The renderer: a reply has to show the remote's settings, not its byte count.
static void testInfoRendersFields() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("info", 1000);

    // DeviceMetadata { firmware_version="2.7.1", canShutdown=true, role=2 }
    uint8_t meta[96];
    size_t mn = writeStringField(meta, sizeof(meta), 0, 1, "2.7.1");
    mn = writeVarintField(meta, sizeof(meta), mn, 3, 1);
    mn = writeVarintField(meta, sizeof(meta), mn, 7, 2);
    mn = writeVarintField(meta, sizeof(meta), mn, 777, 42);   // one we have no name for

    uint8_t resp[160];
    const size_t rn = writeBytesField(resp, sizeof(resp), 0,
                                      GET_DEVICE_METADATA_RESPONSE, meta, mn);
    s.onAdminReply(f.sent[0].id, resp, rn, TRANSPORT_RF, 2000);

    ok(transcriptHas(s, "firmware: 2.7.1"), "a string field renders by name");
    ok(transcriptHas(s, "can shutdown: yes"), "a bool renders as yes/no");
    ok(transcriptHas(s, "role: 2"), "a numeric field renders its value");
    ok(transcriptHas(s, "field 777: 42"),
       "a field with no name still renders, by number");
    ok(!transcriptHas(s, "bytes"), "the byte count is gone");
    s.close();
}

static void testConfigResponseNamesItsBlock() {
    Fake f; Session s; s.open(1, hooksFor(f));
    s.submit("config lora", 1000);
    auto resp = loraResponse(nullptr, 0);
    s.onAdminReply(f.sent[0].id, resp.data(), resp.size(), TRANSPORT_RF, 2000);
    ok(transcriptHas(s, "lora:"), "a config reply names its block");
    ok(transcriptHas(s, "field 8: 3"), "and dumps the block's fields by number");
    s.close();
}

// The commands that were advertised but not wired. Each asserts it now reaches
// the radio rather than printing "unknown command".
static void testPreviouslyUnwiredCommands() {
    Fake f; Session s; s.open(1, hooksFor(f));

    // Mint a session key: every write below needs one.
    s.submit("info", 1000);
    const uint8_t key[8] = { 5,5,5,5,5,5,5,5 };
    uint8_t resp[64];
    size_t rn = writeVarintField(resp, sizeof(resp), 0, GET_DEVICE_METADATA_RESPONSE, 1);
    rn = writeBytesField(resp, sizeof(resp), rn, SESSION_PASSKEY, key, sizeof(key));
    s.onAdminReply(f.sent[0].id, resp, rn, TRANSPORT_RF, 1100);
    size_t sent = f.sent.size();

    // module by name, not by number
    s.submit("module telemetry", 1200);
    ok(f.sent.size() == sent + 1, "module <name> sends");
    Response r;
    decodeResponse(f.sent.back().payload.data(), f.sent.back().payload.size(), r);
    ok(r.field == GET_MODULE_CONFIG_REQUEST && r.varint == 6,
       "module telemetry asks for block 6");
    // A read is finished by its *reply*, not by the routing ack -- an ack only
    // says the packet landed, and the answer is still on its way. Answering with
    // an ack alone would leave the session busy until the 30 s timeout.
    {
        uint8_t mr[48];
        const size_t mn = writeBytesField(mr, sizeof(mr), 0, GET_MODULE_CONFIG_RESPONSE,
                                          (const uint8_t *)"\x32\x00", 2);
        s.onAdminReply(f.sent.back().id, mr, mn, TRANSPORT_RF, 1250);
    }
    ok(!s.busy(), "a reply clears the read");
    sent = f.sent.size();

    // fixedpos: a Position with sfixed32 coordinates
    s.submit("fixedpos 51.5 -0.12 35", 1300);
    ok(f.sent.size() == sent + 1, "fixedpos sends");
    Response rp;
    decodeResponse(f.sent.back().payload.data(), f.sent.back().payload.size(), rp);
    ok(rp.field == SET_FIXED_POSITION, "fixedpos is set_fixed_position");
    ok(rp.hasPasskey, "fixedpos carries the session key");
    ok(rp.payload && rp.payloadLen >= 5, "fixedpos carries a Position");
    if (rp.payload && rp.payloadLen >= 5) {
        // latitude_i is sfixed32, so the tag is (1<<3)|5 and four bytes follow,
        // little-endian. findField() deliberately does not report 32-bit fields,
        // which is why this reads the bytes rather than asking for field 1.
        ok(rp.payload[0] == ((1 << 3) | 5), "latitude_i is wire type 5, not a varint");
        const int32_t latI = (int32_t)((uint32_t)rp.payload[1]
                                       | ((uint32_t)rp.payload[2] << 8)
                                       | ((uint32_t)rp.payload[3] << 16)
                                       | ((uint32_t)rp.payload[4] << 24));
        ok(latI == 515000000, "latitude_i is degrees x 1e7");
    }
    s.onRouting(f.sent.back().id, 0, TRANSPORT_RF, 1350);
    sent = f.sent.size();

    // fixedpos clear
    s.submit("fixedpos clear", 1400);
    Response rc;
    decodeResponse(f.sent.back().payload.data(), f.sent.back().payload.size(), rc);
    ok(rc.field == REMOVE_FIXED_POSITION, "fixedpos clear removes it");
    s.onRouting(f.sent.back().id, 0, TRANSPORT_RF, 1450);
    sent = f.sent.size();

    // set owner reads the User record before replacing it
    s.submit("set owner long Hello", 1500);
    ok(f.sent.size() == sent + 1, "set owner sends");
    Response ro;
    decodeResponse(f.sent.back().payload.data(), f.sent.back().payload.size(), ro);
    ok(ro.field == GET_OWNER_REQUEST, "set owner reads first");
    ok(s.busy(), "and is waiting on that read");

    // Answer with a User carrying a short name that must survive.
    uint8_t user[64];
    size_t un = writeStringField(user, sizeof(user), 0, 2, "Old Name");
    un = writeStringField(user, sizeof(user), un, 3, "ON");
    uint8_t ownerResp[96];
    const size_t on = writeBytesField(ownerResp, sizeof(ownerResp), 0,
                                      GET_OWNER_RESPONSE, user, un);
    s.onAdminReply(f.sent.back().id, ownerResp, on, TRANSPORT_RF, 1600);

    Response rs;
    decodeResponse(f.sent.back().payload.data(), f.sent.back().payload.size(), rs);
    ok(rs.field == SET_OWNER, "and then writes set_owner");
    const uint8_t *nm; size_t nmLen; uint64_t nv; int nw;
    ok(findField(rs.payload, rs.payloadLen, 2, nw, nm, nmLen, nv)
       && nmLen == 5 && !memcmp(nm, "Hello", 5), "long name changed");
    ok(findField(rs.payload, rs.payloadLen, 3, nw, nm, nmLen, nv)
       && nmLen == 2 && !memcmp(nm, "ON", 2), "short name survived");

    // The write is outstanding until its ack; without this the next line is
    // refused as busy and never reaches the length rule being tested.
    s.onRouting(f.sent.back().id, 0, TRANSPORT_RF, 1650);
    ok(!s.busy(), "a routing ack completes set_owner");

    s.submit("set owner short TOOLONG", 1700);
    ok(transcriptHas(s, "at most 4"), "an over-long short name is refused");
    s.close();
}

int main() {
    testPreviouslyUnwiredCommands();
    testInfoRendersFields();
    testConfigResponseNamesItsBlock();
    testOpenAndHelp();
    testReadSends();
    testAutoPrefersMqttWhenNoRf();
    testMqttRefusedWhenBridgeDown();
    testWriteIsReadModifyWrite();
    testFailedReadNeverBecomesAWrite();
    testDestructiveNeedsConfirmAndKey();
    testUnauthorizedNakClosesSession();
    testAckClosesAWrite();
    testBusyRefusesASecondLine();
    testTranscriptRingAndRevision();
    testNotifyLandsInTheTranscript();

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
