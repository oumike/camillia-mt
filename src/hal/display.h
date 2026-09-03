#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/display.h — LovyanGFX display + touch configuration
//
// Provides the LGFX_TDeck class used to initialise the SPI TFT panel and
// optional touch controller on every supported hardware target.  Despite the
// class name, all four builds share this single file — per-device behaviour
// is governed by the TFT_* and TOUCH_* macros from hal/board.h.
//
// Display panel selection:
//   DEVICE_HELTEC_V4_EXPANSION  → Panel_HeltecV4Tft (ST7789 + custom gamma)
//   DEVICE_TLORA_PAGER_TFT      → Panel_ST7796
//   DEVICE_WIO_TRACKER_L2               → Panel_NV3031B (quad-SPI)
//   All others                   → Panel_ST7789
//
// Touch controller selection:
//   DEVICE_HELTEC_V4_EXPANSION  → Touch_Heltec_CHSC6X  (custom LGFX wrapper)
//   DEVICE_TDECK / DEVICE_WIO_TRACKER_L2 → lgfx::Touch_GT911
//   All others                  → no touch
// ════════════════════════════════════════════════════════════════════════════

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#  include <M5GFX.h>
#  include <lgfx/v1/panel/Panel_ST7789.hpp>
#else
#  include <LovyanGFX.hpp>
#endif
#include "config.h"

#if defined(DEVICE_TDECK_PRO)
#include "tdeck_pro_display.h"
#else

#if defined(DEVICE_HELTEC_V4_EXPANSION) && HAS_TOUCH
#include "chsc6x.h"
#include "lgfx/v1/Touch.hpp"
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
class Panel_HeltecV4Tft : public lgfx::Panel_ST7789 {
protected:
    const uint8_t *getInitCommands(uint8_t listno) const override {
        static uint8_t list[] = { 0x26, 1, 0x01, 0xFF, 0xFF };
        if (listno == 1) return list;
        return lgfx::Panel_ST7789::getInitCommands(listno);
    }
};
using DisplayPanel = Panel_HeltecV4Tft;
#elif defined(DEVICE_TLORA_PAGER_TFT)
using DisplayPanel = lgfx::Panel_ST7796;
#elif defined(DEVICE_WIO_TRACKER_L2)
// Only in LovyanGFX 1.2.27+, which is why [env:wio-tracker-l2] pins that
// version in its own libdeps copy while the other display envs stay on 1.1.x.
#include <lgfx/v1/panel/Panel_NV3031B.hpp>
using DisplayPanel = lgfx::Panel_NV3031B;
#else
using DisplayPanel = lgfx::Panel_ST7789;
#endif

#if defined(DEVICE_WIO_TRACKER_L2)
class Light_WioTrackerL2LP5814 : public lgfx::v1::ILight {
public:
    struct config_t {
        uint8_t brightness = TFT_BRIGHTNESS_DEFAULT;
    };

    const config_t &config() const { return _cfg; }
    void config(const config_t &cfg) { _cfg = cfg; }

    bool init(uint8_t brightness) override {
        Wire.beginTransmission(LP5814_ADDR);
        if (Wire.endTransmission() != 0) {
            Serial.printf("[wio-l2-light] LP5814 not found at 0x%02X\n", LP5814_ADDR);
            return false;
        }

        bool ok = true;
        ok = writeReg(LP5814_REG_DEVICE_CONFIG0, 0x01) && ok;
        ok = writeReg(LP5814_REG_MAX_CURRENT, 0x01) && ok;
        ok = writeReg(LP5814_REG_ENABLE_CONTROL, 0x00) && ok;
        ok = writeReg(LP5814_REG_DIM_MODE, 0x4E) && ok;
        ok = writeReg(LP5814_REG_ENGINE_MODE, 0xF0) && ok;
        for (uint8_t channel = 0; channel < 4; channel++) {
            ok = writeReg(LP5814_REG_LED0_DC + channel, LP5814_LED_DC_VALUE) && ok;
        }
        ok = writeReg(LP5814_REG_ENABLE_CONTROL, 0x0F) && ok;
        ok = writeReg(LP5814_REG_UPDATE, 0x55) && ok;
        delay(5);
        setBrightness(brightness);
        return ok;
    }

