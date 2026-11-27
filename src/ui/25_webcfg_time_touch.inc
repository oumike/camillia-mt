// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: web config, screenshots, NTP time, touch input, boot splash.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// What the chat screen's New Message button does. Split out of the event handler
// so the USER button can do the same thing: on this build that button is the
// Enter key's stand-in, and Enter on the chat screen composes.
static void chatComposeFromButton() {
#if UI_TOUCH_ONLY_PROFILE
    if (s_legendModal) return;
#endif
    if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
        openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
    } else {
        openComposePrompt(0, nullptr);
    }
}

static void onChatNewMessagePressed(lv_event_t *e) {
    LV_UNUSED(e);
    chatComposeFromButton();
}

static void refreshChatComposeButtonState() {
#if UI_TOUCH_ONLY_PROFILE
    if (!s_chatNewMsgLabel) return;

    if (s_chatNewMsgBtn) {
        if (s_legendModal) lv_obj_add_state(s_chatNewMsgBtn, LV_STATE_DISABLED);
        else               lv_obj_clear_state(s_chatNewMsgBtn, LV_STATE_DISABLED);
    }

    if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
        lv_label_set_text(s_chatNewMsgLabel, TR("Reply"));
    } else {
        lv_label_set_text(s_chatNewMsgLabel, TR("New Message"));
    }
#endif
}

#if HAS_TOUCH
// Tap-and-hold on a chat message opens the node actions for whoever wrote it —
// the touch equivalent of the second Enter on the chat cursor, and the reason
// openNodesActionMenuFor() takes a node id rather than a list selection. LVGL
// raises this at the input device's long-press threshold while the finger is
// still down, so the menu appears under the finger rather than on release.
//
// Timestamp rather than the pressed object: LVGL still delivers CLICKED on
// release after a long press, and that click would otherwise also select the
// message as a reply target on the way out. Recording *when* the gesture was
// consumed lets the next click be dropped without holding an object pointer
// that a chat rebuild could invalidate underneath us. It self-heals — a long
// press with no click after it (finger dragged off the message) leaves at most
// this window behind, not a permanently swallowed tap.
static uint32_t s_chatLongPressConsumedMs = 0;
static constexpr uint32_t kChatLongPressConsumeMs = 1500;

// Resolve a message's sender and text from its packet id, by scanning the
// channel's display lines. LVGL gives one user_data per registration and the
// long press needs both halves — the packet id is the one that cannot be
// recovered any other way, so that is what gets carried and this looks up the
// rest. Returns false when the line is gone (the backlog scrolled past it).
static bool chatLineForPacketId(uint32_t packetId, uint32_t *senderOut,
                                const char **textOut) {
    if (packetId == 0) return false;
    for (int row = 0; row < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        if (dl->packetId != packetId) continue;
        if (senderOut) *senderOut = dl->senderNodeId;
        if (textOut) *textOut = dl->text;
        return true;
    }
    return false;
}

static void onChatMessageLongPressed(lv_event_t *e) {
    const uint32_t packetId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    uint32_t senderNodeId = 0;
    const char *text = nullptr;
    if (!chatLineForPacketId(packetId, &senderNodeId, &text)) return;

    // Same rule the Enter path uses: nothing to act on for our own messages or
    // for local/system lines, which carry no sender.
    if (senderNodeId == 0 || senderNodeId == s_myNodeId) return;

    // Select the held message before opening. Without this the Reply row, the
    // tapbacks and the full-tray escape would all act on whatever was selected
    // earlier — or on nothing, since a long press never went through the tap
    // handler that normally sets the selection.
    setSelectedReplyContext(packetId, text, senderNodeId);

    s_chatLongPressConsumedMs = millis();
    openMessageActionMenu(packetId, senderNodeId);
}
#endif  // HAS_TOUCH

static void onChatMessagePressed(lv_event_t *e) {
    lv_obj_t *label = lv_event_get_target_obj(e);
    if (!label) return;

#if HAS_TOUCH
    // Drop the release-click that follows a long press we already acted on.
    if (s_chatLongPressConsumedMs != 0
        && (uint32_t)(millis() - s_chatLongPressConsumedMs) < kChatLongPressConsumeMs) {
        s_chatLongPressConsumedMs = 0;
        return;
    }
#endif

    uint32_t replyPacketId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    // Only the classic view's rows are labels; a bubble or an IRC row is a
    // plain container, and lv_label_get_text() on one reads garbage. Those
    // carry a packet id, which setSelectedReplyContext() resolves on its own.
    const char *txt = lv_obj_check_type(label, &lv_label_class)
                          ? lv_label_get_text(label) : nullptr;
    // Sender 0: a tap selects a message for *reply*, and this handler has the
    // label and packet id but no node id. Claiming a sender here would be
    // guessing, and carrying a stale one forward would point the Enter action
    // menu at whoever was highlighted before the tap.
    setSelectedReplyContext(replyPacketId, txt ? txt : "", 0);

    refreshChatComposeButtonState();

    // Defer redraw to the normal loop to avoid deleting the active target in-event.
    s_lastRenderedChannel = -1;
}

static void onWebCfgSaved() {
#if FEATURE_DISCOVERY
    // This save ends in a Radio.reconfigure() from s_cfg, which would retune out
    // from under a preset scan and leave it to "restore" to settings it is
    // already on. The scan is the thing that has to give way: it is temporary by
    // definition and a config save is not.
    discoveryEndPresetScan(/*aborted=*/true);
#endif
    uint8_t prevTheme = s_appliedUiTheme;
    uint8_t prevMode = s_appliedUiMode;
    const uint8_t prevFontSize = s_appliedFontSize;
#if HAS_NAV_BAR_TOGGLE
    const bool prevNavBar = s_appliedNavBar;
#endif
#if HAS_NAV_ALIGN_SETTING
    const NavAlign prevNavAlign = s_appliedNavAlign;
#endif
#if HAS_CHANNEL_PANEL_LOCK
    const bool prevChannelPanelLocked = s_appliedChannelPanelLocked;
#endif

#if !HAS_ENV_SENSOR_TELEMETRY
    s_cfg.telEnvEnabled = false;
#endif
    if (s_cfg.telDeviceIntervalS < 3600UL) s_cfg.telDeviceIntervalS = 3600UL;
    if (s_cfg.telEnvIntervalS < 3600UL) s_cfg.telEnvIntervalS = 3600UL;
    if (s_cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        s_cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }

    persistConfigToPrefs();
    meshBeaconSettingsChanged();
    persistImportedIdentityKeys("web/config-screen");
    syncPrimaryChannelName();
    // Same reason as at boot: the automatic custom slot hashes channel 0's
    // name, which this save may have just changed, and the Radio.reconfigure()
    // below is about to tune to whatever loraFreq holds.
    applyPresetParams(s_cfg);
    persistChannelsToPrefs();
    myDeviceRole = s_cfg.deviceRole;
    applyUiThemePalette();
    s_cfg.brightness = cfgCoerceBrightness(s_cfg.brightness);
    if (!s_screenAsleep) applyBrightness();   // don't light a sleeping panel
#if defined(DEVICE_TANMATSU)
    // The form wrote the level straight to the keyboard; this puts it back
    // behind the screen-sleep gate.
    applyKbBacklightSetting();
#endif

    recomputeChannelHashes();
    deriveNodeId();
    applyTimezoneFromConfig();
    gpsSetEnabled(s_cfg.gpsEnabled);
    syncWifiCredsToPrefs();

    bool debugMonitor = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;
    s_cfg.debugAcks = debugMonitor;
    s_cfg.debugMessages = debugMonitor;
    s_cfg.debugGps = debugMonitor;
    s_cfgDebugLog = debugMonitor;
    debugSetFlags(debugMonitor, debugMonitor, debugMonitor);

    s_ntpConfigured = false;
    s_ntpServerActive[0] = '\0';
    s_ntpLastConfigureMs = 0;

    // Not part of Radio.reconfigure(): the hop budget is a header field this
    // firmware stamps, not a modem setting, so it applies whether or not the
    // radio came up.
    meshSetHopLimit(s_cfg.loraHopLimit);
    if (s_radioReady) {
        Radio.reconfigure(s_cfg.loraFreq, s_cfg.loraBw,
                          s_cfg.loraSf, s_cfg.loraCr, s_cfg.loraPower);
    }

    // Keep export an explicit user action. Auto-export after onboarding/web-save
    // can overwrite or churn SD config unexpectedly.

    // The on-device picker reboots for a theme change; this callback cannot,
    // because it also runs for the custom-theme editor's own save and delete,
    // which are AJAX calls inside a page the user is still working in. The
    // full rebuild is what covers those — it is the path the nav-bar toggle
    // uses, and it draws every screen from the new palette rather than trying
    // to repaint the live one widget by widget. A theme picked in the main
    // settings form reboots anyway (handlePostSave), so there this is just the
    // screen keeping up during the second before the restart.
    if ((prevTheme != s_appliedUiTheme || prevMode != s_appliedUiMode) && s_rootScreen) {
        scheduleUiRebuild(s_cfgModal != nullptr);
    }

#if HAS_NAV_BAR_TOGGLE
    // Turned on or off from the browser: same rebuild the on-device row asks
    // for, so the screen matches the setting without waiting for a reboot.
    if (prevNavBar != s_cfg.navBarEnabled && s_rootScreen) {
        scheduleUiRebuild(s_cfgModal != nullptr);
    }
#endif
#if HAS_KB_LIGHT_TIMEOUT
    // The form may have written the keyboard level straight to the hardware, or
    // changed the timeout; either way put the light where it should rest now.
    kbLightApplyResting();
#endif
#if HAS_NAV_ALIGN_SETTING
    // The nav bar moved: every screen is laid out around it.
    if (prevNavAlign != cfgNavAlign(s_cfg) && s_rootScreen) {
        scheduleUiRebuild(s_cfgModal != nullptr);
    }
#endif
#if HAS_CHANNEL_PANEL_LOCK
    // Docked column or drawer: the chat is laid out around it.
    if (prevChannelPanelLocked != cfgChannelPanelLocked(s_cfg) && s_rootScreen) {
        scheduleUiRebuild(s_cfgModal != nullptr);
    }
#endif

    // Font size is not part of the theme, so a UI rebuild does not cover it,
    // and web config no longer reboots for it — which leaves this as the only
    // thing that makes the change visible. Same steps the on-device picker
    // takes: clear the render cache so the refresh is not skipped as a no-op,
    // then rebuild every view that draws with this face.
    s_cfg.fontSize = (uint8_t)constrain((int)s_cfg.fontSize, 0, FONT_SIZE_MAX);
    if (prevFontSize != s_cfg.fontSize && s_rootScreen) {
        s_lastRenderedChannel = -1;
        s_lastRenderedCount = -1;
        refreshChatView(true);
        refreshDmModal(true);
        refreshLiveView(true);
    }
    s_appliedFontSize = s_cfg.fontSize;
}

static uint32_t pngCrc32Update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
        }
    }
    return crc;
}

static uint32_t pngAdler32Update(uint32_t adler, const uint8_t *data, size_t len) {
    if (!data) return adler;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = (adler >> 16) & 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        a += data[i];
        if (a >= 65521U) a -= 65521U;
        b += a;
        if (b >= 65521U) b %= 65521U;
    }
    return (b << 16) | a;
}

static bool fileWriteAll(File &f, const uint8_t *data, size_t len) {
    if (!data && len) return false;
    while (len > 0) {
        size_t n = f.write(data, len);
        if (n == 0) return false;
        data += n;
        len -= n;
    }
    return true;
}

