#pragma once
#include <Wire.h>
#include "config.h"

// Special key codes returned by TDeckKeyboard::read()
#define KEY_NONE        0x00
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_ESC         0x1B
// Synthetic navigation codes
#define KEY_PREV_CHAN   0x80
#define KEY_NEXT_CHAN   0x81
#define KEY_SCROLL_UP   0x82
#define KEY_SCROLL_DN   0x83
#define KEY_PAGE_UP     0x84
#define KEY_PAGE_DN     0x85
#define KEY_ROLLER      0x86   // trackball click

class TDeckKeyboard {
public:
    void begin();
    char read();

    // Public for static ISR access
    volatile int8_t _dx    = 0;
    volatile int8_t _dy    = 0;
    volatile bool   _click = false;
    static TDeckKeyboard *_instance;

private:
    unsigned long _lastReadMs = 0;
    char mapKey(uint8_t raw);

    static void IRAM_ATTR _isrRight();
    static void IRAM_ATTR _isrLeft();
    static void IRAM_ATTR _isrUp();
    static void IRAM_ATTR _isrDown();
    static void IRAM_ATTR _isrClick();
};
