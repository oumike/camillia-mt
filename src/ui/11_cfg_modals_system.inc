// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Config modals: time, alert sound, colour picker, Wi-Fi scan, presets.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static int daysInMonth(int year, int month) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 31;
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

// Rows the user can reach right now. In Automatic mode the date and time rows
// are hidden, so navigation has to skip them rather than stall on a blank line.
static bool timeCfgRowVisible(int row) {
    if (row == TIME_CFG_SOURCE) return true;
    return timeCfgIsManual();
}

static void timeCfgAdjust(int row, int delta) {
    if (delta == 0) return;
    switch (row) {
        case TIME_CFG_SOURCE:
            s_timeCfgSource = timeCfgIsManual() ? TIME_SOURCE_AUTO : TIME_SOURCE_MANUAL;
            break;
        case TIME_CFG_YEAR:
            s_timeCfgYear += delta;
            if (s_timeCfgYear < 2020) s_timeCfgYear = 2099;
            if (s_timeCfgYear > 2099) s_timeCfgYear = 2020;
            break;
        case TIME_CFG_MONTH:
            s_timeCfgMonth += delta;
            if (s_timeCfgMonth < 1)  s_timeCfgMonth = 12;
            if (s_timeCfgMonth > 12) s_timeCfgMonth = 1;
            break;
        case TIME_CFG_DAY: {
            const int last = daysInMonth(s_timeCfgYear, s_timeCfgMonth);
            s_timeCfgDay += delta;
            if (s_timeCfgDay < 1)    s_timeCfgDay = last;
            if (s_timeCfgDay > last) s_timeCfgDay = 1;
            break;
        }
        case TIME_CFG_HOUR:
            s_timeCfgHour = (s_timeCfgHour + delta + 24) % 24;
            break;
        case TIME_CFG_MINUTE:
            s_timeCfgMinute = (s_timeCfgMinute + delta + 60) % 60;
            break;
        default:
            return;
    }
    // Feb 30 is reachable by changing month or year under a high day number.
    const int last = daysInMonth(s_timeCfgYear, s_timeCfgMonth);
    if (s_timeCfgDay > last) s_timeCfgDay = last;

    // Leaving Manual with the cursor down in the date rows would strand the
    // selection on a row that is about to disappear.
    if (row == TIME_CFG_SOURCE && !timeCfgRowVisible(s_timeCfgSelection)) {
        s_timeCfgSelection = TIME_CFG_SOURCE;
    }
    refreshTimeCfgRows();
}

static void timeCfgSave() {
    const bool manual = timeCfgIsManual();
    s_cfg.timeSource = manual ? TIME_SOURCE_MANUAL : TIME_SOURCE_AUTO;
    markConfigDirty();

    if (manual) {
        if (applyManualClock(s_timeCfgYear, s_timeCfgMonth, s_timeCfgDay,
                             s_timeCfgHour, s_timeCfgMinute)) {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Clock set: %04d-%02d-%02d %02d:%02d"),
                     s_timeCfgYear, s_timeCfgMonth, s_timeCfgDay,
                     s_timeCfgHour, s_timeCfgMinute);
        } else {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Invalid date/time"));
            return;   // stay open so the bad field can be corrected
        }
    } else {
        // Back to automatic: let the next NTP attempt reconfigure from scratch
        // rather than wait out the six-hour re-arm window.
        s_ntpConfigured = false;
        s_gpsClockSyncMs = 0;
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Time source: Internet / GPS"));
    }
    closeTimeCfgModal();
    refreshCfgModal();
}

static void onTimeCfgRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= TIME_CFG_ROW_COUNT) return;
    s_timeCfgSelection = idx;
    // Touch builds have no left/right: a tap steps the field, wrapping at the
    // end, which is the only way to drive this without a keyboard.
    timeCfgAdjust(idx, 1);
}

static void onTimeCfgBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_timeCfgBackdrop) return;
    closeTimeCfgModal();   // discards staged edits
}

static void closeTimeCfgModal() {
    if (lvObjValid(s_timeCfgBackdrop)) {
        lv_obj_del(s_timeCfgBackdrop);
    } else if (lvObjValid(s_timeCfgModal)) {
        lv_obj_del(s_timeCfgModal);
    }
    s_timeCfgBackdrop = nullptr;
    s_timeCfgModal = nullptr;
    s_timeCfgDateGrid = nullptr;
    s_timeCfgTimeGrid = nullptr;
    s_timeCfgHint = nullptr;
    memset(s_timeCfgRows, 0, sizeof(s_timeCfgRows));
    memset(s_timeCfgValues, 0, sizeof(s_timeCfgValues));
}

