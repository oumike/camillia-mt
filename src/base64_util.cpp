#include "base64_util.h"

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64Encode(const uint8_t *in, size_t len, char *out) {
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t)in[i] << 16;
        if (i + 1 < len) b |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) b |= (uint32_t)in[i + 2];
        out[o++] = kB64Chars[(b >> 18) & 0x3F];
        out[o++] = kB64Chars[(b >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? kB64Chars[(b >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? kB64Chars[b & 0x3F] : '=';
    }
    out[o] = '\0';
}

int base64Decode(const char *in, uint8_t *out, int maxLen) {
    uint32_t acc = 0;
    int bits = 0;
    int o = 0;

    for (const char *p = in; *p && *p != '='; p++) {
        int v;
        char c = *p;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else continue;

        acc = (acc << 6) | (uint32_t)v;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            if (o < maxLen) out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }

    return o;
}
