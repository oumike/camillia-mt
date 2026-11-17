// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// CardputerZero: the ST7789 is driven by the kernel (panel-mipi-dbi) and shows
// up as a 320x170 RGB565 framebuffer, so LovyanGFX draws through Panel_fb on
// /dev/fb_lcd (the board's udev link) or /dev/fb0. Backlight is the kernel's
// backlight class device, when one is present.

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/framebuffer/Panel_fb.hpp>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

class Light_LinuxBacklight : public lgfx::v1::ILight {
public:
    bool init(uint8_t brightness) override {
        DIR *d = opendir("/sys/class/backlight");
        if (!d) return false;
        while (struct dirent *e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            snprintf(_path, sizeof(_path), "/sys/class/backlight/%s/brightness", e->d_name);
            char maxPath[300];
            snprintf(maxPath, sizeof(maxPath), "/sys/class/backlight/%s/max_brightness", e->d_name);
            if (FILE *f = fopen(maxPath, "r")) {
                if (fscanf(f, "%d", &_max) != 1) _max = 0;
                fclose(f);
            }
            break;
        }
        closedir(d);
        if (_max <= 0) { _path[0] = '\0'; return false; }
        setBrightness(brightness);
        return true;
    }
    void setBrightness(uint8_t brightness) override {
        if (!_path[0]) return;
        // Never fully dark from a non-zero level: 1/255 rounds to 0 on a
        // backlight with few steps, and the panel would look switched off.
        int v = (brightness * _max + 254) / 255;
        if (FILE *f = fopen(_path, "w")) {
            fprintf(f, "%d\n", v);
            fclose(f);
        }
    }

private:
    char _path[280] = {0};
    int _max = 0;
};

// Panel_fb as shipped (LovyanGFX 1.2.34) gets RGB565 wrong both ways: blits copy
// LovyanGFX's byte-swapped pixels straight into the little-endian framebuffer,
// and fills reassemble green with its two halves exchanged. Running the panel
// in rgb565_nonswapped makes every converted pixel native RGB565, which is the
// framebuffer's own format, so blits are right as they stand; the two fill
// paths are replaced to store that value as it is.
class Panel_CzeroFb : public lgfx::Panel_fb {
public:
    lgfx::color_depth_t setColorDepth(lgfx::color_depth_t depth) override {
        (void)depth;
        _write_depth = _read_depth = lgfx::color_depth_t::rgb565_nonswapped;
        _write_bits = _read_bits = 16;
        return _write_depth;
    }
    void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override {
        writeFillRectPreclipped(x, y, 1, 1, rawcolor);
    }
    void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w,
                                 uint_fast16_t h, uint32_t rawcolor) override {
        if (!_fbp) return;
        uint_fast8_t r = _internal_rotation;
        if (r) {
            if ((1u << r) & 0b10010110) y = _height - (y + h);
            if (r & 2) x = _width - (x + w);
            if (r & 1) { std::swap(x, y); std::swap(w, h); }
        }
        const uint16_t c = (uint16_t)rawcolor;
        const size_t stride = _fix_info.line_length;
        for (uint_fast16_t row = 0; row < h; ++row) {
            uint16_t *p = (uint16_t *)(_fbp + (y + row) * stride) + x;
            for (uint_fast16_t col = 0; col < w; ++col) p[col] = c;
        }
    }
};

class LGFX_TDeck : public lgfx::LGFX_Device {
    Panel_CzeroFb _fb;
    Light_LinuxBacklight _light;

public:
    LGFX_TDeck() {
        auto cfg = _fb.config();
        cfg.panel_width = DEVICE_LCD_LANDSCAPE_W;
        cfg.panel_height = DEVICE_LCD_LANDSCAPE_H;
        cfg.memory_width = DEVICE_LCD_LANDSCAPE_W;
        cfg.memory_height = DEVICE_LCD_LANDSCAPE_H;
        _fb.config(cfg);
        _fb.setDeviceName(access("/dev/fb_lcd", F_OK) == 0 ? "/dev/fb_lcd" : "/dev/fb0");
        _fb.setLight(&_light);
        setPanel(&_fb);
    }
};
