// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: status icons, sleep overlay / lock screen, screen sleep and wake, buttons.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// Defined with the header code far below, but the glance surfaces paint their
// own pair long before that.
static void paintStatusIcons(lv_obj_t *gpsLabel, lv_obj_t *wifiLabel,
                             bool gpsEnabled, bool gpsFix, uint8_t gpsSatCount,
                             bool wifiApMode, bool wifiConnected,
                             const StatusIconInk &ink);

// GPS as the status icons show it: enabled, fix, satellites. Read through here
// by every status surface -- the chat header, the P4's strip (both from
// refreshHeaderStatus()) and the glance band below -- so they agree.
//
// On the T-Display P4 the reading is held for a minute. A receiver working the
// sky gains and drops satellites and the fix every few seconds, and redrawing
// the icon for each one kept it constantly changing for no useful information.
// Turning GPS on or off is the exception: that is the user's own action and
// shows at once. Every other board reads it live, as before.
#if defined(DEVICE_TDISPLAY_P4)
static constexpr uint32_t kGpsUiHoldMs = 60000UL;
#else
static constexpr uint32_t kGpsUiHoldMs = 0;
#endif
static void gpsUiReading(bool &enabled, bool &fix, uint8_t &sats) {
    static bool sHave = false;
    static bool sEnabled = false;
    static bool sFix = false;
    static uint8_t sSats = 0;
    static uint32_t sAtMs = 0;
    const bool liveEnabled = gpsIsEnabled();
    const uint32_t now = millis();
    if (!sHave || kGpsUiHoldMs == 0 || liveEnabled != sEnabled
        || (uint32_t)(now - sAtMs) >= kGpsUiHoldMs) {
        sEnabled = liveEnabled;
        sFix = liveEnabled && gpsHasFix();
        sSats = liveEnabled ? gpsSats() : 0;
        sAtMs = now;
        sHave = true;
    }
    enabled = sEnabled;
    fix = sFix;
    sats = sSats;
}

#if HAS_SLEEP_OVERLAY
static uint32_t tdeckProSleepClockMinuteKey() {
    const time_t now = time(nullptr);
    if (now >= kClockSetEpoch) return (uint32_t)(now / 60);
    return 0x80000000u | (millis() / 60000UL);
}

// Everything the notification area depends on, folded into one value. Used both
// to render it and to decide whether the sleeping panel needs repainting, so
// the two can never disagree about what is currently on screen.
//
// The sequence number in the high word is load-bearing. While this area was a
// count, the unread counts were the whole state: a second message on a channel
// already flagged unread genuinely changed nothing on screen. Now that it shows
// the newest three messages, that second message changes everything and moves
// neither the DM count nor the attention mask -- so a key built from those two
// alone reports "no change" and the panel silently never repaints. The sequence
// number moves on every message, and sitting in its own 32 bits it can never
// collide with an unread state that happens to hash the same way.
static uint64_t tdeckProSleepUnreadKey() {
    uint32_t chanMask = 0;
    for (int i = 0; i < MESH_CHANNELS && i < 24; i++) {
        if (s_channelNeedsAttention[i]) chanMask |= (1u << i);
    }
    int dm = DMs.unreadMessageCount();
    if (dm < 0)   dm = 0;
    if (dm > 255) dm = 255;   // saturates
    const uint32_t unread = ((uint32_t)dm << 24) | chanMask;
    return ((uint64_t)s_tdeckProRecentMsgSeq << 32) | unread;
}

// True when anything is waiting. The preview rows are shown only while
// something is unread, which is what the count line did before them.
static bool tdeckProSleepHasUnread() {
    if (DMs.unreadMessageCount() > 0) return true;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (s_channelNeedsAttention[i]) return true;
    }
    return false;
}

// Copy at most maxChars *characters* of src, appending "..." if that cut
// anything off. Counts codepoints rather than bytes: channel and node names are
// user-supplied and routinely hold multi-byte characters, and a byte-wise clamp
// would slice one in half and emit a broken glyph.
static void tdeckProClampLabel(char *out, size_t outLen, const char *src, int maxChars) {
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!src || !src[0] || maxChars <= 0) return;

    // Reserve three characters of the budget for the ellipsis, so a clamped
    // field still occupies the width it was allotted rather than overrunning it.
    const int keep = (maxChars > 3) ? (maxChars - 3) : maxChars;

    size_t bytes = 0, chars = 0;
    while (src[bytes] && (int)chars < keep) {
        size_t adv = 1;
        while (src[bytes + adv] && utf8util::isContinuationByte((uint8_t)src[bytes + adv])) adv++;
        if (bytes + adv >= outLen - 1) break;
        bytes += adv;
        chars++;
    }

    memcpy(out, src, bytes);
    out[bytes] = '\0';
    if (src[bytes] && keep < maxChars && bytes + 4 <= outLen) {
        memcpy(out + bytes, "...", 4);
    }
}

// One UTF-8 codepoint at *i, advancing *i past it. Malformed input advances by
// at least one byte, so no caller can be made to spin on it.
static uint32_t tdeckProNextCodepoint(const char *s, size_t *i) {
    const uint8_t b0 = (uint8_t)s[*i];
    (*i)++;
    if (b0 < 0x80) return b0;
    const int extra = (b0 >= 0xF0) ? 3 : (b0 >= 0xE0) ? 2 : 1;
    uint32_t cp = b0 & (0x3Fu >> extra);
    for (int k = 0; k < extra && utf8util::isContinuationByte((uint8_t)s[*i]); k++) {
        cp = (cp << 6) | ((uint8_t)s[*i] & 0x3Fu);
        (*i)++;
    }
    return cp;
}

// Longest prefix of `txt` that fits `maxW`, broken at the last space if there is
// one. Returns that prefix's byte length and reports through `nextOut` where the
// following line begins, past the space it broke on.
//
// Widths accumulate exactly the way lv_text_get_width() accumulates them --
// lv_font_get_glyph_width() per codepoint, with the following codepoint passed
// in for kerning -- so the split lands where LVGL would have wrapped it.
static size_t tdeckProBreakAt(const char *txt, const lv_font_t *font, int maxW,
                              size_t *nextOut) {
    size_t i = 0;
    size_t breakLen = 0, breakNext = 0;   // last space seen
    int w = 0;

    while (txt[i]) {
        size_t cur = i;
        const uint32_t letter = tdeckProNextCodepoint(txt, &cur);
        size_t peek = cur;
        const uint32_t nextLetter = txt[peek] ? tdeckProNextCodepoint(txt, &peek) : 0;

        w += lv_font_get_glyph_width(font, letter, nextLetter);
        if (w > maxW) {
            if (breakLen > 0) { *nextOut = breakNext; return breakLen; }
            // A single word wider than the whole line. Break inside it rather
            // than pushing all of it down, which would leave line one empty.
            *nextOut = (i > 0) ? i : cur;
            return *nextOut;
        }
        if (letter == ' ') { breakLen = i; breakNext = cur; }
        i = cur;
    }
    *nextOut = i;
    return i;
}

// The channel a preview row belongs to, as it should read on the panel.
static void tdeckProSleepChannelLabel(int chanIdx, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (chanIdx < 0 || chanIdx >= MESH_CHANNELS) {
        snprintf(out, outLen, "DM");
        return;
    }
    const ChannelKey &ck = CHANNEL_KEYS[chanIdx];
    const char *nm = ck.name_buf[0] ? ck.name_buf : ck.name;
    if (!nm || !nm[0]) snprintf(out, outLen, "ch%d", chanIdx);
    else               utf8util::copyTruncate(out, outLen, nm);
}

// Still-unread messages, newest first, up to the number of rows there are.
// Filtering on unread rather than just replaying the ring keeps the list honest:
// a channel read on another surface drops out of it instead of lingering as a
// preview of something already seen.
static int tdeckProCollectSleepMsgs(const TdeckProRecentMsg *out[kTdeckProSleepMsgSlots]) {
    const bool dmUnread = (DMs.unreadMessageCount() > 0);
    int found = 0;
    for (int back = 1; back <= s_tdeckProRecentMsgCount && found < kTdeckProSleepMsgSlots; back++) {
        const int idx = (s_tdeckProRecentMsgHead + kTdeckProSleepMsgSlots - back) % kTdeckProSleepMsgSlots;
        const TdeckProRecentMsg &m = s_tdeckProRecentMsgs[idx];
        const bool live = (m.chanIdx < 0)
                              ? dmUnread
                              : (m.chanIdx < MESH_CHANNELS && s_channelNeedsAttention[m.chanIdx]);
        if (live) out[found++] = &m;
    }
    return found;
}

// Bold 12 (Terminus Bold on this board, see src/fonts/eink/) with the metrics
// of lv_font_montserrat_12 and its icon and emoji fallbacks chained on. Both
// overrides matter: LVGL positions a label's glyphs from line_height and
// base_line, so the bold face's own metrics would sit the bold half of a row
// off the regular half's baseline, and without the
// fallback any character outside ASCII in a channel or node name would come out
// blank in the bold field while rendering fine in the regular one.
static const lv_font_t *tdeckProBoldRowFont() {
    static lv_font_t bold;
    static bool ready = false;
    if (!ready) {
        bold = lv_font_montserrat_bold_12;
        bold.line_height = lv_font_montserrat_12.line_height;
        bold.base_line   = lv_font_montserrat_12.base_line;
        bold.fallback    = emojiFont(&lv_font_montserrat_12);
        ready = true;
    }
    return &bold;
}

// ── Overlay palette ──────────────────────────────────────────────────────────
// The Pro is black on white because that is what an e-paper panel does well and
// what it holds with the power off. The lock-screen boards use white text with
// an orange clock, blue time/channel and green node accents on black. Every lit
// pixel is backlight the device is paying for, so the glance surface stays
// mostly unlit.
//
// Deliberately not the theme palette. This screen is not part of the themed UI
// — it is what the device looks like when the UI is put away — and a light
// theme would otherwise light the whole panel to show a clock.
#if FEATURE_LOCK_SCREEN
static inline lv_color_t sleepOverlayInk() { return lv_color_make(255, 255, 255); }
static inline lv_color_t sleepOverlayBg()  { return lv_color_make(0, 0, 0); }
static inline lv_color_t sleepOverlayTimeChannelInk() { return lv_color_make(32, 160, 255); }
static inline lv_color_t sleepOverlayNodeInk() { return lv_color_make(64, 220, 112); }
// The clock gets its own ink rather than sharing the time/channel blue. It is
// the one thing on this screen someone is actually looking for at a glance, and
// the blue is spoken for: the per-message prefix labels use it for "14:32
// #general", so recolouring that helper would have repainted every channel name
// on the panel too.
static inline lv_color_t sleepOverlayClockInk() { return lv_color_make(255, 149, 0); }
// The glance surfaces are not themed, so their status icons cannot be either:
// the light theme's "good" green is a dark one, chosen to be read on a pale
// header, and it disappears into this screen's black.
static inline StatusIconInk glanceStatusInk() {
    return StatusIconInk{ sleepOverlayNodeInk(),        // the green already here
                          lv_color_make(255, 107, 107),
                          lv_color_make(255, 107, 107),
                          lv_color_make(244, 211, 94) };
}
#if defined(DEVICE_TDISPLAY_P4)
// Not constants here: portrait on this panel has height to spare, and takes
// larger faces for the title (orientationApplyOverlayGeometry()) and for the
// clock and temperature (glanceFitHeroFonts()). Landscape keeps these.
static const lv_font_t *kSleepOverlayTitleFont       = &lv_font_montserrat_18;
static const lv_font_t *const kSleepOverlayNodeFont  = &lv_font_montserrat_14;
static const lv_font_t *kSleepOverlayTimeFont        = &lv_font_montserrat_32;
static const lv_font_t *kSleepOverlayWxFont          = &lv_font_montserrat_32;
#elif defined(DEVICE_CROWPANEL_50)
// Twice the usual faces, as its chat header is (the home dashboard and the lock
// screen share this header), where the font set goes that high: title 18 -> 32,
// node 14 -> 28 (named 24: hal/crowpanel_50_fonts.h puts every face but 32 and
// 40 a step up), and the clock and temperature 32 -> 40, the largest there is.
// The rows' positions follow from these (orientationApplyOverlayGeometry()).
static const lv_font_t *const kSleepOverlayTitleFont = &lv_font_montserrat_32;
static const lv_font_t *const kSleepOverlayNodeFont  = &lv_font_montserrat_24;
static const lv_font_t *const kSleepOverlayTimeFont  = &lv_font_montserrat_40;
static const lv_font_t *const kSleepOverlayWxFont    = &lv_font_montserrat_40;
#else
static const lv_font_t *const kSleepOverlayTitleFont = &lv_font_montserrat_18;
static const lv_font_t *const kSleepOverlayNodeFont  = &lv_font_montserrat_14;
static const lv_font_t *const kSleepOverlayTimeFont  = &lv_font_montserrat_32;
// Same weight as the clock it sits beside: the two are the point of the row.
static const lv_font_t *const kSleepOverlayWxFont    = &lv_font_montserrat_32;
#endif
#else
static inline lv_color_t sleepOverlayInk() { return lv_color_make(0, 0, 0); }
static inline lv_color_t sleepOverlayBg()  { return lv_color_make(255, 255, 255); }
static inline lv_color_t sleepOverlayTimeChannelInk() { return sleepOverlayInk(); }
static inline lv_color_t sleepOverlayNodeInk() { return sleepOverlayInk(); }
// Stays black: this is the e-paper overlay, where the panel has no colour to
// give and the whole point of the palette is what it holds with the power off.
static inline lv_color_t sleepOverlayClockInk() { return sleepOverlayInk(); }
// One ink here for the same reason the clock has one: nothing on this panel
// carries colour, so state has to read from the glyph and the strikethrough.
static inline StatusIconInk glanceStatusInk() {
    const lv_color_t ink = sleepOverlayInk();
    return StatusIconInk{ ink, ink, ink, ink };
}
static const lv_font_t *const kSleepOverlayTitleFont = &lv_font_montserrat_32;
static const lv_font_t *const kSleepOverlayNodeFont  = &lv_font_montserrat_16;
static const lv_font_t *const kSleepOverlayTimeFont  = &lv_font_montserrat_40;
// One size below the clock here, unlike the boards above. This panel is 240 px
// wide and its clock is 40 px: "12:34 PM" and a temperature at that size come
// to more than the width, so the pair would collide in the middle.
static const lv_font_t *const kSleepOverlayWxFont    = &lv_font_montserrat_32;
#endif

// Every colour a glance header draws with, so the two surfaces that share the
// header can disagree about the palette without the drawing code knowing.
//
// The lock screen's is fixed: that overlay is always a black panel on the lit
// boards and always black-on-white on the Pro's e-paper, and its inks were
// chosen against that known ground. The home dashboard is part of the themed UI
// — it is a screen you use, not what the device looks like with the UI put away
// — so it takes the theme's own colours.
struct GlancePalette {
    lv_color_t bg;         // the ground the header is drawn on
    lv_color_t ink;        // wordmark, the rule under it, date, battery
    lv_color_t nodeInk;    // node name, and the conditions opposite it
    lv_color_t clockInk;   // the clock, and the temperature opposite it
    StatusIconInk status;  // the GPS/Wi-Fi pair in the status band
};

// themed=false is the lock screen's, and is byte-for-byte what every one of
// these surfaces used before the dashboard asked for its own.
static GlancePalette glancePalette(bool themed) {
    const GlancePalette fixed = { sleepOverlayBg(), sleepOverlayInk(),
                                  sleepOverlayNodeInk(), sleepOverlayClockInk(),
                                  glanceStatusInk() };
#if defined(DEVICE_TDECK_PRO)
    // Not negotiable on this board whatever the theme says: the panel is 1-bit
    // and thresholds every colour it is given to pure black or pure white, so a
    // themed palette would come out as a coin toss per glyph. Same exemption
    // homeDashSeriesColor() and homeDashMutedInk() already carry.
    LV_UNUSED(themed);
    return fixed;
#else
    if (!themed) return fixed;

    GlancePalette p = fixed;
    p.bg  = lvColorFrom565(s_ui.bgMain);
    p.ink = lvColorFrom565(s_ui.textMain);
    // The identity line is the one place colour belongs here, so it takes the
    // theme's accent — the same role the fixed palette's green plays.
    p.nodeInk = lvColorFrom565(s_ui.accent);
    // Not the accent as well. The clock is 32-40 px of type: size is already
    // its emphasis, and a second accent-coloured element on a theme with a loud
    // one would fight the node name rather than rank under it. The fixed
    // palette can afford an orange clock because it also owns its ground.
    p.clockInk = lvColorFrom565(s_ui.textMain);
    // The themed pair the chat header already uses, so the icons mean the same
    // thing in the same colours wherever they appear on a themed screen.
    p.status = headerStatusInk();
    return p;
#endif
}

// Geometry of the notification area, in absolute panel coordinates. It sits in
// the band between the date and the battery.
static constexpr int kTdeckProMsgRowLeft   = 8;
static int sleepOverlayMsgRowWidth() {
    const int panelW = lv_disp_get_hor_res(NULL);
    const int width = panelW - 2 * kTdeckProMsgRowLeft;
    return (width > 0) ? width : 1;
}
// A preview may wrap onto a second line, so a message is one or two lines tall
// and the block is laid out by accumulating heights rather than by a fixed
// pitch -- that is what keeps a two-line message from drawing over the one
// below it.
//
// The budget is eight lines of text, however many messages that turns out to be:
// four if every one of them wraps, eight if none does. The tallest the block can
// therefore get is eight single-line messages, which is also the case with the
// most gaps in it: 8*15 + 7*6 = 162 px, which on the T-Deck Pro runs 134..296
// and leaves 24 px at the bottom.
#if HAS_RUNTIME_ORIENTATION
// Both shapes in one build, so these are set at boot rather than compiled in —
// see orientationApplyOverlayGeometry(). Initialised landscape; the portrait
// values are the ones the vertical build used.
static int kTdeckProMsgTop                  = 116;
#elif defined(DEVICE_TLORA_PAGER_TFT)
// The Pager is only 222 px tall in landscape. Five one-line messages consume
// 99 px including gaps and finish at y=205, leaving a safe bottom margin.
static constexpr int kTdeckProMsgTop        = 106;
#elif FEATURE_LOCK_SCREEN && DEVICE_UI_VERTICAL
static constexpr int kTdeckProMsgTop        = 150;
#elif FEATURE_LOCK_SCREEN
// Standard 320x240 lock screens fit six single-line messages: 6*15 + 5*6 =
// 120 px, running 116..236 with four pixels left at the bottom.
static constexpr int kTdeckProMsgTop        = 116;
#else
// The T-Deck Pro: 6 px under the clock's line box, as Home's widgets are
// (glanceBandTop()). Eight one-line messages run 134..296.
static constexpr int kTdeckProMsgTop        = 134;
#endif
#if defined(DEVICE_CROWPANEL_50)
static constexpr int kTdeckProMsgLineH      = 16;   // montserrat_12, drawn as 14 here
#else
static constexpr int kTdeckProMsgLineH      = 15;   // montserrat_12 line box
#endif
static constexpr int kTdeckProMsgMaxLines   = 2;    // per message
#if HAS_RUNTIME_ORIENTATION
static int kTdeckProMsgTotalLines           = 6;    // 320x240; 8 when portrait
#elif defined(DEVICE_TLORA_PAGER_TFT)
static constexpr int kTdeckProMsgTotalLines = 5;    // 480x222
#elif FEATURE_LOCK_SCREEN && DEVICE_UI_VERTICAL
static constexpr int kTdeckProMsgTotalLines = 8;    // 240x320
#elif FEATURE_LOCK_SCREEN
static constexpr int kTdeckProMsgTotalLines = 6;    // across the whole block
#else
static constexpr int kTdeckProMsgTotalLines = 8;    // across the whole block
#endif
static constexpr int kTdeckProMsgGapY       = 6;    // between messages, not lines
// Space between the bold half of a row and the rest, as real pixels rather than
// a trailing space character: whether LVGL counts a trailing space in a text's
// measured width is not something the gap should depend on.
static constexpr int kTdeckProMsgGapPx     = 4;

