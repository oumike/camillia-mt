#include <Arduino.h>
#include "keyboard.h"

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#include <M5Cardputer.h>

static constexpr char CARDPUTER_HID_ENTER = 0x28;

static void logCardputerWord(const std::vector<char> &word) {
    Serial.print("[cp-kb] word=");
    if (word.empty()) {
        Serial.print("<empty>");
        return;
    }
    Serial.print('"');
    for (char key : word) {
        if (key == '\r') Serial.print("\\r");
        else if (key == '\n') Serial.print("\\n");
        else if ((uint8_t)key < 0x20 || (uint8_t)key > 0x7E) Serial.printf("\\x%02X", (uint8_t)key);
        else Serial.print(key);
    }
    Serial.print('"');
}

static void logCardputerHidKeys(const std::vector<uint8_t> &hidKeys) {
    Serial.print(" hid=[");
    for (size_t i = 0; i < hidKeys.size(); ++i) {
        if (i) Serial.print(',');
        Serial.printf("0x%02X", hidKeys[i]);
    }
    Serial.print(']');
}
#endif

TDeckKeyboard *TDeckKeyboard::_instance = nullptr;

void TDeckKeyboard::begin() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    M5Cardputer.begin(true);
    return;
#endif

#if HAS_KEYBOARD
    Wire.begin(KB_SDA, KB_SCL, 100000UL);
    Wire.setClock(400000UL);
    delay(50);
    Wire.beginTransmission(KB_ADDR);
    Wire.endTransmission();
    delay(50);
#endif

#if HAS_TRACKBALL
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
#endif
}

char TDeckKeyboard::readTrackball() {
#if defined(DEVICE_CARDPUTER_LORA_HAT) || !HAS_TRACKBALL
    return KEY_NONE;
#else
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
#endif
}

char TDeckKeyboard::readKey() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    pumpCardputerKeys();
    if (_cardputerCount == 0) return KEY_NONE;
    return dequeueCardputerKey();
#elif !HAS_KEYBOARD
    return KEY_NONE;
#else
    // Read immediately; higher-level poll loop already controls cadence.
    // A local gate here prevents draining buffered bursts and drops keys.
    Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
    if (!Wire.available()) return KEY_NONE;
    uint8_t raw = Wire.read();
    if (raw == 0x00 || raw == 0xFF) return KEY_NONE;
    return mapKey(raw);
#endif
}

#if defined(DEVICE_CARDPUTER_LORA_HAT)
void TDeckKeyboard::enqueueCardputerKey(char key) {
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
    return key;
}

void TDeckKeyboard::pumpCardputerKeys() {
    M5Cardputer.update();
    auto &status = M5Cardputer.Keyboard.keysState();
    bool changed = M5Cardputer.Keyboard.isChange();
    bool pressed = M5Cardputer.Keyboard.isPressed();

    bool enterPressed = M5Cardputer.BtnA.isPressed() || status.enter;
    bool hidEnter = false;
    for (uint8_t hidKey : status.hid_keys) {
        if (hidKey == (uint8_t)CARDPUTER_HID_ENTER) {
            enterPressed = true;
            hidEnter = true;
            break;
        }
    }
    bool wordEnter = false;
    if (!enterPressed) {
        for (char key : status.word) {
            if (key == '\r' || key == '\n') {
                enterPressed = true;
                wordEnter = true;
                break;
            }
        }
    }
    if (!wordEnter) {
        for (char key : status.word) {
            if (key == '\r' || key == '\n') {
                wordEnter = true;
                break;
            }
        }
    }

    if (changed) {
        Serial.printf("[cp-kb] change pressed=%d btnA=%d enter=%d tab=%d del=%d fn=%d shift=%d ctrl=%d alt=%d opt=%d size=%u ",
                      pressed ? 1 : 0,
                      M5Cardputer.BtnA.isPressed() ? 1 : 0,
                      status.enter ? 1 : 0,
                      status.tab ? 1 : 0,
                      status.del ? 1 : 0,
                      status.fn ? 1 : 0,
                      status.shift ? 1 : 0,
                      status.ctrl ? 1 : 0,
                      status.alt ? 1 : 0,
                      status.opt ? 1 : 0,
                      (unsigned)status.word.size());
        logCardputerWord(status.word);
        logCardputerHidKeys(status.hid_keys);
        Serial.println();
    }

    if (enterPressed && !_cardputerEnterDown) {
        Serial.printf("[cp-kb] enter rising btnA=%d state.enter=%d hidEnter=%d wordEnter=%d\n",
                      M5Cardputer.BtnA.isPressed() ? 1 : 0,
                      status.enter ? 1 : 0,
                      hidEnter ? 1 : 0,
                      wordEnter ? 1 : 0);
        enqueueCardputerKey(KEY_ENTER);
    }
    _cardputerEnterDown = enterPressed;

    if (!changed || !pressed) {
        return;
    }

    if (status.tab) enqueueCardputerKey(KEY_TAB);
    if (status.del) enqueueCardputerKey(KEY_BACKSPACE);

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
