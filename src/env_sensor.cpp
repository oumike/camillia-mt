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
    ENV_BACKEND_SHT4X,
    ENV_BACKEND_SHT3X,
    ENV_BACKEND_SHTC3,
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
// Sweeps run so far, and whether probing has been abandoned for this session.
// Without a ceiling a board with no sensor swept the bus twice a minute for its
// entire uptime — the feature failed *and* degraded the UI while failing.
static uint8_t gProbeSweeps = 0;
static bool gProbeGaveUp = false;
// Two, and both of them at boot.
//
// This was 3, then briefly 10 — raised on the theory that a slow power rail
// deserved more chances. Measurement killed that: one sweep costs *seconds*, so
// every extra attempt is seconds of frozen UI. A soldered sensor does not appear
// later, so the right number is "enough to cover a slow rail at boot" and no
// more. See issue #53.
static constexpr uint8_t kMaxProbeSweeps = 2;

// Per-transaction I2C timeout while probing. The Arduino default is 50 ms, and
// a sweep makes ~20 transactions across routes with nothing on them — plus the
// bus-recovery Wire.begin() runs when it finds SDA floating or held low, which
// is the expensive part on unassigned pins. Probing a bus that is not there
// should fail fast; a sensor that is there answers immediately.
static constexpr uint16_t kEnvProbeTimeoutMs = 12;

// ── Route table ───────────────────────────────────────────────────────────────
// SDA/SCL combinations to probe for the sensor.
//
// A board that declares ENV_SDA/ENV_SCL is probed on that route and nothing
// else. The sweep below is a bring-up tool for a board whose wiring is not known
// yet; it should not be the runtime path on a shipping profile.
//
// Two rules it has to respect, both learned from issue #53:
//
//   * Never probe a pin another driver owns. This table used to carry
//     { &Wire, 17, 18 }, which on the Heltec V4 is TFT_SPI_SCK and TFT_RST — so
//     a failed sweep reconfigured the display's clock and reset lines as
//     open-drain I2C with pull-ups, twice, while LovyanGFX was driving the
//     panel. No sensor could ever have answered there, so it was pure harm.
//   * Never re-begin() a bus that is already in use. The touch route shares its
//     port with the touch controller on boards where TOUCH_I2C_PORT matches, and
//     re-initialising it at two clock rates while LVGL's indev timer polls
//     through it is the same hazard main_lvgl.cpp already documents for the
//     keyboard and LovyanGFX. It is marked shared below and is only probed on
//     the first sweep, before the UI is doing much.
#if defined(ENV_I2C_PORT) && (ENV_I2C_PORT == 1)
#define ENV_WIRE_BUS Wire1
#else
#define ENV_WIRE_BUS Wire
#endif

struct EnvRoute {
    TwoWire *wire;
    int sda;
    int scl;
    const char *name;
    // True when another driver owns this bus, so it must not be re-begun once
    // the system is running.
    bool shared;
};

// The guessed routes are opt-in now (-DENV_PROBE_SHOTGUN=1). They cost real
// time on a board with nothing on them — a sweep including them measured 20 s —
// and they cannot succeed on a board whose sensor is somewhere else, so the
// default is to probe only what is actually known:
//
//   * a route the board declares (ENV_SDA/ENV_SCL), or
//   * the touch bus, which is already up and costs nothing extra to ask.
//
// Note the guessed routes never worked as intended anyway: TwoWire::begin() is a
// no-op once the port is initialised ("Bus already started in Master Mode"), so
// the second and subsequent Wire0 entries were re-probing the first one's pins
// rather than their own.
static const EnvRoute kEnvRoutes[] = {
#if defined(ENV_SDA) && defined(ENV_SCL)
    // Declared by the board profile: tried first.
    { &ENV_WIRE_BUS, ENV_SDA, ENV_SCL, "env-declared", false },
#endif
    // The touch bus is always worth asking, in addition to any declared route.
    // It is already up and known-good (touch works), so probing it costs one
    // transaction and no begin() at all.
    //
    // This entry used to sit in an #else against the declared route, which meant
    // that giving a board an ENV_SDA/ENV_SCL silently REMOVED the one bus known
    // to be alive on it. That is exactly what happened on the Heltec V4: adding
    // wadamesh's GPIO3/4 stopped the runtime probe looking at the touch bus, so
    // it found nothing, and the regression looked like a power-rail problem.
#if defined(TOUCH_SDA) && defined(TOUCH_SCL) && (TOUCH_SDA >= 0) && (TOUCH_SCL >= 0)
#if defined(TOUCH_I2C_PORT) && (TOUCH_I2C_PORT == 1)
    { &Wire1, TOUCH_SDA, TOUCH_SCL, "touch-route", true },
#else
    { &Wire,  TOUCH_SDA, TOUCH_SCL, "touch-route", true },
#endif
#endif
#if defined(ENV_PROBE_SHOTGUN) && ENV_PROBE_SHOTGUN
    { &Wire, 41, 42, "wire0-41/42", false },
    { &Wire,  3,  4, "wire0-3/4",   false },
#endif
};
static constexpr size_t kEnvRouteCount = sizeof(kEnvRoutes) / sizeof(kEnvRoutes[0]);

