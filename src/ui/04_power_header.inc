// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: keyboard light, loop phase timing, touch sleep, header colours.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static inline bool kbLightIdleDark() {
#if HAS_KB_LIGHT_TIMEOUT
    return s_kbLightIdleDark;
#else
    return false;
#endif
}

// Drives the light exactly as asked. Callers that mean "the resting state" go
// through setPagerKeyboardBacklight() below, which applies the idle timeout;
// only the notification blink comes here directly.
static void kbLightDrive(bool on) {
#if defined(DEVICE_TDECK_PRO) && defined(KB_BL) && (KB_BL >= 0)
    // Alt+B still decides lit or dark; the Keyboard Light setting decides how
    // bright "lit" is. Two settings for one light only looks like one too many
    // until you have used the toggle -- it is the gesture you reach for in the
    // dark, and it should not have to ask how bright.
    //
    // digitalWrite() would do nothing here: the pin is LEDC's from begin().
    keyboardSetKeypadBacklight(on ? cfgCoerceKbBacklightLevel((int)s_cfg.kbBacklightLevel)
                                  : 0);
#elif defined(DEVICE_TLORA_PAGER_TFT) && defined(KB_BL) && (KB_BL >= 0)
    digitalWrite(KB_BL, on ? HIGH : LOW);
#elif defined(DEVICE_TDISPLAY_P4)
    // The keyboard expansion's backlight: lit with the screen unless its own
    // key (F7) has switched it off -- the same kbBacklightEnabled the T-Deck
    // Pro's Alt+B keeps -- at the level F11 steps through (kbBacklightLevel,
    // the Low/Medium/High/Off stops the other keyboards' setting uses). Set
    // whether or not the expansion is on; the driver holds the level and
    // applies it when it arrives.
    keyboardSetKeypadBacklight(
        (on && s_cfg.kbBacklightEnabled)
            ? cfgCoerceKbBacklightLevel((int)s_cfg.kbBacklightLevel) : 0);
#elif defined(DEVICE_TANMATSU)
    // The coprocessor holds the duty; lit is the Keyboard Light level.
    keyboardSetKeypadBacklight(on ? cfgCoerceKbBacklightLevel((int)s_cfg.kbBacklightLevel)
                                  : 0);
#else
    LV_UNUSED(on);
#endif
}

static void setPagerKeyboardBacklight(bool on) {
    kbLightDrive(on && !kbLightIdleDark());
}

// ── CPU frequency scaling ────────────────────────────────────────────────────
// Screen-off is the only state where nobody is waiting on redraw latency, so
// that is when the CPU drops. 80 MHz is the floor on purpose: on the ESP32-S3
// the APB clock is a fixed 80 MHz for every PLL-sourced CPU frequency
// (240/160/80) and only follows the CPU below that, so at 80 MHz every
// peripheral clock — UART, SPI, I2C, LEDC, esp_timer — is untouched, and
// setCpuFrequencyMhz() doesn't even enter its APB-change path. It is also the
// minimum Wi-Fi will run at.
#if defined(DEVICE_TDISPLAY_P4)
// None of that holds on the ESP32-P4. It runs at 360 MHz and offers 360, 180,
// 90, 40 and below -- no 240, no 80 -- and what dropping it does to the MIPI
// panel, the SDIO link to the C6 that carries Wi-Fi, and PSRAM has not been
// measured. So no scaling: both ends are the clock it boots at, and nothing is
// ever asked for.
static constexpr uint32_t kCpuMhzActive = 360;
static constexpr uint32_t kCpuMhzIdle   = 360;
#else
static constexpr uint32_t kCpuMhzActive = 240;
static constexpr uint32_t kCpuMhzIdle   = 80;
#endif
static uint32_t s_cpuMhz = kCpuMhzActive;
// A frequency the chip has refused once is not asked for again: this runs every
// loop pass, and a refused one would otherwise log an error from the core on
// each of them.
static uint32_t s_cpuMhzRefused = 0;

static void applyCpuMhz(uint32_t mhz) {
    if (s_cpuMhz == mhz || s_cpuMhzRefused == mhz) return;
    if (!setCpuFrequencyMhz(mhz)) {
        s_cpuMhzRefused = mhz;
        Serial.printf("[power] CPU %lu MHz rejected; not asking again\n", (unsigned long)mhz);
        return;
    }
    s_cpuMhz = mhz;
    Serial.printf("[power] CPU %lu MHz\n", (unsigned long)mhz);
}

