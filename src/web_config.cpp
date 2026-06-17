#include "web_config.h"
#include "base64_util.h"
#include "node_db.h"
#include "channel_mgr.h"
#include "dm_mgr.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include "debug_flags.h"
#include "gps.h"
#include "battery_util.h"
#include "utf8_utils.h"

static const uint32_t kConnectTimeout  = 10000;  // ms
static const uint32_t kReleaseCheckTimeoutMs = 7000;
static const char    *kLatestReleaseApiUrl = "https://api.github.com/repos/oumike/camillia-mt/releases/latest";
static const char    *kLatestVersionRawUrl = "https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION";
static const char    *kLatestReleasePageUrl = "https://github.com/oumike/camillia-mt/releases/latest";

static const char    *kUser            = "admin";
static const char    *kDefaultWebPass  = "admin";

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static WebServer      server(80);
static bool           running          = false;
static bool           gOnboarding      = false;
static char           ipBuf[16]        = "";
static char           sessionToken[17] = "";   // hex token; empty = no session
static RhinoConfig   *gCfg             = nullptr;
static WebCfgSaveCb   gOnSave          = nullptr;
static WebCfgScreenshotPngCb gOnScreenshotPng = nullptr;
static volatile bool  gAnnounceReq     = false;
static char           gWifiSsid[64]    = "";
static char           gWifiPass[64]    = "";
static char           gFlashMsg[128]   = "";
static bool           gRebootPending   = false;
static uint32_t       gRebootAtMs      = 0;

static int compareVersionTags(const char *a, const char *b);
static bool fetchLatestReleaseInfo(String &tagOut, String &urlOut, String &errOut);

enum ReleaseCheckState : uint8_t {
    RELEASE_CHECK_IDLE = 0,
    RELEASE_CHECK_PENDING,
    RELEASE_CHECK_RUNNING,
    RELEASE_CHECK_DONE_OK,
    RELEASE_CHECK_DONE_ERR,
};

static ReleaseCheckState gReleaseCheckState = RELEASE_CHECK_IDLE;
static bool              gReleaseCheckUpdateAvailable = false;
static uint32_t          gReleaseCheckStartedAtMs = 0;
static uint32_t          gReleaseCheckFinishedAtMs = 0;
static char              gReleaseCheckLatest[48] = "";
static char              gReleaseCheckUrl[192] = "";
static char              gReleaseCheckErr[192] = "";

static void clearReleaseCheckResult() {
    gReleaseCheckUpdateAvailable = false;
    gReleaseCheckStartedAtMs = 0;
    gReleaseCheckFinishedAtMs = 0;
    gReleaseCheckLatest[0] = '\0';
    gReleaseCheckUrl[0] = '\0';
    gReleaseCheckErr[0] = '\0';
}

static const char *releaseCheckStateName(ReleaseCheckState s) {
    if (s == RELEASE_CHECK_PENDING) return "pending";
    if (s == RELEASE_CHECK_RUNNING) return "running";
    if (s == RELEASE_CHECK_DONE_OK) return "done";
    if (s == RELEASE_CHECK_DONE_ERR) return "error";
    return "idle";
}

static void queueReleaseCheckNow() {
    clearReleaseCheckResult();
    gReleaseCheckState = RELEASE_CHECK_PENDING;
}

static void runQueuedReleaseCheck() {
    if (gReleaseCheckState != RELEASE_CHECK_PENDING) return;

    gReleaseCheckState = RELEASE_CHECK_RUNNING;
    gReleaseCheckStartedAtMs = millis();

    String latest;
    String url;
    String err;
    bool ok = fetchLatestReleaseInfo(latest, url, err);

    if (ok) {
        gReleaseCheckUpdateAvailable = compareVersionTags(APP_VERSION, latest.c_str()) < 0;
        strncpy(gReleaseCheckLatest, latest.c_str(), sizeof(gReleaseCheckLatest) - 1);
        gReleaseCheckLatest[sizeof(gReleaseCheckLatest) - 1] = '\0';
        strncpy(gReleaseCheckUrl, url.c_str(), sizeof(gReleaseCheckUrl) - 1);
        gReleaseCheckUrl[sizeof(gReleaseCheckUrl) - 1] = '\0';
        gReleaseCheckState = RELEASE_CHECK_DONE_OK;
    } else {
        strncpy(gReleaseCheckErr, err.c_str(), sizeof(gReleaseCheckErr) - 1);
        gReleaseCheckErr[sizeof(gReleaseCheckErr) - 1] = '\0';
        size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        char memDiag[96];
        snprintf(memDiag, sizeof(memDiag), " [heap int free=%u largest=%u]",
                 (unsigned)freeInt, (unsigned)largestInt);
        strncat(gReleaseCheckErr, memDiag,
                sizeof(gReleaseCheckErr) - strlen(gReleaseCheckErr) - 1);
        gReleaseCheckState = RELEASE_CHECK_DONE_ERR;
    }

    gReleaseCheckFinishedAtMs = millis();
}

// ── Helpers ───────────────────────────────────────────────────

static const char *currentWebCfgPassword() {
    if (gCfg && gCfg->webCfgPass[0]) return gCfg->webCfgPass;
    return kDefaultWebPass;
}

static bool isLoggedIn() {
    if (!sessionToken[0]) return false;
    String cookie = server.header("Cookie");
    String needle = String("sess=") + sessionToken;
    return cookie.indexOf(needle) >= 0;
}

static void buildDateTimeStamp(char *out, size_t outLen) {
    if (!out || outLen == 0) return;

    time_t now = time(nullptr);
    struct tm tmv;
    if (now > 0 && localtime_r(&now, &tmv)) {
        snprintf(out, outLen,
                 "%04d%02d%02d_%02d%02d%02d",
                 tmv.tm_year + 1900,
                 tmv.tm_mon + 1,
                 tmv.tm_mday,
                 tmv.tm_hour,
                 tmv.tm_min,
                 tmv.tm_sec);
        return;
    }

    strncpy(out, "19700101_000000", outLen - 1);
    out[outLen - 1] = '\0';
}

static void buildExportFileName(const char *shortName, char *out, size_t outLen) {
    char clean[24] = {};
    char stamp[20] = {};
    size_t j = 0;
    if (shortName) {
        for (size_t i = 0; shortName[i] && j < sizeof(clean) - 1; i++) {
            unsigned char c = (unsigned char)shortName[i];
            if (isalnum(c) || c == '_' || c == '-') clean[j++] = (char)c;
            else if (c == ' ' || c == '.')          clean[j++] = '_';
        }
    }
    if (j == 0) strncpy(clean, "node", sizeof(clean) - 1);
    buildDateTimeStamp(stamp, sizeof(stamp));
    snprintf(out, outLen, "%s_config_%s.yaml", clean, stamp);
}

static void buildScreenshotFileName(const char *shortName, char *out, size_t outLen) {
    char clean[24] = {};
    char stamp[20] = {};
    size_t j = 0;
    if (shortName) {
        for (size_t i = 0; shortName[i] && j < sizeof(clean) - 1; i++) {
            unsigned char c = (unsigned char)shortName[i];
            if (isalnum(c) || c == '_' || c == '-') clean[j++] = (char)c;
            else if (c == ' ' || c == '.')          clean[j++] = '_';
        }
    }
    if (j == 0) strncpy(clean, "node", sizeof(clean) - 1);
    buildDateTimeStamp(stamp, sizeof(stamp));
    snprintf(out, outLen, "%s_screen_%s.png", clean, stamp);
}

static void redirect(const char *path) {
    server.sendHeader("Location", path);
    server.send(303);
}

static void setFlashMsg(const char *msg) {
    if (!msg) { gFlashMsg[0] = '\0'; return; }
    strncpy(gFlashMsg, msg, sizeof(gFlashMsg) - 1);
    gFlashMsg[sizeof(gFlashMsg) - 1] = '\0';
}

static void redirectHomeWithFlash(const char *msg = "") {
    if (msg && msg[0]) setFlashMsg(msg);
    redirect("/");
}

static void scheduleReboot(uint32_t delayMs) {
    gRebootPending = true;
    gRebootAtMs = millis() + delayMs;
}

static bool monitorDebugEnabled() {
    if (!gCfg) return false;
    return gCfg->debugAcks || gCfg->debugMessages || gCfg->debugGps;
}

static void setMonitorDebugEnabled(bool enabled) {
    if (!gCfg) return;
    gCfg->debugAcks = enabled;
    gCfg->debugMessages = enabled;
    gCfg->debugGps = enabled;
    debugSetFlags(enabled, enabled, enabled);
}

static uint8_t readBatteryPctWeb() {
    return batteryReadPercent();
}

static void appendJsonEscaped(String &out, const char *s) {
    if (!s) return;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        unsigned char c = *p;
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) {
            char u[7];
            snprintf(u, sizeof(u), "\\u%04X", (unsigned)c);
            out += u;
        } else {
            out += (char)c;
        }
    }
}

