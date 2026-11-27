// SPDX-License-Identifier: GPL-3.0-or-later
#include "czero_keys.h"

#include <Arduino.h>
#include <lvgl.h>

#include <deque>

#include "czero_app.h"
#include "czero_window.h"
#include "keyboard.h"
#include "ui/keyboard.h"

static Keyboard s_kbd;
static bool s_open = false;
static uint32_t s_lastOpenTryMs = 0;
static uint32_t s_lastKeyMs = 0;
static std::deque<char> s_pending;

// Esc acts on release; held this long instead it quits back to the launcher.
static constexpr uint32_t kQuitHoldMs = 3000;
static bool s_escPending = false;
static uint32_t s_escSinceMs = 0;

// The countdown ring appears only once a hold is clearly not a tap.
static constexpr uint32_t kQuitRingDelayMs = 500;
static lv_obj_t *s_quitPanel = nullptr;
static lv_obj_t *s_quitArc = nullptr;
static lv_obj_t *s_quitLabel = nullptr;
static int s_quitShownSec = -1;
// keys_linux.cpp has no hal/ui_coord_shim.h: the desktop build's doubled
// layout is applied here by hand.
static constexpr int32_t kQ = UI_COORD_SCALE;

static void quitRingHide() {
    if (s_quitPanel) lv_obj_delete(s_quitPanel);
    s_quitPanel = s_quitArc = s_quitLabel = nullptr;
    s_quitShownSec = -1;
}

static void quitRingShow(uint32_t heldMs) {
    if (!s_quitPanel) {
        s_quitPanel = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_quitPanel);
        lv_obj_remove_flag(s_quitPanel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_quitPanel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_quitPanel, 96 * kQ, 96 * kQ);
        lv_obj_center(s_quitPanel);
        lv_obj_set_style_radius(s_quitPanel, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_quitPanel, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_quitPanel, LV_OPA_80, 0);

        s_quitArc = lv_arc_create(s_quitPanel);
        lv_obj_remove_flag(s_quitArc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(s_quitArc, 84 * kQ, 84 * kQ);
        lv_obj_center(s_quitArc);
        lv_arc_set_rotation(s_quitArc, 270);
        lv_arc_set_bg_angles(s_quitArc, 0, 360);
        lv_arc_set_range(s_quitArc, 0, (int32_t)kQuitHoldMs);
        lv_obj_set_style_arc_width(s_quitArc, 6 * kQ, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_quitArc, 6 * kQ, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s_quitArc, lv_color_hex(0x404040), LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_quitArc, lv_color_hex(0xFFB060), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_quitArc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(s_quitArc, 0, LV_PART_KNOB);

        s_quitLabel = lv_label_create(s_quitPanel);
        lv_obj_set_style_text_font(s_quitLabel, kQ > 1 ? &lv_font_montserrat_28 : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_quitLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(s_quitLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_quitLabel);
    }
    const uint32_t left = heldMs >= kQuitHoldMs ? 0 : kQuitHoldMs - heldMs;
    // Counts down: the ring empties as the quit approaches.
    lv_arc_set_value(s_quitArc, (int32_t)left);
    const int sec = (int)((left + 999) / 1000);
    if (sec != s_quitShownSec) {
        s_quitShownSec = sec;
        lv_label_set_text_fmt(s_quitLabel, "Quit\n%d", sec);
    }
}

bool czeroKeysBegin() {
    s_lastOpenTryMs = millis();
    // In a desktop window the keys come from the window: the desktop reads
    // the keypad too, and keys meant for other windows are not ours.
    if (czeroWindowed()) return s_open = true;
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
    const bool windowed = czeroWindowed();
    for (;;) {
        KeyEvent e;
        if (windowed) {
            if (!czeroWindowTakeKey(e)) break;
        } else if ((e = s_kbd.read()).key == Key::None) {
            break;
        }
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
        const uint32_t held = millis() - s_escSinceMs;
        if (!(windowed ? czeroWindowEscDown() : s_kbd.escDown())) {
            s_escPending = false;
            quitRingHide();
            // A hold that showed the ring was a cancelled quit, not an Esc.
            if (held < kQuitRingDelayMs) s_pending.push_back(KEY_ESCAPE);
        } else if (held >= kQuitHoldMs) {
            Serial.println("[kb] Esc held 3 s: quitting");
            czeroExit(0);
        } else if (held >= kQuitRingDelayMs) {
            quitRingShow(held);
        }
    }
    if (s_pending.empty()) return KEY_NONE;
    char c = s_pending.front();
    s_pending.pop_front();
    return c;
}

uint32_t czeroLastKeyMs() { return s_lastKeyMs; }
