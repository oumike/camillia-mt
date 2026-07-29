#pragma once
// T-Deck keyboard and trackball event decoding.
#include <Wire.h>
#include "config.h"

// Special key codes returned by TDeckKeyboard::read()
#define KEY_NONE        0x00
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_ESCAPE      0x1B
// Synthetic navigation codes
#define KEY_PREV_CHAN   0x80
#define KEY_NEXT_CHAN   0x81
#define KEY_SCROLL_UP   0x82
#define KEY_SCROLL_DN   0x83
#define KEY_PAGE_UP     0x84
#define KEY_PAGE_DN     0x85
#define KEY_ROLLER      0x86   // trackball click
#define KEY_BACKSPACE_HOLD 0x87  // long-hold BACKSPACE (non-touch panel close)
#define KEY_FN_ENTER    0x88   // Fn+Enter (Cardputer compose shortcut)

// The key currently held down (mapped code), or KEY_NONE when nothing is held,
// plus how long it has been down. Pager builds report this from real press/
// release events; other keyboard builds infer hold from repeated key sightings,
// so callers must treat it as a best-effort enhancement.
char     keyboardHeldKey();
uint32_t keyboardHeldMs();

class TDeckKeyboard {
public:
    void begin();
    char readTrackball();   // returns trackball/click event or KEY_NONE
    char readKey();         // returns keyboard key or KEY_NONE

    // Public for static ISR access
    volatile int8_t _dx    = 0;
    volatile int8_t _dy    = 0;
    volatile bool   _click = false;
#if defined(DEVICE_TLORA_PAGER_TFT)
    volatile uint8_t _rotaryPrevAB = 0;
    volatile int8_t  _rotaryAccum = 0;
    volatile int16_t _rotaryQueued = 0;
#endif
    static TDeckKeyboard *_instance;

    unsigned long _lastScrollMs = 0;  // tracks most recent scroll event for click guard

private:
    char mapKey(uint8_t raw);

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    static constexpr uint8_t CARDPUTER_QUEUE_SIZE = 16;
    char _cardputerQueue[CARDPUTER_QUEUE_SIZE] = {0};
    uint8_t _cardputerHead = 0;
    uint8_t _cardputerTail = 0;
    uint8_t _cardputerCount = 0;
    bool _cardputerEnterDown = false;
    uint32_t _cardputerFnSeenMs = 0;

    void enqueueCardputerKey(char key);
    char dequeueCardputerKey();
    void pumpCardputerKeys();
#endif

    static void IRAM_ATTR _isrRight();
    static void IRAM_ATTR _isrLeft();
    static void IRAM_ATTR _isrUp();
    static void IRAM_ATTR _isrDown();
    static void IRAM_ATTR _isrClick();
#if defined(DEVICE_TLORA_PAGER_TFT)
    static void IRAM_ATTR _isrPagerRotary();
#endif
};

#if defined(DEVICE_CARDPUTER_LORA_HAT)
void cardputerSpeakerSetVolume(uint8_t volume);
bool cardputerSpeakerTone(float frequency, uint32_t duration, int channel = 0, bool stopCurrent = true);
#endif
