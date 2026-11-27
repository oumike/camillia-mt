// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: request tracking, origin guard and route registration.
// Included in order from web_config.cpp as one translation unit; not built on its own.

// Unmatched-request handler, shared by both route sets.
// All routes register through these rather than server.on() directly, so the
// idle-timeout stamp lives in exactly one place. This core's WebServer has no
// per-request hook, and stamping 29 handlers by hand is the kind of thing a
// later route addition quietly forgets — which would show up as web config
// shutting down under an active user.
static void noteWebRequest() { gLastRequestMs = millis(); }

// ── Cross-site request / DNS-rebinding guard ──
// Requests that change state (every POST, plus GET /logout) and the two GETs
// that hand out secrets (/export, /fs-get) must name this device in Host, and
// any Origin/Referer the browser sent must point at that same Host. A request
// with neither Origin nor Referer (curl, scripts) is still accepted, so this
// never gets in the way of anything but a browser on someone else's page.

// "host[:port]" or "[v6]:port" -> host, lowercased, brackets kept off.
static String hostOnly(const String &authority) {
    String h = authority;
    if (h.startsWith("[")) {
        int close = h.indexOf(']');
        h = (close > 0) ? h.substring(1, close) : h.substring(1);
    } else {
        int colon = h.indexOf(':');
        if (colon >= 0) h = h.substring(0, colon);
    }
    h.toLowerCase();
    return h;
}

static bool isIpLiteralHost(const String &host, bool bracketed) {
    if (host.isEmpty()) return false;
    if (bracketed) return true;   // IPv6 literal
    int dots = 0;
    for (size_t i = 0; i < host.length(); i++) {
        char c = host[i];
        if (c == '.') dots++;
        else if (c < '0' || c > '9') return false;
    }
    return dots == 3;
}

// The device's own name, bare or under whatever DNS suffix the local router
// hands out (.local, .lan, .fritz.box, ...). The leftmost label must equal the
// device's MAC-derived hostname exactly, so a rebinding attacker would need to
// know it; the Origin/Referer check still applies on top.
static bool hostMatchesDeviceName(const String &host, const char *name) {
    if (!name || !name[0]) return false;
    String n = name;
    n.toLowerCase();
    if (host == n) return true;
    return host.startsWith(n) && host.length() > n.length() + 1 &&
           host[n.length()] == '.';
}

static bool hostHeaderAllowed() {
    String raw = server.hostHeader();
    raw.trim();
    if (raw.isEmpty()) return true;   // HTTP/1.0 client: nothing to rebind
    const bool bracketed = raw.startsWith("[");
    String host = hostOnly(raw);
    if (host.endsWith(".") && !bracketed) host.remove(host.length() - 1);
    if (isIpLiteralHost(host, bracketed)) return true;
    if (host == "localhost") return true;
    if (hostMatchesDeviceName(host, WiFi.getHostname())) return true;
    if (gApMode && hostMatchesDeviceName(host, WiFi.softAPgetHostname())) return true;
    return false;
}

// Origin "scheme://authority" or Referer "scheme://authority/path..." must
// carry the same authority as Host. "null" (sandboxed frames, opaque
// redirects) is a cross-site source and is refused.
static bool sourceMatchesHost(const String &src) {
    int scheme = src.indexOf("://");
    if (scheme < 0) return false;
    String authority = src.substring(scheme + 3);
    int end = authority.length();
    for (const char *stop = "/?#"; *stop; stop++) {
        int at = authority.indexOf(*stop);
        if (at >= 0 && at < end) end = at;
    }
    authority = authority.substring(0, end);
    int at = authority.lastIndexOf('@');
    if (at >= 0) authority = authority.substring(at + 1);
    return authority.equalsIgnoreCase(server.hostHeader());
}

static bool originAllowed() {
    if (server.hasHeader("Origin")) {
        String origin = server.header("Origin");
        origin.trim();
        if (origin.length()) return sourceMatchesHost(origin);
    }
    if (server.hasHeader("Referer")) {
        String referer = server.header("Referer");
        referer.trim();
        if (referer.length()) return sourceMatchesHost(referer);
    }
    return true;
}

static bool routeNeedsOriginGuard(const char *uri, HTTPMethod method) {
    if (method != HTTP_GET) return true;
    return strcmp(uri, "/logout") == 0 || strcmp(uri, "/export") == 0 ||
           strcmp(uri, "/fs-get") == 0;
}

