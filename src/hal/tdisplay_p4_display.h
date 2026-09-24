#pragma once

#include <LovyanGFX.hpp>
#include <Wire.h>
#include <stdlib.h>

#include "config.h"
#include "tdisplay_p4_io.h"
#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"

class Panel_TDisplayP4RM69A10 : public lgfx::Panel_DSI {
public:
    bool setDcsBrightness(uint8_t brightness) {
        return write_params(0x51, &brightness, 1);
    }

protected:
    const uint8_t *getInitParams(size_t listno) const override {
        static constexpr uint8_t list0[] = {
            2, 0xFE, 0xFD,
            2, 0x80, 0xFC,
            2, 0xFE, 0x00,
            5, 0x2A, 0x00, 0x00, 0x02, 0x37,
            5, 0x2B, 0x00, 0x00, 0x04, 0xCF,
            5, 0x31, 0x00, 0x03, 0x02, 0x34,
            5, 0x30, 0x00, 0x00, 0x04, 0xCF,
            2, 0x12, 0x00,
            2, 0x35, 0x00,
            2, 0x51, 0x00,
            1, CMD_SLPOUT,
            0,
        };
        static constexpr uint8_t list1[] = {
            1, CMD_DISPON,
            0,
        };

        switch (listno) {
            case 0: return list0;
            case 1: return list1;
            default: return nullptr;
        }
    }

    size_t getInitDelay(size_t listno) const override {
        return listno == 0 ? 120 : 0;
    }
};

class Light_TDisplayP4RM69A10 : public lgfx::v1::ILight {
public:
    struct config_t {
        uint8_t brightness = TFT_BRIGHTNESS_DEFAULT;
    };

    const config_t &config() const { return _cfg; }
    void config(const config_t &cfg) { _cfg = cfg; }
    void setPanel(Panel_TDisplayP4RM69A10 *panel) { _panel = panel; }

    bool init(uint8_t brightness) override {
        setBrightness(brightness);
        return _panel != nullptr;
    }

    void setBrightness(uint8_t brightness) override {
        if (_panel && _panel->setDcsBrightness(brightness)) {
            _cfg.brightness = brightness;
        }
    }

    uint8_t getBrightness() const { return _cfg.brightness; }

private:
    Panel_TDisplayP4RM69A10 *_panel = nullptr;
    config_t _cfg;
};

class Touch_TDisplayP4GT9895 : public lgfx::ITouch {
public:
    Touch_TDisplayP4GT9895() {
        _cfg.i2c_addr = TOUCH_ADDR;
        _cfg.x_min = 0;
        _cfg.x_max = TFT_PANEL_WIDTH - 1;
        _cfg.y_min = 0;
        _cfg.y_max = TFT_PANEL_HEIGHT - 1;
    }

    bool init() override {
        if (!tdisplayP4IoBegin()) return false;
        if (!tdisplayP4IoSetTouchResetReleased(false)) return false;
        delay(30);
        if (!tdisplayP4IoSetTouchResetReleased(true)) return false;
        delay(100);

        Wire.setClock(TOUCH_I2C_FREQ);
        Wire.setBufferSize(kMaximumRuntimeInfoSize);

        uint8_t firmware[kFirmwareInfoSize] = {};
        if (!readRegister(kFirmwareVersionAddress, firmware, sizeof(firmware))
            || !validChecksum(firmware, sizeof(firmware))
            || firmware[10] != '9' || firmware[11] != '8'
            || firmware[12] != '9' || firmware[13] != '5') {
            Serial.println("[tdisplay-p4-touch] GT9895 firmware probe failed");
            return false;
        }

        uint8_t lengthBytes[2] = {};
        if (!readRegister(kRuntimeInfoAddress, lengthBytes, sizeof(lengthBytes))) {
            Serial.println("[tdisplay-p4-touch] runtime length read failed");
            return false;
        }
        const size_t length = littleEndian16(lengthBytes);
        if (length < kMinimumRuntimeInfoSize || length > kMaximumRuntimeInfoSize) {
            Serial.printf("[tdisplay-p4-touch] invalid runtime length=%u\n",
                          (unsigned)length);
            return false;
        }

        uint8_t *runtime = static_cast<uint8_t *>(malloc(length));
        if (!runtime) return false;
        const bool readOk = readRegister(kRuntimeInfoAddress, runtime, length)
                         && validChecksum(runtime, length);
        if (!readOk) {
            free(runtime);
            Serial.println("[tdisplay-p4-touch] runtime table read failed");
            return false;
        }

        size_t offset = 2 + 16 + 10 + 4;
        for (size_t arrayIndex = 0; arrayIndex < 5; ++arrayIndex) {
            if (offset >= length - 2) {
                free(runtime);
                return false;
            }
            const size_t byteCount = (size_t)runtime[offset++] * 2u;
            if (byteCount > length - 2 - offset) {
                free(runtime);
                return false;
            }
            offset += byteCount;
        }
        if (offset + 48 > length - 2) {
            free(runtime);
            return false;
        }
        _touchDataAddress = littleEndian32(runtime + offset + 44);
        free(runtime);

        if (_touchDataAddress == 0) {
            Serial.println("[tdisplay-p4-touch] touch report address missing");
            return false;
        }
        Serial.printf("[tdisplay-p4-touch] GT9895 ready report=0x%08lX\n",
                      (unsigned long)_touchDataAddress);
        return true;
    }