// Web config runs a synchronous server that is already latency-sensitive and
// prone to browser timeouts, so it keeps the full clock even with the screen
// off. Reconciled every loop pass rather than only on sleep/wake so that
// starting or stopping web config while the screen is off lands correctly.
static void serviceCpuScaling() {
    applyCpuMhz((s_screenAsleep && !webCfgRunning()) ? kCpuMhzIdle : kCpuMhzActive);
}

// The ST7789/ST7796 controllers need ~120 ms after SLPOUT before they reliably
// accept drawing commands. LovyanGFX's wakeup() issues the command but does not
// wait, so wakeScreen() does. The panel also still holds the pre-sleep frame in
// its RAM across SLPIN/SLPOUT, so the backlight stays off until the loop has
// repainted — otherwise waking shows a stale screen for a frame.
static constexpr uint32_t kPanelWakeSettleMs = 120;
static bool s_backlightPendingOn = false;

// How often loop() runs the view-refresh block. The loop itself keeps its 5 ms
// pace because the T-Deck keyboard needs a sub-12 ms poll cadence; only the
// redraw work is throttled.
static constexpr uint32_t kUiRefreshTickMs = 30;

// ── Loop latency probe ───────────────────────────────────────────────────────
// Times the parts of loop() that can block, and reports the worst offenders on
// a rolling window. Built because the Heltec sluggishness in issue #53 is
// intermittent: a freeze you cannot reproduce on demand cannot be attributed by
// reading the code, and this session already spent two flashes on hypotheses
// that measurement would have settled immediately.
//
// Cost when nothing is slow: two millis() calls per wrapped phase. The report
// only prints when something actually exceeded the threshold, so a healthy
// device stays silent.
#ifndef LOOP_LATENCY_DEBUG
#define LOOP_LATENCY_DEBUG 1
#endif

#if LOOP_LATENCY_DEBUG
// One UI tick. Anything longer than this has dropped a frame by definition.
//
// Only governs which phases are listed individually; the measured/unmeasured
// totals on the same line count every phase regardless, which is what makes an
// iteration assembled entirely from sub-threshold costs visible at all. Drop it
// to ~5 temporarily when one of those needs breaking down.
static constexpr uint32_t kLoopPhaseWarnMs = 30;
static constexpr uint32_t kLoopReportMs = 15000;

struct LoopPhaseStat {
    const char *name;
    uint32_t worstMs;
    uint32_t overCount;
    uint32_t totalOverMs;
};
// Sized for every LOOP_PHASE tag in loop() with headroom. At the lowered
// threshold far more of them qualify, and loopPhaseNote() silently drops any
// phase past the end of this array -- which would look exactly like the phase
// being cheap.
static LoopPhaseStat s_loopPhases[48];
static int s_loopPhaseCount = 0;
static uint32_t s_loopReportLastMs = 0;
static uint32_t s_loopWorstTotalMs = 0;
// How much of the worst pass was spent inside instrumented phases. The gap
// between this and the total is time in code no LOOP_PHASE covers, which is the
// difference between "one of these calls is slow" and "we are not looking in
// the right place at all".
static uint32_t s_loopWorstMeasuredMs = 0;
static uint32_t s_loopMeasuredThisPass = 0;

// Start-of-pass reset for the measured-time accumulator. A function so the
// no-debug build can compile it away with everything else.
static void loopPhaseBeginPass() { s_loopMeasuredThisPass = 0; }

static void loopPhaseNote(const char *name, uint32_t ms) {
    // Counted before the threshold, so the measured total includes the many
    // small costs that are the whole reason this figure exists.
    s_loopMeasuredThisPass += ms;
    if (ms < kLoopPhaseWarnMs) return;          // only the interesting ones
    for (int i = 0; i < s_loopPhaseCount; i++) {
        if (s_loopPhases[i].name == name) {     // literals, so pointer compare
            if (ms > s_loopPhases[i].worstMs) s_loopPhases[i].worstMs = ms;
            s_loopPhases[i].overCount++;
            s_loopPhases[i].totalOverMs += ms;
            return;
        }
    }
    if (s_loopPhaseCount >= (int)(sizeof(s_loopPhases) / sizeof(s_loopPhases[0]))) return;
    s_loopPhases[s_loopPhaseCount++] = LoopPhaseStat{name, ms, 1, ms};
}

