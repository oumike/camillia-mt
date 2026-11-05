// test-cflags: -Ihost -I../src -DDEVICE_MESH_DECK
// test-deps: battery_util.cpp
#include "../src/battery_util.h"
#include "test_support.h"

#include <Wire.h>

// A MAX17048 on the bus: VCELL at 0x02, SOC at 0x04, both big-endian.
static bool gaugePresent;
static uint16_t vcellRaw;
static uint16_t socRaw;
static uint8_t lastReg;
static uint8_t readBuf[2];
static int readPos;
static uint32_t nowMs;

TwoWire Wire;
void TwoWire::beginTransmission(uint8_t) {}
size_t TwoWire::write(uint8_t b) { lastReg = b; return 1; }
uint8_t TwoWire::endTransmission(bool) { return gaugePresent ? 0 : 2; }
uint8_t TwoWire::requestFrom(int, int len) {
    if (!gaugePresent || len != 2) return 0;
    const uint16_t v = (lastReg == 0x02) ? vcellRaw : (lastReg == 0x04) ? socRaw : 0;
    readBuf[0] = (uint8_t)(v >> 8);
    readBuf[1] = (uint8_t)v;
    readPos = 0;
    return 2;
}
int TwoWire::read() { return readPos < 2 ? readBuf[readPos++] : -1; }

uint32_t millis() { return nowMs; }

static void setGauge(float volts, int socPct) {
    gaugePresent = true;
    vcellRaw = (uint16_t)(volts / 0.000078125f + 0.5f);
    socRaw = (uint16_t)(socPct << 8);
}

static void reset(float volts, int socPct) {
    setGauge(volts, socPct);
    nowMs = 100;
    batterySetCalibrationTrim(0);
    batteryInitAdc();
}

// Untrimmed, the gauge's own state of charge is what shows.
static void testUntrimmedUsesGaugeSoc() {
    reset(4.17f, 93);
    CHECK(batteryReadPercent() == 93);
}

// Full off the charger, the gauge can sit short of 100%. Trimming must move the
// shown percentage, or the calibration screen has nothing to calibrate.
static void testTrimMovesPercent() {
    reset(4.17f, 93);
    batterySetCalibrationTrim(10);   // +1.0%: 4.17 V reads 4.21 V
    CHECK(batteryReadPercent() == 100);
    batterySetCalibrationTrim(-20);  // -2.0%: 4.17 V reads 4.087 V
    CHECK(batteryReadPercent() < 93);
    batterySetCalibrationTrim(0);
    CHECK(batteryReadPercent() == 93);
}

static void testSilentGaugeFallsBackToVoltage() {
    reset(4.17f, 93);
    gaugePresent = false;
    CHECK(batteryReadPercent() > 0);
}

int main() {
    testUntrimmedUsesGaugeSoc();
    testTrimMovesPercent();
    testSilentGaugeFallsBackToVoltage();
    return test::report();
}
