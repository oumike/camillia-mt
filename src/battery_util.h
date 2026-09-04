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

// Cardputer only, and a no-op elsewhere: M5Unified owns that board's battery
// ADC and must have been begun before it can be read at all. Called by the
// keyboard driver, which is what calls M5Cardputer.begin().
//
// Until it is, batteryReadVoltage() and friends report "unknown" rather than
// reading through an uninitialised driver — which matters because the
// low-battery boot gate deliberately runs before that init, and an invented
// number there would switch the device off.
void batteryNoteM5Ready();

// Forces a fresh sample and returns it with the calibration trim applied, or
// 0.0f when the source has nothing to report yet. Bypasses both the ~1.2 s
// display cadence and the smoothing filter, because the low-battery boot gate
// cannot wait for either and must be able to tell "no answer" from "flat".
//
// On the BQ25896 boards the first call only *starts* a conversion and returns
// 0.0f; callers poll until a non-zero answer arrives or they give up.
float batteryReadVoltageNow();

// Whether external power (USB/charger) is present. Returns false both when
// there is none and when this board cannot tell, so callers that care about the
// difference must pass `known` — guessing here would risk refusing to charge a
// flat device, which is a worse failure than the one the cutoff prevents.
bool batteryExternalPowerPresent(bool *known = nullptr);

// True when this board can disconnect the battery in hardware. On the BQ25896
// boards that is a real battery cutoff which recovers when USB is plugged in;
// elsewhere there is nothing to do and callers fall back to deep sleep.
bool batteryHardwarePowerOffSupported();

// Open the battery FET. Returns false when unsupported or the write failed.
// Does not return on success — the rail it was powering is gone.
bool batteryHardwarePowerOff();

// One-line diagnostic of the raw reading behind the displayed percentage, so a
// suspect value can be compared against a meter: the board's raw source (charger
// register, ADC millivolts) alongside the filtered voltage and shown percent.
void batteryDebugSnapshot(char *out, size_t outLen);
