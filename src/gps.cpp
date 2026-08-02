#include "gps.h"
#include "config.h"
#include "debug_flags.h"
#include <TinyGPSPlus.h>
#include <Wire.h>

#if defined(DEVICE_TLORA_PAGER_TFT)
#include "hal/xl9555.h"
namespace {

// GPS rail management for the T-LoRa Pager using the XL9555 GPIO expander.
// The expander controls GPS power-enable, reset, and shared GPIO rail.

static bool pagerPrimeGpsRails(bool invertDirSense) {
    Wire.begin(KB_SDA, KB_SCL);
    int expAddr = xl9555FindAddr();
    if (expAddr < 0) {
        Serial.println("[gps] pager expander not found (0x20-0x27)");
        return false;
    }

    uint8_t out0 = 0xFF, out1 = 0xFF, cfg0 = 0xFF, cfg1 = 0xFF;
    if (!xl9555ReadAll((uint8_t)expAddr, out0, out1, cfg0, cfg1)) {
        Serial.printf("[gps] pager expander read failed addr=0x%02X\n", expAddr);
        return false;
    }

    if (invertDirSense) {
        // Some board revisions have the direction-sense polarity inverted.
        // When invertDirSense=true, swap the cfg bit logic so the rails still
        // come up in the correct state on those units.
        xl9555SetInput(XL9555_PIN_GPS_EN,  cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_GPIO_EN, cfg0, cfg1);
        xl9555SetInput(XL9555_PIN_GPS_RST, cfg0, cfg1);
    } else {
        // Standard polarity: drive GPS_EN and GPIO_EN high, pulse GPS_RST low→high.
        xl9555SetOutput(XL9555_PIN_GPS_EN,  true,  out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_GPIO_EN, true,  out0, out1, cfg0, cfg1);
        xl9555SetOutput(XL9555_PIN_GPS_RST, false, out0, out1, cfg0, cfg1);
    }

    if (!xl9555WriteAll((uint8_t)expAddr, out0, out1, cfg0, cfg1)) {
        Serial.printf("[gps] pager expander write failed addr=0x%02X invert=%d\n",
                      expAddr, invertDirSense ? 1 : 0);
        return false;
    }

    delay(20);  // Hold reset low briefly to ensure the GPS module latches it

    // Release reset high
    xl9555SetOutput(XL9555_PIN_GPS_RST, true, out0, out1, cfg0, cfg1);
    (void)xl9555WriteReg((uint8_t)expAddr, XL9555_REG_OUT0, out0);
    (void)xl9555WriteReg((uint8_t)expAddr, XL9555_REG_OUT1, out1);

    Serial.printf("[gps] pager rails primed addr=0x%02X invert=%d\n",
                  expAddr, invertDirSense ? 1 : 0);
    return true;
}

// Drives GPS_EN only, read-modify-write so the other rails on the expander keep
// whatever state the radio's own priming left them in. Used to actually cut
// power when GPS is turned off, rather than just closing the UART on a module
// that keeps drawing ~20-25 mA.
static bool pagerSetGpsRail(bool on) {
    Wire.begin(KB_SDA, KB_SCL);
    int expAddr = xl9555FindAddr();
    if (expAddr < 0) {
        Serial.println("[gps] pager expander not found - cannot switch GPS rail");
        return false;
    }

    uint8_t out0 = 0xFF, out1 = 0xFF, cfg0 = 0xFF, cfg1 = 0xFF;
    if (!xl9555ReadAll((uint8_t)expAddr, out0, out1, cfg0, cfg1)) return false;
    xl9555SetOutput(XL9555_PIN_GPS_EN, on, out0, out1, cfg0, cfg1);
    if (!xl9555WriteAll((uint8_t)expAddr, out0, out1, cfg0, cfg1)) return false;

    Serial.printf("[gps] pager GPS rail %s\n", on ? "on" : "off");
    return true;
}
} // namespace
#endif

// Minimum ms after GPS start before we trust fix data.
// The L76K's hot-start cache emits stale GGA with quality=1 and
// previous-session sats immediately; this blanking window filters it.
static const uint32_t GPS_WARMUP_MS = 10000;
static const uint32_t GPS_SATS_MAX_AGE_MS = 5000;
static const uint32_t GPS_SATS_HOLD_MS = 12000;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
static const uint32_t GPS_BAUD_PROBE_START_MS = 3000;
static const uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 2000;
static const uint32_t GPS_CHECKSUM_STALE_REPROBE_MS = 8000;
static const uint32_t GPS_MIN_CHECKSUM_FOR_STREAM = 2;
#elif defined(DEVICE_TLORA_PAGER_TFT)
static const uint32_t GPS_BAUD_PROBE_START_MS = 3000;
static const uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 3500;
static const uint32_t GPS_CHECKSUM_STALE_REPROBE_MS = 10000;
static const uint32_t GPS_MIN_CHECKSUM_FOR_STREAM = 1;
static const uint32_t GPS_PAGER_RAIL_RECOVERY_MS = 20000;
static const uint32_t GPS_PAGER_NO_SATS_RECOVERY_MS = 120000;
#else
static const uint32_t GPS_BAUD_PROBE_START_MS = 9000;
static const uint32_t GPS_BAUD_PROBE_INTERVAL_MS = 7000;
static const uint32_t GPS_CHECKSUM_STALE_REPROBE_MS = 10000;
static const uint32_t GPS_MIN_CHECKSUM_FOR_STREAM = 1;
#endif
static const uint32_t GPS_STREAM_STALL_MS = 15000;

