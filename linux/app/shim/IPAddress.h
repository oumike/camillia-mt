// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32's IPv4 IPAddress: four octets held in network order, so
// (uint32_t)ip is directly an in_addr_t.
#include <stdio.h>

#include "Arduino.h"

class IPAddress : public Printable {
public:
    IPAddress() : _a(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _a((uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)) {}
    IPAddress(uint32_t a) : _a(a) {}
    IPAddress(const uint8_t *a) : IPAddress(a[0], a[1], a[2], a[3]) {}
    operator uint32_t() const { return _a; }
    bool operator==(const IPAddress &o) const { return _a == o._a; }
    bool operator!=(const IPAddress &o) const { return _a != o._a; }
    bool operator==(const uint8_t *a) const { return *this == IPAddress(a); }
    uint8_t operator[](int i) const { return (uint8_t)(_a >> (8 * i)); }
    IPAddress &operator=(const uint8_t *a) { return *this = IPAddress(a); }
    IPAddress &operator=(uint32_t a) { _a = a; return *this; }
    bool fromString(const char *s) {
        unsigned a, b, c, d;
        char tail;
        if (!s || sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 ||
            c > 255 || d > 255)
            return false;
        *this = IPAddress(a, b, c, d);
        return true;
    }
    bool fromString(const String &s) { return fromString(s.c_str()); }
    String toString() const {
        char b[16];
        snprintf(b, sizeof b, "%u.%u.%u.%u", (*this)[0], (*this)[1], (*this)[2], (*this)[3]);
        return String(b);
    }
    size_t printTo(Print &p) const override { return p.print(toString()); }

private:
    uint32_t _a;
};

// <netinet/in.h> defines INADDR_NONE as a macro; include IPAddress.h first.
#ifndef INADDR_NONE
static const IPAddress INADDR_NONE(0, 0, 0, 0);
#endif
