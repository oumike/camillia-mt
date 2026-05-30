#include <Arduino.h>
#include "keyboard.h"

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#include <M5Cardputer.h>

static constexpr char CARDPUTER_HID_ENTER = 0x28;
static constexpr char CARDPUTER_HID_ESCAPE = 0x29;
static constexpr char CARDPUTER_HID_BACKSPACE = 0x2A;
static constexpr char CARDPUTER_HID_DELETE = 0x4C;

static char normalizeCardputerKey(char key) {
    uint8_t raw = (uint8_t)key;
    if (raw == (uint8_t)CARDPUTER_HID_ENTER || raw == 0x0D || raw == 0x0A) return KEY_ENTER;
    if (raw == (uint8_t)CARDPUTER_HID_ESCAPE || raw == 0x1B) return KEY_ESCAPE;
    if (raw == (uint8_t)CARDPUTER_HID_BACKSPACE || raw == (uint8_t)CARDPUTER_HID_DELETE
        || raw == 0x08 || raw == 0x7F) return KEY_BACKSPACE;
    return key;
}

void cardputerSpeakerSetVolume(uint8_t volume) {
    M5Cardputer.Speaker.setVolume(volume);
}

bool cardputerSpeakerTone(float frequency, uint32_t duration, int channel, bool stopCurrent) {
    return M5Cardputer.Speaker.tone(frequency, duration, channel, stopCurrent);
}
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

uint8_t sTloraModifier = 0;
uint32_t sTloraModifierSetMs = 0;
bool sTloraBackspaceDown = false;
bool sTloraBackspaceHoldSent = false;
uint32_t sTloraBackspaceDownMs = 0;

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

        if (!pressed) continue;

        char mapped = tloraTranslateKey(keyNum);
        if (mapped != KEY_NONE) {
            return mapped;
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
    // TLora pager uses a quadrature wheel (A/B). Decode A/B state in
    // readTrackball() to avoid direction jitter from edge-only interrupts.
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
    // Quadrature decode table for transitions: prevAB<<2 | currAB.
    // Returns -1/0/+1 movement steps.
    static const int8_t qdec[16] = {
         0, -1, +1,  0,
        +1,  0,  0, -1,
        -1,  0,  0, +1,
         0, +1, -1,  0
    };
    static uint8_t prevAB = 0xFF;
    static int8_t accum = 0;
    static unsigned long lastMoveEmitMs = 0;
    static bool pendingClick = false;
    static unsigned long pendingClickMs = 0;
    static unsigned long lastClickSeenMs = 0;
    static unsigned long lastClickEmitMs = 0;
    const unsigned long moveDebounceMs = 20;
    const unsigned long clickQuietMs = 70;
    const unsigned long clickExpireMs = 800;
    const unsigned long clickEdgeDebounceMs = 80;
    const unsigned long clickEmitMinIntervalMs = 280;

    bool clk = false;
    noInterrupts();
    clk = _click;
    _click = false;
    interrupts();

    if (clk && (now - lastClickSeenMs >= clickEdgeDebounceMs)) {
        pendingClick = true;
        pendingClickMs = now;
        lastClickSeenMs = now;
    }

    uint8_t a = (TBALL_UP >= 0 && digitalRead(TBALL_UP) == LOW) ? 1 : 0;
    uint8_t b = (TBALL_DOWN >= 0 && digitalRead(TBALL_DOWN) == LOW) ? 1 : 0;
    uint8_t currAB = (uint8_t)((a << 1) | b);

    if (prevAB == 0xFF) {
        prevAB = currAB;
    } else if (currAB != prevAB) {
        int8_t delta = qdec[(prevAB << 2) | currAB];
        prevAB = currAB;
        if (delta != 0) {
            accum += delta;
            _lastScrollMs = now;
        }
    }

    // Prioritize click delivery once wheel motion settles so entering/exiting
    // channel-scroll mode doesn't lose the first notch to channel navigation.
    if (pendingClick && (now - _lastScrollMs >= clickQuietMs)) {
        pendingClick = false;
        if (now - lastClickEmitMs < clickEmitMinIntervalMs) {
            return KEY_NONE;
        }
        lastClickEmitMs = now;
        return KEY_ROLLER;
    }

    if (now - lastMoveEmitMs >= moveDebounceMs) {
        // One notch is four state transitions on typical rotary encoders.
        if (accum >= 4) {
            accum = 0;
            lastMoveEmitMs = now;
            return KEY_SCROLL_DN;
        }
        if (accum <= -4) {
            accum = 0;
            lastMoveEmitMs = now;
            return KEY_SCROLL_UP;
        }
    }

    // Expire stale pending clicks to avoid ghost toggles.
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
#if (KB_INT >= 0)
    static uint32_t lastIdleProbeMs = 0;
    static uint32_t lastKeyHitMs = 0;
    uint32_t now = millis();
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
            if (now - lastIdleProbeMs < kIdleProbeMs) return KEY_NONE;
            lastIdleProbeMs = now;
        }