static const uint32_t GPS_BAUD_PROBE_LIST[] = {
    (uint32_t)GPS_BAUD,
    9600,
    38400,
    115200,
};
static const size_t GPS_BAUD_PROBE_COUNT = sizeof(GPS_BAUD_PROBE_LIST) / sizeof(GPS_BAUD_PROBE_LIST[0]);

struct GpsProbePort {
    int8_t rx;
    int8_t tx;
};

static const GpsProbePort GPS_PORT_PROBE_LIST[] = {
    { GPS_RX, GPS_TX },
#if defined(DEVICE_TLORA_PAGER_TFT)
    // Some pager units expose RX reliably but may not have GPS TX routed.
    { GPS_RX, -1 },
    // Also try reversed labeling in case the module UART is swapped.
    { GPS_TX, GPS_RX },
    { GPS_TX, -1 },
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Some Heltec revisions/modules are labeled opposite from effective UART wiring.
    { GPS_RX, -1 },
    { GPS_TX, GPS_RX },
    { GPS_TX, -1 },
    { 44, 43 },
    { 44, -1 },
    { 43, 44 },
    { 43, -1 },
    { 38, 39 },
    { 38, -1 },
    { 39, 38 },
    { 39, -1 },
#endif
};
static const size_t GPS_PORT_PROBE_COUNT = sizeof(GPS_PORT_PROBE_LIST) / sizeof(GPS_PORT_PROBE_LIST[0]);

static TinyGPSPlus    _gps;
static HardwareSerial _serial(1);   // UART1
static bool           _enabled       = false;
static uint32_t       _startMs       = 0;     // millis() when GPS was started
static uint32_t       _firstFixMs    = 0;     // millis() when first real fix arrived (0 = none yet)
static uint32_t       _prevSentences = 0;     // sentencesWithFix at last check
static uint32_t       _totalBytes    = 0;
static uint8_t        _lastSats      = 0;
static uint32_t       _lastSatsMs    = 0;
static uint8_t        _baudProbeIdx  = 0;
static uint8_t        _portProbeIdx  = 0;
static uint32_t       _activeBaud    = GPS_BAUD;
static int8_t         _activeRx      = GPS_RX;
static int8_t         _activeTx      = GPS_TX;
#if defined(DEVICE_TLORA_PAGER_TFT)
static bool           _pagerRailInverted = false;
static bool           _pagerRailRetried = false;
static uint32_t       _pagerLastRailPrimeMs = 0;
#endif
// ── Duty cycling ─────────────────────────────────────────────────────────────
// Opt-in. When on, the receiver is parked in a RAM-retained standby between
// position samples instead of tracking continuously (~20-25 mA on these
// modules, which on a screen-off device is the single largest draw left).
// RAM-retained specifically: a hot start with valid ephemeris is seconds, while
// a cold start is minutes and would spend more energy re-acquiring than the
// standby ever saved.
static bool           _dutyEnabled   = false;
static uint32_t       _dutyPeriodS   = 0;
static bool           _dutyAsleep    = false;
static uint32_t       _dutySleptAtMs = 0;
static uint32_t       _dutyWokeAtMs  = 0;
// Set when a wake produces no NMEA at all. Duty cycling then disables itself
// for the rest of the session and leaves the receiver running: an unverifiable
// standby command that wedges the module must not be able to permanently cost
// the user their GPS.
static bool           _dutyFaulted   = false;
// Only wakes that follow a standby we issued are eligible for the fault check.
// A first start legitimately takes longer than the prove window when the baud
// probe has to walk candidate port configs, and faulting on that would disable
// duty cycling on exactly the boards that boot slowest.
static bool           _dutyEverSlept = false;
// Power state, tracked separately from _enabled. _enabled means "we are
// parsing"; this means "the receiver has power". They are not the same thing —
// the old gpsEnd() only closed the UART, so turning GPS off left the module
// tracking satellites at ~20-25 mA with nobody listening. Starts true because
// the module comes up powered from the board rail (and, on the pager, from the
// radio's rail priming) before any of this runs.
static bool           _powered       = true;

static uint32_t       _lastProbeMs   = 0;
static uint32_t       _lastByteMs    = 0;
static uint32_t       _lastDataMs    = 0;
static uint32_t       _lastChecksumMs = 0;
static uint32_t       _lastPassedChecksum = 0;
static uint32_t       _probeStartPassedChecksum = 0;
static uint32_t       _probeStartBytes = 0;
static uint32_t       _lastSatSeenMs = 0;
static uint32_t       _lastNoSatRecoveryMs = 0;
static bool           _nmeaSeen      = false;
static bool           _streamConfigLocked = false;
static bool           _everValidStreamSeen = false;

