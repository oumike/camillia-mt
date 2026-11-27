// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: tool screens, key-change toast, direct messages.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void closeToolScreens(bool closeLive) {
    // Each guarded on its own screen being up, so a nav tap from anywhere else
    // runs none of their side effects (Discovery's brings a preset scan home,
    // Weather's resets the fetch state).
    if (s_snrChartModal) closeSnrRssiChartModal();
    if (s_chUtilChartModal) closeChUtilChartModal();
#if FEATURE_DISCOVERY
    if (s_discoveryModal) closeDiscoveryModal();
#endif
    if (s_beaconsModal) closeBeaconsModal();
#if HAS_WEATHER
    if (s_weatherModal) closeWeatherModal();
#endif
#if FEATURE_MQTT_MONITOR
    if (s_mqttMonModal) closeMqttMonitorModal();
#endif
    if (closeLive && s_liveModal) closeLiveModal();
}

static DmConv *selectedDmConversation() {
    if (s_dmSelection <= 0) return nullptr;
    int convIdx = s_dmSelection - 1;
    if (convIdx < 0 || convIdx >= s_dmConvCount) return nullptr;
    uint32_t nodeId = s_dmConvNodeIds[convIdx];
    if (nodeId == 0) return nullptr;
    return DMs.find(nodeId);
}

static void activateDmSelection(bool allowCompose) {
    if (s_dmSelection == 0) {
        openDmNodePicker();
        return;
    }

    DmConv *selected = selectedDmConversation();
    if (!selected) return;

    if (!s_dmMsgPanelFocused) {
        s_dmMsgPanelFocused = true;
        refreshDmPanelFocusStyles();
    } else if (allowCompose) {
        openComposePromptForDm(selected->nodeId);
    }
}

static const char *dmDeleteTriggerLabel() {
#if UI_TOUCH_ONLY_PROFILE
    return "Long-press";
#else
    // Every keyboard build deletes with D, the same letter the Wi-Fi picker
    // already uses for it (see openCfgWifiDeleteConfirm).
    return "D";
#endif
}

static void dmDeleteSetFlash(const char *msg, uint32_t ttlMs = 2200) {
    if (!msg) msg = "";
    strncpy(s_dmDeleteFlashMsg, msg, sizeof(s_dmDeleteFlashMsg) - 1);
    s_dmDeleteFlashMsg[sizeof(s_dmDeleteFlashMsg) - 1] = '\0';
    s_dmDeleteFlashUntilMs = millis() + ttlMs;
}

// ── Destructive confirmation ─────────────────────────────────────────────
// Shared by DM deletion and touch-only Live clearing. Both use the same Y/N
// prompt shape as the WiFi picker: Y/Enter confirms, N/close cancels, and the
// prompt consumes every other key while it is up.
static void closeDmDeleteConfirm() {
    if (lvObjValid(s_dmDelBackdrop)) {
        lv_obj_del(s_dmDelBackdrop);
    } else if (lvObjValid(s_dmDelModal)) {
        lv_obj_del(s_dmDelModal);
    }
    s_dmDelBackdrop = nullptr;
    s_dmDelModal = nullptr;
    s_dmDeleteTargetNodeId = 0;
    s_destructiveConfirmAction = DESTRUCTIVE_CONFIRM_NONE;
    s_keyReviewBtns[0] = s_keyReviewBtns[1] = nullptr;
    s_keyReviewSel = 0;
    memset(s_keyReviewKey, 0, sizeof(s_keyReviewKey));
}

static void showKeyChangeToast(const char *text);

// Every view that marks a key conflict: the Nodes list glyph and detail, and
// the DM banner. Called when one is raised and when the user settles it.
static void refreshKeyConflictViews() {
    if (s_nodesModal) {
        refreshNodesListRows();
        refreshNodesListSelection();
        refreshNodesDetails();
    }
    if (s_dmModal) refreshDmModal(true);
}

// Reject is its own outcome, distinct from closing the dialog: closing leaves
// the conflict pending to decide later, Reject drops the offered key and stops
// it being raised again until reboot.
static void keyReviewReject() {
    const uint32_t nodeId = s_dmDeleteTargetNodeId;
    uint8_t shown[32];
    memcpy(shown, s_keyReviewKey, sizeof(shown));
    closeDmDeleteConfirm();
    if (nodeId && !Nodes.rejectKeyChange(nodeId, shown)) {
        showKeyChangeToast(TR("Key changed again - review"));
    }
    refreshKeyConflictViews();
}

static void onKeyReviewRejectPressed(lv_event_t *e) {
    LV_UNUSED(e);
    keyReviewReject();
}

static void keyReviewApplySelection() {
    for (int i = 0; i < 2; i++) {
        if (!lvObjValid(s_keyReviewBtns[i])) continue;
        const bool sel = (i == s_keyReviewSel);
        lv_obj_set_style_border_width(s_keyReviewBtns[i], sel ? 3 : 1, 0);
    #if !defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_border_color(s_keyReviewBtns[i],
                                      sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x000000), 0);
    #endif
    }
}

static void showKeyChangeToast(const char *text) {
    if (!text || !text[0]) return;
    lv_obj_t *toast = lv_obj_create(lv_layer_top());
    if (!toast) return;
    lv_obj_remove_style_all(toast);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(toast, lv_disp_get_hor_res(NULL) - 16, 0);
    lv_obj_align(toast, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_radius(toast, 6, 0);
    lv_obj_set_style_pad_hor(toast, 8, 0);
    lv_obj_set_style_pad_ver(toast, 4, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(toast, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0x000000), 0);
#else
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x6B3030), 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0xFFB060), 0);
#endif
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_t *lbl = lv_label_create(toast);
    if (lbl) {
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
    #else
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    #endif
        setLabelTextEmojiSafe(lbl, text);
    }
    lv_obj_delete_delayed(toast, 4000);
}

// A node just offered a key that differs from the pinned one. Log it, put a
// line in Live, and float a short toast -- throttled, because whoever can spoof
// one NodeInfo can spoof a stream of them with a fresh key each time.
// An unsigned NodeInfo tried to change a known signer's name or role and was
// refused (sigRefusesUnsignedIdentity). Most often the node really did rename
// and its NodeInfo lost the signature on a pre-2.8 relay, so the user is told
// how to accept it -- at most once an hour, as upstream's ClientNotification.
static void noteRefusedUnsignedIdentity(uint32_t nodeId, const UserInfo &u) {
    static uint32_t sLastNoteMs = 0;
    Serial.printf("[xeddsa] refused unsigned name/role change for !%08lx (\"%s\")\n",
                  (unsigned long)nodeId, u.longName);
    const uint32_t now = millis();
    if (sLastNoteMs != 0 && (uint32_t)(now - sLastNoteMs) < 3600000UL) return;
    sLastNoteMs = now ? now : 1;
    char who[48];
    nodesActionTitleLabel(nodeId, who, sizeof(who));
    char line[128];
    snprintf(line, sizeof(line), "[sig] %s: %s", who,
             TR("unsigned name change refused - remove the node to accept it"));
    liveFeedAddLine(line, TFT_ORANGE);
}

static void noteKeyConflictRaised(uint32_t nodeId) {
    char who[48];
    nodesActionTitleLabel(nodeId, who, sizeof(who));
    Serial.printf("[keypin] key change offered for !%08lx (%s); pinned key kept\n",
                  (unsigned long)nodeId, who);
    char line[96];
    snprintf(line, sizeof(line), "[key] %s: %s", who, TR("key changed - review in Nodes"));
    liveFeedAddLine(line, TFT_ORANGE);

    // The open review is about a key that has just been replaced. Close it
    // rather than leave a stale fingerprint up; the user reopens it from Node
    // Actions and sees the key now pending. Always told, whatever the throttle.
    if (s_dmDelModal && s_destructiveConfirmAction == DESTRUCTIVE_CONFIRM_KEY_CHANGE
        && s_dmDeleteTargetNodeId == nodeId) {
        closeDmDeleteConfirm();
        showKeyChangeToast(TR("Key changed again - review"));
        refreshKeyConflictViews();
        return;
    }

    static uint32_t s_lastToastMs = 0;
    const uint32_t now = millis();
    if (s_lastToastMs == 0 || (uint32_t)(now - s_lastToastMs) >= 30000) {
        s_lastToastMs = now ? now : 1;
        char buf[112];
        snprintf(buf, sizeof(buf), "! %s: %s", who, TR("key changed - review in Nodes"));
        showKeyChangeToast(buf);
    }
    refreshKeyConflictViews();
}

static void dmDeleteConfirmAccept() {
    // Read before the close, which clears the action and target.
    const DestructiveConfirmAction action = s_destructiveConfirmAction;
    uint32_t nodeId = s_dmDeleteTargetNodeId;
    uint8_t shownKey[32];
    memcpy(shownKey, s_keyReviewKey, sizeof(shownKey));
    closeDmDeleteConfirm();

    if (action == DESTRUCTIVE_CONFIRM_KEY_CHANGE) {
        if (nodeId == 0) return;
        // Only the key the dialog showed; a later offer that replaced it is
        // not accepted on the strength of a click about another one.
        if (!Nodes.acceptKeyChange(nodeId, shownKey)) {
            showKeyChangeToast(TR("Key changed again - review"));
        }
        refreshKeyConflictViews();
        return;
    }
    if (action == DESTRUCTIVE_CONFIRM_LIVE_CLEAR) {
        Channels.clearChannel(CHAN_LIVE);
        s_lastRenderedLiveCount = -1;
        s_lastRenderedLiveScrollOff = -1;
        s_liveClearFlashUntilMs = millis() + 2200;
        refreshLiveView(true);
        return;
    }
    if (action == DESTRUCTIVE_CONFIRM_NODE_RESTORE) {
        if (nodeId == 0) return;
        // Re-read rather than trusting the index: the index holds names and
        // dates only, and what makes a restore worth doing is the position and
        // the public key, which are expensive to reacquire from the mesh.
        NodeEntry rec = {};
        if (!nodeArchiveLoadFull(nodeId, rec)) {
            openCfgActionMessageModal(TR("Restore failed: the archived record could not be read."));
            return;
        }
        // May evict — and that eviction is archived by the same path any other
        // one is, so nothing is lost by restoring into a full table.
        NodeEntry *live = Nodes.upsert(nodeId);
        if (!live) {
            openCfgActionMessageModal(
                TR("Restore failed: every slot is favorited, so there is no room. "
                "Unfavorite a node and try again."));
            return;
        }
        *live = rec;
        live->nodeId = nodeId;   // upsert() owns the slot's identity
        Nodes.markRankingDirty();
        // upsert() saved the blank record it created; this saves what we just
        // put in it.
        Nodes.persist(nodeId);

        if (s_nodesModal) {
            // Re-snapshot: the node has moved from the archived half to the
            // live one, and on a full table something else has moved the other
            // way. Both halves have to be rebuilt for the list to be true.
            snapshotNodesForModal();
            // Land the cursor back on the node that was just restored, wherever
            // it sorted to, rather than on whatever happens to sit at the old
            // index now.
            s_nodesSelected = 0;
            for (int i = 0; i < s_nodesFilteredCount; i++) {
                if (nodesRowNodeId(s_nodesFilteredIdx[i]) == nodeId) { s_nodesSelected = i; break; }
            }
            nodesClampRenderWindow();
            refreshNodesListRows();
            refreshNodesListSelection();
            refreshNodesDetails();
        }
        return;
    }
    if (action == DESTRUCTIVE_CONFIRM_NODE_DELETE) {
        if (nodeId == 0) return;
        const bool gone = Nodes.remove(nodeId);
        // Only the Nodes screen shows a list to repair. Opened from the chat
        // cursor there is nothing behind this dialog, and re-snapshotting would
        // rebuild rows for a modal that is not on screen — the same reasoning
        // the favorite toggle uses.
        if (gone && s_nodesModal) {
            snapshotNodesForModal();
            // The row under the cursor is the one that just went away, so the
            // index cannot be restored by identity the way the favorite toggle
            // restores it. Keep the position instead, clamped to the shorter
            // list, which lands the cursor on whatever moved up into the gap.
            if (s_nodesFilteredCount <= 0) {
                s_nodesSelected = -1;
            } else if (s_nodesSelected >= s_nodesFilteredCount) {
                s_nodesSelected = s_nodesFilteredCount - 1;
            } else if (s_nodesSelected < 0) {
                s_nodesSelected = 0;
            }
            refreshNodesListRows();
            refreshNodesListSelection();
            refreshNodesDetails();
        }
        return;
    }

    if (action != DESTRUCTIVE_CONFIRM_DM_DELETE) return;
    if (nodeId == 0) return;

    if (DMs.deleteConversation(nodeId)) {
        dmDeleteSetFlash(TR("Conversation deleted"));
        s_dmSelection = 0;
        s_dmMsgPanelFocused = false;
        s_dmRenderedConvCount = -1;
        s_dmRenderedNodeId = 0;
        s_dmRenderedMsgCount = -1;
        s_dmRenderedUnreadTotal = -1;
    } else {
        dmDeleteSetFlash(TR("Delete failed"));
    }
    refreshDmModal(true);
}

