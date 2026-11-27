// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: known Wi-Fi networks, Config action list, OTA worker mode, Wi-Fi station.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void populateKnownWifiEntries() {
    s_cfgKnownWifiCount = 0;

    // Slot 0 mirrors configured credentials from web/onboarding config.
    if (s_cfgKnownWifiCount < kKnownWifiMaxCount) {
        KnownWifiEntry &entry = s_cfgKnownWifi[s_cfgKnownWifiCount++];
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.ssid, s_cfg.wifiSsid, sizeof(entry.ssid) - 1);
        strncpy(entry.pass, s_cfg.wifiPass, sizeof(entry.pass) - 1);
    }

    // Slot 1 is not a network: picking it forces the device's own SoftAP, so
    // web config stays reachable when the configured network is out of range
    // or the user simply wants to connect to the device directly.
    if (s_cfgKnownWifiCount < kKnownWifiMaxCount) {
        KnownWifiEntry &entry = s_cfgKnownWifi[s_cfgKnownWifiCount++];
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.ssid, kForceApSsid, sizeof(entry.ssid) - 1);
    }

    // Add known networks created by successful scan/connect attempts.
    for (int i = 0; i < s_cfgKnownWifiAddedCount && s_cfgKnownWifiCount < kKnownWifiMaxCount; i++) {
        KnownWifiEntry &entry = s_cfgKnownWifi[s_cfgKnownWifiCount++];
        entry = s_cfgKnownWifiAdded[i];
    }

    // Keep picker selection anchored to the currently active network source.
    int selected = 0;
    if (s_wifiUsingKnownOverride && s_wifiSelectedSsid[0]) {
        for (int i = 0; i < s_cfgKnownWifiCount; i++) {
            if (strncmp(s_cfgKnownWifi[i].ssid, s_wifiSelectedSsid,
                        sizeof(s_cfgKnownWifi[i].ssid)) == 0) {
                selected = i;
                break;
            }
        }
    }
    s_cfgWifiSelection = selected;
}

static void addTempKnownWifi(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return;
    // No remembered list on this board (see kKnownWifiExtraCount). Returning
    // here also keeps the rolling-window eviction below from indexing [-1].
    if (kKnownWifiExtraCount <= 0) return;

    // "AP" is the picker's force-SoftAP sentinel, not a joinable network; a
    // scanned network by that name would shadow it.
    if (strncmp(ssid, kForceApSsid, sizeof(kForceApSsid)) == 0) return;

    // Configured network remains in slot 0; no need to duplicate it.
    if (strncmp(ssid, s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid)) == 0) {
        return;
    }

    for (int i = 0; i < s_cfgKnownWifiAddedCount; i++) {
        if (strncmp(s_cfgKnownWifiAdded[i].ssid,
                    ssid,
                    sizeof(s_cfgKnownWifiAdded[i].ssid)) == 0) {
            strncpy(s_cfgKnownWifiAdded[i].pass,
                    pass ? pass : "",
                    sizeof(s_cfgKnownWifiAdded[i].pass) - 1);
            return;
        }
    }

    KnownWifiEntry newEntry = {};
    strncpy(newEntry.ssid, ssid, sizeof(newEntry.ssid) - 1);
    strncpy(newEntry.pass, pass ? pass : "", sizeof(newEntry.pass) - 1);

    if (s_cfgKnownWifiAddedCount < kKnownWifiExtraCount) {
        s_cfgKnownWifiAdded[s_cfgKnownWifiAddedCount++] = newEntry;
        saveKnownWifiNetworks();
        return;
    }

    // Keep a rolling window of known scanned networks.
    for (int i = 1; i < kKnownWifiExtraCount; i++) {
        s_cfgKnownWifiAdded[i - 1] = s_cfgKnownWifiAdded[i];
    }
    s_cfgKnownWifiAdded[kKnownWifiExtraCount - 1] = newEntry;
    saveKnownWifiNetworks();
}

// Drop a network from the remembered list. Used when it becomes the configured
// one: slot 0 already shows it, and a second copy in the list is both confusing
// and the thing that would re-introduce a RAM-only override if picked.
static void forgetSavedWifi(const char *ssid) {
    if (!ssid || !ssid[0]) return;
    int w = 0;
    bool removed = false;
    for (int i = 0; i < s_cfgKnownWifiAddedCount; i++) {
        if (strncmp(s_cfgKnownWifiAdded[i].ssid, ssid,
                    sizeof(s_cfgKnownWifiAdded[i].ssid)) == 0) {
            removed = true;
            continue;
        }
        if (w != i) s_cfgKnownWifiAdded[w] = s_cfgKnownWifiAdded[i];
        w++;
    }
    if (!removed) return;
    for (int i = w; i < s_cfgKnownWifiAddedCount; i++) {
        memset(&s_cfgKnownWifiAdded[i], 0, sizeof(s_cfgKnownWifiAdded[i]));
    }
    s_cfgKnownWifiAddedCount = w;
    saveKnownWifiNetworks();
}

// Switch the configured network, keeping the one it replaces in the remembered
// list. This is the whole point of the list: joining network B should not lose
// network A, so you can switch back without re-entering its password.
//
// Order matters. s_cfg is updated *first* so that addTempKnownWifi()'s "don't
// duplicate the configured network" guard compares the outgoing network against
// the *new* configured one rather than against itself, which would make it skip
// the very entry we are trying to keep.
static void wifiSetConfiguredNetwork(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return;

    KnownWifiEntry previous = {};
    strncpy(previous.ssid, s_cfg.wifiSsid, sizeof(previous.ssid) - 1);
    strncpy(previous.pass, s_cfg.wifiPass, sizeof(previous.pass) - 1);
    const bool sameNetwork =
        strncmp(previous.ssid, ssid, sizeof(previous.ssid)) == 0;

    utf8util::copyTruncate(s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid), ssid);
    utf8util::copyTruncate(s_cfg.wifiPass, sizeof(s_cfg.wifiPass), pass ? pass : "");

    if (!sameNetwork && previous.ssid[0]) {
        addTempKnownWifi(previous.ssid, previous.pass);
    }
    // Whatever we just made configured must not also sit in the remembered list.
    forgetSavedWifi(s_cfg.wifiSsid);
}

// ── Remembered-network bridge for config export/import ───────────────────────
// Declared in config_io.h. cfgToYaml()/cfgFromYaml() live in config_io.cpp,
// which has no view of the picker's array or its NVS blob; these are the whole
// of what it needs.

int cfgSavedWifiCount() { return s_cfgKnownWifiAddedCount; }

bool cfgSavedWifiAt(int idx, char *ssidOut, size_t ssidCap, char *passOut, size_t passCap) {
    if (idx < 0 || idx >= s_cfgKnownWifiAddedCount) return false;
    if (ssidOut && ssidCap) utf8util::copyTruncate(ssidOut, ssidCap, s_cfgKnownWifiAdded[idx].ssid);
    if (passOut && passCap) utf8util::copyTruncate(passOut, passCap, s_cfgKnownWifiAdded[idx].pass);
    return true;
}

void cfgSavedWifiClear() {
    s_cfgKnownWifiAddedCount = 0;
    memset(s_cfgKnownWifiAdded, 0, sizeof(s_cfgKnownWifiAdded));
}

void cfgSavedWifiAdd(const char *ssid, const char *pass) {
    // Deliberately not addTempKnownWifi(): that one refuses anything matching
    // the configured SSID, and during an import the caller has already decided
    // which entry is the active one. Duplicates are filtered there instead.
    if (!ssid || !ssid[0]) return;
    // Also covers boards with no remembered list: a config imported from a
    // multi-network device keeps its active network and drops the rest.
    if (s_cfgKnownWifiAddedCount >= kKnownWifiExtraCount) return;
    KnownWifiEntry &e = s_cfgKnownWifiAdded[s_cfgKnownWifiAddedCount++];
    memset(&e, 0, sizeof(e));
    strncpy(e.ssid, ssid, sizeof(e.ssid) - 1);
    strncpy(e.pass, pass ? pass : "", sizeof(e.pass) - 1);
}

void cfgSavedWifiCommit() {
    saveKnownWifiNetworks();
    populateKnownWifiEntries();
}

bool cfgSavedWifiRemember(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return false;
    // Refuses the AP sentinel and the configured network, and persists itself.
    if (!strncmp(ssid, kForceApSsid, sizeof(kForceApSsid))) return false;
    if (!strncmp(ssid, s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid))) return false;
    addTempKnownWifi(ssid, pass);
    populateKnownWifiEntries();
    return true;
}

bool cfgSavedWifiRemove(const char *ssid) {
    if (!ssid || !ssid[0]) return false;
    const int before = s_cfgKnownWifiAddedCount;
    forgetSavedWifi(ssid);   // persists when it removes something
    if (s_cfgKnownWifiAddedCount == before) return false;
    populateKnownWifiEntries();
    return true;
}

// Promote a remembered network to being the configured one. The network it
// replaces goes into the remembered list, so this is a swap rather than a
// replacement and switching back stays one click away.
bool cfgSavedWifiActivate(const char *ssid) {
    if (!ssid || !ssid[0]) return false;

    // Copy the credentials out before mutating: wifiSetConfiguredNetwork()
    // rewrites the very array we would otherwise still be pointing into.
    char pickSsid[sizeof(s_cfgKnownWifiAdded[0].ssid)] = {};
    char pickPass[sizeof(s_cfgKnownWifiAdded[0].pass)] = {};
    bool found = false;
    for (int i = 0; i < s_cfgKnownWifiAddedCount; i++) {
        if (strncmp(s_cfgKnownWifiAdded[i].ssid, ssid,
                    sizeof(s_cfgKnownWifiAdded[i].ssid)) != 0) {
            continue;
        }
        utf8util::copyTruncate(pickSsid, sizeof(pickSsid), s_cfgKnownWifiAdded[i].ssid);
        utf8util::copyTruncate(pickPass, sizeof(pickPass), s_cfgKnownWifiAdded[i].pass);
        found = true;
        break;
    }
    if (!found) return false;

    wifiSetConfiguredNetwork(pickSsid, pickPass);
    // Any picker override would keep feeding the old network to
    // wifiGetActiveCreds() and undo the switch on the next reconnect.
    s_wifiUsingKnownOverride = false;
    memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
    memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
    persistWifiForceAp();
    syncWifiCredsToPrefs();
    flushConfigIfDirty();    // not debounced: the reconnect below may cost us the session
    populateKnownWifiEntries();
    s_wifiStaKickMs = 0;     // re-associate promptly
    Serial.printf("[wifi] web config switched to \"%s\"\n", s_cfg.wifiSsid);
    return true;
}


static void wifiGetActiveCreds(const char **ssidOut, const char **passOut) {
#if defined(PLATFORM_LINUX)
    // The OS owns the network here; whatever it is connected to is the active
    // network, and there are no credentials of our own to hand anyone.
    static char sysSsid[33];
    String cur = WiFi.SSID();
    snprintf(sysSsid, sizeof(sysSsid), "%s", cur.length() ? cur.c_str() : "system network");
    if (ssidOut) *ssidOut = sysSsid;
    if (passOut) *passOut = "";
    return;
#endif
    const char *ssid = s_cfg.wifiSsid;
    const char *pass = s_cfg.wifiPass;
    if (s_wifiUsingKnownOverride && s_wifiSelectedSsid[0]) {
        ssid = s_wifiSelectedSsid;
        pass = s_wifiSelectedPass;
    }
    if (ssidOut) *ssidOut = ssid;
    if (passOut) *passOut = pass;
}

// True when the picker's "AP" entry is selected: the device hosts its own
// network instead of joining one.
static bool wifiForceApMode() {
    return s_wifiUsingKnownOverride
        && strncmp(s_wifiSelectedSsid, kForceApSsid, sizeof(s_wifiSelectedSsid)) == 0;
}

static bool wifiHasActiveCreds() {
    // Forced AP mode has no station credentials by definition. Reporting none
    // keeps every STA path — reconnect kicks, MQTT, the nodes-over-WiFi session
    // — from trying to associate with a network literally named "AP".
    if (wifiForceApMode()) return false;
    const char *ssid = nullptr;
    wifiGetActiveCreds(&ssid, nullptr);
    return ssid && ssid[0];
}

