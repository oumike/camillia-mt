#include "ack_proof.h"

#include <string.h>

#include "sha256_portable.h"

void hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *msg, size_t msgLen,
                uint8_t out[32]) {
    uint8_t k[64] = {};
    if (keyLen > sizeof(k)) {
        sha256Portable(key, keyLen, k);
    } else if (keyLen) {
        memcpy(k, key, keyLen);
    }
    uint8_t pad[64];
    uint8_t inner[32];

    Sha256Ctx c;
    for (size_t i = 0; i < sizeof(pad); i++) pad[i] = k[i] ^ 0x36;
    sha256Init(c);
    sha256Update(c, pad, sizeof(pad));
    if (msgLen) sha256Update(c, msg, msgLen);
    sha256Final(c, inner);

    for (size_t i = 0; i < sizeof(pad); i++) pad[i] = k[i] ^ 0x5c;
    sha256Init(c);
    sha256Update(c, pad, sizeof(pad));
    sha256Update(c, inner, sizeof(inner));
    sha256Final(c, out);

    memset(k, 0, sizeof(k));
    memset(pad, 0, sizeof(pad));
}

static void putLe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void ackProofCompute(const uint8_t sharedKey[32], uint32_t ackFrom, uint32_t ackTo,
                     uint32_t requestId, const uint8_t *routing, size_t routingLen,
                     uint8_t proof[ACK_PROOF_BYTES]) {
    // "ack" with no NUL, then the three integers, then the Routing bytes.
    uint8_t msg[3 + 12 + 256];
    if (routingLen > sizeof(msg) - 15) routingLen = 0;  // cannot happen for a Routing that fits a packet
    memcpy(msg, "ack", 3);
    putLe32(msg + 3, ackFrom);
    putLe32(msg + 7, ackTo);
    putLe32(msg + 11, requestId);
    if (routingLen) memcpy(msg + 15, routing, routingLen);

    uint8_t digest[32];
    hmacSha256(sharedKey, 32, msg, 15 + routingLen, digest);
    memcpy(proof, digest, ACK_PROOF_BYTES);
    memset(digest, 0, sizeof(digest));
}

size_t ackProofAppend(uint8_t *routing, size_t routingLen, size_t cap,
                      const uint8_t proof[ACK_PROOF_BYTES]) {
    if (routingLen + 2 + ACK_PROOF_BYTES > cap) return 0;
    routing[routingLen++] = (ACK_PROOF_FIELD << 3) | 2;
    routing[routingLen++] = ACK_PROOF_BYTES;
    memcpy(routing + routingLen, proof, ACK_PROOF_BYTES);
    return routingLen + ACK_PROOF_BYTES;
}

// Varint reader for the walk below; returns the offset after it, or 0.
static size_t readVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &v) {
    v = 0;
    for (int shift = 0; off < len && shift < 64; shift += 7) {
        const uint8_t b = buf[off++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return off;
    }
    return 0;
}

AckProofFind ackProofExtract(const uint8_t *routing, size_t routingLen,
                             uint8_t proof[ACK_PROOF_BYTES],
                             uint8_t *stripped, size_t *strippedLen) {
    size_t fieldStart = 0, fieldEnd = 0;
    bool found = false;
    size_t i = 0;
    while (i < routingLen) {
        const size_t tagStart = i;
        uint64_t tag;
        i = readVarint(routing, routingLen, i, tag);
        if (!i) return ACK_PROOF_MALFORMED;
        const uint32_t field = (uint32_t)(tag >> 3), wt = (uint32_t)(tag & 7);
        if (wt == 0) {
            uint64_t v;
            i = readVarint(routing, routingLen, i, v);
            if (!i) return ACK_PROOF_MALFORMED;
        } else if (wt == 1 || wt == 5) {
            i += (wt == 1) ? 8 : 4;
            if (i > routingLen) return ACK_PROOF_MALFORMED;
        } else if (wt == 2) {
            uint64_t sz;
            i = readVarint(routing, routingLen, i, sz);
            if (!i || sz > routingLen - i) return ACK_PROOF_MALFORMED;
            if (field == ACK_PROOF_FIELD) {
                if (found || sz != ACK_PROOF_BYTES) return ACK_PROOF_MALFORMED;
                memcpy(proof, routing + i, ACK_PROOF_BYTES);
                found = true;
                fieldStart = tagStart;
                fieldEnd = i + (size_t)sz;
            }
            i += (size_t)sz;
        } else {
            return ACK_PROOF_MALFORMED;
        }
    }
    if (!found) return ACK_PROOF_ABSENT;
    memcpy(stripped, routing, fieldStart);
    memcpy(stripped + fieldStart, routing + fieldEnd, routingLen - fieldEnd);
    *strippedLen = routingLen - (fieldEnd - fieldStart);
    return ACK_PROOF_FOUND;
}
