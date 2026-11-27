// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: system stats modal, touch bottom nav, chat panel styling.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// ── Hidden system-stats screen (system / memory / storage) ───────────────────
// Builds three ready-to-display section bodies. Kept as separate strings so a
// wide screen can lay them out side by side and a narrow one can stack them.
static void buildSysStatsColumns(char *cpuOut, char *memOut, char *stoOut, size_t sz) {
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t heapFree  = ESP.getFreeHeap();
    const uint32_t heapUsed  = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
    const uint32_t heapPct   = heapTotal ? (uint32_t)((uint64_t)heapUsed * 100 / heapTotal) : 0;
    const uint32_t psramTotal = ESP.getPsramSize();
    const uint32_t psramFree  = ESP.getFreePsram();
    const uint32_t upSec = millis() / 1000UL;

    // Battery reads the same filtered values the header icon shows, so a
    // disagreement between this screen and the icon would itself be a bug. The
    // voltage is the useful half: on boards that read through a charger IC it
    // is quantised (20 mV steps on the Pager's BQ25896), so a percentage that
    // moves in jumps is explained by the voltage sitting on those steps.
    const float battV = batteryReadVoltage();
    const uint8_t battPct = batteryReadPercent();

    snprintf(cpuOut, sz,
             TR("%s x%u\n"
             "Clock: %u MHz\n"
             "Revision: %u\n"
             "Uptime: %luh %02lum %02lus\n"
             "Loop rate: %lu /s\n"
             "Battery: %u%% %.3fV"),
             ESP.getChipModel(), (unsigned)ESP.getChipCores(),
             (unsigned)ESP.getCpuFreqMHz(),
             (unsigned)ESP.getChipRevision(),
             (unsigned long)(upSec / 3600UL),
             (unsigned long)((upSec / 60UL) % 60UL),
             (unsigned long)(upSec % 60UL),
             (unsigned long)s_loopsPerSec,
             (unsigned)battPct, (double)battV);

    snprintf(memOut, sz,
             TR("Used: %lu KB (%lu%%)\n"
             "Total: %lu KB\n"
             "Free: %lu KB\n"
             "Min free: %lu KB\n"
             "Max block: %lu KB"),
             (unsigned long)(heapUsed / 1024UL), (unsigned long)heapPct,
             (unsigned long)(heapTotal / 1024UL),
             (unsigned long)(heapFree / 1024UL),
             (unsigned long)(ESP.getMinFreeHeap() / 1024UL),
             (unsigned long)(ESP.getMaxAllocHeap() / 1024UL));

    if (psramTotal > 0) {
        snprintf(stoOut, sz,
                 TR("PSRAM free: %lu KB\n"
                 "PSRAM total: %lu KB\n"
                 "Flash: %lu KB\n"
                 "Sketch: %lu KB\n"
                 "App free: %lu KB"),
                 (unsigned long)(psramFree / 1024UL),
                 (unsigned long)(psramTotal / 1024UL),
                 (unsigned long)(ESP.getFlashChipSize() / 1024UL),
                 (unsigned long)(ESP.getSketchSize() / 1024UL),
                 (unsigned long)(ESP.getFreeSketchSpace() / 1024UL));
    } else {
        snprintf(stoOut, sz,
                 TR("PSRAM: none\n"
                 "Flash: %lu KB\n"
                 "Sketch: %lu KB\n"
                 "App free: %lu KB"),
                 (unsigned long)(ESP.getFlashChipSize() / 1024UL),
                 (unsigned long)(ESP.getSketchSize() / 1024UL),
                 (unsigned long)(ESP.getFreeSketchSpace() / 1024UL));
    }
}

static void closeSysStatsModal() {
    lvObjDeleteSafe(s_sysStatsModal);
    s_sysStatsCols[0] = s_sysStatsCols[1] = s_sysStatsCols[2] = nullptr;
    s_sysStatsOpenedMs = 0;
    s_sysStatsLastRefreshMs = 0;
    s_cfgInfoKeyStreak = 0;
}

static void refreshSysStatsModal(bool force) {
    if (!s_sysStatsModal || !s_sysStatsCols[0]) return;
    if (!lvObjValid(s_sysStatsCols[0])) { s_sysStatsCols[0] = nullptr; return; }
    uint32_t now = millis();
    if (!force && (uint32_t)(now - s_sysStatsLastRefreshMs) < 500UL) return;
    s_sysStatsLastRefreshMs = now;

    // One size for all three: buildSysStatsColumns() takes a single length and
    // applies it to every buffer, so the smallest is what it must be told.
    char cpu[192], mem[192], sto[192];
    buildSysStatsColumns(cpu, mem, sto, sizeof(cpu));

    if (s_sysStatsCols[1] && s_sysStatsCols[2]) {
        // Wide layout: one label per section.
        lv_label_set_text(s_sysStatsCols[0], cpu);
        lv_label_set_text(s_sysStatsCols[1], mem);
        lv_label_set_text(s_sysStatsCols[2], sto);
    } else {
        // Narrow layout: everything stacked in the single scrollable label.
        char body[640];
        snprintf(body, sizeof(body),
                 "SYSTEM\n%s\n\nMEMORY\n%s\n\nSTORAGE\n%s", cpu, mem, sto);
        lv_label_set_text(s_sysStatsCols[0], body);
    }
}

