#include "los.h"
#include "config.h"

#if HAS_NODE_LOS

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <math.h>
#include <string.h>

// ── State shared with the worker ─────────────────────────────────────────────
// The fetch runs on its own short-lived task so a slow endpoint cannot stall
// lv_timer_handler(). Only the worker writes the profile, and only between
// setting LOS_FETCHING and publishing a terminal state, so a plain volatile
// state word is enough of a handoff — the UI never reads the arrays until the
// state says they are finished.
static volatile LosState s_state = LOS_IDLE;
static volatile int      s_httpCode = 0;
static volatile bool     s_taskAlive = false;

static double s_selfLat = 0, s_selfLon = 0;
static double s_peerLat = 0, s_peerLon = 0;
static double s_freqMhz = 868.0;
static char   s_server[96];

static double s_sampLat[kLosSamples];
static double s_sampLon[kLosSamples];
static float  s_elev[kLosSamples];

// 4/3 earth radius (m). The standard radio-propagation model: refraction bends
// signals slightly downward, which is equivalent to a flatter earth.
static constexpr double kLosReEff = 8504000.0;

LosState losState()   { return s_state; }
int      losHttpCode() { return s_httpCode; }

void losReset() {
    // Never yank the state out from under a running fetch: the worker owns the
    // arrays until it publishes, and resetting mid-flight would let a second
    // request start writing them from another task.
    if (s_state == LOS_FETCHING) return;
    s_state = LOS_IDLE;
    s_httpCode = 0;
}

// ── Geometry ─────────────────────────────────────────────────────────────────
static double losDistanceKm(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
    const double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
    const double dp = (lat2 - lat1) * M_PI / 180.0;
    const double dl = (lon2 - lon1) * M_PI / 180.0;
    const double a = sin(dp / 2) * sin(dp / 2)
                   + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2.0 * R * atan2(sqrt(a), sqrt(1.0 - a));
}

static double losBearingDeg(double lat1, double lon1, double lat2, double lon2) {
    const double p1 = lat1 * M_PI / 180.0, p2 = lat2 * M_PI / 180.0;
    const double dl = (lon2 - lon1) * M_PI / 180.0;
    const double y = sin(dl) * cos(p2);
    const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = atan2(y, x) * 180.0 / M_PI;
    if (b < 0) b += 360.0;
    return b;
}

static const char *losCompass(double deg) {
    static const char *const d[] = {"N","NE","E","SE","S","SW","W","NW"};
    return d[(int)((deg + 22.5) / 45.0) & 7];
}

// ── Elevation gap repair ─────────────────────────────────────────────────────
// NaN marks a no-data point ("null" in the reply). Real elevations are never
// NaN, so this is a self-comparison rather than isnan() to dodge the <cmath>
// namespace ambiguity the reference port hit.
static inline bool losReal(float v) { return v == v; }

// Makes all n samples finite: leading/trailing gaps clamp to the nearest real
// reading, interior gaps interpolate linearly. Returns false when too little
// real data came back for the profile to mean anything, in which case the
// caller reports a data error rather than drawing a mostly-invented ridge.
static bool losRepair(float *m, int n, int parsed) {
    for (int i = parsed; i < n; ++i) m[i] = NAN;
    int valid = 0, first = -1, last = -1;
    for (int i = 0; i < n; ++i) {
        if (losReal(m[i])) { ++valid; if (first < 0) first = i; last = i; }
    }
    // Below roughly a third real coverage the shape would be mostly fabricated.
    if (valid < 2 || valid * 3 < n) return false;
    for (int i = 0; i < first; ++i) m[i] = m[first];
    for (int i = last + 1; i < n; ++i) m[i] = m[last];
    int i = first;
    while (i <= last) {
        if (losReal(m[i])) { ++i; continue; }
        const int lo = i - 1;
        int hi = i;
        while (hi <= last && !losReal(m[hi])) ++hi;
        const float a = m[lo], b = m[hi];
        const int span = hi - lo;
        for (int k = lo + 1; k < hi; ++k) {
            m[k] = a + (b - a) * (float)(k - lo) / (float)span;
        }
        i = hi;
    }
    return true;
}

