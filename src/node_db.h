#pragma once
// In-memory + persisted database of known mesh nodes and telemetry/position state.
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
    float    chUtil;
    float    airUtil;
    float    temperatureC;
    float    humidityPct;
    float    pressureHpa;
    uint8_t  hops;            // hop_start - hop_limit of last packet
    uint32_t lastHeardMs;
    bool     hasPosition;
    bool     hasTelemetry;
    bool     hasDeviceTelemetry;
    bool     hasEnvironmentTelemetry;
    bool     hasName;         // true once a real NODEINFO name has been received
    bool     favorite;        // pinned by user; sorted to top in node and DM lists
    int      chanIdx;         // channel last heard on
    uint8_t  chanHash;        // raw on-air channel hash last heard from this node
    bool     hasChanHash;
    uint8_t  pubKey[32];      // Curve25519 public key from their NODEINFO (field 8)
    bool     hasPubKey;
    bool     pkiNoChannel;    // temporarily suppress PKI DM attempts after NO_CHANNEL(6)
    bool     legacyDmNoChannel; // peer returned NO_CHANNEL for channel-encrypted DM
    uint32_t lastSentInfoMs;  // millis() when we last sent our NODEINFO to this node (RAM only)
    uint32_t lastPosMs;       // millis() when we last processed a POSITION packet for this node (RAM only)
    uint32_t lastPersistMs;   // throttles NVS writes for hot update paths
};

class NodeDB {
public:
    void init();          // zeros RAM, then loads persisted nodes from NVS
    void clearPersisted(); // wipe "nodes" NVS namespace and clear runtime node cache
    // Boot-time recovery for a saturated NVS: drops the persisted node cache
    // (RAM copy untouched) when free space has fallen below the reserve settings
    // need. Returns true if it freed space. Ordinary operation never needs this
    // — node persistence self-limits — but a device that filled its partition
    // under an older build cannot save settings until the space comes back.
    bool releaseNvsForSettings();
    void saveAll();        // rewrite all nodes to NVS (after partition erase)

    // Find or create the entry for nodeId. When the table is full this evicts
    // the least-recently-heard non-favorite to make room (see the archive note
    // below). Returns null only in the one case where there is genuinely no
    // room: every slot is favorited, and favorites are never evicted.
    NodeEntry *upsert(uint32_t nodeId);
    NodeEntry *find(uint32_t nodeId);

    // Sorted with favorites first, then by recency. Note this re-sorts on every
    // call, so it is the wrong tool for bulk scans.
    NodeEntry *getByRank(int rank);

    // Direct slot access in storage order — no sort. Use for bulk scans that
    // may mutate entries: getByRank() would reorder the table mid-iteration
    // (favorites sort first), silently skipping nodes.
    NodeEntry *at(int idx);
    int        count() const { return _count; }

    void updateFromPacket(const MeshPacket &pkt);
    void updateUser(uint32_t nodeId, const UserInfo &u);
    void updatePosition(uint32_t nodeId, const PositionInfo &p);
    void updateTelemetry(uint32_t nodeId, const TelemetryInfo &t);
    bool setFavorite(uint32_t nodeId, bool favorite);

private:
    NodeEntry _nodes[MAX_NODES];
    int       _count = 0;
    void      _sort();
    void      _save(uint32_t nodeId);   // write one node blob to NVS
    void      _saveIds();               // rewrite the nodeId index in NVS
};

extern NodeDB Nodes;

// ── Evicted-node archive (FIFO to SD) ────────────────────────────────────────
// The node table is a fixed MAX_NODES ring: once full, upsert() evicts the
// least-recently-heard entry to make room for a newly heard node (favorites and
// named nodes sort ahead of recency, so they are evicted last). Rather than
// losing that node, its full record is copied into a small RAM queue here and
// appended to a CSV on the SD card.
//
// Eviction happens on the packet path, and SD shares the SPI bus with the LoRa
// radio — so eviction only *queues*, and the actual file write is done by
// nodeArchiveFlush() from the main loop. Archiving is best-effort: with no SD
// card present the queue is discarded and counted in nodeArchiveDropped().
// Archiving is opt-in (RhinoConfig::nodeArchiveEnabled) and only possible on a
// board with an SD slot that currently has a card mounted. When it is off or
// unavailable the evicted node is simply dropped — the FIFO itself is unchanged,
// so newly heard and favorited nodes are still what the table keeps.
void     nodeArchiveFlush();      // call from the main loop; no-op when idle
int      nodeArchivePending();    // records queued, not yet written
uint32_t nodeArchiveWritten();    // lifetime records appended to the card
uint32_t nodeArchiveDropped();    // lifetime records lost (no card / write error)
void     nodeArchiveSetEnabled(bool enabled);
bool     nodeArchiveIsEnabled();
bool     nodeArchiveSlotExists();   // compile-time: board has an SD slot at all
bool     nodeArchiveAvailable();    // slot exists AND a card is mounted right now
const char *nodeArchiveFilePath();  // null when the board has no SD slot

// ── Node CSV schema ──────────────────────────────────────────────────────────
// Shared by the SD archive writer and the web CSV export so the two can never
// drift apart. nodeCsvHeader() is the node-field column list with no trailing
// newline; nodeCsvFormatEntry() writes one matching record (also no newline).
// Callers that need extra context columns (the archive prepends archivedEpoch,
// the export prepends source) add them in front of these.
const char *nodeCsvHeader();
void        nodeCsvFormatEntry(const NodeEntry &e, char *out, size_t outLen);
