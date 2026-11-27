// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Nodes modal, release notes, Config modal open and actions.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void openNodesModal() {
    if (s_nodesModal && !lvObjAlive(s_nodesModal)) {
        s_nodesModal = nullptr;
        s_nodesInfoPanel = nullptr;
        s_nodesDetail = nullptr;
        s_nodesListPanel = nullptr;
        memset(s_nodesSectionBox, 0, sizeof(s_nodesSectionBox));
        memset(s_nodesSectionKeys, 0, sizeof(s_nodesSectionKeys));
        memset(s_nodesSectionVals, 0, sizeof(s_nodesSectionVals));
        s_nodesMapPanel = nullptr;
        s_nodesMapTitle = nullptr;
        s_nodesMapCoords = nullptr;
        s_nodesMapMarker = nullptr;
        s_nodesMapTileLayer = nullptr;
        s_nodesMapImage = nullptr;
        s_nodesMapLastRenderUsedDetail = false;
        s_nodesMapLastDetailKey[0] = '\0';
        s_nodesMapLastDetailLod = -1;
        s_nodesList = nullptr;
        s_nodesHintLabel = nullptr;
    }
    if (!s_rootScreen || s_nodesModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeLiveModal();
    closeCfgModal();
    closeLegendModal();
    closeChannelActionsModal();
    // Tools too, since issue #94: it is a full screen with a working nav bar
    // now, so this is reachable by tapping a cell from inside it. It used to be
    // unreachable — Tools was a card on a backdrop that swallowed the tap — and
    // without this Tools would stay alive and allocated underneath, and reappear
    // when this screen closed.
    if (s_liveToolsModal) closeLiveToolsModal();
    nodesPanelWifiEnter();

    // This modal frame + row list can be large; bail out before lv_obj_create
    // would hit LV_ASSERT_MALLOC and hard-reset the device.
    {
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 8192) {
            logLvglMemDiag("openNodesModal aborted (low LVGL mem)");
            return;
        }
    }

    // Each visit opens on the favorites, with no search carried over, and
    // the cursor on the first row rather than the Search button.
    s_nodesFilterOpen = false;
    s_nodesSearchBtnFocused = false;
    s_nodesTopBtn = kNodesTopSearch;
    s_nodesFilterLen = 0;
    s_nodesFilter[0] = '\0';
    snapshotNodesForModal();

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);
    const int modalPad = 4;
    const int contentGap = 3;
    int contentW = modalW - (modalPad * 2);
    if (contentW < 120) contentW = modalW;
    // Less the nav column where the bar is one (Left/Right), as on DMs: the
    // panes are sized absolutely and appendHeltecBottomNav() pads it off.
    if (navIsSideColumn()) contentW -= kSideNavW;

    // List over details, both full width, rather than side by side: the T-Deck
    // Pro's portrait paper, and the T-Display P4 held upright, where 284 px is
    // too narrow to split and there is height to stack into.
#if defined(DEVICE_TDECK_PRO)
    const bool nodesStacked = true;
#elif defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_HELTEC_V4_EXPANSION) \
    || defined(DEVICE_WIO_TRACKER_L2)
    // The Heltec V4 and the Wio Tracker upright too: 240 px is narrower still.
    const bool nodesStacked = uiPortrait();
#else
    const bool nodesStacked = false;
#endif

    // One size on every board. The Pager and T-Deck used to take montserrat_14
    // here, which suited the old single-block description and is much too big
    // for a two-column table: at that size the field names ate the row and the
    // values ellipsized down to nothing. This is what the M9 renders on the same
    // 320x240 panel the T-Deck has, and it is the size the tables were laid out
    // against. The Cardputer, on its own layout, was already using it.
#if defined(DEVICE_TDECK_PRO)
    const lv_font_t *nodesDetailFont = emojiFont(&lv_font_montserrat_12);
#elif defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
    // Stacked, the details have the full panel width and scroll for height, so
    // they take the list's larger face (refreshNodesListRows).
    const lv_font_t *nodesDetailFont =
        emojiFont(nodesStacked ? &lv_font_montserrat_14 : &lv_font_montserrat_10);
#elif defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_WIO_TRACKER_L2)
    // Stacked upright, as on the P4, the details have the full width -- a size
    // up rather than the P4's two, for a panel a third of its height.
    const lv_font_t *nodesDetailFont =
        emojiFont(nodesStacked ? &lv_font_montserrat_12 : &lv_font_montserrat_10);
#elif defined(DEVICE_CROWPANEL_50)
    // A size under the list's, as everywhere: the table has two columns to
    // fit. Named 16, drawn as 18.
    const lv_font_t *nodesDetailFont = emojiFont(&lv_font_montserrat_16);
#else
    const lv_font_t *nodesDetailFont = emojiFont(&lv_font_montserrat_10);
#endif
#if NODES_LAYOUT_WIDE
    int listW = contentW;
    int detailW = contentW;
    if (!nodesStacked) {
        // The list is on the left now and carries long names, so it gets a real
        // share of the width instead of the ~22-24% strip it had when it only ever
        // showed a four-character short name. The detail panel keeps the remainder
        // and has a floor of its own: below that its two columns stop being a table.
        const int kNodesDetailMinW = 110;
        listW = (contentW * 45) / 100;
        if (contentW - listW - contentGap < kNodesDetailMinW) {
            listW = contentW - contentGap - kNodesDetailMinW;
        }
        // Pathologically narrow panel: give the list something rather than a
        // negative width, and let the detail panel scroll for the rest.
        if (listW < 60) listW = 60;
        // Less the 1 px rule between them and the second gap it brings.
        detailW = contentW - listW - 2 * contentGap - 1;
        if (detailW < 60) detailW = 60;
    }
#else
    // Cardputer: unchanged from before the wide layout — a narrow list of short
    // names on the right, details taking the rest on the left.
    int listW = max(68, min(96, (contentW * 24) / 100));
    // Less the 1 px rule between them and the second gap it brings.
    int detailW = contentW - listW - 2 * contentGap - 1;
    if (detailW < 120) {
        int deficit = 120 - detailW;
        listW = max(52, listW - deficit);
        detailW = contentW - listW - 2 * contentGap - 1;
    }