static bool requestPassesOriginGuard(const char *uri, HTTPMethod method) {
    if (!routeNeedsOriginGuard(uri, method)) return true;
    if (!hostHeaderAllowed()) {
        Serial.printf("[web] refused %s: Host '%s'\n", uri, server.hostHeader().c_str());
        return false;
    }
    // Origin/Referer only for state changes: a cross-site link to /export
    // is harmless (the browser downloads it, the linking page reads nothing).
    const bool changesState = method != HTTP_GET || strcmp(uri, "/logout") == 0;
    if (changesState && !originAllowed()) {
        Serial.printf("[web] refused %s: cross-origin request\n", uri);
        return false;
    }
    return true;
}

static void sendOriginRefused() {
    server.send(403, "text/plain", "Forbidden: cross-site request refused");
}

static void onRoute(const char *uri, HTTPMethod method,
                    WebServer::THandlerFunction fn) {
    server.on(uri, method, [fn, uri, method]() {
        noteWebRequest();
        if (!requestPassesOriginGuard(uri, method)) { sendOriginRefused(); return; }
        fn();
    });
}

// The upload callback runs (per chunk) before fn; a refused request never
// reaches the route's upload handler, and fn then answers 403.
static void onRoute(const char *uri, HTTPMethod method,
                    WebServer::THandlerFunction fn,
                    WebServer::THandlerFunction upload) {
    server.on(uri, method,
              [fn, uri, method]() {
                  noteWebRequest();
                  if (!requestPassesOriginGuard(uri, method)) { sendOriginRefused(); return; }
                  fn();
              },
              [upload, uri, method]() {
                  noteWebRequest();
                  if (!requestPassesOriginGuard(uri, method)) return;
                  upload();
              });
}

static void registerNotFound() {
    // Log any unmatched request so we can see exactly what a failing tab hits
    // (the default handler only prints a generic "request handler not found").
    server.onNotFound([]() {
        // Counts as activity: a browser retrying a stale URL still means
        // someone is using this.
        noteWebRequest();
        // Safari asks for these on every page load — four requests per load,
        // each one a full connect/parse/answer cycle this server pays for out
        // of the main loop, and four ESP error lines in a log someone is
        // usually reading a real problem out of. A 404 is not cacheable enough
        // to stop the asking; an empty 204 with a long max-age is. Matched
        // here rather than registered as routes so it costs no handler heap,
        // which the AP path does not have to spare.
        if (server.uri().startsWith("/apple-touch-icon")) {
            server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
            server.send(204, "text/plain", "");
            return;
        }
        Serial.printf("[web] 404 %s %s\n",
                      (server.method() == HTTP_GET) ? "GET" : "POST",
                      server.uri().c_str());
        server.send(404, "text/plain", "Not found");
    });
}

#if HAS_WEB_FILES
// Registered whether or not the tab is on: each handler checks the setting per
// request, so switching it on needs no web config restart.
static void registerFilesRoutes() {
    onRoute("/fs-list",   HTTP_GET,  handleGetFsList);
    onRoute("/fs-get",    HTTP_GET,  handleGetFsGet);
    onRoute("/fs-put",    HTTP_POST, handleFsPutDone, handleFsPutUpload);
    onRoute("/fs-delete", HTTP_POST, handlePostFsDelete);
    onRoute("/fs-mkdir",  HTTP_POST, handlePostFsMkdir);
    onRoute("/fs-copy",   HTTP_POST, handlePostFsCopy);
}
#endif

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
#if HAS_UI_THEMES
    onRoute("/theme-save",        HTTP_POST, handlePostThemeSave);
    onRoute("/theme-delete",      HTTP_POST, handlePostThemeDelete);
#endif
#if HAS_VNC_HOST
    onRoute("/vnc-status",        HTTP_GET,  handleGetVncStatus);
    onRoute("/vnc-toggle",        HTTP_POST, handlePostVncToggle);
    onRoute("/vnc-unlock",        HTTP_POST, handlePostVncUnlock);
#endif
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    onRoute("/chat-targets",      HTTP_GET,  handleGetChatTargets);
    onRoute("/chat-data",         HTTP_GET,  handleGetChatData);
    onRoute("/chat-send",         HTTP_POST, handlePostChatSend);
#endif
    onRoute("/chart-data",        HTTP_GET,  handleGetChartData);
    onRoute("/logout",            HTTP_GET,  handleGetLogout);
    onRoute("/favicon.ico",       HTTP_GET,  handleGetFavicon);
    onRoute("/app.css",           HTTP_GET,  handleGetAppCss);
    onRoute("/announce",          HTTP_POST, handlePostAnnounce);
    onRoute("/telemetry",         HTTP_POST, handlePostTelemetry);
    onRoute("/cs-check",          HTTP_POST, handlePostChatServerCheck);
    if (gOnScreenshotPng) {
        onRoute("/screenshot",    HTTP_GET,  handleGetScreenshot);
    }
