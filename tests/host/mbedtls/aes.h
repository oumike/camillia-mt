// Host stand-in. Not AES: a fixed XOR keystream, enough that a keyed channel
// never "decrypts" a cleartext frame into the same bytes.
#pragma once
#include <stddef.h>
typedef struct { int unused; } mbedtls_aes_context;
static inline void mbedtls_aes_init(mbedtls_aes_context *) {}
static inline void mbedtls_aes_free(mbedtls_aes_context *) {}
static inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *, const unsigned char *, unsigned) { return 0; }
static inline int mbedtls_aes_crypt_ctr(mbedtls_aes_context *, size_t len, size_t *, unsigned char *,
                                        unsigned char *, const unsigned char *in, unsigned char *out) {
    for (size_t i = 0; i < len; i++) out[i] = in[i] ^ 0xFF;
    return 0;
}
