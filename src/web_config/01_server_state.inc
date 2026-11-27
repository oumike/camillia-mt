// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: AP heap checks, release check, idle timeout, reboot scheduling.
// Included in order from web_config.cpp as one translation unit; not built on its own.

// True when the current response should be the lite page.
// Whether AP mode can carry the full page. Measured on the T-Deck Plus with
// every tab open: internal heap never fell below 65 KB free, largest block
// 65.5 KB, where lite was sized for ~20 KB free and a 13 KB block. Boards
// without PSRAM keep their chat buffers in that same internal heap (they free
// them to start Wi-Fi at all), so they stay on lite.
static bool apHasRoomForFullPage() {
#if defined(BOARD_HAS_PSRAM)
    return ESP.getPsramSize() > 0;
#else
    return false;
#endif
}
static inline bool webCfgUseLite() {
    return kLiteOnlyBoard || (gApMode && !apHasRoomForFullPage());
}
static char           ipBuf[24]        = "";
static char           sessionToken[17] = "";   // hex token; empty = no session
static RhinoConfig   *gCfg             = nullptr;

// The credentials webCfgBegin() should actually associate with: the picker's
// override when one was pushed, otherwise what is stored in the config.
static const char *staConnectSsid() {
#if defined(PLATFORM_LINUX)
    // The OS owns the network: never fall into onboarding AP mode for want of
    // credentials of our own.
    return "system network";
#endif
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
// A community preset to apply (kCommunityPresets index), and when. The delay
// lets the redirect reach the browser before the commit reboots the device,
// as scheduleReboot() does for the restarting actions.
static int            gCommunityReq    = -1;
static uint32_t       gCommunityAtMs   = 0;
static uint32_t       gRebootAtMs      = 0;
#if FEATURE_LICENSED_MODE
// A licensed-mode transition from /save, run by webCfgLoop() once the redirect
// has gone out: enterLicensedMode()/exitLicensedMode() save and reboot, so they
// cannot run inside the handler. 1 = enter (or re-enter), 2 = exit.
static uint8_t        gLicReq          = 0;
static uint32_t       gLicAtMs         = 0;
static char           gLicCall[lic::kCallSignMax + 1] = "";
static char           gLicSuffix[40]   = "";
static uint8_t        gLicPwr          = 0;
#endif
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

#if defined(DEVICE_TDISPLAY_P4)
static void logP4HostedInfoOnce() {
    static bool logged = false;
    if (logged || !hostedIsInitialized()) return;

    uint32_t hostMajor = 0;
    uint32_t hostMinor = 0;
    uint32_t hostPatch = 0;
    uint32_t slaveMajor = 0;
    uint32_t slaveMinor = 0;
    uint32_t slavePatch = 0;
    hostedGetHostVersion(&hostMajor, &hostMinor, &hostPatch);
    const char *target = hostedGetSlaveTargetName();
    hostedGetSlaveVersion(&slaveMajor, &slaveMinor, &slavePatch);
    Serial.printf("[wifi-p4] ESP-Hosted host=%lu.%lu.%lu slave=%lu.%lu.%lu target=%s\n",
                  (unsigned long)hostMajor,
                  (unsigned long)hostMinor,
                  (unsigned long)hostPatch,
                  (unsigned long)slaveMajor,
                  (unsigned long)slaveMinor,
                  (unsigned long)slavePatch,
                  target ? target : "unknown");
    if (hostMajor != slaveMajor || hostMinor != slaveMinor || hostPatch != slavePatch) {
        Serial.println("[wifi-p4] ESP-Hosted firmware mismatch; flash the matching C6 image");
    }
    logged = true;
}

static bool recoverP4Hosted(wifi_mode_t mode, const char *stageTag) {
    if (hostedIsBLEActive()) {
        Serial.println("[wifi-p4] C6 reset skipped because ESP-Hosted BLE is active");
        return false;
    }
    Serial.printf("[wifi-p4] Hosted unavailable at %s; resetting ESP32-C6\n",
                  stageTag ? stageTag : "startup");
    WiFi.disconnect(true);
    (void)WiFi.mode(WIFI_OFF);
    if (!tdisplayP4IoResetEsp32C6()) {
        Serial.println("[wifi-p4] ESP32-C6 reset failed");
        return false;
    }
    if (!WiFi.mode(mode)) {
        Serial.println("[wifi-p4] ESP-Hosted reinitialization failed");
        return false;
    }
    logP4HostedInfoOnce();
    Serial.println("[wifi-p4] ESP-Hosted recovered after C6 reset");
    return true;
}
#endif

static bool ensureWifiMode(wifi_mode_t mode, const char *stageTag) {
    if (WiFi.getMode() == mode) {
#if defined(DEVICE_TDISPLAY_P4)
        logP4HostedInfoOnce();
#endif
        return true;
    }
    if (WiFi.mode(mode)) {
#if defined(DEVICE_TDISPLAY_P4)
        logP4HostedInfoOnce();
#endif
        return true;
    }
#if defined(DEVICE_TDISPLAY_P4)
    if (recoverP4Hosted(mode, stageTag)) return true;
#endif
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

void webCfgSetIdleTimeoutS(uint32_t secs) {
    // Only while a session is up: webCfgBegin() seeds it from the config, and
    // webCfgEnd() leaves it zero.
    if (running) gIdleTimeoutMs = secs * 1000UL;
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

#if HAS_SD_MALWARE_SCAN
// Last scan's outcome, kept so the Utilities pane can report it and so the
// delete button only exists while there is something to delete. Declared up
// here because the pane that renders them is built well above the handlers that
// set them.
static char     gSdScanResult[96] = "";
static uint16_t gSdScanHits = 0;
#endif

static void setFlashMsg(const char *msg) {
    if (!msg) { gFlashMsg[0] = '\0'; return; }
    strncpy(gFlashMsg, msg, sizeof(gFlashMsg) - 1);
    gFlashMsg[sizeof(gFlashMsg) - 1] = '\0';
}

static void redirectHomeWithFlash(const char *msg = "") {
    if (msg && msg[0]) setFlashMsg(msg);
    redirect("/");
}

static void webCfgRunPending(bool now);

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

// The full page's head, in two halves around the device's theme colours:
// sendFullHead() links the cached /app.css (web_app_assets.h) and writes the
// theme's :root block between them.
static const char kHeadOpen[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<title>Camillia for Meshtastic</title>"
    "<link rel='icon' type='image/svg+xml' href='/favicon.ico'>"
    "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'>";
static const char kHeadClose[] = "</head><body>";

// Head for the AP-mode pages (web config lite and its /setup form). AP mode
// leaves ~20 KB of internal heap with a largest block near 13 KB, so this is
// deliberately a fraction of the full page's stylesheet (kAppCss, ~16 KB): no
// custom properties, no theme table, no layout grid. Everything here streams from flash, so nothing
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
        "<option value='EU_866'>EU 866 (865.6&ndash;867.6 MHz)</option>"
        "<option value='EU_N_868'>EU Narrow 868 (869.4&ndash;869.65 MHz)</option>"
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
        "<option value='Medium Turbo'>Medium Turbo</option>"
        "<option value='Lite Fast'>Lite Fast</option>"
        "<option value='Lite Slow'>Lite Slow</option>"
        "<option value='Narrow Fast'>Narrow Fast</option>"
        "<option value='Narrow Slow'>Narrow Slow</option>"
#if LORA_BW_CODE_MIN <= 16
        // 15.6 kHz. Listed only where the radio can produce it — see
        // LORA_BW_CODE_MIN. Selecting one elsewhere would reconfigure the radio
        // into silence.
        "<option value='Tiny Fast'>Tiny Fast</option>"
        "<option value='Tiny Slow'>Tiny Slow</option>"
#endif
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
#if LORA_BW_CODE_MIN <= 16
        "<option value='16'>15.6 kHz</option>"
#endif
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

