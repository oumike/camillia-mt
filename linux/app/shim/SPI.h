// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// SPI is spidev, opened by LinuxHal; SPI.begin() in shared code is a no-op.
#include <stdint.h>
class LinuxSPIClass {
public:
    void begin(int = -1, int = -1, int = -1, int = -1) {}
};
inline LinuxSPIClass SPI;
