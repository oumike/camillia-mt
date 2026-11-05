#include "mesh_beacon.h"

#include <string.h>

#include "utf8_utils.h"

namespace {

constexpr uint32_t kMeshBeaconPort = 37;
constexpr size_t kMessageMax = 60;
constexpr uint32_t kHourMs = 3600000u;
constexpr uint32_t kRetryFirstMs = 300000u;
constexpr uint32_t kRetryCapMs = 3600000u;

// Bounded protobuf writer: once anything fails to fit, ok stays false.
struct PbOut {
    uint8_t *buf;
    size_t cap;
    size_t len = 0;
    bool ok = true;

    void byte(uint8_t b) {
        if (len >= cap) { ok = false; return; }
        buf[len++] = b;
    }
    void varint(uint64_t v) {
        while (v >= 0x80) { byte((uint8_t)(v | 0x80)); v >>= 7; }
        byte((uint8_t)v);
    }
    void tag(uint32_t field, uint32_t wireType) { varint(((uint64_t)field << 3) | wireType); }
    void varintField(uint32_t field, uint64_t v) { tag(field, 0); varint(v); }
    void bytesField(uint32_t field, const uint8_t *p, size_t n) {
        tag(field, 2);
        varint(n);
        if (!ok || n > cap - len) { ok = false; return; }
        memcpy(buf + len, p, n);
        len += n;
    }
    size_t result() const { return ok ? len : 0; }
};

} // namespace

size_t encodeMeshBeacon(const MeshBeaconTx &in, uint8_t *out, size_t cap) {
    if (!out) return 0;
    PbOut pb{out, cap};

    const size_t msgLen = strnlen(in.message, kMessageMax);
    if (msgLen) pb.bytesField(1, (const uint8_t *)in.message, msgLen);

    const size_t nameLen = strnlen(in.offerChannelName, sizeof(in.offerChannelName) - 1);
    if (nameLen) {
        uint8_t ch[2 + sizeof(in.offerPsk) + 2 + sizeof(in.offerChannelName)];
        PbOut sub{ch, sizeof(ch)};
        size_t pskLen = in.offerPskLen;
        if (pskLen > sizeof(in.offerPsk)) pskLen = sizeof(in.offerPsk);
        if (pskLen) sub.bytesField(2, in.offerPsk, pskLen);
        sub.bytesField(3, (const uint8_t *)in.offerChannelName, nameLen);
        if (!sub.ok) return 0;
        pb.bytesField(2, ch, sub.len);
    }

    if (in.offerRegion) pb.varintField(3, in.offerRegion);
    if (in.hasOfferPreset) pb.varintField(4, in.offerPreset);
    if (in.hasOfferSlot) pb.varintField(5, in.offerSlot);

    return pb.result();
}

size_t encodeMeshBeaconData(const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    if (!out || (!payload && len)) return 0;
    PbOut pb{out, cap};
    pb.varintField(1, kMeshBeaconPort);
    pb.bytesField(2, payload, len);
    return pb.result();
}

void beaconCopyMessage(char out[61], const char *in) {
    utf8util::copyTruncate(out, kMessageMax + 1, in);
}

uint8_t clampBeaconIntervalH(uint8_t h) {
    static const uint8_t kAllowed[] = {1, 2, 3, 6, 12, 24};
    uint8_t best = kAllowed[0];
    unsigned bestDist = 0xFFFF;
    for (uint8_t a : kAllowed) {
        const unsigned dist = h > a ? (unsigned)(h - a) : (unsigned)(a - h);
        if (dist < bestDist) {      // strict: on a tie the smaller value, seen first, wins
            best = a;
            bestDist = dist;
        }
    }
    return best;
}

uint32_t nextAirtimeRetryMs(uint32_t prevMs) {
    if (prevMs == 0) return kRetryFirstMs;
    if (prevMs >= kRetryCapMs / 2) return kRetryCapMs;
    return prevMs * 2;
}

uint32_t beaconDueInMs(uint32_t nowMs, uint32_t lastSentMs, uint8_t intervalH, uint32_t notBeforeMs) {
    uint32_t wait = 0;
    if (lastSentMs != 0) {
        const uint32_t interval = (uint32_t)clampBeaconIntervalH(intervalH) * kHourMs;
        const uint32_t elapsed = (uint32_t)(nowMs - lastSentMs);
        if (elapsed < interval) wait = interval - elapsed;
    }
    if (notBeforeMs != 0) {
        const uint32_t hold = (uint32_t)(notBeforeMs - nowMs);
        // A "remaining" above 2^31 means notBefore is already in the past.
        if (hold < 0x80000000u && hold > wait) wait = hold;
    }
    return wait;
}

bool beaconRegionHasDutyCycle(const char *regionCode) {
    if (!regionCode) return false;
    return strcmp(regionCode, "EU_433") == 0 || strcmp(regionCode, "EU_868") == 0;
}
