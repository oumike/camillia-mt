// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Config modals: mesh beacon, community presets, language, node name.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static const char *meshBeaconHomeChannelName() {
    const ChannelKey &c0 = CHANNEL_KEYS[0];
    const char *name = c0.name_buf[0] ? c0.name_buf : c0.name;
    if (name && name[0]) return name;
    const uint8_t pi = s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : PRESET_LONG_FAST;
    return s_cfg.loraUsePreset ? kPresets[pi].channelName : CUSTOM_CHANNEL_NAME;
}

static void closeMeshBeaconCfgModal() {
    if (s_chanTextField == kChanTextBeaconMessage) closeChanTextModal();
    if (lvObjValid(s_mbCfgBackdrop)) {
        lv_obj_del(s_mbCfgBackdrop);
    } else if (lvObjValid(s_mbCfgModal)) {
        lv_obj_del(s_mbCfgModal);
    }
    s_mbCfgBackdrop = nullptr;
    s_mbCfgModal = nullptr;
    s_mbCfgWarn = nullptr;
    memset(s_mbCfgRows, 0, sizeof(s_mbCfgRows));
    memset(s_mbCfgValues, 0, sizeof(s_mbCfgValues));
}

static void refreshMeshBeaconCfgModal() {
    if (!s_mbCfgModal || !lvObjValid(s_mbCfgModal)) return;
    const uint8_t target = s_cfg.meshBeaconTargetPreset < PRESET_COUNT
                               ? s_cfg.meshBeaconTargetPreset : PRESET_LONG_FAST;
    char buf[72];
    for (int i = 0; i < MB_CFG_ROW_COUNT; i++) {
        if (!s_mbCfgValues[i]) continue;
        const char *v = "";
        switch (i) {
            case MB_CFG_LISTEN:    v = s_cfg.meshBeaconListen ? TR("On") : TR("Off"); break;
            case MB_CFG_BROADCAST:
                v = lic::active() ? TR("Off while licensed")
                                  : s_cfg.meshBeaconBroadcast ? TR("On") : TR("Off");
                break;
            case MB_CFG_TARGET:    v = kPresets[target].name; break;
            case MB_CFG_OFFER_KEY: v = s_cfg.meshBeaconOfferPsk ? TR("On") : TR("Off"); break;
            case MB_CFG_MESSAGE:
                v = s_cfg.meshBeaconMessage[0] ? s_cfg.meshBeaconMessage : TR("(none)");
                break;
            case MB_CFG_INTERVAL:
                snprintf(buf, sizeof(buf), TR("%u h"),
                         (unsigned)clampBeaconIntervalH(s_cfg.meshBeaconIntervalH));
                v = buf;
                break;
        }
        lv_label_set_text(s_mbCfgValues[i], v);
    }
    if (s_mbCfgWarn) {
        if (s_cfg.meshBeaconOfferPsk) {
            snprintf(buf, sizeof(buf), TR("Anyone on %s can join %s."),
                     kPresets[target].channelName, meshBeaconHomeChannelName());
            lv_label_set_text(s_mbCfgWarn, buf);
            lv_obj_clear_flag(s_mbCfgWarn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_mbCfgWarn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    paintPickerRows(s_mbCfgRows, MB_CFG_ROW_COUNT, s_mbCfgSelection, PICKER_SCROLL_VIEW);
}

// Saved, rescheduled, and redrawn here and on the Config row underneath.
static void meshBeaconCfgChanged() {
    markConfigDirty();
    meshBeaconSettingsChanged();
    refreshMeshBeaconCfgModal();
    refreshCfgModal();
}

static void meshBeaconCfgMessageCommitted(const char *msg) {
    if (!msg) msg = "";
    if (strcmp(msg, s_cfg.meshBeaconMessage) == 0) return;
    beaconCopyMessage(s_cfg.meshBeaconMessage, msg);
    meshBeaconCfgChanged();
}

static const char *mbCfgTargetLabelFor(int idx) {
    if (idx < 0 || idx >= s_mbCfgPresetCount) idx = 0;
    return kPresets[s_mbCfgPresetChoices[idx]].name;
}

static void mbCfgTargetApply(int idx) {
    if (idx < 0 || idx >= s_mbCfgPresetCount) return;
    const uint8_t preset = s_mbCfgPresetChoices[idx];
    if (preset == s_cfg.meshBeaconTargetPreset) return;
    s_cfg.meshBeaconTargetPreset = preset;
    meshBeaconCfgChanged();
}

static const char *mbCfgIntervalLabelFor(int idx) {
    static char buf[16];
    if (idx < 0 || idx >= kMbIntervalCount) idx = 0;
    snprintf(buf, sizeof(buf), TR("%u h"), (unsigned)kMbIntervalsH[idx]);
    return buf;
}

static void mbCfgIntervalApply(int idx) {
    if (idx < 0 || idx >= kMbIntervalCount) return;
    if (kMbIntervalsH[idx] == s_cfg.meshBeaconIntervalH) return;
    s_cfg.meshBeaconIntervalH = kMbIntervalsH[idx];
    meshBeaconCfgChanged();
}

static void meshBeaconCfgActivate(int row) {
    switch (row) {
        case MB_CFG_LISTEN:
            s_cfg.meshBeaconListen = !s_cfg.meshBeaconListen;
            // Turning it off drops what was already collected: leaving stale
            // offers on the Beacons screen after switching the feature off
            // would read as the switch not having worked.
            if (!s_cfg.meshBeaconListen) s_beaconOfferCount = 0;
            meshBeaconCfgChanged();
            break;
        case MB_CFG_BROADCAST:
            s_cfg.meshBeaconBroadcast = !s_cfg.meshBeaconBroadcast;
            meshBeaconCfgChanged();
            break;
        case MB_CFG_OFFER_KEY:
            s_cfg.meshBeaconOfferPsk = !s_cfg.meshBeaconOfferPsk;
            meshBeaconCfgChanged();
            break;
        case MB_CFG_TARGET: {
            // Only presets this radio can run, as on the home preset picker.
            s_mbCfgPresetCount = 0;
            int start = 0;
            for (uint8_t i = 0; i < PRESET_COUNT; i++) {
                if (!presetUsableOnThisRadio(i)) continue;
                if (i == s_cfg.meshBeaconTargetPreset) start = s_mbCfgPresetCount;
                s_mbCfgPresetChoices[s_mbCfgPresetCount++] = i;
            }
            if (s_mbCfgPresetCount == 0) break;
            static CfgSliderPicker spec = {
                TR_NOOP("Target preset"), 0, mbCfgTargetLabelFor, mbCfgTargetApply,
                nullptr, nullptr, nullptr, nullptr,
            };
            spec.count = s_mbCfgPresetCount;
            openCfgSliderModal(&spec, start);
        } break;
        case MB_CFG_MESSAGE:
            openChanTextModal(kChanTextBeaconMessage);
            break;
        case MB_CFG_INTERVAL: {
            const uint8_t cur = clampBeaconIntervalH(s_cfg.meshBeaconIntervalH);
            int start = 0;
            for (int i = 0; i < kMbIntervalCount; i++) {
                if (kMbIntervalsH[i] == cur) { start = i; break; }
            }
            static const CfgSliderPicker kSpec = {
                TR_NOOP("Interval"), kMbIntervalCount, mbCfgIntervalLabelFor, mbCfgIntervalApply,
                nullptr, nullptr, nullptr, nullptr,
            };
            openCfgSliderModal(&kSpec, start);
        } break;
        default:
            break;
    }
}

static void onMeshBeaconCfgRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= MB_CFG_ROW_COUNT) return;
    s_mbCfgSelection = idx;
    paintPickerRows(s_mbCfgRows, MB_CFG_ROW_COUNT, s_mbCfgSelection, PICKER_SCROLL_VIEW);
    meshBeaconCfgActivate(idx);
}

static void openMeshBeaconCfgModal() {
    if (!s_rootScreen) return;
    if (s_mbCfgModal || s_mbCfgBackdrop) return;
    if (s_mbCfgSelection < 0 || s_mbCfgSelection >= MB_CFG_ROW_COUNT) s_mbCfgSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_mbCfgBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_mbCfgBackdrop, w, h);
    lv_obj_align(s_mbCfgBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mbCfgBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mbCfgBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_mbCfgBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mbCfgBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_mbCfgBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_mbCfgBackdrop, 0, 0);
    lv_obj_add_event_cb(s_mbCfgBackdrop,
                        [](lv_event_t *e) {
                            if (lv_event_get_target_obj(e) != s_mbCfgBackdrop) return;
                            closeMeshBeaconCfgModal();
                        },
                        LV_EVENT_CLICKED, nullptr);

    s_mbCfgModal = lv_obj_create(s_mbCfgBackdrop);
    lv_obj_set_size(s_mbCfgModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_mbCfgModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_mbCfgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mbCfgModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mbCfgModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_mbCfgModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_mbCfgModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_mbCfgModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_mbCfgModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_mbCfgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mbCfgModal, 1, 0);
    lv_obj_set_style_pad_all(s_mbCfgModal, 8, 0);
    lv_obj_set_style_pad_row(s_mbCfgModal, 5, 0);
    lv_obj_set_flex_flow(s_mbCfgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mbCfgModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_mbCfgBackdrop);

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t titleColor = lv_color_make(0, 0, 0);
    const lv_color_t hintColor  = lv_color_make(0, 0, 0);
    const lv_color_t rowColor   = lv_color_make(0, 0, 0);
#else
    const lv_color_t titleColor = lv_color_hex(0xD9E8FF);
    const lv_color_t hintColor  = lv_color_hex(0xA7C7FF);
    const lv_color_t rowColor   = (s_cfg.uiMode == UI_MODE_LIGHT)
                                      ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    lv_obj_t *title = lv_label_create(s_mbCfgModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Mesh Beacons"));

#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_mbCfgModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeMeshBeaconCfgModal(); });
#endif

    lv_obj_t *list = lv_obj_create(s_mbCfgModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, (h > 150) ? (h - 104) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(list);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_style_pad_right(list, 2, 0);

    for (int i = 0; i < MB_CFG_ROW_COUNT; i++) {
        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_mbCfgRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, onMeshBeaconCfgRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        lv_label_set_text(name, TR(kMbCfgRowNames[i]));

        lv_obj_t *value = lv_label_create(row);
        lv_obj_set_flex_grow(value, 1);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(value, rowColor, 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        // The message can be 60 bytes; the row shows what fits.
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        s_mbCfgValues[i] = value;
    }

    // Under the rows, so it reads as being about the Include key row above it.
    s_mbCfgWarn = lv_label_create(s_mbCfgModal);
    lv_obj_set_width(s_mbCfgWarn, lv_pct(100));
    lv_obj_set_style_text_font(s_mbCfgWarn, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_mbCfgWarn, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_mbCfgWarn, lv_color_hex(0xFFC98A), 0);
#endif
    lv_obj_set_style_text_align(s_mbCfgWarn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_mbCfgWarn, LV_LABEL_LONG_WRAP);

    lv_obj_t *hint = lv_label_create(s_mbCfgModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a row to change it."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Edit  %s=Back"), modalCloseKeyLabel());
#endif

    refreshMeshBeaconCfgModal();
}

#if FEATURE_LICENSED_MODE
// ── Config -> Licensed (ham) mode ───────────────────────────────────────────
// Call sign, long-name suffix and TX power are staged here and reach the config
// only through enterLicensedMode(), behind a confirm; turning the mode on or off
// saves and reboots. While licensed those three rows are read-only (they are
// what the mode was entered with). Built like the Mesh Beacons popup above, and
// reusing its text and slider modals.
enum LicCfgRow : uint8_t {
    LIC_CFG_CALL = 0,
    LIC_CFG_SUFFIX,
    LIC_CFG_POWER,
    LIC_CFG_TOGGLE,
    LIC_CFG_RESUME,     // only while licTxHold
    LIC_CFG_ROW_MAX
};

static const char *const kLicCfgRowNames[LIC_CFG_ROW_MAX] = {
    TR_NOOP("Call sign"),
    TR_NOOP("Long name suffix"),
    TR_NOOP("TX power"),
    TR_NOOP("Enable"),              // "Disable" while licensed
    TR_NOOP("Resume transmitting"),
};

static lv_obj_t *s_licCfgBackdrop = nullptr;
static lv_obj_t *s_licCfgModal = nullptr;
static lv_obj_t *s_licCfgRows[LIC_CFG_ROW_MAX] = {};
static lv_obj_t *s_licCfgValues[LIC_CFG_ROW_MAX] = {};
static lv_obj_t *s_licCfgWarn = nullptr;
static int       s_licCfgSelection = 0;
static int       s_licCfgRowCount = 0;
static uint8_t   s_licCfgPower = MESH_POWER;
static char      s_licCfgConfirm[400] = {};

static void closeLicCfgModal() {
    if (s_chanTextField == kChanTextLicCall || s_chanTextField == kChanTextLicSuffix) {
        closeChanTextModal();
    }
    if (lvObjValid(s_licCfgBackdrop)) {
        lv_obj_del(s_licCfgBackdrop);
    } else if (lvObjValid(s_licCfgModal)) {
        lv_obj_del(s_licCfgModal);
    }
    s_licCfgBackdrop = nullptr;
    s_licCfgModal = nullptr;
    s_licCfgWarn = nullptr;
    memset(s_licCfgRows, 0, sizeof(s_licCfgRows));
    memset(s_licCfgValues, 0, sizeof(s_licCfgValues));
}

static void licCfgSetWarn(const char *msg) {
    if (!lvObjValid(s_licCfgWarn)) return;
    if (msg && msg[0]) {
        lv_label_set_text(s_licCfgWarn, msg);
        lv_obj_clear_flag(s_licCfgWarn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_licCfgWarn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refreshLicCfgModal() {
    if (!s_licCfgModal || !lvObjValid(s_licCfgModal)) return;
    for (int i = 0; i < s_licCfgRowCount; i++) {
        if (!s_licCfgValues[i]) continue;
        char buf[32];
        const char *v = "";
        switch (i) {
            case LIC_CFG_CALL:   v = s_licCfgCall[0] ? s_licCfgCall : TR("(none)"); break;
            case LIC_CFG_SUFFIX: v = s_licCfgSuffix[0] ? s_licCfgSuffix : TR("(none)"); break;
            case LIC_CFG_POWER:
                snprintf(buf, sizeof(buf), TR("%u dBm"), (unsigned)s_licCfgPower);
                v = buf;
                break;
            default: break;
        }
        lv_label_set_text(s_licCfgValues[i], v);
    }
    paintPickerRows(s_licCfgRows, s_licCfgRowCount, s_licCfgSelection, PICKER_SCROLL_VIEW);
}

static void licCfgTextCommitted(int field, const char *text) {
    if (field == kChanTextLicCall) {
        snprintf(s_licCfgCall, sizeof(s_licCfgCall), "%s", text);
    } else if (field == kChanTextLicSuffix) {
        utf8util::copyTruncate(s_licCfgSuffix, sizeof(s_licCfgSuffix), text);
    }
    licCfgSetWarn(nullptr);
    refreshLicCfgModal();
}

static const char *licCfgPowerLabelFor(int idx) {
    static char buf[16];
    snprintf(buf, sizeof(buf), TR("%u dBm"), (unsigned)(idx + 1));
    return buf;
}

static void licCfgPowerApply(int idx) {
    if (idx < 0 || idx >= MESH_POWER) return;
    s_licCfgPower = (uint8_t)(idx + 1);
    refreshLicCfgModal();
}

// Yes on either confirm. The status is painted before the call: on success
// neither returns, and a device that reboots unexplained reads as a crash.
static void licCfgEnableCommit(int) {
    licCfgSetWarn(TR("Rebooting..."));
    lv_timer_handler();
    char err[96];
    if (!enterLicensedMode(s_licCfgCall, s_licCfgSuffix, s_licCfgPower, err, sizeof(err))) {
        licCfgSetWarn(err[0] ? err : TR("Invalid call sign"));
    }
}

static void licCfgDisableCommit(int) {
    licCfgSetWarn(TR("Rebooting..."));
    lv_timer_handler();
    exitLicensedMode();
}

static void licCfgActivate(int row) {
    const bool licensed = lic::active();
    switch (row) {
        case LIC_CFG_CALL:
        case LIC_CFG_SUFFIX:
        case LIC_CFG_POWER:
            if (licensed) {
                licCfgSetWarn(TR("Turn licensed mode off to change this."));
                break;
            }
            if (row == LIC_CFG_CALL) {
                openChanTextModal(kChanTextLicCall);
            } else if (row == LIC_CFG_SUFFIX) {
                openChanTextModal(kChanTextLicSuffix);
            } else {
                static const CfgSliderPicker kSpec = {
                    TR_NOOP("TX power"), MESH_POWER, licCfgPowerLabelFor, licCfgPowerApply,
                    nullptr, nullptr, nullptr, nullptr,
                };
                openCfgSliderModal(&kSpec, (int)s_licCfgPower - 1);
            }
            break;
        case LIC_CFG_TOGGLE:
            if (!licensed) {
                char norm[lic::kCallSignMax + 1];
                if (!s_licCfgCall[0]) {
                    licCfgSetWarn(TR("Enter a call sign"));
                    break;
                }
                if (!lic::normalizeCallSign(s_licCfgCall, norm, sizeof(norm))) {
                    licCfgSetWarn(TR("Invalid call sign"));
                    break;
                }
                licCfgSetWarn(nullptr);
                snprintf(s_licCfgConfirm, sizeof(s_licCfgConfirm), "%s",
                         TR("Every channel's key will be removed. Encrypted DMs, remote admin "
                            "and relaying for unlicensed nodes will stop. The device will "
                            "reboot. You must hold an amateur licence for the band you use. "
                            "Camillia does not verify your licence."));
                openCfgConfirmModal(-1, s_licCfgConfirm, licCfgEnableCommit, 0);
            } else {
                const RegionPlan *r = regionPlan(s_cfg.region);
                size_t n = (size_t)snprintf(s_licCfgConfirm, sizeof(s_licCfgConfirm), "%s",
                                            TR("Channel keys will not come back. The device will reboot."));
                if (r && r->licensedOnly && n < sizeof(s_licCfgConfirm)) {
                    snprintf(s_licCfgConfirm + n, sizeof(s_licCfgConfirm) - n,
                             TR(" Region will change to %s and transmit stays off until you resume."),
                             MY_REGION);
                }
                openCfgConfirmModal(-1, s_licCfgConfirm, licCfgDisableCommit, 0);
            }
            break;
        case LIC_CFG_RESUME:
            resumeTxAfterLicensedExit();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Transmit resumed"));
            refreshCfgModal();
            // The row goes with the hold it was for. Hidden, not rebuilt: this
            // can be running inside that row's own click event.
            if (lvObjValid(s_licCfgRows[LIC_CFG_RESUME])) {
                lv_obj_add_flag(s_licCfgRows[LIC_CFG_RESUME], LV_OBJ_FLAG_HIDDEN);
            }
            s_licCfgRowCount = LIC_CFG_RESUME;
            if (s_licCfgSelection >= s_licCfgRowCount) s_licCfgSelection = s_licCfgRowCount - 1;
            licCfgSetWarn(TR("Transmit resumed"));
            refreshLicCfgModal();
            break;
        default:
            break;
    }
}

static void onLicCfgRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_licCfgRowCount) return;
    s_licCfgSelection = idx;
    paintPickerRows(s_licCfgRows, s_licCfgRowCount, s_licCfgSelection, PICKER_SCROLL_VIEW);
    licCfgActivate(idx);
}

static void openLicCfgModal() {
    if (!s_rootScreen) return;
    if (s_licCfgModal || s_licCfgBackdrop) return;

    // Staged from the config: what the mode is (or was last) entered with.
    snprintf(s_licCfgCall, sizeof(s_licCfgCall), "%s", s_cfg.callSign);
    s_licCfgSuffix[0] = '\0';
    const size_t callLen = strlen(s_cfg.callSign);
    if (callLen && strncmp(s_cfg.nodeLong, s_cfg.callSign, callLen) == 0
        && s_cfg.nodeLong[callLen] == '/' && s_cfg.nodeLong[callLen + 1] == '/') {
        utf8util::copyTruncate(s_licCfgSuffix, sizeof(s_licCfgSuffix),
                               s_cfg.nodeLong + callLen + 2);
    }
    s_licCfgPower = (uint8_t)constrain((int)s_cfg.loraPower, 1, (int)MESH_POWER);

    s_licCfgRowCount = s_cfg.licTxHold ? LIC_CFG_ROW_MAX : LIC_CFG_RESUME;
    if (s_licCfgSelection < 0 || s_licCfgSelection >= s_licCfgRowCount) s_licCfgSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_licCfgBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_licCfgBackdrop, w, h);
    lv_obj_align(s_licCfgBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_licCfgBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_licCfgBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_licCfgBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_licCfgBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_licCfgBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_licCfgBackdrop, 0, 0);
    lv_obj_add_event_cb(s_licCfgBackdrop,
                        [](lv_event_t *e) {
                            if (lv_event_get_target_obj(e) != s_licCfgBackdrop) return;
                            closeLicCfgModal();
                        },
                        LV_EVENT_CLICKED, nullptr);

    s_licCfgModal = lv_obj_create(s_licCfgBackdrop);
    lv_obj_set_size(s_licCfgModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_licCfgModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_licCfgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_licCfgModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_licCfgModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_licCfgModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_licCfgModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_licCfgModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_licCfgModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_licCfgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_licCfgModal, 1, 0);
    lv_obj_set_style_pad_all(s_licCfgModal, 8, 0);
    lv_obj_set_style_pad_row(s_licCfgModal, 5, 0);
    lv_obj_set_flex_flow(s_licCfgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_licCfgModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_licCfgBackdrop);

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t titleColor = lv_color_make(0, 0, 0);
    const lv_color_t hintColor  = lv_color_make(0, 0, 0);
    const lv_color_t rowColor   = lv_color_make(0, 0, 0);
#else
    const lv_color_t titleColor = lv_color_hex(0xD9E8FF);
    const lv_color_t hintColor  = lv_color_hex(0xA7C7FF);
    const lv_color_t rowColor   = (s_cfg.uiMode == UI_MODE_LIGHT)
                                      ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    lv_obj_t *title = lv_label_create(s_licCfgModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Licensed (ham) mode"));

#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_licCfgModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeLicCfgModal(); });
#endif

    lv_obj_t *list = lv_obj_create(s_licCfgModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, (h > 150) ? (h - 104) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(list);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_style_pad_right(list, 2, 0);

    const bool licensed = lic::active();
    for (int i = 0; i < s_licCfgRowCount; i++) {
        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_licCfgRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, onLicCfgRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        // Read-only while licensed: shown, but dimmed.
        if (licensed && i <= LIC_CFG_POWER) lv_obj_set_style_opa(row, LV_OPA_60, 0);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        lv_label_set_text(name, (i == LIC_CFG_TOGGLE && licensed) ? TR("Disable")
                                                                  : TR(kLicCfgRowNames[i]));

        lv_obj_t *value = lv_label_create(row);
        lv_obj_set_flex_grow(value, 1);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(value, rowColor, 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        s_licCfgValues[i] = value;
    }

    s_licCfgWarn = lv_label_create(s_licCfgModal);
    lv_obj_set_width(s_licCfgWarn, lv_pct(100));
    lv_obj_set_style_text_font(s_licCfgWarn, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_licCfgWarn, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_licCfgWarn, lv_color_hex(0xFFC98A), 0);
#endif
    lv_obj_set_style_text_align(s_licCfgWarn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_licCfgWarn, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_licCfgWarn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *hint = lv_label_create(s_licCfgModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a row to change it."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Edit  %s=Back"), modalCloseKeyLabel());
#endif

    refreshLicCfgModal();
}
#endif  // FEATURE_LICENSED_MODE

// ── Config -> Community Preset ──────────────────────────────────────────────
// The web flasher's community setups (community_presets.h): pick one, read
// what it changes, and confirm. Applying writes region, radio and the whole
// channel plan through the cfg CLI (communityPresetApply()) and reboots.
//
// A sibling of the Modem preset picker above, for the same reason the
// orientation picker is: the rows say different things and the list is built
// differently (only the setups this device can take are offered).
static lv_obj_t *s_cfgCommunityBackdrop = nullptr;
static lv_obj_t *s_cfgCommunityModal = nullptr;
static lv_obj_t **s_cfgCommunityRows = nullptr;    // s_cfgCommunityCount entries
static int      *s_cfgCommunityChoices = nullptr;  // row -> kCommunityPresets index
static int       s_cfgCommunityCount = 0;
static int       s_cfgCommunitySelection = 0;
// The confirmation names everything that changes plus the community's own
// notes, which run to a few hundred bytes.
static char      s_cfgCommunityConfirm[768] = {};

static void closeCfgCommunityModal() {
    if (lvObjValid(s_cfgCommunityBackdrop)) lv_obj_del(s_cfgCommunityBackdrop);
    s_cfgCommunityBackdrop = nullptr;
    s_cfgCommunityModal = nullptr;
    free(s_cfgCommunityRows);
    free(s_cfgCommunityChoices);
    s_cfgCommunityRows = nullptr;
    s_cfgCommunityChoices = nullptr;
    s_cfgCommunityCount = 0;
    s_cfgCommunitySelection = 0;
}

static void refreshCfgCommunitySelection() {
    if (!s_cfgCommunityModal || !s_cfgCommunityRows) return;
    paintPickerRows(s_cfgCommunityRows, s_cfgCommunityCount, s_cfgCommunitySelection,
                    PICKER_SCROLL_VIEW);
}

// Yes on the confirmation. `presetIdx` is the kCommunityPresets index, not
// the row: the picker is closed by now and its table with it.
static void cfgCommunityCommit(int presetIdx) {
    closeCfgCommunityModal();
    // Licensed: the preset's channels go through cfgApplyStagedChannels(),
    // which takes them without their keys; said here, as nothing after the
    // reboot would.
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("%s - rebooting..."),
             lic::active() ? TR("Licensed mode: channel imported without its key")
                           : kCommunityPresets[presetIdx].label);
    refreshCfgModal();
    // Paint the status before going, as the Modem preset picker does: a
    // device that reboots with no explanation reads as a crash.
    lv_timer_handler();
    const char *err = communityPresetApply(presetIdx);   // reboots on success
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Not applied: %s"),
             err ? err : TR("nothing changed"));
    refreshCfgModal();
}

static void cfgCommunityAsk(int row) {
    if (row < 0 || row >= s_cfgCommunityCount) return;
    const int presetIdx = s_cfgCommunityChoices[row];
    communityPresetSummary(presetIdx, s_cfgCommunityConfirm, sizeof(s_cfgCommunityConfirm));
    openCfgConfirmModal(-1, s_cfgCommunityConfirm,
                        [](int i) { cfgCommunityCommit(i); }, presetIdx);
}

static void onCfgCommunityRowPressed(lv_event_t *e) {
    const int row = (int)(intptr_t)lv_event_get_user_data(e);
    if (row < 0 || row >= s_cfgCommunityCount) return;
    s_cfgCommunitySelection = row;
    refreshCfgCommunitySelection();
    cfgCommunityAsk(row);
}

static void openCfgCommunityModal() {
    if (!s_rootScreen || s_cfgCommunityBackdrop) return;
    {
        // Forty-odd rows of two labels each: bail before lv_obj_create would
        // hit LV_ASSERT_MALLOC, as the Nodes screen does.
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 12288) {
            logLvglMemDiag("openCfgCommunityModal aborted (low LVGL mem)");
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Not enough memory to list presets."));
            refreshCfgModal();
            return;
        }
    }
    s_cfgCommunityRows = (lv_obj_t **)calloc(kCommunityPresetCount, sizeof(lv_obj_t *));
    s_cfgCommunityChoices = (int *)calloc(kCommunityPresetCount, sizeof(int));
    if (!s_cfgCommunityRows || !s_cfgCommunityChoices) {
        closeCfgCommunityModal();
        return;
    }
    s_cfgCommunityCount = 0;
    for (int i = 0; i < kCommunityPresetCount; i++) {
        if (communityPresetProblem(i)) continue;   // e.g. ten channels on the Cardputer
        s_cfgCommunityChoices[s_cfgCommunityCount++] = i;
    }
    s_cfgCommunitySelection = 0;
    if (s_cfgCommunityCount == 0) {
        closeCfgCommunityModal();
        return;
    }

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_cfgCommunityBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgCommunityBackdrop, w, h);
    lv_obj_align(s_cfgCommunityBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgCommunityBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgCommunityBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgCommunityBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgCommunityBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgCommunityBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgCommunityBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgCommunityBackdrop,
                        [](lv_event_t *e) {
                            if (lv_event_get_target_obj(e) != s_cfgCommunityBackdrop) return;
                            closeCfgCommunityModal();
                        },
                        LV_EVENT_CLICKED, nullptr);

    s_cfgCommunityModal = lv_obj_create(s_cfgCommunityBackdrop);
    lv_obj_set_size(s_cfgCommunityModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgCommunityModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgCommunityModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgCommunityModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgCommunityModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_cfgCommunityModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_cfgCommunityModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_cfgCommunityModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_cfgCommunityModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_cfgCommunityModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgCommunityModal, 1, 0);
    lv_obj_set_style_pad_all(s_cfgCommunityModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgCommunityModal, 5, 0);
    lv_obj_set_flex_flow(s_cfgCommunityModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgCommunityModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgCommunityBackdrop);

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t titleColor = lv_color_make(0, 0, 0);
    const lv_color_t hintColor  = lv_color_make(0, 0, 0);
    const lv_color_t rowColor   = lv_color_make(0, 0, 0);
#else
    const lv_color_t titleColor = lv_color_hex(0xD9E8FF);
    const lv_color_t hintColor  = lv_color_hex(0xA7C7FF);
    const lv_color_t rowColor   = (s_cfg.uiMode == UI_MODE_LIGHT)
                                      ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    lv_obj_t *title = lv_label_create(s_cfgCommunityModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Community preset"));

#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_cfgCommunityModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeCfgCommunityModal(); });
#endif

    lv_obj_t *warn = lv_label_create(s_cfgCommunityModal);
    lv_obj_set_width(warn, lv_pct(100));
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(warn, hintColor, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_label_set_text(warn, TR("Sets region, radio, channels and MQTT as a local "
                            "mesh publishes them, and clears messages and nodes. "
                            "Asks first, then reboots."));

    lv_obj_t *list = lv_obj_create(s_cfgCommunityModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, (h > 150) ? (h - 104) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(list);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_right(list, 2, 0);

    for (int i = 0; i < s_cfgCommunityCount; i++) {
        const CommunityPreset &cp = kCommunityPresets[s_cfgCommunityChoices[i]];

        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_cfgCommunityRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onCfgCommunityRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_width(name, lv_pct(100));
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_label_set_text(name, cp.label);

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_width(desc, lv_pct(100));
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_long_mode(desc, LV_LABEL_LONG_DOT);
            const int radio = communityPresetRadioIndex(cp);
            lv_label_set_text_fmt(desc, TR("%s  %s  %u ch%s"), cp.region,
                                  radio >= 0 ? kPresets[radio].channelName : TR("own preset"),
                                  (unsigned)cp.channelCount, cp.mqtt.host[0] ? TR("  MQTT") : "");
        }
    }

    lv_obj_t *hint = lv_label_create(s_cfgCommunityModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a preset to review it."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Review  %s=Cancel"), modalCloseKeyLabel());
#endif

    refreshCfgCommunitySelection();
}

#if HAS_RUNTIME_ORIENTATION
// ── Config -> Orientation ────────────────────────────────────────────────────
// Three ways up, shown at once. This replaced a row that cycled through them:
// cycling is fine for two states, but with three it makes reaching the last one
// cost two reboots, and it can only ever say where you are rather than where
// you could go. A picker says both, and costs one reboot from anywhere.
//
// It also retires the confirm dialog that row needed. That dialog existed to
// answer "what will pressing this do", which a list of named destinations
// answers by existing -- and a confirm on top of a deliberate choice is the
// kind of second question people learn to dismiss without reading.
//
// Deliberately a sibling of the preset picker above rather than a shared
// widget. They look alike today, but this one has a fixed three-entry list that
// never filters and needs no scrolling, and the resemblance is not worth
// coupling two modals whose contents have nothing to do with each other.
static lv_obj_t *s_cfgOrientBackdrop = nullptr;
static lv_obj_t *s_cfgOrientModal = nullptr;
static lv_obj_t *s_cfgOrientRows[UI_ORIENT_COUNT] = {};
static int       s_cfgOrientSelection = 0;

static void closeCfgOrientModal() {
    if (lvObjValid(s_cfgOrientBackdrop)) {
        lv_obj_del(s_cfgOrientBackdrop);
    } else if (lvObjValid(s_cfgOrientModal)) {
        lv_obj_del(s_cfgOrientModal);
    }
    s_cfgOrientBackdrop = nullptr;
    s_cfgOrientModal = nullptr;
    memset(s_cfgOrientRows, 0, sizeof(s_cfgOrientRows));
    s_cfgOrientSelection = 0;
}

static void refreshCfgOrientSelection() {
    if (!s_cfgOrientModal) return;
    paintPickerRows(s_cfgOrientRows, (int)UI_ORIENT_COUNT, s_cfgOrientSelection, PICKER_SCROLL_NONE);
}

// Saves `want` as the orientation and restarts into it. Does not return.
// Straight to NVS, not the debounced flush: the reboot would otherwise beat
// the write and the device would come back the way it went down.
// persistConfigToPrefs() writes the standalone uiOrient key that
// loadBootOrientation() reads on the way up.
static void orientSaveAndRestart(uint8_t want) {
    s_cfg.uiOrientation = want;
    persistConfigToPrefs();
    flushPersistentState();   // transcripts too, before we go
    ESP.restart();
}

// Applies the orientation and restarts. Does not return unless it was already
// the one in force.
static void cfgOrientCommit(int idx) {
    if (idx < 0 || idx >= (int)UI_ORIENT_COUNT) return;
    const uint8_t want = (uint8_t)idx;

    // Nothing to write and nothing worth a reboot for. Rebooting to arrive
    // where we already are would read as the picker having misunderstood.
    if (s_cfg.uiOrientation == want) {
        closeCfgOrientModal();
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Already %s."), uiOrientName(want));
        refreshCfgModal();
        return;
    }

    closeCfgOrientModal();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("%s - rebooting..."), uiOrientName(want));
    refreshCfgModal();
    lv_timer_handler();
    delay(900);
    orientSaveAndRestart(want);
}

// See cfgPresetAsk(): names where Yes goes, and leaves the picker underneath.
static void cfgOrientAsk(int idx) {
    if (idx < 0 || idx >= (int)UI_ORIENT_COUNT) return;
    if (s_cfg.uiOrientation == (uint8_t)idx) {   // nothing to confirm
        cfgOrientCommit(idx);
        return;
    }
    snprintf(s_cfgConfirmText, sizeof(s_cfgConfirmText),
             TR("Switch to %s and reboot?"), uiOrientName((uint8_t)idx));
    openCfgConfirmModal(-1, s_cfgConfirmText,
                        [](int i) { cfgOrientCommit(i); }, idx);
}

#if defined(DEVICE_TDISPLAY_P4)
// ── Keyboard expansion -> landscape ──────────────────────────────────────────
// Clipping the keyboard on in portrait counts down ten seconds and reboots
// into landscape; Reboot now (or Enter) skips the wait, Cancel (or Esc) stays
// in portrait. Taking the keyboard off changes nothing -- landscape is usable
// without it.
//
// Only a change from absent to present counts, and the last state seen is kept
// in NVS (`kbSeen`) so that holds across power-offs too. Otherwise a keyboard
// left attached would undo a Portrait chosen in Config at every boot. Kept out
// of the settings blob, like the antenna, so a restored backup cannot start a
// reboot.
static constexpr uint32_t kKbLandscapeCountdownSecs = 10;
static lv_obj_t   *s_kbLandscapeBackdrop = nullptr;
static lv_obj_t   *s_kbLandscapeText = nullptr;
static lv_timer_t *s_kbLandscapeTimer = nullptr;
static uint32_t    s_kbLandscapeSecsLeft = 0;
// 0 absent, 1 present, 0xFF not yet read from NVS.
static uint8_t     s_kbSeen = 0xFF;

static void kbSeenStore(bool present) {
    const uint8_t v = present ? 1 : 0;
    if (s_kbSeen == v) return;
    s_kbSeen = v;
    Preferences p;
    if (p.begin("camillia", false)) {
        p.putUChar("kbSeen", v);
        p.end();
    }
}

static void closeKbLandscapePrompt() {
    if (s_kbLandscapeTimer) {
        lv_timer_del(s_kbLandscapeTimer);
        s_kbLandscapeTimer = nullptr;
    }
    if (lvObjValid(s_kbLandscapeBackdrop)) lv_obj_del(s_kbLandscapeBackdrop);
    s_kbLandscapeBackdrop = nullptr;
    s_kbLandscapeText = nullptr;
}

static void kbLandscapeRebootNow() {
    Serial.println("[orient] keyboard attached - rebooting into Landscape");
    if (s_kbLandscapeText) {
        lv_label_set_text(s_kbLandscapeText, TR("Rebooting..."));
        lv_timer_handler();
    }
    orientSaveAndRestart(UI_ORIENT_LANDSCAPE);
}

static void kbLandscapeCancel() {
    Serial.println("[orient] keyboard landscape switch cancelled");
    closeKbLandscapePrompt();
}

static void refreshKbLandscapeText() {
    if (!s_kbLandscapeText) return;
    lv_label_set_text_fmt(s_kbLandscapeText,
                          TR("Keyboard attached. Switching to landscape in %u s."),
                          (unsigned)s_kbLandscapeSecsLeft);
}

static void onKbLandscapeTick(lv_timer_t *t) {
    LV_UNUSED(t);
    if (s_kbLandscapeSecsLeft > 0) s_kbLandscapeSecsLeft--;
    if (s_kbLandscapeSecsLeft == 0) {
        kbLandscapeRebootNow();
        return;
    }
    refreshKbLandscapeText();
}

static void openKbLandscapePrompt() {
    if (!s_rootScreen || s_kbLandscapeBackdrop) return;
    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 40;
    if (modalW > 320) modalW = 320;

    // The confirm dialog's colours, so it reads as one of the family.
    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t textColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const uint32_t cancelBg = lightUi ? 0xC76565 : 0x6B3030;
    const uint32_t rebootBg = lightUi ? 0x429A56 : 0x2F6B30;

    s_kbLandscapeBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_kbLandscapeBackdrop, w, h);
    lv_obj_align(s_kbLandscapeBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_kbLandscapeBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_kbLandscapeBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_kbLandscapeBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_kbLandscapeBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_kbLandscapeBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_kbLandscapeBackdrop, 0, 0);

    lv_obj_t *modal = lv_obj_create(s_kbLandscapeBackdrop);
    lv_obj_set_size(modal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(modal, modalBg, 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(modal, 1, 0);
    lv_obj_set_style_border_color(modal, modalBorder, 0);
    lv_obj_set_style_pad_all(modal, 12, 0);
    lv_obj_set_style_pad_row(modal, 12, 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_kbLandscapeText = lv_label_create(modal);
    lv_obj_set_width(s_kbLandscapeText, lv_pct(100));
    lv_obj_set_style_text_font(s_kbLandscapeText, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_kbLandscapeText, textColor, 0);
    lv_obj_set_style_text_align(s_kbLandscapeText, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_kbLandscapeText, LV_LABEL_LONG_WRAP);

    lv_obj_t *row = lv_obj_create(modal);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makeBtn = [](lv_obj_t *parent, const char *text, uint32_t bg, lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_height(btn, 40);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
    };
    makeBtn(row, TR("Cancel"), cancelBg,
            [](lv_event_t *e) { LV_UNUSED(e); kbLandscapeCancel(); });
    makeBtn(row, TR("Reboot now"), rebootBg,
            [](lv_event_t *e) { LV_UNUSED(e); kbLandscapeRebootNow(); });

    lv_obj_move_foreground(s_kbLandscapeBackdrop);
    s_kbLandscapeSecsLeft = kKbLandscapeCountdownSecs;
    refreshKbLandscapeText();
    s_kbLandscapeTimer = lv_timer_create(onKbLandscapeTick, 1000, nullptr);
    Serial.printf("[orient] keyboard attached in portrait - landscape in %u s\n",
                  (unsigned)kKbLandscapeCountdownSecs);
}

// Keys while the countdown is up: Enter reboots now, Esc cancels, everything
// else is swallowed so nothing underneath moves while it is deciding.
static bool kbLandscapePromptHandleKey(char k) {
    if (!s_kbLandscapeBackdrop) return false;
    if (k == KEY_ENTER) kbLandscapeRebootNow();
    else if (k == KEY_ESCAPE) kbLandscapeCancel();
    return true;
}

// Once per loop. Waits out first-time setup, the lock screen and a dark panel
// rather than counting down where nobody can see or cancel it; the attach is
// only recorded once the prompt is actually shown, so it is still pending then.
static void serviceKbLandscapeSwitch() {
    if (s_kbSeen == 0xFF) {
        Preferences p;
        s_kbSeen = 0;
        if (p.begin("camillia", true)) {
            s_kbSeen = p.getUChar("kbSeen", 0) ? 1 : 0;
            p.end();
        }
    }
    const bool present = keyboardAttached();
    if (!present) {
        // Pulled mid-countdown: whatever it was for is gone with it.
        if (s_kbLandscapeBackdrop) kbLandscapeCancel();
        kbSeenStore(false);
        return;
    }
    if (s_kbSeen == 1) return;
    if (!uiPortrait()) {           // already where the keyboard wants it
        kbSeenStore(true);
        return;
    }
    // A reboot during first-time setup would come back in portrait -- see the
    // stale-seed replacement in loadBootOrientation() -- and ask again.
    if (s_onboardingModal || s_lockScreenActive || s_screenAsleep) return;
    kbSeenStore(true);
    openKbLandscapePrompt();
}
#endif   // DEVICE_TDISPLAY_P4

static void onCfgOrientRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)UI_ORIENT_COUNT) return;
    s_cfgOrientSelection = idx;
    refreshCfgOrientSelection();
    cfgOrientAsk(idx);
}