    uint_fast8_t getTouchRaw(lgfx::touch_point_t *points,
                             uint_fast8_t count) override {
        if (!points || count == 0 || _touchDataAddress == 0) return 0;

        uint8_t header[kEventHeaderSize] = {};
        if (!readRegister(_touchDataAddress, header, sizeof(header))) return 0;
        if (header[0] == 0) return 0;
        if (!validChecksum(header, sizeof(header))) {
            (void)clearStatus();
            return 0;
        }

        const uint8_t contactCount = header[2] & 0x0F;
        if ((header[0] & kTouchEventMask) == 0 || contactCount == 0
            || contactCount > kMaximumContacts) {
            (void)clearStatus();
            return 0;
        }

        const size_t contactBytes = (size_t)contactCount * kBytesPerContact;
        const size_t payloadSize = contactBytes + kChecksumSize;
        uint8_t payload[kMaximumContacts * kBytesPerContact + kChecksumSize] = {};
        if (!readRegister(_touchDataAddress + kEventHeaderSize,
                          payload, payloadSize)
            || !validChecksum(payload, payloadSize)) {
            (void)clearStatus();
            return 0;
        }

        const uint_fast8_t returned = count < contactCount ? count : contactCount;
        for (uint_fast8_t index = 0; index < returned; ++index) {
            const uint8_t *contact = payload + index * kBytesPerContact;
            const uint16_t rawX = littleEndian16(contact + 2);
            const uint16_t rawY = littleEndian16(contact + 4);
            points[index].x = scaleCoordinate(rawX, TDISPLAY_P4_TOUCH_RAW_WIDTH,
                                              TFT_PANEL_WIDTH);
            points[index].y = scaleCoordinate(rawY, TDISPLAY_P4_TOUCH_RAW_HEIGHT,
                                              TFT_PANEL_HEIGHT);
            points[index].size = littleEndian16(contact + 6);
            points[index].id = (uint8_t)((contact[0] >> 4) & 0x0F);
        }
        (void)clearStatus();
        return returned;
    }

    void wakeup() override {
        (void)init();
    }

    void sleep() override {}

private:
    static constexpr uint32_t kFirmwareVersionAddress = 0x00010014;
    static constexpr uint32_t kRuntimeInfoAddress = 0x00010070;
    static constexpr size_t kFirmwareInfoSize = 28;
    static constexpr size_t kMinimumRuntimeInfoSize = 64;
    static constexpr size_t kMaximumRuntimeInfoSize = 1024;
    static constexpr size_t kEventHeaderSize = 8;
    static constexpr size_t kBytesPerContact = 8;
    static constexpr size_t kChecksumSize = 2;
    static constexpr uint8_t kMaximumContacts = 10;
    static constexpr uint8_t kTouchEventMask = 0x80;

