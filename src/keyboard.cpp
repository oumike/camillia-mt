#include <Arduino.h>
#include "keyboard.h"
#if HAS_BLE_KEYBOARD
#include "ble_keyboard.h"
#endif

#if defined(M9_KB_NEEDS_USB_PAD_RELEASE)
#include "soc/usb_serial_jtag_reg.h"
// GPIO19/20 are the S3's native USB D-/D+ pads, and the ROM's USB-Serial-JTAG
// peripheral owns them from reset — including a pull-up driven onto D+. The M9
// puts its console on an external UART bridge and reuses GPIO20/21 as the
// keyboard I2C bus, so until that claim is dropped the USB PHY clamps SDA:
// every transaction fails, which Wire reports as endTransmission()==5 on writes
// and i2cRead -1 on reads.
//
// Must run before the first Wire.begin() on these pins.
static void m9ReleaseUsbPads() {
    REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
}
#endif

static TwoWire &keyboardBus() {
#if defined(DEVICE_M9)
    return Wire1;
#else
    return Wire;
#endif
}

// Board-independent: the trace is useful on any keyboard build, and the M9 block
// below is the wrong scope for it.
static bool sKeyTrace = false;
void keyboardSetKeyTrace(bool on) { sKeyTrace = on; }
bool keyboardKeyTrace() { return sKeyTrace; }

#if defined(DEVICE_M9)
static uint8_t sM9KeyReg = KB_REG_KEY;
static uint32_t sM9NextEarlyProbeMs = 0;
// ── d-pad centre hold ────────────────────────────────────────────────────────
// The controller does the timing, not the firmware. Holding the centre makes it
// send 0xA3 (its Enter long-press event) all by itself, which is the whole
// gesture — no press-duration tracking on this side, and nothing that depends on
// KB_INT behaving as a level.
//
// That last part is why the obvious approach does not work here: KB_INT-based
// timing is guarded off entirely on early controllers (keyreg 0x01), which is
// what the units in the field actually report, so anything measuring the press
// on the host never runs on them.
//
// The threshold is therefore the controller's own, not a number chosen here. The
// keyboard exposes it at KB_REG_LONG_PRESS_MS (register 0x03, big-endian ms) if
// it ever needs to be stretched to an exact two seconds — untested on the early
// controller, so it is deliberately not written at init.
static uint32_t sM9LastEnterMs = 0;   // for the one-shot timing log below

static bool m9ReadRegister(TwoWire &bus, uint8_t reg, uint8_t &value) {
    bus.beginTransmission((uint8_t)KB_ADDR);
    bus.write(reg);
    if (bus.endTransmission() != 0) return false;
    if (bus.requestFrom((uint8_t)KB_ADDR, (uint8_t)1) != 1) return false;
    value = bus.read();
    return true;
}

static bool m9IsEarlyController(TwoWire &bus, uint8_t &hw, uint8_t &fw) {
    const bool hwOk = m9ReadRegister(bus, KB_REG_HW_VERSION, hw);
    const bool fwOk = m9ReadRegister(bus, KB_REG_FW_VERSION, fw);
    return hwOk && fwOk && hw == 0x03 && fw == 0x10;
}
#endif

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

#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
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

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
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
#if defined(DEVICE_TDECK_PRO)
constexpr uint8_t TLORA_KEYNUM_BACKSPACE = 11;
constexpr uint8_t TLORA_KEY_COUNT = 35;
#else
constexpr uint8_t TLORA_KEYNUM_BACKSPACE = 30;
constexpr uint8_t TLORA_KEY_COUNT = 31;
#endif
constexpr uint8_t TLORA_MOD_SHIFT = 0x01;
constexpr uint8_t TLORA_MOD_SYM = 0x02;
#if defined(DEVICE_TDECK_PRO)
constexpr uint8_t TDECK_PRO_MOD_ALT = 0x04;
#endif
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
// The key number is tracked alongside the mapped character because the
// character depends on modifier state that the press itself consumes: a
// release re-mapped after the fact would yield a different char and never
// match what the press recorded.
char sTloraHeldKey = KEY_NONE;
uint8_t sTloraHeldKeyNum = 0;
uint32_t sTloraHeldSinceMs = 0;

static inline uint8_t tloraReadRotaryAB() {
    uint8_t a = (TBALL_UP >= 0 && digitalRead(TBALL_UP) == LOW) ? 1 : 0;
    uint8_t b = (TBALL_DOWN >= 0 && digitalRead(TBALL_DOWN) == LOW) ? 1 : 0;
    return (uint8_t)((b << 1) | a);
}

