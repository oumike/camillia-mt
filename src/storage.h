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

// Capacity of the mounted backend, in bytes; 0 when nothing is mounted. Read
// from what the driver already knows — the card's CSD, or the filesystem's own
// geometry — so this is cheap enough for a screen to ask on every open. Used
// bytes are deliberately not offered: on FAT that answer costs a walk of the
// allocation table, which is not something a status line should pay for.
uint64_t storageTotalBytes();

// Card class as the host negotiated it: "SDHC", "SDSC", "MMC", or "unknown"
// when the type is not one of those. Empty string on a backend where the
// question does not apply (internal flash), and when nothing is mounted.
const char *storageCardTypeName();

// Drops the mount: ends the backend and clears the mounted flag, so the next
// sdBegin() genuinely re-mounts instead of short-circuiting on a stale "already
// up". Paired with sdMarkUnmounted() in config_io.h, which clears the same
// answer on that side of the fence.
void storageUnmount();

#if HAS_SD_MALWARE_SCAN
// ── SD card malware scan ─────────────────────────────────────────────────────
// A user-run check for the files a Windows storage worm leaves on a card, and a
// confirmed delete of exactly those.
//
// This is not an antivirus and does not pretend to be one: no signature
// database, and no content inspection beyond the first two bytes of a file.
// What it recognises is the shape these worms travel in — an autorun file, a
// Windows program, a script, a shortcut — plus any file at all whose header says
// it is a Windows executable whatever its name claims.
//
// The reason a mesh radio is a sensible place to run it is that none of it can
// execute here. An ESP32 cannot run a Windows binary, so the device is a safe
// place to look at a card that is not safe to open anywhere else.
//
// See HAS_SD_MALWARE_SCAN in config.h for why this is scoped to one board.
struct StorageCardSuspect {
    char        path[64];   // full path, so a repair needs nothing else from the caller
    const char *why;        // static string; why this entry was flagged
};

// Called as the walk proceeds, so a caller can show progress and keep the panel
// alive through what is otherwise a long blocking read. `where` is the folder
// currently being walked.
//
// `total` is 0 during the counting pass. The walk counts the card first —
// readdir only, no file opens — because otherwise a progress bar would have
// nothing honest to measure against: the number of files is not knowable until
// something has been all the way through. Counting is a small fraction of the
// cost of the pass that follows, where each file is opened and read.
//
// That pass is skipped entirely when there is no callback to report to, and for
// a repair it is also skipped when the caller already knows the total. Counting
// is only ever for the bar; nothing about the result depends on it.
//
// Return false to abort. The walk stops where it is and returns what it had:
// for a scan that means an incomplete count, and for a repair it means the files
// already deleted stay deleted, which is why both callers report a cancelled run
// as cancelled rather than as a result.
typedef bool (*StorageScanProgressFn)(uint16_t done, uint16_t total,
                                      uint16_t found, const char *where);

// Walks the card and reports what matches. Returns the total number of matches,
// which may exceed `maxOut` — only the first `maxOut` are written to `out`.
// `scannedOut`, when given, receives the number of files examined.
//
// Bounded: it stops after a fixed depth and a fixed number of entries rather
// than walking a card someone has put a hundred thousand files on. Camillia's
// own /camillia tree is skipped — every file in it was written by this firmware.
uint16_t storageScanCard(StorageCardSuspect *out, uint8_t maxOut, uint16_t *scannedOut,
                         StorageScanProgressFn onProgress = nullptr);

// Deletes what a *fresh* walk recognises, and nothing else.
//
// Deliberately takes no list: it re-derives the targets itself rather than
// trusting paths handed in from a UI, so there is no call anywhere that can be
// pointed at map tiles, chat history or a backup. Directories are never removed.
// Returns the number deleted; `failedOut`, when given, receives the number that
// matched but could not be removed.
//
// `knownTotal` is the file count from the scan that produced this decision. The
// repair walks and re-classifies every file regardless -- that is the safety
// property, and it is untouched -- but with the count already in hand it does
// not need a pass of its own to rediscover it, which is otherwise a second full
// walk of the card between the user answering Yes and anything happening.
uint16_t storageRepairCard(uint16_t *failedOut,
                           StorageScanProgressFn onProgress = nullptr,
                           uint16_t knownTotal = 0);

// Rewrites the card's filesystem from scratch, then tears the mount down.
//
// This is the real thing — f_mkfs(), the same call the SD library itself makes
// when it formats a card it could not read — not a walk that deletes files. It
// therefore takes everything: transcripts, the node database, map tiles and the
// config export, not only what a scan matched. It is offered because it is the
// one answer that is certain, and the caller is expected to have said so.
//
// Returns with the card UNMOUNTED whether it succeeded or not, because after
// f_mkfs the VFS layer is still holding the old filesystem's FAT cache and
// anything that touched it would be writing against a map that no longer
// describes the card. The caller must call sdMarkUnmounted() and then
// sdBegin(true) — the board-specific mount lives there — before using the card
// again, and nothing may touch the filesystem in between.
//
// Returns true when the new filesystem was written.
bool storageFormatCard();
#endif
