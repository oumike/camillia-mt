// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// TwoWire on Linux i2c-dev (app/wire_linux.cpp). Wire is /dev/i2c-1, the bus
// the LoRa cap's expander sits on; Wire1 is the same bus. Pin and clock
// arguments are accepted and ignored: the kernel owns both.
#include "Arduino.h"

class TwoWire : public Stream {
public:
    explicit TwoWire(const char *dev) : _dev(dev) {}
    bool begin(int = -1, int = -1, uint32_t = 0);
    bool begin(uint8_t) { return begin(); }
    bool end();
    bool setClock(uint32_t) { return true; }
    uint32_t getClock() { return 100000; }
    void setTimeOut(uint16_t) {}
    uint16_t getTimeOut() { return 50; }
    void beginTransmission(uint16_t address);
    void beginTransmission(uint8_t address) { beginTransmission((uint16_t)address); }
    void beginTransmission(int address) { beginTransmission((uint16_t)address); }
    uint8_t endTransmission(bool sendStop);
    uint8_t endTransmission() { return endTransmission(true); }
    size_t requestFrom(uint16_t address, size_t size, bool sendStop);
    uint8_t requestFrom(uint16_t address, uint8_t size, bool sendStop) {
        return (uint8_t)requestFrom(address, (size_t)size, sendStop);
    }
    uint8_t requestFrom(uint16_t address, uint8_t size, uint8_t sendStop) {
        return (uint8_t)requestFrom(address, (size_t)size, sendStop != 0);
    }
    uint8_t requestFrom(uint16_t address, uint8_t size) { return requestFrom(address, size, true); }
    uint8_t requestFrom(uint8_t address, uint8_t size) { return requestFrom((uint16_t)address, size, true); }
    uint8_t requestFrom(uint8_t address, uint8_t size, uint8_t sendStop) {
        return requestFrom((uint16_t)address, size, sendStop);
    }
    uint8_t requestFrom(int address, int size) { return requestFrom((uint16_t)address, (uint8_t)size, true); }
    uint8_t requestFrom(int address, int size, int sendStop) {
        return requestFrom((uint16_t)address, (uint8_t)size, (uint8_t)sendStop);
    }
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t n) override;
    int available() override { return (int)(_rxLen - _rxPos); }
    int read() override { return _rxPos < _rxLen ? _rx[_rxPos++] : -1; }
    int peek() override { return _rxPos < _rxLen ? _rx[_rxPos] : -1; }
    void flush() override {}
    using Print::write;

private:
    const char *_dev;
    int _fd = -1;
    uint16_t _addr = 0;
    bool _txOpen = false;
    uint8_t _tx[128];
    size_t _txLen = 0;
    bool _pendingWrite = false;   // endTransmission(false): write held for a repeated start
    uint8_t _rx[128];
    size_t _rxLen = 0, _rxPos = 0;
};

extern TwoWire Wire;
extern TwoWire Wire1;