static bool extractJsonStringField(const String &json, const char *key, String &value) {
    value = "";
    if (!key || !key[0]) return false;

    String needle = "\"";
    needle += key;
    needle += "\"";

    int keyPos = json.indexOf(needle);
    if (keyPos < 0) return false;

    int colonPos = json.indexOf(':', keyPos + needle.length());
    if (colonPos < 0) return false;

    int quotePos = json.indexOf('"', colonPos + 1);
    if (quotePos < 0) return false;

    bool escaped = false;
    for (int i = quotePos + 1; i < json.length(); i++) {
        char c = json.charAt(i);
        if (escaped) {
            if (c == 'n') value += '\n';
            else if (c == 'r') value += '\r';
            else if (c == 't') value += '\t';
            else value += c;
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') return true;
        value += c;
    }

    return false;
}

static int compareVersionTags(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";

    const char *pa = a;
    const char *pb = b;

    for (int seg = 0; seg < 8; seg++) {
        while (*pa && !isdigit((unsigned char)*pa)) pa++;
        while (*pb && !isdigit((unsigned char)*pb)) pb++;

        long va = 0;
        long vb = 0;
        bool hasA = false;
        bool hasB = false;

        while (isdigit((unsigned char)*pa)) {
            hasA = true;
            va = (va * 10L) + (*pa - '0');
            pa++;
        }
        while (isdigit((unsigned char)*pb)) {
            hasB = true;
            vb = (vb * 10L) + (*pb - '0');
            pb++;
        }

        if (!hasA && !hasB) return 0;
        if (!hasA) va = 0;
        if (!hasB) vb = 0;
        if (va < vb) return -1;
        if (va > vb) return 1;
    }

    return 0;
}

static void trimAsciiWhitespace(String &s) {
    int start = 0;
    while (start < s.length() && isspace((unsigned char)s.charAt(start))) start++;

    int end = s.length();
    while (end > start && isspace((unsigned char)s.charAt(end - 1))) end--;

    if (start == 0 && end == s.length()) return;
    s = s.substring(start, end);
}

static bool fetchLatestTagFromVersionFile(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    WiFiClientSecure rawClient;
    rawClient.setInsecure();

    HTTPClient raw;
    if (!raw.begin(rawClient, kLatestVersionRawUrl)) {
        errOut = "Failed to start VERSION request";
        return false;
    }

    raw.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
    raw.addHeader("User-Agent", "camillia-mt-webcfg");

    int rawCode = raw.GET();
    if (rawCode <= 0) {
        errOut = "VERSION network error";
        raw.end();
        return false;
    }
    if (rawCode != 200) {
        errOut = String("VERSION HTTP ") + String(rawCode);
        raw.end();
        return false;
    }

    tagOut = raw.getString();
    raw.end();
    trimAsciiWhitespace(tagOut);

    if (tagOut.length() == 0) {
        errOut = "VERSION file empty";
        return false;
    }

    return true;
}

static bool fetchLatestReleaseInfo(String &tagOut, String &urlOut, String &errOut) {
    tagOut = "";
    urlOut = "";
    errOut = "";

    if (WiFi.status() != WL_CONNECTED) {
        errOut = "WiFi not connected";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    String apiErr;
    HTTPClient http;
    if (!http.begin(client, kLatestReleaseApiUrl)) {
        apiErr = "Failed to start HTTPS request";
    } else {
        http.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
        http.addHeader("User-Agent", "camillia-mt-webcfg");
        http.addHeader("Accept", "application/vnd.github+json");

        int code = http.GET();
        if (code <= 0) {
            apiErr = "Network error";
        } else if (code != 200) {
            apiErr = String("Release API HTTP ") + String(code);
        } else {
            String body = http.getString();
            if (extractJsonStringField(body, "tag_name", tagOut) && tagOut.length() > 0) {
                (void)extractJsonStringField(body, "html_url", urlOut);
                if (urlOut.length() == 0) urlOut = kLatestReleasePageUrl;
                http.end();
                return true;
            }
            apiErr = "Release tag not found";
        }
        http.end();
    }

    String versionErr;
    if (fetchLatestTagFromVersionFile(tagOut, versionErr)) {
        urlOut = kLatestReleasePageUrl;
        return true;
    }

    if (apiErr.length() && versionErr.length()) {
        errOut = apiErr + "; fallback failed (" + versionErr + ")";
    } else if (apiErr.length()) {
        errOut = apiErr;
    } else {
        errOut = versionErr;
    }

    urlOut = kLatestReleasePageUrl;
    return false;
}

// ── HTML helpers ──────────────────────────────────────────────

static const char kHead[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Camillia MT</title>"
    "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'>"
    "<style>"
        ":root{--bg:#10141d;--panel:#1a2230;--panel-2:#232d3e;--line:#4a5b73;"
        "--text:#f4f6fb;--text-dim:#b0b8c8;--accent:#d7869d;--accent-ink:#ffffff}"
        "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;max-width:620px;"
        "margin:1.5em auto;padding:0 1em 2em;background:linear-gradient(180deg,var(--bg),var(--panel));"
        "color:var(--text)}"
        "h2{color:var(--accent);margin-bottom:.2em}"
        "h3{color:var(--text-dim);margin:1.2em 0 .4em;border-bottom:1px solid var(--line);padding-bottom:.2em}"
        "label{display:block;margin:.6em 0 .1em;font-size:.9em;color:var(--text)}"
    "input[type=text],input[type=number],input[type=password],select"
            "{width:100%;padding:.45em;box-sizing:border-box;border:1px solid var(--line);border-radius:5px;"
            "background:var(--panel-2);color:var(--text)}"
        "input[readonly]{background:var(--panel);color:var(--text-dim)}"
        "button{margin-top:1.2em;padding:.55em 1.8em;background:var(--accent);color:var(--accent-ink);"
                     "border:none;border-radius:5px;cursor:pointer;font-size:1em;font-weight:600}"
        ".msg{color:var(--accent);margin:.5em 0}"
        ".err{color:#ff8d8d;margin:.5em 0}"
        ".logout{float:right;font-size:.9em;color:var(--accent);text-decoration:none}"
           ".tab-row{display:flex;align-items:center;gap:.6em;margin:1em 0 .4em;flex-wrap:wrap}"
           ".tab-btns{display:flex;gap:.5em;flex-wrap:wrap}"
           ".tab-metrics{margin-left:auto;display:flex;gap:.4em;align-items:center;flex-wrap:wrap}"
           ".metric-chip{padding:.28em .62em;border-radius:999px;border:1px solid var(--line);"
               "background:var(--panel-2);font-size:.78em;font-weight:700;line-height:1.2;white-space:nowrap}"
           ".metric-good{color:#8ef2b8;border-color:#3e8f66}"
           ".metric-warn{color:#ffd181;border-color:#a57a2d}"
           ".metric-bad{color:#ff9f9f;border-color:#a75454}"
        ".tab-btn{margin:0;padding:.45em 1em;background:var(--panel-2);color:var(--text);"
             "border:1px solid var(--line);border-radius:999px;cursor:pointer;font-size:.9em;font-weight:600}"
        ".tab-btn.active{background:var(--accent);color:var(--accent-ink);border-color:var(--accent)}"
        ".tab-panel{display:none}"
        ".tab-panel.active{display:block}"
        ".map-wrap{margin-top:.6em;border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:.5em}"
        ".map-canvas{display:block;width:100%;height:320px;border-radius:6px;overflow:hidden;background:#08141f}"
        ".map-controls{display:flex;gap:.45em;flex-wrap:wrap;margin:.15em 0 .45em}"
        ".map-mini-btn{margin:0;padding:.35em .75em;background:var(--panel-2);color:var(--text);"
             "border:1px solid var(--line);border-radius:999px;cursor:pointer;font-size:.82em;font-weight:600}"
           ".map-mini-right{margin-left:auto}"
        ".map-mini-btn.active{background:var(--accent);color:var(--accent-ink);border-color:var(--accent)}"
        ".map-mini-btn:disabled{opacity:.6;cursor:default}"
        ".map-legend{display:flex;justify-content:space-between;font-size:.8em;color:var(--text-dim);margin-top:.35em;gap:.5em}"
        ".node-list{margin-top:.8em;display:grid;grid-template-columns:1fr;gap:.5em}"
        ".node-card{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:.55em .7em}"
        ".node-title{font-weight:700;color:var(--accent);margin-bottom:.2em}"
        ".node-meta{font-size:.82em;color:var(--text-dim);margin:.15em 0;word-break:break-word}"
        ".node-meta b{color:var(--text)}"
        ".live-wrap{margin-top:.6em;border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:.5em}"
        ".live-toolbar{display:flex;justify-content:space-between;align-items:center;gap:.5em;margin-bottom:.45em}"
        ".live-toolbar span{font-size:.68em;color:var(--text-dim)}"
        ".live-feed{height:380px;overflow:auto;border:1px solid var(--line);border-radius:6px;background:var(--bg)}"
        ".live-line{padding:.13em .38em;border-bottom:1px solid rgba(255,255,255,.08);"
            "font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.7em;line-height:1.2;"
            "color:#dfe7ef;background:#10254a}"
        ".live-line:nth-child(odd){filter:brightness(1.03)}"
        ".live-line.live-default{background:#10254a;color:#dfe7ef}"
        ".live-line.live-err{background:#4a1d1d;color:#ff8f8f;font-weight:700}"
        ".live-line.live-tx-ack{background:#1e3e27;color:#89e7a5}"
        ".live-line.live-rx-ack{background:#1b3e34;color:#7de8d2}"
        ".live-line.live-tx-text{background:#4a4318;color:#ffd56b}"
        ".live-line.live-tx-node{background:#4a2d1f;color:#f7b46d}"
        ".live-line.live-tx-pos{background:#4a3418;color:#ffc875}"
        ".live-line.live-tx-tlm{background:#1a3b40;color:#84efff}"
        ".live-line.live-tx-dm{background:#33224a;color:#f2a4ff}"
        ".live-line.live-rx-text{background:#12345d;color:#67d8ff}"
        ".live-line.live-rx-node{background:#1d2e58;color:#9ec3ff}"
        ".live-line.live-rx-pos{background:#1a3754;color:#b6e5ff}"
        ".live-line.live-rx-tlm{background:#1a3a3e;color:#9ceadf}"
        ".live-line.live-rx-enc{background:#4a3618;color:#ffbe73}"
        ".live-line.live-rx-other{background:#102d52;color:#8fbfff}"
        ".live-line.live-tx-other{background:#3e3619;color:#e7c96f}"
        ".leaflet-container{font:12px/1.2 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}"
        ".leaflet-control-attribution{font-size:.65em}"
    ".row2{display:grid;grid-template-columns:1fr 1fr;gap:.5em}"
    ".gps-note{margin:.4em 0;font-size:.9em}"
    ".gps-note button{padding:.2em .8em;font-size:.9em;margin-left:.4em}"
        ".gps-hint{font-size:.8em;color:var(--text-dim);margin:.2em 0 .6em}"
        "details{border:1px solid var(--line);border-radius:6px;margin:.8em 0;padding:0 .8em;background:var(--panel)}"
    "details[open]{padding-bottom:.8em}"
        "summary{font-size:1em;font-weight:600;color:var(--accent);cursor:pointer;"
             "padding:.5em 0;list-style:none}"
    "summary::-webkit-details-marker{display:none}"
    "summary::before{content:'\\25B6\\00A0';font-size:.8em}"
    "details[open] summary::before{content:'\\25BC\\00A0';font-size:.8em}"
    ".ch-row{display:grid;grid-template-columns:1fr 2fr auto;gap:.4em;align-items:end;margin:.4em 0}"
    ".ch-row label{margin:0;font-size:.85em}"
        "@media (max-width:560px){.row2{grid-template-columns:1fr}}"
    "</style></head><body>";

// ── Timezone table ────────────────────────────────────────────

struct TzOption { const char *label; const char *posix; };
static const TzOption kTzOptions[] = {
    { "UTC",                                "UTC0"                                   },
    { "US — Hawaii (UTC-10)",               "HST10"                                  },
    { "US — Alaska (UTC-9/-8)",             "AKST9AKDT,M3.2.0,M11.1.0"              },
    { "US — Pacific (UTC-8/-7)",            "PST8PDT,M3.2.0,M11.1.0"                },
    { "US — Mountain (UTC-7/-6)",           "MST7MDT,M3.2.0,M11.1.0"                },
    { "US — Arizona, no DST (UTC-7)",       "MST7"                                   },
    { "US — Central (UTC-6/-5)",            "CST6CDT,M3.2.0,M11.1.0"                },
    { "US — Eastern (Detroit) (UTC-5/-4)",  "EST5EDT,M3.2.0,M11.1.0"                },
    { "Canada — Atlantic (UTC-4/-3)",       "AST4ADT,M3.2.0/0,M11.1.0/0"            },
    { "Brazil — Brasilia (UTC-3)",          "BRT3BRST,M10.3.0/0,M2.3.0/0"           },
    { "Argentina (UTC-3)",                  "ART3"                                   },
    { "UK (UTC+0/+1)",                      "GMT0BST,M3.5.0/1,M10.5.0"              },
    { "Western Europe — CET (UTC+1/+2)",    "CET-1CEST,M3.5.0,M10.5.0/3"           },
    { "Eastern Europe — EET (UTC+2/+3)",    "EET-2EEST,M3.5.0/3,M10.5.0/4"         },
    { "Russia — Moscow (UTC+3)",            "MSK-3"                                  },
    { "India (UTC+5:30)",                   "IST-5:30"                               },
    { "China / Singapore (UTC+8)",          "CST-8"                                  },
    { "Japan / Korea (UTC+9)",              "JST-9"                                  },
    { "Australia — Perth (UTC+8)",          "AWST-8"                                 },
    { "Australia — Eastern (UTC+10/+11)",   "AEST-10AEDT,M10.1.0,M4.1.0/3"         },
    { "New Zealand (UTC+12/+13)",           "NZST-12NZDT,M9.5.0,M4.1.0/3"          },
};
static const int kTzCount = (int)(sizeof(kTzOptions) / sizeof(kTzOptions[0]));

// ── Login page ────────────────────────────────────────────────

static void sendLoginPage(const char *err = "") {
    String html = kHead;
    html +=
        "<h2>Camillia MT</h2>"
        "<form method='POST' action='/login'>"
        "<label>Username<input name='u' type='text' autofocus autocomplete='username'></label>"
        "<label>Password<input name='p' type='password' autocomplete='current-password'></label>"
        "<button type='submit'>Login</button>";
    if (err[0]) {
        html += "<p class='err'>";
        html += err;
        html += "</p>";
    }
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

// ── Config page ───────────────────────────────────────────────

// Helper: flush a chunk of HTML and reset the buffer
static void sendChunk(String &html) {
    server.sendContent(html);
    html = "";
}

static void sendConfigPage(const char *msg = "") {
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }

    // Use chunked transfer to avoid building one giant String
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    char tmp[96];
    String html = kHead;
    uint8_t themeBase = (uint8_t)constrain((int)gCfg->uiTheme, 0, UI_THEME_COUNT - 1);
    uint8_t themePreset = (uint8_t)(themeBase * 2 + (gCfg->uiMode == UI_MODE_LIGHT ? 1 : 0));
    int totalNodes = Nodes.count();
    uint8_t battPct = readBatteryPctWeb();
    const char *battCls = (battPct >= 60) ? "metric-good" : ((battPct >= 25) ? "metric-warn" : "metric-bad");
    bool gpsEn = gpsIsEnabled();
    bool gpsFix = gpsHasFix();
    bool gpsNmea = gpsHasNmeaStream();
    uint8_t gpsSat = gpsSats();
    const char *gpsCls = !gpsEn ? "metric-bad" : (gpsFix ? "metric-good" : (gpsNmea ? "metric-warn" : "metric-bad"));
    char battChip[24];
    snprintf(battChip, sizeof(battChip), "BAT %u%%", (unsigned)battPct);
    char gpsChip[48];
    if (!gpsEn) snprintf(gpsChip, sizeof(gpsChip), "GPS OFF");
    else if (gpsFix) snprintf(gpsChip, sizeof(gpsChip), "GPS FIX %u", (unsigned)gpsSat);
    else if (!gpsNmea) snprintf(gpsChip, sizeof(gpsChip), "GPS NO DATA");
    else snprintf(gpsChip, sizeof(gpsChip), "GPS SEARCH %u", (unsigned)gpsSat);
    int mapPointCount = 0;
    String mapPoints = "[";
    String nodeCards = "";
    uint32_t nowMs = millis();
    auto unzz = [](uint32_t v) -> int32_t {
        return (int32_t)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
    };
    auto coordInRange = [](float lat, float lon) -> bool {
        return !(lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f);
    };
    float mapMeLat = gCfg->latI * 1e-7f;
    float mapMeLon = gCfg->lonI * 1e-7f;
    bool mapHasMe = coordInRange(mapMeLat, mapMeLon);
    auto extractNodeCoords = [&](const NodeEntry *n, float &lat, float &lon) -> bool {
        if (!n) return false;
        bool hasCoords = (n->latI != 0 || n->lonI != 0);
        if (!n->hasPosition && !hasCoords) return false;
        lat = n->latI * 1e-7f;
        lon = n->lonI * 1e-7f;
        if (coordInRange(lat, lon)) return true;
        // Backward compatibility: older firmware decoded signed lat/lon without zigzag.
        int32_t latRecovered = unzz((uint32_t)n->latI);
        int32_t lonRecovered = unzz((uint32_t)n->lonI);
        float latRec = latRecovered * 1e-7f;
        float lonRec = lonRecovered * 1e-7f;
        if (!coordInRange(latRec, lonRec)) return false;
        lat = latRec;
        lon = lonRec;
        return true;
    };
    const bool useImperialUnits = (gCfg && gCfg->displayUnits != 0);
    for (int i = 0; i < totalNodes; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n) continue;

        float lat = 0.0f;
        float lon = 0.0f;
        bool hasLocation = extractNodeCoords(n, lat, lon);
        if (hasLocation) {
            if (mapPointCount > 0) mapPoints += ",";
            snprintf(tmp, sizeof(tmp), "{\"lat\":%.7f,\"lon\":%.7f}", lat, lon);
            mapPoints += tmp;
            mapPointCount++;
        }

        char idBuf[16];
        snprintf(idBuf, sizeof(idBuf), "!%08X", n->nodeId);
        const char *shortName = n->shortName[0] ? n->shortName : "----";
        const char *longName  = n->longName[0]  ? n->longName  : "(unnamed)";
        const char *chanName = "-";
        if (n->chanIdx >= 0 && n->chanIdx < MAX_CHANNELS) {
            const ChannelKey &ck = CHANNEL_KEYS[n->chanIdx];
            const char *nm = ck.name_buf[0] ? ck.name_buf : ck.name;
            if (nm && nm[0]) chanName = nm;
        }

        char heardBuf[40];
        if (n->lastHeardMs == 0 || nowMs < n->lastHeardMs) {
            snprintf(heardBuf, sizeof(heardBuf), "unknown");
        } else {
            snprintf(heardBuf, sizeof(heardBuf), "%lus ago",
                     (unsigned long)((nowMs - n->lastHeardMs) / 1000UL));
        }

        char locBuf[96];
        if (hasLocation) {
            snprintf(locBuf, sizeof(locBuf), "%.6f, %.6f (alt %dm)", lat, lon, (int)n->alt);
        } else {
            snprintf(locBuf, sizeof(locBuf), "unknown");
        }

        char posSeenBuf[40];
        if (n->lastPosMs == 0 || nowMs < n->lastPosMs) {
            snprintf(posSeenBuf, sizeof(posSeenBuf), "never");
        } else {
            snprintf(posSeenBuf, sizeof(posSeenBuf), "%lus ago",
                     (unsigned long)((nowMs - n->lastPosMs) / 1000UL));
        }

        char posStateBuf[48];
        if (n->lastPosMs == 0) {
            snprintf(posStateBuf, sizeof(posStateBuf), "no POSITION packets");
        } else if (n->hasPosition) {
            snprintf(posStateBuf, sizeof(posStateBuf), "position valid");
        } else {
            snprintf(posStateBuf, sizeof(posStateBuf), "packet seen, no fix");
        }

        char rawPosBuf[64];
        snprintf(rawPosBuf, sizeof(rawPosBuf), "%ld, %ld", (long)n->latI, (long)n->lonI);

        char telemBuf[180];
        if (n->hasTelemetry) {
            telemBuf[0] = '\0';
            if (n->hasDeviceTelemetry) {
                snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf),
                         "%.0f%% / %.2fV", n->battPct, n->voltage);
            }
            if (n->hasEnvironmentTelemetry) {
                if (telemBuf[0]) {
                    snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf), " | ");
                }
                if (useImperialUnits) {
                    float tempF = n->temperatureC * (9.0f / 5.0f) + 32.0f;
                    float pressureInHg = n->pressureHpa * 0.0295299831f;
                    snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf),
                             "%.1fF %.1f%% %.2finHg",
                             (double)tempF,
                             (double)n->humidityPct,
                             (double)pressureInHg);
                } else {
                    snprintf(telemBuf + strlen(telemBuf), sizeof(telemBuf) - strlen(telemBuf),
                             "%.1fC %.1f%% %.1fhPa",
                             (double)n->temperatureC,
                             (double)n->humidityPct,
                             (double)n->pressureHpa);
                }
            }
            if (!telemBuf[0]) {
                snprintf(telemBuf, sizeof(telemBuf), "none");
            }
        } else {
            snprintf(telemBuf, sizeof(telemBuf), "none");
        }

        char linkBuf[72];
        if (n->lastHeardMs != 0) {
            snprintf(linkBuf, sizeof(linkBuf), "SNR %.1f dB, hops %u", n->snr, (unsigned)n->hops);
        } else {
            snprintf(linkBuf, sizeof(linkBuf), "unknown");
        }

        nodeCards += "<div class='node-card'>";
        nodeCards += "<div class='node-title'>";
        nodeCards += shortName;
        nodeCards += " -- ";
        nodeCards += longName;
        nodeCards += "</div>";
        nodeCards += "<div class='node-meta'><b>ID:</b> ";
        nodeCards += idBuf;
        nodeCards += "  <b>Channel:</b> ";
        nodeCards += chanName;
        nodeCards += "  <b>Last Heard:</b> ";
        nodeCards += heardBuf;
        nodeCards += "</div>";
        nodeCards += "<div class='node-meta'><b>Location:</b> ";
        nodeCards += locBuf;
        nodeCards += "</div>";
        nodeCards += "<div class='node-meta'><b>Position Pkt:</b> ";
        nodeCards += posSeenBuf;
        nodeCards += "  <b>State:</b> ";
        nodeCards += posStateBuf;
        nodeCards += "  <b>Raw:</b> ";
        nodeCards += rawPosBuf;
        nodeCards += "</div>";
        nodeCards += "<div class='node-meta'><b>Telemetry:</b> ";
        nodeCards += telemBuf;
        nodeCards += "  <b>Link:</b> ";
        nodeCards += linkBuf;
        nodeCards += "</div>";
        nodeCards += "</div>";
    }
    mapPoints += "]";
    html += "<h2>Camillia MT <a class='logout' href='/logout'>Logout</a></h2>";

    if (msg[0]) { html += "<p class='msg'>"; html += msg; html += "</p>"; }

        html += "<div class='tab-row'><div class='tab-btns'>"
            "<button type='button' class='tab-btn active' id='tab-btn-config' onclick=\"switchTab('config')\">Config</button>"
            "<button type='button' class='tab-btn' id='tab-btn-utils' onclick=\"switchTab('utils')\">Utilities</button>"
            "<button type='button' class='tab-btn' id='tab-btn-live' onclick=\"switchTab('live')\">Live</button>"
            "<button type='button' class='tab-btn' id='tab-btn-map' onclick=\"switchTab('map')\">Map</button>"
            "</div><div class='tab-metrics'><span class='metric-chip ";
        html += battCls;
        html += "'>";
        html += battChip;
        html += "</span><span class='metric-chip ";
        html += gpsCls;
        html += "'>";
        html += gpsChip;
        html += "</span></div></div>";
        html += "<div class='tab-panel active' id='tab-config'>";

    html += "<form method='POST' action='/save'>";

    // ── Node Identity ─────────────────────────────────────────
    html += "<details open><summary>Node Identity</summary>";
    html += "<label>Long Name (max 39 chars)"
            "<input name='long' type='text' maxlength='39' value='";
    html += gCfg->nodeLong; html += "'></label>";
    html += "<label>Short Name (max 4 chars)"
            "<input name='short' type='text' maxlength='4' value='";
    html += gCfg->nodeShort; html += "'></label>";
    // Node ID override (developer option — hidden for end users)
    // snprintf(tmp, sizeof(tmp), "%08lx", (unsigned long)gCfg->nodeIdOverride);
    // html += "<label>Node ID Override ...";
    html += "</details>";

        html += "<details><summary>Web Config Access</summary>";
        html += "<label>Username<input type='text' value='admin' readonly></label>";
        html += "<label>Password (leave blank to keep current)"
            "<input name='web_pass' type='password' maxlength='63' autocomplete='new-password'"
            " placeholder='Enter new password'></label>";
        html += "</details>";
    sendChunk(html);

    // ── Device ────────────────────────────────────────────────
    html += "<details open><summary>Device</summary>";
    html += "<div class='row2'>";
    html += "<label>Role<select name='role'>";
    static const struct { uint8_t v; const char *l; } kRoles[] = {
        {0,"CLIENT"},{1,"CLIENT_MUTE"},{2,"ROUTER"},{3,"ROUTER_CLIENT"},
        {4,"REPEATER"},{5,"TRACKER"},{6,"SENSOR"},{7,"TAK"},
        {8,"CLIENT_HIDDEN"},{9,"LOST_AND_FOUND"},{10,"TAK_TRACKER"}
    };
    for (int i = 0; i < 11; i++) {
        snprintf(tmp, sizeof(tmp), "%d", kRoles[i].v);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->deviceRole == kRoles[i].v) html += " selected";
        html += ">"; html += kRoles[i].l; html += "</option>";
    }
    html += "</select></label>";
    html += "<label>Rebroadcast<select name='rebroadcast'>";
    static const struct { uint8_t v; const char *l; } kRebroad[] = {
        {0,"ALL"},{1,"ALL_SKIP_DECODING"},{2,"LOCAL_ONLY"},{3,"KNOWN_ONLY"},{4,"CORE_PORTNUMS_ONLY"}
    };
    for (int i = 0; i < 5; i++) {
        snprintf(tmp, sizeof(tmp), "%d", kRebroad[i].v);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->rebroadcastMode == kRebroad[i].v) html += " selected";
        html += ">"; html += kRebroad[i].l; html += "</option>";
    }
    html += "</select></label></div>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->nodeInfoIntervalS);
    html += "<label>NodeInfo Interval (s)<input name='nodeinfo_intv' type='number' min='60' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->posIntervalS);
    html += "<label>GPS Broadcast Interval (s)<input name='pos_intv' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";
    // Timezone dropdown
    {
        bool tzMatched = false;
        for (int i = 0; i < kTzCount && !tzMatched; i++)
            if (strcmp(gCfg->tzDef, kTzOptions[i].posix) == 0) tzMatched = true;
        html += "<label>Timezone<select name='tzdef'>";
        if (!tzMatched && gCfg->tzDef[0]) {
            html += "<option value='"; html += gCfg->tzDef;
            html += "' selected>Custom: "; html += gCfg->tzDef; html += "</option>";
        }
        for (int i = 0; i < kTzCount; i++) {
            html += "<option value='"; html += kTzOptions[i].posix; html += "'";
            if (strcmp(gCfg->tzDef, kTzOptions[i].posix) == 0) html += " selected";
            html += ">"; html += kTzOptions[i].label; html += "</option>";
        }
        html += "</select></label>";
    }
    html += "<div class='row2'>";
    html += "<label>NTP Server<input name='ntp_server' type='text' maxlength='47' value='";
    html += gCfg->ntpServer;
    html += "' placeholder='pool.ntp.org'></label>";
    html += "</div>";
    html += "</details>";
    sendChunk(html);

    // ── Position ──────────────────────────────────────────────
    html += "<details open><summary>Position</summary>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='gpsEnabled' value='1'";
    if (gCfg->gpsEnabled) html += " checked";
    html += "> GPS Enabled (L76K hardware GPS)</label>";
    html += "<p class='gps-hint'>When GPS is enabled, position is sourced from the GPS module. "
            "The manual coordinates below are used as fallback until a fix is acquired.</p>";
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->gpsPollIntervalS);
        html += "<label>GPS Poll Interval (s)<input name='gps_poll_s' type='number' min='0' max='3600' step='1' value='";
        html += tmp; html += "'></label>";
        html += "<p class='gps-hint'>Set to 0 to poll every loop. Higher values reduce GPS polling frequency.</p>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%.7f", gCfg->latI * 1e-7);
    html += "<label>Latitude&deg; (fallback)<input name='lat' type='number' step='0.0000001' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%.7f", gCfg->lonI * 1e-7);
    html += "<label>Longitude&deg; (fallback)<input name='lon' type='number' step='0.0000001' value='";
    html += tmp; html += "'></label></div>";
    snprintf(tmp, sizeof(tmp), "%d", (int)gCfg->alt);
    html += "<label>Altitude m (fallback)<input name='alt' type='number' value='";
    html += tmp; html += "' style='max-width:120px'></label>";
    html += "</details>";
    sendChunk(html);

    // ── Channels ──────────────────────────────────────────────
    html += "<details><summary>Channels</summary>";
    html += "<p class='gps-hint'>Key: base64 (e.g. \"AQ==\" or \"MA==\"). "
            "Hash is recomputed automatically on save.</p>";
    char b64buf[48];
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ch = CHANNEL_KEYS[i];
        base64Encode(ch.key, ch.keyLen, b64buf);
        html += "<div class='ch-row'>";
        // Name
        snprintf(tmp, sizeof(tmp), "ch%d_name", i);
        html += "<label>"; snprintf(tmp+20, 20, "%d", i); html += "Ch "; html += (tmp+20);
        snprintf(tmp, sizeof(tmp), "ch%d_name", i);
        html += "<input name='"; html += tmp; html += "' type='text' maxlength='11' value='";
        html += ch.name; html += "'></label>";
        // Key
        snprintf(tmp, sizeof(tmp), "ch%d_key", i);
        html += "<label>Key<input name='"; html += tmp;
        html += "' type='text' value='"; html += b64buf; html += "'></label>";
        // Role
        snprintf(tmp, sizeof(tmp), "ch%d_role", i);
        html += "<label>Role<select name='"; html += tmp; html += "'>";
        const char *roles[] = {"PRIMARY","SECONDARY","DISABLED"};
        for (int r = 0; r < 3; r++) {
            snprintf(tmp, sizeof(tmp), "%d", r);
            html += "<option value='"; html += tmp; html += "'";
            if (ch.role == r) html += " selected";
            html += ">"; html += roles[r]; html += "</option>";
        }
        html += "</select></label>";
        html += "</div>";
    }
    html += "</details>";
    sendChunk(html);

    // ── LoRa Radio ────────────────────────────────────────────
    html += "<details open><summary>LoRa Radio</summary>";
    html += "<div class='row2'>"
            "<label>Region<select name='region' id='sel-rgn'>"
            "<option value='US'>US (902&ndash;928 MHz)</option>"
            "<option value='EU_433'>EU 433 (433&ndash;434 MHz)</option>"
            "<option value='EU_868'>EU 868 (869.4&ndash;869.65 MHz)</option>"
            "<option value='CN'>CN (470&ndash;510 MHz)</option>"
            "<option value='JP'>JP (920.5&ndash;923.5 MHz)</option>"
            "<option value='ANZ'>ANZ (915&ndash;928 MHz)</option>"
            "<option value='ANZ_433'>ANZ 433 (433&ndash;434.8 MHz)</option>"
            "<option value='RU'>RU (868.7&ndash;869.2 MHz)</option>"
            "<option value='KR'>KR (920&ndash;923 MHz)</option>"
            "<option value='TW'>TW (920&ndash;925 MHz)</option>"
            "<option value='IN'>IN (865&ndash;867 MHz)</option>"
            "<option value='NZ_865'>NZ 865 (864&ndash;868 MHz)</option>"
            "<option value='TH'>TH (920&ndash;925 MHz)</option>"
            "<option value='UA_433'>UA 433 (433&ndash;434.7 MHz)</option>"
            "<option value='UA_868'>UA 868 (868&ndash;868.6 MHz)</option>"
            "<option value='MY_433'>MY 433 (433&ndash;435 MHz)</option>"
            "<option value='MY_919'>MY 919 (919&ndash;924 MHz)</option>"
            "<option value='SG_923'>SG 923 (917&ndash;925 MHz)</option>"
            "<option value='PH_433'>PH 433 (433&ndash;434.7 MHz)</option>"
            "<option value='PH_868'>PH 868 (868&ndash;869.4 MHz)</option>"
            "<option value='PH_915'>PH 915 (915&ndash;918 MHz)</option>"
            "<option value='KZ_433'>KZ 433 (433&ndash;434.8 MHz)</option>"
            "<option value='KZ_863'>KZ 863 (863&ndash;868 MHz)</option>"
            "<option value='NP_865'>NP 865 (865&ndash;868 MHz)</option>"
            "<option value='BR_902'>BR 902 (902&ndash;907.5 MHz)</option>"
            "<option value='LORA_24'>LoRa 2.4 GHz (2400&ndash;2483.5 MHz)</option>"
            "</select></label>"
            "<label>Modem Preset<select id='sel-pst'>"
            "<option value='Long Fast'>Long Fast (default)</option>"
            "<option value='Long Moderate'>Long Moderate</option>"
            "<option value='Long Slow'>Long Slow</option>"
            "<option value='Long Turbo'>Long Turbo</option>"
            "<option value='Medium Fast'>Medium Fast</option>"
            "<option value='Medium Slow'>Medium Slow</option>"
            "<option value='Short Fast'>Short Fast</option>"
            "<option value='Short Slow'>Short Slow</option>"
            "<option value='Short Turbo'>Short Turbo</option>"
            "</select></label></div>";
    html += "<script>document.getElementById('sel-rgn').value='";
    html += gCfg->region; html += "';</script>";
    html += "<button type='button' onclick='applyPreset()'"
            " style='margin-top:.5em;background:#555'>Apply Preset to fields below</button>"
            "<p class='gps-hint'>Fills frequency, BW, SF, CR and TX power from preset.</p>"
            "<script>"
            "var R={'US':{f:906.875,p:22},'EU_433':{f:433.5,p:10},'EU_868':{f:869.525,p:22},"
            "'CN':{f:490.0,p:19},'JP':{f:922.0,p:13},'ANZ':{f:921.5,p:22},'ANZ_433':{f:433.92,p:14},"
            "'RU':{f:868.95,p:20},'KR':{f:921.5,p:22},'TW':{f:922.5,p:22},'IN':{f:866.0,p:22},"
            "'NZ_865':{f:866.0,p:22},'TH':{f:922.5,p:16},'UA_433':{f:433.85,p:10},'UA_868':{f:868.3,p:14},"
            "'MY_433':{f:434.0,p:20},'MY_919':{f:921.5,p:22},'SG_923':{f:921.0,p:20},'PH_433':{f:433.85,p:10},"
            "'PH_868':{f:868.7,p:14},'PH_915':{f:916.5,p:22},'KZ_433':{f:433.925,p:10},'KZ_863':{f:865.5,p:22},"
            "'NP_865':{f:866.5,p:22},'BR_902':{f:904.75,p:22},'LORA_24':{f:2441.75,p:10}};"
            "var P={'Long Fast':{bw:250,sf:11,cr:5},'Long Moderate':{bw:125,sf:11,cr:8},"
            "'Long Slow':{bw:125,sf:12,cr:8},'Long Turbo':{bw:500,sf:11,cr:8},"
            "'Medium Fast':{bw:250,sf:9,cr:5},'Medium Slow':{bw:250,sf:10,cr:5},"
            "'Short Fast':{bw:250,sf:7,cr:5},'Short Slow':{bw:250,sf:8,cr:5},'Short Turbo':{bw:500,sf:7,cr:5}};"
            "function applyPreset(){"
              "var r=R[document.getElementById('sel-rgn').value];"
              "var p=P[document.getElementById('sel-pst').value];"
              "if(!r||!p)return;"
              "document.querySelector('[name=freq]').value=r.f.toFixed(3);"
              "document.querySelector('[name=bw]').value=p.bw;"
              "document.querySelector('[name=sf]').value=p.sf;"
              "document.querySelector('[name=cr]').value=p.cr;"
              "document.querySelector('[name=pwr]').value=r.p;"
            "}"
            "</script>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%.3f", gCfg->loraFreq);
    html += "<label>Frequency (MHz)<input name='freq' type='number' step='0.001' min='150' max='2500' value='";
    html += tmp; html += "'></label>";
    html += "<label>Bandwidth (kHz)<select name='bw'>";
    const float bwOpts[] = {125.0f,250.0f,500.0f};
    const char *bwLabels[] = {"125 kHz","250 kHz","500 kHz"};
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "%.0f", bwOpts[i]);
        html += "<option value='"; html += tmp; html += "'";
        if (fabsf(gCfg->loraBw - bwOpts[i]) < 0.1f) html += " selected";
        html += ">"; html += bwLabels[i]; html += "</option>";
    }
    html += "</select></label></div><div class='row2'>";
    html += "<label>Spreading Factor<select name='sf'>";
    for (int sf = 7; sf <= 12; sf++) {
        snprintf(tmp, sizeof(tmp), "%d", sf);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->loraSf == sf) html += " selected";
        html += ">SF"; html += tmp; html += "</option>";
    }
    html += "</select></label>";
    html += "<label>Coding Rate<select name='cr'>";
    for (int cr = 5; cr <= 8; cr++) {
        snprintf(tmp, sizeof(tmp), "%d", cr);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->loraCr == cr) html += " selected";
        html += ">4/"; html += tmp; html += "</option>";
    }
    html += "</select></label></div><div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraPower);
    html += "<label>TX Power (dBm, 1&ndash;22)<input name='pwr' type='number' min='1' max='22' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%d", gCfg->loraHopLimit);
    html += "<label>Hop Limit (1&ndash;7)<input name='hop' type='number' min='1' max='7' value='";
    html += tmp; html += "'></label></div>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='ok_to_mqtt' value='1'";
    if (gCfg->okToMqtt) html += " checked";
    html += "> OK to MQTT &mdash; allow MQTT-connected nodes to forward your packets upstream</label>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='ignore_mqtt' value='1'";
    if (gCfg->ignoreMqtt) html += " checked";
    html += "> Ignore MQTT &mdash; drop received packets that arrived via MQTT</label>";
    html += "</details>";
    sendChunk(html);

    // MQTT controls are intentionally omitted from web config.

    // ── Display ───────────────────────────────────────────────
    html += "<details><summary>Display</summary>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->screenOnSecs);
    html += "<label>Screen Timeout (s)<input name='screen_on' type='number' min='0' value='";
    html += tmp; html += "'></label>";
        html += "<label>Units (Display &amp; Telemetry)<select name='disp_units'>"
            "<option value='0'"; if (!gCfg->displayUnits) html += " selected"; html += ">Metric (C / hPa)</option>"
            "<option value='1'"; if ( gCfg->displayUnits) html += " selected"; html += ">Imperial (F / inHg)</option>"
            "</select></label></div>";
    html += "<div class='row2'>";
    html += "<label>Compass North Top<select name='compass_north'>"
            "<option value='1'"; if ( gCfg->compassNorthTop) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->compassNorthTop) html += " selected"; html += ">No</option>"
            "</select></label>";
    html += "<label>Flip Screen<select name='flip_screen'>"
            "<option value='1'"; if ( gCfg->flipScreen) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->flipScreen) html += " selected"; html += ">No</option>"
            "</select></label></div>";
        html += "<label>Splash Melody<select name='splash_melody'>"
            "<option value='1'"; if ( gCfg->splashMelodyEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->splashMelodyEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
        html += "<label>Theme<select name='ui_theme_preset' id='sel-theme-preset'>"
            "<option value='0'"; if (themePreset == 0) html += " selected"; html += ">Camillia Dark</option>"
            "<option value='1'"; if (themePreset == 1) html += " selected"; html += ">Camillia Light</option>"
            "<option value='2'"; if (themePreset == 2) html += " selected"; html += ">Evergreen Dark</option>"
            "<option value='3'"; if (themePreset == 3) html += " selected"; html += ">Evergreen Light</option>"
            "<option value='4'"; if (themePreset == 4) html += " selected"; html += ">Earthy Dark</option>"
            "<option value='5'"; if (themePreset == 5) html += " selected"; html += ">Earthy Light</option>"
            "<option value='6'"; if (themePreset == 6) html += " selected"; html += ">Solarized Dark</option>"
            "<option value='7'"; if (themePreset == 7) html += " selected"; html += ">Solarized Light</option>"
            "<option value='8'"; if (themePreset == 8) html += " selected"; html += ">Crimson Blue Dark</option>"
            "<option value='9'"; if (themePreset == 9) html += " selected"; html += ">Crimson Blue Light</option>"
            "<option value='10'"; if (themePreset == 10) html += " selected"; html += ">Scarlet Pop Dark</option>"
            "<option value='11'"; if (themePreset == 11) html += " selected"; html += ">Scarlet Pop Light</option>"
            "<option value='12'"; if (themePreset == 12) html += " selected"; html += ">Ink Wash Dark</option>"
            "<option value='13'"; if (themePreset == 13) html += " selected"; html += ">Ink Wash Light</option>"
            "<option value='14'"; if (themePreset == 14) html += " selected"; html += ">Lavendar Fields Dark</option>"
            "<option value='15'"; if (themePreset == 15) html += " selected"; html += ">Lavendar Fields Light</option>"
            "<option value='16'"; if (themePreset == 16) html += " selected"; html += ">Wild Flowers Dark</option>"
            "<option value='17'"; if (themePreset == 17) html += " selected"; html += ">Wild Flowers Light</option>"
            "<option value='18'"; if (themePreset == 18) html += " selected"; html += ">Quiet Luxury Dark</option>"
            "<option value='19'"; if (themePreset == 19) html += " selected"; html += ">Quiet Luxury Light</option>"
            "<option value='20'"; if (themePreset == 20) html += " selected"; html += ">Morning Dew Dark</option>"
            "<option value='21'"; if (themePreset == 21) html += " selected"; html += ">Morning Dew Light</option>"
            "<option value='22'"; if (themePreset == 22) html += " selected"; html += ">Winter Chill Dark</option>"
            "<option value='23'"; if (themePreset == 23) html += " selected"; html += ">Winter Chill Light</option>"
            "</select></label>";
        html += "<script>"
            "(function(){"
            "var P={"
                            "'0':{bg:'#10141d',panel:'#1a2230',panel2:'#232d3e',line:'#4a5b73',text:'#f4f6fb',dim:'#b0b8c8',accent:'#d7869d',ink:'#ffffff'},"
                            "'1':{bg:'#f6ede9',panel:'#fff6f3',panel2:'#f4e2dc',line:'#cfb2ab',text:'#2e2220',dim:'#6f5c58',accent:'#b75a74',ink:'#ffffff'},"
                            "'2':{bg:'#091713',panel:'#102722',panel2:'#18332d',line:'#3a5f55',text:'#e8f4ef',dim:'#a5beb4',accent:'#5dbf9a',ink:'#073022'},"
                            "'3':{bg:'#eaf4ee',panel:'#f7fcf9',panel2:'#deece4',line:'#b5ccbf',text:'#1f2e25',dim:'#5f7668',accent:'#2f8f63',ink:'#ffffff'},"
                            "'4':{bg:'#1f1712',panel:'#2a2019',panel2:'#352920',line:'#655345',text:'#f3e9df',dim:'#c4b2a2',accent:'#c38a4a',ink:'#ffffff'},"
                            "'5':{bg:'#f3e9dd',panel:'#fbf4ea',panel2:'#efdfcc',line:'#c9b39a',text:'#3b2d23',dim:'#7f6a57',accent:'#a9763f',ink:'#ffffff'},"
                            "'6':{bg:'#002b36',panel:'#073642',panel2:'#0b4552',line:'#586e75',text:'#eee8d5',dim:'#93a1a1',accent:'#2aa198',ink:'#002b36'},"
                            "'7':{bg:'#fdf6e3',panel:'#eee8d5',panel2:'#e7e2cf',line:'#93a1a1',text:'#002b36',dim:'#586e75',accent:'#268bd2',ink:'#fdf6e3'},"
                            "'8':{bg:'#060f24',panel:'#12244c',panel2:'#1b3363',line:'#3b578f',text:'#f4f8ff',dim:'#b9c9e9',accent:'#ff4a58',ink:'#ffffff'},"
                            "'9':{bg:'#f3f7ff',panel:'#f8fbff',panel2:'#e6efff',line:'#a5bbe7',text:'#1f2d4d',dim:'#5d6e95',accent:'#c62839',ink:'#ffffff'},"
                            "'10':{bg:'#150009',panel:'#760031',panel2:'#8b0038',line:'#6f2d3b',text:'#fff1f1',dim:'#e5b3ba',accent:'#d51c39',ink:'#ffffff'},"
                            "'11':{bg:'#fff2f4',panel:'#fff8f9',panel2:'#ffeaed',line:'#d8a1aa',text:'#3a0a14',dim:'#7e3b49',accent:'#d51c39',ink:'#ffffff'},"
                            "'12':{bg:'#111318',panel:'#1c2128',panel2:'#252b34',line:'#4a525d',text:'#f3f6fa',dim:'#b7c0cc',accent:'#d8dde4',ink:'#080d14'},"
                            "'13':{bg:'#f3f5f7',panel:'#ffffff',panel2:'#e8ebef',line:'#c2c9d0',text:'#1e242c',dim:'#5e6876',accent:'#2e3440',ink:'#ffffff'},"
                            "'14':{bg:'#1a1230',panel:'#251a45',panel2:'#2f2258',line:'#58457f',text:'#f2eefe',dim:'#c9c0e6',accent:'#b79bff',ink:'#160f2a'},"
                            "'15':{bg:'#f5effb',panel:'#fff9ff',panel2:'#ede1f7',line:'#c7b5db',text:'#2f2440',dim:'#6d5f82',accent:'#7b5ba7',ink:'#ffffff'},"
                            "'16':{bg:'#1a2430',panel:'#253547',panel2:'#2d455b',line:'#4b6881',text:'#edf4fb',dim:'#bccbd9',accent:'#c78fcf',ink:'#25192d'},"
                            "'17':{bg:'#f6faf4',panel:'#ffffff',panel2:'#e5f0e2',line:'#bccfb7',text:'#24332f',dim:'#63756f',accent:'#8a5faf',ink:'#ffffff'},"
                            "'18':{bg:'#2a1f17',panel:'#34271e',panel2:'#403126',line:'#6a5848',text:'#f6eee6',dim:'#c9b9a8',accent:'#d9c7a3',ink:'#2a1f16'},"
                            "'19':{bg:'#faf4ea',panel:'#fffdf8',panel2:'#f1e7d5',line:'#cdbfa8',text:'#3b2f24',dim:'#7b6b57',accent:'#a8844f',ink:'#ffffff'},"
                            "'20':{bg:'#12282a',panel:'#1a3638',panel2:'#234345',line:'#4d7072',text:'#ebf7f5',dim:'#b3d0cc',accent:'#9cd8c8',ink:'#123130'},"
                            "'21':{bg:'#eef9f6',panel:'#ffffff',panel2:'#ddf1ec',line:'#b5d5cd',text:'#213531',dim:'#5f7c76',accent:'#4e9c8a',ink:'#ffffff'},"
                            "'22':{bg:'#151f2b',panel:'#1c2a3a',panel2:'#243649',line:'#4c637c',text:'#ecf3fa',dim:'#b5c5d6',accent:'#8fb3d9',ink:'#132030'},"
                            "'23':{bg:'#f1f7fc',panel:'#ffffff',panel2:'#dfebf6',line:'#b6c9dd',text:'#22354a',dim:'#607891',accent:'#5c86b2',ink:'#ffffff'}"
            "};"
            "function apply(){"
              "var k=document.getElementById('sel-theme-preset').value;"
              "var p=P[k]||P['0'];"
              "var r=document.documentElement.style;"
              "r.setProperty('--bg',p.bg);r.setProperty('--panel',p.panel);r.setProperty('--panel-2',p.panel2);"
              "r.setProperty('--line',p.line);r.setProperty('--text',p.text);r.setProperty('--text-dim',p.dim);"
              "r.setProperty('--accent',p.accent);r.setProperty('--accent-ink',p.ink);"
            "}"
            "document.getElementById('sel-theme-preset').addEventListener('change',apply);"
            "apply();"
            "})();"
            "</script>";
    html += "</details>";
    sendChunk(html);

    // ── Power ─────────────────────────────────────────────────
    html += "<details><summary>Power</summary>";
    html += "<label>Power Saving<select name='pwr_saving'>"
            "<option value='1'"; if ( gCfg->isPowerSaving) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->isPowerSaving) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->lsSecs);
    html += "<label>Light Sleep After (s)<input name='ls_secs' type='number' min='0' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->minWakeSecs);
    html += "<label>Min Wake (s)<input name='min_wake' type='number' min='0' value='";
    html += tmp; html += "'></label></div>";
    html += "</details>";
    sendChunk(html);

    // ── Modules ───────────────────────────────────────────────
    html += "<details><summary>Modules</summary>";
    // Telemetry
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Telemetry</h3>";
    html += "<div class='row2'>";
    html += "<label>Device Telemetry<select name='tel_dev_en'>"
            "<option value='1'"; if ( gCfg->telDeviceEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->telDeviceEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    uint32_t telemetryIntervalMins = (gCfg->telDeviceIntervalS >= 60UL)
        ? (uint32_t)(gCfg->telDeviceIntervalS / 60UL)
        : 60UL;
    if (telemetryIntervalMins < 60UL) telemetryIntervalMins = 60UL;
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)telemetryIntervalMins);
    html += "<label>Broadcast Interval (min)<input name='tel_bcast_mins' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";

    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Sensor Telemetry</h3>";
    html += "<div class='row2'>";
