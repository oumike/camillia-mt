#pragma once

#include <Arduino.h>

#include "display_profile.h"

namespace display_splash_detail {

inline void drawCamelliaMark(LGFX_TDeck &lcd, int cx, int cy) {
    const uint16_t SHADOW       = 0x18E4;
    const uint16_t PETAL_OUTER  = 0xF9CF;
    const uint16_t PETAL_MID    = 0xFADF;
    const uint16_t PETAL_INNER  = 0xFF7D;
    const uint16_t PETAL_HILITE = 0xFFDF;
    const uint16_t PETAL_EDGE   = 0xD8A7;
    const uint16_t CENTER       = 0xFD20;
    const uint16_t CENTER_DOT   = 0xFEA0;
    const uint16_t STEM         = 0x64EC;
    const uint16_t LEAF_DARK    = 0x2C87;
    const uint16_t LEAF_LIGHT   = 0x3D68;

    lcd.fillCircle(cx + 1, cy + 4, 34, SHADOW);

    for (int i = 0; i < 10; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
        int px = cx + (int)(23.0f * cosf(a));
        int py = cy + (int)(18.0f * sinf(a));
        int pr = 11 + (i & 1);
        lcd.fillCircle(px, py, pr, PETAL_OUTER);
        lcd.fillCircle(px - 2, py - 2, pr - 4, PETAL_HILITE);
        lcd.drawCircle(px, py, pr, PETAL_EDGE);
    }

    for (int i = 0; i < 8; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
        int px = cx + (int)(13.0f * cosf(a));
        int py = cy + (int)(10.0f * sinf(a));
        lcd.fillCircle(px, py, 9, PETAL_MID);
        lcd.fillCircle(px - 1, py - 1, 5, PETAL_HILITE);
        lcd.drawCircle(px, py, 9, PETAL_EDGE);
    }

    for (int i = 0; i < 5; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 5.0f) + 0.20f;
        int px = cx + (int)(6.0f * cosf(a));
        int py = cy + (int)(5.0f * sinf(a));
        lcd.fillCircle(px, py, 6, PETAL_INNER);
    }

    lcd.fillCircle(cx, cy, 6, CENTER);
    lcd.drawCircle(cx, cy, 6, 0xD4C0);
    for (int i = 0; i < 10; i++) {
        float a = (float)i * 2.0f * (float)M_PI / 10.0f;
        int sx = cx + (int)(4.0f * cosf(a));
        int sy = cy + (int)(4.0f * sinf(a));
        lcd.fillCircle(sx, sy, 1, CENTER_DOT);
    }

    lcd.fillRoundRect(cx - 1, cy + 20, 3, 17, 1, STEM);
    lcd.fillCircle(cx - 21, cy + 28, 8, LEAF_DARK);
    lcd.fillCircle(cx - 14, cy + 30, 6, LEAF_LIGHT);
    lcd.fillCircle(cx + 21, cy + 29, 8, LEAF_DARK);
    lcd.fillCircle(cx + 14, cy + 31, 6, LEAF_LIGHT);
}

inline void drawSplashCommon(LGFX_TDeck &lcd, const DisplayUiProfile &profile,
                             const DisplaySplashPalette &palette,
                             const DisplaySplashData &data) {
    const DisplaySplashProfile &splash = profile.splash;
    const int screenW = lcd.width();
    const int screenH = lcd.height();

    for (int y = 0; y < screenH; y++) {
        int r1 = (palette.bgTop >> 11) & 0x1F;
        int g1 = (palette.bgTop >> 5) & 0x3F;
        int b1 = palette.bgTop & 0x1F;
        int r2 = (palette.bgBottom >> 11) & 0x1F;
        int g2 = (palette.bgBottom >> 5) & 0x3F;
        int b2 = palette.bgBottom & 0x1F;
        int r = r1 + ((r2 - r1) * y) / max(1, screenH - 1);
        int g = g1 + ((g2 - g1) * y) / max(1, screenH - 1);
        int b = b1 + ((b2 - b1) * y) / max(1, screenH - 1);
        uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
        lcd.drawFastHLine(0, y, screenW, c);
    }

    const int cardX = splash.cardMarginX;
    const int cardY = splash.cardTopY;
    const int cardW = screenW - splash.cardMarginX * 2;
    const int cardH = screenH - splash.cardTopY - splash.cardBottomMargin;

    lcd.fillRoundRect(cardX, cardY, cardW, cardH, splash.cardRadius, palette.cardBg);
    lcd.drawRoundRect(cardX, cardY, cardW, cardH, splash.cardRadius, palette.cardEdge);
    lcd.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, splash.cardRadius, palette.cardEdgeHi);

    lcd.setFont(&fonts::Orbitron_Light_32);
    lcd.setTextSize(splash.titleFontScale);
    lcd.setTextColor(palette.title, palette.cardBg);
    const char *appName = "CAMILLIA";
    int tw = lcd.textWidth(appName);
    lcd.drawString(appName, (screenW - tw) / 2, cardY + splash.titleTopInset);

    drawCamelliaMark(lcd, screenW / 2, cardY + cardH / 2 + splash.flowerOffsetY);

    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

    char idBuf[44];
    snprintf(idBuf, sizeof(idBuf), "%s  (%s)", data.nodeLong, data.nodeShort);
    lcd.setTextColor(palette.title, palette.cardBg);
    tw = lcd.textWidth(idBuf);
    lcd.drawString(idBuf, (screenW - tw) / 2, cardY + cardH - splash.idBottomOffset);

    lcd.setTextColor(palette.dim, palette.cardBg);
    tw = lcd.textWidth(data.version);
    lcd.drawString(data.version, (screenW - tw) / 2, cardY + cardH - splash.versionBottomOffset);

    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);

    delay(splash.holdMs);
    lcd.fillScreen(palette.bgMain);
}

}  // namespace display_splash_detail