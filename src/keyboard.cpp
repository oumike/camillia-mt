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
        // KEY_EVENT_A is the FIFO head and pops on read; the registers above it
        // are a passive view of the queue. Reading A+i therefore skips an event
        // per iteration and strands it in the FIFO, which is how a modifier
        // release ends up arriving after the keypress it was meant to modify.
        uint8_t ev = tloraReadReg(TLORA_REG_KEY_EVENT_A);
        if (ev == 0) break;
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

        // Releases must never reach tloraTranslateKey(): it drives the one-shot
        // modifier state machine, so re-mapping a release toggles shift/sym back
        // on (or clears a modifier the press already consumed) and the modifier
        // bleeds into a second keypress.
        if (!pressed) {
            if (keyNum == sTloraHeldKeyNum) {
                sTloraHeldKey = KEY_NONE;
                sTloraHeldKeyNum = 0;
                sTloraHeldSinceMs = 0;
            }
            continue;
        }

        char mapped = tloraTranslateKey(keyNum);

        // Track whatever is currently held so callers can offer hold-to-repeat.
        // The controller reports one press event and then nothing until release,
        // so a held key is invisible without this; the backspace-hold path above
        // already relies on the same press/release pairing.
        if (mapped != KEY_NONE && keyNum != sTloraHeldKeyNum) {
            sTloraHeldKey = mapped;
            sTloraHeldKeyNum = keyNum;
            sTloraHeldSinceMs = now;
        }

        if (mapped != KEY_NONE) {
            return mapped;
        }
    }

    return KEY_NONE;
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

// Shift is a key in the matrix (left half, row 3, col 0), not a modifier line.
#define MD_KEY_SHIFT  0x01
static const uint8_t kMdShiftHalf = 0, kMdShiftRow = 3, kMdShiftCol = 0;

static const char kMdKeymapLeft[5][5] = {
    {'1', '2', '3', '4', '5'},
    {'q', 'w', 'e', 'r', 't'},
    {'a', 's', 'd', 'f', 'g'},
    {MD_KEY_SHIFT, 'z', 'x', 'c', 'v'},
    {KEY_NONE, KEY_TAB, KEY_NONE, ',', ' '},
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
#if defined(MESH_DECK_TOUCH_TRACE)
                // Announce presses on positions the map calls empty. Attaky's
                // published key list does not say where Fn or the symbol key
                // sit in the matrix, so this is how their coordinates get
                // established: press the key, read the row/col, fill it in.
                if (k == KEY_NONE) {
                    Serial.printf("[meshdeck-kb] unmapped: %s half row %u col %u\n",
                                  half ? "RIGHT" : "LEFT", r, c);
                }
#endif
                if (k == KEY_NONE || k == MD_KEY_SHIFT) continue;   // shift is a state
                if (shiftHeld) return mdApplyShift(k);
                return k;
            }
        }
    }
    return KEY_NONE;
}
#endif  // DEVICE_MESH_DECK

#if defined(DEVICE_TDECK)
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
