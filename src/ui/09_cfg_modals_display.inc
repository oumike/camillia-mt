// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Config modals: Wi-Fi delete, colours, brightness, sliders, chat style, theme, fonts.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// True when the row can actually be deleted. Shared by the D shortcut and the
// confirmation prompt, so the prompt is never raised for a row that the delete
// would then refuse — the reason comes back for the picker's notice line.
static bool knownWifiEntryDeletable(int idx, const char **whyNot) {
    if (idx < 0 || idx >= s_cfgKnownWifiCount) {
        if (whyNot) *whyNot = TR("No network selected");
        return false;
    }
    if (strncmp(s_cfgKnownWifi[idx].ssid, kForceApSsid, sizeof(kForceApSsid)) == 0) {
        if (whyNot) *whyNot = TR("AP mode cannot be removed");
        return false;
    }
    if (!s_cfgKnownWifi[idx].ssid[0]) {
        if (whyNot) *whyNot = TR("No network in that slot");
        return false;
    }
    return true;
}

// Where to go when the network being deleted is the one in use: the configured
// slot first, then the session-learned entries in order, and forced AP mode only
// when nothing joinable is left.
static int nextUsableKnownWifiIndex() {
    if (s_cfgKnownWifiCount > 0 && s_cfgKnownWifi[0].ssid[0]) return 0;
    for (int i = kForceApWifiIndex + 1; i < s_cfgKnownWifiCount; i++) {
        if (s_cfgKnownWifi[i].ssid[0]) return i;
    }
    return kForceApWifiIndex;
}

// Removes the highlighted entry from the Choose WiFi list (the D shortcut,
// after the confirmation prompt).
//
// The row kinds delete differently. Slot 0 is the configured credential pair, so
// removing it has to clear the blob *and* the pre-blob standalone NVS keys —
// loadConfigFromPrefs() still reads those as a migration fallback and would
// otherwise hand the deleted SSID straight back on the next boot — *and* the
// copies web config keeps, so a later save cannot restore what was just removed.
// Everything below slot 1 is a remembered network, held in its own NVS blob, so
// those deletions have to be written back through saveKnownWifiNetworks().
static void deleteSelectedKnownWifi() {
    const int idx = s_cfgWifiSelection;
    const char *whyNot = nullptr;
    if (!knownWifiEntryDeletable(idx, &whyNot)) {
        setCfgWifiPickerNotice(whyNot);
        return;
    }

    char ssid[sizeof(s_cfgKnownWifi[0].ssid)];
    utf8util::copyTruncate(ssid, sizeof(ssid), s_cfgKnownWifi[idx].ssid);

    // Whether this is the entry currently supplying credentials — captured
    // before anything is mutated, and true for either source: the configured
    // slot when no override is set, or the override itself.
    const char *activeSsid = nullptr;
    wifiGetActiveCreds(&activeSsid, nullptr);
    const bool wasActive = activeSsid && activeSsid[0]
                           && strncmp(activeSsid, ssid, sizeof(ssid)) == 0;

    if (idx == 0) {
        memset(s_cfg.wifiSsid, 0, sizeof(s_cfg.wifiSsid));
        memset(s_cfg.wifiPass, 0, sizeof(s_cfg.wifiPass));
        webCfgClearWifiCreds();

        Preferences p;
        if (p.begin("camillia", false)) {
            p.remove("wifiSsid");
            p.remove("wifiPass");
            p.end();
        }
        // Written now rather than through markConfigDirty(): a deletion that
        // only reaches flash 1.5 s later is a deletion that a reset in between
        // silently undoes.
        persistConfigToPrefs();
    } else {
        for (int i = 0; i < s_cfgKnownWifiAddedCount; i++) {
            if (strncmp(s_cfgKnownWifiAdded[i].ssid, ssid,
                        sizeof(s_cfgKnownWifiAdded[i].ssid)) != 0) {
                continue;
            }
            for (int j = i + 1; j < s_cfgKnownWifiAddedCount; j++) {
                s_cfgKnownWifiAdded[j - 1] = s_cfgKnownWifiAdded[j];
            }
            s_cfgKnownWifiAddedCount--;
            memset(&s_cfgKnownWifiAdded[s_cfgKnownWifiAddedCount], 0,
                   sizeof(s_cfgKnownWifiAdded[0]));
            // These are persisted now, so a deletion has to reach flash too —
            // otherwise the entry comes straight back on the next boot.
            saveKnownWifiNetworks();
            break;
        }
    }

    // The credentials in use just stopped existing, so the association has to go
    // with them — leaving it up would keep the device on a network it can no
    // longer describe, and an override still naming the deleted entry would go
    // on feeding wifiGetActiveCreds() after the row is gone.
    if (wasActive) {
        s_wifiUsingKnownOverride = false;
        memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
        memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
        if (s_cfg.wifiEnabled) WiFi.disconnect(false);
    }

    populateKnownWifiEntries();

    char notice[96];
    if (wasActive) {
        // Hand the fallback to the picker's own selection path rather than
        // reimplementing it: forced-AP bookkeeping and the web-config restart
        // that a radio-mode change needs both live in there.
        const int fallback = nextUsableKnownWifiIndex();
        applyCfgWifiSelection(fallback);
        // applyCfgWifiSelection() clears the kick timestamp but not the backoff
        // it grew, and a network that had been unreachable for a while can carry
        // an interval of minutes — which the replacement network would then have
        // to serve out before its first association attempt.
        s_wifiStaKickIntervalMs = kWifiKickMinMs;

        if (fallback == kForceApWifiIndex) {
            snprintf(notice, sizeof(notice), TR("Removed %s - no networks left, AP mode"), ssid);
        } else {
            snprintf(notice, sizeof(notice), TR("Removed %s - now %s"),
                     ssid, s_cfgKnownWifi[fallback].ssid);
        }
    } else {
        if (s_cfgWifiSelection >= s_cfgKnownWifiCount) s_cfgWifiSelection = s_cfgKnownWifiCount - 1;
        if (s_cfgWifiSelection < 0) s_cfgWifiSelection = 0;
        snprintf(notice, sizeof(notice), TR("Removed %s"), ssid);
    }

    refreshCfgWifiPickerModal();
    setCfgWifiPickerNotice(notice);
}

// ── Delete confirmation ──────────────────────────────────────────────────
// Deleting is one keystroke on a list, and for the configured slot it discards a
// password that only the user can type back in, so it asks first. Y/Enter
// confirms, N/close cancels; the answer is the only thing this dialog consumes.
static void cfgWifiDeleteConfirmAccept() {
    closeCfgWifiDeleteConfirm();
    deleteSelectedKnownWifi();
}

static void cfgWifiDeleteConfirmReject() {
    closeCfgWifiDeleteConfirm();
}

static void onCfgWifiDelYesPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgWifiDeleteConfirmAccept();
}

static void onCfgWifiDelNoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgWifiDeleteConfirmReject();
}

static void onCfgWifiDelBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgWifiDelBackdrop) return;
    cfgWifiDeleteConfirmReject();
}

static void openCfgWifiDeleteConfirm() {
    if (!s_rootScreen || s_cfgWifiDelModal || s_cfgWifiDelBackdrop) return;

    // Refused rows report on the picker's notice line instead of raising a
    // prompt for something that was never going to be deleted.
    const char *whyNot = nullptr;
    if (!knownWifiEntryDeletable(s_cfgWifiSelection, &whyNot)) {
        setCfgWifiPickerNotice(whyNot);
        return;
    }

    const char *ssid = s_cfgKnownWifi[s_cfgWifiSelection].ssid;
    const char *activeSsid = nullptr;
    wifiGetActiveCreds(&activeSsid, nullptr);
    const bool isActive = activeSsid && activeSsid[0]
                          && strncmp(activeSsid, ssid,
                                     sizeof(s_cfgKnownWifi[0].ssid)) == 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 40;
    if (modalW < 160) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t bodyTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const uint32_t noBtnBg = lightUi ? 0xC76565 : 0x6B3030;
    const uint32_t yesBtnBg = lightUi ? 0x429A56 : 0x2F6B30;

    s_cfgWifiDelBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiDelBackdrop, w, h);
    lv_obj_align(s_cfgWifiDelBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiDelBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiDelBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiDelBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiDelBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiDelBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiDelBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiDelBackdrop, onCfgWifiDelBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiDelModal = lv_obj_create(s_cfgWifiDelBackdrop);
    lv_obj_set_size(s_cfgWifiDelModal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(s_cfgWifiDelModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiDelModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiDelModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiDelModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_cfgWifiDelModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiDelModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiDelModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_cfgWifiDelModal, 10, 0);
    lv_obj_set_style_pad_row(s_cfgWifiDelModal, 8, 0);
    lv_obj_set_flex_flow(s_cfgWifiDelModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiDelModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgWifiDelBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiDelModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Delete WiFi?"));

    lv_obj_t *body = lv_label_create(s_cfgWifiDelModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(body, bodyTextColor, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    // Says what else the delete will do while it can still be called off — the
    // reconnect is the part nobody expects from a list keystroke.
    //
    // Deliberately does not name the replacement. nextUsableKnownWifiIndex()
    // answers correctly *after* the delete, when slot 0 holds whatever is left;
    // asked here, before anything is removed, slot 0 is still the network being
    // deleted — so it used to promise "rhinohome ... will switch to rhinohome".
    if (isActive) {
        lv_label_set_text_fmt(body,
                              TR("%s\nIn use - will switch to the next configured "
                              "WiFi network, or AP mode if none is available"),
                              ssid);
    } else {
        lv_label_set_text(body, ssid);
    }

    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiDelModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 14, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makeDelBtn = [](lv_obj_t *parent, const char *text, uint32_t bgColor,
                         lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
    #if defined(DEVICE_TDECK_PRO)
        LV_UNUSED(bgColor);
        lv_obj_remove_style_all(btn);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    #endif
        lv_obj_set_height(btn, 32);
        lv_obj_set_style_min_width(btn, 78, 0);
        lv_obj_set_style_radius(btn, 4, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
    #endif
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    #endif
        lv_label_set_text(lbl, TR(text));
        lv_obj_center(lbl);
        return btn;
    };
#if UI_TOUCH_ONLY_PROFILE
    makeDelBtn(btnRow, "No", noBtnBg, onCfgWifiDelNoPressed);
    makeDelBtn(btnRow, TR("Yes"), yesBtnBg, onCfgWifiDelYesPressed);
#else
    makeDelBtn(btnRow, "(N)o", noBtnBg, onCfgWifiDelNoPressed);
    makeDelBtn(btnRow, "(Y)es", yesBtnBg, onCfgWifiDelYesPressed);
#endif
}

// ── Own-message color picker ─────────────────────────────────────────────
// A "Reset to Default" cell followed by a grid of the 16 basic colors in
// kUserMsgColors[]. Navigation index 0 is the reset cell (restores the
// adaptive yellow default, s_cfg.userMsgColor = 0xFF); indices 1..N map to the
// palette entries. The chosen value drives userMessageAccentColor565(), which
// colors only the local user's own messages. Selecting reboots so the change
// applies uniformly across every already-rendered view.
static constexpr int kUserMsgColorNavCount = kUserMsgColorCount + 1;  // +1 reset cell

// Grid geometry, shared by the builder and the renderer so the cell size and
// the width they each derive from it cannot drift apart.
//
// Cardputer runs a smaller cell eight to a row rather than four. At 44x30 the
// 16 colors need four rows, and that grid plus the title and the hint line came
// to ~134 px inside a modal capped at 119 — the overflow is what clipped the
// dialog at the top and bottom. Eight 20 px cells put the whole palette on two
// rows that fit without scrolling.
#if UI_CARDPUTER_COMPACT
static constexpr int kMsgColorSwatchW   = 20;
static constexpr int kMsgColorSwatchH   = 20;
static constexpr int kMsgColorSwatchGap = 4;
static constexpr int kMsgColorCols      = 8;
static constexpr int kMsgColorResetH    = 22;
#else
static constexpr int kMsgColorSwatchW   = 44;
static constexpr int kMsgColorSwatchH   = 30;
static constexpr int kMsgColorSwatchGap = 6;
static constexpr int kMsgColorCols      = 4;
static constexpr int kMsgColorResetH    = 26;
#endif
static constexpr int kMsgColorGridW =
    kMsgColorSwatchW * kMsgColorCols + kMsgColorSwatchGap * (kMsgColorCols - 1);

static void closeCfgColorPickerModal() {
    if (lvObjValid(s_cfgColorBackdrop)) {
        lv_obj_del(s_cfgColorBackdrop);
    } else if (lvObjValid(s_cfgColorModal)) {
        lv_obj_del(s_cfgColorModal);
    }
    s_cfgColorBackdrop = nullptr;
    s_cfgColorModal = nullptr;
    s_cfgColorGrid = nullptr;
}

static void refreshCfgColorPickerModal() {
    if (!s_cfgColorModal || !s_cfgColorGrid) return;

    lv_obj_clean(s_cfgColorGrid);

    // Nav index 0: full-width "Reset to Default" cell (adaptive yellow default).
    {
        const bool sel = (s_cfgColorSelection == 0);
        lv_obj_t *reset = lv_obj_create(s_cfgColorGrid);
        lv_obj_remove_style_all(reset);
        lv_obj_set_size(reset, kMsgColorGridW, kMsgColorResetH);
        lv_obj_clear_flag(reset, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(reset, 5, 0);
        lv_obj_set_style_bg_color(reset, lvColorFrom565(TFT_YELLOW), 0);
        lv_obj_set_style_bg_opa(reset, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(reset, sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(reset,
                                      sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2A3550),
                                      0);
        lv_obj_add_flag(reset, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(reset, onCfgColorRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)0);
        lv_obj_t *lbl = lv_label_create(reset);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1A1A1A), 0);
        lv_label_set_text(lbl, TR("Reset to Default"));
        if (sel) lv_obj_scroll_to_view(reset, LV_ANIM_OFF);
    }

    for (int i = 0; i < kUserMsgColorCount; i++) {
        const int nav = i + 1;
        lv_obj_t *sw = lv_obj_create(s_cfgColorGrid);
        lv_obj_remove_style_all(sw);
        lv_obj_set_size(sw, kMsgColorSwatchW, kMsgColorSwatchH);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(sw, 5, 0);
        lv_obj_set_style_bg_color(sw, lvColorFrom565(kUserMsgColors[i].color), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        const bool sel = (nav == s_cfgColorSelection);
        lv_obj_set_style_border_width(sw, sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(sw,
                                      sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2A3550),
                                      0);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(sw, onCfgColorRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)nav);
        if (sel) lv_obj_scroll_to_view(sw, LV_ANIM_OFF);
    }
}

// navIdx: 0 = reset to adaptive default (0xFF); 1..N = palette entry (navIdx-1).
// The colour a row would apply, for the confirm sentence. Mirrors the naming
// in applyCfgColorSelection() below rather than duplicating its assignment.
static const char *cfgColorNavName(int navIdx) {
    if (navIdx <= 0) return TR("Default");
    return TR(kUserMsgColors[navIdx - 1].name);
}

static void applyCfgColorSelection(int navIdx);

// Applying a message colour reboots, so it asks first. The picker stays open
// behind the dialog; No returns to it.
static void cfgColorAsk(int navIdx) {
    if (navIdx < 0 || navIdx >= kUserMsgColorNavCount) return;
    const uint8_t want = (navIdx == 0) ? 0xFF : (uint8_t)(navIdx - 1);
    if (s_cfg.userMsgColor == want) {   // nothing to change, nothing to ask
        closeCfgColorPickerModal();
        snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                 TR("My Message Color: %s (unchanged)"), cfgColorNavName(navIdx));
        refreshCfgModal();
        return;
    }
    snprintf(s_cfgConfirmText, sizeof(s_cfgConfirmText),
             TR("Set message colour to %s and reboot?"), cfgColorNavName(navIdx));
    openCfgConfirmModal(-1, s_cfgConfirmText,
                        [](int i) { applyCfgColorSelection(i); }, navIdx);
}

static void applyCfgColorSelection(int navIdx) {
    if (navIdx < 0 || navIdx >= kUserMsgColorNavCount) return;
    const char *name;
    if (navIdx == 0) {
        s_cfg.userMsgColor = 0xFF;
        name = TR("Default");
    } else {
        s_cfg.userMsgColor = (uint8_t)(navIdx - 1);
        name = TR(kUserMsgColors[navIdx - 1].name);
    }
    persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("My Message Color: %s - rebooting..."), name);
    closeCfgColorPickerModal();
    refreshCfgModal();
    lv_timer_handler();
    delay(1000);
    flushPersistentState();   // settings and transcripts must land before we go
    ESP.restart();
}

static void onCfgColorRowPressed(lv_event_t *e) {
    int navIdx = (int)(intptr_t)lv_event_get_user_data(e);
    s_cfgColorSelection = navIdx;
    cfgColorAsk(navIdx);
}

static void onCfgColorBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgColorBackdrop) return;
    closeCfgColorPickerModal();
    refreshCfgModal();
}

// ── Brightness picker ─────────────────────────────────────────────────────────
// A slider in 10% steps. The panel follows the slider live so the level can be
// judged directly; closing without Enter restores the level we opened with.

// Which input moves between the two rows. Vertical input adjusts the value and
// Left/Right picks the row on most boards — a trackball or a keyboard with real
// arrows wants it that way round. The Pager and the M9 are the exceptions, and
// for the same reason: neither can deliver Left/Right to this modal. The Pager
// has no such keys on its matrix at all, and the M9 folds its pad's Left/Right
// onto the scroll codes before any handler runs (see m9FourWaySelection), so on
// both boards the second row was unreachable by key. Here the wheel and the pad
// pick the row, and the value moves with j/k — plus Left/Right on the M9, which
// this modal is exempted from the fold for.
// The Tanmatsu opts in for consistency with its other popups, where Up/Down
// picks the row and Left/Right changes it.
#if FEATURE_LOCK_SCREEN && (defined(DEVICE_M9) || defined(DEVICE_TLORA_PAGER_TFT) \
                            || UI_TANMATSU_LAYOUT)
