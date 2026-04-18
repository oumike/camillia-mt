#pragma once
#include <LovyanGFX.hpp>
#include "config.h"

class LGFX_TDeck : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
    lgfx::Touch_GT911   _touch;

public:
    LGFX_TDeck() {
        {
            auto cfg       = _bus.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = SPI_SCK;
            cfg.pin_miso   = SPI_MISO;
            cfg.pin_mosi   = SPI_MOSI;
            cfg.pin_dc     = TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg         = _panel.config();
            cfg.pin_cs       = TFT_CS;
            cfg.pin_rst      = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.invert       = true;
            cfg.rgb_order    = false;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = 239;
            cfg.y_min           = 0;
            cfg.y_max           = 319;
            cfg.pin_int         = TOUCH_INT;
            cfg.bus_shared      = true;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = TOUCH_I2C_PORT;
            cfg.i2c_addr        = TOUCH_ADDR;
            cfg.pin_sda         = TOUCH_SDA;
            cfg.pin_scl         = TOUCH_SCL;
            cfg.freq            = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
