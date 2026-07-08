#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 0
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define LV_MEM_SIZE (96U * 1024U)
#else
#define LV_MEM_SIZE (128U * 1024U)
#endif

// --- TEMPORARY DEBUG: hunt the live-screen pool corruption ---------------------
// Validate the LVGL pool on every alloc/free so a corrupting write is caught at
// the NEXT lv_mem op (near the real culprit) instead of much later when the live
// screen's big allocation trips over a poisoned free-list. abort() gives a clean
// ESP32 backtrace. Remove this block once the root cause is fixed — it noticeably
// slows the UI.
#define LV_USE_ASSERT_MEM_INTEGRITY 1
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();

#define LV_USE_LOG 0

#define LV_USE_PNG 1

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1

#define LV_FONT_DEFAULT &lv_font_montserrat_16

#endif
