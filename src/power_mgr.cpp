#include "power_mgr.h"

#include "battery_util.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/rtc_io.h>

// ── State that has to outlive a reset ────────────────────────────────────────
// RTC slow memory survives a brownout reset and does not survive a real power
// cycle, which is exactly the lifetime these want: a brownout loop must be
// remembered across its own resets, and pulling the battery must clear it so a
// device with a fresh cell re-evaluates from scratch.
//
// Magic-guarded because RTC memory is uninitialised on a cold boot: without it,
// whatever bytes happened to be there would read as a brownout count.
static const uint32_t kPowerRtcMagic = 0x504D4731UL;   // "PMG1"
RTC_DATA_ATTR static uint32_t s_pmRtcMagic = 0;
RTC_DATA_ATTR static uint32_t s_pmBrownouts = 0;
RTC_DATA_ATTR static uint32_t s_pmLowBattOff = 0;

// Three consecutive brownout resets is the backstop for the case the boot gate
// misses — a cell that reads fine at rest and collapses the moment the radio
// and backlight come up. Two would risk latching off a device that browned out
// once for an unrelated reason.
static const uint32_t kBrownoutLatchCount = 3;
// Uptime after which a boot counts as healthy and the brownout run is cleared.
// Long enough to be past display, radio, Wi-Fi and GPS bring-up, which is the
// current step that causes the resets in the first place.
static const uint32_t kHealthyUptimeMs = 60000UL;

static PowerMgrHooks s_hooks;
static PowerBatteryTier s_tier = POWER_TIER_UNKNOWN;
static bool s_shedDone = false;
static bool s_brownoutRunCleared = false;
static uint32_t s_tierCandidateSinceMs = 0;
static PowerBatteryTier s_tierCandidate = POWER_TIER_UNKNOWN;
static uint32_t s_lastSampleMs = 0;

// A tier must hold for this long before it is acted on. The transient sag from
// a transmit burst or a backlight step is larger than the gap between tiers, so
// without this the device would shut down mid-sentence on a healthy cell.
static const uint32_t kTierHoldMs = 30000UL;
// The warning tier is only a banner, so it can appear promptly.
static const uint32_t kWarnHoldMs = 5000UL;
static const uint32_t kSampleIntervalMs = 1000UL;

static void pmRtcInit() {
    if (s_pmRtcMagic == kPowerRtcMagic) return;
    s_pmRtcMagic = kPowerRtcMagic;
    s_pmBrownouts = 0;
    s_pmLowBattOff = 0;
}

void powerMgrSetHooks(const PowerMgrHooks &hooks) { s_hooks = hooks; }

PowerBatteryTier powerMgrTier() { return s_tier; }

bool powerMgrShouldDeferHeavyWork() {
    return s_tier == POWER_TIER_WARN || s_tier == POWER_TIER_CRITICAL
           || s_tier == POWER_TIER_CUTOFF;
}

bool powerMgrChargerSenseAvailable() {
#if POWER_HAS_CHARGER_SENSE
    bool known = false;
    (void)batteryExternalPowerPresent(&known);
    return known;
#else
    return false;
#endif
}

bool powerMgrChargerPresent() {
#if POWER_HAS_CHARGER_SENSE
    bool known = false;
    const bool present = batteryExternalPowerPresent(&known);
    return known && present;
#else
    return false;
#endif
}

bool powerMgrConsumeLowBatteryBootNotice() {
    pmRtcInit();
    if (!s_pmLowBattOff) return false;
    s_pmLowBattOff = 0;
    return true;
}

// ── Shutdown ─────────────────────────────────────────────────────────────────

static void pmBoardOff() {
    // Best available, in order of how completely each one stops the drain.
#if defined(BOARD_POWERON) && (BOARD_POWERON >= 0)
    // A real latch: the rail is held up by this pin, so releasing it is a
    // genuine power-off and the charger keeps charging underneath.
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, LOW);
    delay(300);
    // Still here means the rail is being held up by something else (USB, most
    // likely), so fall through rather than spin.
