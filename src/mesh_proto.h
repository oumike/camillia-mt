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
    // Broadcast our position on this channel. Subordinate to RhinoConfig::
    // shareLocation: with the global switch off nothing is transmitted whatever
    // this says. Defaults on for the primary channel and off for the rest, which
    // is exactly what the firmware did before the flag existed.
    bool    shareLocation;
    // Per-channel hop budget, overriding cfg.loraHopLimit for traffic this node
    // originates on this channel. Encoded 0 = unset (use the device default),
    // 1..8 = a budget of (value - 1) hops.
    //
    // The offset exists so that zero means "never configured" — the static table
    // below and every blob written before this field are zero here, and 0 is
    // also a legitimate budget (direct neighbours only). Without the offset the
    // two would be indistinguishable, which is the same trap ChanBlobRecord's
    // extFlags documents.
    uint8_t hopLimitPlus1;
    // Meshtastic 2.8.1 ChannelSettings.use_aead (field 8): encrypt with AES-CCM
    // and a 12-byte tag instead of AES-CTR, so a packet altered in flight fails
    // to decrypt rather than decrypting into something its sender never wrote.
    // Off by default, and every node on the channel has to agree: it changes the
    // channel hash. Ignored on a channel with no key, as upstream clears it
    // there -- read it through channelUsesAead(), not directly.
    bool    useAead;
};

// Encode/decode helpers for the field above, so the +1 lives in one place.
static inline bool    chanHopLimitSet(uint8_t plus1)   { return plus1 != 0; }
static inline uint8_t chanHopLimitGet(uint8_t plus1)   { return (uint8_t)((plus1 - 1) & 0x07); }
static inline uint8_t chanHopLimitMake(uint8_t hops)   { return (uint8_t)((hops & 0x07) + 1); }

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
    ADMIN_APP        = 6,    // Remote administration (AdminMessage). Client only:
                             //   Camillia administers other nodes and does not
                             //   serve the other half -- see issue #89.
    // Named for the CORE_PORTNUMS_ONLY relay list; we do not handle these.
    TEXT_MESSAGE_COMPRESSED_APP = 7,
    WAYPOINT_APP     = 8,
    ALERT_APP        = 11,
    KEY_VERIFICATION_APP = 12,
    STORE_FORWARD_PLUSPLUS_APP = 35,

    STORE_FORWARD_APP = 65,  // Store and Forward module (replayed messages)
    TELEMETRY_APP    = 67,
    NEIGHBORINFO_APP = 71,
    TRACEROUTE_APP   = 70,   // traceroute (not ACK)
    MAP_REPORT_APP   = 73,   // MQTT-only self-description; never sent over LoRa
    MESH_BEACON_APP  = 37,   // Meshtastic 2.7 MeshBeacon: cross-mesh advertisement
    LORA_OTA_APP     = 79,   // Meshtastic 2.8: signed firmware updates carried over
                             //   LoRa as binary ota-common frames. Named so the
                             //   traffic is legible in the log; we neither send
                             //   nor act on it.
};

// ── Decoded incoming packet ───────────────────────────────────
// The parts of a Data message a Meshtastic 2.8.1 signature covers beyond
// portnum, request_id, want_response and the payload, which MeshPacket already
// carries. Kept with the packet so the signature can be checked once, wherever
// it arrived from (RF, MQTT or PKI).
struct DataEnvelope {
    uint32_t replyId;         // Data.reply_id (field 7)
    uint32_t emoji;           // Data.emoji (field 8)
    uint32_t bitfield;        // Data.bitfield (field 9)
    bool     hasBitfield;     // field 9 present; signed separately from its value
    bool     hasSignature;    // Data.xeddsa_signature (field 10) was a full 64 bytes
    uint8_t  signature[64];
};

