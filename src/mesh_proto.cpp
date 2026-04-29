#include "mesh_proto.h"
#include "debug_flags.h"
#include "mbedtls/aes.h"
#include "mbedtls/ccm.h"
#include "mbedtls/sha256.h"
#include <Curve25519.h>
#include <esp_random.h>

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

static void resolveMeshKey(const uint8_t *key, uint8_t keyLen,
                           uint8_t expanded[16],
                           const uint8_t *&outKey, uint8_t &outLen) {
    outKey = key;
    outLen = keyLen;

    // Meshtastic PSK index 0 (AA==) disables channel encryption.
    if (keyLen == 1 && key && key[0] == 0x00) {
        outLen = 0;
        return;
    }

    if (keyLen == 1 && key) {
        expandPsk(key[0], expanded);
        outKey = expanded;
        outLen = 16;
    }
}

uint8_t computeChannelHash(const char *name, const uint8_t *key, uint8_t keyLen) {
    uint8_t exp[16];
    const uint8_t *k = key;
    uint8_t kl = keyLen;
    resolveMeshKey(key, keyLen, exp, k, kl);
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
    { "LongFast", { 0x01 },    1, 0x08, {}, 0 },  // PRIMARY  (AQ==)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "",         { 0x01 },    1, 0x08, {}, 1 },  // SECONDARY (unconfigured)
    { "DM",       { 0 },       0, 0xFF, {}, 2 },  // DISABLED  (virtual, direct messages)
    { "ANN",      { 0 },       0, 0xFF, {}, 2 },  // DISABLED  (virtual, announcements)
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
                uint32_t &requestId, bool &wantResponse) {
    portnum = 0; payPtr = nullptr; payLen = 0; requestId = 0; wantResponse = false;
    size_t i = 0;
    while (i < len) {
        uint64_t tag; i = pbReadVarint(buf, len, i, tag); if (!i) break;
        uint32_t field = tag >> 3, wtype = tag & 7;
        if (wtype == 0) {
            uint64_t v; i = pbReadVarint(buf, len, i, v); if (!i) break;
            if (field == 1) portnum = (uint32_t)v;
            else if (field == 3) wantResponse = (v != 0);
            else if (field == 6) requestId = (uint32_t)v;   // request_id varint (Meshtastic standard)
        } else if (wtype == 2) {
            uint64_t sz; i = pbReadVarint(buf, len, i, sz); if (!i) break;
            if (field == 2) { payPtr = buf + i; payLen = (size_t)sz; }
            i += sz;
        } else if (wtype == 5) {
            // fixed32 — fields 4=dest, 5=source, 6=request_id, 7=reply_id, 8=emoji
            if (i + 4 <= len) {
                uint32_t v; memcpy(&v, buf + i, 4);
                if (field == 6) requestId = v;
            }
            i += 4;
        } else { i = pbSkip(buf, len, i, wtype); if (!i) break; }
    }
    return true;
}

bool decodeUser(const uint8_t *buf, size_t len, UserInfo &out) {
    out.longName[0] = out.shortName[0] = '\0';
    memset(out.pubKey, 0, 32);
    out.hasPubKey = false;
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
            } else if (field == 8 && sz == 32) {
                memcpy(out.pubKey, buf + i, 32);
                out.hasPubKey = true;
            }
            i += sz;
        } else { i = pbSkip(buf, len, i, wtype); if (!i) break; }
    }
    return true;
}