static bool fileWriteBe32(File &f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)((v >> 24) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)(v & 0xFF),
    };
    return fileWriteAll(f, b, sizeof(b));
}

static bool pngWriteChunk(File &f, const char type[4], const uint8_t *data, uint32_t len) {
    if (!type) return false;
    if (!fileWriteBe32(f, len)) return false;
    if (!fileWriteAll(f, (const uint8_t *)type, 4)) return false;
    if (len > 0 && !fileWriteAll(f, data, len)) return false;

    uint32_t crc = 0xFFFFFFFFUL;
    crc = pngCrc32Update(crc, (const uint8_t *)type, 4);
    if (len > 0) crc = pngCrc32Update(crc, data, len);
    crc ^= 0xFFFFFFFFUL;
    return fileWriteBe32(f, crc);
}

static bool captureWebScreenshotPng(const char *outPath) {
#if !HAS_FILE_STORAGE
    (void)outPath;
    return false;
#else
    if (!outPath || !outPath[0]) return false;

    const int32_t w = displayDev().width();
    const int32_t h = displayDev().height();
    if (w <= 0 || h <= 0) return false;

    const size_t rowPixels = (size_t)w;
    const size_t scanlineLen = 1 + rowPixels * 3;

#if defined(DEVICE_TDECK)
    const size_t frame565Bytes = rowPixels * (size_t)h * sizeof(uint16_t);
    uint16_t *frame565 = (uint16_t *)malloc(frame565Bytes);
    if (!frame565) return false;
    memset(frame565, 0, frame565Bytes);

    s_screenshotCaptureFrame = frame565;
    s_screenshotCaptureW = w;
    s_screenshotCaptureH = h;
    s_screenshotCaptureTouched = false;
    s_screenshotCaptureActive = true;

    lv_obj_invalidate(lv_scr_act());
    for (int i = 0; i < 12 && !s_screenshotCaptureTouched; i++) {
        lv_timer_handler();
        delay(12);
    }

    s_screenshotCaptureActive = false;
    s_screenshotCaptureFrame = nullptr;
    s_screenshotCaptureW = 0;
    s_screenshotCaptureH = 0;

    if (!s_screenshotCaptureTouched) {
        free(frame565);
        return false;
    }
#endif
    uint8_t *scanline = (uint8_t *)malloc(scanlineLen);
#if defined(DEVICE_TDECK)
    if (!scanline) {
        free(frame565);
        free(scanline);
        return false;
    }
#else
    if (!scanline) {
        free(scanline);
        return false;
    }
#endif

    if (storageFs().exists(outPath)) storageFs().remove(outPath);
    File f = storageFs().open(outPath, FILE_WRITE);
    if (!f) {
#if defined(DEVICE_TDECK)
        free(frame565);
#endif
        free(scanline);
        return false;
    }

    bool resumeRx = Radio.isReady();
    if (resumeRx) Radio.setRxPaused(true);

    bool ok = true;
    do {
        static const uint8_t kPngSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (!fileWriteAll(f, kPngSig, sizeof(kPngSig))) { ok = false; break; }

        uint8_t ihdr[13];
        ihdr[0] = (uint8_t)(((uint32_t)w >> 24) & 0xFF);
        ihdr[1] = (uint8_t)(((uint32_t)w >> 16) & 0xFF);
        ihdr[2] = (uint8_t)(((uint32_t)w >> 8) & 0xFF);
        ihdr[3] = (uint8_t)((uint32_t)w & 0xFF);
        ihdr[4] = (uint8_t)(((uint32_t)h >> 24) & 0xFF);
        ihdr[5] = (uint8_t)(((uint32_t)h >> 16) & 0xFF);
        ihdr[6] = (uint8_t)(((uint32_t)h >> 8) & 0xFF);
        ihdr[7] = (uint8_t)((uint32_t)h & 0xFF);
        ihdr[8] = 8;   // bit depth
        ihdr[9] = 2;   // truecolor RGB
        ihdr[10] = 0;  // compression method
        ihdr[11] = 0;  // filter method
        ihdr[12] = 0;  // no interlace
        if (!pngWriteChunk(f, "IHDR", ihdr, sizeof(ihdr))) { ok = false; break; }

        // zlib stream with deflate "stored" blocks, one block per scanline.
        uint64_t idatLen64 = 2ULL + (uint64_t)h * (5ULL + (uint64_t)scanlineLen) + 4ULL;
        if (idatLen64 > 0xFFFFFFFFULL) { ok = false; break; }
        uint32_t idatLen = (uint32_t)idatLen64;

        if (!fileWriteBe32(f, idatLen)) { ok = false; break; }
        if (!fileWriteAll(f, (const uint8_t *)"IDAT", 4)) { ok = false; break; }

        uint32_t idatCrc = 0xFFFFFFFFUL;
        idatCrc = pngCrc32Update(idatCrc, (const uint8_t *)"IDAT", 4);

        const uint8_t zlibHdr[2] = {0x78, 0x01};
        if (!fileWriteAll(f, zlibHdr, sizeof(zlibHdr))) { ok = false; break; }
        idatCrc = pngCrc32Update(idatCrc, zlibHdr, sizeof(zlibHdr));

        uint32_t adler = 1;
        for (int32_t y = 0; y < h; y++) {
            scanline[0] = 0; // filter: None

#if defined(DEVICE_TDECK)
            const uint16_t *srcRow = frame565 + ((size_t)y * rowPixels);
            size_t p = 1;
            for (int32_t x = 0; x < w; x++) {
                // Native-order RGB565, exactly as captured from the LVGL flush
                // buffer. v9 has no LV_COLOR_16_SWAP; byte order is the display
                // driver's concern and LovyanGFX takes the native layout.
                uint16_t c = srcRow[x];

                uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
                uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
                uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
                scanline[p++] = r;
                scanline[p++] = g;
                scanline[p++] = b;
            }
#else
            displayDev().waitDMA();
            displayDev().startWrite();
            displayDev().readRectRGB(0, y, w, 1, scanline + 1);
            displayDev().endWrite();
#endif

            adler = pngAdler32Update(adler, scanline, scanlineLen);

            uint16_t blockLen = (uint16_t)scanlineLen;
            uint16_t nlen = (uint16_t)~blockLen;
            uint8_t blockHdr[5] = {
                (uint8_t)((y == (h - 1)) ? 1 : 0),
                (uint8_t)(blockLen & 0xFF),
                (uint8_t)((blockLen >> 8) & 0xFF),
                (uint8_t)(nlen & 0xFF),
                (uint8_t)((nlen >> 8) & 0xFF),
            };

            if (!fileWriteAll(f, blockHdr, sizeof(blockHdr))) { ok = false; break; }
            idatCrc = pngCrc32Update(idatCrc, blockHdr, sizeof(blockHdr));

            if (!fileWriteAll(f, scanline, scanlineLen)) { ok = false; break; }
            idatCrc = pngCrc32Update(idatCrc, scanline, scanlineLen);
        }
        if (!ok) break;

        uint8_t adlerBe[4] = {
            (uint8_t)((adler >> 24) & 0xFF),
            (uint8_t)((adler >> 16) & 0xFF),
            (uint8_t)((adler >> 8) & 0xFF),
            (uint8_t)(adler & 0xFF),
        };
        if (!fileWriteAll(f, adlerBe, sizeof(adlerBe))) { ok = false; break; }
        idatCrc = pngCrc32Update(idatCrc, adlerBe, sizeof(adlerBe));

        idatCrc ^= 0xFFFFFFFFUL;
        if (!fileWriteBe32(f, idatCrc)) { ok = false; break; }

        if (!pngWriteChunk(f, "IEND", nullptr, 0)) { ok = false; break; }
    } while (false);

    if (resumeRx) Radio.setRxPaused(false);

    f.close();
    if (!ok && storageFs().exists(outPath)) {
        storageFs().remove(outPath);
    }

#if defined(DEVICE_TDECK)
    free(frame565);
#endif
    free(scanline);
    return ok;
#endif
}

static void startWebConfigAuto() {
    if (!s_webCfgEnabled) {
        Serial.println("[web] auto start disabled");
        return;
    }
    if (webCfgRunning()) return;
    webCfgSetForceAp(wifiForceApMode());
    webCfgPushActiveCreds();
#if HAS_FILE_STORAGE && !defined(DEVICE_TDECK_PRO)
    bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, captureWebScreenshotPng);
#else
    bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
#endif
    s_webCfgAwaitStation = !ok && webCfgStartDeferred();
    if (s_webCfgAwaitStation) {
        Serial.println("[web] auto start waits for WiFi (MQTT holds the station)");
        return;
    }
    if (!ok) {
        Serial.println("[web] auto start failed");
        s_webCfgEnabled = false;
        persistWebCfgEnabled();
        return;
    }

    if (webCfgIsLite()) {
        Serial.printf("[web] web config lite (AP): %s\n", webCfgIP());
    } else {
        Serial.printf("[web] web config: %s\n", webCfgIP());
    }

    // Auto-start means the user never chose this, so warn here too. Boards that
    // don't reclaim chat memory report false and see nothing. (On boards where
    // this runs before buildUi(), the modal call is a no-op — but those are the
    // PSRAM boards, which never pause chat.)
    if (webCfgChatPaused()) {
        Serial.println("[web] chat buffers released - messages are being dropped");
        // Not while onboarding is up. Onboarding owns the keyboard until it
        // finishes, so a popup raised over it can never be dismissed — and its
        // advice ("turn web config off") is not something the user can act on
        // mid-setup anyway. Defer it to whenever onboarding closes.
        if (s_onboardingModal) {
            s_webCfgChatPausedNoticePending = true;
        } else {
            showWebCfgChatPausedNotice();
        }
    }
}

static void showWebCfgChatPausedNotice() {
    char warn[320];
    snprintf(warn, sizeof(warn),
             TR("Web config: %s\n\n"
             "Chat is PAUSED while web config runs - this device needs that "
             "memory for Wi-Fi. Messages sent to you now are not received "
             "or stored.\n\n"
             "Turn web config off to resume messaging."),
             webCfgIP());
    openCfgActionMessageModal(warn);
}

static bool shouldHideChatLine(const char *text) {
    if (!text) return false;
    return strcmp(text, "[radio] listening for mesh traffic") == 0
        || strcmp(text, "[radio] listening to mesh traffic") == 0;
}

static bool isDuplicate(uint32_t from, uint32_t id) {
    for (int i = 0; i < kRxDedupSize; i++) {
        if (s_seenPkts[i].from == from && s_seenPkts[i].id == id) return true;
    }
    s_seenPkts[s_seenHead] = {from, id};
    s_seenHead = (s_seenHead + 1) % kRxDedupSize;
    return false;
}

static void applyTimezoneFromConfig() {
#if HAS_SYSTEM_SETTINGS
    // The OS's zone (/etc/localtime), as every other app on the device shows.
    unsetenv("TZ");
#else
    const char *tz = (s_cfg.tzDef[0]) ? s_cfg.tzDef : "UTC0";
    setenv("TZ", tz, 1);
#endif
    tzset();
}

static inline bool clockIsSet() {
    return time(nullptr) >= kClockSetEpoch;
}

// The user owns the clock: NTP and GPS must both leave it alone.
static inline bool timeSourceIsManual() {
    return s_cfg.timeSource == TIME_SOURCE_MANUAL;
}

// Whether anything here may set the clock: not when the user has, and never
// where the OS keeps it (HAS_SYSTEM_SETTINGS), which it does with its own NTP.
static inline bool clockIsOursToSet() {
    return !HAS_SYSTEM_SETTINGS && !timeSourceIsManual();
}