#else
        if (now - lastIdleProbeMs < kIdleProbeMs) return KEY_NONE;
        lastIdleProbeMs = now;
#endif
    }
#endif
    uint8_t count = Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
    if (!Wire.available()) return KEY_NONE;
    uint8_t raw = Wire.read();
    if (raw == 0x00 || raw == 0xFF) return KEY_NONE;
#if defined(DEVICE_TDECK)
    lastKeyHitMs = now;
#endif
    return mapKey(raw);
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

    if (enterPressed && !_cardputerEnterDown) {
        enqueueCardputerKey(KEY_ENTER);
        // Treat Enter as a discrete high-priority action so it reaches
        // main-loop handling even if other key state changes occur this tick.
        _cardputerEnterDown = enterPressed;
        return;
    }
    _cardputerEnterDown = enterPressed;

    if (!changed || !pressed) {
        return;
    }

    bool hidDeleteQueued = false;
    for (uint8_t hidKey : status.hid_keys) {
        if (hidKey == (uint8_t)CARDPUTER_HID_ESCAPE) {
            enqueueCardputerKey(KEY_ESCAPE);
            continue;
        }
        if (hidKey == (uint8_t)CARDPUTER_HID_BACKSPACE || hidKey == (uint8_t)CARDPUTER_HID_DELETE) {
            enqueueCardputerKey(KEY_BACKSPACE);
            hidDeleteQueued = true;
        }
    }

    if (status.tab) enqueueCardputerKey(KEY_TAB);
    if (status.del && !hidDeleteQueued) enqueueCardputerKey(KEY_BACKSPACE);

    bool nodeFocusChordQueued = false;
    for (char key : status.word) {
        if (key == '\r' || key == '\n') {
            continue;
        }

        if (status.alt && (key == 'e' || key == 'E')) {
            if (!nodeFocusChordQueued) {
                enqueueCardputerKey(KEY_NODE_FOCUS);
                nodeFocusChordQueued = true;
            }
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
        case 0x7F: return KEY_BACKSPACE;
        case 0x08: return KEY_BACKSPACE;
        case 0x05: return KEY_NODE_FOCUS;  // ALT+E
        default:   return (char)raw;
    }
}

void IRAM_ATTR TDeckKeyboard::_isrRight() { if (_instance) _instance->_dx++; }
void IRAM_ATTR TDeckKeyboard::_isrLeft()  { if (_instance) _instance->_dx--; }
void IRAM_ATTR TDeckKeyboard::_isrUp()    { if (_instance) _instance->_dy--; }
void IRAM_ATTR TDeckKeyboard::_isrDown()  { if (_instance) _instance->_dy++; }
void IRAM_ATTR TDeckKeyboard::_isrClick() { if (_instance) _instance->_click = true; }
