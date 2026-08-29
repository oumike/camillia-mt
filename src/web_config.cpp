#include "web_config.h"
#include "ble_keyboard.h"   // coexistence holds Wi-Fi modem sleep on
#if HAS_VNC_HOST
#include "vnc_host.h"
#endif
#include "mesh_channel_plan.h"
#include "base64_util.h"
#include "web_icon.h"
#include "node_db.h"
#include "channel_mgr.h"
#include "dm_mgr.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "storage.h"
#include "state_maps.h"
#include "env_sensor.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/select.h>   // clientWritable(): poll the socket before writing
#include "debug_flags.h"
#include "gps.h"
#include "ota_update.h"   // otaLayoutSupportsUpdate()
#include "battery_util.h"
#include "utf8_utils.h"

static const uint32_t kConnectTimeout  = 10000;  // ms
static const uint32_t kReleaseCheckTimeoutMs = 7000;
static const uint32_t kWebTlsMinInternalFree = 70000;
static const uint32_t kWebTlsMinLargestBlock = 52000;
static const bool kWebDeviceReleaseCheckEnabled = false;
static const char    *kLatestReleaseApiUrl = "https://api.github.com/repos/oumike/camillia-mt/releases/latest";
static const char    *kLatestVersionRawUrl = "https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION";
static const char    *kLatestReleasePageUrl = "https://github.com/oumike/camillia-mt/releases/latest";

static const char    *kUser            = "admin";
static const char    *kDefaultWebPass  = "admin";

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static WebServer      server(80);
// Captive-portal DNS for AP mode: answers every lookup with the SoftAP IP so a
// connecting phone resolves its portal-detection host to us and opens the page
// immediately, instead of firing partial/abandoned probe requests at an open AP
// that never resolves — those stall the synchronous WebServer's header read and
// freeze the main loop. Only active while an AP is up.
static DNSServer      gDns;
static bool           gCaptiveActive   = false;
static bool           running          = false;
static bool           gOnboarding      = false;
// ── Idle timeout ─────────────────────────────────────────────────────────────
// Web config has to run Wi-Fi with modem power-save disabled (see the
// WiFi.setSleep(false) calls below), which makes it one of the largest
// continuous draws on the device — and nothing ever stopped it except the user
// remembering to. Stamped on every request; webCfgLoop() raises gIdleExpired
// once the window passes and the main loop performs the actual teardown, so
// the shutdown path stays the same one the manual toggle uses.
static uint32_t       gLastRequestMs   = 0;
static uint32_t       gIdleTimeoutMs   = 0;     // 0 = never expire
static bool           gIdleExpired     = false;
// True while the server is serving the SoftAP variant ("web config lite"): the
// Config tab only, no Utilities/Live/Chat/Nodes. AP mode has far less internal
// heap to work with than STA, and the heavy tabs are what tip it over. This is
// tracked separately from gCaptiveActive because captive DNS can fail to start
// while the AP itself is up — the page weight must follow the radio mode, not
// the DNS server.
static bool           gApMode          = false;
// Set from the on-device WiFi picker's "AP" entry: start the SoftAP even when
// credentials are saved, instead of joining the network.
static bool           gForceAp         = false;
// Set from the on-device WiFi picker's network entries, via webCfgSetStaCreds().
// Empty ssid means "no override" — use the credentials stored in RhinoConfig.
// The picker's choice is not written back into the config (that would overwrite
// the user's configured default), so it has to reach us this way or we would
// dial the wrong network every time web config starts.
static char           gStaSsidOverride[64] = {};
static char           gStaPassOverride[64] = {};
// Pumped while the station-connect wait below blocks; see webCfgSetWaitCb().
static void         (*gWaitCb)()           = nullptr;

// Boards that always serve web config lite, regardless of radio mode. The
// Cardputer has no PSRAM and ~23 KB of internal heap once Wi-Fi is up, which the
// full page's Live/Nodes/Utilities panes and their JS cannot fit: writes crawl
// at a few hundred bytes per 300 ms because the stack can't get TX buffers.
// Lite carries the entire Config pane, so nothing configurable is lost.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static const bool     kLiteOnlyBoard   = true;
#else
static const bool     kLiteOnlyBoard   = false;
#endif

// True when the current response should be the lite page.
static inline bool webCfgUseLite() { return gApMode || kLiteOnlyBoard; }
static char           ipBuf[16]        = "";
static char           sessionToken[17] = "";   // hex token; empty = no session
static RhinoConfig   *gCfg             = nullptr;

// The credentials webCfgBegin() should actually associate with: the picker's
// override when one was pushed, otherwise what is stored in the config.
static const char *staConnectSsid() {
    return gStaSsidOverride[0] ? gStaSsidOverride : gCfg->wifiSsid;
}
static const char *staConnectPass() {
    return gStaSsidOverride[0] ? gStaPassOverride : gCfg->wifiPass;
}
static WebCfgSaveCb   gOnSave          = nullptr;
static WebCfgScreenshotPngCb gOnScreenshotPng = nullptr;
static volatile bool  gAnnounceReq     = false;
static volatile bool  gTelemetryReq    = false;
// Chat tab: single pending send drained by the main loop.
static volatile bool  gChatSendReq     = false;
static bool           gChatSendIsDm    = false;
static uint32_t       gChatSendTarget  = 0;
static uint32_t       gChatSendReplyId = 0;
static uint32_t       gChatSendEmoji   = 0;
static char           gChatSendText[MESH_TEXT_MAX_LEN + 1] = "";
// Manual clock set from the config form, applied on the main loop.
static volatile bool  gManualTimeReq   = false;
static int            gManualTime[5]   = {0, 0, 0, 0, 0};  // y, mon, day, hour, min
#if HAS_VNC_HOST
static volatile bool  gVncToggleReq    = false;
static bool           gVncToggleOn     = false;
#endif
static char           gWifiSsid[64]    = "";
static char           gWifiPass[64]    = "";
static char           gFlashMsg[128]   = "";
static bool           gRebootPending   = false;
static uint32_t       gRebootAtMs      = 0;
static bool           gWebBuffersReclaimed = false;

static int compareVersionTags(const char *a, const char *b);
static bool isPrereleaseTag(const char *tag);
static bool fetchLatestReleaseInfo(String &tagOut, String &urlOut, String &errOut);
static void logWebHttpsDiag(const char *stage, const char *url = nullptr);

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

static void logWifiHeapDiag(const char *tag) {
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t freeDma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t largestDma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    Serial.printf("[web] %s [heap int free=%u largest=%u 8bit=%u dma free=%u largest=%u]\n",
                  tag ? tag : "wifi",
                  (unsigned)freeInt,
                  (unsigned)largestInt,
                  (unsigned)free8,
                  (unsigned)freeDma,
                  (unsigned)largestDma);
}

static bool ensureWifiMode(wifi_mode_t mode, const char *stageTag) {
    if (WiFi.getMode() == mode) return true;
    if (WiFi.mode(mode)) return true;
    Serial.printf("[web] WiFi.mode(%d) failed at %s\n",
                  (int)mode,
                  stageTag ? stageTag : "startup");
    logWifiHeapDiag("wifi mode failure");
    return false;
}

static void runQueuedReleaseCheck() {
    if (gReleaseCheckState != RELEASE_CHECK_PENDING) return;

    if (!kWebDeviceReleaseCheckEnabled) {
        gReleaseCheckState = RELEASE_CHECK_DONE_ERR;
        gReleaseCheckStartedAtMs = millis();
        gReleaseCheckFinishedAtMs = gReleaseCheckStartedAtMs;
        strncpy(gReleaseCheckErr,
                "Device-side release check disabled on this build",
                sizeof(gReleaseCheckErr) - 1);
        gReleaseCheckErr[sizeof(gReleaseCheckErr) - 1] = '\0';
        return;
    }

    gReleaseCheckState = RELEASE_CHECK_RUNNING;
    gReleaseCheckStartedAtMs = millis();
    logWebHttpsDiag("release-check-start");

    String latest;
    String url;
    String err;
    bool ok = fetchLatestReleaseInfo(latest, url, err);

    if (ok) {
        // Never advertise a prerelease (alpha) as an available update, even if
        // the resolved tag somehow is one. /releases/latest excludes them, but
        // guard here too so the web UI never nudges onto an alpha build.
        gReleaseCheckUpdateAvailable =
            !isPrereleaseTag(latest.c_str())
            && compareVersionTags(APP_VERSION, latest.c_str()) < 0;
        strncpy(gReleaseCheckLatest, latest.c_str(), sizeof(gReleaseCheckLatest) - 1);
        gReleaseCheckLatest[sizeof(gReleaseCheckLatest) - 1] = '\0';
        strncpy(gReleaseCheckUrl, url.c_str(), sizeof(gReleaseCheckUrl) - 1);
        gReleaseCheckUrl[sizeof(gReleaseCheckUrl) - 1] = '\0';
        gReleaseCheckState = RELEASE_CHECK_DONE_OK;
        Serial.printf("[web-https] release-check-ok latest=%s\n", gReleaseCheckLatest);
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
        Serial.printf("[web-https] release-check-fail err=%s\n", gReleaseCheckErr);
    }

    gReleaseCheckFinishedAtMs = millis();
}

// ── Helpers ───────────────────────────────────────────────────

static const char *currentWebCfgPassword() {
    if (gCfg && gCfg->webCfgPass[0]) return gCfg->webCfgPass;
    return kDefaultWebPass;
}

