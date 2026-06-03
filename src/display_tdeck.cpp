#include "display_profile.h"
#include "config.h"
#include "display_splash_common.h"

#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
namespace {
constexpr DisplayUiProfile kDisplayUiProfile = {
    {
        false,
        1.0f,
        2,
        9,
        10,
        4,
    },
    {
        5,
        3,
        2,
        2,
        8,
        1,
    },
    {
        16,
        26,
        26,
        8,
        1.0f,
        12,
        0,
        -4,
    #if defined(DEVICE_TDECK)
        1.15f,
    #else
        1.0f,
    #endif
        28,
        15,
        1500,
        false,
    },
};
}

const DisplayUiProfile &displayUiProfile() {
    return kDisplayUiProfile;
}

void displayDrawSplash(LGFX_TDeck &lcd, const DisplayUiProfile &profile,
                       const DisplaySplashPalette &palette,
                       const DisplaySplashData &data) {
    display_splash_detail::drawSplashCommon(lcd, profile, palette, data);
}
#endif
