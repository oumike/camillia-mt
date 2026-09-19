# Handoff — `tdisplay-p4` build target (issue #90)

Working notes for the LilyGO T-Display P4 port. Written 2026-09-19, **before the
hardware arrived**.

**Read issue #90 first, then this.** #90 is the ticket and carries the reasoning;
this file is the working log that fills in as the port proceeds. Right now it is
almost entirely pre-hardware research plus the order the work has to happen in.

Target variant: **4.1" AMOLED** (568x1232, RM69A10, GT9895 touch), **SX1262**
radio, **no keyboard** in hand — but the TCA8418 keyboard accessory is in scope.

## 0. Status

**Nothing is implemented.** No branch carries code, no env exists, no pin header
has been written. Board expected the week of 2026-09-21.

The one thing that can usefully be done before the hardware lands is the
**toolchain spike** in section 3 — it needs no board, and its outcome decides the
shape of everything after it.

## 1. The rule that governs this port

From `.github/copilot-instructions.md`, non-negotiable:

> Do not invent pin maps, bus addresses, or peripheral wiring.
> Do not claim hardware success without evidence (serial logs, measured
> behavior, or user confirmation).

Nothing in this file is a pin. Where a pin is eventually needed it comes from
LilyGO's own device config headers under `libraries/lilygo_device_driver/` in
<https://github.com/Xinyuan-LilyGO/T-Display-P4>, or from the schematic — and it
gets recorded in section 4 with its source, not asserted.

This matters more here than on previous ports because LilyGO's reference code is
**ESP-IDF only** (they require >= 5.5.4 and ship no Arduino or PlatformIO
support). Their drivers are material to port *from*, never a dependency to pull
in, and transcribing a pin out of IDF driver code is exactly where a wrong number
enters unnoticed.

## 2. Why this port is not shaped like #71 or #56

Three independent problems, in the order they block each other:

1. **Toolchain.** ESP32-P4 is RISC-V and does not exist on arduino-esp32 2.0.17,
   which is what `espressif32@7.0.1` ships and what all eleven envs build
   against. P4 needs arduino-esp32 3.3.x, reachable from PlatformIO only via the
   pioarduino platform fork.
2. **Display.** MIPI-DSI, RM69A10 — a bus and a controller LovyanGFX may or may
   not be able to drive under that core.
3. **Radio.** The SoC has none. WiFi and BLE live on an ESP32-C6 over SDIO via
   ESP-Hosted, with its own separately-flashed firmware.

Only after all three are answered does a pin map matter. Resist the urge to write
`hw_tdisplay_p4.h` first; it is the familiar-feeling part and it is the part that
cannot be validated until the other three work.

## 3. First task — the toolchain spike (no hardware needed)

Decision already taken on #90: **the P4 env alone moves to pioarduino; the other
ten stay on `espressif32@7.0.1`.** The consequence is that `src/` must compile
under both Arduino 2.x and 3.x from that point on.

Spike, in order:

1. Stand up a throwaway `[env:tdisplay-p4]` on a pinned pioarduino release with a
   near-empty `build_src_filter`. Confirm it compiles *anything* for P4.
2. Add LVGL 9.5. It is core-version-agnostic in principle; confirm it.
3. **Add LovyanGFX and find out whether its `esp32p4` `Bus_DSI` path builds under
   Arduino 3.3.x at all.** Upstream <https://github.com/lovyan03/LovyanGFX/issues/788>
   reports it does not on 3.3.4 / IDF 5.5.1. If that reproduces, decide between
   waiting on upstream, carrying a patch (there is precedent: `tools/patch_lgfx_dmadesc.py`
   already patches LovyanGFX in this repo), or dropping LovyanGFX for this board.
4. Widen `build_src_filter` toward the real list and collect every Arduino-3.x
   compile error. Known-in-advance:
   - `analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db)` — `src/battery_util.cpp:486`.
     `ADC_11db` deprecated in 3.x. **Shared by every env**, so whatever fixes it
     must stay valid on 2.0.17.
   - `ledcSetup()` / `ledcAttachPin()` — `src/hal/tdeck_pro_display.h:81-82`.
     Already excluded from a P4 build; listed so it is not mistaken for a new find.
   The rest of the list is what the spike is for.

**Record the full error list in section 4 when you have it.** That list, not the
pin map, is the real size of this ticket.

### If LovyanGFX cannot drive the panel

Fallback shape is already in the repo: `src/hal/display.h:30` shunts the T-Deck
Pro wholesale to `src/hal/tdeck_pro_display.h`, which drives e-paper through
GxEPD2 and feeds LVGL itself. A `tdisplay_p4_display.h` doing the same over
`esp_lcd_mipi_dsi` is the same move. It is more work but it is a road already
paved, and it removes the dependency on upstream LovyanGFX timing.

## 4. Source-resolved hardware details

*(Empty. Fill in as each fact is resolved, with its source — LilyGO header path,
schematic sheet, or a measurement. Do not pre-populate from product pages.)*

| Subsystem | Fact | Source | Verified on HW |
| --- | --- | --- | --- |
| | | | |

## 5. What should reuse existing code

Established by reading, not yet by building. Each is a hypothesis to confirm:

- **SX1262** — `src/mesh_radio.cpp` should need pins only.
- **SKY13453 antenna switch via XL9535** — `src/hal/xl9555.{h,cpp}` already drives
  the XL9555 / TCA9555 / PCA9555 family and auto-scans 0x20-0x27
  (`src/hal/xl9555.h:3-13`). Confirm the XL9535 register map matches and that its
  address falls in that range.