#if HAS_ENV_SENSOR_TELEMETRY
    html += "<label>Environment Sensor Telemetry<select name='tel_env_en'>"
            "<option value='1'"; if ( gCfg->telEnvEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->telEnvEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<label>Sensor Interval<input type='text' value='Uses broadcast interval above' disabled></label>";
#else
    html += "<label>Environment Sensor Telemetry<input type='text' value='Unavailable on this build' disabled></label>";
    html += "<label>Sensor Interval<input type='text' value='Unavailable on this build' disabled></label>";
    html += "<input type='hidden' name='tel_env_en' value='0'>";
#endif
    html += "</div>";
    // Canned Messages
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Canned Messages</h3>";
    html += "<label>Enabled<select name='canned_en'>"
            "<option value='1'"; if ( gCfg->cannedEnabled) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->cannedEnabled) html += " selected"; html += ">No</option>"
            "</select></label>";
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Alerts</h3>";
    html += "<label>Notification Sound<select name='msg_alert_sound'>"
            "<option value='0'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_DEFAULT) html += " selected"; html += ">Default</option>"
            "<option value='1'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_CHIRPY) html += " selected"; html += ">Chirpy</option>"
            "<option value='2'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_BASS) html += " selected"; html += ">Bass</option>"
            "<option value='3'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_OFF) html += " selected"; html += ">Off</option>"
            "</select></label>";