static void dmDeleteConfirmReject() {
    const DestructiveConfirmAction action = s_destructiveConfirmAction;
    closeDmDeleteConfirm();
    if (action == DESTRUCTIVE_CONFIRM_DM_DELETE) refreshDmModal(true);
}

static void onDmDelYesPressed(lv_event_t *e) {
    LV_UNUSED(e);
    dmDeleteConfirmAccept();
}

static void onDmDelNoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    dmDeleteConfirmReject();
}

static void onDmDelBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_dmDelBackdrop) return;
    dmDeleteConfirmReject();
}

static void openDestructiveConfirm(DestructiveConfirmAction action, uint32_t nodeId) {
    if (!s_rootScreen || action == DESTRUCTIVE_CONFIRM_NONE) return;
    if (action == DESTRUCTIVE_CONFIRM_DM_DELETE && nodeId == 0) return;
    if (action == DESTRUCTIVE_CONFIRM_NODE_DELETE && nodeId == 0) return;
    if (action == DESTRUCTIVE_CONFIRM_NODE_RESTORE && nodeId == 0) return;
    if (action == DESTRUCTIVE_CONFIRM_KEY_CHANGE && !Nodes.hasKeyConflict(nodeId)) return;
    if (action == DESTRUCTIVE_CONFIRM_LIVE_CLEAR
        && (!s_liveModal || Channels.get(CHAN_LIVE).count <= 0)) return;
    if (s_dmDelModal || s_dmDelBackdrop) return;

    s_destructiveConfirmAction = action;
    s_dmDeleteTargetNodeId = nodeId;

    char titleText[40];
    char bodyText[288];
    if (action == DESTRUCTIVE_CONFIRM_KEY_CHANGE) {
        char who[48];
        nodesActionTitleLabel(nodeId, who, sizeof(who));
        bool signedByNew = false;
        uint32_t atMs = 0;
        uint8_t offered[32];
        Nodes.pendingKey(nodeId, offered, &signedByNew, &atMs);
        // The exact key on screen is the one Accept/Reject may act on.
        memcpy(s_keyReviewKey, offered, sizeof(s_keyReviewKey));
        // ASCII "!" rather than a warning glyph: not every board's font has one.
        snprintf(titleText, sizeof(titleText), "! %s", TR("Key changed"));
        snprintf(bodyText, sizeof(bodyText), "%s\n%s\n%s %02X%02X%02X%02X%s",
                 who,
                 TR("This node is advertising a new encryption key. Accept only if "
                    "you know they reset or reflashed their device."),
                 TR("New key:"), offered[0], offered[1], offered[2], offered[3],
                 signedByNew ? TR(" (signed by new key)") : "");
    } else if (action == DESTRUCTIVE_CONFIRM_LIVE_CLEAR) {
        snprintf(titleText, sizeof(titleText), TR("Clear entire live feed?"));
        if (s_liveFilter == LIVE_FILTER_ALL && !s_liveNoMqtt) {
            snprintf(bodyText, sizeof(bodyText), TR("All live traffic will be erased"));
        } else {
            snprintf(bodyText, sizeof(bodyText),
                     TR("All live traffic will be erased, including lines hidden by the filter"));
        }
    } else if (action == DESTRUCTIVE_CONFIRM_NODE_RESTORE) {
        // Named from the archive index, not nodesActionTitleLabel(): the node is
        // by definition not in the table, so that helper would only have the
        // hex id to offer and the prompt would not say who this is.
        const ArchivedNode *a = currentNodesArchivedSelection();
        const char *who = TR("This node");
        if (a && a->nodeId == nodeId) {
            if (a->longName[0])       who = a->longName;
            else if (a->shortName[0]) who = a->shortName;
        }
        snprintf(titleText, sizeof(titleText), TR("Restore node?"));
        // The eviction is the part worth warning about: putting one node back
        // into a full table takes another one out.
        snprintf(bodyText, sizeof(bodyText),
                 TR("%s returns to the live list.\nIf it is full, the oldest "
                 "non-favorite is archived to make room."), who);
    } else if (action == DESTRUCTIVE_CONFIRM_NODE_DELETE) {
        char who[48];
        nodesActionTitleLabel(nodeId, who, sizeof(who));
        snprintf(titleText, sizeof(titleText), TR("Delete node?"));
        // Says what it is not, because the row next to it is Ignore and the two
        // read alike: this forgets a record, and the node reappears the next
        // time it is heard.
        snprintf(bodyText, sizeof(bodyText),
                 TR("%s will be removed from the node list."), who);
    } else {
        char who[48];
        nodesActionTitleLabel(nodeId, who, sizeof(who));
        snprintf(titleText, sizeof(titleText), TR("Delete conversation?"));
        snprintf(bodyText, sizeof(bodyText), TR("%s\nMessage history will be erased"), who);
    }

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

    s_dmDelBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_dmDelBackdrop, w, h);
    lv_obj_align(s_dmDelBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_dmDelBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dmDelBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_dmDelBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_dmDelBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_dmDelBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_dmDelBackdrop, 0, 0);
    lv_obj_add_event_cb(s_dmDelBackdrop, onDmDelBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_dmDelModal = lv_obj_create(s_dmDelBackdrop);
    lv_obj_set_size(s_dmDelModal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(s_dmDelModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_dmDelModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dmDelModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_dmDelModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_dmDelModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dmDelModal, 1, 0);
    lv_obj_set_style_border_color(s_dmDelModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_dmDelModal, 10, 0);
    lv_obj_set_style_pad_row(s_dmDelModal, 8, 0);
    lv_obj_set_flex_flow(s_dmDelModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmDelModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_dmDelBackdrop);

    lv_obj_t *title = lv_label_create(s_dmDelModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, titleText);

    lv_obj_t *body = lv_label_create(s_dmDelModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(body, bodyTextColor, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, bodyText);

    lv_obj_t *btnRow = lv_obj_create(s_dmDelModal);
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

    auto makeDmDelBtn = [](lv_obj_t *parent, const char *text, uint32_t bgColor,
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
    if (action == DESTRUCTIVE_CONFIRM_KEY_CHANGE) {
        // Reject first and selected: it is the safe answer to an unexpected
        // key. Closing (backdrop, close key) answers neither and leaves it
        // pending.
    #if UI_TOUCH_ONLY_PROFILE
        s_keyReviewBtns[0] = makeDmDelBtn(btnRow, TR_NOOP("Reject"), noBtnBg, onKeyReviewRejectPressed);
        s_keyReviewBtns[1] = makeDmDelBtn(btnRow, TR_NOOP("Accept new key"), yesBtnBg, onDmDelYesPressed);
    #else
        s_keyReviewBtns[0] = makeDmDelBtn(btnRow, TR_NOOP("(R)eject"), noBtnBg, onKeyReviewRejectPressed);
        s_keyReviewBtns[1] = makeDmDelBtn(btnRow, TR_NOOP("(A)ccept new key"), yesBtnBg, onDmDelYesPressed);
    #endif
        s_keyReviewSel = 0;
        keyReviewApplySelection();
        return;
    }
#if UI_TOUCH_ONLY_PROFILE
    makeDmDelBtn(btnRow, "No", noBtnBg, onDmDelNoPressed);
    makeDmDelBtn(btnRow, TR_NOOP("Yes"), yesBtnBg, onDmDelYesPressed);
#else
    makeDmDelBtn(btnRow, "(N)o", noBtnBg, onDmDelNoPressed);
    makeDmDelBtn(btnRow, "(Y)es", yesBtnBg, onDmDelYesPressed);
#endif
}

static void openDmDeleteConfirm(uint32_t nodeId) {
    openDestructiveConfirm(DESTRUCTIVE_CONFIRM_DM_DELETE, nodeId);
}

#if UI_TOUCH_ONLY_PROFILE
static void openLiveClearConfirm() {
    openDestructiveConfirm(DESTRUCTIVE_CONFIRM_LIVE_CLEAR, 0);
}

static void onLiveRowLongPressed(lv_event_t *e) {
    LV_UNUSED(e);
    openLiveClearConfirm();
}
#endif

static void dmRequestDeleteSelectedConversation() {
    DmConv *selected = selectedDmConversation();
    if (!selected || selected->nodeId == 0) return;
    openDmDeleteConfirm(selected->nodeId);
}

#if UI_TOUCH_ONLY_PROFILE
// The right panel's own compose button. Writes to whichever conversation is
// selected; with none selected there is nobody to write to, so it falls through
// to the picker that chooses one — the same thing the New DM row does.
static void onDmNewMessagePressed(lv_event_t *e) {
    LV_UNUSED(e);
    if (s_dmNodePickerModal || s_composeModal) return;
    DmConv *selected = selectedDmConversation();
    if (selected && selected->nodeId != 0) {
        openComposePromptForDm(selected->nodeId);
        return;
    }
    openDmNodePicker();
}

// Consumes a refresh that was held back while a row was under a finger.
// Returns false while the finger is still down, so the caller waits for the
// gesture to finish rather than pulling the rug out from under it.
static bool dmTakeDeferredRefresh() {
    if (s_dmRowPressActive || !s_dmRefreshPending) return false;
    s_dmRefreshPending = false;
    return true;
}
#endif

// ── DM conversation drawer ───────────────────────────────────────────────────
// The channel drawer's twin (channelDrawerSlide()), on the DM screen. It is not
// a separate state: it is the conversation list having the keys. Open while
// s_dmMsgPanelFocused is false -- the list's keys, the list in view -- and shut
// while the messages have them, so every path that already moved focus between
// the two panes (Enter, Backspace, the wheel click, a tap on a row, a delete)
// opens and closes it without knowing it exists.
static bool dmDrawerBuilt() {
    return lvObjValid(s_dmModal) && lvObjValid(s_dmConvPanel) && lvObjValid(s_dmDrawerScrim)
        && lvObjValid(s_dmMsgPanel);
}

#if HAS_CHANNEL_DRAWER_ANIM
static void dmDrawerSlideXCb(void *obj, int32_t x) {
    lv_obj_set_x((lv_obj_t *)obj, x);
}

static void dmDrawerSlideDone(lv_anim_t *a) {
    lv_obj_t *drawer = (lv_obj_t *)a->var;
    if (!s_dmDrawerOpen && lvObjValid(drawer)) {
        lv_obj_add_flag(drawer, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

static void dmDrawerSlide(bool open) {
    if (!dmDrawerBuilt()) return;

    // Placed every time, even when it is not moving: the nav column's padding
    // goes on the screen after the drawer is made (appendHeltecBottomNav()),
    // and the Cardputer's hint line comes and goes under the pane with the
    // delete prompt. Coordinates are the screen's content area, which starts
    // its padding in from the display edge; the drawer sits flush against that
    // edge, or against the nav column when it is on the left, over the message
    // pane's full height.
    lv_obj_update_layout(s_dmModal);
    const int32_t padL = lv_obj_get_style_pad_left(s_dmModal, LV_PART_MAIN)
                       + lv_obj_get_style_border_width(s_dmModal, LV_PART_MAIN);
    const int32_t padT = lv_obj_get_style_pad_top(s_dmModal, LV_PART_MAIN)
                       + lv_obj_get_style_border_width(s_dmModal, LV_PART_MAIN);
    lv_obj_t *content = lv_obj_get_parent(s_dmMsgPanel);
    const int32_t paneY = lv_obj_get_y(content);
    const int32_t paneH = lv_obj_get_height(content);
#if defined(DEVICE_TDECK_PRO)
    // From the top of the screen, over the header, as the channel drawer is
    // there: on 1-bit paper with no dim, a sheet the pane's height only looks
    // like the pane got narrower.
    lv_obj_set_y(s_dmConvPanel, -padT);
    lv_obj_set_height(s_dmConvPanel, paneY + paneH + padT);
#else
    lv_obj_set_y(s_dmConvPanel, paneY);
    lv_obj_set_height(s_dmConvPanel, paneH);
#endif
    const int32_t shownX = navSideInsetLeft() - padL;
    const int32_t hiddenX = -padL - lv_obj_get_width(s_dmConvPanel) - 2;

    lv_obj_set_pos(s_dmDrawerScrim, -padL, -padT);
    // No scrim with nothing behind the drawer to go back to: tapping it could
    // not shut the list (dmDrawerSetOpen()), and up it would cover the nav bar
    // that is the touch boards' only way off this screen. Set on every pass,
    // so the first conversation arriving with the list up brings it in.
    if (open && s_dmConvCount > 0) lv_obj_clear_flag(s_dmDrawerScrim, LV_OBJ_FLAG_HIDDEN);
    else                           lv_obj_add_flag(s_dmDrawerScrim, LV_OBJ_FLAG_HIDDEN);
    if (open == s_dmDrawerOpen
        && open != lv_obj_has_flag(s_dmConvPanel, LV_OBJ_FLAG_HIDDEN)) {
        return;   // already where it was asked to be
    }
    s_dmDrawerOpen = open;

    if (open) {
        lv_obj_move_foreground(s_dmDrawerScrim);
        if (lv_obj_has_flag(s_dmConvPanel, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_x(s_dmConvPanel, hiddenX);
            lv_obj_clear_flag(s_dmConvPanel, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_move_foreground(s_dmConvPanel);
        // The selection may be anywhere in a long list.
        if (s_dmSelection > 0 && s_dmSelection <= s_dmConvCount
            && lvObjValid(s_dmConvRows[s_dmSelection - 1])) {
            lv_obj_scroll_to_view(s_dmConvRows[s_dmSelection - 1], LV_ANIM_OFF);
        }
    } else {
        lv_obj_add_flag(s_dmDrawerScrim, LV_OBJ_FLAG_HIDDEN);
    }

#if HAS_CHANNEL_DRAWER_ANIM
    lv_anim_delete(s_dmConvPanel, dmDrawerSlideXCb);
    if (s_dmDrawerAnimReady) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_dmConvPanel);
        lv_anim_set_exec_cb(&a, dmDrawerSlideXCb);
        lv_anim_set_values(&a, lv_obj_get_x(s_dmConvPanel), open ? shownX : hiddenX);
        lv_anim_set_duration(&a, kChannelDrawerSlideMs);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(&a, dmDrawerSlideDone);
        lv_anim_start(&a);
        return;
    }
#endif
    lv_obj_set_x(s_dmConvPanel, open ? shownX : hiddenX);
    if (!open) lv_obj_add_flag(s_dmConvPanel, LV_OBJ_FLAG_HIDDEN);
}

// Brings the drawer to where the focus says it belongs.
static void dmDrawerSync() {
    if (!dmDrawerBuilt()) return;
    dmDrawerSlide(!(s_dmMsgPanelFocused && s_dmSelection > 0));
}

// Asked for by name: the toggle key, a tap on the header or the scrim, a swipe.
// Shutting it needs a conversation to show behind it. From the New DM row that
// is the top one; with no conversations at all there is nothing to show, and
// the list stays.
static void dmDrawerSetOpen(bool open) {
    if (!dmDrawerBuilt()) return;
    if (open) {
        s_dmMsgPanelFocused = false;
        refreshDmPanelFocusStyles();
        return;
    }
    if (s_dmSelection <= 0) {
        if (s_dmConvCount <= 0) return;
        s_dmSelection = 1;
        s_dmMsgPanelFocused = true;
        refreshDmModal(true);
        return;
    }
    s_dmMsgPanelFocused = true;
    refreshDmPanelFocusStyles();
}

static void onDmDrawerScrimPressed(lv_event_t *e) {
    LV_UNUSED(e);
    dmDrawerSetOpen(false);
}

#if HAS_TOUCH
static void onDmHeaderTitlePressed(lv_event_t *e) {
    LV_UNUSED(e);
    // Only ever opens in practice: while the drawer is up the scrim covers the
    // header, and the tap closes it from there.
    dmDrawerSetOpen(!s_dmDrawerOpen);
}
#endif

#if HAS_TOUCH && UI_CHANNEL_LIST_DROPDOWN
// The edge swipe's half for this screen (channelDrawerIndevCb()): the DM screen
// has to be the one in front, with nothing of its own over it.
static bool dmDrawerCanOpenFromEdge() {
    if (!dmDrawerBuilt() || s_dmDrawerOpen) return false;
    if (s_composeModal || s_dmNodePickerModal || s_dmDelModal) return false;
    lv_obj_t *parent = lv_obj_get_parent(s_dmModal);
    if (!parent) return false;
    const int32_t from = lv_obj_get_index(s_dmModal);
    if (from < 0) return false;
    const int32_t dispW = lv_disp_get_hor_res(NULL);
    const int32_t dispH = lv_disp_get_ver_res(NULL);
    const uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = (uint32_t)from + 1; i < n; i++) {
        lv_obj_t *o = lv_obj_get_child(parent, (int32_t)i);
        if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_get_width(o) >= dispW * 3 / 4 && lv_obj_get_height(o) >= dispH / 2) {
            return false;
        }
    }
    return true;
}
#endif

// The header names the conversation being read, as the chat's names its
// channel; the screen's own name when there is none.
static void dmRefreshHeaderTitle(const DmConv *selected) {
    if (!lvObjValid(s_dmHeaderTitle) || !dmDrawerBuilt()) return;
    if (!selected) {
        screenHeaderSetTitle(s_dmHeaderTitle, TR("Direct Messages"));
        return;
    }
    char name[64];
    chatSenderLabel(selected->nodeId, name, sizeof(name));
    if (!name[0] && liveShortNameUsable(selected->shortName)) {
        snprintf(name, sizeof(name), "%s", selected->shortName);
    }
    if (!name[0]) snprintf(name, sizeof(name), "!%08lX", (unsigned long)selected->nodeId);
    screenHeaderSetTitle(s_dmHeaderTitle, name);
}

// The key legend under the messages. One place for it: the screen sets it on
// open, and refreshDmModal() puts it back after the delete flow has borrowed
// the line.
static const char *dmHintText() {
    static char buf[128];
    const bool drawer = dmListIsDrawer();
#if UI_CARDPUTER_COMPACT
    LV_UNUSED(drawer);
    buf[0] = '\0';   // no static legend here (openDmModal())
#elif defined(DEVICE_TDECK)
    snprintf(buf, sizeof(buf), "%s",
             drawer ? TR("J/K = Select   Enter = Open   C = List   Space = Compose   D = Delete")
                    : TR("J/K = Select   Space = Compose   Enter = Focus   D = Delete"));
#elif defined(DEVICE_TLORA_PAGER_TFT)
    LV_UNUSED(drawer);
    snprintf(buf, sizeof(buf), "%s",
             TR("Up/Down = Select   Space = Compose   Enter = Focus   D = Delete"));
#elif UI_TOUCH_ONLY_PROFILE
    snprintf(buf, sizeof(buf), "%s",
             drawer ? TR("Tap name = List   Long-press 3s = Delete   DM below = Back")
                    : TR("Tap = Open   Long-press 3s = Delete   DM below = Back"));
#elif defined(DEVICE_M9)
    snprintf(buf, sizeof(buf),
             drawer ? TR("Up/Down = Select   Enter = Open   DM = List   Space = Compose   %s = Delete   %s = Back")
                    : TR("Up/Down = Select   Space = Compose   Enter = Focus   %s = Delete   %s = Back"),
             dmDeleteTriggerLabel(), modalCloseKeyLabel());
#else
    snprintf(buf, sizeof(buf),
             drawer ? TR("Up/Down = Select   Enter = Open   C = List   Space = Compose   %s = Delete   %s = Back")
                    : TR("Up/Down = Select   Space = Compose   Enter = Focus   %s = Delete   %s = Back"),
             dmDeleteTriggerLabel(), modalCloseKeyLabel());
#endif
    return buf;
}

static void refreshDmPanelFocusStyles() {
    dmDrawerSync();
    if (!s_dmConvTitle || !s_dmMsgTitle) return;

    // The panes have no frame now -- one rule divides them -- so the pane with
    // the keys is marked on its title: underlined, and in the brighter ink. The
    // underline is what carries it on the Pro's 1-bit e-paper, where the two
    // inks come out the same.
    const bool msgFocused = s_dmMsgPanelFocused && (s_dmSelection > 0);
    auto mark = [](lv_obj_t *title, bool focused) {
        lv_obj_set_style_text_decor(title,
                                    focused ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
#if !defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(
            title,
            focused ? lv_color_hex(0x90B4FF)
                    : ((s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D)
                                                       : lv_color_hex(0xD9E8FF)),
            0);
#endif
    };
    mark(s_dmConvTitle, !msgFocused);
    mark(s_dmMsgTitle, msgFocused);
}

static void onDmConversationPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > s_dmConvCount) return;
    // The end of a hold-to-delete: in the drawer rows answer on CLICKED, which
    // LVGL still sends on that release, and it would shut the drawer behind the
    // prompt the hold just raised.
    if (s_dmDelModal) return;
    s_dmSelection = idx;
    s_dmMsgPanelFocused = (idx > 0);
#if UI_TOUCH_ONLY_PROFILE
    // Asked for, not performed. This handler runs on LV_EVENT_PRESSED — finger
    // still down — and refreshDmModal() rebuilds the list with lv_obj_clean(),
    // which would delete this very row and take the LV_EVENT_CLICKED that
    // activates it along with it. That is exactly why tapping New DM did
    // nothing: the row was gone before the tap could complete. loop()'s UI tick
    // runs it once the gesture is over.
    s_dmRefreshPending = true;
#else
    refreshDmModal(true);
#endif
}

static void onDmConversationPressState(lv_event_t *e) {
#if UI_TOUCH_ONLY_PROFILE
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    // Press tracking covers every row, New DM (index 0) included, so the list
    // is held still for the whole of any gesture. The hold-to-delete timing
    // below is still conversations-only.
    {
        const lv_event_code_t stateCode = lv_event_get_code(e);
        if (stateCode == LV_EVENT_PRESSED) {
            s_dmRowPressActive = true;
        } else if (stateCode == LV_EVENT_RELEASED || stateCode == LV_EVENT_PRESS_LOST) {
            // Only lift the hold. The deferred refresh is *not* run from here:
            // LVGL sends RELEASED before CLICKED (lv_indev.c indev_proc_release),
            // so rebuilding the list at this point would delete the row before
            // the click that activates it — the same bug one event later. The
            // UI tick in loop() picks the refresh up a few milliseconds later,
            // by which time the gesture is over.
            s_dmRowPressActive = false;
        }
    }

    if (idx <= 0 || idx > s_dmConvCount) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_dmTouchPressStartMs = millis();
        s_dmTouchPressRowIdx = idx;
        s_dmTouchLongPressTriggered = false;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (s_dmTouchPressRowIdx == idx
            && !s_dmTouchLongPressTriggered
            && (millis() - s_dmTouchPressStartMs) >= 3000UL) {
            s_dmTouchLongPressTriggered = true;
            s_dmSelection = idx;
            s_dmMsgPanelFocused = false;
            dmRequestDeleteSelectedConversation();
        }
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_dmTouchPressRowIdx == idx) {
            s_dmTouchPressStartMs = 0;
            s_dmTouchPressRowIdx = -1;
            s_dmTouchLongPressTriggered = false;
        }
        return;
    }
#else
    LV_UNUSED(e);
#endif
}

// Keeps the rendered slice covering the selection, exactly as
// nodesClampRenderWindow() does for the Nodes list.
static void dmNodePickerClampRenderWindow() {
    if (s_dmNodeFilteredCount <= 0) {
        s_dmNodePickerRenderStart = 0;
        return;
    }

    if (s_dmNodeSelection < 0) s_dmNodeSelection = 0;
    if (s_dmNodeSelection >= s_dmNodeFilteredCount) s_dmNodeSelection = s_dmNodeFilteredCount - 1;

    int renderRows = min(kNodesMaxRenderedRows, s_dmNodeFilteredCount);
    if (renderRows < 1) renderRows = 1;
    int maxStart = s_dmNodeFilteredCount - renderRows;
    if (maxStart < 0) maxStart = 0;

    if (s_dmNodePickerRenderStart > maxStart) s_dmNodePickerRenderStart = maxStart;
    if (s_dmNodeSelection < s_dmNodePickerRenderStart) {
        s_dmNodePickerRenderStart = s_dmNodeSelection;
    } else if (s_dmNodeSelection >= (s_dmNodePickerRenderStart + renderRows)) {
        s_dmNodePickerRenderStart = s_dmNodeSelection - renderRows + 1;
    }

    if (s_dmNodePickerRenderStart < 0) s_dmNodePickerRenderStart = 0;
    if (s_dmNodePickerRenderStart > maxStart) s_dmNodePickerRenderStart = maxStart;
}

static lv_obj_t *selectedDmNodePickerRowObj() {
    for (int i = 0; i < s_dmNodePickerRowCount; i++) {
        if (s_dmNodePickerRenderedIdx[i] == s_dmNodeSelection) return s_dmNodePickerRows[i];
    }
    return nullptr;
}

// Repaints the highlight on the rows already on screen. This is the whole point
// of the window: moving the selection is four style writes, not a rebuild.
static void refreshDmNodePickerSelection() {
    for (int i = 0; i < s_dmNodePickerRowCount; i++) {
        lv_obj_t *row = s_dmNodePickerRows[i];
        if (!row) continue;
        bool selected = (s_dmNodePickerRenderedIdx[i] == s_dmNodeSelection);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_border_width(row, selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    #else
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selected ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
        lv_obj_set_style_bg_color(row,
                                  selected ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                  0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_40, 0);
    #endif
    }
}

// Moves the highlight, rebuilding only when the window itself has to scroll.
static void dmNodePickerMoveSelection(int next) {
    if (s_dmNodeFilteredCount <= 0) return;
    if (next < 0) next = 0;
    if (next >= s_dmNodeFilteredCount) next = s_dmNodeFilteredCount - 1;
    if (next == s_dmNodeSelection) return;

    s_dmNodeSelection = next;
    const int prevStart = s_dmNodePickerRenderStart;
    dmNodePickerClampRenderWindow();
    if (s_dmNodePickerRenderStart != prevStart) {
        refreshDmNodePicker(true);   // window scrolled: the slice really changed
    } else {
        refreshDmNodePickerSelection();
    }
    if (lv_obj_t *row = selectedDmNodePickerRowObj()) {
        lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }
}

static void onDmNodePressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_dmNodeFilteredCount) return;
    dmNodePickerMoveSelection(idx);
}