- **TCA8418 keyboard accessory** — `src/keyboard.cpp` already drives it for
  T-Deck Pro and T-LoRa Pager.
- **microSD** — `src/storage.cpp:16-174` has a complete `HAS_SD_MMC` SDIO backend
  (Wio Tracker L2 uses it).
- **BQ27220 fuel gauge** — `src/battery_util.cpp:297-325` already reads an I2C
  gauge (MAX17048) beside the ADC path. Third backend, existing shape.
- **ES8311 audio** — already handled for Wio Tracker L2 notification audio.
- **16 MB flash** — `partitions_16mb_fs.csv` should drop in unchanged.

Explicitly **not** reuse:

- **Brightness.** No backlight pin; RM69A10 brightness is a DCS command, so this
  needs a new `lgfx::ILight` (or equivalent on the fallback path). Model it on
  `Light_WioTrackerL2LP5814`, `src/hal/display.h:60+`.

## 6. The resolution problem, stated precisely

568x1232 is ~700k pixels against 76.8k on every 240x320 board here. Two halves,
and they are not equally bad:

**The plumbing is fine.** `lv_display_create()` takes `displayDev().width()` /
`.height()` (`src/main_lvgl.cpp:48466`), and rendering is
`LV_DISPLAY_RENDER_MODE_PARTIAL` against a stripe buffer sized
`kMaxHorRes * kDrawBufLines` (`:48424`). Panel size flows through. 32 MB of PSRAM
means the buffer is not a constraint either.

**The layout is not fine.** ~61 literal `240`/`320`s in `main_lvgl.cpp`, and both
layout profiles — `UI_CHANNEL_LIST_DROPDOWN` and `UI_TOUCH_ONLY_PROFILE`,
`src/hal/board.h:165-185` — describe 240x320 and the Pager's 480px width. This
panel is a third shape.

For bring-up the bar on #90 is *usable*, not *good*. Establish early whether a
240x320-derived layout renders correctly-but-small at this size (acceptable for
bring-up, follow-up ticket to fix) or renders **broken** (blocking). That single
observation decides whether a `UI_LARGE_PANEL_PROFILE` is in this ticket or the
next one.

Related: is landscape even meaningful on a portrait-native phone-shaped panel?
If not, this board takes the plain `TFT_ROTATION_DEFAULT` path rather than
`HAS_RUNTIME_ORIENTATION` (issue #77).

## 7. The C6, and a question the build system cannot answer

WiFi and BLE are on the ESP32-C6 over SDIO. Under arduino-esp32 3.3.x the `WiFi`
object is *intended* to work over ESP-Hosted transparently, which if true means
most of the ten files touching `WiFi.h` need nothing: `web_config.cpp`,
`ota_update.cpp`, `mqtt_bridge.cpp`, `vnc_host.cpp`, `weather.cpp`, `los.cpp`,
`main_lvgl.cpp`.

Two things that will not be transparent:

- **BLE central.** `src/ble_keyboard.cpp` is a NimBLE-Arduino central. If hosted
  BLE does not support the central role, this board ships `HAS_BLE_KEYBOARD 0`
  and the accessory keyboard becomes the only external-keyboard path.
- **Shipping the C6 firmware.** The C6 carries its own ESP-Hosted image, flashed
  over its own UART connector. **OTA here updates the P4 application image only.**
  So: how does a user get the C6 image initially, and how does it stay current
  when we bump the hosted version? This needs an answer in `docs/BUILD.md` and
  possibly in `scripts/release.sh`, and it is a genuinely new problem — no
  existing board has a second processor with its own firmware.

## 8. Traps carried forward from previous ports

Cheap to avoid, expensive to debug — all three have bitten this repo before:

- **The `config.h` device guard.** `src/config.h:24` lists every `DEVICE_*`. A
  target missing from it still gets `DEVICE_TDECK` forced on underneath, and
  `board.h` matches T-Deck first — so the build succeeds, boots, and drives the
  T-Deck pin map. The giveaway is the boot log reporting another board's pins.
  Documented at `src/hal/board.h:16-21`.
- **The OTA asset slug.** `otaCurrentDeviceAssetSlug()`, `src/ota_update.cpp:559`.
  A missing arm does not fail loudly — it returns another board's slug and offers
  the device firmware built for different hardware. This exact thing happened to
  the R8; the comment in that function is the scar.
- **The env lists are in three places.** `scripts/release.sh:15` (`RELEASE_ENVS`),
  `.github/workflows/build.yml:81-91` (explicit `pio run -e` lines), and
  `platformio.ini`. Adding an env to one and not the others means it either never
  gets built in CI or never gets published.

Also worth fixing while in here: `.github/copilot-instructions.md`'s Build Targets
and hardware-truth-source lists are **already stale** — missing `tdeck-pro`,
`wio-tracker-l2`, `heltec-r8` and their headers.

## 9. Verification commands

```bash
pio run -e tdisplay-p4         # this target

pio run                        # ALL envs. Do this before any commit touching
                               # config.h / board.h / display.h / battery_util.cpp
                               # or anything else in the shared src/ set.
```

The all-target build matters more on this port than on any previous one. The P4
env is on a **different Arduino core major** from the other ten, so a change that
makes P4 compile can break nine shipping boards, and only the full run catches it.

Last known good: **not yet built.**
