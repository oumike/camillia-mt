// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: channel selector and screen header.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// Forces the next refreshChannelGlow() to re-assert every property.
//
// Needed because applyChannelButtonTheme() writes some of the same properties
// (border width and colour) from its own rules. Without this, a theme change
// would leave the buttons carrying that function's idea of the border while the
// cache still believed the glow's version was on screen — visible as a dropdown
// cursor that vanishes on a theme switch. Object-pointer changes are caught by
// the cache itself, so a UI rebuild does not need to call this.
static void invalidateChannelGlowPaint() {
    for (int i = 0; i < MESH_CHANNELS; i++) s_channelGlowPaint[i].valid = false;
    s_selectorGlowPaint.valid = false;
}

static void refreshChannelGlow(bool force) {
    uint32_t now = millis();
    if (!force && (uint32_t)(now - s_lastChannelGlowAnimMs) < 70) return;
    s_lastChannelGlowAnimMs = now;

    const uint32_t periodMs = 900;
    const uint32_t halfPeriodMs = periodMs / 2;
    uint32_t phase = now % periodMs;
    uint32_t tri = (phase <= halfPeriodMs) ? phase : (periodMs - phase);
    uint8_t pulseOpa = (uint8_t)(LV_OPA_20 + (tri * (LV_OPA_90 - LV_OPA_20)) / halfPeriodMs);
    uint8_t outlineW = (uint8_t)(1 + (tri * 3) / halfPeriodMs);
    uint8_t shadowW = (uint8_t)(2 + (tri * 7) / halfPeriodMs);
    bool anyUnread = false;

    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = s_channelBtns[i];
        if (!btn) continue;

        bool active = (i == s_activeChannel);
        bool animate = s_channelNeedsAttention[i] && !active;
    #if defined(DEVICE_TDECK_PRO)
        // E-paper has no glow animation; the trailing star still marks unread.
        animate = false;
    #endif
        // Any board with the overlay dropdown needs the cursor drawn — without
        // it the list opens with nothing highlighted, so up/down move an
        // invisible selection and the list looks inert even though it isn't.
        // The Pager has no dropdown, so there is no cursor to show there.
        bool dropdownCursor = UI_CHANNEL_LIST_DROPDOWN
                              && (isChannelDropdownVisible()
#if HAS_CHANNEL_PANEL_LOCK
                                  || channelColumnFocused()
#endif
                                  )
                              && s_cardputerDropdownSelection == i;
        anyUnread = anyUnread || animate;
        lv_obj_t *lbl = s_channelLabels[i];

        ChannelGlowPaint &paint = s_channelGlowPaint[i];
        const uint8_t uiMode = (uint8_t)s_cfg.uiMode;
        // A pulsing button is the one case where an unchanged input set still
        // has to be repainted — the pulse values below move on their own.
        const bool pulsing = animate && !dropdownCursor;
        const bool objsSame = paint.valid && paint.btn == btn && paint.lbl == lbl;
        const bool stateSame = objsSame
                               && paint.active == active
                               && paint.animate == animate
                               && paint.dropdownCursor == dropdownCursor
                               && paint.uiMode == uiMode;
        const bool pulseSame = !pulsing
                               || (paint.pulseOpa == pulseOpa
                                   && paint.outlineW == outlineW
                                   && paint.shadowW == shadowW);

        char text[48];
        const char *name = channelName(i);
        if (!name || !name[0]) name = TR("Channel");
        if (s_channelNeedsAttention[i] && !active) {
            snprintf(text, sizeof(text), "%s *", name);
        } else {
            snprintf(text, sizeof(text), "%s", name);
        }
        const bool textSame = objsSame && strcmp(paint.text, text) == 0;

        if (stateSame && pulseSame && textSame) continue;

        paint.btn = btn;
        paint.lbl = lbl;
        paint.valid = true;
        paint.active = active;
        paint.animate = animate;
        paint.dropdownCursor = dropdownCursor;
        paint.uiMode = uiMode;
        paint.pulseOpa = pulseOpa;
        paint.outlineW = outlineW;
        paint.shadowW = shadowW;
        snprintf(paint.text, sizeof(paint.text), "%s", text);

        if (lbl) {
            // Split from the style block: the name changes far less often than
            // the pulse, and lv_label_set_text() reflows the label.
            if (!textSame) {
                lv_label_set_text(lbl, text);
#if UI_CARDPUTER_COMPACT || UI_TOUCH_ONLY_PROFILE
                sizeChannelButtonToLabel(i);
#endif
            }
            if (!stateSame) {
                lv_obj_set_style_text_color(
                    lbl,
                    active
                        ? (s_cfg.uiMode == UI_MODE_DARK ? lv_color_hex(0x0B1E44) : lv_color_hex(0xD9E8FF))
                        : lv_color_hex(0xD9E8FF),
                    0);
            }
        }

        // Border width marks the layout dirty, so leave the whole block alone
        // unless the shape actually moved or the pulse advanced.
        if (stateSame && pulseSame) continue;

    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, (active || dropdownCursor) ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_width(btn, 0, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
    #else
        if (pulsing) {
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x8EEBFF), 0);

            lv_obj_set_style_outline_color(btn, lv_color_hex(0x8EEBFF), 0);
            lv_obj_set_style_outline_pad(btn, 0, 0);
            lv_obj_set_style_outline_width(btn, outlineW, 0);
            lv_obj_set_style_outline_opa(btn, pulseOpa, 0);

            lv_obj_set_style_shadow_color(btn, lv_color_hex(0x4EC9FF), 0);
            lv_obj_set_style_shadow_spread(btn, 1, 0);
            lv_obj_set_style_shadow_width(btn, shadowW, 0);
            lv_obj_set_style_shadow_opa(btn, pulseOpa, 0);
        } else {
            lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
            lv_obj_set_style_border_color(btn, active ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C), 0);
            lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_outline_width(btn, 0, 0);
            lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);

            if (dropdownCursor) {
                lv_obj_set_style_border_width(btn, 2, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xF4D35E), 0);
                lv_obj_set_style_outline_color(btn, lv_color_hex(0xF4D35E), 0);
                lv_obj_set_style_outline_pad(btn, 0, 0);
                lv_obj_set_style_outline_width(btn, 1, 0);
                lv_obj_set_style_outline_opa(btn, LV_OPA_70, 0);
            }
        }
#endif
    }

    if (s_channelSelectorBtn) {
        bool selectorShouldGlow = anyUnread;
#if UI_CHANNEL_LIST_DROPDOWN
        selectorShouldGlow = anyUnread && !isChannelDropdownVisible();
#endif
    #if defined(DEVICE_TDECK_PRO)
        // Bare text, like everywhere else now (see buildUi()). The Pro never
        // pulsed -- a halo is animation -- so there is nothing to keep here.
        lv_obj_set_style_bg_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
    #else
        #if UI_CARDPUTER_COMPACT
        const bool selectorNavCursor = (!s_cardputerMainChatPanelFocused
                        && !s_pagerChatCursorMode
                        && !isChannelDropdownVisible()
                        && !s_tracerouteModal
                        && !s_composeModal
                        && !s_cfgModal
                        && !s_legendModal
                        && !s_liveModal
                        && !s_dmModal
                        && !s_dmNodePickerModal
                        && !s_nodesModal);
    #else
        const bool selectorNavCursor = false;
    #endif

        // Same reasoning as the buttons above: the idle branch below is fully
        // static, and re-asserting it every 70 ms was invalidating the selector
        // (and marking the header layout dirty) for nothing. Only the glowing
        // branch has to be rewritten each pass.
        {
            SelectorGlowPaint &sp = s_selectorGlowPaint;
            const uint8_t uiMode = (uint8_t)s_cfg.uiMode;
            const bool stateSame = sp.valid
                                   && sp.btn == s_channelSelectorBtn
                                   && sp.glow == selectorShouldGlow
                                   && sp.navCursor == selectorNavCursor
                                   && sp.uiMode == uiMode;
            // Nothing here pulses any more (the selector is header text), so
            // the pulse frame is no reason to repaint it.
            if (stateSame) return;

            sp.btn = s_channelSelectorBtn;
            sp.valid = true;
            sp.glow = selectorShouldGlow;
            sp.navCursor = selectorNavCursor;
            sp.uiMode = uiMode;
            sp.pulseOpa = pulseOpa;
            sp.outlineW = outlineW;
            sp.shadowW = shadowW;
        }

    #if UI_CARDPUTER_COMPACT
        if (selectorNavCursor) {
            lv_obj_set_style_border_width(s_channelSelectorBtn, 2, 0);
            lv_obj_set_style_border_color(s_channelSelectorBtn, lv_color_hex(0xF4D35E), 0);
            lv_obj_set_style_outline_color(s_channelSelectorBtn, lv_color_hex(0xF4D35E), 0);
            lv_obj_set_style_outline_pad(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_70, 0);
            lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
        } else {
            lv_obj_set_style_border_width(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        }
    #else
        // Header text, not a control: no border, halo or unread pulse on any
        // board. The unread state lives on the rows in the channel drawer.
        LV_UNUSED(selectorShouldGlow);
        lv_obj_set_style_border_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
    #endif
#endif
    }
}