// Some firmwares update GSA regularly while GGA satellite fields can remain stale.
// Track both GN and GP talkers and prefer fresh GSA "satellites used" counts.
static TinyGPSCustom  _gngsaSat01(_gps, "GNGSA", 3);
static TinyGPSCustom  _gngsaSat02(_gps, "GNGSA", 4);
static TinyGPSCustom  _gngsaSat03(_gps, "GNGSA", 5);
static TinyGPSCustom  _gngsaSat04(_gps, "GNGSA", 6);
static TinyGPSCustom  _gngsaSat05(_gps, "GNGSA", 7);
static TinyGPSCustom  _gngsaSat06(_gps, "GNGSA", 8);
static TinyGPSCustom  _gngsaSat07(_gps, "GNGSA", 9);
static TinyGPSCustom  _gngsaSat08(_gps, "GNGSA", 10);
static TinyGPSCustom  _gngsaSat09(_gps, "GNGSA", 11);
static TinyGPSCustom  _gngsaSat10(_gps, "GNGSA", 12);
static TinyGPSCustom  _gngsaSat11(_gps, "GNGSA", 13);
static TinyGPSCustom  _gngsaSat12(_gps, "GNGSA", 14);
static TinyGPSCustom* _gngsaSats[12] = {
    &_gngsaSat01, &_gngsaSat02, &_gngsaSat03, &_gngsaSat04,
    &_gngsaSat05, &_gngsaSat06, &_gngsaSat07, &_gngsaSat08,
    &_gngsaSat09, &_gngsaSat10, &_gngsaSat11, &_gngsaSat12
};

static TinyGPSCustom  _gpgsaSat01(_gps, "GPGSA", 3);
static TinyGPSCustom  _gpgsaSat02(_gps, "GPGSA", 4);
static TinyGPSCustom  _gpgsaSat03(_gps, "GPGSA", 5);
static TinyGPSCustom  _gpgsaSat04(_gps, "GPGSA", 6);
static TinyGPSCustom  _gpgsaSat05(_gps, "GPGSA", 7);
static TinyGPSCustom  _gpgsaSat06(_gps, "GPGSA", 8);
static TinyGPSCustom  _gpgsaSat07(_gps, "GPGSA", 9);
static TinyGPSCustom  _gpgsaSat08(_gps, "GPGSA", 10);
static TinyGPSCustom  _gpgsaSat09(_gps, "GPGSA", 11);
static TinyGPSCustom  _gpgsaSat10(_gps, "GPGSA", 12);
static TinyGPSCustom  _gpgsaSat11(_gps, "GPGSA", 13);
static TinyGPSCustom  _gpgsaSat12(_gps, "GPGSA", 14);
static TinyGPSCustom* _gpgsaSats[12] = {
    &_gpgsaSat01, &_gpgsaSat02, &_gpgsaSat03, &_gpgsaSat04,
    &_gpgsaSat05, &_gpgsaSat06, &_gpgsaSat07, &_gpgsaSat08,
    &_gpgsaSat09, &_gpgsaSat10, &_gpgsaSat11, &_gpgsaSat12
};

// GGA field 7 carries satellites used in current fix solution.
static TinyGPSCustom _gnggaSatsUsed(_gps, "GNGGA", 7);
static TinyGPSCustom _gpggaSatsUsed(_gps, "GPGGA", 7);

// Some modules emit GNS (not GGA) as their primary fix sentence.
// GNS field 7 also carries satellites used in fix.
static TinyGPSCustom _gngnsSatsUsed(_gps, "GNGNS", 7);
static TinyGPSCustom _gpgnsSatsUsed(_gps, "GPGNS", 7);

// GSV field 3 carries satellites in view, useful before the first full fix.
static TinyGPSCustom _gngsvSatsView(_gps, "GNGSV", 3);
static TinyGPSCustom _gpgsvSatsView(_gps, "GPGSV", 3);
static TinyGPSCustom _glgsvSatsView(_gps, "GLGSV", 3);
static TinyGPSCustom _gagsvSatsView(_gps, "GAGSV", 3);
static TinyGPSCustom _bdgsvSatsView(_gps, "BDGSV", 3);
static TinyGPSCustom _gbgsvSatsView(_gps, "GBGSV", 3);
static TinyGPSCustom _gqgsvSatsView(_gps, "GQGSV", 3);
static TinyGPSCustom _qzgsvSatsView(_gps, "QZGSV", 3);

static uint8_t gsaSatsUsed(TinyGPSCustom* const sats[12], bool &fresh) {
    fresh = false;
    uint8_t used = 0;
    for (int i = 0; i < 12; i++) {
        TinyGPSCustom *term = sats[i];
        if (!term) continue;
        if (term->isValid() && term->age() < GPS_SATS_MAX_AGE_MS) fresh = true;
        const char *prn = term->value();
        if (prn && prn[0] != '\0') used++;
    }
    return used;
}

static uint8_t parseCustomU8(TinyGPSCustom &term, bool &fresh) {
    fresh = false;
    if (!term.isValid()) return 0;
    if (term.age() >= GPS_SATS_MAX_AGE_MS) return 0;
    fresh = true;
    int v = atoi(term.value());
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    return (uint8_t)v;
}

static void gpsApplyPortAndBaud(int8_t rx, int8_t tx, uint32_t baud) {
    _serial.end();
    _serial.begin(baud, SERIAL_8N1, rx, tx);
    _activeRx = rx;
    _activeTx = tx;
    _activeBaud = baud;

    // passedChecksum() is cumulative over runtime; mark a new baseline on each
    // UART config so stream-detection checks only fresh checksums/bytes.
    _probeStartPassedChecksum = _gps.passedChecksum();
    _probeStartBytes = _totalBytes;
    _lastPassedChecksum = _probeStartPassedChecksum;
    uint32_t now = millis();
    _lastChecksumMs = now;
    _lastByteMs = now;
}

