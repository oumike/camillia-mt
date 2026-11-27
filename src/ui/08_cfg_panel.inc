// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Config panel: action list, focus styles, row handling.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void initCfgActions() {
    s_cfgActionCount = 0;
    // A table-driven row (kCfgGenRows), listed like any other.
    // None on builds without HAS_CFG_EXTRA_ROWS, where web config carries them.
    auto addGen = [](int genId) {
#if HAS_CFG_EXTRA_ROWS
        s_cfgActions[s_cfgActionCount++] = CFG_ACTION_GEN_FIRST + genId;
#else
        (void)genId;
#endif
    };

    // Order here is the order on screen. Grouped by what a person is trying to
    // do rather than by which subsystem owns the setting: identity, then the
    // radios, then what they carry, then how the device looks and sounds, then
    // the mesh modules, then the things you do once or rarely.

    // First row: it is this node's identity — the one setting every other node
    // on the mesh sees — and after onboarding this screen is the only place it
    // can be changed without the web form.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NODE_NAME;
    // Directly under the name for the same reason the name is first: both are
    // what the rest of the mesh sees of this node, and a node on the wrong
    // preset is invisible to it entirely.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_PRESET;
    // Under Preset: the other way to pick one, along with the region and the
    // channels a local mesh uses.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_COMMUNITY_PRESET;
#if FEATURE_LICENSED_MODE
    // With the other "which band, which mesh" choices.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_LICENSED_MODE;
#endif
#if defined(DEVICE_TDISPLAY_P4)
    // Under the preset: the other half of "can this node be heard at all".
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_ANTENNA;
#endif
    // The rest of the radio, finest-grained last: where it may transmit, how
    // loud and how far, what kind of node it is, then the custom modem that
    // replaces the preset, and the identity rows nobody changes twice.
    addGen(CFG_GEN_REGION);
    addGen(CFG_GEN_TX_POWER);
    addGen(CFG_GEN_HOP_LIMIT);
    addGen(CFG_GEN_ROLE);
    addGen(CFG_GEN_CUSTOM_MODEM);
    addGen(CFG_GEN_CUSTOM_BW);
    addGen(CFG_GEN_CUSTOM_SF);
    addGen(CFG_GEN_CUSTOM_CR);
    addGen(CFG_GEN_CUSTOM_SLOT);
    addGen(CFG_GEN_RX_BOOST);
    addGen(CFG_GEN_NODEID_PUBKEY);
    addGen(CFG_GEN_NODEID_OVERRIDE);

    // The radios themselves, each with whatever picks what it talks to. The
    // CardputerZero's are the system's (Wi-Fi, Bluetooth, GPS): it switches and
    // configures them, and Camillia only uses them (applyLoadedConfigInvariants()).
#if !defined(DEVICE_CARDPUTER_ZERO)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WIFI_TOGGLE;
#endif
#if !defined(PLATFORM_LINUX)
    // On Linux the OS picks the network.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHOOSE_WIFI;
#endif
#if defined(DEVICE_CARDPUTER_ZERO)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEBCFG;
#elif HAS_BLE_KEYBOARD
    // Web Config sits with the WiFi pair on these boards instead of down in the
    // services group below. It is what most people turn WiFi on *for*, and
    // picking a network — or AP mode, which switches this row on by itself —
    // reads as unfinished until the row saying where to point a browser is
    // beside it. The two Bluetooth rows then follow, far enough down that the
    // keyboard is no longer sitting between WiFi and the thing it serves.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEBCFG;
    // Same two-row shape as WiFi above -- a toggle, then the picker that
    // chooses what it connects to -- because it is the same job, and someone
    // looking for one row routinely means the other.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_BLE_KBD_TOGGLE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_BLE_KBD_PAIR;
#endif
#if !defined(DEVICE_CARDPUTER_ZERO)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_GPS_TOGGLE;
#endif
#if HAS_FILE_STORAGE
    // Beside the GPS it depends on, so it can be switched on at the roadside
    // without a laptop. Only where there is storage for the log to land on.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WARDRIVE;
#endif

    // Then the services those radios carry, which are useless without them.
#if !HAS_BLE_KEYBOARD && !defined(DEVICE_CARDPUTER_ZERO)
    // Boards with a keyboard row listed it higher up, beside the WiFi pair.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEBCFG;
#endif
#if HAS_VNC_HOST
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_VNC_HOST;
#endif
#if HAS_WEB_FILES
    // Under the web config rows it belongs to: it switches a tab of that page.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEB_FILES;
#endif
    // How web config is reached, under the rows that turn it on.
    addGen(CFG_GEN_WEB_AUTH);
    addGen(CFG_GEN_WEB_PASS);
    addGen(CFG_GEN_WEB_IDLE);
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_MQTT_TOGGLE;
    // The broker the bridge talks to, then what crosses it.
    addGen(CFG_GEN_MQTT_SERVER);
    addGen(CFG_GEN_MQTT_PORT);
    addGen(CFG_GEN_MQTT_USER);
    addGen(CFG_GEN_MQTT_PASS);
    addGen(CFG_GEN_MQTT_ROOT);
    addGen(CFG_GEN_MQTT_ENCRYPT);
    addGen(CFG_GEN_MQTT_MAP_REPORT);
    addGen(CFG_GEN_OK_TO_MQTT);
    addGen(CFG_GEN_IGNORE_MQTT);

    // In the same run as GPS above, because the two are routinely confused:
    // that row picks whether the hardware is one of the position sources, this
    // decides whether any of them leave the device.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SHARE_LOCATION;
    // Immediately after it: this row only means anything while sharing is on,
    // and someone turning sharing off is often really looking for this instead.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_POSITION_PRECISION;
    // How often, how the GPS is read, and the position used without a fix.
    addGen(CFG_GEN_POS_INTERVAL);
#if !defined(DEVICE_CARDPUTER_ZERO)
    addGen(CFG_GEN_GPS_POLL);
    addGen(CFG_GEN_GPS_DUTY);
#endif
    addGen(CFG_GEN_FIXED_LAT);
    addGen(CFG_GEN_FIXED_LON);
    addGen(CFG_GEN_FIXED_ALT);

    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHANNEL_CFG;
    // After the channels: like them, it decides what this node hears.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SIG_POLICY;

    // ── How it looks ─────────────────────────────────────────────────────────
#if HAS_UI_THEMES
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_THEME;
#endif
    // All builds get the full set — the Cardputer's bubble/outline styles render
    // fine on its 240x135 panel.
#if HAS_CHAT_STYLE_OPTIONS
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHAT_STYLE;
#endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHAT_NAMES;
#if HAS_UI_COLOR_OPTIONS
    // The three colour rows in one run: what other senders look like, what you
    // look like, and the reset that reassigns the first.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHAT_COLORS;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_OWNER_COLOR;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_RESET_CHAT_COLORS;