static bool isLoggedIn() {
    // When authentication is disabled the web config is open — treat every
    // request as authenticated so all gated handlers pass through.
    if (gCfg && !gCfg->webCfgAuthEnabled) return true;
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

static void logWebHttpsDiag(const char *stage, const char *url) {
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (url && url[0]) {
        Serial.printf("[web-https] %s url=%s [heap int free=%u largest=%u]\n",
                      stage ? stage : "event",
                      url,
                      (unsigned)freeInt,
                      (unsigned)largestInt);
    } else {
        Serial.printf("[web-https] %s [heap int free=%u largest=%u]\n",
                      stage ? stage : "event",
                      (unsigned)freeInt,
                      (unsigned)largestInt);
    }
}

static void preferExternalHeapForWebHttps() {
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
    static bool enabled = false;
    if (!enabled) {
        // Route generic heap allocations to PSRAM first, preserving internal
        // contiguous blocks needed by mbedTLS/esp-sha during handshake.
        heap_caps_malloc_extmem_enable(0);
        enabled = true;
    }
#endif
}

static bool webHasHeapForTls(String &errOut) {
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (freeInt >= kWebTlsMinInternalFree && largestInt >= kWebTlsMinLargestBlock) {
        errOut = "";
        return true;
    }

    char buf[128];
    snprintf(buf,
             sizeof(buf),
             "TLS skipped (low heap): int_free=%u largest=%u",
             (unsigned)freeInt,
             (unsigned)largestInt);
    errOut = buf;
    logWebHttpsDiag("tls-skip-low-heap");
    return false;
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

// A tag is a prerelease when it carries a SemVer prerelease suffix (anything
// after a '-', e.g. "v3.1.2-alpha.1"). We never surface these as an available
// update, so devices are never nudged onto an alpha build.
static bool isPrereleaseTag(const char *tag) {
    if (!tag) return false;
    for (const char *p = tag; *p; ++p) {
        if (*p == '-') return true;
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

// The on-device release check is disabled (kWebDeviceReleaseCheckEnabled) and
// its UI was removed, so these HTTPS fetches were unreachable. They were the
// last TLS users in this file; removed so WiFiClientSecure can be dropped from
// the firmware entirely.
static bool fetchLatestReleaseInfo(String &tagOut, String &urlOut, String &errOut) {
    tagOut = "";
    urlOut = kLatestReleasePageUrl;
    errOut = "Release check disabled (no TLS in firmware)";
    return false;
}

// ── HTML helpers ──────────────────────────────────────────────

// ── Favicon ──────────────────────────────────────────────────────────────────
// The markup lives in web_icon.h, shared with the VNC viewer page, which is a
// second HTML page on a second server and cannot reach this one's route.
static const char kFavicon[] = CAMELLIA_FAVICON_SVG;

static const char kHead[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Camillia for Meshtastic</title>"
    "<link rel='icon' type='image/svg+xml' href='/favicon.ico'>"
    "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'>"
    "<style>"
        ":root{color-scheme:dark;--bg:#10141d;--panel:#1a2230;--panel-2:#232d3e;--line:#4a5b73;"
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
        "option{background:var(--panel-2);color:var(--text)}"
        "input[readonly]{background:var(--panel);color:var(--text-dim)}"
        "button{margin-top:1.2em;padding:.55em 1.8em;background:var(--accent);color:var(--accent-ink);"
                     "border:none;border-radius:5px;cursor:pointer;font-size:1em;font-weight:600}"
        ".msg{color:var(--accent);margin:.5em 0}"
        ".err{color:#ff8d8d;margin:.5em 0}"
        // Chooser modal (font size). Reused via the .chooser-* classes.
        ".chooser-btn{width:100%;margin-top:.1em;padding:.45em;text-align:left;background:var(--panel-2);"
             "color:var(--text);border:1px solid var(--line);border-radius:5px;cursor:pointer;font-size:.95em;"
             "font-weight:600}"
        ".modal-back{display:none;position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:50;"
             "align-items:center;justify-content:center}"
        ".modal-back.open{display:flex}"
        ".modal-card{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:1em;"
             "width:min(90vw,320px);box-shadow:0 10px 30px rgba(0,0,0,.5)}"
        ".modal-card h3{margin:.1em 0 .6em;border:none;color:var(--accent)}"
        ".modal-opt{width:100%;margin:.35em 0;padding:.6em;background:var(--panel-2);color:var(--text);"
             "border:1px solid var(--line);border-radius:6px;cursor:pointer;font-size:.95em;text-align:left}"
        ".modal-opt.sel{border-color:var(--accent);background:var(--panel-2)}"
        ".modal-opt b{display:block;font-weight:700}"
        ".modal-opt span{display:block;font-size:.8em;color:var(--text-dim)}"
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
        ".tab-pane-center{max-width:760px;margin:0 auto}"
        "#tab-config form{max-width:760px;margin:0 auto}"
        // Same 760px column and same left alignment as the config tab, so the
        // section boxes and their contents line up when switching tabs. The
        // per-element centering this replaced (auto margins on h3/p/form plus
        // text-align:center on the pane) is all dropped rather than overridden —
        // left is the default, so the overrides that fought it are dead weight.
        "#tab-utils .tab-pane-center{max-width:760px}"
        "#tab-utils form{max-width:520px;margin:.5em 0}"
                ".remote-toggle{display:flex;align-items:center;gap:.55em;margin:.7em 0;color:var(--text)}"
                ".remote-toggle input{width:auto;margin:0}"
                ".remote-status{font-size:.82em;color:var(--text-dim);margin:.35em 0 .6em}"
                ".remote-frame{display:block;width:100%;height:min(72vh,520px);border:1px solid var(--line);"
                         "border-radius:6px;background:#000}"
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
        ".chat-feed{margin-top:.6em;height:360px;overflow:auto;border:1px solid var(--line);border-radius:8px;background:var(--bg);padding:.5em;display:flex;flex-direction:column;gap:.4em}"
        ".chat-msg{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:.4em .55em;max-width:88%}"
        ".chat-msg.mine{align-self:flex-end;background:var(--panel-2);border-color:var(--accent)}"
        ".chat-body{font-size:.9em;white-space:pre-wrap;word-break:break-word}"
        ".chat-actions{margin-top:.3em;display:flex;flex-wrap:wrap;gap:.25em;align-items:center}"
        ".chat-tap{margin:0;padding:.1em .4em;font-size:.95em;line-height:1.2;background:var(--panel-2);color:var(--text);border:1px solid var(--line);border-radius:999px;cursor:pointer}"
        ".chat-mini{margin:0;padding:.1em .5em;font-size:.8em;background:var(--panel-2);color:var(--text);border:1px solid var(--line);border-radius:6px;cursor:pointer}"
        ".chat-reply{margin-top:.5em;display:flex;align-items:center;gap:.5em;font-size:.82em;color:var(--text-dim);border-left:3px solid var(--accent);padding:.2em .5em;background:var(--panel)}"
        ".chat-reply span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
        ".chat-compose{margin-top:.5em;display:flex;gap:.5em}"
        ".chat-compose input{flex:1}"
        ".chat-compose button{margin:0}"
        ".node-head{display:flex;align-items:flex-start;justify-content:space-between;gap:.6em}"
        ".node-title{font-weight:700;color:var(--accent);margin-bottom:.2em}"
        ".fav-tag{margin-left:.4em;font-weight:600;font-size:.78em;color:var(--text-dim)}"
        ".fav-btn{flex:0 0 auto;white-space:nowrap}"
        ".fav-btn[data-fav='1']{border-color:var(--accent);color:var(--accent)}"
        ".fav-btn:disabled{opacity:.6;cursor:default}"
        ".node-meta{font-size:.82em;color:var(--text-dim);margin:.15em 0;word-break:break-word}"
        ".node-meta b{color:var(--text)}"
        ".live-wrap{margin-top:.6em;border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:.5em}"
        ".live-toolbar{display:flex;justify-content:space-between;align-items:center;gap:.5em;margin-bottom:.45em}"
        ".live-toolbar span{font-size:.68em;color:var(--text-dim)}"
        ".live-feed{height:380px;overflow:auto;border:1px solid var(--line);border-radius:6px;background:var(--bg)}"
        ".chart-wrap{margin-top:.6em;display:grid;grid-template-columns:1fr;gap:.7em}"
        ".chart-box{border:1px solid var(--line);border-radius:8px;background:var(--panel);padding:.55em .7em}"
        ".chart-head{display:flex;justify-content:space-between;align-items:baseline;gap:.5em;margin-bottom:.35em;font-size:.85em}"
        ".chart-stats{font-size:.72em;color:var(--text-dim);font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}"
        ".chart-canvas{width:100%;height:180px;display:block;background:var(--bg);border:1px solid var(--line);border-radius:6px}"
        ".chart-legend{margin-top:.3em;font-size:.72em;color:var(--text-dim)}"
        ".chart-legend .swatch{display:inline-block;width:.8em;height:.8em;margin-right:.25em;vertical-align:middle;border-radius:2px}"
        ".sw-chutil{background:#67d8ff}.sw-airutil{background:#ffd56b}"
        ".sw-snr{background:#9ceadf}.sw-rssi{background:#f7b46d}"
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
    // One card per channel. The old four-column grid predated the uplink,
    // downlink and location toggles: with seven children in four tracks the
    // fields wrapped into implicit rows that lined up with nothing.
    ".ch-row{border:1px solid var(--line);border-radius:6px;background:var(--bg);"
         "padding:.45em .6em;margin:.5em 0}"
    ".ch-row label{margin:0;font-size:.85em}"
    ".ch-hd{display:flex;align-items:center;gap:.35em;margin-bottom:.4em}"
    ".ch-no{font-weight:600;font-size:.85em;color:var(--accent);margin-right:auto}"
    ".ch-fields{display:grid;grid-template-columns:1fr 2fr 1fr;gap:.4em;align-items:end}"
    ".ch-tog{display:flex;flex-wrap:wrap;gap:.15em .9em;margin-top:.45em}"
    ".ch-tog label{display:flex;align-items:center;gap:.3em}"
    ".ch-btn{margin:0;padding:.3em .6em;font-size:.8em;line-height:1.15;cursor:pointer;"
         "background:var(--panel-2);color:var(--text);border:1px solid var(--line);border-radius:6px}"
    ".ch-btn[disabled]{opacity:.35;cursor:default}"
    ".ch-clear{margin:0;padding:.3em .6em;font-size:.8em;line-height:1.15;background:#c0392b;color:#fff}"
        "@media (max-width:560px){.row2,.ch-fields{grid-template-columns:1fr}}"
    "</style></head><body>";

// Head for the AP-mode pages (web config lite and its /setup form). AP mode
// leaves ~20 KB of internal heap with a largest block near 13 KB, so this is
// deliberately a fraction of kHead's ~4 KB of CSS: no custom properties, no
// theme table, no layout grid. Everything here streams from flash, so nothing
// on the lite path ever needs a large contiguous allocation.
static const char kLiteHead[] =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Camillia &mdash; Web Config Lite</title>"
        "<link rel='icon' type='image/svg+xml' href='/favicon.ico'>"
        "<style>"
        "body{font-family:sans-serif;margin:0 auto;padding:1em;max-width:40em}"
        "h2{font-size:1.2em}"
        "h3{font-size:1em;margin:1.4em 0 .3em;padding-bottom:.2em;border-bottom:1px solid #999}"
        "p{font-size:.85em}"
        "label{display:block;margin:.5em 0 .1em;font-size:.9em}"
        "input,select{width:100%;box-sizing:border-box;padding:.4em;font-size:1em}"
        "input[type=checkbox]{width:auto}"
        "button{padding:.6em 1.2em;font-size:1em;margin-top:.6em}"
        "</style></head><body>";

// Region and preset <option> lists, shared by the full config page and web
// config lite so the two never drift. Streamed straight from flash in lite.
static const char kRegionOptions[] =
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
        "<option value='LORA_24'>LoRa 2.4 GHz (2400&ndash;2483.5 MHz)</option>";

static const char kPresetOptions[] =
        "<option value='Long Fast'>Long Fast (default)</option>"
        "<option value='Long Moderate'>Long Moderate</option>"
        "<option value='Long Slow'>Long Slow</option>"
        "<option value='Long Turbo'>Long Turbo</option>"
        "<option value='Medium Fast'>Medium Fast</option>"
        "<option value='Medium Slow'>Medium Slow</option>"
        "<option value='Short Fast'>Short Fast</option>"
        "<option value='Short Slow'>Short Slow</option>"
        "<option value='Short Turbo'>Short Turbo</option>"
        "<option value='Custom'>Custom&hellip;</option>";

// Custom modem settings. One constant serves both pages: the full page hides
// the block until Custom is selected (applyPreset() does it), while lite has no
// JS and simply always shows it — the fields are read only when the preset
// select says Custom, so a visible block is harmless there.
//
// The bandwidth list is what THIS board's radio can produce. On the LR1121
// pager 31.25 kHz is absent rather than offered-and-rejected: the LR1121 has no
// sub-GHz bandwidth below 62.5, and a setting the radio refuses would leave the
// device on the air at nothing anyone can hear.
static const char kCustomLoraFields[] =
        "<div id='cust-blk'>"
        "<div class='row2'><label>Bandwidth<select name='cbw' id='sel-cbw' onchange='applyPreset()'>"
#if LORA_BW_CODE_MIN <= 31
        "<option value='31'>31.25 kHz</option>"
#endif
        "<option value='62'>62.5 kHz</option>"
        "<option value='125'>125 kHz</option>"
        "<option value='250'>250 kHz</option>"
        "<option value='500'>500 kHz</option>"
        "</select></label>"
        "<label>Spreading Factor<select name='csf' id='sel-csf' onchange='applyPreset()'>"
        "<option value='7'>SF7</option><option value='8'>SF8</option>"
        "<option value='9'>SF9</option><option value='10'>SF10</option>"
        "<option value='11'>SF11</option><option value='12'>SF12</option>"
        "</select></label></div>"
        "<div class='row2'><label>Coding Rate<select name='ccr' id='sel-ccr' onchange='applyPreset()'>"
        "<option value='5'>4/5</option><option value='6'>4/6</option>"
        "<option value='7'>4/7</option><option value='8'>4/8</option>"
        "</select></label>"
        "<label>Frequency Slot<input name='cslot' id='in-cslot' type='number' min='0' max='255' "
        "onchange='applyPreset()'></label></div>"
        "<p class='gps-hint'>Custom modem settings, used when Modem Preset is Custom. "
        "Slot 0 derives the frequency from the primary channel name, the way a preset does; "
        "any other value pins the slot. Every node in your mesh has to match.</p>"
        "</div>";

// Theme preset names used by both the lite select and the swatch picker.
// Most families offer dark+light; Camillia Black is dark-only.
static const char *kThemePresetNames[] = {
    "Camillia Dark",        "Camillia Light",
    "Evergreen Dark",       "Evergreen Light",
    "Earthy Dark",          "Earthy Light",
    "Solarized Dark",       "Solarized Light",
    "Crimson Blue Dark",    "Crimson Blue Light",
    "Scarlet Pop Dark",     "Scarlet Pop Light",
    "Ink Wash Dark",        "Ink Wash Light",
    "Lavendar Fields Dark", "Lavendar Fields Light",
    "Wild Flowers Dark",    "Wild Flowers Light",
    "Quiet Luxury Dark",    "Quiet Luxury Light",
    "Morning Dew Dark",     "Morning Dew Light",
    "Winter Chill Dark",    "Winter Chill Light",
    "Camillia Black",
};
static const uint8_t kThemePresetCount =
    (uint8_t)(sizeof(kThemePresetNames) / sizeof(kThemePresetNames[0]));

static const char *kNotifyLedColorNames[] = {
    "Red", "Green", "Blue", "Yellow", "Cyan", "Magenta", "White", "Off"
};
static const uint8_t kNotifyLedColorCount =
    (uint8_t)(sizeof(kNotifyLedColorNames) / sizeof(kNotifyLedColorNames[0]));

// Widened back to 8 bits per channel for the browser. The stored value is
// RGB565, so this is lossy in the direction that already happened — round-trip
// it and you get the same 565 back, which is what makes the color inputs
// stable when a theme is reopened.
static uint32_t rgb565To888(uint16_t c) {
    const uint32_t r = (((c >> 11) & 0x1F) * 255U) / 31U;
    const uint32_t g = (((c >> 5) & 0x3F) * 255U) / 63U;
    const uint32_t b = ((c & 0x1F) * 255U) / 31U;
    return (r << 16) | (g << 8) | b;
}

static uint16_t rgb888To565(uint32_t rgb) {
    return (uint16_t)((((rgb >> 16) & 0xF8) << 8)
                    | (((rgb >> 8) & 0xFC) << 3)
                    | ((rgb & 0xFF) >> 3));
}

// Accepts "#RRGGBB" or "RRGGBB" — the color input posts the first, a person
// typing a hex code by hand may post either.
static bool parseHexColor(const String &s, uint16_t &out) {
    const char *p = s.c_str();
    if (*p == '#') p++;
    if (strlen(p) != 6) return false;
    uint32_t v = 0;
    for (int i = 0; i < 6; i++) {
        const char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = rgb888To565(v);
    return true;
}

// Custom themes continue the preset numbering: kThemePresetCount + slot, so a
// slot keeps its number whether or not the slots before it are filled. The
// picker only draws cards for slots that exist, so the gaps never show.
static constexpr uint8_t kThemePresetCustomBase = kThemePresetCount;
static constexpr uint8_t kThemePresetTotal =
    (uint8_t)(kThemePresetCount + UI_CUSTOM_THEME_SLOTS);

static uint8_t themePresetFromConfig(const RhinoConfig &cfg) {
    if (uiThemeIsCustom(cfg.uiTheme)) {
        return (uint8_t)(kThemePresetCustomBase + uiThemeCustomSlot(cfg.uiTheme));
    }

    uint8_t themeBase = (uint8_t)constrain((int)cfg.uiTheme, 0, UI_THEME_COUNT - 1);
    if (themeBase == UI_THEME_CAMELLIA_BLACK) {
        return (uint8_t)(kThemePresetCount - 1);
    }

    uint8_t mode = (cfg.uiMode == UI_MODE_LIGHT) ? UI_MODE_LIGHT : UI_MODE_DARK;
    return (uint8_t)constrain((int)(themeBase * 2 + mode), 0, (int)kThemePresetCount - 1);
}

static void themePresetToConfig(uint8_t preset, uint8_t &themeOut, uint8_t &modeOut) {
    preset = (uint8_t)constrain((int)preset, 0, (int)kThemePresetTotal - 1);
    if (preset >= kThemePresetCustomBase) {
        const int slot = preset - kThemePresetCustomBase;
        const UiCustomTheme *ct = uiCustomThemeGet(slot);
        if (ct) {
            themeOut = uiThemeFromCustomSlot(slot);
            modeOut = ct->mode;   // the theme carries its own mode
            return;
        }
        // Deleted between the page render and the save. Leave the config alone
        // rather than selecting a theme that is not there.
        return;
    }
    if (preset == (uint8_t)(kThemePresetCount - 1)) {
        themeOut = UI_THEME_CAMELLIA_BLACK;
        modeOut = UI_MODE_DARK;
        return;
    }

    themeOut = (uint8_t)constrain((int)(preset / 2), 0, UI_THEME_COUNT - 1);
    modeOut = (uint8_t)((preset & 1) ? UI_MODE_LIGHT : UI_MODE_DARK);
    if (uiThemeForcesDark(themeOut)) modeOut = UI_MODE_DARK;
}

// The remaining constants below are the config pane's fat static blobs, pulled
// out of the build-a-String path. Appending a multi-KB literal forces a
// contiguous allocation that big; in AP mode, where the largest free block is
// ~13 KB, that is what made the page fail to render. Streamed with
// sendContent_P they cost nothing, so the same pane serves both modes.

static const char kPresetJs[] =
        "<script>"
        // Region band edges {start,end MHz, power dBm} — mirror kRegions.
        "var R={'US':{s:902,e:928,p:22},'EU_433':{s:433,e:434,p:10},'EU_868':{s:869.4,e:869.65,p:22},"
        "'CN':{s:470,e:510,p:19},'JP':{s:920.5,e:923.5,p:13},'ANZ':{s:915,e:928,p:22},'ANZ_433':{s:433.05,e:434.79,p:14},"
        "'RU':{s:868.7,e:869.2,p:20},'KR':{s:920,e:923,p:22},'TW':{s:920,e:925,p:22},'IN':{s:865,e:867,p:22},"
        "'NZ_865':{s:864,e:868,p:22},'TH':{s:920,e:925,p:16},'UA_433':{s:433,e:434.7,p:10},'UA_868':{s:868,e:868.6,p:14},"
        "'MY_433':{s:433,e:435,p:20},'MY_919':{s:919,e:924,p:22},'SG_923':{s:917,e:925,p:20},'PH_433':{s:433,e:434.7,p:10},"
        "'PH_868':{s:868,e:869.4,p:14},'PH_915':{s:915,e:918,p:22},'KZ_433':{s:433.075,e:434.775,p:10},'KZ_863':{s:863,e:868,p:22},"
        "'NP_865':{s:865,e:868,p:22},'BR_902':{s:902,e:907.5,p:22},'LORA_24':{s:2400,e:2483.5,p:10}};"
        // Preset params {bw kHz, sf, cr, channel name for frequency-slot hash}.
        "var P={'Long Fast':{bw:250,sf:11,cr:5,cn:'LongFast'},'Long Moderate':{bw:125,sf:11,cr:8,cn:'LongMod'},"
        "'Long Slow':{bw:125,sf:12,cr:8,cn:'LongSlow'},'Long Turbo':{bw:500,sf:11,cr:8,cn:'LongTurbo'},"
        "'Medium Fast':{bw:250,sf:9,cr:5,cn:'MediumFast'},'Medium Slow':{bw:250,sf:10,cr:5,cn:'MediumSlow'},"
        "'Short Fast':{bw:250,sf:7,cr:5,cn:'ShortFast'},'Short Slow':{bw:250,sf:8,cr:5,cn:'ShortSlow'},'Short Turbo':{bw:500,sf:7,cr:5,cn:'ShortTurbo'}};"
        // djb2 hash + Meshtastic frequency-slot calc, matching regionSlotFreq().
        "function djb2(t){var h=5381;for(var i=0;i<t.length;i++)h=((h*33)+t.charCodeAt(i))>>>0;return h;}"
        "function nSlot(r,p){var n=Math.floor((r.e-r.s)/(p.bw/1000));return n<1?1:n;}"
        "function slotN(r,p,i){var bw=p.bw/1000;var n=nSlot(r,p);if(i>=n)i=n-1;return r.s+bw/2+i*bw;}"
        // Primary channel name, which is what a custom slot-0 setup hashes.
        // Saving with Custom selected renames a still-default channel 0 to
        // "Custom" (syncPrimaryChannelName), so a preset name in that field is
        // not what the device will end up hashing — predict the rename here or
        // the readout advertises a frequency the radio never tunes to.
        "function isDefName(n){if(!n||n=='Custom')return 1;"
          "for(var k in P)if(P[k].cn==n)return 1;return 0;}"
        "function cn0(){var e=document.querySelector('[name=ch0_name]');"
          "var n=e?e.value.trim():'';return isDefName(n)?'Custom':n;}"
        "function applyPreset(){"
          "var r=R[document.getElementById('sel-rgn').value];"
          "var pv=document.getElementById('sel-pst').value;"
          "var cust=(pv=='Custom');"
          "var cb=document.getElementById('cust-blk');"
          "if(cb)cb.style.display=cust?'':'none';"
          "var p,slot=0;"
          "if(cust){"
            "var c=+document.getElementById('sel-cbw').value;"
            "p={bw:c==31?31.25:c==62?62.5:c,sf:+document.getElementById('sel-csf').value,"
              "cr:+document.getElementById('sel-ccr').value,cn:cn0()};"
            "slot=+document.getElementById('in-cslot').value||0;"
          "}else p=P[pv];"
          "if(!r||!p)return;"
          "var n=nSlot(r,p);"
          "var si=slot>0?(slot>n?n:slot):(djb2(p.cn)%n)+1;"
          "var f=slotN(r,p,si-1);"
          "document.getElementById('d-freq').textContent=f.toFixed(3)+' MHz'"
            "+(cust?' (slot '+si+' of '+n+')':'');"
          "document.getElementById('d-bw').textContent=p.bw+' kHz';"
          "document.getElementById('d-sf').textContent='SF'+p.sf;"
          "document.getElementById('d-cr').textContent='4/'+p.cr;"
          "document.querySelector('[name=pwr]').value=r.p;"
        "}"
        "document.addEventListener('DOMContentLoaded',applyPreset);"
        "</script>";

static const char kLoraReadout[] =
        "<div class='row2' style='align-items:center'>"
        "<label>Frequency<span id='d-freq' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:8em;text-align:center'>—</span></label>"
        "<label>Bandwidth<span id='d-bw' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:6em;text-align:center'>—</span></label>"
        "</div><div class='row2' style='align-items:center'>"
        "<label>Spreading Factor<span id='d-sf' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:5em;text-align:center'>—</span></label>"
        "<label>Coding Rate<span id='d-cr' style='display:inline-block;padding:.25em .5em;"
        "background:var(--panel-2);color:var(--text);border-radius:4px;min-width:5em;text-align:center'>—</span></label>"
        "</div>";

static const char kFontModal[] =
        "<div class='modal-back' id='fsModal' onclick='if(event.target===this)fsClose()'>"
        "<div class='modal-card'><h3>Font Size</h3>"
        "<button type='button' class='modal-opt' onclick='fsPick(0,\"Small\")'>"
            "<b>Small</b><span>Compact chat &amp; DM text</span></button>"
        "<button type='button' class='modal-opt' onclick='fsPick(1,\"Medium\")'>"
            "<b>Medium</b><span>Default chat &amp; DM text</span></button>"
        "<button type='button' class='modal-opt' onclick='fsPick(2,\"Large\")'>"
            "<b>Large</b><span>Larger chat &amp; DM text</span></button>"
        "<button type='button' class='modal-opt' onclick='fsPick(3,\"Extra Large\")'>"
            "<b>Extra Large</b><span>Largest chat &amp; DM text</span></button>"
        "</div></div>"
        "<script>"
        "function fsMark(v){var m=document.querySelectorAll('#fsModal .modal-opt');"
        "for(var i=0;i<m.length;i++)m[i].classList.toggle('sel',i==v);}"
        "function fsOpen(){fsMark(+document.getElementById('fsInput').value);"
        "document.getElementById('fsModal').classList.add('open');}"
        "function fsClose(){document.getElementById('fsModal').classList.remove('open');}"
        "function fsPick(v,l){document.getElementById('fsInput').value=v;"
        "document.getElementById('fsBtn').textContent=l;fsClose();}"
        "</script>";

// Swatch picker: each theme preset renders as a clickable card showing a
// three-color preview (background, panel, accent). Cards are built in JS from
// the same palette table used for live preview, keyed by preset.
static const char kThemePicker[] =
        "<style>"
        ".theme-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));"
        "gap:8px;margin:.2em 0 .4em}"
        ".theme-card{display:flex;align-items:center;gap:8px;padding:7px 9px;cursor:pointer;"
        "border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--text)}"
        ".theme-card.sel{border-color:var(--accent);border-width:2px;padding:6px 8px}"
        ".theme-sws{display:flex;gap:3px;flex:0 0 auto}"
        ".theme-sw{width:16px;height:16px;border-radius:3px;border:1px solid rgba(0,0,0,.25)}"
        ".theme-name{font-size:.86em;line-height:1.15}"
        ".theme-filter{display:flex;align-items:center;gap:8px;margin:.2em 0 .1em}"
        ".theme-filter input{flex:1 1 auto;margin:0}"
        ".theme-filter span{font-size:.8em;color:var(--text-dim);white-space:nowrap}"
        // Custom cards carry two corner buttons, so they need room underneath
        // the row and a positioning context for them.
        ".theme-card.cust{position:relative;padding-bottom:15px}"
        ".theme-card.cust.sel{padding-bottom:14px}"
        ".theme-btn-cnr{position:absolute;bottom:2px;width:15px;height:15px;line-height:13px;"
        "text-align:center;border-radius:4px;font-size:11px;font-weight:700;cursor:pointer;"
        "border:1px solid rgba(0,0,0,.3);padding:0}"
        ".theme-del{right:3px;background:#c62839;color:#fff}"
        ".theme-edit{right:21px;background:var(--panel-2);color:var(--text)}"
        ".theme-card.new{justify-content:center;border-style:dashed;font-weight:600}"
        // The builder. A fixed overlay rather than a <dialog>, which older
        // mobile browsers on the far end of a device AP still do not have.
        ".tb-back{position:fixed;inset:0;background:rgba(0,0,0,.55);display:none;z-index:50}"
        ".tb-back.on{display:block}"
        ".tb-box{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);"
        "width:min(340px,92vw);max-height:92vh;overflow:auto;background:var(--panel);color:var(--text);"
        "border:1px solid var(--line);border-radius:10px;padding:14px}"
        ".tb-box h3{margin:0 0 .5em}"
        ".tb-row{display:flex;align-items:center;gap:8px;margin:.45em 0}"
        ".tb-row label{flex:1 1 auto;font-size:.88em;margin:0}"
        ".tb-row input[type=color]{width:38px;height:26px;padding:0;flex:0 0 auto}"
        ".tb-row input[type=text]{width:96px;flex:0 0 auto;font-family:monospace}"
        ".tb-modes{display:flex;gap:14px;align-items:center;margin:.5em 0}"
        ".tb-modes label{display:flex;align-items:center;gap:5px;font-size:.88em;margin:0}"
        ".tb-modes input{width:auto;margin:0}"
        ".tb-prev{border:1px solid var(--line);border-radius:8px;padding:9px;margin:.6em 0}"
        ".tb-prev-t{font-weight:700;font-size:.9em}"
        ".tb-prev-d{font-size:.78em;opacity:.85}"
        ".tb-prev-a{display:inline-block;margin-top:6px;padding:3px 9px;border-radius:5px;font-size:.78em}"
        ".tb-msg{font-size:.8em;min-height:1.1em;margin:.3em 0}"
        ".tb-acts{display:flex;gap:8px;justify-content:flex-end;margin-top:.7em}"
        "</style>"
        "<div class='theme-filter'>"
        "<input type='text' id='themeFilter' placeholder='Filter themes' autocomplete='off'>"
        "<span id='themeCount'></span></div>"
        "<div id='themeGrid' class='theme-grid'></div>"
        "<div class='tb-back' id='tbBack'><div class='tb-box'>"
        "<h3 id='tbTitle'>New Theme</h3>"
        "<div class='tb-row'><label for='tbName'>Name</label>"
        "<input type='text' id='tbName' maxlength='15' style='width:150px' placeholder='My Theme'></div>"
        "<div class='tb-row'><label for='tbBgP'>Background</label>"
        "<input type='color' id='tbBgP'><input type='text' id='tbBg' maxlength='7'></div>"
        "<div class='tb-row'><label for='tbPnP'>Panel</label>"
        "<input type='color' id='tbPnP'><input type='text' id='tbPn' maxlength='7'></div>"
        "<div class='tb-row'><label for='tbP2P'>Panel Alt</label>"
        "<input type='color' id='tbP2P'><input type='text' id='tbP2' maxlength='7'></div>"
        "<div class='tb-row'><label for='tbAcP'>Accent</label>"
        "<input type='color' id='tbAcP'><input type='text' id='tbAc' maxlength='7'></div>"
        "<div class='tb-modes'><span style='font-size:.88em'>Mode</span>"
        "<label><input type='radio' name='tbMode' value='0' checked>Dark</label>"
        "<label><input type='radio' name='tbMode' value='1'>Light</label></div>"
        "<div class='tb-prev' id='tbPrev'>"
        "<div class='tb-prev-t' id='tbPrevT'>Preview</div>"
        "<div class='tb-prev-d' id='tbPrevD'>Dim text reads like this</div>"
        "<span class='tb-prev-a' id='tbPrevA'>Accent</span></div>"
        "<div class='tb-row'><label for='tbCode'>Import code</label>"
        "<input type='text' id='tbCode' style='width:150px;font-family:monospace' placeholder='paste hex'>"
        "<button type='button' id='tbImport'>Load</button></div>"
        "<div class='tb-msg' id='tbMsg'></div>"
        "<div class='tb-acts'><button type='button' id='tbCancel'>Cancel</button>"
        "<button type='button' id='tbSave'>Save Theme</button></div>"
        "</div></div>"
        "<script>"
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
                                                "'23':{bg:'#f1f7fc',panel:'#ffffff',panel2:'#dfebf6',line:'#b6c9dd',text:'#22354a',dim:'#607891',accent:'#5c86b2',ink:'#ffffff'},"
                                                "'24':{bg:'#000000',panel:'#000000',panel2:'#0a0a0a',line:'#666666',text:'#f3f6fa',dim:'#b7c0cc',accent:'#ffffff',ink:'#080d14'}"
        "};"
        "var NAMES=['Camillia Dark','Camillia Light','Evergreen Dark','Evergreen Light',"
          "'Earthy Dark','Earthy Light','Solarized Dark','Solarized Light',"
          "'Crimson Blue Dark','Crimson Blue Light','Scarlet Pop Dark','Scarlet Pop Light',"
          "'Ink Wash Dark','Ink Wash Light','Lavendar Fields Dark','Lavendar Fields Light',"
          "'Wild Flowers Dark','Wild Flowers Light','Quiet Luxury Dark','Quiet Luxury Light',"
                    "'Morning Dew Dark','Morning Dew Light','Winter Chill Dark','Winter Chill Light',"
                    "'Camillia Black'];"
        "var input=document.getElementById('themeInput');"
        // Custom themes are authored as four colors; the rest of their web
        // palette is derived here the same way applyUiThemePalette() derives it
        // on the device — same blend ratios, same mode constants — so the page
        // preview matches what the panel will actually show.
        "function hx(c){c=c.replace('#','');return [parseInt(c.substr(0,2),16),"
          "parseInt(c.substr(2,2),16),parseInt(c.substr(4,2),16)];}"
        "function mix(a,b,t){var x=hx(a),y=hx(b),o='#';"
          "for(var i=0;i<3;i++){var v=Math.round(x[i]+(y[i]-x[i])*t/255);"
            "o+=('0'+Math.max(0,Math.min(255,v)).toString(16)).slice(-2);}return o;}"
        "function derive(t){var L=(t.m==1);return {bg:t.bg,panel:t.p,panel2:t.p2,"
          "line:mix(t.p2,t.bg,L?135:150),text:L?'#1e242c':'#f3f6fa',"
          "dim:L?'#5e6876':'#b7c0cc',accent:t.a,ink:L?'#ffffff':'#080d14'};}"
        "function pal(k){"
          "if(P[k])return P[k];"
          "var t=byKey(k);return t?derive(t):P['0'];}"
        "function byKey(k){var s=parseInt(k,10)-CUSTOM_BASE;"
          "for(var i=0;i<CUSTOM.length;i++)if(CUSTOM[i].s===s)return CUSTOM[i];return null;}"
        "function isLightKey(k){var t=byKey(k);"
          "if(t)return t.m==1;return (parseInt(k,10)%2)==1;}"
        "function apply(){"
          "var k=input.value;var p=pal(k);"
          "var r=document.documentElement.style;"
          "r.setProperty('--bg',p.bg);r.setProperty('--panel',p.panel);r.setProperty('--panel-2',p.panel2);"
          "r.setProperty('--line',p.line);r.setProperty('--text',p.text);r.setProperty('--text-dim',p.dim);"
          "r.setProperty('--accent',p.accent);r.setProperty('--accent-ink',p.ink);"
          "r.colorScheme=isLightKey(k)?'light':'dark';"
        "}"
        "function select(k){input.value=k;"
          "var cards=document.querySelectorAll('#themeGrid .theme-card');"
          "for(var i=0;i<cards.length;i++)cards[i].classList.toggle('sel',cards[i].dataset.k==k);"
          "apply();}"
        "var grid=document.getElementById('themeGrid');"
        "function card(k,name,cols,cust){"
          "var c=document.createElement('div');"
          "c.className='theme-card'+(cust?' cust':'');c.dataset.k=k;"
          "var sw=document.createElement('div');sw.className='theme-sws';"
          "cols.forEach(function(col){"
            "var b=document.createElement('div');b.className='theme-sw';b.style.background=col;sw.appendChild(b);});"
          "var nm=document.createElement('div');nm.className='theme-name';nm.textContent=name;"
          "c.appendChild(sw);c.appendChild(nm);"
          "c.addEventListener('click',function(){select(k);});"
          "return c;}"
        // Case-insensitive substring on the name, matching the on-device filter.
        "var fbox=document.getElementById('themeFilter'),fcount=document.getElementById('themeCount');"
        "function pass(name){var f=(fbox.value||'').trim().toLowerCase();"
          "return !f||String(name).toLowerCase().indexOf(f)>=0;}"
        "function build(){"
          "grid.innerHTML='';"
          "var shown=0,total=0;"
          "Object.keys(P).forEach(function(k){"
            "var p=P[k],nm=NAMES[k]||('Theme '+k);total++;"
            "if(!pass(nm))return;shown++;"
            "grid.appendChild(card(k,nm,[p.bg,p.panel2,p.accent],false));});"
          "CUSTOM.forEach(function(t){"
            "var k=String(CUSTOM_BASE+t.s);total++;"
            "if(!pass(t.n))return;shown++;"
            "var c=card(k,t.n,[t.bg,t.p2,t.a],true);"
            "var ed=document.createElement('button');ed.type='button';"
            "ed.className='theme-btn-cnr theme-edit';ed.textContent='\\u270E';"
            "ed.title='Edit theme';"
            "ed.addEventListener('click',function(e){e.stopPropagation();openBuilder(t);});"
            "var dl=document.createElement('button');dl.type='button';"
            "dl.className='theme-btn-cnr theme-del';dl.textContent='-';"
            "dl.title='Delete theme';"
            "dl.addEventListener('click',function(e){e.stopPropagation();del(t);});"
            "c.appendChild(ed);c.appendChild(dl);"
            "grid.appendChild(c);});"
          // +New is an action, not a theme, so it goes away while filtering
          // rather than sitting at the end of every result set.
          "if(CUSTOM.length<CUSTOM_SLOTS&&!(fbox.value||'').trim()){"
            "var n=document.createElement('div');n.className='theme-card new';"
            "n.textContent='+New';n.title='Build a custom theme';"
            "n.addEventListener('click',function(){openBuilder();});"
            "grid.appendChild(n);}"
          "fcount.textContent=(shown<total)?(shown+' of '+total):'';"
          "var cards=document.querySelectorAll('#themeGrid .theme-card');"
          "for(var i=0;i<cards.length;i++)cards[i].classList.toggle('sel',cards[i].dataset.k==input.value);"
        "}"
        "fbox.addEventListener('input',build);"
        // Enter in the filter would submit the config form; with one match left
        // it should pick that theme instead, which is what the on-device filter
        // does too.
        "fbox.addEventListener('keydown',function(e){if(e.key!=='Enter')return;"
          "e.preventDefault();"
          "var only=document.querySelectorAll('#themeGrid .theme-card');"
          "if(only.length===1&&only[0].dataset.k!==undefined)select(only[0].dataset.k);});"
        // ── Builder ──
        "var back=document.getElementById('tbBack'),msg=document.getElementById('tbMsg');"
        "var F={name:'tbName',bg:'tbBg',pn:'tbPn',p2:'tbP2',ac:'tbAc',code:'tbCode'};"
        "function g(id){return document.getElementById(id);}"
        "function pairs(){return [['tbBg','tbBgP'],['tbPn','tbPnP'],['tbP2','tbP2P'],['tbAc','tbAcP']];}"
        "function norm(v){v=(v||'').trim();if(v.charAt(0)!='#')v='#'+v;"
          "return /^#[0-9a-fA-F]{6}$/.test(v)?v.toLowerCase():null;}"
        "function mode(){var r=document.querySelector('input[name=tbMode]:checked');"
          "return r?parseInt(r.value,10):0;}"
        "function setMode(m){var r=document.querySelector('input[name=tbMode][value=\"'+m+'\"]');"
          "if(r)r.checked=true;}"
        "function preview(){"
          "var b=norm(g('tbBg').value),p=norm(g('tbPn').value),"
            "q=norm(g('tbP2').value),a=norm(g('tbAc').value);"
          "if(!b||!p||!q||!a)return;"
          "var d=derive({m:mode(),bg:b,p:p,p2:q,a:a});"
          "var box=g('tbPrev');box.style.background=d.panel;box.style.borderColor=d.line;"
          "g('tbPrevT').style.color=d.text;g('tbPrevD').style.color=d.dim;"
          "var ac=g('tbPrevA');ac.style.background=d.accent;ac.style.color=d.ink;}"
        "pairs().forEach(function(pr){"
          "var t=g(pr[0]),c=g(pr[1]);"
          "t.addEventListener('input',function(){var v=norm(t.value);if(v){c.value=v;preview();}});"
          "c.addEventListener('input',function(){t.value=c.value;preview();});});"
        "Array.prototype.forEach.call(document.querySelectorAll('input[name=tbMode]'),"
          "function(r){r.addEventListener('change',preview);});"
        "function fill(t){g('tbName').value=t.n;setMode(t.m);"
          "g('tbBg').value=t.bg;g('tbPn').value=t.p;g('tbP2').value=t.p2;g('tbAc').value=t.a;"
          "pairs().forEach(function(pr){var v=norm(g(pr[0]).value);if(v)g(pr[1]).value=v;});"
          "preview();}"
        // -1 = building a new theme, otherwise the slot being edited. Save posts
        // it back so an edit overwrites in place instead of consuming a slot.
        "var editSlot=-1;"
        "function openBuilder(t){msg.textContent='';g('tbCode').value='';"
          "editSlot=t?t.s:-1;"
          "g('tbTitle').textContent=t?('Edit '+t.n):'New Theme';"
          "fill(t||{n:'',m:0,bg:'#10141d',p:'#1a2230',p2:'#232d3e',a:'#d7869d'});"
          "back.classList.add('on');g('tbName').focus();}"
        "function closeBuilder(){back.classList.remove('on');}"
        "g('tbCancel').addEventListener('click',closeBuilder);"
        "back.addEventListener('click',function(e){if(e.target===back)closeBuilder();});"
        "function post(url,body,ok){"
          "var x=new XMLHttpRequest();x.open('POST',url,true);"
          "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
          "x.onreadystatechange=function(){if(x.readyState!=4)return;"
            "var j=null;try{j=JSON.parse(x.responseText);}catch(_){}"
            "if(x.status>=200&&x.status<300&&j&&j.ok)ok(j);"
            "else msg.textContent=(j&&j.error)?j.error:'Request failed.';};"
          "x.send(body);}"
        "function stored(j){"
          // Replace in place when the slot already had a theme, so an import
          // over an existing slot does not produce two cards for it.
          "var found=false;"
          "for(var i=0;i<CUSTOM.length;i++)if(CUSTOM[i].s===j.slot){CUSTOM[i]=j.theme;found=true;}"
          "if(!found)CUSTOM.push(j.theme);"
          "CUSTOM.sort(function(a,b){return a.s-b.s;});"
          "build();select(String(CUSTOM_BASE+j.slot));closeBuilder();}"
        "g('tbSave').addEventListener('click',function(){"
          "var nm=(g('tbName').value||'').trim();"
          "if(!nm){msg.textContent='Give the theme a name.';return;}"
          "var b=norm(g('tbBg').value),p=norm(g('tbPn').value),"
            "q=norm(g('tbP2').value),a=norm(g('tbAc').value);"
          "if(!b||!p||!q||!a){msg.textContent='Colors must be hex like #1a2230.';return;}"
          "msg.textContent='Saving...';"
          "post('/theme-save','name='+encodeURIComponent(nm)+'&mode='+mode()+"
            "'&bg='+encodeURIComponent(b)+'&panel='+encodeURIComponent(p)+"
            "'&panel2='+encodeURIComponent(q)+'&accent='+encodeURIComponent(a)+"
            "(editSlot>=0?('&slot='+editSlot):''),stored);});"
        "g('tbImport').addEventListener('click',function(){"
          "var c=(g('tbCode').value||'').trim();"
          "if(!c){msg.textContent='Paste a share code first.';return;}"
          "msg.textContent='Reading code...';"
          // Decoded on the device so the browser never reimplements the format.
          // preview=1 fills the form instead of storing, which lets someone
          // rename or adjust an imported theme before committing a slot.
          "post('/theme-save','preview=1&code='+encodeURIComponent(c),function(j){"
            "msg.textContent='Code loaded — Save Theme to keep it.';fill(j.theme);});});"
        "function del(t){"
          "if(!window.confirm('Delete the theme \"'+t.n+'\"? This cannot be undone.'))return;"
          "post('/theme-delete','slot='+t.s,function(j){"
            "CUSTOM=CUSTOM.filter(function(x){return x.s!==t.s;});"
            "if(parseInt(input.value,10)===CUSTOM_BASE+t.s)input.value='0';"
            "build();select(input.value);});}"
        "build();"
        "select(input.value);"
        "})();"
        "</script>";

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

// ── Shared page output helpers ────────────────────────────────

// Helper: flush a chunk of HTML and reset the buffer.
//
// Writes are skipped once the client is gone — a browser that navigated away or
// refreshed mid-render would otherwise leave us writing into a dead socket,
// blocking the main loop for the client timeout on every remaining chunk. The
// page keeps building (cheap, no network) and simply lands nowhere.
// Set when a response is abandoned mid-render; cleared at the start of each page.
static bool gSendAborted = false;

// Largest single write handed to the socket. Comfortably inside one TCP segment
// so a write can't outrun the space the writability check just observed.
static const size_t kFlashChunkBytes = 512;

// Waits (briefly) for the socket to accept data, and reports whether it will.
//
// This exists because WiFiClient::write() cannot be bounded from outside: it
// sends with MSG_DONTWAIT behind a select() with a hardcoded 1 s timeout and 10
// retries — a partial send re-arms them — and it ignores setTimeout(). When the
// response outgrows LWIP's TCP send buffer (TCP_SND_BUF, 5744 bytes by default)
// and ACKs stop arriving, a single sendContent() blocks the main loop for a full
// 10 s, taking the UI and radio down with it. Checking writability ourselves
// first means we never enter that loop unless the socket is ready.
// How long a single write may wait for the socket to accept anything before the
// response is written off. Was 250 ms, which the logs showed abandoning pages
// over chunks that merely took 307 ms — a slow link should give a slow page,
// not no page. A dead client is still caught immediately by connected(), and a
// reset arrives as an error from write(), so this only bounds genuine
// slowness.
static const uint32_t kWriteWindowMs = 1000;

static bool clientWritable(uint32_t timeoutMs) {
    const int sock = server.client().fd();
    if (sock < 0) return false;

    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(sock, &writable);
    struct timeval tv;
    tv.tv_sec  = (time_t)(timeoutMs / 1000);
    tv.tv_usec = (suseconds_t)((timeoutMs % 1000) * 1000);
    return select(sock + 1, nullptr, &writable, nullptr, &tv) > 0
           && FD_ISSET(sock, &writable);
}

// True when the response can't continue: client gone, or the socket has stopped
// draining. Either way the rest of the page is skipped — it keeps building (that
// part is cheap and needs no network) and simply lands nowhere.
static bool sendStalled() {
    if (gSendAborted) return true;
    if (!server.client().connected()) { gSendAborted = true; return true; }
    if (!clientWritable(kWriteWindowMs)) {
        Serial.println("[web] socket not draining — abandoning response");
        gSendAborted = true;
        return true;
    }
    return false;
}

// Writes every byte or gives up and marks the response aborted.
//
// This exists because WiFiClient::write() is allowed to write *less* than asked:
// it retries WIFI_CLIENT_MAX_WRITE_RETRY times and then returns however many
// bytes it managed. WebServer::sendContent() throws that return value away, and
// in chunked mode it has already announced the full length in the chunk header
// — so a short write leaves the browser reading the next chunk's size line as
// payload. The stream desyncs and the rest of the page renders as garbage
// (stray tag fragments and interleaved text), which is not obviously a network
// problem when you are looking at it.
//
// Looping on the return value is the whole fix. A write that cannot finish is
// then an abandoned response, which the page builder already copes with,
// instead of a corrupt one.
static bool writeAllRaw(const char *data, size_t len) {
    // Bound once: client() returns by value, and the copy shares the socket
    // through a refcounted handle. Fetching it per iteration would be correct
    // but pointless churn.
    WiFiClient c = server.client();
    size_t off = 0;
    while (off < len) {
        if (!c.connected()) return false;
        if (!clientWritable(kWriteWindowMs)) return false;
        const size_t n = c.write((const uint8_t *)(data + off), len - off);
        if (n == 0) return false;
        off += n;
    }
    return true;
}

// One chunked-transfer chunk, framed by hand so the size header can never
// disagree with what actually went out — and emitted as a SINGLE write.
//
// The single write matters as much as the framing. Sending the size header, the
// payload and the trailing CRLF as three writes makes three tiny TCP segments,
// and with Nagle enabled the second and third are held until the client ACKs
// the first. The client's delayed-ACK timer is ~100-200 ms, so the page went
// out at one ACK round-trip per 512 bytes — measured at 107-307 ms per chunk,
// which is both glacial and enough to trip the writability check and abandon
// the response. Coalescing costs one 512-byte memcpy and removes the round trip
// entirely.
//
// Static rather than stack: this is called for every chunk of every page, and
// half a KB of stack in a WebServer handler is not worth the risk. Single
// request at a time, so there is nothing to share it with.
static char sChunkFrame[kFlashChunkBytes + 24];

static bool sendChunkedRaw(const char *data, size_t len) {
    if (!len) return true;
    if (len > kFlashChunkBytes) return false;   // caller slices; never happens
    const int hn = snprintf(sChunkFrame, sizeof(sChunkFrame), "%x\r\n", (unsigned)len);
    if (hn <= 0) return false;
    memcpy(sChunkFrame + hn, data, len);
    sChunkFrame[hn + len]     = '\r';
    sChunkFrame[hn + len + 1] = '\n';
    return writeAllRaw(sChunkFrame, (size_t)hn + len + 2);
}

// Slices, frames and sends. Returns false once the response has been abandoned.
static bool sendSliced(const char *data, size_t len, const char *what) {
    // Nagle is the other half of the stall above: it holds a small write back
    // waiting for an ACK that the client is itself delaying. Off for the whole
    // response — every page here is a stream of modest chunks, which is exactly
    // the traffic pattern Nagle penalises. Idempotent, so setting it per slice
    // rather than plumbing a response-start hook costs a setsockopt per section.
    //
    // Guarded on connected(): setsockopt on a socket the peer has already closed
    // fails with EINVAL, and the core logs every failure at error level. On a
    // client that goes away mid-response that is one console line per slice,
    // which buries whatever else is being diagnosed. A dropped client also has
    // nothing to gain from TCP_NODELAY.
    if (server.client().connected()) server.client().setNoDelay(true);
    for (size_t off = 0; off < len; ) {
        if (sendStalled()) return false;
        const size_t n = (len - off > kFlashChunkBytes) ? kFlashChunkBytes
                                                        : (len - off);
        const uint32_t t0 = millis();
        const bool ok = sendChunkedRaw(data + off, n);
        const uint32_t dt = millis() - t0;
        if (dt > 100) Serial.printf("[web] slow %s: %u bytes in %u ms\n",
                                    what, (unsigned)n, (unsigned)dt);
        if (!ok) {
            // Half a chunk may already be on the wire, so the stream cannot be
            // trusted from here. Drop the socket rather than let the browser
            // parse the wreckage as content.
            Serial.printf("[web] short write in %s — dropping response\n", what);
            gSendAborted = true;
            server.client().stop();
            return false;
        }
        off += n;
        delay(1);   // let the TCP stack push data out and take ACKs
    }
    return true;
}

static void sendChunk(String &html) {
    const size_t len = html.length();
    if (!len) return;
    // Sliced for the same reason as sendFlash(): keep any single write inside
    // what the socket just said it could take.
    sendSliced(html.c_str(), len, "chunk");
    html = "";
}

// Same guards for the flash-resident constants, which bypass the String buffer.
//
// Sliced rather than handed over whole: several of these run to multiple KB, and
// a socket reporting "writable" only promises *some* space. Passing 7 KB to one
// write() lets it fill the send buffer and then spin in its own retry loop for
// 10 s — exactly the stall the writability check was meant to prevent. Writing
// in small pieces keeps that check meaningful, since it runs again before each.
static void sendFlash(const char *progmem) {
    // ESP32 maps flash into the address space, so these constants can be read
    // and written straight through — no _P copy step, and the same framed
    // writer as everything else.
    sendSliced(progmem, strlen(progmem), "flash chunk");
}

// Flush only once the buffer is worth a TCP segment. Chunk size is a balance
// with two failure modes at either end: one huge String needs a contiguous
// allocation a fragmented AP-mode heap can't provide, while a flush per table
// row means dozens of tiny chunked writes, and each one is a separate segment
// the synchronous WebServer blocks on. In AP mode, with LWIP down to ~15 KB,
// a pbuf allocation that fails mid-write wedges the main loop.
static void sendChunkIfBig(String &html, size_t threshold = 700) {
    if (html.length() >= threshold) sendChunk(html);
}

#if HAS_STATE_MAPS
// ── State map cache ──────────────────────────────────────────────────────────
// The Locate modal on the device draws a node's position onto a cached PNG of
// the state it falls in. This firmware has no TLS client, so it cannot fetch
// those maps itself — the browser, which already renders the Nodes heatmap from
// live OSM tiles, does the fetching and hands the finished image back here.
//
// This file owns the receiving half: what is already cached (/state-map-status)
// and where new ones land (/state-map-upload). The bounding boxes both ends
// work from come from state_maps.h so the two cannot drift apart.

// Compile-time: some boards have nowhere to put a file at all. Runtime: a board
// with a slot still needs a card in it. The Maps Download section is hidden
// outright when either is false, rather than shown disabled — there is nothing
// actionable to say to a browser about a card slot it cannot see.
static bool stateMapStorageReady() {
#if !HAS_FILE_STORAGE
    return false;
#else
    // sdBegin(), not storageBegin(). They are not interchangeable: sdBegin() is
    // the board-aware mount — it runs SPI.begin() with this board's LoRa pins and
    // deasserts the SD, LoRa and TFT chip selects before touching the card, which
    // is what makes a shared SPI bus safe, and it carries the per-board speed and
    // profile ladders. storageBegin() only calls SD.begin() against whatever state
    // the bus happens to be in, and tracks its own separate mounted flag.
    //
    // On a board where something had already brought the bus up, the difference
    // was invisible. On the M9 it was not. Every other storage call site in this
    // file and in main_lvgl.cpp already uses sdBegin(); this was the odd one out.
    //
    // Boards with no slot but internal flash are covered too: sdBegin() mounts
    // LittleFS for them, so callers never need to know which backend they have.
    return sdBegin();
#endif
}

static bool stateMapEnsureDir() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!stateMapStorageReady()) return false;
    if (!storageFs().exists("/camillia")) storageFs().mkdir("/camillia");
    if (!storageFs().exists("/camillia/state_maps")) storageFs().mkdir("/camillia/state_maps");
    return storageFs().exists("/camillia/state_maps");
#endif
}

// A cached map counts only when the PNG is real. A truncated upload leaves a
// file the device would reject at decode time anyway; reporting it as present
// would just move the confusion to the device.
// One directory pass, in place of fifty path probes.
//
// The status endpoint used to ask stateMapCached() about every state in the
// table. On a board backed by LittleFS each miss is a failed open that the VFS
// layer both charges for and logs, and fifty of them measured ~625 ms on the
// Heltec — a blocking stall in loop() every time the config page was opened,
// which is ~20 dropped UI frames and reads as exactly the lag issue #53 is
// about. Reading the directory costs one open regardless of how empty it is.
//
// Dimensions come off the directory entry's own handle, so a cached map costs no
// extra open either.
struct StateMapCacheEntry {
    char code[3];
    uint32_t w;
    uint32_t h;
};

static int stateMapScanCache(StateMapCacheEntry *out, int maxOut) {
#if !HAS_FILE_STORAGE
    (void)out; (void)maxOut;
    return 0;
#else
    if (!out || maxOut <= 0) return 0;
    if (!stateMapStorageReady()) return 0;

    File dir = storageFs().open("/camillia/state_maps");
    if (!dir) return 0;
    if (!dir.isDirectory()) { dir.close(); return 0; }

    int n = 0;
    for (File f = dir.openNextFile(); f && n < maxOut; f = dir.openNextFile()) {
        if (f.isDirectory()) { f.close(); continue; }

        // name() is a full path on some cores and a bare name on others.
        const char *nm = f.name();
        if (!nm) { f.close(); continue; }
        const char *base = nm;
        for (const char *p2 = nm; *p2; p2++) {
            if (*p2 == '/') base = p2 + 1;
        }

        // Exactly "XX.png"; anything else in the folder is not ours.
        auto lower = [](char c) -> char { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
        if (strlen(base) != 6
            || lower(base[2]) != '.' || lower(base[3]) != 'p'
            || lower(base[4]) != 'n' || lower(base[5]) != 'g') {
            f.close();
            continue;
        }

        uint8_t hdr[24] = {0};
        const bool readOk = (f.read(hdr, sizeof(hdr)) == sizeof(hdr));
        f.close();
        if (!readOk) continue;

        static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0) continue;
        if (!(hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R')) continue;

        out[n].code[0] = (char)toupper((unsigned char)base[0]);
        out[n].code[1] = (char)toupper((unsigned char)base[1]);
        out[n].code[2] = '\0';
        out[n].w = ((uint32_t)hdr[16] << 24) | ((uint32_t)hdr[17] << 16)
                 | ((uint32_t)hdr[18] << 8) | hdr[19];
        out[n].h = ((uint32_t)hdr[20] << 24) | ((uint32_t)hdr[21] << 16)
                 | ((uint32_t)hdr[22] << 8) | hdr[23];
        if (out[n].w == 0 || out[n].h == 0) continue;
        n++;
    }
    dir.close();
    return n;
#endif
}

// GET /state-map-status
// One object per state: the bounds to composite against, and whether it is
// already on the card. The browser intersects this with the node positions it
// already has (NODE_HEAT_POINTS) to decide what to download.
static void handleGetStateMapStatus() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    const bool ready = stateMapStorageReady();

    // Scanned once up front. Everything below answers from this, so the endpoint
    // touches the filesystem a fixed number of times instead of once per state.
    StateMapCacheEntry cached[kUsStateMapCount];
    const int cachedCount = ready ? stateMapScanCache(cached, kUsStateMapCount) : 0;

    // gSendAborted is sticky across requests: once any response gives up on a
    // stalled socket, every later chunked send is skipped until someone clears
    // it. Every chunked sender in this file has to open by resetting it — a
    // client that walked away mid-download would otherwise mute this endpoint
    // until the next reboot.
    gSendAborted = false;

    // Chunked rather than one String: 50 entries of bounds is a few KB, and this
    // file's rule is that nothing assembles a multi-KB contiguous buffer it can
    // avoid.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    String out;
    out.reserve(1024);
    out += "{\"storage\":";
    out += (ready ? "true" : "false");
    out += ",\"imageW\":";
    out += String(kStateMapImageW);
    out += ",\"imageH\":";
    out += String(kStateMapImageH);
    out += ",\"states\":[";

    for (int i = 0; i < kUsStateMapCount; i++) {
        const UsStateMapSpec &st = kUsStateMaps[i];
        if (i) out += ",";
        out += "{\"code\":\"";
        out += st.code;
        out += "\",\"name\":\"";
        out += st.name;
        out += "\",\"latMin\":";
        out += String(st.latMin, 4);
        out += ",\"latMax\":";
        out += String(st.latMax, 4);
        out += ",\"lonMin\":";
        out += String(st.lonMin, 4);
        out += ",\"lonMax\":";
        out += String(st.lonMax, 4);
        const StateMapCacheEntry *hit = nullptr;
        for (int c = 0; c < cachedCount; c++) {
            if (cached[c].code[0] == st.code[0] && cached[c].code[1] == st.code[1]) {
                hit = &cached[c];
                break;
            }
        }
        out += ",\"cached\":";
        out += hit ? "true" : "false";
        // A map cached at an older resolution still renders, just softly, so it
        // is reported rather than hidden: the page offers to refresh it instead
        // of making the user clear the whole cache to get a sharper one.
        if (hit) {
            out += ",\"w\":";
            out += String((unsigned)hit->w);
            out += ",\"h\":";
            out += String((unsigned)hit->h);
        }
        out += "}";
        sendChunkIfBig(out, 900);
    }

    out += "]}";
    sendChunk(out);
    server.sendContent("");
}

// POST /state-map-upload   (multipart, one state per request)
//
// The state code travels in the *filename* ("PA.png"), not a form field: the
// upload callback runs while the body is still streaming, long before
// server.arg() can see any field that follows the file part. The bounds do come
// from form fields, read in the completion handler below once the body is in.
//
// Bytes go straight to the file as they arrive. The /import handler above
// buffers into a static 8 KB array because a YAML config has to be parsed whole;
// a PNG does not, so this path holds one File handle and nothing else — cheaper
// than the upload that already ships.
static File   stateMapUploadFile;
static char   stateMapUploadCode[3] = "";
static bool   stateMapUploadOk = false;
static size_t stateMapUploadBytes = 0;

// Case-folded by hand rather than with strncasecmp(): that is POSIX, not C, and
// nothing else in this firmware relies on it being pulled in.
static const UsStateMapSpec *stateMapSpecForCode(const char *code) {
    if (!code || !code[0] || !code[1]) return nullptr;
    auto up = [](char c) -> char { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; };
    const char a = up(code[0]);
    const char b = up(code[1]);
    for (int i = 0; i < kUsStateMapCount; i++) {
        if (kUsStateMaps[i].code[0] == a && kUsStateMaps[i].code[1] == b) {
            return &kUsStateMaps[i];
        }
    }
    return nullptr;
}

static void handleStateMapUpload() {
#if !HAS_FILE_STORAGE
    return;
#else
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        stateMapUploadOk = false;
        stateMapUploadBytes = 0;
        stateMapUploadCode[0] = '\0';
        if (stateMapUploadFile) stateMapUploadFile.close();

        // Not logged in, no storage, or a filename that is not a state we know:
        // accept the body and drop it. Refusing mid-stream leaves the socket
        // holding a partial request the browser reports as a network error
        // rather than the refusal it is; the completion handler sends the real
        // status once the body is drained.
        if (!isLoggedIn() || !stateMapEnsureDir()) return;

        char code[3] = "";
        strncpy(code, upload.filename.c_str(), 2);
        code[2] = '\0';
        const UsStateMapSpec *spec = stateMapSpecForCode(code);
        if (!spec) return;

        strncpy(stateMapUploadCode, spec->code, 2);
        stateMapUploadCode[2] = '\0';

        // Written to its final name directly. A half-written file is caught by
        // the PNG sniff on the way out and removed, so a temp-and-rename dance
        // would buy nothing on a filesystem this simple.
        String path = nodesStateMapPath(stateMapUploadCode);
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        stateMapUploadFile = storageFs().open(path.c_str(), FILE_WRITE);
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (!stateMapUploadFile) return;   // keep draining the socket
        size_t wrote = stateMapUploadFile.write(upload.buf, upload.currentSize);
        if (wrote != upload.currentSize) {
            // Card full or gone. Stop writing but keep reading, same reason as
            // the refusals above.
            stateMapUploadFile.close();
            return;
        }
        stateMapUploadBytes += wrote;
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        if (!stateMapUploadFile) return;
        stateMapUploadFile.close();
        stateMapUploadOk = (stateMapUploadBytes > 0);
        return;
    }

    // UPLOAD_FILE_ABORTED
    if (stateMapUploadFile) stateMapUploadFile.close();
#endif
}

// POST /clear-maps
// Deletes every cached state map. Enumerated from kUsStateMaps rather than
// swept as a directory: this owns exactly the files it writes and has no
// business deleting whatever else a user keeps on their card. The completion
// markers go too, so a later cache-version check cannot decide a now-empty
// directory is a finished download.
static void handlePostClearMaps() {
#if !HAS_FILE_STORAGE
    server.send(503, "text/plain", "No storage on this board");
#else
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!stateMapStorageReady()) {
        redirectHomeWithFlash("No storage detected - nothing to clear");
        return;
    }

    int removed = 0;
    for (int i = 0; i < kUsStateMapCount; i++) {
        String png = nodesStateMapPath(kUsStateMaps[i].code);
        if (storageFs().exists(png.c_str()) && storageFs().remove(png.c_str())) removed++;
        String meta = nodesStateMapMetaPath(kUsStateMaps[i].code);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
    }
    if (storageFs().exists(kStateMapMarkerPath)) storageFs().remove(kStateMapMarkerPath);
    if (storageFs().exists(kStateMapLegacyMarkerPath)) storageFs().remove(kStateMapLegacyMarkerPath);

    char msg[64];
    snprintf(msg, sizeof(msg), "Cleared %d cached map%s", removed, removed == 1 ? "" : "s");
    redirectHomeWithFlash(msg);
#endif
}

static void handleStateMapUploadDone() {
#if !HAS_FILE_STORAGE
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"no storage on this board\"}");
#else
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
        return;
    }
    if (!stateMapUploadCode[0]) {
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"unknown or missing state code\"}");
        return;
    }

    String path = nodesStateMapPath(stateMapUploadCode);

    // The sniff is the whole validation. Anything that is not a PNG here is a
    // truncated or failed upload, and leaving it on the card would have the
    // device retry a decode that can never succeed.
    if (!stateMapUploadOk || !nodesFileLooksLikePng(path.c_str())) {
        if (storageFs().exists(path.c_str())) storageFs().remove(path.c_str());
        String meta = nodesStateMapMetaPath(stateMapUploadCode);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
        stateMapUploadCode[0] = '\0';
        server.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"upload was not a complete PNG\"}");
        return;
    }

    // Bounds are what the browser actually composited, which is not the state's
    // nominal box — tiles do not stop at state lines. Recording the real extent
    // is what puts the pin in the right place; without it the device falls back
    // to the nominal box and the position drifts by miles.
    const UsStateMapSpec *spec = stateMapSpecForCode(stateMapUploadCode);
    bool haveBounds = server.hasArg("latMin") && server.hasArg("latMax")
                      && server.hasArg("lonMin") && server.hasArg("lonMax");
    float latMin = spec ? spec->latMin : 0.0f;
    float latMax = spec ? spec->latMax : 0.0f;
    float lonMin = spec ? spec->lonMin : 0.0f;
    float lonMax = spec ? spec->lonMax : 0.0f;
    if (haveBounds) {
        latMin = server.arg("latMin").toFloat();
        latMax = server.arg("latMax").toFloat();
        lonMin = server.arg("lonMin").toFloat();
        lonMax = server.arg("lonMax").toFloat();
        // A degenerate or inverted box would divide by zero in the pin math.
        if (!(latMax > latMin) || !(lonMax > lonMin)) haveBounds = false;
    }
    if (haveBounds) {
        nodesWriteStateMapMeta(stateMapUploadCode, latMin, latMax, lonMin, lonMax);
    }

    char out[160];
    snprintf(out, sizeof(out),
             "{\"ok\":true,\"code\":\"%s\",\"bytes\":%u,\"bounds\":%s}",
             stateMapUploadCode,
             (unsigned)stateMapUploadBytes,
             haveBounds ? "true" : "false");
    stateMapUploadCode[0] = '\0';
    server.send(200, "application/json", out);