static constexpr bool kBrightVertPicksRow = true;
#else
static constexpr bool kBrightVertPicksRow = false;
#endif

static void setCfgBrightnessPreview(int pct) {
    s_cfg.brightness = cfgCoerceBrightness(pct);
    applyBrightness();
    if (lvObjValid(s_cfgBrightValue)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)s_cfg.brightness);
        lv_label_set_text(s_cfgBrightValue, buf);
    }
}

#if FEATURE_LOCK_SCREEN
// Paints one row as focused and the other as not: the caret on its label, the
// accent outline around its slider, and full-strength value text, against a
// dimmed knob and indicator on the row the keys are not on.
//
// Three cues rather than one because a slider already carries a moving knob, so
// a single quiet mark is easy to lose next to it — and because the caret alone
// is two characters of 10 px text on panels as small as 320x240.
static void paintCfgBrightnessRow(lv_obj_t *slider, lv_obj_t *label,
                                  lv_obj_t *value, const char *name, bool focused) {
    if (lvObjValid(label)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s%s", focused ? "> " : "  ", name);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_color(label,
                                    focused ? lvColorFrom565(s_ui.selectAccent)
                                            : lv_color_hex(0xA7C7FF), 0);
    }
    if (lvObjValid(value)) {
        // The unfocused level stays readable — both rows preview live and the
        // point is to compare them — just clearly not the one being moved.
        lv_obj_set_style_text_opa(value, focused ? LV_OPA_COVER : LV_OPA_50, 0);
    }
    if (!lvObjValid(slider)) return;
    // Outline rather than border: a border on a slider eats into the track and
    // makes the focused row's bar visibly thinner than the other one.
    lv_obj_set_style_outline_width(slider, focused ? 2 : 0, LV_PART_MAIN);
    // 2, not more: the outline is drawn outside the track and the modal's rows
    // are 6 px apart, so a wider halo starts touching the label above it.
    lv_obj_set_style_outline_pad(slider, 2, LV_PART_MAIN);
    lv_obj_set_style_outline_color(slider, lvColorFrom565(s_ui.selectAccent), LV_PART_MAIN);
    lv_obj_set_style_outline_opa(slider, focused ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider,
                              focused ? lvColorFrom565(s_ui.selectAccent)
                                      : lv_color_hex(0x2E5390), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider,
                              focused ? lv_color_hex(0xE8F1FF) : lv_color_hex(0x7E97C4),
                              LV_PART_KNOB);
}

// Marks which row the keys act on. Touch boards can drag either slider directly
// and never need this, but it still follows the slider being touched so the two
// input styles cannot disagree about where focus is.
static void refreshCfgBrightnessFocus() {
    paintCfgBrightnessRow(s_cfgBrightSlider, s_cfgBrightScreenLabel, s_cfgBrightValue,
                          TR("Screen"), s_cfgBrightFocus == 0);
    paintCfgBrightnessRow(s_cfgBrightLockSlider, s_cfgBrightLockLabel, s_cfgBrightLockValue,
                          TR("Lock screen"), s_cfgBrightFocus == 1);
    // The modal is height-capped and scrolls, so moving focus has to carry the
    // row with it. Without this the lock row is selectable off the bottom of
    // the shorter panels, which reads as the keys having done nothing.
    lv_obj_t *focused = (s_cfgBrightFocus == 1) ? s_cfgBrightLockSlider
                                                : s_cfgBrightSlider;
    if (lvObjValid(focused)) lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
}

// Drives the panel straight to the previewed level rather than going through
// applyBrightness(): the lock screen is not up while this modal is open, so
// applyBrightness() would resolve to the UI's level and show nothing. This is
// the only way to judge a glance level while looking at it.
static void setCfgLockBrightnessPreview(int pct) {
    s_cfg.lockScreenBrightness = cfgCoerceBrightness(pct);
    displayDev().setBrightness(cfgBrightnessDuty(s_cfg.lockScreenBrightness));
    if (lvObjValid(s_cfgBrightLockValue)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)s_cfg.lockScreenBrightness);
        lv_label_set_text(s_cfgBrightLockValue, buf);
    }
}

static void onCfgBrightLockSliderChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target_obj(e);
    if (!slider) return;
    // Only on a real change of row. This fires for every event of a drag, and
    // repainting both rows — and scrolling the focused one into view — under a
    // finger that is already on the right slider is churn at best.
    if (s_cfgBrightFocus != 1) {
        s_cfgBrightFocus = 1;
        refreshCfgBrightnessFocus();
    }
    setCfgLockBrightnessPreview((int)lv_slider_get_value(slider));
}
#endif

#if HAS_TOUCH
// Cancel/commit row for touch-capable modals that stage a value.
//
// A modal on a touch-only board can stage a value but cannot commit one: there
// is no Enter to press and no close key to back out with. Brightness and
// battery-trim previewed live and then had no control at all — the only way out
// was a tap outside the modal, which cancels, so a value dragged exactly where
// the user wanted it could not be kept. The same gap applies to anything else
// that stages: the sound picker committed on a second tap of the same row, and
// Channel Edit and Time & Date had a Save cell in their grid but no signposted
// discard at all.
//
// Named next to appendHeltecCloseX() because it is the same class of
// affordance, and shares its styling so a board only ever grows one look.
// commitText lets a caller say "Apply" where that reads better than "Save".
static void appendHeltecCancelSaveRow(lv_obj_t *parent, lv_event_cb_t cancelCb,
                                      lv_event_cb_t commitCb,
                                      const char *commitText = "Save") {
    if (!parent) return;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    struct Local {
        static void make(lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_size(btn, 78, 26);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x8FB5E6), 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
            lv_label_set_text(lbl, TR(text));
            lv_obj_center(lbl);
        }
    };
    Local::make(row, TR("Cancel"), cancelCb);
    Local::make(row, commitText, commitCb);
}
#endif  // HAS_TOUCH

static void closeCfgBrightnessModal() {
    if (lvObjValid(s_cfgBrightBackdrop)) {
        lv_obj_del(s_cfgBrightBackdrop);
    } else if (lvObjValid(s_cfgBrightModal)) {
        lv_obj_del(s_cfgBrightModal);
    }
    s_cfgBrightBackdrop = nullptr;
    s_cfgBrightModal = nullptr;
    s_cfgBrightSlider = nullptr;
    s_cfgBrightValue = nullptr;
#if FEATURE_LOCK_SCREEN
    s_cfgBrightLockSlider = nullptr;
    s_cfgBrightLockValue = nullptr;
    s_cfgBrightScreenLabel = nullptr;
    s_cfgBrightLockLabel = nullptr;
#endif
}

// Abandon the preview and put the panel back where it was. Teardown-safe: the
// settings screen can close with this modal open, and an unapplied preview must
// not linger in s_cfg where the next unrelated save would persist it.
static void revertCfgBrightnessPreview() {
    if (!s_cfgBrightModal && !s_cfgBrightBackdrop) return;
    s_cfg.brightness = s_cfgBrightOriginal;
#if FEATURE_LOCK_SCREEN
    // Both rows preview live, so both have to be put back -- including when the
    // user was on the lock row when they cancelled, which is also why the panel
    // is driven from applyBrightness() below rather than left where the preview
    // had it.
    s_cfg.lockScreenBrightness = s_cfgBrightLockOriginal;
#endif
    applyBrightness();
    closeCfgBrightnessModal();
}

static void cancelCfgBrightness() {
    revertCfgBrightnessPreview();
    refreshCfgModal();
}

static void applyCfgBrightness() {
    persistConfigToPrefs();
#if FEATURE_LOCK_SCREEN
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Brightness: %u%%  Lock: %u%%"),
             (unsigned)s_cfg.brightness,
             (unsigned)cfgCoerceBrightness(s_cfg.lockScreenBrightness));
    // The lock row previews on the live panel, so saving from it would otherwise
    // leave the UI sitting at the glance level until something else repainted.
    applyBrightness();
#else
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Brightness: %u%%"),
             (unsigned)s_cfg.brightness);
#endif
    closeCfgBrightnessModal();
    refreshCfgModal();
}

// Nudge by whole steps; used by the key handler on non-touch boards.
static void stepCfgBrightness(int steps) {
    if (!steps) return;
#if FEATURE_LOCK_SCREEN
    if (s_cfgBrightFocus == 1) {
        const int nextLock = (int)s_cfg.lockScreenBrightness + steps * BRIGHTNESS_PCT_STEP;
        const uint8_t coercedLock = cfgCoerceBrightness(nextLock);
        if (coercedLock == cfgCoerceBrightness(s_cfg.lockScreenBrightness)) return;
        setCfgLockBrightnessPreview(coercedLock);
        if (lvObjValid(s_cfgBrightLockSlider)) {
            lv_slider_set_value(s_cfgBrightLockSlider, coercedLock, LV_ANIM_OFF);
        }
        return;
    }
#endif
    const int next = (int)s_cfg.brightness + steps * BRIGHTNESS_PCT_STEP;
    const uint8_t coerced = cfgCoerceBrightness(next);
    if (coerced == s_cfg.brightness) return;
    setCfgBrightnessPreview(coerced);
    if (lvObjValid(s_cfgBrightSlider)) {
        lv_slider_set_value(s_cfgBrightSlider, coerced, LV_ANIM_OFF);
    }
}

static void onCfgBrightSliderChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target_obj(e);
    if (!slider) return;
#if FEATURE_LOCK_SCREEN
    if (s_cfgBrightFocus != 0) {   // same drag-churn reason as the lock row
        s_cfgBrightFocus = 0;
        refreshCfgBrightnessFocus();
    }
#endif
    setCfgBrightnessPreview((int)lv_slider_get_value(slider));
}

static void onCfgBrightBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgBrightBackdrop) return;
    cancelCfgBrightness();
}

