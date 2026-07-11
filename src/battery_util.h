#pragma once
#include <Arduino.h>

// Initialize battery ADC and optional board-specific sense-enable pin.
void batteryInitAdc();

// Read local battery voltage in volts.
float batteryReadVoltage();

// Read local battery percentage mapped from voltage via Li-ion SOC curve.
uint8_t batteryReadPercent();