#endif
}
#endif  // HAS_STATE_MAPS

// Appends text into a single-quoted HTML attribute. Channel names are free text
// typed on the device or imported from YAML, and one apostrophe in a name would
// otherwise close the value= attribute and mangle the rest of the form — the
// name field would come back truncated on the next save.
static void appendAttr(String &html, const char *s) {
    if (!s) return;
    for (; *s; ++s) {
        switch (*s) {
            case '&':  html += "&amp;";  break;
            case '<':  html += "&lt;";   break;
            case '>':  html += "&gt;";   break;
            case '\'': html += "&#39;";  break;
            case '"':  html += "&quot;"; break;
            default:   html += *s;       break;
        }
    }
}

// Config-pane section wrappers. The full page folds each section into a
// <details> accordion; web config lite renders a plain heading with everything
// expanded — no folding, no styling, so nothing depends on CSS being there.
static void section(String &html, bool lite, const char *title, bool open) {
    const char *safeTitle = title ? title : "?";
    if (lite) {
        // Progress marker: if a render stalls, the last line names the section.
        Serial.printf("[web] lite: %s\n", safeTitle);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        char heapTag[80];
        snprintf(heapTag, sizeof(heapTag), "lite: %s", safeTitle);
        logWifiHeapDiag(heapTag);
#endif
        html += "<h3>";
        html += safeTitle;
        html += "</h3>";
        return;
    }
    html += open ? "<details open><summary>" : "<details><summary>";
    html += safeTitle;
    html += "</summary>";
}

static void sectionEnd(String &html, bool lite) {
    if (!lite) html += "</details>";
}

// ── Login page ────────────────────────────────────────────────

static void sendLoginPage(const char *err = "") {
    logWifiHeapDiag("serving login page");
    gSendAborted = false;

    // The head is a multi-KB flash constant; building it into a String needs a
    // contiguous DRAM block a fragmented heap often can't provide (the "broken
    // login" symptom). Always stream it via chunked transfer so no large
    // contiguous allocation is needed; only the small form body is built in
    // RAM. AP mode gets the lite head — kHead's ~4 KB of CSS is more than the
    // heap left after SoftAP comes up.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    sendFlash(webCfgUseLite() ? kLiteHead : kHead);
    String html;
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
    sendChunk(html);
    server.sendContent("");   // terminate chunked response
}

// ── Config page ───────────────────────────────────────────────

// Renders the config page in one of two modes. `lite` is the AP-mode variant
// ("web config lite"): same Config pane, same form, same /save handler, but the
// small CSS head and none of the other tabs — no node list, live feed, chat or
// map. Both modes stream every large constant straight from flash and flush the
// working String often, which is what lets the pane render inside AP mode's
// ~13 KB largest free block.
static void sendConfigPage(const char *msg = "", bool lite = false) {
    if (!gCfg) { server.send(500, "text/plain", "No config"); return; }
    logWifiHeapDiag(lite ? "serving config page (lite)" : "serving config page");

    // Use chunked transfer to avoid building one giant String
    gSendAborted = false;
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    // Bounds the socket's SO_RCVTIMEO/SO_SNDTIMEO. Note this does NOT bound how
    // long a chunk write can block: WiFiClient::write() runs its own select()
    // retry loop with a hardcoded 1 s timeout and ignores this value. It only
    // helps the read side.
    // Same guard as sendSliced(): a setsockopt on a closed socket is an error
    // line, not a timeout.
    if (server.client().connected()) server.client().setTimeout(3);

    char tmp[96];
    // Stream the head straight from flash rather than seeding the String with
    // it: kHead is ~4 KB and even kLiteHead is ~1 KB, and a contiguous
    // allocation that size is exactly what a fragmented heap can't provide.
    sendFlash(lite ? kLiteHead : kHead);
    String html;
    uint8_t themePreset = themePresetFromConfig(*gCfg);
    // Zero in lite mode: the node loops below accumulate tens of KB of options
    // and detail panels, which AP mode has no room for and lite never shows.
    int totalNodes = lite ? 0 : Nodes.count();
    uint8_t battPct = readBatteryPctWeb();
    const char *battCls = (battPct >= 60) ? "metric-good" : ((battPct >= 25) ? "metric-warn" : "metric-bad");
    bool gpsEn = gpsIsEnabled();
    bool gpsFix = gpsHasFix();
    bool gpsNmea = gpsHasNmeaStream();
    uint8_t gpsSat = gpsSats();
    const char *gpsCls = !gpsEn ? "metric-bad" : (gpsFix ? "metric-good" : (gpsNmea ? "metric-warn" : "metric-bad"));
    char battChip[24];
    // battCls above stays percentage-derived in both modes: the chip's color is
    // a state-of-charge signal, same as the on-device header dot.
    if (gCfg && gCfg->battDisplayMode == BATT_DISPLAY_VOLTAGE) {
        snprintf(battChip, sizeof(battChip), "BAT %.2fV", (double)batteryReadVoltage());
    } else {
        snprintf(battChip, sizeof(battChip), "BAT %u%%", (unsigned)battPct);
    }
    char gpsChip[48];
    if (!gpsEn) snprintf(gpsChip, sizeof(gpsChip), "GPS OFF");
    else if (gpsFix) snprintf(gpsChip, sizeof(gpsChip), "GPS FIX %u", (unsigned)gpsSat);
    else if (!gpsNmea) snprintf(gpsChip, sizeof(gpsChip), "GPS NO DATA");
    else snprintf(gpsChip, sizeof(gpsChip), "GPS SEARCH %u", (unsigned)gpsSat);
    int mapPointCount = 0;
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
    // Only the map-point count is needed up front (for the header); the
    // option/detail/point lists are streamed at the emit points below.
    for (int i = 0; i < totalNodes; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n) continue;
        float lat = 0.0f, lon = 0.0f;
        if (extractNodeCoords(n, lat, lon)) mapPointCount++;
    }

    // Stream the "Nodes Seen" <option> list directly to the client, batching
    // into ~1 KB chunks so no large contiguous String is ever allocated.
    //
    // This used to be the no-PSRAM path only; boards with PSRAM accumulated the
    // whole list into Strings and appended them to the page at the end. That
    // does not survive a full node table: at 250 nodes the detail panels alone
    // come to ~114 KB, and `html += nodeDetails` then asks for a ~126 KB
    // contiguous String. The grow does not produce usable memory at that size —
    // the String reported length 126863 with only 12791 real bytes and the rest
    // reading as zeros, which went out as ~30 KB of NUL bytes and a page the
    // browser rendered as garbage. Streaming has no such ceiling, so every
    // board now uses it and the accumulate path is gone.
    auto streamNodeOptions = [&]() {
        String buf;
        // Rank order already puts favorites first (see cmpNodes in node_db.cpp),
        // so the boundary is simply the first non-favorite after at least one
        // favorite — no second pass, and nothing to keep in sync if the sort
        // keys change. A disabled <option> is the one separator every browser
        // renders the same way; <option> borders are not stylable.
        bool sawFavorite = false;
        bool sawDivider  = false;
        for (int i = 0; i < totalNodes; i++) {
            NodeEntry *n = Nodes.getByRank(i);
            if (!n) continue;
            if (n->favorite) {
                sawFavorite = true;
            } else if (sawFavorite && !sawDivider) {
                buf += "<option disabled data-fav-divider='1'>"
                       "&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;"
                       "&mdash;&mdash;&mdash;&mdash;</option>";
                sawDivider = true;
            }
            const char *shortName = n->shortName[0] ? n->shortName : "----";
            const char *longName  = n->longName[0]  ? n->longName  : "(unnamed)";
            char optId[16];
            snprintf(optId, sizeof(optId), "!%08X", n->nodeId);
            // The value stays the rank index (it is what selectNode() turns into
            // an nd-<rank> lookup); the id and flag are what lets the favorite
            // toggle re-sort this list in place without touching the values.
            buf += "<option value='";
            buf += String(i);
            buf += "' data-nid='";
            buf += optId;
            buf += "' data-fav='";
            buf += n->favorite ? "1" : "0";
            buf += "'>";
            buf += shortName;
            buf += " -- ";
            buf += longName;
            buf += "</option>";
            if (buf.length() > 1024) sendChunk(buf);
        }
        if (buf.length()) sendChunk(buf);
    };

    // Stream the JSON array of map points ([{lat,lon},...]) in ~1 KB chunks.
    auto streamMapPoints = [&]() {
        String buf = "[";
        int cnt = 0;
        char t[64];
        for (int i = 0; i < totalNodes; i++) {
            NodeEntry *n = Nodes.getByRank(i);
            if (!n) continue;
            float lat = 0.0f, lon = 0.0f;
            if (!extractNodeCoords(n, lat, lon)) continue;
            if (cnt > 0) buf += ",";
            snprintf(t, sizeof(t), "{\"lat\":%.7f,\"lon\":%.7f}", lat, lon);
            buf += t;
            cnt++;
            if (buf.length() > 1024) sendChunk(buf);
        }
        buf += "]";
        sendChunk(buf);
    };

    // Stream the per-node detail panels in ~1 KB chunks. Mirrors the PSRAM
    // accumulation path below; keep the two in sync if you change the markup.
    auto streamNodeDetails = [&]() {
        String buf;
        char tmp[96];
        for (int i = 0; i < totalNodes; i++) {
            NodeEntry *n = Nodes.getByRank(i);
            if (!n) continue;

            float lat = 0.0f, lon = 0.0f;
            bool hasLocation = extractNodeCoords(n, lat, lon);

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

            char latAttr[24], lonAttr[24];
            snprintf(latAttr, sizeof(latAttr), "%.7f", lat);
            snprintf(lonAttr, sizeof(lonAttr), "%.7f", lon);
            buf += "<div class='node-detail' id='nd-";
            buf += String(i);
            buf += "' data-loc='";
            buf += hasLocation ? "1" : "0";
            buf += "' data-lat='";
            buf += latAttr;
            buf += "' data-lon='";
            buf += lonAttr;
            buf += "'>";
            buf += "<div class='node-card'>";
            buf += "<div class='node-head'>";
            buf += "<div class='node-title'>";
            buf += shortName;
            buf += " -- ";
            buf += longName;
            // Says why this node sorted to the top of the list, and — more to
            // the point — that it is exempt from eviction and from the
            // keep-favorites clear.
            //
            // Always emitted, shown or hidden by style: the toggle below flips
            // this without a page rebuild, and toggling a display is a great
            // deal less fragile than inserting an element into a title whose
            // text came from another node's name.
            buf += "<span class='fav-tag' data-fav-tag='";
            buf += idBuf;
            buf += "'";
            if (!n->favorite) buf += " style='display:none'";
            buf += ">(favorite)</span>";
            buf += "</div>";   // .node-title
            buf += "<button type='button' class='chat-mini fav-btn' data-fav-btn='";
            buf += idBuf;
            buf += "' data-fav='";
            buf += n->favorite ? "1" : "0";
            buf += "' onclick='toggleNodeFavorite(this)'>";
            buf += n->favorite ? "Unfavorite" : "Favorite";
            buf += "</button>";
            buf += "</div>";   // .node-head
            buf += "<div class='node-meta'><b>ID:</b> ";
            buf += idBuf;
            buf += "  <b>Channel:</b> ";
            buf += chanName;
            buf += "  <b>Last Heard:</b> ";
            buf += heardBuf;
            buf += "</div>";
            buf += "<div class='node-meta'><b>Location:</b> ";
            buf += locBuf;
            buf += "</div>";
            buf += "<div class='node-meta'><b>Position Pkt:</b> ";
            buf += posSeenBuf;
            buf += "  <b>State:</b> ";
            buf += posStateBuf;
            buf += "  <b>Raw:</b> ";
            buf += rawPosBuf;
            buf += "</div>";
            buf += "<div class='node-meta'><b>Telemetry:</b> ";
            buf += telemBuf;
            buf += "  <b>Link:</b> ";
            buf += linkBuf;
            buf += "</div>";
            buf += "</div>";   // .node-card
            buf += "</div>";   // .node-detail
            if (buf.length() > 1024) sendChunk(buf);
        }
        if (buf.length()) sendChunk(buf);
    };
    // Everything above this point is the node list: on the full page it walks
    // every known node and builds the dropdown and detail panels in RAM before
    // a single byte goes out. If a render stalls between "serving config page"
    // and here, it is that build, not the socket.
    if (!lite) logWifiHeapDiag("node list built");

    html += lite ? "<h2>Camillia &mdash; Web Config Lite"
                 : "<h2>Camillia for Meshtastic";
    if (gCfg && gCfg->webCfgAuthEnabled)
        html += " <a class='logout' href='/logout'>Logout</a>";
    html += "</h2>";

    if (msg[0]) { html += "<p class='msg'>"; html += msg; html += "</p>"; }

    // This board freed its chat buffers so Wi-Fi could start, so the device is
    // dropping messages for as long as this page is being served. Say so
    // prominently — it is not recoverable by anything on this page.
    if (gWebBuffersReclaimed) {
        html += "<p style='color:#d81f2a;font-weight:700;border:1px solid #d81f2a;"
                "border-radius:4px;padding:.5em;margin:.5em 0'>"
                "Chat is paused. This device needed its message memory to run "
                "Wi-Fi, so messages sent to it are not being received or stored "
                "while web config is on. Turn web config off on the device to "
                "resume messaging.</p>";
    }

    if (lite) {
        html += "<p>Access-point mode. All settings are here; the live feed, "
                "chat and node map need the device on your WiFi network. "
                "<a href='/setup'>Quick WiFi setup</a></p>";
    } else {
        html += "<div class='tab-row'><div class='tab-btns'>"
            "<button type='button' class='tab-btn active' id='tab-btn-config' onclick=\"switchTab('config')\">Config</button>"
            "<button type='button' class='tab-btn' id='tab-btn-wifi' onclick=\"switchTab('wifi')\">WiFi</button>"
            "<button type='button' class='tab-btn' id='tab-btn-utils' onclick=\"switchTab('utils')\">Utilities</button>"
            "<button type='button' class='tab-btn' id='tab-btn-live' onclick=\"switchTab('live')\">Live</button>"
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
            "<button type='button' class='tab-btn' id='tab-btn-chat' onclick=\"switchTab('chat')\">Chat</button>"
#endif
            "<button type='button' class='tab-btn' id='tab-btn-map' onclick=\"switchTab('map')\">Nodes</button>";
#if HAS_VNC_HOST
    html += "<button type='button' class='tab-btn' id='tab-btn-remote' onclick=\"switchTab('remote')\">Remote</button>";
#endif
        html += "</div><div class='tab-metrics'><span class='metric-chip ";
        html += battCls;
        html += "'>";
        html += battChip;
        html += "</span><span class='metric-chip ";
        html += gpsCls;
        html += "'>";
        html += gpsChip;
        html += "</span></div></div>";
        html += "<div class='tab-panel active' id='tab-config'><div class='tab-pane-center'>";
    }
    sendChunk(html);

    html += "<form method='POST' action='/save'>";

    // ── Node Identity ─────────────────────────────────────────
    section(html, lite, "Node Identity", true);
    html += "<label>Long Name (max 39 chars)"
            "<input name='long' type='text' maxlength='39' value='";
    html += gCfg->nodeLong; html += "'></label>";
    html += "<label>Short Name (max 4 chars)"
            "<input name='short' type='text' maxlength='4' value='";
    html += gCfg->nodeShort; html += "'></label>";
    // Node ID override (developer option — hidden for end users)
    // snprintf(tmp, sizeof(tmp), "%08lx", (unsigned long)gCfg->nodeIdOverride);
    // html += "<label>Node ID Override ...";
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Device ────────────────────────────────────────────────
    section(html, lite, "Device", false);
    html += "<div class='row2'>";
    html += "<label>Role<select name='role'>";
    // Only client roles are offered; values are the canonical Meshtastic enum
    // positions (kept intact for wire compatibility and rebroadcast gating).
    static const struct { uint8_t v; const char *l; } kRoles[] = {
        {0,"CLIENT"},{1,"CLIENT_MUTE"},{8,"CLIENT_HIDDEN"}
    };
    for (int i = 0; i < (int)(sizeof(kRoles) / sizeof(kRoles[0])); i++) {
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
    // GPS Broadcast Interval lives in the Position section, next to the poll
    // interval it is easily confused with.
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->nodeInfoIntervalS);
    html += "<label>NodeInfo Interval (s)<input name='nodeinfo_intv' type='number' min='60' value='";
    html += tmp; html += "'></label>";
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
            sendChunkIfBig(html);
        }
        html += "</select></label>";
    }
    html += "<div class='row2'>";
    html += "<label>NTP Server<input name='ntp_server' type='text' maxlength='47' value='";
    html += gCfg->ntpServer;
    html += "' placeholder='pool.ntp.org'></label>";
    html += "</div>";

#if HAS_NODE_LOS
    // Elevation endpoint for Node Actions -> LOS. Plain http:// only, and the
    // help text says so rather than leaving the operator to discover it as a
    // connection failure: this firmware carries no TLS client, so an https://
    // address cannot be reached at all.
    html += "<div class='row2'>";
    html += "<label>Elevation Server (LOS)<input name='los_elev' type='text' maxlength='95' value='";
    html += gCfg->losElevServer;
    html += "' placeholder='http://host:port'></label>";
    html += "</div>";
    html += "<p class='hint'>Terrain line-of-sight fetches ground elevation from "
            "<code>&lt;server&gt;/elev?locations=lat,lon|...</code>, expecting a CSV of "
            "metres. Must be <b>http://</b> — there is no TLS client in this firmware. "
            "Leave empty to disable LOS.</p>";
