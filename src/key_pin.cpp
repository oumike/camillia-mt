#include "key_pin.h"
#include <string.h>

KeyOffer KeyPinTable::offer(uint32_t nodeId, const uint8_t *held, const uint8_t offered[32],
                            bool signedByNewKey, uint32_t nowMs) {
    if (!held) {
        // A conflict cannot outlive the key it was against.
        drop(nodeId);
        return KEY_OFFER_FIRST;
    }
    if (memcmp(held, offered, 32) == 0) return KEY_OFFER_SAME;
    if (_isRejected(nodeId, offered)) return KEY_OFFER_REJECTED;

    KeyConflict *slot = nullptr;
    for (int i = 0; i < KEY_PIN_MAX_CONFLICTS; i++) {
        if (_c[i].nodeId == nodeId) { slot = &_c[i]; break; }
    }
    if (slot && memcmp(slot->key, offered, 32) == 0) {
        slot->atMs = nowMs;
        if (signedByNewKey) slot->signedByNewKey = true;
        return KEY_OFFER_PENDING;
    }
    if (!slot) {
        for (int i = 0; i < KEY_PIN_MAX_CONFLICTS; i++) {
            if (_c[i].nodeId == 0) { slot = &_c[i]; break; }
        }
    }
    if (!slot) {
        // Full: the stalest offer goes. Wrap-safe age, as millis() rolls over.
        slot = &_c[0];
        for (int i = 1; i < KEY_PIN_MAX_CONFLICTS; i++) {
            if ((uint32_t)(nowMs - _c[i].atMs) > (uint32_t)(nowMs - slot->atMs)) slot = &_c[i];
        }
    }
    // A different key from the one already pending replaces it: the latest
    // offer is the one the user is asked about.
    slot->nodeId = nodeId;
    slot->atMs = nowMs;
    memcpy(slot->key, offered, 32);
    slot->signedByNewKey = signedByNewKey;
    return KEY_OFFER_NEW_CONFLICT;
}

const KeyConflict *KeyPinTable::find(uint32_t nodeId) const {
    if (nodeId == 0) return nullptr;
    for (int i = 0; i < KEY_PIN_MAX_CONFLICTS; i++) {
        if (_c[i].nodeId == nodeId) return &_c[i];
    }
    return nullptr;
}

bool KeyPinTable::take(uint32_t nodeId, KeyConflict &out, const uint8_t *expected) {
    const KeyConflict *c = find(nodeId);
    if (!c) return false;
    if (expected && memcmp(c->key, expected, 32) != 0) return false;
    out = *c;
    drop(nodeId);
    return true;
}

bool KeyPinTable::reject(uint32_t nodeId, const uint8_t *expected) {
    const KeyConflict *c = find(nodeId);
    if (!c) return false;
    if (expected && memcmp(c->key, expected, 32) != 0) return false;
    Rejected &r = _r[_rNext];
    _rNext = (uint8_t)((_rNext + 1) % KEY_PIN_MAX_REJECTS);
    r.nodeId = nodeId;
    memcpy(r.prefix, c->key, KEY_PIN_REJECT_PREFIX);
    drop(nodeId);
    return true;
}

void KeyPinTable::drop(uint32_t nodeId) {
    if (nodeId == 0) return;
    for (int i = 0; i < KEY_PIN_MAX_CONFLICTS; i++) {
        if (_c[i].nodeId == nodeId) memset(&_c[i], 0, sizeof(_c[i]));
    }
}

void KeyPinTable::clear() {
    memset(_c, 0, sizeof(_c));
    memset(_r, 0, sizeof(_r));
    _rNext = 0;
    memset(_e, 0, sizeof(_e));
    _eNext = 0;
}

void KeyPinTable::rememberEvicted(uint32_t nodeId, const uint8_t key[32]) {
    if (nodeId == 0 || !key) return;
    Evicted *slot = nullptr;
    for (int i = 0; i < KEY_PIN_MAX_EVICTED; i++) {
        if (_e[i].nodeId == nodeId) { slot = &_e[i]; break; }
    }
    if (!slot) {
        // A ring: the slot written longest ago is the next to go.
        slot = &_e[_eNext];
        _eNext = (uint8_t)((_eNext + 1) % KEY_PIN_MAX_EVICTED);
    }
    slot->nodeId = nodeId;
    memcpy(slot->key, key, 32);
}

bool KeyPinTable::takeEvicted(uint32_t nodeId, uint8_t out[32]) {
    if (nodeId == 0) return false;
    for (int i = 0; i < KEY_PIN_MAX_EVICTED; i++) {
        if (_e[i].nodeId == nodeId) {
            if (out) memcpy(out, _e[i].key, 32);
            memset(&_e[i], 0, sizeof(_e[i]));
            return true;
        }
    }
    return false;
}

int KeyPinTable::evictedCount() const {
    int n = 0;
    for (int i = 0; i < KEY_PIN_MAX_EVICTED; i++) {
        if (_e[i].nodeId != 0) n++;
    }
    return n;
}

int KeyPinTable::pendingCount() const {
    int n = 0;
    for (int i = 0; i < KEY_PIN_MAX_CONFLICTS; i++) {
        if (_c[i].nodeId != 0) n++;
    }
    return n;
}

bool KeyPinTable::_isRejected(uint32_t nodeId, const uint8_t key[32]) const {
    for (int i = 0; i < KEY_PIN_MAX_REJECTS; i++) {
        if (_r[i].nodeId == nodeId && memcmp(_r[i].prefix, key, KEY_PIN_REJECT_PREFIX) == 0) {
            return true;
        }
    }
    return false;
}