static void openCfgOrientModal() {
    if (!s_rootScreen) return;
    if (s_cfgOrientModal || s_cfgOrientBackdrop) return;

    s_cfgOrientSelection = (s_cfg.uiOrientation < UI_ORIENT_COUNT)
                               ? (int)s_cfg.uiOrientation : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_cfgOrientBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgOrientBackdrop, w, h);
    lv_obj_align(s_cfgOrientBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgOrientBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgOrientBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgOrientBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgOrientBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgOrientBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgOrientBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgOrientBackdrop,
                        [](lv_event_t *e) {
                            if (lv_event_get_target_obj(e) != s_cfgOrientBackdrop) return;
                            closeCfgOrientModal();
                        },
                        LV_EVENT_CLICKED, nullptr);

    s_cfgOrientModal = lv_obj_create(s_cfgOrientBackdrop);
    lv_obj_set_size(s_cfgOrientModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgOrientModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgOrientModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgOrientModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgOrientModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_cfgOrientModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_cfgOrientModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_cfgOrientModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_cfgOrientModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_cfgOrientModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgOrientModal, 1, 0);
    lv_obj_set_style_pad_all(s_cfgOrientModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgOrientModal, 5, 0);
    lv_obj_set_flex_flow(s_cfgOrientModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgOrientModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgOrientBackdrop);

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t titleColor = lv_color_make(0, 0, 0);
    const lv_color_t hintColor  = lv_color_make(0, 0, 0);
    const lv_color_t rowColor   = lv_color_make(0, 0, 0);