// Builds one titled "card" column inside a flex-row parent and returns its
// body label (the part refreshSysStatsModal updates).
static lv_obj_t *makeSysStatsColumn(lv_obj_t *parent, const char *header,
                                    const lv_font_t *headFont, const lv_font_t *bodyFont) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_height(col, lv_pct(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(col, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_50, 0);
    lv_obj_set_style_border_width(col, 1, 0);
    lv_obj_set_style_border_color(col, lv_color_hex(0x2C5A9E), 0);
    lv_obj_set_style_radius(col, 4, 0);
    lv_obj_set_style_pad_all(col, 5, 0);
    lv_obj_set_style_pad_row(col, 3, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *head = lv_label_create(col);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_style_text_font(head, headFont, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(head, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(head, lv_color_hex(0x6BF0DC), 0);
#endif
    lv_label_set_text(head, header);

    lv_obj_t *body = lv_label_create(col);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, bodyFont, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(body, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9F7F0), 0);
#endif
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, "");
    return body;
}

static void openSysStatsModal() {
    if (!s_rootScreen || s_sysStatsModal) return;

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    // Wide screens (pager 480, T-Deck 320) get the full-width 3-column layout;
    // narrow screens fall back to a single stacked, scrollable list.
    const bool wide = screenW >= 300;

#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *bodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif
    const lv_font_t *headFont = &lv_font_montserrat_12;

    s_sysStatsModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_sysStatsModal, screenW - 8, screenH - 8);
    lv_obj_align(s_sysStatsModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_sysStatsModal, LV_OBJ_FLAG_SCROLLABLE);
    // Distinct teal accent so the hidden screen reads as "not a normal modal".
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_sysStatsModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_sysStatsModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_sysStatsModal, lv_color_hex(0x0B1E45), 0);
    lv_obj_set_style_border_color(s_sysStatsModal, lv_color_hex(0x39E0C8), 0);
#endif
    lv_obj_set_style_bg_opa(s_sysStatsModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sysStatsModal, 1, 0);
    lv_obj_set_style_pad_all(s_sysStatsModal, 6, 0);
    lv_obj_set_style_pad_row(s_sysStatsModal, 4, 0);
    lv_obj_set_flex_flow(s_sysStatsModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sysStatsModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_sysStatsModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(0x6BF0DC), 0);
#endif
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("System Stats"));

    if (wide) {
        // A flex-row band that grows to fill the space between title and hint,
        // holding three equal-width section cards.
        lv_obj_t *cols = lv_obj_create(s_sysStatsModal);
        lv_obj_set_width(cols, lv_pct(100));
        lv_obj_set_flex_grow(cols, 1);
        lv_obj_clear_flag(cols, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(cols, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cols, 0, 0);
        lv_obj_set_style_pad_all(cols, 0, 0);
        lv_obj_set_style_pad_column(cols, 6, 0);
        lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        // "SYSTEM" rather than "CPU": the column carries uptime, loop rate and
        // now battery alongside the chip identity.
        s_sysStatsCols[0] = makeSysStatsColumn(cols, "SYSTEM", headFont, bodyFont);
        s_sysStatsCols[1] = makeSysStatsColumn(cols, "MEMORY", headFont, bodyFont);
        s_sysStatsCols[2] = makeSysStatsColumn(cols, "STORAGE", headFont, bodyFont);
    } else {
        lv_obj_t *panel = lv_obj_create(s_sysStatsModal);
        lv_obj_set_width(panel, lv_pct(100));
        lv_obj_set_flex_grow(panel, 1);
        setupVScroll(panel);
        lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);

        s_sysStatsCols[0] = lv_label_create(panel);
        lv_obj_set_width(s_sysStatsCols[0], lv_pct(100));
        lv_obj_set_style_text_font(s_sysStatsCols[0], bodyFont, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(s_sysStatsCols[0], lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(s_sysStatsCols[0], lv_color_hex(0xD9F7F0), 0);
    #endif
        lv_label_set_long_mode(s_sysStatsCols[0], LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_sysStatsCols[0], "");
        s_sysStatsCols[1] = nullptr;
        s_sysStatsCols[2] = nullptr;
    }

#if UI_TOUCH_ONLY_PROFILE
    // Reachable here only through a keyboard driven over VNC (five presses of
    // I), so without this the one way out was the keyboard that opened it —
    // and whoever walked over to the device itself was stuck.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_sysStatsModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeSysStatsModal(); });
#else
    lv_obj_t *hint = lv_label_create(s_sysStatsModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(hint, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7FD8CC), 0);
#endif
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("Any key / %s = Close"), modalCloseKeyLabel());
#endif

    s_sysStatsOpenedMs = millis();
    lv_obj_move_foreground(s_sysStatsModal);
    refreshSysStatsModal(true);
    Serial.println("[lvgl-cfg] system-stats easter egg opened");
}

static void closeLegendModal() {
    lvObjDeleteSafe(s_legendModal);
    s_legendScroll = nullptr;
    // As above: Help carries a bar since issue #95, so it owns a status cluster
    // that dies with it.
    clearNavBarStatusIcons();
    refreshChatComposeButtonState();
}

static void onHeltecBottomNavPressed(lv_event_t *e) {
#if UI_TOUCH_NAV_BAR
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    // The screens Tools opens carry this bar on every build, with no X of
    // their own, so any cell has to take down whichever one it was tapped from
    // before going anywhere -- left standing, it would sit under the
    // destination and come back when that closed. Live is closed only by the
    // cells that already did (Home, Chat), and, upright on the stack-layout
    // boards, by Tools itself.
    if (target != HELTEC_NAV_TOOLS) closeToolScreens(false);
    switch (target) {
        case HELTEC_NAV_CFG:
            if (s_cfgModal) closeCfgModal();
            else openCfgModal();
            break;
        case HELTEC_NAV_DM:
            if (s_dmModal) closeDmModal();
            else openDmModal();
            break;
        case HELTEC_NAV_NODES:
            if (s_nodesModal) closeNodesModal();
            else openNodesModal();
            break;
        case HELTEC_NAV_LIVE:
            if (s_liveModal) closeLiveModal();
            else openLiveModal();
            break;
        case HELTEC_NAV_TOOLS:
            if (s_liveToolsModal) {
                closeLiveToolsModal();
                break;
            }
            // From one of its tools: back to the list. Upright, Live is one of
            // them like any other; elsewhere it stays, as below.
#if HAS_TOOL_STACK_LAYOUT
            closeToolScreens(uiPortrait());   // toolStackLayout(), defined further down
#else
            closeToolScreens(false);
#endif
            // Clear the screens this cell can be tapped from before opening
            // Tools, as openNavToolsShortcut() does for the keyboard route.
            // Left standing, they stay under whatever tool is picked next, and
            // their key handlers run ahead of every tool surface's in
            // pumpKeyboardInput() and end in an unconditional continue — so
            // Discovery opened this way from Nodes, DMs or Config showed its
            // W/P/C/S hints while every one of those keys went to the screen
            // underneath. Live stays: its handler sits below the tools', and
            // backing out of a tool opened from Live is meant to land there.
#if HAS_WEATHER
            closeWeatherModal();
#endif
            if (s_nodesActionModal) closeNodesActionMenu();
            if (s_channelActionsModal) closeChannelActionsModal();
            if (s_legendModal) closeLegendModal();
            if (s_cfgModal) closeCfgModal();
            if (s_dmModal) closeDmModal();
            if (s_nodesModal) closeNodesModal();
            openLiveToolsModal();
            break;
        case HELTEC_NAV_ACTIONS:
            if (s_channelActionsModal) closeChannelActionsModal();
            else openChannelActionsModal();
            break;
        case HELTEC_NAV_LEGEND:
            if (s_legendModal) closeLegendModal();
            else openLegendModal();
            break;
        case HELTEC_NAV_HOME:
        case HELTEC_NAV_CHAT: {
            // Chat is the root screen; the nav-reachable screens are modals
            // layered over it. So "go home" is "close them", innermost first —
            // a menu opened from one of these screens has to go before the
            // screen it was opened from, or it is left floating over chat.
            //
            // Every one is checked rather than just the active screen's own:
            // Home is on the bar of all of them, and whichever is on top is the
            // only one the tap can be coming from anyway.
            //
            // Both cells share this teardown and differ only in what they leave
            // standing at the end of it.
            if (s_liveToolsModal) closeLiveToolsModal();
#if HAS_WEATHER
            // Unguarded, unlike its neighbours: s_weatherModal is defined with
            // the weather screen far below this function, and closeWeatherModal()
            // is a no-op when nothing is open anyway.
            closeWeatherModal();
#endif
            if (s_nodesActionModal) closeNodesActionMenu();
            if (s_channelActionsModal) closeChannelActionsModal();
            if (s_legendModal) closeLegendModal();
            if (s_cfgModal) closeCfgModal();
            if (s_dmModal) closeDmModal();
            if (s_nodesModal) closeNodesModal();
            if (s_liveModal) closeLiveModal();
#if HAS_HOME_DASHBOARD
            // The dashboard is a sibling of the chat screen rather than a modal
            // over it, so it survives the teardown above and has to be opened or
            // closed by name.
            if (target == HELTEC_NAV_CHAT) closeHomeDashboard();
            else                           openHomeDashboard();
#endif
            break;
        }
        default:
            break;
    }
#else
    LV_UNUSED(e);
#endif
}