#endif

    s_nodesModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodesModal, modalW, modalH);
    lv_obj_align(s_nodesModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodesModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_nodesModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodesModal, LV_OPA_COVER, 0);
    // No border: this screen fills the display, and the 1 px frame only took
    // room from it. The headers and nav bar read the width, so they follow.
    lv_obj_set_style_border_width(s_nodesModal, 0, 0);
    lv_obj_set_style_pad_all(s_nodesModal, 4, 0);
    lv_obj_set_style_pad_row(s_nodesModal, 4, 0);
    lv_obj_set_flex_flow(s_nodesModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Retitled with the count (and the search, while one is in force) by
    // refreshNodesListRows(), through screenHeaderSetTitle().
    s_nodesTitleLabel = buildScreenHeader(s_nodesModal, "");

    lv_obj_t *content = lv_obj_create(s_nodesModal);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    // The rule between the panes reaches past this box to the display edges
    // when they are stacked (buildPaneRule()).
    lv_obj_add_flag(content, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    if (nodesStacked) {
        lv_obj_set_style_pad_row(content, contentGap, 0);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_style_pad_column(content, contentGap, 0);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    }
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

#if NODES_LAYOUT_WIDE
    // Node list, on the left. It reads first and it is what you steer, so it is
    // the panel your eye lands on; the details describe whatever it is pointing
    // at. (This pair used to be the other way round, from when the list was a
    // narrow strip of four-character short names.)
    lv_obj_t *listPanel = lv_obj_create(content);
    s_nodesListPanel = listPanel;
    lv_obj_set_width(listPanel, listW);
#if NODES_INFO_AS_MODAL
    // Nothing under it: the details are the Node Info action's modal.
    lv_obj_set_flex_grow(listPanel, 1);
#else
    // Stacked (P4 portrait): the list takes the top 45% and the details grow
    // into the rest; side by side it runs the full height.
    lv_obj_set_height(listPanel, nodesStacked ? lv_pct(45) : lv_pct(100));
#endif
    lv_obj_clear_flag(listPanel, LV_OBJ_FLAG_SCROLLABLE);
    // No frame: a rule divides the panes, and the fill marks the focused one
    // (refreshNodesPanelFocusStyles()).
    lv_obj_set_style_bg_opa(listPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listPanel, 0, 0);
    lv_obj_set_style_pad_left(listPanel, 2, 0);
    lv_obj_set_style_pad_right(listPanel, 1, 0);
    lv_obj_set_style_pad_top(listPanel, 2, 0);
    lv_obj_set_style_pad_bottom(listPanel, 2, 0);

    buildNodesSearchButton(listPanel);

    s_nodesList = lv_obj_create(listPanel);
    lv_obj_set_width(s_nodesList, lv_pct(100));
    lv_obj_set_flex_grow(s_nodesList, 1);
    lv_obj_add_flag(s_nodesList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_nodesList);
    lv_obj_set_scrollbar_mode(s_nodesList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_nodesList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_nodesList, 0, 0);
    lv_obj_set_style_pad_left(s_nodesList, 1, 0);
    lv_obj_set_style_pad_right(s_nodesList, 8, 0);
    lv_obj_set_style_pad_top(s_nodesList, 0, 0);
    lv_obj_set_style_pad_bottom(s_nodesList, 0, 0);
    lv_obj_set_style_pad_row(s_nodesList, 2, 0);
    lv_obj_set_style_width(s_nodesList, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_nodesList, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_nodesList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_nodesList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_nodesList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_nodesList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

#if !NODES_INFO_AS_MODAL
    buildPaneRule(content, nodesStacked);
    buildNodesInfoPanel(content, detailW, nodesStacked, nodesDetailFont);
#else
    LV_UNUSED(detailW);
    LV_UNUSED(nodesDetailFont);
#endif
#else
    // Cardputer: the original arrangement — detail panel on the left taking most
    // of the width, narrow node list on the right.
    lv_obj_t *infoPanel = lv_obj_create(content);
    s_nodesInfoPanel = infoPanel;
    lv_obj_set_width(infoPanel, detailW);
    lv_obj_set_height(infoPanel, lv_pct(100));
    lv_obj_add_flag(infoPanel, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(infoPanel);
    lv_obj_set_scrollbar_mode(infoPanel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(infoPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(infoPanel, 0, 0);
    lv_obj_set_style_pad_all(infoPanel, 4, 0);
    lv_obj_set_style_width(infoPanel, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(infoPanel, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(infoPanel, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(infoPanel, 2, LV_PART_SCROLLBAR);

    s_nodesDetail = lv_label_create(infoPanel);
    lv_obj_set_width(s_nodesDetail, lv_pct(100));
    lv_obj_set_style_text_font(s_nodesDetail, nodesDetailFont, 0);
    lv_obj_set_style_text_color(s_nodesDetail, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(s_nodesDetail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_nodesDetail, "");

    buildPaneRule(content, false);

    lv_obj_t *listPanel = lv_obj_create(content);
    s_nodesListPanel = listPanel;
    lv_obj_set_width(listPanel, listW);
    lv_obj_set_height(listPanel, lv_pct(100));
    lv_obj_clear_flag(listPanel, LV_OBJ_FLAG_SCROLLABLE);
    // No frame: a rule divides the panes, and the fill marks the focused one
    // (refreshNodesPanelFocusStyles()).
    lv_obj_set_style_bg_opa(listPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listPanel, 0, 0);
    lv_obj_set_style_pad_left(listPanel, 2, 0);
    lv_obj_set_style_pad_right(listPanel, 1, 0);
    lv_obj_set_style_pad_top(listPanel, 2, 0);
    lv_obj_set_style_pad_bottom(listPanel, 2, 0);

    buildNodesSearchButton(listPanel);

    s_nodesList = lv_obj_create(listPanel);
    lv_obj_set_width(s_nodesList, lv_pct(100));
    lv_obj_set_flex_grow(s_nodesList, 1);
    lv_obj_add_flag(s_nodesList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_nodesList);
    lv_obj_set_scrollbar_mode(s_nodesList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_nodesList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_nodesList, 0, 0);
    lv_obj_set_style_pad_left(s_nodesList, 1, 0);
    lv_obj_set_style_pad_right(s_nodesList, 8, 0);
    lv_obj_set_style_pad_top(s_nodesList, 0, 0);
    lv_obj_set_style_pad_bottom(s_nodesList, 0, 0);
    lv_obj_set_style_pad_row(s_nodesList, 2, 0);
    lv_obj_set_style_width(s_nodesList, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_nodesList, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_nodesList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_nodesList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_nodesList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_nodesList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
#endif  // NODES_LAYOUT_WIDE


    s_nodesListRowCount = 0;
    s_nodesRenderStart = 0;
    memset(s_nodesRenderedFilteredIdx, 0, sizeof(s_nodesRenderedFilteredIdx));
    memset(s_nodesListRows, 0, sizeof(s_nodesListRows));

    s_nodesInfoFocused = false;
    nodesApplyFilter();
    refreshNodesListRows();

    refreshNodesListSelection();
    refreshNodesDetails();
    refreshNodesPanelFocusStyles();

#if UI_TOUCH_ONLY_PROFILE
    appendHeltecBottomNav(s_nodesModal, HELTEC_NAV_NODES);
#else
    lv_obj_t *hint = lv_label_create(s_nodesModal);
    s_nodesHintLabel = hint;
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    // The legend itself is refreshNodesHint()'s to write, and now that the label
    // exists it can. The earlier refreshNodesListRows() call above ran before
    // this label was created, so its own attempt was a no-op. This used to be a
    // hardcoded copy of the same text, which had already drifted from the real
    // one in its spacing.
    refreshNodesHint();
#endif

#if HAS_NAV_BAR_TOGGLE
    // Keyboard board with the bar switched on: it joins the key hints above
    // rather than replacing them — the hints name the keys this screen answers
    // to, which six navigation buttons say nothing about. Appended after the
    // hint so the bar's layout spacer is the last child and the hint is not
    // pushed under it. A no-op with the bar switched off, which is what leaves
    // this screen exactly as it looked before the bar existed.
    appendHeltecBottomNav(s_nodesModal, HELTEC_NAV_NODES);
#endif
}

// ── Release notes ────────────────────────────────────────────────────────────
// Shows the notes baked into this image at build time (RELEASE_NOTES.md ->
// src/release_notes.h). Nothing is fetched: the text is already in flash, so it
// works with the radio off, no WiFi, and on boards without OTA.
static void closeReleaseNotesModal() {
    if (lvObjValid(s_releaseNotesBackdrop)) {
        lv_obj_del(s_releaseNotesBackdrop);
    } else if (lvObjValid(s_releaseNotesModal)) {
        lv_obj_del(s_releaseNotesModal);
    }
    s_releaseNotesBackdrop = nullptr;
    s_releaseNotesModal = nullptr;
    s_releaseNotesScroll = nullptr;
}

static void onReleaseNotesBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_releaseNotesBackdrop) return;
    closeReleaseNotesModal();
}

#if UI_TOUCH_ONLY_PROFILE
static void onReleaseNotesClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeReleaseNotesModal();
}
#endif

static void openReleaseNotesModal() {
    if (!s_rootScreen || s_releaseNotesModal || s_releaseNotesBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);

    s_releaseNotesBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_releaseNotesBackdrop, w, h);
    lv_obj_align(s_releaseNotesBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_releaseNotesBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_releaseNotesBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_releaseNotesBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_releaseNotesBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_releaseNotesBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_releaseNotesBackdrop, 0, 0);
    lv_obj_add_event_cb(s_releaseNotesBackdrop, onReleaseNotesBackdropPressed,
                        LV_EVENT_CLICKED, nullptr);

    // Notes are prose, so this takes as much of the panel as it can get.
    int modalW = w - 12;
    if (modalW < 120) modalW = w - 4;
    const int modalH = (h > 40) ? (h - 12) : h;

    s_releaseNotesModal = lv_obj_create(s_releaseNotesBackdrop);
    lv_obj_set_size(s_releaseNotesModal, modalW, modalH);
    lv_obj_align(s_releaseNotesModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_releaseNotesModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_releaseNotesModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_releaseNotesModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_releaseNotesModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_releaseNotesModal, 1, 0);
    lv_obj_set_style_border_color(s_releaseNotesModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_releaseNotesModal, 6, 0);
    lv_obj_set_style_pad_row(s_releaseNotesModal, 4, 0);
    lv_obj_set_flex_flow(s_releaseNotesModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_releaseNotesModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_releaseNotesBackdrop);

    lv_obj_t *title = lv_label_create(s_releaseNotesModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(title, TR("Release Notes  %s"), APP_VERSION);

    // Takes the space the title and the footer below don't, so the notes scroll
    // inside a frame rather than carrying the header off the top with them.
    s_releaseNotesScroll = lv_obj_create(s_releaseNotesModal);
    lv_obj_set_width(s_releaseNotesScroll, lv_pct(100));
    lv_obj_set_flex_grow(s_releaseNotesScroll, 1);
    setupVScroll(s_releaseNotesScroll);
    lv_obj_set_scrollbar_mode(s_releaseNotesScroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_releaseNotesScroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_releaseNotesScroll, 0, 0);
    lv_obj_set_style_pad_all(s_releaseNotesScroll, 0, 0);
    lv_obj_set_style_pad_right(s_releaseNotesScroll, 2, 0);

    lv_obj_t *body = lv_label_create(s_releaseNotesScroll);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    if (RELEASE_NOTES_TEXT[0]) {
        lv_label_set_text(body, RELEASE_NOTES_TEXT);
    } else {
        // A dev build, or a release whose summary generation failed.
        lv_label_set_text(body, TR("No release notes in this build."));
    }

#if UI_TOUCH_ONLY_PROFILE
    // Touch-only: the backdrop margin is too thin to aim at, so closing needs a
    // real target. The X takes no row of its own, so the notes get the height
    // the old footer button was spending.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_releaseNotesModal, onReleaseNotesClosePressed);
#else
    lv_obj_t *hint = lv_label_create(s_releaseNotesModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if defined(DEVICE_TDECK)
    // No dedicated Up/Down keys here; J/K and the trackball drive scroll.
    lv_label_set_text(hint, TR("J/K = Scroll   Bksp = Close"));
#else
    lv_label_set_text(hint, TR("Up/Down/J/K = Scroll   Bksp = Close"));
#endif
#endif
}

// Help's prose was written with its line breaks in, sized to the old 10 px
// face. At the larger one those breaks land mid-line and leave every line
// ragged, so a paragraph is run back together and the label wraps it to the
// width it actually has. Only prose goes through here: the key lists and the
// transport key are one item per line on purpose.
static const char *legendReflow(const char *text, char *buf, size_t bufLen) {
    size_t n = 0;
    for (const char *p = text; *p && n + 1 < bufLen; p++) buf[n++] = (*p == '\n') ? ' ' : *p;
    buf[n] = '\0';
    return buf;
}

// The Help screen's transport-icon key, shared by every board's layout. The
// icons go in as arguments so the translated text keeps them.
static const char *legendTransportText() {
    static char buf[160];
    snprintf(buf, sizeof(buf),
             TR("Transport Symbols:\n%s Radio Transmission\n%s MQTT Transmission"),
             LV_SYMBOL_RADIO_TINY, LV_SYMBOL_GLOBE_TINY);
    return buf;
}

// How to reach the channel list on this board, for Help. Empty where there is
// nothing to reach it for: the Pager and the large-panel P4 in landscape keep
// the list anchored beside the chat, always in view. Everywhere else it is the drawer
// (channelDrawerSlide()), and what opens it differs by what the board has --
// a swipe where there is touch, the chat key where there is a keyboard, the
// Messages button on the M9.
static const char *legendChannelListTextRaw() {
#if UI_CHANNEL_LIST_DROPDOWN
#if HAS_CHANNEL_PANEL_LOCK && !UI_TOUCH_ONLY_PROFILE
    if (!channelListIsDropdown()) {
        return TR("Channels: Left or C, Up/Down to move,\n"
                  "Enter picks. Right twice hides it.");
    }
    return TR("Channels: Left opens the list, Left\n"
              "again docks it beside the chat.");
#endif
    if (!channelListIsDropdown()) return "";
#if defined(DEVICE_M9)
    return TR("Channels: press Messages again on the\n"
              "chat screen. Enter picks one.");
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    return TR("Channels: H on the chat screen.\nEnter picks one.");
#elif UI_TOUCH_ONLY_PROFILE
    return TR("Channels: tap the channel name at the\n"
              "top, or swipe in from the left edge.");
#elif HAS_TOUCH
    return TR("Channels: C on the chat screen, tap the\n"
              "channel name, or swipe in from the left.");
#else
    return TR("Channels: C on the chat screen.\nEnter picks one.");
#endif
#else
    return "";
#endif
}

static const char *legendChannelListText() {
    static char buf[200];
    return legendReflow(legendChannelListTextRaw(), buf, sizeof(buf));
}

static void openLegendModal() {
    if (s_legendModal && !lvObjAlive(s_legendModal)) {
        s_legendModal = nullptr;
        s_legendScroll = nullptr;
    }
    if (!s_rootScreen || s_legendModal) return;

    // Help replaces the screen you were on rather than floating over it (issue
    // #95). It used to close only DM, which made it the one nav destination that
    // stacked: tap Nodes then ?, and Help landed as a card over the roster with
    // the Nodes cell still lit on the bar underneath it. Every other cell on that
    // bar replaces the screen, and now this one does too.
    //
    // Innermost first, the same order and the same reason as the Home/Chat
    // teardown in onHeltecBottomNavPressed(): a menu opened from one of these
    // screens has to go before the screen it was opened from. The matching
    // closeLegendModal() calls in the other openers are the symmetric other half
    // of this rather than a one-way rule.
    if (s_composeModal) closeComposePrompt();
    if (s_liveToolsModal) closeLiveToolsModal();
#if HAS_WEATHER
    closeWeatherModal();
#endif
    if (s_nodesActionModal) closeNodesActionMenu();
    if (s_channelActionsModal) closeChannelActionsModal();
    if (s_cfgModal) closeCfgModal();
    closeDmModal();
    if (s_nodesModal) closeNodesModal();
    if (s_liveModal) closeLiveModal();

    // Same guard openLiveModal() takes: Help builds a nav bar of its own now, so
    // it allocates more than the old card did, and LV_ASSERT_MALLOC resets the
    // device rather than failing. The closes above have already freed whatever
    // was on screen.
    {
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 6144) {
            logLvglMemDiag("openLegendModal aborted (low LVGL mem)");
            return;
        }
    }

    // Sized to be read, not to fit: the body scrolls where it runs long, so
    // the face no longer has to shrink until the whole list squeezes onto the
    // panel. The Cardputer's 240x135 stays at 10 -- one line of 14 there is a
    // tenth of the screen.
#if UI_CARDPUTER_COMPACT
    const lv_font_t *legendBodyFont = &lv_font_montserrat_10;
#elif defined(DEVICE_TDECK)
    // Two columns in 320 px: 12 is as large as the key list goes and still
    // keeps one key to a line.
    const lv_font_t *legendBodyFont = &lv_font_montserrat_12;
#elif defined(DEVICE_TDISPLAY_P4)
    const lv_font_t *legendBodyFont =
        uiPortrait() ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
#elif defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_WIO_TRACKER_L2)
    // Upright as on the P4: the column is narrow, the prose reflows, and the
    // body scrolls, so the larger face costs nothing but scrolling.
    const lv_font_t *legendBodyFont =
        uiPortrait() ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
#else
    const lv_font_t *legendBodyFont = &lv_font_montserrat_14;
#endif

    // A screen, not a card. Help owns the last cell on the nav bar, so it has to
    // be the kind of thing a cell leads to — full bleed, its own copy of the bar,
    // its own cell lit.
    //
    // This is what retires the height ladder that used to stand here: 132 by
    // default, 126 touch-only landscape, 142 touch-only portrait, 146 on the
    // Pager and T-Deck, +14 once the key list gained a ninth row, then clamped to
    // ver_res - 16 because the Pager is only 222 px tall, and a scrollbar on the
    // Cardputer because none of it was enough there. That pile was the symptom:
    // the key list had outgrown the card. A full screen has the room it was being
    // squeezed for, on every panel, with no per-board arithmetic at all.
    //
    // Full bleed on every board, including one with the nav bar switched off and
    // therefore no ? cell to light. One layout is the whole point — the ladder
    // above existed regardless of the bar, and so did the content that outgrew it.
    //
    // pad_all 4 is load-bearing: appendHeltecBottomNav() and the screen header
    // cancel it (and the border, which this screen does without, as DMs,
    // Nodes, Config and Tools do) to reach the display edges.
    s_legendModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_legendModal, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_align(s_legendModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_legendModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_legendModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_legendModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_legendModal, 0, 0);
    lv_obj_set_style_pad_all(s_legendModal, 4, 0);
    lv_obj_set_style_pad_row(s_legendModal, 4, 0);
    lv_obj_set_flex_flow(s_legendModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_legendModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // The chat screen's header, named for this screen, as on DMs, Nodes, Config
    // and Tools. No close X under it on any build: the nav bar is the way out,
    // as it is from those.
    buildScreenHeader(s_legendModal, TR("Help"));

    // The body scrolls, not the screen. With the larger face the key list runs
    // past the bottom on the smaller panels, and scrolling the whole screen
    // would carry it up over the header, which floats in place.
    s_legendScroll = lv_obj_create(s_legendModal);
    lv_obj_remove_style_all(s_legendScroll);
    lv_obj_set_width(s_legendScroll, lv_pct(100));
    lv_obj_set_flex_grow(s_legendScroll, 1);
    lv_obj_add_flag(s_legendScroll, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_legendScroll);
    lv_obj_set_scrollbar_mode(s_legendScroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(s_legendScroll, 6, 0);   // clear of the scrollbar
    lv_obj_set_style_width(s_legendScroll, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_legendScroll, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_legendScroll, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_legendScroll, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_row(s_legendScroll, 4, 0);
    lv_obj_set_flex_flow(s_legendScroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_legendScroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

#if UI_TOUCH_ONLY_PROFILE
    lv_obj_t *body = lv_label_create(s_legendScroll);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, legendBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    // One TR() key per paragraph: a single literal cannot span the #if, so
    // the pieces are translated separately and joined here.
    const char *channelsHelp = legendChannelListText();
    char navHelp[320];
    legendReflow(
#if HAS_HOME_DASHBOARD
        TR("Bottom buttons: Home, Chats, DM, Nodes, Tools, Config, Help.\n"
           "Home is the dashboard; Chats is the messages, and its icon\n"
           "blinks when a channel has something unread."),
#else
        // Unreachable today — both touch-only boards get FEATURE_LOCK_SCREEN and
        // so HAS_HOME_DASHBOARD — but it named the cells as they were two
        // changes ago: Live left the bar for Tools, and Chats joined it.
        TR("Bottom buttons: Chats, DM, Nodes, Tools, Config, Help."),
#endif
        navHelp, sizeof(navHelp));
    lv_label_set_text_fmt(
        body,
        "%s\n%s%s%s\n\n%s",
        TR("Touch Navigation:"),
        navHelp,
        channelsHelp[0] ? "\n\n" : "", channelsHelp,
        legendTransportText());
#elif defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    lv_obj_t *bodyRow = lv_obj_create(s_legendScroll);
    lv_obj_set_width(bodyRow, lv_pct(100));
    lv_obj_set_height(bodyRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(bodyRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bodyRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bodyRow, 0, 0);
    lv_obj_set_style_pad_all(bodyRow, 0, 0);
    lv_obj_set_style_pad_column(bodyRow, 8, 0);
    lv_obj_set_flex_flow(bodyRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bodyRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *leftCol = lv_label_create(bodyRow);
    lv_obj_set_width(leftCol, lv_pct(50));
    lv_obj_set_style_text_font(leftCol, legendBodyFont, 0);
    lv_obj_set_style_text_color(leftCol, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(leftCol, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        leftCol,
#if HAS_HOME_DASHBOARD
        TR("(H) Home dashboard\n"
           "(C) Chat (again: channels)\n"
           "(F) Configuration\n"
           "(D) Direct Messages\n"
           "(N) Nodes\n"
           "(L) Tools (Live, charts)\n"
           "(A) Channel Actions\n"
           "(P) Help (this screen)\n"
           "(E) Emoji\n"
           "(Space) Compose/Reply\n"
           "(Enter) Focus Messages"));
#else
        TR("(D) Direct Messages\n"
           "(C) Configuration\n"
           "(N) Nodes\n"
           "(L) Tools (Live, charts)\n"
           "(A) Channel Actions\n"
           "(H) Channel selector\n"
           "(P) Help (this screen)\n"
           "(E) Emoji\n"
           "(Space) Compose/Reply\n"
           "(Enter) Focus Messages"));
#endif

    lv_obj_t *rightCol = lv_obj_create(bodyRow);
    lv_obj_set_width(rightCol, lv_pct(50));
    lv_obj_set_height(rightCol, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(rightCol, 1);
    lv_obj_clear_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(rightCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightCol, 0, 0);
    lv_obj_set_style_pad_all(rightCol, 0, 0);
    lv_obj_set_style_pad_row(rightCol, 2, 0);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *rightMain = lv_label_create(rightCol);
    lv_obj_set_width(rightMain, lv_pct(100));
    lv_obj_set_style_text_font(rightMain, legendBodyFont, 0);
    lv_obj_set_style_text_color(rightMain, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(rightMain, LV_LABEL_LONG_WRAP);
    lv_label_set_text(rightMain, legendTransportText());

    // Empty on the Pager, whose list is anchored beside the chat.
    if (legendChannelListText()[0]) {
        lv_obj_t *rightChannels = lv_label_create(rightCol);
        lv_obj_set_width(rightChannels, lv_pct(100));
        lv_obj_set_style_text_font(rightChannels, legendBodyFont, 0);
        lv_obj_set_style_text_color(rightChannels, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_long_mode(rightChannels, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(rightChannels, "\n%s", legendChannelListText());
    }

#if defined(DEVICE_TDECK)
    lv_obj_t *rightNote = lv_label_create(rightCol);
    lv_obj_set_width(rightNote, lv_pct(100));
    // Use a larger font to emphasize this as a bold-style callout.
    lv_obj_set_style_text_font(rightNote, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rightNote, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_long_mode(rightNote, LV_LABEL_LONG_WRAP);
    lv_label_set_text(rightNote, TR("\nT-Deck trackball: hold click 2s to sleep"));
#endif
#else
    lv_obj_t *body = lv_label_create(s_legendScroll);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, legendBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    const char *channelsHelp = legendChannelListText();
    lv_label_set_text_fmt(
        body,
        "%s\n\n%s%s%s",
#if HAS_HOME_DASHBOARD
        TR("(H) Home dashboard\n"
           "(C) Chat (again: channels)\n"
           "(F) Configuration\n"
           "(D) Direct Messages\n"
           "(N) Nodes\n"
           "(L) Tools (Live, charts)\n"
           "(A) Channel Actions\n"
           "(P) Help (this screen)\n"
           "(E) Emoji\n"
           "(Space) Compose/Reply\n"
           "(Enter) Focus Messages"),
#else
        TR("(D) Direct Messages\n"
           "(C) Configuration\n"
           "(N) Nodes\n"
           "(L) Tools (Live, charts)\n"
           "(A) Channel Actions\n"
           "(H) Channel selector\n"
           "(P) Help (this screen)\n"
           "(E) Emoji\n"
           "(Space) Compose/Reply\n"
           "(Enter) Focus Messages"),
#endif
        channelsHelp, channelsHelp[0] ? "\n\n" : "",
        legendTransportText());
#endif

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_legendModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, legendBodyFont, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    // modalCloseKeyLabel(), not a literal "Backspace": the Cardputer sets
    // kModalCloseUsesEscape, so isModalCloseKey() there accepts Esc and nothing
    // else — this line was naming a key that does not close anything on the one
    // board whose users most need the reminder.
    lv_label_set_text_fmt(hint, TR("%s to close Help"), modalCloseKeyLabel());
#endif

    // Last, so the bar's layout spacer is the final child of the flex column and
    // the key list above is not pushed under the bar — the same ordering rule
    // Nodes, DM and Config follow. A no-op where the bar is switched off.
    appendHeltecBottomNav(s_legendModal, HELTEC_NAV_LEGEND);

    refreshChatComposeButtonState();
}

static void openCfgModal() {
    if (s_cfgModal && !lvObjAlive(s_cfgModal)) {
        s_cfgModal = nullptr;
        s_cfgActionList = nullptr;
        s_cfgInfoList = nullptr;
        s_cfgHeaderStatus = nullptr;
    }
    if (!s_rootScreen || s_cfgModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeLegendModal();
    closeChannelActionsModal();
    // Tools too, since issue #94: it is a full screen with a working nav bar
    // now, so this is reachable by tapping a cell from inside it. It used to be
    // unreachable — Tools was a card on a backdrop that swallowed the tap — and
    // without this Tools would stay alive and allocated underneath, and reappear
    // when this screen closed.
    if (s_liveToolsModal) closeLiveToolsModal();

#if UI_CFG_SECTIONS
    // Every way into Config starts at the grid; see s_cfgSection.
    s_cfgSection = -1;
    s_cfgGridSelection = 0;
    s_cfgBackBtn = nullptr;
    s_cfgInfoBtn = nullptr;
#endif
    initCfgActions();
    s_cfgSelection = 0;
    s_cfgStatus[0] = '\0';
    // The armed tag is deliberately *not* cleared here. It is set by a release
    // check that succeeded this boot, and the whole point of arming it is that
    // the Firmware Update row then reads "Install <tag>" -- clearing it on open
    // would make that label unreachable for the boot check, which finds its
    // update long before anyone opens Config. It is dropped where it actually
    // goes stale: a channel switch, and the update action itself.
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    s_cfgLastActivateMs = 0;
    s_cfgLastScrollMs = 0;
    s_cfgEnterLockUntilMs = 0;
    s_cfgAwaitEnterRelease = false;
    s_cfgInfoPanelFocused = false;
    if (s_cfgDebugLog) {
        Serial.printf("[lvgl-cfg] open actions=%d\n", s_cfgActionCount);
    }

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_cfgModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgModal, modalW, modalH);
    lv_obj_align(s_cfgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_cfgModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgModal, LV_OPA_COVER, 0);
    // No border: this screen fills the display, and the 1 px frame only took
    // room from it. The headers and nav bar read the width, so they follow.
    lv_obj_set_style_border_width(s_cfgModal, 0, 0);
    lv_obj_set_style_pad_all(s_cfgModal, 4, 0);
    lv_obj_set_style_pad_row(s_cfgModal, 4, 0);
    lv_obj_set_flex_flow(s_cfgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // The status this bar used to carry beside the title ("Ready", or the
    // armed filter) is in the title now -- refreshCfgModal() -- and Info is in
    // the secondary header under it, as Nodes' Filter is.
    s_cfgHeaderStatus = buildScreenHeader(s_cfgModal, TR("Configuration"));

#if UI_CFG_SECTIONS || UI_TOUCH_ONLY_PROFILE
    lv_obj_t *cfgSubHeader = buildSecondaryHeader(s_cfgModal);
#endif
#if UI_CFG_SECTIONS
    // First, so it sits at the left where a way back is looked for. Hidden on
    // the grid by refreshCfgModal().
    {
        char backText[32];
        snprintf(backText, sizeof(backText), LV_SYMBOL_LEFT " %s", TR("Back"));
        s_cfgBackBtn = secondaryHeaderBtn(cfgSubHeader, backText, onCfgBackPressed);
        lv_obj_add_flag(s_cfgBackBtn, LV_OBJ_FLAG_HIDDEN);
    }
#endif
#if UI_TOUCH_ONLY_PROFILE
#if UI_CFG_SECTIONS
    s_cfgInfoBtn =
#endif
    secondaryHeaderBtn(cfgSubHeader, TR("Info"), onCfgHeaderInfoPressed);
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_t *cfgColumns = lv_obj_create(s_cfgModal);
    lv_obj_set_width(cfgColumns, lv_pct(100));
    lv_obj_set_flex_grow(cfgColumns, 1);
    lv_obj_clear_flag(cfgColumns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cfgColumns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cfgColumns, 0, 0);
    lv_obj_set_style_pad_all(cfgColumns, 0, 0);
    lv_obj_set_style_pad_column(cfgColumns, 4, 0);
    lv_obj_set_flex_flow(cfgColumns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cfgColumns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_cfgActionList = lv_obj_create(cfgColumns);
    lv_obj_set_width(s_cfgActionList, lv_pct(58));
    lv_obj_set_height(s_cfgActionList, lv_pct(100));
    lv_obj_add_flag(s_cfgActionList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgActionList);
    lv_obj_set_scrollbar_mode(s_cfgActionList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgActionList, 1, 0);
    lv_obj_set_style_border_color(s_cfgActionList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgActionList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgActionList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgActionList, 2, 0);
    lv_obj_set_style_width(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_cfgActionList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_cfgInfoList = lv_obj_create(cfgColumns);
    lv_obj_set_width(s_cfgInfoList, lv_pct(42));
    lv_obj_set_height(s_cfgInfoList, lv_pct(100));
    lv_obj_add_flag(s_cfgInfoList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgInfoList);
    lv_obj_set_scrollbar_mode(s_cfgInfoList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgInfoList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgInfoList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgInfoList, 1, 0);
    lv_obj_set_style_border_color(s_cfgInfoList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgInfoList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgInfoList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgInfoList, 2, 0);
    lv_obj_set_style_width(s_cfgInfoList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_cfgInfoList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_cfgInfoList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_cfgInfoList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_cfgInfoList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgInfoList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#else
    s_cfgActionList = lv_obj_create(s_cfgModal);
    lv_obj_set_width(s_cfgActionList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgActionList, 1);
    lv_obj_add_flag(s_cfgActionList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgActionList);
    lv_obj_set_scrollbar_mode(s_cfgActionList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgActionList, 1, 0);
    lv_obj_set_style_border_color(s_cfgActionList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgActionList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgActionList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgActionList, 2, 0);
    lv_obj_set_style_width(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_cfgActionList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#endif

#if UI_TOUCH_ONLY_PROFILE
    appendHeltecBottomNav(s_cfgModal, HELTEC_NAV_CFG);
#else
    lv_obj_t *hint = lv_label_create(s_cfgModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_pad_top(hint, 0, 0);
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_MESH_DECK)
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
#else
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, TR("To close hit backspace.  (I)nformation"));
#endif

#if HAS_NAV_BAR_TOGGLE
    // Keyboard board with the bar switched on: it joins the key hints above
    // rather than replacing them — the hints name the keys this screen answers
    // to, which six navigation buttons say nothing about. Appended after the
    // hint so the bar's layout spacer is the last child and the hint is not
    // pushed under it. A no-op with the bar switched off, which is what leaves
    // this screen exactly as it looked before the bar existed.
    appendHeltecBottomNav(s_cfgModal, HELTEC_NAV_CFG);
#endif

    refreshCfgModal();
}

static void activateCfgSelection() {
    if (s_cfgActionCount <= 0 || s_cfgSelection < 0 || s_cfgSelection >= s_cfgActionCount) return;
    const int actionId = s_cfgActions[s_cfgSelection];
    if (cfgActionDisabled(actionId)) {
        const char *disabledMessage = TR("Enable WiFi first");
#if HAS_VNC_HOST
        if (actionId == CFG_ACTION_VNC_HOST && !vncNetworkConnected()) {
            disabledMessage = TR("Connect to WiFi before enabling VNC");
        }
#endif
#if HAS_BLE_KEYBOARD
        if (actionId == CFG_ACTION_BLE_KBD_PAIR) {
            disabledMessage = TR("Turn BT Keyboard on first");
        }
#endif
        if ((actionId == CFG_ACTION_CS_MODE || actionId == CFG_ACTION_CS_CHECK) && lic::active()) {
            disabledMessage = TR("Chat server is off while licensed");
        }
        if (cfgGenIsAction(actionId)) {
            const char *why = nullptr;
            cfgGenDisabled(actionId - CFG_ACTION_GEN_FIRST, &why);
            if (why) disabledMessage = TR(why);
        }
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", disabledMessage);
        openCfgActionMessageModal(s_cfgStatus);
        return;
    }
    if (s_cfgDebugLog) {
        char actionText[80];
        Serial.printf("[lvgl-cfg] activate sel=%d action=%d label=\"%s\" confirmAction=%d dtConfirm=%lu dtAct=%lu dtScroll=%lu\n",
                      s_cfgSelection,
                      actionId,
                      cfgActionLabel(actionId, actionText, sizeof(actionText)),
                      s_cfgConfirmAction,
                      (unsigned long)(millis() - s_cfgConfirmMs),
                      (unsigned long)(millis() - s_cfgLastActivateMs),
                      (unsigned long)(millis() - s_cfgLastScrollMs));
    }

    if (cfgActionNeedsConfirm(actionId)) {
        if (cfgActionConfirmSentence(actionId, s_cfgConfirmText, sizeof(s_cfgConfirmText))) {
            openCfgConfirmModal(actionId, s_cfgConfirmText);
        } else {
            openCfgConfirmModal(actionId);
        }
        return;
    }

    performCfgAction(actionId);
}

// Executes a CFG action immediately. Confirmable actions reach here only after
// the user answers Yes in the confirmation dialog; others come straight from
// activateCfgSelection.
#if HAS_SD_MALWARE_SCAN
// How many files the scan that raised the dialog examined. Handed to the repair
// so that pressing Yes does not spend a second full walk of the card counting
// again -- the number is already known and has not changed.
static uint16_t s_sdScanLastTotal = 0;

// Wider than s_cfgConfirmText: this dialog names files rather than an action,
// and its box scrolls, so the list is bounded by what is useful to read rather
// than by what fits on the panel.
static char s_sdScanConfirmText[512] = {0};

// ── Scan progress ────────────────────────────────────────────────────────────
// The walk is synchronous and can run for tens of seconds on a full card -- each
// file is opened and read -- so loop() is not running and nothing else is going
// to repaint the panel. This modal is driven from the scan's own progress
// callback, which pumps LVGL itself. Same approach the WiFi scan and the config
// import already take from inside a Config action.
//
// Pumping LVGL from here is safe because of something this board happens to be:
// the M9 has no touch panel, so a Config row and the confirm dialog's Yes are
// both reached through pumpKeyboardInput() from loop(), never from inside an
// LVGL event callback. On a touch board onCfgActionRowPressed() and
// onCfgConfirmYesPressed() *are* event callbacks, and the lv_timer_handler()
// below would then be re-entrant. So if HAS_SD_MALWARE_SCAN is ever widened past
// this board (see config.h), the pump has to move out of the callback and the
// walk has to be driven a slice at a time from loop() instead.
static lv_obj_t *s_sdScanBackdrop = nullptr;
static lv_obj_t *s_sdScanModal    = nullptr;
static lv_obj_t *s_sdScanBar      = nullptr;
static lv_obj_t *s_sdScanStatus   = nullptr;
static lv_obj_t *s_sdScanDetail   = nullptr;
static uint32_t  s_sdScanLastPaintMs = 0;
// Set by the progress callback when Back is pressed, read by the two callers so
// a cancelled run reports as cancelled rather than as "nothing found".
static bool      s_sdScanCancelled = false;

static void closeSdScanProgressModal() {
    if (lvObjValid(s_sdScanBackdrop)) {
        lv_obj_del(s_sdScanBackdrop);
    } else if (lvObjValid(s_sdScanModal)) {
        lv_obj_del(s_sdScanModal);
    }
    s_sdScanBackdrop = nullptr;
    s_sdScanModal    = nullptr;
    s_sdScanBar      = nullptr;
    s_sdScanStatus   = nullptr;
    s_sdScanDetail   = nullptr;
}

static void openSdScanProgressModal(const char *title) {
    if (!s_rootScreen) return;
    closeSdScanProgressModal();

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    int modalW = screenW - 28;
    if (modalW < 180) modalW = screenW - 8;
    if (modalW > 300) modalW = 300;
    lv_coord_t contentW = (lv_coord_t)(modalW - 18);
    if (contentW < 64) contentW = 64;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t bodyColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const lv_color_t dimColor = lightUi ? lv_color_hex(0x35567E) : lv_color_hex(0xA7C7FF);

    s_sdScanBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_sdScanBackdrop, screenW, screenH);
    lv_obj_align(s_sdScanBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_sdScanBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    // Clickable, but with no handler: it swallows taps rather than letting them
    // reach the Config rows underneath, which nothing is servicing anyway while
    // the walk has the main loop.
    lv_obj_add_flag(s_sdScanBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_sdScanBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_sdScanBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_sdScanBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_sdScanBackdrop, 0, 0);

    s_sdScanModal = lv_obj_create(s_sdScanBackdrop);
    lv_obj_set_size(s_sdScanModal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(s_sdScanModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_sdScanModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_sdScanModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_sdScanModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sdScanModal, 1, 0);
    lv_obj_set_style_border_color(s_sdScanModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_sdScanModal, 10, 0);
    lv_obj_set_style_pad_row(s_sdScanModal, 6, 0);
    lv_obj_set_flex_flow(s_sdScanModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sdScanModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *head = lv_label_create(s_sdScanModal);
    lv_obj_set_width(head, contentW);
    lv_obj_set_style_text_font(head, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(head, titleColor, 0);
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(head, title);

    s_sdScanStatus = lv_label_create(s_sdScanModal);
    lv_obj_set_width(s_sdScanStatus, contentW);
    lv_obj_set_style_text_font(s_sdScanStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_sdScanStatus, bodyColor, 0);
    lv_label_set_long_mode(s_sdScanStatus, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_sdScanStatus, TR("Counting files on the card..."));

    s_sdScanBar = lv_bar_create(s_sdScanModal);
    lv_obj_set_width(s_sdScanBar, contentW);
    lv_obj_set_height(s_sdScanBar, 10);
    lv_bar_set_range(s_sdScanBar, 0, 100);
    lv_bar_set_value(s_sdScanBar, 0, LV_ANIM_OFF);

    // Which folder the walk is in. Elided rather than wrapped: a deep path would
    // otherwise resize the modal on every directory change and make the whole
    // dialog jitter for the length of the scan.
    s_sdScanDetail = lv_label_create(s_sdScanModal);
    lv_obj_set_width(s_sdScanDetail, contentW);
    lv_obj_set_style_text_font(s_sdScanDetail, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_sdScanDetail, dimColor, 0);
    lv_label_set_long_mode(s_sdScanDetail, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_sdScanDetail, "/");

    lv_obj_t *hint = lv_label_create(s_sdScanModal);
    lv_obj_set_width(hint, contentW);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, dimColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, TR("Back = Cancel"));

    lv_obj_move_foreground(s_sdScanBackdrop);
    s_sdScanLastPaintMs = 0;   // so the first callback paints rather than waiting
    s_sdScanCancelled = false;
    lv_timer_handler();
}

// Handed to the storage walk. Throttled: every repaint is a full flush over SPI,
// and pumping on each of several thousand files would cost more than the reads
// it is reporting on.
static bool sdScanProgress(uint16_t done, uint16_t total, uint16_t found,
                           const char *where) {
    if (!s_sdScanModal) return true;

    const uint32_t now = millis();
    if (s_sdScanLastPaintMs != 0 && (uint32_t)(now - s_sdScanLastPaintMs) < 60) return true;
    s_sdScanLastPaintMs = now;

    // Cancel. loop() is not running -- the walk has the CPU -- so pumpKeyboardInput()
    // is not going to see this key and it has to be read here instead. Reading
    // it also consumes it, which is what stops a handful of keys pressed during
    // a long scan from landing on the Config screen the moment it finishes.
    const char k = s_keyboard.readKey();
    if (k != KEY_NONE && isModalCloseKey(k)) {
        s_sdScanCancelled = true;
        Serial.println("[sd-scan] cancelled by Back");
        return false;
    }

    char buf[72];
    if (total == 0) {
        // Counting pass: there is no denominator yet, so the bar stays empty and
        // the running count is the thing that shows it is alive.
        snprintf(buf, sizeof(buf), TR("Counting files... %u found"), (unsigned)done);
        lv_bar_set_value(s_sdScanBar, 0, LV_ANIM_OFF);
    } else {
        const uint32_t pct = (uint32_t)done * 100UL / (total ? total : 1);
        snprintf(buf, sizeof(buf), TR("Checking %u of %u   %u%%"),
                 (unsigned)done, (unsigned)total, (unsigned)pct);
        lv_bar_set_value(s_sdScanBar, (int32_t)pct, LV_ANIM_OFF);
    }
    lv_label_set_text(s_sdScanStatus, buf);

    if (found) {
        snprintf(buf, sizeof(buf), TR("%s   -   %u match%s so far"),
                 (where && where[0]) ? where : "/", (unsigned)found,
                 (found == 1) ? "" : "es");
    } else {
        snprintf(buf, sizeof(buf), "%s", (where && where[0]) ? where : "/");
    }
    lv_label_set_text(s_sdScanDetail, buf);

    lv_timer_handler();
    return true;
}

// Yes on the export prompt: writes /camillia/config.yaml back onto the card the
// format just emptied. Offered rather than done automatically, because a config
// export is the user's copy of their own settings and putting one somewhere is
// their decision, not a side effect of cleaning a card.
static void cfgSdExportCommit(int) {
    const bool ok = cfgExport(s_cfg);
    openCfgActionMessageModal(
        ok ? TR("Configuration exported to /camillia/config.yaml.\n\nThe card is "
             "formatted and holds nothing else. Chat history, the node database "
             "and any map tiles were on the old filesystem and are gone.")
           : TR("Export FAILED. The card formatted, but the configuration could not "
             "be written back to it - try Export from the Config screen."),
        TR("SD Card Format"));
}

// Yes on the format confirmation. Everything below this line is the point of no
// return, which is why it sits behind two dialogs rather than one.
static void cfgSdFormatCommit(int) {
    // Up for the duration: f_mkfs on a large card is not instant, and the
    // remount after it is a second bus conversation. There is no progress to
    // report from inside f_mkfs -- it does not call back -- so this is a
    // "working" panel rather than a bar that would have to lie.
    openSdScanProgressModal(TR("Formatting SD Card"));
    if (s_sdScanStatus) {
        lv_label_set_text(s_sdScanStatus, TR("Writing a new filesystem..."));
    }
    if (s_sdScanDetail) lv_label_set_text(s_sdScanDetail, TR("Do not remove the card"));
    lv_timer_handler();

    const bool formatted = storageFormatCard();

    // storageFormatCard() leaves the card unmounted whichever way it went, so
    // both sides of the mount bookkeeping have to be told before anything asks
    // for the card again.
    sdMarkUnmounted();
    const bool remounted = sdBegin(true);
    if (remounted) storageFs().mkdir("/camillia");

    closeSdScanProgressModal();

    if (!formatted) {
        openCfgActionMessageModal(
            remounted
                ? TR("Format FAILED. The card was left as it was and is still "
                  "mounted.\n\nFormatting it on a PC is the reliable way out.")
                : TR("Format FAILED, and the card could not be remounted.\n\nReboot "
                  "the device, or format the card on a PC."),
            TR("SD Card Format"));
        return;
    }
    if (!remounted) {
        openCfgActionMessageModal(
            TR("The card was formatted but could not be remounted. Reboot the "
            "device and it should come up clean."),
            TR("SD Card Format"));
        return;
    }

    openCfgConfirmModal(-1,
                        TR("Card formatted.\n\nWrite your configuration back to it "
                        "as /camillia/config.yaml?"),
                        cfgSdExportCommit, 0);
}

// Format on the scan's dialog. Asks again, and in plainer words: the first
// dialog was about a handful of matched files, and this is about everything on
// the card. Someone who arrived here to remove two files should not be able to
// erase their message history without being told that is what they are doing.
static void cfgSdFormatAsk(int) {
    openCfgConfirmModal(-1,
                        TR("Format the SD card?\n\nThis erases EVERYTHING on it - "
                        "chat and DM history, the node database, map tiles and "
                        "your saved configuration - not just the files the scan "
                        "matched.\n\nIt cannot be undone."),
                        cfgSdFormatCommit, 0);
}

// Yes on the dialog below. Re-walks the card and deletes what it recognises —
// it is handed no list, so nothing a dialog got wrong can widen what goes.
static void cfgSdRepairCommit(int) {
    openSdScanProgressModal(TR("Removing Files"));
    uint16_t failed = 0;
    const uint16_t removed = storageRepairCard(&failed, sdScanProgress,
                                               s_sdScanLastTotal);
    closeSdScanProgressModal();

    char msg[256];
    if (s_sdScanCancelled) {
        // Stopping a delete does not put anything back, so this reports what
        // actually happened rather than treating cancel as "nothing happened".
        snprintf(msg, sizeof(msg),
                 TR("Cancelled after removing %u file%s. The rest were left alone - "
                 "run the scan again to see what is still there."),
                 (unsigned)removed, (removed == 1) ? "" : "s");
    } else if (failed) {
        snprintf(msg, sizeof(msg),
                 TR("Removed %u file%s. %u could not be deleted - the card may be "
                 "write-protected.\n\nRe-run the scan to check."),
                 (unsigned)removed, (removed == 1) ? "" : "s", (unsigned)failed);
    } else if (removed) {
        snprintf(msg, sizeof(msg),
                 TR("Removed %u file%s.\n\nThis card looked clean afterwards, but the "
                 "scan reads names and headers, not contents - it cannot promise "
                 "the card is clean. Reformatting is the only answer that can."),
                 (unsigned)removed, (removed == 1) ? "" : "s");
    } else {
        snprintf(msg, sizeof(msg), "%s",
                 TR("Nothing was removed. The files may have gone already, or the "
                 "card may be write-protected."));
    }
    openCfgActionMessageModal(msg, TR("SD Card Scan"));
}

// Runs the scan and reports. Scanning changes nothing, so it needs no
// confirmation of its own; the confirmation is the one in front of the delete.
static void cfgSdScanRun() {
    // sdBegin() rather than a bare mounted-check: it is what every other SD
    // caller here uses, it is idempotent, and it picks up a card that was put in
    // after boot. storageMounted() is emphatically not the test -- on this board
    // the mount happens in config_io.cpp and that flag is never set, so asking
    // it reported "no card" on a device with a card in it.
    if (!sdBegin()) {
        openCfgActionMessageModal(TR("No SD card is mounted."), TR("SD Card Scan"));
        return;
    }

    // Six rather than three: the dialog's box scrolls now, so the limit is how
    // many paths anyone will actually read before answering, not how many fit.
    // The count above them is the honest total either way.
    StorageCardSuspect found[6];
    uint16_t scanned = 0;
    openSdScanProgressModal(TR("Scanning SD Card"));
    const uint16_t hits =
        storageScanCard(found, (uint8_t)(sizeof(found) / sizeof(found[0])), &scanned,
                        sdScanProgress);
    s_sdScanLastTotal = scanned;
    // Down before either dialog below goes up: they are separate objects, so
    // this one would otherwise sit over whichever replaced it.
    closeSdScanProgressModal();

    // Cancelled: back to the Config screen with nothing in the way. A dialog
    // here would be a result for a question the user just withdrew, and the one
    // thing that must not appear is "nothing found" -- the scan did not finish,
    // so it found nothing only in the sense that it stopped looking.
    if (s_sdScanCancelled) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("SD scan cancelled"));
        return;
    }

    if (hits == 0) {
        char msg[360];
        snprintf(msg, sizeof(msg),
                 TR("Examined %u file%s. Nothing matching Windows malware was "
                 "found.\n\nThis reads names and file headers, not contents, so "
                 "it is a good sign rather than a guarantee."),
                 (unsigned)scanned, (scanned == 1) ? "" : "s");
        openCfgActionMessageModal(msg, TR("SD Card Scan"));
        return;
    }

    const uint8_t listed =
        (uint8_t)((hits < (sizeof(found) / sizeof(found[0]))) ? hits
                                                             : sizeof(found) / sizeof(found[0]));
    int len = snprintf(s_sdScanConfirmText, sizeof(s_sdScanConfirmText),
                       TR("Found %u file%s matching Windows malware:\n"),
                       (unsigned)hits, (hits == 1) ? "" : "s");
    for (uint8_t i = 0; i < listed && len > 0 && (size_t)len < sizeof(s_sdScanConfirmText); i++) {
        len += snprintf(s_sdScanConfirmText + len, sizeof(s_sdScanConfirmText) - (size_t)len,
                        "\n%s", found[i].path);
    }
    if (hits > listed && len > 0 && (size_t)len < sizeof(s_sdScanConfirmText)) {
        len += snprintf(s_sdScanConfirmText + len, sizeof(s_sdScanConfirmText) - (size_t)len,
                        TR("\n+%u more"), (unsigned)(hits - listed));
    }
    if (len > 0 && (size_t)len < sizeof(s_sdScanConfirmText)) {
        snprintf(s_sdScanConfirmText + len, sizeof(s_sdScanConfirmText) - (size_t)len, "%s",
                 TR("\n\nDelete them? Nothing else on the card is touched.\n\n"
                 "Format erases the whole card instead - the only certain fix."));
    }

    openCfgConfirmModal(-1, s_sdScanConfirmText, cfgSdRepairCommit, 0,
                        "(F)ormat", cfgSdFormatAsk);
}
#endif  // HAS_SD_MALWARE_SCAN

#if HAS_CFG_EXTRA_ROWS
// ── Table-driven Config rows: the table ─────────────────────────────────────
// See CfgGenId. One entry per id, in id order (checked below). A row is one of
// three kinds, and fills in the hooks for its kind only:
//
//   toggle  getBool / setBool             Enter flips it
//   choice  opts, or count + nameAt,      opens the slider picker
//           with getIdx / setIdx
//   text    getText / setText             opens the one-line text modal
//
// After any change cfgGenFinish() applies it the way the flags say: by
// rebooting, by re-running the whole live-apply path the web form uses
// (onWebCfgSaved()), or by simply saving. `after` runs first, for the few rows
// with one extra thing to poke.

static int regionPickCount();
static const RegionPlan *regionPickAt(int idx);

enum CfgGenKind : uint8_t { CFG_GEN_TOGGLE, CFG_GEN_CHOICE, CFG_GEN_TEXT };

enum : uint8_t {
    // Applied by rebooting. A toggle asks first; a choice or text row says so
    // in its editor's title instead, since its editor is already a deliberate
    // second step.
    CFG_GEN_F_REBOOT = 1u << 0,
    // Applied live through onWebCfgSaved(): radio retune, hop limit, role,
    // time zone, NTP, debug flags.
    CFG_GEN_F_LIVE   = 1u << 1,
    // Text shown as dots while typed, and only as set/unset on the row.
    CFG_GEN_F_MASKED = 1u << 2,
    // Text whose empty value means the feature is off: the row reads Off
    // rather than (none).
    CFG_GEN_F_EMPTY_OFF = 1u << 3,
};

// One stop of a fixed choice list: the stored value and what it is called.
struct CfgGenOpt {
    uint32_t    value;
    const char *label;   // TR_NOOP
};

struct CfgGenRow {
    int8_t      id;        // its CfgGenId, so the order can be checked
    const char *label;     // TR_NOOP; the row reads "<label>: <value>"
    int8_t      section;   // CfgSectionId on UI_CFG_SECTIONS builds
    uint8_t     kind;
    uint8_t     flags;
    // toggle
    bool (*getBool)();
    void (*setBool)(bool on);
    // choice, from a fixed list ...
    const CfgGenOpt *opts;
    uint8_t     optCount;
    uint32_t (*getVal)();
    void (*setVal)(uint32_t v);
    // ... or computed
    int  (*count)();
    const char *(*nameAt)(int idx);   // translated; may use a static buffer
    int  (*getIdx)();
    void (*setIdx)(int idx);
    // text
    void (*getText)(char *out, size_t cap);
    const char *(*setText)(const char *in);   // nullptr = applied; else TR_NOOP error
    uint8_t     maxLen;
    // common, optional
    bool (*disabled)();
    const char *disabledWhy;   // TR_NOOP
    void (*after)();
    // Shown, and answered Yes to, before the row's editor opens (TR_NOOP).
    // For the settings whose consequences reach past this device.
    const char *warn;
};

static constexpr CfgGenRow cfgGenWarn(CfgGenRow r, const char *warn) {
    r.warn = warn;
    return r;
}

#if UI_CFG_SECTIONS
#define CFG_GEN_SEC(x) ((int8_t)CFG_SEC_##x)
#else
#define CFG_GEN_SEC(x) ((int8_t)0)
#endif

// Shorthands, so each row below reads as what it is rather than as a run of
// nullptrs in the right places.
static constexpr CfgGenRow cfgGenToggle(int8_t id, const char *label, int8_t sec, uint8_t flags,
                                        bool (*g)(), void (*s)(bool),
                                        bool (*disabled)() = nullptr,
                                        const char *why = nullptr,
                                        void (*after)() = nullptr) {
    return CfgGenRow{ id, label, sec, CFG_GEN_TOGGLE, flags, g, s,
                      nullptr, 0, nullptr, nullptr,
                      nullptr, nullptr, nullptr, nullptr,
                      nullptr, nullptr, 0, disabled, why, after, nullptr };
}
static constexpr CfgGenRow cfgGenList(int8_t id, const char *label, int8_t sec, uint8_t flags,
                                      const CfgGenOpt *opts, uint8_t n,
                                      uint32_t (*g)(), void (*s)(uint32_t),
                                      bool (*disabled)() = nullptr,
                                      const char *why = nullptr,
                                      void (*after)() = nullptr) {
    return CfgGenRow{ id, label, sec, CFG_GEN_CHOICE, flags, nullptr, nullptr,
                      opts, n, g, s,
                      nullptr, nullptr, nullptr, nullptr,
                      nullptr, nullptr, 0, disabled, why, after, nullptr };
}
static constexpr CfgGenRow cfgGenPick(int8_t id, const char *label, int8_t sec, uint8_t flags,
                                      int (*count)(), const char *(*nameAt)(int),
                                      int (*g)(), void (*s)(int),
                                      bool (*disabled)() = nullptr,
                                      const char *why = nullptr,
                                      void (*after)() = nullptr) {
    return CfgGenRow{ id, label, sec, CFG_GEN_CHOICE, flags, nullptr, nullptr,
                      nullptr, 0, nullptr, nullptr,
                      count, nameAt, g, s,
                      nullptr, nullptr, 0, disabled, why, after, nullptr };
}
static constexpr CfgGenRow cfgGenText(int8_t id, const char *label, int8_t sec, uint8_t flags,
                                      uint8_t maxLen,
                                      void (*g)(char *, size_t), const char *(*s)(const char *),
                                      bool (*disabled)() = nullptr,
                                      const char *why = nullptr,
                                      void (*after)() = nullptr) {
    return CfgGenRow{ id, label, sec, CFG_GEN_TEXT, flags, nullptr, nullptr,
                      nullptr, 0, nullptr, nullptr,
                      nullptr, nullptr, nullptr, nullptr,
                      g, s, maxLen, disabled, why, after, nullptr };
}

// Copies a string setting into its field, refusing rather than clipping one
// that does not fit: a truncated server name or password is a wrong one.
template <size_t N>
static const char *cfgGenStore(char (&dst)[N], const char *in) {
    if (!in) in = "";
    if (strlen(in) >= N) return TR_NOOP("Too long");
    snprintf(dst, N, "%s", in);
    return nullptr;
}

// Plain http:// or nothing: neither the LOS nor the weather fetcher has a TLS
// client, so an https:// address could never be reached.
template <size_t N>
static const char *cfgGenStoreHttpUrl(char (&dst)[N], const char *in) {
    if (in && in[0] && strncmp(in, "http://", 7) != 0) return TR_NOOP("Must start with http://");
    return cfgGenStore(dst, in);
}

// Whole number in [lo, hi], or nullptr-on-success semantics like the stores.
static bool cfgGenParseLong(const char *in, long lo, long hi, long &out) {
    if (!in || !in[0]) return false;
    char *end = nullptr;
    const long v = strtol(in, &end, 10);
    if (end == in || *end || v < lo || v > hi) return false;
    out = v;
    return true;
}

static bool cfgGenParseDegrees(const char *in, double limit, int32_t &outE7) {
    if (!in || !in[0]) return false;
    char *end = nullptr;
    const double v = strtod(in, &end);
    if (end == in || *end || v < -limit || v > limit) return false;
    outE7 = (int32_t)lround(v * 1e7);
    return true;
}

static void cfgGenMqttChanged() {
    // Drop the session so the next loop reconnects with what was just set.
    if (s_cfg.mqttEnabled) mqttBridgeConfigChanged();
}

static void cfgGenCustomModemChanged() {
    // The four custom values only drive the radio while Custom is in force;
    // otherwise they are what switching to Custom will start from.
    if (!s_cfg.loraUsePreset) onWebCfgSaved();
}

static bool cfgGenLicensed() { return lic::active(); }

// ── Fixed choice lists ──────────────────────────────────────────────────────

static const CfgGenOpt kCfgGenRoles[] = {
    { 0,  TR_NOOP("Client") },
    { 1,  TR_NOOP("Client (mute)") },
    { 5,  TR_NOOP("Tracker") },
    { 8,  TR_NOOP("Client (hidden)") },
#if UI_OFFER_CLIENT_BASE
    { 12, TR_NOOP("Client base") },
#endif
};

static const CfgGenOpt kCfgGenWebIdle[] = {
    { 300,  TR_NOOP("5 min") },
    { 600,  TR_NOOP("10 min") },
    { 900,  TR_NOOP("15 min") },
    { 1800, TR_NOOP("30 min") },
    { 3600, TR_NOOP("1 hour") },
    { 0,    TR_NOOP("Never") },
};

static const CfgGenOpt kCfgGenPosIntervals[] = {
    { 300,   TR_NOOP("5 min") },
    { 600,   TR_NOOP("10 min") },
    { 900,   TR_NOOP("15 min") },
    { 1800,  TR_NOOP("30 min") },
    { 3600,  TR_NOOP("1 hour") },
    { 7200,  TR_NOOP("2 hours") },
    { 21600, TR_NOOP("6 hours") },
    { 43200, TR_NOOP("12 hours") },
    { 86400, TR_NOOP("24 hours") },
};

static const CfgGenOpt kCfgGenNodeInfoIntervals[] = {
    { 900,   TR_NOOP("15 min") },
    { 1800,  TR_NOOP("30 min") },
    { 3600,  TR_NOOP("1 hour") },
    { 10800, TR_NOOP("3 hours") },
    { 21600, TR_NOOP("6 hours") },
    { 43200, TR_NOOP("12 hours") },
    { 86400, TR_NOOP("24 hours") },
};

static const CfgGenOpt kCfgGenGpsPoll[] = {
    { 0,    TR_NOOP("Continuous") },
    { 5,    TR_NOOP("5 sec") },
    { 10,   TR_NOOP("10 sec") },
    { 30,   TR_NOOP("30 sec") },
    { 60,   TR_NOOP("1 min") },
    { 300,  TR_NOOP("5 min") },
    { 900,  TR_NOOP("15 min") },
    { 1800, TR_NOOP("30 min") },
    { 3600, TR_NOOP("1 hour") },
};

// Telemetry is held to an hour or more, as the web form and the load path do.
static const CfgGenOpt kCfgGenTelIntervals[] = {
    { 3600,  TR_NOOP("1 hour") },
    { 7200,  TR_NOOP("2 hours") },
    { 10800, TR_NOOP("3 hours") },
    { 21600, TR_NOOP("6 hours") },
    { 43200, TR_NOOP("12 hours") },
    { 86400, TR_NOOP("24 hours") },
};

// NEIGHBORINFO_MIN_INTERVAL_S (4 h) is the floor.
static const CfgGenOpt kCfgGenNeighborIntervals[] = {
    { 14400, TR_NOOP("4 hours") },
    { 21600, TR_NOOP("6 hours") },
    { 43200, TR_NOOP("12 hours") },
    { 86400, TR_NOOP("24 hours") },
};

static const CfgGenOpt kCfgGenRebroadcast[] = {
    { 0, TR_NOOP("All") },
    { 1, TR_NOOP("All, skip decoding") },
    { 2, TR_NOOP("Local only") },
    { 3, TR_NOOP("Known only") },
    { 4, TR_NOOP("Core apps only") },
};

static const CfgGenOpt kCfgGenOtaAuto[] = {
    { OTA_AUTO_UPDATE_OFF, TR_NOOP("Off") },
    { OTA_AUTO_UPDATE_1H,  TR_NOOP("Every hour") },
    { OTA_AUTO_UPDATE_6H,  TR_NOOP("Every 6 hours") },
    { OTA_AUTO_UPDATE_12H, TR_NOOP("Every 12 hours") },
    { OTA_AUTO_UPDATE_24H, TR_NOOP("Every 24 hours") },
};

static const CfgGenOpt kCfgGenCustomSf[] = {
    { 7, "SF7" }, { 8, "SF8" }, { 9, "SF9" }, { 10, "SF10" }, { 11, "SF11" }, { 12, "SF12" },
};
static_assert(LORA_SF_MIN == 7 && LORA_SF_MAX == 12, "kCfgGenCustomSf lists SF7..SF12");

static const CfgGenOpt kCfgGenCustomCr[] = {
    { 5, "4/5" }, { 6, "4/6" }, { 7, "4/7" }, { 8, "4/8" },
};
static_assert(LORA_CR_MIN == 5 && LORA_CR_MAX == 8, "kCfgGenCustomCr lists 4/5..4/8");

// Auto-favorite distance, in meters, offered in the units the device shows.
static const CfgGenOpt kCfgGenAutoFavMetric[] = {
    { 250,   "250 m" }, { 500, "500 m" }, { 1000, "1 km" },
    { 2000,  "2 km" },  { 5000, "5 km" }, { 10000, "10 km" },
};
static const CfgGenOpt kCfgGenAutoFavImperial[] = {
    { 402,   "1/4 mi" }, { 805, "1/2 mi" }, { 1609, "1 mi" },
    { 3219,  "2 mi" },   { 8047, "5 mi" },  { 16093, "10 mi" },
};

#define CFG_GEN_N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

// ── Computed choice lists ───────────────────────────────────────────────────

static int cfgGenRegionCount() { return regionPickCount(); }
static const char *cfgGenRegionName(int idx) { return regionPickAt(idx)->code; }
static int cfgGenRegionIdx() {
    const int n = regionPickCount();
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(regionPickAt(i)->code, s_cfg.region)) return i;
    }
    return 0;
}
static void cfgGenRegionSet(int idx) {
    utf8util::copyTruncate(s_cfg.region, sizeof(s_cfg.region), regionPickAt(idx)->code);
    applyPresetParams(s_cfg);   // frequency and slot follow the region
}

static int cfgGenPowerCount() { return MESH_POWER; }   // 1..MESH_POWER dBm
static const char *cfgGenPowerName(int idx) {
    static char buf[16];
    snprintf(buf, sizeof(buf), TR("%d dBm"), idx + 1);
    return buf;
}
static int cfgGenPowerIdx() { return constrain((int)s_cfg.loraPower, 1, (int)MESH_POWER) - 1; }
static void cfgGenPowerSet(int idx) { s_cfg.loraPower = (uint8_t)(idx + 1); }

static int cfgGenHopCount() { return 7; }   // 1..7
static const char *cfgGenHopName(int idx) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d", idx + 1);
    return buf;
}
static int cfgGenHopIdx() { return constrain((int)s_cfg.loraHopLimit, 1, 7) - 1; }
static void cfgGenHopSet(int idx) { s_cfg.loraHopLimit = (uint8_t)(idx + 1); }

// Only the bandwidths this board's radio can be tuned to.
static int cfgGenBwCount() { return kBwCodeCount; }
static const char *cfgGenBwName(int idx) {
    static char buf[16];
    const float khz = loraBwFromCode(kBwCodes[idx]);
    if (khz == (float)(int)khz) snprintf(buf, sizeof(buf), TR("%d kHz"), (int)khz);
    else                        snprintf(buf, sizeof(buf), TR("%.2f kHz"), (double)khz);
    return buf;
}
static int cfgGenBwIdx() {
    const uint16_t cur = loraCoerceBwCode(s_cfg.loraCustomBwKhz);
    for (int i = 0; i < kBwCodeCount; i++) if (kBwCodes[i] == cur) return i;
    return 0;
}
static void cfgGenBwSet(int idx) { s_cfg.loraCustomBwKhz = kBwCodes[idx]; }

static int cfgGenAutoFavCount() {
    return s_cfg.displayUnits ? CFG_GEN_N(kCfgGenAutoFavImperial) : CFG_GEN_N(kCfgGenAutoFavMetric);
}
static const CfgGenOpt *cfgGenAutoFavOpts() {
    return s_cfg.displayUnits ? kCfgGenAutoFavImperial : kCfgGenAutoFavMetric;
}
static const char *cfgGenAutoFavName(int idx) { return cfgGenAutoFavOpts()[idx].label; }
static int cfgGenAutoFavIdx() {
    const CfgGenOpt *o = cfgGenAutoFavOpts();
    const int n = cfgGenAutoFavCount();
    int best = 0;
    for (int i = 1; i < n; i++) {
        if (labs((long)o[i].value - (long)s_cfg.autoFavoriteRangeM)
            < labs((long)o[best].value - (long)s_cfg.autoFavoriteRangeM)) best = i;
    }
    return best;
}
static void cfgGenAutoFavSet(int idx) { s_cfg.autoFavoriteRangeM = cfgGenAutoFavOpts()[idx].value; }

// ── The table ───────────────────────────────────────────────────────────────

static constexpr CfgGenRow kCfgGenRows[CFG_GEN__COUNT] = {
    // ── Radio & Identity ─────────────────────────────────────────────────────
    cfgGenPick(CFG_GEN_REGION, TR_NOOP("Region"), CFG_GEN_SEC(RADIO), CFG_GEN_F_REBOOT,
               cfgGenRegionCount, cfgGenRegionName, cfgGenRegionIdx, cfgGenRegionSet),
    // Licensed mode owns the power level while it is on (its own popup).
    cfgGenPick(CFG_GEN_TX_POWER, TR_NOOP("TX Power"), CFG_GEN_SEC(RADIO), CFG_GEN_F_LIVE,
               cfgGenPowerCount, cfgGenPowerName, cfgGenPowerIdx, cfgGenPowerSet,
               cfgGenLicensed, TR_NOOP("Set in Licensed mode")),
    cfgGenPick(CFG_GEN_HOP_LIMIT, TR_NOOP("Hop Limit"), CFG_GEN_SEC(RADIO), CFG_GEN_F_LIVE,
               cfgGenHopCount, cfgGenHopName, cfgGenHopIdx, cfgGenHopSet),
    cfgGenList(CFG_GEN_ROLE, TR_NOOP("Device Role"), CFG_GEN_SEC(RADIO), CFG_GEN_F_LIVE,
               kCfgGenRoles, CFG_GEN_N(kCfgGenRoles),
               []() -> uint32_t { return s_cfg.deviceRole; },
               [](uint32_t v) { s_cfg.deviceRole = cfgCoerceDeviceRole((uint8_t)v); }),
    // Leaving or entering Custom changes what the radio and the primary
    // channel's slot are, which the preset row reboots for too.
    cfgGenToggle(CFG_GEN_CUSTOM_MODEM, TR_NOOP("Custom Modem"), CFG_GEN_SEC(RADIO), CFG_GEN_F_REBOOT,
                 []() { return !s_cfg.loraUsePreset; },
                 [](bool on) { s_cfg.loraUsePreset = !on; applyPresetParams(s_cfg); }),
    cfgGenPick(CFG_GEN_CUSTOM_BW, TR_NOOP("Custom Bandwidth"), CFG_GEN_SEC(RADIO), 0,
               cfgGenBwCount, cfgGenBwName, cfgGenBwIdx, cfgGenBwSet,
               nullptr, nullptr, cfgGenCustomModemChanged),
    cfgGenList(CFG_GEN_CUSTOM_SF, TR_NOOP("Custom Spreading Factor"), CFG_GEN_SEC(RADIO), 0,
               kCfgGenCustomSf, CFG_GEN_N(kCfgGenCustomSf),
               []() -> uint32_t { return s_cfg.loraCustomSf; },
               [](uint32_t v) { s_cfg.loraCustomSf = (uint8_t)v; },
               nullptr, nullptr, cfgGenCustomModemChanged),
    cfgGenList(CFG_GEN_CUSTOM_CR, TR_NOOP("Custom Coding Rate"), CFG_GEN_SEC(RADIO), 0,
               kCfgGenCustomCr, CFG_GEN_N(kCfgGenCustomCr),
               []() -> uint32_t { return s_cfg.loraCustomCr; },
               [](uint32_t v) { s_cfg.loraCustomCr = (uint8_t)v; },
               nullptr, nullptr, cfgGenCustomModemChanged),
    cfgGenText(CFG_GEN_CUSTOM_SLOT, TR_NOOP("Custom Frequency Slot"), CFG_GEN_SEC(RADIO), 0, 3,
               [](char *o, size_t n) { snprintf(o, n, "%u", (unsigned)s_cfg.loraCustomSlot); },
               [](const char *in) -> const char * {
                   long v = 0;
                   if (!cfgGenParseLong(in, 0, 255, v)) return TR_NOOP("Enter 0 (auto) to 255");
                   s_cfg.loraCustomSlot = (uint8_t)v;
                   return nullptr;
               },
               nullptr, nullptr, cfgGenCustomModemChanged),
    // Only read when the radio is brought up.
    cfgGenToggle(CFG_GEN_RX_BOOST, TR_NOOP("RX Boosted Gain"), CFG_GEN_SEC(RADIO), CFG_GEN_F_REBOOT,
                 []() { return s_cfg.loraRxBoostedGain; },
                 [](bool on) { s_cfg.loraRxBoostedGain = on; }),
    // The node id is this node's identity: every peer sees a new node.
    cfgGenToggle(CFG_GEN_NODEID_PUBKEY, TR_NOOP("Node ID from Key"), CFG_GEN_SEC(RADIO), CFG_GEN_F_REBOOT,
                 []() { return s_cfg.nodeIdFromPubKey; },
                 [](bool on) { s_cfg.nodeIdFromPubKey = on; }),
    // A fixed node id, instead of the one derived from the MAC or the key.
    // Blank turns it off. Asks first: this is the node's identity on the mesh.
    cfgGenWarn(
        cfgGenText(CFG_GEN_NODEID_OVERRIDE, TR_NOOP("Node ID Override"), CFG_GEN_SEC(RADIO),
                   CFG_GEN_F_REBOOT | CFG_GEN_F_EMPTY_OFF, 11,
                   [](char *o, size_t n) {
                       if (s_cfg.nodeIdOverride) snprintf(o, n, "!%08lx", (unsigned long)s_cfg.nodeIdOverride);
                       else if (n) o[0] = '\0';
                   },
                   [](const char *in) -> const char * {
                       const uint32_t id = parseNodeIdText(in);
                       // Blank, "none" or 0 is off; anything else must name a node.
                       const bool blank = !in || !in[0] || !strcasecmp(in, "none") || !strcmp(in, "0");
                       if (!blank && (id == 0 || id == 0xFFFFFFFFUL))
                           return TR_NOOP("Enter a node ID like !1a2b3c4d, or blank for off");
                       s_cfg.nodeIdOverride = blank ? 0 : id;
                       return nullptr;
                   }),
        TR_NOOP("Warning: this replaces this node's ID. Every other node will see a new "
                "node, and existing DMs and favorites stay with the old ID. Two nodes with "
                "the same ID disrupt each other on the mesh. Blank turns it off. Continue?")),

    // ── Connections ──────────────────────────────────────────────────────────
    cfgGenToggle(CFG_GEN_WEB_AUTH, TR_NOOP("Web Config Login"), CFG_GEN_SEC(CONNECT), 0,
                 []() { return s_cfg.webCfgAuthEnabled; },
                 [](bool on) { s_cfg.webCfgAuthEnabled = on; }),
    // Blank means the default password, as it does in web config.
    cfgGenText(CFG_GEN_WEB_PASS, TR_NOOP("Web Config Password"), CFG_GEN_SEC(CONNECT), CFG_GEN_F_MASKED,
               (uint8_t)(sizeof(s_cfg.webCfgPass) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.webCfgPass); },
               [](const char *in) { return cfgGenStore(s_cfg.webCfgPass, in); }),
    cfgGenList(CFG_GEN_WEB_IDLE, TR_NOOP("Web Config Idle Off"), CFG_GEN_SEC(CONNECT), 0,
               kCfgGenWebIdle, CFG_GEN_N(kCfgGenWebIdle),
               []() -> uint32_t { return cfgCoerceWebCfgIdle((long)s_cfg.webCfgIdleTimeoutS); },
               [](uint32_t v) { s_cfg.webCfgIdleTimeoutS = cfgCoerceWebCfgIdle((long)v); },
               nullptr, nullptr,
               []() { webCfgSetIdleTimeoutS(s_cfg.webCfgIdleTimeoutS); }),
    cfgGenText(CFG_GEN_MQTT_SERVER, TR_NOOP("MQTT Server"), CFG_GEN_SEC(CONNECT), 0,
               (uint8_t)(sizeof(s_cfg.mqttServer) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.mqttServer); },
               [](const char *in) { return cfgGenStore(s_cfg.mqttServer, in); },
               nullptr, nullptr, cfgGenMqttChanged),
    cfgGenText(CFG_GEN_MQTT_PORT, TR_NOOP("MQTT Port"), CFG_GEN_SEC(CONNECT), 0, 5,
               [](char *o, size_t n) { snprintf(o, n, "%u", (unsigned)s_cfg.mqttPort); },
               [](const char *in) -> const char * {
                   long v = 0;
                   if (!cfgGenParseLong(in, 1, 65535, v)) return TR_NOOP("Enter a port, 1 to 65535");
                   s_cfg.mqttPort = (uint16_t)v;
                   return nullptr;
               },
               nullptr, nullptr, cfgGenMqttChanged),
    cfgGenText(CFG_GEN_MQTT_USER, TR_NOOP("MQTT Username"), CFG_GEN_SEC(CONNECT), 0,
               (uint8_t)(sizeof(s_cfg.mqttUser) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.mqttUser); },
               [](const char *in) { return cfgGenStore(s_cfg.mqttUser, in); },
               nullptr, nullptr, cfgGenMqttChanged),
    cfgGenText(CFG_GEN_MQTT_PASS, TR_NOOP("MQTT Password"), CFG_GEN_SEC(CONNECT), CFG_GEN_F_MASKED,
               (uint8_t)(sizeof(s_cfg.mqttPass) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.mqttPass); },
               [](const char *in) { return cfgGenStore(s_cfg.mqttPass, in); },
               nullptr, nullptr, cfgGenMqttChanged),
    // Required: the bridge builds every topic from it.
    cfgGenText(CFG_GEN_MQTT_ROOT, TR_NOOP("MQTT Root Topic"), CFG_GEN_SEC(CONNECT), 0,
               (uint8_t)(sizeof(s_cfg.mqttRoot) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.mqttRoot); },
               [](const char *in) -> const char * {
                   if (!in || !in[0]) return TR_NOOP("A root topic is required");
                   return cfgGenStore(s_cfg.mqttRoot, in);
               },
               nullptr, nullptr, cfgGenMqttChanged),
    cfgGenToggle(CFG_GEN_MQTT_ENCRYPT, TR_NOOP("MQTT Encryption"), CFG_GEN_SEC(CONNECT), 0,
                 []() { return s_cfg.mqttEncryption; },
                 [](bool on) { s_cfg.mqttEncryption = on; },
                 nullptr, nullptr, cfgGenMqttChanged),
    cfgGenToggle(CFG_GEN_MQTT_MAP_REPORT, TR_NOOP("MQTT Map Report"), CFG_GEN_SEC(CONNECT), 0,
                 []() { return s_cfg.mqttMapReport; },
                 [](bool on) { s_cfg.mqttMapReport = on; }),
    cfgGenToggle(CFG_GEN_OK_TO_MQTT, TR_NOOP("OK to MQTT"), CFG_GEN_SEC(CONNECT), 0,
                 []() { return s_cfg.okToMqtt; },
                 [](bool on) { s_cfg.okToMqtt = on; }),
    cfgGenToggle(CFG_GEN_IGNORE_MQTT, TR_NOOP("Ignore MQTT Packets"), CFG_GEN_SEC(CONNECT), 0,
                 []() { return s_cfg.ignoreMqtt; },
                 [](bool on) { s_cfg.ignoreMqtt = on; }),

    // ── Location ─────────────────────────────────────────────────────────────
    cfgGenList(CFG_GEN_POS_INTERVAL, TR_NOOP("Position Broadcast"), CFG_GEN_SEC(LOCATION), 0,
               kCfgGenPosIntervals, CFG_GEN_N(kCfgGenPosIntervals),
               []() -> uint32_t { return s_cfg.posIntervalS; },
               [](uint32_t v) { s_cfg.posIntervalS = v; }),
    cfgGenList(CFG_GEN_GPS_POLL, TR_NOOP("GPS Poll Interval"), CFG_GEN_SEC(LOCATION), 0,
               kCfgGenGpsPoll, CFG_GEN_N(kCfgGenGpsPoll),
               []() -> uint32_t { return s_cfg.gpsPollIntervalS; },
               [](uint32_t v) { s_cfg.gpsPollIntervalS = v; }),
    // Parks the receiver between polls; see gpsSetDutyCycle().
    cfgGenToggle(CFG_GEN_GPS_DUTY, TR_NOOP("GPS Standby Between Polls"), CFG_GEN_SEC(LOCATION), 0,
                 []() { return s_cfg.gpsDutyCycleEnabled; },
                 [](bool on) { s_cfg.gpsDutyCycleEnabled = on; }),
    cfgGenText(CFG_GEN_FIXED_LAT, TR_NOOP("Fixed Latitude"), CFG_GEN_SEC(LOCATION), 0, 12,
               [](char *o, size_t n) { snprintf(o, n, "%.7f", s_cfg.latI * 1e-7); },
               [](const char *in) -> const char * {
                   int32_t v = 0;
                   if (!cfgGenParseDegrees(in, 90.0, v)) return TR_NOOP("Enter -90 to 90");
                   s_cfg.latI = v;
                   return nullptr;
               }),
    cfgGenText(CFG_GEN_FIXED_LON, TR_NOOP("Fixed Longitude"), CFG_GEN_SEC(LOCATION), 0, 13,
               [](char *o, size_t n) { snprintf(o, n, "%.7f", s_cfg.lonI * 1e-7); },
               [](const char *in) -> const char * {
                   int32_t v = 0;
                   if (!cfgGenParseDegrees(in, 180.0, v)) return TR_NOOP("Enter -180 to 180");
                   s_cfg.lonI = v;
                   return nullptr;
               }),
    cfgGenText(CFG_GEN_FIXED_ALT, TR_NOOP("Fixed Altitude (m)"), CFG_GEN_SEC(LOCATION), 0, 6,
               [](char *o, size_t n) { snprintf(o, n, "%d", (int)s_cfg.alt); },
               [](const char *in) -> const char * {
                   long v = 0;
                   if (!cfgGenParseLong(in, -500, 20000, v)) return TR_NOOP("Enter -500 to 20000");
                   s_cfg.alt = (int32_t)v;
                   return nullptr;
               }),

    // ── Mesh Services ────────────────────────────────────────────────────────
    // Licensed mode pins this to Local only.
    cfgGenList(CFG_GEN_REBROADCAST, TR_NOOP("Rebroadcast"), CFG_GEN_SEC(MESH), 0,
               kCfgGenRebroadcast, CFG_GEN_N(kCfgGenRebroadcast),
               []() -> uint32_t { return s_cfg.rebroadcastMode; },
               [](uint32_t v) { s_cfg.rebroadcastMode = (uint8_t)v; },
               cfgGenLicensed, TR_NOOP("Fixed while licensed")),
    // And this to the station-ID interval.
    cfgGenList(CFG_GEN_NODEINFO_INTERVAL, TR_NOOP("NodeInfo Broadcast"), CFG_GEN_SEC(MESH), 0,
               kCfgGenNodeInfoIntervals, CFG_GEN_N(kCfgGenNodeInfoIntervals),
               []() -> uint32_t { return s_cfg.nodeInfoIntervalS; },
               [](uint32_t v) { s_cfg.nodeInfoIntervalS = v; },
               cfgGenLicensed, TR_NOOP("Fixed while licensed")),
    cfgGenToggle(CFG_GEN_TEL_DEVICE, TR_NOOP("Device Telemetry"), CFG_GEN_SEC(MESH), 0,
                 []() { return s_cfg.telDeviceEnabled; },
                 [](bool on) { s_cfg.telDeviceEnabled = on; }),
    cfgGenToggle(CFG_GEN_TEL_ENV, TR_NOOP("Environment Telemetry"), CFG_GEN_SEC(MESH), 0,
                 []() { return s_cfg.telEnvEnabled; },
                 [](bool on) { s_cfg.telEnvEnabled = HAS_ENV_SENSOR_TELEMETRY && on; }),
    // One interval for both, as in web config.
    cfgGenList(CFG_GEN_TEL_INTERVAL, TR_NOOP("Telemetry Interval"), CFG_GEN_SEC(MESH), 0,
               kCfgGenTelIntervals, CFG_GEN_N(kCfgGenTelIntervals),
               []() -> uint32_t { return s_cfg.telDeviceIntervalS; },
               [](uint32_t v) { s_cfg.telDeviceIntervalS = v; s_cfg.telEnvIntervalS = v; }),
    cfgGenList(CFG_GEN_NEIGHBOR_INTERVAL, TR_NOOP("Neighborhood Info Interval"), CFG_GEN_SEC(MESH), 0,
               kCfgGenNeighborIntervals, CFG_GEN_N(kCfgGenNeighborIntervals),
               []() -> uint32_t { return s_cfg.neighborInfoIntervalS; },
               [](uint32_t v) { s_cfg.neighborInfoIntervalS = v; }),
    cfgGenToggle(CFG_GEN_NEIGHBOR_LORA, TR_NOOP("Neighborhood Info over LoRa"), CFG_GEN_SEC(MESH), 0,
                 []() { return s_cfg.neighborInfoOverLora; },
                 [](bool on) { s_cfg.neighborInfoOverLora = on; }),
    cfgGenToggle(CFG_GEN_AUTOFAV, TR_NOOP("Auto-Favorite Nearby"), CFG_GEN_SEC(MESH), 0,
                 []() { return s_cfg.autoFavoriteEnabled; },
                 [](bool on) { s_cfg.autoFavoriteEnabled = on; }),
    cfgGenPick(CFG_GEN_AUTOFAV_RANGE, TR_NOOP("Auto-Favorite Range"), CFG_GEN_SEC(MESH), 0,
               cfgGenAutoFavCount, cfgGenAutoFavName, cfgGenAutoFavIdx, cfgGenAutoFavSet),
    cfgGenText(CFG_GEN_LOS_SERVER, TR_NOOP("LOS Elevation Server"), CFG_GEN_SEC(MESH), 0,
               (uint8_t)(sizeof(s_cfg.losElevServer) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.losElevServer); },
               [](const char *in) { return cfgGenStoreHttpUrl(s_cfg.losElevServer, in); }),
    cfgGenText(CFG_GEN_WEATHER_SERVER, TR_NOOP("Weather Server"), CFG_GEN_SEC(MESH), 0,
               (uint8_t)(sizeof(s_cfg.weatherServer) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.weatherServer); },
               [](const char *in) { return cfgGenStoreHttpUrl(s_cfg.weatherServer, in); }),

    // ── Device & Firmware ────────────────────────────────────────────────────
    // A POSIX TZ string, e.g. EST5EDT,M3.2.0,M11.1.0. Blank is UTC.
    cfgGenText(CFG_GEN_TIMEZONE, TR_NOOP("Time Zone"), CFG_GEN_SEC(DEVICE), CFG_GEN_F_LIVE,
               (uint8_t)(sizeof(s_cfg.tzDef) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.tzDef); },
               [](const char *in) { return cfgGenStore(s_cfg.tzDef, in); }),
    // Blank restores the default server.
    cfgGenText(CFG_GEN_NTP_SERVER, TR_NOOP("NTP Server"), CFG_GEN_SEC(DEVICE), CFG_GEN_F_LIVE,
               (uint8_t)(sizeof(s_cfg.ntpServer) - 1),
               [](char *o, size_t n) { snprintf(o, n, "%s", s_cfg.ntpServer); },
               [](const char *in) {
                   return cfgGenStore(s_cfg.ntpServer, (in && in[0]) ? in : MY_NTP_SERVER);
               }),
    cfgGenToggle(CFG_GEN_POWER_SAVING, TR_NOOP("Power Saving"), CFG_GEN_SEC(DEVICE), 0,
                 []() { return s_cfg.isPowerSaving; },
                 [](bool on) { s_cfg.isPowerSaving = on; }),
    cfgGenToggle(CFG_GEN_OTA_BOOT_CHECK, TR_NOOP("Update Check at Boot"), CFG_GEN_SEC(DEVICE), 0,
                 []() { return s_cfg.otaAutoCheckEnabled; },
                 [](bool on) { s_cfg.otaAutoCheckEnabled = on; }),
    cfgGenList(CFG_GEN_OTA_AUTO_UPDATE, TR_NOOP("Auto-Install Updates"), CFG_GEN_SEC(DEVICE), 0,
               kCfgGenOtaAuto, CFG_GEN_N(kCfgGenOtaAuto),
               []() -> uint32_t { return cfgCoerceOtaAutoUpdate(s_cfg.otaAutoUpdatePeriod); },
               [](uint32_t v) { s_cfg.otaAutoUpdatePeriod = cfgCoerceOtaAutoUpdate((uint8_t)v); }),
    // The three serial categories move together, as onWebCfgSaved() has them.
    cfgGenToggle(CFG_GEN_DEBUG_LOG, TR_NOOP("Serial Debug Logging"), CFG_GEN_SEC(DEVICE), CFG_GEN_F_LIVE,
                 []() { return s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps; },
                 [](bool on) { s_cfg.debugAcks = s_cfg.debugMessages = s_cfg.debugGps = on; }),
};

static constexpr bool cfgGenTableInOrder() {
    for (int i = 0; i < CFG_GEN__COUNT; i++) {
        if (kCfgGenRows[i].id != i) return false;
    }
    return true;
}

static_assert(cfgGenTableInOrder(), "kCfgGenRows must list every CfgGenId, in order");

static const CfgGenRow *cfgGenRow(int genId) {
    if (genId < 0 || genId >= CFG_GEN__COUNT) return nullptr;
    return &kCfgGenRows[genId];
}

// The choice row's current stop. A fixed list matches the stored value
// exactly, or failing that the nearest stop -- a value imported from YAML or
// set in web config need not be one of them, and the picker should open on
// the closest rather than at one end.
static int cfgGenCurrentIdx(const CfgGenRow &r) {
    if (!r.opts) return r.getIdx ? r.getIdx() : 0;
    const uint32_t cur = r.getVal ? r.getVal() : 0;
    int best = 0;
    for (int i = 0; i < r.optCount; i++) {
        if (r.opts[i].value == cur) return i;
        const long d = labs((long)r.opts[i].value - (long)cur);
        if (d < labs((long)r.opts[best].value - (long)cur)) best = i;
    }
    return best;
}

static int cfgGenChoiceCount(const CfgGenRow &r) {
    return r.opts ? r.optCount : (r.count ? r.count() : 0);
}

static const char *cfgGenChoiceName(const CfgGenRow &r, int idx) {
    if (r.opts) return TR(r.opts[idx].label);
    return r.nameAt ? r.nameAt(idx) : "";
}

// The value part of the row label, into buf.
static void cfgGenValueText(const CfgGenRow &r, char *buf, size_t bufLen) {
    switch (r.kind) {
        case CFG_GEN_TOGGLE:
            snprintf(buf, bufLen, "%s", (r.getBool && r.getBool()) ? TR("On") : TR("Off"));
            break;
        case CFG_GEN_CHOICE: {
            const int n = cfgGenChoiceCount(r);
            // A value that matches no stop exactly reads as itself, not as the
            // nearest stop: the row must say what is stored.
            if (r.opts && r.getVal) {
                const uint32_t cur = r.getVal();
                bool exact = false;
                for (int i = 0; i < r.optCount; i++) exact = exact || (r.opts[i].value == cur);
                if (!exact) {
                    snprintf(buf, bufLen, TR("Custom (%lu)"), (unsigned long)cur);
                    break;
                }
            }
            snprintf(buf, bufLen, "%s", n > 0 ? cfgGenChoiceName(r, cfgGenCurrentIdx(r)) : "");
        } break;
        case CFG_GEN_TEXT: {
            char cur[100] = "";
            if (r.getText) r.getText(cur, sizeof(cur));
            if (r.flags & CFG_GEN_F_MASKED) {
                snprintf(buf, bufLen, "%s", cur[0] ? TR("Set") : TR("(none)"));
            } else if (!cur[0] && (r.flags & CFG_GEN_F_EMPTY_OFF)) {
                snprintf(buf, bufLen, "%s", TR("Off"));
            } else {
                snprintf(buf, bufLen, "%s", cur[0] ? cur : TR("(none)"));
            }
        } break;
    }
}

static const char *cfgGenLabel(int genId, char *buf, size_t bufLen) {
    const CfgGenRow *r = cfgGenRow(genId);
    if (!r) { snprintf(buf, bufLen, "%s", TR("(unknown)")); return buf; }
    char val[64];
    cfgGenValueText(*r, val, sizeof(val));
    snprintf(buf, bufLen, TR("%s: %s"), TR(r->label), val);
    return buf;
}

static int cfgGenSection(int genId) {
    const CfgGenRow *r = cfgGenRow(genId);
    return r ? r->section : 0;
}

static bool cfgGenDisabled(int genId, const char **why) {
    const CfgGenRow *r = cfgGenRow(genId);
    const bool off = r && r->disabled && r->disabled();
    if (why) *why = off ? r->disabledWhy : nullptr;
    return off;
}

// A row with a warning asks before its editor opens. Otherwise only toggles
// ask: their Enter is the whole change, and a reboot should never be the
// surprise at the end of one keypress.
static bool cfgGenNeedsConfirm(int genId) {
    const CfgGenRow *r = cfgGenRow(genId);
    if (!r) return false;
    if (r->warn) return true;
    return r->kind == CFG_GEN_TOGGLE && (r->flags & CFG_GEN_F_REBOOT);
}

// Phrased from the destination, for the reason cfgActionConfirmSentence()
// gives: a toggle's label names the state being left.
static void cfgGenConfirmSentence(int genId, char *buf, size_t bufLen) {
    const CfgGenRow *r = cfgGenRow(genId);
    if (!r) { buf[0] = '\0'; return; }
    if (r->warn) { snprintf(buf, bufLen, "%s", TR(r->warn)); return; }
    const bool on = r->getBool && r->getBool();
    snprintf(buf, bufLen, TR("Turn %s %s and reboot?"), TR(r->label), on ? TR("off") : TR("on"));
}

// Applies a change already written into s_cfg.
static void cfgGenFinish(int genId) {
    const CfgGenRow *r = cfgGenRow(genId);
    if (!r) return;
    if (r->after) r->after();
    char label[96];
    cfgGenLabel(genId, label, sizeof(label));
    if (r->flags & CFG_GEN_F_REBOOT) {
        persistConfigToPrefs();
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("%s - rebooting..."), label);
        refreshCfgModal();
        // Paint the status before going, as the preset row does: a device that
        // reboots with no explanation reads as a crash.
        lv_timer_handler();
        delay(1000);
        flushPersistentState();
        ESP.restart();
        return;
    }
    if (r->flags & CFG_GEN_F_LIVE) onWebCfgSaved();   // saves, then applies
    else                           persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", label);
    refreshCfgModal();
}

// ── Editors ─────────────────────────────────────────────────────────────────

// The row whose slider picker or text modal is open. Both editors take plain
// function pointers with no context, so the trampolines below read it.
static int s_cfgGenOpen = -1;
static CfgSliderPicker s_cfgGenSlider = {};
static char s_cfgGenTitle[64];

static const char *cfgGenSliderLabel(int idx) {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    return r ? cfgGenChoiceName(*r, idx) : "";
}

static void cfgGenSliderApply(int idx) {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    if (!r) return;
    // Saved where it already was: nothing to apply, and above all nothing to
    // reboot for. An off-list value (see cfgGenValueText) is not "where it
    // already was" -- saving it onto the nearest stop is a real change.
    char before[64];
    cfgGenValueText(*r, before, sizeof(before));
    if (idx == cfgGenCurrentIdx(*r) && !strcmp(before, cfgGenChoiceName(*r, idx))) {
        cfgGenLabel(s_cfgGenOpen, s_cfgStatus, sizeof(s_cfgStatus));
        return;
    }
    if (r->opts) {
        if (r->setVal && idx >= 0 && idx < r->optCount) r->setVal(r->opts[idx].value);
    } else if (r->setIdx) {
        r->setIdx(idx);
    }
    cfgGenFinish(s_cfgGenOpen);
}

// The editor's title: the row name, plus a warning when saving reboots.
static const char *cfgGenEditorTitle(const CfgGenRow &r) {
    if (r.flags & CFG_GEN_F_REBOOT) {
        snprintf(s_cfgGenTitle, sizeof(s_cfgGenTitle), TR("%s (reboots)"), TR(r.label));
    } else {
        snprintf(s_cfgGenTitle, sizeof(s_cfgGenTitle), "%s", TR(r.label));
    }
    return s_cfgGenTitle;
}

static const char *cfgGenTextTitle() {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    return r ? cfgGenEditorTitle(*r) : "";
}

static uint32_t cfgGenTextMaxLen() {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    return r ? r->maxLen : 0;
}

static bool cfgGenTextMasked() {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    return r && (r->flags & CFG_GEN_F_MASKED);
}

static void cfgGenTextInitial(char *out, size_t cap) {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    if (cap) out[0] = '\0';
    if (r && r->getText) r->getText(out, cap);
}

// Set by the commit when the text came back as it went in, so Done does not
// save, apply or -- above all -- reboot for nothing.
static bool s_cfgGenTextUnchanged = false;

static const char *cfgGenTextCommit(const char *text) {
    const CfgGenRow *r = cfgGenRow(s_cfgGenOpen);
    s_cfgGenTextUnchanged = false;
    if (!r || !r->setText) return nullptr;
    char before[100] = "", after[100] = "";
    if (r->getText) r->getText(before, sizeof(before));
    const char *err = r->setText(text);
    if (err) return err;
    if (r->getText) r->getText(after, sizeof(after));
    s_cfgGenTextUnchanged = !strcmp(before, after);
    return nullptr;
}

static void cfgGenTextDone() {
    const int id = s_cfgGenOpen;
    s_cfgGenOpen = -1;
    if (s_cfgGenTextUnchanged) {
        cfgGenLabel(id, s_cfgStatus, sizeof(s_cfgStatus));
        refreshCfgModal();
        return;
    }
    cfgGenFinish(id);
}

static void cfgGenPerform(int genId) {
    const CfgGenRow *r = cfgGenRow(genId);
    if (!r) return;
    switch (r->kind) {
        case CFG_GEN_TOGGLE:
            if (r->setBool && r->getBool) r->setBool(!r->getBool());
            cfgGenFinish(genId);
            break;
        case CFG_GEN_CHOICE: {
            const int n = cfgGenChoiceCount(*r);
            if (n <= 0) return;
            s_cfgGenOpen = genId;
            // The slider reads its spec for as long as it is open, so it lives
            // here rather than on the stack. TR() is applied by the picker, and
            // the title is already translated, which TR() passes through.
            s_cfgGenSlider = {};
            s_cfgGenSlider.title    = cfgGenEditorTitle(*r);
            s_cfgGenSlider.count    = n;
            s_cfgGenSlider.labelFor = cfgGenSliderLabel;
            s_cfgGenSlider.apply    = cfgGenSliderApply;
            s_cfgGenSlider.leftEnd  = nullptr;
            s_cfgGenSlider.rightEnd = nullptr;
            openCfgSliderModal(&s_cfgGenSlider, cfgGenCurrentIdx(*r));
        } break;
        case CFG_GEN_TEXT:
            s_cfgGenOpen = genId;
            openChanTextModal(kChanTextCfgGen);
            break;
    }
}

#else  // !HAS_CFG_EXTRA_ROWS
// No table on this build: initCfgActions() lists none of these rows, so nothing
// below is ever reached. Stubs keep the shared dispatch compiling.
static const char *cfgGenLabel(int, char *buf, size_t bufLen) {
    if (bufLen) buf[0] = '\0';
    return buf;
}
static int  cfgGenSection(int) { return 0; }
static bool cfgGenDisabled(int, const char **why) { if (why) *why = nullptr; return false; }
static bool cfgGenNeedsConfirm(int) { return false; }
static void cfgGenConfirmSentence(int, char *buf, size_t bufLen) { if (bufLen) buf[0] = '\0'; }
static void cfgGenPerform(int) {}
static const char *cfgGenTextTitle() { return ""; }
static uint32_t    cfgGenTextMaxLen() { return 0; }
static bool        cfgGenTextMasked() { return false; }
static void        cfgGenTextInitial(char *out, size_t cap) { if (cap) out[0] = '\0'; }
static const char *cfgGenTextCommit(const char *) { return nullptr; }
static void        cfgGenTextDone() {}
#endif  // HAS_CFG_EXTRA_ROWS

// ── Config backups: export / import pickers ─────────────────────────────────

// Imports one backup and reboots into it; on failure leaves the reason in
// s_cfgStatus. The body the Import row always ran, now for any backup name.
static void cfgImportNamed(const char *name) {
    bool ok = cfgImportFrom(s_cfg, name);
    if (ok) {
#if FEATURE_LICENSED_MODE
        // Licensed now, or by the imported config: its channels come in
        // keyless. Stripped before onWebCfgSaved() persists them.
        const bool licKeysStripped =
            (lic::active() || s_cfg.licensedMode) && licStripChannels(0);
#else
        const bool licKeysStripped = false;
#endif
        onWebCfgSaved();
        persistUiTheme();
        persistMessageAlertSetting();
        persistSplashMelodySetting();
        s_lastRenderedChannel = -1;
        s_lastRenderedCount = -1;
        refreshHeaderTime(true);
        refreshHeaderStatus(true);
        refreshChannelGlow(true);
        refreshChatView(true);
        if (licKeysStripped) {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s",
                     TR("Licensed mode: channel imported without its key"));
            refreshCfgModal();
            lv_timer_handler();
            delay(3000);
        }
        const char *importWarn = cfgImportWarning();
        const bool hasImportWarn = importWarn && importWarn[0];
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s",
                 hasImportWarn ? importWarn : TR("Imported OK - rebooting..."));
        refreshCfgModal();
        lv_timer_handler();
        delay(hasImportWarn ? 4000 : 1000);
        flushPersistentState();   // settings and transcripts must land before we go
        ESP.restart();
    } else {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Import FAILED (no %s?)"), name);
    }
}

// The timestamped name an export would get now, from the local clock.
static void cfgBackupStampName(char *out, size_t cap) {
    const time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(out, cap, "config-%Y%m%d-%H%M%S.yaml", &lt);
}

static bool cfgExportOffersStamp() { return clockIsSet(); }

static char s_cfgBackupNames[CFG_BACKUP_LIST_MAX][CFG_BACKUP_NAME_MAX];
static int  s_cfgBackupCount = 0;
static char s_cfgExportStamp[CFG_BACKUP_NAME_MAX];

// Re-read each time: a card can be swapped, and an export just added one.
static bool cfgImportHasChoice() {
    s_cfgBackupCount = cfgListBackups(s_cfgBackupNames, CFG_BACKUP_LIST_MAX);
    return s_cfgBackupCount > 1;
}

// Whether Export appends the date; remembered until reboot.
static bool s_cfgExportAppendDate = false;

// Follows the checkbox while the picker is open, and the saved choice after.
static const char *cfgExportPickLabel(int) {
    const bool stamp = s_cfgSliderSpec ? s_cfgSliderCheckStaged : s_cfgExportAppendDate;
    return stamp ? s_cfgExportStamp : "config.yaml";
}

static void cfgExportPickApply(int) {
    const char *name = cfgExportPickLabel(0);
    sdForceRescan();
    const bool ok = cfgExportAs(s_cfg, name);
    if (ok) snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Exported to /camillia/%s"), name);
    else    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Export FAILED (no SD?)"));
    openCfgActionMessageModal(s_cfgStatus);
}

// config.yaml, overwritten as Export always has, or with "Append date" ticked
// a new timestamped file beside it. Only offered with the clock set: a stamp
// from a clock that does not know the date would sort as the oldest backup on
// the card.
static void openCfgExportPicker() {
    cfgBackupStampName(s_cfgExportStamp, sizeof(s_cfgExportStamp));
    static const CfgSliderPicker kSpec = {
        TR_NOOP("Export Config As"), 1, cfgExportPickLabel, cfgExportPickApply,
        nullptr, nullptr, TR_NOOP("Append date"), &s_cfgExportAppendDate,
    };
    openCfgSliderModal(&kSpec, 0);
}

static void cfgImportPickedCommit(int idx) {
    if (idx < 0 || idx >= s_cfgBackupCount) return;
    cfgImportNamed(s_cfgBackupNames[idx]);   // reboots on success
    openCfgActionMessageModal(s_cfgStatus);
}

static const char *cfgImportPickLabel(int idx) {
    return (idx >= 0 && idx < s_cfgBackupCount) ? s_cfgBackupNames[idx] : "";
}

// The pick, then the same question the single-file Import asks, naming it.
static void cfgImportPickApply(int idx) {
    if (idx < 0 || idx >= s_cfgBackupCount) return;
    snprintf(s_cfgConfirmText, sizeof(s_cfgConfirmText),
             TR("Import %s and reboot?"), s_cfgBackupNames[idx]);
    openCfgConfirmModal(-1, s_cfgConfirmText, cfgImportPickedCommit, idx);
}

static void openCfgImportPicker() {
    static CfgSliderPicker spec = {};
    spec.title    = TR_NOOP("Import Which Backup?");
    spec.count    = s_cfgBackupCount;
    spec.labelFor = cfgImportPickLabel;
    spec.apply    = cfgImportPickApply;
    spec.leftEnd  = TR_NOOP("newest");
    spec.rightEnd = TR_NOOP("oldest");
    openCfgSliderModal(&spec, 0);
}

static void performCfgAction(int actionId) {
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    bool showActionPopup = true;

    // The row reads back its own value, so there is no popup to show.
    if (cfgGenIsAction(actionId)) {
        cfgGenPerform(actionId - CFG_ACTION_GEN_FIRST);
        refreshCfgModal();
        return;
    }

    switch (actionId) {
        case CFG_ACTION_WEBCFG: {
            showActionPopup = false;
            if (s_webCfgEnabled) {
                Serial.println("[web] CFG action: disable web config");
                s_webCfgEnabled = false;
                persistWebCfgEnabled();
                if (webCfgRunning()) {
                    webCfgEnd();   // tears the radio down; re-associate STA now
                }
                s_wifiStaKickMs = 0;   // reconnect station immediately for the IP
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Web Config disabled"));
            } else {
                Serial.println("[web] CFG action: enable web config");
#if HAS_BLE_KEYBOARD
                // The other half of the exclusion enforced when the keyboard is
                // switched on. Stopped before webCfgBegin(), so the server never
                // touches Wi-Fi power save while a BT controller is live.
                const bool stoppedBleKbd = webCfgStopBleKeyboardForWeb();
#endif
                s_webCfgEnabled = true;
                persistWebCfgEnabled();
                webCfgSetForceAp(wifiForceApMode());

                webCfgPushActiveCreds();
#if HAS_FILE_STORAGE && !defined(DEVICE_TDECK_PRO)
                bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, captureWebScreenshotPng);
#else
                bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
#endif
                if (ok) {
                    if (webCfgIsLite()) {
                        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Web Lite: %s"), webCfgIP());
                    } else {
                        snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Web: %s"), webCfgIP());
                    }
                    // On boards without the RAM for both, chat memory was just
                    // handed to Wi-Fi. Messages are dropped, not queued, so make
                    // that a dialog rather than a status line the user may miss.
                    if (webCfgChatPaused()) {
                        char warn[320];
                        snprintf(warn, sizeof(warn),
                                 TR("Web config: %s\n\n"
                                 "Chat is PAUSED while web config runs - this "
                                 "device needs that memory for Wi-Fi. Messages "
                                 "sent to you now are not received or stored.\n\n"
                                 "Turn web config off to resume messaging."),
                                 webCfgIP());
                        openCfgActionMessageModal(warn);
                    }
#if HAS_BLE_KEYBOARD
                    // After the chat-paused warning, not before: that one costs
                    // the user messages and outranks this. In practice they
                    // cannot both fire -- chat pausing is a no-PSRAM board and
                    // none of those build the keyboard in -- but the order is
                    // the one that would be right if they ever could.
                    else if (stoppedBleKbd) {
                        openCfgActionMessageModal(TR(kWebBleKbdExclusiveNotice));
                    }
#endif
#if defined(PLATFORM_LINUX)
                    // Not on port 80, so a browser given the bare IP finds
                    // nothing (issue #162). Only when switched on here: an
                    // auto-start at boot stays quiet, and the row keeps the
                    // address once this is closed.
                    else {
                        char note[320];
                        snprintf(note, sizeof(note),
                                 TR("Open http://%s/ in a browser on the same network.\n\n"
                                    "Type the address in full, port included: the "
                                    "CardputerZero serves web config on its own port, "
                                    "not the usual 80."),
                                 webCfgIP());
                        openCfgActionMessageModal(note, TR("Web Config: On"));
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
        } break;

#if HAS_VNC_HOST
        case CFG_ACTION_VNC_HOST: {
            showActionPopup = false;
            if (s_vncEnabled) {
                Serial.println("[vnc] CFG action: disable host");
                (void)applyVncEnabled(false);
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("VNC Host off"));
            } else {
                if (!vncNetworkConnected()) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                             "%s", TR("Connect to WiFi before enabling VNC"));
                    openCfgActionMessageModal(s_cfgStatus);
                    break;
                }
                char vncIp[16] = {};
                WiFi.localIP().toString().toCharArray(vncIp, sizeof(vncIp));
                Serial.println("[vnc] CFG action: enable host");
                if (!applyVncEnabled(true)) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                             "%s", TR("VNC Host failed to allocate PSRAM"));
                    openCfgActionMessageModal(s_cfgStatus);
                    break;
                }
                if (webCfgRunning() && !webCfgIsLite()) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                             TR("VNC Host on - PIN %s\nOpen http://%s/, select Remote and enter the PIN"),
                             vncHostPin(), vncIp);
                } else {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                             TR("VNC Host on - PIN %s\nTurn on Web Config, open its Remote tab and enter the PIN"),
                             vncHostPin());
                }
                s_vncPinNoticeSerial = vncHostPinSerial();
                s_vncPinNoticeSession = vncHostSession();
                openCfgActionMessageModal(s_cfgStatus);
            }
        } break;
#endif

#if HAS_WEB_FILES
        case CFG_ACTION_WEB_FILES:
            // No reboot and no server restart: the page reads the setting each
            // time it is built, and every /fs-* request checks it again, so off
            // locks the files at once even for a page already open.
            showActionPopup = false;   // row already reads On/Off
            s_cfg.webFilesEnabled = cfgWebFilesEnabled(s_cfg) ? 0 : kWebFilesOn;
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Web Files: %s"),
                     cfgWebFilesEnabled(s_cfg) ? TR("On") : TR("Off"));
            break;
#endif

#if HAS_BLE_KEYBOARD
        case CFG_ACTION_BLE_KBD_TOGGLE: {
            showActionPopup = false;   // the row already reads the state back
            if (s_cfg.bleKbdEnabled) {
                bleKeyboardSetEnabled(false);
                s_cfg.bleKbdEnabled = false;
                markConfigDirty();
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("BT Keyboard off"));
                break;
            }
            // Web config goes first, before the stack comes up and never after:
            // starting the BT controller while web config holds modem
            // power-save off aborts inside coex_core_enable(), and that is not
            // survivable -- the device reboots.
            const bool stoppedWeb = bleKbdStopWebConfigForBt();
            if (!bleKeyboardSetEnabled(true)) {
                // Leave the setting off rather than claiming a stack that is
                // not there; the row would otherwise read On forever.
                s_cfg.bleKbdEnabled = false;
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         "%s", TR("BT Keyboard failed to start (low memory)"));
                openCfgActionMessageModal(s_cfgStatus);
                break;
            }
            s_cfg.bleKbdEnabled = true;
            markConfigDirty();
            if (s_cfg.bleKbdAddr[0]) {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         TR("BT Keyboard on - connecting to %s"), s_cfg.bleKbdName);
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         "%s", TR("BT Keyboard on - use Pair to choose one"));
            }
            if (stoppedWeb) openCfgActionMessageModal(TR(kBleKbdWebExclusiveNotice));
        } break;

        case CFG_ACTION_BLE_KBD_PAIR:
            showActionPopup = false;
            if (bleKeyboardRepairNeeded()) {
                openBleKbdRepairPrompt();
            } else {
                openCfgBleKbdModal();
            }
            break;
