#include <Arduino.h>
#include "keyboard.h"

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#ifdef KEY_TAB
#undef KEY_TAB
#endif
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#ifdef KEY_ESCAPE
#undef KEY_ESCAPE
#endif
#include <M5Cardputer.h>

#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#ifdef KEY_TAB
#undef KEY_TAB
#endif
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#ifdef KEY_ESCAPE
#undef KEY_ESCAPE
#endif
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_ESCAPE      0x1B

static constexpr char CARDPUTER_HID_ENTER = 0x28;
static constexpr char CARDPUTER_HID_ESCAPE = 0x29;
static constexpr char CARDPUTER_HID_BACKSPACE = 0x2A;
static constexpr char CARDPUTER_HID_DELETE = 0x4C;
static constexpr char CARDPUTER_HID_ARROW_LEFT = 0x50;
static constexpr char CARDPUTER_HID_ARROW_DOWN = 0x51;
static constexpr char CARDPUTER_HID_ARROW_UP = 0x52;
static constexpr char CARDPUTER_HID_ARROW_RIGHT = 0x4F;

static char normalizeCardputerKey(char key) {
    uint8_t raw = (uint8_t)key;
    if (raw == (uint8_t)CARDPUTER_HID_ENTER || raw == 0x0D || raw == 0x0A) return KEY_ENTER;
    if (raw == (uint8_t)CARDPUTER_HID_ESCAPE || raw == 0x1B) return KEY_ESCAPE;
    if (raw == (uint8_t)CARDPUTER_HID_BACKSPACE || raw == (uint8_t)CARDPUTER_HID_DELETE
        || raw == 0x08 || raw == 0x7F) return KEY_BACKSPACE;
    if (raw == (uint8_t)CARDPUTER_HID_ARROW_UP) return KEY_SCROLL_UP;
    if (raw == (uint8_t)CARDPUTER_HID_ARROW_DOWN) return KEY_SCROLL_DN;
    if (raw == (uint8_t)CARDPUTER_HID_ARROW_LEFT) return KEY_PREV_CHAN;
    if (raw == (uint8_t)CARDPUTER_HID_ARROW_RIGHT) return KEY_NEXT_CHAN;
    return key;
}

void cardputerSpeakerSetVolume(uint8_t volume) {
    M5Cardputer.Speaker.setVolume(volume);
}

bool cardputerSpeakerTone(float frequency, uint32_t duration, int channel, bool stopCurrent) {
    return M5Cardputer.Speaker.tone(frequency, duration, channel, stopCurrent);
}
#endif