static char dmNodePickerAsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool dmNodePickerContainsNoCase(const char *text, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!text || !text[0]) return false;
    for (int i = 0; text[i]; i++) {
        int j = 0;
        while (needle[j] && text[i + j]
               && dmNodePickerAsciiLower(text[i + j]) == dmNodePickerAsciiLower(needle[j])) {
            j++;
        }
        if (!needle[j]) return true;
    }
    return false;
}

static void dmNodePickerApplyFilter() {
    s_dmNodeFilteredCount = 0;

    bool useFilter = (s_dmNodeFilterOpen && s_dmNodeFilterLen > 0);
    for (int i = 0; i < s_dmNodeSnapshotCount && s_dmNodeFilteredCount < MAX_NODES; i++) {
        // Resolved live; null if evicted since the snapshot, leaving nothing
        // but the id to match against.
        const NodeEntry *n = Nodes.find(s_dmNodeSnapshotIds[i]);
        if (useFilter) {
            bool match = false;
            if (n && dmNodePickerContainsNoCase(n->shortName, s_dmNodeFilter)) match = true;
            if (!match && n && dmNodePickerContainsNoCase(n->longName, s_dmNodeFilter)) match = true;
            if (!match) continue;
        }

        s_dmNodeFilteredIdx[s_dmNodeFilteredCount++] = i;
    }

    if (s_dmNodeFilteredCount <= 0) {
        s_dmNodeSelection = 0;
    } else {
        s_dmNodeSelection = constrain(s_dmNodeSelection, 0, s_dmNodeFilteredCount - 1);
    }
    // A filter keystroke can drop the selection anywhere in the new list, so the
    // window has to follow it before the next render.
    dmNodePickerClampRenderWindow();
}

