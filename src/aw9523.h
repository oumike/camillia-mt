#pragma once
// AW9523B 16-bit I2C GPIO expander.
//
// The Attaky Mesh Deck puts four of these on the bus and reaches most of its
// input through them, so this is shared rather than owned by any one consumer:
//   0x58  touch interrupt (P04)
//   0x59  D-pad/buttons (P00..P07), status LED (P10..P12), touch reset (P13)
//   0x5A  keyboard, left half   — 5x5 matrix
//   0x5B  keyboard, right half  — 5x5 matrix
//
// Pin naming follows the datasheet: "P0n" is port 0 bit n, "P1n" is port 1
// bit n. The keyboard's documented "rows P10..P14 / columns P00..P04" is
// therefore port 1 bits 0..4 driving, port 0 bits 0..4 reading.
#include <Arduino.h>
#include <Wire.h>   // TwoWire / Wire, used in the begin() default argument

class Aw9523 {
public:
    static constexpr uint8_t kChipId = 0x23;

    // Registers, per the AW9523B datasheet.
    enum : uint8_t {
        REG_INPUT0   = 0x00,
        REG_INPUT1   = 0x01,
        REG_OUTPUT0  = 0x02,
        REG_OUTPUT1  = 0x03,
        REG_CONFIG0  = 0x04,   // 1 = input, 0 = output
        REG_CONFIG1  = 0x05,
        REG_INTMASK0 = 0x06,   // 0 = interrupt enabled, 1 = masked
        REG_INTMASK1 = 0x07,
        REG_CHIPID   = 0x10,
        REG_GCR      = 0x11,   // bit4: port 0 push-pull (1) vs open-drain (0)
        REG_LEDMODE0 = 0x12,   // 1 = GPIO, 0 = constant-current LED
        REG_LEDMODE1 = 0x13,
        REG_SOFTRST  = 0x7F,
    };

    // Verifies the chip ID and puts every pin in a known state: all GPIO (not
    // LED mode), all inputs, interrupts masked. Returns false if nothing
    // answers at addr or the ID is wrong.
    bool begin(uint8_t addr, TwoWire &bus = Wire);

    bool present() const { return _present; }
    uint8_t address() const { return _addr; }

    // Whole-port access. Port 0 is bits P00..P07, port 1 is P10..P17.
    bool readPort(uint8_t port, uint8_t &value);
    bool writePort(uint8_t port, uint8_t value);
    bool configPort(uint8_t port, uint8_t inputMask);   // 1 = input
    bool maskInterrupts(uint8_t port, uint8_t mask);    // 1 = masked

    // Port 0 defaults to open-drain out of reset, which cannot drive a matrix
    // column high. Call with true once if port 0 has to source current.
    bool setPort0PushPull(bool pushPull);

    bool readReg(uint8_t reg, uint8_t &value);
    bool writeReg(uint8_t reg, uint8_t value);

private:
    TwoWire *_bus = nullptr;
    uint8_t  _addr = 0;
    bool     _present = false;
};