#endif
    html += "</details>";
    sendChunk(html);

    // WiFi
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>WiFi</h3>"
            "<label>SSID<input name='wifi_ssid' type='text' maxlength='63' value='";
    html += gWifiSsid;
    html += "'></label>"
            "<label>Password<input name='wifi_pass' type='password' maxlength='63' value='";
    html += gWifiPass;
    html += "'></label>";

    html += "<button type='submit' style='width:100%;margin-top:1.5em'>Save All</button></form>";
    sendChunk(html);

    html += "</div><div class='tab-panel' id='tab-utils'>";

    // ── Diagnostics / Utilities ───────────────────────────────
    html +=
        "<h3 style='margin-top:1.5em'>Diagnostics</h3>"
        "<form method='POST' action='/announce'>"
        "<button type='submit' style='background:#e07b00'>"
        "&#128225; Send NODEINFO Broadcast</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Forces immediate re-announcement to the mesh (NODEINFO + position).</p>";

    html +=
        "<h3 style='margin-top:.8em'>Debug Output</h3>"
        "<form method='POST' action='/set-debug-monitor'>"
        "<label style='display:flex;align-items:center;gap:.5em'>"
        "<input type='checkbox' name='dbg_monitor' value='1'";
    if (monitorDebugEnabled()) html += " checked";
    html +=
        "> Enable serial debug output in pio device monitor</label>"
        "<button type='submit' style='margin-top:.45em;background:#355f9b'>Apply Debug Flag</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "When disabled, debug logs are suppressed from serial monitor output.</p>";

    html +=
        "<h3 style='margin-top:.8em'>Software Update</h3>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .45em'>Current firmware: <b>";
    html += APP_VERSION;
    html +=
        "</b></p>"
        "<button type='button' id='check-release-btn'"
        " style='background:#1f7a8c;margin-top:.1em'"
        " onclick='checkLatestRelease()'>Check for New Release</button>"
        "<p id='release-check-result' style='font-size:.82em;color:#888;margin:.45em 0 1em'></p>";

    html +=
        "<h3 style='margin-top:.5em'>Backup &amp; Restore</h3>"
        "<p><a href='/export'"
        " style='display:inline-block;padding:.4em 1.2em;background:#2a9d8f;"
        "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
        "&#11015; Export Config</a></p>"
        "<form method='POST' action='/import' enctype='multipart/form-data'"
        " style='margin-top:.6em'>"
        "<label>Import a YAML config file.</label>  "
        "<input type='file' name='f' accept='.yaml,.yml'"
        " style='margin-top:.3em'><br />"
        "<button type='submit'>&#11014; Upload &amp; Apply</button>"
        "</form>";

    if (gOnScreenshotPng) {
        html +=
            "<h3 style='margin-top:1.1em'>Display Capture</h3>"
            "<p><a href='/screenshot'"
            " style='display:inline-block;padding:.4em 1.2em;background:#3b82f6;"
            "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
            "&#128247; Capture &amp; Download PNG</a></p>"
            "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
            "Captures the current on-device screen and downloads it as a PNG file.</p>";
    }

    html +=
        "<h3 style='margin-top:1.5em;color:#c0392b'>Danger Zone</h3>"
        "<form method='POST' action='/clear-messages'"
        " onsubmit=\"return confirm('This will clear all stored channel and DM messages. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Clear Messages</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
        "Clears saved and in-memory chat/DM history without resetting device configuration.</p>"
        "<form method='POST' action='/clear-nodes'"
        " onsubmit=\"return confirm('This will clear all discovered nodes and reboot. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Clear Nodes</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
        "Clears the persisted node database and reboots.</p>"
        "<form method='POST' action='/factory-reset'"
        " onsubmit=\"return confirm('This will erase ALL settings and reboot the device. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Factory Reset</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Erases all NVS configuration (node identity, channels, keys) and reboots."
        " The device will behave as if freshly flashed.</p>";

    html += "</div><div class='tab-panel' id='tab-live'>";
    html += "<h3 style='margin-top:1.2em'>Live RX/TX</h3>"
            "<p class='gps-hint'>Streams the Announcements feed from the device, similar to the on-device ANN view.</p>"
            "<div class='live-wrap'>"
            "<div class='live-toolbar'>"
            "<button type='button' class='map-mini-btn' onclick='clearLiveFeed()'>Clear View</button>"
            "<span id='live-status'>Paused</span>"
            "</div>"
            "<div id='live-feed' class='live-feed'></div>"
            "</div>";

    html += "</div><div class='tab-panel' id='tab-map'>";
    html += "<h3 style='margin-top:1.2em'>Node Heatmap</h3>";
    html += "<p class='gps-hint'>Positioned nodes: ";
    snprintf(tmp, sizeof(tmp), "%d", mapPointCount);
    html += tmp;
    html += " / ";
    snprintf(tmp, sizeof(tmp), "%d", totalNodes);
    html += tmp;
    html += ". Heat intensity increases where multiple nodes overlap.</p>";
    html += "<div class='map-controls'>"
            "<button type='button' id='map-fit-btn' class='map-mini-btn' onclick='fitNodeMap()'>Fit Nodes</button>"
            "<button type='button' class='map-mini-btn' onclick='worldNodeMap()'>World View</button>"
            "<button type='button' id='map-heat-btn' class='map-mini-btn active' onclick='toggleNodeHeat()'>Heat: On</button>";
        html += "<button type='button' id='map-me-btn' class='map-mini-btn map-mini-right' onclick='centerOnMeMap()'";
        if (!mapHasMe) html += " disabled";
        html += ">ME</button></div>";
    html += "<div class='map-wrap'>"
            "<div id='node-heatmap' class='map-canvas'></div>"
            "<div class='map-legend'><span>Drag to pan, scroll/pinch to zoom</span><span id='map-status'></span></div>"
            "</div>";
    html += "<h3 style='margin-top:1em'>Nodes Seen</h3>";
    html += "<p class='gps-hint'>Device and location details for discovered nodes.</p>";
    html += "<div class='node-list'>";
    if (nodeCards.length() > 0) html += nodeCards;
    else html += "<div class='node-card'><div class='node-meta'>No nodes discovered yet.</div></div>";
    html += "</div>";
    html += "<script>var NODE_HEAT_POINTS=";
    html += mapPoints;
    html += ";var NODE_ME_POINT=";
    if (mapHasMe) {
        snprintf(tmp, sizeof(tmp), "{lat:%.7f,lon:%.7f}", mapMeLat, mapMeLon);
        html += tmp;
    } else {
        html += "null";
    }
    html += ";</script>";

    html += "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>"
            "<script src='https://unpkg.com/leaflet.heat@0.2.0/dist/leaflet-heat.js'></script>";

        html += "</div>"
                        "<script>"
                        "var nodeMap=null;"
                        "var nodeMarkerLayer=null;"
                        "var nodeHeatLayer=null;"
                        "var nodeHeatOn=true;"
                        "var livePollTimer=null;"
                        "var liveCursor=-1;"
                        "function setMapStatus(msg){"
                            "var el=document.getElementById('map-status');"
                            "if(el)el.textContent=msg||'';"
                        "}"
                        "function setLiveStatus(msg){"
                            "var el=document.getElementById('live-status');"
                            "if(el)el.textContent=msg||'';"
                        "}"
                        "function clearLiveFeed(){"
                            "var box=document.getElementById('live-feed');"
                            "if(box)box.innerHTML='';"
                        "}"
                        "function setReleaseCheckResult(msg,col){"
                            "var el=document.getElementById('release-check-result');"
                            "if(!el)return null;"
                            "el.style.color=col||'#888';"
                            "el.textContent=msg||'';"
                            "return el;"
                        "}"
                        "function parseVersionNums(v){"
                            "var m=String(v||'').match(/\\d+/g);"
                            "if(!m)return [];"
                            "for(var i=0;i<m.length;i++)m[i]=parseInt(m[i],10)||0;"
                            "return m;"
                        "}"
                        "function compareVersionNums(a,b){"
                            "var va=parseVersionNums(a),vb=parseVersionNums(b);"
                            "var n=va.length>vb.length?va.length:vb.length;"
                            "if(n<1)n=1;"
                            "for(var i=0;i<n;i++){"
                                "var xa=i<va.length?va[i]:0;"
                                "var xb=i<vb.length?vb[i]:0;"
                                "if(xa<xb)return -1;"
                                "if(xa>xb)return 1;"
                            "}"
                            "return 0;"
                        "}"
                        "function pollDeviceReleaseCheck(deadline,done){"
                            "var poll=function(start){"
                                "var path='/release-check'+(start?'?start=1':'');"
                                "var req=fetch(path,{cache:'no-store'})"
                                    ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.json();});"
                                "var timeout=new Promise(function(_,reject){setTimeout(function(){reject(new Error('timeout'));},15000);});"
                                "Promise.race([req,timeout])"
                                    ".then(function(d){"
                                        "var cur=(d&&d.current)?d.current:'unknown';"
                                        "var st=(d&&d.status)?d.status:'';"
                                        "if(st==='pending'||st==='running'||st==='idle'){"
                                            "setReleaseCheckResult('Checking latest release...','#b0b8c8');"
                                            "if(Date.now()>=deadline){setReleaseCheckResult('Update check failed: request timed out','#ff9f9f');done();return;}"
                                            "setTimeout(function(){poll(false);},900);"
                                            "return;"
                                        "}"
                                        "if(d&&d.error){setReleaseCheckResult('Update check failed: '+d.error,'#ff9f9f');done();return;}"
                                        "var latest=(d&&d.latest)?d.latest:'';"
                                        "if(d&&d.updateAvailable){"
                                            "var el=setReleaseCheckResult('New release available: '+latest+' (current '+cur+').','#8ef2b8');"
                                            "if(el&&d.url){"
                                                "var a=document.createElement('a');"
                                                "a.href=d.url;"
                                                "a.target='_blank';"
                                                "a.rel='noopener';"
                                                "a.style.marginLeft='0.45em';"
                                                "a.textContent='View release';"
                                                "el.appendChild(a);"
                                            "}"
                                        "}else if(latest){"
                                            "setReleaseCheckResult('You are up to date ('+cur+'). Latest: '+latest+'.','#8ef2b8');"
                                        "}else{"
                                            "setReleaseCheckResult('No release information returned.','#ffd181');"
                                        "}"
                                        "done();"
                                    "})"
                                    ".catch(function(err){"
                                        "var msg=(err&&err.message)?err.message:'network error';"
                                        "if(msg==='timeout')msg='request timed out';"
                                        "setReleaseCheckResult('Update check failed: '+msg,'#ff9f9f');"
                                        "done();"
                                    "});"
                            "};"
                            "poll(true);"
                        "}"
                        "function checkLatestRelease(){"
                            "var btn=document.getElementById('check-release-btn');"
                            "if(btn)btn.disabled=true;"
                            "var done=function(){if(btn)btn.disabled=false;};"
                            "var deadline=Date.now()+35000;"
                            "setReleaseCheckResult('Checking latest release...','#b0b8c8');"
                            "var cur='" APP_VERSION "';"
                            "var releaseUrl='https://github.com/oumike/camillia-mt/releases/latest';"
                            "var apiReq=fetch('https://api.github.com/repos/oumike/camillia-mt/releases/latest',{cache:'no-store',headers:{'Accept':'application/vnd.github+json'}})"
                                ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.json();})"
                                ".then(function(d){"
                                    "var latest=(d&&d.tag_name)?String(d.tag_name).trim():'';"
                                    "var url=(d&&d.html_url)?d.html_url:releaseUrl;"
                                    "if(!latest)throw new Error('missing tag');"
                                    "return {latest:latest,url:url};"
                                "})"
                                ".catch(function(){"
                                    "return fetch('https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION',{cache:'no-store'})"
                                        ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.text();})"
                                        ".then(function(t){"
                                            "var latest=String(t||'').trim();"
                                            "if(!latest)throw new Error('missing tag');"
                                            "return {latest:latest,url:releaseUrl};"
                                        "});"
                                "});"
                            "var timeout=new Promise(function(_,reject){setTimeout(function(){reject(new Error('timeout'));},15000);});"
                            "Promise.race([apiReq,timeout])"
                                ".then(function(info){"
                                    "var latest=(info&&info.latest)?info.latest:'';"
                                    "if(!latest){setReleaseCheckResult('No release information returned.','#ffd181');done();return;}"
                                    "if(compareVersionNums(cur,latest)<0){"
                                        "var el=setReleaseCheckResult('New release available: '+latest+' (current '+cur+').','#8ef2b8');"
                                        "if(el&&info.url){"
                                            "var a=document.createElement('a');"
                                            "a.href=info.url;"
                                            "a.target='_blank';"
                                            "a.rel='noopener';"
                                            "a.style.marginLeft='0.45em';"
                                            "a.textContent='View release';"
                                            "el.appendChild(a);"
                                        "}"
                                        "done();"
                                        "return;"
                                    "}"
                                    "setReleaseCheckResult('You are up to date ('+cur+'). Latest: '+latest+'.','#8ef2b8');"
                                    "done();"
                                "})"
                                ".catch(function(){"
                                    "setReleaseCheckResult('Checking latest release on device...','#b0b8c8');"
                                    "pollDeviceReleaseCheck(deadline,done);"
                                "});"
                        "}"
                        "function liveSplitTimestamp(t){"
                            "var s=String(t||'').trim();"
                            "var m=s.match(/^(\\d\\d:\\d\\d|--:--)\\s+(.*)$/);"
                            "if(m)return {ts:m[1],body:String(m[2]||'').trim()};"
                            "return {ts:'',body:s};"
                        "}"
                        "function livePrefixTs(ts,msg){return ts?(ts+' '+msg):msg;}"
                        "function livePortLabel(tag){"
                            "if(!tag)return 'data';"
                            "if(tag==='T')return 'text';"
                            "if(tag==='N')return 'nodeinfo';"
                            "if(tag==='P')return 'position';"
                            "if(tag==='E')return 'telemetry';"
                            "if(tag==='A')return 'routing';"
                            "return 'data';"
                        "}"
                        "function liveDestLabel(dst){"
                            "if(!dst)return 'node';"
                            "if(dst==='B'||dst==='BCAST')return 'broadcast';"
                            "if(dst==='M')return 'me';"
                            "if(dst==='U')return 'node';"
                            "return 'node';"
                        "}"
                        "function liveRoutingErrorName(err){"
                            "if(err===0)return 'NONE';"
                            "if(err===1)return 'NO_ROUTE';"
                            "if(err===2)return 'GOT_NAK';"
                            "if(err===3)return 'TIMEOUT';"
                            "if(err===4)return 'NO_INTERFACE';"
                            "if(err===5)return 'MAX_RETRANSMIT';"
                            "if(err===6)return 'NO_CHANNEL';"
                            "if(err===7)return 'TOO_LARGE';"
                            "if(err===8)return 'NO_RESPONSE';"
                            "if(err===9)return 'DUTY_CYCLE_LIMIT';"
                            "if(err===35)return 'PKI_UNKNOWN_PUBKEY';"
                            "return '';"
                        "}"
                        "function liveHexReq(req){"
                            "var n=parseInt(req,16);"
                            "if(isNaN(n))return String(req||'').toUpperCase();"
                            "var h=(n>>>0).toString(16).toUpperCase();"
                            "while(h.length<8)h='0'+h;"
                            "if(h.length>8)h=h.slice(-8);"
                            "return h;"
                        "}"
                        "function liveFormatFirmwareLine(t){"
                            "var p=liveSplitTimestamp(t);"
                            "var body=p.body;"
                            "var ts=p.ts;"
                            "if(!body)return '';"
                            "var m=body.match(/^R\\s+([^>\\s]+)>([^\\s]+)\\s+([^\\s]+)\\s+c(\\d+)\\s+h([^\\s]{1,3})$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX '+livePortLabel(m[3])+' from '+m[1]+' to '+liveDestLabel(m[2])+' on ch'+m[4]+' hash '+m[5]);"
                            "}"
                            "m=body.match(/^R\\s+([^>\\s]+)>([^\\s]+)\\s+([^\\s]+)\\s+c(\\d+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX '+livePortLabel(m[3])+' from '+m[1]+' to '+liveDestLabel(m[2])+' on ch'+m[4]);"
                            "}"
                            "m=body.match(/^R\\s+ACK\\s+([^\\s]+)\\s+([0-9A-Fa-f]{1,8})\\s+h([^\\s]{1,3})$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX routing ACK from '+m[1]+' req:'+liveHexReq(m[2])+' hash:'+m[3]);"
                            "}"
                            "m=body.match(/^R\\s+NAK\\s+([^\\s]+)\\s+([0-9A-Fa-f]{1,8})\\s+err(\\d+)\\s+h([^\\s]{1,3})$/);"
                            "if(m){"
                                "var err=parseInt(m[3],10);"
                                "var errName=liveRoutingErrorName(isNaN(err)?-1:err);"
                                "if(errName)return livePrefixTs(ts,'RX routing NAK from '+m[1]+' req:'+liveHexReq(m[2])+' '+errName+'('+m[3]+') hash:'+m[4]);"
                                "return livePrefixTs(ts,'RX routing NAK from '+m[1]+' req:'+liveHexReq(m[2])+' err:'+m[3]+' hash:'+m[4]);"
                            "}"
                            "m=body.match(/^R\\s+([^\\s]+)\\s+ENC\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'RX encrypted packet from '+m[1]+' (hash '+m[2]+')');"
                            "}"
                            "m=body.match(/^T\\s+ACK\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX routing ACK to '+m[1]+' ('+m[2]+')');"
                            "}"
                            "m=body.match(/^T\\s+TXT\\s+([^\\s]+)\\s+c(\\d+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX text to '+liveDestLabel(m[1])+' on ch'+m[2]+' id:'+m[3]);"
                            "}"
                            "m=body.match(/^T\\s+TXT\\s+([^\\s]+)\\s+ER$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX text to '+liveDestLabel(m[1])+' FAILED');"
                            "}"
                            "m=body.match(/^T\\s+POS\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX position to '+liveDestLabel(m[1])+' id:'+m[2]+' ('+m[3]+')');"
                            "}"
                            "m=body.match(/^T\\s+NOD\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "var mode=(m[1]==='U')?'unicast':'broadcast';"
                                "return livePrefixTs(ts,'TX nodeinfo '+mode+' to '+m[2]+' ('+m[3]+')');"
                            "}"
                            "m=body.match(/^T\\s+DM\\s+([^\\s]+)\\s+([^\\s]+)\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX DM to '+m[2]+' via '+m[1]+' id:'+m[3]);"
                            "}"
                            "m=body.match(/^T\\s+DM\\s+ER\\s+([^\\s]+)$/);"
                            "if(m){"
                                "return livePrefixTs(ts,'TX DM FAILED ('+m[1]+')');"
                            "}"
                            "return String(t||'');"
                        "}"
                        "function liveTrafficClass(t){"
                            "var s=liveSplitTimestamp(t).body;"
                            "if(!s)return 'live-default';"
                            "if(s.indexOf(' ER')>=0||s.indexOf('R NAK')===0)return 'live-err';"
                            "if(s.indexOf('T ACK')===0)return 'live-tx-ack';"
                            "if(s.indexOf('R ACK')===0)return 'live-rx-ack';"
                            "if(s.indexOf('T DM')===0)return 'live-tx-dm';"
                            "if(s.indexOf('R ')===0&&s.indexOf(' ENC ')>=0)return 'live-rx-enc';"
                            "var m=s.match(/^R\\s+([^>\\s]+)>([^\\s]+)\\s+([A-Z])\\s+c(\\d+)(?:\\s+([^\\s]+))?$/);"
                            "if(m){"
                                "if(m[3]==='T')return 'live-rx-text';"
                                "if(m[3]==='N')return 'live-rx-node';"
                                "if(m[3]==='P')return 'live-rx-pos';"
                                "if(m[3]==='E')return 'live-rx-tlm';"
                                "return 'live-rx-other';"
                            "}"
                            "if(s.indexOf('T TXT')===0)return 'live-tx-text';"
                            "if(s.indexOf('T NOD')===0)return 'live-tx-node';"
                            "if(s.indexOf('T POS')===0)return 'live-tx-pos';"
                            "if(s.indexOf('T TLM')===0)return 'live-tx-tlm';"
                            "if(s.indexOf('R ')===0)return 'live-rx-other';"
                            "if(s.indexOf('T ')===0)return 'live-tx-other';"
                            "return 'live-default';"
                        "}"
                        "function appendLiveLines(lines){"
                            "var box=document.getElementById('live-feed');"
                            "if(!box||!lines||!lines.length)return;"
                            "var autoScroll=(box.scrollTop+box.clientHeight+18)>=box.scrollHeight;"
                            "for(var i=0;i<lines.length;i++){"
                                "var raw=(lines[i]&&lines[i].t)?lines[i].t:'';"
                                "var txt=liveFormatFirmwareLine(raw);"
                                "var cls=liveTrafficClass(raw);"
                                "var row=document.createElement('div');"
                                "row.className='live-line '+cls;"
                                "row.textContent=txt;"
                                "if(raw&&raw!==txt)row.title=raw;"
                                "box.appendChild(row);"
                            "}"
                            "while(box.childElementCount>280){box.removeChild(box.firstChild);}"
                            "if(autoScroll)box.scrollTop=box.scrollHeight;"
                        "}"
                        "function pollLiveFeed(){"
                            "var url='/live-data';"
                            "if(liveCursor>=0)url+='?after='+liveCursor;"
                            "fetch(url,{cache:'no-store'})"
                                ".then(function(r){if(!r.ok)throw new Error('http '+r.status);return r.json();})"
                                ".then(function(d){"
                                    "var cnt=(d&&d.lines&&d.lines.length)?d.lines.length:0;"
                                    "if(cnt)appendLiveLines(d.lines);"
                                    "if(d&&typeof d.total==='number')liveCursor=d.total-1;"
                                    "setLiveStatus(cnt?('+'+cnt+' update'+(cnt===1?'':'s')):'Listening...');"
                                "})"
                                ".catch(function(){setLiveStatus('Live feed unavailable');});"
                        "}"
                        "function startLivePolling(){"
                            "if(livePollTimer)return;"
                            "setLiveStatus('Connecting...');"
                            "pollLiveFeed();"
                            "livePollTimer=setInterval(pollLiveFeed,1500);"
                        "}"
                        "function stopLivePolling(){"
                            "if(!livePollTimer)return;"
                            "clearInterval(livePollTimer);"
                            "livePollTimer=null;"
                            "setLiveStatus('Paused');"
                        "}"
                        "function ensureNodeMap(){"
                            "var mapEl=document.getElementById('node-heatmap');"
                            "if(!mapEl)return;"
                            "if(!window.L){"
                                "mapEl.innerHTML='<div style=\"padding:1em;color:#ffd0d0\">Map library unavailable. Check internet access for tile/CDN loading.</div>';"
                                "setMapStatus('Basemap unavailable');"
                                "return;"
                            "}"
                            "if(nodeMap){nodeMap.invalidateSize();return;}"
                            "nodeMap=L.map('node-heatmap',{zoomControl:true,worldCopyJump:true,minZoom:2,maxZoom:19});"
                            "var base=L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{" 
                                "maxZoom:19,attribution:'&copy; OpenStreetMap contributors'"
                            "}).addTo(nodeMap);"
                            "base.on('tileerror',function(){setMapStatus('Basemap tile load issue');});"
                            "nodeMarkerLayer=L.featureGroup().addTo(nodeMap);"
                            "var pts=(window.NODE_HEAT_POINTS||[]);"
                            "var heatPts=[];"
                            "for(var i=0;i<pts.length;i++){"
                                "var p=pts[i];"
                                "if(!isFinite(p.lat)||!isFinite(p.lon))continue;"
                                "var ll=[p.lat,p.lon];"
                                "heatPts.push([p.lat,p.lon,0.9]);"
                                "L.circleMarker(ll,{radius:5,weight:1,color:'#fff0b2',fillColor:'#ff7a45',fillOpacity:0.8}).addTo(nodeMarkerLayer);"
                            "}"
                            "if(window.L.heatLayer&&heatPts.length){"
                                "nodeHeatLayer=L.heatLayer(heatPts,{radius:26,blur:22,maxZoom:13,"
                                    "gradient:{0.2:'#2b83ba',0.45:'#abdda4',0.65:'#fdae61',0.9:'#d7191c'}}).addTo(nodeMap);"
                                "nodeHeatOn=true;"
                            "} else {"
                                "nodeHeatOn=false;"
                                "var hb=document.getElementById('map-heat-btn');"
                                "if(hb){hb.textContent='Heat: N/A';hb.disabled=true;hb.classList.remove('active');}"
                            "}"
                            "if(nodeMarkerLayer.getLayers().length){"
                                "nodeMap.fitBounds(nodeMarkerLayer.getBounds(),{padding:[18,18],maxZoom:13});"
                                "setMapStatus(nodeMarkerLayer.getLayers().length+' positioned node'+(nodeMarkerLayer.getLayers().length===1?'':'s'));"
                            "} else {"
                                "nodeMap.setView([20,0],2);"
                                "setMapStatus('No node positions yet');"
                            "}"
                        "}"
                        "function fitNodeMap(){"
                            "ensureNodeMap();"
                            "if(nodeMap&&nodeMarkerLayer&&nodeMarkerLayer.getLayers().length){"
                                "nodeMap.fitBounds(nodeMarkerLayer.getBounds(),{padding:[18,18],maxZoom:13});"
                            "}"
                        "}"
                        "function worldNodeMap(){"
                            "ensureNodeMap();"
                            "if(nodeMap)nodeMap.setView([20,0],2);"
                        "}"
                        "function centerOnMeMap(){"
                            "ensureNodeMap();"
                            "var me=window.NODE_ME_POINT;"
                            "if(!nodeMap||!me||!isFinite(me.lat)||!isFinite(me.lon)){setMapStatus('Device position unavailable');return;}"
                            "var z=nodeMap.getZoom();"
                            "if(!isFinite(z)||z<13)z=13;"
                            "nodeMap.setView([me.lat,me.lon],z);"
                            "setMapStatus('Centered on this device');"
                        "}"
                        "function toggleNodeHeat(){"
                            "ensureNodeMap();"
                            "var hb=document.getElementById('map-heat-btn');"
                            "if(!nodeMap||!nodeHeatLayer){if(hb)hb.classList.remove('active');return;}"
                            "if(nodeHeatOn){nodeMap.removeLayer(nodeHeatLayer);nodeHeatOn=false;if(hb){hb.textContent='Heat: Off';hb.classList.remove('active');}}"
                            "else{nodeHeatLayer.addTo(nodeMap);nodeHeatOn=true;if(hb){hb.textContent='Heat: On';hb.classList.add('active');}}"
                        "}"
                        "function switchTab(tab){"
                            "var isCfg=(tab==='config');"
                            "var isUtil=(tab==='utils');"
                            "var isLive=(tab==='live');"
                            "var isMap=(tab==='map');"
                            "document.getElementById('tab-config').classList.toggle('active',isCfg);"
                            "document.getElementById('tab-utils').classList.toggle('active',isUtil);"
                            "document.getElementById('tab-live').classList.toggle('active',isLive);"
                            "document.getElementById('tab-map').classList.toggle('active',isMap);"
                            "document.getElementById('tab-btn-config').classList.toggle('active',isCfg);"
                            "document.getElementById('tab-btn-utils').classList.toggle('active',isUtil);"
                            "document.getElementById('tab-btn-live').classList.toggle('active',isLive);"
                            "document.getElementById('tab-btn-map').classList.toggle('active',isMap);"
                            "if(isLive)startLivePolling();else stopLivePolling();"
                            "if(isMap){ensureNodeMap();setTimeout(function(){if(nodeMap)nodeMap.invalidateSize();},0);}"
                        "}"
                        "window.addEventListener('resize',function(){"
                            "var mapTab=document.getElementById('tab-map');"
                            "if(mapTab&&mapTab.classList.contains('active')&&nodeMap)nodeMap.invalidateSize();"
                        "});"
                        "</script>";

    html += "</body></html>";
    server.sendContent(html);
    server.sendContent("");   // empty chunk signals end of response
}