static void openCfgBrightnessModal() {
    if (!s_rootScreen || s_cfgBrightModal || s_cfgBrightBackdrop) return;

    s_cfgBrightOriginal = cfgCoerceBrightness(s_cfg.brightness);
#if FEATURE_LOCK_SCREEN
    s_cfgBrightLockOriginal = cfgCoerceBrightness(s_cfg.lockScreenBrightness);
    s_cfgBrightFocus = 0;
#endif

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgBrightBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgBrightBackdrop, w, h);
    lv_obj_align(s_cfgBrightBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgBrightBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgBrightBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgBrightBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgBrightBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgBrightBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgBrightBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgBrightBackdrop, onCfgBrightBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgBrightModal = lv_obj_create(s_cfgBrightBackdrop);
    lv_obj_set_size(s_cfgBrightModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgBrightModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgBrightModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_cfgBrightModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_cfgBrightModal, LV_DIR_VER);
    lv_obj_add_flag(s_cfgBrightModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgBrightModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgBrightModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgBrightModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgBrightModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgBrightModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgBrightModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgBrightModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgBrightModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgBrightBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgBrightModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Brightness"));

#if FEATURE_LOCK_SCREEN
    s_cfgBrightScreenLabel = lv_label_create(s_cfgBrightModal);
    lv_obj_set_width(s_cfgBrightScreenLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBrightScreenLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgBrightScreenLabel, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgBrightScreenLabel, LV_TEXT_ALIGN_CENTER, 0);
#endif

    s_cfgBrightValue = lv_label_create(s_cfgBrightModal);
    lv_obj_set_width(s_cfgBrightValue, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBrightValue, &lv_font_montserrat_16, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_cfgBrightValue, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_cfgBrightValue, lv_color_hex(0xFFFFFF), 0);
#endif
    lv_obj_set_style_text_align(s_cfgBrightValue, LV_TEXT_ALIGN_CENTER, 0);

    s_cfgBrightSlider = lv_slider_create(s_cfgBrightModal);
    lv_obj_set_width(s_cfgBrightSlider, modalW - 40);
    lv_slider_set_range(s_cfgBrightSlider, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    lv_slider_set_value(s_cfgBrightSlider, s_cfgBrightOriginal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_cfgBrightSlider, lv_color_hex(0x123266), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_cfgBrightSlider, lvColorFrom565(s_ui.selectAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_cfgBrightSlider, lv_color_hex(0xE8F1FF), LV_PART_KNOB);
    lv_obj_add_event_cb(s_cfgBrightSlider, onCfgBrightSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);
#if defined(DEVICE_TDISPLAY_P4)
    // Room this panel has and the 6 px row gap does not give. A slider's knob
    // and its 2 px focus outline both draw outside the slider's own box, so at
    // 6 px they ran into the text either side -- which is what left the lock
    // row looking wedged under the screen one. Space round each slider, a gap
    // between the two groups, and one above the buttons below.
    constexpr int kBrightSliderGap = 8;
    constexpr int kBrightGroupGap  = 12;
    constexpr int kBrightButtonGap = 16;
    lv_obj_set_style_margin_top(s_cfgBrightSlider, kBrightSliderGap, 0);
    lv_obj_set_style_margin_bottom(s_cfgBrightSlider, kBrightSliderGap, 0);
#endif

#if FEATURE_LOCK_SCREEN
    s_cfgBrightLockLabel = lv_label_create(s_cfgBrightModal);
#if defined(DEVICE_TDISPLAY_P4)
    lv_obj_set_style_margin_top(s_cfgBrightLockLabel, kBrightGroupGap, 0);
#endif
    lv_obj_set_width(s_cfgBrightLockLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBrightLockLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgBrightLockLabel, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgBrightLockLabel, LV_TEXT_ALIGN_CENTER, 0);

    s_cfgBrightLockValue = lv_label_create(s_cfgBrightModal);
    lv_obj_set_width(s_cfgBrightLockValue, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBrightLockValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_cfgBrightLockValue, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_cfgBrightLockValue, LV_TEXT_ALIGN_CENTER, 0);

    s_cfgBrightLockSlider = lv_slider_create(s_cfgBrightModal);
    lv_obj_set_width(s_cfgBrightLockSlider, modalW - 40);
    lv_slider_set_range(s_cfgBrightLockSlider, BRIGHTNESS_PCT_MIN, BRIGHTNESS_PCT_MAX);
    lv_slider_set_value(s_cfgBrightLockSlider, s_cfgBrightLockOriginal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_cfgBrightLockSlider, lv_color_hex(0x123266), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_cfgBrightLockSlider, lvColorFrom565(s_ui.selectAccent),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_cfgBrightLockSlider, lv_color_hex(0xE8F1FF), LV_PART_KNOB);
    lv_obj_add_event_cb(s_cfgBrightLockSlider, onCfgBrightLockSliderChanged,
                        LV_EVENT_VALUE_CHANGED, nullptr);
#if defined(DEVICE_TDISPLAY_P4)
    lv_obj_set_style_margin_top(s_cfgBrightLockSlider, kBrightSliderGap, 0);
    lv_obj_set_style_margin_bottom(s_cfgBrightLockSlider, kBrightSliderGap, 0);
#endif
#endif

#if UI_TOUCH_ONLY_PROFILE
    appendHeltecCancelSaveRow(
        s_cfgBrightModal,
        [](lv_event_t *e) { LV_UNUSED(e); cancelCfgBrightness(); },
        [](lv_event_t *e) { LV_UNUSED(e); applyCfgBrightness(); });
#if defined(DEVICE_TDISPLAY_P4)
    // The row is the modal's last child; set the gap on it from here rather
    // than teach the shared builder about one board.
    lv_obj_set_style_margin_top(lv_obj_get_child(s_cfgBrightModal, -1),
                                kBrightButtonGap, 0);
#endif
#else
    lv_obj_t *hint = lv_label_create(s_cfgBrightModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if FEATURE_LOCK_SCREEN
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_label_set_text(hint, TR("j/k=Adjust  Wheel=Row  Enter=Save  Backspace=Cancel"));
#elif defined(DEVICE_M9)
    lv_label_set_text(hint, TR("j/k or </>=Adjust  Up/Dn=Row  Enter=Save  Back=Cancel"));
#else
    lv_label_set_text(hint, TR("j/k=Adjust  </>=Row  Enter=Save  Backspace=Cancel"));
#endif
#else
    lv_label_set_text(hint, TR("j/k=Adjust  Enter=Save  Backspace=Cancel"));
#endif
#endif

#if FEATURE_LOCK_SCREEN
    // Seeds the lock row's label without touching the panel: the modal opens
    // focused on the screen row, so the level under the user's eyes should stay
    // the screen one until they move down to the other.
    if (lvObjValid(s_cfgBrightLockValue)) {
        char lockBuf[8];
        snprintf(lockBuf, sizeof(lockBuf), "%u%%", (unsigned)s_cfgBrightLockOriginal);
        lv_label_set_text(s_cfgBrightLockValue, lockBuf);
    }
    refreshCfgBrightnessFocus();
#endif
    setCfgBrightnessPreview(s_cfgBrightOriginal);
}

#if HAS_VOLUME_CONTROL
// ── Notification volume picker ───────────────────────────────────────────────
// Same shape as the brightness slider: 10% steps, live preview, Enter saves and
// closing without it restores the level we opened with. Unlike brightness the
// effect is not visible, so each change plays the alert tone at the new level —
// rate-limited, because holding a key would otherwise machine-gun the speaker.
static constexpr uint32_t kVolPreviewMinGapMs = 400;

static void setCfgVolumePreview(int pct, bool audible) {
    s_cfg.volumePct = cfgCoerceVolume(pct);
    if (lvObjValid(s_cfgVolValue)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)s_cfg.volumePct);
        lv_label_set_text(s_cfgVolValue, buf);
    }
    if (!audible || s_cfg.volumePct == 0) return;
    const uint32_t now = millis();
    if ((uint32_t)(now - s_cfgVolLastPreviewMs) < kVolPreviewMinGapMs) return;
    s_cfgVolLastPreviewMs = now;
    playVolumePreviewTone();   // plays even when notification sound is OFF
}

static void closeCfgVolumeModal() {
    if (lvObjValid(s_cfgVolBackdrop)) {
        lv_obj_del(s_cfgVolBackdrop);
    } else if (lvObjValid(s_cfgVolModal)) {
        lv_obj_del(s_cfgVolModal);
    }
    s_cfgVolBackdrop = nullptr;
    s_cfgVolModal = nullptr;
    s_cfgVolSlider = nullptr;
    s_cfgVolValue = nullptr;
}

// Teardown-safe, like the brightness equivalent: the settings screen can close
// with this open, and an unapplied preview must not linger in s_cfg where the
// next unrelated save would persist it.
static void revertCfgVolumePreview() {
    if (!s_cfgVolModal && !s_cfgVolBackdrop) return;
    s_cfg.volumePct = s_cfgVolOriginal;
    closeCfgVolumeModal();
}

static void cancelCfgVolume() {
    revertCfgVolumePreview();
    refreshCfgModal();
}

static void applyCfgVolume() {
    persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Volume: %u%%"),
             (unsigned)s_cfg.volumePct);
    closeCfgVolumeModal();
    refreshCfgModal();
}

static void stepCfgVolume(int steps) {
    if (!steps) return;
    const int next = (int)s_cfg.volumePct + steps * VOLUME_PCT_STEP;
    const uint8_t coerced = cfgCoerceVolume(next);
    if (coerced == s_cfg.volumePct) return;
    setCfgVolumePreview(coerced, true);
    if (lvObjValid(s_cfgVolSlider)) {
        lv_slider_set_value(s_cfgVolSlider, coerced, LV_ANIM_OFF);
    }
}

static void onCfgVolSliderChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target_obj(e);
    if (!slider) return;
    setCfgVolumePreview((int)lv_slider_get_value(slider), true);
}

static void onCfgVolBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgVolBackdrop) return;
    cancelCfgVolume();
}

static void openCfgVolumeModal() {
    if (!s_rootScreen || s_cfgVolModal || s_cfgVolBackdrop) return;

    s_cfgVolOriginal = cfgCoerceVolume((int)s_cfg.volumePct);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgVolBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgVolBackdrop, w, h);
    lv_obj_align(s_cfgVolBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgVolBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgVolBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgVolBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgVolBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgVolBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgVolBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgVolBackdrop, onCfgVolBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgVolModal = lv_obj_create(s_cfgVolBackdrop);
    lv_obj_set_size(s_cfgVolModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgVolModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgVolModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_cfgVolModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_cfgVolModal, LV_DIR_VER);
    lv_obj_add_flag(s_cfgVolModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgVolModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgVolModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgVolModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgVolModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgVolModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgVolModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgVolModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgVolModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgVolBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgVolModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Volume"));

    s_cfgVolValue = lv_label_create(s_cfgVolModal);
    lv_obj_set_width(s_cfgVolValue, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgVolValue, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_cfgVolValue, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_cfgVolValue, LV_TEXT_ALIGN_CENTER, 0);

    s_cfgVolSlider = lv_slider_create(s_cfgVolModal);
    lv_obj_set_width(s_cfgVolSlider, modalW - 40);
    lv_slider_set_range(s_cfgVolSlider, VOLUME_PCT_MIN, VOLUME_PCT_MAX);
    lv_slider_set_value(s_cfgVolSlider, s_cfgVolOriginal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_cfgVolSlider, lv_color_hex(0x123266), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_cfgVolSlider, lvColorFrom565(s_ui.selectAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_cfgVolSlider, lv_color_hex(0xE8F1FF), LV_PART_KNOB);
    lv_obj_add_event_cb(s_cfgVolSlider, onCfgVolSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);
#if defined(DEVICE_TDISPLAY_P4)
    // Same crowding the brightness sliders have on this panel: the knob and its
    // focus outline draw outside the slider's box, so the 6 px row gap leaves
    // them touching the text above and the buttons below. See there.
    constexpr int kVolSliderGap = 8;
    constexpr int kVolButtonGap = 16;
    lv_obj_set_style_margin_top(s_cfgVolSlider, kVolSliderGap, 0);
    lv_obj_set_style_margin_bottom(s_cfgVolSlider, kVolSliderGap, 0);
#endif

    // This modal used to skip the Cancel/Save row on the grounds that the only
    // touch-only board was the Heltec, whose BOARD_BUZZER 6 puts it outside
    // HAS_VOLUME_CONTROL — so the row was unreachable. The profile has since
    // grown boards that do have amplitude control (the Wio Tracker L2's ES8311
    // and the T-Display P4), where the slider stages a level with no way to
    // commit it: no Enter, and a tap outside cancels.
#if UI_TOUCH_ONLY_PROFILE
    appendHeltecCancelSaveRow(
        s_cfgVolModal,
        [](lv_event_t *e) { LV_UNUSED(e); cancelCfgVolume(); },
        [](lv_event_t *e) { LV_UNUSED(e); applyCfgVolume(); });
#if defined(DEVICE_TDISPLAY_P4)
    // The row is the modal's last child; set the gap on it from here rather
    // than teach the shared builder about one board.
    lv_obj_set_style_margin_top(lv_obj_get_child(s_cfgVolModal, -1), kVolButtonGap, 0);
#endif
#else
    lv_obj_t *hint = lv_label_create(s_cfgVolModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, TR("j/k=Adjust  Enter=Save  Backspace=Cancel"));
#endif

    // Silent on open: announce the level visually, do not beep before asked.
    setCfgVolumePreview(s_cfgVolOriginal, false);
}
#endif  // HAS_VOLUME_CONTROL

// ── Ordered-list slider picker ───────────────────────────────────────────────
// Nothing is applied until Save. Both settings behind this change what the
// device puts on the air or how it behaves when unattended, which is not
// something to do to someone on the way past a row.

static void cfgSliderShow(int idx) {
    if (!s_cfgSliderSpec) return;
    if (idx < 0) idx = 0;
    if (idx >= s_cfgSliderSpec->count) idx = s_cfgSliderSpec->count - 1;
    s_cfgSliderStaged = idx;
    if (lvObjValid(s_cfgSliderValue) && s_cfgSliderSpec->labelFor) {
        lv_label_set_text(s_cfgSliderValue, TR(s_cfgSliderSpec->labelFor(idx)));
    }
}

static void closeCfgSliderModal() {
    if (lvObjValid(s_cfgSliderBackdrop)) {
        lv_obj_del(s_cfgSliderBackdrop);
    } else if (lvObjValid(s_cfgSliderModal)) {
        lv_obj_del(s_cfgSliderModal);
    }
    s_cfgSliderBackdrop = nullptr;
    s_cfgSliderModal = nullptr;
    s_cfgSliderCtl = nullptr;
    s_cfgSliderValue = nullptr;
    s_cfgSliderSpec = nullptr;
    s_cfgSliderCheck = nullptr;
}

// Teardown-safe, like the brightness and volume equivalents: the settings
// screen can close with this open. Nothing has to be restored — the slider only
// ever moved s_cfgSliderStaged — so this is just the close.
static void cancelCfgSlider() {
    if (!s_cfgSliderModal && !s_cfgSliderBackdrop) return;
    closeCfgSliderModal();
}

static void cancelCfgSliderAndRefresh() {
    cancelCfgSlider();
    refreshCfgModal();
}

static void applyCfgSlider() {
    const CfgSliderPicker *spec = s_cfgSliderSpec;
    if (!spec) { closeCfgSliderModal(); return; }
    const int idx = (s_cfgSliderStaged >= 0 && s_cfgSliderStaged < spec->count)
                        ? s_cfgSliderStaged : 0;
    const bool checked = s_cfgSliderCheckStaged;
    // Close first: apply() writes s_cfgStatus, and refreshCfgModal() below has
    // to see the modal already gone.
    closeCfgSliderModal();
    // Before apply(), which may act on it straight away (Discovery starts its
    // run from there).
    if (spec->checkValue) *spec->checkValue = checked;
    if (spec->apply) spec->apply(idx);
    refreshCfgModal();
}

static void stepCfgSlider(int steps) {
    if (!steps || !s_cfgSliderSpec) return;
    const int next = s_cfgSliderStaged + steps;
    if (next < 0 || next >= s_cfgSliderSpec->count) return;
    cfgSliderShow(next);
    if (lvObjValid(s_cfgSliderCtl)) {
        lv_slider_set_value(s_cfgSliderCtl, next, LV_ANIM_OFF);
    }
}

static void onCfgSliderChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target_obj(e);
    if (!slider) return;
    cfgSliderShow((int)lv_slider_get_value(slider));
}

static bool cfgSliderCheckOnly(const CfgSliderPicker *spec) {
    return spec && spec->count == 1 && spec->checkLabel && spec->checkValue;
}

// Keyboard builds reach the checkbox with Space; touch builds tap it. Both land
// here so the staged value and the box on screen cannot disagree.
static void toggleCfgSliderCheck() {
    if (!s_cfgSliderSpec || !s_cfgSliderSpec->checkValue) return;
    s_cfgSliderCheckStaged = !s_cfgSliderCheckStaged;
    if (lvObjValid(s_cfgSliderCheck)) {
        if (s_cfgSliderCheckStaged) lv_obj_add_state(s_cfgSliderCheck, LV_STATE_CHECKED);
        else lv_obj_remove_state(s_cfgSliderCheck, LV_STATE_CHECKED);
    }
    cfgSliderShow(s_cfgSliderStaged);   // the label may follow the box
}

#if UI_ARROW_GRID_NAV
// Outlines whichever control has focus. Same halo the Brightness rows use
// (paintCfgBrightnessRow()), so "which one am I on" reads the same way.
static void refreshCfgSliderFocus() {
    const lv_color_t accent = lvColorFrom565(s_ui.selectAccent);
    lv_obj_t *const ctls[2] = { s_cfgSliderCtl, s_cfgSliderCheck };
    for (int i = 0; i < 2; i++) {
        if (!lvObjValid(ctls[i])) continue;
        const bool focused = (s_cfgSliderFocus == i);
        lv_obj_set_style_outline_width(ctls[i], focused ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(ctls[i], 2, LV_PART_MAIN);
        lv_obj_set_style_outline_color(ctls[i], accent, LV_PART_MAIN);
        lv_obj_set_style_outline_opa(ctls[i], focused ? LV_OPA_COVER : LV_OPA_TRANSP,
                                     LV_PART_MAIN);
    }
    // The modal is height-capped and scrolls on the shorter panels.
    lv_obj_t *focused = ctls[s_cfgSliderFocus ? 1 : 0];
    if (lvObjValid(focused)) lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
}
#endif

static void onCfgSliderCheckChanged(lv_event_t *e) {
    lv_obj_t *box = lv_event_get_target_obj(e);
    if (!box) return;
    s_cfgSliderCheckStaged = lv_obj_has_state(box, LV_STATE_CHECKED);
    cfgSliderShow(s_cfgSliderStaged);
}

static void onCfgSliderBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgSliderBackdrop) return;
    cancelCfgSliderAndRefresh();
}