// Called at the end of loop(); prints only when the window caught something.
static void loopPhaseReport(uint32_t nowMs, uint32_t loopMs) {
    if (loopMs > s_loopWorstTotalMs) {
        s_loopWorstTotalMs = loopMs;
        s_loopWorstMeasuredMs = s_loopMeasuredThisPass;
    }
    if (s_loopReportLastMs == 0) s_loopReportLastMs = nowMs;
    if ((uint32_t)(nowMs - s_loopReportLastMs) < kLoopReportMs) return;
    s_loopReportLastMs = nowMs;

    // Print when any phase blew the budget, and also when the iteration as a
    // whole did even though nothing single-handedly caused it — a loop that is
    // slow from many small costs looks healthy phase by phase.
    if (s_loopPhaseCount > 0 || s_loopWorstTotalMs >= kLoopPhaseWarnMs) {
        Serial.printf("[loop] worst %lums (%lums in phases, %lums unmeasured) | over %lums:",
                      (unsigned long)s_loopWorstTotalMs,
                      (unsigned long)s_loopWorstMeasuredMs,
                      (unsigned long)(s_loopWorstTotalMs > s_loopWorstMeasuredMs
                                      ? s_loopWorstTotalMs - s_loopWorstMeasuredMs : 0),
                      (unsigned long)kLoopPhaseWarnMs);
        for (int i = 0; i < s_loopPhaseCount; i++) {
            Serial.printf(" %s=%lums x%lu (%lums total)",
                          s_loopPhases[i].name,
                          (unsigned long)s_loopPhases[i].worstMs,
                          (unsigned long)s_loopPhases[i].overCount,
                          (unsigned long)s_loopPhases[i].totalOverMs);
        }
        if (s_loopPhaseCount == 0) Serial.print(" (no single phase over budget)");
        Serial.println();
    }
    s_loopPhaseCount = 0;
    s_loopWorstTotalMs = 0;
    s_loopWorstMeasuredMs = 0;
}

// Wraps a statement and records how long it took. __VA_ARGS__ so assignments
// and multi-argument calls pass through unchanged.
#define LOOP_PHASE(tag, ...) do {                    \
        uint32_t _lp0 = millis();                    \
        __VA_ARGS__;                                 \
        loopPhaseNote(tag, millis() - _lp0);         \
    } while (0)
#else
#define LOOP_PHASE(tag, ...) do { __VA_ARGS__; } while (0)
static inline void loopPhaseBeginPass() {}
static inline void loopPhaseNote(const char *, uint32_t) {}
static inline void loopPhaseReport(uint32_t, uint32_t) {}
#endif



// ── Pre-sleep dim ────────────────────────────────────────────────────────────
// The backlight is usually the largest single draw while the screen is on, so
// the tail of the idle timeout is worth reclaiming. Doubles as a warning that
// the screen is about to sleep, which is why it is a visible step down rather
// than a gradual fade.
static constexpr uint32_t kPreSleepDimMs  = 5000;  // dim for the last 5 s
static constexpr uint8_t  kPreSleepDimPct = 25;    // % of the configured level
static bool s_preSleepDimmed = false;

#if defined(DEVICE_MESH_DECK)
// One shared handle for expander 0x59. It carries both the FT6636 reset line
// (P13) and the front buttons (P00-P07), and Aw9523::begin() issues a soft
// reset — calling it a second time would drop P13 and put the touch controller
// straight back into reset, so it is opened exactly once here.
static Aw9523 s_mdButtons;
static bool   s_mdButtonsReady = false;
static uint8_t s_mdBtnPrev = 0xFF;   // active-low; all released
static char    s_mdPendingKey = KEY_NONE;   // D-pad press awaiting the key pump