// ── Onboarding (WiFi setup) ───────────────────────────────────

static void sendOnboardingPage(const char *err = "") {
    String html = kHead;
    html +=
        "<h2>Camillia-MT Setup</h2>"
        "<p style='color:#555;font-size:.95em'>"
        "Enter your WiFi network name and password. The device will connect "
        "and display its IP address on screen.</p>"
        "<form method='POST' action='/onboard'>"
        "<label>WiFi Name (SSID)"
        "<input name='ssid' type='text' autofocus autocomplete='off' "
        "placeholder='MyNetwork'></label>"
        "<label>WiFi Password"
        "<input name='pass' type='password' autocomplete='off'></label>"
        "<button type='submit'>Connect</button>";
    if (err[0]) {
        html += "<p class='err'>";
        html += err;
        html += "</p>";
    }
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

static void handleGetOnboard() {
    sendOnboardingPage();
}

static void handlePostOnboard() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    if (ssid.isEmpty()) {
        sendOnboardingPage("WiFi name cannot be empty.");
        return;
    }
    Preferences prefs;
    prefs.begin("camillia", false);
    prefs.putString("wifiSsid", ssid);
    prefs.putString("wifiPass", pass);
    prefs.end();

    strncpy(gWifiSsid, ssid.c_str(), sizeof(gWifiSsid) - 1);
    gWifiSsid[sizeof(gWifiSsid) - 1] = '\0';
    strncpy(gWifiPass, pass.c_str(), sizeof(gWifiPass) - 1);
    gWifiPass[sizeof(gWifiPass) - 1] = '\0';
    if (gCfg) {
        strncpy(gCfg->wifiSsid, gWifiSsid, sizeof(gCfg->wifiSsid) - 1);
        gCfg->wifiSsid[sizeof(gCfg->wifiSsid) - 1] = '\0';
        strncpy(gCfg->wifiPass, gWifiPass, sizeof(gCfg->wifiPass) - 1);
        gCfg->wifiPass[sizeof(gCfg->wifiPass) - 1] = '\0';
    }

    String html = kHead;
    html +=
        "<h2>WiFi Saved</h2>"
        "<p>The device is rebooting and will connect to <b>";
    html += ssid;
    html +=
        "</b>.</p>"
        "<p>Once connected, the IP address will appear on the device screen. "
        "Open that address in your browser to complete setup.</p>"
        "</body></html>";
    server.send(200, "text/html", html);
    server.stop();
    delay(500);
    ESP.restart();
}

