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

void storageUnmount() {
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    SD_MMC.end();
#elif HAS_SD_CARD
    SD.end();
#else
    LittleFS.end();
#endif
    sMounted = false;
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

#if HAS_SD_MALWARE_SCAN
// FatFs directly, for f_mkfs(). The Arduino SD wrapper exposes no format, and
// esp_vfs_fat_sdcard_format() cannot help here either: the SPI-SD path registers
// its own ardu_sdcard_t with FatFs rather than going through
// esp_vfs_fat_sdspi_mount(), so there is no sdmmc_card_t to hand it. fatfs/src
// is on the include path already -- the SD library is built against it.
#include "ff.h"

namespace {

// Bounds. The walk is user-initiated rather than automatic, so it can afford to
// be thorough — but not unbounded: a card can hold a hundred thousand map tiles,
// and the header check below costs an open and a read apiece.
constexpr uint8_t  kScanMaxDepth   = 4;
constexpr uint16_t kScanMaxEntries = 4000;

// Ours. Every file under it was written by this firmware, so walking it would
// spend most of the budget confirming what we already know, and a repair has no
// business anywhere near it.
constexpr const char *kOwnTree = "/camillia";

// The same question storageTotalBytes() asks, and for the same reason: on the
// SPI-SD boards sMounted is never set, because sdBegin() in config_io.cpp does
// the mounting. Testing the flag here reported "no card" on a board with a card
// in it -- which is exactly the trap the comment above storageTotalBytes()
// warns about.
bool cardPresent() {
#if defined(HAS_SD_MMC) && HAS_SD_MMC
    return SD_MMC.cardType() != CARD_NONE;
#else
    return SD.cardType() != CARD_NONE;
#endif
}

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool equalsNoCase(const char *a, const char *b) {
    for (;; a++, b++) {
        if (lowerAscii(*a) != lowerAscii(*b)) return false;
        if (!*a) return true;
    }
}

// The file's extension without the dot, lower-cased, or "" when it has none.
void extOf(const char *name, char *buf, size_t bufLen) {
    buf[0] = '\0';
    const char *dot = nullptr;
    for (const char *p = name; *p; p++) {
        if (*p == '.') dot = p;
    }
    if (!dot || !dot[1]) return;
    size_t n = 0;
    for (const char *p = dot + 1; *p && n + 1 < bufLen; p++) {
        buf[n++] = lowerAscii(*p);
    }
    buf[n] = '\0';
}

// Windows program, script and shortcut extensions. Not exhaustive — it cannot
// be — which is why the header check runs as well.
bool isWindowsExecutableExt(const char *ext) {
    static const char *const kExts[] = {
        "exe", "pif", "com", "scr", "bat", "cmd", "cpl", "msi",
        "vbs", "vbe", "js",  "jse", "wsf", "wsh", "hta",
    };
    for (const char *e : kExts) {
        if (equalsNoCase(ext, e)) return true;
    }
    return false;
}

// "MZ" — the DOS header every Windows PE still opens with, thirty years on.
// This is what catches a dropper renamed to something harmless-looking, which
// an extension list never will.
bool hasDosExecutableHeader(fs::FS &fs, const char *path) {
    File f = fs.open(path, FILE_READ);
    if (!f) return false;
    if (f.isDirectory() || f.size() < 2) { f.close(); return false; }
    uint8_t magic[2] = {0, 0};
    const size_t got = f.read(magic, sizeof(magic));
    f.close();
    return got == sizeof(magic) && magic[0] == 'M' && magic[1] == 'Z';
}

// name() is a full path on some cores and a bare name on others.
const char *baseNameOf(const char *nm) {
    const char *base = nm;
    for (const char *p = nm; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

// The one place that decides whether a file is a target. Scan and repair both
// go through it, so what a repair deletes is exactly what the scan showed —
// they cannot drift apart into a dialog that lists one thing and removes
// another. Returns nullptr for anything that is not a match.
const char *classify(fs::FS &fs, const char *path, const char *base) {
    if (equalsNoCase(base, "autorun.inf")) {
        return "autorun file - hijacks what opening the card does on Windows";
    }
    char ext[8];
    extOf(base, ext, sizeof(ext));
    if (equalsNoCase(ext, "lnk")) {
        return "Windows shortcut - these masquerade as folders";
    }
    if (isWindowsExecutableExt(ext)) {
        return "Windows program or script";
    }
    // Last, because it is the only check that costs a read.
    if (hasDosExecutableHeader(fs, path)) {
        return "Windows program under a harmless-looking name";
    }
    return nullptr;
}

struct WalkCtx {
    StorageCardSuspect *out;
    uint8_t             maxOut;
    uint8_t             written;
    uint16_t            found;
    uint16_t            scanned;
    bool                removing;
    uint16_t            removed;
    uint16_t            failed;
    // Counting pass: readdir only, no classify and no file opens. It exists so
    // the pass after it has a denominator to report progress against.
    bool                counting;
    uint16_t            total;      // 0 while counting
    StorageScanProgressFn onProgress;
    bool                aborted;    // the caller asked to stop
};

void walk(fs::FS &fs, const char *dirPath, uint8_t depth, WalkCtx &ctx) {
    if (depth > kScanMaxDepth || ctx.aborted) return;

    File dir = fs.open(dirPath);
    if (!dir) return;
    if (!dir.isDirectory()) { dir.close(); return; }

    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (ctx.aborted || ctx.scanned >= kScanMaxEntries) { f.close(); break; }

        const char *nm = f.name();
        if (!nm || !nm[0]) { f.close(); continue; }

        // Copied out before the close, not borrowed across it: name() hands back
        // a pointer into the File's own storage, and closing it takes that with
        // it. Everything below happens after the close — the header check
        // reopens by path, and the recursion opens its own handle — so nothing
        // may still be pointing at this one.
        char base[40];
        snprintf(base, sizeof(base), "%s", baseNameOf(nm));
        const bool isDir = f.isDirectory();
        f.close();

        char path[64];
        if (dirPath[1] == '\0') {   // "/" — no separator to add
            snprintf(path, sizeof(path), "/%s", base);
        } else {
            snprintf(path, sizeof(path), "%s/%s", dirPath, base);
        }

        if (isDir) {
            if (equalsNoCase(path, kOwnTree)) continue;
            // Handles held open across recursion are what exhausts the driver's
            // file table on a deep card, so this one is already closed.
            walk(fs, path, (uint8_t)(depth + 1), ctx);
            continue;
        }

        ctx.scanned++;
        if (ctx.counting) {
            // Cheap enough to report on every entry; the caller throttles.
            if (ctx.onProgress && !ctx.onProgress(ctx.scanned, 0, 0, dirPath)) {
                ctx.aborted = true;
            }
            continue;
        }

        const char *why = classify(fs, path, base);
        if (ctx.onProgress && !ctx.onProgress(ctx.scanned, ctx.total, ctx.found, dirPath)) {
            ctx.aborted = true;
        }
        if (!why) continue;

        ctx.found++;
        if (ctx.removing) {
            if (fs.remove(path)) {
                ctx.removed++;
                Serial.printf("[sd-scan] removed %s\n", path);
            } else {
                ctx.failed++;
                Serial.printf("[sd-scan] REMOVE FAILED %s\n", path);
            }
            continue;
        }
        if (ctx.out && ctx.written < ctx.maxOut) {
            snprintf(ctx.out[ctx.written].path, sizeof(ctx.out[ctx.written].path),
                     "%s", path);
            ctx.out[ctx.written].why = why;
            ctx.written++;
        }
        Serial.printf("[sd-scan] %s - %s\n", path, why);
    }
    dir.close();
}

}  // namespace

uint16_t storageScanCard(StorageCardSuspect *out, uint8_t maxOut, uint16_t *scannedOut,
                         StorageScanProgressFn onProgress) {
    if (scannedOut) *scannedOut = 0;
    if (!cardPresent()) return 0;

    const uint32_t startMs = millis();

    // Pass one: how many files are there. Nothing is opened, so this is a small
    // fraction of what pass two costs, and it is what lets the bar mean
    // something other than "still going".
    //
    // Only run when there is a bar. Web config calls this with no callback and
    // would otherwise pay for a second full walk of the card to produce a number
    // nobody ever sees.
    uint16_t total = 0;
    if (onProgress) {
        WalkCtx counting{};
        counting.counting = true;
        counting.onProgress = onProgress;
        walk(storageFs(), "/", 0, counting);
        if (counting.aborted) {
            Serial.println("[sd-scan] cancelled while counting");
            return 0;
        }
        total = counting.scanned;
    }

    WalkCtx ctx{};
    ctx.out = out;
    ctx.maxOut = out ? maxOut : 0;
    ctx.total = total;
    ctx.onProgress = onProgress;

    walk(storageFs(), "/", 0, ctx);
    if (scannedOut) *scannedOut = ctx.scanned;

    Serial.printf("[sd-scan] %u file%s examined in %lu ms, %u match%s\n",
                  (unsigned)ctx.scanned, (ctx.scanned == 1) ? "" : "s",
                  (unsigned long)(millis() - startMs),
                  (unsigned)ctx.found, (ctx.found == 1) ? "" : "es");
    return ctx.found;
}

namespace {

// Which FatFs volume the card is mounted as. Probed rather than assumed: the
// drive number is the SD library's private _pdrv, and while it is 0 in practice
// here -- nothing else in this firmware mounts a FatFs volume -- formatting the
// wrong drive is not the kind of thing to leave to "in practice". f_getfree()
// only answers for a mounted volume, so a reply is also confirmation that this
// is the drive to write.
int mountedFatDrive() {
    for (int pdrv = 0; pdrv < FF_VOLUMES; pdrv++) {
        char path[4] = { (char)('0' + pdrv), ':', '\0', '\0' };
        DWORD clusters = 0;
        FATFS *fs = nullptr;
        if (f_getfree(path, &clusters, &fs) == FR_OK) return pdrv;
    }
    return -1;
}

}  // namespace

bool storageFormatCard() {
    if (!cardPresent()) {
        Serial.println("[sd-format] no card");
        return false;
    }

    const int pdrv = mountedFatDrive();
    if (pdrv < 0) {
        Serial.println("[sd-format] no mounted FatFs volume to format");
        return false;
    }

    BYTE *work = (BYTE *)malloc(FF_MAX_SS);
    if (!work) {
        Serial.println("[sd-format] out of memory for the mkfs work buffer");
        return false;
    }

    // FM_ANY, exactly as the SD library's own format path uses: FatFs picks FAT,
    // FAT32 or exFAT to suit the card's size rather than us guessing wrong on
    // either end of the range. Zero cluster size means "choose for me" too.
    //
    // f_mkfs() changed shape between FatFs revisions and both are in play
    // depending on which core is installed: 86604 (R0.13c, what the Arduino
    // framework bundles today) takes the format byte and the cluster size as
    // separate arguments, while the newer one passes a MKFS_PARM struct. The
    // revision ID is the only thing that tells them apart at compile time.
    char path[4] = { (char)('0' + pdrv), ':', '\0', '\0' };
    Serial.printf("[sd-format] f_mkfs on drive %s (FatFs rev %d)\n",
                  path, (int)FF_DEFINED);
#if FF_DEFINED == 86604
    const FRESULT res = f_mkfs(path, (BYTE)FM_ANY, 0, work, FF_MAX_SS);
#else
    const MKFS_PARM opt = { (BYTE)FM_ANY, 0, 0, 0, 0 };
    const FRESULT res = f_mkfs(path, &opt, work, FF_MAX_SS);
#endif
    free(work);

    // Down either way, and immediately. On success the cached FAT describes a
    // filesystem that no longer exists; on failure the card is in whatever state
    // a half-written mkfs left it. Neither is safe to keep serving reads from.
    storageUnmount();

    if (res != FR_OK) {
        Serial.printf("[sd-format] f_mkfs failed (%d)\n", (int)res);
        return false;
    }
    Serial.println("[sd-format] filesystem rewritten");
    return true;
}

uint16_t storageRepairCard(uint16_t *failedOut, StorageScanProgressFn onProgress,
                           uint16_t knownTotal) {
    if (failedOut) *failedOut = 0;
    if (!cardPresent()) return 0;

    // The count comes from the scan that put the question on screen, so this
    // normally does no counting at all -- pressing Yes gets straight on with it
    // rather than walking the whole card again first. The fallback is for a
    // caller that has a bar but no count to give it.
    uint16_t total = knownTotal;
    if (onProgress && total == 0) {
        WalkCtx counting{};
        counting.counting = true;
        counting.onProgress = onProgress;
        walk(storageFs(), "/", 0, counting);
        if (counting.aborted) {
            Serial.println("[sd-scan] repair cancelled while counting");
            return 0;
        }
        total = counting.scanned;
    }

    WalkCtx ctx{};
    ctx.removing = true;
    ctx.total = total;
    ctx.onProgress = onProgress;

    walk(storageFs(), "/", 0, ctx);
    if (failedOut) *failedOut = ctx.failed;

    Serial.printf("[sd-scan] repair: %u removed, %u failed\n",
                  (unsigned)ctx.removed, (unsigned)ctx.failed);
    return ctx.removed;
}
#endif  // HAS_SD_MALWARE_SCAN
