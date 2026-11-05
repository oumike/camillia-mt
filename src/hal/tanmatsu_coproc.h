#pragma once
// Tanmatsu CH32V203 coprocessor over I2C (Wire, BOARD_I2C_SDA/SCL, 0x5F).
// Keyboard matrix, display/keyboard backlight, PMIC ADC and charger status,
// ESP32-C6 power control and system power-off all live behind it.
// Register map: nicolaielectronics/tanmatsu-coprocessor.

#include <stddef.h>
#include <stdint.h>

#define TANMATSU_KEYBOARD_BYTES 9

bool tanmatsuCoprocBegin();
bool tanmatsuCoprocReady();

bool tanmatsuCoprocRead(uint8_t reg, uint8_t *buf, size_t len);
bool tanmatsuCoprocWrite(uint8_t reg, const uint8_t *buf, size_t len);

bool tanmatsuCoprocReadKeys(uint8_t out[TANMATSU_KEYBOARD_BYTES]);
bool tanmatsuCoprocSetDisplayBacklight(uint8_t level);
bool tanmatsuCoprocSetKeyboardBacklight(uint8_t level);

// Turns the ESP32-C6 off and back on into its application firmware. Required
// at every P4 boot: a P4 reset leaves the C6's ESP-Hosted state out of step.
bool tanmatsuCoprocRadioPowerCycle();

// Battery voltage in mV from the PMIC ADC, 0 when unreadable.
uint16_t tanmatsuCoprocBatteryMv();
// Returns false when the status register cannot be read.
bool tanmatsuCoprocChargeStatus(bool *batteryAttached, bool *usbPresent,
                                bool *charging);
bool tanmatsuCoprocPowerButtonPressed();
void tanmatsuCoprocPowerOff();

// Front RGB LEDs (TANMATSU_LED_*). The first call switches the coprocessor to
// manual LED mode, which turns off its own power/radio indicators, so whoever
// calls this owns all six from then on. Unchanged colors are not rewritten.
bool tanmatsuCoprocSetLed(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

// Speaker amplifier enable (OUTPUT bit 0).
bool tanmatsuCoprocSetAmplifier(bool on);

// ES8156 DAC at I2C 0x08 on the coprocessor's bus: init for 16-bit I2S slave
// with MCLK, and set its digital volume (0-255).
bool tanmatsuCodecBegin();
bool tanmatsuCodecSetVolume(uint8_t vol);
