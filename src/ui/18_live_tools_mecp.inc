// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: channel actions, Live tools, MECP form, Live filter.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void closeChannelActionsModal() {
    lvObjDeleteSafe(s_channelActionsModal);
    s_channelActionsModal = nullptr;
    s_channelActionsMuteBtn = nullptr;
    s_channelActionsMuteLabel = nullptr;
    s_channelActionsShareBtn = nullptr;
    s_channelActionsShareLabel = nullptr;
    s_channelActionsChanIdx = -1;
}

static void openChannelActionsModal() {
    if (s_channelActionsModal) closeChannelActionsModal();
    if (s_activeChannel < 0 || s_activeChannel >= MESH_CHANNELS) return;
    // On Heltec the bottom nav is embedded in every full-screen modal, so this
    // can be tapped from within one. Close any sibling first, matching how the
    // other nav targets behave. No-ops on keyboard boards opened from home.
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeCfgModal();
    closeLiveModal();
    closeLegendModal();
    s_channelActionsChanIdx = s_activeChannel;

    const int modalW = min(220, (int)lv_disp_get_hor_res(NULL) - 14);
    // Two 30px buttons plus the two-line title and the close hint overflow a
    // 135px-tall panel (Cardputer), and the modal is LV_SIZE_CONTENT — it would
    // simply run off the screen. Short panels get the compact button instead.
    const int btnH = (lv_disp_get_ver_res(NULL) <= 160) ? 24 : 30;
    lv_obj_t *parent = s_rootScreen ? s_rootScreen : lv_scr_act();
    s_channelActionsModal = lv_obj_create(parent);
    lv_obj_set_width(s_channelActionsModal, modalW);
    lv_obj_set_height(s_channelActionsModal, LV_SIZE_CONTENT);
    lv_obj_align(s_channelActionsModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_channelActionsModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_channelActionsModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_channelActionsModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_channelActionsModal, 1, 0);
    lv_obj_set_style_border_color(s_channelActionsModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_channelActionsModal, 6, 0);
    lv_obj_set_style_pad_row(s_channelActionsModal, 6, 0);
    lv_obj_set_flex_flow(s_channelActionsModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_channelActionsModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_channelActionsModal);

    const char *chName = CHANNEL_KEYS[s_channelActionsChanIdx].name_buf[0]
                             ? CHANNEL_KEYS[s_channelActionsChanIdx].name_buf
                             : CHANNEL_KEYS[s_channelActionsChanIdx].name;

    lv_obj_t *title = lv_label_create(s_channelActionsModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(title, TR("Channel Actions\n%s"),
                          (chName && chName[0]) ? chName : TR("(unnamed)"));

    lv_obj_t *btn = lv_btn_create(s_channelActionsModal);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
#endif
    s_channelActionsMuteBtn = btn;
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, btnH);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, onChannelActionMutePressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    s_channelActionsMuteLabel = lbl;
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x13233D) : lv_color_hex(0xE8F1FF), 0);
    lv_obj_center(lbl);

    lv_obj_t *shareBtn = lv_btn_create(s_channelActionsModal);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_remove_style_all(shareBtn);
    lv_obj_add_flag(shareBtn, LV_OBJ_FLAG_CLICKABLE);
#endif
    s_channelActionsShareBtn = shareBtn;
    lv_obj_set_width(shareBtn, lv_pct(100));
    lv_obj_set_height(shareBtn, btnH);
    lv_obj_set_style_radius(shareBtn, 4, 0);
    lv_obj_set_style_pad_all(shareBtn, 2, 0);
    lv_obj_set_style_shadow_width(shareBtn, 0, 0);
    lv_obj_add_event_cb(shareBtn, onChannelActionSharePressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *shareLbl = lv_label_create(shareBtn);
    s_channelActionsShareLabel = shareLbl;
    lv_obj_set_style_text_font(shareLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(shareLbl,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x13233D) : lv_color_hex(0xE8F1FF), 0);
    lv_obj_center(shareLbl);

#if UI_TOUCH_ONLY_PROFILE
    // Touch-only board: the corner X stands in for the keyboard hint, and the
    // two actions keep the full width of the modal to themselves. The title
    // carries a channel name on its second line, so it is told to stop short of
    // the button.
    reserveHeltecCloseXGap(title);
    appendHeltecCloseX(s_channelActionsModal, onChannelActionClosePressed);
#else
    lv_obj_t *hint = lv_label_create(s_channelActionsModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("%s = Close"), modalCloseKeyLabel());
#endif

    refreshChannelActionsModal();
}