#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
namespace {
char sHeldKeyBestEffort = KEY_NONE;
uint32_t sHeldSinceMsBestEffort = 0;
uint32_t sHeldLastSeenMsBestEffort = 0;
constexpr uint32_t kHeldStaleMsBestEffort = 260;

static inline void clearHeldKeyBestEffort() {
    sHeldKeyBestEffort = KEY_NONE;
    sHeldSinceMsBestEffort = 0;
    sHeldLastSeenMsBestEffort = 0;
}

static inline void noteHeldKeyBestEffort(char key, uint32_t nowMs) {
    if (key == KEY_NONE) return;
    if (key != sHeldKeyBestEffort) {
        sHeldKeyBestEffort = key;
        sHeldSinceMsBestEffort = nowMs;
    }
    sHeldLastSeenMsBestEffort = nowMs;
}

static inline void expireHeldKeyBestEffort(uint32_t nowMs) {
    if (sHeldKeyBestEffort == KEY_NONE || sHeldLastSeenMsBestEffort == 0) return;
    if ((uint32_t)(nowMs - sHeldLastSeenMsBestEffort) > kHeldStaleMsBestEffort) {
        clearHeldKeyBestEffort();
    }
}
} // namespace
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
constexpr uint8_t TLORA_KB_ADDR = 0x34;
constexpr uint8_t TLORA_REG_INT_STAT = 0x02;
constexpr uint8_t TLORA_REG_KEY_LCK_EC = 0x03;
constexpr uint8_t TLORA_REG_KEY_EVENT_A = 0x04;
constexpr uint8_t TLORA_REG_GPIO_INT_EN_1 = 0x1A;
constexpr uint8_t TLORA_REG_GPIO_INT_EN_2 = 0x1B;
constexpr uint8_t TLORA_REG_GPIO_INT_EN_3 = 0x1C;
constexpr uint8_t TLORA_REG_KP_GPIO_1 = 0x1D;
constexpr uint8_t TLORA_REG_KP_GPIO_2 = 0x1E;
constexpr uint8_t TLORA_REG_KP_GPIO_3 = 0x1F;
constexpr uint8_t TLORA_REG_GPI_EM_1 = 0x20;
constexpr uint8_t TLORA_REG_GPI_EM_2 = 0x21;
constexpr uint8_t TLORA_REG_GPI_EM_3 = 0x22;
constexpr uint8_t TLORA_REG_GPIO_DIR_1 = 0x23;
constexpr uint8_t TLORA_REG_GPIO_DIR_2 = 0x24;
constexpr uint8_t TLORA_REG_GPIO_DIR_3 = 0x25;
constexpr uint8_t TLORA_REG_GPIO_INT_LVL_1 = 0x26;
constexpr uint8_t TLORA_REG_GPIO_INT_LVL_2 = 0x27;
constexpr uint8_t TLORA_REG_GPIO_INT_LVL_3 = 0x28;
constexpr uint8_t TLORA_REG_DEBOUNCE_DIS_1 = 0x29;
constexpr uint8_t TLORA_REG_DEBOUNCE_DIS_2 = 0x2A;
constexpr uint8_t TLORA_REG_DEBOUNCE_DIS_3 = 0x2B;
constexpr uint16_t TLORA_MOD_TIMEOUT_MS = 1500;
constexpr uint16_t TLORA_BKSP_HOLD_MS = 3000;
constexpr uint8_t TLORA_KEYNUM_BACKSPACE = 30;
constexpr uint8_t TLORA_MOD_SHIFT = 0x01;
constexpr uint8_t TLORA_MOD_SYM = 0x02;
constexpr int8_t kRotaryDelta[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0,
};
constexpr int8_t kRotaryDetentTransitions = 4;
constexpr int16_t kRotaryQueueMax = 24;

uint8_t sTloraModifier = 0;
uint32_t sTloraModifierSetMs = 0;
bool sTloraBackspaceDown = false;
bool sTloraBackspaceHoldSent = false;
uint32_t sTloraBackspaceDownMs = 0;
// Currently-held key and when it went down, for keyboardHeldKey() below.
char sTloraHeldKey = KEY_NONE;
uint32_t sTloraHeldSinceMs = 0;

static inline uint8_t tloraReadRotaryAB() {
    uint8_t a = (TBALL_UP >= 0 && digitalRead(TBALL_UP) == LOW) ? 1 : 0;
    uint8_t b = (TBALL_DOWN >= 0 && digitalRead(TBALL_DOWN) == LOW) ? 1 : 0;
    return (uint8_t)((b << 1) | a);
}

const char kTloraTapMap[31][3] = {
    {'q', 'Q', '1'},
    {'w', 'W', '2'},
    {'e', 'E', '3'},
    {'r', 'R', '4'},
    {'t', 'T', '5'},
    {'y', 'Y', '6'},
    {'u', 'U', '7'},
    {'i', 'I', '8'},
    {'o', 'O', '9'},
    {'p', 'P', '0'},
    {'a', 'A', '*'},
    {'s', 'S', '/'},
    {'d', 'D', '+'},
    {'f', 'F', '-'},
    {'g', 'G', '='},
    {'h', 'H', ':'},
    {'j', 'J', '\''},
    {'k', 'K', '"'},
    {'l', 'L', '@'},
    {KEY_ENTER, KEY_NONE, KEY_TAB},
    {KEY_NONE, KEY_NONE, KEY_NONE},
    {'z', 'Z', '_'},
    {'x', 'X', '$'},
    {'c', 'C', ';'},
    {'v', 'V', '?'},
    {'b', 'B', '!'},
    {'n', 'N', ','},
    {'m', 'M', '.'},
    {KEY_NONE, KEY_NONE, KEY_NONE},
    {KEY_BACKSPACE, KEY_NONE, KEY_ESCAPE},
    {' ', KEY_NONE, KEY_NONE},
};

void tloraWriteReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(TLORA_KB_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t tloraReadReg(uint8_t reg) {
    Wire.beginTransmission(TLORA_KB_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)TLORA_KB_ADDR, (uint8_t)1);
    if (!Wire.available()) return 0;
    return Wire.read();
}

void tloraResetKeyboardController() {
    // Mirror the TCA8418 matrix setup used by Meshtastic's tlora-pager profile.
    tloraWriteReg(TLORA_REG_GPIO_DIR_1, 0x00);
    tloraWriteReg(TLORA_REG_GPIO_DIR_2, 0x00);
    tloraWriteReg(TLORA_REG_GPIO_DIR_3, 0x00);

    tloraWriteReg(TLORA_REG_GPI_EM_1, 0xFF);
    tloraWriteReg(TLORA_REG_GPI_EM_2, 0xFF);
    tloraWriteReg(TLORA_REG_GPI_EM_3, 0xFF);

    tloraWriteReg(TLORA_REG_GPIO_INT_LVL_1, 0x00);
    tloraWriteReg(TLORA_REG_GPIO_INT_LVL_2, 0x00);
    tloraWriteReg(TLORA_REG_GPIO_INT_LVL_3, 0x00);

    tloraWriteReg(TLORA_REG_GPIO_INT_EN_1, 0xFF);
    tloraWriteReg(TLORA_REG_GPIO_INT_EN_2, 0xFF);
    tloraWriteReg(TLORA_REG_GPIO_INT_EN_3, 0xFF);

    // 4 rows, 10 columns.
    tloraWriteReg(TLORA_REG_KP_GPIO_1, 0x0F);
    tloraWriteReg(TLORA_REG_KP_GPIO_2, 0xFF);
    tloraWriteReg(TLORA_REG_KP_GPIO_3, 0x03);

    tloraWriteReg(TLORA_REG_DEBOUNCE_DIS_1, 0x00);
    tloraWriteReg(TLORA_REG_DEBOUNCE_DIS_2, 0x00);
    tloraWriteReg(TLORA_REG_DEBOUNCE_DIS_3, 0x00);

    while (tloraReadReg(TLORA_REG_KEY_EVENT_A) != 0) {
    }
    tloraWriteReg(TLORA_REG_INT_STAT, 0x03);
}

char tloraTranslateKey(uint8_t keyNum) {
    uint32_t now = millis();
    if (sTloraModifier && (now - sTloraModifierSetMs > TLORA_MOD_TIMEOUT_MS)) {
        sTloraModifier = 0;
    }

    // Key numbers are 1-based from TCA8418 event FIFO.
    if (keyNum == 21) {
        sTloraModifier ^= TLORA_MOD_SYM;
        sTloraModifierSetMs = now;
        return KEY_NONE;
    }
    if (keyNum == 29) {
        sTloraModifier ^= TLORA_MOD_SHIFT;
        sTloraModifierSetMs = now;
        return KEY_NONE;
    }

    if (keyNum < 1 || keyNum > 31) return KEY_NONE;
    uint8_t idx = keyNum - 1;

    uint8_t mode = 0;
    if (sTloraModifier & TLORA_MOD_SYM) mode = 2;
    else if (sTloraModifier & TLORA_MOD_SHIFT) mode = 1;

    char mapped = kTloraTapMap[idx][mode];
    if (mapped == KEY_NONE) mapped = kTloraTapMap[idx][0];

    // Consume one-shot modifier state after non-modifier keypress.
    sTloraModifier = 0;
    return mapped;
}

