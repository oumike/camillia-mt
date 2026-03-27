#include "mesh_proto.h"
#include "mbedtls/aes.h"

// ── PSK expansion ─────────────────────────────────────────────
// Meshtastic DEFAULT_KEY = kDkBase[0..14] + PSK_byte.
// PSK 0x01 → DEFAULT_KEY unchanged (base64 "AQ==").
static const uint8_t kDkBase[15] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69
};

void expandPsk(uint8_t psk, uint8_t out[16]) {
    memcpy(out, kDkBase, 15);
    out[15] = psk;
}

uint8_t computeChannelHash(const char *name, const uint8_t *key, uint8_t keyLen) {
    uint8_t exp[16];
    const uint8_t *k = key;
    uint8_t kl = keyLen;
    if (keyLen == 1) { expandPsk(key[0], exp); k = exp; kl = 16; }
    uint8_t h = 0;
    for (const char *p = name; *p; p++) h ^= (uint8_t)*p;
    for (int i = 0; i < kl; i++) h ^= k[i];
    return h;
}

// ── Channel key table ─────────────────────────────────────────
// 1-byte PSK keys are stored as a single byte and expanded at runtime via expandPsk().
// role: 0=PRIMARY, 1=SECONDARY, 2=DISABLED
ChannelKey CHANNEL_KEYS[MAX_CHANNELS] = {
    //           name          PSK       len  hash  name_buf  role
    { "LongFast",  { 0x01 },   1, 0x08, {}, 0 },  // PRIMARY  (AQ==)
    { "Michigan",  { 0x30 },   1, 0x1D, {}, 1 },  // SECONDARY (MA==)
    { "DevTest",   { 0x01 },   1, 0x63, {}, 1 },  // SECONDARY (AQ==)
    { "Sumat",     {
        0x54,0xc2,0x22,0xfa,0x29,0x9a,0xe1,0x46,
        0x3b,0x76,0x6c,0x28,0xa9,0xe3,0x32,0xb5,
        0x2f,0xaf,0x1c,0x59,0xf9,0x53,0x75,0xad,
        0x51,0xd0,0x38,0x4c,0x7b,0xea,0x16,0xdc }, 32, 0xD9, {}, 1 },  // SECONDARY (AES-256)
    { "WMI",       { 0x30 },   1, 0x60, {}, 1 },  // SECONDARY (MA==)
    { "YOOPER",    { 0x30 },   1, 0x2D, {}, 1 },  // SECONDARY (MA==)
    { "Washtenaw", { 0x30 },   1, 0x77, {}, 1 },  // SECONDARY (MA==)
    { "Muskegon",  { 0x30 },   1, 0x10, {}, 1 },  // SECONDARY (MA==)
    { "ANN",       { 0 },      0, 0xFF, {}, 2 },  // DISABLED  (virtual, local-only)
};

// ── Protobuf helpers ──────────────────────────────────────────
size_t pbReadVarint(const uint8_t *buf, size_t len, size_t off, uint64_t &val) {
    val = 0;
    int shift = 0;
    while (off < len) {
        uint8_t b = buf[off++];
        val |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) return off;
    }
    return 0;
}

static size_t pbSkip(const uint8_t *buf, size_t len, size_t i, int wtype) {
    if (wtype == 0) { uint64_t v; return pbReadVarint(buf, len, i, v); }
    if (wtype == 1) return i + 8;
    if (wtype == 5) return i + 4;
    if (wtype == 2) {
        uint64_t sz; size_t j = pbReadVarint(buf, len, i, sz);
        return j ? j + sz : 0;
    }
    return 0;
}