// Pulls the FT6636 out of reset. Its RST line is expander 0x59 P13, held low at
// power-up, so the controller is mute until this runs — which has to be before
// LovyanGFX probes the touch bus in lcd.init().
//
// Sequence and timings follow the wadamesh port (GPL-3.0-or-later, as here):
// put P13 in GPIO mode, make it an output, drive it low, then release. The
// 300 ms tail is the FT6636's own boot; a shorter wait and the first probe
// still finds nothing.
// Frees an I2C bus left stuck by a reset.
//
// A CPU reset can land in the middle of a transfer, and a slave that was mid-
// byte keeps driving SDA low waiting for clocks that never come. Every later
// transfer then fails or times out, and nothing recovers it — except removing
// power, which is why the device needs an off/on after a flash but not after a
// clean boot. This board scans its keyboard matrix continuously, so a reset
// landing mid-transaction is likely rather than exotic.
//
// The fix is the standard one: clock SCL by hand until the slave finishes the
// byte it thinks it is sending and lets SDA go, then issue a STOP.
static void meshDeckI2cBusRecover() {
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    const bool sdaLow = (digitalRead(I2C_SDA) == LOW);

    // Clock unconditionally rather than only when SDA reads low. A slave caught
    // mid-byte releases SDA for every 1 bit it is shifting out, so a high SDA
    // does not mean the bus is idle — it may simply be between low bits. That
    // is what the earlier check got wrong: it skipped recovery, and the next
    // transfer to the chip that was mid-byte timed out (ESP_ERR_TIMEOUT 263).
    //
    // Nine clocks with no START are ignored by an idle slave, and the whole
    // sequence costs about 100 us, so running it every boot is free insurance.
    pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SCL, HIGH);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
    }

    // STOP condition: SDA released while SCL is high.
    pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA, HIGH);
    delayMicroseconds(5);

    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    if (sdaLow || digitalRead(I2C_SDA) != HIGH) {
        Serial.printf("[meshdeck] I2C recovery: sda was %s, now %s\n",
                      sdaLow ? "LOW" : "high",
                      digitalRead(I2C_SDA) == HIGH ? "released" : "STILL LOW");
    }
}

static void meshDeckReleaseTouchReset() {
    Aw9523 &exp = s_mdButtons;
    if (!exp.begin(TOUCH_RST_EXPANDER, Wire)) {
        Serial.printf("[meshdeck] touch reset expander 0x%02X not found\n",
                      TOUCH_RST_EXPANDER);
        return;
    }
    s_mdButtonsReady = true;   // same chip backs the front buttons
    // begin() already put both ports in GPIO mode and made them inputs.
    constexpr uint8_t kP13 = (1u << 3);
    uint8_t cfg = 0xFF, out = 0xFF;
    exp.readReg(Aw9523::REG_CONFIG1, cfg);
    exp.readReg(Aw9523::REG_OUTPUT1, out);

    exp.writeReg(Aw9523::REG_OUTPUT1, (uint8_t)(out & ~kP13));   // assert reset
    exp.writeReg(Aw9523::REG_CONFIG1, (uint8_t)(cfg & ~kP13));   // P13 -> output
    delay(10);
    exp.writeReg(Aw9523::REG_OUTPUT1, (uint8_t)(out | kP13));    // release
    delay(300);                                                  // controller boot

    // Report whether the controller actually came up. LovyanGFX disables touch
    // silently when its probe inside lcd.init() finds nothing, so without this
    // a dead panel and a mis-wired one look identical from the outside.
    Wire.beginTransmission((uint8_t)TOUCH_ADDR);
    const bool acked = (Wire.endTransmission() == 0);
    Serial.printf("[meshdeck] FT6636 out of reset, 0x%02X %s\n",
                  (unsigned)TOUCH_ADDR, acked ? "ACK" : "NO ACK");
    if (!acked) {
        // Everything that answers, so a wrong address or a dead bus is obvious.
        Serial.print("[meshdeck] I2C scan:");
        for (uint8_t a = 0x08; a < 0x78; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
        }
        Serial.println();
    }
}
#endif

#if defined(DEVICE_TDECK) && HAS_TOUCH && !SCREEN_WAKE_FROM_TOUCH
// One-shot boot diagnostic. LilyGO has shipped T-Deck boards whose GT911 latches
// a different I2C address (0x14 vs 0x5D) depending on the INT level at power-on,
// and the panel that comes up at 0x5D does not resume scanning after the
// 0x8040=0x05 deep-sleep command — with no reset line (TOUCH_RST == -1) there is
// no way to revive it, so setTouchSleep() no longer issues that command (see
// below). This probe stays as a quick way to see what a given board actually is:
// it prints every address that ACKs on the shared
// touch/keyboard bus, flags the ones that map to known controllers, then tries
// to read the GT911 product-ID + firmware registers. Runs through Wire because
// the bus is already shared with the keyboard on the same pins.