static bool gpsNextProbeConfig() {
    if (GPS_BAUD_PROBE_COUNT == 0 || GPS_PORT_PROBE_COUNT == 0) return false;

    size_t combos = GPS_BAUD_PROBE_COUNT * GPS_PORT_PROBE_COUNT;
    for (size_t attempt = 0; attempt < combos; attempt++) {
        _baudProbeIdx = (uint8_t)((_baudProbeIdx + 1) % GPS_BAUD_PROBE_COUNT);
        if (_baudProbeIdx == 0) {
            _portProbeIdx = (uint8_t)((_portProbeIdx + 1) % GPS_PORT_PROBE_COUNT);
        }

        const GpsProbePort &p = GPS_PORT_PROBE_LIST[_portProbeIdx];
        uint32_t candidateBaud = GPS_BAUD_PROBE_LIST[_baudProbeIdx];
        if (p.rx < 0) continue;
        if (candidateBaud == _activeBaud && p.rx == _activeRx && p.tx == _activeTx) continue;

        gpsApplyPortAndBaud(p.rx, p.tx, candidateBaud);
        debugLogGps("[gps] probing baud=%lu (rx=%d tx=%d)\n",
                    (unsigned long)candidateBaud, (int)p.rx, (int)p.tx);
        return true;
    }
    return false;
}

void gpsBegin() {
    _powered = true;   // priming below restores the rail on boards that gate it
#if defined(DEVICE_TLORA_PAGER_TFT)
    _pagerRailInverted = false;
    _pagerRailRetried = false;
    (void)pagerPrimeGpsRails(_pagerRailInverted);
#endif
    _baudProbeIdx  = 0;
    _portProbeIdx  = 0;
    _activeRx      = GPS_PORT_PROBE_LIST[0].rx;
    _activeTx      = GPS_PORT_PROBE_LIST[0].tx;
    _activeBaud    = GPS_BAUD;
    gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
    // Nudge the module out of any standby a previous gpsEnd() left it in.
    // Without this a re-enable would open the port onto a silent receiver and
    // the baud prober would walk every candidate config looking for a stream
    // that was never going to arrive. Harmless when it is already awake: it is
    // not a valid sentence, so it is discarded. Placed here rather than in
    // gpsApplyPortAndBaud() because the prober calls that repeatedly.
    _serial.write((uint8_t)'\r');
    _serial.write((uint8_t)'\n');
    _serial.flush();
    _enabled       = true;
    _startMs       = millis();
    _firstFixMs    = 0;
    _prevSentences = 0;
    _totalBytes    = 0;
    _lastSats      = 0;
    _lastSatsMs    = 0;
    _lastProbeMs   = _startMs;
    _lastByteMs    = _startMs;
    _lastDataMs    = 0;
    _lastChecksumMs = _startMs;
    _lastPassedChecksum = _gps.passedChecksum();
    _lastSatSeenMs = _startMs;
    _lastNoSatRecoveryMs = _startMs;
    _nmeaSeen      = false;
    _streamConfigLocked = false;
    _everValidStreamSeen = false;
    // Duty-cycle timers are relative to this start, not to a previous session:
    // a stale _dutyWokeAtMs would make the first pass look like a wake that had
    // already been awake for hours, and park (or fault) the receiver instantly.
    _dutyAsleep    = false;
    _dutyEverSlept = false;
    _dutyFaulted   = false;
    _dutyWokeAtMs  = _startMs;
    _dutySleptAtMs = _startMs;
#if defined(DEVICE_TLORA_PAGER_TFT)
    _pagerLastRailPrimeMs = _startMs;
#endif
    Serial.printf("[gps] started on UART1 baud=%lu rx=%d tx=%d\n",
                (unsigned long)_activeBaud, (int)_activeRx, (int)_activeTx);
}

static void gpsSendNmea(const char *body);   // defined with the duty-cycle code

#if !defined(DEVICE_TLORA_PAGER_TFT)
// Indefinite standby, for boards with no GPS enable pin. PMTK161,0 is genuinely
// open-ended. PCAS12 takes a duration and has no documented "forever", so it
// gets a day — if a CASIC part self-wakes after that with GPS switched off it
// will idle unheard until the next toggle, which is still strictly better than
// the previous behaviour of never powering down at all. Scoped to the boards
// that use it; the pager cuts the rail instead and never reads this.
static const uint32_t GPS_STANDBY_INDEF_S = 86400;
#endif