char tloraReadMappedKey() {
    uint32_t now = millis();
    if (sTloraBackspaceDown && !sTloraBackspaceHoldSent
        && (now - sTloraBackspaceDownMs >= TLORA_BKSP_HOLD_MS)) {
        sTloraBackspaceHoldSent = true;
        return KEY_BACKSPACE_HOLD;
    }

    uint8_t count = tloraReadReg(TLORA_REG_KEY_LCK_EC) & 0x0F;
    if (count == 0) return KEY_NONE;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t ev = tloraReadReg(TLORA_REG_KEY_EVENT_A + i);
        bool pressed = (ev & 0x80) != 0;
        uint8_t keyNum = ev & 0x7F;

        if (keyNum == TLORA_KEYNUM_BACKSPACE) {
            if (pressed) {
                if (!sTloraBackspaceDown) {
                    sTloraBackspaceDown = true;
                    sTloraBackspaceHoldSent = false;
                    sTloraBackspaceDownMs = now;
                }

                // Pager shortcut: Symbol + Backspace closes compose/panels
                // using the same path as long-hold backspace, but instantly.
                if (sTloraModifier & TLORA_MOD_SYM) {
                    sTloraModifier = 0;
                    sTloraBackspaceHoldSent = true;
                    return KEY_BACKSPACE_HOLD;
                }
            } else {
                sTloraBackspaceDown = false;
                sTloraBackspaceHoldSent = false;
            }
        }

        // Track whatever is currently held so callers can offer hold-to-repeat.
        // The controller reports one press event and then nothing until release,
        // so a held key is invisible without this; the backspace-hold path above
        // already relies on the same press/release pairing.
        char heldMapped = tloraTranslateKey(keyNum);
        if (!pressed) {
            if (heldMapped != KEY_NONE && heldMapped == sTloraHeldKey) {
                sTloraHeldKey = KEY_NONE;
                sTloraHeldSinceMs = 0;
            }
            continue;
        }
        if (heldMapped != KEY_NONE && heldMapped != sTloraHeldKey) {
            sTloraHeldKey = heldMapped;
            sTloraHeldSinceMs = now;
        }

        if (heldMapped != KEY_NONE) {
            return heldMapped;
        }
    }

    return KEY_NONE;
}
} // namespace
#endif

TDeckKeyboard *TDeckKeyboard::_instance = nullptr;

void TDeckKeyboard::begin() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    M5Cardputer.begin(true);
    return;
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    Wire.begin(KB_SDA, KB_SCL, 100000UL);
    Wire.setClock(400000UL);
    delay(30);
#if (KB_INT >= 0)
    pinMode(KB_INT, INPUT_PULLUP);
#endif
#if defined(KB_BL) && (KB_BL >= 0)
    pinMode(KB_BL, OUTPUT);
    digitalWrite(KB_BL, HIGH);
#endif
    tloraResetKeyboardController();
#elif HAS_KEYBOARD
    Wire.begin(KB_SDA, KB_SCL, 100000UL);
    Wire.setClock(400000UL);
    delay(50);
    Wire.beginTransmission(KB_ADDR);
    Wire.endTransmission();
    delay(50);
#if (KB_INT >= 0)
    pinMode(KB_INT, INPUT_PULLUP);
#endif
#endif

#if HAS_TRACKBALL
    if (TBALL_UP >= 0) pinMode(TBALL_UP, INPUT_PULLUP);
    if (TBALL_DOWN >= 0) pinMode(TBALL_DOWN, INPUT_PULLUP);
    if (TBALL_LEFT >= 0) pinMode(TBALL_LEFT, INPUT_PULLUP);
    if (TBALL_RIGHT >= 0) pinMode(TBALL_RIGHT, INPUT_PULLUP);
    if (TBALL_CLICK >= 0) pinMode(TBALL_CLICK, INPUT_PULLUP);

    _instance = this;
#if defined(DEVICE_TLORA_PAGER_TFT)
    // Use CHANGE interrupts on A/B so every quadrature edge is captured even
    // under UI load. readTrackball() drains queued detent steps.
    noInterrupts();
    _rotaryPrevAB = tloraReadRotaryAB();
    _rotaryAccum = 0;
    _rotaryQueued = 0;
    _click = false;
    interrupts();

    if (TBALL_UP >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_UP), _isrPagerRotary, CHANGE);
    if (TBALL_DOWN >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_DOWN), _isrPagerRotary, CHANGE);
    if (TBALL_CLICK >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_CLICK), _isrClick, FALLING);