// Hand web config the network the device is actually using, right before it
// brings the radio up. The on-device WiFi picker can override the configured
// network without writing back to the config — deliberately, so choosing a
// network once does not overwrite the user's saved default — which means web
// config cannot find that choice on its own. Without this it dials the *stored*
// SSID, times out, and drops to AP fallback, taking the device off the network
// it was reachable on a moment earlier.
//
// Pairs with webCfgSetForceAp() at the same call sites; forced-AP mode reports
// no station credentials, so this pushes an empty override and the AP branch
// takes over.
static void webCfgPushActiveCreds() {
    const char *ssid = nullptr;
    const char *pass = nullptr;
    wifiGetActiveCreds(&ssid, &pass);
    if (wifiForceApMode()) { ssid = nullptr; pass = nullptr; }
    webCfgSetStaCreds(ssid, pass);
}

#if HAS_VNC_HOST
static bool vncNetworkConnected() {
    return s_cfg.wifiEnabled
        && WiFi.status() == WL_CONNECTED
        && WiFi.getMode() != WIFI_AP
        && (uint32_t)WiFi.localIP() != 0;
}
#endif

static const char *wifiActiveSsid() {
    const char *ssid = nullptr;
    wifiGetActiveCreds(&ssid, nullptr);
    return (ssid && ssid[0]) ? ssid : "";
}

static bool wifiBeginActiveKnown() {
    const char *ssid = nullptr;
    const char *pass = nullptr;
    wifiGetActiveCreds(&ssid, &pass);
    if (!ssid || !ssid[0]) return false;
    WiFi.begin(ssid, pass ? pass : "");
    return true;
}

// What the archive rows append when it has nowhere to write: a card on the SD
// boards, the disk on the CardputerZero (HAS_NODE_ARCHIVE without a slot).
static const char *archiveNoStorageSuffix() {
#if HAS_SD_CARD
    return TR(" (no card)");
#else
    return TR(" (no storage)");
#endif
}

static const char *cfgActionLabel(int actionId, char *buf, size_t bufLen) {
    if (!buf || bufLen == 0) return "";
    if (cfgGenIsAction(actionId)) return cfgGenLabel(actionId - CFG_ACTION_GEN_FIRST, buf, bufLen);
    switch (actionId) {
        case CFG_ACTION_WIFI_TOGGLE:
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "%s", TR("WiFi: Off"));
            } else if ((uint32_t)WiFi.localIP() != 0) {
                // Real STA/network address (MQTT etc.), shown regardless of web config.
                snprintf(buf, bufLen, TR("WiFi: On (%s)"), WiFi.localIP().toString().c_str());
            } else if (webCfgRunning()) {
                snprintf(buf, bufLen, TR("WiFi: On (%s)"), webCfgIP());  // AP fallback
            } else if (wifiForceApMode()) {
                snprintf(buf, bufLen, "%s", TR("WiFi: On (AP mode)"));
            } else if (!wifiHasActiveCreds()) {
                snprintf(buf, bufLen, "%s", TR("WiFi: On (AP MODE ONLY)"));
            } else {
                snprintf(buf, bufLen, TR("WiFi: On (connecting %s)"), wifiActiveSsid());
            }
            break;
        case CFG_ACTION_CHOOSE_WIFI:
            if (s_wifiUsingKnownOverride && s_wifiSelectedSsid[0]) {
                snprintf(buf, bufLen, TR("Choose WiFi: %s"), s_wifiSelectedSsid);
            } else if (s_cfg.wifiSsid[0]) {
                snprintf(buf, bufLen, TR("Choose WiFi: %s"), s_cfg.wifiSsid);
            } else {
                snprintf(buf, bufLen, "%s", TR("Choose WiFi: (none)"));
            }
            break;
#if HAS_BLE_KEYBOARD
        case CFG_ACTION_BLE_KBD_TOGGLE: {
            if (!s_cfg.bleKbdEnabled) {
                snprintf(buf, bufLen, "%s", TR("BT Keyboard: Off"));
            } else {
                char status[80];
                bleKeyboardStatusText(status, sizeof(status));
                snprintf(buf, bufLen, TR("BT Keyboard: %s"), status);
            }
        } break;
        case CFG_ACTION_BLE_KBD_PAIR:
            if (bleKeyboardRepairNeeded()) {
                snprintf(buf, bufLen, "%s", TR("Pairing lost - tap to re-pair"));
            } else if (s_cfg.bleKbdName[0]) {
                snprintf(buf, bufLen, TR("Pair BT Keyboard: %s"), s_cfg.bleKbdName);
            } else {
                snprintf(buf, bufLen, "%s", TR("Pair BT Keyboard: (none)"));
            }
            break;
#endif
        case CFG_ACTION_WEBCFG:
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "%s", TR("Web Config: Off (WiFi off)"));
            } else if (!s_webCfgEnabled) {
                snprintf(buf, bufLen, "%s", TR("Web Config: Disabled"));
            } else if (webCfgRunning()) {
                // AP mode serves the reduced page; name it so the missing tabs
                // read as the mode, not a failure. Chat-paused outranks it —
                // that one costs the user messages.
                if (webCfgChatPaused()) {
                    snprintf(buf, bufLen, "%s", TR("Web Config: On (chat PAUSED)"));
                } else {
#if defined(PLATFORM_LINUX)
                    // Its own port, not 80: the row carries the whole address
                    // so it is still in view after the start notice closes.
                    snprintf(buf, bufLen, TR("Web Config: On (%s)"), webCfgIP());
#else
                    snprintf(buf, bufLen, webCfgIsLite() ? TR("Web Config: On (Lite)")
                                                         : TR("Web Config: On"));
#endif
                }
            } else if (s_webCfgAwaitStation) {
                snprintf(buf, bufLen, "%s", TR("Web Config: On (waiting for WiFi)"));
            } else {
                snprintf(buf, bufLen, "%s", TR("Web Config: Enabled"));
            }
            break;
#if HAS_VNC_HOST
        case CFG_ACTION_VNC_HOST: {
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "%s", TR("VNC Host: Off (WiFi off)"));
            } else if (!s_vncEnabled && !vncNetworkConnected()) {
                snprintf(buf, bufLen, "%s", TR("VNC Host: Unavailable (WiFi not connected)"));
            } else if (!s_vncEnabled) {
                snprintf(buf, bufLen, "%s", TR("VNC Host: Off"));
            } else if (!vncNetworkConnected()) {
                snprintf(buf, bufLen, "%s", TR("VNC Host: On (network disconnected)"));
            } else if (vncHostClientConnected()) {
                snprintf(buf, bufLen, TR("VNC Host: PIN %s (browser connected)"),
                         vncHostPin());
            } else if (!webCfgRunning()) {
                snprintf(buf, bufLen, TR("VNC Host: PIN %s (turn on Web Config)"),
                         vncHostPin());
            } else {
                // The PIN is entered in the web config's Remote tab, so that
                // is the address to give, not the mirror's own port.
                snprintf(buf, bufLen, TR("VNC Host: PIN %s - http://%s/"),
                         vncHostPin(), webCfgIP());
            }
        } break;
#endif
#if HAS_WEB_FILES
        case CFG_ACTION_WEB_FILES:
            snprintf(buf, bufLen, TR("Web Files: %s"),
                     cfgWebFilesEnabled(s_cfg) ? TR("On") : TR("Off"));
            break;
#endif
        case CFG_ACTION_GPS_TOGGLE:
            if (s_cfg.gpsEnabled) {
                snprintf(buf, bufLen, "%s", TR("GPS: Enabled (Hardware)"));
            } else {
                snprintf(buf, bufLen, "%s", TR("GPS: Disabled (Default Coords)"));
            }
            break;
        case CFG_ACTION_SHARE_LOCATION:
            snprintf(buf, bufLen, TR("Share Location: %s"), s_cfg.shareLocation ? TR("On") : TR("Off"));
            break;
        case CFG_ACTION_POSITION_PRECISION:
            snprintf(buf, bufLen, TR("Location Precision: %s"),
                     TR(positionPrecisionLabel(s_cfg.positionPrecision)));
            break;
        case CFG_ACTION_SIG_POLICY:
            snprintf(buf, bufLen, TR("Signed Packets: %s"),
                     TR(kSigPolicyLabel[sigPolicyCoerce(s_cfg.packetSigPolicy)]));
            break;
        case CFG_ACTION_NODE_NAME:
            snprintf(buf, bufLen, TR("Node Name: %s (%s)"),
                     s_cfg.nodeLong[0]  ? s_cfg.nodeLong  : TR("unset"),
                     s_cfg.nodeShort[0] ? s_cfg.nodeShort : "----");
            break;
#if defined(DEVICE_TDISPLAY_P4)
        case CFG_ACTION_ANTENNA:
            snprintf(buf, bufLen, TR("Antenna: %s"),
                     cfgP4AntennaExternal(s_cfg) ? TR("external") : TR("internal"));
            break;
#endif
        case CFG_ACTION_PRESET:
            // "Custom" rather than a preset name when loraUsePreset is false:
            // modemPreset still holds the last preset in that state (it is what
            // Custom was edited away from), so naming it here would report a
            // preset the radio is not actually running.
            snprintf(buf, bufLen, TR("Preset (%s)"),
                     !s_cfg.loraUsePreset ? TR("Custom")
                       : kPresets[s_cfg.modemPreset < PRESET_COUNT
                                  ? s_cfg.modemPreset : PRESET_LONG_FAST].name);
            break;
        case CFG_ACTION_COMMUNITY_PRESET:
            snprintf(buf, bufLen, "%s", TR("Community Preset"));
            break;
#if FEATURE_LICENSED_MODE
        case CFG_ACTION_LICENSED_MODE:
            snprintf(buf, bufLen, TR("Licensed (ham) mode: %s"),
                     lic::active() ? s_cfg.callSign : TR("Off"));
            break;
#endif
        case CFG_ACTION_EXPORT:
            snprintf(buf, bufLen, "%s", TR("Export Config"));
            break;
        case CFG_ACTION_IMPORT:
            snprintf(buf, bufLen, "%s", TR("Import Config"));
            break;
        case CFG_ACTION_THEME:
            snprintf(buf, bufLen, TR("Theme: %s"), uiThemePresetNameFromCfg());
            break;
        case CFG_ACTION_OWNER_COLOR:
            snprintf(buf, bufLen, TR("My Message Color: %s"),
                     (s_cfg.userMsgColor < kUserMsgColorCount)
                         ? TR(kUserMsgColors[s_cfg.userMsgColor].name)
                         : TR("Default"));
            break;
        case CFG_ACTION_UNITS:
            snprintf(buf, bufLen, TR("Units: %s"), s_cfg.displayUnits ? TR("Imperial") : TR("Metric"));
            break;
#if HAS_RUNTIME_ORIENTATION
        case CFG_ACTION_ORIENTATION:
            snprintf(buf, bufLen, TR("Orientation: %s"),
                     uiOrientName(s_cfg.uiOrientation));
            break;
#endif
        case CFG_ACTION_LANGUAGE:
            snprintf(buf, bufLen, TR("Language: %s"),
                     kUiLangNames[s_cfg.uiLanguage < LANG_COUNT ? s_cfg.uiLanguage : LANG_EN]);
            break;
#if HAS_SPELLCHECK
        case CFG_ACTION_SPELL_CHECK:
            // The word list is English, so in any other language the setting
            // is kept but has nothing to act on -- and the row says so, rather
            // than reading On over a compose box that never offers anything.
            snprintf(buf, bufLen, TR("Spell Check: %s%s"),
                     s_cfg.spellCheckEnabled ? TR("On") : TR("Off"),
                     (s_cfg.spellCheckEnabled && i18nGetLang() != LANG_EN)
                         ? TR(" (English only)") : "");
            break;
#endif
        case CFG_ACTION_BATT_DISPLAY:
            snprintf(buf, bufLen, TR("Battery Display: %s"),
                     s_cfg.battDisplayMode == BATT_DISPLAY_VOLTAGE ? TR("Voltage") : TR("Percent"));
            break;
        case CFG_ACTION_CHAT_STYLE:
#if defined(DEVICE_TDECK_PRO)
            snprintf(buf, bufLen, TR("Chat Type: %s"), chatStyleName(s_cfg.chatStyle));
#else
            snprintf(buf, bufLen, TR("Chat Style: %s"), chatStyleName(s_cfg.chatStyle));
