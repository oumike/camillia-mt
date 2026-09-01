#include "battery_util.h"
#include "config.h"
#include <Wire.h>
#include <string.h>

// Per-board correction for ADC-divider voltage reads. Boards that read battery
// through a resistor divider on a plain ADC pin can land consistently high or
// low from divider tolerance and ADC source-impedance settling; a board header
// may define BATT_CAL to scale the raw voltage back to true. Boards that read
// from a fuel-gauge/PMU chip (the Pager's BQ25896, the Cardputer's M5Unified
// path) are already calibrated in hardware and leave this at 1.0.
#ifndef BATT_CAL
#define BATT_CAL 1.0f
#endif

#if defined(DEVICE_SQUARE)
#include <Adafruit_ADS1X15.h>
#include "hal/square_io.h"
namespace {
constexpr uint32_t kSquareAdsRetryBackoffMs = 15000UL;
constexpr uint32_t kSquareAdsReadIntervalMs = 30000UL;
constexpr uint8_t kSquareAdsSampleCount = 3;

Adafruit_ADS1115 sSquareAds;
bool sSquareAdsReady = false;
bool sSquareAdsReadDone = false;
uint32_t sSquareAdsNextProbeMs = 0;
uint32_t sSquareAdsLastReadMs = 0;
float sSquareAdsCachedVolts = 0.0f;

static bool squareAdsEnsureReady() {
    if (sSquareAdsReady) return true;

    const uint32_t nowMs = millis();
    if (sSquareAdsNextProbeMs != 0
        && (int32_t)(nowMs - sSquareAdsNextProbeMs) < 0) {
        return false;
    }
    if (!squareIoReady() || !squareIoSetBatterySense(true)) {
        sSquareAdsNextProbeMs = nowMs + kSquareAdsRetryBackoffMs;
        return false;
    }

    delay(10);
    if (!sSquareAds.begin(ADS1115_ADDR, &Wire)) {
        (void)squareIoSetBatterySense(false);
        sSquareAdsNextProbeMs = nowMs + kSquareAdsRetryBackoffMs;
        Serial.printf("[batt] square ADS1115 not found at 0x%02X\n", ADS1115_ADDR);
        return false;
    }

    sSquareAds.setGain(GAIN_TWO);
    sSquareAdsReady = true;
    sSquareAdsNextProbeMs = 0;
    (void)squareIoSetBatterySense(false);
    Serial.printf("[batt] square ADS1115 ready addr=0x%02X channel=%u divider=%.2f\n",
                  ADS1115_ADDR, (unsigned)ADS1115_BATT_CHANNEL, (double)BATT_DIV);
    return true;
}

static float batteryReadSquareAdsVolts() {
    const uint32_t nowMs = millis();
    if (sSquareAdsReadDone
        && (uint32_t)(nowMs - sSquareAdsLastReadMs) < kSquareAdsReadIntervalMs) {
        return sSquareAdsCachedVolts;
    }
    if (!squareAdsEnsureReady() || !squareIoSetBatterySense(true)) {
        return sSquareAdsCachedVolts;
    }

    delay(10);
    float voltsSum = 0.0f;
    uint8_t validSamples = 0;
    for (uint8_t sample = 0; sample < kSquareAdsSampleCount; sample++) {
        const int16_t raw = sSquareAds.readADC_SingleEnded(ADS1115_BATT_CHANNEL);
        if (raw <= 0) continue;
        voltsSum += sSquareAds.computeVolts(raw);
        validSamples++;
    }
    (void)squareIoSetBatterySense(false);

    if (validSamples == 0) return sSquareAdsCachedVolts;
    sSquareAdsCachedVolts = (voltsSum / validSamples) * BATT_DIV * BATT_CAL;
    sSquareAdsLastReadMs = nowMs;
    sSquareAdsReadDone = true;
    return sSquareAdsCachedVolts;
}
} // namespace
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
namespace {
constexpr uint8_t kBq25896Addr = 0x6B;
constexpr uint8_t kBqRegAdcControl = 0x02;
constexpr uint8_t kBqRegWatchdogRst = 0x03;   // bit 6 = WD_RST
constexpr uint8_t kBqRegBattVoltage = 0x0E;
constexpr uint16_t kBqVbatBaseMv = 2304;
constexpr uint16_t kBqVbatStepMv = 20;
constexpr uint32_t kBqRetryBackoffMs = 15000UL;

static bool sBqWireStarted = false;
static bool sBqPresent = false;
static bool sBqConversionPending = false;   // one-shot started, result not yet read
static bool sBqZeroLogged = false;   // rate-limits the BATV=0 log
static uint32_t sBqNextProbeMs = 0;
static uint8_t sBqLastRawReg = 0;    // last REG0E byte, for the `batt` CLI dump
static uint16_t sBqLastMv = 0;       // last decoded millivolts

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
    sBqConversionPending = false;
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

// One-shot conversions, not continuous. Continuous mode (CONV_RATE, bit 6) has
// to survive on its own between reads, and on this board it does not: anything
// that resets the charger's registers — the I2C watchdog below, a brown-out,
// the charger re-entering a state that halts conversion — leaves REG0E
// answering 0 or a stale value while the chip still ACKs every read. There is
// no status bit that distinguishes "no conversion running" from "battery is
// flat", which is why it presents as a battery stuck at 0% or a percentage that
// does not track reality.
//
// A one-shot cannot go stale the same way: CONV_START is set here and cleared
// by the hardware when the result is ready, so the bit itself says whether
// REG0E holds a fresh answer. Started on one sample and collected on the next
// (~1.2 s later), so nothing blocks the UI waiting ~70 ms for a conversion.
static bool bqStartConversion() {
    uint8_t adc = 0;
    if (!bqReadReg(kBqRegAdcControl, adc)) return false;
    adc &= (uint8_t)~0x40;   // CONV_RATE = 0: one shot
    adc |= 0x80;             // CONV_START
    return bqWriteReg(kBqRegAdcControl, adc);
}

// True once the hardware has cleared CONV_START, meaning REG0E is fresh.
static bool bqConversionReady(bool &readOk) {
    uint8_t adc = 0;
    readOk = bqReadReg(kBqRegAdcControl, adc);
    if (!readOk) return false;
    return (adc & 0x80) == 0;
}

// Pet the charger's I2C watchdog (REG03 bit 6, WD_RST). It defaults to 40 s,
// and on expiry the BQ25896 restores its registers to power-on defaults — which
// clears the ADC enable we set above, so BATV then reads 0 with the chip still
// happily ACKing. That is why the gauge would work for the first half-minute
// after boot and read 0% forever after.
static bool bqKickWatchdog() {
    uint8_t reg = 0;
    if (!bqReadReg(kBqRegWatchdogRst, reg)) return false;
    return bqWriteReg(kBqRegWatchdogRst, (uint8_t)(reg | 0x40));
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
        sBqConversionPending = false;
    }