static lv_color_t chatPanelBackgroundColor() {
    // Camillia Black means black. Its panelBg already is, but the accent blend
    // below would lift the chat area to a grey — the accent on this theme is
    // white — which is the one thing this theme exists to avoid. Not routed
    // through lv_color_hex(): that macro is the theme remapper, and here we
    // want the literal.
    if (s_cfg.uiTheme == UI_THEME_CAMELLIA_BLACK) {
        return lv_color_make(0x00, 0x00, 0x00);
    }
    if (s_cfg.uiMode == UI_MODE_LIGHT) {
        return lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, 52));
    }
    return lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, 34));
}

// The chat panel is normally translucent over the screen background. On the
// black theme that alpha is a liability rather than a look: anything that ever
// sits behind it would tint the "black", so it covers instead.
static lv_opa_t chatPanelBackgroundOpa() {
#if defined(DEVICE_TDECK_PRO)
    return LV_OPA_TRANSP;
#else
    return (s_cfg.uiTheme == UI_THEME_CAMELLIA_BLACK) ? LV_OPA_COVER : LV_OPA_60;
#endif
}

#if UI_TOUCH_NAV_BAR
// Paints one nav cell in its normal or active styling. Defined below with the
// rest of the bar; the unread flash under it has to be able to put a cell back
// the way it found it.
static void navBarStyleCell(lv_obj_t *btn, bool isActive);

// Which cell a bar is currently marking active, stored on the bar the same way
// each cell stores which destination it is: offset by one so 0 reads as "not
// recorded". The unread flash repaints a cell and has to put back the styling
// it replaced, and only the bar knows whether that cell was the lit one.
static constexpr int kNavBarNoActive = -1;

static void navBarNoteActive(lv_obj_t *bar, int activeTarget) {
    if (!bar || !lv_obj_is_valid(bar)) return;
    lv_obj_set_user_data(bar, (void *)(intptr_t)(activeTarget + 1));
}

static int navBarActiveTarget(lv_obj_t *bar) {
    if (!bar || !lv_obj_is_valid(bar)) return kNavBarNoActive;
    const intptr_t tag = (intptr_t)lv_obj_get_user_data(bar);
    return (tag == 0) ? kNavBarNoActive : (int)(tag - 1);
}
#endif  // UI_TOUCH_NAV_BAR

#if UI_TOUCH_NAV_BAR
// The DM cell's glyph, for every nav bar that currently exists — the chat
// screen's, plus one on whatever full-screen modal is sitting over it. Recorded
// as the bars are built rather than searched for at blink time: the blink runs
// at 2 Hz and has no business walking the widget tree to find its target.
//
// Entries go stale when a modal closes and takes its bar with it, so every
// reader validates before touching one and clears what it finds dead.
static constexpr int kNavDmIconSlots = 4;
static lv_obj_t *s_navDmIcons[kNavDmIconSlots] = {nullptr};
// Same again for the Chats cell. Same slot count for the same reason: one bar
// on the chat screen plus one on whatever modal is over it.
static lv_obj_t *s_navChatIcons[kNavDmIconSlots] = {nullptr};
// Where the blink currently is, at file scope so a bar built mid-blink can be
// painted to match rather than sitting at the wrong colour until the next flip.
static bool s_navDmLit = false;
static bool s_navChatLit = false;

// The whole cell, not the glyph inside it. On a bar of icon buttons the glyph
// is a small part of a large target, and a button that changes colour is what
// reads as "this one wants you" from arm's length — a recoloured 14 px symbol
// does not. The cell is the label's parent; the label goes dark so it stays
// legible on the amber rather than disappearing into it.
//
// Unlit restores through navBarStyleCell() rather than by remembering what was
// replaced, so a cell that is also the active one comes back correctly marked.
static void navCellApplyInk(lv_obj_t *label, bool lit) {
    if (!label || !lv_obj_is_valid(label)) return;

    lv_obj_t *cell = lv_obj_get_parent(label);
    if (!cell || !lv_obj_is_valid(cell)) {
        // No cell to flash — colour the glyph, as this used to.
        lv_obj_set_style_text_color(label,
                                    lit ? lv_color_hex(0xF4D35E) : lv_color_hex(0xD9E8FF), 0);
        return;
    }

    if (lit) {
#if defined(DEVICE_TDECK_PRO)
        // The border carries it, not a fill. Amber is not a colour this panel
        // has, but filling the cell instead turns it into a black box: the glyph
        // is buried in it and the result reads as a blot rather than a mark. So
        // the cell keeps its white face and thickens its edge -- past the 2 px
        // an *active* cell already uses, so "there is something here" and "you
        // are here" stay tellable apart.
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 3, 0);
        lv_obj_set_style_border_color(cell, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_text_color(label, lv_color_make(0, 0, 0), 0);
#else
        lv_obj_set_style_bg_color(cell, lv_color_hex(0xF4D35E), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0xF4D35E), 0);
        // Fixed, not a theme token: s_ui.battWarn is the same amber on every
        // theme (see applyUiThemePalette), so the ink that has to read on it is
        // the same everywhere too.
        lv_obj_set_style_text_color(label, lv_color_make(0x16, 0x23, 0x3A), 0);
#endif
    } else {
        const intptr_t tag = (intptr_t)lv_obj_get_user_data(cell);
        const int target = (tag == 0) ? kNavBarNoActive : (int)(tag - 1);
        navBarStyleCell(cell, target != kNavBarNoActive
                              && target == navBarActiveTarget(lv_obj_get_parent(cell)));
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(label, lv_color_make(0, 0, 0), 0);
#else
        lv_obj_set_style_text_color(label, lv_color_hex(0xD9E8FF), 0);
#endif
    }
}

static void navDmApplyInk(lv_obj_t *label, bool lit) {
    navCellApplyInk(label, lit);
}

// The Chats cell gets the same treatment for unread *channel* traffic that the
// DM cell gets for private messages: it is the bar that is always on screen, it
// is where the tap has to land anyway, and colour costs no width on a bar that
// is already seven cells across.
//
// A different amber would be a puzzle, so it is the same one. The two cells sit
// apart on the bar and carry different glyphs; what the colour says is "there is
// something here", and that means the same thing in both places.
static void navChatApplyInk(lv_obj_t *label, bool lit) {
    navCellApplyInk(label, lit);
}

