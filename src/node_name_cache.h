#pragma once
// Names of nodes pushed out of a full node table.
//
// Eviction used to take a node's identity with it. A node that came back --
// usually minutes later, since on a mesh busier than MAX_NODES the table is a
// rolling window of the most recently heard -- was recreated as !xxxxxxxx and
// stayed that way until its next NodeInfo, which Meshtastic broadcasts every
// three hours by default. Once the mesh turns the table over faster than that,
// almost every entry is one that lost its name on the way out and has not
// heard it again yet: a full table of unidentified nodes.
//
// So the name outlives the record. upsert() files the name of the node it
// evicts here and takes it back when that node is heard again. Only the name:
// the evicted key has its own list in KeyPinTable, because restoring a key is a
// trust decision and restoring a name is not.
//
// RAM only, a ring (the name filed longest ago goes first). The caller supplies
// the storage, so the board decides how much it can afford -- PSRAM holds
// thousands of these, a board without it a few dozen.
//
// Kept free of Arduino so it can be tested on the host
// (tests/test_node_name_cache.cpp).
#include <stddef.h>
#include <stdint.h>

struct NodeIdentity {
    uint32_t nodeId;          // 0 = empty slot
    char     shortName[5];
    char     longName[40];
};

class NodeNameCache {
public:
    // Hand over `capacity` slots of storage. Null or zero leaves the cache
    // empty and every call below a no-op, which is the right failure for a
    // board that could not allocate it: eviction behaves as it always did.
    void begin(NodeIdentity *slots, int capacity);

    // File a name. Replaces an earlier entry for the same node in place.
    // Ignored for node 0 and for a node with neither name set.
    void remember(uint32_t nodeId, const char *shortName, const char *longName);
    // The filed name of a node, removed from the cache. False when none.
    bool take(uint32_t nodeId, NodeIdentity &out);
    // Drop one node's name (the user removed the node).
    void forget(uint32_t nodeId);
    void clear();

    int count() const;
    int capacity() const { return _cap; }

private:
    NodeIdentity *_s = nullptr;
    int _cap = 0;
    int _next = 0;
};
