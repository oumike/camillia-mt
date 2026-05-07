#pragma once
// LovyanGFX display + touch profile for LilyGO T-Deck hardware.
#include <LovyanGFX.hpp>
#include "config.h"

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
#else
using DisplayPanel = lgfx::Panel_ST7789;
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
    lgfx::Light_PWM    _light;
#if HAS_TOUCH
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    Touch_Heltec_CHSC6X _touch;
#else
    lgfx::Touch_GT911   _touch;
#endif
#endif

public:
    LGFX_TDeck() {
        {
            auto cfg       = _bus.config();
            cfg.spi_host   = TFT_SPI_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = TFT_SPI_WRITE_HZ;
            cfg.freq_read  = TFT_SPI_READ_HZ;
            cfg.spi_3wire  = TFT_SPI_3WIRE;
            cfg.use_lock   = true;
            cfg.pin_sclk   = TFT_SPI_SCK;
            cfg.pin_miso   = TFT_SPI_MISO;
            cfg.pin_mosi   = TFT_SPI_MOSI;
            cfg.pin_dc     = TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg         = _panel.config();
            cfg.pin_cs       = TFT_CS;
            cfg.pin_rst      = TFT_RST;
            cfg.panel_width  = TFT_PANEL_WIDTH;
            cfg.panel_height = TFT_PANEL_HEIGHT;
            cfg.offset_x     = TFT_PANEL_OFFSET_X;
            cfg.offset_y     = TFT_PANEL_OFFSET_Y;
            cfg.invert       = TFT_INVERT;
            cfg.rgb_order    = TFT_RGB_ORDER;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = TFT_BL;
            cfg.invert      = TFT_BL_INVERT;
            cfg.freq        = TFT_BL_FREQ;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
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
            cfg.offset_rotation = 0;
            cfg.i2c_port        = TOUCH_I2C_PORT;
            cfg.i2c_addr        = TOUCH_ADDR;
            cfg.pin_sda         = TOUCH_SDA;
            cfg.pin_scl         = TOUCH_SCL;
            cfg.freq            = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
#endif
        setPanel(&_panel);
    }
};
