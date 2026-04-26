#pragma once
#include <Arduino.h>

// Shared base64 helpers used by config import/export paths.
// Output buffer for encoding must be at least (4 * ((len + 2) / 3)) + 1 bytes.
void base64Encode(const uint8_t *in, size_t len, char *out);

// Decodes a base64 string into out (up to maxLen bytes).
// Accepts standard and URL-safe alphabets. Returns decoded byte count.
int base64Decode(const char *in, uint8_t *out, int maxLen);
