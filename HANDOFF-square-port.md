# Handoff — `square` build target (issue #56)

Working notes for whoever picks this up next. Written 2026-08-26.

**Read this first, then issue #56.** Several of #56's open questions are already
answered below; re-deriving them is wasted effort.

## Continuation update — 2026-08-26

This section supersedes the stale working-tree snapshot and next-step status
below. The work remains uncommitted.

Completed in this continuation:

- Added `src/hal/square_io.{h,cpp}` on top of the existing XL9555-compatible
  driver, compiled only in `[env:square]`.
- Expander boot now starts I2C at SDA 47 / SCL 48, reads the fixed address
  `0x21`, stages output latches before direction changes, follows the upstream
  LCD/touch/GNSS reset sequence, drives LCD control bit 4 high after reset, and
  logs the resulting shadows.
- Added named Square controls for LCD/touch resets and LCD, Grove, GNSS, LED,
  USB OTG, audio PA, SD and battery-sense power.
- Routed GNSS power/reset through expander bits 13/9 for start, disable and
  re-enable. Square now actually probes the swapped UART pair as §2 says.
- Added `UI_TOUCH_ONLY_PROFILE` for Heltec + Square. The 155 touch-only UI
  conditions in `main_lvgl.cpp` now use it; Heltec memory, sensor rollback,
  storage exclusion, onboarding, vertical and compact-selector conditions stay
  device-specific. `keyboard.cpp` now uses `HAS_KEYBOARD` rather than Heltec
  identity for no-keyboard behavior.
- Enabled the existing VNC Host/browser Remote feature for Square. It uses the
  board's 8 MB PSRAM for the 320x240 RGB565 frame copy. BLE remains explicitly
  disabled; Remote keyboard/pointer input is provided through VNC instead.
- Made LovyanGFX source drift and partial patch states fatal in
  `tools/patch_lgfx_dmadesc.py`; the fresh-checkout "library not fetched yet"
  retry remains non-fatal.
- Added `--square`, the interactive build menu entry, hardware-test array entry
  and CI build. `release.sh` explicitly keeps Square out pending hardware
  acceptance.
- Updated README and build/hardware docs using only the public `square`
  codename and marked it as a bring-up target with no release artifact.
- Researched the user-supplied upstream Meshtastic branch. It resolves the
  former hardware-reference blockers without needing a private schematic:
  - shared I2C runs at 100 kHz;
  - the panel uses GPIO 46 as its LovyanGFX CS while expander bit 4 is a
    separate output held high after LCD reset;
  - LP5814 registers are `0x00/01/02/04/05/0F`, DC `0x14..0x17`, PWM
    `0x18..0x1B`, with update value `0x55`;
  - ADS1115 battery input is AIN0 with `GAIN_TWO` and a confirmed 2:1 divider;
  - GNSS starts MCU-side RX 18 / TX 17, and reset is active-high/released low;
  - native USB upload uses the 1200-bps touch reset and waits for re-enumeration.
- Implemented the LP5814 `ILight`, post-GT911 I2C recovery, ADS1115 battery
  path, corrected GNSS direction/reset polarity and upstream upload behavior.
- Set Square's logical display rotation to 0. With the sourced panel offset of
  1, LovyanGFX resolves this to internal rotation 1 (320x240), rotating the UI
  left 90 degrees from the previous internal rotation 2.
- Square text entry is on-screen-only. All five reachable textarea flows share
  a full-width keyboard layout; the target explicitly keeps `HAS_BLE_KEYBOARD`
  at 0 and has no NimBLE/`ble_keyboard.cpp` build entries.
- The top Wake button reads expander bit 0 through the GPIO45 interrupt: a press
  wakes immediately, while a two-second hold when awake shuts the screen off.
- Enabled the card as a one-bit SD_MMC backend on CLK 2 / CMD 3 / D0 1. Mounting
  powers expander bit 14, calls `SD_MMC.setPins()`, and uses `/sdcard` without
  format-on-failure. Square now uses `partitions.csv`; app slots and NVS remain
  at the same offsets as the former LittleFS table.
- All existing SD feature gates are active for Square: firmware config
  Export/Import at `/camillia/config.yaml`, first-boot import detection,
  channel/DM persistence, node archive, state maps, discovery snapshots and web
  storage operations.
- Fixed wake artifacts caused by `Panel_NV3031B::setSleep(false)`: LovyanGFX
  software-resets and reinitializes this controller on wake, which invalidates
  panel RAM. Square now invalidates the entire LVGL screen while the LP5814 is
  dark, flushes the rebuilt frame, waits for the panel bus, then restores
  brightness.

