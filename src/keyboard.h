#pragma once
// T-Deck keyboard and trackball event decoding.
#include <Wire.h>
#include "config.h"

// Special key codes returned by TDeckKeyboard::read()
#define KEY_NONE        0x00
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
// Synthetic navigation codes
#define KEY_PREV_CHAN   0x80
#define KEY_NEXT_CHAN   0x81
#define KEY_SCROLL_UP   0x82
#define KEY_SCROLL_DN   0x83
#define KEY_PAGE_UP     0x84
#define KEY_PAGE_DN     0x85
#define KEY_ROLLER      0x86   // trackball click
#define KEY_NODE_FOCUS  0x05   // ALT+E — focus/unfocus node list; verify with serial if wrong

class TDeckKeyboard {
public:
    void begin();
    char readTrackball();   // returns trackball/click event or KEY_NONE
    char readKey();         // returns keyboard key or KEY_NONE

    // Public for static ISR access
    volatile int8_t _dx    = 0;
    volatile int8_t _dy    = 0;
    volatile bool   _click = false;
    static TDeckKeyboard *_instance;

    unsigned long _lastScrollMs = 0;  // tracks most recent scroll event for click guard

private:
    char mapKey(uint8_t raw);

    static void IRAM_ATTR _isrRight();
    static void IRAM_ATTR _isrLeft();
    static void IRAM_ATTR _isrUp();
    static void IRAM_ATTR _isrDown();
    static void IRAM_ATTR _isrClick();
};
