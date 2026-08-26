#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/xl9555.h — XL9555 16-bit I2C GPIO expander driver
//
// The XL9555 (compatible with TCA9555 / PCA9555) controls power rails and
// peripheral resets through a single I2C address on multiple boards.
//
// Pin layout on the pager:
//   Port 0 (pins 0-7):   DRV_EN, AMP_EN, KB_RST, LORA_EN, GPS_EN, NFC_EN, -, GPS_RST
//   Port 1 (pins 8-15):  KB_EN, GPIO_EN, -, SD_PULLEN, SD_EN, -, -, -
//
// The expander address is auto-scanned in the 0x20–0x27 range because LilyGO
// board revisions differ in how the A0/A1/A2 address pins are tied.
//
// Both mesh_radio.cpp (LoRa rail) and gps.cpp (GPS rail) use this driver.
// Keeping it in one place avoids duplicate initialization and allows both
// subsystems to share the same expander handle without conflicting I2C traffic.
//
// Usage:
//   int addr = xl9555FindAddr();        // scan for expander
//   if (addr < 0) { /* not found */ }
//
//   uint8_t out0, out1, cfg0, cfg1;
//   xl9555ReadAll(addr, out0, out1, cfg0, cfg1);   // snapshot current state
//   xl9555SetOutput(pin, level, out0, out1, cfg0, cfg1);
//   xl9555SetInput(pin, cfg0, cfg1);
//   xl9555WriteAll(addr, out0, out1, cfg0, cfg1);  // commit changes
// ════════════════════════════════════════════════════════════════════════════

#pragma once
#include <Arduino.h>
#include <Wire.h>

// ── Register addresses ────────────────────────────────────────────────────────
// The XL9555 / TCA9555 exposes four byte registers per port.
static constexpr uint8_t XL9555_REG_IN0  = 0x00;   // Input port 0  (read)
static constexpr uint8_t XL9555_REG_IN1  = 0x01;   // Input port 1  (read)
static constexpr uint8_t XL9555_REG_OUT0 = 0x02;   // Output port 0 (read/write)
static constexpr uint8_t XL9555_REG_OUT1 = 0x03;   // Output port 1 (read/write)
static constexpr uint8_t XL9555_REG_POL0 = 0x04;   // Polarity inversion 0
static constexpr uint8_t XL9555_REG_POL1 = 0x05;   // Polarity inversion 1
static constexpr uint8_t XL9555_REG_CFG0 = 0x06;   // Direction: 1=input, 0=output
static constexpr uint8_t XL9555_REG_CFG1 = 0x07;

// ── Pager expander pin assignments ───────────────────────────────────────────
// Symbolic names for the bits used by radio and GPS subsystems.
// Port 0
static constexpr uint8_t XL9555_PIN_DRV_EN   = 0;
static constexpr uint8_t XL9555_PIN_AMP_EN   = 1;
static constexpr uint8_t XL9555_PIN_KB_RST   = 2;
static constexpr uint8_t XL9555_PIN_LORA_EN  = 3;
static constexpr uint8_t XL9555_PIN_GPS_EN   = 4;
static constexpr uint8_t XL9555_PIN_NFC_EN   = 5;
// pin 6 reserved
static constexpr uint8_t XL9555_PIN_GPS_RST  = 7;
// Port 1
static constexpr uint8_t XL9555_PIN_KB_EN    = 8;
static constexpr uint8_t XL9555_PIN_GPIO_EN  = 9;
// pin 10 reserved
static constexpr uint8_t XL9555_PIN_SD_PULLEN = 11;
static constexpr uint8_t XL9555_PIN_SD_EN    = 12;

// ── API ───────────────────────────────────────────────────────────────────────

// Scan I2C addresses 0x20-0x27 and return the first one that ACKs.
// Returns -1 if no expander is found on the bus.
int xl9555FindAddr();

// Read all four state registers into the provided variables.
// Returns true on success.
bool xl9555ReadAll(uint8_t addr, uint8_t &out0, uint8_t &out1,
                  uint8_t &cfg0, uint8_t &cfg1);

// Write all four state registers back to the expander.
// Returns true on success.
bool xl9555WriteAll(uint8_t addr, uint8_t out0, uint8_t out1,
                   uint8_t cfg0, uint8_t cfg1);

// Configure a single pin as an output and set its level.
// pin 0-7 → port 0,  pin 8-15 → port 1.
void xl9555SetOutput(uint8_t pin, bool level,
                     uint8_t &out0, uint8_t &out1,
                     uint8_t &cfg0, uint8_t &cfg1);

// Configure a single pin as an input (hi-Z).
void xl9555SetInput(uint8_t pin, uint8_t &cfg0, uint8_t &cfg1);

// Low-level single-register read/write helpers (used internally and in tests).
bool xl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val);
bool xl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t &val);