static void probeTouchController() {
    Serial.println("[touch-probe] scanning shared I2C bus...");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;
        const char *hint = (addr == 0x5D || addr == 0x14) ? " GT911?"
                         : (addr == 0x1A)                  ? " CST328?"
                         : (addr == 0x15)                  ? " CST816?"
                         : (addr == 0x38)                  ? " FT6x36?"
                         : (addr == KB_ADDR)               ? " keyboard"
                                                           : "";
        Serial.printf("[touch-probe]   ACK 0x%02X%s\n", addr, hint);
        ++found;
    }
    if (!found) Serial.println("[touch-probe]   nothing ACKed");

    Wire.beginTransmission((uint8_t)TOUCH_ADDR);
    Wire.write(0x81);
    Wire.write(0x40);
    if (Wire.endTransmission() == 0
        && Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)6) == 6) {
        char id[5] = {0};
        for (int i = 0; i < 4; ++i) id[i] = (char)Wire.read();
        const uint8_t fwL = (uint8_t)Wire.read();
        const uint8_t fwH = (uint8_t)Wire.read();
        Serial.printf("[touch-probe] GT911 @0x%02X id=\"%s\" fw=0x%02X%02X\n",
                      (uint8_t)TOUCH_ADDR, id, fwH, fwL);
    } else {
        Serial.printf("[touch-probe] no GT911 product-ID at 0x%02X"
                      " (likely a different controller)\n", (uint8_t)TOUCH_ADDR);
    }
}
#endif

// Puts the touch controller into its own low-power mode alongside the panel.
// Only safe where touch is not a wake gesture: Touch_GT911::sleep() drives the
// INT pin as an output, which is exactly the line serviceTouchWakeWhileAsleep()
// and the light-sleep wake path read. On boards where a tap must wake the
// display this would silently break it, so it is gated on the same policy flag.
static void setTouchSleep(bool asleep) {
#if HAS_TOUCH && !SCREEN_WAKE_FROM_TOUCH
#if defined(DEVICE_TDECK_PRO)
    // CST328/CST3530 stays available as a wake source on this board profile.
    (void)asleep;
#else
    lgfx::ITouch *t = displayDev().touch();
    if (!t) return;
#if defined(DEVICE_TDECK) || defined(DEVICE_WIO_TRACKER_L2)
    // Deliberately do NOT issue the GT911 deep-sleep (0x8040=0x05) here. The
    // T-Deck has no touch reset line, and at least one panel revision (the unit
    // that latches I2C address 0x5D) never resumes coordinate scanning from an
    // INT-pulse wake afterwards — it still ACKs I2C, so it looks awake, but
    // reports no touches. Skipping the command leaves the GT911 in its own
    // low-power idle scanning, which recovers cleanly and costs only a little
    // current while the screen is off. Nothing to wake.
    //
    // The Wio Tracker L2 is the same trap by a different route: its INT and
    // RST both hang off the PCA9555 rather than a GPIO, so cfg.pin_int is -1
    // and LovyanGFX has no line to pulse to bring the controller back.
    // Sleeping it would be one-way.
    (void)t;
    (void)asleep;
#else
    if (asleep) t->sleep();
    else        t->wakeup();
#endif
#endif
#else
    LV_UNUSED(asleep);
#endif
}

// Pushes the brightness the device should be at right now. Used at boot, on
// wake, and live while the slider moves; sleepScreen() still drives the
// backlight to 0.
//
// Which level that is depends on what is on the panel: the lock screen has its
// own, usually much lower, because it is a glance surface rather than something
// anyone reads for minutes. Deciding it here rather than at the call sites is
// what keeps a config save made *while the lock screen is up* -- from web
// config, say -- from quietly restoring full brightness underneath it.
static void applyBrightness() {
    uint8_t pct = s_cfg.brightness;
#if FEATURE_LOCK_SCREEN
    if (s_lockScreenActive) pct = s_cfg.lockScreenBrightness;
#endif
    displayDev().setBrightness(cfgBrightnessDuty(cfgCoerceBrightness(pct)));
}

