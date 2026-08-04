// ════════════════════════════════════════════════════════════════════════════
// mesh_deck_probe — discovers the Attaky Mesh Deck's pin map on real hardware.
//
//   pio run -e mesh-deck-probe -t upload && pio device monitor -e mesh-deck-probe
//
// Attaky documents signals by connector position and never publishes the
// host-side F-BTB -> ESP32-S3 GPIO table, so the numbers have to come off the
// board. This sketch finds them by measurement rather than guesswork, then
// prints a block ready to paste into src/hal/hw_mesh_deck.h.
//
// It leans on two oracles:
//
//   • I2C  — the chips on this board have known, distinctive addresses
//            (FT6636 0x38, AW9523 0x58/0x59/0x5A/0x5B, MAX17048 0x36,
//            QMI8658A 0x6B, STK3311-X 0x48). Sweeping candidate SDA/SCL pairs
//            and looking for that fingerprint identifies the buses outright.
//
//   • BUSY — the SX1262 drives BUSY high while it digests a command and drops
//            it when done. That makes the radio self-identifying: pulse a
//            candidate RESET and watch every other pin for the release edge to
//            find RESET/BUSY, then use BUSY as a completion signal to confirm a
//            candidate SCK/MOSI/CS set really is talking to the radio.
//
// Nothing here is destructive: every pin is tested for an existing driver
// before it is driven, and anything already being driven is left alone.
// ════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>

// ── Candidate GPIOs ─────────────────────────────────────────────────────────
// The core module is an ESP32-S3-WROOM-1U-N16R8. On that part GPIO26..32 are
// the SPI flash and GPIO33..37 are the octal PSRAM, so they are not merely
// unavailable — touching them crashes the running image. GPIO19/20 are USB D-/D+
// and carry this sketch's own serial output. GPIO0 is a strapping pin.
static const uint8_t kCandidates[] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
};
static const size_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

// Chips the datasheets promise, with the bus each is expected on.
struct KnownChip {
    uint8_t     addr;
    const char *name;
};
static const KnownChip kKnownChips[] = {
    {0x36, "MAX17048 fuel gauge"},
    {0x38, "FT6636 touch"},
    {0x48, "STK3311-X ambient light"},
    {0x58, "AW9523 expander (touch INT)"},
    {0x59, "AW9523 expander (buttons/LED)"},
    {0x5A, "AW9523 keyboard, left half"},
    {0x5B, "AW9523 keyboard, right half"},
    {0x6B, "QMI8658A IMU"},
};
static const size_t kKnownChipCount = sizeof(kKnownChips) / sizeof(kKnownChips[0]);

static bool  s_safeToDrive[64] = {};
static bool  s_reserved[64] = {};   // discovered I2C lines: never drive these
static int   s_i2cSda = -1, s_i2cScl = -1;
static int   s_kbSda  = -1, s_kbScl  = -1;
static int   s_loraRst = -1, s_loraBusy = -1;
static int   s_loraSck = -1, s_loraMosi = -1, s_loraCs = -1, s_loraMiso = -1;

// ── Stage 1: which pins are safe to drive ───────────────────────────────────
// A pin with nothing on it follows its own pull resistor. A pin held by another
// output ignores the pull and reports the same level both ways. Skipping the
// latter keeps the SPI sweep from fighting a driver and stressing either side.
static void censusDrivablePins() {
    Serial.println(F("\n[1/4] Checking which candidate pins are free to drive"));
    for (size_t i = 0; i < kCandidateCount; i++) {
        const uint8_t p = kCandidates[i];
        pinMode(p, INPUT_PULLUP);
        delayMicroseconds(300);
        const int hi = digitalRead(p);
        pinMode(p, INPUT_PULLDOWN);
        delayMicroseconds(300);
        const int lo = digitalRead(p);
        pinMode(p, INPUT);

        s_safeToDrive[p] = (hi == HIGH && lo == LOW);
        if (!s_safeToDrive[p]) {
            Serial.printf("      GPIO%-2u held %s by something else - will not drive it\n",
                          p, hi == HIGH ? "HIGH" : "LOW");
        }
    }
}

// ── Stage 2: I2C bus discovery ──────────────────────────────────────────────
static uint8_t scanBusForKnownChips(int sda, int scl, uint8_t *foundOut, size_t foundMax) {
    uint8_t found = 0;
    Wire.end();
    if (!Wire.begin(sda, scl, 100000)) return 0;

    for (size_t i = 0; i < kKnownChipCount; i++) {
        Wire.beginTransmission(kKnownChips[i].addr);
        if (Wire.endTransmission() == 0) {
            if (found < foundMax) foundOut[found] = kKnownChips[i].addr;
            found++;
        }
    }
    Wire.end();
    return found;
}