#endif

        case CFG_ACTION_GPS_TOGGLE: {
            s_cfg.gpsEnabled = !s_cfg.gpsEnabled;
            gpsSetEnabled(s_cfg.gpsEnabled);
            persistConfigToPrefs();
            if (s_cfg.gpsEnabled) {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         "%s", TR("GPS enabled (hardware)"));
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         "%s", TR("GPS disabled (using default coordinates)"));
            }
            refreshHeaderStatus(true);
        } break;

        case CFG_ACTION_SHARE_LOCATION: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SHARE_LOCATION");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.shareLocation = !s_cfg.shareLocation;
            markConfigDirty();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     s_cfg.shareLocation ? TR("Location sharing on")
                                         : TR("Location sharing off (no position sent)"));
        } break;

        case CFG_ACTION_POSITION_PRECISION: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec POSITION_PRECISION");
            showActionPopup = false;   // the picker is the feedback
            // A slider rather than a cycling row: eleven ordered stops is a
            // scale, and cycling past ten of them to reach the eleventh is not
            // how anyone would choose one.
            openCfgPosPrecModal();
        } break;

        case CFG_ACTION_SIG_POLICY: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SIG_POLICY");
            showActionPopup = false;   // the picker is the feedback
            // A picker rather than a cycling row: each policy drops different
            // traffic, so it is chosen with its consequence in view.
            openSigPolicyModal();
        } break;

        case CFG_ACTION_EXPORT: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec EXPORT");
            // The user picked this from a menu; if they just inserted a card,
            // the failed-probe cooldown must not be what tells them there isn't one.
            sdForceRescan();
            if (cfgExportOffersStamp()) {
                showActionPopup = false;   // the picker is the next step
                openCfgExportPicker();
                break;
            }
            bool ok = cfgExport(s_cfg);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     ok ? TR("Exported to /camillia/config.yaml") : TR("Export FAILED (no SD?)"));
        } break;

        case CFG_ACTION_IMPORT: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec IMPORT");
            sdForceRescan();   // same reason as Export above
            if (cfgImportHasChoice()) {
                showActionPopup = false;   // the picker, then its own confirm
                openCfgImportPicker();
                break;
            }
            // One backup, or none: that one, which is config.yaml unless the
            // card only holds a timestamped copy.
            cfgImportNamed(s_cfgBackupCount == 1 ? s_cfgBackupNames[0] : "config.yaml");
        } break;

        case CFG_ACTION_THEME:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec THEME");
            showActionPopup = false;
            openThemeModal();   // pick from a preview grid instead of cycling
            break;

        case CFG_ACTION_OWNER_COLOR:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec OWNER_COLOR");
            showActionPopup = false;
            openCfgColorPickerModal();
            break;

        case CFG_ACTION_BATT_DISPLAY:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec BATT_DISPLAY");
            showActionPopup = false;   // row already reads Percent/Voltage
            s_cfg.battDisplayMode = (s_cfg.battDisplayMode == BATT_DISPLAY_VOLTAGE)
                                    ? BATT_DISPLAY_PERCENT : BATT_DISPLAY_VOLTAGE;
            persistConfigToPrefs();
            // Forced: the reading itself has not changed, only how it is
            // written, so the early-out in refreshHeaderStatus() would keep the
            // old string on screen until the battery moved.
            refreshHeaderStatus(true);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Battery Display: %s"),
                     s_cfg.battDisplayMode == BATT_DISPLAY_VOLTAGE ? TR("Voltage") : TR("Percent"));
            break;

        case CFG_ACTION_OTA_CHANNEL:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec OTA_CHANNEL");
            showActionPopup = false;   // row already reads Stable/Alpha
            // Toggle against what the row shows, and always store an explicit
            // choice -- picking a channel by hand must stop the auto-derivation
            // from overriding it on the next boot.
            s_cfg.otaChannel =
                (otaResolveChannel(s_cfg.otaChannel) == OTA_CHANNEL_ALPHA)
                    ? OTA_CHANNEL_STABLE : OTA_CHANNEL_ALPHA;
            otaSetChannel(s_cfg.otaChannel);
            // A check already armed against the other channel would install the
            // wrong build, so it is dropped and re-run below.
            s_cfgOtaInstallArmed = false;
            s_cfgOtaLatestTag[0] = '\0';
            persistConfigToPrefs();
            {
                // The toggle always flips the *resolved* channel, so there is
                // no "changed to what it already was" case to filter here --
                // unlike the web form, which can write an explicit value equal
                // to what AUTO already resolved to.
                //
                // Reported now rather than left to the service: the reasons a
                // check cannot run are all knowable at this point, and saying
                // so immediately beats a row that reads "checking..." for a
                // second and then gives up.
                s_otaCheckRowNote[0] = '\0';   // last check answered for the old channel
                const char *why = otaCheckBlockedReason();
                const char *chanName = cfgOtaChannelName(otaResolveChannel(s_cfg.otaChannel));
                // Unconditional, not behind s_cfgDebugLog: this is the one line
                // that says whether a switch asked for a check and, if not, why.
                Serial.printf("[ota-check] channel switched to %s; %s\n",
                              chanName, why ? why : TR("check requested"));
                if (why) {
                    utf8util::copyTruncate(s_otaCheckRowNote, sizeof(s_otaCheckRowNote),
                                           TR(" - check unavailable"));
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", why);
                    openCfgActionMessageModal(s_cfgStatus);
                } else {
                    otaRequestCheckNow();
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                             TR("Release Channel: %s - checking..."), chanName);
                }
            }
            break;

        case CFG_ACTION_LANGUAGE:
            showActionPopup = false;   // the modal is the whole interaction
            openCfgLangModal();
            break;
