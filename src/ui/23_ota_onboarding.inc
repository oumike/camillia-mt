// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: OTA update prompt and first-time setup.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// ── Boot firmware-update prompt ───────────────────────────────────────────────
// Once per boot, with WiFi up, ask the release server whether a newer build
// exists and offer to install it. Declining is remembered for the rest of this
// boot only — a reboot asks again. The check is one plain-HTTP GET against the
// hardcoded release proxy (see ota_update.cpp), so it is cheap enough to run
// inline from the UI loop; the signed binary is what actually gets verified.
static void closeOtaUpdatePrompt() {
    if (lvObjValid(s_otaPromptBackdrop)) {
        lv_obj_del(s_otaPromptBackdrop);
    } else if (lvObjValid(s_otaPromptModal)) {
        lv_obj_del(s_otaPromptModal);
    }
    s_otaPromptBackdrop = nullptr;
    s_otaPromptModal = nullptr;
}

static void otaPromptDecline() {
    Serial.println("[ota-check] update declined for this boot");
    closeOtaUpdatePrompt();
}

static void otaPromptAccept() {
    Serial.printf("[ota-check] update accepted: %s -> %s\n", APP_VERSION, s_otaAutoCheckTag);
    closeOtaUpdatePrompt();
    // Same install path as the CFG screen's Firmware Update action: it reboots
    // into OTA minimal mode, where the download and signature check happen.
    performCfgAction(CFG_ACTION_OTA_UPDATE);
}

static void onOtaPromptNoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    otaPromptDecline();
}

static void onOtaPromptYesPressed(lv_event_t *e) {
    LV_UNUSED(e);
    otaPromptAccept();
}

