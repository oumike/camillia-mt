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
// Mesh Deck symbol key. Its 48-key matrix reaches letters, digits, comma and
// period and nothing else, so this opens the on-screen symbol tray rather than
// typing a character of its own.
#define KEY_SYMBOL      0x89
// Dedicated M9 Messages button. Kept synthetic so its raw controller code does
// not collide with KEY_NEXT_CHAN.
#define KEY_OPEN_DMS    0x8A
#define KEY_OPEN_HOME   0x8B
#define KEY_OPEN_LIVE   0x8C
#define KEY_OPEN_DISCOVERY 0x8D
#define KEY_OPEN_NODES  0x8E
#define KEY_SLEEP_SCREEN 0x8F
// The M9's dedicated Back button, distinct from the keyboard's Backspace key.
// Both used to map to KEY_BACKSPACE, which made them impossible to tell apart
// downstream — Back deleted one character at a time and could not be given its
// own meaning. Compose treats this as "discard the draft and close"; every
// other consumer goes through isBackspaceKey(), which counts it, so Back
// behaves as it always did everywhere else.
//
// 0x91 rather than 0x90: the M9 driver already names raw 0x90 as a value it
// drops, and reusing the number for a mapped code invites confusing the two.
#define KEY_BACK_BTN    0x91
#define KEY_TOGGLE_KB_BACKLIGHT 0x92
// Alt+C. No dedicated button raises this — the M9's controller resolves Alt
// itself and never reports the combination, so this one is keyboard-only.
#define KEY_OPEN_CONFIG 0x93

// The key currently held down (mapped code), or KEY_NONE when nothing is held,
// plus how long it has been down. Pager builds report this from real press/
// release events; other keyboard builds infer hold from repeated key sightings,
// so callers must treat it as a best-effort enhancement.
#if defined(DEVICE_MESH_DECK)
// Whether each keyboard half's AW9523 answered at init. Exposed because the
// boot-time report is printed before the USB bridge settles and is routinely
// lost — a half that never came up looks exactly like a keymap bug otherwise.
bool     meshDeckKeyboardHalfPresent(int half);   // 0 = left 0x5A, 1 = right 0x5B
#endif

// Logs every raw scancode and the code it maps to, until switched off again.
//
// Which physical key produced a given code is not always obvious — the M9's Back
// button and its keyboard Backspace both used to look like KEY_BACKSPACE from
// the outside, and a board's controller revision can move a scancode. This turns
// the guess into a reading. Off by default; toggled by the "keys" serial command.
void     keyboardSetKeyTrace(bool on);
bool     keyboardKeyTrace();

char     keyboardHeldKey();
uint32_t keyboardHeldMs();

// Drains the keyboard controller without returning anything, for callers that
// are already blocked on work long enough for the controller's own FIFO to
// overflow. The e-paper build calls this throughout a panel refresh, which
// parks loop() for the better part of a second. Paces itself internally and
// sleeps 1 ms on the passes it skips, so it is safe to call from a spin loop.
void     keyboardServiceDuringBlockingWork();

// Keys read off the controller but not yet handed to the UI, and when the
// controller last reported a press (0 if it never has). Together they say
// whether a typing burst is still in flight.
uint8_t  keyboardPendingKeys();
uint32_t keyboardLastKeyMs();

#if defined(DEVICE_TDECK)
// Sets the T-Deck keyboard backlight (0 = off, 255 = full). The LEDs belong to
// the keyboard's own ESP32-C3, so this is an I2C command to it rather than a
// pin: LILYGO_KB_BRIGHTNESS_CMD, supported by keyboard firmware from 2024-12-25
// on. Older controllers ignore the write, which makes the call a no-op rather
// than an error. There is no way to read the level back — the C3 answers reads
// with key data only — so callers own whatever they last set.
void tdeckKeyboardSetBacklight(uint8_t duty);
#endif

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