// Sets the system clock from a local wall-clock reading (24-hour). The fields go
// through mktime, so the configured timezone and its DST rules are what turn the
// time the user typed into the epoch — entering 14:30 means 14:30 where they are.
static bool applyManualClock(int year, int mon, int day, int hour, int minute) {
    if (year < 2020 || year > 2099) return false;
    if (mon < 1 || mon > 12 || day < 1 || day > 31) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    if (HAS_SYSTEM_SETTINGS) return false;

    applyTimezoneFromConfig();

    struct tm tmv = {};
    tmv.tm_year  = year - 1900;
    tmv.tm_mon   = mon - 1;
    tmv.tm_mday  = day;
    tmv.tm_hour  = hour;
    tmv.tm_min   = minute;
    tmv.tm_sec   = 0;
    tmv.tm_isdst = -1;          // let the zone rules decide, don't guess

    time_t epoch = mktime(&tmv);
    if (epoch <= 0) return false;

    struct timeval tv = {};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) return false;

    Serial.printf("[time] manual clock set: %04d-%02d-%02d %02d:%02d (epoch %lld)\n",
                  year, mon, day, hour, minute, (long long)epoch);
    return true;
}

// UTC calendar fields -> epoch seconds, without depending on timegm(). GPS
// reports UTC, so this deliberately skips the timezone that applyManualClock()
// leans on. (Howard Hinnant's days_from_civil.)
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

// GPS is the time source when there is no network path. Only in automatic mode,
// and only when the clock is unset or an hour stale, so a fix doesn't fight NTP
// or overwrite what the user typed.
static void serviceGpsTimeSync(uint32_t now) {
    if (!clockIsOursToSet()) return;
    if (!gpsIsEnabled() || !gpsHasFix()) return;
    if (clockIsSet() && s_gpsClockSyncMs != 0 && (now - s_gpsClockSyncMs) < 3600000UL) return;

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (!gpsUtcDateTime(y, mo, d, h, mi, s)) return;
    if (y < 2020 || y > 2099) return;

    const int64_t epoch = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400LL
                        + (int64_t)h * 3600 + (int64_t)mi * 60 + s;
    if (epoch < (int64_t)kClockSetEpoch) return;

    struct timeval tv = {};
    tv.tv_sec = (time_t)epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) return;

    applyTimezoneFromConfig();
    s_gpsClockSyncMs = now ? now : 1;
    Serial.printf("[time] GPS clock sync: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  y, mo, d, h, mi, s);
}

static bool wifiHasInternetTimePath() {
    if (!clockIsOursToSet()) return false;   // nothing may overwrite a set clock
    if (WiFi.status() != WL_CONNECTED) return false;
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_AP;
}

static const char *configuredNtpServer() {
    return s_cfg.ntpServer[0] ? s_cfg.ntpServer : MY_NTP_SERVER;
}

static void ensureNtpConfigured() {
    if (!wifiHasInternetTimePath()) return;

    const char *srv = configuredNtpServer();
    if (s_ntpConfigured && strcmp(s_ntpServerActive, srv) == 0
        && (millis() - s_ntpLastConfigureMs) < 21600000UL) {
        return;
    }

    configTime(0, 0, srv);
    // configTime can leave TZ handling in UTC; re-apply configured timezone.
    applyTimezoneFromConfig();
    strncpy(s_ntpServerActive, srv, sizeof(s_ntpServerActive) - 1);
    s_ntpServerActive[sizeof(s_ntpServerActive) - 1] = '\0';
    s_ntpConfigured = true;
    s_ntpLastConfigureMs = millis();
    Serial.printf("[time] NTP configured: %s\n", s_ntpServerActive);
}

static bool ntpSyncSystemClock() {
    if (!wifiHasInternetTimePath()) return false;
    ensureNtpConfigured();
    return time(nullptr) >= kClockSetEpoch;
}

// Automatic mode has to be able to re-arm itself. NTP is otherwise only tried at
// boot, so a device switched back from Manual — or one that had no network when
// it started — would sit on a stale clock until the next restart.
// ensureNtpConfigured() self-throttles, so calling it on a slow tick is cheap.
static void serviceAutoTimeSync(uint32_t now) {
    if (timeSourceIsManual()) return;
    if (!wifiHasInternetTimePath()) return;

    static uint32_t sLastTryMs = 0;
    if (sLastTryMs != 0 && (now - sLastTryMs) < 60000UL) return;
    sLastTryMs = now ? now : 1;
    ensureNtpConfigured();
}

static bool waitForNtpSync(uint32_t timeoutMs, bool pumpWebCfg) {
    uint32_t syncStartMs = millis();
    while ((millis() - syncStartMs) < timeoutMs) {
        if (pumpWebCfg) webCfgLoop();
        if (ntpSyncSystemClock()) return true;
        delay(200);
    }
    return false;
}

static bool bootTimeNtpSyncDirectSta(const char *ssidOverride, const char *passOverride) {
    const char *ssid = (ssidOverride && ssidOverride[0]) ? ssidOverride : s_cfg.wifiSsid;
    const char *pass = (ssidOverride && ssidOverride[0]) ? passOverride : s_cfg.wifiPass;
    if (!ssid || !ssid[0]) return false;

    wifi_mode_t prevMode = WiFi.getMode();
    bool wasConnected = (WiFi.status() == WL_CONNECTED);

    if (!wasConnected) {
        if (prevMode == WIFI_AP) {
#ifdef WIFI_AP_STA
            WiFi.mode(WIFI_AP_STA);
#else
            WiFi.mode(WIFI_STA);
#endif
        } else {
            WiFi.mode(WIFI_STA);
        }

        WiFi.begin(ssid, pass);
        uint32_t connectStartMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - connectStartMs) < 10000UL) {
            bootSplashTick();   // longest single step at boot; keep the dots moving
            delay(100);
        }
    }

    bool synced = waitForNtpSync(10000UL, false);

    if (!wasConnected) {
        switch (prevMode) {
            case WIFI_OFF:
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                break;
            case WIFI_AP:
                WiFi.disconnect(false);
                WiFi.mode(WIFI_AP);
                break;
            case WIFI_STA:
                WiFi.mode(WIFI_STA);
                WiFi.disconnect(false);
                break;
#ifdef WIFI_AP_STA
            case WIFI_AP_STA:
                WiFi.mode(WIFI_AP_STA);
                WiFi.disconnect(false);
                break;
#endif
            default:
                break;
        }
    }

    return synced;
}

static void bootTimeNtpSync() {
    // Manual clock: don't spend ten seconds bringing WiFi up at boot for a sync
    // that would be refused anyway.
    if (!clockIsOursToSet()) {
        applyTimezoneFromConfig();
        Serial.println(HAS_SYSTEM_SETTINGS ? "[time] Using the system clock"
                                           : "[time] Boot time sync skipped (clock set manually)");
        return;
    }

    bool bootTimeSynced = false;
    char cfgWifiSsid[sizeof(s_cfg.wifiSsid)] = {};
    char cfgWifiPass[sizeof(s_cfg.wifiPass)] = {};
    strncpy(cfgWifiSsid, s_cfg.wifiSsid, sizeof(cfgWifiSsid) - 1);
    strncpy(cfgWifiPass, s_cfg.wifiPass, sizeof(cfgWifiPass) - 1);

    // Keep Web Config disabled unless explicitly enabled. Boot-time time sync
    // uses a temporary STA connection and restores prior Wi-Fi state.
    bootTimeSynced = bootTimeNtpSyncDirectSta(cfgWifiSsid, cfgWifiPass);

    if (bootTimeSynced) {
        Serial.println("[time] Boot time sync OK");
    } else {
        Serial.println("[time] Boot time sync skipped/failed");
    }

    // Keep localtime conversion pinned to configured timezone after network time calls.
    applyTimezoneFromConfig();
}

// Panel pixels per pixel of the boot splash's canvas: the LVGL pixel scale,
// and on a natively rendered UI (UI_COORD_SCALE) the layout scale too, so the
// splash -- drawn straight to the panel in fixed sizes -- keeps its proportions.
#define SPLASH_PANEL_SCALE (UI_PIXEL_SCALE * UI_COORD_SCALE)

#if SPLASH_PANEL_SCALE > 1
// Writes a block of UI pixels (board.h) to the panel, each one as a
// SPLASH_PANEL_SCALE-square block (UI_PIXEL_SCALE wherever LVGL flushes
// through it). x, y, w and h are in UI pixels; stride is the
// source's row pitch in pixels. One source row at a time, so the expansion
// needs a few KB rather than a second copy of the block.
//
// Templated on the pixel type because LVGL hands over native-order RGB565 and
// an LGFX sprite stores it byte-swapped; both pass through untouched.
template <typename PixelT>
static void pushPixelScaled(int32_t x, int32_t y, int32_t w, int32_t h,
                            const PixelT *src, int32_t stride) {
    constexpr int kScale = SPLASH_PANEL_SCALE;
    static PixelT s_rows[(size_t)kMaxHorRes * kScale * kScale];
    if (w <= 0 || h <= 0 || w > (int32_t)kMaxHorRes) return;
    const int32_t scaledW = w * kScale;
    displayDev().startWrite();
    for (int32_t row = 0; row < h; row++) {
        const PixelT *in = src + (size_t)row * (size_t)stride;
        PixelT *out = s_rows;
        for (int32_t i = 0; i < w; i++) {
            for (int k = 0; k < kScale; k++) *out++ = in[i];
        }
        for (int r = 1; r < kScale; r++) {
            memcpy(s_rows + (size_t)r * (size_t)scaledW, s_rows,
                   (size_t)scaledW * sizeof(PixelT));
        }
        displayDev().pushImage(x * kScale, (y + row) * kScale,
                               scaledW, kScale, s_rows);
    }
    displayDev().endWrite();
}
#endif

static void lvglFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
#if defined(DEVICE_TDECK_PRO)
    // Indexed LVGL buffers begin with two lv_color32_t palette entries. The I1
    // pixels after them are MSB-first, 0=black/1=white, exactly GxEPD2's native
    // bitmap representation. I1 invalidation is byte-aligned by LVGL.
    constexpr size_t kI1PaletteBytes = 2u * sizeof(lv_color32_t);
    const size_t sourceStride = ((size_t)w + 7u) / 8u;
    if (!lcd.pushI1(area->x1, area->y1, w, h,
                    px_map + kI1PaletteBytes, sourceStride)) {
        Serial.printf("[eink] rejected I1 flush area=(%ld,%ld)-(%ld,%ld)\n",
                      (long)area->x1, (long)area->y1,
                      (long)area->x2, (long)area->y2);
    }
#if HAS_VNC_HOST
    // The mirror is RGB565 and this panel is not, so the host expands the bits
    // as it writes them into its own framebuffer. Same pointer and same stride
    // the panel push above was handed: if that framing were wrong the display
    // would be wrong first.
    //
    // Note this runs on every LVGL flush, not on every e-paper refresh, so the
    // browser sees changes as they are drawn rather than when the panel next
    // settles.
    vncHostCaptureFlushI1(area->x1, area->y1, w, h,
                          px_map + kI1PaletteBytes, sourceStride);
#endif
    if (lv_display_flush_is_last(disp)) lcd.requestRefresh();
#else
    // v9 hands over a raw byte buffer in the display's colour format (RGB565).
    uint16_t *pixels = (uint16_t *)px_map;
#if UI_PIXEL_SCALE > 1
    // LVGL draws at 1/UI_PIXEL_SCALE of the panel (board.h). The mirror and
    // screenshot hooks below still see LVGL's own pixels.
    pushPixelScaled(area->x1, area->y1, w, h, (const lgfx::rgb565_t *)pixels, w);