static void openCfgSliderModal(const CfgSliderPicker *spec, int startIdx) {
    if (!s_rootScreen || s_cfgSliderModal || s_cfgSliderBackdrop) return;
    if (!spec || spec->count <= 0) return;

    s_cfgSliderSpec = spec;
    if (startIdx < 0) startIdx = 0;
    if (startIdx >= spec->count) startIdx = spec->count - 1;
    s_cfgSliderStaged = startIdx;
    s_cfgSliderCheckStaged = spec->checkValue ? *spec->checkValue : false;
    const bool checkOnly = cfgSliderCheckOnly(spec);
#if UI_ARROW_GRID_NAV
    s_cfgSliderFocus = checkOnly ? 1 : 0;   // on the slider, when there is one
#endif

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgSliderBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgSliderBackdrop, w, h);
    lv_obj_align(s_cfgSliderBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgSliderBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgSliderBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgSliderBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgSliderBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgSliderBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgSliderBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgSliderBackdrop, onCfgSliderBackdropPressed,
                        LV_EVENT_CLICKED, nullptr);

    s_cfgSliderModal = lv_obj_create(s_cfgSliderBackdrop);
    lv_obj_set_size(s_cfgSliderModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgSliderModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgSliderModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_cfgSliderModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_cfgSliderModal, LV_DIR_VER);
    lv_obj_add_flag(s_cfgSliderModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgSliderModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgSliderModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgSliderModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgSliderModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgSliderModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgSliderModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgSliderModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgSliderModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgSliderBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgSliderModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR(spec->title));

    s_cfgSliderValue = lv_label_create(s_cfgSliderModal);
    lv_obj_set_width(s_cfgSliderValue, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgSliderValue, &lv_font_montserrat_16, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_cfgSliderValue, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_cfgSliderValue, lv_color_hex(0xFFFFFF), 0);
#endif
    lv_obj_set_style_text_align(s_cfgSliderValue, LV_TEXT_ALIGN_CENTER, 0);

    if (!checkOnly) {
        s_cfgSliderCtl = lv_slider_create(s_cfgSliderModal);
        lv_obj_set_width(s_cfgSliderCtl, modalW - 40);
        // One stop per list entry: the slider cannot land between two real values.
        lv_slider_set_range(s_cfgSliderCtl, 0, spec->count - 1);
        lv_slider_set_value(s_cfgSliderCtl, startIdx, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_cfgSliderCtl, lv_color_hex(0x123266), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_cfgSliderCtl, lvColorFrom565(s_ui.selectAccent),
                                  LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_cfgSliderCtl, lv_color_hex(0xE8F1FF), LV_PART_KNOB);
        lv_obj_add_event_cb(s_cfgSliderCtl, onCfgSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);

        // Which end is which. The value label above names the current stop, so
        // these only have to anchor the direction of travel.
        if (spec->leftEnd && spec->rightEnd) {
            lv_obj_t *ends = lv_label_create(s_cfgSliderModal);
            lv_obj_set_width(ends, lv_pct(100));
            lv_obj_set_style_text_font(ends, &lv_font_montserrat_10, 0);
            // A text colour, not a line colour. This was 0x8FB5E6, which
            // themedColorHex() maps to s_ui.dividerHi -- panelAlt blended a third of
            // the way toward the accent. On a dark theme that lands pale and reads
            // fine, which is why it survived; on a light one panelAlt is already
            // near-white, so the blend came out as near-white text on the
            // near-white panel behind it and "30 sec <-> 6 hours" all but vanished.
            //
            // 0xD9E8FF is s_ui.textMain, the same ink the title above uses: near
            // black on a light theme, near white on a dark one. Hard-coding black
            // would have fixed the light themes by breaking every dark one, since
            // this modal is one surface whose colour the theme decides.
            lv_obj_set_style_text_color(ends, lv_color_hex(0xD9E8FF), 0);
            lv_obj_set_style_text_align(ends, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text_fmt(ends, TR("%s  <->  %s"), TR(spec->leftEnd), TR(spec->rightEnd));
        }
    }

    const bool hasCheck = (spec->checkLabel && spec->checkValue);
    if (hasCheck) {
        s_cfgSliderCheck = lv_checkbox_create(s_cfgSliderModal);
        lv_checkbox_set_text(s_cfgSliderCheck, TR(spec->checkLabel));
        lv_obj_set_style_text_font(s_cfgSliderCheck, &lv_font_montserrat_12, 0);
        // The same theme-mapped ink as the end captions above; see there.
        lv_obj_set_style_text_color(s_cfgSliderCheck, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_margin_top(s_cfgSliderCheck, 4, 0);
        if (s_cfgSliderCheckStaged) lv_obj_add_state(s_cfgSliderCheck, LV_STATE_CHECKED);
        lv_obj_add_event_cb(s_cfgSliderCheck, onCfgSliderCheckChanged,
                            LV_EVENT_VALUE_CHANGED, nullptr);
    }

    // Space toggles the checkbox on every keyboard, so the hint says so wherever
    // there is one: always on keyboard builds, and on touch builds while a
    // keyboard is attached -- the same rule as the Clear Nodes options.
#if UI_TOUCH_ONLY_PROFILE
    if (hasCheck && keyboardAttached())
#endif
    {
        lv_obj_t *hint = lv_label_create(s_cfgSliderModal);
        lv_obj_set_width(hint, lv_pct(100));
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        if (checkOnly) {
            lv_label_set_text_fmt(hint, TR("Space=Toggle  Enter=Save  %s=Cancel"),
                                  modalCloseKeyLabel());
        } else if (hasCheck) {
#if UI_ARROW_GRID_NAV
            lv_label_set_text_fmt(hint,
                                  TR("Up/Down=Select  Left/Right=Change  Space=Toggle  Enter=Save  %s=Cancel"),
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(hint, TR("Move=Adjust  Space=Toggle  Enter=Save  %s=Cancel"),
                                  modalCloseKeyLabel());
#endif
        } else {
            lv_label_set_text_fmt(hint, TR("Move=Adjust  Enter=Save  %s=Cancel"), modalCloseKeyLabel());
        }
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    }
#if UI_TOUCH_ONLY_PROFILE
    appendHeltecCancelSaveRow(
        s_cfgSliderModal,
        [](lv_event_t *e) { LV_UNUSED(e); cancelCfgSliderAndRefresh(); },
        [](lv_event_t *e) { LV_UNUSED(e); applyCfgSlider(); });
#endif

    cfgSliderShow(startIdx);
#if UI_ARROW_GRID_NAV
    if (hasCheck) refreshCfgSliderFocus();
#endif
}

// ── Location precision ───────────────────────────────────────────────────────

static const char *cfgPosPrecLabelFor(int idx) {
    static char buf[48];
    if (idx < 0 || idx >= kPositionPrecisionCount) idx = 0;
    if (kPositionPrecisions[idx].bits >= 32) return TR("Precise");
    snprintf(buf, sizeof(buf), TR("Within %s"), kPositionPrecisions[idx].label);
    return buf;
}

static void cfgPosPrecApply(int idx) {
    if (idx < 0 || idx >= kPositionPrecisionCount) idx = 0;
    const uint8_t chosen = positionPrecisionCoerce(kPositionPrecisions[idx].bits);
    s_cfg.positionPrecision = chosen;
    persistConfigToPrefs();
    if (chosen >= 32) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Location sent exactly"));
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Location rounded to %s"),
                 TR(positionPrecisionLabel(chosen)));
    }
}

static void openCfgPosPrecModal() {
    int startIdx = 0;
    const uint8_t cur = positionPrecisionCoerce(s_cfg.positionPrecision);
    for (int i = 0; i < kPositionPrecisionCount; i++) {
        if (kPositionPrecisions[i].bits == cur) { startIdx = i; break; }
    }
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Location Precision"),
        kPositionPrecisionCount,
        cfgPosPrecLabelFor,
        cfgPosPrecApply,
        TR_NOOP("exact"),
        TR_NOOP("vague"),
    };
    openCfgSliderModal(&kSpec, startIdx);
}

#if HAS_LIGHT_NOTIFY
// ── Notification light timeout ───────────────────────────────────────────────

static const char *cfgLightTimeoutLabelFor(int idx) {
    if (idx < 0 || idx >= kNotifyLightTimeoutCount) idx = 0;
    if (kNotifyLightTimeouts[idx].secs == 0) return TR("Until read");
    return TR(kNotifyLightTimeouts[idx].label);
}

static void cfgLightTimeoutApply(int idx) {
    if (idx < 0 || idx >= kNotifyLightTimeoutCount) idx = 0;
    s_cfg.notifyLightTimeoutS = kNotifyLightTimeouts[idx].secs;
    persistConfigToPrefs();
    if (s_cfg.notifyLightTimeoutS == 0) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Light reminds until read"));
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Light reminds for %s"),
                 TR(notifyLightTimeoutName(s_cfg.notifyLightTimeoutS)));
    }
}

static void openCfgLightTimeoutModal() {
    int startIdx = 0;
    for (int i = 0; i < kNotifyLightTimeoutCount; i++) {
        if (kNotifyLightTimeouts[i].secs == s_cfg.notifyLightTimeoutS) { startIdx = i; break; }
    }
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Notification Light Timeout"),
        kNotifyLightTimeoutCount,
        cfgLightTimeoutLabelFor,
        cfgLightTimeoutApply,
        TR_NOOP("5 sec"),
        TR_NOOP("until read"),
    };
    openCfgSliderModal(&kSpec, startIdx);
}
#endif  // HAS_LIGHT_NOTIFY

#if HAS_POWER_LED_LEVEL
// ── Power light brightness ───────────────────────────────────────────────────

static const char *cfgPowerLedLabelFor(int idx) {
    if (idx < 0 || idx >= kPowerLedLevelCount) idx = 0;
    return TR(kPowerLedLevels[idx].label);
}

static void cfgPowerLedApply(int idx) {
    if (idx < 0 || idx >= kPowerLedLevelCount) idx = 0;
    s_cfg.powerLedPct = kPowerLedLevels[idx].pct;
    persistConfigToPrefs();
    tanmatsuRefreshStatusLeds();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Status Lights: %s"),
             TR(powerLedLevelName(s_cfg.powerLedPct)));
}

static void openCfgPowerLedModal() {
    int startIdx = 0;
    const uint8_t cur = cfgCoercePowerLedPct(s_cfg.powerLedPct);
    for (int i = 0; i < kPowerLedLevelCount; i++) {
        if (kPowerLedLevels[i].pct == cur) { startIdx = i; break; }
    }
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Status Lights"),
        kPowerLedLevelCount,
        cfgPowerLedLabelFor,
        cfgPowerLedApply,
        TR_NOOP("dim"),
        TR_NOOP("off"),
    };
    openCfgSliderModal(&kSpec, startIdx);
}
#endif  // HAS_POWER_LED_LEVEL

#if HAS_KB_BACKLIGHT_LEVEL
#if !defined(DEVICE_TDISPLAY_P4)   // the P4 sends its row to F11, not a picker
static const char *cfgKbBacklightLabelFor(int idx) {
    if (idx < 0 || idx >= kKbBacklightLevelCount) idx = 0;
    return TR(kKbBacklightLevels[idx].label);
}
#endif

// Pushes the stored level at the hardware, by whatever route this board takes.
// The Pro is the one that cannot simply be told a number: Alt+B owns whether the
// keyboard is lit at all, so the level has to arrive through that gate or a
// change here would light a keyboard the user had turned off.
static void applyKbBacklightSetting() {
#if defined(DEVICE_TDECK_PRO)
    setPagerKeyboardBacklight(tdeckProKeyboardBacklightEnabled());
#elif defined(DEVICE_TDISPLAY_P4)
    // Through the same gate as the Pro, for the same reason: F7 decides whether
    // the keyboard is lit, and a sleeping screen has it dark regardless.
    setPagerKeyboardBacklight(!s_screenAsleep);
#elif defined(DEVICE_TANMATSU)
    // Dark while the screen sleeps; wakeScreen() relights it at this level.
    setPagerKeyboardBacklight(!s_screenAsleep);
#elif defined(DEVICE_TDECK)
    // The level is where it rests -- unless the idle timeout has it dark.
    keyboardSetKeypadBacklight(kbLightIdleDark() ? 0 : s_cfg.kbBacklightLevel);
#else
    keyboardSetKeypadBacklight(s_cfg.kbBacklightLevel);
#endif
}

#if !defined(DEVICE_TDISPLAY_P4)
static void cfgKbBacklightApply(int idx) {
    if (idx < 0 || idx >= kKbBacklightLevelCount) idx = 0;
    const uint8_t was = s_cfg.kbBacklightLevel;
    s_cfg.kbBacklightLevel = kKbBacklightLevels[idx].level;
    persistConfigToPrefs();
    applyKbBacklightSetting();

    LV_UNUSED(was);
    if (s_cfg.kbBacklightLevel == 0) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Keyboard light off"));
#if defined(DEVICE_M9)
    } else if (was == 0) {
        // M9 only, and the one change that does not take effect now: its
        // controller keeps the auto-light disabled until the next power cycle,
        // so the register write lands and nothing happens until then. Said here
        // rather than left to be discovered as a setting that failed.
        //
        // The T-Deck has no such rule -- its backlight is a plain duty the
        // keyboard applies immediately, in either direction.
        snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                 TR("Keyboard light %s - after a power cycle"),
                 TR(kbBacklightLevelName(s_cfg.kbBacklightLevel)));
#endif
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Keyboard light %s"),
                 TR(kbBacklightLevelName(s_cfg.kbBacklightLevel)));
    }
}

static void openCfgKbBacklightModal() {
    int startIdx = 0;
    for (int i = 0; i < kKbBacklightLevelCount; i++) {
        if (kKbBacklightLevels[i].level == s_cfg.kbBacklightLevel) { startIdx = i; break; }
    }
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Keyboard Light"),
        kKbBacklightLevelCount,
        cfgKbBacklightLabelFor,
        cfgKbBacklightApply,
        TR_NOOP("low"),
        TR_NOOP("off"),
    };
    openCfgSliderModal(&kSpec, startIdx);
}
#endif  // !DEVICE_TDISPLAY_P4
#endif  // HAS_KB_BACKLIGHT_LEVEL

#if HAS_KB_LIGHT_TIMEOUT
// ── Keyboard light timeout (issue #104) ──────────────────────────────────────
// The same ordered slider as the screen and notification-light timeouts.
static const char *cfgKbLightTimeoutLabelFor(int idx) {
    if (idx < 0 || idx >= kKbLightTimeoutCount) idx = 0;
    return TR(kKbLightTimeouts[idx].label);
}

static void cfgKbLightTimeoutApply(int idx) {
    if (idx < 0 || idx >= kKbLightTimeoutCount) idx = 0;
    s_cfg.kbLightTimeoutS = kKbLightTimeouts[idx].secs;
    persistConfigToPrefs();
    // Nothing to apply by hand: the press that saved this was input, so the
    // light is lit, and serviceKbLightTimeout() measures the new value from it.
    if (s_cfg.kbLightTimeoutS == 0) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Keyboard light stays on"));
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Keyboard light off after %s"),
                 TR(kbLightTimeoutName(s_cfg.kbLightTimeoutS)));
    }
}

static void openCfgKbLightTimeoutModal() {
    int startIdx = kKbLightTimeoutCount - 1;   // Never, if the stored value is off-list
    for (int i = 0; i < kKbLightTimeoutCount; i++) {
        if (kKbLightTimeouts[i].secs == s_cfg.kbLightTimeoutS) { startIdx = i; break; }
    }
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Keyboard Light Timeout"),
        kKbLightTimeoutCount,
        cfgKbLightTimeoutLabelFor,
        cfgKbLightTimeoutApply,
        TR_NOOP("10 sec"),
        TR_NOOP("never"),
    };
    openCfgSliderModal(&kSpec, startIdx);
}
#endif  // HAS_KB_LIGHT_TIMEOUT