#else
    const lv_color_t titleColor = lv_color_hex(0xD9E8FF);
    const lv_color_t hintColor  = lv_color_hex(0xA7C7FF);
    const lv_color_t rowColor   = (s_cfg.uiMode == UI_MODE_LIGHT)
                                      ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    lv_obj_t *title = lv_label_create(s_cfgOrientModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Screen orientation"));

#if UI_TOUCH_ONLY_PROFILE
    // Same corner X as the preset picker, and for the same reason: every row
    // here reboots, so leaving without choosing has to be obvious.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_cfgOrientModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeCfgOrientModal(); });
#endif

    lv_obj_t *warn = lv_label_create(s_cfgOrientModal);
    lv_obj_set_width(warn, lv_pct(100));
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(warn, hintColor, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    // The panel rotation is fixed when the display comes up, which is the whole
    // reason this cannot just take effect.
    lv_label_set_text(warn, TR("Asks first, then reboots."));

    lv_obj_t *list = lv_obj_create(s_cfgOrientModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);   // three rows always fit
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);

    // Shapes rather than angles: "240x320" is checkable against what is in your
    // hand, where "rotation 3" is only meaningful next to the source.
    static const char *kOrientDesc[UI_ORIENT_COUNT] = {
        TR("Wide"), TR("Tall"), TR("Tall, turned 180"),
    };

    for (int i = 0; i < (int)UI_ORIENT_COUNT; i++) {
        const bool current = (s_cfg.uiOrientation == (uint8_t)i);
        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_cfgOrientRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onCfgOrientRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        // Marked in the text as well as by the highlight: the highlight also
        // means "where the selection is", and those part company the moment
        // anyone presses Down.
        if (current) lv_label_set_text_fmt(name, TR("%s  (current)"), uiOrientName((uint8_t)i));
        else         lv_label_set_text(name, uiOrientName((uint8_t)i));

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_text(desc, kOrientDesc[i]);
        }
    }

    lv_obj_t *hint = lv_label_create(s_cfgOrientModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap one to apply."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Apply  %s=Cancel"), modalCloseKeyLabel());
