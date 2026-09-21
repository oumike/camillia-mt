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
//   DEVICE_HELTEC_R8            Heltec WiFi LoRa 32 V4-R8 + Expansion Kit V2.
//                               Its build defines DEVICE_HELTEC_V4_EXPANSION as
//                               well -- see the include chain below.
//   DEVICE_MESH_DECK            Attaky Mesh Deck 1.0 (modular frame)
//   DEVICE_M9                   Elecrow ThinkNode M9 (LR1110, no touch)
//   DEVICE_WIO_TRACKER_L2       Seeed Wio Tracker L2 (NV3031B, gated rails)
// ════════════════════════════════════════════════════════════════════════════

#if defined(DEVICE_TDECK)
#  include "hw_tdeck.h"
#elif defined(DEVICE_TDECK_PRO)
#  include "hw_tdeck_pro.h"
#elif defined(DEVICE_TLORA_PAGER_TFT)
#  include "hw_tlora_pager.h"
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
#  include "hw_cardputer.h"
// Before the V4 arm on purpose. An R8 build defines DEVICE_HELTEC_V4_EXPANSION
// too, so that it inherits every piece of V4 behaviour keyed on that macro --
// the touch-only UI profile, the CHSC6X paths, the rotation default, the sensor
// and GPS arms -- without each of those three dozen call sites having to learn
// about a second Heltec. What differs between the two boards is almost entirely
// pins, and those live in the header this arm selects. Anything that is a real
// behavioural difference gates on DEVICE_HELTEC_R8 explicitly.
#elif defined(DEVICE_HELTEC_R8)
#  include "hw_heltec_r8.h"
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#  include "hw_heltec_v4.h"
#elif defined(DEVICE_MESH_DECK)
#  include "hw_mesh_deck.h"
#elif defined(DEVICE_M9)
#  include "hw_m9.h"
#elif defined(DEVICE_WIO_TRACKER_L2)
#  include "hw_wio_tracker_l2.h"
#else
#  error "No DEVICE_* build flag set. Define one of: DEVICE_TDECK, DEVICE_TDECK_PRO, \
DEVICE_TLORA_PAGER_TFT, DEVICE_CARDPUTER_LORA_HAT, DEVICE_HELTEC_V4_EXPANSION, \
DEVICE_HELTEC_R8, DEVICE_MESH_DECK, DEVICE_M9, DEVICE_WIO_TRACKER_L2"
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
#elif defined(DEVICE_WIO_TRACKER_L2)
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

// ── Runtime panel orientation ────────────────────────────────────────────────
// The Heltec V4 runs either way up and which one is a setting rather than a
// build, so both rotations are named here and setup() picks one from NVS before
// the panel comes up (issue #77). Every other board keeps the single
// TFT_ROTATION_DEFAULT above, and uiPortrait() folds to a compile-time constant
// there, so nothing downstream pays for a choice it does not have.
//
// Both Heltec families: the V4 on its expansion kit and the V4-R8 on the
// Expansion Kit V2. They share this header's rotation values because they share
// a panel orientation — only the pin maps differ.
#if defined(DEVICE_HELTEC_V4_EXPANSION)
#  define HAS_RUNTIME_ORIENTATION 1
#  define TFT_ROTATION_LANDSCAPE  3
#  define TFT_ROTATION_PORTRAIT   0
#elif defined(DEVICE_WIO_TRACKER_L2)
// Same 240x320 panel as the Heltec, so every layout number the runtime path
// already carries for both shapes applies here unchanged -- this board needed
// only its own pair of rotations.
//
// Those are NOT the Heltec's, because this panel sets offset_rotation=1 and
// LovyanGFX adds that to the logical rotation below. So logical 0 is internal
// 1 (320x240, the landscape this board has always run, and the value the
// TFT_ROTATION_DEFAULT chain above still gives it), and logical 1 is internal
// 2 -- the 240x320 portrait this panel ran before it was turned landscape, per
// the note beside TFT_ROTATION_DEFAULT. Logical 3 is the same portrait upside
// down, so if the panel comes up inverted this is the one line to change.
//
// The GT911 carries its own offset_rotation (TOUCH_OFFSET_ROTATION=2); LGFX
// composes that with whatever the display is set to, so touch tracks the
// rotation rather than needing a second pair of values here.
#  define HAS_RUNTIME_ORIENTATION 1
#  define TFT_ROTATION_LANDSCAPE  0
#  define TFT_ROTATION_PORTRAIT   1
#else
#  define HAS_RUNTIME_ORIENTATION 0
#endif

#if HAS_RUNTIME_ORIENTATION
// The second portrait, 180 degrees from the first. Derived rather than given
// per board: a half turn is always two quarter turns on from whatever that
// board calls portrait, so every board with runtime orientation gets this for
// free and cannot get it inconsistent with its own TFT_ROTATION_PORTRAIT.
//
// Why two portraits at all: which way up portrait wants to be depends on where
// the cable leaves the case and which hand is holding it, and that is a
// property of the moment rather than of the board. Landscape has the same
// question, but it already has an answer baked into each board's
// TFT_ROTATION_LANDSCAPE, chosen so the screen sits the right way up relative
// to that board's buttons.
#  define TFT_ROTATION_PORTRAIT_180  (((TFT_ROTATION_PORTRAIT) + 2) & 3)
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
    || defined(DEVICE_M9) || defined(DEVICE_WIO_TRACKER_L2)