#if defined(DEVICE_TDECK_PRO)
// The Pro has the same visible QWERTY layout as T-Deck, but its TCA8418 sends
// matrix positions. Printable letters intentionally reach the shared shortcut
// layer unchanged; Alt adds explicit directional navigation where available.
const char kTloraTapMap[TLORA_KEY_COUNT][3] = {
    {'p', 'P', '@'},
    {'o', 'O', '+'},
    {'i', 'I', '-'},
    {'u', 'U', '_'},
    {'y', 'Y', ')'},
    {'t', 'T', '('},
    {'r', 'R', '3'},
    {'e', 'E', '2'},
    {'w', 'W', '1'},
    {'q', 'Q', '#'},
    {KEY_BACKSPACE, KEY_BACKSPACE, KEY_BACKSPACE_HOLD},
    {'l', 'L', '"'},
    {'k', 'K', '\''},
    {'j', 'J', ';'},
    {'h', 'H', ':'},
    {'g', 'G', '/'},
    {'f', 'F', '6'},
    {'d', 'D', '5'},
    {'s', 'S', '4'},
    {'a', 'A', '*'},
    {KEY_ENTER, KEY_ENTER, KEY_ENTER},
    {'$', '$', '$'},
    {'m', 'M', '.'},
    {'n', 'N', ','},
    {'b', 'B', '!'},
    {'v', 'V', '?'},
    {'c', 'C', '9'},
    {'x', 'X', '8'},
    {'z', 'Z', '7'},
    {KEY_NONE, KEY_NONE, KEY_NONE},
    {KEY_NONE, KEY_NONE, KEY_NONE},
    {KEY_NONE, KEY_NONE, KEY_NONE},
    {' ', ' ', ' '},
    {KEY_NONE, KEY_NONE, '0'},
    {KEY_NONE, KEY_NONE, KEY_NONE},
};
#else
const char kTloraTapMap[TLORA_KEY_COUNT][3] = {
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
#endif

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

    // Key numbers are 1-based from the TCA8418 event FIFO.
#if defined(DEVICE_TDECK_PRO)
    if (keyNum == 31 || keyNum == 35) {
        sTloraModifier ^= TLORA_MOD_SHIFT;
        sTloraModifierSetMs = now;
        return KEY_NONE;
    }
    if (keyNum == 32) {
        sTloraModifier ^= TLORA_MOD_SYM;
        sTloraModifierSetMs = now;
        return KEY_NONE;
    }
    if (keyNum == 30) {
        sTloraModifier ^= TDECK_PRO_MOD_ALT;
        sTloraModifierSetMs = now;
        return KEY_NONE;
    }
#else
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
#endif

    if (keyNum < 1 || keyNum > TLORA_KEY_COUNT) return KEY_NONE;
    uint8_t idx = keyNum - 1;

#if defined(DEVICE_TDECK_PRO)
    if (sTloraModifier & TDECK_PRO_MOD_ALT) {
        char nav = KEY_NONE;
        if (keyNum == 8) nav = KEY_SCROLL_UP;
        else if (keyNum == 15) nav = KEY_OPEN_HOME;
        else if (keyNum == 17) nav = KEY_NEXT_CHAN;
        else if (keyNum == 19) nav = KEY_PREV_CHAN;
        else if (keyNum == 25) nav = KEY_TOGGLE_KB_BACKLIGHT;
        else if (keyNum == 28) nav = KEY_SCROLL_DN;
        else if (keyNum == 10) nav = KEY_ESCAPE;
        sTloraModifier = 0;
        if (nav != KEY_NONE) return nav;
    }
#endif

    uint8_t mode = 0;
    if (sTloraModifier & TLORA_MOD_SYM) mode = 2;
    else if (sTloraModifier & TLORA_MOD_SHIFT) mode = 1;

    char mapped = kTloraTapMap[idx][mode];
    if (mapped == KEY_NONE) mapped = kTloraTapMap[idx][0];

    // Consume one-shot modifier state after non-modifier keypress.
    sTloraModifier = 0;
    return mapped;
}

// ── Keystroke queue ─────────────────────────────────────────────────────────
// The TCA8418's event FIFO is ten entries deep and every keystroke costs two of
// them — a press and a release — so the controller can hold five keys and no
// more. On the e-paper build that is nowhere near enough: a GDEQ031T10 partial
// update parks loop() for about 700 ms and a full one for 1.1 s, and a fast
// typist overruns the FIFO well inside a single refresh. What does not fit is
// discarded by the controller, which is a letter gone for good — the missing
// characters people see when they type quickly.
//
// So every poll empties the whole hardware FIFO into this queue, and the poll
// itself also runs from inside the refresh's busy wait (see
// keyboardServiceDuringBlockingWork). The controller is then never the thing
// holding a keystroke: what was typed is remembered here, and the UI reads it
// at its own pace while the panel catches up.
constexpr uint8_t TLORA_QUEUE_SIZE = 64;
char     sTloraQueue[TLORA_QUEUE_SIZE] = {0};
uint8_t  sTloraQueueHead = 0;    // next slot to write
uint8_t  sTloraQueueTail = 0;    // next slot to read
uint8_t  sTloraQueueCount = 0;
uint32_t sTloraLastKeyMs = 0;    // when the controller last reported a press

void tloraQueuePush(char key) {
    if (key == KEY_NONE) return;
    // A full queue is sixty-four unread keys, which the UI cannot fall behind by
    // unless something upstream has stalled outright. Drop the newest rather
    // than the oldest: losing the tail of a burst is recoverable by typing the
    // rest, whereas dropping from the head reorders what was already typed.
    if (sTloraQueueCount >= TLORA_QUEUE_SIZE) return;
    sTloraQueue[sTloraQueueHead] = key;
    sTloraQueueHead = (uint8_t)((sTloraQueueHead + 1u) % TLORA_QUEUE_SIZE);
    sTloraQueueCount++;
}

char tloraQueuePop() {
    if (sTloraQueueCount == 0) return KEY_NONE;
    const char key = sTloraQueue[sTloraQueueTail];
    sTloraQueueTail = (uint8_t)((sTloraQueueTail + 1u) % TLORA_QUEUE_SIZE);
    sTloraQueueCount--;
    return key;
}

