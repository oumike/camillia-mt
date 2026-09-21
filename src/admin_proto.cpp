#include "admin_proto.h"

#include <string.h>

namespace AdminProto {

// ── Primitives ───────────────────────────────────────────────────────────────

size_t writeVarint(uint8_t *buf, size_t cap, size_t off, uint64_t v) {
    if (!buf) return 0;
    do {
        if (off >= cap) return 0;
        buf[off++] = (uint8_t)((v & 0x7F) | (v > 0x7F ? 0x80 : 0));
        v >>= 7;
    } while (v);
    return off;
}

size_t writeTag(uint8_t *buf, size_t cap, size_t off, uint32_t field, int wtype) {
    return writeVarint(buf, cap, off, ((uint64_t)field << 3) | (uint64_t)wtype);
}

size_t writeVarintField(uint8_t *buf, size_t cap, size_t off, uint32_t field, uint64_t v) {
    off = writeTag(buf, cap, off, field, WT_VARINT);
    if (!off) return 0;
    return writeVarint(buf, cap, off, v);
}

size_t writeBytesField(uint8_t *buf, size_t cap, size_t off, uint32_t field,
                       const uint8_t *data, size_t len) {
    if (len && !data) return 0;
    off = writeTag(buf, cap, off, field, WT_LEN);
    if (!off) return 0;
    off = writeVarint(buf, cap, off, (uint64_t)len);
    if (!off) return 0;
    if (off + len > cap) return 0;
    if (len) memcpy(buf + off, data, len);
    return off + len;
}

size_t writeStringField(uint8_t *buf, size_t cap, size_t off, uint32_t field,
                        const char *s) {
    if (!s) return 0;
    return writeBytesField(buf, cap, off, field, (const uint8_t *)s, strlen(s));
}

size_t writeSfixed32Field(uint8_t *buf, size_t cap, size_t off, uint32_t field,
                          int32_t v) {
    off = writeTag(buf, cap, off, field, WT_32BIT);
    if (!off || off + 4 > cap) return 0;
    const uint32_t u = (uint32_t)v;
    buf[off++] = (uint8_t)(u & 0xFF);
    buf[off++] = (uint8_t)((u >> 8) & 0xFF);
    buf[off++] = (uint8_t)((u >> 16) & 0xFF);
    buf[off++] = (uint8_t)((u >> 24) & 0xFF);
    return off;
}

size_t readVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &val) {
    val = 0;
    int shift = 0;
    while (off < len) {
        const uint8_t b = buf[off++];
        // Ten groups of seven bits is the most a 64-bit value can occupy. A
        // longer run is a malformed message, not a large number, and must not
        // be allowed to shift indefinitely.
        if (shift > 63) return 0;
        val |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) return off;
    }
    return 0;
}

size_t skipValue(const uint8_t *buf, size_t len, size_t off, int wtype) {
    switch (wtype) {
        case WT_VARINT: { uint64_t v; return readVarint(buf, len, off, v); }
        case WT_64BIT:  return (off + 8 <= len) ? off + 8 : 0;
        case WT_32BIT:  return (off + 4 <= len) ? off + 4 : 0;
        case WT_LEN: {
            uint64_t sz = 0;
            const size_t j = readVarint(buf, len, off, sz);
            if (!j || sz > len || j + (size_t)sz > len) return 0;
            return j + (size_t)sz;
        }
        default: return 0;   // groups (3/4) are not used by any message here
    }
}

