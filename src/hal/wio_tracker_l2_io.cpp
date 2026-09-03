#include "wio_tracker_l2_io.h"

#include <Arduino.h>
#include <Wire.h>

#include "hw_wio_tracker_l2.h"
#include "xl9555.h"

namespace {

uint8_t s_out0 = 0xFF;
uint8_t s_out1 = 0xFF;
uint8_t s_cfg0 = 0xFF;
uint8_t s_cfg1 = 0xFF;
bool s_ready = false;

bool writeState() {
    return xl9555WriteAll(EXPANDER_ADDR, s_out0, s_out1, s_cfg0, s_cfg1);
}

void stageOutput(uint8_t bit, bool high) {
    xl9555SetOutput(bit, high, s_out0, s_out1, s_cfg0, s_cfg1);
}

bool setOutput(uint8_t bit, bool high) {
    if (!s_ready) return false;
    const uint8_t previousOut0 = s_out0;
    const uint8_t previousOut1 = s_out1;
    const uint8_t previousCfg0 = s_cfg0;
    const uint8_t previousCfg1 = s_cfg1;
    stageOutput(bit, high);
    if (writeState()) return true;

    s_out0 = previousOut0;
    s_out1 = previousOut1;
    s_cfg0 = previousCfg0;
    s_cfg1 = previousCfg1;
    return false;
}

} // namespace

bool wioTrackerL2IoBegin() {
    s_ready = false;
    pinMode(EXPANDER_INT, INPUT_PULLUP);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);

    Serial.printf("[wio-l2-io] i2c sda=%d scl=%d freq=%u expander=0x%02X\n",
                  BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ, EXPANDER_ADDR);
    if (!xl9555ReadAll(EXPANDER_ADDR, s_out0, s_out1, s_cfg0, s_cfg1)) {
        Serial.println("[wio-l2-io] expander read failed");
        return false;
    }

    xl9555SetInput(EXP_BIT_WAKE_BUTTON, s_cfg0, s_cfg1);
    xl9555SetInput(EXP_BIT_I2C_INT, s_cfg0, s_cfg1);
    xl9555SetInput(EXP_BIT_SD_DETECT, s_cfg0, s_cfg1);
    xl9555SetInput(EXP_BIT_LCD_CS, s_cfg0, s_cfg1);

    stageOutput(EXP_BIT_TOUCH_INT, false);
    stageOutput(EXP_BIT_LCD_POWER, true);
    stageOutput(EXP_BIT_LCD_RST, true);
    stageOutput(EXP_BIT_GROVE_POWER, false);
    stageOutput(EXP_BIT_TOUCH_RST, false);
    stageOutput(EXP_BIT_GNSS_RST, true);
    stageOutput(EXP_BIT_USER_LED, false);
    stageOutput(EXP_BIT_USB_OTG_EN, false);
    stageOutput(EXP_BIT_AUDIO_PA_POWER, false);
    stageOutput(EXP_BIT_GNSS_POWER, true);
    stageOutput(EXP_BIT_SD_POWER, false);
    stageOutput(EXP_BIT_BATT_SENSE_EN, false);

    if (!writeState()) {
        Serial.println("[wio-l2-io] safe-state write failed");
        return false;
    }

    delay(10);
    stageOutput(EXP_BIT_GNSS_RST, false);
    if (!writeState()) {
        Serial.println("[wio-l2-io] GNSS reset release failed");
        return false;
    }

    delay(40);
    stageOutput(EXP_BIT_LCD_RST, false);
    if (!writeState()) {
        Serial.println("[wio-l2-io] LCD reset assertion failed");
        return false;
    }

    delay(10);
    stageOutput(EXP_BIT_LCD_RST, true);
    if (!writeState()) {
        Serial.println("[wio-l2-io] LCD reset release failed");
        return false;
    }

    delay(500);
    stageOutput(EXP_BIT_LCD_CS, true);
    if (!writeState()) {
        Serial.println("[wio-l2-io] LCD control release failed");
        return false;
    }

    delay(10);
    stageOutput(EXP_BIT_TOUCH_RST, true);
    if (!writeState()) {
        Serial.println("[wio-l2-io] touch reset release failed");
        return false;
    }

    delay(60);
    s_ready = true;
    uint8_t input0 = 0xFF;
    (void)xl9555ReadReg(EXPANDER_ADDR, XL9555_REG_IN0, input0);
    Serial.printf("[wio-l2-io] ready out0=0x%02X out1=0x%02X cfg0=0x%02X cfg1=0x%02X\n",
                  s_out0, s_out1, s_cfg0, s_cfg1);
    return true;
}

bool wioTrackerL2IoReady() {
    return s_ready;
}

bool wioTrackerL2IoReadWakeButton(bool &pressed) {
    pressed = false;
    if (!s_ready) return false;

    uint8_t input0 = 0xFF;
    if (!xl9555ReadReg(EXPANDER_ADDR, XL9555_REG_IN0, input0)) return false;
    pressed = (input0 & (uint8_t)(1U << EXP_BIT_WAKE_BUTTON)) == 0;
    return true;
}

bool wioTrackerL2IoSetLcdPower(bool enabled) {
    return setOutput(EXP_BIT_LCD_POWER, enabled);
}

bool wioTrackerL2IoSetLcdResetReleased(bool released) {
    return setOutput(EXP_BIT_LCD_RST, released);
}

bool wioTrackerL2IoSetTouchResetReleased(bool released) {
    return setOutput(EXP_BIT_TOUCH_RST, released);
}

bool wioTrackerL2IoSetGrovePower(bool enabled) {
    return setOutput(EXP_BIT_GROVE_POWER, enabled);
}

bool wioTrackerL2IoSetGnssPower(bool enabled) {
    return setOutput(EXP_BIT_GNSS_POWER, enabled);
}

bool wioTrackerL2IoSetGnssResetReleased(bool released) {
    return setOutput(EXP_BIT_GNSS_RST, !released);
}

bool wioTrackerL2IoSetUserLed(bool enabled) {
    return setOutput(EXP_BIT_USER_LED, enabled);
}

bool wioTrackerL2IoSetUsbOtg(bool enabled) {
    return setOutput(EXP_BIT_USB_OTG_EN, enabled);
}

bool wioTrackerL2IoSetAudioPaPower(bool enabled) {
    return setOutput(EXP_BIT_AUDIO_PA_POWER, enabled);
}

bool wioTrackerL2IoSetSdPower(bool enabled) {
    return setOutput(EXP_BIT_SD_POWER, enabled);
}

bool wioTrackerL2IoSetBatterySense(bool enabled) {
    return setOutput(EXP_BIT_BATT_SENSE_EN, enabled);
}