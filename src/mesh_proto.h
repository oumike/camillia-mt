#pragma once
// Meshtastic packet structures, protobuf encode/decode, and crypto helpers.
#include <Arduino.h>
#include "config.h"

// ── Channel key table ─────────────────────────────────────────
// Expanded from ribl_config.yaml channel_url.
// 1-byte PSK N expands to DEFAULT_KEY with last byte = N.
// DEFAULT_KEY = {0xd4,0xf1,...,0x69,0x01}

struct ChannelKey {
    const char *name;         // points to literal at init; redirected to name_buf after import
    uint8_t     key[32];
    uint8_t     keyLen;       // 16 = AES-128, 32 = AES-256
    uint8_t     hash;         // XOR(name_bytes) ^ XOR(expanded_key_bytes)
    char        name_buf[16]; // mutable storage for imported names (zero at static init)
    uint8_t role;             // 0=PRIMARY, 1=SECONDARY, 2=DISABLED
    bool    uplinkEnabled;    // publish packets heard on this channel to MQTT
    bool    downlinkEnabled;  // re-inject MQTT traffic for this channel onto LoRa
    bool    muted;            // suppress visual + audio notifications for this channel
};

// Inline definitions so the table lives in mesh_proto.cpp (extern declared below)
extern ChannelKey CHANNEL_KEYS[MAX_CHANNELS];

// ── Meshtastic raw packet header (16 bytes, little-endian) ────
struct __attribute__((packed)) MeshHdr {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t  flags;    // [2:0]=hop_limit [3]=want_ack [4]=via_mqtt [7:5]=hop_start
    uint8_t  channel;    // channel hash
    uint8_t  next_hop;   // low byte of next-hop node (0 = no preference)
    uint8_t  relay_node; // low byte of node that relayed this packet
};

// ── Meshtastic port numbers ───────────────────────────────────
enum PortNum : uint32_t {
    UNKNOWN_APP      = 0,
    TEXT_MESSAGE_APP = 1,
    POSITION_APP     = 3,
    NODEINFO_APP     = 4,
    ROUTING_APP      = 5,    // ACK/NAK packets (Meshtastic PortNum_ROUTING_APP)
    STORE_FORWARD_APP = 65,  // Store and Forward module (replayed messages)
    TELEMETRY_APP    = 67,
    NEIGHBORINFO_APP = 71,
    TRACEROUTE_APP   = 70,   // traceroute (not ACK)
};

// ── Decoded incoming packet ───────────────────────────────────
struct MeshPacket {
    MeshHdr  hdr;
    uint32_t portnum;
    float    rssi;
    float    snr;
    uint32_t rxMs;            // millis() at receipt
    uint8_t  payload[220];    // decrypted inner payload (after Data wrapper)
    size_t   payloadLen;
    uint32_t requestId;       // non-zero for ROUTING_APP ACK/NAK
    uint32_t dataDest;        // Data.dest (field 4), when present
    uint32_t dataSource;      // Data.source (field 5), when present
    bool     hasDataDest;
    bool     hasDataSource;
    bool     wantResponse;    // Data.want_response: requester wants us to send our NODEINFO back
    bool     decrypted;
    int      chanIdx;         // which channel key was used (-1 = none, -2 = PKI)
    uint8_t  rawCipher[240];  // preserved raw cipher for deferred PKI decrypt in handleRx
    size_t   rawLen;          // 0 if not stored
};

// ── Decoded app-layer payloads ────────────────────────────────
struct TextMsg {
    char     text[MESH_TEXT_MAX_LEN + 1];
    uint32_t replyId;
};

struct UserInfo {
    char    longName[40];
    char    shortName[5];
    uint8_t pubKey[32];   // Curve25519 public key (field 8), zero if absent
    bool    hasPubKey;
};

struct PositionInfo {
    int32_t  latI;   // degrees * 1e7
    int32_t  lonI;
    int32_t  alt;    // meters
};

struct TelemetryInfo {
    float battPct;
    float voltage;
    float chUtil;
    float airUtil;
    float temperatureC;
    float humidityPct;
    float pressureHpa;
    bool  hasDeviceMetrics;
    bool  hasEnvironmentMetrics;
    bool  valid;
};

static constexpr size_t MESH_NEIGHBOR_MAX = 10;

struct NeighborEdgeInfo {
    uint32_t nodeId;
    float    snr;
    uint32_t lastRxTime;
    uint32_t nodeBroadcastIntervalS;
};

struct NeighborInfoPayload {
    uint32_t nodeId;
    uint32_t lastSentById;
    uint32_t nodeBroadcastIntervalS;
    NeighborEdgeInfo neighbors[MESH_NEIGHBOR_MAX];
    uint8_t  neighborCount;
};

// ── Protobuf helpers ──────────────────────────────────────────
size_t pbReadVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &val);

