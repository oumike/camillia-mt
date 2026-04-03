#include "node_db.h"
#include <Preferences.h>

NodeDB Nodes;

// ── NVS layout ────────────────────────────────────────────────
// Namespace : "nodes"
// Key "ids" : blob of uint32_t[n]  — list of known nodeIds
// Key "n_XXXXXXXX" : NodeBlob for that nodeId (hex, 8 chars → 10-char key)

struct NodeBlob {
    char    longName[40];
    char    shortName[5];
    int32_t latI, lonI, alt;
    float   battPct, voltage;
    uint8_t pubKey[32];
    uint8_t chanIdx;
    // bit 0 = hasPosition, 1 = hasName, 2 = hasPubKey, 3 = hasTelemetry
    uint8_t flags;
};

static void nodeKey(char *buf, uint32_t id) {
    snprintf(buf, 12, "n_%08x", id);
}

// ── Init ──────────────────────────────────────────────────────

void NodeDB::init() {
    memset(_nodes, 0, sizeof(_nodes));
    _count = 0;

    // Load persisted nodes directly into the array (bypasses save to avoid
    // triggering NVS writes during boot).
    Preferences p;
    p.begin("nodes", true);  // read-only
    uint32_t ids[MAX_NODES] = {};
    int n = (int)(p.getBytes("ids", ids, sizeof(ids)) / sizeof(uint32_t));
    for (int i = 0; i < n && _count < MAX_NODES; i++) {
        char key[12]; nodeKey(key, ids[i]);
        NodeBlob b = {};
        if (p.getBytes(key, &b, sizeof(b)) != sizeof(b)) continue;
        NodeEntry *e = &_nodes[_count++];
        memset(e, 0, sizeof(*e));
        e->nodeId = ids[i];
        strncpy(e->longName,  b.longName,  sizeof(e->longName)  - 1);
        strncpy(e->shortName, b.shortName, sizeof(e->shortName) - 1);
        e->latI = b.latI; e->lonI = b.lonI; e->alt = b.alt;
        e->battPct = b.battPct; e->voltage = b.voltage;
        memcpy(e->pubKey, b.pubKey, 32);
        e->chanIdx      = b.chanIdx;
        e->hasPosition  = (b.flags & 1) != 0;
        e->hasName      = (b.flags & 2) != 0;
        e->hasPubKey    = (b.flags & 4) != 0;
        e->hasTelemetry = (b.flags & 8) != 0;
        e->lastHeardMs  = 0;  // unknown after reboot
    }
    p.end();
    Serial.printf("[nodedb] loaded %d node(s) from NVS\n", _count);
}

// ── Persistence helpers ───────────────────────────────────────

void NodeDB::_save(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (!e || !e->nodeId) return;

    NodeBlob b = {};
    strncpy(b.longName,  e->longName,  sizeof(b.longName)  - 1);
    strncpy(b.shortName, e->shortName, sizeof(b.shortName) - 1);
    b.latI = e->latI; b.lonI = e->lonI; b.alt = e->alt;
    b.battPct = e->battPct; b.voltage = e->voltage;
    memcpy(b.pubKey, e->pubKey, 32);
    b.chanIdx = (uint8_t)e->chanIdx;
    b.flags = (e->hasPosition  ? 1 : 0) | (e->hasName      ? 2 : 0)
            | (e->hasPubKey    ? 4 : 0) | (e->hasTelemetry ? 8 : 0);

    char key[12]; nodeKey(key, nodeId);
    Preferences p; p.begin("nodes", false);
    p.putBytes(key, &b, sizeof(b));
    p.end();
}

void NodeDB::_saveIds() {
    uint32_t ids[MAX_NODES];
    int n = 0;
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId) ids[n++] = _nodes[i].nodeId;
    Preferences p; p.begin("nodes", false);
    p.putBytes("ids", ids, n * sizeof(uint32_t));
    p.end();
}