// A registered label empties its own slot as it is deleted. lv_obj_is_valid()
// alone cannot tell: a bar that went with a closed modal frees its labels, LVGL
// hands the same address to whatever is built next, and the stale slot then
// passes as valid. The blink went on painting that object's parent -- the home
// dashboard's SNR/RSSI card, once -- amber twice a second.
static void onNavIconDeleted(lv_event_t *e) {
    lv_obj_t *label = lv_event_get_target_obj(e);
    for (int i = 0; i < kNavDmIconSlots; i++) {
        if (s_navChatIcons[i] == label) s_navChatIcons[i] = nullptr;
        if (s_navDmIcons[i] == label)   s_navDmIcons[i] = nullptr;
    }
}

static void navIconStore(lv_obj_t **slot, lv_obj_t *label) {
    *slot = label;
    lv_obj_add_event_cb(label, onNavIconDeleted, LV_EVENT_DELETE, nullptr);
}

static void navChatIconRegister(lv_obj_t *label) {
    for (int i = 0; i < kNavDmIconSlots; i++) {
        if (s_navChatIcons[i] == label) { navChatApplyInk(label, s_navChatLit); return; }
        if (!s_navChatIcons[i] || !lv_obj_is_valid(s_navChatIcons[i])) {
            navIconStore(&s_navChatIcons[i], label);
            navChatApplyInk(label, s_navChatLit);
            return;
        }
    }
    navIconStore(&s_navChatIcons[0], label);   // all live: the oldest gives way
    navChatApplyInk(label, s_navChatLit);
}

static void navDmIconRegister(lv_obj_t *label) {
    for (int i = 0; i < kNavDmIconSlots; i++) {
        if (s_navDmIcons[i] == label) { navDmApplyInk(label, s_navDmLit); return; }
        if (!s_navDmIcons[i] || !lv_obj_is_valid(s_navDmIcons[i])) {
            navIconStore(&s_navDmIcons[i], label);
            navDmApplyInk(label, s_navDmLit);
            return;
        }
    }
    navIconStore(&s_navDmIcons[0], label);   // all live: the oldest gives way
    navDmApplyInk(label, s_navDmLit);
}
#endif  // UI_TOUCH_NAV_BAR

#if UI_TOUCH_NAV_BAR
// How a nav cell says "you are here". Pulled out of the build loop because the
// bar outlives the screen it was built for: the chat screen's bar is the one
// showing under the home dashboard too, so the lit cell moves between Home and
// Chats without the bar itself being rebuilt.
static void navBarStyleCell(lv_obj_t *btn, bool isActive) {
    if (!btn || !lv_obj_is_valid(btn)) return;
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, isActive ? 2 : 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
#else
    // Theme tokens, not the fixed blues these used to be. The bar was the one
    // surface that stayed navy on a red or green theme, which read as a piece of
    // another firmware sitting under the chat.
    //
    // selectBg/tabIdle rather than the raw values: the active cell is doing what
    // a selected row does and the rest are doing what an inactive tab does, so
    // they take the tokens that already mean those things on every theme.
    lv_obj_set_style_bg_color(
        btn,
        (s_cfg.uiMode == UI_MODE_LIGHT)
            ? chatPanelBackgroundColor()
            : lvColorFrom565(isActive ? s_ui.selectBg : s_ui.tabIdle),
        0);
    lv_obj_set_style_bg_opa(btn, isActive ? LV_OPA_80 : LV_OPA_60, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn,
                                  isActive ? lv_color_hex(0xE8F1FF) : lv_color_hex(0x335D9D),
                                  0);
#endif
}

// Move the lit cell on an already-built bar. Walks the children rather than
// keeping a table of them: seven widgets on a screen transition is nothing, and
// a table would be another thing to invalidate when a bar goes with its modal.
static void navBarSetActive(lv_obj_t *bar, int activeTarget) {
    if (!bar || !lv_obj_is_valid(bar)) return;
    navBarNoteActive(bar, activeTarget);
    const uint32_t n = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(bar, i);
        if (!child) continue;
        // Stored as target+1 so that 0 means "not a nav cell". The status
        // cluster shares this bar on the keyboard boards and carries no user
        // data, and HELTEC_NAV_CFG is 0 — without the offset the two would be
        // indistinguishable, and the status box would light up as Config.
        const intptr_t tag = (intptr_t)lv_obj_get_user_data(child);
        if (tag == 0) continue;
        navBarStyleCell(child, (int)(tag - 1) == activeTarget);
    }
}
#endif

#if UI_TOUCH_NAV_BAR && defined(DEVICE_TANMATSU)
// The Tanmatsu's six coloured function keys carry shapes, not "F1".."F6" --
// red cross, orange triangle, yellow square, green circle, blue club, purple
// diamond, left to right -- so the nav bar names each key by its shape. No font
// here has those glyphs, so they are drawn: a bare object whose user data is
// the shape, painted in its own DRAW_MAIN.
enum TmKeyShape : uint8_t {
    TM_SHAPE_CROSS = 1,
    TM_SHAPE_TRIANGLE,
    TM_SHAPE_SQUARE,
    TM_SHAPE_CIRCLE,
    TM_SHAPE_CLUB,
    TM_SHAPE_DIAMOND,
};

static void tmDrawDisc(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r,
                       lv_color_t color) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = 0;
    const lv_area_t a = {cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(layer, &d, &a);
}

static void tmDrawTri(lv_layer_t *layer, int32_t x0, int32_t y0, int32_t x1,
                      int32_t y1, int32_t x2, int32_t y2, lv_color_t color) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color;
    d.opa = LV_OPA_COVER;
    d.p[0].x = x0; d.p[0].y = y0;
    d.p[1].x = x1; d.p[1].y = y1;
    d.p[2].x = x2; d.p[2].y = y2;
    lv_draw_triangle(layer, &d);
}

static void onTmKeyShapeDraw(lv_event_t *e) {
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!obj || !layer) return;
    // Panel pixels, not layout ones: the parenthesised name steps around the
    // coordinate shim, and everything below is drawn at full resolution.
    lv_area_t a;
    (lv_obj_get_coords)(obj, &a);
    const int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    const int32_t s = (w < h ? w : h);
    const int32_t cx = a.x1 + w / 2, cy = a.y1 + h / 2;
    const int32_t r = s / 2;
    const int32_t x0 = cx - r, y0 = cy - r, x1 = x0 + s - 1, y1 = y0 + s - 1;

    switch ((intptr_t)lv_obj_get_user_data(obj)) {
        case TM_SHAPE_CROSS: {
            lv_draw_line_dsc_t d;
            lv_draw_line_dsc_init(&d);
            d.color = lv_color_hex(0xFF5A5A);
            d.width = s / 5 > 2 ? s / 5 : 2;
            d.round_start = 1;
            d.round_end = 1;
            const int32_t in = d.width / 2;
            d.p1.x = x0 + in; d.p1.y = y0 + in; d.p2.x = x1 - in; d.p2.y = y1 - in;
            lv_draw_line(layer, &d);
            d.p1.x = x1 - in; d.p1.y = y0 + in; d.p2.x = x0 + in; d.p2.y = y1 - in;
            lv_draw_line(layer, &d);
            break;
        }
        case TM_SHAPE_TRIANGLE:
            tmDrawTri(layer, cx, y0, x1, y1, x0, y1, lv_color_hex(0xFF9F40));
            break;
        case TM_SHAPE_SQUARE: {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = lv_color_hex(0xFFD84A);
            d.bg_opa = LV_OPA_COVER;
            d.radius = 1;
            d.border_width = 0;
            const int32_t in = s / 10;
            const lv_area_t sq = {x0 + in, y0 + in, x1 - in, y1 - in};
            lv_draw_rect(layer, &d, &sq);
            break;
        }
        case TM_SHAPE_CIRCLE:
            tmDrawDisc(layer, cx, cy, r, lv_color_hex(0x4CD964));
            break;
        case TM_SHAPE_CLUB: {
            // Three lobes and a flared stem, like the key's print.
            const lv_color_t c = lv_color_hex(0x4DA3FF);
            const int32_t lr = s * 23 / 100;
            tmDrawDisc(layer, cx, y0 + lr, lr, c);
            tmDrawDisc(layer, x0 + lr, cy + lr / 3, lr, c);
            tmDrawDisc(layer, x1 - lr, cy + lr / 3, lr, c);
            tmDrawTri(layer, cx, cy - lr / 2, cx + lr, y1, cx - lr, y1, c);
            break;
        }
        case TM_SHAPE_DIAMOND: {
            const lv_color_t c = lv_color_hex(0xB57CFF);
            tmDrawTri(layer, cx, y0, x1, cy, x0, cy, c);
            tmDrawTri(layer, x0, cy, x1, cy, cx, y1, c);
            break;
        }
        default:
            break;
    }
}
#endif

