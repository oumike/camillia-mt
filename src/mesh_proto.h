#pragma once
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
};

// Inline definitions so the table lives in mesh_proto.cpp (extern declared below)
extern ChannelKey CHANNEL_KEYS[MAX_CHANNELS];

// ── Meshtastic raw packet header (16 bytes, little-endian) ────
struct __attribute__((packed)) MeshHdr {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t  flags;    // [2:0]=hop_limit [3]=want_ack [4]=via_mqtt [7:5]=hop_start
    uint8_t  channel;  // channel hash
    uint8_t  reserved[2];
};

// ── Meshtastic port numbers ───────────────────────────────────
enum PortNum : uint32_t {
    UNKNOWN_APP      = 0,
    TEXT_MESSAGE_APP = 1,
    POSITION_APP     = 3,
    NODEINFO_APP     = 4,
    ROUTING_APP      = 70,   // ACK/NAK packets (also carries traceroute)
    TELEMETRY_APP    = 67,
    NEIGHBORINFO_APP = 71,
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
    bool     wantResponse;    // Data.want_response: requester wants us to send our NODEINFO back
    bool     decrypted;
    int      chanIdx;         // which channel key was used (-1 = none)
};

// ── Decoded app-layer payloads ────────────────────────────────
struct TextMsg {
    char     text[201];
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
    bool  valid;
};

// ── Protobuf helpers ──────────────────────────────────────────
size_t pbReadVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &val);

// Decode Data message: fills portnum, payload slice, requestId, wantResponse
bool decodeData(const uint8_t *buf, size_t len,
                uint32_t &portnum, const uint8_t *&payPtr, size_t &payLen,
                uint32_t &requestId, bool &wantResponse);

bool decodeUser(const uint8_t *buf, size_t len, UserInfo &out);
bool decodePosition(const uint8_t *buf, size_t len, PositionInfo &out);
bool decodeTelemetry(const uint8_t *buf, size_t len, TelemetryInfo &out);

// ── PSK expansion ─────────────────────────────────────────────
// Expand a 1-byte PSK to the 16-byte Meshtastic DEFAULT_KEY variant.
void    expandPsk(uint8_t psk, uint8_t out[16]);

// Compute the on-air channel hash (XOR of name bytes ^ XOR of expanded key bytes).
uint8_t computeChannelHash(const char *name, const uint8_t *key, uint8_t keyLen);

// ── Curve25519 PKI key pair (generated once, stored in NVS) ──
// Defined in main.cpp; used by mesh_proto.cpp and dm_mgr.cpp.
extern uint8_t myPubKey[32];
extern uint8_t myPrivKey[32];

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

// ── Protobuf encoder ──────────────────────────────────────────
// Encode a TEXT_MESSAGE_APP Data message. Returns encoded length.
size_t encodeTextMessage(const char *text, uint8_t *buf, size_t bufLen);

// Encode a NODEINFO_APP Data message (User proto). Returns encoded length.
// wantResponse=true asks the receiver to reply with their own NODEINFO (use for broadcasts).
size_t encodeNodeInfo(uint32_t nodeId, const char *longName,
                      const char *shortName, const uint8_t *mac6,
                      uint8_t *buf, size_t bufLen, bool wantResponse = true);

// Encode a POSITION_APP Data message. lat/lon are sint32 (degrees * 1e7),
// alt is int32 (meters). Returns encoded length.
size_t encodePosition(int32_t latI, int32_t lonI, int32_t alt,
                      uint8_t *buf, size_t bufLen);

// Encode a ROUTING_APP success ACK Data message.
// requestId = original packet ID; fromNodeId = our nodeId (sets Data.source field).
size_t encodeRouting(uint32_t requestId, uint32_t fromNodeId,
                     uint8_t *buf, size_t bufLen);

// ── Port name helper ──────────────────────────────────────────
const char *portnumName(uint32_t p);
