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