void NodeDB::clearPersisted() {
    Preferences p; p.begin("nodes", false);
    p.clear();
    p.end();
}

// ── Core operations ───────────────────────────────────────────

NodeEntry *NodeDB::find(uint32_t nodeId) {
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId == nodeId) return &_nodes[i];
    return nullptr;
}

NodeEntry *NodeDB::upsert(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (e) return e;

    uint32_t evictedId = 0;
    if (_count >= MAX_NODES) {
        _sort();
        evictedId = _nodes[_count - 1].nodeId;
        // Delete the evicted node's blob from NVS
        char evKey[12]; nodeKey(evKey, evictedId);
        Preferences p; p.begin("nodes", false); p.remove(evKey); p.end();
        e = &_nodes[_count - 1];
    } else {
        e = &_nodes[_count++];
    }
    memset(e, 0, sizeof(*e));
    e->nodeId = nodeId;
    snprintf(e->shortName, sizeof(e->shortName), "%04X", nodeId & 0xFFFF);
    snprintf(e->longName,  sizeof(e->longName),  "!%08x", nodeId);

    _saveIds();   // add new / remove evicted from the index
    _save(nodeId); // write initial blob so load() never finds an orphaned ID
    return e;
}

NodeEntry *NodeDB::getByRank(int rank) {
    _sort();
    if (rank < 0 || rank >= _count) return nullptr;
    return &_nodes[rank];
}

static int cmpNodes(const void *a, const void *b) {
    const NodeEntry *na = (const NodeEntry *)a;
    const NodeEntry *nb = (const NodeEntry *)b;
    if (!na->nodeId) return  1;
    if (!nb->nodeId) return -1;
    if (na->hasName && !nb->hasName) return -1;
    if (!na->hasName && nb->hasName) return  1;
    if (na->lastHeardMs > nb->lastHeardMs) return -1;
    if (na->lastHeardMs < nb->lastHeardMs) return  1;
    return 0;
}

void NodeDB::_sort() {
    qsort(_nodes, _count, sizeof(NodeEntry), cmpNodes);
}

// ── Update methods ────────────────────────────────────────────

void NodeDB::updateFromPacket(const MeshPacket &pkt) {
    NodeEntry *e = upsert(pkt.hdr.from);
    e->lastHeardMs = pkt.rxMs;
    e->snr         = pkt.snr;
    e->chanIdx     = pkt.chanIdx;
    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    uint8_t hopStart = (pkt.hdr.flags >> 5) & 0x07;
    e->hops = (hopStart > hopLimit) ? (hopStart - hopLimit) : 0;
    // Don't save on every packet — only on meaningful data changes below.
}

void NodeDB::updateUser(uint32_t nodeId, const UserInfo &u) {
    NodeEntry *e = upsert(nodeId);
    if (u.longName[0])  { strncpy(e->longName,  u.longName,  sizeof(e->longName)  - 1); e->hasName = true; }
    if (u.shortName[0]) { strncpy(e->shortName, u.shortName, sizeof(e->shortName) - 1); e->hasName = true; }
    if (u.hasPubKey)    { memcpy(e->pubKey, u.pubKey, 32); e->hasPubKey = true; }
    _save(nodeId);
}

void NodeDB::updatePosition(uint32_t nodeId, const PositionInfo &pos) {
    NodeEntry *e = upsert(nodeId);
    e->latI = pos.latI; e->lonI = pos.lonI; e->alt = pos.alt;
    e->hasPosition = (pos.latI != 0 || pos.lonI != 0);
    _save(nodeId);
}

void NodeDB::updateTelemetry(uint32_t nodeId, const TelemetryInfo &t) {
    if (!t.valid) return;
    NodeEntry *e = upsert(nodeId);
    e->battPct      = t.battPct;
    e->voltage      = t.voltage;
    e->hasTelemetry = true;
    _save(nodeId);
}
