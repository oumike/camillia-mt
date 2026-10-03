// Vendored from camillia-chat-server 9754229; edit there, not here.
#pragma once
// Minimal SHA-256 (FIPS 180-4), dependency-free so cs_proto builds natively.
#include <stddef.h>
#include <stdint.h>

void sha256Portable(const uint8_t *data, size_t len, uint8_t out[32]);

struct Sha256Ctx {
    uint32_t h[8];
    uint8_t  buf[64];
    uint64_t total;
    size_t   bufLen;
};
void sha256Init(Sha256Ctx &c);
void sha256Update(Sha256Ctx &c, const uint8_t *data, size_t len);
void sha256Final(Sha256Ctx &c, uint8_t out[32]);