struct MeshPacket {
    MeshHdr  hdr;
    uint32_t portnum;
    float    rssi;
    float    snr;
    uint32_t rxMs;            // millis() at receipt
    // Meshtastic's Data.payload maximum. Was 220, which is enough for any text
    // message sent directly but not for one arriving through Store and Forward:
    // a replay wraps the original text in a StoreAndForward message, and the
    // ~4-5 bytes of extra framing pushed full-length messages over the old cap
    // — where they were dropped in silence.
    uint8_t  payload[237];    // decrypted inner payload (after Data wrapper)
    size_t   payloadLen;
    uint32_t requestId;       // non-zero for ROUTING_APP ACK/NAK
    uint32_t dataDest;        // Data.dest (field 4), when present
    uint32_t dataSource;      // Data.source (field 5), when present
    bool     hasDataDest;
    bool     hasDataSource;
    bool     wantResponse;    // Data.want_response: requester wants us to send our NODEINFO back
    DataEnvelope env;         // the rest of what a signature covers, see above
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

// Cap on our *own* User.long_name, in bytes. Meshtastic 2.8 truncates long_name
// to 24 bytes before storing or rebroadcasting it, and mesh.options tells
// clients to enforce 24 in their UI — a longer name is silently clipped on
// every 2.8 screen it reaches. Enforced at every path that writes our name:
// onboarding, the web config form, and YAML import.
//
// The receive buffers below stay 40 bytes. mesh.options keeps the decode width
// at 40 so older senders still parse, and peers on older firmware will keep
// sending long names for a long time.
//
// Truncation must land on a UTF-8 boundary, never mid-codepoint — use
// utf8util::copyTruncate(dst, MESH_LONG_NAME_MAX_BYTES + 1, src), which takes a
// buffer size rather than a byte count.
#define MESH_LONG_NAME_MAX_BYTES 24

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

// ── MeshBeacon (port 37) ──────────────────────────────────────
// Meshtastic 2.7's cross-mesh advertisement. A node retunes its radio to
// another preset/region/channel and broadcasts a short message plus an optional
// "offer" naming a channel, preset and region — so nodes on *that* mesh learn a
// different mesh exists. We decode what arrives on our own config; the offer is
// only ever shown to the user, never applied. Meshtastic is equally firm about
// that ("firmware never applies it automatically"), and silently retuning
// someone's radio out from under them would be a hostile thing for a stranger's
// packet to be able to do.
struct MeshBeaconPayload {
    char     message[101];       // MeshBeacon.message, field 1 (<=100 bytes on the wire)
    bool     hasOfferChannel;    // field 2 present
    char     offerChannelName[16];
    uint8_t  offerPsk[32];
    uint8_t  offerPskLen;
    bool     offerUsesAead;      // ChannelSettings.use_aead (field 8), Meshtastic 2.8.1
    uint8_t  offerRegion;        // Meshtastic RegionCode enum; 0 = UNSET
    bool     hasOfferPreset;
    uint8_t  offerPreset;        // Meshtastic ModemPreset enum — NOT camillia's
    bool     valid;
};

// Decode a MESH_BEACON_APP payload. The offer_channel submessage is parsed for
// name and PSK only; the rest of ChannelSettings is not something we act on.
bool decodeMeshBeacon(const uint8_t *buf, size_t len, MeshBeaconPayload &out);

// ── Protobuf helpers ──────────────────────────────────────────
size_t pbReadVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &val);

// Decode Data message: fills portnum, payload slice, requestId, wantResponse
bool decodeData(const uint8_t *buf, size_t len,
                uint32_t &portnum, const uint8_t *&payPtr, size_t &payLen,
                uint32_t &requestId, bool &wantResponse,
                uint32_t *destNode = nullptr, bool *hasDestNode = nullptr,
                uint32_t *sourceNode = nullptr, bool *hasSourceNode = nullptr,
                // reply_id, emoji, bitfield and xeddsa_signature (fields 7-10).
                // The signature is copied only when it is a full 64 bytes: 2.8
                // emits 0 or 64 and treats anything between as malformed, so a
                // short one is not something to half-accept.
                DataEnvelope *env = nullptr);

bool decodeUser(const uint8_t *buf, size_t len, UserInfo &out);
bool decodePosition(const uint8_t *buf, size_t len, PositionInfo &out);
bool decodeTelemetry(const uint8_t *buf, size_t len, TelemetryInfo &out);
bool decodeNeighborInfo(const uint8_t *buf, size_t len, NeighborInfoPayload &out);

// ── PSK expansion ─────────────────────────────────────────────
// Expand a 1-byte PSK to the 16-byte Meshtastic DEFAULT_KEY variant.
void    expandPsk(uint8_t psk, uint8_t out[16]);