#else
    displayDev().pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)pixels);
#endif
#if HAS_VNC_HOST
    vncHostCaptureFlush(area->x1, area->y1, w, h, pixels);
#endif
#endif
    // Web screenshots stay T-Deck-only: unlike the mirror above, the capture
    // path mallocs a whole frame out of *internal* RAM (see
    // captureWebScreenshotPng), which is not a cost the other boards have been
    // sized for. It shared the mirror's guard only because both hook the flush.
#if defined(DEVICE_TDECK)
    if (s_screenshotCaptureActive && s_screenshotCaptureFrame && s_screenshotCaptureW > 0 && s_screenshotCaptureH > 0) {
        int32_t capX1 = area->x1 < 0 ? 0 : area->x1;
        int32_t capY1 = area->y1 < 0 ? 0 : area->y1;
        int32_t capX2 = area->x2 >= s_screenshotCaptureW ? (s_screenshotCaptureW - 1) : area->x2;
        int32_t capY2 = area->y2 >= s_screenshotCaptureH ? (s_screenshotCaptureH - 1) : area->y2;

        if (capX1 <= capX2 && capY1 <= capY2) {
            for (int32_t y = capY1; y <= capY2; y++) {
                int32_t srcY = y - area->y1;
                int32_t srcX = capX1 - area->x1;
                const uint16_t *src = pixels + (srcY * w) + srcX;
                uint16_t *dst = s_screenshotCaptureFrame + ((size_t)y * (size_t)s_screenshotCaptureW) + capX1;
                for (int32_t x = capX1; x <= capX2; x++) {
                    *dst++ = *src++;
                }
            }
            s_screenshotCaptureTouched = true;
        }
    }
#endif
    lv_display_flush_ready(disp);
}

// ── P4 portrait status strip ─────────────────────────────────────────────────
// GPS, Wi-Fi and battery, in the blank strip under the nav bar that keeps the
// UI clear of the panel's rounded bottom corners (UI_BOTTOM_SAFE_H, board.h).
// Portrait only: that is the shape with height to spare, and the header gives
// the room the battery leaves to the clock (layoutHeaderInlineItems()).
//
// The strip is outside the display LVGL lays the UI out on -- that is what
// keeps every screen above the curve -- so it gets a small display of its own
// rather than a row inside the main one. Every layout keeps the screen height it
// already had, and the strip still draws with LVGL's fonts and symbols. Its
// flush writes below the main display, scaled exactly like lvglFlush(). Nothing
// on it is interactive, so no input device is attached.
//
// Centred, so its ends stay clear of the corners the strip exists to avoid.
// Painted from refreshHeaderStatus() with the same readings as the header.
#if HAS_STATUS_STRIP
static lv_display_t *s_stripDisplay = nullptr;
static uint8_t      *s_stripBuf = nullptr;
static int32_t       s_stripTopY = 0;        // UI pixels: where the strip starts
static lv_obj_t     *s_stripGps = nullptr;
static lv_obj_t     *s_stripWifi = nullptr;
static lv_obj_t     *s_stripBattIcon = nullptr;
static lv_obj_t     *s_stripBatt = nullptr;

static void lvglStripFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    pushPixelScaled(area->x1, s_stripTopY + area->y1, w, h,
                    (const lgfx::rgb565_t *)px_map, w);
    lv_display_flush_ready(disp);
}

