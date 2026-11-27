// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: chat button row and main UI construction.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// P4 landscape with the keyboard expansion on: no Actions / New Message row
// under the chat. The keyboard reaches both, and the row's height goes to the
// message list, which is the flex_grow child of the chat panel and takes
// whatever a hidden row gives up. Built either way and only hidden, so the
// keyboard coming and going needs no rebuild (serviceChatBtnRowKeyboard()).
static void chatBtnRowApplyKeyboard() {
#if defined(DEVICE_TDISPLAY_P4)
    if (!lvObjValid(s_chatBtnRow)) return;
    if (!uiPortrait() && keyboardAttached()) {
        lv_obj_add_flag(s_chatBtnRow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_chatBtnRow, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

#if defined(DEVICE_TDISPLAY_P4)
// Once per loop: re-applies the above whenever the keyboard is attached or
// pulled, which keyboardAttachSeq() counts.
static void serviceChatBtnRowKeyboard() {
    static uint32_t seen = UINT32_MAX;
    const uint32_t seq = keyboardAttachSeq();
    if (seq == seen) return;
    seen = seq;
    chatBtnRowApplyKeyboard();
#if !UI_LARGE_PANEL_PROFILE
    // The bottom bar's height follows the keyboard too (p4NavBarHeight()), and
    // every screen is laid out around it, so a change is a rebuild.
    if (s_rootScreen && bottomNavEnabled() && !navIsSideColumn()
        && p4NavBarHeight() != kBottomNavHeight) {
        scheduleUiRebuild(s_cfgModal != nullptr);
    }
#endif
}
#endif

static void buildUi() {
#if defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
    // Before anything is laid out: the keyboard may have come or gone since boot.
    kBottomNavHeight = p4NavBarHeight();
#endif
#if HAS_NAV_BAR_TOGGLE
    s_appliedNavBar = bottomNavEnabled();
#endif
#if HAS_NAV_ALIGN_SETTING
    s_appliedNavAlign = cfgNavAlign(s_cfg);
#endif
#if HAS_CHANNEL_PANEL_LOCK
    s_appliedChannelPanelLocked = cfgChannelPanelLocked(s_cfg);
#endif
    lv_obj_t *screen = lv_obj_create(NULL);
    s_rootScreen = screen;
    s_channelSelectorFixedBtnW = 0;
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1E44), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Room the nav column takes off either side of the chat when the bar is one
    // (Nav Alignment Left/Right; navIsSideColumn()), a couple of pixels clear
    // of it. Both 0 with the bar along the bottom, or off.
    const int navGap = navIsSideColumn() ? 2 : 0;
    const int navPadL = navSideInsetLeft()  ? navSideInsetLeft()  + navGap : 0;
    const int navPadR = navSideInsetRight() ? navSideInsetRight() + navGap : 0;
    // How much height the bar takes off the bottom: none as a column.
    const int navBottomH = navIsSideColumn() ? 0 : kBottomNavHeight;
    LV_UNUSED(navPadL);
    LV_UNUSED(navPadR);
    LV_UNUSED(navBottomH);

    lv_obj_t *panel = nullptr;
// Full-width chat, no side panel: these boards put channels in the overlay
// dropdown (UI_CHANNEL_LIST_DROPDOWN) instead of an anchored list, so reserving
// a column for one leaves an empty strip down the side.
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || UI_TOUCH_ONLY_PROFILE || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9) \
    || UI_TANMATSU_LAYOUT
    const int panelMargin = 6;
#if UI_TANMATSU_LAYOUT
    // Height is what this panel is short of: the header sits near the top edge
    // and the chat close under it, the side margins as on the T-Deck.
    const int headerTop = 2;
    const int chatGap = 3;
#else
    const int headerTop = panelMargin;
    const int chatGap = 6;
#endif
#if HAS_NAV_BAR_TOGGLE
    // Taller than the 14 px key-hint strip when the bar is on, because it
    // carries tappable icon buttons rather than a line of text; the extra
    // pixels come out of the message list, which is what chatH measures. With
    // the bar off it is the old 14 again, so the screen is the one that shipped
    // before the bar down to the pixel.
#if defined(DEVICE_TDECK_PRO)
    const int chatLegendH = bottomNavEnabled() ? kBottomNavHeight : 30;
#else
    const int chatLegendH = bottomNavEnabled() ? kBottomNavHeight : 14;
#endif
#else
    const int chatLegendH = 14;
#endif
#else
    const int panelMargin = 0;
    const int headerTop = panelMargin;
    const int chatGap = 3;
    const int chatLegendH = 28;
#endif
#if defined(DEVICE_TDISPLAY_P4)
    // Portrait has height to spare: half as tall again, for a bigger channel
    // name and clock (see selectorTextFont / clockTextFont below). The chat
    // list is what gives it up -- it is sized from what is left over.
    // 35 leaves the selector (chatHeaderH - 6) 29 px round the 24's line --
    // room for its descenders and no more.
    const int chatHeaderH = uiPortrait() ? 35 : 25;
#elif defined(DEVICE_CROWPANEL_50)
    // Twice the usual, fonts with it (below): the 5" has the room, and the
    // header is read at arm's length. Every screen's header copies this one
    // (s_chatHdrGeom), so they all grow together.
    const int chatHeaderH = 50;
#elif UI_TANMATSU_LAYOUT
    // Native resolution: 20 units is 40 px, around the 24 px name and clock
    // (montserrat_12, below) with the 10s beside them. The selector
    // (chatHeaderH - 6) is 28 px, which holds the 12's 27 px line, descenders
    // included, once the name is not nudged down (selectorTextYOffset).
    const int chatHeaderH = 20;
#else
    const int chatHeaderH = 25;
#endif
    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
#if defined(DEVICE_TDISPLAY_P4)
    // Large-panel landscape: the Pager's anchored channel column down the left,
    // and the chat and its header beside it. The pixel-doubled landscape slides
    // the list in instead (channelListIsDropdown()), so this is 0 there. 120 px against the Pager's 89, for this
    // panel's longer names and its 616 px. The column itself is the dropdown
    // list, re-dressed once the buttons are in it (see the end of buildUi()).
    const int p4ChannelColW = channelListIsDropdown() ? 0 : 120;
    // With the nav as a side column, the chat and its header stop short of it
    // (navPadL / navPadR), on whichever side it is.
    const int chatX = panelMargin + navPadL + (p4ChannelColW > 0 ? p4ChannelColW + 4 : 0);
#elif HAS_CHANNEL_PANEL_LOCK
    // Channel panel locked: the P4's anchored column, narrower for this
    // 400 px-wide layout. Unlocked, the list is the drawer and this is 0.
    const int p4ChannelColW = channelListIsDropdown() ? 0 : 96;
    const int chatX = panelMargin + navPadL + (p4ChannelColW > 0 ? p4ChannelColW + 4 : 0);
#else
    const int chatX = panelMargin + navPadL;
#endif
    const int chatW = screenW - chatX - panelMargin - navPadR;
    const int chatY = headerTop + chatHeaderH + chatGap;
#if UI_TOUCH_NAV_BAR
    // The bar runs the full width of the display and sits flush with the bottom
    // edge — the same place and size the copy inside every modal takes, so the
    // buttons stay put as you navigate instead of shifting by the panel margin
    // on the way in and back out. The margin is therefore not subtracted here:
    // there is nothing below the bar to leave room for.
    //
    // On a touch-only board this is what the arithmetic already worked out to
    // (panelMargin 0, chatLegendH 28 == kBottomNavHeight); spelling it out is
    // what makes it true on the boards where the margin is 6.
    // A side column takes no height: the chat runs to the bottom.
    const int chatH = bottomNavEnabled()
                      ? (screenH - chatY - navBottomH - 3)
                      : (screenH - panelMargin - chatY - chatLegendH - 3);
#else
    const int chatH = screenH - panelMargin - chatY - chatLegendH - 3;
#endif
#elif UI_CARDPUTER_COMPACT
    const int panelMargin = 2;
    const int headerTop = panelMargin;
    const int chatGap = 0;
#if defined(DEVICE_CARDPUTER_ZERO)
    // 35 px more height than the ADV and smaller-looking glass: a 26 px bar
    // takes the 12 px faces (>= 25 below) for the name, clock and battery.
    // Every screen's header copies this one (s_chatHdrGeom).
    const int chatHeaderH = 26;
#else
    const int chatHeaderH = 22;
#endif
#if HAS_NAV_BAR_TOGGLE
    // The bar costs this board more than any other: 28 px of a 135 px panel
    // against the 12 the hint strip takes, out of the shortest message list of
    // any build. It is still the same 28 rather than a Cardputer-sized variant,
    // because a shorter bar here would be a second set of cell metrics to keep
    // right and the whole point of the setting is that the bar looks the way it
    // looks. Off is the default, and off is the screen that shipped.
    const int chatLegendH = bottomNavEnabled() ? kBottomNavHeight : 12;
#else
    const int chatLegendH = 12;
#endif
    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    const int chatX = panelMargin + navPadL;
    const int chatW = screenW - chatX - panelMargin - navPadR;
    const int chatY = panelMargin + chatHeaderH + chatGap;
#if UI_TOUCH_NAV_BAR
    // Flush with the bottom edge with the bar up, so the margin is not
    // subtracted — the same arithmetic the boards above do, for the same
    // reason: there is nothing below the bar to leave room for.
    const int chatH = bottomNavEnabled()
                      ? (screenH - chatY - navBottomH - 3)
                      : (screenH - panelMargin - chatY - chatLegendH - 3);
#else
    const int chatH = screenH - panelMargin - chatY - chatLegendH - 3;
#endif
#else
    const int panelMargin = 6;
    const int panelW = 89;
    const int panelH = lv_disp_get_ver_res(NULL) - panelMargin * 2;
#if defined(DEVICE_TLORA_PAGER_TFT)
    // 222 px tall: the header and the channel panel start near the top edge
    // and the chat close under the header, keeping the side margins.
    const int headerTop = 2;
    const int chatGap = 3;
#else
    const int headerTop = panelMargin;
    const int chatGap = 6;
#endif
    const int chatHeaderH = 25;
#if HAS_NAV_BAR_TOGGLE
    // Same two states as the boards above: 28 px of icon cells with the bar on,
    // the old 14 px text strip with it off. This is the board with the most
    // room for it — 480 px wide, and its hint line already reads as a nav bar
    // in words.
    const int chatLegendH = bottomNavEnabled() ? kBottomNavHeight : 14;
#else
    const int chatLegendH = 14;
#endif

    // Worked out before the panel is built rather than after, because on the
    // boards whose key-hint bar spans the whole display the panel has to know
    // where that bar starts. The chat column itself is unchanged by any of it.
    // A nav column on the left moves the channel panel over with the chat.
    const int panelX = panelMargin + navPadL;
    const int chatX = panelX + panelW + chatGap;
    const int chatW = lv_disp_get_hor_res(NULL) - chatX - panelMargin - navPadR;
    const int chatY = headerTop + chatHeaderH + chatGap;
#if UI_TOUCH_NAV_BAR
    // With the bar up it sits flush with the bottom edge, so the bottom margin
    // panelH reserves is not subtracted; with it off this is the expression it
    // has always been, written out rather than in terms of panelH so the two
    // states read side by side. A side column takes no height at all.
    const int chatH = bottomNavEnabled()
                      ? (lv_disp_get_ver_res(NULL) - chatY - navBottomH - 3)
                      : (panelH - chatHeaderH - chatGap - chatLegendH - 3);
#else
    const int chatH = panelH - chatHeaderH - chatGap - chatLegendH - 3;
#endif
    // Where the footer starts, whichever footer it is. The channel list stops
    // here, so it follows the bar up and down without a second calculation.
    const int legendTop = chatY + chatH + 3;

#if defined(DEVICE_TLORA_PAGER_TFT)
    // The bar runs edge to edge on this board (see where it is aligned, below),
    // so the channel list stops above it instead of running down beside it.
    // Anchored to the top rather than centred, because LV_ALIGN_LEFT_MID would
    // put half of the height it just gave up straight back under the bar.
    const int panelBodyH = legendTop - headerTop;
#else
    const int panelBodyH = panelH;
#endif

    panel = lv_obj_create(screen);
    lv_obj_set_size(panel, panelW, panelBodyH);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, panelX, headerTop);
#else
    lv_obj_align(panel, LV_ALIGN_LEFT_MID, panelX, 0);
#endif
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(panel, 1, 0);
    lv_obj_set_style_pad_right(panel, 1, 0);
    lv_obj_set_style_pad_top(panel, 3, 0);
    lv_obj_set_style_pad_bottom(panel, 3, 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
    static lv_coord_t panelCols[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    // Built from MESH_CHANNELS rather than written out, because a hand-written
    // template silently swallows the channels past its end: a row with no track
    // to sit in is simply not placed, so the extra channels would vanish from
    // this panel with nothing to indicate why.
    static lv_coord_t panelRows[MESH_CHANNELS + 1];
    for (int r = 0; r < MESH_CHANNELS; r++) panelRows[r] = LV_GRID_FR(1);
    panelRows[MESH_CHANNELS] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_style_pad_column(panel, 4, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(panel, panelCols, panelRows);
#else
    lv_obj_set_style_pad_row(panel, 5, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#endif

#endif

    // Children align to the header's content area, so anything that has to line
    // up with a header child from outside the bar owes it these pixels.
    const int kChatHeaderBorderW = 1;
    const int kChatHeaderPad = 2;

    s_chatHeaderBar = lv_obj_create(screen);
    lv_obj_set_size(s_chatHeaderBar, chatW, chatHeaderH);
    lv_obj_align(s_chatHeaderBar, LV_ALIGN_TOP_LEFT, chatX, headerTop);
    lv_obj_clear_flag(s_chatHeaderBar, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(s_chatHeaderBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chatHeaderBar, kChatHeaderBorderW, 0);
    lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(
        s_chatHeaderBar,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? chatPanelBackgroundColor() : lv_color_hex(0x0E285B),
        0);
    lv_obj_set_style_bg_opa(s_chatHeaderBar, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_60 : LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatHeaderBar, kChatHeaderBorderW, 0);
    lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_pad_all(s_chatHeaderBar, kChatHeaderPad, 0);
    cornerSafeHeader(s_chatHeaderBar);

    const lv_font_t *headerTextFont = (chatHeaderH >= 25) ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
    // Clock gets a larger font than the other header items so the time reads at a glance.
    // Cardputer keeps the original clock size to fit its narrow header.
#if UI_CARDPUTER_COMPACT
    const lv_font_t *clockTextFont = headerTextFont;
#elif defined(DEVICE_TDECK_PRO) || UI_TANMATSU_LAYOUT
    const lv_font_t *clockTextFont = &lv_font_montserrat_12;
#else
    const lv_font_t *clockTextFont = (chatHeaderH >= 25) ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
#endif
#if defined(DEVICE_TDISPLAY_P4)
    if (uiPortrait()) clockTextFont = &lv_font_montserrat_24;   // the taller header
#endif
#if UI_TOUCH_ONLY_PROFILE
    const lv_font_t *headerIconFont = (chatHeaderH >= 25) ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
#endif
#if defined(DEVICE_CROWPANEL_50)
    // The doubled header (chatHeaderH): each face twice its usual size.
    headerTextFont = &lv_font_montserrat_24;
    clockTextFont = &lv_font_montserrat_32;
    headerIconFont = &lv_font_montserrat_28;
    const int headerPadX = 12;
#else
    const int headerPadX = (chatHeaderH >= 25) ? 6 : 4;
#endif
#if UI_CHANNEL_LIST_DROPDOWN
    s_channelStrip = nullptr;
    s_channelList = nullptr;
    s_channelDrawerOpen = false;
    s_channelDrawerScrim = nullptr;

    const int selectorBtnH = chatHeaderH - 6;
#if UI_TOUCH_ONLY_PROFILE
    const bool compactHeltecSelector = useCompactVerticalHeltecSelector();
    const int selectorBtnW = compactHeltecSelector
        ? max((int)lv_font_get_line_height(headerTextFont) + 4, 14)
        : min(max(chatW / 3, 96), 220);
    const int selectorBtnOffsetX = compactHeltecSelector ? 1 : headerPadX;
#elif UI_CARDPUTER_COMPACT
    const int selectorBtnW = min(max(chatW / 3, 84), 108);
    const int selectorBtnOffsetX = headerPadX;
#else
    const int selectorBtnW = min(max(chatW / 3, 96), 220);
    const int selectorBtnOffsetX = headerPadX;
#endif

// No caret on these: the selector reads as a plain channel name, which is the
// T-Deck presentation. It also buys the label the 14 px the caret reserved,
// which matters more here than on the T-Deck — same 320x240 pixels, smaller
// glass, so channel names have less room to be legible in.
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9) \
    || UI_TANMATSU_LAYOUT
    const bool showSelectorCaret = false;
#elif UI_TOUCH_ONLY_PROFILE
    const bool showSelectorCaret = false;   // see refreshChannelSelectorLabel()
#elif UI_CARDPUTER_COMPACT
    const bool showSelectorCaret = false;
#else
    const bool showSelectorCaret = true;
#endif

#if UI_CARDPUTER_COMPACT || UI_TANMATSU_LAYOUT
    const lv_coord_t selectorTextYOffset = 0;
#else
    const lv_coord_t selectorTextYOffset = 1;
#endif

    // The clock's face: the name is header text now, not a button's label, so
    // it takes the one text style the header already has -- which also carries
    // the per-board sizes (the P4 portrait header's larger face, the Pro's and
    // Cardputer's smaller ones) without a second table of them. The selector's
    // width is still measured from the widest name in it
    // (refreshChannelSelectorLabel()).
    const lv_font_t *selectorTextFont = clockTextFont;

    s_channelSelectorBtn = lv_btn_create(s_chatHeaderBar);
    // Every board, not just the Pro now: the theme's button styles are what
    // give it a fill, and a pressed state that darkens it -- neither of which
    // header text should have. It stays clickable; see below.
    lv_obj_remove_style_all(s_channelSelectorBtn);
    lv_obj_add_flag(s_channelSelectorBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_channelSelectorBtn, selectorBtnW, selectorBtnH);
    lv_obj_align(s_channelSelectorBtn, LV_ALIGN_LEFT_MID, selectorBtnOffsetX, 0);
    lv_obj_set_style_radius(s_channelSelectorBtn, 6, 0);
#if UI_TOUCH_ONLY_PROFILE
    if (compactHeltecSelector) {
        lv_obj_set_style_pad_left(s_channelSelectorBtn, 1, 0);
        lv_obj_set_style_pad_right(s_channelSelectorBtn, 1, 0);
        lv_obj_set_style_pad_top(s_channelSelectorBtn, 1, 0);
        lv_obj_set_style_pad_bottom(s_channelSelectorBtn, 1, 0);
    } else {
        lv_obj_set_style_pad_left(s_channelSelectorBtn, 6, 0);
        lv_obj_set_style_pad_right(s_channelSelectorBtn, 6, 0);
        lv_obj_set_style_pad_top(s_channelSelectorBtn, 2, 0);
        lv_obj_set_style_pad_bottom(s_channelSelectorBtn, 2, 0);
    }
#else
    lv_obj_set_style_pad_left(s_channelSelectorBtn, 6, 0);
    lv_obj_set_style_pad_right(s_channelSelectorBtn, 6, 0);
    lv_obj_set_style_pad_top(s_channelSelectorBtn, 2, 0);
    lv_obj_set_style_pad_bottom(s_channelSelectorBtn, 2, 0);
#endif
    lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
    // Looks like header text, the name of the chat being read, but a tap on it
    // still opens the channel drawer -- as does a swipe in from the left edge,
    // or the keys that always opened it. Kept as the same object (the header
    // lays itself out around it) with the button look taken off.
    // refreshChannelGlow() and applyChannelButtonTheme() keep it bare.
    lv_obj_add_event_cb(s_channelSelectorBtn, onChannelSelectorPressed,
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_channelSelectorBtn, 0, 0);
    lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
    // The name starts at the header's own inset, the same distance in from
    // the left that the battery sits from the right -- not a further 6 px
    // in, which was the button's padding around its label.
    lv_obj_set_style_pad_left(s_channelSelectorBtn, 0, 0);

    s_channelSelectorLabel = lv_label_create(s_channelSelectorBtn);
    lv_obj_set_style_text_font(s_channelSelectorLabel, selectorTextFont, 0);
    lv_obj_set_style_text_align(s_channelSelectorLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_channelSelectorLabel, LV_LABEL_LONG_DOT);
    labelSingleLine(s_channelSelectorLabel);
#if UI_TOUCH_ONLY_PROFILE
    if (compactHeltecSelector) {
        lv_obj_set_width(s_channelSelectorLabel, 1);
        lv_obj_align(s_channelSelectorLabel, LV_ALIGN_LEFT_MID, 0, selectorTextYOffset);
        lv_obj_add_flag(s_channelSelectorLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_width(s_channelSelectorLabel, showSelectorCaret ? (selectorBtnW - 22) : (selectorBtnW - 8));
        lv_obj_align(s_channelSelectorLabel, LV_ALIGN_LEFT_MID, 4, selectorTextYOffset);
    }
#else
    lv_obj_set_width(s_channelSelectorLabel, showSelectorCaret ? (selectorBtnW - 22) : (selectorBtnW - 8));
    lv_obj_align(s_channelSelectorLabel, LV_ALIGN_LEFT_MID, 4, selectorTextYOffset);
#endif

    s_channelSelectorCaretLabel = lv_label_create(s_channelSelectorBtn);
    lv_obj_set_style_text_font(s_channelSelectorCaretLabel, selectorTextFont, 0);
    lv_obj_set_style_text_color(s_channelSelectorCaretLabel, lv_color_hex(0xD9E8FF), 0);
#if UI_TOUCH_ONLY_PROFILE
    if (compactHeltecSelector) {
        lv_obj_align(s_channelSelectorCaretLabel, LV_ALIGN_CENTER, 0, selectorTextYOffset);
    } else {
        lv_obj_align(s_channelSelectorCaretLabel, LV_ALIGN_RIGHT_MID, -5, selectorTextYOffset);
    }
#else
    lv_obj_align(s_channelSelectorCaretLabel, LV_ALIGN_RIGHT_MID, -5, selectorTextYOffset);
#endif
    if (!showSelectorCaret) {
        lv_obj_add_flag(s_channelSelectorCaretLabel, LV_OBJ_FLAG_HIDDEN);
    }
#else
    s_channelSelectorBtn = nullptr;
    s_channelSelectorLabel = nullptr;
    s_channelSelectorCaretLabel = nullptr;
    s_channelStrip = nullptr;
    s_channelList = nullptr;
#endif

    s_chatHeaderTime = lv_label_create(s_chatHeaderBar);
    lv_obj_align(s_chatHeaderTime, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_chatHeaderTime, clockTextFont, 0);
    lv_obj_set_style_text_color(s_chatHeaderTime, lv_color_hex(0xD9E8FF), 0);

#if UI_TOUCH_ONLY_PROFILE
    s_chatHeaderGps = lv_label_create(s_chatHeaderBar);
    lv_obj_align_to(s_chatHeaderGps, s_channelSelectorBtn, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_text_font(s_chatHeaderGps, headerTextFont, 0);
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
#else
    s_chatHeaderGps = nullptr;
#endif

    s_chatHeaderBattBar = lv_obj_create(s_chatHeaderBar);
    lv_obj_remove_style_all(s_chatHeaderBattBar);
    {
#if UI_CARDPUTER_COMPACT
        const int battW = 6;
#elif defined(DEVICE_CROWPANEL_50)
        const int battW = 16;
#else
        const int battW = (chatHeaderH >= 25) ? 8 : 7;
#endif
#if defined(DEVICE_CROWPANEL_50)
        const int battH = 16;
#else
        const int battH = (chatHeaderH >= 25) ? 8 : 7;
#endif
        lv_obj_set_size(s_chatHeaderBattBar, battW, battH);
    }
    lv_obj_align(s_chatHeaderBattBar, LV_ALIGN_RIGHT_MID, -headerPadX, 0);
    lv_obj_set_style_radius(s_chatHeaderBattBar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_chatHeaderBattBar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerGoodGreenColor(), 0);
    lv_obj_set_style_border_width(s_chatHeaderBattBar, 1, 0);
    lv_obj_set_style_border_color(s_chatHeaderBattBar, lv_color_hex(0x274A84), 0);
    lv_obj_set_style_pad_all(s_chatHeaderBattBar, 0, 0);

    s_chatHeaderBattText = lv_label_create(s_chatHeaderBar);
    lv_obj_align_to(s_chatHeaderBattText, s_chatHeaderBattBar, LV_ALIGN_OUT_LEFT_MID, -3, 0);
    lv_obj_set_style_text_font(s_chatHeaderBattText, headerTextFont, 0);
    lv_obj_set_style_text_color(s_chatHeaderBattText, lv_color_hex(0xBFD6FF), 0);

#if UI_TOUCH_ONLY_PROFILE
    s_chatHeaderWifi = lv_label_create(s_chatHeaderBar);
    lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_font(s_chatHeaderWifi, headerIconFont, 0);
    lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);

    s_chatDmAlert = lv_label_create(s_chatHeaderBar);
    lv_obj_set_style_text_font(s_chatDmAlert, headerIconFont, 0);
    lv_obj_set_style_text_color(s_chatDmAlert, lv_color_hex(0xF4D35E), 0);
    lv_label_set_text(s_chatDmAlert, LV_SYMBOL_ENVELOPE);
    // Heltec top-bar order is selector → gps → wifi → …; sit the envelope
    // immediately right of the wifi icon so it doesn't overlap gps.
    lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_add_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);
#else
    s_chatHeaderWifi = nullptr;
    s_chatDmAlert = nullptr;
    s_chatChanAlert = nullptr;
#endif

    s_chatHdrGeom.margin = panelMargin;
    s_chatHdrGeom.top = headerTop;
    s_chatHdrGeom.h = chatHeaderH;
#if UI_CHANNEL_LIST_DROPDOWN
    // Where refreshChannelSelectorLabel() leaves the name: flush with the
    // selector's left edge, or 4 px in where a caret shares the button.
#if UI_TOUCH_ONLY_PROFILE
    s_chatHdrGeom.titleX = compactHeltecSelector
        ? headerPadX
        : selectorBtnOffsetX + (showSelectorCaret ? 4 : 0);
#else
    s_chatHdrGeom.titleX = selectorBtnOffsetX + (showSelectorCaret ? 4 : 0);
#endif
    s_chatHdrGeom.textYOffset = selectorTextYOffset;
#else
    s_chatHdrGeom.titleX = headerPadX + 4;
    s_chatHdrGeom.textYOffset = 1;
#endif
    s_chatHdrGeom.padX = headerPadX;
    s_chatHdrGeom.battDotW = lv_obj_get_style_width(s_chatHeaderBattBar, LV_PART_MAIN);
    s_chatHdrGeom.battDotH = lv_obj_get_style_height(s_chatHeaderBattBar, LV_PART_MAIN);
    s_chatHdrGeom.clockFont = clockTextFont;
    s_chatHdrGeom.textFont = headerTextFont;
#if UI_TOUCH_ONLY_PROFILE
    s_chatHdrGeom.iconFont = headerIconFont;
#else
    s_chatHdrGeom.iconFont = headerTextFont;
#endif

#if UI_CHANNEL_LIST_DROPDOWN
    layoutHeaderInlineItems();
#endif

    s_chatPanel = lv_obj_create(screen);
    lv_obj_set_size(s_chatPanel, chatW, chatH);
    lv_obj_align(s_chatPanel, LV_ALIGN_TOP_LEFT, chatX, chatY);
    lv_obj_clear_flag(s_chatPanel, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(s_chatPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chatPanel, 1, 0);
    lv_obj_set_style_border_color(s_chatPanel, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(
        s_chatPanel,
        chatPanelBackgroundColor(),
        0);
    lv_obj_set_style_bg_opa(s_chatPanel, chatPanelBackgroundOpa(), 0);
    lv_obj_set_style_border_width(s_chatPanel, 1, 0);
    lv_obj_set_style_border_color(s_chatPanel, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_pad_all(s_chatPanel, 4, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_obj_set_style_pad_row(s_chatPanel, 4, 0);
    lv_obj_set_flex_flow(s_chatPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#endif

    s_chatList = lv_obj_create(s_chatPanel);
#if UI_TOUCH_ONLY_PROFILE
    lv_obj_set_width(s_chatList, lv_pct(100));
    lv_obj_set_flex_grow(s_chatList, 1);
#else
    lv_obj_set_size(s_chatList, lv_pct(100), lv_pct(100));
    lv_obj_align(s_chatList, LV_ALIGN_TOP_LEFT, 0, 0);
#endif
    lv_obj_add_flag(s_chatList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_chatList);
    lv_obj_set_scrollbar_mode(s_chatList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_chatList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chatList, 0, 0);
    lv_obj_set_style_pad_all(s_chatList, 0, 0);
#if defined(DEVICE_TDECK) || defined(DEVICE_M9)
    lv_obj_set_style_pad_right(s_chatList, 0, 0);
#else
    lv_obj_set_style_pad_right(s_chatList, 6, 0);
#endif
    lv_obj_set_style_pad_row(s_chatList, 1, 0);
    lv_obj_set_style_width(s_chatList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_chatList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_chatList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_chatList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_chatList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // Load older history when the user scrolls to the top of the render window.
    lv_obj_add_event_cb(s_chatList, onChatListScroll, LV_EVENT_SCROLL, nullptr);

#if UI_CHANNEL_LIST_DROPDOWN
    const lv_font_t *channelNavFont = kMainScreenFont;
#if defined(DEVICE_MESH_DECK)
    // Same size as the selector button's own label, so the open list reads as
    // that button unfolded rather than as a smaller menu below it.
    channelNavFont = &lv_font_montserrat_14;
#elif defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
    channelNavFont = &lv_font_montserrat_14; // nearest built-in to requested size 13
#if HAS_CHANNEL_PANEL_LOCK
    // Docked, the column is read down at native resolution: 10 is drawn as a
    // 20 px face there, against the 28 px the drawer's 14 comes out at.
    if (!channelListIsDropdown()) channelNavFont = &lv_font_montserrat_10;
#endif
#elif defined(DEVICE_CROWPANEL_50)
    // Its list text as it is (kMainScreenFont, drawn as 20), not the 14 the
    // other touch boards take: the 800x480 panel has the room.
#elif UI_TOUCH_ONLY_PROFILE
    if (!useCompactVerticalHeltecSelector()) channelNavFont = &lv_font_montserrat_14; // keep vertical Heltec unchanged
#endif
#if defined(DEVICE_MESH_DECK)
    // Rows have to clear the larger font: a row spends 2 px of padding and up to
    // 2 px of border per side, and anything shorter clips the glyphs.
    const int channelNavBtnH =
        max(kMainScreenChannelBtnHeight, (int)lv_font_get_line_height(channelNavFont) + 8);
#elif HAS_CHANNEL_PANEL_LOCK && UI_TANMATSU_LAYOUT
    // Docked rows shrink with their font: 2 px padding and up to 2 px of border
    // a side around an 11-unit line still clears in 20.
    const int channelNavBtnH = channelListIsDropdown() ? kMainScreenChannelBtnHeight : 20;
#else
    const int channelNavBtnH = kMainScreenChannelBtnHeight;
#endif

    {
#if defined(DEVICE_MESH_DECK)
        // Placeholder only. The real width is the selector button's, which is
        // measured from the channel names after this point, so
        // fitChannelDropdownToButtonContent() sets the final width once the
        // button has settled.
        const int dropdownW = selectorBtnW;
#else
#if defined(DEVICE_CROWPANEL_50)
        // Wider for its larger names (channelNavFont above).
        const int dropdownW = min(max(chatW / 2, 120), 420);
#else
        const int dropdownW = min(max(chatW / 2, 120), 260);
#endif
#endif

        // Behind the drawer and over everything else: a tap anywhere outside
        // the list closes it. Built first so the list, raised after it on every
        // open, sits on top. No dim on the Pro, where a partial opacity dithers.
        s_channelDrawerScrim = lv_obj_create(screen);
        lv_obj_remove_style_all(s_channelDrawerScrim);
        lv_obj_set_size(s_channelDrawerScrim, lv_disp_get_hor_res(NULL),
                        lv_disp_get_ver_res(NULL));
        lv_obj_set_pos(s_channelDrawerScrim, 0, 0);
        lv_obj_set_style_bg_color(s_channelDrawerScrim, lv_color_black(), 0);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(s_channelDrawerScrim, LV_OPA_TRANSP, 0);
#else
        lv_obj_set_style_bg_opa(s_channelDrawerScrim, LV_OPA_40, 0);
#endif
        lv_obj_add_flag(s_channelDrawerScrim,
                        (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
        lv_obj_clear_flag(s_channelDrawerScrim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_channelDrawerScrim, onChannelDrawerScrimPressed,
                            LV_EVENT_CLICKED, nullptr);

        // The drawer: the chat's full height, flush with the left edge of the
        // display. Its x is the slide's (channelDrawerSlide()); it starts
        // hidden, and is placed off screen each time it opens.
        s_channelList = lv_obj_create(screen);
#if defined(DEVICE_TDECK_PRO)
        // From the top of the screen, not the top of the chat. At the chat's
        // own height the drawer's edges fell exactly on the chat box's border
        // lines, and with no shadow or dim on this 1-bit panel to lift it off
        // the page, the chat box just looked narrower -- its border ending at
        // the drawer as though it had moved over to make room. Covering the
        // header too, with a border all round (below), makes it plainly a
        // sheet laid over the screen.
        lv_obj_set_size(s_channelList, dropdownW, chatY + chatH);
        lv_obj_set_pos(s_channelList, navSideInsetLeft(), 0);
#else
        lv_obj_set_size(s_channelList, dropdownW, chatH);
        // Beside a nav column on the left, not under it (channelDrawerSlide()).
        lv_obj_set_pos(s_channelList, navSideInsetLeft(), chatY);
#endif
        // v9 made lv_obj_flag_t a real enum, so an OR of two flags is an int in
        // C++ and needs the cast back.
        lv_obj_add_flag(s_channelList,
                        (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_HIDDEN));
        setupVScroll(s_channelList);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_scrollbar_mode(s_channelList, LV_SCROLLBAR_MODE_OFF);
    #else
        lv_obj_set_scrollbar_mode(s_channelList, LV_SCROLLBAR_MODE_AUTO);
    #endif
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_color(s_channelList, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_bg_opa(s_channelList, LV_OPA_COVER, 0);
        // White on white otherwise: the one edge that shows is the one that
        // has to say where the drawer stops.
        lv_obj_set_style_border_width(s_channelList, 2, 0);
        lv_obj_set_style_border_color(s_channelList, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(s_channelList, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_channelList, 1, 0);
        lv_obj_set_style_border_color(s_channelList, lv_color_hex(0x335D9D), 0);
    #endif
        // A drawer, not a popup: square, and edged only on the side that
        // faces the chat -- the others are the display's own edges. Except on
        // the Pro, where the bottom edge sits over the chat and has to be
        // drawn for the drawer to read as on top of it (see above).
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_border_side(s_channelList,
            (lv_border_side_t)(LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), 0);
#else
        lv_obj_set_style_border_side(s_channelList, LV_BORDER_SIDE_RIGHT, 0);
#endif
        lv_obj_set_style_radius(s_channelList, 0, 0);
        lv_obj_set_style_pad_all(s_channelList, 4, 0);
        lv_obj_set_style_pad_row(s_channelList, 4, 0);
        lv_obj_set_style_width(s_channelList, 2, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(s_channelList, LV_OPA_70, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(s_channelList, 2, LV_PART_SCROLLBAR);
        lv_obj_set_flex_flow(s_channelList, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_channelList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_move_foreground(s_channelList);
    }
#endif

#if UI_TOUCH_ONLY_PROFILE
    // Actions and New Message share the strip under the chat: both act on the
    // conversation being read, so they belong next to it rather than in the
    // bottom nav, which is about going somewhere else. Actions is the narrow one
    // — it opens a menu, while New Message is the button this screen exists for.
    //
    // Split by flex_grow 1 : 2 rather than percentages, so the row's column gap
    // comes out of the total instead of pushing the pair past 100% and wrapping.
    // 22, not 28. The label inside is montserrat_10, so 22 still leaves ~4 px
    // of clear space above and below the text — enough that the buttons do not
    // read as cramped — and the six pixels go straight to the message list,
    // which is the flex_grow child of this panel and takes whatever the fixed
    // rows leave behind.
#if UI_LARGE_PANEL_PROFILE
    const int chatBtnRowH = 56;
    const lv_font_t *chatActionFont = &lv_font_montserrat_18;
#elif defined(DEVICE_CROWPANEL_50)
    // Twice the height and the text (named 18: drawn as 20, see
    // hal/crowpanel_50_fonts.h), with the nav bar and header.
    const int chatBtnRowH = 44;
    const lv_font_t *chatActionFont = &lv_font_montserrat_18;
#else
    const int chatBtnRowH = 22;
    const lv_font_t *chatActionFont = &lv_font_montserrat_10;
#endif

    lv_obj_t *chatBtnRow = lv_obj_create(s_chatPanel);
    lv_obj_set_width(chatBtnRow, lv_pct(100));
    lv_obj_set_height(chatBtnRow, chatBtnRowH);
    lv_obj_clear_flag(chatBtnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(chatBtnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chatBtnRow, 0, 0);
    lv_obj_set_style_pad_all(chatBtnRow, 0, 0);
    lv_obj_set_style_pad_column(chatBtnRow, 4, 0);
    lv_obj_set_flex_flow(chatBtnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chatBtnRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_chatActBtn = lv_btn_create(chatBtnRow);
    lv_obj_set_flex_grow(s_chatActBtn, 1);
    lv_obj_set_height(s_chatActBtn, lv_pct(100));
    lv_obj_set_style_radius(s_chatActBtn, 5, 0);
    lv_obj_set_style_pad_all(s_chatActBtn, 2, 0);
    lv_obj_set_style_shadow_width(s_chatActBtn, 0, 0);
    lv_obj_set_style_bg_color(s_chatActBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(s_chatActBtn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatActBtn, 1, 0);
    lv_obj_set_style_border_color(s_chatActBtn, lv_color_hex(0x335D9D), 0);
    // The nav button's own handler, so this opens and closes the same modal the
    // same way. On CLICKED rather than the nav bar's PRESSED, to match the
    // button it now sits beside.
    lv_obj_add_event_cb(s_chatActBtn,
                        onHeltecBottomNavPressed,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)HELTEC_NAV_ACTIONS);

    lv_obj_t *chatActLabel = lv_label_create(s_chatActBtn);
    lv_obj_set_style_text_font(chatActLabel, chatActionFont, 0);
    lv_obj_set_style_text_color(chatActLabel, lv_color_hex(0xE8F1FF), 0);
    // "Actions" does not fit the button at portrait width.
    lv_label_set_text(chatActLabel, uiPortrait() ? TR("Act") : TR("Actions"));
    lv_obj_center(chatActLabel);

    s_chatNewMsgBtn = lv_btn_create(chatBtnRow);
    lv_obj_set_flex_grow(s_chatNewMsgBtn, 2);
    lv_obj_set_height(s_chatNewMsgBtn, lv_pct(100));
    lv_obj_set_style_radius(s_chatNewMsgBtn, 5, 0);
    lv_obj_set_style_pad_all(s_chatNewMsgBtn, 2, 0);
    lv_obj_set_style_shadow_width(s_chatNewMsgBtn, 0, 0);
    lv_obj_set_style_bg_color(s_chatNewMsgBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(s_chatNewMsgBtn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatNewMsgBtn, 1, 0);
    lv_obj_set_style_border_color(s_chatNewMsgBtn, lv_color_hex(0x335D9D), 0);
    lv_obj_add_event_cb(s_chatNewMsgBtn, onChatNewMessagePressed, LV_EVENT_CLICKED, nullptr);

    s_chatNewMsgLabel = lv_label_create(s_chatNewMsgBtn);
    lv_obj_set_style_text_font(s_chatNewMsgLabel, chatActionFont, 0);
    lv_obj_set_style_text_color(s_chatNewMsgLabel, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(s_chatNewMsgLabel, TR("New Message"));
    lv_obj_center(s_chatNewMsgLabel);
    s_chatBtnRow = chatBtnRow;
    chatBtnRowApplyKeyboard();
#else
    s_chatNewMsgBtn = nullptr;
    s_chatNewMsgLabel = nullptr;
    s_chatActBtn = nullptr;
    s_chatBtnRow = nullptr;
#endif

    s_chatShortcutBar = lv_obj_create(screen);
    // The width the bar is actually given, for the key-hint label's budget
    // further down. Recorded here rather than read back with lv_obj_get_width():
    // a size just set is not resolved until LVGL lays the screen out, so the
    // query would have returned 0 and left the hint label zero pixels wide.
    int legendBarW = chatW;
    LV_UNUSED(legendBarW);   // no hint label on the touch-only builds
#if UI_TOUCH_NAV_BAR
    if (bottomNavEnabled()) {
        // Edge to edge along the bottom, matching appendHeltecBottomNav()'s
        // copy exactly. Anything else and the same six buttons are a different
        // width here than they are one screen in, which reads as the bar
        // jumping every time you navigate.
        if (navIsSideColumn()) {
            // Nav Alignment Left/Right: down one edge, the full height, where
            // every screen's copy of it sits (appendHeltecBottomNav()).
            legendBarW = kSideNavW;
            lv_obj_set_size(s_chatShortcutBar, kSideNavW,
                            lv_disp_get_ver_res(NULL) - kSideNavTopY);
            lv_obj_align(s_chatShortcutBar,
                         navSideLeft() ? LV_ALIGN_TOP_LEFT : LV_ALIGN_TOP_RIGHT,
                         0, kSideNavTopY);
        } else {
            legendBarW = lv_disp_get_hor_res(NULL);
            lv_obj_set_size(s_chatShortcutBar, legendBarW, kBottomNavHeight);
            lv_obj_align(s_chatShortcutBar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        }
    } else {
#if defined(DEVICE_TLORA_PAGER_TFT)
        // Edge to edge. The chat column is only part of this display's width,
        // so a bar that stopped where the column does left the bottom of the
        // channel list showing beside it — a strip of a different screen along
        // the bottom edge, under a footer that looked like it had been cut
        // short. Inside the else since issue #92: this board draws the icon bar
        // now too, and that one is placed by the branch above.
        legendBarW = lv_disp_get_hor_res(NULL);
        lv_obj_set_size(s_chatShortcutBar, legendBarW, chatLegendH);
        lv_obj_align(s_chatShortcutBar, LV_ALIGN_TOP_LEFT, 0, chatY + chatH + 3);
#else
#if HAS_CHANNEL_PANEL_LOCK && !UI_TOUCH_ONLY_PROFILE
        if (!channelListIsDropdown()) {
            // Docked column: under it as well as the chat, the Pager's way.
            // Kept to the chat's width, the key hints ran out of room and
            // wrapped onto a second line the bar has no height for.
            legendBarW = lv_disp_get_hor_res(NULL) - 2 * panelMargin;
            lv_obj_set_size(s_chatShortcutBar, legendBarW, chatLegendH);
            lv_obj_align(s_chatShortcutBar, LV_ALIGN_TOP_LEFT, panelMargin, chatY + chatH + 3);
        } else
#endif
        {
        lv_obj_set_size(s_chatShortcutBar, chatW, chatLegendH);
        lv_obj_align(s_chatShortcutBar, LV_ALIGN_TOP_LEFT, chatX, chatY + chatH + 3);
        }
#endif
    }
#elif defined(DEVICE_TLORA_PAGER_TFT)
    // Edge to edge, for the reason spelled out above. Only reachable on a build
    // that has opted out of the bar entirely (UI_NO_NAV_BAR).
    legendBarW = lv_disp_get_hor_res(NULL);
    lv_obj_set_size(s_chatShortcutBar, legendBarW, chatLegendH);
    lv_obj_align(s_chatShortcutBar, LV_ALIGN_TOP_LEFT, 0, chatY + chatH + 3);
#else
    lv_obj_set_size(s_chatShortcutBar, chatW, chatLegendH);
    lv_obj_align(s_chatShortcutBar, LV_ALIGN_TOP_LEFT, chatX, chatY + chatH + 3);
#endif
    lv_obj_clear_flag(s_chatShortcutBar, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(s_chatShortcutBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chatShortcutBar, 1, 0);
    lv_obj_set_style_border_color(s_chatShortcutBar, lv_color_make(0, 0, 0), 0);
#else
    // panelBg/divider, the same tokens every other panel on this screen takes,
    // so the bar belongs to the theme rather than staying navy under it.
    lv_obj_set_style_bg_color(s_chatShortcutBar, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatShortcutBar, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_chatShortcutBar, 1, 0);
    lv_obj_set_style_border_color(s_chatShortcutBar, lv_color_hex(0x335D9D), 0);
#endif
    lv_obj_set_style_pad_left(s_chatShortcutBar, 4, 0);
    lv_obj_set_style_pad_right(s_chatShortcutBar, 4, 0);
    lv_obj_set_style_pad_top(s_chatShortcutBar, 0, 0);
    lv_obj_set_style_pad_bottom(s_chatShortcutBar, 0, 0);

#if UI_TOUCH_ONLY_PROFILE
    s_chatShortcutText = nullptr;
    // Home is the lit button because the dashboard is what opens over this bar
    // at boot. Home and Chats are the one pair of cells that share a bar —
    // both surfaces live on the root screen — so the mark moves between them
    // from openHomeDashboard()/closeHomeDashboard() rather than being fixed
    // here. See homeDashMarkNavBar().
    populateHeltecBottomNav(s_chatShortcutBar, HELTEC_NAV_HOME);
#elif HAS_NAV_BAR_TOGGLE
    if (bottomNavEnabled()) {
    // Touch nav on a board that also has a keyboard: the same icon row the
    // touch-only builds use, sharing this bar with the GPS/WiFi/DM icons that
    // already lived on it. The key-hint text that used to fill the left is what
    // pays for the room — six buttons plus the status icons is the whole line.
    // The keys themselves still work, and each button that has one now prints
    // it beside its icon, so the hints are on the buttons rather than gone.
    s_chatShortcutText = nullptr;
    populateHeltecBottomNav(s_chatShortcutBar, HELTEC_NAV_HOME);

    // The same cluster the modals' copies of this bar carry.
    buildNavStatusCluster(s_chatShortcutBar, &s_chatStatusBox, &s_chatHeaderGps,
                          &s_chatHeaderWifi, &s_chatDmAlert, &s_chatChanAlert);
    } else {
    // Bar switched off: fall through to the key-hint strip below, which is the
    // footer this board had before the bar and is still what every other
    // keyboard build draws.
#endif
#if !UI_TOUCH_ONLY_PROFILE
    s_chatShortcutText = lv_label_create(s_chatShortcutBar);
#if defined(DEVICE_TDECK_PRO)
    lv_coord_t legendTextW = chatW - 8;
#else
    // Measured against the bar, not the chat column: the two are the same width
    // on most boards, but not where the bar spans the whole display.
    lv_coord_t legendTextW = (lv_coord_t)legendBarW - bottomStatusReserveW();
    if (legendTextW < 40) legendTextW = (lv_coord_t)legendBarW;
#endif
    lv_obj_set_width(s_chatShortcutText, legendTextW);
    lv_obj_set_style_text_font(s_chatShortcutText, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_chatShortcutText, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(s_chatShortcutText, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_chatShortcutText, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_chatShortcutText, LV_ALIGN_TOP_MID, 0, 1);
#else
    lv_obj_set_style_text_color(s_chatShortcutText, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_chatShortcutText, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_chatShortcutText, LV_LABEL_LONG_DOT);
    lv_obj_align(s_chatShortcutText, LV_ALIGN_LEFT_MID, 2, 0);
#endif
    lv_label_set_text(s_chatShortcutText, chatShortcutHintText());

    s_chatHeaderGps = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatHeaderGps, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_make(0, 0, 0), 0);
    lv_obj_align(s_chatHeaderGps, LV_ALIGN_BOTTOM_RIGHT, -4, -1);
#else
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align(s_chatHeaderGps, LV_ALIGN_RIGHT_MID, -4, 0);
#endif

    s_chatHeaderWifi = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatHeaderWifi, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);
#endif
    lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);

    s_chatDmAlert = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatDmAlert, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_chatDmAlert, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_chatDmAlert, lv_color_hex(0xF4D35E), 0);
#endif
    lv_label_set_text(s_chatDmAlert, LV_SYMBOL_ENVELOPE);
    lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_add_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);

    // Unread channel traffic, one glyph further left. A bell, not a second
    // envelope: these two blink together and sit a few pixels apart, so shape
    // is the only thing telling them apart.
    s_chatChanAlert = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatChanAlert, &lv_font_montserrat_10, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(s_chatChanAlert, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_color(s_chatChanAlert, lv_color_hex(0xF4D35E), 0);
#endif
    lv_label_set_text(s_chatChanAlert, LV_SYMBOL_BELL);
    lv_obj_align_to(s_chatChanAlert, s_chatDmAlert, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    lv_obj_add_flag(s_chatChanAlert, LV_OBJ_FLAG_HIDDEN);
#endif
#if HAS_NAV_BAR_TOGGLE
    }
#endif

    for (int i = 0; i < MESH_CHANNELS; i++) {
#if UI_CHANNEL_LIST_DROPDOWN
        lv_obj_t *btn = lv_btn_create(s_channelList ? s_channelList : screen);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
#endif
        s_channelBtns[i] = btn;
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, max(channelNavBtnH, 20));
        lv_obj_set_style_radius(btn, 6, 0);
#if defined(DEVICE_MESH_DECK)
        // Half the usual side padding, spent on the panel matching the selector
        // button's width instead — see fitChannelDropdownToButtonContent().
        lv_obj_set_style_pad_left(btn, 2, 0);
        lv_obj_set_style_pad_right(btn, 2, 0);
#else
        lv_obj_set_style_pad_left(btn, 4, 0);
        lv_obj_set_style_pad_right(btn, 4, 0);
#endif
        lv_obj_set_style_pad_top(btn, 2, 0);
        lv_obj_set_style_pad_bottom(btn, 2, 0);
        lv_obj_set_style_outline_width(btn, 0, 0);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        s_channelLabels[i] = lbl;
    lv_obj_set_style_text_font(lbl, channelNavFont, 0);
        lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_height(lbl, lv_font_get_line_height(channelNavFont));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        const char *name = channelName(i);
        if (name[0]) {
            lv_label_set_text(lbl, name);
        } else {
            lv_label_set_text(lbl, TR("Channel"));
        }
        lv_obj_center(lbl);
#else
    #if UI_CARDPUTER_COMPACT
        lv_obj_t *btn = lv_obj_create(s_channelList ? s_channelList : screen);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    #elif UI_TOUCH_ONLY_PROFILE
        lv_obj_t *btn = lv_btn_create(s_channelList ? s_channelList : screen);
    #elif defined(DEVICE_TLORA_PAGER_TFT)
        lv_obj_t *btn = lv_obj_create(panel);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH, 0, 1,
                             LV_GRID_ALIGN_STRETCH, i, 1);
    #else
        lv_obj_t *btn = lv_btn_create(panel);
    #endif
        s_channelBtns[i] = btn;
    #if UI_CARDPUTER_COMPACT
        lv_obj_set_size(btn, 40, kMainScreenChannelBtnHeight);
    #elif UI_TOUCH_ONLY_PROFILE
        lv_obj_set_width(btn, LV_SIZE_CONTENT);
        lv_obj_set_height(btn, kMainScreenChannelBtnHeight);
    #elif defined(DEVICE_TLORA_PAGER_TFT)
        lv_obj_set_size(btn, lv_pct(100), lv_pct(100));
    #else
        lv_obj_set_width(btn, lv_pct(94));
        lv_obj_set_height(btn, kMainScreenChannelBtnHeight);
    #endif
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_set_style_outline_width(btn, 0, 0);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
    #if UI_TOUCH_ONLY_PROFILE
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    #else
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    #endif

        lv_obj_t *lbl = lv_label_create(btn);
        s_channelLabels[i] = lbl;
        lv_obj_set_style_text_font(lbl, kMainScreenFont, 0);
    #if UI_CARDPUTER_COMPACT || UI_TOUCH_ONLY_PROFILE
        lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    #else
        lv_obj_set_width(lbl, lv_pct(100));
    #endif
        lv_obj_set_height(lbl, lv_font_get_line_height(kMainScreenFont));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    #if UI_CARDPUTER_COMPACT || UI_TOUCH_ONLY_PROFILE
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    #else
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    #endif
        const char *name = channelName(i);
        if (name[0]) {
            lv_label_set_text(lbl, name);
        } else {
            lv_label_set_text(lbl, TR("Channel"));
        }
    #if UI_CARDPUTER_COMPACT || UI_TOUCH_ONLY_PROFILE
        sizeChannelButtonToLabel(i);
    #endif
        lv_obj_center(lbl);
#endif
    }

#if defined(DEVICE_TDISPLAY_P4) || HAS_CHANNEL_PANEL_LOCK
    if (!channelListIsDropdown() && s_channelList) {
#if HAS_CHANNEL_PANEL_LOCK
        s_channelColumnFocus = false;
#endif
        // Landscape: the dropdown list, built above with every channel button
        // in it, becomes the anchored column the chat was offset for -- the
        // Pager's layout, from the P4's own parts. Shown always, down the left
        // from just under the rounded corner to the bottom of the chat, above
        // the nav bar. Its dropdown paths all stand down (channelListIsDropdown()),
        // so a tap on a channel only switches to it.
        const int colTop = max(2, UI_CORNER_SAFE_X);
        lv_obj_set_size(s_channelList, p4ChannelColW, (chatY + chatH) - colTop);
        lv_obj_align(s_channelList, LV_ALIGN_TOP_LEFT, panelMargin + navPadL, colTop);
        lv_obj_clear_flag(s_channelList, LV_OBJ_FLAG_HIDDEN);
        // The Pager panel's fill: a column of the screen, not a popup over it.
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_bg_opa(s_channelList, LV_OPA_70, 0);
        // Undo the drawer's shape (square, right edge only): this is the
        // rounded, fully edged panel the column always was.
        lv_obj_set_style_border_side(s_channelList, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_radius(s_channelList, 6, 0);
        for (int i = 0; i < MESH_CHANNELS; i++) {
            // Full column width rather than sized to the name, which is what a
            // dropdown wants and a list you read down does not.
            if (s_channelBtns[i]) lv_obj_set_width(s_channelBtns[i], lv_pct(100));
        }
        // The header's selector opened the dropdown; with the list always in
        // view it has nothing to do, and the active channel is the lit row.
        if (s_channelSelectorBtn) lv_obj_add_flag(s_channelSelectorBtn, LV_OBJ_FLAG_HIDDEN);
    }
#endif

    refreshChatComposeButtonState();

    int initialChannel = constrain(s_activeChannel, 0, MESH_CHANNELS - 1);
    setActiveChannel(initialChannel);
    // After setActiveChannel, so the selector button has already measured itself
    // and the dropdown can match its width.
    fitChannelDropdownToButtonContent();
    refreshHeaderTime(true);
    refreshHeaderStatus(true);
    lv_scr_load(screen);
}

// Everything parented to s_rootScreen has to be closed through its own path
// while the old tree is still alive: once the root is deleted, lvObjValid()
// cannot tell a stale pointer from a new widget at the same address.
static void closeAllModalsForRebuild() {
    // The accent and spell boxes delete asynchronously; the root delete would
    // free them first and the queued delete would then hit freed memory.
    if (lvObjValid(s_accentBox)) lv_obj_del(s_accentBox);
    s_accentBox = nullptr;
    accentBoxHide();
#if HAS_SPELLCHECK
    if (lvObjValid(s_spellBox)) lv_obj_del(s_spellBox);
    s_spellBox = nullptr;
    spellBoxHide();
#endif

#if HAS_STATE_MAPS
    closeLegacyMapPrompt();
#endif
    closeOtaUpdatePrompt();
#if HAS_BLE_KEYBOARD
    closeBleKbdRepairPrompt();
#endif
#if HAS_SD_MALWARE_SCAN
    closeSdScanProgressModal();
#endif
#if HAS_ADMIN_TERMINAL
    closeAdminTerminalModal();
#endif
    closeEmojiPicker();
#if HAS_MESSAGE_ACTIONS
    closeMsgSenderInfoModal();
#endif
    closeTracerouteProgressModal();
#if HAS_NODE_LOCATE
    closeNodeLocateModal();
#endif
#if HAS_NODE_LOS
    closeNodeLosModal();
#endif
    closeChUtilChartModal();
    closeSnrRssiChartModal();
#if FEATURE_DISCOVERY
    closeDiscoveryPresetModal();
    closeDiscoveryModal();
#endif
    closeBeaconsModal();
#if HAS_WEATHER
    closeWeatherModal();
#endif
#if FEATURE_MQTT_MONITOR
    closeMqttConfirmModal();
    closeMqttSendModal();
    closeMqttMonitorModal();
#endif

    closeComposePrompt();
    closeMecpForm();
    closeChannelActionsModal();
    closeDmDeleteConfirm();
    closeDmNodePicker();
    closeDmModal();
    closeLiveFilterModal();
    closeLiveToolsModal();
    closeLiveModal();
    closeNodesFilterDialog();
    closeNodesActionMenu();
    closeNodesModal();
    closeLegendModal();
    closeCfgModal();
    closeCfgWifiDeleteConfirm();
    closeCfgColorPickerModal();
    closeAlertSoundModal();
    closeChatStyleModal();
    closeThemeModal();
    closeChatNameModal();
    closeFontSizeModal();
    closeSigPolicyModal();
    closeChanCfgModal();
#if !defined(DEVICE_TLORA_PAGER_TFT)
    lvObjDeleteSafe(s_nodeInfoModal);
#endif
#if HAS_HOME_DASHBOARD
    closeHomeDashboard();
#endif

    // Registered dots live in the old tree.
    s_chatDots.count = 0;
    s_dmDots.count = 0;
}

static void rebuildUiForThemeChange(bool reopenCfg) {
    int preservedChannel = constrain(s_activeChannel, 0, MESH_CHANNELS - 1);
    int preservedCfgSelection = s_cfgSelection;
#if UI_CFG_SECTIONS
    // A theme or font change rebuilds the UI from inside a section; come back
    // to that section, not the grid.
    const int preservedCfgSection = s_cfgSection;
#endif

    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        lv_indev_reset(indev, nullptr);
    }

    closeAllModalsForRebuild();

    lvObjDeleteSafe(s_rootScreen);

    // Explicitly, rather than leaning on the cache's object-pointer check: the
    // rebuild below allocates new buttons immediately after these are freed, so
    // LVGL can hand back the same addresses. A recycled pointer would read as
    // "same object, already painted" and leave a button unstyled.
    invalidateChannelGlowPaint();

    memset(s_channelBtns, 0, sizeof(s_channelBtns));
    memset(s_channelLabels, 0, sizeof(s_channelLabels));
    s_channelStrip = nullptr;
    s_channelList = nullptr;
    s_channelDrawerOpen = false;
    s_channelDrawerScrim = nullptr;
    s_channelSelectorBtn = nullptr;
    s_channelSelectorLabel = nullptr;
    s_channelSelectorCaretLabel = nullptr;
    s_channelSelectorFixedBtnW = 0;
    s_chatHeaderBar = nullptr;
    s_chatHeaderTime = nullptr;
    s_chatHeaderGps = nullptr;
    s_chatHeaderWifi = nullptr;
    s_chatDmAlert = nullptr;
    s_chatChanAlert = nullptr;
    s_chatHeaderBattText = nullptr;
    s_chatHeaderBattBar = nullptr;
    s_chatPanel = nullptr;
    s_chatList = nullptr;
    s_chatNewMsgBtn = nullptr;
    s_chatNewMsgLabel = nullptr;
    s_chatActBtn = nullptr;
    s_chatBtnRow = nullptr;
    s_chatShortcutBar = nullptr;
    s_chatShortcutText = nullptr;
    s_chatStatusBox = nullptr;

    s_activeChannel = preservedChannel;
    buildUi();

    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        lv_indev_reset(indev, nullptr);
    }

    if (reopenCfg) {
        openCfgModal();
#if UI_CFG_SECTIONS
        if (preservedCfgSection >= 0) {
            s_cfgSection = preservedCfgSection;
            initCfgActions();
        }
#endif
        if (s_cfgActionCount > 0) {
            s_cfgSelection = constrain(preservedCfgSelection, 0, s_cfgActionCount - 1);
        }
        refreshCfgModal();
    }
}

#if defined(DEVICE_CARDPUTER_ZERO) && CZERO_DESKTOP
// The desktop window (linux/app/czero_window.cpp). The wheel scrolls whatever
// scrolls under the mouse. A new window size, once it settles, becomes the
// panel's and LVGL's, and the UI is laid out again for it, back on the screen
// that was open.
static void czeroFollowWindow() {
    if (const int notches = czeroWindowTakeWheel()) {
        int32_t x = 0, y = 0;
        (void)czeroWindowPointer(x, y);
        const lv_point_t pt{x, y};
        lv_obj_t *obj = lv_indev_search_obj(lv_layer_top(), (lv_point_t *)&pt);
        if (!obj) obj = lv_indev_search_obj(lv_screen_active(), (lv_point_t *)&pt);
        for (; obj; obj = lv_obj_get_parent(obj)) {
            if (!lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) continue;
            if ((lv_obj_get_scroll_top)(obj) <= 0 && (lv_obj_get_scroll_bottom)(obj) <= 0) continue;
            // 30 layout pixels a notch; the shim doubles them.
            lv_obj_scroll_by(obj, 0, notches * 30, LV_ANIM_ON);
            s_lastActivityMs = millis();
            break;
        }
    }

    int w = 0, h = 0;
    if (!czeroWindowSize(w, h)) return;
    if (w == (int)displayDev().width() && h == (int)displayDev().height()) return;
    // As processPendingUiRebuild(): these own the screen, so wait them out.
    if (s_onboardingModal) return;

    char reopen = KEY_NONE;
    if (lvObjValid(s_cfgModal))            reopen = KEY_OPEN_CONFIG;
    else if (lvObjValid(s_nodesModal))     reopen = KEY_OPEN_NODES;
    else if (lvObjValid(s_liveToolsModal)) reopen = KEY_OPEN_TOOLS;
    else if (lvObjValid(s_dmModal))        reopen = KEY_OPEN_DMS;
    else if (lvObjValid(s_legendModal))    reopen = KEY_OPEN_HELP;
    else if (lvObjValid(s_homeDash))       reopen = KEY_OPEN_HOME;

    displayDev().czeroResize(w, h);
    lv_display_set_resolution(s_lvDisplay, w, h);
    czeroWindowShow(w, h);
    Serial.printf("[app] window %dx%d\n", w, h);
    rebuildUiForThemeChange(false);
    if (reopen != KEY_NONE) (void)handleGlobalNavigationKey(reopen);
}
#endif

static void scheduleUiRebuild(bool reopenCfg) {
    s_uiRebuildPending = true;
    if (reopenCfg) s_uiRebuildReopenCfg = true;
}

static void processPendingUiRebuild() {
#if defined(DEVICE_TDECK_PRO)
    if (s_screenAsleep) return;
#endif
    if (!s_uiRebuildPending) return;
    // These own the screen and end on their own; rebuild once they have gone
    // rather than tearing them down mid-flow.
    if (s_onboardingModal) return;
#if HAS_SLEEP_OVERLAY
    if (s_tdeckProSleepOverlay) return;
#endif
#if HAS_RUNTIME_ORIENTATION && defined(DEVICE_TDISPLAY_P4)
    if (s_kbLandscapeBackdrop) return;
#endif

    const bool reopenCfg = s_uiRebuildReopenCfg;
    s_uiRebuildPending = false;
    s_uiRebuildReopenCfg = false;

    rebuildUiForThemeChange(reopenCfg);
#if HAS_CHANNEL_PANEL_LOCK
    if (s_channelColumnFocusAfterRebuild) {
        s_channelColumnFocusAfterRebuild = false;
        const int sel = s_channelColumnFocusSel;
        s_channelColumnFocusSel = -1;
        s_cardputerDropdownSelection = (sel >= 0 && sel < MESH_CHANNELS) ? sel : s_activeChannel;
        setChannelColumnFocus(true);
    }
#endif
}

// In-place normalise a serial CLI line: trim whitespace, collapse runs of
// spaces, and lowercase the command so dispatch can use plain strcmp().
static void normalizeSerialCommand(char *line, bool lower) {
    if (!line) return;

    size_t len = strlen(line);
    while (len > 0) {
        char c = line[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            line[--len] = '\0';
        } else {
            break;
        }
    }

    size_t start = 0;
    while (line[start] == ' ' || line[start] == '\t') start++;
    if (start > 0) {
        memmove(line, line + start, strlen(line + start) + 1);
    }

    bool lastWasSpace = false;
    size_t out = 0;
    for (size_t i = 0; line[i]; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\t') c = ' ';
        if (c == ' ') {
            if (lastWasSpace) continue;
            lastWasSpace = true;
            line[out++] = ' ';
            continue;
        }
        lastWasSpace = false;
        line[out++] = lower ? (char)tolower(c) : (char)c;
    }
    line[out] = '\0';

    if (out > 0 && line[out - 1] == ' ') {
        line[out - 1] = '\0';
    }
}

static bool parseCliUnsignedInt(const char *s, int &out, int maxVal) {
    if (!s || !*s) return false;
    if (maxVal < 1) maxVal = 1;
    int v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        v = (v * 10) + (*p - '0');
        if (v > maxVal) {
            v = maxVal;
            break;
        }
    }
    out = v;
    return true;
}

static uint32_t s_cliNodeSeedRound = 0;
static uint32_t s_cliChatSeedRound = 0;

static void seedNodesForRepro(int requestedCount) {
    int target = requestedCount;
    if (target < 1) target = 1;
    if (target > MAX_NODES) target = MAX_NODES;

    int seeded = 0;
    int favorited = 0;
    uint32_t now = millis();
    uint32_t roundOffset = s_cliNodeSeedRound * (uint32_t)MAX_NODES;
    for (int i = 0; i < target; i++) {
        // Deterministic but well-spread IDs so repeated runs hit stable slots.
        uint32_t idIdx = roundOffset + (uint32_t)i;
        uint32_t nodeId = 0x14000000UL + (uint32_t)(idIdx * 2654435761UL);
        NodeEntry *e = Nodes.upsert(nodeId);
        if (!e) continue;

        snprintf(e->longName, sizeof(e->longName), "Seed Node %03d", i + 1);
        snprintf(e->shortName, sizeof(e->shortName), "S%03d", (i + 1) % 1000);
        e->hasName = true;
        e->lastHeardMs = now - (uint32_t)((target - i) * 25U);
        e->snr = -12.0f + (float)(i % 30) * 0.8f;
        e->hops = (uint8_t)(i % 4);
        e->hasHops = true;   // seeded distances are known ones, not absent fields

        PositionInfo p = {};
        p.latI = 404000000 + (i * 131);
        p.lonI = -739000000 + (i * 167);
        p.alt = 25 + (i % 200);
        Nodes.updatePosition(nodeId, p);

        TelemetryInfo t = {};
        t.valid = true;
        t.hasDeviceMetrics = true;
        t.hasEnvironmentMetrics = ((i % 3) == 0);
        t.battPct = 20.0f + (float)(i % 80);
        t.voltage = 3.45f + (float)(i % 45) * 0.01f;
        t.chUtil = (float)(i % 100) * 0.7f;
        t.airUtil = (float)(i % 50) * 0.3f;
        t.temperatureC = 14.0f + (float)(i % 28) * 0.5f;
        t.humidityPct = 30.0f + (float)(i % 65);
        t.pressureHpa = 996.0f + (float)(i % 35) * 0.8f;
        Nodes.updateTelemetry(nodeId, t);

        if ((i % 8) == 0 && Nodes.setFavorite(nodeId, true)) {
            favorited++;
        }

        seeded++;
        if ((i & 0x0F) == 0) yield();
    }

    // hasName/lastHeardMs above are written straight through the upsert()
    // pointer, which only marks itself when it creates a slot — a re-seed round
    // that lands on existing nodes would otherwise leave the cached sort stale.
    Nodes.markRankingDirty();

    if (s_nodesModal) {
        snapshotNodesForModal();
        refreshNodesListRows();
        refreshNodesListSelection();
        refreshNodesDetails();
    }
    s_lastRenderedCount = -1;
    s_lastRenderedChannel = -1;
    s_cliNodeSeedRound++;

    Serial.printf("[cli] nodes seeded=%d favorites=%d count=%d\n",
                  seeded, favorited, Nodes.count());
}

static void seedChatForRepro(int requestedCount) {
    int target = requestedCount;
    if (target < 1) target = 1;
    if (target > MAX_MSG_LINES) target = MAX_MSG_LINES;

    int added = 0;
    uint32_t base = s_cliChatSeedRound * 1000UL;

    for (int chanIdx = 0; chanIdx < MESH_CHANNELS; chanIdx++) {
        for (int i = 0; i < target; i++) {
            uint32_t n = base + (uint32_t)(chanIdx * target + i);
            uint32_t sender = 0x22000000UL + (n * 2654435761UL);
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "[%04X] ", (unsigned)(sender & 0xFFFF));

            char msg[220];
            snprintf(msg, sizeof(msg),
                     "Seed msg %d/%d on ch%d. This is synthetic chat traffic to stress rendering with wrapped text and many rows. "
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789",
                     i + 1, target, chanIdx);

            Channels.addMessage(chanIdx, prefix, msg, TFT_WHITE, 0, false, sender);
            added++;
            if ((added & 0x1F) == 0) yield();
        }
    }

    s_cliChatSeedRound++;
    s_lastRenderedChannel = -1;
    refreshChatView(true);
    int activeChan = constrain(s_activeChannel, 0, MESH_CHANNELS - 1);
    Serial.printf("[cli] chat seeded=%d/ch x %d ch (total=%d) active ch%d lines=%d\n",
                  target, MESH_CHANNELS, added, activeChan,
                  Channels.get(activeChan).count);
}

// `cfg` is the web flasher's Customize protocol (see loadProvisionedConfig()).
// Every reply is one line starting "[cfg] ", so the page can pick it out of
// boot logs; "[cfg] err" lines are the failures. Bump kCfgProtoVersion when a
// command changes meaning, not when a key is added -- `caps` lists the keys.
//
//   cfg caps
//   cfg abort                          drop everything staged
//   cfg get preset
//   cfg set preset <name>
//   cfg set slot <n>                   pin the preset to frequency slot n (1-based,
//                                      as Meshtastic's channel_num); 0 = its own
//   cfg channels clear                 stage a whole new channel plan, all slots unused
//   cfg set channel <i> role=primary|secondary|disabled name=<%-encoded>
//                       psk=default|none|<base64> pos=0|1 up=0|1 down=0|1
//   cfg logo begin <bytes>             then `logo data <hex>` lines, then
//   cfg logo end <crc32 hex>           a PNG no larger than caps' logo=WxH
//   cfg logo clear
//   cfg set longname <%-encoded>       the on-device setup answers; on first
//   cfg set shortname <%-encoded>      boot a commit with a region finishes
//   cfg set region <code>              setup in their place (unset names come
//   cfg set role <CLIENT|CLIENT_MUTE|TRACKER|CLIENT_HIDDEN|CLIENT_BASE>   from the node ID)
//   cfg set ssid <%-encoded>
//   cfg set wifipass <%-encoded>
//   cfg commit
static constexpr int kCfgProtoVersion = 1;

// Arguments keep their case, so names match case-insensitively: the channel
// name ("LongMod") or the label with the spaces dropped ("LongModerate").
static int presetFromCliName(const char *value) {
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        if (!strcasecmp(value, kPresets[i].channelName)) return i;
        char label[24];
        size_t o = 0;
        for (const char *s = kPresets[i].name; *s && o + 1 < sizeof(label); s++) {
            if (*s != ' ') label[o++] = *s;
        }
        label[o] = '\0';
        if (!strcasecmp(value, label)) return i;
    }
    return -1;
}

// A whole channel plan, staged by `cfg channels clear` + `cfg set channel`.
// Slots never set stay unused: unnamed SECONDARY on the default key, which is
// how the firmware ships its empty slots.
struct CfgStagedChannel {
    char    name[12];   // Meshtastic's limit is 11 bytes
    uint8_t key[32];
    uint8_t keyLen;
    uint8_t role;
    bool    shareLocation;
    bool    uplink;
    bool    downlink;
};
static CfgStagedChannel s_cfgStagedChans[MESH_CHANNELS];
static bool s_cfgChansStaged = false;

enum CfgLogoState : uint8_t { CFG_LOGO_NONE, CFG_LOGO_RECEIVING, CFG_LOGO_READY, CFG_LOGO_CLEAR };
static CfgLogoState s_cfgLogoState = CFG_LOGO_NONE;
static uint8_t *s_cfgLogoBuf = nullptr;
static size_t s_cfgLogoSize = 0;
static size_t s_cfgLogoRecv = 0;

// What on-device setup would otherwise ask (see onboardingFinalize()). Empty
// strings and role < 0 are "not sent".
struct CfgStagedSetup {
    char nodeLong[MESH_LONG_NAME_MAX_BYTES + 1];
    char nodeShort[sizeof(RhinoConfig::nodeShort)];
    char region[sizeof(RhinoConfig::region)];
    int  role;
    char wifiSsid[sizeof(RhinoConfig::wifiSsid)];
    char wifiPass[sizeof(RhinoConfig::wifiPass)];
    bool wifiPassSet;   // an open network's password is legitimately empty
};
static CfgStagedSetup s_cfgSetup = { {}, {}, {}, -1, {}, {}, false };

static void cfgSetupReset() {
    s_cfgSetup = CfgStagedSetup{ {}, {}, {}, -1, {}, {}, false };
}

static bool cfgSetupStaged() {
    return s_cfgSetup.nodeLong[0] || s_cfgSetup.nodeShort[0] || s_cfgSetup.region[0]
        || s_cfgSetup.role >= 0 || s_cfgSetup.wifiSsid[0] || s_cfgSetup.wifiPassSet;
}

// `cfg set mqtt ...` and `cfg set oktomqtt`: a community's broker. A staged
// broker is committed with the bridge switched on, which is what sending one
// means. okToMqtt < 0 is "not sent".
struct CfgStagedMqtt {
    bool     set;
    char     host[sizeof(RhinoConfig::mqttServer)];
    uint16_t port;
    char     user[sizeof(RhinoConfig::mqttUser)];
    char     pass[sizeof(RhinoConfig::mqttPass)];
    char     root[sizeof(RhinoConfig::mqttRoot)];
    bool     tls;
    bool     encryption;
    int8_t   okToMqtt;
};
static CfgStagedMqtt s_cfgMqtt = { false, {}, 0, {}, {}, {}, false, false, -1 };

static void cfgMqttReset() {
    s_cfgMqtt = CfgStagedMqtt{};
    s_cfgMqtt.okToMqtt = -1;
}

static bool cfgMqttStaged() { return s_cfgMqtt.set || s_cfgMqtt.okToMqtt >= 0; }

static void cfgLogoReset() {
    free(s_cfgLogoBuf);
    s_cfgLogoBuf = nullptr;
    s_cfgLogoSize = s_cfgLogoRecv = 0;
    s_cfgLogoState = CFG_LOGO_NONE;
}

static void cfgStagedChannelsReset() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        CfgStagedChannel &c = s_cfgStagedChans[i];
        memset(&c, 0, sizeof(c));
        c.key[0] = 0x01;
        c.keyLen = 1;
        c.role = 1;
    }
}

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// %XX-decodes src into dst; false on a bad escape or overflow.
static bool cfgPercentDecode(const char *src, char *dst, size_t dstLen) {
    size_t o = 0;
    for (const char *s = src; *s; s++) {
        char c = *s;
        if (c == '%') {
            const int hi = hexNibble(s[1]);
            const int lo = (hi >= 0) ? hexNibble(s[2]) : -1;
            if (lo < 0) return false;
            c = (char)(hi << 4 | lo);
            s += 2;
        }
        if (o + 1 >= dstLen) return false;
        dst[o++] = c;
    }
    dst[o] = '\0';
    return true;
}

static void cfgSetChannel(const char *args) {
    char *end = nullptr;
    const long idx = strtol(args, &end, 10);
    if (end == args || idx < 0 || idx >= MESH_CHANNELS) {
        s_cliOut->printf("[cfg] err channel: slot must be 0-%d\n", MESH_CHANNELS - 1);
        return;
    }
    if (!s_cfgChansStaged) {
        s_cliOut->println("[cfg] err channel: send `cfg channels clear` first");
        return;
    }

    CfgStagedChannel c = {};
    c.key[0] = 0x01;
    c.keyLen = 1;
    c.role = (idx == 0) ? 0 : 1;
    char tok[128];
    const char *p = end;
    while (*p) {
        while (*p == ' ') p++;
        size_t n = 0;
        while (p[n] && p[n] != ' ') n++;
        if (!n) break;
        if (n >= sizeof(tok)) {
            s_cliOut->printf("[cfg] err channel %ld: argument too long\n", idx);
            return;
        }
        memcpy(tok, p, n);
        tok[n] = '\0';
        p += n;

        char *eq = strchr(tok, '=');
        if (!eq) {
            s_cliOut->printf("[cfg] err channel %ld: expected key=value, got \"%s\"\n", idx, tok);
            return;
        }
        *eq = '\0';
        const char *k = tok;
        const char *v = eq + 1;
        const bool flag = !strcmp(v, "1");
        if (!strcasecmp(k, "role")) {
            if (!strcasecmp(v, "primary")) c.role = 0;
            else if (!strcasecmp(v, "secondary")) c.role = 1;
            else if (!strcasecmp(v, "disabled")) c.role = 2;
            else { s_cliOut->printf("[cfg] err channel %ld: bad role \"%s\"\n", idx, v); return; }
        } else if (!strcasecmp(k, "name")) {
            if (!cfgPercentDecode(v, c.name, sizeof(c.name))) {
                s_cliOut->printf("[cfg] err channel %ld: name must be 11 bytes or fewer\n", idx);
                return;
            }
        } else if (!strcasecmp(k, "psk")) {
            if (!strcasecmp(v, "default")) {
                c.key[0] = 0x01;
                c.keyLen = 1;
            } else if (!strcasecmp(v, "none")) {
                c.key[0] = 0x00;   // Meshtastic's "no encryption"
                c.keyLen = 1;
            } else {
                uint8_t kb[33];
                const int kl = base64Decode(v, kb, sizeof(kb));
                if (kl != 1 && kl != 16 && kl != 32) {
                    s_cliOut->printf("[cfg] err channel %ld: psk must decode to 1, 16 or 32 bytes\n", idx);
                    return;
                }
                memcpy(c.key, kb, kl);
                c.keyLen = (uint8_t)kl;
            }
        } else if (!strcasecmp(k, "pos")) {
            c.shareLocation = flag;
        } else if (!strcasecmp(k, "up")) {
            c.uplink = flag;
        } else if (!strcasecmp(k, "down")) {
            c.downlink = flag;
        } else {
            s_cliOut->printf("[cfg] err channel %ld: unknown key \"%s\"\n", idx, k);
            return;
        }
    }

    if ((idx == 0) != (c.role == 0)) {
        s_cliOut->printf("[cfg] err channel %ld: slot 0 is the primary channel and only slot 0\n", idx);
        return;
    }
    if (c.role == 1 && !c.name[0]) {
        s_cliOut->printf("[cfg] err channel %ld: a secondary channel needs a name\n", idx);
        return;
    }
    s_cfgStagedChans[idx] = c;
    s_cliOut->printf("[cfg] ok channel %ld staged\n", idx);
}

//   cfg set mqtt host=<%-enc> [port=N] [user=<%-enc>] [pass=<%-enc>] root=<%-enc>
//                [tls=0|1] [enc=0|1]
// Every field is sent each time, so nothing of a previous broker is kept: an
// omitted user/pass is empty, port 1883, tls off, encryption on.
static void cfgSetMqtt(const char *args) {
    CfgStagedMqtt m = {};
    m.port = 1883;
    m.encryption = true;
    m.okToMqtt = s_cfgMqtt.okToMqtt;
    char tok[160];
    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        size_t n = 0;
        while (p[n] && p[n] != ' ') n++;
        if (!n) break;
        if (n >= sizeof(tok)) { s_cliOut->println("[cfg] err mqtt: argument too long"); return; }
        memcpy(tok, p, n);
        tok[n] = '\0';
        p += n;
        char *eq = strchr(tok, '=');
        if (!eq) { s_cliOut->printf("[cfg] err mqtt: expected key=value, got \"%s\"\n", tok); return; }
        *eq = '\0';
        const char *k = tok;
        const char *v = eq + 1;
        auto text = [&](char *dst, size_t len) -> bool {
            if (cfgPercentDecode(v, dst, len)) return true;
            s_cliOut->printf("[cfg] err mqtt: %s must be %u bytes or fewer\n", k, (unsigned)(len - 1));
            return false;
        };
        if (!strcasecmp(k, "host")) { if (!text(m.host, sizeof(m.host))) return; }
        else if (!strcasecmp(k, "user")) { if (!text(m.user, sizeof(m.user))) return; }
        else if (!strcasecmp(k, "pass")) { if (!text(m.pass, sizeof(m.pass))) return; }
        else if (!strcasecmp(k, "root")) { if (!text(m.root, sizeof(m.root))) return; }
        else if (!strcasecmp(k, "port")) {
            char *end = nullptr;
            const long port = strtol(v, &end, 10);
            if (end == v || *end || port < 1 || port > 65535) {
                s_cliOut->printf("[cfg] err mqtt: bad port \"%s\"\n", v);
                return;
            }
            m.port = (uint16_t)port;
        }
        else if (!strcasecmp(k, "tls")) m.tls = !strcmp(v, "1");
        else if (!strcasecmp(k, "enc")) m.encryption = !strcmp(v, "1");
        else { s_cliOut->printf("[cfg] err mqtt: unknown key \"%s\"\n", k); return; }
    }
    if (!m.host[0] || !m.root[0]) {
        s_cliOut->println("[cfg] err mqtt: host and root are required");
        return;
    }
    m.set = true;
    s_cfgMqtt = m;
    s_cliOut->printf("[cfg] ok mqtt=%s:%u staged\n", m.host, (unsigned)m.port);
}

// Copies a staged broker into s_cfg and switches the bridge on. Wi-Fi is the
// bridge's master switch (the load path turns MQTT off without it), so it is
// switched on too -- but only with a network to join; without one the bridge
// stays on and waits for Wi-Fi to be set up.
static void cfgApplyStagedMqtt(const char *wifiSsid) {
    if (s_cfgMqtt.set) {
        utf8util::copyTruncate(s_cfg.mqttServer, sizeof(s_cfg.mqttServer), s_cfgMqtt.host);
        utf8util::copyTruncate(s_cfg.mqttUser, sizeof(s_cfg.mqttUser), s_cfgMqtt.user);
        utf8util::copyTruncate(s_cfg.mqttPass, sizeof(s_cfg.mqttPass), s_cfgMqtt.pass);
        utf8util::copyTruncate(s_cfg.mqttRoot, sizeof(s_cfg.mqttRoot), s_cfgMqtt.root);
        s_cfg.mqttPort = s_cfgMqtt.port;
        s_cfg.mqttEncryption = s_cfgMqtt.encryption;
        s_cfg.mqttEnabled = true;
        if (wifiSsid && wifiSsid[0]) s_cfg.wifiEnabled = true;
    }
    if (s_cfgMqtt.okToMqtt >= 0) s_cfg.okToMqtt = s_cfgMqtt.okToMqtt != 0;
}

// `cfg set <field> <value>` for the setup answers. False when `args` names
// none of them, so the caller can go on to its other `set` forms.
static bool cfgSetSetupField(const char *args) {
    const char *sp = strchr(args, ' ');
    const size_t keyLen = sp ? (size_t)(sp - args) : strlen(args);
    const char *value = sp ? sp + 1 : "";
    auto is = [&](const char *key) { return strlen(key) == keyLen && !strncasecmp(args, key, keyLen); };

    // Decoded into a buffer one byte larger than the field, so that a value
    // which does not fit is refused rather than cut short.
    auto decodeInto = [&](const char *what, char *dst, size_t dstLen, bool allowEmpty) -> bool {
        char buf[72];
        if (dstLen + 1 > sizeof(buf)) dstLen = sizeof(buf) - 1;
        if (!cfgPercentDecode(value, buf, dstLen + 1) || strlen(buf) >= dstLen) {
            s_cliOut->printf("[cfg] err %s: at most %u bytes\n", what, (unsigned)(dstLen - 1));
            return false;
        }
        if (!allowEmpty && !buf[0]) {
            s_cliOut->printf("[cfg] err %s: cannot be empty\n", what);
            return false;
        }
        memcpy(dst, buf, strlen(buf) + 1);
        s_cliOut->printf("[cfg] ok %s staged\n", what);
        return true;
    };

    if (is("longname")) {
        decodeInto("longname", s_cfgSetup.nodeLong, sizeof(s_cfgSetup.nodeLong), false);
        return true;
    }
    if (is("shortname")) {
        decodeInto("shortname", s_cfgSetup.nodeShort, sizeof(s_cfgSetup.nodeShort), false);
        return true;
    }
    if (is("ssid")) {
        decodeInto("ssid", s_cfgSetup.wifiSsid, sizeof(s_cfgSetup.wifiSsid), false);
        return true;
    }
    if (is("wifipass")) {
        if (decodeInto("wifipass", s_cfgSetup.wifiPass, sizeof(s_cfgSetup.wifiPass), true)) {
            s_cfgSetup.wifiPassSet = true;
        }
        return true;
    }
    if (is("region")) {
        for (uint8_t i = 0; i < kRegionCount; i++) {
            if (!strcasecmp(value, kRegions[i].code)) {
                // Amateur bands only while licensed (and never below the radio's
                // range): staging one would land the node in TX hold.
                if (!regionSelectable(kRegions[i].code, lic::active())) {
                    s_cliOut->printf("[cfg] err region: requires licensed mode\n");
                    return true;
                }
                utf8util::copyTruncate(s_cfgSetup.region, sizeof(s_cfgSetup.region), kRegions[i].code);
                s_cliOut->printf("[cfg] ok region=%s staged\n", kRegions[i].code);
                return true;
            }
        }
        s_cliOut->printf("[cfg] err region: unknown \"%s\"\n", value);
        return true;
    }
    if (is("role")) {
        // The roles setup offers, plus CLIENT_BASE below.
        for (int i = 0; i < kOnboardRoleCount; i++) {
            if (!strcasecmp(value, kOnboardRoles[i].label)) {
                s_cfgSetup.role = kOnboardRoles[i].value;
                s_cliOut->printf("[cfg] ok role=%s staged\n", kOnboardRoles[i].label);
                return true;
            }
        }
        // CLIENT_BASE on a board whose setup does not suggest it: the
        // customizer is how someone sets up a base, so it is taken here.
        if (!strcasecmp(value, "CLIENT_BASE")) {
            s_cfgSetup.role = 12;
            s_cliOut->printf("[cfg] ok role=CLIENT_BASE staged\n");
            return true;
        }
        s_cliOut->printf("[cfg] err role: unknown \"%s\"\n", value);
        return true;
    }
    return false;
}

// Meshtastic's own default naming: the last four hex digits of the node ID.
static void cfgNamesFromNodeId(char *nodeLong, size_t longLen, char *nodeShort, size_t shortLen) {
    const unsigned tail = (unsigned)(s_myNodeId & 0xFFFF);
    snprintf(nodeShort, shortLen, "%04x", tail);
    snprintf(nodeLong, longLen, "Camillia %04x", tail);
}

static void cfgLogoCommand(const char *args) {
    if (!strncasecmp(args, "data ", 5)) {
        if (s_cfgLogoState != CFG_LOGO_RECEIVING) {
            s_cliOut->println("[cfg] err logo: no transfer in progress");
            return;
        }
        const char *hex = args + 5;
        const size_t n = strlen(hex);
        if (n % 2 || s_cfgLogoRecv + n / 2 > s_cfgLogoSize) {
            cfgLogoReset();
            s_cliOut->println("[cfg] err logo: bad chunk - transfer abandoned");
            return;
        }
        for (size_t i = 0; i < n; i += 2) {
            const int hi = hexNibble(hex[i]);
            const int lo = hexNibble(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                cfgLogoReset();
                s_cliOut->println("[cfg] err logo: bad hex - transfer abandoned");
                return;
            }
            s_cfgLogoBuf[s_cfgLogoRecv++] = (uint8_t)(hi << 4 | lo);
        }
        s_cliOut->printf("[cfg] ok logo %u\n", (unsigned)s_cfgLogoRecv);
        return;
    }

    if (!strncasecmp(args, "begin ", 6)) {
        cfgLogoReset();
        const long size = strtol(args + 6, nullptr, 10);
        if (size <= 0 || (size_t)size > kLogoMaxBytes) {
            s_cliOut->printf("[cfg] err logo: size must be 1-%u bytes\n", (unsigned)kLogoMaxBytes);
            return;
        }
        s_cfgLogoBuf = (uint8_t *)malloc((size_t)size);
        if (!s_cfgLogoBuf) {
            s_cliOut->println("[cfg] err logo: out of memory");
            return;
        }
        s_cfgLogoSize = (size_t)size;
        s_cfgLogoState = CFG_LOGO_RECEIVING;
        s_cliOut->printf("[cfg] ok logo begin %u\n", (unsigned)s_cfgLogoSize);
        return;
    }

    if (!strncasecmp(args, "end ", 4)) {
        if (s_cfgLogoState != CFG_LOGO_RECEIVING || s_cfgLogoRecv != s_cfgLogoSize) {
            cfgLogoReset();
            s_cliOut->println("[cfg] err logo: incomplete transfer");
            return;
        }
        const uint32_t want = (uint32_t)strtoul(args + 4, nullptr, 16);
        const uint32_t got = meshCrc32(s_cfgLogoBuf, s_cfgLogoSize);
        int w = 0, h = 0;
        if (got != want) {
            cfgLogoReset();
            s_cliOut->printf("[cfg] err logo: checksum %08x, expected %08x\n", (unsigned)got, (unsigned)want);
            return;
        }
        if (!pngDimensions(s_cfgLogoBuf, s_cfgLogoSize, w, h)) {
            cfgLogoReset();
            s_cliOut->println("[cfg] err logo: not a PNG");
            return;
        }
        if (w > s_splashLogoW || h > s_splashLogoH) {
            cfgLogoReset();
            s_cliOut->printf("[cfg] err logo: %dx%d is larger than %dx%d\n", w, h, s_splashLogoW, s_splashLogoH);
            return;
        }
        s_cfgLogoState = CFG_LOGO_READY;
        s_cliOut->printf("[cfg] ok logo staged %dx%d\n", w, h);
        return;
    }

    if (!strcasecmp(args, "clear")) {
        cfgLogoReset();
        s_cfgLogoState = CFG_LOGO_CLEAR;
        s_cliOut->println("[cfg] ok logo clear staged");
        return;
    }

    s_cliOut->printf("[cfg] err unknown: logo %s\n", args);
}

// Writes or removes the stored logo. NVS holds a page back for compaction and
// fails writes once into it, so a logo that would leave too little is refused
// here rather than starving the settings that are saved after it.
static bool cfgCommitLogo() {
    Preferences p;
    if (!p.begin(kLogoNs, false)) return false;
    p.remove(kLogoKey);
    if (s_cfgLogoState == CFG_LOGO_CLEAR) {
        p.end();
        return true;
    }
    nvs_stats_t st = {};
    const size_t need = s_cfgLogoSize / 32 + 8 + 160;
    if (nvs_get_stats(nullptr, &st) == ESP_OK && st.free_entries < need) {
        p.end();
        s_cliOut->printf("[cfg] logo needs %u NVS entries, %u free\n",
                      (unsigned)need, (unsigned)st.free_entries);
        return false;
    }
    const bool ok = p.putBytes(kLogoKey, s_cfgLogoBuf, s_cfgLogoSize) == s_cfgLogoSize;
    if (!ok) p.remove(kLogoKey);
    p.end();
    return ok;
}

// Copies the staged plan over CHANNEL_KEYS. An unnamed primary takes the
// preset's name, as a freshly installed device's does.
static void cfgApplyStagedChannels() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const CfgStagedChannel &c = s_cfgStagedChans[i];
        ChannelKey &ck = CHANNEL_KEYS[i];
        memset(ck.name_buf, 0, sizeof(ck.name_buf));
        strncpy(ck.name_buf, c.name, sizeof(ck.name_buf) - 1);
        ck.name = ck.name_buf;
        memset(ck.key, 0, sizeof(ck.key));
        memcpy(ck.key, c.key, c.keyLen);
        ck.keyLen = c.keyLen;
        ck.role = c.role;
        ck.uplinkEnabled = c.uplink;
        ck.downlinkEnabled = c.downlink;
        ck.shareLocation = c.shareLocation;
        ck.muted = false;
        ck.hopLimitPlus1 = 0;
        ck.useAead = false;
    }
    // Licensed: the plan is taken, its keys are not. Serial cfg and community
    // presets both land here.
    if (lic::active() && licStripChannels(0)) {
        s_cliOut->println("[cfg] Licensed mode: channel imported without its key");
    }
    syncPrimaryChannelName();
    recomputeChannelHashes();
    for (int i = 0; i < MESH_CHANNELS; i++) Channels.get(i).name = CHANNEL_KEYS[i].name;
}

static void handleCfgCommand(const char *args) {
    if (!strcasecmp(args, "caps")) {
        s_cliOut->printf("[cfg] caps v=%d firstboot=%d keys=preset,slot,channels,setup,mqtt%s channels=%d",
                      kCfgProtoVersion, s_firstBoot ? 1 : 0,
                      (s_splashLogoW > 0 && s_splashLogoH > 0) ? ",logo" : "", MESH_CHANNELS);
        if (s_splashLogoW > 0 && s_splashLogoH > 0) {
            const uint16_t c = s_splashLogoBg;
            const unsigned r = ((c >> 11) & 0x1F) * 255 / 31;
            const unsigned g = ((c >> 5) & 0x3F) * 255 / 63;
            const unsigned b = (c & 0x1F) * 255 / 31;
            s_cliOut->printf(" logo=%dx%d logobg=%02X%02X%02X logomax=%u",
                          s_splashLogoW, s_splashLogoH, r, g, b, (unsigned)kLogoMaxBytes);
        }
        s_cliOut->print(" presets=");
        bool first = true;
        for (uint8_t i = 0; i < PRESET_COUNT; i++) {
            if (!presetUsableOnThisRadio(i)) continue;
            s_cliOut->printf("%s%s", first ? "" : ",", kPresets[i].channelName);
            first = false;
        }
        s_cliOut->println();
        return;
    }

    if (!strcasecmp(args, "abort")) {
        s_cfgStagedPreset = -1;
        s_cfgStagedSlot = -1;
        s_cfgChansStaged = false;
        cfgLogoReset();
        cfgSetupReset();
        cfgMqttReset();
        s_cliOut->println("[cfg] ok aborted");
        return;
    }

    if (!strcasecmp(args, "get preset")) {
        s_cliOut->printf("[cfg] preset=%s custom=%d\n",
                      kPresets[s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : 0].channelName,
                      s_cfg.loraUsePreset ? 0 : 1);
        return;
    }

    if (!strncasecmp(args, "set preset ", 11)) {
        const int preset = presetFromCliName(args + 11);
        if (preset < 0) {
            s_cliOut->printf("[cfg] err preset: unknown \"%s\"\n", args + 11);
            return;
        }
        // The picker on the device never offers these; neither may a cable.
        if (!presetUsableOnThisRadio((uint8_t)preset)) {
            s_cliOut->printf("[cfg] err preset: %s is not supported by this radio\n",
                          kPresets[preset].channelName);
            return;
        }
        s_cfgStagedPreset = preset;
        s_cliOut->printf("[cfg] ok preset=%s staged\n", kPresets[preset].channelName);
        return;
    }

    if (!strncasecmp(args, "set slot ", 9)) {
        char *end = nullptr;
        const long slot = strtol(args + 9, &end, 10);
        if (end == args + 9 || *end || slot < 0 || slot > 255) {
            s_cliOut->printf("[cfg] err slot: expected 0-255, got \"%s\"\n", args + 9);
            return;
        }
        s_cfgStagedSlot = (int)slot;
        s_cliOut->printf("[cfg] ok slot=%ld staged\n", slot);
        return;
    }

    if (!strcasecmp(args, "channels clear")) {
        cfgStagedChannelsReset();
        s_cfgChansStaged = true;
        s_cliOut->printf("[cfg] ok channels cleared slots=%d\n", MESH_CHANNELS);
        return;
    }

    if (!strncasecmp(args, "set channel ", 12)) {
        cfgSetChannel(args + 12);
        return;
    }

    if (!strncasecmp(args, "set mqtt ", 9)) {
        cfgSetMqtt(args + 9);
        return;
    }

    if (!strncasecmp(args, "set oktomqtt ", 13)) {
        const char *v = args + 13;
        if (strcmp(v, "0") && strcmp(v, "1")) {
            s_cliOut->printf("[cfg] err oktomqtt: expected 0 or 1, got \"%s\"\n", v);
            return;
        }
        s_cfgMqtt.okToMqtt = (int8_t)(v[0] - '0');
        s_cliOut->printf("[cfg] ok oktomqtt=%s staged\n", v);
        return;
    }

    if (!strncasecmp(args, "set ", 4) && cfgSetSetupField(args + 4)) return;

    if (!strncasecmp(args, "logo ", 5)) {
        cfgLogoCommand(args + 5);
        return;
    }

    if (!strcasecmp(args, "commit")) {
        if (s_cfgLogoState == CFG_LOGO_RECEIVING) {
            cfgLogoReset();
            s_cliOut->println("[cfg] err commit: logo transfer was not finished");
            return;
        }
        const bool havePreset = s_cfgStagedPreset >= 0;
        const bool haveChans = s_cfgChansStaged;
        const bool haveLogo = s_cfgLogoState == CFG_LOGO_READY || s_cfgLogoState == CFG_LOGO_CLEAR;
        const bool haveSetup = cfgSetupStaged();
        const bool haveMqtt = cfgMqttStaged();
        if (!havePreset && !haveChans && !haveLogo && !haveSetup && !haveMqtt) {
            s_cliOut->println("[cfg] ok nothing staged");
            return;
        }
        if (haveChans && s_cfgStagedChans[0].role != 0) {
            s_cliOut->println("[cfg] err commit: slot 0 must be the primary channel");
            return;
        }
        // Setup is finished in one go or not at all: the region is what it
        // cannot do without (names come from the node ID, role and Wi-Fi have
        // defaults), and a partial answer would only be lost to setup.
        if (s_firstBoot && haveSetup && !s_cfgSetup.region[0]) {
            s_cliOut->println("[cfg] err commit: setup needs a region");
            return;
        }
        // A slot is only ever sent with the preset it pins.
        if (s_cfgStagedSlot > 0 && !havePreset) {
            s_cliOut->println("[cfg] err commit: a slot needs a preset");
            return;
        }
        const uint8_t preset = havePreset ? (uint8_t)s_cfgStagedPreset : 0;
        const uint8_t slot = s_cfgStagedSlot > 0 ? (uint8_t)s_cfgStagedSlot : 0;
        if (slot) {
            const char *region = s_cfgSetup.region[0] ? s_cfgSetup.region : s_cfg.region;
            const uint32_t slots = regionSlotCount(region, kPresets[preset].bw);
            if (slots && slot > slots) {
                s_cliOut->printf("[cfg] err commit: %s has %u slots for %s, not %u\n",
                              region, (unsigned)slots, kPresets[preset].channelName, slot);
                return;
            }
        }
        const bool radioSame = slot == 0
            ? s_cfg.loraUsePreset
            : (!s_cfg.loraUsePreset && s_cfg.loraCustomSlot == slot
               && s_cfg.loraCustomBwKhz == loraBwToCode(kPresets[preset].bw)
               && s_cfg.loraCustomSf == kPresets[preset].sf && s_cfg.loraCustomCr == kPresets[preset].cr);
        const bool presetChanges = havePreset
            && !(radioSame && s_cfg.modemPreset == preset && !s_firstBoot);

        // Logo first: it is the one most likely not to fit, and failing before
        // anything else is written leaves the device as it was.
        if (haveLogo && !cfgCommitLogo()) {
            s_cliOut->println("[cfg] err commit: no room for the logo (NVS full)");
            return;
        }

        // Before the channels, so an unnamed primary takes the new preset's name.
        if (presetChanges) {
            if (s_firstBoot) {
                Preferences p;
                const bool ok = p.begin(kProvNs, false) && p.putUChar("preset", preset)
                             && p.putUChar("slot", slot);
                p.end();
                if (!ok) {
                    s_cliOut->println("[cfg] err commit: could not write NVS");
                    return;
                }
            }
            applyProvisionedPreset(preset, slot);
        }

        // Into s_cfg now, so setup (on the device or answered below) saves it
        // with everything else. Kept in the provisioning namespace as well, as
        // the preset is, for a setup that only finishes after a reboot.
        if (haveMqtt) {
            cfgApplyStagedMqtt(s_cfgSetup.wifiSsid[0] ? s_cfgSetup.wifiSsid : s_cfg.wifiSsid);
            if (s_firstBoot) {
                Preferences p;
                bool ok = p.begin(kProvNs, false);
                if (ok && s_cfgMqtt.set) {
                    // Not chained: an empty user or password writes 0 bytes,
                    // which is not a failure. Host and root never are empty.
                    p.putString("mqHost", s_cfgMqtt.host);
                    p.putUShort("mqPort", s_cfgMqtt.port);
                    p.putString("mqUser", s_cfgMqtt.user);
                    p.putString("mqPass", s_cfgMqtt.pass);
                    p.putString("mqRoot", s_cfgMqtt.root);
                    p.putBool("mqTls", s_cfgMqtt.tls);
                    p.putBool("mqEnc", s_cfgMqtt.encryption);
                    ok = p.isKey("mqHost") && p.isKey("mqRoot");
                }
                if (ok && s_cfgMqtt.okToMqtt >= 0) ok = p.putChar("ok2mq", s_cfgMqtt.okToMqtt) == 1;
                p.end();
                if (!ok) {
                    s_cliOut->println("[cfg] err commit: could not write NVS");
                    return;
                }
            }
        }

        // The channel plan lives in its own namespace, which first-boot
        // detection never looks at, so it is saved directly even mid-setup.
        if (haveChans) {
            cfgApplyStagedChannels();
            if (!persistChannelsToPrefs()) {
                s_cliOut->println("[cfg] err commit: could not save channels (NVS full?)");
                return;
            }
        }

        char chans[8] = "-";
        if (haveChans) {
            int n = 0;
            for (int i = 0; i < MESH_CHANNELS; i++) {
                if (s_cfgStagedChans[i].role != 2 && (i == 0 || s_cfgStagedChans[i].name[0])) n++;
            }
            snprintf(chans, sizeof(chans), "%d", n);
        }
        char summary[128];
        char presetSummary[24] = "-";
        if (havePreset) {
            snprintf(presetSummary, sizeof(presetSummary), slot ? "%s@%u" : "%s",
                     kPresets[preset].channelName, slot);
        }
        snprintf(summary, sizeof(summary), "preset=%s channels=%s logo=%s setup=%s mqtt=%s",
                 presetSummary, chans,
                 !haveLogo ? "-" : (s_cfgLogoState == CFG_LOGO_CLEAR ? "cleared" : "set"),
                 !haveSetup ? "-" : (s_cfgSetup.region[0] ? s_cfgSetup.region : "set"),
                 s_cfgMqtt.set ? s_cfgMqtt.host : (haveMqtt ? "set" : "-"));
        const CfgStagedSetup setup = s_cfgSetup;
        s_cfgStagedPreset = -1;
        s_cfgStagedSlot = -1;
        s_cfgChansStaged = false;
        cfgLogoReset();
        cfgSetupReset();
        cfgMqttReset();

        // Answers setup in the user's place: onboardingFinalize() saves them
        // with everything committed above, ends first boot and reboots.
        if (s_firstBoot && haveSetup) {
            cfgNamesFromNodeId(s_onboardingLongScratch, sizeof(s_onboardingLongScratch),
                               s_onboardingShortScratch, sizeof(s_onboardingShortScratch));
            if (setup.nodeLong[0]) {
                utf8util::copyTruncate(s_onboardingLongScratch, sizeof(s_onboardingLongScratch), setup.nodeLong);
            }
            if (setup.nodeShort[0]) {
                utf8util::copyTruncate(s_onboardingShortScratch, sizeof(s_onboardingShortScratch), setup.nodeShort);
            }
            utf8util::copyTruncate(s_onboardingRegionScratch, sizeof(s_onboardingRegionScratch), setup.region);
            s_onboardingRoleScratch = setup.role >= 0 ? (uint8_t)setup.role : 1;   // setup's CLIENT_MUTE
            utf8util::copyTruncate(s_onboardingWifiSsidScratch, sizeof(s_onboardingWifiSsidScratch), setup.wifiSsid);
            utf8util::copyTruncate(s_onboardingWifiPassScratch, sizeof(s_onboardingWifiPassScratch), setup.wifiPass);
            s_cliOut->printf("[cfg] ok committed %s setup-done rebooting\n", summary);
            s_cliOut->flush();
            delay(300);
            onboardingFinalize();   // does not return
            return;
        }

        if (s_firstBoot) {
            s_cliOut->printf("[cfg] ok committed %s applies-after=setup\n", summary);
            return;
        }

        // An existing install: only what was sent changes.
        if (haveSetup) {
            if (setup.nodeLong[0]) utf8util::copyTruncate(s_cfg.nodeLong, MESH_LONG_NAME_MAX_BYTES + 1, setup.nodeLong);
            if (setup.nodeShort[0]) utf8util::copyTruncate(s_cfg.nodeShort, sizeof(s_cfg.nodeShort), setup.nodeShort);
            if (setup.region[0]) utf8util::copyTruncate(s_cfg.region, sizeof(s_cfg.region), setup.region);
            if (setup.role >= 0) s_cfg.deviceRole = cfgCoerceDeviceRole((uint8_t)setup.role);
            if (setup.wifiSsid[0]) utf8util::copyTruncate(s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid), setup.wifiSsid);
            if (setup.wifiPassSet) utf8util::copyTruncate(s_cfg.wifiPass, sizeof(s_cfg.wifiPass), setup.wifiPass);
            cfgCoerceLicensed(s_cfg);   // a ham region must not persist unlicensed
            applyPresetParams(s_cfg);   // the region moves the frequency
        }
        if (!presetChanges && !haveChans && !haveLogo && !haveSetup && !haveMqtt) {
            s_cliOut->printf("[cfg] ok committed %s unchanged\n", summary);
            return;
        }
        if (presetChanges || haveSetup || haveMqtt) persistConfigToPrefs();
        s_cliOut->printf("[cfg] ok committed %s rebooting\n", summary);
        s_cliOut->flush();
        delay(300);
        flushPersistentState();   // settings and transcripts must land before we go
        ESP.restart();
        return;
    }

    s_cliOut->printf("[cfg] err unknown: %s\n", args);
}

// ── Community presets ───────────────────────────────────────────────────────
// The web flasher's community setups (community_presets.h, generated from the
// flasher's own list), applied on the device: Config -> Community Preset, and
// the web config's LoRa Radio section. Each is turned into the very `cfg`
// commands the flasher would send over USB and run through handleCfgCommand(),
// so the staging, the validation and the commit (save, then reboot) are the
// CLI's, not a second copy of them that could drift.

// Holds the last line written to it, which is all a `cfg` command's answer is.
class CfgCaptureStream : public Stream {
public:
    char line[160] = {};
    size_t write(uint8_t c) override {
        if (c == '\n') { m_pending = true; return 1; }
        if (m_pending) { m_len = 0; m_pending = false; }
        if (m_len + 1 < sizeof(line)) { line[m_len++] = (char)c; line[m_len] = '\0'; }
        return 1;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
private:
    size_t m_len = 0;
    bool m_pending = false;
};

static int communityPresetRadioIndex(const CommunityPreset &cp) {
    return cp.preset[0] ? presetFromCliName(cp.preset) : -1;
}

const char *communityPresetProblem(int idx) {
    if (idx < 0 || idx >= kCommunityPresetCount) return "unknown preset";
    const CommunityPreset &cp = kCommunityPresets[idx];
    if (cp.channelCount > MESH_CHANNELS) {
        static char why[48];
        snprintf(why, sizeof(why), TR("needs %u channels, this device has %d"),
                 (unsigned)cp.channelCount, MESH_CHANNELS);
        return why;
    }
    const int radio = communityPresetRadioIndex(cp);
    if (cp.preset[0] && (radio < 0 || !presetUsableOnThisRadio((uint8_t)radio))) {
        return TR("its radio preset is not supported by this radio");
    }
    // A slot is only ever published with the preset it pins (the commit
    // refuses one without), so `radio` is a real index here.
    if (cp.slot && radio >= 0) {
        const uint32_t slots = regionSlotCount(cp.region, kPresets[radio].bw);
        if (slots && cp.slot > slots) return TR("its frequency slot is outside the region");
    }
    return nullptr;
}

// What applying preset `idx` changes, for the confirmation.
static void communityPresetSummary(int idx, char *out, size_t outLen) {
    const CommunityPreset &cp = kCommunityPresets[idx];
    const int radio = communityPresetRadioIndex(cp);
    char radioText[48];
    if (radio < 0) {
        snprintf(radioText, sizeof(radioText), "%s", TR("unchanged"));
    } else if (cp.slot) {
        snprintf(radioText, sizeof(radioText), TR("%s, slot %u"), kPresets[radio].name,
                 (unsigned)cp.slot);
    } else {
        snprintf(radioText, sizeof(radioText), "%s", kPresets[radio].name);
    }
    char mqttText[96];
    if (cp.mqtt.host[0]) {
        snprintf(mqttText, sizeof(mqttText), TR("%s (%s), turned on"), cp.mqtt.host, cp.mqtt.root);
    } else {
        snprintf(mqttText, sizeof(mqttText), "%s", TR("unchanged"));
    }
    int n = snprintf(out, outLen,
                     TR("Apply %s?\n\nRegion: %s\nRadio: %s\nChannels: replaced with %u\n"
                        "MQTT: %s\n\n"
                        "Your channels are overwritten, messages and non-favorite "
                        "nodes are cleared, and the device reboots."),
                     cp.label, cp.region, radioText, (unsigned)cp.channelCount, mqttText);
    if (cp.notes[0] && n > 0 && (size_t)n < outLen) {
        snprintf(out + n, outLen - n, "\n\n%s", cp.notes);
    }
}

// Stages preset `idx` and commits it. On success the commit reboots and this
// does not return; otherwise it returns why, from the CLI's own answer.
static const char *communityPresetApply(int idx) {
    static char err[160];
    if (const char *why = communityPresetProblem(idx)) {
        snprintf(err, sizeof(err), "%s", why);
        return err;
    }
    // Setup is still to run, and commits made under it wait for it: that is
    // the flasher's path. Config and web config are not reachable from there.
    if (s_firstBoot) {
        snprintf(err, sizeof(err), "%s", TR("finish setup first"));
        return err;
    }
    const CommunityPreset &cp = kCommunityPresets[idx];
    Serial.printf("[cfg] applying community preset %s\n", cp.id);

    CfgCaptureStream cap;
    Stream *const prevOut = s_cliOut;
    s_cliOut = &cap;
    auto run = [&](const char *cmd) -> bool {
        cap.line[0] = '\0';
        handleCfgCommand(cmd);
        Serial.printf("[cfg] %s -> %s\n", cmd, cap.line);
        return strncmp(cap.line, "[cfg] err", 9) != 0;
    };

    char cmd[192];
    bool ok = run("abort");
    if (ok && cp.preset[0]) {
        snprintf(cmd, sizeof(cmd), "set preset %s", cp.preset);
        ok = run(cmd);
        if (ok && cp.slot) {
            snprintf(cmd, sizeof(cmd), "set slot %u", (unsigned)cp.slot);
            ok = run(cmd);
        }
    }
    if (ok) {
        snprintf(cmd, sizeof(cmd), "set region %s", cp.region);
        ok = run(cmd);
    }
    if (ok) ok = run("channels clear");
    for (int i = 0; ok && i < cp.channelCount; i++) {
        const CommunityPresetChannel &ch = kCommunityPresetChannels[cp.firstChannel + i];
        // The name is %-encoded as the flasher sends it, so spaces and
        // punctuation survive the CLI's space-separated arguments.
        char name[40];
        size_t o = 0;
        for (const char *s = ch.name; *s && o + 4 < sizeof(name); s++) {
            const unsigned char c = (unsigned char)*s;
            if (isalnum(c) || c == '_' || c == '.' || c == '~' || c == '-') name[o++] = (char)c;
            else o += snprintf(name + o, sizeof(name) - o, "%%%02X", c);
        }
        name[o] = '\0';
        snprintf(cmd, sizeof(cmd), "set channel %d role=%s name=%s psk=%s pos=%d up=%d down=%d",
                 i, i == 0 ? "primary" : "secondary", name, ch.psk,
                 ch.pos ? 1 : 0, ch.up ? 1 : 0, ch.down ? 1 : 0);
        ok = run(cmd);
    }
    // The community's broker, %-encoded the same way; staging one is what
    // switches MQTT on at the commit.
    auto enc = [](const char *src, char *dst, size_t len) {
        size_t o = 0;
        for (const char *s = src; *s && o + 4 < len; s++) {
            const unsigned char c = (unsigned char)*s;
            if (isalnum(c) || c == '_' || c == '.' || c == '~' || c == '-') dst[o++] = (char)c;
            else o += snprintf(dst + o, len - o, "%%%02X", c);
        }
        dst[o] = '\0';
    };
    if (ok && cp.mqtt.host[0]) {
        char host[3 * sizeof(s_cfg.mqttServer)], user[3 * sizeof(s_cfg.mqttUser)];
        char pass[3 * sizeof(s_cfg.mqttPass)], root[3 * sizeof(s_cfg.mqttRoot)];
        enc(cp.mqtt.host, host, sizeof(host));
        enc(cp.mqtt.user, user, sizeof(user));
        enc(cp.mqtt.pass, pass, sizeof(pass));
        enc(cp.mqtt.root, root, sizeof(root));
        char mq[sizeof(host) + sizeof(user) + sizeof(pass) + sizeof(root) + 64];
        snprintf(mq, sizeof(mq), "set mqtt host=%s port=%u user=%s pass=%s root=%s tls=%d enc=%d",
                 host, (unsigned)cp.mqtt.port, user, pass, root,
                 cp.mqtt.tls ? 1 : 0, cp.mqtt.encryption ? 1 : 0);
        ok = run(mq);
        if (ok && cp.mqtt.okToMqtt >= 0) {
            ok = run(cp.mqtt.okToMqtt ? "set oktomqtt 1" : "set oktomqtt 0");
        }
    }
    if (ok) {
        // A different mesh: what was heard on the old one is noise on the new
        // one. Everything staged was accepted, so the commit below is what
        // remains, and it reboots -- the same clears as Config's Clear
        // Messages and Clear Nodes, which keeps favorites.
        Channels.clearAllMessages(true);
        DMs.clearAll(true);
        clearSelectedMsgContext();
        const int removed = Nodes.clearNonFavorites();
        Serial.printf("[cfg] community preset: messages cleared, %d nodes cleared, %d favorites kept\n",
                      removed, Nodes.count());
        // Reboots on success. Whatever it prints first goes to `cap`, which is
        // fine: nobody is waiting on a USB answer to this.
        ok = run("commit");
    }
    if (!ok) {
        snprintf(err, sizeof(err), "%s", cap.line[0] ? cap.line + 6 : "failed");
    }
    run("abort");   // nothing half-staged may ride along on a later commit
    s_cliOut = prevOut;
    return ok ? nullptr : err;
}

// Dispatch a single normalised CLI command. Aliases are intentional so the
// common shorthand a developer might type (`scan`, `i2c`) maps to the same
// action as the canonical command.
static void handleSerialCommandLine(char *line) {
    if (!line) return;
    // `cfg` arguments keep their case: channel names, PSKs.
    normalizeSerialCommand(line, false);
    if (strncasecmp(line, "cfg ", 4) == 0) {
        handleCfgCommand(line + 4);
        return;
    }
    normalizeSerialCommand(line);
    if (!line[0]) return;

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        s_cliOut->println("[cli] commands: help | nvs | batt | chatdump | keys | gps | env | env scan | env scan all | telemetry now | announce now | nodes stats | nodes seed [count] | chat seed [count] | cfg caps | cfg abort | cfg get preset | cfg set preset <name> | cfg set slot <n> | cfg channels clear | cfg set channel <i> k=v.. | cfg logo begin|data|end|clear | cfg set longname|shortname|region|role|ssid|wifipass <v> | cfg commit");
        return;
    }

    // On-demand because the boot-time copy is emitted before USB CDC finishes
    // enumerating — on these boards the first second of serial output never
    // reaches the host, and this is exactly the report you need after a reboot.
    if (strcmp(line, "nvs") == 0) {
        logNvsHealth();
        return;
    }

    // Ground truth for what the bubble renderer is actually parsing: the stored
    // line as bytes, plus what chatParsePrefix() makes of it. Beliefs about the
    // prefix format are what made the last two attempts at bubble timestamps
    // land on nothing.
    if (strcmp(line, "chatdump") == 0) {
        const DisplayLine *rows[MAX_MSG_LINES] = {};
        int rowCount = 0;
        collectChatRows(rows, rowCount);
        s_cliOut->printf("[chat] channel %d, %d row(s)\n", s_activeChannel, rowCount);
        const int show = (rowCount < 4) ? rowCount : 4;
        for (int r = 0; r < show; r++) {
            if (!rows[r]) continue;
            char icon[12], clock[LIVE_CLOCK_BUF];
            chatParsePrefix(rows[r]->text, icon, sizeof(icon), clock, sizeof(clock));
            s_cliOut->printf("[chat] row%d epoch=%lu text=\"%s\"\n",
                          r, (unsigned long)rows[r]->epoch, rows[r]->text);
            s_cliOut->print("[chat]      bytes:");
            for (int b = 0; b < 14 && rows[r]->text[b]; b++) {
                s_cliOut->printf(" %02X", (unsigned char)rows[r]->text[b]);
            }
            s_cliOut->printf("\n[chat]      parsed icon=\"%s\" clock=\"%s\"\n", icon, clock);
        }
        return;
    }

    if (strcmp(line, "batt") == 0 || strcmp(line, "battery") == 0) {
        char snap[192];
        batteryDebugSnapshot(snap, sizeof(snap));
        s_cliOut->println(snap);
        return;
    }

    if (strcmp(line, "nodes") == 0 || strcmp(line, "nodes stats") == 0) {
        int favorites = 0;
        for (int i = 0; i < Nodes.count(); i++) {
            NodeEntry *e = Nodes.at(i);
            if (e && e->nodeId && e->favorite) favorites++;
        }
        s_cliOut->printf("[cli] nodes count=%d favorites=%d max=%d\n",
                      Nodes.count(), favorites, MAX_NODES);
        return;
    }

    if (strcmp(line, "nodes seed") == 0 || strncmp(line, "nodes seed ", 11) == 0) {
        int target = 220;
        if (line[10] == ' ') {
            int parsed = 0;
            if (!parseCliUnsignedInt(line + 11, parsed, MAX_NODES) || parsed < 1) {
                s_cliOut->printf("[cli] usage: nodes seed [1-%d]\n", MAX_NODES);
                return;
            }
            target = parsed;
        }
        s_cliOut->printf("[cli] seeding %d synthetic nodes...\n", target);
        seedNodesForRepro(target);
        return;
    }

    if (strcmp(line, "chat seed") == 0 || strncmp(line, "chat seed ", 10) == 0) {
        int target = 120;
        if (line[9] == ' ') {
            int parsed = 0;
            if (!parseCliUnsignedInt(line + 10, parsed, MAX_MSG_LINES) || parsed < 1) {
                s_cliOut->printf("[cli] usage: chat seed [1-%d]\n", MAX_MSG_LINES);
                return;
            }
            target = parsed;
        }
        s_cliOut->printf("[cli] seeding %d synthetic chat messages...\n", target);
        seedChatForRepro(target);
        return;
    }

    if (strcmp(line, "keys") == 0 || strcmp(line, "keys trace") == 0) {
        const bool on = !keyboardKeyTrace();
        keyboardSetKeyTrace(on);
        s_cliOut->printf("[cli] key trace %s\n", on ? "ON - press keys to see raw/mapped codes"
                                                 : "off");
        return;
    }

#if defined(DEVICE_TDECK)
    // For the keys the trace above cannot see. The keyboard's own controller
    // swallows some of them -- Alt, and the mic key -- so they never arrive as a
    // character and the only way to identify one is to read the matrix while it
    // is held.
    if (strcmp(line, "keys matrix") == 0) {
        tdeckKeyboardProbeRawMatrix(5000);
        return;
    }
#endif

#if HAS_GPS
    if (strcmp(line, "gps") == 0 || strcmp(line, "gps status") == 0) {
        gpsDebugReport();
        return;
    }
#endif

    if (strcmp(line, "env") == 0 || strcmp(line, "env status") == 0) {
        bool hasSensor = envHasSensor() || envBegin();
        s_cliOut->printf("[cli] env status: %s\n", hasSensor ? envSensorName() : "none");
        if (hasSensor) {
            EnvReading env = {};
            if (envRead(env)) {
                s_cliOut->printf("[cli] env sample: T=%.2fC H=%.2f%% P=%.2fhPa\n",
                              (double)env.temperatureC,
                              (double)env.humidityPct,
                              (double)env.pressureHpa);
            }
        }
        return;
    }

    if (strcmp(line, "env scan") == 0
        || strcmp(line, "scan") == 0
        || strcmp(line, "i2c") == 0
        || strcmp(line, "i2c scan") == 0) {
        s_cliOut->println("[cli] env scan requested");
        bool ok = envDebugScan(true);
        s_cliOut->printf("[cli] env scan result: %s\n", ok ? envSensorName() : "none");
        return;
    }

    if (strcmp(line, "env scan all") == 0
        || strcmp(line, "scan all") == 0
        || strcmp(line, "i2c scan all") == 0) {
        s_cliOut->println("[cli] full i2c scan requested");
        bool ok = envDebugFullScan(true);
        s_cliOut->printf("[cli] full i2c scan result: %s\n", ok ? envSensorName() : "none");
        return;
    }

    if (strcmp(line, "telemetry") == 0 || strcmp(line, "telemetry now") == 0) {
        webCfgQueueTelemetry();
        s_cliOut->println("[cli] telemetry queued");
        return;
    }

    if (strcmp(line, "announce") == 0 || strcmp(line, "announce now") == 0) {
        webCfgQueueAnnounce();
        s_cliOut->println("[cli] announce queued");
        return;
    }

    s_cliOut->printf("[cli] unknown command: %s\n", line);
    s_cliOut->println("[cli] try: help");
}

// Drain pending bytes from a console and dispatch any complete
// newline-terminated command lines; replies go back to that console.
static void serviceSerialInput(Stream &port, SerialCmdInput &in) {
    while (port.available() > 0) {
        int raw = port.read();
        if (raw < 0) break;

        char c = (char)raw;
        if (c == '\r') continue;

        if (c == '\n') {
            s_cliOut = &port;
            // A cut-off line is dropped, not run: half a `cfg` argument is worse
            // than none.
            if (in.overflow) {
                port.println("[cli] line too long - ignored");
            } else if (in.len > 0) {
                in.buf[in.len] = '\0';
                handleSerialCommandLine(in.buf);
            }
            s_cliOut = &Serial;
            in.len = 0;
            in.buf[0] = '\0';
            in.overflow = false;
            continue;
        }

        if (in.len + 1 < sizeof(in.buf)) {
            in.buf[in.len++] = c;
        } else {
            in.overflow = true;
        }
    }
}

// Called once per loop().
static void serviceSerialCommands() {
    serviceSerialInput(Serial, s_serialCmdUart);
#if CLI_ON_NATIVE_USB
    serviceSerialInput(USBSerial, s_serialCmdUsb);
#endif
#if SOC_USB_SERIAL_JTAG_SUPPORTED && ARDUINO_USB_MODE && (ARDUINO_USB_CDC_ON_BOOT || CLI_ON_NATIVE_USB)
    // Core 2.0.17's HWCDC can strand output: a write made while it thinks the
    // host is away arms the TX-empty interrupt before queueing, and if that
    // fires in between it finds nothing, disarms itself, and the bytes wait
    // for the next write -- seconds or more. A `cfg` reply stuck like that
    // times out in the web flasher. While anything is queued, do what the
    // driver does to reconnect: arm the interrupt and commit the FIFO, whose
    // (possibly empty) packet the host's pickup turns into that interrupt.
#if ARDUINO_USB_CDC_ON_BOOT
    HWCDC &usbCdc = Serial;
#else
    HWCDC &usbCdc = USBSerial;
#endif
    static int s_usbCdcTxIdleFree = 0;   // ring free space with nothing queued
    const int txFree = usbCdc.availableForWrite();
    if (txFree > s_usbCdcTxIdleFree) s_usbCdcTxIdleFree = txFree;
    if (txFree < s_usbCdcTxIdleFree) {
        usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        usb_serial_jtag_ll_txfifo_flush();
    }
#endif
}

#if UI_TANMATSU_LAYOUT
// ── Scrollbar gutters ────────────────────────────────────────────────────────
// LVGL draws a scrollbar inside its container's right edge, on top of the
// children, so anything laid out to that edge is painted over. Rather than
// re-pad the dozens of lists one by one, every container that actually scrolls
// vertically is given enough right padding to clear its own bar. Run after
// each redraw, when layout is current; the padding only ever grows, so a
// container is touched once and the walk is otherwise read-only.
static void reserveScrollbarGutters(lv_obj_t *obj) {
    if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)
        && (lv_obj_get_scroll_dir(obj) & LV_DIR_VER)) {
        const lv_scrollbar_mode_t mode = lv_obj_get_scrollbar_mode(obj);
        if (mode != LV_SCROLLBAR_MODE_OFF) {
            const int32_t need = lv_obj_get_style_width(obj, LV_PART_SCROLLBAR)
                               + lv_obj_get_style_pad_right(obj, LV_PART_SCROLLBAR) + 2;
            if (lv_obj_get_style_pad_right(obj, LV_PART_MAIN) < need
                && (mode == LV_SCROLLBAR_MODE_ON
                    || lv_obj_get_scroll_top(obj) > 0
                    || lv_obj_get_scroll_bottom(obj) > 0)) {
                lv_obj_set_style_pad_right(obj, need, LV_PART_MAIN);
            }
        }
    }
    const uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) reserveScrollbarGutters(lv_obj_get_child(obj, (int32_t)i));
}

static void onRefrReadyReserveGutters(lv_event_t *e) {
    lv_display_t *disp = (lv_display_t *)lv_event_get_target(e);
    reserveScrollbarGutters(lv_display_get_screen_active(disp));
    reserveScrollbarGutters(lv_display_get_layer_top(disp));
}
#endif

// ── MeshBeacon broadcast (port 37) ───────────────────────────────────────────
// Advertises this node's mesh (channel 0's name, optionally its PSK, our preset
// and region) onto a target preset's public default channel, every
// meshBeaconIntervalH hours. When the target is not what the radio is on, the
// send is a tempTuneEnter() / transmit / tempTuneLeave() inside one loop()
// call: Radio.transmit() blocks, so nothing else can go out while parked.
//
// Nothing here writes s_cfg, calls markConfigDirty() or saves the channel plan.
// The only persistent state is the last-sent time, in its own NVS key, so an
// hourly beacon does not rewrite the whole config blob.
static constexpr const char *kMbLastSentKey      = "mbLastSent";   // NVS "camillia", uint32 epoch s
static constexpr uint32_t    kMbSettleMs         = 120000;  // after boot / a beacon settings save
static constexpr uint32_t    kMbServiceGapMs     = 1000;    // serviceMeshBeacon() poll rate
static constexpr float       kMbAirUtilTxMaxPct  = 5.0f;    // half the EU 10 % duty cycle

static uint32_t s_mbLastSentMs     = 0;   // millis() of the last send; 0 = never
static uint32_t s_mbLastEpoch      = 0;   // epoch s of the last send; 0 = unknown
static uint32_t s_mbNotBeforeMs    = 0;   // settle hold; 0 = none
static uint32_t s_mbAirRetryMs     = 0;   // current air-util backoff step
static uint32_t s_mbAirHoldUntilMs = 0;   // air-util hold; 0 = none
static uint32_t s_mbLastServiceMs  = 0;
static uint32_t s_mbSettingsSig    = 0;
static bool     s_mbPayloadValid   = false;
static size_t   s_mbPayloadLen     = 0;
static uint8_t  s_mbPayload[160];
static MeshBeaconTx s_mbTx;

// beaconDueInMs() reads 0 as "never sent" / "no hold", so a real timestamp
// that happens to land on 0 is stored as 1.
static inline uint32_t mbNonZeroMs(uint32_t ms) { return ms ? ms : 1; }

static uint32_t meshBeaconSettingsSignature() {
    uint32_t h = 2166136261u;
    auto mix = [&h](const void *data, size_t len) {
        const uint8_t *b = (const uint8_t *)data;
        for (size_t i = 0; i < len; i++) { h ^= b[i]; h *= 16777619u; }
    };
    mix(&s_cfg.meshBeaconBroadcast, sizeof(s_cfg.meshBeaconBroadcast));
    mix(&s_cfg.meshBeaconTargetPreset, sizeof(s_cfg.meshBeaconTargetPreset));
    mix(&s_cfg.meshBeaconOfferPsk, sizeof(s_cfg.meshBeaconOfferPsk));
    mix(&s_cfg.meshBeaconIntervalH, sizeof(s_cfg.meshBeaconIntervalH));
    mix(s_cfg.meshBeaconMessage, strnlen(s_cfg.meshBeaconMessage, sizeof(s_cfg.meshBeaconMessage)));
    return h;
}

// Beacon settings were saved: the payload is rebuilt on the next send, and the
// first check is pushed out by kMbSettleMs. The saved last-sent time still
// applies, so toggling broadcast off and on cannot get around the interval.
static void meshBeaconSettingsChanged() {
    s_mbPayloadValid = false;
    s_mbSettingsSig  = meshBeaconSettingsSignature();
    s_mbNotBeforeMs  = mbNonZeroMs(millis() + kMbSettleMs);
}

// Every config / channel-plan save. The offer (channel 0, preset, region) can
// change without a beacon setting changing, so the payload is always dropped;
// the settle hold only applies when a beacon setting itself moved, so an
// unrelated save (a brightness nudge) does not keep pushing the beacon back.
static void meshBeaconConfigSaved() {
    s_mbPayloadValid = false;
    if (meshBeaconSettingsSignature() != s_mbSettingsSig) meshBeaconSettingsChanged();
}

static bool meshBeaconGateOpen() {
#if defined(LORA_TEST_MQTT_ONLY) && LORA_TEST_MQTT_ONLY
    return false;
#else
    // Off while licensed: the beacon is sealed with the default channel key,
    // and nothing may go out encrypted on amateur bands.
    if (lic::active()) return false;
    return s_cfg.meshBeaconBroadcast && s_radioReady && Radio.isReady() &&
           s_myNodeId != 0 && s_cfg.deviceRole != relay::ROLE_CLIENT_HIDDEN;
#endif
}

// Whether the target preset can run here: this radio can produce it, and its
// bandwidth fits inside the current region's band. Never substituted.
static bool meshBeaconTargetRunnable(uint8_t target) {
    if (target >= PRESET_COUNT || !presetUsableOnThisRadio(target)) return false;
    for (uint8_t i = 0; i < kRegionCount; i++) {
        if (strcmp(kRegions[i].code, s_cfg.region) != 0) continue;
        const float bandMhz = kRegions[i].freqEnd - kRegions[i].freqStart;
        return kPresets[target].bw / 1000.0f <= bandMhz + 0.001f;
    }
    return false;
}

static uint32_t meshBeaconDueInMs(uint32_t nowMs) {
    const uint32_t a = beaconDueInMs(nowMs, s_mbLastSentMs, s_cfg.meshBeaconIntervalH, s_mbNotBeforeMs);
    const uint32_t b = beaconDueInMs(nowMs, s_mbLastSentMs, s_cfg.meshBeaconIntervalH, s_mbAirHoldUntilMs);
    return a > b ? a : b;
}

// The offer, from the live setup. Cached until the next config save.
static void meshBeaconBuildPayload() {
    MeshBeaconTx &tx = s_mbTx;
    memset(&tx, 0, sizeof(tx));
    beaconCopyMessage(tx.message, s_cfg.meshBeaconMessage);

    const ChannelKey &c0 = CHANNEL_KEYS[0];
    const uint8_t pi = s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : PRESET_LONG_FAST;
    const char *name = meshBeaconHomeChannelName();
    strncpy(tx.offerChannelName, name, sizeof(tx.offerChannelName) - 1);
    tx.offerChannelName[sizeof(tx.offerChannelName) - 1] = '\0';

    // A private key in a public beacon is a public key: opt-in only. Never
    // while licensed, where channels carry no key to offer.
    if (!lic::active() && s_cfg.meshBeaconOfferPsk
        && c0.keyLen > 0 && c0.keyLen <= sizeof(tx.offerPsk)) {
        memcpy(tx.offerPsk, c0.key, c0.keyLen);
        tx.offerPskLen = c0.keyLen;
    }
    tx.offerRegion = regionCodeToMeshtastic(s_cfg.region);
    // Custom modem settings have no preset to offer: channel + region only.
    if (s_cfg.loraUsePreset) {
        const int mp = presetToMeshtastic(pi);
        if (mp >= 0) {
            tx.hasOfferPreset = true;
            tx.offerPreset    = (uint8_t)mp;
        }
    }
    // The slot only when a receiver would land elsewhere by deriving it from
    // our region, bandwidth and the offered channel name.
    const float derived = regionSlotFreq(s_cfg.region, s_cfg.loraBw, tx.offerChannelName);
    if (fabsf(derived - s_cfg.loraFreq) > 0.0005f) {
        const uint32_t count = regionSlotCount(s_cfg.region, s_cfg.loraBw);
        for (uint32_t n = 0; n < count; n++) {
            if (fabsf(regionSlotFreqNum(s_cfg.region, s_cfg.loraBw, n) - s_cfg.loraFreq) <= 0.0005f) {
                tx.hasOfferSlot = true;
                tx.offerSlot    = n + 1;   // 1-based, like channel_num
                break;
            }
        }
    }

    s_mbPayloadLen   = encodeMeshBeacon(tx, s_mbPayload, sizeof(s_mbPayload));
    s_mbPayloadValid = true;
}

// Recorded before the send, so a crash mid-send cannot cause a re-send. With a
// valid clock the epoch is stored; without one 0 is stored, which boot reads
// as "sent at an unknown time" and so waits a full interval.
static void meshBeaconRecordSent(uint32_t nowMs) {
    s_mbLastSentMs = mbNonZeroMs(nowMs);
    s_mbLastEpoch  = clockIsSet() ? (uint32_t)time(nullptr) : 0;
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putUInt(kMbLastSentKey, s_mbLastEpoch);
    p.end();
}

// Boot: maps the saved last-sent time into this session's millis() domain.
static void meshBeaconBoot() {
    uint32_t saved = 0;
    bool     has   = false;
    Preferences p;
    if (p.begin("camillia", true)) {
        has = p.isKey(kMbLastSentKey);
        if (has) saved = p.getUInt(kMbLastSentKey, 0);
        p.end();
    }
    const uint32_t now = millis();
    s_mbSettingsSig = meshBeaconSettingsSignature();
    s_mbNotBeforeMs = mbNonZeroMs(now + kMbSettleMs);
    if (!has) return;

    s_mbLastEpoch = saved;
    if (saved != 0 && clockIsSet()) {
        const uint32_t intervalS = (uint32_t)clampBeaconIntervalH(s_cfg.meshBeaconIntervalH) * 3600UL;
        const uint32_t nowEpoch  = (uint32_t)time(nullptr);
        uint32_t elapsed = nowEpoch > saved ? nowEpoch - saved : 0;
        if (elapsed > intervalS) elapsed = intervalS;
        s_mbLastSentMs = mbNonZeroMs(now - elapsed * 1000UL);
    } else {
        // No clock to measure the gap with: count it as sent just now, so a
        // reboot errs towards waiting, never towards re-beaconing.
        s_mbLastSentMs = mbNonZeroMs(now);
    }
    Serial.printf("[beacon] last sent epoch=%lu, next in %lu s\n", (unsigned long)saved,
                  (unsigned long)(meshBeaconDueInMs(millis()) / 1000UL));
}

static MeshBeaconStatus meshBeaconStatus() {
    MeshBeaconStatus st = { MeshBeaconStatus::OFF, 0, s_mbLastEpoch };
    if (!meshBeaconGateOpen()) return st;
    if (discoveryRadioParked()) { st.state = MeshBeaconStatus::HELD_SCAN; return st; }
    if (!meshBeaconTargetRunnable(s_cfg.meshBeaconTargetPreset)) {
        st.state = MeshBeaconStatus::HELD_PRESET;
        return st;
    }
    st.state    = s_mbAirHoldUntilMs ? MeshBeaconStatus::HELD_AIRTIME : MeshBeaconStatus::WAITING;
    st.nextInMs = meshBeaconDueInMs(millis());
    return st;
}

static void serviceMeshBeacon() {
    const uint32_t now = millis();
    if (s_mbLastServiceMs != 0 && (uint32_t)(now - s_mbLastServiceMs) < kMbServiceGapMs) return;
    s_mbLastServiceMs = mbNonZeroMs(now);

    // Expired holds are cleared, gates or not, so one cannot read as live again
    // after a millis() half-wrap.
    if (s_mbNotBeforeMs && beaconDueInMs(now, 0, 1, s_mbNotBeforeMs) == 0) s_mbNotBeforeMs = 0;
    if (s_mbAirHoldUntilMs && beaconDueInMs(now, 0, 1, s_mbAirHoldUntilMs) == 0) s_mbAirHoldUntilMs = 0;

    if (!meshBeaconGateOpen()) return;
    if (discoveryRadioParked()) return;   // HELD_SCAN: retried once the scan ends
    const uint8_t target = s_cfg.meshBeaconTargetPreset;
    if (!meshBeaconTargetRunnable(target)) return;   // HELD_PRESET
    if (meshBeaconDueInMs(now) != 0) return;

    // A ham-only target region needs no licensed guard here: the beacon only
    // ever uses the current region, which cfgCoerceLicensed() keeps off ham
    // bands unless licensed.
    if (beaconRegionHasDutyCycle(s_cfg.region)) {
        const float airTx = Radio.airUtilTxPercent();
        if (airTx >= kMbAirUtilTxMaxPct) {
            s_mbAirRetryMs     = nextAirtimeRetryMs(s_mbAirRetryMs);
            s_mbAirHoldUntilMs = mbNonZeroMs(now + s_mbAirRetryMs);
            Serial.printf("[beacon] held: TX air-util %.1f%% >= %.0f%% in %s, retry in %lu min\n",
                          (double)airTx, (double)kMbAirUtilTxMaxPct, s_cfg.region,
                          (unsigned long)(s_mbAirRetryMs / 60000UL));
            return;
        }
    }
    s_mbAirHoldUntilMs = 0;

    if (!s_mbPayloadValid) meshBeaconBuildPayload();
    if (s_mbPayloadLen == 0) return;   // nothing to say (cannot happen while the offer is automatic)

    meshBeaconRecordSent(now);

    // The target's public default channel: the default key, named after the
    // preset. Value-initialised so useAead is false and the hash is plain
    // computeChannelHash(name, key, 16).
    const PresetParams &p = kPresets[target];
    ChannelKey key{};
    expandPsk(0x01, key.key);
    key.keyLen = 16;
    key.name   = p.channelName;
    key.hash   = channelKeyHash(key);

    const float freq = regionSlotFreq(s_cfg.region, p.bw, p.channelName);
    const bool retune = !(fabsf(freq - s_cfg.loraFreq) <= 0.0005f &&
                          fabsf(p.bw - s_cfg.loraBw) <= 0.01f &&
                          p.sf == s_cfg.loraSf && p.cr == s_cfg.loraCr);
    bool ok;
    if (retune) {
        TempTuneSnapshot snap = {};
        tempTuneEnter(target, snap, /*renameCh0=*/false);
        ok = Channels.sendMeshBeacon(s_myNodeId, s_mbPayload, s_mbPayloadLen, key);
        tempTuneLeave(snap);   // unconditional: a failed TX must still come home
    } else {
        ok = Channels.sendMeshBeacon(s_myNodeId, s_mbPayload, s_mbPayloadLen, key);
    }

    if (ok) s_mbAirRetryMs = 0;
    Serial.printf("[beacon] %s on %s %.3f MHz (%u bytes, offer ch=%s, psk=%s, retune=%s)\n",
                  ok ? "sent" : "send FAILED", p.channelName, (double)freq,
                  (unsigned)s_mbPayloadLen, s_mbTx.offerChannelName,
                  s_mbTx.offerPskLen ? "yes" : "no", retune ? "yes" : "no");
}