static void openOtaUpdatePrompt() {
    if (!s_rootScreen || s_otaPromptModal || s_otaPromptBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 40;
    if (modalW < 160) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t versionBg = lightUi ? lv_color_hex(0xF5F9FF) : lv_color_hex(0x123266);
    const lv_color_t versionTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const lv_color_t hintColor = lightUi ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF);

    s_otaPromptBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_otaPromptBackdrop, w, h);
    lv_obj_align(s_otaPromptBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_otaPromptBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_otaPromptBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_otaPromptBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_otaPromptBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_otaPromptBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_otaPromptBackdrop, 0, 0);

    s_otaPromptModal = lv_obj_create(s_otaPromptBackdrop);
    lv_obj_set_size(s_otaPromptModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_otaPromptModal, (h > 40) ? (h - 12) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_otaPromptModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_otaPromptModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_otaPromptModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_otaPromptModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_otaPromptModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_otaPromptModal, 1, 0);
    lv_obj_set_style_border_color(s_otaPromptModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_otaPromptModal, 8, 0);
    lv_obj_set_style_pad_row(s_otaPromptModal, 7, 0);
    lv_obj_set_flex_flow(s_otaPromptModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_otaPromptModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_otaPromptBackdrop);

    lv_obj_t *title = lv_label_create(s_otaPromptModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Firmware Update"));

    // The version pair is the point of the dialog, so it gets its own boxed row.
    lv_obj_t *versionBox = lv_obj_create(s_otaPromptModal);
    lv_obj_set_width(versionBox, lv_pct(100));
    lv_obj_set_height(versionBox, LV_SIZE_CONTENT);
    lv_obj_clear_flag(versionBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(versionBox, versionBg, 0);
    lv_obj_set_style_bg_opa(versionBox, LV_OPA_60, 0);
    lv_obj_set_style_border_width(versionBox, 1, 0);
    lv_obj_set_style_border_color(versionBox, modalBorder, 0);
    lv_obj_set_style_pad_left(versionBox, 6, 0);
    lv_obj_set_style_pad_right(versionBox, 6, 0);
    lv_obj_set_style_pad_top(versionBox, 5, 0);
    lv_obj_set_style_pad_bottom(versionBox, 5, 0);

    lv_obj_t *versions = lv_label_create(versionBox);
    lv_obj_set_width(versions, lv_pct(100));
    lv_obj_set_style_text_font(versions, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(versions, versionTextColor, 0);
    lv_obj_set_style_text_align(versions, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(versions, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(versions, TR("%s -> %s"), APP_VERSION, s_otaAutoCheckTag);

    lv_obj_t *hint = lv_label_create(s_otaPromptModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, TR("Install now? The device reboots to update."));

    lv_obj_t *btnRow = lv_obj_create(s_otaPromptModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 12, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makeOtaBtn = [](lv_obj_t *parent, const char *text, uint32_t bgColor,
                         lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
    #if defined(DEVICE_TDECK_PRO)
        LV_UNUSED(bgColor);
        lv_obj_remove_style_all(btn);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    #endif
        lv_obj_set_height(btn, 32);
        lv_obj_set_style_min_width(btn, 80, 0);
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
    makeOtaBtn(btnRow, "No", lightUi ? 0xC76565 : 0x6B3030, onOtaPromptNoPressed);
    makeOtaBtn(btnRow, TR_NOOP("Yes"), lightUi ? 0x429A56 : 0x2F6B30, onOtaPromptYesPressed);
#else
    makeOtaBtn(btnRow, "(N)o", lightUi ? 0xC76565 : 0x6B3030, onOtaPromptNoPressed);
    makeOtaBtn(btnRow, "(Y)es", lightUi ? 0x429A56 : 0x2F6B30, onOtaPromptYesPressed);
#endif
}

#if HAS_STATE_MAPS
static constexpr uint8_t kLegacyMapMigrationVersion = 1;
static constexpr const char *kLegacyMapMigrationPref = "mapMigVer";

static bool legacyMapFilenameOwned(const char *filename, bool detailMap) {
    if (!filename || !filename[0]) return false;
    const char *base = filename;
    for (const char *cursor = filename; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    }

    if (detailMap) {
        if (strcmp(base, "detail_maps.version") == 0) return true;
        if (strlen(base) < 14) return false;
        char key[11] = {};
        memcpy(key, base, 10);
        key[10] = '\0';
        int lat10 = 0;
        int lon10 = 0;
        if (!nodesDetailMapParseKey(key, lat10, lon10)) return false;
        const char *suffix = base + 10;
        return strcmp(suffix, ".png") == 0
               || strcmp(suffix, ".meta") == 0
               || strcmp(suffix, ".png.tmp") == 0;
    }

    if (strcmp(base, "state_maps.complete") == 0 || strcmp(base, ".complete") == 0) {
        return true;
    }
    if (strlen(base) < 6) return false;
    char code[3] = {
        (char)toupper((unsigned char)base[0]),
        (char)toupper((unsigned char)base[1]),
        '\0',
    };
    bool knownState = false;
    for (int i = 0; i < kUsStateMapCount; i++) {
        if (strcmp(code, kUsStateMaps[i].code) == 0) {
            knownState = true;
            break;
        }
    }
    if (!knownState) return false;
    const char *suffix = base + 2;
    return strcmp(suffix, ".png") == 0
           || strcmp(suffix, ".meta") == 0
           || strcmp(suffix, ".png.tmp") == 0;
}

static bool legacyMapDirHasContent(const char *path, bool detailMap) {
#if !HAS_FILE_STORAGE
    LV_UNUSED(path);
    LV_UNUSED(detailMap);
    return false;
#else
    if (!path || !storageFs().exists(path)) return false;
    File dir = storageFs().open(path);
    if (!dir) return false;
    if (!dir.isDirectory()) {
        bool owned = legacyMapFilenameOwned(path, detailMap);
        dir.close();
        return owned;
    }
    bool hasContent = false;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        bool owned = !entry.isDirectory() && legacyMapFilenameOwned(entry.name(), detailMap);
        entry.close();
        if (owned) {
            hasContent = true;
            break;
        }
    }
    dir.close();
    return hasContent;
#endif
}

static int removeLegacyMapDirFiles(const char *path, bool detailMap) {
#if !HAS_FILE_STORAGE
    LV_UNUSED(path);
    LV_UNUSED(detailMap);
    return 0;
#else
    if (!path || !storageFs().exists(path)) return 0;
    File dir = storageFs().open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    int removed = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;
        String entryPath = entry.name();
        bool owned = !entry.isDirectory() && legacyMapFilenameOwned(entry.name(), detailMap);
        entry.close();
        if (!owned) continue;
        if (!entryPath.startsWith("/")) {
            String fullPath = path;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += entryPath;
            entryPath = fullPath;
        }
        if (storageFs().remove(entryPath.c_str())) removed++;
    }
    dir.close();
    storageFs().rmdir(path);
    return removed;
#endif
}

static bool legacyMapFilesExist() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!sdBegin()) return false;
        return legacyMapDirHasContent("/camillia/state_maps", false)
            || legacyMapDirHasContent("/camillia/detail_maps", true);
#endif
}

static bool legacyMapMigrationRecorded() {
    Preferences prefs;
    if (!prefs.begin("camillia", true)) return false;
    uint8_t version = prefs.getUChar(kLegacyMapMigrationPref, 0);
    prefs.end();
    return version >= kLegacyMapMigrationVersion;
}

static void recordLegacyMapMigration() {
    Preferences prefs;
    if (!prefs.begin("camillia", false)) return;
    prefs.putUChar(kLegacyMapMigrationPref, kLegacyMapMigrationVersion);
    prefs.end();
}

static void closeLegacyMapPrompt() {
    if (lvObjValid(s_legacyMapPromptBackdrop)) {
        lv_obj_del(s_legacyMapPromptBackdrop);
    } else if (lvObjValid(s_legacyMapPromptModal)) {
        lv_obj_del(s_legacyMapPromptModal);
    }
    s_legacyMapPromptBackdrop = nullptr;
    s_legacyMapPromptModal = nullptr;
    s_legacyMapPromptStatus = nullptr;
}

static void keepLegacyMapFiles() {
    Serial.println("[map-migration] keeping legacy map files");
    recordLegacyMapMigration();
    s_legacyMapMigrationChecked = true;
    closeLegacyMapPrompt();
}

static void removeLegacyMapFiles() {
    if (s_legacyMapPromptStatus && lvObjValid(s_legacyMapPromptStatus)) {
        lv_label_set_text(s_legacyMapPromptStatus, TR("Removing legacy maps..."));
        lv_obj_clear_flag(s_legacyMapPromptStatus, LV_OBJ_FLAG_HIDDEN);
        lv_timer_handler();
    }

    int removed = removeLegacyMapDirFiles("/camillia/state_maps", false)
                  + removeLegacyMapDirFiles("/camillia/detail_maps", true);
    if (legacyMapFilesExist()) {
        Serial.println("[map-migration] legacy map removal incomplete");
        if (s_legacyMapPromptStatus && lvObjValid(s_legacyMapPromptStatus)) {
            lv_label_set_text(s_legacyMapPromptStatus,
                              TR("Could not remove every file. Check storage and retry."));
        }
        return;
    }

    Serial.printf("[map-migration] removed %d legacy map files\n", removed);
    recordLegacyMapMigration();
    s_legacyMapMigrationChecked = true;
    closeLegacyMapPrompt();
}

static void onLegacyMapKeepPressed(lv_event_t *event) {
    LV_UNUSED(event);
    keepLegacyMapFiles();
}

static void onLegacyMapRemovePressed(lv_event_t *event) {
    LV_UNUSED(event);
    removeLegacyMapFiles();
}

static void openLegacyMapPrompt() {
    if (!s_rootScreen || s_legacyMapPromptModal || s_legacyMapPromptBackdrop) return;

    const int screenW = lv_disp_get_hor_res(nullptr);
    const int screenH = lv_disp_get_ver_res(nullptr);
    int modalW = screenW - 32;
    if (modalW < 180) modalW = screenW - 8;
    if (modalW > 300) modalW = 300;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t textColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xE8F1FF);
    const lv_color_t mutedColor = lightUi ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF);

    s_legacyMapPromptBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_legacyMapPromptBackdrop, screenW, screenH);
    lv_obj_align(s_legacyMapPromptBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_legacyMapPromptBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_legacyMapPromptBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_legacyMapPromptBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_legacyMapPromptBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_legacyMapPromptBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_legacyMapPromptBackdrop, 0, 0);

    s_legacyMapPromptModal = lv_obj_create(s_legacyMapPromptBackdrop);
    lv_obj_set_size(s_legacyMapPromptModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_legacyMapPromptModal,
                                (screenH > 40) ? (screenH - 12) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_legacyMapPromptModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_legacyMapPromptModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_legacyMapPromptModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_legacyMapPromptModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_legacyMapPromptModal, 1, 0);
    lv_obj_set_style_border_color(s_legacyMapPromptModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_legacyMapPromptModal, 8, 0);
    lv_obj_set_style_pad_row(s_legacyMapPromptModal, 7, 0);
    lv_obj_set_flex_flow(s_legacyMapPromptModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_legacyMapPromptModal, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_legacyMapPromptBackdrop);

    lv_obj_t *title = lv_label_create(s_legacyMapPromptModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, textColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Old Map Files Found"));

    lv_obj_t *body = lv_label_create(s_legacyMapPromptModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(body, textColor, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body,
                      TR("These files are not used by the new tile map. Remove them to reclaim storage?"));

    s_legacyMapPromptStatus = lv_label_create(s_legacyMapPromptModal);
    lv_obj_set_width(s_legacyMapPromptStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_legacyMapPromptStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_legacyMapPromptStatus, mutedColor, 0);
    lv_obj_set_style_text_align(s_legacyMapPromptStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_legacyMapPromptStatus, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_legacyMapPromptStatus, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *buttonRow = lv_obj_create(s_legacyMapPromptModal);
    lv_obj_set_width(buttonRow, lv_pct(100));
    lv_obj_set_height(buttonRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(buttonRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(buttonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttonRow, 0, 0);
    lv_obj_set_style_pad_all(buttonRow, 0, 0);
    lv_obj_set_style_pad_column(buttonRow, 10, 0);
    lv_obj_set_flex_flow(buttonRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonRow, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto makeButton = [](lv_obj_t *parent, const char *text, uint32_t color,
                         lv_event_cb_t callback) {
        lv_obj_t *button = lv_btn_create(parent);
    #if defined(DEVICE_TDECK_PRO)
        LV_UNUSED(color);
        lv_obj_remove_style_all(button);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    #endif
        lv_obj_set_height(button, 32);
        lv_obj_set_style_min_width(button, 86, 0);
        lv_obj_set_style_radius(button, 4, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    #endif
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(label, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    #endif
        lv_label_set_text(label, TR(text));
        lv_obj_center(label);
    };

#if UI_TOUCH_ONLY_PROFILE
    makeButton(buttonRow, "Keep", lightUi ? 0x55759D : 0x31527C, onLegacyMapKeepPressed);
    makeButton(buttonRow, TR_NOOP("Remove"), lightUi ? 0xC76565 : 0x6B3030, onLegacyMapRemovePressed);
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_legacyMapPromptModal, onLegacyMapKeepPressed);
#else
    makeButton(buttonRow, "(K)eep", lightUi ? 0x55759D : 0x31527C, onLegacyMapKeepPressed);
    makeButton(buttonRow, "(R)emove", lightUi ? 0xC76565 : 0x6B3030,
               onLegacyMapRemovePressed);
#endif
}

static void serviceLegacyMapMigrationPrompt(uint32_t nowMs) {
    if (s_legacyMapMigrationChecked || s_legacyMapPromptModal) return;
    if ((int32_t)(nowMs - s_legacyMapMigrationNextCheckMs) < 0) return;
    if (!s_rootScreen || s_firstBoot || s_onboardingModal || s_otaPromptModal
        || s_cfgConfirmModal || s_cfgActionMsgModal) {
        return;
    }
    if (legacyMapMigrationRecorded()) {
        s_legacyMapMigrationChecked = true;
        return;
    }
#if HAS_FILE_STORAGE
    if (!sdBegin()) {
        // Bounded, because sdBegin() does not cache a failure: every retry
        // re-runs the whole mount sequence, which on a card-less device is two
        // ~1 s f_mount attempts that block the main loop. Retrying at a fixed
        // 10 s interval therefore cost a ~2 s stall every 10 s for the entire
        // uptime, forever, on any SD-capable board with no card in it. In the
        // field that showed up as a recurring `map:migrate=2031ms` in the loop
        // report and an endless stream of "[sd] not found".
        //
        // A few spaced attempts still cover a card that is slow to settle after
        // boot; past that this gives up for the rest of the boot. Legacy map
        // migration is a one-time upgrade concern, so deferring it to the next
        // reboot costs a user who has a card nothing, and hands a user who does
        // not have one their main loop back.
        static constexpr int kLegacyMapProbeMax = 3;
        static int sProbes = 0;
        if (++sProbes >= kLegacyMapProbeMax) {
            s_legacyMapMigrationChecked = true;
            Serial.printf("[map] no storage after %d probes; skipping the legacy "
                          "map check for this boot\n", sProbes);
            return;
        }
        // 10 s, then 20 s. Doubling keeps the total stall bounded at ~6 s
        // rather than growing with uptime.
        s_legacyMapMigrationNextCheckMs = nowMs + (10000UL << (sProbes - 1));
        return;
    }
#endif
    if (!legacyMapFilesExist()) {
        s_legacyMapMigrationChecked = true;
        return;
    }
    openLegacyMapPrompt();
}
#endif

// ── First-boot onboarding ─────────────────────────────────────────────────
// Shown once from setup() on freshly flashed devices. If a config exists on
// the SD card the user is offered an import; otherwise (or if declined) we
// prompt for a node name and persist it. Blocks all other UI input while up.

static void onboardingDeriveShortFromLong(const char *longName, char *shortOut, size_t shortLen) {
    if (!shortOut || shortLen == 0) return;
    shortOut[0] = '\0';
    if (!longName) return;
    size_t o = 0;
    const size_t cap = shortLen - 1;
    for (const char *p = longName; *p && o < cap; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c > 0x20 && c < 0x7F) shortOut[o++] = (char)c;
    }
    shortOut[o] = '\0';
    if (o == 0) {
        utf8util::copyTruncate(shortOut, shortLen, MY_SHORT_NAME);
    }
}

static void onboardingSetStatus(const char *msg) {
    if (!s_onboardingStatus) return;
    lv_label_set_text(s_onboardingStatus, msg ? msg : "");
}

#if defined(DEVICE_TDECK_PRO)
static void styleOnboardingPaperTree(lv_obj_t *parent) {
    if (!parent) return;
    const uint32_t childCount = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < childCount; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
        if (!child) continue;
        lv_obj_set_style_text_color(child, lv_color_make(0, 0, 0), 0);
        if (lv_obj_check_type(child, &lv_button_class)) {
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, LV_STATE_PRESSED);
            lv_obj_set_style_border_width(child, 2, 0);
            lv_obj_set_style_border_width(child, 2, LV_STATE_PRESSED);
            lv_obj_set_style_border_color(child, lv_color_make(0, 0, 0), 0);
            lv_obj_set_style_border_color(child, lv_color_make(0, 0, 0), LV_STATE_PRESSED);
            lv_obj_set_style_shadow_width(child, 0, 0);
        }
        styleOnboardingPaperTree(child);
    }
}
#endif

// Roles offered during onboarding (see cfgCoerceDeviceRole for which roles this
// firmware supports and why the infrastructure ones are absent); values are the
// canonical Meshtastic enum positions so they stay wire-compatible.
//
// Ordered by enum value rather than by likelihood, so the list reads the same
// way the web config's does and the same way Meshtastic's own documentation
// lists them.
struct OnboardRoleOption { uint8_t value; const char *label; };
static const OnboardRoleOption kOnboardRoles[] = {
    {0, "CLIENT"},
    {1, "CLIENT_MUTE"},
    {5, "TRACKER"},
    {8, "CLIENT_HIDDEN"},
#if UI_OFFER_CLIENT_BASE
    {12, "CLIENT_BASE"},
#endif
};
static const int kOnboardRoleCount =
    (int)(sizeof(kOnboardRoles) / sizeof(kOnboardRoles[0]));

// The regions a picker may offer: kRegions less what regionSelectable() refuses
// (amateur bands unless licensed). Picker indexes count only these, so index and
// code are always mapped through here, never through kRegions directly.
static int regionPickCount() {
    const bool licensed = lic::active();
    int n = 0;
    for (uint8_t i = 0; i < kRegionCount; i++) {
        if (regionSelectable(kRegions[i].code, licensed)) n++;
    }
    return n;
}

static const RegionPlan *regionPickAt(int idx) {
    const bool licensed = lic::active();
    int n = 0;
    for (uint8_t i = 0; i < kRegionCount; i++) {
        if (!regionSelectable(kRegions[i].code, licensed)) continue;
        if (n++ == idx) return &kRegions[i];
    }
    return &kRegions[0];  // "US", always selectable
}

// Map a region code / role value back to its option index, falling back to the
// onboarding default (US / CLIENT_MUTE) when the value isn't recognised.
static int onboardingRegionIndex(const char *code) {
    if (code && code[0]) {
        const int count = regionPickCount();
        for (int i = 0; i < count; i++) {
            if (strcmp(regionPickAt(i)->code, code) == 0) return i;
        }
    }
    return 0;  // the first selectable region is "US"
}

static int onboardingRoleIndex(uint8_t value) {
    for (int i = 0; i < kOnboardRoleCount; i++) {
        if (kOnboardRoles[i].value == value) return i;
    }
    return 1;  // CLIENT_MUTE
}

// Count of options on the active picker stage.
static int onboardingPickerCount() {
    return (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION)
               ? regionPickCount()
               : kOnboardRoleCount;
}

// Text label for the currently selected picker option.
static const char *onboardingPickerCurrentText() {
    if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION) {
        return regionPickAt(s_onboardingPickIndex)->code;
    }
    return kOnboardRoles[s_onboardingPickIndex].label;
}

// The region stage's key hint, kept so the ham-band warning can stand in for it
// while an amateur region is showing and give it back after.
static char s_onboardingPickHint[96] = {};

static void onboardingRegionWarnUpdate() {
    if (s_onboardingStage != ONBOARD_STAGE_SELECT_REGION || !s_onboardingStatus) return;
    const bool ham = regionPickAt(s_onboardingPickIndex)->licensedOnly;
    lv_label_set_text(s_onboardingStatus,
                      ham ? TR("Needs an antenna and front end for this band.")
                          : s_onboardingPickHint);
}

static void onboardingComputeModalSizeForStage(uint8_t stage, int screenW, int screenH,
                                               int &modalW, int &modalH) {
    const bool textEntry = stage == ONBOARD_STAGE_ENTER_LONG
                       || stage == ONBOARD_STAGE_ENTER_SHORT;
    modalW = screenW - 20;
    if (modalW < 180) modalW = screenW - 4;
    if (modalW > 320) modalW = 320;

    modalH = screenH - 20;
    if (modalH < 140) modalH = screenH - 4;

#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    if (textEntry) modalW = screenW;
#endif

#if UI_LARGE_PANEL_PROFILE
    modalW = screenW - 32;
    const int maxModalW = textEntry && !uiPortrait() ? 900 : 536;
    if (modalW > maxModalW) modalW = maxModalW;
    modalH = screenH - 32;
    if (modalH > 860) modalH = 860;
#endif

#if UI_CARDPUTER_COMPACT
    // Derived from the panel, not capped: at 240x135 this is 228/226 x 91,
    // the ADV's long-standing size, and on the 320x170 Zero it takes the
    // extra room instead of leaving a 240-px box in the middle.
    modalW = screenW - (stage == ONBOARD_STAGE_ASK_IMPORT ? 12 : 14);
    if (modalW < 180) modalW = screenW - 4;

    modalH = screenH - 44;
    if (modalH < 84) modalH = screenH - 6;
#endif

    if (!textEntry) modalH = LV_SIZE_CONTENT;
}

#if ONBOARD_BUTTON_FOCUS
static void onboardingRegisterBtn(lv_obj_t *btn, lv_event_cb_t cb) {
    if (!btn || s_onboardingBtnCount >= kOnboardingMaxBtns) return;
    s_onboardingBtns[s_onboardingBtnCount] = btn;
    s_onboardingBtnCbs[s_onboardingBtnCount] = cb;
    s_onboardingBtnCount++;
}

static void onboardingRefreshBtnFocus() {
    for (int i = 0; i < s_onboardingBtnCount; i++) {
        lv_obj_t *btn = s_onboardingBtns[i];
        if (!lvObjValid(btn)) continue;
        const bool focused = (i == s_onboardingBtnFocus);
        lv_obj_set_style_border_width(btn, focused ? 3 : 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    }
}

// Left/Right move the focus, Enter presses. False when the key is not one of
// those (or the stage has no buttons), so the stage's own keys still run.
static bool onboardingButtonFocusKey(char k) {
    if (s_onboardingBtnCount <= 0) return false;
    if (k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN || k == KEY_TAB) {
        const int step = (k == KEY_PREV_CHAN) ? -1 : 1;
        s_onboardingBtnFocus = (s_onboardingBtnFocus + step + s_onboardingBtnCount)
                               % s_onboardingBtnCount;
        onboardingRefreshBtnFocus();
        return true;
    }
    if (k == KEY_ENTER) {
        const int idx = constrain(s_onboardingBtnFocus, 0, s_onboardingBtnCount - 1);
        lv_event_cb_t cb = s_onboardingBtnCbs[idx];
        // Called directly rather than through an LVGL event: most of these
        // re-render the stage, which deletes the button they belong to.
        if (cb) cb(nullptr);
        return true;
    }
    return false;
}
#endif

static void renderOnboardingStage() {
    if (!s_onboardingModal) return;

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    int modalW = 0;
    int modalH = 0;
    onboardingComputeModalSizeForStage(s_onboardingStage, screenW, screenH, modalW, modalH);
    const bool contentSized = (modalH == LV_SIZE_CONTENT);
    lv_obj_set_size(s_onboardingModal, modalW, modalH);
    lv_obj_set_style_max_height(s_onboardingModal, screenH - 4, 0);
    lv_obj_align(s_onboardingModal, LV_ALIGN_CENTER, 0, 0);
    if (contentSized) {
        lv_obj_add_flag(s_onboardingModal, LV_OBJ_FLAG_SCROLLABLE);
        setupVScroll(s_onboardingModal);
        lv_obj_set_scrollbar_mode(s_onboardingModal, LV_SCROLLBAR_MODE_AUTO);
    } else {
        lv_obj_clear_flag(s_onboardingModal, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_flex_align(s_onboardingModal,
#if defined(DEVICE_TDISPLAY_P4)
                          contentSized ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
#else
                          LV_FLEX_ALIGN_START,
#endif
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

#if UI_CARDPUTER_COMPACT
    const bool compactOnboarding = true;
    const bool compactImportStage = (s_onboardingStage == ONBOARD_STAGE_ASK_IMPORT);
#else
    const bool compactOnboarding = false;
    const bool compactImportStage = false;
#endif

#if UI_LARGE_PANEL_PROFILE
    const bool largeOnboarding = true;
#else
    const bool largeOnboarding = false;
#endif

    lv_obj_set_style_pad_all(s_onboardingModal,
                             largeOnboarding ? 16
                               : (compactImportStage ? 6 : (compactOnboarding ? 8 : 10)),
                             0);
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    if (!contentSized) {
        lv_obj_set_style_pad_left(s_onboardingModal, 2, 0);
        lv_obj_set_style_pad_right(s_onboardingModal, 2, 0);
    }
#endif
    lv_obj_set_style_pad_row(s_onboardingModal,
                                                         largeOnboarding ? 14
                                                             : (compactImportStage ? 4 : (compactOnboarding ? 6 : 8)),
                             0);

    // Wipe children and rebuild for the current stage. Keep the modal
    // container itself so its geometry/layout stays consistent.
    lv_obj_clean(s_onboardingModal);
    s_onboardingInput = nullptr;
    s_onboardingKeyboard = nullptr;
    s_onboardingStatus = nullptr;
    s_onboardingPickLabel = nullptr;
#if ONBOARD_BUTTON_FOCUS
    s_onboardingBtnCount = 0;
#endif

    const lv_font_t *onboardingBodyFont =
                largeOnboarding ? &lv_font_montserrat_24
                    : (compactOnboarding ? &lv_font_montserrat_10 : &lv_font_montserrat_12);
    const lv_font_t *onboardingPickerFont =
                largeOnboarding ? &lv_font_montserrat_32
                    : (compactOnboarding ? &lv_font_montserrat_12 : &lv_font_montserrat_16);
    const lv_font_t *onboardingInputFont =
                largeOnboarding ? &lv_font_montserrat_24
                    : (compactOnboarding ? &lv_font_montserrat_12 : &lv_font_montserrat_14);
        const lv_font_t *onboardingStatusFont =
                largeOnboarding ? &lv_font_montserrat_18 : &lv_font_montserrat_10;
#if !UI_CARDPUTER_COMPACT
    const lv_font_t *onboardingButtonFont =
                largeOnboarding ? &lv_font_montserrat_18
                    : (compactOnboarding ? &lv_font_montserrat_10 : &lv_font_montserrat_12);
        const int onboardingButtonH = largeOnboarding ? 56 : (compactOnboarding ? 30 : 36);
        const int onboardingButtonMinW = largeOnboarding ? 132 : (compactOnboarding ? 86 : 100);
        const int onboardingStepBtnW = largeOnboarding ? 60 : (compactOnboarding ? 34 : 40);
        const int onboardingStepBtnH = largeOnboarding ? 56 : (compactOnboarding ? 30 : 36);
#endif
        const int onboardingInputH = largeOnboarding ? 60 : (compactOnboarding ? 30 : 34);

#if !UI_CARDPUTER_COMPACT
    const lv_font_t *onboardingTitleFont =
                largeOnboarding ? &lv_font_montserrat_28
                    : (compactOnboarding ? &lv_font_montserrat_12 : &lv_font_montserrat_16);
    lv_obj_t *title = lv_label_create(s_onboardingModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, onboardingTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Welcome to Camillia for Meshtastic"));
#endif

    lv_obj_t *body = lv_label_create(s_onboardingModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, onboardingBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    if (s_onboardingStage == ONBOARD_STAGE_ASK_IMPORT) {
        lv_label_set_text(body,
                          "A saved config was found on SD.\n"
                          "Import it now?");

#if UI_CARDPUTER_COMPACT
        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, onboardingStatusFont, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_onboardingStatus, TR("Y/Enter=Import   N/Bksp=Skip"));
#else

        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow,
                        compactImportStage ? 8 : (compactOnboarding ? 10 : 14),
                        0);
        lv_obj_set_style_pad_row(btnRow, largeOnboarding ? 14 : 8, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        const int importBtnH = compactImportStage ? 30 : onboardingButtonH;
        const int importBtnMinW = compactImportStage ? 72 : (compactOnboarding ? 80 : 84);
        const lv_font_t *importBtnFont = onboardingButtonFont;

        auto makeBtn = [](lv_obj_t *parent, const char *text, uint32_t color,
                  lv_event_cb_t cb,
                  int height,
                  int minWidth,
                  const lv_font_t *font) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, height);
            lv_obj_set_style_min_width(btn, minWidth, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
#if ONBOARD_BUTTON_FOCUS
            onboardingRegisterBtn(btn, cb);
#endif
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, font, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, TR(text));
            lv_obj_center(lbl);
            return btn;
        };
#if UI_TOUCH_ONLY_PROFILE
        makeBtn(btnRow, "No",  0x6B3030,
            [](lv_event_t *) { onboardingDeclineImport(); },
            importBtnH, importBtnMinW, importBtnFont);
        makeBtn(btnRow, TR_NOOP("Yes"), 0x2F6B30,
            [](lv_event_t *) { onboardingAcceptImport(); },
            importBtnH, importBtnMinW, importBtnFont);
#else
        makeBtn(btnRow, "(N)o",  0x6B3030,
            [](lv_event_t *) { onboardingDeclineImport(); },
            importBtnH, importBtnMinW, importBtnFont);
        makeBtn(btnRow, "(Y)es", 0x2F6B30,
            [](lv_event_t *) { onboardingAcceptImport(); },
            importBtnH, importBtnMinW, importBtnFont);
#endif
#if ONBOARD_BUTTON_FOCUS
        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, onboardingStatusFont, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_onboardingStatus,
                          TR("Left/Right = Select   Enter = Press   Y = Import   N = Skip"));
#endif
    #endif
    } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION
               || s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
        // Region / role picker: a single value shown between < / > steppers so
        // touch users can change it, while the roller / j-k cycle it and Enter
        // advances. No text entry on these stages.
        const bool isRegion = (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION);
        lv_label_set_text(body,
                          isRegion ? TR("Select your radio region/preset.")
                                   : TR("Select this node's role."));

        lv_obj_t *pickRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(pickRow, lv_pct(100));
        lv_obj_set_height(pickRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(pickRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(pickRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pickRow, 0, 0);
        lv_obj_set_style_pad_all(pickRow, 0, 0);
        lv_obj_set_style_pad_column(pickRow, compactOnboarding ? 6 : 10, 0);
        lv_obj_set_flex_flow(pickRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pickRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

#if !UI_CARDPUTER_COMPACT
        auto makeStepBtn = [=](lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_size(btn, onboardingStepBtnW, onboardingStepBtnH);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3C4A66), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingPickerFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
            return btn;
        };
        makeStepBtn(pickRow, LV_SYMBOL_LEFT,
                    [](lv_event_t *) { onboardingPickerStep(-1); });
        #endif

        s_onboardingPickLabel = lv_label_create(pickRow);
            lv_obj_set_width(s_onboardingPickLabel, lv_pct(100));
        #if !UI_CARDPUTER_COMPACT
            lv_obj_set_flex_grow(s_onboardingPickLabel, 1);
        #endif
        lv_obj_set_style_text_font(s_onboardingPickLabel, onboardingPickerFont, 0);
        lv_obj_set_style_text_color(s_onboardingPickLabel, lv_color_hex(0xE8F1FF), 0);
        lv_obj_set_style_text_align(s_onboardingPickLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_onboardingPickLabel, onboardingPickerCurrentText());

        #if !UI_CARDPUTER_COMPACT
        makeStepBtn(pickRow, LV_SYMBOL_RIGHT,
                    [](lv_event_t *) { onboardingPickerStep(1); });
        #endif

        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, onboardingStatusFont, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
        #if UI_CARDPUTER_COMPACT
            lv_label_set_text(s_onboardingStatus, TR("j/k=Change   Enter=Next   Bksp=Back"));
        #elif UI_TOUCH_ONLY_PROFILE
        lv_label_set_text(s_onboardingStatus, TR("Use arrows to choose, then tap Next"));
        #elif ONBOARD_BUTTON_FOCUS
        lv_label_set_text(s_onboardingStatus,
                          TR("Up/Down = Change   Left/Right = Select   Enter = Press   Esc = Back"));
        #else
        lv_label_set_text(s_onboardingStatus, TR("Wheel/j-k=Change   Enter=Next"));
        #endif
        snprintf(s_onboardingPickHint, sizeof(s_onboardingPickHint), "%s",
                 lv_label_get_text(s_onboardingStatus));
        onboardingRegionWarnUpdate();

        #if !UI_CARDPUTER_COMPACT
        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, compactOnboarding ? 10 : 14, 0);
        lv_obj_set_style_pad_row(btnRow, largeOnboarding ? 14 : 8, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        auto makeBtn = [=](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, onboardingButtonH);
            lv_obj_set_style_min_width(btn, onboardingButtonMinW, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
#if ONBOARD_BUTTON_FOCUS
            onboardingRegisterBtn(btn, cb);
#endif
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingButtonFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, TR(text));
            lv_obj_center(lbl);
            return btn;
        };
        makeBtn(btnRow, TR_NOOP("Back"), 0x3C4A66,
                [](lv_event_t *) { onboardingPickerBack(); });
        makeBtn(btnRow, TR_NOOP("Next"), 0x2F6B30,
                [](lv_event_t *) { onboardingPickerAdvance(); });
    #endif
    } else if (s_onboardingStage == ONBOARD_STAGE_CHOOSE_WIFI) {
        char summary[160];
        if (s_onboardingWifiSsidScratch[0]) {
            snprintf(summary,
                     sizeof(summary),
                     TR("Optional WiFi setup.\nSelected: %s\nChoose a different network or finish."),
                     s_onboardingWifiSsidScratch);
        } else {
            snprintf(summary,
                     sizeof(summary),
                     TR("Optional WiFi setup.\nNo network selected.\nChoose WiFi now or finish without WiFi."));
        }
        lv_label_set_text(body, summary);

        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, onboardingStatusFont, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
#if UI_CARDPUTER_COMPACT
        lv_label_set_text(s_onboardingStatus, TR("N=Choose WiFi   Enter=Finish   Bksp=Back"));
#elif ONBOARD_BUTTON_FOCUS
        lv_label_set_text(s_onboardingStatus,
                          TR("Left/Right = Select   Enter = Press   W = WiFi   F = Finish   Esc = Back"));
#else
        lv_label_set_text(s_onboardingStatus, TR("Choose WiFi or Finish"));
#endif

#if !UI_CARDPUTER_COMPACT
        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, compactOnboarding ? 8 : 10, 0);
        lv_obj_set_style_pad_row(btnRow, largeOnboarding ? 14 : 8, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        auto makeBtn = [=](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, onboardingButtonH);
            lv_obj_set_style_min_width(btn, onboardingButtonMinW, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
#if ONBOARD_BUTTON_FOCUS
            onboardingRegisterBtn(btn, cb);
#endif
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingButtonFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, TR(text));
            lv_obj_center(lbl);
            return btn;
        };

        makeBtn(btnRow, TR_NOOP("Back"), 0x3C4A66,
                [](lv_event_t *) {
                    s_onboardingStage = ONBOARD_STAGE_SELECT_ROLE;
                    s_onboardingPickIndex = onboardingRoleIndex(s_onboardingRoleScratch);
                    renderOnboardingStage();
                });
#if ONBOARD_BUTTON_FOCUS
        makeBtn(btnRow, TR_NOOP("Choose (W)iFi"), 0x3C4A66,
                [](lv_event_t *) { openCfgWifiPickerModal(true); });
        makeBtn(btnRow, TR_NOOP("(F)inish"), 0x2F6B30,
                [](lv_event_t *) { onboardingFinalize(); });
#else
        makeBtn(btnRow, TR_NOOP("Choose WiFi"), 0x3C4A66,
                [](lv_event_t *) { openCfgWifiPickerModal(true); });
        makeBtn(btnRow, TR_NOOP("Finish"), 0x2F6B30,
                [](lv_event_t *) { onboardingFinalize(); });
#endif
#endif
    } else {
        const bool isShortStage = (s_onboardingStage == ONBOARD_STAGE_ENTER_SHORT);

        if (isShortStage) {
            char summary[96];
            snprintf(summary, sizeof(summary),
                     TR("Long name: %s\nEnter a short name (up to 4 chars)."),
                     s_onboardingLongScratch);
            lv_label_set_text(body, summary);
        } else {
            lv_label_set_text(body, TR("Enter this node's long name."));
        }

        s_onboardingInput = lv_textarea_create(s_onboardingModal);
        if (!s_onboardingInput) {
            logLvglMemDiag("onboarding input aborted (low LVGL mem)");
            closeOnboardingModal();
            return;
        }
        lv_obj_set_width(s_onboardingInput, lv_pct(100));
        lv_obj_set_height(s_onboardingInput, onboardingInputH);
        lv_obj_set_style_text_font(s_onboardingInput, onboardingInputFont, 0);
        lv_obj_set_style_text_color(s_onboardingInput, lv_color_hex(0xE8F1FF), 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_color(s_onboardingInput, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_border_color(s_onboardingInput, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(s_onboardingInput, lv_color_hex(0x102B61), 0);
        lv_obj_set_style_border_color(s_onboardingInput, lv_color_hex(0x4C76BA), 0);
    #endif
        lv_obj_set_style_bg_opa(s_onboardingInput, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_onboardingInput, 1, 0);
        lv_textarea_set_one_line(s_onboardingInput, true);
        accentAttach(s_onboardingInput);
        if (isShortStage) {
            char derived[sizeof(s_cfg.nodeShort)];
            onboardingDeriveShortFromLong(s_onboardingLongScratch, derived, sizeof(derived));
            lv_textarea_set_max_length(s_onboardingInput, sizeof(s_cfg.nodeShort) - 1);
            lv_textarea_set_placeholder_text(s_onboardingInput, TR("Short name"));
            lv_textarea_set_text(s_onboardingInput,
                                 s_onboardingShortScratch[0] ? s_onboardingShortScratch
                                                             : derived);
        } else {
            // LVGL counts characters, not bytes, so this is a UI hint only; the
            // byte cap lands in copyTruncate when the field is committed.
            lv_textarea_set_max_length(s_onboardingInput, MESH_LONG_NAME_MAX_BYTES);
            lv_textarea_set_placeholder_text(s_onboardingInput, TR("Long name"));
            lv_textarea_set_text(s_onboardingInput, s_onboardingLongScratch);
        }
        lv_textarea_set_cursor_pos(s_onboardingInput, LV_TEXTAREA_CURSOR_LAST);

        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, onboardingStatusFont, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
    #if UI_CARDPUTER_COMPACT
        const char *statusHint = "Enter=Next";
        if (isShortStage)      statusHint = TR("Enter=Next   Bksp(empty)=Back");
    #elif UI_TOUCH_ONLY_PROFILE
        const char *statusHint = isShortStage
                                     ? TR("Tap Next to continue, or Back")
                                     : TR("Tap Next to continue");
    #elif ONBOARD_BUTTON_FOCUS
        const char *statusHint = isShortStage
                                     ? TR("Type a name   Left/Right = Select   Enter = Press   Esc = Back")
                                     : TR("Type a name   Enter = Next");
    #else
        const char *statusHint = TR("Enter=Next");
        if (isShortStage)      statusHint = TR("Enter=Next    Bksp(empty)=Back");
    #endif
        lv_label_set_text(s_onboardingStatus, statusHint);

    #if !UI_CARDPUTER_COMPACT
        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, compactOnboarding ? 10 : 14, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        auto makeBtn = [=](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, onboardingButtonH);
            lv_obj_set_style_min_width(btn, onboardingButtonMinW, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
#if ONBOARD_BUTTON_FOCUS
            onboardingRegisterBtn(btn, cb);
#endif
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingButtonFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, TR(text));
            lv_obj_center(lbl);
            return btn;
        };

        if (isShortStage) {
            makeBtn(btnRow, TR_NOOP("Back"), 0x3C4A66,
                    [](lv_event_t *) {
                        s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
                        renderOnboardingStage();
                    });
        }
        const char *nextLabel = TR_NOOP("Next");
        makeBtn(btnRow, nextLabel, 0x2F6B30,
                [](lv_event_t *) { onboardingCommitName(); });
    #endif

#if UI_TOUCH_ONLY_PROFILE
        // Not with a physical keyboard attached (the T-Display P4's expansion):
        // the name is typed on it, through pumpKeyboardInput().
        if (!keyboardAttached()) {
        s_onboardingKeyboard = lv_keyboard_create(s_onboardingModal);
        configureOnScreenKeyboard(s_onboardingKeyboard);
#if defined(DEVICE_TDISPLAY_P4)
        // configureOnScreenKeyboard() put a growing spacer above the keyboard
        // to hold it on the bottom edge, and that spacer takes every free pixel
        // -- so centring the column has nothing left to centre with. A second
        // one at the top shares the space with it equally: the title, input and
        // buttons land centred in what the keyboard leaves, and it stays put.
        {
            lv_obj_t *topSpacer = lv_obj_create(s_onboardingModal);
            lv_obj_remove_style_all(topSpacer);
            lv_obj_clear_flag(topSpacer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE
                                                         | LV_OBJ_FLAG_SCROLLABLE));
            lv_obj_set_size(topSpacer, 1, 0);
            lv_obj_set_flex_grow(topSpacer, 1);
            lv_obj_move_to_index(topSpacer, 0);
        }
#endif
        lv_keyboard_set_mode(s_onboardingKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_onboardingKeyboard, s_onboardingInput);
        lv_obj_add_event_cb(s_onboardingKeyboard,
                            [](lv_event_t *e) {
                                lv_event_code_t c = lv_event_get_code(e);
                                if (c == LV_EVENT_READY) onboardingCommitName();
                            },
                            LV_EVENT_READY, nullptr);
        }   // !keyboardAttached()
#endif
    }

#if defined(DEVICE_TDECK_PRO)
    styleOnboardingPaperTree(s_onboardingModal);
#endif
#if ONBOARD_BUTTON_FOCUS
    s_onboardingBtnFocus = s_onboardingBtnCount > 0 ? s_onboardingBtnCount - 1 : 0;
    onboardingRefreshBtnFocus();
#endif
}

static void openOnboardingModal() {
    if (!s_rootScreen || s_onboardingModal || s_onboardingBackdrop) return;

    s_onboardingSdConfigPresent = cfgSdConfigExists();
    s_onboardingStage = s_onboardingSdConfigPresent
                            ? ONBOARD_STAGE_ASK_IMPORT
                            : ONBOARD_STAGE_ENTER_LONG;
    s_onboardingLongScratch[0] = '\0';
    s_onboardingShortScratch[0] = '\0';
    // Onboarding defaults: US region, CLIENT_MUTE role, no WiFi.
    utf8util::copyTruncate(s_onboardingRegionScratch, sizeof(s_onboardingRegionScratch),
                           MY_REGION);
    s_onboardingRoleScratch = 1;  // CLIENT_MUTE
    s_onboardingWifiSsidScratch[0] = '\0';
    s_onboardingWifiPassScratch[0] = '\0';
    s_onboardingPickIndex = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = 0;
    int modalH = 0;
    onboardingComputeModalSizeForStage(s_onboardingStage, w, h, modalW, modalH);

    s_onboardingBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_onboardingBackdrop, w, h);
    lv_obj_align(s_onboardingBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_onboardingBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_onboardingBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_onboardingBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_onboardingBackdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_onboardingBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_onboardingBackdrop, 0, 0);

    s_onboardingModal = lv_obj_create(s_onboardingBackdrop);
    lv_obj_set_size(s_onboardingModal, modalW, modalH);
    lv_obj_align(s_onboardingModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_onboardingModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_onboardingModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_onboardingModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_onboardingModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_onboardingModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_onboardingModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_onboardingModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_onboardingModal, 1, 0);
    lv_obj_set_style_pad_all(s_onboardingModal, 10, 0);
    lv_obj_set_style_pad_row(s_onboardingModal, 8, 0);
    lv_obj_set_flex_flow(s_onboardingModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_move_foreground(s_onboardingBackdrop);

    renderOnboardingStage();
}

static void closeOnboardingModal() {
    if (lvObjValid(s_onboardingBackdrop)) {
        lv_obj_del(s_onboardingBackdrop);
    } else if (lvObjValid(s_onboardingModal)) {
        lv_obj_del(s_onboardingModal);
    }
    s_onboardingBackdrop = nullptr;
    s_onboardingModal = nullptr;
    s_onboardingInput = nullptr;
    s_onboardingKeyboard = nullptr;
    s_onboardingStatus = nullptr;
    s_onboardingPickLabel = nullptr;

    // Web config came up during setup and paused chat; now there is a keyboard
    // free to dismiss the notice with.
    if (s_webCfgChatPausedNoticePending) {
        s_webCfgChatPausedNoticePending = false;
        showWebCfgChatPausedNotice();
    }
}

static void onboardingAcceptImport() {
    Serial.println("[onboarding] importing SD config");
    bool ok = cfgImport(s_cfg);
    if (!ok) {
        Serial.println("[onboarding] import failed; falling back to name entry");
        s_onboardingSdConfigPresent = false;
        s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
        renderOnboardingStage();
        onboardingSetStatus(TR("Import failed - enter a name"));
        return;
    }
    persistConfigToPrefs();
#if FEATURE_LICENSED_MODE
    // A licensed config restores keyless, as boot would make it anyway; said
    // here so the missing keys are not a surprise.
    if (s_cfg.licensedMode && licStripChannels(0)) {
        onboardingSetStatus(TR("Licensed mode: channel imported without its key"));
        lv_timer_handler();
        delay(3000);
    }
#endif
    // cfgImport() also populated the CHANNEL_KEYS[] plan, but persistConfigToPrefs()
    // only saves the RhinoConfig fields — not the channel plan. Persist the
    // channels too (mirroring onWebCfgSaved) so imported channels survive the
    // reboot; otherwise the user had to re-import after boot to restore them.
    syncPrimaryChannelName();
    persistChannelsToPrefs();
    // And the identity keypair, for the same reason. This device has none of
    // its own yet, so without this the reboot below mints a new one and the
    // restore quietly loses the node's identity.
    persistImportedIdentityKeys("onboarding");
    Serial.println("[onboarding] imported OK - rebooting");
    const char *importWarn = cfgImportWarning();
    const bool hasImportWarn = importWarn && importWarn[0];
    if (hasImportWarn) onboardingSetStatus(importWarn);
    lv_timer_handler();
    delay(hasImportWarn ? 4000 : 500);
    flushPersistentState();   // settings and transcripts must land before we go
    ESP.restart();
}

static void onboardingDeclineImport() {
    s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
    renderOnboardingStage();
}

// Advances onboarding text-entry stages (long/short name).
static void onboardingCommitName() {
    if (!s_onboardingInput) return;
    const char *typed = lv_textarea_get_text(s_onboardingInput);
    String name = typed ? String(typed) : String();
    name.trim();

    switch (s_onboardingStage) {
    case ONBOARD_STAGE_ENTER_LONG:
        if (name.length() == 0) {
            onboardingSetStatus(TR("Long name cannot be empty"));
            return;
        }
        utf8util::copyTruncate(s_onboardingLongScratch, MESH_LONG_NAME_MAX_BYTES + 1,
                               name.c_str());
        s_onboardingStage = ONBOARD_STAGE_ENTER_SHORT;
        renderOnboardingStage();
        return;

    case ONBOARD_STAGE_ENTER_SHORT:
        if (name.length() == 0) {
            onboardingSetStatus(TR("Short name cannot be empty"));
            return;
        }
        utf8util::copyTruncate(s_onboardingShortScratch, sizeof(s_onboardingShortScratch),
                               name.c_str());
        s_onboardingStage = ONBOARD_STAGE_SELECT_REGION;
        s_onboardingPickIndex = onboardingRegionIndex(s_onboardingRegionScratch);
        renderOnboardingStage();
        return;

    default:
        return;
    }
}

// Cycle the current region/role picker option by delta (wraps around).
static void onboardingPickerStep(int delta) {
    if (s_onboardingStage != ONBOARD_STAGE_SELECT_REGION
        && s_onboardingStage != ONBOARD_STAGE_SELECT_ROLE) {
        return;
    }
    const int count = onboardingPickerCount();
    if (count <= 0) return;
    s_onboardingPickIndex = ((s_onboardingPickIndex + delta) % count + count) % count;
    if (s_onboardingPickLabel) {
        lv_label_set_text(s_onboardingPickLabel, onboardingPickerCurrentText());
    }
    onboardingRegionWarnUpdate();
}

// Commit the current picker selection and move to the next stage.
static void onboardingPickerAdvance() {
    if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION) {
        utf8util::copyTruncate(s_onboardingRegionScratch, sizeof(s_onboardingRegionScratch),
                               regionPickAt(s_onboardingPickIndex)->code);
        s_onboardingStage = ONBOARD_STAGE_SELECT_ROLE;
        s_onboardingPickIndex = onboardingRoleIndex(s_onboardingRoleScratch);
        renderOnboardingStage();
    } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
        s_onboardingRoleScratch = kOnboardRoles[s_onboardingPickIndex].value;
#if defined(PLATFORM_LINUX)
        // No Wi-Fi step: the OS owns the network.
        onboardingFinalize();
        return;
#endif
        s_onboardingStage = ONBOARD_STAGE_CHOOSE_WIFI;
        renderOnboardingStage();
    }
}

// Step a picker stage back to the previous stage.
static void onboardingPickerBack() {
    if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION) {
        s_onboardingStage = ONBOARD_STAGE_ENTER_SHORT;
        renderOnboardingStage();
    } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
        s_onboardingStage = ONBOARD_STAGE_SELECT_REGION;
        s_onboardingPickIndex = onboardingRegionIndex(s_onboardingRegionScratch);
        renderOnboardingStage();
    }
}