// Empties the controller's FIFO into the queue above. Touches nothing but I2C,
// the modifier state machine and the queue, which is what makes it safe to call
// from the middle of a panel refresh.
void tloraDrainController() {
    const uint32_t now = millis();

    // Bounded rather than "until empty": a controller answering with a stuck
    // count would otherwise spin here forever. Four sweeps is forty events,
    // far more than one poll can legitimately find waiting.
    for (uint8_t sweep = 0; sweep < 4; sweep++) {
        const uint8_t count = tloraReadReg(TLORA_REG_KEY_LCK_EC) & 0x0F;

        // K_INT latches on the first key event and stays latched until it is
        // written back, so never acknowledging it pinned KB_INT low for the
        // rest of the session and made the line useless as a "something is
        // waiting" signal — every poll had to hit I2C to find out. It is
        // acknowledged here, *before* the events below are popped, so a key
        // struck mid-drain re-asserts it instead of being acknowledged unseen;
        // the next sweep then finds that event and takes it.
        tloraWriteReg(TLORA_REG_INT_STAT, 0x03);
        if (count == 0) break;

        for (uint8_t i = 0; i < count; i++) {
            // KEY_EVENT_A is the FIFO head and pops on read; the registers above
            // it are a passive view of the queue. Reading A+i therefore skips an
            // event per iteration and strands it in the FIFO, which is how a
            // modifier release ends up arriving after the keypress it was meant
            // to modify.
            uint8_t ev = tloraReadReg(TLORA_REG_KEY_EVENT_A);
            if (ev == 0) break;
            bool pressed = (ev & 0x80) != 0;
            uint8_t keyNum = ev & 0x7F;

            // Every press counts as typing activity, modifiers included, so the
            // e-paper build can tell a burst is still in flight even when the
            // key that just arrived maps to nothing on its own.
            if (pressed) sTloraLastKeyMs = now;

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
                        tloraQueuePush(KEY_BACKSPACE_HOLD);
                        continue;
                    }
                } else {
                    sTloraBackspaceDown = false;
                    sTloraBackspaceHoldSent = false;
                }
            }

            // Releases must never reach tloraTranslateKey(): it drives the
            // one-shot modifier state machine, so re-mapping a release toggles
            // shift/sym back on (or clears a modifier the press already
            // consumed) and the modifier bleeds into a second keypress.
            if (!pressed) {
                if (keyNum == sTloraHeldKeyNum) {
                    sTloraHeldKey = KEY_NONE;
                    sTloraHeldKeyNum = 0;
                    sTloraHeldSinceMs = 0;
                }
                continue;
            }

            char mapped = tloraTranslateKey(keyNum);

            // Track whatever is currently held so callers can offer
            // hold-to-repeat. The controller reports one press event and then
            // nothing until release, so a held key is invisible without this;
            // the backspace-hold path above already relies on the same
            // press/release pairing.
            if (mapped != KEY_NONE && keyNum != sTloraHeldKeyNum) {
                sTloraHeldKey = mapped;
                sTloraHeldKeyNum = keyNum;
                sTloraHeldSinceMs = now;
            }

            tloraQueuePush(mapped);
        }
    }
}

// Rate-gated controller poll, shared by readKey() and the panel busy wait so
// both entry points pace the controller identically. KB_INT is asserted
// whenever the FIFO holds something, which makes the timed probe below a
// backstop for the one narrow window the interrupt cannot cover: an event that
// lands between a drain's last read and its acknowledgement.
void tloraPollController(uint32_t now) {
    static uint32_t lastPollMs = 0;
#if (KB_INT >= 0)
    if (digitalRead(KB_INT) != KB_INT_ACTIVE_LEVEL
        && (uint32_t)(now - lastPollMs) < 120) {
        return;
    }
#endif
    lastPollMs = now;
    tloraDrainController();
}

char tloraReadMappedKey() {
    uint32_t now = millis();
    if (sTloraBackspaceDown && !sTloraBackspaceHoldSent
        && (now - sTloraBackspaceDownMs >= TLORA_BKSP_HOLD_MS)) {
        sTloraBackspaceHoldSent = true;
        return KEY_BACKSPACE_HOLD;
    }

    // Only talk to the controller once the queue has run dry. pumpKeyboardInput()
    // calls this up to eight times a pass, and with the queue fed both from here
    // and from the busy wait, an I2C round trip per call would be pure overhead
    // for keys already in hand.
    if (sTloraQueueCount == 0) tloraPollController(now);
    return tloraQueuePop();
}
} // namespace
#endif

TDeckKeyboard *TDeckKeyboard::_instance = nullptr;

#if defined(DEVICE_MESH_DECK)
// ── Attaky Mesh Deck keyboard ───────────────────────────────────────────────
// Two AW9523 expanders, one per half, each scanning its own 5x5 matrix: rows
// P10..P14 (port 1 bits 0..4) are pulled low one at a time and columns
// P00..P04 (port 0 bits 0..4) are read back. A key reads pressed when its
// row x column intersection pulls a column low.
//
// Only one row is an output at a time; the others are left as inputs rather
// than driven high. Two keys held in the same column would otherwise short a
// driven-high row against a driven-low one through the matrix.
#include "aw9523.h"

static Aw9523  s_mdKbLeft, s_mdKbRight;
static uint8_t s_mdPrev[2][5] = {};      // column bits already reported as down
static uint8_t s_mdStable[2][5] = {};    // debounced column bits per row
static uint8_t s_mdRelCand[2][5] = {};   // bits currently counting down to released
static uint8_t s_mdRelCount[2][5] = {};  // consecutive reads backing that release

// Matrix positions, taken from the wadamesh MeshCore port's attaky_mesh_series
// variant (GPL-3.0-or-later, as is this project), which runs on this hardware:
// https://github.com/ALLFATHER-BV/wadamesh
//
// Worth keeping rather than re-deriving: Attaky's published key list cannot
// produce this. It names N and M under both halves and gives the left half more
// keys than it has positions, whereas the real layout puts B/N/M on the RIGHT
// half at row 3, and leaves seven positions empty.
#define MESH_DECK_KEYMAP_KNOWN 1

// Shift and Alt are keys in the left matrix, not separate modifier lines.
#define MD_KEY_SHIFT  0x01
#define MD_KEY_ALT    0x02
static const uint8_t kMdShiftHalf = 0, kMdShiftRow = 3, kMdShiftCol = 0;
static const uint8_t kMdAltHalf = 0, kMdAltRow = 4, kMdAltCol = 2;

static const char kMdKeymapLeft[5][5] = {
    {'1', '2', '3', '4', '5'},
    {'q', 'w', 'e', 'r', 't'},
    {'a', 's', 'd', 'f', 'g'},
    {MD_KEY_SHIFT, 'z', 'x', 'c', 'v'},
    {KEY_NONE, KEY_TAB, MD_KEY_ALT, ',', ' '},
};
static const char kMdKeymapRight[5][5] = {
    {'6', '7', '8', '9', '0'},
    {'y', 'u', 'i', 'o', 'p'},
    {'h', 'j', 'k', 'l', KEY_BACKSPACE},
    {'b', 'n', 'm', KEY_NONE, KEY_ENTER},
    // wadamesh types a literal '#' here. This is the physical symbol key, and
    // with no other punctuation on the matrix a single '#' is worth far less
    // than a way to reach the rest — so it opens the symbol tray instead, and
    // '#' lives in that tray.
    {KEY_NONE, '.', KEY_NONE, KEY_SYMBOL, KEY_NONE},
};