#if HAS_RUNTIME_ORIENTATION
        case CFG_ACTION_ORIENTATION:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec ORIENTATION");
            showActionPopup = false;   // the modal is the whole interaction
            openCfgOrientModal();
            break;
#endif

        case CFG_ACTION_UNITS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec UNITS");
            showActionPopup = false;   // row already reads Imperial/Metric
            s_cfg.displayUnits = (uint8_t)(s_cfg.displayUnits ? 0 : 1);
            // The radius is entered in the display's units, so an untouched one
            // re-rounds to the new system's 1 km / 1 mi instead of reading back
            // as 0.62 or 1.61. A value the user typed is left alone.
            if (cfgAutoFavRangeIsDefault(s_cfg.autoFavoriteRangeM)) {
                s_cfg.autoFavoriteRangeM = cfgDefaultAutoFavRangeM(s_cfg.displayUnits);
            }
            persistConfigToPrefs();
            refreshNodesDetails();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Units: %s"),
                     s_cfg.displayUnits ? TR("Imperial") : TR("Metric"));
            break;

        case CFG_ACTION_CHAT_STYLE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHAT_STYLE");
            showActionPopup = false;
            openChatStyleModal();   // pick directly; reboots only on a real change
            break;

        case CFG_ACTION_CHAT_NAMES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHAT_NAMES");
            showActionPopup = false;
            openChatNameModal();    // pick directly; applies live, no reboot
            break;

        case CFG_ACTION_NODE_NAME:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NODE_NAME");
            showActionPopup = false;   // the modal reports what it saved
            openCfgNodeNameModal();
            break;