// ── Screen timeout ───────────────────────────────────────────────────────────
// How long the panel stays lit with no input before it sleeps. Same ordered
// slider as the light timeout above, and ascending with Never last for the same
// reason: left to right has to mean "stays on longer" the whole way across.
//
// The web form takes a free-form number of seconds, so a stored value need not
// be one of these. Nothing coerces it — the slider opens on the nearest stop
// and only writes a listed value when the user actually saves, which leaves a
// hand-set 45 alone until they choose to change it.
struct ScreenTimeoutOption {
    uint32_t    secs;
    const char *label;
};

static const ScreenTimeoutOption kScreenTimeouts[] = {
    {   15, TR_NOOP("15 sec") },
    {   30, TR_NOOP("30 sec") },
    {   60, TR_NOOP("1 min")  },
    {  120, TR_NOOP("2 min")  },
    {  300, TR_NOOP("5 min")  },
    {  600, TR_NOOP("10 min") },
    // A device on a bench is watched rather than carried, and the only thing
    // above ten minutes used to be Never — which is a different decision, not a
    // longer one. These two also stop the slider quietly shortening a browser-set
    // value: 1800 opened on 10 min and was written back as 600 the moment the
    // user saved, even if they had only come to look (issue #93).
    { 1800, TR_NOOP("30 min") },
    { 3600, TR_NOOP("60 min") },
    {    0, TR_NOOP("Never")  },
};
static constexpr int kScreenTimeoutCount =
    (int)(sizeof(kScreenTimeouts) / sizeof(kScreenTimeouts[0]));

// Label for any stored value, listed or not, so the config row can render a
// number the slider cannot itself produce. The fallback buffer is static
// because callers copy the text immediately (lv_label_set_text, snprintf).
static const char *screenTimeoutName(uint32_t secs) {
    for (int i = 0; i < kScreenTimeoutCount; i++) {
        if (kScreenTimeouts[i].secs == secs) return TR(kScreenTimeouts[i].label);
    }
    static char buf[16];
    if (secs >= 60 && (secs % 60) == 0) {
        snprintf(buf, sizeof(buf), TR("%u min"), (unsigned)(secs / 60));
    } else {
        snprintf(buf, sizeof(buf), TR("%u sec"), (unsigned)secs);
    }
    return buf;
}

// Nearest listed stop to a stored value. Never (0) is matched exactly and
// otherwise skipped by value rather than by index, so the table stays free to
// be reordered and a stray 5 opens on 15 seconds instead of switching the
// timeout off.
static int screenTimeoutNearestIdx(uint32_t secs) {
    int best = 0;
    long bestDelta = -1;
    for (int i = 0; i < kScreenTimeoutCount; i++) {
        if (kScreenTimeouts[i].secs == secs) return i;
        if (kScreenTimeouts[i].secs == 0) continue;
        long delta = (long)kScreenTimeouts[i].secs - (long)secs;
        if (delta < 0) delta = -delta;
        if (bestDelta < 0 || delta < bestDelta) {
            bestDelta = delta;
            best = i;
        }
    }
    return best;
}

static const char *cfgScreenTimeoutLabelFor(int idx) {
    if (idx < 0 || idx >= kScreenTimeoutCount) idx = 0;
    return TR(kScreenTimeouts[idx].label);
}

static void cfgScreenTimeoutApply(int idx) {
    if (idx < 0 || idx >= kScreenTimeoutCount) idx = 0;
    s_cfg.screenOnSecs = kScreenTimeouts[idx].secs;
    persistConfigToPrefs();
    // Restart the idle clock. Without this the new timeout is measured against
    // whenever input was last recorded, so shortening it from 10 minutes to 15
    // seconds could put the panel out before the user has read the confirmation.
    s_lastActivityMs = millis();
    if (s_cfg.screenOnSecs == 0) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Screen stays on"));
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Screen sleeps after %s"),
                 screenTimeoutName(s_cfg.screenOnSecs));
    }
}

static void openCfgScreenTimeoutModal() {
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Screen Timeout"),
        kScreenTimeoutCount,
        cfgScreenTimeoutLabelFor,
        cfgScreenTimeoutApply,
        TR_NOOP("15 sec"),
        TR_NOOP("never"),
    };
    openCfgSliderModal(&kSpec, screenTimeoutNearestIdx(s_cfg.screenOnSecs));
}

#if FEATURE_LOCK_SCREEN
// ── Lock screen dwell ────────────────────────────────────────────────────────
// How long the lock screen stays lit before the panel goes out for real. Same
// stepped-slider shape as Screen Timeout above, and for the same reason: the
// values worth choosing are a short list, and a free-running number would let
// someone pick 37 minutes without meaning to.
//
// Five to sixty in five-minute steps, then "Stay on". Stay on is last rather
// than first because it is the expensive answer — the panel never goes out on
// its own — and a list you scroll through should not open on it.
struct LockScreenOffOption { uint32_t secs; const char *label; };
static const LockScreenOffOption kLockScreenOffs[] = {
    // Three short steps below the five-minute grid. A lock screen is a glance
    // surface, and half a minute of it is a reasonable thing to want; going
    // straight from 30 sec to 5 min would make the slider useless in between.
    {   30, TR_NOOP("30 sec") },
    {   60, TR_NOOP("1 min")  },
    {  120, TR_NOOP("2 min")  },
    {  300, TR_NOOP("5 min")  },
    {  600, TR_NOOP("10 min") },
    {  900, TR_NOOP("15 min") },
    { 1200, TR_NOOP("20 min") },
    { 1500, TR_NOOP("25 min") },
    { 1800, TR_NOOP("30 min") },
    { 2100, TR_NOOP("35 min") },
    { 2400, TR_NOOP("40 min") },
    { 2700, TR_NOOP("45 min") },
    { 3000, TR_NOOP("50 min") },
    { 3300, TR_NOOP("55 min") },
    { 3600, TR_NOOP("60 min") },
    {    0, TR_NOOP("Stay on") },
};
static constexpr int kLockScreenOffCount =
    (int)(sizeof(kLockScreenOffs) / sizeof(kLockScreenOffs[0]));

// Label for any stored value, listed or not, so the config row can render
// something a hand-edited YAML produced. Static buffer for the same reason
// screenTimeoutName()'s is: callers copy the text immediately.
static const char *lockScreenOffName(uint32_t secs) {
    for (int i = 0; i < kLockScreenOffCount; i++) {
        if (kLockScreenOffs[i].secs == secs) return TR(kLockScreenOffs[i].label);
    }
    static char buf[16];
    if (secs < 60U) snprintf(buf, sizeof(buf), TR("%u sec"), (unsigned)secs);
    else            snprintf(buf, sizeof(buf), TR("%u min"), (unsigned)(secs / 60));
    return buf;
}

// Nearest listed stop, with "Stay on" (0) matched exactly and never approached
// by distance — the same rule screenTimeoutNearestIdx() follows, and for the
// same reason: 0 is the smallest number in the table and every stray value
// would otherwise land on it.
static int lockScreenOffNearestIdx(uint32_t secs) {
    int best = 0;
    long bestDelta = -1;
    for (int i = 0; i < kLockScreenOffCount; i++) {
        if (kLockScreenOffs[i].secs == secs) return i;
        if (kLockScreenOffs[i].secs == LOCK_SCREEN_OFF_NEVER) continue;
        long delta = (long)kLockScreenOffs[i].secs - (long)secs;
        if (delta < 0) delta = -delta;
        if (bestDelta < 0 || delta < bestDelta) {
            bestDelta = delta;
            best = i;
        }
    }
    return best;
}

static const char *cfgLockScreenOffLabelFor(int idx) {
    if (idx < 0 || idx >= kLockScreenOffCount) idx = 0;
    return TR(kLockScreenOffs[idx].label);
}

static void cfgLockScreenOffApply(int idx) {
    if (idx < 0 || idx >= kLockScreenOffCount) idx = 0;
    s_cfg.lockScreenOffSecs = kLockScreenOffs[idx].secs;
    persistConfigToPrefs();
    // Restart the dwell against the new value. Only reachable with the lock
    // screen down, but shortening 60 minutes to 5 should not be measured from
    // whenever the last one started either way.
    s_lockScreenSinceMs = millis();
    if (s_cfg.lockScreenOffSecs == LOCK_SCREEN_OFF_NEVER) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Lock screen stays on"));
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Screen off after %s on lock"),
                 lockScreenOffName(s_cfg.lockScreenOffSecs));
    }
}

static void openCfgLockScreenOffModal() {
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Lock Screen Off"),
        kLockScreenOffCount,
        cfgLockScreenOffLabelFor,
        cfgLockScreenOffApply,
        TR_NOOP("5 min"),
        TR_NOOP("stay on"),
    };
    openCfgSliderModal(&kSpec, lockScreenOffNearestIdx(s_cfg.lockScreenOffSecs));
}
#endif  // FEATURE_LOCK_SCREEN

// ── Battery calibration ──────────────────────────────────────────────────────
// Same slider shape as brightness, trimming the measured battery voltage by
// +/-20% in 0.5% steps. The reading updates live while the slider moves, so the
// procedure is: measure the pack with a meter, adjust until the number here
// agrees, save.
//
// Worth being clear about what this does and does not fix, because the reason
// people reach for it is usually "my percentage is wrong". Displayed percent
// comes from a resting-voltage curve, so a voltage read even 50 mV off moves it
// by several points across the flat middle of the Li-ion curve — that this
// corrects. It does not scale with pack capacity: a 1000 mAh and an 8000 mAh
// cell at 3.9 V are both at the same state of charge, they just hold it for
// different lengths of time. Two units disagreeing at the same charge level is
// a divider/ADC difference, which is exactly what this trims out.
//
// The Mesh Deck's MAX17048 reports state of charge itself, which is what shows
// at zero trim; any other trim hands the percentage to the trimmed voltage the
// same way (see batteryReadPercent).

// Tenths of a percent with an explicit sign. Not "%+d.%d": integer division
// truncates toward zero, so a trim of -5 would print as "+0.5%".
static const char *battTrimText(int trim, char *buf, size_t bufLen) {
    const int mag = (trim < 0) ? -trim : trim;
    snprintf(buf, bufLen, "%s%d.%d%%", (trim < 0) ? "-" : "+", mag / 10, mag % 10);
    return buf;
}

// force: redraw now because the trim just moved. Otherwise this is the UI tick
// asking, and it is rate-limited — batteryReadVoltageUntrimmed() goes to the
// hardware every call, which on the Pager means driving the charger's one-shot
// ADC over I2C. At tick rate that would be ~30 conversions a second competing
// with the sampler that feeds the actual battery display.
static void refreshCfgBattCalReadout(bool force) {
    if (!s_cfgBattCalModal) return;

    static uint32_t lastMs = 0;
    const uint32_t now = millis();
    if (!force && (uint32_t)(now - lastMs) < 700) return;
    lastMs = now;

    if (lvObjValid(s_cfgBattCalValue)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f V   %u%%",
                 (double)batteryReadVoltage(), (unsigned)batteryReadPercent());
        lv_label_set_text(s_cfgBattCalValue, buf);
    }
    if (lvObjValid(s_cfgBattCalDetail)) {
        char trimBuf[12];
        char buf[48];
        snprintf(buf, sizeof(buf), TR("sensor %.2f V    trim %s"),
                 (double)batteryReadVoltageUntrimmed(),
                 battTrimText((int)s_cfg.battCalTrim, trimBuf, sizeof(trimBuf)));
        lv_label_set_text(s_cfgBattCalDetail, buf);
    }
}

static void setCfgBattCalPreview(int trim) {
    s_cfg.battCalTrim = cfgCoerceBattCalTrim(trim);
    batterySetCalibrationTrim((int)s_cfg.battCalTrim);
    refreshCfgBattCalReadout(true);
}

static void closeCfgBattCalModal() {
    if (lvObjValid(s_cfgBattCalBackdrop)) {
        lv_obj_del(s_cfgBattCalBackdrop);
    } else if (lvObjValid(s_cfgBattCalModal)) {
        lv_obj_del(s_cfgBattCalModal);
    }
    s_cfgBattCalBackdrop = nullptr;
    s_cfgBattCalModal = nullptr;
    s_cfgBattCalSlider = nullptr;
    s_cfgBattCalValue = nullptr;
    s_cfgBattCalDetail = nullptr;
}

// Teardown-safe like the brightness and volume previews: the settings screen can
// close with this modal open, and an unapplied trim must not linger in s_cfg
// where the next unrelated save would persist it.
static void revertCfgBattCalPreview() {
    if (!s_cfgBattCalModal && !s_cfgBattCalBackdrop) return;
    s_cfg.battCalTrim = s_cfgBattCalOriginal;
    batterySetCalibrationTrim((int)s_cfgBattCalOriginal);
    closeCfgBattCalModal();
}

static void cancelCfgBattCal() {
    revertCfgBattCalPreview();
    refreshCfgModal();
}

static void applyCfgBattCal() {
    persistConfigToPrefs();
    char trimBuf[12];
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Battery trim: %s"),
             battTrimText((int)s_cfg.battCalTrim, trimBuf, sizeof(trimBuf)));
    closeCfgBattCalModal();
    refreshCfgModal();
}

// Nudge by whole steps; used by the key handler on non-touch boards.
static void stepCfgBattCal(int steps) {
    if (!steps) return;
    const int16_t next = cfgCoerceBattCalTrim((int)s_cfg.battCalTrim + steps * BATT_CAL_TRIM_STEP);
    if (next == s_cfg.battCalTrim) return;
    setCfgBattCalPreview(next);
    if (lvObjValid(s_cfgBattCalSlider)) {
        lv_slider_set_value(s_cfgBattCalSlider, next, LV_ANIM_OFF);
    }
}

static void onCfgBattCalSliderChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target_obj(e);
    if (!slider) return;
    // Snap to the same step the keys use, so dragging and stepping cannot leave
    // the trim on a value the keys can never return to.
    const int raw = (int)lv_slider_get_value(slider);
    const int snapped = ((raw < 0 ? raw - BATT_CAL_TRIM_STEP / 2 : raw + BATT_CAL_TRIM_STEP / 2)
                         / BATT_CAL_TRIM_STEP) * BATT_CAL_TRIM_STEP;
    setCfgBattCalPreview(snapped);
}

static void onCfgBattCalBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgBattCalBackdrop) return;
    cancelCfgBattCal();
}