// ── Panel orientation ────────────────────────────────────────────────────────
// Which way up this board runs. A setting on the boards that can do both, and a
// compile-time constant everywhere else — so every call site below reads the
// same way regardless, and only the board with a choice pays for one.
//
// Deliberately NOT read out of the settings blob. That blob is unpacked in
// loadConfigFromSd(), roughly two hundred lines after the panel has already
// been initialised and rotated, and rotation is the one setting that has to be
// known before the display comes up. So this rides in its own NVS key, exactly
// as wifiForceAp, webCfgEnabled and vncEnabled do, and for the same reason —
// see the note beside them in persistConfigToPrefs().
//
// Changing it takes a reboot, which is what makes a plain cached bool correct
// here: nothing can move it between boots, so no consumer has to re-read it.
#if HAS_RUNTIME_ORIENTATION
// 0 = landscape, 1 = portrait, 2 = portrait turned 180 degrees. A code rather
// than a bool because there are three answers now -- but only the panel
// rotation tells 1 and 2 apart: both are the same 240x320 shape, so every
// layout decision in this file still asks the yes/no question through
// uiPortrait() and none of them had to learn about the second portrait.
enum : uint8_t {
    UI_ORIENT_LANDSCAPE    = 0,
    UI_ORIENT_PORTRAIT     = 1,
    UI_ORIENT_PORTRAIT_180 = 2,
    UI_ORIENT_COUNT        = 3,
};
static uint8_t s_uiOrient = (ORIENTATION_SEED_PORTRAIT != 0) ? UI_ORIENT_PORTRAIT
                                                             : UI_ORIENT_LANDSCAPE;

static inline bool uiPortrait() { return s_uiOrient != UI_ORIENT_LANDSCAPE; }

// What the panel is actually turned to. The only consumer is the setRotation()
// call in setup(); everything else wants the shape, not the angle.
static inline uint8_t uiRotationValue() {
    switch (s_uiOrient) {
        case UI_ORIENT_PORTRAIT:     return TFT_ROTATION_PORTRAIT;
        case UI_ORIENT_PORTRAIT_180: return TFT_ROTATION_PORTRAIT_180;
        default:                     return TFT_ROTATION_LANDSCAPE;
    }
}

// One name per code, for the Config row, the confirm dialog and the boot log.
static inline const char *uiOrientName(uint8_t o) {
    switch (o) {
        case UI_ORIENT_PORTRAIT:     return TR("Portrait");
        case UI_ORIENT_PORTRAIT_180: return TR("Portrait 180");
        default:                     return TR("Landscape");
    }
}

// Called before lcd.init(). Reads the key, or — on a device that has never had
// one — takes this build's seed and writes it, so the flag becomes the device's
// own from the first boot and no later update can move it.
static void loadBootOrientation() {
    Preferences p;
    // Read/write, because this may have to seed. A failure leaves the seed in
    // place, which is the right answer for this boot and gets written next time.
    if (!p.begin("camillia", false)) {
        Serial.println("[orient] NVS unavailable; using build seed");
        return;
    }
    const uint8_t stored = p.getUChar("uiOrient", 0xFF);
#if defined(DEVICE_TDISPLAY_P4)
    // Early P4 test builds seeded landscape before this target became
    // portrait-first. If onboarding has not been completed, no user has had a
    // chance to choose an orientation yet, so replace that stale seed once.
    const bool onboardingIncomplete = !p.isKey("cfg") && !p.isKey("nodeLong");
    if (onboardingIncomplete && stored == UI_ORIENT_LANDSCAPE) {
        s_uiOrient = UI_ORIENT_PORTRAIT;
        p.putUChar("uiOrient", s_uiOrient);
        Serial.println("[orient] replaced pre-port stale landscape seed with Portrait");
        p.end();
        return;
    }
#endif
    if (stored == 0xFF) {
        p.putUChar("uiOrient", s_uiOrient);
        Serial.printf("[orient] no key yet - seeded %s from this build\n",
                      uiOrientName(s_uiOrient));
    } else {
        // Anything outside the enum degrades to portrait rather than being
        // ignored: every value this key has ever held that was not zero meant
        // portrait, so an older build meeting a newer code lands on the right
        // shape instead of silently flipping the panel back to landscape.
        s_uiOrient = (stored < UI_ORIENT_COUNT) ? stored
                   : (stored != 0 ? UI_ORIENT_PORTRAIT : UI_ORIENT_LANDSCAPE);
        Serial.printf("[orient] %s\n", uiOrientName(s_uiOrient));
    }
    p.end();
}
#else
static constexpr bool uiPortrait() { return DEVICE_UI_VERTICAL != 0; }
static inline void loadBootOrientation() {}
#endif