static void snapshotNodesForDmPicker() {
    s_dmNodeSnapshotCount = 0;
    s_dmNodeFilteredCount = 0;
    s_dmNodeSelection = -1;
    s_dmNodePickerRenderStart = 0;
    s_dmNodePickerRowCount = 0;
    s_dmNodeFilterOpen = false;
    s_dmNodeFilterLen = 0;
    s_dmNodeFilter[0] = '\0';

    int total = Nodes.count();
    if (total < 0) total = 0;
    if (total > MAX_NODES) total = MAX_NODES;

    for (int i = 0; i < total && s_dmNodeSnapshotCount < MAX_NODES; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n || n->nodeId == 0) continue;
        if (n->nodeId == s_myNodeId) continue;

        bool seen = false;
        for (int j = 0; j < s_dmNodeSnapshotCount; j++) {
            if (s_dmNodeSnapshotIds[j] == n->nodeId) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        s_dmNodeSnapshotIds[s_dmNodeSnapshotCount++] = n->nodeId;
    }

    dmNodePickerApplyFilter();
}

static const NodeEntry *selectedDmNodeForPicker() {
    if (s_dmNodeSelection < 0 || s_dmNodeSelection >= s_dmNodeFilteredCount) return nullptr;
    int snapshotIdx = s_dmNodeFilteredIdx[s_dmNodeSelection];
    if (snapshotIdx < 0 || snapshotIdx >= s_dmNodeSnapshotCount) return nullptr;
    // Resolved live against NodeDB; null if evicted since the snapshot.
    return Nodes.find(s_dmNodeSnapshotIds[snapshotIdx]);
}

static void activateDmNodePickerSelection() {
    const NodeEntry *n = selectedDmNodeForPicker();
    if (!n || n->nodeId == 0) return;

    const uint32_t selectedNodeId = n->nodeId;
    DmConv *conv = DMs.find(selectedNodeId);
    if (!conv) {
        const char *shortName = liveShortNameUsable(n->shortName) ? n->shortName : nullptr;
        conv = DMs.findOrCreate(selectedNodeId, shortName);
    }

    closeDmNodePicker();
    refreshDmModal(true);
    if (!conv) return;

    for (int i = 0; i < s_dmConvCount; i++) {
        if (s_dmConvNodeIds[i] == selectedNodeId) {
            s_dmSelection = i + 1;  // +1 because row 0 is "New DM"
            // Into the new conversation where the list is a drawer, as from
            // Nodes (executeNodesActionSelection()).
            if (dmListIsDrawer()) s_dmMsgPanelFocused = true;
            break;
        }
    }
    refreshDmModal(true);
}

static void refreshDmNodePicker(bool force) {
    LV_UNUSED(force);
    if (!lvObjAlive(s_dmNodePickerModal) || !lvObjAlive(s_dmNodePickerList)) {
        closeDmNodePicker();
        return;
    }

    const lv_color_t dmPickerTextColor =
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF);
    const lv_color_t dmPickerHintColor =
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF);

    if (s_dmNodePickerTitle) {
        lv_obj_set_style_text_color(s_dmNodePickerTitle, dmPickerTextColor, 0);
        if (s_dmNodeFilterOpen) {
            char title[64];
            snprintf(title, sizeof(title), TR("New DM: Select Node [%s]"), s_dmNodeFilter);
            lv_label_set_text(s_dmNodePickerTitle, title);
        } else {
            lv_label_set_text(s_dmNodePickerTitle, TR("New DM: Select Node"));
        }
    }

    if (s_dmNodePickerHint) {
        lv_obj_set_style_text_color(s_dmNodePickerHint, dmPickerHintColor, 0);
#if UI_TOUCH_ONLY_PROFILE
        // Nothing here changes with the filter — there is no keyboard to open
        // one — so the line says the one thing that is always true.
        lv_label_set_text(s_dmNodePickerHint, TR("Tap a node to start a DM"));
#elif UI_CARDPUTER_COMPACT
        lv_label_set_text(s_dmNodePickerHint,
                          s_dmNodeFilterOpen
                              ? TR("Type = Filter   Bksp = Edit Filter   Enter = Open DM   Esc = Back")
                              : TR("Type = Filter   Enter = Open DM   Esc = Back"));
#else
        lv_label_set_text(s_dmNodePickerHint,
                          s_dmNodeFilterOpen
                              ? TR("Type = Filter   Bksp = Edit/Close Filter   Enter = Open DM")
                              : TR("Type = Filter   Enter = Open DM   Bksp = Back"));
#endif
    }

    lv_obj_clean(s_dmNodePickerList);
    memset(s_dmNodePickerRows, 0, sizeof(s_dmNodePickerRows));
    memset(s_dmNodePickerRenderedIdx, 0, sizeof(s_dmNodePickerRenderedIdx));
    s_dmNodePickerRowCount = 0;

    if (s_dmNodeFilteredCount <= 0) {
        lv_obj_t *empty = lv_label_create(s_dmNodePickerList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(empty, dmPickerTextColor, 0);
        if (s_dmNodeFilterOpen && s_dmNodeFilterLen > 0) {
            char noMatch[64];
            snprintf(noMatch, sizeof(noMatch), TR("No matches for: %s"), s_dmNodeFilter);
            lv_label_set_text(empty, noMatch);
        } else {
            lv_label_set_text(empty, TR("No known nodes yet"));
        }
        return;
    }

    dmNodePickerClampRenderWindow();
    const int renderStart = s_dmNodePickerRenderStart;
    const int renderLimit = min(kNodesMaxRenderedRows, s_dmNodeFilteredCount - renderStart);
    for (int listIdx = 0; listIdx < renderLimit; listIdx++) {
        lv_mem_monitor_t pickerMem;
        lv_mem_monitor(&pickerMem);
        if (pickerMem.free_biggest_size < 4096) {
            logLvglMemDiag("dm node picker render stopped (low LVGL mem)");
            break;
        }

        const int i = renderStart + listIdx;
        bool selected = (i == s_dmNodeSelection);
        int snapshotIdx = s_dmNodeFilteredIdx[i];
        if (snapshotIdx < 0 || snapshotIdx >= s_dmNodeSnapshotCount) continue;
        // Resolve live. Evicted nodes still emit a row (from their id) so rows
        // stay 1:1 with the filtered list and selection indices stay valid.
        const uint32_t nodeId = s_dmNodeSnapshotIds[snapshotIdx];
        const NodeEntry *n = Nodes.find(nodeId);
        char nodeIdText[12];
        snprintf(nodeIdText, sizeof(nodeIdText), "!%08lX", (unsigned long)nodeId);

        lv_obj_t *row = lv_btn_create(s_dmNodePickerList);
        if (!row) {
            Serial.println("[dm] node picker row allocation failed");
            break;
        }
        #if defined(DEVICE_TDECK_PRO)
            lv_obj_remove_style_all(row);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        #endif
        lv_obj_set_width(row, lv_pct(97));
        lv_obj_set_height(row, 22);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 3, 0);
        lv_obj_set_style_pad_right(row, 3, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_border_width(row, selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    #else
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selected ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
        lv_obj_set_style_bg_color(row,
                                  selected ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                  0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_40, 0);
#endif
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onDmNodePressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);
#if UI_TOUCH_ONLY_PROFILE
        // Second half of the gesture, matching the USER button on this screen —
        // without it the picker's own "Tap a node to start a DM" was a promise
        // the row could not keep.
        lv_obj_add_event_cb(row,
                            [](lv_event_t *ev) { LV_UNUSED(ev); activateDmNodePickerSelection(); },
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
#endif

        lv_obj_t *lbl = lv_label_create(row);
        if (!lbl) {
            lv_obj_del(row);
            Serial.println("[dm] node picker label allocation failed");
            break;
        }
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, dmPickerTextColor, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        char rowText[64];
        const char *longDisp = (n && n->longName[0]) ? n->longName : "(unknown)";
        const char *shortDisp = (n && liveShortNameUsable(n->shortName)) ? n->shortName
                                                                         : nodeIdText;
        snprintf(rowText,
             sizeof(rowText),
             "%s%s (%s)",
             (n && n->favorite) ? "* " : "",
             longDisp,
             shortDisp);
        setLabelTextEmojiSafe(lbl, rowText);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        if (s_dmNodePickerRowCount < MAX_NODES) {
            s_dmNodePickerRows[s_dmNodePickerRowCount] = row;
            s_dmNodePickerRenderedIdx[s_dmNodePickerRowCount] = i;
            s_dmNodePickerRowCount++;
        }
    }

    // The picker never did this, so on a list taller than the panel the
    // highlight simply walked off the bottom edge and out of sight.
    if (lv_obj_t *selRow = selectedDmNodePickerRowObj()) {
        lv_obj_scroll_to_view(selRow, LV_ANIM_OFF);
    }
}

static void openDmNodePicker() {
    if (!s_dmModal || s_dmNodePickerModal) return;

    snapshotNodesForDmPicker();

    int modalW = lv_disp_get_hor_res(NULL) - 20;
    int modalH = lv_disp_get_ver_res(NULL) - 24;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;
    if (modalH < 100) modalH = lv_disp_get_ver_res(NULL) - 8;

    s_dmNodePickerModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_dmNodePickerModal, modalW, modalH);
    lv_obj_align(s_dmNodePickerModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_dmNodePickerModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_dmNodePickerModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_dmNodePickerModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dmNodePickerModal, 1, 0);
    lv_obj_set_style_border_color(s_dmNodePickerModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_dmNodePickerModal, 4, 0);
    lv_obj_set_style_pad_row(s_dmNodePickerModal, 4, 0);
    lv_obj_set_flex_flow(s_dmNodePickerModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmNodePickerModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_dmNodePickerModal);
    s_dmNodePickerTitle = title;
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(
        title,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
        0);
    lv_label_set_text(title, TR("New DM: Select Node"));

    s_dmNodePickerList = lv_obj_create(s_dmNodePickerModal);
    lv_obj_set_width(s_dmNodePickerList, lv_pct(100));
    lv_obj_set_flex_grow(s_dmNodePickerList, 1);
    lv_obj_add_flag(s_dmNodePickerList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_dmNodePickerList);
    lv_obj_set_scrollbar_mode(s_dmNodePickerList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_dmNodePickerList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dmNodePickerList, 0, 0);
    lv_obj_set_style_pad_all(s_dmNodePickerList, 0, 0);
    lv_obj_set_style_pad_right(s_dmNodePickerList, 6, 0);
    lv_obj_set_style_pad_row(s_dmNodePickerList, 2, 0);
    lv_obj_set_style_width(s_dmNodePickerList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_dmNodePickerList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_dmNodePickerList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_dmNodePickerList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_dmNodePickerList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmNodePickerList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *hint = lv_label_create(s_dmNodePickerModal);
    s_dmNodePickerHint = hint;
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(
        hint,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF),
        0);
#if UI_CARDPUTER_COMPACT
    lv_label_set_text(hint, TR("Type = Filter   Enter = Open DM   Esc = Back"));
#elif UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(hint, TR("Tap a node to start a DM"));
    reserveHeltecCloseXRow(title);
    appendHeltecCloseX(s_dmNodePickerModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeDmNodePicker(); });
