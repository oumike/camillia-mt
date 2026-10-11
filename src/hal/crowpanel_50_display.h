#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/crowpanel_50_display.h — CrowPanel Advance 5.0" display and touch
//
// An 800×480 RGB565 panel on the S3's LCD_CAM peripheral, scanned out of a
// PSRAM framebuffer, with GT911 touch. Bus pins and timings are Elecrow's
// (5.0/factory_code/LovyanGFX_Driver.h), which Meshtastic's LGFX_ELECROW70 uses
// unchanged. The backlight and the touch reset belong to a PCA9557 (V1.0) or a
// board MCU (V1.1+), so this board gets its own LGFX_TDeck rather than another
// arm of display.h's SPI class -- the same shape as the T-Deck Pro and P4.
// ════════════════════════════════════════════════════════════════════════════

#include <LovyanGFX.hpp>
#include <Wire.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

#include "config.h"

// Whatever on this board owns the backlight and the touch reset: the PCA9557
// on V1.0, the board MCU from V1.1 (hw_crowpanel_50.h). begin() works out which
// by who answers, so one build runs either for display and touch.
class CrowPanelBoardCtl {
public:
    enum Kind : uint8_t { NONE, PCA9557, MCU };

    Kind begin() {
        Wire.begin(TOUCH_SDA, TOUCH_SCL);
        if (probe(CROWPANEL_IOEX_ADDR)) {
            _kind = PCA9557;
            beginPca();
            Serial.printf("[crowpanel] V1.0 board: PCA9557 at 0x%02X\n", CROWPANEL_IOEX_ADDR);
#if !CROWPANEL_50_HW_V10
            Serial.println("[crowpanel] this build expects V1.2+; on V1.0 the radio needs -DCROWPANEL_50_HW_V10=1");
#endif
        } else if (probe(CROWPANEL_MCU_ADDR)) {
            _kind = MCU;
            Serial.printf("[crowpanel] V1.1+ board: MCU at 0x%02X\n", CROWPANEL_MCU_ADDR);
#if CROWPANEL_50_HW_V10
            Serial.println("[crowpanel] this build is for V1.0; on V1.2+ the radio needs the default build");
#endif
        } else {
            Serial.printf("[crowpanel] no backlight controller at 0x%02X or 0x%02X; I2C has:",
                          CROWPANEL_IOEX_ADDR, CROWPANEL_MCU_ADDR);
            for (uint8_t a = 0x08; a < 0x78; a++) {
                if (probe(a)) Serial.printf(" 0x%02X", a);
            }
            Serial.println();
        }
        return _kind;
    }

    // GT911 address select: INT held low through the controller's reset gives
    // 0x5D, then INT is released to it (Elecrow's sequence on both revisions).
    // Elecrow's V1.2 code repeats the MCU's wake command until the GT911
    // answers rather than trusting one, so this does too, a bounded number of
    // times. The GT911 can also come up at 0x14 if the strap is missed, so
    // this returns the address it answered at (0 if neither).
    uint8_t resetTouch() {
        if (_kind == NONE) return 0;
        for (int attempt = 1; attempt <= 5; attempt++) {
            pinMode(TOUCH_INT, OUTPUT);
            digitalWrite(TOUCH_INT, LOW);
            if (_kind == PCA9557) {
                pcaSet(CROWPANEL_IOEX_TP_RST, false);
                delay(20);
                pcaSet(CROWPANEL_IOEX_TP_RST, true);
                delay(100);
            } else {
                mcuSendAlways(CROWPANEL_MCU_TOUCH_ON);
                delay(120);
            }
            pinMode(TOUCH_INT, INPUT);
            delay(100);
            const bool at5d = probe(0x5D), at14 = probe(0x14);
            if (at5d || at14) {
                Serial.printf("[crowpanel] GT911 at 0x%02X after %d attempt%s\n",
                              at5d ? 0x5D : 0x14, attempt, attempt == 1 ? "" : "s");
                return at5d ? 0x5D : 0x14;
            }
        }
        Serial.println("[crowpanel] GT911 did not answer at 0x5D or 0x14: no touch");
        return 0;
    }

    // 0 is off. V1.0's backlight only switches; the MCU dims, 255 brightest.
    void setBacklight(uint8_t level) {
        if (_kind == PCA9557) {
            pcaSet(CROWPANEL_IOEX_BL, level > 0);
        } else if (_kind == MCU) {
            if (level == 0) {
                mcuSend(CROWPANEL_MCU_BL_OFF);
                return;
            }
            const uint8_t span = CROWPANEL_MCU_BL_MIN - CROWPANEL_MCU_BL_MAX;
            const uint8_t cmd = (uint8_t)(CROWPANEL_MCU_BL_MIN - (uint32_t)(level - 1) * span / 254);
            if (cmd != _lastMcu) {
                Serial.printf("[crowpanel] backlight %u -> MCU %u\n", (unsigned)level, (unsigned)cmd);
            }
            mcuSend(cmd);
        }
    }

private:
    // PCA9557 registers, the TCA9534's: input, output, polarity, configuration
    // (1 = input). Outputs are shadowed so one pin can change without a read.
    static constexpr uint8_t kRegOutput = 0x01;
    static constexpr uint8_t kRegConfig = 0x03;

    static uint8_t mask(uint8_t pin) { return (uint8_t)(1u << pin); }

    static bool probe(uint8_t addr) {
        Wire.beginTransmission(addr);
        return Wire.endTransmission() == 0;
    }