static void populateHeltecBottomNav(lv_obj_t *bar, int activeTarget) {
#if UI_TOUCH_NAV_BAR
    if (!bar) return;

    struct NavItem {
        const char *icon;
        int target;
        // The one glyph that is not from the built-in symbol set, and so has to
        // be drawn with the emoji face instead of the plain one.
        bool emoji;
    };

    // Nodes is a list of people, so it gets a person. LVGL's symbol set has no
    // contact glyph — it is FontAwesome's icon subset, and no bust/user icon is
    // in the montserrat cmap — so this one comes from the emoji fallback face
    // that already renders emoji in chat.
    //
    // With a fallback: emojiFont() hands back the base face unchanged when the
    // emoji font failed to initialize (a full LVGL pool at boot), and drawing
    // U+1F464 with the base face would put a tofu box in the nav bar. A list
    // icon is a worse Nodes icon than a person, and a much better one than a
    // rectangle.
    // 14 on every board. It used to be the largest size that still left room
    // for a shortcut letter beside the glyph; with the letters gone (issue #78)
    // the glyph has the button to itself and this is simply the size that reads
    // at a glance without crowding a 320 px bar's ~35 px cells.
#if UI_LARGE_PANEL_PROFILE
    const lv_font_t *const navIconFont = &lv_font_montserrat_28;
#elif defined(DEVICE_TDISPLAY_P4)
    // Grown with the taller bar (kBottomNavHeight). Capped by the emoji fallback
    // (emoji_font.cpp): Chat, Nodes and Tools draw from it, and a face without
    // one would drop all three to their plain-symbol stand-ins. Portrait's
    // taller bar takes 20, which this board alone gives an emoji face; 18
    // otherwise.
    const lv_font_t *const navIconFont = uiPortrait() ? &lv_font_montserrat_20
                                                      : &lv_font_montserrat_18;
#elif defined(DEVICE_CROWPANEL_50)
    // With the doubled bar (kBottomNavHeight). 24 rather than 28: it is the
    // largest size the CrowPanels give an emoji face (emoji_font.cpp), and
    // Chat, Nodes and Tools draw from it.
    const lv_font_t *const navIconFont = &lv_font_montserrat_24;
#elif defined(DEVICE_CROWPANEL_35)
    // Portrait's doubled bar takes 24, as the 5" does: the largest size the
    // CrowPanels give an emoji face. Landscape's 28 px bar keeps 14.
    const lv_font_t *const navIconFont = uiPortrait() ? &lv_font_montserrat_24
                                                      : &lv_font_montserrat_14;
#else
    const lv_font_t *const navIconFont = &lv_font_montserrat_14;
#endif
    const lv_font_t *const navEmojiFont = emojiFont(navIconFont);
    const bool navEmojiReady = (navEmojiFont != navIconFont);
    const char *const kContactIcon = "\U0001F464";  // bust in silhouette
    // Tools took Live's place on the bar, and LVGL's symbol set has no wrench —
    // the same gap the contact icon works around, so it takes the same emoji
    // fallback. LV_SYMBOL_BARS stands in when the emoji face failed to load;
    // LV_SYMBOL_SETTINGS would have read as a second Config button.
    const char *const kToolsIcon = "\U0001F527";    // wrench

    // DM's icon is board-dependent, and only because of how the T-Deck Pro
    // draws. Its panel is 1-bit (the I1 flush path in the display driver), so
    // LVGL thresholds every glyph at a hard luminance cut with no dithering —
    // LV_DRAW_SW_I1_LUM_THRESHOLD, 127. LV_SYMBOL_ENVELOPE is FontAwesome's
    // *solid* envelope whose flap is a pair of thin light strokes: they land on
    // the wrong side of that cut and the whole glyph floods to a filled
    // rectangle, which is what it looked like on the device.
    //
    // Noto Emoji is line art, so it survives the same treatment. U+2709 is the
    // envelope this always wanted — an outline with an open flap rather than a
    // solid block — and it comes out about 30% covered at 12, 14 and 16 px with
    // the flap still legible, against the ~47% of a glyph that has flooded.
    //
    // It used to be the speech balloon, which survives thresholding just as
    // well but belongs to the Chats cell beside it: taking it for DM is what
    // pushed channels onto a bare "#" on this one board, so the private
    // conversations wore the speech bubble and the public ones did not.
    //
    // Every other board anti-aliases, where FontAwesome's envelope renders
    // correctly and is the more conventional icon, so they keep it.
#if defined(DEVICE_TDECK_PRO)
    const bool navDmIsEmoji = navEmojiReady;
    const char *const kDmIcon =
        navEmojiReady ? "\u2709" : LV_SYMBOL_ENVELOPE;  // outline envelope
#else
    const bool navDmIsEmoji = false;
    const char *const kDmIcon = LV_SYMBOL_ENVELOPE;
#endif

    // Chats — the channel conversations, as against DM's private ones. The
    // speech balloon on every board whose emoji face loaded, DM included now
    // that it wears an envelope instead: the two are different glyphs from the
    // same face rather than a choice between them.
    //
    // "#" is what is left when that face failed to load at all. It is plain
    // ASCII, so the 1-bit panel cannot threshold it into a blob, it is what a
    // channel is called everywhere else in this UI, and it can never collide
    // with the list glyph Nodes falls back to in the same situation.
    const bool navChatIsEmoji = navEmojiReady;
    const char *const kChatIcon = navChatIsEmoji ? "\U0001F4AC" : "#";
    // Icons rather than words. Six buttons of text across 240 px meant either
    // an abbreviation per board ("Config" on the wide one, "Cfg" on the tall
    // one) or a 10 px label with nothing left over; a glyph is the same size on
    // both and reads at a glance on a bar you hit with a thumb.
    //
    // Home first, on the far left, where the way back belongs. It is not a
    // sixth screen — see HELTEC_NAV_HOME in onHeltecBottomNavPressed().
    //
    // The glyphs are all in the built-in montserrat symbol set except Help,
    // which has no symbol to borrow: LVGL's font carries no question mark icon,
    // so the ASCII one stands in and reads the same.
    //
    // No Actions entry. It acts on the channel you are reading, so it belongs
    // with the chat screen's own buttons and nowhere else — offering it from
    // Nodes or Live meant a button that silently acted on a channel that was
    // not even on screen. The chat screen builds its own, next to New Message.
    //
    // HELTEC_NAV_ACTIONS itself stays: that button routes through
    // onHeltecBottomNavPressed() below, so the open/close behaviour is shared.
    //
    // No Live entry either, for the same reason it is still in the enum: Tools
    // sits in that slot now and carries Live as its first row, so the feed is
    // one tap further away rather than gone.
    // Home, then the three screens that carry content, then the two that do
    // not. Config used to sit second, which put the settings screen under the
    // thumb of anyone reaching for Home and split the reading destinations
    // apart; it now sits with Help at the right end, where you go deliberately
    // rather than in passing.
    const NavItem kItems[] = {
        {LV_SYMBOL_HOME,     HELTEC_NAV_HOME,   false},
        // Immediately right of Home, because the two are a pair: Home is the
        // glance at the node, this is the messages. It also retires the toggle
        // Home briefly carried — a second tap on Home meaning "show me the chat"
        // was only ever a way around not having this cell.
        {kChatIcon,          HELTEC_NAV_CHAT,   navChatIsEmoji},
        {kDmIcon,            HELTEC_NAV_DM,     navDmIsEmoji},
        // The node roster, and the packet feed coming in over the air.
        {navEmojiReady ? kContactIcon : LV_SYMBOL_LIST,
                             HELTEC_NAV_NODES,  navEmojiReady},
        {navEmojiReady ? kToolsIcon : LV_SYMBOL_BARS,
                             HELTEC_NAV_TOOLS,  navEmojiReady},
        {LV_SYMBOL_SETTINGS, HELTEC_NAV_CFG,    false},
        {"?",                HELTEC_NAV_LEGEND, false},
    };

    // Portrait is 80 px narrower and the bar carries the same seven cells, so
    // it gives up its padding first.
    const int barPadX   = uiPortrait() ? 1 : 2;
    const int barPadY   = 1;
    const int barPadCol = uiPortrait() ? 1 : 2;
    const int btnPad    = uiPortrait() ? 0 : 1;

    // Nav Alignment Left/Right: the same cells stacked down a narrow column
    // (see navIsSideColumn()). On the P4 the column is placed below the rounded
    // top corner (kSideNavTopY); the bottom is already clear, the display
    // stopping short of it (UI_BOTTOM_SAFE_H).
    const bool sideColumn = navIsSideColumn();

    if (sideColumn) {
        lv_obj_set_style_pad_left(bar, 1, 0);
        lv_obj_set_style_pad_right(bar, 1, 0);
        lv_obj_set_style_pad_top(bar, 1, 0);   // the column itself starts low (kSideNavTopY)
        lv_obj_set_style_pad_bottom(bar, 1, 0);
        lv_obj_set_style_pad_row(bar, 1, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_style_pad_left(bar, barPadX, 0);
        lv_obj_set_style_pad_right(bar, barPadX, 0);
        lv_obj_set_style_pad_top(bar, barPadY, 0);
        lv_obj_set_style_pad_bottom(bar, barPadY, 0);
        lv_obj_set_style_pad_column(bar, barPadCol, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    }
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const lv_color_t navTextColor = lv_color_hex(0xD9E8FF);

    // Recorded before the cells are built, so a bar that is never re-marked
    // still knows which of them is the lit one — the unread flash asks.
    navBarNoteActive(bar, activeTarget);

    for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); i++) {
        lv_obj_t *btn = lv_btn_create(bar);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(btn);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
#endif
        lv_obj_set_flex_grow(btn, 1);
        if (sideColumn) {
            lv_obj_set_width(btn, lv_pct(100));   // grows down the column instead
        } else {
            lv_obj_set_height(btn, lv_pct(100));
        }
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, sideColumn ? 0 : btnPad, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        navBarStyleCell(btn, activeTarget == kItems[i].target);

        // Which destination this cell is, readable back off the widget. It is
        // what lets navBarSetActive() re-mark the bar when the root screen
        // switches between the dashboard and the chat without rebuilding it —
        // those two share one bar, so the lit cell has to be able to move.
        lv_obj_set_user_data(btn, (void *)(intptr_t)(kItems[i].target + 1));

        lv_obj_add_event_cb(btn,
                            onHeltecBottomNavPressed,
                            LV_EVENT_PRESSED,
                            (void *)(intptr_t)kItems[i].target);

        lv_obj_t *label = lv_label_create(btn);
        // Bigger than the 10 the words used: a glyph carries no letters to
        // read, so it has to be big enough to recognise by shape. Still clears
        // the bar with room for the button's border and padding.
        lv_obj_set_style_text_font(label,
                                   kItems[i].emoji ? navEmojiFont : navIconFont, 0);
        lv_obj_set_style_text_color(label, navTextColor, 0);
        lv_label_set_text(label, kItems[i].icon);
        // The glyph is the whole button now (issue #78): every board that draws
        // this bar is a touch device, so a printed keyboard hint was telling a
        // thumb about a key. The keys themselves are unchanged — Help still
        // lists them, and turning the bar off still brings back the key-hint
        // strip under the chat.
        lv_obj_center(label);
#if defined(DEVICE_TDISPLAY_P4)
        // P4 landscape with the keyboard expansion on: the F-key that reaches
        // each cell (kTloraTapMap), small beside its glyph -- F1..F6 left to
        // right along the bar, F10 for Help. Only with the keyboard there, as
        // it names keys that are otherwise nowhere; not in portrait, whose
        // narrower cells have no room beside the glyph. Checked as the bar is
        // built, which is on every screen change.
        if (!uiPortrait() && keyboardAttached()) {
            const char *fkey = nullptr;
            switch (kItems[i].target) {
                case HELTEC_NAV_HOME:   fkey = "F1";  break;
                case HELTEC_NAV_CHAT:   fkey = "F2";  break;
                case HELTEC_NAV_DM:     fkey = "F3";  break;
                case HELTEC_NAV_NODES:  fkey = "F4";  break;
                case HELTEC_NAV_TOOLS:  fkey = "F5";  break;
                case HELTEC_NAV_CFG:    fkey = "F6";  break;
                case HELTEC_NAV_LEGEND: fkey = "F10"; break;
                default: break;
            }
            if (fkey) {
                // Glyph and key side by side, the pair centred in the cell --
                // or, in the side column, the key under the glyph, since the
                // column has height to give and no width.
                lv_obj_set_flex_flow(btn, sideColumn ? LV_FLEX_FLOW_COLUMN
                                                     : LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                      LV_FLEX_ALIGN_CENTER);
                if (sideColumn) {
                    lv_obj_set_style_pad_row(btn, 0, 0);
                } else {
                    lv_obj_set_style_pad_column(btn, 3, 0);
                }
                lv_obj_t *hint = lv_label_create(btn);
                lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
                lv_obj_set_style_text_color(hint, lv_color_hex(0x8FB5E6), 0);
                lv_label_set_text(hint, fkey);
            }
        }
#elif defined(DEVICE_TANMATSU)
        // The shape on the function key that reaches each cell (kTmKeymap:
        // F1..F6 are Home, Chats, DMs, Nodes, Tools, Config), small beside the
        // glyph -- or under it in the side column, which has height to give
        // and no width. Help has no coloured key, so no shape.
        {
            intptr_t shape = 0;
            switch (kItems[i].target) {
                case HELTEC_NAV_HOME:  shape = TM_SHAPE_CROSS;    break;
                case HELTEC_NAV_CHAT:  shape = TM_SHAPE_TRIANGLE; break;
                case HELTEC_NAV_DM:    shape = TM_SHAPE_SQUARE;   break;
                case HELTEC_NAV_NODES: shape = TM_SHAPE_CIRCLE;   break;
                case HELTEC_NAV_TOOLS: shape = TM_SHAPE_CLUB;     break;
                case HELTEC_NAV_CFG:   shape = TM_SHAPE_DIAMOND;  break;
                default: break;
            }
            if (shape) {
                lv_obj_set_flex_flow(btn, sideColumn ? LV_FLEX_FLOW_COLUMN
                                                     : LV_FLEX_FLOW_ROW);
                lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                                      LV_FLEX_ALIGN_CENTER);
                if (sideColumn) {
                    lv_obj_set_style_pad_row(btn, 1, 0);
                } else {
                    lv_obj_set_style_pad_column(btn, 3, 0);
                }
                lv_obj_t *hint = lv_obj_create(btn);
                lv_obj_remove_style_all(hint);
                // Not clickable, so a press on the shape is a press on the cell.
                lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_remove_flag(hint, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_size(hint, 7, 7);   // 14 panel pixels on this board
                lv_obj_set_user_data(hint, (void *)shape);
                lv_obj_add_event_cb(hint, onTmKeyShapeDraw, LV_EVENT_DRAW_MAIN, nullptr);
            }
        }
#endif
        // Registered on every board that draws this bar, not just the
        // touch-only ones: where the bar is a setting, the alert follows the
        // setting rather than the board.
        if (kItems[i].target == HELTEC_NAV_DM) navDmIconRegister(label);
        if (kItems[i].target == HELTEC_NAV_CHAT) navChatIconRegister(label);
    }
#else
    LV_UNUSED(bar);
    LV_UNUSED(activeTarget);
#endif
}

