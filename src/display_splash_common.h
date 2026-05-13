#pragma once

#include <Arduino.h>

#include "display_profile.h"

namespace display_splash_detail {

inline void drawCamelliaMark(LGFX_TDeck &lcd, int cx, int cy, float scale = 1.0f) {
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

    auto scaled = [&](float value, int minValue = 0) -> int {
        int result = (int)lroundf(value * scale);
        return (result < minValue) ? minValue : result;
    };

    const int shadowDx = scaled(1.0f);
    const int shadowDy = scaled(4.0f);
    const int shadowR = scaled(34.0f, 1);
    const int petalOuterOrbitX = scaled(23.0f, 1);
    const int petalOuterOrbitY = scaled(18.0f, 1);
    const int petalMidOrbitX = scaled(13.0f, 1);
    const int petalMidOrbitY = scaled(10.0f, 1);
    const int petalInnerOrbitX = scaled(6.0f, 1);
    const int petalInnerOrbitY = scaled(5.0f, 1);
    const int petalOuterR0 = scaled(11.0f, 1);
    const int petalOuterR1 = scaled(12.0f, 1);
    const int petalHiliteOuterDx = scaled(2.0f);
    const int petalHiliteOuterDy = scaled(2.0f);
    const int petalHiliteOuterR0 = scaled(7.0f, 1);
    const int petalHiliteOuterR1 = scaled(8.0f, 1);
    const int petalMidR = scaled(9.0f, 1);
    const int petalMidHiliteDx = scaled(1.0f);
    const int petalMidHiliteDy = scaled(1.0f);
    const int petalMidHiliteR = scaled(5.0f, 1);
    const int petalInnerR = scaled(6.0f, 1);
    const int centerR = scaled(6.0f, 1);
    const int centerDotOrbit = scaled(4.0f, 1);
    const int centerDotR = scaled(1.0f, 1);
    const int stemX = scaled(1.0f);
    const int stemY = scaled(20.0f);
    const int stemW = scaled(3.0f, 1);
    const int stemH = scaled(17.0f, 1);
    const int stemRadius = scaled(1.0f, 1);
    const int leafOuterX = scaled(21.0f, 1);
    const int leafOuterYLeft = scaled(28.0f, 1);
    const int leafOuterYRight = scaled(29.0f, 1);
    const int leafOuterR = scaled(8.0f, 1);
    const int leafInnerX = scaled(14.0f, 1);
    const int leafInnerYLeft = scaled(30.0f, 1);
    const int leafInnerYRight = scaled(31.0f, 1);
    const int leafInnerR = scaled(6.0f, 1);

    lcd.fillCircle(cx + shadowDx, cy + shadowDy, shadowR, SHADOW);

    for (int i = 0; i < 10; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
        int px = cx + (int)lroundf((float)petalOuterOrbitX * cosf(a));
        int py = cy + (int)lroundf((float)petalOuterOrbitY * sinf(a));
        int pr = (i & 1) ? petalOuterR1 : petalOuterR0;
        int hiliteR = (i & 1) ? petalHiliteOuterR1 : petalHiliteOuterR0;
        lcd.fillCircle(px, py, pr, PETAL_OUTER);
        lcd.fillCircle(px - petalHiliteOuterDx, py - petalHiliteOuterDy, hiliteR, PETAL_HILITE);
        lcd.drawCircle(px, py, pr, PETAL_EDGE);
    }

    for (int i = 0; i < 8; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
        int px = cx + (int)lroundf((float)petalMidOrbitX * cosf(a));
        int py = cy + (int)lroundf((float)petalMidOrbitY * sinf(a));
        lcd.fillCircle(px, py, petalMidR, PETAL_MID);
        lcd.fillCircle(px - petalMidHiliteDx, py - petalMidHiliteDy, petalMidHiliteR, PETAL_HILITE);
        lcd.drawCircle(px, py, petalMidR, PETAL_EDGE);
    }

    for (int i = 0; i < 5; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 5.0f) + 0.20f;
        int px = cx + (int)lroundf((float)petalInnerOrbitX * cosf(a));
        int py = cy + (int)lroundf((float)petalInnerOrbitY * sinf(a));
        lcd.fillCircle(px, py, petalInnerR, PETAL_INNER);
    }

    lcd.fillCircle(cx, cy, centerR, CENTER);
    lcd.drawCircle(cx, cy, centerR, 0xD4C0);
    for (int i = 0; i < 10; i++) {
        float a = (float)i * 2.0f * (float)M_PI / 10.0f;
        int sx = cx + (int)lroundf((float)centerDotOrbit * cosf(a));
        int sy = cy + (int)lroundf((float)centerDotOrbit * sinf(a));
        lcd.fillCircle(sx, sy, centerDotR, CENTER_DOT);
    }

    lcd.fillRoundRect(cx - stemX, cy + stemY, stemW, stemH, stemRadius, STEM);
    lcd.fillCircle(cx - leafOuterX, cy + leafOuterYLeft, leafOuterR, LEAF_DARK);
    lcd.fillCircle(cx - leafInnerX, cy + leafInnerYLeft, leafInnerR, LEAF_LIGHT);
    lcd.fillCircle(cx + leafOuterX, cy + leafOuterYRight, leafOuterR, LEAF_DARK);
    lcd.fillCircle(cx + leafInnerX, cy + leafInnerYRight, leafInnerR, LEAF_LIGHT);
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

