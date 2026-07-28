#pragma once
#include <Arduino.h>

// Initialize battery ADC and optional board-specific sense-enable pin.
void batteryInitAdc();

// Read local battery voltage in volts.
float batteryReadVoltage();

// Read local battery percentage mapped from voltage via Li-ion SOC curve.
uint8_t batteryReadPercent();

// One-line diagnostic of the raw reading behind the displayed percentage, so a
// suspect value can be compared against a meter: the board's raw source (charger
// register, ADC millivolts) alongside the filtered voltage and shown percent.
void batteryDebugSnapshot(char *out, size_t outLen);
