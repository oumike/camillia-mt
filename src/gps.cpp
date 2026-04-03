#include "gps.h"
#include "config.h"
#include <TinyGPSPlus.h>

static TinyGPSPlus    _gps;
static HardwareSerial _serial(1);   // UART1
static bool           _enabled = false;

void gpsBegin() {
    _serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    _enabled = true;
    Serial.println("[gps] started on UART1");
}

void gpsEnd() {
    _serial.end();
    _enabled = false;
    Serial.println("[gps] stopped");
}

void gpsLoop() {
    if (!_enabled) return;
    static uint32_t _lastDbg    = 0;
    static uint32_t _totalBytes = 0;
    while (_serial.available()) {
        char c = (char)_serial.read();
        _gps.encode(c);
        _totalBytes++;
    }
    // Every 5 s print a one-line summary so we can confirm data flow
    uint32_t now = millis();
    if (now - _lastDbg >= 5000) {
        _lastDbg = now;
        Serial.printf("[gps] bytes=%lu  sats=%d  fix=%d  chars=%lu  sentences=%lu  failed=%lu\n",
                      _totalBytes,
                      _gps.satellites.isValid() ? (int)_gps.satellites.value() : -1,
                      (int)_gps.location.isValid(),
                      _gps.charsProcessed(),
                      _gps.sentencesWithFix(),
                      _gps.failedChecksum());
    }
}

void gpsSetEnabled(bool en) {
    if (en && !_enabled) gpsBegin();
    else if (!en && _enabled) gpsEnd();
}

bool gpsIsEnabled() { return _enabled; }

bool gpsHasFix() {
    return _enabled
        && _gps.location.isValid()
        && _gps.location.age() < 5000;
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
    return _gps.satellites.isValid() ? (uint8_t)_gps.satellites.value() : 0;
}

float gpsCourse() {
    return _gps.course.isValid() ? (float)_gps.course.deg() : 0.0f;
}

float gpsSpeedKmh() {
    return _gps.speed.isValid() ? (float)_gps.speed.kmph() : 0.0f;
}
