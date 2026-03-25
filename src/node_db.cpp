#include "node_db.h"

NodeDB Nodes;

void NodeDB::init() {
    memset(_nodes, 0, sizeof(_nodes));
    _count = 0;
}

NodeEntry *NodeDB::find(uint32_t nodeId) {
    for (int i = 0; i < _count; i++)
        if (_nodes[i].nodeId == nodeId) return &_nodes[i];
    return nullptr;
}

NodeEntry *NodeDB::upsert(uint32_t nodeId) {
    NodeEntry *e = find(nodeId);
    if (e) return e;
    if (_count >= MAX_NODES) {
        // Evict oldest
        _sort();
        e = &_nodes[_count - 1];
    } else {
        e = &_nodes[_count++];
    }
    memset(e, 0, sizeof(*e));
    e->nodeId = nodeId;
    snprintf(e->shortName, sizeof(e->shortName), "%04X", nodeId & 0xFFFF);
    snprintf(e->longName,  sizeof(e->longName),  "!%08x", nodeId);
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
    // Empty slots to end
    if (!na->nodeId) return  1;
    if (!nb->nodeId) return -1;
    // Identified nodes (real NODEINFO name) before unidentified
    if (na->hasName && !nb->hasName) return -1;
    if (!na->hasName && nb->hasName) return  1;
    // Within each group: most recently heard first
    if (na->lastHeardMs > nb->lastHeardMs) return -1;
    if (na->lastHeardMs < nb->lastHeardMs) return  1;
    return 0;
}

void NodeDB::_sort() {
    qsort(_nodes, _count, sizeof(NodeEntry), cmpNodes);
}

void NodeDB::updateFromPacket(const MeshPacket &pkt) {
    NodeEntry *e = upsert(pkt.hdr.from);
    e->lastHeardMs = pkt.rxMs;
    e->snr         = pkt.snr;
    e->chanIdx     = pkt.chanIdx;
    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    uint8_t hopStart = (pkt.hdr.flags >> 5) & 0x07;
    e->hops = (hopStart > hopLimit) ? (hopStart - hopLimit) : 0;
}

void NodeDB::updateUser(uint32_t nodeId, const UserInfo &u) {
    NodeEntry *e = upsert(nodeId);
    if (u.longName[0])  { strncpy(e->longName,  u.longName,  sizeof(e->longName)  - 1); e->hasName = true; }
    if (u.shortName[0]) { strncpy(e->shortName, u.shortName, sizeof(e->shortName) - 1); e->hasName = true; }
}

void NodeDB::updatePosition(uint32_t nodeId, const PositionInfo &p) {
    NodeEntry *e = upsert(nodeId);
    e->latI = p.latI; e->lonI = p.lonI; e->alt = p.alt;
    e->hasPosition = (p.latI != 0 || p.lonI != 0);
}

void NodeDB::updateTelemetry(uint32_t nodeId, const TelemetryInfo &t) {
    if (!t.valid) return;
    NodeEntry *e = upsert(nodeId);
    e->battPct     = t.battPct;
    e->voltage     = t.voltage;
    e->hasTelemetry = true;
}