#if HAS_STATE_MAPS
    // Map cache. Full-page routes only: filling it needs the browser to reach
    // OSM, which means a station connection, and the lite page AP mode serves
    // has no Utilities tab to put the control on.
    onRoute("/state-map-status",  HTTP_GET,  handleGetStateMapStatus);
    onRoute("/state-map-upload",  HTTP_POST, handleStateMapUploadDone, handleStateMapUpload);
    onRoute("/detail-map-upload", HTTP_POST, handleDetailMapUploadDone, handleDetailMapUpload);
    onRoute("/map-tile-status",   HTTP_GET,  handleGetMapTileStatus);
    onRoute("/map-tile-upload",   HTTP_POST, handleMapTileUploadDone, handleMapTileUpload);
    onRoute("/map-tile-fetch",    HTTP_POST, handlePostMapTileFetch);
#endif
    onRoute("/nodes.csv",         HTTP_GET,  handleGetNodesCsv);
    onRoute("/messages.csv",      HTTP_GET,  handleGetMessagesCsv);
    onRoute("/wardrive.csv",      HTTP_GET,  handleGetWardriveCsv);
    onRoute("/clear-wardrive",    HTTP_POST, handlePostClearWardrive);
    onRoute("/export",            HTTP_GET,  handleGetExport);
    onRoute("/export-sd",         HTTP_POST, handlePostExportSd);
    onRoute("/import",            HTTP_POST, handleImportDone, handleImportUpload);
#if HAS_UI_COLOR_OPTIONS
    onRoute("/reset-chat-colors", HTTP_POST, handlePostResetChatColors);
#endif
    onRoute("/reboot",            HTTP_POST, handlePostReboot);
    onRoute("/community-preset",  HTTP_POST, handlePostCommunityPreset);
    onRoute("/clear-messages",    HTTP_POST, handlePostClearMessages);
#if !defined(PLATFORM_LINUX)
    onRoute("/wifi-add",          HTTP_POST, handlePostWifiAdd);
    onRoute("/wifi-forget",       HTTP_POST, handlePostWifiForget);
    onRoute("/wifi-use",          HTTP_POST, handlePostWifiUse);
#endif
#if HAS_ADMIN_TERMINAL
    // Registered here only: the AP-lite page gets none of these, same heap rule
    // as chat and the live feed.
    onRoute("/admin-data",        HTTP_GET,  handleGetAdminData);
    onRoute("/admin-help",        HTTP_GET,  handleGetAdminHelp);
    onRoute("/admin-send",        HTTP_POST, handlePostAdminSend);
    onRoute("/admin-peer-verify", HTTP_POST, handlePostAdminPeerVerify);
    onRoute("/admin-rescan",      HTTP_POST, handlePostAdminRescan);
#endif
#if HAS_SD_MALWARE_SCAN
    onRoute("/sd-scan",           HTTP_POST, handlePostSdScan);
    onRoute("/sd-repair",         HTTP_POST, handlePostSdRepair);
#endif
    onRoute("/clear-nodes",       HTTP_POST, handlePostClearNodes);
#if HAS_STATE_MAPS
    onRoute("/clear-maps",        HTTP_POST, handlePostClearMaps);
#endif
    onRoute("/node-favorite",     HTTP_POST, handlePostNodeFavorite);
    onRoute("/factory-reset",     HTTP_POST, handlePostFactoryReset);
#if HAS_WEB_FILES
    registerFilesRoutes();
#endif
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
    gStartDeferred = false;
    bool reclaimUiBuffers = false;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    reclaimUiBuffers = true;
#elif defined(PLATFORM_LINUX)
    reclaimUiBuffers = false;   // ordinary Linux RAM; chat keeps running
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
#if FEATURE_LICENSED_MODE
    gLicReq = 0;