// Port 1 resting state: all five rows high, and P15..P17 (unused) held high.
static constexpr uint8_t kMdP1Idle = 0xE0;

// Puts a half into scanning shape: columns in, rows out. Aw9523::begin() leaves
// everything as inputs, which is the safe default but cannot drive a matrix.
static bool mdConfigureHalf(Aw9523 &dev) {
    if (!dev.present()) return false;
    return dev.configPort(0, 0xFF)          // port 0 = columns, inputs
        && dev.configPort(1, 0x00)          // port 1 = rows, outputs
        && dev.writePort(0, 0x00)
        && dev.writePort(1, kMdP1Idle);
}

// Drives each row low in turn and returns a pressed-bit mask per row.
//
// The other four rows are driven HIGH rather than left floating. Floating them
// would be gentler on simultaneous presses, but it only reads correctly if the
// columns have pull-ups, and whether this module has them is not documented.
// Driving high is what the working wadamesh port does, so it is known good.
static bool mdScanHalf(Aw9523 &dev, uint8_t rowsOut[5]) {
    if (!dev.present()) return false;
    for (uint8_t r = 0; r < 5; r++) {
        const uint8_t out = (uint8_t)(~(1u << r) | kMdP1Idle);
        if (!dev.writePort(1, out)) return false;
        delayMicroseconds(50);                  // let the column settle
        uint8_t cols = 0;
        if (!dev.readPort(0, cols)) return false;
        const uint8_t downBits = (uint8_t)(~cols & 0x1F);
        // All five columns of one row reading pressed is not something fingers
        // do — it is what a glitched I2C read looks like (0x00 back from the
        // expander). Discarding the whole scan is right rather than reporting
        // five keys: those become key events, and any key wakes the display,
        // so a single bad read while the screen is off turns into a spurious
        // wake. Light-sleep naps stop the I2C clock, which is exactly when
        // this happens.
        if (downBits == 0x1F) return false;
        rowsOut[r] = downBits;
    }
    dev.writePort(1, kMdP1Idle);
    return true;
}

// Asymmetric debounce: a press registers on the FIRST scan that sees it, and
// only the release has to be confirmed by kMdDebounceScans consecutive reads.
//
// The symmetric version this replaces needed two consecutive reads for a press
// too, which made the shortest registrable press two whole scans. That is only
// cheap if scans really happen on the interval below — and they do not. Nothing
// scans the matrix on a timer: readKey() is called once per loop() pass, so the
// interval is a floor and the loop period is the actual rate. On the Mesh Deck
// that period is ~18 ms with the channel list closed and 50-166 ms with it
// open, so "two scans" was a 36-330 ms minimum press. Taps shorter than that
// vanished with no trace, which is what made Enter feel late and sometimes miss
// entirely — worst exactly where the UI was busiest.
//
// Requiring nothing on the press edge is safe against contact bounce precisely
// because the release edge is still debounced: a chattering contact reads
// 1,0,1,0..., and every 1 after the first finds the bit already set, so it
// cannot produce a second keystroke. The bit does not clear — and so cannot
// re-fire — until it has read 0 for kMdDebounceScans in a row.
//
// What this does give up is the old rule's incidental cover against a single
// glitched read setting one column bit. Whole-scan corruption is still caught
// in mdScanHalf() (all five columns low); a clean single-bit glitch would now
// surface as one phantom keystroke rather than being swallowed. That trade is
// the right way round: dropped real keys were happening constantly, and a
// single-bit I2C glitch has never been observed on this bus.
static constexpr uint8_t kMdDebounceScans = 2;

static void mdDebounce(uint8_t half, const uint8_t fresh[5]) {
    for (uint8_t r = 0; r < 5; r++) {
        uint8_t stable = s_mdStable[half][r];

        // Newly-down bits go live immediately.
        const uint8_t pressBits = (uint8_t)(fresh[r] & ~stable);
        if (pressBits) {
            stable |= pressBits;
            s_mdStable[half][r] = stable;
        }

        // Bits still reported down that this read says are up.
        const uint8_t releaseBits = (uint8_t)(stable & ~fresh[r]);
        if (!releaseBits) {
            s_mdRelCand[half][r] = 0;
            s_mdRelCount[half][r] = 0;
            continue;
        }
        if (releaseBits != s_mdRelCand[half][r]) {
            // A different set of bits than the one being counted: start over
            // rather than letting a bounce inherit the earlier tally.
            s_mdRelCand[half][r] = releaseBits;
            s_mdRelCount[half][r] = 1;
            continue;
        }
        if (s_mdRelCount[half][r] < 0xFF) s_mdRelCount[half][r]++;
        if (s_mdRelCount[half][r] >= kMdDebounceScans) {
            s_mdStable[half][r] = (uint8_t)(stable & ~releaseBits);
            s_mdRelCand[half][r] = 0;
            s_mdRelCount[half][r] = 0;
        }
    }
}

// Shifted form of a key. Letters uppercase; the number row and the two
// punctuation keys follow the US QWERTY legends, which is muscle memory for
// anyone and puts the ten most-used symbols one chord away instead of behind
// the symbol tray. wadamesh shifts letters only, so its firmware cannot type
// any of these at all.
static char mdApplyShift(char k) {
    if (k >= 'a' && k <= 'z') return (char)(k - 'a' + 'A');
    switch (k) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case ',': return '<';
        case '.': return '>';
        default:  return k;
    }
}

bool meshDeckKeyboardHalfPresent(int half) {
    return half ? s_mdKbRight.present() : s_mdKbLeft.present();
}

// Walks both halves over I2C and folds the result into the debounced state.
//
// Both halves are scanned before any key is emitted. Shift lives in the matrix
// rather than on its own line, so its held state has to be known for this same
// pass — resolving a keypress against the previous pass's shift would drop the
// capital on a fast shift-then-letter.
static void mdScanAll() {
    uint8_t fresh[5];

    for (uint8_t half = 0; half < 2; half++) {
        Aw9523 &dev = half ? s_mdKbRight : s_mdKbLeft;
        if (!dev.present()) continue;
        if (!mdScanHalf(dev, fresh)) continue;
        mdDebounce(half, fresh);
    }
}

