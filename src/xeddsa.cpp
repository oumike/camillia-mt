#include "xeddsa.h"
#include <Ed25519.h>
#include <string.h>

// ── Field arithmetic mod p = 2^255 - 19 ───────────────────────
//
// Written out here rather than borrowed from the Crypto library, whose field
// operations are private to Curve25519 and only reachable through a macro the
// library provides for its own unit tests. Depending on that from production
// code would tie us to an internal representation that is free to change.
//
// Representation is eight 32-bit limbs, little-endian (limb 0 is least
// significant), holding a value below 2^256. Values are kept reduced below p
// only where it matters — mulmod reduces, and toBytes finishes the job.
//
// This runs once per never-before-seen sender key and the result is cached, so
// the schoolbook multiply and the Fermat inversion below are not on any hot
// path. Clarity is worth more here than speed: every line is checkable against
// the textbook form, and correctness is not observable at runtime — a wrong
// answer produces a key that simply fails to verify, silently.
namespace {

typedef uint32_t fe[8];

void feFromBytes(fe out, const uint8_t in[32]) {
    for (int i = 0; i < 8; i++) {
        out[i] = (uint32_t)in[i * 4]
               | ((uint32_t)in[i * 4 + 1] << 8)
               | ((uint32_t)in[i * 4 + 2] << 16)
               | ((uint32_t)in[i * 4 + 3] << 24);
    }
    // RFC 7748 §5 requires the top bit of a received u coordinate to be ignored.
    out[7] &= 0x7FFFFFFFu;
}

// Add 'carry' worth of p back in / take it out, keeping the value in range.
// Adds b*2^256 mod p, i.e. b*38, into the eight limbs and returns any further
// carry out of the top.
uint32_t feAddScaled(fe r, uint32_t b) {
    uint64_t c = (uint64_t)b * 38u;
    for (int i = 0; i < 8; i++) {
        c += r[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

void feAdd(fe r, const fe a, const fe b) {
    uint64_t c = 0;
    for (int i = 0; i < 8; i++) {
        c += (uint64_t)a[i] + b[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    // A carry out of 2^256 is 2^256 mod p = 38.
    uint32_t extra = (uint32_t)c;
    while (extra) extra = feAddScaled(r, extra);
}

void feSub(fe r, const fe a, const fe b) {
    // Plain limb-wise subtract first. A borrow out of the top means the true
    // result is negative and what sits in r is that result plus 2^256.
    uint32_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t d = (uint64_t)a[i] - (uint64_t)b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (d >> 63) & 1u;   // set when the subtraction went negative
    }
    if (borrow) {
        // r == a - b + 2^256, and we want a - b + 2p = a - b + 2^256 - 38.
        // So take 38 back off.
        uint32_t b2 = 38;
        for (int i = 0; i < 8 && b2; i++) {
            uint64_t d = (uint64_t)r[i] - b2;
            r[i] = (uint32_t)d;
            b2 = ((d >> 63) & 1u) ? 1u : 0u;
        }
    }
}

void feMul(fe r, const fe a, const fe b) {
    uint32_t t[16] = {0};
    for (int i = 0; i < 8; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 8; j++) {
            uint64_t cur = (uint64_t)t[i + j] + (uint64_t)a[i] * b[j] + carry;
            t[i + j] = (uint32_t)cur;
            carry = cur >> 32;
        }
        uint64_t k = (uint64_t)t[i + 8] + carry;
        t[i + 8] = (uint32_t)k;
        // Schoolbook cannot carry past t[15] for 256x256 -> 512.
    }
    // Reduce: 2^256 == 38 (mod p), so fold the high half into the low half.
    uint32_t lo[8], hi[8];
    for (int i = 0; i < 8; i++) { lo[i] = t[i]; hi[i] = t[i + 8]; }

    uint64_t carry = 0;
    for (int i = 0; i < 8; i++) {
        carry += (uint64_t)lo[i] + (uint64_t)hi[i] * 38u;
        lo[i] = (uint32_t)carry;
        carry >>= 32;
    }
    uint32_t extra = (uint32_t)carry;
    while (extra) extra = feAddScaled(lo, extra);

    for (int i = 0; i < 8; i++) r[i] = lo[i];
}

void feSquare(fe r, const fe a) { feMul(r, a, a); }

void feOne(fe r) { r[0] = 1; for (int i = 1; i < 8; i++) r[i] = 0; }

// Inversion by Fermat: a^(p-2) mod p. p-2 = 2^255 - 21, whose binary form is
// 250 ones, then 0, 1, 0, 1, 1 reading from the top — the standard addition
// chain is spelled out as a plain square-and-multiply over the exponent bits so
// it can be checked against the constant rather than trusted.
void feInvert(fe r, const fe a) {
    static const uint8_t kPMinus2[32] = {
        0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F,
    };
    fe result;
    feOne(result);
    // Most significant bit first.
    for (int byte = 31; byte >= 0; byte--) {
        for (int bit = 7; bit >= 0; bit--) {
            feSquare(result, result);
            if ((kPMinus2[byte] >> bit) & 1) feMul(result, result, a);
        }
    }
    for (int i = 0; i < 8; i++) r[i] = result[i];
}

void feToBytes(uint8_t out[32], const fe in) {
    // Bring the value fully below p: subtract p once if it fits, twice at most.
    uint32_t t[8];
    for (int i = 0; i < 8; i++) t[i] = in[i];

    for (int pass = 0; pass < 2; pass++) {
        // Trial-subtract p = 2^255 - 19, i.e. add 19 and check bit 255.
        uint32_t probe[8];
        uint64_t c = 19;
        for (int i = 0; i < 8; i++) {
            c += t[i];
            probe[i] = (uint32_t)c;
            c >>= 32;
        }
        // If adding 19 set bit 255, then t >= p and the reduced value is
        // exactly probe with bit 255 cleared.
        if (probe[7] & 0x80000000u) {
            probe[7] &= 0x7FFFFFFFu;
            for (int i = 0; i < 8; i++) t[i] = probe[i];
        }
    }

    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(t[i]);
        out[i * 4 + 1] = (uint8_t)(t[i] >> 8);
        out[i * 4 + 2] = (uint8_t)(t[i] >> 16);
        out[i * 4 + 3] = (uint8_t)(t[i] >> 24);
    }
}

} // namespace

void xeddsaCurveToEdPub(const uint8_t curvePubKey[32], uint8_t edPubKey[32]) {
    fe u, one, num, den, denInv, y;
    feFromBytes(u, curvePubKey);
    feOne(one);
    feSub(num, u, one);     // u - 1
    feAdd(den, u, one);     // u + 1
    feInvert(denInv, den);
    feMul(y, num, denInv);
    feToBytes(edPubKey, y);
    // XEdDSA normalises to sign bit zero; the map cannot recover it anyway.
    edPubKey[31] &= 0x7F;
}

bool xeddsaVerify(const uint8_t senderPubKey[32],
                  uint32_t fromNode, uint32_t packetId, uint32_t portnum,
                  const uint8_t *payload, size_t payloadLen,
                  const uint8_t signature[XEDDSA_SIGNATURE_BYTES]) {
    if (!senderPubKey || !signature) return false;
    if (payloadLen && !payload) return false;

    // A key we have never learned is all zeroes. Verifying against it would be
    // meaningless, and the birational map divides by u + 1 — which is 1 here,
    // so it would not even fail loudly.
    bool allZero = true;
    for (int i = 0; i < 32; i++) {
        if (senderPubKey[i]) { allZero = false; break; }
    }
    if (allZero) return false;

    // fromNode || packetId || portnum || payload. The three integers go out as
    // four little-endian bytes each, which is what upstream's memcpy of a
    // uint32 produces on every platform Meshtastic targets. Written explicitly
    // so this does not silently change meaning if it is ever built big-endian.
    uint8_t msg[12 + 256];
    if (payloadLen > sizeof(msg) - 12) return false;
    const uint32_t hdr[3] = { fromNode, packetId, portnum };
    for (int i = 0; i < 3; i++) {
        msg[i * 4]     = (uint8_t)(hdr[i]);
        msg[i * 4 + 1] = (uint8_t)(hdr[i] >> 8);
        msg[i * 4 + 2] = (uint8_t)(hdr[i] >> 16);
        msg[i * 4 + 3] = (uint8_t)(hdr[i] >> 24);
    }
    if (payloadLen) memcpy(msg + 12, payload, payloadLen);

    uint8_t edPub[32];
    xeddsaCurveToEdPub(senderPubKey, edPub);

    return Ed25519::verify(signature, edPub, msg, 12 + payloadLen);
}
