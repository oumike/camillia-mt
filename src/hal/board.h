#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/board.h — Master hardware abstraction selector
//
// Every source file in this project includes config.h, which in turn includes
// this header.  Based on the DEVICE_* build flag set in platformio.ini, this
// header includes exactly one per-device pin file and then defines a small set
// of board-agnostic convenience aliases used elsewhere.
//
// Adding a new hardware target:
//   1. Create  src/hal/hw_<device>.h  with all the required pin macros (copy
//      an existing device file as a starting point).
//   2. Add a new  #elif defined(DEVICE_<DEVICE>)  branch below.
//   3. Add a new  [env:<target>]  section in platformio.ini  and set
//      -DDEVICE_<DEVICE>=1  in its build_flags.
//   3b. Add DEVICE_<DEVICE> to the "no device specified" guard at the top of
//      config.h, and give it a MY_HW_MODEL arm there. Miss the guard and the
//      new target still gets DEVICE_TDECK forced on underneath it — this file
//      then matches DEVICE_TDECK first and the build compiles against the
//      T-Deck pin map, with the new header never included. It builds, boots,
//      and drives the wrong pins; the giveaway is the boot log reporting
//      another board's pin numbers.
//   4. If your device needs TFT rotation different from the default (landscape),
//      set  TFT_ROTATION_DEFAULT  in the device header or in the elif chain at
//      the bottom of this file.
//
// Supported devices (one must be defined at compile time):
//   DEVICE_TDECK                LilyGO T-Deck
//   DEVICE_TDECK_PRO            LilyGO T-Deck Pro
//   DEVICE_TLORA_PAGER_TFT      LilyGO T-LoRa Pager TFT
//   DEVICE_CARDPUTER_LORA_HAT   M5Stack Cardputer + LoRa-1262 Cap
//   DEVICE_HELTEC_V4_EXPANSION  Heltec WiFi LoRa 32 V3 + TFT expansion
//   DEVICE_MESH_DECK            Attaky Mesh Deck 1.0 (modular frame)
//   DEVICE_M9                   Elecrow ThinkNode M9 (LR1110, no touch)
//   DEVICE_SQUARE               Square (QSPI NV3031B, expander-gated rails)
// ════════════════════════════════════════════════════════════════════════════

#if defined(DEVICE_TDECK)
#  include "hw_tdeck.h"
#elif defined(DEVICE_TDECK_PRO)
#  include "hw_tdeck_pro.h"
#elif defined(DEVICE_TLORA_PAGER_TFT)
#  include "hw_tlora_pager.h"
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
#  include "hw_cardputer.h"
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#  include "hw_heltec_v4.h"
#elif defined(DEVICE_MESH_DECK)
#  include "hw_mesh_deck.h"
#elif defined(DEVICE_M9)
#  include "hw_m9.h"
#elif defined(DEVICE_SQUARE)
#  include "hw_square.h"
#else
#  error "No DEVICE_* build flag set. Define one of: DEVICE_TDECK, DEVICE_TDECK_PRO, \
DEVICE_TLORA_PAGER_TFT, DEVICE_CARDPUTER_LORA_HAT, DEVICE_HELTEC_V4_EXPANSION, \
DEVICE_MESH_DECK, DEVICE_M9, DEVICE_SQUARE"
#endif

#ifndef KB_INT_ACTIVE_LEVEL
#  define KB_INT_ACTIVE_LEVEL LOW
#endif