#if defined(DEVICE_TDISPLAY_P4)
        case CFG_ACTION_ANTENNA:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec ANTENNA");
            showActionPopup = false;   // the row reads the result
            if (cfgP4AntennaExternal(s_cfg)) {
                // Back to the on-board antenna, which is always attached:
                // nothing to warn about, so no question either.
                cfgAntennaCommit(0);
            } else {
                // No changes nothing; Yes is the user saying an antenna is on.
                openCfgConfirmModal(-1, TR(kAntennaExternalWarning), cfgAntennaCommit, 1);
            }
            break;
#endif

        case CFG_ACTION_PRESET:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec PRESET");
            showActionPopup = false;   // the modal is the whole interaction
            openCfgPresetModal();
            break;

        case CFG_ACTION_COMMUNITY_PRESET:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec COMMUNITY_PRESET");
            showActionPopup = false;   // the modal is the whole interaction
            openCfgCommunityModal();
            break;

#if FEATURE_LICENSED_MODE
        case CFG_ACTION_LICENSED_MODE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec LICENSED_MODE");
            showActionPopup = false;   // the popup is the whole interaction
            openLicCfgModal();
            break;
#endif

        case CFG_ACTION_CHAT_COLORS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHAT_COLORS");
            showActionPopup = false;   // row already reads On/Off; applies live
            s_cfg.chatColorsEnabled = !s_cfg.chatColorsEnabled;
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Chat Colors: %s"),
                     s_cfg.chatColorsEnabled ? TR("On") : TR("Off"));
            s_lastRenderedChannel = -1;
            s_lastRenderedCount = -1;
            refreshChatView(true);
            break;


        case CFG_ACTION_FONT_SIZE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec FONT_SIZE");
            showActionPopup = false;
            openFontSizeModal();    // pick directly; applies live, no reboot
            break;

        case CFG_ACTION_BRIGHTNESS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec BRIGHTNESS");
            showActionPopup = false;
            openCfgBrightnessModal();   // previews live, no reboot
            break;

        case CFG_ACTION_SCREEN_TIMEOUT:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SCREEN_TIMEOUT");
            showActionPopup = false;    // row already reads the chosen value
            openCfgScreenTimeoutModal();
            break;

