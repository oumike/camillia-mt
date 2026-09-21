#include "admin_peers.h"
#include <Preferences.h>

AdminPeers AdminPeerList;

// Blob-backed storage in the shared "camillia" namespace, exactly as the ignore
// list does it. The struct is fixed-width and written whole, so a shorter blob
// from an older build simply loads fewer entries.
static constexpr const char *kNamespace = "camillia";
static constexpr const char *kKey       = "adminPeers";

void AdminPeers::init() {
    _count = 0;
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return;
    if (prefs.isKey(kKey)) {
        size_t blobLen = prefs.getBytesLength(kKey);
        const size_t maxLen = sizeof(_peers);
        if (blobLen > maxLen) blobLen = maxLen;
        const size_t got = prefs.getBytes(kKey, _peers, blobLen);
        _count = (int)(got / sizeof(AdminPeer));
        if (_count > kMax) _count = kMax;
    }
    prefs.end();

    // CONFIRMED is a claim about a live key exchange, and nothing here proves it
    // still holds: the remote may have been reconfigured, or had its admin_key
    // list cleared, while this device was off. Demoting on load means the first
    // thing the terminal does after a reboot is prove itself again, which costs
    // one packet and is the difference between a gate and a memory of a gate.
    int demoted = 0;
    for (int i = 0; i < _count; i++) {
        if (_peers[i].state == ADMIN_PEER_CONFIRMED) {
            _peers[i].state = ADMIN_PEER_PENDING;
            demoted++;
        }
    }
    Serial.printf("[admin-peers] loaded %d entries (%d re-verify on use)\n",
                  _count, demoted);
}

const AdminPeer *AdminPeers::find(uint32_t nodeId) const {
    if (nodeId == 0 || nodeId == 0xFFFFFFFF) return nullptr;
    for (int i = 0; i < _count; i++) {
        if (_peers[i].nodeId == nodeId) return &_peers[i];
    }
    return nullptr;
}

AdminPeer *AdminPeers::_findMutable(uint32_t nodeId) {
    return const_cast<AdminPeer *>(find(nodeId));
}

const AdminPeer *AdminPeers::at(int i) const {
    if (i < 0 || i >= _count) return nullptr;
    return &_peers[i];
}

bool AdminPeers::mayOpenTerminal(uint32_t nodeId) const {
    const AdminPeer *p = find(nodeId);
    return p && p->state != ADMIN_PEER_DENIED;
}

bool AdminPeers::isConfirmed(uint32_t nodeId) const {
    const AdminPeer *p = find(nodeId);
    return p && p->state == ADMIN_PEER_CONFIRMED;
}

bool AdminPeers::add(uint32_t nodeId) {
    if (nodeId == 0 || nodeId == 0xFFFFFFFF) return false;
    if (find(nodeId)) return false;
    if (_count >= kMax) {
        Serial.printf("[admin-peers] list full (%d), refusing !%08lx\n",
                      kMax, (unsigned long)nodeId);
        return false;
    }
    _peers[_count] = AdminPeer{ nodeId, ADMIN_PEER_PENDING, ADMIN_VIA_NONE, 0, 0 };
    _count++;
    _persist();
    Serial.printf("[admin-peers] added !%08lx pending (%d total)\n",
                  (unsigned long)nodeId, _count);
    return true;
}

bool AdminPeers::remove(uint32_t nodeId) {
    for (int i = 0; i < _count; i++) {
        if (_peers[i].nodeId != nodeId) continue;
        for (int j = i + 1; j < _count; j++) _peers[j - 1] = _peers[j];
        _count--;
        _peers[_count] = AdminPeer{};
        _persist();
        Serial.printf("[admin-peers] removed !%08lx (%d total)\n",
                      (unsigned long)nodeId, _count);
        return true;
    }
    return false;
}

void AdminPeers::confirm(uint32_t nodeId, AdminPeerTransport via, uint32_t epochNow) {
    AdminPeer *p = _findMutable(nodeId);
    if (!p) return;
    p->state = ADMIN_PEER_CONFIRMED;
    p->lastTransport = (uint8_t)via;
    p->lastVerified = epochNow;
    _persist();
    Serial.printf("[admin-peers] !%08lx confirmed via %s\n",
                  (unsigned long)nodeId, transportName((uint8_t)via));
}

void AdminPeers::deny(uint32_t nodeId) {
    AdminPeer *p = _findMutable(nodeId);
    if (!p) return;
    p->state = ADMIN_PEER_DENIED;
    _persist();
    Serial.printf("[admin-peers] !%08lx DENIED by the remote\n",
                  (unsigned long)nodeId);
}

void AdminPeers::resetState(uint32_t nodeId) {
    AdminPeer *p = _findMutable(nodeId);
    if (!p) return;
    p->state = ADMIN_PEER_PENDING;
    _persist();
}

void AdminPeers::replace(const AdminPeer *peers, int n) {
    if (n < 0) n = 0;
    if (n > kMax) n = kMax;
    _count = 0;
    for (int i = 0; i < n; i++) {
        if (!peers[i].nodeId || peers[i].nodeId == 0xFFFFFFFF) continue;
        _peers[_count] = peers[i];
        // An import carries a list, not a proof. Whatever state the YAML
        // claimed, the peer proves itself here the same way any other does.
        _peers[_count].state = ADMIN_PEER_PENDING;
        _count++;
    }
    for (int i = _count; i < kMax; i++) _peers[i] = AdminPeer{};
    _persist();
    Serial.printf("[admin-peers] replaced with %d entries\n", _count);
}

void AdminPeers::clear() {
    _count = 0;
    for (int i = 0; i < kMax; i++) _peers[i] = AdminPeer{};
    _persist();
}

void AdminPeers::_persist() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    if (_count > 0) {
        prefs.putBytes(kKey, _peers, sizeof(AdminPeer) * (size_t)_count);
    } else {
        prefs.remove(kKey);
    }
    prefs.end();
}

const char *AdminPeers::stateName(uint8_t state) {
    switch (state) {
        case ADMIN_PEER_PENDING:   return "pending";
        case ADMIN_PEER_CONFIRMED: return "confirmed";
        case ADMIN_PEER_DENIED:    return "denied";
        default:                   return "unknown";
    }
}

const char *AdminPeers::transportName(uint8_t transport) {
    switch (transport) {
        case ADMIN_VIA_RF:   return "rf";
        case ADMIN_VIA_MQTT: return "mqtt";
        default:             return "never";
    }
}
