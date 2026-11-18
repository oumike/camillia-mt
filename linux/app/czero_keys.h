// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The CardputerZero keyboard for src/keyboard.cpp: the kernel's tca8418
// evdev device, decoded by linux/ui/keyboard.cpp and mapped to KEY_* codes.
#include <stdint.h>

bool czeroKeysBegin();
// Next key as keyboard.h's char codes, or KEY_NONE.
char czeroReadKey();
uint32_t czeroLastKeyMs();