// Decode Data message: fills portnum, payload slice, requestId, wantResponse
bool decodeData(const uint8_t *buf, size_t len,
                uint32_t &portnum, const uint8_t *&payPtr, size_t &payLen,
                uint32_t &requestId, bool &wantResponse,
                uint32_t *destNode = nullptr, bool *hasDestNode = nullptr,
                uint32_t *sourceNode = nullptr, bool *hasSourceNode = nullptr);

bool decodeUser(const uint8_t *buf, size_t len, UserInfo &out);
bool decodePosition(const uint8_t *buf, size_t len, PositionInfo &out);
bool decodeTelemetry(const uint8_t *buf, size_t len, TelemetryInfo &out);
bool decodeNeighborInfo(const uint8_t *buf, size_t len, NeighborInfoPayload &out);

// ── PSK expansion ─────────────────────────────────────────────
// Expand a 1-byte PSK to the 16-byte Meshtastic DEFAULT_KEY variant.
void    expandPsk(uint8_t psk, uint8_t out[16]);

// Compute the on-air channel hash (XOR of name bytes ^ XOR of expanded key bytes).
uint8_t computeChannelHash(const char *name, const uint8_t *key, uint8_t keyLen);

// ── Curve25519 PKI key pair (generated once, stored in NVS) ──
// Defined in the active UI entrypoint (main_lvgl.cpp); used by mesh_proto.cpp and dm_mgr.cpp.
extern uint8_t myPubKey[32];
extern uint8_t myPrivKey[32];

// Device role (Config.DeviceConfig.Role) — 0=CLIENT, 2=ROUTER, etc.
// Set from gCfg.deviceRole in setup() after config is loaded.
extern uint8_t myDeviceRole;

// ── Encryption / decryption ───────────────────────────────────
// Try all known channel keys; returns channel index or -1.
int  decryptPacket(const MeshHdr &hdr, const uint8_t *cipher,
                   uint8_t *plain, size_t len);

// Encrypt with a specific key (16 or 32 bytes).
bool encryptPayload(uint32_t packetId, uint32_t fromNode,
                    const uint8_t *key, uint8_t keyLen,
                    const uint8_t *plain, uint8_t *cipher, size_t len);

// PKI-encrypt plain[plainLen] → out[plainLen + 12].
// Uses Curve25519 ECDH(myPrivKey, recipientPubKey) → SHA256 → AES-CCM.
// Wire format: [ciphertext(N)] [CCM-tag(8)] [extraNonce(4)]
// hdr.channel must be set to 0 by the caller to signal PKI.
bool encryptPki(uint32_t packetId, uint32_t fromNode,
                const uint8_t *recipientPubKey,
                const uint8_t *plain, size_t plainLen,
                uint8_t *out);

// Monotonic per-boot packet ID source to avoid duplicate from:id collisions.
// Returns non-zero IDs suitable for MeshHdr.id and Data.request_id.
uint32_t nextMeshPacketId();

// PKI-decrypt a received packet (hdr.channel == 0).
// cipher: raw payload bytes (ciphertext + tag(8) + extraNonce(4))
// cipherLen must be > 12; plain must be at least cipherLen-12 bytes.
// plainLen is set to cipherLen-12 on success.
bool decryptPki(const MeshHdr &hdr, const uint8_t *cipher, size_t cipherLen,
                const uint8_t *senderPubKey, uint8_t *plain, size_t &plainLen);

// ── Protobuf encoder ──────────────────────────────────────────
// Encode a TEXT_MESSAGE_APP Data message. Returns encoded length.
// bitfield: optional Data.bitfield value; bit 0 = OK_TO_MQTT.
// replyId: optional Data.reply_id value (message ID being replied to).
// emoji: optional Data.emoji value (non-zero marks a tapback reaction).
size_t encodeTextMessage(const char *text, uint8_t *buf, size_t bufLen,
                         uint32_t bitfield = 0, uint32_t replyId = 0,
                         uint32_t emoji = 0);

// Encode a unicast TEXT_MESSAGE_APP Data message with explicit Data.dest/source.
// Use for DM interoperability with peers that validate decoded destination fields.
size_t encodeTextMessageUnicast(const char *text,
                                uint32_t fromNode, uint32_t toNode,
                                uint8_t *buf, size_t bufLen,
                                uint32_t replyId = 0, uint32_t emoji = 0);

// Encode a NODEINFO_APP Data message (User proto). Returns encoded length.
// wantResponse=true asks the receiver to reply with their own NODEINFO (use for broadcasts).
// bitfield: optional Data.bitfield value; bit 0 = OK_TO_MQTT.
size_t encodeNodeInfo(uint32_t nodeId, const char *longName,
                      const char *shortName, const uint8_t *mac6,
                      uint8_t *buf, size_t bufLen,
                      bool wantResponse = true, uint32_t bitfield = 0);

