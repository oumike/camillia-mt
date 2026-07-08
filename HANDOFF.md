# Handoff — camillia-mt (Meshtastic firmware) session

Written 2026-07-07 for a tool/assistant switch. This documents what was done, the
current **open problem** (a live-screen crash), and exactly how to continue.

---

## Project basics

- PlatformIO / Arduino-ESP32 firmware. LVGL UI (v8.3.11).
- Build envs (in `platformio.ini`): `tdeck`, `tlora-pager-tft`, `cardputer-cap`,
  `heltec-v4`, `heltec-v4-vertical`.
- **Only `cardputer-cap` has no PSRAM** (`src/hal/hw_cardputer.h`: `HAS_PSRAM 0`,
  LVGL pool `LV_MEM_SIZE = 96 KB`). All other boards have PSRAM and a 128 KB LVGL pool.
- Commands:
  - Build: `pio run -e <env>`
  - Flash: `pio run -e <env> -t upload`
  - Monitor: `pio device monitor -e <env>` (115200)
- Decode a crash backtrace:
  `~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -e .pio/build/<env>/firmware.elf -fpiC <addr> <addr> ...`

---

## Git state

- **HEAD = `4799abc` "Release v2.9.0"** already contains the finished work below
  (touched `channel_mgr.cpp/.h`, `web_config.cpp`, and part of `main_lvgl.cpp`).
- **Uncommitted working tree** = the *in-progress live-crash investigation only*:
  - `src/lv_conf.h` (LVGL mem-integrity debug toggle)
  - `src/main_lvgl.cpp` (live-screen diagnostics + guards)

---

## DONE and working (committed in v2.9.0)

Original problem: on the **no-PSRAM Cardputer**, the web config server got a Wi-Fi IP
but pages wouldn't load ("broken login, unresponsive"). Root cause was DRAM
exhaustion — by the time Wi-Fi is up there was ~9 KB free / ~3.8 KB largest block,
so building the ~4 KB `kHead` HTML string and per-node lists failed. Fixes (all
Cardputer-gated with `#if defined(DEVICE_CARDPUTER_LORA_HAT)` unless noted):

1. **Chat-buffer reclaim during web config** — `ChannelMgr::releaseBuffers()` /
   `restoreBuffers()` (`channel_mgr.cpp/.h`). `webCfgBegin()` frees the ~35 KB of
   channel line buffers (and `DMs.clearAll(false)`); `webCfgEnd()` reallocates +
   reloads (`restoreBuffers()`, then `DMs.clearAll(false); DMs.loadAll()` to avoid a
   double-load). `addMessage`/`getLine` already no-op on `lines == nullptr`, so
   freeing mid-session is safe.
2. **Stream `kHead` from flash** (`sendLoginPage`, `sendConfigPage`,
   `sendOnboardingPage`) via `server.sendContent_P(kHead)` instead of
   `String html = kHead` — avoids the ~4 KB contiguous alloc. Chunked responses are
   terminated with `server.sendContent("")`.
3. **Stream per-node data** in `sendConfigPage` — Cardputer no longer accumulates
   `nodeOptions`/`nodeDetails`/`mapPoints` (~30 KB for 64 nodes). It counts map
   points, then streams the option/detail/point lists in ~1 KB chunks
   (`streamNodeOptions`/`streamMapPoints`/`streamNodeDetails` lambdas). The PSRAM
   path is unchanged (`#if !defined(...)`).
4. **Boot-ordering fix** (`main_lvgl.cpp setup()`): on Cardputer, `startWebConfigAuto()`
   moved to the **end** of `setup()` (after `Channels.init()` etc.) so the reclaim
   actually has buffers to free. Other boards keep the early call. (Before this, boot
   auto-start reclaimed 0 bytes then got starved → 9 KB free → LWIP write failures.)
5. **Removed the web-config Chat tab on Cardputer** — tab button, panel HTML, chat
   JS, `switchTab` chat branch, `handleGetChatTargets/handleGetChatData/handlePostChatSend`,
   and their routes, all `#if !defined(DEVICE_CARDPUTER_LORA_HAT)`. Verified: zero
   `chat-targets`/`chat-data` strings in the Cardputer binary, still present in tdeck.
6. **`onNotFound` logger** (all boards, in `registerCommonRoutes`) — prints
   `[web] 404 <METHOD> <uri>` instead of the generic message.
7. **Onboarding SD-import channel persistence (ALL boards, not gated)** — bug fix in
   `onboardingAcceptImport()` (`main_lvgl.cpp`). It called `cfgImport()` (which fills
   both `s_cfg` **and** the global `CHANNEL_KEYS[]` plan) but only
   `persistConfigToPrefs()`, so imported channels were lost on reboot. Added
   `syncPrimaryChannelName(); persistChannelsToPrefs();` to mirror `onWebCfgSaved()`.
   **Confirmed working** by the user (imported → rebooted → channels restored).

Verification aids left in `web_config.cpp`: `logWifiHeapDiag()` calls print
`[web] ... [heap int free=.. largest=.. 8bit=..]` at pre-Wi-Fi, after reclaim,
server-up, and serving-login. Healthy Cardputer numbers after the fixes:
`reclaimed 35840 bytes`, login served at ~43 KB free / ~29 KB largest.

---

## OPEN PROBLEM — live-screen crash (the reason for the handoff)

**Symptom:** After the device has been up "for a bit," opening the on-device **Live**
screen crashes and reboots. Happens on **every board** (user is currently testing on
**T-Deck**, which has PSRAM), **intermittently**.