// CRC-32 (IEEE 802.3), matching Meshtastic's crc32Buffer(). Used for the 2.8
// node-number derivation, where the value has to agree with what a peer
// computes over the same public key.
uint32_t meshCrc32(const void *buf, size_t len);

// Whether this channel's traffic is decryptable by anyone — no key, or a key
// from the published defaultpsk family. Used to cap outgoing position precision.
bool    channelKeyIsPublic(const ChannelKey &ck);

// Ceiling on position precision for a publicly-decryptable channel: ~700 m at
// the latitude cell, which stays roughly constant worldwide. Meshtastic 2.8
// enforces this as a hard cap no setting can raise (MAX_POSITION_PRECISION_
// PUBLIC_KEY in mesh/PositionPrecision.h) and uses the same 15 for the MQTT
// map-report ceiling. A precision already at or below it is left alone — this
// only ever coarsens.
#define MESH_MAX_POSITION_PRECISION_PUBLIC 15

// Compute the on-air channel hash (XOR of name bytes ^ XOR of expanded key bytes).
uint8_t computeChannelHash(const char *name, const uint8_t *key, uint8_t keyLen);

// Bytes an AEAD channel adds to every packet: the AES-CCM tag
// (MESHTASTIC_AEAD_OVERHEAD upstream).
#define MESH_AEAD_OVERHEAD 12

// Whether this channel encrypts with AES-CCM. useAead on a channel that has no
// key (keyLen 0, or PSK index 0) is ignored: there is nothing to authenticate
// with, and upstream clears the flag in that case.
bool    channelUsesAead(const ChannelKey &ck);

// The hash this channel goes by on the air: computeChannelHash(), XORed with
// 0xAE when the channel uses AEAD (Channels::generateHash() in 2.8.1), so an
// AEAD channel and a plain one with the same name and key never match each
// other's traffic. Everything that sets ChannelKey::hash goes through this.
uint8_t channelKeyHash(const ChannelKey &ck);

// ── Curve25519 PKI key pair (generated once, stored in NVS) ──
// Defined in the active UI entrypoint (main_lvgl.cpp); used by mesh_proto.cpp and dm_mgr.cpp.
extern uint8_t myPubKey[32];
extern uint8_t myPrivKey[32];

// Device role (Config.DeviceConfig.Role) — 0=CLIENT, 2=ROUTER, etc.
// Set from gCfg.deviceRole in setup() after config is loaded.
extern uint8_t myDeviceRole;

// ── Encryption / decryption ───────────────────────────────────
// Try all known channel keys; returns channel index or -1. plainLen is set to
// the length of what landed in plain: len for an AES-CTR channel, len minus
// the tag for an AEAD one.
int  decryptPacket(const MeshHdr &hdr, const uint8_t *cipher,
                   uint8_t *plain, size_t len, size_t &plainLen);

// AES-CTR with a specific key (16 or 32 bytes, or a 1-byte PSK index). For a
// key that is not a channel's -- the chat-server discovery key. Channel
// traffic goes through encryptChannelPayload(), which honours use_aead.
bool encryptPayload(uint32_t packetId, uint32_t fromNode,
                    const uint8_t *key, uint8_t keyLen,
                    const uint8_t *plain, uint8_t *cipher, size_t len);

// Encrypt plain[len] for channel ck: AES-CTR, or AES-CCM with the tag appended
// when channelUsesAead(ck). to is the packet's destination, which AEAD binds
// into the tag. Returns the length written to cipher (len, or len +
// MESH_AEAD_OVERHEAD), or 0 when it would not fit in cipherCap or failed.
size_t encryptChannelPayload(const ChannelKey &ck, uint32_t packetId,
                             uint32_t fromNode, uint32_t toNode,
                             const uint8_t *plain, size_t len,
                             uint8_t *cipher, size_t cipherCap);

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
// Every encoder below writes Data.bitfield (field 9) unconditionally, including
// when the value is zero. Meshtastic 2.8 treats a packet with hop_start == 0 and
// no bitfield as pre-2.3.0 firmware and keeps it out of module processing, the
// phone, MQTT and rebroadcast — so omitting the field when it is zero makes us
// invisible to 2.8 nodes. See pbWriteDataBitfield() in mesh_proto.cpp.
//
// Encode a TEXT_MESSAGE_APP Data message. Returns encoded length.
// bitfield: Data.bitfield value; bit 0 = OK_TO_MQTT.
// replyId: optional Data.reply_id value (message ID being replied to).
// emoji: optional Data.emoji value (non-zero marks a tapback reaction).
size_t encodeTextMessage(const char *text, uint8_t *buf, size_t bufLen,
                         uint32_t bitfield = 0, uint32_t replyId = 0,
                         uint32_t emoji = 0);