static void refreshTimeCfgRows() {
    if (!s_timeCfgModal) return;

    // Automatic mode drops both complete date/time lines. Flex skips hidden
    // children, so the modal gives that space back instead of retaining an
    // empty carrier row.
    const bool manual = timeCfgIsManual();
    if (s_timeCfgDateGrid) {
        if (manual) lv_obj_clear_flag(s_timeCfgDateGrid, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_timeCfgDateGrid, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_timeCfgTimeGrid) {
        if (manual) lv_obj_clear_flag(s_timeCfgTimeGrid, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_timeCfgTimeGrid, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < TIME_CFG_ROW_COUNT; i++) {
        lv_obj_t *row = s_timeCfgRows[i];
        if (!row) continue;
        if (!timeCfgRowVisible(i)) continue;   // hidden, or inside a hidden line

        const bool sel = (i == s_timeCfgSelection);
        paintPickerRow(row, sel);
        // Recursive: the date and time cells sit inside a grid, one level below
        // the modal that actually scrolls.
        if (sel) lv_obj_scroll_to_view_recursive(row, LV_ANIM_OFF);

        lv_obj_t *val = s_timeCfgValues[i];
        if (!val) continue;
        switch (i) {
            case TIME_CFG_SOURCE:
                lv_label_set_text(val, timeCfgIsManual() ? TR("Manual") : TR("Internet / GPS"));
                break;
            case TIME_CFG_YEAR:   lv_label_set_text_fmt(val, "%04d", s_timeCfgYear);   break;
            case TIME_CFG_MONTH:  lv_label_set_text_fmt(val, "%02d", s_timeCfgMonth);  break;
            case TIME_CFG_DAY:    lv_label_set_text_fmt(val, "%02d", s_timeCfgDay);    break;
            case TIME_CFG_HOUR:   lv_label_set_text_fmt(val, "%02d", s_timeCfgHour);   break;
            case TIME_CFG_MINUTE: lv_label_set_text_fmt(val, "%02d", s_timeCfgMinute); break;
            default: break;
        }
    }

    if (s_timeCfgHint) {
#if UI_TOUCH_ONLY_PROFILE
        // "then Save" is redundant now the button row says it.
        lv_label_set_text(s_timeCfgHint, timeCfgIsManual()
                              ? "Tap a field to step it"
                              : "Tap the source to change it");
#else
        lv_label_set_text_fmt(s_timeCfgHint, TR("L/R=Change   Enter=Save   %s=Back"),
                              modalCloseKeyLabel());
#endif
    }
}

static void openTimeCfgModal() {
    if (!s_rootScreen || s_timeCfgModal || s_timeCfgBackdrop) return;

    // Stage from the config and the running clock, so Manual opens on the time
    // the device currently believes it is rather than on an arbitrary default.
    s_timeCfgSource = cfgCoerceTimeSource(s_cfg.timeSource);
    applyTimezoneFromConfig();
    time_t nowSec = time(nullptr);
    if (nowSec >= kClockSetEpoch) {
        struct tm lt;
        localtime_r(&nowSec, &lt);
        s_timeCfgYear   = lt.tm_year + 1900;
        s_timeCfgMonth  = lt.tm_mon + 1;
        s_timeCfgDay    = lt.tm_mday;
        s_timeCfgHour   = lt.tm_hour;
        s_timeCfgMinute = lt.tm_min;
    }
    s_timeCfgSelection = TIME_CFG_SOURCE;

    // Borrows the channel modals' per-board metrics: same kind of surface (a
    // short list of single-line rows), so the two should measure the same.
    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 16;
    if (modalW > kChanModalMaxW) modalW = kChanModalMaxW;
    if (modalW < 120) modalW = w - 4;

    s_timeCfgBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_timeCfgBackdrop, w, h);
    lv_obj_align(s_timeCfgBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_timeCfgBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_timeCfgBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_timeCfgBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_timeCfgBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_timeCfgBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_timeCfgBackdrop, 0, 0);
    lv_obj_add_event_cb(s_timeCfgBackdrop, onTimeCfgBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_timeCfgModal = lv_obj_create(s_timeCfgBackdrop);
    lv_obj_set_size(s_timeCfgModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_timeCfgModal,
                                (h > 40) ? (h - 2 * kChanModalPad) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_timeCfgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_timeCfgModal, LV_OBJ_FLAG_CLICKABLE);
    setupVScroll(s_timeCfgModal);
    lv_obj_set_scrollbar_mode(s_timeCfgModal, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_timeCfgModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_timeCfgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_timeCfgModal, 1, 0);
    lv_obj_set_style_border_color(s_timeCfgModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_timeCfgModal, kChanModalPad, 0);
    lv_obj_set_style_pad_row(s_timeCfgModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_timeCfgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_timeCfgModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_timeCfgBackdrop);

    lv_obj_t *title = lv_label_create(s_timeCfgModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, kChanModalTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Time and Date"));

    s_timeCfgHint = lv_label_create(s_timeCfgModal);
    lv_obj_set_width(s_timeCfgHint, lv_pct(100));
    lv_obj_set_style_text_font(s_timeCfgHint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_timeCfgHint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_timeCfgHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_timeCfgHint, "");

    static const char *kTimeRowLabel[TIME_CFG_ROW_COUNT] = {
        TR_NOOP("Source"), TR_NOOP("Year"), TR_NOOP("Month"), TR_NOOP("Day"), TR_NOOP("Hour"), TR_NOOP("Minute")
    };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    // One cell of the modal: the full-width Source row or one column of the
    // date/time grids. Values are short enough — four digits at most — that even
    // a third of the Cardputer's width fits "Month 07" on a single line.
    auto makeCell = [&](lv_obj_t *parent, int i, int widthPct) {
        lv_obj_t *cell = lv_btn_create(parent);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(cell);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_timeCfgRows[i] = cell;
        lv_obj_set_width(cell, lv_pct(widthPct));
        lv_obj_set_height(cell, kChanModalRowH);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_pad_left(cell, 5, 0);
        lv_obj_set_style_pad_right(cell, 5, 0);
        lv_obj_set_style_pad_top(cell, 1, 0);
        lv_obj_set_style_pad_bottom(cell, 1, 0);
        lv_obj_set_style_pad_column(cell, 4, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(cell, onTimeCfgRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *label = lv_label_create(cell);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(label, rowTextColor, 0);
        lv_label_set_text(label, TR(kTimeRowLabel[i]));

        lv_obj_t *val = lv_label_create(cell);
        s_timeCfgValues[i] = val;
        lv_obj_set_flex_grow(val, 1);
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(val, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(val, rowTextColor, 0);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text(val, "");
    };

    // Transparent carrier for one grid line. Hiding this is what makes the whole
    // date (or time) line vanish in Automatic mode — hiding the cells alone would
    // leave an empty band behind.
    auto makeGrid = [&](int cellHeight) {
        lv_obj_t *grid = lv_obj_create(s_timeCfgModal);
        lv_obj_set_width(grid, lv_pct(100));
        lv_obj_set_height(grid, cellHeight);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_style_pad_column(grid, kChanModalGap, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        return grid;
    };

    makeCell(s_timeCfgModal, TIME_CFG_SOURCE, 100);

    // Date across three columns, then hour and minute across the full next row.
    // Percentages leave room for the gutter pad_column draws between cells.
    s_timeCfgDateGrid = makeGrid(kChanModalRowH);
    makeCell(s_timeCfgDateGrid, TIME_CFG_YEAR,  32);
    makeCell(s_timeCfgDateGrid, TIME_CFG_MONTH, 32);
    makeCell(s_timeCfgDateGrid, TIME_CFG_DAY,   32);

    s_timeCfgTimeGrid = makeGrid(kChanModalRowH);
    makeCell(s_timeCfgTimeGrid, TIME_CFG_HOUR,   49);
    makeCell(s_timeCfgTimeGrid, TIME_CFG_MINUTE, 49);

#if HAS_TOUCH
    // The sole commit/discard controls. They stay visible when Automatic mode
    // hides both date/time grids, so that compact modal still has an explicit
    // way to apply or abandon the staged source change.
    appendHeltecCancelSaveRow(
        s_timeCfgModal,
        [](lv_event_t *e) { LV_UNUSED(e); closeTimeCfgModal(); },
        [](lv_event_t *e) { LV_UNUSED(e); timeCfgSave(); });
#endif

    lv_obj_update_layout(s_timeCfgModal);
    refreshTimeCfgRows();
}

// ── Notification-sound picker ────────────────────────────────────────────────
// Picking directly instead of cycling. Moving the selection previews the sound,
// which means s_cfg.msgAlertSound has to hold the highlighted mode while the
// modal is open (triggerMessageAlert plays whatever is configured). The original
// value is stashed on open and restored if the user cancels, so previewing is
// never destructive.
static void closeAlertSoundModal() {
    if (lvObjValid(s_alertSoundBackdrop)) {
        lv_obj_del(s_alertSoundBackdrop);
    } else if (lvObjValid(s_alertSoundModal)) {
        lv_obj_del(s_alertSoundModal);
    }
    s_alertSoundBackdrop = nullptr;
    s_alertSoundModal = nullptr;
    memset(s_alertSoundRows, 0, sizeof(s_alertSoundRows));
}

// Cancel path: undo any preview-driven change before closing.
static void cancelAlertSoundModal() {
    // Guarded like revertCfgBrightnessPreview(), and for a sharper reason now
    // that closeCfgModal() calls this on every Config teardown: applying a tone
    // does NOT update s_alertSoundOriginal, so an unguarded revert would undo a
    // sound the user had just saved, every time they left the Config screen.
    if (!s_alertSoundModal && !s_alertSoundBackdrop) return;
    s_cfg.msgAlertSound = s_alertSoundOriginal;
    closeAlertSoundModal();
    refreshCfgModal();
}

static void refreshAlertSoundSelection() {
    if (!s_alertSoundModal) return;
    paintPickerRows(s_alertSoundRows, MSG_ALERT_SOUND_MAX + 1, s_alertSoundSelection, PICKER_SCROLL_VIEW);
}

// Move the highlight and play that mode so the user hears it before committing.
static void previewAlertSoundSelection(int mode) {
    if (mode < 0 || mode > MSG_ALERT_SOUND_MAX) return;
    s_alertSoundSelection = mode;
    s_cfg.msgAlertSound = (uint8_t)mode;
    refreshAlertSoundSelection();
    playAlertSound((uint8_t)mode);   // the sound only; no lights or vibration
}

static void applyAlertSoundSelection(int mode) {
    if (mode < 0 || mode > MSG_ALERT_SOUND_MAX) return;
    s_cfg.msgAlertSound = (uint8_t)mode;
    if ((uint8_t)mode != s_alertSoundOriginal) {
        persistMessageAlertSetting();
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Notification sound: %s"),
             msgAlertSoundName((uint8_t)mode));
    closeAlertSoundModal();
    refreshCfgModal();
}

static void onAlertSoundRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    // First tap on a different row previews it; tapping the highlighted row
    // commits. Touch has no separate "move" gesture, so this gives touch-only
    // builds the same hear-before-you-commit behavior as arrow navigation.
    if (idx != s_alertSoundSelection) previewAlertSoundSelection(idx);
    else                              applyAlertSoundSelection(idx);
}

static void onAlertSoundBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_alertSoundBackdrop) return;
    cancelAlertSoundModal();
}

static void openAlertSoundModal() {
    if (!s_rootScreen || s_alertSoundModal || s_alertSoundBackdrop) return;
    s_alertSoundOriginal = s_cfg.msgAlertSound;
    s_alertSoundSelection = (s_cfg.msgAlertSound <= MSG_ALERT_SOUND_MAX)
                                ? s_cfg.msgAlertSound : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    // Four short labels in a 2x2 grid, so this modal is deliberately narrower
    // than the list-style pickers — no description column to make room for.
    int modalW = w - 40;
    if (modalW < 150) modalW = w - 8;
    if (modalW > 210) modalW = 210;

    s_alertSoundBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_alertSoundBackdrop, w, h);
    lv_obj_align(s_alertSoundBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_alertSoundBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_alertSoundBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_alertSoundBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_alertSoundBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_alertSoundBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_alertSoundBackdrop, 0, 0);
    lv_obj_add_event_cb(s_alertSoundBackdrop, onAlertSoundBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_alertSoundModal = lv_obj_create(s_alertSoundBackdrop);
    lv_obj_set_size(s_alertSoundModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_alertSoundModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_alertSoundModal, LV_ALIGN_CENTER, 0, 0);
    // Content taller than the height cap must stay reachable; the refresh
    // helpers scroll the selected row into view.
    lv_obj_add_flag(s_alertSoundModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_alertSoundModal, LV_DIR_VER);
    lv_obj_add_flag(s_alertSoundModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_alertSoundModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_alertSoundModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_alertSoundModal, 1, 0);
    lv_obj_set_style_border_color(s_alertSoundModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_alertSoundModal, 8, 0);
    lv_obj_set_style_pad_row(s_alertSoundModal, 6, 0);
    lv_obj_set_flex_flow(s_alertSoundModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_alertSoundModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_alertSoundBackdrop);

    lv_obj_t *title = lv_label_create(s_alertSoundModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Notification Sound"));

    lv_obj_t *hint = lv_label_create(s_alertSoundModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    // Just the preview gesture now: the Apply button below says what commits,
    // so the hint no longer has to carry the whole discoverability burden.
    // Tapping the selected row again still applies, for muscle memory.
    lv_label_set_text(hint, TR("Tap to preview"));
#else
    // Kept short so it fits the narrowed modal without wrapping to three lines.
    lv_label_set_text_fmt(hint, TR("Move=Preview  Enter=OK  %s=Cancel"),
                          modalCloseKeyLabel());
#endif

    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    // Wrapping row container: four ~half-width buttons fall into two rows of
    // two. The tone names carry no description text — moving the selection
    // plays the tone, which describes it better than a caption could.
    lv_obj_t *grid = lv_obj_create(s_alertSoundModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);

    for (int i = 0; i <= MSG_ALERT_SOUND_MAX; i++) {
        lv_obj_t *row = lv_btn_create(grid);
        s_alertSoundRows[i] = row;
        lv_obj_set_width(row, lv_pct(46));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onAlertSoundRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_width(name, lv_pct(100));
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(name, msgAlertSoundName((uint8_t)i));
        lv_obj_center(name);
    }

#if UI_TOUCH_ONLY_PROFILE
    // "Apply" rather than "Save": this picker's whole point is hearing a tone
    // before committing, and the button that ends that audition reads better as
    // the verb for it.
    //
    // Built after the tone grid on purpose — if LVGL is short of memory the
    // modal loses its buttons rather than its content, and the backdrop tap
    // still cancels.
    appendHeltecCancelSaveRow(
        s_alertSoundModal,
        [](lv_event_t *e) { LV_UNUSED(e); cancelAlertSoundModal(); },
        [](lv_event_t *e) { LV_UNUSED(e); applyAlertSoundSelection(s_alertSoundSelection); },
        TR_NOOP("Apply"));
#endif

    refreshAlertSoundSelection();
}

#if HAS_NOTIFY_LED
static void closeNotifyLedColorModal() {
    if (lvObjValid(s_notifyLedColorBackdrop)) {
        lv_obj_del(s_notifyLedColorBackdrop);
    } else if (lvObjValid(s_notifyLedColorModal)) {
        lv_obj_del(s_notifyLedColorModal);
    }
    s_notifyLedColorBackdrop = nullptr;
    s_notifyLedColorModal = nullptr;
    memset(s_notifyLedColorRows, 0, sizeof(s_notifyLedColorRows));
}

static void refreshNotifyLedColorSelection() {
    if (!s_notifyLedColorModal) return;
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selBorder  = isLight ? lv_color_hex(0x111111) : lv_color_hex(0xF4F7FF);
    const lv_color_t idleBorder = isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);

    for (int i = 0; i < kNotifyLedColorOptionCount; i++) {
        lv_obj_t *row = s_notifyLedColorRows[i];
        if (!row) continue;
        const bool sel = (i == s_notifyLedColorSelection);
        lv_obj_set_style_border_width(row, sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(row, sel ? selBorder : idleBorder, 0);
        lv_obj_set_style_outline_width(row, sel ? 2 : 0, 0);
        lv_obj_set_style_outline_color(row, sel ? selBorder : idleBorder, 0);
        lv_obj_set_style_outline_opa(row, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (sel) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }
}

static void applyNotifyLedColorSelection(int color) {
    const uint8_t chosen = cfgCoerceNotifyLedColor(color);
    uint8_t *target = s_notifyLedColorTargetDm ? &s_cfg.notifyLedColorDm
                                                : &s_cfg.notifyLedColorChannel;
    const uint8_t prior = cfgCoerceNotifyLedColor((int)*target);
    *target = chosen;
    if (prior != chosen) {
        persistConfigToPrefs();
    }

    snprintf(s_cfgStatus,
             sizeof(s_cfgStatus),
             TR("%s: %s"),
             s_notifyLedColorTargetDm ? TR("Direct Message LED") : TR("Channel Message LED"),
             notifyLedColorName(chosen));
    closeNotifyLedColorModal();
    refreshCfgModal();
}

static void onNotifyLedColorRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= kNotifyLedColorOptionCount) return;

    // First tap previews by selecting the swatch; second tap commits.
    if (idx != s_notifyLedColorSelection) {
        s_notifyLedColorSelection = idx;
        refreshNotifyLedColorSelection();
        return;
    }
    applyNotifyLedColorSelection(idx);
}

static void onNotifyLedColorBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_notifyLedColorBackdrop) return;
    closeNotifyLedColorModal();
    refreshCfgModal();
}

static void openNotifyLedColorModal(bool forDm) {
    if (!s_rootScreen || s_notifyLedColorModal || s_notifyLedColorBackdrop) return;

    s_notifyLedColorTargetDm = forDm;
    s_notifyLedColorSelection = cfgCoerceNotifyLedColor(
        forDm ? (int)s_cfg.notifyLedColorDm : (int)s_cfg.notifyLedColorChannel);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 40;
    if (modalW < 180) modalW = w - 10;
    if (modalW > 220) modalW = 220;

    s_notifyLedColorBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_notifyLedColorBackdrop, w, h);
    lv_obj_align(s_notifyLedColorBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_notifyLedColorBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_notifyLedColorBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_notifyLedColorBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_notifyLedColorBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_notifyLedColorBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_notifyLedColorBackdrop, 0, 0);
    lv_obj_add_event_cb(s_notifyLedColorBackdrop,
                        onNotifyLedColorBackdropPressed,
                        LV_EVENT_CLICKED,
                        nullptr);

    s_notifyLedColorModal = lv_obj_create(s_notifyLedColorBackdrop);
    lv_obj_set_size(s_notifyLedColorModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_notifyLedColorModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_notifyLedColorModal, LV_ALIGN_CENTER, 0, 0);
    setupVScroll(s_notifyLedColorModal);
    lv_obj_set_scrollbar_mode(s_notifyLedColorModal, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_notifyLedColorModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_notifyLedColorModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_notifyLedColorModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_notifyLedColorModal, 1, 0);
    lv_obj_set_style_border_color(s_notifyLedColorModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_notifyLedColorModal, 8, 0);
    lv_obj_set_style_pad_row(s_notifyLedColorModal, 6, 0);
    lv_obj_set_flex_flow(s_notifyLedColorModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_notifyLedColorModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_notifyLedColorBackdrop);

    lv_obj_t *title = lv_label_create(s_notifyLedColorModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, forDm ? TR("Direct Message LED") : TR("Channel Message LED"));

    lv_obj_t *hint = lv_label_create(s_notifyLedColorModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("Move=Preview  Enter=OK  %s=Cancel"),
                          modalCloseKeyLabel());

    lv_obj_t *grid = lv_obj_create(s_notifyLedColorModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, kNotifyLedColorGridW);
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, kNotifyLedColorCellGap, 0);
    lv_obj_set_style_pad_column(grid, kNotifyLedColorCellGap, 0);

    const uint8_t kNotifyLedDisplayOrder[kNotifyLedColorOptionCount] = {
        NOTIFY_LED_COLOR_OFF,
        NOTIFY_LED_COLOR_RED,
        NOTIFY_LED_COLOR_GREEN,
        NOTIFY_LED_COLOR_BLUE,
        NOTIFY_LED_COLOR_YELLOW,
        NOTIFY_LED_COLOR_CYAN,
        NOTIFY_LED_COLOR_MAGENTA,
        NOTIFY_LED_COLOR_WHITE,
    };

    for (int pos = 0; pos < kNotifyLedColorOptionCount; pos++) {
        const uint8_t color = kNotifyLedDisplayOrder[pos];
        lv_obj_t *row = lv_btn_create(grid);
        s_notifyLedColorRows[color] = row;
        lv_obj_set_size(row, kNotifyLedColorCellW, kNotifyLedColorCellH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(row,
                            onNotifyLedColorRowPressed,
                            LV_EVENT_CLICKED,
                            (void *)(intptr_t)color);

        if (color == NOTIFY_LED_COLOR_OFF) {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x101820), 0);
            lv_obj_t *off = lv_label_create(row);
            lv_obj_set_style_text_font(off, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(off, lv_color_hex(0xE8F1FF), 0);
            lv_label_set_text(off, TR("OFF"));
            lv_obj_center(off);
        } else {
            lv_obj_set_style_bg_color(row, tftColorToLv(notifyLedColorPreview565(color)), 0);
        }
    }

    refreshNotifyLedColorSelection();
}
#endif

static void openCfgColorPickerModal() {
    if (!s_rootScreen || s_cfgColorModal || s_cfgColorBackdrop) return;

    s_cfgColorSelection = (s_cfg.userMsgColor < kUserMsgColorCount)
                              ? (s_cfg.userMsgColor + 1) : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgColorBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgColorBackdrop, w, h);
    lv_obj_align(s_cfgColorBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgColorBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgColorBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgColorBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgColorBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgColorBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgColorBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgColorBackdrop, onCfgColorBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgColorModal = lv_obj_create(s_cfgColorBackdrop);
    lv_obj_set_size(s_cfgColorModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgColorModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgColorModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgColorModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgColorModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgColorModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgColorModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgColorModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgColorModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgColorModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgColorModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgColorModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgColorModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgColorBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgColorModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("My Message Color"));

    // Cardputer has no hint row: the line and its 6 px of padding are the space
    // the swatch grid needs to fit between the title and the bottom edge.
#if !UI_CARDPUTER_COMPACT
    lv_obj_t *hint = lv_label_create(s_cfgColorModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if UI_TOUCH_ONLY_PROFILE
                      "Tap a color to apply"
#else
                      "Arrows=Move  Enter=Select  Backspace=Cancel"
#endif
    );
#endif

    s_cfgColorGrid = lv_obj_create(s_cfgColorModal);
    // kMsgColorCols swatches per row. The height is capped so a grid taller than
    // the modal scrolls, with the highlighted swatch scrolled into view.
    lv_obj_set_width(s_cfgColorGrid, kMsgColorGridW);
#if UI_CARDPUTER_COMPACT
    // Derived, not guessed: the modal is capped at h-16, and border, padding,
    // the title line and one row gap are all that sit above the grid. The old
    // hardcoded h-60 assumed a hint row that is no longer built here, which
    // left the grid 17 px taller than the modal could show.
    int gridMaxH = h - 16 - 2 - 16
                   - (int)lv_font_get_line_height(&lv_font_montserrat_16) - 6;
    if (gridMaxH < 40) gridMaxH = 40;
#else
    const int gridMaxH = (h > 60) ? (h - 60) : LV_SIZE_CONTENT;
#endif
    lv_obj_set_height(s_cfgColorGrid, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgColorGrid, gridMaxH, 0);
    lv_obj_add_flag(s_cfgColorGrid, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgColorGrid);
    lv_obj_set_scrollbar_mode(s_cfgColorGrid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_cfgColorGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cfgColorGrid, 0, 0);
    lv_obj_set_style_pad_all(s_cfgColorGrid, 0, 0);
    lv_obj_set_style_pad_row(s_cfgColorGrid, kMsgColorSwatchGap, 0);
    lv_obj_set_style_pad_column(s_cfgColorGrid, kMsgColorSwatchGap, 0);
    lv_obj_set_flex_flow(s_cfgColorGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_cfgColorGrid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    refreshCfgColorPickerModal();
}

static void onCfgWifiRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
    applyCfgWifiSelection(idx);
    if (fromOnboarding) {
        const char *activeSsid = nullptr;
        const char *activePass = nullptr;
        wifiGetActiveCreds(&activeSsid, &activePass);
        utf8util::copyTruncate(s_onboardingWifiSsidScratch,
                               sizeof(s_onboardingWifiSsidScratch),
                               activeSsid ? activeSsid : "");
        utf8util::copyTruncate(s_onboardingWifiPassScratch,
                               sizeof(s_onboardingWifiPassScratch),
                               activePass ? activePass : "");
    }
    closeCfgWifiPickerModal();
    if (fromOnboarding || s_onboardingModal) {
        renderOnboardingStage();
        if (s_cfgStatus[0]) onboardingSetStatus(s_cfgStatus);
    } else {
        refreshCfgModal();
        if (s_cfgStatus[0]) {
            openCfgActionMessageModal(s_cfgStatus);
        }
    }
}

static void onCfgWifiBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgWifiBackdrop) return;
    const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
    closeCfgWifiPickerModal();
    if (fromOnboarding || s_onboardingModal) renderOnboardingStage();
    else refreshCfgModal();
}