// ── Nav alignment: the nav bar as a side column ─────────────────────────────
// Left or Right (RhinoConfig::navAlign, Config -> Nav Alignment) turns the row
// along the bottom into a narrow column down that edge, its cells reduced to
// the glyph -- and, on the P4 with its keyboard, the F-key that reaches it --
// so every screen gets the full height back at the cost of this much width.
// Center is the row. It began on the pixel-doubled P4 in landscape, ~616 x
// ~228, where a row along the bottom spent the scarcest dimension; it is a
// choice on every board with the bar now.
//
// Read live: changing it rebuilds the screen, and nothing caches it.
#if UI_LARGE_PANEL_PROFILE
static constexpr int kSideNavW = 68;
#elif defined(DEVICE_TDISPLAY_P4)
static constexpr int kSideNavW = 34;
#elif defined(DEVICE_CROWPANEL_50)
// Wider than the bottom bar is tall (56), unlike the other boards' column, so
// its 24 px icons (navIconFont) have padding either side.
static constexpr int kSideNavW = 72;
#else
// The bottom bar's height on these boards (kBottomNavHeight): the same 28 px
// cell edge, turned on its side.
static constexpr int kSideNavW = 28;
#endif
// Where the column starts. Below the P4 panel's rounded top corner, which
// clipped the top cell when the column ran from y = 0 with only
// UI_CORNER_SAFE_X of padding; square panels start at the top. The bottom
// needs nothing: the P4's display stops short of that curve (UI_BOTTOM_SAFE_H).
#if defined(DEVICE_TDISPLAY_P4)
static constexpr int kSideNavTopY = 22 * (UI_LARGE_PANEL_PROFILE ? 2 : 1);
#else
static constexpr int kSideNavTopY = 0;
#endif

static inline bool navIsSideColumn() {
    return bottomNavEnabled() && cfgNavAlign(s_cfg) != NAV_ALIGN_CENTER;
}

static inline bool navSideLeft() {
    return navIsSideColumn() && cfgNavAlign(s_cfg) == NAV_ALIGN_LEFT;
}

// What the column takes off each side of the screen: 0 on the side it is not on.
static inline int navSideInsetLeft()  { return navSideLeft() ? kSideNavW : 0; }
static inline int navSideInsetRight() {
    return (navIsSideColumn() && !navSideLeft()) ? kSideNavW : 0;
}

// Whether channels live in the overlay dropdown (true) or a list anchored
// beside the chat, as on the Pager (false). A fixed property of every board
// (UI_CHANNEL_LIST_DROPDOWN) except the T-Display P4, where it follows the
// orientation chosen at boot: portrait is 284 px wide and keeps the dropdown;
// pixel-doubled landscape slides the same drawer in, and only the large-panel
// landscape layout keeps the Pager's anchored column.
//
// The P4 compiles the dropdown code (UI_CHANNEL_LIST_DROPDOWN is 1 there), and
// where it is anchored buildUi() turns that same list into the column. So the
// dropdown's open/close/measure paths are what have to stand down there, and
// they ask this.
static inline bool channelListIsDropdown() {
#if defined(DEVICE_TDISPLAY_P4)
    return uiPortrait() || !UI_LARGE_PANEL_PROFILE
#if HAS_CHANNEL_PANEL_LOCK
           || !cfgChannelPanelLocked(s_cfg)
#endif
           ;
#elif HAS_CHANNEL_PANEL_LOCK
    // Display > Channel Panel: locked docks it beside the chat (buildUi()).
    // Read at build time; the setting rebuilds the screen when it changes.
    return !cfgChannelPanelLocked(s_cfg);
#else
    return UI_CHANNEL_LIST_DROPDOWN != 0;
#endif
}

#if HAS_CHANNEL_PANEL_LOCK
// The locked panel's keyboard cursor (s_channelColumnFocus). Keyboard-first
// boards only: the touch-profile P4 keeps its own model, where Up/Down drive
// the docked list whenever the chat cursor is off.
static inline bool channelColumnDocked() {
#if UI_TOUCH_ONLY_PROFILE
    return false;
#else
    return !channelListIsDropdown() && s_channelList != nullptr;
#endif
}

static inline bool channelColumnFocused() {
    return s_channelColumnFocus && channelColumnDocked();
}

