#include "gps.h"
#include "config.h"
#include "debug_flags.h"
#include <TinyGPSPlus.h>

// Minimum ms after GPS start before we trust fix data.
// The L76K's hot-start cache emits stale GGA with quality=1 and
// previous-session sats immediately; this blanking window filters it.
static const uint32_t GPS_WARMUP_MS = 10000;
static const uint32_t GPS_SATS_MAX_AGE_MS = 5000;
static const uint32_t GPS_SATS_HOLD_MS = 12000;

static TinyGPSPlus    _gps;
static HardwareSerial _serial(1);   // UART1
static bool           _enabled       = false;
static uint32_t       _startMs       = 0;     // millis() when GPS was started
static uint32_t       _firstFixMs    = 0;     // millis() when first real fix arrived (0 = none yet)
static uint32_t       _prevSentences = 0;     // sentencesWithFix at last check
static uint32_t       _totalBytes    = 0;
static uint8_t        _lastSats      = 0;
static uint32_t       _lastSatsMs    = 0;

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

void gpsBegin() {
    _serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    _enabled       = true;
    _startMs       = millis();
    _firstFixMs    = 0;
    _prevSentences = 0;
    _totalBytes    = 0;
    _lastSats      = 0;
    _lastSatsMs    = 0;
    debugLogGps("[gps] started on UART1\n");
}

void gpsEnd() {
    _serial.end();
    _enabled = false;
    debugLogGps("[gps] stopped\n");
}

void gpsLoop() {
    if (!_enabled) return;
    static uint32_t _lastDbg = 0;
    uint32_t now = millis();

    while (_serial.available()) {
        char c = (char)_serial.read();
        _gps.encode(c);
        _totalBytes++;
    }

    // Detect first real fix: only after warmup to ignore stale hot-start data
    if (_firstFixMs == 0
        && (now - _startMs) >= GPS_WARMUP_MS
        && _gps.location.isValid()
        && _gps.location.age() < 5000) {
        _firstFixMs = now;
        debugLogGps("[gps] first fix after %lums sats=%d\n",
                    (unsigned long)(_firstFixMs - _startMs),
                    _gps.satellites.isValid() ? (int)_gps.satellites.value() : 0);
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
    if (en && !_enabled) gpsBegin();
    else if (!en && _enabled) gpsEnd();
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

    // Fallback to GGA sats if it is fresh.
    if (!hasFresh && _gps.satellites.isValid() && _gps.satellites.age() < GPS_SATS_MAX_AGE_MS) {
        hasFresh = true;
        sats = (uint8_t)_gps.satellites.value();
    }

    if (hasFresh) {
        if (sats > 0) {
            _lastSats = sats;
            _lastSatsMs = now;
            return sats;
        }
        // A transient zero can appear between sentence updates; smooth it while fix is valid.
        if (gpsHasFix() && _lastSats > 0 && (now - _lastSatsMs) < GPS_SATS_HOLD_MS)
            return _lastSats;
        _lastSats = 0;
        _lastSatsMs = now;
        return 0;
    }

    // No fresh sat sentence right now; keep the last valid count briefly.
    if (gpsHasFix() && _lastSats > 0 && (now - _lastSatsMs) < GPS_SATS_HOLD_MS)
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