bool findField(const uint8_t *buf, size_t len, uint32_t field,
               int &wtype, const uint8_t *&valPtr, size_t &valLen, uint64_t &varint) {
    valPtr = nullptr;
    valLen = 0;
    varint = 0;
    wtype = -1;
    if (!buf) return false;

    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        i = readVarint(buf, len, i, tag);
        if (!i) return false;
        const uint32_t f = (uint32_t)(tag >> 3);
        const int wt = (int)(tag & 0x07);

        if (f == field) {
            if (wt == WT_VARINT) {
                if (!readVarint(buf, len, i, varint)) return false;
                wtype = wt;
                return true;
            }
            if (wt == WT_LEN) {
                uint64_t sz = 0;
                const size_t j = readVarint(buf, len, i, sz);
                if (!j || sz > len || j + (size_t)sz > len) return false;
                valPtr = buf + j;
                valLen = (size_t)sz;
                wtype = wt;
                return true;
            }
            // 32/64-bit fields are not something this client reads; treat as
            // absent rather than guessing at an interpretation.
            return false;
        }

        i = skipValue(buf, len, i, wt);
        if (!i) return false;
    }
    return false;
}

// ── The splice ───────────────────────────────────────────────────────────────
// One walk, copying every record whole. The replacement is written in the
// position the old field occupied; if the walk ends without finding it, the
// replacement is appended. `written` guards against a repeated field being
// replaced more than once -- protobuf allows repeats, and turning two into two
// copies of the same new value would be a quiet corruption.
namespace {

size_t spliceImpl(const uint8_t *src, size_t srcLen,
                  uint32_t field, bool asBytes,
                  uint64_t varVal, const uint8_t *bytesVal, size_t bytesLen,
                  uint8_t *dst, size_t dstCap) {
    if (!dst || (srcLen && !src)) return 0;

    auto emit = [&](size_t off) -> size_t {
        return asBytes ? writeBytesField(dst, dstCap, off, field, bytesVal, bytesLen)
                       : writeVarintField(dst, dstCap, off, field, varVal);
    };

    size_t out = 0;
    size_t i = 0;
    bool written = false;

    while (i < srcLen) {
        const size_t recStart = i;
        uint64_t tag = 0;
        i = readVarint(src, srcLen, i, tag);
        if (!i) return 0;
        const uint32_t f = (uint32_t)(tag >> 3);
        const int wt = (int)(tag & 0x07);

        const size_t recEnd = skipValue(src, srcLen, i, wt);
        if (!recEnd) return 0;

        if (f == field && !written) {
            out = emit(out);
            if (!out) return 0;
            written = true;
        } else if (f == field) {
            // A later copy of a field we are replacing: dropped, so the result
            // carries exactly one value for it.
        } else {
            // Verbatim, tag and all. This is the clause that keeps a remote's
            // unknown fields intact.
            const size_t n = recEnd - recStart;
            if (out + n > dstCap) return 0;
            memcpy(dst + out, src + recStart, n);
            out += n;
        }
        i = recEnd;
    }

    if (!written) {
        out = emit(out);
        if (!out) return 0;
    }
    return out;
}

}  // namespace

size_t spliceVarint(const uint8_t *src, size_t srcLen,
                    uint32_t field, uint64_t newVal,
                    uint8_t *dst, size_t dstCap) {
    return spliceImpl(src, srcLen, field, false, newVal, nullptr, 0, dst, dstCap);
}

size_t spliceBytes(const uint8_t *src, size_t srcLen,
                   uint32_t field, const uint8_t *val, size_t valLen,
                   uint8_t *dst, size_t dstCap) {
    return spliceImpl(src, srcLen, field, true, 0, val, valLen, dst, dstCap);
}

// ── AdminMessage helpers ─────────────────────────────────────────────────────

size_t encodeGetRequest(uint8_t *buf, size_t cap, uint32_t field, uint64_t value) {
    return writeVarintField(buf, cap, 0, field, value);
}

namespace {

// The passkey rides as field 101 after the command itself. Order is not
// significant to a protobuf decoder, but keeping the command first makes a
// captured packet read the way the command table does.
size_t appendPasskey(uint8_t *buf, size_t cap, size_t off,
                     const uint8_t *passkey, size_t passkeyLen) {
    if (!passkey || !passkeyLen) return off;
    if (passkeyLen > kSessionPasskeyMax) return 0;
    return writeBytesField(buf, cap, off, SESSION_PASSKEY, passkey, passkeyLen);
}

}  // namespace