#if FEATURE_LOCK_SCREEN
        case CFG_ACTION_LOCK_SCREEN:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec LOCK_SCREEN");
            s_cfg.lockScreenEnabled = !s_cfg.lockScreenEnabled;
            persistConfigToPrefs();
            // Turning it off with it on screen is only reachable from web
            // config, but the call is free and makes the setting honest either
            // way: off means off now, not at the next timeout.
            if (!s_cfg.lockScreenEnabled) exitLockScreen();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Lock Screen: %s"),
                     s_cfg.lockScreenEnabled ? TR("On") : TR("Off"));
            break;

        case CFG_ACTION_LOCK_SCREEN_OFF:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec LOCK_SCREEN_OFF");
            showActionPopup = false;    // row already reads the chosen value
            openCfgLockScreenOffModal();
            break;
#endif

#if HAS_SCROLL_INVERT
        case CFG_ACTION_INVERT_SCROLL:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec INVERT_SCROLL");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.invertScroll = !s_cfg.invertScroll;
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Invert Scrolling: %s"),
                     s_cfg.invertScroll ? TR("On") : TR("Off"));
            break;
#endif

#if HAS_NAV_BAR_TOGGLE
        case CFG_ACTION_NAV_BAR:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NAV_BAR");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.navBarEnabled = !s_cfg.navBarEnabled;
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Nav Bar: %s"),
                     s_cfg.navBarEnabled ? TR("On") : TR("Off"));
            // The bar is built into the screens, not painted over them, and the
            // chat screen gives up a different amount of height for it — so
            // this is a rebuild, not a restyle. Deferred because we are inside
            // an LVGL event callback on a row the rebuild is about to delete.
            scheduleUiRebuild(true);
            break;