bool decodeData(const uint8_t *buf, size_t len,
                uint32_t &portnum, const uint8_t *&payPtr, size_t &payLen,
                uint32_t &requestId) {
    portnum = 0; payPtr = nullptr; payLen = 0; requestId = 0;
    size_t i = 0;
    while (i < len) {
        uint64_t tag; i = pbReadVarint(buf, len, i, tag); if (!i) break;
        uint32_t field = tag >> 3, wtype = tag & 7;
        if (wtype == 0) {
            uint64_t v; i = pbReadVarint(buf, len, i, v); if (!i) break;
            if (field == 1) portnum    = (uint32_t)v;
            if (field == 6) requestId  = (uint32_t)v;
        } else if (wtype == 2) {
            uint64_t sz; i = pbReadVarint(buf, len, i, sz); if (!i) break;
            if (field == 2) { payPtr = buf + i; payLen = (size_t)sz; }
            i += sz;
        } else { i = pbSkip(buf, len, i, wtype); if (!i) break; }
    }
    return true;
}

bool decodeUser(const uint8_t *buf, size_t len, UserInfo &out) {
    out.longName[0] = out.shortName[0] = '\0';
    size_t i = 0;
    while (i < len) {
        uint64_t tag; i = pbReadVarint(buf, len, i, tag); if (!i) break;
        uint32_t field = tag >> 3, wtype = tag & 7;
        if (wtype == 2) {
            uint64_t sz; i = pbReadVarint(buf, len, i, sz); if (!i) break;
            if (field == 2 && sz < sizeof(out.longName)) {
                memcpy(out.longName,  buf + i, sz); out.longName[sz]  = '\0';
            } else if (field == 3 && sz < sizeof(out.shortName)) {
                memcpy(out.shortName, buf + i, sz); out.shortName[sz] = '\0';
            }
            i += sz;
        } else { i = pbSkip(buf, len, i, wtype); if (!i) break; }
    }
    return true;
}

bool decodePosition(const uint8_t *buf, size_t len, PositionInfo &out) {
    out.latI = out.lonI = out.alt = 0;
    size_t i = 0;
    while (i < len) {
        uint64_t tag; i = pbReadVarint(buf, len, i, tag); if (!i) break;
        uint32_t field = tag >> 3, wtype = tag & 7;
        if (wtype == 0) {
            uint64_t v; i = pbReadVarint(buf, len, i, v); if (!i) break;
            int32_t sv = (int32_t)v;
            if (field == 1) out.latI = sv;
            else if (field == 2) out.lonI = sv;
            else if (field == 3) out.alt  = sv;
        } else { i = pbSkip(buf, len, i, wtype); if (!i) break; }
    }
    return true;
}

bool decodeTelemetry(const uint8_t *buf, size_t len, TelemetryInfo &out) {
    // Telemetry.device_metrics = field 1 (DeviceMetrics)
    // DeviceMetrics: battery_level=1(uint32), voltage=2(float), channel_utilization=3(float), air_util_tx=4(float)
    out = {0, 0, 0, 0, false};
    size_t i = 0;
    while (i < len) {
        uint64_t tag; i = pbReadVarint(buf, len, i, tag); if (!i) break;
        uint32_t field = tag >> 3, wtype = tag & 7;
        if (wtype == 2 && field == 1) {
            // DeviceMetrics submessage
            uint64_t sz; i = pbReadVarint(buf, len, i, sz); if (!i) break;
            const uint8_t *dm = buf + i; size_t dmLen = sz; i += sz;
            size_t j = 0;
            while (j < dmLen) {
                uint64_t t2; j = pbReadVarint(dm, dmLen, j, t2); if (!j) break;
                uint32_t f2 = t2 >> 3, w2 = t2 & 7;
                if (w2 == 0) {
                    uint64_t v; j = pbReadVarint(dm, dmLen, j, v); if (!j) break;
                    if (f2 == 1) out.battPct = (float)v;
                } else if (w2 == 5) {
                    if (j + 4 <= dmLen) {
                        float fv; memcpy(&fv, dm + j, 4);
                        if (f2 == 2) out.voltage = fv;
                        else if (f2 == 3) out.chUtil  = fv;
                        else if (f2 == 4) out.airUtil = fv;
                    }
                    j += 4;
                } else { j = pbSkip(dm, dmLen, j, w2); if (!j) break; }
            }
            out.valid = true;
        } else { i = pbSkip(buf, len, i, wtype); if (!i) break; }
    }
    return true;
}

