// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: onboarding page, login and the config form save.
// Included in order from web_config.cpp as one translation unit; not built on its own.

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
    // Overwrites the WiFi credentials and reboots, so once a network is set up
    // it needs the same login as /save. First-boot onboarding has nothing to
    // overwrite and no session to have; isLoggedIn() is always true with auth off.
    if (!gOnboarding && !isLoggedIn()) { redirect("/login"); return; }
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
    appendAttr(html, ssid.c_str());
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
    // Both radio modes serve a config page from "/": the full one, or web
    // config lite in AP mode on a board without the heap for it.
    Serial.printf("[web] GET / (%s%s)\n", webCfgUseLite() ? "lite" : "full", gApMode ? ", AP" : "");
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

#if FEATURE_LICENSED_MODE
    // Licensed mode. Decided and validated before any field is written, so a
    // refused call sign saves nothing: the error is shown and the form can be
    // corrected. Enter (also a call-sign change while licensed, which re-enters
    // to recompose the long name) and exit run after the redirect, from
    // webCfgLoop(), because both reboot.
    const bool licWas  = cfgBefore.licensedMode;
    const bool licForm = server.hasArg("lic_present");
    bool licEnter = false, licExit = false;
    char licCall[lic::kCallSignMax + 1] = "";
    char licSuffix[sizeof(gLicSuffix)] = "";
    uint8_t licPwr = (uint8_t)constrain((int)gCfg->loraPower, 1, (int)MESH_POWER);
    if (licForm) {
        const bool want = server.hasArg("lic_en");
        utf8util::copyTruncate(licSuffix, sizeof(licSuffix), server.arg("lic_suffix").c_str());
        if (server.hasArg("lic_pwr")) {
            licPwr = (uint8_t)constrain((int)server.arg("lic_pwr").toInt(), 1, (int)MESH_POWER);
        }
        if (want) {
            const String call = server.arg("lic_call");
            // Never truncated: an over-long call sign is refused, not shortened.
            if (!lic::normalizeCallSign(call.c_str(), licCall, sizeof(licCall))) {
                redirectHomeWithFlash(call.length()
                    ? "Not saved: invalid call sign (1-15 letters, digits or /)."
                    : "Not saved: enter a call sign to turn licensed mode on.");
                return;
            }
            licEnter = !licWas || strcmp(licCall, cfgBefore.callSign) != 0;
        } else {
            licExit = licWas;
        }
    }
#endif
    // Node identity
    String lng  = server.arg("long");
    String shrt = server.arg("short");
    utf8util::copyTruncate(gCfg->nodeLong, MESH_LONG_NAME_MAX_BYTES + 1, lng.c_str());
    utf8util::copyTruncate(gCfg->nodeShort, sizeof(gCfg->nodeShort), shrt.c_str());
    // An unchecked checkbox sends nothing, so applying this unconditionally
    // would switch the setting off for any form that did not carry it (an
    // older page, a partial send). Gated on the marker the page emits beside it.
    if (server.hasArg("nodeid_present")) {
        gCfg->nodeIdFromPubKey = (server.arg("nodeid_pubkey") == "1");
    }
    // A select always submits, so its own name is the marker.
    if (server.hasArg("sig_policy")) {
        gCfg->packetSigPolicy = sigPolicyCoerce(server.arg("sig_policy").toInt());
    }

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

    // Node ID override. Blank (or "none"/"0") is off; anything that does not
    // name a node keeps the current value rather than silently clearing it.
    if (server.hasArg("node_id_ovr")) {
        String ovr = server.arg("node_id_ovr");
        ovr.trim();
        if (ovr.length() == 0 || ovr.equalsIgnoreCase("none") || ovr == "0") {
            gCfg->nodeIdOverride = 0;
        } else {
            const uint32_t id = parseNodeIdText(ovr.c_str());
            if (id != 0 && id != 0xFFFFFFFFUL) gCfg->nodeIdOverride = id;
        }
    }

    // Device
    gCfg->deviceRole        = cfgCoerceDeviceRole((uint8_t)server.arg("role").toInt());
    gCfg->rebroadcastMode   = (uint8_t)constrain(server.arg("rebroadcast").toInt(), 0,  4);