    // Keep the charger's watchdog from expiring and restoring register defaults.
    bqKickWatchdog();

    // No conversion in flight: start one and collect it on the next sample.
    if (!sBqConversionPending) {
        sBqConversionPending = bqStartConversion();
        if (!sBqConversionPending) {
            bqMarkUnavailable(nowMs);
        }
        return 0.0f;   // caller keeps the previous reading; see batteryRefreshFilter
    }

    bool ctrlReadOk = false;
    if (!bqConversionReady(ctrlReadOk)) {
        if (!ctrlReadOk) {
            Serial.println("[batt] BQ25896 read failed - retrying probe");
            sBqConversionPending = false;
            bqMarkUnavailable(nowMs);
        }
        return 0.0f;   // still converting: hold, don't report a false 0%
    }
    sBqConversionPending = false;

    uint8_t reg = 0;
    if (!bqReadReg(kBqRegBattVoltage, reg)) {
        Serial.println("[batt] BQ25896 read failed - retrying probe");
        bqMarkUnavailable(nowMs);
        return 0.0f;
    }
    sBqLastRawReg = reg;
    uint8_t raw = reg & 0x7F;
    if (raw == 0) {
        // Conversion completed and still reported nothing. With one-shot that is
        // a real answer rather than a stalled ADC, so there is nothing to re-arm
        // — but it is also indistinguishable from a battery below the 2.304 V
        // floor, so report unknown and let the caller hold its last value.
        if (!sBqZeroLogged) {
            Serial.printf("[batt] BQ25896 BATV=0 after conversion (reg0x0E=0x%02X)\n", reg);
            sBqZeroLogged = true;
        }
        return 0.0f;
    }
    sBqZeroLogged = false;