// Returns the first not-yet-reported press from the debounced state, or
// KEY_NONE. Touches no hardware, so it is safe to call when the scan interval
// has not elapsed — which is the point: one scan can turn up several presses,
// and holding the extras behind the interval would put them a whole loop pass
// apart for no reason.
static char mdEmitPending() {
    const bool shiftHeld =
        (s_mdStable[kMdShiftHalf][kMdShiftRow] & (1u << kMdShiftCol)) != 0;
    const bool altHeld =
        (s_mdStable[kMdAltHalf][kMdAltRow] & (1u << kMdAltCol)) != 0;

    for (uint8_t half = 0; half < 2; half++) {
        if (!(half ? s_mdKbRight : s_mdKbLeft).present()) continue;
        for (uint8_t r = 0; r < 5; r++) {
            const uint8_t now = s_mdStable[half][r];
            // Releases are always consumed; presses are consumed one at a time,
            // as they are reported. Marking the whole row reported (prev = now)
            // was losing the second of two keys pressed in the same row within
            // one debounce window, because this function returns after the
            // first one and the other's edge was already cleared.
            s_mdPrev[half][r] &= now;
            const uint8_t downEdges = (uint8_t)(now & ~s_mdPrev[half][r]);
            if (!downEdges) continue;

            for (uint8_t c = 0; c < 5; c++) {
                if (!(downEdges & (1u << c))) continue;
                s_mdPrev[half][r] |= (uint8_t)(1u << c);
                const char k = half ? kMdKeymapRight[r][c] : kMdKeymapLeft[r][c];
                if (k == KEY_NONE) {
#if defined(MESH_DECK_TOUCH_TRACE)
                    Serial.printf("[meshdeck-kb] unmapped: %s half row %u col %u\n",
                                  half ? "RIGHT" : "LEFT", r, c);
#else
                    if (sKeyTrace) {
                        Serial.printf("[meshdeck-kb] unmapped: %s half row %u col %u\n",
                                      half ? "RIGHT" : "LEFT", r, c);
                    }
#endif
                    continue;
                }
                if (k == MD_KEY_SHIFT || k == MD_KEY_ALT) continue;
                if (altHeld && (k == 'h' || k == 'H')) return KEY_OPEN_HOME;
                if (shiftHeld) return mdApplyShift(k);
                return k;
            }
        }
    }
    return KEY_NONE;
}
#endif  // DEVICE_MESH_DECK

#if defined(DEVICE_TDECK)
static constexpr uint8_t TDECK_KB_MODE_RAW_CMD = 0x03;
static constexpr uint8_t TDECK_KB_MODE_KEY_CMD = 0x04;
static bool sTdeckKeyboardRawModeSupported = false;

void tdeckKeyboardSetBacklight(uint8_t duty) {
    // Exactly two bytes, one command per transmission. The stock C3 firmware's
    // onReceive() switch falls through from LILYGO_KB_BRIGHTNESS_CMD into the
    // Alt+B default-brightness case, so a second command batched behind this one
    // would be read as that case's argument instead of as a command.
    Wire.beginTransmission(KB_ADDR);
    Wire.write((uint8_t)0x01);          // LILYGO_KB_BRIGHTNESS_CMD
    Wire.write(duty);
    // Return value ignored on purpose — see the boot probe in begin().
    (void)Wire.endTransmission();
}

static bool tdeckKeyboardAltHeldRaw() {
    if (!sTdeckKeyboardRawModeSupported) return false;

    Wire.beginTransmission(KB_ADDR);
    Wire.write(TDECK_KB_MODE_RAW_CMD);
    if (Wire.endTransmission() != 0) return false;

    uint8_t matrix[5] = {};
    const uint8_t count = Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)sizeof(matrix));
    uint8_t readCount = 0;
    while (Wire.available() && readCount < sizeof(matrix)) {
        matrix[readCount++] = (uint8_t)Wire.read();
    }

    Wire.beginTransmission(KB_ADDR);
    Wire.write(TDECK_KB_MODE_KEY_CMD);
    (void)Wire.endTransmission();

    // LilyGo raw reports five column bytes with row bits. Alt is C0/R4.
    return count == sizeof(matrix) && readCount == sizeof(matrix)
           && (matrix[0] & (1u << 4)) != 0;
}
#endif

void TDeckKeyboard::begin() {
#if defined(DEVICE_MESH_DECK)
    Wire.begin(KB_SDA, KB_SCL, 100000UL);
    Wire.setClock(400000UL);
    delay(30);
#if (KB_INT_LEFT >= 0)
    pinMode(KB_INT_LEFT, INPUT_PULLUP);
#endif
#if (KB_INT_RIGHT >= 0)
    pinMode(KB_INT_RIGHT, INPUT_PULLUP);
#endif
#if defined(MESH_DECK_TOUCH_TRACE)
    // Report *why* a half is missing: an address that does not ACK at all is a
    // wiring/seating problem, whereas one that ACKs with the wrong ID means the
    // chip is there but is not the AW9523 we assume.
    auto probeHalf = [](uint8_t addr) -> const char * {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) return "no-ack";
        Wire.beginTransmission(addr);
        Wire.write((uint8_t)0x10);                 // CHIPID
        if (Wire.endTransmission(false) != 0) return "ack-but-no-id-read";
        if (Wire.requestFrom((int)addr, 1) != 1) return "ack-but-no-id-data";
        const uint8_t id = (uint8_t)Wire.read();
        return (id == 0x23) ? "id-ok" : "wrong-id";
    };
    const char *whyL = probeHalf(KB_LEFT_I2C_ADDR);
    const char *whyR = probeHalf(KB_RIGHT_I2C_ADDR);