#endif

    refreshCfgOrientSelection();
}
#endif  // HAS_RUNTIME_ORIENTATION

#if HAS_NAV_ALIGN_SETTING
// ── Config -> Nav Alignment ──────────────────────────────────────────────────
// Every placement choice for the nav bar, as one picker: Left, Center, Right.
// Center is the row along the bottom; Left and Right condense it into a column
// down that edge (navIsSideColumn()). Shaped like the orientation picker above
// -- a fixed three-row list, tap or Enter to apply -- but nothing here reboots,
// so there is no confirm: a pick rebuilds the screens around the bar and that
// is the whole of it.
static constexpr int kNavAlignCount = 3;   // NAV_ALIGN_LEFT .. NAV_ALIGN_RIGHT
static lv_obj_t *s_cfgNavAlignBackdrop = nullptr;
static lv_obj_t *s_cfgNavAlignModal = nullptr;
static lv_obj_t *s_cfgNavAlignRows[kNavAlignCount] = {};
static int       s_cfgNavAlignSelection = NAV_ALIGN_CENTER;

static void closeCfgNavAlignModal() {
    if (lvObjValid(s_cfgNavAlignBackdrop)) {
        lv_obj_del(s_cfgNavAlignBackdrop);
    } else if (lvObjValid(s_cfgNavAlignModal)) {
        lv_obj_del(s_cfgNavAlignModal);
    }
    s_cfgNavAlignBackdrop = nullptr;
    s_cfgNavAlignModal = nullptr;
    memset(s_cfgNavAlignRows, 0, sizeof(s_cfgNavAlignRows));
    s_cfgNavAlignSelection = NAV_ALIGN_CENTER;
}

