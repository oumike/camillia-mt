#pragma once
// ════════════════════════════════════════════════════════════════════════════
// ble_keyboard.h — external Bluetooth (BLE HID) keyboard as an input source
//
// The device acts as the HOGP *host*: it scans for peripherals advertising the
// HID service (0x1812), connects out to the one the user picks, bonds, and
// turns the keyboard's input reports into ordinary KEY_* codes that
// pumpKeyboardInput() merges with every other input source. Nothing downstream
// of that merge knows a key came from Bluetooth.
//
// This is the opposite role from the btEnabled/btMode/btFixedPin fields in
// RhinoConfig, which describe the device as a *peripheral* a phone pairs to.
// Those are inert and unrelated; do not reuse them here.
//
// Every function is safe to call whether or not the stack is running, and all
// of them are safe from the UI thread: the blocking parts (scan, connect, GATT
// discovery) run on a worker task and are driven by request/poll, never by
// blocking the caller.
//
// Only compiled where HAS_BLE_KEYBOARD is 1 (see hal/board.h); callers guard
// their calls with the same macro.
// ════════════════════════════════════════════════════════════════════════════
#include <stdint.h>
#include <stddef.h>

#include "config.h"   // HAS_BLE_KEYBOARD

// What the host is doing right now. Reported to the UI verbatim; the human
// wording lives in bleKeyboardStatusText().
enum BleKbdState : uint8_t {
    BLE_KBD_OFF = 0,      // stack down, nothing running
    BLE_KBD_IDLE,         // stack up, no keyboard remembered
    BLE_KBD_SCANNING,     // looking for advertisers
    BLE_KBD_CONNECTING,   // connecting/bonding to the chosen keyboard
    BLE_KBD_CONNECTED,    // link up, reports arriving
    BLE_KBD_RETRYING,     // remembered keyboard out of range; backing off
    BLE_KBD_FAILED,       // last attempt failed for a reason worth showing
};

// Brings the module up with the persisted settings. Safe to call once, from
// setup(), before the stack has ever run: with `enabled` false it does nothing
// at all beyond remembering the pairing, so a board that never turns the
// feature on never pays the RAM.
void bleKeyboardBegin(bool enabled, const char *addr, uint8_t addrType, const char *name);

// Starts or stops the stack. Returns the state it ended up in, which is what
// the caller should persist — a failed start reports false rather than
// leaving the setting claiming a stack that is not there.
bool bleKeyboardSetEnabled(bool on);
bool bleKeyboardEnabled();

// Called every loop pass. Synthesizes auto-repeat (BLE keyboards notify on
// press and release and send nothing in between, so repeat is the host's job)
// and nothing else — the link itself is the worker's business.
void bleKeyboardService(uint32_t nowMs);

// Pops one translated key, KEY_* or ASCII. False when the queue is empty.
bool bleKeyboardPopKey(char *outKey);

// The key currently held down and for how long, for long-press behaviours.
// Real press/release from the wire, not a heuristic.
char     bleKeyboardHeldKey();
uint32_t bleKeyboardHeldMs();

uint8_t bleKeyboardState();
bool    bleKeyboardConnected();

// True whenever the radio is up in any capacity. Light sleep must not run
// while this holds: a nap stops servicing the controller and drops the link.
bool bleKeyboardActive();

// One line for the settings row / modal, e.g. "Connected: Keychron K2 Pro".
void bleKeyboardStatusText(char *out, size_t outLen);

// The passkey to show while a first-time pairing is in flight, or 0 when there
// is nothing to show. Many BLE keyboards ask for a code to be typed on the
// keyboard itself and confirmed with Enter; the number has to come from us.
uint32_t bleKeyboardPasskey();

// Keyboard's own battery level (HOGP devices expose 0x180F), 0xFF if unknown.
uint8_t bleKeyboardBatteryPct();

// ── Pairing ─────────────────────────────────────────────────────────────────
// Kicks off a scan. Returns false if the stack is not up or one is running.
bool bleKeyboardStartScan();
bool bleKeyboardScanning();
int  bleKeyboardScanCount();
// Copies one scan result out. `hid` reports whether the device advertised the
// HID service outright or only looked like a keyboard by appearance.
bool bleKeyboardScanEntry(int idx, char *name, size_t nameLen,
                          char *addr, size_t addrLen, int *rssi, bool *hid);
// Connects to a scan result and remembers it.
bool bleKeyboardPairWith(int idx);
// Drops the bond and the remembered address.
void bleKeyboardForget();

bool        bleKeyboardHasPaired();
const char *bleKeyboardPairedName();
const char *bleKeyboardPairedAddr();
uint8_t     bleKeyboardPairedAddrType();

// True while the BLE stack needs Wi-Fi modem sleep left on. Wi-Fi/BT
// coexistence does not work without it, and turning it off around a live BT
// controller aborts inside coex_core_enable() -- so web config asks before
// disabling power save for its own latency. Declared for every board, and
// answered inline as false where the feature is not compiled in, so callers do
// not each have to guard.
#if HAS_BLE_KEYBOARD
bool bleKeyboardHoldsWifiModemSleep();
#else
inline bool bleKeyboardHoldsWifiModemSleep() { return false; }
#endif

// True once, after the worker changed the remembered pairing. The caller
// copies the pairing into config and persists it; polled rather than pushed so
// the flash write happens on the main loop, not on the BLE task.
bool bleKeyboardTakePairingDirty();
