// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The CardputerZero's own settings, which Camillia follows rather than keeping
// copies of (HAS_SYSTEM_SETTINGS): the launcher's Settings app sets the
// backlight level, the screen timeout and the volume, and the OS keeps the
// clock and timezone. Read from the launcher's settings file
// (~/.config/cardputerzero/config.json) each time Camillia starts.

#include <stddef.h>
#include <stdint.h>

// Backlight level, percent (1-100). The launcher's when it has one, else the
// panel's current level, else 60.
int czeroSystemBrightnessPct();
// Screen timeout in seconds; 0 means never.
uint32_t czeroSystemScreenOffSecs();
// Puts the backlight back at the system's level, for the launcher to take over
// a lit screen whatever Camillia left it at (asleep, lock screen).
void czeroSystemRestoreBacklight();
// Plays mono 16-bit PCM through the system's sound server, at the system's
// volume. Returns at once; the sound plays behind the caller.
void czeroSystemPlayPcm(const int16_t *samples, size_t count, unsigned rate);
