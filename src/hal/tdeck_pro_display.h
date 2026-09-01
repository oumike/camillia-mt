#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <CSE_CST328.h>
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <Wire.h>
#include <cstring>

extern volatile bool g_tdeckProTouchIrq;
void IRAM_ATTR tdeckProTouchInterrupt();

// GxEPD2 drives the exact GDEQ031T10/UC8253 panel. The LovyanGFX sprite is used
// only by direct boot/OTA drawing before LVGL starts; the running UI supplies
// native I1 pixels through pushI1(), with no RGB-to-monochrome conversion.
class LGFX_TDeck : public lgfx::LGFX_Sprite {
public:
    using EinkPanel = GxEPD2_310_GDEQ031T10;
    using EinkDisplay = GxEPD2_BW<EinkPanel, 40>;

    LGFX_TDeck()
        : _epd(EinkPanel(TFT_CS, TFT_DC, TFT_RST, EINK_BUSY_PIN)),
          _cst328(TFT_PANEL_WIDTH, TFT_PANEL_HEIGHT, &Wire, TOUCH_RST, TOUCH_INT) {}

    bool init() {
        pinMode(TFT_CS, OUTPUT);
        pinMode(LORA_CS, OUTPUT);
        pinMode(SD_CS, OUTPUT);
        digitalWrite(TFT_CS, HIGH);
        digitalWrite(LORA_CS, HIGH);
        digitalWrite(SD_CS, HIGH);

        setPsram(true);
        setColorDepth(16);
        if (!createSprite(TFT_PANEL_WIDTH, TFT_PANEL_HEIGHT)) {
            Serial.println("[eink] RGB565 canvas allocation failed");
            return false;
        }
        fillScreen(TFT_WHITE);

        _mono = static_cast<uint8_t *>(ps_malloc(kMonoBytes));
        if (!_mono) {
            _mono = static_cast<uint8_t *>(malloc(kMonoBytes));
        }
        _sentMono = static_cast<uint8_t *>(ps_malloc(kMonoBytes));
        if (!_sentMono) {
            _sentMono = static_cast<uint8_t *>(malloc(kMonoBytes));
        }
        if (!_mono || !_sentMono) {
            Serial.println("[eink] frame buffer allocation failed");
            free(_mono);
            free(_sentMono);
            _mono = nullptr;
            _sentMono = nullptr;
            deleteSprite();
            return false;
        }
        memset(_mono, 0xFF, kMonoBytes);
        memset(_sentMono, 0xFF, kMonoBytes);

        SPI.begin(TFT_SPI_SCK, TFT_SPI_MISO, TFT_SPI_MOSI, TFT_CS);
        _epd.init(115200, true, 2, false, SPI,
                  SPISettings(TFT_SPI_WRITE_HZ, MSBFIRST, SPI_MODE0));
        _epd.setRotation(TFT_ROTATION_DEFAULT);

#if TFT_BL >= 0
        ledcSetup(TFT_BL_PWM_CH, TFT_BL_FREQ, 8);
        ledcAttachPin(TFT_BL, TFT_BL_PWM_CH);
#endif

        Wire.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_I2C_FREQ);
        resetTouch();
        _touchIsCst3530 = probeCst3530();
        if (!_touchIsCst3530) {
            _touchReady = _cst328.begin();
            _cst328.setRotation(TFT_ROTATION_DEFAULT);
        } else {
            pinMode(TOUCH_INT, INPUT_PULLUP);
            _touchReady = initCst3530();
            if (_touchReady) {
                g_tdeckProTouchIrq = false;
                attachInterrupt(digitalPinToInterrupt(TOUCH_INT), tdeckProTouchInterrupt, FALLING);
            }
        }