// Commit every onboarding selection into the live config, persist it, and
// reboot so the radio (region/preset), role, and WiFi come up with the choices.
static void onboardingFinalize() {
    utf8util::copyTruncate(s_cfg.nodeLong, MESH_LONG_NAME_MAX_BYTES + 1, s_onboardingLongScratch);
    utf8util::copyTruncate(s_cfg.nodeShort, sizeof(s_cfg.nodeShort), s_onboardingShortScratch);
    utf8util::copyTruncate(s_cfg.region, sizeof(s_cfg.region), s_onboardingRegionScratch);
    s_cfg.deviceRole = cfgCoerceDeviceRole(s_onboardingRoleScratch);
    utf8util::copyTruncate(s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid), s_onboardingWifiSsidScratch);
    utf8util::copyTruncate(s_cfg.wifiPass, sizeof(s_cfg.wifiPass), s_onboardingWifiPassScratch);

    // Meshtastic 2.8 switches a fresh US install from Long Fast to Long Turbo
    // when the region is picked (menuHandler::presetForRegionSelection()). The
    // preset changes the channel name, which feeds both the channel hash and the
    // frequency-slot hash — so a default 2.8 US node and a Long Fast node are on
    // different frequencies at different modem settings and cannot hear each
    // other at all. Mirroring it here is what keeps a new node on the mesh its
    // neighbours are actually using.
    //
    // Only on first run, and only while the preset is still the install default:
    // a user who deliberately moved off Long Fast keeps their choice, and an
    // existing configuration is never touched. Nor is one picked in the web
    // flasher's Customize dialog, which is as deliberate as any.
    if (s_firstBoot && !s_presetProvisioned && s_cfg.modemPreset == PRESET_LONG_FAST &&
        strcmp(s_cfg.region, "US") == 0) {
        s_cfg.modemPreset = PRESET_LONG_TURBO;
        Serial.println("[onboarding] US region: preset defaulted to Long Turbo "
                       "(Meshtastic 2.8 default)");
    }

    // The picker lists every region; a ham band must not persist unlicensed.
    cfgCoerceLicensed(s_cfg);
    // Re-derive loraFreq/BW/SF/CR from the chosen region + current preset.
    applyPresetParams(s_cfg);
    s_firstBoot = false;

    Serial.printf("[onboarding] saved nodeLong=\"%s\" nodeShort=\"%s\" region=\"%s\" "
                  "role=%u wifi=\"%s\" preset=\"%s\"\n",
                  s_cfg.nodeLong, s_cfg.nodeShort, s_cfg.region, s_cfg.deviceRole,
                  s_cfg.wifiSsid, kPresets[s_cfg.modemPreset].channelName);

    persistConfigToPrefs();
    Serial.println("[onboarding] complete - rebooting");
    lv_timer_handler();
    delay(500);
    flushPersistentState();   // settings and transcripts must land before we go
    ESP.restart();
}

