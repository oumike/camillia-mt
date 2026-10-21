#pragma once
// Public-key pinning for remote nodes (trust on first use).
//
// The first X25519 key we learn for a node is kept. A later NodeInfo (or any
// other remote source) offering a *different* key does not replace it: the
// offer is parked here as a pending conflict until the user explicitly accepts
// or rejects it on the device. Without this, anyone able to spoof `from` with
// an unsigned NodeInfo could swap in their own key and read our DMs to that
// node, or send DMs that authenticate as it. Upstream Meshtastic pins too.
//
// A small fixed table rather than 32 more bytes on every NodeEntry: conflicts
// are rare and MAX_NODES entries are not. RAM only -- a node whose key really
// changed rebroadcasts its NodeInfo, which raises the conflict again after a
// reboot.
//
// Kept free of Arduino so it can be tested on the host (tests/test_key_pin.cpp).
#include <stddef.h>
#include <stdint.h>

struct KeyConflict {
    uint32_t nodeId;          // 0 = empty slot
    uint32_t atMs;            // millis() of the latest offer of this key
    uint8_t  key[32];         // the offered (not yet trusted) key
    // The offering packet carried an XEdDSA signature that verifies under the
    // offered key. Proves the sender holds that key's private half -- not that
    // the sender is who it claims to be.
    bool     signedByNewKey;
};

enum KeyOffer : uint8_t {
    KEY_OFFER_FIRST,          // no key held yet: caller stores it (TOFU)
    KEY_OFFER_SAME,           // matches the pinned key: nothing to do
    KEY_OFFER_REJECTED,       // the user already rejected this exact key
    KEY_OFFER_PENDING,        // same key as the conflict already pending
    KEY_OFFER_NEW_CONFLICT,   // newly recorded conflict: warn the user once
};

static constexpr int KEY_PIN_MAX_CONFLICTS = 8;
static constexpr int KEY_PIN_MAX_REJECTS   = 8;
static constexpr int KEY_PIN_REJECT_PREFIX = 8;
// Keys of nodes pushed out of a full node table. Full keys, since a match is
// restored as the node's key: an attacker flooding fake IDs to evict a node
// must not get its next NodeInfo trusted as a first key.
static constexpr int KEY_PIN_MAX_EVICTED  = 32;

class KeyPinTable {
public:
    // Judge one offered key. `held` is null when no key is held for the node.
    // Never writes the node's key itself: that is the caller's job, and only
    // for KEY_OFFER_FIRST.
    KeyOffer offer(uint32_t nodeId, const uint8_t *held, const uint8_t offered[32],
                   bool signedByNewKey, uint32_t nowMs);

    const KeyConflict *find(uint32_t nodeId) const;
    // Remove the pending conflict and hand it back (the accept path).
    // `expected`, when given, is the key the user was shown: if the pending key
    // has changed since, nothing is taken and false is returned.
    bool take(uint32_t nodeId, KeyConflict &out, const uint8_t *expected = nullptr);
    // Drop the pending conflict and remember its key so the same offer does
    // not warn again. False when nothing was pending, or (with `expected`) the
    // pending key is no longer the one the user was shown.
    bool reject(uint32_t nodeId, const uint8_t *expected = nullptr);
    // Forget a pending conflict without remembering it (node removed/evicted).
    void drop(uint32_t nodeId);
    void clear();
    int  pendingCount() const;

    // Remember the key of a node evicted from the table (oldest goes first).
    void rememberEvicted(uint32_t nodeId, const uint8_t key[32]);
    // The remembered key of an evicted node, removed from the list. False when
    // none is remembered.
    bool takeEvicted(uint32_t nodeId, uint8_t out[32]);
    int  evictedCount() const;

private:
    KeyConflict _c[KEY_PIN_MAX_CONFLICTS] = {};
    struct Rejected {
        uint32_t nodeId;
        uint8_t  prefix[KEY_PIN_REJECT_PREFIX];
    };
    Rejected _r[KEY_PIN_MAX_REJECTS] = {};
    uint8_t  _rNext = 0;
    struct Evicted {
        uint32_t nodeId;
        uint8_t  key[32];
    };
    Evicted  _e[KEY_PIN_MAX_EVICTED] = {};
    uint8_t  _eNext = 0;

    bool _isRejected(uint32_t nodeId, const uint8_t key[32]) const;
};