// Candidate routes for the *diagnostic* scans only ("i2c scan" / "i2c scan all"
// over serial). Deliberately wider than the runtime list: finding an undeclared
// sensor is exactly what those commands are for, and their cost is paid once,
// when a human asks, rather than on the announce path.
//
// TFT_SPI_SCK/TFT_RST are still excluded. Those belong to the display, nothing
// can answer there, and re-pinning them while LovyanGFX drives the panel is the
// hazard that started this.
static const EnvRoute kEnvDiagRoutes[] = {
#if (TOUCH_I2C_PORT == 1)
    { &Wire1, TOUCH_SDA, TOUCH_SCL, "touch-route", true },
#else
    { &Wire,  TOUCH_SDA, TOUCH_SCL, "touch-route", true },
#endif
#if defined(ENV_SDA) && defined(ENV_SCL)
    { &ENV_WIRE_BUS, ENV_SDA, ENV_SCL, "env-declared", false },
#endif
    { &Wire, 41, 42, "wire0-41/42", false },
    // Both orderings. Which line is SDA and which is SCL is a real ambiguity on
    // the Heltec V4 — the board JSON says 3/4, the TFT build flags say 4/3 —
    // and a scan that only tries one of them reports "nothing here" either way.
    { &Wire,  3,  4, "wire0-3/4",   false },
    { &Wire,  4,  3, "wire0-4/3",   false },
};
static constexpr size_t kEnvDiagRouteCount =
    sizeof(kEnvDiagRoutes) / sizeof(kEnvDiagRoutes[0]);

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

