#pragma once
// AdminMessage wire format: encode, decode, and the single-field splice.
//
// Deliberately free of Arduino headers. This is the one module in the firmware
// whose bugs corrupt *another operator's* node rather than ours -- a malformed
// set_config blanks their radio settings from across the mesh -- so it is kept
// compilable on a host, where tests/test_admin_proto.cpp exercises it.
//
// Built on the same hand-rolled protobuf style as mesh_proto.cpp rather than
// nanopb: the messages here are a handful of fields, and the splice below has
// to work on bytes we have no descriptor for, which a generated decoder cannot
// do by construction.
#include <stdint.h>
#include <stddef.h>

namespace AdminProto {

// ── AdminMessage field numbers ───────────────────────────────────────────────
// From meshtastic/protobufs admin.proto. Only the ones this client sends or
// reads are listed; the rest are deliberately absent so an unhandled value
// stays visibly unhandled rather than half-supported.
enum Field : uint32_t {
    GET_CHANNEL_REQUEST          = 1,
    GET_CHANNEL_RESPONSE         = 2,
    GET_OWNER_REQUEST            = 3,
    GET_OWNER_RESPONSE           = 4,
    GET_CONFIG_REQUEST           = 5,
    GET_CONFIG_RESPONSE          = 6,
    GET_MODULE_CONFIG_REQUEST    = 7,
    GET_MODULE_CONFIG_RESPONSE   = 8,
    GET_DEVICE_METADATA_REQUEST  = 12,
    GET_DEVICE_METADATA_RESPONSE = 13,
    SET_OWNER                    = 32,
    SET_CHANNEL                  = 33,
    SET_CONFIG                   = 34,
    SET_MODULE_CONFIG            = 35,
    SET_FAVORITE_NODE            = 39,
    REMOVE_FAVORITE_NODE         = 40,
    SET_FIXED_POSITION           = 41,
    REMOVE_FIXED_POSITION        = 42,
    SET_TIME_ONLY                = 43,
    SET_IGNORED_NODE             = 47,
    REMOVE_IGNORED_NODE          = 48,
    BEGIN_EDIT_SETTINGS          = 64,
    COMMIT_EDIT_SETTINGS         = 65,
    FACTORY_RESET_DEVICE         = 94,
    REBOOT_SECONDS               = 97,
    SHUTDOWN_SECONDS             = 98,
    FACTORY_RESET_CONFIG         = 99,
    NODEDB_RESET                 = 100,
    SESSION_PASSKEY              = 101,
};

// ── Config oneof blocks ──────────────────────────────────────────────────────
// Field numbers *inside* Config, which are also the values get_config_request
// takes. set_config replaces the whole sub-message, which is why every write is
// a read-modify-write -- see spliceVarint() below.
enum ConfigBlock : uint32_t {
    CONFIG_DEVICE    = 1,
    CONFIG_POSITION  = 2,
    CONFIG_POWER     = 3,
    CONFIG_NETWORK   = 4,
    CONFIG_DISPLAY   = 5,
    CONFIG_LORA      = 6,
    CONFIG_BLUETOOTH = 7,
    CONFIG_SECURITY  = 8,
};

// The session credential a state-changing message must echo. Eight bytes, minted
// by the target and carried on every read response; see admin_client.
constexpr size_t kSessionPasskeyMax = 8;

// ── Wire types ───────────────────────────────────────────────────────────────
constexpr int WT_VARINT = 0;
constexpr int WT_64BIT  = 1;
constexpr int WT_LEN    = 2;
constexpr int WT_32BIT  = 5;

// ── Primitives ───────────────────────────────────────────────────────────────
// Every writer takes the buffer, its capacity and the current offset, and
// returns the new offset -- or 0 for "would not fit". Returning 0 rather than
// truncating matters here: a truncated admin message is a valid-looking
// protobuf that means something else.
size_t writeVarint(uint8_t *buf, size_t cap, size_t off, uint64_t v);
size_t writeTag(uint8_t *buf, size_t cap, size_t off, uint32_t field, int wtype);
size_t writeVarintField(uint8_t *buf, size_t cap, size_t off, uint32_t field, uint64_t v);
size_t writeBytesField(uint8_t *buf, size_t cap, size_t off, uint32_t field,
                       const uint8_t *data, size_t len);
size_t writeStringField(uint8_t *buf, size_t cap, size_t off, uint32_t field,
                        const char *s);
// Position.latitude_i / longitude_i are sfixed32, not varints: four bytes
// little-endian, wire type 5. Written as a varint they decode as a different
// number entirely, which on a fixed position means a point somewhere else.
size_t writeSfixed32Field(uint8_t *buf, size_t cap, size_t off, uint32_t field,
                          int32_t v);

// Reads one varint. Returns the offset after it, or 0 if the buffer ends first.
size_t readVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &val);