static void gpsPowerDown() {
    if (!_powered) return;
#if defined(DEVICE_TLORA_PAGER_TFT)
    // The pager has a real switch, so use it — nothing beats cutting the rail.
    // Except on the inverted-polarity revisions: there "enabled" is expressed
    // by configuring the pin as an input, and driving it as an output to
    // disable is a guess about hardware we cannot test. Those units keep the
    // old always-on behaviour rather than risk a GPS that will not come back.
    if (!_pagerRailInverted) {
        (void)pagerSetGpsRail(false);
    } else {
        debugLogGps("[gps] inverted rail polarity - leaving GPS powered\n");
        return;   // still powered; don't claim otherwise
    }
#else
    // No GPS enable pin on these boards; the module is fed from the board rail.
    // Standby is the only lever, and it has to be sent before the UART closes.
    //
    // The port may not be open at all: booting with GPS disabled never calls
    // gpsBegin(), and that is precisely the case this needs to cover. Open it
    // just long enough to speak. GPS_BAUD is the compile-time default rather
    // than a probed rate — there is nothing to probe against a receiver we are
    // about to silence, and a wrong guess only means the command is not
    // understood, which is the behaviour we already had.
    const bool hadPort = _enabled;
    if (!hadPort) {
        _serial.begin(GPS_BAUD, SERIAL_8N1, _activeRx, _activeTx);
        delay(10);
    }
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "PCAS12,%lu", (unsigned long)GPS_STANDBY_INDEF_S);
    gpsSendNmea(cmd);
    delay(20);
    gpsSendNmea("PMTK161,0");
    delay(20);
    if (!hadPort) _serial.end();
#endif
    _powered = false;
    // Not debug-gated. Power state changes are rare, user-initiated, and the
    // only externally visible evidence that turning GPS off did anything at
    // all — on boards with no enable pin there is no rail message to go with
    // it. Hiding this behind the debug flag made a successful power-down
    // indistinguishable from the code never running.
    Serial.println("[gps] powered down");
}

static void gpsPowerUp() {
    if (_powered) return;
#if defined(DEVICE_TLORA_PAGER_TFT)
    (void)pagerSetGpsRail(true);
    delay(20);   // let the rail settle before the module is probed
#endif
    // Non-pager boards need no action here: the module still has power, and
    // gpsBegin() nudges it out of standby once the UART is open.
    _powered = true;
    Serial.println("[gps] powered up");
}

void gpsEnd() {
    // Order matters: the standby command has to go out while the port is still
    // open.
    gpsPowerDown();
    _serial.end();
    _enabled = false;
    _dutyAsleep = false;
    Serial.println("[gps] stopped");
}

// ── Duty-cycle standby ───────────────────────────────────────────────────────
// Two command dialects ship on these boards under the same "L76K" label, and
// there is no reliable way to tell them apart at runtime:
//   PCAS (CASIC / AT6558-class)   $PCAS12,<sec>  standby for N seconds
//   PMTK (MediaTek L76-class)     $PMTK161,0     standby, RAM retained
// Both are sent. An NMEA input sentence with a valid checksum that the receiver
// does not recognise is discarded, so the one that does not apply is inert —
// which is what makes sending both safe rather than a guess.
//
// Both forms retain RAM, so the next start is a hot one. Wake is serial
// activity on the module's RX line in either dialect.
static const uint32_t GPS_DUTY_MIN_PERIOD_S    = 120;     // below this, acquire cost dominates
static const uint32_t GPS_DUTY_MAX_ACQUIRE_MS  = 120000;  // give up on a cycle after this
static const uint32_t GPS_DUTY_WAKE_PROVE_MS   = 15000;   // NMEA must return within this

static void gpsSendNmea(const char *body) {
    uint8_t ck = 0;
    for (const char *p = body; *p; ++p) ck ^= (uint8_t)*p;
    _serial.printf("$%s*%02X\r\n", body, ck);
    _serial.flush();
}

static void gpsDutySleep() {
    if (_dutyAsleep || !_enabled) return;
    // Ask for a standby slightly longer than our own timer so the module's
    // internal wake (on the dialects that honour a duration) never beats us to
    // it — our timer stays the source of truth either way.
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "PCAS12,%lu", (unsigned long)(_dutyPeriodS + 5));
    gpsSendNmea(cmd);
    delay(20);
    gpsSendNmea("PMTK161,0");

    _dutyAsleep    = true;
    _dutyEverSlept = true;
    _dutySleptAtMs = millis();
    _nmeaSeen      = false;
    debugLogGps("[gps] duty: standby for %lus\n", (unsigned long)_dutyPeriodS);
}

static void gpsDutyWake() {
    if (!_dutyAsleep) return;
    // Any serial activity wakes both dialects. The newline is deliberately not
    // a command — it just has to arrive on the module's RX line.
    _serial.write((uint8_t)'\r');
    _serial.write((uint8_t)'\n');
    _serial.flush();

    _dutyAsleep   = false;
    _dutyWokeAtMs = millis();
    // Re-arm the warm-up blanking: a hot start replays cached GGA with a stale
    // quality flag exactly the way a cold boot does (see GPS_WARMUP_MS).
    _startMs        = _dutyWokeAtMs;
    _lastByteMs     = _dutyWokeAtMs;
    _lastChecksumMs = _dutyWokeAtMs;
    _nmeaSeen       = false;
    debugLogGps("[gps] duty: wake\n");
}

