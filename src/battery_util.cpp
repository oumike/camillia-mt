#include "battery_util.h"
#include "config.h"
#include <Wire.h>

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
constexpr uint8_t kBq25896Addr = 0x6B;
constexpr uint8_t kBqRegAdcControl = 0x02;
constexpr uint8_t kBqRegBattVoltage = 0x0E;
constexpr uint16_t kBqVbatBaseMv = 2304;
constexpr uint16_t kBqVbatStepMv = 20;
constexpr uint32_t kBqRetryBackoffMs = 15000UL;

static bool sBqWireStarted = false;
static bool sBqPresent = false;
static bool sBqConfigured = false;
static uint32_t sBqNextProbeMs = 0;

static void bqEnsureWire() {
    if (sBqWireStarted) return;
    Wire.begin(KB_SDA, KB_SCL);
    sBqWireStarted = true;
}

static bool bqProbePresent() {
    bqEnsureWire();
    Wire.beginTransmission(kBq25896Addr);
    return Wire.endTransmission() == 0;
}

static void bqMarkUnavailable(uint32_t nowMs) {
    sBqPresent = false;
    sBqConfigured = false;
    sBqNextProbeMs = nowMs + kBqRetryBackoffMs;
}

static bool bqReadReg(uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(kBq25896Addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)kBq25896Addr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

static bool bqWriteReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(kBq25896Addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool bqEnableBatteryAdc() {
    uint8_t adc = 0;
    if (!bqReadReg(kBqRegAdcControl, adc)) return false;
    adc |= 0x80; // ADC conversion enable
    adc |= 0x40; // Continuous conversion mode
    return bqWriteReg(kBqRegAdcControl, adc);
}

static float batteryReadPagerBqVolts() {
    bqEnsureWire();
    uint32_t nowMs = millis();

    if (!sBqPresent) {
        if (sBqNextProbeMs != 0 && (int32_t)(nowMs - sBqNextProbeMs) < 0) {
            return 0.0f;
        }
        if (!bqProbePresent()) {
            bqMarkUnavailable(nowMs);
            return 0.0f;
        }
        sBqPresent = true;
        sBqConfigured = false;
    }

    if (!sBqConfigured) {
        sBqConfigured = bqEnableBatteryAdc();
        if (!sBqConfigured) {
            bqMarkUnavailable(nowMs);
            return 0.0f;
        }
    }

    uint8_t reg = 0;
    if (!bqReadReg(kBqRegBattVoltage, reg)) {
        bqMarkUnavailable(nowMs);
        return 0.0f;
    }
    uint8_t raw = reg & 0x7F;
    if (raw == 0) return 0.0f;

    uint16_t mv = (uint16_t)(kBqVbatBaseMv + (raw * kBqVbatStepMv));
    return mv / 1000.0f;
}
} // namespace
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION) && (BATT_SENSE_ENABLE_PIN >= 0)
static int  sHeltecSenseLevel = BATT_SENSE_ENABLE_LEVEL;
static bool sHeltecSenseLocked = false;
#endif

static inline int clampPct(int v) {
    if (v < 0) return 0;
    if (v > 100) return 100;
    return v;
}

#if (BATT_ADC_PIN >= 0)
static int32_t batteryReadAdcMilliVoltsOnce() {
    int mv = analogReadMilliVolts(BATT_ADC_PIN);
    if (mv > 0) return (int32_t)mv;

    int raw = analogRead(BATT_ADC_PIN);
    if (raw <= 0) return 0;
    return (int32_t)((raw * 3300L) / 4095L);
}

static float batterySampleAdcVolts(int samples) {
    if (samples < 1) samples = 1;
    int64_t mvSum = 0;
    int valid = 0;
    for (int i = 0; i < samples; i++) {
        int32_t mv = batteryReadAdcMilliVoltsOnce();
        if (mv > 0) {
            mvSum += mv;
            valid++;
        }
    }
    if (valid <= 0) return 0.0f;
    return (mvSum / (float)valid) / 1000.0f;
}
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION) && (BATT_SENSE_ENABLE_PIN >= 0)
static void batterySetSenseLevel(int level) {
    digitalWrite(BATT_SENSE_ENABLE_PIN, level ? HIGH : LOW);
}

static float batterySampleHeltecAtLevel(int level) {
    batterySetSenseLevel(level);
    delay(2);
    return batterySampleAdcVolts(12);
}

static float batteryReadHeltecAdcVolts() {
    const int defaultLevel = (BATT_SENSE_ENABLE_LEVEL == LOW) ? LOW : HIGH;
    const int oppositeLevel = (defaultLevel == LOW) ? HIGH : LOW;

    if (!sHeltecSenseLocked) {
        float vDefault = batterySampleHeltecAtLevel(defaultLevel);
        float vOpp = batterySampleHeltecAtLevel(oppositeLevel);

        if (vOpp > (vDefault + 0.03f)) {
            sHeltecSenseLevel = oppositeLevel;
        } else {
            sHeltecSenseLevel = defaultLevel;
        }
        sHeltecSenseLocked = true;
    }

    float vadc = batterySampleHeltecAtLevel(sHeltecSenseLevel);
    if (vadc < 0.05f) {
        int altLevel = (sHeltecSenseLevel == LOW) ? HIGH : LOW;
        float vAlt = batterySampleHeltecAtLevel(altLevel);
        if (vAlt > (vadc + 0.03f)) {
            sHeltecSenseLevel = altLevel;
            vadc = vAlt;
        }
    }

    batterySetSenseLevel(sHeltecSenseLevel);
    return vadc;
}
#endif

void batteryInitAdc() {
#if (BATT_SENSE_ENABLE_PIN >= 0)
    pinMode(BATT_SENSE_ENABLE_PIN, OUTPUT);
    digitalWrite(BATT_SENSE_ENABLE_PIN, BATT_SENSE_ENABLE_LEVEL);
#endif
#if (BATT_ADC_PIN >= 0)
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);   // 0-3.3V ADC input range
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION) && (BATT_SENSE_ENABLE_PIN >= 0)
    sHeltecSenseLevel = (BATT_SENSE_ENABLE_LEVEL == LOW) ? LOW : HIGH;
    sHeltecSenseLocked = false;
#endif
#if defined(DEVICE_TLORA_PAGER_TFT)
    bqEnsureWire();
    if (bqProbePresent() && bqEnableBatteryAdc()) {
        sBqPresent = true;
        sBqConfigured = true;
        sBqNextProbeMs = 0;
    } else {
        bqMarkUnavailable(millis());
    }
#endif
}

float batteryReadVoltage() {
#if (BATT_ADC_PIN < 0)
#if defined(DEVICE_TLORA_PAGER_TFT)
    return batteryReadPagerBqVolts();
#else
    return 0.0f;
#endif
#else
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    float vadc =
#if (BATT_SENSE_ENABLE_PIN >= 0)
        batteryReadHeltecAdcVolts();
#else
        batterySampleAdcVolts(16);
#endif
    return vadc * BATT_DIV;
#else
    float vadc = batterySampleAdcVolts(8);
    return vadc * BATT_DIV;
#endif
#endif
}

uint8_t batteryReadPercent() {
    float vbat = batteryReadVoltage();
    int pct = (int)((vbat - BATT_VMIN) / (BATT_VMAX - BATT_VMIN) * 100.0f);
    return (uint8_t)clampPct(pct);
}