static void discoverI2cBuses() {
    Serial.println(F("\n[2/4] Sweeping GPIO pairs for the I2C buses"));
    Serial.println(F("      (looking for the documented address fingerprint)"));

    uint8_t bestFound = 0;
    for (size_t a = 0; a < kCandidateCount; a++) {
        for (size_t b = 0; b < kCandidateCount; b++) {
            if (a == b) continue;
            const uint8_t sda = kCandidates[a], scl = kCandidates[b];

            uint8_t hits[kKnownChipCount];
            const uint8_t n = scanBusForKnownChips(sda, scl, hits, kKnownChipCount);
            if (n == 0) continue;

            Serial.printf("      HIT  SDA=GPIO%-2u SCL=GPIO%-2u  ->  %u chip(s):", sda, scl, n);
            bool sawCore = false, sawKeyboard = false;
            for (uint8_t i = 0; i < n && i < kKnownChipCount; i++) {
                Serial.printf(" 0x%02X", hits[i]);
                if (hits[i] == 0x38 || hits[i] == 0x59 || hits[i] == 0x36) sawCore = true;
                if (hits[i] == 0x5A || hits[i] == 0x5B) sawKeyboard = true;
            }
            Serial.println();

            // Prefer the pairing that sees the most chips; the frame may carry
            // the keyboard on the same main bus or on the secondary one.
            if (n > bestFound && sawCore) {
                bestFound = n;
                s_i2cSda = sda;
                s_i2cScl = scl;
            }
            if (sawKeyboard && s_kbSda < 0) {
                s_kbSda = sda;
                s_kbScl = scl;
            }
        }
    }

    if (s_i2cSda < 0) {
        Serial.println(F("      No bus found. If the keyboard/expanders are unpowered at boot,"));
        Serial.println(F("      the frame may gate their rail - retry with the device awake."));
        return;
    }

    // Off limits from here on: the later stages bit-bang push-pull, and doing
    // that to a live bus can wedge every chip hanging off it.
    if (s_i2cSda >= 0) s_reserved[s_i2cSda] = true;
    if (s_i2cScl >= 0) s_reserved[s_i2cScl] = true;
    if (s_kbSda  >= 0) s_reserved[s_kbSda]  = true;
    if (s_kbScl  >= 0) s_reserved[s_kbScl]  = true;

    Serial.printf("\n      Main bus:     SDA=GPIO%d  SCL=GPIO%d\n", s_i2cSda, s_i2cScl);
    if (s_kbSda >= 0 && (s_kbSda != s_i2cSda || s_kbScl != s_i2cScl)) {
        Serial.printf("      Keyboard bus: SDA=GPIO%d  SCL=GPIO%d (separate)\n", s_kbSda, s_kbScl);
    } else if (s_kbSda >= 0) {
        Serial.println(F("      Keyboard shares the main bus."));
    }

    // Full enumeration of whatever is actually out there, expected or not.
    Serial.println(F("\n      Full scan of the main bus:"));
    Wire.end();
    Wire.begin(s_i2cSda, s_i2cScl, 100000);
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;
        const char *name = "unknown";
        for (size_t i = 0; i < kKnownChipCount; i++) {
            if (kKnownChips[i].addr == addr) { name = kKnownChips[i].name; break; }
        }
        Serial.printf("        0x%02X  %s\n", addr, name);
    }
    Wire.end();
}

