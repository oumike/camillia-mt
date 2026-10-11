#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/crowpanel_50_display.h — CrowPanel Advance 5.0" display and touch
//
// An 800×480 RGB565 panel on the S3's LCD_CAM peripheral, scanned out of a
// PSRAM framebuffer, with GT911 touch. Bus pins and timings are Elecrow's
// (5.0/factory_code/LovyanGFX_Driver.h), which Meshtastic's LGFX_ELECROW70 uses
// unchanged. The backlight and the touch reset are on the PCA9557 expander, so
// this board gets its own LGFX_TDeck rather than another arm of display.h's
// SPI class -- the same shape as the T-Deck Pro and T-Display P4 headers.
// ════════════════════════════════════════════════════════════════════════════

#include <LovyanGFX.hpp>
#include <Wire.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

#include "config.h"

// The PCA9557's four registers are the TCA9534's: input, output, polarity,
// configuration (1 = input). Outputs are kept in a shadow so one pin can change
// without reading the port back.
class CrowPanelIoExpander {
public:
    bool begin() {
        Wire.begin(TOUCH_SDA, TOUCH_SCL);
        Wire.beginTransmission(CROWPANEL_IOEX_ADDR);
        if (Wire.endTransmission() != 0) {
            Serial.printf("[crowpanel] PCA9557 not found at 0x%02X: no backlight or touch reset\n",
                          CROWPANEL_IOEX_ADDR);
            return false;
        }
        _ok = true;
        const uint8_t outputs = mask(CROWPANEL_IOEX_BL) | mask(CROWPANEL_IOEX_TP_RST)
                              | mask(CROWPANEL_IOEX_AMP_MUTE) | mask(CROWPANEL_IOEX_AMP_SHUT);
        // Levels first, then direction, so no pin glitches on the way to output.
        // Elecrow's start-up state: backlight on, amplifier held off.
        _out = mask(CROWPANEL_IOEX_BL) | mask(CROWPANEL_IOEX_TP_RST) | mask(CROWPANEL_IOEX_AMP_SHUT);
        writeReg(kRegOutput, _out);
        writeReg(kRegConfig, (uint8_t)~outputs);
        return true;
    }

    void set(uint8_t pin, bool high) {
        if (!_ok) return;
        const uint8_t next = high ? (uint8_t)(_out | mask(pin)) : (uint8_t)(_out & ~mask(pin));
        if (next == _out) return;
        _out = next;
        writeReg(kRegOutput, _out);
    }

    bool ok() const { return _ok; }

private:
    static constexpr uint8_t kRegOutput = 0x01;
    static constexpr uint8_t kRegConfig = 0x03;

    static uint8_t mask(uint8_t pin) { return (uint8_t)(1u << pin); }

    void writeReg(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(CROWPANEL_IOEX_ADDR);
        Wire.write(reg);
        Wire.write(value);
        Wire.endTransmission();
    }

    uint8_t _out = 0;
    bool _ok = false;
};

// The backlight driver's enable line is all there is: on or off, no dimming.
// Brightness 0 turns it off (screen sleep), anything else on.
class Light_CrowPanel50 : public lgfx::v1::ILight {
public:
    void setExpander(CrowPanelIoExpander *ioex) { _ioex = ioex; }

    bool init(uint8_t brightness) override {
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override {
        if (_ioex) _ioex->set(CROWPANEL_IOEX_BL, brightness > 0);
    }

private:
    CrowPanelIoExpander *_ioex = nullptr;
};

class LGFX_TDeck : public lgfx::LGFX_Device {
    lgfx::Bus_RGB       _bus;
    lgfx::Panel_RGB     _panel;
    Light_CrowPanel50   _light;
    lgfx::Touch_GT911   _touch;
    CrowPanelIoExpander _ioex;

public:
    bool init_impl(bool use_reset, bool use_clear) override {
        if (_ioex.begin()) {
            // GT911 address select: INT low through the rising edge of reset
            // gives 0x5D (Elecrow's and Meshtastic's sequence), then INT is
            // released back to the controller.
            pinMode(TOUCH_INT, OUTPUT);
            digitalWrite(TOUCH_INT, LOW);
            _ioex.set(CROWPANEL_IOEX_TP_RST, false);
            delay(20);
            _ioex.set(CROWPANEL_IOEX_TP_RST, true);
            delay(100);
            pinMode(TOUCH_INT, INPUT);
        }
        return LGFX_Device::init_impl(use_reset, use_clear);
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

        _light.setExpander(&_ioex);
        _panel.setLight(&_light);

        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_WIDTH - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_HEIGHT - 1;
            cfg.pin_int = -1;            // polled; INT is only the address strap
            cfg.pin_rst = -1;            // on the expander, pulsed in init_impl()
            cfg.bus_shared = true;       // the expander is on the same pins
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
