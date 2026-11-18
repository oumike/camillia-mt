// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino core functions for the CardputerZero app: time, random, ESP.
#include <Arduino.h>
#include <RNG.h>
#include <esp_random.h>

#include <sched.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

static uint64_t monoUs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
static const uint64_t s_startUs = monoUs();

extern "C" {
uint32_t millis(void) { return (uint32_t)((monoUs() - s_startUs) / 1000); }
uint32_t micros(void) { return (uint32_t)(monoUs() - s_startUs); }
void delay(uint32_t ms) { usleep((useconds_t)ms * 1000); }
void delayMicroseconds(uint32_t us) { usleep(us); }
void yield(void) { sched_yield(); }
int64_t esp_timer_get_time(void) { return (int64_t)(monoUs() - s_startUs); }

void esp_fill_random(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        ssize_t n = getrandom(p, len, 0);
        if (n <= 0) abort();
        p += n;
        len -= (size_t)n;
    }
}
uint32_t esp_random(void) {
    uint32_t v;
    esp_fill_random(&v, sizeof v);
    return v;
}
}

// Arduino's random() is seeded and repeatable on request; the firmware only
// asks it for jitter and ids, so the kernel CSPRNG serves every call.
long random(long max) { return max <= 0 ? 0 : (long)(esp_random() % (uint32_t)max); }
long random(long min, long max) { return max <= min ? min : min + random(max - min); }
void randomSeed(unsigned long) {}
long map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) return out_min;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// The Crypto library's RNG.cpp is built around Arduino EEPROM/flash seeding;
// Curve25519 and Ed25519 only need rand(), which the kernel supplies.
RNGClass CryptRNG;
RNGClass::RNGClass() : credits(0), firstSave(1), initialized(1), trngPending(0) {}
RNGClass::~RNGClass() {}
void RNGClass::rand(uint8_t *data, size_t len) { esp_fill_random(data, len); }

EspClass ESP;

[[noreturn]] void EspClass::restart() { esp_restart(); }
uint32_t EspClass::getFreeHeap() { return esp_get_free_heap_size(); }
uint32_t EspClass::getHeapSize() {
    long pages = sysconf(_SC_PHYS_PAGES), sz = sysconf(_SC_PAGESIZE);
    uint64_t b = (pages > 0 && sz > 0) ? (uint64_t)pages * (uint64_t)sz : 0;
    return b > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)b;
}
uint64_t EspClass::getEfuseMac() {
    uint8_t m[6];
    esp_efuse_mac_get_default(m);
    uint64_t v = 0;
    for (int i = 5; i >= 0; i--) v = (v << 8) | m[i];
    return v;
}

// newlib has these and the Arduino core's WString uses them; glibc does not.
extern "C" char *ltoa(long value, char *result, int base);
extern "C" char *ultoa(unsigned long value, char *result, int base);
extern "C" char *itoa(int val, char *s, int radix) { return ltoa(val, s, radix); }
extern "C" char *utoa(unsigned int val, char *s, int radix) { return ultoa(val, s, radix); }