#endif
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

    const char *headers[] = {"Cookie", "Origin", "Referer"};
    server.collectHeaders(headers, 3);

    auto startApMode = [&](const char *modeTag, bool onboarding) {
        gOnboarding = onboarding;
        gApMode     = true;   // "/" serves lite here unless the board has PSRAM
        WiFi.disconnect(false);
        if (!ensureWifiMode(WIFI_AP, modeTag)) return false;
        delay(120);
        bool apStarted = WiFi.softAP("camillia-mt");
#if defined(DEVICE_TDISPLAY_P4)
        if (!apStarted && recoverP4Hosted(WIFI_AP, "SoftAP start")) {
            delay(120);
            apStarted = WiFi.softAP("camillia-mt");
        }
#endif
        if (!apStarted) {
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

        // Both AP paths serve web config lite, except on boards with the heap
        // for the full page; those also keep the quick WiFi setup form, which
        // is how a device on its own AP gets onto a network.
        if (webCfgUseLite()) {
            registerLiteRoutes();
        } else {
            registerCommonRoutes();
            onRoute("/setup",   HTTP_GET,  handleGetOnboard);
            onRoute("/onboard", HTTP_POST, handlePostOnboard);
        }
        // No delay(1) inside handleClient() when no client is waiting.
        // WebServer takes that delay on every single call, which — with
        // webCfgLoop() running once per main-loop pass — is a full FreeRTOS
        // tick added to every iteration for the entire time the server is up,
        // whether or not anyone is connected. The loop already ends with its
        // own delay(5), so the yield it exists to provide is still there; this
        // only stops paying for it twice, and it is one tick less latency
        // before a request that has arrived gets noticed.
        gFirstServiceMs = 0;   // clocks start at first service, not here
        server.enableDelay(false);
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
    // With the MQTT bridge on, the station is not web config's to give up: the
    // fallback SoftAP would cut the bridge off for the whole session, which
    // the idle timeout may never end. Web config waits for the network
    // instead, and the main loop starts it once the station associates.
    auto fallBackToAp = [&]() -> bool {
        if (gCfg && gCfg->mqttEnabled) {
            Serial.println("[web] no station and MQTT is on - waiting for WiFi, not falling back to AP");
            gStartDeferred = true;
            return false;
        }
        return startApMode("AP fallback", false);
    };
    if (!ensureWifiMode(WIFI_STA, "STA connect")) {
        Serial.println("[web] STA mode failed — using AP fallback");
        bool ok = fallBackToAp();
        if (!ok) restoreUiBuffersIfNeeded();
        return ok;
    }
#if defined(DEVICE_TDISPLAY_P4)
    bool hostedRecoveryAttempted = false;
#endif
    wl_status_t beginStatus = WiFi.begin(staConnectSsid(), staConnectPass());
#if defined(DEVICE_TDISPLAY_P4)
    if (beginStatus == WL_NO_SHIELD && recoverP4Hosted(WIFI_STA, "STA begin")) {
        hostedRecoveryAttempted = true;
        beginStatus = WiFi.begin(staConnectSsid(), staConnectPass());
    }
#endif
    if (beginStatus == WL_CONNECT_FAILED || beginStatus == WL_NO_SHIELD) {
        Serial.printf("[web] STA begin failed (%d)\n", (int)beginStatus);
        logWifiHeapDiag("STA begin failed");
        bool ok = fallBackToAp();
        if (!ok) restoreUiBuffersIfNeeded();
        return ok;
    }
    Serial.printf("[web] connecting to \"%s\"%s ...\n", staConnectSsid(),
                  gStaSsidOverride[0] ? " (picker)" : "");

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        wl_status_t st = WiFi.status();
        if (st == WL_NO_SHIELD) {
#if defined(DEVICE_TDISPLAY_P4)
            if (!hostedRecoveryAttempted && recoverP4Hosted(WIFI_STA, "STA connect")) {
                hostedRecoveryAttempted = true;
                (void)WiFi.begin(staConnectSsid(), staConnectPass());
                start = millis();
                continue;
            }
#endif
            Serial.println("[web] WiFi stack unavailable during STA connect");
            logWifiHeapDiag("STA connect unavailable");
            bool ok = fallBackToAp();
            if (!ok) restoreUiBuffersIfNeeded();
            return ok;
        }
        if (millis() - start >= kConnectTimeout) {
            Serial.println("[web] STA connect timeout — falling back to AP mode");
            bool ok = fallBackToAp();
            if (!ok) restoreUiBuffersIfNeeded();
            return ok;
        }
        if (gWaitCb) gWaitCb();
        delay(100);
    }

    WiFi.localIP().toString().toCharArray(ipBuf, sizeof(ipBuf));
#if defined(PLATFORM_LINUX)
    {
        const size_t n = strlen(ipBuf);
        snprintf(ipBuf + n, sizeof(ipBuf) - n, ":%u", (unsigned)kWebCfgPort);
    }
#endif

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
#if HAS_WEB_FILES
        // Station mode only, so not inside registerLiteRoutes(), which AP
        // onboarding shares on every board.
        registerFilesRoutes();
#endif
    } else {
        registerCommonRoutes();
    }
    // Same reason as the AP path above: WebServer's idle delay(1) is a
    // per-main-loop-pass tax for as long as the server is running.
    gFirstServiceMs = 0;   // clocks start at first service, not here
    server.enableDelay(false);
    server.begin();
    running = true;
    Serial.printf("[web] ready at http://%s/\n", ipBuf);
    logWifiHeapDiag("server up (STA)");
    return true;
}

