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

// Reads the latest environment sample.
// Returns false when no sensor is available or the read fails.
bool envRead(EnvReading &out);

// Human-friendly sensor backend name, or "none".
const char *envSensorName();

// Run a runtime diagnostic scan/probe of known environment sensor routes.
// If forceReprobe is true, clears cached state and probes immediately.
// Returns true when a supported sensor is detected.
bool envDebugScan(bool forceReprobe = true);

// Run an extended runtime I2C scan (0x03..0x77) across known routes.
// Also performs a supported-sensor reprobe and returns true if detected.
bool envDebugFullScan(bool forceReprobe = true);