// ── TFT default rotation ──────────────────────────────────────────────────────
// Most boards use landscape (rotation=1).  Override per-device where needed.
#if defined(DEVICE_TDECK_PRO)
#  define TFT_ROTATION_DEFAULT 0
#elif defined(DEVICE_HELTEC_V4_EXPANSION) && !DEVICE_UI_VERTICAL
#  define TFT_ROTATION_DEFAULT 3
#elif defined(DEVICE_SQUARE)
// The panel carries offset_rotation=1. LovyanGFX adds that to this logical
// rotation, so 0 produces internal rotation 1: 320x240, rotated left 90 degrees
// from the previous internal rotation 2. Keep the sourced panel/touch offsets.
#  define TFT_ROTATION_DEFAULT 0
#elif defined(DEVICE_TLORA_PAGER_TFT)
#  define TFT_ROTATION_DEFAULT 3
#elif defined(DEVICE_MESH_DECK)
// The ST7789 is mounted rotated 180° from the usual landscape orientation, so
// the default (1) comes out upside down on this frame.
#  define TFT_ROTATION_DEFAULT 3
#elif defined(DEVICE_M9)
// Hardware-verified on this build: 1 comes out upside down, 3 puts the keyboard
// below the screen. The reference MeshCore port records the opposite, but it
// drives the panel through Adafruit_ST7789 while this uses LovyanGFX, and the
// two libraries do not number rotations the same way — so its value does not
// carry over, and the panel is the only authority.
#  define TFT_ROTATION_DEFAULT 3
#elif DEVICE_UI_VERTICAL
#  define TFT_ROTATION_DEFAULT 0
#else
#  define TFT_ROTATION_DEFAULT 1
#endif

// ── Channel list presentation ────────────────────────────────────────────────
// Two layouts exist for the main screen. Boards with this set render channels
// as an overlay dropdown that appears on demand, leaving the full width to the
// chat; boards without it keep a permanently anchored channel list beside the
// chat, which suits the Pager's wide 480px panel but wastes a squarer one.
//
// This used to be spelled out longhand as the same three-device condition at a
// dozen call sites, which made adding a board a dozen chances to miss one.
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_HELTEC_V4_EXPANSION) \
    || defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || defined(DEVICE_SQUARE)
#  define UI_CHANNEL_LIST_DROPDOWN 1
#else
#  define UI_CHANNEL_LIST_DROPDOWN 0
#endif

// ── Touch-only UI profile ───────────────────────────────────────────────────
// Boards with touch input but no built-in keyboard use tap-first controls,
// an on-screen keyboard and the roomier 320x240 touch layout. Keep this
// separate from hardware-specific Heltec paths such as CHSC6X and VEXT.
#if defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_SQUARE)
#  define UI_TOUCH_ONLY_PROFILE 1
#else
#  define UI_TOUCH_ONLY_PROFILE 0
#endif

// ── Bottom icon nav bar ──────────────────────────────────────────────────────
// Wider than UI_TOUCH_ONLY_PROFILE: a bar of tap targets is worth having on
// anything you can tap, so the test is the panel, not the absence of a
// keyboard. That takes in the touch-only boards and the two that have both a
// keyboard and a touch panel (T-Deck, Mesh Deck).
//
// Board-capability driven rather than a list of DEVICE_ names, so a new board
// with a touch panel gets the bar by declaring HAS_TOUCH and nothing else.
// Where the board has a keyboard as well the bar is a preference — see
// HAS_NAV_BAR_TOGGLE in config.h — and where it does not, taps are the only way
// off a screen and the bar is not negotiable.
#if UI_TOUCH_ONLY_PROFILE || HAS_TOUCH
#  define UI_TOUCH_NAV_BAR 1
#else
#  define UI_TOUCH_NAV_BAR 0
#endif

// ── Screen mirror (VNC host) ─────────────────────────────────────────────────
// Boards that serve their own browser viewer and stream the panel to it. Two
// hardware prerequisites: PSRAM, because the host keeps a full RGB565 copy of
// the panel (320x240 or 240x320 = 150 KB everywhere except the Pager's 480x222
// = 208 KB) that internal RAM cannot spare, and a WiFi station, since the
// stream needs a routable address.
//
// This stays an explicit allowlist rather than a test of BOARD_HAS_PSRAM so a
// new board opts in deliberately. Square has the same 320x240 / 8 MB PSRAM
// shape as the existing hosts and exposes the browser Remote controls.
//
// The Heltec entry covers both its environments — heltec-v4 and the vertical
// variant share DEVICE_HELTEC_V4_EXPANSION and differ only in rotation, which
// the host never sees: it takes the panel size from the display at init, so the
// portrait build mirrors 240x320 without anything here changing.
//
// This was spelled out as defined(DEVICE_TDECK) at twenty-six call sites across
// three files, which is twenty-six chances to miss one when a board joins.
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT) \
    || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || defined(DEVICE_SQUARE)