size_t encodeVarintCommand(uint8_t *buf, size_t cap, uint32_t field, uint64_t value,
                           const uint8_t *passkey, size_t passkeyLen) {
    size_t off = writeVarintField(buf, cap, 0, field, value);
    if (!off) return 0;
    return appendPasskey(buf, cap, off, passkey, passkeyLen);
}

size_t encodeBytesCommand(uint8_t *buf, size_t cap, uint32_t field,
                          const uint8_t *val, size_t valLen,
                          const uint8_t *passkey, size_t passkeyLen) {
    size_t off = writeBytesField(buf, cap, 0, field, val, valLen);
    if (!off) return 0;
    return appendPasskey(buf, cap, off, passkey, passkeyLen);
}

bool decodeResponse(const uint8_t *buf, size_t len, Response &out) {
    memset(&out, 0, sizeof(out));
    if (!buf || !len) return false;

    bool sawField = false;
    size_t i = 0;
    while (i < len) {
        uint64_t tag = 0;
        i = readVarint(buf, len, i, tag);
        if (!i) return false;
        const uint32_t f = (uint32_t)(tag >> 3);
        const int wt = (int)(tag & 0x07);

        const size_t after = skipValue(buf, len, i, wt);
        if (!after) return false;

        if (f == SESSION_PASSKEY && wt == WT_LEN) {
            uint64_t sz = 0;
            const size_t j = readVarint(buf, len, i, sz);
            if (!j) return false;
            // Longer than the target ever mints means this is not a passkey we
            // understand; recording a truncation would hand a wrong credential
            // to the next write.
            if (sz && sz <= kSessionPasskeyMax) {
                memcpy(out.passkey, buf + j, (size_t)sz);
                out.passkeyLen = (size_t)sz;
                out.hasPasskey = true;
            }
        } else if (!sawField) {
            // The first non-passkey field is the answer. AdminMessage is a
            // oneof, so there is only ever one.
            out.field = f;
            sawField = true;
            if (wt == WT_LEN) {
                uint64_t sz = 0;
                const size_t j = readVarint(buf, len, i, sz);
                if (!j) return false;
                out.payload = buf + j;
                out.payloadLen = (size_t)sz;
            } else if (wt == WT_VARINT) {
                if (!readVarint(buf, len, i, out.varint)) return false;
            }
        }
        i = after;
    }
    return sawField;
}

bool isReadRequest(const uint8_t *buf, size_t len) {
    Response r;
    if (!decodeResponse(buf, len, r)) return false;
    switch (r.field) {
        case GET_CHANNEL_REQUEST:
        case GET_OWNER_REQUEST:
        case GET_CONFIG_REQUEST:
        case GET_MODULE_CONFIG_REQUEST:
        case GET_DEVICE_METADATA_REQUEST:
            return true;
        default:
            return false;
    }
}

