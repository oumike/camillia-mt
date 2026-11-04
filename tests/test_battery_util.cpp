// test-cflags: -Ihost -DDEVICE_TDECK
#include "../src/battery_util.h"
#include "test_support.h"

#include <cmath>
#include <vector>

static uint32_t nowMs;
static std::vector<uint32_t> adcMv;
static size_t sampleIndex;
static uint16_t adcRaw;

uint32_t millis() { return nowMs; }
uint32_t analogReadMilliVolts(uint8_t) {
    const size_t index = std::min(sampleIndex++, adcMv.size() - 1);
    return adcMv[index];
}
uint16_t analogRead(uint8_t) { return adcRaw; }
void analogSetPinAttenuation(uint8_t, adc_attenuation_t) {}

static bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.00001f;
}

static void reset(uint32_t mv, uint32_t atMs = 100, uint16_t raw = 0) {
    adcMv = {mv};
    adcRaw = raw;
    sampleIndex = 0;
    nowMs = atMs;
    batterySetCalibrationTrim(0);
    batteryInitAdc();
    sampleIndex = 0;
}

static void testVoltageAndCurve() {
    const struct { uint32_t mv; float volts; uint8_t percent; } cases[] = {
        {0, 0.0f, 0}, {1500, 3.09f, 0}, {1800, 3.708f, 1},
        {1900, 3.914f, 46}, {2000, 4.12f, 91}, {2100, 4.326f, 100},
    };
    for (const auto &c : cases) {
        reset(c.mv);
        CHECK(near(batteryReadVoltage(), c.volts));
        const uint8_t percent = batteryReadPercent();
        if (percent != c.percent) {
            printf("  ADC %u mV: expected %u%%, got %u%%\n", c.mv, c.percent, percent);
        }
        CHECK(percent == c.percent);
    }
}

static void testSamplingAndRawFallback() {
    reset(0);
    adcMv = {0, 2000, 0, 2000, 0, 2000, 0, 2000};
    sampleIndex = 0;
    CHECK(near(batteryReadVoltageNow(), 4.12f));
    CHECK(sampleIndex == 8);

    reset(0, 100, 2048);
    CHECK(near(batteryReadVoltageNow(), 3.399f));
}

static void testCadenceAndRollover() {
    for (uint32_t start : {100u, UINT32_MAX - 500u}) {
        reset(2000, start);
        adcMv = {1800};
        nowMs = start + 1199u;
        CHECK(near(batteryReadVoltage(), 4.12f));
        CHECK(sampleIndex == 0);
        nowMs = start + 1200u;
        CHECK(near(batteryReadVoltage(), 3.9346f));
        CHECK(sampleIndex == 8);
    }
}

static void testHysteresisAndUnknownSamples() {
    reset(1900);
    adcMv = {1910};
    nowMs += 1200;
    CHECK(near(batteryReadVoltage(), 3.917708f));
    CHECK(batteryReadPercent() == 46);
    nowMs += 1200;
    CHECK(batteryReadPercent() == 47);

    const float last = batteryReadVoltage();
    adcMv = {0};
    nowMs += 1200;
    CHECK(near(batteryReadVoltage(), last));
    CHECK(batteryReadPercent() == 47);
    CHECK(batteryReadVoltageNow() == 0.0f);
}

static void testFreshReadAndCalibration() {
    reset(2000);
    adcMv = {1800};
    CHECK(near(batteryReadVoltageNow(), 3.708f));
    CHECK(near(batteryReadVoltageUntrimmed(), 3.708f));
    CHECK(near(batteryReadVoltage(), 4.12f));

    adcMv = {2000};
    batterySetCalibrationTrim(25);
    CHECK(near(batteryReadVoltage(), 4.223f));
    CHECK(near(batteryReadVoltageNow(), 4.223f));
    CHECK(near(batteryReadVoltageUntrimmed(), 4.12f));
    CHECK(batteryReadPercent() == 100);
    batterySetCalibrationTrim(600);
    CHECK(near(batteryReadVoltage(), 6.18f));
    batterySetCalibrationTrim(-600);
    CHECK(near(batteryReadVoltage(), 2.06f));
    batterySetCalibrationTrim(0);
    CHECK(near(batteryReadVoltage(), 4.12f));
}

static void testUnknownPowerIsNotReportedAsKnown() {
    bool known = true;
    CHECK(!batteryExternalPowerPresent(&known) && !known);
    CHECK(!batteryHardwarePowerOffSupported());
    CHECK(!batteryHardwarePowerOff());
}

int main() {
    testVoltageAndCurve();
    testSamplingAndRawFallback();
    testCadenceAndRollover();
    testHysteresisAndUnknownSamples();
    testFreshReadAndCalibration();
    testUnknownPowerIsNotReportedAsKnown();
    return test::report();
}
