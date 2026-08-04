#include "storage.h"
#include "config.h"

#if HAS_SD_CARD
#include <SPI.h>
#endif

namespace {
bool sMounted = false;
}

fs::FS &storageFs() {
#if HAS_SD_CARD
    return SD;
#else
    return LittleFS;
#endif
}

bool storageMounted() { return sMounted; }

const char *storageName() {
#if HAS_SD_CARD
    return "SD card";
#else
    return "internal flash";
#endif
}

bool storageBegin() {
    if (sMounted) return true;

#if HAS_SD_CARD
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