**This is NOT memory exhaustion.** The instrumentation proves it — at the crash the
LVGL pool was healthy:
```
[lvgl] openLiveModal pool used=22% free=103372 biggest=90428 frag=13%
Guru Meditation Error: Core 1 panic'ed (StoreProhibited).
EXCVADDR: 0x000005e0   (write to a near-null address)
Backtrace decodes to:
  lv_obj_class_create_obj (lvgl/src/core/lv_obj_class.c)
    <- lv_label create/event
    <- refreshLiveView()      (src/main_lvgl.cpp)
    <- openLiveModal()        (src/main_lvgl.cpp)
    <- loop()
```
Interpretation: **LVGL's memory pool free-list is corrupted** — `lv_mem_alloc`
returns a garbage pointer, and the subsequent object init writes to ~`0x5e0`
(StoreProhibited). The live screen is the *victim* (largest allocation, up to 64
wrapped labels), not the cause. The corrupting write happens **elsewhere, earlier**.

**Already audited and ruled OUT (all bounds-safe):**
- `renderEmojiSafeText` + `appendTextLiteral` (emoji→alias expansion) — bounded to `dstLen`.
- `formatLiveLineText` — all `sscanf` field widths and `snprintf` are bounded.
- Live chart writes — `lv_chart_set_point_count(60)` is called **before** the series
  are added and the `set_value_by_id` loop stays in `[0,60)`.
- `ChannelMgr::getLine` — index math is guarded, `% MAX_MSG_LINES`.
- Screen sleep/wake — only calls `displayDev().setBrightness()`, does **not** tear
  down LVGL objects or the display, so the "[screen] woke" correlation is incidental
  (any interaction wakes the screen).

**Two earlier wrong turns (so you don't repeat them):**
- Pool-*exhaustion* theory was wrong (pool is at 22%). The `openLiveModal`/
  `refreshLiveView` low-memory guards added for it are harmless but do NOT fix this.
- Sleep/wake is not the cause.

### Current uncommitted diagnostic build (what's flashed now)

- `src/main_lvgl.cpp`:
  - `logLvglMemDiag(const char*)` helper (+ forward decl near the other prototypes);
    called at the top of `openLiveModal()`.
  - Pool guards: `refreshLiveView` breaks the label loop if `free_biggest_size < 3072`;
    `openLiveModal` aborts if `< 6144`. **These do not fire here (pool healthy) and are
    not the fix** — they were the exhaustion theory. Keep or remove at your discretion.
- `src/lv_conf.h` — **the important part**:
  ```c
  #define LV_USE_ASSERT_MEM_INTEGRITY 1
  #define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
  #define LV_ASSERT_HANDLER abort();
  ```
  This validates the LVGL pool on **every alloc/free** and `abort()`s the instant it
  finds corruption — i.e. at the **next LVGL op after the corrupting write**, giving a
  backtrace near the real culprit. NOTE: it noticeably slows the UI; it is a temporary
  debug toggle — remove once fixed. (Do not put `Serial`/C++ in `LV_ASSERT_HANDLER`;
  LVGL compiles as C — that was already tried and broke the build.)

### Next steps for whoever picks this up

1. Flash the current tree to the crashing board: `pio run -e tdeck -t upload && pio device monitor -e tdeck`.
2. Use it normally until the mem-integrity assert fires (`abort()` → `Guru Meditation`
   / backtrace). It should now trap **earlier and elsewhere** than the live screen —
   that new location is the clue.
3. Decode the backtrace with `addr2line` (command above) against the exact
   `.pio/build/tdeck/firmware.elf` that was flashed (rebuilding changes only the build
   timestamp, so line numbers stay valid for the same source).
4. The culprit is whatever writes past an LVGL-allocated buffer or uses a freed/dangling
   `lv_obj`. Prime suspects still worth checking that were NOT yet audited:
   - Repeated `lv_obj_clean` + rebuild churn in list views (`refreshLiveView`,
     `refreshChatView`, nodes) racing with a pending async layout/anim on a
     just-deleted child (single-threaded, but `refreshX` runs between
     `lv_timer_handler` calls).
   - Any `lv_canvas`/`lv_img`/PNG (`LV_USE_PNG 1`) buffer draw (e.g. `drawCamelliaMark`
     splash/flower) writing out of bounds.
   - Any place a static buffer adjacent to the LVGL pool arena could overflow.
5. Once found and fixed, **revert the `lv_conf.h` debug block** (and optionally the
   `logLvglMemDiag`/guard scaffolding in `main_lvgl.cpp`), then rebuild all envs.

### Useful context for reproduction
- The crash needs uptime ("a bit") — corruption accumulates. Note any specific action
  right before a trap (screen opened, packet type received) — that usually pins it.
- Backtraces on ESP32-S3: the last frame is often stack garbage (ignore it).

---

## Quick file map

- `src/web_config.cpp` — web config server (all the DONE web work; heap diagnostics).
- `src/channel_mgr.cpp/.h` — channel/message ring buffers; `releaseBuffers/restoreBuffers`.
- `src/dm_mgr.cpp/.h` — DM conversations (`DMs` instance; `clearAll`, `loadAll`).
- `src/main_lvgl.cpp` — LVGL UI + main loop + `setup()`; onboarding
  (`onboardingAcceptImport`), live screen (`openLiveModal`/`refreshLiveView`), charts,
  and the current live-crash diagnostics.
- `src/lv_conf.h` — LVGL config incl. the temporary mem-integrity debug toggle.
- `src/hal/hw_*.h` — per-board pins/flags (`HAS_PSRAM`, `HAS_SD_CARD`, ...).
