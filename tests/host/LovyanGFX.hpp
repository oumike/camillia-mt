#pragma once
#include <stdint.h>
#include <stdlib.h>

constexpr int SPI2_HOST = 1;
constexpr uint16_t TFT_RED = 0xF800;
constexpr uint16_t TFT_WHITE = 0xFFFF;

// Compile hal/display.h for send-path tests; no display operation is allowed.
namespace lgfx {
struct HostConfig {
    int spi_host = 0, freq_write = 0, freq_read = 0, use_lock = 0;
    int pin_sclk = 0, spi_mode = 0, spi_3wire = 0;
    int pin_miso = 0, pin_mosi = 0, pin_dc = 0;
    int pin_cs = 0, pin_rst = 0, panel_width = 0, panel_height = 0;
    int offset_x = 0, offset_y = 0, invert = 0, rgb_order = 0;
    int readable = 0, dummy_read_pixel = 0, end_read_delay_us = 0;
    int pin_bl = 0, freq = 0, pwm_channel = 0;
    int x_min = 0, x_max = 0, y_min = 0, y_max = 0, pin_int = 0;
    int bus_shared = 0, offset_rotation = 0, i2c_port = 0, i2c_addr = 0;
    int pin_sda = 0, pin_scl = 0;
};
class HostConfigurable {
public:
    HostConfig config() const { abort(); }
    void config(const HostConfig &) { abort(); }
};
class Bus_SPI : public HostConfigurable {};
class Light_PWM : public HostConfigurable {};
class Touch_GT911 : public HostConfigurable {};
class Panel_ST7789 : public HostConfigurable {
public:
    void setBus(Bus_SPI *) { abort(); }
    void setLight(Light_PWM *) { abort(); }
    void setTouch(Touch_GT911 *) { abort(); }
};
class LGFX_Device {
public:
    void setPanel(Panel_ST7789 *) { abort(); }
};
}
