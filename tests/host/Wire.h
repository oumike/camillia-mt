#pragma once
// The T-Deck ADC battery path includes Wire.h but makes no I2C calls. Fuel-gauge
// boards do; their tests define these members as a fake gauge on the bus.
#include <cstddef>
#include <cstdint>

class TwoWire {
public:
    void beginTransmission(uint8_t addr);
    size_t write(uint8_t b);
    uint8_t endTransmission(bool stop = true);
    uint8_t requestFrom(int addr, int len);
    int read();
};

extern TwoWire Wire;