#else
    // Physical mapping (empirically confirmed):
    //   roll right → TBALL_DOWN, roll left  → TBALL_LEFT
    //   roll up    → TBALL_RIGHT, roll down → TBALL_UP
    if (TBALL_DOWN >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_DOWN), _isrRight, FALLING);
    if (TBALL_LEFT >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_LEFT), _isrLeft, FALLING);
    if (TBALL_RIGHT >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_RIGHT), _isrDown, FALLING);
    if (TBALL_UP >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_UP), _isrUp, FALLING);
    if (TBALL_CLICK >= 0) attachInterrupt(digitalPinToInterrupt(TBALL_CLICK), _isrClick, FALLING);
#endif
#endif
}

char TDeckKeyboard::readTrackball() {
#if defined(DEVICE_CARDPUTER_LORA_HAT) || !HAS_TRACKBALL
    return KEY_NONE;
#else
    unsigned long now = millis();

#if defined(DEVICE_TLORA_PAGER_TFT)
    static bool pendingClick = false;
    static unsigned long pendingClickMs = 0;
    constexpr unsigned long clickQuietMs = 90;
    constexpr unsigned long clickExpireMs = 500;

    bool clk = false;
    int16_t queued = 0;
    noInterrupts();
    clk = _click;
    _click = false;
    queued = _rotaryQueued;
    if (queued > 0) {
        _rotaryQueued = queued - 1;
    } else if (queued < 0) {
        _rotaryQueued = queued + 1;
    }
    interrupts();

    if (queued > 0) {
        _lastScrollMs = now;
        return KEY_SCROLL_UP;
    }
    if (queued < 0) {
        _lastScrollMs = now;
        return KEY_SCROLL_DN;
    }

    if (clk) {
        pendingClick = true;
        pendingClickMs = now;
    }
    if (pendingClick && (now - _lastScrollMs >= clickQuietMs)) {
        pendingClick = false;
        return KEY_ROLLER;
    }
    if (pendingClick && (now - pendingClickMs > clickExpireMs)) {
        pendingClick = false;
    }
    return KEY_NONE;
#else
    static unsigned long lastMoveEmitMs = 0;
    static bool pendingClick = false;
    static unsigned long pendingClickMs = 0;
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

    if (clk) {
        pendingClick = true;
        pendingClickMs = now;
    }

    // Suppress accidental clicks during motion, but defer rather than drop.
    if (pendingClick && (now - _lastScrollMs >= 160)) {
        pendingClick = false;
        return KEY_ROLLER;
    }
    if (pendingClick && (now - pendingClickMs > 500)) {
        pendingClick = false;
    }

    // Ignore overly-frequent movement pulses (trackball bounce/noise).
    if ((dy != 0 || dx != 0) && (now - lastMoveEmitMs < moveDebounceMs)) return KEY_NONE;

    if (dy < 0) { lastMoveEmitMs = now; return KEY_SCROLL_UP; }
    if (dy > 0) { lastMoveEmitMs = now; return KEY_SCROLL_DN; }

#if defined(DEVICE_TDECK)
    // T-Deck horizontal trackball motion selects previous/next channel.
    if (dx < 0) { lastMoveEmitMs = now; return KEY_PREV_CHAN; }
    if (dx > 0) { lastMoveEmitMs = now; return KEY_NEXT_CHAN; }
#endif

    if (dx != 0) lastMoveEmitMs = now;

    return KEY_NONE;
#endif
#endif
}

char TDeckKeyboard::readKey() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    pumpCardputerKeys();
    if (_cardputerCount == 0) return KEY_NONE;
    return dequeueCardputerKey();
#elif defined(DEVICE_TLORA_PAGER_TFT)
    static uint32_t lastIdleProbeMs = 0;
    uint32_t now = millis();
#if (KB_INT >= 0)
    bool irqActive = (digitalRead(KB_INT) == LOW);
    if (!irqActive) {
        if (now - lastIdleProbeMs < 120) return KEY_NONE;
        lastIdleProbeMs = now;
    }
#endif
    return tloraReadMappedKey();