    bool readRegister(uint32_t address, uint8_t *data, size_t length) {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write((uint8_t)(address >> 24));
        Wire.write((uint8_t)(address >> 16));
        Wire.write((uint8_t)(address >> 8));
        Wire.write((uint8_t)address);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom((uint8_t)TOUCH_ADDR, length, true) != length) {
            return false;
        }
        for (size_t i = 0; i < length; ++i) data[i] = (uint8_t)Wire.read();
        return true;
    }

    bool writeRegister(uint32_t address, const uint8_t *data, size_t length) {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write((uint8_t)(address >> 24));
        Wire.write((uint8_t)(address >> 16));
        Wire.write((uint8_t)(address >> 8));
        Wire.write((uint8_t)address);
        Wire.write(data, length);
        return Wire.endTransmission() == 0;
    }

    bool clearStatus() {
        const uint8_t clear = 0;
        return writeRegister(_touchDataAddress, &clear, 1);
    }

    static uint16_t littleEndian16(const uint8_t *data) {
        return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    }

    static uint32_t littleEndian32(const uint8_t *data) {
        return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
             | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }

    static bool validChecksum(const uint8_t *data, size_t length) {
        if (!data || length < kChecksumSize) return false;
        uint32_t sum = 0;
        for (size_t i = 0; i < length - kChecksumSize; ++i) sum += data[i];
        return (uint16_t)sum == littleEndian16(data + length - 2);
    }

    static uint16_t scaleCoordinate(uint16_t value, uint16_t sourceExtent,
                                    uint16_t targetExtent) {
        uint32_t scaled = (uint32_t)value * targetExtent / sourceExtent;
        if (scaled >= targetExtent) scaled = targetExtent - 1;
        return (uint16_t)scaled;
    }

    uint32_t _touchDataAddress = 0;
};

class LGFX_TDeck : public lgfx::LGFX_Device {
public:
    LGFX_TDeck() {
        {
            auto cfg = _bus.config();
            cfg.bus_id = 0;
            cfg.lane_num = TDISPLAY_P4_DSI_LANES;
            cfg.lane_mbps = TDISPLAY_P4_DSI_LANE_MBPS;
            cfg.ldo_chan_id = 3;
            cfg.ldo_voltage_mv = 2500;
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
            cfg.pin_rst = -1;
            _panel.config(cfg);
            _panel.setBus(&_bus);

            auto detail = _panel.config_detail();
            detail.dpi_freq_mhz = TDISPLAY_P4_DSI_DPI_MHZ;
            detail.hsync_back_porch = TDISPLAY_P4_DSI_HBP;
            detail.hsync_pulse_width = TDISPLAY_P4_DSI_HSYNC;
            detail.hsync_front_porch = TDISPLAY_P4_DSI_HFP;
            detail.vsync_back_porch = TDISPLAY_P4_DSI_VBP;
            detail.vsync_pulse_width = TDISPLAY_P4_DSI_VSYNC;
            detail.vsync_front_porch = TDISPLAY_P4_DSI_VFP;
            _panel.config_detail(detail);
        }
        {
            auto cfg = _touch.config();
            cfg.pin_rst = -1;
            cfg.pin_sda = TOUCH_SDA;
            cfg.pin_scl = TOUCH_SCL;
            cfg.pin_int = -1;
            cfg.freq = TOUCH_I2C_FREQ;
            cfg.x_min = 0;
            cfg.x_max = TFT_PANEL_WIDTH - 1;
            cfg.y_min = 0;
            cfg.y_max = TFT_PANEL_HEIGHT - 1;
            cfg.i2c_port = TOUCH_I2C_PORT;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        {
            auto cfg = _light.config();
            cfg.brightness = TFT_BRIGHTNESS_DEFAULT;
            _light.config(cfg);
            _light.setPanel(&_panel);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }

    bool init_impl(bool use_reset, bool use_clear) override {
        if (!tdisplayP4IoBegin()) return false;
        if (!tdisplayP4IoSetScreenResetReleased(false)) return false;
        delay(10);
        if (!tdisplayP4IoSetScreenResetReleased(true)) return false;
        delay(120);
        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

private:
    lgfx::Bus_DSI _bus;
    Panel_TDisplayP4RM69A10 _panel;
    Touch_TDisplayP4GT9895 _touch;
    Light_TDisplayP4RM69A10 _light;
};