// The date and battery's face in the status band. Doubled on the CrowPanel 5",
// with the rest of its header (named 24: see hal/crowpanel_50_fonts.h).
static inline const lv_font_t *glanceBandFont() {
#if defined(DEVICE_CROWPANEL_50)
    return &lv_font_montserrat_24;
#else
    return &lv_font_montserrat_14;
#endif
}

// The status band across the top: date pinned left, battery pinned right, both
// at montserrat_14. The date used to be an 18 px centred line in the middle of
// the stack, but a corner pinned opposite the battery reads as a status band
// only if the two match -- a heavier date beside a lighter battery reads as two
// unrelated things that happen to share a row.
static constexpr int kTdeckProBandInset    = 8;    // from either edge
#if HAS_RUNTIME_ORIENTATION
static int kTdeckProBandTop                = 4;
static int kTdeckProTitleTop               = 24;   // 18 px face, 21 px line box
static int kTdeckProNodeTop                = 54;   // 14 px face, 16 px line box
static int kTdeckProTimeTop                = 72;   // 32 px face, 35 px line box

// The portrait values, applied once at boot. Landscape needs nothing done: it
// is what the initialisers above already hold, so a board that never rotates
// runs exactly the numbers it ran before this was a setting.
static void orientationApplyOverlayGeometry() {
#if defined(DEVICE_CROWPANEL_50)
    // Its doubled faces (kSleepOverlay*Font, glanceBandFont()) in either
    // orientation, each row placed under the line box of the one above -- read
    // from the fonts rather than written down, so a face change moves them.
    kTdeckProBandTop  = 8;
    kTdeckProTitleTop = kTdeckProBandTop
                      + (int)lv_font_get_line_height(glanceBandFont()) + 8;
    kTdeckProNodeTop  = kTdeckProTitleTop
                      + (int)lv_font_get_line_height(kSleepOverlayTitleFont) + 10;
    kTdeckProTimeTop  = kTdeckProNodeTop
                      + (int)lv_font_get_line_height(kSleepOverlayNodeFont) + 4;
    kTdeckProMsgTop   = kTdeckProTimeTop
                      + (int)lv_font_get_line_height(kSleepOverlayTimeFont) + 6;
    // 480 px of height in landscape, 800 in portrait: room for more of them.
    kTdeckProMsgTotalLines = uiPortrait() ? 24 : 12;
    return;
#endif
    if (!uiPortrait()) return;
    kTdeckProMsgTop        = 150;
    kTdeckProMsgTotalLines = 8;      // 240x320 has room for two more
    kTdeckProBandTop       = 8;
    kTdeckProTitleTop      = 30;
    kTdeckProNodeTop       = 58;
    kTdeckProTimeTop       = 82;
#if defined(DEVICE_TDISPLAY_P4)
    // A 32 px title rather than 18, so the rows under it move down: title line
    // box 35 + 4, node line box 16 + 8. The clock's own size is settled when
    // the header is built (glanceFitHeroFonts()); whatever it lands on, the
    // widgets below start under it, so they give up the height, not the text.
    kSleepOverlayTitleFont = &lv_font_montserrat_32;
    kTdeckProNodeTop       = kTdeckProTitleTop + 35 + 4;
    kTdeckProTimeTop       = kTdeckProNodeTop + 16 + 8;
#endif
}
#elif defined(DEVICE_TLORA_PAGER_TFT)
static constexpr int kTdeckProBandTop      = 3;
static constexpr int kTdeckProTitleTop     = 20;   // 18 px face, 21 px line box
static constexpr int kTdeckProNodeTop      = 44;   // 14 px face, 16 px line box
static constexpr int kTdeckProTimeTop      = 62;   // 32 px face, 35 px line box
#elif FEATURE_LOCK_SCREEN && DEVICE_UI_VERTICAL
static constexpr int kTdeckProBandTop      = 8;
static constexpr int kTdeckProTitleTop     = 30;   // 18 px face, 21 px line box
static constexpr int kTdeckProNodeTop      = 58;   // 14 px face, 16 px line box
static constexpr int kTdeckProTimeTop      = 82;   // 32 px face, 35 px line box
#elif FEATURE_LOCK_SCREEN
static constexpr int kTdeckProBandTop      = 4;
// Same order and grouping as the Pro, compressed into 80 px less height. The
// smaller faces also avoid linking 24 px and 40 px LVGL glyph tables solely for
// a backlit lock screen.
static constexpr int kTdeckProTitleTop     = 24;   // 18 px face, 21 px line box
static constexpr int kTdeckProNodeTop      = 54;   // 14 px face, 16 px line box
static constexpr int kTdeckProTimeTop      = 72;   // 32 px face, 35 px line box
#else
static constexpr int kTdeckProBandTop      = 8;
// "Camillia", the node name and the clock group together under the band, tight
// enough to read as one block. The clock's face is Terminus 32 in Montserrat
// 40's 44 px line box (src/fonts/eink/), which leaves 12 px of it empty above
// the digits -- so its top sits inside the node name's line box, and the digits
// land 9 px under the name's baseline.
static constexpr int kTdeckProTitleTop     = 30;   // 32 px face, 35 px line box
static constexpr int kTdeckProNodeTop      = 68;   // 16 px face, 18 px line box
static constexpr int kTdeckProTimeTop      = 78;   // 40 px face, 44 px line box
#endif
#if !HAS_RUNTIME_ORIENTATION
static inline void orientationApplyOverlayGeometry() {}
#endif
// However tight the row gets, leave this much for the message itself -- the
// point of the change is to show some of what was said, so a long channel name
// and a long sender name must give way to it rather than squeeze it to nothing.
static constexpr int kTdeckProMsgBodyMinPx = 74;