// `uiW` and `topY` in UI pixels: the main display's width and height.
static void statusStripInit(int32_t uiW, int32_t topY) {
    if (s_stripDisplay || !uiPortrait()) return;
    const size_t bufBytes = (size_t)uiW * UI_BOTTOM_SAFE_H * 2u;   // RGB565
    s_stripBuf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_stripBuf) {
        s_stripBuf = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_stripBuf) {
        Serial.println("[strip] buffer allocation failed; no status strip");
        return;
    }
    // lv_display_create() leaves the default display alone once there is one,
    // so every lv_disp_get_*_res(NULL) in the UI still answers for the main one.
    s_stripDisplay = lv_display_create(uiW, UI_BOTTOM_SAFE_H);
    if (!s_stripDisplay) {
        heap_caps_free(s_stripBuf);
        s_stripBuf = nullptr;
        Serial.println("[strip] display allocation failed; no status strip");
        return;
    }
    s_stripTopY = topY;
    lv_display_set_color_format(s_stripDisplay, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_stripDisplay, lvglStripFlush);
    lv_display_set_buffers(s_stripDisplay, s_stripBuf, nullptr, (uint32_t)bufBytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_obj_t *scr = lv_display_get_screen_active(s_stripDisplay);
    lv_obj_remove_style_all(scr);
    // Black like the rest of the strip: on this AMOLED that is pixels off, so
    // the corners the strip sits between stay invisible.
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);

    // The nav bar's status size on this board (buildNavStatusCluster()).
    const lv_font_t *font = &lv_font_montserrat_14;
    s_stripGps = lv_label_create(row);
    lv_obj_set_style_text_font(s_stripGps, font, 0);
    s_stripWifi = lv_label_create(row);
    lv_obj_set_style_text_font(s_stripWifi, font, 0);

    // Icon and reading as one item, so the column gap does not open between them.
    lv_obj_t *batt = lv_obj_create(row);
    lv_obj_remove_style_all(batt);
    lv_obj_set_size(batt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(batt, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(batt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(batt, 4, 0);
    // Reading first, then the icon: "100%" to the left of the battery.
    s_stripBatt = lv_label_create(batt);
    lv_obj_set_style_text_font(s_stripBatt, font, 0);
    // Coloured in statusStripPaint(), not here: see there.
    lv_label_set_text(s_stripBatt, "");
    s_stripBattIcon = lv_label_create(batt);
    lv_obj_set_style_text_font(s_stripBattIcon, font, 0);
    lv_label_set_text(s_stripBattIcon, LV_SYMBOL_BATTERY_EMPTY);

    Serial.printf("[strip] status strip %ldx%d at UI y=%ld\n",
                  (long)uiW, UI_BOTTOM_SAFE_H, (long)topY);
}

static void statusStripPaint(bool gpsEnabled, bool gpsFix, uint8_t gpsSats,
                             bool wifiApMode, bool wifiConnected,
                             uint8_t battPct, float battV, lv_color_t battInk) {
    if (!s_stripDisplay) return;
    // The lock screen's ink, not the header's: it is chosen for a black ground,
    // which this always is, where the header's follows the UI theme.
    paintStatusIcons(s_stripGps, s_stripWifi, gpsEnabled, gpsFix, gpsSats,
                     wifiApMode, wifiConnected, glanceStatusInk());
    // A battery rather than the header's dot: with room for a real icon it can
    // show the level as well as the colour. Same five steps LVGL's symbol set
    // has, and the header's charge colour on it.
    const char *icon = (battPct >= 90) ? LV_SYMBOL_BATTERY_FULL
                     : (battPct >= 65) ? LV_SYMBOL_BATTERY_3
                     : (battPct >= 40) ? LV_SYMBOL_BATTERY_2
                     : (battPct >= 15) ? LV_SYMBOL_BATTERY_1
                                       : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text(s_stripBattIcon, icon);
    lv_obj_set_style_text_color(s_stripBattIcon, battInk, 0);
    // Formatted here rather than copied from the header's label, so the strip
    // does not depend on another widget's state to have a reading. The same
    // choice of units the header makes (Config -> battery display).
    char battText[12];
    if (s_cfg.battDisplayMode == BATT_DISPLAY_VOLTAGE) {
        snprintf(battText, sizeof(battText), "%.2fV", (double)battV);
    } else {
        snprintf(battText, sizeof(battText), "%u%%", (unsigned)battPct);
    }
    lv_label_set_text(s_stripBatt, battText);
    // A fixed ink, set on every paint. lv_color_hex() is the theme remap in this
    // file (themedColorHex()), and the strip is built with the display -- before
    // the theme table is filled -- so 0xBFD6FF resolved to an all-zero
    // statusText and the reading was drawn black on the strip's black. The
    // strip is black on every theme, so it wants the plain colour anyway.
    lv_obj_set_style_text_color(s_stripBatt, lv_color_make(0xBF, 0xD6, 0xFF), 0);
}
#endif

#if HAS_VNC_HOST
static void lvglVncPointerRead(lv_indev_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
    int16_t x = 0;
    int16_t y = 0;
    bool pressed = false;
    if (vncHostReadPointer(&x, &y, &pressed)) {
        data->point.x = x;
        data->point.y = y;
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

#if defined(DEVICE_MESH_DECK)
// Reads the FT6636 over Arduino Wire instead of going through LovyanGFX.
//
// LovyanGFX drives its own I2C on port 0, and s_keyboard.begin() calls
// Wire.begin()/setClock() on that same port after lcd.init() — so the panel's
// touch handle is left talking to a bus another driver has re-initialised, and
// every read comes back empty. The controller itself is fine: it ACKs at 0x38
// on the Wire bus the expanders and keyboard use all session. So use that bus.
//
// Register layout is the FT6x36 standard: one 7-byte burst from 0x02 gives the
// touch count and the first point's 12-bit X and Y.
static bool meshDeckReadTouch(int32_t *outX, int32_t *outY) {
    Wire.beginTransmission((uint8_t)TOUCH_ADDR);
    Wire.write((uint8_t)0x02);                      // TD_STATUS
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)TOUCH_ADDR, 7) != 7) return false;

    uint8_t b[7];
    for (int i = 0; i < 7; i++) b[i] = (uint8_t)Wire.read();

    const uint8_t points = (uint8_t)(b[0] & 0x0F);
    if (points == 0 || points > 2) return false;    // 0xFF etc. = nothing valid

    const uint16_t rawX = (uint16_t)(((b[1] & 0x0F) << 8) | b[2]);
    const uint16_t rawY = (uint16_t)(((b[3] & 0x0F) << 8) | b[4]);

    // Raw coordinates are in the panel's native portrait frame (240x320). The
    // panel is mounted 180 degrees out, so the UI runs at rotation 3 — the same
    // +2 correction Attaky documents for the display applies to touch.
    const int32_t nativeW = TFT_PANEL_WIDTH;        // 240
    const int32_t nativeH = TFT_PANEL_HEIGHT;       // 320
    int32_t x = rawX, y = rawY;
    if (x >= nativeW) x = nativeW - 1;
    if (y >= nativeH) y = nativeH - 1;

    // rotation 3 (landscape, 180 from rotation 1): screen x runs along native
    // y reversed, screen y along native x.
    *outX = (nativeH - 1) - y;
    *outY = x;
    return true;
}
#endif

#if FEATURE_LOCK_SCREEN && TOUCH_POLL_ENABLED && HAS_TOUCH && HAS_MESSAGE_ACTIONS \
    && SCREEN_WAKE_FROM_TOUCH
#define HAS_LOCK_MSG_HOLD 1
#else
#define HAS_LOCK_MSG_HOLD 0
#endif

#if HAS_LOCK_MSG_HOLD
// Long press on a lock-screen message preview: unlock, open that message's
// channel, and bring up Message Actions for it, as a hold on the same message in
// chat would. Every other touch on the lock screen still dismisses it on
// contact; a touch that lands on a preview row waits to see whether it is a
// hold, and dismisses on release if it is not.
static constexpr uint32_t kLockMsgHoldMs     = 600;
static constexpr int32_t  kLockMsgHoldSlopPx = 16;
static bool     s_lockMsgHoldActive = false;
static uint32_t s_lockMsgHoldPid = 0;
static uint32_t s_lockMsgHoldSender = 0;
static int      s_lockMsgHoldChan = -1;
static uint32_t s_lockMsgHoldStartMs = 0;
static int32_t  s_lockMsgHoldX = 0;
static int32_t  s_lockMsgHoldY = 0;
// Set once Actions has opened under a finger still down, so the rest of that
// touch cannot press whatever in the menu happens to be under it.
static bool     s_lockTouchSwallow = false;

// The preview row under screen row `y`, or -1. Only rows Message Actions can act
// on count: channel messages from someone else, carrying a packet id -- the
// same rule the chat view's long press applies. A DM row reads as no row.
static int lockMsgRowAt(int32_t y) {
    if (s_lockMsgPage && glanceFacing(s_lockCarousel) != GLANCE_PAGE_MESSAGES) return -1;
    const int32_t slop = kTdeckProMsgGapY / 2;
    for (int i = 0; i < kTdeckProSleepMsgSlots; i++) {
        lv_obj_t *bold = s_tdeckProSleepMsgBold[i];
        if (!lvObjValid(bold) || lv_obj_has_flag(bold, LV_OBJ_FLAG_HIDDEN)) continue;
        if (s_lockMsgRowPid[i] == 0 || s_lockMsgRowChan[i] < 0
            || s_lockMsgRowChan[i] >= MESH_CHANNELS
            || s_lockMsgRowSender[i] == 0 || s_lockMsgRowSender[i] == s_myNodeId) {
            continue;
        }
        lv_area_t a;
        lv_obj_get_coords(bold, &a);
        int32_t bottom = a.y2;
        lv_obj_t *cont = s_tdeckProSleepMsgCont[i];
        if (lvObjValid(cont) && !lv_obj_has_flag(cont, LV_OBJ_FLAG_HIDDEN)) {
            lv_area_t c;
            lv_obj_get_coords(cont, &c);
            if (c.y2 > bottom) bottom = c.y2;
        }
        if (y >= a.y1 - slop && y <= bottom + slop) return i;
    }
    return -1;
}

static void lockMsgOpenActions(uint32_t pid, int chan, uint32_t sender) {
    if (pid == 0 || chan < 0 || chan >= MESH_CHANNELS) return;

    exitLockScreen();
    if (s_lockScreenActive) return;

    // Out of whatever was open underneath: Actions acts on the active channel,
    // and a tapback sent with the DM screen up would go to that conversation.
#if HAS_HOME_DASHBOARD
    if (homeDashboardVisible()) closeHomeDashboard();
#endif
    if (s_dmModal) closeDmModal();
    if (s_nodesActionModal) closeNodesActionMenu();
    closeEmojiPicker();

    if (chan != s_activeChannel) setActiveChannel(chan);
    const char *text = nullptr;
    uint32_t lineSender = 0;
    if (chatLineForPacketId(pid, &lineSender, &text) && lineSender != 0) sender = lineSender;
    setSelectedReplyContext(pid, text ? text : "", sender);
    openMessageActionMenu(pid, sender);
    Serial.printf("[screen] lock screen hold, actions for %08lx\n", (unsigned long)pid);
}

// A touched sample while locked. True when this consumed it; false leaves the
// caller to dismiss as for any other touch.
static bool lockMsgHoldOnTouch(uint32_t nowMs, int32_t x, int32_t y) {
    // A hold from an earlier lock session that never saw its release.
    if (s_lockMsgHoldActive
        && (int32_t)(s_lockScreenSinceMs - s_lockMsgHoldStartMs) > 0) {
        s_lockMsgHoldActive = false;
    }
    if (!s_lockMsgHoldActive) {
        if ((int32_t)(nowMs - s_screenWakeBlockedUntilMs) < 0) return false;
        const int row = lockMsgRowAt(y);
        if (row < 0) return false;
        s_lockMsgHoldActive = true;
        // The message, not the row: a repaint mid-hold can move every row down.
        s_lockMsgHoldPid = s_lockMsgRowPid[row];
        s_lockMsgHoldSender = s_lockMsgRowSender[row];
        s_lockMsgHoldChan = s_lockMsgRowChan[row];
        s_lockMsgHoldStartMs = nowMs;
        s_lockMsgHoldX = x;
        s_lockMsgHoldY = y;
        return true;
    }
    if (abs(x - s_lockMsgHoldX) > kLockMsgHoldSlopPx
        || abs(y - s_lockMsgHoldY) > kLockMsgHoldSlopPx
        || (s_lockMsgPage && glanceFacing(s_lockCarousel) != GLANCE_PAGE_MESSAGES)) {
        // A drag rather than a hold, or the band turned away from the message
        // under the finger: dismiss like any other touch.
        s_lockMsgHoldActive = false;
        return false;
    }
    if ((uint32_t)(nowMs - s_lockMsgHoldStartMs) >= kLockMsgHoldMs) {
        s_lockMsgHoldActive = false;
        s_lockTouchSwallow = true;
        lockMsgOpenActions(s_lockMsgHoldPid, s_lockMsgHoldChan, s_lockMsgHoldSender);
    }
    return true;
}

// The finger came up. A press on a preview row that never became a hold is a
// tap, and a tap dismisses.
static void lockMsgHoldOnRelease(uint32_t nowMs) {
    s_lockTouchSwallow = false;
    if (!s_lockMsgHoldActive) return;
    s_lockMsgHoldActive = false;
    (void)tryExitLockScreenFromInput(nowMs, true);
}
#endif  // HAS_LOCK_MSG_HOLD

#if TOUCH_POLL_ENABLED
// A finger used to scroll a list must not end up tapping the row it held. LVGL
// already withholds the click from a press that turned into a scroll; two
// things got past that.
//
// A controller that misses a report mid-drag. The read comes back untouched
// for a frame, LVGL takes that as a release -- ending the scroll -- and the
// next frame as a fresh press on whatever row is under the finger now, which
// the lift then clicks. So a release has to last kTouchReleaseHoldMs before it
// is reported; until then the finger is held where it was last seen.
//
// A finger landing on a list still coasting from a flick. LVGL stops the
// coast and treats the touch as a new press, so lifting clicks the row it
// stopped on. That touch is a grip, so its clicks are dropped (onTouchIndev-
// Event()); it can still drag.
static constexpr uint32_t kTouchReleaseHoldMs = 60;
static bool     s_touchDown = false;
static bool     s_touchGrip = false;
static uint32_t s_touchLastDownMs = 0;
static int32_t  s_touchLastX = 0;
static int32_t  s_touchLastY = 0;

static void onTouchIndevEvent(lv_event_t *e) {
    if (!s_touchGrip) return;
    lv_indev_stop_processing((lv_indev_t *)lv_event_get_current_target(e));
}
#endif

static void lvglTouchRead(lv_indev_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
#if TOUCH_POLL_ENABLED
    int32_t tx = 0;
    int32_t ty = 0;
#if defined(DEVICE_MESH_DECK)
    const bool touched = meshDeckReadTouch(&tx, &ty);
#elif defined(DEVICE_CARDPUTER_ZERO)
    const bool touched = czeroWindowPointer(tx, ty);
#else
    const bool touched = displayDev().getTouch(&tx, &ty);
#endif
#if UI_PIXEL_SCALE > 1
    // Panel pixels back into LVGL's scaled-down space (board.h).
    tx /= UI_PIXEL_SCALE;
    ty /= UI_PIXEL_SCALE;
#endif
#if UI_BOTTOM_SAFE_H > 0
    // A finger in the blank strip under the UI (board.h) meant the row just
    // above it -- the nav bar, as often as not -- so give it that row.
    {
        const int32_t maxY = (int32_t)lv_disp_get_ver_res(NULL) - 1;
        if (ty > maxY) ty = maxY;
    }
#endif

#if defined(DEVICE_MESH_DECK) && defined(MESH_DECK_TOUCH_TRACE)
    // Instrumentation at the component boundary, per Attaky's touch bring-up
    // guide: this one line splits a dead-touch fault in half. If it never
    // prints, the read callback is not running at all and the fault is above
    // the driver — LVGL not pumping its input subsystem, or the indev never
    // registered. If it prints with touched=0 forever, the read path is alive
    // and the fault is the controller or its transform.
    //
    // Rate-limited, and it must log every call rather than only on a touch —
    // "never prints" is precisely the signal we are looking for.
    {
        static uint32_t lastTraceMs = 0;
        static uint32_t calls = 0;
        calls++;
        const uint32_t nowMs = millis();
        if ((uint32_t)(nowMs - lastTraceMs) >= 1000UL) {
            lastTraceMs = nowMs;
            // Probe the controller here too. The one-shot line at boot is
            // printed before the USB bridge settles and gets lost, and with the
            // read path proven alive this is the remaining question: does the
            // FT6636 answer at all?
            Wire.beginTransmission((uint8_t)TOUCH_ADDR);
            const bool acked = (Wire.endTransmission() == 0);
            Serial.printf("[touch-trace] read_cb calls=%lu touched=%d screen=(%ld,%ld) 0x%02X=%s"
                          "  kb: L(0x5A)=%s R(0x5B)=%s\n",
                          (unsigned long)calls, touched ? 1 : 0,
                          (long)tx, (long)ty, (unsigned)TOUCH_ADDR,
                          acked ? "ACK" : "no-ack",
                          meshDeckKeyboardHalfPresent(0) ? "ok" : "MISSING",
                          meshDeckKeyboardHalfPresent(1) ? "ok" : "MISSING");
        }
    }
#endif

#if defined(DEVICE_MESH_DECK) && defined(MESH_DECK_TOUCH_TRACE)
    if (touched) {
        // Unthrottled-per-tap: touch known corners to verify the rotation
        // mapping without waiting for the 1 Hz trace.
        static uint32_t lastHitMs = 0;
        const uint32_t hitMs = millis();
        if ((uint32_t)(hitMs - lastHitMs) >= 150UL) {
            lastHitMs = hitMs;
            Serial.printf("[touch-hit] screen=(%ld,%ld) of %dx%d\n",
                          (long)tx, (long)ty,
                          (int)lv_disp_get_hor_res(NULL), (int)lv_disp_get_ver_res(NULL));
        }
    }
#endif

    if (touched) {
#if FEATURE_LOCK_SCREEN
        if (s_lockScreenActive) {
#if HAS_LOCK_MSG_HOLD
            if (lockMsgHoldOnTouch(millis(), tx, ty)) {
                data->state = LV_INDEV_STATE_RELEASED;
                return;
            }
#endif
            (void)tryExitLockScreenFromInput(millis(), SCREEN_WAKE_FROM_TOUCH);
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
#endif
#if HAS_LOCK_MSG_HOLD
        if (s_lockTouchSwallow) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
#endif
        if (s_screenAsleep) {
            // Normally unreachable — loop() stops calling lv_timer_handler()
            // while the screen is off — but modal helpers do call it directly,
            // so honour the board's wake policy here rather than letting a
            // stray tap wake a device that excludes touch as a wake gesture.
#if SCREEN_WAKE_FROM_TOUCH
            (void)tryWakeScreenFromInput(millis());
#endif
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        s_lastActivityMs = millis();
        if (!s_touchDown) {
            s_touchDown = true;
            // Set while LVGL is still coasting a flicked list.
            s_touchGrip = (lv_indev_get_scroll_obj(indev) != nullptr);
        }
        s_touchLastDownMs = s_lastActivityMs;
        s_touchLastX = tx;
        s_touchLastY = ty;
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else if (s_touchDown && (uint32_t)(millis() - s_touchLastDownMs) < kTouchReleaseHoldMs) {
        // A missed report, not a lift (see kTouchReleaseHoldMs).
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = s_touchLastX;
        data->point.y = s_touchLastY;
    } else {
        s_touchDown = false;
#if HAS_LOCK_MSG_HOLD
        lockMsgHoldOnRelease(millis());
#endif
        data->state = LV_INDEV_STATE_RELEASED;
    }
#else
    data->state = LV_INDEV_STATE_RELEASED;
#endif
}

// Touch-to-wake while the screen is off.
//
// lvglTouchRead() above is an LVGL indev callback, so it only ever runs from
// inside lv_timer_handler() — and loop() stops calling that once the screen is
// asleep. On boards where touch is a wake gesture that callback was the only
// path by which a tap could wake the display, so the poll has to happen
// directly here instead. The touch controller is a separate I2C device and
// stays readable while the panel itself is in SLPIN.
//
// Compiled out where touch is not a wake gesture (T-Deck), which also drops the
// I2C poll that would otherwise run for the entire time the screen is off.
static void serviceTouchWakeWhileAsleep() {
#if TOUCH_POLL_ENABLED && SCREEN_WAKE_FROM_TOUCH
    int32_t tx = 0;
    int32_t ty = 0;
    if (displayDev().getTouch(&tx, &ty)) {
        (void)tryWakeScreenFromInput(millis());
    }
#endif
}

// ── Splash boot-progress line ────────────────────────────────────────────────
// The splash now stays up through the whole init sequence, which is several
// seconds of WiFi, storage and radio work. This is the line that says which
// part is running, drawn straight to the panel like the rest of the splash —
// LVGL is initialised by then but nothing pumps it until loop().
//
// drawBootSplash() registers the band; bootSplashStatus() repaints it. Only the
// layouts that have somewhere to put it register one, so on the Pager and
// Cardputer every call below is a no-op rather than something to #if around at
// each of the ~a dozen call sites in setup().
static bool     s_splashStatusOn = false;
static int      s_splashStatusX  = 0;
static int      s_splashStatusY  = 0;
static int      s_splashStatusW  = 0;
static uint16_t s_splashStatusFg = 0;
static uint16_t s_splashStatusBg = 0;
static char     s_splashStatusMsg[40] = "";
static uint8_t  s_splashStatusDots = 1;      // 1..3
static uint32_t s_splashStatusDotMs = 0;

static constexpr uint32_t kSplashDotIntervalMs = 350;

// ── Splash canvas ───────────────────────────────────────────────────────────
// The splash draws straight to the panel in fixed pixel sizes, so on a
// pixel-scaled panel (UI_PIXEL_SCALE, board.h) it would come out at a fraction
// of the size it was laid out for. There it draws into a UI-sized sprite
// instead, and splashPresent() writes that out scaled exactly as the LVGL flush
// does. The sprite is the UI area -- the bottom strip excluded -- so the card
// stays clear of the rounded corners the same way the interface does.
//
// Everywhere else splashDev() is displayDev() and presenting is a no-op.
#if SPLASH_PANEL_SCALE > 1
static LGFX_Sprite s_splashCanvas(&lcd);
static bool s_splashCanvasTried = false;
static bool s_splashCanvasReady = false;

static lgfx::LovyanGFX &splashDev() {
    if (!s_splashCanvasTried) {
        s_splashCanvasTried = true;
        s_splashCanvas.setPsram(true);
        s_splashCanvas.setColorDepth(16);
        const int32_t w = displayDev().width() / SPLASH_PANEL_SCALE;
        const int32_t h = displayDev().height() / SPLASH_PANEL_SCALE - UI_BOTTOM_SAFE_H;
        s_splashCanvasReady = (s_splashCanvas.createSprite(w, h) != nullptr);
        if (!s_splashCanvasReady) {
            // Unscaled beats absent: the splash still says the device is booting.
            Serial.println("[splash] canvas allocation failed; drawing unscaled");
        }
    }
    if (s_splashCanvasReady) return s_splashCanvas;
    return displayDev();
}

// Pushes one rectangle of the canvas (canvas pixels) to the panel.
static void splashPresent(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!s_splashCanvasReady) return;
    const int32_t cw = s_splashCanvas.width();
    const int32_t ch = s_splashCanvas.height();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > cw) w = cw - x;
    if (y + h > ch) h = ch - y;
    if (w <= 0 || h <= 0) return;
    // 16-bit sprites hold RGB565 byte-swapped.
    const lgfx::swap565_t *buf = (const lgfx::swap565_t *)s_splashCanvas.getBuffer();
    pushPixelScaled(x, y, w, h, buf + (size_t)y * (size_t)cw + (size_t)x, cw);
}

static void splashPresentAll() {
    if (!s_splashCanvasReady) return;
    const int32_t stripTop = s_splashCanvas.height() * SPLASH_PANEL_SCALE;
    displayDev().fillRect(0, stripTop, displayDev().width(),
                          displayDev().height() - stripTop, TFT_BLACK);
    splashPresent(0, 0, s_splashCanvas.width(), s_splashCanvas.height());
}

// The canvas is only needed until the UI owns the panel.
static void splashCanvasRelease() {
    if (s_splashCanvasReady) s_splashCanvas.deleteSprite();
    s_splashCanvasReady = false;
}
#else
static auto &splashDev() { return displayDev(); }
static inline void splashPresent(int32_t, int32_t, int32_t, int32_t) {}
static inline void splashPresentAll() {}
static inline void splashCanvasRelease() {}
#endif

// Repaint the band with the current message and dot count.
static void bootSplashStatusPaint() {
    if (!s_splashStatusOn || !s_splashStatusMsg[0]) return;
    char line[sizeof(s_splashStatusMsg) + 4];
    const uint8_t n = (s_splashStatusDots < 1) ? 1
                      : (s_splashStatusDots > 3) ? 3 : s_splashStatusDots;
    snprintf(line, sizeof(line), "%s%.*s", s_splashStatusMsg, (int)n, "...");

    splashDev().setFont(&fonts::DejaVu12);
    splashDev().setTextSize(1.0f);
    const int h = splashDev().fontHeight();
    // Clear the whole band first: messages vary in width, and the tail of a
    // longer previous one would otherwise stay on screen next to a shorter one.
    // This also covers the dots shrinking from three back to one.
    splashDev().fillRect(s_splashStatusX, s_splashStatusY, s_splashStatusW, h,
                          s_splashStatusBg);
    splashDev().setTextColor(s_splashStatusFg, s_splashStatusBg);
    // Centred on the message alone, not on message+dots, so the text does not
    // jitter left and right as the dots cycle.
    const int msgW = splashDev().textWidth(s_splashStatusMsg);
    splashDev().drawString(line, s_splashStatusX + max(0, (s_splashStatusW - msgW) / 2),
                            s_splashStatusY);
    splashPresent(s_splashStatusX, s_splashStatusY, s_splashStatusW, h);
#if defined(DEVICE_TDECK_PRO)
    lcd.requestRefresh();
    lcd.serviceRefresh();
#endif
}

static void bootSplashStatus(const char *msg) {
    if (!s_splashStatusOn || !msg || !*msg) return;
    strlcpy(s_splashStatusMsg, msg, sizeof(s_splashStatusMsg));
    s_splashStatusDots = 1;
    s_splashStatusDotMs = millis();
    bootSplashStatusPaint();
}

// Advance the dots. Called from the blocking waits that make boot slow enough
// to be worth animating — the station-connect loops in bootTimeNtpSync() and
// web config, and the splash hold itself. Steps that finish in milliseconds
// simply never call it, which is fine: nothing is on screen long enough to
// animate. Rate-limited, so callers can invoke it as often as they like.
static void bootSplashTick() {
    if (!s_splashStatusOn) return;
    const uint32_t now = millis();
    if ((uint32_t)(now - s_splashStatusDotMs) < kSplashDotIntervalMs) return;
    s_splashStatusDotMs = now;
    s_splashStatusDots = (uint8_t)((s_splashStatusDots % 3) + 1);
    bootSplashStatusPaint();
}

// Stop accepting progress updates. Called once the UI owns the panel, so a late
// call cannot paint a boot message over the running interface.
static void bootSplashStatusEnd() {
    s_splashStatusOn = false;
    splashCanvasRelease();
}

// Where drawBootSplash() put (or would put) a stored logo, for `cfg caps`.
static int s_splashLogoW = 0;
static int s_splashLogoH = 0;
static uint16_t s_splashLogoBg = 0;

static void drawBootSplash() {
    const int screenW = splashDev().width();
    const int screenH = splashDev().height();

    // Color displays use the fixed Camillia Dark splash. The e-paper build uses
    // its equally fixed inverse paper palette.
#if HAS_UI_THEMES
    const UiThemePresetLite &splashPreset = kUiThemePresets[0]; // Camillia Dark
#else
    const UiThemePresetLite &splashPreset = kPaperTheme;
#endif
    const uint16_t splashBgMain = splashPreset.bgMain;
    const uint16_t splashPanelBg = splashPreset.panelBg;
    const uint16_t splashPanelAlt = splashPreset.panelAlt;
    const uint16_t splashAccent = splashPreset.accent;
#if HAS_UI_THEMES
    const uint16_t splashTextMain = rgb565(0xF3, 0xF6, 0xFA);
    const uint16_t splashTextDim = rgb565(0xB7, 0xC0, 0xCC);
#else
    const uint16_t splashTextMain = TFT_BLACK;
    const uint16_t splashTextDim = TFT_BLACK;
#endif

    const uint16_t bgTop = blend565(splashBgMain, splashPanelBg, 96);
    const uint16_t bgBottom = splashBgMain;
    const uint16_t cardBg = splashPanelBg;
    const uint16_t cardEdge = blend565(splashPanelBg, splashAccent, 66);
    const uint16_t cardEdgeHi = blend565(splashPanelAlt, splashAccent, 92);
    const uint16_t titleCol = splashTextMain;
    const uint16_t subCol = splashTextDim;
    const uint16_t dimCol = blend565(splashTextDim, splashPanelBg, 72);

    for (int y = 0; y < screenH; y++) {
        uint8_t t = (uint8_t)((255UL * y) / max(1, screenH - 1));
        splashDev().drawFastHLine(0, y, screenW, blend565(bgTop, bgBottom, t));
    }

    const int cardMargin = 10;
    // A card sized to what it holds, centred on a background with faint
    // camellias in its corners, rather than one filling the panel: the boards
    // whose screens are far bigger than the card's contents.
#if defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_CROWPANEL_50)
#define SPLASH_FLOATING_CARD 1
#else
#define SPLASH_FLOATING_CARD 0
#endif
#if defined(DEVICE_TDISPLAY_P4)
    // The portrait card's width in both orientations, centred. Landscape used
    // to run the card end to end across 616 px with the same narrow stack of
    // title, flower and footer in the middle of it; now it is the same card,
    // on a wider background. Everything below is placed from cardX/cardW.
    const int cardW = min(screenW - cardMargin * 2,
                          DEVICE_LCD_PORTRAIT_W / UI_PIXEL_SCALE - cardMargin * 2);
    const int cardX = (screenW - cardW) / 2;
#elif defined(DEVICE_CROWPANEL_50)
    // Room round the title and footer, which are drawn at the sizes of the
    // 320 px boards, without stretching them across an 800 px panel.
    const int cardW = min(screenW - cardMargin * 2, 360);
    const int cardX = (screenW - cardW) / 2;
#else
    const int cardX = cardMargin;
#endif
#if SPLASH_FLOATING_CARD
    // Sized to what it holds rather than to the canvas, and centred in it: on
    // the 596-tall canvas any fixed card left dead space above and below the
    // flower. Title block, a gap, the flower, a gap, the footer -- each measured
    // the way the drawing below places it, so the flower lands in the middle of
    // its band with kSplashFlowerGap clear either side. The gradient still fills
    // the screen round it. Capped at the canvas, which only bites in landscape.
    int cardH = 0;
    {
        // The title block exactly as drawn below: brand, subtitle, version.
        splashDev().setFont(&Roboto_Bold26pt7b);
        splashDev().setTextSize(MY_SPLASH_TITLE_SCALE);
        const int brandCapH = max(8, (int)splashDev().fontHeight() - MY_SPLASH_SUBTITLE_GAP_TRIM);
        splashDev().setFont(&Roboto_Medium14pt7b);
        splashDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
        const int subH = (int)splashDev().fontHeight();
        splashDev().setFont(&fonts::DejaVu9);
        splashDev().setTextSize(1.0f);
        const int verH = (int)splashDev().fontHeight() + 2;
        const int titleBlockH = 12 + MY_SPLASH_TITLE_Y_OFFSET + brandCapH + subH + verH;

        // drawCamelliaMark() at scale 1 reaches 30 px above its centre (outer
        // petals) and 38 below (leaves). The band is centred on the mark, so
        // it is sized to the larger half on both sides.
        constexpr int kSplashFlowerBandH = 2 * 38;
        constexpr int kSplashFlowerGap = 10;
        // Node line at cardH-36 and status at cardH-20; the flower band ends
        // at cardH-40.
        constexpr int kSplashFooterH = 40;
        cardH = min(screenH - 20, titleBlockH + kSplashFlowerGap * 2
                                  + kSplashFlowerBandH + kSplashFooterH);
    }
    const int cardY = (screenH - cardH) / 2;
#else
    const int cardY = 10;
    const int cardH = screenH - 20;
#endif
#if !SPLASH_FLOATING_CARD
    const int cardW = screenW - cardMargin * 2;
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    const float flowerScale = 1.15f;
#else
    const float flowerScale = 1.0f;
#endif

    // `alpha` fades the whole mark into the splash background: 255 is the
    // mark as designed, lower is fainter. Each colour is mixed with the
    // gradient at the height it is drawn at, since the shapes are opaque.
    auto drawCamelliaMark = [&](int cx, int cy, float scale, uint8_t alpha = 255) {
        const uint16_t PETAL_OUTER  = 0xF9CF;
        const uint16_t PETAL_MID    = 0xFADF;
        const uint16_t PETAL_INNER  = 0xFF7D;
        const uint16_t PETAL_EDGE   = 0xD8A7;
        const uint16_t CENTER       = 0xFD20;
        const uint16_t CENTER_DOT   = 0xFEA0;
        const uint16_t STEM         = 0x64EC;
        const uint16_t LEAF_DARK    = 0x2C87;
        const uint16_t LEAF_LIGHT   = 0x3D68;
        auto fade = [&](uint16_t c, int y) -> uint16_t {
            if (alpha >= 255) return c;
            const int yy = (y < 0) ? 0 : (y >= screenH ? screenH - 1 : y);
            const uint16_t bg = blend565(bgTop, bgBottom,
                                         (uint8_t)((255UL * yy) / max(1, screenH - 1)));
            return blend565(bg, c, alpha);
        };

        auto scaled = [&](float value, int minValue = 0) -> int {
            int result = (int)lroundf(value * scale);
            return (result < minValue) ? minValue : result;
        };

        const int petalOuterOrbitX = scaled(23.0f, 1);
        const int petalOuterOrbitY = scaled(18.0f, 1);
        const int petalMidOrbitX = scaled(13.0f, 1);
        const int petalMidOrbitY = scaled(10.0f, 1);
        const int petalInnerOrbitX = scaled(6.0f, 1);
        const int petalInnerOrbitY = scaled(5.0f, 1);
        const int petalOuterR0 = scaled(11.0f, 1);
        const int petalOuterR1 = scaled(12.0f, 1);
        const int petalMidR = scaled(9.0f, 1);
        const int petalInnerR = scaled(6.0f, 1);
        const int centerR = scaled(6.0f, 1);
        const int centerDotOrbit = scaled(4.0f, 1);
        const int centerDotR = scaled(1.0f, 1);
        const int stemX = scaled(1.0f);
        const int stemY = scaled(20.0f);
        const int stemW = scaled(3.0f, 1);
        const int stemH = scaled(17.0f, 1);
        const int stemRadius = scaled(1.0f, 1);
        const int leafOuterX = scaled(21.0f, 1);
        const int leafOuterYLeft = scaled(28.0f, 1);
        const int leafOuterYRight = scaled(29.0f, 1);
        const int leafOuterR = scaled(8.0f, 1);
        const int leafInnerX = scaled(14.0f, 1);
        const int leafInnerYLeft = scaled(30.0f, 1);
        const int leafInnerYRight = scaled(31.0f, 1);
        const int leafInnerR = scaled(6.0f, 1);

#if defined(DEVICE_TDECK_PRO)
        const uint16_t ink = TFT_BLACK;
        for (int i = 0; i < 10; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
            int px = cx + (int)lroundf((float)petalOuterOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalOuterOrbitY * sinf(a));
            splashDev().drawCircle(px, py, (i & 1) ? petalOuterR1 : petalOuterR0, ink);
        }
        for (int i = 0; i < 8; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
            int px = cx + (int)lroundf((float)petalMidOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalMidOrbitY * sinf(a));
            splashDev().drawCircle(px, py, petalMidR, ink);
        }
        for (int i = 0; i < 5; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 5.0f) + 0.20f;
            int px = cx + (int)lroundf((float)petalInnerOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalInnerOrbitY * sinf(a));
            splashDev().drawCircle(px, py, petalInnerR, ink);
        }
        splashDev().drawCircle(cx, cy, centerR, ink);
        splashDev().drawRoundRect(cx - stemX, cy + stemY, stemW, stemH, stemRadius, ink);
        splashDev().drawCircle(cx - leafOuterX, cy + leafOuterYLeft, leafOuterR, ink);
        splashDev().drawCircle(cx + leafOuterX, cy + leafOuterYRight, leafOuterR, ink);
        return;
#endif

        for (int i = 0; i < 10; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
            int px = cx + (int)lroundf((float)petalOuterOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalOuterOrbitY * sinf(a));
            int pr = (i & 1) ? petalOuterR1 : petalOuterR0;
            splashDev().fillCircle(px, py, pr, fade(PETAL_OUTER, py));
            splashDev().drawCircle(px, py, pr, fade(PETAL_EDGE, py));
        }

        for (int i = 0; i < 8; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
            int px = cx + (int)lroundf((float)petalMidOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalMidOrbitY * sinf(a));
            splashDev().fillCircle(px, py, petalMidR, fade(PETAL_MID, py));
            splashDev().drawCircle(px, py, petalMidR, fade(PETAL_EDGE, py));
        }

        for (int i = 0; i < 5; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 5.0f) + 0.20f;
            int px = cx + (int)lroundf((float)petalInnerOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalInnerOrbitY * sinf(a));
            splashDev().fillCircle(px, py, petalInnerR, fade(PETAL_INNER, py));
        }

        splashDev().fillCircle(cx, cy, centerR, fade(CENTER, cy));
        splashDev().drawCircle(cx, cy, centerR, fade(0xD4C0, cy));
        for (int i = 0; i < 10; i++) {
            float a = (float)i * 2.0f * (float)M_PI / 10.0f;
            int sx = cx + (int)lroundf((float)centerDotOrbit * cosf(a));
            int sy = cy + (int)lroundf((float)centerDotOrbit * sinf(a));
            splashDev().fillCircle(sx, sy, centerDotR, fade(CENTER_DOT, sy));
        }

        splashDev().fillRoundRect(cx - stemX, cy + stemY, stemW, stemH, stemRadius, fade(STEM, cy + stemY + stemH / 2));
        splashDev().fillCircle(cx - leafOuterX, cy + leafOuterYLeft, leafOuterR, fade(LEAF_DARK, cy + leafOuterYLeft));
        splashDev().fillCircle(cx - leafInnerX, cy + leafInnerYLeft, leafInnerR, fade(LEAF_LIGHT, cy + leafInnerYLeft));
        splashDev().fillCircle(cx + leafOuterX, cy + leafOuterYRight, leafOuterR, fade(LEAF_DARK, cy + leafOuterYRight));
        splashDev().fillCircle(cx + leafInnerX, cy + leafInnerYRight, leafInnerR, fade(LEAF_LIGHT, cy + leafInnerYRight));
    };

#if SPLASH_FLOATING_CARD
    // Four large, faint camellias behind the card, one centred on each corner
    // of the screen, so each shows as a partial bloom framing the card. Drawn
    // before the card, which covers whatever of them reaches it. Scaled to the
    // card's height (the mark is ~68 px tall at scale 1) so they keep their
    // proportion to it in either orientation.
    {
        const float ghostScale = (float)cardH * 1.1f / 68.0f;
        const int corners[4][2] = {
            {0, 0}, {screenW - 1, 0}, {0, screenH - 1}, {screenW - 1, screenH - 1},
        };
        for (const auto &c : corners) {
            drawCamelliaMark(c[0], c[1], ghostScale, 44);
        }
    }
#endif

    splashDev().fillRoundRect(cardX, cardY, cardW, cardH, 12, cardBg);
    splashDev().drawRoundRect(cardX, cardY, cardW, cardH, 12, cardEdge);
    splashDev().drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 12, cardEdgeHi);

    const char *version = APP_VERSION;

    char nodeLine[72];
    const char *nodeLong = s_cfg.nodeLong[0] ? s_cfg.nodeLong : "unknown node";
    const char *nodeShort = s_cfg.nodeShort[0] ? s_cfg.nodeShort : "----";
    snprintf(nodeLine, sizeof(nodeLine), "%s (%s)", nodeLong, nodeShort);

    // A logo from the flasher's Customize dialog takes the flower's place. The
    // box is recorded whether or not one is stored: `cfg caps` reports it so
    // the page can size the image for this device.
    s_splashLogoBg = cardBg;
    auto drawStoredLogo = [&](int x, int y, int w, int h) -> bool {
        s_splashLogoW = max(0, w);
        s_splashLogoH = max(0, h);
        size_t len = 0;
        uint8_t *png = loadStoredLogo(len);
        if (!png) return false;
        int pw = 0, ph = 0;
        bool drawn = false;
        if (pngDimensions(png, len, pw, ph)) {
            const int dx = x + max(0, (w - pw) / 2);
            const int dy = y + max(0, (h - ph) / 2);
            drawn = splashDev().drawPng(png, len, dx, dy, min(pw, w), min(ph, h));
        }
        free(png);
        if (!drawn) Serial.println("[splash] stored logo could not be drawn");
        return drawn;
    };

