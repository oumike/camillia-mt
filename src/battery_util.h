#pragma once
#include <Arduino.h>

// Initialize battery ADC and optional board-specific sense-enable pin.
void batteryInitAdc();

// Read local battery voltage in volts.
float batteryReadVoltage();

// Apply the user's per-unit voltage trim, in parts per thousand (0 = none, +25
// means the hardware reads 2.5% low and every reading is scaled up by that).
// Multiplies with the board's compile-time BATT_CAL rather than replacing it.
// Resets the smoothing filter, so the next read reflects the new trim instead of
// easing into it over several samples.
void batterySetCalibrationTrim(int trimPermille);

// Battery voltage with the trim removed — what the hardware actually reported.
// Unfiltered and unsmoothed: this is the number the calibration UI compares
// against a meter, and it must not lag behind the trim being adjusted. Returns
// 0 when the source has nothing to report.
float batteryReadVoltageUntrimmed();

// Read local battery percentage mapped from voltage via Li-ion SOC curve.
uint8_t batteryReadPercent();

// One-line diagnostic of the raw reading behind the displayed percentage, so a
// suspect value can be compared against a meter: the board's raw source (charger
// register, ADC millivolts) alongside the filtered voltage and shown percent.
void batteryDebugSnapshot(char *out, size_t outLen);
