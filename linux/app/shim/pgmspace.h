// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string.h>
#define PROGMEM
#define PGM_P const char *
#define PSTR(s) (s)
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_float(a) (*(const float *)(a))
#define pgm_read_ptr(a) (*(void *const *)(a))
#define strlen_P strlen
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define sprintf_P sprintf
#define snprintf_P snprintf
#define vsnprintf_P vsnprintf

// memcpy_P / memcmp_P: LovyanGFX defines them as inline functions whenever
// ARDUINO is undefined, so take its definitions rather than clash with them.
#include <lgfx/utility/pgmspace.h>