#if FEATURE_LICENSED_MODE
    if (!licWas)   // locked to the station-ID interval while licensed; not on the form
#endif
    gCfg->nodeInfoIntervalS = (uint32_t)constrain(server.arg("nodeinfo_intv").toInt(),
                                                  (long)60, (long)CFG_BROADCAST_INTERVAL_MAX_S);
    gCfg->posIntervalS      = (uint32_t)constrain(server.arg("pos_intv").toInt(),
                                                  (long)60, (long)CFG_BROADCAST_INTERVAL_MAX_S);
    // hasArg-guarded: the lite page does not render it.
    if (server.hasArg("gps_duty")) {
        gCfg->gpsDutyCycleEnabled = server.arg("gps_duty") == "1";
    }
    if (server.hasArg("gps_poll_s")) {
        gCfg->gpsPollIntervalS = (uint32_t)constrain(server.arg("gps_poll_s").toInt(), 0, 3600);
    } else if (server.hasArg("gps_poll_ms")) {
        long oldMsSigned = server.arg("gps_poll_ms").toInt();
        if (oldMsSigned < 0) oldMsSigned = 0;
        uint32_t oldMs = (uint32_t)oldMsSigned;
        gCfg->gpsPollIntervalS = (oldMs == 0) ? 0 : (uint32_t)constrain((long)((oldMs + 999UL) / 1000UL), (long)0, (long)3600);
    }
#if !HAS_SYSTEM_SETTINGS   // the OS's clock and zone: not on the page
    strncpy(gCfg->tzDef, server.arg("tzdef").c_str(), sizeof(gCfg->tzDef) - 1);
    gCfg->tzDef[sizeof(gCfg->tzDef) - 1] = '\0';
    strncpy(gCfg->ntpServer, server.arg("ntp_server").c_str(), sizeof(gCfg->ntpServer) - 1);
    gCfg->ntpServer[sizeof(gCfg->ntpServer) - 1] = '\0';
#endif
#if HAS_NODE_LOS
    // Trailing slashes are stripped by the fetcher, so both forms are accepted.
    strncpy(gCfg->losElevServer, server.arg("los_elev").c_str(),
            sizeof(gCfg->losElevServer) - 1);
    gCfg->losElevServer[sizeof(gCfg->losElevServer) - 1] = '\0';
#endif
#if HAS_WEATHER
    // Trailing slashes are stripped by the fetcher here too.
    strncpy(gCfg->weatherServer, server.arg("weather_srv").c_str(),
            sizeof(gCfg->weatherServer) - 1);
    gCfg->weatherServer[sizeof(gCfg->weatherServer) - 1] = '\0';
#endif
    if (!gCfg->ntpServer[0]) {
        strncpy(gCfg->ntpServer, MY_NTP_SERVER, sizeof(gCfg->ntpServer) - 1);
        gCfg->ntpServer[sizeof(gCfg->ntpServer) - 1] = '\0';
    }

    // Time source, and the clock itself when set manually. The clock is queued
    // rather than set here: the main loop owns it, and this handler runs on the
    // web server task.
    if (server.hasArg("clock_format")) {
        gCfg->clockFormat = cfgCoerceClockFormat(server.arg("clock_format").toInt());
    }
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
#if !defined(DEVICE_CARDPUTER_ZERO)  // the system's GPS: always on
    gCfg->gpsEnabled = (server.arg("gpsEnabled") == "1");
#endif
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
#if FEATURE_LICENSED_MODE
        if (licWas) kh = "";   // keys stay empty while licensed