bool decodePosition(const uint8_t *buf, size_t len, PositionInfo &out) {
    out.latI = out.lonI = out.alt = 0;
    // Legacy compatibility: older builds encoded lat/lon as sint32 varints.
    auto unzz = [](uint32_t v) -> int32_t {
        return (int32_t)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
    };
    size_t i = 0;
    while (i < len) {
        uint64_t tag; i = pbReadVarint(buf, len, i, tag); if (!i) break;
        uint32_t field = tag >> 3, wtype = tag & 7;
        if (wtype == 5) {
            // Current Meshtastic Position.latitude_i/longitude_i are sfixed32.
            if (i + 4 > len) break;
            uint32_t v = (uint32_t)buf[i]
                       | ((uint32_t)buf[i + 1] << 8)
                       | ((uint32_t)buf[i + 2] << 16)
                       | ((uint32_t)buf[i + 3] << 24);
            if (field == 1) out.latI = (int32_t)v;
            else if (field == 2) out.lonI = (int32_t)v;
            else if (field == 3) out.alt  = (int32_t)v;
            i += 4;
        } else if (wtype == 0) {
            uint64_t v; i = pbReadVarint(buf, len, i, v); if (!i) break;
            if (field == 1) out.latI = unzz((uint32_t)v);
            else if (field == 2) out.lonI = unzz((uint32_t)v);
            else if (field == 3) out.alt  = (int32_t)(uint32_t)v;
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
    uint32_t reqId = 0; bool wantResp = false;
    decodeData(plain, len, portnum, payPtr, payLen, reqId, wantResp);
    // Accept known ports or any non-zero port up to 1024
    return portnum > 0 && portnum <= 1024;
}

int decryptPacket(const MeshHdr &hdr, const uint8_t *cipher,
                  uint8_t *plain, size_t len) {
    auto tryDecrypt = [&](int i) -> bool {
        uint8_t exp[16];
        const uint8_t *keyPtr = CHANNEL_KEYS[i].key;
        uint8_t keyLen = CHANNEL_KEYS[i].keyLen;
        resolveMeshKey(CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen, exp, keyPtr, keyLen);
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
    uint8_t exp[16];
    resolveMeshKey(key, keyLen, exp, key, keyLen);
    if (keyLen == 0) { memcpy(cipher, plain, len); return true; }
    return aesCtr(key, keyLen, packetId, fromNode, plain, cipher, len);
}

// ── PKI (Curve25519) encryption ───────────────────────────────
// Meshtastic wire format: [ciphertext(N)] [CCM-tag(8)] [extraNonce(4)]
// Nonce (8 bytes): [packetId_LE32(4)] [extraNonce_LE(4)]
// Key: SHA256(ECDH(myPrivKey, recipientPubKey))
// Caller sets hdr.channel = 0 to signal PKI to receiving nodes.
static bool derivePkiAesKey(const uint8_t *remotePubKey, uint8_t outAesKey[32]) {
    if (!remotePubKey) return false;

    uint8_t sharedKey[32];
    uint8_t localPriv[32];
    memcpy(sharedKey, remotePubKey, 32);
    memcpy(localPriv, myPrivKey, 32);

    // Match Meshtastic firmware behavior (Curve25519::dh2 + SHA256).
    if (!Curve25519::dh2(sharedKey, localPriv)) {
        return false;
    }

    mbedtls_sha256(sharedKey, 32, outAesKey, 0);
    return true;
}

bool encryptPki(uint32_t packetId, uint32_t fromNode,
                const uint8_t *recipientPubKey,
                const uint8_t *plain, size_t plainLen,
                uint8_t *out) {
    bool ok = false;
    int step = 0;
    do {
        uint8_t aesKey[32];
    if (!derivePkiAesKey(recipientPubKey, aesKey)) { step = 1; break; }

        uint32_t extraNonce;
        esp_fill_random(&extraNonce, sizeof(extraNonce));

        // 13-byte nonce: [packetId_LE32][extraNonce_LE32][fromNode_LE32][0x00]
        uint8_t nonce[13] = {};
        memcpy(nonce,     &packetId,   4);
        memcpy(nonce + 4, &extraNonce, 4);
        memcpy(nonce + 8, &fromNode,   4);

        mbedtls_ccm_context ccm;
        mbedtls_ccm_init(&ccm);
        int ret = mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, aesKey, 256);
        if (ret != 0) { step=2; mbedtls_ccm_free(&ccm); break; }
        uint8_t tag[8];
        ret = mbedtls_ccm_encrypt_and_tag(&ccm, plainLen,
                                           nonce, sizeof(nonce),
                                           nullptr, 0,
                                           plain, out,
                                           tag, sizeof(tag));
        mbedtls_ccm_free(&ccm);
        if (ret != 0) { step=3; break; }

        memcpy(out + plainLen,     tag,         8);
        memcpy(out + plainLen + 8, &extraNonce, 4);
        ok = true;
    } while (false);

    if (!ok) debugLogMessages("[pki] encryptPki failed at step %d\n", step);
    return ok;
}

// ── PKI decrypt ───────────────────────────────────────────────
// Wire format: [ciphertext(N)] [CCM-tag(8)] [extraNonce(4)]
// Nonce (13 bytes): [packetId_LE32(4)] [extraNonce_LE32(4)] [fromNode_LE32(4)] [0x00]
bool decryptPki(const MeshHdr &hdr, const uint8_t *cipher, size_t cipherLen,
                const uint8_t *senderPubKey, uint8_t *plain, size_t &plainLen) {
    if (cipherLen < 13) return false;   // need at least 1 byte of plaintext + 12 overhead

    plainLen = cipherLen - 12;
    const uint8_t *ciphertext = cipher;
    const uint8_t *tag        = cipher + plainLen;      // tag[8]
    uint32_t extraNonce;
    memcpy(&extraNonce, cipher + plainLen + 8, 4);      // extraNonce[4]

    bool ok = false;
    int step = 0;
    do {
        uint8_t aesKey[32];
        if (!derivePkiAesKey(senderPubKey, aesKey)) { step = 1; break; }

        // 13-byte nonce: [packetId_LE32][extraNonce_LE32][fromNode_LE32][0x00]
        uint8_t nonce[13] = {};
        memcpy(nonce,     &hdr.id,   4);
        memcpy(nonce + 4, &extraNonce, 4);
        memcpy(nonce + 8, &hdr.from, 4);

        mbedtls_ccm_context ccm;
        mbedtls_ccm_init(&ccm);
        int ret = mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, aesKey, 256);
        if (ret != 0) { step=2; mbedtls_ccm_free(&ccm); break; }
        ret = mbedtls_ccm_auth_decrypt(&ccm, plainLen,
                                        nonce, sizeof(nonce),
                                        nullptr, 0,
                                        ciphertext, plain,
                                        tag, 8);
        mbedtls_ccm_free(&ccm);
        if (ret != 0) { step=3; break; }
        ok = true;
    } while (false);

    if (!ok) debugLogMessages("[pki] decryptPki failed at step %d\n", step);
    return ok;
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

size_t encodeTextMessage(const char *text, uint8_t *buf, size_t bufLen, uint32_t bitfield) {
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
    // field 9 (bitfield), varint — only written when non-zero (e.g. OK_TO_MQTT bit)
    if (bitfield) {
        n += pbWriteVarint(buf + n, (9 << 3) | 0);
        n += pbWriteVarint(buf + n, bitfield);
    }
    return n;
}

size_t encodeTextMessageUnicast(const char *text,
                                uint32_t fromNode, uint32_t toNode,
                                uint8_t *buf, size_t bufLen) {
    size_t n = encodeTextMessage(text, buf, bufLen, 0);
    if (n == 0) return 0;
    if (n + 10 > bufLen) return 0;

    // Data field 4 (dest), fixed32
    buf[n++] = (4 << 3) | 5;
    memcpy(buf + n, &toNode, 4); n += 4;

    // Data field 5 (source), fixed32
    buf[n++] = (5 << 3) | 5;
    memcpy(buf + n, &fromNode, 4); n += 4;

    return n;
}

size_t encodeNodeInfo(uint32_t nodeId, const char *longName,
                      const char *shortName, const uint8_t *mac6,
                      uint8_t *buf, size_t bufLen, bool wantResponse) {
    // Build inner User message (extra 34 bytes for field 8 = public_key)
    uint8_t user[164]; size_t u = 0;

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

    u += pbWriteVarint(user + u, (5 << 3) | 0);
    u += pbWriteVarint(user + u, 50); // HardwareModel::T_DECK = 50

    // field 6 = is_licensed: omit (defaults to false).
    // Official Meshtastic firmware STRIPS the public key when is_licensed=true,
    // so setting it here would break PKI DMs with standard nodes.

    // field 7 = role (Config.DeviceConfig.Role varint).
    if (myDeviceRole != 0) {  // CLIENT (0) is proto default, omit to save bytes
        u += pbWriteVarint(user + u, (7 << 3) | 0);
        u += pbWriteVarint(user + u, myDeviceRole);
    }

    // field 8 = public_key (bytes, 32 bytes).  Advertise our Curve25519 public key so
    // other nodes can encrypt PKI DMs to us, and so we can encrypt DMs to them.
    // Only include if key is non-zero (i.e. key generation succeeded).
    bool pubKeyValid = false;
    for (int i = 0; i < 32; i++) { if (myPubKey[i]) { pubKeyValid = true; break; } }
    if (pubKeyValid && u + 35 <= sizeof(user)) {
        u += pbWriteVarint(user + u, (8 << 3) | 2);
        u += pbWriteVarint(user + u, 32);
        memcpy(user + u, myPubKey, 32); u += 32;
    }
    debugLogMessages("[nodeinfo] encode: pubKey=%s  user=%u bytes\n",
                     pubKeyValid ? "YES" : "NO", (unsigned)u);

    // Wrap in Data message
    size_t n = 0;
    n += pbWriteVarint(buf + n, (1 << 3) | 0);
    n += pbWriteVarint(buf + n, NODEINFO_APP);
    n += pbWriteVarint(buf + n, (2 << 3) | 2);
    n += pbWriteVarint(buf + n, u);
    if (n + u > bufLen) return 0;
    memcpy(buf + n, user, u); n += u;
    if (wantResponse) {
        // want_response = true: causes receiving nodes to reply with their own NODEINFO
        n += pbWriteVarint(buf + n, (3 << 3) | 0);  // field 3, varint
        n += pbWriteVarint(buf + n, 1);              // true
    }
    return n;
}

size_t encodePosition(int32_t latI, int32_t lonI, int32_t alt,
                      uint8_t *buf, size_t bufLen) {
    uint8_t pos[32]; size_t p = 0;

    // Meshtastic Position.latitude_i/longitude_i are sfixed32.
    auto writeFixed32 = [&](uint32_t field, int32_t value) -> bool {
        if (p + 5 > sizeof(pos)) return false;
        pos[p++] = (uint8_t)((field << 3) | 5);
        uint32_t v = (uint32_t)value;
        pos[p++] = (uint8_t)(v & 0xFF);
        pos[p++] = (uint8_t)((v >> 8) & 0xFF);
        pos[p++] = (uint8_t)((v >> 16) & 0xFF);
        pos[p++] = (uint8_t)((v >> 24) & 0xFF);
        return true;
    };

    if (!writeFixed32(1, latI) || !writeFixed32(2, lonI)) return 0;

    // altitude is int32 — plain varint (two's complement for negatives)
    if (p + 6 > sizeof(pos)) return 0;
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

size_t encodeRouting(uint32_t requestId, uint32_t fromNodeId, uint32_t errorReason,
                     uint8_t *buf, size_t bufLen) {
    // Inner Routing proto: field 3 (error_reason), varint
    uint8_t inner[4]; size_t innerLen = 0;
    inner[innerLen++] = (3 << 3) | 0;  // field 3, varint
    innerLen += pbWriteVarint(inner + innerLen, errorReason);

    size_t n = 0;
    // Data field 1 (portnum = ROUTING_APP), varint
    n += pbWriteVarint(buf + n, (1 << 3) | 0);
    n += pbWriteVarint(buf + n, ROUTING_APP);
    // Data field 2 (payload = inner Routing proto), length-delimited
    n += pbWriteVarint(buf + n, (2 << 3) | 2);
    n += pbWriteVarint(buf + n, innerLen);
    if (n + innerLen + 10 > bufLen) return 0;
    memcpy(buf + n, inner, innerLen); n += innerLen;
    // Data field 5 (source), fixed32 — identifies who is sending this ACK
    buf[n++] = (5 << 3) | 5;  // field 5, wire type 5 (fixed32)
    memcpy(buf + n, &fromNodeId, 4); n += 4;
    // Data field 6 (request_id), fixed32 — ID of the packet being ACK'd
    buf[n++] = (6 << 3) | 5;  // field 6, wire type 5 (fixed32)
    memcpy(buf + n, &requestId, 4); n += 4;
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
