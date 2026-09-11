#pragma once

#include <stdint.h>

// ── Current conditions ───────────────────────────────────────────────────────
// One reading for wherever this node is, fetched through an operator-run proxy.
//
// This firmware has no TLS client and every weather API is HTTPS-only, so the
// device speaks plain HTTP to something the operator runs and that speaks HTTPS
// upstream — the same arrangement the elevation proxy gives terrain LOS. The
// contract is deliberately tiny so the parser here stays small; see
// docs/WEATHER.md and tools/weather-proxy/.
//
//   GET <server>/weather?lat=&lon=&units=m|i
//   -> v2,temp,feels,humidity,wind,gust,dir,code,isday,epoch,tunit,sunit,desc,place
//
// v1 is the same line without the trailing place name, and is still accepted:
// an operator running an older proxy gets everything but the place.
//
// Every numeric field is whole, and the proxy has already converted to the
// units asked for, so nothing here does arithmetic on the reply.

enum WeatherState : uint8_t {
    WEATHER_IDLE = 0,
    WEATHER_FETCHING,
    WEATHER_OK,
    WEATHER_ERR_NO_SERVER,    // no proxy configured
    WEATHER_ERR_NO_WIFI,
    WEATHER_ERR_NO_POSITION,  // no GPS fix and no stored position
    WEATHER_ERR_HTTP,         // transport/status failure; see weatherHttpCode()
    WEATHER_ERR_BADREPLY,     // 200, but not this contract
};

struct WeatherReading {
    int  temp;            // whole degrees, already in the requested unit
    int  feels;
    int  humidityPct;
    int  wind;            // whole km/h or mph, per windUnit
    int  gust;
    int  dirDeg;          // where the wind comes FROM, 0-359
    int  code;            // WMO 0-99
    bool isDay;
    uint32_t observedEpoch;
    char tempUnit[4];     // "C" / "F"
    char windUnit[8];     // "km/h" / "mph"
    char desc[32];        // "Partly cloudy"
    char place[40];       // "Golden, CO"; empty when the proxy could not say
    double lat, lon;      // the rounded position this reading is for
    uint32_t fetchedMs;   // millis() when it landed, for the age line
};

// Kicks off a fetch on a worker task. Returns false and sets the state when it
// cannot start (no server, no Wi-Fi, already running). `imperial` picks the
// units the proxy converts to, so it should follow cfg.displayUnits.
//
// lat/lon are rounded to two decimals (~1 km) before they leave — weather is a
// town-resolution question and there is no reason to put a precise fix on the
// wire. The proxy rounds again on arrival.
bool weatherRequest(const char *server, double lat, double lon, bool imperial);

WeatherState weatherState();
int          weatherHttpCode();   // last HTTP status, or a negative internal code
void         weatherReset();      // back to WEATHER_IDLE, for closing the screen

// The last good reading, if there has ever been one. Returns true even when the
// current state is an error: a stale reading shown with its age beats an empty
// screen, and the caller decides how to say so.
bool weatherLatest(WeatherReading &out);

// Milliseconds since the last good reading landed, or 0 if there is none. What
// the screen uses to decide whether to refresh on open and what age to print.
uint32_t weatherAgeMs();

// How old a reading may be before it is worth fetching again. Both the Weather
// screen (on open) and the lock screen (on its minute tick) test against this,
// which is what keeps the actual request rate at one per TTL however often
// either of them looks.
static constexpr uint32_t kWeatherTtlMs = 10UL * 60UL * 1000UL;

// How old a reading may be and still be shown on the lock screen's status band.
//
// Deliberately generous next to the Weather screen's own ten-minute refresh
// window, and for a different job: that number decides when to spend a fetch,
// this one decides when a number stops being worth believing. The glance
// surface never fetches — it shows what the Weather screen last obtained — so
// without a ceiling a device left asleep overnight would wake showing
// yesterday's afternoon as if it were now.
static constexpr uint32_t kWeatherGlanceMaxAgeMs = 2UL * 60UL * 60UL * 1000UL;
