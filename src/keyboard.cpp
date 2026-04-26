#include <Arduino.h>
#include "keyboard.h"

TDeckKeyboard *TDeckKeyboard::_instance = nullptr;

void TDeckKeyboard::begin() {
    Wire.begin(KB_SDA, KB_SCL, 100000UL);
    Wire.setClock(400000UL);
    delay(50);
    Wire.beginTransmission(KB_ADDR);
    Wire.endTransmission();
    delay(50);

    pinMode(TBALL_UP,    INPUT_PULLUP);
    pinMode(TBALL_DOWN,  INPUT_PULLUP);
    pinMode(TBALL_LEFT,  INPUT_PULLUP);
    pinMode(TBALL_RIGHT, INPUT_PULLUP);
    pinMode(TBALL_CLICK, INPUT_PULLUP);

    _instance = this;
    // Physical mapping (empirically confirmed):
    //   roll right → TBALL_DOWN, roll left  → TBALL_LEFT
    //   roll up    → TBALL_RIGHT, roll down → TBALL_UP
    attachInterrupt(digitalPinToInterrupt(TBALL_DOWN),  _isrRight, FALLING);
    attachInterrupt(digitalPinToInterrupt(TBALL_LEFT),  _isrLeft,  FALLING);
    attachInterrupt(digitalPinToInterrupt(TBALL_RIGHT), _isrDown,  FALLING);
    attachInterrupt(digitalPinToInterrupt(TBALL_UP),    _isrUp,    FALLING);
    attachInterrupt(digitalPinToInterrupt(TBALL_CLICK), _isrClick, FALLING);
}

char TDeckKeyboard::readTrackball() {
    unsigned long now = millis();
    static unsigned long lastMoveEmitMs = 0;
    const unsigned long moveDebounceMs = 45;

    // Drain trackball ISR state
    noInterrupts();
    int8_t dx  = _dx;
    int8_t dy  = _dy;
    bool   clk = _click;
    _dx = _dy = 0;
    _click = false;
    interrupts();

    // Track the last time scroll motion was seen
    if (dx != 0 || dy != 0) _lastScrollMs = now;

    // Suppress accidental clicks that fire during or just after rolling
    if (clk && (now - _lastScrollMs >= 200)) return KEY_ROLLER;

    // Ignore overly-frequent movement pulses (trackball bounce/noise).
    if ((dy != 0 || dx != 0) && (now - lastMoveEmitMs < moveDebounceMs)) return KEY_NONE;

    if (dy < 0) { lastMoveEmitMs = now; return KEY_SCROLL_UP; }
    if (dy > 0) { lastMoveEmitMs = now; return KEY_SCROLL_DN; }

    if (dx != 0) lastMoveEmitMs = now;

    return KEY_NONE;
}

char TDeckKeyboard::readKey() {
    // Read immediately; higher-level poll loop already controls cadence.
    // A local gate here prevents draining buffered bursts and drops keys.
    Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
    if (!Wire.available()) return KEY_NONE;
    uint8_t raw = Wire.read();
    if (raw == 0x00 || raw == 0xFF) return KEY_NONE;
    return mapKey(raw);
}

char TDeckKeyboard::mapKey(uint8_t raw) {
    switch (raw) {
        case 0x0D: return KEY_ENTER;
        case 0x0A: return KEY_ENTER;
        case 0x7F: return KEY_BACKSPACE;
        case 0x08: return KEY_BACKSPACE;
        case 0x05: return KEY_NODE_FOCUS;  // ALT+E
        default:
            if (raw < 0x20 || raw > 0x7E)
                Serial.printf("[kb] raw=0x%02X\n", raw);
            return (char)raw;
    }
}

void IRAM_ATTR TDeckKeyboard::_isrRight() { if (_instance) _instance->_dx++; }
void IRAM_ATTR TDeckKeyboard::_isrLeft()  { if (_instance) _instance->_dx--; }
void IRAM_ATTR TDeckKeyboard::_isrUp()    { if (_instance) _instance->_dy--; }
void IRAM_ATTR TDeckKeyboard::_isrDown()  { if (_instance) _instance->_dy++; }
void IRAM_ATTR TDeckKeyboard::_isrClick() { if (_instance) _instance->_click = true; }