#endif

    // A true battery disconnect on the BQ25896 boards, which recovers by
    // itself when USB is plugged in.
    if (batteryHardwarePowerOffSupported() && batteryHardwarePowerOff()) {
        delay(300);
    }

    // Last resort. Microamps rather than zero, but it stops the boot loop and
    // stops the meaningful drain, and it is the only option on the boards with
    // neither a latch nor a charger FET.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
#if defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)
    // Arm the button so the device can be brought back deliberately. Only RTC
    // GPIOs can wake from deep sleep; on a pin that cannot, this is skipped and
    // recovery is via USB or a battery pull.
    if (rtc_gpio_is_valid_gpio((gpio_num_t)USER_BUTTON_PIN)) {
        esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN,
                                     (USER_BUTTON_ACTIVE_LEVEL == LOW) ? 0 : 1);
    }
#endif
    esp_deep_sleep_start();
}

void powerMgrShutdown(const char *reason) {
    Serial.printf("[power] shutting down: %s\n", reason ? reason : "unspecified");
    Serial.flush();

    pmRtcInit();
    // Recorded before anything that can fail, so the next boot can say why the
    // device switched itself off even if the rest of this sequence goes wrong.
    s_pmLowBattOff = 1;
    // The run is over — this is a deliberate stop, not another brownout. Left
    // set and the next boot would latch immediately on a charged battery.
    s_pmBrownouts = 0;

    if (s_hooks.showMessage) s_hooks.showMessage("Battery empty\nShutting down");
    // Persistence before power: this is the clean shutdown path that never
    // existed, and it is the whole reason a brownout loses data a manual reboot
    // would have kept.
    if (s_hooks.flushPersistence) s_hooks.flushPersistence();
    if (s_hooks.prepareForOff) s_hooks.prepareForOff();

    Serial.flush();
    pmBoardOff();
    // pmBoardOff() does not return. If it somehow does, spin rather than
    // continue into a boot that is about to brown out again.
    for (;;) delay(1000);
}

// ── Boot gate ────────────────────────────────────────────────────────────────

void powerMgrBootGate() {
    pmRtcInit();

    const esp_reset_reason_t why = esp_reset_reason();
    if (why == ESP_RST_BROWNOUT) {
        s_pmBrownouts++;
        Serial.printf("[power] brownout reset (%lu in a row)\n",
                      (unsigned long)s_pmBrownouts);
        if (s_pmBrownouts >= kBrownoutLatchCount) {
            // Do not attempt the rest of boot: bringing the display and radio
            // up is the current step that causes the reset, so trying again
            // just spends more of a cell that is already too low.
            powerMgrShutdown("brownout loop");
        }
    } else if (why != ESP_RST_DEEPSLEEP && why != ESP_RST_SW) {
        // A cold start or an external reset: whatever run was in progress is
        // over. Deep-sleep and software resets are left alone because they are
        // how this module and the OTA path restart the device on purpose.
        s_pmBrownouts = 0;
    }

#if !HAS_BATTERY_SENSE
    // Nothing to measure, so there is nothing to protect. Said out loud rather
    // than silently doing nothing, because "the cutoff did not fire" on such a
    // board is a hardware fact and not a bug to chase.
    Serial.println("[power] no battery sense on this build; low-battery cutoff disabled");
#else
    batteryInitAdc();

    // Poll rather than take one reading. The BQ25896 boards answer 0.0f on the
    // sample that starts a conversion, the wio-tracker's ADS1115 needs its
    // sense rail enabled, and any board can miss a sample on a busy bus. An
    // unknown is not a low battery.
    int valid = 0;
    int low = 0;
    for (int attempt = 0; attempt < 12 && valid < 3; attempt++) {
        const float v = batteryReadVoltageNow();
        if (v > 0.0f) {
            valid++;
            if (v < (float)BATT_BOOT_GATE_V) low++;
            else low = 0;   // one healthy reading clears the run
        }
        delay(80);
    }

    if (valid == 0) {
        Serial.println("[power] boot gate: battery did not answer; continuing");
        return;
    }
    if (low < 3) return;

    // Charger last, because it is the most expensive check and only matters
    // once everything else says "shut down". Never turn off on external power:
    // a device that will not boot far enough to charge is worse than a loop.
    if (powerMgrChargerPresent()) {
        Serial.println("[power] boot gate: battery low but charger present; continuing");
        return;
    }

    Serial.println("[power] boot gate: battery below cutoff");
    powerMgrShutdown("battery empty at boot");
#endif
}