static void gpsServiceDutyCycle(uint32_t now) {
    if (_dutyFaulted || !_dutyEnabled || !_enabled) return;

    if (_dutyAsleep) {
        if ((uint32_t)(now - _dutySleptAtMs) < _dutyPeriodS * 1000UL) return;
        gpsDutyWake();
        return;
    }

    const uint32_t awakeMs = (uint32_t)(now - _dutyWokeAtMs);

    // Fault check: if a wake never produces NMEA, the standby command wedged
    // the receiver. Stop duty cycling and leave it awake — losing the battery
    // saving is a far better outcome than losing GPS until a power cycle.
    if (_dutyEverSlept && !_nmeaSeen && awakeMs >= GPS_DUTY_WAKE_PROVE_MS) {
        _dutyFaulted = true;
        Serial.println("[gps] duty: no NMEA after wake - disabling duty cycle for this session");
        return;
    }

    // gpsHasFix() already withholds a verdict until GPS_WARMUP_MS has passed,
    // so this cannot sleep on the stale hot-start cache.
    if (gpsHasFix() || awakeMs >= GPS_DUTY_MAX_ACQUIRE_MS) {
        if (!gpsHasFix()) {
            debugLogGps("[gps] duty: no fix in %lums, sleeping anyway\n",
                        (unsigned long)awakeMs);
        }
        gpsDutySleep();
    }
}

void gpsSetDutyCycle(bool enabled, uint32_t periodS) {
    // Below the floor the receiver would spend most of each cycle re-acquiring,
    // which costs more than it saves — treat that as "off" rather than
    // pretending to duty cycle.
    const bool viable = enabled && periodS >= GPS_DUTY_MIN_PERIOD_S;
    if (enabled && !viable) {
        debugLogGps("[gps] duty: period %lus below %lus floor - staying always-on\n",
                    (unsigned long)periodS, (unsigned long)GPS_DUTY_MIN_PERIOD_S);
    }
    if (viable == _dutyEnabled && periodS == _dutyPeriodS) return;

    _dutyEnabled = viable;
    _dutyPeriodS = periodS;
    if (!viable) {
        gpsDutyWake();          // no-op unless we were parked
        _dutyFaulted = false;   // a config change earns a fresh attempt
    } else if (_dutyWokeAtMs == 0) {
        _dutyWokeAtMs = millis();
    }
}

bool gpsIsAsleep() { return _dutyAsleep; }