#endif
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
            snprintf(field, sizeof(field), "ch%d_aead", i);
            CHANNEL_KEYS[i].useAead = (server.arg(field) == "1");
        }
        // AEAD needs a key; cleared without one, as Meshtastic's fixupChannel()
        // does, so it cannot switch on later by surprise when a key is added.
        if (!channelUsesAead(CHANNEL_KEYS[i])) CHANNEL_KEYS[i].useAead = false;
        // Recompute on-air hash from current name + key (+ AEAD)
        CHANNEL_KEYS[i].hash = channelKeyHash(CHANNEL_KEYS[i]);
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
    if (server.hasArg("rx_boost")) {
        gCfg->loraRxBoostedGain = server.arg("rx_boost") == "1";
    }

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
#if HAS_RUNTIME_ORIENTATION
    // Gated on HAS_RUNTIME_ORIENTATION alone, matching the control that renders
    // it. It used to sit inside the FEATURE_LOCK_SCREEN block below, which is
    // true on both boards that have runtime orientation today and so never bit
    // -- but the two settings are unrelated, and a board with one and not the
    // other would have rendered the select and silently discarded the answer.
    //
    // hasArg-guarded like brightness: a lite page that does not render the
    // control must not be read as a request to set it to zero.
    if (server.hasArg("orientation")) {
        // Clamped rather than trusted: this arrives from a form post, and a
        // value past the end would be written to NVS and read back next boot.
        const long o = server.arg("orientation").toInt();
        gCfg->uiOrientation = (uint8_t)((o >= 0 && o <= 2) ? o : 0);
    }
#endif
#if FEATURE_LOCK_SCREEN
    // hasArg-guarded for the same reason as brightness above.
    if (server.hasArg("lock_brightness")) {
        gCfg->lockScreenBrightness =
            cfgCoerceBrightness(server.arg("lock_brightness").toInt());
    }
#endif
#if !HAS_SYSTEM_SETTINGS   // the system's screen timeout: not on the page
    gCfg->screenOnSecs    = (uint32_t)server.arg("screen_on").toInt();
#endif
#if FEATURE_LOCK_SCREEN
    // hasArg-guarded like volume and brightness above: a lite page that does not
    // render these must leave the stored values alone rather than zero them —
    // and zero here is a real setting ("stay on"), so a missing field silently
    // becoming one would be worse than usual.
    if (server.hasArg("lock_screen")) {
        gCfg->lockScreenEnabled = server.arg("lock_screen").toInt() != 0;
    }
    if (server.hasArg("lock_screen_off")) {
        gCfg->lockScreenOffSecs =
            cfgCoerceLockScreenOffSecs((uint32_t)server.arg("lock_screen_off").toInt());
    }
#endif
    // Kept because the auto-favorite radius below is submitted in whatever units
    // the page was *rendered* in, which is not necessarily the units this same
    // POST is switching to.
    // Absent (an older cached page) keeps the current language rather than
    // falling back to English.
    if (server.hasArg("ui_lang")) {
        const long lang = server.arg("ui_lang").toInt();
        gCfg->uiLanguage = (lang >= 0 && lang < LANG_COUNT) ? (uint8_t)lang : LANG_EN;
    }
    // hasArg-guarded like splash_melody below: absent on builds without the
    // word list, and on a page cached from before the setting existed.
    if (server.hasArg("spell_check")) {
        gCfg->spellCheckEnabled = server.arg("spell_check").toInt() != 0;
    }
#if defined(DEVICE_TDISPLAY_P4)
    // Absent (lite, or a cached page from before this field) leaves it alone.
    // The confirmation happened in the browser before the form was submitted.
    if (server.hasArg("antenna")) {
        gCfg->p4Antenna = (server.arg("antenna") == "1") ? kP4AntennaExternal : 0;
    }
#endif
    const uint8_t prevUnits = gCfg->displayUnits;
    gCfg->displayUnits    = server.arg("disp_units").toInt() != 0 ? 1 : 0;
    if (server.hasArg("batt_display")) {
        gCfg->battDisplayMode =
            (uint8_t)constrain(server.arg("batt_display").toInt(), 0, BATT_DISPLAY_MAX);
    }
#if HAS_CHAT_STYLE_OPTIONS && !defined(DEVICE_CARDPUTER_LORA_HAT)
    if (server.hasArg("chat_style")) {
        long cs = server.arg("chat_style").toInt();
        gCfg->chatStyle = cfgCoerceChatStyle((int)cs);
    }
#elif !HAS_CHAT_STYLE_OPTIONS
    gCfg->chatStyle = CHAT_STYLE_OUTLINE;