#endif
            break;
        case CFG_ACTION_CHAT_NAMES:
            snprintf(buf, bufLen, TR("Chat Names: %s"), chatNameStyleName(s_cfg.chatNameStyle));
            break;
        case CFG_ACTION_CHAT_COLORS:
            snprintf(buf, bufLen, TR("Chat Colors: %s"),
                     s_cfg.chatColorsEnabled ? TR("On") : TR("Off"));
            break;
        case CFG_ACTION_FONT_SIZE:
            snprintf(buf, bufLen, TR("Font Size: %s"), fontSizeName(s_cfg.fontSize));
            break;
        case CFG_ACTION_BRIGHTNESS:
            snprintf(buf, bufLen, TR("Brightness: %u%%"), (unsigned)s_cfg.brightness);
            break;
        case CFG_ACTION_SCREEN_TIMEOUT:
            snprintf(buf, bufLen, TR("Screen Timeout: %s"),
                     screenTimeoutName(s_cfg.screenOnSecs));
            break;
        #if FEATURE_LOCK_SCREEN
        case CFG_ACTION_LOCK_SCREEN:
            snprintf(buf, bufLen, TR("Lock Screen: %s"),
                     s_cfg.lockScreenEnabled ? TR("On") : TR("Off"));
            break;
        case CFG_ACTION_LOCK_SCREEN_OFF:
            snprintf(buf, bufLen, TR("Lock Screen Off: %s"),
                     lockScreenOffName(s_cfg.lockScreenOffSecs));
            break;
        #endif
        #if HAS_SCROLL_INVERT
        case CFG_ACTION_INVERT_SCROLL:
            snprintf(buf, bufLen, TR("Invert Scrolling: %s"), s_cfg.invertScroll ? TR("On") : TR("Off"));
            break;
        #endif
        #if HAS_NAV_BAR_TOGGLE
        case CFG_ACTION_NAV_BAR:
            snprintf(buf, bufLen, TR("Nav Bar: %s"), s_cfg.navBarEnabled ? TR("On") : TR("Off"));
            break;
        #endif
        #if HAS_CHANNEL_PANEL_LOCK
        case CFG_ACTION_CHANNEL_PANEL_LOCK:
            snprintf(buf, bufLen, TR("Channel Panel: %s"),
                     cfgChannelPanelLocked(s_cfg) ? TR("Locked") : TR("Unlocked"));
            break;
        #endif
        #if HAS_NAV_ALIGN_SETTING
        case CFG_ACTION_NAV_ALIGN:
            snprintf(buf, bufLen, TR("Nav Alignment: %s"), navAlignName(cfgNavAlign(s_cfg)));
            break;
        #endif
        case CFG_ACTION_BATT_CAL:
            if (s_cfg.battCalTrim == 0) {
                snprintf(buf, bufLen, "%s", TR("Battery Calibration: Off"));
            } else {
                char trimBuf[12];
                snprintf(buf, bufLen, TR("Battery Calibration: %s"),
                         battTrimText((int)s_cfg.battCalTrim, trimBuf, sizeof(trimBuf)));
            }
            break;
        case CFG_ACTION_NEIGHBOR_INFO:
            snprintf(buf, bufLen, TR("Neighborhood Info: %s"), s_cfg.neighborInfoEnabled ? TR("On") : TR("Off"));
            break;
        case CFG_ACTION_MESH_BEACON: {
            char both[48];
            const char *state = TR("Off");
            if (s_cfg.meshBeaconListen && s_cfg.meshBeaconBroadcast) {
                snprintf(both, sizeof(both), "%s \xC2\xB7 %s", TR("Listen"), TR("Broadcast"));
                state = both;
            } else if (s_cfg.meshBeaconListen) {
                state = TR("Listen");
            } else if (s_cfg.meshBeaconBroadcast) {
                state = TR("Broadcast");
            }
            if (lic::active() && s_cfg.meshBeaconBroadcast)
                state = s_cfg.meshBeaconListen ? TR("Listen") : TR("Off");
            snprintf(buf, bufLen, TR("Mesh Beacons: %s"), state);
        } break;
        case CFG_ACTION_CS_MODE:
            snprintf(buf, bufLen, TR("Chat Server: %s"),
                     lic::active()                                   ? TR("Off while licensed") :
                     s_cfg.chatServerMode == CHAT_SERVER_MODE_OFF    ? TR("Off") :
                     s_cfg.chatServerMode == CHAT_SERVER_MODE_MANUAL ? TR("Manual only")
                                                                     : TR("Automatic"));
            break;
        case CFG_ACTION_CS_SERVER: {
            char who[32];
            chatServerLabel(who, sizeof(who));
            snprintf(buf, bufLen, TR("Chat Server Node: %s"), who);
            break;
        }
        case CFG_ACTION_CS_CHECK:
            snprintf(buf, bufLen, "%s", TR("Check for Messages Now"));
            break;
        case CFG_ACTION_MQTT_TOGGLE:
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "%s", TR("MQTT Bridge: Off (WiFi off)"));
            } else {
                snprintf(buf, bufLen, TR("MQTT Bridge: %s"), s_cfg.mqttEnabled ? TR("On") : TR("Off"));
            }
            break;
        case CFG_ACTION_MSG_ALERT:
            snprintf(buf, bufLen, TR("Notification Sound: %s"), msgAlertSoundName(s_cfg.msgAlertSound));
            break;
        #if HAS_HAPTIC
        case CFG_ACTION_VIBRATION:
            snprintf(buf, bufLen, TR("Vibration: %s"), hapticStyleName(cfgHapticStyle(s_cfg)));
            break;
        #endif
        #if HAS_VOLUME_CONTROL
        case CFG_ACTION_VOLUME:
            snprintf(buf, bufLen, TR("Volume: %u%%"), (unsigned)s_cfg.volumePct);
            break;
        #endif
        #if HAS_NOTIFY_LED
        case CFG_ACTION_NOTIFY_LED_CHANNEL_COLOR:
            snprintf(buf, bufLen, TR("Channel Message LED: %s"),
                     notifyLedColorName(s_cfg.notifyLedColorChannel));
            break;
        case CFG_ACTION_NOTIFY_LED_DM_COLOR:
            snprintf(buf, bufLen, TR("Direct Message LED: %s"),
                     notifyLedColorName(s_cfg.notifyLedColorDm));
            break;
        #endif
        #if HAS_KB_BLINK
        case CFG_ACTION_KB_BLINK:
            snprintf(buf, bufLen, TR("Keyboard Blink: %s"), s_cfg.kbBlinkEnabled ? TR("On") : TR("Off"));
            break;
        case CFG_ACTION_KB_BLINK_CHAN_FLASHES:
            snprintf(buf, bufLen, TR("Blink Flashes (Channel): %u"),
                     (unsigned)cfgCoerceKbFlashes((int)s_cfg.kbBlinkChanFlashes));
            break;
        case CFG_ACTION_KB_BLINK_DM_FLASHES:
            snprintf(buf, bufLen, TR("Blink Flashes (DM): %u"),
                     (unsigned)cfgCoerceKbFlashes((int)s_cfg.kbBlinkDmFlashes));
            break;
        #endif
        #if HAS_LIGHT_NOTIFY
        case CFG_ACTION_NOTIFY_LIGHT_TIMEOUT:
            snprintf(buf, bufLen, TR("Notification Light Timeout: %s"),
                     TR(notifyLightTimeoutName(s_cfg.notifyLightTimeoutS)));
            break;
        #endif
        #if HAS_POWER_LED_LEVEL
        case CFG_ACTION_POWER_LED_LEVEL:
            snprintf(buf, bufLen, TR("Status Lights: %s"),
                     TR(powerLedLevelName(cfgCoercePowerLedPct(s_cfg.powerLedPct))));
            break;
        #endif
        #if HAS_KB_BACKLIGHT_LEVEL
        case CFG_ACTION_KB_BACKLIGHT_LEVEL:
#if defined(DEVICE_TDISPLAY_P4)
            // No level shown: it did not match what the keyboard was actually
            // doing, and F11 is where the brightness is changed on this board.
            snprintf(buf, bufLen, "%s", TR("Keyboard Light"));
#else
            snprintf(buf, bufLen, TR("Keyboard Light: %s"),
                     TR(kbBacklightLevelName(s_cfg.kbBacklightLevel)));
#endif
            break;
        #endif
        #if HAS_KB_LIGHT_TIMEOUT
        case CFG_ACTION_KB_LIGHT_TIMEOUT:
            snprintf(buf, bufLen, TR("Keyboard Light Timeout: %s"),
                     TR(kbLightTimeoutName(s_cfg.kbLightTimeoutS)));
            break;
        #endif
        case CFG_ACTION_SPLASH_MELODY:
            snprintf(buf, bufLen, TR("Splash Melody: %s"), s_cfg.splashMelodyEnabled ? TR("On") : TR("Off"));
            break;
        case CFG_ACTION_CHANNEL_CFG:
            snprintf(buf, bufLen, "%s", TR("Channel Configuration"));
            break;
        case CFG_ACTION_OTA_UPDATE:
            if (s_cfgOtaInstallArmed && s_cfgOtaLatestTag[0]) {
                snprintf(buf, bufLen, TR("Firmware Update: Install %s"), s_cfgOtaLatestTag);
            } else {
                snprintf(buf, bufLen, "%s", TR("Firmware Update Check"));
            }
            break;
        case CFG_ACTION_OTA_CHANNEL:
            // Resolved, not stored: an untouched device shows the channel its
            // running build puts it on, which is the thing the update check
            // will actually follow.
            //
            // The check a switch kicks off blocks the loop for as long as the
            // HTTP request takes, so the row says one is running before it
            // starts. Without it the UI just stops for up to 12 seconds with
            // nothing on screen accounting for it.
            snprintf(buf, bufLen, TR("Release Channel: %s%s"),
                     cfgOtaChannelName(otaResolveChannel(s_cfg.otaChannel)),
                     s_otaCheckRowNote);
            break;
        case CFG_ACTION_RELEASE_NOTES:
            snprintf(buf, bufLen, "%s", TR("Release Notes"));
            break;
        case CFG_ACTION_TIME_DATE:
            snprintf(buf, bufLen, TR("Time and Date: %s"),
                     (s_cfg.timeSource == TIME_SOURCE_MANUAL) ? TR("Manual") : TR("Auto"));
            break;
        case CFG_ACTION_CLOCK_FORMAT:
            snprintf(buf, bufLen, TR("Clock Format: %s"),
                     (s_cfg.clockFormat == CLOCK_FORMAT_12H) ? TR("12-hour") : TR("24-hour"));
            break;
        case CFG_ACTION_RESET_CHAT_COLORS:
            snprintf(buf, bufLen, "%s", TR("Reset Chat Colors"));
            break;
        case CFG_ACTION_CLEAR_MSGS:
            snprintf(buf, bufLen, "%s", TR("Clear Messages"));
            break;
#if HAS_FILE_STORAGE
        case CFG_ACTION_WARDRIVE:
            // The log's state rides on the row, for the same reason as the
            // archive below: it is the one place that can keep saying why
            // nothing is being written.
            snprintf(buf, bufLen, TR("Wardriving: %s%s"),
                     s_cfg.wardriveLogEnabled ? TR("On") : TR("Off"),
                     !s_cfg.wardriveLogEnabled ? ""
                     : !wardriveLogAvailable() ? TR(" (no storage)")
                     : wardriveLogIsFull()     ? TR(" (log full)")
                     : !gpsHasFix()            ? TR(" (no GPS fix)")
                     : "");
            break;
#endif
        case CFG_ACTION_ARCHIVE_NODES:
            // Same "(no card)" caveat as the row below, for the same reason:
            // with nothing to write to, an evicted node is simply dropped, and
            // the row is the only place that can keep saying so.
            snprintf(buf, bufLen, TR("Archive Dropped Nodes: %s%s"),
                     s_cfg.nodeArchiveEnabled ? TR("On") : TR("Off"),
                     (s_cfg.nodeArchiveEnabled && !nodeArchiveAvailable()) ? archiveNoStorageSuffix() : "");
            break;
        case CFG_ACTION_SHOW_ARCHIVED:
            // The card state is part of the label rather than a popup on the
            // toggle: switching this on with no card in the slot does nothing
            // visible on the Nodes screen, and the row is the only place that
            // can keep saying why.
            snprintf(buf, bufLen, TR("Show Archived Nodes: %s%s"),
                     s_cfg.nodeArchiveShow ? TR("On") : TR("Off"),
                     (s_cfg.nodeArchiveShow && !nodeArchiveAvailable()) ? archiveNoStorageSuffix() : "");
            break;
        case CFG_ACTION_CLEAR_NODES:
            snprintf(buf, bufLen, "%s", TR("Clear Nodes"));
            break;
        case CFG_ACTION_FACTORY_RESET:
            snprintf(buf, bufLen, "%s", TR("Factory Reset"));
            break;
#if HAS_SD_MALWARE_SCAN
        case CFG_ACTION_SD_SCAN:
            snprintf(buf, bufLen, TR("Scan SD Card for Malware%s"),
                     sdCardMounted() ? "" : TR(" (no card)"));
            break;
#endif
        default:
            snprintf(buf, bufLen, "%s", TR("(unknown)"));
            break;
    }
    return buf;
}

