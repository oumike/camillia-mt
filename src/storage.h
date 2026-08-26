#pragma once
// File storage backend.
//
// Boards use SPI SD, one-bit SD_MMC, or LittleFS depending on their wiring.
// Keep callers on fs::FS so backend-specific details stay in storageBegin().
//
// SDFS, SDMMCFS and LittleFSFS derive from fs::FS and expose identical open() /
// exists() / remove() / mkdir() / rmdir(), so callers go through storageFs()
// and never name a backend. Only mounting differs, and that is storageBegin()'s
// job.
#include <Arduino.h>
#include <FS.h>
// Must come before the backend tests below: those macros are defined by the
// board header this pulls in.
#include "config.h"

#if defined(HAS_SD_MMC) && HAS_SD_MMC
#  include <SD_MMC.h>
#elif HAS_SD_CARD
#  include <SD.h>
#else
#  include <LittleFS.h>
#endif

// True when this board can persist files at all, whichever backend it uses.
// Prefer this over HAS_SD_CARD for "can we save/load a file?" decisions —
// HAS_SD_CARD answers the narrower question of whether a card slot exists.
#if HAS_SD_CARD || defined(HAS_INTERNAL_FS)
#  define HAS_FILE_STORAGE 1
#else
#  define HAS_FILE_STORAGE 0
#endif

// The mounted filesystem. Safe to call before storageBegin(); operations on an
// unmounted filesystem simply fail, as they did with an absent SD card.
fs::FS &storageFs();

// Mounts the backend if it is not already up. Idempotent, and cheap to call
// repeatedly — the existing sdBegin() call sites do exactly that.
bool storageBegin();

// True once the backend is mounted.
bool storageMounted();

// Human-readable backend name for diagnostics and UI ("SD card", "internal
// flash"), so a message can say where a file actually went.
const char *storageName();