#if !defined(DEVICE_TLORA_PAGER_TFT) && !UI_CARDPUTER_COMPACT
    // Native-size Roboto GFX fonts (crisp at this size, no bitmap upscaling).
    int splashContentBottom = cardY + 40;
    splashDev().setTextColor(titleCol, cardBg);

    auto drawCentered = [&](const char *text, int y) {
        int w = splashDev().textWidth(text);
        splashDev().drawString(text, cardX + max(0, (cardW - w) / 2), y);
    };

    // Large brand name on top (Roboto Bold 26pt).
    splashDev().setFont(&Roboto_Bold26pt7b);
    splashDev().setTextSize(MY_SPLASH_TITLE_SCALE);
    const int brandY = cardY + 12 + MY_SPLASH_TITLE_Y_OFFSET;
    drawCentered("Camillia", brandY);
    const int brandCap = max(8, (int)splashDev().fontHeight() - MY_SPLASH_SUBTITLE_GAP_TRIM);

    // "for Meshtastic" underneath (Roboto Medium 14pt).
    splashDev().setFont(&Roboto_Medium14pt7b);
    splashDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
    const int subY = brandY + brandCap;
    drawCentered("for Meshtastic", subY);
    splashContentBottom = subY + splashDev().fontHeight();

    // Version tucked under the subtitle in a small face. It moved here from the
    // footer so the footer line can carry the boot progress instead — the
    // version is a thing you glance at once, the progress is what is changing.
    // APP_VERSION already carries its own "v" (the VERSION file reads "v4.1.1"),
    // so this only adds the parentheses.
    {
        char verSmall[32];
        snprintf(verSmall, sizeof(verSmall), "(%s)", version);
        splashDev().setFont(&fonts::DejaVu9);
        splashDev().setTextSize(1.0f);
        splashDev().setTextColor(dimCol, cardBg);
        drawCentered(verSmall, splashContentBottom + 2);
        splashContentBottom += splashDev().fontHeight() + 2;
        splashDev().setTextColor(titleCol, cardBg);
    }
