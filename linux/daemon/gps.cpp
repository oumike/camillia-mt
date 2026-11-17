// SPDX-License-Identifier: GPL-3.0-or-later
#include "gps.h"

#include <Arduino.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr uint32_t kFixStaleMs = 10000;

speed_t baudConst(int baud) {
    switch (baud) {
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B115200;
    }
}

// ddmm.mmmm + hemisphere -> degrees * 1e7.
bool coord(const char *v, const char *hemi, int32_t &out) {
    if (!*v || !*hemi) return false;
    double raw = atof(v);
    double deg = floor(raw / 100.0);
    double val = deg + (raw - deg * 100.0) / 60.0;
    if (*hemi == 'S' || *hemi == 'W') val = -val;
    out = (int32_t)lround(val * 1e7);
    return true;
}

}  // namespace

bool nmeaChecksumOk(const char *s) {
    if (*s != '$') return false;
    uint8_t sum = 0;
    const char *p = s + 1;
    for (; *p && *p != '*'; p++) sum ^= (uint8_t)*p;
    if (*p != '*') return false;
    return strtoul(p + 1, nullptr, 16) == sum;
}

bool Gps::open(const std::string &device, int baud) {
    _fd = ::open(device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (_fd < 0) {
        fprintf(stderr, "[gps] %s: %s\n", device.c_str(), strerror(errno));
        return false;
    }
    termios t = {};
    tcgetattr(_fd, &t);
    cfmakeraw(&t);
    cfsetispeed(&t, baudConst(baud));
    cfsetospeed(&t, baudConst(baud));
    t.c_cflag |= CLOCAL | CREAD;
    tcsetattr(_fd, TCSANOW, &t);
    tcflush(_fd, TCIFLUSH);
    return true;
}

bool Gps::current() const { return _fix.valid && millis() - _fix.atMs < kFixStaleMs; }

void Gps::feed(const char *data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = data[i];
        if (c == '$') _len = 0;
        if (c == '\r' || c == '\n') {
            if (_len) {
                _buf[_len] = 0;
                sentence(_buf);
                _len = 0;
            }
        } else if (_len + 1 < sizeof _buf) {
            _buf[_len++] = c;
        } else {
            _len = 0;
        }
    }
}

bool Gps::service() {
    char chunk[256];
    ssize_t n;
    while (_fd >= 0 && (n = read(_fd, chunk, sizeof chunk)) > 0) feed(chunk, (size_t)n);
    bool wasValid = _fix.valid;
    if (_fix.valid && millis() - _fix.atMs >= kFixStaleMs) _fix.valid = false;
    bool changed = _changed || wasValid != _fix.valid;
    _changed = false;
    return changed;
}

void Gps::sentence(char *s) {
    if (!nmeaChecksumOk(s)) return;
    *strchr(s, '*') = 0;
    // Split in place; empty fields stay empty strings.
    const char *f[24];
    int nf = 0;
    for (char *p = s; nf < 24;) {
        f[nf++] = p;
        char *comma = strchr(p, ',');
        if (!comma) break;
        *comma = 0;
        p = comma + 1;
    }
    if (nf < 1 || strlen(f[0]) < 6) return;
    const char *type = f[0] + 3;   // skip "$GP" / "$GN" / "$BD"

    if (!strcmp(type, "GGA") && nf >= 10) {
        int quality = atoi(f[6]);
        _fix.sats = atoi(f[7]);
        int32_t lat, lon;
        if (quality > 0 && coord(f[2], f[3], lat) && coord(f[4], f[5], lon)) {
            int32_t alt = (int32_t)lround(atof(f[9]));
            // Only a move of ~1 m or more counts as a change worth telling clients.
            if (!_fix.valid || labs(lat - _fix.latI) > 90 || labs(lon - _fix.lonI) > 90) _changed = true;
            _fix.latI = lat;
            _fix.lonI = lon;
            _fix.altM = alt;
            _fix.valid = true;
            _fix.atMs = millis();
        } else if (_fix.valid) {
            _fix.valid = false;
            _changed = true;
        }
    } else if (!strcmp(type, "RMC") && nf >= 7) {
        if (f[2][0] != 'A' && _fix.valid) {
            _fix.valid = false;
            _changed = true;
        }
    }
}