// ── Runtime monitor ──────────────────────────────────────────────────────────

#if HAS_BATTERY_SENSE
// Which tier a voltage sits in, with hysteresis applied against the tier we are
// already in so a device on charge does not oscillate across a threshold.
static PowerBatteryTier pmTierFor(float v, PowerBatteryTier current) {
    const float hyst = (float)BATT_RECOVER_HYST_V;
    // Leaving a tier needs the extra margin; entering one does not.
    const float cutoff   = (float)BATT_CUTOFF_V   + ((current == POWER_TIER_CUTOFF) ? hyst : 0.0f);
    const float critical = (float)BATT_CRITICAL_V + ((current == POWER_TIER_CRITICAL
                                                      || current == POWER_TIER_CUTOFF) ? hyst : 0.0f);
    const float warn     = (float)BATT_WARN_V     + ((current == POWER_TIER_OK
                                                      || current == POWER_TIER_UNKNOWN) ? 0.0f : hyst);
    if (v < cutoff)   return POWER_TIER_CUTOFF;
    if (v < critical) return POWER_TIER_CRITICAL;
    if (v < warn)     return POWER_TIER_WARN;
    return POWER_TIER_OK;
}
#endif

void powerMgrService(uint32_t nowMs) {
#if !HAS_BATTERY_SENSE
    (void)nowMs;
#else
    // Clear a brownout run once this boot has proved itself. Without it the
    // count would accumulate across unrelated resets days apart and eventually
    // latch a perfectly healthy device off.
    if (!s_brownoutRunCleared && nowMs >= kHealthyUptimeMs) {
        s_brownoutRunCleared = true;
        if (s_pmBrownouts != 0) {
            Serial.println("[power] uptime healthy; brownout run cleared");
            s_pmBrownouts = 0;
        }
    }

    if ((uint32_t)(nowMs - s_lastSampleMs) < kSampleIntervalMs) return;
    s_lastSampleMs = nowMs;

    // The filtered reading, not a forced one: this runs beside the UI and the
    // radio, and the smoothing is what keeps a transmit sag out of the decision.
    const float v = batteryReadVoltage();
    if (v <= 0.0f) return;   // no answer is not a low battery

    const PowerBatteryTier want = pmTierFor(v, s_tier);

    // On external power nothing may escalate. Recovery is still allowed, so a
    // charging device climbs back out of WARN and CRITICAL as it should.
    if (want > s_tier && powerMgrChargerPresent()) {
        s_tierCandidate = s_tier;
        s_tierCandidateSinceMs = nowMs;
        return;
    }

    if (want != s_tierCandidate) {
        s_tierCandidate = want;
        s_tierCandidateSinceMs = nowMs;
        return;
    }
    if (want == s_tier) return;

    // Dropping to a lower tier applies at once; climbing to a worse one has to
    // hold, because that is the direction with consequences.
    const bool worse = (want > s_tier);
    const uint32_t hold = !worse ? 0
                        : (want == POWER_TIER_WARN ? kWarnHoldMs : kTierHoldMs);
    if ((uint32_t)(nowMs - s_tierCandidateSinceMs) < hold) return;

    const PowerBatteryTier previous = s_tier;
    s_tier = want;
    Serial.printf("[power] battery %.2f V: tier %d -> %d\n", (double)v, (int)previous, (int)want);
    if (s_hooks.tierChanged) s_hooks.tierChanged(s_tier);

    if (s_tier == POWER_TIER_CRITICAL && !s_shedDone) {
        // Once per run down. Shedding again on every re-entry would fight the
        // user turning Wi-Fi back on.
        s_shedDone = true;
        Serial.println("[power] critical: shedding loads");
        if (s_hooks.shedLoads) s_hooks.shedLoads();
    }
    if (s_tier == POWER_TIER_CUTOFF) {
        powerMgrShutdown("battery below cutoff");
    }
#endif
}