    void setBrightness(uint8_t brightness) override {
        for (uint8_t channel = 0; channel < 4; channel++) {
            (void)writeReg(LP5814_REG_LED0_PWM + channel, brightness);
        }
        _cfg.brightness = brightness;
    }

    uint8_t getBrightness() const { return _cfg.brightness; }

private:
    bool writeReg(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(LP5814_ADDR);
        Wire.write(reg);
        Wire.write(value);
        const uint8_t error = Wire.endTransmission();
        if (error != 0) {
            Serial.printf("[wio-l2-light] LP5814 write reg 0x%02X failed: %u\n",
                          reg, (unsigned)error);
            return false;
        }
        return true;
    }

    config_t _cfg;
};
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION) && HAS_TOUCH
class Touch_Heltec_CHSC6X : public lgfx::ITouch {
public:
    Touch_Heltec_CHSC6X() {
        _cfg.i2c_addr = TOUCH_ADDR;
        _cfg.x_min = 0;
        _cfg.x_max = TFT_PANEL_WIDTH - 1;
        _cfg.y_min = 0;
        _cfg.y_max = TFT_PANEL_HEIGHT - 1;
    }

    bool init(void) override {
        if (_touch == nullptr) {
#if (TOUCH_I2C_PORT == 1)
            _touch = new chsc6x(&Wire1, TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST);
#else
            _touch = new chsc6x(&Wire, TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST);
#endif
        }
        _touch->chsc6x_init();
        return true;
    }

    uint_fast8_t getTouchRaw(lgfx::touch_point_t *tp, uint_fast8_t /*count*/) override {
        uint16_t rawX = 0;
        uint16_t rawY = 0;
        if (_touch && _touch->chsc6x_read_touch_info(&rawX, &rawY) == 0) {
            int16_t x = (int16_t)rawX;
            int16_t y = (int16_t)rawY;

            // Some CHSC6X firmwares report swapped axes (320x240 instead of 240x320).
            // Normalize to panel-native portrait coordinates and let LGFX rotation handle the rest.
            if (x >= TFT_PANEL_WIDTH || y >= TFT_PANEL_HEIGHT) {
                x = (int16_t)rawY;
                y = (int16_t)rawX;
            }

            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= TFT_PANEL_WIDTH) x = TFT_PANEL_WIDTH - 1;
            if (y >= TFT_PANEL_HEIGHT) y = TFT_PANEL_HEIGHT - 1;

            tp[0].x = x;
            tp[0].y = y;
            tp[0].size = 1;
            tp[0].id = 1;
            return 1;
        }

        tp[0].size = 0;
        return 0;
    }

    void wakeup(void) override {}
    void sleep(void) override {}

private:
    chsc6x *_touch = nullptr;
};
#endif

class LGFX_TDeck : public lgfx::LGFX_Device {
    DisplayPanel _panel;
    lgfx::Bus_SPI      _bus;
#if defined(DEVICE_WIO_TRACKER_L2)
    Light_WioTrackerL2LP5814 _light;
#else
    lgfx::Light_PWM    _light;
#endif
#if HAS_TOUCH
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    Touch_Heltec_CHSC6X _touch;
#else
    lgfx::Touch_GT911   _touch;
#endif
#endif

public:
#if defined(DEVICE_WIO_TRACKER_L2)
    bool init_impl(bool use_reset, bool use_clear) override {
        (void)_light.init(_light.config().brightness);
        const bool result = LGFX_Device::init_impl(use_reset, use_clear);
        Wire.end();
        Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
        Wire.setClock(BOARD_I2C_FREQ);
        return result;
    }
#endif

