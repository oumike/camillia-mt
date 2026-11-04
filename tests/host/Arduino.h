// Host stand-in for <Arduino.h>: just what Arduino-free code under test needs.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
using std::max;
using std::min;

enum adc_attenuation_t { ADC_11db };
uint32_t millis();
uint32_t analogReadMilliVolts(uint8_t pin);
uint16_t analogRead(uint8_t pin);
void analogSetPinAttenuation(uint8_t pin, adc_attenuation_t attenuation);