// Boards with a global-navigation gesture: the M9's dedicated buttons, and the
// Alt+letter chords on every keyboard that exposes Alt as a modifier of its own
// (see altNavShortcut() in keyboard.cpp). Boards outside this set have no way to
// raise one, so the whole mechanism compiles out.
#define HAS_GLOBAL_NAV_SHORTCUTS \
    (defined(DEVICE_M9) || defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) \
     || defined(DEVICE_MESH_DECK) || defined(DEVICE_TLORA_PAGER_TFT) \
     || UI_CARDPUTER_COMPACT || defined(DEVICE_TDISPLAY_P4) \
     || UI_TANMATSU_LAYOUT)

#if HAS_GLOBAL_NAV_SHORTCUTS
static bool prepareGlobalNavigation() {
    // Onboarding owns the whole input surface until identity and region exist.
    if (s_onboardingModal) return false;
#if HAS_STATE_MAPS
    // Home is an escape hatch, not consent to delete files. Treat it as Keep,
    // record that choice, and continue closing the remaining surfaces.
    if (s_legacyMapPromptModal) keepLegacyMapFiles();
#endif

    if (s_otaPromptModal) otaPromptDecline();
#if HAS_BLE_KEYBOARD
    // Same rule as the map prompt: leaving is not consent, so it reads as Ignore.
    if (s_bleKbdRepairModal) bleKbdRepairIgnore();
#endif
#if HAS_HOME_DASHBOARD
    // Closed here rather than in each shortcut: every one of them is going
    // somewhere else, and a dashboard left behind would sit under the screen
    // they opened and reappear when it closed.
    closeHomeDashboard();
#endif
#if HAS_CHANNEL_PANEL_LOCK
    if (s_channelColumnFocus) setChannelColumnFocus(false);
#endif
    closeEmojiPicker();
#if HAS_MESSAGE_ACTIONS
    closeMsgSenderInfoModal();
#endif
    closeTracerouteProgressModal();
    closeChUtilChartModal();
    closeSnrRssiChartModal();
#if FEATURE_DISCOVERY
    closeDiscoveryModal();
#endif
    closeBeaconsModal();
#if HAS_WEATHER
    closeWeatherModal();
#endif
#if FEATURE_MQTT_MONITOR
    closeMqttMonitorModal();
#endif

    // Revert staged previews first, then remove root-owned picker backdrops
    // that are not children of the Settings modal itself.
    closeCfgModal();
    closeCfgColorPickerModal();
    closeAlertSoundModal();
    closeChatStyleModal();
    closeThemeModal();
    closeChatNameModal();
    closeFontSizeModal();
    closeSigPolicyModal();
    closeChanCfgModal();
    closeNodesActionMenu();
    closeLiveModal();
    closeNodesModal();
    closeLegendModal();
    // Tools belongs on this list since issue #94 made it a full screen rather
    // than a card on a backdrop: this function's contract is that nothing is
    // left standing behind the destination, and a Tools screen left underneath
    // would reappear when that destination closed.
    closeLiveToolsModal();
    closeMecpForm();
    closeChannelActionsModal();
    setChannelDropdownVisible(false);

    if (s_composeModal) closeComposePrompt();
    return true;
}
#endif

