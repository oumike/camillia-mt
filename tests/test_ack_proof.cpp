// test-deps: sha256_portable.cpp
#include "../src/ack_proof.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static void hexTo(const char *hex, uint8_t *out) {
    for (size_t i = 0; hex[2 * i]; i++) {
        unsigned b;
        sscanf(hex + 2 * i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

// RFC 4231 test cases 1, 2 and 6 (the last with a key longer than the block).
static void testHmacVectors() {
    uint8_t out[32], want[32];

    uint8_t key1[20];
    memset(key1, 0x0b, sizeof(key1));
    hmacSha256(key1, sizeof(key1), (const uint8_t *)"Hi There", 8, out);
    hexTo("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", want);
    CHECK(memcmp(out, want, 32) == 0);

    const char *msg2 = "what do ya want for nothing?";
    hmacSha256((const uint8_t *)"Jefe", 4, (const uint8_t *)msg2, strlen(msg2), out);
    hexTo("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", want);
    CHECK(memcmp(out, want, 32) == 0);

    uint8_t key6[131];
    memset(key6, 0xaa, sizeof(key6));
    const char *msg6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmacSha256(key6, sizeof(key6), (const uint8_t *)msg6, strlen(msg6), out);
    hexTo("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", want);
    CHECK(memcmp(out, want, 32) == 0);
}

// The proof is the first 8 bytes of the HMAC over "ack" | from | to | id | routing.
static void testComputeMatchesDefinition() {
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    const uint8_t routing[] = { 0x18, 0x00 };   // Routing{error_reason = NONE}

    uint8_t proof[ACK_PROOF_BYTES];
    ackProofCompute(key, 0x11223344, 0xA1B2C3D4, 0xDEADBEEF, routing, sizeof(routing), proof);

    const uint8_t msg[] = { 'a', 'c', 'k',
                            0x44, 0x33, 0x22, 0x11,
                            0xD4, 0xC3, 0xB2, 0xA1,
                            0xEF, 0xBE, 0xAD, 0xDE,
                            0x18, 0x00 };
    uint8_t full[32];
    hmacSha256(key, 32, msg, sizeof(msg), full);
    CHECK(memcmp(proof, full, ACK_PROOF_BYTES) == 0);

    // Each binding changes the value.
    uint8_t other[ACK_PROOF_BYTES];
    ackProofCompute(key, 0xA1B2C3D4, 0x11223344, 0xDEADBEEF, routing, sizeof(routing), other);
    CHECK(memcmp(proof, other, ACK_PROOF_BYTES) != 0);   // direction
    ackProofCompute(key, 0x11223344, 0xA1B2C3D4, 0xDEADBEF0, routing, sizeof(routing), other);
    CHECK(memcmp(proof, other, ACK_PROOF_BYTES) != 0);   // request id
    const uint8_t nak[] = { 0x18, 0x05 };
    ackProofCompute(key, 0x11223344, 0xA1B2C3D4, 0xDEADBEEF, nak, sizeof(nak), other);
    CHECK(memcmp(proof, other, ACK_PROOF_BYTES) != 0);   // ack vs nak
}

static void testAppendExtractRoundTrip() {
    const uint8_t proof[ACK_PROOF_BYTES] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t routing[32] = { 0x18, 0x00 };
    const size_t n = ackProofAppend(routing, 2, sizeof(routing), proof);
    CHECK(n == 12);
    CHECK(routing[2] == 0x22 && routing[3] == 0x08);

    uint8_t got[ACK_PROOF_BYTES], stripped[32];
    size_t strippedLen = 0;
    CHECK(ackProofExtract(routing, n, got, stripped, &strippedLen) == ACK_PROOF_FOUND);
    CHECK(memcmp(got, proof, ACK_PROOF_BYTES) == 0);
    CHECK(strippedLen == 2 && stripped[0] == 0x18 && stripped[1] == 0x00);

    // A bare ack has no proof, which is not the same as a bad one.
    CHECK(ackProofExtract(routing, 2, got, stripped, &strippedLen) == ACK_PROOF_ABSENT);

    // No room: the caller sends the ack unproven.
    CHECK(ackProofAppend(routing, 2, 11, proof) == 0);
}

static void testExtractRejectsMalformed() {
    const uint8_t proof[ACK_PROOF_BYTES] = {};
    uint8_t twice[32] = { 0x18, 0x00 };
    size_t n = ackProofAppend(twice, 2, sizeof(twice), proof);
    n = ackProofAppend(twice, n, sizeof(twice), proof);
    uint8_t got[ACK_PROOF_BYTES], stripped[32];
    size_t strippedLen = 0;
    CHECK(ackProofExtract(twice, n, got, stripped, &strippedLen) == ACK_PROOF_MALFORMED);

    const uint8_t shortProof[] = { 0x18, 0x00, 0x22, 0x04, 1, 2, 3, 4 };
    CHECK(ackProofExtract(shortProof, sizeof(shortProof), got, stripped, &strippedLen)
          == ACK_PROOF_MALFORMED);

    const uint8_t truncated[] = { 0x18, 0x00, 0x22, 0x08, 1, 2 };
    CHECK(ackProofExtract(truncated, sizeof(truncated), got, stripped, &strippedLen)
          == ACK_PROOF_MALFORMED);
}

// A field placed after the proof is kept in the stripped copy, so the MAC covers it.
static void testUnknownFieldAfterProofIsCovered() {
    const uint8_t routing[] = { 0x18, 0x00,
                                0x22, 0x08, 9, 9, 9, 9, 9, 9, 9, 9,
                                0x28, 0x07 };   // field 5 varint, unknown here
    uint8_t got[ACK_PROOF_BYTES], stripped[32];
    size_t strippedLen = 0;
    CHECK(ackProofExtract(routing, sizeof(routing), got, stripped, &strippedLen) == ACK_PROOF_FOUND);
    const uint8_t want[] = { 0x18, 0x00, 0x28, 0x07 };
    CHECK(strippedLen == sizeof(want) && memcmp(stripped, want, sizeof(want)) == 0);
}

int main() {
    testHmacVectors();
    testComputeMatchesDefinition();
    testAppendExtractRoundTrip();
    testExtractRejectsMalformed();
    testUnknownFieldAfterProofIsCovered();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
