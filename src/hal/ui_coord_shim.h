#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/ui_coord_shim.h — layout coordinates to LVGL pixels (UI_COORD_SCALE)
//
// The UI in main_lvgl.cpp is laid out in fixed pixel numbers for ~400x240-class
// panels. On the Tanmatsu (800x480) those used to be honoured by letting LVGL
// draw a 400x240 frame and doubling every pixel on the way out, which is
// legible but soft: text is a 2x2-blocked 12 px face rather than a 24 px one.
//
// With UI_COORD_SCALE at 2 (board.h) LVGL renders the panel natively instead,
// and this header keeps the layout code unchanged by translating at the LVGL
// boundary, for the rest of main_lvgl.cpp:
//
//   * Every size, position, offset and pixel style the UI hands LVGL is
//     multiplied by UI_COORD_SCALE. Special values (lv_pct, LV_SIZE_CONTENT,
//     LV_RADIUS_CIRCLE, LV_COORD_MAX) pass through untouched.
//   * Every size or position the UI reads back from LVGL is divided by it, so
//     arithmetic on what it read stays in the units it writes.
//   * Every Montserrat face it names means the face twice the size.
//
// Function-like macros, so they only fire on calls and only in the file that
// includes this: LVGL's own sources, and the inline helpers in its headers,
// were compiled before these exist. That is also why this must come after
// every LVGL header -- main_lvgl.cpp includes it last.
//
// What this cannot see is anything that hands LVGL raw pixels without going
// through one of these calls: lv_line point arrays, canvas buffers, and fields
// read straight out of an lv_font_t. Those few places multiply or divide by
// UI_COORD_SCALE themselves.
// ════════════════════════════════════════════════════════════════════════════

#include <lvgl.h>
#include "board.h"

#if UI_COORD_SCALE > 1

// Logical -> LVGL. Anything that is not a plain small coordinate is a sentinel
// or an encoded value and must arrive exactly as written.
static inline int32_t uiS(int32_t v) {
    if (LV_COORD_IS_SPEC(v)) return v;
    if (v >= 16000 || v <= -16000) return v;
    return v * UI_COORD_SCALE;
}

// LVGL -> logical.
static inline int32_t uiU(int32_t v) {
    if (LV_COORD_IS_SPEC(v)) return v;
    if (v >= 16000 * UI_COORD_SCALE || v <= -16000 * UI_COORD_SCALE) return v;
    return v / UI_COORD_SCALE;
}

// Rounded up, for widths that get summed and compared against a limit: an
// underestimate there is a line that overflows by a few pixels.
static inline int32_t uiUCeil(int32_t v) {
    return (v + UI_COORD_SCALE - 1) / UI_COORD_SCALE;
}

static inline void uiObjGetCoords(const lv_obj_t *obj, lv_area_t *a) {
    (lv_obj_get_coords)(obj, a);
    // Keeps lv_area_get_width() exact for a block that starts on an even pixel.
    a->x1 = a->x1 / UI_COORD_SCALE;
    a->y1 = a->y1 / UI_COORD_SCALE;
    a->x2 = (a->x2 + 1) / UI_COORD_SCALE - 1;
    a->y2 = (a->y2 + 1) / UI_COORD_SCALE - 1;
}

static inline void uiTextGetSize(lv_point_t *res, const char *txt, const lv_font_t *font,
                                 int32_t letterSpace, int32_t lineSpace, int32_t maxW,
                                 lv_text_flag_t flag) {
    (lv_text_get_size)(res, txt, font, uiS(letterSpace), uiS(lineSpace), uiS(maxW), flag);
    res->x = uiUCeil(res->x);
    res->y = uiUCeil(res->y);
}

// ── Setters ─────────────────────────────────────────────────────────────────
#define lv_obj_set_width(o, v)            (lv_obj_set_width)((o), uiS(v))
#define lv_obj_set_height(o, v)           (lv_obj_set_height)((o), uiS(v))
#define lv_obj_set_size(o, w, h)          (lv_obj_set_size)((o), uiS(w), uiS(h))
#define lv_obj_set_pos(o, x, y)           (lv_obj_set_pos)((o), uiS(x), uiS(y))
#define lv_obj_set_x(o, v)                (lv_obj_set_x)((o), uiS(v))
#define lv_obj_set_y(o, v)                (lv_obj_set_y)((o), uiS(v))
#define lv_obj_align(o, a, x, y)          (lv_obj_align)((o), (a), uiS(x), uiS(y))
#define lv_obj_align_to(o, b, a, x, y)    (lv_obj_align_to)((o), (b), (a), uiS(x), uiS(y))
#define lv_obj_scroll_by(o, x, y, an)     (lv_obj_scroll_by)((o), uiS(x), uiS(y), (an))
#define lv_obj_scroll_to_y(o, y, an)      (lv_obj_scroll_to_y)((o), uiS(y), (an))
#define lv_obj_set_ext_click_area(o, v)   (lv_obj_set_ext_click_area)((o), uiS(v))

