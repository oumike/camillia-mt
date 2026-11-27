// SPDX-License-Identifier: GPL-3.0-or-later
// Part of web_config.cpp: API: debug monitor, theme, chat targets, charts, announce.
// Included in order from web_config.cpp as one translation unit; not built on its own.

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

    // Never the PIN: it is only ever shown on the device.
    char response[224];
    const String ip = WiFi.localIP().toString();
    snprintf(response, sizeof(response),
             "{\"enabled\":%s,\"running\":%s,\"client\":%s,"
             "\"pending\":%s,\"ip\":\"%s\",\"port\":%u,\"session\":%lu}",
             vncHostEnabled() ? "true" : "false",
             vncHostRunning() ? "true" : "false",
             vncHostClientConnected() ? "true" : "false",
             gVncToggleReq ? "true" : "false",
             ip.c_str(), (unsigned)vncHostPort(),
             (unsigned long)vncHostSession());
    server.send(200, "application/json", response);
}

// Trades the PIN shown on the device for a VNC session token. Runs on the main
// loop like every handler, which is also where the PIN is redrawn.
static void handlePostVncUnlock() {
    if (!isLoggedIn()) {
        server.send(403, "application/json", "{\"ok\":false,\"error\":\"Not logged in.\"}");
        return;
    }
    if (!vncHostEnabled()) {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"The VNC host is off.\"}");
        return;
    }
    if (vncHostUnlockRetryMs() > 0) {
        server.send(429, "application/json",
                    "{\"ok\":false,\"error\":\"Too many attempts. Wait a moment and try again.\"}");
        return;
    }
    char token[65];
    const String pin = server.arg("pin");
    if (!vncHostRedeemPin(pin.c_str(), token, sizeof(token))) {
        server.send(403, "application/json",
                    "{\"ok\":false,\"error\":\"Wrong PIN. Check the PIN shown on the device.\"}");
        return;
    }
    char response[128];
    snprintf(response, sizeof(response), "{\"ok\":true,\"token\":\"%s\",\"session\":%lu}",
             token, (unsigned long)vncHostSession());
    memset(token, 0, sizeof(token));
    server.sendHeader("Cache-Control", "no-store");
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
        uint32_t replyId = 0;
        bool reaction = false;
        if (isDm) {
            const DmLine &dl = dmLines[i % cap];
            t = dl.text; pid = dl.packetId; ack = (uint8_t)dl.ack;
        } else {
            const DisplayLine &dl = chLines[i % cap];
            t = dl.text; pid = dl.packetId; ack = (uint8_t)dl.ack;
            replyId = dl.replyId; reaction = dl.reaction;
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
        // Channel replies: the packet id answered, and whether it is a reaction.
        if (replyId) {
            out += ",\"rp\":";
            out += String(replyId);
            if (reaction) out += ",\"rx\":1";
        }
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

static void handlePostChatServerCheck() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    if (lic::active()) {
        redirectHomeWithFlash("Chat server is off while licensed.");
        return;
    }
    webCfgQueueChatServerCheck();
    redirectHomeWithFlash("Chat server check queued.");
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

// ── Reboot ────────────────────────────────────────────────────
//
// Nothing is erased and nothing is written: this is the "turn it off and on
// again" button for a device that is across the room or on a mast. It goes
// through scheduleReboot() like every other restarting action here, so the
// redirect is actually delivered before the radio drops -- calling
// ESP.restart() inline would kill the connection mid-response and leave the
// browser showing a network error instead of a confirmation.
//
// The 900 ms matches the destructive handlers. Persistence is flushed by the
// reboot path itself, so a queued transcript snapshot is not lost.
// Applies a community preset: validated here so a refusal can be shown, then
// queued for the main loop (webCfgTakeCommunityPreset()), which runs it
// through the cfg CLI and reboots once this redirect has gone out.
static void handlePostCommunityPreset() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    const String id = server.arg("id");
    int idx = -1;
    for (int i = 0; i < kCommunityPresetCount; i++) {
        if (id == kCommunityPresets[i].id) { idx = i; break; }
    }
    if (idx < 0) { redirectHomeWithFlash("Unknown community preset."); return; }
    if (const char *why = communityPresetProblem(idx)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s was not applied: %s.", kCommunityPresets[idx].label, why);
        redirectHomeWithFlash(msg);
        return;
    }
    Serial.printf("[web] community preset %s requested\n", kCommunityPresets[idx].id);
    gCommunityReq = idx;
    gCommunityAtMs = millis() + 900;
    char msg[160];
    if (lic::active()) {
        // Shorter: the flash message is 128 bytes and the note must fit.
        snprintf(msg, sizeof(msg), "Licensed mode: channel imported without its key. "
                 "Applying %s; the device reboots.", kCommunityPresets[idx].label);
    } else {
        snprintf(msg, sizeof(msg), "Applying %s. The device reboots and this page will "
                 "not respond until it is back.", kCommunityPresets[idx].label);
    }
    redirectHomeWithFlash(msg);
}

static void handlePostReboot() {
    if (!isLoggedIn()) { redirect("/login"); return; }
    Serial.println("[web] reboot requested from Danger Zone");
    scheduleReboot(900);
    redirectHomeWithFlash("Rebooting now. This page will not respond until the "
                          "device is back.");
}