static void setChannelColumnFocus(bool on) {
    if (on && !channelColumnDocked()) on = false;
    s_channelColumnFocus = on;
    if (on) {
        if (s_cardputerDropdownSelection < 0 || s_cardputerDropdownSelection >= MESH_CHANNELS) {
            s_cardputerDropdownSelection = s_activeChannel;
        }
        int sel = s_cardputerDropdownSelection;
        if (sel >= 0 && sel < MESH_CHANNELS && s_channelBtns[sel]) {
            lv_obj_scroll_to_view(s_channelBtns[sel], LV_ANIM_OFF);
        }
    } else {
        s_cardputerDropdownSelection = -1;
    }
    refreshChannelGlow(true);
}

// Arrow keys: Left in the open drawer docks the panel, cursor on the row it
// had; Right from the chat undocks it, drawer closed. This is the Channel
// Panel setting itself, so Config and web config agree with it and it
// survives a reboot. A rebuild, as from Config: the chat's width changes.
static bool s_channelColumnFocusAfterRebuild = false;
static int  s_channelColumnFocusSel = -1;

static void setChannelPanelDockedFromKeys(bool docked, int focusSel = -1) {
    if (cfgChannelPanelLocked(s_cfg) == docked) return;
    if (s_pagerChatCursorMode) pagerExitChatCursorMode(true);
    if (isChannelDropdownVisible()) setChannelDropdownVisible(false);
    s_channelColumnFocus = false;
    cfgSetChannelPanelLocked(s_cfg, docked);
    persistConfigToPrefs();
    // Picked up by processPendingUiRebuild() once the new column exists. The
    // P4 needs none: its docked list takes Up/Down whenever the chat cursor
    // is off, which it now is.
    s_channelColumnFocusAfterRebuild = docked && !UI_TOUCH_ONLY_PROFILE;
    s_channelColumnFocusSel = focusSel;
    scheduleUiRebuild(false);
}
#endif

// The drawers' slide (the channel list's, and the DM screen's conversation
// list). Instant on the Pro, for the reason the dashboard carousel is
// (HAS_HOME_CAROUSEL_ANIM): every animated frame there is an e-paper refresh.
#if defined(DEVICE_TDECK_PRO)
#define HAS_CHANNEL_DRAWER_ANIM 0
#else
#define HAS_CHANNEL_DRAWER_ANIM 1
#endif
static constexpr uint32_t kChannelDrawerSlideMs = 180;

// The DM screen's conversations slide in the same way wherever the channels do:
// a drawer over the messages rather than a pane beside them. The Pager and the
// large-panel P4 in landscape keep the two panes, as they keep the channel
// column.
static inline bool dmListIsDrawer() {
    return channelListIsDropdown();
}

// ── Status icon ink ──────────────────────────────────────────────────────────
// The GPS and Wi-Fi icons say four things by colour: located, searching,
// connected, serving an AP. Which colours those are depends on the surface, not
// on the state, so the state logic (below, in paintStatusIcons) takes a palette
// rather than deciding one.
static inline lv_color_t headerGoodGreenColor() {
    return (s_cfg.uiMode == UI_MODE_LIGHT)
        ? lv_color_hex(0x2C7A3B)
        : lv_color_hex(0x84E07A);
}

static inline lv_color_t headerGpsBadColor() {
    uint16_t bad = s_ui.battBad;
    if (s_cfg.uiMode == UI_MODE_DARK) {
        bad = blend565(bad, 0xFFFF, 96);
    }
    return lvColorFrom565(bad);
}

// GPS and Wi-Fi are two icons with four states between them, and each state
// wants its own colour. Separate fields for the two "bad" inks because they are
// not the same red: the GPS one is derived from the theme's battery-empty
// colour, and the Wi-Fi one is fixed.
struct StatusIconInk {
    lv_color_t good;      // GPS holding a fix, Wi-Fi associated
    lv_color_t gpsBad;    // GPS off, or on without a fix
    lv_color_t wifiBad;   // Wi-Fi off or disconnected
    lv_color_t ap;        // Wi-Fi serving its own access point
};

// The themed chat header and its copy on the modal nav bar.
static inline StatusIconInk headerStatusInk() {
#if defined(DEVICE_TDECK_PRO)
    // E-paper: one ink, and the panel decides nothing else.
    const lv_color_t ink = lv_color_make(0, 0, 0);
    return StatusIconInk{ ink, ink, ink, ink };
#else
    return StatusIconInk{ headerGoodGreenColor(), headerGpsBadColor(),
                          lv_color_hex(0xFF6B6B), lv_color_hex(0xF4D35E) };
#endif
}