// ── Fetch ────────────────────────────────────────────────────────────────────
// GET <server>/elev?locations=lat,lon|...  ->  CSV of metres, "null" for holes.
// Plain HTTP by necessity: this firmware has no TLS client, which is why the
// endpoint is operator-supplied rather than a public HTTPS elevation API.
static int losFetch(float *out) {
    if (WiFi.status() != WL_CONNECTED) { s_httpCode = -1; return -1; }

    const size_t kUrlSz = 1024;
    char *url = (char *)malloc(kUrlSz);   // worker-local; freed below
    if (!url) { s_httpCode = -3; return -1; }

    char base[sizeof(s_server)];
    strncpy(base, s_server, sizeof(base));
    base[sizeof(base) - 1] = '\0';
    size_t blen = strlen(base);
    while (blen > 0 && base[blen - 1] == '/') base[--blen] = '\0';

    int o = snprintf(url, kUrlSz, "%s/elev?locations=", base);
    for (int i = 0; i < kLosSamples && o > 0 && o < (int)kUrlSz - 24; ++i) {
        o += snprintf(url + o, kUrlSz - o, "%s%.5f,%.5f",
                      i ? "|" : "", s_sampLat[i], s_sampLon[i]);
    }

    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    // 20 s, not the 9 s this first had. A cold request goes proxy -> upstream
    // elevation API, and the reference proxy alone allows 12 s upstream plus a
    // retry — so a 9 s read timeout gave up on requests that were going to
    // succeed, and did it most often on exactly the uncached paths the user
    // just asked about. Affordable because this runs on the worker: a slow
    // fetch costs the "Analyzing..." label staying up, not UI latency.
    http.setTimeout(20000);
    if (!http.begin(client, url)) {
        free(url);
        s_httpCode = -2;
        return -1;
    }
    const int code = http.GET();
    s_httpCode = code;
    if (code != HTTP_CODE_OK) {
        http.end();
        client.stop();
        free(url);
        return -1;
    }
    String body = http.getString();
    http.end();
    client.stop();
    free(url);

    int cnt = 0;
    const char *p = body.c_str();
    // Keep the head of the body for the log below. One line, only on failure,
    // and it is the difference between "the proxy is misconfigured" and a
    // guess about terrain coverage.
    char head[49];
    strncpy(head, p, sizeof(head) - 1);
    head[sizeof(head) - 1] = '\0';
    for (char *h = head; *h; ++h) { if (*h == '\r' || *h == '\n') *h = ' '; }
    while (*p && cnt < kLosSamples) {
        while (*p == ' ') ++p;
        if (!strncmp(p, "null", 4)) { out[cnt++] = NAN; p += 4; }
        else {
            char *end = nullptr;
            const double v = strtod(p, &end);
            if (end == p) break;
            out[cnt++] = (float)v;
            p = end;
        }
        while (*p && *p != ',') ++p;
        if (*p == ',') ++p;
    }
    if (cnt == 0) {
        Serial.printf("[los] no elevation values in reply (%u bytes): \"%s\"\n",
                      (unsigned)body.length(), head);
    }
    return cnt;
}

static void losTask(void *arg) {
    (void)arg;
    const int parsed = losFetch(s_elev);
    if (parsed < 0) {
        s_state = LOS_ERR_HTTP;
    } else if (parsed == 0) {
        // 200 with a body that yielded no numbers at all. That is not terrain
        // with no coverage, it is the wrong endpoint answering — a reverse proxy
        // pointing at a website returns a perfectly good 200 full of HTML, and
        // reporting that as "data too sparse" sends the operator to look at the
        // path they chose instead of at the proxy config.
        s_state = LOS_ERR_BADREPLY;
    } else if (!losRepair(s_elev, kLosSamples, parsed)) {
        s_state = LOS_ERR_DATA;
    } else {
        s_state = LOS_OK;
    }
    s_taskAlive = false;
    vTaskDelete(nullptr);
}