    uint16_t mv = (uint16_t)(kBqVbatBaseMv + (raw * kBqVbatStepMv));
    sBqLastMv = mv;
    return mv / 1000.0f;
}
} // namespace
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#include <M5Unified.h>
namespace {
// The Cardputer's battery divider sits on ADC1_GPIO10, which M5Unified claims via
// the ESP-IDF adc_oneshot driver during M5Cardputer.begin(). Reading that same pin
// with Arduino's legacy analogRead returns 0 once the oneshot driver owns ADC1, so
// defer to M5Unified's calibrated reading (millivolts, already divider-scaled).
static float batteryReadCardputerVolts() {
    int mv = M5.Power.getBatteryVoltage();
    if (mv <= 0) return 0.0f;
    return mv / 1000.0f;
}
} // namespace
#endif

#if defined(DEVICE_MESH_DECK)
#include <Wire.h>
namespace {
// The Mesh Deck's Standard Cell carries a MAX17048 fuel gauge instead of a
// divider to an ADC, so there is nothing analog to sample. The gauge tracks
// charge by coulomb counting and reports state directly, which is strictly
// better than inferring percentage from a resting voltage curve — its SOC
// register is used as-is rather than being pushed back through
// batteryVoltageToPct().
//
// VCELL (0x02) is 78.125 uV per LSB; SOC (0x04) is 1/256 % per LSB.
constexpr uint8_t kMaxRegVCell = 0x02;
constexpr uint8_t kMaxRegSoc   = 0x04;

bool maxReadReg16(uint8_t reg, uint16_t &out) {
    Wire.beginTransmission(BATT_FUEL_GAUGE_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)BATT_FUEL_GAUGE_ADDR, 2) != 2) return false;
    const uint8_t hi = (uint8_t)Wire.read();
    const uint8_t lo = (uint8_t)Wire.read();
    out = (uint16_t)((hi << 8) | lo);
    return true;
}

float batteryReadMeshDeckVolts() {
    uint16_t raw = 0;
    if (!maxReadReg16(kMaxRegVCell, raw)) return 0.0f;
    return (float)raw * 0.000078125f;
}

