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

// Sized to the PSRAM part rather than to the smallest board that has any.
//
// 384 KB was not enough. Decoding one 240x160 state map costs ~250 KB while it
// runs — lv_lodepng always decodes to ARGB8888, so the output alone is 150 KB,
// on top of the compressed file and the inflate scratch — and against a live
// Nodes screen that pushed lv_realloc() into failure:
//
//   [Error] lv_realloc: couldn't reallocate memory   lv_mem.c:162
//   [Warn]  decoder_open: Error decoding PNG         lv_lodepng.c:175
//   [Error] lv_draw_image_normal_helper: Failed to open image
//
// The same pressure was already breaking the tiny_ttf glyph cache ("cache not
// allocated"), silently, on every screen carrying emoji — so this was costing
// more than the map. The 8 MB boards report ~7.7 MB of PSRAM free with the old
// pool in place, so this is spare capacity that was simply never claimed, and
// it leaves room for a higher-resolution map (480x320 decodes to 614 KB).
#if defined(DEVICE_HELTEC_V4_EXPANSION)
// 2 MB part. This was 768 KB to fit a state map decode; maps are compiled out
// on this board now (HAS_STATE_MAPS), so that reason is gone — but the other
// one is not. The glyph-cache failures quoted above are an emoji/tiny_ttf
// problem, not a map problem, and they were happening against the old 384 KB
// on a panel this size. 512 KB keeps that headroom without reserving a quarter
// of the board's PSRAM for it.
//
// Measurable rather than guessed: the "[lvgl] openLiveModal pool used=N%" line
// reports actual occupancy, so trim or raise this against a real reading.
#define LV_MEM_SIZE (512U * 1024U)
#else
// 8 MB parts (T-Deck, Pager, M9, Mesh Deck).
#define LV_MEM_SIZE (2048U * 1024U)
#endif
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
// No PSRAM: keep the internal pool tighter to preserve AP/web-config headroom.
#define LV_MEM_SIZE (80U * 1024U)
#else
#define LV_MEM_SIZE (128U * 1024U)
#endif

// Keep LVGL assert-integrity checks off in normal builds; with constrained RAM,
// transient pool pressure can otherwise force hard abort/reboot paths.
#define LV_USE_ASSERT_MEM_INTEGRITY 0

// On, at warning level, and routed to Serial by lvglLogToSerial() in
// main_lvgl.cpp. LVGL reports decode and draw failures through this and nothing
// else — with it off, a PNG that will not decode simply renders as empty space,
// which is exactly how the Locate map failed silently through several rounds of
// guessing. The warning level is quiet in normal operation.
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
// Not LVGL's own printf: on boards that put the console on USB-CDC it would go
// to a port nobody is watching. The registered callback uses Serial instead.
#define LV_LOG_PRINTF 0
#define LV_LOG_USE_TIMESTAMP 0
#define LV_LOG_USE_FILE_LINE 1

// v9 renamed the PNG decoder to LODEPNG (the old upng-based LV_USE_PNG is gone).
// Used by the SD-card node map image.
#define LV_USE_LODEPNG 1

// Keep decoded PNGs resident on PSRAM boards so map redraws (status updates,
// pans, zooms) do not re-run lodepng every frame on the UI thread.
//
// 0 disables decoded-image caching completely (LVGL default), which is fine on
// tiny builds but too expensive for map-heavy screens on boards like T-Deck.
// Heltec v4 expansion keeps the default disabled path because its LVGL pool is
// intentionally capped lower.
#if defined(BOARD_HAS_PSRAM) && !defined(DEVICE_HELTEC_V4_EXPANSION)
#define LV_CACHE_DEF_SIZE (512U * 1024U)
#endif

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
