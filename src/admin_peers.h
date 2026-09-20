#pragma once
// Nodes this device is allowed to administer, and how far each one is proved.
//
// This is the privilege gate for the remote admin terminal (issue #89). The
// list is user-curated and the state is *earned*, not asserted: CONFIRMED means
// a PKI get_device_metadata_request came back with a session passkey, which an
// unauthorized sender never gets. Anything less hides the terminal entirely
// rather than greying it -- offering a disabled "Admin" row on a stranger's node
// is an invitation to send unauthorized admin packets across the mesh.
//
// Modeled on ignore_list: a blob in the shared "camillia" NVS namespace, small
// enough to rewrite whole on every mutation, with replace() for YAML import.
#include <Arduino.h>

// Where a peer stands. Ordered by how much is known, not by severity.
enum AdminPeerState : uint8_t {
    ADMIN_PEER_PENDING   = 0,   // in the list, never proved
    ADMIN_PEER_CONFIRMED = 1,   // a probe returned a session passkey
    ADMIN_PEER_DENIED    = 2,   // a probe NAKed NOT_AUTHORIZED / ADMIN_PUBLIC_KEY_UNAUTHORIZED
};

// Which transport last proved the peer. Recorded for the peers screen only --
// authorization is a property of the key pair, not the route, so it is never
// consulted when deciding whether the terminal opens.
enum AdminPeerTransport : uint8_t {
    ADMIN_VIA_NONE = 0,
    ADMIN_VIA_RF   = 1,
    ADMIN_VIA_MQTT = 2,
};

struct AdminPeer {
    uint32_t nodeId;
    uint8_t  state;          // AdminPeerState
    uint8_t  lastTransport;  // AdminPeerTransport
    uint16_t _pad;
    uint32_t lastVerified;   // epoch seconds, 0 when never
};

class AdminPeers {
public:
    // Sixteen is well past what anyone administers by hand, and the list is
    // rewritten whole on every change.
    static constexpr int kMax = 16;

    void init();

    const AdminPeer *find(uint32_t nodeId) const;
    int              count() const { return _count; }
    const AdminPeer *at(int i) const;

    // True only for CONFIRMED. The single question every UI gate asks, so the
    // rule lives in one place and cannot drift between the device and the
    // browser.
    bool mayAdminister(uint32_t nodeId) const;

    // Adds at PENDING. Returns false when already present or the list is full.
    bool add(uint32_t nodeId);
    bool remove(uint32_t nodeId);

    // Records what a probe or a later command proved. `confirm` carries the
    // transport for the peers screen; `deny` is what a 33/37 NAK produces.
    void confirm(uint32_t nodeId, AdminPeerTransport via, uint32_t epochNow);
    void deny(uint32_t nodeId);

    // Back to PENDING without forgetting the peer -- what "verify again" does,
    // and what a config change on the remote deserves.
    void resetState(uint32_t nodeId);

    // Bulk replacement for YAML import. Persists once at the end.
    void replace(const AdminPeer *peers, int n);
    void clear();

    static const char *stateName(uint8_t state);
    static const char *transportName(uint8_t transport);

private:
    AdminPeer _peers[kMax] = {};
    int       _count = 0;
    void      _persist() const;
    AdminPeer *_findMutable(uint32_t nodeId);
};

extern AdminPeers AdminPeerList;