#endif

#if HAS_CHANNEL_PANEL_LOCK
        case CFG_ACTION_CHANNEL_PANEL_LOCK: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHANNEL_PANEL_LOCK");
            showActionPopup = false;   // row already reads Locked/Unlocked
            const bool locked = !cfgChannelPanelLocked(s_cfg);
            cfgSetChannelPanelLocked(s_cfg, locked);
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Channel Panel: %s"),
                     locked ? TR("Locked") : TR("Unlocked"));
            // Docked column or drawer changes the chat's width: a rebuild,
            // deferred out of this event callback like the Nav Bar row's.
            scheduleUiRebuild(true);
            break;
        }
#endif

#if HAS_NAV_ALIGN_SETTING
        case CFG_ACTION_NAV_ALIGN:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NAV_ALIGN");
            showActionPopup = false;   // the modal is the whole interaction
            openCfgNavAlignModal();
            break;
#endif

        case CFG_ACTION_BATT_CAL:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec BATT_CAL");
            showActionPopup = false;
            openCfgBattCalModal();      // previews live, no reboot
            break;

#if HAS_VOLUME_CONTROL
        case CFG_ACTION_VOLUME:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec VOLUME");
            showActionPopup = false;
            openCfgVolumeModal();       // previews audibly, no reboot
            break;
#endif

        case CFG_ACTION_NEIGHBOR_INFO:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NEIGHBOR_INFO");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.neighborInfoEnabled = !s_cfg.neighborInfoEnabled;
            persistConfigToPrefs();
            if (s_cfg.neighborInfoEnabled) {
                s_nextNeighborInfoTxMs = 0;
            }
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     TR("Neighborhood info: %s"),
                     s_cfg.neighborInfoEnabled ? TR("On") : TR("Off"));
            break;

        case CFG_ACTION_MESH_BEACON:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec MESH_BEACON");
            showActionPopup = false;   // the popup is the whole interaction
            openMeshBeaconCfgModal();
            break;

        case CFG_ACTION_CS_MODE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CS_MODE");
            showActionPopup = false;   // row reads the new mode
            s_cfg.chatServerMode = (uint8_t)((s_cfg.chatServerMode + 1) % 3);   // Off -> Auto -> Manual -> Off
            persistConfigToPrefs();
            chatServerModeChanged();
            break;

        case CFG_ACTION_CS_SERVER: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CS_SERVER");
            showActionPopup = false;
            if (s_cfg.chatServerNodeId) {
                chatServerClear();
                persistConfigToPrefs();
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Chat server cleared"));
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s",
                         TR("Pick one in Nodes: Chat Server"));
            }
            break;
        }

        case CFG_ACTION_CS_CHECK: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CS_CHECK");
            showActionPopup = false;
            const char *why = nullptr;
            if (chatServerCheckNow(&why)) {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s",
                         why ? TR("Searching for a chat server...") : TR("Checking chat server..."));
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", why ? why : TR("Already checking"));
            }
            break;
        }

        case CFG_ACTION_WIFI_TOGGLE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec WIFI_TOGGLE");
            s_cfg.wifiEnabled = !s_cfg.wifiEnabled;
            s_wifiStaKickMs = 0;   // (re)associate station promptly on enable
            if (!s_cfg.wifiEnabled) {
                // Master off: stop both consumers and drop the radio.
                s_cfg.mqttEnabled = false;
#if HAS_VNC_HOST
                if (s_vncEnabled) {
                    (void)applyVncEnabled(false);
                }
#endif
                if (s_webCfgEnabled) {
                    s_webCfgEnabled = false;
                    persistWebCfgEnabled();
                }
                if (webCfgRunning()) webCfgEnd();
                mqttBridgeConfigChanged();
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
            }
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     TR("WiFi %s"), s_cfg.wifiEnabled ? TR("on") : TR("off"));
            break;

        case CFG_ACTION_CHOOSE_WIFI:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHOOSE_WIFI");
            showActionPopup = false;
            openCfgWifiPickerModal();
            break;

        case CFG_ACTION_MQTT_TOGGLE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec MQTT_TOGGLE");
            s_cfg.mqttEnabled = !s_cfg.mqttEnabled;
            persistConfigToPrefs();
            // Reboot so the change applies cleanly.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     TR("MQTT Bridge: %s - rebooting..."), s_cfg.mqttEnabled ? TR("On") : TR("Off"));
            refreshCfgModal();
            lv_timer_handler();
            delay(1000);
            flushPersistentState();   // settings and transcripts must land before we go
            ESP.restart();
            break;

        case CFG_ACTION_MSG_ALERT:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec MSG_ALERT");
            showActionPopup = false;
            openAlertSoundModal();   // pick directly; navigating previews each tone
            break;

#if HAS_HAPTIC
        // Cycles in place like the blink-flash rows, five values not earning a
        // modal; each step buzzes once in the new style so it can be felt.
        case CFG_ACTION_VIBRATION: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec VIBRATION");
            showActionPopup = false;   // row already reads the style
            const uint8_t next = (uint8_t)((cfgHapticStyle(s_cfg) + 1) % HAPTIC_CFG_COUNT);
            cfgSetHapticStyle(s_cfg, next);
            markConfigDirty();
            tdisplayP4HapticPlay(next, 1);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Vibration: %s"), hapticStyleName(next));
            break;
        }
#endif

#if HAS_NOTIFY_LED
        case CFG_ACTION_NOTIFY_LED_CHANNEL_COLOR:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NOTIFY_LED_CHANNEL_COLOR");
            showActionPopup = false;
            openNotifyLedColorModal(false);
            break;

        case CFG_ACTION_NOTIFY_LED_DM_COLOR:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NOTIFY_LED_DM_COLOR");
            showActionPopup = false;
            openNotifyLedColorModal(true);
            break;
#endif

#if HAS_KB_BLINK
        case CFG_ACTION_KB_BLINK:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec KB_BLINK");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.kbBlinkEnabled = !s_cfg.kbBlinkEnabled;
            kbBlinkApplySetting();
            markConfigDirty();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Keyboard blink: %s"),
                     s_cfg.kbBlinkEnabled ? TR("On") : TR("Off"));
            break;

        // Both counts cycle 1 -> 2 -> 3 -> 1 in place, like the other small
        // enumerated rows: three values do not earn a modal. A pattern already
        // running keeps its length; the next one picks the new count up.
        case CFG_ACTION_KB_BLINK_CHAN_FLASHES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec KB_BLINK_CHAN_FLASHES");
            showActionPopup = false;   // row already reads the count
            s_cfg.kbBlinkChanFlashes =
                cfgCoerceKbFlashes((int)s_cfg.kbBlinkChanFlashes % KB_FLASHES_MAX + 1);
            markConfigDirty();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Channel blink: %u flash%s"),
                     (unsigned)s_cfg.kbBlinkChanFlashes,
                     (s_cfg.kbBlinkChanFlashes == 1) ? "" : "es");
            break;

        case CFG_ACTION_KB_BLINK_DM_FLASHES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec KB_BLINK_DM_FLASHES");
            showActionPopup = false;   // row already reads the count
            s_cfg.kbBlinkDmFlashes =
                cfgCoerceKbFlashes((int)s_cfg.kbBlinkDmFlashes % KB_FLASHES_MAX + 1);
            markConfigDirty();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("DM blink: %u flash%s"),
                     (unsigned)s_cfg.kbBlinkDmFlashes,
                     (s_cfg.kbBlinkDmFlashes == 1) ? "" : "es");
            break;
#endif

#if HAS_LIGHT_NOTIFY
        // A slider, like location precision: the values are a scale from "stops
        // soonest" to "never stops", and a scale is picked, not cycled through.
        case CFG_ACTION_NOTIFY_LIGHT_TIMEOUT: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NOTIFY_LIGHT_TIMEOUT");
            showActionPopup = false;   // the picker is the feedback
            openCfgLightTimeoutModal();
        } break;
#endif

#if HAS_POWER_LED_LEVEL
        case CFG_ACTION_POWER_LED_LEVEL: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec POWER_LED_LEVEL");
            showActionPopup = false;   // the picker is the feedback
            openCfgPowerLedModal();
        } break;
#endif

#if HAS_KB_BACKLIGHT_LEVEL
        case CFG_ACTION_KB_BACKLIGHT_LEVEL: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec KB_BACKLIGHT_LEVEL");
            showActionPopup = false;   // the picker is the feedback
#if defined(DEVICE_TDISPLAY_P4)
            openCfgActionMessageModal(
                TR("Press F11 on the keyboard to change its brightness."),
                TR("Keyboard Light"));
#else
            openCfgKbBacklightModal();
#endif
        } break;
#endif

#if HAS_KB_LIGHT_TIMEOUT
        case CFG_ACTION_KB_LIGHT_TIMEOUT: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec KB_LIGHT_TIMEOUT");
            showActionPopup = false;   // the picker is the feedback
            openCfgKbLightTimeoutModal();
        } break;
#endif

        case CFG_ACTION_SPLASH_MELODY:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SPLASH_MELODY");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.splashMelodyEnabled = !s_cfg.splashMelodyEnabled;
            persistSplashMelodySetting();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Splash melody: %s"), s_cfg.splashMelodyEnabled ? TR("On") : TR("Off"));
            break;

        case CFG_ACTION_CHANNEL_CFG:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHANNEL_CFG");
            showActionPopup = false;
            openChanCfgModal();
            break;

        case CFG_ACTION_RELEASE_NOTES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec RELEASE_NOTES");
            showActionPopup = false;
            openReleaseNotesModal();
            break;

        case CFG_ACTION_TIME_DATE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec TIME_DATE");
            showActionPopup = false;
            openTimeCfgModal();
            break;

        case CFG_ACTION_CLOCK_FORMAT:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CLOCK_FORMAT");
            showActionPopup = false;   // row already reads 12-hour/24-hour
            s_cfg.clockFormat = (s_cfg.clockFormat == CLOCK_FORMAT_12H)
                                ? CLOCK_FORMAT_24H : CLOCK_FORMAT_12H;
            liveClockSet12Hour(s_cfg.clockFormat == CLOCK_FORMAT_12H);
            persistConfigToPrefs();
            // Both forced: the minute has not changed and neither has any
            // message, so the early-outs in these two would hold the old
            // rendering on screen until something else moved.
            refreshHeaderTime(true);
            refreshChatView(true);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Clock Format: %s"),
                     (s_cfg.clockFormat == CLOCK_FORMAT_12H) ? TR("12-hour") : TR("24-hour"));
            break;

        case CFG_ACTION_OTA_UPDATE: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec OTA_UPDATE");
            Serial.println("[ota-worker] firmware update action requested");
            s_cfgOtaInstallArmed = false;
            s_cfgOtaLatestTag[0] = '\0';

#if defined(DEVICE_CARDPUTER_LORA_HAT)
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     "%s", TR("OTA disabled on Cardputer build"));
            break;
#elif defined(DEVICE_CARDPUTER_ZERO)
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     "%s", TR("Update Camillia from the M5Stack app store"));
            break;
#endif

            // A third-party installer's layout keeps another firmware in the
            // slot an update would land in. Refuse before touching the network.
            if (!otaLayoutSupportsUpdate()) {
                snprintf(s_cfgStatus,
                         sizeof(s_cfgStatus),
                         "%s", otaLayoutBlockedReason());
                break;
            }

            bool webWasRunning = webCfgRunning();
            if (webWasRunning) {
                Serial.println("[ota-worker] stopping web config to free heap before TLS");
                webCfgEnd();
                delay(120);
            }
#if HAS_BLE_KEYBOARD
            // Same reason as the web-config teardown directly above, and it has
            // to happen here rather than being left to the reboot handoff: the
            // "immediate worker run" below downloads and flashes inside the
            // *running* firmware, so a resident BLE stack spends 30-40 KB of
            // internal DRAM through the one moment the update needs it most.
            //
            // Restored below only if the update did not happen. A successful
            // install reboots, and setup() brings the keyboard back from the
            // saved pairing on the way up.
            const bool bleKbdWasOn = bleKeyboardEnabled();
            if (bleKbdWasOn) {
                Serial.println("[ota-worker] stopping BLE keyboard to free internal heap");
                bleKeyboardSetEnabled(false);
                delay(120);
            }
#endif

            bool flagSaved = requestOtaWorkerModeOnce();
            bool nvsVisible = isOtaWorkerModeRequestedOnce();
            bool rtcVisible = isOtaWorkerModeRequestedRtc();
            bool flagVisible = nvsVisible || rtcVisible;
            Serial.printf("[ota-worker] request flag saved=%d visible=%d (nvs=%d rtc=%d)\n",
                          flagSaved ? 1 : 0,
                          flagVisible ? 1 : 0,
                          nvsVisible ? 1 : 0,
                          rtcVisible ? 1 : 0);
            if (!(flagSaved && flagVisible)) {
#if HAS_BLE_KEYBOARD
                if (bleKbdWasOn) bleKeyboardSetEnabled(true);
#endif
                snprintf(s_cfgStatus,
                         sizeof(s_cfgStatus),
                         "%s", TR("OTA request failed (NVS write). Retry."));
                break;
            }

            Serial.println("[ota-worker] attempting immediate worker run");
            if (runOtaWorkerModeIfRequested()) {
                // Only reached when the update did not install -- a successful
                // one restarts inside the call and never comes back here.
#if defined(DEVICE_TDECK_PRO)
                // The OTA screen painted straight to the canvas, so LVGL's idea
                // of the panel and the panel itself have diverged. Repaint the
                // whole UI and clear in one full refresh, rather than letting
                // the next partial diff itself against a frame LVGL never drew.
                lv_obj_invalidate(lv_screen_active());
                lcd.requestRefresh(true);
#endif
#if HAS_BLE_KEYBOARD
                if (bleKbdWasOn) bleKeyboardSetEnabled(true);
#endif
                if (s_otaWorkerBootNotice[0]) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", s_otaWorkerBootNotice);
                } else {
                    snprintf(s_cfgStatus,
                             sizeof(s_cfgStatus),
                             "%s", TR("OTA worker completed. Check status."));
                }
                break;
            }

            Serial.println("[ota-worker] immediate worker did not start; falling back to reboot handoff");
            requestSkipWebAutoStartOnce();
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     "%s", TR("Rebooting into OTA minimal mode..."));
            refreshCfgModal();
            openCfgActionMessageModal(s_cfgStatus);
            delay(800);
            flushPersistentState();   // settings and transcripts must land before we go
            ESP.restart();
        } break;

        case CFG_ACTION_RESET_CHAT_COLORS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec RESET_CHAT_COLORS");
            showActionPopup = false;   // the row and the status line say it all
            s_cfg.chatColorSalt = cfgNextChatColorSalt(s_cfg.chatColorSalt);
            persistConfigToPrefs();
            // Colors are computed per row at render time, so a forced rebuild is
            // the whole of "reassign" — nothing is stored per node.
            s_lastRenderedChannel = -1;
            s_lastRenderedCount = -1;
            refreshChatView(true);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Chat colors reassigned"));
            break;

        case CFG_ACTION_CLEAR_MSGS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CLEAR_MSGS");
            Channels.clearAllMessages(true);
            DMs.clearAll(true);
            clearSelectedMsgContext();
            s_lastRenderedChannel = -1;
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Messages cleared - rebooting..."));
            refreshCfgModal();
            delay(1000);
            flushPersistentState();   // settings and transcripts must land before we go
            ESP.restart();
            refreshChatView(true);
            break;

#if HAS_SPELLCHECK
        case CFG_ACTION_SPELL_CHECK:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SPELL_CHECK");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.spellCheckEnabled = !s_cfg.spellCheckEnabled;
            persistConfigToPrefs();
            // Read on every word typed, so nothing to apply: the next word
            // finished in a compose box is checked or not accordingly.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Spell Check: %s"),
                     s_cfg.spellCheckEnabled ? TR("On") : TR("Off"));
            break;
#endif

#if HAS_FILE_STORAGE
        case CFG_ACTION_WARDRIVE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec WARDRIVE");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.wardriveLogEnabled = !s_cfg.wardriveLogEnabled;
            persistConfigToPrefs();
            // wardrive_log is told by the main loop, which mirrors this every
            // pass -- the same arrangement as the archive below.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Wardriving: %s"),
                     s_cfg.wardriveLogEnabled ? TR("On") : TR("Off"));
            break;