#endif

    // ── Time and Date ─────────────────────────────────────────
    // Automatic means NTP when there's a network path and GPS otherwise. Manual
    // stops both from touching the clock and uses the fields below, which are
    // read as local time in the timezone selected above.
    {
        const bool manual = (gCfg->timeSource == TIME_SOURCE_MANUAL);
        html += "<label>Time Source<select name='time_source' id='time_source'>";
        html += "<option value='0'";
        if (!manual) html += " selected";
        html += ">Automatic (Internet / GPS)</option>";
        html += "<option value='1'";
        if (manual) html += " selected";
        html += ">Manual</option></select></label>";

        // Seeded from the device clock so the fields open on something sane.
        char dbuf[16] = "";
        char tbuf[8] = "";
        time_t nowSec = time(nullptr);
        if (nowSec >= 1700000000) {
            struct tm lt;
            localtime_r(&nowSec, &lt);
            strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &lt);
            strftime(tbuf, sizeof(tbuf), "%H:%M", &lt);
        }

        html += "<div id='manual_row' class='row2'";
        if (!manual) html += " style='display:none'";
        html += ">";
        html += "<label>Date<input name='manual_date' type='date' value='";
        html += dbuf;
        html += "'></label>";
        html += "<label>Time (24h)<input name='manual_time' type='time' value='";
        html += tbuf;
        html += "'></label></div>";
        sendChunkIfBig(html);   // AP mode renders this with very little heap
        html += "<p class='gps-hint' id='manual_hint'";
        if (!manual) html += " style='display:none'";
        html += ">Saving applies this time to the device clock. "
                "The clock is not battery-backed, so it restarts unset after a power cycle.</p>";
        html += "<p class='gps-hint'><button type='button' id='browser_time'>Use this browser's time"
                "</button></p>";
        html += "<script>"
                "(function(){"
                "var s=document.getElementById('time_source');"
                "var m=document.getElementById('manual_row');"
                "var h=document.getElementById('manual_hint');"
                "function u(){var on=s.value=='1';m.style.display=on?'':'none';"
                "h.style.display=on?'':'none';}"
                "s.addEventListener('change',u);u();"
                "document.getElementById('browser_time').addEventListener('click',function(){"
                "var n=new Date(),p=function(v){return(v<10?'0':'')+v;};"
                "document.getElementsByName('manual_date')[0].value="
                "n.getFullYear()+'-'+p(n.getMonth()+1)+'-'+p(n.getDate());"
                "document.getElementsByName('manual_time')[0].value="
                "p(n.getHours())+':'+p(n.getMinutes());"
                "s.value='1';u();});"
                "})();"
                "</script>";
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Position ──────────────────────────────────────────────
    section(html, lite, "Position", false);
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='shareLocation' value='1'";
    if (gCfg->shareLocation) html += " checked";
    html += "> Share Location (broadcast position to the mesh)</label>";
    html += "<p class='gps-hint'>Off suppresses every position transmission, including "
            "the manual announce, whatever the settings below say. Nothing else on this "
            "page stops the broadcast: GPS Enabled only chooses where the coordinates "
            "come from.</p>";
    html += "<label>Location Precision<select name='pos_precision'>";
    for (int i = 0; i < kPositionPrecisionCount; i++) {
        snprintf(tmp, sizeof(tmp), "%u", (unsigned)kPositionPrecisions[i].bits);
        html += "<option value='"; html += tmp; html += "'";
        if (gCfg->positionPrecision == kPositionPrecisions[i].bits) html += " selected";
        html += ">"; html += kPositionPrecisions[i].label; html += "</option>";
    }
    html += "</select></label>";
    html += "<p class='gps-hint'>Rounds the coordinates before they are transmitted, so "
            "the mesh sees the area you are in rather than the spot you are standing on. "
            "The device still uses your exact position locally. Receiving nodes are told "
            "how coarse the value is, so other Meshtastic clients can show it as an area. "
            "Applies to every channel this node shares position on.</p>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='gpsEnabled' value='1'";
    if (gCfg->gpsEnabled) html += " checked";
    html += "> GPS Enabled (L76K hardware GPS)</label>";
    html += "<p class='gps-hint'>When GPS is enabled, position is sourced from the GPS module. "
            "The manual coordinates below are used as fallback until a fix is acquired.</p>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->gpsPollIntervalS);
    html += "<label>GPS Poll Interval (s)<input name='gps_poll_s' type='number' min='0' max='3600' step='1' value='";
    html += tmp; html += "'></label>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->posIntervalS);
    html += "<label>GPS Broadcast Interval (s)<input name='pos_intv' type='number' min='60' value='";
    html += tmp; html += "'></label></div>";
    html += "<p class='gps-hint'>Poll is how often this device reads its own GPS "
            "(0 = every loop). Broadcast is how often that position is sent to "
            "the mesh.</p>";
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
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Channels ──────────────────────────────────────────────
    section(html, lite, "Channels", false);
    html += "<p class='gps-hint'>Key: base64 (e.g. \"AQ==\" or \"MA==\"). "
            "Hash is recomputed automatically on save. "
            "Uplink publishes this channel's traffic to MQTT; downlink re-broadcasts "
            "MQTT traffic for it onto LoRa. Location broadcasts this node's position "
            "on the channel &mdash; only where Share Location above is on, and never "
            "on an unnamed or disabled channel.</p>";
    // Marker so the POST handler applies per-channel checkbox state (an unchecked
    // box submits nothing) only when this section was rendered.
    html += "<input type='hidden' name='ch_flags' value='1'>";
    if (!lite) {
        html += "<p class='gps-hint'>Use &#9650;/&#9660; to move a channel between "
                "slots. Channel 0 is the primary and stays where it is.</p>";
    }
    html += "<div id='ch-list'>";
    char b64buf[48];
    // Channel roles, not the device role list kRoles above — same function scope.
    static const char *kChanRoles[] = {"PRIMARY", "SECONDARY", "DISABLED"};
    // Field name, label, and current state for the three per-channel toggles, so
    // the three near-identical blocks this replaced cannot drift apart.
    struct ChanToggle { const char *suffix; const char *label; bool on; };
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ch = CHANNEL_KEYS[i];
        base64Encode(ch.key, ch.keyLen, b64buf);
        html += "<div class='ch-row'>";

        // Header: slot number, reorder controls, Clear.
        snprintf(tmp, sizeof(tmp), "<div class='ch-hd'><span class='ch-no'>Channel %d</span>", i);
        html += tmp;
        if (!lite) {
            // Slot 0 is the primary and never moves, so its buttons are present
            // but disabled — dropping them would leave its header a different
            // shape from the others. Slot 1 cannot move up into 0, and the
            // last slot cannot move down: those limits are fixed by position, so
            // they are baked in here rather than recomputed after every swap.
            const bool canUp   = (i > 1);
            const bool canDown = (i > 0 && i < MESH_CHANNELS - 1);
            html += "<button type='button' class='ch-btn' data-mv='-1' title='Move up'";
            if (!canUp) html += " disabled";
            html += ">&#9650;</button>";
            html += "<button type='button' class='ch-btn' data-mv='1' title='Move down'";
            if (!canDown) html += " disabled";
            html += ">&#9660;</button>";
        }
        // Clear stays a self-contained inline handler: web config lite ships no
        // script at all, and this button works there too. It now unchecks the
        // toggles instead of blanking their value attribute — that left the box
        // ticked while submitting an empty value, which the POST handler read as
        // off, so the form and the device disagreed.
        html += "<button type='button' class='ch-clear' onclick=\""
                "this.closest('.ch-row').querySelectorAll('input').forEach(function(el){"
                "if(el.type=='checkbox')el.checked=false;else el.value='';})\">Clear</button></div>";

        // Name / Key / Role
        html += "<div class='ch-fields'>";
        snprintf(tmp, sizeof(tmp), "ch%d_name", i);
        html += "<label>Name<input name='"; html += tmp;
        html += "' type='text' maxlength='11' value='";
        appendAttr(html, ch.name);
        html += "'></label>";
        snprintf(tmp, sizeof(tmp), "ch%d_key", i);
        html += "<label>Key<input name='"; html += tmp;
        html += "' type='text' value='"; html += b64buf; html += "'></label>";
        snprintf(tmp, sizeof(tmp), "ch%d_role", i);
        html += "<label>Role<select name='"; html += tmp; html += "'>";
        for (int r = 0; r < 3; r++) {
            snprintf(tmp, sizeof(tmp), "<option value='%d'%s>%s</option>",
                     r, (ch.role == r) ? " selected" : "", kChanRoles[r]);
            html += tmp;
        }
        html += "</select></label>";

        // Hop limit: a select rather than a number box so "Default" is a value
        // you pick, not an empty field you have to know to leave alone. The
        // reorder script swaps `input,select` values between rows, so this moves
        // with its channel like everything else here.
        snprintf(tmp, sizeof(tmp), "ch%d_hop", i);
        html += "<label>Hops<select name='"; html += tmp; html += "'>";
        {
            char optbuf[96];
            snprintf(optbuf, sizeof(optbuf), "<option value=''%s>Default (%d)</option>",
                     chanHopLimitSet(ch.hopLimitPlus1) ? "" : " selected",
                     (int)gCfg->loraHopLimit);
            html += optbuf;
            for (int h = 0; h <= 7; h++) {
                const bool sel = chanHopLimitSet(ch.hopLimitPlus1)
                                 && chanHopLimitGet(ch.hopLimitPlus1) == (uint8_t)h;
                snprintf(optbuf, sizeof(optbuf), "<option value='%d'%s>%d%s</option>",
                         h, sel ? " selected" : "", h,
                         (h == 0) ? " (direct only)" : "");
                html += optbuf;
            }
        }
        html += "</select></label>";
        html += "</div>";

        // Toggles
        const ChanToggle toggles[] = {
            { "up",   "Uplink",   ch.uplinkEnabled },
            { "down", "Downlink", ch.downlinkEnabled },
            { "loc",  "Location", ch.shareLocation },
        };
        html += "<div class='ch-tog'>";
        for (const ChanToggle &t : toggles) {
            snprintf(tmp, sizeof(tmp), "<label><input type='checkbox' name='ch%d_%s' value='1'%s>%s</label>",
                     i, t.suffix, t.on ? " checked" : "", t.label);
            html += tmp;
        }
        html += "</div></div>";
        sendChunkIfBig(html);
    }
    html += "</div>";
    if (!lite) {
        // Reordering swaps the two rows' field values rather than moving the rows
        // themselves: the slot number a channel occupies is what the mesh sees,
        // so the headers have to stay put and the definitions move between them.
        // That also keeps every ch<N>_* input name bound to its own slot, so the
        // POST handler needs no idea any of this happened.
        html += "<script>"
                "(function(){"
                "var L=document.getElementById('ch-list');"
                "L.addEventListener('click',function(e){"
                "var b=e.target.closest('button[data-mv]');if(!b)return;"
                "var rows=Array.prototype.slice.call(L.children);"
                "var i=rows.indexOf(b.closest('.ch-row')),j=i+(+b.dataset.mv);"
                "if(i<1||j<1||j>=rows.length)return;"
                "var a=rows[i].querySelectorAll('input,select');"
                "var c=rows[j].querySelectorAll('input,select');"
                "for(var k=0;k<a.length;k++){"
                "if(a[k].type=='checkbox'){var t=a[k].checked;a[k].checked=c[k].checked;c[k].checked=t;}"
                "else{var v=a[k].value;a[k].value=c[k].value;c[k].value=v;}}"
                "});"
                "})();"
                "</script>";
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // ── LoRa Radio ────────────────────────────────────────────
    section(html, lite, "LoRa Radio", false);
    // The live frequency/BW/SF/CR readout is driven by kPresetJs. Lite skips
    // both, so its selects carry no onchange handler to call.
    html += lite ? "<div class='row2'><label>Region<select name='region' id='sel-rgn'>"
                 : "<div class='row2'><label>Region<select name='region' id='sel-rgn' onchange='applyPreset()'>";
    sendChunk(html);
    sendFlash(kRegionOptions);
    html += lite ? "</select></label><label>Modem Preset<select id='sel-pst' name='modem_preset'>"
                 : "</select></label><label>Modem Preset<select id='sel-pst' name='modem_preset' onchange='applyPreset()'>";
    sendChunk(html);
    sendFlash(kPresetOptions);
    html += "</select></label></div>";
    sendChunk(html);
    sendFlash(kCustomLoraFields);
    // Both option lists are static constants, so the current values are applied
    // here rather than by marking one <option> selected. This runs before
    // kPresetJs registers its DOMContentLoaded handler, so the first
    // applyPreset() already sees these values.
    html += "<script>document.getElementById('sel-rgn').value='";
    html += gCfg->region; html += "';"
            "document.getElementById('sel-pst').value='";
    html += gCfg->loraUsePreset
                ? kPresets[gCfg->modemPreset < PRESET_COUNT ? gCfg->modemPreset : 0].name
                : "Custom";
    html += "';";
    // A value with no matching <option> — a bandwidth this radio cannot do, or
    // an SF/CR out of range from an imported config — would leave the select
    // blank. Coerce to what applyPresetParams() would land on anyway. Set
    // through a one-line helper so the four assignments still fit in tmp[96].
    html += "function sv(i,v){document.getElementById(i).value=v;}";
    snprintf(tmp, sizeof(tmp), "sv('sel-cbw',%u);sv('sel-csf',%u);sv('sel-ccr',%u);sv('in-cslot',%u);",
             (unsigned)loraCoerceBwCode(gCfg->loraCustomBwKhz),
             (unsigned)constrain((int)gCfg->loraCustomSf, LORA_SF_MIN, LORA_SF_MAX),
             (unsigned)constrain((int)gCfg->loraCustomCr, LORA_CR_MIN, LORA_CR_MAX),
             (unsigned)gCfg->loraCustomSlot);
    html += tmp;
    // Lite carries no kPresetJs, so the custom fields' onchange would throw on
    // every edit. Stub it: with no live readout to update there is nothing for
    // it to do, and the block it would show/hide is always visible here.
    if (lite) html += "function applyPreset(){}";
    html += "</script>";
    html += "<p class='gps-hint'>Frequency and modem parameters are derived automatically "
            "from region and preset, or from the fields above when the preset is Custom.</p>";
    sendChunk(html);
    if (!lite) {
        sendFlash(kPresetJs);
        sendFlash(kLoraReadout);
    }
    html += "<div class='row2'>";
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
    sectionEnd(html, lite);
    sendChunk(html);

    // ── MQTT ──────────────────────────────────────────────────
    section(html, lite, "MQTT", false);
    // Marker so the POST handler can apply checkbox state (unchecked boxes send
    // nothing) only when this section was actually rendered.
    html += "<input type='hidden' name='mqtt_present' value='1'>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='mqtt_en' value='1'";
    if (gCfg->mqttEnabled) html += " checked";
    html += "> Enable MQTT bridge</label>";
    html += "<label>Broker<input name='mqtt_server' type='text' maxlength='63' value='";
    html += gCfg->mqttServer;
    html += "'></label>";
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%u", (unsigned)gCfg->mqttPort);
    html += "<label>Port<input name='mqtt_port' type='number' min='1' max='65535' value='";
    html += tmp; html += "'></label>";
    // No TLS checkbox: this firmware has no TLS client, so MQTT is always
    // plaintext (use port 1883). Channel payloads remain end-to-end encrypted
    // with the channel key regardless of transport.
    html += "</div>";
    html += "<p class='gps-hint'>MQTT is plaintext only (port 1883) — this "
            "firmware has no TLS client. Channel payloads stay encrypted with "
            "your channel key.</p>";
    html += "<label>Username<input name='mqtt_user' type='text' maxlength='31' value='";
    html += gCfg->mqttUser;
    html += "'></label>"
            "<label>Password<input name='mqtt_pass' type='password' maxlength='47' value='";
    html += gCfg->mqttPass;
    html += "'></label>";
    html += "<label>Root topic<input name='mqtt_root' type='text' maxlength='47' value='";
    html += gCfg->mqttRoot;
    html += "'></label>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='mqtt_encrypt' value='1'";
    if (gCfg->mqttEncryption) html += " checked";
    html += "> Encrypt payloads (publish ciphertext to /2/e/)</label>";
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='mqtt_map_report' value='1'";
    if (gCfg->mqttMapReport) html += " checked";
    html += "> Map reporting (this node to /2/map/ every 15 min)</label>";
    html += "<p style='font-size:.8em;color:var(--muted);margin:.4em 0 0'>"
            "Requires WiFi credentials above. Default broker: mqtt.meshtastic.org:8883.</p>";
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Display ───────────────────────────────────────────────
    section(html, lite, "Display", false);
    // Brightness: a range input in the same 10% steps as the on-device slider,
    // with the value mirrored next to it since a bare range shows no number.
    {
        char b[8];
        snprintf(b, sizeof(b), "%u", (unsigned)cfgCoerceBrightness(gCfg->brightness));
        html += "<label>Brightness <output id='briOut'>";
        html += b; html += "%</output>"
                "<input name='brightness' type='range' id='briIn'"
                " min='"; html += String(BRIGHTNESS_PCT_MIN);
        html += "' max='"; html += String(BRIGHTNESS_PCT_MAX);
        html += "' step='"; html += String(BRIGHTNESS_PCT_STEP);
        html += "' value='"; html += b;
        html += "' oninput=\"document.getElementById('briOut').textContent=this.value+'%'\">"
                "</label>";
    }
    html += "<div class='row2'>";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)gCfg->screenOnSecs);
    html += "<label>Screen Timeout (s)<input name='screen_on' type='number' min='0' value='";
    html += tmp; html += "'></label>";
        html += "<label>Units (Display &amp; Telemetry)<select name='disp_units'>"
            "<option value='0'"; if (!gCfg->displayUnits) html += " selected"; html += ">Metric (C / hPa)</option>"
            "<option value='1'"; if ( gCfg->displayUnits) html += " selected"; html += ">Imperial (F / inHg)</option>"
            "</select></label>";
    html += "<label>Battery Display<select name='batt_display'>"
            "<option value='0'";
    if (gCfg->battDisplayMode != BATT_DISPLAY_VOLTAGE) html += " selected";
    html += ">Percentage (87%)</option>"
            "<option value='1'";
    if (gCfg->battDisplayMode == BATT_DISPLAY_VOLTAGE) html += " selected";
    html += ">Voltage (3.94V)</option>"
            "</select></label></div>";
#if HAS_SCROLL_INVERT
    // Trackball direction. Sits in Display rather than getting a section of its
    // own: it is a comfort preference like brightness, and one heading is heap
    // the AP-mode lite page cannot spare.
    html += "<div class='row2'>";
    html += "<label>Invert Scrolling (trackball)<select name='invert_scroll'>"
            "<option value='0'"; if (!gCfg->invertScroll) html += " selected"; html += ">Off</option>"
            "<option value='1'"; if ( gCfg->invertScroll) html += " selected"; html += ">On</option>"
            "</select></label></div>";
#endif
    html += "<div class='row2'>";
    html += "<label>Chat Style<select name='chat_style'>"
            "<option value='0'"; if (gCfg->chatStyle == CHAT_STYLE_CLASSIC) html += " selected"; html += ">Classic</option>"
            "<option value='1'"; if (gCfg->chatStyle == CHAT_STYLE_BUBBLES) html += " selected"; html += ">Bubbles</option>"
            "<option value='2'"; if (gCfg->chatStyle == CHAT_STYLE_OUTLINE) html += " selected"; html += ">Outline</option>"
            "</select></label></div>";
    html += "<div class='row2'>";
    html += "<label>Chat Names<select name='chat_names'>"
            "<option value='0'"; if (gCfg->chatNameStyle == CHAT_NAME_SHORT) html += " selected"; html += ">Short (ABCD)</option>"
            "<option value='1'"; if (gCfg->chatNameStyle == CHAT_NAME_LONG)  html += " selected"; html += ">Long (full name)</option>"
            "</select></label></div>";
    // Font size chooser — a modal (rather than a plain select) so the sizes read
    // as a deliberate pick. The button shows the current value; the hidden input
    // is what actually submits with the form.
    {
        const uint8_t fs = (gCfg->fontSize <= FONT_SIZE_MAX) ? gCfg->fontSize : FONT_SIZE_MEDIUM;
        const char *fsName = (fs == FONT_SIZE_SMALL) ? "Small"
                             : (fs == FONT_SIZE_LARGE) ? "Large"
                             : (fs == FONT_SIZE_XLARGE) ? "Extra Large" : "Medium";
        if (lite) {
            // Plain select — the modal needs CSS and JS that lite doesn't carry.
            static const char *kFsNames[] = { "Small", "Medium", "Large", "Extra Large" };
            html += "<label>Font Size (chat &amp; DM)<select name='font_size'>";
            for (uint8_t v = 0; v <= FONT_SIZE_MAX && v < 4; v++) {
                snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                         (unsigned)v, (v == fs) ? " selected" : "", kFsNames[v]);
                html += tmp;
            }
            html += "</select></label>";
        } else {
            html += "<label>Font Size (chat &amp; DM)</label>";
            html += "<button type='button' class='chooser-btn' id='fsBtn' onclick='fsOpen()'>";
            html += fsName;
            html += "</button>";
            snprintf(tmp, sizeof(tmp), "<input type='hidden' name='font_size' id='fsInput' value='%u'>", (unsigned)fs);
            html += tmp;
            sendChunk(html);
            sendFlash(kFontModal);
        }
    }
    if (lite) {
        // Plain select instead of the swatch grid: the grid is ~7 KB of JS plus
        // its own stylesheet, and it only exists to preview colors lite doesn't use.
        html += "<label>Theme<select name='ui_theme_preset'>";
        for (uint8_t v = 0; v < kThemePresetCount; v++) {
            snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                     (unsigned)v, (v == themePreset) ? " selected" : "",
                     kThemePresetNames[v]);
            html += tmp;
            sendChunkIfBig(html);
        }
        // Custom themes get plain options here — no builder, which needs the
        // grid page, but they must at least be listed. Without them a device
        // wearing a custom theme opens this page with nothing selected, so the
        // browser posts back the first option and a save from AP mode silently
        // replaces the user's theme with Camillia Dark.
        for (int slot = 0; slot < UI_CUSTOM_THEME_SLOTS; slot++) {
            const UiCustomTheme *ct = uiCustomThemeGet(slot);
            if (!ct) continue;
            const uint8_t v = (uint8_t)(kThemePresetCustomBase + slot);
            snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                     (unsigned)v, (v == themePreset) ? " selected" : "", ct->name);
            html += tmp;
            sendChunkIfBig(html);
        }
        html += "</select></label>";
    } else {
        html += "<label>Theme</label>";
        {
            char themeHidden[110];
            snprintf(themeHidden, sizeof(themeHidden),
                     "<input type='hidden' name='ui_theme_preset' id='themeInput' value='%u'>",
                     (unsigned)themePreset);
            html += themeHidden;
        }
        // The saved custom themes, emitted ahead of the picker so its script can
        // build cards for them. Server-side rather than fetched, because this
        // page already streams in one pass and the AP-mode variant cannot afford
        // a second round trip.
        html += "<script>var CUSTOM=[";
        {
            bool first = true;
            for (int slot = 0; slot < UI_CUSTOM_THEME_SLOTS; slot++) {
                const UiCustomTheme *ct = uiCustomThemeGet(slot);
                if (!ct) continue;
                char row[160];
                snprintf(row, sizeof(row),
                         "%s{s:%d,n:'%s',m:%u,bg:'#%06lX',p:'#%06lX',p2:'#%06lX',a:'#%06lX'}",
                         first ? "" : ",", slot, ct->name, (unsigned)ct->mode,
                         (unsigned long)rgb565To888(ct->bgMain),
                         (unsigned long)rgb565To888(ct->panelBg),
                         (unsigned long)rgb565To888(ct->panelAlt),
                         (unsigned long)rgb565To888(ct->accent));
                html += row;
                first = false;
                sendChunkIfBig(html);
            }
        }
        {
            char tail[64];
            snprintf(tail, sizeof(tail), "];var CUSTOM_BASE=%u,CUSTOM_SLOTS=%u;</script>",
                     (unsigned)kThemePresetCustomBase, (unsigned)UI_CUSTOM_THEME_SLOTS);
            html += tail;
        }
        sendChunk(html);
        sendFlash(kThemePicker);
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Notifications ─────────────────────────────────────────
    // Every way the device can get your attention, in one place rather than
    // split between Display (volume) and Modules (alert tones). The name holds
    // on every board: where there is no audio hardware the section is the
    // notification LED and keyboard blink alone.
    section(html, lite, "Notifications", false);
#if HAS_VOLUME_CONTROL
    // Same shape as the brightness slider. Compiled out where the board alerts
    // through a passive buzzer: amplitude is not controllable there, so the
    // control would be present and do nothing.
    {
        char v[8];
        snprintf(v, sizeof(v), "%u", (unsigned)cfgCoerceVolume((int)gCfg->volumePct));
        html += "<label>Notification Volume <output id='volOut'>";
        html += v; html += "%</output>"
                "<input name='volume' type='range' id='volIn'"
                " min='"; html += String(VOLUME_PCT_MIN);
        html += "' max='"; html += String(VOLUME_PCT_MAX);
        html += "' step='"; html += String(VOLUME_PCT_STEP);
        html += "' value='"; html += v;
        html += "' oninput=\"document.getElementById('volOut').textContent=this.value+'%'\">"
                "</label>";
    }
#endif
// Every board that can make a sound, which is the same test the on-device row
// uses (CFG_ACTION_MSG_ALERT) and the same one the Splash Melody select below
// already used. It was a three-device list, so the Heltec — which alerts
// through a passive buzzer on BOARD_BUZZER rather than a codec — chirped with
// no way to turn it off from this page, even though its Config screen had the
// setting all along. The Mesh Deck stays out on HAS_AUDIO_ALERTS being 0.
//
// All four options mean something on every board here. The buzzer boards used to
// be the exception — Default, Chirpy and Bass all fell through to a single
// tone() call, so only Off changed anything — and they now have three patterns
// of their own, pitched for a piezo rather than a speaker.
#if HAS_AUDIO_ALERTS
    html += "<label>Notification Sound<select name='msg_alert_sound'>"
            "<option value='0'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_DEFAULT) html += " selected"; html += ">Default</option>"
            "<option value='1'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_CHIRPY) html += " selected"; html += ">Chirpy</option>"
            "<option value='2'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_BASS) html += " selected"; html += ">Bass</option>"
            "<option value='3'"; if (gCfg->msgAlertSound == MSG_ALERT_SOUND_OFF) html += " selected"; html += ">Off</option>"
            "</select></label>";
#endif
#if defined(DEVICE_MESH_DECK)
    // Kept in this section rather than given its own: it is the same question
    // as the alert tone — how the device gets your attention — and one more
    // heading is heap the AP-mode lite page cannot spare. Compiled out on boards
    // with no notification LED wired.
    html += "<label>Channel Message LED Color<select name='notify_led_channel_color'>";
    {
        const uint8_t selected = cfgCoerceNotifyLedColor((int)gCfg->notifyLedColorChannel);
        for (uint8_t i = 0; i < kNotifyLedColorCount; i++) {
            char opt[96];
            snprintf(opt, sizeof(opt), "<option value='%u'%s>%s</option>",
                     (unsigned)i,
                     (i == selected) ? " selected" : "",
                     kNotifyLedColorNames[i]);
            html += opt;
        }
    }
    html += "</select></label>";

    html += "<label>Direct Message LED Color<select name='notify_led_dm_color'>";
    {
        const uint8_t selected = cfgCoerceNotifyLedColor((int)gCfg->notifyLedColorDm);
        for (uint8_t i = 0; i < kNotifyLedColorCount; i++) {
            char opt[96];
            snprintf(opt, sizeof(opt), "<option value='%u'%s>%s</option>",
                     (unsigned)i,
                     (i == selected) ? " selected" : "",
                     kNotifyLedColorNames[i]);
            html += opt;
        }
    }
    html += "</select></label>";
#endif
#if HAS_KB_BLINK
    // Sits with the LED colors above for the same reason they sit here: it
    // answers the same question on a board whose only notification light is the
    // keyboard.
    html += "<label>Keyboard Blink on Message<select name='kb_blink'>"
            "<option value='1'"; if ( gCfg->kbBlinkEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->kbBlinkEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    // Flash counts. Separate per kind because with no notification LED on these
    // boards the length of the pattern is the only thing telling a DM from a
    // channel message.
    html += "<div class='row2'>";
    for (int f = 0; f < 2; f++) {
        const bool isDm = (f == 1);
        const uint8_t cur = cfgCoerceKbFlashes(
            (int)(isDm ? gCfg->kbBlinkDmFlashes : gCfg->kbBlinkChanFlashes));
        html += isDm ? "<label>Blink Flashes (DM)<select name='kb_flash_dm'>"
                     : "<label>Blink Flashes (Channel)<select name='kb_flash_chan'>";
        for (int n = KB_FLASHES_MIN; n <= KB_FLASHES_MAX; n++) {
            snprintf(tmp, sizeof(tmp), "<option value='%d'%s>%d flash%s</option>",
                     n, (n == cur) ? " selected" : "", n, (n == 1) ? "" : "es");
            html += tmp;
        }
        html += "</select></label>";
    }
    html += "</div>";
    html += "<p style='font-size:.8em;color:var(--muted);margin:.2em 0 0'>"
            "Only used while the screen is off.</p>";
#endif
#if HAS_LIGHT_NOTIFY
    // One field for whichever light the board has: the notification LED and the
    // keyboard backlight never coexist, and the question is the same either way.
    html += "<label>Light Timeout<select name='light_timeout'>";
    for (int i = 0; i < kNotifyLightTimeoutCount; i++) {
        snprintf(tmp, sizeof(tmp), "<option value='%u'%s>%s</option>",
                 (unsigned)kNotifyLightTimeouts[i].secs,
                 (gCfg->notifyLightTimeoutS == kNotifyLightTimeouts[i].secs) ? " selected" : "",
                 kNotifyLightTimeouts[i].label);
        html += tmp;
    }
    html += "</select></label>";
    html += "<p style='font-size:.8em;color:var(--muted);margin:.2em 0 0'>"
            "How long the light keeps reminding you after a message arrives. The "
            "clock restarts on each new message, and reading the message stops it "
            "immediately either way. Never means it keeps reminding until read.</p>";
#endif
#if HAS_NAV_BAR_TOGGLE
    html += "<label>Bottom Nav Bar<select name='nav_bar'>"
            "<option value='1'"; if ( gCfg->navBarEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->navBarEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<p style='font-size:.8em;color:var(--muted);margin:.2em 0 0'>"
            "Row of tappable icons along the bottom of the screen - Home, "
            "DM, Nodes, Live, Config, Help. Disabled restores the keyboard "
            "shortcut hints that used to sit there. The keys work either way.</p>";
#endif
#if HAS_AUDIO_ALERTS
    html += "<label>Splash Melody<select name='splash_melody'>"
            "<option value='1'"; if ( gCfg->splashMelodyEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->splashMelodyEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
#endif
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Power ─────────────────────────────────────────────────
    section(html, lite, "Power", false);
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
    sectionEnd(html, lite);
    sendChunk(html);

    // ── Modules ───────────────────────────────────────────────
    section(html, lite, "Modules", false);
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
    // Three states, and they are worth telling apart. A board that supports the
    // feature but has nothing on the bus used to look identical to one that was
    // simply switched off — the setting sat there saying "Enabled" while nothing
    // was ever sent, which is exactly the doubt this is meant to remove.
    //
    // Same shape as the Archive dropped nodes checkbox above: the control is
    // disabled where it cannot do anything, and the reason takes the place of
    // the description. The stored preference is left untouched either way, so it
    // survives until a sensor is present.
    {
        const bool sensorFound = envHasSensor();
        const bool stillLooking = !sensorFound && !envProbeExhausted();

        // Left enabled even with no sensor detected. Disabling it was a mistake:
        // it is a perfectly reasonable thing to switch on before fitting a
        // sensor, and the setting is harmless without one — the announce path
        // simply finds nothing to read. The status line below says what is
        // actually there; it does not need to police the control as well.
        html += "<label>Environment Sensor Telemetry<select name='tel_env_en'>"
                "<option value='1'"; if ( gCfg->telEnvEnabled) html += " selected"; html += ">Enabled</option>"
                "<option value='0'"; if (!gCfg->telEnvEnabled) html += " selected"; html += ">Disabled</option>"
                "</select></label>";
        html += "<label>Sensor Interval<input type='text' value='Uses broadcast interval above' disabled></label>";

        html += "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em;grid-column:1/-1'>";
        if (sensorFound) {
            html += "Detected <b>";
            html += envSensorName();
            html += "</b> on <code>";
            html += envProbeRouteName();
            html += "</code>.";
        } else if (stillLooking) {
            html += "Still looking for a sensor. Reload the page to see the result.";
        } else {
            html += "<b>No sensor detected</b> at boot, so nothing is sent even with "
                    "this enabled. You can still turn it on ready for one. Run "
                    "<code>i2c scan all</code> on the serial console to see every "
                    "address that answered, on every candidate bus.";
        }
        html += "</p>";
    }
#else
    html += "<label>Environment Sensor Telemetry<input type='text' value='Unavailable on this build' disabled></label>";
    html += "<label>Sensor Interval<input type='text' value='Unavailable on this build' disabled></label>";
    html += "<input type='hidden' name='tel_env_en' value='0'>";
#endif
    html += "</div>";

    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Mesh Beacons</h3>";
    html += "<label>Listen for beacons<select name='mesh_beacon_listen'>"
            "<option value='1'"; if ( gCfg->meshBeaconListen) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->meshBeaconListen) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    html += "<p style='font-size:.82em;color:var(--muted);margin:.2em 0 0'>"
            "Meshtastic 2.7 nodes can retune briefly to advertise a different mesh, "
            "naming a channel, preset and region. With this on, any such beacon that "
            "reaches this device is listed on the Beacons screen, under Live &rarr; "
            "Tools. Receive-only &mdash; nothing "
            "is transmitted, and an offer is never applied to your radio. Off by default.</p>";

    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Neighborhood Info</h3>";
    html += "<div class='row2'>";
    html += "<label>Enabled<select name='neighbor_info_en'>"
            "<option value='1'"; if ( gCfg->neighborInfoEnabled) html += " selected"; html += ">Enabled</option>"
            "<option value='0'"; if (!gCfg->neighborInfoEnabled) html += " selected"; html += ">Disabled</option>"
            "</select></label>";
    uint32_t neighborInfoIntervalMins = (gCfg->neighborInfoIntervalS >= 60UL)
        ? (uint32_t)(gCfg->neighborInfoIntervalS / 60UL)
        : 240UL;
    if (neighborInfoIntervalMins < 240UL) neighborInfoIntervalMins = 240UL;
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)neighborInfoIntervalMins);
    html += "<label>Broadcast Interval (min)<input name='neighbor_info_mins' type='number' min='240' value='";
    html += tmp; html += "'></label></div>";
    html += "<label>Transmit Over LoRa<select name='neighbor_info_lora'>"
            "<option value='1'"; if ( gCfg->neighborInfoOverLora) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->neighborInfoOverLora) html += " selected"; html += ">No</option>"
            "</select></label>";

    // Store and Forward (client)
    html += "<h3 style='font-size:.95em;margin:.8em 0 .3em'>Store &amp; Forward (Client)</h3>";
    html += "<label>Receive Replayed Messages<select name='snf_client_en'>"
            "<option value='1'"; if ( gCfg->snfClientEnabled) html += " selected"; html += ">Yes</option>"
            "<option value='0'"; if (!gCfg->snfClientEnabled) html += " selected"; html += ">No</option>"
            "</select></label>";
    // A router never replays unsolicited — it only answers a CLIENT_HISTORY — so
    // this switch on its own never receives anything but heartbeats. The button
    // that does the asking is a standalone form, so it lives with the other
    // "do something now" actions under Utilities rather than inside the config
    // form here; this just says where it went.
    html += "<p style='font-size:.82em;color:#888;margin:.1em 0 .6em'>"
            "Displays messages replayed by a Store &amp; Forward router, prefixed "
            "<code>[SF]</code>. A router only replays when asked &mdash; use "
            "<em>Request Replay Now</em> under Utilities &rarr; Diagnostics.</p>";

    html += "<label>Router Node ID<input name='snf_router_id' type='text' maxlength='16'"
            " placeholder='auto' value='";
    if (gCfg->snfRouterNodeId != 0) {
        char routerBuf[16];
        snprintf(routerBuf, sizeof(routerBuf), "!%08lx", (unsigned long)gCfg->snfRouterNodeId);
        html += routerBuf;
    }
    html += "'></label>"
            "<p style='font-size:.82em;color:#888;margin:.1em 0 .6em'>"
            "Leave blank to use whichever router this node hears a heartbeat from. "
            "Set it &mdash; as <code>!aabbccdd</code> &mdash; to always ask one "
            "specific router, and heartbeats from any other are then ignored when "
            "choosing who to ask. Worth setting when your router has heartbeats "
            "switched off, which is the Meshtastic default and leaves it "
            "undiscoverable otherwise.</p>";
    sectionEnd(html, lite);
    sendChunk(html);

    // WiFi. Collapsed by default, matching MQTT / Power / Modules. A <details>
    // only hides its contents visually — the inputs stay in the form and submit
    // either way, which is what the other collapsed sections already rely on.
    section(html, lite, "WiFi", false);
    // appendAttr, not a raw append: an SSID or password containing an
    // apostrophe would close the value= attribute and mangle the rest of the
    // form, so the field would come back truncated on the next save. Same
    // reason the channel-name fields use it.
    html += "<label>SSID<input name='wifi_ssid' type='text' maxlength='63' value='";
    appendAttr(html, gWifiSsid);
    html += "'></label>"
            "<label>Password<input name='wifi_pass' type='password' maxlength='63' value='";
    appendAttr(html, gWifiPass);
    html += "'></label>";
    if (!lite) {
        html += "<p class='gps-hint'>This is the network the device joins. Other "
                "remembered networks, and switching between them, live on the "
                "<b>WiFi</b> tab.</p>";
    }
    sectionEnd(html, lite);
    sendChunk(html);

    // Firmware Updates. Compiled out on the Cardputer, where OTA is disabled
    // altogether — offering the toggle there would promise a check that never
    // leads anywhere. The update source itself is not configurable by design.
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    section(html, lite, "Firmware Updates", false);
    if (!otaLayoutSupportsUpdate()) {
        // Installed by a third-party flasher into its own partition layout: the
        // slot an update would land in belongs to another firmware. Say so
        // instead of offering a toggle that leads nowhere.
        html += "<p style='font-size:.82em;color:#888;margin:.1em 0 .5em'>"
                "Updates are unavailable on this install: the device was flashed "
                "with a third-party partition layout, where the spare app slot may "
                "hold other firmware. Re-flash the factory image to enable "
                "over-the-air updates.</p>";
    } else {
        html += "<p style='font-size:.82em;color:#888;margin:.1em 0 .5em'>"
                "On boot, once WiFi is up, the device asks whether a newer release "
                "exists and offers to install it. Declining is remembered until the "
                "next reboot.</p>";
        html += "<label>Check for Updates on Boot<select name='ota_autocheck'>"
                "<option value='1'"; if ( gCfg->otaAutoCheckEnabled) html += " selected"; html += ">Yes</option>"
                "<option value='0'"; if (!gCfg->otaAutoCheckEnabled) html += " selected"; html += ">No</option>"
                "</select></label>";
    }
    sectionEnd(html, lite);
    sendChunk(html);