// A CFG row is greyed out and non-interactive when its precondition is unmet:
// both network consumers require WiFi, and the web-config portal is locked off
// while the MQTT bridge is on (they don't run together).
static bool cfgActionDisabled(int actionId) {
    if (cfgGenIsAction(actionId)) return cfgGenDisabled(actionId - CFG_ACTION_GEN_FIRST, nullptr);
    switch (actionId) {
        case CFG_ACTION_MQTT_TOGGLE: return !s_cfg.wifiEnabled;
        case CFG_ACTION_CS_MODE:     return lic::active();
        case CFG_ACTION_CS_CHECK:    return lic::active() || s_cfg.chatServerMode == CHAT_SERVER_MODE_OFF;
#if HAS_SD_MALWARE_SCAN
        // Nothing to look at. The row stays visible rather than disappearing:
        // "no card" is the answer to the question someone opened this to ask.
        case CFG_ACTION_SD_SCAN: return !sdCardMounted();
#endif
        case CFG_ACTION_WEBCFG:
            return !s_cfg.wifiEnabled;
#if HAS_FILE_STORAGE
        // Nowhere to write: greyed out, but never while on, so it can always
        // be switched off again.
        case CFG_ACTION_WARDRIVE:
            return !s_cfg.wardriveLogEnabled && !wardriveLogAvailable();
#endif
#if HAS_VNC_HOST
        // Keep an active row usable after a transient link drop so VNC can
        // still be turned off. Only activation requires a live station.
        case CFG_ACTION_VNC_HOST: return !s_vncEnabled && !vncNetworkConnected();
#endif
#if HAS_BLE_KEYBOARD
        // Scanning needs the radio up, and the radio only comes up with the
        // feature switched on.
        case CFG_ACTION_BLE_KBD_PAIR: return !s_cfg.bleKbdEnabled;
#endif
        default:                     return false;
    }
}

// Names for the canonical Meshtastic Role enum, so an imported or legacy value
// reads as itself rather than as a number. Only the handful cfgCoerceDeviceRole
// admits are reachable from a setting; the rest are here so a config that
// arrived carrying one is named honestly before it is coerced.
static const char *cfgDeviceRoleName(uint8_t role) {
    switch (role) {
        case 0:  return "CLIENT";
        case 1:  return "CLIENT_MUTE";
        // 2 read "CLIENT_HIDDEN_MQTT", which is not a Meshtastic role at all and
        // disagreed with kRoleNames[] in config_io.cpp. ROUTER is what the
        // protobuf calls this position.
        case 2:  return "ROUTER";
        case 3:  return "ROUTER_CLIENT";
        case 4:  return "REPEATER";
        case 5:  return "TRACKER";
        case 6:  return "SENSOR";
        case 7:  return "TAK";
        case 8:  return "CLIENT_HIDDEN";
        case 9:  return "LOST_AND_FOUND";
        case 10: return "TAK_TRACKER";
        case 11: return "ROUTER_LATE";
        case 12: return "CLIENT_BASE";
        default: return "UNKNOWN";
    }
}

static bool cfgActionNeedsConfirm(int actionId) {
    // Orientation used to be here: it rebooted the instant it was pressed, so
    // it had to say so first. It opens a picker now, and the picker names every
    // destination and says it reboots -- so the confirm had become a second
    // question on top of a deliberate choice, which is the kind people learn to
    // dismiss without reading.
    if (cfgGenIsAction(actionId)) return cfgGenNeedsConfirm(actionId - CFG_ACTION_GEN_FIRST);
    return
        // Reboots to apply.
        actionId == CFG_ACTION_MQTT_TOGGLE
        // Unless a picker comes first: it names the file, and the import
        // picker asks again before it reboots.
        || (actionId == CFG_ACTION_EXPORT && !cfgExportOffersStamp())
        || (actionId == CFG_ACTION_IMPORT && !cfgImportHasChoice())
        || actionId == CFG_ACTION_OTA_UPDATE
    || actionId == CFG_ACTION_CLEAR_MSGS
        || actionId == CFG_ACTION_CLEAR_NODES
        || actionId == CFG_ACTION_FACTORY_RESET;
}

// The sentence the confirmation asks. Returns false for the actions that are
// happy quoting their own row label, which is most of them.
//
// This exists because the default is wrong for a *toggle*. Quoting the label
// works while the label is a verb -- "Export Config", "Factory Reset" -- but a
// toggle's label is a state readout, so the dialog ends up naming the state
// being left while Yes applies the state being entered. With the bridge off,
// "MQTT Bridge: Off" reads as "turn it off?" and Yes turns it on.
//
// A hook rather than a special case inside the one caller: any toggle added to
// cfgActionNeedsConfirm() later inherits exactly the same bug, and this is the
// place a fix for it already exists.
static bool cfgActionConfirmSentence(int actionId, char *buf, size_t bufLen) {
    if (cfgGenIsAction(actionId)) {
        cfgGenConfirmSentence(actionId - CFG_ACTION_GEN_FIRST, buf, bufLen);
        return true;
    }
    switch (actionId) {
        case CFG_ACTION_MQTT_TOGGLE:
            // Phrased from the destination, and it names the reboot -- which the
            // row does and the old prompt never mentioned, leaving the reboot to
            // arrive as a surprise after the one question the user was asked.
            snprintf(buf, bufLen, TR("Turn MQTT Bridge %s and reboot?"),
                     s_cfg.mqttEnabled ? TR("off") : TR("on"));
            return true;
        case CFG_ACTION_CLEAR_NODES:
            // The dialog carries the two options under this, so the sentence
            // names only what is always true.
            snprintf(buf, bufLen, "%s", TR("Clear nodes and reboot?"));
            return true;
        default:
            return false;
    }
}

static void cfgDebugSelection(const char *tag, int actionId) {
    if (!s_cfgDebugLog) return;
    char actionText[80];
    Serial.printf("[lvgl-cfg] %s sel=%d action=%d label=\"%s\"\n",
                  tag,
                  s_cfgSelection,
                  actionId,
                  cfgActionLabel(actionId, actionText, sizeof(actionText)));
}

// Single mover for every config-screen navigation input — the keyboard arrows,
// j/k, the pager wheel and the expander buttons all fold onto KEY_SCROLL_UP/DN
// before they get here, so wrapping once covers all of them. The list is long
// enough that walking it end to end to reach the last item is the common case.
#if UI_CFG_SECTIONS
// One step across the section grid. Sideways walks the buttons in reading
// order and wraps, so Right from the end of a row lands on the start of the
// next. Up and Down keep the column and wrap top to bottom; Down from above a
// short last row lands on that row's last button rather than going nowhere.
static void cfgGridStep(int dRow, int dCol) {
    const int n = s_cfgGridCount;
    if (n <= 0) return;
    const int cols = s_cfgGridCols > 0 ? s_cfgGridCols : 1;
    const int cur = s_cfgGridSelection;
    int next = cur;
    if (dCol != 0) {
        next = ((cur + dCol) % n + n) % n;
    } else if (dRow > 0) {
        const int lastRow = (n - 1) / cols;
        next = cur + cols;
        if (next >= n) next = (cur / cols < lastRow) ? n - 1 : cur % cols;
    } else if (dRow < 0) {
        next = cur - cols;
        if (next < 0) {
            next = ((n - 1) / cols) * cols + cur % cols;   // same column, last row
            if (next >= n) next -= cols;                   // short last row
        }
    }
    if (next == cur) return;
    s_cfgGridSelection = next;
    s_cfgLastScrollMs = millis();
    refreshCfgGridSelection();
}
#endif

static void cfgMoveSelection(int delta, const char *tag) {
#if UI_CFG_SECTIONS
    if (cfgGridShown()) {
#if CFG_GRID_FOUR_WAY
        // Up/Down move a row; Left/Right arrive separately (the config key
        // handler) and move one button.
        cfgGridStep(delta, 0);
#else
        // Every other board walks the buttons left to right and then down,
        // the order they read in: their wheels and paired keys have only the
        // one axis, and stepping a row at a time would leave buttons they
        // could never reach.
        cfgGridStep(0, delta);
#endif
        return;
    }
#endif
    if (s_cfgActionCount <= 0) return;
    int next = s_cfgSelection + delta;
    // Modulo rather than a clamp: past the last item lands on the first.
    next %= s_cfgActionCount;
    if (next < 0) next += s_cfgActionCount;
    if (next == s_cfgSelection) return;
    s_cfgSelection = next;
    s_cfgLastScrollMs = millis();
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    cfgDebugSelection(tag, s_cfgActions[s_cfgSelection]);
    // Repaint, not rebuild: only the highlight moved. See refreshCfgSelection().
    refreshCfgSelection();
}

// Rebuilds the row list for the current filter and puts the selection back at
// the top. Keeping the old index would land on an unrelated row, since the list
// it indexed into no longer exists.
static void cfgApplyFilter() {
    initCfgActions();
    s_cfgSelection = 0;
    s_cfgConfirmAction = -1;   // a pending confirmation is about a row that may be gone
    s_cfgConfirmMs = 0;
    refreshCfgModal();
}

static void syncWifiCredsToPrefs() {
    if (!s_cfg.wifiSsid[0]) return;
    // Credentials live inside the settings blob; saving them means saving it.
    markConfigDirty();
}

static void persistWebCfgEnabled() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool("webCfgEnabled", s_webCfgEnabled);
    p.end();
}

#if HAS_BLE_KEYBOARD
// ── Web config and the BLE keyboard are mutually exclusive ──────────────────
// Not a preference: the two genuinely cannot share this hardware. Web config
// turns Wi-Fi modem power-save off, because the synchronous WebServer stalls on
// DTIM-buffered packets; Espressif's Wi-Fi/BT coexistence requires modem sleep
// to be on. Bringing the BT controller up in that state does not fail with an
// error code -- it calls abort() from inside coex_core_enable() and takes the
// device down with it.
//
// Coexistence with modem sleep forced on does work, but it makes every web page
// slow enough to look broken. So whichever the user switches on wins, the other
// is switched off and persisted off, and they are told which and why. Turning
// one off does not bring the other back: that would be a second surprise, and
// the row they want is one press away.
//
// Both helpers return true when they actually stopped something, so the caller
// knows whether to raise the notice.
static bool bleKbdStopWebConfigForBt() {
#if defined(PLATFORM_LINUX)
    // The OS runs Wi-Fi and Bluetooth on separate stacks; nothing to share.
    return false;
#endif
    const bool wasOn = s_webCfgEnabled || webCfgRunning();
    if (!wasOn) return false;
    Serial.println("[blekbd] stopping web config: cannot share the radio with Bluetooth");
    s_webCfgEnabled = false;
    persistWebCfgEnabled();
    if (webCfgRunning()) webCfgEnd();   // tears the radio down
    s_wifiStaKickMs = 0;                // re-associate the station right away
    return true;
}

static bool webCfgStopBleKeyboardForWeb() {
#if defined(PLATFORM_LINUX)
    return false;
#endif
    if (!s_cfg.bleKbdEnabled && !bleKeyboardEnabled()) return false;
    Serial.println("[web] stopping BLE keyboard: cannot share the radio with web config");
    bleKeyboardSetEnabled(false);
    s_cfg.bleKbdEnabled = false;
    markConfigDirty();
    return true;
}

static const char *const kBleKbdWebExclusiveNotice = TR_NOOP(
    "Web Config stopped.\n\n"
    "The Bluetooth keyboard and Web Config share one radio and cannot run at "
    "the same time.\n\n"
    "Turn the keyboard off to use Web Config again.");
static const char *const kWebBleKbdExclusiveNotice = TR_NOOP(
    "BT Keyboard turned off.\n\n"
    "Web Config and the Bluetooth keyboard share one radio and cannot run at "
    "the same time.\n\n"
    "Turn Web Config off to use the keyboard again.");
#endif

#if HAS_VNC_HOST
// The PIN last put in front of the user, so serviceVncPinNotice() shows each
// new one once: on a web-side enable, or after wrong guesses redraw it.
static uint32_t s_vncPinNoticeSerial = 0;
static uint32_t s_vncPinNoticeSession = 0;

static void persistVncEnabled() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool("vncEnabled", s_vncEnabled);
    p.end();
}

static bool applyVncEnabled(bool enabled) {
    if (enabled) {
        if (!vncNetworkConnected()) return false;
        if (!vncHostEnabled() && !vncHostSetEnabled(true)) return false;
    } else if (vncHostEnabled()) {
        (void)vncHostSetEnabled(false);
    }

    if (s_vncEnabled != enabled) {
        s_vncEnabled = enabled;
        persistVncEnabled();
    }
    return true;
}
#endif