static int tdeckProTextWidth(const char *txt, const lv_font_t *font) {
    lv_point_t sz = {0, 0};
    lv_text_get_size(&sz, txt, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    return (int)sz.x;
}

// Fills one message: bold "HH:MM Channel", then regular "Name: message", the
// latter allowed to wrap onto a second line before LVGL ellipsises it. Draws at
// `y`, uses at most `maxLines` of them, and returns the height it took, so the
// caller can place the next message under it and keep count of the budget.
//
// The continuation line begins where the time above it ended, so it clears the
// timestamp column but sits left of the channel and sender -- far enough in that
// it cannot be mistaken for another message starting, far enough out that it
// gets most of the row's width. Montserrat's digits are proportional, so that x
// is measured per message rather than assumed.
//
// The fields are clamped by measuring rather than by counting characters. A
// character budget has to assume an average glyph width, and it is wrong in both
// directions -- "WWWWWWWW" and "iiiiiiii" are the same eight characters and
// nowhere near the same width -- which on a 240 px panel is the difference
// between a readable row and one with no message left on it.
static int tdeckProFillSleepMsgRow(int row, int y, int maxLines,
                                   const TdeckProRecentMsg &m) {
    lv_obj_t *boldLbl = s_tdeckProSleepMsgBold[row];
    lv_obj_t *senderLbl = s_tdeckProSleepMsgSender[row];
    lv_obj_t *restLbl = s_tdeckProSleepMsgRest[row];
    lv_obj_t *contLbl = s_tdeckProSleepMsgCont[row];
    if (!boldLbl || !senderLbl || !restLbl) return 0;

    const lv_font_t *boldFont = tdeckProBoldRowFont();
    const lv_font_t *restFont = emojiFont(&lv_font_montserrat_12);

    // The same fold the chat view applies, for the same reason: these faces
    // draw ASCII and little else, and a phone keyboard curls an apostrophe into
    // U+2019 on the way out — which reaches LVGL as a missing-glyph box. The
    // ring stores what arrived, so the fold belongs here, at the one place that
    // draws it.
    //
    // Once, and before anything measures it. Measuring the raw text and drawing
    // the folded one would break the line at a position computed from different
    // bytes, so the split would land in the wrong place.
    char body[sizeof(m.text) + 48];
    renderEmojiSafeText(m.text, body, sizeof(body));

    char timeText[LIVE_CLOCK_BUF] = "";
    if (m.epoch != 0) {
        const time_t when = (time_t)m.epoch;
        struct tm lt;
        localtime_r(&when, &lt);
        liveFormatClock(lt, timeText, sizeof(timeText));
    }

    // Where the continuation line will start. Measured from the time alone, not
    // from the whole bold field, so it lands under the channel name.
    const int timeW = timeText[0] ? tdeckProTextWidth(timeText, boldFont) : 0;

    char chanFull[24];
    tdeckProSleepChannelLabel(m.chanIdx, chanFull, sizeof(chanFull));

    char sender[48];
    {
        char raw[sizeof(sender)];
        chatSenderLabel(m.senderNodeId, raw, sizeof(raw));
        // Folded before the clamp below counts characters, not after: the fold
        // is what turns a three-byte codepoint into one.
        renderEmojiSafeText(raw, sender, sizeof(sender));
    }
    if (!sender[0]) snprintf(sender, sizeof(sender), "?");

    // Shrink the channel first, then the sender, one character at a time, until
    // the fixed part of the row leaves the body its minimum. The message text
    // itself is not clamped here at all -- LV_LABEL_LONG_DOT on a label of known
    // width does that, and does it by measurement too.
    char boldText[40];
    char senderText[32];
    int chanChars = 12;
    int nameChars = 14;
    int boldW = 0;
    int senderW = 0;
    for (;;) {
        char chanText[24];
        tdeckProClampLabel(chanText, sizeof(chanText), chanFull, chanChars);

        char nameText[32];
        tdeckProClampLabel(nameText, sizeof(nameText), sender, nameChars);

        if (timeText[0]) snprintf(boldText, sizeof(boldText), "%s %s", timeText, chanText);
        else             snprintf(boldText, sizeof(boldText), "%s", chanText);
        snprintf(senderText, sizeof(senderText), "%s:", nameText);

        boldW = tdeckProTextWidth(boldText, boldFont);
        senderW = tdeckProTextWidth(senderText, restFont);
        const int fixedW = boldW + kTdeckProMsgGapPx
                         + senderW + kTdeckProMsgGapPx;
        if (fixedW <= sleepOverlayMsgRowWidth() - kTdeckProMsgBodyMinPx) {
            break;
        }
        // Trim whichever field is still the more generous, so neither collapses
        // while the other stays long.
        if (chanChars >= nameChars && chanChars > 4)      chanChars--;
        else if (nameChars > 4)                            nameChars--;
        else {
            // Both are at their floor: the row is as short as it can be made.
            break;
        }
    }

    lv_label_set_text(boldLbl, boldText);
    lv_obj_align(boldLbl, LV_ALIGN_TOP_LEFT, kTdeckProMsgRowLeft, y);

    const int senderX = kTdeckProMsgRowLeft + boldW + kTdeckProMsgGapPx;
    lv_label_set_text(senderLbl, senderText);
    lv_obj_align(senderLbl, LV_ALIGN_TOP_LEFT, senderX, y);

    const int restX = senderX + senderW + kTdeckProMsgGapPx;
    int restW = sleepOverlayMsgRowWidth() - (restX - kTdeckProMsgRowLeft);
    if (restW < 1) restW = 1;

    // Split the body where LVGL would have wrapped it, rather than letting one
    // label wrap: the two lines start at different x, which is not something a
    // single label can express.
    size_t contStart = 0;
    const size_t firstLen = tdeckProBreakAt(body, restFont, restW, &contStart);
    const bool wraps = (maxLines > 1) && contLbl && (body[contStart] != '\0');

    lv_obj_clear_flag(boldLbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(senderLbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(restLbl, restW, kTdeckProMsgLineH);
    lv_obj_align(restLbl, LV_ALIGN_TOP_LEFT, restX, y);
    lv_obj_clear_flag(restLbl, LV_OBJ_FLAG_HIDDEN);

    if (!wraps) {
        // One line, either because the body fits on one or because the block has
        // no room left for a second. LV_LABEL_LONG_DOT rather than CLIP here:
        // with no continuation line to carry the remainder, the ellipsis is the
        // only thing left that can say there was more to the message.
        lv_label_set_long_mode(restLbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(restLbl, body);
        if (contLbl) lv_obj_add_flag(contLbl, LV_OBJ_FLAG_HIDDEN);
        return kTdeckProMsgLineH;
    }

    // Clipped, not dotted: this half of the split fits by construction, and the
    // ellipsis belongs on the line that carries what is left over.
    char firstLine[sizeof(body)];
    const size_t firstCopy = (firstLen < sizeof(firstLine)) ? firstLen : sizeof(firstLine) - 1;
    memcpy(firstLine, body, firstCopy);
    firstLine[firstCopy] = '\0';
    lv_label_set_long_mode(restLbl, LV_LABEL_LONG_CLIP);
    lv_label_set_text(restLbl, firstLine);

    // Line two, starting at the x where the time above it ended. The height is
    // one line and fixed, which is what makes the ellipsis appear: dots are what
    // LV_LABEL_LONG_DOT writes when text overflows the box it was given, so a
    // box left to grow to fit could never overflow and would never show them.
    const int contX = kTdeckProMsgRowLeft + timeW;
    int contW = sleepOverlayMsgRowWidth() - timeW;
    if (contW < 1) contW = 1;

    lv_label_set_text(contLbl, body + contStart);
    lv_obj_set_size(contLbl, contW, kTdeckProMsgLineH);
    lv_obj_align(contLbl, LV_ALIGN_TOP_LEFT, contX, y + kTdeckProMsgLineH);
    lv_obj_clear_flag(contLbl, LV_OBJ_FLAG_HIDDEN);

    return kTdeckProMsgMaxLines * kTdeckProMsgLineH;
}

// Repaints the whole notification area.
#if FEATURE_LOCK_SCREEN && HAS_TOUCH && HAS_MESSAGE_ACTIONS
// What each preview row is showing, as of its last paint, for the lock screen's
// long press (lockMsgRowAt()). Copied rather than pointed at: the ring slot a
// row was filled from can be overwritten before the row is repainted.
static uint32_t s_lockMsgRowPid[kTdeckProSleepMsgSlots] = {};
static uint32_t s_lockMsgRowSender[kTdeckProSleepMsgSlots] = {};
static int8_t   s_lockMsgRowChan[kTdeckProSleepMsgSlots] = {};
#endif

static void tdeckProRefreshSleepMsgRows() {
    const TdeckProRecentMsg *picked[kTdeckProSleepMsgSlots] = {nullptr};
    const int n = tdeckProSleepHasUnread() ? tdeckProCollectSleepMsgs(picked) : 0;

#if FEATURE_LOCK_SCREEN
    // Rows on a carousel page are placed from the top of that page, and the
    // page already sits at kTdeckProMsgTop. Rows straight on the overlay carry
    // the offset themselves, as they always have.
    int y = s_lockMsgPage ? 0 : kTdeckProMsgTop;
#else
    int y = kTdeckProMsgTop;
#endif
    int linesLeft = kTdeckProMsgTotalLines;
    for (int i = 0; i < kTdeckProSleepMsgSlots; i++) {
        if (i < n && picked[i] && linesLeft > 0) {
            // Offer the message what is left of the budget, never more than a
            // message may take. With one line left a long body still gets shown,
            // ellipsised, rather than dropped for not fitting in two.
            const int allow = (linesLeft < kTdeckProMsgMaxLines) ? linesLeft
                                                                 : kTdeckProMsgMaxLines;
            // Advance by what the message actually drew. A fixed pitch would
            // have to be two lines tall for every message -- leaving a blank
            // line under each short one -- or overlap whenever one wrapped.
            const int drew = tdeckProFillSleepMsgRow(i, y, allow, *picked[i]);
            y += drew + kTdeckProMsgGapY;
            linesLeft -= drew / kTdeckProMsgLineH;
#if FEATURE_LOCK_SCREEN && HAS_TOUCH && HAS_MESSAGE_ACTIONS
            s_lockMsgRowPid[i]    = picked[i]->packetId;
            s_lockMsgRowSender[i] = picked[i]->senderNodeId;
            s_lockMsgRowChan[i]   = picked[i]->chanIdx;
#endif
        } else if (s_tdeckProSleepMsgBold[i] && s_tdeckProSleepMsgSender[i]
               && s_tdeckProSleepMsgRest[i]) {
            // Hidden, not blanked: an empty label still occupies a line box, and
            // three of those under a quiet clock is exactly the empty-notification
            // -area artefact the single label was written to avoid.
            lv_obj_add_flag(s_tdeckProSleepMsgBold[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_tdeckProSleepMsgSender[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_tdeckProSleepMsgRest[i], LV_OBJ_FLAG_HIDDEN);
            if (s_tdeckProSleepMsgCont[i]) {
                lv_obj_add_flag(s_tdeckProSleepMsgCont[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// Everything the glance header's two status icons show, folded into one value
// so a caller can ask "has any of this changed" without re-reading it twice.
// Never zero: GlanceHeader::statusKey uses that for "not painted yet".
static uint16_t glanceStatusIconsKey() {
    bool gpsEnabled = false, gpsFix = false;
    uint8_t gpsSatCount = 0;
    gpsUiReading(gpsEnabled, gpsFix, gpsSatCount);
    wifi_mode_t wifiMode = WiFi.getMode();
    bool wifiApMode = (wifiMode == WIFI_AP);
#ifdef WIFI_AP_STA
    wifiApMode = wifiApMode || (wifiMode == WIFI_AP_STA);
#endif
    uint16_t key = 0x8000u;
    if (gpsEnabled)                                  key |= 0x0100u;
    if (gpsFix)                                      key |= 0x0200u;
    if (wifiApMode)                                  key |= 0x0400u;
    if (!wifiApMode && WiFi.status() == WL_CONNECTED) key |= 0x0800u;
    // Satellite count in the low byte: it is drawn, so a change in it is a
    // change on the panel. Clamped rather than masked — 255 and 0 must not
    // compare equal.
    const unsigned sats = gpsSatCount;
    key |= (uint16_t)(sats > 99 ? 99 : sats);
    return key;
}

// The GPS/Wi-Fi pair in a glance header's status band, painted and placed.
// Cheap enough to call on a state change rather than only on the minute tick:
// two labels, and on a lit panel LVGL redraws only what it is told changed.
static void paintGlanceStatusIcons(GlanceHeader &w) {
    if (!lvObjValid(w.gps) || !lvObjValid(w.wifi)) {
        // A header built without the pair (see buildGlanceHeader): it is
        // already showing everything it is going to, so record the state as
        // drawn rather than leaving callers to re-check it forever.
        w.statusKey = glanceStatusIconsKey();
        return;
    }

    bool gpsEnabled = false, gpsFix = false;
    uint8_t gpsSatCount = 0;
    gpsUiReading(gpsEnabled, gpsFix, gpsSatCount);
    wifi_mode_t wifiMode = WiFi.getMode();
    bool wifiApMode = (wifiMode == WIFI_AP);
#ifdef WIFI_AP_STA
    wifiApMode = wifiApMode || (wifiMode == WIFI_AP_STA);
#endif
    const bool wifiConnected = (!wifiApMode && WiFi.status() == WL_CONNECTED);

    paintStatusIcons(w.gps, w.wifi, gpsEnabled, gpsFix,
                     gpsSatCount, wifiApMode, wifiConnected,
                     glancePalette(w.themed).status);

    // Packed inward from the battery, the same order and direction the chat
    // header uses. Re-placed on every paint rather than once at build time
    // because the battery reading is what they hang off, and "9%" and "4.05V"
    // are not the same width.
    if (lvObjValid(w.batt)) {
        lv_obj_t *band = lv_obj_get_parent(w.batt);
        if (band) lv_obj_update_layout(band);
        if (lv_obj_has_flag(w.batt, LV_OBJ_FLAG_HIDDEN)) {
            // Battery hidden (the P4's status strip has it): Wi-Fi takes its
            // place against the corner rather than leaving its width as a gap.
            lv_obj_align_to(w.wifi, w.batt, LV_ALIGN_RIGHT_MID, 0, 0);
        } else {
            lv_obj_align_to(w.wifi, w.batt, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        }
        lv_obj_align_to(w.gps, w.wifi, LV_ALIGN_OUT_LEFT_MID, -7, 0);
    }

    w.statusKey = glanceStatusIconsKey();
}

// Repaint a glance header from the current clock, weather, battery and config.
// The caller decides when this runs; this decides what it says.
#if HAS_WEATHER
// Where the node name and the clock sit, which depends on whether there is
// anything to put beside them. With a reading they are the left half of a
// two-column hero block, each pair centred in its own half, the sky and the
// temperature mirroring them on the right. Without one that column is empty, and
// a pair still sitting in the left half reads as a layout with something missing
// from it rather than as a layout — so they take the whole width and centre, the
// way the builds with no weather support at all have always drawn them.
//
// Both offsets are the ones buildGlanceHeader() would have used, kept here
// rather than duplicated at the two call sites: this is the only code that
// decides where these two labels go.
// Size of a label's text as it will wrap at `maxW`: newlines are honoured, so a
// multi-line detail block measures as its widest line by its line count.
static lv_point_t glanceTextSize(lv_obj_t *label, int maxW) {
    lv_point_t sz = {0, 0};
    if (!lvObjValid(label)) return sz;
    lv_text_get_size(&sz, lv_label_get_text(label),
                     lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     0, lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                     maxW, LV_TEXT_FLAG_NONE);
    return sz;
}

// Where the temperature's label goes so that its digits start level with the
// clock's. Both labels are placed by their tops, and the tops of the digits sit
// a different distance down each line box whenever the two faces' line metrics
// differ -- on the T-Deck Pro the clock and the temperature are the same
// Terminus 32 glyphs in 44 px and 35 px boxes, which put the temperature 4 px
// high. Measured from a digit rather than assumed, so a font change follows.
static int glanceWxTempTop() {
#if defined(DEVICE_TDECK_PRO)
    auto digitTop = [](const lv_font_t *font) {
        lv_font_glyph_dsc_t g = {};
        if (!lv_font_get_glyph_dsc(font, &g, '0', 0)) return 0;
        return (int)lv_font_get_line_height(font) - (int)font->base_line
             - (int)g.box_h - (int)g.ofs_y;
    };
    return kTdeckProTimeTop + digitTop(kSleepOverlayTimeFont)
                            - digitTop(kSleepOverlayWxFont);
#else
    return kTdeckProTimeTop;
#endif
}

// The wide hero (GlanceHeader::wideHero): two cells, the rule between them.
// In each, the reading is left-aligned at the cell's left edge -- node name over
// the clock, conditions over the temperature -- and the detail block is right-
// aligned at the cell's right edge, centred on the two rows it sits beside. The
// reading gets whatever width the detail leaves it, and is cut short before it
// can run under it.
static void alignGlanceWide(GlanceHeader &w, bool wxShown) {
    lv_obj_t *parent = lv_obj_get_parent(w.node);
    lv_obj_update_layout(parent);
    const int fullW = (int)lv_obj_get_content_width(parent);
    const int halfW = fullW / 2;
    const int inset = kTdeckProBandInset;
    const int cellW = halfW - (2 * inset);
    const int heroH = (kTdeckProTimeTop - kTdeckProNodeTop)
                    + (int)lv_font_get_line_height(kSleepOverlayTimeFont);
    constexpr int kGap = 10;   // between a reading and its detail block

    if (lvObjValid(w.wxRule)) {
        if (wxShown) lv_obj_clear_flag(w.wxRule, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(w.wxRule, LV_OBJ_FLAG_HIDDEN);
    }

    // One cell: reading labels `top` and `bottom` from x, detail block at the
    // cell's right edge. Returns nothing; positions are all it decides.
    auto placeCell = [&](int x, lv_obj_t *top, lv_obj_t *bottom, lv_obj_t *detail) {
        const int bottomW = glanceTextSize(bottom, LV_COORD_MAX).x;
        int detailW = 0;
        if (lvObjValid(detail) && !lv_obj_has_flag(detail, LV_OBJ_FLAG_HIDDEN)) {
            // Never wider than the cell leaves beside the large reading under
            // it; past that it wraps rather than running underneath.
            const int room = max(40, cellW - bottomW - kGap);
            const lv_point_t sz = glanceTextSize(detail, room);
            // +1: a label exactly as wide as its measured text can still wrap
            // its longest line on rounding.
            detailW = sz.x + 1;
            lv_obj_set_width(detail, detailW);
            lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(detail, LV_ALIGN_TOP_LEFT, x + cellW - detailW,
                         kTdeckProNodeTop + max(0, (heroH - (int)sz.y) / 2));
        }
        const int readingW = max(20, cellW - (detailW ? detailW + kGap : 0));
        lv_obj_set_width(top, readingW);
        lv_obj_set_style_text_align(top, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(top, LV_ALIGN_TOP_LEFT, x, kTdeckProNodeTop);
        lv_obj_set_style_text_align(bottom, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, x, kTdeckProTimeTop);
    };

    placeCell(inset, w.node, w.time, w.nodeInfo);
    if (wxShown && lvObjValid(w.wxDesc) && lvObjValid(w.wxTemp)) {
        placeCell(halfW + inset, w.wxDesc, w.wxTemp, w.wxDetail);
    }
}

static void alignGlanceHero(GlanceHeader &w, bool wxShown) {
    if (!lvObjValid(w.node) || !lvObjValid(w.time)) return;
    if (w.wideHero) {
        alignGlanceWide(w, wxShown);
        return;
    }
    // The divider only means something while there are two columns to divide.
    // Centred with nothing to its right it would read as the panel being split
    // down the middle for no reason.
    if (lvObjValid(w.wxRule)) {
        if (wxShown) lv_obj_clear_flag(w.wxRule, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(w.wxRule, LV_OBJ_FLAG_HIDDEN);
    }
    // Each column's pair is centred in its own half rather than pushed to the
    // outer edge, so the two read as matching cells either side of the rule.
    // The x offsets are a quarter of the parent from its middle.
    if (wxShown && lvObjValid(w.wxDesc) && lvObjValid(w.wxTemp)) {
        lv_obj_t *parent = lv_obj_get_parent(w.node);
        lv_obj_update_layout(parent);
        const int halfW = (int)lv_obj_get_content_width(parent) / 2;
        const int cellW = halfW - (2 * kTdeckProBandInset);
        const int quarter = halfW / 2;
        lv_obj_set_width(w.node, cellW);
        lv_obj_set_style_text_align(w.node, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(w.node, LV_ALIGN_TOP_MID, -quarter, kTdeckProNodeTop);
        lv_obj_align(w.time, LV_ALIGN_TOP_MID, -quarter, kTdeckProTimeTop);
        lv_obj_set_width(w.wxDesc, cellW);
        lv_obj_set_style_text_align(w.wxDesc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(w.wxDesc, LV_ALIGN_TOP_MID, quarter, kTdeckProNodeTop);
        lv_obj_set_style_text_align(w.wxTemp, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(w.wxTemp, LV_ALIGN_TOP_MID, quarter, glanceWxTempTop());
        return;
    }
    // No reading (or no side column): one centred column at the build-time width.
    lv_obj_set_width(w.node, lv_pct(92));
    lv_obj_set_style_text_align(w.node, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(w.node, LV_ALIGN_TOP_MID, 0, kTdeckProNodeTop);
    lv_obj_align(w.time, LV_ALIGN_TOP_MID, 0, kTdeckProTimeTop);
}
#endif

#if HAS_WEATHER
// The fuller reading, one fact per line: conditions, how it feels, the wind,
// and where and how old. No degree sign -- it is not in the built-in faces --
// so units read as the hero's do ("72F"). The first line repeats the hero's
// temperature and conditions; the wide hero, which shows those beside it,
// takes the lines after it (glanceWeatherDetail()).
static void glanceWeatherText(const WeatherReading &wx, char *text, size_t cap) {
    static const char *const kCompass[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    const char *dir = kCompass[((wx.dirDeg % 360 + 360) % 360 + 22) / 45 % 8];
    char gust[24] = "";
    if (wx.gust > wx.wind) snprintf(gust, sizeof(gust), TR(", gusts %d"), wx.gust);
    char where[64] = "";
    const uint32_t ageMin = weatherAgeMs() / 60000UL;
    if (wx.place[0]) {
        snprintf(where, sizeof(where), TR("\n%s, %lu min ago"),
                 wx.place, (unsigned long)ageMin);
    } else {
        snprintf(where, sizeof(where), TR("\n%lu min ago"), (unsigned long)ageMin);
    }
    snprintf(text, cap,
             TR("%d%s  %s\nFeels %d%s, humidity %d%%\nWind %s %d %s%s%s"),
             wx.temp, wx.tempUnit, wx.desc,
             wx.feels, wx.tempUnit, wx.humidityPct,
             dir, wx.wind, wx.windUnit, gust, where);
}

// Everything after the first line of glanceWeatherText(): the details only.
// The same translated string, so a translation's line order carries over.
static const char *glanceWeatherDetail(const WeatherReading &wx, char *text, size_t cap) {
    glanceWeatherText(wx, text, cap);
    const char *nl = strchr(text, '\n');
    return nl ? nl + 1 : text;
}
#endif

// This node's particulars for the wide hero, one per line: ID, short name,
// firmware, role.
static void glanceNodeInfoText(char *out, size_t cap) {
    snprintf(out, cap, "!%08lx\n%s\n%s\n%s",
             (unsigned long)s_myNodeId,
             s_cfg.nodeShort[0] ? s_cfg.nodeShort : "-",
             APP_VERSION,
             cfgDeviceRoleName(s_cfg.deviceRole));
}

static void updateGlanceHeader(GlanceHeader &w) {
    if (!w.node || !w.time || !w.date) return;

    const char *nodeName = s_cfg.nodeLong[0] ? s_cfg.nodeLong : TR("Unknown");
    lv_label_set_text(w.node, nodeName);

    char timeText[LIVE_CLOCK_BUF] = "--:--";
    char dateText[48]; snprintf(dateText, sizeof(dateText), "%s", TR("Date unavailable"));
    const time_t now = time(nullptr);
    if (now >= kClockSetEpoch) {
        struct tm localTime;
        localtime_r(&now, &localTime);
        liveFormatClock(localTime, timeText, sizeof(timeText));
        if (i18nGetLang() == LANG_EN) {
            strftime(dateText, sizeof(dateText), "%a, %b %d, %Y", &localTime);
        } else {
            // strftime() only knows English names. Day first, as every other
            // UI language writes it; the names come from the translations.
            static const char *const kWday[7] = {
                TR_NOOP("Sun"), TR_NOOP("Mon"), TR_NOOP("Tue"), TR_NOOP("Wed"),
                TR_NOOP("Thu"), TR_NOOP("Fri"), TR_NOOP("Sat")
            };
            static const char *const kMon[12] = {
                TR_NOOP("Jan"), TR_NOOP("Feb"), TR_NOOP("Mar"), TR_NOOP("Apr"),
                TR_NOOP("May"), TR_NOOP("Jun"), TR_NOOP("Jul"), TR_NOOP("Aug"),
                TR_NOOP("Sep"), TR_NOOP("Oct"), TR_NOOP("Nov"), TR_NOOP("Dec")
            };
            snprintf(dateText, sizeof(dateText), TR("%s, %d %s %d"),
                     TR(kWday[localTime.tm_wday % 7]), localTime.tm_mday,
                     TR(kMon[localTime.tm_mon % 12]), localTime.tm_year + 1900);
        }
    }
    lv_label_set_text(w.time, timeText);

    lv_label_set_text(w.date, dateText);

#if HAS_WEATHER
    // The right-hand half of the two hero rows. Read from the cache and never
    // fetched: this screen exists to be cheap, and waking Wi-Fi to refresh a
    // temperature nobody asked for is the opposite of that. It shows whatever
    // the Weather screen last obtained, and nothing at all once that is too old
    // to believe — a device asleep overnight should not wake showing yesterday
    // afternoon as if it were now.
    // Keep the reading current while the screen is up. This runs on the
    // overlay's own repaint, which is once a minute, and weatherRequest() is a
    // no-op until the cached reading passes its TTL — so the actual fetch rate
    // is the TTL, not the tick.
    //
    // It will not bring the radio up: the Wi-Fi check is explicit here rather
    // than left to weatherRequest(), because failing inside it would also flip
    // the module's state to an error, and a glance surface should have no
    // opinion on what the Weather screen last displayed.
    WeatherReading wx;
    if (weatherState() != WEATHER_FETCHING
        && s_cfg.weatherServer[0]
        && WiFi.status() == WL_CONNECTED) {
        const bool haveFresh = weatherLatest(wx) && weatherAgeMs() < kWeatherTtlMs;
        double wxLat = 0, wxLon = 0;
        if (!haveFresh && nodeLosSelfPosition(wxLat, wxLon)) {
            weatherRequest(s_cfg.weatherServer, wxLat, wxLon, s_cfg.displayUnits != 0);
        }
    }

    const bool wxFresh = weatherLatest(wx) && weatherAgeMs() < kWeatherGlanceMaxAgeMs;
    if (lvObjValid(w.wxTemp) && lvObjValid(w.wxDesc)) {
        // Read before the flag is touched below: the label's own hidden state
        // is what the header is currently laid out for, so it answers "has this
        // changed" without a second field to keep in step with it.
        const bool wasShown = !lv_obj_has_flag(w.wxTemp, LV_OBJ_FLAG_HIDDEN);
        if (wxFresh) {
            char t[16];
            snprintf(t, sizeof(t), "%d%s", wx.temp, wx.tempUnit);
            lv_label_set_text(w.wxTemp, t);
            lv_label_set_text(w.wxDesc, wx.desc);
            lv_obj_clear_flag(w.wxTemp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(w.wxDesc, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Hidden, not emptied: an empty label still occupies its row, and
            // the left column should read as deliberate rather than as the
            // survivor of something that failed.
            lv_obj_add_flag(w.wxTemp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(w.wxDesc, LV_OBJ_FLAG_HIDDEN);
        }
        // The hero pair moves with it. Only on a change: an align call
        // invalidates the object, and on the Pro's e-paper that is a real
        // refresh to spend once a minute on a layout that has not moved.
        if (wxFresh != wasShown) alignGlanceHero(w, wxFresh);
    }
    if (w.wxBelow && lvObjValid(w.wxBelowText)) {
        if (wxFresh) {
            char text[224];
            glanceWeatherText(wx, text, sizeof(text));
            lv_label_set_text(w.wxBelowText, text);
            lv_obj_clear_flag(w.wxBelowText, LV_OBJ_FLAG_HIDDEN);
            if (lvObjValid(w.wxBelowRule)) lv_obj_clear_flag(w.wxBelowRule, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Nothing retrieved (or too old): no rule leading to nothing.
            lv_obj_add_flag(w.wxBelowText, LV_OBJ_FLAG_HIDDEN);
            if (lvObjValid(w.wxBelowRule)) lv_obj_add_flag(w.wxBelowRule, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (w.wideHero) {
        if (lvObjValid(w.nodeInfo)) {
            char info[96];
            glanceNodeInfoText(info, sizeof(info));
            lv_label_set_text(w.nodeInfo, info);
        }
        if (lvObjValid(w.wxDetail)) {
            if (wxFresh) {
                char text[224];
                lv_label_set_text(w.wxDetail, glanceWeatherDetail(wx, text, sizeof(text)));
                lv_obj_clear_flag(w.wxDetail, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(w.wxDetail, LV_OBJ_FLAG_HIDDEN);
            }
        }
        // Every repaint, not only when the reading comes or goes: the widths
        // the cells are divided by are the texts', and they change by the
        // minute ("9:59" to "10:00", a new wind reading).
        alignGlanceHero(w, wxFresh);
    }
#endif

    // Read here rather than on its own timer, so the battery costs no extra
    // panel refresh: callers run this when the minute rolls over (or on an
    // unread change that is already repainting), and on e-paper a refresh is
    // ~1 s of parked loop() and real battery. A charge level that is one minute
    // stale on a sleeping device is not worth a redraw of its own.
    //
    // Follows the Battery Display setting for the same reason the header does —
    // someone who chose voltage wants voltage everywhere, not just awake.
    if (w.batt && lv_obj_is_valid(w.batt)) {
        char battText[16];
        if (s_cfg.battDisplayMode == BATT_DISPLAY_VOLTAGE) {
            // snprintf, not lv_label_set_text_fmt: LVGL's printf has no float
            // support in this build, so "%.2fV" would reach the panel literally.
            snprintf(battText, sizeof(battText), "%.2fV",
                     (double)batteryReadVoltage());
        } else {
            snprintf(battText, sizeof(battText), "%u%%",
                     (unsigned)batteryReadPercent());
        }
        lv_label_set_text(w.batt, battText);
        // No repositioning: pinned to the top-right corner it cannot collide
        // with the preview rows, so it stays where buildGlanceHeader put it
        // whether or not there is anything unread.
    }

    // Last, because the icons are placed against the battery label and it has
    // just been given the text that decides its width.
    paintGlanceStatusIcons(w);
}

static void updateTdeckProSleepClock() {
    if (!s_sleepGlance.node || !s_sleepGlance.time || !s_sleepGlance.date) return;

    updateGlanceHeader(s_sleepGlance);

    if (s_tdeckProSleepMsgBold[0] && lv_obj_is_valid(s_tdeckProSleepMsgBold[0])) {
        tdeckProRefreshSleepMsgRows();
        s_tdeckProSleepUnreadKey = tdeckProSleepUnreadKey();
    }

    // Re-read rather than threaded out of the header: the two want the same
    // instant, and a second time() call a microsecond later cannot land in a
    // different minute than the one just drawn.
    const time_t now = time(nullptr);
    s_tdeckProSleepMinuteKey = (now >= kClockSetEpoch)
                                   ? (uint32_t)(now / 60)
                                   : (0x80000000u | (millis() / 60000UL));
}

// Build the glance header into `parent` and record its widgets in `w`.
//
// Every position here is an absolute offset from the parent's top edge rather
// than a share of it, so a parent shorter than the panel lays the header out
// exactly as the full-screen lock overlay does -- which is what lets the home
// dashboard hand it the top half of the screen and get the same block back.
// themed picks the palette (see glancePalette): the lock screen passes false
// for its own fixed one, the home dashboard true for the theme's.
//
// withStatusIcons=false leaves the GPS/Wi-Fi pair out of the status band. The
// home dashboard passes false wherever the bar underneath it is already showing
// them: the dashboard does not cover the bottom bar, so on those boards the two
// would be on screen twice, a few centimetres apart. The lock screen always
// passes true — it is an opaque, full-panel overlay, and nothing it covers is
// showing anything.
#if defined(DEVICE_TDISPLAY_P4)
// Portrait on the P4: the clock and temperature at 40 px wherever they fit, 32
// where they do not. Each sits in half the width beside the other (see
// alignGlanceHero()), and a 12-hour "12:58 PM" at 40 px is wider than that half
// of a 284 px panel, where the 24-hour form is not -- so it is measured, against
// a worst-case reading in the clock format actually set, rather than assumed.
static void glanceFitHeroFonts() {
    if (!uiPortrait()) return;
    const int colW = (int)lv_disp_get_hor_res(NULL) / 2 - kTdeckProBandInset - 6;
    auto fits = [colW](const char *text, const lv_font_t *font) {
        lv_point_t sz = {0, 0};
        lv_text_get_size(&sz, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        return (int)sz.x <= colW;
    };
    struct tm worst = {};
    worst.tm_hour = 12;   // "12", and PM wherever there is a suffix
    worst.tm_min = 58;
    char clockSample[LIVE_CLOCK_BUF];
    liveFormatClock(worst, clockSample, sizeof(clockSample));
    kSleepOverlayTimeFont = fits(clockSample, &lv_font_montserrat_40)
                                ? &lv_font_montserrat_40 : &lv_font_montserrat_32;
    // Three digits and a sign is as wide as a temperature gets.
    kSleepOverlayWxFont = fits("-100\xC2\xB0" "F", &lv_font_montserrat_40)
                              ? &lv_font_montserrat_40 : &lv_font_montserrat_32;
}
#endif

#if !defined(HAS_EINK)
// A small camellia: five pink petals round a gold centre, d units across, its
// top-left corner at (x, y) from the parent's top-middle.
static void glanceAddCamellia(lv_obj_t *parent, int d, int x, int y) {
    lv_obj_t *f = lv_obj_create(parent);
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, d, d);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(f, LV_ALIGN_TOP_MID, x, y);

    auto dot = [f](int size, int cx, int cy, uint32_t rgb) {
        lv_obj_t *o = lv_obj_create(f);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, size, size);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(o, cx - size / 2, cy - size / 2);
    };
    // Petal centres at 72-degree steps from straight up, in thousandths.
    static const int16_t kDir[5][2] = {
        {0, -1000}, {951, -309}, {588, 809}, {-588, 809}, {-951, -309},
    };
    const int c = d / 2;
    const int petal = (d * 52) / 100;
    const int reach = c - petal / 2;
    for (const auto &v : kDir) {
        dot(petal, c + (v[0] * reach) / 1000, c + (v[1] * reach) / 1000, 0xE0457B);
    }
    dot((d * 34) / 100, c, c, 0xF6C343);
}
#endif

static void buildGlanceHeader(lv_obj_t *parent, GlanceHeader &w,
                              bool withStatusIcons, bool themed) {
    w.themed = themed;
#if HAS_STATUS_STRIP
    // Portrait: GPS, Wi-Fi and battery are in the status strip under the nav
    // bar, which shows under this screen too -- not a second time up here.
    if (uiPortrait()) withStatusIcons = false;
#endif
#if defined(DEVICE_TDISPLAY_P4)
    glanceFitHeroFonts();   // before anything below reads those faces
#endif
    const GlancePalette pal = glancePalette(themed);

    w.title = lv_label_create(parent);
    lv_obj_set_width(w.title, lv_pct(92));
    lv_obj_set_style_text_font(w.title, kSleepOverlayTitleFont, 0);
    lv_obj_set_style_text_color(w.title, pal.ink, 0);
    lv_obj_set_style_text_align(w.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w.title, TR("Camillia"));
    lv_obj_align(w.title, LV_ALIGN_TOP_MID, 0, kTdeckProTitleTop);
#if !defined(HAS_EINK)
    // A flower either side of the wordmark, for a little colour. Placed from
    // the measured word, so a translated title keeps them beside it.
    {
        const int lineH = (int)lv_font_get_line_height(kSleepOverlayTitleFont);
        lv_point_t sz = {0, 0};
        lv_text_get_size(&sz, TR("Camillia"), kSleepOverlayTitleFont, 0, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int d = (lineH * 55) / 100;
        if (d < 10) d = 10;
        const int offset = (int)sz.x / 2 + d / 2 + d / 2;
        const int y = kTdeckProTitleTop + (lineH - d) / 2;
        glanceAddCamellia(parent, d, -offset, y);
        glanceAddCamellia(parent, d, offset, y);
    }
#endif

    // A rule under the wordmark, separating "which device is this" from
    // everything the screen is actually reporting.
    //
    // Placed in the middle of whatever gap the board leaves rather than at a
    // fixed offset below the title: the six layouts put the title and the node
    // name between 3 px and 9 px apart, so any constant that cleared one would
    // land on top of the other. Measured from the title's own face, so it
    // follows a font change too.
    {
        const int titleBottom =
            kTdeckProTitleTop + (int)lv_font_get_line_height(kSleepOverlayTitleFont);
        int ruleY = titleBottom + ((kTdeckProNodeTop - titleBottom) / 2);
        if (ruleY <= titleBottom) ruleY = titleBottom;   // no gap: sit flush

        lv_obj_t *sleepRule = lv_obj_create(parent);
        // Edge to edge. The lock screen and the home dashboard both hand this
        // a full-width, unpadded parent, so 100% is the panel.
        lv_obj_set_width(sleepRule, lv_pct(100));
        lv_obj_set_height(sleepRule, 1);
        lv_obj_clear_flag(sleepRule, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(sleepRule, 0, 0);
        lv_obj_set_style_radius(sleepRule, 0, 0);
        lv_obj_set_style_pad_all(sleepRule, 0, 0);
        lv_obj_set_style_bg_color(sleepRule, pal.ink, 0);
#if defined(DEVICE_TDECK_PRO)
        // Full strength on the e-paper: that panel is 1-bit, so a partial
        // opacity thresholds to solid or to nothing with no say in which.
        lv_obj_set_style_bg_opa(sleepRule, LV_OPA_COVER, 0);
#else
        // Dimmer than the type it separates — a divider should organise the
        // screen, not compete with it for attention.
        lv_obj_set_style_bg_opa(sleepRule, LV_OPA_40, 0);
#endif
        lv_obj_align(sleepRule, LV_ALIGN_TOP_MID, 0, ruleY);
    }

    w.node = lv_label_create(parent);
    lv_obj_set_width(w.node, lv_pct(92));
    lv_obj_set_style_text_font(w.node, kSleepOverlayNodeFont, 0);
    lv_obj_set_style_text_color(w.node, pal.nodeInk, 0);
#if HAS_WEATHER
    // Left half of the hero block when there is a reading to sit opposite it,
    // and the middle of the panel when there is not. Two rows either way, each
    // with a small label and a large one: node name over the clock on this
    // side, sky over the temperature on the other. Placed by alignGlanceHero()
    // below, once both labels of the pair exist — it owns that choice, and it
    // is remade whenever the reading comes or goes.
    lv_label_set_long_mode(w.node, LV_LABEL_LONG_DOT);
#else
    lv_obj_set_style_text_align(w.node, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.node, LV_LABEL_LONG_DOT);
    lv_obj_align(w.node, LV_ALIGN_TOP_MID, 0, kTdeckProNodeTop);
#endif

    w.time = lv_label_create(parent);
    lv_obj_set_style_text_font(w.time, kSleepOverlayTimeFont, 0);
    lv_obj_set_style_text_color(w.time, pal.clockInk, 0);
#if HAS_WEATHER
    // Assigned on every path, for the reason given at w.wxRule below.
    w.wxBelowRule = nullptr;
    w.wxBelowText = nullptr;
    w.nodeInfo = nullptr;
    w.wxDetail = nullptr;
    if (w.wideHero) {
        // The detail blocks share one face, a size under the node name, so
        // four short lines sit comfortably beside the two hero rows.
        w.nodeInfo = lv_label_create(parent);
        lv_obj_set_style_text_font(w.nodeInfo, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(w.nodeInfo, pal.nodeInk, 0);
        lv_label_set_long_mode(w.nodeInfo, LV_LABEL_LONG_WRAP);
        lv_label_set_text(w.nodeInfo, "");
    }
    if (w.wxBelow) {
        // Weather under the clock rather than beside it: a rule across the
        // column, and the fuller reading left-aligned beneath. The side column is
        // not built, so the node name and clock stay centred (alignGlanceHero()
        // with nothing shown), and updateGlanceHeader() fills this instead.
        w.wxDesc = nullptr;
        w.wxTemp = nullptr;
        w.wxRule = nullptr;
        const int ruleY = kTdeckProTimeTop
                        + (int)lv_font_get_line_height(kSleepOverlayTimeFont) + 6;
        w.wxBelowRule = lv_obj_create(parent);
        lv_obj_set_size(w.wxBelowRule, lv_pct(84), 1);
        lv_obj_clear_flag(w.wxBelowRule, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(w.wxBelowRule, 0, 0);
        lv_obj_set_style_radius(w.wxBelowRule, 0, 0);
        lv_obj_set_style_pad_all(w.wxBelowRule, 0, 0);
        lv_obj_set_style_bg_color(w.wxBelowRule, pal.ink, 0);
        lv_obj_set_style_bg_opa(w.wxBelowRule, LV_OPA_40, 0);
        lv_obj_align(w.wxBelowRule, LV_ALIGN_TOP_MID, 0, ruleY);
        lv_obj_add_flag(w.wxBelowRule, LV_OBJ_FLAG_HIDDEN);

        w.wxBelowText = lv_label_create(parent);
        lv_obj_set_width(w.wxBelowText, lv_pct(92));
        lv_obj_set_style_text_font(w.wxBelowText, kSleepOverlayNodeFont, 0);
        lv_obj_set_style_text_color(w.wxBelowText, pal.ink, 0);
        lv_obj_set_style_text_align(w.wxBelowText, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_line_space(w.wxBelowText, 3, 0);
        lv_label_set_long_mode(w.wxBelowText, LV_LABEL_LONG_WRAP);
        lv_label_set_text(w.wxBelowText, "");
        lv_obj_align(w.wxBelowText, LV_ALIGN_TOP_MID, 0, ruleY + 7);
        lv_obj_add_flag(w.wxBelowText, LV_OBJ_FLAG_HIDDEN);
        alignGlanceHero(w, /*wxShown=*/false);
    } else {
    // The opposite column. Same two rows, mirrored: the sky on the node's row,
    // the temperature on the clock's.
    w.wxDesc = lv_label_create(parent);
    lv_obj_set_style_text_font(w.wxDesc, kSleepOverlayNodeFont, 0);
    lv_obj_set_style_text_color(w.wxDesc, pal.nodeInk, 0);
    lv_obj_set_style_text_align(w.wxDesc, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(w.wxDesc, LV_LABEL_LONG_DOT);
    lv_obj_align(w.wxDesc, LV_ALIGN_TOP_RIGHT,
                 -kTdeckProBandInset, kTdeckProNodeTop);
    lv_obj_add_flag(w.wxDesc, LV_OBJ_FLAG_HIDDEN);

    w.wxTemp = lv_label_create(parent);
    lv_obj_set_style_text_font(w.wxTemp, kSleepOverlayWxFont, 0);
    lv_obj_set_style_text_color(w.wxTemp, pal.clockInk, 0);
    lv_obj_set_style_text_align(w.wxTemp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(w.wxTemp, LV_ALIGN_TOP_RIGHT,
                 -kTdeckProBandInset, glanceWxTempTop());
    lv_obj_add_flag(w.wxTemp, LV_OBJ_FLAG_HIDDEN);

    if (w.wideHero) {
        w.wxDetail = lv_label_create(parent);
        lv_obj_set_style_text_font(w.wxDetail, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(w.wxDetail, pal.nodeInk, 0);
        lv_label_set_long_mode(w.wxDetail, LV_LABEL_LONG_WRAP);
        lv_label_set_text(w.wxDetail, "");
        lv_obj_add_flag(w.wxDetail, LV_OBJ_FLAG_HIDDEN);
    }

    // The seam between the two columns. Same ink, same weight and the same
    // e-paper exemption as the rule under the wordmark above -- one divider
    // idiom on this header, turned on its side.
    //
    // It spans the hero block exactly: from the top of the node name's line box
    // to the bottom of the clock's, which is the taller of the two columns in
    // every layout (the temperature's face is never larger than the clock's).
    // Measured from the fonts rather than given a constant, because the six
    // layouts put these rows in six different places.
    //
    // Cleared before the guard below, not left to the guard: showTdeckProSleep-
    // Clock() rebuilds the overlay without resetting the struct, so every field
    // here has to be assigned on every path or it keeps a pointer into the
    // header that was just deleted.
    w.wxRule = nullptr;
    const int heroH = (kTdeckProTimeTop - kTdeckProNodeTop)
                    + (int)lv_font_get_line_height(kSleepOverlayTimeFont);
    if (heroH > 1) {
        w.wxRule = lv_obj_create(parent);
        lv_obj_set_width(w.wxRule, 1);
        lv_obj_set_height(w.wxRule, heroH);
        lv_obj_clear_flag(w.wxRule, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_width(w.wxRule, 0, 0);
        lv_obj_set_style_radius(w.wxRule, 0, 0);
        lv_obj_set_style_pad_all(w.wxRule, 0, 0);
        lv_obj_set_style_bg_color(w.wxRule, pal.ink, 0);
#if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(w.wxRule, LV_OPA_COVER, 0);
#else
        lv_obj_set_style_bg_opa(w.wxRule, LV_OPA_40, 0);
#endif
        lv_obj_align(w.wxRule, LV_ALIGN_TOP_MID, 0, kTdeckProNodeTop);
        lv_obj_add_flag(w.wxRule, LV_OBJ_FLAG_HIDDEN);
    }

    // Centred to start with, because the three labels above start hidden. The
    // first repaint that finds a fresh reading moves the pair into the left
    // half; until then a header with no weather is drawn as one that never had
    // any.
    alignGlanceHero(w, /*wxShown=*/false);
    }   // !w.wxBelow
#else
    lv_obj_align(w.time, LV_ALIGN_TOP_MID, 0, kTdeckProTimeTop);
#endif

    // Left half of the status band. Sized to its own text rather than to a
    // percentage of the panel: a 92% box pinned to the left edge would reach
    // under the battery, and the two would collide the moment either grew.
    w.date = lv_label_create(parent);
    lv_obj_set_style_text_font(w.date, glanceBandFont(), 0);
    lv_obj_set_style_text_color(w.date, pal.ink, 0);
    lv_obj_set_style_text_align(w.date, LV_TEXT_ALIGN_LEFT, 0);
    // This band is the only row of the glance that reaches the top corners,
    // so it alone takes the rounded-panel inset (board.h).
#if defined(DEVICE_TDISPLAY_P4)
    // P4 landscape: centred, with GPS, Wi-Fi and the battery together at the
    // right-hand end. The band is 616 px across there, and a date pinned to
    // the far corner read as unrelated to the readings at the other.
    if (!uiPortrait()) {
        lv_obj_set_style_text_align(w.date, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(w.date, LV_ALIGN_TOP_MID, 0, kTdeckProBandTop);
    } else
#endif
    lv_obj_align(w.date, LV_ALIGN_TOP_LEFT,
                 kTdeckProBandInset + UI_CORNER_SAFE_X, kTdeckProBandTop);

    // Right half of the status band, opposite the date. Still the smallest text
    // on the screen -- it is a glance value, and on a panel with no colour the
    // only way to rank information is size -- but a corner now says so as well.
    // Right-aligned to the edge rather than centred, so "4.05V" and "9%" both
    // end in the same place instead of drifting with the reading.
    w.batt = lv_label_create(parent);
    lv_obj_set_style_text_font(w.batt, glanceBandFont(), 0);
    lv_obj_set_style_text_color(w.batt, pal.ink, 0);
    lv_obj_set_style_text_align(w.batt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(w.batt, LV_ALIGN_TOP_RIGHT,
                 -(kTdeckProBandInset + UI_CORNER_SAFE_X), kTdeckProBandTop);
#if HAS_STATUS_STRIP
    // In portrait the reading is in the status strip under the nav bar, which
    // shows on this screen too. Kept and updated, only hidden: it is still the
    // anchor the icons below are placed from (paintGlanceStatusIcons()).
    if (uiPortrait()) lv_obj_add_flag(w.batt, LV_OBJ_FLAG_HIDDEN);
#endif

    // GPS and Wi-Fi, inside the same band, running inward from the battery.
    // "Is it on the network, does it know where it is" is half of what anyone
    // wakes this screen to find out.
    //
    // A size below the date and the battery on purpose: those are readings, and
    // these are indicator lights. The band is also the narrowest row on the
    // panel — a 240 px one carries a date and a battery already — so the two
    // smallest icons that can still be told apart is what fits beside them.
    if (!withStatusIcons) {
        paintGlanceStatusIcons(w);   // records the state; there is nothing to draw
        return;
    }

    w.gps = lv_label_create(parent);
    lv_obj_set_style_text_font(w.gps, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(w.gps, pal.ink, 0);
    lv_label_set_text(w.gps, LV_SYMBOL_GPS);

    w.wifi = lv_label_create(parent);
    lv_obj_set_style_text_font(w.wifi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(w.wifi, pal.ink, 0);
    lv_label_set_text(w.wifi, LV_SYMBOL_WIFI);

    // Placed here as well as on every repaint: the caller may not update the
    // header before the panel is next drawn, and two icons stacked in the
    // corner they were created in is a worse first frame than none at all.
    paintGlanceStatusIcons(w);
}

// The header's shape, shared by Home and the lock screen so a landscape touch
// panel draws one header on both: weather beside the clock, never under it, and
// on the P4 in landscape the two-cell wide hero (alignGlanceWide()).
static void glanceSetHomeShape(GlanceHeader &w) {
#if HAS_WEATHER
    w.wxBelow = false;
#if defined(DEVICE_TDISPLAY_P4)
    w.wideHero = !uiPortrait();
#else
    w.wideHero = false;
#endif
#else
    LV_UNUSED(w);
#endif
}

// The first free row under the header's clock, which is where Home's widget
// band starts. Read after buildGlanceHeader(): the P4 settles the clock's face
// there (glanceFitHeroFonts()).
static int glanceBandTop() {
    return kTdeckProTimeTop + (int)lv_font_get_line_height(kSleepOverlayTimeFont) + 6;
}

static void showTdeckProSleepClock() {
    if (!s_rootScreen) return;
    if (s_tdeckProSleepOverlay && lv_obj_is_valid(s_tdeckProSleepOverlay)) {
        lv_obj_del(s_tdeckProSleepOverlay);
    }

    s_tdeckProSleepOverlay = lv_obj_create(s_rootScreen);
    lv_obj_remove_style_all(s_tdeckProSleepOverlay);
    lv_obj_set_size(s_tdeckProSleepOverlay, lv_pct(100), lv_pct(100));
    lv_obj_align(s_tdeckProSleepOverlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_tdeckProSleepOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_tdeckProSleepOverlay, sleepOverlayBg(), 0);
    lv_obj_set_style_bg_opa(s_tdeckProSleepOverlay, LV_OPA_COVER, 0);
#if FEATURE_LOCK_SCREEN
    // Swallow taps. remove_style_all() leaves the overlay non-clickable, which
    // on a lit panel means touches fall straight through to whatever screen is
    // still built underneath — the lock screen would be pressing buttons the
    // user cannot see. The Pro does not need this: its panel is asleep and the
    // touch controller with it.
    lv_obj_add_flag(s_tdeckProSleepOverlay, LV_OBJ_FLAG_CLICKABLE);
#endif

#if FEATURE_LOCK_SCREEN && HAS_TOUCH
    // Landscape touch panels: the same header Home draws (glanceSetHomeShape()).
    if (!uiPortrait()) glanceSetHomeShape(s_sleepGlance);
#endif
    buildGlanceHeader(s_tdeckProSleepOverlay, s_sleepGlance,
                      /*withStatusIcons=*/true, /*themed=*/false);

    // Under the date: message previews, newest first. Separate first-line labels
    // keep time/channel blue, sender green and message white on Wio. Plain text
    // has no border or background, for the reason the single count line had none --
    // the rows exist even when there is nothing to say, and any persistent
    // decoration would draw empty boxes on an otherwise quiet screensaver. They
    // are hidden rather than emptied when unused; see tdeckProRefreshSleepMsgRows.
    //
    // Left-aligned, unlike everything above them: this is a list of messages,
    // and centring each row would leave the names in a different place on every
    // line for no gain.
    lv_obj_t *msgParent = s_tdeckProSleepOverlay;
#if FEATURE_LOCK_SCREEN
    // On the lock screen this band is a carousel and the messages are its first
    // face, so the rows are built onto that page instead of onto the overlay.
    // Everything below is unchanged by it: the page is full width and sits at
    // the band top (kTdeckProMsgTop, or Home's band top on landscape touch
    // panels), so the x offsets and the row width still mean what they
    // did, and only the y origin moves -- see tdeckProRefreshSleepMsgRows().
    //
    // The Pro never gets here: FEATURE_LOCK_SCREEN excludes it, and its band
    // stays the fixed message list it has always been.
    //
    // The band runs down to the bottom edge, with nothing taken off for a
    // margin. The message
    // budget is sized to exactly this: kTdeckProMsgTotalLines and the gaps
    // between them are chosen per board to run from kTdeckProMsgTop to within a
    // few pixels of the bottom, so a band any shorter would clip the last row
    // against the page it now lives in -- where before it was a child of a
    // full-height overlay and had the whole panel to overhang into. The cards
    // on the other faces keep their inset regardless: the page inside this
    // carries kTdeckProBandInset/2 of padding of its own.
    int bandTop = kTdeckProMsgTop;
#if HAS_TOUCH
    // Landscape touch panels start the band where Home starts its widgets, so
    // the cards sit in the same place on both screens. Every such panel still
    // fits the full message budget under it (kTdeckProMsgTotalLines).
    if (!uiPortrait()) bandTop = glanceBandTop();
#endif
    const int bandH = lv_disp_get_ver_res(NULL) - bandTop;
    s_lockMsgPage = buildLockCarousel(s_tdeckProSleepOverlay, bandTop, bandH);
    if (s_lockMsgPage) msgParent = s_lockMsgPage;
#endif
    for (int i = 0; i < kTdeckProSleepMsgSlots; i++) {
        s_tdeckProSleepMsgBold[i] = lv_label_create(msgParent);
        lv_obj_set_style_text_font(s_tdeckProSleepMsgBold[i], tdeckProBoldRowFont(), 0);
        lv_obj_set_style_text_color(s_tdeckProSleepMsgBold[i], sleepOverlayTimeChannelInk(), 0);
        // Pinned rather than inherited. Every position on these rows is computed
        // from lv_text_get_size() and a 15 px line, so letter spacing, line
        // spacing and padding have to be the values that arithmetic assumes --
        // a theme that set any of them would put the labels somewhere the code
        // did not measure.
        lv_obj_set_style_text_letter_space(s_tdeckProSleepMsgBold[i], 0, 0);
        lv_obj_set_style_text_line_space(s_tdeckProSleepMsgBold[i], 0, 0);
        lv_obj_set_style_pad_all(s_tdeckProSleepMsgBold[i], 0, 0);
        lv_label_set_long_mode(s_tdeckProSleepMsgBold[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_tdeckProSleepMsgBold[i], LV_OBJ_FLAG_HIDDEN);

        s_tdeckProSleepMsgSender[i] = lv_label_create(msgParent);
        lv_obj_set_style_text_font(s_tdeckProSleepMsgSender[i], emojiFont(&lv_font_montserrat_12), 0);
        lv_obj_set_style_text_color(s_tdeckProSleepMsgSender[i], sleepOverlayNodeInk(), 0);
        lv_obj_set_style_text_letter_space(s_tdeckProSleepMsgSender[i], 0, 0);
        lv_obj_set_style_text_line_space(s_tdeckProSleepMsgSender[i], 0, 0);
        lv_obj_set_style_pad_all(s_tdeckProSleepMsgSender[i], 0, 0);
        lv_label_set_long_mode(s_tdeckProSleepMsgSender[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_tdeckProSleepMsgSender[i], LV_OBJ_FLAG_HIDDEN);

        s_tdeckProSleepMsgRest[i] = lv_label_create(msgParent);
        lv_obj_set_style_text_font(s_tdeckProSleepMsgRest[i], emojiFont(&lv_font_montserrat_12), 0);
        lv_obj_set_style_text_color(s_tdeckProSleepMsgRest[i], sleepOverlayInk(), 0);
        lv_obj_set_style_text_letter_space(s_tdeckProSleepMsgRest[i], 0, 0);
        lv_obj_set_style_text_line_space(s_tdeckProSleepMsgRest[i], 0, 0);
        lv_obj_set_style_pad_all(s_tdeckProSleepMsgRest[i], 0, 0);
        // Clipped, not dotted: the split is computed here, so whatever is put in
        // this label already fits. An ellipsis on it could only be a disagreement
        // between the measurement and LVGL, and it belongs on the line below.
        lv_label_set_long_mode(s_tdeckProSleepMsgRest[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_tdeckProSleepMsgRest[i], LV_OBJ_FLAG_HIDDEN);

        // Line two. The only label that ellipsises -- it holds everything that
        // did not fit on line one, and says so when it cannot show all of that
        // either.
        s_tdeckProSleepMsgCont[i] = lv_label_create(msgParent);
        lv_obj_set_style_text_font(s_tdeckProSleepMsgCont[i], emojiFont(&lv_font_montserrat_12), 0);
        lv_obj_set_style_text_color(s_tdeckProSleepMsgCont[i], sleepOverlayInk(), 0);
        lv_obj_set_style_text_letter_space(s_tdeckProSleepMsgCont[i], 0, 0);
        lv_obj_set_style_text_line_space(s_tdeckProSleepMsgCont[i], 0, 0);
        lv_obj_set_style_pad_all(s_tdeckProSleepMsgCont[i], 0, 0);
        lv_label_set_long_mode(s_tdeckProSleepMsgCont[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(s_tdeckProSleepMsgCont[i], LV_OBJ_FLAG_HIDDEN);
    }


    updateTdeckProSleepClock();
    lv_obj_move_foreground(s_tdeckProSleepOverlay);
    lv_obj_invalidate(s_tdeckProSleepOverlay);
    lv_refr_now(s_lvDisplay);
#if defined(DEVICE_TDECK_PRO)
    // E-paper only. The panel is about to be put to sleep with this image on
    // it, so the image has to be pushed now; a lit LCD is repainted by LVGL's
    // normal timer and needs none of this.
    lcd.requestRefresh(true);
    lcd.serviceRefresh(true);
#endif
}

static void hideTdeckProSleepClock() {
    if (s_tdeckProSleepOverlay && lv_obj_is_valid(s_tdeckProSleepOverlay)) {
        lv_obj_del(s_tdeckProSleepOverlay);
    }
    s_tdeckProSleepOverlay = nullptr;
    // Whole struct, not field by field: every widget in it was a child of the
    // overlay and went with it, and a list maintained by hand is one field
    // behind the next time the header grows one. Same reset closeHomeDashboard()
    // does to its copy.
    s_sleepGlance = GlanceHeader{};
    for (int i = 0; i < kTdeckProSleepMsgSlots; i++) {
        s_tdeckProSleepMsgBold[i] = nullptr;
        s_tdeckProSleepMsgSender[i] = nullptr;
        s_tdeckProSleepMsgRest[i] = nullptr;
        s_tdeckProSleepMsgCont[i] = nullptr;
    }
#if FEATURE_LOCK_SCREEN
    s_lockMsgPage = nullptr;
    resetLockCarousel();
#endif
    s_tdeckProSleepMinuteKey = UINT32_MAX;
    s_tdeckProSleepUnreadKey = UINT64_MAX;
    s_tdeckProSleepUnreadPendingKey = UINT64_MAX;
    s_tdeckProSleepUnreadChangedMs = 0;
    if (s_rootScreen) lv_obj_invalidate(s_rootScreen);
}

// How long the unread state must hold still before the panel is repainted for
// it. A busy channel can land several messages in a few seconds, and each
// repaint parks loop() for the better part of a second and costs real battery
// on a device that is meant to be asleep -- so a burst collapses into one
// refresh showing the final count rather than one per message.
static constexpr uint32_t kTdeckProSleepUnreadQuietMs = 3000;

static void serviceTdeckProSleepClock() {
#if FEATURE_LOCK_SCREEN
    // The overlay is what is on a *lit* panel here, so the gate is the lock
    // screen being up rather than the panel being out. s_lockScreenActive is
    // declared below with the rest of the lock-screen state; this runs from the
    // main loop, well after both exist.
    if (!s_lockScreenActive || !s_tdeckProSleepOverlay) return;
#else
    if (!s_screenAsleep || !s_tdeckProSleepOverlay) return;
#endif

    const uint32_t nowMs = millis();
    const uint64_t unreadNow = tdeckProSleepUnreadKey();
    if (unreadNow != s_tdeckProSleepUnreadPendingKey) {
        s_tdeckProSleepUnreadPendingKey = unreadNow;
        s_tdeckProSleepUnreadChangedMs = nowMs;
    }
    // Settled on something other than what the overlay is showing.
    const bool unreadDue =
        (unreadNow != s_tdeckProSleepUnreadKey)
        && ((uint32_t)(nowMs - s_tdeckProSleepUnreadChangedMs) >= kTdeckProSleepUnreadQuietMs);

#if FEATURE_LOCK_SCREEN
    // GPS and Wi-Fi are the one thing on this screen that is worth showing the
    // moment it changes — watching a device pick up a fix, or come up as an AP,
    // is why the icons are here. Cheap, because it repaints the two labels
    // rather than the overlay: LVGL invalidates what changed and the panel is
    // lit already. Not done on the Pro, where any change at all is a full
    // e-paper refresh; there they ride the minute below like everything else.
    if (glanceStatusIconsKey() != s_sleepGlance.statusKey) {
        paintGlanceStatusIcons(s_sleepGlance);
    }
#endif

    const bool minuteDue = (tdeckProSleepClockMinuteKey() != s_tdeckProSleepMinuteKey);
    if (!minuteDue && !unreadDue) return;

#if defined(DEVICE_TDECK_PRO)
    // Wake the controller only for the repaint, then put it straight back.
    lcd.wakeup(false);
    updateTdeckProSleepClock();
    lv_obj_invalidate(s_tdeckProSleepOverlay);
    lv_refr_now(s_lvDisplay);
    lcd.serviceRefresh(true);
    lcd.sleep();
#else
    // Panel is already on and LVGL is already running: mark it dirty and let
    // the normal refresh timer pick it up.
    updateTdeckProSleepClock();
#if FEATURE_LOCK_SCREEN
    // Whatever face the band is showing rides the same minute the clock does:
    // a chart's newest sample, and a node page's ages, which creep with the
    // clock rather than with traffic. The messages face is repainted twice on
    // this tick -- updateTdeckProSleepClock() owns it and this does not know
    // which face is up -- which is two label writes a minute for a much simpler
    // rule about who repaints what.
    refreshLockCarouselPage();
#endif
    lv_obj_invalidate(s_tdeckProSleepOverlay);
#endif
}
#endif

static void sleepScreen(const char *reason) {
    if (s_screenAsleep) return;

    // sleep() zeroes the backlight and then issues SLPIN, which stops the
    // panel's internal refresh — setBrightness(0) alone leaves the controller
    // fully powered and self-refreshing an image nobody can see. It drives the
    // panel's brightness directly rather than going through
    // LGFX_Device::setBrightness(), so the cached duty survives for wakeup().
#if defined(DEVICE_TDECK_PRO)
    showTdeckProSleepClock();
#endif
    displayDev().sleep();
    setTouchSleep(true);
    setPagerKeyboardBacklight(false);
    s_backlightPendingOn = false;
    s_preSleepDimmed = false;
    s_screenAsleep = true;
    s_screenWakeBlockedUntilMs = millis() + kScreenWakeInputDelayMs;
    serviceCpuScaling();

    if (reason && reason[0]) {
        Serial.printf("[screen] sleeping (%s)\n", reason);
    } else {
        Serial.println("[screen] sleeping");
    }
}

// The chat view is in front of the user again, so whatever piled up behind the
// glance overlay for the conversation that is actually open has now been seen.
// This is the counterpart to the RX gates: they stopped trusting "selected" to
// mean "read", so something has to mark it read at the moment that becomes
// true. Without it the last-used channel would stay flagged and the open DM
// unread while the user looks straight at them.
static void noteUiVisibleAgain() {
    if (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
        s_channelNeedsAttention[s_activeChannel] = false;
    }
    const uint32_t peer = openDmPeerNodeId();
    if (peer != 0) {
        DMs.markRead(peer);
        refreshDmModal(true);   // clears the envelope indicator with it
    }
    refreshChannelGlow(true);
}

#if FEATURE_LOCK_SCREEN
// Puts the UI away behind the glance overlay, leaving the panel lit. Callers
// check s_cfg.lockScreenEnabled — with it off nothing here is ever reached and
// the firmware keeps its original two-state behaviour exactly.
static void enterLockScreen(const char *reason) {
    if (s_lockScreenActive || s_screenAsleep) return;

    showTdeckProSleepClock();
    s_lockScreenActive = true;
    s_lockScreenSinceMs = millis();
    // Match the dark-panel path's guard so the press that entered the lock
    // screen cannot also dismiss it when that input reports its release/click.
    s_screenWakeBlockedUntilMs = s_lockScreenSinceMs + kScreenWakeInputDelayMs;
    // Whatever the backlight was doing on the way here — the pre-sleep dim in
    // particular — the lock screen is shown at its own configured level, not at
    // whatever fraction the dim had reached. A glance surface that arrives
    // already dimmed by something else reads as a fault. s_lockScreenActive is
    // set above, so applyBrightness() below resolves to lockScreenBrightness.
    s_preSleepDimmed = false;
    setPagerKeyboardBacklight(false);
    applyBrightness();

    if (s_cfg.lockScreenOffSecs == LOCK_SCREEN_OFF_NEVER) {
        Serial.printf("[screen] lock screen (%s), staying on\n", reason ? reason : "");
    } else {
        Serial.printf("[screen] lock screen (%s), panel out in %lus\n",
                      reason ? reason : "",
                      (unsigned long)s_cfg.lockScreenOffSecs);
    }
}

// Back to the UI. Used by each board's wake gesture, and by anything that has
// to take the overlay down without going through panel sleep — a UI rebuild.
static void exitLockScreen() {
    if (!s_lockScreenActive) return;
    s_lockScreenActive = false;
    hideTdeckProSleepClock();

    // Restart the idle clock. Without this the screen timeout is measured from
    // whenever input last landed, which was before the lock screen, and the UI
    // would drop straight back onto it.
    s_lastActivityMs = millis();
    s_preSleepDimmed = false;

    // The UI underneath has been sitting behind an opaque overlay and its
    // dirty-checked refreshers have had nothing to do; force the repaint the
    // same way wakeScreen() does.
    s_lastRenderedChannel = -1;
    s_lastRenderedCount = -1;
    s_lastHeaderTime[0] = '\0';
    s_lastBattPct = 255;
    setPagerKeyboardBacklight(true);
    // s_lockScreenActive is already false, so this resolves back to the UI's own
    // brightness. Nothing else on the way out does it, and without it the UI
    // would come back at the lock screen's level.
    applyBrightness();
    if (s_rootScreen) lv_obj_invalidate(s_rootScreen);
    noteUiVisibleAgain();
    Serial.println("[screen] lock screen dismissed");
}

// Consumes every input while locked. A permitted wake gesture dismisses the
// overlay once the entry guard has elapsed; all other input stays swallowed so
// it cannot operate the UI hidden underneath.
static bool tryExitLockScreenFromInput(uint32_t nowMs, bool mayWake) {
    if (!s_lockScreenActive) return false;
    if (mayWake && (int32_t)(nowMs - s_screenWakeBlockedUntilMs) >= 0) {
        exitLockScreen();
    }
    return true;
}

// Ends the lit phase once lockScreenOffSecs has run out. Never fires on the
// "stay on" setting, which is the whole point of offering it.
static void serviceLockScreen(uint32_t nowMs) {
    if (!s_lockScreenActive) return;

    // A UI rebuild deletes the root screen and takes the overlay with it
    // without anything here being told. Left alone, the flag would stay set
    // over a visible UI and the next wake gesture would appear to do nothing.
    if (!s_tdeckProSleepOverlay || !lv_obj_is_valid(s_tdeckProSleepOverlay)) {
        s_tdeckProSleepOverlay = nullptr;
        exitLockScreen();
        return;
    }
    // Turned off from web config while it was up.
    if (!s_cfg.lockScreenEnabled) {
        exitLockScreen();
        return;
    }

    // Ahead of the timeout below, and deliberately: on the "stay on" setting
    // that return is the end of this function, and a lock screen left up
    // forever is exactly the one that should not sit on one face forever.
    serviceLockCarousel(nowMs);

    if (s_cfg.lockScreenOffSecs == LOCK_SCREEN_OFF_NEVER) return;

    const uint32_t heldMs = (uint32_t)(nowMs - s_lockScreenSinceMs);
    if (heldMs < (uint32_t)s_cfg.lockScreenOffSecs * 1000UL) return;

    // Panel out first, overlay down second: taking the overlay down while the
    // screen is still lit would show a frame of the UI on the way to darkness.
    s_lockScreenActive = false;
    sleepScreen("lock screen timeout");
    hideTdeckProSleepClock();
}
#endif  // FEATURE_LOCK_SCREEN

static void requestScreenOff(const char *reason) {
#if FEATURE_LOCK_SCREEN
    if (s_cfg.lockScreenEnabled) {
        enterLockScreen(reason);
        return;
    }
#endif
    sleepScreen(reason);
}

#if HAS_NOTIFY_LED
// ── RGB notification LED ────────────────────────────────────────────────────
// Cathodes on expander 0x59 P10 (R) / P11 (G) / P12 (B), active low, common
// anode. Two rules from Attaky's driver notes are load-bearing here:
//
//   * share the expander handle the buttons and CTP_RESET already use, and
//   * read-modify-write — never write a whole port byte.
//
// P13 on this same port is the touch controller's reset line. A blanket write
// would drop it and put the FT6636 back into reset, killing touch until the
// next boot. So every access below masks itself to P10..P12.
//
// On the Tanmatsu the same light is the coprocessor's "messages" LED; only
// meshDeckLedSet() differs, and the state machine below is shared.
#if defined(DEVICE_MESH_DECK)
static constexpr uint8_t kMdLedMask = 0x07;          // P10 | P11 | P12

static void meshDeckLedSet(bool r, bool g, bool b) {
    if (!s_mdButtonsReady) return;

    static bool configured = false;
    if (!configured) {
        uint8_t cfg = 0xFF;
        if (!s_mdButtons.readReg(Aw9523::REG_CONFIG1, cfg)) return;
        // Only the three LED bits become outputs; P13..P17 keep their direction.
        if (!s_mdButtons.writeReg(Aw9523::REG_CONFIG1, (uint8_t)(cfg & ~kMdLedMask))) return;
        configured = true;
    }

    uint8_t out = 0xFF;
    if (!s_mdButtons.readReg(Aw9523::REG_OUTPUT1, out)) return;
    out |= kMdLedMask;                               // all three off (active low)
    if (r) out &= (uint8_t)~(1u << 0);
    if (g) out &= (uint8_t)~(1u << 1);
    if (b) out &= (uint8_t)~(1u << 2);
    s_mdButtons.writeReg(Aw9523::REG_OUTPUT1, out);
}
#elif defined(DEVICE_TANMATSU)
// Bright enough to catch the eye beside the screen without lighting the room.
static constexpr uint8_t kTanmatsuLedLevel = 0x30;

static void meshDeckLedSet(bool r, bool g, bool b) {
    tanmatsuCoprocSetLed(TANMATSU_LED_MESSAGE,
                         r ? kTanmatsuLedLevel : 0,
                         g ? kTanmatsuLedLevel : 0,
                         b ? kTanmatsuLedLevel : 0);
}
#endif

// Blink pattern state. Non-blocking: an incoming message only arms the counter,
// and the loop services it — a blocking blink would stall the UI and the radio.
static uint8_t  s_mdLedPhasesLeft = 0;
static uint32_t s_mdLedNextMs = 0;
static uint32_t s_mdLedPatternStartMs = 0;
static bool     s_mdLedLit = false;
static constexpr uint8_t  kMdLedBlinks  = 2;         // a double blink
static constexpr uint32_t kMdLedOnMs    = 60;
static constexpr uint32_t kMdLedOffMs   = 90;
// Period of the unread reminder, measured start-to-start rather than as a gap
// after the previous blink: the double blink itself runs 300 ms, so timing from
// the end would put the visible cadence at 1.3 s instead of 1 s.
static constexpr uint32_t kMdLedRepeatMs = 1000;
static uint8_t s_mdLedColor = NOTIFY_LED_COLOR_BLUE;
static uint8_t s_mdLedColorFirst = NOTIFY_LED_COLOR_BLUE;
static uint8_t s_mdLedColorSecond = NOTIFY_LED_COLOR_BLUE;

static bool meshDeckLedColorEnabled(uint8_t color) {
    return cfgCoerceNotifyLedColor((int)color) != NOTIFY_LED_COLOR_OFF;
}

static bool meshDeckHasUnreadChannels() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (s_channelNeedsAttention[i]) return true;
    }
    return false;
}

static bool meshDeckLedDualColorEnabled() {
    const uint8_t chColor = cfgCoerceNotifyLedColor((int)s_cfg.notifyLedColorChannel);
    const uint8_t dmColor = cfgCoerceNotifyLedColor((int)s_cfg.notifyLedColorDm);
    // Dual-color only makes sense when both categories are actually unread.
    return meshDeckLedColorEnabled(chColor)
           && meshDeckLedColorEnabled(dmColor)
           && meshDeckHasUnreadChannels()
           && DMs.hasUnread();
}

static void meshDeckLedSetColor(uint8_t color, bool on) {
    const uint8_t c = cfgCoerceNotifyLedColor((int)color);
#if defined(DEVICE_MESH_DECK)
    meshDeckKeyboardSetLedColor(c, on);
#endif
    if (!on || c == NOTIFY_LED_COLOR_OFF) {
        meshDeckLedSet(false, false, false);
        return;
    }

    bool r = false, g = false, b = false;
    switch (c) {
        case NOTIFY_LED_COLOR_RED:     r = true; break;
        case NOTIFY_LED_COLOR_GREEN:   g = true; break;
        case NOTIFY_LED_COLOR_BLUE:    b = true; break;
        case NOTIFY_LED_COLOR_YELLOW:  r = true; g = true; break;
        case NOTIFY_LED_COLOR_CYAN:    g = true; b = true; break;
        case NOTIFY_LED_COLOR_MAGENTA: r = true; b = true; break;
        case NOTIFY_LED_COLOR_WHITE:   r = true; g = true; b = true; break;
        case NOTIFY_LED_COLOR_OFF:     break;
        default:                       b = true; break;
    }
    meshDeckLedSet(r, g, b);
}

static uint8_t meshDeckUnreadColor() {
    const uint8_t dmColor = cfgCoerceNotifyLedColor((int)s_cfg.notifyLedColorDm);
    const uint8_t chColor = cfgCoerceNotifyLedColor((int)s_cfg.notifyLedColorChannel);

    if (DMs.hasUnread() && meshDeckLedColorEnabled(dmColor)) return dmColor;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (s_channelNeedsAttention[i] && meshDeckLedColorEnabled(chColor)) {
            return chColor;
        }
    }
    return NOTIFY_LED_COLOR_OFF;
}

static void meshDeckLedStartPattern(uint8_t color) {
    const uint8_t c = cfgCoerceNotifyLedColor((int)color);
    if (!meshDeckLedColorEnabled(c)) return;

    if (meshDeckLedDualColorEnabled()) {
        // Requested behavior: with both types enabled, double-blink as
        // Channel color first, then DM color.
        s_mdLedColorFirst = cfgCoerceNotifyLedColor((int)s_cfg.notifyLedColorChannel);
        s_mdLedColorSecond = cfgCoerceNotifyLedColor((int)s_cfg.notifyLedColorDm);
    } else {
        // Otherwise both blinks stay on the triggering notification type.
        s_mdLedColorFirst = c;
        s_mdLedColorSecond = c;
    }

    s_mdLedColor = s_mdLedColorFirst;
    s_mdLedPhasesLeft = (uint8_t)(kMdLedBlinks * 2);
    s_mdLedNextMs = 0;                               // fire on the next service
    s_mdLedPatternStartMs = millis();
}

// Called for every new message, channel or DM.
static void meshDeckLedNotify(bool isDm) {
    meshDeckLedStartPattern(isDm ? s_cfg.notifyLedColorDm
                                 : s_cfg.notifyLedColorChannel);
}

// Turning the setting off mid-pattern has to darken the LED now — the service
// loop below returns early once disabled, so it would otherwise leave the light
// stuck in whichever half of a blink it was in.
static void meshDeckLedApplySetting() {
    s_mdLedPhasesLeft = 0;
    s_mdLedLit = false;
    meshDeckLedSetColor(s_mdLedColor, false);
}

// True while any channel or DM still has something unread. Read from the state
// the UI already maintains rather than counting messages separately, so marking
// a channel read stops the LED by the same action that clears its badge.
static bool meshDeckHasUnread() { return meshDeckHasUnreadChannels() || DMs.hasUnread(); }

static void meshDeckServiceLed() {
    const uint8_t unreadColor = meshDeckUnreadColor();
    if (!meshDeckLedColorEnabled(unreadColor)) {
        if (s_mdLedPhasesLeft != 0 || s_mdLedLit) meshDeckLedApplySetting();
        return;
    }

    const uint32_t now = millis();

    if (s_mdLedPhasesLeft == 0) {
        // Idle: re-arm periodically for as long as something is unread. Reading
        // the messages clears the flags above and the reminder simply stops.
        //
        // The timeout is checked only here, in the idle branch, so a pattern
        // already running always finishes and leaves the LED dark — the same
        // way it behaves when a channel is marked read mid-blink.
        if (!lightNotifyWindowOpen()) return;
        if ((uint32_t)(now - s_mdLedPatternStartMs) >= kMdLedRepeatMs) {
            meshDeckLedStartPattern(unreadColor);
        } else {
            return;
        }
    }

    if (s_mdLedNextMs != 0 && (int32_t)(now - s_mdLedNextMs) < 0) return;

    s_mdLedLit = !s_mdLedLit;
    if (s_mdLedLit) {
        // Two blinks total: first ON pulse gets index 0, second gets index 1.
        const uint8_t onPulseIndex = (uint8_t)(((kMdLedBlinks * 2) - s_mdLedPhasesLeft) / 2);
        s_mdLedColor = (onPulseIndex == 0) ? s_mdLedColorFirst : s_mdLedColorSecond;
        meshDeckLedSetColor(s_mdLedColor, true);
    } else {
        meshDeckLedSetColor(s_mdLedColor, false);
    }
    s_mdLedNextMs = now + (s_mdLedLit ? kMdLedOnMs : kMdLedOffMs);
    s_mdLedPhasesLeft--;

    if (s_mdLedPhasesLeft == 0) {
        s_mdLedLit = false;
        meshDeckLedSetColor(s_mdLedColor, false);    // always end dark
    }
}
#endif  // HAS_NOTIFY_LED

#if defined(DEVICE_TANMATSU)
// ── Tanmatsu power and Wi-Fi lights ─────────────────────────────────────────
// Driving the messages LED puts the coprocessor in manual LED mode, which also
// stops it driving the power and radio LEDs. So they are driven here, both at
// the user's Status Lights level: the power LED in the coprocessor's own
// colors, and the radio LED (the one marked with the Wi-Fi glyph) following
// Wi-Fi rather than the C6.
static bool s_tmStatusLedsDirty = true;

// Settings changes call this so the new level shows now rather than at the
// next one-second poll.
static void tanmatsuRefreshStatusLeds() { s_tmStatusLedsDirty = true; }

static uint8_t tanmatsuScaleLed(uint8_t full, uint8_t pct) {
    if (!full || !pct) return 0;
    const uint16_t v = (uint16_t)(((uint32_t)full * pct + 50) / 100);
    return v ? (uint8_t)v : 1;   // a set level never rounds a color away
}

static void tanmatsuServiceStatusLeds() {
    static uint32_t lastMs = 0;
    const uint32_t now = millis();
    if (!s_tmStatusLedsDirty && (uint32_t)(now - lastMs) < 1000) return;
    lastMs = now;
    s_tmStatusLedsDirty = false;

    // Power: blue on battery, orange charging, green charged on USB, red on
    // USB with no battery -- the coprocessor's automatic-mode meanings.
    uint8_t r = 0, g = 255, b = 0;
    bool battery = false, usb = false, charging = false;
    if (tanmatsuCoprocChargeStatus(&battery, &usb, &charging)) {
        if (usb && !battery)  { r = 255; g = 0;   b = 0;   }
        else if (charging)    { r = 255; g = 80;  b = 0;   }
        else if (usb)         { r = 0;   g = 255; b = 0;   }
        else                  { r = 0;   g = 0;   b = 255; }
    }
    const uint8_t pct = cfgCoercePowerLedPct(s_cfg.powerLedPct);
    tanmatsuCoprocSetLed(TANMATSU_LED_POWER, tanmatsuScaleLed(r, pct),
                         tanmatsuScaleLed(g, pct), tanmatsuScaleLed(b, pct));

    // Wi-Fi: green connected, blue in AP (setup) mode, amber while trying to
    // join, dark when off. Same tests as the status bar's Wi-Fi icon.
    const wifi_mode_t mode = WiFi.getMode();
    bool ap = (mode == WIFI_AP);
#ifdef WIFI_AP_STA
    ap = ap || (mode == WIFI_AP_STA);
#endif
    const bool sta = !ap && (mode == WIFI_STA);
    r = g = b = 0;
    if (ap)                                        { b = 255; }
    else if (sta && WiFi.status() == WL_CONNECTED) { g = 255; }
    else if (sta)                                  { r = 255; g = 80; }
    tanmatsuCoprocSetLed(TANMATSU_LED_RADIO, tanmatsuScaleLed(r, pct),
                         tanmatsuScaleLed(g, pct), tanmatsuScaleLed(b, pct));
}
#endif

#if defined(DEVICE_MESH_DECK)
// The front buttons hang off expander 0x59, not GPIO, so they are polled rather
// than interrupt-driven. Per the Attaky docs the right shoulder is R1 = P06 and
// Power = P07. P06 is bound here; P07 is left alone so Power keeps the meaning
// wadamesh gives it, including the long-press hardware power-cut that never
// reaches firmware.
//
// Deliberately not routed through the normal key path: every key there wakes
// the screen as a side effect, so a press would light the panel straight back
// up and the button could never turn it off.
static void meshDeckPollButtons() {
    if (!s_mdButtonsReady) return;

    static uint32_t lastPollMs = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - lastPollMs) < 40) return;   // debounce + bus courtesy
    lastPollMs = now;

    uint8_t p0 = 0xFF;
    if (!s_mdButtons.readPort(0, p0)) return;

    // Same reasoning as the matrix scan: every button reading pressed at once
    // is a bad read, not a hand. Acting on it would inject D-pad keys, and any
    // key wakes the panel — which is how a glitched read while the screen is
    // off becomes a screen that refuses to stay off.
    if (p0 == 0x00) {
        s_mdBtnPrev = 0xFF;   // resync; do not treat the recovery as presses
        return;
    }

    // BTN_R2 (P06) is the screen key: the R button, rightmost of the shoulder
    // pair along the top edge. It takes the same tap/hold rule as every other
    // board's wake button -- which needs the button's *level* every poll, not
    // just its press edge, because the hold has to be timed. Serviced ahead of
    // the edge work below for that reason: a poll with no fresh edges in it is
    // still a poll that has to advance a hold in progress.
    //
    // The hardware Power button (P07) is left alone; see hw_mesh_deck.h for why
    // a hold there is unusable.
    //
    // Handled here rather than folded into the key path below, and returning
    // before it: every key there wakes the panel as a side effect, so a press
    // that became a key would light the display straight back up and the button
    // could never turn it off.
    //
    // An expander pin cannot wake the CPU out of a light-sleep nap, so a press
    // made mid-nap registers when the nap ends rather than immediately. That is
    // why the BOOT button keeps this job as well (see pollUserButton): it is a
    // real GPIO and the board's only input that works while the CPU is down.
    static WakeButtonPoll s_mdScreenBtn;
    const bool screenBtnDown = (p0 & (uint8_t)(1u << BTN_R2_BIT)) == 0;   // active low
    const bool screenBtnActed = serviceWakeButton(s_mdScreenBtn, screenBtnDown, now,
                                                  "BTN_R2");

    // Fresh press edges across the whole port, active low. BTN_R2 is masked out:
    // it has had its say above, and leaving it in would have the edge path act
    // on the same press a second time.
    const uint8_t pressed =
        (uint8_t)(~p0 & (uint8_t)(s_mdBtnPrev) & (uint8_t)~(1u << BTN_R2_BIT));
    s_mdBtnPrev = p0;
    if (screenBtnActed) return;
    if (!pressed) return;

    // The D-pad folds onto the same navigation tokens the keyboard produces, so
    // it drives every screen exactly as j/k and the arrows already do — no
    // per-screen handling needed. These DO go through the key path, so they
    // wake the display like any other key.
    char k = KEY_NONE;
    if      (pressed & (1u << 4)) k = KEY_SCROLL_UP;   // P04 UP
    else if (pressed & (1u << 0)) k = KEY_SCROLL_DN;   // P00 DOWN
    else if (pressed & (1u << 1)) k = KEY_PREV_CHAN;   // P01 LEFT
    else if (pressed & (1u << 3)) k = KEY_NEXT_CHAN;   // P03 RIGHT
    else if (pressed & (1u << 2)) k = KEY_ENTER;       // P02 SELECT
    else if (pressed & (1u << BTN_L1_BIT)) k = KEY_OPEN_HOME;   // top-left: Home
    if (k != KEY_NONE) s_mdPendingKey = k;
}

// Handed to pumpKeyboardInput() so D-pad presses enter the same queue as the
// matrix keys and need no special-casing anywhere downstream.
static char meshDeckReadDpadKey() {
    const char k = s_mdPendingKey;
    s_mdPendingKey = KEY_NONE;
    return k;
}
#endif

#if HAS_KB_BLINK
// ── Keyboard-backlight notification blink ────────────────────────────────────
// Neither of these boards has a notification LED, so a message arriving with the
// screen off left no trace at all. The keyboard backlight is the light they do
// have. T-Deck Pro also pulses while awake; see kbBlinkAllowedNow().
//
// Shape and timing follow the Mesh Deck's RGB LED above deliberately: arm on
// arrival, re-arm every second while anything stays unread, and never block —
// the service call below does the toggling, because a blocking blink would stall
// the UI and the radio for the length of the pattern.
//
// Colour is what separates a channel message from a DM on the Mesh Deck; here
// the only thing to vary is the count, so each kind carries its own (1..3,
// defaulting to the 1 and 2 that were hardcoded before they were settings).
// Three flashes at 60/90 ms still finish well inside kKbBlinkRepeatMs.
static constexpr uint32_t kKbBlinkOnMs     = 60;
static constexpr uint32_t kKbBlinkOffMs    = 90;
// Start-to-start, matching kMdLedRepeatMs: measured from the end, a two-blink DM
// pattern would drift to a slower cadence than a one-blink channel pattern.
static constexpr uint32_t kKbBlinkRepeatMs = 1000;
static constexpr uint8_t  kKbBlinkDuty     = 255;

static uint8_t  s_kbBlinkPhasesLeft = 0;
static uint32_t s_kbBlinkNextMs = 0;
static uint32_t s_kbBlinkPatternStartMs = 0;
static bool     s_kbBlinkLit = false;
static uint32_t s_kbBlinkActivityAnchorMs = 0;
static bool     s_kbBlinkDismissedByActivity = true;

static bool kbBlinkRestingLit() {
#if defined(DEVICE_TDECK_PRO)
    return !s_screenAsleep && tdeckProKeyboardBacklightEnabled() && !kbLightIdleDark();
#else
    return false;
#endif
}

// Pro supports an explicit Alt+B resting state, so pulse away from that state
// even while awake. The other boards retain their screen-off-only behavior.
static bool kbBlinkAllowedNow() {
#if defined(DEVICE_TDECK_PRO)
    return true;
#else
    return s_screenAsleep;
#endif
}

static void kbBlinkSetLit(bool lit) {
    s_kbBlinkLit = lit;
#if defined(DEVICE_TDECK)
    // Unlit is the user's resting level, not 0. This board used to rest dark
    // with no say in it, so "off" was the only thing the dark half of a blink
    // could mean; now that the keyboard has a level of its own, a blink has to
    // pulse away from it and land back on it, or every notification would leave
    // the keyboard darker than the user set it.
    // Dark rather than the level while the idle timeout has it out.
    tdeckKeyboardSetBacklight(lit ? kKbBlinkDuty
                                  : (kbLightIdleDark() ? 0 : s_cfg.kbBacklightLevel));
#elif defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    // Raw: the lit half has to flash even while the idle timeout has the
    // keyboard dark, and the unlit half is whatever kbBlinkRestingLit() says.
    kbLightDrive(lit);
#else
    LV_UNUSED(lit);
#endif
}

static bool kbBlinkHasUnreadChannels() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (s_channelNeedsAttention[i]) return true;
    }
    return false;
}

// Read from the state the UI already maintains rather than counting messages
// separately, so marking a channel read stops the blink by the same action that
// clears its badge.
static bool kbBlinkHasUnread() { return kbBlinkHasUnreadChannels() || DMs.hasUnread(); }

// DMs win when both are pending: the longer pattern is the one worth noticing,
// and it is the same precedence meshDeckUnreadColor() applies. Both counts are
// user-settable (1..3); coerced here so a bad stored value cannot run the
// pattern forever or make it vanish.
static uint8_t kbBlinkCount(bool isDm) {
    return cfgCoerceKbFlashes((int)(isDm ? s_cfg.kbBlinkDmFlashes : s_cfg.kbBlinkChanFlashes));
}

static void kbBlinkStartPattern(bool isDm) {
    if (!s_cfg.kbBlinkEnabled || !kbBlinkAllowedNow()) return;
    if (s_kbBlinkPhasesLeft == 0) s_kbBlinkLit = kbBlinkRestingLit();
    s_kbBlinkPhasesLeft = (uint8_t)(kbBlinkCount(isDm) * 2);
    s_kbBlinkNextMs = 0;                             // fire on the next service
    s_kbBlinkPatternStartMs = millis();
}

// Called for every new message, channel or DM.
static void kbBlinkNotify(bool isDm) {
    s_kbBlinkActivityAnchorMs = s_lastActivityMs;
    s_kbBlinkDismissedByActivity = false;
    kbBlinkStartPattern(isDm);
}

// Ends the pattern and puts the backlight back where it belongs. Called whenever
// a pattern is cut short rather than run out: the screen waking, or the setting
// being switched off mid-blink — without this the light would stay stuck in
// whichever half of a blink it was in.
//
// Who "puts it back" differs. T-Deck rests dark, T-Deck Pro restores the Alt+B
// state, and Pager leaves the pin to sleepScreen()/wakeScreen().
static void kbBlinkRelease(bool forceDark) {
    s_kbBlinkPhasesLeft = 0;
#if defined(DEVICE_TLORA_PAGER_TFT)
    LV_UNUSED(forceDark);
    s_kbBlinkLit = false;
#elif defined(DEVICE_TDECK_PRO)
    LV_UNUSED(forceDark);
    kbBlinkSetLit(kbBlinkRestingLit());
#else
    if (s_kbBlinkLit || forceDark) kbBlinkSetLit(false);
#endif
}

static void kbBlinkApplySetting() {
    kbBlinkRelease(!s_cfg.kbBlinkEnabled);
}

// True only while a pattern is mid-flight. The nap gate consults this so a
// 1500 ms light sleep cannot land inside a 60 ms blink and stretch it into a
// glow; between patterns the device is free to nap as usual.
static bool kbBlinkBusy() { return s_kbBlinkPhasesLeft != 0; }

static void serviceKbBlink() {
    if (!s_cfg.kbBlinkEnabled) {
#if defined(DEVICE_TDECK_PRO)
        if (s_kbBlinkPhasesLeft != 0 || s_kbBlinkLit != kbBlinkRestingLit()) {
            kbBlinkApplySetting();
        }
#else
        if (s_kbBlinkPhasesLeft != 0 || s_kbBlinkLit) kbBlinkApplySetting();
#endif
        return;
    }

    if (!s_kbBlinkDismissedByActivity
        && s_lastActivityMs != s_kbBlinkActivityAnchorMs) {
        s_kbBlinkDismissedByActivity = true;
        kbBlinkRelease(false);
    }
    if (s_kbBlinkDismissedByActivity) return;

    if (!kbBlinkAllowedNow()) {
        // Screen just woke mid-pattern. On the T-Deck that leaves a lit keyboard
        // nobody else will turn off, so kbBlinkRelease() darkens it; on the Pager
        // wakeScreen() has already set the awake state and it stays out of the
        // way. Either way the pattern ends here rather than resuming later.
        kbBlinkRelease(false);
        return;
    }

    const uint32_t now = millis();

    if (s_kbBlinkPhasesLeft == 0) {
        // Idle: re-arm periodically for as long as something is unread. Reading
        // the messages clears the flags and the reminder simply stops.
        //
        // The timeout is checked only here, in the idle branch, so a pattern
        // already running always finishes and restores the resting state.
        if (!kbBlinkHasUnread()) return;
        if (!lightNotifyWindowOpen()) return;
        if ((uint32_t)(now - s_kbBlinkPatternStartMs) < kKbBlinkRepeatMs) return;
        kbBlinkStartPattern(DMs.hasUnread());
    }

    if (s_kbBlinkNextMs != 0 && (int32_t)(now - s_kbBlinkNextMs) < 0) return;

    kbBlinkSetLit(!s_kbBlinkLit);
    s_kbBlinkNextMs = now + (s_kbBlinkLit ? kKbBlinkOnMs : kKbBlinkOffMs);
    s_kbBlinkPhasesLeft--;

    if (s_kbBlinkPhasesLeft == 0) {
#if defined(DEVICE_TDECK_PRO)
        kbBlinkSetLit(kbBlinkRestingLit());
#elif !defined(DEVICE_TLORA_PAGER_TFT)
        if (s_kbBlinkLit) kbBlinkSetLit(false);
#endif
    }
}
#endif  // HAS_KB_BLINK

#if defined(DEVICE_TDECK_PRO)
static void toggleTdeckProKeyboardBacklight() {
    s_cfg.kbBacklightEnabled = !s_cfg.kbBacklightEnabled;
    // Saved on the toggle, not at shutdown: this board has no clean shutdown --
    // it is put down, or the battery runs out -- so anything not written here
    // is not written at all.
    markConfigDirty();
#if HAS_KB_BLINK
    kbBlinkRelease(false);
#else
    setPagerKeyboardBacklight(!s_screenAsleep && tdeckProKeyboardBacklightEnabled());
#endif
    Serial.printf("[kb-bl] T-Deck Pro %s\n",
                  tdeckProKeyboardBacklightEnabled() ? "on" : "off");
}
#endif

#if HAS_KB_LIGHT_TIMEOUT
// Puts the keyboard light where it rests right now, given the screen, the
// board's own on/off control and the idle timeout. Only ever called on a change
// of the idle state; everything else that moves the light already does so.
static void kbLightApplyResting() {
#if defined(DEVICE_TDECK)
    // Rests at its level whether or not the screen is on; see applyKbBacklightSetting().
    keyboardSetKeypadBacklight(kbLightIdleDark() ? 0 : s_cfg.kbBacklightLevel);
#else
    bool screenWants = !s_screenAsleep;
#if FEATURE_LOCK_SCREEN
    if (s_lockScreenActive) screenWants = false;   // nothing on it takes typing
#endif
#if defined(DEVICE_TDECK_PRO)
    screenWants = screenWants && tdeckProKeyboardBacklightEnabled();   // Alt+B
#endif
    setPagerKeyboardBacklight(screenWants);   // P4: F7 is applied in there
#endif
}

// Once per loop: puts the keyboard light out after kbLightTimeoutS with no
// input, and back on with the next key or touch. "Input" is s_lastActivityMs,
// the same clock the screen timeout runs on, so the two agree on what counts.
// Acts on edges only, so it costs a subtraction a pass and never fights the
// paths that move the light for their own reasons.
static void serviceKbLightTimeout() {
    const uint32_t secs = s_cfg.kbLightTimeoutS;
    const bool dark = (secs != 0)
                      && (uint32_t)(millis() - s_lastActivityMs) >= secs * 1000UL;
    if (dark == s_kbLightIdleDark) return;
    s_kbLightIdleDark = dark;
    Serial.printf("[kb-bl] idle timeout: %s\n", dark ? "dark" : "lit");
#if HAS_KB_BLINK
    // A pattern mid-flight ends on kbBlinkRestingLit(), which reads the new
    // state; changing the light under it would only cut a flash short.
    if (kbBlinkBusy()) return;
#endif
    kbLightApplyResting();
}
#endif  // HAS_KB_LIGHT_TIMEOUT

static void wakeScreen() {
    if (!s_screenAsleep) {
        s_lastActivityMs = millis();
        return;
    }

    // Full clock first: the repaint that follows on this same loop pass should
    // run at speed.
    applyCpuMhz(kCpuMhzActive);

#if defined(DEVICE_TDECK_PRO)
    hideTdeckProSleepClock();
#endif

    // SLPOUT, then straight back to a dark backlight — wakeup() restores the
    // brightness LGFX cached, which would expose the stale frame the controller
    // kept. serviceBacklightWake() lights it after the repaint lands. The
    // settle wait is blocking because sending pixels before the controller is
    // ready is what produces corrupt wakes; ~120 ms once per wake is the cost.
    displayDev().wakeup();
    displayDev().setBrightness(0);
    // The touch controller wakes on its own bus, so the time its handshake
    // spends waiting counts against the panel's settle window rather than
    // adding to it.
    const uint32_t panelWakeStartMs = millis();
    setTouchSleep(false);
    const uint32_t settledMs = millis() - panelWakeStartMs;
    if (settledMs < kPanelWakeSettleMs) delay(kPanelWakeSettleMs - settledMs);
    s_backlightPendingOn = true;
    s_preSleepDimmed = false;

    s_screenAsleep = false;
#if defined(DEVICE_TDECK_PRO) && HAS_KB_BLINK
    kbBlinkRelease(false);
#else
    setPagerKeyboardBacklight(true);
#endif
    // After the flag clears, so the conversation on screen reads as visible
    // again. This is also the T-Deck Pro's path out of its sleeping overlay.
    noteUiVisibleAgain();
    s_lastActivityMs = millis();

    // Force repaint after wake so stale text/colors are not shown.
    s_lastRenderedChannel = -1;
    s_lastRenderedCount = -1;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
    s_lastHeaderTime[0] = '\0';
    s_lastBattPct = 255;
    s_lastBattCentiV = 0xFFFF;
    s_lastGpsSats = 255;
#if defined(DEVICE_WIO_TRACKER_L2)
    // Panel_NV3031B::setSleep(false) performs a software reset and reruns the
    // controller init sequence, so panel RAM no longer contains a valid frame.
    // Cache invalidation above only refreshes objects whose data changed; mark
    // the whole LVGL screen dirty while the LP5814 backlight is still dark.
    if (s_rootScreen) lv_obj_invalidate(s_rootScreen);
#endif
    Serial.println("[screen] woke");
}

#if FEATURE_LOCK_SCREEN && (HAS_WAKE_BUTTON || defined(DEVICE_M9))
// Brings a dark panel back up onto the lock screen rather than onto the UI.
// This is the tap half of the wake button's two gestures -- and on the M9, of
// the d-pad centre's, which the controller splits into two key codes for us: glancing at the clock
// and at what has arrived should not cost the user their unlock, and it is the
// most a press made in a pocket can reach.
//
// Deliberately not wakeScreen() followed by enterLockScreen(). That order comes
// up on the UI first and runs noteUiVisibleAgain() on the way, which marks the
// selected channel and any open DM as read -- for a screen the user is not
// being shown, which is the one thing the lock screen exists not to do. The
// panel bring-up below is wakeScreen()'s, line for line; what follows it is not.
static void wakeToLockScreen(const char *reason) {
    if (!s_screenAsleep || s_lockScreenActive) return;

    // Full clock first: the overlay build and repaint that follow on this same
    // loop pass should run at speed.
    applyCpuMhz(kCpuMhzActive);

    // SLPOUT, then straight back to a dark backlight -- the controller still
    // holds the pre-sleep frame of the UI across SLPIN/SLPOUT, and lighting it
    // before the overlay is drawn would flash the chat screen at someone who
    // asked for the lock screen. serviceBacklightWake() lights it after the
    // repaint lands.
    displayDev().wakeup();
    displayDev().setBrightness(0);
    const uint32_t panelWakeStartMs = millis();
    setTouchSleep(false);
    const uint32_t settledMs = millis() - panelWakeStartMs;
    if (settledMs < kPanelWakeSettleMs) delay(kPanelWakeSettleMs - settledMs);

    s_screenAsleep = false;
    s_backlightPendingOn = true;
    s_preSleepDimmed = false;
    // Stays dark: nothing on this screen takes typing.
    setPagerKeyboardBacklight(false);

    // Set before the overlay is built, so that applyBrightness() -- reached
    // through serviceBacklightWake() once the repaint has landed -- resolves to
    // lockScreenBrightness rather than to the UI's own level.
    s_lockScreenActive = true;
    s_lockScreenSinceMs = millis();
    // Same guard enterLockScreen() sets, for the same reason: the press that
    // brought this screen up must not also be read as the input that dismisses
    // it. It does not latch -- a button held through it unlocks the moment it
    // expires rather than needing a second press.
    s_screenWakeBlockedUntilMs = s_lockScreenSinceMs + kScreenWakeInputDelayMs;

    showTdeckProSleepClock();
    // The panel's RAM no longer holds anything valid (on the Wio Tracker L2
    // setSleep(false) reruns the whole controller init), and the overlay is a
    // fresh object tree either way. Mark the lot dirty while the backlight is
    // still down.
    if (s_rootScreen) lv_obj_invalidate(s_rootScreen);

    serviceCpuScaling();

    if (s_cfg.lockScreenOffSecs == LOCK_SCREEN_OFF_NEVER) {
        Serial.printf("[screen] woke to lock screen (%s), staying on\n",
                      reason ? reason : "");
    } else {
        Serial.printf("[screen] woke to lock screen (%s), panel out in %lus\n",
                      reason ? reason : "",
                      (unsigned long)s_cfg.lockScreenOffSecs);
    }
}
#endif  // FEATURE_LOCK_SCREEN && (HAS_WAKE_BUTTON || DEVICE_M9)

// Lights the backlight once the panel has settled and the loop has repainted.
// The flag persists until this runs, so a loop pass that bails out early just
// defers the backlight to the next full pass rather than losing it.
static void serviceBacklightWake() {
    if (!s_backlightPendingOn) return;
#if defined(DEVICE_WIO_TRACKER_L2)
    // LVGL's flush is synchronous today, but wait on the panel bus explicitly
    // before exposing the freshly rebuilt frame if that implementation changes.
    displayDev().waitDMA();
#endif
    s_backlightPendingOn = false;
    applyBrightness();
}

static bool tryWakeScreenFromInput(uint32_t nowMs) {
    if (!s_screenAsleep) {
        s_lastActivityMs = nowMs;
        return true;
    }

    if ((int32_t)(nowMs - s_screenWakeBlockedUntilMs) < 0) {
        return false;
    }

    wakeScreen();
    return true;
}

#if HAS_WAKE_BUTTON
// ── What a board's dedicated wake button means ───────────────────────────────
// Every board with one routes its press through this pair, so the gesture reads
// the same on all of them:
//
//   tap   -- UI in front of the user: put it away, which lands on the lock
//            screen where that is enabled and on a dark panel where it is not.
//            Dark panel: bring the lock screen up. Lock screen already up, or a
//            dark panel with the lock screen disabled: nothing.
//   hold  -- two seconds, from the lock screen and from a dark panel alike:
//            unlock, straight through to the UI.
//
// One rule in every state, so the gesture does not depend on which state the
// user is looking at -- and on a dark panel that is the point, because there is
// nothing there to read it off. The asymmetry is deliberate rather than
// cosmetic: putting the device away is cheap to undo and wants the quick
// gesture, while bringing it back is what a pocket does by accident, and a
// button held for two seconds is the one input a pocket does not produce. A tap
// can never reach the UI, so the worst a pocket press costs is a lit lock
// screen that times itself back out.
//
// This is the rule the Wio Tracker L2 already had; the only board-specific part
// left is how the press is read off the hardware.
//
// Returns true when the press was consumed.
static bool wakeButtonTap(uint32_t nowMs, const char *reason) {
#if FEATURE_LOCK_SCREEN
    if (s_lockScreenActive) {
        // Swallowed. The hold is the only way in, from here and from a dark
        // panel both -- see above.
        return true;
    }
    if (s_screenAsleep) {
        if (!s_cfg.lockScreenEnabled) return true;
        // The post-sleep guard, used for exactly what it is there for: a press
        // that arrives while the panel is on its way out should not turn round
        // and bring it straight back.
        if ((int32_t)(nowMs - s_screenWakeBlockedUntilMs) < 0) return true;
        wakeToLockScreen(reason);
        return true;
    }
#else
    // No lock screen on this board, so a dark panel has nothing to show short
    // of the UI itself, and the UI is what the hold is for.
    if (s_screenAsleep) return true;
#endif
    s_lastActivityMs = nowMs;
    requestScreenOff(reason);
    return true;
}

// Returns true once the hold has been acted on, so the caller can stop
// retrying it. False means "not yet" -- both paths below can refuse while the
// post-sleep input guard is still running, and neither latches when they do, so
// a button kept down takes effect the moment the guard expires rather than
// needing a second press.
static bool wakeButtonHold(uint32_t nowMs, const char *reason) {
    const char *what = (reason && reason[0]) ? reason : "wake button";
#if FEATURE_LOCK_SCREEN
    if (s_lockScreenActive) {
        // Not counted as the input that restarts the screen timeout;
        // exitLockScreen() does that itself.
        (void)tryExitLockScreenFromInput(nowMs, true);
        if (s_lockScreenActive) return false;
        Serial.printf("[screen] %s hold, unlocked\n", what);
        return true;
    }
#endif
    if (s_screenAsleep) {
        if (!tryWakeScreenFromInput(nowMs)) return false;
        Serial.printf("[screen] %s hold, woke panel\n", what);
        return true;
    }

    // Awake and unlocked already, so there is nothing to bring back. Claimed
    // anyway: it keeps the release from being read as a tap -- and the screen
    // put away -- by someone who has simply been resting on the button.
    s_lastActivityMs = nowMs;
    return true;
}

// The debounce on its own. Updates the stable level and returns true when it
// changed on this call. Split out because on the keyboard boards the BOOT
// button doubles as Config's Enter key, which wants the press edge without any
// of the tap/hold meaning -- and must not run a second debounce of its own, or
// the two would disagree about when the button went down.
static bool serviceWakeButtonDebounce(WakeButtonPoll &st, bool pressed,
                                      uint32_t nowMs) {
    if (pressed != st.rawPressed) {
        st.rawPressed = pressed;
        st.changedMs  = nowMs;
        // Counted on the raw edge rather than the debounced one, deliberately.
        // On the boards whose button hangs off an I2C expander the CPU is
        // napping when the press lands, so the first poll after it only ever
        // starts the debounce -- and if the loop were allowed straight back
        // into a nap, the poll that confirms the press would be a full
        // NAP_MAX_MS later, on top of the one the press already waited out.
        // Blocking from the first read collapses that to one poll interval.
        // Bounce costs nothing: it flips the count back and forth over a few
        // milliseconds during which the loop is awake either way.
        if (pressed) s_wakeButtonsHeld++;
        else if (s_wakeButtonsHeld) s_wakeButtonsHeld--;
    }
    if ((uint32_t)(nowMs - st.changedMs) < kWakeButtonDebounceMs) return false;
    if (st.stablePressed == st.rawPressed) return false;
    st.stablePressed = st.rawPressed;
    return true;
}

// Undoes a press the hardware can no longer confirm -- the Wio Tracker L2's
// expander not answering its bus mid-hold. Puts the button back in the up state
// without running either gesture, and hands the CPU its nap back; leaving the
// count standing would disable power saving for as long as the bus stayed
// silent.
#if defined(DEVICE_WIO_TRACKER_L2)
static void wakeButtonForceRelease(WakeButtonPoll &st) {
    if (st.rawPressed && s_wakeButtonsHeld) s_wakeButtonsHeld--;
    st = WakeButtonPoll{};
}
#endif

// Debounces one wake button and tells its two gestures apart. The caller reads
// the raw level however its hardware requires -- a GPIO, an I2C expander port --
// and hands it in; everything downstream of that is shared.
//
// Returns true when this call consumed the input.
static bool serviceWakeButton(WakeButtonPoll &st, bool pressed, uint32_t nowMs,
                              const char *reason) {
    bool acted = false;
    if (serviceWakeButtonDebounce(st, pressed, nowMs)) {
        if (st.stablePressed) {
            st.holdStartMs = nowMs;
            st.claimed = false;
        } else {
            // Release. The tap can only be recognised here: until the button
            // comes up there is no telling it from a hold that has not reached
            // kScreenSleepHoldMs yet.
            const bool wasTap = !st.claimed
                && st.holdStartMs != 0
                && (uint32_t)(nowMs - st.holdStartMs) < kScreenSleepHoldMs;
            st.holdStartMs = 0;
            st.claimed = false;
            if (wasTap) acted = wakeButtonTap(nowMs, reason);
        }
    }

    if (!st.stablePressed || st.claimed) return acted;
    if (st.holdStartMs == 0
        || (uint32_t)(nowMs - st.holdStartMs) < kScreenSleepHoldMs) return acted;

    st.claimed = wakeButtonHold(nowMs, reason);
    return acted || st.claimed;
}

#endif  // HAS_WAKE_BUTTON

static bool serviceTdeckTrackballSleepHold(uint32_t nowMs) {
#if defined(DEVICE_TDECK) && HAS_TRACKBALL && (TBALL_CLICK >= 0)
    const bool pressed = (digitalRead(TBALL_CLICK) == LOW);

    if (pressed) {
        if (!s_tdeckTrackballHoldActive) {
            s_tdeckTrackballHoldActive = true;
            s_tdeckTrackballHoldTriggered = false;
            s_tdeckTrackballHoldStartMs = nowMs;
        }

        if (!s_tdeckTrackballHoldTriggered
            && (uint32_t)(nowMs - s_tdeckTrackballHoldStartMs) >= kScreenSleepHoldMs) {
            s_tdeckTrackballHoldTriggered = true;
            // Ignore any pending click event from this same press.
            s_tdeckSuppressRollerClick = true;
            if (!s_screenAsleep) {
                requestScreenOff("T-Deck trackball hold");
                return true;
            }
        }
    } else {
        s_tdeckTrackballHoldActive = false;
        s_tdeckTrackballHoldTriggered = false;
        s_tdeckSuppressRollerClick = false;
    }
#else
    LV_UNUSED(nowMs);
#endif

    return false;
}

#if defined(DEVICE_WIO_TRACKER_L2)
// The board's Wake button, on the expander beside the touch controller. Reading
// it is all that is board-specific here: what a tap and a hold mean is
// serviceWakeButton()'s, and is shared with every other board that has one.
static bool serviceWioTrackerL2WakeButton(uint32_t nowMs) {
    static WakeButtonPoll btn;
    static uint32_t nextReadMs = 0;

    const bool interruptActive = digitalRead(EXPANDER_INT) == LOW;
    if (!btn.rawPressed && !btn.stablePressed && !interruptActive) return false;
    if ((int32_t)(nowMs - nextReadMs) < 0) return false;
    nextReadMs = nowMs + 15;

    bool pressed = false;
    if (!wioTrackerL2IoReadWakeButton(pressed)) {
        // No reading to time a hold against, and keeping the CPU up on a bus
        // that is not answering would disable power saving for as long as it
        // stayed unanswered. Let the nap back in.
        wakeButtonForceRelease(btn);
        return false;
    }

    return serviceWakeButton(btn, pressed, nowMs, "Wio Tracker L2 Wake button");
}
#endif

// The last clause is what homeShouldOpenChannelList() needs. Not plain
// HAS_HOME_DASHBOARD: the Pager has a dashboard but keeps its channel list
// anchored beside the chat, so it has no caller for this and would only get an
// unused-function warning out of it.
#if UI_TOUCH_ONLY_PROFILE || defined(DEVICE_M9) \
    || (HAS_HOME_DASHBOARD && UI_CHANNEL_LIST_DROPDOWN)
// True when the chat screen is what the user is looking at — nothing floating
// over it. Buttons need this in a way taps do not: a tap lands on whatever is
// actually on top, while a button press has to work out for itself who it is
// for. The USER button asks so it knows whether to open the composer; the Home
// gesture asks so it knows whether it has anywhere left to go.
//
// An explicit list rather than a flag, because the modals do not share one. It
// has to name everything that can be up while the three full-screen views
// (Config, DM, Nodes) are all closed — those are excluded by the caller, which
// checks them first. The Config screen's own sub-modals ride on s_cfgModal and
// need no entry here.
//
// A modal added later that forgets to appear in this list costs one thing: the
// USER button opens the composer over it. Nothing crashes, and it is visible
// the first time anyone presses the button on that screen.
static bool chatScreenIsForeground() {
#if HAS_STATE_MAPS
    if (s_legacyMapPromptModal) return false;
#endif
#if HAS_HOME_DASHBOARD
    // Not a modal, but it covers chat all the same, and the chat button's
    // second press must not open the channel list from behind it.
    if (s_homeDash) return false;
#endif
    return !s_cfgModal && !s_dmModal && !s_nodesModal && !s_liveModal
        && !s_composeModal && !s_emojiPickerModal && !s_legendModal
        && !s_channelActionsModal && !s_nodesActionModal && !s_tracerouteModal
        && !s_releaseNotesModal && !s_onboardingModal && !s_sysStatsModal
        && !s_nodeInfoModal && !s_cfgActionMsgModal
#if HAS_MESSAGE_ACTIONS
        && !s_msgInfoModal
#endif
        ;
}
#endif

static bool pollUserButton(uint32_t nowMs) {
#if defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)
#if !UI_TOUCH_ONLY_PROFILE || defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_MESH_DECK)
    // Off the touch-only boards this button *is* the screen button, so it gets
    // the shared tap/hold rule rather than acting on a bare press edge. Its one
    // extra job -- standing in for Enter on the Config screen -- is checked
    // first and only while the UI is actually in front of the user, which is
    // the only state in which that meaning exists.
    {
        static WakeButtonPoll screenBtn;
        const bool pressed =
            (digitalRead(USER_BUTTON_PIN) == USER_BUTTON_ACTIVE_LEVEL);
        const bool uiIsUp = !s_screenAsleep
#if FEATURE_LOCK_SCREEN
            && !s_lockScreenActive
#endif
            ;
#if !UI_TOUCH_ONLY_PROFILE
        if (uiIsUp && s_cfgModal) {
            // Edge-triggered, as it always was: Config's Enter is a press, not
            // a hold. Fed through the same debounce so the two cannot disagree
            // about when the button went down.
            const bool wasDown = screenBtn.stablePressed;
            (void)serviceWakeButtonDebounce(screenBtn, pressed, nowMs);
            if (!wasDown && screenBtn.stablePressed) {
                activateCfgSelection();
                return true;
            }
            return screenBtn.stablePressed;
        }
#elif defined(DEVICE_MESH_DECK)
        // Second from the left along the top edge, so with the UI up it is the
        // Chat key beside L1's Home: one press goes to chat, a second on chat
        // opens the channel list. Screen-off is R1's job alone. With the panel
        // dark or locked it keeps the shared tap/hold rule below -- it is the
        // board's only input that can wake the CPU out of a nap.
        if (uiIsUp) {
            const bool wasDown = screenBtn.stablePressed;
            (void)serviceWakeButtonDebounce(screenBtn, pressed, nowMs);
            if (!wasDown && screenBtn.stablePressed) s_mdPendingKey = KEY_OPEN_CHAT_BTN;
            return screenBtn.stablePressed;
        }
#else
        // The T-Display P4 (HAS_WAKE_BUTTON, board.h): lock and unlock only,
        // exactly as the Wio Tracker L2's Wake button. Config is tapped here,
        // so it has no use for a hardware Enter.
        (void)uiIsUp;
#endif
        if (serviceWakeButton(screenBtn, pressed, nowMs, "BOOT button")) return true;
        // Held, or resting: nothing else on this board may act on it.
        if (screenBtn.stablePressed) return true;
    }
#else
    static bool userBtnRawPrev = false;
    static bool userBtnStable = false;
    static uint32_t userBtnDebounceMs = 0;

    bool userPressed = (digitalRead(USER_BUTTON_PIN) == USER_BUTTON_ACTIVE_LEVEL);
    if (userPressed != userBtnRawPrev) {
        userBtnRawPrev = userPressed;
        userBtnDebounceMs = nowMs;
    }

    if ((nowMs - userBtnDebounceMs) >= 30 && userPressed != userBtnStable) {
        userBtnStable = userPressed;
        if (userBtnStable) {
#if FEATURE_LOCK_SCREEN
            if (s_lockScreenActive) {
                // On touch-only boards GPIO0 is the UI action button, not the
                // display toggle, so it is not a wake gesture and the press is
                // swallowed like any other input the lock screen eats.
                (void)tryExitLockScreenFromInput(nowMs, false);
                return true;
            }
#endif
            // Consume the press as a wake before inspecting the hidden UI.
            // The held-button fallback below retries after the wake-input guard.
            if (s_screenAsleep) {
                (void)tryWakeScreenFromInput(nowMs);
                return true;
            }
            if (s_cfgModal) {
                activateCfgSelection();
                return true;
            }
            // Innermost surface first. Each entry is the same thing its screen
            // activates on a tap — this button is the Enter key's stand-in on a
            // build that has no Enter key, so the two must not disagree about
            // what "activate" means anywhere.
            if (s_emojiPickerModal) {
                emojiPickerActivate(s_emojiPickerSelection);
                return true;
            }
            // The key review stacks over the Node Actions menu: a press must not
            // fire an action hidden behind it, nor pick Accept/Reject blind.
            // Answered by tap only here.
            if (s_dmDelModal && s_destructiveConfirmAction == DESTRUCTIVE_CONFIRM_KEY_CHANGE) {
                return true;
            }
            if (s_nodesActionModal) {
                executeNodesActionSelection();
                return true;
            }
            if (s_channelActionsModal) {
                // Mute, which is what Enter does here on the keyboard builds and
                // what the modal's first button does on a tap. Location is the
                // other button and stays tap-only: there is one button press to
                // spend and muting is the one people come here for.
                toggleActiveChannelMute();
                return true;
            }
            if (s_dmModal && s_dmNodePickerModal && !s_composeModal) {
                activateDmNodePickerSelection();
                return true;
            }
            if (s_dmModal && !s_dmNodePickerModal && !s_composeModal) {
                activateDmSelection();
                return true;
            }
            if (s_nodesModal && !s_composeModal && !s_nodesActionModal && !s_tracerouteModal) {
                openNodesActionMenu();
                return true;
            }
            // Nothing over the chat screen: compose, which is what its New
            // Message button does and what Enter does on the keyboard builds.
            if (chatScreenIsForeground()) {
                chatComposeFromButton();
                return true;
            }
        }
    }

    if (userBtnStable && s_screenAsleep) {
        if (!tryWakeScreenFromInput(nowMs)) {
            return true;
        }
        return true;
    }
#endif  // !UI_TOUCH_ONLY_PROFILE || DEVICE_TDISPLAY_P4 || DEVICE_MESH_DECK
#endif  // USER_BUTTON_PIN

#if defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)
    {
        static WakeButtonPoll displayBtn;
        const bool pressed = (digitalRead(DISPLAY_TOGGLE_BUTTON_PIN)
                              == DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL);
        if (serviceWakeButton(displayBtn, pressed, nowMs, "GPIO35 button")) return true;
        if (displayBtn.stablePressed) return true;
    }
#endif

#if !((defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)) || (defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)))
    LV_UNUSED(nowMs);
#endif

    return false;
}

// ── Settings storage: one blob, not ~80 keys ─────────────────────────────────
// Every NVS key costs at least one 32-byte entry, and a rewrite spends fresh
// entries for each until a page can be compacted. On the 16 KB partition a
// third-party flash layout hands us, ~80 config keys plus 48 channel keys came
// to 169 entries against the ~73 that are actually writable (NVS keeps a whole
// page in reserve for compaction), so a full save could not be placed at all.
// Packed as blobs the same data is ~50 entries, and a save costs the same.
//
// Fields may only be APPENDED to RhinoConfig: a shorter stored blob is copied
// into the front of the struct and anything newer keeps its compiled default.
// Reordering or removing a field means bumping kCfgBlobVersion, which discards
// older blobs and resets settings to defaults.
static constexpr const char *kCfgBlobKey     = "cfg";
static constexpr uint16_t    kCfgBlobVersion = 1;

struct CfgBlobHeader {
    uint16_t version;
    uint16_t bytes;      // RhinoConfig bytes following the header
};

// One buffer shared by the save and load paths. Static rather than stack because
// serviceConfigFlush() saves from the main loop, on the same 8 KB task stack LVGL
// renders from; shared rather than one each because this board has little static
// RAM to spare. Only the UI thread touches it, and never re-entrantly.
static uint8_t s_cfgBlobBuf[sizeof(CfgBlobHeader) + sizeof(RhinoConfig)];

// ── Remembered WiFi networks ─────────────────────────────────────────────────
// Its own blob rather than fields on RhinoConfig: 5 x 128 bytes would grow the
// settings blob by two-thirds, and that one is rewritten on every settings
// change while this list only moves when a network is added, replaced or
// deleted. Same versioned-header shape as the config blob, so a build whose
// layout we cannot interpret falls back to "no remembered networks" rather than
// reading someone else's bytes as SSIDs.
static constexpr const char *kWifiNetsBlobKey     = "wifiNets";
static constexpr uint16_t    kWifiNetsBlobVersion = 1;

struct WifiNetsBlobHeader {
    uint16_t version;
    uint16_t count;      // KnownWifiEntry records following the header
};