#endif

    const bool okL = s_mdKbLeft.begin(KB_LEFT_I2C_ADDR, Wire) && mdConfigureHalf(s_mdKbLeft);
    const bool okR = s_mdKbRight.begin(KB_RIGHT_I2C_ADDR, Wire) && mdConfigureHalf(s_mdKbRight);
    Serial.printf("[meshdeck-kb] left(0x%02X)=%s right(0x%02X)=%s\n",
                  KB_LEFT_I2C_ADDR, okL ? "ok" : "MISSING",
                  KB_RIGHT_I2C_ADDR, okR ? "ok" : "MISSING");
#if defined(MESH_DECK_TOUCH_TRACE)
    Serial.printf("[meshdeck-kb] probe: left=%s right=%s\n", whyL, whyR);

    // Whatever is actually on the bus, so a half sitting at an unexpected
    // address is obvious rather than inferred.
    // Bring-up only. A full 112-address sweep is cheap on a healthy bus and
    // very expensive on a stuck one: every address hits the I2C timeout, and
    // that much blocking in setup() can trip the watchdog before the UI ever
    // starts. Not something to run on every boot once the halves are known.
    Serial.print("[meshdeck-kb] I2C scan:");
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    Serial.println();
#endif
    return;
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    M5Cardputer.begin(true);
    return;
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    Wire.begin(KB_SDA, KB_SCL, 100000UL);
    Wire.setClock(400000UL);
    delay(30);
#if (KB_INT >= 0)
    pinMode(KB_INT, (KB_INT_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
#if defined(KB_BL) && (KB_BL >= 0)
    pinMode(KB_BL, OUTPUT);
    digitalWrite(KB_BL, HIGH);
#endif
    tloraResetKeyboardController();
#elif HAS_KEYBOARD
#if defined(M9_KB_NEEDS_USB_PAD_RELEASE)
    // Before Wire.begin(): the USB PHY is holding SDA until this runs.
    m9ReleaseUsbPads();
    delay(5);
#endif
    TwoWire &bus = keyboardBus();
    bus.begin(KB_SDA, KB_SCL, 100000UL);
#if !defined(DEVICE_M9)
    bus.setClock(400000UL);
#endif
    delay(50);
    bus.beginTransmission(KB_ADDR);
    bus.endTransmission();
    delay(50);
#if defined(DEVICE_M9)
    {
        uint8_t hw = 0xFF;
        uint8_t fw = 0xFF;
        const bool earlyController = m9IsEarlyController(bus, hw, fw);
        sM9KeyReg = earlyController ? KB_REG_KEY_EARLY : KB_REG_KEY;
        sM9NextEarlyProbeMs = millis() + 1000;

        uint8_t sample = 0xFF;
        const bool keyOk = m9ReadRegister(bus, sM9KeyReg, sample);
        Serial.printf("[kb] m9 probe bus=Wire1 proto=%s hw=0x%02X fw=0x%02X keyreg=0x%02X sample=0x%02X int=%d ok=%d\n",
                      earlyController ? "early" : "stc8h",
                      hw, fw, sM9KeyReg, sample, digitalRead(KB_INT), keyOk ? 1 : 0);
    }
#endif
#if (KB_INT >= 0)
    pinMode(KB_INT, (KB_INT_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
#if defined(DEVICE_TDECK)
    // One probe write, logged. A keyboard whose firmware predates the backlight
    // commands behaves exactly like one whose blink logic is broken — silence —
    // and this line is the only thing that tells the two apart afterwards. The
    // result is deliberately not used to gate anything: the C3 is documented as
    // not answering write requests in the ordinary way, so a non-zero code here
    // is not proof that the command was ignored.
    Wire.beginTransmission(KB_ADDR);
    Wire.write((uint8_t)0x01);          // LILYGO_KB_BRIGHTNESS_CMD
    Wire.write((uint8_t)0x00);          // start dark, matching the C3's own boot duty
    Serial.printf("[kb-bl] brightness probe ack=%d\n", (int)Wire.endTransmission());

    Wire.beginTransmission(KB_ADDR);
    Wire.write(TDECK_KB_MODE_RAW_CMD);
    const bool rawCommandOk = (Wire.endTransmission() == 0);
    const uint8_t rawCount = rawCommandOk
                                 ? Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)5)
                                 : 0;
    while (Wire.available()) (void)Wire.read();
    Wire.beginTransmission(KB_ADDR);
    Wire.write(TDECK_KB_MODE_KEY_CMD);
    (void)Wire.endTransmission();
    sTdeckKeyboardRawModeSupported = rawCommandOk && rawCount == 5;
    Serial.printf("[kb] T-Deck raw matrix mode=%s\n",
                  sTdeckKeyboardRawModeSupported ? "available" : "unavailable");
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
#if defined(DEVICE_MESH_DECK)
    // The halves do raise an interrupt on a change — but on this board neither
    // INT line reaches a GPIO (both land on expander 0x58, see KB_INT_LEFT /
    // KB_INT_RIGHT), so both reads below compile out and irqActive is always
    // false. The matrix is therefore walked by polling.
    //
    // This interval is a RATE LIMIT, not a scan rate. Nothing here runs on a
    // timer: readKey() is called once per loop() pass (from pumpKeyboardInput),
    // so the true scan interval is whichever is LONGER — this floor, or the
    // loop period. On the Mesh Deck the loop period is the one that binds:
    // ~18 ms with the channel list closed, and 50-166 ms with it open, because
    // lv_timer_handler() renders and flushes over blocking SPI in the same
    // thread. The matrix is simply not scanned for the length of a repaint.
    //
    // That is why the debounce above no longer requires a press to survive two
    // scans: at 166 ms per scan that was a 330 ms minimum press. Presses now
    // land on first sighting, so worst-case latency is one loop pass rather
    // than two, and no tap is lost unless it opens and closes entirely inside
    // a single repaint.
    //
    // One walk is ten I2C transactions plus 250 us of row settling, about
    // 2.5 ms, so 10 ms is the point where the bus cost stops being free.
    static constexpr uint32_t kMdScanIntervalMs = 10;
    static uint32_t lastScanMs = 0;
    const uint32_t nowMs = millis();
    bool irqActive = false;
#if (KB_INT_LEFT >= 0)
    irqActive = irqActive || (digitalRead(KB_INT_LEFT) == LOW);
#endif
#if (KB_INT_RIGHT >= 0)
    irqActive = irqActive || (digitalRead(KB_INT_RIGHT) == LOW);
#endif
    // Anything the last scan turned up but has not reported yet goes out first,
    // ahead of the interval gate — those cost no bus traffic, and making them
    // wait for the next scan is what put two keys of one scan a full loop pass
    // apart.
    const char pending = mdEmitPending();
    if (pending != KEY_NONE) return pending;

    if (!irqActive) {
        if ((uint32_t)(nowMs - lastScanMs) < kMdScanIntervalMs) return KEY_NONE;
    }
    lastScanMs = nowMs;
    mdScanAll();
    return mdEmitPending();
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    pumpCardputerKeys();
    if (_cardputerCount == 0) return KEY_NONE;
    return dequeueCardputerKey();
#elif defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    // The IRQ gate and the idle probe live in tloraPollController() now, so the
    // panel busy wait reaches them too and one poll cadence covers both callers.
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
    bool irqActive = (digitalRead(KB_INT) == KB_INT_ACTIVE_LEVEL);
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
#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
                expireHeldKeyBestEffort(now);
#endif
                return KEY_NONE;
            }
            lastIdleProbeMs = now;
        }