#endif

    // Node Management. The archive checkbox is only meaningful where the node
    // table can actually spill somewhere: the board needs an SD slot AND a
    // mounted card. Where it can't, the box is rendered disabled with the
    // reason shown, and handlePostSave leaves the stored preference untouched
    // so it survives until a card is available.
    {
        const bool slot    = nodeArchiveSlotExists();
        const bool canArch = slot && nodeArchiveAvailable();
        section(html, lite, "Node Management", false);
        html += "<p style='font-size:.82em;color:#888;margin:.1em 0 .5em'>"
                "The device keeps the ";
        html += String(MAX_NODES);
        html += " most recently heard nodes. When that fills up, the "
                "oldest non-favorite is dropped to make room — favorites are never "
                "dropped.</p>";

        html += "<label style='display:flex;align-items:center;gap:.5em'>"
                "<input type='checkbox' name='node_archive' value='1'";
        if (gCfg->nodeArchiveEnabled) html += " checked";
        if (!canArch) html += " disabled";
        html += " style='width:auto;margin:0'>"
                "<span>Archive dropped nodes to SD card</span></label>";

        html += "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>";
        if (!slot) {
            html += "Unavailable: this board has no SD card slot. Dropped nodes "
                    "are discarded.";
        } else if (!canArch) {
            html += "Unavailable: no SD card detected. Insert a card and reboot "
                    "to enable archiving.";
        } else {
            html += "Appends each dropped node to <code>";
            html += nodeArchiveFilePath();
            html += "</code> so its details are preserved after it leaves the "
                    "live list.";
        }
        html += "</p>";

        // Auto-favorite. Radius is stored in meters but shown in whatever the
        // Units setting uses, so it reads naturally alongside the rest of the UI.
        const bool imperial = (gCfg->displayUnits != 0);
        const float shown = imperial ? (gCfg->autoFavoriteRangeM / 1609.344f)
                                     : (gCfg->autoFavoriteRangeM / 1000.0f);
        char rangeBuf[24];
        snprintf(rangeBuf, sizeof(rangeBuf), "%.2f", (double)shown);

        html += "<label style='display:flex;align-items:center;gap:.5em;margin-top:.6em'>"
                "<input type='checkbox' name='autofav' value='1'";
        if (gCfg->autoFavoriteEnabled) html += " checked";
        html += " style='width:auto;margin:0'>"
                "<span>Auto-favorite nearby nodes</span></label>";

        html += "<label>Auto-favorite radius (";
        html += imperial ? "miles" : "km";
        html += ")<input name='autofav_range' type='number' min='0.05' max='500'"
                " step='0.05' value='";
        html += rangeBuf;
        html += "'></label>";

        html += "<p style='font-size:.82em;color:#888;margin:.3em 0 .8em'>"
                "Any node reporting a position within this radius is favorited "
                "automatically. Favorites sort to the top of the node and DM lists "
                "and are never dropped when the node table fills up. This only ever "
                "<em>adds</em> favorites &mdash; a node moving out of range is never "
                "un-favorited, so it can't undo one you set by hand. Requires a known "
                "position for both your node and theirs.</p>";

        html += "<p style='margin:.2em 0 1em'><a href='/nodes.csv'"
                " style='display:inline-block;padding:.4em 1.2em;background:#3b82f6;"
                "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
                "&#11015; Export Node List (CSV)</a></p>"
                "<p style='font-size:.82em;color:#888;margin:-.6em 0 1em'>"
                "Downloads every node currently known to the device, plus any "
                "previously archived nodes if an archive exists.</p>";
        sectionEnd(html, lite);
    }

    // Web Config Access. Last section on the page by design: it governs who can
    // reach this page rather than how the device behaves, so it sits after
    // everything someone actually came here to change.
    section(html, lite, "Web Config Access", false);
    html += "<label style='display:flex;align-items:center;gap:.5em'>"
            "<input type='checkbox' name='web_auth' value='1' id='web-auth-cb'"
            " onchange=\"document.getElementById('web-auth-creds').style.display="
            "this.checked?'':'none'\"";
    if (gCfg->webCfgAuthEnabled) html += " checked";
    html += "> Require login (authentication)</label>";
    html += "<p class='gps-hint'>When disabled, the web config is reachable without a "
            "username or password. Only leave this off on trusted networks.</p>";
    html += "<div id='web-auth-creds'";
    if (!gCfg->webCfgAuthEnabled) html += " style='display:none'";
    html += ">";
    html += "<label>Username<input type='text' value='admin' readonly></label>";
    html += "<label>Password (leave blank to keep current)"
        "<input name='web_pass' type='password' maxlength='63' autocomplete='new-password'"
        " placeholder='Enter new password'></label>";
    html += "</div>";
    sectionEnd(html, lite);
    sendChunk(html);

    html += "<button type='submit' style='width:100%;margin-top:1.5em'>Save All</button></form>";
    sendChunk(html);

    if (lite) {
        // Restore, but not backup. Import streams the upload into a static
        // buffer and parses it on the stack, so it costs AP mode nothing;
        // export has to build the whole YAML in one String, which is the sort
        // of multi-KB contiguous allocation this mode cannot promise. Outside
        // the form above rather than inside it — a nested form would submit to
        // /save instead.
        html += "<hr style='margin:1.4em 0;border:0;border-top:1px solid #3a4553'>"
                "<h3 style='margin:.2em 0 .4em'>Restore Config</h3>"
                "<form method='POST' action='/import' enctype='multipart/form-data'>"
                "<label>Import a YAML config file"
                "<input type='file' name='f' accept='.yaml,.yml'></label>"
                "<button type='submit' style='width:100%;margin-top:.6em'>"
                "Upload &amp; Apply</button></form>"
                "<p style='font-size:.82em;color:#888'>Applies the file and reboots. "
                "Files over 8 KB are rejected rather than partly applied. Export is "
                "available once the device is on your WiFi.</p>";
        // No further tabs in AP mode — close the document.
        html += "</body></html>";
        sendChunk(html);
        if (gSendAborted) {
            // Never terminated the chunked stream — drop the socket so the
            // browser fails the load now instead of waiting on more chunks.
            server.client().stop();
            logWifiHeapDiag("lite page abandoned");
            return;
        }
        server.sendContent("");   // terminate chunked response
        logWifiHeapDiag("lite page sent");
        return;
    }

    // ── WiFi tab ──────────────────────────────────────────────
    // Its own tab rather than a block under the Config pane's Save All
    // button, which read as an afterthought bolted below the primary action.
    // Rendered after the lite return above, so the AP page never sees it —
    // that mode has no tab bar and serves the Config pane alone.
    html += "</div></div><div class='tab-panel' id='tab-wifi'><div class='tab-pane-center'>";
    // ── Saved WiFi networks ───────────────────────────────────
    // After the config form closes, so each row is a standalone form: nesting
    // one inside the form above would submit to /save instead. Below the lite
    // return, so it never reaches the AP page — that mode is deliberately the
    // Config pane only, and the heap there has nothing spare for a per-row list.
    //
    // The SSID/Password fields in the WiFi section above still set the active
    // network; this is for keeping the others around and swapping between them.
    section(html, false, "Saved Networks", false);
    {
        char nSsid[64], nPass[64];
        const int savedCount = cfgSavedWifiCount();

        html += "<p style='font-size:.82em;color:#888;margin:.1em 0 .6em'>"
                "Networks this device remembers. <b>Use</b> switches to one and keeps "
                "the current network in the list, so you can swap back. Switching "
                "reconnects the radio &mdash; if you are reading this page over WiFi, "
                "it will drop and you will need to reach the device on the new "
                "network.</p>";

        html += "<table style='width:100%;border-collapse:collapse;font-size:.9em'>";
        // SSIDs are free text chosen by whoever owns the access point, so every
        // one of them goes through appendAttr() — an apostrophe would otherwise
        // close the value= attribute and mangle the rest of the form, and a '<'
        // would inject markup from a network name we merely scanned.
        if (gCfg->wifiSsid[0]) {
            html += "<tr><td style='padding:.3em 0'><b>";
            appendAttr(html, gCfg->wifiSsid);
            html += "</b> <span style='color:#5a9;font-size:.85em'>&#9679; active</span></td>"
                    "<td style='text-align:right;color:#888;font-size:.85em'>in use</td></tr>";
        }
        for (int i = 0; i < savedCount; i++) {
            if (!cfgSavedWifiAt(i, nSsid, sizeof(nSsid), nPass, sizeof(nPass))) continue;
            if (!nSsid[0]) continue;
            html += "<tr><td style='padding:.3em 0'>";
            appendAttr(html, nSsid);
            html += "</td><td style='text-align:right;white-space:nowrap'>"
                    "<form method='POST' action='/wifi-use' style='display:inline'>"
                    "<input type='hidden' name='ssid' value='";
            appendAttr(html, nSsid);
            html += "'><button type='submit' style='padding:.15em .7em'>Use</button></form> "
                    "<form method='POST' action='/wifi-forget' style='display:inline'"
                    " onsubmit=\"return confirm('Forget this network?')\">"
                    "<input type='hidden' name='ssid' value='";
            appendAttr(html, nSsid);
            html += "'><button type='submit' style='padding:.15em .7em;background:#c0392b'>"
                    "Forget</button></form></td></tr>";
        }
        html += "</table>";

        if (savedCount == 0) {
            html += "<p style='font-size:.82em;color:#888;margin:.3em 0'>"
                    "No other networks remembered yet.</p>";
        }

        char slots[64];
        snprintf(slots, sizeof(slots), "%d of %d slots used", savedCount, CFG_SAVED_WIFI_MAX);
        html += "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>";
        html += slots;
        html += ". When full, the least recently added one drops off.</p>";

        html += "<form method='POST' action='/wifi-add'>"
                "<label>Add network &mdash; SSID"
                "<input name='ssid' type='text' maxlength='63' required></label>"
                "<label>Password (blank for an open network)"
                "<input name='pass' type='password' maxlength='63'></label>"
                "<button type='submit' style='margin-top:.4em'>Remember Network</button>"
                "</form>"
                "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
                "Adds to the list without switching to it. The credentials are not "
                "tested until the device connects.</p>";
    }
    sectionEnd(html, false);
    sendChunk(html);

    html += "</div></div><div class='tab-panel' id='tab-utils'><div class='tab-pane-center'>";

    // ── Diagnostics / Utilities ───────────────────────────────
    // Collapsible, matching the config tab. These pass `false` rather than
    // `lite` because the lite page returns above and never reaches this tab.
    section(html, false, "Diagnostics", false);
    html +=
        "<form method='POST' action='/announce'>"
        "<button type='submit' style='background:#e07b00'>"
        "&#128225; Send NODEINFO Broadcast</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Forces immediate re-announcement to the mesh (NODEINFO + position).</p>";

    html +=
        "<form method='POST' action='/telemetry'>"
        "<button type='submit' style='background:#0f766e'>"
        "&#128202; Send Telemetry Now</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Forces immediate telemetry TX (device + environment when available).</p>";

    html +=
        "<form method='POST' action='/snf-request'>"
        "<button type='submit' style='background:#6b4fa0'>"
        "&#128260; Request Replay Now</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Asks a Store &amp; Forward router for the last ";
    html += String((unsigned)SNF_HISTORY_WINDOW_MIN);
    html += " minutes of stored messages. A router never replays on its own, so "
            "this is what makes <em>Receive Replayed Messages</em> do anything. "
            "Needs that setting on and a router that has been heard recently &mdash; "
            "routers announce themselves with a periodic heartbeat.";
    if (webCfgSnfResult()[0]) {
        html += "<br>Last attempt: ";
        html += webCfgSnfResult();
    }
    html += "</p>";
    sectionEnd(html, false);
    sendChunk(html);

#if HAS_STATE_MAPS
    // ── Maps Download ─────────────────────────────────────────────
    // Absent, not disabled, when there is nowhere to put the files: on a board
    // with no storage there is nothing the reader could do about it, and a
    // permanently greyed section is worse than one that was never there.
    if (stateMapStorageReady()) {
        section(html, false, "Maps Download", false);
        html +=
            "<p style='font-size:.82em;color:#888;margin:.1em 0 .6em'>"
            "The device shows a node's position on a cached map of the state it "
            "falls in (Nodes &rarr; Actions &rarr; Locate). It cannot fetch those "
            "maps itself, so this page downloads them and saves them to the "
            "card. Only the states your known nodes are actually in get "
            "downloaded.</p>";
        html +=
            "<div id='mapdl-status' style='font-size:.82em;color:#888;margin:.2em 0 .6em'>"
            "Checking cached maps&hellip;</div>";
        html +=
            "<button type='button' id='mapdl-btn' style='background:#2a6f97' disabled>"
            "&#128506; Download maps for known nodes</button>";
        html +=
            "<p style='font-size:.82em;color:#888;margin:.4em 0 1em'>"
            "Downloading writes to the card over the same SPI bus the radio uses, "
            "so expect the mesh to miss traffic while it runs. Map tiles come from "
            "OpenStreetMap.</p>";
        sectionEnd(html, false);
        sendChunk(html);
    }
#endif

    section(html, false, "Debug Output", false);
    html +=
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
    sectionEnd(html, false);
    sendChunk(html);

    section(html, false, "Software Update", false);
    html +=
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>Current firmware: <b>";
    html += APP_VERSION;
    html +=
        "</b></p>";
    // Update-available check intentionally removed for now. The check plumbing
    // (queueReleaseCheckNow / runQueuedReleaseCheck / the /release-check route
    // and its client JS) remains dormant behind kWebDeviceReleaseCheckEnabled and
    // is no longer reachable from the UI.

    sectionEnd(html, false);
    sendChunk(html);

    section(html, false, "Backup &amp; Restore", false);
    html +=
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
    sectionEnd(html, false);
    sendChunk(html);

    if (gOnScreenshotPng) {
        section(html, false, "Display Capture", false);
        html +=
            "<p><a href='/screenshot'"
            " style='display:inline-block;padding:.4em 1.2em;background:#3b82f6;"
            "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
            "&#128247; Capture &amp; Download PNG</a></p>"
            "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
            "Captures the current on-device screen and downloads it as a PNG file.</p>";
        sectionEnd(html, false);
        sendChunk(html);
    }

    // Above the Danger Zone rather than inside it: it changes nothing but which
    // palette entry each node draws in, and nothing about it needs a confirm.
    section(html, false, "Chat Colors", false);
    html +=
        "<form method='POST' action='/reset-chat-colors'>"
        "<button type='submit'>&#127912; Reset Chat Colors</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Each node's chat color comes from its node ID, so two nodes can end up "
        "sharing one. This re-rolls every assignment at once &mdash; press again "
        "for another arrangement. Messages are not touched.</p>";
    sectionEnd(html, false);
    sendChunk(html);

    // Directly above the Danger Zone, which is where Clear Messages lives:
    // someone reading that button is exactly the person who wants this first.
    section(html, false, "Messages", false);
    html += "<p style='margin:.2em 0 1em'><a href='/messages.csv'"
            " style='display:inline-block;padding:.4em 1.2em;background:#3b82f6;"
            "color:#fff;border-radius:3px;text-decoration:none;font-size:.95em'>"
            "&#11015; Export Messages (CSV)</a></p>"
            "<p style='font-size:.82em;color:#888;margin:-.6em 0 1em'>"
            "Downloads every channel message and direct message the device still "
            "holds, one row each, with the channel or peer, timestamp, sender and "
            "delivery state. Long messages are joined back into one row. The file "
            "is sent to this browser only &mdash; nothing is written to the "
            "device's SD card. History is limited by what the device keeps in "
            "memory, so take a copy before clearing anything below.</p>";
    sectionEnd(html, false);
    sendChunk(html);

    // Written out rather than using section(): the helper has no way to carry
    // the red, and losing it would make the destructive actions read like any
    // other group. Collapsed like the rest, which also puts a deliberate click
    // in front of Factory Reset.
    html +=
        "<details><summary style='color:#c0392b'>Danger Zone</summary>"
        "<form method='POST' action='/clear-messages'"
        " onsubmit=\"return confirm('This will clear all stored channel and DM messages. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Clear Messages</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
        "Clears saved and in-memory chat/DM history without resetting device configuration.</p>"
        "<form method='POST' action='/clear-nodes'"
        " onsubmit=\"return confirm('This will clear all discovered nodes EXCEPT favorites, "
        "then reboot. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Clear Nodes (Keep Favorites)</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
        "Drops every non-favorited node from the persisted database and reboots. "
        "Favorited nodes are kept, with their names, keys and positions.</p>"
        "<form method='POST' action='/clear-nodes'"
        " onsubmit=\"return confirm('This will clear all discovered nodes INCLUDING favorites, "
        "then reboot. Continue?')\">"
        "<input type='hidden' name='all' value='1'>"
        "<button type='submit' style='background:#c0392b'>"
        "Clear Nodes (All)</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
        "Clears the entire persisted node database &mdash; favorites included &mdash; "
        "and reboots.</p>";

#if HAS_STATE_MAPS
    // Only where there is a cache to clear. Hidden on the same terms as the
    // Maps Download section above, so the two never disagree about whether this
    // device has maps at all.
    if (stateMapStorageReady()) {
        html +=
            "<form method='POST' action='/clear-maps'"
            " onsubmit=\"return confirm('This will delete every cached state map from the card. "
            "Putting them back means running Maps Download again - the device cannot "
            "fetch maps on its own. Continue?')\">"
            "<button type='submit' style='background:#c0392b'>"
            "Clear Maps</button>"
            "</form>"
            "<p style='font-size:.82em;color:#888;margin:.3em 0 .6em'>"
            "Deletes the cached state maps that Nodes &rarr; Actions &rarr; Locate draws on. "
            "Nothing else on the card is touched, and no node or message data is affected. "
            "This is reversible: <em>Maps Download</em> above puts them back, since maps "
            "always arrive from this page rather than being fetched by the device.</p>";
    }
#endif

    html +=
        "<form method='POST' action='/factory-reset'"
        " onsubmit=\"return confirm('This will erase ALL settings and reboot the device. Continue?')\">"
        "<button type='submit' style='background:#c0392b'>"
        "Factory Reset</button>"
        "</form>"
        "<p style='font-size:.82em;color:#888;margin:.3em 0 1em'>"
        "Erases all NVS configuration (node identity, channels, keys) and reboots."
        " The device will behave as if freshly flashed.</p>"
        "</details>";

    html += "</div></div><div class='tab-panel' id='tab-live'>";
    html += "<h3 style='margin-top:1.2em'>Live RX/TX</h3>"
            "<p class='gps-hint'>Streams the Announcements feed from the device, similar to the on-device ANN view.</p>"
            "<div class='live-wrap'>"
            "<div class='live-toolbar'>"
            "<button type='button' class='map-mini-btn' onclick='clearLiveFeed()'>Clear View</button>"
            "<span id='live-status'>Paused</span>"
            "</div>"
            "<div id='live-feed' class='live-feed'></div>"
            "</div>"
            "<h3 style='margin-top:1.2em'>Live Charts</h3>"
            "<p class='gps-hint'>Same data shown on-device under Live &rarr; Tools. Last 60 samples, newest on the right.</p>"
            "<div class='chart-wrap'>"
              "<div class='chart-box'>"
                "<div class='chart-head'><strong>Channel Utilization (%)</strong>"
                  "<span id='chart-chutil-stats' class='chart-stats'>--</span></div>"
                "<canvas id='chart-chutil' class='chart-canvas' width='600' height='180'></canvas>"
                "<div class='chart-legend'>"
                  "<span class='swatch sw-chutil'></span>ChUtil"
                  "&nbsp;&nbsp;<span class='swatch sw-airutil'></span>AirTx"
                "</div>"
              "</div>"
              "<div class='chart-box'>"
                "<div class='chart-head'><strong>SNR (dB) / RSSI (dBm)</strong>"
                  "<span id='chart-snr-stats' class='chart-stats'>--</span></div>"
                "<canvas id='chart-snr' class='chart-canvas' width='600' height='180'></canvas>"
                "<div class='chart-legend'>"
                  "<span class='swatch sw-snr'></span>SNR (left axis, dB)"
                  "&nbsp;&nbsp;<span class='swatch sw-rssi'></span>RSSI (right axis, dBm)"
                "</div>"
              "</div>"
            "</div>";

    // Chat tab is omitted on the no-PSRAM Cardputer: its chat/DM buffers are
    // freed while web config runs (see webCfgBegin), so the tab would show
    // nothing, and dropping it trims the page and the polling load.
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    html += "</div><div class='tab-panel' id='tab-chat'>";
    html += "<h3 style='margin-top:1.2em'>Chat</h3>";
    html += "<label style='display:inline-flex;align-items:center;gap:.4em;margin:.2em 0'>"
            "<input type='checkbox' id='chat-live' checked onchange='chatLiveToggle()' style='width:auto'>"
            " Live updates</label>";
    html += "<p class='gps-hint'>Live channel and direct-message conversations. "
            "Send, reply, and react to messages over the mesh. Uncheck <b>Live updates</b> "
            "to stop background polling.</p>";
    html += "<div id='chat-body'>";
    html += "<label>Conversation<select id='chat-target' onchange='chatTargetChanged()'>"
            "<option value=''>-- select --</option></select></label>";
    html += "<div id='chat-feed' class='chat-feed'></div>";
    html += "<div id='chat-reply-bar' class='chat-reply' style='display:none'>"
            "<span id='chat-reply-text'></span>"
            "<button type='button' class='chat-mini' onclick='chatCancelReply()'>&times;</button></div>";
    html += "<div class='chat-compose'>"
            "<input type='text' id='chat-input' maxlength='200' placeholder='Message...' "
            "onkeydown='if(event.key===\"Enter\"){event.preventDefault();chatSend();}'>"
            "<button type='button' onclick='chatSend()'>Send</button></div>";
    html += "</div>";  // #chat-body
#endif
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
    if (totalNodes > 0) {
        html += "<p class='gps-hint'>Select a node to see its details and location.</p>";
        html += "<select id='node-select' onchange='selectNode()'>";
        html += "<option value=''>-- select a node --</option>";
        sendChunk(html);            // flush before streaming the option list
        streamNodeOptions();
        html += "</select>";
        html += "<div id='node-info' style='margin-top:.6em'></div>";
        html += "<div class='map-wrap' style='margin-top:.6em'>"
                "<div id='node-mini-map' class='map-canvas' style='height:220px'></div></div>";
        html += "<div id='node-detail-store' style='display:none'>";
        sendChunk(html);            // flush before streaming the detail panels
        streamNodeDetails();
        html += "</div>";
    } else {
        html += "<p class='gps-hint'>No nodes discovered yet.</p>";
    }
    html += "<script>var NODE_HEAT_POINTS=";
    sendChunk(html);                // flush before streaming the points array
    streamMapPoints();
    html += ";var NODE_ME_POINT=";
    if (mapHasMe) {
        snprintf(tmp, sizeof(tmp), "{lat:%.7f,lon:%.7f}", mapMeLat, mapMeLon);
        html += tmp;
    } else {
        html += "null";
    }
    html += ";</script>";

#if HAS_STATE_MAPS
    // Maps Download status. Runs after NODE_HEAT_POINTS is defined above, and
    // no-ops when the section was not rendered (no storage on this device).
    //
    // The containing-state search deliberately mirrors nodesStateForCoords() in
    // the firmware, smallest-box tie-break included: if the two disagreed about
    // which state a node is in, this page would cache a map the device never
    // looks at.
    html +=
        "<script>\n"
        "(function(){\n"
        "var el=document.getElementById('mapdl-status');\n"
        "if(!el)return;\n"
        "var btn=document.getElementById('mapdl-btn');\n"
        "var TILE=256,MAXTILES=64,ST=null,NEED=[];\n"
        "// Web Mercator pixel coords at zoom z.\n"
        "function lon2px(lon,z){return (lon+180)/360*TILE*Math.pow(2,z);}\n"
        "function lat2px(lat,z){var s=Math.sin(lat*Math.PI/180);\n"
        "  return (0.5-Math.log((1+s)/(1-s))/(4*Math.PI))*TILE*Math.pow(2,z);}\n"
        "// Smallest zoom whose tiles already carry at least the detail we keep, so we\n"
        "// downscale rather than upscale and never fetch more than we can use.\n"
        "function zoomFor(s,W,H){\n"
        "  for(var z=2;z<=11;z++){\n"
        "    var w=lon2px(s.lonMax,z)-lon2px(s.lonMin,z);\n"
        "    var h=lat2px(s.latMin,z)-lat2px(s.latMax,z);\n"
        "    if(w>=W&&h>=H){\n"
        "      var nx=Math.ceil(w/TILE)+1,ny=Math.ceil(h/TILE)+1;\n"
        "      if(nx*ny>MAXTILES)return z-1<2?2:z-1;\n"
        "      return z;\n"
        "    }\n"
        "  }\n"
        "  return 11;\n"
        "}\n"
        "function loadTile(z,x,y){\n"
        "  return new Promise(function(res){\n"
        "    var img=new Image();\n"
        "    img.crossOrigin='anonymous';                       // needed to read pixels back\n"
        "    img.onload=function(){res(img);};\n"
        "    img.onerror=function(){res(null);};                // a hole beats a failed run\n"
        "    img.src='https://tile.openstreetmap.org/'+z+'/'+x+'/'+y+'.png';\n"
        "  });\n"
        "}\n"
        "// Build the Mercator mosaic covering the state's box, then resample it into an\n"
        "// equirectangular image: the device places its pin with a LINEAR latitude\n"
        "// mapping, so a Mercator image would sit the marker wrong - by 13px on Alaska.\n"
        "// Each destination row is drawn from the Mercator rows that actually cover it.\n"
        "async function buildStatePng(s,W,H,onTile){\n"
        "  var z=zoomFor(s,W,H);\n"
        "  var px0=lon2px(s.lonMin,z),px1=lon2px(s.lonMax,z);\n"
        "  var py0=lat2px(s.latMax,z),py1=lat2px(s.latMin,z);\n"
        "  var tx0=Math.floor(px0/TILE),tx1=Math.floor(px1/TILE);\n"
        "  var ty0=Math.floor(py0/TILE),ty1=Math.floor(py1/TILE);\n"
        "  var mw=(tx1-tx0+1)*TILE,mh=(ty1-ty0+1)*TILE;\n"
        "  var mos=document.createElement('canvas');mos.width=mw;mos.height=mh;\n"
        "  var mc=mos.getContext('2d');\n"
        "  mc.fillStyle='#e8eef7';mc.fillRect(0,0,mw,mh);\n"
        "  var total=(tx1-tx0+1)*(ty1-ty0+1),done=0;\n"
        "  for(var ty=ty0;ty<=ty1;ty++){\n"
        "    for(var tx=tx0;tx<=tx1;tx++){\n"
        "      var img=await loadTile(z,tx,ty);\n"
        "      if(img)mc.drawImage(img,(tx-tx0)*TILE,(ty-ty0)*TILE);\n"
        "      done++;if(onTile)onTile(done,total);\n"
        "    }\n"
        "  }\n"
        "  var out=document.createElement('canvas');out.width=W;out.height=H;\n"
        "  var oc=out.getContext('2d');\n"
        "  oc.imageSmoothingEnabled=true;\n"
        "  var sx=px0-tx0*TILE,sw=px1-px0;\n"
        "  for(var dy=0;dy<H;dy++){\n"
        "    var latT=s.latMax-(s.latMax-s.latMin)*dy/H;\n"
        "    var latB=s.latMax-(s.latMax-s.latMin)*(dy+1)/H;\n"
        "    var syT=lat2px(latT,z)-ty0*TILE,syB=lat2px(latB,z)-ty0*TILE;\n"
        "    var sh=Math.max(1,syB-syT);\n"
        "    oc.drawImage(mos,sx,syT,sw,sh,0,dy,W,1);\n"
        "  }\n"
        "  return new Promise(function(res){out.toBlob(res,'image/png');});\n"
        "}\n"
        "function upload(code,blob,s){\n"
        "  var fd=new FormData();\n"
        "  fd.append('file',blob,code+'.png');\n"
        "  var q='?latMin='+s.latMin+'&latMax='+s.latMax+'&lonMin='+s.lonMin+'&lonMax='+s.lonMax;\n"
        "  return fetch('/state-map-upload'+q,{method:'POST',body:fd}).then(function(r){return r.json();});\n"
        "}\n"
        "function say(t){el.textContent=t;}\n"
        "async function run(){\n"
        "  btn.disabled=true;\n"
        "  var okN=0,failN=0;\n"
        "  for(var i=0;i<NEED.length;i++){\n"
        "    var s=NEED[i];\n"
        "    try{\n"
        "      var blob=await buildStatePng(s,ST.imageW,ST.imageH,function(d,t){\n"
        "        say('['+(i+1)+'/'+NEED.length+'] '+s.name+': tile '+d+' of '+t);\n"
        "      });\n"
        "      say('['+(i+1)+'/'+NEED.length+'] '+s.name+': uploading '+Math.round(blob.size/1024)+' KB');\n"
        "      var r=await upload(s.code,blob,s);\n"
        "      if(r&&r.ok){okN++;}else{failN++;}\n"
        "    }catch(e){failN++;}\n"
        "  }\n"
        "  say('Done. '+okN+' saved'+(failN?', '+failN+' failed':'')+'. Reload to refresh the list.');\n"
        "  btn.disabled=false;\n"
        "}\n"
        "fetch('/state-map-status').then(function(r){return r.json();}).then(function(d){\n"
        "  ST=d;\n"
        "  if(!d||!d.storage){say('No storage detected on the device.');return;}\n"
        "  var pts=(window.NODE_HEAT_POINTS||[]),need={};\n"
        "  pts.forEach(function(p){\n"
        "    var best=null,ba=1e9;\n"
        "    d.states.forEach(function(s){\n"
        "      if(p.lat<s.latMin||p.lat>s.latMax||p.lon<s.lonMin||p.lon>s.lonMax)return;\n"
        "      var a=(s.latMax-s.latMin)*(s.lonMax-s.lonMin);\n"
        "      if(!best||a<ba){best=s;ba=a;}});\n"
        "    if(best)need[best.code]=best;});\n"
        "  // Cached at a different resolution is stale, not finished: the device still\n"
        "  // draws it, just softly, and re-running the download replaces it in place.\n"
        "  function fresh(s){return s.cached && s.w==d.imageW && s.h==d.imageH;}\n"
        "  var codes=Object.keys(need).sort();\n"
        "  if(!codes.length){say('No known node reports a position inside the United States, so there is nothing to download yet.');return;}\n"
        "  NEED=codes.filter(function(c){return !fresh(need[c]);}).map(function(c){return need[c];});\n"
        "  var stale=codes.filter(function(c){return need[c].cached && !fresh(need[c]);}).length;\n"
        "  say(codes.length+' state'+(codes.length==1?'':'s')+' needed: '+\n"
        "      codes.map(function(c){\n"
        "        var s=need[c];\n"
        "        return s.name+(fresh(s)?' (cached)':s.cached?' (cached at '+s.w+'x'+s.h+', now '+d.imageW+'x'+d.imageH+')':'');\n"
        "      }).join(', ')+\n"
        "      '. '+(NEED.length?NEED.length+' to download'+(stale?' ('+stale+' to refresh at the higher resolution)':'')+'.':'All cached.'));\n"
        "  if(btn){\n"
        "    btn.disabled=!NEED.length;\n"
        "    btn.title=NEED.length?'Downloads '+NEED.length+' state map'+(NEED.length==1?'':'s')+' at '+d.imageW+'x'+d.imageH:'Every needed state is already cached at the current resolution';\n"
        "    btn.onclick=run;\n"
        "  }\n"
        "}).catch(function(){say('Could not read the map cache status.');});\n"
        "})();\n"
        "</script>";
#endif


    html += "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>"
            "<script src='https://unpkg.com/leaflet.heat@0.2.0/dist/leaflet-heat.js'></script>";

        html += "</div>";
#if HAS_VNC_HOST
        const String vncIp = WiFi.localIP().toString();
        html += "<div class='tab-panel' id='tab-remote'><div class='tab-pane-center'>"
            "<h3 style='margin-top:1.2em'>Remote</h3>"
            "<label class='remote-toggle'><input type='checkbox' id='remote-enabled' onchange='remoteToggle()'";
        if (vncHostEnabled()) html += " checked";
        html += "><span>Enable VNC host</span></label>"
            "<p id='remote-status' class='remote-status'>Checking remote host...</p>"
            "<iframe id='remote-frame' class='remote-frame' title='Remote control' data-src='http://";
        html += vncIp;
        html += ":";
        html += String(vncHostPort());
        html += "/' style='display:none'></iframe></div></div>";
