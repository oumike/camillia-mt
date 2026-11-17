// SPDX-License-Identifier: GPL-3.0-or-later
#include "linux_board.h"
#include "config.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

bool linuxCapPowerOn() {
    int fd = open(LINUX_CAP_POWER_LED, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[board] open %s: %s\n", LINUX_CAP_POWER_LED, strerror(errno));
        return false;
    }
    char cur = '0';
    bool wasOn = read(fd, &cur, 1) == 1 && cur != '0';
    bool ok = wasOn || (lseek(fd, 0, SEEK_SET) == 0 && write(fd, "1", 1) == 1);
    close(fd);
    if (!ok) {
        fprintf(stderr, "[board] enabling cap power failed (need root or czero-cap-power.service)\n");
        return false;
    }
    // The cap's rails and the PI4IOE need a moment after a cold switch-on.
    if (!wasOn) usleep(200 * 1000);
    return true;
}

static bool i2cWrite(int fd, uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return write(fd, b, 2) == 2;
}
static bool i2cRead(int fd, uint8_t reg, uint8_t &val) {
    return write(fd, &reg, 1) == 1 && read(fd, &val, 1) == 1;
}

// PI4IOE5V6408: 0x03 direction (1 = output), 0x05 output, 0x07 high-Z.
bool linuxCapArmRfSwitch() {
    int fd = open(LINUX_CAP_I2C_DEV, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[board] open %s: %s\n", LINUX_CAP_I2C_DEV, strerror(errno));
        return false;
    }
    bool ok = ioctl(fd, I2C_SLAVE, LINUX_CAP_IOE_ADDR) >= 0;
    uint8_t out = 0, dir = 0, hiz = 0xFF;
    ok = ok && i2cRead(fd, 0x05, out) && i2cWrite(fd, 0x05, out | 0x01);
    ok = ok && i2cRead(fd, 0x03, dir) && i2cWrite(fd, 0x03, dir | 0x01);
    ok = ok && i2cRead(fd, 0x07, hiz) && i2cWrite(fd, 0x07, hiz & ~0x01);
    close(fd);
    if (!ok) fprintf(stderr, "[board] PI4IOE @0x%02x not responding (is the cap powered?)\n", LINUX_CAP_IOE_ADDR);
    return ok;
}