// The picker's "AP" choice survives reboots; other picker overrides remain
// runtime-only, since they hold credentials that already live in config.
static void persistWifiForceAp() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool("wifiForceAp", wifiForceApMode());
    p.end();
}

// NVS keys are limited to 15 chars on ESP32.
static constexpr const char *kPrefSkipWebAutoOnce = "skipWebAuto1";
static constexpr const char *kPrefOtaWorkerOnce = "otaWorker1";
static constexpr uint32_t kOtaWorkerRtcMagic = 0x4F544131UL;  // "OTA1"
RTC_DATA_ATTR static uint32_t s_otaWorkerRtcFlag = 0;

static void setOtaWorkerBootNotice(const char *msg) {
    if (!msg) msg = "";
    strncpy(s_otaWorkerBootNotice, msg, sizeof(s_otaWorkerBootNotice) - 1);
    s_otaWorkerBootNotice[sizeof(s_otaWorkerBootNotice) - 1] = '\0';
}

static void requestSkipWebAutoStartOnce() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool(kPrefSkipWebAutoOnce, true);
    p.end();
}

static bool consumeSkipWebAutoStartOnce() {
    Preferences p;
    if (!p.begin("camillia", false)) return false;
    bool skip = p.getBool(kPrefSkipWebAutoOnce, false);
    if (skip) {
        p.putBool(kPrefSkipWebAutoOnce, false);
    }
    p.end();
    return skip;
}

static bool requestOtaWorkerModeOnce() {
    // RTC survives software reboot and gives us a second, low-friction path
    // when NVS writes are temporarily unreliable.
    s_otaWorkerRtcFlag = kOtaWorkerRtcMagic;

    Preferences p;
    if (!p.begin("camillia", false)) return true;
    p.putBool(kPrefOtaWorkerOnce, true);
    p.end();

    Preferences v;
    if (!v.begin("camillia", true)) return true;
    bool verify = v.getBool(kPrefOtaWorkerOnce, false);
    v.end();
    return verify || (s_otaWorkerRtcFlag == kOtaWorkerRtcMagic);
}

static bool isOtaWorkerModeRequestedRtc() {
    return s_otaWorkerRtcFlag == kOtaWorkerRtcMagic;
}

static bool consumeOtaWorkerModeRtcOnce() {
    bool run = (s_otaWorkerRtcFlag == kOtaWorkerRtcMagic);
    if (run) {
        s_otaWorkerRtcFlag = 0;
    }
    return run;
}

static bool isOtaWorkerModeRequestedOnce() {
    Preferences p;
    if (!p.begin("camillia", true)) return false;
    bool run = p.getBool(kPrefOtaWorkerOnce, false);
    p.end();
    return run;
}

static bool consumeOtaWorkerModeOnce() {
    Preferences p;
    if (!p.begin("camillia", false)) return false;
    bool run = p.getBool(kPrefOtaWorkerOnce, false);
    if (run) {
        p.putBool(kPrefOtaWorkerOnce, false);
    }
    p.end();
    return run;
}

// Why a non-factory flash layout gets no update: Launcher-installed devices are
// told where updates come from instead.
static const char *otaLayoutBlockedReason() {
    return otaInstalledByLauncher() ? TR("Update with Launcher (installed by Launcher)")
                                    : TR("OTA needs the factory image (flash layout)");
}

// The gates a release check cannot get past no matter who asked for it, as
// opposed to the boot check's own policy (once per boot, and only if the user
// wants one). Returns null when a check can proceed, otherwise a reason short
// enough to drop straight into s_cfgStatus.
static const char *otaCheckBlockedReason() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Unreachable in practice -- the row that calls this is not built on the
    // Cardputer -- but performCfgAction() still compiles the case, so it needs
    // an answer that matches the rest of the build's OTA story.
    return "OTA disabled on Cardputer build";
#else
    // A third-party installer's layout keeps another firmware in the slot an
    // update would land in, so there is nothing worth checking for.
    if (!otaLayoutSupportsUpdate()) return otaLayoutBlockedReason();
    if (!s_cfg.wifiEnabled) return TR("Update check needs WiFi on");
    // Also the AP/web-config case: the station is not associated there, so
    // there is no route to the release proxy.
    if (WiFi.status() != WL_CONNECTED) return TR("Update check needs WiFi connected");
    return nullptr;
#endif
}

// Asks the loop service for a release check. Deliberately does not run one
// here: otaCheckLatestRelease() is a synchronous HTTP GET with a 12 s timeout,
// so calling it from a keypress handler would freeze the UI before anything
// explaining the freeze had been painted. Requesting while one is already
// pending re-arms the settle window rather than queueing a second check.
static void otaRequestCheckNow() {
    s_otaCheckRequested = true;
    s_otaCheckRequestedDueMs = millis() + kOtaManualCheckSettleMs;
    s_otaCheckDeadlineMs = millis() + kOtaCheckRequestLifetimeMs;
    utf8util::copyTruncate(s_otaCheckRowNote, sizeof(s_otaCheckRowNote), TR(" - checking..."));
}

static bool s_otaWorkerUiReady = true;

// The stem of the title every install-phase draw carries. Named rather than
// repeated as a literal because the e-paper path below matches on it to decide
// which state is the long one, and a typo in either place would silently stop
// matching. It is a *prefix* now, not the whole title -- see below.
static const char kOtaInstallTitle[] = "Installing update";

// The title actually drawn: the stem plus the release being written. The panel
// is the only place a user can see which version their device is about to
// become, and during an install it is also the only thing on screen that could
// tell them the update they asked for is the update they are getting.
//
// File scope rather than a local, because the progress callback is a
// capture-less lambda (it has to convert to a plain function pointer) and so
// cannot carry it in. Composed once per install, before the transfer starts, so
// every draw for that install sees the same string -- which is what keeps the
// chrome dedupe below collapsing them into one paint.
static char s_otaInstallTitle[80] = {};

static const char *otaComposeInstallTitle(const char *tag) {
    if (tag && tag[0]) {
        snprintf(s_otaInstallTitle, sizeof(s_otaInstallTitle), "%s %s", kOtaInstallTitle, tag);
    } else {
        // No tag to name: the bare stem reads correctly on its own, where
        // "Installing update latest" would not.
        snprintf(s_otaInstallTitle, sizeof(s_otaInstallTitle), "%s", kOtaInstallTitle);
    }
    return s_otaInstallTitle;
}

#if defined(DEVICE_TDECK_PRO)
// ── The Pro's OTA screen ────────────────────────────────────────────────────
// The shared drawing below does not work on this board, for two separate
// reasons. It paints white text on a flooded black screen, which on e-paper
// means driving every pixel to black — slow, and the inverse of the
// black-on-white the rest of this build uses. And once LVGL has flushed a
// frame, serviceRefresh() ignores the canvas entirely (see refreshFromCanvas),
// so a UI-triggered update drew its whole progress sequence into a buffer
// nobody read: the panel just held the config screen until the device rebooted.
//
// The replacement is deliberately static. A GDEQ031T10 update blocks for about
// 700 ms, so the 180 ms progress-bar cadence would have the panel refreshing
// through the entire download instead of downloading — and a percentage that
// costs two thirds of a second to show is worse than no percentage at all. One
// screen goes up when the install starts and stays up, untouched, until the
// device reboots into the new firmware.
static char s_otaProShownHeadline[48] = {};

static void otaProDrawScreen(const char *headline, const char *detail) {
    if (!s_otaWorkerUiReady) return;

    displayDev().fillScreen(TFT_WHITE);
    displayDev().setTextColor(TFT_BLACK, TFT_WHITE);
    displayDev().setTextSize(1);

    const int w = (int)displayDev().width();
    auto centred = [w](const char *text) {
        const int textW = (int)displayDev().textWidth(text);
        return (w > textW) ? ((w - textW) / 2) : 4;
    };

    int y = 128;   // roughly centred on the 320-tall panel, headline first
    if (headline && headline[0]) {
        displayDev().setFont(&fonts::DejaVu18);
        displayDev().drawString(headline, centred(headline), y);
        y += 36;
    }

    displayDev().setFont(&fonts::DejaVu12);
    if (detail && detail[0]) {
        displayDev().drawString(detail, centred(detail), y);
        y += 26;
    }

    // The screen stands still for minutes with no progress indication of any
    // kind, which is exactly when someone decides the device has hung and pulls
    // the power — mid-flash, the one moment it must not happen.
    const char *warn = "Keep the device powered.";
    displayDev().drawString(warn, centred(warn), y);

    lcd.refreshFromCanvas(true);
}

// Repaints only when the state actually changes. Progress ticks all arrive
// under the same headline, so they collapse into the single paint that started
// the install; `detail` is ignored for that decision because it moves
// constantly during a transfer ("Starting download...", "Downloading...") and
// says nothing the static screen does not already say.
static void otaProShowState(const char *headline, const char *detail) {
    if (!headline) headline = "";
    if (strncmp(s_otaProShownHeadline, headline, sizeof(s_otaProShownHeadline) - 1) == 0) {
        return;
    }
    snprintf(s_otaProShownHeadline, sizeof(s_otaProShownHeadline), "%s", headline);
    otaProDrawScreen(headline, detail);
}

// Install is the state worth spelling out: it is the only one that lasts
// minutes, and it is the one the panel is really there to explain. Every other
// state keeps the caller's own wording, which is already short enough to fit.
static const char *otaProHeadlineFor(const char *title) {
    // Prefix, not equality: the install title now carries the release tag, and an
    // exact match would stop firing the moment it did -- leaving the Pro showing
    // the raw title instead of its own wording.
    if (title && strncmp(title, kOtaInstallTitle, sizeof(kOtaInstallTitle) - 1) == 0) {
        return "Firmware updating...";
    }
    return (title && title[0]) ? title : "Firmware updating...";
}
#endif

#if defined(DEVICE_TDECK_PRO)
// The Pro's screen is static by design (see above): there is no incremental
// render state to throw away, so the invalidation the TFT path needs is a no-op
// here and the call sites do not have to care which board they are on.
static inline void otaWorkerInvalidateScreen() {}
#else
// ── The TFT install screen's render cache ───────────────────────────────────
// The install screen is redrawn about 5.5 times a second for the whole download
// and almost nothing on it changes between ticks: a few pixels of bar and three
// short strings. Wiping to black and repainting all of it costs a full frame of
// blocking SPI — ~31 ms at TFT_SPI_WRITE_HZ 40 MHz, ~17% of the download's wall
// time — inside the loop that is supposed to be draining the socket. It also
// blinks the whole panel at 5.5 Hz.
//
// So the screen is split in two: chrome (background, title, bar outline) is
// painted once and keyed on the title, and each field below remembers what it
// last put on the panel and skips the draw when the answer has not moved. The
// state is all fixed-size POD in .bss — this path exists precisely so the TLS
// handshake gets the largest contiguous internal DRAM it can, and it must not
// add a single heap allocation.
struct OtaUiTextRow {
    char     text[160];   // sized for the longest detail drawn: the err[] buffer
    uint16_t color;
    int      width;       // pixels the cached string covered, for tail clearing
    bool     valid;
};

static bool s_otaUiChromeValid = false;
static char s_otaUiTitle[sizeof(s_otaInstallTitle)] = {};
static int  s_otaUiLastFillW = 0;
static bool s_otaUiBarStalled = false;
static OtaUiTextRow s_otaUiPctRow = {};
static OtaUiTextRow s_otaUiBytesRow = {};
static OtaUiTextRow s_otaUiDetailRow = {};

// Geometry is derived from the live panel height so the whole OTA block sits
// vertically centred in either orientation. Typography grows from the old
// DejaVu12 to 18 px on normal panels and 24 px on the high-DPI P4; the one
// sub-180 px panel keeps 12 px so every row still fits.
static constexpr int kOtaUiBarX = 8;
static constexpr int kOtaUiTextX = 8;
static constexpr int kOtaRhinoW = 24;
static constexpr int kOtaRhinoH = 13;
static constexpr int kOtaRhinoStep = 6;

static void otaWorkerUseLargeFont() {
    displayDev().setTextSize(1);
    if ((int)displayDev().height() < 180) {
        displayDev().setFont(&fonts::DejaVu12);
        return;
    }
#if defined(DEVICE_TDISPLAY_P4)
    displayDev().setFont(&fonts::DejaVu24);
#else
    displayDev().setFont(&fonts::DejaVu18);
#endif
}

struct OtaUiLayout {
    int top;
    int fontH;
    int barY;
    int barH;
    int pctY;
    int bytesY;
    int detailY;
    int rhinoY;
};

