// test-cflags: -Ihost
#include "../src/base64_util.h"
#include "test_support.h"

#include <array>
#include <vector>

static void testRfc4648Vectors() {
    const struct { const char *plain; const char *encoded; } cases[] = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
    };
    for (const auto &c : cases) {
        const size_t n = strlen(c.plain);
        std::vector<char> encoded(strlen(c.encoded) + 1);
        base64Encode(reinterpret_cast<const uint8_t *>(c.plain), n, encoded.data());
        CHECK(strcmp(encoded.data(), c.encoded) == 0);

        uint8_t decoded[8] = {};
        CHECK(base64Decode(c.encoded, decoded, sizeof(decoded)) == static_cast<int>(n));
        CHECK(memcmp(decoded, c.plain, n) == 0);
    }
}

static void testBinaryAndUrlSafeKeys() {
    const uint8_t key[] = {0x00, 0xFB, 0xFF, 0xFF, 0x10};
    char encoded[9];
    base64Encode(key, sizeof(key), encoded);
    CHECK(strcmp(encoded, "APv//xA=") == 0);
    for (const char *text : {"APv//xA=", "APv__xA=", "APv__xA", " APv/\r\n/xA= "}) {
        uint8_t decoded[5];
        CHECK(base64Decode(text, decoded, sizeof(decoded)) == 5);
        CHECK(memcmp(decoded, key, sizeof(key)) == 0);
    }
    uint8_t decoded[3];
    CHECK(base64Decode("-_8=", decoded, sizeof(decoded)) == 2);
    CHECK(decoded[0] == 0xFB && decoded[1] == 0xFF);
}

static void testOutputCapacity() {
    for (int cap = 0; cap <= 6; cap++) {
        std::array<uint8_t, 8> out;
        out.fill(0xA5);
        CHECK(base64Decode("Zm9vYmFy", out.data() + 1, cap) == cap);
        CHECK(memcmp(out.data() + 1, "foobar", cap) == 0);
        CHECK(out[0] == 0xA5);
        for (size_t i = static_cast<size_t>(cap) + 1; i < out.size(); i++) {
            CHECK(out[i] == 0xA5);
        }
    }
    uint8_t out = 0xA5;
    CHECK(base64Decode("", &out, 1) == 0 && out == 0xA5);
    CHECK(base64Decode("=Zm9v", &out, 1) == 0 && out == 0xA5);
    CHECK(base64Decode("Z", &out, 1) == 0 && out == 0xA5);
    CHECK(base64Decode("Zg==ignored", &out, 1) == 1 && out == 'f');
}

static void testEveryByteAndRemainder() {
    std::array<uint8_t, 256> input;
    for (size_t i = 0; i < input.size(); i++) input[i] = static_cast<uint8_t>(i);
    for (size_t n = 0; n <= input.size(); n++) {
        std::vector<char> encoded(4 * ((n + 2) / 3) + 1);
        base64Encode(input.data(), n, encoded.data());
        std::array<uint8_t, 256> decoded = {};
        CHECK(base64Decode(encoded.data(), decoded.data(), decoded.size()) == static_cast<int>(n));
        CHECK(memcmp(decoded.data(), input.data(), n) == 0);
    }
}

int main() {
    testRfc4648Vectors();
    testBinaryAndUrlSafeKeys();
    testOutputCapacity();
    testEveryByteAndRemainder();
    return test::report();
}
