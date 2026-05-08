#include "display_profile.h"
#include "config.h"
#include "display_splash_common.h"

#if defined(DEVICE_CARDPUTER_LORA_HAT)
namespace {
constexpr DisplayUiProfile kDisplayUiProfile = {
    {
        true,
        1.0f,
        2,
        5,
        6,
        6,
    },
    {
        4,
        2,
        1,
        2,
        8,
        0,
    },
    {
        10,
        16,
        8,
        6,
        0.68f,
        10,
        0,
        -1,
        0.44f,
        24,
        12,
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
