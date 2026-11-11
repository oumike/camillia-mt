#include "../src/sig_policy.h"

#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static SigPolicyInput in(uint8_t policy, SigCheck check) {
    SigPolicyInput i = {};
    i.policy = policy;
    i.check = check;
    i.bootstrap = SIG_BOOTSTRAP_NA;
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
    i.pkiEncrypted = false;
    i.exempt = true;
    CHECK(accepts(i));                          // ACK, key verification, AES-CCM
    CHECK(!sigPolicyDecide(i).markSigner);

    // Balanced (2.8.2): unsigned is accepted, from a known signer too. Its
    // guard is on identity, not on the packet.
    i = in(SIG_POLICY_BALANCED, SIG_CHECK_UNSIGNED);
    CHECK(accepts(i));
    i.knownSigner = true;
    CHECK(accepts(i));
    CHECK(!sigPolicyDecide(i).markSigner);

    // An exempt packet still drops when its signature fails.
    i = in(SIG_POLICY_STRICT, SIG_CHECK_FAILED);
    i.exempt = true;
    CHECK(!accepts(i));
}

static void testIdentityRefusal() {
    // Compatible never refuses.
    CHECK(!sigRefusesUnsignedIdentity(SIG_POLICY_COMPATIBLE, false, true, false));
    for (uint8_t p = SIG_POLICY_BALANCED; p <= SIG_POLICY_STRICT; p++) {
        CHECK(sigRefusesUnsignedIdentity(p, false, true, false));
        CHECK(!sigRefusesUnsignedIdentity(p, true, true, false));    // signed
        CHECK(!sigRefusesUnsignedIdentity(p, false, false, false));  // never signed
        CHECK(!sigRefusesUnsignedIdentity(p, false, true, true));    // AES-CCM channel
    }
    CHECK(!sigRefusesUnsignedIdentity(9, false, true, false));       // out of range: Compatible
}

static void testUnicastSigning() {
    CHECK(!sigSignsUnicastPort(SIG_POLICY_COMPATIBLE, 4));
    CHECK(sigSignsUnicastPort(SIG_POLICY_BALANCED, 4));
    CHECK(sigSignsUnicastPort(SIG_POLICY_STRICT, 3));
    CHECK(!sigSignsUnicastPort(SIG_POLICY_STRICT, 1));
    CHECK(!sigSignsUnicastPort(SIG_POLICY_STRICT, 70));
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
    testIdentityRefusal();
    testUnicastSigning();
    testCoerce();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
