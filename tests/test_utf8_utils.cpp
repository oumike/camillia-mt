#include "../src/utf8_utils.h"
#include "test_support.h"

#include <array>

static void testEveryMultibyteBoundary() {
    const char text[] = "A" "\xC3\xA9" "\xE2\x82\xAC" "\xF0\x9F\x8C\xB8" "Z";
    const size_t prefixes[] = {0, 1, 1, 3, 3, 3, 6, 6, 6, 6, 10, 11, 11};
    for (size_t limit = 0; limit < sizeof(prefixes) / sizeof(prefixes[0]); limit++) {
        CHECK(utf8util::clampPrefixBytes(text, limit) == prefixes[limit]);
        std::array<char, 16> out;
        out.fill('!');
        const size_t n = utf8util::copyTruncate(out.data() + 1, limit + 1, text);
        CHECK(n == prefixes[limit]);
        CHECK(memcmp(out.data() + 1, text, n) == 0);
        CHECK(out[n + 1] == '\0');
        CHECK(out[0] == '!' && out[limit + 2] == '!');
    }
}

static void testLengthDelimitedInput() {
    // No NUL after the source: the byte overload must obey srcLen, not strlen.
    const uint8_t bytes[] = {'A', 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x8C, 0xB8};
    const size_t prefixes[] = {0, 0, 1, 1, 3, 3, 3, 6, 6, 6, 6, 10};
    for (size_t cap = 0; cap <= sizeof(bytes) + 1; cap++) {
        std::array<char, 14> out;
        out.fill('!');
        const size_t n = utf8util::copyTruncateBytes(out.data() + 1, cap, bytes, sizeof(bytes));
        CHECK(n == prefixes[cap]);
        CHECK(memcmp(out.data() + 1, bytes, n) == 0);
        if (cap) CHECK(out[n + 1] == '\0');
        CHECK(out[0] == '!' && out[cap + 1] == '!');
    }

    const uint8_t embeddedNul[] = {'a', 0, 'b'};
    char out[4];
    CHECK(utf8util::copyTruncateBytes(out, sizeof(out), embeddedNul, sizeof(embeddedNul)) == 3);
    CHECK(memcmp(out, "a\0b\0", 4) == 0);
}

static void testEmptyAndNullInputs() {
    char out[] = "keep";
    CHECK(utf8util::clampPrefixBytes(nullptr, 8) == 0);
    CHECK(utf8util::copyTruncate(nullptr, 0, "x") == 0);
    CHECK(utf8util::copyTruncate(out, 0, "x") == 0 && strcmp(out, "keep") == 0);
    CHECK(utf8util::copyTruncate(out, sizeof(out), nullptr) == 0 && out[0] == '\0');
    CHECK(utf8util::copyTruncate(out, 1, "x") == 0 && out[0] == '\0');
    CHECK(utf8util::copyTruncateBytes(nullptr, 0, nullptr, 0) == 0);
    CHECK(utf8util::copyTruncateBytes(out, sizeof(out), nullptr, 10) == 0 && out[0] == '\0');
}

int main() {
    testEveryMultibyteBoundary();
    testLengthDelimitedInput();
    testEmptyAndNullInputs();
    return test::report();
}
