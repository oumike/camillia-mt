#pragma once

// Own-message colour choices (RhinoConfig::userMsgColor), shared by the
// on-device picker and Web Config so the two list the same colours.
//
//   0..15                 an entry of kUserMsgPalette
//   USER_MSG_COLOR_CUSTOM RhinoConfig::userMsgCustomRgb
//   anything else         the adaptive theme default (USER_MSG_COLOR_DEFAULT)

#include <stdint.h>
#include <ctype.h>

static constexpr uint8_t USER_MSG_COLOR_CUSTOM  = 0xFE;
static constexpr uint8_t USER_MSG_COLOR_DEFAULT = 0xFF;
static constexpr uint32_t USER_MSG_CUSTOM_RGB_DEFAULT = 0xFFE000;

struct UserMsgPaletteEntry {
    uint32_t rgb;        // 0xRRGGBB
    const char *name;    // English; on-device it is drawn through TR()
};

// Names are bare literals so this header needs nothing from i18n.h; the
// matching TR_NOOP keys live beside the picker in main_lvgl.cpp.
static constexpr UserMsgPaletteEntry kUserMsgPalette[] = {
    {0xFFFFFF, "White"},
    {0xC0C0C0, "Silver"},
    {0xFF3B30, "Red"},
    {0xFF9500, "Orange"},
    {0xFFE000, "Yellow"},
    {0x7CFC00, "Lime"},
    {0x34C759, "Green"},
    {0x30D0C0, "Teal"},
    {0x00E5FF, "Cyan"},
    {0x5AC8FA, "Sky"},
    {0x3B82F6, "Blue"},
    {0x5E5CE6, "Indigo"},
    {0xAF52DE, "Purple"},
    {0xFF2D95, "Magenta"},
    {0xFF6FB5, "Pink"},
    {0xB5651D, "Brown"},
};

static constexpr int kUserMsgPaletteCount =
    (int)(sizeof(kUserMsgPalette) / sizeof(kUserMsgPalette[0]));

// Accepts "RRGGBB" or "#RRGGBB", surrounding spaces allowed. Nothing is
// written to *out unless the whole string is a valid colour.
inline bool userMsgParseHexRgb(const char *s, uint32_t *out) {
    if (!s || !out) return false;
    while (*s == ' ') s++;
    if (*s == '#') s++;
    uint32_t v = 0;
    int n = 0;
    for (; isxdigit((unsigned char)*s); s++, n++) {
        if (n >= 6) return false;
        const char c = (char)tolower((unsigned char)*s);
        v = (v << 4) | (uint32_t)((c <= '9') ? (c - '0') : (c - 'a' + 10));
    }
    while (*s == ' ') s++;
    if (*s || n != 6) return false;
    *out = v;
    return true;
}
