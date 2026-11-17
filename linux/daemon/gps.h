// SPDX-License-Identifier: GPL-3.0-or-later
// NMEA reader for the cap's L76K GPS on a serial port (GGA and RMC only).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

struct GpsFix {
    bool valid = false;
    int32_t latI = 0, lonI = 0;   // degrees * 1e7
    int32_t altM = 0;
    int sats = 0;
    uint32_t atMs = 0;            // millis() of the last valid fix
};

class Gps {
public:
    bool open(const std::string &device, int baud);
    int fd() const { return _fd; }
    // Reads what is waiting; true when the fix changed validity or moved.
    bool service();
    // Parses NMEA text as if read from the port.
    void feed(const char *data, size_t n);
    const GpsFix &fix() const { return _fix; }
    // A fix older than this is treated as lost.
    bool current() const;

private:
    void sentence(char *s);
    int _fd = -1;
    char _buf[128];
    size_t _len = 0;
    GpsFix _fix;
    bool _changed = false;
};

// Checks a "$...*hh" sentence's checksum; exposed for tests.
bool nmeaChecksumOk(const char *s);