#if HAS_GLOBAL_NAV_SHORTCUTS
// ── Global navigation shortcuts ──────────────────────────────────────────────
// One destination each, reachable from anywhere: prepareGlobalNavigation()
// above tears down every modal, picker, filter and staged preview first, so
// these do not care what was on screen when the key arrived.
//
// Shared by the M9's dedicated buttons and the Alt+letter chords on the
// keyboard boards. Same destinations either way — a shortcut that behaved
// differently depending on which hardware raised it would be two features.
static void openNavDirectMessagesShortcut() {
    if (!prepareGlobalNavigation()) return;
    if (s_dmModal && lvObjAlive(s_dmModal)) {
        // Already there: drop back to the conversation list rather than
        // reopening, which would discard the list state for no reason. Where
        // the list is a drawer, a second press slides it in, and a third out
        // again -- the M9's Messages button does the same with the channels.
        closeDmNodePicker();
        lv_obj_move_foreground(s_dmModal);
        refreshDmModal(true);
        if (dmDrawerBuilt()) dmDrawerSetOpen(!s_dmDrawerOpen);
        return;
    }
    openDmModal();
}

static void openNavToolsShortcut() {
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
    openLiveToolsModal();
}

static void openNavNodesShortcut() {
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
    openNodesModal();
}

