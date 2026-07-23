#pragma once
// Monochrome emoji rendering support.
//
// LVGL resolves a glyph the current font lacks by walking lv_font_t::fallback.
// We can't set that field on the const built-in Montserrat faces, so at startup
// we make a mutable copy of each text size and point its fallback at an
// lv_tiny_ttf instance of the same size, backed by a flash-resident Noto Emoji
// face (src/fonts/emoji_font.h). Text labels then draw emoji inline wherever the
// message, name, or preview contains them — no per-call-site handling.
#include <lvgl.h>

// Build the emoji-enabled Montserrat copies + tiny_ttf instances. Call once
// after lv_init() and before any UI is built. Safe to call more than once.
void emojiFontInit();

// Returns the emoji-enabled equivalent of a Montserrat text face. `base` is one
// of the &lv_font_montserrat_* pointers used for chat/DM/node text. Unknown
// fonts (or a build with emoji disabled) return `base` unchanged, so callers can
// wrap unconditionally.
const lv_font_t *emojiFont(const lv_font_t *base);
