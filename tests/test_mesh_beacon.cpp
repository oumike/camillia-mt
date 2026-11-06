// Host tests for the MeshBeacon (port 37) encoder and broadcast schedule.
// The decode round trip lives in test_mesh_proto.cpp, which links the decoder.
#include "../src/mesh_beacon.h"
#include "test_support.h"

static bool bytesEqual(const uint8_t *got, size_t gotLen, const uint8_t *want, size_t wantLen) {
    return gotLen == wantLen && memcmp(got, want, wantLen) == 0;
}

static MeshBeaconTx longFastOffer() {
    MeshBeaconTx tx{};
    strcpy(tx.offerChannelName, "LongFast");
    tx.offerRegion = 1;
    tx.hasOfferPreset = true;
    tx.offerPreset = 0;
    return tx;
}

static void test_encode_text_only() {
    MeshBeaconTx tx{};
    strcpy(tx.message, "hi");
    uint8_t out[64];
    const uint8_t want[] = {0x0A, 0x02, 0x68, 0x69};
    CHECK(bytesEqual(out, encodeMeshBeacon(tx, out, sizeof(out)), want, sizeof(want)));
}

static void test_encode_offer_no_psk() {
    const MeshBeaconTx tx = longFastOffer();
    uint8_t out[64];
    const uint8_t want[] = {0x12, 0x0A, 0x1A, 0x08, 0x4C, 0x6F, 0x6E, 0x67, 0x46, 0x61,
                            0x73, 0x74, 0x18, 0x01, 0x20, 0x00};
    CHECK(bytesEqual(out, encodeMeshBeacon(tx, out, sizeof(out)), want, sizeof(want)));
}

static void test_encode_offer_with_psk() {
    MeshBeaconTx tx{};
    strcpy(tx.offerChannelName, "Home");
    tx.offerPsk[0] = 0x01;
    tx.offerPskLen = 1;
    tx.offerRegion = 3;
    uint8_t out[64];
    const uint8_t want[] = {0x12, 0x09, 0x12, 0x01, 0x01, 0x1A, 0x04, 0x48, 0x6F, 0x6D,
                            0x65, 0x18, 0x03};
    CHECK(bytesEqual(out, encodeMeshBeacon(tx, out, sizeof(out)), want, sizeof(want)));
}

static void test_encode_with_slot() {
    MeshBeaconTx tx = longFastOffer();
    tx.hasOfferSlot = true;
    tx.offerSlot = 20;
    uint8_t out[64];
    const uint8_t want[] = {0x12, 0x0A, 0x1A, 0x08, 0x4C, 0x6F, 0x6E, 0x67, 0x46, 0x61,
                            0x73, 0x74, 0x18, 0x01, 0x20, 0x00, 0x28, 0x14};
    CHECK(bytesEqual(out, encodeMeshBeacon(tx, out, sizeof(out)), want, sizeof(want)));
}

static void test_encode_empty_returns_zero() {
    const MeshBeaconTx tx{};
    uint8_t out[64];
    CHECK(encodeMeshBeacon(tx, out, sizeof(out)) == 0);
}

static void test_encode_overflow_returns_zero() {
    MeshBeaconTx tx{};
    strcpy(tx.message, "hi");
    uint8_t out[3];
    CHECK(encodeMeshBeacon(tx, out, sizeof(out)) == 0);
}

static void test_data_envelope() {
    const uint8_t payload[] = {0xAA};
    uint8_t out[16];
    const uint8_t want[] = {0x08, 0x25, 0x12, 0x01, 0xAA};
    CHECK(bytesEqual(out, encodeMeshBeaconData(payload, sizeof(payload), out, sizeof(out)),
                     want, sizeof(want)));
    CHECK(encodeMeshBeaconData(payload, sizeof(payload), out, 4) == 0);
}

static void test_message_truncates_on_utf8_boundary() {
    char in[64];
    memset(in, 'a', 59);
    strcpy(in + 59, "\xC3\xA9");
    char out[61];
    beaconCopyMessage(out, in);
    CHECK(strlen(out) == 59);
    CHECK(memcmp(out, in, 59) == 0);

    beaconCopyMessage(out, nullptr);
    CHECK(out[0] == '\0');
}

static void test_interval_clamp() {
    CHECK(clampBeaconIntervalH(0) == 1);
    CHECK(clampBeaconIntervalH(4) == 3);
    CHECK(clampBeaconIntervalH(5) == 6);
    CHECK(clampBeaconIntervalH(9) == 6);
    CHECK(clampBeaconIntervalH(18) == 12);
    CHECK(clampBeaconIntervalH(30) == 24);
    CHECK(clampBeaconIntervalH(6) == 6);
}

static void test_airtime_backoff() {
    const uint32_t steps[] = {300000, 600000, 1200000, 2400000, 3600000, 3600000};
    uint32_t ms = 0;
    for (uint32_t want : steps) {
        ms = nextAirtimeRetryMs(ms);
        CHECK(ms == want);
    }
}

static void test_due_never_sent() {
    CHECK(beaconDueInMs(5000, 0, 1, 0) == 0);
}

static void test_not_due() {
    CHECK(beaconDueInMs(1000 + 1800000, 1000, 1, 0) == 1800000);
}

static void test_due_after_reboot_without_clock() {
    const uint32_t now = 4321;
    CHECK(beaconDueInMs(now, now, 1, 0) == 3600000);
}

static void test_due_across_millis_wrap() {
    CHECK(beaconDueInMs(0x0000F000u + 3600000u, 0xFFFF0000u, 1, 0) == 0);
}

static void test_hold_extends() {
    const uint32_t now = 10000000;
    CHECK(beaconDueInMs(now, now - 3600000, 1, now + 5000) == 5000);
}

static void test_duty_cycle_regions() {
    CHECK(beaconRegionHasDutyCycle("EU_868"));
    CHECK(beaconRegionHasDutyCycle("EU_433"));
    CHECK(!beaconRegionHasDutyCycle("US"));
    CHECK(!beaconRegionHasDutyCycle(nullptr));
}

int main() {
    test_encode_text_only();
    test_encode_offer_no_psk();
    test_encode_offer_with_psk();
    test_encode_with_slot();
    test_encode_empty_returns_zero();
    test_encode_overflow_returns_zero();
    test_data_envelope();
    test_message_truncates_on_utf8_boundary();
    test_interval_clamp();
    test_airtime_backoff();
    test_due_never_sent();
    test_not_due();
    test_due_after_reboot_without_clock();
    test_due_across_millis_wrap();
    test_hold_extends();
    test_duty_cycle_regions();
    return test::report();
}