// Steps over one field's value given its wire type. Returns the offset after
// it, or 0 on a malformed or truncated record.
size_t skipValue(const uint8_t *buf, size_t len, size_t off, int wtype);

// Finds the first occurrence of `field`. On a length-delimited field, valPtr and
// valLen point into `buf`; on a varint, `varint` carries the value. Returns
// false when the field is absent or the message is malformed.
bool findField(const uint8_t *buf, size_t len, uint32_t field,
               int &wtype, const uint8_t *&valPtr, size_t &valLen, uint64_t &varint);

// ── The splice ───────────────────────────────────────────────────────────────
// Rewrites one field inside an encoded message, copying everything else
// byte-for-byte -- *including fields this firmware has no name for*.
//
// This is the whole reason the admin client does not decode-and-re-encode. A
// remote node may be running newer firmware whose Config carries fields we have
// never heard of; a round trip through our own struct would drop them, and
// set_config would then blank them on a stranger's device. Copying the bytes we
// did not come to change is the only way to be sure we change nothing else.
//
// The field is replaced in place when present and appended when absent, so
// field order is preserved wherever it already existed. Returns the number of
// bytes written to dst, or 0 if it would not fit or src is malformed.
size_t spliceVarint(const uint8_t *src, size_t srcLen,
                    uint32_t field, uint64_t newVal,
                    uint8_t *dst, size_t dstCap);
size_t spliceBytes(const uint8_t *src, size_t srcLen,
                   uint32_t field, const uint8_t *val, size_t valLen,
                   uint8_t *dst, size_t dstCap);

// ── AdminMessage helpers ─────────────────────────────────────────────────────
// A read request: one field carrying a value (an enum for get_config_request, or
// `true` for the bool requests like get_owner_request). Reads carry no session
// passkey -- the target exempts them, and a read is what mints the key in the
// first place.
size_t encodeGetRequest(uint8_t *buf, size_t cap, uint32_t field, uint64_t value);

// A state-changing message. `passkey` is echoed back as field 101; omitting it
// (null, or len 0) earns ADMIN_BAD_SESSION_KEY from the target rather than
// silence, which is why the client never sends a write without one.
size_t encodeVarintCommand(uint8_t *buf, size_t cap, uint32_t field, uint64_t value,
                           const uint8_t *passkey, size_t passkeyLen);
size_t encodeBytesCommand(uint8_t *buf, size_t cap, uint32_t field,
                          const uint8_t *val, size_t valLen,
                          const uint8_t *passkey, size_t passkeyLen);

// What came back. `field` is the AdminMessage field that was set; for a
// length-delimited one, payload/payloadLen point into `buf`. `passkey` is the
// session credential when the response carried one -- its presence is also what
// the authorization probe tests, because an unauthorized sender never gets here.
struct Response {
    uint32_t       field;
    const uint8_t *payload;
    size_t         payloadLen;
    uint64_t       varint;
    uint8_t        passkey[kSessionPasskeyMax];
    size_t         passkeyLen;
    bool           hasPasskey;
};

// Returns false on a malformed message or one carrying no recognised field.
bool decodeResponse(const uint8_t *buf, size_t len, Response &out);

// True when the encoded message is a read request. The transport sets
// Data.want_response from this: a read expects an AdminMessage back, while a
// write is answered by a routing ACK only -- asking for a response there would
// be a request the far end is never going to satisfy.
bool isReadRequest(const uint8_t *buf, size_t len);

// Human name for an AdminMessage field, for the transcript. "unknown" for
// anything outside the table above.
const char *fieldName(uint32_t field);

// ModuleConfig oneof blocks, the values get_module_config_request takes.
// Verified against meshtastic/protobufs module_config.proto.
enum ModuleBlock : uint32_t {
    MODULE_MQTT           = 1,
    MODULE_SERIAL         = 2,
    MODULE_EXTNOTIF       = 3,
    MODULE_STOREFORWARD   = 4,
    MODULE_RANGETEST      = 5,
    MODULE_TELEMETRY      = 6,
    MODULE_CANNEDMSG      = 7,
    MODULE_AUDIO          = 8,
    MODULE_REMOTEHW       = 9,
    MODULE_NEIGHBORINFO   = 10,
    MODULE_AMBIENT        = 11,
    MODULE_DETECTION      = 12,
    MODULE_PAXCOUNTER     = 13,
};

const char *moduleBlockName(uint32_t block);
uint32_t    moduleBlockFromName(const char *name);

// Human name for a Config block, and the reverse for the command parser.
// blockFromName() returns 0 for an unrecognised name, which is not a valid
// block number, so callers can test it directly.
const char *configBlockName(uint32_t block);
uint32_t    configBlockFromName(const char *name);

}  // namespace AdminProto
