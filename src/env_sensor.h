#pragma once

#include <stdint.h>

struct EnvReading {
    float temperatureC;
    float humidityPct;
    float pressureHpa;
};

// Initialize local environment sensor(s) when available.
bool envBegin();

// Returns true when at least one environment sensor is available.
bool envHasSensor();

// True once probing has been abandoned for this session, so a caller can stop
// asking on a short cadence. Always true on a board with no env support built in.
bool envProbeExhausted();

// Name of the route the sensor was found on ("none" when nothing was detected).
const char *envProbeRouteName();

// Reads the latest environment sample.
// Returns false when no sensor is available or the read fails.
bool envRead(EnvReading &out);

// Human-friendly sensor backend name, or "none".
const char *envSensorName();

// ── Per-sensor enumeration ───────────────────────────────────────────────────
// envRead() above reports the *primary* sensor, which is what telemetry sends.
// A board can carry more than one part on the same bus (the Heltec V4 expansion
// has a BME280 at 0x76 and an SHTC3 at 0x70), and the Device Info page lists
// them individually, so these expose each detected sensor separately.
//
// Not every part measures every quantity — a BMP280 has no humidity, an SHTC3 no
// pressure — hence the per-field validity flags rather than a sentinel value.
struct EnvSensorInfo {
    const char *name;        // e.g. "BME280@0x76"
    bool  hasTemperature;
    bool  hasHumidity;
    bool  hasPressure;
    float temperatureC;
    float humidityPct;
    float pressureHpa;
};

// Number of individually detected sensors (0 when none).
uint8_t envSensorCount();

// Reads detected sensor `idx` (0-based). Returns false for an out-of-range index
// or a failed read.
bool envSensorInfoAt(uint8_t idx, EnvSensorInfo &out);

// Run a runtime diagnostic scan/probe of known environment sensor routes.
// If forceReprobe is true, clears cached state and probes immediately.
// Returns true when a supported sensor is detected.
bool envDebugScan(bool forceReprobe = true);

// Run an extended runtime I2C scan (0x03..0x77) across known routes.
// Also performs a supported-sensor reprobe and returns true if detected.
bool envDebugFullScan(bool forceReprobe = true);
