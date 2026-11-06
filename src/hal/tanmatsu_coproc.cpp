#include "config.h"

#if defined(DEVICE_TANMATSU)

#include "tanmatsu_coproc.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

SemaphoreHandle_t s_lock = nullptr;
bool s_ready = false;

class BusLock {
public:
    BusLock() { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~BusLock() { if (s_lock) xSemaphoreGiveRecursive(s_lock); }
};

}  // namespace

bool tanmatsuCoprocBegin() {
    if (s_ready) return true;
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    BusLock lock;
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);
    Wire.beginTransmission((uint8_t)TANMATSU_COPROC_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[tanmatsu] coprocessor not found at 0x5F");
        return false;
    }
    s_ready = true;
    uint8_t ver[2] = {};
    if (tanmatsuCoprocRead(TANMATSU_REG_FW_VERSION, ver, sizeof(ver))) {
        Serial.printf("[tanmatsu] coprocessor fw %u\n",
                      (unsigned)(ver[0] | (ver[1] << 8)));
    }
    // Continuous PMIC ADC conversion, so VBAT is always current.
    const uint8_t adc = 1u << 1;
    tanmatsuCoprocWrite(TANMATSU_REG_PMIC_ADC_CONTROL, &adc, 1);
    return true;
}

bool tanmatsuCoprocReady() { return s_ready; }

bool tanmatsuCoprocRead(uint8_t reg, uint8_t *buf, size_t len) {
    if (!s_ready || !buf || len == 0) return false;
    BusLock lock;
    Wire.beginTransmission((uint8_t)TANMATSU_COPROC_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)TANMATSU_COPROC_ADDR, len, true) != len) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) buf[i] = (uint8_t)Wire.read();
    return true;
}

bool tanmatsuCoprocWrite(uint8_t reg, const uint8_t *buf, size_t len) {
    if (!s_ready) return false;
    BusLock lock;
    Wire.beginTransmission((uint8_t)TANMATSU_COPROC_ADDR);
    Wire.write(reg);
    if (buf && len) Wire.write(buf, len);
    return Wire.endTransmission() == 0;
}

bool tanmatsuCoprocReadKeys(uint8_t out[TANMATSU_KEYBOARD_BYTES]) {
    return tanmatsuCoprocRead(TANMATSU_REG_KEYBOARD, out, TANMATSU_KEYBOARD_BYTES);
}

bool tanmatsuCoprocSetDisplayBacklight(uint8_t level) {
    // The coprocessor applies register 11 the moment it is written, so a level
    // that does not stick means the write never landed. Retry briefly, then read
    // it back and log what actually happened: on hardware that is the only way
    // to tell a NAKed write from a coprocessor that ignored a good one.
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        ok = tanmatsuCoprocWrite(TANMATSU_REG_DISPLAY_BACKLIGHT, &level, 1);
        if (!ok) delay(2);
    }
    uint8_t readBack = 0;
    const bool readOk = tanmatsuCoprocRead(TANMATSU_REG_DISPLAY_BACKLIGHT, &readBack, 1);
    static int s_lastLogged = -1;
    if (!ok || !readOk || readBack != level || s_lastLogged != level) {
        Serial.printf("[tanmatsu] backlight %u: write %s, readback %s %u\n",
                      (unsigned)level, ok ? "ok" : "FAILED",
                      readOk ? "ok" : "FAILED", (unsigned)readBack);
        s_lastLogged = level;
    }
    return ok;
}

bool tanmatsuCoprocSetKeyboardBacklight(uint8_t level) {
    return tanmatsuCoprocWrite(TANMATSU_REG_KEYBOARD_BACKLIGHT, &level, 1);
}

bool tanmatsuCoprocRadioPowerCycle() {
    const uint8_t off = TANMATSU_RADIO_DISABLED;
    const uint8_t app = TANMATSU_RADIO_APPLICATION;
    if (!tanmatsuCoprocWrite(TANMATSU_REG_RADIO_CONTROL, &off, 1)) return false;
    delay(100);
    if (!tanmatsuCoprocWrite(TANMATSU_REG_RADIO_CONTROL, &app, 1)) return false;
    // The C6 needs time to boot its ESP-Hosted slave before SDIO enumeration.
    delay(100);
    return true;
}

namespace {
bool readVbatOnce(uint16_t &mv) {
    uint8_t v[2] = {};
    if (!tanmatsuCoprocRead(TANMATSU_REG_PMIC_ADC_VBAT, v, sizeof(v))) return false;
    mv = (uint16_t)(v[0] | (v[1] << 8));
    // The PMIC's VBAT ADC spans 2.304-4.848 V; anything else is a bad read.
    return mv >= 2304 && mv <= 4848;
}
}  // namespace