#  define UI_CHANNEL_LIST_DROPDOWN 1
#else
#  define UI_CHANNEL_LIST_DROPDOWN 0
#endif

// ── Touch-only UI profile ───────────────────────────────────────────────────
// Boards with touch input but no built-in keyboard use tap-first controls,
// an on-screen keyboard and the roomier 320x240 touch layout. Keep this
// separate from hardware-specific Heltec paths such as CHSC6X and VEXT.
#if defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_WIO_TRACKER_L2)
#  define UI_TOUCH_ONLY_PROFILE 1
#else
#  define UI_TOUCH_ONLY_PROFILE 0
#endif

// ── Dedicated screen / wake button ───────────────────────────────────────────
// True on the boards that have a physical button whose job is the screen: the
// side toggle, the BOOT button where nothing else has claimed it, or the Wio
// Tracker L2's Wake button on its expander. Those buttons share one rule -- tap
// to put the device away or to glance at the lock screen, hold to unlock -- and
// this is what compiles that rule in.
//
// On the touch-only boards GPIO0 is the UI action button rather than a screen
// key, so it does not count; those boards wake from the panel instead. The
// Heltec R8 has neither and comes out false, which is correct: touch is its
// only wake gesture.
#if (defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0) && !UI_TOUCH_ONLY_PROFILE) \
    || (defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)) \
    || defined(DEVICE_WIO_TRACKER_L2)
#  define HAS_WAKE_BUTTON 1
#else
#  define HAS_WAKE_BUTTON 0
#endif

// ── Bottom icon nav bar ──────────────────────────────────────────────────────
// Every board builds the bar. This used to test HAS_TOUCH, on the reasoning
// that a bar of tap targets is only worth having on something you can tap —
// but nothing about the bar is useless without a finger: it names every
// destination, it carries the GPS/WiFi cluster, it takes the unread marks, and
// on the M9 and the Pager its cells are genuinely clickable from the pointer the
// browser Remote registers. The keyboard boards with no panel simply start with
// it switched off — see MY_NAV_BAR_ENABLED in config.h — which is the footer
// they have always drawn; turning it on is the user's call. Issue #92.
//
// So this is no longer a capability test. That question moved wholesale to
// HAS_NAV_BAR_TOGGLE in config.h, which asks whether there is a keyboard to fall
// back on and therefore whether the bar is a preference or the only way off a
// screen.
//
// Kept as a macro rather than deleted, for two reasons: it gates a good deal of
// code that has nothing to build without it, and a board can still opt out by
// defining UI_NO_NAV_BAR in its hw_*.h. That is the escape hatch if an
// environment cannot afford the flash — the bar, its cells, its status cluster
// and its settings row all compile out and that board keeps its key-hint strip.
#if defined(UI_NO_NAV_BAR)
#  define UI_TOUCH_NAV_BAR 0
#else
#  define UI_TOUCH_NAV_BAR 1
#endif

// ── Screen mirror (VNC host) ─────────────────────────────────────────────────
// Boards that serve their own browser viewer and stream the panel to it. Two
// hardware prerequisites: PSRAM, because the host keeps a full RGB565 copy of
// the panel (320x240 or 240x320 = 150 KB everywhere except the Pager's 480x222
// = 208 KB) that internal RAM cannot spare, and a WiFi station, since the
// stream needs a routable address.
//
// This stays an explicit allowlist rather than a test of BOARD_HAS_PSRAM so a
// new board opts in deliberately. The Wio Tracker L2 has the same 320x240 /
// 8 MB PSRAM shape as the existing hosts and exposes the browser Remote
// controls.
//
// The Heltec entry covers both its environments — heltec-v4 and the vertical
// variant share DEVICE_HELTEC_V4_EXPANSION and differ only in rotation, which
// the host never sees: it takes the panel size from the display at init, so the
// portrait build mirrors 240x320 without anything here changing.
//
// The T-Deck Pro is the one host that does not hand LVGL RGB565. Its panel runs
// at LV_COLOR_FORMAT_I1, so lvglFlush() feeds the mirror through
// vncHostCaptureFlushI1() instead, which expands the bits as it writes. The
// browser still receives ordinary RGB565, and the mirror updates at LVGL's rate
// rather than the e-paper's — the remote view refreshes faster than the panel
// it is mirroring.
//
// This was spelled out as defined(DEVICE_TDECK) at twenty-six call sites across
// three files, which is twenty-six chances to miss one when a board joins.
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT) \
    || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || defined(DEVICE_WIO_TRACKER_L2) \
    || defined(DEVICE_TDECK_PRO)
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
#if defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_WIO_TRACKER_L2)
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