#else
        if (now - lastIdleProbeMs < kIdleProbeMs) {
#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
            expireHeldKeyBestEffort(now);
#endif
            return KEY_NONE;
        }
        lastIdleProbeMs = now;
#endif
    }
#endif
#if defined(DEVICE_M9)
    // The M9's controller is register-addressed: point it at the key register
    // first, then read one byte. A bare read returns whatever register was last
    // selected — register 0 is the hardware version, a constant — so without
    // this the driver reads the same non-zero byte forever and never sees a key.
    //
    // Also the back-off: when the controller is not answering, every poll would
    // otherwise emit a Wire error line, which floods the console many times a
    // second and buries everything else. Failures are counted and polling drops
    // to once a second until one succeeds.
    static uint32_t sM9NextProbeMs = 0;
    static uint16_t sM9FailStreak = 0;
    TwoWire &bus = keyboardBus();
    if (sM9KeyReg != KB_REG_KEY_EARLY
        && (int32_t)(now - sM9NextEarlyProbeMs) >= 0) {
        sM9NextEarlyProbeMs = now + 1000;
        uint8_t hw = 0xFF;
        uint8_t fw = 0xFF;
        if (m9IsEarlyController(bus, hw, fw)) {
            sM9KeyReg = KB_REG_KEY_EARLY;
            Serial.printf("[kb] m9 switched to early protocol hw=0x%02X fw=0x%02X keyreg=0x%02X\n",
                          hw, fw, sM9KeyReg);
        }
    }
    if (sM9FailStreak >= 8 && (int32_t)(now - sM9NextProbeMs) < 0) {
        expireHeldKeyBestEffort(now);
        return KEY_NONE;
    }
    bus.beginTransmission((uint8_t)KB_ADDR);
    bus.write(sM9KeyReg);
    if (bus.endTransmission() != 0) {
        if (sM9FailStreak < 0xFFFF) sM9FailStreak++;
        if (sM9FailStreak == 8) {
            Serial.println("[kb] m9 controller not responding; backing off to 1 Hz");
        }
        sM9NextProbeMs = now + 1000;
        expireHeldKeyBestEffort(now);
        return KEY_NONE;
    }
#endif
    uint8_t count = keyboardBus().requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
#if defined(DEVICE_M9)
    if (count == 1) {
        if (sM9FailStreak >= 8) Serial.println("[kb] m9 controller responding again");
        sM9FailStreak = 0;
    } else {
        if (sM9FailStreak < 0xFFFF) sM9FailStreak++;
        if (sM9FailStreak == 8) {
            Serial.printf("[kb] m9 read failed keyreg=0x%02X count=%u; backing off to 1 Hz\n",
                          sM9KeyReg, (unsigned)count);
        }
        sM9NextProbeMs = now + 1000;
    }
#endif
    if (!keyboardBus().available()) {
    #if defined(DEVICE_TDECK) && (KB_INT >= 0)
        if (irqActive) {
            // While the keyboard IRQ stays asserted, preserve the current held
            // key even if this poll doesn't return a byte yet.
            return KEY_NONE;
        }
    #endif
#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
        expireHeldKeyBestEffort(now);
#endif
        return KEY_NONE;
    }
    uint8_t raw = keyboardBus().read();
    if (raw == 0x00 || raw == 0xFF) {
    #if defined(DEVICE_TDECK) && (KB_INT >= 0)
        if (irqActive) {
            // Same as above: no new byte yet, but key hardware still signals
            // pending activity, so don't drop held-key timing.
            return KEY_NONE;
        }
    #endif
#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
        expireHeldKeyBestEffort(now);
#endif
        return KEY_NONE;
    }
#if defined(DEVICE_TDECK)
    lastKeyHitMs = now;
#endif
    char mapped = mapKey(raw);
#if defined(DEVICE_TDECK)
    if ((mapped == 'h' || mapped == 'H') && tdeckKeyboardAltHeldRaw()) {
        mapped = KEY_OPEN_HOME;
    }
#endif
    if (sKeyTrace) {
        Serial.printf("[kb] raw=0x%02X -> mapped=0x%02X%s\n",
                      raw, (uint8_t)mapped,
                      mapped == KEY_NONE ? "  (dropped)" : "");
    }
