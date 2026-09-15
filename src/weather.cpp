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
#include <strings.h>   // strncasecmp

static volatile WeatherState s_state     = WEATHER_IDLE;
static volatile int          s_httpCode  = 0;
static volatile bool         s_taskAlive = false;

static char   s_server[96];
static double s_lat = 0, s_lon = 0;
static bool   s_imperial = false;

// ── Address cache ────────────────────────────────────────────────────────────
// This exists because resolving a name here could freeze the whole device.
//
// WiFiClient::connect(hostname, ...) resolves through
// WiFiGenericClass::hostByName(), which setConnectTimeout() does not bound.
// That function takes a *process-wide* DNS lock -- WIFI_DNS_IDLE_BIT in the one
// Arduino event group -- and when the name does not resolve it holds the lock
// for up to 15 s. Any other task wanting to resolve anything then waits up to
// 16 s for the same bit before paying its own 15 s. The UI thread is one of
// those tasks, so a weather fetch aimed at a server that had gone away stalled
// the display for as much as half a minute, and again on the next refresh: the
// dashboard asks for weather on its own, so nobody had to open the screen for
// it to happen. (The same trap is documented at the state-map fetch in
// main_lvgl.cpp, which was made dead code rather than fixed.)
//
// So the name is resolved at most once per server address and the result kept;
// every fetch after that connects straight to the address. A failure starts a
// backoff, because retrying a name that does not resolve is precisely the thing
// that costs 15 s of everyone else's DNS.
static char      s_addrFor[sizeof(s_server)] = {0};
static IPAddress s_addrIp;
static bool      s_addrValid = false;
static uint8_t   s_addrFails = 0;
static uint32_t  s_addrNextTryMs = 0;

// 30 s, doubling to a 15 min ceiling. Long enough that a server which is really
// gone stops costing anything; short enough that one caused by the Wi-Fi coming
// up a moment late clears on the next look at the screen.
static uint32_t weatherBackoffMs(uint8_t fails) {
    uint32_t ms = 30000UL;
    for (uint8_t i = 1; i < fails && ms < 900000UL; i++) ms *= 2;
    return (ms > 900000UL) ? 900000UL : ms;
}

// Drop the cached address. Called when a connection to it fails: the name may
// have moved, and holding a dead address would keep the screen broken until the
// server string changed.
static void weatherForgetAddress() {
    s_addrValid = false;
    s_addrFor[0] = '\0';
}

// "http://host[:port]" -> host, port. Only http:// is accepted, for the reason
// the docs give: there is no TLS client on this path.
static bool weatherSplitServer(const char *server, char *host, size_t cap, uint16_t &port) {
    if (!server || !host || cap == 0) return false;
    const char *p = server;
    if (strncasecmp(p, "http://", 7) == 0)       p += 7;
    else if (strncasecmp(p, "https://", 8) == 0) return false;
    port = 80;
    size_t n = 0;
    while (*p && *p != '/' && *p != ':' && n + 1 < cap) host[n++] = *p++;
    host[n] = '\0';
    if (n == 0) return false;
    if (*p == ':') {
        const int v = atoi(p + 1);
        if (v <= 0 || v > 65535) return false;
        port = (uint16_t)v;
    }
    return true;
}

