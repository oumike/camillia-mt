#include "../src/sig_policy.h"

#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static SigPolicyInput in(uint8_t policy, SigCheck check) {
    SigPolicyInput i = {};
    i.policy = policy;
    i.check = check;
    i.bootstrap = SIG_BOOTSTRAP_NA;
    i.broadcast = true;
    i.dataLen = 40;
    return i;
}

static bool accepts(const SigPolicyInput &i) { return sigPolicyDecide(i).accept; }

// Failed, malformed and bad-bootstrap signatures drop under every policy;
// a verified one is accepted and marks the sender.
static void testSignedAllPolicies() {
    for (uint8_t p = 0; p < SIG_POLICY_COUNT; p++) {
        SigPolicyResult r = sigPolicyDecide(in(p, SIG_CHECK_VERIFIED));
        CHECK(r.accept && r.markSigner);
        CHECK(!accepts(in(p, SIG_CHECK_FAILED)));
        CHECK(!accepts(in(p, SIG_CHECK_MALFORMED)));

        SigPolicyInput b = in(p, SIG_CHECK_NO_KEY);
        b.bootstrap = SIG_BOOTSTRAP_INVALID;
        CHECK(!accepts(b));
        b.bootstrap = SIG_BOOTSTRAP_VERIFIED;
        r = sigPolicyDecide(b);
        CHECK(r.accept && r.markSigner);
    }
    // Signed but unverifiable: only Strict drops it.
    CHECK(accepts(in(SIG_POLICY_COMPATIBLE, SIG_CHECK_NO_KEY)));
    CHECK(accepts(in(SIG_POLICY_BALANCED, SIG_CHECK_NO_KEY)));
    CHECK(!accepts(in(SIG_POLICY_STRICT, SIG_CHECK_NO_KEY)));
    CHECK(!sigPolicyDecide(in(SIG_POLICY_COMPATIBLE, SIG_CHECK_NO_KEY)).markSigner);
}

static void testUnsigned() {
    SigPolicyInput i = in(SIG_POLICY_COMPATIBLE, SIG_CHECK_UNSIGNED);
    i.knownSigner = true;
    CHECK(accepts(i));                          // Compatible never minds

    i.policy = SIG_POLICY_STRICT;
    i.knownSigner = false;
    CHECK(!accepts(i));
    i.pkiEncrypted = true;
    CHECK(accepts(i));                          // PKI is authenticated already

    // Balanced: unsigned from a non-signer is fine.
    i = in(SIG_POLICY_BALANCED, SIG_CHECK_UNSIGNED);
    CHECK(accepts(i));
    // From a known signer, a broadcast that would have fitted signed is a downgrade.
    i.knownSigner = true;
    CHECK(!accepts(i));
    i.dataLen = 255 - 16 - 66;                  // exactly fits: still a downgrade
    CHECK(!accepts(i));
    i.dataLen = 255 - 16 - 66 + 1;              // too long to sign: accepted
    CHECK(accepts(i));
    // A unicast is not something a signer signs.
    i.dataLen = 40;
    i.broadcast = false;
    CHECK(accepts(i));
    // Nor a PKI DM.
    i.broadcast = true;
    i.pkiEncrypted = true;
    CHECK(accepts(i));
    CHECK(!sigPolicyDecide(i).markSigner);
}

static void testCoerce() {
    CHECK(sigPolicyCoerce(0) == SIG_POLICY_COMPATIBLE);
    CHECK(sigPolicyCoerce(2) == SIG_POLICY_STRICT);
    CHECK(sigPolicyCoerce(3) == SIG_POLICY_COMPATIBLE);
    CHECK(sigPolicyCoerce(255) == SIG_POLICY_COMPATIBLE);
    CHECK(sigPolicyCoerce(-1) == SIG_POLICY_COMPATIBLE);
    // An out-of-range stored value behaves as Compatible, not as Strict.
    SigPolicyInput i = in(7, SIG_CHECK_UNSIGNED);
    CHECK(accepts(i));
}

int main() {
    testSignedAllPolicies();
    testUnsigned();
    testCoerce();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
