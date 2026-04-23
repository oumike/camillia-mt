#pragma once
#include <Arduino.h>

// ── GPS module (L76K via UART1) ───────────────────────────────
// Pins: GPS_RX / GPS_TX / GPS_BAUD defined in config.h

// Initialise UART and begin parsing NMEA.
void gpsBegin();

// Shut down UART (low-power disable).
void gpsEnd();

// Drain available UART bytes into TinyGPS++. Call every loop().
void gpsLoop();

// Enable or disable GPS at runtime (calls gpsBegin / gpsEnd).
void gpsSetEnabled(bool en);

// True while GPS serial is active.
bool gpsIsEnabled();

// True if a valid fix has been received within the last 5 seconds.
bool gpsHasFix();

// Position as scaled integers (degrees × 1e7). Valid only when gpsHasFix().
int32_t gpsLatI();
int32_t gpsLonI();
int32_t gpsAltM();

// Satellite count (0 until a real fix is confirmed).
uint8_t gpsSats();

// Time since first fix was acquired (ms), or 0 if no fix yet.
uint32_t gpsFixAgeMs();

// Time spent searching (ms). If fix acquired, returns time-to-first-fix.
uint32_t gpsSearchTimeMs();

// Course over ground in degrees (0=N, 90=E). Valid only when moving with a fix.
float gpsCourse();

// Speed over ground in km/h.
float gpsSpeedKmh();

// UTC date/time from GPS fix (year, month, day, hour, minute, second).
// Returns false until a valid fix with valid date+time is available.
bool gpsUtcDateTime(int &year, int &month, int &day,
					int &hour, int &minute, int &second);