#endif
        html += "<script>"
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
                                ".catch(function(err){"
                                    "var msg=(err&&err.message)?err.message:'network error';"
                                    "if(msg==='timeout')msg='request timed out';"
                                    "setReleaseCheckResult('Release check failed in browser: '+msg,'#ff9f9f');"
                                    "done();"
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
                            "if(tag==='G')return 'neighborinfo';"
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
                            "startChartPolling();"
                        "}"
                        "function stopLivePolling(){"
                            "if(!livePollTimer)return;"
                            "clearInterval(livePollTimer);"
                            "livePollTimer=null;"
                            "setLiveStatus('Paused');"
                            "stopChartPolling();"
                        "}"
                        "var chartPollTimer=null;"
                        "function drawChartFrame(ctx,w,h,pad){"
                            "ctx.clearRect(0,0,w,h);"
                            "ctx.fillStyle='#0a1a36';ctx.fillRect(0,0,w,h);"
                            "ctx.strokeStyle='#1f3a6a';ctx.lineWidth=1;"
                            "ctx.beginPath();"
                            "for(var i=0;i<=4;i++){"
                                "var y=pad.t+(h-pad.t-pad.b)*i/4;"
                                "ctx.moveTo(pad.l,y);ctx.lineTo(w-pad.r,y);"
                            "}"
                            "for(var j=0;j<=6;j++){"
                                "var x=pad.l+(w-pad.l-pad.r)*j/6;"
                                "ctx.moveTo(x,pad.t);ctx.lineTo(x,h-pad.b);"
                            "}"
                            "ctx.stroke();"
                        "}"
                        "function drawSeries(ctx,vals,cap,minV,maxV,pad,w,h,color){"
                            "if(!vals||vals.length<1)return;"
                            "if(maxV<=minV)maxV=minV+1;"
                            "var iw=w-pad.l-pad.r,ih=h-pad.t-pad.b;"
                            "var step=iw/Math.max(1,cap-1);"
                            "var offset=cap-vals.length;"
                            "ctx.strokeStyle=color;ctx.lineWidth=1.5;ctx.beginPath();"
                            "var started=false;"
                            "for(var i=0;i<vals.length;i++){"
                                "var idx=offset+i;"
                                "var x=pad.l+idx*step;"
                                "var nv=(vals[i]-minV)/(maxV-minV);"
                                "if(nv<0)nv=0;if(nv>1)nv=1;"
                                "var y=pad.t+(1-nv)*ih;"
                                "if(!started){ctx.moveTo(x,y);started=true;}"
                                "else ctx.lineTo(x,y);"
                            "}"
                            "ctx.stroke();"
                        "}"
                        "function drawYLabels(ctx,pad,w,h,minV,maxV,suffix,align){"
                            "ctx.fillStyle='#7ea1d4';ctx.font='10px ui-monospace,Menlo,Consolas,monospace';"
                            "ctx.textBaseline='middle';"
                            "for(var i=0;i<=4;i++){"
                                "var v=maxV-(maxV-minV)*i/4;"
                                "var y=pad.t+(h-pad.t-pad.b)*i/4;"
                                "var label=Math.round(v)+suffix;"
                                "if(align==='right'){ctx.textAlign='left';ctx.fillText(label,w-pad.r+2,y);}"
                                "else{ctx.textAlign='right';ctx.fillText(label,pad.l-2,y);}"
                            "}"
                        "}"
                        "function statsLine(name,unit,s){"
                            "if(!s||!s.values||s.values.length<1)return name+' --';"
                            "var sum=0,mn=Infinity,mx=-Infinity;"
                            "for(var i=0;i<s.values.length;i++){var v=s.values[i];sum+=v;if(v<mn)mn=v;if(v>mx)mx=v;}"
                            "var avg=sum/s.values.length;"
                            "var cur=(s.hasLast?s.last:s.values[s.values.length-1]);"
                            "return name+' cur '+cur.toFixed(1)+unit+'  avg '+avg.toFixed(1)+unit+'  min '+mn.toFixed(1)+unit+'  max '+mx.toFixed(1)+unit+'  n='+s.values.length;"
                        "}"
                        "function renderChUtilChart(d){"
                            "var c=document.getElementById('chart-chutil');if(!c)return;"
                            "var ctx=c.getContext('2d');"
                            "var w=c.width=c.clientWidth||600;var h=c.height=180;"
                            "var pad={l:36,r:10,t:8,b:14};"
                            "drawChartFrame(ctx,w,h,pad);"
                            "drawYLabels(ctx,pad,w,h,0,100,'%','left');"
                            "var cap=d.capacity||60;"
                            "drawSeries(ctx,(d.chUtil&&d.chUtil.values)||[],cap,0,100,pad,w,h,'#67d8ff');"
                            "drawSeries(ctx,(d.airUtil&&d.airUtil.values)||[],cap,0,100,pad,w,h,'#ffd56b');"
                            "var stats=document.getElementById('chart-chutil-stats');"
                            "if(stats)stats.textContent=statsLine('ChUtil','%',d.chUtil)+'   '+statsLine('AirTx','%',d.airUtil);"
                        "}"
                        "function seriesBounds(vals,fallbackLo,fallbackHi){"
                            "if(!vals||!vals.length)return[fallbackLo,fallbackHi];"
                            "var mn=Infinity,mx=-Infinity;"
                            "for(var i=0;i<vals.length;i++){var v=vals[i];if(v<mn)mn=v;if(v>mx)mx=v;}"
                            "var pad=(mx-mn)*0.15;if(pad<1)pad=1;"
                            "return[Math.floor(mn-pad),Math.ceil(mx+pad)];"
                        "}"
                        "function renderSnrChart(d){"
                            "var c=document.getElementById('chart-snr');if(!c)return;"
                            "var ctx=c.getContext('2d');"
                            "var w=c.width=c.clientWidth||600;var h=c.height=180;"
                            "var pad={l:40,r:48,t:8,b:14};"
                            "drawChartFrame(ctx,w,h,pad);"
                            "var snrVals=(d.snr&&d.snr.values)||[];"
                            "var rssiVals=(d.rssi&&d.rssi.values)||[];"
                            "var sb=seriesBounds(snrVals,-20,15);"
                            "var rb=seriesBounds(rssiVals,-130,-30);"
                            "drawYLabels(ctx,pad,w,h,sb[0],sb[1],'dB','left');"
                            "drawYLabels(ctx,pad,w,h,rb[0],rb[1],'dBm','right');"
                            "var cap=d.capacity||60;"
                            "drawSeries(ctx,snrVals,cap,sb[0],sb[1],pad,w,h,'#9ceadf');"
                            "drawSeries(ctx,rssiVals,cap,rb[0],rb[1],pad,w,h,'#f7b46d');"
                            "var stats=document.getElementById('chart-snr-stats');"
                            "if(stats)stats.textContent=statsLine('SNR','dB',d.snr)+'   '+statsLine('RSSI','dBm',d.rssi);"
                        "}"
                        "function pollChartData(){"
                            "fetch('/chart-data',{cache:'no-store',credentials:'same-origin'})"
                                ".then(function(r){if(!r.ok)throw r;return r.json();})"
                                ".then(function(d){renderChUtilChart(d);renderSnrChart(d);})"
                                ".catch(function(){});"
                        "}"
                        "function startChartPolling(){"
                            "if(chartPollTimer)return;"
                            "pollChartData();"
                            "chartPollTimer=setInterval(pollChartData,2000);"
                        "}"
                        "function stopChartPolling(){"
                            "if(!chartPollTimer)return;"
                            "clearInterval(chartPollTimer);"
                            "chartPollTimer=null;"
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
                        "var nodeMini=null;var nodeMiniMarker=null;"
                        "function ensureNodeMini(){"
                            "var el=document.getElementById('node-mini-map');"
                            "if(!el)return null;"
                            "if(!window.L){el.innerHTML='<div style=\"padding:1em;color:#ffd0d0\">Map library unavailable. Check internet access.</div>';return null;}"
                            "if(nodeMini){nodeMini.invalidateSize();return nodeMini;}"
                            "nodeMini=L.map('node-mini-map',{zoomControl:true,minZoom:2,maxZoom:19});"
                            "L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'&copy; OpenStreetMap contributors'}).addTo(nodeMini);"
                            "return nodeMini;"
                        "}"
                        "function showNodeMini(hasLoc,lat,lon){"
                            "var m=ensureNodeMini();"
                            "if(!m)return;"
                            "if(nodeMiniMarker){m.removeLayer(nodeMiniMarker);nodeMiniMarker=null;}"
                            "if(hasLoc&&isFinite(lat)&&isFinite(lon)){"
                                "m.setView([lat,lon],12);"
                                "nodeMiniMarker=L.marker([lat,lon]).addTo(m);"
                            "}else{"
                                // No node position: show a regional map (this device's area if known).
                                "var me=window.NODE_ME_POINT;"
                                "if(me&&isFinite(me.lat)&&isFinite(me.lon))m.setView([me.lat,me.lon],7);"
                                "else m.setView([39.5,-98.35],4);"
                            "}"
                            "setTimeout(function(){m.invalidateSize();},50);"
                        "}"
                        "function selectNode(){"
                            "var sel=document.getElementById('node-select');"
                            "var info=document.getElementById('node-info');"
                            "if(!sel||!info)return;"
                            "if(sel.value===''){info.innerHTML='';return;}"
                            "var d=document.getElementById('nd-'+sel.value);"
                            "if(!d){info.innerHTML='';return;}"
                            "info.innerHTML=d.innerHTML;"
                            "showNodeMini(d.getAttribute('data-loc')==='1',parseFloat(d.getAttribute('data-lat')),parseFloat(d.getAttribute('data-lon')));"
                        "}"
                        // ── Favorite toggle ───────────────────────────────────
                        // The detail panels are cloned out of the hidden store
                        // into #node-info, so every node has two copies of its
                        // card in the DOM. Updates go by node id across the whole
                        // document rather than to the clicked button, or the copy
                        // that was not clicked goes stale and reappears wrong the
                        // next time the node is selected.
                        "function applyNodeFavorite(id,fav){"
                            "var i,els;"
                            "els=document.querySelectorAll(\"[data-fav-btn='\"+id+\"']\");"
                            "for(i=0;i<els.length;i++){"
                                "els[i].setAttribute('data-fav',fav?'1':'0');"
                                "els[i].textContent=fav?'Unfavorite':'Favorite';"
                            "}"
                            "els=document.querySelectorAll(\"[data-fav-tag='\"+id+\"']\");"
                            "for(i=0;i<els.length;i++)els[i].style.display=fav?'':'none';"
                            "var opts=document.querySelectorAll(\"#node-select option[data-nid='\"+id+\"']\");"
                            "for(i=0;i<opts.length;i++)opts[i].setAttribute('data-fav',fav?'1':'0');"
                            "resortNodeOptions();"
                        "}"
                        // Re-partitions the dropdown the way the device ranks it:
                        // favorites first, everything else after, relative order
                        // untouched inside each group (the server's secondary keys
                        // — named-ness, then recency — still hold within a group,
                        // and nothing here reorders across one). appendChild moves
                        // existing nodes, so no option is rebuilt and the values
                        // that map to nd-<rank> are never touched.
                        "function resortNodeOptions(){"
                            "var sel=document.getElementById('node-select');"
                            "if(!sel)return;"
                            "var keep=sel.value,favs=[],rest=[],div=null,i,o;"
                            "for(i=0;i<sel.options.length;i++){"
                                "o=sel.options[i];"
                                "if(o.getAttribute('data-fav-divider')==='1'){div=o;continue;}"
                                "if(!o.hasAttribute('data-nid'))continue;"   // the placeholder
                                "if(o.getAttribute('data-fav')==='1')favs.push(o);else rest.push(o);"
                            "}"
                            "if(div&&div.parentNode)div.parentNode.removeChild(div);"
                            "for(i=0;i<favs.length;i++)sel.appendChild(favs[i]);"
                            "if(favs.length&&rest.length){"
                                "if(!div){"
                                    "div=document.createElement('option');"
                                    "div.disabled=true;"
                                    "div.setAttribute('data-fav-divider','1');"
                                    "div.textContent='\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014\\u2014';"
                                "}"
                                "sel.appendChild(div);"
                            "}"
                            "for(i=0;i<rest.length;i++)sel.appendChild(rest[i]);"
                            "sel.value=keep;"
                        "}"
                        "function toggleNodeFavorite(btn){"
                            "var id=btn.getAttribute('data-fav-btn');"
                            "var next=btn.getAttribute('data-fav')==='1'?'0':'1';"
                            "if(!id)return;"
                            // Every copy of this node's button, not just the one
                            // clicked: they are the same action in flight.
                            "var btns=document.querySelectorAll(\"[data-fav-btn='\"+id+\"']\"),i;"
                            "for(i=0;i<btns.length;i++)btns[i].disabled=true;"
                            "var done=function(){for(var j=0;j<btns.length;j++)btns[j].disabled=false;};"
                            "fetch('/node-favorite',{method:'POST',"
                                "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
                                "body:'id='+encodeURIComponent(id)+'&fav='+next})"
                            ".then(function(r){return r.json();})"
                            ".then(function(j){"
                                "if(!j||!j.ok)throw new Error(j&&j.error?j.error:'failed');"
                                "applyNodeFavorite(id,j.fav===1);"
                                "done();"
                            "})"
                            ".catch(function(e){"
                                "done();"
                                "alert('Could not change favorite: '+(e&&e.message?e.message:e));"
                            "});"
                        "}"
                        // ── Chat tab (omitted on Cardputer — no PSRAM) ────────
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
                        // Mirrored by kTapbackTray in main_lvgl.cpp, which is what
                        // the device's Message Actions menu offers. Same set, same
                        // order — keep the two in step so a reaction means the same
                        // thing whichever side sent it.
                        "var CHAT_TAPS=['\\uD83D\\uDC4D','\\uD83D\\uDC4E','\\u203C\\uFE0F','\\u2753','\\uD83D\\uDE02','\\uD83D\\uDE22'];"
                        "var chatIsDm=false;var chatId='';var chatCursor=-1;var chatLines=[];"
                        "var chatReplyPid=0;var chatPreview={};var chatTimer=null;var chatTargetsTimer=null;"
                        "function chatEsc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
                        "function chatTargetChanged(){"
                            "var sel=document.getElementById('chat-target');var v=sel?sel.value:'';"
                            "chatLines=[];chatCursor=-1;chatReplyPid=0;chatPreview={};"
                            "chatCancelReply();"
                            "var feed=document.getElementById('chat-feed');if(feed)feed.innerHTML='';"
                            "if(v===''){chatId='';return;}"
                            "chatIsDm=(v.charAt(0)==='d');chatId=v.substring(2);"
                            "pollChat();"
                        "}"
                        "function loadChatTargets(){"
                            "fetch('/chat-targets',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
                                "var sel=document.getElementById('chat-target');if(!sel)return;"
                                "if(document.activeElement===sel)return;"   // don't rebuild while the user is choosing
                                "var cur=sel.value;var h='<option value=\"\">-- select --</option>';"
                                "if(d.channels&&d.channels.length){h+='<optgroup label=\"Channels\">';"
                                    "d.channels.forEach(function(c){h+='<option value=\"c:'+c.id+'\">'+chatEsc(c.name)+'</option>';});h+='</optgroup>';}"
                                "if(d.dms&&d.dms.length){h+='<optgroup label=\"Direct Messages\">';"
                                    "d.dms.forEach(function(c){h+='<option value=\"d:'+c.id+'\">'+chatEsc(c.name)+'</option>';});h+='</optgroup>';}"
                                "sel.innerHTML=h;if(cur)sel.value=cur;"
                            "}).catch(function(){});"
                        "}"
                        "function pollChat(){"
                            "if(chatId==='')return;"
                            "var q=chatIsDm?('dm='+encodeURIComponent(chatId)):('ch='+encodeURIComponent(chatId));"
                            "var url='/chat-data?'+q+(chatCursor>=0?('&after='+chatCursor):'');"
                            "fetch(url,{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){"
                                "if(!d||!d.lines)return;"
                                "for(var i=0;i<d.lines.length;i++)chatLines.push(d.lines[i]);"
                                "if(typeof d.total==='number')chatCursor=d.total-1;"
                                "chatRender();"
                            "}).catch(function(){});"
                        "}"
                        "function chatBubble(pid,mine,txt){"
                            "if(pid!==0)chatPreview[pid]=txt;"
                            "var s='<div class=\"chat-msg'+(mine?' mine':'')+'\"><div class=\"chat-body\">'+chatEsc(txt)+'</div>';"
                            // No reply/tapback on our own messages.
                            "if(pid!==0&&!mine){s+='<div class=\"chat-actions\">';"
                                "s+='<button type=\"button\" class=\"chat-mini\" onclick=\"chatReply('+pid+')\">Reply</button>';"
                                "for(var k=0;k<CHAT_TAPS.length;k++)s+='<button type=\"button\" class=\"chat-tap\" onclick=\"chatTapback('+pid+','+k+')\">'+CHAT_TAPS[k]+'</button>';"
                                "s+='</div>';}"
                            "return s+'</div>';"
                        "}"
                        "function chatRender(){"
                            "var feed=document.getElementById('chat-feed');if(!feed)return;"
                            // Don't rebuild the feed while the user is composing — reassigning
                            // innerHTML blurs the focused input / drops the mobile keyboard.
                            "var ae=document.activeElement;if(ae&&ae.id==='chat-input')return;"
                            "var atBottom=feed.scrollHeight-feed.scrollTop-feed.clientHeight<40;"
                            "var h='';var i=0;var L=chatLines;"
                            // A wrapped message is stored one line per wrap, and
                            // ChannelMgr::_wordWrap() pushes those lines LAST SEGMENT
                            // FIRST — the device draws its feed newest-at-top, so
                            // reversed in storage is correct order on the glass. Read
                            // the group back to front to put it together again.
                            // L[j-1] is the opening line: it carries the "HH:MM [id] "
                            // prefix and is the only line given an ack state, so it is
                            // also where `mine` has to come from.
                            "while(i<L.length){var pid=L[i].pid;var j=i+1;"
                                "if(pid!==0){while(j<L.length&&L[j].pid===pid)j++;}"
                                "var head=L[j-1];var txt=head.t;"
                                "for(var k=j-2;k>=i;k--){var seg=L[k].t.replace(/^\\s{1,2}/,'');if(seg)txt+=' '+seg;}"
                                "h+=chatBubble(pid,!!head.mine,txt);i=j;}"
                            "feed.innerHTML=h;if(atBottom)feed.scrollTop=feed.scrollHeight;"
                        "}"
                        "function chatReply(pid){"
                            "chatReplyPid=pid;var bar=document.getElementById('chat-reply-bar');"
                            "var t=document.getElementById('chat-reply-text');"
                            "if(t)t.textContent='Replying: '+String(chatPreview[pid]||'').substring(0,60);"
                            "if(bar)bar.style.display='flex';"
                            "var inp=document.getElementById('chat-input');if(inp){inp.scrollIntoView({block:'nearest'});inp.focus();}"
                        "}"
                        "function chatCancelReply(){chatReplyPid=0;var bar=document.getElementById('chat-reply-bar');if(bar)bar.style.display='none';}"
                        "function chatPost(text,reply,emoji){"
                            "if(chatId==='')return;"
                            "var body='dm='+(chatIsDm?'1':'0')+'&id='+encodeURIComponent(chatId)+'&text='+encodeURIComponent(text)+'&reply='+(reply||0)+'&emoji='+(emoji||0);"
                            "fetch('/chat-send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
                                ".then(function(){setTimeout(pollChat,600);}).catch(function(){});"
                        "}"
                        "function chatTapback(pid,idx){chatPost(CHAT_TAPS[idx],pid,1);}"
                        "function chatSend(){"
                            "var inp=document.getElementById('chat-input');if(!inp)return;var t=inp.value.trim();if(!t)return;"
                            "chatPost(t,chatReplyPid,0);inp.value='';chatCancelReply();"
                        "}"
                        "function chatLiveOn(){var c=document.getElementById('chat-live');return c?c.checked:true;}"
                        "function chatStartTimers(){if(!chatTimer)chatTimer=setInterval(pollChat,2000);if(!chatTargetsTimer)chatTargetsTimer=setInterval(loadChatTargets,5000);}"
                        "function chatStopTimers(){if(chatTimer){clearInterval(chatTimer);chatTimer=null;}if(chatTargetsTimer){clearInterval(chatTargetsTimer);chatTargetsTimer=null;}}"
                        "function chatBodySync(){var b=document.getElementById('chat-body');if(b)b.style.display=chatLiveOn()?'':'none';}"
                        "function chatLiveToggle(){chatBodySync();if(chatLiveOn()){loadChatTargets();if(chatId!=='')pollChat();chatStartTimers();}else{chatStopTimers();}}"
                        "function startChatPolling(){chatBodySync();loadChatTargets();if(chatId!=='')pollChat();if(chatLiveOn())chatStartTimers();}"
                        "function stopChatPolling(){chatStopTimers();}"
#endif
#if HAS_VNC_HOST
                        "var remotePollTimer=null;"
                        "function remoteFrameSync(on){"
                            "var frame=document.getElementById('remote-frame');"
                            "var panel=document.getElementById('tab-remote');"
                            "if(!frame)return;"
                            "var active=!!(panel&&panel.classList.contains('active'));"
                            "frame.style.display=(on&&active)?'block':'none';"
                            "if(on&&active){if(!frame.getAttribute('src'))frame.setAttribute('src',frame.getAttribute('data-src'));}"
                            "else if(frame.getAttribute('src'))frame.removeAttribute('src');"
                        "}"
                        "function remoteRender(d){"
                            "var box=document.getElementById('remote-enabled');"
                            "var status=document.getElementById('remote-status');"
                            "if(!box)return;"
                            "box.disabled=!!d.pending;box.checked=!!d.enabled;"
                            "if(status){"
                                "if(d.pending)status.textContent='Applying...';"
                                "else if(!d.enabled)status.textContent='Remote host is off.';"
                                "else if(d.client)status.textContent='Browser connected.';"
                                "else if(d.running)status.textContent='Ready at http://'+d.ip+':'+d.port+'/';"
                                "else status.textContent='Starting remote host...';"
                            "}"
                            "remoteFrameSync(!!d.enabled);"
                        "}"
                        "function pollRemote(){"
                            "fetch('/vnc-status',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error();return r.json();})"
                                ".then(remoteRender).catch(function(){var s=document.getElementById('remote-status');if(s)s.textContent='Remote status unavailable.';});"
                        "}"
                        "function remoteToggle(){"
                            "var box=document.getElementById('remote-enabled');if(!box)return;"
                            "box.disabled=true;var desired=box.checked;"
                            "var status=document.getElementById('remote-status');if(status)status.textContent=desired?'Starting...':'Stopping...';"
                            "fetch('/vnc-toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'enabled='+(desired?'1':'0')})"
                                ".then(function(r){if(!r.ok)throw new Error();setTimeout(pollRemote,150);})"
                                ".catch(function(){box.disabled=false;pollRemote();});"
                        "}"
                        "function startRemotePolling(){pollRemote();if(!remotePollTimer)remotePollTimer=setInterval(pollRemote,1000);}"
                        "function stopRemotePolling(){if(remotePollTimer){clearInterval(remotePollTimer);remotePollTimer=null;}remoteFrameSync(false);}"
#endif
                        "function switchTab(tab){"
                            "var isCfg=(tab==='config');"
                            "var isWifi=(tab==='wifi');"
                            "var isUtil=(tab==='utils');"
                            "var isLive=(tab==='live');"
                            "var isMap=(tab==='map');"
                            "document.getElementById('tab-config').classList.toggle('active',isCfg);"
                            "document.getElementById('tab-wifi').classList.toggle('active',isWifi);"
                            "document.getElementById('tab-utils').classList.toggle('active',isUtil);"
                            "document.getElementById('tab-live').classList.toggle('active',isLive);"
                            "document.getElementById('tab-map').classList.toggle('active',isMap);"
                            "document.getElementById('tab-btn-config').classList.toggle('active',isCfg);"
                            "document.getElementById('tab-btn-wifi').classList.toggle('active',isWifi);"
                            "document.getElementById('tab-btn-utils').classList.toggle('active',isUtil);"
                            "document.getElementById('tab-btn-live').classList.toggle('active',isLive);"
                            "document.getElementById('tab-btn-map').classList.toggle('active',isMap);"
                            "if(isLive)startLivePolling();else stopLivePolling();"
#if HAS_VNC_HOST
                            "var isRemote=(tab==='remote');"
                            "document.getElementById('tab-remote').classList.toggle('active',isRemote);"
                            "document.getElementById('tab-btn-remote').classList.toggle('active',isRemote);"
                            "if(isRemote)startRemotePolling();else stopRemotePolling();"
#endif
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
                            "var isChat=(tab==='chat');"
                            "document.getElementById('tab-chat').classList.toggle('active',isChat);"
                            "document.getElementById('tab-btn-chat').classList.toggle('active',isChat);"
                            "if(isChat)startChatPolling();else stopChatPolling();"
#endif
                            "if(isMap){"
                                "ensureNodeMap();"
                                "var _s=document.getElementById('node-select');"
                                "if(document.getElementById('node-mini-map')){if(_s&&_s.value!==''){selectNode();}else{showNodeMini(false);}}"
                                "setTimeout(function(){if(nodeMap)nodeMap.invalidateSize();if(nodeMini)nodeMini.invalidateSize();},0);"
                            "}"
                        "}"
                        "window.addEventListener('resize',function(){"
                            "var mapTab=document.getElementById('tab-map');"
                            "if(mapTab&&mapTab.classList.contains('active')&&nodeMap)nodeMap.invalidateSize();"
                        "});"
                        "</script>";

    html += "</body></html>";
    sendChunk(html);
    if (gSendAborted) {
        server.client().stop();
        logWifiHeapDiag("config page abandoned");
        return;
    }
    server.sendContent("");   // empty chunk signals end of response
    logWifiHeapDiag("config page sent");
}

// ── Onboarding (WiFi setup) ───────────────────────────────────

static void sendOnboardingPage(const char *err = "") {
    gSendAborted = false;
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    sendFlash(kLiteHead);

    String html;
    html.reserve(480);
    html +=
        "<h2>WiFi Setup</h2>"
        "<form method='POST' action='/onboard'>"
        "<p><label>SSID</label><br>"
        "<input name='ssid' type='text' autofocus autocomplete='off' placeholder='MyNetwork'></p>"
        "<p><label>Password</label><br>"
        "<input name='pass' type='password' autocomplete='off'>"
        "</p><p><button type='submit'>Save and Reboot</button></p>";
    if (err && err[0]) {
        html += "<p>";
        html += err;
        html += "</p>";
    }
    html += "</form></body></html>";
    sendChunk(html);
    server.sendContent("");   // terminate chunked response
}

static void handleGetOnboard() {
    sendOnboardingPage();
}

static void handleGetConfig() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    char msg[sizeof(gFlashMsg)];
    strncpy(msg, gFlashMsg, sizeof(msg));
    msg[sizeof(msg) - 1] = '\0';
    gFlashMsg[0] = '\0';
    sendConfigPage(msg, /*lite=*/webCfgUseLite());
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

    // Write the settings blob too, via the same callback every other save path
    // uses. The standalone keys above are the pre-blob format: on a device that
    // has already migrated they are ignored at load, so onboarding alone left
    // the credentials nowhere the next boot would look and the setup AP came
    // straight back. A freshly erased device has no blob yet, takes the legacy
    // read path, and is why this only reproduced after an upgrade-in-place.
    // Synchronous on purpose — the debounced markConfigDirty() path would not
    // survive the ESP.restart() below.
    if (gOnSave) gOnSave();

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    sendFlash(kLiteHead);

    String html;
    html +=
        "<h2>Saved</h2>"
        "<p>The device is rebooting and will connect to <b>";
    html += ssid;
    html +=
        "</b>.</p>"
        "</body></html>";
    sendChunk(html);
    server.sendContent("");   // terminate chunked response
    server.stop();
    delay(500);
    // Transcript snapshots are debounced; anything inside the current quiet
    // window would not survive this restart otherwise.
    Channels.flushPersistence();
    DMs.flushPersistence();
    ESP.restart();
}

// ── Route handlers ────────────────────────────────────────────

static void handleGetRoot() {
    // Both radio modes serve a config page from "/": the full one in STA, web
    // config lite in AP. The phone's captive-portal probes are answered
    // separately by onNotFound with a redirect to the minimal /setup form, so
    // they never pay for building this page.
    Serial.printf("[web] GET / (%s)\n", gApMode ? "lite" : "full");
    handleGetConfig();
}