bool losRequest(const char *server,
                double selfLat, double selfLon,
                double peerLat, double peerLon,
                double freqMhz) {
    if (s_state == LOS_FETCHING || s_taskAlive) return false;
    if (!server || !server[0]) { s_state = LOS_ERR_NO_SERVER; return false; }
    if (WiFi.status() != WL_CONNECTED) { s_state = LOS_ERR_NO_WIFI; return false; }

    strncpy(s_server, server, sizeof(s_server));
    s_server[sizeof(s_server) - 1] = '\0';
    s_selfLat = selfLat; s_selfLon = selfLon;
    s_peerLat = peerLat; s_peerLon = peerLon;
    s_freqMhz = (freqMhz > 1.0) ? freqMhz : 868.0;

    // Linear lat/lon interpolation. Fine below ~100 km, which is well past the
    // range where the terrain question is interesting.
    for (int i = 0; i < kLosSamples; ++i) {
        const double f = (double)i / (kLosSamples - 1);
        s_sampLat[i] = s_selfLat + f * (s_peerLat - s_selfLat);
        s_sampLon[i] = s_selfLon + f * (s_peerLon - s_selfLon);
    }

    s_httpCode = 0;
    s_state = LOS_FETCHING;
    s_taskAlive = true;
    // Core 0, and short-lived: the stack is only held for the duration of one
    // fetch rather than parked for the whole session like the VNC task.
    if (xTaskCreatePinnedToCore(losTask, "los", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
        s_taskAlive = false;
        s_state = LOS_ERR_HTTP;
        s_httpCode = -4;
        return false;
    }
    return true;
}

// ── Analysis ─────────────────────────────────────────────────────────────────
bool losAnalyze(float antSelfM, float antPeerM, LosAnalysis &out) {
    if (s_state != LOS_OK) return false;

    const double distKm = losDistanceKm(s_selfLat, s_selfLon, s_peerLat, s_peerLon);
    const double D = distKm * 1000.0;
    if (!(D > 1.0)) return false;   // same point, or nonsense input

    const double lambda = 300.0 / s_freqMhz;
    const double h0 = (double)s_elev[0] + antSelfM;
    const double hN = (double)s_elev[kLosSamples - 1] + antPeerM;

    double minClear = 1e9, worstD1 = 0, minFresMargin = 1e9, worstF1 = 0;
    int worstIdx = -1;

    for (int i = 1; i < kLosSamples - 1; ++i) {
        const double f = (double)i / (kLosSamples - 1);
        const double d1 = f * D, d2 = (1.0 - f) * D;
        // Curvature "bulge": how far the earth rises between the two ends.
        const double bulge = d1 * d2 / (2.0 * kLosReEff);
        const double terr  = (double)s_elev[i] + bulge;
        const double sight = h0 + f * (hN - h0);
        const double clear = sight - terr;
        const double F1    = sqrt(lambda * d1 * d2 / D);
        if (clear < minClear) { minClear = clear; worstIdx = i; worstD1 = d1; }
        const double fm = clear - 0.6 * F1;
        if (fm < minFresMargin) { minFresMargin = fm; worstF1 = F1; }
    }

    out.verdict = (minClear < 0)      ? LOS_BLOCKED
                : (minFresMargin < 0) ? LOS_MARGINAL
                                      : LOS_CLEAR;
    out.distanceKm = distKm;
    out.bearingDeg = losBearingDeg(s_selfLat, s_selfLon, s_peerLat, s_peerLon);
    out.compass    = losCompass(out.bearingDeg);
    out.minClearM  = minClear;
    out.worstD1M   = worstD1;
    out.worstF1M   = worstF1;
    out.freqMhz    = s_freqMhz;
    out.antSelfM   = antSelfM;
    out.antPeerM   = antPeerM;
    out.worstIdx   = worstIdx;
    out.h0M        = h0;
    out.hNM        = hN;
    for (int i = 0; i < kLosSamples; ++i) {
        const double f = (double)i / (kLosSamples - 1);
        const double d1 = f * D, d2 = (1.0 - f) * D;
        out.terrainM[i] = (float)((double)s_elev[i] + d1 * d2 / (2.0 * kLosReEff));
    }
    return true;
}

#else   // !HAS_NODE_LOS

bool losRequest(const char *, double, double, double, double, double) { return false; }
LosState losState() { return LOS_IDLE; }
int      losHttpCode() { return 0; }
void     losReset() {}
bool     losAnalyze(float, float, LosAnalysis &) { return false; }

#endif  // HAS_NODE_LOS
