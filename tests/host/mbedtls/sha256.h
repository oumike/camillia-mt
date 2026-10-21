#pragma once
#include <stddef.h>
#include <string.h>
static inline int mbedtls_sha256(const unsigned char *, size_t, unsigned char *out, int) { memset(out, 0, 32); return 0; }