#define lv_obj_set_style_border_width(o, v, s)      (lv_obj_set_style_border_width)((o), uiS(v), (s))
#define lv_obj_set_style_length(o, v, s)            (lv_obj_set_style_length)((o), uiS(v), (s))
#define lv_obj_set_style_line_dash_gap(o, v, s)     (lv_obj_set_style_line_dash_gap)((o), uiS(v), (s))
#define lv_obj_set_style_line_dash_width(o, v, s)   (lv_obj_set_style_line_dash_width)((o), uiS(v), (s))
#define lv_obj_set_style_line_width(o, v, s)        (lv_obj_set_style_line_width)((o), uiS(v), (s))
#define lv_obj_set_style_margin_bottom(o, v, s)     (lv_obj_set_style_margin_bottom)((o), uiS(v), (s))
#define lv_obj_set_style_margin_top(o, v, s)        (lv_obj_set_style_margin_top)((o), uiS(v), (s))
#define lv_obj_set_style_margin_left(o, v, s)       (lv_obj_set_style_margin_left)((o), uiS(v), (s))
#define lv_obj_set_style_margin_right(o, v, s)      (lv_obj_set_style_margin_right)((o), uiS(v), (s))
#define lv_obj_set_style_max_height(o, v, s)        (lv_obj_set_style_max_height)((o), uiS(v), (s))
#define lv_obj_set_style_max_width(o, v, s)         (lv_obj_set_style_max_width)((o), uiS(v), (s))
#define lv_obj_set_style_min_height(o, v, s)        (lv_obj_set_style_min_height)((o), uiS(v), (s))
#define lv_obj_set_style_min_width(o, v, s)         (lv_obj_set_style_min_width)((o), uiS(v), (s))
#define lv_obj_set_style_outline_pad(o, v, s)       (lv_obj_set_style_outline_pad)((o), uiS(v), (s))
#define lv_obj_set_style_outline_width(o, v, s)     (lv_obj_set_style_outline_width)((o), uiS(v), (s))
#define lv_obj_set_style_pad_all(o, v, s)           (lv_obj_set_style_pad_all)((o), uiS(v), (s))
#define lv_obj_set_style_pad_bottom(o, v, s)        (lv_obj_set_style_pad_bottom)((o), uiS(v), (s))
#define lv_obj_set_style_pad_column(o, v, s)        (lv_obj_set_style_pad_column)((o), uiS(v), (s))
#define lv_obj_set_style_pad_hor(o, v, s)           (lv_obj_set_style_pad_hor)((o), uiS(v), (s))
#define lv_obj_set_style_pad_left(o, v, s)          (lv_obj_set_style_pad_left)((o), uiS(v), (s))
#define lv_obj_set_style_pad_right(o, v, s)         (lv_obj_set_style_pad_right)((o), uiS(v), (s))
#define lv_obj_set_style_pad_row(o, v, s)           (lv_obj_set_style_pad_row)((o), uiS(v), (s))
#define lv_obj_set_style_pad_top(o, v, s)           (lv_obj_set_style_pad_top)((o), uiS(v), (s))
#define lv_obj_set_style_pad_ver(o, v, s)           (lv_obj_set_style_pad_ver)((o), uiS(v), (s))
#define lv_obj_set_style_radius(o, v, s)            (lv_obj_set_style_radius)((o), uiS(v), (s))
#define lv_obj_set_style_shadow_spread(o, v, s)     (lv_obj_set_style_shadow_spread)((o), uiS(v), (s))
#define lv_obj_set_style_shadow_width(o, v, s)      (lv_obj_set_style_shadow_width)((o), uiS(v), (s))
#define lv_obj_set_style_shadow_offset_x(o, v, s)   (lv_obj_set_style_shadow_offset_x)((o), uiS(v), (s))
#define lv_obj_set_style_shadow_offset_y(o, v, s)   (lv_obj_set_style_shadow_offset_y)((o), uiS(v), (s))
#define lv_obj_set_style_size(o, w, h, s)           (lv_obj_set_style_size)((o), uiS(w), uiS(h), (s))
#define lv_obj_set_style_text_letter_space(o, v, s) (lv_obj_set_style_text_letter_space)((o), uiS(v), (s))
#define lv_obj_set_style_text_line_space(o, v, s)   (lv_obj_set_style_text_line_space)((o), uiS(v), (s))
#define lv_obj_set_style_translate_x(o, v, s)       (lv_obj_set_style_translate_x)((o), uiS(v), (s))
#define lv_obj_set_style_translate_y(o, v, s)       (lv_obj_set_style_translate_y)((o), uiS(v), (s))
#define lv_obj_set_style_width(o, v, s)             (lv_obj_set_style_width)((o), uiS(v), (s))
#define lv_obj_set_style_height(o, v, s)            (lv_obj_set_style_height)((o), uiS(v), (s))
#define lv_obj_set_style_arc_width(o, v, s)         (lv_obj_set_style_arc_width)((o), uiS(v), (s))