#elif !HAS_KEYBOARD
    return KEY_NONE;
#else
    // Read immediately; higher-level poll loop already controls cadence.
    // A local gate here prevents draining buffered bursts and drops keys.
    uint32_t now = millis();
#if (KB_INT >= 0)
    static uint32_t lastIdleProbeMs = 0;
    static uint32_t lastKeyHitMs = 0;
    bool irqActive = (digitalRead(KB_INT) == LOW);
    // T-Deck can miss very short taps if we only probe every 250ms when the
    // IRQ line is not asserted; keep a faster fallback cadence there.
#if defined(DEVICE_TDECK)
    static constexpr uint32_t kIdleProbeMs = 12;
    static constexpr uint32_t kBurstWindowMs = 28;
#else
    static constexpr uint32_t kIdleProbeMs = 250;
#endif
    if (!irqActive) {
#if defined(DEVICE_TDECK)
        bool inBurstDrain = (now - lastKeyHitMs) < kBurstWindowMs;
        if (!inBurstDrain) {
            if (now - lastIdleProbeMs < kIdleProbeMs) {
#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
                expireHeldKeyBestEffort(now);
#endif
                return KEY_NONE;
            }
            lastIdleProbeMs = now;
        }
#else
        if (now - lastIdleProbeMs < kIdleProbeMs) {
#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
            expireHeldKeyBestEffort(now);
#endif
            return KEY_NONE;
        }
        lastIdleProbeMs = now;
#endif
    }
#endif
    uint8_t count = Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
    if (!Wire.available()) {
    #if defined(DEVICE_TDECK) && (KB_INT >= 0)
        if (irqActive) {
            // While the keyboard IRQ stays asserted, preserve the current held
            // key even if this poll doesn't return a byte yet.
            return KEY_NONE;
        }
    #endif
#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
        expireHeldKeyBestEffort(now);
#endif
        return KEY_NONE;
    }
    uint8_t raw = Wire.read();
    if (raw == 0x00 || raw == 0xFF) {
    #if defined(DEVICE_TDECK) && (KB_INT >= 0)
        if (irqActive) {
            // Same as above: no new byte yet, but key hardware still signals
            // pending activity, so don't drop held-key timing.
            return KEY_NONE;
        }
    #endif
#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
        expireHeldKeyBestEffort(now);
#endif
        return KEY_NONE;
    }
#if defined(DEVICE_TDECK)
    lastKeyHitMs = now;
#endif
    char mapped = mapKey(raw);
#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
    noteHeldKeyBestEffort(mapped, now);
#endif
    return mapped;
#endif
}

#if defined(DEVICE_CARDPUTER_LORA_HAT)
void TDeckKeyboard::enqueueCardputerKey(char key) {
    key = normalizeCardputerKey(key);
    if (key == KEY_NONE) return;
    if (_cardputerCount >= CARDPUTER_QUEUE_SIZE) {
        _cardputerTail = (uint8_t)((_cardputerTail + 1) % CARDPUTER_QUEUE_SIZE);
        _cardputerCount--;
    }
    _cardputerQueue[_cardputerHead] = key;
    _cardputerHead = (uint8_t)((_cardputerHead + 1) % CARDPUTER_QUEUE_SIZE);
    _cardputerCount++;
}

char TDeckKeyboard::dequeueCardputerKey() {
    if (_cardputerCount == 0) return KEY_NONE;
    char key = _cardputerQueue[_cardputerTail];
    _cardputerTail = (uint8_t)((_cardputerTail + 1) % CARDPUTER_QUEUE_SIZE);
    _cardputerCount--;
    return normalizeCardputerKey(key);
}