#else
    lv_label_set_text(hint, TR("Type = Filter   Enter = Open DM   Bksp = Back"));
#endif

    refreshDmNodePicker(true);
}

static void closeDmNodePicker() {
    lvObjDeleteSafe(s_dmNodePickerModal);
    s_dmNodePickerList = nullptr;
    s_dmNodePickerTitle = nullptr;
    s_dmNodePickerHint = nullptr;
    s_dmNodeSnapshotCount = 0;
    s_dmNodeFilteredCount = 0;
    s_dmNodeSelection = -1;
    s_dmNodeFilterOpen = false;
    s_dmNodeFilterLen = 0;
    s_dmNodeFilter[0] = '\0';
    s_dmNodePickerRenderStart = 0;
    s_dmNodePickerRowCount = 0;
    memset(s_dmNodeSnapshotIds, 0, sizeof(s_dmNodeSnapshotIds));
    memset(s_dmNodeFilteredIdx, 0, sizeof(s_dmNodeFilteredIdx));
    memset(s_dmNodePickerRows, 0, sizeof(s_dmNodePickerRows));
    memset(s_dmNodePickerRenderedIdx, 0, sizeof(s_dmNodePickerRenderedIdx));
}

// ── DM bubble-style helpers ──────────────────────────────────────────────────
// Our own DM lines are written by DmMgr with a "<me> " prefix; received lines
// use "[name] ". Only the leading prefix window is scanned so body text
// containing "<me>" can't flip a bubble's alignment.
static bool dmLineIsFromMe(const char *line) {
    if (!line) return false;
    const int kWindow = 28;
    for (const char *p = line; *p && (int)(p - line) < kWindow; p++) {
        if (p[0] == ']') return false;   // received prefix closed first
        if (p[0] == '<' && p[1] == 'm' && p[2] == 'e' && p[3] == '>') return true;
    }
    return false;
}

static DisplayLine::AckState dmAckToDisplayAck(DmLine::AckState a) {
    switch (a) {
        case DmLine::PENDING:     return DisplayLine::PENDING;
        case DmLine::ACKED:       return DisplayLine::ACKED;
        case DmLine::ACKED_RELAY: return DisplayLine::ACKED_RELAY;
        case DmLine::NAKED:       return DisplayLine::NAKED;
        case DmLine::TX_FAILED:   return DisplayLine::TX_FAILED;
        default:                  return DisplayLine::NONE;
    }
}

static void refreshDmModal(bool force) {
#if UI_TOUCH_ONLY_PROFILE
    // A DM arriving mid-gesture used to rebuild the list under the finger,
    // which cancelled whatever the finger was doing — a tap, or a hold that was
    // three seconds into a delete. Hold the redraw until the gesture ends.
    if (s_dmRowPressActive && lvObjAlive(s_dmModal)) {
        s_dmRefreshPending = true;
        return;
    }
#endif
    if (!lvObjAlive(s_dmModal)
        || !lvObjAlive(s_dmConvList)
        || !lvObjAlive(s_dmMsgList)) {
        s_dmModal = nullptr;
        s_dmConvPanel = nullptr;
        s_dmConvList = nullptr;
        s_dmMsgPanel = nullptr;
        s_dmMsgList = nullptr;
        s_dmHintLabel = nullptr;
        s_dmDrawerScrim = nullptr;
        s_dmHeaderTitle = nullptr;
        return;
    }

    uint32_t now = millis();
    if (s_dmDeleteFlashUntilMs != 0 && (int32_t)(now - s_dmDeleteFlashUntilMs) > 0) {
        s_dmDeleteFlashMsg[0] = '\0';
        s_dmDeleteFlashUntilMs = 0;
        force = true;
    }

#if defined(DEVICE_TDECK_PRO)
    const lv_color_t dmPanelTextColor = lv_color_make(0, 0, 0);
#else
    const lv_color_t dmPanelTextColor =
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF);
#endif
    const uint16_t dmMessageBaseColor =
        (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x1B, 0x24, 0x3D) : TFT_WHITE;

    uint32_t selectedNodeIdBefore = 0;
    if (s_dmSelection > 0) {
        int prevConvIdx = s_dmSelection - 1;
        if (prevConvIdx >= 0 && prevConvIdx < s_dmConvCount) {
            selectedNodeIdBefore = s_dmConvNodeIds[prevConvIdx];
        }
    }

    memset(s_dmConvNodeIds, 0, sizeof(s_dmConvNodeIds));
    s_dmConvCount = 0;

    int rankedCount = DMs.count();
    for (int i = 0; i < rankedCount && s_dmConvCount < MAX_DM_CONVS; i++) {
        DmConv *c = DMs.getByRank(i);
        if (!c) continue;
        s_dmConvNodeIds[s_dmConvCount++] = c->nodeId;
    }

    int totalRows = s_dmConvCount + 1;  // +1 for the "New DM" row
    if (totalRows <= 0) {
        s_dmSelection = -1;
    } else {
        int selectedIdx = 0;
        for (int i = 0; i < s_dmConvCount; i++) {
            if (s_dmConvNodeIds[i] == selectedNodeIdBefore) {
                selectedIdx = i + 1;
                break;
            }
        }
        if (selectedNodeIdBefore == 0 && s_dmSelection >= 0) {
            selectedIdx = s_dmSelection;
        }
        if (selectedIdx < 0) selectedIdx = 0;
        if (selectedIdx >= totalRows) selectedIdx = totalRows - 1;
        s_dmSelection = selectedIdx;
    }

    DmConv *selected = selectedDmConversation();
    if (selected) {
        DMs.markRead(selected->nodeId);
    } else {
        s_dmMsgPanelFocused = false;
    }

    int selectedMsgCount = selected ? selected->count : -1;
    uint32_t selectedNodeId = selected ? selected->nodeId : 0;
    int unreadTotal = DMs.unreadMessageCount();

    const bool selectedConvChanged = (s_dmRenderedNodeId != selectedNodeId);
    const bool selectedMsgIncreased =
        (selectedNodeId != 0)
        && (s_dmRenderedNodeId == selectedNodeId)
        && (s_dmRenderedMsgCount >= 0)
        && (selectedMsgCount > s_dmRenderedMsgCount);
    const bool autoScrollToLatest = selectedConvChanged || selectedMsgIncreased;

    if (!force
        && s_dmRenderedConvCount == s_dmConvCount
        && s_dmRenderedNodeId == selectedNodeId
        && s_dmRenderedMsgCount == selectedMsgCount
        && s_dmRenderedUnreadTotal == unreadTotal) {
        return;
    }

    // Cleared with the rows, not before the early-out above: the drawer reads
    // them between rebuilds to scroll the selection into view (dmDrawerSlide()).
    memset(s_dmConvRows, 0, sizeof(s_dmConvRows));
    lv_obj_clean(s_dmConvList);
    freshDotsForget(s_dmDots, openDmPeerNodeId());
    lv_obj_clean(s_dmMsgList);

    const lv_font_t *dmListFont = kMainScreenFont;
    const lv_font_t *dmMsgFont = scaledChatFont(kMainScreenFont);
#if defined(DEVICE_TLORA_PAGER_TFT)
    int dmListRowH = 24;
#elif defined(DEVICE_CROWPANEL_50)
    int dmListRowH = 40;   // its larger list font (kMainScreenFont)
#else
    int dmListRowH = 22;
#endif
#if defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
    // Portrait gives the contact list the whole width of a tall panel, which
    // the main-screen size left mostly empty. Two sizes up, rows to match.
    if (uiPortrait()) {
        dmListFont = &lv_font_montserrat_16;
        dmListRowH = 30;
    }