static void openNavConfigShortcut() {
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
    // prepareGlobalNavigation() closes Config on its way through, so this
    // reopens it clean. That also makes Alt+C from inside a Config sub-picker
    // a way back to the top of Config rather than a no-op.
    openCfgModal();
}

static void openNavHelpShortcut() {
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
    // Same reopen-clean shape as Config above, and for the same reason:
    // prepareGlobalNavigation() closes Help on its way through, so the chord is
    // a way back to Help rather than a no-op when Help is already what is up.
    openLegendModal();
}

#if HAS_HOME_DASHBOARD && UI_CHANNEL_LIST_DROPDOWN
// Chat has one job — get back to the messages — and on the chat screen that job
// is already done. So there it does the next thing you wanted: opens the channel
// list, which on these boards is a dropdown that has to be asked for.
//
// Only the *deliberate* ways of asking get this. The M9's Messages button and a
// plain C on the chat screen do; the Alt+C/Sym+C chord does not, and gets the
// plain "close everything and go to chat" instead — see the note on
// openKeyboardHomeShortcut() for what binding the reflexive escape gesture to
// the expensive surface cost the Mesh Deck's keyboard.
//
// Asked before prepareGlobalNavigation() runs, because that closes everything
// this looks at. A dropdown that is already open counts as "not plain chat":
// Home closes it and stops there, so the gesture stays a way out of things
// rather than a toggle that can leave you where you started.
static bool homeShouldOpenChannelList() {
    // Anchored beside the chat already: nothing to open -- except on a locked
    // keyboard-only panel, where this asks for the column's cursor instead.
#if HAS_CHANNEL_PANEL_LOCK
    if (!channelListIsDropdown() && !channelColumnDocked()) return false;
#else
    if (!channelListIsDropdown()) return false;
#endif
    if (!chatScreenIsForeground()) return false;
    if (isChannelDropdownVisible()) return false;
    // The Tools surfaces sit above chat but outside that predicate's list — it
    // was written for the USER button, where missing one costs a composer over
    // the wrong screen. Here it would cost a dropdown popping open behind a
    // screen the press was meant to close, which looks like the button did two
    // things. Everything prepareGlobalNavigation() tears down belongs here; the
    // Config pickers do not, because Config stays open behind them and
    // s_cfgModal already answers for the lot.
    if (s_otaPromptModal || s_liveToolsModal || s_beaconsModal
        || s_chUtilChartModal || s_snrChartModal) {
        return false;
    }
#if HAS_BLE_KEYBOARD
    if (s_bleKbdRepairModal) return false;
#endif
#if FEATURE_DISCOVERY
    if (s_discoveryModal) return false;
#endif
#if FEATURE_MQTT_MONITOR
    if (s_mqttMonModal) return false;
#endif
    return true;
}
#endif

