#pragma once
#include <Arduino.h>

// GPS lifecycle, fix state, and telemetry accessors.

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

// Duty cycling: park the receiver in a RAM-retained standby between position
// samples instead of tracking continuously. periodS is how long to stay parked;
// a period below the internal floor (120 s) is treated as "off", because below
// that the re-acquire costs more than the standby saves. RAM is retained so the
// next start is a hot one.
//
// Self-disabling: if a wake ever fails to produce NMEA, duty cycling turns
// itself off for the session and leaves the receiver running. The standby
// command dialect cannot be verified at runtime, and a wedged GPS is a much
// worse outcome than a missed battery saving.
void gpsSetDutyCycle(bool enabled, uint32_t periodS);

// True while the receiver is parked in standby. Fix accessors report no fix
// during this window; the last known position lives in config.
bool gpsIsAsleep();

// True if a valid fix has been received within the last 5 seconds.
bool gpsHasFix();

// True once valid NMEA sentences are seen from the GPS UART stream.
bool gpsHasNmeaStream();

// Milliseconds since the last received GPS UART byte.
// If no byte has ever arrived since gpsBegin(), returns search time since start.
// Returns UINT32_MAX when GPS is disabled.
uint32_t gpsDataAgeMs();

// Position as scaled integers (degrees × 1e7). Valid only when gpsHasFix().
int32_t gpsLatI();
int32_t gpsLonI();
int32_t gpsAltM();

// Satellite count (prefers satellites used; falls back to satellites in view).
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
