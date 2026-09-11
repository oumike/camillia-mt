#include "weather.h"
#include "config.h"

#if HAS_WEATHER

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static volatile WeatherState s_state     = WEATHER_IDLE;
static volatile int          s_httpCode  = 0;
static volatile bool         s_taskAlive = false;

static char   s_server[96];
static double s_lat = 0, s_lon = 0;
static bool   s_imperial = false;

// The last good reading, kept across errors on purpose: a stale number with its
// age on it is more use than a blank screen when a refresh fails.
static WeatherReading s_last;
static bool           s_haveLast = false;

WeatherState weatherState()   { return s_state; }
int          weatherHttpCode() { return s_httpCode; }

void weatherReset() {
    // Never yank the state out from under a running fetch — the worker owns
    // s_last until it publishes.
    if (s_state == WEATHER_FETCHING) return;
    s_state = WEATHER_IDLE;
    s_httpCode = 0;
}

bool weatherLatest(WeatherReading &out) {
    if (!s_haveLast) return false;
    out = s_last;
    return true;
}

uint32_t weatherAgeMs() {
    if (!s_haveLast) return 0;
    return (uint32_t)(millis() - s_last.fetchedMs);
}

// ── Parse ────────────────────────────────────────────────────────────────────
// v1,temp,feels,humidity,wind,gust,dir,code,isday,epoch,tunit,sunit,desc
//
// Split in place over exactly thirteen fields. Anything else — a captive
// portal's HTML, a reverse proxy's error page, a future contract version — is
// rejected rather than half-read, which is the whole point of the leading "v1".
// v1 is thirteen fields; v2 adds the place name as a fourteenth. The place is
// last precisely so it can contain the comma in "Golden, CO" — the split stops
// once the earlier fields are out, and whatever remains is taken verbatim.
static constexpr int kWeatherFieldsV1 = 13;
static constexpr int kWeatherFieldsV2 = 14;

static bool weatherParse(char *body, WeatherReading &out) {
    char *f[kWeatherFieldsV2] = {nullptr};
    int n = 0;
    char *p = body;
    f[n++] = p;
    while (*p && n < kWeatherFieldsV2) {
        if (*p == ',') { *p = '\0'; f[n++] = p + 1; }
        p++;
    }
    // Trim the trailing newline a proxy or curl-friendly server may add.
    for (char *e = f[n - 1]; *e; ++e) {
        if (*e == '\r' || *e == '\n') { *e = '\0'; break; }
    }

    // The version decides the field count, rather than the count being guessed
    // from what arrived: a short v2 line is a broken v2 line, not a v1 one.
    const bool v2 = (strcmp(f[0], "v2") == 0);
    const bool v1 = (strcmp(f[0], "v1") == 0);
    if (!v1 && !v2) return false;
    if (n != (v2 ? kWeatherFieldsV2 : kWeatherFieldsV1)) return false;

    out.temp          = atoi(f[1]);
    out.feels         = atoi(f[2]);
    out.humidityPct   = atoi(f[3]);
    out.wind          = atoi(f[4]);
    out.gust          = atoi(f[5]);
    out.dirDeg        = atoi(f[6]) % 360;
    out.code          = atoi(f[7]);
    out.isDay         = (atoi(f[8]) != 0);
    out.observedEpoch = (uint32_t)strtoul(f[9], nullptr, 10);
    strncpy(out.tempUnit, f[10], sizeof(out.tempUnit) - 1);
    out.tempUnit[sizeof(out.tempUnit) - 1] = '\0';
    strncpy(out.windUnit, f[11], sizeof(out.windUnit) - 1);
    out.windUnit[sizeof(out.windUnit) - 1] = '\0';
    strncpy(out.desc, f[12], sizeof(out.desc) - 1);
    out.desc[sizeof(out.desc) - 1] = '\0';
    if (!out.desc[0]) strncpy(out.desc, "Unknown", sizeof(out.desc) - 1);
    out.place[0] = '\0';
    if (v2) {
        strncpy(out.place, f[13], sizeof(out.place) - 1);
        out.place[sizeof(out.place) - 1] = '\0';
    }
    return true;
}