// Encode a POSITION_APP Data message. lat/lon are sfixed32 (degrees * 1e7),
// alt is int32 (meters). Returns encoded length.
// bitfield: optional Data.bitfield value; bit 0 = OK_TO_MQTT.
size_t encodePosition(int32_t latI, int32_t lonI, int32_t alt,
                      uint8_t *buf, size_t bufLen, uint32_t bitfield = 0);

// Encode TELEMETRY_APP Data messages.
// timeEpoch sets Telemetry.time (field 1, Unix seconds); pass 0 to omit it when
// the wall clock is not yet synced.
// DeviceMetrics: battery_level(1), voltage(2), channel_utilization(3),
// air_util_tx(4), uptime_seconds(5).
size_t encodeTelemetryDevice(uint8_t battPct, float voltage,
                             float chUtil, float airUtilTx, uint32_t uptimeS,
                             uint32_t timeEpoch,
                             uint8_t *buf, size_t bufLen,
                             uint32_t bitfield = 0);

size_t encodeTelemetryEnvironment(float temperatureC, float humidityPct, float pressureHpa,
                                  uint32_t timeEpoch,
                                  uint8_t *buf, size_t bufLen,
                                  uint32_t bitfield = 0);

size_t encodeNeighborInfo(uint32_t nodeId,
                          uint32_t nodeBroadcastIntervalS,
                          const NeighborEdgeInfo *neighbors,
                          size_t neighborCount,
                          uint8_t *buf, size_t bufLen,
                          uint32_t bitfield = 0);

// Encode a ROUTING_APP Data message.
// requestId = original packet ID; fromNodeId = our nodeId (sets Data.source field).
// errorReason = Routing.error_reason (0 = ACK success, non-zero = NAK).
size_t encodeRouting(uint32_t requestId, uint32_t fromNodeId, uint32_t errorReason,
                     uint8_t *buf, size_t bufLen);

// Encode a TRACEROUTE_APP Data message containing an empty RouteDiscovery
// payload. wantResponse should stay true for request packets.
size_t encodeTracerouteRequest(uint8_t *buf, size_t bufLen, bool wantResponse = true);

// Encode the TRACEROUTE_APP reply a traceroute's destination owes its sender.
// routePayload is the RouteDiscovery from the request, echoed back unchanged:
// the hops in it are appended by the nodes that relay a traceroute, not by the
// node it was aimed at. request_id is what marks the packet as a response —
// without it the requester reads the reply as another request.
size_t encodeTracerouteReply(uint8_t *buf, size_t bufLen,
                             const uint8_t *routePayload, size_t routePayloadLen,
                             uint32_t requestId, uint32_t fromNodeId);

// Encode a POSITION_APP Data request with an empty payload and want_response=true.
// Used to ask a specific peer to reply with their current Position.
size_t encodePositionRequest(uint8_t *buf, size_t bufLen);

// ── ServiceEnvelope (MQTT bridge) ─────────────────────────────
// Meshtastic MQTT does not carry the packed 16-byte on-air header. It publishes
// a ServiceEnvelope { packet: MeshPacket, channel_id: string, gateway_id: string }
// where the inner MeshPacket is the protobuf form. These helpers convert between
// a decoded MeshPacket (as produced by the radio RX path) and that wire form.
//
// Encrypted mode (`msh/.../2/e/`): the inner MeshPacket carries the ciphertext
// verbatim in its `encrypted` field (no re-encryption), so the caller must have
// preserved pkt.rawCipher/pkt.rawLen.

// Encode a ServiceEnvelope for the given packet. channelName is the human channel
// name (ServiceEnvelope.channel_id); gatewayId is our node id as "!aabbccdd".
// cipher/cipherLen is the raw on-air ciphertext to place in MeshPacket.encrypted.
// rxTime is Unix seconds (0 to omit). Returns encoded length, or 0 on overflow.
size_t encodeServiceEnvelope(const MeshHdr &hdr,
                             const uint8_t *cipher, size_t cipherLen,
                             float rxSnr, int32_t rxRssi, uint32_t rxTime,
                             const char *channelName, const char *gatewayId,
                             uint8_t *out, size_t outLen);

// Decode a ServiceEnvelope received from MQTT. Reconstructs the on-air header
// (with the via_mqtt flag forced on) and copies MeshPacket.encrypted into
// cipher[cipherCap]. channelName (may be nullptr) receives ServiceEnvelope
// .channel_id. Returns false if no encrypted payload was present or on overflow.
bool decodeServiceEnvelope(const uint8_t *buf, size_t len,
                           MeshHdr &hdr, uint8_t *cipher, size_t cipherCap,
                           size_t &cipherLen,
                           char *channelName, size_t channelNameCap);

// ── Port name helper ──────────────────────────────────────────
const char *portnumName(uint32_t p);