// ── Route handlers ────────────────────────────────────────────

static void handleGetRoot() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    char msg[sizeof(gFlashMsg)];
    strncpy(msg, gFlashMsg, sizeof(msg));
    msg[sizeof(msg) - 1] = '\0';
    gFlashMsg[0] = '\0';
    sendConfigPage(msg);
}

static void handleGetLogin() {
    if (isLoggedIn()) { redirect("/"); return; }
    sendLoginPage();
}

static void handlePostLogin() {
    String u = server.arg("u");
    String p = server.arg("p");
    if (u == kUser && p == currentWebCfgPassword()) {
        uint32_t r1 = esp_random(), r2 = esp_random();
        snprintf(sessionToken, sizeof(sessionToken), "%08x%08x", r1, r2);
        String cookie = String("sess=") + sessionToken + "; Path=/; HttpOnly";
        server.sendHeader("Set-Cookie", cookie);
        redirect("/");
    } else {
        sendLoginPage("Invalid username or password.");
    }
}

static void handlePostSave() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }

    // Node identity
    String lng  = server.arg("long");
    String shrt = server.arg("short");
    utf8util::copyTruncate(gCfg->nodeLong, sizeof(gCfg->nodeLong), lng.c_str());
    utf8util::copyTruncate(gCfg->nodeShort, sizeof(gCfg->nodeShort), shrt.c_str());

    // Web config auth (username is fixed to admin)
    String webPass = server.arg("web_pass");
    if (webPass.length() > 0) {
        strncpy(gCfg->webCfgPass, webPass.c_str(), sizeof(gCfg->webCfgPass) - 1);
        gCfg->webCfgPass[sizeof(gCfg->webCfgPass) - 1] = '\0';
    }
    if (!gCfg->webCfgPass[0]) {
        strncpy(gCfg->webCfgPass, kDefaultWebPass, sizeof(gCfg->webCfgPass) - 1);
        gCfg->webCfgPass[sizeof(gCfg->webCfgPass) - 1] = '\0';
    }

    // Node ID override hidden for end users; preserve existing value
    // String ovr = server.arg("node_id_ovr");
    // gCfg->nodeIdOverride = (ovr.length() > 0) ? (uint32_t)strtoul(ovr.c_str(), nullptr, 16) : 0;

    // Device
    gCfg->deviceRole        = (uint8_t)constrain(server.arg("role").toInt(),        0, 10);
    gCfg->rebroadcastMode   = (uint8_t)constrain(server.arg("rebroadcast").toInt(), 0,  4);
    gCfg->nodeInfoIntervalS = (uint32_t)max((long)60, server.arg("nodeinfo_intv").toInt());
    gCfg->posIntervalS      = (uint32_t)max((long)60, server.arg("pos_intv").toInt());
    if (server.hasArg("gps_poll_s")) {
        gCfg->gpsPollIntervalS = (uint32_t)constrain(server.arg("gps_poll_s").toInt(), 0, 3600);
    } else if (server.hasArg("gps_poll_ms")) {
        long oldMsSigned = server.arg("gps_poll_ms").toInt();
        if (oldMsSigned < 0) oldMsSigned = 0;
        uint32_t oldMs = (uint32_t)oldMsSigned;
        gCfg->gpsPollIntervalS = (oldMs == 0) ? 0 : (uint32_t)constrain((long)((oldMs + 999UL) / 1000UL), (long)0, (long)3600);
    }
    strncpy(gCfg->tzDef, server.arg("tzdef").c_str(), sizeof(gCfg->tzDef) - 1);
    gCfg->tzDef[sizeof(gCfg->tzDef) - 1] = '\0';
    strncpy(gCfg->ntpServer, server.arg("ntp_server").c_str(), sizeof(gCfg->ntpServer) - 1);
    gCfg->ntpServer[sizeof(gCfg->ntpServer) - 1] = '\0';
    if (!gCfg->ntpServer[0]) {
        strncpy(gCfg->ntpServer, MY_NTP_SERVER, sizeof(gCfg->ntpServer) - 1);
        gCfg->ntpServer[sizeof(gCfg->ntpServer) - 1] = '\0';
    }

    // Position
    gCfg->gpsEnabled = (server.arg("gpsEnabled") == "1");
    gCfg->latI = (int32_t)(server.arg("lat").toFloat() * 1e7f);
    gCfg->lonI = (int32_t)(server.arg("lon").toFloat() * 1e7f);
    gCfg->alt  = (int32_t)server.arg("alt").toInt();

    // Channels
    for (int i = 0; i < MESH_CHANNELS; i++) {
        char field[16];
        snprintf(field, sizeof(field), "ch%d_name", i);
        String nm = server.arg(field);
        nm.trim();
        if (debugMessagesEnabled()) {
            Serial.printf("[cfg] ch%d: name='%s' key='%s' role='%s'\n", i,
                          nm.c_str(), server.arg(String("ch") + i + "_key").c_str(),
                          server.arg(String("ch") + i + "_role").c_str());
        }
        if (nm.length() > 0 && nm.length() < sizeof(CHANNEL_KEYS[i].name_buf)) {
            strncpy(CHANNEL_KEYS[i].name_buf, nm.c_str(), sizeof(CHANNEL_KEYS[i].name_buf) - 1);
            CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
            CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
        }
        snprintf(field, sizeof(field), "ch%d_key", i);
        String kh = server.arg(field);
        kh.trim();
        if (kh.length() >= 2) {
            uint8_t kbuf[32];
            int klen = base64Decode(kh.c_str(), kbuf, 32);
            if (klen > 0) { memcpy(CHANNEL_KEYS[i].key, kbuf, klen); CHANNEL_KEYS[i].keyLen = (uint8_t)klen; }
        }
        snprintf(field, sizeof(field), "ch%d_role", i);
        CHANNEL_KEYS[i].role = (uint8_t)constrain(server.arg(field).toInt(), 0, 2);
        // Recompute on-air hash from current name + key
        const char *nm2 = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
        CHANNEL_KEYS[i].hash = computeChannelHash(nm2, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
    }

    // Region
    String rgn = server.arg("region");
    if (rgn.length() > 0 && rgn.length() < sizeof(gCfg->region))
        strncpy(gCfg->region, rgn.c_str(), sizeof(gCfg->region) - 1);

    // LoRa
    gCfg->okToMqtt    = (server.arg("ok_to_mqtt")   == "1");
    gCfg->ignoreMqtt  = (server.arg("ignore_mqtt")  == "1");
    gCfg->loraFreq     = server.arg("freq").toFloat();
    gCfg->loraBw       = server.arg("bw").toFloat();
    gCfg->loraSf       = (uint8_t)constrain(server.arg("sf").toInt(),  7, 12);
    gCfg->loraCr       = (uint8_t)constrain(server.arg("cr").toInt(),  5,  8);
    gCfg->loraPower    = (uint8_t)constrain(server.arg("pwr").toInt(), 1, 22);
    gCfg->loraHopLimit = (uint8_t)constrain(server.arg("hop").toInt(), 1,  7);

    // Network / MQTT (only apply when fields are present; UI currently hides these controls)
    if (server.hasArg("mqtt_en")) {
        gCfg->mqttEnabled = server.arg("mqtt_en").toInt() != 0;
    }
    if (server.hasArg("mqtt_server")) {
        strncpy(gCfg->mqttServer, server.arg("mqtt_server").c_str(), sizeof(gCfg->mqttServer) - 1);
        gCfg->mqttServer[sizeof(gCfg->mqttServer) - 1] = '\0';
    }
    if (server.hasArg("mqtt_user")) {
        strncpy(gCfg->mqttUser, server.arg("mqtt_user").c_str(), sizeof(gCfg->mqttUser) - 1);
        gCfg->mqttUser[sizeof(gCfg->mqttUser) - 1] = '\0';
    }
    if (server.hasArg("mqtt_pass")) {
        strncpy(gCfg->mqttPass, server.arg("mqtt_pass").c_str(), sizeof(gCfg->mqttPass) - 1);
        gCfg->mqttPass[sizeof(gCfg->mqttPass) - 1] = '\0';
    }
    if (server.hasArg("mqtt_root")) {
        strncpy(gCfg->mqttRoot, server.arg("mqtt_root").c_str(), sizeof(gCfg->mqttRoot) - 1);
        gCfg->mqttRoot[sizeof(gCfg->mqttRoot) - 1] = '\0';
    }
    if (server.hasArg("mqtt_encrypt")) {
        gCfg->mqttEncryption = (server.arg("mqtt_encrypt") == "1");
    }
    if (server.hasArg("mqtt_map_report")) {
        gCfg->mqttMapReport  = (server.arg("mqtt_map_report") == "1");
    }

    // Display
    gCfg->screenOnSecs    = (uint32_t)server.arg("screen_on").toInt();
    gCfg->displayUnits    = server.arg("disp_units").toInt() != 0 ? 1 : 0;
    gCfg->compassNorthTop = server.arg("compass_north").toInt() != 0;
    gCfg->flipScreen      = server.arg("flip_screen").toInt() != 0;
    gCfg->splashMelodyEnabled = server.arg("splash_melody").toInt() != 0;
    // Legacy compatibility: only apply chat spacing if an older web form sends it.
    if (server.hasArg("chat_space")) {
        gCfg->chatSpacing = (uint8_t)constrain(server.arg("chat_space").toInt(), 0, 2);
    }
    if (server.hasArg("ui_theme_preset")) {
        uint8_t preset = (uint8_t)constrain(server.arg("ui_theme_preset").toInt(), 0, 23);
        gCfg->uiTheme = (uint8_t)constrain((int)(preset / 2), 0, UI_THEME_COUNT - 1);
        gCfg->uiMode = (uint8_t)((preset & 1) ? UI_MODE_LIGHT : UI_MODE_DARK);
    } else {
        // Backward-compatible fallback for older forms.
        gCfg->uiTheme = (uint8_t)constrain(server.arg("ui_theme").toInt(), 0, UI_THEME_COUNT - 1);
        gCfg->uiMode  = (uint8_t)(server.arg("ui_mode").toInt() != 0 ? UI_MODE_LIGHT : UI_MODE_DARK);
    }

    // Power
    gCfg->isPowerSaving = server.arg("pwr_saving").toInt() != 0;
    gCfg->lsSecs        = (uint32_t)server.arg("ls_secs").toInt();
    gCfg->minWakeSecs   = (uint32_t)server.arg("min_wake").toInt();

    // Modules
    uint32_t telemetryIntervalS = 3600UL;
    if (server.hasArg("tel_bcast_mins")) {
        long mins = server.arg("tel_bcast_mins").toInt();
        if (mins < 60L) mins = 60L;
        telemetryIntervalS = (uint32_t)mins * 60UL;
    } else if (server.hasArg("tel_dev_intv")) {
        long sec = server.arg("tel_dev_intv").toInt();
        if (sec < 3600L) sec = 3600L;
        telemetryIntervalS = (uint32_t)sec;
    }

    gCfg->telDeviceEnabled   = server.arg("tel_dev_en").toInt() != 0;
    gCfg->telDeviceIntervalS = telemetryIntervalS;
#if HAS_ENV_SENSOR_TELEMETRY
    gCfg->telEnvEnabled      = server.arg("tel_env_en").toInt() != 0;
    gCfg->telEnvIntervalS    = telemetryIntervalS;
#else
    gCfg->telEnvEnabled      = false;
    if (gCfg->telEnvIntervalS < 3600UL) gCfg->telEnvIntervalS = 3600UL;
#endif
    gCfg->cannedEnabled      = server.arg("canned_en").toInt() != 0;
    if (server.hasArg("canned_msgs")) {
        strncpy(gCfg->cannedMessages, server.arg("canned_msgs").c_str(), sizeof(gCfg->cannedMessages) - 1);
        gCfg->cannedMessages[sizeof(gCfg->cannedMessages) - 1] = '\0';
    }
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    gCfg->msgAlertSound      = (uint8_t)constrain(server.arg("msg_alert_sound").toInt(), 0, 3);
#endif
    // WiFi credentials — save directly to NVS (not part of gCfg)
    {
        String ssid = server.arg("wifi_ssid");
        String pass = server.arg("wifi_pass");
        ssid.trim(); pass.trim();
        if (ssid.length() > 0) {
            strncpy(gWifiSsid, ssid.c_str(), sizeof(gWifiSsid) - 1);
            gWifiSsid[sizeof(gWifiSsid) - 1] = '\0';
            strncpy(gWifiPass, pass.c_str(), sizeof(gWifiPass) - 1);
            gWifiPass[sizeof(gWifiPass) - 1] = '\0';
            strncpy(gCfg->wifiSsid, gWifiSsid, sizeof(gCfg->wifiSsid) - 1);
            gCfg->wifiSsid[sizeof(gCfg->wifiSsid) - 1] = '\0';
            strncpy(gCfg->wifiPass, gWifiPass, sizeof(gCfg->wifiPass) - 1);
            gCfg->wifiPass[sizeof(gCfg->wifiPass) - 1] = '\0';
        }

        // Persist auth/connectivity keys immediately so reboot recovery doesn't
        // rely solely on higher-level save callbacks.
        Preferences prefs;
        prefs.begin("camillia", false);
        if (gCfg->webCfgPass[0]) prefs.putString("webPass", gCfg->webCfgPass);
        if (gCfg->wifiSsid[0]) prefs.putString("wifiSsid", gCfg->wifiSsid);
        if (gCfg->wifiPass[0]) prefs.putString("wifiPass", gCfg->wifiPass);
        prefs.end();
    }

    if (gOnSave) gOnSave();
    scheduleReboot(900);
    redirectHomeWithFlash("Saved. Rebooting now...");
}

