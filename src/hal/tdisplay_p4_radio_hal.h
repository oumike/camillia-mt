#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include "hw_tdisplay_p4.h"
#include "tdisplay_p4_io.h"

class TDisplayP4RadioHal : public ArduinoHal {
public:
    static constexpr uint32_t kDio1 = UINT32_MAX - 1U;
    static constexpr uint32_t kReset = UINT32_MAX - 2U;

    TDisplayP4RadioHal()
        : ArduinoHal(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0)) {}

    void pinMode(uint32_t pin, uint32_t mode) override {
        if (pin == kDio1 || pin == kReset) return;
        ArduinoHal::pinMode(pin, mode);
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin == kReset) {
            _resetReleased = value != 0;
            (void)tdisplayP4IoSetRadioResetReleased(_resetReleased);
            return;
        }
        if (pin != kDio1) ArduinoHal::digitalWrite(pin, value);
    }

    uint32_t digitalRead(uint32_t pin) override {
        if (pin == kDio1) {
            bool high = false;
            return tdisplayP4IoReadRadioDio1(high) && high ? HIGH : LOW;
        }
        if (pin == kReset) {
            return _resetReleased ? HIGH : LOW;
        }
        return ArduinoHal::digitalRead(pin);
    }

    void attachInterrupt(uint32_t interruptNum, void (*callback)(void),
                         uint32_t mode) override {
        if (interruptNum == kDio1) {
            (void)mode;
            ::attachInterrupt(digitalPinToInterrupt(TDISPLAY_P4_EXPANDER_INT),
                              callback, FALLING);
            return;
        }
        ArduinoHal::attachInterrupt(interruptNum, callback, mode);
    }

    void detachInterrupt(uint32_t interruptNum) override {
        if (interruptNum == kDio1) {
            ::detachInterrupt(digitalPinToInterrupt(TDISPLAY_P4_EXPANDER_INT));
            return;
        }
        ArduinoHal::detachInterrupt(interruptNum);
    }

    uint32_t pinToInterrupt(uint32_t pin) override {
        if (pin == kDio1) return kDio1;
        return ArduinoHal::pinToInterrupt(pin);
    }

private:
    bool _resetReleased = false;
};