        Serial.printf("[eink] GDEQ031T10 ready (%dx%d, reset=%d, touch=%s)\n",
                      width(), height(), TFT_RST,
                      _touchReady ? (_touchIsCst3530 ? "CST3530" : "CST328") : "missing");
        _fullRefreshPending = true;
        _refreshPending = true;
        return true;
    }

    void setBrightness(uint8_t brightness) {
        _brightness = brightness;
        if (!_sentFrameValid && brightness != 0) return;
        writeBrightness(brightness);
    }

    void writeBrightness(uint8_t brightness) {
#if TFT_BL >= 0
        ledcWrite(TFT_BL_PWM_CH, TFT_BL_INVERT ? (uint8_t)(255u - brightness) : brightness);
#else
        (void)brightness;
#endif
    }

    void sleep() {
        setBrightness(0);
        _epd.powerOff();
        _sleeping = true;
    }

    void wakeup(bool fullRefresh = true) {
        _sleeping = false;
        requestRefresh(fullRefresh);
    }

    bool getTouch(int32_t *x, int32_t *y) {
        if (!_touchReady || !x || !y) return false;

        int16_t rawX = 0;
        int16_t rawY = 0;
        if (_touchIsCst3530) {
            if (!g_tdeckProTouchIrq && digitalRead(TOUCH_INT) != LOW) return false;
            g_tdeckProTouchIrq = false;
            if (!readCst3530(rawX, rawY)) return false;
        } else {
            if (_cst328.getTouches() == 0) return false;
            const CSE_TouchPoint point = _cst328.getPoint(0);
            rawX = point.x;
            rawY = point.y;
        }

        if (rawX < 0 || rawY < 0 || rawX >= width() || rawY >= height()) return false;
        *x = rawX;
        *y = rawY;
        return true;
    }

    void requestRefresh(bool full = false) {
        _refreshPending = true;
        _fullRefreshPending = _fullRefreshPending || full;
    }

    bool pushI1(int32_t x, int32_t y, int32_t w, int32_t h,
                const uint8_t *pixels, size_t sourceStride) {
        if (!_mono || !pixels || w <= 0 || h <= 0) return false;
        if (x < 0 || y < 0 || x + w > TFT_PANEL_WIDTH || y + h > TFT_PANEL_HEIGHT) return false;
        if ((x & 7) != 0 || (w & 7) != 0) return false;

        const size_t destinationStride = (size_t)TFT_PANEL_WIDTH / 8u;
        const size_t rowBytes = (size_t)w / 8u;
        uint8_t *destination = _mono + (size_t)y * destinationStride + (size_t)x / 8u;
        for (int32_t row = 0; row < h; ++row) {
            memcpy(destination + (size_t)row * destinationStride,
                   pixels + (size_t)row * sourceStride,
                   rowBytes);
        }
        _lvglI1Active = true;
        return true;
    }

    void serviceRefresh(bool force = false) {
        if (!_refreshPending || _sleeping || !_mono) return;
        const uint32_t now = millis();
        if (!force && _lastRefreshMs != 0
            && (uint32_t)(now - _lastRefreshMs) < EINK_REFRESH_MIN_MS) {
            return;
        }

        bool full = _fullRefreshPending || _partialRefreshes >= kFullRefreshEvery;
        if (!_lvglI1Active) makeMonochromeBitmap();
        if (!full && _sentFrameValid && memcmp(_mono, _sentMono, kMonoBytes) == 0) {
            _refreshPending = false;
            return;
        }

        digitalWrite(LORA_CS, HIGH);
        digitalWrite(SD_CS, HIGH);
        digitalWrite(TFT_CS, HIGH);
        if (full) {
            _epd.setFullWindow();
        } else {
            _epd.setPartialWindow(0, 0, width(), height());
        }
        _epd.firstPage();
        do {
            _epd.drawInvertedBitmap(0, 0, _mono, width(), height(), GxEPD_BLACK);
        } while (_epd.nextPage());
        _epd.powerOff();

        memcpy(_sentMono, _mono, kMonoBytes);
        _sentFrameValid = true;
        writeBrightness(_brightness);
        _lastRefreshMs = millis();
        _refreshPending = false;
        _fullRefreshPending = false;
        _partialRefreshes = full ? 0 : (uint8_t)(_partialRefreshes + 1);
    }

