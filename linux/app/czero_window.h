// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The CardputerZero's two builds: camillia on its LCD (the launcher), and
// camillia-desktop (CZERO_DESKTOP) in a window on the Raspberry Pi desktop it
// shows on a monitor, with the Tanmatsu's layout (UI_TANMATSU_LAYOUT). In the
// window the panel draws into czeroWindowFrame() instead of the framebuffer, an
// SDL thread shows it, keys come from the window rather than the keypad's evdev
// device (which the desktop also reads), and the mouse is the touch panel. The
// UI takes the window's size as it is resized. In the LCD build the window
// functions do nothing.
#include <stdint.h>

#include "hal/board.h"
#include "ui/keyboard.h"

constexpr bool czeroWindowed() { return CZERO_DESKTOP; }

// The panel's pixels, native RGB565, rows as wide as the panel currently is;
// room for CZERO_WINDOW_MAX_W x _H.
uint16_t *czeroWindowFrame();
// The size the window has settled at, even in both directions; false while it
// is still being dragged.
bool czeroWindowSize(int &w, int &h);
// The size the frame now holds, which is what the window shows.
void czeroWindowShow(int w, int h);
// Opens the window on its own thread; false if SDL could not.
bool czeroWindowOpen();
// 0 black .. 255 full: the backlight, which the window stands in for.
void czeroWindowSetLevel(uint8_t level);
// The window was closed: the app should save and leave.
bool czeroWindowClosed();

// Keys typed into the window, in the keypad's own terms.
bool czeroWindowTakeKey(KeyEvent &out);
bool czeroWindowEscDown();
// The mouse, in panel pixels: whether the left button is down, and where.
bool czeroWindowPointer(int32_t &x, int32_t &y);
// Wheel notches turned since the last call, positive away from the user.
int czeroWindowTakeWheel();

// The desktop menu entry (Internet). The store allows no files in
// /usr/share/applications, so the package keeps it in /usr/share/camillia/ and
// each start copies it to ${XDG_DATA_HOME:-~/.local/share}/applications.
void czeroInstallMenuEntry();