// Resolve `host` unless we already hold its address, honouring the backoff.
// Returns false without touching DNS while the backoff is running -- that is
// the part that stops the freeze repeating.
static bool weatherResolve(const char *host, int &errOut) {
    if (s_addrValid && strcmp(s_addrFor, host) == 0) return true;

    const uint32_t now = millis();
    if (s_addrFails > 0 && (int32_t)(now - s_addrNextTryMs) < 0) {
        errOut = WX_ERR_DNS_HOLD;
        return false;
    }

    IPAddress ip;
    // Literal addresses never reach the resolver: IPAddress::fromString()
    // short-circuits inside hostByName(), and taking that path here keeps a
    // numeric server out of the backoff bookkeeping entirely.
    if (!ip.fromString(host)) {
        if (!WiFi.hostByName(host, ip) || (uint32_t)ip == 0) {
            if (s_addrFails < 255) s_addrFails++;
            s_addrNextTryMs = now + weatherBackoffMs(s_addrFails);
            weatherForgetAddress();
            errOut = WX_ERR_DNS;
            return false;
        }
    }
    s_addrIp = ip;
    strncpy(s_addrFor, host, sizeof(s_addrFor) - 1);
    s_addrFor[sizeof(s_addrFor) - 1] = '\0';
    s_addrValid = true;
    s_addrFails = 0;
    return true;
}

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
    if (WiFi.status() != WL_CONNECTED) { s_httpCode = WX_ERR_NO_WIFI; return false; }

    char host[sizeof(s_server)];
    uint16_t port = 80;
    if (!weatherSplitServer(s_server, host, sizeof(host), port)) {
        s_httpCode = WX_ERR_BAD_URL;
        return false;
    }

    int resolveErr = 0;
    if (!weatherResolve(host, resolveErr)) { s_httpCode = resolveErr; return false; }

    char uri[96];
    snprintf(uri, sizeof(uri), "/weather?lat=%.2f&lon=%.2f&units=%c",
             s_lat, s_lon, s_imperial ? 'i' : 'm');

    // Connected by address, on purpose. This is the call that used to resolve,
    // and the cache above is the whole reason it no longer does -- so nothing on
    // this path can take the global DNS lock and stall the UI behind it.
    WiFiClient client;
    if (!client.connect(s_addrIp, port, 5000)) {
        // The address we hold no longer answers. Drop it so the next attempt
        // resolves again rather than retrying a host that has moved.
        weatherForgetAddress();
        s_httpCode = WX_ERR_CONNECT;
        return false;
    }

    HTTPClient http;
    http.setReuse(false);
    // 20 s, matching LOS. A cold request is proxy -> upstream API, and the
    // reference proxy allows 12 s upstream plus a retry, so a shorter read
    // timeout would abandon requests that were going to succeed. Affordable
    // because this runs on the worker: a slow fetch costs the "Fetching..."
    // label staying up, not UI latency.
    http.setTimeout(20000);
    // The hostname, not the address. HTTPClient::connect() returns early when
    // the client it was handed is already connected, so this never resolves and
    // never opens a second socket -- it only decides the Host header, and the
    // reference proxy is a vhost that answers 404 without the right one.
    if (!http.begin(client, host, port, uri)) {
        client.stop();
        s_httpCode = WX_ERR_BEGIN;
        return false;
    }

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
    if (body.length() == 0 || body.length() > 255) { s_httpCode = WX_ERR_BODY; return false; }
    char buf[256];
    strncpy(buf, body.c_str(), sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    if (!weatherParse(buf, out)) { s_httpCode = WX_ERR_PARSE; return false; }
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
        s_state = (s_httpCode == WX_ERR_PARSE) ? WEATHER_ERR_BADREPLY : WEATHER_ERR_HTTP;
    }
    s_taskAlive = false;
    vTaskDelete(nullptr);
}

bool weatherRequest(const char *server, double lat, double lon, bool imperial) {
    if (s_state == WEATHER_FETCHING || s_taskAlive) return false;
    if (!server || !server[0]) { s_state = WEATHER_ERR_NO_SERVER; return false; }
    if (WiFi.status() != WL_CONNECTED) { s_state = WEATHER_ERR_NO_WIFI; return false; }

    // A changed server address invalidates everything cached about the old one,
    // backoff included: a new address deserves an immediate try, not the
    // cooldown the previous one earned.
    if (strncmp(s_server, server, sizeof(s_server) - 1) != 0) {
        weatherForgetAddress();
        s_addrFails = 0;
        s_addrNextTryMs = 0;
    }
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
        s_httpCode = WX_ERR_TASK;
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
