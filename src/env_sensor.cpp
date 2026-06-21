// env_sensor.cpp — Heltec V4 expansion environment sensor backend.
//
// Detects and reads one of BME280, BMP280, or AHT20 over I2C across a small
// set of known Heltec routes. Boot-time probing is conservative; deeper
// diagnostics are exposed via envDebugScan()/envDebugFullScan() so they can
// be triggered from the serial CLI without scanning at every boot.

#include "env_sensor.h"
#include "config.h"

#if defined(DEVICE_HELTEC_V4_EXPANSION)

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <math.h>

// ── Backend state ─────────────────────────────────────────────────────────────

enum EnvBackend : uint8_t {
    ENV_BACKEND_NONE = 0,
    ENV_BACKEND_BME280,
    ENV_BACKEND_BMP280,
    ENV_BACKEND_AHT20,
};

static Adafruit_BME280 gBme280;
static Adafruit_BMP280 gBmp280Wire(&Wire);
static Adafruit_BMP280 gBmp280Wire1(&Wire1);
static Adafruit_BMP280 *gBmp280Active = nullptr;
static bool gReady = false;
static uint8_t gI2cAddr = 0;
static EnvBackend gBackend = ENV_BACKEND_NONE;
static TwoWire *gEnvWireActive = nullptr;
static const char *gProbeRoute = nullptr;
static uint32_t gLastInitAttemptMs = 0;
static bool gLoggedProbeFailure = false;

// ── Route table ───────────────────────────────────────────────────────────────
// Single source of truth for SDA/SCL pin combinations to probe. The first
// entry is the touch-shared bus; alternate routes are restricted to Wire0 so
// the touch controller on Wire1 is never reconfigured mid-run.

struct EnvRoute {
    TwoWire *wire;
    int sda;
    int scl;
    const char *name;
};

static const EnvRoute kEnvRoutes[] = {
#if (TOUCH_I2C_PORT == 1)
    { &Wire1, TOUCH_SDA, TOUCH_SCL, "touch-route" },
#else
    { &Wire,  TOUCH_SDA, TOUCH_SCL, "touch-route" },
#endif
    { &Wire, 41, 42, "wire0-41/42" },
    { &Wire,  3,  4, "wire0-3/4"   },
    { &Wire, 17, 18, "wire0-17/18" },
};
static constexpr size_t kEnvRouteCount = sizeof(kEnvRoutes) / sizeof(kEnvRoutes[0]);

static TwoWire &envWire() {
    return *kEnvRoutes[0].wire;
}

static Adafruit_BMP280 *envBmpForWire(TwoWire &w) {
    return (&w == &Wire1) ? &gBmp280Wire1 : &gBmp280Wire;
}

static void envResetCachedState() {
    gReady = false;
    gI2cAddr = 0;
    gBackend = ENV_BACKEND_NONE;
    gBmp280Active = nullptr;
    gEnvWireActive = nullptr;
    gProbeRoute = nullptr;
    gLastInitAttemptMs = 0;
    gLoggedProbeFailure = false;
}

static bool envAddressPresent(TwoWire &w, uint8_t addr) {
    w.beginTransmission(addr);
    return w.endTransmission(true) == 0;
}

// ── AHT20 minimal driver ──────────────────────────────────────────────────────

static bool envAhtWrite(TwoWire &w, const uint8_t *data, size_t len) {
    w.beginTransmission(0x38);
    for (size_t i = 0; i < len; i++) {
        w.write(data[i]);
    }
    return w.endTransmission(true) == 0;
}