static void applyChannelButtonTheme() {
    // This writes border width and colour from its own rules, over the top of
    // whatever refreshChannelGlow() last put there. Drop that function's cache
    // so the next pass re-asserts the glow/cursor state instead of trusting a
    // record of pixels this loop is about to overwrite.
    invalidateChannelGlowPaint();

    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = s_channelBtns[i];
        if (!btn) continue;

        bool active = (i == s_activeChannel);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    #else
        lv_obj_set_style_bg_color(
            btn,
            (s_cfg.uiMode == UI_MODE_LIGHT)
                ? chatPanelBackgroundColor()
                : (active ? lv_color_hex(0x2A4FB4) : lv_color_hex(0x102750)),
            0);
        lv_obj_set_style_bg_opa(btn, active ? LV_OPA_90 : LV_OPA_60, 0);
        lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn, active ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C), 0);
#endif

        lv_obj_t *lbl = s_channelLabels[i];
        if (lbl) {
#if defined(DEVICE_TDECK_PRO)
            lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
#else
            lv_obj_set_style_text_color(
                lbl,
                active
                    ? (s_cfg.uiMode == UI_MODE_DARK ? lv_color_hex(0x0B1E44) : lv_color_hex(0xD9E8FF))
                    : lv_color_hex(0xD9E8FF),
                0);
#endif
        }
    }

    if (s_channelSelectorBtn) {
        // Header text, not a button: no fill of its own, so the header shows
        // through, and no border, outline or shadow. This used to paint the
        // selector as a raised button on every theme pass -- after buildUi()
        // had already made it bare, which is why it kept coming back.
        lv_obj_set_style_bg_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
    }
    if (s_channelSelectorLabel) {
        // The clock's ink, as refreshChannelSelectorLabel() sets it.
        lv_obj_set_style_text_color(s_channelSelectorLabel, lv_color_hex(0xD9E8FF), 0);
    }
    if (s_channelSelectorCaretLabel) {
        lv_obj_set_style_text_color(
            s_channelSelectorCaretLabel,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
            0);
    }
}

static void setActiveChannel(int channelIdx) {
    if (channelIdx < 0 || channelIdx >= MESH_CHANNELS) return;
    if (s_composeModal && channelIdx != s_composeChannelIdx) closeComposePrompt();
    s_activeChannel = channelIdx;
    s_pagerChatCursorMode = false;
    s_pagerChatCursorDisplayIndex = -1;
#if UI_CARDPUTER_COMPACT
    s_cardputerMainChatPanelFocused = false;
#endif
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || UI_CARDPUTER_COMPACT || defined(DEVICE_M9) \
    || UI_TANMATSU_LAYOUT
    s_cardputerDropdownSelection = channelIdx;
#endif
    clearSelectedMsgContext();
    // Opening a channel always lands on its newest message. This is a latch, not
    // just the force flag below, because the refresh here is a no-op until the
    // chat panel exists (boot picks a channel before buildUi runs) — the intent
    // has to survive until the first render that can honor it.
    s_chatScrollToLatestOnce = true;
    s_chatWindowGrowPending = false;
    s_chatWindowAnchorActive = false;
    s_channelNeedsAttention[channelIdx] = false;
    Channels.setActive(channelIdx);
    refreshChannelSelectorLabel();
    applyChannelButtonTheme();
    refreshChannelGlow(true);
    if (s_channelList && s_channelBtns[channelIdx]) {
        lv_obj_scroll_to_view(s_channelBtns[channelIdx], LV_ANIM_OFF);
    }
    refreshChatView(true);
}

static bool useCompactVerticalHeltecSelector() {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Was its own build flag (HELTEC_COMPACT_SELECTOR), set by the vertical env
    // and nothing else — so it was never anything but "this panel is portrait"
    // spelled a second way. Folded into the setting with the rest of it.
    return uiPortrait();
#else
    return false;
#endif
}

static bool isChannelDropdownVisible() {
#if UI_CHANNEL_LIST_DROPDOWN
    // Anchored, the list is always showing -- and is not a dropdown to close.
    if (!channelListIsDropdown()) return false;
    return s_channelList && s_channelDrawerOpen;
#else
    return false;
#endif
}

static void refreshChannelSelectorLabel() {
#if !UI_CHANNEL_LIST_DROPDOWN
    return;
#endif
    if (!channelListIsDropdown()) return;   // no selector with an anchored list
    if (!s_channelSelectorLabel) return;

    const char *name = channelName(s_activeChannel);
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
    if (isChannelDropdownVisible()
        && s_cardputerDropdownSelection >= 0
        && s_cardputerDropdownSelection < MESH_CHANNELS) {
        name = channelName(s_cardputerDropdownSelection);
    }
#endif
    if (!name || !name[0]) name = TR("Channel");
#if UI_TOUCH_ONLY_PROFILE
    if (useCompactVerticalHeltecSelector()) {
        name = "";
    }
#endif

// No caret on these: the selector reads as a plain channel name, which is the
// T-Deck presentation. It also buys the label the 14 px the caret reserved,
// which matters more here than on the T-Deck — same 320x240 pixels, smaller
// glass, so channel names have less room to be legible in.
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9) \
    || UI_TANMATSU_LAYOUT
    const bool showSelectorCaret = false;
#elif UI_TOUCH_ONLY_PROFILE
    // The caret said "this opens"; the selector no longer does (it is only the
    // chat's name now, and the list is the drawer).
    const bool showSelectorCaret = false;
#elif UI_CARDPUTER_COMPACT
    const bool showSelectorCaret = false;
#else
    const bool showSelectorCaret = true;