#endif
    if (server.hasArg("batt_cal")) {
        gCfg->battCalTrim = cfgCoerceBattCalTrim(server.arg("batt_cal").toInt());
    }
#if HAS_UI_COLOR_OPTIONS
    if (server.hasArg("chat_colors")) {
        gCfg->chatColorsEnabled = server.arg("chat_colors") == "1";
    }
    if (server.hasArg("user_msg_color")) {
        const long c = server.arg("user_msg_color").toInt();
        if (c == USER_MSG_COLOR_DEFAULT || c == USER_MSG_COLOR_CUSTOM
            || (c >= 0 && c < kUserMsgPaletteCount)) {
            gCfg->userMsgColor = (uint8_t)c;
        }
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
        gCfg->nodeArchiveShow    = server.hasArg("node_archive_show");
    }
    // Same disabled-checkbox guard as the archive above.
    if (wardriveLogFilePath() && wardriveLogAvailable()) {
        gCfg->wardriveLogEnabled = server.hasArg("wardrive_log");
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
    // hasArg-guarded like nav_bar: only rendered on builds that have the bar.
    // Stored only when it differs from what is in force, so a save that leaves
    // the select alone keeps an unset default unset -- on the P4 that default
    // follows the orientation, and pinning it here would carry landscape's
    // column into portrait.
    if (server.hasArg("nav_align")) {
        const uint8_t want = navAlignParse(server.arg("nav_align").c_str());
        if (want != kNavAlignUnset
            && want != navAlignCode(cfgNavAlign(*gCfg))) {
            gCfg->navAlign = want;
        }
    }
#if defined(DEVICE_TDECK_PRO) || defined(DEVICE_TDISPLAY_P4)
    if (server.hasArg("kb_light_on")) {
        gCfg->kbBacklightEnabled = server.arg("kb_light_on") == "1";
    }
#endif
#if HAS_KB_BACKLIGHT_LEVEL
    // hasArg-guarded like the two above: the control only exists on the one
    // board that has this, and an absent field would otherwise read back as 0 --
    // which here means switching the keypad light off until a power cycle.
    if (server.hasArg("keypad_light")) {
        gCfg->kbBacklightLevel =
            cfgCoerceKbBacklightLevel(server.arg("keypad_light").toInt());
        keyboardSetKeypadBacklight(gCfg->kbBacklightLevel);
    }
#endif
#if HAS_KB_LIGHT_TIMEOUT
    // hasArg-guarded like keypad_light: only rendered where the board has it.
    if (server.hasArg("kb_light_timeout")) {
        gCfg->kbLightTimeoutS =
            cfgCoerceKbLightTimeout(server.arg("kb_light_timeout").toInt());
    }
#endif
#if HAS_POWER_LED_LEVEL
    // Picked up by the status-light poll within a second; no reboot.
    if (server.hasArg("power_led_pct")) {
        gCfg->powerLedPct = cfgCoercePowerLedPct(server.arg("power_led_pct").toInt());
    }
#endif
#if HAS_CHANNEL_PANEL_LOCK
    // onWebCfgSaved() rebuilds the screen around it; no reboot.
    if (server.hasArg("channel_panel_lock")) {
        cfgSetChannelPanelLocked(*gCfg, server.arg("channel_panel_lock") == "1");
    }
#endif
#if HAS_NOTIFY_LED
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
#if HAS_UI_THEMES
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
#else
    gCfg->uiTheme = MY_UI_THEME;
    gCfg->uiMode = UI_MODE_LIGHT;
#endif

    // Power
#if !HAS_SYSTEM_SETTINGS   // no Power section on the page
    gCfg->isPowerSaving = server.arg("pwr_saving").toInt() != 0;
#endif
    if (server.hasArg("dbg_lite")) setMonitorDebugEnabled(server.arg("dbg_lite") == "1");

    // Modules
    uint32_t telemetryIntervalS = 3600UL;
    if (server.hasArg("tel_bcast_mins")) {
        long mins = server.arg("tel_bcast_mins").toInt();
        if (mins < 60L) mins = 60L;
        if (mins > (long)(CFG_BROADCAST_INTERVAL_MAX_S / 60UL)) mins = (long)(CFG_BROADCAST_INTERVAL_MAX_S / 60UL);
        telemetryIntervalS = (uint32_t)mins * 60UL;
    } else if (server.hasArg("tel_dev_intv")) {
        long sec = server.arg("tel_dev_intv").toInt();
        if (sec < 3600L) sec = 3600L;
        telemetryIntervalS = (uint32_t)sec;
    }
    if (telemetryIntervalS > CFG_BROADCAST_INTERVAL_MAX_S) telemetryIntervalS = CFG_BROADCAST_INTERVAL_MAX_S;

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
        if (mins > (long)(CFG_BROADCAST_INTERVAL_MAX_S / 60UL)) mins = (long)(CFG_BROADCAST_INTERVAL_MAX_S / 60UL);
        neighborInfoIntervalS = (uint32_t)mins * 60UL;
    } else if (server.hasArg("neighbor_info_intv")) {
        long sec = server.arg("neighbor_info_intv").toInt();
        if (sec < (long)NEIGHBORINFO_MIN_INTERVAL_S) sec = (long)NEIGHBORINFO_MIN_INTERVAL_S;
        neighborInfoIntervalS = (uint32_t)sec;
    }
    if (neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }
    if (neighborInfoIntervalS > CFG_BROADCAST_INTERVAL_MAX_S) {
        neighborInfoIntervalS = CFG_BROADCAST_INTERVAL_MAX_S;
    }
    if (server.hasArg("neighbor_info_en")) {
        gCfg->neighborInfoEnabled = server.arg("neighbor_info_en").toInt() != 0;
    }
    if (server.hasArg("mesh_beacon_listen")) {
        gCfg->meshBeaconListen = server.arg("mesh_beacon_listen").toInt() != 0;
    }
    if (server.hasArg("mesh_beacon_broadcast")) {
        gCfg->meshBeaconBroadcast = server.arg("mesh_beacon_broadcast").toInt() != 0;
    }
    if (server.hasArg("mesh_beacon_target")) {
        const long t = server.arg("mesh_beacon_target").toInt();
        // Out of range falls to LongFast in cfgCoerceMeshBeacon(); a preset this
        // radio cannot run is kept unchanged rather than substituted.
        if (t < 0 || t >= PRESET_COUNT) {
            gCfg->meshBeaconTargetPreset = PRESET_COUNT;
        } else if (presetUsableOnThisRadio((uint8_t)t)) {
            gCfg->meshBeaconTargetPreset = (uint8_t)t;
        }
    }
    if (server.hasArg("mesh_beacon_offer_psk")) {
        gCfg->meshBeaconOfferPsk = server.arg("mesh_beacon_offer_psk").toInt() != 0;
    }
    if (server.hasArg("mesh_beacon_interval")) {
        const long h = server.arg("mesh_beacon_interval").toInt();
        gCfg->meshBeaconIntervalH = h < 0 ? 0 : (h > 255 ? 255 : (uint8_t)h);
    }
    if (server.hasArg("mesh_beacon_message")) {
        const String m = server.arg("mesh_beacon_message");
        beaconCopyMessage(gCfg->meshBeaconMessage, m.c_str());
    }
    cfgCoerceMeshBeacon(*gCfg);
    cfgCoerceLicensed(*gCfg);
    // That may have moved the region (or the preset with it); the radio
    // fields were derived above from the pre-coercion values.
    applyPresetParams(*gCfg);