static void onCfgWifiOpenScanPressed(lv_event_t *e) {
    LV_UNUSED(e);
    openCfgWifiScanModal();
}

static void onCfgWifiCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
    closeCfgWifiPickerModal();
    if (fromOnboarding || s_onboardingModal) renderOnboardingStage();
    else refreshCfgModal();
}

static void onCfgWifiScanBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgWifiScanBackdrop) return;
    closeCfgWifiScanModal();
    if (s_cfgWifiModal) refreshCfgWifiPickerModal();
}

static void refreshCfgWifiScanModal(bool runScan) {
    if (!s_cfgWifiScanModal || !s_cfgWifiScanList) return;

    if (runScan) {
        s_cfgScannedWifiCount = 0;
        s_cfgWifiScanSelection = 0;
        if (s_cfgWifiScanStatus) {
            lv_label_set_text(s_cfgWifiScanStatus, TR("Scanning WiFi networks..."));
        }
        lv_timer_handler();

        // A scan needs a station interface. Coming from OFF the driver has only
        // just been started, and coming from AP-only there is no station at all;
        // in both cases scanNetworks() can return before the interface is ready,
        // so give it a moment rather than asking immediately.
        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_OFF) {
            WiFi.mode(WIFI_STA);
            delay(150);
        } else if (mode == WIFI_AP) {
            WiFi.mode(WIFI_AP_STA);
            delay(150);
        }

        // An association attempt in flight makes a scan fail outright, and
        // serviceWifiStation() kicks one every few seconds whenever credentials
        // exist and the station is not up. Only when we are not already
        // associated — scanning from a live connection is fine, and dropping it
        // to look at a list would be a poor trade.
        if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false);

        int found = WiFi.scanNetworks();
        if (found < 0) {
            // WIFI_SCAN_FAILED / WIFI_SCAN_RUNNING. Usually the driver still
            // settling after a mode change, or left busy by a web-config AP that
            // was just torn down. One retry costs a moment and clears most of
            // them; the alternative is telling the user there are no networks
            // when we never actually looked.
            Serial.printf("[wifi] scan returned %d, retrying\n", found);
            WiFi.scanDelete();
            delay(400);
            found = WiFi.scanNetworks();
        }
        Serial.printf("[wifi] scan found %d network(s)\n", found);

        if (found > 0) {
            for (int i = 0; i < found && s_cfgScannedWifiCount < kScannedWifiMaxCount; i++) {
                String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;

                bool seen = false;
                for (int j = 0; j < s_cfgScannedWifiCount; j++) {
                    if (ssid.equals(s_cfgScannedWifi[j].ssid)) {
                        seen = true;
                        break;
                    }
                }
                if (seen) continue;

                ScannedWifiEntry &entry = s_cfgScannedWifi[s_cfgScannedWifiCount++];
                memset(&entry, 0, sizeof(entry));
                strncpy(entry.ssid, ssid.c_str(), sizeof(entry.ssid) - 1);
                entry.rssi = WiFi.RSSI(i);
                entry.secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            if (s_cfgWifiScanStatus) {
                char status[64];
                snprintf(status, sizeof(status), TR("Found %d network(s)"), s_cfgScannedWifiCount);
                lv_label_set_text(s_cfgWifiScanStatus, status);
            }
        } else if (s_cfgWifiScanStatus) {
            // "None in range" and "the scan did not run" are different answers,
            // and only one of them is worth retrying. Reporting both as "No
            // networks found" is what made a transient radio state look like
            // there was nothing out there.
            lv_label_set_text(s_cfgWifiScanStatus,
                              (found < 0) ? TR("Scan failed - Rescan to try again")
                                          : TR("No networks found"));
        }
        WiFi.scanDelete();
    }

    lv_obj_clean(s_cfgWifiScanList);
    for (int i = 0; i < s_cfgScannedWifiCount; i++) {
        const ScannedWifiEntry &entry = s_cfgScannedWifi[i];
        char rowText[120];
        snprintf(rowText,
                 sizeof(rowText),
                 "%s  %ld dBm%s",
                 entry.ssid,
                 (long)entry.rssi,
                 entry.secure ? TR("  lock") : TR("  open"));

        lv_obj_t *row = lv_label_create(s_cfgWifiScanList);
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
        lv_obj_set_style_border_width(row, i == s_cfgWifiScanSelection ? 2 : 0, 0);
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
        lv_obj_add_event_cb(row, onCfgWifiScanRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (i == s_cfgWifiScanSelection) {
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

static void onCfgWifiScanRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_cfgWifiScanSelection = idx;
    refreshCfgWifiScanModal(false);
    openCfgWifiPassModal(idx);
}

static void onCfgWifiScanCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeCfgWifiScanModal();
    if (s_cfgWifiModal) refreshCfgWifiPickerModal();
}

static void onCfgWifiScanRescanPressed(lv_event_t *e) {
    LV_UNUSED(e);
    refreshCfgWifiScanModal(true);
}

static void onCfgWifiScanConnectPressed(lv_event_t *e) {
    LV_UNUSED(e);
    if (s_cfgWifiScanSelection >= 0 && s_cfgWifiScanSelection < s_cfgScannedWifiCount) {
        openCfgWifiPassModal(s_cfgWifiScanSelection);
    }
}

static void onCfgWifiPassBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgWifiPassBackdrop) return;
    closeCfgWifiPassModal();
    refreshCfgWifiScanModal(false);
}

