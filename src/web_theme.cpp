#include "web_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Widened the same way as rgb565To888() in web_config.cpp, so a colour reads
// the same here as in the theme editor's colour inputs.
static uint32_t to888(uint16_t c) {
    const uint32_t r = (((c >> 11) & 0x1F) * 255U) / 31U;
    const uint32_t g = (((c >> 5) & 0x3F) * 255U) / 63U;
    const uint32_t b = ((c & 0x1F) * 255U) / 31U;
    return (r << 16) | (g << 8) | b;
}

static double channelLum(uint32_t v) {
    const double s = (double)v / 255.0;
    return s <= 0.03928 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}

static double luminance(uint16_t c) {
    const uint32_t rgb = to888(c);
    return 0.2126 * channelLum((rgb >> 16) & 0xFF)
         + 0.7152 * channelLum((rgb >> 8) & 0xFF)
         + 0.0722 * channelLum(rgb & 0xFF);
}

double webThemeContrast(uint16_t a, uint16_t b) {
    const double la = luminance(a), lb = luminance(b);
    const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

// RGB565 moved a fraction toward black (dark=true) or white, in 888 space and
// back. Only for the status colours, so precision is not a concern.
static uint16_t shade(uint16_t c, double t, bool dark) {
    const uint32_t rgb = to888(c);
    uint32_t ch[3] = {(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF};
    for (uint32_t &v : ch) v = dark ? (uint32_t)(v * (1.0 - t)) : (uint32_t)(v + (255 - v) * t);
    return (uint16_t)(((ch[0] & 0xF8) << 8) | ((ch[1] & 0xFC) << 3) | (ch[2] >> 3));
}

// Battery greens and ambers are tuned to glow on the device's dark status bar.
// On a light page they fade out, so walk each toward black (or white, on a dark
// page that somehow has a pale one) until it reads at 3:1 against the page.
static uint16_t readableStatus(uint16_t c, uint16_t bg, bool isLight) {
    uint16_t out = c;
    for (int i = 1; i <= 10 && webThemeContrast(out, bg) < 3.0; i++) out = shade(c, i * 0.1, isLight);
    return out;
}

size_t webThemeCssVars(const WebThemeColors &c, char *out, size_t outLen) {
    char text[8], dim[8];
    if (webThemeContrast(c.text, c.bg) < 4.5) {
        strcpy(text, c.isLight ? "#2a1d24" : "#f4f6fb");
        strcpy(dim,  c.isLight ? "#76626d" : "#b0b8c8");
    } else {
        snprintf(text, sizeof(text), "#%06lx", (unsigned long)to888(c.text));
        snprintf(dim,  sizeof(dim),  "#%06lx", (unsigned long)to888(c.textDim));
    }
#define HEX(v) (unsigned long)to888(v)
    char buf[512];
    const int n = snprintf(buf, sizeof(buf),
        ":root{color-scheme:%s;--bg:#%06lx;--panel:#%06lx;--panel-2:#%06lx;"
        "--panel-3:#%06lx;--line:#%06lx;--line-hi:#%06lx;--input:#%06lx;"
        "--accent:#%06lx;--text:%s;--dim:%s;--on-accent:#%06lx;"
        "--good:#%06lx;--warn:#%06lx;--bad:#%06lx}",
        c.isLight ? "light" : "dark",
        HEX(c.bg), HEX(c.panel), HEX(c.panelAlt), HEX(c.panelStrong),
        HEX(c.divider), HEX(c.dividerHi), HEX(c.inputBg), HEX(c.accent),
        text, dim, HEX(c.onAccent), HEX(readableStatus(c.good, c.bg, c.isLight)),
        HEX(readableStatus(c.warn, c.bg, c.isLight)), HEX(readableStatus(c.bad, c.bg, c.isLight)));
#undef HEX
    if (n <= 0 || (size_t)n >= sizeof(buf) || (size_t)n >= outLen) return 0;
    memcpy(out, buf, (size_t)n + 1);
    return (size_t)n;
}

uint32_t webAssetHash(const char *s) {
    uint32_t h = 0x811c9dc5u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 0x01000193u;
    }
    return h;
}