#if HAS_NAV_BAR_TOGGLE
// The GPS/WiFi/DM icons that share the nav bar with the buttons, as a fixed
// width box at the end of the row.
//
// Boxed rather than dropped loose into the bar for two reasons. The buttons are
// flex_grow, so they divide up whatever is left after this — a bare label would
// be measured mid-layout and the buttons would resize with the satellite count.
// And the DM envelope blinks by toggling HIDDEN, which a flex row answers by
// re-laying out every sibling; boxed, the blink stays inside.
//
// The labels are positioned by hand inside the box, packed right to left the
// way this bar has always packed them. refreshHeaderStatus() re-runs the same
// alignments as the GPS label grows and shrinks with the satellite count.
static void buildNavStatusCluster(lv_obj_t *bar, lv_obj_t **boxOut, lv_obj_t **gpsOut,
                                  lv_obj_t **wifiOut, lv_obj_t **dmOut,
                                  lv_obj_t **chanOut) {
    if (!bar) return;

    // Measured, not chosen. This cluster is only ever built as part of the nav
    // bar, and with the bar up the unread alert lives on the Chats and DM cells
    // (issue #91) -- so the envelope and bell this box used to reserve room for
    // are gone, and the width they held would otherwise sit on the bar as dead
    // space with the buttons crowded beside it.
    //
    // Measuring rather than subtracting a guess from the old 76: the answer
    // depends on the font, and a number picked by eye is one that quietly stops
    // fitting the first time a glyph or a font changes. "GPS 12" is the widest
    // ordinary reading -- "GPS SEARCH 12" exists but is allowed to sit under the
    // button edge, which is what the old width did too.
#if defined(DEVICE_TDISPLAY_P4)
    // Scaled with the P4's taller bar and larger icons (kBottomNavHeight).
    const lv_font_t *const statusFont = &lv_font_montserrat_14;
#elif defined(DEVICE_CROWPANEL_50)
    const lv_font_t *const statusFont = &lv_font_montserrat_20;   // the doubled bar
#else
    const lv_font_t *const statusFont = &lv_font_montserrat_10;
#endif
    lv_point_t szGps = {0, 0}, szWifi = {0, 0};
    lv_text_get_size(&szGps, "GPS 12", statusFont, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    lv_text_get_size(&szWifi, LV_SYMBOL_WIFI, statusFont, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    // 2 right margin + gps + 7 gap + wifi + 2 left margin, matching the
    // alignments below.
    const int statusBoxW = 2 + (int)szGps.x + 7 + (int)szWifi.x + 2;

    lv_obj_t *box = lv_obj_create(bar);
    // See onNavStatusClusterDeleted(): this is what keeps the statics below from
    // outliving the objects they name when a screen closes without clearing them.
    lv_obj_add_event_cb(box, onNavStatusClusterDeleted, LV_EVENT_DELETE, nullptr);
    lv_obj_set_size(box, statusBoxW, lv_pct(100));
#if HAS_STATUS_STRIP
    // In portrait these readings are in the status strip under the bar
    // (statusStripInit()). Built and kept, only hidden: refreshHeaderStatus()
    // still paints through these handles, and a hidden child takes no room in
    // the bar's flex row, so the cells get the width back.
    if (uiPortrait()) lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
#endif
    // The condensed column (Nav Alignment Left/Right) is one cell wide, and
    // "GPS 12" is wider than that. Hidden the same way, for the same reasons:
    // the handles stay live and the cells keep the column's height.
    if (navIsSideColumn()) lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    lv_obj_t *gps = lv_label_create(box);
    lv_obj_set_style_text_font(gps, statusFont, 0);
    lv_obj_set_style_text_color(gps, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align(gps, LV_ALIGN_RIGHT_MID, -2, 0);

    lv_obj_t *wifi = lv_label_create(box);
    lv_obj_set_style_text_font(wifi, statusFont, 0);
    lv_obj_set_style_text_color(wifi, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align_to(wifi, gps, LV_ALIGN_OUT_LEFT_MID, -7, 0);

    // No envelope and no bell. They were the unread indicators for a bar that
    // did not carry them on its cells; it does now, and building two labels that
    // can never be shown would be reserving the width all over again.
    //
    // The out-params are still filled -- with null -- because the refreshers
    // hold these handles and setDmAlertVisible() is null-safe. That keeps the
    // callers free of a second rule about when the handles exist.

    if (boxOut)  *boxOut  = box;
    if (gpsOut)  *gpsOut  = gps;
    if (wifiOut) *wifiOut = wifi;
    if (dmOut)   *dmOut   = nullptr;
    if (chanOut) *chanOut = nullptr;
}
#endif

static void appendHeltecBottomNav(lv_obj_t *parent, int activeTarget) {
#if UI_TOUCH_NAV_BAR
    if (!parent) return;
    // Where the bar is a setting, this is what makes it one on every screen but
    // chat: the callers below ask unconditionally and get nothing when it is
    // off, so no screen has to know about the preference.
    if (!bottomNavEnabled()) return;

    // Match the height used by the main chat screen's shortcut bar
    // (chatLegendH) so the nav buttons render at the exact same width/height
    // inside DM/Cfg/Nodes/Live as on the home screen.
    const int navBarHeight = kBottomNavHeight;

    // Modal containers all use pad_all=4, and a 1 px border -- except DMs,
    // Nodes, Config and Tools, which have none (the border was a frame round a
    // screen that already fills the display). The bar needs to span the full
    // display width and sit flush with the bottom edge, so it is placed with
    // IGNORE_LAYOUT and offset back through the parent pad + border, the border
    // read rather than assumed. A transparent spacer keeps the flex column
    // reserving the same vertical room the bar would otherwise occupy.
    const int padInset = 4;
    const int borderInset = (int)lv_obj_get_style_border_width(parent, LV_PART_MAIN);
    const int contentReserve = navBarHeight - padInset - borderInset;

    if (navIsSideColumn()) {
        // Left/Right: a column down one edge, the full height of the display,
        // in the same place and size as the chat screen's copy. No spacer: the
        // room is reserved by widening the parent's padding on that side, so
        // the flex column lays its content out beside the bar and keeps the
        // whole height. The offsets cancel the parent's pad and border the
        // same way the bottom placement below does.
        const bool left = navSideLeft();
        const int padSide = kSideNavW + padInset;
        if (left) {
            lv_obj_set_style_pad_left(parent, padSide, 0);
        } else {
            lv_obj_set_style_pad_right(parent, padSide, 0);
        }

        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(bar, kSideNavW, lv_disp_get_ver_res(NULL) - kSideNavTopY);
        lv_obj_align(bar, left ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT,
                     left ? -(padSide + borderInset) : (padSide + borderInset),
                     kSideNavTopY - (padInset + borderInset));
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_hex(0x335D9D), 0);
        populateHeltecBottomNav(bar, activeTarget);
        return;
    }

    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_height(spacer, contentReserve > 0 ? contentReserve : 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);

    lv_obj_t *bar = lv_obj_create(parent);
    // FLOATING as well as IGNORE_LAYOUT. IGNORE_LAYOUT alone keeps the flex
    // column from placing the bar, but it does not keep the bar still when the
    // parent scrolls — LVGL translates every ordinary child by the scroll
    // offset, so on a screen whose body can scroll (Help on the Cardputer, whose
    // key list cannot fit 135 px at any font size) the bar would slide off the
    // bottom edge with the content. FLOATING pins it to the parent's content
    // area instead, which is the same thing appendHeltecCloseX() does with the
    // corner X and for the same reason. A no-op on the screens whose panel does
    // not scroll, which is all of the others.
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_width(bar, lv_disp_get_hor_res(NULL));
    lv_obj_set_height(bar, navBarHeight);
    // BOTTOM_LEFT anchor with negative x cancels parent pad+border on the left,
    // positive y pushes through pad+border on the bottom — bar ends up at the
    // device edges identical to the home-screen shortcut bar.
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT,
                 -(padInset + borderInset),
                 padInset + borderInset);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x335D9D), 0);
#endif

    populateHeltecBottomNav(bar, activeTarget);

#if HAS_NAV_BAR_TOGGLE
    // Same icons as the chat screen's copy of this bar. Without them, walking
    // into Config or Nodes is the one place the GPS fix and the WiFi state go
    // dark — this board keeps them on the bottom bar, and the bottom bar is
    // right here.
    buildNavStatusCluster(bar, &s_navStatusBox, &s_navStatusGps,
                          &s_navStatusWifi, &s_navStatusDm, &s_navStatusChan);
    // The labels are created empty, and the periodic refresh early-outs while
    // nothing has changed — which, opening a screen, is the normal case. Force
    // one pass so they arrive with the readings already on them.
    refreshHeaderStatus(true);
    refreshDmAlertIndicator();
    refreshChatAlertIndicator();
#endif
#else
    LV_UNUSED(parent);
    LV_UNUSED(activeTarget);
#endif
}