// ── Getters ─────────────────────────────────────────────────────────────────
// lv_disp_get_hor_res / _ver_res are LVGL's v8 names for these two, as
// object-like macros, so they arrive here too.
#define lv_display_get_horizontal_resolution(d) uiU((lv_display_get_horizontal_resolution)(d))
#define lv_display_get_vertical_resolution(d)   uiU((lv_display_get_vertical_resolution)(d))
#define lv_obj_get_width(o)           uiU((lv_obj_get_width)(o))
#define lv_obj_get_height(o)          uiU((lv_obj_get_height)(o))
#define lv_obj_get_content_width(o)   uiU((lv_obj_get_content_width)(o))
#define lv_obj_get_content_height(o)  uiU((lv_obj_get_content_height)(o))
#define lv_obj_get_x(o)               uiU((lv_obj_get_x)(o))
#define lv_obj_get_y(o)               uiU((lv_obj_get_y)(o))
#define lv_obj_get_scroll_y(o)        uiU((lv_obj_get_scroll_y)(o))
#define lv_obj_get_scroll_top(o)      uiU((lv_obj_get_scroll_top)(o))
#define lv_obj_get_scroll_bottom(o)   uiU((lv_obj_get_scroll_bottom)(o))
#define lv_obj_get_coords(o, a)       uiObjGetCoords((o), (a))

#define lv_obj_get_style_border_width(o, p)      uiU((lv_obj_get_style_border_width)((o), (p)))
#define lv_obj_get_style_width(o, p)             uiU((lv_obj_get_style_width)((o), (p)))
#define lv_obj_get_style_height(o, p)            uiU((lv_obj_get_style_height)((o), (p)))
#define lv_obj_get_style_pad_bottom(o, p)        uiU((lv_obj_get_style_pad_bottom)((o), (p)))
#define lv_obj_get_style_pad_left(o, p)          uiU((lv_obj_get_style_pad_left)((o), (p)))
#define lv_obj_get_style_pad_right(o, p)         uiU((lv_obj_get_style_pad_right)((o), (p)))
#define lv_obj_get_style_pad_top(o, p)           uiU((lv_obj_get_style_pad_top)((o), (p)))
#define lv_obj_get_style_text_letter_space(o, p) uiU((lv_obj_get_style_text_letter_space)((o), (p)))
#define lv_obj_get_style_text_line_space(o, p)   uiU((lv_obj_get_style_text_line_space)((o), (p)))

#define lv_font_get_line_height(f)        uiUCeil((lv_font_get_line_height)(f))
#define lv_font_get_glyph_width(f, l, n)  ((uint16_t)uiUCeil((lv_font_get_glyph_width)((f), (l), (n))))
#define lv_text_get_size(r, t, f, ls, lns, mw, fl) uiTextGetSize((r), (t), (f), (ls), (lns), (mw), (fl))

// ── Fonts ───────────────────────────────────────────────────────────────────
// Each name means the face twice its size. Montserrat stops at 48, so 24 and
// everything above it share that face; the large clocks come out a little
// smaller than a true double. References rather than chained macros, so 10 ->
// 20 does not go on to 20 -> 40.
namespace uifont2x {
static const lv_font_t &f8  = lv_font_montserrat_16;
static const lv_font_t &f10 = lv_font_montserrat_20;
static const lv_font_t &f12 = lv_font_montserrat_24;
static const lv_font_t &f14 = lv_font_montserrat_28;
static const lv_font_t &f16 = lv_font_montserrat_32;
static const lv_font_t &f18 = lv_font_montserrat_36;
static const lv_font_t &f20 = lv_font_montserrat_40;
static const lv_font_t &f24 = lv_font_montserrat_48;
}  // namespace uifont2x

#define lv_font_montserrat_8  uifont2x::f8
#define lv_font_montserrat_10 uifont2x::f10
#define lv_font_montserrat_12 uifont2x::f12
#define lv_font_montserrat_14 uifont2x::f14
#define lv_font_montserrat_16 uifont2x::f16
#define lv_font_montserrat_18 uifont2x::f18
#define lv_font_montserrat_20 uifont2x::f20
#define lv_font_montserrat_24 uifont2x::f24
#define lv_font_montserrat_28 uifont2x::f24
#define lv_font_montserrat_32 uifont2x::f24
#define lv_font_montserrat_40 uifont2x::f24

#endif  // UI_COORD_SCALE > 1