static void handlePostSetDebugMonitor() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) {
        redirectHomeWithFlash("Debug update failed: no config.");
        return;
    }

    bool enabled = (server.arg("dbg_monitor") == "1");
    setMonitorDebugEnabled(enabled);

    Preferences prefs;
    prefs.begin("camillia", false);
    prefs.putBool("dbgAcks", enabled);
    prefs.putBool("dbgMsgs", enabled);
    prefs.putBool("dbgGps", enabled);
    prefs.end();

    redirectHomeWithFlash(enabled
        ? "Debug monitor enabled."
        : "Debug monitor disabled.");
}

static void handleGetLiveData() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    int after = -1;
    if (server.hasArg("after")) after = server.arg("after").toInt();

    Channel &ann = Channels.get(CHAN_ANN);
    int total = ann.count;
    int oldest = max(0, total - MAX_MSG_LINES);
    int from = (after >= 0) ? (after + 1) : (total - 40);
    if (from < oldest) from = oldest;
    if (from < 0) from = 0;
    if (from > total) from = total;

    String out;
    out.reserve(4096);
    out += "{\"total\":";
    out += String(total);
    out += ",\"from\":";
    out += String(from);
    out += ",\"lines\":[";

    if (ann.lines) {
        bool first = true;
        for (int i = from; i < total; i++) {
            const DisplayLine &dl = ann.lines[i % MAX_MSG_LINES];
            if (!first) out += ",";
            first = false;
            out += "{\"i\":";
            out += String(i);
            out += ",\"t\":\"";
            appendJsonEscaped(out, dl.text);
            out += "\"}";
        }
    }

    out += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

