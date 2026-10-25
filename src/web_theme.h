#pragma once
// The device's theme as CSS custom properties for the web config page, so the
// page wears the same colours as the screen in your hand. Arduino-free: the
// palette comes in as RGB565 from main_lvgl.cpp (webCfgThemeColors()) and the
// host tests drive this directly.

#include <stddef.h>
#include <stdint.h>

// RGB565, as in main_lvgl.cpp's UiPalette.
struct WebThemeColors {
    uint16_t bg, panel, panelAlt, panelStrong, divider, dividerHi, inputBg,
             accent, text, textDim, onAccent, good, warn, bad;
    bool isLight;
};

// WCAG 2 contrast ratio of two colours, from 1 (same) to 21 (black on white).
double webThemeContrast(uint16_t a, uint16_t b);

// Writes ":root{color-scheme:…;--bg:#rrggbb;…}" into out. Returns its length,
// or 0 when it does not fit, in which case out is left untouched. Text below
// 4.5:1 against the background is replaced by Camillia's text pair for the
// mode, so a custom theme can never make the page unreadable.
size_t webThemeCssVars(const WebThemeColors &c, char *out, size_t outLen);
