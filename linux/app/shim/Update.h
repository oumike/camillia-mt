// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// arduino-esp32's Update, inert: OTA images are ESP32 binaries, and this app
// is updated through its package instead.
#include "Arduino.h"

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#define U_FLASH 0
#define UPDATE_ERROR_ABORT 8
class UpdateClass {
public:
    bool begin(size_t = UPDATE_SIZE_UNKNOWN, int = U_FLASH, int = -1, uint8_t = 0, const char * = nullptr) { return false; }
    size_t write(uint8_t *, size_t) { return 0; }
    size_t writeStream(Stream &) { return 0; }
    bool end(bool = false) { return false; }
    void abort() {}
    bool isFinished() { return false; }
    bool hasError() { return true; }
    uint8_t getError() { return UPDATE_ERROR_ABORT; }
    const char *errorString() { return "OTA is not available on this device"; }
    void printError(Print &) {}
};
inline UpdateClass Update;
