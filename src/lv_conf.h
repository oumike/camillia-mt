#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 0
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define LV_MEM_SIZE (96U * 1024U)
#else
#define LV_MEM_SIZE (128U * 1024U)
#endif

// Keep LVGL assert-integrity checks off in normal builds; with constrained RAM,
// transient pool pressure can otherwise force hard abort/reboot paths.
#define LV_USE_ASSERT_MEM_INTEGRITY 0

#define LV_USE_LOG 0

#define LV_USE_PNG 1

// Monochrome emoji rendering: stb_truetype rasterizes glyphs from a flash-
// resident Noto Emoji face on demand, wired in as the Montserrat fallback font
// (see src/emoji_font.*). FILE_SUPPORT stays off — the face is baked in, so no
// filesystem dependency and it works identically on every board.
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

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
