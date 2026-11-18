// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_idf_linux.h"
#include "esp32-hal-log.h"
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_ATTR
#define EXT_RAM_BSS_ATTR
#define ARDUINO_ISR_ATTR