static OtaUiLayout otaWorkerLayout() {
    otaWorkerUseLargeFont();
    const int fontH = (int)displayDev().fontHeight();
    const int barH = fontH + 6;
    const int contentH = fontH + 12 + barH + 10
                       + fontH * 3 + 8 * 3 + kOtaRhinoH;
    const int top = max(4, ((int)displayDev().height() - contentH) / 2);
    const int barY = top + fontH + 12;
    const int pctY = barY + barH + 10;
    const int bytesY = pctY + fontH + 8;
    const int detailY = bytesY + fontH + 8;
    return {
        top, fontH, barY, barH, pctY, bytesY, detailY,
        detailY + fontH + 8
    };
}

static inline int otaWorkerBarW() {
    return max(120, (int)displayDev().width() - 16);
}

// ── The rhino ────────────────────────────────────────────────────────────────
// Runs back and forth under the detail line for the length of the download.
// Purely decorative, and cheap enough to stay that way: an opaque 24x13 blit is
// 624 bytes of pixels, about 0.13 ms at 40 MHz, against the ~31 ms full-frame
// wipe this screen used to pay on every tick. Only the strip the sprite vacates
// is cleared, so there is no flicker and no second pass over the pixels it
// still covers.
//
// It takes one step per progress callback that reaches it, so it runs at
// whatever rate the download is reporting -- about 5.5 steps a second while
// bytes are moving, and a crawl once the transfer stalls. That is not a bug: a
// rhino slowing to a stop is a fair reading of what the panel is already
// saying in words.
//
// 1 bpp, MSB first, 24 px = exactly three bytes a row with no padding. The
// source art is kept beside each row because that is the only form of it anyone
// can edit.
static const uint8_t kOtaRhinoRightA[] = {
    0x00, 0x00, 0x00,   // ........................
    0x00, 0x00, 0x02,   // ......................#.
    0x00, 0x00, 0x06,   // .....................##.
    0x00, 0x00, 0x8C,   // ................#...##..
    0x20, 0x00, 0xDC,   // ..#.............##.###..
    0x33, 0xFF, 0xBE,   // ..##..###########.#####.
    0x7F, 0xFF, 0xFF,   // .#######################
    0x7F, 0xFF, 0xFF,   // .#######################
    0x7F, 0xFF, 0xFE,   // .######################.
    0x3F, 0xFF, 0xFC,   // ..####################..
    0x3B, 0x81, 0xDC,   // ..###.###......###.###..
    0x3B, 0x81, 0xDC,   // ..###.###......###.###..
    0x33, 0x01, 0x98,   // ..##..##.......##..##...
};

static const uint8_t kOtaRhinoRightB[] = {
    0x00, 0x00, 0x00,   // ........................
    0x00, 0x00, 0x02,   // ......................#.
    0x00, 0x00, 0x06,   // .....................##.
    0x00, 0x00, 0x8C,   // ................#...##..
    0x20, 0x00, 0xDC,   // ..#.............##.###..
    0x33, 0xFF, 0xBE,   // ..##..###########.#####.
    0x7F, 0xFF, 0xFF,   // .#######################
    0x7F, 0xFF, 0xFF,   // .#######################
    0x7F, 0xFF, 0xFE,   // .######################.
    0x3F, 0xFF, 0xFC,   // ..####################..
    0x71, 0xC3, 0x8E,   // .###...###....###...###.
    0x71, 0xC3, 0x8E,   // .###...###....###...###.
    0xC0, 0xC3, 0x06,   // ##......##....##.....##.
};

static const uint8_t kOtaRhinoLeftA[] = {
    0x00, 0x00, 0x00,   // ........................
    0x40, 0x00, 0x00,   // .#......................
    0x60, 0x00, 0x00,   // .##.....................
    0x31, 0x00, 0x00,   // ..##...#................
    0x3B, 0x00, 0x04,   // ..###.##.............#..
    0x7D, 0xFF, 0xCC,   // .#####.###########..##..
    0xFF, 0xFF, 0xFE,   // #######################.
    0xFF, 0xFF, 0xFE,   // #######################.
    0x7F, 0xFF, 0xFE,   // .######################.
    0x3F, 0xFF, 0xFC,   // ..####################..
    0x3B, 0x81, 0xDC,   // ..###.###......###.###..
    0x3B, 0x81, 0xDC,   // ..###.###......###.###..
    0x19, 0x80, 0xCC,   // ...##..##.......##..##..
};

static const uint8_t kOtaRhinoLeftB[] = {
    0x00, 0x00, 0x00,   // ........................
    0x40, 0x00, 0x00,   // .#......................
    0x60, 0x00, 0x00,   // .##.....................
    0x31, 0x00, 0x00,   // ..##...#................
    0x3B, 0x00, 0x04,   // ..###.##.............#..
    0x7D, 0xFF, 0xCC,   // .#####.###########..##..
    0xFF, 0xFF, 0xFE,   // #######################.
    0xFF, 0xFF, 0xFE,   // #######################.
    0x7F, 0xFF, 0xFE,   // .######################.
    0x3F, 0xFF, 0xFC,   // ..####################..
    0x71, 0xC3, 0x8E,   // .###...###....###...###.
    0x71, 0xC3, 0x8E,   // .###...###....###...###.
    0x60, 0xC3, 0x03,   // .##.....##....##......##
};

static int     s_otaRhinoX = -1;     // left edge in panel coords; -1 = not placed
static int     s_otaRhinoDir = 1;    // +1 running right, -1 running left
static uint8_t s_otaRhinoFrame = 0;  // alternates the legs

static void otaWorkerRunRhino(int y) {
    const int minX = 8;
    const int maxX = (int)displayDev().width() - 8 - kOtaRhinoW;
    // A panel with no room for the track, or none below the detail row, simply
    // does not get a rhino. Everything above it still draws.
    if (maxX <= minX) return;
    if (y + kOtaRhinoH > (int)displayDev().height()) return;

    const int prevX = s_otaRhinoX;
    if (prevX < 0) {
        s_otaRhinoX = minX;
        s_otaRhinoDir = 1;
    } else {
        s_otaRhinoX += s_otaRhinoDir * kOtaRhinoStep;
        if (s_otaRhinoX >= maxX) {
            s_otaRhinoX = maxX;
            s_otaRhinoDir = -1;
        } else if (s_otaRhinoX <= minX) {
            s_otaRhinoX = minX;
            s_otaRhinoDir = 1;
        }
        s_otaRhinoFrame ^= 1;
    }

    // Direction is read after the turn, so at each end it faces the way it is
    // about to go -- which renders as the rhino turning around rather than
    // moonwalking back across the panel.
    const uint8_t *bits;
    if (s_otaRhinoDir > 0) bits = s_otaRhinoFrame ? kOtaRhinoRightB : kOtaRhinoRightA;
    else                   bits = s_otaRhinoFrame ? kOtaRhinoLeftB  : kOtaRhinoLeftA;

    displayDev().drawBitmap(s_otaRhinoX, y, bits, kOtaRhinoW, kOtaRhinoH,
                            TFT_WHITE, TFT_BLACK);

    // Clear only what it left behind. The opaque blit above has already
    // repainted everything the sprite still covers, so wiping the whole track
    // first would be both wasted pixels and a visible blink.
    if (prevX >= 0 && prevX != s_otaRhinoX) {
        if (s_otaRhinoX > prevX) {
            displayDev().fillRect(prevX, y, s_otaRhinoX - prevX, kOtaRhinoH, TFT_BLACK);
        } else {
            displayDev().fillRect(s_otaRhinoX + kOtaRhinoW, y,
                                  prevX - s_otaRhinoX, kOtaRhinoH, TFT_BLACK);
        }
    }
}

// Anything that repaints the panel out from under this cache has to say so, or
// the next progress tick draws a delta against a screen that no longer holds
// what the delta was measured from.
static void otaWorkerInvalidateScreen() {
    s_otaUiChromeValid = false;
    s_otaUiTitle[0] = '\0';
    s_otaUiLastFillW = 0;
    s_otaUiBarStalled = false;
    s_otaUiPctRow.valid = false;
    s_otaUiBytesRow.valid = false;
    s_otaUiDetailRow.valid = false;
    s_otaRhinoX = -1;
    s_otaRhinoDir = 1;
    s_otaRhinoFrame = 0;
}

static inline bool otaWorkerRowChanged(const OtaUiTextRow &row, const char *text, uint16_t color) {
    return !row.valid || row.color != color || strcmp(row.text, text) != 0;
}

// One text row, drawn only when its rendered value or its colour actually moved.
// setTextColor(fg, bg) paints an opaque background, so a string at least as wide
// as the last one erases it for free; a shorter one leaves a tail, which is all
// the fillRect here is for — a few hundred bytes of pixels against the ~150 KB a
// fillScreen costs.
static void otaWorkerDrawTextRow(OtaUiTextRow &row, int y, const char *text, uint16_t color) {
    if (!text) text = "";
    if (!otaWorkerRowChanged(row, text, color)) return;

    const int newW = (int)displayDev().textWidth(text);
    displayDev().setTextColor(color, TFT_BLACK);
    displayDev().drawString(text, kOtaUiTextX, y);
    if (row.valid && row.width > newW) {
        displayDev().fillRect(kOtaUiTextX + newW,
                              y,
                              row.width - newW,
                              displayDev().fontHeight() + 2,
                              TFT_BLACK);
    }

    snprintf(row.text, sizeof(row.text), "%s", text);
    row.color = color;
    row.width = newW;
    row.valid = true;
}

// The one paint still allowed to cost a full frame, because it only happens on a
// phase change: the first progress draw of an install attempt, or a title change
// — which is how the "OTA install failed" screen gets the whole panel it wants.
static void otaWorkerDrawProgressChrome(const char *title) {
    const OtaUiLayout layout = otaWorkerLayout();
    displayDev().startWrite();
    displayDev().fillScreen(TFT_BLACK);
    otaWorkerUseLargeFont();
    displayDev().setTextColor(TFT_WHITE, TFT_BLACK);
    if (title && title[0]) displayDev().drawString(title, kOtaUiTextX, layout.top);
    displayDev().drawRect(kOtaUiBarX, layout.barY,
                          otaWorkerBarW(), layout.barH, TFT_WHITE);
    displayDev().endWrite();

    otaWorkerInvalidateScreen();
    snprintf(s_otaUiTitle, sizeof(s_otaUiTitle), "%s", title ? title : "");
    s_otaUiChromeValid = true;
}
#endif

static void otaWorkerDrawStatus(const char *line1, const char *line2 = nullptr) {
    if (!s_otaWorkerUiReady) return;
#if defined(DEVICE_TDECK_PRO)
    otaProShowState(otaProHeadlineFor(line1), line2);
    return;
#else
    // This one legitimately wipes the panel, which leaves nothing of the
    // progress screen the cache below thinks is still up there.
    otaWorkerInvalidateScreen();

    displayDev().startWrite();
    displayDev().fillScreen(TFT_BLACK);
    displayDev().setTextColor(TFT_WHITE, TFT_BLACK);
    otaWorkerUseLargeFont();

    const int fontH = (int)displayDev().fontHeight();
    const int lineCount = ((line1 && line1[0]) ? 1 : 0)
                        + ((line2 && line2[0]) ? 1 : 0);
    const int blockH = lineCount > 0 ? lineCount * fontH + (lineCount - 1) * 10 : 0;
    int y = max(4, ((int)displayDev().height() - blockH) / 2);
    if (line1 && line1[0]) {
        displayDev().drawString(line1, 8, y);
        y += fontH + 10;
    }
    if (line2 && line2[0]) {
        displayDev().drawString(line2, 8, y);
    }
    displayDev().endWrite();
#endif
}

