#pragma once
// XEdDSA signature verification (Meshtastic 2.8, Data.xeddsa_signature).
//
// 2.8 signs packets with XEdDSA over the Curve25519 identity every node already
// has — no new key material and no key exchange. Verification is ordinary
// Ed25519 verification; the only extra step is converting the sender's
// Curve25519 public key into the Ed25519 public key it corresponds to, which is
// what this module exists to do.
//
// Only verification is implemented. Signing needs the private-scalar path
// (Ed25519 signing from a supplied scalar rather than a seed), which the Crypto
// library we link does not expose — see docs and issue #60 B1b.
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Size of an XEdDSA signature on the wire. 2.8 emits either 0 or exactly this
// and rejects anything between, so a partial signature is not a thing to
// tolerate — it is a malformed packet.
#define XEDDSA_SIGNATURE_BYTES 64

// Convert a Curve25519 public key (Montgomery u) to the Ed25519 public key
// (Edwards y, sign bit clear) that XEdDSA signs under.
//
// The map is y = (u - 1) / (u + 1) mod 2^255-19, from RFC 7748 §4.1. A
// Curve25519 public key carries only u, so the Edwards x coordinate — a single
// sign bit in the Ed25519 encoding — cannot be recovered from it. XEdDSA
// resolves that by always normalising the signer's key to sign bit zero, so
// this clears the bit unconditionally rather than taking it as input.
void xeddsaCurveToEdPub(const uint8_t curvePubKey[32], uint8_t edPubKey[32]);

// Verify a signature over one packet.
//
// The signed message is fromNode || packetId || portnum || payload, each of the
// three integers as four little-endian bytes. Binding all three means a
// signature cannot be lifted onto another packet, re-attributed to another
// sender, or replayed against a different port.
//
// Returns false for a wrong signature, a malformed key, or an all-zero public
// key (a peer whose key we have never learned) — never true on doubt.
bool xeddsaVerify(const uint8_t senderPubKey[32],
                  uint32_t fromNode, uint32_t packetId, uint32_t portnum,
                  const uint8_t *payload, size_t payloadLen,
                  const uint8_t signature[XEDDSA_SIGNATURE_BYTES]);
