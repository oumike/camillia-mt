// SPDX-License-Identifier: GPL-3.0-or-later
#include "linux_hal.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

// RadioLibHal(input, output, low, high, rising, falling)
LinuxHal::LinuxHal(const char *spiDev, const char *gpioChip, uint32_t speedHz)
    : RadioLibHal(0, 1, 0, 1, 1, 2), _spiDev(spiDev), _chip(gpioChip), _speed(speedHz) {}

bool LinuxHal::_openChip() {
    if (_chipFd >= 0) return true;
    _chipFd = open(_chip, O_RDWR | O_CLOEXEC);
    if (_chipFd < 0) fprintf(stderr, "[hal] open %s: %s\n", _chip, strerror(errno));
    return _chipFd >= 0;
}

void LinuxHal::init() {
    _openChip();
    spiBegin();
}

void LinuxHal::term() {
    _stopIrqThread();
    spiEnd();
    for (auto &kv : _lines) close(kv.second);
    _lines.clear();
    if (_chipFd >= 0) close(_chipFd);
    _chipFd = -1;
}

bool LinuxHal::_requestLine(uint32_t pin, uint64_t flags) {
    auto it = _lines.find(pin);
    if (it != _lines.end()) {
        gpio_v2_line_config cfg{};
        cfg.flags = flags;
        if (ioctl(it->second, GPIO_V2_LINE_SET_CONFIG_IOCTL, &cfg) < 0) {
            fprintf(stderr, "[hal] gpio%u set config: %s\n", pin, strerror(errno));
            return false;
        }
        return true;
    }
    if (!_openChip()) return false;
    gpio_v2_line_request req{};
    req.offsets[0] = pin;
    req.num_lines = 1;
    strncpy(req.consumer, "camillia-lora", sizeof(req.consumer) - 1);
    req.config.flags = flags;
    if (ioctl(_chipFd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        fprintf(stderr, "[hal] gpio%u request: %s\n", pin, strerror(errno));
        return false;
    }
    _lines[pin] = req.fd;
    return true;
}

void LinuxHal::pinMode(uint32_t pin, uint32_t mode) {
    if (pin == RADIOLIB_NC) return;
    _requestLine(pin, mode == 1 ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT);
}

void LinuxHal::digitalWrite(uint32_t pin, uint32_t value) {
    auto it = _lines.find(pin);
    if (pin == RADIOLIB_NC || it == _lines.end()) return;
    gpio_v2_line_values v{};
    v.mask = 1;
    v.bits = value ? 1 : 0;
    ioctl(it->second, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

uint32_t LinuxHal::digitalRead(uint32_t pin) {
    auto it = _lines.find(pin);
    if (pin == RADIOLIB_NC || it == _lines.end()) return 0;
    gpio_v2_line_values v{};
    v.mask = 1;
    if (ioctl(it->second, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return 0;
    return v.bits & 1;
}

// One interrupt line is all RadioLib uses (DIO1). The callback runs on this
// thread, so it must only do what an ISR would: MeshRadio::_onDio1 sets a flag.
void LinuxHal::attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) {
    if (pin == RADIOLIB_NC || cb == nullptr) return;
    _stopIrqThread();
    uint64_t edge = (mode == 2) ? GPIO_V2_LINE_FLAG_EDGE_FALLING : GPIO_V2_LINE_FLAG_EDGE_RISING;
    if (!_requestLine(pin, GPIO_V2_LINE_FLAG_INPUT | edge)) return;
    if (pipe2(_irqStopPipe, O_CLOEXEC) < 0) return;
    const int lineFd = _lines[pin];
    const int stopFd = _irqStopPipe[0];
    _irqRun = true;
    _irqThread = std::thread([this, lineFd, stopFd, cb]() {
        pollfd fds[2] = {{lineFd, POLLIN, 0}, {stopFd, POLLIN, 0}};
        while (_irqRun) {
            if (poll(fds, 2, -1) < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (fds[1].revents) break;
            if (fds[0].revents & POLLIN) {
                gpio_v2_line_event ev[8];
                if (read(lineFd, ev, sizeof ev) > 0) cb();
            }
        }
    });
}

void LinuxHal::detachInterrupt(uint32_t pin) {
    _stopIrqThread();
    if (pin != RADIOLIB_NC && _lines.count(pin)) _requestLine(pin, GPIO_V2_LINE_FLAG_INPUT);
}

void LinuxHal::_stopIrqThread() {
    if (!_irqThread.joinable()) return;
    _irqRun = false;
    if (write(_irqStopPipe[1], "x", 1) < 0) { /* thread exits on next event */ }
    _irqThread.join();
    close(_irqStopPipe[0]);
    close(_irqStopPipe[1]);
    _irqStopPipe[0] = _irqStopPipe[1] = -1;
}

void LinuxHal::delay(RadioLibTime_t ms) { usleep(ms * 1000); }
void LinuxHal::delayMicroseconds(RadioLibTime_t us) { usleep(us); }
RadioLibTime_t LinuxHal::millis() { return micros() / 1000; }
RadioLibTime_t LinuxHal::micros() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (RadioLibTime_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

void LinuxHal::spiBegin() {
    if (_spiFd >= 0) return;
    _spiFd = open(_spiDev, O_RDWR | O_CLOEXEC);
    if (_spiFd < 0) {
        fprintf(stderr, "[hal] open %s: %s\n", _spiDev, strerror(errno));
        return;
    }
    uint8_t mode = SPI_MODE_0, bits = 8;
    ioctl(_spiFd, SPI_IOC_WR_MODE, &mode);
    ioctl(_spiFd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(_spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &_speed);
}

void LinuxHal::spiTransfer(uint8_t *out, size_t len, uint8_t *in) {
    spiBegin();
    spi_ioc_transfer t{};
    t.tx_buf = (unsigned long)out;
    t.rx_buf = (unsigned long)in;
    t.len = len;
    t.speed_hz = _speed;
    t.bits_per_word = 8;
    if (ioctl(_spiFd, SPI_IOC_MESSAGE(1), &t) < 0)
        fprintf(stderr, "[hal] spi transfer: %s\n", strerror(errno));
}

void LinuxHal::spiEnd() {
    if (_spiFd >= 0) close(_spiFd);
    _spiFd = -1;
}