#endif

        case CFG_ACTION_ARCHIVE_NODES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec ARCHIVE_NODES");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.nodeArchiveEnabled = !s_cfg.nodeArchiveEnabled;
            persistConfigToPrefs();
            // node_db is told by the main loop, which mirrors this every pass
            // (see the nodeArchiveSetEnabled call there) -- so nothing to push
            // from here, and no SD access on a config keypress.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Archive Dropped Nodes: %s"),
                     s_cfg.nodeArchiveEnabled ? TR("On") : TR("Off"));
            break;

        case CFG_ACTION_SHOW_ARCHIVED:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SHOW_ARCHIVED");
            showActionPopup = false;   // row already reads On/Off
            s_cfg.nodeArchiveShow = !s_cfg.nodeArchiveShow;
            persistConfigToPrefs();
            // Nothing is read here: the archived half comes off the card when
            // the Nodes screen takes its snapshot, so this takes effect the next
            // time that screen is opened. Deliberately so -- touching the card
            // from a config keypress is the SPI contention with the radio that
            // the snapshot-time rule exists to avoid.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Show Archived Nodes: %s"),
                     s_cfg.nodeArchiveShow ? TR("On") : TR("Off"));
            break;

        case CFG_ACTION_CLEAR_NODES: {
            // Options as the confirmation left them (cfgClearNodesAddOptions()).
            const bool inclFavs = s_clearNodesInclFavs;
            const bool inclArch = s_clearNodesInclArchive && nodeArchiveSlotExists();
            if (s_cfgDebugLog) {
                Serial.printf("[lvgl-cfg] exec CLEAR_NODES favs=%d archive=%d\n",
                              inclFavs ? 1 : 0, inclArch ? 1 : 0);
            }
            int removed = 0;
            bool archiveCleared = false;
            if (inclFavs) {
                Nodes.clearPersisted();
                archiveCleared = clearNodeDbOnSd(inclArch);
            } else {
                removed = Nodes.clearNonFavorites();
                // Not clearNodeDbOnSd(): its legacy files are whole-table
                // snapshots, so deleting them would take the favorites too.
                if (inclArch) archiveCleared = clearNodeArchiveOnSd();
            }
            // Reported rather than assumed: with no card in the slot the archive
            // is still out there on a card somewhere, and claiming to have
            // cleared it would be a lie the user acts on.
#if HAS_SD_CARD
            const char *archNote = !inclArch      ? ""
                                 : archiveCleared ? TR(", SD archive cleared")
                                                  : TR(", SD archive NOT cleared (no card)");
#else
            // The CardputerZero's archive is on its own disk, not a card.
            const char *archNote = !inclArch      ? ""
                                 : archiveCleared ? TR(", archive cleared")
                                                  : TR(", archive NOT cleared");
#endif
            if (inclFavs) {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("All nodes cleared%s - rebooting..."),
                         archNote);
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         TR("Cleared %d nodes, kept %d favorites%s - rebooting..."),
                         removed, Nodes.count(), archNote);
            }
            refreshCfgModal();
            delay(1000);
            flushPersistentState();   // settings and transcripts must land before we go
            ESP.restart();
            break;
        }

        case CFG_ACTION_FACTORY_RESET:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec FACTORY_RESET");
            Channels.clearAllMessages(true);
            DMs.clearAll(true);
            Nodes.clearPersisted();
            clearNodeDbOnSd();
            sdRmDirRecursive("/camillia/dms");
            cfgFactoryEraseNvs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Factory reset - rebooting..."));
            refreshCfgModal();
            delay(1000);
            flushPersistentState();   // settings and transcripts must land before we go
            ESP.restart();
            break;

#if HAS_SD_MALWARE_SCAN
        case CFG_ACTION_SD_SCAN:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SD_SCAN");
            showActionPopup = false;   // the scan puts up its own result
            cfgSdScanRun();
            break;
#endif
    }

    if (showActionPopup && s_cfgStatus[0]) {
        openCfgActionMessageModal(s_cfgStatus);
    }
    refreshCfgModal();
}

static void closeCfgConfirmModal() {
    if (lvObjValid(s_cfgConfirmBackdrop)) {
        lv_obj_del(s_cfgConfirmBackdrop);
    } else if (lvObjValid(s_cfgConfirmModal)) {
        lv_obj_del(s_cfgConfirmModal);
    }
    s_cfgConfirmBackdrop = nullptr;
    s_cfgConfirmModal = nullptr;
    s_cfgConfirmScroll = nullptr;
    s_cfgConfirmAltFn = nullptr;
    s_cfgConfirmAltLabel[0] = '\0';
    s_cfgConfirmAltKey = 0;
    s_cfgConfirmPendingAction = -1;
    // Cleared with the dialog, so a later row-based confirm cannot inherit a
    // callback left behind by a picker.
    s_cfgConfirmFn = nullptr;
    s_cfgConfirmFnArg = 0;
    s_cfgConfirmText[0] = '\0';
    // Gone with the dialog. The two flags are left for performCfgAction(),
    // which runs after this close on Yes; the next open resets them.
    s_clearNodesFavsCheck = nullptr;
    s_clearNodesArchCheck = nullptr;
}

// Yes: run the pending action. No/cancel: just close the dialog, returning to
// the CFG screen underneath.
static void cfgConfirmAccept() {
    int action = s_cfgConfirmPendingAction;
    // Taken before the close, which clears them.
    CfgConfirmFn fn = s_cfgConfirmFn;
    const int    arg = s_cfgConfirmFnArg;
    if (action == CFG_ACTION_OTA_UPDATE) {
        Serial.println("[ota-worker] confirm accepted for firmware update");
    }
    closeCfgConfirmModal();
    if (fn) { fn(arg); return; }        // most of these do not return
    if (action >= 0) performCfgAction(action);
}

// The third button. Same shape as cfgConfirmAccept(): the callback is taken
// before the close, because closing clears it.
static void cfgConfirmAlt() {
    CfgConfirmFn fn = s_cfgConfirmAltFn;
    const int    arg = s_cfgConfirmFnArg;
    closeCfgConfirmModal();
    if (fn) fn(arg);
}

static void cfgConfirmReject() {
    closeCfgConfirmModal();
    refreshCfgModal();
}

static void onCfgConfirmAltPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgConfirmAlt();
}

static void onCfgConfirmYesPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgConfirmAccept();
}

static void onCfgConfirmNoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgConfirmReject();
}

// Clear Nodes options. The flags are the truth; the boxes mirror them, whether
// the change came from a tap (the box already flipped itself) or from F/A.
static void cfgClearNodesSyncChecks() {
    // The focus ring is for key input. A touch-only board shows it only with
    // a keyboard attached; tapping a box needs no cursor.
#if UI_TOUCH_ONLY_PROFILE
    const bool showFocus = keyboardAttached();
#else
    const bool showFocus = true;
#endif
    lv_obj_t *const boxes[2] = { s_clearNodesFavsCheck, s_clearNodesArchCheck };
    const bool on[2] = { s_clearNodesInclFavs, s_clearNodesInclArchive };
    for (int i = 0; i < 2; i++) {
        if (!lvObjValid(boxes[i])) continue;
        if (on[i]) lv_obj_add_state(boxes[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(boxes[i], LV_STATE_CHECKED);
        const bool focused = showFocus && i == s_clearNodesFocus;
        lv_obj_set_style_outline_width(boxes[i], focused ? 2 : 0, 0);
        lv_obj_set_style_outline_pad(boxes[i], 2, 0);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_outline_color(boxes[i], lv_color_make(0, 0, 0), 0);   // e-paper
#else
        lv_obj_set_style_outline_color(boxes[i], lv_color_hex(0xFFC34D), 0);
#endif
    }
}

static void onClearNodesCheckChanged(lv_event_t *e) {
    lv_obj_t *cb = lv_event_get_target_obj(e);
    const bool on = lv_obj_has_state(cb, LV_STATE_CHECKED);
    if (cb == s_clearNodesFavsCheck)      s_clearNodesInclFavs = on;
    else if (cb == s_clearNodesArchCheck) s_clearNodesInclArchive = on;
}

// Keys for the boxes: up/down (and left/right, which the M9 and the arrow
// keyboards send) move the focus, wrapping; Space toggles the focused box; F
// and A toggle theirs directly, the way Y and N answer for the buttons. True
// when the key was one of these.
static bool cfgClearNodesOptionKey(char k) {
    if (s_cfgConfirmPendingAction != CFG_ACTION_CLEAR_NODES) return false;
    const int count = s_clearNodesArchCheck ? 2 : 1;
    if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN || k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN) {
        const int step = (k == KEY_SCROLL_UP || k == KEY_PREV_CHAN) ? -1 : 1;
        s_clearNodesFocus = (s_clearNodesFocus + step + count) % count;
    } else if (k == ' ') {
        if (s_clearNodesFocus == 0) s_clearNodesInclFavs = !s_clearNodesInclFavs;
        else                        s_clearNodesInclArchive = !s_clearNodesInclArchive;
    } else if ((k == 'f' || k == 'F') && s_clearNodesFavsCheck) {
        s_clearNodesInclFavs = !s_clearNodesInclFavs;
        s_clearNodesFocus = 0;
    } else if ((k == 'a' || k == 'A') && s_clearNodesArchCheck) {
        s_clearNodesInclArchive = !s_clearNodesInclArchive;
        s_clearNodesFocus = 1;
    } else {
        return false;
    }
    cfgClearNodesSyncChecks();
    return true;
}

static void cfgClearNodesAddOptions(lv_obj_t *parent, lv_color_t textColor) {
    auto addCheck = [&](const char *label) {
        lv_obj_t *cb = lv_checkbox_create(parent);
        lv_obj_set_width(cb, lv_pct(100));
        lv_checkbox_set_text(cb, label);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(cb, textColor, 0);
        lv_obj_add_event_cb(cb, onClearNodesCheckChanged, LV_EVENT_VALUE_CHANGED, nullptr);
        return cb;
    };
#if UI_TOUCH_ONLY_PROFILE
    s_clearNodesFavsCheck = addCheck(TR("Include favorites"));
#else
    s_clearNodesFavsCheck = addCheck(TR("Include (F)avorites"));
#endif
    // Only where the board can keep an archive: elsewhere there is none to clear.
    if (nodeArchiveSlotExists()) {
#if UI_TOUCH_ONLY_PROFILE
        s_clearNodesArchCheck = addCheck(TR("Include archived (SD card)"));
#elif HAS_SD_CARD
        s_clearNodesArchCheck = addCheck(TR("Include (A)rchived (SD card)"));
#else
        // The CardputerZero keeps the archive on its own disk.
        s_clearNodesArchCheck = addCheck(TR("Include (A)rchived"));
#endif
    }
#if UI_TOUCH_ONLY_PROFILE
    if (keyboardAttached())
#endif
    {
        lv_obj_t *hint = lv_label_create(parent);
        lv_obj_set_width(hint, lv_pct(100));
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(hint, textColor, 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hint, TR("Move=Select  Space=Toggle"));
    }
    cfgClearNodesSyncChecks();
}

static void openCfgConfirmModal(int actionId, const char *text,
                                CfgConfirmFn fn, int fnArg,
                                const char *altLabel, CfgConfirmFn altFn) {
    if (!s_rootScreen || s_cfgConfirmModal || s_cfgConfirmBackdrop) return;
    s_cfgConfirmPendingAction = actionId;
    s_cfgConfirmFn = fn;
    s_cfgConfirmFnArg = fnArg;
    s_clearNodesInclFavs = false;
    s_clearNodesInclArchive = false;
    s_clearNodesFocus = 0;

    s_cfgConfirmAltFn = (altLabel && altLabel[0]) ? altFn : nullptr;
    s_cfgConfirmAltKey = 0;
    s_cfgConfirmAltLabel[0] = '\0';
    if (s_cfgConfirmAltFn) {
        snprintf(s_cfgConfirmAltLabel, sizeof(s_cfgConfirmAltLabel), "%s", altLabel);
        // The mnemonic, read out of the label the same way "(Y)es" and "(N)o"
        // carry theirs, so the button and the key it answers to cannot disagree.
        for (const char *p2 = s_cfgConfirmAltLabel; p2[0] && p2[1] && p2[2]; p2++) {
            if (p2[0] == '(' && p2[2] == ')') {
                s_cfgConfirmAltKey = (char)tolower((unsigned char)p2[1]);
                break;
            }
        }
        if (!s_cfgConfirmAltKey) {
            s_cfgConfirmAltKey = (char)tolower((unsigned char)s_cfgConfirmAltLabel[0]);
        }
    }

    // Sized for the community preset confirmation, a summary plus the
    // community's notes; the SD scan's, which names the files it is about to
    // delete, is next. Everything else here is one row label.
    char actionText[768];
    const bool ownText = (text && text[0]);
    if (ownText) {
        strncpy(actionText, text, sizeof(actionText) - 1);
        actionText[sizeof(actionText) - 1] = '\0';
    } else {
        cfgActionLabel(actionId, actionText, sizeof(actionText));
    }
    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    // 40 px of margin is comfortable on a 320 px panel and a fifth of a 240 px
    // one -- and a fifth of the panel, on this dialog, comes straight out of
    // the button row. The narrow boards give up the margin instead.
    int modalW = w - (w <= 260 ? 16 : 40);
    if (modalW < 160) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t actionBg = lightUi ? lv_color_hex(0xF5F9FF) : lv_color_hex(0x123266);
    const lv_color_t actionTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const uint32_t noBtnBg = lightUi ? 0xC76565 : 0x6B3030;
    const uint32_t yesBtnBg = lightUi ? 0x429A56 : 0x2F6B30;
    // Neither the safe answer nor the one the dialog is named after, and the
    // most destructive thing on the screen: its own colour, not a second green.
    const uint32_t altBtnBg = lightUi ? 0xB07028 : 0x7A4A12;
    const lv_color_t btnTextColor = lv_color_hex(0xFFFFFF);

    // Full-screen backdrop makes the dialog truly modal for touch builds.
    s_cfgConfirmBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgConfirmBackdrop, w, h);
    lv_obj_align(s_cfgConfirmBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgConfirmBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgConfirmBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgConfirmBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgConfirmBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgConfirmBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgConfirmBackdrop, 0, 0);

    s_cfgConfirmModal = lv_obj_create(s_cfgConfirmBackdrop);
    lv_obj_set_size(s_cfgConfirmModal, modalW, LV_SIZE_CONTENT);
    // Content-sized as before, but no longer allowed off the panel. Long text
    // grows the box below until this bites; past that the box scrolls and the
    // dialog stays put, so the buttons cannot be pushed out of reach.
    lv_obj_set_style_max_height(s_cfgConfirmModal, (lv_coord_t)(h - 12), 0);
    lv_obj_align(s_cfgConfirmModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgConfirmModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgConfirmModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgConfirmModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_cfgConfirmModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgConfirmModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgConfirmModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_cfgConfirmModal, 10, 0);
    lv_obj_set_style_pad_row(s_cfgConfirmModal, 10, 0);
    lv_obj_set_flex_flow(s_cfgConfirmModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgConfirmModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgConfirmBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgConfirmModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title,
                      actionId == CFG_ACTION_CHAT_STYLE ? TR("Confirm Chat Style") : TR("Confirm?"));

    lv_obj_t *actionBox = lv_obj_create(s_cfgConfirmModal);
    s_cfgConfirmScroll = actionBox;
    lv_obj_set_width(actionBox, lv_pct(100));
    lv_obj_set_height(actionBox, LV_SIZE_CONTENT);
    // Capped rather than fixed: a one-line confirmation still draws a one-line
    // box, and only text long enough to crowd the buttons starts scrolling. Half
    // the panel leaves the title above and the Yes/No row below their room,
    // which is what the dialog is actually for.
    lv_coord_t boxMaxH = (lv_coord_t)(h / 2);
    if (boxMaxH < 44) boxMaxH = 44;
    lv_obj_set_style_max_height(actionBox, boxMaxH, 0);
    setupVScroll(actionBox);
    lv_obj_set_scrollbar_mode(actionBox, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(actionBox, actionBg, 0);
    lv_obj_set_style_bg_opa(actionBox, LV_OPA_60, 0);
    lv_obj_set_style_border_width(actionBox, 1, 0);
    lv_obj_set_style_border_color(actionBox, modalBorder, 0);
    lv_obj_set_style_pad_left(actionBox, 6, 0);
    lv_obj_set_style_pad_right(actionBox, 6, 0);
    lv_obj_set_style_pad_top(actionBox, 4, 0);
    lv_obj_set_style_pad_bottom(actionBox, 4, 0);

    lv_obj_t *q = lv_label_create(actionBox);
    lv_obj_set_width(q, lv_pct(100));
    lv_obj_set_style_text_font(q, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(q, actionTextColor, 0);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    // "Action: <row label>" reads correctly for a row, and wrongly for a
    // sentence someone wrote for this dialog. Chat Style was the first caller
    // to supply its own wording and got a special case; ownText generalises it.
    if (ownText || actionId == CFG_ACTION_CHAT_STYLE) {
        lv_label_set_text(q, actionText);
    } else {
        lv_label_set_text_fmt(q, TR("Action: %s"), actionText);
    }

    if (actionId == CFG_ACTION_CLEAR_NODES) {
        cfgClearNodesAddOptions(s_cfgConfirmModal, titleTextColor);
    }

    lv_obj_t *btnRow = lv_obj_create(s_cfgConfirmModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);

    // Divided, not wished for. Two buttons at the old fixed 84 px with a 14 px
    // gap want 182 px, and a 240 px panel offers this row 180 -- which is why
    // Yes and No each lost a sliver off the outside edge. Add the optional
    // third button and the row wants 280 in the same 180.
    //
    // So the row is measured: whatever the modal's content box is, minus the
    // gaps, divided by however many buttons this dialog ended up with. 84 stays
    // the ceiling, so nothing changes on a panel that always had the room.
    const int btnCount = s_cfgConfirmAltFn ? 3 : 2;
    const int rowW     = modalW - 2 * 10;            // the modal's own padding
    int btnGap = 14;
    if (rowW < btnCount * 84 + (btnCount - 1) * btnGap) btnGap = 6;
    int btnMinW = (rowW - (btnCount - 1) * btnGap) / btnCount;
    if (btnMinW > 84) btnMinW = 84;
    // No lower clamp. Integer division already guarantees the row fits, and a
    // floor is the one thing that could put it back over the edge -- on a panel
    // narrow enough to hit it, a cramped button beats a missing one. Nothing in
    // the fleet is that narrow today; this is here so it stays true if
    // something is.

    lv_obj_set_style_pad_column(btnRow, btnGap, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makeConfirmBtn = [](lv_obj_t *parent,
                             const char *text,
                             uint32_t bgColor,
                             lv_color_t txtColor,
                             lv_event_cb_t cb,
                             int minW) {
        lv_obj_t *btn = lv_btn_create(parent);
    #if defined(DEVICE_TDECK_PRO)
        LV_UNUSED(bgColor);
        LV_UNUSED(txtColor);
        lv_obj_remove_style_all(btn);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    #endif
        lv_obj_set_height(btn, 36);
        lv_obj_set_style_min_width(btn, minW, 0);
        // Grows to the share it was measured for rather than to its text, so
        // the row lands on the arithmetic above instead of near it.
        lv_obj_set_flex_grow(btn, 1);
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
        lv_obj_set_style_text_color(lbl, txtColor, 0);
    #endif
        // Bounded by the button so a caller's own alt label -- "(F)ormat and
        // erase" is a real one -- shortens instead of pushing its button wide
        // and the row off the panel.
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl, TR(text));
        lv_obj_center(lbl);
        return btn;
    };
#if UI_TOUCH_ONLY_PROFILE
    makeConfirmBtn(btnRow, TR_NOOP("No"), noBtnBg, btnTextColor, onCfgConfirmNoPressed, btnMinW);
    makeConfirmBtn(btnRow, TR_NOOP("Yes"), yesBtnBg, btnTextColor, onCfgConfirmYesPressed, btnMinW);
#else
    makeConfirmBtn(btnRow, TR_NOOP("(N)o"), noBtnBg, btnTextColor, onCfgConfirmNoPressed, btnMinW);
    makeConfirmBtn(btnRow, TR_NOOP("(Y)es"), yesBtnBg, btnTextColor, onCfgConfirmYesPressed, btnMinW);
#endif
    if (s_cfgConfirmAltFn) {
        makeConfirmBtn(btnRow, s_cfgConfirmAltLabel, altBtnBg, btnTextColor,
                       onCfgConfirmAltPressed, btnMinW);
    }
}

