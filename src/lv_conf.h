#ifndef LV_CONF_H
#define LV_CONF_H

// Partial config: lv_conf_internal.h fills in the LVGL v9 defaults for every
// option not set here, so only the deltas from stock live in this file.

#define LV_COLOR_DEPTH 16
// v9 has no LV_COLOR_16_SWAP. lv_color_t is always 24-bit RGB; the *display*
// carries the pixel format (RGB565 here, chosen from LV_COLOR_DEPTH), and byte
// order is the driver's problem — LovyanGFX pushes rgb565_t natively.

// v9 replaced LV_MEM_CUSTOM with a stdlib selector. Built-in keeps the fixed
// LV_MEM_SIZE pool, which is what the low-memory guards in main_lvgl.cpp
// (lv_mem_monitor free_biggest_size checks) reason about.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN

#if defined(BOARD_HAS_PSRAM)
// Put the pool in PSRAM rather than the default static array.
//
// By default lv_mem_init() declares `static MEM_UNIT work_mem_int[LV_MEM_SIZE]`,
// which lands in internal DRAM — the same arena WiFi, TLS and the OTA handshake
// draw from. At 128 KB that left the largest free internal block at ~9 KB, so
// the pool could not be grown even though the UI needed it: opening the emoji
// picker over a full DM view ran the pool down to ~21 KB free, and glyph
// rasterization then failed inside stb_truetype (which allocates via lv_malloc).
//
// Moving the pool out fixes both ends — LVGL gets far more room than it can use,
// and internal DRAM gets ~128 KB of contiguous space back for the network path.
//
// The cost is that LVGL's objects and styles now live behind the PSRAM cache,
// and walking them is pointer-heavy. If the UI feels sluggish, this block is the
// thing to revert; the pool returns to internal DRAM by deleting it.
#define LV_MEM_POOL_INCLUDE "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define LV_MEM_SIZE (384U * 1024U)
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
// No PSRAM: keep the internal pool tighter to preserve AP/web-config headroom.
#define LV_MEM_SIZE (80U * 1024U)
#else
#define LV_MEM_SIZE (128U * 1024U)
#endif

// Keep LVGL assert-integrity checks off in normal builds; with constrained RAM,
// transient pool pressure can otherwise force hard abort/reboot paths.
#define LV_USE_ASSERT_MEM_INTEGRITY 0

#define LV_USE_LOG 0

// v9 renamed the PNG decoder to LODEPNG (the old upng-based LV_USE_PNG is gone).
// Used by the SD-card node map image.
#define LV_USE_LODEPNG 1

// Monochrome emoji rendering: stb_truetype rasterizes glyphs from a flash-
// resident Noto Emoji face on demand, wired in as the Montserrat fallback font
// (see src/emoji_font.*). FILE_SUPPORT stays off — the face is baked in, so no
// filesystem dependency and it works identically on every board.
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0

// v9 dropped LV_TICK_CUSTOM; the tick source is installed at runtime instead
// with lv_tick_set_cb(millis) — see uiInit() in main_lvgl.cpp.

// Widgets the UI actually builds. Everything else stays at its default.
#define LV_USE_CHART 1
#define LV_USE_SCALE 1

#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1

#define LV_FONT_DEFAULT &lv_font_montserrat_16

#endif