void TDeckKeyboard::pumpCardputerKeys() {
    M5Cardputer.update();
    auto &status = M5Cardputer.Keyboard.keysState();
    bool changed = M5Cardputer.Keyboard.isChange();
    bool pressed = M5Cardputer.Keyboard.isPressed();
    const uint32_t now = millis();

    if (status.fn) {
        _cardputerFnSeenMs = now;
    }
    const bool fnActiveForEnter = status.fn || ((uint32_t)(now - _cardputerFnSeenMs) <= 180U);

    bool enterPressed = M5Cardputer.BtnA.isPressed() || status.enter;
    for (uint8_t hidKey : status.hid_keys) {
        if (hidKey == (uint8_t)CARDPUTER_HID_ENTER) {
            enterPressed = true;
            break;
        }
    }
    if (!enterPressed) {
        for (char key : status.word) {
            if (key == '\r' || key == '\n') {
                enterPressed = true;
                break;
            }
        }
    }

#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
    char heldCandidate = KEY_NONE;
    if (enterPressed) {
        heldCandidate = fnActiveForEnter ? KEY_FN_ENTER : KEY_ENTER;
    }
    if (heldCandidate == KEY_NONE) {
        for (uint8_t hidKey : status.hid_keys) {
            if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_UP) { heldCandidate = KEY_SCROLL_UP; break; }
            if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_DOWN) { heldCandidate = KEY_SCROLL_DN; break; }
            if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_LEFT) { heldCandidate = KEY_PREV_CHAN; break; }
            if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_RIGHT) { heldCandidate = KEY_NEXT_CHAN; break; }
            if (hidKey == (uint8_t)CARDPUTER_HID_BACKSPACE || hidKey == (uint8_t)CARDPUTER_HID_DELETE) {
                heldCandidate = status.fn ? KEY_BACKSPACE_HOLD : KEY_BACKSPACE;
                break;
            }
            if (hidKey == (uint8_t)CARDPUTER_HID_ESCAPE) { heldCandidate = KEY_ESCAPE; break; }
        }
    }
    if (heldCandidate == KEY_NONE) {
        for (char key : status.word) {
            if (key == '\r' || key == '\n') continue;
            if (status.fn) {
                if (key == ';') { heldCandidate = KEY_SCROLL_UP; break; }
                if (key == '.') { heldCandidate = KEY_SCROLL_DN; break; }
                if (key == ',') { heldCandidate = KEY_PREV_CHAN; break; }
                if (key == '/') { heldCandidate = KEY_NEXT_CHAN; break; }
            }
            heldCandidate = normalizeCardputerKey(key);
            break;
        }
    }
    if (heldCandidate != KEY_NONE) noteHeldKeyBestEffort(heldCandidate, now);
    else clearHeldKeyBestEffort();
#endif

    if (enterPressed && !_cardputerEnterDown) {
        enqueueCardputerKey(fnActiveForEnter ? KEY_FN_ENTER : KEY_ENTER);
        // Treat Enter as a discrete high-priority action so it reaches
        // main-loop handling even if other key state changes occur this tick.
        _cardputerEnterDown = enterPressed;
        return;
    }
    _cardputerEnterDown = enterPressed;

    if (!changed || !pressed) {
        return;
    }

    // Cardputer delete is Fn+Backspace. Surface it as KEY_BACKSPACE_HOLD so
    // UI code can bind delete behavior without hijacking normal backspace.
    if (status.fn && status.del) {
        enqueueCardputerKey(KEY_BACKSPACE_HOLD);
        return;
    }

    bool hidDeleteQueued = false;
    for (uint8_t hidKey : status.hid_keys) {
        if (hidKey == (uint8_t)CARDPUTER_HID_ESCAPE) {
            enqueueCardputerKey(KEY_ESCAPE);
            continue;
        }
        if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_UP) {
            enqueueCardputerKey(KEY_SCROLL_UP);
            continue;
        }
        if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_DOWN) {
            enqueueCardputerKey(KEY_SCROLL_DN);
            continue;
        }
        if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_LEFT) {
            enqueueCardputerKey(KEY_PREV_CHAN);
            continue;
        }
        if (hidKey == (uint8_t)CARDPUTER_HID_ARROW_RIGHT) {
            enqueueCardputerKey(KEY_NEXT_CHAN);
            continue;
        }
        if (hidKey == (uint8_t)CARDPUTER_HID_BACKSPACE || hidKey == (uint8_t)CARDPUTER_HID_DELETE) {
            enqueueCardputerKey(status.fn ? KEY_BACKSPACE_HOLD : KEY_BACKSPACE);
            hidDeleteQueued = true;
        }
    }

    if (status.tab) enqueueCardputerKey(KEY_TAB);
    if (status.del && !hidDeleteQueued) {
        enqueueCardputerKey(status.fn ? KEY_BACKSPACE_HOLD : KEY_BACKSPACE);
    }

    for (char key : status.word) {
        if (key == '\r' || key == '\n') {
            continue;
        }

        if (status.fn) {
            switch (key) {
                case ';': enqueueCardputerKey(KEY_SCROLL_UP); continue;
                case '.': enqueueCardputerKey(KEY_SCROLL_DN); continue;
                case ',': enqueueCardputerKey(KEY_PREV_CHAN); continue;
                case '/': enqueueCardputerKey(KEY_NEXT_CHAN); continue;
                default: break;
            }
        }

        enqueueCardputerKey(key);
    }
}
#endif