#if HAS_HOME_DASHBOARD
// The chat screen as a destination of its own, which it has to be once Home
// means the dashboard. Reached by the M9's Messages button, by C on a keyboard,
// and by the Alt+C/Sym+C chord.
//
// Two flags rather than one behaviour, because the callers genuinely differ:
//
//  - allowChannelList: whether a press that lands on chat when chat is already
//    there should go on to open the channel list. Deliberate presses yes, the
//    reflexive chord no (see homeShouldOpenChannelList above).
//  - resetChannel: the M9's button has always returned to the first channel,
//    and people have that in their fingers. The keyboard boards' equivalent has
//    always kept the channel you were reading. Neither should change now.
static void openNavChatShortcut(bool allowChannelList, bool resetChannel) {
#if UI_CHANNEL_LIST_DROPDOWN
    // Asked before prepareGlobalNavigation() runs, because that closes
    // everything the predicate looks at.
    const bool pickChannel = allowChannelList && homeShouldOpenChannelList();
#else
    // The Pager keeps its channel list anchored beside the chat at all times,
    // so there is nothing here to ask for.
    LV_UNUSED(allowChannelList);
    const bool pickChannel = false;
#endif
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
    if (pickChannel) {
#if HAS_CHANNEL_PANEL_LOCK
        if (channelColumnDocked()) {
            setChannelColumnFocus(true);
            return;
        }
#endif
        setChannelDropdownVisible(true);
        refreshChannelGlow(true);
        return;
    }
    if (resetChannel) setActiveChannel(0);
}