    #if defined(DEVICE_TLORA_PAGER_TFT)
    const int cardOuterPadX = 6;
    const int cardOuterPadY = 8;
    const int cardX = cardOuterPadX;
    const int cardY = cardOuterPadY;
    const int cardW = screenW - cardOuterPadX * 2;
    const int cardH = screenH - cardOuterPadY * 2;
    #else
    const int cardX = splash.cardMarginX;
    const int cardY = splash.cardTopY;
    const int cardW = screenW - splash.cardMarginX * 2;
    const int cardH = screenH - splash.cardTopY - splash.cardBottomMargin;
    #endif

    lcd.fillRoundRect(cardX, cardY, cardW, cardH, splash.cardRadius, palette.cardBg);
    lcd.drawRoundRect(cardX, cardY, cardW, cardH, splash.cardRadius, palette.cardEdge);
    lcd.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, splash.cardRadius, palette.cardEdgeHi);

    lcd.setFont(&fonts::Orbitron_Light_32);
    lcd.setTextSize(splash.titleFontScale);
    lcd.setTextColor(palette.title, palette.cardBg);
    const char *appName = "CAMILLIA";
    int tw = lcd.textWidth(appName);
    #if defined(DEVICE_TLORA_PAGER_TFT)
    const int contentInset = max(10, splash.cardRadius + 2);
    const int paneGap = 10;
    const int contentW = cardW - (contentInset * 2) - paneGap;
    const int leftX = cardX + contentInset;
    const int leftW = (contentW * 46) / 100;
    const int rightX = leftX + leftW + paneGap;
    const int rightW = cardX + cardW - contentInset - rightX;

    float titleScale = splash.titleFontScale * 1.35f;
    if (titleScale < 1.2f) titleScale = 1.2f;
    lcd.setTextSize(titleScale);
    tw = lcd.textWidth(appName);
    const int titleY = cardY + max(8, splash.titleTopInset - 2);
    const int titleX = leftX + max(0, (leftW - tw) / 2);
    lcd.drawString(appName, titleX, titleY);

    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    String verInline(data.version);
    const int rightEdgeX = cardX + cardW - contentInset;
    int verX = titleX + tw + 10;
    int verMaxW = max(10, rightEdgeX - verX);
    if (lcd.textWidth(verInline.c_str()) > verMaxW) {
        while (verInline.length() > 3 && lcd.textWidth((verInline + "...").c_str()) > verMaxW) {
            verInline.remove(verInline.length() - 1);
        }
        verInline += "...";
    }
    lcd.setTextColor(palette.dim, palette.cardBg);
    lcd.drawString(verInline.c_str(), verX, titleY + 10);

    lcd.setFont(&fonts::Orbitron_Light_32);
    lcd.setTextSize(titleScale);
    const int titleH = lcd.fontHeight();
    const int flowerAreaTop = titleY + titleH + 8;
    const int flowerAreaBottom = cardY + cardH - 12;
    const int flowerHalfH = max(18, (flowerAreaBottom - flowerAreaTop) / 2);
    const int flowerHalfW = max(18, (leftW / 2) - 12);
    float flowerScale = min((float)flowerHalfH / 38.0f, (float)flowerHalfW / 35.0f);
    if (flowerScale < 1.12f) flowerScale = 1.12f;
    if (flowerScale > 1.70f) flowerScale = 1.70f;
    const int flowerRadius = max(14, (int)lroundf(35.0f * flowerScale));
    const int flowerCx = leftX + flowerRadius + 6 + splash.flowerOffsetX;

    drawCamelliaMark(lcd,
                     flowerCx,
                     (flowerAreaTop + flowerAreaBottom) / 2 + splash.flowerOffsetY,
                     flowerScale);

    char idBuf[44];
    snprintf(idBuf, sizeof(idBuf), "%s  (%s)", data.nodeLong, data.nodeShort);

    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    String idText(idBuf);
    if (lcd.textWidth(idText.c_str()) > rightW) {
        while (idText.length() > 3 && lcd.textWidth((String("...") + idText).c_str()) > rightW) {
            idText.remove(0, 1);
        }
        idText = String("...") + idText;
    }

    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    lcd.setTextColor(palette.title, palette.cardBg);
    const int idW = lcd.textWidth(idText.c_str());
    const int idX = rightEdgeX - idW;
    lcd.drawString(idText.c_str(), idX, cardY + cardH - splash.idBottomOffset);
    #else
    lcd.drawString(appName, (screenW - tw) / 2, cardY + splash.titleTopInset);

    drawCamelliaMark(lcd,
                     screenW / 2 + splash.flowerOffsetX,
                     cardY + cardH / 2 + splash.flowerOffsetY,
                     splash.flowerScale);

    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    char idBuf[44];
    snprintf(idBuf, sizeof(idBuf), "%s  (%s)", data.nodeLong, data.nodeShort);
    lcd.setTextColor(palette.title, palette.cardBg);
    tw = lcd.textWidth(idBuf);
    lcd.drawString(idBuf, (screenW - tw) / 2, cardY + cardH - splash.idBottomOffset);

    lcd.setTextColor(palette.dim, palette.cardBg);
    tw = lcd.textWidth(data.version);
    lcd.drawString(data.version, (screenW - tw) / 2, cardY + cardH - splash.versionBottomOffset);
    #endif

    lcd.setFont(UI_BODY_FONT);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    if (splash.holdOnScreen) {
        while (true) delay(1000);
    }

    delay(splash.holdMs);
    lcd.fillScreen(palette.bgMain);
}

}  // namespace display_splash_detail