// Returns -1 when the gauge does not answer, so callers can fall back.
int batteryReadMeshDeckPct() {
    uint16_t raw = 0;
    if (!maxReadReg16(kMaxRegSoc, raw)) return -1;
    int pct = (int)(raw >> 8);            // whole percent; low byte is fraction
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    return pct;
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

namespace {
constexpr uint32_t kBatteryUpdateIntervalMs = 1200UL;
constexpr float kBatteryAlphaSlow = 0.18f;
constexpr float kBatteryAlphaFast = 0.45f;
constexpr float kBatteryFastDeltaV = 0.08f;
constexpr int kBatteryPctHysteresis = 2;

struct BatteryFilterState {
    bool initialized;
    uint32_t lastSampleMs;
    float filteredVoltage;
    uint8_t displayPct;
};

static BatteryFilterState sBatteryFilter = {};

static int batteryVoltageToPct(float vbat) {
    // Use a Li-ion OCV curve rather than a linear 3.0V..4.2V ramp so
    // displayed SOC better matches real runtime across all battery builds.
    struct CurvePt {
        float v;
        uint8_t pct;
    };
    static const CurvePt kLiIonCurve[] = {
        {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85},
        {4.02f, 75},  {3.98f, 65}, {3.95f, 55}, {3.91f, 45},
        {3.87f, 35},  {3.85f, 30}, {3.84f, 25}, {3.82f, 20},
        {3.80f, 15},  {3.79f, 10}, {3.77f, 5},  {3.73f, 2},
        {3.70f, 0},
    };

    if (vbat <= 0.0f) return 0;
    if (vbat >= kLiIonCurve[0].v) return 100;
    const int last = (int)(sizeof(kLiIonCurve) / sizeof(kLiIonCurve[0])) - 1;
    if (vbat <= kLiIonCurve[last].v) return 0;

    for (int i = 0; i < last; i++) {
        const CurvePt &hi = kLiIonCurve[i];
        const CurvePt &lo = kLiIonCurve[i + 1];
        if (vbat <= hi.v && vbat >= lo.v) {
            float span = hi.v - lo.v;
            if (span <= 0.0001f) return (int)lo.pct;
            float t = (vbat - lo.v) / span;
            float pctf = lo.pct + (hi.pct - lo.pct) * t;
            return clampPct((int)(pctf + 0.5f));
        }
    }
    return 0;
}

static void batteryResetFilter() {
    memset(&sBatteryFilter, 0, sizeof(sBatteryFilter));
}
}

static void batteryRefreshFilter(bool forceSample);

// User trim from settings, applied on top of BATT_CAL. Kept here rather than
// read from the config struct so this file stays independent of the UI layer;
// main_lvgl pushes it at boot and whenever the calibration screen saves.
static float sBatteryCalTrimScale = 1.0f;

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
#if (BATT_ADC_PIN >= 0) && !defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer battery is read via M5Unified (adc_oneshot); leave the legacy
    // Arduino ADC driver off GPIO10 so it doesn't fight for ADC1.
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);   // 0-3.3V ADC input range
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION) && (BATT_SENSE_ENABLE_PIN >= 0)
    sHeltecSenseLevel = (BATT_SENSE_ENABLE_LEVEL == LOW) ? LOW : HIGH;
    sHeltecSenseLocked = false;
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    bqEnsureWire();
    if (bqProbePresent() && bqStartConversion()) {
        // First conversion is in flight; batteryRefreshFilter() below will not
        // get a voltage from it, which is why the filter treats 0 as "unknown"
        // and holds rather than displaying 0%.
        sBqPresent = true;
        sBqConversionPending = true;
        sBqNextProbeMs = 0;
    } else {
        bqMarkUnavailable(millis());
    }
#endif
#if defined(DEVICE_SQUARE)
    (void)squareAdsEnsureReady();
#endif

    batteryResetFilter();
    batteryRefreshFilter(true);
}

// Whatever the hardware reports, before the user's trim: charger register, fuel
// gauge, or divider-scaled ADC.
static float batteryReadVoltageHw() {
#if (BATT_ADC_PIN < 0)
#if defined(DEVICE_SQUARE)
    return batteryReadSquareAdsVolts();
#elif defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    return batteryReadPagerBqVolts();
#elif defined(DEVICE_MESH_DECK)
    return batteryReadMeshDeckVolts();
#else
    return 0.0f;
#endif
#else
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return batteryReadCardputerVolts();
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    float vadc =
#if (BATT_SENSE_ENABLE_PIN >= 0)
        batteryReadHeltecAdcVolts();
#else
        batterySampleAdcVolts(16);
#endif
    return vadc * BATT_DIV * BATT_CAL;
#else
    float vadc = batterySampleAdcVolts(8);
    return vadc * BATT_DIV * BATT_CAL;
#endif
#endif
}

// The hardware reading with the user's trim applied. Everything downstream —
// the filter, the percentage, the header gauge — sees only this.
static float batteryReadVoltageRaw() {
    const float v = batteryReadVoltageHw();
    if (v <= 0.0f) return 0.0f;   // unknown stays unknown; the filter holds
    return v * sBatteryCalTrimScale;
}