static void refreshCfgNavAlignSelection() {
    if (!s_cfgNavAlignModal) return;
    paintPickerRows(s_cfgNavAlignRows, kNavAlignCount, s_cfgNavAlignSelection,
                    PICKER_SCROLL_NONE);
}

static void cfgNavAlignCommit(int idx) {
    if (idx < 0 || idx >= kNavAlignCount) return;
    const NavAlign want = (NavAlign)idx;
    closeCfgNavAlignModal();
    if (cfgNavAlign(s_cfg) == want) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Already %s."), navAlignName(want));
        refreshCfgModal();
        return;
    }
    // Stored as a letter even when it matches the board's unset default: a
    // deliberate pick should not move again when the orientation does.
    s_cfg.navAlign = navAlignCode(want);
    persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Nav Alignment: %s"), navAlignName(want));
    // Every screen is laid out around the bar, so a rebuild, as for the Nav Bar
    // toggle -- deferred because this runs inside an LVGL event callback.
    scheduleUiRebuild(true);
}

static void onCfgNavAlignRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= kNavAlignCount) return;
    s_cfgNavAlignSelection = idx;
    refreshCfgNavAlignSelection();
    cfgNavAlignCommit(idx);
}

static void openCfgNavAlignModal() {
    if (!s_rootScreen) return;
    if (s_cfgNavAlignModal || s_cfgNavAlignBackdrop) return;

    const NavAlign current = cfgNavAlign(s_cfg);
    s_cfgNavAlignSelection = (int)current;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_cfgNavAlignBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgNavAlignBackdrop, w, h);
    lv_obj_align(s_cfgNavAlignBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgNavAlignBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgNavAlignBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgNavAlignBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgNavAlignBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgNavAlignBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgNavAlignBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgNavAlignBackdrop,
                        [](lv_event_t *e) {
                            if (lv_event_get_target_obj(e) != s_cfgNavAlignBackdrop) return;
                            closeCfgNavAlignModal();
                        },
                        LV_EVENT_CLICKED, nullptr);

    s_cfgNavAlignModal = lv_obj_create(s_cfgNavAlignBackdrop);
    lv_obj_set_size(s_cfgNavAlignModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgNavAlignModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgNavAlignModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgNavAlignModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgNavAlignModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_cfgNavAlignModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_cfgNavAlignModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_cfgNavAlignModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_cfgNavAlignModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_cfgNavAlignModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgNavAlignModal, 1, 0);
    lv_obj_set_style_pad_all(s_cfgNavAlignModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgNavAlignModal, 5, 0);
    lv_obj_set_flex_flow(s_cfgNavAlignModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgNavAlignModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgNavAlignBackdrop);

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t titleColor = lv_color_make(0, 0, 0);
    const lv_color_t hintColor  = lv_color_make(0, 0, 0);
    const lv_color_t rowColor   = lv_color_make(0, 0, 0);
#else
    const lv_color_t titleColor = lv_color_hex(0xD9E8FF);
    const lv_color_t hintColor  = lv_color_hex(0xA7C7FF);
    const lv_color_t rowColor   = (s_cfg.uiMode == UI_MODE_LIGHT)
                                      ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);
#endif

    lv_obj_t *title = lv_label_create(s_cfgNavAlignModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Nav Alignment"));

#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_cfgNavAlignModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeCfgNavAlignModal(); });
#endif

    lv_obj_t *list = lv_obj_create(s_cfgNavAlignModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);   // three rows always fit
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);

    static const char *kNavAlignDesc[kNavAlignCount] = {
        TR("Condensed column down the left edge"),
        TR("Full row along the bottom"),
        TR("Condensed column down the right edge"),
    };

    for (int i = 0; i < kNavAlignCount; i++) {
        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_cfgNavAlignRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onCfgNavAlignRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        // Marked in the text as well as by the highlight, as in the orientation
        // picker: the highlight moves with the keys, the mark does not.
        if (i == (int)current) {
            lv_label_set_text_fmt(name, TR("%s  (current)"), navAlignName((NavAlign)i));
        } else {
            lv_label_set_text(name, navAlignName((NavAlign)i));
        }

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            lv_label_set_text(desc, kNavAlignDesc[i]);
        }
    }

    lv_obj_t *hint = lv_label_create(s_cfgNavAlignModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap one to apply."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Apply  %s=Cancel"), modalCloseKeyLabel());
