// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Arduino surface for the firmware UI built on Linux (DEVICE_CARDPUTER_ZERO,
// linux/CMakeLists.txt target "camillia"). String/Print/Stream are the
// arduino-esp32 2.0.17 core's own sources; the rest is the subset of the
// esp32 core API the shared sources call, mapped onto Linux or made inert
// where the hardware does not exist here (GPIO interrupts, LEDC, ADC).
//
// ARDUINO is deliberately left undefined: RadioLib and LovyanGFX would
// otherwise pick their Arduino back ends instead of the Linux ones.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <ctype.h>
#include <time.h>
#include <algorithm>
#include <cmath>

#include "pgmspace.h"
#include "esp32-hal.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

typedef bool boolean;
typedef uint8_t byte;
typedef uint16_t word;

#ifdef __cplusplus
#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "WCharacter.h"

using std::min;
using std::max;
using std::abs;
using std::isinf;
using std::isnan;

#define _min(a, b) ((a) < (b) ? (a) : (b))
#define _max(a, b) ((a) > (b) ? (a) : (b))
template <typename T, typename L, typename H>
static inline auto constrain(T x, L lo, H hi) -> decltype(x + lo + hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define bit(b) (1UL << (b))


long random(long max);
long random(long min, long max);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

#define SERIAL_8N1 0x800001c

// UART 0 is the console (stdout; input unused -- the device is driven from its
// own keyboard). Other UART numbers are Linux ttys, opened by begin(); see
// app/serial_tty.cpp for the number-to-device mapping (UART 1 = GPS).
class HardwareSerial : public Stream {
public:
    explicit HardwareSerial(int uart = 0) : _uart(uart) {}
    void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rx = -1, int8_t tx = -1,
               bool invert = false, unsigned long timeoutMs = 20000UL, uint8_t rxfifoFull = 112);
    void end();
    void setRxBufferSize(size_t) {}
    void setTxBufferSize(size_t) {}
    void setTxTimeoutMs(uint32_t) {}
    int available() override;
    int availableForWrite() override { return 4096; }
    int read() override;
    int peek() override;
    void flush() override;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *buf, size_t n) override;
    using Print::write;
    operator bool() const { return _uart == 0 || _fd >= 0; }
    bool isConnected() const { return true; }

private:
    void fill();
    int _uart;
    int _fd = -1;
    uint8_t _buf[512];
    size_t _head = 0, _len = 0;
};
extern HardwareSerial Serial;

class EspClass {
public:
    [[noreturn]] void restart();
    uint32_t getFreeHeap();
    uint32_t getHeapSize();
    uint32_t getMinFreeHeap() { return getFreeHeap(); }
    uint32_t getMaxAllocHeap() { return getFreeHeap(); }
    uint32_t getPsramSize() { return 0; }
    uint32_t getFreePsram() { return 0; }
    uint32_t getFlashChipSize() { return 0; }
    uint32_t getSketchSize() { return 0; }
    uint32_t getFreeSketchSpace() { return 0; }
    uint8_t getChipRevision() { return 0; }
    uint8_t getChipCores() { return 4; }
    uint32_t getCpuFreqMHz() { return 1000; }
    const char *getChipModel() { return "CardputerZero"; }
    uint64_t getEfuseMac();
};
extern EspClass ESP;
#endif  // __cplusplus

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
#define OPEN_DRAIN 0x10
#define OUTPUT_OPEN_DRAIN 0x13
#define RISING 0x01
#define FALLING 0x02
#define CHANGE 0x03
#define ONLOW 0x04
#define ONHIGH 0x05

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// No GPIO on this board's shared paths: every pin they name is -1 (see
// hal/hw_cardputer_zero.h). The radio's pins belong to LinuxHal.
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline int analogRead(int) { return 0; }
inline uint32_t analogReadMilliVolts(int) { return 0; }
inline void analogSetPinAttenuation(int, int) {}
inline void analogReadResolution(int) {}
inline void analogWrite(int, int) {}
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int, void (*)(void), int) {}
inline void detachInterrupt(int) {}
inline void interrupts() {}
inline void noInterrupts() {}
inline uint32_t ledcSetup(uint8_t, uint32_t f, uint8_t) { return f; }
inline void ledcAttachPin(int, uint8_t) {}
inline bool ledcAttach(int, uint32_t, uint8_t) { return false; }
inline void ledcWrite(int, uint32_t) {}
inline double ledcWriteTone(int, uint32_t) { return 0; }
inline void tone(int, unsigned int, unsigned long = 0) {}
inline void noTone(int) {}
inline bool setCpuFrequencyMhz(uint32_t) { return true; }
inline uint32_t getCpuFrequencyMhz() { return 1000; }
inline float temperatureRead() { return NAN; }
inline bool psramFound() { return false; }
inline void *ps_malloc(size_t n) { return malloc(n); }
inline void *ps_calloc(size_t n, size_t s) { return calloc(n, s); }
inline void *ps_realloc(void *p, size_t n) { return realloc(p, n); }

// The loop runs on the process's main thread; its stack is the kernel's.
#define SET_LOOP_TASK_STACK_SIZE(sz)
// Linux keeps its own clock (NTP via systemd-timesyncd); nothing to start.
static inline void configTime(long, int, const char * = nullptr, const char * = nullptr, const char * = nullptr) {}
static inline void configTzTime(const char *tz, const char * = nullptr, const char * = nullptr, const char * = nullptr) {
    if (tz) { setenv("TZ", tz, 1); tzset(); }
}
static inline bool getLocalTime(struct tm *info, uint32_t = 5000) {
    time_t now = time(nullptr);
    if (now < 1600000000) return false;
    localtime_r(&now, info);
    return true;
}
#endif