static void openCfgBattCalModal() {
    if (!s_rootScreen || s_cfgBattCalModal || s_cfgBattCalBackdrop) return;

    s_cfgBattCalOriginal = cfgCoerceBattCalTrim((int)s_cfg.battCalTrim);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgBattCalBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgBattCalBackdrop, w, h);
    lv_obj_align(s_cfgBattCalBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgBattCalBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgBattCalBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgBattCalBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgBattCalBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgBattCalBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgBattCalBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgBattCalBackdrop, onCfgBattCalBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgBattCalModal = lv_obj_create(s_cfgBattCalBackdrop);
    lv_obj_set_size(s_cfgBattCalModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgBattCalModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgBattCalModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_cfgBattCalModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_cfgBattCalModal, LV_DIR_VER);
    lv_obj_add_flag(s_cfgBattCalModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgBattCalModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgBattCalModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgBattCalModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgBattCalModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgBattCalModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgBattCalModal, 5, 0);
    lv_obj_set_flex_flow(s_cfgBattCalModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgBattCalModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgBattCalBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgBattCalModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Battery Calibration"));

    s_cfgBattCalValue = lv_label_create(s_cfgBattCalModal);
    lv_obj_set_width(s_cfgBattCalValue, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBattCalValue, &lv_font_montserrat_16, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_cfgBattCalValue, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_cfgBattCalValue, lv_color_hex(0xFFFFFF), 0);
#endif
    lv_obj_set_style_text_align(s_cfgBattCalValue, LV_TEXT_ALIGN_CENTER, 0);

    s_cfgBattCalDetail = lv_label_create(s_cfgBattCalModal);
    lv_obj_set_width(s_cfgBattCalDetail, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBattCalDetail, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgBattCalDetail, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgBattCalDetail, LV_TEXT_ALIGN_CENTER, 0);

    s_cfgBattCalSlider = lv_slider_create(s_cfgBattCalModal);
    lv_obj_set_width(s_cfgBattCalSlider, modalW - 40);
    lv_slider_set_range(s_cfgBattCalSlider, BATT_CAL_TRIM_MIN, BATT_CAL_TRIM_MAX);
    lv_slider_set_value(s_cfgBattCalSlider, s_cfgBattCalOriginal, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_cfgBattCalSlider, lv_color_hex(0x123266), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_cfgBattCalSlider, lvColorFrom565(s_ui.selectAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_cfgBattCalSlider, lv_color_hex(0xE8F1FF), LV_PART_KNOB);
    lv_obj_add_event_cb(s_cfgBattCalSlider, onCfgBattCalSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    // The instructions are the first thing to go on a short panel: the modal
    // would otherwise outgrow its height cap and start scrolling, hiding the
    // slider and the reading it exists to show. Two lines of prose are not worth
    // that on a 135 px display.
    if (h >= 200) {
        lv_obj_t *how = lv_label_create(s_cfgBattCalModal);
        lv_obj_set_width(how, lv_pct(100));
        lv_obj_set_style_text_font(how, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(how, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_text_align(how, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(how, LV_LABEL_LONG_WRAP);
        lv_label_set_text(how,
                          TR("Match the voltage to a meter reading.\n"
                             "Percent follows it. 0% = uncalibrated."));
    }

#if UI_TOUCH_ONLY_PROFILE
    appendHeltecCancelSaveRow(
        s_cfgBattCalModal,
        [](lv_event_t *e) { LV_UNUSED(e); cancelCfgBattCal(); },
        [](lv_event_t *e) { LV_UNUSED(e); applyCfgBattCal(); });
#else
    lv_obj_t *hint = lv_label_create(s_cfgBattCalModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, TR("j/k=Adjust  Enter=Save  Backspace=Cancel"));
#endif

    setCfgBattCalPreview(s_cfgBattCalOriginal);
}

// ── Chat-style picker ─────────────────────────────────────────────────────────
// A small in-CFG modal to pick Classic / Bubbles / Outline directly, replacing
// the old cycle toggle. The style is chosen per chat rebuild, so applying one is
// just a forced re-render.
static void closeChatStyleModal() {
    if (lvObjValid(s_chatStyleBackdrop)) {
        lv_obj_del(s_chatStyleBackdrop);
    } else if (lvObjValid(s_chatStyleModal)) {
        lv_obj_del(s_chatStyleModal);
    }
    s_chatStyleBackdrop = nullptr;
    s_chatStyleModal = nullptr;
    memset(s_chatStyleRows, 0, sizeof(s_chatStyleRows));
}

// ── Picker rows ──────────────────────────────────────────────────────────────
// The highlight every single-choice list in Config draws: the selected row
// filled and ringed, the rest a quiet slab. It was written out twelve times,
// palette and all, once per picker -- identical but for which array each copy
// walked -- so a change to the look was twelve edits and a missed one was a
// picker that no longer matched its neighbours.
//
// The e-paper build draws a black ring on a transparent row instead: the
// blues would threshold to a black slab under black text on a 1-bit panel.
static void paintPickerRow(lv_obj_t *row, bool sel) {
    if (!row) return;
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, sel ? 2 : 0, 0);
    lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
#else
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    lv_obj_set_style_bg_color(row, sel ? (isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F))
                                       : (isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266)), 0);
    lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : (isLight ? LV_OPA_90 : LV_OPA_40), 0);
    lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
    lv_obj_set_style_border_color(row, sel ? (isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF))
                                           : (isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C)), 0);
#endif
}

// How the selected row is brought into view. Recursive where the rows sit in a
// grid and the object that actually scrolls is the modal a level or two up;
// none where the list is short enough never to scroll.
enum PickerScroll : uint8_t {
    PICKER_SCROLL_NONE,
    PICKER_SCROLL_VIEW,
    PICKER_SCROLL_RECURSIVE,
};

static void paintPickerRows(lv_obj_t *const *rows, int count, int selection,
                            PickerScroll scroll) {
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = rows[i];
        if (!row) continue;
        const bool sel = (i == selection);
        paintPickerRow(row, sel);
        if (!sel) continue;
        if (scroll == PICKER_SCROLL_VIEW) {
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        } else if (scroll == PICKER_SCROLL_RECURSIVE) {
            lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);
        }
    }
}

static void refreshChatStyleSelection() {
    if (!s_chatStyleModal) return;
    paintPickerRows(s_chatStyleRows, CHAT_STYLE_MAX + 1, s_chatStyleSelection, PICKER_SCROLL_VIEW);
}

static void applyChatStyleSelection(int style) {
    if (!cfgChatStyleAllowed(style)) return;
    const bool changed = ((uint8_t)style != s_cfg.chatStyle);
    s_cfg.chatStyle = (uint8_t)style;
    if (changed) {
        persistConfigToPrefs();
        // Applies live: the classic/bubble branch is chosen per rebuild, and
        // refreshChatView() tears the list down with lv_obj_clean() before
        // repopulating, so nothing structural survives from the old style.
        // The DM view reads the same setting when it repopulates.
        s_lastRenderedChannel = -1;
        s_lastRenderedCount = -1;
        refreshChatView(true);
    }
#if defined(DEVICE_TDECK_PRO)
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Chat Type: %s%s"),
             chatStyleName((uint8_t)style), changed ? "" : TR(" (unchanged)"));
#else
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Chat Style: %s%s"),
             chatStyleName((uint8_t)style), changed ? "" : TR(" (unchanged)"));
#endif
    closeChatStyleModal();
    refreshCfgModal();
}

static void onChatStyleRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_chatStyleSelection = idx;
    applyChatStyleSelection(idx);
}

static void onChatStyleBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_chatStyleBackdrop) return;
    closeChatStyleModal();
    refreshCfgModal();
}

static void openChatStyleModal() {
    if (!s_rootScreen || s_chatStyleModal || s_chatStyleBackdrop) return;
    s_chatStyleSelection = cfgCoerceChatStyle(s_cfg.chatStyle);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 280) modalW = 280;

    s_chatStyleBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chatStyleBackdrop, w, h);
    lv_obj_align(s_chatStyleBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chatStyleBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chatStyleBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatStyleBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chatStyleBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_chatStyleBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chatStyleBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chatStyleBackdrop, onChatStyleBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chatStyleModal = lv_obj_create(s_chatStyleBackdrop);
    lv_obj_set_size(s_chatStyleModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_chatStyleModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_chatStyleModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_chatStyleModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_chatStyleModal, LV_DIR_VER);
    lv_obj_add_flag(s_chatStyleModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_chatStyleModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_chatStyleModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_chatStyleModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_chatStyleModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_chatStyleModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chatStyleModal, 1, 0);
    lv_obj_set_style_pad_all(s_chatStyleModal, 8, 0);
    lv_obj_set_style_pad_row(s_chatStyleModal, 6, 0);
    lv_obj_set_flex_flow(s_chatStyleModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatStyleModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_chatStyleBackdrop);

    lv_obj_t *title = lv_label_create(s_chatStyleModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_label_set_text(title, TR("Chat Type"));
#else
    lv_label_set_text(title, TR("Chat Style"));
#endif

    lv_obj_t *hint = lv_label_create(s_chatStyleModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(hint, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#endif
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if UI_TOUCH_ONLY_PROFILE
                      "Tap a style to apply"
#else
                      "Arrows=Move  Enter=Select  Backspace=Cancel"
#endif
    );

    static const char *kStyleDesc[CHAT_STYLE_MAX + 1] = {
#if defined(DEVICE_TDECK_PRO)
        "Flat black text lines",
        "",
        "Outlined messages",
        "Time, name and text, like the lock screen",
#else
        "Flat colored text lines",
        "Filled color bubbles",
        "Outlined color bubbles",
        "Time, name and text, like the lock screen",
#endif
    };
#if defined(DEVICE_TDECK_PRO)
    const lv_color_t rowTextColor = lv_color_make(0, 0, 0);
#else
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    for (int i = 0; i <= CHAT_STYLE_MAX; i++) {
        if (!cfgChatStyleAllowed(i)) continue;
        lv_obj_t *row = lv_btn_create(s_chatStyleModal);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_chatStyleRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onChatStyleRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_label_set_text(name, chatStyleName((uint8_t)i));

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowTextColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_text(desc, kStyleDesc[i]);
        }
    }

    refreshChatStyleSelection();
}

// ── Theme picker ──────────────────────────────────────────────────────────────
// Replaces blind theme cycling with a scrollable modal: every theme/mode preset
// gets a row showing its name and a three-swatch preview (background, panel,
// accent). Navigating highlights; Enter/tap saves the choice and reboots, the
// way the orientation row does — every screen is built from the palette, and
// buildUi() on the way up is the one path that reads all of it correctly.
static lv_obj_t *s_themeBackdrop = nullptr;
static lv_obj_t *s_themeModal = nullptr;
// Sized for the built-ins plus every custom slot, so the array does not have
// to be resized when a user adds one. uiThemeChoiceCount() is the live bound.
static lv_obj_t *s_themeRows[kUiThemePresetCount + UI_CUSTOM_THEME_SLOTS] = {};
static int s_themeSelection = 0;

// ── Theme filter ─────────────────────────────────────────────────────────────
// Twenty-nine themes is a long way to scroll to reach "Winter Chill". Typing
// narrows the list to matching names; the selection then indexes the *visible*
// rows, and s_themeVisible maps those back to choice indices.
//
// Armed with Space, exactly as the CFG and Nodes screens do it, and for the same
// reason: until the user asks to filter, letters have to stay available for
// navigation — j and k in particular, which are how this list is driven on a
// board with no trackball.
static lv_obj_t *s_themeList = nullptr;      // the scrollable row container
static lv_obj_t *s_themeFilterLabel = nullptr;
static bool s_themeFilterOpen = false;
static char s_themeFilter[16] = "";
static int s_themeVisible[kUiThemePresetCount + UI_CUSTOM_THEME_SLOTS] = {};
static int s_themeVisibleCount = 0;

static void closeThemeModal() {
    if (lvObjValid(s_themeBackdrop)) {
        lv_obj_del(s_themeBackdrop);
    } else if (lvObjValid(s_themeModal)) {
        lv_obj_del(s_themeModal);
    }
    s_themeBackdrop = nullptr;
    s_themeModal = nullptr;
    s_themeList = nullptr;
    s_themeFilterLabel = nullptr;
    s_themeFilterOpen = false;
    s_themeFilter[0] = '\0';
    s_themeVisibleCount = 0;
    memset(s_themeRows, 0, sizeof(s_themeRows));
}

// Case-insensitive substring, so "dark" finds every dark variant and "sun"
// finds "Sunset Ridge" without the user typing from the start of the name.
static bool themeNameMatchesFilter(const char *name) {
    if (!s_themeFilter[0]) return true;
    if (!name) return false;
    for (const char *start = name; *start; start++) {
        const char *a = start;
        const char *b = s_themeFilter;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

static void refreshThemeSelection() {
    if (!s_themeModal) return;
    paintPickerRows(s_themeRows, s_themeVisibleCount, s_themeSelection, PICKER_SCROLL_VIEW);
}

static void onThemeRowPressed(lv_event_t *e);

// Rebuilds the row list against the current filter, keeping the previously
// selected theme selected when it survives the filter. Called on open and after
// every keystroke that changes the filter.
static void rebuildThemeRows() {
    if (!s_themeList) return;

    // Remember the theme that was selected, not its row number: the row numbers
    // are about to change under it.
    int keepChoice = -1;
    if (s_themeSelection >= 0 && s_themeSelection < s_themeVisibleCount) {
        keepChoice = s_themeVisible[s_themeSelection];
    }

    lv_obj_clean(s_themeList);
    memset(s_themeRows, 0, sizeof(s_themeRows));
    s_themeVisibleCount = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
    const lv_color_t swatchBorder = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x8FA6CC) : lv_color_hex(0x0A1730);
    const int swatch = (w <= 160) ? 12 : 16;

    const int total = uiThemeChoiceCount();
    for (int i = 0; i < total; i++) {
        UiThemeChoice p;
        if (!uiThemeChoiceAt(i, p)) break;
        if (!themeNameMatchesFilter(p.name)) continue;

        const int vis = s_themeVisibleCount;
        s_themeVisible[vis] = i;
        s_themeVisibleCount++;

        lv_obj_t *row = lv_btn_create(s_themeList);
        s_themeRows[vis] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, onThemeRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)vis);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_label_set_text(name, p.name);

        // Three-swatch preview: background, panel, accent.
        const uint16_t swatches[3] = { p.bgMain, p.panelAlt, p.accent };
        for (int s = 0; s < 3; s++) {
            lv_obj_t *box = lv_obj_create(row);
            lv_obj_remove_style_all(box);
            lv_obj_set_size(box, swatch, swatch);
            lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(box, 3, 0);
            lv_obj_set_style_bg_color(box, tftColorToLv(swatches[s]), 0);
            lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(box, 1, 0);
            lv_obj_set_style_border_color(box, swatchBorder, 0);
        }
    }

    // Follow the kept theme to its new row; otherwise start at the top, which
    // is what someone narrowing a list expects to be looking at.
    s_themeSelection = 0;
    if (keepChoice >= 0) {
        for (int v = 0; v < s_themeVisibleCount; v++) {
            if (s_themeVisible[v] == keepChoice) { s_themeSelection = v; break; }
        }
    }

    if (s_themeFilterLabel) {
        if (s_themeFilterOpen) {
            // Brackets are the armed cue, present before anything is typed —
            // the same signal the CFG and Nodes headers give.
            if (s_themeVisibleCount == 0) {
                lv_label_set_text_fmt(s_themeFilterLabel, TR("[%s] no match"), s_themeFilter);
            } else {
                lv_label_set_text_fmt(s_themeFilterLabel, TR("[%s] %d"),
                                      s_themeFilter, s_themeVisibleCount);
            }
        } else {
            // Says "reboots" because it does: applying a theme restarts the
            // device rather than repainting the screen you are looking at.
            lv_label_set_text(s_themeFilterLabel,
#if UI_TOUCH_ONLY_PROFILE
                              "Tap a theme - reboots"
#else
                              "Space=Filter  Enter=Apply+reboot"
#endif
            );
        }
    }

    // Resolve geometry first: lv_obj_scroll_to_view() is a no-op before the tree
    // is laid out, which is why the initial selection previously landed off-screen.
    lv_obj_update_layout(s_themeModal);
    refreshThemeSelection();
}

static void applyThemeSelection(int idx);

