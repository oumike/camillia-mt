#pragma once

#include <stdint.h>

bool tdisplayP4IoBegin();
bool tdisplayP4IoReady();

bool tdisplayP4IoSetScreenResetReleased(bool released);
bool tdisplayP4IoSetTouchResetReleased(bool released);
bool tdisplayP4IoSetAudioPower(bool enabled);
bool tdisplayP4IoSetSdPower(bool enabled);
bool tdisplayP4IoSetEsp32C6Power(bool enabled);
bool tdisplayP4IoResetEsp32C6();
bool tdisplayP4IoSetGpsAwake(bool awake);
bool tdisplayP4IoReadGpsWake(bool &latchedHigh, bool &sampledHigh,
							 bool &configuredOutput);
bool tdisplayP4IoSetRadioResetReleased(bool released);
bool tdisplayP4IoSelectInternalAntenna(bool internal);

bool tdisplayP4IoReadRadioDio1(bool &high);
bool tdisplayP4IoClearInterrupt();
bool tdisplayP4IoPrepareForSleep();

// I2C controller 1 (Wire1) serves two pin pairs on this board: the keyboard
// accessory's TCA8418 on 46/45, and the ES8311 audio codec on 20/21. The P4 has
// only two general-purpose I2C controllers and controller 0 is the main bus, so
// the two take turns: this points Wire1 at the one asked for -- re-pinning only
// when it is currently on the other -- and returns it.
//
// Only safe because both are driven from the main loop task and never overlap.
// LilyGO drives the keyboard with software I2C to avoid this; here the keyboard
// driver is written against TwoWire, so it keeps the hardware bus and shares.
class TwoWire;
enum TdisplayP4I2c1Route : uint8_t {
    TDISPLAY_P4_I2C1_KEYBOARD,
    TDISPLAY_P4_I2C1_AUDIO,
};
TwoWire &tdisplayP4I2c1(TdisplayP4I2c1Route route);

// Makes every software restart (ESP.restart(), esp_restart()) a full system
// reset. On the P4 esp_restart() is a CPU reset that leaves the MIPI-DSI host
// as the last session configured it, and the display then came up blank after
// every settings change, import or orientation switch until reset by hand.
// Call once, as early in setup() as possible (see the .cpp for why early).
void tdisplayP4InstallFullRestart();
// True once, on the boot that follows one of those restarts. It then reports
// as a watchdog reset (ESP_RST_WDT), and code that tells deliberate restarts
// from crashes by reset reason asks this to know it was a software restart.
bool tdisplayP4ConsumeFullRestartMarker();