#endif


#if UI_CARDPUTER_COMPACT
    // Cardputer splash, ADV and Zero alike: centered flower with version
    // directly underneath.
    const int flowerCx = cardX + (cardW / 2);
    const int flowerCy = cardY + (cardH / 2) - 10;
    const float cardputerFlowerScale = 0.90f;
    const int flowerBottom = flowerCy + (int)lroundf(36.0f * cardputerFlowerScale);
    // As on the other cards, a logo stands in for the flower.
    const int logoTop = flowerCy - (int)lroundf(32.0f * cardputerFlowerScale);
    if (!drawStoredLogo(cardX + 8, logoTop, cardW - 16, flowerBottom - logoTop)) {
        drawCamelliaMark(flowerCx, flowerCy, cardputerFlowerScale);
    }

    char verLine[72];
    snprintf(verLine, sizeof(verLine), "Version: %s", version);
    splashDev().setFont(&fonts::DejaVu12);
    splashDev().setTextSize(1.0f);
    splashDev().setTextColor(dimCol, cardBg);
    // Center version text between the flower and bottom of the display.
    const int versionBandCenterY = (flowerBottom + screenH) / 2;
    const int versionTopY = versionBandCenterY - (splashDev().fontHeight() / 2);
    int verW = splashDev().textWidth(verLine);
    splashDev().drawString(verLine,
                            cardX + max(0, (cardW - verW) / 2),
                            versionTopY);