// Applying a theme reboots, so it asks first. The unchanged case is answered
// by applyThemeSelection() itself, which already has a no-change branch.
static void cfgThemeAsk(int idx) {
    if (idx < 0 || idx >= s_themeVisibleCount) return;
    UiThemeChoice p;
    if (!uiThemeChoiceAt(s_themeVisible[idx], p)) return;
    if (p.theme == s_cfg.uiTheme && p.mode == s_cfg.uiMode) {
        applyThemeSelection(idx);   // says "(unchanged)" and closes; no reboot
        return;
    }
    snprintf(s_cfgConfirmText, sizeof(s_cfgConfirmText),
             TR("Switch to theme %s and reboot?"), p.name);
    openCfgConfirmModal(-1, s_cfgConfirmText,
                        [](int i) { applyThemeSelection(i); }, idx);
}

static void applyThemeSelection(int idx) {
    // idx addresses the visible (filtered) rows, not the full theme list.
    if (idx < 0 || idx >= s_themeVisibleCount) return;
    UiThemeChoice p;
    if (!uiThemeChoiceAt(s_themeVisible[idx], p)) return;
    if (p.theme == s_cfg.uiTheme && p.mode == s_cfg.uiMode) {
        // No change — just return to the CFG screen.
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Theme: %s (unchanged)"), p.name);
        closeThemeModal();
        refreshCfgModal();
        return;
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Theme: %s - rebooting..."), p.name);
    closeThemeModal();
    // Same sequence the orientation row uses: paint the status line, give it a
    // moment to actually reach the panel, then go.
    refreshCfgModal();
    lv_timer_handler();

    // After that repaint, deliberately. Half the UI reads s_cfg.uiMode directly
    // and the other half reads the palette s_ui, so a screen drawn between the
    // two being updated is drawn in neither theme. The new one arrives whole,
    // on the way back up.
    //
    // Straight to NVS, not the debounced flush persistUiTheme() queues: the
    // restart below would otherwise beat the write and the device would come
    // back wearing the theme it went down in.
    s_cfg.uiTheme = p.theme;
    s_cfg.uiMode = p.mode;
    persistConfigToPrefs();

    delay(900);
    flushPersistentState();   // transcripts too, before we go
    ESP.restart();
}

static void onThemeRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_themeSelection = idx;
    cfgThemeAsk(idx);
}

static void onThemeBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_themeBackdrop) return;
    closeThemeModal();
    refreshCfgModal();
}

static void openThemeModal() {
    if (!s_rootScreen || s_themeModal || s_themeBackdrop) return;
    // Every open starts unfiltered; the selection is resolved after the rows
    // exist, since it indexes them.
    s_themeFilterOpen = false;
    s_themeFilter[0] = '\0';
    s_themeVisibleCount = 0;
    s_themeSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 190) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_themeBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_themeBackdrop, w, h);
    lv_obj_align(s_themeBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_themeBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_themeBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_themeBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_themeBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_themeBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_themeBackdrop, 0, 0);
    lv_obj_add_event_cb(s_themeBackdrop, onThemeBackdropPressed, LV_EVENT_CLICKED, nullptr);

    // Fixed-size modal holding a fixed header (title + hint) and a scrollable
    // row list below. The modal itself does NOT scroll — earlier it was the
    // scroll container with center main-axis alignment, which centered the
    // 24-row overflow and pushed the top (and the initial selection) off-screen.
    s_themeModal = lv_obj_create(s_themeBackdrop);
    lv_obj_set_width(s_themeModal, modalW);
    lv_obj_set_height(s_themeModal, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_themeModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_themeModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_themeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_themeModal, LV_DIR_VER);
    lv_obj_set_style_bg_color(s_themeModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_themeModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_themeModal, 1, 0);
    lv_obj_set_style_border_color(s_themeModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_themeModal, 8, 0);
    lv_obj_set_style_pad_row(s_themeModal, 5, 0);
    lv_obj_set_flex_flow(s_themeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_themeModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_themeBackdrop);

    lv_obj_t *title = lv_label_create(s_themeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Theme"));

    // Doubles as the filter readout: it shows what has been typed and how many
    // themes still match, and the key legend when nothing has been.
    lv_obj_t *hint = lv_label_create(s_themeModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
    s_themeFilterLabel = hint;

    // Scrollable list holding the theme rows; the header above stays fixed.
    lv_obj_t *list = lv_obj_create(s_themeModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    // Cap the list so it scrolls internally, leaving room for the header.
    lv_obj_set_style_max_height(list, (h > 96) ? (h - 78) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 5, 0);
    lv_obj_set_style_pad_right(list, 2, 0);

    s_themeList = list;
    rebuildThemeRows();

    // Open on the theme in use. Done after the rows exist because the selection
    // indexes them, not the theme list.
    const int current = uiThemePresetIndexFromCfg();
    for (int v = 0; v < s_themeVisibleCount; v++) {
        if (s_themeVisible[v] == current) {
            s_themeSelection = v;
            break;
        }
    }
    refreshThemeSelection();
}

// ── Chat-name picker ──────────────────────────────────────────────────────────
// A small in-CFG modal to pick Short / Long sender names in chat. Unlike the
// chat-style picker this applies live (no reboot): bubble views re-render
// immediately and new classic-chat lines follow the choice.
static void refreshChatView(bool force);

static void closeChatNameModal() {
    if (lvObjValid(s_chatNameBackdrop)) {
        lv_obj_del(s_chatNameBackdrop);
    } else if (lvObjValid(s_chatNameModal)) {
        lv_obj_del(s_chatNameModal);
    }
    s_chatNameBackdrop = nullptr;
    s_chatNameModal = nullptr;
    memset(s_chatNameRows, 0, sizeof(s_chatNameRows));
}

static void refreshChatNameSelection() {
    if (!s_chatNameModal) return;
    paintPickerRows(s_chatNameRows, CHAT_NAME_MAX + 1, s_chatNameSelection, PICKER_SCROLL_VIEW);
}

static void applyChatNameSelection(int style) {
    if (style < 0 || style > CHAT_NAME_MAX) return;
    const bool changed = ((uint8_t)style != s_cfg.chatNameStyle);
    s_cfg.chatNameStyle = (uint8_t)style;
    if (changed) {
        persistConfigToPrefs();
        // Re-render chat live so bubble name tags update immediately.
        s_lastRenderedChannel = -1;
        refreshChatView(true);
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Chat Names: %s%s"),
             chatNameStyleName((uint8_t)style), changed ? "" : TR(" (unchanged)"));
    closeChatNameModal();
    refreshCfgModal();
}

static void onChatNameRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_chatNameSelection = idx;
    applyChatNameSelection(idx);
}

static void onChatNameBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_chatNameBackdrop) return;
    closeChatNameModal();
    refreshCfgModal();
}

static void openChatNameModal() {
    if (!s_rootScreen || s_chatNameModal || s_chatNameBackdrop) return;
    s_chatNameSelection = (s_cfg.chatNameStyle <= CHAT_NAME_MAX) ? s_cfg.chatNameStyle : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 280) modalW = 280;

    s_chatNameBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chatNameBackdrop, w, h);
    lv_obj_align(s_chatNameBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chatNameBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chatNameBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatNameBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chatNameBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_chatNameBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chatNameBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chatNameBackdrop, onChatNameBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chatNameModal = lv_obj_create(s_chatNameBackdrop);
    lv_obj_set_size(s_chatNameModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_chatNameModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_chatNameModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_chatNameModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_chatNameModal, LV_DIR_VER);
    lv_obj_add_flag(s_chatNameModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatNameModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatNameModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chatNameModal, 1, 0);
    lv_obj_set_style_border_color(s_chatNameModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chatNameModal, 8, 0);
    lv_obj_set_style_pad_row(s_chatNameModal, 6, 0);
    lv_obj_set_flex_flow(s_chatNameModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatNameModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_chatNameBackdrop);

    lv_obj_t *title = lv_label_create(s_chatNameModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Chat Names"));

    lv_obj_t *hint = lv_label_create(s_chatNameModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if UI_TOUCH_ONLY_PROFILE
                      "Tap a name style to apply"
#else
                      "Arrows=Move  Enter=Select  Backspace=Cancel"
#endif
    );

    static const char *kNameLabel[CHAT_NAME_MAX + 1] = { TR_NOOP("Short"), TR_NOOP("Long") };
    static const char *kNameDesc[CHAT_NAME_MAX + 1] = {
        TR_NOOP("4-char short name (ABCD)"),
        TR_NOOP("Full node name when known"),
    };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    for (int i = 0; i <= CHAT_NAME_MAX; i++) {
        lv_obj_t *row = lv_btn_create(s_chatNameModal);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_chatNameRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onChatNameRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_label_set_text(name, TR(kNameLabel[i]));

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowTextColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_text(desc, TR(kNameDesc[i]));
        }
    }

    refreshChatNameSelection();
}

// ── Font-size picker ─────────────────────────────────────────────────────────
// Picks the chat/DM message font size (Small/Medium/Large). Applies live like
// the chat-name picker — no reboot — by re-rendering the chat and DM views once
// the choice is committed.
static void closeFontSizeModal() {
    if (lvObjValid(s_fontSizeBackdrop)) {
        lv_obj_del(s_fontSizeBackdrop);
    } else if (lvObjValid(s_fontSizeModal)) {
        lv_obj_del(s_fontSizeModal);
    }
    s_fontSizeBackdrop = nullptr;
    s_fontSizeModal = nullptr;
    memset(s_fontSizeRows, 0, sizeof(s_fontSizeRows));
}

static void refreshFontSizeSelection() {
    if (!s_fontSizeModal) return;
    paintPickerRows(s_fontSizeRows, FONT_SIZE_MAX + 1, s_fontSizeSelection, PICKER_SCROLL_VIEW);
}

static void applyFontSizeSelection(int size) {
    if (size < 0 || size > FONT_SIZE_MAX) return;
    const bool changed = ((uint8_t)size != s_cfg.fontSize);
    s_cfg.fontSize = (uint8_t)size;
    s_appliedFontSize = s_cfg.fontSize;
    if (changed) {
        persistConfigToPrefs();
        // Re-render every view that draws with this face so the new size takes
        // effect immediately.
        s_lastRenderedChannel = -1;
        s_lastRenderedCount = -1;
        refreshChatView(true);
        refreshDmModal(true);
        refreshLiveView(true);
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Font Size: %s%s"),
             fontSizeName((uint8_t)size), changed ? "" : TR(" (unchanged)"));
    closeFontSizeModal();
    refreshCfgModal();
}

static void onFontSizeRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_fontSizeSelection = idx;
    applyFontSizeSelection(idx);
}

static void onFontSizeBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_fontSizeBackdrop) return;
    closeFontSizeModal();
    refreshCfgModal();
}

static void openFontSizeModal() {
    if (!s_rootScreen || s_fontSizeModal || s_fontSizeBackdrop) return;
    s_fontSizeSelection = (s_cfg.fontSize <= FONT_SIZE_MAX) ? s_cfg.fontSize : FONT_SIZE_MEDIUM;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    // Four short labels in a 2x2 grid, so this modal is deliberately narrower
    // than the list-style pickers — no description column to make room for.
    int modalW = w - 40;
    if (modalW < 150) modalW = w - 8;
    if (modalW > 210) modalW = 210;

    s_fontSizeBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_fontSizeBackdrop, w, h);
    lv_obj_align(s_fontSizeBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_fontSizeBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_fontSizeBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_fontSizeBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_fontSizeBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_fontSizeBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_fontSizeBackdrop, 0, 0);
    lv_obj_add_event_cb(s_fontSizeBackdrop, onFontSizeBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_fontSizeModal = lv_obj_create(s_fontSizeBackdrop);
    lv_obj_set_size(s_fontSizeModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_fontSizeModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_fontSizeModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_fontSizeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_fontSizeModal, LV_DIR_VER);
    lv_obj_add_flag(s_fontSizeModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_fontSizeModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_fontSizeModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_fontSizeModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_fontSizeModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_fontSizeModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_fontSizeModal, 1, 0);
    lv_obj_set_style_pad_all(s_fontSizeModal, 8, 0);
    lv_obj_set_style_pad_row(s_fontSizeModal, 6, 0);
    lv_obj_set_flex_flow(s_fontSizeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_fontSizeModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_fontSizeBackdrop);

    lv_obj_t *title = lv_label_create(s_fontSizeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Font Size"));

    lv_obj_t *hint = lv_label_create(s_fontSizeModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(hint, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#endif
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a size to apply"));
#else
    // Kept short so it fits the narrowed modal without wrapping to three lines.
    lv_label_set_text_fmt(hint, TR("Move  Enter=OK  %s=Cancel"), modalCloseKeyLabel());
#endif

    // "X-Large" rather than the full name fontSizeName() reports: the grid cells
    // are ~half the modal width and the spelled-out form wraps to two lines on
    // the narrowest board.
    static const char *kSizeLabel[FONT_SIZE_MAX + 1] = { TR_NOOP("Small"), TR_NOOP("Medium"), TR_NOOP("Large"), TR_NOOP("X-Large") };
#if defined(DEVICE_TDECK_PRO)
    const lv_color_t rowTextColor = lv_color_make(0, 0, 0);
#else
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    // Wrapping row container: four ~half-width buttons fall into two rows of
    // two. A stacked list of four described rows overflows the 135 px-tall
    // Cardputer panel, and the size names need no caption.
    lv_obj_t *grid = lv_obj_create(s_fontSizeModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);

    for (int i = 0; i <= FONT_SIZE_MAX; i++) {
        lv_obj_t *row = lv_btn_create(grid);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_fontSizeRows[i] = row;
        lv_obj_set_width(row, lv_pct(46));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onFontSizeRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_width(name, lv_pct(100));
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(name, TR(kSizeLabel[i]));
        lv_obj_center(name);
    }

    refreshFontSizeSelection();
}

// ── Signed Packets picker ────────────────────────────────────────────────────
// Picks the packet signature policy (src/sig_policy.h). Each option carries
// its one-line consequence, so a policy is chosen knowing what it drops rather
// than by cycling past it. Applies without a reboot: processMeshPacket() reads
// s_cfg.packetSigPolicy per packet.
static void closeSigPolicyModal() {
    if (lvObjValid(s_sigPolicyBackdrop)) {
        lv_obj_del(s_sigPolicyBackdrop);
    } else if (lvObjValid(s_sigPolicyModal)) {
        lv_obj_del(s_sigPolicyModal);
    }
    s_sigPolicyBackdrop = nullptr;
    s_sigPolicyModal = nullptr;
    memset(s_sigPolicyRows, 0, sizeof(s_sigPolicyRows));
}

static void refreshSigPolicySelection() {
    if (!s_sigPolicyModal) return;
    paintPickerRows(s_sigPolicyRows, SIG_POLICY_COUNT, s_sigPolicySelection, PICKER_SCROLL_VIEW);
}

static void applySigPolicySelection(int policy) {
    if (policy < 0 || policy >= SIG_POLICY_COUNT) return;
    const bool changed = ((uint8_t)policy != sigPolicyCoerce(s_cfg.packetSigPolicy));
    s_cfg.packetSigPolicy = (uint8_t)policy;
    meshSetSignPolicy(s_cfg.packetSigPolicy);
    if (changed) markConfigDirty();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR(kSigPolicyDesc[policy]));
    closeSigPolicyModal();
    refreshCfgModal();
}

static void onSigPolicyRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_sigPolicySelection = idx;
    applySigPolicySelection(idx);
}

static void onSigPolicyBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_sigPolicyBackdrop) return;
    closeSigPolicyModal();
    refreshCfgModal();
}