#endif

    lv_label_set_text(s_channelSelectorLabel, name);
    // The clock's ink, through the same themed mapping it uses, so the two
    // pieces of header text always match whatever the theme turns it into.
    lv_obj_set_style_text_color(s_channelSelectorLabel, lv_color_hex(0xD9E8FF), 0);
    if (s_channelSelectorCaretLabel) {
        if (showSelectorCaret) {
            lv_obj_clear_flag(s_channelSelectorCaretLabel, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_channelSelectorCaretLabel, isChannelDropdownVisible() ? "^" : "v");
            lv_obj_set_style_text_color(
                s_channelSelectorCaretLabel,
                (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
                0);
        } else {
            lv_obj_add_flag(s_channelSelectorCaretLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }

#if UI_CHANNEL_LIST_DROPDOWN
    bool allowDynamicSelectorWidth = true;
#if UI_TOUCH_ONLY_PROFILE
    allowDynamicSelectorWidth = !useCompactVerticalHeltecSelector();
#endif
    if (allowDynamicSelectorWidth && s_channelSelectorBtn) {
        const lv_coord_t selectorEdgePad = 6;
        const lv_coord_t selectorAntiClip = 2;

        // Keep selector width fixed: size it once from the widest channel label and never resize on channel changes.
        if (s_channelSelectorFixedBtnW <= 0) {
            lv_coord_t textW = 0;
            for (int i = 0; i < MESH_CHANNELS; i++) {
                const char *candidate = channelName(i);
                if (!candidate || !candidate[0]) candidate = TR("Channel");

                lv_label_set_text(s_channelSelectorLabel, candidate);
                lv_label_set_long_mode(s_channelSelectorLabel, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(s_channelSelectorLabel, LV_SIZE_CONTENT);
                lv_obj_update_layout(s_channelSelectorLabel);

                lv_coord_t candidateW = lv_obj_get_width(s_channelSelectorLabel);
                if (candidateW > textW) textW = candidateW;
            }

            // Measure button width from text + equal edge padding (+ optional caret room).
            lv_coord_t desiredW = textW + (showSelectorCaret ? 26 : (selectorEdgePad * 2 + selectorAntiClip));
#if defined(DEVICE_TDECK) || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
            if (desiredW < 56) desiredW = 56;
#elif UI_CARDPUTER_COMPACT
            if (desiredW < 60) desiredW = 60;
#elif defined(DEVICE_TDISPLAY_P4)
            // Wider in portrait, where the header grew: a short name like "Main"
            // should not leave a small button in a big bar.
            if (desiredW < (uiPortrait() ? 120 : 72)) desiredW = uiPortrait() ? 120 : 72;
#else
            if (desiredW < 72) desiredW = 72;
#endif

            lv_obj_t *header = lv_obj_get_parent(s_channelSelectorBtn);
            if (header) {
                lv_coord_t maxW = lv_obj_get_width(header) / 2;

                // Keep selector from colliding with centered clock text.
                if (s_chatHeaderTime && lv_obj_get_parent(s_chatHeaderTime) == header) {
                    lv_obj_update_layout(header);
                    lv_obj_update_layout(s_chatHeaderTime);
                    lv_coord_t selectorLeft = lv_obj_get_x(s_channelSelectorBtn);
                    lv_coord_t clockLeft = lv_obj_get_x(s_chatHeaderTime);
                    lv_coord_t maxBeforeClock = clockLeft - selectorLeft - 6;
                    if (maxBeforeClock > 0 && maxBeforeClock < maxW) maxW = maxBeforeClock;
                }

                if (maxW > 0 && desiredW > maxW) desiredW = maxW;
            }
            if (desiredW < 44) desiredW = 44;
            s_channelSelectorFixedBtnW = desiredW;
        }

        lv_label_set_text(s_channelSelectorLabel, name);
        lv_obj_set_width(s_channelSelectorBtn, s_channelSelectorFixedBtnW);
        lv_label_set_long_mode(s_channelSelectorLabel, LV_LABEL_LONG_DOT);
        labelSingleLine(s_channelSelectorLabel);

        if (showSelectorCaret) {
            lv_obj_set_style_text_align(s_channelSelectorLabel, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_width(s_channelSelectorLabel, s_channelSelectorFixedBtnW - 22);
            lv_obj_align(s_channelSelectorLabel, LV_ALIGN_LEFT_MID, 4, 1);
            if (s_channelSelectorCaretLabel) {
                lv_obj_align(s_channelSelectorCaretLabel, LV_ALIGN_RIGHT_MID, -5, 1);
            }
        } else {
            // Left-aligned: it is the header's title now, reading from the same
            // edge the battery mirrors on the right, not a label centred in a
            // button. The box keeps its fixed width so the clock stays put
            // whichever channel is showing.
            lv_obj_set_style_text_align(s_channelSelectorLabel, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_width(s_channelSelectorLabel, max((lv_coord_t)1, (lv_coord_t)(s_channelSelectorFixedBtnW - (selectorEdgePad * 2))));
            lv_obj_align(s_channelSelectorLabel, LV_ALIGN_LEFT_MID, 0, 1);
        }

        layoutHeaderInlineItems();
    }
#endif
}

#if UI_CHANNEL_LIST_DROPDOWN
// ── Channel drawer ───────────────────────────────────────────────────────────
// The slide itself (HAS_CHANNEL_DRAWER_ANIM, kChannelDrawerSlideMs: defined
// beside channelListIsDropdown(), for the DM screen's drawer too).
#if HAS_CHANNEL_DRAWER_ANIM
static void channelDrawerSlideXCb(void *obj, int32_t x) {
    lv_obj_set_x((lv_obj_t *)obj, x);
}

// Hidden only once it has actually left, and only if nothing reopened it on
// the way out -- a swipe back in mid-slide restarts the animation from here.
static void channelDrawerSlideDone(lv_anim_t *a) {
    lv_obj_t *list = (lv_obj_t *)a->var;
    if (!s_channelDrawerOpen && lvObjValid(list)) {
        lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

static void channelDrawerSlide(bool open) {
    if (!lvObjValid(s_channelList)) return;
    if (open == s_channelDrawerOpen
        && open != lv_obj_has_flag(s_channelList, LV_OBJ_FLAG_HIDDEN)) {
        return;   // already where it was asked to be
    }
    s_channelDrawerOpen = open;

    // Off screen is the drawer's own width to the left of the edge, which is
    // read now rather than at build: fitChannelDropdownToButtonContent() sets
    // the width after the first channel names are known.
    lv_obj_update_layout(s_channelList);
    // Open, it sits against the nav column when that is on the left (P4
    // landscape); it still slides in from beyond the screen's edge.
    const int32_t shownX = navSideInsetLeft();
    const int32_t hiddenX = -lv_obj_get_width(s_channelList) - 2;

    if (lvObjValid(s_channelDrawerScrim)) {
        if (open) {
            lv_obj_clear_flag(s_channelDrawerScrim, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_channelDrawerScrim);
        } else {
            lv_obj_add_flag(s_channelDrawerScrim, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (open) {
        if (lv_obj_has_flag(s_channelList, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_set_x(s_channelList, hiddenX);
            lv_obj_clear_flag(s_channelList, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_move_foreground(s_channelList);
    }

#if HAS_CHANNEL_DRAWER_ANIM
    lv_anim_delete(s_channelList, channelDrawerSlideXCb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_channelList);
    lv_anim_set_exec_cb(&a, channelDrawerSlideXCb);
    lv_anim_set_values(&a, lv_obj_get_x(s_channelList), open ? shownX : hiddenX);
    lv_anim_set_duration(&a, kChannelDrawerSlideMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, channelDrawerSlideDone);
    lv_anim_start(&a);
#else
    lv_obj_set_x(s_channelList, open ? shownX : hiddenX);
    if (!open) lv_obj_add_flag(s_channelList, LV_OBJ_FLAG_HIDDEN);
#endif
}

static void onChannelDrawerScrimPressed(lv_event_t *e) {
    LV_UNUSED(e);
    setChannelDropdownVisible(false);
}

#if HAS_TOUCH
// Whether a swipe in from the edge should open the drawer: the chat has to be
// the screen in front. chatScreenIsForeground() names the modals it knows of;
// the z-order check behind it catches the full-screen ones it does not (the
// Tools charts, Weather, ...), all of which are children of the root screen
// built after the chat panel and as wide as the display.
static bool channelDrawerCanOpenFromEdge() {
    if (!channelListIsDropdown() || !lvObjValid(s_channelList)) return false;
    if (!lvObjValid(s_chatPanel)) return false;
#if UI_TOUCH_ONLY_PROFILE || defined(DEVICE_M9) \
    || (HAS_HOME_DASHBOARD && UI_CHANNEL_LIST_DROPDOWN)
    if (!chatScreenIsForeground()) return false;
#endif
    lv_obj_t *parent = lv_obj_get_parent(s_chatPanel);
    if (!parent) return false;
    const int32_t from = lv_obj_get_index(s_chatPanel);
    if (from < 0) return false;
    const int32_t dispW = lv_disp_get_hor_res(NULL);
    const int32_t dispH = lv_disp_get_ver_res(NULL);
    const uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = (uint32_t)from + 1; i < n; i++) {
        lv_obj_t *o = lv_obj_get_child(parent, (int32_t)i);
        if (!o || o == s_channelList || o == s_channelDrawerScrim) continue;
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_get_width(o) >= dispW * 3 / 4 && lv_obj_get_height(o) >= dispH / 2) {
            return false;
        }
    }
    return true;
}

// How far in from the left edge a press may start and still count as a swipe
// from the edge. Wide enough to land with a thumb, narrow enough that a swipe
// across the middle of the chat is still nothing.
static constexpr int32_t kChannelDrawerEdgePx = 18;
static bool s_channelDrawerEdgePress = false;

// Attached to the touch input device rather than to an object: LVGL sends
// PRESSED and GESTURE to the indev whatever was under the finger, so the chat
// needs no invisible edge strip over its messages to catch the swipe.
static void channelDrawerIndevCb(lv_event_t *e) {
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_current_target(e);
    if (!indev) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        // The edge is the nav column's inner edge when it is on the left: a
        // press on the column is a nav tap, not the start of a swipe.
        const int32_t edge = navSideInsetLeft();
        s_channelDrawerEdgePress = (p.x >= edge && p.x <= edge + kChannelDrawerEdgePx);
        return;
    }
    if (code != LV_EVENT_GESTURE) return;
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    const bool fromEdge = s_channelDrawerEdgePress;
    s_channelDrawerEdgePress = false;
    // The DM screen's drawer first: while that screen is up, the chat (and so
    // the channel drawer) is underneath it and channelDrawerCanOpenFromEdge()
    // says no anyway.
    if (dir == LV_DIR_RIGHT && fromEdge && dmDrawerCanOpenFromEdge()) {
        dmDrawerSetOpen(true);
        return;
    }
    if (dir == LV_DIR_LEFT && s_dmDrawerOpen && dmDrawerBuilt()) {
        dmDrawerSetOpen(false);
        return;
    }
    if (dir == LV_DIR_RIGHT && fromEdge && !isChannelDropdownVisible()
        && channelDrawerCanOpenFromEdge()) {
        setChannelDropdownVisible(true);
    } else if (dir == LV_DIR_LEFT && isChannelDropdownVisible()) {
        // Pushed back the way it came, from anywhere on screen.
        setChannelDropdownVisible(false);
    }
}

static void channelDrawerAttachTouch(lv_indev_t *indev) {
    if (!indev) return;
    lv_indev_add_event_cb(indev, channelDrawerIndevCb, LV_EVENT_PRESSED, nullptr);
    lv_indev_add_event_cb(indev, channelDrawerIndevCb, LV_EVENT_GESTURE, nullptr);
}
#endif  // HAS_TOUCH
#endif  // UI_CHANNEL_LIST_DROPDOWN

static void setChannelDropdownVisible(bool visible) {
#if !UI_CHANNEL_LIST_DROPDOWN
    // Not just an early return: the drawer (channelDrawerSlide()) is compiled
    // only where there is a dropdown, so the body below cannot be either.
    LV_UNUSED(visible);
#else
    if (!channelListIsDropdown()) return;   // anchored: never hidden or raised
    if (!s_channelList) return;

    if (visible) {
        channelDrawerSlide(true);
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
        if (s_cardputerDropdownSelection < 0 || s_cardputerDropdownSelection >= MESH_CHANNELS) {
            s_cardputerDropdownSelection = s_activeChannel;
        }
        if (s_cardputerDropdownSelection >= 0 && s_cardputerDropdownSelection < MESH_CHANNELS
            && s_channelBtns[s_cardputerDropdownSelection]) {
            lv_obj_scroll_to_view(s_channelBtns[s_cardputerDropdownSelection], LV_ANIM_OFF);
        }
#endif
    } else {
        channelDrawerSlide(false);
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
        s_cardputerDropdownSelection = -1;
#endif
    }

    refreshChannelSelectorLabel();
    refreshChannelGlow(true);
#endif
}

static void onChannelSelectorPressed(lv_event_t *e) {
    LV_UNUSED(e);
    if (!channelListIsDropdown()) return;
    // A toggle, though a touch only ever opens with it: while the drawer is up
    // the scrim covers the header, so a second tap on the name lands on the
    // scrim and closes it from there.
    setChannelDropdownVisible(!isChannelDropdownVisible());
}

static void onChannelPressed(lv_event_t *e) {
    int channelIdx = (int)(intptr_t)lv_event_get_user_data(e);

    if (channelIdx == s_activeChannel && isChannelDropdownVisible()) {
        setChannelDropdownVisible(false);
        refreshChannelGlow(true);
        return;
    }

    setActiveChannel(channelIdx);
    setChannelDropdownVisible(false);
}

static const char *channelName(int idx) {
    if (idx < 0 || idx >= MESH_CHANNELS) return "";
    const ChannelKey &ck = CHANNEL_KEYS[idx];
    if (ck.name_buf[0]) return ck.name_buf;
    return ck.name ? ck.name : "";
}

static void sizeChannelButtonToLabel(int idx) {
#if UI_CARDPUTER_COMPACT || UI_TOUCH_ONLY_PROFILE
    if (s_channelList && !s_channelStrip) return;
    if (idx < 0 || idx >= MESH_CHANNELS) return;
    lv_obj_t *btn = s_channelBtns[idx];
    lv_obj_t *lbl = s_channelLabels[idx];
    if (!btn || !lbl) return;

    lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_update_layout(lbl);

    lv_coord_t targetW = lv_obj_get_width(lbl) + 14;
    #if UI_TOUCH_ONLY_PROFILE
    if (targetW < 52) targetW = 52;
    #else
    if (targetW < 34) targetW = 34;
    #endif
    lv_obj_set_width(btn, targetW);
    lv_obj_set_height(btn, kMainScreenChannelBtnHeight);
    lv_obj_center(lbl);
#else
    LV_UNUSED(idx);
#endif
}

static void fitChannelDropdownToButtonContent() {
#if UI_CHANNEL_LIST_DROPDOWN
    // This fitter applies only to the floating channel dropdown list.
    if (!s_channelList || s_channelStrip) return;
    if (!channelListIsDropdown()) return;   // the anchored column keeps its width

    lv_coord_t maxLabelW = 0;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *lbl = s_channelLabels[i];
        if (!lbl) continue;
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lbl, LV_SIZE_CONTENT);
        lv_obj_update_layout(lbl);
        lv_coord_t w = lv_obj_get_width(lbl);
        if (w > maxLabelW) maxLabelW = w;
    }
    if (maxLabelW <= 0) return;

#if defined(DEVICE_TDECK_PRO)
    // Clear air between the rows and the drawer's edges: at 2 px the rows ran
    // into its black border. The right side also has to step over that 2 px
    // border itself, which LVGL takes out of the content area before padding.
    const lv_coord_t listPadLeft = 6;
    const lv_coord_t listPadRight = 6;
    const lv_coord_t listBorderRight = 2;   // buildUi(): the drawer's edge
    const lv_coord_t buttonTextPad = 4;
    const lv_coord_t scrollbarGutter = 0;
#elif defined(DEVICE_MESH_DECK)
    // Tighter than the shared values below, and deliberately so: 2+2 panel pad,
    // 4 of gutter and 2+2 inside the row spend 12 px between the panel edge and
    // the text — the same 12 px the selector button spends on its own 6 px side
    // padding. That is what lets the panel be exactly as wide as the button
    // while still fitting every name the button fits.
    const lv_coord_t listPadLeft = 2;
    const lv_coord_t listPadRight = 2;
    const lv_coord_t buttonTextPad = 4;
    const lv_coord_t scrollbarGutter = 4;
#else
    const lv_coord_t listPadLeft = 4;
    const lv_coord_t listPadRight = 4;
    const lv_coord_t buttonTextPad = 8; // 4px left + 4px right, matching button inner padding
    const lv_coord_t scrollbarGutter = 8; // keep scrollbar between panel edge and buttons
#endif

    lv_coord_t buttonW = maxLabelW + buttonTextPad;
    if (buttonW < 56) buttonW = 56;

    lv_coord_t dropdownW = listPadLeft + buttonW + scrollbarGutter + listPadRight;

#if defined(DEVICE_TDECK_PRO)
    // At least as wide as the name box in the header, as the dropdown was, but
    // grown rather than squeezed when the rows and their margins need more:
    // the drawer is its own sheet now, not the selector opening downward, so
    // there is no edge of the button it has to line up with.
    dropdownW += listBorderRight;
    lv_coord_t selectorW = s_channelSelectorFixedBtnW;
    if (selectorW <= 0 && s_channelSelectorBtn) selectorW = lv_obj_get_width(s_channelSelectorBtn);
    if (selectorW > dropdownW) dropdownW = selectorW;
    buttonW = dropdownW - listPadLeft - listPadRight - listBorderRight;
#elif defined(DEVICE_MESH_DECK)
    // Take the selector button's width outright when it is the larger of the
    // two: the list is that button opening downward, so anything short of its
    // edges reads as a separate panel. The selector sizes itself once from the
    // widest channel name (refreshChannelSelectorLabel), which is why this runs
    // after the first setActiveChannel rather than at build time.
    lv_coord_t selectorW = s_channelSelectorFixedBtnW;
    if (selectorW <= 0 && s_channelSelectorBtn) selectorW = lv_obj_get_width(s_channelSelectorBtn);
    if (selectorW > dropdownW) {
        dropdownW = selectorW;
        buttonW = dropdownW - listPadLeft - listPadRight - scrollbarGutter;
    }
#endif

    lv_obj_t *parent = lv_obj_get_parent(s_channelList);
    if (parent) {
        lv_coord_t maxW = lv_obj_get_width(parent) - 8;
        if (maxW > 0 && dropdownW > maxW) {
            dropdownW = maxW;
            lv_coord_t maxButtonW = dropdownW - listPadLeft - listPadRight - scrollbarGutter;
            if (maxButtonW < 44) maxButtonW = 44;
            if (buttonW > maxButtonW) buttonW = maxButtonW;
        }
    }

    lv_obj_set_width(s_channelList, dropdownW);
    lv_obj_set_style_pad_left(s_channelList, listPadLeft, 0);
    lv_obj_set_style_pad_right(s_channelList, listPadRight, 0);
    lv_obj_set_style_pad_right(s_channelList, 4, LV_PART_SCROLLBAR);

    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = s_channelBtns[i];
        lv_obj_t *lbl = s_channelLabels[i];
        if (!btn || !lbl) continue;

        lv_obj_set_width(btn, buttonW);
        // Same padding the row was built with, or the label is sized for room
        // the row does not have and every name ellipsizes.
        lv_obj_set_width(lbl, max((lv_coord_t)1, (lv_coord_t)(buttonW - buttonTextPad)));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_center(lbl);
    }
#endif
}

// A build runs on whichever partition table is in flash, not necessarily this
// repo's partitions.csv (nvs, 64KB @0x650000). Third-party installers write our
// app into a slot of *their* table, so NVS can land somewhere smaller or shared
// with other payloads. Preferences fails silently when it runs out of room, and
// the symptom is settings that apply immediately and then vanish on reboot — so
// state the geometry once at boot instead of leaving it to guesswork.
// Per-namespace key census. Which namespace owns the partition decides what has
// to shrink, and on a small NVS that is not guessable from our side: the WiFi
// driver's credentials and the PHY calibration blob live in the same partition
// as our settings. Counts keys, not entries — a blob key spans several entries,
// so the true footprint of a blob-heavy namespace is larger than its key count.
static void logNvsNamespaceCensus() {
    struct NsTally { char ns[16]; int keys; };
    NsTally tally[10] = {};
    int tracked = 0;
    int untracked = 0;

    nvs_iterator_t it = nvsEntryFindCompat("nvs", nullptr);
    while (it) {
        nvs_entry_info_t info = {};
        nvs_entry_info(it, &info);
        int slot = -1;
        for (int i = 0; i < tracked; i++) {
            if (strcmp(tally[i].ns, info.namespace_name) == 0) { slot = i; break; }
        }
        if (slot < 0 && tracked < (int)(sizeof(tally) / sizeof(tally[0]))) {
            slot = tracked++;
            strncpy(tally[slot].ns, info.namespace_name, sizeof(tally[0].ns) - 1);
        }
        if (slot >= 0) tally[slot].keys++; else untracked++;
        it = nvsEntryNextCompat(it);
    }

    for (int i = 0; i < tracked; i++) {
        Serial.printf("[nvs]   %-18s %3d keys\n", tally[i].ns, tally[i].keys);
    }
    if (untracked) {
        Serial.printf("[nvs]   (%d keys in further namespaces)\n", untracked);
    }
}

static void logNvsHealth() {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    if (!part) {
        Serial.println("[nvs] no NVS partition in this flash layout - nothing can persist");
        return;
    }
    nvs_stats_t st = {};
    if (nvs_get_stats(nullptr, &st) == ESP_OK) {
        Serial.printf("[nvs] '%s' @0x%06X %uKB - entries used=%u free=%u, namespaces=%u\n",
                      part->label, (unsigned)part->address, (unsigned)(part->size / 1024),
                      (unsigned)st.used_entries, (unsigned)st.free_entries,
                      (unsigned)st.namespace_count);
        // NVS holds one page (126 entries) back for compaction and will not
        // spend it, so free_entries sitting at ~one page means the partition is
        // full in practice — writes fail with NOT_ENOUGH_SPACE while the stats
        // still show entries "free".
        if (st.free_entries <= 130) {
            Serial.println("[nvs] FULL - only the compaction page is left; settings writes will fail");
        }
        logNvsNamespaceCensus();
    } else {
        Serial.printf("[nvs] '%s' @0x%06X %uKB (stats unavailable)\n",
                      part->label, (unsigned)part->address, (unsigned)(part->size / 1024));
    }
}

static void loadConfigFromSd() {
    cfgInitDefaults(s_cfg);
    myDeviceRole = s_cfg.deviceRole;
    s_webCfgEnabled = false;
    // The WiFi driver otherwise mirrors the SSID/password into NVS on every
    // connect, duplicating credentials we already store ourselves. Free on a
    // 64 KB partition; not free on the 16 KB one a third-party layout provides.
    WiFi.persistent(false);
    logNvsHealth();

    if (!sdBegin()) {
        Serial.println("[lvgl-poc] SD not available; loading state from NVS");
    } else {
        Serial.println("[lvgl-poc] boot config import disabled; loading state from NVS");
    }

    loadConfigFromPrefs();
    loadProvisionedConfig();
#if !HAS_ENV_SENSOR_TELEMETRY
    s_cfg.telEnvEnabled = false;
#endif
    if (s_cfg.telDeviceIntervalS < 3600UL) s_cfg.telDeviceIntervalS = 3600UL;
    if (s_cfg.telEnvIntervalS < 3600UL) s_cfg.telEnvIntervalS = 3600UL;
    if (s_cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        s_cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }
    loadChannelsFromPrefs();
    // Straight after both loads: rewrites channel keys before anything hashes
    // them, and sets the TX inhibit before the radio comes up.
    licensedModeBootSetup();
    // After the config, because the remembered list is defined relative to the
    // configured network (the picker shows configured first, then these).
    loadKnownWifiNetworks();
    // Before the palette, which needs the slots populated to resolve a custom
    // selection — and would otherwise log a spurious "slot is empty" fallback
    // on every boot of a device using one.
    uiCustomThemesLoad();
    applyUiThemePalette();
    // Boot renders at whatever was loaded, so record it here — otherwise the
    // first web save would see a change against 0xFF and rebuild for nothing.
    s_appliedFontSize = (uint8_t)constrain((int)s_cfg.fontSize, 0, FONT_SIZE_MAX);
    myDeviceRole = s_cfg.deviceRole;
    // Only meaningful once the stored value is in s_cfg — set from setup(), it
    // would latch the compiled default and never see what the user chose.
    otaSetChannel(s_cfg.otaChannel);
    // Saving the web config form reboots, so a channel switch made from the
    // browser leaves its "check now" behind in NVS for this boot to pick up.
    // Consumed here and not in loadConfigForOtaWorker(): the worker boot exists
    // to install, and has no UI to report a check on.
    s_otaBootCheckForced = otaConsumeBootCheckOnce();
    if (s_otaBootCheckForced) {
        Serial.println("[ota-check] release check forced by a web-config channel switch");
    }
}

static void loadConfigForOtaWorker() {
    cfgInitDefaults(s_cfg);
    myDeviceRole = s_cfg.deviceRole;
    loadConfigFromPrefs();
    myDeviceRole = s_cfg.deviceRole;
    // This boot is the one that downloads and installs, so it above all has to
    // resolve the same channel the user picked in the UI.
    otaSetChannel(s_cfg.otaChannel);
}

static void refreshHeaderTime(bool force) {
    if (!s_chatHeaderTime) return;

    char clock[LIVE_CLOCK_BUF];
    liveBuildPrefix(clock, sizeof(clock));
    size_t n = strlen(clock);
    if (n > 0 && clock[n - 1] == ' ') clock[n - 1] = '\0';

    // Licensed: the badge and call sign ride on the clock label, so they cost
    // no header height and the header layout measures them like any clock.
    char buf[kHeaderTimeBuf];
#if FEATURE_LICENSED_MODE
    if (lic::active() && s_cfg.callSign[0]) {
        snprintf(buf, sizeof(buf), "HAM %s %s", s_cfg.callSign, clock);
    } else
#endif
    {
        snprintf(buf, sizeof(buf), "%s", clock);
    }

    if (!force && strcmp(buf, s_lastHeaderTime) == 0) return;
    lv_label_set_text(s_chatHeaderTime, buf);
#if UI_CHANNEL_LIST_DROPDOWN
    layoutHeaderInlineItems();
#endif
    for (ScreenHeader &h : s_screenHeaders) {
        if (!lvObjValid(h.bar) || !h.time) continue;
        lv_label_set_text(h.time, buf);
        layoutScreenHeader(h);
    }
    strncpy(s_lastHeaderTime, buf, sizeof(s_lastHeaderTime) - 1);
    s_lastHeaderTime[sizeof(s_lastHeaderTime) - 1] = '\0';
}

// Whether the header puts the battery reading beside its dot. Not on the Wio
// Tracker held upright: 240 px across has the name, the clock, GPS and Wi-Fi to
// fit as well, and the dot's colour already says how full it is -- so the
// reading goes and the icons close up on the dot.
static inline bool headerBattTextShown() {
#if defined(DEVICE_WIO_TRACKER_L2)
    return !uiPortrait();
#else
    return true;
#endif
}

// Width of a label's text as it will draw, measured rather than read back with
// lv_obj_get_width(): that is only right after a layout pass, and these are
// asked mid-build, before one has happened.
static lv_coord_t headerLabelTextW(lv_obj_t *label) {
    if (!label) return 0;
    lv_point_t sz = {0, 0};
    lv_text_get_size(&sz, lv_label_get_text(label),
                     lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

// Puts a header clock on the centre line of the display -- not of its bar, and
// not of the gap between whatever sits at the bar's two ends, which is where it
// used to go and why it read as off centre. Only where the gap is too narrow for
// that does it move, the least it has to, to stay clear of both ends.
//
// contentL is where the bar's content area starts on the display (LEFT_MID
// offsets count from there); slotL and slotR are the display x the clock has to
// stay between.
//
// Except held upright on the P4, the Heltec V4 and the Wio Tracker, where the
// clock goes to the right-hand end and the name, not the clock, gives way when
// the two meet. On the P4 that end is otherwise empty -- its battery, GPS and
// Wi-Fi are in the status strip under the nav bar -- so the clock is inset as
// far as the name is from the left. On the other two that end is the status
// cluster, and the clock sits just inside it, the same gap from its first icon.
static inline bool headerClockAtRight() {
#if HAS_STATUS_STRIP || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_WIO_TRACKER_L2)
    return uiPortrait();
#else
    return false;
#endif
}

// Where the clock goes, before it is kept clear of whatever is on its left.
static lv_coord_t headerClockX(lv_coord_t clockW, lv_coord_t slotR) {
    if (headerClockAtRight()) return slotR - s_chatHdrGeom.padX - clockW;
    lv_coord_t x = ((lv_coord_t)lv_disp_get_hor_res(NULL) - clockW) / 2;
    if (x + clockW > slotR) x = slotR - clockW;
    return x;
}

static void centreHeaderClock(lv_obj_t *clock, lv_coord_t contentL,
                              lv_coord_t slotL, lv_coord_t slotR, lv_coord_t yOffset) {
    if (!clock) return;
    const lv_coord_t w = headerLabelTextW(clock);
    lv_coord_t x = headerClockX(w, slotR);
    if (!headerClockAtRight() && x < slotL) x = slotL;
    lv_obj_align(clock, LV_ALIGN_LEFT_MID, x - contentL, yOffset);
}

// Holds a LONG_DOT label to one line. Dots only stand in for the overflow when
// the label's height is fixed; left to size itself, a name too long for its
// width wraps onto a second line instead.
static void labelSingleLine(lv_obj_t *label) {
    if (!label) return;
    lv_obj_set_height(label,
                      lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
}

static void layoutHeaderInlineItems() {
#if UI_CHANNEL_LIST_DROPDOWN
    if (!s_chatHeaderTime || !s_chatHeaderBattBar) return;
    if (!s_channelSelectorBtn || !s_chatHeaderBattText || !s_chatHeaderBar) return;

    const lv_coord_t headerTextYOffset = 1;
    bool rightClusterShown = true;

#if HAS_STATUS_STRIP
    if (uiPortrait()) {
        // Battery, GPS and Wi-Fi are in the status strip under the nav bar
        // (statusStripInit()), so the right end of the header is the clock's
        // (headerClockAtRight()).
        lv_obj_add_flag(s_chatHeaderBattBar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_chatHeaderBattText, LV_OBJ_FLAG_HIDDEN);
        // The touch-only chat screen keeps GPS and Wi-Fi in this bar too (only
        // the keyboard boards move them onto the nav bar). They are in the
        // strip as well, so here they go.
        if (s_chatHeaderGps && lv_obj_get_parent(s_chatHeaderGps) == s_chatHeaderBar) {
            lv_obj_add_flag(s_chatHeaderGps, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_chatHeaderWifi && lv_obj_get_parent(s_chatHeaderWifi) == s_chatHeaderBar) {
            lv_obj_add_flag(s_chatHeaderWifi, LV_OBJ_FLAG_HIDDEN);
        }
        rightClusterShown = false;
    }
#endif

    // The battery reading, or the dot alone (headerBattTextShown()), is what
    // the icons pack in against.
    lv_obj_t *battAnchor = s_chatHeaderBattText;
    if (rightClusterShown) {
        // Keep battery icon at the far right, with percent text close to it.
        lv_obj_align(s_chatHeaderBattBar, LV_ALIGN_RIGHT_MID, -4, 0);
        if (headerBattTextShown()) {
            lv_obj_clear_flag(s_chatHeaderBattText, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(s_chatHeaderBattText, s_chatHeaderBattBar, LV_ALIGN_OUT_LEFT_MID, -3, headerTextYOffset);
        } else {
            lv_obj_add_flag(s_chatHeaderBattText, LV_OBJ_FLAG_HIDDEN);
            battAnchor = s_chatHeaderBattBar;
        }

        // Optional status icons packed in beside the battery, so the right end
        // of the bar is one cluster the clock keeps clear of.
        if (s_chatHeaderGps && s_chatHeaderWifi
            && lv_obj_get_parent(s_chatHeaderGps) == s_chatHeaderBar
            && lv_obj_get_parent(s_chatHeaderWifi) == s_chatHeaderBar) {
            lv_obj_align_to(s_chatHeaderWifi, battAnchor, LV_ALIGN_OUT_LEFT_MID, -6, 0);
            lv_obj_align_to(s_chatHeaderGps, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -7, 0);

            if (s_chatDmAlert && lv_obj_is_valid(s_chatDmAlert)
                && lv_obj_get_parent(s_chatDmAlert) == s_chatHeaderBar) {
                lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
                if (lvObjValid(s_chatChanAlert)
                    && lv_obj_get_parent(s_chatChanAlert) == s_chatHeaderBar) {
                    lv_obj_align_to(s_chatChanAlert, s_chatDmAlert, LV_ALIGN_OUT_LEFT_MID, -4, 0);
                }
            }
        }
    }

    lv_obj_update_layout(s_chatHeaderBar);
    lv_area_t barArea;
    lv_obj_get_coords(s_chatHeaderBar, &barArea);
    const lv_coord_t border = lv_obj_get_style_border_width(s_chatHeaderBar, LV_PART_MAIN);
    const lv_coord_t contentL = barArea.x1 + border
                              + lv_obj_get_style_pad_left(s_chatHeaderBar, LV_PART_MAIN);
    const lv_coord_t contentR = barArea.x2 + 1 - border
                              - lv_obj_get_style_pad_right(s_chatHeaderBar, LV_PART_MAIN);

    // Left bound: the channel name, where there is one. With an anchored
    // channel list (P4 landscape) the selector is hidden but still has a
    // position, which must not count.
    lv_coord_t slotL = contentL;
    if (channelListIsDropdown() && !lv_obj_has_flag(s_channelSelectorBtn, LV_OBJ_FLAG_HIDDEN)) {
        lv_area_t selectorArea;
        lv_obj_get_coords(s_channelSelectorBtn, &selectorArea);
        slotL = selectorArea.x2 + 1 + 4;
    }

    // Right bound: the leftmost thing in the status cluster.
    lv_coord_t slotR = contentR;
    if (rightClusterShown) {
        lv_obj_t *leftmost = battAnchor;
        if (s_chatHeaderGps && lv_obj_get_parent(s_chatHeaderGps) == s_chatHeaderBar
            && !lv_obj_has_flag(s_chatHeaderGps, LV_OBJ_FLAG_HIDDEN)) {
            leftmost = s_chatHeaderGps;
        }
        lv_area_t a;
        lv_obj_get_coords(leftmost, &a);
        slotR = a.x1 - 2;
    }

    centreHeaderClock(s_chatHeaderTime, contentL, slotL, slotR, headerTextYOffset);
#endif
}

// ── Screen header ────────────────────────────────────────────────────────────
// DMs, Nodes, Config and Tools open with the chat screen's header bar rather
// than one of their own: the same bar in the same place, the screen's name where
// the channel name goes, the clock on the centre line, and whatever that board's
// chat header carries at the right-hand end (battery everywhere; GPS and Wi-Fi
// too on the touch boards; nothing upright on the P4, whose status strip under
// the nav bar has them). Moving between the screens, only the name changes.
//
// Painted from refreshHeaderTime() and refreshHeaderStatus(), from the same
// readings as the chat header, so the two can never disagree.

static void onScreenHeaderDeleted(lv_event_t *e) {
    lv_obj_t *bar = lv_event_get_target_obj(e);
    for (ScreenHeader &h : s_screenHeaders) {
        if (h.bar == bar) h = ScreenHeader{};
    }
}

static void layoutScreenHeader(ScreenHeader &h) {
    if (!lvObjValid(h.bar) || !h.title || !h.time) return;
    const ChatHeaderGeom &g = s_chatHdrGeom;

    // Worked out from where the bar was put rather than read from its coords:
    // a side nav column widens the screen's padding after this is built, and
    // the bar only moves on the next layout pass.
    const lv_coord_t border = lv_obj_get_style_border_width(h.bar, LV_PART_MAIN);
    const lv_coord_t contentL = h.barX + border + lv_obj_get_style_pad_left(h.bar, LV_PART_MAIN);
    const lv_coord_t contentR = h.barX + h.barW - border
                              - lv_obj_get_style_pad_right(h.bar, LV_PART_MAIN);

    // The right-hand cluster, packed exactly as layoutHeaderInlineItems() packs
    // the chat header's.
    lv_coord_t slotR = contentR;
    if (h.battDot && h.battText) {
        lv_obj_align(h.battDot, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_t *battAnchor = h.battText;
        slotR = contentR - 4 - g.battDotW;
        if (headerBattTextShown()) {
            lv_obj_align_to(h.battText, h.battDot, LV_ALIGN_OUT_LEFT_MID, -3, 1);
            slotR -= 3 + headerLabelTextW(h.battText);
        } else {
            lv_obj_add_flag(h.battText, LV_OBJ_FLAG_HIDDEN);
            battAnchor = h.battDot;
        }
        if (h.gps && h.wifi) {
            lv_obj_align_to(h.wifi, battAnchor, LV_ALIGN_OUT_LEFT_MID, -6, 0);
            lv_obj_align_to(h.gps, h.wifi, LV_ALIGN_OUT_LEFT_MID, -7, 0);
            slotR -= 6 + headerLabelTextW(h.wifi) + 7 + headerLabelTextW(h.gps);
        }
        slotR -= 2;
    }

    // Where the clock wants to be, so the name knows how much room it has.
    const lv_coord_t clockW = headerLabelTextW(h.time);
    const lv_coord_t clockX = headerClockX(clockW, slotR);
    const lv_coord_t titleL = contentL + g.titleX;
    const lv_coord_t titleMax = clockX - 6 - titleL;

    // The channel name's face where the name fits beside the clock, and a size
    // or two down where it does not ("Configuration" in the P4's upright 24 px
    // would otherwise run under it) -- only then is it cut short.
    static const lv_font_t *const kSmaller[] = {
#if defined(DEVICE_CROWPANEL_50)
        // Steps down from its doubled 32 px header. Only here: naming a font
        // links it, and no other board's header starts above 24.
        &lv_font_montserrat_28, &lv_font_montserrat_24, &lv_font_montserrat_20,
#endif
        &lv_font_montserrat_18, &lv_font_montserrat_16,
        &lv_font_montserrat_14, &lv_font_montserrat_12,
    };
    const lv_font_t *font = g.clockFont;
    lv_point_t sz = {0, 0};
    lv_text_get_size(&sz, h.titleText, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    for (const lv_font_t *f : kSmaller) {
        if (sz.x <= titleMax) break;
        if (lv_font_get_line_height(f) >= lv_font_get_line_height(font)) continue;
        font = f;
        lv_text_get_size(&sz, h.titleText, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    }
    const lv_coord_t titleW = (titleMax <= 0) ? 0 : min((lv_coord_t)sz.x, titleMax);
    if (lv_obj_get_style_text_font(h.title, LV_PART_MAIN) != font) {
        lv_obj_set_style_text_font(h.title, font, 0);
    }
    lv_obj_set_width(h.title, titleW);
    labelSingleLine(h.title);
    lv_label_set_text(h.title, h.titleText);
    lv_obj_align(h.title, LV_ALIGN_LEFT_MID, g.titleX, g.textYOffset);

    centreHeaderClock(h.time, contentL, titleL + titleW + 6, slotR, 1);
}

static lv_obj_t *buildScreenHeader(lv_obj_t *modal, const char *text) {
    if (!modal) return nullptr;
    ScreenHeader *slot = nullptr;
    for (ScreenHeader &h : s_screenHeaders) {
        if (!lvObjValid(h.bar)) { h = ScreenHeader{}; slot = &h; break; }
    }
    if (!slot) slot = &s_screenHeaders[0];   // cannot happen: one screen at a time
    ScreenHeader &h = *slot;
    const ChatHeaderGeom &g = s_chatHdrGeom;

    // Every screen that takes this is a full-display panel with 4 px of padding
    // and whatever border it has -- the same pair appendHeltecBottomNav()
    // cancels to put its bar on the display edges, and cancelled here the same
    // way so this bar lands where the chat header is.
    const int inset = (int)lv_obj_get_style_border_width(modal, LV_PART_MAIN) + 4;
    const int leftGap = navSideLeft() ? 2 : 0;
    const int rightGap = (navIsSideColumn() && !navSideLeft()) ? 2 : 0;
    h.barX = g.margin + navSideInsetLeft() + leftGap;
    h.barW = (int)lv_disp_get_hor_res(NULL) - 2 * g.margin
           - navSideInsetLeft() - navSideInsetRight() - leftGap - rightGap;

    // Holds the bar's room in the screen's flex column; the bar itself floats.
    lv_obj_t *spacer = lv_obj_create(modal);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_height(spacer, max(1, g.top + g.h - inset));
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    h.bar = lv_obj_create(modal);
    lv_obj_add_flag(h.bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(h.bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(h.bar, h.barW, g.h);
    // From the content area's left edge, which the side column's padding (added
    // later, by appendHeltecBottomNav()) moves by exactly navSideInsetLeft().
    lv_obj_align(h.bar, LV_ALIGN_TOP_LEFT, g.margin + leftGap - inset, g.top - inset);
    lv_obj_clear_flag(h.bar, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(h.bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(h.bar, 1, 0);
    lv_obj_set_style_border_color(h.bar, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(
        h.bar,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? chatPanelBackgroundColor() : lv_color_hex(0x0E285B),
        0);
    lv_obj_set_style_bg_opa(h.bar, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_60 : LV_OPA_70, 0);
    lv_obj_set_style_border_width(h.bar, 1, 0);
    lv_obj_set_style_border_color(h.bar, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_pad_all(h.bar, 2, 0);
    cornerSafeHeader(h.bar);
    lv_obj_add_event_cb(h.bar, onScreenHeaderDeleted, LV_EVENT_DELETE, nullptr);

    snprintf(h.titleText, sizeof(h.titleText), "%s", text ? text : "");
    h.title = lv_label_create(h.bar);
    lv_obj_set_style_text_font(h.title, g.clockFont, 0);
    lv_obj_set_style_text_color(h.title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(h.title, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(h.title, LV_LABEL_LONG_DOT);

    h.time = lv_label_create(h.bar);
    lv_obj_set_style_text_font(h.time, g.clockFont, 0);
    lv_obj_set_style_text_color(h.time, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(h.time, s_chatHeaderTime ? lv_label_get_text(s_chatHeaderTime) : "");

    bool rightCluster = true;
#if HAS_STATUS_STRIP
    if (uiPortrait()) rightCluster = false;   // in the status strip instead
#endif
    if (rightCluster) {
#if UI_TOUCH_ONLY_PROFILE
        h.gps = lv_label_create(h.bar);
        lv_obj_set_style_text_font(h.gps, g.textFont, 0);
        lv_obj_set_style_text_color(h.gps, lv_color_hex(0xBFD6FF), 0);
        lv_label_set_text(h.gps, "");

        h.wifi = lv_label_create(h.bar);
        lv_obj_set_style_text_font(h.wifi, g.iconFont, 0);
        lv_obj_set_style_text_color(h.wifi, lv_color_hex(0xBFD6FF), 0);
        lv_label_set_text(h.wifi, "");
#endif
        h.battDot = lv_obj_create(h.bar);
        lv_obj_remove_style_all(h.battDot);
        lv_obj_set_size(h.battDot, g.battDotW, g.battDotH);
        lv_obj_set_style_radius(h.battDot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(h.battDot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(h.battDot, headerGoodGreenColor(), 0);
        lv_obj_set_style_border_width(h.battDot, 1, 0);
        lv_obj_set_style_border_color(h.battDot, lv_color_hex(0x274A84), 0);

        h.battText = lv_label_create(h.bar);
        lv_obj_set_style_text_font(h.battText, g.textFont, 0);
        lv_obj_set_style_text_color(h.battText, lv_color_hex(0xBFD6FF), 0);
        lv_label_set_text(h.battText,
                          s_chatHeaderBattText ? lv_label_get_text(s_chatHeaderBattText) : "");
    }

    layoutScreenHeader(h);
    // The periodic refresh early-outs while nothing has changed, which opening a
    // screen normally is; one forced pass fills the icons and battery in now.
    refreshHeaderStatus(true);
    return h.title;
}

// Renames a screen header (Nodes puts its count and filter in the name).
static void screenHeaderSetTitle(lv_obj_t *title, const char *text) {
    if (!title) return;
    for (ScreenHeader &h : s_screenHeaders) {
        if (h.title != title || !lvObjValid(h.bar)) continue;
        if (strcmp(h.titleText, text) == 0) return;
        snprintf(h.titleText, sizeof(h.titleText), "%s", text);
        layoutScreenHeader(h);
        return;
    }
}

#if UI_TOUCH_ONLY_PROFILE || UI_CFG_SECTIONS
// The secondary header: the row under a screen header that holds the screen's
// own actions (Nodes' Filter, Config's Info), packed from the left. They used
// to share the header with the title; the header's right end now carries the
// same status cluster as the chat screen's, and there is no room for both.
static lv_obj_t *buildSecondaryHeader(lv_obj_t *modal) {
    lv_obj_t *row = lv_obj_create(modal);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(row, toolStackLayout() ? 6 : 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *secondaryHeaderBtn(lv_obj_t *row, const char *text, lv_event_cb_t cb) {
    const bool big = toolStackLayout();
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_height(btn, big ? kStackBtnH : 22);
    lv_obj_set_style_min_width(btn, big ? 72 : 58, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_hor(btn, big ? 10 : 6, 0);
    lv_obj_set_style_pad_ver(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x335D9D), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
#if defined(DEVICE_CROWPANEL)
    lv_obj_set_style_text_font(lbl, big ? STACK_BTN_FONT : &lv_font_montserrat_12, 0);
#else
    lv_obj_set_style_text_font(lbl, big ? STACK_BTN_FONT : &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}
#endif

static inline lv_color_t headerBatteryDotColor(uint8_t battPct) {
    if (battPct >= 80) {
        return headerGoodGreenColor();
    }
    if (battPct >= 40) {
        return (s_cfg.uiMode == UI_MODE_LIGHT)
            ? lv_color_hex(0xA77900)
            : lv_color_hex(0xF4D35E);
    }
    return (s_cfg.uiMode == UI_MODE_LIGHT)
        ? lv_color_hex(0xC74242)
        : lv_color_hex(0xFF6B6B);
}

// Text, color and strikethrough for one GPS/WiFi pair. Several pairs can be on
// screen at once — the chat screen's, the one on a modal's copy of the nav bar,
// and the glance header the lock screen and home dashboard each own — and they
// must never disagree, so all of them are painted from the same values by this
// code. What differs is only which palette they are painted in (see
// StatusIconInk) and where they sit: placement stays with the caller, because
// these live in bars that pack in different directions.
static void paintStatusIcons(lv_obj_t *gpsLabel, lv_obj_t *wifiLabel,
                             bool gpsEnabled, bool gpsFix, uint8_t gpsSatCount,
                             bool wifiApMode, bool wifiConnected,
                             const StatusIconInk &ink) {
    if (gpsLabel) {
        if (gpsEnabled) {
            lv_label_set_text_fmt(gpsLabel, TR("%s %u"), LV_SYMBOL_GPS, (unsigned)gpsSatCount);
        } else {
            lv_label_set_text(gpsLabel, LV_SYMBOL_GPS);
        }
        lv_obj_set_style_text_color(gpsLabel,
                                    (gpsEnabled && gpsFix) ? ink.good : ink.gpsBad, 0);
    }

    if (wifiLabel) {
        const bool wifiOffOrDisconnected = (!wifiApMode && !wifiConnected);
        lv_label_set_text(wifiLabel, wifiApMode ? LV_SYMBOL_UPLOAD : LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(
            wifiLabel,
            wifiApMode ? ink.ap : (wifiOffOrDisconnected ? ink.wifiBad : ink.good), 0);
        lv_obj_set_style_text_decor(
            wifiLabel,
            wifiOffOrDisconnected ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE,
            0);
    }
}

static void refreshHeaderStatus(bool force) {
    if (!s_chatHeaderGps || !s_chatHeaderWifi || !s_chatHeaderBattText || !s_chatHeaderBattBar) return;

    static uint32_t lastPollMs = 0;
    uint32_t now = millis();
    if (!force && (uint32_t)(now - lastPollMs) < 800) return;
    lastPollMs = now;

    const uint8_t battPct = batteryReadPercent();
    // Quantised to the two decimals actually drawn, so the early-out below
    // compares what the user can see rather than raw ADC jitter.
    const float battV = batteryReadVoltage();
    const uint16_t battCentiV = (uint16_t)(battV * 100.0f + 0.5f);
    bool gpsEnabled = false, gpsFix = false;
    uint8_t gpsSatCount = 0;
    gpsUiReading(gpsEnabled, gpsFix, gpsSatCount);   // held a minute on the P4
    wifi_mode_t wifiMode = WiFi.getMode();
    bool wifiApMode = (wifiMode == WIFI_AP);
#ifdef WIFI_AP_STA
    wifiApMode = wifiApMode || (wifiMode == WIFI_AP_STA);
#endif
    bool wifiConnected = (!wifiApMode && WiFi.status() == WL_CONNECTED);

    // Voltage is in the early-out alongside percent, not instead of it: on the
    // flat part of the discharge curve the percentage can sit still for tens of
    // millivolts, and a percent-only comparison would freeze a voltage reading
    // on screen until the curve finally moved.
    if (!force && battPct == s_lastBattPct && battCentiV == s_lastBattCentiV
        && gpsEnabled == s_lastGpsEnabled
        && gpsFix == s_lastGpsFix && gpsSatCount == s_lastGpsSats
        && wifiConnected == s_lastWifiConnected && wifiApMode == s_lastWifiApMode) {
        return;
    }

    // The dot stays a state-of-charge signal in both modes. A voltage reading
    // next to a green/amber/red dot is the pairing that makes voltage mode
    // useful — the thresholds are percentages and stay percentages.
    lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerBatteryDotColor(battPct), 0);
    if (s_cfg.battDisplayMode == BATT_DISPLAY_VOLTAGE) {
        // snprintf, not lv_label_set_text_fmt: LVGL's own printf has no float
        // support unless LV_SPRINTF_USE_FLOAT is set, which it is not here, so
        // "%.2fV" reaches the screen as a literal "fV". Every other float in
        // this file is formatted this way for the same reason.
        char battBuf[12];
#if UI_CARDPUTER_COMPACT
        // Same width treatment the percent branch gets on this panel: "4.02" is
        // four glyphs, exactly like "100".
        snprintf(battBuf, sizeof(battBuf), "%.2f", (double)battV);
#else
        snprintf(battBuf, sizeof(battBuf), "%.2fV", (double)battV);
#endif
        lv_label_set_text(s_chatHeaderBattText, battBuf);
    } else {
#if UI_CARDPUTER_COMPACT
        lv_label_set_text_fmt(s_chatHeaderBattText, "%u", (unsigned)battPct);
#else
        lv_label_set_text_fmt(s_chatHeaderBattText, "%u%%", (unsigned)battPct);
#endif
    }

    paintStatusIcons(s_chatHeaderGps, s_chatHeaderWifi,
                     gpsEnabled, gpsFix, gpsSatCount, wifiApMode, wifiConnected,
                     headerStatusInk());
#if HAS_STATUS_STRIP
    statusStripPaint(gpsEnabled, gpsFix, gpsSatCount, wifiApMode, wifiConnected,
                     battPct, battV,
                     headerBatteryDotColor(battPct));
#endif
    // The copies on DMs, Nodes, Config and Tools (buildScreenHeader()).
    for (ScreenHeader &h : s_screenHeaders) {
        if (!lvObjValid(h.bar)) continue;
        paintStatusIcons(h.gps, h.wifi,
                         gpsEnabled, gpsFix, gpsSatCount, wifiApMode, wifiConnected,
                         headerStatusInk());
        if (h.battDot) lv_obj_set_style_bg_color(h.battDot, headerBatteryDotColor(battPct), 0);
        if (h.battText) lv_label_set_text(h.battText, lv_label_get_text(s_chatHeaderBattText));
        layoutScreenHeader(h);
    }

    lv_obj_t *gpsParent = lv_obj_get_parent(s_chatHeaderGps);
    lv_obj_t *wifiParent = lv_obj_get_parent(s_chatHeaderWifi);
    const bool packFromRight = (gpsParent == s_chatShortcutBar)
                               || (s_chatStatusBox && gpsParent == s_chatStatusBox);
    if (gpsParent && gpsParent == wifiParent) {
        if (packFromRight) {
#if defined(DEVICE_TDECK_PRO)
            if (gpsParent == s_chatShortcutBar && !bottomNavEnabled()) {
                lv_obj_align(s_chatHeaderGps, LV_ALIGN_BOTTOM_RIGHT, -4, -1);
            } else {
                lv_obj_align(s_chatHeaderGps, LV_ALIGN_RIGHT_MID, -4, 0);
            }
#else
            lv_obj_align(s_chatHeaderGps, LV_ALIGN_RIGHT_MID, -4, 0);
#endif
            lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);
        } else {
            lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        }
    }
    if (s_chatDmAlert && lv_obj_is_valid(s_chatDmAlert)
        && lv_obj_get_parent(s_chatDmAlert) == wifiParent) {
        // Bottom shortcut bar packs icons from the right edge, so DM sits
        // left of wifi. The Heltec top bar packs from the left, so DM sits
        // right of wifi (between wifi and the battery indicator).
        if (packFromRight) {
            lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        } else {
            lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        }
        // The bell follows the envelope, on whichever side of wifi it landed:
        // outward from it, so the pair reads as one cluster either way.
        if (lvObjValid(s_chatChanAlert)
            && lv_obj_get_parent(s_chatChanAlert) == wifiParent) {
            lv_obj_align_to(s_chatChanAlert, s_chatDmAlert,
                            packFromRight ? LV_ALIGN_OUT_LEFT_MID : LV_ALIGN_OUT_RIGHT_MID,
                            packFromRight ? -4 : 4, 0);
        }
    }
#if HAS_NAV_BAR_TOGGLE
    // The copy on whichever modal is open. Its own box, always packed from the
    // right, so none of the parent-sniffing above applies to it.
    if (s_navStatusGps || s_navStatusWifi) {
        paintStatusIcons(s_navStatusGps, s_navStatusWifi,
                         gpsEnabled, gpsFix, gpsSatCount, wifiApMode, wifiConnected,
                         headerStatusInk());
        if (s_navStatusGps) lv_obj_align(s_navStatusGps, LV_ALIGN_RIGHT_MID, -2, 0);
        if (s_navStatusGps && s_navStatusWifi) {
            lv_obj_align_to(s_navStatusWifi, s_navStatusGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);
        }
        if (s_navStatusWifi && s_navStatusDm) {
            lv_obj_align_to(s_navStatusDm, s_navStatusWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        }
    }
#endif
#if UI_CHANNEL_LIST_DROPDOWN
    layoutHeaderInlineItems();
#endif

    s_lastBattPct = battPct;
    s_lastBattCentiV = battCentiV;
    s_lastGpsEnabled = gpsEnabled;
    s_lastGpsFix = gpsFix;
    s_lastGpsSats = gpsSatCount;
    s_lastWifiConnected = wifiConnected;
    s_lastWifiApMode = wifiApMode;
}

// Envelope icon left of the wifi icon that blinks whenever any DM
// conversation has unread messages. Cleared as soon as the user opens the
// conversation (DMs.markRead runs).
static void setDmAlertVisible(lv_obj_t *label, bool visible) {
    if (!label || !lv_obj_is_valid(label)) return;
    const bool currentlyHidden = lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN);
    if (visible && currentlyHidden) {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else if (!visible && !currentlyHidden) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refreshDmAlertIndicator() {
    const bool unread = DMs.hasUnread();
    // 500 ms on / 500 ms off, derived from the free-running millis() counter so
    // no phase has to be persisted across calls. Every indicator reads the same
    // clock, so where two are showing they blink together rather than chasing
    // each other.
    const bool blink = unread && (((millis() / 500UL) & 1UL) == 0UL);

#if UI_TOUCH_NAV_BAR
    // Where the bar is up, the alert goes on the cell that answers it. This is
    // a runtime question, not a build-time one: on the boards where the bar is a
    // setting it has to follow the setting, or turning the bar on leaves the
    // mark in the footer corner and the button it refers to at the other end of
    // the screen -- and both saying it at once is the same thing twice, a few
    // pixels apart.
    const bool onNav = bottomNavEnabled();
#if defined(DEVICE_TDECK_PRO)
    // Held, not blinked. Every flip is a full e-paper refresh: at 2 Hz that is
    // visibly slow, costs real battery, and would leave the bar flickering for
    // as long as anything is unread. Marked until the conversation is opened
    // says the same thing and costs one refresh.
    const bool cellLit = onNav && unread;
#else
    const bool cellLit = onNav && blink;
#endif

    // Only when it changes. This runs every loop pass, and re-asserting the
    // same colour would invalidate the label on every one of them.
    static bool primed = false;
    if (cellLit != s_navDmLit || !primed) {
        s_navDmLit = cellLit;
        primed = true;
        for (int i = 0; i < kNavDmIconSlots; i++) {
            if (s_navDmIcons[i] && !lv_obj_is_valid(s_navDmIcons[i])) {
                s_navDmIcons[i] = nullptr;   // its bar went with a closed modal
                continue;
            }
            navDmApplyInk(s_navDmIcons[i], cellLit);
        }
    }

    if (onNav) {
        // The footer envelope is still built -- other things in that strip are
        // laid out relative to it -- but with the bar up it never shows.
        setDmAlertVisible(s_chatDmAlert, false);
#if HAS_NAV_BAR_TOGGLE
        setDmAlertVisible(s_navStatusDm, false);
#endif
        return;
    }
#endif  // UI_TOUCH_NAV_BAR

    setDmAlertVisible(s_chatDmAlert, blink);
#if HAS_NAV_BAR_TOGGLE
    setDmAlertVisible(s_navStatusDm, blink);
#endif
}

// The same thing for unread *channel* traffic, split the same way and on the
// same 500 ms phase — so where both are showing they blink together as one
// cluster rather than chasing each other.
//
// Boards with a nav bar blink its Chats cell instead of drawing a glyph: the
// cell is already on screen, it is where the tap has to land, and a bar seven
// cells wide has no room to spend on a status icon that repeats what one of the
// cells could say itself. Boards without one get the bell beside the envelope.
static void refreshChatAlertIndicator() {
    // Not the channel being read. Its own mark waits out freshSeenMs() and
    // clears on leaving (serviceFreshMarks()), but the channel list never shows
    // it for the active row, and neither should this: the alert used to keep
    // blinking for a conversation that was open in front of the user.
    int reading = -1;
    if (!glanceOverlayHidesUi() && !openDmPeerNodeId() && chatPanelInFront()
        && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
        reading = s_activeChannel;
    }
    bool unread = false;
    for (int i = 0; i < MESH_CHANNELS && !unread; i++) {
        unread = s_channelNeedsAttention[i] && i != reading;
    }
    const bool blink = unread && (((millis() / 500UL) & 1UL) == 0UL);

#if UI_TOUCH_NAV_BAR
    const bool onNav = bottomNavEnabled();
#if defined(DEVICE_TDECK_PRO)
    const bool cellLit = onNav && unread;   // held, for the reason the DM copy gives
#else
    const bool cellLit = onNav && blink;
#endif

    static bool primed = false;
    if (cellLit != s_navChatLit || !primed) {
        s_navChatLit = cellLit;
        primed = true;
        for (int i = 0; i < kNavDmIconSlots; i++) {
            if (s_navChatIcons[i] && !lv_obj_is_valid(s_navChatIcons[i])) {
                s_navChatIcons[i] = nullptr;   // its bar went with a closed modal
                continue;
            }
            navChatApplyInk(s_navChatIcons[i], cellLit);
        }
    }

    if (onNav) {
        setDmAlertVisible(s_chatChanAlert, false);
#if HAS_NAV_BAR_TOGGLE
        setDmAlertVisible(s_navStatusChan, false);
#endif
        return;
    }
#endif  // UI_TOUCH_NAV_BAR

    setDmAlertVisible(s_chatChanAlert, blink);
#if HAS_NAV_BAR_TOGGLE
    setDmAlertVisible(s_navStatusChan, blink);
#endif
}