// ── Sensirion SHT4x / SHT3x (GXHT30, "GXHTV3") ───────────────────────────────
// Both answer at 0x44 (0x45 on the alternate strap) and both return the same
// 6-byte frame: temperature word + CRC, humidity word + CRC. They differ in the
// command set and in the humidity formula, so they are detected and read apart.
static uint8_t envSensirionCrc(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// Reads the 6-byte word/CRC/word/CRC frame both parts share. Returns false on a
// short read or a CRC mismatch, which is what makes this usable as a probe: an
// unrelated chip that happens to ACK at 0x44 will not produce two valid CRCs.
static bool envSensirionFrame(TwoWire &w, uint8_t addr, uint16_t &w0, uint16_t &w1) {
    uint8_t f[6];
    if (w.requestFrom(addr, (uint8_t)6, (uint8_t)true) != 6) return false;
    for (uint8_t i = 0; i < 6; i++) f[i] = (uint8_t)w.read();
    if (envSensirionCrc(&f[0], 2) != f[2]) return false;
    if (envSensirionCrc(&f[3], 2) != f[5]) return false;
    w0 = (uint16_t)(((uint16_t)f[0] << 8) | f[1]);
    w1 = (uint16_t)(((uint16_t)f[3] << 8) | f[4]);
    return true;
}

static bool envSensirionCmd(TwoWire &w, uint8_t addr, const uint8_t *cmd, size_t len) {
    w.beginTransmission(addr);
    if (w.write(cmd, len) != len) { (void)w.endTransmission(true); return false; }
    return w.endTransmission(true) == 0;
}

// SHT4x: single-byte commands. 0x89 reads the serial number, which is a
// side-effect-free way to confirm the part before trusting a measurement.
static bool envSht4xProbe(TwoWire &w, uint8_t addr) {
    if (!envAddressPresent(w, addr)) return false;
    static const uint8_t serialCmd[1] = { 0x89 };
    if (!envSensirionCmd(w, addr, serialCmd, sizeof(serialCmd))) return false;
    delay(10);
    uint16_t a = 0, b = 0;
    return envSensirionFrame(w, addr, a, b);
}

static bool envSht4xRead(TwoWire &w, uint8_t addr, float &temperatureC, float &humidityPct) {
    static const uint8_t measCmd[1] = { 0xFD };   // high repeatability, no heater
    if (!envSensirionCmd(w, addr, measCmd, sizeof(measCmd))) return false;
    delay(10);                                     // 8.2 ms max conversion
    uint16_t rawT = 0, rawH = 0;
    if (!envSensirionFrame(w, addr, rawT, rawH)) return false;
    temperatureC = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    float rh = -6.0f + 125.0f * ((float)rawH / 65535.0f);
    humidityPct = (rh < 0.0f) ? 0.0f : (rh > 100.0f ? 100.0f : rh);
    return true;
}

// SHT3x / GXHT30: two-byte commands. 0x2400 is single-shot, high repeatability,
// clock stretching disabled — the variant that does not hold SCL, which matters
// on a bus shared with the touch controller.
static const uint8_t kSht3xMeasCmd[2] = { 0x24, 0x00 };

static bool envSht3xRead(TwoWire &w, uint8_t addr, float &temperatureC, float &humidityPct) {
    if (!envSensirionCmd(w, addr, kSht3xMeasCmd, sizeof(kSht3xMeasCmd))) return false;
    delay(20);                                     // 15 ms max conversion
    uint16_t rawT = 0, rawH = 0;
    if (!envSensirionFrame(w, addr, rawT, rawH)) return false;
    temperatureC = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    float rh = 100.0f * ((float)rawH / 65535.0f);
    humidityPct = (rh < 0.0f) ? 0.0f : (rh > 100.0f ? 100.0f : rh);
    return true;
}

static bool envSht3xProbe(TwoWire &w, uint8_t addr) {
    if (!envAddressPresent(w, addr)) return false;
    float t = NAN, h = NAN;
    return envSht3xRead(w, addr, t, h) && !isnan(t) && !isnan(h);
}

// ── Sensirion SHTC3 ──────────────────────────────────────────────────────────
// Address 0x70, and the part wadamesh labels "GXHTV3" on the Heltec V4
// expansion board. Unlike the SHT3x/SHT4x it sleeps between conversions, so
// every exchange is wake -> measure -> sleep. Same 6-byte frame and same CRC.
static constexpr uint8_t kShtc3Addr = 0x70;

static bool envShtc3Wake(TwoWire &w) {
    static const uint8_t wakeCmd[2] = { 0x35, 0x17 };
    if (!envSensirionCmd(w, kShtc3Addr, wakeCmd, sizeof(wakeCmd))) return false;
    delayMicroseconds(300);          // 240 us max wake-up
    return true;
}

static void envShtc3Sleep(TwoWire &w) {
    static const uint8_t sleepCmd[2] = { 0xB0, 0x98 };
    (void)envSensirionCmd(w, kShtc3Addr, sleepCmd, sizeof(sleepCmd));
}

static bool envShtc3Read(TwoWire &w, float &temperatureC, float &humidityPct) {
    if (!envShtc3Wake(w)) return false;
    // 0x7866: temperature first, normal power, clock stretching DISABLED. Not
    // holding SCL matters on a bus shared with other traffic.
    static const uint8_t measCmd[2] = { 0x78, 0x66 };
    if (!envSensirionCmd(w, kShtc3Addr, measCmd, sizeof(measCmd))) {
        envShtc3Sleep(w);
        return false;
    }
    delay(15);                        // 12.1 ms max conversion
    uint16_t rawT = 0, rawH = 0;
    const bool ok = envSensirionFrame(w, kShtc3Addr, rawT, rawH);
    envShtc3Sleep(w);
    if (!ok) return false;
    temperatureC = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    float rh = 100.0f * ((float)rawH / 65535.0f);
    humidityPct = (rh < 0.0f) ? 0.0f : (rh > 100.0f ? 100.0f : rh);
    return true;
}

static bool envShtc3Probe(TwoWire &w) {
    if (!envAddressPresent(w, kShtc3Addr)) return false;

    // 0x70 is also the base address of the TCA9548A I2C mux, so ACK alone means
    // nothing here. Two independent confirmations are accepted:
    //
    //   1. the ID register reads back SHTC3's signature, or
    //   2. a measurement comes back with both CRCs valid and plausible values.
    //
    // The ID check alone was too strict: the Heltec V4 expansion's part is a
    // GXHTV3 (an SHTC3 work-alike) and its ID word does not match Sensirion's
    // signature, so a genuine, readable sensor was being rejected. A mux still
    // cannot pass (2) — it has no conversion to return and no CRC to get right.
    if (!envShtc3Wake(w)) return false;
    static const uint8_t idCmd[2] = { 0xEF, 0xC8 };
    uint16_t id = 0;
    bool idValid = false;
    if (envSensirionCmd(w, kShtc3Addr, idCmd, sizeof(idCmd))) {
        delay(2);
        uint8_t f[3];
        if (w.requestFrom(kShtc3Addr, (uint8_t)3, (uint8_t)true) == 3) {
            for (uint8_t i = 0; i < 3; i++) f[i] = (uint8_t)w.read();
            if (envSensirionCrc(&f[0], 2) == f[2]) {
                id = (uint16_t)(((uint16_t)f[0] << 8) | f[1]);
                idValid = true;
            }
        }
    }
    envShtc3Sleep(w);

    if (idValid && (id & 0x083F) == 0x0807) {
        Serial.printf("[env] SHTC3 @0x%02X id=0x%04X\n", kShtc3Addr, id);
        return true;
    }

    float t = NAN, h = NAN;
    const bool measured = envShtc3Read(w, t, h);
    const bool plausible = measured && !isnan(t) && !isnan(h)
                           && t > -50.0f && t < 125.0f
                           && h >= 0.0f && h <= 100.0f;
    Serial.printf("[env] 0x%02X id=%s measure=%s%s\n", kShtc3Addr,
                  idValid ? "ok" : "bad",
                  measured ? "ok" : "fail",
                  plausible ? " (accepted as SHTC3-class)" : "");
    return plausible;
}

// ── Detected-sensor table ────────────────────────────────────────────────────
// Filled after a successful probe by sweeping the winning bus for every part we
// know. envRead()/telemetry still use the primary backend above; this table is
// what the Device Info page lists.
struct EnvDetected {
    EnvBackend backend;
    uint8_t    addr;
};
static EnvDetected gDetected[4];
static uint8_t     gDetectedCount = 0;

static void envDetectedAdd(EnvBackend backend, uint8_t addr) {
    const uint8_t cap = (uint8_t)(sizeof(gDetected) / sizeof(gDetected[0]));
    if (gDetectedCount >= cap) return;
    for (uint8_t i = 0; i < gDetectedCount; i++) {
        if (gDetected[i].backend == backend && gDetected[i].addr == addr) return;
    }
    gDetected[gDetectedCount].backend = backend;
    gDetected[gDetectedCount].addr    = addr;
    gDetectedCount++;
}

static const char *envBackendName(EnvBackend backend) {
    switch (backend) {
        case ENV_BACKEND_BME280: return "BME280";
        case ENV_BACKEND_BMP280: return "BMP280";
        case ENV_BACKEND_AHT20:  return "AHT20";
        case ENV_BACKEND_SHT4X:  return "SHT4x";
        case ENV_BACKEND_SHT3X:  return "SHT3x";
        case ENV_BACKEND_SHTC3:  return "SHTC3";
        default:                 return "sensor";
    }
}

// "BME280@0x76". Rotating buffers rather than one static: the Device Info page
// asks for every sensor's info before it draws any of them, and with a single
// buffer each row would end up showing the last sensor's name.
static const char *envBackendLabel(EnvBackend backend, uint8_t addr) {
    static char bufs[4][20];
    static uint8_t next = 0;
    char *out = bufs[next];
    next = (uint8_t)((next + 1) % (sizeof(bufs) / sizeof(bufs[0])));
    snprintf(out, sizeof(bufs[0]), "%s@0x%02X", envBackendName(backend), addr);
    return out;
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
static bool envTryProbe(TwoWire &w, int sda, int scl, const char *routeName,
                        uint32_t freqHz, bool ownBus) {
    if (sda < 0 || scl < 0) return false;

    // Only initialise a bus this module owns. A bus another driver brought up is
    // probed exactly as it stands: re-begin()ing it resets the pins and clock
    // underneath that driver, which is what made the touch controller flaky, and
    // it buys nothing — an address probe does not care what rate the bus runs at.
    //
    // Skipping the begin() is also what lets a shared route be probed on every
    // sweep instead of only at boot. Restricting it to boot meant a sensor that
    // shares the touch bus and was not powered up yet could never be found at
    // all, which is strictly worse than the hazard it was avoiding.
    const uint16_t prevTimeout = w.getTimeOut();
    w.setTimeOut(kEnvProbeTimeoutMs);
    if (ownBus) {
        // end() first, or begin() may be a no-op and we probe someone else's
        // pins. TwoWire::begin() returns early with "Bus already started in
        // Master Mode" when the port is already up, and on this board something
        // can get there first — battery_util's bqEnsureWire() calls
        // Wire.begin(KB_SDA, KB_SCL), which is Wire.begin(-1, -1) on a profile
        // with no keyboard bus.
        //
        // wadamesh does not hit this because MeshCore never begins the sensor
        // bus at all when ENV_PIN_SDA/SCL are undefined — it inherits a Wire0
        // its board setup already brought up on these same pins.
        w.end();
        if (!w.begin(sda, scl, freqHz)) {
            Serial.printf("[env] Wire.begin(sda=%d scl=%d @%luHz) FAILED on %s\n",
                          sda, scl, (unsigned long)freqHz, routeName ? routeName : "route");
            w.setTimeOut(prevTimeout);
            return false;
        }
    }

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
                w.setTimeOut(prevTimeout);
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
                w.setTimeOut(prevTimeout);
                return true;
            }
            continue;
        }
    }

    // Sensirion SHT4x / SHT3x (GXHT30) at 0x44, alternate strap 0x45. This is
    // the "GXHTV3/SHT4X (ch3)" part wadamesh reports on the Heltec V4.
    {
        const uint8_t shtAddrs[] = { 0x44, 0x45 };
        for (size_t i = 0; i < sizeof(shtAddrs); i++) {
            const uint8_t addr = shtAddrs[i];
            if (envSht4xProbe(w, addr)) {
                gReady = true;
                gI2cAddr = addr;
                gBackend = ENV_BACKEND_SHT4X;
                gBmp280Active = nullptr;
                gEnvWireActive = &w;
                gProbeRoute = routeName;
                w.setTimeOut(prevTimeout);
                return true;
            }
            if (envSht3xProbe(w, addr)) {
                gReady = true;
                gI2cAddr = addr;
                gBackend = ENV_BACKEND_SHT3X;
                gBmp280Active = nullptr;
                gEnvWireActive = &w;
                gProbeRoute = routeName;
                w.setTimeOut(prevTimeout);
                return true;
            }
        }
    }

    // SHTC3 at 0x70 (the Heltec V4 expansion's "GXHTV3").
    if (envShtc3Probe(w)) {
        gReady = true;
        gI2cAddr = kShtc3Addr;
        gBackend = ENV_BACKEND_SHTC3;
        gBmp280Active = nullptr;
        gEnvWireActive = &w;
        gProbeRoute = routeName;
        w.setTimeOut(prevTimeout);
        return true;
    }

    // AHT20 fallback (temperature + humidity, no pressure).
    if (envAhtProbe(w)) {
        gReady = true;
        gI2cAddr = 0x38;
        gBackend = ENV_BACKEND_AHT20;
        gBmp280Active = nullptr;
        gEnvWireActive = &w;
        gProbeRoute = routeName;
        w.setTimeOut(prevTimeout);
        return true;
    }

    w.setTimeOut(prevTimeout);
    // Nothing here: hand the port back rather than leaving it re-pinned to a
    // route we are not using. end() means the next caller's begin() actually
    // takes effect instead of hitting "Bus already started in Master Mode" and
    // silently inheriting our pins — which is how a failed probe started
    // breaking unrelated I2C traffic ("requestFrom(): ... Error 263").
    if (ownBus) w.end();
    return false;
}

