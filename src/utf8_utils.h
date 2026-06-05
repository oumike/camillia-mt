#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace utf8util {

inline bool isContinuationByte(uint8_t b) {
    return (b & 0xC0u) == 0x80u;
}

inline size_t clampPrefixBytes(const char *src, size_t maxBytes) {
    if (!src || maxBytes == 0) return 0;

    size_t n = strnlen(src, maxBytes);
    while (n > 0 && src[n] != '\0' && isContinuationByte((uint8_t)src[n])) {
        n--;
    }
    return n;
}

inline size_t copyTruncate(char *dst, size_t dstSize, const char *src) {
    if (!dst || dstSize == 0) return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    size_t n = clampPrefixBytes(src, dstSize - 1);
    if (n > 0) memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

inline size_t copyTruncateBytes(char *dst, size_t dstSize,
                                const uint8_t *src, size_t srcLen) {
    if (!dst || dstSize == 0) return 0;
    if (!src || srcLen == 0) {
        dst[0] = '\0';
        return 0;
    }

    size_t maxCopy = (srcLen < (dstSize - 1)) ? srcLen : (dstSize - 1);
    size_t n = maxCopy;
    while (n > 0 && n < srcLen && isContinuationByte(src[n])) {
        n--;
    }

    if (n > 0) memcpy(dst, src, n);
    dst[n] = '\0';
    return n;
}

} // namespace utf8util