static bool envAhtReadMeasurement(TwoWire &w, float &temperatureC, float &humidityPct) {
    static const uint8_t trigCmd[3] = { 0xAC, 0x33, 0x00 };
    if (!envAhtWrite(w, trigCmd, sizeof(trigCmd))) return false;

    uint32_t startMs = millis();
    while ((uint32_t)(millis() - startMs) < 120UL) {
        if (w.requestFrom((uint8_t)0x38, (uint8_t)1, (uint8_t)true) != 1) {
            delay(3);
            continue;
        }
        uint8_t status = w.read();
        if ((status & 0x80) == 0) break;
        delay(5);
    }

    uint8_t frame[7] = {0};
    if (w.requestFrom((uint8_t)0x38, (uint8_t)7, (uint8_t)true) != 7) return false;
    for (int i = 0; i < 7; i++) frame[i] = w.read();
    if (frame[0] & 0x80) return false;

    uint32_t rawHum = ((uint32_t)frame[1] << 12)
                    | ((uint32_t)frame[2] << 4)
                    | ((uint32_t)(frame[3] >> 4) & 0x0F);
    uint32_t rawTemp = ((uint32_t)(frame[3] & 0x0F) << 16)
                     | ((uint32_t)frame[4] << 8)
                     | (uint32_t)frame[5];

    humidityPct = ((float)rawHum * 100.0f) / 1048576.0f;
    temperatureC = ((float)rawTemp * 200.0f) / 1048576.0f - 50.0f;
    return !isnan(temperatureC) && !isnan(humidityPct);
}

static bool envAhtProbe(TwoWire &w) {
    if (!envAddressPresent(w, 0x38)) return false;

    static const uint8_t initCmd[3] = { 0xBE, 0x08, 0x00 };
    (void)envAhtWrite(w, initCmd, sizeof(initCmd));
    delay(12);

    float t = NAN;
    float h = NAN;
    if (!envAhtReadMeasurement(w, t, h)) return false;
    return true;
}

// ── Chip ID read (BMx280 family register 0xD0) ───────────────────────────────

static bool envReadChipId(TwoWire &w, uint8_t addr, uint8_t &chipId) {
    chipId = 0;
    if (!envAddressPresent(w, addr)) return false;

    w.beginTransmission(addr);
    w.write((uint8_t)0xD0);
    if (w.endTransmission(true) != 0) return false;
    if (w.requestFrom((uint8_t)addr, (uint8_t)1, (uint8_t)true) != 1) return false;
    chipId = w.read();
    return true;
}

// ── Probe ─────────────────────────────────────────────────────────────────────

// Attempt to initialise a supported sensor on the given bus/pins. On success,
// sets the module's global state so envRead()/envSensorName() can be used.
static bool envTryProbe(TwoWire &w, int sda, int scl, const char *routeName, uint32_t freqHz) {
    if (sda < 0 || scl < 0) return false;

    w.begin(sda, scl, freqHz);

    // BMx280-family detection: read chip ID at 0xD0 on the two known addresses.
    const uint8_t candidates[] = { 0x76, 0x77 };
    for (size_t i = 0; i < sizeof(candidates); i++) {
        uint8_t addr = candidates[i];
        uint8_t chipId = 0;
        if (!envReadChipId(w, addr, chipId)) continue;

        if (chipId == 0x60) {
            if (gBme280.begin(addr, &w)) {
                gReady = true;
                gI2cAddr = addr;
                gBackend = ENV_BACKEND_BME280;
                gBmp280Active = nullptr;
                gEnvWireActive = &w;
                gProbeRoute = routeName;
                return true;
            }
            continue;
        }

        if (chipId == 0x56 || chipId == 0x57 || chipId == 0x58) {
            Adafruit_BMP280 *bmp = envBmpForWire(w);
            if (bmp && bmp->begin(addr, chipId)) {
                gReady = true;
                gI2cAddr = addr;
                gBackend = ENV_BACKEND_BMP280;
                gBmp280Active = bmp;
                gEnvWireActive = &w;
                gProbeRoute = routeName;
                return true;
            }
            continue;
        }
    }

    // AHT20 fallback (temperature + humidity, no pressure).
    if (envAhtProbe(w)) {
        gReady = true;
        gI2cAddr = 0x38;
        gBackend = ENV_BACKEND_AHT20;
        gBmp280Active = nullptr;
        gEnvWireActive = &w;
        gProbeRoute = routeName;
        return true;
    }

    return false;
}

