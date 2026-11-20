// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Who laid out the flash this firmware runs from, judged from the partition
// table alone (issue #160).
//
// Camillia's own images ship a dual-slot table: ota_0 at 0x10000 and an ota_1
// of the same size, and an over-the-air update writes whichever is not running.
//
// bmorcelli's Launcher (https://github.com/bmorcelli/Launcher) keeps itself at
// 0x10000 instead -- a `test` app on the ESP32-S3 and a `factory` app on the
// ESP32-P4 (support_files/custom_*.csv there) -- and installs each firmware
// into an OTA slot it creates above itself. Its 20 KB NVS partition is shared by
// Launcher and every app it installed.
//
// Kept free of ESP-IDF so the decision can be tested on the host
// (tests/test_flash_layout.cpp). ota_update.cpp reads the partition table and
// passes it in.
#include <stdint.h>

enum FlashLayout : uint8_t {
    // Neither shape. Fails closed: no update is offered and nothing about the
    // partition's other users is assumed.
    FLASH_LAYOUT_UNKNOWN = 0,
    // Camillia's dual-slot table: an over-the-air update is safe.
    FLASH_LAYOUT_CAMILLIA,
    // Installed by Launcher: updates come through Launcher, and the NVS
    // partition also holds Launcher's and its other apps' data.
    FLASH_LAYOUT_LAUNCHER,
};

// One app partition, or present == false when the table has none of that kind.
struct FlashAppPartition {
    bool     present;
    uint8_t  subtype;   // ESP-IDF app subtype: 0x00 factory, 0x10 + n ota_n, 0x20 test
    uint32_t address;
    uint32_t size;
};

// The first partition of each kind that esp_partition_find_first() returns,
// and the one this firmware is running from.
struct FlashLayoutParts {
    FlashAppPartition factory;
    FlashAppPartition test;
    FlashAppPartition ota0;
    FlashAppPartition ota1;
    FlashAppPartition ota2;
    FlashAppPartition running;
};

// The first app partition follows the bootloader, partition table and data
// partitions at the conventional 0x10000. Whoever owns the flash keeps their own
// app there, which is what pushes an installer's OTA slots higher.
static constexpr uint32_t kFlashFirstAppAddress = 0x10000;

static inline bool flashAppSubtypeIsOta(uint8_t subtype) {
    return subtype >= 0x10 && subtype <= 0x1F;
}

static inline FlashLayout flashLayoutClassify(const FlashLayoutParts &p) {
    if (!p.running.present) return FLASH_LAYOUT_UNKNOWN;

    // Camillia's: two slots of the same size, no third, the first at 0x10000,
    // and the running app in one of them. Structural rather than a size
    // comparison against partitions.csv, so it stays right if the sizes change.
    if (p.ota0.present && p.ota1.present && !p.ota2.present
        && p.ota0.size == p.ota1.size
        && p.ota0.address == kFlashFirstAppAddress
        && (p.running.address == p.ota0.address || p.running.address == p.ota1.address)) {
        return FLASH_LAYOUT_CAMILLIA;
    }

    // Launcher's: its own non-OTA app at 0x10000, and this firmware in one of
    // the OTA slots it created above it.
    const bool installerAtBase =
        (p.test.present && p.test.address == kFlashFirstAppAddress)
        || (p.factory.present && p.factory.address == kFlashFirstAppAddress);
    if (installerAtBase && flashAppSubtypeIsOta(p.running.subtype)
        && p.running.address != kFlashFirstAppAddress) {
        return FLASH_LAYOUT_LAUNCHER;
    }
    return FLASH_LAYOUT_UNKNOWN;
}
