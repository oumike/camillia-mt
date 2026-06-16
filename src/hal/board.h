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
//   4. If your device needs TFT rotation different from the default (landscape),
//      set  TFT_ROTATION_DEFAULT  in the device header or in the elif chain at
//      the bottom of this file.
//
// Supported devices (one must be defined at compile time):
//   DEVICE_TDECK                LilyGO T-Deck
//   DEVICE_TLORA_PAGER_TFT      LilyGO T-LoRa Pager TFT
//   DEVICE_CARDPUTER_LORA_HAT   M5Stack Cardputer + LoRa-1262 Cap
//   DEVICE_HELTEC_V4_EXPANSION  Heltec WiFi LoRa 32 V3 + TFT expansion
// ════════════════════════════════════════════════════════════════════════════

#if defined(DEVICE_TDECK)
#  include "hw_tdeck.h"
#elif defined(DEVICE_TLORA_PAGER_TFT)
#  include "hw_tlora_pager.h"
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
#  include "hw_cardputer.h"
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#  include "hw_heltec_v4.h"
#else
#  error "No DEVICE_* build flag set. Define one of: DEVICE_TDECK, \
DEVICE_TLORA_PAGER_TFT, DEVICE_CARDPUTER_LORA_HAT, DEVICE_HELTEC_V4_EXPANSION"
#endif

// ── TFT default rotation ──────────────────────────────────────────────────────
// Most boards use landscape (rotation=1).  Override per-device where needed.
#if defined(DEVICE_HELTEC_V4_EXPANSION) && !DEVICE_UI_VERTICAL
#  define TFT_ROTATION_DEFAULT 3
#elif defined(DEVICE_TLORA_PAGER_TFT)
#  define TFT_ROTATION_DEFAULT 3
#elif DEVICE_UI_VERTICAL
#  define TFT_ROTATION_DEFAULT 0
#else
#  define TFT_ROTATION_DEFAULT 1
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

// ── Backward-compatible SPI bus aliases ──────────────────────────────────────
// Several modules reference SPI_SCK/SPI_MISO/SPI_MOSI for the LoRa bus.
// These resolve to the LoRa-specific macros so both spellings work.
#define SPI_SCK   LORA_SPI_SCK
#define SPI_MISO  LORA_SPI_MISO
#define SPI_MOSI  LORA_SPI_MOSI
