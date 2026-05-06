#pragma once
#include <Arduino.h>

// Initialize battery ADC and optional board-specific sense-enable pin.
void batteryInitAdc();

// Read local battery voltage in volts.
float batteryReadVoltage();

// Read local battery percentage mapped to BATT_VMIN..BATT_VMAX.
uint8_t batteryReadPercent();
