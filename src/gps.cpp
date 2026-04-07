#include "gps.h"
#include "config.h"
#include <TinyGPSPlus.h>

// Minimum ms after GPS start before we trust fix data.
// The L76K's hot-start cache emits stale GGA with quality=1 and
// previous-session sats immediately; this blanking window filters it.
static const uint32_t GPS_WARMUP_MS = 10000;

static TinyGPSPlus    _gps;
static HardwareSerial _serial(1);   // UART1
static bool           _enabled       = false;
static uint32_t       _startMs       = 0;     // millis() when GPS was started
static uint32_t       _firstFixMs    = 0;     // millis() when first real fix arrived (0 = none yet)
static uint32_t       _prevSentences = 0;     // sentencesWithFix at last check
static uint32_t       _totalBytes    = 0;

void gpsBegin() {
    _serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    _enabled       = true;
    _startMs       = millis();
    _firstFixMs    = 0;
    _prevSentences = 0;
    _totalBytes    = 0;
    Serial.println("[gps] started on UART1");
}

void gpsEnd() {
    _serial.end();
    _enabled = false;
    Serial.println("[gps] stopped");
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
        Serial.printf("[gps] first fix after %lums  sats=%d\n",
                      (unsigned long)(_firstFixMs - _startMs),
                      _gps.satellites.isValid() ? (int)_gps.satellites.value() : 0);
    }

    // Every 5 s print a one-line summary
    if (now - _lastDbg >= 5000) {
        _lastDbg = now;
        uint32_t sf = _gps.sentencesWithFix();
        Serial.printf("[gps] sats=%d  fix=%d  q=%c  sf=%lu(+%lu)  age=%lums  hdop=%.1f  pos=%.6f,%.6f\n",
                      _gps.satellites.isValid() ? (int)_gps.satellites.value() : -1,
                      (int)_gps.location.isValid(),
                      _gps.location.isValid() ? (char)_gps.location.FixQuality() : '?',
                      sf, sf - _prevSentences,
                      _gps.location.isValid() ? (unsigned long)_gps.location.age() : 0UL,
                      _gps.hdop.isValid() ? _gps.hdop.hdop() : 99.9,
                      _gps.location.lat(), _gps.location.lng());
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
    return _gps.satellites.isValid() ? (uint8_t)_gps.satellites.value() : 0;
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