static void otaWorkerDrawProgress(const char *title,
                                  const char *detail,
                                  size_t written,
                                  size_t total,
                                  bool stalled) {
    if (!s_otaWorkerUiReady) return;
#if defined(DEVICE_TDECK_PRO)
    // Byte counts, percentages and the stall flag are all dropped here: none of
    // them can be shown without a 700 ms panel refresh per update.
    (void)written;
    (void)total;
    (void)stalled;
    otaProShowState(otaProHeadlineFor(title), detail);
    return;
#else
    // A new title means a new phase, and the phase screens do not share a
    // layout — that is what earns the full repaint here, and it is the only
    // thing that does.
    if (!s_otaUiChromeValid ||
        strncmp(s_otaUiTitle, title ? title : "", sizeof(s_otaUiTitle) - 1) != 0) {
        otaWorkerDrawProgressChrome(title);
    }

    const OtaUiLayout layout = otaWorkerLayout();
    otaWorkerUseLargeFont();

    // The rhino runs on the install screen only; the failure screen keeps the
    // panel still. Ahead of the change test below on purpose -- the whole point
    // of it is that it moves on every tick, whether or not anything else did.
    // This is the one thing that now draws on a tick where nothing changed, and
    // it costs about a five-hundredth of what the old full repaint did.
    if (strncmp(s_otaUiTitle, kOtaInstallTitle, sizeof(kOtaInstallTitle) - 1) == 0) {
        displayDev().startWrite();
        otaWorkerRunRhino(layout.rhinoY);
        displayDev().endWrite();
    }

    const int barW = otaWorkerBarW();
    int fillW = 0;
    int pct = 0;
    if (total > 0) {
        pct = (int)((written * 100UL) / total);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        // Quantising through whole percent is what bounds the bar to at most
        // 100 fills for a download instead of one per callback.
        fillW = (barW - 2) * pct / 100;
    }

    char pctBuf[24];
    if (total > 0) snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    else snprintf(pctBuf, sizeof(pctBuf), "working...");

    char bytesBuf[64];
    if (total > 0) {
        snprintf(bytesBuf,
                 sizeof(bytesBuf),
                 "%lu / %lu KB",
                 (unsigned long)(written / 1024UL),
                 (unsigned long)(total / 1024UL));
    } else {
        snprintf(bytesBuf,
                 sizeof(bytesBuf),
                 "%lu KB downloaded",
                 (unsigned long)(written / 1024UL));
    }

    const uint16_t pctColor = stalled ? TFT_ORANGE : TFT_CYAN;
    const uint16_t detailColor = stalled ? TFT_ORANGE : TFT_WHITE;
    const uint16_t barColor = stalled ? TFT_ORANGE : TFT_GREEN;
    const char *detailText = (detail && detail[0]) ? detail : "";

    const bool barMoved = (fillW != s_otaUiLastFillW) || (stalled != s_otaUiBarStalled);
    if (!barMoved &&
        !otaWorkerRowChanged(s_otaUiPctRow, pctBuf, pctColor) &&
        !otaWorkerRowChanged(s_otaUiBytesRow, bytesBuf, TFT_WHITE) &&
        !otaWorkerRowChanged(s_otaUiDetailRow, detailText, detailColor)) {
        return;   // nothing on this screen says anything new: touch no pixels
    }

    // One transaction for the whole composite paint, so LovyanGFX holds the bus
    // across the primitives instead of re-acquiring it for each one.
    displayDev().startWrite();

    if (fillW < s_otaUiLastFillW) {
        // The bar only ever runs backwards when the transfer restarted from
        // zero — the low-memory TLS retry. Without this the bar would keep the
        // failed attempt's fill and never move again.
        displayDev().fillRect(kOtaUiBarX + 1, layout.barY + 1,
                      barW - 2, layout.barH - 2, TFT_BLACK);
        s_otaUiLastFillW = 0;
    }
    if (stalled != s_otaUiBarStalled) {
        // Colour flip only: repaint the span already filled, once. 18 px tall,
        // so ~11.5 KB even at full width.
        if (s_otaUiLastFillW > 0) {
            displayDev().fillRect(kOtaUiBarX + 1, layout.barY + 1,
                                  s_otaUiLastFillW, layout.barH - 2, barColor);
        }
        s_otaUiBarStalled = stalled;
    }
    if (fillW > s_otaUiLastFillW) {
        // The common case, and the whole point: paint the new segment only.
        displayDev().fillRect(kOtaUiBarX + 1 + s_otaUiLastFillW, layout.barY + 1,
                      fillW - s_otaUiLastFillW, layout.barH - 2, barColor);
        s_otaUiLastFillW = fillW;
    }

    otaWorkerDrawTextRow(s_otaUiPctRow, layout.pctY, pctBuf, pctColor);
    otaWorkerDrawTextRow(s_otaUiBytesRow, layout.bytesY, bytesBuf, TFT_WHITE);
    otaWorkerDrawTextRow(s_otaUiDetailRow, layout.detailY, detailText, detailColor);

    displayDev().endWrite();
#endif
}

static bool otaWorkerEnsureWifiConnected() {
    if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
        return true;
    }
    if (!wifiHasActiveCreds()) {
        return false;
    }

    wifi_mode_t mode = WiFi.getMode();
    switch (mode) {
        case WIFI_OFF:
            WiFi.mode(WIFI_STA);
            wifiBeginActiveKnown();
            break;
        case WIFI_STA:
            wifiBeginActiveKnown();
            break;
#ifdef WIFI_AP_STA
        case WIFI_AP:
            WiFi.mode(WIFI_AP_STA);
            wifiBeginActiveKnown();
            break;
#endif
        case WIFI_AP_STA:
            wifiBeginActiveKnown();
            break;
        default:
            break;
    }

    uint32_t startMs = millis();
    while ((millis() - startMs) < 12000UL) {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            return true;
        }
        delay(120);
    }

    return (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP);
}

static bool otaWorkerErrIsTlsLowMem(const char *err) {
    return err && strstr(err, "TLS init failed (low memory)") != nullptr;
}

static void otaWorkerLogHeap(const char *tag) {
    const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    Serial.printf("[ota-worker] %s heap int_free=%lu largest=%lu\n",
                  tag ? tag : "heap",
                  (unsigned long)freeInt,
                  (unsigned long)largestInt);
}

static bool otaWorkerReconnectWifiForLowMemRetry() {
    if (!wifiHasActiveCreds()) return false;

    Serial.println("[ota-worker] low-mem retry: cycling WiFi stack");
    otaWorkerLogHeap("before wifi recycle");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(180);

    WiFi.mode(WIFI_STA);
    wifiBeginActiveKnown();

    uint32_t startMs = millis();
    while ((millis() - startMs) < 12000UL) {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            otaWorkerLogHeap("after wifi recycle");
            return true;
        }
        delay(120);
    }

    otaWorkerLogHeap("wifi recycle failed");
    return false;
}

// Keeps the station link associated whenever the WiFi master switch is on and
// credentials exist, so the device always holds an IP — independent of the web
// config portal or the MQTT bridge. Non-blocking: kicks WiFi.begin() at most once
// per interval and returns immediately. Coexists with the AP (AP_STA) so the web
// portal keeps working alongside it.
// Reconnect backoff. A scan-and-associate attempt is among the most expensive
// things the radio does, and the old fixed 10 s retry ran forever — a device
// carried out of range of its home AP paid that every 10 s for the rest of its
// uptime. Back off to a 5 minute ceiling instead; the moment an association
// succeeds the interval resets, so coming back into range still reconnects
// within one interval rather than being permanently slow.
static constexpr uint32_t kWifiKickMinMs = 10000UL;
static constexpr uint32_t kWifiKickMaxMs = 300000UL;
static uint32_t s_wifiStaKickIntervalMs = kWifiKickMinMs;

// Remember the network we actually associated with, by making it the configured
// one. The WiFi picker's choice is a RAM-only override (see wifiGetActiveCreds),
// so without this a device that joined a network from the picker reverts to the
// old configured SSID on the next boot — "it forgot my network".
//
// Runs on a *successful* association only, which is the point: a network that
// would not connect never displaces one that did. Fires once per override,
// since promoting clears the override that triggered it.
static void wifiPromoteConnectedNetwork() {
    if (!s_wifiUsingKnownOverride || !s_wifiSelectedSsid[0]) return;
    // "AP" is the force-SoftAP sentinel, not a joinable network. It cannot reach
    // here anyway (wifiHasActiveCreds() reports none for it, so the station
    // service returns before connecting), but promoting it would write a network
    // literally named "AP" into the config.
    if (wifiForceApMode()) return;

    const bool sameAsConfigured =
        strncmp(s_wifiSelectedSsid, s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid)) == 0 &&
        strncmp(s_wifiSelectedPass, s_cfg.wifiPass, sizeof(s_cfg.wifiPass)) == 0;

    if (!sameAsConfigured) {
        // Keeps the network being replaced in the remembered list, so switching
        // back to it later does not mean re-entering its password.
        wifiSetConfiguredNetwork(s_wifiSelectedSsid, s_wifiSelectedPass);
        syncWifiCredsToPrefs();
        Serial.printf("[wifi] connected to \"%s\" - saved as the configured network\n",
                      s_cfg.wifiSsid);
    }

    // Drop the override either way: it has served its purpose, and leaving it
    // set would keep the picker anchored to a runtime value that no longer
    // differs from the configured one.
    s_wifiUsingKnownOverride = false;
    memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
    memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
    // Clearing the override also drops any persisted "AP" choice, which would
    // otherwise force the SoftAP on the next boot and mask the network just
    // saved — same reasoning as the connect-dialog path.
    persistWifiForceAp();

    // Slot 0 now holds the network we are on, so rebuilding re-anchors the
    // picker selection to it.
    populateKnownWifiEntries();
    refreshCfgWifiPickerModal();   // no-op unless the picker is open
}

// Set by the low-battery load shed. A runtime hold rather than a config change:
// s_cfg.wifiEnabled is the user's setting, and clearing it would be picked up by
// the next unrelated markConfigDirty() and written to NVS — silently turning
// their Wi-Fi off for good because the battery once ran low. Cleared by a
// reboot, which is what happens after a charge.
static bool s_powerWifiHold = false;

#if defined(DEVICE_TDISPLAY_P4)
// ── P4 Wi-Fi start-up load ───────────────────────────────────────────────────
// On the keyboard accessory's supply, the board browned out or tripped a
// watchdog in the first seconds of loop() -- but only with Wi-Fi on and the
// AMOLED bright together; either alone booted clean every time. The first full
// screen at full brightness and a scan-and-associate are the two biggest draws
// it has, and they landed on the same moment. So nothing starts Wi-Fi until
// the screen has been up a while, and the C6 transmits at a lower ceiling,
// which trims every burst after that too.
static constexpr uint32_t kP4WifiSettleMs = 10000;
// 15 dBm against the default 19.5: a few metres of range for a smaller spike.
static constexpr wifi_power_t kP4WifiTxPower = WIFI_POWER_15dBm;
static uint32_t s_p4LoopStartMs = 0;
// startWebConfigAuto() held back from setup() until the settle has passed.
static bool s_p4WebAutoStartPending = false;

static bool p4WifiSettled(uint32_t now) {
    if (s_p4LoopStartMs == 0) s_p4LoopStartMs = now ? now : 1;
    return (uint32_t)(now - s_p4LoopStartMs) >= kP4WifiSettleMs;
}

// Every path that brings the station or the AP up -- web config, the reconnect
// below, the picker, the OTA retry -- ends in one of these events, so the cap
// is applied there rather than at each call site. It has to follow the start:
// the driver resets the limit when the interface comes up.
static void p4WifiCapTxPowerInit() {
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
        WiFi.setTxPower(kP4WifiTxPower);
    }, ARDUINO_EVENT_WIFI_STA_START);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
        WiFi.setTxPower(kP4WifiTxPower);
    }, ARDUINO_EVENT_WIFI_AP_START);
}
#endif

static void serviceWifiStation(uint32_t now) {
    // Ahead of the web-config check: the shed stops that too, and this must not
    // let the station come back up underneath it.
    if (s_powerWifiHold) return;
#if defined(DEVICE_TDISPLAY_P4)
    if (!p4WifiSettled(now)) return;
#endif
    // Web config owns the radio while it's up: webCfgBegin() sets up STA (or an
    // AP fallback when the saved station won't connect) and webCfgEnd() tears it
    // down. Kicking a station reconnect here mid-session flips WiFi.mode() to
    // AP_STA and calls WiFi.begin() underneath it — which drops the AP client and
    // wedges the driver against the synchronous web server, so the page never
    // loads and the device looks frozen. Leave the radio to web config.
    if (webCfgRunning()) return;
    if (!s_cfg.wifiEnabled || !wifiHasActiveCreds()) return;
    if (WiFi.status() == WL_CONNECTED) {
        // Associated: next disconnection gets a prompt retry again.
        s_wifiStaKickIntervalMs = kWifiKickMinMs;
        // This is the only place that knows an association actually succeeded,
        // so it is where a picked network earns its place in the config.
        wifiPromoteConnectedNetwork();
        // And where web config, held back while MQTT kept the station, can
        // finally join it.
        if (s_webCfgAwaitStation) {
            s_webCfgAwaitStation = false;
            startWebConfigAuto();
        }
        return;
    }
    if (s_wifiStaKickMs != 0 && (now - s_wifiStaKickMs) < s_wifiStaKickIntervalMs) return;
    s_wifiStaKickMs = now;
    if (s_wifiStaKickIntervalMs < kWifiKickMaxMs) {
        s_wifiStaKickIntervalMs *= 2;
        if (s_wifiStaKickIntervalMs > kWifiKickMaxMs) s_wifiStaKickIntervalMs = kWifiKickMaxMs;
    }

    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_AP)       WiFi.mode(WIFI_AP_STA);
    else if (mode == WIFI_OFF) WiFi.mode(WIFI_STA);
    wifiBeginActiveKnown();
}

