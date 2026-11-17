// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Minimal Arduino surface for the shared mesh sources built on Linux
// (DEVICE_CARDPUTER_ZERO). Only what mesh_radio/mesh_proto/xeddsa_sig/
// debug_flags actually call; anything else should fail to compile so it is
// noticed rather than silently stubbed.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define RISING 1
#define FALLING 2

using std::min;
using std::max;

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

// Board GPIO is owned by LinuxHal (RadioLib) on this target; these exist so
// shared code that touches optional pins compiles. All such pins are -1 here.
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }

class LinuxSerial {
public:
    void begin(unsigned long) {}
    int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    size_t print(const char *s);
    size_t println(const char *s = "");
    size_t println(int v);
    void flush();
};
extern LinuxSerial Serial;