static void onCfgWifiPassCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeCfgWifiPassModal();
    refreshCfgWifiScanModal(false);
}

static void cfgWifiConnectFromPassModal() {
    if (!s_cfgWifiPassInput || !s_cfgWifiPassTargetSsid[0]) return;

    // Copied out: the wait below runs LVGL, and Cancel there deletes the
    // textarea and clears the target SSID.
    char ssid[sizeof(s_cfgWifiPassTargetSsid)];
    char pass[sizeof(s_wifiSelectedPass)];
    snprintf(ssid, sizeof(ssid), "%s", s_cfgWifiPassTargetSsid);
    const char *typed = lv_textarea_get_text(s_cfgWifiPassInput);
    snprintf(pass, sizeof(pass), "%s", typed ? typed : "");

    if (s_cfgWifiPassStatus) {
        lv_label_set_text_fmt(s_cfgWifiPassStatus, TR("Connecting to %s..."), ssid);
        lv_timer_handler();
    }

    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    } else if (mode == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
    }

    WiFi.disconnect(false);
    WiFi.begin(ssid, pass);

    bool connected = false;
    uint32_t startMs = millis();
    while ((millis() - startMs) < 12000UL) {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            connected = true;
            break;
        }
        lv_timer_handler();
        delay(120);
    }

    // Cancelled (or replaced by another network's prompt) while waiting.
    if (!s_cfgWifiPassModal || strcmp(s_cfgWifiPassTargetSsid, ssid) != 0) return;

    if (connected) {
        s_wifiStaKickMs = 0;

        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("WiFi connected: %s"), ssid);
        if (s_cfgWifiPickerOnboardingMode) {
            // Onboarding keeps this a runtime override on purpose: the scratch
            // copied below is what onboardingFinalize() folds into s_cfg once
            // the whole flow is confirmed. The network is not the configured one
            // yet, so it needs a temp entry to stay in the picker list.
            addTempKnownWifi(ssid, pass);
            s_wifiUsingKnownOverride = true;
            strncpy(s_wifiSelectedSsid, ssid, sizeof(s_wifiSelectedSsid) - 1);
            strncpy(s_wifiSelectedPass, pass, sizeof(s_wifiSelectedPass) - 1);
            utf8util::copyTruncate(s_onboardingWifiSsidScratch,
                                   sizeof(s_onboardingWifiSsidScratch),
                                   ssid);
            utf8util::copyTruncate(s_onboardingWifiPassScratch,
                                   sizeof(s_onboardingWifiPassScratch),
                                   pass);
            closeCfgWifiPassModal();
            closeCfgWifiScanModal();
            closeCfgWifiPickerModal();
            renderOnboardingStage();
            onboardingSetStatus(s_cfgStatus);
            return;
        }
        // Outside onboarding this picker *is* the WiFi settings UI, so a network
        // joined here becomes the configured one rather than a runtime override.
        // Leaving it as an override was why credentials entered on an already
        // onboarded device never survived a reboot: the override lives only in
        // RAM, s_cfg.wifiSsid stayed empty, and syncWifiCredsToPrefs() bails on
        // exactly that condition — so nothing ever reached the settings blob and
        // the next boot came up in the onboarding AP. A freshly erased device
        // hid this by going through onboarding, whose scratch does reach s_cfg.
        //
        // wifiSetConfiguredNetwork() also moves the network being replaced into
        // the remembered list, so adding a second network does not lose the
        // first — and it makes sure the new one is not listed twice, as both the
        // configured entry and a remembered copy.
        wifiSetConfiguredNetwork(ssid, pass);
        s_wifiUsingKnownOverride = false;
        memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
        memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
        // Clearing the override also drops any persisted "AP" choice, which
        // would otherwise force the SoftAP again on the next boot and mask the
        // network just saved.
        persistWifiForceAp();
        syncWifiCredsToPrefs();
        flushConfigIfDirty();   // not debounced: the user may power-cycle next

        closeCfgWifiPassModal();
        closeCfgWifiScanModal();
        populateKnownWifiEntries();
        refreshCfgWifiPickerModal();
        openCfgActionMessageModal(s_cfgStatus);
        return;
    }

    if (s_cfgWifiScanStatus) {
        lv_label_set_text_fmt(s_cfgWifiScanStatus,
                              TR("Connection failed: %s"),
                              ssid);
    }
    closeCfgWifiPassModal();
    refreshCfgWifiScanModal(false);
}

static void onCfgWifiPassConnectPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgWifiConnectFromPassModal();
}

static void openCfgWifiPassModal(int scanIdx) {
    if (!s_rootScreen) return;
    if (scanIdx < 0 || scanIdx >= s_cfgScannedWifiCount) return;

    closeCfgWifiPassModal();

    const ScannedWifiEntry &entry = s_cfgScannedWifi[scanIdx];
    strncpy(s_cfgWifiPassTargetSsid, entry.ssid, sizeof(s_cfgWifiPassTargetSsid) - 1);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 320) modalW = 320;
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    modalW = w;
#endif

    s_cfgWifiPassBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiPassBackdrop, w, h);
    lv_obj_align(s_cfgWifiPassBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiPassBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiPassBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiPassBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiPassBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiPassBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiPassBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiPassBackdrop, onCfgWifiPassBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiPassModal = lv_obj_create(s_cfgWifiPassBackdrop);
#if UI_TOUCH_ONLY_PROFILE
    lv_obj_set_size(s_cfgWifiPassModal, modalW, h - 18);
#else
    lv_obj_set_size(s_cfgWifiPassModal, modalW, LV_SIZE_CONTENT);
#endif
    lv_obj_align(s_cfgWifiPassModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiPassModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_cfgWifiPassModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiPassModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiPassModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiPassModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgWifiPassModal, 8, 0);
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    lv_obj_set_style_pad_left(s_cfgWifiPassModal, 2, 0);
    lv_obj_set_style_pad_right(s_cfgWifiPassModal, 2, 0);
#endif
    lv_obj_set_style_pad_row(s_cfgWifiPassModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgWifiPassModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiPassModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgWifiPassBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiPassModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(title, TR("Connect: %s"), s_cfgWifiPassTargetSsid);

    s_cfgWifiPassInput = lv_textarea_create(s_cfgWifiPassModal);
    if (!s_cfgWifiPassInput) {
        logLvglMemDiag("wifi pass modal aborted (low LVGL mem)");
        closeCfgWifiPassModal();
        return;
    }
    lv_obj_set_width(s_cfgWifiPassInput, lv_pct(100));
    lv_obj_set_height(s_cfgWifiPassInput, 42);
    lv_obj_set_style_text_font(s_cfgWifiPassInput, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_cfgWifiPassInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_cfgWifiPassInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiPassInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiPassInput, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiPassInput, lv_color_hex(0x4C76BA), 0);
    lv_textarea_set_one_line(s_cfgWifiPassInput, true);
    lv_textarea_set_max_length(s_cfgWifiPassInput, 63);
    lv_textarea_set_password_mode(s_cfgWifiPassInput, false);
    lv_textarea_set_placeholder_text(s_cfgWifiPassInput,
                                     entry.secure ? TR("WiFi password") : TR("Open network (leave blank)"));

    s_cfgWifiPassStatus = lv_label_create(s_cfgWifiPassModal);
    lv_obj_set_width(s_cfgWifiPassStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgWifiPassStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgWifiPassStatus, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgWifiPassStatus, LV_TEXT_ALIGN_LEFT, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(s_cfgWifiPassStatus, TR("Enter password, then tap Connect"));
#else
    lv_label_set_text(s_cfgWifiPassStatus, TR("Enter=Connect  Backspace=Back"));
#endif

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiPassModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onCfgWifiPassCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, TR("Cancel"));
    lv_obj_center(cancelLbl);

    lv_obj_t *connectBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(connectBtn, 1);
    lv_obj_set_height(connectBtn, lv_pct(100));
    lv_obj_add_event_cb(connectBtn, onCfgWifiPassConnectPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connectLbl = lv_label_create(connectBtn);
    lv_obj_set_style_text_font(connectLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(connectLbl, TR("Connect"));
    lv_obj_center(connectLbl);

    // Not with a physical keyboard attached (the T-Display P4's expansion):
    // keys typed on it reach this field through pumpKeyboardInput(), and an
    // on-screen one would only take half the panel. Checked as this opens.
    if (!keyboardAttached()) {
        s_cfgWifiPassKeyboard = lv_keyboard_create(s_cfgWifiPassModal);
        configureOnScreenKeyboard(s_cfgWifiPassKeyboard);
        lv_keyboard_set_textarea(s_cfgWifiPassKeyboard, s_cfgWifiPassInput);
    }
#endif
}

#if HAS_BLE_KEYBOARD
// ── External Bluetooth keyboard pairing ─────────────────────────────────────
// Deliberately the same scan -> pick -> pair -> remember shape as the WiFi scan
// modal directly below, because that is what the flow is; the one difference is
// that a BLE keyboard often asks for a code to be typed *on the keyboard*, so
// the status line has to carry a passkey the WiFi flow never needs.
static void closeCfgBleKbdModal() {
    if (lvObjValid(s_cfgBleKbdBackdrop)) {
        lv_obj_del(s_cfgBleKbdBackdrop);
    } else if (lvObjValid(s_cfgBleKbdModal)) {
        lv_obj_del(s_cfgBleKbdModal);
    }
    s_cfgBleKbdBackdrop = nullptr;
    s_cfgBleKbdModal    = nullptr;
    s_cfgBleKbdList     = nullptr;
    s_cfgBleKbdStatus   = nullptr;
}

// Copies whatever the worker settled on into config. Called from the modal and
// from the loop, so a keyboard that pairs while the dialog is closed is still
// remembered.
static void syncBleKbdPairingToConfig() {
    if (!bleKeyboardTakePairingDirty()) return;
    utf8util::copyTruncate(s_cfg.bleKbdAddr, sizeof(s_cfg.bleKbdAddr), bleKeyboardPairedAddr());
    utf8util::copyTruncate(s_cfg.bleKbdName, sizeof(s_cfg.bleKbdName), bleKeyboardPairedName());
    s_cfg.bleKbdAddrType = bleKeyboardPairedAddrType();
    markConfigDirty();
    Serial.printf("[blekbd] pairing saved: \"%s\" %s\n", s_cfg.bleKbdName, s_cfg.bleKbdAddr);
}

static void refreshCfgBleKbdModal() {
    if (!lvObjValid(s_cfgBleKbdModal) || !lvObjValid(s_cfgBleKbdList)) return;
    s_cfgBleKbdPaintMs = millis();

    if (lvObjValid(s_cfgBleKbdStatus)) {
        char status[96];
        bleKeyboardStatusText(status, sizeof(status));
        const uint32_t passkey = bleKeyboardPasskey();
        char line[160];
        if (passkey) {
            // The keyboard has no screen, so the number can only come from
            // here. Worth showing for the whole attempt rather than only when
            // the peer asks: by the time it asks, the user is already looking
            // at the keyboard.
            snprintf(line, sizeof(line), TR("%s\nType %06u then Enter on the keyboard"),
                     status, (unsigned)passkey);
        } else {
            snprintf(line, sizeof(line), "%s", status);
        }
        lv_label_set_text(s_cfgBleKbdStatus, line);
    }

    const int count = bleKeyboardScanCount();
    if (s_cfgBleKbdSelection >= count) s_cfgBleKbdSelection = count > 0 ? count - 1 : 0;
    if (s_cfgBleKbdSelection < 0) s_cfgBleKbdSelection = 0;

    lv_obj_clean(s_cfgBleKbdList);
    if (count == 0) {
        lv_obj_t *row = lv_label_create(s_cfgBleKbdList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
        lv_label_set_text(row,
                          bleKeyboardScanning()
                              ? TR("Looking for keyboards...")
                              : TR("No keyboards found yet.\n\nPut the keyboard into pairing "
                                "mode, then press Scan.\n\nOnly Bluetooth LE keyboards can "
                                "pair - this hardware has no Bluetooth Classic radio."));
        return;
    }

    for (int i = 0; i < count; i++) {
        char name[32] = {0};
        char addr[20] = {0};
        int  rssi = 0;
        bool hid = false;
        if (!bleKeyboardScanEntry(i, name, sizeof(name), addr, sizeof(addr), &rssi, &hid)) continue;

        char rowText[96];
        snprintf(rowText, sizeof(rowText), "%s  %d dBm%s", name, rssi, hid ? "" : "  ?");

        lv_obj_t *row = lv_label_create(s_cfgBleKbdList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_set_style_pad_bottom(row, 4, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex((i & 1) ? 0x123266 : 0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, rowText);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, onCfgBleKbdRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (i == s_cfgBleKbdSelection) {
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
        }
    }
}

static void cfgBleKbdPairSelected() {
    if (bleKeyboardScanCount() <= 0) return;
    if (!bleKeyboardPairWith(s_cfgBleKbdSelection)) return;
    refreshCfgBleKbdModal();
}

static void cfgBleKbdForget() {
    bleKeyboardForget();
    s_cfg.bleKbdAddr[0] = '\0';
    s_cfg.bleKbdName[0] = '\0';
    s_cfg.bleKbdAddrType = 0;
    markConfigDirty();
    refreshCfgBleKbdModal();
}

static void onCfgBleKbdRowPressed(lv_event_t *e) {
    s_cfgBleKbdSelection = (int)(intptr_t)lv_event_get_user_data(e);
    refreshCfgBleKbdModal();
}

static void onCfgBleKbdBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_cfgBleKbdBackdrop) return;
    closeCfgBleKbdModal();
    refreshCfgModal();
}

static void onCfgBleKbdClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeCfgBleKbdModal();
    refreshCfgModal();
}

static void onCfgBleKbdScanPressed(lv_event_t *e) {
    LV_UNUSED(e);
    if (bleKeyboardStartScan()) s_cfgBleKbdAutoScanned = true;
    refreshCfgBleKbdModal();
}

static void onCfgBleKbdPairPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgBleKbdPairSelected();
}

static void onCfgBleKbdForgetPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgBleKbdForget();
}

static void openCfgBleKbdModal(bool autoScan) {
    if (!s_rootScreen || s_cfgBleKbdModal || s_cfgBleKbdBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 340) modalW = 340;

    s_cfgBleKbdSelection = 0;

    s_cfgBleKbdBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgBleKbdBackdrop, w, h);
    lv_obj_align(s_cfgBleKbdBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgBleKbdBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgBleKbdBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgBleKbdBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgBleKbdBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgBleKbdBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgBleKbdBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgBleKbdBackdrop, onCfgBleKbdBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgBleKbdModal = lv_obj_create(s_cfgBleKbdBackdrop);
    lv_obj_set_size(s_cfgBleKbdModal, modalW, (h > 120) ? (h - 20) : LV_SIZE_CONTENT);
    lv_obj_align(s_cfgBleKbdModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgBleKbdModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgBleKbdModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgBleKbdModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgBleKbdModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgBleKbdModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgBleKbdModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgBleKbdModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgBleKbdModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgBleKbdModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgBleKbdModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgBleKbdBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgBleKbdModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Bluetooth Keyboard"));

#if UI_TOUCH_ONLY_PROFILE
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_cfgBleKbdModal, onCfgBleKbdClosePressed);
#endif

    s_cfgBleKbdStatus = lv_label_create(s_cfgBleKbdModal);
    lv_obj_set_width(s_cfgBleKbdStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgBleKbdStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgBleKbdStatus, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgBleKbdStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_cfgBleKbdStatus, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_cfgBleKbdStatus, "");

    s_cfgBleKbdList = lv_obj_create(s_cfgBleKbdModal);
    lv_obj_set_width(s_cfgBleKbdList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgBleKbdList, 1);
    lv_obj_add_flag(s_cfgBleKbdList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgBleKbdList);
    lv_obj_set_scrollbar_mode(s_cfgBleKbdList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgBleKbdList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgBleKbdList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgBleKbdList, 1, 0);
    lv_obj_set_style_border_color(s_cfgBleKbdList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgBleKbdList, 2, 0);
    lv_obj_set_style_pad_row(s_cfgBleKbdList, 2, 0);
    lv_obj_set_flex_flow(s_cfgBleKbdList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgBleKbdList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    // Buttons, not key hints: this board has no keyboard of its own, so until
    // one is paired the only way to drive this dialog is the touch panel. Only
    // the three actions live here — closing is the corner X, as everywhere.
    lv_obj_t *btnRow = lv_obj_create(s_cfgBleKbdModal);
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

    struct BleKbdButton { const char *label; lv_event_cb_t cb; };
    const BleKbdButton kButtons[] = {
        { TR_NOOP("Scan"),   onCfgBleKbdScanPressed   },
        { TR_NOOP("Pair"),   onCfgBleKbdPairPressed   },
        { TR_NOOP("Forget"), onCfgBleKbdForgetPressed },
    };
    for (const BleKbdButton &b : kButtons) {
        lv_obj_t *btn = lv_btn_create(btnRow);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, lv_pct(100));
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_add_event_cb(btn, b.cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_label_set_text(lbl, TR(b.label));
        lv_obj_center(lbl);
    }

    refreshCfgBleKbdModal();
    // Opening the dialog is the moment someone is looking for a keyboard, so
    // start looking without making them press Scan first -- except when it
    // opens to show a re-pair's passkey, where a scan would only get in the way.
    s_cfgBleKbdAutoScanned = autoScan ? bleKeyboardStartScan() : true;
}

// ── Lost pairing prompt ─────────────────────────────────────────────────────
// The module never re-pairs a bonded keyboard on its own (see ble_keyboard.cpp,
// refuseBondedPeer()): a keyboard that comes back without its bond is either
// one the user reset, or something else using its address, and only the user
// can tell which. So the question is put here, with the safe answer selected.
static void closeBleKbdRepairPrompt() {
    if (lvObjValid(s_bleKbdRepairBackdrop)) {
        lv_obj_del(s_bleKbdRepairBackdrop);
    } else if (lvObjValid(s_bleKbdRepairModal)) {
        lv_obj_del(s_bleKbdRepairModal);
    }
    s_bleKbdRepairBackdrop = nullptr;
    s_bleKbdRepairModal = nullptr;
    s_bleKbdRepairBtns[0] = nullptr;
    s_bleKbdRepairBtns[1] = nullptr;
}

static void bleKbdRepairPaintSelection() {
    for (int i = 0; i < 2; i++) {
        if (!lvObjValid(s_bleKbdRepairBtns[i])) continue;
        const bool sel = (i == s_bleKbdRepairSel);
        lv_obj_set_style_border_width(s_bleKbdRepairBtns[i], sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_bleKbdRepairBtns[i], lv_color_hex(0xFFFFFF), 0);
    }
}

static void bleKbdRepairMoveSelection() {
    s_bleKbdRepairSel = s_bleKbdRepairSel ? 0 : 1;
    bleKbdRepairPaintSelection();
}

static void bleKbdRepairAccept() {
    closeBleKbdRepairPrompt();
    if (!bleKeyboardRepair()) {
        refreshCfgModal();
        return;
    }
    Serial.println("[blekbd] re-pair confirmed on device");
    // The pairing dialog is where the passkey is shown; a re-pair needs it as
    // much as a first pairing does.
    openCfgBleKbdModal(false);
    refreshCfgModal();
}

static void bleKbdRepairIgnore() {
    Serial.println("[blekbd] re-pair declined; keyboard stays unpaired, retries stopped");
    closeBleKbdRepairPrompt();
    refreshCfgModal();
}

static void onBleKbdRepairAcceptPressed(lv_event_t *e) {
    LV_UNUSED(e);
    bleKbdRepairAccept();
}

static void onBleKbdRepairIgnorePressed(lv_event_t *e) {
    LV_UNUSED(e);
    bleKbdRepairIgnore();
}

static void openBleKbdRepairPrompt() {
    if (!s_rootScreen || s_bleKbdRepairModal || s_bleKbdRepairBackdrop) return;
    if (!bleKeyboardRepairNeeded()) return;

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

    s_bleKbdRepairSel = 1;

    s_bleKbdRepairBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_bleKbdRepairBackdrop, screenW, screenH);
    lv_obj_align(s_bleKbdRepairBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_bleKbdRepairBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bleKbdRepairBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_bleKbdRepairBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_bleKbdRepairBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_bleKbdRepairBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_bleKbdRepairBackdrop, 0, 0);

    s_bleKbdRepairModal = lv_obj_create(s_bleKbdRepairBackdrop);
    lv_obj_set_size(s_bleKbdRepairModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_bleKbdRepairModal,
                                (screenH > 40) ? (screenH - 12) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_bleKbdRepairModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_bleKbdRepairModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bleKbdRepairModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_bleKbdRepairModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_bleKbdRepairModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bleKbdRepairModal, 1, 0);
    lv_obj_set_style_border_color(s_bleKbdRepairModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_bleKbdRepairModal, 8, 0);
    lv_obj_set_style_pad_row(s_bleKbdRepairModal, 7, 0);
    lv_obj_set_flex_flow(s_bleKbdRepairModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_bleKbdRepairModal, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_bleKbdRepairBackdrop);

    lv_obj_t *title = lv_label_create(s_bleKbdRepairModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, textColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Bluetooth Keyboard"));

    const char *name = bleKeyboardPairedName();
    lv_obj_t *body = lv_label_create(s_bleKbdRepairModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(body, textColor, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(body,
                          TR("Keyboard \"%s\" lost its pairing. Re-pair only if you reset "
                             "the keyboard yourself."),
                          (name && name[0]) ? name : TR("keyboard"));

    char reason[48];
    bleKeyboardRepairReason(reason, sizeof(reason));
    if (reason[0]) {
        lv_obj_t *why = lv_label_create(s_bleKbdRepairModal);
        lv_obj_set_width(why, lv_pct(100));
        lv_obj_set_style_text_font(why, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(why, mutedColor, 0);
        lv_obj_set_style_text_align(why, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(why, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(why, TR("Reason: %s"), reason);
    }

    lv_obj_t *buttonRow = lv_obj_create(s_bleKbdRepairModal);
    lv_obj_set_width(buttonRow, lv_pct(100));
    lv_obj_set_height(buttonRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(buttonRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(buttonRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(buttonRow, 0, 0);
    lv_obj_set_style_pad_all(buttonRow, 2, 0);
    lv_obj_set_style_pad_column(buttonRow, 10, 0);
    lv_obj_set_flex_flow(buttonRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonRow, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto makeButton = [](lv_obj_t *parent, const char *text, uint32_t color,
                         lv_event_cb_t callback) {
        lv_obj_t *button = lv_btn_create(parent);
        lv_obj_set_height(button, 32);
        lv_obj_set_style_min_width(button, 86, 0);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(label, TR(text));
        lv_obj_center(label);
        return button;
    };
    // Re-pair gets the cautionary colour: it is the answer that hands a
    // keyboard input to whatever completes the next pairing.
#if UI_TOUCH_ONLY_PROFILE
    s_bleKbdRepairBtns[0] = makeButton(buttonRow, TR_NOOP("Re-pair"),
                                       lightUi ? 0xB07028 : 0x7A4A12, onBleKbdRepairAcceptPressed);
    s_bleKbdRepairBtns[1] = makeButton(buttonRow, TR_NOOP("Ignore"),
                                       lightUi ? 0x5B7BA8 : 0x2C4A7A, onBleKbdRepairIgnorePressed);
#else
    s_bleKbdRepairBtns[0] = makeButton(buttonRow, TR_NOOP("(R)e-pair"),
                                       lightUi ? 0xB07028 : 0x7A4A12, onBleKbdRepairAcceptPressed);
    s_bleKbdRepairBtns[1] = makeButton(buttonRow, TR_NOOP("(I)gnore"),
                                       lightUi ? 0x5B7BA8 : 0x2C4A7A, onBleKbdRepairIgnorePressed);
#endif
    bleKbdRepairPaintSelection();
}

// Loop hook: raises the prompt once per refusal, waiting out the dialogs that
// must not be covered, and drops it if the question went away underneath it
// (feature switched off, keyboard forgotten).
static void serviceBleKbdRepairPrompt() {
    if (s_bleKbdRepairModal) {
        if (!bleKeyboardRepairNeeded()) closeBleKbdRepairPrompt();
        return;
    }
    if (!s_rootScreen || s_onboardingModal || s_otaPromptModal || s_cfgConfirmModal) return;
#if HAS_STATE_MAPS
    if (s_legacyMapPromptModal) return;
#endif
    if (bleKeyboardTakeRepairPrompt()) openBleKbdRepairPrompt();
}
#endif  // HAS_BLE_KEYBOARD

static void openCfgWifiScanModal() {
    if (!s_rootScreen || s_cfgWifiScanModal || s_cfgWifiScanBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 340) modalW = 340;

    s_cfgWifiScanBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiScanBackdrop, w, h);
    lv_obj_align(s_cfgWifiScanBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiScanBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiScanBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiScanBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiScanBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgWifiScanBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiScanBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiScanBackdrop, onCfgWifiScanBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiScanModal = lv_obj_create(s_cfgWifiScanBackdrop);
    lv_obj_set_size(s_cfgWifiScanModal, modalW, (h > 120) ? (h - 20) : LV_SIZE_CONTENT);
    lv_obj_align(s_cfgWifiScanModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiScanModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiScanModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiScanModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiScanModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiScanModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiScanModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgWifiScanModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgWifiScanModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgWifiScanModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiScanModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgWifiScanBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiScanModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Scan Networks"));

    s_cfgWifiScanStatus = lv_label_create(s_cfgWifiScanModal);
    lv_obj_set_width(s_cfgWifiScanStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgWifiScanStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgWifiScanStatus, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgWifiScanStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_cfgWifiScanStatus,
#if UI_TOUCH_ONLY_PROFILE
                      "Pick a network, then Connect"
#else
                      "Enter=Connect  N=Rescan  Backspace=Back"
#endif
    );

    s_cfgWifiScanList = lv_obj_create(s_cfgWifiScanModal);
    lv_obj_set_width(s_cfgWifiScanList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgWifiScanList, 1);
    lv_obj_add_flag(s_cfgWifiScanList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgWifiScanList);
    lv_obj_set_scrollbar_mode(s_cfgWifiScanList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgWifiScanList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiScanList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiScanList, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiScanList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgWifiScanList, 2, 0);
    lv_obj_set_style_pad_row(s_cfgWifiScanList, 2, 0);
    lv_obj_set_flex_flow(s_cfgWifiScanList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiScanList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiScanModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onCfgWifiScanCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, TR("Cancel"));
    lv_obj_center(cancelLbl);

    lv_obj_t *rescanBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(rescanBtn, 1);
    lv_obj_set_height(rescanBtn, lv_pct(100));
    lv_obj_add_event_cb(rescanBtn, onCfgWifiScanRescanPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *rescanLbl = lv_label_create(rescanBtn);
    lv_obj_set_style_text_font(rescanLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(rescanLbl, TR("Rescan"));
    lv_obj_center(rescanLbl);

    lv_obj_t *connectBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(connectBtn, 1);
    lv_obj_set_height(connectBtn, lv_pct(100));
    lv_obj_add_event_cb(connectBtn, onCfgWifiScanConnectPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connectLbl = lv_label_create(connectBtn);
    lv_obj_set_style_text_font(connectLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(connectLbl, TR("Connect"));
    lv_obj_center(connectLbl);
#endif

    refreshCfgWifiScanModal(true);
}

static void openCfgWifiPickerModal(bool forOnboarding) {
    if (!s_rootScreen || s_cfgWifiModal || s_cfgWifiBackdrop) return;
    s_cfgWifiPickerOnboardingMode = forOnboarding;
    populateKnownWifiEntries();

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 340) modalW = 340;

    s_cfgWifiBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiBackdrop, w, h);
    lv_obj_align(s_cfgWifiBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgWifiBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiBackdrop, onCfgWifiBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiModal = lv_obj_create(s_cfgWifiBackdrop);
    lv_obj_set_size(s_cfgWifiModal, modalW, (h > 120) ? (h - 20) : LV_SIZE_CONTENT);
    lv_obj_align(s_cfgWifiModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgWifiModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgWifiModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgWifiModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgWifiBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Choose WiFi"));

    s_cfgWifiHint = lv_label_create(s_cfgWifiModal);
    lv_obj_set_width(s_cfgWifiHint, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgWifiHint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgWifiHint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgWifiHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_cfgWifiHint,
#if UI_TOUCH_ONLY_PROFILE
                      "Pick known WiFi or tap New"
#else
                      "Enter=Select  N=New  D=Delete  Bksp=Cancel"
#endif
    );

    s_cfgWifiList = lv_obj_create(s_cfgWifiModal);
    lv_obj_set_width(s_cfgWifiList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgWifiList, 1);
    lv_obj_add_flag(s_cfgWifiList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgWifiList);
    lv_obj_set_scrollbar_mode(s_cfgWifiList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgWifiList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiList, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgWifiList, 2, 0);
    lv_obj_set_style_pad_row(s_cfgWifiList, 2, 0);
    lv_obj_set_flex_flow(s_cfgWifiList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onCfgWifiCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, TR("Cancel"));
    lv_obj_center(cancelLbl);

    lv_obj_t *newBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(newBtn, 1);
    lv_obj_set_height(newBtn, lv_pct(100));
    lv_obj_add_event_cb(newBtn, onCfgWifiOpenScanPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *newLbl = lv_label_create(newBtn);
    lv_obj_set_style_text_font(newLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(newLbl, TR("New"));
    lv_obj_center(newLbl);
#endif

    refreshCfgWifiPickerModal();
}

// ── Node name editor ─────────────────────────────────────────────────────────
// The long and short name in one modal, because they are one setting: a short
// name is only ever picked against the long name it abbreviates. Nothing
// reaches s_cfg until Save, so an abandoned edit leaves the identity the rest
// of the mesh knows untouched.
static void onboardingDeriveShortFromLong(const char *longName, char *shortOut, size_t shortLen);

// ── Config -> Preset ─────────────────────────────────────────────────────────
// The device's own modem preset, changed from the device. Until now the only
// way to move a node off its preset was the web form, which needs a second
// machine on the same network; this is the route that needs neither.
//
// Committing reboots, which is why the row sits behind a modal rather than
// cycling in place like the On/Off rows do: a preset change is a different
// radio configuration, and half of it applied is a node that hears nothing.
//
// Only presets this radio can actually run are offered. presetUsableOnThisRadio()
// excludes the 15.6 kHz Tiny pair on hardware without both a TCXO and an SX126x,
// which would otherwise accept the setting, reconfigure, and go silent -- the
// one failure mode a picker must not be able to produce.
static lv_obj_t *s_cfgPresetBackdrop = nullptr;
static lv_obj_t *s_cfgPresetModal = nullptr;
static lv_obj_t *s_cfgPresetRows[PRESET_COUNT] = {};
static uint8_t   s_cfgPresetChoices[PRESET_COUNT] = {};
static int       s_cfgPresetCount = 0;
static int       s_cfgPresetSelection = 0;

static void closeCfgPresetModal() {
    if (lvObjValid(s_cfgPresetBackdrop)) {
        lv_obj_del(s_cfgPresetBackdrop);
    } else if (lvObjValid(s_cfgPresetModal)) {
        lv_obj_del(s_cfgPresetModal);
    }
    s_cfgPresetBackdrop = nullptr;
    s_cfgPresetModal = nullptr;
    memset(s_cfgPresetRows, 0, sizeof(s_cfgPresetRows));
    s_cfgPresetCount = 0;
    s_cfgPresetSelection = 0;
}

static void refreshCfgPresetSelection() {
    if (!s_cfgPresetModal) return;
    paintPickerRows(s_cfgPresetRows, s_cfgPresetCount, s_cfgPresetSelection, PICKER_SCROLL_VIEW);
}

// Applies the preset and restarts. Does not return.
static void cfgPresetCommit(int idx) {
    if (idx < 0 || idx >= s_cfgPresetCount) return;
    const uint8_t preset = s_cfgPresetChoices[idx];

    // Taken before the modal goes, since closing clears the choice table.
    const char *name = kPresets[preset].name;

    // Already on it: nothing to write and nothing worth a reboot for. Closing
    // is the whole response -- rebooting to arrive where we already are would
    // look like the picker had misunderstood.
    if (s_cfg.loraUsePreset && s_cfg.modemPreset == preset) {
        closeCfgPresetModal();
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Preset already %s."), name);
        refreshCfgModal();
        return;
    }

    closeCfgPresetModal();

    s_cfg.modemPreset = preset;
    // Picking a preset is also how you leave custom modem settings. Without
    // this the custom BW/SF/CR would stay in force and the row would name a
    // preset the radio is not running.
    s_cfg.loraUsePreset = true;
    // Re-derives loraFreq/BW/SF/CR from region + preset, and coerces anything
    // this radio cannot be tuned to.
    applyPresetParams(s_cfg);
    persistConfigToPrefs();

    snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Preset: %s - rebooting..."), name);
    refreshCfgModal();
    // Paint the status before going, exactly as the MQTT toggle does: a device
    // that reboots with no explanation reads as a crash.
    lv_timer_handler();
    delay(1000);
    flushPersistentState();   // settings and transcripts must land before we go
    ESP.restart();
}

// Names the destination and asks, instead of committing. The picker stays open
// underneath, so No lands back on the list rather than on the Config screen.
static void cfgPresetAsk(int idx) {
    if (idx < 0 || idx >= s_cfgPresetCount) return;
    const uint8_t preset = s_cfgPresetChoices[idx];
    // Already the one in force: nothing to confirm, and the commit says so.
    if (s_cfg.loraUsePreset && s_cfg.modemPreset == preset) {
        cfgPresetCommit(idx);
        return;
    }
    snprintf(s_cfgConfirmText, sizeof(s_cfgConfirmText),
             TR("Switch to %s and reboot?"), kPresets[preset].name);
    openCfgConfirmModal(-1, s_cfgConfirmText,
                        [](int i) { cfgPresetCommit(i); }, idx);
}

static void onCfgPresetRowPressed(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_cfgPresetCount) return;
    // A tap commits here, unlike the Discovery picker where it only selects.
    // The costs are the other way round: there a stray tap parks the node off
    // its own mesh for five minutes with no further confirmation, while this
    // announces what it did and is undone by picking the previous row again.
    s_cfgPresetSelection = idx;
    refreshCfgPresetSelection();
    cfgPresetAsk(idx);
}

static void openCfgPresetModal() {
    if (!s_rootScreen) return;
    if (s_cfgPresetModal || s_cfgPresetBackdrop) return;

    s_cfgPresetCount = 0;
    s_cfgPresetSelection = 0;
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (!presetUsableOnThisRadio(i)) continue;
        // Start on the one in force, so Enter without moving is a no-op rather
        // than a reboot onto whatever happened to be first.
        if (s_cfg.loraUsePreset && s_cfg.modemPreset == i) {
            s_cfgPresetSelection = s_cfgPresetCount;
        }
        s_cfgPresetChoices[s_cfgPresetCount++] = i;
    }
    if (s_cfgPresetCount == 0) return;   // no radio can do this, but be safe

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    s_cfgPresetBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgPresetBackdrop, w, h);
    lv_obj_align(s_cfgPresetBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgPresetBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgPresetBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgPresetBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgPresetBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgPresetBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgPresetBackdrop, 0, 0);
    // Tapping outside is the close gesture on the touch-only builds, which have
    // no close key to press.
    lv_obj_add_event_cb(s_cfgPresetBackdrop,
                        [](lv_event_t *e) {
                            if (lv_event_get_target_obj(e) != s_cfgPresetBackdrop) return;
                            closeCfgPresetModal();
                        },
                        LV_EVENT_CLICKED, nullptr);

    s_cfgPresetModal = lv_obj_create(s_cfgPresetBackdrop);
    lv_obj_set_size(s_cfgPresetModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgPresetModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgPresetModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgPresetModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgPresetModal, LV_OBJ_FLAG_CLICKABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_cfgPresetModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_cfgPresetModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_cfgPresetModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_cfgPresetModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_cfgPresetModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgPresetModal, 1, 0);
    lv_obj_set_style_pad_all(s_cfgPresetModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgPresetModal, 5, 0);
    lv_obj_set_flex_flow(s_cfgPresetModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgPresetModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgPresetBackdrop);

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

    lv_obj_t *title = lv_label_create(s_cfgPresetModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, titleColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Modem preset"));

#if UI_TOUCH_ONLY_PROFILE
    // A corner X, because the backdrop tap is a gesture you have to already
    // know about and this modal is one you may well want to leave without
    // choosing anything -- every row in it reboots the device. Reserving the
    // row first keeps the floating button from hanging over the warning below.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_cfgPresetModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeCfgPresetModal(); });
#endif

    // Says the two things that are not undoable by looking: that choosing
    // restarts the device, and that the mesh has to agree for any of it to work.
    lv_obj_t *warn = lv_label_create(s_cfgPresetModal);
    lv_obj_set_width(warn, lv_pct(100));
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(warn, hintColor, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
    lv_label_set_text(warn, TR("Asks first, then reboots. Only nodes on the same "
                            "preset can hear each other."));

    lv_obj_t *list = lv_obj_create(s_cfgPresetModal);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    // Capped so the hint below stays on screen rather than scrolling away with
    // the rows on the shorter panels.
    lv_obj_set_style_max_height(list, (h > 150) ? (h - 104) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(list);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_style_pad_right(list, 2, 0);

    for (int i = 0; i < s_cfgPresetCount; i++) {
        const uint8_t idx = s_cfgPresetChoices[i];
        const PresetParams &p = kPresets[idx];
        const bool current = s_cfg.loraUsePreset && s_cfg.modemPreset == idx;

        lv_obj_t *row = lv_btn_create(list);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_cfgPresetRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onCfgPresetRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowColor, 0);
        // Marked in the text as well as by the highlight: the highlight also
        // means "where the selection is", and those part company the moment
        // anyone presses Down.
        if (current) lv_label_set_text_fmt(name, TR("%s  (current)"), p.name);
        else         lv_label_set_text(name, p.name);

        if (kModalRowDescriptions) {
            lv_obj_t *desc = lv_label_create(row);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(desc, rowColor, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
#else
            lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
#endif
            // The on-air channel name is the part that has to match the rest of
            // the mesh, so it leads.
            lv_label_set_text_fmt(desc, TR("%s  SF%u  %.0f kHz"),
                                  p.channelName, (unsigned)p.sf, (double)p.bw);
        }
    }

    lv_obj_t *hint = lv_label_create(s_cfgPresetModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    // Just what a tap does now: the corner X above says how to leave, so this
    // no longer has to carry that too. Tapping the backdrop still works for
    // anyone who already reaches for it.
    lv_label_set_text(hint, TR("Tap a preset to apply."));
#else
    lv_label_set_text_fmt(hint, TR("Move  Enter=Apply  %s=Cancel"), modalCloseKeyLabel());
#endif

    refreshCfgPresetSelection();
}

// ── Config -> Mesh Beacons ──────────────────────────────────────────────────
// Listening for other meshes' beacons and broadcasting our own, in one popup
// built like the preset picker: a list of rows, Enter (or a tap) changes the
// one under the cursor. Toggles flip in place; the two pickers reuse the shared
// slider modal and the message reuses the channel text-entry modal, so every
// board's keys, trackball, d-pad and touch work here the way they do there.
// Each change is saved at once (markConfigDirty) and reschedules the beacon.
enum MbCfgRow : uint8_t {
    MB_CFG_LISTEN = 0,
    MB_CFG_BROADCAST,
    MB_CFG_TARGET,
    MB_CFG_OFFER_KEY,
    MB_CFG_MESSAGE,
    MB_CFG_INTERVAL,
    MB_CFG_ROW_COUNT
};

static const char *const kMbCfgRowNames[MB_CFG_ROW_COUNT] = {
    TR_NOOP("Listen"),
    TR_NOOP("Broadcast"),
    TR_NOOP("Target preset"),
    TR_NOOP("Include key"),
    TR_NOOP("Message"),
    TR_NOOP("Interval"),
};

static const uint8_t kMbIntervalsH[] = { 1, 2, 3, 6, 12, 24 };
static constexpr int kMbIntervalCount = (int)(sizeof(kMbIntervalsH) / sizeof(kMbIntervalsH[0]));

static lv_obj_t *s_mbCfgBackdrop = nullptr;
static lv_obj_t *s_mbCfgModal = nullptr;
static lv_obj_t *s_mbCfgRows[MB_CFG_ROW_COUNT] = {};
static lv_obj_t *s_mbCfgValues[MB_CFG_ROW_COUNT] = {};
static lv_obj_t *s_mbCfgWarn = nullptr;
static int       s_mbCfgSelection = 0;
static uint8_t   s_mbCfgPresetChoices[PRESET_COUNT] = {};
static int       s_mbCfgPresetCount = 0;