static void refreshLiveView(bool force) {
    if (!s_liveModal || !s_liveList) return;
    if (!lv_obj_is_valid(s_liveModal) || !lv_obj_is_valid(s_liveList)) {
        s_liveModal = nullptr;
        s_liveList = nullptr;
        s_lastRenderedLiveCount = -1;
        s_lastRenderedLiveScrollOff = -1;
        return;
    }
    if (lv_obj_get_parent(s_liveList) != s_liveModal
        || lv_obj_get_disp(s_liveModal) == nullptr
        || lv_obj_get_disp(s_liveList) == nullptr) {
        s_liveList = nullptr;
        s_lastRenderedLiveCount = -1;
        s_lastRenderedLiveScrollOff = -1;
        return;
    }

    // The same face the chat and DM views draw with, so one Font Size setting
    // governs every text surface rather than the feed keeping a size of its own.
    // This was a fixed montserrat_10 (12 on the Pager), which ignored the
    // setting entirely — and, being a raw Montserrat face rather than an
    // emoji-enabled one, drew any emoji in a live line as a fallback box
    // despite the text going through setLabelTextEmojiSafe() below.
    //
    // Only the feed rows scale. The header's filter chip is fixed at 92px to
    // hold the longest filter name, so a larger face would spill out of it
    // rather than move the layout with it.
    const lv_font_t *liveBodyFont = scaledChatFont(kChannelChatFont);

    const Channel &ch = Channels.get(CHAN_LIVE);
    const uint32_t nowMs = millis();
    if (s_liveClearFlashUntilMs != 0
        && (int32_t)(nowMs - s_liveClearFlashUntilMs) >= 0) {
        s_liveClearFlashUntilMs = 0;
        s_lastRenderedLiveCount = -1;
        s_lastRenderedLiveScrollOff = -1;
    }
    if (!force && s_lastRenderedLiveCount == ch.count && s_lastRenderedLiveScrollOff == ch.scrollOff) {
        return;
    }

    const bool stickToTop = force || (lv_obj_get_scroll_y(s_liveList) <= 2);
    const int32_t prevScrollY = lv_obj_get_scroll_y(s_liveList);

    lv_obj_clean(s_liveList);

    int rowCount = 0;
    for (int row = 0; row < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(CHAN_LIVE, row);
        if (!dl) break;

        // Filtered out before anything is created, so a hidden line costs no
        // LVGL pool memory and does not count against the low-memory cutoff
        // below — the point of filtering a busy feed is to render less of it.
        if (s_liveNoMqtt && dl->viaMqtt) continue;
        if (s_liveFilter != LIVE_FILTER_ALL
            && !liveFilterMatches(s_liveFilter, classifyLiveTraffic(*dl))) {
            continue;
        }

        // Each wrapped label costs a few hundred bytes from LVGL's fixed
        // LV_MEM_SIZE pool. If the pool can't satisfy lv_label_create(), LVGL
        // fires LV_ASSERT_MALLOC and the default handler resets the device —
        // this is the live-screen crash seen after long uptime once the pool
        // is full/fragmented. Stop early and render what fits instead.
        lv_mem_monitor_t liveMem;
        lv_mem_monitor(&liveMem);
        if (liveMem.free_biggest_size < 3072) {
            logLvglMemDiag("live render stopped (low LVGL mem)");
            break;
        }
        rowCount++;

        lv_obj_t *msg = lv_label_create(s_liveList);
        lv_obj_set_width(msg, lv_pct(100));
        lv_obj_set_style_text_font(msg, liveBodyFont, 0);
        lv_obj_set_style_pad_left(msg, 2, 0);
        lv_obj_set_style_pad_right(msg, 4, 0);
        lv_obj_set_style_pad_top(msg, 1, 0);
        lv_obj_set_style_pad_bottom(msg, 1, 0);
        lv_obj_set_style_radius(msg, 2, 0);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
#if UI_TOUCH_ONLY_PROFILE
        lv_obj_add_flag(msg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(msg, onLiveRowLongPressed, LV_EVENT_LONG_PRESSED, nullptr);
#endif

        uint16_t lineColor = liveLineTrafficColor(*dl);
        if (dl->packetId) {
            switch (dl->ack) {
                case DisplayLine::ACKED:
                    lineColor = (s_cfg.uiMode == UI_MODE_LIGHT) ? (uint16_t)0x0320 : TFT_GREEN;
                    break;
                case DisplayLine::ACKED_RELAY:
                    lineColor = userMessageAccentColor565();
                    break;
                case DisplayLine::NAKED:
                    lineColor = TFT_RED;
                    break;
                case DisplayLine::TX_FAILED:
                    lineColor = TFT_RED;
                    break;
                default:
                    break;
            }
        }

    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(msg, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(msg, liveLineBgColor(*dl), 0);
        lv_obj_set_style_bg_opa(msg, (lineColor == TFT_RED) ? LV_OPA_60 : LV_OPA_40, 0);
        lv_obj_set_style_text_color(msg, tftColorToLv(lineColor), 0);
    #endif

        char rendered[128];
        formatLiveLineText(*dl, rendered, sizeof(rendered));
        setLabelTextEmojiSafe(msg, rendered);
    }

    if (rowCount == 0) {
        lv_obj_t *empty = lv_label_create(s_liveList);
        lv_obj_set_style_text_font(empty, liveBodyFont, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(empty, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
    #endif
        // Naming the filter matters here: an empty screen under a filter looks
        // exactly like a dead radio, and the difference is one word.
        if (s_liveClearFlashUntilMs != 0) {
            lv_label_set_text(empty, TR("Live feed cleared"));
        } else if (s_liveFilter == LIVE_FILTER_ALL && !s_liveNoMqtt) {
            lv_label_set_text(empty, TR("No live traffic yet"));
        } else {
            char what[40];
            if (s_liveNoMqtt && s_liveFilter == LIVE_FILTER_ALL) {
                snprintf(what, sizeof(what), "%s", TR("non-MQTT"));
            } else if (s_liveNoMqtt) {
                snprintf(what, sizeof(what), TR("non-MQTT %s"), liveFilterName(s_liveFilter));
            } else {
                snprintf(what, sizeof(what), "%s", liveFilterName(s_liveFilter));
            }
            lv_label_set_text_fmt(empty, TR("No %s traffic yet\n(filter is on - %s)"),
                                  what,
#if UI_TOUCH_ONLY_PROFILE
                                  TR("tap Filter to change")
#else
                                  TR("press F to change")
#endif
                                  );
        }
    }

    // Flush the flex layout so scroll extents are accurate before positioning;
    // otherwise scroll_to_y bounds against stale geometry and can leave the top
    // row not flush with the edge.
    lv_obj_update_layout(s_liveList);

    if (stickToTop) {
        lv_obj_scroll_to_y(s_liveList, 0, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(s_liveList, prevScrollY, LV_ANIM_OFF);
    }

    bool overflow = lv_obj_get_scroll_bottom(s_liveList) > 0 || lv_obj_get_scroll_top(s_liveList) > 0;
    lv_obj_set_scrollbar_mode(s_liveList, overflow ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);

    s_lastRenderedLiveCount = ch.count;
    s_lastRenderedLiveScrollOff = ch.scrollOff;
}

// Report the LVGL heap pool (a fixed LV_MEM_SIZE arena, not the system heap).
// Fragmentation/exhaustion of this pool after long uptime is the prime suspect
// for the live-screen crash seen across all boards.
static void logLvglMemDiag(const char *tag) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    // stackFree is the lowest the loopTask stack has ever got (bytes still
    // unused at the high-water mark), so it accounts for the deep emoji
    // rasterization path even if we are not in it right now. If this ever
    // approaches zero, raise SET_LOOP_TASK_STACK_SIZE at the top of this file.
    unsigned stackFree = (unsigned)uxTaskGetStackHighWaterMark(nullptr);
    // The LVGL pool and the system heap are separate arenas; stb_truetype
    // rasterizes out of the LVGL pool (STBTT_malloc is #defined to lv_malloc),
    // so print both to tell which one is actually under pressure.
    unsigned heapBiggest =
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("[lvgl] %s pool used=%u%% free=%u biggest=%u frag=%u%% "
                  "stackFree=%u heapBiggest=%u\n",
                  tag ? tag : "mem",
                  (unsigned)m.used_pct,
                  (unsigned)m.free_size,
                  (unsigned)m.free_biggest_size,
                  (unsigned)m.frag_pct,
                  stackFree,
                  heapBiggest);
}

static void openLiveModal() {
    if (s_liveModal && !lvObjAlive(s_liveModal)) {
        s_liveModal = nullptr;
        s_liveList = nullptr;
    }
    if (!s_rootScreen || s_liveModal) return;
    logLvglMemDiag("openLiveModal");
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeCfgModal();
    closeLegendModal();
    closeChannelActionsModal();
    // Tools too, since issue #94: it is a full screen with a working nav bar
    // now, so this is reachable by tapping a cell from inside it. It used to be
    // unreachable — Tools was a card on a backdrop that swallowed the tap — and
    // without this Tools would stay alive and allocated underneath, and reappear
    // when this screen closed.
    if (s_liveToolsModal) closeLiveToolsModal();

    Channels.get(CHAN_LIVE).scrollOff = 0;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;

    // If the LVGL pool is too full to safely build the modal frame + list,
    // abort instead of crashing inside lv_obj_create (LV_ASSERT_MALLOC resets
    // the device). The closes above already freed any other open modals, so
    // this only trips when memory is genuinely too low. The per-line guard in
    // refreshLiveView handles running low partway through rendering.
    {
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 6144) {
            logLvglMemDiag("openLiveModal aborted (low LVGL mem)");
            return;
        }
    }

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_liveModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_liveModal, modalW, modalH);
    lv_obj_align(s_liveModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_liveModal, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_liveModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_liveModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_liveModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_liveModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_liveModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_liveModal, 1, 0);
    lv_obj_set_style_pad_all(s_liveModal, 4, 0);
    lv_obj_set_style_pad_row(s_liveModal, 4, 0);
    lv_obj_set_flex_flow(s_liveModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_liveModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Portrait (P4, Heltec V4): Tools' title in place of the header bar. On a
    // touch build Filter is in the row under the title in either orientation.
#if HAS_TOOL_STACK_LAYOUT
    if (toolStackLayout()) {
        toolStackTitle(s_liveModal, TR("LIVE"));
    } else
#endif
    {
    lv_obj_t *header = lv_obj_create(s_liveModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(header, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    cornerSafeHeader(header);
    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_label_set_text(title, TR("LIVE"));
    lv_obj_center(title);

#if !UI_TOUCH_ONLY_PROFILE
    // Keyboard builds: a plain chip on the right of the centered LIVE title. Not
    // a button — F opens the picker, and a tap target with no touch panel behind
    // it would just be decoration.
    s_liveFilterHeaderLabel = lv_label_create(header);
    lv_obj_set_style_text_font(s_liveFilterHeaderLabel, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_liveFilterHeaderLabel, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_liveFilterHeaderLabel, lv_color_hex(0xA7C7FF), 0);
#endif
    lv_obj_align(s_liveFilterHeaderLabel, LV_ALIGN_RIGHT_MID, 0, 0);
#endif
    }   // header bar
#if UI_TOUCH_ONLY_PROFILE
    // Touch build: no F key, so the filter is a button -- one that says which
    // filter is in force (refreshLiveFilterHeader()) -- in the row under the
    // title. No Tools button: Tools is the screen you arrive from and carries a
    // Live row of its own, and the nav bar goes back to it.
    {
        lv_obj_t *actions = toolActionRow(s_liveModal);
        s_liveFilterHeaderLabel = toolActionBtn(
            actions, TR("Filter"),
            [](lv_event_t *e) { LV_UNUSED(e); openLiveFilterModal(); });
    }
#endif
    refreshLiveFilterHeader();

    s_liveList = lv_obj_create(s_liveModal);
    lv_obj_set_width(s_liveList, lv_pct(100));
    lv_obj_set_flex_grow(s_liveList, 1);
    lv_obj_add_flag(s_liveList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_liveList);
    lv_obj_set_scrollbar_mode(s_liveList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_liveList, liveListBackdropColor(), 0);
    lv_obj_set_style_bg_opa(s_liveList, liveListBackdropOpa(), 0);
    lv_obj_set_style_border_width(s_liveList, 1, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_border_color(s_liveList, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_border_color(s_liveList, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_pad_all(s_liveList, 0, 0);
    lv_obj_set_style_pad_right(s_liveList, 4, 0);
    lv_obj_set_style_pad_row(s_liveList, 1, 0);
    lv_obj_set_style_width(s_liveList, 2, LV_PART_SCROLLBAR);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_liveList, lv_color_make(0, 0, 0), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_liveList, LV_OPA_COVER, LV_PART_SCROLLBAR);
#else
    lv_obj_set_style_bg_color(s_liveList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_liveList, LV_OPA_70, LV_PART_SCROLLBAR);
#endif
    lv_obj_set_style_radius(s_liveList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_liveList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_liveList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

#if UI_TOUCH_ONLY_PROFILE
    // Tools lit: Live is one of its screens, as every other it opens.
    appendHeltecBottomNav(s_liveModal, HELTEC_NAV_TOOLS);
#else
    lv_obj_t *hint = lv_label_create(s_liveModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if UI_CARDPUTER_COMPACT
    // No Back tooltip here. Esc leaves every modal on this build, and carrying
    // it made the legend 239 px wide against 230 px of content — it wrapped to
    // a second line and took 11 px off the feed. The rest fits on one line.
    lv_label_set_text(hint, TR("C = Clear   F = Filter"));
#else
    lv_label_set_text_fmt(hint, TR("%s = Back   C = Clear   F = Filter"),
                          modalCloseKeyLabel());
#endif
#endif

#if HAS_NAV_BAR_TOGGLE
    // Keyboard board with the bar switched on: it joins the key hints above
    // rather than replacing them — the hints name the keys this screen answers
    // to, which six navigation buttons say nothing about. Appended after the
    // hint so the bar's layout spacer is the last child and the hint is not
    // pushed under it. A no-op with the bar switched off, which is what leaves
    // this screen exactly as it looked before the bar existed.
    appendHeltecBottomNav(s_liveModal, HELTEC_NAV_TOOLS);
#endif

    refreshLiveView(true);
}

// ── Live Tools modal ─────────────────────────────────────────────────────────
static void refreshLiveToolsSelection() {
    paintPickerRows(s_liveToolsRows, LIVE_TOOL_COUNT, s_liveToolsSelection, PICKER_SCROLL_NONE);
#if defined(DEVICE_TDECK_PRO)
    // A hairline round every other tool, so the grid reads as buttons rather
    // than loose words. The picker rows elsewhere in Config stay bare; this is
    // a grid of things to press, not a list to choose one line from.
    for (int i = 0; i < LIVE_TOOL_COUNT; i++) {
        if (s_liveToolsRows[i] && i != s_liveToolsSelection) {
            lv_obj_set_style_border_width(s_liveToolsRows[i], 1, 0);
        }
    }
#endif
}

// MQTT needs a broker and a broker needs WiFi, so with the master switch off
// that row is dead. It stays on the grid rather than vanishing — a tool that
// comes and goes with a setting is harder to find than one you can see is
// unavailable — and says why in its own label.
static bool liveToolEnabled(int tool) {
#if FEATURE_MQTT_MONITOR
    if (tool == LIVE_TOOL_MQTT) return s_cfg.wifiEnabled;
#endif
#if HAS_WEATHER
    // Same rule as MQTT: the row stays on the grid and says why it is dead,
    // rather than coming and going with a setting.
    if (tool == LIVE_TOOL_WEATHER) return s_cfg.wifiEnabled;
#endif
    LV_UNUSED(tool);
    return true;
}

// One press, both announcements — the pair that used to be "Send NODEINFO
// Broadcast" and "Send Telemetry Now" at the bottom of Config. They are
// together here because they were never really separate in use: telling the
// mesh who you are and what your battery is doing is one act of introducing
// yourself, and anyone who pressed one went on to press the other.
//
// Both calls are kept rather than collapsed to the announce alone, which does
// already nudge telemetry along (see the forceAnnounce block in
// serviceNodeAnnounce). That nudge is conditional: it only clears the telemetry
// timer when telDeviceEnabled is set, whereas webCfgQueueTelemetry() forces a
// device packet whichever way that setting is left. Dropping it would quietly
// make this button do less than the old Config row did on exactly the builds
// that had periodic telemetry turned off.
static constexpr uint32_t kToolAnnounceCooldownMs = 30000;
static uint32_t s_toolAnnounceNextMs = 0;

// Yes on the confirm below. The cooldown is armed here, on the answer, not on
// the press that raised the dialog: a press that was then declined sent
// nothing, and should not cost the next one thirty seconds.
static void liveToolsAnnounceCommit(int) {
    webCfgQueueAnnounce();
    webCfgQueueTelemetry();
    // Armed on the answer, not on the transmission. The main loop may hold both
    // for a radio that is not ready yet, and a user who cannot see that should
    // not be able to stack up queued announcements by pressing again.
    s_toolAnnounceNextMs = millis() + kToolAnnounceCooldownMs;
    openCfgActionMessageModal(TR("NODEINFO + telemetry queued."));
}

// Deliberately not a disabled row: the grid is built once when Tools opens, so
// a row greyed out on that basis would still claim to be unavailable thirty
// seconds later when it was not. Answering the press with the time left is
// honest at the moment it is read, and needs nothing repainting.
//
// Asks before sending. Announce sits on the grid among tools that only open a
// screen, so a stray Enter or tap on it put two broadcasts on the air with no
// way back. The cooldown is checked first, so a press that would be refused
// anyway says so without a dialog in front of it.
static void liveToolsAnnounceNow() {
    const uint32_t now = millis();
    // Signed difference, so this stays correct across the millis() wrap — and
    // it needs no "never pressed" sentinel, because a zero deadline is simply
    // one that every real now has already passed.
    if ((int32_t)(now - s_toolAnnounceNextMs) < 0) {
        const uint32_t leftMs = s_toolAnnounceNextMs - now;
        char msg[64];
        // Rounded up: a press with 200 ms left should not be answered with "0s".
        snprintf(msg, sizeof(msg), TR("Just announced. Try again in %us."),
                 (unsigned)((leftMs + 999) / 1000));
        openCfgActionMessageModal(msg);
        return;
    }

    openCfgConfirmModal(-1, TR("Send NODEINFO and telemetry to the mesh now?"),
                        liveToolsAnnounceCommit, 0);
}

// ── MECP composer and beacon (issue #158) ────────────────────────────────────
// Composer: one form of dropdowns -- action, severity, one or two codes, person
// count, position and time, an optional note and the channel -- sent straight
// from the form. A MAYDAY is confirmed first; sendComposeMessage() confirms one
// typed by hand the same way.
//
// Beacon: B01 with position and time, sent at once and then on the reference
// schedule (5 min, 15 min after 6 h, 30 min after 24 h) on the channel chosen
// in the form. A drill sends D01 B01 at routine severity and stops itself
// after three. Stopping sends B03 (a drill D03 B03). Received B02/B03 are only
// displayed: they are advisory on a channel, and this client does not slow down.
static bool s_mecpAttachPos = true;   // the form's starting choice

static bool     s_mecpBeaconOn = false;
static bool     s_mecpBeaconDrill = false;
static int      s_mecpBeaconChan = 0;
static uint32_t s_mecpBeaconStartMs = 0;
static uint32_t s_mecpBeaconLastMs = 0;
static uint16_t s_mecpBeaconSends = 0;
static constexpr uint16_t kMecpDrillSends = 3;

// GPS fix first, then the configured fixed position. Full precision: an
// emergency message is the one place the position-precision setting must not
// blur where help has to go, and attaching it is the user's explicit choice.
static bool mecpSelfPosition(double &lat, double &lon) {
#if HAS_GPS
    if (gpsHasFix() && (gpsLatI() != 0 || gpsLonI() != 0)) {
        lat = (double)gpsLatI() / 1e7;
        lon = (double)gpsLonI() / 1e7;
        return true;
    }
#endif
    if (s_cfg.latI != 0 || s_cfg.lonI != 0) {
        lat = (double)s_cfg.latI / 1e7;
        lon = (double)s_cfg.lonI / 1e7;
        return true;
    }
    return false;
}

// MECP timestamps are the sender's local time (MECP 1.4.0).
static void mecpAttachPosTime(mecp::Draft &d) {
    d.hasGps = mecpSelfPosition(d.lat, d.lon);
    const time_t now = time(nullptr);
    d.hasTime = (now >= kClockSetEpoch);
    if (d.hasTime) {
        struct tm lt;
        localtime_r(&now, &lt);
        d.hour = (uint8_t)lt.tm_hour;
        d.minute = (uint8_t)lt.tm_min;
    }
}

static void mecpDraftAddCode(mecp::Draft &d, const char *code) {
    if (!code || d.codeCount >= mecp::kMaxDraftCodes) return;
    for (int i = 0; i < d.codeCount; i++)
        if (strncmp(d.codes[i], code, 3) == 0) return;
    snprintf(d.codes[d.codeCount++], sizeof(d.codes[0]), "%s", code);
}

static bool mecpSendOnChannel(int chan, const mecp::Draft &d, const char *note = "") {
    char wire[mecp::kMaxWireBytes + 1];
    if (!mecp::encode(d, note ? note : "", i18nGetLang(), wire, sizeof(wire))) return false;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) return false;
    const bool ok = Channels.sendText(s_myNodeId, wire, s_cfg.okToMqtt, chan, 0);
    if (!ok) Channels.addMessage(chan, "", TR("! TX failed"), TFT_RED, 0);
    refreshChatView(true);
    return ok;
}

static void mecpBeaconSendOnce() {
    mecp::Draft d = {};
    d.severity = s_mecpBeaconDrill ? mecp::SEV_ROUTINE : mecp::SEV_MAYDAY;
    if (s_mecpBeaconDrill) mecpDraftAddCode(d, "D01");
    mecpDraftAddCode(d, "B01");
    mecpAttachPosTime(d);
    mecpSendOnChannel(s_mecpBeaconChan, d);
    s_mecpBeaconSends++;
    s_mecpBeaconLastMs = millis();
}

static void mecpBeaconStart(int drill, int chan) {
    if (s_mecpBeaconOn) return;
    s_mecpBeaconOn = true;
    s_mecpBeaconDrill = (drill != 0);
    s_mecpBeaconChan = (chan >= 0 && chan < MESH_CHANNELS) ? chan : 0;
    s_mecpBeaconStartMs = millis();
    s_mecpBeaconSends = 0;
    mecpBeaconSendOnce();
}

static void mecpBeaconStop() {
    if (!s_mecpBeaconOn) return;
    s_mecpBeaconOn = false;
    mecp::Draft d = {};
    d.severity = mecp::SEV_ROUTINE;
    if (s_mecpBeaconDrill) mecpDraftAddCode(d, "D03");
    mecpDraftAddCode(d, "B03");
    mecpSendOnChannel(s_mecpBeaconChan, d);
}

static void serviceMecpBeacon() {
    if (!s_mecpBeaconOn) return;
    const uint32_t now = millis();
    const uint32_t up = now - s_mecpBeaconStartMs;
    const uint32_t intervalMs = (up < 6UL * 3600000UL)  ? 5UL * 60000UL
                              : (up < 24UL * 3600000UL) ? 15UL * 60000UL
                                                        : 30UL * 60000UL;
    if (now - s_mecpBeaconLastMs < intervalMs) return;
    if (s_mecpBeaconDrill && s_mecpBeaconSends >= kMecpDrillSends) {
        mecpBeaconStop();
        return;
    }
    mecpBeaconSendOnce();
}

// ── Composer form ──
// Every choice is a dropdown on one form, the channel last and always chosen
// here rather than taken from the chat screen. Where the panel is too short for
// every row (CardputerZero, T-Deck) the rows are paged and Previous/Next walk
// them; the form is the same object either way.
//
// Keys: Up/Down move between controls; Enter opens a dropdown, Up/Down then
// change it and Enter keeps it (the close key puts it back); Left/Right change
// a dropdown without opening it where the board has them. Enter on the note
// moves on, Enter on a button presses it.
enum MecpField : uint8_t {
    MF_ACTION, MF_SEVERITY, MF_CAT1, MF_CODE1, MF_CAT2, MF_CODE2,
    MF_PEOPLE, MF_POSITION, MF_NOTE, MF_CHANNEL, MF_COUNT
};
enum MecpButton : uint8_t { MB_CANCEL, MB_PREV, MB_NEXT, MB_SEND, MB_COUNT };
enum MecpAction : uint8_t { MECP_ACT_COMPOSE, MECP_ACT_BEACON, MECP_ACT_DRILL, MECP_ACT_STOP };

static const uint16_t kMecpPeople[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 30, 50, 100 };
static constexpr int kMecpPeopleCount = (int)(sizeof(kMecpPeople) / sizeof(kMecpPeople[0]));

static lv_obj_t *s_mecpFormBackdrop = nullptr;
static lv_obj_t *s_mecpFormModal = nullptr;
static lv_obj_t *s_mecpFormTitle = nullptr;
static lv_obj_t *s_mecpFormRows[MF_COUNT] = {};
static lv_obj_t *s_mecpFormCtl[MF_COUNT] = {};
static lv_obj_t *s_mecpFormBtns[MB_COUNT] = {};
static lv_obj_t *s_mecpFormSendLbl = nullptr;
#if UI_TOUCH_ONLY_PROFILE
static lv_obj_t *s_mecpFormKeyboard = nullptr;
#endif
static uint8_t   s_mecpActions[3];           // the Action dropdown's entries
static int       s_mecpActionCount = 0;
static int8_t    s_mecpChanMap[MESH_CHANNELS];
static int       s_mecpChanCount = 0;
static int       s_mecpLastChan = 0;         // primary until the user picks one
static int       s_mecpPerPage = MF_COUNT;   // rows a page holds
static int       s_mecpPage = 0;
static int       s_mecpPageCount = 1;
static lv_obj_t *s_mecpFocusList[MF_COUNT + MB_COUNT];
static int       s_mecpFocusCount = 0;
static int       s_mecpFocus = 0;
static bool      s_mecpEditing = false;      // a dropdown is open under the keys
static uint32_t  s_mecpEditOrig = 0;

// What the confirmation answers: captured when Send is pressed, so the send
// is exactly what was confirmed.
static mecp::Draft s_mecpPendDraft = {};
static char        s_mecpPendNote[mecp::kMaxWireBytes + 1];
static int         s_mecpPendChan = 0;

static bool textFieldCaretKey(lv_obj_t *ta, char k);

static void closeMecpForm() {
    if (lvObjValid(s_mecpFormBackdrop)) lv_obj_del(s_mecpFormBackdrop);
    s_mecpFormBackdrop = nullptr;
    s_mecpFormModal = nullptr;
    s_mecpFormTitle = nullptr;
    s_mecpFormSendLbl = nullptr;
    for (int i = 0; i < MF_COUNT; i++) { s_mecpFormRows[i] = nullptr; s_mecpFormCtl[i] = nullptr; }
    for (int i = 0; i < MB_COUNT; i++) s_mecpFormBtns[i] = nullptr;
#if UI_TOUCH_ONLY_PROFILE
    s_mecpFormKeyboard = nullptr;
#endif
    s_mecpFocusCount = 0;
    s_mecpEditing = false;
}

static bool mecpFormNoteFocused() {
    return s_mecpFormModal && !s_mecpEditing && s_mecpFocus < s_mecpFocusCount
           && s_mecpFocusList[s_mecpFocus] == s_mecpFormCtl[MF_NOTE];
}

static int mecpFormSel(MecpField f) {
    lv_obj_t *dd = s_mecpFormCtl[f];
    return (lvObjValid(dd) && f != MF_NOTE) ? (int)lv_dropdown_get_selected(dd) : 0;
}

static uint8_t mecpFormAction() {
    const int i = mecpFormSel(MF_ACTION);
    return (i >= 0 && i < s_mecpActionCount) ? s_mecpActions[i] : MECP_ACT_COMPOSE;
}

static int mecpFormChannel() {
    const int i = mecpFormSel(MF_CHANNEL);
    return (i >= 0 && i < s_mecpChanCount) ? s_mecpChanMap[i] : 0;
}

static bool mecpFieldShown(int f) {
    const uint8_t act = mecpFormAction();
    if (f == MF_ACTION) return true;
    if (act == MECP_ACT_STOP) return false;
    if (act != MECP_ACT_COMPOSE) return f == MF_CHANNEL;
    if (f == MF_CODE2) return mecpFormSel(MF_CAT2) > 0;
    return true;
}

// Fills a code dropdown with the codes of one category.
static void mecpFillCodes(lv_obj_t *dd, int category) {
    if (!lvObjValid(dd)) return;
    static char opts[1600];
    opts[0] = '\0';
    const uint8_t lang = i18nGetLang();
    const int n = mecp::categoryCodeCount(category);
    for (int i = 0; i < n; i++) {
        char code[4];
        if (!mecp::categoryCode(category, i, code)) continue;
        const char *name = mecp::codeLabel(code, lang);
        char line[96];
        snprintf(line, sizeof(line), "%s%s %s", i ? "\n" : "", code, name ? name : "");
        strlcat(opts, line, sizeof(opts));
    }
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, 0);
}

static void mecpFormPaintFocus() {
    const lv_color_t accent = lvColorFrom565(s_ui.selectAccent);
    for (int i = 0; i < s_mecpFocusCount; i++) {
        lv_obj_t *o = s_mecpFocusList[i];
        if (!lvObjValid(o)) continue;
        const bool on = (i == s_mecpFocus);
        lv_obj_set_style_outline_width(o, on ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_outline_pad(o, 1, LV_PART_MAIN);
        lv_obj_set_style_outline_color(o, accent, LV_PART_MAIN);
        lv_obj_set_style_outline_opa(o, on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    }
    if (s_mecpFocus < s_mecpFocusCount && lvObjValid(s_mecpFocusList[s_mecpFocus]))
        lv_obj_scroll_to_view(s_mecpFocusList[s_mecpFocus], LV_ANIM_OFF);
}

// Shows the rows that apply to the chosen action on the current page, the
// buttons that page needs, and rebuilds the order the keys move through them.
static void mecpFormLayout() {
    if (!s_mecpFormModal) return;
    lv_obj_t *wasFocused = (s_mecpFocus < s_mecpFocusCount) ? s_mecpFocusList[s_mecpFocus] : nullptr;

    int shown[MF_COUNT];
    int n = 0;
    for (int f = 0; f < MF_COUNT; f++)
        if (mecpFieldShown(f)) shown[n++] = f;
    const int per = s_mecpPerPage > 0 ? s_mecpPerPage : 1;
    s_mecpPageCount = (n + per - 1) / per;
    if (s_mecpPageCount < 1) s_mecpPageCount = 1;
    if (s_mecpPage >= s_mecpPageCount) s_mecpPage = s_mecpPageCount - 1;
    if (s_mecpPage < 0) s_mecpPage = 0;

    for (int f = 0; f < MF_COUNT; f++)
        if (s_mecpFormRows[f]) lv_obj_add_flag(s_mecpFormRows[f], LV_OBJ_FLAG_HIDDEN);
    s_mecpFocusCount = 0;
    for (int i = s_mecpPage * per; i < n && i < (s_mecpPage + 1) * per; i++) {
        lv_obj_clear_flag(s_mecpFormRows[shown[i]], LV_OBJ_FLAG_HIDDEN);
        s_mecpFocusList[s_mecpFocusCount++] = s_mecpFormCtl[shown[i]];
    }

    const bool last = (s_mecpPage == s_mecpPageCount - 1);
    const bool vis[MB_COUNT] = { true, s_mecpPage > 0, !last, last };
    // Focus order: the forward button first, so Down from the last field
    // lands on Next or Send rather than on Cancel.
    static const int kOrder[MB_COUNT] = { MB_NEXT, MB_SEND, MB_PREV, MB_CANCEL };
    for (int b = 0; b < MB_COUNT; b++) {
        if (!s_mecpFormBtns[b]) continue;
        if (vis[b]) lv_obj_clear_flag(s_mecpFormBtns[b], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_mecpFormBtns[b], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < MB_COUNT; i++)
        if (vis[kOrder[i]] && s_mecpFormBtns[kOrder[i]])
            s_mecpFocusList[s_mecpFocusCount++] = s_mecpFormBtns[kOrder[i]];

    const uint8_t act = mecpFormAction();
    if (s_mecpFormSendLbl) {
        lv_label_set_text(s_mecpFormSendLbl,
                          act == MECP_ACT_STOP ? TR("Stop")
                          : act == MECP_ACT_COMPOSE ? TR("Send") : TR("Start"));
    }
    if (s_mecpFormTitle) {
        if (s_mecpPageCount > 1)
            lv_label_set_text_fmt(s_mecpFormTitle, "MECP  %d/%d", s_mecpPage + 1, s_mecpPageCount);
        else
            lv_label_set_text(s_mecpFormTitle, "MECP");
    }

    s_mecpFocus = 0;
    for (int i = 0; i < s_mecpFocusCount; i++)
        if (s_mecpFocusList[i] == wasFocused) { s_mecpFocus = i; break; }
    mecpFormPaintFocus();
}

static void mecpFormFieldChanged(int f) {
    if (f == MF_CAT1) mecpFillCodes(s_mecpFormCtl[MF_CODE1], mecpFormSel(MF_CAT1));
    if (f == MF_CAT2) {
        const int c = mecpFormSel(MF_CAT2);
        if (c > 0) mecpFillCodes(s_mecpFormCtl[MF_CODE2], c - 1);
    }
    if (f == MF_ACTION || f == MF_CAT2) mecpFormLayout();
}

static void onMecpFormDropdownChanged(lv_event_t *e) {
    mecpFormFieldChanged((int)(intptr_t)lv_event_get_user_data(e));
}

static void mecpFormSendCommit(int) {
    const int chan = s_mecpPendChan;
    closeMecpForm();
    s_mecpLastChan = chan;
    mecpSendOnChannel(chan, s_mecpPendDraft, s_mecpPendNote);
    setActiveChannel(chan);
}

static void mecpFormBeaconCommit(int) {
    closeMecpForm();
    s_mecpLastChan = s_mecpPendChan;
    mecpBeaconStart(0, s_mecpPendChan);
}

static void mecpFormSubmit() {
    if (!s_mecpFormModal) return;
    const uint8_t act = mecpFormAction();
    s_mecpPendChan = mecpFormChannel();
    if (act == MECP_ACT_STOP) {
        closeMecpForm();
        mecpBeaconStop();
        return;
    }
    if (act == MECP_ACT_DRILL) {
        closeMecpForm();
        s_mecpLastChan = s_mecpPendChan;
        mecpBeaconStart(1, s_mecpPendChan);
        return;
    }
    if (act == MECP_ACT_BEACON) {
        openCfgConfirmModal(-1, TR("Start a MAYDAY beacon on this channel? It repeats until stopped."),
                            mecpFormBeaconCommit, 0);
        return;
    }

    mecp::Draft &d = s_mecpPendDraft;
    d = {};
    d.severity = (uint8_t)mecpFormSel(MF_SEVERITY);
    char code[4];
    if (mecp::categoryCode(mecpFormSel(MF_CAT1), mecpFormSel(MF_CODE1), code))
        mecpDraftAddCode(d, code);
    if (mecpFormSel(MF_CAT2) > 0
        && mecp::categoryCode(mecpFormSel(MF_CAT2) - 1, mecpFormSel(MF_CODE2), code))
        mecpDraftAddCode(d, code);
    const int p = mecpFormSel(MF_PEOPLE);
    d.pax = (p >= 0 && p < kMecpPeopleCount) ? kMecpPeople[p] : 0;
    s_mecpAttachPos = (mecpFormSel(MF_POSITION) == 0);
    if (s_mecpAttachPos) mecpAttachPosTime(d);
    const char *note = lvObjValid(s_mecpFormCtl[MF_NOTE])
                           ? lv_textarea_get_text(s_mecpFormCtl[MF_NOTE]) : "";
    snprintf(s_mecpPendNote, sizeof(s_mecpPendNote), "%s", note ? note : "");

    char wire[mecp::kMaxWireBytes + 1];
    if (!mecp::encode(d, s_mecpPendNote, i18nGetLang(), wire, sizeof(wire))) {
        openCfgActionMessageModal(TR("Could not build the MECP message."), "MECP");
        return;
    }
    if (d.severity == mecp::SEV_MAYDAY) {
        openCfgConfirmModal(-1, TR("Send a MAYDAY? Use it only when life is in danger."),
                            mecpFormSendCommit, 0);
        return;
    }
    mecpFormSendCommit(0);
}

static void mecpFormGoPage(int delta) {
    s_mecpPage += delta;
    s_mecpFocus = 0;
    s_mecpFocusCount = 0;   // land on the page's first control
    mecpFormLayout();
}

static void onMecpFormButton(lv_event_t *e) {
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
        case MB_CANCEL: closeMecpForm(); break;
        case MB_PREV:   mecpFormGoPage(-1); break;
        case MB_NEXT:   mecpFormGoPage(+1); break;
        default:        mecpFormSubmit(); break;
    }
}

static void onMecpFormBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_mecpFormBackdrop) return;
    closeMecpForm();
}

#if UI_TOUCH_ONLY_PROFILE
static void onMecpNoteFocused(lv_event_t *e) {
    LV_UNUSED(e);
    if (!lvObjValid(s_mecpFormKeyboard)) return;
    lv_keyboard_set_textarea(s_mecpFormKeyboard, s_mecpFormCtl[MF_NOTE]);
    lv_obj_clear_flag(s_mecpFormKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_mecpFormModal, LV_ALIGN_TOP_MID, 0, 4);
}
static void onMecpKeyboardDone(lv_event_t *e) {
    LV_UNUSED(e);
    if (lvObjValid(s_mecpFormKeyboard)) lv_obj_add_flag(s_mecpFormKeyboard, LV_OBJ_FLAG_HIDDEN);
    if (lvObjValid(s_mecpFormModal)) lv_obj_align(s_mecpFormModal, LV_ALIGN_CENTER, 0, 0);
}
#endif

static lv_obj_t *mecpFormRow(lv_obj_t *parent, int f, const char *label) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_all(row, 2, 0);   // room for the focus outline
    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_width(lbl, lv_pct(30));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_label_set_text(lbl, label);
    s_mecpFormRows[f] = row;
    return row;
}

static lv_obj_t *mecpFormDropdown(int f, const char *label, const char *options, int sel) {
    lv_obj_t *row = mecpFormRow(s_mecpFormModal, f, label);
    lv_obj_t *dd = lv_dropdown_create(row);
    lv_obj_set_flex_grow(dd, 1);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_ver(dd, 4, 0);
    lv_dropdown_set_options(dd, options);
    if (sel > 0) lv_dropdown_set_selected(dd, (uint32_t)sel);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) lv_obj_set_style_text_font(list, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(dd, onMecpFormDropdownChanged, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)f);
    s_mecpFormCtl[f] = dd;
    return dd;
}

static void openMecpForm() {
    if (!s_rootScreen || s_mecpFormBackdrop) return;
    const uint8_t lang = i18nGetLang();

    s_mecpActionCount = 0;
    s_mecpActions[s_mecpActionCount++] = MECP_ACT_COMPOSE;
    if (s_mecpBeaconOn) {
        s_mecpActions[s_mecpActionCount++] = MECP_ACT_STOP;
    } else {
        s_mecpActions[s_mecpActionCount++] = MECP_ACT_BEACON;
        s_mecpActions[s_mecpActionCount++] = MECP_ACT_DRILL;
    }
    s_mecpChanCount = 0;
    int chanSel = 0;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (CHANNEL_KEYS[i].role == 2) continue;   // disabled
        if (i == s_mecpLastChan) chanSel = s_mecpChanCount;
        s_mecpChanMap[s_mecpChanCount++] = (int8_t)i;
    }
    if (s_mecpChanCount == 0) s_mecpChanMap[s_mecpChanCount++] = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 8;
    if (modalW > 440) modalW = 440;

    s_mecpFormBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_mecpFormBackdrop, w, h);
    lv_obj_align(s_mecpFormBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_mecpFormBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mecpFormBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_mecpFormBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_mecpFormBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_mecpFormBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_mecpFormBackdrop, 0, 0);
    lv_obj_add_event_cb(s_mecpFormBackdrop, onMecpFormBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_mecpFormModal = lv_obj_create(s_mecpFormBackdrop);
    lv_obj_set_size(s_mecpFormModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_mecpFormModal, h - 8, 0);
    lv_obj_align(s_mecpFormModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_mecpFormModal, LV_OBJ_FLAG_CLICKABLE);
    setupVScroll(s_mecpFormModal);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_mecpFormModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_mecpFormModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_mecpFormModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_mecpFormModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_mecpFormModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mecpFormModal, 1, 0);
    lv_obj_set_style_pad_all(s_mecpFormModal, 6, 0);
    lv_obj_set_style_pad_row(s_mecpFormModal, 2, 0);
    lv_obj_set_flex_flow(s_mecpFormModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_mecpFormModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_mecpFormTitle = lv_label_create(s_mecpFormModal);
    lv_obj_set_width(s_mecpFormTitle, lv_pct(100));
    lv_obj_set_style_text_font(s_mecpFormTitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_mecpFormTitle, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(s_mecpFormTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_mecpFormTitle, "MECP");

    char opts[1600];
    // Action
    opts[0] = '\0';
    for (int i = 0; i < s_mecpActionCount; i++) {
        char line[96];
        const char *name = TR("Compose message");
        if (s_mecpActions[i] == MECP_ACT_BEACON) name = TR("Start beacon");
        else if (s_mecpActions[i] == MECP_ACT_DRILL) name = TR("Beacon drill (3 sends)");
        else if (s_mecpActions[i] == MECP_ACT_STOP) {
            snprintf(line, sizeof(line), TR("Stop: %s (%u sent)"),
                     mecp::uiText(s_mecpBeaconDrill ? mecp::UI_BEACON_DRILL : mecp::UI_BEACON_ACTIVE, lang),
                     (unsigned)s_mecpBeaconSends);
            name = line;
        }
        if (i) strlcat(opts, "\n", sizeof(opts));
        strlcat(opts, name, sizeof(opts));
    }
    mecpFormDropdown(MF_ACTION, TR("Action"), opts, 0);

    // Severity: MAYDAY first, as the protocol numbers them, but resting on
    // Routine -- the form should never open on MAYDAY.
    opts[0] = '\0';
    for (int i = 0; i < mecp::kSeverityCount; i++) {
        if (i) strlcat(opts, "\n", sizeof(opts));
        strlcat(opts, mecp::severityName((uint8_t)i, lang), sizeof(opts));
    }
    mecpFormDropdown(MF_SEVERITY, TR("Severity"), opts, mecp::SEV_ROUTINE);

    char cats[640] = "";
    for (int i = 0; i < mecp::categoryCount(); i++) {
        char line[80];
        snprintf(line, sizeof(line), "%s%c  %s", i ? "\n" : "", mecp::categoryLetter(i),
                 mecp::categoryName(i, lang));
        strlcat(cats, line, sizeof(cats));
    }
    mecpFormDropdown(MF_CAT1, TR("Category"), cats, 0);
    mecpFormDropdown(MF_CODE1, TR("Code"), "", 0);
    mecpFillCodes(s_mecpFormCtl[MF_CODE1], 0);

    snprintf(opts, sizeof(opts), "%s\n%s", TR("None"), cats);
    mecpFormDropdown(MF_CAT2, TR("2nd category"), opts, 0);
    mecpFormDropdown(MF_CODE2, TR("2nd code"), "", 0);
    mecpFillCodes(s_mecpFormCtl[MF_CODE2], 0);

    snprintf(opts, sizeof(opts), "%s", TR("None"));
    for (int i = 1; i < kMecpPeopleCount; i++) {
        char line[16];
        snprintf(line, sizeof(line), "\n%u", (unsigned)kMecpPeople[i]);
        strlcat(opts, line, sizeof(opts));
    }
    mecpFormDropdown(MF_PEOPLE, mecp::uiText(mecp::UI_PERSON_COUNT, lang), opts, 0);

    snprintf(opts, sizeof(opts), "%s\n%s", TR("Attach"), TR("Don't attach"));
    mecpFormDropdown(MF_POSITION, TR("Position & time"), opts, s_mecpAttachPos ? 0 : 1);

    {
        lv_obj_t *row = mecpFormRow(s_mecpFormModal, MF_NOTE, TR("Note"));
        lv_obj_t *ta = lv_textarea_create(row);
        lv_obj_set_flex_grow(ta, 1);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, 120);
        lv_textarea_set_placeholder_text(ta, TR("Optional"));
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
        lv_obj_set_style_pad_ver(ta, 4, 0);
#if UI_TOUCH_ONLY_PROFILE
        lv_obj_add_event_cb(ta, onMecpNoteFocused, LV_EVENT_CLICKED, nullptr);
#endif
        s_mecpFormCtl[MF_NOTE] = ta;
    }

    opts[0] = '\0';
    for (int i = 0; i < s_mecpChanCount; i++) {
        const int c = s_mecpChanMap[i];
        const char *name = CHANNEL_KEYS[c].name;
        char line[48];
        snprintf(line, sizeof(line), "%s%d  %s", i ? "\n" : "", c,
                 (name && name[0]) ? name : (c == 0 ? TR("Primary") : "-"));
        strlcat(opts, line, sizeof(opts));
    }
    mecpFormDropdown(MF_CHANNEL, TR("Channel"), opts, chanSel);

    lv_obj_t *btnRow = lv_obj_create(s_mecpFormModal);
    lv_obj_remove_style_all(btnRow);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btnRow, 6, 0);
    lv_obj_set_style_pad_all(btnRow, 3, 0);
    static const char *const kBtnText[MB_COUNT] = {
        TR_NOOP("Cancel"), TR_NOOP("Previous"), TR_NOOP("Next"), TR_NOOP("Send")
    };
    for (int b = 0; b < MB_COUNT; b++) {
        lv_obj_t *btn = lv_btn_create(btnRow);
        lv_obj_set_height(btn, 24);
        lv_obj_set_width(btn, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(btn, 10, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, b == MB_SEND
                                           ? lv_color_hex(s_cfg.uiMode == UI_MODE_LIGHT ? 0xC76565 : 0x6B3030)
                                           : lv_color_hex(0x16386F), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x8FB5E6), 0);
        lv_obj_add_event_cb(btn, onMecpFormButton, LV_EVENT_CLICKED, (void *)(intptr_t)b);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
        lv_label_set_text(lbl, TR(kBtnText[b]));
        lv_obj_center(lbl);
        s_mecpFormBtns[b] = btn;
        if (b == MB_SEND) s_mecpFormSendLbl = lbl;
    }

#if UI_TOUCH_ONLY_PROFILE
    s_mecpFormKeyboard = lv_keyboard_create(s_mecpFormBackdrop);
    configureOnScreenKeyboard(s_mecpFormKeyboard);
    lv_obj_set_size(s_mecpFormKeyboard, w, h / 2);
    lv_obj_align(s_mecpFormKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_mecpFormKeyboard, onMecpKeyboardDone, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_mecpFormKeyboard, onMecpKeyboardDone, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_flag(s_mecpFormKeyboard, LV_OBJ_FLAG_HIDDEN);
#endif

    // Page size from what was actually laid out: everything that is not a
    // field row is chrome, and what is left of the height holds the rows.
    lv_obj_move_foreground(s_mecpFormBackdrop);
    lv_obj_update_layout(s_mecpFormModal);
    const int gap = 2;
    int rowH = 0;
    for (int f = 0; f < MF_COUNT; f++) {
        const int rh = lv_obj_get_height(s_mecpFormRows[f]);
        if (rh > rowH) rowH = rh;
    }
    const int chrome = 12 + 2 /*border*/ + lv_obj_get_height(s_mecpFormTitle) + gap
                       + lv_obj_get_height(btnRow) + gap;
    const int avail = (h - 8) - chrome;
    s_mecpPerPage = (rowH > 0) ? avail / (rowH + gap) : MF_COUNT;
    if (s_mecpPerPage < 1) s_mecpPerPage = 1;
    if (s_mecpPerPage > MF_COUNT) s_mecpPerPage = MF_COUNT;

    s_mecpPage = 0;
    s_mecpFocus = 0;
    s_mecpFocusCount = 0;
    s_mecpEditing = false;
    mecpFormLayout();
}

static void mecpFormStepDropdown(lv_obj_t *dd, int delta) {
    if (!lvObjValid(dd) || !delta) return;
    const int n = (int)lv_dropdown_get_option_count(dd);
    int next = (int)lv_dropdown_get_selected(dd) + delta;
    if (next < 0) next = 0;
    if (next > n - 1) next = n - 1;
    if (next == (int)lv_dropdown_get_selected(dd)) return;
    lv_dropdown_set_selected(dd, (uint32_t)next);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) lv_obj_invalidate(list);
}

static int mecpFieldOf(lv_obj_t *o) {
    for (int f = 0; f < MF_COUNT; f++) if (s_mecpFormCtl[f] == o) return f;
    return -1;
}

// The form's keys. Always consumes: nothing may reach the screen beneath.
static void mecpFormHandleKey(char k, bool invertScrollNav) {
    if (!s_mecpFormModal || s_mecpFocusCount == 0) return;
    lv_obj_t *cur = s_mecpFocusList[s_mecpFocus];
    const int field = mecpFieldOf(cur);
    int vdelta = 0;
    if (k == KEY_SCROLL_UP)      vdelta = invertScrollNav ? 1 : -1;
    else if (k == KEY_SCROLL_DN) vdelta = invertScrollNav ? -1 : 1;
    int hdelta = 0;
    if (k == KEY_PREV_CHAN || k == KEY_PAGE_DN)      hdelta = -1;
    else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_UP) hdelta = 1;

    if (s_mecpEditing) {
        if (k == KEY_ENTER || k == KEY_ROLLER) {
            lv_dropdown_close(cur);
            s_mecpEditing = false;
            if ((uint32_t)lv_dropdown_get_selected(cur) != s_mecpEditOrig) mecpFormFieldChanged(field);
            return;
        }
        if (isModalCloseKey(k)) {
            lv_dropdown_set_selected(cur, s_mecpEditOrig);
            lv_dropdown_close(cur);
            s_mecpEditing = false;
            return;
        }
        mecpFormStepDropdown(cur, vdelta ? vdelta : hdelta);
        return;
    }

    if (field == MF_NOTE) {
        if (k == KEY_ENTER || k == KEY_TAB || vdelta) {
            const int next = s_mecpFocus + (vdelta < 0 ? -1 : 1);
            if (next >= 0 && next < s_mecpFocusCount) { s_mecpFocus = next; mecpFormPaintFocus(); }
            return;
        }
        if (textFieldCaretKey(cur, k)) return;
        if (isBackspaceKey(k)) {
            const char *t = lv_textarea_get_text(cur);
            if (k != KEY_BACKSPACE) {
                closeMecpForm();   // a hold, or the M9's Back button
            } else if (t && t[0]) {
                lv_textarea_delete_char(cur);
            } else if (s_mecpFocus > 0) {
                s_mecpFocus--;     // empty: step back, as the name fields do
                mecpFormPaintFocus();
            }
            return;
        }
        if (k == KEY_ESCAPE) { closeMecpForm(); return; }
        if (k >= 0x20 && k < 0x7F) {
            char one[2] = { k, '\0' };
            lv_textarea_add_text(cur, one);
        }
        return;
    }

    if (isModalCloseKey(k)) {
        closeMecpForm();
        return;
    }
    if (vdelta || k == KEY_TAB) {
        int next = s_mecpFocus + (vdelta ? vdelta : 1);
        if (next < 0) next = 0;
        if (next >= s_mecpFocusCount) next = s_mecpFocusCount - 1;
        if (next != s_mecpFocus) { s_mecpFocus = next; mecpFormPaintFocus(); }
        return;
    }
    if (field >= 0) {
        if (hdelta) {
            mecpFormStepDropdown(cur, hdelta);
            mecpFormFieldChanged(field);
            return;
        }
        if (k == KEY_ENTER || k == KEY_ROLLER) {
            s_mecpEditOrig = lv_dropdown_get_selected(cur);
            s_mecpEditing = true;
            lv_dropdown_open(cur);
        }
        return;
    }
    // A button. Left/Right follow the row as drawn, not the focus order.
    if (hdelta) {
        int b = 0;
        while (b < MB_COUNT && s_mecpFormBtns[b] != cur) b++;
        for (b += hdelta; b >= 0 && b < MB_COUNT; b += hdelta) {
            if (!s_mecpFormBtns[b] || lv_obj_has_flag(s_mecpFormBtns[b], LV_OBJ_FLAG_HIDDEN)) continue;
            for (int i = 0; i < s_mecpFocusCount; i++)
                if (s_mecpFocusList[i] == s_mecpFormBtns[b]) s_mecpFocus = i;
            mecpFormPaintFocus();
            break;
        }
        return;
    }
    if (k == KEY_ENTER || k == KEY_ROLLER) {
        if (cur == s_mecpFormBtns[MB_CANCEL])      closeMecpForm();
        else if (cur == s_mecpFormBtns[MB_PREV])   mecpFormGoPage(-1);
        else if (cur == s_mecpFormBtns[MB_NEXT])   mecpFormGoPage(+1);
        else                                       mecpFormSubmit();
    }
}

// Opening a tool drops Tools rather than stacking it underneath, so backing out
// of a chart lands on whatever Tools was opened over: Live when it was opened
// from there, the chat screen when it was reached from the nav bar or L.
static void liveToolsActivate(int tool) {
    if (tool < 0 || tool >= LIVE_TOOL_COUNT) return;
    if (!liveToolEnabled(tool)) return;
    closeLiveToolsModal();
    switch (tool) {
        // Live is a destination like any other tool, so it closes Tools on
        // the way in exactly as the charts do. openLiveModal() no-ops when the
        // feed is already up, which is what makes this safe when Tools was
        // opened from Live in the first place.
        case LIVE_TOOL_LIVE:      openLiveModal();         break;
        case LIVE_TOOL_SNR:       openSnrRssiChartModal(); break;
        case LIVE_TOOL_CHUTIL:    openChUtilChartModal();  break;
#if FEATURE_DISCOVERY
        case LIVE_TOOL_DISCOVERY: openDiscoveryModal();    break;
#endif
        case LIVE_TOOL_BEACONS:   openBeaconsModal();      break;
#if HAS_WEATHER
        case LIVE_TOOL_WEATHER:   openWeatherModal();      break;
#endif
#if FEATURE_MQTT_MONITOR
        case LIVE_TOOL_MQTT:      openMqttMonitorModal();  break;
#endif
        // Transmits rather than opening anything. Tools is already closed by
        // the time this runs, so the popup it raises stands on whatever Tools
        // was opened over, and dismissing it lands there.
        case LIVE_TOOL_MECP:      openMecpForm();          break;
        case LIVE_TOOL_ANNOUNCE:  liveToolsAnnounceNow();  break;
        default: break;
    }
}

static void onLiveToolRowPressed(lv_event_t *e) {
    liveToolsActivate((int)(intptr_t)lv_event_get_user_data(e));
}

static void openLiveToolsModal() {
    // Deliberately no s_liveModal requirement any more. Tools is its own
    // destination now — the nav bar and the L key come straight here — and every
    // surface it opens already stands alone, needing nothing but the root
    // screen. Live went from being the way in to being the first row.
    if (!s_rootScreen) return;
    if (s_liveToolsModal) closeLiveToolsModal();

    s_liveToolsSelection = LIVE_TOOL_LIVE;

    // Same guard openLiveModal() takes, and for the same reason: Tools now
    // builds a nav bar of its own on top of the grid, so it allocates more than
    // it used to, and LV_ASSERT_MALLOC resets the device rather than failing.
    // The close above has already freed anything Tools itself had open.
    {
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 6144) {
            logLvglMemDiag("openLiveToolsModal aborted (low LVGL mem)");
            return;
        }
    }

    const int modalW = lv_disp_get_hor_res(NULL);
    const int modalH = lv_disp_get_ver_res(NULL);

    // A screen, not a popup (issue #94). Tools owns a cell on the nav bar, so it
    // has to be the kind of thing a cell leads to: full bleed, its own copy of
    // the bar, its own cell lit, every other cell still one tap away. It used to
    // be a centered card on a 50%-black backdrop, which dimmed the bar it should
    // have been lighting and swallowed taps aimed at it — a tap on Nodes closed
    // Tools instead of opening Nodes.
    //
    // pad_all 4 is not cosmetic here: appendHeltecBottomNav() measures its
    // offsets against it (and the border, which this screen does without) to put
    // the bar on the display edges. See the padInset/borderInset pair there.
    //
    // Losing tap-outside-to-close goes with the backdrop and is the point rather
    // than a regression: Tools now closes the three ways its neighbours do — the
    // corner X, the close key, Home — plus a second tap on its own cell.
    s_liveToolsModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_liveToolsModal, modalW, modalH);
    lv_obj_align(s_liveToolsModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_liveToolsModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_liveToolsModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_liveToolsModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_liveToolsModal, LV_OPA_COVER, 0);
    // No border: this screen fills the display, and the 1 px frame only took
    // room from it. The headers and nav bar read the width, so they follow.
    lv_obj_set_style_border_width(s_liveToolsModal, 0, 0);
    // 4, not kChanModalPad. appendHeltecBottomNav() cancels exactly pad 4 plus
    // the border to put its bar on the display edges, and kChanModalPad is 3 on
    // the Cardputer and 8 on the Pager and the 320 px boards — either would
    // leave the bar inset or overhanging by the difference. The grid does not
    // care: it is a fixed-width centered block, so the panel's own padding no
    // longer decides its width.
    lv_obj_set_style_pad_all(s_liveToolsModal, 4, 0);
    lv_obj_set_style_pad_row(s_liveToolsModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_liveToolsModal, LV_FLEX_FLOW_COLUMN);

    // P4 portrait: on a tall panel a grid floating mid-screen read as a popup.
    // There the tools run from the top and stack one per row, as
    // full-width buttons with room between them, two font sizes up.
#if defined(DEVICE_TDISPLAY_P4)
    const bool toolsStacked = uiPortrait();
#else
    constexpr bool toolsStacked = false;
#endif
    // The tools straight under the header on both portrait boards; the Heltec
    // keeps its two-column grid there, the height not being there for a list
    // of eight.
    const bool toolsTop = toolStackLayout();
    // P4 landscape: the same two-column grid, but filling the room under the
    // header rather than the 300 px block sized for 320x240 panels, which left
    // most of this panel empty. Row heights are set once the layout is known,
    // at the end; the font and gaps grow with them.
#if defined(DEVICE_TDISPLAY_P4)
    const bool toolsFill = !toolsStacked;
    const lv_font_t *toolsFillFont =
        UI_LARGE_PANEL_PROFILE ? &lv_font_montserrat_28 : &lv_font_montserrat_16;
    const int toolsFillGap = UI_LARGE_PANEL_PROFILE ? 20 : 10;
#elif defined(DEVICE_CROWPANEL_50)
    // As P4 landscape: the grid takes the whole screen under the header rather
    // than the 300 px block sized for a 320x240 panel, in either orientation.
    // Named 20, drawn as 24 (hal/crowpanel_50_fonts.h).
    constexpr bool toolsFill = true;
    const lv_font_t *toolsFillFont = &lv_font_montserrat_20;
    constexpr int toolsFillGap = 12;
#elif defined(DEVICE_CROWPANEL_35)
    // The same on the 3.5", which was drawing the 320x240 block: a strip of
    // small buttons across the middle of 480x320, and a third of 320x480.
    constexpr bool toolsFill = true;
    const lv_font_t *toolsFillFont = &lv_font_montserrat_16;
    constexpr int toolsFillGap = 8;
#else
    constexpr bool toolsFill = false;
    const lv_font_t *toolsFillFont = kChanModalRowFont;
    constexpr int toolsFillGap = kChanModalGap;
#endif
    const lv_font_t *toolsRowFont   = toolsStacked ? &lv_font_montserrat_14
                                    : toolsFill    ? toolsFillFont : kChanModalRowFont;
    const int toolsRowH   = toolsStacked ? 44 : kChanModalRowH;
    const int toolsRowGap = toolsStacked ? 10 : toolsFill ? toolsFillGap : kChanModalGap;
    lv_obj_set_flex_align(s_liveToolsModal, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // The panel went full bleed; the grid did not. This is the content width the
    // cells were laid out against and the one people have learned the positions
    // in, so it stays a centered block rather than spreading to fill a 480 px
    // Pager. kChanModalCellPct below is a percentage of this, not of the panel.
    const int toolsContentW =
        min(kChanModalMaxW, (int)lv_disp_get_hor_res(NULL) - 14) - 2 * kChanModalPad;

    // The chat screen's header, named for this screen, as on DMs, Nodes and
    // Config.
    buildScreenHeader(s_liveToolsModal, TR("Tools"));

    // Everything under the header. Upright the list runs from the top; in
    // landscape the grid stays centred in the room left between the header and
    // the nav bar, where it has always floated.
    lv_obj_t *body = lv_obj_create(s_liveToolsModal);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(body, kChanModalGap, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body,
                          toolsTop ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

#if UI_TOUCH_ONLY_PROFILE
    static const char *kToolLabels[LIVE_TOOL_COUNT] = {
        TR_NOOP("Live"), TR_NOOP("SNR/RSSI"), TR_NOOP("ChUtil"),
#if FEATURE_DISCOVERY
        TR_NOOP("Discovery"),
#endif
        TR_NOOP("Beacons"),
#if HAS_WEATHER
        TR_NOOP("Weather"),
#endif
#if FEATURE_MQTT_MONITOR
        TR_NOOP("MQTT"),
#endif
        TR_NOOP("MECP"),
        TR_NOOP("Announce"),
    };
#else
#if !defined(DEVICE_TDECK_PRO)
    // Not on the T-Deck Pro: the (L)etters on the buttons already say what
    // the keys do, and the line only took height from the grid.
    lv_obj_t *hint = lv_label_create(body);
    lv_obj_set_width(hint, toolsContentW);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("Move  Enter=Open  %s=Back"), modalCloseKeyLabel());
#endif

    static const char *kToolLabels[LIVE_TOOL_COUNT] = {
        TR_NOOP("(L)ive"), TR_NOOP("(S)NR/RSSI"), TR_NOOP("Ch(U)til"),
#if FEATURE_DISCOVERY
        TR_NOOP("(D)iscovery"),
#endif
        TR_NOOP("(B)eacons"),
#if HAS_WEATHER
        TR_NOOP("(W)eather"),
#endif
#if FEATURE_MQTT_MONITOR
        TR_NOOP("(M)QTT"),
#endif
        TR_NOOP("M(E)CP"),
        TR_NOOP("(A)nnounce"),
    };
#endif

    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_set_width(grid, toolsContentW);
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, toolsRowGap, 0);
    lv_obj_set_style_pad_column(grid, toolsFill ? toolsFillGap : kChanModalGap, 0);
    lv_obj_set_flex_flow(grid, toolsStacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    for (int pos = 0; pos < LIVE_TOOL_COUNT; pos++) {
        // Column-major, as in the channel grids: left column first, then right.
        // Stacked, there is one column and the list order is the order.
        const int i = toolsStacked
                          ? pos
                          : (pos % kChanModalCols) * kLiveToolRowsPerCol + (pos / kChanModalCols);
        if (i >= LIVE_TOOL_COUNT) continue;

        lv_obj_t *row = lv_btn_create(grid);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_liveToolsRows[i] = row;
        lv_obj_set_width(row, lv_pct(toolsStacked ? 100 : kChanModalCellPct));
        lv_obj_set_height(row, toolsRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 5, 0);
        lv_obj_set_style_pad_right(row, 5, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);

        // Nothing to enable or disable while this modal is up: the settings
        // behind liveToolEnabled() are only reachable from screens that cannot
        // be open at the same time, so this is read once, here.
        const bool enabled = liveToolEnabled(i);
        if (enabled) {
            lv_obj_add_event_cb(row, onLiveToolRowPressed, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
        } else {
            lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, toolsRowFont, 0);
        lv_obj_set_style_text_color(lbl, enabled ? rowTextColor : lv_color_hex(0x7590BE), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
#if FEATURE_MQTT_MONITOR
        if (!enabled && i == LIVE_TOOL_MQTT) {
            lv_label_set_text_fmt(lbl, TR("%s - WiFi off"), kToolLabels[i]);
        } else
#endif
        {
            lv_label_set_text(lbl, TR(kToolLabels[i]));
        }
        lv_obj_center(lbl);
    }

    // No X on any build: the nav bar is the way out (its Tools cell closes
    // this, any other cell goes there), as on every screen listed here.

    // Last, so the bar's layout spacer is the final child of the flex column and
    // the grid above is not pushed under the bar. Same ordering rule Nodes, DM
    // and Config follow, and the reason it is spelled out on each of them.
    //
    // A no-op where the bar is switched off, which is what leaves Tools looking
    // as it does today on a keyboard board with no bar — minus the backdrop.
    appendHeltecBottomNav(s_liveToolsModal, HELTEC_NAV_TOOLS);

    if (toolsFill) {
        // Measured after the nav bar is in, so the room it takes -- along the
        // bottom or down the side column -- is already gone from the body.
        // A gap's margin on every side keeps the cells off the header, the
        // bar and the panel's rounded corners.
        lv_obj_update_layout(s_liveToolsModal);
        const int fillW = (int)lv_obj_get_content_width(body) - 2 * toolsFillGap;
        const int fillH = (int)lv_obj_get_content_height(body) - 2 * toolsFillGap;
        int rowH = (fillH - (kLiveToolRowsPerCol - 1) * toolsRowGap) / kLiveToolRowsPerCol;
        if (rowH < toolsRowH) rowH = toolsRowH;
        const int gridW = (fillW > toolsContentW) ? fillW : toolsContentW;
        if (fillW > toolsContentW) lv_obj_set_width(grid, fillW);
        // Cells in pixels, not kChanModalCellPct: two 49% cells and this gap
        // only fit side by side where 2% of the grid covers the gap -- 600 px
        // at a 12 px gap. Narrower (the CrowPanel 5" upright, 448 px) each cell
        // wrapped onto a row of its own, and the grid became one column.
        const int cellW = (gridW - toolsFillGap * (kChanModalCols - 1)) / kChanModalCols;
        for (int i = 0; i < LIVE_TOOL_COUNT; i++) {
            if (!s_liveToolsRows[i]) continue;
            lv_obj_set_height(s_liveToolsRows[i], rowH);
            if (!toolsStacked) lv_obj_set_width(s_liveToolsRows[i], cellW);
        }
    }

    refreshLiveToolsSelection();
}

// ── Live filter picker ───────────────────────────────────────────────────────
static void refreshLiveFilterSelection() {
    // Ten rows do not fit the shortest panel, so the grid scrolls; keeping
    // the selection in view is what makes arrowing past the fold work.
    paintPickerRows(s_liveFilterRows, LIVE_FILTER_COUNT, s_liveFilterSelection, PICKER_SCROLL_VIEW);
    if (lvObjValid(s_liveFilterNoMqttRow)) {
        paintPickerRow(s_liveFilterNoMqttRow, s_liveFilterSelection == kLiveFilterNoMqttSel);
    }
}

static void liveFilterNoMqttRowText(char *out, size_t outLen) {
    // Worded as what is shown, so the default reads On: the row says whether
    // MQTT traffic is in the feed, and s_liveNoMqtt is its inverse.
    snprintf(out, outLen, "%s: %s", TR("Include MQTT Live Data"),
             s_liveNoMqtt ? TR("Off") : TR("On"));
}

// The chip in the Live header. Rewritten in place rather than rebuilt, so
// changing the filter does not disturb the header layout.
static void refreshLiveFilterHeader() {
    if (!lvObjValid(s_liveFilterHeaderLabel)) {
        s_liveFilterHeaderLabel = nullptr;
        return;
    }
    // "No MQTT:" stands in for "Filter:" rather than adding to it, so the chip
    // stays about the width it was.
    lv_label_set_text_fmt(s_liveFilterHeaderLabel, s_liveNoMqtt ? TR("No MQTT: %s") : TR("Filter: %s"),
                          liveFilterName(s_liveFilter));
}

// Flips the No MQTT filter in place. Unlike picking a kind it leaves the picker
// open, since the point is to combine it with one.
static void liveFilterToggleNoMqtt() {
    s_liveNoMqtt = !s_liveNoMqtt;
    refreshLiveFilterHeader();
    if (lvObjValid(s_liveFilterNoMqttRow)) {
        lv_obj_t *lbl = lv_obj_get_child(s_liveFilterNoMqttRow, 0);
        if (lbl) {
            char text[64];
            liveFilterNoMqttRowText(text, sizeof(text));
            lv_label_set_text(lbl, text);
        }
    }
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
    refreshLiveView(true);
}

static void liveFilterApply(int filter) {
    if (filter < 0 || filter >= LIVE_FILTER_COUNT) return;
    closeLiveFilterModal();
    if ((uint8_t)filter == s_liveFilter) return;

    s_liveFilter = (uint8_t)filter;
    refreshLiveFilterHeader();
    // refreshLiveView() early-returns when the line count and scroll offset are
    // unchanged, and neither moves when only the filter does. Clearing both is
    // what forces the rebuild.
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
    refreshLiveView(true);
}

static void onLiveFilterRowPressed(lv_event_t *e) {
    liveFilterApply((int)(intptr_t)lv_event_get_user_data(e));
}

static void onLiveFilterBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_liveFilterBackdrop) return;
    closeLiveFilterModal();
}

static void openLiveFilterModal() {
    if (!s_rootScreen || !s_liveModal) return;
    if (s_liveFilterModal) closeLiveFilterModal();

    // Opens on the filter that is in force, so Enter is a no-op confirmation
    // rather than a surprise.
    s_liveFilterSelection = s_liveFilter;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    const int modalW = min(kChanModalMaxW, w - 14);

    s_liveFilterBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_liveFilterBackdrop, w, h);
    lv_obj_align(s_liveFilterBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_liveFilterBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_liveFilterBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_liveFilterBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_liveFilterBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_liveFilterBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_liveFilterBackdrop, 0, 0);
    lv_obj_add_event_cb(s_liveFilterBackdrop, onLiveFilterBackdropPressed,
                        LV_EVENT_CLICKED, nullptr);

    s_liveFilterModal = lv_obj_create(s_liveFilterBackdrop);
    lv_obj_set_size(s_liveFilterModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_liveFilterModal,
                                (h > 40) ? (h - 2 * kChanModalPad) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_liveFilterModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_liveFilterModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_liveFilterModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_liveFilterModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_liveFilterModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_liveFilterModal, 1, 0);
    lv_obj_set_style_border_color(s_liveFilterModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_liveFilterModal, kChanModalPad, 0);
    lv_obj_set_style_pad_row(s_liveFilterModal, kChanModalGap, 0);
    lv_obj_set_flex_flow(s_liveFilterModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_liveFilterModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_liveFilterBackdrop);

    lv_obj_t *title = lv_label_create(s_liveFilterModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, kChanModalTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Live Filter"));

#if !UI_TOUCH_ONLY_PROFILE
    lv_obj_t *hint = lv_label_create(s_liveFilterModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("Move  Enter=Apply  M=MQTT  %s=Back"), modalCloseKeyLabel());
#endif

    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    // The No MQTT toggle: full width above the kind grid, because it combines
    // with whichever kind is picked rather than being one of them.
    {
        lv_obj_t *row = lv_btn_create(s_liveFilterModal);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_liveFilterNoMqttRow = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, kChanModalRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 5, 0);
        lv_obj_set_style_pad_right(row, 5, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, [](lv_event_t *e) { LV_UNUSED(e); liveFilterToggleNoMqtt(); },
                            LV_EVENT_CLICKED, nullptr);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(lbl, rowTextColor, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        char text[64];
        liveFilterNoMqttRowText(text, sizeof(text));
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
    }

    lv_obj_t *grid = lv_obj_create(s_liveFilterModal);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    // Ten rows overflow the 240x135 panel even in two columns. Rather than
    // clipping the tail (which would make the last filters unreachable there),
    // the grid scrolls once it runs out of room and sits at content height
    // everywhere else — on every panel above the Cardputer's, all five rows fit
    // and no scrollbar appears.
    {
        // Title, the hint below it on keyboard builds, and the paddings
        // between them; the rest of the panel is the grid's to use. The touch
        // build has neither hint nor footer — its X floats over the corner — so
        // the grid gets that height back.
        // Plus the No MQTT row above the grid.
        constexpr int kFilterChromeH = (UI_TOUCH_ONLY_PROFILE ? 30 : 52)
                                       + kChanModalRowH + kChanModalGap;
        int gridMaxH = h - 2 * kChanModalPad - kFilterChromeH;
        const int minGridH = 2 * (kChanModalRowH + kChanModalGap);
        if (gridMaxH < minGridH) gridMaxH = minGridH;
        lv_obj_set_style_max_height(grid, gridMaxH, 0);
    }
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(grid);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, kChanModalGap, 0);
    lv_obj_set_style_pad_column(grid, kChanModalGap, 0);
    lv_obj_set_style_width(grid, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(grid, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    for (int pos = 0; pos < LIVE_FILTER_COUNT; pos++) {
        // Column-major, matching Tools and the channel grids, so a step of one
        // walks down a column rather than across.
        const int i = (pos % kChanModalCols) * kLiveFilterRowsPerCol + (pos / kChanModalCols);
        if (i >= LIVE_FILTER_COUNT) continue;

        lv_obj_t *row = lv_btn_create(grid);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_liveFilterRows[i] = row;
        lv_obj_set_width(row, lv_pct(kChanModalCellPct));
        lv_obj_set_height(row, kChanModalRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 5, 0);
        lv_obj_set_style_pad_right(row, 5, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onLiveFilterRowPressed, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, kChanModalRowFont, 0);
        lv_obj_set_style_text_color(lbl, rowTextColor, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_label_set_text(lbl, liveFilterName(i));
        lv_obj_center(lbl);
    }

#if UI_TOUCH_ONLY_PROFILE
    // No close key on this build, so the way out has to be on screen — same
    // treatment the Tools modal gets.
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_liveFilterModal,
                       [](lv_event_t *e) { LV_UNUSED(e); closeLiveFilterModal(); });
#endif

    refreshLiveFilterSelection();
}

