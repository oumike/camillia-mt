// Vendored from camillia-chat-server 9754229; edit there, not here.
#include "cs_proto.h"
#include "sha256_portable.h"
#include <string.h>

namespace csp {

const char    DISCOVERY_CHANNEL_NAME[] = "camillia-cs";
const uint8_t DISCOVERY_KEY[16] = {0x45, 0xa5, 0x1e, 0xe6, 0xe7, 0x82, 0x49, 0x3f,
                                   0xfb, 0x9c, 0xdb, 0x16, 0x4b, 0xd7, 0xcd, 0xf7};

namespace {

// Meshtastic's default key; a 1-byte PSK N replaces the last byte with N.
const uint8_t kDefaultKeyBase[15] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                     0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69};

struct Writer {
    uint8_t *p; size_t cap; size_t n = 0; bool ok = true;
    void u8(uint8_t v)  { if (n + 1 > cap) { ok = false; return; } p[n++] = v; }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) u8((uint8_t)(v >> (8 * i))); }
    void bytes(const void *src, size_t len) {
        if (n + len > cap) { ok = false; return; }
        memcpy(p + n, src, len); n += len;
    }
    size_t done() const { return ok ? n : 0; }
};

struct Reader {
    const uint8_t *p; size_t len; size_t n = 0; bool ok = true;
    uint8_t u8() { if (n + 1 > len) { ok = false; return 0; } return p[n++]; }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= (uint32_t)u8() << (8 * i);
        return v;
    }
    void bytes(void *dst, size_t cnt) {
        if (n + cnt > len) { ok = false; return; }
        memcpy(dst, p + n, cnt); n += cnt;
    }
};

bool readHeader(Reader &r, Type want) {
    uint8_t v = r.u8(), t = r.u8();
    return r.ok && v == VERSION && t == want;
}

// Length-prefixed string into a fixed buffer of `cap` bytes (incl. NUL).
bool readStr(Reader &r, char *dst, size_t cap) {
    uint8_t len = r.u8();
    if (!r.ok || len >= cap) return false;
    r.bytes(dst, len);
    dst[len] = 0;
    return r.ok;
}

void writeStr(Writer &w, const char *s, size_t maxLen) {
    size_t len = strnlen(s, maxLen);
    w.u8((uint8_t)len);
    w.bytes(s, len);
}

}  // namespace

uint32_t channelId(const char *name, const uint8_t *key, size_t keyLen) {
    uint8_t expanded[16];
    if (keyLen == 1) {
        memcpy(expanded, kDefaultKeyBase, 15);
        expanded[15] = key[0];
        key = expanded; keyLen = 16;
    }
    Sha256Ctx c;
    sha256Init(c);
    sha256Update(c, (const uint8_t *)name, strlen(name));
    if (keyLen) sha256Update(c, key, keyLen);
    uint8_t d[32];
    sha256Final(c, d);
    return (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
}

bool peekType(const uint8_t *buf, size_t len, Type &out) {
    if (len < 2 || buf[0] != VERSION || buf[1] < DISCOVER || buf[1] > BATCH) return false;
    out = (Type)buf[1];
    return true;
}

size_t itemWireSize(const Item &it) { return ITEM_OVERHEAD + it.textLen; }

size_t encodeDiscover(uint8_t *buf, size_t cap) {
    Writer w{buf, cap};
    w.u8(VERSION); w.u8(DISCOVER);
    return w.done();
}

size_t encodeAnnounce(const Announce &a, uint8_t *buf, size_t cap) {
    if (a.count > MAX_ANNOUNCE_CHANNELS) return 0;
    Writer w{buf, cap};
    w.u8(VERSION); w.u8(ANNOUNCE);
    writeStr(w, a.shortName, sizeof(a.shortName) - 1);
    w.u8(a.count);
    for (uint8_t i = 0; i < a.count; i++) {
        w.u32(a.ch[i].id);
        writeStr(w, a.ch[i].name, sizeof(a.ch[i].name) - 1);
    }
    return w.done();
}

bool decodeAnnounce(const uint8_t *buf, size_t len, Announce &out) {
    Reader r{buf, len};
    if (!readHeader(r, ANNOUNCE)) return false;
    if (!readStr(r, out.shortName, sizeof(out.shortName))) return false;
    out.count = r.u8();
    if (!r.ok || out.count > MAX_ANNOUNCE_CHANNELS) return false;
    for (uint8_t i = 0; i < out.count; i++) {
        out.ch[i].id = r.u32();
        if (!readStr(r, out.ch[i].name, sizeof(out.ch[i].name))) return false;
    }
    return r.ok && r.n == len;
}

size_t encodeRequest(const Request &q, uint8_t *buf, size_t cap) {
    Writer w{buf, cap};
    w.u8(VERSION); w.u8(REQUEST);
    w.u32(q.epoch); w.u32(q.cursor); w.u32(q.anchorFrom); w.u32(q.anchorId);
    return w.done();
}

bool decodeRequest(const uint8_t *buf, size_t len, Request &out) {
    Reader r{buf, len};
    if (!readHeader(r, REQUEST)) return false;
    out.epoch = r.u32(); out.cursor = r.u32(); out.anchorFrom = r.u32(); out.anchorId = r.u32();
    return r.ok && r.n == len;
}

size_t encodeBatch(const BatchHeader &h, const Item *items, uint8_t n, uint8_t *buf, size_t cap) {
    if (cap > MAX_PAYLOAD) cap = MAX_PAYLOAD;
    Writer w{buf, cap};
    w.u8(VERSION); w.u8(BATCH);
    w.u32(h.epoch); w.u8(h.flags); w.u32(h.serverTime); w.u8(n);
    for (uint8_t i = 0; i < n; i++) {
        const Item &it = items[i];
        if (it.textLen > MAX_TEXT) return 0;
        w.u32(it.seq); w.u32(it.from); w.u32(it.packetId); w.u32(it.ageSec);
        w.u8(it.textLen);
        w.bytes(it.text, it.textLen);
    }
    return w.done();
}

bool decodeBatch(const uint8_t *buf, size_t len, BatchHeader &h, Item *items, uint8_t cap,
                 uint8_t &n) {
    Reader r{buf, len};
    if (!readHeader(r, BATCH)) return false;
    h.epoch = r.u32(); h.flags = r.u8(); h.serverTime = r.u32(); h.count = r.u8();
    if (!r.ok || h.count > cap) return false;
    for (uint8_t i = 0; i < h.count; i++) {
        Item &it = items[i];
        it.seq = r.u32(); it.from = r.u32(); it.packetId = r.u32(); it.ageSec = r.u32();
        it.textLen = r.u8();
        if (!r.ok || it.textLen > MAX_TEXT) return false;
        r.bytes(it.text, it.textLen);
        it.text[it.textLen] = 0;
    }
    if (!r.ok || r.n != len) return false;
    n = h.count;
    return true;
}

}  // namespace csp