#  define HAS_VNC_HOST 1
#else
#  define HAS_VNC_HOST 0
#endif

// ── BLE keyboard host (HID over GATT central) ────────────────────────────────
// Boards that can pair an external Bluetooth keyboard and merge its keypresses
// into the same pipeline as the built-in one. See src/ble_keyboard.cpp.
//
// One hardware fact governs the whole feature and is worth stating here rather
// than only in the docs: every board this project supports is an ESP32-S3,
// which has no Bluetooth Classic (BR/EDR) radio at all. Only a BLE / "Bluetooth
// Low Energy" keyboard can ever pair; a Classic-only one cannot, on any board,
// with any firmware. docs/BLUETOOTH_KEYBOARDS.md has the buying guidance.
//
// Keyboard-less touch boards gain the most. Nothing in the implementation is
// board-specific — this macro plus a build_src_filter entry is the entire gate
// — but the NimBLE stack costs 30-40 KB of internal DRAM while it is running,
// so BLE remains an explicit per-board opt-in.
#if defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_SQUARE)
#  define HAS_BLE_KEYBOARD 1
#else
#  define HAS_BLE_KEYBOARD 0
#endif

// ── Panel offset defaults ─────────────────────────────────────────────────────
// Some panels have a pixel offset baked into the driver IC.  Boards that don't
// need an offset simply don't define these in their hw_*.h; default to zero.
#ifndef TFT_PANEL_OFFSET_X
#  define TFT_PANEL_OFFSET_X 0
#endif
#ifndef TFT_PANEL_OFFSET_Y
#  define TFT_PANEL_OFFSET_Y 0
#endif

// ── Screen wake policy defaults ──────────────────────────────────────────────
// Which inputs are allowed to wake a sleeping display. Boards that want a
// narrower gesture set (T-Deck: trackball click only) override these in their
// hw_*.h. Anything excluded here is also excluded as a light-sleep GPIO wake
// source, so it cannot wake the CPU either.
#ifndef SCREEN_WAKE_FROM_KEYBOARD
#  define SCREEN_WAKE_FROM_KEYBOARD 1
#endif
#ifndef SCREEN_WAKE_FROM_TOUCH
#  define SCREEN_WAKE_FROM_TOUCH 1
#endif

// ── Light-sleep nap length ───────────────────────────────────────────────────
// How long a light-sleep nap may last before a timer wake. Inputs that can
// assert a GPIO wake line (keyboard IRQ, wheel/trackball click, buttons) make
// this irrelevant to responsiveness — they interrupt the nap directly, so the
// timer only has to be quick enough for scheduled TX, and a longer nap
// amortises the fixed entry/exit cost.
//
// The exception is a board whose primary input cannot raise an interrupt at
// all: there the nap length *is* the input latency, and a long one drops
// keystrokes outright. Cardputer's keyboard is a matrix scanned in software
// over shared GPIO with no IRQ line, so it takes the short nap. Its BOOT button
// is armed as a wake source, but that is not how anyone types.
#ifndef NAP_MAX_MS
#  if defined(DEVICE_CARDPUTER_LORA_HAT)
#    define NAP_MAX_MS 250
#  else
#    define NAP_MAX_MS 1500
#  endif
#endif

// ── Backward-compatible SPI bus aliases ──────────────────────────────────────
// Several modules reference SPI_SCK/SPI_MISO/SPI_MOSI for the LoRa bus.
// These resolve to the LoRa-specific macros so both spellings work.
#define SPI_SCK   LORA_SPI_SCK
#define SPI_MISO  LORA_SPI_MISO
#define SPI_MOSI  LORA_SPI_MOSI