// Encode a unicast TEXT_MESSAGE_APP Data message with explicit Data.dest/source.
// Use for DM interoperability with peers that validate decoded destination fields.
// Data { portnum = ADMIN_APP, payload = <AdminMessage>, dest, source } plus
// want_response for a read. The admin payload is built by admin_proto; this only
// wraps it in the Data envelope every portnum shares.
//
// dest/source are set for the same reason a DM sets them: a PKI packet carries
// no channel, so these are what tells the far end who the exchange is between.
size_t encodeAdminData(const uint8_t *admin, size_t adminLen,
                       uint32_t fromNode, uint32_t toNode, bool wantResponse,
                       uint8_t *buf, size_t bufLen);

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

// A NODEINFO_APP Data message describing *another* node, for the node actions'
// Share: its id, names and, when known, public key (pubKey32 may be null). What
// this node does not store about it -- hardware model, role, MAC -- is left out
// rather than filled with our own, which encodeNodeInfo() would do. Never asks
// for a response and never carries OK_TO_MQTT.
size_t encodeSharedNodeInfo(uint32_t nodeId, const char *longName,
                            const char *shortName, const uint8_t *pubKey32,
                            uint8_t *buf, size_t bufLen);

// Coarsen a coordinate pair to `precisionBits` of latitude/longitude, in place.
// No-op at 32 or above. This is Meshtastic's imprecise-location transform: mask
// off the low bits, then add half a cell so the transmitted point sits in the
// middle of the area it could be in rather than at its south-west corner.
//
// Call it once, on the way out. Applying it twice is not idempotent — the
// second pass re-centres an already-centred point and walks it half a cell
// further each time.
void applyPositionPrecision(int32_t &latI, int32_t &lonI, uint8_t precisionBits);

// Encode a POSITION_APP Data message. lat/lon are sfixed32 (degrees * 1e7),
// alt is int32 (meters). Returns encoded length.
// bitfield: optional Data.bitfield value; bit 0 = OK_TO_MQTT.
// precisionBits fills Position.precision_bits so the receiver knows how much of
// the coordinate is real; it does not itself coarsen anything — pass the
// coordinates through applyPositionPrecision() first.
size_t encodePosition(int32_t latI, int32_t lonI, int32_t alt,
                      uint8_t *buf, size_t bufLen, uint32_t bitfield = 0,
                      uint8_t precisionBits = 32);

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
//
// proofKey, when given, is the PKI shared key with ackTo (pkiSharedKey()): the
// Routing message then carries a Routing.ack_proof (ack_proof.h), which a 2.8.1
// sender uses to show the message as received by us and nobody else.
size_t encodeRouting(uint32_t requestId, uint32_t fromNodeId, uint32_t errorReason,
                     uint8_t *buf, size_t bufLen, uint32_t bitfield = 0,
                     const uint8_t *proofKey = nullptr, uint32_t ackTo = 0);

// SHA256(X25519(our private key, peer public key)): the key PKI direct messages
// to and from that peer are encrypted under, and the ack proof key.
bool pkiSharedKey(const uint8_t peerPubKey[32], uint8_t out[32]);

// Encode a TRACEROUTE_APP Data message containing an empty RouteDiscovery
// payload. wantResponse should stay true for request packets.
size_t encodeTracerouteRequest(uint8_t *buf, size_t bufLen, bool wantResponse = true,
                               uint32_t bitfield = 0);

// Encode the TRACEROUTE_APP reply a traceroute's destination owes its sender.
// routePayload is the RouteDiscovery from the request: the hops in it are
// appended by the nodes that relay a traceroute, not by the node it was aimed
// at. request_id is what marks the packet as a response — without it the
// requester reads the reply as another request.
//
// rxSnr is the SNR this node heard the request at, appended to the echoed
// RouteDiscovery as one snr_towards entry. It used to be echoed back completely
// unchanged, which meant a reply from this firmware carried no SNR at all and a
// direct traceroute between two Camillia nodes had nothing to report — the
// requester reads snr_towards, so somebody has to write it. Pass NAN when
// there is no measurement and the field is left off entirely.
size_t encodeTracerouteReply(uint8_t *buf, size_t bufLen,
                             const uint8_t *routePayload, size_t routePayloadLen,
                             uint32_t requestId, uint32_t fromNodeId,
                             float rxSnr, uint32_t bitfield = 0);