    LGFX_TDeck() {
        {
            auto cfg       = _bus.config();
            cfg.spi_host   = TFT_SPI_HOST;
            cfg.freq_write = TFT_SPI_WRITE_HZ;
            cfg.freq_read  = TFT_SPI_READ_HZ;
            cfg.use_lock   = true;
            cfg.pin_sclk   = TFT_SPI_SCK;
#if defined(DEVICE_WIO_TRACKER_L2)
            // Quad-SPI. 1.2.27 drives quad through this same Bus_SPI class --
            // setting pin_io0..pin_io3 is what selects it, there is no separate
            // bus type. There is no DC line on a QSPI panel: command/data is
            // carried in the transaction, so pin_dc stays -1 and pin_mosi/miso
            // are unused in favour of the four IO lines.
            cfg.spi_mode   = TFT_SPI_MODE;
            cfg.spi_3wire  = false;
            cfg.pin_miso   = -1;
            cfg.pin_mosi   = -1;
            cfg.pin_dc     = -1;
            cfg.pin_io0    = TFT_QSPI_IO0;
            cfg.pin_io1    = TFT_QSPI_IO1;
            cfg.pin_io2    = TFT_QSPI_IO2;
            cfg.pin_io3    = TFT_QSPI_IO3;
#else
            cfg.spi_mode   = 0;
            cfg.spi_3wire  = TFT_SPI_3WIRE;
            cfg.pin_miso   = TFT_SPI_MISO;
            cfg.pin_mosi   = TFT_SPI_MOSI;
            cfg.pin_dc     = TFT_DC;
#endif
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg         = _panel.config();
            cfg.pin_cs       = TFT_CS;
            cfg.pin_rst      = TFT_RST;
            cfg.panel_width  = TFT_PANEL_WIDTH;
            cfg.panel_height = TFT_PANEL_HEIGHT;
#if defined(DEVICE_WIO_TRACKER_L2)
            cfg.pin_busy     = -1;
            cfg.memory_width = TFT_PANEL_WIDTH;
            cfg.memory_height = TFT_PANEL_HEIGHT;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = false;
#endif
            cfg.offset_x     = TFT_PANEL_OFFSET_X;
            cfg.offset_y     = TFT_PANEL_OFFSET_Y;
            cfg.invert       = TFT_INVERT;
            cfg.rgb_order    = TFT_RGB_ORDER;
#if defined(TFT_PANEL_OFFSET_ROTATION)
            cfg.offset_rotation = TFT_PANEL_OFFSET_ROTATION;
#endif
            // Screenshot capture depends on panel readback.
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
            cfg.readable = true;
        #if defined(DEVICE_TDECK)
            // T-Deck ST7789 readback aligns with panel default phase.
            cfg.dummy_read_pixel = 16;
            cfg.end_read_delay_us = 8;
    #else
                        // Cardputer ST7789V2 readback is cleaner with default 16 dummy bits.
                        cfg.dummy_read_pixel = 16;
    #endif
#else
            cfg.readable = (TFT_SPI_MISO >= 0) && !TFT_SPI_3WIRE;
#endif
            _panel.config(cfg);
        }
        {
#if defined(DEVICE_WIO_TRACKER_L2)
            auto cfg        = _light.config();
            cfg.brightness  = TFT_BRIGHTNESS_DEFAULT;
            _light.config(cfg);
#else
            auto cfg        = _light.config();
            cfg.pin_bl      = TFT_BL;
            cfg.invert      = TFT_BL_INVERT;
            cfg.freq        = TFT_BL_FREQ;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
#endif
            _panel.setLight(&_light);
        }
#if HAS_TOUCH
        {
            auto cfg = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = TFT_PANEL_WIDTH - 1;
            cfg.y_min           = 0;
            cfg.y_max           = TFT_PANEL_HEIGHT - 1;
            cfg.pin_int         = TOUCH_INT;
            cfg.bus_shared      = false;
#if defined(TOUCH_OFFSET_ROTATION)
            cfg.offset_rotation = TOUCH_OFFSET_ROTATION;
#else
            cfg.offset_rotation = 0;
#endif
            cfg.i2c_port        = TOUCH_I2C_PORT;
            cfg.i2c_addr        = TOUCH_ADDR;
            cfg.pin_sda         = TOUCH_SDA;
            cfg.pin_scl         = TOUCH_SCL;
#if defined(TOUCH_I2C_FREQ)
            cfg.freq            = TOUCH_I2C_FREQ;
#else
            cfg.freq            = 400000;
#endif
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
#endif
        setPanel(&_panel);
    }
};

#endif  // DEVICE_TDECK_PRO
