// SPDX-License-Identifier: GPL-3.0-or-later
// LVGL 9.5 configuration for camillia-czero-ui (CardputerZero, Linux fbdev).
// Anything not set here takes LVGL's default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 20

#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_BSD 0
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT 1
#define LV_LINUX_FBDEV_BUFFER_SIZE 60

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// The firmware's Latin-extended Montserrat (src/fonts/latin), in place of
// LVGL's ASCII-only copies, so accented names and messages render.
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_CUSTOM_DECLARE \
    LV_FONT_DECLARE(lv_font_montserrat_10) \
    LV_FONT_DECLARE(lv_font_montserrat_12) \
    LV_FONT_DECLARE(lv_font_montserrat_14)
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif
