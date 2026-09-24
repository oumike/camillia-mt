#include "tdisplay_p4_io.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_system.h>
#include <hal/wdt_hal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "hw_tdisplay_p4.h"
#include "xl9555.h"

namespace {

uint8_t s_out0 = 0xFF;
uint8_t s_out1 = 0xFF;
uint8_t s_cfg0 = 0xFF;
uint8_t s_cfg1 = 0xFF;
bool s_ready = false;
SemaphoreHandle_t s_mutex = nullptr;

bool lock() {
    return s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(250)) == pdTRUE;
}

void unlock() {
    xSemaphoreGive(s_mutex);
}

void stageOutput(uint8_t bit, bool high) {
    xl9555SetOutput(bit, high, s_out0, s_out1, s_cfg0, s_cfg1);
}

bool writeState() {
    return xl9555WriteAll(TDISPLAY_P4_EXPANDER_ADDR,
                          s_out0, s_out1, s_cfg0, s_cfg1);
}

bool setOutput(uint8_t bit, bool high) {
    if (!s_ready || !lock()) return false;

    const uint8_t previousOut0 = s_out0;
    const uint8_t previousOut1 = s_out1;
    const uint8_t previousCfg0 = s_cfg0;
    const uint8_t previousCfg1 = s_cfg1;
    stageOutput(bit, high);
    const bool ok = writeState();
    if (!ok) {
        s_out0 = previousOut0;
        s_out1 = previousOut1;
        s_cfg0 = previousCfg0;
        s_cfg1 = previousCfg1;
    }
    unlock();
    return ok;
}

bool readInput(uint8_t bit, bool &high) {
    high = false;
    if (!s_ready || !lock()) return false;
    uint8_t value = 0;
    const uint8_t reg = bit < 8 ? XL9555_REG_IN0 : XL9555_REG_IN1;
    const bool ok = xl9555ReadReg(TDISPLAY_P4_EXPANDER_ADDR, reg, value);
    if (ok) high = (value & (uint8_t)(1U << (bit & 7U))) != 0;
    unlock();
    return ok;
}

} // namespace

bool tdisplayP4IoBegin() {
    if (s_ready) return true;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex || !lock()) return false;

    pinMode(TDISPLAY_P4_EXPANDER_INT, INPUT_PULLUP);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);
    Wire.setClock(BOARD_I2C_FREQ);

    Serial.printf("[tdisplay-p4-io] i2c sda=%d scl=%d expander=0x%02X\n",
                  BOARD_I2C_SDA, BOARD_I2C_SCL, TDISPLAY_P4_EXPANDER_ADDR);
    if (!xl9555ReadAll(TDISPLAY_P4_EXPANDER_ADDR,
                       s_out0, s_out1, s_cfg0, s_cfg1)) {
        Serial.println("[tdisplay-p4-io] expander read failed");
        unlock();
        return false;
    }
    if (!xl9555WriteReg(TDISPLAY_P4_EXPANDER_ADDR, XL9555_REG_POL0, 0)
        || !xl9555WriteReg(TDISPLAY_P4_EXPANDER_ADDR, XL9555_REG_POL1, 0)) {
        Serial.println("[tdisplay-p4-io] expander polarity reset failed");
        unlock();
        return false;
    }

    xl9555SetInput(TDISPLAY_P4_EXP_TOUCH_INT, s_cfg0, s_cfg1);
    xl9555SetInput(TDISPLAY_P4_EXP_IMU_INT, s_cfg0, s_cfg1);
    xl9555SetInput(TDISPLAY_P4_EXP_RTC_INT, s_cfg0, s_cfg1);
    xl9555SetInput(TDISPLAY_P4_EXP_ESP32C6_WAKE, s_cfg0, s_cfg1);
    xl9555SetInput(TDISPLAY_P4_EXP_RADIO_DIO1, s_cfg0, s_cfg1);

    stageOutput(TDISPLAY_P4_EXP_POWER_3V3, false);
    stageOutput(TDISPLAY_P4_EXP_ANTENNA_SELECT, true);
    stageOutput(TDISPLAY_P4_EXP_SCREEN_RST, false);
    stageOutput(TDISPLAY_P4_EXP_TOUCH_RST, false);
    stageOutput(TDISPLAY_P4_EXP_ETHERNET_RST, false);
    stageOutput(TDISPLAY_P4_EXP_AUDIO_POWER, false);
    stageOutput(TDISPLAY_P4_EXP_USB_PHY_POWER, true);
    stageOutput(TDISPLAY_P4_EXP_GPS_WAKE, false);
    stageOutput(TDISPLAY_P4_EXP_ESP32C6_EN, true);
    stageOutput(TDISPLAY_P4_EXP_SD_POWER, true);
    stageOutput(TDISPLAY_P4_EXP_RADIO_RST, false);

    if (!writeState()) {
        Serial.println("[tdisplay-p4-io] safe-state write failed");
        unlock();
        return false;
    }

    s_ready = true;
    unlock();
    delay(20);
    (void)tdisplayP4IoClearInterrupt();
    Serial.println("[tdisplay-p4-io] ready");
    return true;
}

bool tdisplayP4IoReady() {
    return s_ready;
}

bool tdisplayP4IoSetScreenResetReleased(bool released) {
    return setOutput(TDISPLAY_P4_EXP_SCREEN_RST, released);
}

bool tdisplayP4IoSetTouchResetReleased(bool released) {
    return setOutput(TDISPLAY_P4_EXP_TOUCH_RST, released);
}

bool tdisplayP4IoSetAudioPower(bool enabled) {
    return setOutput(TDISPLAY_P4_EXP_AUDIO_POWER, enabled);
}

bool tdisplayP4IoSetSdPower(bool enabled) {
    return setOutput(TDISPLAY_P4_EXP_SD_POWER, !enabled);
}

bool tdisplayP4IoSetEsp32C6Power(bool enabled) {
    return setOutput(TDISPLAY_P4_EXP_ESP32C6_EN, enabled);
}

bool tdisplayP4IoResetEsp32C6() {
    Serial.println("[wifi-p4] resetting ESP32-C6 through XL9535");
    if (!tdisplayP4IoSetEsp32C6Power(true)) return false;
    delay(100);
    if (!tdisplayP4IoSetEsp32C6Power(false)) return false;
    delay(100);
    if (!tdisplayP4IoSetEsp32C6Power(true)) return false;
    delay(1100);
    Serial.println("[wifi-p4] ESP32-C6 reset complete");
    return true;
}

// esp_restart() on the ESP32-P4 is SW_CPU_RESET: esp_system_reset_modules_on_exit()
// (esp_system/port/soc/esp32p4/system_internal.c) resets the DMA engines, UARTs,
// SDMMC and crypto, and not the MIPI-DSI host or its DPI path. The display
// driver then initialises over a host still half-configured from the session
// that restarted, and the panel stays dark. The reset button is a full chip
// reset and always recovers it -- which is exactly what the user saw: a reboot
// from software came up blank, the next one by hand was fine.
//
// So a software restart becomes that full reset: the RTC watchdog's system
// reset, which resets "the CPU and all peripherals" (wdt_types.h) and leaves
// only the RTC domain. Done as a shutdown handler so it covers every restart
// path without touching one of them, OTA's included.
//
// esp_restart() runs shutdown handlers newest first, and this one does not
// return -- so it is registered before anything else (Wi-Fi's own shutdown
// handler among them) to run last, after the others have done their work.
// RTC memory survives the system reset (only the RTC domain is spared), so this
// is how the next boot knows its watchdog reset was a deliberate restart.
static constexpr uint32_t kFullRestartMagic = 0x50345253u;   // "P4RS"
RTC_NOINIT_ATTR static uint32_t s_fullRestartMarker;

bool tdisplayP4ConsumeFullRestartMarker() {
    const bool ours = (s_fullRestartMarker == kFullRestartMagic);
    s_fullRestartMarker = 0;
    return ours;
}

static void tdisplayP4FullRestart() {
    s_fullRestartMarker = kFullRestartMagic;
    wdt_hal_context_t rwdt = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rwdt);
    // Ticks of the slow clock: ~130 ms at 150 kHz, longer on a 32 kHz crystal.
    // Nothing is waiting on it; it only has to be short.
    wdt_hal_config_stage(&rwdt, WDT_STAGE0, 20000, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_enable(&rwdt);
    wdt_hal_write_protect_enable(&rwdt);
    while (true) {
    }
}

void tdisplayP4InstallFullRestart() {
    static bool s_installed = false;
    if (s_installed) return;
    s_installed = (esp_register_shutdown_handler(tdisplayP4FullRestart) == ESP_OK);
}

TwoWire &tdisplayP4I2c1(TdisplayP4I2c1Route route) {
    // Neither: nothing has begun Wire1 yet.
    static int s_current = -1;
    if (s_current != (int)route) {
        if (s_current >= 0) Wire1.end();
        if (route == TDISPLAY_P4_I2C1_AUDIO) {
            // LilyGO t_display_p4_config.h: es8311 on i2c::kPort2 (20/21).
            Wire1.begin(BOARD_AUDIO_I2C_SDA, BOARD_AUDIO_I2C_SCL, 400000UL);
        } else {
            // The rate keyboard.cpp begins it at; its own begin() is then a no-op.
            Wire1.begin(KB_SDA, KB_SCL, 100000UL);
        }
        s_current = (int)route;
    }
    return Wire1;
}

bool tdisplayP4IoSetGpsAwake(bool awake) {
    return setOutput(TDISPLAY_P4_EXP_GPS_WAKE, awake);
}

bool tdisplayP4IoReadGpsWake(bool &latchedHigh, bool &sampledHigh,
                             bool &configuredOutput) {
    latchedHigh = false;
    sampledHigh = false;
    configuredOutput = false;
    if (!s_ready || !lock()) return false;

    uint8_t output = 0;
    uint8_t input = 0;
    uint8_t config = 0;
    const uint8_t mask = (uint8_t)(1U << (TDISPLAY_P4_EXP_GPS_WAKE & 7U));
    const bool ok = xl9555ReadReg(TDISPLAY_P4_EXPANDER_ADDR,
                                  XL9555_REG_OUT1, output)
                 && xl9555ReadReg(TDISPLAY_P4_EXPANDER_ADDR,
                                  XL9555_REG_IN1, input)
                 && xl9555ReadReg(TDISPLAY_P4_EXPANDER_ADDR,
                                  XL9555_REG_CFG1, config);
    if (ok) {
        latchedHigh = (output & mask) != 0;
        sampledHigh = (input & mask) != 0;
        configuredOutput = (config & mask) == 0;
    }
    unlock();
    return ok;
}

bool tdisplayP4IoSetRadioResetReleased(bool released) {
    return setOutput(TDISPLAY_P4_EXP_RADIO_RST, released);
}

bool tdisplayP4IoSelectInternalAntenna(bool internal) {
    return setOutput(TDISPLAY_P4_EXP_ANTENNA_SELECT, internal);
}

bool tdisplayP4IoReadRadioDio1(bool &high) {
    return readInput(TDISPLAY_P4_EXP_RADIO_DIO1, high);
}

bool tdisplayP4IoClearInterrupt() {
    if (!s_ready || !lock()) return false;
    uint8_t input0 = 0;
    uint8_t input1 = 0;
    const bool ok = xl9555ReadReg(TDISPLAY_P4_EXPANDER_ADDR,
                                  XL9555_REG_IN0, input0)
                 && xl9555ReadReg(TDISPLAY_P4_EXPANDER_ADDR,
                                  XL9555_REG_IN1, input1);
    unlock();
    return ok;
}

bool tdisplayP4IoPrepareForSleep() {
    if (!s_ready || !lock()) return false;
    stageOutput(TDISPLAY_P4_EXP_SCREEN_RST, false);
    stageOutput(TDISPLAY_P4_EXP_TOUCH_RST, false);
    stageOutput(TDISPLAY_P4_EXP_AUDIO_POWER, false);
    stageOutput(TDISPLAY_P4_EXP_GPS_WAKE, false);
    stageOutput(TDISPLAY_P4_EXP_ESP32C6_EN, false);
    stageOutput(TDISPLAY_P4_EXP_SD_POWER, true);
    stageOutput(TDISPLAY_P4_EXP_RADIO_RST, false);
    const bool ok = writeState();
    unlock();
    return ok;
}