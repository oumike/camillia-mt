// Host test for src/admin_proto.cpp. Build and run with tests/run.sh.
//
// The splice cases are the reason this file exists. Everything else here is an
// ordinary round trip; the splice is the one operation whose failure mode is
// silently reconfiguring somebody else's radio, so it is tested against
// byte-exact expectations rather than "did it parse".
#include "../src/admin_proto.h"

#include <stdio.h>
#include <string.h>

using namespace AdminProto;

static int g_fail = 0;
static int g_run = 0;

static void ok(bool cond, const char *what) {
    g_run++;
    if (!cond) {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}

static void okBytes(const uint8_t *got, size_t gotLen,
                    const uint8_t *want, size_t wantLen, const char *what) {
    g_run++;
    if (gotLen != wantLen || memcmp(got, want, wantLen) != 0) {
        g_fail++;
        printf("  FAIL  %s\n        got ", what);
        for (size_t i = 0; i < gotLen; i++) printf("%02x", got[i]);
        printf("\n        want ");
        for (size_t i = 0; i < wantLen; i++) printf("%02x", want[i]);
        printf("\n");
    }
}

// ── primitives ───────────────────────────────────────────────────────────────
static void testVarint() {
    uint8_t buf[16];
    size_t n = writeVarint(buf, sizeof(buf), 0, 300);
    ok(n == 2 && buf[0] == 0xAC && buf[1] == 0x02, "varint 300 encodes to ac 02");

    uint64_t v = 0;
    ok(readVarint(buf, n, 0, v) == n && v == 300, "varint 300 round trips");

    // Capacity is respected rather than overrun.
    uint8_t tiny[1];
    ok(writeVarint(tiny, sizeof(tiny), 0, 300) == 0, "varint refuses to overflow");

    // A run with no terminator must fail rather than read past the end.
    const uint8_t unterminated[3] = { 0x80, 0x80, 0x80 };
    ok(readVarint(unterminated, sizeof(unterminated), 0, v) == 0,
       "unterminated varint is rejected");
}

static void testFindField() {
    uint8_t buf[64];
    size_t n = writeVarintField(buf, sizeof(buf), 0, 1, 7);
    n = writeStringField(buf, sizeof(buf), n, 2, "hi");
    ok(n > 0, "mixed message encodes");

    int wt = -1;
    const uint8_t *p = nullptr;
    size_t len = 0;
    uint64_t var = 0;

    ok(findField(buf, n, 1, wt, p, len, var) && wt == WT_VARINT && var == 7,
       "findField reads a varint");
    ok(findField(buf, n, 2, wt, p, len, var) && wt == WT_LEN && len == 2
       && !memcmp(p, "hi", 2), "findField reads a string");
    ok(!findField(buf, n, 3, wt, p, len, var), "findField reports an absent field");
}

// ── the splice ───────────────────────────────────────────────────────────────
static void testSpliceReplacesInPlace() {
    // field 1 = 1, field 2 = 3, field 3 = 9
    uint8_t src[32];
    size_t n = writeVarintField(src, sizeof(src), 0, 1, 1);
    n = writeVarintField(src, sizeof(src), n, 2, 3);
    n = writeVarintField(src, sizeof(src), n, 3, 9);

    uint8_t want[32];
    size_t wn = writeVarintField(want, sizeof(want), 0, 1, 1);
    wn = writeVarintField(want, sizeof(want), wn, 2, 5);
    wn = writeVarintField(want, sizeof(want), wn, 3, 9);

    uint8_t dst[32];
    const size_t dn = spliceVarint(src, n, 2, 5, dst, sizeof(dst));
    okBytes(dst, dn, want, wn, "splice replaces a field and keeps position");
}

static void testSpliceAppendsWhenAbsent() {
    uint8_t src[32];
    size_t n = writeVarintField(src, sizeof(src), 0, 1, 1);

    uint8_t want[32];
    size_t wn = writeVarintField(want, sizeof(want), 0, 1, 1);
    wn = writeVarintField(want, sizeof(want), wn, 4, 42);

    uint8_t dst[32];
    const size_t dn = spliceVarint(src, n, 4, 42, dst, sizeof(dst));
    okBytes(dst, dn, want, wn, "splice appends a field that was absent");
}

static void testSpliceKeepsUnknownFields() {
    // This is the case the whole design turns on: a remote running newer
    // firmware carries fields we have no name for, in wire types we do not
    // otherwise handle. They must come back byte-identical.
    uint8_t src[64];
    size_t n = 0;
    n = writeVarintField(src, sizeof(src), n, 1, 3);          // hop_limit, say
    n = writeStringField(src, sizeof(src), n, 900, "future"); // unknown, len
    // an unknown 32-bit field
    n = writeTag(src, sizeof(src), n, 901, WT_32BIT);
    src[n++] = 0xDE; src[n++] = 0xAD; src[n++] = 0xBE; src[n++] = 0xEF;
    // an unknown 64-bit field
    n = writeTag(src, sizeof(src), n, 902, WT_64BIT);
    for (int i = 0; i < 8; i++) src[n++] = (uint8_t)i;

    uint8_t want[64];
    size_t wn = 0;
    wn = writeVarintField(want, sizeof(want), wn, 1, 5);      // the one change
    wn = writeStringField(want, sizeof(want), wn, 900, "future");
    wn = writeTag(want, sizeof(want), wn, 901, WT_32BIT);
    want[wn++] = 0xDE; want[wn++] = 0xAD; want[wn++] = 0xBE; want[wn++] = 0xEF;
    wn = writeTag(want, sizeof(want), wn, 902, WT_64BIT);
    for (int i = 0; i < 8; i++) want[wn++] = (uint8_t)i;

    uint8_t dst[64];
    const size_t dn = spliceVarint(src, n, 1, 5, dst, sizeof(dst));
    okBytes(dst, dn, want, wn, "splice preserves unknown fields byte-for-byte");
}

static void testSpliceCollapsesRepeats() {
    // Protobuf permits a repeated field; replacing it must leave exactly one
    // value rather than two copies of the new one.
    uint8_t src[32];
    size_t n = writeVarintField(src, sizeof(src), 0, 2, 1);
    n = writeVarintField(src, sizeof(src), n, 2, 2);

    uint8_t want[32];
    const size_t wn = writeVarintField(want, sizeof(want), 0, 2, 9);

    uint8_t dst[32];
    const size_t dn = spliceVarint(src, n, 2, 9, dst, sizeof(dst));
    okBytes(dst, dn, want, wn, "splice collapses a repeated field to one value");
}

static void testSpliceBytes() {
    uint8_t src[64];
    size_t n = writeStringField(src, sizeof(src), 0, 1, "old");
    n = writeVarintField(src, sizeof(src), n, 2, 7);

    uint8_t want[64];
    size_t wn = writeStringField(want, sizeof(want), 0, 1, "newer");
    wn = writeVarintField(want, sizeof(want), wn, 2, 7);

    uint8_t dst[64];
    const size_t dn = spliceBytes(src, n, 1, (const uint8_t *)"newer", 5,
                                  dst, sizeof(dst));
    okBytes(dst, dn, want, wn, "splice rewrites a length-delimited field");
}

static void testSpliceRefusesBadInput() {
    uint8_t dst[8];
    // Truncated length-delimited record.
    const uint8_t bad[] = { 0x0A, 0x05, 'a', 'b' };
    ok(spliceVarint(bad, sizeof(bad), 1, 1, dst, sizeof(dst)) == 0,
       "splice rejects a truncated message");

    uint8_t src[32];
    const size_t n = writeVarintField(src, sizeof(src), 0, 1, 1);
    ok(spliceVarint(src, n, 1, 1, dst, 1) == 0,
       "splice refuses a destination that is too small");
}

// ── AdminMessage ─────────────────────────────────────────────────────────────
static void testEncodeDecode() {
    uint8_t buf[64];
    size_t n = encodeGetRequest(buf, sizeof(buf), GET_CONFIG_REQUEST, CONFIG_LORA);
    ok(n > 0, "get_config_request encodes");

    Response r;
    ok(decodeResponse(buf, n, r) && r.field == GET_CONFIG_REQUEST
       && r.varint == CONFIG_LORA, "get_config_request decodes");
    ok(!r.hasPasskey, "a read carries no passkey");

    // A response carrying both a payload and the session credential.
    const uint8_t key[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t resp[64];
    size_t rn = writeBytesField(resp, sizeof(resp), 0, GET_CONFIG_RESPONSE,
                                (const uint8_t *)"\x30\x05", 2);
    rn = writeBytesField(resp, sizeof(resp), rn, SESSION_PASSKEY, key, sizeof(key));

    Response r2;
    ok(decodeResponse(resp, rn, r2), "response decodes");
    ok(r2.field == GET_CONFIG_RESPONSE, "response field is the config response");
    ok(r2.payloadLen == 2 && r2.payload[0] == 0x30, "response payload is exposed");
    ok(r2.hasPasskey && r2.passkeyLen == 8 && !memcmp(r2.passkey, key, 8),
       "response passkey is captured");
}

static void testWriteCarriesPasskey() {
    const uint8_t key[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
    uint8_t buf[64];
    const size_t n = encodeVarintCommand(buf, sizeof(buf), REBOOT_SECONDS, 5,
                                         key, sizeof(key));
    ok(n > 0, "reboot encodes");

    Response r;
    ok(decodeResponse(buf, n, r) && r.field == REBOOT_SECONDS && r.varint == 5,
       "reboot decodes");
    ok(r.hasPasskey && !memcmp(r.passkey, key, 8), "a write carries the passkey");

    // An over-long passkey is refused rather than truncated -- a wrong
    // credential earns a NAK, and a truncated one is a wrong credential.
    const uint8_t longKey[16] = { 0 };
    ok(encodeVarintCommand(buf, sizeof(buf), REBOOT_SECONDS, 5, longKey,
                           sizeof(longKey)) == 0,
       "an over-long passkey is refused");
}

static void testNames() {
    ok(!strcmp(fieldName(SET_CONFIG), "set_config"), "field name");
    ok(!strcmp(fieldName(4242), "unknown"), "unknown field name");
    ok(configBlockFromName("lora") == CONFIG_LORA, "block name parses");
    ok(configBlockFromName("nope") == 0, "unknown block name is rejected");
    ok(!strcmp(configBlockName(CONFIG_SECURITY), "security"), "block name renders");
}

int main() {
    testVarint();
    testFindField();
    testSpliceReplacesInPlace();
    testSpliceAppendsWhenAbsent();
    testSpliceKeepsUnknownFields();
    testSpliceCollapsesRepeats();
    testSpliceBytes();
    testSpliceRefusesBadInput();
    testEncodeDecode();
    testWriteCarriesPasskey();
    testNames();

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