static void handleGetLogin() {
    if (isLoggedIn()) {
        redirect("/");
        return;
    }
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

    // Snapshot for the reboot decision below. Taken before any field is written
    // so it can be compared against the applied result.
    const RhinoConfig cfgBefore = *gCfg;

    // Node identity
    String lng  = server.arg("long");
    String shrt = server.arg("short");
    utf8util::copyTruncate(gCfg->nodeLong, sizeof(gCfg->nodeLong), lng.c_str());
    utf8util::copyTruncate(gCfg->nodeShort, sizeof(gCfg->nodeShort), shrt.c_str());

    // Web config auth (username is fixed to admin)
    // Unchecked checkboxes are not submitted, so absence means "disabled".
    gCfg->webCfgAuthEnabled = server.hasArg("web_auth");
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
    gCfg->deviceRole        = cfgCoerceClientRole((uint8_t)server.arg("role").toInt());
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
#if HAS_NODE_LOS
    // Trailing slashes are stripped by the fetcher, so both forms are accepted.
    strncpy(gCfg->losElevServer, server.arg("los_elev").c_str(),
            sizeof(gCfg->losElevServer) - 1);
    gCfg->losElevServer[sizeof(gCfg->losElevServer) - 1] = '\0';
#endif
    if (!gCfg->ntpServer[0]) {
        strncpy(gCfg->ntpServer, MY_NTP_SERVER, sizeof(gCfg->ntpServer) - 1);
        gCfg->ntpServer[sizeof(gCfg->ntpServer) - 1] = '\0';
    }

    // Time source, and the clock itself when set manually. The clock is queued
    // rather than set here: the main loop owns it, and this handler runs on the
    // web server task.
    if (server.hasArg("time_source")) {
        gCfg->timeSource = cfgCoerceTimeSource(server.arg("time_source").toInt());
    }
    if (gCfg->timeSource == TIME_SOURCE_MANUAL
        && server.hasArg("manual_date") && server.hasArg("manual_time")) {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0;
        // <input type=date> gives YYYY-MM-DD, <input type=time> gives HH:MM.
        if (sscanf(server.arg("manual_date").c_str(), "%d-%d-%d", &y, &mo, &d) == 3
            && sscanf(server.arg("manual_time").c_str(), "%d:%d", &h, &mi) == 2) {
            gManualTime[0] = y;
            gManualTime[1] = mo;
            gManualTime[2] = d;
            gManualTime[3] = h;
            gManualTime[4] = mi;
            gManualTimeReq = true;
        }
    }

    // Position
    gCfg->shareLocation = (server.arg("shareLocation") == "1");
    // hasArg-guarded, unlike the checkbox above: a checkbox that posts nothing
    // means "off", but a select that posts nothing means the field was never on
    // the page. Reading it unconditionally would turn any partial form — a lite
    // render, a truncated send — into a silent reset to exact coordinates,
    // which is the one direction this setting must never move on its own.
    // Coerced rather than trusted: it decides how much of the operator's real
    // position goes on the air.
    if (server.hasArg("pos_precision")) {
        gCfg->positionPrecision =
            positionPrecisionCoerce((uint8_t)server.arg("pos_precision").toInt());
    }
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
        // Hop limit. Guarded by hasArg, not by an empty-string test: a select
        // always submits something when it was rendered, and an absent field
        // means this form did not carry the channels section at all — which must
        // leave the stored value alone rather than reset it to Default.
        snprintf(field, sizeof(field), "ch%d_hop", i);
        if (server.hasArg(field)) {
            const String hv = server.arg(field);
            if (hv.length() == 0) {
                CHANNEL_KEYS[i].hopLimitPlus1 = 0;      // Default
            } else {
                CHANNEL_KEYS[i].hopLimitPlus1 =
                    chanHopLimitMake((uint8_t)constrain(hv.toInt(), 0, 7));
            }
        }
        // MQTT uplink/downlink and position sharing — apply only when the channels
        // section was rendered (unchecked boxes submit nothing, so we can't
        // distinguish off from absent).
        if (server.hasArg("ch_flags")) {
            snprintf(field, sizeof(field), "ch%d_up", i);
            CHANNEL_KEYS[i].uplinkEnabled = (server.arg(field) == "1");
            snprintf(field, sizeof(field), "ch%d_down", i);
            CHANNEL_KEYS[i].downlinkEnabled = (server.arg(field) == "1");
            snprintf(field, sizeof(field), "ch%d_loc", i);
            CHANNEL_KEYS[i].shareLocation = (server.arg(field) == "1");
        }
        // Recompute on-air hash from current name + key
        const char *nm2 = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
        CHANNEL_KEYS[i].hash = computeChannelHash(nm2, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
    }

    // Region
    String rgn = server.arg("region");
    if (rgn.length() > 0 && rgn.length() < sizeof(gCfg->region))
        strncpy(gCfg->region, rgn.c_str(), sizeof(gCfg->region) - 1);

    // LoRa — derive freq/BW/SF/CR from region + preset; keep power and hop manual
    gCfg->okToMqtt    = (server.arg("ok_to_mqtt")   == "1");
    gCfg->ignoreMqtt  = (server.arg("ignore_mqtt")  == "1");
    String presetStr = server.arg("modem_preset");
    gCfg->loraUsePreset = (presetStr != "Custom");
    if (gCfg->loraUsePreset) {
        gCfg->modemPreset = presetFromName(presetStr.c_str());
    } else {
        // modemPreset is deliberately left alone: it is what Custom is edited
        // away from, and what picking a preset again comes back to.
        if (server.hasArg("cbw"))   gCfg->loraCustomBwKhz = (uint16_t)server.arg("cbw").toInt();
        if (server.hasArg("csf"))   gCfg->loraCustomSf    = (uint8_t)server.arg("csf").toInt();
        if (server.hasArg("ccr"))   gCfg->loraCustomCr    = (uint8_t)server.arg("ccr").toInt();
        if (server.hasArg("cslot")) gCfg->loraCustomSlot  =
            (uint8_t)constrain(server.arg("cslot").toInt(), 0, 255);
    }
    // Sets loraFreq/BW/SF/CR from region + preset, or from the custom fields;
    // also coerces every custom value into what this radio can be tuned to.
    applyPresetParams(*gCfg);
    gCfg->loraPower    = (uint8_t)constrain(server.arg("pwr").toInt(), 1, 22);
    gCfg->loraHopLimit = (uint8_t)constrain(server.arg("hop").toInt(), 1,  7);

    // Network / MQTT. Text fields apply whenever present; checkbox state is only
    // meaningful when the MQTT section was rendered (mqtt_present marker), since
    // an unchecked box submits no value at all.
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
    if (server.hasArg("mqtt_port")) {
        long port = server.arg("mqtt_port").toInt();
        if (port >= 1 && port <= 65535) gCfg->mqttPort = (uint16_t)port;
    }
    if (server.hasArg("mqtt_present")) {
        gCfg->mqttEnabled    = (server.arg("mqtt_en")         == "1");
        // No TLS client in this firmware; clear any stale flag from an older
        // config so nothing implies MQTT is running over TLS.
        gCfg->mqttTls        = false;
        gCfg->mqttEncryption = (server.arg("mqtt_encrypt")    == "1");
        gCfg->mqttMapReport  = (server.arg("mqtt_map_report") == "1");
    }

    // Display
#if HAS_VOLUME_CONTROL
    // hasArg-guarded like brightness: a form rendered without the control (or a
    // partial POST) must leave the stored value alone rather than zero it.
    if (server.hasArg("volume")) {
        gCfg->volumePct = cfgCoerceVolume(server.arg("volume").toInt());
    }
#endif
    if (server.hasArg("brightness")) {
        gCfg->brightness = cfgCoerceBrightness(server.arg("brightness").toInt());
    }
    gCfg->screenOnSecs    = (uint32_t)server.arg("screen_on").toInt();
    // Kept because the auto-favorite radius below is submitted in whatever units
    // the page was *rendered* in, which is not necessarily the units this same
    // POST is switching to.
    const uint8_t prevUnits = gCfg->displayUnits;
    gCfg->displayUnits    = server.arg("disp_units").toInt() != 0 ? 1 : 0;
    if (server.hasArg("batt_display")) {
        gCfg->battDisplayMode =
            (uint8_t)constrain(server.arg("batt_display").toInt(), 0, BATT_DISPLAY_MAX);
    }
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    if (server.hasArg("chat_style")) {
        long cs = server.arg("chat_style").toInt();
        if (cs < 0 || cs > CHAT_STYLE_MAX) cs = CHAT_STYLE_CLASSIC;
        gCfg->chatStyle = (uint8_t)cs;
    }
#endif
    if (server.hasArg("chat_names")) {
        long cn = server.arg("chat_names").toInt();
        if (cn < 0 || cn > CHAT_NAME_MAX) cn = CHAT_NAME_SHORT;
        gCfg->chatNameStyle = (uint8_t)cn;
    }
    if (server.hasArg("font_size")) {
        gCfg->fontSize = (uint8_t)constrain(server.arg("font_size").toInt(), 0, FONT_SIZE_MAX);
    }
    // Only honor the archive checkbox when it was actually enabled in the form.
    // A disabled checkbox submits nothing, which is indistinguishable from
    // "unchecked" — so without this guard, saving on a card-less device would
    // silently clear a preference the user set while a card was present.
    if (nodeArchiveSlotExists() && nodeArchiveAvailable()) {
        gCfg->nodeArchiveEnabled = server.hasArg("node_archive");
    }
    gCfg->autoFavoriteEnabled = server.hasArg("autofav");
    if (server.hasArg("autofav_range")) {
        // Shown in km or miles per the Units setting; stored in meters. Read in
        // prevUnits: the number in the box is the one the browser was shown, so
        // switching Metric -> Imperial in this same save must not reinterpret a
        // "1.00" that meant kilometres as miles.
        const double v = server.arg("autofav_range").toDouble();
        if (v > 0.0) {
            const double perUnit = (prevUnits != 0) ? 1609.344 : 1000.0;
            double meters = v * perUnit;
            if (meters < 50.0) meters = 50.0;             // below this GPS noise dominates
            if (meters > 800000.0) meters = 800000.0;     // 800 km / ~500 mi ceiling
            gCfg->autoFavoriteRangeM = (uint32_t)(meters + 0.5);
        }
    }
    // Units changed and the radius is still stock: re-round it to the new
    // system's 1 km / 1 mi so the field doesn't come back reading 0.62 or 1.61.
    // Anything the user typed is left exactly where they put it.
    if (gCfg->displayUnits != prevUnits
        && cfgAutoFavRangeIsDefault(gCfg->autoFavoriteRangeM)) {
        gCfg->autoFavoriteRangeM = cfgDefaultAutoFavRangeM(gCfg->displayUnits);
    }
    // hasArg-guarded like the other conditional controls. Unguarded, a form
    // rendered without this control (a board with no audio) posted an empty
    // string here, which reads back as 0 and turned the setting off on every
    // save.
    if (server.hasArg("splash_melody")) {
        gCfg->splashMelodyEnabled = server.arg("splash_melody").toInt() != 0;
    }
    // hasArg-guarded for the same reason as splash_melody above: the control is
    // only rendered where the bar is optional, and an absent field would
    // otherwise read back as 0 and switch it off on every save.
    if (server.hasArg("nav_bar")) {
        gCfg->navBarEnabled = server.arg("nav_bar").toInt() != 0;
    }
#if defined(DEVICE_MESH_DECK)
    // Legacy compatibility for older cached pages that still post notify_led:
    // the new model is per-type control, so turning the old global toggle off
    // means both types become Off.
    if (server.hasArg("notify_led") && server.arg("notify_led").toInt() == 0) {
        gCfg->notifyLedColorChannel = NOTIFY_LED_COLOR_OFF;
        gCfg->notifyLedColorDm = NOTIFY_LED_COLOR_OFF;
    }
    if (server.hasArg("notify_led_channel_color")) {
        gCfg->notifyLedColorChannel =
            cfgCoerceNotifyLedColor(server.arg("notify_led_channel_color").toInt());
    }
    if (server.hasArg("notify_led_dm_color")) {
        gCfg->notifyLedColorDm =
            cfgCoerceNotifyLedColor(server.arg("notify_led_dm_color").toInt());
    }
#endif
#if HAS_KB_BLINK
    if (server.hasArg("kb_blink")) {
        gCfg->kbBlinkEnabled = server.arg("kb_blink").toInt() != 0;
    }
    if (server.hasArg("kb_flash_chan")) {
        gCfg->kbBlinkChanFlashes = cfgCoerceKbFlashes(server.arg("kb_flash_chan").toInt());
    }
    if (server.hasArg("kb_flash_dm")) {
        gCfg->kbBlinkDmFlashes = cfgCoerceKbFlashes(server.arg("kb_flash_dm").toInt());
    }
#endif
#if HAS_LIGHT_NOTIFY
    if (server.hasArg("light_timeout")) {
        gCfg->notifyLightTimeoutS =
            cfgCoerceNotifyLightTimeout(server.arg("light_timeout").toInt());
    }
#endif
#if HAS_SCROLL_INVERT
    if (server.hasArg("invert_scroll")) {   // absent on boards with no trackball
        gCfg->invertScroll = server.arg("invert_scroll").toInt() != 0;
    }
#endif
    // Legacy compatibility: only apply chat spacing if an older web form sends it.
    if (server.hasArg("chat_space")) {
        gCfg->chatSpacing = (uint8_t)constrain(server.arg("chat_space").toInt(), 0, 2);
    }
    if (server.hasArg("ui_theme_preset")) {
        // kThemePresetTotal, not kThemePresetCount: the custom slots live past
        // the built-ins, and clamping to the old bound would quietly rewrite
        // every custom selection to the last preset.
        uint8_t preset = (uint8_t)constrain(server.arg("ui_theme_preset").toInt(),
                                            0,
                                            (int)kThemePresetTotal - 1);
        themePresetToConfig(preset, gCfg->uiTheme, gCfg->uiMode);
    } else {
        // Backward-compatible fallback for older forms.
        gCfg->uiTheme = (uint8_t)constrain(server.arg("ui_theme").toInt(), 0, UI_THEME_COUNT - 1);
        gCfg->uiMode  = (uint8_t)(server.arg("ui_mode").toInt() != 0 ? UI_MODE_LIGHT : UI_MODE_DARK);
        if (uiThemeForcesDark(gCfg->uiTheme)) gCfg->uiMode = UI_MODE_DARK;
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

    uint32_t neighborInfoIntervalS = gCfg->neighborInfoIntervalS;
    if (server.hasArg("neighbor_info_mins")) {
        long mins = server.arg("neighbor_info_mins").toInt();
        if (mins < 240L) mins = 240L;
        neighborInfoIntervalS = (uint32_t)mins * 60UL;
    } else if (server.hasArg("neighbor_info_intv")) {
        long sec = server.arg("neighbor_info_intv").toInt();
        if (sec < (long)NEIGHBORINFO_MIN_INTERVAL_S) sec = (long)NEIGHBORINFO_MIN_INTERVAL_S;
        neighborInfoIntervalS = (uint32_t)sec;
    }
    if (neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }
    if (server.hasArg("neighbor_info_en")) {
        gCfg->neighborInfoEnabled = server.arg("neighbor_info_en").toInt() != 0;
    }
    if (server.hasArg("mesh_beacon_listen")) {
        gCfg->meshBeaconListen = server.arg("mesh_beacon_listen").toInt() != 0;
    }
    gCfg->neighborInfoIntervalS = neighborInfoIntervalS;
    if (server.hasArg("neighbor_info_lora")) {
        gCfg->neighborInfoOverLora = server.arg("neighbor_info_lora").toInt() != 0;
    }

    if (server.hasArg("ota_autocheck")) {
        gCfg->otaAutoCheckEnabled = server.arg("ota_autocheck").toInt() != 0;
    }
    if (server.hasArg("snf_router_id")) {
        // Blank, "none" or unparseable all come back 0, which is "unset" — the
        // way to hand router selection back to heartbeat discovery. Shared with
        // the YAML importer so a hand-edited config.yaml and this box accept
        // exactly the same spellings.
        gCfg->snfRouterNodeId = parseNodeIdText(server.arg("snf_router_id").c_str());
    }
    if (server.hasArg("snf_client_en")) {
        gCfg->snfClientEnabled = server.arg("snf_client_en").toInt() != 0;
    }
#if HAS_AUDIO_ALERTS
    if (server.hasArg("msg_alert_sound")) {   // same guard, same reason
        gCfg->msgAlertSound  = (uint8_t)constrain(server.arg("msg_alert_sound").toInt(), 0, 3);
    }
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
        prefs.putBool("webCfgAuth", gCfg->webCfgAuthEnabled);
        if (gCfg->wifiSsid[0]) prefs.putString("wifiSsid", gCfg->wifiSsid);
        if (gCfg->wifiPass[0]) prefs.putString("wifiPass", gCfg->wifiPass);
        prefs.end();
    }

    if (gOnSave) gOnSave();

    // Font size alone does not need a reboot — the save callback re-renders chat
    // and DMs at the new size, exactly as the on-device picker does. Rebooting
    // for it meant a ~10 second outage to change how big the text is.
    //
    // Decided by comparing the applied config against the snapshot with the font
    // size masked out: if that is the only field that moved, nothing else needs
    // a restart. Any other difference — and any field this comparison does not
    // understand — still reboots, so the failure direction is the old behaviour
    // rather than a setting that silently never takes effect. Both structs come
    // from the same live object, so their padding bytes match and memcmp is
    // meaningful.
    RhinoConfig cfgMasked = *gCfg;
    cfgMasked.fontSize = cfgBefore.fontSize;
    const bool fontSizeOnly = (memcmp(&cfgMasked, &cfgBefore, sizeof(RhinoConfig)) == 0)
                              && (gCfg->fontSize != cfgBefore.fontSize);

    if (fontSizeOnly) {
        redirectHomeWithFlash("Saved. Font size applied.");
        return;
    }

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

#if HAS_VNC_HOST
static void handleGetVncStatus() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    char response[192];
    const String ip = WiFi.localIP().toString();
    snprintf(response, sizeof(response),
             "{\"enabled\":%s,\"running\":%s,\"client\":%s,"
             "\"pending\":%s,\"ip\":\"%s\",\"port\":%u}",
             vncHostEnabled() ? "true" : "false",
             vncHostRunning() ? "true" : "false",
             vncHostClientConnected() ? "true" : "false",
             gVncToggleReq ? "true" : "false",
             ip.c_str(), (unsigned)vncHostPort());
    server.send(200, "application/json", response);
}

static void handlePostVncToggle() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    gVncToggleOn = server.arg("enabled") == "1";
    gVncToggleReq = true;
    server.send(202, "application/json", "{\"queued\":true}");
}
#endif

// ── Custom theme builder ─────────────────────────────────────────────────────
// Written straight through rather than queued like the VNC toggle: handlers run
// inside webCfgLoop() on the main loop thread (see server.handleClient() there),
// which is the same thread that owns NVS everywhere else — the WiFi credentials
// above are already saved this way. Writing here also means the response can
// carry the slot that was assigned, which is what the page needs to draw the
// new card without a reload.
static void sendThemeJson(int slot, const UiCustomTheme &t) {
    char body[220];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"slot\":%d,\"theme\":{\"s\":%d,\"n\":\"%s\",\"m\":%u,"
             "\"bg\":\"#%06lX\",\"p\":\"#%06lX\",\"p2\":\"#%06lX\",\"a\":\"#%06lX\"}}",
             slot, slot, t.name, (unsigned)t.mode,
             (unsigned long)rgb565To888(t.bgMain),
             (unsigned long)rgb565To888(t.panelBg),
             (unsigned long)rgb565To888(t.panelAlt),
             (unsigned long)rgb565To888(t.accent));
    server.send(200, "application/json", body);
}

static void handlePostThemeSave() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    UiCustomTheme t = {};
    if (server.hasArg("code")) {
        // Import path. The device owns the format, so a code that round-trips
        // here is a code every device will accept.
        if (!uiCustomThemeDecode(server.arg("code").c_str(), t)) {
            server.send(400, "application/json",
                        "{\"error\":\"That share code is not valid.\"}");
            return;
        }
        if (server.arg("preview") == "1") {
            // Fill the form only. The slot is reported as -1 because nothing
            // was stored; the page uses the theme fields and ignores it.
            sendThemeJson(-1, t);
            return;
        }
    } else {
        String name = server.arg("name");
        name.trim();
        if (name.length() == 0) {
            server.send(400, "application/json", "{\"error\":\"Give the theme a name.\"}");
            return;
        }
        strncpy(t.name, name.c_str(), sizeof(t.name) - 1);
        t.mode = (server.arg("mode").toInt() == UI_MODE_LIGHT) ? UI_MODE_LIGHT : UI_MODE_DARK;
        if (!parseHexColor(server.arg("bg"), t.bgMain)
            || !parseHexColor(server.arg("panel"), t.panelBg)
            || !parseHexColor(server.arg("panel2"), t.panelAlt)
            || !parseHexColor(server.arg("accent"), t.accent)) {
            server.send(400, "application/json",
                        "{\"error\":\"Colors must be hex like #1a2230.\"}");
            return;
        }
    }

    int slot = server.hasArg("slot") ? server.arg("slot").toInt() : -1;
    if (slot >= UI_CUSTOM_THEME_SLOTS) slot = -1;
    if (slot < 0 && uiCustomThemeFirstFree() < 0) {
        char full[96];
        snprintf(full, sizeof(full),
                 "{\"error\":\"All %d theme slots are full. Delete one first.\"}",
                 UI_CUSTOM_THEME_SLOTS);
        server.send(409, "application/json", full);
        return;
    }

    slot = uiCustomThemeSave(slot, t);
    if (slot < 0) {
        server.send(500, "application/json", "{\"error\":\"Could not save the theme.\"}");
        return;
    }

    // Editing the theme the device is currently wearing has to repaint it, or
    // the panel keeps showing the colors that were just replaced until
    // something else happens to re-apply the palette.
    if (gCfg && uiThemeIsCustom(gCfg->uiTheme)
        && uiThemeCustomSlot(gCfg->uiTheme) == slot) {
        if (gOnSave) gOnSave();
    }

    const UiCustomTheme *stored = uiCustomThemeGet(slot);
    sendThemeJson(slot, stored ? *stored : t);
}

static void handlePostThemeDelete() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    const int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= UI_CUSTOM_THEME_SLOTS) {
        server.send(400, "application/json", "{\"error\":\"No such theme slot.\"}");
        return;
    }
    if (!uiCustomThemeDelete(slot)) {
        server.send(500, "application/json", "{\"error\":\"Could not delete the theme.\"}");
        return;
    }

    // Deleting the theme the device is currently wearing leaves the selection
    // pointing at an empty slot. Move it off deliberately and repaint, rather
    // than letting applyUiThemePalette() discover it later and fall back with
    // the panel already drawn in the deleted colors.
    if (gCfg && uiThemeIsCustom(gCfg->uiTheme)
        && uiThemeCustomSlot(gCfg->uiTheme) == slot) {
        gCfg->uiTheme = UI_THEME_CAMELLIA;
        gCfg->uiMode = UI_MODE_DARK;
        if (gOnSave) gOnSave();
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleGetLiveData() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    int after = -1;
    if (server.hasArg("after")) after = server.arg("after").toInt();

    Channel &liveChan = Channels.get(CHAN_LIVE);
    int total = liveChan.count;
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

    if (liveChan.lines) {
        bool first = true;
        for (int i = from; i < total; i++) {
            const DisplayLine &dl = liveChan.lines[i % MAX_MSG_LINES];
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

// ── Chat tab endpoints ────────────────────────────────────────────
// Omitted on the Cardputer (no PSRAM): the web Chat tab is removed there, so
// these handlers and their routes aren't registered.
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
// Lists selectable chat targets: enabled channels + known DM conversations.
static void handleGetChatTargets() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    String out;
    out.reserve(1024);
    out += "{\"channels\":[";
    bool first = true;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ck = CHANNEL_KEYS[i];
        if (ck.role == 2) continue;   // DISABLED
        const char *nm = ck.name_buf[0] ? ck.name_buf : ck.name;
        if (!nm || !nm[0]) continue;
        if (!first) out += ",";
        first = false;
        out += "{\"id\":";
        out += String(i);
        out += ",\"name\":\"";
        appendJsonEscaped(out, nm);
        out += "\"}";
    }
    out += "],\"dms\":[";
    first = true;
    int dn = DMs.count();
    for (int i = 0; i < dn; i++) {
        DmConv *c = DMs.getByRank(i);
        if (!c || c->nodeId == 0) continue;
        char idhex[12];
        snprintf(idhex, sizeof(idhex), "%08X", c->nodeId);
        if (!first) out += ",";
        first = false;
        out += "{\"id\":\"";
        out += idhex;
        out += "\",\"name\":\"";
        appendJsonEscaped(out, c->shortName[0] ? c->shortName : idhex);
        out += "\"}";
    }
    out += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

// Live message feed for one target (channel ch=N or DM dm=<hex nodeId>).
// Mirrors handleGetLiveData: {total, from, lines:[{i, pid, mine, ack, t}]}.
static void handleGetChatData() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    int after = server.hasArg("after") ? server.arg("after").toInt() : -1;

    const bool isDm = server.hasArg("dm");
    DisplayLine *chLines = nullptr;
    DmLine *dmLines = nullptr;
    int count = 0, cap = 0;
    if (isDm) {
        uint32_t nodeId = (uint32_t)strtoul(server.arg("dm").c_str(), nullptr, 16);
        DmConv *c = DMs.find(nodeId);
        if (c && c->lines) { dmLines = c->lines; count = c->count; cap = MAX_DM_LINES; }
    } else {
        int ch = server.hasArg("ch") ? server.arg("ch").toInt() : 0;
        if (ch >= 0 && ch < MESH_CHANNELS) {
            Channel &c = Channels.get(ch);
            if (c.lines) { chLines = c.lines; count = c.count; cap = MAX_MSG_LINES; }
        }
    }

    int oldest = (cap > 0) ? max(0, count - cap) : 0;
    int from = (after >= 0) ? (after + 1) : (count - 60);
    if (from < oldest) from = oldest;
    if (from < 0) from = 0;
    if (from > count) from = count;

    String out;
    out.reserve(6144);
    out += "{\"total\":";
    out += String(count);
    out += ",\"from\":";
    out += String(from);
    out += ",\"lines\":[";
    bool first = true;
    for (int i = from; i < count; i++) {
        const char *t;
        uint32_t pid;
        uint8_t ack;
        if (isDm) {
            const DmLine &dl = dmLines[i % cap];
            t = dl.text; pid = dl.packetId; ack = (uint8_t)dl.ack;
        } else {
            const DisplayLine &dl = chLines[i % cap];
            t = dl.text; pid = dl.packetId; ack = (uint8_t)dl.ack;
        }
        if (!first) out += ",";
        first = false;
        out += "{\"i\":";
        out += String(i);
        out += ",\"pid\":";
        out += String(pid);
        out += ",\"mine\":";
        out += (ack != 0 ? "1" : "0");
        out += ",\"ack\":";
        out += String((int)ack);
        out += ",\"t\":\"";
        appendJsonEscaped(out, t);
        out += "\"}";
    }
    out += "]}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

// Queue a chat send (message / reply / tapback) for the main loop to transmit.
static void handlePostChatSend() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }
    String text = server.arg("text");
    if (text.length() == 0) {
        server.send(400, "application/json", "{\"error\":\"empty\"}");
        return;
    }
    bool isDm = server.arg("dm").toInt() != 0;
    uint32_t replyId = server.hasArg("reply")
        ? (uint32_t)strtoul(server.arg("reply").c_str(), nullptr, 10) : 0;
    uint32_t emoji = (server.arg("emoji").toInt() != 0) ? 1 : 0;
    uint32_t target = isDm
        ? (uint32_t)strtoul(server.arg("id").c_str(), nullptr, 16)
        : (uint32_t)server.arg("id").toInt();
    webCfgQueueChatSend(isDm, target, text.c_str(), replyId, emoji);
    server.send(200, "application/json", "{\"ok\":true}");
}
#endif  // !DEVICE_CARDPUTER_LORA_HAT

// Returns JSON snapshots of the four live-chart series so the web UI can
// render the same channel-utilization and SNR/RSSI charts that the on-device
// live feed exposes via the u/s keyboard shortcuts.
static void handleGetChartData() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    WebChartSnapshot chUtil, airUtil, snr, rssi;
    webChartSnapshotChUtil(chUtil);
    webChartSnapshotAirUtil(airUtil);
    webChartSnapshotSnr(snr);
    webChartSnapshotRssi(rssi);

    String out;
    out.reserve(4096);

    auto appendSeries = [&](const char *name, const WebChartSnapshot &s) {
        out += "\"";
        out += name;
        out += "\":{\"count\":";
        out += String(s.count);
        out += ",\"hasLast\":";
        out += (s.hasLast ? "true" : "false");
        out += ",\"last\":";
        if (s.hasLast) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", (double)s.lastVal);
            out += buf;
        } else {
            out += "null";
        }
        out += ",\"values\":[";
        for (int i = 0; i < s.count; i++) {
            if (i > 0) out += ",";
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", (double)s.values[i]);
            out += buf;
        }
        out += "]}";
    };

    out += "{\"capacity\":";
    out += String(WebChartSnapshot::CAP);
    out += ",";
    appendSeries("chUtil", chUtil);
    out += ",";
    appendSeries("airUtil", airUtil);
    out += ",";
    appendSeries("snr", snr);
    out += ",";
    appendSeries("rssi", rssi);
    out += "}";

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
            if (kWebDeviceReleaseCheckEnabled) {
                Serial.println("[web-https] release-check queued by client");
                queueReleaseCheckNow();
            } else {
                clearReleaseCheckResult();
                gReleaseCheckState = RELEASE_CHECK_DONE_ERR;
                gReleaseCheckStartedAtMs = millis();
                gReleaseCheckFinishedAtMs = gReleaseCheckStartedAtMs;
                strncpy(gReleaseCheckErr,
                        "Device-side release check disabled on this build",
                        sizeof(gReleaseCheckErr) - 1);
                gReleaseCheckErr[sizeof(gReleaseCheckErr) - 1] = '\0';
            }
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

static void handlePostTelemetry() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    webCfgQueueTelemetry();
    redirectHomeWithFlash("Telemetry TX queued.");
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

    File f = storageFs().open(kTmpScreenshotPath, FILE_READ);
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

// ── Reset Chat Colors ─────────────────────────────────────────
//
// Nothing per-node is stored: the color is a hash of the node id, and this
// re-rolls the salt that hash is mixed with. gOnSave persists it; the chat
// repaints on its own because the salt is part of the render signature.
static void handlePostResetChatColors() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }
    gCfg->chatColorSalt = cfgNextChatColorSalt(gCfg->chatColorSalt);
    if (gOnSave) gOnSave();
    redirectHomeWithFlash("Chat colors reassigned.");
}

// ── Saved WiFi networks ───────────────────────────────────────
// Applied straight through rather than queued: these handlers run inside
// webCfgLoop() on the main loop thread, which is the thread that owns NVS and
// the WiFi state everywhere else.

static void handlePostWifiAdd() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();
    if (ssid.isEmpty()) {
        redirectHomeWithFlash("Network name cannot be empty.");
        return;
    }
    if (cfgSavedWifiRemember(ssid.c_str(), pass.c_str())) {
        redirectHomeWithFlash("Network remembered.");
    } else {
        // Refused for a reason worth naming rather than a silent no-op: the
        // active network is already listed, and "AP" is the picker's sentinel.
        redirectHomeWithFlash("Not added - that is the active network, or a reserved name.");
    }
}

static void handlePostWifiForget() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    String ssid = server.arg("ssid");
    ssid.trim();
    if (cfgSavedWifiRemove(ssid.c_str())) {
        redirectHomeWithFlash("Network forgotten.");
    } else {
        redirectHomeWithFlash("That network is not in the remembered list.");
    }
}

static void handlePostWifiUse() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    String ssid = server.arg("ssid");
    ssid.trim();
    if (!cfgSavedWifiActivate(ssid.c_str())) {
        redirectHomeWithFlash("That network is not in the remembered list.");
        return;
    }
    // Answer before the radio moves. The switch re-associates the station, so a
    // browser reading this over WiFi is about to lose the device; sending the
    // page first at least lets the message land.
    redirectHomeWithFlash("Switched network. Reconnecting - this page may become "
                          "unreachable until you join the new network.");
}

// ── Store & Forward: request a replay ─────────────────────────
// Queued rather than sent here: the main loop owns the LoRa TX path and the
// router-tracking state, and it writes the outcome back through
// webCfgSetSnfResult() for the next render of the Modules section.
static void handlePostSnfRequest() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    webCfgQueueSnfRequest();
    redirectHomeWithFlash("Replay requested from the Store & Forward router.");
}

// ── Clear Nodes ───────────────────────────────────────────────

// Delete the on-SD node-database files. Legacy paths: nothing in this firmware
// writes them any more, but a card carrying them from an older build would
// otherwise resurrect nodes that were just cleared. The eviction archive
// (nodeArchiveFilePath()) is deliberately left alone — it is a historical log
// the user opted into, not live node state.
static void clearNodeDbFilesOnSd() {
#if HAS_FILE_STORAGE
    if (!sdBegin()) return;

    const char *nodeFiles[] = {
        "/camillia/nodes.db",
        "/camillia/nodes.bin",
        "/camillia/nodes.json",
        "/camillia/node_db.bin",
        "/camillia/node_db.json",
    };
    for (size_t i = 0; i < sizeof(nodeFiles) / sizeof(nodeFiles[0]); i++) {
        if (storageFs().exists(nodeFiles[i])) {
            storageFs().remove(nodeFiles[i]);
        }
    }

    File nodesDir = storageFs().open("/camillia/nodes");
    if (nodesDir && nodesDir.isDirectory()) {
        File f = nodesDir.openNextFile();
        while (f) {
            String fp = String("/camillia/nodes/") + f.name();
            f.close();
            storageFs().remove(fp.c_str());
            f = nodesDir.openNextFile();
        }
        nodesDir.close();
        storageFs().rmdir("/camillia/nodes");
    }
#endif
}

// One route, two behaviors, selected by the "all" field that only the
// clear-everything form posts. Wiping favorites is the branch that has to ask
// for itself: anything arriving without that field — an older bookmark, a script
// written against the single-button version of this route — gets the variant
// that keeps them, which is the recoverable mistake of the two.
//
// The destructive half now also clears the SD copies, matching what the
// on-device Clear Nodes (All) action has always done; until now a web clear left
// them behind.
static void handlePostClearNodes() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    if (server.hasArg("all")) {
        Nodes.clearPersisted();
        clearNodeDbFilesOnSd();
        scheduleReboot(200);
        redirectHomeWithFlash("Node database cleared. Rebooting now...");
        return;
    }

    const int removed = Nodes.clearNonFavorites();
    const int kept = Nodes.count();
    // No SD deletion here: those files are whole-table snapshots and would take
    // the favorites with them. Same call the device action makes.
    char msg[96];
    snprintf(msg, sizeof(msg),
             "Cleared %d nodes, kept %d favorites. Rebooting now...", removed, kept);
    scheduleReboot(200);
    redirectHomeWithFlash(msg);
}

// ── Node favorite toggle ──────────────────────────────────────
// Answers JSON rather than redirecting like the forms above it: the Nodes tab
// has a streamed option list and a map behind it, and rebuilding that page to
// flip one bit would cost seconds on a full table and drop the user back on the
// Config tab. The page updates itself from the response instead.
static void handlePostNodeFavorite() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"error\":\"unauthorized\"}");
        return;
    }

    // Hex node id, with or without the leading '!' the page displays.
    String idArg = server.arg("id");
    if (idArg.startsWith("!")) idArg.remove(0, 1);
    const uint32_t nodeId = (uint32_t)strtoul(idArg.c_str(), nullptr, 16);
    if (nodeId == 0) {
        server.send(400, "application/json", "{\"error\":\"bad node id\"}");
        return;
    }

    const bool want = (server.arg("fav") == "1");
    NodeEntry *n = Nodes.find(nodeId);
    if (!n || !n->nodeId) {
        server.send(404, "application/json", "{\"error\":\"no such node\"}");
        return;
    }

    // setFavorite() returns false when the flag already matches. That is not a
    // failure for a toggle whose request may have been retried — report the
    // state the node is actually in, which is the state that was asked for.
    (void)Nodes.setFavorite(nodeId, want);

    char body[40];
    snprintf(body, sizeof(body), "{\"ok\":true,\"fav\":%d}", n->favorite ? 1 : 0);
    server.send(200, "application/json", body);
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

    clearNodeDbFilesOnSd();

    // Delete saved DM conversations from SD card (if supported on this board)
#if HAS_FILE_STORAGE
    {
        File dir = storageFs().open("/camillia/dms");
        if (dir && dir.isDirectory()) {
            File f = dir.openNextFile();
            while (f) {
                String fp = String("/camillia/dms/") + f.name();
                f.close();
                storageFs().remove(fp.c_str());
                f = dir.openNextFile();
            }
            dir.close();
            storageFs().rmdir("/camillia/dms");
        }
    }
#endif

    scheduleReboot(900);
    redirectHomeWithFlash("Factory reset complete. Rebooting now...");
}

// ── Export / Import ───────────────────────────────────────────