Validation performed in this continuation:

- VS Code C/C++/INI/YAML/Markdown diagnostics: no errors in touched files.
- Pylance syntax validation for `tools/patch_lgfx_dmadesc.py`: no errors.
- `pio project config --lint`: clean.
- `bash -n` on the build/test/release/backup/flash scripts: clean.
- `git diff --check`: clean.
- **No PlatformIO build or hardware operation was run.** Repo instructions and
  user preference require an explicit request before `pio run`; the last 8/8
  result below predates this continuation.

The reference-fact blockers are resolved. Remaining work is executable
validation: build, flash, inspect serial logs, sweep brightness, compare battery
voltage against a meter, and test touch corners/radio/GNSS/SD. Audio remains a
separate follow-up.

---

## 0. Confidentiality — read before writing anything public

The `square` hardware is unreleased and its identity is under embargo, so:

- **Use the codename `square` everywhere.** Env, macros, filenames, artifacts,
  commit messages, issue comments, docs.
- The product name, vendor and the user's tester status must not appear in the
  repo, in commits, or in GitHub comments. **This repo is public** (`oumike/camillia-mt`).
- Issue #56 already discloses the pin map, panel part number and expander bit
  map — that was the user's own considered call. Do not widen it.
- Ask the user if you need the identity for a technical reason; do not infer it
  into a file.

`backups/` is gitignored (it holds a flash dump of the vendor's pre-release
firmware plus a node identity key). Keep it that way.

---

## 1. Where things stand

