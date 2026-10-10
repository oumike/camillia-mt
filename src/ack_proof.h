#pragma once
// Routing.ack_proof (Meshtastic 2.8.1): a receipt for a direct message that only
// its real recipient could have produced.
//
// An explicit ack is a ROUTING_APP packet under channel encryption, and on the
// default channel anyone can forge one. When the two ends already share a
// Curve25519 key, the acking node can append an 8-byte proof:
//
//   ack_proof = HMAC-SHA256(shared_key,
//                           "ack" | LE32(ack from) | LE32(ack to)
//                                 | LE32(request_id) | routing)[0..8)
//
// shared_key is SHA256(X25519(our private key, their public key)), the same key
// PKI direct messages are encrypted under; routing is the encoded Routing message
// without the ack_proof field. The proof travels as Routing field 4.
//
// Upstream treats a proof as advisory: a valid one marks the ack verified, and a
// missing one changes nothing. So does this.
//
// Kept free of Arduino headers so the host tests can check it.
#include <stddef.h>
#include <stdint.h>

#define ACK_PROOF_BYTES 8
#define ACK_PROOF_FIELD 4   // Routing.ack_proof

// HMAC-SHA256 (RFC 2104). Exposed for the host tests' RFC 4231 vectors.
void hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *msg, size_t msgLen,
                uint8_t out[32]);

// The proof for an ack or nak sent from ackFrom to ackTo, answering requestId.
void ackProofCompute(const uint8_t sharedKey[32], uint32_t ackFrom, uint32_t ackTo,
                     uint32_t requestId, const uint8_t *routing, size_t routingLen,
                     uint8_t proof[ACK_PROOF_BYTES]);

// Append the proof to an encoded Routing message as field 4. Returns the new
// length, or 0 if it does not fit (the caller then sends the ack unproven).
size_t ackProofAppend(uint8_t *routing, size_t routingLen, size_t cap,
                      const uint8_t proof[ACK_PROOF_BYTES]);

enum AckProofFind { ACK_PROOF_ABSENT, ACK_PROOF_FOUND, ACK_PROOF_MALFORMED };

// Find the proof in a received Routing message and copy out the message without
// it (stripped must hold routingLen bytes). Two proofs, or one of the wrong
// length, is malformed: two fields mean two possible excisions and two verdicts.
AckProofFind ackProofExtract(const uint8_t *routing, size_t routingLen,
                             uint8_t proof[ACK_PROOF_BYTES],
                             uint8_t *stripped, size_t *strippedLen);