#endif

    refreshCfgNavAlignSelection();
}
#endif  // HAS_NAV_ALIGN_SETTING

#if defined(DEVICE_TDISPLAY_P4)
// ── Antenna (T-Display P4) ───────────────────────────────────────────────────
// The board routes the radio to its on-board antenna or to the external socket
// through a SKY13453 selector (MeshRadio::setExternalAntenna()). Transmitting
// into an empty socket can damage the radio, so moving to external is asked
// first; moving back to the on-board antenna never is. Applied live -- the
// selector is a GPIO on the expander, the radio itself is not touched.
static const char *const kAntennaExternalWarning = TR_NOOP(
    "Switching to external can damage the radio if the antenna is not attached.  "
    "Please attach the external antenna before pressing Yes");

static void cfgAntennaCommit(int external) {
    s_cfg.p4Antenna = external ? kP4AntennaExternal : 0;
    Radio.setExternalAntenna(external != 0);
    persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Antenna: %s"),
             external ? TR("external") : TR("internal"));
    refreshCfgModal();
}
#endif


// ── UI language picker (issue #99) ───────────────────────────────────────────
// Config -> Language. A dropdown of every built-in language, each in its own
// name ("Español", "Français"), with Cancel/Save; Save stores the choice and
// reboots, because every label already on screen was built in the old one and
// a rebuild of every screen is exactly what a reboot is. Keyboard boards move
// the dropdown with Up/Down, save with Enter and cancel with the close key.
static lv_obj_t *s_cfgLangBackdrop = nullptr;
static lv_obj_t *s_cfgLangModal = nullptr;
static lv_obj_t *s_cfgLangDropdown = nullptr;

static void closeCfgLangModal() {
    if (lvObjValid(s_cfgLangBackdrop)) {
        lv_obj_del(s_cfgLangBackdrop);
    } else if (lvObjValid(s_cfgLangModal)) {
        lv_obj_del(s_cfgLangModal);
    }
    s_cfgLangBackdrop = nullptr;
    s_cfgLangModal = nullptr;
    s_cfgLangDropdown = nullptr;
}

static void cfgLangCancel() {
    closeCfgLangModal();
    refreshCfgModal();
}

static void cfgLangSave() {
    const uint8_t want = lvObjValid(s_cfgLangDropdown)
                             ? (uint8_t)lv_dropdown_get_selected(s_cfgLangDropdown)
                             : s_cfg.uiLanguage;
    closeCfgLangModal();
    if (want >= LANG_COUNT || want == s_cfg.uiLanguage) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Already %s."),
                 kUiLangNames[s_cfg.uiLanguage < LANG_COUNT ? s_cfg.uiLanguage : LANG_EN]);
        refreshCfgModal();
        return;
    }
    s_cfg.uiLanguage = want;
    persistConfigToPrefs();
    // Said in the language being left: it is the one still on screen.
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("%s - rebooting..."), kUiLangNames[want]);
    refreshCfgModal();
    lv_timer_handler();
    delay(900);
    flushPersistentState();   // transcripts too, before we go
    ESP.restart();
}

static void stepCfgLang(int delta) {
    if (!lvObjValid(s_cfgLangDropdown) || delta == 0) return;
    int next = (int)lv_dropdown_get_selected(s_cfgLangDropdown) + delta;
    if (next < 0) next = 0;
    if (next > (int)LANG_COUNT - 1) next = (int)LANG_COUNT - 1;
    lv_dropdown_set_selected(s_cfgLangDropdown, (uint32_t)next);
}

static void onCfgLangBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgLangBackdrop) return;
    cfgLangCancel();
}

static void openCfgLangModal() {
    if (!s_rootScreen || s_cfgLangModal || s_cfgLangBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgLangBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgLangBackdrop, w, h);
    lv_obj_align(s_cfgLangBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgLangBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgLangBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgLangBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgLangBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgLangBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgLangBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgLangBackdrop, onCfgLangBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgLangModal = lv_obj_create(s_cfgLangBackdrop);
    lv_obj_set_size(s_cfgLangModal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(s_cfgLangModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgLangModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgLangModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_cfgLangModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_cfgLangModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_cfgLangModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_cfgLangModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_cfgLangModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgLangModal, 1, 0);
    lv_obj_set_style_pad_all(s_cfgLangModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgLangModal, 8, 0);
    lv_obj_set_flex_flow(s_cfgLangModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgLangModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgLangBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgLangModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Language"));

    // Options in each language's own name, so the list is readable whatever
    // language the device is currently in.
    // 22 names at up to ~20 bytes each, plus separators.
    char options[512] = "";
    for (int i = 0; i < (int)LANG_COUNT; i++) {
        if (i) strlcat(options, "\n", sizeof(options));
        strlcat(options, kUiLangNames[i], sizeof(options));
    }
    s_cfgLangDropdown = lv_dropdown_create(s_cfgLangModal);
    lv_obj_set_width(s_cfgLangDropdown, lv_pct(100));
    lv_dropdown_set_options(s_cfgLangDropdown, options);
    lv_dropdown_set_selected(s_cfgLangDropdown,
                             s_cfg.uiLanguage < LANG_COUNT ? s_cfg.uiLanguage : LANG_EN);
    lv_obj_set_style_text_font(s_cfgLangDropdown, &lv_font_montserrat_14, 0);

#if UI_TOUCH_ONLY_PROFILE
    appendHeltecCancelSaveRow(
        s_cfgLangModal,
        [](lv_event_t *e) { LV_UNUSED(e); cfgLangCancel(); },
        [](lv_event_t *e) { LV_UNUSED(e); cfgLangSave(); });
#else
    lv_obj_t *hint = lv_label_create(s_cfgLangModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(hint, TR("Up/Down=Change  Enter=Save  %s=Cancel"), modalCloseKeyLabel());
#endif
}

static void closeCfgNodeNameModal() {
    if (lvObjValid(s_cfgNodeNameBackdrop)) {
        lv_obj_del(s_cfgNodeNameBackdrop);
    } else if (lvObjValid(s_cfgNodeNameModal)) {
        lv_obj_del(s_cfgNodeNameModal);
    }
    s_cfgNodeNameBackdrop   = nullptr;
    s_cfgNodeNameModal      = nullptr;
    s_cfgNodeNameLongInput  = nullptr;
    s_cfgNodeNameShortInput = nullptr;
    s_cfgNodeNameKeyboard   = nullptr;
    s_cfgNodeNameStatus     = nullptr;
    s_cfgNodeNameFocus      = 0;
}

static void cfgNodeNameSetStatus(const char *msg) {
    if (!lvObjValid(s_cfgNodeNameStatus)) return;
    lv_label_set_text(s_cfgNodeNameStatus, msg ? msg : "");
}

static lv_obj_t *cfgNodeNameFocusedInput() {
    return (s_cfgNodeNameFocus == 1) ? s_cfgNodeNameShortInput : s_cfgNodeNameLongInput;
}

// Paints the focus border and, on the touch-only builds, points the on-screen
// keyboard at the field that is taking characters. Two fields is one more than
// every other text modal here has, so which one is live has to be visible.
static void refreshCfgNodeNameFocus() {
    if (!s_cfgNodeNameLongInput || !s_cfgNodeNameShortInput) return;
#if defined(DEVICE_TDECK_PRO)
    const lv_color_t focusBorder = lv_color_make(0, 0, 0);
    const lv_color_t idleBorder  = lv_color_make(0, 0, 0);
#else
    const lv_color_t focusBorder = lv_color_hex(0x9CC2FF);
    const lv_color_t idleBorder  = lv_color_hex(0x4C76BA);
#endif
    lv_obj_t *focused = cfgNodeNameFocusedInput();
    lv_obj_t *fields[2] = {s_cfgNodeNameLongInput, s_cfgNodeNameShortInput};
    for (int i = 0; i < 2; i++) {
        const bool sel = (fields[i] == focused);
        lv_obj_set_style_border_width(fields[i], sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(fields[i], sel ? focusBorder : idleBorder, 0);
    }
    if (s_cfgNodeNameKeyboard) lv_keyboard_set_textarea(s_cfgNodeNameKeyboard, focused);
}

static void cfgNodeNameSetFocus(int which) {
    s_cfgNodeNameFocus = (which == 1) ? 1 : 0;
    refreshCfgNodeNameFocus();
}

// Commit both fields. Rejects an empty long name the way onboarding does, and
// fills an empty short name from the long one rather than rejecting it, since
// one can always be derived.
static void cfgNodeNameSave() {
    if (!s_cfgNodeNameLongInput || !s_cfgNodeNameShortInput) return;

    const char *typedLong  = lv_textarea_get_text(s_cfgNodeNameLongInput);
    const char *typedShort = lv_textarea_get_text(s_cfgNodeNameShortInput);
    String longName  = typedLong  ? String(typedLong)  : String();
    String shortName = typedShort ? String(typedShort) : String();
    longName.trim();
    shortName.trim();

    if (longName.length() == 0) {
        cfgNodeNameSetStatus(TR("Long name cannot be empty"));
        cfgNodeNameSetFocus(0);
        return;
    }

    char newLong[sizeof(s_cfg.nodeLong)];
    char newShort[sizeof(s_cfg.nodeShort)];
    utf8util::copyTruncate(newLong, sizeof(newLong), longName.c_str());
    if (shortName.length() == 0) {
        onboardingDeriveShortFromLong(newLong, newShort, sizeof(newShort));
    } else {
        utf8util::copyTruncate(newShort, sizeof(newShort), shortName.c_str());
    }

    const bool changed = (strcmp(newLong, s_cfg.nodeLong) != 0)
                         || (strcmp(newShort, s_cfg.nodeShort) != 0);
    if (changed) {
        utf8util::copyTruncate(s_cfg.nodeLong,  sizeof(s_cfg.nodeLong),  newLong);
        utf8util::copyTruncate(s_cfg.nodeShort, sizeof(s_cfg.nodeShort), newShort);
        persistConfigToPrefs();
        // The mesh only learns a name from a NODEINFO, and the next scheduled
        // one can be an hour out — so the rename would look like it had not
        // taken anywhere but on this screen. No reboot: nothing else in the
        // radio configuration moved.
        webCfgQueueAnnounce();
        Serial.printf("[cfg] node name set to \"%s\" (%s); NODEINFO queued\n",
                      s_cfg.nodeLong, s_cfg.nodeShort);
    }

    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Node Name: %s (%s)%s"),
             s_cfg.nodeLong, s_cfg.nodeShort, changed ? "" : TR(" (unchanged)"));
    closeCfgNodeNameModal();
    refreshCfgModal();
}

static void cfgNodeNameCancel() {
    closeCfgNodeNameModal();
    refreshCfgModal();
}

static void onCfgNodeNameFieldPressed(lv_event_t *e) {
    cfgNodeNameSetFocus((int)(intptr_t)lv_event_get_user_data(e));
}

static void onCfgNodeNameBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgNodeNameBackdrop) return;
    cfgNodeNameCancel();
}

#if UI_TOUCH_ONLY_PROFILE
static void onCfgNodeNameSavePressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgNodeNameSave();
}

static void onCfgNodeNameClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgNodeNameCancel();
}
#endif