**HEAD is `50fb913` "Release v4.6.0"** — that commit contains the completed BLE
keyboard feature (issue #40), which is *done and committed*. See §5.

**The `square` port is in progress and uncommitted.** Working tree:

```
 M platformio.ini      # [env:square]
 M src/config.h        # DEVICE_SQUARE guard, MESH_HW_MODEL_SQUARE 137
 M src/hal/board.h     # include branch + capability macros
 M src/hal/display.h   # Panel_NV3031B + quad-SPI bus branch
?? boards/square.json
?? src/hal/hw_square.h # 260 lines, full pin map
```

**All 8 targets build clean** (`pio run`), `square` included:
`square` = 84.4% flash (2,764,617 / 3,276,800), 44.6% RAM. Most headroom of any
board — it carries neither NimBLE (Heltec) nor the audio driver (Pager).

That satisfies the first acceptance criterion on #56. Nothing has been run on
hardware yet.

---

## 2. Findings that close open questions on #56

Do not re-investigate these. Each was verified against the pinned toolchain, not
assumed.

### Risk #1 (LovyanGFX) is much smaller than the ticket feared

- **`Panel_NV3031B` exists in 1.2.27** — `src/lgfx/v1/panel/Panel_NV3031B.hpp`.
- **There is no separate `Bus_QSPI` class, and none is needed.** 1.2.27 drives
  quad through the *same* `lgfx::Bus_SPI`: `config_t` gained
  `pin_io0..pin_io3` and an internal `_is_quad_spi` flag
  (`platforms/esp32/Bus_SPI.hpp:84-88`). Setting the four IO pins is what
  selects quad mode. So `display.h`'s existing `lgfx::Bus_SPI _bus` member is
  unchanged — only the config block differs. Already implemented.
- **`tools/patch_lgfx_dmadesc.py` applies cleanly to 1.2.27.** Both `OLD_ALLOC`
  and `OLD_SETUP` match verbatim; confirmed by running it
  (`[patch_lgfx_dmadesc] patched LovyanGFX Bus_SPI DMA descriptor handling`).
  The ticket's worry about version drift does not bite at this version.
  Note the script *warns* but does not fail the build on drift — #56 asks for it
  to fail loudly. Still outstanding if you want that hardened.
- Per-env libdeps isolation works as expected: `[env:square]` pins
  `lovyan03/LovyanGFX@1.2.27`, the other six envs stay on `^1.1.16`, and all
  eight build.

### Both GPS open questions are already handled by existing code

`gps.cpp` has a probe ladder that walks **baud and swapped pins**:

- `GPS_BAUD_PROBE_LIST` = 9600 / 38400 / 115200 (`gps.cpp:111`)
- `GPS_PORT_PROBE_LIST` includes `{ GPS_TX, GPS_RX }` (`gps.cpp:128`) — i.e. the
  reversed pair.

The upstream GPS setup passes its variant macros directly to
`HardwareSerial::begin(..., rx, tx)`, establishing MCU-side RX 18 / TX 17.
`hw_square.h` now starts with that pair and retains the reversed probe as a
fallback.

### SD decision — settled, see the comment posted on #56

`storage.h` already abstracts to `fs::FS &`; no caller names a backend. Adding
SDIO is three compile-time branches in one file. `SD_MMC.setPins(clk, cmd, d0)`
exists in Arduino core 2.0.17 (`libraries/SD_MMC/src/SD_MMC.h:51`) and `begin()`
takes a `mode1bit` flag, so the board's non-default 1-bit pins are supported.

The expander dependency is now implemented, so Square uses `HAS_SD_CARD 1` and
the one-bit SD_MMC backend. Mounting enables power on bit 14 before configuring
CLK 2 / CMD 3 / D0 1.

There is **no lock-in**: `partitions_16mb_fs.csv` is byte-identical to
`partitions.csv` through `coredump`, `nvs` included at 0x650000 (its own header
says so). Moving LittleFS → SD later preserves settings and node identity.

---

## 3. Source-resolved hardware details

The Meshtastic branch carries working implementations rather than only pin
declarations. Camillia now follows these details:

- LP5814: chip enable `0x00=0x01`, max current `0x01=0x01`, outputs disabled at
  `0x02=0x00`, dim mode `0x04=0x4E`, engine mode `0x05=0xF0`, four DC values of
  200 at `0x14..0x17`, outputs enabled with `0x02=0x0F`, and `0x0F=0x55` to
  latch. Brightness writes all four PWM registers `0x18..0x1B`.
- Display init: initialize LP5814 before the base LGFX init, let GT911 probe,
  then reset `Wire` and restore 100 kHz before later brightness writes.
- LCD controls: the NV3031B bus config uses GPIO 46 as `pin_cs`. Expander bit 4
  is still configured as an output and driven high after the LCD reset sequence.
- ADS1115: address `0x48`, single-ended AIN0, `GAIN_TWO`, three samples, and
  multiply by the 2:1 divider. Camillia gates bit 15 only around ADC activity
  and lets its existing battery filter do the smoothing.

These are source-verified but **not hardware-verified in Camillia**. Do not call
the acceptance criteria complete until serial and measured checks pass.

---

## 4. Current implementation and next checks

### PCA9555 expander — implemented

**Reuse `src/hal/xl9555.cpp`, do not write a new driver.** The XL9555 is
register-compatible with PCA9555/TCA9555 (its own header says so), the register
map is identical, and the implementation is generic — it talks to the global
`Wire` with no pager-specific assumptions.

`square_io.cpp` uses the fixed address and shadowed output/config bytes. The
source-backed bring-up state is:

| Bit | Name | Boot state |
| --- | --- | --- |
| 0,1,2 | wake btn, I²C INT, SD detect | inputs |
| 3 | touch INT | output low; controller is polled |
| 4 | LCD control | output high after LCD reset |
| 5 | LCD power | on |
| 6 | LCD RST | high → low → high |
| 7 | Grove power | off |
| 8 | touch RST | low → high after LCD setup |
| 9 | GNSS RST | high (assert) → low (release) |
| 10 | user LED | off |
| 11 | USB OTG | off |
| 12 | audio PA | off (no audio driver) |
| 13 | GNSS power | on |
| 14 | SD power | enabled when SD_MMC mounts |
| 15 | battery sense | off; enable only around a read |

### LP5814 `ILight` — implemented, needs brightness sweep

### ADS1115 battery — implemented, needs meter comparison

The Square environment pins `Adafruit ADS1X15@2.6.2`. The reader uses AIN0,
`GAIN_TWO`, three samples and the 2:1 divider, with a 30-second hardware cache.
Bit 15 is enabled only around probe/read activity. Validate at low, mid and full
charge; the upstream gain clips nominal inputs above 2.048 V (4.096 V after the
2x scale), so the top of the charge curve deserves particular attention.

### Touch UI profile — implemented, needs device walk

Square now shares `UI_TOUCH_ONLY_PROFILE` with Heltec while hardware-specific,
vertical, compact-selector and low-memory paths stay separate.

### Scripts and docs — implemented

Square is in local selection, the hardware-test list and CI, but intentionally
not in `RELEASE_ENVS`.

### Remaining feature follow-up

- Add ES8311/ES7243E audio only if tone/microphone support is desired.

---

## 5. Context: BLE keyboard (issue #40) — done, committed in v4.6.0

Shipped on the **Heltec envs only** (`heltec-v4`, `heltec-v4-vertical`).
`HAS_BLE_KEYBOARD` in `hal/board.h` is the whole gate; NimBLE and
`ble_keyboard.cpp` are in those two envs' `lib_deps` / `build_src_filter` only.

The one thing to know if you touch it: **Web Config and the BLE keyboard are
mutually exclusive, enforced in both directions with an on-screen dialog**
(`main_lvgl.cpp:4158`, `bleKbdStopWebConfigForBt()` /
`webCfgStopBleKeyboardForWeb()`). This is not a preference — Web Config sets
`WiFi.setSleep(false)` (`WIFI_PS_NONE`) and Espressif's Wi-Fi/BT coexistence
requires modem sleep. Starting the BT controller in that state **aborts inside
`coex_core_enable()` and reboots the device**; it is not a catchable error. This
was hit on real hardware and diagnosed from the backtrace. Do not "simplify" that
exclusion away.

Untested on hardware: nobody has paired an actual keyboard yet.

---

## 6. Hardware on the bench — two boards, easy to confuse

They swap on the same port (`/dev/cu.usbmodem101`). Tell them apart by MAC or
PSRAM size — `esptool.py --port ... flash_id` prints both:

| MAC | PSRAM | What it is |
| --- | --- | --- |
| `68:ee:8f:51:8f:68` | 8 MB | the **square** board, running vendor Meshtastic 2.7.25 |
| `90:70:69:85:5f:94` | 2 MB | the **Heltec** Camillia board, node `!69855f94` |

### Backups

- `backups/68ee8f518f68-20260826-085855/` — **complete, verified** full-flash
  backup of the square board's vendor firmware. All six files match their
  MANIFEST sha256; bootloader/partition-table/app magics valid; flash is
  unencrypted so a byte-for-byte restore reproduces it exactly. **Take this
  seriously — it is the only way back to the vendor firmware.**
- `backups/68ee8f518f68-20260826-101032/` — junk, partial (partition table only)
  from an aborted run. Safe to delete.
- **The Heltec board has NOT been backed up.** Worth doing before flashing.

### Two traps in `backup.sh`

1. **Restore does not check the MAC.** It checks flash size and demands a typed
   `RESTORE`, but both boards are 16 MB ESP32-S3, so restoring the square
   backup onto the Heltec would succeed and overwrite its node identity. Worth
   adding a MANIFEST-MAC guard mirroring the existing flash-size check.
2. **It exits 0 even when a device fails** ("Backed up: 0 failed: 1", exit code
   0). Anything scripting it will not notice.

### Serial port

A `pio device monitor` / `pio run -t monitor` holds the port exclusively and
makes esptool fail with `Errno 35 Resource temporarily unavailable`. Stop it
first. The user's monitor is currently **stopped** — restart with
`pio run -e heltec-v4 -t monitor`.

---

## 7. Small gotchas found along the way

- **`#if (BOARD_VEXT_ENABLE >= 0)` with the macro undefined evaluates to
  `0 >= 0` → true.** Omitting a `BOARD_*` macro therefore *enables* the block
  and fails confusingly at C++ level. Every board header defines
  `BOARD_VEXT_ENABLE` / `BOARD_VEXT_ON_LEVEL`, using `-1`/`HIGH` when absent.
  `hw_square.h` now does too. Same trap likely lurks for other `>= 0` guards.
- **Do not `pgrep -f platformio` inside a wait loop** — the loop's own command
  line contains the string, so it matches itself and never exits. Use
  `pgrep -f "penv/bin/pio"`.
- `src/main_lvgl.cpp` is ~35k lines. `grep -n` for an anchor and patch with a
  script; do not try to read it linearly.
- Pre-existing cosmetic bug, not worth a commit on its own:
  `main_lvgl.cpp` prints a literal `\n` in
  `Serial.printf("[lvgl-poc] started (%dx%d)\\n", ...)` — it is `\\n` in the
  source and present in `HEAD`, so it is not from this work.

---

## 8. Verification commands

```bash
pio run -e square              # this target
pio run                        # all 8 — do this before any commit touching
                               # config.h / board.h / display.h / storage.*
```

Shared headers were edited, so an all-target build is the regression check that
matters. Last known good: **8/8 SUCCESS**, 2026-08-26.
