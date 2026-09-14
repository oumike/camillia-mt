#include "storage.h"
#include "config.h"

#if HAS_SD_CARD && !(defined(HAS_SD_MMC) && HAS_SD_MMC)
#include <SPI.h>
#endif
#if defined(DEVICE_WIO_TRACKER_L2)
#include "hal/wio_tracker_l2_io.h"
#endif

namespace {
bool sMounted = false;
}

fs::FS &storageFs() {
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    return SD_MMC;
#elif HAS_SD_CARD
    return SD;
#else
    return LittleFS;
#endif
}

bool storageMounted() { return sMounted; }

const char *storageName() {
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    return "SD card (SD_MMC)";
#elif HAS_SD_CARD
    return "SD card";
#else
    return "internal flash";
#endif
}

// Both of these ask the driver rather than sMounted. On the SPI-SD boards the
// card is mounted by sdBegin() in config_io.cpp — its retry ladder and bus
// setup live there — which never sets the flag below, so a check against it
// would report "no card" on exactly the boards that have one. cardType() is
// the driver's own answer and is safe before begin(): no card object means
// CARD_NONE.
uint64_t storageTotalBytes() {
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    return (SD_MMC.cardType() == CARD_NONE) ? 0 : SD_MMC.cardSize();
#elif HAS_SD_CARD
    return (SD.cardType() == CARD_NONE) ? 0 : SD.cardSize();
#else
    return sMounted ? LittleFS.totalBytes() : 0;
#endif
}

const char *storageCardTypeName() {
#if HAS_SD_CARD
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    const uint8_t type = SD_MMC.cardType();
#else
    const uint8_t type = SD.cardType();
#endif
    switch (type) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        case CARD_NONE: return "";
        default:        return "unknown";
    }
#else
    // Internal flash: there is no card, so there is no class to report. The
    // caller prints storageName() instead.
    return "";
#endif
}

bool storageBegin() {
    if (sMounted) return true;

#if defined(HAS_SD_MMC) && HAS_SD_MMC
#if defined(DEVICE_WIO_TRACKER_L2)
    if (!wioTrackerL2IoReady() || !wioTrackerL2IoSetSdPower(true)) {
        Serial.println("[sd] SD_MMC power enable failed");
        return false;
    }
#endif
    delay(10);
    if (!SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0)) {
        Serial.printf("[sd] SD_MMC pin setup failed clk=%d cmd=%d d0=%d\n",
                      SDMMC_CLK, SDMMC_CMD, SDMMC_D0);
#if defined(DEVICE_WIO_TRACKER_L2)
        (void)wioTrackerL2IoSetSdPower(false);
#endif
        return false;
    }
    sMounted = SD_MMC.begin("/sdcard", /*mode1bit=*/true,
                            /*format_if_mount_failed=*/false);
    if (sMounted && SD_MMC.cardType() == CARD_NONE) {
        SD_MMC.end();
        sMounted = false;
    }
    if (sMounted) {
        Serial.printf("[sd] SD_MMC mounted: %llu MB, %llu MB used\n",
                      (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                      (unsigned long long)(SD_MMC.usedBytes() / (1024ULL * 1024ULL)));
    } else {
        Serial.printf("[sd] SD_MMC not found clk=%d cmd=%d d0=%d\n",
                      SDMMC_CLK, SDMMC_CMD, SDMMC_D0);
#if defined(DEVICE_WIO_TRACKER_L2)
        (void)wioTrackerL2IoSetSdPower(false);
#endif
    }
#elif HAS_SD_CARD
    // Card mounting is board-specific (shared SPI bus, expander rails, retry
    // ladders), so it stays where it always was — in config_io.cpp's sdBegin().
    // This path exists so storageBegin() is callable on every board.
    sMounted = SD.begin(SD_CS, SPI, 4000000);
    if (!sMounted) sMounted = SD.begin(SD_CS, SPI, 1000000);
#elif defined(HAS_INTERNAL_FS)
    // format-on-fail: a blank or corrupted partition is formatted once rather
    // than leaving the device with no storage until someone reflashes. The
    // partition is named in the board's partition table, not the default
    // "spiffs", so the label has to be passed explicitly.
    sMounted = LittleFS.begin(/*formatOnFail=*/true, "/littlefs",
                              /*maxOpenFiles=*/5, INTERNAL_FS_PARTITION);
    if (sMounted) {
        Serial.printf("[fs] %s mounted: %u KB used of %u KB\n",
                      storageName(),
                      (unsigned)(LittleFS.usedBytes() / 1024),
                      (unsigned)(LittleFS.totalBytes() / 1024));
    } else {
        Serial.printf("[fs] %s mount FAILED (partition '%s' missing?)\n",
                      storageName(), INTERNAL_FS_PARTITION);
    }
#endif
    return sMounted;
}
