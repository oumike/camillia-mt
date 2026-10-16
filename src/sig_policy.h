#pragma once
// Meshtastic 2.8.1's packet signature policy (SecurityConfig.packet_signature_policy,
// field 9): which received packets a node accepts, given what their XEdDSA
// signature said. Mirrors checkXeddsaReceivePolicy() in upstream's Router.cpp,
// so a Camillia node set to a policy drops exactly what a stock 2.8.1 node on
// that policy drops.
//
// Kept free of Arduino and of the packet structs so the decision can be tested
// on the host (tests/test_sig_policy.cpp). The caller does the crypto and
// passes in the outcome.
#include <stddef.h>
#include <stdint.h>

// Values are upstream's enum, so they mean the same thing in a YAML backup or
// over remote admin.
enum SigPolicy : uint8_t {
    // Accept unsigned packets. Still drops a signature that fails, a malformed
    // one, and a signed first-contact NodeInfo that does not check out.
    // Upstream's default, and ours.
    SIG_POLICY_COMPATIBLE = 0,
    // As Compatible, and also drops an unsigned broadcast from a node that has
    // signed before, when that broadcast had room for a signature. That is a
    // downgrade: a signer always signs broadcasts that fit.
    SIG_POLICY_BALANCED   = 1,
    // Accept only packets with a verified signature, or that arrived
    // PKI-encrypted. Everything else is dropped, including any node whose key
    // we do not hold yet.
    SIG_POLICY_STRICT     = 2,
};
#define SIG_POLICY_COUNT 3

// Anything out of range (a hand-edited YAML, a stray byte) reads as the default.
inline uint8_t sigPolicyCoerce(int v) {
    return (v >= 0 && v < SIG_POLICY_COUNT) ? (uint8_t)v : (uint8_t)SIG_POLICY_COMPATIBLE;
}

inline const char *sigPolicyName(uint8_t v) {
    switch (sigPolicyCoerce(v)) {
        case SIG_POLICY_BALANCED: return "Balanced";
        case SIG_POLICY_STRICT:   return "Strict";
        default:                  return "Compatible";
    }
}

// What the packet's Data.xeddsa_signature turned out to be.
enum SigCheck : uint8_t {
    SIG_CHECK_UNSIGNED,       // no signature field (or an empty one)
    SIG_CHECK_MALFORMED,      // present, but not 64 bytes
    SIG_CHECK_VERIFIED,       // 64 bytes, verified against the key we hold
    SIG_CHECK_FAILED,         // 64 bytes, did not verify against the key we hold
    // 64 bytes and we hold no key for the sender, so there was nothing to check
    // against. A NodeInfo is the exception, see SigPolicyInput::bootstrap.
    SIG_CHECK_NO_KEY,
};

// A signed NodeInfo from a node whose key we do not hold yet. 2.8.1 accepts it
// only if the key it carries hashes to the sender's node ID and the signature
// verifies under that key; otherwise anyone could introduce a node under any
// ID with a key of their choosing, signed.
enum SigBootstrap : uint8_t {
    SIG_BOOTSTRAP_NA,         // not a NodeInfo, or not SIG_CHECK_NO_KEY
    SIG_BOOTSTRAP_VERIFIED,
    SIG_BOOTSTRAP_INVALID,
};

struct SigPolicyInput {
    uint8_t      policy;          // SigPolicy
    SigCheck     check;
    SigBootstrap bootstrap;       // only read when check == SIG_CHECK_NO_KEY
    bool         pkiEncrypted;    // decrypted with the sender's PKI key
    bool         knownSigner;     // this sender has passed a signature check before
    bool         broadcast;       // addressed to everyone
    size_t       dataLen;         // encoded Data, unsigned (for the downgrade rule)
};

struct SigPolicyResult {
    bool accept;
    // The packet proved the sender holds its key: remember it as a signer, so
    // Balanced can catch a later unsigned broadcast from it.
    bool markSigner;
};

inline SigPolicyResult sigPolicyDecide(const SigPolicyInput &in) {
    const uint8_t policy = sigPolicyCoerce(in.policy);
    switch (in.check) {
        case SIG_CHECK_VERIFIED: return {true, true};
        // Under every policy: a signature that does not verify means the key we
        // hold is not theirs, or the packet is not.
        case SIG_CHECK_FAILED:    return {false, false};
        // Under every policy. A crafted partial signature would otherwise pass
        // as unsigned while dodging Balanced's size test.
        case SIG_CHECK_MALFORMED: return {false, false};
        case SIG_CHECK_NO_KEY:
            if (in.bootstrap == SIG_BOOTSTRAP_VERIFIED) return {true, true};
            if (in.bootstrap == SIG_BOOTSTRAP_INVALID)  return {false, false};
            return {policy != SIG_POLICY_STRICT, false};
        case SIG_CHECK_UNSIGNED:
        default:
            break;
    }

    // Unsigned. PKI decryption already proves who sent it.
    if (in.pkiEncrypted) return {true, false};
    if (policy == SIG_POLICY_STRICT) return {false, false};
    if (policy == SIG_POLICY_COMPATIBLE) return {true, false};

    // Balanced: drop only what a signer always signs, a broadcast whose signed
    // encoding would have fitted in a LoRa frame. Upstream also covers unicasts
    // when the receiving node is itself licensed (ham mode, where DMs go
    // plaintext and are signed); Camillia has no licensed mode.
    if (in.knownSigner && in.broadcast) {
        // XEDDSA_SIGNATURE_FIELD_BYTES (66) + MESH_HEADER_BYTES (16) <=
        // MESH_MAX_FRAME_BYTES (255), as upstream's canonicalSignableSize() test.
        if (in.dataLen + 66 + 16 <= 255) return {false, false};
    }
    return {true, false};
}
