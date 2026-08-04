#pragma once
// File storage backend.
//
// Every board until now kept its files on a microSD card, so the code called
// SD.* directly. The Mesh Deck has no card slot but does have 16 MB of flash,
// most of it unallocated, so it keeps the same files in a LittleFS partition
// instead.
//
// Both SDFS and LittleFSFS derive from fs::FS and expose identical open() /
// exists() / remove() / mkdir() / rmdir(), so callers go through storageFs()
// and never name a backend. Only mounting differs, and that is storageBegin()'s
// job.
#include <Arduino.h>
#include <FS.h>
// Must come before the HAS_SD_CARD test below: that macro is defined by the
// board header this pulls in. Without it the test silently evaluates to 0 and
// an SD board compiles the LittleFS branch — which fails at the first use of
// SD, and would have picked the wrong backend if it had not.
#include "config.h"

#if HAS_SD_CARD
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