#if defined(DEVICE_TDISPLAY_P4)
// The P4's panel is refreshed continuously from a framebuffer in PSRAM (MIPI-DSI
// video mode). An install is a Wi-Fi download written straight to flash, and
// that starves the framebuffer read: the DSI bridge underruns and fills the
// panel with bright blue, over and over for the whole install -- ESP-IDF's DPI
// driver says as much in its underrun handler ("the LCD display may already
// becomes blue"). A lower pixel clock does not reliably cure it, because flash
// writes can stall PSRAM outright; the configuration that would needs a rebuilt
// IDF, which the prebuilt Arduino libraries do not allow.
//
// So the panel is simply off while flash is being written: brightness 0 and
// sleep, which on an AMOLED is black, not blue. The screen says so before it
// goes. Success reboots into the new firmware and brings the panel up fresh;
// a failure turns it back on to show why.
static bool s_otaP4PanelOff = false;

static void otaWorkerP4PanelOff() {
    displayDev().setBrightness(0);
    displayDev().sleep();
    s_otaP4PanelOff = true;
}

static void otaWorkerP4PanelOn() {
    if (!s_otaP4PanelOff) return;
    displayDev().wakeup();
    delay(120);   // sleep-out settle before the panel takes commands again
    displayDev().setBrightness(TFT_BRIGHTNESS_DEFAULT);
    s_otaP4PanelOff = false;
    // Whatever was drawn while it slept is in the framebuffer, but the next
    // draw should not trust a delta against it.
    otaWorkerInvalidateScreen();
}
#endif

static bool runOtaWorkerModeIfRequested() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    if (isOtaWorkerModeRequestedRtc() || isOtaWorkerModeRequestedOnce()) {
        (void)consumeOtaWorkerModeRtcOnce();
        (void)consumeOtaWorkerModeOnce();
        otaSetNetworkAllowed(false);
        Serial.println("[ota-worker] disabled on cardputer build");
    }
    return false;
#else
    bool rtcRequested = isOtaWorkerModeRequestedRtc();
    bool nvsRequested = isOtaWorkerModeRequestedOnce();
    if (!(rtcRequested || nvsRequested)) return false;

    // Consume the request before doing any work, not after. The rest of this
    // run lives in RAM, so clearing here costs nothing — and it means a reboot
    // at any point during the update (power cut, watchdog, user pulling the
    // plug on a stalled download) comes back up in normal firmware instead of
    // landing in OTA minimal mode again. A partial download can't leave the
    // device unbootable either: Update only flips the boot partition on a
    // successful Update.end(), so an interrupted write keeps the running app.
    (void)consumeOtaWorkerModeRtcOnce();
    (void)consumeOtaWorkerModeOnce();

    otaSetNetworkAllowed(true);

#if defined(DEVICE_TDECK_PRO)
    // Forget what the panel showed last time: a second attempt in the same
    // session would otherwise dedupe against the previous run's screen and
    // paint nothing at all.
    s_otaProShownHeadline[0] = '\0';
#endif

    Serial.printf("[ota-worker] one-shot mode requested (rtc=%d nvs=%d)\n",
                  rtcRequested ? 1 : 0,
                  nvsRequested ? 1 : 0);
    otaWorkerDrawStatus("OTA minimal mode", "Preparing network...");

    if (!otaWorkerEnsureWifiConnected()) {
        Serial.println("[ota-worker] wifi unavailable");
        otaWorkerDrawStatus("OTA minimal mode", "WiFi not connected");
        setOtaWorkerBootNotice("OTA check failed: WiFi not connected");
        delay(1800);
        otaSetNetworkAllowed(false);
        return true;
    }

    otaWorkerDrawStatus("OTA minimal mode", "Checking latest release...");
    OtaCheckResult check = {};
    bool checkOk = otaCheckLatestRelease(check) && check.ok;
    if (!checkOk && otaWorkerErrIsTlsLowMem(check.error)) {
        Serial.println("[ota-worker] low-mem TLS on check; retrying after WiFi recycle");
        otaWorkerDrawStatus("OTA minimal mode", "Retrying check...");
        if (otaWorkerReconnectWifiForLowMemRetry()) {
            memset(&check, 0, sizeof(check));
            checkOk = otaCheckLatestRelease(check) && check.ok;
        }
    }
    if (!checkOk) {
        Serial.printf("[ota-worker] check failed: %s\n", check.error);
        otaWorkerDrawStatus("OTA check failed", check.error[0] ? check.error : "unknown");
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        snprintf(notice,
                 sizeof(notice),
                 "OTA check failed: %s",
                 check.error[0] ? check.error : "unknown");
        setOtaWorkerBootNotice(notice);
        delay(1800);
        otaSetNetworkAllowed(false);
        return true;
    }

    if (!check.updateAvailable) {
        Serial.printf("[ota-worker] up to date: %s\n", check.latestTag);
        otaWorkerDrawStatus("Already up to date",
                            check.latestTag[0] ? check.latestTag : APP_VERSION);
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        snprintf(notice,
                 sizeof(notice),
                 "Firmware already up to date (%s)",
                 check.latestTag[0] ? check.latestTag : APP_VERSION);
        setOtaWorkerBootNotice(notice);
        delay(1400);
        otaSetNetworkAllowed(false);
        return true;
    }

    // Composed once, here, ahead of every draw that uses it. The tag stays on
    // the second line as well: on the TFT this screen is overwritten by the
    // progress draw a few instructions later and nobody reads it, but on the
    // Pro it *is* the install screen -- otaProShowState() dedupes every later
    // tick into this one paint -- and dropping the line would take the version
    // off the only screen that board shows for the whole update.
    otaWorkerDrawStatus(otaComposeInstallTitle(check.latestTag),
                        check.latestTag[0] ? check.latestTag : "latest");
#if defined(DEVICE_TDISPLAY_P4)
    // See otaWorkerP4PanelOff(): the install would otherwise flash the panel
    // blue throughout. Said on screen first, and held long enough to read.
    otaWorkerDrawStatus(otaComposeInstallTitle(check.latestTag),
                        "Screen off until it restarts");
    delay(2500);
    otaWorkerP4PanelOff();
#endif
    static volatile size_t s_otaWorkerBytesWritten = 0;
    static volatile size_t s_otaWorkerBytesTotal = 0;
    static volatile uint32_t s_otaWorkerLastProgressMs = 0;
    static volatile size_t s_otaWorkerLastAdvancedBytes = 0;
    static uint32_t s_otaWorkerLastDrawMs = 0;
    static bool s_otaWorkerLastDrawStalled = false;
    auto onOtaProgress = [](size_t written, size_t total) {
        const uint32_t now = millis();
        if (written != s_otaWorkerLastAdvancedBytes) {
            s_otaWorkerLastAdvancedBytes = written;
            s_otaWorkerLastProgressMs = now;
        }
        s_otaWorkerBytesWritten = written;
        s_otaWorkerBytesTotal = total;

        const bool stalled = (s_otaWorkerLastProgressMs != 0)
                          && ((uint32_t)(now - s_otaWorkerLastProgressMs) > 3000UL);

        // Entering or leaving a stall is news, so it is shown at once. After
        // that the floor is 180 ms while bytes are moving and 1 s once they
        // have stopped. The old code did the opposite -- `|| stalled` bypassed
        // the throttle entirely -- and since this callback fires from the
        // download loop's idle branches, which spin on delay(10), a stall meant
        // ~25 repaints a second competing with the very poll that was trying to
        // recover before the 15 s abort fired.
        const uint32_t minGapMs = stalled ? 1000UL : 180UL;
        if (stalled != s_otaWorkerLastDrawStalled ||
            (uint32_t)(now - s_otaWorkerLastDrawMs) >= minGapMs) {
            // Cheap when nothing changed: otaWorkerDrawProgress() compares each
            // field against what is already on the panel and touches no pixels
            // if they all match.
            otaWorkerDrawProgress(s_otaInstallTitle,
                                  stalled ? "No progress for 3s..." : "Downloading...",
                                  written,
                                  total,
                                  stalled);
            s_otaWorkerLastDrawStalled = stalled;
            s_otaWorkerLastDrawMs = now;
        }
    };

    s_otaWorkerBytesWritten = 0;
    s_otaWorkerBytesTotal = 0;
    s_otaWorkerLastAdvancedBytes = 0;
    s_otaWorkerLastProgressMs = millis();
    s_otaWorkerLastDrawMs = 0;
    s_otaWorkerLastDrawStalled = false;
    otaWorkerInvalidateScreen();
    otaWorkerDrawProgress(s_otaInstallTitle,
                          "Starting download...",
                          0,
                          0,
                          false);

    char err[160] = {};
    bool installOk = otaInstallLatestRelease(check.latestTag[0] ? check.latestTag : nullptr,
                                             err,
                                             sizeof(err),
                                             onOtaProgress);
    if (!installOk && otaWorkerErrIsTlsLowMem(err)) {
        Serial.println("[ota-worker] low-mem TLS on install; retrying after WiFi recycle");
        otaWorkerDrawProgress(s_otaInstallTitle,
                              "Retrying download...",
                              (size_t)s_otaWorkerBytesWritten,
                              (size_t)s_otaWorkerBytesTotal,
                              true);
        if (otaWorkerReconnectWifiForLowMemRetry()) {
            err[0] = '\0';
            // Second attempt, second transfer: the screen starts clean rather
            // than carrying the failed run's chrome and bar fill. The byte
            // counters are deliberately left alone -- if the retry dies before
            // its first callback, the failure screen should still show how far
            // the first attempt got.
            s_otaWorkerLastAdvancedBytes = 0;
            s_otaWorkerLastProgressMs = millis();
            s_otaWorkerLastDrawMs = 0;
            s_otaWorkerLastDrawStalled = false;
            otaWorkerInvalidateScreen();
            installOk = otaInstallLatestRelease(check.latestTag[0] ? check.latestTag : nullptr,
                                                err,
                                                sizeof(err),
                                                onOtaProgress);
        }
    }

    if (installOk) {
        Serial.println("[ota-worker] install complete, rebooting");
        otaWorkerDrawStatus("OTA installed", "Rebooting...");
        delay(700);
        otaSetNetworkAllowed(false);
        flushPersistentState();   // settings and transcripts must land before we go
        ESP.restart();
        return true;
    }

    // Final refresh with last known transfer counters for context on failure.
#if defined(DEVICE_TDISPLAY_P4)
    otaWorkerP4PanelOn();   // the failure has to be seen; flash writing is over
#endif
    {
        size_t curW = (size_t)s_otaWorkerBytesWritten;
        size_t curT = (size_t)s_otaWorkerBytesTotal;
        otaWorkerDrawProgress("OTA install failed",
                              err[0] ? err : "unknown",
                              curW,
                              curT,
                              true);
    }

    Serial.printf("[ota-worker] install failed: %s\n", err);
    {
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        if (strcmp(err, kOtaErrNeedsUsbInstall) == 0) {
            // The one failure the user has to act on, so it gets words rather
            // than an error string. A key rather than the sentence itself: the
            // notice is shown after the reboot, through TR(), in the UI font.
            snprintf(notice, sizeof(notice), "%s", kOtaNoticeNeedsUsb);
        } else {
            snprintf(notice,
                     sizeof(notice),
                     "OTA install failed: %s",
                     err[0] ? err : "unknown");
        }
        setOtaWorkerBootNotice(notice);
    }
    delay(2200);
    otaSetNetworkAllowed(false);
    return true;
#endif
}

#if defined(DEVICE_TDECK_PRO)
// Reads the persisted setting rather than keeping a copy. It used to be a plain
// bool initialised to true, which meant a keyboard deliberately turned dark came
// back lit after every reboot and every flash.
static inline bool tdeckProKeyboardBacklightEnabled() {
    return s_cfg.kbBacklightEnabled;
}
#endif

// ── Keyboard light timeout (issue #104) ──────────────────────────────────────
// True while the keyboard light has been put out for want of input
// (kbLightTimeoutS; serviceKbLightTimeout() below). Every resting "lit" asks
// this, so nothing that relights the keyboard as a matter of course -- a wake,
// the end of a notification blink -- brings it back while the device is idle.
// The blink's own lit phases do not ask: a notification still flashes.
#if HAS_KB_LIGHT_TIMEOUT
static bool s_kbLightIdleDark = false;
#endif
