// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// RadioLib HAL for Linux: spidev for SPI, the GPIO v2 character-device uAPI
// for control lines and DIO1 edge events. No libgpiod/lgpio dependency.
//
// CS is driven by the kernel (spidevB.C), so the Module is built with
// RADIOLIB_NC for CS and those writes are ignored.
#include <RadioLib.h>
#include <atomic>
#include <map>
#include <thread>

class LinuxHal : public RadioLibHal {
public:
    LinuxHal(const char *spiDev, const char *gpioChip, uint32_t speedHz = 2000000);
    ~LinuxHal() override { term(); }

    void init() override;
    void term() override;

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t pin) override;

    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override { return 0; }

    void spiBegin() override;
    void spiBeginTransaction() override {}
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override;
    void spiEndTransaction() override {}
    void spiEnd() override;

private:
    // MeshRadio::init() resets the chip before RadioLib calls init(), so the
    // devices are opened on first use rather than only there.
    bool _openChip();
    bool _requestLine(uint32_t pin, uint64_t flags);
    void _stopIrqThread();

    const char *_spiDev;
    const char *_chip;
    uint32_t _speed;
    int _spiFd = -1;
    int _chipFd = -1;
    std::map<uint32_t, int> _lines;

    std::thread _irqThread;
    std::atomic<bool> _irqRun{false};
    int _irqStopPipe[2] = {-1, -1};
};