static void openSigPolicyModal() {
    if (!s_rootScreen || s_sigPolicyModal || s_sigPolicyBackdrop) return;
    s_sigPolicySelection = sigPolicyCoerce(s_cfg.packetSigPolicy);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 280) modalW = 280;

    s_sigPolicyBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_sigPolicyBackdrop, w, h);
    lv_obj_align(s_sigPolicyBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_sigPolicyBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_sigPolicyBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_sigPolicyBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_sigPolicyBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_sigPolicyBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_sigPolicyBackdrop, 0, 0);
    lv_obj_add_event_cb(s_sigPolicyBackdrop, onSigPolicyBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_sigPolicyModal = lv_obj_create(s_sigPolicyBackdrop);
    lv_obj_set_size(s_sigPolicyModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_sigPolicyModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_sigPolicyModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_sigPolicyModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_sigPolicyModal, LV_DIR_VER);
    lv_obj_add_flag(s_sigPolicyModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_sigPolicyModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_sigPolicyModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_sigPolicyModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_sigPolicyModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_sigPolicyModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sigPolicyModal, 1, 0);
    lv_obj_set_style_pad_all(s_sigPolicyModal, 8, 0);
    lv_obj_set_style_pad_row(s_sigPolicyModal, 6, 0);
    lv_obj_set_flex_flow(s_sigPolicyModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sigPolicyModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_sigPolicyBackdrop);

    lv_obj_t *title = lv_label_create(s_sigPolicyModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Signed Packets"));

    lv_obj_t *hint = lv_label_create(s_sigPolicyModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(hint, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#endif
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap one to apply."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=OK  %s=Cancel"), modalCloseKeyLabel());
#endif

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t rowTextColor = lv_color_make(0, 0, 0);
#else
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    for (int i = 0; i < SIG_POLICY_COUNT; i++) {
        lv_obj_t *row = lv_btn_create(s_sigPolicyModal);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_sigPolicyRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onSigPolicyRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_label_set_text(name, TR(kSigPolicyLabel[i]));

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_width(desc, lv_pct(100));
            lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowTextColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_text(desc, TR(kSigPolicyDesc[i]));
        }
    }

    refreshSigPolicySelection();
}

// ── Channel configuration ────────────────────────────────────────────────────
// Two nested modals. The slot picker lists all MESH_CHANNELS slots, showing the
// channel name where one is set and "Channel N" where the slot is still empty.
// Selecting a slot opens an editor for its name, encryption type and raw key.
//
// The editor works on a staged copy: nothing reaches CHANNEL_KEYS[] until Save,
// so backing out leaves the live channel plan — and therefore the radio — alone.
// This mirrors what the web config's Channels section can already do, for the
// (common) case of a device that never has a browser pointed at it.

// Encryption types we can write. The stored form is Meshtastic's: a 1-byte PSK
// index that the receiver expands (0 = plaintext, 1 = the well-known default
// key), or a literal 16/32-byte key.
enum ChanEncType : uint8_t {
    CHAN_ENC_NONE = 0,
    CHAN_ENC_DEFAULT,
    CHAN_ENC_AES128,
    CHAN_ENC_AES256,
    CHAN_ENC_TYPE_COUNT
};

// Channel-modal layout. Eight slots stacked one per row, each with its name and
// encryption on separate lines, came to roughly 420px of content — every panel
// had to scroll to pick a channel. Both modals now lay their buttons out in two
// columns of single-line rows, which puts all eight slots (and the whole editor)
// in view on every board.
//
// The picker rows carry the slot name alone. They used to append the encryption
// type, but that is the one field the list cannot act on, it is the editor's
// second row anyway, and dropping it is what buys the second column its width.
//
// Cells fill column-major (left column 0-3, right column 4-7) so stepping the
// selection by one still walks straight down a column — the Pager's wheel is the
// only way to move on that board, and a row-major fill would have left it
// zig-zagging across the grid.
#if UI_CARDPUTER_COMPACT
// 240x135: eight 19px rows in two columns, plus title and hint, come to 125 of
// the 129px the modal is allowed — the tightest fit of any board, so these
// numbers have very little slack in them.
static constexpr int  kChanModalCols  = 2;
#if defined(DEVICE_CARDPUTER_ZERO)
// 320x170, 164px allowed: four 25px rows a column at montserrat_12 under a
// montserrat_14 title come to ~160, the extra room going to legibility.
static constexpr int  kChanModalMaxW  = 300;
static constexpr int  kChanModalPad   = 5;
static constexpr int  kChanModalGap   = 4;
static constexpr int  kChanModalRowH  = 25;
static const lv_font_t *kChanModalTitleFont = &lv_font_montserrat_14;
static const lv_font_t *kChanModalRowFont   = &lv_font_montserrat_12;
#else
static constexpr int  kChanModalMaxW  = 236;
static constexpr int  kChanModalPad   = 3;
static constexpr int  kChanModalGap   = 3;
static constexpr int  kChanModalRowH  = 19;
static const lv_font_t *kChanModalTitleFont = &lv_font_montserrat_12;
static const lv_font_t *kChanModalRowFont   = &lv_font_montserrat_10;
#endif
// A half-width cell here is ~112px. "Encryption" plus its value does not fit,
// and an ellipsised field name is worse than a short one.
#define CHAN_EDIT_LABELS { TR_NOOP("Name"), TR_NOOP("Enc"), TR_NOOP("AEAD"), TR_NOOP("Save"), TR_NOOP("Loc"), TR_NOOP("Key"), TR_NOOP("Hops") }
#elif defined(DEVICE_TLORA_PAGER_TFT)
static constexpr int  kChanModalCols  = 2;
static constexpr int  kChanModalMaxW  = 448;
static constexpr int  kChanModalPad   = 8;
static constexpr int  kChanModalGap   = 5;
static constexpr int  kChanModalRowH  = 28;
static const lv_font_t *kChanModalTitleFont = &lv_font_montserrat_16;
static const lv_font_t *kChanModalRowFont   = &lv_font_montserrat_12;
#define CHAN_EDIT_LABELS { TR_NOOP("Name"), TR_NOOP("Encryption"), TR_NOOP("AEAD"), TR_NOOP("Save"), TR_NOOP("Location"), TR_NOOP("Key"), TR_NOOP("Hops") }
#else
// 320x240 boards (T-Deck, Heltec V4, Mesh Deck). At 300px wide a 49% cell is
// ~138px, which holds "0  LongFast" at montserrat_12 with room to spare.
static constexpr int  kChanModalCols  = 2;
static constexpr int  kChanModalMaxW  = 300;
static constexpr int  kChanModalPad   = 8;
static constexpr int  kChanModalGap   = 6;
static constexpr int  kChanModalRowH  = 28;
static const lv_font_t *kChanModalTitleFont = &lv_font_montserrat_16;
static const lv_font_t *kChanModalRowFont   = &lv_font_montserrat_12;
// ~138px cells at montserrat_12: "Location" plus "Off" fits, "Encryption" plus
// "AES-256" is the tightest pairing and ellipsises its value, not its name.
#define CHAN_EDIT_LABELS { TR_NOOP("Name"), TR_NOOP("Encryption"), TR_NOOP("AEAD"), TR_NOOP("Save"), TR_NOOP("Location"), TR_NOOP("Key"), TR_NOOP("Hops") }
#endif
static constexpr int kChanModalRowsPerCol =
    (MESH_CHANNELS + kChanModalCols - 1) / kChanModalCols;

// Width of one grid cell as a percentage of the row: 49% for two columns leaves
// the gutter that pad_column draws between them.
static constexpr int kChanModalCellPct = (kChanModalCols > 1) ? 49 : 100;

// ── Live Tools ───────────────────────────────────────────────────────────────
// The Live screen's chart surfaces used to cost one keyboard letter and one
// header button each, which does not scale past two. They sit behind a single
// (T)ools modal now, built on the channel grid metrics just above: same two
// columns, same column-major fill, so a step of one walks down a column here
// too. Laid out, that is:
//     SNR/RSSI  | Beacons
//     ChUtil    | MQTT
//     Discovery |
// (Cardputer has neither Discovery nor MQTT, leaving it SNR/RSSI and ChUtil in
// the left column with Beacons alone in the right.)
//
// Announce is last on the grid and is the one entry that is not a screen: it
// transmits instead of opening something. It sits at the end so the tools that
// do open a screen keep the positions people have learned, and because an
// action is the thing you least want to land on by overshooting.
enum LiveTool : uint8_t {
    LIVE_TOOL_LIVE = 0,
    LIVE_TOOL_SNR,
    LIVE_TOOL_CHUTIL,
#if FEATURE_DISCOVERY
    LIVE_TOOL_DISCOVERY,
#endif
    LIVE_TOOL_BEACONS,
#if HAS_WEATHER
    LIVE_TOOL_WEATHER,
#endif
#if FEATURE_MQTT_MONITOR
    LIVE_TOOL_MQTT,
#endif
    LIVE_TOOL_MECP,
    LIVE_TOOL_ANNOUNCE,
    LIVE_TOOL_COUNT
};
static constexpr int kLiveToolRowsPerCol =
    (LIVE_TOOL_COUNT + kChanModalCols - 1) / kChanModalCols;

// Keeps the letter keys in step with the "(S)NR/RSSI" style labels below.
static constexpr char kLiveToolShortcuts[LIVE_TOOL_COUNT] = {
    'L', 'S', 'U',
#if FEATURE_DISCOVERY
    'D',
#endif
    'B',
#if HAS_WEATHER
    'W',
#endif
#if FEATURE_MQTT_MONITOR
    'M',
#endif
    'E',
    'A',
};

// ── Live traffic filter ──────────────────────────────────────────────────────
// On a busy mesh the lines worth reading — text, DMs, errors — scroll away under
// position and telemetry chatter. The filter narrows the Live feed to one kind
// of traffic, backed by classifyLiveTraffic() — the same classifier that already
// colors the rows — so there is nothing new to parse and no way for the colors
// and the filter to disagree about what a line is.
//
// Grouped rather than one entry per LiveTrafficClass: the RX and TX halves of a
// kind belong together on screen, and sixteen rows would not fit the smallest
// panel this has to open on. liveFilterName()/liveFilterMatches() are defined
// down beside that classifier.
enum LiveFilter : uint8_t {
    LIVE_FILTER_ALL = 0,
    LIVE_FILTER_TEXT,
    LIVE_FILTER_DM,
    LIVE_FILTER_POS,
    LIVE_FILTER_TLM,
    LIVE_FILTER_NODE,
    LIVE_FILTER_ACK,
    LIVE_FILTER_ENC,
    LIVE_FILTER_ERROR,
    LIVE_FILTER_OTHER,
    LIVE_FILTER_COUNT
};

// Session-local on purpose: a filter is a way of looking at the feed right now,
// not a setting. Reopening Live starts on All, so a filter left on days ago can
// never masquerade as a quiet mesh. (Also why there is no new config field.)
static uint8_t s_liveFilter = LIVE_FILTER_ALL;
// The primary filter, layered under the kind filter above rather than being one
// of its rows: on, the feed drops every line whose packet came through a broker
// (via_mqtt set, whether downlinked here or relayed onto the air by a node that
// downlinked it), and the kind filter then narrows what is left. Lines this node
// wrote itself -- transmits, errors, notices -- never carry the flag and stay.
// Session-local for the same reason as s_liveFilter.
static bool s_liveNoMqtt = false;
// The picker's selection index for the No MQTT toggle above the kind grid.
static constexpr int kLiveFilterNoMqttSel = -1;
static lv_obj_t *s_liveFilterNoMqttRow = nullptr;

// The picker itself, built on the same grid metrics as Tools. It is a sibling of
// Tools rather than an entry in it: Tools opens other screens and closes itself,
// where this changes how the screen behind it renders and hands you straight
// back to it.
static lv_obj_t *s_liveFilterBackdrop = nullptr;
static lv_obj_t *s_liveFilterModal    = nullptr;
static lv_obj_t *s_liveFilterRows[LIVE_FILTER_COUNT] = {};
static lv_obj_t *s_liveFilterHeaderLabel = nullptr;   // the chip in the Live header
static int       s_liveFilterSelection = LIVE_FILTER_ALL;
static constexpr int kLiveFilterRowsPerCol =
    (LIVE_FILTER_COUNT + kChanModalCols - 1) / kChanModalCols;

static lv_obj_t *s_liveToolsModal    = nullptr;
static lv_obj_t *s_liveToolsRows[LIVE_TOOL_COUNT] = {};
static int       s_liveToolsSelection = LIVE_TOOL_LIVE;

// ── MeshBeacon offers (port 37) ──────────────────────────────────────────────
// Beacons from other meshes: a node over there retuned its radio to our config
// long enough to say "there is a mesh on this channel/preset/region". We keep
// the last few and show them on the Beacons tool; we never act on one.
//
// Not gated on a config flag. Meshtastic gates listening behind
// FLAG_LISTEN_ENABLED, but that is a knob for a module that also transmits —
// decoding a packet that already arrived costs no airtime and a few hundred
// bytes of RAM, and a beacon nobody sees is a beacon that may as well not have
// been sent.
//
// Eight senders, because the whole point of the Beacons screen is that these
// arrive minutes or hours apart and a short list quietly loses the one you were
// waiting for. Cardputer keeps the original four: an entry is ~160 bytes of
// .bss, and first-boot onboarding there has almost none to spare.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static constexpr int kBeaconOfferMax = 4;
#else
static constexpr int kBeaconOfferMax = 8;
#endif

struct BeaconOffer {
    uint32_t sender;
    uint32_t rxMs;              // millis() of the most recent one from this sender
    uint32_t firstMs;           // millis() of the first, for the repeat interval
    uint16_t hits;              // how many we have heard from this sender
    float    snr;
    float    rssi;
    uint8_t  hops;              // only meaningful when hasHops
    bool     hasHops;
    char     message[101];
    bool     hasChannel;
    char     channelName[16];
    bool     channelHasPsk;
    bool     channelUsesAead;   // an AEAD channel: joining it means turning AEAD on here too
    int      preset;            // our PRESET_* index, or -1 when not offered/unmappable
    char     region[12];        // "" when not offered
    bool     hasSlot;
    uint32_t slot;              // offer_slot, 1-based; only when hasSlot
    int8_t   chanIdx;           // channel it arrived on, for legacy-split pairing
    bool     textPaired;        // message came from a split TEXT_MESSAGE, not the beacon
};

static BeaconOffer s_beaconOffers[kBeaconOfferMax] = {};
static int         s_beaconOfferCount = 0;
// Bumped on every stored beacon, so the Beacons screen can tell "same list" from
// "same list, one of them just repeated" without hashing the whole table.
static uint32_t    s_beaconOfferSeq = 0;

// Meshtastic's FLAG_LEGACY_SPLIT sender option splits a beacon in two: a
// MESH_BEACON_APP carrying the offer with no text, then a plain TEXT_MESSAGE_APP
// carrying the text, both on the same channel. The flag lives in the *sender's*
// module config and never travels on air, so there is nothing in the packet to
// key off -- the split is only visible as an offer that arrived without words,
// followed by words that arrived without an offer.
//
// This reunites them: a text from a node whose most recent beacon came in
// text-less, on the same channel and within the window, is stored as that
// beacon's message. The chat row is left alone; the text was broadcast to the
// channel and stays a message like any other. Only the Beacons view gains
// anything, which is why a wrong guess here is cheap.
//
// Deliberately narrow, because this is a heuristic and not a protocol:
//   - only an offer that has no text of its own is ever filled in,
//   - only from the same sender on the same channel,
//   - only within kBeaconSplitPairMs of that beacon,
//   - and only the first such text; a second one does not overwrite it.
static constexpr uint32_t kBeaconSplitPairMs = 30000;