#endif
    // Font Size scales the chat and DM text in every style, so every build gets
    // it — the Cardputer arguably most of all.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_FONT_SIZE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_BRIGHTNESS;
    #if HAS_POWER_LED_LEVEL
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_POWER_LED_LEVEL;
    #endif
    #if HAS_KB_BACKLIGHT_LEVEL
    // Next to the screen brightness, because they are the same question asked
    // about the two lit surfaces this board has.
    #if defined(DEVICE_TDISPLAY_P4)
    // The P4's keyboard is an expansion that comes and goes; with it off there
    // is no light to ask about. Read when the list is built, i.e. each time
    // Settings opens.
    if (keyboardAttached())
    #endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_KB_BACKLIGHT_LEVEL;
    #endif
    #if HAS_KB_LIGHT_TIMEOUT
    // Under the light it times out, gated the same way on the P4.
    #if defined(DEVICE_TDISPLAY_P4)
    if (keyboardAttached())
    #endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_KB_LIGHT_TIMEOUT;
    #endif
    // Decides what the bottom of every screen looks like; set once to taste.
    #if HAS_NAV_BAR_TOGGLE
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NAV_BAR;
    #endif
    #if HAS_CHANNEL_PANEL_LOCK
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHANNEL_PANEL_LOCK;
    #endif
    #if HAS_NAV_ALIGN_SETTING
    // Every placement choice in one row: bottom row or a column down either
    // side. Listed with the bar switched off too, so it can be set ahead.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NAV_ALIGN;
    #endif
    // With Brightness rather than the maintenance actions: both are about the
    // backlight, and the pair is the usual reason someone opens this screen on
    // a device running on a battery.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SCREEN_TIMEOUT;
    #if FEATURE_LOCK_SCREEN
    // Directly under Screen Timeout, because that is the timeout that leads
    // here: the three rows read top to bottom as the whole story of what the
    // panel does when it is left alone. The dwell row stays listed while the
    // lock screen is off — greying rows out is not a pattern this screen has,
    // and the row above it already says why it does nothing.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_LOCK_SCREEN;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_LOCK_SCREEN_OFF;
    #endif
    #if HAS_RUNTIME_ORIENTATION
    // Below the lock-screen pair, at the end of the run of rows about what the
    // panel itself does. It is also the only row here that reboots, which is a
    // reason to keep it off the path someone scrolls through to reach the rest.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_ORIENTATION;
    #endif
    // Beside Orientation: the other row here that reboots, and a whole-UI
    // setting of the same kind. Absent from English-only builds.
    #if I18N_ENABLED
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_LANGUAGE;
    #endif
    // How typing behaves, next to the language it depends on: the suggestions
    // are English, and only offered while the UI is.
    #if HAS_SPELLCHECK
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SPELL_CHECK;
    #endif
    // A comfort setting, changed once to taste. Only on boards with a trackball
    // to invert.
    #if HAS_SCROLL_INVERT
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_INVERT_SCROLL;
    #endif

    // ── How it gets your attention ───────────────────────────────────────────
    #if HAS_VOLUME_CONTROL
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_VOLUME;
    #endif
    // Both play a tone, so both are absent where nothing can play one — the
    // rows would otherwise be settings for hardware the board does not have.
    #if HAS_AUDIO_ALERTS
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SPLASH_MELODY;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_MSG_ALERT;
    #endif
    // The touch half of the same group: what a new message buzzes, if anything.
    #if HAS_HAPTIC
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_VIBRATION;
    #endif
    // The light half of the same group. Absent on boards with no LED wired.
    #if HAS_NOTIFY_LED
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NOTIFY_LED_CHANNEL_COLOR;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NOTIFY_LED_DM_COLOR;
    #endif
    // The same job as those LED rows on the two boards whose only notification
    // light is the keyboard. Absent where no keyboard backlight can be driven.
    #if HAS_KB_BLINK
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_KB_BLINK;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_KB_BLINK_CHAN_FLASHES;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_KB_BLINK_DM_FLASHES;
    #endif
    // Last of the notification group, and it governs whichever light this board
    // has — the LED rows above on the Mesh Deck, the blink rows on the other two.
    #if HAS_LIGHT_NOTIFY
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NOTIFY_LIGHT_TIMEOUT;
    #endif

    // ── How it reads things out ──────────────────────────────────────────────
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_TIME_DATE;
    // Directly under the row that sets the clock: the two questions a person
    // asks about time in a row, what it is and how it is written.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CLOCK_FORMAT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_UNITS;
    // Unconditional: every supported board reads a battery voltage, so there is
    // no board this row would have nothing to show.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_BATT_DISPLAY;
    // Where the clock comes from, and how the device runs unattended.
    addGen(CFG_GEN_TIMEZONE);
    addGen(CFG_GEN_NTP_SERVER);
    addGen(CFG_GEN_POWER_SAVING);
    addGen(CFG_GEN_DEBUG_LOG);

    // ── Mesh modules ─────────────────────────────────────────────────────────
    // What this node relays and announces, then the modules.
    addGen(CFG_GEN_REBROADCAST);
    addGen(CFG_GEN_NODEINFO_INTERVAL);
    addGen(CFG_GEN_TEL_DEVICE);
#if HAS_ENV_SENSOR_TELEMETRY
    addGen(CFG_GEN_TEL_ENV);
#endif
    addGen(CFG_GEN_TEL_INTERVAL);
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NEIGHBOR_INFO;
    addGen(CFG_GEN_NEIGHBOR_INTERVAL);
    addGen(CFG_GEN_NEIGHBOR_LORA);
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_MESH_BEACON;
    // camillia chat server: mode, the node it uses (activating clears it; a node
    // is set from Nodes -> Chat Server), and a manual catch-up.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CS_MODE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CS_SERVER;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CS_CHECK;
    addGen(CFG_GEN_AUTOFAV);
    addGen(CFG_GEN_AUTOFAV_RANGE);
    // The two HTTP services Tools reaches out to.
#if HAS_NODE_LOS
    addGen(CFG_GEN_LOS_SERVER);
#endif
#if HAS_WEATHER
    addGen(CFG_GEN_WEATHER_SERVER);
#endif

    // ── Done once, or rarely ─────────────────────────────────────────────────
    // Hardware trim: set once per unit rather than adjusted, which is why it is
    // down here and not up among the display settings.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_BATT_CAL;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CLEAR_MSGS;
#if HAS_NODE_ARCHIVE
    // In the order the two settings depend on each other: keep the evicted
    // nodes, then show them. Only on boards that can keep the archive (an SD
    // slot, or the CardputerZero's disk) — elsewhere these would toggle
    // settings with nothing behind them.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_ARCHIVE_NODES;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SHOW_ARCHIVED;
#endif
    // One row; its confirmation carries the "include favorites" and "include
    // archived" options (cfgClearNodesAddOptions()).
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CLEAR_NODES;
// Every real SD-card target gets firmware-side config transfer. Preserve the
// Mesh Deck's existing internal-flash transfer path; Heltec remains excluded.
#if HAS_SD_CARD || (HAS_FILE_STORAGE && !defined(DEVICE_HELTEC_V4_EXPANSION))
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_EXPORT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_IMPORT;
#endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_FACTORY_RESET;
#if HAS_SD_MALWARE_SCAN
    // Under Factory Reset, with the other things that act on stored data rather
    // than on a setting.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SD_SCAN;
