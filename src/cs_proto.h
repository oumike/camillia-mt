// Vendored from camillia-chat-server 9754229; edit there, not here.
#pragma once
// camillia chat server protocol (spec §4). Pure C++, no Arduino — shared
// with camillia-mt, which vendors this file pair.
#include <stddef.h>
#include <stdint.h>

namespace csp {

constexpr uint8_t VERSION = 1;
enum Type : uint8_t { DISCOVER = 1, ANNOUNCE = 2, REQUEST = 3, BATCH = 4 };

constexpr uint8_t  FLAG_LAST       = 1;
constexpr uint8_t  FLAG_MORE       = 2;
constexpr uint8_t  FLAG_TIME_VALID = 4;
constexpr uint8_t  FLAG_FIRST      = 8;   // first packet of a batch: the client resyncs its gap check here
constexpr uint32_t AGE_UNKNOWN     = 0xFFFFFFFF;
// SX1262 max frame 255 - 16-byte Meshtastic header - 8 bytes of Data framing
// for port 256 (portnum, payload tag+len, bitfield) = 231. Not Meshtastic's 233.
constexpr size_t   MAX_PAYLOAD     = 231;
constexpr size_t   MAX_TEXT        = 200;
constexpr int      MAX_ANNOUNCE_CHANNELS    = 10;   // 10 x (4+1+11) + 9 = 169 bytes max
constexpr size_t   ITEM_OVERHEAD   = 17;   // seq, from, packetId, age, textLen
constexpr size_t   BATCH_HEADER    = 12;   // version, type, epoch, flags, time, count

// Discovery channel shared by every server and client.
extern const char    DISCOVERY_CHANNEL_NAME[];   // "camillia-cs"
extern const uint8_t DISCOVERY_KEY[16];

struct AnnounceChannel { uint32_t id; char name[12]; };
struct Announce        { char shortName[5]; uint8_t count; AnnounceChannel ch[MAX_ANNOUNCE_CHANNELS]; };
struct Request         { uint32_t epoch, cursor, anchorFrom, anchorId; };
struct Item            { uint32_t seq, from, packetId, ageSec; uint8_t textLen; char text[MAX_TEXT + 1]; };
struct BatchHeader     { uint32_t epoch; uint8_t flags; uint32_t serverTime; uint8_t count; };

// First 4 bytes of SHA-256(name || key), little-endian. A 1-byte PSK is first
// expanded to Meshtastic's 16-byte default-key form, so "AQ==" and its
// expanded key name the same channel. keyLen 0 hashes the name alone.
uint32_t channelId(const char *name, const uint8_t *key, size_t keyLen);

bool   peekType(const uint8_t *buf, size_t len, Type &out);
size_t itemWireSize(const Item &it);

// Encoders return the encoded length, or 0 if the buffer is too small.
// Decoders return false on a wrong version/type or malformed input.
size_t encodeDiscover(uint8_t *buf, size_t cap);
size_t encodeAnnounce(const Announce &a, uint8_t *buf, size_t cap);
bool   decodeAnnounce(const uint8_t *buf, size_t len, Announce &out);
size_t encodeRequest(const Request &r, uint8_t *buf, size_t cap);
bool   decodeRequest(const uint8_t *buf, size_t len, Request &out);
size_t encodeBatch(const BatchHeader &h, const Item *items, uint8_t n, uint8_t *buf, size_t cap);
bool   decodeBatch(const uint8_t *buf, size_t len, BatchHeader &h, Item *items, uint8_t cap,
                   uint8_t &n);

}  // namespace csp
