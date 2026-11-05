#pragma once

// MeshBeacon (port 37) broadcast: the wire encoder and the send schedule.
// Pure logic, no Arduino headers, so it is host-testable (tests/test_mesh_beacon.cpp).
// Decoding lives in mesh_proto (decodeMeshBeacon).

#include <stddef.h>
#include <stdint.h>

struct MeshBeaconTx {
    char     message[61];
    char     offerChannelName[13];   // "" = no offer_channel
    uint8_t  offerPsk[32];
    uint8_t  offerPskLen;            // 0 = omit psk
    uint8_t  offerRegion;            // Meshtastic RegionCode; 0 = omit
    bool     hasOfferPreset;
    uint8_t  offerPreset;            // Meshtastic ModemPreset
    bool     hasOfferSlot;
    uint32_t offerSlot;              // 1-based
};

// MeshBeacon protobuf: message(1), offer_channel(2){psk(2), name(3)},
// offer_region(3), offer_preset(4), offer_slot(5). Returns 0 when nothing would
// be encoded or the result does not fit in cap.
size_t encodeMeshBeacon(const MeshBeaconTx &in, uint8_t *out, size_t cap);

// Data{portnum(1) = 37, payload(2)}. Returns 0 on overflow.
size_t encodeMeshBeaconData(const uint8_t *payload, size_t len, uint8_t *out, size_t cap);

// Copies at most 60 bytes of in, never splitting a UTF-8 code point.
void beaconCopyMessage(char out[61], const char *in);

// Snaps to the nearest of {1,2,3,6,12,24} hours, ties down; 0 -> 1, >24 -> 24.
uint8_t clampBeaconIntervalH(uint8_t h);

// Air-utilisation retry delay: 0 -> 5 min, then doubling, capped at 60 min.
uint32_t nextAirtimeRetryMs(uint32_t prevMs);

// 0 when a beacon is due, else ms until it is. lastSentMs == 0 means never
// sent; notBeforeMs == 0 means no hold. Wrap-safe across millis() rollover.
uint32_t beaconDueInMs(uint32_t nowMs, uint32_t lastSentMs, uint8_t intervalH, uint32_t notBeforeMs);

// True for the regions with a legal duty-cycle limit ("EU_433", "EU_868").
bool beaconRegionHasDutyCycle(const char *regionCode);