// Walk all known routes at both 400kHz and 100kHz looking for a supported
// sensor. The touch route is probed first to share its existing bus init.
static bool envTryProbeAllRoutes() {
    for (size_t i = 0; i < kEnvRouteCount; i++) {
        const EnvRoute &r = kEnvRoutes[i];
        if (envTryProbe(*r.wire, r.sda, r.scl, r.name, 400000U)
            || envTryProbe(*r.wire, r.sda, r.scl, r.name, 100000U)) {
            return true;
        }
    }
    return false;
}

// ── Diagnostics ───────────────────────────────────────────────────────────────

// Lightweight per-route scan: ACKs the addresses we care about and reads
// chip ID for the BMx280 candidates. Used at boot failure and on `env scan`.
static void envScanRoute(TwoWire &w, int sda, int scl, const char *routeName) {
    if (sda < 0 || scl < 0) return;

    w.begin(sda, scl, 100000U);

    bool has2E = envAddressPresent(w, 0x2E);
    bool has38 = envAddressPresent(w, 0x38);
    bool has76 = envAddressPresent(w, 0x76);
    bool has77 = envAddressPresent(w, 0x77);

    uint8_t id76 = 0;
    uint8_t id77 = 0;
    bool idOk76 = has76 && envReadChipId(w, 0x76, id76);
    bool idOk77 = has77 && envReadChipId(w, 0x77, id77);
    char id76Txt[8] = "--";
    char id77Txt[8] = "--";
    if (idOk76) snprintf(id76Txt, sizeof(id76Txt), "0x%02X", id76);
    if (idOk77) snprintf(id77Txt, sizeof(id77Txt), "0x%02X", id77);

    Serial.printf("[env] scan %s sda=%d scl=%d ack2E=%s ack38=%s ack76=%s ack77=%s ids76/77=%s/%s\n",
                  routeName ? routeName : "route",
                  sda, scl,
                  has2E ? "Y" : "N",
                  has38 ? "Y" : "N",
                  has76 ? "Y" : "N",
                  has77 ? "Y" : "N",
                  id76Txt, id77Txt);
}

// Exhaustive per-route scan: ACKs every address in 0x03..0x77. Used on
// `env scan all` to discover unknown sensors.
static void envScanRouteFull(TwoWire &w, int sda, int scl, const char *routeName) {
    if (sda < 0 || scl < 0) return;

    w.begin(sda, scl, 100000U);

    char addrs[224] = {};
    size_t used = 0;
    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (!envAddressPresent(w, addr)) continue;
        found++;
        if (used + 6 < sizeof(addrs)) {
            int n = snprintf(addrs + used, sizeof(addrs) - used,
                             "%s0x%02X",
                             (used == 0) ? "" : " ",
                             addr);
            if (n > 0) used += (size_t)n;
        }
    }
    if (found == 0) {
        snprintf(addrs, sizeof(addrs), "none");
    }

    uint8_t id76 = 0;
    uint8_t id77 = 0;
    bool idOk76 = envReadChipId(w, 0x76, id76);
    bool idOk77 = envReadChipId(w, 0x77, id77);
    char id76Txt[8] = "--";
    char id77Txt[8] = "--";
    if (idOk76) snprintf(id76Txt, sizeof(id76Txt), "0x%02X", id76);
    if (idOk77) snprintf(id77Txt, sizeof(id77Txt), "0x%02X", id77);

    Serial.printf("[env] full-scan %s sda=%d scl=%d count=%d addrs=%s ids76/77=%s/%s\n",
                  routeName ? routeName : "route",
                  sda, scl, found, addrs,
                  id76Txt, id77Txt);
}