char TDeckKeyboard::mapKey(uint8_t raw) {
    switch (raw) {
    case 0x0D: return KEY_ENTER;
    case 0x0A: return KEY_ENTER;
        case 0x1B: return KEY_ESCAPE;
#if defined(DEVICE_TDECK)
    case 0x7F: return KEY_BACKSPACE_HOLD;
#else
    case 0x7F: return KEY_BACKSPACE;
#endif
        case 0x08: return KEY_BACKSPACE;
        default:   return (char)raw;
    }
}

void IRAM_ATTR TDeckKeyboard::_isrRight() { if (_instance) _instance->_dx++; }
void IRAM_ATTR TDeckKeyboard::_isrLeft()  { if (_instance) _instance->_dx--; }
void IRAM_ATTR TDeckKeyboard::_isrUp()    { if (_instance) _instance->_dy--; }
void IRAM_ATTR TDeckKeyboard::_isrDown()  { if (_instance) _instance->_dy++; }
#if defined(DEVICE_TLORA_PAGER_TFT)
void IRAM_ATTR TDeckKeyboard::_isrPagerRotary() {
    if (!_instance) return;

    TDeckKeyboard *kb = _instance;
    uint8_t curr = tloraReadRotaryAB();
    uint8_t prev = (uint8_t)(kb->_rotaryPrevAB & 0x03);
    uint8_t idx = (uint8_t)((prev << 2) | curr);
    int8_t delta = kRotaryDelta[idx];

    kb->_rotaryPrevAB = curr;

    if (delta == 0) {
        // Invalid jump/noise; clear partial state to avoid drift.
        if (curr != prev) kb->_rotaryAccum = 0;
        return;
    }

    int8_t accum = (int8_t)(kb->_rotaryAccum + delta);
    if (accum >= kRotaryDetentTransitions) {
        kb->_rotaryAccum = 0;
        if (kb->_rotaryQueued < kRotaryQueueMax) kb->_rotaryQueued++;
        return;
    }
    if (accum <= -kRotaryDetentTransitions) {
        kb->_rotaryAccum = 0;
        if (kb->_rotaryQueued > -kRotaryQueueMax) kb->_rotaryQueued--;
        return;
    }
    kb->_rotaryAccum = accum;
}
#endif
void IRAM_ATTR TDeckKeyboard::_isrClick() { if (_instance) _instance->_click = true; }

// Held-key reporting. Only the Pager's controller gives us press *and* release
// events, so it is the only build that can answer this; elsewhere callers get
// a best-effort heuristic keyed from recent keyboard activity.
char keyboardHeldKey() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    return sTloraHeldKey;
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    return KEY_NONE;
#else
    uint32_t now = millis();
    expireHeldKeyBestEffort(now);
    return sHeldKeyBestEffort;
#endif
}

uint32_t keyboardHeldMs() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (sTloraHeldKey == KEY_NONE || sTloraHeldSinceMs == 0) return 0;
    return millis() - sTloraHeldSinceMs;
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    return 0;
#else
    uint32_t now = millis();
    expireHeldKeyBestEffort(now);
    if (sHeldKeyBestEffort == KEY_NONE || sHeldSinceMsBestEffort == 0) return 0;
    return now - sHeldSinceMsBestEffort;
#endif
}
