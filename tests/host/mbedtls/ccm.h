#pragma once
#include <stddef.h>
typedef struct { int unused; } mbedtls_ccm_context;
enum { MBEDTLS_CIPHER_ID_AES = 2 };
static inline void mbedtls_ccm_init(mbedtls_ccm_context *) {}
static inline void mbedtls_ccm_free(mbedtls_ccm_context *) {}
static inline int mbedtls_ccm_setkey(mbedtls_ccm_context *, int, const unsigned char *, unsigned) { return -1; }
static inline int mbedtls_ccm_encrypt_and_tag(mbedtls_ccm_context *, size_t, const unsigned char *, size_t,
                                              const unsigned char *, size_t, const unsigned char *,
                                              unsigned char *, unsigned char *, size_t) { return -1; }
static inline int mbedtls_ccm_auth_decrypt(mbedtls_ccm_context *, size_t, const unsigned char *, size_t,
                                           const unsigned char *, size_t, const unsigned char *,
                                           unsigned char *, const unsigned char *, size_t) { return -1; }