void gpsLoop() {
    if (!_enabled) return;
    static uint32_t _lastDbg = 0;
    uint32_t now = millis();

    gpsServiceDutyCycle(now);
    if (_dutyAsleep) {
        // Nothing is arriving by design. Returning here also keeps the
        // stale-checksum baud re-probe below from firing on the silence and
        // walking the port through every candidate config while parked.
        return;
    }

    bool sawBytes = false;
    while (_serial.available()) {
        char c = (char)_serial.read();
        _gps.encode(c);
        _totalBytes++;
        sawBytes = true;
    }
    if (sawBytes) {
        _lastByteMs = now;
        _lastDataMs = now;
    }

    uint32_t passed = _gps.passedChecksum();
    if (passed != _lastPassedChecksum) {
        _lastPassedChecksum = passed;
        _lastChecksumMs = now;
    }

        const bool hasFreshChecksums = (passed >= (_probeStartPassedChecksum + GPS_MIN_CHECKSUM_FOR_STREAM));
        const bool hasFreshBytes = (_totalBytes >= (_probeStartBytes + 24));
        if (!_nmeaSeen && hasFreshChecksums && hasFreshBytes) {
        _nmeaSeen = true;
            _streamConfigLocked = true;
            _everValidStreamSeen = true;
        debugLogGps("[gps] valid NMEA stream detected at baud=%lu\n", (unsigned long)_activeBaud);
    }

    // Boards can occasionally latch onto a noisy UART config that yields
    // sporadic checksum passes once, then no real sentence progress. Re-probe.
    if (_nmeaSeen
        && hasFreshChecksums
        && (now - _lastChecksumMs) >= GPS_CHECKSUM_STALE_REPROBE_MS
        && (now - _lastProbeMs) >= GPS_BAUD_PROBE_INTERVAL_MS) {
    debugLogGps("[gps] checksum stale (%lums) on baud=%lu rx=%d tx=%d, %s\n",
                    (unsigned long)(now - _lastChecksumMs),
                    (unsigned long)_activeBaud, (int)_activeRx, (int)_activeTx,
                    _streamConfigLocked ? "restarting current UART" : "probing next");
        _nmeaSeen = false;
        _lastProbeMs = now;
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (_streamConfigLocked || _everValidStreamSeen) {
            gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
            return;
        }
#endif
        gpsNextProbeConfig();
    }

    uint8_t satsNow = 0;
    if (_nmeaSeen) {
        satsNow = gpsSats();
        if (satsNow > 0) _lastSatSeenMs = now;
    }

    if (!_nmeaSeen
        && (now - _startMs) >= GPS_BAUD_PROBE_START_MS
        && (now - _lastProbeMs) >= GPS_BAUD_PROBE_INTERVAL_MS) {
        _lastProbeMs = now;
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (_streamConfigLocked || _everValidStreamSeen) {
            gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
            return;
        }
        if ((now - _pagerLastRailPrimeMs) >= GPS_PAGER_RAIL_RECOVERY_MS) {
            _pagerRailInverted = !_pagerRailInverted;
            (void)pagerPrimeGpsRails(_pagerRailInverted);
            _pagerLastRailPrimeMs = now;
            gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
            debugLogGps("[gps] no stream yet; periodic rail recovery invert=%d\n",
                        _pagerRailInverted ? 1 : 0);
            return;
        }
        if (!_pagerRailRetried) {
            _pagerRailRetried = true;
            _pagerRailInverted = true;
            (void)pagerPrimeGpsRails(_pagerRailInverted);
            _pagerLastRailPrimeMs = now;
            gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
            debugLogGps("[gps] no stream yet; retried pager rails with invert=1\n");
            return;
        }
#endif
        gpsNextProbeConfig();
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    if (_nmeaSeen
        && !_streamConfigLocked
        && !_everValidStreamSeen
        && satsNow == 0
        && (now - _startMs) >= GPS_WARMUP_MS
        && (now - _lastSatSeenMs) >= GPS_PAGER_NO_SATS_RECOVERY_MS
        && (now - _lastNoSatRecoveryMs) >= GPS_PAGER_NO_SATS_RECOVERY_MS
        && (now - _lastProbeMs) >= GPS_BAUD_PROBE_INTERVAL_MS) {
        _lastProbeMs = now;
        _lastNoSatRecoveryMs = now;
        // Keep GPS powered so it can continue sky search; just probe next UART config.
        _nmeaSeen = false;
        debugLogGps("[gps] no sats for %lums; probing UART without rail reset\n",
                    (unsigned long)(now - _lastSatSeenMs));
        gpsNextProbeConfig();
        return;
    }
#endif

    if (_nmeaSeen && (now - _lastByteMs) >= GPS_STREAM_STALL_MS) {
        debugLogGps("[gps] stream stalled for %lums, restarting UART1 at baud=%lu\n",
                    (unsigned long)(now - _lastByteMs), (unsigned long)_activeBaud);
        gpsApplyPortAndBaud(_activeRx, _activeTx, _activeBaud);
        _lastByteMs = now;
    }

    // Detect first real fix: only after warmup to ignore stale hot-start data
    if (_firstFixMs == 0
        && (now - _startMs) >= GPS_WARMUP_MS
        && _gps.location.isValid()
        && _gps.location.age() < 5000) {
        _firstFixMs = now;
        debugLogGps("[gps] first fix after %lums sats=%d lat=%.6f lon=%.6f\n",
                    (unsigned long)(_firstFixMs - _startMs),
                    _gps.satellites.isValid() ? (int)_gps.satellites.value() : 0,
                    _gps.location.lat(), _gps.location.lng());
    }

    if (debugGpsEnabled() && (now - _lastDbg >= 5000)) {
        _lastDbg = now;
        uint32_t sf = _gps.sentencesWithFix();
        debugLogGps("[gps] sats=%d fix=%d q=%c sf=%lu(+%lu) age=%lums hdop=%.1f pos=%.6f,%.6f bytes=%lu\n",
                    _gps.satellites.isValid() ? (int)_gps.satellites.value() : -1,
                    (int)_gps.location.isValid(),
                    _gps.location.isValid() ? (char)_gps.location.FixQuality() : '?',
                    (unsigned long)sf,
                    (unsigned long)(sf - _prevSentences),
                    _gps.location.isValid() ? (unsigned long)_gps.location.age() : 0UL,
                    _gps.hdop.isValid() ? _gps.hdop.hdop() : 99.9,
                    _gps.location.lat(), _gps.location.lng(),
                    (unsigned long)_totalBytes);
        _prevSentences = sf;
    }

}

void gpsSetEnabled(bool en) {
    if (en) {
        gpsPowerUp();               // no-op unless a previous disable cut power
        if (!_enabled) gpsBegin();
        return;
    }
    if (_enabled) {
        gpsEnd();
    } else {
        // Disabled and never started — the boot path. _enabled is already
        // false, so the old code did nothing here and the module was left
        // powered for the entire session on a device that had GPS switched
        // off. _powered guards against repeating the work on every config save.
        gpsPowerDown();
    }
}

bool gpsIsEnabled() { return _enabled; }

bool gpsHasFix() {
    if (!_enabled) return false;
    // Ignore everything during the warm-up window after start/restart
    if (millis() - _startMs < GPS_WARMUP_MS) return false;
    if (!_gps.location.isValid()) return false;
    if (_gps.location.age() > 5000) return false;
    // HDOP sanity check: the L76K hot-start cache often reports high HDOP
    // (or stale HDOP) with a fake quality > 0.  Real outdoor fixes with
    // 8+ sats have HDOP < 3; indoor/marginal < 10.  Reject > 20.
    if (_gps.hdop.isValid() && _gps.hdop.hdop() > 20.0) return false;
    return true;
}

bool gpsHasNmeaStream() {
    return _enabled && _nmeaSeen;
}

uint32_t gpsDataAgeMs() {
    if (!_enabled) return UINT32_MAX;
    uint32_t now = millis();
    if (_lastDataMs == 0) return now - _startMs;
    return now - _lastDataMs;
}

int32_t gpsLatI() {
    return (int32_t)(_gps.location.lat() * 1e7);
}

int32_t gpsLonI() {
    return (int32_t)(_gps.location.lng() * 1e7);
}

int32_t gpsAltM() {
    return _gps.altitude.isValid() ? (int32_t)_gps.altitude.meters() : 0;
}

uint8_t gpsSats() {
    if (!_enabled) return 0;
    // Don't report stale cached sat count during warmup
    if (millis() - _startMs < GPS_WARMUP_MS) return 0;

    uint32_t now = millis();

    // Prefer fresh GSA-based "satellites used" when available.
    bool gngsaFresh = false;
    uint8_t gngsaUsed = gsaSatsUsed(_gngsaSats, gngsaFresh);

    bool gpgsaFresh = false;
    uint8_t gpgsaUsed = gsaSatsUsed(_gpgsaSats, gpgsaFresh);

    bool hasFresh = false;
    uint8_t sats = 0;
    if (gngsaFresh || gpgsaFresh) {
        // GN and GP can alternate; use the best fresh reading.
        hasFresh = true;
        sats = max(gngsaUsed, gpgsaUsed);
    }

    // Fallback to direct GGA satellites-used fields (GN/GP talkers).
    if (!hasFresh) {
        bool gnggaFresh = false;
        bool gpggaFresh = false;
        uint8_t gngga = parseCustomU8(_gnggaSatsUsed, gnggaFresh);
        uint8_t gpgga = parseCustomU8(_gpggaSatsUsed, gpggaFresh);
        if (gnggaFresh || gpggaFresh) {
            hasFresh = true;
            sats = max(gngga, gpgga);
        }
    }

    // Additional fallback for modules that output GNS instead of GGA.
    if (!hasFresh) {
        bool gngnsFresh = false;
        bool gpgnsFresh = false;
        uint8_t gngns = parseCustomU8(_gngnsSatsUsed, gngnsFresh);
        uint8_t gpgns = parseCustomU8(_gpgnsSatsUsed, gpgnsFresh);
        if (gngnsFresh || gpgnsFresh) {
            hasFresh = true;
            sats = max(gngns, gpgns);
        }
    }

    // Legacy TinyGPS++ satellite value (typically from GPGGA).
    if (!hasFresh && _gps.satellites.isValid() && _gps.satellites.age() < GPS_SATS_MAX_AGE_MS) {
        hasFresh = true;
        sats = (uint8_t)_gps.satellites.value();
    }

    // Some modules report satellites in view via GSV before GSA/GGA stabilize.
    if (!hasFresh) {
        bool gngsvFresh = false;
        bool gpgsvFresh = false;
        bool glgsvFresh = false;
        bool gagsvFresh = false;
        bool bdgsvFresh = false;
        bool gbgsvFresh = false;
        bool gqgsvFresh = false;
        bool qzgsvFresh = false;
        uint8_t gngsv = parseCustomU8(_gngsvSatsView, gngsvFresh);
        uint8_t gpgsv = parseCustomU8(_gpgsvSatsView, gpgsvFresh);
        uint8_t glgsv = parseCustomU8(_glgsvSatsView, glgsvFresh);
        uint8_t gagsv = parseCustomU8(_gagsvSatsView, gagsvFresh);
        uint8_t bdgsv = parseCustomU8(_bdgsvSatsView, bdgsvFresh);
        uint8_t gbgsv = parseCustomU8(_gbgsvSatsView, gbgsvFresh);
        uint8_t gqgsv = parseCustomU8(_gqgsvSatsView, gqgsvFresh);
        uint8_t qzgsv = parseCustomU8(_qzgsvSatsView, qzgsvFresh);
        if (gngsvFresh || gpgsvFresh || glgsvFresh || gagsvFresh
            || bdgsvFresh || gbgsvFresh || gqgsvFresh || qzgsvFresh) {
            hasFresh = true;
            sats = gngsv;
            sats = max(sats, gpgsv);
            sats = max(sats, glgsv);
            sats = max(sats, gagsv);
            sats = max(sats, bdgsv);
            sats = max(sats, gbgsv);
            sats = max(sats, gqgsv);
            sats = max(sats, qzgsv);
        }
    }

    if (hasFresh) {
        if (sats > 0) {
            _lastSats = sats;
            _lastSatsMs = now;
            return sats;
        }
        // A transient zero can appear between sentence updates; smooth it while fix is valid.
        if (_nmeaSeen && _lastSats > 0 && (now - _lastSatsMs) < GPS_SATS_HOLD_MS)
            return _lastSats;
        _lastSats = 0;
        _lastSatsMs = now;
        return 0;
    }

    // No fresh sat sentence right now; keep the last valid count briefly.
    if (_nmeaSeen && _lastSats > 0 && (now - _lastSatsMs) < GPS_SATS_HOLD_MS)
        return _lastSats;

    _lastSats = 0;
    _lastSatsMs = now;

    return 0;
}

uint32_t gpsFixAgeMs() {
    return _firstFixMs ? (millis() - _firstFixMs) : 0;
}

uint32_t gpsSearchTimeMs() {
    if (!_enabled) return 0;
    if (_firstFixMs) return _firstFixMs - _startMs;
    return millis() - _startMs;
}

float gpsCourse() {
    return _gps.course.isValid() ? (float)_gps.course.deg() : 0.0f;
}

float gpsSpeedKmh() {
    return _gps.speed.isValid() ? (float)_gps.speed.kmph() : 0.0f;
}

bool gpsUtcDateTime(int &year, int &month, int &day,
                    int &hour, int &minute, int &second) {
    if (!gpsHasFix()) return false;
    if (!_gps.date.isValid() || !_gps.time.isValid()) return false;
    if (_gps.date.age() > 5000 || _gps.time.age() > 5000) return false;

    year   = _gps.date.year();
    month  = _gps.date.month();
    day    = _gps.date.day();
    hour   = _gps.time.hour();
    minute = _gps.time.minute();
    second = _gps.time.second();
    return true;
}