// ── AES-CTR core ─────────────────────────────────────────────
static bool aesCtr(const uint8_t *key, uint8_t keyLen,
                   uint32_t packetId, uint32_t fromNode,
                   const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t nonce[16] = {0};
    memcpy(nonce,     &packetId, 4);
    memcpy(nonce + 8, &fromNode, 4);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int bits = (keyLen == 32) ? 256 : 128;
    if (mbedtls_aes_setkey_enc(&ctx, key, bits) != 0) {
        mbedtls_aes_free(&ctx); return false;
    }
    size_t nc_off = 0; uint8_t stream[16] = {0};
    bool ok = (mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, nonce, stream, in, out) == 0);
    mbedtls_aes_free(&ctx);
    return ok;
}

// Returns true if plain looks like a valid Meshtastic Data protobuf.
static bool looksLikeData(const uint8_t *plain, size_t len) {
    if (len < 2) return false;
    // Expect field 1 or 2 as first tag; wire types 0 (varint) or 2 (len-delim)
    uint8_t tag = plain[0];
    if (tag == 0 || tag == 0xFF) return false;
    int wtype = tag & 0x07;
    if (wtype > 5) return false;
    // Try to decode portnum (field 1, varint) to verify it's a known port
    uint32_t portnum = 0; const uint8_t *payPtr = nullptr; size_t payLen = 0;
    uint32_t reqId = 0;
    decodeData(plain, len, portnum, payPtr, payLen, reqId);
    // Accept known ports or any non-zero port up to 1024
    return portnum > 0 && portnum <= 1024;
}

int decryptPacket(const MeshHdr &hdr, const uint8_t *cipher,
                  uint8_t *plain, size_t len) {
    auto tryDecrypt = [&](int i) -> bool {
        uint8_t exp[16];
        const uint8_t *keyPtr = CHANNEL_KEYS[i].key;
        uint8_t keyLen = CHANNEL_KEYS[i].keyLen;
        if (keyLen == 1) { expandPsk(CHANNEL_KEYS[i].key[0], exp); keyPtr = exp; keyLen = 16; }
        if (keyLen == 0) {
            memcpy(plain, cipher, len);
        } else {
            if (!aesCtr(keyPtr, keyLen, hdr.id, hdr.from, cipher, plain, len)) return false;
        }
        return looksLikeData(plain, len);
    };

    // Pass 1: try the channel whose hash matches hdr.channel
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (CHANNEL_KEYS[i].hash != hdr.channel) continue;
        if (tryDecrypt(i)) return i;
    }
    // Pass 2: fall back — try all keys (handles unknown/unregistered channels)
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (CHANNEL_KEYS[i].hash == hdr.channel) continue; // already tried
        if (tryDecrypt(i)) return i;
    }
    return -1;
}

bool encryptPayload(uint32_t packetId, uint32_t fromNode,
                    const uint8_t *key, uint8_t keyLen,
                    const uint8_t *plain, uint8_t *cipher, size_t len) {
    if (keyLen == 0) { memcpy(cipher, plain, len); return true; }
    uint8_t exp[16];
    if (keyLen == 1) { expandPsk(key[0], exp); key = exp; keyLen = 16; }
    return aesCtr(key, keyLen, packetId, fromNode, plain, cipher, len);
}

// ── Protobuf encoder ──────────────────────────────────────────
static size_t pbWriteVarint(uint8_t *buf, uint64_t val) {
    size_t n = 0;
    do {
        buf[n] = (val & 0x7F) | (val > 0x7F ? 0x80 : 0);
        val >>= 7; n++;
    } while (val);
    return n;
}

