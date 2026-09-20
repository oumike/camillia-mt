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

int main() {
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

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