#endif

    {
        bool selectedRow = (s_dmSelection == 0);
        lv_obj_t *row = lv_btn_create(s_dmConvList);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        lv_obj_set_width(row, lv_pct(97));
        lv_obj_set_height(row, dmListRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 3, 0);
        lv_obj_set_style_pad_right(row, 3, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_border_width(row, selectedRow ? 2 : 0, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    #else
        lv_obj_set_style_border_width(row, selectedRow ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selectedRow ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
        lv_obj_set_style_bg_color(row,
                                  selectedRow ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                  0);
        lv_obj_set_style_bg_opa(row, selectedRow ? LV_OPA_70 : LV_OPA_40, 0);
#endif
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onDmConversationPressed, LV_EVENT_PRESSED, (void *)(intptr_t)0);
#if UI_TOUCH_ONLY_PROFILE
        // Tap runs it, the same as the USER button does on this row. On CLICKED,
        // so dragging the conversation list past it only scrolls.
        //
        // The conversation rows below deliberately do not get this: tapping one
        // focuses its messages, which is what the *first* USER press does there.
        // Activating on tap as well would mean a single tap opened the composer
        // instead of the thread you wanted to read.
        lv_obj_add_event_cb(row,
                            [](lv_event_t *ev) { LV_UNUSED(ev); activateDmSelection(); },
                            LV_EVENT_CLICKED, (void *)(intptr_t)0);
#endif
        lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_PRESSED, (void *)(intptr_t)0);
        lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_PRESSING, (void *)(intptr_t)0);
        lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_RELEASED, (void *)(intptr_t)0);
        lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_PRESS_LOST, (void *)(intptr_t)0);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, dmListFont, 0);
        lv_obj_set_style_text_color(lbl, dmPanelTextColor, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl, TR("New DM"));
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    if (s_dmConvCount > 0) {
        for (int i = 0; i < s_dmConvCount; i++) {
            DmConv *c = DMs.find(s_dmConvNodeIds[i]);
            if (!c) continue;

            int rowIdx = i + 1;
            bool selectedRow = (rowIdx == s_dmSelection);
            lv_obj_t *row = lv_btn_create(s_dmConvList);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_remove_style_all(row);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
#endif
            s_dmConvRows[i] = row;
            lv_obj_set_width(row, lv_pct(97));
            lv_obj_set_height(row, dmListRowH);
            lv_obj_set_style_radius(row, 4, 0);
            lv_obj_set_style_pad_left(row, 3, 0);
            lv_obj_set_style_pad_right(row, 3, 0);
            lv_obj_set_style_pad_top(row, 1, 0);
            lv_obj_set_style_pad_bottom(row, 1, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_border_width(row, selectedRow ? 2 : 0, 0);
            lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
#else
            lv_obj_set_style_border_width(row, selectedRow ? 2 : 1, 0);
            lv_obj_set_style_border_color(row,
                                          selectedRow ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                          0);
            lv_obj_set_style_bg_color(row,
                                      selectedRow ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                      0);
            lv_obj_set_style_bg_opa(row, selectedRow ? LV_OPA_70 : LV_OPA_40, 0);
#endif
            lv_obj_set_style_shadow_width(row, 0, 0);
            // In the drawer a tap on a row also shuts it, so it waits for the
            // tap to finish: on PRESSED, dragging the list to scroll it would
            // pick whichever row the finger landed on and slam the drawer.
            lv_obj_add_event_cb(row, onDmConversationPressed,
                                dmListIsDrawer() ? LV_EVENT_CLICKED : LV_EVENT_PRESSED,
                                (void *)(intptr_t)rowIdx);
            lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_PRESSED, (void *)(intptr_t)rowIdx);
            lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_PRESSING, (void *)(intptr_t)rowIdx);
            lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_RELEASED, (void *)(intptr_t)rowIdx);
            lv_obj_add_event_cb(row, onDmConversationPressState, LV_EVENT_PRESS_LOST, (void *)(intptr_t)rowIdx);

            lv_obj_t *lbl = lv_label_create(row);
            lv_obj_set_width(lbl, lv_pct(100));
            lv_obj_set_style_text_font(lbl, dmListFont, 0);
            lv_obj_set_style_text_color(lbl, dmPanelTextColor, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

            char name[20];
            if (liveShortNameUsable(c->shortName)) {
                snprintf(name, sizeof(name), "%s", c->shortName);
            } else {
                snprintf(name, sizeof(name), "!%08lX", (unsigned long)c->nodeId);
            }

            char rowText[48];
            NodeEntry *n = Nodes.find(c->nodeId);
            const bool favorite = (n && n->favorite);
            if (c->unreadCount > 0 && c->nodeId != selectedNodeId) {
                snprintf(rowText,
                         sizeof(rowText),
                         "%s%s (%u)",
                         favorite ? "* " : "",
                         name,
                         (unsigned)c->unreadCount);
            } else {
                snprintf(rowText,
                         sizeof(rowText),
                         "%s%s",
                         favorite ? "* " : "",
                         name);
            }
            setLabelTextEmojiSafe(lbl, rowText);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        }
    }

    selected = selectedDmConversation();
    if (!selected) {
        lv_obj_t *empty = lv_label_create(s_dmMsgList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, dmMsgFont, 0);
        lv_obj_set_style_text_color(empty, dmPanelTextColor, 0);
        // The message panel says what it is waiting for, not which key to
        // press. The old text on the s_dmSelection == 0 branch taught a
        // keybinding in the space messages occupy — and named the wrong key on
        // builds where Esc closes modals (kModalCloseUsesEscape), which is
        // every Cardputer.
        lv_label_set_text(empty, TR("Select a conversation"));
    } else {
        const DmLine *renderRows[MAX_DM_LINES] = {};
        int rowCount = 0;
        lv_obj_t *lastMsgObj = nullptr;
        for (int row = 0; row < MAX_DM_LINES; row++) {
            const DmLine *dl = DMs.getLine(selected, row, MAX_DM_LINES);
            if (!dl) break;
            renderRows[rowCount++] = dl;
        }

        chatBubbleBeginRender(s_dmMsgList);
        uint32_t lastDateBucket = 0;
        for (int row = rowCount - 1; row >= 0; row--) {
            const DmLine *dl = renderRows[row];
            if (!dl) continue;

            // MECP draws decoded (issue #158); the stored line keeps the wire text.
            char dmShown[384];
            const char *dmText = dl->text;
            {
                utf8util::copyTruncate(dmShown, sizeof(dmShown), dl->text);
                if (mecpDecodeLineInPlace(dmShown, sizeof(dmShown),
                                          (size_t)(chatStripPrefix(dl->text) - dl->text)))
                    dmText = dmShown;
            }

            // Insert a date marker before the first DM that lands on a new
            // local calendar day. DM lines store one logical message each, so
            // every line is a "first of group" candidate.
            uint32_t curBucket = chatDateBucket(dl->epoch);
            if (curBucket != 0 && curBucket != lastDateBucket) {
                insertChatDateMarker(s_dmMsgList, dl->epoch, dmMsgFont);
                lastDateBucket = curBucket;
            }

            // IRC style: the channel view's rows. Unlike bubbles, each one is
            // named, the peer included -- left-aligned rows have nothing else
            // to say which side of the conversation a line came from.
            const bool freshDot = dl->fresh && freshDotRoom(s_dmDots);
            lv_obj_t *dot = nullptr;
            if (s_cfg.chatStyle == CHAT_STYLE_IRC) {
                const bool isMe = dmLineIsFromMe(dl->text);
                const uint32_t who = isMe ? s_myNodeId : selected->nodeId;
                char dmTimeBuf[LIVE_CLOCK_BUF], dmName[48] = "";
                chatParsePrefix(dl->text, nullptr, 0, dmTimeBuf, sizeof(dmTimeBuf));
                if (!dmTimeBuf[0]) formatChatClock(dl->epoch, dmTimeBuf, sizeof(dmTimeBuf));
                if (who != 0 && !isMe) chatSenderLabel(who, dmName, sizeof(dmName));
                chatMakeIrcRow(s_dmMsgList, dmMsgFont, who, isMe,
                               dmTimeBuf, dmName, chatStripPrefix(dmText),
                               dmAckToDisplayAck(dl->ack),
                               0, false, &lastMsgObj, nullptr, nullptr,
                               freshDot ? &dot : nullptr);
                freshDotTrack(s_dmDots, dot, FRESH_DOT_SLOT);
                continue;
            }

            // Bubbles style: reuse the channel-chat bubble renderer. Our lines
            // sit right-aligned in the accent/ack color, the peer's left-aligned
            // in their stable per-node color. No name tag (1:1 conversation, the
            // peer is already named in the panel header) and no press handler
            // (DMs have no reply/selection model).
            if (chatStyleUsesBubbles(s_cfg.chatStyle)) {
                const bool isMe = dmLineIsFromMe(dl->text);
                char dmIconBuf[12], dmTimeBuf[LIVE_CLOCK_BUF], dmMetaBuf[28];
                chatParsePrefix(dl->text, dmIconBuf, sizeof(dmIconBuf),
                                dmTimeBuf, sizeof(dmTimeBuf));
                if (!dmTimeBuf[0]) formatChatClock(dl->epoch, dmTimeBuf, sizeof(dmTimeBuf));
                chatComposeBubbleMeta(dmIconBuf, dmTimeBuf, dmMetaBuf, sizeof(dmMetaBuf));
                chatMakeBubble(s_dmMsgList,
                               isMe ? 0u : selected->nodeId,
                               isMe,
                               dmMetaBuf,
                               nullptr,
                               chatStripPrefix(dmText),
                               dmAckToDisplayAck(dl->ack),
                               0,
                               false,
                               &lastMsgObj,
                               nullptr,
                               nullptr,
                               freshDot ? &dot : nullptr);
                freshDotTrack(s_dmDots, dot, FRESH_DOT_OBJ);
                continue;
            }

            // The same marker the classic channel view carries. Color alone is
            // the weaker signal of the two: it competes with the per-node line
            // colors, and a theme where green reads close to the base text
            // color loses it entirely. A marked line is a span group, for the
            // marker's smaller face.
            const bool ackedLine = (dl->ack == DmLine::ACKED);
#if defined(DEVICE_TDECK_PRO)
            // Every line on the T-Deck Pro, for the bold sender tag.
            const bool spanLine = true;
#else
            // And a new message, for its dot.
            const bool spanLine = ackedLine || freshDot;
#endif
            lv_obj_t *msg = spanLine ? lv_spangroup_create(s_dmMsgList)
                                     : lv_label_create(s_dmMsgList);
            // A span group is clickable by default, a label is not; DM lines
            // have no press handler, so keep them inert as labels were.
            if (spanLine) lv_obj_remove_flag(msg, LV_OBJ_FLAG_CLICKABLE);
            lastMsgObj = msg;
            lv_obj_set_width(msg, lv_pct(100));
            lv_obj_set_style_text_font(msg, dmMsgFont, 0);
            lv_obj_set_style_pad_left(msg, 2, 0);
            lv_obj_set_style_pad_right(msg, 4, 0);
            lv_obj_set_style_pad_top(msg, 0, 0);
            lv_obj_set_style_pad_bottom(msg, 0, 0);
            // DmMgr now stores one logical message per DmLine; let LVGL wrap it
            // to the actual pane pixel width so font metrics drive line breaks.
            if (!spanLine) lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

            uint16_t lineColor = dl->color;
            switch (dl->ack) {
                case DmLine::ACKED:
                    lineColor = (s_cfg.uiMode == UI_MODE_LIGHT) ? (uint16_t)0x0320 : TFT_GREEN;
                    break;
                case DmLine::ACKED_RELAY:
                    lineColor = userMessageAccentColor565();
                    break;
                case DmLine::NAKED:
                case DmLine::TX_FAILED:
                    lineColor = TFT_RED;
                    break;
                default:
                    break;
            }

            if (s_cfg.uiMode == UI_MODE_LIGHT && lineColor == TFT_WHITE) {
                lineColor = dmMessageBaseColor;
            }
#if defined(DEVICE_TDECK_PRO)
            lineColor = TFT_BLACK;
#endif

            lv_obj_set_style_text_color(msg, tftColorToLv(lineColor), 0);
            stylePaperMessageRow(msg);
#if !defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
#endif
            lv_span_t *dotSpan = nullptr;
#if defined(DEVICE_TDECK_PRO)
            chatSetLineSpans(msg, dmText, dmMsgFont, ackedLine, chatNameFont(dmMsgFont),
                             freshDot ? &dotSpan : nullptr);
#else
            if (spanLine) {
                chatSetLineSpans(msg, dmText, dmMsgFont, ackedLine, nullptr,
                                 freshDot ? &dotSpan : nullptr);
            } else {
                setLabelTextEmojiSafe(msg, dmText);
            }
#endif
            if (dotSpan) freshDotTrack(s_dmDots, msg, FRESH_DOT_SPAN, dotSpan);
        }

        // Last in the thread so it sits under the newest message, where the
        // eye lands, and the autoscroll below brings it into view.
        if (Nodes.hasKeyConflict(selected->nodeId)) {
            lv_obj_t *warn = lv_label_create(s_dmMsgList);
            lv_obj_set_width(warn, lv_pct(100));
            lv_label_set_long_mode(warn, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(warn, dmMsgFont, 0);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_color(warn, tftColorToLv(TFT_BLACK), 0);
#else
            lv_obj_set_style_text_color(warn, tftColorToLv(TFT_ORANGE), 0);
#endif
            lv_label_set_text(warn, TR("! Key changed - review in Nodes. "
                                       "Messages still use the old key."));
            lastMsgObj = warn;
        }

        if (autoScrollToLatest && lastMsgObj) {
            // Two separate reasons this lands mid-conversation without help,
            // both of which the channel chat view already handles:
            //
            // 1. Wrapped message labels have no final height until layout runs,
            //    so scrolling against stale geometry aims at the wrong offset.
            // 2. scroll_to_view only guarantees the object is *visible* — for a
            //    tall last message it happily stops with that message's top edge
            //    at the viewport top, leaving the rest of it below the fold.
            lv_obj_update_layout(s_dmMsgList);
            lv_obj_scroll_to_view(lastMsgObj, LV_ANIM_OFF);
            int32_t bottomGap = lv_obj_get_scroll_bottom(s_dmMsgList);
            if (bottomGap > 0) {
                // Absolute position rather than lv_obj_scroll_by(): scroll_by's
                // dy is inverted (negative reveals content below), which is an
                // easy sign to get backwards.
                lv_obj_scroll_to_y(s_dmMsgList,
                                   lv_obj_get_scroll_y(s_dmMsgList) + bottomGap,
                                   LV_ANIM_OFF);
            }
        }

        if (rowCount == 0) {
            lv_obj_t *empty = lv_label_create(s_dmMsgList);
            lv_obj_set_width(empty, lv_pct(100));
            lv_obj_set_style_text_font(empty, dmMsgFont, 0);
            lv_obj_set_style_text_color(empty, dmPanelTextColor, 0);
            lv_label_set_text(empty, TR("No messages yet"));
        }
    }

    s_dmRenderedConvCount = s_dmConvCount;
    s_dmRenderedNodeId = selected ? selected->nodeId : 0;
    s_dmRenderedMsgCount = selected ? selected->count : -1;
    s_dmRenderedUnreadTotal = unreadTotal;

    if (s_dmHintLabel) {
        if (s_dmDeleteFlashMsg[0]) {
            lv_label_set_text(s_dmHintLabel, s_dmDeleteFlashMsg);
        } else {
            lv_label_set_text(s_dmHintLabel, dmHintText());
        }
#if UI_CARDPUTER_COMPACT
        // The line exists only while the delete flow is talking. Hiding rather
        // than blanking it hands the height back to the conversation list,
        // which is the flex-grow child above.
        const char *hintTxt = lv_label_get_text(s_dmHintLabel);
        if (hintTxt && hintTxt[0]) lv_obj_clear_flag(s_dmHintLabel, LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_add_flag(s_dmHintLabel, LV_OBJ_FLAG_HIDDEN);
#endif
    }

    dmRefreshHeaderTitle(selected);
    refreshDmPanelFocusStyles();
}

static void openDmModal() {
    if (s_dmModal && !lvObjAlive(s_dmModal)) {
        s_dmModal = nullptr;
        s_dmConvPanel = nullptr;
        s_dmConvList = nullptr;
        s_dmMsgPanel = nullptr;
        s_dmMsgList = nullptr;
        s_dmConvTitle = nullptr;
        s_dmMsgTitle = nullptr;
        s_dmHintLabel = nullptr;
        s_dmDrawerScrim = nullptr;
        s_dmHeaderTitle = nullptr;
        s_dmDrawerOpen = false;
        s_dmDrawerAnimReady = false;
    }
    if (!s_rootScreen || s_dmModal) return;
    if (s_composeModal) closeComposePrompt();
    closeLiveModal();
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

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);
    // s_dmModal has no border and pad_all=4, so usable content width is modalW - 2*4.
    // Less the nav column where the bar is one (Left/Right): the panes are
    // sized absolutely, and appendHeltecBottomNav() reserves it as padding.
    int contentW = modalW - 8 - (navIsSideColumn() ? kSideNavW : 0);
    // Where the conversations are a drawer, the messages have the whole width
    // and the list slides in over them from the left (dmDrawerSlide()).
    const bool dmDrawer = dmListIsDrawer();
    // Conversations over messages, both full width, instead of side by side:
    // the P4 held upright is 284 px wide -- too narrow to split -- and has the
    // height to stack into, the same way its Nodes screen does. The Heltec V4
    // and the Wio Tracker upright (240 px) take the same layout.
#if defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_HELTEC_V4_EXPANSION) \
    || defined(DEVICE_WIO_TRACKER_L2)
    const bool dmStacked = !dmDrawer && uiPortrait();
#else
    const bool dmStacked = false;
#endif
    // The drawer is as wide as the channel drawer (buildUi()).
#if defined(DEVICE_CROWPANEL_50)
    const int drawerW = min(max(contentW / 2, 120), 420);
#else
    const int drawerW = min(max(contentW / 2, 120), 260);
#endif
    int leftW = dmDrawer ? drawerW : (dmStacked ? contentW : max(96, (contentW * 38) / 100));
    // Side by side: a 3 px gap either side of the 1 px rule between the panes.
    int rightW = (dmDrawer || dmStacked) ? contentW : (contentW - leftW - 7);

    s_dmModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_dmModal, modalW, modalH);
    lv_obj_align(s_dmModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_dmModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_dmModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_dmModal, LV_OPA_COVER, 0);
    // No border: this screen fills the display, and the 1 px frame only took
    // room from it. The headers and nav bar read the width, so they follow.
    lv_obj_set_style_border_width(s_dmModal, 0, 0);
    lv_obj_set_style_pad_all(s_dmModal, 4, 0);
    lv_obj_set_style_pad_row(s_dmModal, 4, 0);
    lv_obj_set_flex_flow(s_dmModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *dmTitle = buildScreenHeader(s_dmModal, TR("Direct Messages"));
    if (dmDrawer) {
        // Retitled with the conversation being read (dmRefreshHeaderTitle()).
        s_dmHeaderTitle = dmTitle;
#if HAS_TOUCH
        // A tap on the name opens the drawer, as on the chat's channel name.
        if (dmTitle) {
            lv_obj_add_flag(dmTitle, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(dmTitle, 6);
            lv_obj_add_event_cb(dmTitle, onDmHeaderTitlePressed, LV_EVENT_CLICKED, nullptr);
        }
#endif
    }

    lv_obj_t *content = lv_obj_create(s_dmModal);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    // The rule between the panes reaches past this box to the display edges
    // when they are stacked (see below).
    lv_obj_add_flag(content, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    if (dmStacked) {
        lv_obj_set_style_pad_row(content, 3, 0);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    } else {
        lv_obj_set_style_pad_column(content, 3, 0);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    }
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *leftPanel = nullptr;
    if (dmDrawer) {
        // Behind the drawer and over everything else on the screen, the header
        // and nav bar included: a tap anywhere off the list shuts it. Built
        // first so the drawer, raised after it on every open, sits on top. No
        // dim on the Pro, where a partial opacity dithers. As the channel
        // drawer's (buildUi()).
        s_dmDrawerScrim = lv_obj_create(s_dmModal);
        lv_obj_remove_style_all(s_dmDrawerScrim);
        lv_obj_add_flag(s_dmDrawerScrim, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(s_dmDrawerScrim, modalW, modalH);
        lv_obj_set_style_bg_color(s_dmDrawerScrim, lv_color_black(), 0);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(s_dmDrawerScrim, LV_OPA_TRANSP, 0);
#else
        lv_obj_set_style_bg_opa(s_dmDrawerScrim, LV_OPA_40, 0);
#endif
        lv_obj_add_flag(s_dmDrawerScrim,
                        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
        lv_obj_clear_flag(s_dmDrawerScrim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_dmDrawerScrim, onDmDrawerScrimPressed, LV_EVENT_CLICKED, nullptr);

        // The drawer: out of the screen's flex column, hidden until it opens,
        // and placed by dmDrawerSlide() each time it moves. Styled as the
        // channel drawer is.
        leftPanel = lv_obj_create(s_dmModal);
        lv_obj_add_flag(leftPanel,
                        (lv_obj_flag_t)(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN));
        lv_obj_set_size(leftPanel, leftW, lv_pct(100));
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_color(leftPanel, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_bg_opa(leftPanel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(leftPanel, 2, 0);
        lv_obj_set_style_border_color(leftPanel, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_border_side(leftPanel,
            (lv_border_side_t)(LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), 0);
#else
        lv_obj_set_style_bg_color(leftPanel, lv_color_hex(0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(leftPanel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(leftPanel, 1, 0);
        lv_obj_set_style_border_color(leftPanel, lv_color_hex(0x335D9D), 0);
        lv_obj_set_style_border_side(leftPanel, LV_BORDER_SIDE_RIGHT, 0);
#endif
        lv_obj_set_style_radius(leftPanel, 0, 0);
        lv_obj_set_style_pad_all(leftPanel, 4, 0);
    } else {
        leftPanel = lv_obj_create(content);
        lv_obj_set_width(leftPanel, leftW);
        // Stacked: the conversation list takes the top 40%, the messages the rest.
        lv_obj_set_height(leftPanel, dmStacked ? lv_pct(40) : lv_pct(100));
        // No box round either pane: a single rule between them does the dividing.
        lv_obj_set_style_bg_opa(leftPanel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(leftPanel, 0, 0);
        lv_obj_set_style_pad_all(leftPanel, 2, 0);
    }
    s_dmConvPanel = leftPanel;
    lv_obj_clear_flag(leftPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(leftPanel, 2, 0);
    lv_obj_set_flex_flow(leftPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // The drawer needs no title: being open says the list has the keys. The
    // panes' titles are what say which one has them (refreshDmPanelFocusStyles()).
    if (!dmDrawer) {
        lv_obj_t *leftTitle = lv_label_create(leftPanel);
        lv_obj_set_width(leftTitle, lv_pct(100));
#if defined(DEVICE_CROWPANEL_50)
        lv_obj_set_style_text_font(leftTitle, &lv_font_montserrat_14, 0);   // drawn as 16
#else
        lv_obj_set_style_text_font(leftTitle, &lv_font_montserrat_10, 0);
#endif
        lv_obj_set_style_text_color(
            leftTitle,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
            0);
        lv_label_set_text(leftTitle, TR("Conversations"));
        s_dmConvTitle = leftTitle;
    }

    s_dmConvList = lv_obj_create(leftPanel);
    lv_obj_set_width(s_dmConvList, lv_pct(100));
    lv_obj_set_flex_grow(s_dmConvList, 1);
    lv_obj_add_flag(s_dmConvList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_dmConvList);
    lv_obj_set_scrollbar_mode(s_dmConvList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_dmConvList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dmConvList, 0, 0);
    lv_obj_set_style_pad_all(s_dmConvList, 0, 0);
    lv_obj_set_style_pad_right(s_dmConvList, 6, 0);
    lv_obj_set_style_pad_row(s_dmConvList, 2, 0);
    lv_obj_set_style_width(s_dmConvList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_dmConvList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_dmConvList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_dmConvList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_dmConvList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmConvList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    if (!dmDrawer) buildPaneRule(content, dmStacked);

    lv_obj_t *rightPanel = lv_obj_create(content);
    s_dmMsgPanel = rightPanel;
    lv_obj_set_width(rightPanel, rightW);
    if (dmStacked) {
        lv_obj_set_flex_grow(rightPanel, 1);
    } else {
        lv_obj_set_height(rightPanel, lv_pct(100));
    }
    lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(rightPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightPanel, 0, 0);
    lv_obj_set_style_pad_all(rightPanel, 2, 0);
    lv_obj_set_style_pad_row(rightPanel, 2, 0);
    lv_obj_set_flex_flow(rightPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // The header names the conversation where the list is a drawer.
    if (!dmDrawer) {
        lv_obj_t *rightTitle = lv_label_create(rightPanel);
        lv_obj_set_width(rightTitle, lv_pct(100));
#if defined(DEVICE_CROWPANEL_50)
        lv_obj_set_style_text_font(rightTitle, &lv_font_montserrat_14, 0);   // drawn as 16
#else
        lv_obj_set_style_text_font(rightTitle, &lv_font_montserrat_10, 0);
#endif
        lv_obj_set_style_text_color(
            rightTitle,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
            0);
        lv_label_set_text(rightTitle, TR("Messages"));
        s_dmMsgTitle = rightTitle;
    }

    s_dmMsgList = lv_obj_create(rightPanel);
    lv_obj_set_width(s_dmMsgList, lv_pct(100));
    lv_obj_set_flex_grow(s_dmMsgList, 1);
    lv_obj_add_flag(s_dmMsgList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_dmMsgList);
    lv_obj_set_scrollbar_mode(s_dmMsgList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_dmMsgList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dmMsgList, 0, 0);
    lv_obj_set_style_pad_all(s_dmMsgList, 0, 0);
    lv_obj_set_style_pad_right(s_dmMsgList, 6, 0);
    lv_obj_set_style_pad_row(s_dmMsgList, 1, 0);
    lv_obj_set_style_width(s_dmMsgList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_dmMsgList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_dmMsgList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_dmMsgList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_dmMsgList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmMsgList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

#if UI_TOUCH_ONLY_PROFILE
    // Sits under the messages, in the panel it writes to. Reading a thread and
    // answering it are the same errand, and on a touch-only build there was no
    // way to start the second half from here at all.
    //
    // Outside the message list, so the rebuild that redraws the thread on every
    // new message leaves it alone.
    lv_obj_t *dmNewMsgBtn = lv_btn_create(rightPanel);
    lv_obj_set_width(dmNewMsgBtn, lv_pct(100));
#if defined(DEVICE_CROWPANEL_50)
    lv_obj_set_height(dmNewMsgBtn, 44);   // as the chat's, doubled
#else
    lv_obj_set_height(dmNewMsgBtn, 24);
#endif
    lv_obj_set_style_radius(dmNewMsgBtn, 4, 0);
    lv_obj_set_style_pad_all(dmNewMsgBtn, 0, 0);
    lv_obj_set_style_shadow_width(dmNewMsgBtn, 0, 0);
    lv_obj_set_style_bg_color(dmNewMsgBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(dmNewMsgBtn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(dmNewMsgBtn, 1, 0);
    lv_obj_set_style_border_color(dmNewMsgBtn, lv_color_hex(0x8FB5E6), 0);
    lv_obj_add_event_cb(dmNewMsgBtn, onDmNewMessagePressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *dmNewMsgLbl = lv_label_create(dmNewMsgBtn);
#if defined(DEVICE_CROWPANEL_50)
    lv_obj_set_style_text_font(dmNewMsgLbl, &lv_font_montserrat_18, 0);   // drawn as 20
#else
    lv_obj_set_style_text_font(dmNewMsgLbl, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_color(dmNewMsgLbl, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(dmNewMsgLbl, TR("New Message"));
    lv_obj_center(dmNewMsgLbl);
#endif

    lv_obj_t *hint = lv_label_create(s_dmModal);
    s_dmHintLabel = hint;
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(
        hint,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF),
        0);
    lv_label_set_text(hint, dmHintText());
#if UI_CARDPUTER_COMPACT
    // No static key legend on this build: a 240x135 panel cannot spare a
    // permanent line for it. The label itself stays, because the delete flow
    // has nowhere else to prompt — refreshDmModal() unhides it for "Confirm
    // delete" and the result, then hides it again. A hidden object is skipped
    // by the flex layout, so it costs no height while there is nothing to say.
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
#endif

    s_dmRenderedConvCount = -1;
    s_dmRenderedNodeId = 0;
    s_dmRenderedMsgCount = -1;
    s_dmRenderedUnreadTotal = -1;
    s_dmMsgPanelFocused = false;
    // With a drawer, the screen opens on the most recent conversation, the list
    // shut; only with none to show does it open on the list. (Row 0 is New DM.)
    s_dmDrawerOpen = false;
    s_dmDrawerAnimReady = false;
    if (dmDrawer && DMs.count() > 0) {
        s_dmSelection = 1;
        s_dmMsgPanelFocused = true;
    }

#if UI_TOUCH_NAV_BAR
    appendHeltecBottomNav(s_dmModal, HELTEC_NAV_DM);
#endif
    // After the nav bar, whose padding the drawer is placed against.
    refreshDmModal(true);
    s_dmDrawerAnimReady = true;
}

#if NODES_LAYOUT_WIDE
// The details pane: the placeholder label and the four section tables, every
// one of them registered in the s_nodes* pointers refreshNodesDetails() writes.
// On the Nodes screen beside or under the list; on the T-Deck Pro inside the
// Node Info modal instead (NODES_INFO_AS_MODAL). `growHeight` takes the
// parent's remaining height from a column flex, else the full height of a row.
static lv_obj_t *buildNodesInfoPanel(lv_obj_t *parent, int width, bool growHeight,
                                     const lv_font_t *font) {
    // On the T-Deck Pro this is only ever built into the Node Info modal, which
    // is ink on paper: every pale blue below would cross the 1 bpp threshold to
    // white and vanish into the modal's white face.
#if defined(DEVICE_TDECK_PRO)
    const lv_color_t valueInk = lv_color_make(0, 0, 0);
    const lv_color_t scrollInk = lv_color_make(0, 0, 0);
#else
    const lv_color_t valueInk = lv_color_hex(0xD9E8FF);
    const lv_color_t scrollInk = lv_color_hex(0x8FB5E6);
#endif
    lv_obj_t *infoPanel = lv_obj_create(parent);
    s_nodesInfoPanel = infoPanel;
    lv_obj_set_width(infoPanel, width);
    if (growHeight) {
        lv_obj_set_flex_grow(infoPanel, 1);
    } else {
        lv_obj_set_height(infoPanel, lv_pct(100));
    }
    lv_obj_add_flag(infoPanel, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(infoPanel);
    lv_obj_set_scrollbar_mode(infoPanel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(infoPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(infoPanel, 0, 0);
    lv_obj_set_style_pad_all(infoPanel, 4, 0);
    lv_obj_set_style_pad_row(infoPanel, 3, 0);
    lv_obj_set_style_width(infoPanel, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(infoPanel, scrollInk, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(infoPanel, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(infoPanel, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(infoPanel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(infoPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(infoPanel, 8, 0);

    // Shown only when there is nothing selected; the sections replace it as soon
    // as there is.
    s_nodesDetail = lv_label_create(infoPanel);
    lv_obj_set_width(s_nodesDetail, lv_pct(100));
    lv_obj_set_style_text_font(s_nodesDetail, font, 0);
    lv_obj_set_style_text_color(s_nodesDetail, valueInk, 0);
    lv_label_set_long_mode(s_nodesDetail, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_nodesDetail, "");

    // Section width: the Pager has room for two sections abreast, which is what
    // its old two-column detail split was for. Everywhere else they stack, and
    // the panel scrolls.
#if defined(DEVICE_TLORA_PAGER_TFT)
    const int32_t nodesSectionW = lv_pct(48);
#else
    const int32_t nodesSectionW = lv_pct(100);
#endif
    // Measured from the font rather than hardcoded per board: the widest field
    // name decides the column, so changing the font above can never again leave
    // the names clipped or the values starting halfway across the panel. The
    // value column takes whatever is left.
    int nodesKeyColW;
    {
        lv_point_t keySz;
        lv_text_get_size(&keySz, TR("Last heard"), font, 0, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        nodesKeyColW = keySz.x + 6;   // + the gutter to the values
    }
    static const char *kNodesSectionTitles[NODES_SEC_COUNT] = {
        TR("Identity"), TR("Link"), TR("Position"), TR("Telemetry")
    };
    for (int i = 0; i < NODES_SEC_COUNT; i++) {
        lv_obj_t *box = lv_obj_create(infoPanel);
        s_nodesSectionBox[i] = box;
        lv_obj_set_width(box, nodesSectionW);
        lv_obj_set_height(box, LV_SIZE_CONTENT);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_set_style_pad_top(box, (i == 0) ? 0 : 2, 0);
        lv_obj_set_style_pad_row(box, 1, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        lv_obj_t *hdr = lv_label_create(box);
        lv_obj_set_width(hdr, lv_pct(100));
        lv_obj_set_style_text_font(hdr, font, 0);
        // Amber against the pale-blue body, the same split Discovery and Beacons
        // use for a heading.
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(hdr, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(hdr, lv_color_hex(0xFFC98A), 0);
    #endif
        lv_obj_set_style_border_color(hdr, lv_color_hex(0x335D9D), 0);
        lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(hdr, 1, 0);
        lv_label_set_long_mode(hdr, LV_LABEL_LONG_CLIP);
        lv_label_set_text(hdr, kNodesSectionTitles[i]);

        lv_obj_t *table = lv_obj_create(box);
        lv_obj_set_width(table, lv_pct(100));
        lv_obj_set_height(table, LV_SIZE_CONTENT);
        lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(table, 0, 0);
        lv_obj_set_style_pad_all(table, 0, 0);
        lv_obj_set_style_pad_column(table, 4, 0);
        lv_obj_set_flex_flow(table, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        s_nodesSectionKeys[i] = lv_label_create(table);
        lv_obj_set_width(s_nodesSectionKeys[i], nodesKeyColW);
        lv_obj_set_style_text_font(s_nodesSectionKeys[i], font, 0);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_text_color(s_nodesSectionKeys[i], lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_text_color(s_nodesSectionKeys[i], lv_color_hex(0x9FBEEA), 0);
    #endif
        lv_label_set_long_mode(s_nodesSectionKeys[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_nodesSectionKeys[i], "");

        // LONG_DOT, never WRAP: a wrapped value would take two lines where its
        // field name takes one, and every row below it would stop lining up.
        s_nodesSectionVals[i] = lv_label_create(table);
        lv_obj_set_flex_grow(s_nodesSectionVals[i], 1);
        lv_obj_set_style_text_font(s_nodesSectionVals[i], font, 0);
        lv_obj_set_style_text_color(s_nodesSectionVals[i], valueInk, 0);
        lv_label_set_long_mode(s_nodesSectionVals[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(s_nodesSectionVals[i], "");
    }
    return infoPanel;
}
#endif  // NODES_LAYOUT_WIDE

#if NODES_INFO_AS_MODAL
static void closeNodesInfoDlg() {
    if (!s_nodesInfoDlg) return;
    lvObjDeleteSafe(s_nodesInfoDlg);
    // The sections went with it. refreshNodesDetails() finds the placeholder
    // gone and does nothing until the modal is opened again.
    s_nodesInfoPanel = nullptr;
    s_nodesDetail = nullptr;
    memset(s_nodesSectionBox, 0, sizeof(s_nodesSectionBox));
    memset(s_nodesSectionKeys, 0, sizeof(s_nodesSectionKeys));
    memset(s_nodesSectionVals, 0, sizeof(s_nodesSectionVals));
}

// Node Actions -> Node Info: the Nodes screen's detail sections for the node
// under the list cursor, in a modal the full height of the panel. The sections
// are the same objects the screen would have built (buildNodesInfoPanel()), so
// refreshNodesDetails() fills them, and keeps them current, unchanged.
static void openNodesInfoDlg() {
    if (!s_rootScreen || !s_nodesModal || s_nodesInfoDlg) return;
    // Archived rows too: refreshNodesDetails() gives them their record, and this
    // is the only place on this board it is shown (see the Enter handler).
    const NodeEntry *live = currentNodesSelection();
    const ArchivedNode *archived = live ? nullptr : currentNodesArchivedSelection();
    if (!live && !archived) return;
    const uint32_t nodeId = live ? live->nodeId : archived->nodeId;
    {
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 8192) {
            logLvglMemDiag("openNodesInfoDlg aborted (low LVGL mem)");
            return;
        }
    }

    const int modalW = lv_disp_get_hor_res(NULL) - 8;
    const int modalH = lv_disp_get_ver_res(NULL) - 8;
    const lv_color_t ink = lv_color_make(0, 0, 0);

    s_nodesInfoDlg = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodesInfoDlg, modalW, modalH);
    lv_obj_align(s_nodesInfoDlg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodesInfoDlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_nodesInfoDlg, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_bg_opa(s_nodesInfoDlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_nodesInfoDlg, ink, 0);
    lv_obj_set_style_border_width(s_nodesInfoDlg, 1, 0);
    lv_obj_set_style_radius(s_nodesInfoDlg, 0, 0);
    lv_obj_set_style_pad_all(s_nodesInfoDlg, 4, 0);
    lv_obj_set_style_pad_row(s_nodesInfoDlg, 3, 0);
    lv_obj_set_flex_flow(s_nodesInfoDlg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesInfoDlg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_nodesInfoDlg);

    lv_obj_t *title = lv_label_create(s_nodesInfoDlg);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, emojiFont(&lv_font_montserrat_14), 0);
    lv_obj_set_style_text_color(title, ink, 0);
    lv_obj_set_style_border_color(title, ink, 0);
    lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(title, 1, 0);
    lv_obj_set_style_pad_bottom(title, 2, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    {
        char who[48];
        char titleBuf[80];
        if (archived) {
            utf8util::copyTruncate(who, sizeof(who),
                                   archived->longName[0] ? archived->longName
                                                         : archived->shortName);
            if (!who[0]) snprintf(who, sizeof(who), "!%08lX", (unsigned long)nodeId);
        } else {
            nodesActionTitleLabel(nodeId, who, sizeof(who));
        }
        snprintf(titleBuf, sizeof(titleBuf), "%s: %s", TR("Node Info"), who);
        setLabelTextEmojiSafe(title, titleBuf);
    }

    // Content width less the modal's padding and border; the panel grows into
    // the height between the title and the hint, and scrolls.
    lv_obj_t *panel = buildNodesInfoPanel(s_nodesInfoDlg, modalW - 10,
                                          /*growHeight=*/true,
                                          emojiFont(&lv_font_montserrat_12));

    // A tap anywhere on it closes it; a drag scrolls the panel instead, and
    // LVGL sends no click for a press that became a scroll.
    auto onTap = [](lv_event_t *ev) { LV_UNUSED(ev); closeNodesInfoDlg(); };
    lv_obj_add_event_cb(s_nodesInfoDlg, onTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(panel, onTap, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *hint = lv_label_create(s_nodesInfoDlg);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, ink, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, TR("J/K = Scroll   %s / Tap = Close"), modalCloseKeyLabel());

    refreshNodesDetails();
}
#endif  // NODES_INFO_AS_MODAL

