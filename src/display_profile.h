#pragma once

#include <stdint.h>

#include "lgfx_tdeck.h"

struct DisplayHeaderProfile {
    bool useCompactStatusFont;
    float statusFontScale;
    int statusTextX;
    int statusFlowerGap;
    int statusTimeGap;
    int statusTimeRightPad;
};

struct DisplayTabsProfile {
    int tabPadX;
    int tabGap;
    int tabEdgePad;
    int tabPillInsetY;
    int tabPillMinHeight;
    int tabLabelYOffset;
};

struct DisplaySplashProfile {
    int cardMarginX;
    int cardTopY;
    int cardBottomMargin;
    int cardRadius;
    float titleFontScale;
    int titleTopInset;
    int flowerOffsetY;
    int idBottomOffset;
    int versionBottomOffset;
    uint16_t holdMs;
};

struct DisplayUiProfile {
    DisplayHeaderProfile header;
    DisplayTabsProfile tabs;
    DisplaySplashProfile splash;
};

struct DisplaySplashPalette {
    uint16_t bgTop;
    uint16_t bgBottom;
    uint16_t cardBg;
    uint16_t cardEdge;
    uint16_t cardEdgeHi;
    uint16_t title;
    uint16_t dim;
    uint16_t bgMain;
};

struct DisplaySplashData {
    const char *nodeLong;
    const char *nodeShort;
    const char *version;
};

const DisplayUiProfile &displayUiProfile();
void displayDrawSplash(LGFX_TDeck &lcd, const DisplayUiProfile &profile,
                       const DisplaySplashPalette &palette,
                       const DisplaySplashData &data);