// ── Stage 3: SX1262 RESET and BUSY ──────────────────────────────────────────
// Releasing the radio from reset makes BUSY fall once the chip finishes booting.
// Pulsing each candidate RESET and watching every other pin for that edge finds
// both lines without knowing anything about the SPI wiring yet.
static void discoverLoraResetBusy() {
    Serial.println(F("\n[3/4] Pulsing candidate RESET pins, watching for the BUSY edge"));

    for (size_t r = 0; r < kCandidateCount; r++) {
        const uint8_t rst = kCandidates[r];
        if (!s_safeToDrive[rst] || s_reserved[rst]) continue;

        // Park every other candidate as an input so a BUSY edge is visible.
        for (size_t i = 0; i < kCandidateCount; i++) {
            if (kCandidates[i] != rst) pinMode(kCandidates[i], INPUT);
        }

        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW);
        delay(2);

        // Only pins sitting LOW under reset can show the rise-then-fall we are
        // after. Seeding the "went high" flag from the current level instead
        // would let an already-high pin that later drops read as a match.
        bool startedLow[64] = {};
        bool wentHigh[64] = {};
        for (size_t i = 0; i < kCandidateCount; i++) {
            const uint8_t p = kCandidates[i];
            if (p != rst) startedLow[p] = (digitalRead(p) == LOW);
        }

        digitalWrite(rst, HIGH);

        // SX1262 asserts BUSY immediately and clears it within ~1 ms of POR.
        bool roseThenFell[64] = {};
        const uint32_t deadline = millis() + 20;
        while (millis() < deadline) {
            for (size_t i = 0; i < kCandidateCount; i++) {
                const uint8_t p = kCandidates[i];
                if (p == rst || !startedLow[p]) continue;
                if (digitalRead(p) == HIGH) wentHigh[p] = true;
            }
            delayMicroseconds(200);
        }
        for (size_t i = 0; i < kCandidateCount; i++) {
            const uint8_t p = kCandidates[i];
            if (p == rst) continue;
            roseThenFell[p] = startedLow[p] && wentHigh[p] && (digitalRead(p) == LOW);
        }

        pinMode(rst, INPUT);

        for (size_t i = 0; i < kCandidateCount; i++) {
            const uint8_t p = kCandidates[i];
            if (!roseThenFell[p]) continue;
            Serial.printf("      Candidate: RESET=GPIO%-2u  BUSY=GPIO%-2u\n", rst, p);
            if (s_loraRst < 0) { s_loraRst = rst; s_loraBusy = p; }
        }
    }

    if (s_loraRst < 0) {
        Serial.println(F("      No RESET/BUSY pair seen. The radio rail may be switched -"));
        Serial.println(F("      check whether an AW9523 output gates it before retrying."));
    }
}

// ── Stage 4: SX1262 SPI lines ───────────────────────────────────────────────
// With BUSY known it becomes a completion oracle: bit-bang GetStatus (0xC0) on a
// candidate SCK/MOSI/CS set and BUSY only pulses if the radio actually received
// it. Sweeping three lines is ~24k tries, which is seconds, not hours.
static bool sx1262Responds(uint8_t sck, uint8_t mosi, uint8_t cs, uint8_t *statusOut) {
    pinMode(sck, OUTPUT);  digitalWrite(sck, LOW);
    pinMode(mosi, OUTPUT); digitalWrite(mosi, LOW);
    pinMode(cs, OUTPUT);   digitalWrite(cs, HIGH);
    delayMicroseconds(50);

    if (digitalRead(s_loraBusy) == HIGH) return false;   // not idle; wrong guess

    digitalWrite(cs, LOW);
    delayMicroseconds(5);
    uint8_t rx = 0;
    const uint8_t tx = 0xC0;                              // GetStatus
    for (int bit = 7; bit >= 0; bit--) {
        digitalWrite(mosi, (tx >> bit) & 1);
        delayMicroseconds(2);
        digitalWrite(sck, HIGH);
        delayMicroseconds(2);
        if (s_loraMiso >= 0) rx = (uint8_t)((rx << 1) | (digitalRead(s_loraMiso) & 1));
        digitalWrite(sck, LOW);
        delayMicroseconds(2);
    }
    digitalWrite(cs, HIGH);
    if (statusOut) *statusOut = rx;

    // A real SX1262 raises BUSY while it processes the command.
    const uint32_t deadline = micros() + 2000;
    while (micros() < deadline) {
        if (digitalRead(s_loraBusy) == HIGH) return true;
    }
    return false;
}