static void batteryRefreshFilter(bool forceSample) {
    uint32_t now = millis();
    if (!forceSample && sBatteryFilter.initialized
        && (uint32_t)(now - sBatteryFilter.lastSampleMs) < kBatteryUpdateIntervalMs) {
        return;
    }

    float rawV = batteryReadVoltageRaw();
    sBatteryFilter.lastSampleMs = now;

    if (rawV <= 0.0f) {
        if (!sBatteryFilter.initialized) {
            sBatteryFilter.filteredVoltage = 0.0f;
            sBatteryFilter.displayPct = 0;
        }
        return;
    }

    if (!sBatteryFilter.initialized) {
        sBatteryFilter.initialized = true;
        sBatteryFilter.filteredVoltage = rawV;
        sBatteryFilter.displayPct = (uint8_t)batteryVoltageToPct(rawV);
        return;
    }

    float deltaV = rawV - sBatteryFilter.filteredVoltage;
    if (deltaV < 0.0f) deltaV = -deltaV;
    float alpha = (deltaV >= kBatteryFastDeltaV) ? kBatteryAlphaFast : kBatteryAlphaSlow;
    sBatteryFilter.filteredVoltage += (rawV - sBatteryFilter.filteredVoltage) * alpha;

    int targetPct = batteryVoltageToPct(sBatteryFilter.filteredVoltage);
    int diff = targetPct - (int)sBatteryFilter.displayPct;
    if (diff >= kBatteryPctHysteresis) {
        sBatteryFilter.displayPct = (uint8_t)((diff >= 6)
            ? targetPct
            : clampPct((int)sBatteryFilter.displayPct + 1));
    } else if (diff <= -kBatteryPctHysteresis) {
        sBatteryFilter.displayPct = (uint8_t)((diff <= -6)
            ? targetPct
            : clampPct((int)sBatteryFilter.displayPct - 1));
    }
}

float batteryReadVoltage() {
    batteryRefreshFilter(false);
    return sBatteryFilter.initialized ? sBatteryFilter.filteredVoltage : 0.0f;
}

float batteryReadVoltageUntrimmed() {
    return batteryReadVoltageHw();
}

void batterySetCalibrationTrim(int trimPermille) {
    if (trimPermille < -500) trimPermille = -500;
    if (trimPermille > 500) trimPermille = 500;
    const float scale = 1.0f + (trimPermille / 1000.0f);
    if (scale > sBatteryCalTrimScale - 0.0001f && scale < sBatteryCalTrimScale + 0.0001f) {
        return;
    }
    sBatteryCalTrimScale = scale;
    // The filter holds a voltage measured under the old trim, and it eases
    // toward new values a fraction at a time. Drop it rather than let the
    // display crawl toward the corrected reading over the next several seconds.
    batteryResetFilter();
    batteryRefreshFilter(true);
}

uint8_t batteryReadPercent() {
#if defined(DEVICE_MESH_DECK)
    // Prefer the gauge's own state of charge. Everything else here derives a
    // percentage from a filtered resting voltage because it has nothing better;
    // this board does, and coulomb counting stays honest under load where the
    // voltage curve sags and reads low.
    const int gaugePct = batteryReadMeshDeckPct();
    if (gaugePct >= 0) return (uint8_t)gaugePct;
    // Gauge silent: fall through to the shared voltage-curve estimate.
#endif
    batteryRefreshFilter(false);
    return sBatteryFilter.initialized ? sBatteryFilter.displayPct : 0;
}

void batteryDebugSnapshot(char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    batteryRefreshFilter(false);

    const float filtered = sBatteryFilter.initialized ? sBatteryFilter.filteredVoltage : 0.0f;
    const int shown = sBatteryFilter.initialized ? (int)sBatteryFilter.displayPct : -1;

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    // Raw register included deliberately: bit 7 is THERM_STAT, so a value that
    // looks 128 too high is a masking problem, not a bad battery. 20 mV per LSB
    // means the voltage can only ever be 2304 + 20n mV.
    snprintf(out, outLen,
             "[batt] BQ25896 present=%d pending=%d reg0x0E=0x%02X raw=%u -> %umV | "
             "trim=x%.3f filtered=%.3fV shown=%d%% (curve=%d%%)",
             sBqPresent ? 1 : 0,
             sBqConversionPending ? 1 : 0,
             sBqLastRawReg,
             (unsigned)(sBqLastRawReg & 0x7F),
             (unsigned)sBqLastMv,
             (double)sBatteryCalTrimScale,
             (double)filtered,
             shown,
             batteryVoltageToPct(filtered));
#else
    const float hwV = batteryReadVoltageHw();
    snprintf(out, outLen,
             "[batt] raw=%.3fV trim=x%.3f | filtered=%.3fV shown=%d%% (curve=%d%%)",
             (double)hwV, (double)sBatteryCalTrimScale,
             (double)filtered, shown, batteryVoltageToPct(filtered));
#endif
}
