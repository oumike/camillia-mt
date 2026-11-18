// SPDX-License-Identifier: GPL-3.0-or-later
#include <Wire.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

TwoWire Wire("/dev/i2c-1");
TwoWire Wire1("/dev/i2c-1");

bool TwoWire::begin(int, int, uint32_t) {
    if (_fd >= 0) return true;
    _fd = open(_dev, O_RDWR | O_CLOEXEC);
    return _fd >= 0;
}

bool TwoWire::end() {
    if (_fd >= 0) close(_fd);
    _fd = -1;
    return true;
}

void TwoWire::beginTransmission(uint16_t address) {
    _addr = address;
    _txLen = 0;
    _txOpen = true;
    _pendingWrite = false;
}

size_t TwoWire::write(uint8_t c) {
    if (!_txOpen || _txLen >= sizeof(_tx)) return 0;
    _tx[_txLen++] = c;
    return 1;
}

size_t TwoWire::write(const uint8_t *buf, size_t n) {
    size_t done = 0;
    while (done < n && write(buf[done])) done++;
    return done;
}

// Arduino return codes: 0 ok, 2 NACK on address, 4 other error.
uint8_t TwoWire::endTransmission(bool sendStop) {
    if (!_txOpen) return 4;
    _txOpen = false;
    if (!begin()) return 4;
    if (!sendStop) {
        // Register-read idiom: the write goes out with the read in one
        // I2C_RDWR transfer (repeated start) when requestFrom() follows.
        _pendingWrite = true;
        return 0;
    }
    i2c_msg msg = {_addr, 0, (uint16_t)_txLen, _tx};
    // A zero-length write is the presence probe; i2c-dev accepts it as a
    // message with no data, which the controller sends as address + stop.
    i2c_rdwr_ioctl_data x = {&msg, 1};
    return ioctl(_fd, I2C_RDWR, &x) < 0 ? 2 : 0;
}

size_t TwoWire::requestFrom(uint16_t address, size_t size, bool) {
    _rxLen = _rxPos = 0;
    if (size > sizeof(_rx)) size = sizeof(_rx);
    if (!begin() || size == 0) { _pendingWrite = false; return 0; }
    i2c_msg msgs[2];
    unsigned n = 0;
    if (_pendingWrite && _addr == address) msgs[n++] = {address, 0, (uint16_t)_txLen, _tx};
    _pendingWrite = false;
    msgs[n++] = {address, I2C_M_RD, (uint16_t)size, _rx};
    i2c_rdwr_ioctl_data x = {msgs, n};
    if (ioctl(_fd, I2C_RDWR, &x) < 0) return 0;
    _rxLen = size;
    return size;
}