uint16_t tanmatsuCoprocBatteryMv() {
    // The coprocessor rewrites the two VBAT bytes from its own loop, so one read
    // can pair the low byte of one sample with the high byte of the next -- off
    // by up to 255 mV. Two reads that agree are taken; otherwise "unknown",
    // which the filter holds through.
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint16_t a = 0, b = 0;
        if (!readVbatOnce(a) || !readVbatOnce(b)) continue;
        const int diff = (int)a - (int)b;
        if (diff >= -40 && diff <= 40) return (uint16_t)((a + b) / 2);
    }
    return 0;
}

bool tanmatsuCoprocChargeStatus(bool *batteryAttached, bool *usbPresent,
                                bool *charging) {
    uint8_t v = 0;
    if (!tanmatsuCoprocRead(TANMATSU_REG_PMIC_CHARGING_STATUS, &v, 1)) return false;
    const uint8_t stage = (v >> 3) & 0x3;  // 0 none, 1 pre, 2 fast, 3 done
    if (batteryAttached) *batteryAttached = v & 0x01;
    if (usbPresent) *usbPresent = (v >> 1) & 0x01;
    if (charging) *charging = stage == 1 || stage == 2;
    return true;
}

bool tanmatsuCoprocPowerButtonPressed() {
    uint8_t v = 0;
    if (!tanmatsuCoprocRead(TANMATSU_REG_INPUT, &v, 1)) return false;
    return (v >> TANMATSU_INPUT_POWER_BUTTON_BIT) & 0x01;
}

void tanmatsuCoprocPowerOff() {
    const uint8_t off = 0x01;
    tanmatsuCoprocWrite(TANMATSU_REG_PMIC_POWER_CONTROL, &off, 1);
}

namespace {
uint8_t s_leds[TANMATSU_LED_COUNT * 3] = {};   // G R B per LED, as the coprocessor wants
bool s_ledsManual = false;
}  // namespace

bool tanmatsuCoprocSetLed(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= TANMATSU_LED_COUNT || !s_ready) return false;
    BusLock lock;
    if (!s_ledsManual) {
        // Global brightness to full: per-LED levels below do all the scaling.
        const uint8_t full = 255;
        const uint8_t manual = 0;
        if (!tanmatsuCoprocWrite(TANMATSU_REG_LED_BRIGHTNESS, &full, 1)) return false;
        memset(s_leds, 0, sizeof(s_leds));
        if (!tanmatsuCoprocWrite(TANMATSU_REG_LED_DATA, s_leds, sizeof(s_leds))) return false;
        if (!tanmatsuCoprocWrite(TANMATSU_REG_LED_MODE, &manual, 1)) return false;
        s_ledsManual = true;
    }
    uint8_t *p = &s_leds[index * 3];
    if (p[0] == g && p[1] == r && p[2] == b) return true;
    const uint8_t grb[3] = {g, r, b};
    if (!tanmatsuCoprocWrite((uint8_t)(TANMATSU_REG_LED_DATA + index * 3), grb, 3)) return false;
    memcpy(p, grb, 3);
    return true;
}

bool tanmatsuCoprocSetAmplifier(bool on) {
    const uint8_t v = on ? (uint8_t)(1u << TANMATSU_OUTPUT_AMPLIFIER_BIT) : 0;
    return tanmatsuCoprocWrite(TANMATSU_REG_OUTPUT, &v, 1);
}

namespace {
bool codecWrite(uint8_t reg, uint8_t v) {
    Wire.beginTransmission((uint8_t)AUDIO_CODEC_ADDR);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}
}  // namespace

bool tanmatsuCodecBegin() {
    if (!s_ready) return false;
    // es8156_configure() from the badge-bsp, register by register (page 0).
    static const uint8_t kInit[][2] = {
        {0x02, 0x04}, {0x20, 0x2A}, {0x21, 0x3C}, {0x22, 0x00}, {0x24, 0x07}, {0x23, 0x04},
        {0x0A, 0x01}, {0x0B, 0x01}, {0x11, 0x00}, {0x0D, 0x14}, {0x18, 0x00}, {0x08, 0x3F},
        {0x00, 0x02}, {0x00, 0x03}, {0x25, 0x20}, {0x25, 0x20},
    };
    BusLock lock;
    bool ok = codecWrite(0xFC, 0x00);
    for (const auto &r : kInit) ok = ok && codecWrite(r[0], r[1]);
    // The BSP's 100% level. Loudness is scaled in the samples, as on the other
    // boards, so this stays fixed.
    ok = ok && codecWrite(0x14, 180);
    if (!ok) Serial.println("[tanmatsu] ES8156 init failed");
    return ok;
}

bool tanmatsuCodecSetVolume(uint8_t vol) {
    if (!s_ready) return false;
    BusLock lock;
    return codecWrite(0x14, vol);
}

#endif  // DEVICE_TANMATSU