    void beginPca() {
        const uint8_t outputs = mask(CROWPANEL_IOEX_BL) | mask(CROWPANEL_IOEX_TP_RST)
                              | mask(CROWPANEL_IOEX_AMP_MUTE) | mask(CROWPANEL_IOEX_AMP_SHUT);
        // Levels first, then direction, so no pin glitches on the way to output.
        // Elecrow's start-up state: backlight on, amplifier held off.
        _out = mask(CROWPANEL_IOEX_BL) | mask(CROWPANEL_IOEX_TP_RST) | mask(CROWPANEL_IOEX_AMP_SHUT);
        pcaWrite(kRegOutput, _out);
        pcaWrite(kRegConfig, (uint8_t)~outputs);
    }

    void pcaSet(uint8_t pin, bool high) {
        const uint8_t next = high ? (uint8_t)(_out | mask(pin)) : (uint8_t)(_out & ~mask(pin));
        if (next == _out) return;
        _out = next;
        pcaWrite(kRegOutput, _out);
    }

    void pcaWrite(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(CROWPANEL_IOEX_ADDR);
        Wire.write(reg);
        Wire.write(value);
        Wire.endTransmission();
    }

    void mcuSend(uint8_t cmd) {
        if (cmd == _lastMcu) return;
        if (mcuSendAlways(cmd)) _lastMcu = cmd;
    }

    bool mcuSendAlways(uint8_t cmd) {
        Wire.beginTransmission(CROWPANEL_MCU_ADDR);
        Wire.write(cmd);
        return Wire.endTransmission() == 0;
    }

    Kind _kind = NONE;
    uint8_t _out = 0;
    int16_t _lastMcu = -1;
};

class Light_CrowPanel50 : public lgfx::v1::ILight {
public:
    void setController(CrowPanelBoardCtl *ctl) { _ctl = ctl; }

    bool init(uint8_t brightness) override {
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override {
        if (_ctl) _ctl->setBacklight(brightness);
    }

private:
    CrowPanelBoardCtl *_ctl = nullptr;
};

class LGFX_TDeck : public lgfx::LGFX_Device {
    lgfx::Bus_RGB       _bus;
    lgfx::Panel_RGB     _panel;
    Light_CrowPanel50   _light;
    lgfx::Touch_GT911   _touch;
    CrowPanelBoardCtl   _ctl;

public:
    bool init_impl(bool use_reset, bool use_clear) override {
        _ctl.begin();
        const uint8_t touchAddr = _ctl.resetTouch();
        if (touchAddr) {
            auto cfg = _touch.config();
            cfg.i2c_addr = touchAddr;
            _touch.config(cfg);
        }
        const bool ok = LGFX_Device::init_impl(use_reset, use_clear);
        // LovyanGFX drops the touch device without a word when its own init of
        // it fails, which looks exactly like a dead panel from outside.
        Serial.printf("[crowpanel] touch driver %s\n", touch() ? "up" : "DISABLED (init failed)");
        return ok;
    }

    LGFX_TDeck() {
        {
            auto cfg = _panel.config();
            cfg.memory_width  = TFT_PANEL_WIDTH;
            cfg.memory_height = TFT_PANEL_HEIGHT;
            cfg.panel_width   = TFT_PANEL_WIDTH;
            cfg.panel_height  = TFT_PANEL_HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            // The framebuffer is the only copy of the screen, so readback --
            // screenshots, the VNC mirror's resync -- costs nothing here.
            cfg.readable = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _panel.config_detail();
            cfg.use_psram = 1;   // 800×480×2 = 750 KB; there is no room for it inside
            _panel.config_detail(cfg);
        }
        {
            auto cfg = _bus.config();
            cfg.panel = &_panel;
            cfg.pin_d0  = GPIO_NUM_21;   // B0
            cfg.pin_d1  = GPIO_NUM_47;   // B1
            cfg.pin_d2  = GPIO_NUM_48;   // B2
            cfg.pin_d3  = GPIO_NUM_45;   // B3
            cfg.pin_d4  = GPIO_NUM_38;   // B4
            cfg.pin_d5  = GPIO_NUM_9;    // G0
            cfg.pin_d6  = GPIO_NUM_10;   // G1
            cfg.pin_d7  = GPIO_NUM_11;   // G2
            cfg.pin_d8  = GPIO_NUM_12;   // G3
            cfg.pin_d9  = GPIO_NUM_13;   // G4
            cfg.pin_d10 = GPIO_NUM_14;   // G5
            cfg.pin_d11 = GPIO_NUM_7;    // R0
            cfg.pin_d12 = GPIO_NUM_17;   // R1
            cfg.pin_d13 = GPIO_NUM_18;   // R2
            cfg.pin_d14 = GPIO_NUM_3;    // R3
            cfg.pin_d15 = GPIO_NUM_46;   // R4
            cfg.pin_henable = GPIO_NUM_42;
            cfg.pin_vsync   = GPIO_NUM_41;
            cfg.pin_hsync   = GPIO_NUM_40;
            cfg.pin_pclk    = GPIO_NUM_39;
            cfg.freq_write  = CROWPANEL_PCLK_HZ;

            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 8;
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 8;
            cfg.pclk_idle_high    = 1;
            _bus.config(cfg);
        }
        _panel.setBus(&_bus);

        _light.setController(&_ctl);
        _panel.setLight(&_light);

        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_WIDTH - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_HEIGHT - 1;
            cfg.pin_int = -1;            // polled; INT is only the address strap
            cfg.pin_rst = -1;            // the board controller's, see resetTouch()
            cfg.bus_shared = true;       // the backlight controller is on the same pins
            cfg.offset_rotation = 0;
            cfg.i2c_port = TOUCH_I2C_PORT;
            cfg.i2c_addr = TOUCH_ADDR;
            cfg.pin_sda  = TOUCH_SDA;
            cfg.pin_scl  = TOUCH_SCL;
            cfg.freq     = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }

        setPanel(&_panel);
    }
};