// Walk all known routes at both 400kHz and 100kHz looking for a supported
// sensor. The touch route is probed first to share its existing bus init.
static bool envTryProbeAllRoutes() {
    // Per-route timing. A sweep on this board measured 20.002 s and three
    // separate theories about where that went were all wrong — bounded I2C
    // timeouts changed nothing, Wire.begin() is a no-op once the port is up, and
    // the address scan is only four addresses and runs once. So it reports
    // itself now instead of being reasoned about.
    const uint32_t sweepStartMs = millis();
    for (size_t i = 0; i < kEnvRouteCount; i++) {
        const EnvRoute &r = kEnvRoutes[i];
        const uint32_t routeStartMs = millis();
        bool hit;
        if (r.shared) {
            // One pass, no begin(), at whatever clock its owner set.
            hit = envTryProbe(*r.wire, r.sda, r.scl, r.name, 0, /*ownBus=*/false);
        } else {
            hit = envTryProbe(*r.wire, r.sda, r.scl, r.name, 400000U, /*ownBus=*/true)
               || envTryProbe(*r.wire, r.sda, r.scl, r.name, 100000U, /*ownBus=*/true);
        }
        const uint32_t routeMs = millis() - routeStartMs;
        if (routeMs >= 50) {
            Serial.printf("[env] probe %s took %lums%s\n",
                          r.name, (unsigned long)routeMs, hit ? " (hit)" : "");
        }
        if (hit) return true;
    }
    const uint32_t sweepMs = millis() - sweepStartMs;
    if (sweepMs >= 50) {
        Serial.printf("[env] sweep took %lums\n", (unsigned long)sweepMs);
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
    bool has44 = envAddressPresent(w, 0x44);
    bool has45 = envAddressPresent(w, 0x45);
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

    Serial.printf("[env] scan %s sda=%d scl=%d ack2E=%s ack38=%s ack44=%s ack45=%s "
                  "ack76=%s ack77=%s ids76/77=%s/%s\n",
                  routeName ? routeName : "route",
                  sda, scl,
                  has2E ? "Y" : "N",
                  has38 ? "Y" : "N",
                  has44 ? "Y" : "N",
                  has45 ? "Y" : "N",
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
// The routes the runtime probe actually uses. Used by the once-per-boot failure
// log, which sits on the announce path and must stay cheap — reporting on buses
// nothing looked at would be misleading as well as slow.
static void envForEachRoute(EnvScanFn scanFn) {
    for (size_t i = 0; i < kEnvRouteCount; i++) {
        const EnvRoute &r = kEnvRoutes[i];
        // Same reason as envTryProbe(): a route we own has to be re-pinned, or
        // the scan reports on whichever pins Wire0 happened to already have.
        if (!r.shared) r.wire->end();
        scanFn(*r.wire, r.sda, r.scl, r.name);
    }
}

// The wide list, for the explicit "i2c scan" / "i2c scan all" commands only.
static void envForEachDiagRoute(EnvScanFn scanFn) {
    for (size_t i = 0; i < kEnvDiagRouteCount; i++) {
        const EnvRoute &r = kEnvDiagRoutes[i];
        // TwoWire::begin() is a no-op once the port is up, so without this the
        // second and later Wire0 routes would quietly re-scan the first one's
        // pins and report its answers under their own name. That is why the
        // original sweep could never have cleared 3/4 — it never reached them.
        //
        // Not done for a bus another driver owns: tearing that down would break
        // touch. It is already initialised, and scanning it as-is is correct.
        if (!r.shared) r.wire->end();
        scanFn(*r.wire, r.sda, r.scl, r.name);
    }
    // Leave the shared bus as we found it rather than pointing Wire0 at whatever
    // route happened to be last.
    for (size_t i = 0; i < kEnvRouteCount; i++) {
        const EnvRoute &r = kEnvRoutes[i];
        if (!r.shared) { r.wire->end(); r.wire->begin(r.sda, r.scl, 100000U); }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

// Sweeps the bus the primary was found on for every part we know, so the Device
// Info page can list each one. The primary is always entry 0: it is the sensor
// telemetry reports, and keeping it first means the page and the mesh agree
// about which reading is "the" reading.
//
// A second BMx280 is deliberately not enumerated. Reading one needs its own
// Adafruit driver instance bound to that address, and there is exactly one
// instance here — recording an address we cannot then read would put a row on
// the page that never shows a value.
static void envEnumerateDetected() {
    gDetectedCount = 0;
    if (!gReady) return;

    envDetectedAdd(gBackend, gI2cAddr);
    if (!gEnvWireActive) return;
    TwoWire &w = *gEnvWireActive;

    if (gBackend != ENV_BACKEND_SHTC3 && envShtc3Probe(w)) {
        envDetectedAdd(ENV_BACKEND_SHTC3, kShtc3Addr);
    }
    const uint8_t shtAddrs[] = { 0x44, 0x45 };
    for (size_t i = 0; i < sizeof(shtAddrs); i++) {
        const uint8_t addr = shtAddrs[i];
        if (gI2cAddr == addr && (gBackend == ENV_BACKEND_SHT4X || gBackend == ENV_BACKEND_SHT3X)) {
            continue;
        }
        if (envSht4xProbe(w, addr))      envDetectedAdd(ENV_BACKEND_SHT4X, addr);
        else if (envSht3xProbe(w, addr)) envDetectedAdd(ENV_BACKEND_SHT3X, addr);
    }
    if (gBackend != ENV_BACKEND_AHT20 && envAhtProbe(w)) {
        envDetectedAdd(ENV_BACKEND_AHT20, 0x38);
    }

    if (gDetectedCount > 1) {
        Serial.printf("[env] %u sensors on %s:", (unsigned)gDetectedCount,
                      (gProbeRoute && gProbeRoute[0]) ? gProbeRoute : "route");
        for (uint8_t i = 0; i < gDetectedCount; i++) {
            Serial.printf(" %s@0x%02X", envBackendName(gDetected[i].backend),
                          gDetected[i].addr);
        }
        Serial.println();
    }
}

uint8_t envSensorCount() {
    return gDetectedCount;
}

bool envSensorInfoAt(uint8_t idx, EnvSensorInfo &out) {
    if (idx >= gDetectedCount || !gEnvWireActive) return false;
    TwoWire &w = *gEnvWireActive;
    const EnvBackend backend = gDetected[idx].backend;
    const uint8_t addr = gDetected[idx].addr;

    out.name = envBackendLabel(backend, addr);
    out.hasTemperature = out.hasHumidity = out.hasPressure = false;
    out.temperatureC = out.humidityPct = out.pressureHpa = 0.0f;

    float t = NAN, h = NAN;
    switch (backend) {
        case ENV_BACKEND_BME280: {
            EnvReading r;
            if (!envRead(r)) return false;
            out.temperatureC = r.temperatureC;
            out.humidityPct  = r.humidityPct;
            out.pressureHpa  = r.pressureHpa;
            out.hasTemperature = out.hasHumidity = out.hasPressure = true;
            return true;
        }
        case ENV_BACKEND_BMP280: {
            EnvReading r;
            if (!envRead(r)) return false;
            out.temperatureC = r.temperatureC;
            out.pressureHpa  = r.pressureHpa;
            out.hasTemperature = out.hasPressure = true;
            return true;
        }
        case ENV_BACKEND_SHTC3:
            if (!envShtc3Read(w, t, h)) return false;
            break;
        case ENV_BACKEND_SHT4X:
            if (!envSht4xRead(w, addr, t, h)) return false;
            break;
        case ENV_BACKEND_SHT3X:
            if (!envSht3xRead(w, addr, t, h)) return false;
            break;
        case ENV_BACKEND_AHT20:
            if (!envAhtReadMeasurement(w, t, h)) return false;
            break;
        default:
            return false;
    }

    if (isnan(t) || isnan(h)) return false;
    out.temperatureC = t;
    out.humidityPct  = h;
    out.hasTemperature = out.hasHumidity = true;
    return true;
}

bool envBegin() {
    if (gReady) return true;
    // Cheap and final once the ceiling is hit, so a caller that keeps asking
    // costs nothing rather than re-sweeping the bus.
    if (gProbeGaveUp) return false;

    uint32_t nowMs = millis();
    // Throttle retries so a missing sensor doesn't spam the bus.
    if (gLastInitAttemptMs != 0 && (uint32_t)(nowMs - gLastInitAttemptMs) < 5000UL) {
        return false;
    }
    gLastInitAttemptMs = nowMs;

    const bool found = envTryProbeAllRoutes();
    gProbeSweeps++;

    if (found) {
        Serial.printf("[env] detected %s via %s\n",
                      envSensorName(),
                      (gProbeRoute && gProbeRoute[0]) ? gProbeRoute : "route");
        // The primary is whatever answered first; the board may carry more than
        // one part on that same bus, and the Device Info page lists them all.
        envEnumerateDetected();
        gLoggedProbeFailure = false;
        return true;
    }

    // Log once per cold-start so failure is visible without repeating.
    if (!gLoggedProbeFailure) {
        Serial.println("[env] sensor probe failed (BME280/BMP280 @0x76/0x77, "
                       "SHT4x/SHT3x @0x44/0x45, AHT20 @0x38)");
        envForEachRoute(envScanRoute);
        gLoggedProbeFailure = true;
    }

    if (gProbeSweeps >= kMaxProbeSweeps) {
        gProbeGaveUp = true;
        Serial.printf("[env] no sensor found after %u sweeps - probing stopped "
                      "for this session\n", (unsigned)gProbeSweeps);
    }
    return false;
}

bool envProbeExhausted() {
    return gProbeGaveUp;
}

const char *envProbeRouteName() {
    return (gProbeRoute && gProbeRoute[0]) ? gProbeRoute : "none";
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
    } else if (gBackend == ENV_BACKEND_SHT4X) {
        if (!gEnvWireActive) return false;
        if (!envSht4xRead(*gEnvWireActive, gI2cAddr, t, h)) return false;
        p = 0.0f;
    } else if (gBackend == ENV_BACKEND_SHT3X) {
        if (!gEnvWireActive) return false;
        if (!envSht3xRead(*gEnvWireActive, gI2cAddr, t, h)) return false;
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
    if (gBackend == ENV_BACKEND_SHT4X) {
        return (gI2cAddr == 0x45) ? "SHT4x@0x45" : "SHT4x@0x44";
    }
    if (gBackend == ENV_BACKEND_SHT3X) {
        return (gI2cAddr == 0x45) ? "SHT3x@0x45" : "SHT3x@0x44";
    }
    return "sensor";
}

bool envDebugScan(bool forceReprobe) {
    if (forceReprobe) envResetCachedState();

    bool ok = envBegin();
    if (ok) envForEachDiagRoute(envScanRoute);
    return ok;
}

bool envDebugFullScan(bool forceReprobe) {
    if (forceReprobe) envResetCachedState();

    envForEachDiagRoute(envScanRouteFull);

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

bool envProbeExhausted() {
    // Nothing to probe on this board, so it has always given up.
    return true;
}

const char *envProbeRouteName() {
    return "none";
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

uint8_t envSensorCount() {
    return 0;
}

bool envSensorInfoAt(uint8_t idx, EnvSensorInfo &out) {
    (void)idx;
    (void)out;
    return false;
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