// Encode a POSITION_APP Data request with an empty payload and want_response=true.
// Used to ask a specific peer to reply with their current Position.
size_t encodePositionRequest(uint8_t *buf, size_t bufLen, uint32_t bitfield = 0);

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

// ── Map report (mqtt.proto MapReport) ─────────────────────────
// A node's periodic self-description, published straight to <root>/2/map/ so an
// MQTT-fed map can place it without inferring anything from mesh traffic. It is
// the one thing this firmware publishes that never goes on the air: there is no
// LoRa frame behind it, no channel key involved, and no receiver on RF.
//
// So, unlike every other envelope we publish, the inner MeshPacket carries a
// *decoded* Data (portnum MAP_REPORT_APP) rather than ciphertext.
//
// hw_model is not a field here — it is MY_HW_MODEL, the same compile-time
// constant encodeNodeInfo() sends, and nothing at runtime can change it.
struct MapReportInfo {
    const char *longName;
    const char *shortName;
    const char *firmwareVersion;
    uint8_t  role;                // Config.DeviceConfig.Role
    uint8_t  region;              // Config.LoRaConfig.RegionCode (Meshtastic enum)
    uint8_t  modemPreset;         // Config.LoRaConfig.ModemPreset (Meshtastic enum)
    bool     hasDefaultChannel;   // primary channel still carries the default PSK
    // false omits latitude/longitude/altitude/precision entirely, rather than
    // sending a zeroed position that reads as a real fix off West Africa.
    bool     hasPosition;
    int32_t  latI;                // degrees * 1e7, already coarsened by the caller
    int32_t  lonI;
    int32_t  alt;                 // metres
    uint8_t  positionPrecision;   // how many bits of lat/lon are real
    uint32_t onlineLocalNodes;
};

// Encode a ServiceEnvelope carrying one MapReport. channelName/gatewayId are the
// envelope's channel_id/gateway_id, as in encodeServiceEnvelope(). Returns
// encoded length, or 0 on overflow.
size_t encodeMapReportEnvelope(uint32_t fromNode, const MapReportInfo &info,
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

// ── Hop limit ─────────────────────────────────────────────────
// The hop budget this node stamps on packets it originates. Set from
// cfg.loraHopLimit; MESH_HOP_LIMIT is only the compiled default that seeds that
// setting, and is deliberately not read at transmit time — every originating
// path used to use the constant directly, so a configured limit changed the
// info panel and nothing else.
//
// Relayed traffic is not covered by any of this: a relay decrements the budget
// it received and forwards, and rewriting someone else's would corrupt the
// hop_start - hop_limit arithmetic that tells every node how far away a sender
// is.
void meshSetHopLimit(uint8_t hops);      // clamped to the 3 header bits (0-7)
uint8_t meshHopLimit();

// Header flags for a packet this node originates: hop_limit and hop_start both
// set to the current budget — they are equal at origination by definition, and
// that is what lets a receiver derive the hops taken — OR'd with whatever else
// the caller needs (want_ack, via_mqtt).
uint8_t meshOriginHopFlags(uint8_t extraFlags = 0);

// Same, for a path that has its own, shorter budget in mind (the discovery
// sweep). Never exceeds the configured limit: a node told to stay within one hop
// should not have some other subsystem reaching further on its behalf.
uint8_t meshOriginHopFlagsCapped(uint8_t hops, uint8_t extraFlags = 0);

// Origin flags for traffic going out on a specific channel: that channel's own
// hop limit when it has one, otherwise the device default. A channel value is an
// explicit override and is used as given — it may be higher than the device
// default, because "this channel needs more reach" is exactly the case the
// setting exists for.
uint8_t meshOriginHopFlagsForChannel(int chanIdx, uint8_t extraFlags = 0);

// ── Port name helper ──────────────────────────────────────────
const char *portnumName(uint32_t p);