#if HAS_TOOL_STACK_LAYOUT
// ── Portrait: the screens Tools opens ────────────────────────────────────────
// Held upright (P4, Heltec V4), every one is laid out the same way: Tools'
// title, the screen's own actions as a row of big buttons under it, its
// content, and the nav bar with Tools lit. Landscape keeps the header bar.
// On every build the bar is the way out of these screens -- Tools goes back to
// the list, any other cell goes there (closeToolScreens()) -- and none of them
// carries a close X.
static inline bool toolStackLayout() { return uiPortrait(); }

#if defined(DEVICE_TDISPLAY_P4)
static constexpr int kStackBtnH = 40;
#define STACK_BTN_FONT (&lv_font_montserrat_14)
#else
static constexpr int kStackBtnH = 30;
#define STACK_BTN_FONT (&lv_font_montserrat_12)
#endif

static lv_obj_t *toolStackTitle(lv_obj_t *modal, const char *text,
                             lv_color_t color = lv_color_hex(0xD9E8FF)) {
    lv_obj_t *title = lv_label_create(modal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_color(title, color, 0);
    stackTitleOnXLine(title, /*reserveX=*/false);
    lv_label_set_text(title, text);
    return title;
}

#else
static inline bool toolStackLayout() { return false; }
static constexpr int kStackBtnH = 22;
#define STACK_BTN_FONT (&lv_font_montserrat_10)
#endif

#if UI_TOUCH_ONLY_PROFILE
// The screens Tools opens put their actions (Clear, Scan, Filter, Sweep...) in
// a row of their own directly under the title, in either orientation, rather
// than in the header beside it -- where there was room for two or three small
// fixed-width buttons at most, and the title lost the width to them. Equal
// shares of the width, so one button or four fill the row the same way.
// Upright on the stack-layout boards the buttons are the big ones; otherwise
// the size Discovery's row has always had.
static lv_obj_t *toolActionRow(lv_obj_t *modal) {
    const bool big = toolStackLayout();
    lv_obj_t *row = lv_obj_create(modal);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, big ? 6 : 0, 0);
    lv_obj_set_style_pad_column(row, big ? 6 : 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return row;
}

// Returns the label: the callers that keep a handle retitle it later.
static lv_obj_t *toolActionBtn(lv_obj_t *row, const char *text, lv_event_cb_t cb) {
    const bool big = toolStackLayout();
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_height(btn, big ? kStackBtnH : 22);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_hor(btn, 4, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x8FB5E6), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, big ? STACK_BTN_FONT : &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(lbl, lv_pct(100), 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return lbl;
}
#endif

