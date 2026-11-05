#pragma once
// Tanmatsu ST7701 480x800 MIPI-DSI panel. The init sequence is Nicolai
// Electronics' (mipi-dsi-abstraction, dsi_panel_nicolaielectronics_st7701.c,
// MIT). Backlight is a coprocessor register, not a P4 PWM pin.

#include <LovyanGFX.hpp>

#include "config.h"
#include "tanmatsu_coproc.h"
#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"

class Panel_TanmatsuST7701 : public lgfx::Panel_DSI {
protected:
    const uint8_t *getInitParams(size_t listno) const override {
        // Bank 0/1 setup through the D0 power register, which wants 100 ms.
        static constexpr uint8_t list0[] = {
            6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
            1, 0x13,
            2, 0xEF, 0x08,
            6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10,
            3, 0xC0, 0x63, 0x00,
            3, 0xC1, 0x10, 0x02,
            3, 0xC2, 0x37, 0x08,
            2, 0xCC, 0x38,
            17, 0xB0, 0x40, 0xC9, 0x90, 0x0D, 0x0F, 0x04, 0x00, 0x07,
                      0x07, 0x1C, 0x04, 0x52, 0x0F, 0xDF, 0x26, 0xCF,
            17, 0xB1, 0x40, 0xC9, 0xCF, 0x0C, 0x90, 0x04, 0x00, 0x07,
                      0x08, 0x1B, 0x06, 0x55, 0x13, 0x62, 0xE7, 0xCF,
            6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11,
            2, 0xB0, 0x5D,
            2, 0xB1, 0x2D,
            2, 0xB2, 0x07,
            2, 0xB3, 0x80,
            2, 0xB5, 0x08,
            2, 0xB7, 0x85,
            2, 0xB8, 0x20,
            2, 0xB9, 0x10,
            2, 0xC1, 0x78,
            2, 0xC2, 0x78,
            2, 0xD0, 0x88,
            0,
        };
        // GIP timing, then back to bank 0 and out of sleep.
        static constexpr uint8_t list1[] = {
            4, 0xE0, 0x00, 0x19, 0x02,
            12, 0xE1, 0x05, 0xA0, 0x07, 0xA0, 0x04, 0xA0, 0x06, 0xA0,
                      0x00, 0x44, 0x44,
            14, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00,
            5, 0xE3, 0x00, 0x00, 0x33, 0x33,
            3, 0xE4, 0x44, 0x44,
            17, 0xE5, 0x0D, 0x31, 0xC8, 0xAF, 0x0F, 0x33, 0xC8, 0xAF,
                      0x09, 0x2D, 0xC8, 0xAF, 0x0B, 0x2F, 0xC8, 0xAF,
            5, 0xE6, 0x00, 0x00, 0x33, 0x33,
            3, 0xE7, 0x44, 0x44,
            17, 0xE8, 0x0C, 0x30, 0xC8, 0xAF, 0x0E, 0x32, 0xC8, 0xAF,
                      0x08, 0x2C, 0xC8, 0xAF, 0x0A, 0x2E, 0xC8, 0xAF,
            8, 0xEB, 0x02, 0x00, 0xE4, 0xE4, 0x44, 0x00, 0x40,
            3, 0xEC, 0x3C, 0x00,
            17, 0xED, 0xAB, 0x89, 0x76, 0x54, 0x01, 0xFF, 0xFF, 0xFF,
                      0xFF, 0xFF, 0xFF, 0x10, 0x45, 0x67, 0x98, 0xBA,
            6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
            1, CMD_SLPOUT,
            0,
        };
        static constexpr uint8_t list2[] = {
            1, CMD_DISPON,
            0,
        };
        switch (listno) {
            case 0: return list0;
            case 1: return list1;
            case 2: return list2;
            default: return nullptr;
        }
    }

    size_t getInitDelay(size_t listno) const override {
        switch (listno) {
            case 0: return 100;
            case 1: return 120;
            default: return 0;
        }
    }
};

class Light_TanmatsuCoproc : public lgfx::v1::ILight {
public:
    bool init(uint8_t brightness) override {
        setBrightness(brightness);
        return true;
    }

    void setBrightness(uint8_t brightness) override {
        tanmatsuCoprocSetDisplayBacklight(toDuty(brightness));
    }

private:
    // The panel looks fully lit from about 28% PWM duty, so a straight 0-255
    // pass-through left most of the brightness range looking identical. The
    // 5%..100% UI range (duty 13..255) is spread over 6%..28% duty instead, the
    // band wadamesh found to be visible step by step on this panel. Levels
    // below 5% (pre-sleep dim) scale down proportionally and never reach 0
    // unless asked for 0.
    static uint8_t toDuty(uint8_t level) {
        constexpr uint32_t kInLow = 13, kInHigh = 255;    // 5% and 100%
        constexpr uint32_t kOutLow = 15, kOutHigh = 71;   // 6% and 28% of 255
        if (level == 0) return 0;
        if (level < kInLow) {
            const uint32_t v = (uint32_t)level * kOutLow / kInLow;
            return (uint8_t)(v ? v : 1);
        }
        return (uint8_t)(kOutLow + ((uint32_t)level - kInLow) * (kOutHigh - kOutLow)
                                       / (kInHigh - kInLow));
    }
};

class LGFX_TDeck : public lgfx::LGFX_Device {
public:
    LGFX_TDeck() {
        {
            auto cfg = _bus.config();
            cfg.bus_id = 0;
            cfg.lane_num = TANMATSU_DSI_LANES;
            cfg.lane_mbps = TANMATSU_DSI_LANE_MBPS;
            cfg.ldo_chan_id = TANMATSU_DSI_LDO_CHAN;
            cfg.ldo_voltage_mv = TANMATSU_DSI_LDO_MV;
            _bus.config(cfg);
        }
        {
            auto cfg = _panel.config();
            cfg.memory_width = TFT_PANEL_WIDTH;
            cfg.memory_height = TFT_PANEL_HEIGHT;
            cfg.panel_width = TFT_PANEL_WIDTH;
            cfg.panel_height = TFT_PANEL_HEIGHT;
            cfg.readable = true;
            cfg.rgb_order = TFT_RGB_ORDER;
            cfg.bus_shared = false;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.pin_cs = -1;
            // Reset is pulsed by init_impl() below, with ST7701 timing.
            cfg.pin_rst = -1;
            _panel.config(cfg);
            _panel.setBus(&_bus);

            auto detail = _panel.config_detail();
            detail.dpi_freq_mhz = TANMATSU_DSI_DPI_MHZ;
            detail.hsync_back_porch = TANMATSU_DSI_HBP;
            detail.hsync_pulse_width = TANMATSU_DSI_HSYNC;
            detail.hsync_front_porch = TANMATSU_DSI_HFP;
            detail.vsync_back_porch = TANMATSU_DSI_VBP;
            detail.vsync_pulse_width = TANMATSU_DSI_VSYNC;
            detail.vsync_front_porch = TANMATSU_DSI_VFP;
            _panel.config_detail(detail);
        }
        _panel.setLight(&_light);
        setPanel(&_panel);
    }

    bool init_impl(bool use_reset, bool use_clear) override {
        tanmatsuCoprocBegin();
        pinMode(TFT_RST, OUTPUT);
        digitalWrite(TFT_RST, LOW);
        delay(10);
        digitalWrite(TFT_RST, HIGH);
        delay(120);
        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

private:
    lgfx::Bus_DSI _bus;
    Panel_TanmatsuST7701 _panel;
    Light_TanmatsuCoproc _light;
};