// Walk the route table invoking `scanFn` on each entry. Used by both the
// quick-scan and full-scan diagnostic flows.
typedef void (*EnvScanFn)(TwoWire &, int, int, const char *);
static void envForEachRoute(EnvScanFn scanFn) {
    for (size_t i = 0; i < kEnvRouteCount; i++) {
        const EnvRoute &r = kEnvRoutes[i];
        scanFn(*r.wire, r.sda, r.scl, r.name);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

bool envBegin() {
    if (gReady) return true;

    uint32_t nowMs = millis();
    // Throttle retries so a missing sensor doesn't spam the bus.
    if (gLastInitAttemptMs != 0 && (uint32_t)(nowMs - gLastInitAttemptMs) < 5000UL) {
        return false;
    }
    gLastInitAttemptMs = nowMs;

    if (envTryProbeAllRoutes()) {
        Serial.printf("[env] detected %s via %s\n",
                      envSensorName(),
                      (gProbeRoute && gProbeRoute[0]) ? gProbeRoute : "route");
        gLoggedProbeFailure = false;
        return true;
    }

    // Log once per cold-start so failure is visible without repeating.
    if (!gLoggedProbeFailure) {
        Serial.println("[env] sensor probe failed (BME280/BMP280 @0x76/0x77, AHT20 @0x38)");
        envForEachRoute(envScanRoute);
        gLoggedProbeFailure = true;
    }
    return false;
}

bool envHasSensor() {
    return gReady;
}

bool envRead(EnvReading &out) {
    if (!gReady) return false;

    float t = NAN;
    float h = NAN;
    float p = NAN;

    if (gBackend == ENV_BACKEND_BME280) {
        t = gBme280.readTemperature();
        h = gBme280.readHumidity();
        p = gBme280.readPressure() / 100.0f;
    } else if (gBackend == ENV_BACKEND_BMP280) {
        if (!gBmp280Active) return false;
        t = gBmp280Active->readTemperature();
        h = 0.0f;
        p = gBmp280Active->readPressure() / 100.0f;
    } else if (gBackend == ENV_BACKEND_AHT20) {
        if (!gEnvWireActive) return false;
        if (!envAhtReadMeasurement(*gEnvWireActive, t, h)) return false;
        p = 0.0f;
    } else {
        return false;
    }

    if (isnan(t) || isnan(p) || (gBackend == ENV_BACKEND_BME280 && isnan(h))) return false;

    out.temperatureC = t;
    out.humidityPct = h;
    out.pressureHpa = p;
    return true;
}

const char *envSensorName() {
    if (!gReady) return "none";
    if (gBackend == ENV_BACKEND_BME280) {
        return (gI2cAddr == 0x77) ? "BME280@0x77" : "BME280@0x76";
    }
    if (gBackend == ENV_BACKEND_BMP280) {
        return (gI2cAddr == 0x77) ? "BMP280@0x77" : "BMP280@0x76";
    }
    if (gBackend == ENV_BACKEND_AHT20) {
        return "AHT20@0x38";
    }
    return "sensor";
}

bool envDebugScan(bool forceReprobe) {
    if (forceReprobe) envResetCachedState();

    bool ok = envBegin();
    if (ok) envForEachRoute(envScanRoute);
    return ok;
}

bool envDebugFullScan(bool forceReprobe) {
    if (forceReprobe) envResetCachedState();

    envForEachRoute(envScanRouteFull);

    bool ok = envTryProbeAllRoutes();
    if (ok) {
        Serial.printf("[env] full-scan detected %s via %s\n",
                      envSensorName(),
                      (gProbeRoute && gProbeRoute[0]) ? gProbeRoute : "route");
    } else {
        Serial.println("[env] full-scan detected no supported sensor");
    }
    return ok;
}

#else

bool envBegin() {
    return false;
}

bool envHasSensor() {
    return false;
}

bool envRead(EnvReading &out) {
    (void)out;
    return false;
}

const char *envSensorName() {
    return "none";
}

bool envDebugScan(bool forceReprobe) {
    (void)forceReprobe;
    return false;
}

bool envDebugFullScan(bool forceReprobe) {
    (void)forceReprobe;
    return false;
}

#endif