const char *fieldName(uint32_t field) {
    switch (field) {
        case GET_CHANNEL_REQUEST:          return "get_channel_request";
        case GET_CHANNEL_RESPONSE:         return "get_channel_response";
        case GET_OWNER_REQUEST:            return "get_owner_request";
        case GET_OWNER_RESPONSE:           return "get_owner_response";
        case GET_CONFIG_REQUEST:           return "get_config_request";
        case GET_CONFIG_RESPONSE:          return "get_config_response";
        case GET_MODULE_CONFIG_REQUEST:    return "get_module_config_request";
        case GET_MODULE_CONFIG_RESPONSE:   return "get_module_config_response";
        case GET_DEVICE_METADATA_REQUEST:  return "get_device_metadata_request";
        case GET_DEVICE_METADATA_RESPONSE: return "get_device_metadata_response";
        case SET_OWNER:                    return "set_owner";
        case SET_CHANNEL:                  return "set_channel";
        case SET_CONFIG:                   return "set_config";
        case SET_MODULE_CONFIG:            return "set_module_config";
        case SET_FAVORITE_NODE:            return "set_favorite_node";
        case REMOVE_FAVORITE_NODE:         return "remove_favorite_node";
        case SET_FIXED_POSITION:           return "set_fixed_position";
        case REMOVE_FIXED_POSITION:        return "remove_fixed_position";
        case SET_TIME_ONLY:                return "set_time_only";
        case SET_IGNORED_NODE:             return "set_ignored_node";
        case REMOVE_IGNORED_NODE:          return "remove_ignored_node";
        case BEGIN_EDIT_SETTINGS:          return "begin_edit_settings";
        case COMMIT_EDIT_SETTINGS:         return "commit_edit_settings";
        case FACTORY_RESET_DEVICE:         return "factory_reset_device";
        case REBOOT_SECONDS:               return "reboot_seconds";
        case SHUTDOWN_SECONDS:             return "shutdown_seconds";
        case FACTORY_RESET_CONFIG:         return "factory_reset_config";
        case NODEDB_RESET:                 return "nodedb_reset";
        case SESSION_PASSKEY:              return "session_passkey";
        default:                           return "unknown";
    }
}

namespace {
struct BlockName { const char *name; uint32_t block; };
const BlockName kModuleBlocks[] = {
    { "mqtt",         MODULE_MQTT         },
    { "serial",       MODULE_SERIAL       },
    { "extnotif",     MODULE_EXTNOTIF     },
    { "storeforward", MODULE_STOREFORWARD },
    { "rangetest",    MODULE_RANGETEST    },
    { "telemetry",    MODULE_TELEMETRY    },
    { "cannedmsg",    MODULE_CANNEDMSG    },
    { "audio",        MODULE_AUDIO        },
    { "remotehw",     MODULE_REMOTEHW     },
    { "neighborinfo", MODULE_NEIGHBORINFO },
    { "ambient",      MODULE_AMBIENT      },
    { "detection",    MODULE_DETECTION    },
    { "paxcounter",   MODULE_PAXCOUNTER   },
};
}  // namespace

const char *moduleBlockName(uint32_t block) {
    for (const auto &b : kModuleBlocks) {
        if (b.block == block) return b.name;
    }
    return "unknown";
}

uint32_t moduleBlockFromName(const char *name) {
    if (!name) return 0;
    for (const auto &b : kModuleBlocks) {
        if (!strcmp(name, b.name)) return b.block;
    }
    return 0;
}

const char *configBlockName(uint32_t block) {
    switch (block) {
        case CONFIG_DEVICE:    return "device";
        case CONFIG_POSITION:  return "position";
        case CONFIG_POWER:     return "power";
        case CONFIG_NETWORK:   return "network";
        case CONFIG_DISPLAY:   return "display";
        case CONFIG_LORA:      return "lora";
        case CONFIG_BLUETOOTH: return "bluetooth";
        case CONFIG_SECURITY:  return "security";
        default:               return "unknown";
    }
}

uint32_t configBlockFromName(const char *name) {
    if (!name) return 0;
    struct { const char *name; uint32_t block; } kBlocks[] = {
        { "device",    CONFIG_DEVICE    },
        { "position",  CONFIG_POSITION  },
        { "power",     CONFIG_POWER     },
        { "network",   CONFIG_NETWORK   },
        { "display",   CONFIG_DISPLAY   },
        { "lora",      CONFIG_LORA      },
        { "bluetooth", CONFIG_BLUETOOTH },
        { "security",  CONFIG_SECURITY  },
    };
    for (const auto &b : kBlocks) {
        if (!strcmp(name, b.name)) return b.block;
    }
    return 0;
}

}  // namespace AdminProto
