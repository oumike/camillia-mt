// ════════════════════════════════════════════════════════════════════════════
// hal/xl9555.cpp — XL9555 16-bit I2C GPIO expander implementation
//
// See hal/xl9555.h for the full description and usage guide.
// This file is only compiled for the T-LoRa Pager TFT build; all call sites
// are already guarded by  #if defined(DEVICE_TLORA_PAGER_TFT).
// ════════════════════════════════════════════════════════════════════════════

#include "xl9555.h"

// ── Low-level register I/O ────────────────────────────────────────────────────

bool xl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool xl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    // Repeated-start (false) keeps the bus held so the read follows immediately.
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

// ── Bus scan ──────────────────────────────────────────────────────────────────

int xl9555FindAddr() {
    // A0/A1/A2 strapping yields addresses 0x20-0x27.
    // LilyGO board revisions differ, so we scan rather than hard-code.
    for (uint8_t a = 0x20; a <= 0x27; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) return (int)a;
    }
    return -1;  // Expander not found; caller should check power rail / I2C init
}

// ── Bulk state read/write ─────────────────────────────────────────────────────

bool xl9555ReadAll(uint8_t addr,
                   uint8_t &out0, uint8_t &out1,
                   uint8_t &cfg0, uint8_t &cfg1) {
    return xl9555ReadReg(addr, XL9555_REG_OUT0, out0)
        && xl9555ReadReg(addr, XL9555_REG_OUT1, out1)
        && xl9555ReadReg(addr, XL9555_REG_CFG0, cfg0)
        && xl9555ReadReg(addr, XL9555_REG_CFG1, cfg1);
}

bool xl9555WriteAll(uint8_t addr,
                    uint8_t out0, uint8_t out1,
                    uint8_t cfg0, uint8_t cfg1) {
    return xl9555WriteReg(addr, XL9555_REG_OUT0, out0)
        && xl9555WriteReg(addr, XL9555_REG_OUT1, out1)
        && xl9555WriteReg(addr, XL9555_REG_CFG0, cfg0)
        && xl9555WriteReg(addr, XL9555_REG_CFG1, cfg1);
}

// ── Pin helpers ───────────────────────────────────────────────────────────────

void xl9555SetOutput(uint8_t pin, bool level,
                     uint8_t &out0, uint8_t &out1,
                     uint8_t &cfg0, uint8_t &cfg1) {
    // Pins 0-7 map to port 0; pins 8-15 map to port 1.
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) {
        cfg0 &= (uint8_t)~bit;          // Set as output (cfg bit = 0)
        if (level) out0 |= bit;
        else       out0 &= (uint8_t)~bit;
    } else {
        cfg1 &= (uint8_t)~bit;
        if (level) out1 |= bit;
        else       out1 &= (uint8_t)~bit;
    }
}

void xl9555SetInput(uint8_t pin, uint8_t &cfg0, uint8_t &cfg1) {
    // Set cfg bit = 1 to configure pin as high-impedance input.
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) cfg0 |= bit;
    else         cfg1 |= bit;
}