static void handleGetReleaseCheck() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    bool startNow = false;
    if (server.hasArg("start")) {
        String arg = server.arg("start");
        startNow = (arg == "1" || arg == "true");
    }

    if (startNow) {
        if (gReleaseCheckState != RELEASE_CHECK_RUNNING &&
            gReleaseCheckState != RELEASE_CHECK_PENDING) {
            queueReleaseCheckNow();
        }
    }

    String current = APP_VERSION;
    String out = "{";
    out += "\"current\":\"";
    appendJsonEscaped(out, current.c_str());
    out += "\"";
    out += ",\"status\":\"";
    out += releaseCheckStateName(gReleaseCheckState);
    out += "\"";

    if (gReleaseCheckState == RELEASE_CHECK_DONE_OK) {
        out += ",\"latest\":\"";
        appendJsonEscaped(out, gReleaseCheckLatest);
        out += "\"";
        out += ",\"updateAvailable\":";
        out += gReleaseCheckUpdateAvailable ? "true" : "false";
        out += ",\"url\":\"";
        appendJsonEscaped(out, gReleaseCheckUrl);
        out += "\"";
    } else if (gReleaseCheckState == RELEASE_CHECK_DONE_ERR) {
        out += ",\"error\":\"";
        appendJsonEscaped(out, gReleaseCheckErr);
        out += "\"";
    }

    if (gReleaseCheckStartedAtMs > 0) {
        out += ",\"startedMs\":";
        out += String(gReleaseCheckStartedAtMs);
    }
    if (gReleaseCheckFinishedAtMs > 0) {
        out += ",\"finishedMs\":";
        out += String(gReleaseCheckFinishedAtMs);
    }

    out += "}";

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

// ── Announce ─────────────────────────────────────────────────

static void handlePostAnnounce() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    webCfgQueueAnnounce();
    redirectHomeWithFlash("NODEINFO broadcast queued.");
}

static void handleGetScreenshot() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gOnScreenshotPng) {
        server.send(503, "text/plain", "Screenshot capture is unavailable.");
        return;
    }

    static const char *kTmpScreenshotPath = "/camillia/web_screenshot.png";
    if (!gOnScreenshotPng(kTmpScreenshotPath)) {
        server.send(500, "text/plain", "Screenshot capture failed.");
        return;
    }

    File f = SD.open(kTmpScreenshotPath, FILE_READ);
    if (!f) {
        server.send(500, "text/plain", "Screenshot file unavailable.");
        return;
    }

    char fileName[64];
    buildScreenshotFileName(gCfg ? gCfg->nodeShort : nullptr, fileName, sizeof(fileName));
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Content-Disposition", cd);
    server.streamFile(f, "image/png");
    f.close();
}

// ── Clear Messages ───────────────────────────────────────────

static void handlePostClearMessages() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    Channels.clearAllMessages(true);
    DMs.clearAll(true);
    redirectHomeWithFlash("All stored messages cleared.");
}

// ── Clear Nodes ───────────────────────────────────────────────

static void handlePostClearNodes() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    Nodes.clearPersisted();
    scheduleReboot(200);
    redirectHomeWithFlash("Node database cleared. Rebooting now...");
}

// ── Factory Reset ─────────────────────────────────────────────

static void handlePostFactoryReset() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    Channels.clearAllMessages(true);
    DMs.clearAll(true);

    // Erase the entire NVS partition
    nvs_flash_erase();
    nvs_flash_init();
    Nodes.clearPersisted();

    // Delete saved DM conversations from SD card (if supported on this board)
#if HAS_SD_CARD
    {
        const char *nodeFiles[] = {
            "/camillia/nodes.db",
            "/camillia/nodes.bin",
            "/camillia/nodes.json",
            "/camillia/node_db.bin",
            "/camillia/node_db.json",
        };
        for (size_t i = 0; i < sizeof(nodeFiles) / sizeof(nodeFiles[0]); i++) {
            if (SD.exists(nodeFiles[i])) {
                SD.remove(nodeFiles[i]);
            }
        }

        File nodesDir = SD.open("/camillia/nodes");
        if (nodesDir && nodesDir.isDirectory()) {
            File f = nodesDir.openNextFile();
            while (f) {
                String fp = String("/camillia/nodes/") + f.name();
                f.close();
                SD.remove(fp.c_str());
                f = nodesDir.openNextFile();
            }
            nodesDir.close();
            SD.rmdir("/camillia/nodes");
        }

        File dir = SD.open("/camillia/dms");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                String fp = String("/camillia/dms/") + f.name();
                f.close();
                SD.remove(fp.c_str());
                f = dir.openNextFile();
            }
            dir.close();
            SD.rmdir("/camillia/dms");
        }
    }
#endif

    scheduleReboot(900);
    redirectHomeWithFlash("Factory reset complete. Rebooting now...");
}

// ── Export / Import ───────────────────────────────────────────

static void handleGetExport() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }
    String yaml;
    char fileName[64];
    char cd[128];
    buildExportFileName(gCfg->nodeShort, fileName, sizeof(fileName));
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    cfgToYaml(*gCfg, yaml);
    server.sendHeader("Content-Disposition", cd);
    server.send(200, "text/x-yaml", yaml);
}

static char   importBuf[8192];
static size_t importLen = 0;
static bool   importOk  = false;

static void handleImportUpload() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        importLen = 0;
        importOk  = false;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        size_t space = sizeof(importBuf) - importLen - 1;
        size_t chunk = upload.currentSize < space ? upload.currentSize : space;
        memcpy(importBuf + importLen, upload.buf, chunk);
        importLen += chunk;
    } else if (upload.status == UPLOAD_FILE_END) {
        importBuf[importLen] = '\0';
        importOk = true;
    }
}

static void handleImportDone() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }
    if (!importOk || importLen == 0) {
        redirectHomeWithFlash("Import failed: no data received.");
        return;
    }
    if (!cfgImportFromBuf(importBuf, importLen, *gCfg)) {
        redirectHomeWithFlash("Import failed: parse error.");
        return;
    }
    if (!gCfg->webCfgPass[0]) {
        strncpy(gCfg->webCfgPass, kDefaultWebPass, sizeof(gCfg->webCfgPass) - 1);
        gCfg->webCfgPass[sizeof(gCfg->webCfgPass) - 1] = '\0';
    }

    // Apply imported WiFi credentials to runtime and NVS.
    strncpy(gWifiSsid, gCfg->wifiSsid, sizeof(gWifiSsid) - 1);
    gWifiSsid[sizeof(gWifiSsid) - 1] = '\0';
    strncpy(gWifiPass, gCfg->wifiPass, sizeof(gWifiPass) - 1);
    gWifiPass[sizeof(gWifiPass) - 1] = '\0';
    {
        Preferences prefs;
        prefs.begin("camillia", false);
        prefs.putString("wifiSsid", gWifiSsid);
        prefs.putString("wifiPass", gWifiPass);
        prefs.end();
    }

    if (gOnSave) gOnSave();
    scheduleReboot(900);
    redirectHomeWithFlash("Import complete. Rebooting now...");
}

static void handleGetLogout() {
    sessionToken[0] = '\0';
    server.sendHeader("Set-Cookie", "sess=; Path=/; Max-Age=0");
    redirect("/login");
}

// ── Public API ────────────────────────────────────────────────

bool webCfgBegin(RhinoConfig *cfg, WebCfgSaveCb onSave,
                 WebCfgScreenshotPngCb onScreenshotPng) {
    if (running) return true;

    gCfg    = cfg;
    gOnSave = onSave;
    gOnScreenshotPng = onScreenshotPng;
    gFlashMsg[0] = '\0';
    gRebootPending = false;
    gRebootAtMs = 0;
    clearReleaseCheckResult();
    gReleaseCheckState = RELEASE_CHECK_IDLE;

    // Load saved WiFi credentials
    Preferences prefs;
    prefs.begin("camillia", true);
    String savedSsid = prefs.isKey("wifiSsid") ? prefs.getString("wifiSsid", "") : "";
    String savedPass = prefs.isKey("wifiPass") ? prefs.getString("wifiPass", "") : "";
    String savedWebPass = prefs.isKey("webPass") ? prefs.getString("webPass", "") : "";
    prefs.end();
    strncpy(gWifiSsid, savedSsid.c_str(), sizeof(gWifiSsid) - 1);
    gWifiSsid[sizeof(gWifiSsid) - 1] = '\0';
    strncpy(gWifiPass, savedPass.c_str(), sizeof(gWifiPass) - 1);
    gWifiPass[sizeof(gWifiPass) - 1] = '\0';
    strncpy(gCfg->wifiSsid, gWifiSsid, sizeof(gCfg->wifiSsid) - 1);
    gCfg->wifiSsid[sizeof(gCfg->wifiSsid) - 1] = '\0';
    strncpy(gCfg->wifiPass, gWifiPass, sizeof(gCfg->wifiPass) - 1);
    gCfg->wifiPass[sizeof(gCfg->wifiPass) - 1] = '\0';
    if (savedWebPass.length() == 0) savedWebPass = kDefaultWebPass;
    strncpy(gCfg->webCfgPass, savedWebPass.c_str(), sizeof(gCfg->webCfgPass) - 1);
    gCfg->webCfgPass[sizeof(gCfg->webCfgPass) - 1] = '\0';

    const char *headers[] = {"Cookie"};
    server.collectHeaders(headers, 1);

    if (savedSsid.isEmpty()) {
        // ── Onboarding mode: create an AP with full config ────
        gOnboarding = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("camillia-mt");
        delay(100);
        WiFi.softAPIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

        // Serve both the onboarding WiFi page and full config
        server.on("/setup",   HTTP_GET,  handleGetOnboard);
        server.on("/onboard", HTTP_POST, handlePostOnboard);
        server.on("/",        HTTP_GET,  handleGetRoot);
        server.on("/login",   HTTP_GET,  handleGetLogin);
        server.on("/login",   HTTP_POST, handlePostLogin);
        server.on("/save",    HTTP_POST, handlePostSave);
        server.on("/set-debug-monitor", HTTP_POST, handlePostSetDebugMonitor);
        server.on("/live-data", HTTP_GET, handleGetLiveData);
        server.on("/release-check", HTTP_GET, handleGetReleaseCheck);
        server.on("/logout",  HTTP_GET,  handleGetLogout);
        server.on("/announce",HTTP_POST, handlePostAnnounce);
        if (gOnScreenshotPng) {
            server.on("/screenshot", HTTP_GET, handleGetScreenshot);
        }
        server.on("/export",  HTTP_GET,  handleGetExport);
        server.on("/import",        HTTP_POST, handleImportDone, handleImportUpload);
        server.on("/clear-messages", HTTP_POST, handlePostClearMessages);
        server.on("/clear-nodes",   HTTP_POST, handlePostClearNodes);
        server.on("/factory-reset", HTTP_POST, handlePostFactoryReset);
        server.begin();
        running = true;
        Serial.printf("[web] onboarding AP at http://%s/\n", ipBuf);
        return true;
    }

    // ── Normal mode: connect to saved WiFi ────────────────────
    gOnboarding = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    Serial.printf("[web] connecting to \"%s\" ...\n", savedSsid.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= kConnectTimeout) {
            Serial.println("[web] STA connect timeout — falling back to AP mode");
            WiFi.disconnect(false);
#ifdef WIFI_AP_STA
            WiFi.mode(WIFI_AP_STA);
#else
            WiFi.mode(WIFI_AP);
#endif
            delay(120);
            if (!WiFi.softAP("camillia-mt")) {
                // Retry with AP-only mode if AP+STA bring-up fails.
                WiFi.mode(WIFI_AP);
                delay(120);
                WiFi.softAP("camillia-mt");
            }
            delay(500);
            WiFi.softAPIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

            server.on("/",        HTTP_GET,  handleGetRoot);
            server.on("/login",   HTTP_GET,  handleGetLogin);
            server.on("/login",   HTTP_POST, handlePostLogin);
            server.on("/save",    HTTP_POST, handlePostSave);
            server.on("/set-debug-monitor", HTTP_POST, handlePostSetDebugMonitor);
            server.on("/live-data", HTTP_GET, handleGetLiveData);
            server.on("/release-check", HTTP_GET, handleGetReleaseCheck);
            server.on("/logout",  HTTP_GET,  handleGetLogout);
            server.on("/announce",HTTP_POST, handlePostAnnounce);
            if (gOnScreenshotPng) {
                server.on("/screenshot", HTTP_GET, handleGetScreenshot);
            }
            server.on("/export",  HTTP_GET,  handleGetExport);
            server.on("/import",        HTTP_POST, handleImportDone, handleImportUpload);
            server.on("/clear-messages", HTTP_POST, handlePostClearMessages);
            server.on("/clear-nodes",   HTTP_POST, handlePostClearNodes);
            server.on("/factory-reset", HTTP_POST, handlePostFactoryReset);
            server.begin();
            running = true;
            Serial.printf("[web] AP fallback at http://%s/\n", ipBuf);
            return true;
        }
        delay(100);
    }

    WiFi.localIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

    server.on("/",        HTTP_GET,  handleGetRoot);
    server.on("/login",   HTTP_GET,  handleGetLogin);
    server.on("/login",   HTTP_POST, handlePostLogin);
    server.on("/save",    HTTP_POST, handlePostSave);
    server.on("/set-debug-monitor", HTTP_POST, handlePostSetDebugMonitor);
    server.on("/live-data", HTTP_GET, handleGetLiveData);
    server.on("/release-check", HTTP_GET, handleGetReleaseCheck);
    server.on("/logout",  HTTP_GET,  handleGetLogout);
    server.on("/announce",HTTP_POST, handlePostAnnounce);
    if (gOnScreenshotPng) {
        server.on("/screenshot", HTTP_GET, handleGetScreenshot);
    }
    server.on("/export",  HTTP_GET,  handleGetExport);
    server.on("/import",        HTTP_POST, handleImportDone, handleImportUpload);
    server.on("/clear-messages", HTTP_POST, handlePostClearMessages);
    server.on("/clear-nodes",   HTTP_POST, handlePostClearNodes);
    server.on("/factory-reset", HTTP_POST, handlePostFactoryReset);
    server.begin();
    running = true;
    Serial.printf("[web] ready at http://%s/\n", ipBuf);
    return true;
}

void webCfgEnd() {
    if (!running) return;
    sessionToken[0] = '\0';
    gFlashMsg[0] = '\0';
    gRebootPending = false;
    gRebootAtMs = 0;
    clearReleaseCheckResult();
    gReleaseCheckState = RELEASE_CHECK_IDLE;
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ipBuf[0]    = '\0';
    gCfg        = nullptr;
    gOnSave     = nullptr;
    gOnScreenshotPng = nullptr;
    running     = false;
    gOnboarding = false;
    Serial.println("[web] stopped");
}

bool webCfgIsOnboarding() { return gOnboarding; }
const char *webCfgWifiSsid() { return gWifiSsid; }
const char *webCfgWifiPass() { return gWifiPass; }

void webCfgLoop() {
    if (!running) return;

    server.handleClient();
    runQueuedReleaseCheck();
    if (gRebootPending && (int32_t)(millis() - gRebootAtMs) >= 0) {
        gRebootPending = false;
        server.stop();
        delay(100);
        ESP.restart();
    }
}

bool webCfgRunning() { return running; }
const char *webCfgIP() { return ipBuf; }

bool webCfgAnnounceRequested() {
    if (!gAnnounceReq) return false;
    gAnnounceReq = false;
    return true;
}

void webCfgQueueAnnounce() {
    gAnnounceReq = true;
}