static void discoverLoraSpi() {
    Serial.println(F("\n[4/4] Sweeping SCK/MOSI/CS, using BUSY to confirm the radio answered"));
    if (s_loraBusy < 0) {
        Serial.println(F("      Skipped: BUSY unknown, so there is nothing to confirm against."));
        return;
    }

    for (size_t a = 0; a < kCandidateCount && s_loraSck < 0; a++) {
        const uint8_t sck = kCandidates[a];
        if (!s_safeToDrive[sck] || s_reserved[sck]) continue;
        if (sck == (uint8_t)s_loraBusy || sck == (uint8_t)s_loraRst) continue;
        for (size_t b = 0; b < kCandidateCount && s_loraSck < 0; b++) {
            const uint8_t mosi = kCandidates[b];
            if (mosi == sck || !s_safeToDrive[mosi] || s_reserved[mosi]) continue;
            if (mosi == (uint8_t)s_loraBusy || mosi == (uint8_t)s_loraRst) continue;
            for (size_t c = 0; c < kCandidateCount; c++) {
                const uint8_t cs = kCandidates[c];
                if (cs == sck || cs == mosi || !s_safeToDrive[cs] || s_reserved[cs]) continue;
                if (cs == (uint8_t)s_loraBusy || cs == (uint8_t)s_loraRst) continue;

                if (sx1262Responds(sck, mosi, cs, nullptr)) {
                    Serial.printf("      SCK=GPIO%-2u MOSI=GPIO%-2u CS=GPIO%-2u  <- radio answered\n",
                                  sck, mosi, cs);
                    s_loraSck = sck; s_loraMosi = mosi; s_loraCs = cs;
                    break;
                }
            }
        }
    }

    if (s_loraSck < 0) {
        Serial.println(F("      No combination answered."));
        return;
    }

    // MISO last: replay GetStatus against each remaining pin and keep the one
    // returning a plausible status (SX1262 chip mode 0x2..0x5 in bits 6:4).
    Serial.println(F("      Looking for MISO"));
    for (size_t i = 0; i < kCandidateCount; i++) {
        const uint8_t p = kCandidates[i];
        if (p == s_loraSck || p == s_loraMosi || p == s_loraCs) continue;
        if (p == (uint8_t)s_loraBusy || p == (uint8_t)s_loraRst) continue;
        if (s_reserved[p]) continue;

        pinMode(p, INPUT);
        s_loraMiso = p;
        uint8_t status = 0;
        if (sx1262Responds(s_loraSck, s_loraMosi, s_loraCs, &status)) {
            const uint8_t mode = (uint8_t)((status >> 4) & 0x7);
            if (mode >= 0x2 && mode <= 0x5) {
                Serial.printf("      MISO=GPIO%-2u (status 0x%02X, chip mode %u)\n", p, status, mode);
                return;
            }
        }
        s_loraMiso = -1;
    }
    Serial.println(F("      MISO not identified; the other three are still good."));
}

static void printReport() {
    Serial.println(F("\n════════════════════════════════════════════════════════════"));
    Serial.println(F("Paste into src/hal/hw_mesh_deck.h, then define MESH_DECK_PINS_KNOWN"));
    Serial.println(F("════════════════════════════════════════════════════════════"));
    Serial.printf("#define I2C_SDA       %d\n", s_i2cSda);
    Serial.printf("#define I2C_SCL       %d\n", s_i2cScl);
    Serial.printf("#define LORA_SPI_SCK  %d\n", s_loraSck);
    Serial.printf("#define LORA_SPI_MOSI %d\n", s_loraMosi);
    Serial.printf("#define LORA_SPI_MISO %d\n", s_loraMiso);
    Serial.printf("#define LORA_CS       %d\n", s_loraCs);
    Serial.printf("#define LORA_RST      %d\n", s_loraRst);
    Serial.printf("#define LORA_BUSY     %d\n", s_loraBusy);
    Serial.println(F("\nStill to find by hand (this sketch cannot infer them):"));
    Serial.println(F("  LORA_DIO1  - IRQ line; it only moves once the radio is configured"));
    Serial.println(F("               and receiving, so catch it with a running RX."));
    Serial.println(F("  TFT_*      - the panel is write-only, so it answers nothing."));
    Serial.println(F("  GPS_RX/TX  - see the UART hint below."));
    Serial.println(F("  KB_INT_*   - watch for a falling edge while pressing a key."));
    Serial.println(F("\nUART hint: the GNSS talks unprompted at 9600 baud, so watching each"));
    Serial.println(F("remaining candidate for NMEA ('$GxRMC') identifies GPS_RX directly."));
}

// ── Stage 5: interactive input census ───────────────────────────────────────
// The datasheets account for 8 buttons on expander 0x59 (D-pad, Enter, L1, R1,
// Power) plus the 48-key matrix. Real units have more inputs than that, and the
// extras are not documented anywhere, so this stage stops predicting and just
// watches: it finds every AW9523 on the bus and names whichever line moves.
//
// Direct buttons and matrix keys are told apart by construction. A direct
// button sits on a pin that changes with no help, so it shows up while every
// expander is passively read. A matrix key only conducts when its row is
// driven, so it stays invisible here and is reported by row/column instead.
static const uint8_t kExpanderScanLo = 0x58;
static const uint8_t kExpanderScanHi = 0x5F;

static uint8_t s_expAddrs[8] = {};
static uint8_t s_expCount = 0;
static uint8_t s_expBase[8][2] = {};      // resting level per expander, per port
static bool    s_expIsKeyboard[8] = {};

static bool expReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    val = (uint8_t)Wire.read();
    return true;
}