#if FEATURE_LICENSED_MODE
    if (licForm && licWas && !licEnter && !licExit) {
        // Staying licensed under the same call sign: the suffix and power are
        // simply stored. The long name stays CALL//suffix.
        gCfg->loraPower = licPwr;
        lic::composeLongName(gCfg->callSign, licSuffix, gCfg->nodeLong, MESH_LONG_NAME_MAX_BYTES + 1);
    }
    if (licForm && !licEnter && gCfg->licTxHold && server.hasArg("lic_resume")) {
        resumeTxAfterLicensedExit();
    }
#endif
    gCfg->neighborInfoIntervalS = neighborInfoIntervalS;
    if (server.hasArg("neighbor_info_lora")) {
        gCfg->neighborInfoOverLora = server.arg("neighbor_info_lora").toInt() != 0;
    }

    if (server.hasArg("ota_autocheck")) {
        gCfg->otaAutoCheckEnabled = server.arg("ota_autocheck").toInt() != 0;
    }
    if (server.hasArg("ota_autoupdate")) {
        // Clamped to the enum, and anything unrecognised is Off. The failure
        // mode of a malformed or injected value here must never be a device
        // that flashes itself hourly.
        const long submitted = server.arg("ota_autoupdate").toInt();
        gCfg->otaAutoUpdatePeriod =
            (submitted >= 0 && submitted <= OTA_AUTO_UPDATE_MAX)
                ? cfgCoerceOtaAutoUpdate((uint8_t)submitted)
                : OTA_AUTO_UPDATE_OFF;
        // Deliberately no otaRequestBootCheckOnce() here, unlike the channel
        // switch below: there is nothing for the user to be shown. The
        // scheduler picks the new period up on its next loop pass and runs its
        // first cycle a settle window later.
    }
    if (server.hasArg("ota_channel")) {
        // Anything that is not an explicit Alpha is Stable, so a malformed or
        // injected value cannot opt a device into prerelease builds. Submitting
        // the form always stores an explicit channel, never AUTO -- the user
        // has now chosen, so the running build must stop deciding for them.
        const uint8_t resolvedBefore = otaResolveChannel(cfgBefore.otaChannel);
        gCfg->otaChannel = (server.arg("ota_channel").toInt() == OTA_CHANNEL_ALPHA)
                               ? OTA_CHANNEL_ALPHA : OTA_CHANNEL_STABLE;
        otaSetChannel(gCfg->otaChannel);
        // Compared resolved, not stored: the form always writes an explicit
        // value, so AUTO on an alpha build submitting as Alpha is a change to
        // the stored setting but not to the channel updates come from. Saving
        // the page without touching the dropdown must not check for nothing.
        if (otaResolveChannel(gCfg->otaChannel) != resolvedBefore) {
            // Not checked here. Saving this form reboots (see the bottom of
            // this handler), and a synchronous release check run underneath the
            // live web server is a request nobody answers -- the same reason
            // the announce paths hold off while a browser is waiting. Left as a
            // one-shot for the next boot instead, where it runs with the server
            // down and reports on the device's own screen.
            otaRequestBootCheckOnce();
        }
    }
    if (server.hasArg("cs_mode")) {
        gCfg->chatServerMode = (uint8_t)constrain(server.arg("cs_mode").toInt(), 0, 2);
    }
    if (server.hasArg("cs_server")) {
        // "!aabbccdd", a short name (most recently heard match wins) or blank.
        // Applied through the main loop, which owns the client; unchanged leaves it.
        String v = server.arg("cs_server");
        v.trim();
        uint32_t id = 0;
        if (v.length()) {
            id = parseNodeIdText(v.c_str());
            if (id == 0) {
                long bestHeard = -1;
                for (int i = 0; i < Nodes.count(); i++) {
                    NodeEntry *n = Nodes.at(i);
                    if (n && n->nodeId && strcasecmp(n->shortName, v.c_str()) == 0 &&
                        (long)n->lastHeardMs > bestHeard) {
                        bestHeard = (long)n->lastHeardMs;
                        id = n->nodeId;
                    }
                }
            }
        }
        const bool unchanged = (id == gCfg->chatServerNodeId) && (id != 0 || v.length() == 0);
        if (!unchanged && (id != 0 || v.length() == 0)) webCfgQueueChatServerSet(id);
    }