#endif

    // ── Firmware ─────────────────────────────────────────────────────────────
    // The CardputerZero app is updated through M5Stack's app store instead.
#if !defined(DEVICE_CARDPUTER_LORA_HAT) && !defined(DEVICE_CARDPUTER_ZERO)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_OTA_UPDATE;
    // Directly under the update action it governs: it decides what that check
    // will find. Absent on the Cardputer for the same reason the update row is
    // — a channel setting for an update path that does not exist is inert.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_OTA_CHANNEL;
    addGen(CFG_GEN_OTA_BOOT_CHECK);
    addGen(CFG_GEN_OTA_AUTO_UPDATE);
#endif
    // Under the update entries it describes. Present on every board — the notes
    // are baked into the image, so this needs neither OTA nor WiFi, and the
    // Cardputer (no OTA) still gets to see what its build contains.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_RELEASE_NOTES;

    // Narrow to the rows matching the filter, in place and last so the whole
    // list above stays a plain statement of what exists and in what order —
    // every #if there already says enough without a filter test as well.
    //
    // Matched against the rendered label, so "wifi" finds "WiFi: On (...)" and
    // typing part of a *value* works too: the label is what is on screen, and
    // matching anything else would look broken.
    if (s_cfgFilterOpen && s_cfgFilterLen > 0) {
        int kept = 0;
        for (int i = 0; i < s_cfgActionCount; i++) {
            char label[80];
            cfgActionLabel(s_cfgActions[i], label, sizeof(label));
            if (!dmNodePickerContainsNoCase(label, s_cfgFilter)) continue;
            s_cfgActions[kept++] = s_cfgActions[i];
        }
        s_cfgActionCount = kept;
    }
#if UI_CFG_SECTIONS
    else {
        // The grid lists the sections this build has rows for, read off the
        // full list before it is narrowed to the open one.
        bool present[CFG_SEC__COUNT] = {};
        for (int i = 0; i < s_cfgActionCount; i++) present[cfgActionSection(s_cfgActions[i])] = true;
        s_cfgGridCount = 0;
        for (int sec = 0; sec < CFG_SEC__COUNT; sec++) {
            if (present[sec]) s_cfgGridSections[s_cfgGridCount++] = (int8_t)sec;
        }
        if (s_cfgGridSelection >= s_cfgGridCount) s_cfgGridSelection = 0;

        if (s_cfgSection >= 0 && !s_cfgFilterOpen) {
            int kept = 0;
            for (int i = 0; i < s_cfgActionCount; i++) {
                if (cfgActionSection(s_cfgActions[i]) == s_cfgSection) s_cfgActions[kept++] = s_cfgActions[i];
            }
            s_cfgActionCount = kept;
        }
    }
#endif
}

static void refreshCfgPanelFocusStyles() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (!s_cfgActionList || !s_cfgInfoList) return;

    const lv_color_t activeBorder = lv_color_hex(0x8FB5E6);
    const lv_color_t inactiveBorder = lv_color_hex(0x335D9D);

    lv_obj_set_style_border_width(s_cfgActionList, s_cfgInfoPanelFocused ? 1 : 2, 0);
    lv_obj_set_style_border_color(s_cfgActionList, s_cfgInfoPanelFocused ? inactiveBorder : activeBorder, 0);

    lv_obj_set_style_border_width(s_cfgInfoList, s_cfgInfoPanelFocused ? 2 : 1, 0);
    lv_obj_set_style_border_color(s_cfgInfoList, s_cfgInfoPanelFocused ? activeBorder : inactiveBorder, 0);
#endif
}

// Fills info[] with the current node's identity/radio details and returns the
// line count. Shared by the pager's always-on info panel and the (I)nformation
// popup on the other builds, so the two never drift apart.
// Render a node's last-heard time as a compact local timestamp. lastHeardMs is a
// millis() stamp, so it needs the wall clock to become a date; before NTP has
// set the clock we fall back to a relative age rather than printing 1970.
static void deviceInfoFormatHeard(uint32_t lastHeardMs, char *out, size_t outLen) {
    const uint32_t nowMs = millis();
    if (lastHeardMs == 0 || nowMs < lastHeardMs) {
        snprintf(out, outLen, "?");
        return;
    }
    const uint32_t ageS = (nowMs - lastHeardMs) / 1000UL;
    const time_t nowEpoch = time(nullptr);
    if (nowEpoch > 1700000000) {
        time_t t = nowEpoch - (time_t)ageS;
        struct tm lt;
        localtime_r(&t, &lt);
        char clock[LIVE_CLOCK_BUF];
        liveFormatClock(lt, clock, sizeof(clock));
        snprintf(out, outLen, "%02d/%02d %s",
                 lt.tm_mon + 1, lt.tm_mday, clock);
    } else if (ageS < 3600UL) {
        snprintf(out, outLen, TR("%lum ago"), (unsigned long)(ageS / 60UL));
    } else {
        snprintf(out, outLen, TR("%luh ago"), (unsigned long)(ageS / 3600UL));
    }
}

static const char *deviceInfoNodeLabel(const NodeEntry *e, char *buf, size_t len) {
    if (liveShortNameUsable(e->shortName)) return e->shortName;
    snprintf(buf, len, "!%08lx", (unsigned long)e->nodeId);
    return buf;
}

