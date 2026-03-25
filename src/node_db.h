#pragma once
#include <Arduino.h>
#include "mesh_proto.h"

struct NodeEntry {
    uint32_t nodeId;          // 0 = empty slot
    char     longName[40];
    char     shortName[5];
    int32_t  latI, lonI;      // degrees * 1e7
    int32_t  alt;             // meters
    float    snr;
    float    battPct;
    float    voltage;
    uint8_t  hops;            // hop_start - hop_limit of last packet
    uint32_t lastHeardMs;
    bool     hasPosition;
    bool     hasTelemetry;
    bool     hasName;       // true once a real NODEINFO name has been received
    int      chanIdx;         // channel last heard on
};

class NodeDB {
public:
    void init();

    // Find or create entry for nodeId. Returns pointer (never null).
    NodeEntry *upsert(uint32_t nodeId);
    NodeEntry *find(uint32_t nodeId);

    // Sorted by lastHeardMs descending (most recent first).
    NodeEntry *getByRank(int rank);
    int        count() const { return _count; }

    void updateFromPacket(const MeshPacket &pkt);
    void updateUser(uint32_t nodeId, const UserInfo &u);
    void updatePosition(uint32_t nodeId, const PositionInfo &p);
    void updateTelemetry(uint32_t nodeId, const TelemetryInfo &t);

private:
    NodeEntry _nodes[MAX_NODES];
    int       _count = 0;
    void      _sort();
};

extern NodeDB Nodes;