// ── Fetch ────────────────────────────────────────────────────────────────────
// Plain HTTP by necessity, exactly as losFetch() is and for the same reason.
static bool weatherFetch(WeatherReading &out) {
    if (WiFi.status() != WL_CONNECTED) { s_httpCode = -1; return false; }

    char base[sizeof(s_server)];
    strncpy(base, s_server, sizeof(base));
    base[sizeof(base) - 1] = '\0';
    size_t blen = strlen(base);
    while (blen > 0 && base[blen - 1] == '/') base[--blen] = '\0';

    char url[sizeof(base) + 64];
    snprintf(url, sizeof(url), "%s/weather?lat=%.2f&lon=%.2f&units=%c",
             base, s_lat, s_lon, s_imperial ? 'i' : 'm');

    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    // 20 s, matching LOS. A cold request is proxy -> upstream API, and the
    // reference proxy allows 12 s upstream plus a retry, so a shorter read
    // timeout would abandon requests that were going to succeed. Affordable
    // because this runs on the worker: a slow fetch costs the "Fetching..."
    // label staying up, not UI latency.
    http.setTimeout(20000);
    if (!http.begin(client, url)) { s_httpCode = -2; return false; }

    const int code = http.GET();
    s_httpCode = code;
    if (code != HTTP_CODE_OK) {
        http.end();
        client.stop();
        return false;
    }
    String body = http.getString();
    http.end();
    client.stop();

    // One line of a documented contract; anything much larger is not it, and
    // copying it into the parser's buffer would be the wrong response.
    if (body.length() == 0 || body.length() > 255) { s_httpCode = -5; return false; }
    char buf[256];
    strncpy(buf, body.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    if (!weatherParse(buf, out)) { s_httpCode = -6; return false; }
    out.lat = s_lat;
    out.lon = s_lon;
    out.fetchedMs = millis();
    return true;
}

static void weatherTask(void *) {
    WeatherReading r = {};
    const bool ok = weatherFetch(r);
    if (ok) {
        s_last = r;
        s_haveLast = true;
        s_state = WEATHER_OK;
    } else {
        // -6 is "answered, but not this contract", which is a different thing
        // to tell the user than a transport failure.
        s_state = (s_httpCode == -6) ? WEATHER_ERR_BADREPLY : WEATHER_ERR_HTTP;
    }
    s_taskAlive = false;
    vTaskDelete(nullptr);
}

bool weatherRequest(const char *server, double lat, double lon, bool imperial) {
    if (s_state == WEATHER_FETCHING || s_taskAlive) return false;
    if (!server || !server[0]) { s_state = WEATHER_ERR_NO_SERVER; return false; }
    if (WiFi.status() != WL_CONNECTED) { s_state = WEATHER_ERR_NO_WIFI; return false; }

    strncpy(s_server, server, sizeof(s_server));
    s_server[sizeof(s_server) - 1] = '\0';
    // Rounded here, before it leaves: two decimals is about a kilometre, which
    // is finer than any weather answer needs. The proxy rounds again, so a
    // precise fix cannot reach the upstream even by accident.
    s_lat = round(lat * 100.0) / 100.0;
    s_lon = round(lon * 100.0) / 100.0;
    s_imperial = imperial;

    s_httpCode = 0;
    s_state = WEATHER_FETCHING;
    s_taskAlive = true;
    // Core 0 and short-lived, like the LOS worker: the stack is held for one
    // fetch rather than parked for the session.
    if (xTaskCreatePinnedToCore(weatherTask, "weather", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
        s_taskAlive = false;
        s_state = WEATHER_ERR_HTTP;
        s_httpCode = -4;
        return false;
    }
    return true;
}

#else   // !HAS_WEATHER

bool weatherRequest(const char *, double, double, bool) { return false; }
WeatherState weatherState() { return WEATHER_IDLE; }
int      weatherHttpCode() { return 0; }
void     weatherReset() {}
bool     weatherLatest(WeatherReading &) { return false; }
uint32_t weatherAgeMs() { return 0; }

#endif  // HAS_WEATHER
