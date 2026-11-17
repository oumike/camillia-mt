// SPDX-License-Identifier: GPL-3.0-or-later
// CardputerZero keyboard: the tca8418 keypad's evdev device, turned into
// characters and navigation keys. The kernel driver handles the Fn and Sym
// layers itself; Sym keys arrive as keycodes whose symbols are listed in
// /usr/share/keymaps/tca8418_keypad_m5stack_keymap.map, with a built-in copy
// for when that file is missing.
#pragma once

#include <stdint.h>

#include <string>

enum class Key : uint8_t { None, Char, Enter, Backspace, Delete, Esc, Tab, Up, Down, Left, Right, PageUp, PageDown, Home, End };

struct KeyEvent {
    Key key = Key::None;
    std::string text;   // UTF-8, for Key::Char
};

class Keyboard {
public:
    // Opens the keypad (or the device named by CAMILLIA_KBD); false if none.
    bool open();
    int fd() const { return _fd; }
    // Next decoded key, or Key::None when nothing more is waiting.
    KeyEvent read();
    // Esc is physically down (press seen, release not yet).
    bool escDown() const { return _escDown; }

private:
    void loadKeymap();
    int _fd = -1;
    bool _escDown = false;
    bool _shift = false, _shiftUsed = false, _oneShot = false, _caps = false;
    std::string _sym[256];
};
