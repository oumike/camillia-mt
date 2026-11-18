// SPDX-License-Identifier: GPL-3.0-or-later
#include "czero_keys.h"

#include <Arduino.h>

#include <deque>

#include "czero_app.h"
#include "keyboard.h"
#include "ui/keyboard.h"

static Keyboard s_kbd;
static bool s_open = false;
static uint32_t s_lastOpenTryMs = 0;
static uint32_t s_lastKeyMs = 0;
static std::deque<char> s_pending;

// Esc acts on release; held this long instead it quits back to the launcher.
static constexpr uint32_t kQuitHoldMs = 4000;
static bool s_escPending = false;
static uint32_t s_escSinceMs = 0;

bool czeroKeysBegin() {
    s_lastOpenTryMs = millis();
    s_open = s_kbd.open();
    if (!s_open) Serial.println("[kb] CardputerZero keypad not found (tca8418 evdev)");
    return s_open;
}

static char mapKey(const KeyEvent &e) {
    switch (e.key) {
        case Key::Enter: return KEY_ENTER;
        case Key::Backspace: return KEY_BACKSPACE;
        // As on the Cardputer ADV: Fn+Backspace (delete) is the panel-close key.
        case Key::Delete: return KEY_BACKSPACE_HOLD;
        case Key::Tab: return KEY_TAB;
        case Key::Up: return KEY_SCROLL_UP;
        case Key::Down: return KEY_SCROLL_DN;
        case Key::Left: return KEY_PREV_CHAN;
        case Key::Right: return KEY_NEXT_CHAN;
        case Key::PageUp: return KEY_PAGE_UP;
        case Key::PageDown: return KEY_PAGE_DN;
        default: return KEY_NONE;
    }
}

char czeroReadKey() {
    if (!s_pending.empty()) {
        char c = s_pending.front();
        s_pending.pop_front();
        return c;
    }
    if (!s_open) {
        // The keypad can come up after us at boot; look again now and then.
        if ((uint32_t)(millis() - s_lastOpenTryMs) < 3000) return KEY_NONE;
        if (!czeroKeysBegin()) return KEY_NONE;
    }
    for (;;) {
        KeyEvent e = s_kbd.read();
        if (e.key == Key::None) break;
        s_lastKeyMs = millis();
        if (e.key == Key::Char) {
            // char codes 0x80 and up are commands, so only ASCII is typed.
            for (unsigned char ch : e.text)
                if (ch >= 0x20 && ch < 0x7f) s_pending.push_back((char)ch);
        } else if (e.key == Key::Esc) {
            if (!s_escPending) { s_escPending = true; s_escSinceMs = millis(); }
        } else if (char k = mapKey(e)) {
            s_pending.push_back(k);
        }
    }
    if (s_escPending) {
        if (!s_kbd.escDown()) {
            s_escPending = false;
            s_pending.push_back(KEY_ESCAPE);
        } else if ((uint32_t)(millis() - s_escSinceMs) >= kQuitHoldMs) {
            Serial.println("[kb] Esc held 4 s: quitting to the launcher");
            czeroExit(0);
        }
    }
    if (s_pending.empty()) return KEY_NONE;
    char c = s_pending.front();
    s_pending.pop_front();
    return c;
}

uint32_t czeroLastKeyMs() { return s_lastKeyMs; }
