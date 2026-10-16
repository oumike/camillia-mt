#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/crowpanel_50_fonts.h — every UI font two pixels larger on the CrowPanel 5"
//
// The 800x480 panel reads small at the sizes the rest of the firmware is drawn
// in, and those sizes are named directly at several hundred call sites. Rather
// than a second set of numbers at each, main_lvgl.cpp includes this after
// lvgl.h, and from here on each Montserrat name means the face one step up:
// +2 px through 20, then the next size the font set has (20 -> 24 -> 28 -> 32).
// 32 and 40 keep their own faces -- 32 is the header clock, and 40 would not
// fit the header.
//
// The font files and emoji_font.cpp are not affected: they still define and
// key on the real faces, and emoji_font.cpp carries faces for the sizes this
// asks for. References rather than chained macros, because the preprocessor
// would otherwise carry 10 -> 12 -> 14 ... all the way up.
// ════════════════════════════════════════════════════════════════════════════

#include <lvgl.h>

#if defined(DEVICE_CROWPANEL_50)
namespace cp50font {
static const lv_font_t &f8  = lv_font_montserrat_10;
static const lv_font_t &f10 = lv_font_montserrat_12;
static const lv_font_t &f12 = lv_font_montserrat_14;
static const lv_font_t &f14 = lv_font_montserrat_16;
static const lv_font_t &f16 = lv_font_montserrat_18;
static const lv_font_t &f18 = lv_font_montserrat_20;
static const lv_font_t &f20 = lv_font_montserrat_24;
static const lv_font_t &f24 = lv_font_montserrat_28;
static const lv_font_t &f28 = lv_font_montserrat_32;
}  // namespace cp50font

#define lv_font_montserrat_8  cp50font::f8
#define lv_font_montserrat_10 cp50font::f10
#define lv_font_montserrat_12 cp50font::f12
#define lv_font_montserrat_14 cp50font::f14
#define lv_font_montserrat_16 cp50font::f16
#define lv_font_montserrat_18 cp50font::f18
#define lv_font_montserrat_20 cp50font::f20
#define lv_font_montserrat_24 cp50font::f24
#define lv_font_montserrat_28 cp50font::f28
#endif