static void openCfgNodeNameModal() {
    if (!s_rootScreen || s_cfgNodeNameModal || s_cfgNodeNameBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 320) modalW = 320;
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    modalW = w;
#endif

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t modalBg      = lv_color_make(255, 255, 255);
    const lv_color_t modalBorder  = lv_color_make(0, 0, 0);
    const lv_color_t titleColor   = lv_color_make(0, 0, 0);
    const lv_color_t captionColor = lv_color_make(0, 0, 0);
    const lv_color_t inputBg      = lv_color_make(255, 255, 255);
    const lv_color_t inputText    = lv_color_make(0, 0, 0);
#else
    const lv_color_t modalBg      = lv_color_hex(0x0E285B);
    const lv_color_t modalBorder  = lv_color_hex(0x5C86C6);
    const lv_color_t titleColor   = lv_color_hex(0xD9E8FF);
    const lv_color_t captionColor = lv_color_hex(0xA7C7FF);
    const lv_color_t inputBg      = lv_color_hex(0x102B61);
    const lv_color_t inputText    = lv_color_hex(0xE8F1FF);
#endif

    s_cfgNodeNameBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgNodeNameBackdrop, w, h);
    lv_obj_align(s_cfgNodeNameBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgNodeNameBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgNodeNameBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgNodeNameBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgNodeNameBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgNodeNameBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgNodeNameBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgNodeNameBackdrop, onCfgNodeNameBackdropPressed,
                        LV_EVENT_CLICKED, nullptr);

    s_cfgNodeNameModal = lv_obj_create(s_cfgNodeNameBackdrop);
#if UI_TOUCH_ONLY_PROFILE
    lv_obj_set_size(s_cfgNodeNameModal, modalW, h - 18);
#else
    lv_obj_set_size(s_cfgNodeNameModal, modalW, LV_SIZE_CONTENT);
#endif
    lv_obj_align(s_cfgNodeNameModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgNodeNameModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_cfgNodeNameModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_cfgNodeNameModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgNodeNameModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgNodeNameModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_cfgNodeNameModal, 8, 0);
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    lv_obj_set_style_pad_left(s_cfgNodeNameModal, 2, 0);
    lv_obj_set_style_pad_right(s_cfgNodeNameModal, 2, 0);
#endif
    lv_obj_set_style_pad_row(s_cfgNodeNameModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgNodeNameModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgNodeNameModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgNodeNameBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgNodeNameModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Node Name"));

    // Side by side rather than stacked: it keeps the whole editor inside one
    // screen on the 135-line panels, and the two fields are read together
    // anyway — the short name is an abbreviation of what is to its left.
    lv_obj_t *fieldRow = lv_obj_create(s_cfgNodeNameModal);
    lv_obj_set_width(fieldRow, lv_pct(100));
    lv_obj_set_height(fieldRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(fieldRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(fieldRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fieldRow, 0, 0);
    lv_obj_set_style_pad_all(fieldRow, 0, 0);
    lv_obj_set_style_pad_column(fieldRow, 6, 0);
    lv_obj_set_flex_flow(fieldRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fieldRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    auto makeField = [&](const char *caption, int grow, size_t maxLen,
                         const char *value, int which) {
        lv_obj_t *col = lv_obj_create(fieldRow);
        lv_obj_set_flex_grow(col, grow);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_row(col, 2, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        lv_obj_t *cap = lv_label_create(col);
        lv_obj_set_width(cap, lv_pct(100));
        lv_obj_set_style_text_font(cap, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(cap, captionColor, 0);
        lv_label_set_text(cap, caption);

        lv_obj_t *ta = lv_textarea_create(col);
        if (!ta) return (lv_obj_t *)nullptr;
        lv_obj_set_width(ta, lv_pct(100));
        lv_obj_set_height(ta, 36);
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ta, inputText, 0);
        lv_obj_set_style_bg_color(ta, inputBg, 0);
        lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ta, 1, 0);
        lv_obj_set_style_pad_all(ta, 4, 0);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, (uint32_t)maxLen);
        lv_textarea_set_placeholder_text(ta, caption);
        lv_textarea_set_text(ta, value ? value : "");
        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
        lv_obj_add_event_cb(ta, onCfgNodeNameFieldPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)which);
        accentAttach(ta);
        return ta;
    };

    s_cfgNodeNameLongInput  = makeField(TR("Long name"), 3, sizeof(s_cfg.nodeLong) - 1,
                                        s_cfg.nodeLong, 0);
    s_cfgNodeNameShortInput = makeField(TR("Short"), 1, sizeof(s_cfg.nodeShort) - 1,
                                        s_cfg.nodeShort, 1);
    if (!s_cfgNodeNameLongInput || !s_cfgNodeNameShortInput) {
        logLvglMemDiag("node name modal aborted (low LVGL mem)");
        closeCfgNodeNameModal();
        return;
    }

    s_cfgNodeNameStatus = lv_label_create(s_cfgNodeNameModal);
    lv_obj_set_width(s_cfgNodeNameStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgNodeNameStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgNodeNameStatus, captionColor, 0);
    lv_obj_set_style_text_align(s_cfgNodeNameStatus, LV_TEXT_ALIGN_LEFT, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(s_cfgNodeNameStatus, TR("Tap a field to edit, then Save"));
#else
    lv_label_set_text_fmt(s_cfgNodeNameStatus, TR("Enter=Save  Tab=Field  %s=Erase/Back"),
                          modalCloseKeyLabel());
#endif

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *btnRow = lv_obj_create(s_cfgNodeNameModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *closeBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(closeBtn, 1);
    lv_obj_set_height(closeBtn, lv_pct(100));
    lv_obj_add_event_cb(closeBtn, onCfgNodeNameClosePressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *closeLbl = lv_label_create(closeBtn);
    lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(closeLbl, TR("Close"));
    lv_obj_center(closeLbl);

    lv_obj_t *saveBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(saveBtn, 1);
    lv_obj_set_height(saveBtn, lv_pct(100));
    lv_obj_add_event_cb(saveBtn, onCfgNodeNameSavePressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *saveLbl = lv_label_create(saveBtn);
    lv_obj_set_style_text_font(saveLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(saveLbl, TR("Save"));
    lv_obj_center(saveLbl);

    // Not with a physical keyboard attached (the T-Display P4's expansion):
    // keys typed on it reach this field through pumpKeyboardInput(), and an
    // on-screen one would only take half the panel. Checked as this opens.
    // cfgNodeNameSetFocus() already copes with there being none.
    if (!keyboardAttached()) {
        s_cfgNodeNameKeyboard = lv_keyboard_create(s_cfgNodeNameModal);
        configureOnScreenKeyboard(s_cfgNodeNameKeyboard);
    }
#endif

    cfgNodeNameSetFocus(0);
}

static void closeCfgModal() {
    // This screen may have been carrying a copy of the nav bar, and its status
    // icons go with it.
    clearNavBarStatusIcons();
    revertCfgBrightnessPreview();
    revertCfgBattCalPreview();  // same reason: an unapplied preview must not persist
#if HAS_VOLUME_CONTROL
    revertCfgVolumePreview();
#endif
    cancelCfgSlider();    // an unapplied slider position must not persist either
    // The sound picker stages the same way the sliders do: previewing writes the
    // tone straight into s_cfg.msgAlertSound, and persistMessageAlertSetting()
    // is only a markConfigDirty() — a deferred whole-config flush. So a tone
    // auditioned and never applied would be written out by the next unrelated
    // setting change. Reverting here closes that, and matches what the four
    // lines above already do for brightness, battery trim and the sliders.
    cancelAlertSoundModal();
    // Reopening Config should show all of it. A filter left armed from last
    // time reads as rows having gone missing.
    s_cfgFilterOpen = false;
    s_cfgFilterLen = 0;
    s_cfgFilter[0] = '\0';
    closeCfgWifiPickerModal();
    closeCfgNodeNameModal();
    // A Config text row left open: nothing is staged, so closing is the cancel.
    if (s_chanTextField == kChanTextCfgGen) closeChanTextModal();
    closeCfgPresetModal();
    closeMeshBeaconCfgModal();
#if FEATURE_LICENSED_MODE
    closeLicCfgModal();
#endif
    closeCfgCommunityModal();
    closeCfgLangModal();
#if HAS_RUNTIME_ORIENTATION
    closeCfgOrientModal();
#endif
#if HAS_NAV_ALIGN_SETTING
    closeCfgNavAlignModal();
#endif
#if HAS_BLE_KEYBOARD
    closeCfgBleKbdModal();
#endif
    closeCfgConfirmModal();
    closeCfgActionMessageModal();
    closeSysStatsModal();
    closeReleaseNotesModal();
    closeTimeCfgModal();
#if HAS_NOTIFY_LED
    closeNotifyLedColorModal();
#endif
#if !defined(DEVICE_TLORA_PAGER_TFT)
    closeNodeInfoModal();
#endif
    lvObjDeleteSafe(s_cfgModal);
    s_cfgActionList = nullptr;
    s_cfgInfoList = nullptr;
    s_cfgHeaderStatus = nullptr;
#if UI_CFG_SECTIONS
    s_cfgBackBtn = nullptr;
    s_cfgInfoBtn = nullptr;
#endif
    s_cfgPaintedSelection = -1;   // rows are gone; nothing carries the highlight
    s_cfgAwaitEnterRelease = false;
    s_cfgInfoPanelFocused = false;
}

static void closeCfgActionMessageModal() {
    if (lvObjValid(s_cfgActionMsgBackdrop)) {
        lv_obj_del(s_cfgActionMsgBackdrop);
    } else if (lvObjValid(s_cfgActionMsgModal)) {
        lv_obj_del(s_cfgActionMsgModal);
    }
    s_cfgActionMsgBackdrop = nullptr;
    s_cfgActionMsgModal = nullptr;
    s_cfgActionMsgOpenedMs = 0;
}

static void onCfgActionMessageBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) == s_cfgActionMsgBackdrop) {
        closeCfgActionMessageModal();
    }
}

static void openCfgActionMessageModal(const char *msg, const char *titleText) {
    if (!s_rootScreen) return;

    const char *displayMsg = msg;
    while (displayMsg && *displayMsg && isspace((unsigned char)*displayMsg)) {
        displayMsg++;
    }
    if (!displayMsg || !displayMsg[0]) {
        displayMsg = TR("No result details available.");
    }

    Serial.printf("[lvgl-cfg] popup: %s\n", displayMsg);

    closeCfgActionMessageModal();

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    int modalW = screenW - 28;
    if (modalW < 180) modalW = screenW - 8;
    if (modalW > 320) modalW = 320;
    lv_coord_t contentW = (lv_coord_t)(modalW - 18);
    if (contentW < 64) contentW = 64;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t bodyPanelBg = lightUi ? lv_color_hex(0xF5F9FF) : lv_color_hex(0x123266);
    const lv_color_t bodyTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
#if !UI_TOUCH_ONLY_PROFILE
    const lv_color_t hintTextColor = lightUi ? lv_color_hex(0x35567E) : lv_color_hex(0xA7C7FF);
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *bodyFont = &lv_font_montserrat_14;
#elif defined(DEVICE_TDECK)
    const lv_font_t *bodyFont = &lv_font_montserrat_14;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif

    s_cfgActionMsgBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgActionMsgBackdrop, screenW, screenH);
    lv_obj_align(s_cfgActionMsgBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgActionMsgBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgActionMsgBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgActionMsgBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgActionMsgBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgActionMsgBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgActionMsgBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgActionMsgBackdrop,
                        onCfgActionMessageBackdropPressed,
                        LV_EVENT_CLICKED,
                        nullptr);

    s_cfgActionMsgModal = lv_obj_create(s_cfgActionMsgBackdrop);
    lv_obj_set_size(s_cfgActionMsgModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgActionMsgModal, screenH - 14, 0);
    lv_obj_align(s_cfgActionMsgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_cfgActionMsgModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgActionMsgModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_cfgActionMsgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgActionMsgModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgActionMsgModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_cfgActionMsgModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgActionMsgModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgActionMsgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionMsgModal,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_cfgActionMsgModal);
    lv_obj_set_width(title, contentW);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, (titleText && titleText[0]) ? titleText : TR("Action Result"));

    lv_obj_t *bodyPanel = lv_obj_create(s_cfgActionMsgModal);
    lv_obj_set_width(bodyPanel, contentW);
    lv_coord_t bodyMaxH = (lv_coord_t)(screenH / 3);
    if (bodyMaxH < 44) bodyMaxH = 44;
    if (bodyMaxH > 96) bodyMaxH = 96;
    lv_obj_set_height(bodyPanel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(bodyPanel, bodyMaxH, 0);
    lv_obj_add_flag(bodyPanel, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(bodyPanel);
    lv_obj_set_scrollbar_mode(bodyPanel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(bodyPanel, bodyPanelBg, 0);
    lv_obj_set_style_bg_opa(bodyPanel, LV_OPA_60, 0);
    lv_obj_set_style_border_width(bodyPanel, 1, 0);
    lv_obj_set_style_border_color(bodyPanel, modalBorder, 0);
    lv_obj_set_style_pad_left(bodyPanel, 6, 0);
    lv_obj_set_style_pad_right(bodyPanel, 6, 0);
    lv_obj_set_style_pad_top(bodyPanel, 4, 0);
    lv_obj_set_style_pad_bottom(bodyPanel, 4, 0);

    lv_obj_t *body = lv_label_create(bodyPanel);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, bodyFont, 0);
    lv_obj_set_style_text_color(body, bodyTextColor, 0);
    lv_obj_set_style_text_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, displayMsg);

#if UI_TOUCH_ONLY_PROFILE
    // Touch-only build: a corner X where the key hint used to be. Tapping the
    // backdrop already dismissed this, but that is a gesture you have to know
    // about, and this popup is the one that tells you what an action just did —
    // it should not be the one you have to guess your way out of.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_cfgActionMsgModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeCfgActionMessageModal(); });
#else
    lv_obj_t *hint = lv_label_create(s_cfgActionMsgModal);
    lv_obj_set_width(hint, contentW);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintTextColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("%s = Close"), modalCloseKeyLabel());
#endif

    lv_obj_move_foreground(s_cfgActionMsgBackdrop);
    s_cfgActionMsgOpenedMs = millis();
}

#if !defined(DEVICE_TLORA_PAGER_TFT)
static void closeNodeInfoModal() {
    lvObjDeleteSafe(s_nodeInfoModal);
}

// Shows the current node's identity/radio details in a dismissible popup,
// layered over the CFG modal. Any key (or the close key) dismisses it.
#if HAS_ENV_SENSOR_TELEMETRY
// Transparent, non-scrolling flex column. The Device Info page puts the device
// rows in one and the environment readings in another; the modal itself keeps
// ownership of scrolling, so neither column scrolls on its own.
static lv_obj_t *infoMakeColumn(lv_obj_t *parent) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 3, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    return col;
}

