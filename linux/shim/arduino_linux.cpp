// SPDX-License-Identifier: GPL-3.0-or-later
#include <Arduino.h>
#include <esp_random.h>
#include <RNG.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

LinuxSerial Serial;

static uint64_t monoUs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
static const uint64_t s_startUs = monoUs();

uint32_t millis() { return (uint32_t)((monoUs() - s_startUs) / 1000); }
uint32_t micros() { return (uint32_t)(monoUs() - s_startUs); }
void delay(uint32_t ms) { usleep((useconds_t)ms * 1000); }
void delayMicroseconds(uint32_t us) { usleep(us); }

int LinuxSerial::printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    return n;
}
size_t LinuxSerial::print(const char *s) { fputs(s, stdout); fflush(stdout); return strlen(s); }
size_t LinuxSerial::println(const char *s) { puts(s); fflush(stdout); return strlen(s) + 1; }
size_t LinuxSerial::println(int v) { return printf("%d\n", v); }
void LinuxSerial::flush() { fflush(stdout); }

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

// The Crypto library's RNG.cpp is built around Arduino EEPROM/flash seeding.
// Curve25519::dh1 and Ed25519 only need rand(), which the kernel CSPRNG
// supplies directly.
RNGClass CryptRNG;
RNGClass::RNGClass() : credits(0), firstSave(1), initialized(1), trngPending(0) {}
RNGClass::~RNGClass() {}
void RNGClass::rand(uint8_t *data, size_t len) { esp_fill_random(data, len); }