private:
    static constexpr size_t kMonoBytes =
        ((size_t)TFT_PANEL_WIDTH * (size_t)TFT_PANEL_HEIGHT) / 8u;
    static constexpr uint8_t kFullRefreshEvery = 10;

    void resetTouch() {
        pinMode(TOUCH_RST, OUTPUT);
        digitalWrite(TOUCH_RST, HIGH);
        delay(20);
        digitalWrite(TOUCH_RST, LOW);
        delay(80);
        digitalWrite(TOUCH_RST, HIGH);
        delay(20);
    }

    bool probeCst3530() {
        const uint8_t command[] = {0xD0, 0x03, 0x00, 0x00};
        uint8_t response[7] = {};
        for (uint8_t attempt = 0; attempt < 5; ++attempt) {
            Wire.beginTransmission((uint8_t)TOUCH_ADDR);
            Wire.write(command, sizeof(command));
            if (Wire.endTransmission() == 0
                && Wire.requestFrom((int)TOUCH_ADDR, (int)sizeof(response)) == sizeof(response)) {
                Wire.readBytes(response, sizeof(response));
                if (response[2] == 0xCA && response[3] == 0xCA) return true;
            }
            const uint8_t wake[] = {0xD0, 0x00, 0x04, 0x00};
            Wire.beginTransmission((uint8_t)TOUCH_ADDR);
            Wire.write(wake, sizeof(wake));
            (void)Wire.endTransmission();
            delay(50);
        }
        return false;
    }

    bool writeCst3530Command(uint32_t command) {
        const uint8_t bytes[] = {
            (uint8_t)(command >> 24),
            (uint8_t)(command >> 16),
            (uint8_t)(command >> 8),
            (uint8_t)command,
        };
        Wire.beginTransmission((uint8_t)TOUCH_ADDR);
        Wire.write(bytes, sizeof(bytes));
        return Wire.endTransmission() == 0;
    }

    bool initCst3530() {
        bool ok = writeCst3530Command(0xD0000400);
        delay(20);
        ok = writeCst3530Command(0xD0000400) && ok;
        delay(20);
        ok = writeCst3530Command(0xD0000000) && ok;
        ok = writeCst3530Command(0xD0000C00) && ok;
        ok = writeCst3530Command(0xD0000100) && ok;
        return ok;
    }

    bool readCst3530(int16_t &x, int16_t &y) {
        const uint8_t readCommand[] = {0xD0, 0x07, 0x00, 0x00};
        const uint8_t clearCommand[] = {0xD0, 0x00, 0x02, 0xAB};
        uint8_t response[50] = {};

        Wire.beginTransmission((uint8_t)TOUCH_ADDR);
        Wire.write(readCommand, sizeof(readCommand));
        if (Wire.endTransmission() != 0
            || Wire.requestFrom((int)TOUCH_ADDR, 9) != 9) {
            return false;
        }
        size_t received = Wire.readBytes(response, 9);
        const uint8_t fingerCount = response[3] & 0x0F;
        const uint8_t keyCount = (response[3] >> 4) & 0x0F;
        const uint8_t totalCount = (uint8_t)(fingerCount + keyCount);
        if (totalCount > 1) {
            size_t extra = (size_t)(totalCount - 1) * 5u;
            if (extra > sizeof(response) - received) extra = sizeof(response) - received;
            const size_t available = Wire.requestFrom((int)TOUCH_ADDR, (int)extra);
            if (available == extra) received += Wire.readBytes(response + received, extra);
        }

        Wire.beginTransmission((uint8_t)TOUCH_ADDR);
        Wire.write(clearCommand, sizeof(clearCommand));
        (void)Wire.endTransmission();

        if (fingerCount == 0 || (response[8] >> 4) == 0) return false;
        const size_t index = (size_t)keyCount * 5u;
        if (index + 7u >= received) return false;
        x = (int16_t)(response[index + 4] | ((uint16_t)(response[index + 7] & 0x0F) << 8));
        y = (int16_t)(response[index + 5] | ((uint16_t)(response[index + 7] & 0xF0) << 4));
        return true;
    }

    void makeMonochromeBitmap() {
        memset(_mono, 0xFF, kMonoBytes);
        const int32_t canvasWidth = width();
        const int32_t canvasHeight = height();
        const size_t stride = ((size_t)canvasWidth + 7u) / 8u;
        for (int32_t y = 0; y < canvasHeight; ++y) {
            for (int32_t x = 0; x < canvasWidth; ++x) {
                const uint16_t color = (uint16_t)readPixelValue(x, y);
                const uint16_t red = (uint16_t)(((color >> 11) & 0x1F) * 255u / 31u);
                const uint16_t green = (uint16_t)(((color >> 5) & 0x3F) * 255u / 63u);
                const uint16_t blue = (uint16_t)((color & 0x1F) * 255u / 31u);
                const uint16_t luminance = (uint16_t)((red * 54u + green * 183u + blue * 19u) >> 8);
                if (luminance < 144u) {
                    _mono[(size_t)y * stride + ((size_t)x >> 3)] &=
                        (uint8_t)~(0x80u >> (x & 7));
                }
            }
        }
    }

    EinkDisplay _epd;
    CSE_CST328 _cst328;
    uint8_t *_mono = nullptr;
    uint8_t *_sentMono = nullptr;
    uint32_t _lastRefreshMs = 0;
    uint8_t _partialRefreshes = 0;
    uint8_t _brightness = 0;
    bool _refreshPending = false;
    bool _fullRefreshPending = false;
    bool _sleeping = false;
    bool _touchReady = false;
    bool _touchIsCst3530 = false;
    bool _sentFrameValid = false;
    bool _lvglI1Active = false;
};