size_t encodeTextMessage(const char *text, uint8_t *buf, size_t bufLen) {
    size_t n = 0;
    size_t textLen = strlen(text);
    // field 1 (portnum = TEXT_MESSAGE_APP = 1), varint
    n += pbWriteVarint(buf + n, (1 << 3) | 0);
    n += pbWriteVarint(buf + n, TEXT_MESSAGE_APP);
    // field 2 (payload), length-delimited
    n += pbWriteVarint(buf + n, (2 << 3) | 2);
    n += pbWriteVarint(buf + n, textLen);
    if (n + textLen > bufLen) return 0;
    memcpy(buf + n, text, textLen);
    n += textLen;
    return n;
}

size_t encodeNodeInfo(uint32_t nodeId, const char *longName,
                      const char *shortName, const uint8_t *mac6,
                      uint8_t *buf, size_t bufLen) {
    // Build inner User message
    uint8_t user[128]; size_t u = 0;

    char idStr[12]; snprintf(idStr, sizeof(idStr), "!%08x", nodeId);
    size_t idLen = strlen(idStr);
    u += pbWriteVarint(user + u, (1 << 3) | 2);
    u += pbWriteVarint(user + u, idLen);
    memcpy(user + u, idStr, idLen); u += idLen;

    size_t lnLen = strlen(longName);
    u += pbWriteVarint(user + u, (2 << 3) | 2);
    u += pbWriteVarint(user + u, lnLen);
    memcpy(user + u, longName, lnLen); u += lnLen;

    size_t snLen = strlen(shortName);
    u += pbWriteVarint(user + u, (3 << 3) | 2);
    u += pbWriteVarint(user + u, snLen);
    memcpy(user + u, shortName, snLen); u += snLen;

    u += pbWriteVarint(user + u, (4 << 3) | 2);
    u += pbWriteVarint(user + u, 6);
    memcpy(user + u, mac6, 6); u += 6;

    u += pbWriteVarint(user + u, (6 << 3) | 0);
    u += pbWriteVarint(user + u, 84); // HardwareModel::T_DECK

    // Wrap in Data message
    size_t n = 0;
    n += pbWriteVarint(buf + n, (1 << 3) | 0);
    n += pbWriteVarint(buf + n, NODEINFO_APP);
    n += pbWriteVarint(buf + n, (2 << 3) | 2);
    n += pbWriteVarint(buf + n, u);
    if (n + u > bufLen) return 0;
    memcpy(buf + n, user, u); n += u;
    return n;
}

size_t encodePosition(int32_t latI, int32_t lonI, int32_t alt,
                      uint8_t *buf, size_t bufLen) {
    uint8_t pos[32]; size_t p = 0;

    // lat/lon are sint32 in Meshtastic proto — zigzag encode
    auto zz = [](int32_t v) -> uint32_t {
        return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
    };
    p += pbWriteVarint(pos + p, (1 << 3) | 0); p += pbWriteVarint(pos + p, zz(latI));
    p += pbWriteVarint(pos + p, (2 << 3) | 0); p += pbWriteVarint(pos + p, zz(lonI));
    // altitude is int32 — plain varint (two's complement for negatives)
    p += pbWriteVarint(pos + p, (3 << 3) | 0); p += pbWriteVarint(pos + p, (uint32_t)alt);

    // Wrap in Data message
    size_t n = 0;
    n += pbWriteVarint(buf + n, (1 << 3) | 0);
    n += pbWriteVarint(buf + n, POSITION_APP);
    n += pbWriteVarint(buf + n, (2 << 3) | 2);
    n += pbWriteVarint(buf + n, p);
    if (n + p > bufLen) return 0;
    memcpy(buf + n, pos, p); n += p;
    return n;
}

const char *portnumName(uint32_t p) {
    switch (p) {
        case TEXT_MESSAGE_APP:  return "TEXT";
        case POSITION_APP:      return "POSITION";
        case NODEINFO_APP:      return "NODEINFO";
        case ROUTING_APP:       return "ROUTING";
        case TELEMETRY_APP:     return "TELEMETRY";
        case NEIGHBORINFO_APP:  return "NEIGHBORINFO";
        default:                return "UNKNOWN";
    }
}