// "29.7 GB" / "512 MB" / "1.5 MB" — whichever unit keeps the number readable.
static void deviceInfoFormatBytes(uint64_t bytes, char *out, size_t cap) {
    // snprintf rather than lv_label_set_text_fmt for the same reason as
    // everywhere else in this file: LVGL's printf has no float support here.
    if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
        snprintf(out, cap, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= (1024ULL * 1024ULL)) {
        snprintf(out, cap, "%.0f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(out, cap, "%lu KB", (unsigned long)(bytes / 1024ULL));
    }
}

// How many rows buildDeviceInfoLines() can produce. One number, because two
// callers size an array from it and a stale copy in either is a silently
// truncated screen.
static constexpr int kDeviceInfoMaxLines = 18;   // 16 worst case, +2 licensed

static int buildDeviceInfoLines(char info[][96], int maxLines) {
    int n = 0;
    bool hasPubKey = false;
    for (int i = 0; i < 32; i++) {
        if (myPubKey[i] != 0) { hasPubKey = true; break; }
    }
    if (n < maxLines) snprintf(info[n++], 96, TR("Firmware: %s"), APP_VERSION);
    if (n < maxLines) snprintf(info[n++], 96, TR("Node ID: !%08lx"), (unsigned long)s_myNodeId);
    // A TRACKER that is not sharing location transmits no position at all, which
    // is the whole of the role — and nothing else on the device says so, since
    // Share Location looks perfectly reasonable on its own. Called out here
    // rather than refused at the point of setting: the two are set on different
    // screens, in either order, so refusing one would just be a race.
    if (n < maxLines) {
        const bool trackerNotSharing =
            (s_cfg.deviceRole == 5 /*TRACKER*/) && !s_cfg.shareLocation;
        // Licensed mode pins rebroadcast to LOCAL_ONLY whatever the role; there
        // is no role or rebroadcast row on the device, so this line says it.
        snprintf(info[n++], 96, TR("Role: %s%s"), cfgDeviceRoleName(s_cfg.deviceRole),
                 lic::active() ? TR(" (licensed: local only)")
                 : trackerNotSharing ? TR(" (not sharing location)") : "");
    }
    if (n < maxLines) snprintf(info[n++], 96, TR("PKI key: %s"), hasPubKey ? TR("present") : TR("missing"));
    if (n < maxLines) snprintf(info[n++], 96, TR("Long: %s"), s_cfg.nodeLong);
    if (n < maxLines) snprintf(info[n++], 96, TR("Short: %s"), s_cfg.nodeShort);
    if (n < maxLines) snprintf(info[n++], 96, TR("Preset: %s"),
                               s_cfg.loraUsePreset
                                   ? kPresets[s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : 0].name
                                   : TR("Custom"));
    if (n < maxLines) snprintf(info[n++], 96, TR("Freq: %.3f MHz"), s_cfg.loraFreq);
    // One decimal: the custom bandwidths include 62.5 and 31.25 kHz, which the
    // old %.0f rendered as a plain "62" — the same digits as the config code,
    // but not the bandwidth the radio is actually running.
    if (n < maxLines) snprintf(info[n++], 96, "BW %.4g SF %d CR 4/%d",
                               (double)s_cfg.loraBw, s_cfg.loraSf, s_cfg.loraCr);
    if (n < maxLines) snprintf(info[n++], 96, TR("Pwr %d dBm Hops %d"), s_cfg.loraPower, s_cfg.loraHopLimit);
    if (n < maxLines) snprintf(info[n++], 96, TR("Relayed: %lu"), (unsigned long)s_rebroadcastCount);
    if (n < maxLines) snprintf(info[n++], 96, TR("Stood down: %lu"), (unsigned long)s_relayStoodDownCount);
    // The station-ID NodeInfo interval licensed mode fixes; not settable here.
    if (lic::active() && n < maxLines) {
        snprintf(info[n++], 96, "%s", TR("NodeInfo (licensed: 10 min)"));
    }
    if (lic::active() && n < maxLines) {
        snprintf(info[n++], 96, TR("Lic drops %lu / NI mismatch %lu"),
                 (unsigned long)s_licDropUnlicensed, (unsigned long)s_licNodeInfoMismatch);
    }

#if HAS_FILE_STORAGE
    // Storage, and on a board with a slot, what the last mount attempt made of
    // it. A device that has quietly stopped archiving nodes, writing DM history
    // or taking an export looks exactly like one that never had a card, and
    // until now the only place that difference was stated was the serial log —
    // which needs a cable, and a reboot to read from the start.
    {
        SdProbeStatus sd;
        sdGetProbeStatus(sd);
        char sizeBuf[16];
        deviceInfoFormatBytes(storageTotalBytes(), sizeBuf, sizeof(sizeBuf));

        if (!sd.hasSlot) {
            // No slot to diagnose: this board keeps the same files in internal
            // flash, and "is the card in" is not a question it can be asked.
            if (n < maxLines) {
                if (sdCardMounted()) {
                    snprintf(info[n++], 96, TR("Storage: %s %s"), storageName(), sizeBuf);
                } else {
                    snprintf(info[n++], 96, TR("Storage: %s (not mounted)"), storageName());
                }
            }
        } else if (sd.mounted) {
            const char *type = storageCardTypeName();
            if (n < maxLines) {
                if (sd.mountedHz > 0) {
                    // The clock it answered at is the useful half: a card that
                    // mounts only at 400 kHz is working but marginal, and that
                    // is worth seeing before it starts failing writes.
                    snprintf(info[n++], 96, TR("SD: %s %s @%lu kHz"),
                             type[0] ? type : "card", sizeBuf,
                             (unsigned long)(sd.mountedHz / 1000UL));
                } else {
                    snprintf(info[n++], 96, "SD: %s %s", type[0] ? type : "card", sizeBuf);
                }
            }
        } else {
#if defined(HAS_SD_SOFT_SPI) && HAS_SD_SOFT_SPI
            if (n < maxLines) snprintf(info[n++], 96, TR("SD: not mounted"));
            if (n < maxLines) {
                if (storageMounted()) {
                    snprintf(info[n++], 96, TR("Storage: %s %s"), storageName(), sizeBuf);
                } else {
                    snprintf(info[n++], 96, TR("Storage: %s (not mounted)"), storageName());
                }
            }
#else
            if (n < maxLines) snprintf(info[n++], 96, TR("SD: no card"));
            if (n < maxLines) {
                char when[16];
                if (sd.retryInMs > 0) {
                    snprintf(when, sizeof(when), TR("retry %lus"),
                             (unsigned long)((sd.retryInMs + 999UL) / 1000UL));
                } else {
                    snprintf(when, sizeof(when), TR("retry now"));
                }
#if defined(HAS_SD_MMC) && HAS_SD_MMC
                snprintf(info[n++], 96, "SD: %s, %u fails", when, (unsigned)sd.failStreak);
#else
                // Which rates were tried separates the two failures that matter:
                // nothing answering after the whole ladder says empty slot or
                // dead card, while a single-rate miss is just the damped retry
                // and says nothing yet.
                snprintf(info[n++], 96, TR("SD: %s, %s, %u fails"),
                         sd.triedAllSpeeds ? TR("all rates tried") : TR("one rate tried"),
                         when, (unsigned)sd.failStreak);
#endif
            }
#endif
        }
    }
#endif

    // Most / least recently heard node. Entries restored from NVS have
    // lastHeardMs == 0 (unknown after reboot), so only nodes actually heard
    // since boot are candidates.
    const NodeEntry *newest = nullptr;
    const NodeEntry *oldest = nullptr;
    const int nodeCount = Nodes.count();
    for (int i = 0; i < nodeCount; i++) {
        NodeEntry *e = Nodes.getByRank(i);
        if (!e || e->nodeId == 0 || e->lastHeardMs == 0) continue;
        if (!newest || e->lastHeardMs > newest->lastHeardMs) newest = e;
        if (!oldest || e->lastHeardMs < oldest->lastHeardMs) oldest = e;
    }
    if (n < maxLines) {
        if (newest) {
            char idBuf[12], when[24];
            deviceInfoFormatHeard(newest->lastHeardMs, when, sizeof(when));
            snprintf(info[n++], 96, TR("Newest: %s %s"),
                     deviceInfoNodeLabel(newest, idBuf, sizeof(idBuf)), when);
        } else {
            snprintf(info[n++], 96, TR("Newest: none heard yet"));
        }
    }
    if (n < maxLines) {
        if (oldest) {
            char idBuf[12], when[24];
            deviceInfoFormatHeard(oldest->lastHeardMs, when, sizeof(when));
            snprintf(info[n++], 96, TR("Oldest: %s %s"),
                     deviceInfoNodeLabel(oldest, idBuf, sizeof(idBuf)), when);
        } else {
            snprintf(info[n++], 96, TR("Oldest: none heard yet"));
        }
    }
    return n;
}

static lv_color_t cfgContrastColorFor565(uint16_t c) {
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
    // Relative luminance approximation for robust light/dark contrast choice.
    uint16_t luma = (uint16_t)((30U * r + 59U * g + 11U * b) / 100U);
    return (luma >= 128U) ? lv_color_make(0x00, 0x00, 0x00)
                          : lv_color_make(0xFF, 0xFF, 0xFF);
}

// Paints one row for its current state. Every visual property a row can carry
// is written on every call — nothing is left to whatever the row happened to
// look like before — so this can repaint an existing row rather than only
// dress a freshly built one.
static void cfgStyleRow(lv_obj_t *row, int i, bool disabled, bool selected) {
    if (!row) return;
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(i);
    lv_obj_set_style_text_color(row, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_opa(row, disabled ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, selected ? 2 : 0, 0);
    lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
    return;
#endif
    const lv_color_t selectionOutlineColor = cfgContrastColorFor565(s_ui.selectBg);
    const lv_color_t selectionBgColor = lvColorFrom565(s_ui.selectBg);
    const lv_color_t selectionTextColor = cfgContrastColorFor565(s_ui.selectBg);
    const lv_opa_t selectionBgOpa = (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_80;
    const bool oddRow = (i & 1) != 0;

    if (disabled) {
        // Muted, and never drawn selected: keep the alternating-row hint only.
        lv_obj_set_style_text_color(row, lv_color_hex(0x5A6B85), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
        lv_obj_set_style_bg_opa(row, oddRow ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_outline_width(row, 0, 0);
        lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
        return;
    }
    if (selected) {
        lv_obj_set_style_text_color(row, selectionTextColor, 0);
        lv_obj_set_style_bg_color(row, selectionBgColor, 0);
        lv_obj_set_style_bg_opa(row, selectionBgOpa, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, selectionOutlineColor, 0);
        lv_obj_set_style_outline_width(row, 1, 0);
        lv_obj_set_style_outline_color(row, selectionOutlineColor, 0);
        lv_obj_set_style_outline_pad(row, 0, 0);
        lv_obj_set_style_outline_opa(row, LV_OPA_70, 0);
        return;
    }
    lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(row, oddRow ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
}

// Moving the selection changes which row is highlighted and nothing else, so it
// repaints the existing rows instead of going through refreshCfgModal().
//
// That full rebuild deletes and recreates every row label — around fifty of
// them, each with a cfgActionLabel() call behind it, and on the Pager it also
// wipes the info panel and re-runs buildDeviceInfoLines(), which walks the
// whole node table. Doing all that per keypress is what made holding a
// direction key on the Pager feel like wading.
// Index whose row currently carries the highlight, so moving it only has to
// repaint two rows. -1 when the list has just been rebuilt or torn down.
static int s_cfgPaintedSelection = -1;

static void refreshCfgSelection() {
    if (!s_cfgActionList) return;
#if UI_CFG_SECTIONS
    // The grid's buttons are not rows, however many of them there are.
    if (cfgGridShown()) return;
#endif
    const int rows = (int)lv_obj_get_child_cnt(s_cfgActionList);
    // Out of step with the model — the list has not been built for the current
    // action set. Nothing to repaint; the next full refresh will do it.
    if (rows != s_cfgActionCount) return;

    // Rows carry live values ("WiFi: On (connecting ...)"), and before this
    // split they were re-rendered on every selection move. Keep that, but only
    // touch a label whose text actually changed — an unchanged set_text still
    // invalidates the row and would redraw the whole list per keypress.
    for (int i = 0; i < rows; i++) {
        lv_obj_t *row = lv_obj_get_child(s_cfgActionList, i);
        if (!row) continue;
        char rowText[80];
        cfgActionLabel(s_cfgActions[i], rowText, sizeof(rowText));
        const char *shown = lv_label_get_text(row);
        if (!shown || strcmp(shown, rowText) != 0) lv_label_set_text(row, rowText);
    }

    auto paint = [&](int i) {
        if (i < 0 || i >= rows) return (lv_obj_t *)nullptr;
        lv_obj_t *row = lv_obj_get_child(s_cfgActionList, i);
        if (!row) return (lv_obj_t *)nullptr;
        const bool disabled = cfgActionDisabled(s_cfgActions[i]);
        const bool selected = (i == s_cfgSelection) && !disabled;
        cfgStyleRow(row, i, disabled, selected);
        return selected ? row : (lv_obj_t *)nullptr;
    };

    // Only the row losing the highlight and the one gaining it change
    // appearance. Restyling all of them would invalidate the whole list and
    // redraw it on every keypress, which is most of what made this slow.
    if (s_cfgPaintedSelection != s_cfgSelection) paint(s_cfgPaintedSelection);
    lv_obj_t *selectedRowObj = paint(s_cfgSelection);
    s_cfgPaintedSelection = s_cfgSelection;

    if (selectedRowObj) lv_obj_scroll_to_view(selectedRowObj, LV_ANIM_OFF);
}

#if UI_CFG_SECTIONS
static void cfgStyleGridBtn(lv_obj_t *btn, bool selected) {
    if (!btn) return;
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
#if defined(DEVICE_TDECK_PRO)
    // Black on white, as the rows are on this panel: the highlight is a
    // heavier frame rather than a fill.
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
    return;
#endif
    if (selected) {
        // The rows' highlight, so a button and a row picked by the keyboard
        // look the same.
        cfgStyleRow(btn, 0, false, true);
        if (lbl) lv_obj_set_style_text_color(lbl, cfgContrastColorFor565(s_ui.selectBg), 0);
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xD9E8FF), 0);
}

static void refreshCfgGridSelection() {
    if (!s_cfgActionList || !cfgGridShown()) return;
    const int n = (int)lv_obj_get_child_cnt(s_cfgActionList);
    if (n != s_cfgGridCount) return;   // not the grid's buttons
    lv_obj_t *selectedBtn = nullptr;
    for (int i = 0; i < n; i++) {
        lv_obj_t *btn = lv_obj_get_child(s_cfgActionList, i);
        cfgStyleGridBtn(btn, i == s_cfgGridSelection);
        if (i == s_cfgGridSelection) selectedBtn = btn;
    }
    if (selectedBtn) lv_obj_scroll_to_view(selectedBtn, LV_ANIM_OFF);
}

// Deferred: the click arrives on a grid button, and opening the section
// rebuilds the list that button lives in.
static void cfgOpenSectionAsync(void *arg) {
    if (!s_cfgModal) return;
    cfgOpenSection((int)(intptr_t)arg);
}

static void onCfgSectionBtnClicked(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_cfgGridCount) return;
    s_cfgGridSelection = idx;
    lv_async_call(cfgOpenSectionAsync, (void *)(intptr_t)s_cfgGridSections[idx]);
}

static void cfgShowGridAsync(void *) {
    if (!s_cfgModal) return;
    cfgShowGrid();
}

static void onCfgBackPressed(lv_event_t *e) {
    LV_UNUSED(e);
    lv_async_call(cfgShowGridAsync, nullptr);
}

static void cfgOpenSection(int sec) {
    if (sec < 0 || sec >= CFG_SEC__COUNT) return;
    s_cfgSection = sec;
    s_cfgSelection = 0;
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    s_cfgPaintedSelection = -1;
    initCfgActions();
    refreshCfgModal();
    // A long section may have been scrolled before; start at its top.
    if (s_cfgActionList) lv_obj_scroll_to_y(s_cfgActionList, 0, LV_ANIM_OFF);
}

static void cfgShowGrid() {
    const int left = s_cfgSection;
    s_cfgSection = -1;
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    initCfgActions();
    // Back on the button of the section just left, so the keyboard picks up
    // where it was.
    for (int i = 0; i < s_cfgGridCount; i++) {
        if (s_cfgGridSections[i] == left) { s_cfgGridSelection = i; break; }
    }
    refreshCfgModal();
}

// The grid itself: one button per section, in the list's place. Three to a
// row on a screen at least 600 px wide (the CrowPanel 5.0, the P4 in
// landscape), two below that.
static void buildCfgGrid() {
#if defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_CROWPANEL)
    const lv_font_t *font = &lv_font_montserrat_16;
    const int btnH = 72;
    const int btnPad = 4;
#elif UI_CARDPUTER_COMPACT || defined(DEVICE_TLORA_PAGER_TFT)
    // 135 px of height on the Cardputer, and the Pager's list is the left 58%
    // of a 222 px-tall screen: a smaller font, so a wrapped name still fits.
    const lv_font_t *font = &lv_font_montserrat_12;
    const int btnH = 44;
    const int btnPad = 2;
#else
    const lv_font_t *font = &lv_font_montserrat_14;
    const int btnH = 52;
    const int btnPad = 4;
#endif
    const int cols = (lv_disp_get_hor_res(NULL) >= 600) ? 3 : 2;
    s_cfgGridCols = cols;
    // Just under 1/cols: SPACE_EVENLY shares out what is left between and
    // around the buttons, and exact fractions would wrap one to the next row.
    const int btnPct = (cols == 3) ? 31 : 47;

    lv_obj_set_flex_flow(s_cfgActionList, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_cfgActionList, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_cfgActionList, 4, 0);
    lv_obj_set_style_pad_row(s_cfgActionList, 4, 0);

    for (int i = 0; i < s_cfgGridCount; i++) {
        const CfgSectionInfo &info = kCfgSections[s_cfgGridSections[i]];
        lv_obj_t *btn = lv_btn_create(s_cfgActionList);
        lv_obj_set_size(btn, lv_pct(btnPct), btnH);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, btnPad, 0);
        lv_obj_add_event_cb(btn, onCfgSectionBtnClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        char text[64];
        snprintf(text, sizeof(text), "%s\n%s", info.icon, TR(info.name));
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);

        cfgStyleGridBtn(btn, i == s_cfgGridSelection);
    }
}
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
// The Pager's always-on Device Info panel, right of the rows or the grid.
static void fillCfgPagerInfo() {
    const lv_font_t *cfgRowFont = &lv_font_montserrat_12;
    const int cfgPadTop = 3;
    const int cfgPadBottom = 3;
    char info[kDeviceInfoMaxLines][96] = {};
    int infoCount = buildDeviceInfoLines(info, kDeviceInfoMaxLines);

    lv_obj_t *infoHeader = lv_label_create(s_cfgInfoList);
    lv_obj_set_width(infoHeader, lv_pct(100));
    lv_obj_set_style_text_font(infoHeader, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(infoHeader, lv_color_hex(0xBFD6FF), 0);
    lv_obj_set_style_bg_color(infoHeader, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(infoHeader, LV_OPA_70, 0);
    lv_obj_set_style_pad_left(infoHeader, 4, 0);
    lv_obj_set_style_pad_right(infoHeader, 4, 0);
    lv_obj_set_style_pad_top(infoHeader, 3, 0);
    lv_obj_set_style_pad_bottom(infoHeader, 3, 0);
    lv_label_set_long_mode(infoHeader, LV_LABEL_LONG_DOT);
    lv_label_set_text(infoHeader, TR("Device Info"));

    for (int i = 0; i < infoCount; i++) {
        lv_obj_t *row = lv_label_create(s_cfgInfoList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, cfgRowFont, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_bg_opa(row, (i & 1) ? LV_OPA_30 : LV_OPA_10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, cfgPadTop, 0);
        lv_obj_set_style_pad_bottom(row, cfgPadBottom, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, info[i]);
    }
}
#endif

static void refreshCfgModal() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (!s_cfgModal || !s_cfgActionList || !s_cfgInfoList || !s_cfgHeaderStatus) return;
#else
    if (!s_cfgModal || !s_cfgActionList || !s_cfgHeaderStatus) return;
#endif

    if (s_cfgActionCount <= 0) {
        initCfgActions();
    }
    if (s_cfgSelection < 0) s_cfgSelection = 0;
    if (s_cfgSelection >= s_cfgActionCount) s_cfgSelection = s_cfgActionCount - 1;

    if (s_cfgFilterOpen) {
        // Brackets show as soon as the filter is armed, even while empty, so
        // there is a visible reason the list just changed. Same cue as Nodes,
        // and in the same place: after the screen's name in its header.
        char filterText[48];
        if (s_cfgActionCount == 0) {
            snprintf(filterText, sizeof(filterText), TR("[%s] no match"), s_cfgFilter);
        } else {
            snprintf(filterText, sizeof(filterText), "[%s] %d", s_cfgFilter, s_cfgActionCount);
        }
        char headerText[96];
        snprintf(headerText, sizeof(headerText), "%s %s", TR("Configuration"), filterText);
        screenHeaderSetTitle(s_cfgHeaderStatus, headerText);
    } else {
#if UI_CFG_SECTIONS
        // Inside a section its name is the title; the grid is "Configuration".
        screenHeaderSetTitle(s_cfgHeaderStatus,
                             (s_cfgSection >= 0) ? TR(kCfgSections[s_cfgSection].name)
                                                 : TR("Configuration"));
#else
        screenHeaderSetTitle(s_cfgHeaderStatus, TR("Configuration"));
#endif
    }

    lv_obj_clean(s_cfgActionList);
#if UI_CFG_SECTIONS
    if (lvObjValid(s_cfgBackBtn)) {
        if (cfgGridShown()) lv_obj_add_flag(s_cfgBackBtn, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_clear_flag(s_cfgBackBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (lvObjValid(s_cfgInfoBtn)) {
        if (cfgGridShown()) lv_obj_clear_flag(s_cfgInfoBtn, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(s_cfgInfoBtn, LV_OBJ_FLAG_HIDDEN);
    }
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_clean(s_cfgInfoList);
    refreshCfgPanelFocusStyles();
#endif
    s_cfgPaintedSelection = -1;
    if (cfgGridShown()) {
        buildCfgGrid();
#if defined(DEVICE_TLORA_PAGER_TFT)
        fillCfgPagerInfo();
#endif
        return;
    }
    // A list again: undo what buildCfgGrid() set on the same container.
    lv_obj_set_flex_flow(s_cfgActionList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_cfgActionList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgActionList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgActionList, 2, 0);
#endif
    lv_obj_t *selectedRowObj = nullptr;

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *cfgRowFont = &lv_font_montserrat_12;
    const int cfgPadTop = 3;
    const int cfgPadBottom = 3;
#elif defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO)
    const lv_font_t *cfgRowFont = &lv_font_montserrat_14;
    const int cfgPadTop = 4;
    const int cfgPadBottom = 4;
#elif defined(DEVICE_MESH_DECK)
    // Same 320x240 panel as the T-Deck but smaller glass, so it takes the
    // T-Deck's row font and a little more height on top: rows here are tapped as
    // often as they are scrolled to. The list scrolls, so the cost is only that
    // fewer rows are on screen at once.
    const lv_font_t *cfgRowFont = &lv_font_montserrat_14;
    const int cfgPadTop = 6;
    const int cfgPadBottom = 6;
#elif defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_CROWPANEL)
    // Roomier touch panels: readable text and taller tap targets.
    const lv_font_t *cfgRowFont = &lv_font_montserrat_16;
    const int cfgPadTop = 14;
    const int cfgPadBottom = 14;
#elif defined(DEVICE_CARDPUTER_ZERO) && UI_CARDPUTER_COMPACT
    // 320x170 behind small glass: the ADV's 10 px rows are hard to read on it,
    // and the extra 35 px of height pays for bigger text and more space.
    const lv_font_t *cfgRowFont = &lv_font_montserrat_14;
    const int cfgPadTop = 4;
    const int cfgPadBottom = 4;
#elif UI_TOUCH_ONLY_PROFILE
    // Touch-only build: tall rows give a comfortable tap target. The action list
    // scrolls, so the extra height just means a bit more scrolling.
    const lv_font_t *cfgRowFont = &lv_font_montserrat_10;
    const int cfgPadTop = 11;
    const int cfgPadBottom = 11;
#else
    const lv_font_t *cfgRowFont = &lv_font_montserrat_10;
    const int cfgPadTop = 2;
    const int cfgPadBottom = 2;
#endif

    for (int i = 0; i < s_cfgActionCount; i++) {
        const int actionId = s_cfgActions[i];
        const bool disabled = cfgActionDisabled(actionId);
        char rowText[80];

        lv_obj_t *row = lv_label_create(s_cfgActionList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, cfgRowFont, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, cfgPadTop, 0);
        lv_obj_set_style_pad_bottom(row, cfgPadBottom, 0);
        lv_obj_set_style_radius(row, 3, 0);
        // Greyed rows are non-interactive on touch; keyboard activation is gated
        // separately in activateCfgSelection().
        if (!disabled) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row,
                        onCfgActionRowPressed,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
        }
#if defined(DEVICE_CROWPANEL)
        lv_obj_set_style_min_height(row, 47, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
#else
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
#endif
        lv_label_set_text(row, cfgActionLabel(actionId, rowText, sizeof(rowText)));

        const bool selected = (i == s_cfgSelection) && !disabled;
        cfgStyleRow(row, i, disabled, selected);
        if (selected) selectedRowObj = row;
    }

    s_cfgPaintedSelection = s_cfgSelection;
    if (selectedRowObj) {
        lv_obj_scroll_to_view(selectedRowObj, LV_ANIM_OFF);
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    fillCfgPagerInfo();
#endif
}

static void onCfgActionRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_cfgActionCount) return;
    s_cfgSelection = idx;
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
#if HAS_TOUCH
    // A tap is the whole gesture, so it selects the row and runs it -- on the
    // keyboard boards with a panel too (T-Deck, T-Deck Pro): a tap that only
    // moved the highlight left the finger needing Enter to finish the job.
    // Enter still runs the highlighted row there, as it always has.
    //
    // A drag does not reach here. LVGL only sends LV_EVENT_CLICKED when the
    // press did not turn into a scroll, so flicking the list past a row still
    // just scrolls it.
    //
    // refreshCfgSelection() rather than refreshCfgModal(): it repaints the two
    // rows whose highlight changed instead of rebuilding every row, so the row
    // this event belongs to is still alive when the action below runs.
    refreshCfgSelection();
    activateCfgSelection();
#else
    refreshCfgModal();
#endif
}

#if UI_TOUCH_ONLY_PROFILE
static void onCfgHeaderInfoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    openNodeInfoModal();
}
#endif

static void onCfgWifiRowPressed(lv_event_t *e);
static void onCfgWifiBackdropPressed(lv_event_t *e);
static void onCfgWifiScanRowPressed(lv_event_t *e);

static void closeCfgWifiPassModal() {
    if (lvObjValid(s_cfgWifiPassBackdrop)) {
        lv_obj_del(s_cfgWifiPassBackdrop);
    } else if (lvObjValid(s_cfgWifiPassModal)) {
        lv_obj_del(s_cfgWifiPassModal);
    }
    s_cfgWifiPassBackdrop = nullptr;
    s_cfgWifiPassModal = nullptr;
    s_cfgWifiPassInput = nullptr;
    s_cfgWifiPassKeyboard = nullptr;
    s_cfgWifiPassStatus = nullptr;
    s_cfgWifiPassTargetSsid[0] = '\0';
}

static void closeCfgWifiScanModal() {
    closeCfgWifiPassModal();
    if (lvObjValid(s_cfgWifiScanBackdrop)) {
        lv_obj_del(s_cfgWifiScanBackdrop);
    } else if (lvObjValid(s_cfgWifiScanModal)) {
        lv_obj_del(s_cfgWifiScanModal);
    }
    s_cfgWifiScanBackdrop = nullptr;
    s_cfgWifiScanModal = nullptr;
    s_cfgWifiScanList = nullptr;
    s_cfgWifiScanStatus = nullptr;
}

static void closeCfgWifiDeleteConfirm() {
    if (lvObjValid(s_cfgWifiDelBackdrop)) {
        lv_obj_del(s_cfgWifiDelBackdrop);
    } else if (lvObjValid(s_cfgWifiDelModal)) {
        lv_obj_del(s_cfgWifiDelModal);
    }
    s_cfgWifiDelBackdrop = nullptr;
    s_cfgWifiDelModal = nullptr;
}

static void closeCfgWifiPickerModal() {
    // Ahead of the picker's own teardown: the prompt is parented to the root
    // screen, so closing the picker under it would leave it on screen owning
    // the keyboard with nothing behind it.
    closeCfgWifiDeleteConfirm();
    closeCfgWifiScanModal();
    if (lvObjValid(s_cfgWifiBackdrop)) {
        lv_obj_del(s_cfgWifiBackdrop);
    } else if (lvObjValid(s_cfgWifiModal)) {
        lv_obj_del(s_cfgWifiModal);
    }
    s_cfgWifiBackdrop = nullptr;
    s_cfgWifiModal = nullptr;
    s_cfgWifiList = nullptr;
    s_cfgWifiHint = nullptr;
    s_cfgWifiPickerOnboardingMode = false;
}

// Replaces the shortcut line with a one-off message. Reverts on the next open,
// which is the whole lifetime that matters here.
static void setCfgWifiPickerNotice(const char *msg) {
    if (lvObjValid(s_cfgWifiHint)) lv_label_set_text(s_cfgWifiHint, msg);
}

static void refreshCfgWifiPickerModal() {
    if (!s_cfgWifiModal || !s_cfgWifiList) return;

    lv_obj_clean(s_cfgWifiList);
    for (int i = 0; i < s_cfgKnownWifiCount; i++) {
        const KnownWifiEntry &entry = s_cfgKnownWifi[i];
        // Network names only — where an entry came from isn't something the
        // user picks by, and the "AP" row reads as a choice on its own.
        char rowText[96];
        snprintf(rowText, sizeof(rowText), "%s",
                 entry.ssid[0] ? entry.ssid : TR("(not set)"));

        lv_obj_t *row = lv_label_create(s_cfgWifiList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_set_style_pad_bottom(row, 4, 0);
        lv_obj_set_style_radius(row, 3, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, i == s_cfgWifiSelection ? 2 : 0, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_text_color(row, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(row, lv_color_hex((i & 1) ? 0x123266 : 0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, 0, 0);
    #endif
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, rowText);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, onCfgWifiRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (i == s_cfgWifiSelection) {
#if defined(DEVICE_TDECK_PRO)
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
#else
            lv_obj_set_style_bg_color(row, lvColorFrom565(s_ui.selectBg), 0);
            lv_obj_set_style_bg_opa(row, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_80, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(0xE8F1FF), 0);
            lv_obj_set_style_text_color(row,
                                        (s_cfg.uiMode == UI_MODE_LIGHT)
                                            ? lv_color_hex(0x000000)
                                            : lv_color_hex(0xFFFFFF),
                                        0);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
#endif
        }
    }
}

static void applyCfgWifiSelection(int idx) {
    if (idx < 0 || idx >= s_cfgKnownWifiCount) return;

    const KnownWifiEntry &entry = s_cfgKnownWifi[idx];
    if (idx == 0) {
        if (!entry.ssid[0]) {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Configured WiFi is empty"));
            return;
        }
        s_wifiUsingKnownOverride = false;
        memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
        memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("WiFi selected: %s"), entry.ssid);
    } else {
        s_wifiUsingKnownOverride = true;
        strncpy(s_wifiSelectedSsid, entry.ssid, sizeof(s_wifiSelectedSsid) - 1);
        strncpy(s_wifiSelectedPass, entry.pass, sizeof(s_wifiSelectedPass) - 1);
        if (wifiForceApMode()) {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("AP mode selected"));
        } else {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("WiFi selected: %s"), entry.ssid);
        }
    }

    s_cfgWifiSelection = idx;
    s_wifiStaKickMs = 0;
    const bool forceAp = wifiForceApMode();
    webCfgSetForceAp(forceAp);
    persistWifiForceAp();
    if (s_cfg.wifiEnabled && wifiHasActiveCreds()) {
        WiFi.disconnect(false);
    }

    // Picking AP used to set a flag and nothing else. Nothing in the firmware
    // raises a SoftAP on its own — web config is what does, and it reads the
    // flag when it starts — so on a device with web config off, "AP mode
    // selected" was a row that described a radio state the device was not in
    // and had no way of reaching. The two settings that decide whether it can
    // are therefore moved here rather than left for the user to find.
    //
    // Not with the master WiFi switch off, though: the load-time invariants
    // clear both settings in that case, so switching them on here would be
    // undone on the next boot, and neither can start meanwhile. The row says
    // what is missing instead — the same answer the Web Config row gives.
    if (forceAp && !s_cfg.wifiEnabled) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("AP mode selected - enable WiFi first"));
    } else if (forceAp) {
        // MQTT first: the bridge dials out to a broker, and a device serving
        // its own network has no route to one -- and while it is on, web
        // config will not leave the station for the SoftAP (see
        // webCfgStartDeferred()). Stopped live rather than through the CFG
        // row's reboot — this is a network being chosen, and a restart in the
        // middle of that is not what anyone pressed.
        if (s_cfg.mqttEnabled) {
            s_cfg.mqttEnabled = false;
            mqttBridgeConfigChanged();
            persistConfigToPrefs();
            Serial.println("[wifi] AP mode: MQTT bridge switched off");
        }
        if (!s_webCfgEnabled) {
            s_webCfgEnabled = true;
            persistWebCfgEnabled();
            Serial.println("[wifi] AP mode: web config switched on");
        }
    }

    // Web config picked its radio mode when it started, so a running server has
    // to be restarted to move between the SoftAP and the station network — and
    // one just switched on above has to be started at all, or choosing AP would
    // still leave the radio idle until the next boot.
    const bool startForAp = forceAp && s_cfg.wifiEnabled && s_webCfgEnabled && !webCfgRunning();
    if (s_webCfgEnabled && (webCfgRunning() || startForAp)) {
#if HAS_BLE_KEYBOARD
        // Same exclusion the CFG row enforces: the server must not come up
        // while a BT keyboard controller is live. Only on the start path — if
        // the server was already running, the keyboard is already stopped.
        const bool stoppedBleKbd = startForAp ? webCfgStopBleKeyboardForWeb() : false;
#endif
        if (webCfgRunning()) webCfgEnd();
        webCfgPushActiveCreds();
#if HAS_FILE_STORAGE && !defined(DEVICE_TDECK_PRO)
        bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, captureWebScreenshotPng);
#else
        bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
#endif
        if (ok) {
            if (forceAp) {
                // Says AP, and says where: the address is the whole point of
                // the mode, and it is not the one the station network gave.
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("AP mode: %s"), webCfgIP());
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         webCfgIsLite() ? TR("Web Lite: %s") : TR("Web: %s"), webCfgIP());
            }
            // Same two notices the CFG row raises when it starts the server,
            // in the same order: a board that pauses chat for Wi-Fi is costing
            // the user messages, which outranks a keyboard that was switched
            // off. Only on the start path — a restart changes neither.
            if (startForAp && webCfgChatPaused()) {
                showWebCfgChatPausedNotice();
            }
#if HAS_BLE_KEYBOARD
            else if (stoppedBleKbd) {
                openCfgActionMessageModal(TR(kWebBleKbdExclusiveNotice));
            }
#endif
        } else if (webCfgStartDeferred()) {
            s_webCfgAwaitStation = true;
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Web Config: On (waiting for WiFi)"));
        } else {
            s_webCfgEnabled = false;
            persistWebCfgEnabled();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Web Config start failed"));
        }
    }
}

// The AP row's fixed position in the picker. It is a mode, not a network, so it
// is the floor of the fallback search below rather than a candidate.
static constexpr int kForceApWifiIndex = 1;