#if defined(DEVICE_M9)
    // Remembered so the log below can report how long the controller waits
    // before calling a press a long-press — the one number this side does not
    // otherwise know.
    if (mapped == KEY_ENTER) sM9LastEnterMs = now;
    static bool sM9FirstKeyLogged = false;
    if (!sM9FirstKeyLogged && mapped != KEY_NONE) {
        sM9FirstKeyLogged = true;
        Serial.printf("[kb] m9 first key raw=0x%02X mapped=0x%02X keyreg=0x%02X\n",
                      raw, (uint8_t)mapped, sM9KeyReg);
    }
    // Once per boot: how long the controller waited before calling the centre a
    // long-press, and whether it sent an ordinary Enter first. That is the only
    // way to see this device's threshold from here — it is the controller's
    // number, not one this firmware sets.
    static bool sM9HoldDiagLogged = false;
    if (!sM9HoldDiagLogged && raw == 0xA3) {
        sM9HoldDiagLogged = true;
        Serial.printf("[kb] m9 centre long-press: %lums after last Enter\n",
                      sM9LastEnterMs ? (unsigned long)(now - sM9LastEnterMs) : 0UL);
    }
#endif
#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
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

#if !defined(DEVICE_TLORA_PAGER_TFT) && HAS_KEYBOARD
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
#if defined(DEVICE_M9)
    // The M9's controller resolves shift/symbol/alt itself and sends final
    // ASCII for printable keys, so only the d-pad and the dedicated function
    // keys arrive as sentinels. Values confirmed on hardware by the reference
    // port; anything not listed falls through to the shared table below.
    switch (raw) {
        case 0xB5: return KEY_SCROLL_UP;    // d-pad up
        case 0xB6: return KEY_SCROLL_DN;    // d-pad down
        // The UI collapses these to up/down in one-dimensional contexts, but
        // preserves them as horizontal movement in multi-column pickers.
        case 0xB4: return KEY_PREV_CHAN;    // d-pad left
        case 0xB7: return KEY_NEXT_CHAN;    // d-pad right
        // Early controllers report Back as 0x86. Production STC firmware uses
        // 0x86 for Map hold and reports the same Back matrix position as 0x87.
        // KEY_BACK_BTN, not KEY_BACKSPACE: this is the dedicated Back button,
        // and the keyboard's own Backspace arrives separately as 0x08/0x7F.
        // Mapping both to KEY_BACKSPACE made them the same key to every
        // consumer, so Back could only ever do what Backspace did.
        case 0x86:
            return (sM9KeyReg == KB_REG_KEY_EARLY) ? KEY_BACK_BTN : KEY_NONE;
        case 0x87:
            return (sM9KeyReg == KB_REG_KEY_EARLY) ? KEY_NONE : KEY_BACK_BTN;
        case 0x89: return KEY_BACKSPACE_HOLD;  // Back/Delete long-press
        // Centre held. The controller raises this on its own, and it is the
        // only "still held" signal the early protocol gives us — see the note
        // above. It used to be a second close gesture; Back's own long-press
        // (0x89, just above) is the close-hold now, and this is "screen off".
        case 0xA3: return KEY_SLEEP_SCREEN;
        case 0x81: return KEY_OPEN_DMS;      // dedicated Messages button
        // Home opens Home, and only that. Holding it used to sleep the screen,
        // which is now the d-pad centre's job.
        case 0x82: return KEY_OPEN_HOME;     // dedicated Home button
        case 0x83: return KEY_OPEN_LIVE;     // function button below Home
        case 0x84: return KEY_OPEN_NODES;    // GPS-area button below Back
        case 0x85: return KEY_OPEN_DISCOVERY;  // dedicated Map button
        // Dedicated M9 functions have no Camillia binding yet. Their raw values
        // overlap this driver's synthetic navigation codes, so drop them.
        case 0x88:
        case 0x90:
            return KEY_NONE;
        default: break;
    }
#endif
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

// Keeps the keyboard controller drained while the caller is blocked on
// something long. The e-paper build calls this from inside the panel's busy
// wait, where loop() is parked for the better part of a second and the
// controller's ten-event FIFO overflows silently if nobody empties it.
//
// It paces itself because GxEPD2 calls it from a spin loop with no delay of its
// own: on the passes where it does not poll it hands back the millisecond that
// wait would otherwise have slept, so this costs no more CPU than the wait did.
void keyboardServiceDuringBlockingWork() {
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    static uint32_t lastServiceMs = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastServiceMs) < 4) {
        delay(1);
        return;
    }
    lastServiceMs = now;
    tloraPollController(now);
#else
    delay(1);
#endif
}

// Keys taken off the controller but not yet handed to the UI, and when the
// controller last reported a press (0 if it never has). Together they say
// whether a typing burst is still in flight, which the e-paper build uses to
// decide whether this is a good moment to spend ~700 ms repainting the panel.
uint8_t keyboardPendingKeys() {
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    return sTloraQueueCount;
#else
    return 0;
#endif
}

uint32_t keyboardLastKeyMs() {
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    return sTloraLastKeyMs;
#else
    return 0;
#endif
}

// Held-key reporting. Only the Pager's controller gives us press *and* release
// events, so it is the only build that can answer this; elsewhere callers get
// a best-effort heuristic keyed from recent keyboard activity.
char keyboardHeldKey() {
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    return sTloraHeldKey;
#elif !HAS_KEYBOARD
#  if HAS_BLE_KEYBOARD
    // This board has no keyboard of its own, so a held key can only come from a
    // paired Bluetooth one -- and that link reports real press and release
    // events, which makes the answer exact rather than the best-effort
    // heuristic the other builds fall back to.
    return bleKeyboardHeldKey();
#  else
    return KEY_NONE;
#  endif
#else
    uint32_t now = millis();
    expireHeldKeyBestEffort(now);
    return sHeldKeyBestEffort;
#endif
}

uint32_t keyboardHeldMs() {
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    if (sTloraHeldKey == KEY_NONE || sTloraHeldSinceMs == 0) return 0;
    return millis() - sTloraHeldSinceMs;
#elif !HAS_KEYBOARD
#  if HAS_BLE_KEYBOARD
    return bleKeyboardHeldMs();
#  else
    return 0;
#  endif
#else
    uint32_t now = millis();
    expireHeldKeyBestEffort(now);
    if (sHeldKeyBestEffort == KEY_NONE || sHeldSinceMsBestEffort == 0) return 0;
    return now - sHeldSinceMsBestEffort;
#endif
}
