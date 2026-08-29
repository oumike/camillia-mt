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
    uint8_t  hops;            // hop_start - hop_limit of last packet; only meaningful when hasHops
    // Older firmware and MQTT-injected packets arrive with hop_start == 0, which
    // makes the subtraction above yield 0 — indistinguishable from a genuine
    // direct neighbor. Mirrors Meshtastic's has_hops_away: without it, "0 hops"
    // means either "next to us" or "no idea", and Discovery cannot tell them
    // apart. RAM only, like everything below it.
    bool     hasHops;
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

#if FEATURE_DISCOVERY
// ── Neighbor reports (topology, RAM only) ────────────────────────────────────
// One node's advertised list of *its* direct neighbors, from a NEIGHBORINFO_APP
// broadcast. This is the only source we have for links that do not touch us,
// and so for nodes we have heard *about* but never heard *from*.
//
// Held in a small side table rather than as fields on NodeEntry: only a handful
// of nodes ever broadcast NeighborInfo, while NodeEntry is allocated MAX_NODES
// (250) times over. Hanging a 10-entry neighbor list off every node record
// would cost ~14 KB of DRAM to hold data for a dozen of them, on boards where
// the heap is already tight enough that web config has an AP-mode lite page.
//
// Never persisted: the graph is stale the moment the mesh moves, and node
// records are already the NVS pressure point (see releaseNvsForSettings()).
struct NeighborReport {
    uint32_t nodeId;                     // reporter; 0 = empty slot
    uint32_t updatedMs;                  // millis() of the last report
    uint32_t intervalS;                  // reporter's advertised broadcast interval
    uint8_t  count;
    uint32_t ids[MESH_NEIGHBOR_MAX];
    // Quarter-dB, as Meshtastic puts SNR on the wire. Whole dB would lose a
    // quarter of the resolution the packet actually carried.
    int8_t   snrQ4[MESH_NEIGHBOR_MAX];
};

// Reporters tracked at once. Well past the number of nodes within earshot on a
// normal mesh, and the table evicts the stalest entry rather than dropping new
// reports, so an unusually busy one degrades instead of going blind.
static constexpr int MAX_NEIGHBOR_REPORTS = 24;
#endif  // FEATURE_DISCOVERY

class NodeDB {
public:
    void init();          // zeros RAM, then loads persisted nodes from NVS
    void clearPersisted(); // wipe "nodes" NVS namespace and clear runtime node cache
    // Drop every non-favorited node from RAM and NVS, keeping favorites intact
    // with their names, keys, positions and favorite flag. Returns how many were
    // removed. The counterpart to clearPersisted() for the common case: flush
    // months of stale mesh nodes without losing the handful that were pinned.
    int  clearNonFavorites();
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

    // Forget one node: its RAM entry and its NVS blob. Returns false when the
    // table did not have it.
    //
    // Not an ignore, and deliberately not sticky: nothing here stops the node
    // coming straight back the next time a packet from it arrives, which is
    // what will happen if it is still on the mesh. It removes a record, not a
    // node — for "stop showing me this node", see IgnoreList.
    bool remove(uint32_t nodeId);

    // Sorted with favorites first, then by recency. The sort is memoized: it
    // runs on the first call after a ranking field changes and is skipped by
    // every call until the next change, so walking rank 0..count-1 costs one
    // sort for the whole pass rather than one per element.
    //
    // The table is still reordered under you, just at a predictable point — a
    // ranked walk that mutates ranking fields as it goes must call
    // markRankingDirty() and restart, or use at() instead (see below).
    NodeEntry *getByRank(int rank);

    // Force the next getByRank() to re-sort. Only needed when a ranking field
    // (favorite, hasName, lastHeardMs, nodeId) is written straight through a
    // pointer from find()/at()/getByRank() rather than via the update methods
    // below, which already mark themselves.
    void markRankingDirty() { _sortDirty = true; }

    // Direct slot access in storage order — no sort. Use for bulk scans that
    // may mutate entries: getByRank() would reorder the table mid-iteration
    // (favorites sort first), silently skipping nodes.
    NodeEntry *at(int idx);
    int        count() const { return _count; }

    void updateFromPacket(const MeshPacket &pkt);

#if FEATURE_DISCOVERY
    // Record a NEIGHBORINFO_APP report. reporterId is the packet sender; the
    // payload's own node_id wins when it is set, matching how Meshtastic
    // attributes a report that reached it through a relay.
    void updateNeighbors(uint32_t reporterId, const NeighborInfoPayload &n);

    // Iteration over live reports. Expired ones are skipped, so the index space
    // is sparse: check for null rather than assuming a contiguous run.
    const NeighborReport *neighborReportAt(int idx) const;
    // Live reports right now. O(MAX_NEIGHBOR_REPORTS); not a cached counter.
    int neighborReportCount() const;

    // Forget every stored report. Returns how many were dropped. The graph
    // rebuilds itself from the next NeighborInfo broadcast each node makes, so
    // this costs nothing permanent — it just stops a stale picture of the mesh
    // from lingering for the hours a broadcast interval can run to.
    int clearNeighbors();
#endif

    void updateUser(uint32_t nodeId, const UserInfo &u);
    void updatePosition(uint32_t nodeId, const PositionInfo &p);
    void updateTelemetry(uint32_t nodeId, const TelemetryInfo &t);
    bool setFavorite(uint32_t nodeId, bool favorite);

private:
    NodeEntry _nodes[MAX_NODES];
    int       _count = 0;
    // Set by anything that changes ranking order; cleared by _sort(). Starts
    // true so the first getByRank() after boot sorts the loaded table.
    bool      _sortDirty = true;
#if FEATURE_DISCOVERY
    NeighborReport _neighbors[MAX_NEIGHBOR_REPORTS] = {};
    // A report is dropped at 2x the reporter's own advertised interval, the
    // rule Meshtastic's cleanUpNeighbors() uses: one missed broadcast is a lost
    // packet, two is a node that is gone.
    static bool _neighborExpired(const NeighborReport &r, uint32_t nowMs);
#endif
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