static void infoAddRow(lv_obj_t *parent, const lv_font_t *font,
                       uint32_t color, const char *text) {
    lv_obj_t *row = lv_label_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_text_font(row, font, 0);
    lv_obj_set_style_text_color(row, lv_color_hex(color), 0);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    lv_label_set_text(row, text);
}
#endif  // HAS_ENV_SENSOR_TELEMETRY

static void openNodeInfoModal() {
    if (!s_rootScreen || s_nodeInfoModal) return;

    char info[kDeviceInfoMaxLines][96] = {};
    int infoCount = buildDeviceInfoLines(info, kDeviceInfoMaxLines);

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;

#if defined(DEVICE_TDECK)
    const lv_font_t *bodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif

    s_nodeInfoModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodeInfoModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_nodeInfoModal, lv_disp_get_ver_res(NULL) - 16, 0);
    lv_obj_align(s_nodeInfoModal, LV_ALIGN_CENTER, 0, 0);
    setupVScroll(s_nodeInfoModal);
    lv_obj_set_scrollbar_mode(s_nodeInfoModal, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_nodeInfoModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodeInfoModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodeInfoModal, 1, 0);
    lv_obj_set_style_border_color(s_nodeInfoModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_nodeInfoModal, 6, 0);
    lv_obj_set_style_pad_row(s_nodeInfoModal, 3, 0);
    lv_obj_set_flex_flow(s_nodeInfoModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodeInfoModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_nodeInfoModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, TR("Device Info"));

#if UI_TOUCH_ONLY_PROFILE
    // The title no longer has to share its row with a Close button: the X
    // floats over the corner. It still has to be the corner rather than the
    // foot of the list — this modal scrolls, and a control down there would
    // have to be scrolled to before it could be tapped.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_nodeInfoModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeNodeInfoModal(); });
#endif

    // Device rows go in the left column and environment readings in the right,
    // but only when there is actually something to put on the right. With no
    // sensor detected the rows stay full width rather than sitting in a
    // half-width column beside an empty gap.
    lv_obj_t *rowsParent = s_nodeInfoModal;
#if HAS_ENV_SENSOR_TELEMETRY
    const uint8_t envCount = envSensorCount();
    lv_obj_t *envCol = nullptr;
    if (envCount > 0) {
        lv_obj_t *body = lv_obj_create(s_nodeInfoModal);
        lv_obj_set_width(body, lv_pct(100));
        lv_obj_set_height(body, LV_SIZE_CONTENT);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 0, 0);
        lv_obj_set_style_pad_column(body, 8, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        rowsParent = infoMakeColumn(body);
        envCol = infoMakeColumn(body);
    }
#endif

    for (int i = 0; i < infoCount; i++) {
        lv_obj_t *row = lv_label_create(rowsParent);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, bodyFont, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, info[i]);
    }

#if HAS_ENV_SENSOR_TELEMETRY
    if (envCol) {
        infoAddRow(envCol, bodyFont, 0xA7C7FF, TR("Environment"));
        char line[48];
        for (uint8_t i = 0; i < envCount; i++) {
            EnvSensorInfo si;
            if (!envSensorInfoAt(i, si)) {
                // Detected but not answering right now. Naming it is more use
                // than dropping the row: it says the part is there and the read
                // failed, which is a different problem from having no sensor.
                infoAddRow(envCol, bodyFont, 0xD9E8FF, TR("sensor: read failed"));
                continue;
            }
            infoAddRow(envCol, bodyFont, 0xD9E8FF, si.name ? si.name : "sensor");
            if (si.hasTemperature) {
                snprintf(line, sizeof(line), TR("  Temp   %.1f C"), si.temperatureC);
                infoAddRow(envCol, bodyFont, 0xD9E8FF, line);
            }
            if (si.hasHumidity) {
                snprintf(line, sizeof(line), TR("  Humid  %.1f %%"), si.humidityPct);
                infoAddRow(envCol, bodyFont, 0xD9E8FF, line);
            }
            if (si.hasPressure) {
                snprintf(line, sizeof(line), TR("  Press  %.1f hPa"), si.pressureHpa);
                infoAddRow(envCol, bodyFont, 0xD9E8FF, line);
            }
        }
    }
#endif

    // Heltec has the Close button above instead: naming a key on a build with no
    // keyboard was only ever telling the user to look for something that is not
    // there.
#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_nodeInfoModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, bodyFont, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_pad_top(hint, 3, 0);
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO)
    // T-Deck keyboards have no dedicated Up/Down keys; J/K drive scroll.
    lv_label_set_text_fmt(hint, TR("J/K = Scroll   %s = Close"), modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint,
                          TR("Up/Down/J/K = Scroll   %s = Close"),
                          modalCloseKeyLabel());
#endif
#endif   // !UI_TOUCH_ONLY_PROFILE
}
#endif

