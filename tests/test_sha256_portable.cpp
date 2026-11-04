#include "../src/sha256_portable.h"
#include "test_support.h"

#include <algorithm>
#include <string>

static void checkDigest(const uint8_t digest[32], const char *expected) {
    char hex[65];
    for (size_t i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    CHECK(strcmp(hex, expected) == 0);
}

static void testKnownAnswers() {
    const struct { const char *text; const char *digest; } cases[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
         "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
         "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
    };
    for (const auto &c : cases) {
        uint8_t digest[32];
        sha256Portable(reinterpret_cast<const uint8_t *>(c.text), strlen(c.text), digest);
        checkDigest(digest, c.digest);
    }
}

static void testPaddingAndStreamingBoundaries() {
    // Independent hashlib SHA-256 vectors for 'a' repeated n times.
    const struct { size_t n; const char *digest; } cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
        {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    };
    for (const auto &c : cases) {
        const std::string input(c.n, 'a');
        const auto *bytes = reinterpret_cast<const uint8_t *>(input.data());
        uint8_t digest[32];
        sha256Portable(bytes, input.size(), digest);
        checkDigest(digest, c.digest);
        for (size_t split = 0; split <= c.n; split++) {
            Sha256Ctx ctx;
            sha256Init(ctx);
            sha256Update(ctx, bytes, split);
            sha256Update(ctx, bytes, 0);
            sha256Update(ctx, bytes + split, c.n - split);
            sha256Final(ctx, digest);
            checkDigest(digest, c.digest);
        }
    }
}

static void testMillionAAndIndependentContexts() {
    Sha256Ctx a, b;
    sha256Init(a);
    sha256Init(b);
    const uint8_t abc[] = {'a', 'b', 'c'};
    uint8_t block[1000];
    memset(block, 'a', sizeof(block));
    for (int i = 0; i < 1000; i++) {
        sha256Update(a, block, sizeof(block));
        if (i < 3) sha256Update(b, abc + i, 1);
    }
    uint8_t digest[32];
    sha256Final(a, digest);
    checkDigest(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    sha256Final(b, digest);
    checkDigest(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

int main() {
    testKnownAnswers();
    testPaddingAndStreamingBoundaries();
    testMillionAAndIndependentContexts();
    return test::report();
}