#elif defined(DEVICE_TLORA_PAGER_TFT)
    // Pager has much wider horizontal space; use a split layout similar to v1.
    const int contentInset = 12;
    const int paneGap = 10;
    const int contentW = cardW - (contentInset * 2) - paneGap;
    const int leftX = cardX + contentInset;
    const int leftW = max(100, (contentW * 46) / 100);
    const int rightX = leftX + leftW + paneGap;
    const int rightW = max(100, cardX + cardW - contentInset - rightX);

    auto trimTailToFit = [&](const char *src, int maxWidth) -> String {
        String out = src ? String(src) : String();
        if (maxWidth <= 0) return String();
        while (out.length() > 3 && splashDev().textWidth(out.c_str()) > maxWidth) {
            out.remove(out.length() - 1);
        }
        if (splashDev().textWidth(out.c_str()) > maxWidth && out.length() > 3) {
            out.remove(out.length() - 3);
            out += "...";
        }
        return out;
    };

    auto trimHeadToFit = [&](const char *src, int maxWidth) -> String {
        String out = src ? String(src) : String();
        if (maxWidth <= 0) return String();
        while (out.length() > 3 && splashDev().textWidth(out.c_str()) > maxWidth) {
            out.remove(0, 1);
        }
        if (splashDev().textWidth(out.c_str()) > maxWidth && out.length() > 3) {
            out = String("...") + out.substring(3);
        }
        return out;
    };

    // Match pager splash typography to the Roboto title/subtitle treatment,
    // but keep both phrases on a single centered line.
    const int titleY = cardY + 14;
    const int titleGap = 8;

    splashDev().setFont(&Roboto_Bold26pt7b);
    splashDev().setTextSize(MY_SPLASH_TITLE_SCALE);
    const int camilliaW = splashDev().textWidth("Camillia");
    const int camilliaH = splashDev().fontHeight();

    splashDev().setFont(&Roboto_Medium14pt7b);
    splashDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
    const int meshW = splashDev().textWidth("for Meshtastic");
    const int meshH = splashDev().fontHeight();

    const int lineW = camilliaW + titleGap + meshW;
    const int lineX = leftX + max(0, (leftW - lineW) / 2);
    const int meshY = titleY + 3;
    const int titleBlockBottom = max(titleY + camilliaH, meshY + meshH);

    splashDev().setFont(&Roboto_Bold26pt7b);
    splashDev().setTextSize(MY_SPLASH_TITLE_SCALE);
    splashDev().setTextColor(titleCol, cardBg);
    splashDev().drawString("Camillia", lineX, titleY);

    splashDev().setFont(&Roboto_Medium14pt7b);
    splashDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
    splashDev().setTextColor(subCol, cardBg);
    splashDev().drawString("for Meshtastic", lineX + camilliaW + titleGap, meshY);

    const int flowerAreaTop = titleBlockBottom + 8;
    const int flowerAreaBottom = cardY + cardH - 14;
    // A logo from the flasher's Customize dialog takes the flower's pane.
    if (!drawStoredLogo(leftX + 4, flowerAreaTop, leftW - 8, flowerAreaBottom - flowerAreaTop)) {
        float pagerFlowerScale = min((float)(flowerAreaBottom - flowerAreaTop) / 76.0f,
                                     (float)(leftW - 16) / 70.0f);
        if (pagerFlowerScale < 1.10f) pagerFlowerScale = 1.10f;
        if (pagerFlowerScale > 1.70f) pagerFlowerScale = 1.70f;
        drawCamelliaMark(leftX + (leftW / 2),
                         (flowerAreaTop + flowerAreaBottom) / 2,
                         pagerFlowerScale);
    }

    const int rightInfoTop = titleBlockBottom + 6;
    const int dividerTop = rightInfoTop;
    const int dividerBottom = cardY + cardH - 16;
    const int dividerH = max(8, dividerBottom - dividerTop);
    splashDev().drawFastVLine(rightX - (paneGap / 2), dividerTop, dividerH, cardEdgeHi);

    splashDev().setFont(&fonts::DejaVu12);
    splashDev().setTextSize(1.0f);
    splashDev().setTextColor(subCol, cardBg);
    String longLine = trimTailToFit(nodeLong, rightW);
    String shortLine = trimTailToFit((String("(") + nodeShort + ")").c_str(), rightW);
    const int nodeLineY = dividerTop;
    const int nodeLineStep = splashDev().fontHeight() + 4;
    splashDev().drawString(longLine.c_str(), rightX, nodeLineY);
    splashDev().drawString(shortLine.c_str(), rightX, nodeLineY + nodeLineStep);

    char verLine[72];
    snprintf(verLine, sizeof(verLine), "Version: %s", version);
    String verText = trimHeadToFit(verLine, rightW);
    splashDev().setTextColor(dimCol, cardBg);
    const int versionY = dividerBottom - splashDev().fontHeight();
    splashDev().drawString(verText.c_str(), rightX, versionY);
#else
    // Center the flower in the space between the title block and the footer
    // text. A logo from the flasher's Customize dialog takes its place.
    const int flowerBandTop = splashContentBottom;
    const int flowerBandBottom = cardY + cardH - 40;
    if (!drawStoredLogo(cardX + 12, flowerBandTop + 4, cardW - 24, flowerBandBottom - flowerBandTop - 8)) {
        drawCamelliaMark(cardX + (cardW / 2),
                         (flowerBandTop + flowerBandBottom) / 2,
                         flowerScale);
    }

    splashDev().setFont(&fonts::DejaVu12);
    splashDev().setTextSize(1.0f);
    splashDev().setTextColor(subCol, cardBg);
    int nodeW = splashDev().textWidth(nodeLine);
    splashDev().drawString(nodeLine, cardX + max(0, (cardW - nodeW) / 2), cardY + cardH - 36);

    // Where the version line used to sit. The version moved up under the
    // subtitle; this band now reports what boot is doing, updated by
    // bootSplashStatus() as setup() works through it.
    s_splashStatusX  = cardX + 6;
    s_splashStatusW  = max(0, cardW - 12);
    s_splashStatusY  = cardY + cardH - 20;
    s_splashStatusFg = dimCol;
    s_splashStatusBg = cardBg;
    s_splashStatusOn = true;
#endif

#if defined(DEVICE_TDECK_PRO)
    lcd.requestRefresh(true);
    lcd.serviceRefresh(true);
#endif

    // Held, then left on screen. Everything between here and buildUi() — WiFi
    // association, NTP, web config start, the node and channel stores, radio
    // and GPS bring-up — is seconds of work with nothing to show, and this used
    // to black the panel out before any of it ran. Leaving the splash up means
    // the device looks like it is starting rather than like it is off.
    //
    // Nothing repaints the panel in between: LVGL is initialised by now but
    // lv_timer_handler() is not called until loop(), so the last thing drawn
    // stays drawn. buildUi() loads a screen with an opaque background, and the
    // first handler pass paints over the splash a band at a time.
    //
    // Ticked rather than a flat delay so the dots are already moving during the
    // hold, instead of the line sitting still until the first slow step.
    splashPresentAll();
    bootSplashStatus("Starting up");
    const uint32_t holdUntil = millis() + 1200;
    while ((int32_t)(millis() - holdUntil) < 0) {
        bootSplashTick();
        delay(20);
    }
#undef SPLASH_FLOATING_CARD
}

// ── Channel-button paint cache ───────────────────────────────────────────────
// Everything refreshChannelGlow() writes to a channel button, remembered so the
// next pass can skip the writes when nothing about that button changed.
//
// This is not a micro-optimisation. LVGL invalidates on every
// lv_obj_set_style_*() and lv_label_set_text() call whether or not the value
// differs, and lv_obj_set_style_border_width() carries LV_STYLE_PROP_LAYOUT_REFR
// — so re-asserting an unchanged border marked the list's flex layout dirty.
// This function runs every 70 ms, so with the dropdown open (buttons visible,
// invalidations no longer discarded) it was forcing a full re-layout and
// repaint of the channel list seven times more often than anything changed.
// Measured cost: loop() fell from ~55 passes/s to 6-20 while the list was open,
// which starved the keyboard scan in readKey() and is what made Enter feel
// late — and, at 166 ms per pass, sometimes miss entirely.
//
// Buttons that are genuinely pulsing still get written every pass; that is the
// animation. In the common case nothing is unread, nothing pulses, and the
// whole loop becomes a set of comparisons.
struct ChannelGlowPaint {
    lv_obj_t *btn;
    lv_obj_t *lbl;
    bool      valid;
    bool      active;
    bool      animate;
    bool      dropdownCursor;
    uint8_t   uiMode;
    uint8_t   pulseOpa;
    uint8_t   outlineW;
    uint8_t   shadowW;
    char      text[48];
};
static ChannelGlowPaint s_channelGlowPaint[MESH_CHANNELS] = {};

struct SelectorGlowPaint {
    lv_obj_t *btn;
    bool      valid;
    bool      glow;
    bool      navCursor;
    uint8_t   uiMode;
    uint8_t   pulseOpa;
    uint8_t   outlineW;
    uint8_t   shadowW;
};
static SelectorGlowPaint s_selectorGlowPaint = {};

