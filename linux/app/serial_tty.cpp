// SPDX-License-Identifier: GPL-3.0-or-later
// HardwareSerial: UART 0 is stdout; UART 1 is the cap's GPS on LINUX_GPS_TTY.
#include <Arduino.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"

HardwareSerial Serial(0);

static const char *uartPath(int uart) {
    switch (uart) {
#ifdef LINUX_GPS_TTY
        case 1: return LINUX_GPS_TTY;
#endif
        default: return nullptr;
    }
}

static speed_t baudConst(unsigned long b) {
    switch (b) {
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B9600;
    }
}

void HardwareSerial::begin(unsigned long baud, uint32_t, int8_t, int8_t, bool, unsigned long, uint8_t) {
    if (_uart == 0) return;
    end();
    const char *path = uartPath(_uart);
    if (!path) return;
    _fd = ::open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (_fd < 0) {
        fprintf(stderr, "[uart%d] open %s: %s\n", _uart, path, strerror(errno));
        return;
    }
    struct termios t;
    if (tcgetattr(_fd, &t) == 0) {
        cfmakeraw(&t);
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
        cfsetispeed(&t, baudConst(baud));
        cfsetospeed(&t, baudConst(baud));
        tcsetattr(_fd, TCSANOW, &t);
    }
    tcflush(_fd, TCIFLUSH);
    _head = _len = 0;
}

void HardwareSerial::end() {
    if (_fd >= 0) ::close(_fd);
    _fd = -1;
    _head = _len = 0;
}

void HardwareSerial::fill() {
    if (_fd < 0 || _len > 0) return;
    ssize_t n = ::read(_fd, _buf, sizeof _buf);
    if (n > 0) {
        _head = 0;
        _len = (size_t)n;
    }
}

int HardwareSerial::available() {
    fill();
    return (int)_len;
}

int HardwareSerial::read() {
    fill();
    if (!_len) return -1;
    _len--;
    return _buf[_head++];
}

int HardwareSerial::peek() {
    fill();
    return _len ? _buf[_head] : -1;
}

void HardwareSerial::flush() {
    if (_uart == 0) fflush(stdout);
    else if (_fd >= 0) tcdrain(_fd);
}

size_t HardwareSerial::write(const uint8_t *buf, size_t n) {
    if (_uart == 0) return fwrite(buf, 1, n, stdout);
    if (_fd < 0) return 0;
    size_t done = 0;
    while (done < n) {
        ssize_t w = ::write(_fd, buf + done, n - done);
        if (w > 0) done += (size_t)w;
        else if (w < 0 && errno == EAGAIN) {
            struct pollfd p = {_fd, POLLOUT, 0};
            poll(&p, 1, 100);
        } else break;
    }
    return done;
}