#if HAS_AUDIO_ALERTS
    if (server.hasArg("msg_alert_sound")) {   // same guard, same reason
        gCfg->msgAlertSound  = (uint8_t)constrain(server.arg("msg_alert_sound").toInt(), 0, 3);
    }
#endif
#if HAS_HAPTIC
    if (server.hasArg("vibration")) {
        cfgSetHapticStyle(*gCfg, hapticStyleParse(server.arg("vibration").c_str()));
    }
#endif
    // WiFi credentials — save directly to NVS (not part of gCfg)
    {
#if !defined(PLATFORM_LINUX)
        // Not on the CardputerZero: its page has no SSID / Password fields, as
        // the OS owns the network, so a wifi_ssid in a post is ignored.
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
#endif

        if (server.hasArg("webcfg_idle_s")) {
            gCfg->webCfgIdleTimeoutS =
                cfgCoerceWebCfgIdle(server.arg("webcfg_idle_s").toInt());
            // The running session too, not just the next one: otherwise the act
            // of lengthening the timeout could still be cut short by the old
            // value before the new one ever applied.
            gIdleTimeoutMs = gCfg->webCfgIdleTimeoutS * 1000UL;
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

#if FEATURE_LICENSED_MODE
    if (licEnter || licExit) {
        // The rest of the form is already applied and saved by gOnSave above;
        // enterLicensedMode()/exitLicensedMode() save again and reboot, after
        // this redirect has reached the browser.
        gLicReq = licEnter ? 1 : 2;
        memcpy(gLicCall, licCall, sizeof(gLicCall));
        memcpy(gLicSuffix, licSuffix, sizeof(gLicSuffix));
        gLicPwr = licPwr;
        gLicAtMs = millis() + 900;
        redirectHomeWithFlash(licEnter ? "Saved. Entering licensed mode and rebooting..."
                                       : "Saved. Leaving licensed mode and rebooting...");
        return;
    }
#endif

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
    //
    // Spell check is masked out the same way: the compose box reads it on
    // every word typed, so it takes effect without a restart too.
    RhinoConfig cfgMasked = *gCfg;
    cfgMasked.fontSize = cfgBefore.fontSize;
    cfgMasked.spellCheckEnabled = cfgBefore.spellCheckEnabled;
    // Mirrored into wardrive_log by the main loop every pass, so toggling it
    // needs no reboot -- and a reboot would also wipe the session counters.
    cfgMasked.wardriveLogEnabled = cfgBefore.wardriveLogEnabled;
    // Nav alignment: onWebCfgSaved() rebuilds the screen around it.
    cfgMasked.navAlign = cfgBefore.navAlign;
    // Read live by the keyboard light service every loop pass.
    cfgMasked.kbLightTimeoutS = cfgBefore.kbLightTimeoutS;
    // Read on every received packet.
    cfgMasked.packetSigPolicy = cfgBefore.packetSigPolicy;
    // Read by the Tanmatsu status-light poll once a second.
    cfgMasked.powerLedPct = cfgBefore.powerLedPct;
    // Channel panel: onWebCfgSaved() rebuilds the screen around it.
    cfgMasked.channelPanelLock = cfgBefore.channelPanelLock;
    const bool fontChanged  = (gCfg->fontSize != cfgBefore.fontSize);
    const bool spellChanged = (gCfg->spellCheckEnabled != cfgBefore.spellCheckEnabled);
    const bool wardriveChanged = (gCfg->wardriveLogEnabled != cfgBefore.wardriveLogEnabled);
    const bool navAlignChanged = (gCfg->navAlign != cfgBefore.navAlign);
    const bool kbLightTimeoutChanged = (gCfg->kbLightTimeoutS != cfgBefore.kbLightTimeoutS);
    const bool sigPolicyChanged = (gCfg->packetSigPolicy != cfgBefore.packetSigPolicy);
    const bool powerLedChanged = (gCfg->powerLedPct != cfgBefore.powerLedPct);
    const bool channelPanelChanged = (gCfg->channelPanelLock != cfgBefore.channelPanelLock);
    const bool liveOnly = (memcmp(&cfgMasked, &cfgBefore, sizeof(RhinoConfig)) == 0)
                          && (fontChanged || spellChanged || wardriveChanged
                              || navAlignChanged || kbLightTimeoutChanged
                              || sigPolicyChanged || powerLedChanged
                              || channelPanelChanged);

    if (sigPolicyChanged) meshSetSignPolicy(gCfg->packetSigPolicy);
    if (liveOnly) {
        redirectHomeWithFlash(fontChanged ? "Saved. Font size applied." : "Saved.");
        return;
    }

    scheduleReboot(900);
    redirectHomeWithFlash("Saved. Rebooting now...");
}