static bool expWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static void beginInputCensus() {
    Serial.println(F("\n[5/5] Input census - press every button, one at a time"));

    if (s_i2cSda < 0) {
        Serial.println(F("      Skipped: no I2C bus was found."));
        return;
    }
    Wire.end();
    Wire.begin(s_i2cSda, s_i2cScl, 100000);

    for (uint8_t a = kExpanderScanLo; a <= kExpanderScanHi; a++) {
        uint8_t id = 0;
        if (!expReadReg(a, 0x10, id) || id != 0x23) continue;   // AW9523 chip ID

        // Both ports to GPIO input. The mode registers reset to LED (constant
        // current) mode, where a pin sinks instead of reading, so this has to
        // be set before anything can be sensed.
        expWriteReg(a, 0x12, 0xFF);   // LED mode port 0 -> GPIO
        expWriteReg(a, 0x13, 0xFF);   // LED mode port 1 -> GPIO
        expWriteReg(a, 0x04, 0xFF);   // config port 0 -> input
        expWriteReg(a, 0x05, 0xFF);   // config port 1 -> input

        if (s_expCount < 8) {
            s_expAddrs[s_expCount] = a;
            s_expIsKeyboard[s_expCount] = (a == 0x5A || a == 0x5B);
            expReadReg(a, 0x00, s_expBase[s_expCount][0]);
            expReadReg(a, 0x01, s_expBase[s_expCount][1]);
            Serial.printf("      AW9523 at 0x%02X  resting P0=0x%02X P1=0x%02X%s\n",
                          a, s_expBase[s_expCount][0], s_expBase[s_expCount][1],
                          s_expIsKeyboard[s_expCount] ? "  (keyboard half)" : "");
            s_expCount++;
        }
    }

    if (s_expCount == 0) {
        Serial.println(F("      No AW9523 found - is the frame powered and the FPC seated?"));
        return;
    }
    Serial.printf("      Watching %u expander(s). Press buttons now.\n", s_expCount);
    Serial.println(F("      Direct buttons report as PIN; matrix keys as row/col.\n"));
}

static void pollInputCensus() {
    for (uint8_t i = 0; i < s_expCount; i++) {
        const uint8_t addr = s_expAddrs[i];

        // Passive read first: catches every directly-wired input, including any
        // the documentation never mentioned.
        for (uint8_t port = 0; port < 2; port++) {
            uint8_t now = 0;
            if (!expReadReg(addr, port ? 0x01 : 0x00, now)) continue;
            const uint8_t changed = (uint8_t)(now ^ s_expBase[i][port]);
            if (!changed) continue;
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (!(changed & (1u << bit))) continue;
                const bool pressed = !(now & (1u << bit));   // inputs idle high
                Serial.printf("  0x%02X  P%u%u  %s\n", addr, port, bit,
                              pressed ? "PRESSED " : "released");
            }
            s_expBase[i][port] = now;
        }

        // Then the matrix, for the two keyboard halves only.
        if (!s_expIsKeyboard[i]) continue;
        for (uint8_t r = 0; r < 5; r++) {
            const uint8_t rowBit = (uint8_t)(1u << r);
            expWriteReg(addr, 0x05, (uint8_t)(0xFF & ~rowBit));   // row r output
            expWriteReg(addr, 0x03, (uint8_t)(0xFF & ~rowBit));   // driven low
            delayMicroseconds(60);
            uint8_t cols = 0;
            if (expReadReg(addr, 0x00, cols)) {
                const uint8_t pressed = (uint8_t)(~cols & 0x1F);
                for (uint8_t c = 0; c < 5; c++) {
                    if (pressed & (1u << c)) {
                        Serial.printf("  0x%02X  matrix row %u col %u  PRESSED\n", addr, r, c);
                    }
                }
            }
            expWriteReg(addr, 0x05, 0xFF);   // release the row
        }
        // The matrix scan drives port 1, so re-baseline it rather than let the
        // next passive pass read the scan's own footprint as a button.
        expReadReg(addr, 0x01, s_expBase[i][1]);
    }
}

void setup() {
    Serial.begin(115200);
    const uint32_t start = millis();
    while (!Serial && (millis() - start) < 4000) delay(10);

    Serial.println(F("\n\nAttaky Mesh Deck pin probe"));
    Serial.println(F("Reports measured pin assignments; drives nothing already in use."));

    censusDrivablePins();
    discoverI2cBuses();
    discoverLoraResetBusy();
    discoverLoraSpi();
    printReport();
    beginInputCensus();
}

void loop() {
    pollInputCensus();
    delay(15);
}