// Export every node the device knows about as CSV: the live table first, then
// any previously archived (evicted) nodes from the SD file. Both share the
// column schema from node_db (nodeCsvHeader/nodeCsvFormatEntry) so the two
// halves line up; a leading "source" column distinguishes them, and
// "archivedEpoch" is populated only for archived rows.
static void handleGetNodesCsv() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "camillia-nodes-%s.csv",
             (gCfg && gCfg->nodeShort[0]) ? gCfg->nodeShort : "node");
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    server.sendHeader("Content-Disposition", cd);
    server.sendHeader("Cache-Control", "no-store");

    // See handleGetStateMapStatus(): the abort flag outlives the request that
    // set it, so a chunked sender that does not clear it here stays silent
    // after any client walks away mid-download.
    gSendAborted = false;

    // Chunked: the live table alone can be MAX_NODES rows, too big to assemble
    // in one String on a device this size.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");

    String out;
    out.reserve(1024);
    out  = "source,archivedEpoch,";
    out += nodeCsvHeader();
    out += "\n";

    const int liveCount = Nodes.count();
    for (int i = 0; i < liveCount; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n || n->nodeId == 0) continue;
        char rec[512];
        nodeCsvFormatEntry(*n, rec, sizeof(rec));
        out += "live,,";
        out += rec;
        out += "\n";
        if (out.length() > 1024) { sendChunk(out); }
    }
    if (out.length()) { sendChunk(out); }

    // Archived rows are streamed through verbatim: each already begins with its
    // archivedEpoch followed by the shared columns, so only "archived," is
    // prepended. The file's own header line is skipped.
    const char *archPath = nodeArchiveFilePath();
    if (archPath && sdBegin() && storageFs().exists(archPath)) {
        File af = storageFs().open(archPath, FILE_READ);
        if (af) {
            bool firstLine = true;
            while (af.available()) {
                String line = af.readStringUntil('\n');
                line.trim();
                if (!line.length()) continue;
                if (firstLine) {
                    firstLine = false;
                    if (line.startsWith("archivedEpoch")) continue;   // header
                }
                out += "archived,";
                out += line;
                out += "\n";
                if (out.length() > 1024) { sendChunk(out); }
            }
            af.close();
        }
    }

    if (out.length()) sendChunk(out);
    server.sendContent("");   // terminate the chunked response
}

// Export every stored message as CSV, streamed straight to the browser. Web
// config only, and deliberately never written to the card: the device already
// persists chat to keep it across reboots, and a second copy sitting in the
// filesystem would be one more thing to manage on a device whose storage is
// shared with the radio. A backup you take is a backup you have.
//
// Wrapped channel messages are put back together here. ChannelMgr::_wordWrap()
// stores one line per wrap and pushes them last-segment-first (the device draws
// newest at the top), so a raw dump of the ring would come out with every
// multi-line message reversed. Same reassembly the web chat view does.
static void csvAppendQuoted(String &out, const char *text) {
    out += '"';
    for (const char *p = text ? text : ""; *p; p++) {
        if (*p == '"') out += '"';        // doubled, per RFC 4180
        if (*p == '\r') continue;         // CR would confuse the line ending
        out += *p;
    }
    out += '"';
}

static const char *msgAckName(uint8_t ack) {
    switch (ack) {
        case DisplayLine::PENDING:     return "PENDING";
        case DisplayLine::ACKED:       return "ACKED";
        case DisplayLine::ACKED_RELAY: return "ACKED_RELAY";
        case DisplayLine::NAKED:       return "NAKED";
        case DisplayLine::TX_FAILED:   return "TX_FAILED";
        default:                       return "";
    }
}

// Local calendar time for an epoch, or empty when the clock was not set when
// the message arrived. Never invents a date for a 0 epoch.
static void msgIsoTime(uint32_t epoch, char *out, size_t outLen) {
    if (epoch < 1700000000UL) { out[0] = '\0'; return; }
    time_t t = (time_t)epoch;
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(out, outLen, "%Y-%m-%d %H:%M:%S", &lt);
}

static void handleGetMessagesCsv() {
    if (!isLoggedIn()) { redirect("/login"); return; }

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "camillia-messages-%s.csv",
             (gCfg && gCfg->nodeShort[0]) ? gCfg->nodeShort : "node");
    char cd[128];
    snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", fileName);
    server.sendHeader("Content-Disposition", cd);
    server.sendHeader("Cache-Control", "no-store");

    // Same reset as the node CSV above, for the same reason.
    gSendAborted = false;

    // Chunked for the same reason as the node CSV: every channel of history
    // plus every DM is far too much to assemble in one String here.
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "");

    String out;
    out.reserve(1024);
    out = "source,channel,peer,epoch,localTime,sender,packetId,ack,text\n";

    char iso[24];

    for (int ch = 0; ch < MESH_CHANNELS; ch++) {
        Channel &c = Channels.get(ch);
        if (!c.lines || c.count <= 0) continue;
        const char *chanName = c.name ? c.name : "";
        const int oldest = max(0, c.count - MAX_MSG_LINES);

        int i = oldest;
        while (i < c.count) {
            const DisplayLine &head0 = c.lines[i % MAX_MSG_LINES];
            const uint32_t pid = head0.packetId;
            int j = i + 1;
            if (pid != 0) {
                while (j < c.count && c.lines[j % MAX_MSG_LINES].packetId == pid) j++;
            }
            // Back to front: the last stored segment is the start of the
            // message, and it is the line carrying the prefix and the ack.
            const DisplayLine &head = c.lines[(j - 1) % MAX_MSG_LINES];
            String text = head.text;
            for (int k = j - 2; k >= i; k--) {
                const char *seg = c.lines[k % MAX_MSG_LINES].text;
                while (*seg == ' ') seg++;      // drop the continuation indent
                if (*seg) { text += ' '; text += seg; }
            }

            msgIsoTime(head.epoch, iso, sizeof(iso));
            char meta[128];
            snprintf(meta, sizeof(meta), "channel,%d %s,,%lu,%s,!%08lX,%lu,%s,",
                     ch, chanName,
                     (unsigned long)head.epoch, iso,
                     (unsigned long)head.senderNodeId,
                     (unsigned long)head.packetId,
                     msgAckName((uint8_t)head.ack));
            out += meta;
            csvAppendQuoted(out, text.c_str());
            out += "\n";
            if (out.length() > 1024) { sendChunk(out); }
            i = j;
        }
    }
    if (out.length()) { sendChunk(out); }

    // DMs store one line per message — DmMgr does no wrapping, it lets the UI
    // wrap the label — so these need no reassembly.
    const int convCount = DMs.count();
    for (int ci = 0; ci < convCount; ci++) {
        DmConv *conv = DMs.getByRank(ci);
        if (!conv || !conv->lines || conv->count <= 0) continue;
        char peer[32];
        snprintf(peer, sizeof(peer), "!%08lX %s",
                 (unsigned long)conv->nodeId, conv->shortName);
        const int oldest = max(0, conv->count - MAX_DM_LINES);
        for (int i = oldest; i < conv->count; i++) {
            const DmLine &dl = conv->lines[i % MAX_DM_LINES];
            msgIsoTime(dl.epoch, iso, sizeof(iso));
            char meta[160];
            snprintf(meta, sizeof(meta), "dm,,%s,%lu,%s,,%lu,%s,",
                     peer,
                     (unsigned long)dl.epoch, iso,
                     (unsigned long)dl.packetId,
                     msgAckName((uint8_t)dl.ack));
            out += meta;
            csvAppendQuoted(out, dl.text);
            out += "\n";
            if (out.length() > 1024) { sendChunk(out); }
        }
    }

    if (out.length()) sendChunk(out);
    server.sendContent("");   // terminate the chunked response
}

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

// Static, not heap: 8 KB is more than AP mode could allocate contiguously, and
// the buffer exists either way since both route sets share these handlers.
static char   importBuf[8192];
static size_t importLen = 0;
static bool   importOk  = false;
static bool   importOverflow = false;

static void handleImportUpload() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        importLen = 0;
        importOk  = false;
        importOverflow = false;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Refuse the file rather than fill the buffer and parse what fit: a
        // truncated YAML still parses, so the old clamp applied the first N
        // settings of the file and silently dropped the rest.
        if (importOverflow) return;              // keep draining the socket
        const size_t space = sizeof(importBuf) - importLen - 1;
        if (upload.currentSize > space) {
            importOverflow = true;
            return;
        }
        memcpy(importBuf + importLen, upload.buf, upload.currentSize);
        importLen += upload.currentSize;
    } else if (upload.status == UPLOAD_FILE_END) {
        if (importOverflow) return;
        importBuf[importLen] = '\0';
        importOk = true;
    }
}

// AP mode answers inline instead of redirecting home. A 302 to "/" makes the
// browser pull the whole lite config page again — the largest allocation this
// mode makes — and on success it would do that while a reboot is already
// scheduled. A couple hundred bytes says the same thing for none of the heap.
static void sendImportResult(const char *msg) {
    if (!webCfgUseLite()) {
        redirectHomeWithFlash(msg);
        return;
    }
    String page;
    page.reserve(320);
    page = "<!doctype html><meta name='viewport' content='width=device-width,"
           "initial-scale=1'><body style='font-family:sans-serif;background:#12161c;"
           "color:#e6e6e6;padding:1.2em'><p>";
    page += msg;
    page += "</p><p><a href='/' style='color:#6cf'>Back to config</a></p></body>";
    server.send(200, "text/html", page);
}

static void handleImportDone() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (!gCfg) { redirect("/"); return; }
    if (importOverflow) {
        sendImportResult("Import failed: file larger than 8 KB.");
        return;
    }
    if (!importOk || importLen == 0) {
        sendImportResult("Import failed: no data received.");
        return;
    }
    if (!cfgImportFromBuf(importBuf, importLen, *gCfg)) {
        sendImportResult("Import failed: parse error.");
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
    sendImportResult("Import complete. Rebooting now...");
}

static void handleGetFavicon() {
    // Cached hard: it is a fixed asset on a device whose whole job is to answer
    // a handful of requests without stalling, and re-serving it on every page
    // load costs a round trip this server can feel.
    server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
    server.send(200, "image/svg+xml", kFavicon);
}

static void handleGetLogout() {
    sessionToken[0] = '\0';
    server.sendHeader("Set-Cookie", "sess=; Path=/; Max-Age=0");
    redirect("/login");
}

// ── Public API ────────────────────────────────────────────────

// Unmatched-request handler, shared by both route sets.
// All routes register through these rather than server.on() directly, so the
// idle-timeout stamp lives in exactly one place. This core's WebServer has no
// per-request hook, and stamping 29 handlers by hand is the kind of thing a
// later route addition quietly forgets — which would show up as web config
// shutting down under an active user.
static void noteWebRequest() { gLastRequestMs = millis(); }

static void onRoute(const char *uri, HTTPMethod method,
                    WebServer::THandlerFunction fn) {
    server.on(uri, method, [fn]() { noteWebRequest(); fn(); });
}

static void onRoute(const char *uri, HTTPMethod method,
                    WebServer::THandlerFunction fn,
                    WebServer::THandlerFunction upload) {
    server.on(uri, method,
              [fn]()     { noteWebRequest(); fn(); },
              [upload]() { noteWebRequest(); upload(); });
}

static void registerNotFound() {
    // Log any unmatched request so we can see exactly what a failing tab hits
    // (the default handler only prints a generic "request handler not found").
    server.onNotFound([]() {
        // Counts as activity: captive-portal probes and a browser retrying a
        // stale URL both mean someone is still using this.
        noteWebRequest();
        // In AP/captive mode, steer the OS's portal-detection probes (which hit
        // unknown paths like /generate_204, /hotspot-detect.html) to the minimal
        // setup form so the phone surfaces the portal instead of retrying.
        // Answering fast here is also what keeps those probes from stalling the
        // header read — they never pay to build the lite page.
        if (gCaptiveActive) {
            char loc[64];
            snprintf(loc,
                     sizeof(loc),
                     "http://%s/%s",
                     ipBuf,
                     "setup");
            server.sendHeader("Location", loc);
            server.send(302, "text/plain", "");
            return;
        }
        Serial.printf("[web] 404 %s %s\n",
                      (server.method() == HTTP_GET) ? "GET" : "POST",
                      server.uri().c_str());
        server.send(404, "text/plain", "Not found");
    });
}

// Routes for web config lite (both AP paths: first-boot onboarding and the
// STA-connect fallback). Deliberately excludes every heavy endpoint — chat,
// live feed, charts, node CSV, import/export, screenshots — both because the
// lite page never calls them and because each registration costs heap that AP
// mode doesn't have to spare.
static void registerLiteRoutes() {
    onRoute("/",           HTTP_GET,  handleGetRoot);
    onRoute("/config",     HTTP_GET,  handleGetConfig);
    onRoute("/setup",      HTTP_GET,  handleGetOnboard);
    onRoute("/onboard",    HTTP_POST, handlePostOnboard);
    onRoute("/save",       HTTP_POST, handlePostSave);
    onRoute("/login",      HTTP_GET,  handleGetLogin);
    onRoute("/login",      HTTP_POST, handlePostLogin);
    onRoute("/logout",     HTTP_GET,  handleGetLogout);
    onRoute("/favicon.ico", HTTP_GET, handleGetFavicon);
    // The one heavy-sounding endpoint lite does carry. Restoring a config is
    // exactly what someone stuck on the AP with a misconfigured device needs,
    // and the cost is a route registration plus WebServer's ~2 KB multipart
    // buffer during the POST — the handler parses out of a static buffer on the
    // stack. Its sibling /export stays out: building the YAML needs one large
    // contiguous String.
    onRoute("/import",     HTTP_POST, handleImportDone, handleImportUpload);
    registerNotFound();
}

// Full route set, registered only for a working STA connection.
static void registerCommonRoutes() {
    onRoute("/",                  HTTP_GET,  handleGetRoot);
    onRoute("/config",            HTTP_GET,  handleGetConfig);
    onRoute("/login",             HTTP_GET,  handleGetLogin);
    onRoute("/login",             HTTP_POST, handlePostLogin);
    onRoute("/save",              HTTP_POST, handlePostSave);
    onRoute("/set-debug-monitor", HTTP_POST, handlePostSetDebugMonitor);
    onRoute("/live-data",         HTTP_GET,  handleGetLiveData);
    onRoute("/theme-save",        HTTP_POST, handlePostThemeSave);
    onRoute("/theme-delete",      HTTP_POST, handlePostThemeDelete);
#if HAS_VNC_HOST
    onRoute("/vnc-status",        HTTP_GET,  handleGetVncStatus);
    onRoute("/vnc-toggle",        HTTP_POST, handlePostVncToggle);
#endif
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    onRoute("/chat-targets",      HTTP_GET,  handleGetChatTargets);
    onRoute("/chat-data",         HTTP_GET,  handleGetChatData);
    onRoute("/chat-send",         HTTP_POST, handlePostChatSend);
#endif
    onRoute("/chart-data",        HTTP_GET,  handleGetChartData);
    onRoute("/logout",            HTTP_GET,  handleGetLogout);
    onRoute("/favicon.ico",       HTTP_GET,  handleGetFavicon);
    onRoute("/announce",          HTTP_POST, handlePostAnnounce);
    onRoute("/telemetry",         HTTP_POST, handlePostTelemetry);
    if (gOnScreenshotPng) {
        onRoute("/screenshot",    HTTP_GET,  handleGetScreenshot);
    }
#if HAS_STATE_MAPS
    // Map cache. Full-page routes only: filling it needs the browser to reach
    // OSM, which means a station connection, and the lite page AP mode serves
    // has no Utilities tab to put the control on.
    onRoute("/state-map-status",  HTTP_GET,  handleGetStateMapStatus);
    onRoute("/state-map-upload",  HTTP_POST, handleStateMapUploadDone, handleStateMapUpload);
#endif
    onRoute("/nodes.csv",         HTTP_GET,  handleGetNodesCsv);
    onRoute("/messages.csv",      HTTP_GET,  handleGetMessagesCsv);
    onRoute("/export",            HTTP_GET,  handleGetExport);
    onRoute("/import",            HTTP_POST, handleImportDone, handleImportUpload);
    onRoute("/reset-chat-colors", HTTP_POST, handlePostResetChatColors);
    onRoute("/clear-messages",    HTTP_POST, handlePostClearMessages);
    onRoute("/wifi-add",          HTTP_POST, handlePostWifiAdd);
    onRoute("/wifi-forget",       HTTP_POST, handlePostWifiForget);
    onRoute("/wifi-use",          HTTP_POST, handlePostWifiUse);
    onRoute("/snf-request",       HTTP_POST, handlePostSnfRequest);
    onRoute("/clear-nodes",       HTTP_POST, handlePostClearNodes);
#if HAS_STATE_MAPS
    onRoute("/clear-maps",        HTTP_POST, handlePostClearMaps);
#endif
    onRoute("/node-favorite",     HTTP_POST, handlePostNodeFavorite);
    onRoute("/factory-reset",     HTTP_POST, handlePostFactoryReset);
    registerNotFound();
}

bool webCfgBegin(RhinoConfig *cfg, WebCfgSaveCb onSave,
                 WebCfgScreenshotPngCb onScreenshotPng) {
    if (running) return true;

    // Start the idle clock before any of the (slow) radio bring-up, so the
    // window measures time the user had the page available rather than time
    // spent associating.
    gIdleExpired   = false;
    gLastRequestMs = millis();
    gIdleTimeoutMs = cfg ? (cfg->webCfgIdleTimeoutS * 1000UL) : 0;

    // Baseline internal-heap snapshot before WiFi/LWIP allocate anything.
    // On the no-PSRAM Cardputer this is the key margin to watch: compare it
    // against the "server up" snapshot below to see how much WiFi consumed.
    logWifiHeapDiag("web start (pre-WiFi)");

    // No-PSRAM builds are most sensitive to internal-heap pressure in AP mode.
    // Reclaim chat/DM buffers for the duration of the web session, then restore
    // from persistence in webCfgEnd().
    gWebBuffersReclaimed = false;
    bool reclaimUiBuffers = false;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    reclaimUiBuffers = true;
#elif defined(BOARD_HAS_PSRAM)
    reclaimUiBuffers = (ESP.getPsramSize() == 0);
#else
    reclaimUiBuffers = true;
#endif
    if (reclaimUiBuffers) {
        size_t reclaimed = Channels.releaseBuffers();
        DMs.clearAll(false);   // free DM line buffers, keep SD-persisted history
        gWebBuffersReclaimed = true;
        Serial.printf("[web] reclaimed %u bytes of chat buffers\n", (unsigned)reclaimed);
        logWifiHeapDiag("after chat reclaim");
    }

    auto restoreUiBuffersIfNeeded = [&]() {
        if (!gWebBuffersReclaimed) return;
        Channels.restoreBuffers();
        DMs.clearAll(false);
        DMs.loadAll();
        gWebBuffersReclaimed = false;
    };

    gCfg    = cfg;
    gApMode = false;
    gOnSave = onSave;
    gOnScreenshotPng = onScreenshotPng;
    gAnnounceReq = false;
    gTelemetryReq = false;
    gFlashMsg[0] = '\0';
    gRebootPending = false;
    gRebootAtMs = 0;
    clearReleaseCheckResult();
    gReleaseCheckState = RELEASE_CHECK_IDLE;

    // Load saved WiFi credentials. The settings blob loaded by
    // loadConfigFromPrefs() is the source of truth — it always runs before this.
    // The standalone "wifiSsid"/"wifiPass"/"webPass" keys are a pre-blob format
    // that the blob migration reclaims, so they are read only as a fallback for
    // a device that has not migrated yet. Copying them over gCfg unconditionally
    // wiped the credentials the blob had just supplied, which is why WiFi (and a
    // custom web password) came back empty on every boot after the migration.
    //
    // Everything downstream must therefore key off gCfg, never savedSsid: on a
    // migrated device those keys are gone, so savedSsid is empty even when the
    // blob has good credentials. Deciding AP-vs-STA on savedSsid sent every such
    // device to the onboarding AP on each boot.
    Preferences prefs;
    prefs.begin("camillia", true);
    String savedSsid = prefs.isKey("wifiSsid") ? prefs.getString("wifiSsid", "") : "";
    String savedPass = prefs.isKey("wifiPass") ? prefs.getString("wifiPass", "") : "";
    String savedWebPass = prefs.isKey("webPass") ? prefs.getString("webPass", "") : "";
    prefs.end();
    if (!gCfg->wifiSsid[0] && savedSsid.length()) {
        strncpy(gCfg->wifiSsid, savedSsid.c_str(), sizeof(gCfg->wifiSsid) - 1);
        gCfg->wifiSsid[sizeof(gCfg->wifiSsid) - 1] = '\0';
        strncpy(gCfg->wifiPass, savedPass.c_str(), sizeof(gCfg->wifiPass) - 1);
        gCfg->wifiPass[sizeof(gCfg->wifiPass) - 1] = '\0';
    }
    strncpy(gWifiSsid, gCfg->wifiSsid, sizeof(gWifiSsid) - 1);
    gWifiSsid[sizeof(gWifiSsid) - 1] = '\0';
    strncpy(gWifiPass, gCfg->wifiPass, sizeof(gWifiPass) - 1);
    gWifiPass[sizeof(gWifiPass) - 1] = '\0';
    if (!gCfg->webCfgPass[0]) {
        const char *webPass = savedWebPass.length() ? savedWebPass.c_str() : kDefaultWebPass;
        strncpy(gCfg->webCfgPass, webPass, sizeof(gCfg->webCfgPass) - 1);
        gCfg->webCfgPass[sizeof(gCfg->webCfgPass) - 1] = '\0';
    }

    const char *headers[] = {"Cookie"};
    server.collectHeaders(headers, 1);

    auto startApMode = [&](const char *modeTag, bool onboarding) {
        gOnboarding = onboarding;
        gApMode     = true;   // "/" serves web config lite for this session
        WiFi.disconnect(false);
        if (!ensureWifiMode(WIFI_AP, modeTag)) return false;
        delay(120);
        if (!WiFi.softAP("camillia-mt")) {
            Serial.printf("[web] %s start failed\n", onboarding ? "onboarding AP" : "AP fallback");
            logWifiHeapDiag(onboarding ? "onboarding AP start failed" : "AP fallback start failed");
            return false;
        }
        delay(250);
        // Disable modem power-save for the AP session too (the STA path does the
        // same below): with it on, the synchronous WebServer stalls on inbound
        // packets and the page load times out. webCfgEnd() powers the radio off.
        //
        // Except while the BLE keyboard is up. Wi-Fi/BT coexistence requires
        // modem sleep, and turning it off underneath a live BT controller is
        // the same fault that aborts inside coex_core_enable() when the two are
        // started the other way round. A slower page is the correct trade
        // against taking the device down.
        if (!bleKeyboardHoldsWifiModemSleep()) WiFi.setSleep(false);
        IPAddress apIP = WiFi.softAPIP();
        apIP.toString().toCharArray(ipBuf, sizeof(ipBuf));

        // Captive-portal DNS: resolve every host to us so the connecting device
        // pops the portal straight to the config/onboarding page.
        gDns.setErrorReplyCode(DNSReplyCode::NoError);
        if (gDns.start(53, "*", apIP)) {
            gCaptiveActive = true;
        } else {
            Serial.println("[web] captive DNS start failed (continuing without it)");
        }

        // Both AP paths serve web config lite; the full page is STA-only.
        registerLiteRoutes();
        server.begin();
        running = true;
        Serial.printf("[web] %s at http://%s/\n",
                      onboarding ? "onboarding AP" : "AP fallback",
                      ipBuf);
        logWifiHeapDiag("server up (AP)");
        return true;
    };

    // No credentials means onboarding; the picker's "AP" entry forces the same
    // SoftAP even when credentials exist, so the user can reach web config
    // without their network (or when it's out of range).
    if (!staConnectSsid()[0] || gForceAp) {
        const bool onboarding = !staConnectSsid()[0];
        bool ok = startApMode(onboarding ? "onboarding AP" : "forced AP", onboarding);
        if (!ok) restoreUiBuffersIfNeeded();
        return ok;
    }

    // ── Normal mode: connect to saved WiFi ────────────────────
    gOnboarding = false;
    if (!ensureWifiMode(WIFI_STA, "STA connect")) {
        Serial.println("[web] STA mode failed — using AP fallback");
        bool ok = startApMode("AP fallback", false);
        if (!ok) restoreUiBuffersIfNeeded();
        return ok;
    }
    wl_status_t beginStatus = WiFi.begin(staConnectSsid(), staConnectPass());
    if (beginStatus == WL_CONNECT_FAILED || beginStatus == WL_NO_SHIELD) {
        Serial.printf("[web] STA begin failed (%d)\n", (int)beginStatus);
        logWifiHeapDiag("STA begin failed");
        bool ok = startApMode("AP fallback", false);
        if (!ok) restoreUiBuffersIfNeeded();
        return ok;
    }
    Serial.printf("[web] connecting to \"%s\"%s ...\n", staConnectSsid(),
                  gStaSsidOverride[0] ? " (picker)" : "");

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        wl_status_t st = WiFi.status();
        if (st == WL_NO_SHIELD) {
            Serial.println("[web] WiFi stack unavailable during STA connect");
            logWifiHeapDiag("STA connect unavailable");
            bool ok = startApMode("AP fallback", false);
            if (!ok) restoreUiBuffersIfNeeded();
            return ok;
        }
        if (millis() - start >= kConnectTimeout) {
            Serial.println("[web] STA connect timeout — falling back to AP mode");
            bool ok = startApMode("AP fallback", false);
            if (!ok) restoreUiBuffersIfNeeded();
            return ok;
        }
        if (gWaitCb) gWaitCb();
        delay(100);
    }

    WiFi.localIP().toString().toCharArray(ipBuf, sizeof(ipBuf));

    // Disable STA modem power-save for the web session. The default
    // WIFI_PS_MIN_MODEM buffers our inbound packets until the AP's next DTIM
    // beacon (~100-300ms), which the synchronous WebServer turns into stalls,
    // retransmits, and browser timeouts. webCfgEnd() powers the radio off, so
    // this only stays in effect while web config is up.
    //
    // Skipped while the BLE keyboard holds modem sleep on -- see the AP path
    // above and startStack() in ble_keyboard.cpp for why coexistence cannot
    // run without it.
    if (!bleKeyboardHoldsWifiModemSleep()) WiFi.setSleep(false);

    if (kLiteOnlyBoard) {
        registerLiteRoutes();
    } else {
        registerCommonRoutes();
    }
    server.begin();
    running = true;
    Serial.printf("[web] ready at http://%s/\n", ipBuf);
    logWifiHeapDiag("server up (STA)");
    return true;
}

void webCfgEnd() {
    if (!running) return;
    gIdleExpired    = false;
    gIdleTimeoutMs  = 0;
    sessionToken[0] = '\0';
    gFlashMsg[0] = '\0';
    gRebootPending = false;
    gRebootAtMs = 0;
    clearReleaseCheckResult();
    gReleaseCheckState = RELEASE_CHECK_IDLE;
    if (gCaptiveActive) {
        gDns.stop();
        gCaptiveActive = false;
    }
    server.stop();

    // Keep the station association when the device wants WiFi anyway. Web
    // config used to power the radio down unconditionally here, on the basis
    // that it had brought the radio up — but when it merely *joined* the
    // network the device was going to be on regardless, dropping the
    // association only forces serviceWifiStation() to rebuild it, which costs a
    // full reassociation (and an MQTT/NTP outage) for nothing.
    //
    // Not kept when we were hosting the SoftAP (that has to come down, and the
    // station was never up), when WiFi is switched off in settings, or when
    // there is no association to preserve in the first place.
    const bool keepStation = gCfg && gCfg->wifiEnabled && !gApMode
                             && WiFi.status() == WL_CONNECTED;
    if (keepStation) {
        // The one radio setting the session actually changed. webCfgBegin()
        // turns modem power-save off because the synchronous WebServer stalls
        // on DTIM-buffered packets; that was only ever meant to last as long as
        // the server, and the old teardown "restored" it by killing the radio.
        WiFi.setSleep(true);
    } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    if (gWebBuffersReclaimed) {
        // Reallocate chat/DM buffers released in webCfgBegin() and reload
        // persisted history now that Wi-Fi/web RAM pressure is gone.
        Channels.restoreBuffers();
        DMs.clearAll(false);
        DMs.loadAll();
        gWebBuffersReclaimed = false;
    }

    ipBuf[0]    = '\0';
    gCfg        = nullptr;
    gOnSave     = nullptr;
    gOnScreenshotPng = nullptr;
    gAnnounceReq = false;
    gTelemetryReq = false;
    // Cancel any chat send the web UI queued but that the main loop hasn't
    // drained yet — serviceWebChatSend() only runs while the server is up, so a
    // leftover request would otherwise fire the next time it's started.
    gChatSendReq   = false;
    gChatSendText[0] = '\0';
#if HAS_VNC_HOST
    gVncToggleReq = false;
#endif
    running     = false;
    gOnboarding = false;
    gApMode     = false;
    Serial.printf("[web] stopped%s\n",
                  keepStation ? " (station connection kept)" : " (radio off)");
}

bool webCfgIsOnboarding() { return gOnboarding; }
bool webCfgIsLite() { return running && webCfgUseLite(); }
bool webCfgChatPaused() { return running && gWebBuffersReclaimed; }
void webCfgSetForceAp(bool force) { gForceAp = force; }

void webCfgSetWaitCb(void (*cb)()) { gWaitCb = cb; }

void webCfgSetStaCreds(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) {
        gStaSsidOverride[0] = '\0';
        gStaPassOverride[0] = '\0';
        return;
    }
    strlcpy(gStaSsidOverride, ssid, sizeof(gStaSsidOverride));
    strlcpy(gStaPassOverride, pass ? pass : "", sizeof(gStaPassOverride));
}
const char *webCfgWifiSsid() { return gWifiSsid; }
const char *webCfgWifiPass() { return gWifiPass; }

void webCfgClearWifiCreds() {
    memset(gWifiSsid, 0, sizeof(gWifiSsid));
    memset(gWifiPass, 0, sizeof(gWifiPass));
}

void webCfgLoop() {
    if (!running) return;

    if (gCaptiveActive) gDns.processNextRequest();
    server.handleClient();

#if HAS_VNC_HOST
    // The VNC iframe streams through port 8765, so its traffic never reaches
    // WebServer's request handlers. Keep the full page alive while the host is
    // enabled; once VNC is turned off, the normal idle countdown starts here.
    if (vncHostEnabled()) {
        gLastRequestMs = millis();
        gIdleExpired = false;
    }
#endif

    // Onboarding is exempt: the user may be reading the setup page for a while
    // before submitting anything, and pulling the AP out from under a
    // half-finished first-time setup is worse than the power it saves.
    if (gIdleTimeoutMs && !gIdleExpired && !gOnboarding
        && (uint32_t)(millis() - gLastRequestMs) >= gIdleTimeoutMs) {
        gIdleExpired = true;
        Serial.printf("[web] idle %lus with no request - requesting shutdown\n",
                      (unsigned long)(gIdleTimeoutMs / 1000UL));
    }
    if (gRebootPending && (int32_t)(millis() - gRebootAtMs) >= 0) {
        gRebootPending = false;
        server.stop();
        delay(100);
        Channels.flushPersistence();
        DMs.flushPersistence();
        ESP.restart();
    }
}

bool webCfgRunning() { return running; }

bool webCfgIdleExpired() { return gIdleExpired; }
const char *webCfgIP() { return ipBuf; }

bool webCfgAnnounceRequested() {
    if (!gAnnounceReq) return false;
    gAnnounceReq = false;
    return true;
}

void webCfgQueueAnnounce() {
    gAnnounceReq = true;
}

bool webCfgTelemetryRequested() {
    if (!gTelemetryReq) return false;
    gTelemetryReq = false;
    return true;
}

void webCfgQueueTelemetry() {
    gTelemetryReq = true;
}

void webCfgQueueChatSend(bool isDm, uint32_t targetId, const char *text,
                         uint32_t replyId, uint32_t emoji) {
    gChatSendIsDm    = isDm;
    gChatSendTarget  = targetId;
    gChatSendReplyId = replyId;
    gChatSendEmoji   = emoji;
    strlcpy(gChatSendText, text ? text : "", sizeof(gChatSendText));
    gChatSendReq     = true;
}

bool webCfgTakeChatSend(bool &isDm, uint32_t &targetId,
                        char *text, size_t textLen,
                        uint32_t &replyId, uint32_t &emoji) {
    if (!gChatSendReq) return false;
    gChatSendReq = false;
    isDm     = gChatSendIsDm;
    targetId = gChatSendTarget;
    replyId  = gChatSendReplyId;
    emoji    = gChatSendEmoji;
    if (text && textLen) strlcpy(text, gChatSendText, textLen);
    return true;
}

// ── Store & Forward replay bridge ────────────────────────────────────────────
static bool gSnfReq = false;
static char gSnfResult[64] = "";

void webCfgQueueSnfRequest() { gSnfReq = true; }

bool webCfgTakeSnfRequest() {
    if (!gSnfReq) return false;
    gSnfReq = false;
    return true;
}

void webCfgSetSnfResult(const char *msg) {
    strlcpy(gSnfResult, msg ? msg : "", sizeof(gSnfResult));
}

const char *webCfgSnfResult() { return gSnfResult; }

bool webCfgTakeManualTime(int &year, int &mon, int &day, int &hour, int &minute) {
    if (!gManualTimeReq) return false;
    gManualTimeReq = false;
    year   = gManualTime[0];
    mon    = gManualTime[1];
    day    = gManualTime[2];
    hour   = gManualTime[3];
    minute = gManualTime[4];
    return true;
}

#if HAS_VNC_HOST
bool webCfgTakeVncToggle(bool &enabled) {
    if (!gVncToggleReq) return false;
    gVncToggleReq = false;
    enabled = gVncToggleOn;
    return true;
}
#endif