static void openNavHomeDashboardShortcut() {
    // prepareGlobalNavigation() closes the dashboard on its way through, so
    // Home pressed while already there rebuilds it rather than doing nothing.
    // That is the same "tear down whatever is up and go" contract every other
    // shortcut has, and it costs a dozen LVGL objects.
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
    openHomeDashboard();
}
#endif  // HAS_HOME_DASHBOARD

#if HAS_HOME_DASHBOARD
// Whether KEY_OPEN_CHAT arrives from a labelled button or from a modifier
// chord. The M9's controller resolves Alt itself and reports no chords at all,
// so on that board the code can only have come from the Messages button; on
// every other board it can only have come from Alt+C/Sym+C.
#if defined(DEVICE_M9)
static constexpr bool kNavChatKeyIsDeliberate = true;
#else
static constexpr bool kNavChatKeyIsDeliberate = false;
#endif
#endif

#if defined(DEVICE_CARDPUTER_ZERO)
static constexpr uint32_t kZeroEscDoubleTapMs = 400;

// The number row's F-key meaning outside text entry (see pumpKeyboardInput).
static char zeroNumberRowKey(char k) {
    switch (k) {
        case '1': return KEY_OPEN_HOME;
#if HAS_HOME_DASHBOARD
        case '2': return KEY_OPEN_CHAT_BTN;
#else
        case '2': return KEY_OPEN_HOME;   // no dashboard: Home is chat
#endif
        case '3': return KEY_OPEN_DMS;
        case '4': return KEY_OPEN_NODES;
        case '5': return KEY_OPEN_TOOLS;
        case '6': return KEY_OPEN_CONFIG;
        case '0': return KEY_OPEN_HELP;
        default:  return KEY_NONE;
    }
}
#endif

// Returns true when the key was a navigation shortcut and has been acted on.
static bool handleGlobalNavigationKey(char key) {
#if defined(DEVICE_M9)
    if (key == KEY_SLEEP_SCREEN) {
        requestScreenOff("M9 d-pad centre hold");
        return true;
    }
#endif
#if HAS_HOME_DASHBOARD
    // Home is the dashboard on every board that has one; chat is its own
    // destination beside it. Outside the M9 block above because the boards
    // without hardware buttons reach both of these by chord.
    if (key == KEY_OPEN_HOME) {
        openNavHomeDashboardShortcut();
        return true;
    }
    if (key == KEY_OPEN_CHAT) {
        // The M9 raises this from the Messages button, a deliberate press that
        // has always reset to the first channel. Everywhere else it is the
        // Alt+C/Sym+C chord — the reflexive escape hatch, which keeps both the
        // channel you were on and the channel list closed.
        openNavChatShortcut(/*allowChannelList=*/kNavChatKeyIsDeliberate,
                            /*resetChannel=*/kNavChatKeyIsDeliberate);
        return true;
    }
    if (key == KEY_OPEN_CHAT_BTN) {
        // The P4's F2: a labelled key, so as deliberate as the M9's button and
        // pressed on chat it opens the channel list. It keeps the channel you
        // were on, as every keyboard board's chat shortcut always has.
        openNavChatShortcut(/*allowChannelList=*/true, /*resetChannel=*/false);
        return true;
    }
#endif
    if (key == KEY_OPEN_DMS) {
        openNavDirectMessagesShortcut();
        return true;
    }
    if (key == KEY_OPEN_TOOLS) {
        openNavToolsShortcut();
        return true;
    }
    if (key == KEY_OPEN_NODES) {
        openNavNodesShortcut();
        return true;
    }
    if (key == KEY_OPEN_CONFIG) {
        openNavConfigShortcut();
        return true;
    }
    if (key == KEY_OPEN_HELP) {
        openNavHelpShortcut();
        return true;
    }
    return false;
}
#endif  // HAS_GLOBAL_NAV_SHORTCUTS

#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_TLORA_PAGER_TFT) || UI_CARDPUTER_COMPACT \
    || UI_TANMATSU_LAYOUT
// Alt+H is "close everything and get back to chat", and it deliberately does
// NOT open the channel list the way the M9's dedicated Home button does.
//
// It did briefly, for symmetry with that button. On the Mesh Deck that turned
// out to cost keystrokes: readKey() is called once per loop() pass, so the
// matrix scan interval is the loop period, and that is ~18 ms with the channel
// list closed against 50-166 ms with it open (see the note in readKey(),
// keyboard.cpp) — lv_timer_handler() renders and flushes over blocking SPI on
// the same thread, and the matrix is simply not scanned while it does. Making
// the escape-hatch gesture *open* the expensive surface meant a reflexive
// Alt+H left the board dropping keys, and these boards already have a one-key
// way to open the list: H on its own.
static void openKeyboardHomeShortcut() {
#if HAS_HOME_DASHBOARD
    // Home means the dashboard now, chord included. The reason the chord stayed
    // cheap still holds: the dashboard is a static surface that repaints only
    // when a chart's sequence moves, not a live-filtered list.
    openNavHomeDashboardShortcut();
#else
    if (!prepareGlobalNavigation()) return;
    closeDmModal();
#endif
}
#endif

#if defined(DEVICE_TDECK_PRO)
// Whether now is a good moment to repaint the e-paper panel.
//
// A partial update parks loop() for ~700 ms and a full one for ~1.1 s, and the
// middle of a typing burst is the worst possible moment to start one: the panel
// commits a half-finished line, and every key struck while it paints has to wait
// out the rest of the cycle before the UI can even look at it. So the refresh
// waits for a short gap in typing and then paints the finished line in one go.
//
// The wait is bounded. After kEinkTypingHoldMaxMs of unbroken typing the panel
// refreshes regardless, so someone typing continuously watches the text arrive
// in chunks rather than watching a frozen screen. Nothing is lost either way:
// keys are applied to the text field as they arrive, and the panel is always
// catching up to the current state rather than replaying an old one.
static constexpr uint32_t kEinkTypingQuietMs   = 140;
static constexpr uint32_t kEinkTypingHoldMaxMs = 1200;
static uint32_t s_einkTypingHoldSinceMs = 0;

static bool einkRefreshDueNow() {
    const uint32_t lastKeyMs = keyboardLastKeyMs();
    if (lastKeyMs == 0) return true;   // nothing has ever been typed

    const uint32_t now = millis();
    if ((uint32_t)(now - lastKeyMs) >= kEinkTypingQuietMs
        && keyboardPendingKeys() == 0) {
        s_einkTypingHoldSinceMs = 0;
        return true;
    }

    if (s_einkTypingHoldSinceMs == 0) s_einkTypingHoldSinceMs = now;
    if ((uint32_t)(now - s_einkTypingHoldSinceMs) >= kEinkTypingHoldMaxMs) {
        s_einkTypingHoldSinceMs = now;   // start the next chunk's budget
        return true;
    }
    return false;
}
#endif

