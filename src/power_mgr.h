#pragma once
// Low-battery protection: the one place that decides when the device is too
// flat to keep running, and turns it off cleanly instead of letting it brown
// out.
//
// Without this, a cell at its knee does not stop the device — it browns out
// partway through boot, the load drops, the cell recovers a few tens of
// millivolts, and the whole thing repeats. Every cycle burns charge, so the
// loop tightens rather than settling, and it only ends when the pack's own
// protection FET opens or the cell is over-discharged.
//
// Three rules shape everything here, and all three exist because getting them
// wrong is worse than having no protection at all:
//
//   1. An unknown reading is never "flat". batteryReadVoltage() reports 0.0f
//      for "no answer" — the BQ25896 boards return it on the sample that starts
//      a conversion, and any board returns it before its first successful read.
//      A naive `if (v < 3.3) off()` would switch every device off on its first
//      boot sample.
//   2. One sample is never enough. A LoRa transmit or a backlight step sags the
//      rail by more than the gap between these tiers.
//   3. Never turn off while external power is present. A device shut down on a
//      charger that then refuses to boot far enough to charge is bricked until
//      someone disassembles it — a strictly worse outcome than the boot loop.
#include <Arduino.h>
#include "config.h"

enum PowerBatteryTier {
    POWER_TIER_UNKNOWN = 0,   // no valid reading yet; behaves as OK
    POWER_TIER_OK,
    POWER_TIER_WARN,          // BATT_WARN_V: tell the user, start nothing costly
    POWER_TIER_CRITICAL,      // BATT_CRITICAL_V: shed load to stay receiving
    POWER_TIER_CUTOFF,        // BATT_CUTOFF_V: flush and turn off
};

// What power_mgr needs the application to do at each stage. Supplied as hooks
// rather than called directly so this module stays free of the UI, the radio
// and the storage layer — it owns the decision, not the machinery.
//
// Every hook is optional; a null one is skipped. The boot gate runs before any
// of them can be set, which is deliberate: at that point nothing is initialised,
// so there is nothing to flush or shut down.
struct PowerMgrHooks {
    // Everything that must survive: config, channel transcripts, DMs, node db.
    // Nothing may start an NVS write after this returns.
    void (*flushPersistence)() = nullptr;
    // Drop the big consumers — Wi-Fi/web config, GPS, backlight, and anything
    // else that can wait. Called once on entry to CRITICAL. Must be safe to
    // call with the UI up.
    void (*shedLoads)() = nullptr;
    // Radio to sleep, panel and backlight off. Called immediately before the
    // board is powered down.
    void (*prepareForOff)() = nullptr;
    // Show the user why, if a panel is up and cheap to write to.
    void (*showMessage)(const char *msg) = nullptr;
    // Tier changed. For banners and for suppressing expensive actions.
    void (*tierChanged)(PowerBatteryTier tier) = nullptr;
};

void powerMgrSetHooks(const PowerMgrHooks &hooks);

// Called from setup() immediately after the power rail is raised and *before*
// the display, radio, Wi-Fi and GPS are brought up — that current step is what
// causes the brownout, so the decision has to happen ahead of it.
//
// Also carries the brownout backstop: a count of consecutive brownout resets
// held in RTC memory, which latches the device off if the gate somehow lets a
// loop start anyway. RTC memory survives a brownout reset but not a real power
// cycle, which is the behaviour we want — after the battery is physically
// removed and replaced we should re-evaluate from scratch.
void powerMgrBootGate();

// Serviced from loop(). Cheap: it reads the already-filtered voltage and does
// nothing at all until the debounce timers say otherwise.
void powerMgrService(uint32_t nowMs);

PowerBatteryTier powerMgrTier();

// True when the battery is low enough that expensive, interruptible work should
// not be started — an OTA download, a map tile fetch. WARN and below.
bool powerMgrShouldDeferHeavyWork();

// External power present. False when absent *and* when this board cannot tell;
// powerMgrChargerSenseAvailable() separates the two.
bool powerMgrChargerPresent();
bool powerMgrChargerSenseAvailable();

// Flush, announce, and turn the board off by the best means it has: release the
// power latch where there is one, else open the charger's battery FET, else
// deep sleep with only the user button armed. Does not return.
void powerMgrShutdown(const char *reason);

// True when this boot follows a low-battery shutdown, so the UI can say so
// rather than leaving the user wondering why the device switched itself off.
// Consumed on the first call.
bool powerMgrConsumeLowBatteryBootNotice();
