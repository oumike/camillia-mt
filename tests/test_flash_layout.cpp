// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/flash_layout.h"

#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

static const uint8_t kFactory = 0x00;
static const uint8_t kOta0 = 0x10;
static const uint8_t kOta1 = 0x11;
static const uint8_t kOta2 = 0x12;
static const uint8_t kTest = 0x20;

static FlashAppPartition app(uint8_t subtype, uint32_t address, uint32_t size) {
    FlashAppPartition p = {};
    p.present = true;
    p.subtype = subtype;
    p.address = address;
    p.size = size;
    return p;
}

// partitions_16mb.csv: app0 ota_0 at 0x10000 and app1 ota_1 at 0x670000.
static FlashLayoutParts camilliaTable() {
    FlashLayoutParts p = {};
    p.ota0 = app(kOta0, 0x10000, 0x640000);
    p.ota1 = app(kOta1, 0x670000, 0x640000);
    p.running = p.ota0;
    return p;
}

// Launcher's support_files/custom_16Mb.csv (S3, `test`) or custom_16Mb_p4.csv
// (P4, `factory`), plus the slot Launcher created for Camillia after coredump.
static FlashLayoutParts launcherTable(uint8_t launcherSubtype) {
    FlashLayoutParts p = {};
    const FlashAppPartition launcher = app(launcherSubtype, 0x10000, 0x180000);
    if (launcherSubtype == kTest) p.test = launcher;
    else p.factory = launcher;
    p.ota0 = app(kOta0, 0x1A0000, 0x3C0000);
    p.running = p.ota0;
    return p;
}

static void testCamilliaTable() {
    FlashLayoutParts p = camilliaTable();
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_CAMILLIA);
    p.running = p.ota1;
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_CAMILLIA);
}

static void testLauncherS3AndP4() {
    CHECK(flashLayoutClassify(launcherTable(kTest)) == FLASH_LAYOUT_LAUNCHER);
    CHECK(flashLayoutClassify(launcherTable(kFactory)) == FLASH_LAYOUT_LAUNCHER);
}

// Launcher sizes each slot to the image it installs. Two apps whose slots
// happen to match in size must still not read as Camillia's table: the slot
// an update would write belongs to the other app.
static void testLauncherWithTwoApps() {
    FlashLayoutParts p = launcherTable(kTest);
    p.ota0 = app(kOta0, 0x1A0000, 0x300000);
    p.ota1 = app(kOta1, 0x4A0000, 0x300000);
    p.running = p.ota1;
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_LAUNCHER);
}

static void testFailsClosed() {
    FlashLayoutParts p = camilliaTable();
    p.running = FlashAppPartition{};
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_UNKNOWN);

    p = camilliaTable();
    p.ota2 = app(kOta2, 0xCB0000, 0x340000);
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_UNKNOWN);

    p = camilliaTable();
    p.ota1.size = 0x300000;
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_UNKNOWN);

    // OTA slots above 0x10000 with no installer app below them.
    p = launcherTable(kTest);
    p.test = FlashAppPartition{};
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_UNKNOWN);

    // An installer app that is not at 0x10000.
    p = launcherTable(kFactory);
    p.factory.address = 0x20000;
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_UNKNOWN);

    // Running from Launcher's own partition rather than a slot it created.
    p = launcherTable(kTest);
    p.running = p.test;
    CHECK(flashLayoutClassify(p) == FLASH_LAYOUT_UNKNOWN);
}

int main() {
    testCamilliaTable();
    testLauncherS3AndP4();
    testLauncherWithTwoApps();
    testFailsClosed();
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
