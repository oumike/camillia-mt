// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: chat view bubbles and rendering.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// ──────────────────────────────────────────────────────────────────────────
// Chat / DM date markers
//
// A horizontal divider with a date label ("--- June 24th 2026 ---") is
// inserted above the first message that lands on a new local-calendar day.
// Each stored message snapshots its wall-clock epoch when it arrives; if
// epoch is 0 (clock not yet synced when message was received, or loaded from
// legacy persistence) no marker is drawn for that message.
// ──────────────────────────────────────────────────────────────────────────
static uint32_t chatDateBucket(uint32_t epoch) {
    if (epoch < 1700000000) return 0;
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    // year*512 + yday is a cheap monotonically-unique key per local calendar day.
    return (uint32_t)((tmv.tm_year + 1900) * 512 + tmv.tm_yday);
}

// The clock for a stored message, in whichever format liveBuildPrefix() is
// currently writing, and sharing its 1700000000 sentinel for "clock was not
// synced when this arrived". Empty output means no usable time, and callers
// drop the field rather than print a placeholder inside a bubble.
//
// The fallback for a stored line that carries no prefix to parse; the bubble
// renderer prefers the prefix, so most lines keep the format they were written
// in. Only lines reaching this path follow a later change of the setting.
static void formatChatClock(uint32_t epoch, char *out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    if (epoch < 1700000000) return;
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    liveFormatClock(tmv, out, len);
}

static void formatChatDateLabel(uint32_t epoch, char *out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    if (epoch < 1700000000) return;
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    static const char *kMonths[12] = {
        TR("January"), TR("February"), TR("March"), TR("April"), TR("May"), TR("June"),
        TR("July"), TR("August"), TR("September"), TR("October"), TR("November"), TR("December")
    };
    int day = tmv.tm_mday;
    const char *suf = "th";
    int mod100 = day % 100;
    int mod10 = day % 10;
    if (!(mod100 >= 11 && mod100 <= 13)) {
        if (mod10 == 1) suf = "st";
        else if (mod10 == 2) suf = "nd";
        else if (mod10 == 3) suf = "rd";
    }
    int monIdx = tmv.tm_mon;
    if (monIdx < 0 || monIdx > 11) monIdx = 0;
    // English reads "September 25th 2026". Every other language puts the day
    // first and has no ordinal suffix, and TR() cannot reorder arguments, so
    // those get a format of their own ("25 de septiembre de 2026").
    if (i18nGetLang() != LANG_EN) {
        snprintf(out, len, TR("%d %s %d"), day, kMonths[monIdx], 1900 + tmv.tm_year);
        return;
    }
    snprintf(out, len, "%s %d%s %d",
             kMonths[monIdx], day, suf, 1900 + tmv.tm_year);
}

static void insertChatDateMarker(lv_obj_t *parent, uint32_t epoch,
                                 const lv_font_t *font) {
    if (!parent || !font) return;
    char dateBuf[40];
    formatChatDateLabel(epoch, dateBuf, sizeof(dateBuf));
    if (!dateBuf[0]) return;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(row, 3, 0);
    lv_obj_set_style_pad_bottom(row, 3, 0);
    lv_obj_set_style_pad_left(row, 0, 0);
    lv_obj_set_style_pad_right(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_height(left, 1);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x6F8FBF), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_70, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(lbl, dateBuf);

    lv_obj_t *right = lv_obj_create(row);
    lv_obj_remove_style_all(right);
    lv_obj_set_height(right, 1);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x6F8FBF), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_70, 0);
}

// ── Bubble chat style helpers ────────────────────────────────────────────────
// Stable per-node bubble color: a curated categorical palette indexed by a hash
// of the node id, so a given node always maps to the same color across reboots.
static uint16_t nodeBubbleColor565(uint32_t nodeId) {
    static const uint8_t pal[][3] = {
        { 41,128,185}, { 39,174, 96}, {142, 68,173}, {211, 84,  0},
        { 22,160,133}, {192, 57, 43}, {127,140,141}, {212, 84,140},
        {243,156, 18}, { 52, 73, 94},
    };
    const int n = (int)(sizeof(pal) / sizeof(pal[0]));
    // The salt is what "Reset Chat Colors" changes. It defaults to 0, which
    // leaves the mapping exactly as it was before the salt existed.
    uint32_t h = (nodeId ^ s_cfg.chatColorSalt) * 2654435761u;
    const uint8_t *c = pal[(h >> 24) % n];
    return chatColorForTheme(rgb565(c[0], c[1], c[2]));
}

// Pick black or white body text for legibility on a given bubble background,
// by luminance — keeps text readable in both light and dark themes since the
// bubble color (not the theme) drives contrast.
static lv_color_t bubbleTextColor(uint16_t bg565) {
    uint8_t r = (uint8_t)(((bg565 >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((bg565 >>  5) & 0x3F) << 2);
    uint8_t b = (uint8_t)((bg565 & 0x1F) << 3);
    uint32_t lum = (299u * r + 587u * g + 114u * b) / 1000u;
    return lum > 145 ? lv_color_hex(0x101010) : lv_color_hex(0xF2F5FF);
}

// A chat message's prefix ("<icon>  HH:MM [name] " for received, "HH:MM<me> "
// for our own) is stripped for bubble rendering — the sender is shown as a name
// tag and color instead. Only the leading prefix window is scanned so body text
// containing "] " or "> " isn't cut.
// Splits a stored line's prefix into its transport icon and its clock — the two
// things classic draws and bubbles were dropping.
//
// The prefix is built at store time and is the only place this information
// survives: appendRxText() writes "<icon>  HH:MM [Name] " (the icon says radio
// vs MQTT bridge), our own sends write "HH:MM <me> ", and the Cardputer build
// omits the icon entirely. DisplayLine::epoch is a separate, later field that is
// 0 for anything stored before it existed or loaded from a legacy persistence
// format — which is why sourcing bubble timestamps from epoch alone showed
// nothing on lines classic happily timestamps.
//
// The clock is found by pattern rather than position, since the icon precedes
// it. The scan is bounded to the prefix window so a "12:34" inside message text
// cannot be mistaken for one.
//
// Both clock formats have to be recognised here, and not only the one currently
// selected: lines are stored with the prefix that was in force when they
// arrived, so a transcript spanning a settings change carries both.
//
// liveFindClock() does the recognising, shared with the live feed's own splitter
// so the two can never disagree about what a stored prefix looks like.
static void chatParsePrefix(const char *line,
                            char *iconOut, size_t iconLen,
                            char *timeOut, size_t timeLen) {
    if (iconOut && iconLen) iconOut[0] = '\0';
    if (timeOut && timeLen) timeOut[0] = '\0';
    if (!line) return;

    const int kWindow = 24;   // icon + clock always lands well inside this
    const char *found = nullptr;
    const char *clockEnd = liveFindClock(line, kWindow, &found);
    if (!clockEnd || !found) return;

    const size_t clockLen = (size_t)(clockEnd - found);
    if (timeOut && timeLen > clockLen) {
        memcpy(timeOut, found, clockLen);
        timeOut[clockLen] = '\0';
    }

    // Whatever sits ahead of the clock is the transport icon (empty when the
    // build or the message type has none). Copied as raw bytes: these are
    // multi-byte UTF-8 symbol glyphs from the font.
    if (iconOut && iconLen) {
        size_t n = (size_t)(found - line);
        while (n > 0 && line[n - 1] == ' ') n--;   // drop the spacer before the time
        if (n > 0 && n < iconLen) {
            memcpy(iconOut, line, n);
            iconOut[n] = '\0';
        }
    }
}

// Where the classic style's ACK marker goes: directly after the clock, where it
// reads as part of the line's own metadata ("12:34 [A] [Name] body") instead of
// as a word appended to whatever the sender happened to end on. On a message
// long enough to wrap, the end of the line is also the worst place for it: it
// lands alone on the last row, far from the message it belongs to.
//
// Same clock scan chatParsePrefix() uses, widened to accept the "--:--"
// placeholder liveBuildPrefix() writes before the clock is set — that line has
// a prefix like any other, and the marker should not move because the time is
// unknown. A line with no clock at all (the bare "! TX failed" notices) has
// nothing to sit beside, so the marker goes to the end there.
static size_t chatAckMarkerOffset(const char *line) {
    if (!line) return 0;
    const int kWindow = 24;   // icon + clock always lands well inside this
    // A real clock first, in either format, so the marker lands after the
    // meridiem rather than between "2:34" and its "PM".
    if (const char *end = liveFindClock(line, kWindow, nullptr)) {
        return (size_t)(end - line);
    }
    // liveFindClock() deliberately does not match "--:--" — it is not a
    // time — and only this marker cares where that placeholder ends.
    for (const char *p = line; *p && (int)(p - line) < kWindow; p++) {
        if (p[0] == '-' && p[1] == '-' && p[2] == ':'
            && p[3] == '-' && p[4] == '-') {
            return (size_t)(p + 5 - line);
        }
    }
    return strlen(line);
}

// The ACK marker, in every chat style: "[A]", in a face well under the chat
// text's so it reads as a note on the message rather than part of it.
static const char kChatAckMarker[] = "[A]";
static const lv_font_t *chatAckMarkerFont(const lv_font_t *chatFont) {
    const int lh = chatFont ? (int)lv_font_get_line_height(chatFont) : 0;
    if (lh >= 30) return &lv_font_montserrat_18;   // 28 and 32
    if (lh >= 21) return &lv_font_montserrat_12;   // 18 and 20
    return &lv_font_montserrat_10;                 // 16 and under
}

// Where a classic line's sender tag sits: "[Name]" on a received line, "<me>"
// on our own -- the bracketed field that ends the prefix, found the way
// chatStripPrefix() finds the prefix's end, brackets included. False when the
// line has no tag (notices, prefix-less lines).
static bool chatFindNameTag(const char *line, size_t *from, size_t *to) {
    if (!line) return false;
    const int kWindow = 64;   // chatStripPrefix()'s, for the same long names
    for (const char *p = line; *p && (int)(p - line) < kWindow; p++) {
        if (!((p[0] == ']' || p[0] == '>') && p[1] == ' ')) continue;
        const char open = (p[0] == ']') ? '[' : '<';
        for (const char *q = p; q > line; ) {
            q--;
            if (*q == open) {
                *from = (size_t)(q - line);
                *to = (size_t)(p + 1 - line);
                return true;
            }
        }
        return false;
    }
    return false;
}

// The new-message dot's colour: the theme accent, black on paper.
static lv_color_t freshDotColor() {
#if defined(DEVICE_TDECK_PRO)
    return lv_color_make(0, 0, 0);
#else
    return lvColorFrom565(s_ui.accent);
#endif
}

// A third of the line, so it reads as a mark beside the text and not a glyph.
static int freshDotSize(const lv_font_t *font) {
    const int lh = font ? (int)lv_font_get_line_height(font) : 14;
    return constrain(lh / 3, 5, 12);
}

static lv_obj_t *makeFreshDot(lv_obj_t *parent, int d) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, d, d);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, freshDotColor(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    return dot;
}

static void chatAddLineSpan(lv_obj_t *sg, const char *text, size_t n, const lv_font_t *font) {
    if (n == 0) return;
    char buf[512];
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, text, n);
    buf[n] = '\0';
    lv_span_t *sp = lv_spangroup_add_span(sg);
    if (font) lv_style_set_text_font(lv_span_get_style(sp), font);
    lv_spangroup_set_span_text(sg, sp, buf);
}

// Fills a classic message drawn as a span group: the line as one paragraph,
// with the small ACK marker spliced in after its clock when ackMark is set, and
// the sender tag in nameFont when one is given (the T-Deck Pro's bold names). A
// span group rather than a label, because a label draws in one face. The spans
// set no colour of their own, so the whole line, marker included, takes the
// group's -- except the new-message dot, when outFreshDot asks for one: it goes
// between the clock and the sender tag, in the accent, and *outFreshDot is the
// span (null on a line with no tag to put it in front of).
static void chatSetLineSpans(lv_obj_t *sg, const char *line, const lv_font_t *chatFont,
                             bool ackMark, const lv_font_t *nameFont,
                             lv_span_t **outFreshDot) {
    if (outFreshDot) *outFreshDot = nullptr;
    if (!sg) return;
    // Folded before the split, so the offsets are measured in the bytes drawn.
    // The clock is ASCII and the fold leaves it where it was.
    char text[512];   // a merged channel message is 384, a DM line under that
    renderEmojiSafeText(line ? line : "", text, sizeof(text));
    const size_t len = strlen(text);

    size_t pos = 0;
    if (ackMark) {
        const size_t at = chatAckMarkerOffset(text);
        chatAddLineSpan(sg, text, at, nullptr);
        lv_span_t *mark = lv_spangroup_add_span(sg);
        lv_style_set_text_font(lv_span_get_style(mark), chatAckMarkerFont(chatFont));
        // A space either side, in the marker's own small face: the line's
        // spacing around it then does not depend on which side of it the text
        // had one.
        pos = at;
        while (text[pos] == ' ') pos++;
        char markText[8];
        snprintf(markText, sizeof(markText), " %s%s", kChatAckMarker, text[pos] ? " " : "");
        lv_spangroup_set_span_text(sg, mark, markText);
    }
    size_t nameFrom = 0, nameTo = 0;
    if ((nameFont || outFreshDot) && chatFindNameTag(text, &nameFrom, &nameTo)
        && nameFrom >= pos) {
        chatAddLineSpan(sg, text + pos, nameFrom - pos, nullptr);
        if (outFreshDot) {
            lv_span_t *dot = lv_spangroup_add_span(sg);
            lv_style_set_text_color(lv_span_get_style(dot), freshDotColor());
            // One space either side, counting one the clock already ends in.
            const bool spaceBefore = nameFrom > 0 && text[nameFrom - 1] == ' ';
            lv_spangroup_set_span_text(sg, dot, spaceBefore ? LV_SYMBOL_BULLET " "
                                                            : " " LV_SYMBOL_BULLET " ");
            *outFreshDot = dot;
        }
        chatAddLineSpan(sg, text + nameFrom, nameTo - nameFrom, nameFont);
        pos = nameTo;
    }
    chatAddLineSpan(sg, text + pos, len - pos, nullptr);
    lv_spangroup_refresh(sg);
}

static void chatSetAckedLineText(lv_obj_t *sg, const char *line, const lv_font_t *chatFont) {
    chatSetLineSpans(sg, line, chatFont, true, nullptr, nullptr);
}

// Joins the icon and clock into the bubble's leading meta field, in the same
// order classic shows them. Either half may be absent.
static void chatComposeBubbleMeta(const char *icon, const char *clock,
                                  char *out, size_t len) {
    if (!out || len == 0) return;
    const bool haveIcon  = (icon && icon[0]);
    const bool haveClock = (clock && clock[0]);
    if (haveIcon && haveClock)      snprintf(out, len, "%s %s", icon, clock);
    else if (haveClock)             snprintf(out, len, "%s", clock);
    else if (haveIcon)              snprintf(out, len, "%s", icon);
    else                            out[0] = '\0';
}

static const char *chatStripPrefix(const char *line) {
    // The sender prefix ("<icon> <time>[Name] ") always ends at the first "] "
    // (or "> "), which precedes any message text. The window just bounds the
    // scan so a prefix-less line that happens to contain "] " isn't truncated.
    // It must be wide enough for the longest prefix: with Long chat names the
    // bracketed name can be a full 39-char node long name plus the time/icon
    // decorations, so a 28-char window left long-name prefixes unstripped and
    // the name leaked into the bubble body as well as the header.
    const int kWindow = 64;
    for (const char *p = line; *p && (int)(p - line) < kWindow; p++) {
        if ((p[0] == ']' || p[0] == '>') && p[1] == ' ') return p + 2;
    }
    return line;
}

// Usable width of the list bubbles are being rendered into, resolved once per
// render pass.
//
// This has to be measured, not assumed: the chat pane and the narrower DM pane
// differ, and so do all five panels. The catch is that a list's coordinates are
// only filled in by a layout pass, and a render that runs before the first one
// reads back 0 — so the fallback, not the measurement, would decide the width.
// On the Pager that fallback was the full 480 px display against a 359 px list,
// wide enough that nothing ever looked like it needed wrapping, and the chat
// render cache then held that bad pass on screen. lv_obj_update_layout() forces
// the coordinates to resolve first. Doing it here, once per pass, rather than
// per bubble, keeps it off the O(n) path — laying out a list while appending to
// it would otherwise cost a full tree walk per message.
static lv_coord_t s_chatBubbleListW = 0;
// IRC style, per render pass: the time column's width (0 until the first row
// measures it, since that is when the font is known) and the last row made, so
// a separator goes between two rows but not between a row and a date marker.
static lv_coord_t s_chatIrcTimeColW = 0;
static lv_obj_t *s_chatIrcLastRow = nullptr;

static void chatBubbleBeginRender(lv_obj_t *list) {
    s_chatBubbleListW = 0;
    s_chatIrcTimeColW = 0;
    s_chatIrcLastRow = nullptr;
    if (!list) return;
    lv_obj_update_layout(list);
    s_chatBubbleListW = lv_obj_get_content_width(list);
    if (s_chatBubbleListW > 0) return;
    // Layout still unresolved: fall back to the parent panel, then to a share of
    // the display that no build's chat pane exceeds. Both under-estimate rather
    // than over-estimate — a narrow bubble is cosmetic, a wide one truncates.
    lv_obj_t *parent = lv_obj_get_parent(list);
    if (parent) s_chatBubbleListW = lv_obj_get_content_width(parent) - 6;
    if (s_chatBubbleListW <= 0) s_chatBubbleListW = (lv_coord_t)((lv_disp_get_hor_res(NULL) * 7) / 10);
}

// Widest a bubble label may draw before it has to reflow. Mirrors the bubble's
// own geometry: 94% of the row (its max_width), less its 4 px padding per side
// and, in outline style, its border.
static lv_coord_t chatBubbleLabelMaxWidth(int borderW) {
    const lv_coord_t maxW = (lv_coord_t)(((int)s_chatBubbleListW * 94) / 100 - 8 - 2 * borderW);
    return (maxW > 24) ? maxW : 24;
}

// Size one bubble label so nothing gets clipped.
//
// Bubbles are LV_SIZE_CONTENT wide with a 94% cap so short messages stay short,
// and their body label clips rather than reflows. That combination truncates any
// text wider than the cap, and two things routinely are: channel lines arrive
// pre-wrapped at MSG_CHARS — a character count tuned for the classic full-width
// row, so it overshoots a 94% bubble on every board and overshoots much further
// at Large/Extra Large — and DM bodies arrive unwrapped entirely.
//
// So measure what the label will really draw (post emoji substitution, in the
// scaled font) and only reflow when it overspills. Text that already fits keeps
// LV_SIZE_CONTENT, so short bubbles stay short. When it doesn't fit, the label
// is re-measured at the wrap width and pinned to the widest resulting line, so
// a message that wraps to three short lines gets a bubble that hugs them
// instead of one stretched to the full 94%.
static void chatFitBubbleLabel(lv_obj_t *label, const lv_font_t *font, lv_coord_t maxW) {
    if (!label || !font || maxW <= 0) return;
    const char *text = lv_label_get_text(label);
    if (!text || !text[0]) return;

    lv_point_t size;
    lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= maxW) return;

    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_txt_get_size(&size, text, font, 0, 0, maxW, LV_TEXT_FLAG_NONE);
    lv_coord_t w = size.x + 2;   // guard against rounding at the wrap boundary
    if (w > maxW) w = maxW;
    lv_obj_set_width(label, w);
}

#if defined(DEVICE_TDECK_PRO)
// A bubble's header on the T-Deck Pro: the time in the chat face, then the
// sender in its bold twin (chatNameFont()), then the ACK mark. Three labels in
// a row rather than one, because a label draws in one face.
//
// The row hugs its labels when they fit on one line. When they do not -- a long
// chat name next to the time -- it takes the bubble's full label width and
// wraps, so the name drops under the time instead of being clipped, the way
// the single-label header reflowed.
static void chatMakeBubbleHeaderPro(lv_obj_t *b, const char *metaTag, const char *whoTag,
                                    bool ackMark, const lv_font_t *font,
                                    lv_color_t tagColor, lv_coord_t maxW) {
    const lv_font_t *nameFont = chatNameFont(font);
    const lv_coord_t gap = (lv_coord_t)lv_font_get_glyph_width(font, ' ', ' ');

    lv_obj_t *hdr = lv_obj_create(b);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(hdr, gap, 0);

    lv_coord_t total = 0;
    auto addLabel = [&](const char *text, const lv_font_t *f) {
        lv_obj_t *l = lv_label_create(hdr);
        lv_obj_set_style_text_font(l, f, 0);
        lv_obj_set_style_text_color(l, tagColor, 0);
        lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
        lv_label_set_text(l, text);
        lv_point_t sz;
        lv_txt_get_size(&sz, text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        total += (total ? gap : 0) + (lv_coord_t)sz.x;
        return l;
    };
    if (metaTag && metaTag[0]) addLabel(metaTag, font);
    lv_obj_t *nm = addLabel(whoTag, nameFont);
    if (ackMark) addLabel(kChatAckMarker, chatAckMarkerFont(font));

    if (total <= maxW) {
        lv_obj_set_width(hdr, LV_SIZE_CONTENT);
    } else {
        lv_obj_set_width(hdr, maxW);
        // A name too long for a line of its own wraps inside its label.
        chatFitBubbleLabel(nm, nameFont, maxW);
    }
}
#endif

// ── Replies and reactions in channel chat ──────────────────────────────────
//
// A reply carries the packet id it answers (DisplayLine::replyId). A reaction
// (tapback) is a reply with the emoji flag: it is hidden as a line of its own
// and tallied as a chip under the message it reacts to instead.
//
// The renderers read the current message's decoration from s_chatDeco, set by
// refreshChatViewBubbles() and the classic loop around each message. DM threads
// never set it, so their bubbles and rows are drawn exactly as before.
struct ChatReplyDeco {
    uint32_t replyId;        // nonzero: this message answers replyId
    bool     found;          // the original is still in this channel's history
    bool     origIsMe;
    uint32_t origSender;
    char     origName[40];
    char     snippet[48];
    uint8_t  chipCount;
    char     chipEmoji[6][16];
    uint8_t  chipN[6];
};
static const ChatReplyDeco *s_chatDeco = nullptr;

struct ChatReactionTally {
    uint32_t target;
    char     emoji[16];
    uint8_t  count;
};
static ChatReactionTally s_chatReactions[32];
static int s_chatReactionCount = 0;

// Tally the active channel's reactions once per render pass.
static void chatCollectReactions() {
    s_chatReactionCount = 0;
    for (int row = 0; row < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        if (!dl->reaction || dl->replyId == 0) continue;
        if (dl->text[0] == ' ' && dl->text[1] == ' ') continue;   // continuation
        char emoji[16];
        const char *e = chatStripPrefix(dl->text);
        while (*e == ' ') e++;
        utf8util::copyTruncate(emoji, sizeof(emoji), e);
        if (!emoji[0]) continue;
        int k = 0;
        for (; k < s_chatReactionCount; k++) {
            if (s_chatReactions[k].target == dl->replyId
                && strcmp(s_chatReactions[k].emoji, emoji) == 0) break;
        }
        if (k < s_chatReactionCount) {
            if (s_chatReactions[k].count < 99) s_chatReactions[k].count++;
        } else if (s_chatReactionCount < (int)(sizeof(s_chatReactions) / sizeof(s_chatReactions[0]))) {
            ChatReactionTally &t = s_chatReactions[s_chatReactionCount++];
            t.target = dl->replyId;
            memcpy(t.emoji, emoji, sizeof(t.emoji));
            t.count = 1;
        }
    }
}

// Fill the decoration for one message. rows are the visible (collected) rows,
// msg its first line, ownId its packet id. False when there is nothing to draw.
static bool chatBuildReplyDeco(const DisplayLine *const *rows, int rowCount,
                               const DisplayLine *msg, uint32_t ownId,
                               ChatReplyDeco &d) {
    memset(&d, 0, sizeof(d));
    if (!msg) return false;

    if (ownId != 0) {
        for (int k = 0; k < s_chatReactionCount && d.chipCount < 6; k++) {
            if (s_chatReactions[k].target != ownId) continue;
            memcpy(d.chipEmoji[d.chipCount], s_chatReactions[k].emoji, sizeof(d.chipEmoji[0]));
            d.chipN[d.chipCount] = s_chatReactions[k].count;
            d.chipCount++;
        }
    }

    if (msg->replyId != 0 && !msg->reaction) {
        d.replyId = msg->replyId;
        // Lowest index in a packet group is its first (prefix) line.
        for (int r = 0; r < rowCount; r++) {
            const DisplayLine *o = rows[r];
            if (!o || o->packetId != msg->replyId) continue;
            d.found = true;
            d.origSender = o->senderNodeId;
            d.origIsMe = (s_myNodeId != 0 && o->senderNodeId == s_myNodeId);
            if (d.origIsMe) {
                snprintf(d.origName, sizeof(d.origName), "%s", TR("You"));
            } else if (o->senderNodeId != 0) {
                chatSenderLabel(o->senderNodeId, d.origName, sizeof(d.origName));
            }
            const char *s = chatStripPrefix(o->text);
            while (*s == ' ') s++;
            const size_t kSnip = 28;
            size_t len = strlen(s);
            if (len > kSnip) {
                size_t cut = kSnip;
                while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;
                snprintf(d.snippet, sizeof(d.snippet), "%.*s...", (int)cut, s);
            } else {
                snprintf(d.snippet, sizeof(d.snippet), "%s", s);
            }
            for (char *p = d.snippet; *p; p++) {
                if (*p == '\n' || *p == '\r') *p = ' ';
            }
            break;
        }
    }
    return d.replyId != 0 || d.chipCount != 0;
}

static lv_color_t chatReplyBarColor(const ChatReplyDeco &d) {
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(d);
    return lv_color_make(0, 0, 0);
#else
    if (d.found && s_cfg.chatColorsEnabled && d.origSender != 0) {
        return tftColorToLv(d.origIsMe ? userMessageAccentColor565()
                                       : nodeBubbleColor565(d.origSender));
    }
    return (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x7C8799) : lv_color_hex(0x8A97AD);
#endif
}

// "↪@Name" for the compact style; "↪@…" when the original has scrolled away.
static void chatReplyTagText(const ChatReplyDeco &d, char *out, size_t outLen) {
    if (d.found && d.origName[0]) snprintf(out, outLen, "\xE2\x86\xAA@%s", d.origName);
    else snprintf(out, outLen, "\xE2\x86\xAA@...");
}

// The quote block: a coloured left bar beside "↪ Name: snippet" on one line.
static lv_obj_t *chatMakeReplyQuote(lv_obj_t *parent, const ChatReplyDeco &d,
                                    const lv_font_t *font, lv_color_t textColor,
                                    lv_coord_t maxW) {
    lv_obj_t *q = lv_obj_create(parent);
    lv_obj_remove_style_all(q);
    lv_obj_clear_flag(q, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(q, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(q, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_side(q, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(q, 3, 0);
    lv_obj_set_style_border_color(q, chatReplyBarColor(d), 0);
    lv_obj_set_style_border_opa(q, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(q, 4, 0);

    char text[112];
    if (d.found) {
        snprintf(text, sizeof(text), "\xE2\x86\xAA %s: %s",
                 d.origName[0] ? d.origName : "?", d.snippet);
    } else {
        snprintf(text, sizeof(text), "\xE2\x86\xAA %s", TR("earlier message"));
    }
    lv_obj_t *lbl = lv_label_create(q);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, textColor, 0);
#if !defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_opa(lbl, LV_OPA_70, 0);
#endif
    setLabelTextEmojiSafe(lbl, text);
    const lv_coord_t room = maxW - 7;
    lv_point_t sz;
    lv_text_get_size(&sz, lv_label_get_text(lbl), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (room > 16 && sz.x > room) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, room);
    }
    return q;
}

// Reactions under a message's body: one emoji (and count) per reaction.
static lv_obj_t *chatMakeReactionChips(lv_obj_t *parent, const ChatReplyDeco &d,
                                  const lv_font_t *font, lv_color_t color) {
    if (d.chipCount == 0) return nullptr;
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_pad_row(row, 2, 0);
    lv_obj_set_style_pad_top(row, 2, 0);
    for (int k = 0; k < d.chipCount; k++) {
        lv_obj_t *chip = lv_label_create(row);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_font(chip, font, 0);
        lv_obj_set_style_text_color(chip, color, 0);
        char t[24];
        if (d.chipN[k] > 1) snprintf(t, sizeof(t), "%s %u", d.chipEmoji[k], (unsigned)d.chipN[k]);
        else snprintf(t, sizeof(t), "%s", d.chipEmoji[k]);
        setLabelTextEmojiSafe(chip, t);
    }
    return row;
}

// Create one message bubble (row wrapper + colored container + optional name tag
// + body) in the chat list. Right-aligned + accent for our own messages,
// left-aligned + node color for others. Updates the scroll anchor pointers.
static void chatMakeBubble(lv_obj_t *list, uint32_t sender, bool isMe,
                           const char *metaTag, const char *nameTag,
                           const char *body,
                           DisplayLine::AckState ackState,
                           uint32_t replyPacketId, bool isSelected,
                           lv_obj_t **outLast, lv_obj_t **outSelected,
                           lv_event_cb_t onPressed,
                           lv_obj_t **outFreshDot) {
    lv_obj_t *rowW = lv_obj_create(list);
    lv_obj_remove_style_all(rowW);
    lv_obj_set_width(rowW, lv_pct(100));
    lv_obj_set_height(rowW, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(rowW, 1, 0);
    lv_obj_set_style_pad_bottom(rowW, 1, 0);
    lv_obj_set_flex_flow(rowW, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rowW, isMe ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    stylePaperMessageRow(rowW);

    const ChatReplyDeco *deco = s_chatDeco;

    // New message: the dot to the bubble's left, centred on it. Ours are never
    // new, so it is always on the left-aligned side.
    if (outFreshDot) {
        *outFreshDot = nullptr;
        if (!isMe) {
            lv_obj_set_flex_align(rowW, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(rowW, 3, 0);
            *outFreshDot = makeFreshDot(rowW, freshDotSize(scaledChatFont(kChannelChatFont)));
        }
    }

#if !defined(DEVICE_TDECK_PRO)
    uint16_t bg565;
    if (s_cfg.chatColorsEnabled) {
        bg565 = isMe ? userMessageAccentColor565()
                     : (sender ? nodeBubbleColor565(sender) : rgb565(90, 99, 120));
    } else {
        // Keep bubble structure but neutralize per-node color coding.
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            bg565 = isMe ? rgb565(0xC6, 0xCF, 0xE2) : rgb565(0xD7, 0xDF, 0xEF);
        } else {
            bg565 = isMe ? rgb565(0x52, 0x5D, 0x72) : rgb565(0x43, 0x4D, 0x62);
        }
    }
    if (isMe && s_cfg.chatColorsEnabled) {
        switch (ackState) {
            case DisplayLine::ACKED:
                bg565 = rgb565(0x2D, 0x7D, 0x46);
                break;
            case DisplayLine::NAKED:
            case DisplayLine::TX_FAILED:
                bg565 = rgb565(0xA8, 0x38, 0x3A);
                break;
            default:
                break;
        }
    }
#endif
    lv_obj_t *b = lv_obj_create(rowW);
    lv_obj_remove_style_all(b);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(b, lv_pct(94), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 1, 0);

    // Filled (BUBBLES): solid per-node background, contrast text on top.
    // Outlined (OUTLINE): transparent fill + a colored border carrying the
    // node/ack color, with the body in the theme's readable text color and the
    // sender/state tag tinted in the node color.
#if defined(DEVICE_TDECK_PRO)
    const bool outline = true;
    const int borderW = 1;
#else
    const bool outline = (s_cfg.chatStyle == CHAT_STYLE_OUTLINE);
    const int borderW = outline ? ((s_cfg.uiMode == UI_MODE_LIGHT) ? 2 : 1) : 0;
#endif
    const lv_font_t *bubbleFont = scaledChatFont(kChannelChatFont);
    const lv_coord_t bubbleMaxW = chatBubbleLabelMaxWidth(borderW);
    lv_color_t tagColor;   // sender name + ME/ME(SENT) tag
    lv_color_t bodyColor;  // message text
#if defined(DEVICE_TDECK_PRO)
    LV_UNUSED(outline);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, borderW, 0);
    lv_obj_set_style_border_color(b, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
    tagColor = lv_color_make(0, 0, 0);
    bodyColor = lv_color_make(0, 0, 0);
#else
    if (outline) {
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b, borderW, 0);
        lv_obj_set_style_border_color(b, tftColorToLv(bg565), 0);
        lv_obj_set_style_border_opa(b, LV_OPA_COVER, 0);
        // The border keeps the full node color, but on dark themes several
        // palette colors (slate, purple, deep red) are too dim to read as text
        // at 70% opacity — lighten the name/state tag toward white so it stays
        // legible while still carrying the node's hue.
        tagColor  = (s_cfg.uiMode == UI_MODE_LIGHT)
                        ? tftColorToLv(bg565)
                        : lv_color_lighten(tftColorToLv(bg565), 190);
        bodyColor = (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x16233A)
                                                    : lv_color_hex(0xE8F1FF);
    } else {
        lv_obj_set_style_bg_color(b, tftColorToLv(bg565), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        tagColor  = bubbleTextColor(bg565);
        bodyColor = tagColor;
    }
#endif

    const char *stateTag = isMe ? "ME" : nullptr;
    const bool ackMark = isMe && (ackState == DisplayLine::ACKED
                                  || ackState == DisplayLine::ACKED_RELAY);

    const char *whoTag = (nameTag && nameTag[0]) ? nameTag : stateTag;
#if defined(DEVICE_TDECK_PRO)
    if (whoTag) {
        chatMakeBubbleHeaderPro(b, metaTag, whoTag, ackMark, bubbleFont, tagColor, bubbleMaxW);
    } else
#endif
    if (whoTag || (metaTag && metaTag[0])) {
        char header[96];
        if (metaTag && metaTag[0] && whoTag) {
            snprintf(header, sizeof(header), "%s  %s", metaTag, whoTag);
        } else {
            snprintf(header, sizeof(header), "%s", whoTag ? whoTag : metaTag);
        }
#if CHAT_REPLY_TAG_STYLE
        if (deco && deco->replyId) {
            char tag[48];
            chatReplyTagText(*deco, tag, sizeof(tag));
            const size_t hl = strlen(header);
            snprintf(header + hl, sizeof(header) - hl, "  %s", tag);
        }
#endif
        // With an ACK in, the header is a row: the tag, then the small marker
        // sitting on its baseline side.
        lv_obj_t *hdrParent = b;
        if (ackMark) {
            hdrParent = lv_obj_create(b);
            lv_obj_remove_style_all(hdrParent);
            lv_obj_set_size(hdrParent, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(hdrParent, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(hdrParent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                                  LV_FLEX_ALIGN_END);
            lv_obj_set_style_pad_column(hdrParent, 3, 0);
        }
        lv_obj_t *nm = lv_label_create(hdrParent);
        lv_obj_set_style_text_font(nm, bubbleFont, 0);
        lv_obj_set_style_text_color(nm, tagColor, 0);
        lv_obj_set_style_text_opa(nm, LV_OPA_70, 0);
        lv_label_set_text(nm, header);
        // A long chat name plus the time can be wider than the bubble on its own.
        chatFitBubbleLabel(nm, bubbleFont, bubbleMaxW);
        if (ackMark) {
            lv_obj_t *mark = lv_label_create(hdrParent);
            lv_obj_set_style_text_font(mark, chatAckMarkerFont(bubbleFont), 0);
            lv_obj_set_style_text_color(mark, tagColor, 0);
            lv_obj_set_style_text_opa(mark, LV_OPA_70, 0);
            lv_label_set_text(mark, kChatAckMarker);
        }
    }

#if !CHAT_REPLY_TAG_STYLE
    if (deco && deco->replyId) {
        chatMakeReplyQuote(b, *deco, bubbleFont, tagColor, bubbleMaxW);
    }
#endif

    lv_obj_t *bl = lv_label_create(b);
    lv_obj_set_style_text_font(bl, bubbleFont, 0);
    lv_obj_set_style_text_color(bl, bodyColor, 0);
    lv_label_set_long_mode(bl, LV_LABEL_LONG_CLIP);  // body carries explicit '\n'
    lv_obj_set_width(bl, LV_SIZE_CONTENT);
    setLabelTextEmojiSafe(bl, body);
    // Measured after the text is set: emoji substitution rewrites it, so the
    // source string is not what actually gets drawn.
    chatFitBubbleLabel(bl, bubbleFont, bubbleMaxW);

    if (deco) chatMakeReactionChips(b, *deco, bubbleFont, tagColor);

    if (isSelected) {
        const bool lightTheme = (s_cfg.uiMode == UI_MODE_LIGHT);
        lv_obj_set_style_outline_width(b, lightTheme ? 3 : 2, 0);
        lv_obj_set_style_outline_color(
            b,
            lightTheme ? lv_color_hex(0x13233D) : lv_color_hex(0xFFFFFF),
            0);
        lv_obj_set_style_outline_opa(b, lightTheme ? LV_OPA_COVER : LV_OPA_80, 0);
        if (outSelected) *outSelected = rowW;
    }

    // DM bubbles pass nullptr: they have no channel-chat reply/selection model.
    if (onPressed) {
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, onPressed, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)replyPacketId);
#if HAS_TOUCH
        // Hold opens Message Actions. Gated on onPressed along with the tap
        // above, so DM bubbles stay non-interactive as they are today — in a DM
        // thread the menu would only offer to DM the person already being DM'd.
        //
        // Carries the *packet id*, not the sender: the handler needs both, LVGL
        // allows one user_data, and the sender is recoverable from the display
        // line while the packet id is not. A message with no packet id has
        // nothing to react or reply to, so it gets no long press at all.
        if (sender != 0 && !isMe && replyPacketId != 0) {
            lv_obj_add_event_cb(b, onChatMessageLongPressed, LV_EVENT_LONG_PRESSED,
                                (void *)(uintptr_t)replyPacketId);
        }
#endif
    }

    if (outLast) *outLast = rowW;
}

// One message in the IRC style: the lock screen's notice row, brought into the
// chat. Time in the lock screen's blue, then "Name:" and the text, which wraps
// with its second line under the text's first rather than back at the edge, so
// a wrap never reads as a new message starting.
//
// The time sits in a column of its own, as wide as the widest clock the
// current format writes, so every message's text starts at the same x. The
// name and text are one span group: two labels side by side could not wrap
// into each other, and label recolor has no escape for '#', which channel text
// is full of.
//
// No transport icon, unlike classic: the lock screen does not show one, and a
// glyph in front of some times and not others would break the column.
static void chatMakeIrcRow(lv_obj_t *list, const lv_font_t *font,
                           uint32_t sender, bool isMe,
                           const char *timeText, const char *nameText,
                           const char *body,
                           DisplayLine::AckState ackState,
                           uint32_t replyPacketId, bool isSelected,
                           lv_obj_t **outLast, lv_obj_t **outSelected,
                           lv_event_cb_t onPressed,
                           lv_obj_t **outFreshDot) {
    if (!list || !font) return;
    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);

    // Same separator classic draws, and only after a row: one after a date
    // marker would sit between the marker and the message it dates.
    if (s_chatIrcLastRow && lv_obj_get_child(list, -1) == s_chatIrcLastRow) {
        lv_obj_t *sep = lv_obj_create(list);
        lv_obj_remove_style_all(sep);
        lv_obj_set_size(sep, lv_pct(100), 1);
#if defined(DEVICE_TDECK_PRO)
        // Black and solid: the 1-bit panel thresholds, and a 70% blue rule
        // would land on one side of the cut or the other row by row.
        lv_obj_set_style_bg_color(sep, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
#else
        lv_obj_set_style_bg_color(sep, lv_color_hex(0x3F669F), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
#endif
    }

    // The lock screen's blue, darkened for a pale background; its white body
    // becomes the theme's own text colour for the same reason.
    const lv_color_t timeColor = lightUi ? lv_color_hex(0x1F5FAF) : lv_color_make(32, 160, 255);
    const lv_color_t bodyColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xE8F1FF);

    // The lock screen's green, unless Chat Colors gives each node its own.
    uint16_t name565 = lightUi ? rgb565(0x1E, 0x7A, 0x3C) : rgb565(64, 220, 112);
    if (s_cfg.chatColorsEnabled && sender != 0) {
        name565 = isMe ? userMessageAccentColor565() : nodeBubbleColor565(sender);
    }
    // Our own messages carry their delivery state the way classic does: green
    // for a routing ACK, the accent for a relay, red for a failure.
    const char *ackMarker = nullptr;
    if (isMe) {
        switch (ackState) {
            case DisplayLine::ACKED:
                name565 = lightUi ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                ackMarker = kChatAckMarker;
                break;
            case DisplayLine::ACKED_RELAY:
                name565 = userMessageAccentColor565();
                ackMarker = kChatAckMarker;
                break;
            case DisplayLine::NAKED:
            case DisplayLine::TX_FAILED:
                name565 = TFT_RED;
                break;
            default:
                break;
        }
    }
#if defined(DEVICE_TDECK_PRO)
    // Everything in black, as the Pro's outline bubbles draw it: the panel is
    // 1-bit, and the blue, green and node colours above threshold to black or
    // vanish to white depending on the shade. The delivery state still shows,
    // through the ack mark after the text.
    LV_UNUSED(timeColor);
    LV_UNUSED(bodyColor);
    const lv_color_t ink = lv_color_make(0, 0, 0);
    const lv_color_t nameColorPro = ink;
#endif
    const lv_color_t nameColor =
#if defined(DEVICE_TDECK_PRO)
        nameColorPro;
#else
        tftColorToLv(name565);
#endif

    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#if defined(DEVICE_TDECK) || defined(DEVICE_M9)
    const int padL = 1, padR = 0;
#else
    const int padL = 2, padR = 4;
#endif
    const ChatReplyDeco *deco = s_chatDeco;
    const int gap = 4;   // the lock screen's gap between time and sender
    lv_obj_set_style_pad_left(row, padL, 0);
    lv_obj_set_style_pad_right(row, padR, 0);
    lv_obj_set_style_pad_column(row, gap, 0);
    stylePaperMessageRow(row);

    // The column is sized from the font once per pass. A line stored under the
    // other clock format can still be wider; that row's text starts a little
    // further in rather than being clipped.
    if (s_chatIrcTimeColW <= 0) {
        lv_point_t sz;
        lv_text_get_size(&sz, liveClockIs12Hour() ? "00:00 PM" : "00:00", font,
                         0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        s_chatIrcTimeColW = (lv_coord_t)sz.x;
    }
    lv_coord_t timeW = s_chatIrcTimeColW;
    if (timeText && timeText[0]) {
        lv_point_t sz;
        lv_text_get_size(&sz, timeText, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > timeW) timeW = (lv_coord_t)sz.x;
    }

    lv_obj_t *timeLbl = lv_label_create(row);
    lv_obj_set_style_text_font(timeLbl, font, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_color(timeLbl, ink, 0);
#else
    lv_obj_set_style_text_color(timeLbl, timeColor, 0);
#endif
    lv_label_set_long_mode(timeLbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(timeLbl, timeW);
    lv_label_set_text(timeLbl, (timeText && timeText[0]) ? timeText : "");

    // The new-message dot's slot, between the time and the sender. Every row
    // has one, so the text starts at the same x with a dot or without; only a
    // new message's is filled. Pushed down to sit on the first line's middle.
    const int dotD = freshDotSize(font);
    lv_obj_t *dot = makeFreshDot(row, dotD);
    lv_obj_set_style_margin_top(dot, max(0, ((int)lv_font_get_line_height(font) - dotD) / 2), 0);
    if (outFreshDot) *outFreshDot = dot;
    else lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);

    // A fixed width is what makes a span group wrap; left at content width it
    // grows to one long line instead.
    lv_coord_t textW = s_chatBubbleListW - padL - padR - timeW - gap - dotD - gap;
    if (textW < 24) textW = 24;
    // With reactions, the text and the reactions under it share a column.
    lv_obj_t *textCol = row;
    if (deco && deco->chipCount) {
        textCol = lv_obj_create(row);
        lv_obj_remove_style_all(textCol);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(textCol, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(textCol, textW);
        lv_obj_set_height(textCol, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
    }
    lv_obj_t *sg = lv_spangroup_create(textCol);
    // Unlike a label, a span group is clickable by default, so it took every
    // press on the text and the row's tap and long press never fired.
    lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(sg, textW);
    lv_obj_set_height(sg, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(sg, font, 0);
    lv_obj_set_style_pad_all(sg, 0, 0);

    // Our own messages are "ME", as the bubbles call them, not whatever the
    // node database has for this node -- often only its id.
    if (isMe) nameText = "ME";

    // Folded once, before LVGL measures anything, the same as every other
    // place that draws received text.
    if (nameText && nameText[0]) {
        char name[64];
        renderEmojiSafeText(nameText, name, sizeof(name));
        char tagged[sizeof(name) + 52];
#if CHAT_REPLY_TAG_STYLE
        if (deco && deco->replyId) {
            char tag[48], tagSafe[48];
            chatReplyTagText(*deco, tag, sizeof(tag));
            renderEmojiSafeText(tag, tagSafe, sizeof(tagSafe));
            snprintf(tagged, sizeof(tagged), "%s %s: ", name, tagSafe);
        } else
#endif
        snprintf(tagged, sizeof(tagged), "%s: ", name);
        lv_span_t *sp = lv_spangroup_add_span(sg);
        lv_style_set_text_color(lv_span_get_style(sp), nameColor);
#if defined(DEVICE_TDECK_PRO)
        lv_style_set_text_font(lv_span_get_style(sp), chatNameFont(font));
#endif
        lv_spangroup_set_span_text(sg, sp, tagged);
    }
    {
        char text[400];
        renderEmojiSafeText(body ? body : "", text, sizeof(text));
        lv_span_t *sp = lv_spangroup_add_span(sg);
#if defined(DEVICE_TDECK_PRO)
        lv_style_set_text_color(lv_span_get_style(sp), ink);
#else
        lv_style_set_text_color(lv_span_get_style(sp), bodyColor);
#endif
        lv_spangroup_set_span_text(sg, sp, text);
    }
    if (ackMarker) {
        lv_span_t *sp = lv_spangroup_add_span(sg);
        lv_style_set_text_color(lv_span_get_style(sp), nameColor);
        lv_style_set_text_font(lv_span_get_style(sp), chatAckMarkerFont(font));
        char markText[8];
        snprintf(markText, sizeof(markText), " %s", ackMarker);
        lv_spangroup_set_span_text(sg, sp, markText);
    }
    lv_spangroup_refresh(sg);
    if (textCol != row) {
        chatMakeReactionChips(textCol, *deco, font,
#if defined(DEVICE_TDECK_PRO)
                              ink);
#else
                              bodyColor);
#endif
    }

#if !CHAT_REPLY_TAG_STYLE
    // The quote goes above the row, lined up with the text column.
    if (deco && deco->replyId) {
        lv_obj_t *q = chatMakeReplyQuote(list, *deco, font,
#if defined(DEVICE_TDECK_PRO)
                                         ink,
#else
                                         bodyColor,
#endif
                                         textW);
        lv_obj_set_style_margin_left(q, padL + timeW + gap + dotD + gap, 0);
        lv_obj_set_style_margin_top(q, 1, 0);
        lv_obj_move_to_index(q, lv_obj_get_index(row));
    }
#endif

    if (isSelected) {
#if defined(DEVICE_TDECK_PRO)
        // An outline, as the Pro's bubbles mark theirs: a filled row would
        // threshold to solid black and take the black text with it.
        lv_obj_set_style_outline_width(row, 2, 0);
        lv_obj_set_style_outline_pad(row, 1, 0);
        lv_obj_set_style_outline_color(row, ink, 0);
        lv_obj_set_style_outline_opa(row, LV_OPA_COVER, 0);
#else
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A4E8F), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_70, 0);
#endif
        if (outSelected) *outSelected = row;
    }

    // DMs pass nullptr, as they do for bubbles: no reply/selection model there.
    if (onPressed) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, onPressed, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)replyPacketId);
#if HAS_TOUCH
        // Hold opens Message Actions; see chatMakeBubble() for why it carries
        // the packet id.
        if (sender != 0 && !isMe && replyPacketId != 0) {
            lv_obj_add_event_cb(row, onChatMessageLongPressed, LV_EVENT_LONG_PRESSED,
                                (void *)(uintptr_t)replyPacketId);
        }
#endif
    }

    s_chatIrcLastRow = row;
    if (outLast) *outLast = row;
}

// Render the chat rows as per-node colored bubbles (Bubbles and Outline), or as
// IRC rows, which take each message apart the same way.
static void refreshChatViewBubbles(const DisplayLine *const *rows, int rowCount,
                                   const int *displayOrder, int displayCount,
                                   int startFrom, int endAt,
                                   lv_obj_t **lastMsgObj, lv_obj_t **selectedMsgObj,
                                   lv_obj_t **anchorObj) {
    if (endAt > displayCount) endAt = displayCount;   // never walk past the order
    chatBubbleBeginRender(s_chatList);
    uint32_t lastDateBucket = 0;
    int n = startFrom;
    while (n < endAt) {
        int i = displayOrder[n];

        // Remember whether this message is the saved scroll anchor before n
        // advances past its continuation lines; chatMakeBubble reports the object.
        bool anchorHere = false;
        if (anchorObj && !*anchorObj && s_chatWindowAnchorActive && rows[i]) {
            anchorHere = (s_chatWindowAnchorPacketId != 0)
                ? (rows[i]->packetId == s_chatWindowAnchorPacketId)
                : (strcmp(rows[i]->text, s_chatWindowAnchorText) == 0);
        }

        uint32_t curBucket = chatDateBucket(rows[i]->epoch);
        if (curBucket != 0 && curBucket != lastDateBucket) {
            insertChatDateMarker(s_chatList, rows[i]->epoch, scaledChatFont(kChannelChatFont));
            lastDateBucket = curBucket;
        }

        uint32_t sender = rows[i]->senderNodeId;
        bool isMe = (s_myNodeId != 0 && sender == s_myNodeId);
        uint32_t replyPacketId = resolveReplyPacketId(rows, rowCount, i);

        // Reconstruct the message body from its wrapped lines: strip the first
        // line's prefix, then rejoin the continuation lines (leading indent
        // removed) with spaces.
        //
        // Storage wraps at MSG_CHARS, a character count sized for the classic
        // full-width row — always wider than a bubble's 94% cap, on every board
        // and at every font size. Keeping those breaks as '\n' therefore meant
        // each stored line still overflowed and got wrapped a second time,
        // leaving a short orphan after every line. Rejoining hands LVGL the
        // whole paragraph so it breaks once, at the real bubble width. The
        // breaks being rejoined were chosen at spaces, so this restores the
        // original text; only a word longer than a full line (which storage
        // splits mid-word) picks up a stray space. Any newline the sender typed
        // lives inside a stored line and survives untouched.
        char body[320];
        size_t bl = 0;
        const char *b0 = chatStripPrefix(rows[i]->text);
        bl += (size_t)snprintf(body + bl, sizeof(body) - bl, "%s", b0);
        n++;
        while (n < endAt) {
            int j = displayOrder[n];
            const char *t = rows[j]->text;
            if (!(t[0] == ' ' && t[1] == ' ')) break;   // not a continuation line
            const char *c = t;
            while (*c == ' ') c++;
            if (bl < sizeof(body) - 1)
                bl += (size_t)snprintf(body + bl, sizeof(body) - bl, " %s", c);
            n++;
        }
        if (rows[i]->packetId != 0) mecpDecodeLineInPlace(body, sizeof(body), 0);

        // Sender name tag for others (from NodeDB; fall back to hex id). Honors
        // the Chat Names setting: Long uses the advertised long name when known.
        char nameBuf[24];
        const char *nameTag = nullptr;
        if (!isMe && sender != 0) {
            NodeEntry *ne = Nodes.find(sender);
            if (s_cfg.chatNameStyle == CHAT_NAME_LONG && ne && ne->hasName && ne->longName[0]) {
                nameTag = ne->longName;
            } else if (ne && ne->shortName[0]) {
                nameTag = ne->shortName;
            } else {
                snprintf(nameBuf, sizeof(nameBuf), "%04X", (unsigned)(sender & 0xFFFF));
                nameTag = nameBuf;
            }
        }

        bool isSelected = false;
        if (s_selectedMsgReplyPacketId != 0) {
            isSelected = (replyPacketId == s_selectedMsgReplyPacketId);
        } else {
            isSelected = (strcmp(rows[i]->text, s_selectedMsgText) == 0);
        }

        // Prefer the prefix, so a bubble shows the same time and transport icon
        // classic does for the same line; fall back to epoch for lines stored
        // without a prefix.
        char iconBuf[12], timeBuf[LIVE_CLOCK_BUF], metaBuf[28];
        chatParsePrefix(rows[i]->text, iconBuf, sizeof(iconBuf), timeBuf, sizeof(timeBuf));
        if (!timeBuf[0]) formatChatClock(rows[i]->epoch, timeBuf, sizeof(timeBuf));

        if (s_cfg.chatStyle == CHAT_STYLE_IRC) {
            // Every sender is named here, ourselves included ("ME"): with no
            // bubble alignment to tell the two sides apart, the name is all
            // there is. chatSenderLabel() is what the lock screen names them with.
            char ircName[48] = "";
            if (sender != 0 && !isMe) chatSenderLabel(sender, ircName, sizeof(ircName));
            const bool freshDot = rows[i]->fresh && freshDotRoom(s_chatDots);
            lv_obj_t *dot = nullptr;
            ChatReplyDeco deco;
            if (chatBuildReplyDeco(rows, rowCount, rows[i], replyPacketId, deco)) s_chatDeco = &deco;
            chatMakeIrcRow(s_chatList, scaledChatFont(kChannelChatFont),
                           sender, isMe, timeBuf, ircName, body,
                           rows[i]->ack, replyPacketId, isSelected,
                           lastMsgObj, selectedMsgObj, onChatMessagePressed,
                           freshDot ? &dot : nullptr);
            s_chatDeco = nullptr;
            freshDotTrack(s_chatDots, dot, FRESH_DOT_SLOT);
            if (anchorHere && lastMsgObj) *anchorObj = *lastMsgObj;
            continue;
        }

        chatComposeBubbleMeta(iconBuf, timeBuf, metaBuf, sizeof(metaBuf));
        const bool freshDot = rows[i]->fresh && freshDotRoom(s_chatDots);
        lv_obj_t *dot = nullptr;
        ChatReplyDeco deco;
        if (chatBuildReplyDeco(rows, rowCount, rows[i], replyPacketId, deco)) s_chatDeco = &deco;
        chatMakeBubble(s_chatList, sender, isMe, metaBuf, nameTag, body,
                       rows[i]->ack,
                       replyPacketId, isSelected,
                       lastMsgObj, selectedMsgObj,
                       onChatMessagePressed,
                       freshDot ? &dot : nullptr);
        s_chatDeco = nullptr;
        freshDotTrack(s_chatDots, dot, FRESH_DOT_OBJ);
        if (anchorHere && lastMsgObj) *anchorObj = *lastMsgObj;
    }
}

// FNV-1a over non-row context that still changes rendered chat appearance.
// Row payload mutations are tracked by Channel::revision so this stays O(1)
// regardless of chat backlog size.
static uint32_t chatRenderContextSignature() {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(s_selectedMsgReplyPacketId);
    for (const char *p = s_selectedMsgText; *p; p++) mix((uint8_t)*p);
    mix((uint32_t)s_cfg.chatStyle);
    mix((uint32_t)s_cfg.chatNameStyle);
    // Both change every row's geometry, so a cached pass must not survive them.
    mix((uint32_t)s_cfg.fontSize);
    mix((uint32_t)s_cfg.chatColorsEnabled);
    // Reshuffling node colors from the web form has to repaint the chat too,
    // and this signature is the only thing that notices a config change made
    // from outside the settings screen.
    mix(s_cfg.chatColorSalt);
    mix((uint32_t)s_cfg.uiMode);
    mix((uint32_t)(s_pagerChatCursorMode ? 1u : 0u));
    mix((uint32_t)s_pagerChatCursorDisplayIndex);
    mix((uint32_t)i18nGetLang());   // MECP lines render in the UI language
    return h;
}

// Picks a window of at most `maxMessages` logical messages out of displayOrder.
// Logical-message boundaries mirror buildChatCursorOrder (same packetId run,
// else two-space continuation lines) so the window never splits a wrapped or
// grouped message. Sets *moreAbove when older messages remain above it.
//
// focusMsg picks where the window sits. -1 anchors it to the newest message,
// which is the scroll-back reading position. Any other value is a logical
// message index the window must contain, and it is centred so the message has
// context on both sides; that index shares its space with buildChatCursorOrder's
// cursorOrder, so the Pager cursor can be handed straight in.
//
// Writes the displayOrder range to render as [*startOut, *endOut).
static void chatWindowRange(const DisplayLine *const *rows,
                            const int *displayOrder, int displayCount,
                            int maxMessages, int focusMsg,
                            int *startOut, int *endOut, bool *moreAbove) {
    if (startOut)  *startOut  = 0;
    if (endOut)    *endOut    = displayCount;
    if (moreAbove) *moreAbove = false;
    if (maxMessages <= 0 || displayCount <= 0) return;

    // displayOrder index where each logical message starts. Only the UI thread
    // calls this (never re-entrant), so a static keeps it off the stack.
    static int starts[MAX_MSG_LINES];
    int msgCount = 0;
    for (int n = 0; n < displayCount && msgCount < MAX_MSG_LINES;) {
        starts[msgCount++] = n;
        int anchor = displayOrder[n];
        uint32_t packetId = rows[anchor] ? rows[anchor]->packetId : 0;
        n++;
        while (n < displayCount) {
            int j = displayOrder[n];
            if (!rows[j]) break;
            if (packetId != 0) {
                if (rows[j]->packetId != packetId) break;
                n++;
                continue;
            }
            const char *t = rows[j]->text;
            if (!(t[0] == ' ' && t[1] == ' ')) break;   // not a continuation line
            n++;
        }
    }

    if (msgCount <= maxMessages) return;   // whole history already fits

    int firstMsg;
    if (focusMsg < 0) {
        firstMsg = msgCount - maxMessages;
    } else {
        if (focusMsg >= msgCount) focusMsg = msgCount - 1;
        firstMsg = focusMsg - maxMessages / 2;
        if (firstMsg < 0) firstMsg = 0;
        if (firstMsg > msgCount - maxMessages) firstMsg = msgCount - maxMessages;
    }
    const int lastMsg = firstMsg + maxMessages;   // exclusive

    if (startOut)  *startOut  = starts[firstMsg];
    if (endOut)    *endOut    = (lastMsg >= msgCount) ? displayCount : starts[lastMsg];
    if (moreAbove) *moreAbove = (firstMsg > 0);
}

// Scroll handler on s_chatList: when the user reaches the top and older messages
// exist outside the window, flag a grow. The rebuild itself happens on the next
// refreshChatView tick (never inside the scroll event) to avoid re-entering
// LVGL layout mid-scroll.
static void onChatListScroll(lv_event_t *e) {
    (void)e;
    if (!s_chatList) return;
    // Only a real user gesture loads older history. Our own rebuild scrolls, and
    // a list too short to scroll at all, must never latch a grow — a grow that
    // latches on its own re-anchors the viewport to the top of the old window on
    // the next tick, which reads as the chat jumping away from the latest message
    // right after a channel opens.
    if (s_chatRebuilding) return;
    if (lv_obj_get_scroll_bottom(s_chatList) <= 0) return;   // nothing to scroll
    if (!s_chatWindowMoreAbove || s_chatWindowGrowPending) return;
    if (lv_obj_get_scroll_top(s_chatList) > kChatWindowGrowScrollMargin) return;
    s_chatWindowGrowPending = true;
}

static void refreshChatView(bool force) {
    if (!s_chatPanel || !s_chatList) return;

    refreshChatComposeButtonState();

    const Channel &ch = Channels.get(s_activeChannel);
    const uint32_t contextSig = chatRenderContextSignature();

    // Opening a different channel resets the window so it starts small (fast)
    // regardless of how far back the user scrolled in the previous one.
    const bool channelChanged = (s_activeChannel != s_chatWindowChannel);
    if (channelChanged) {
        s_chatWindowChannel = s_activeChannel;
        s_chatRenderWindowMsgs = kChatWindowBaseMsgs;
        s_chatWindowMoreAbove = false;
        s_chatWindowGrowPending = false;
        s_chatWindowAnchorActive = false;
    }

    if (!force && !s_chatWindowGrowPending && !s_chatScrollToLatestOnce
        && s_activeChannel == s_lastRenderedChannel
        && ch.revision == s_lastChatRenderedRevision
        && contextSig == s_lastChatContextSignature) {
        return;
    }

    s_chatRebuilding = true;   // cleared once the final scroll placement is done

    const DisplayLine *rows[MAX_MSG_LINES] = {};
    int rowCount = 0;
    collectChatRows(rows, rowCount);
    chatCollectReactions();

    // A channel switch always lands on the newest message. Both measurements
    // below describe the channel being left — its scroll position says nothing
    // about where the incoming one should open, and carrying it over is what
    // made a switch sometimes arrive part-way up the new channel's history.
    const bool stickToBottom = force || s_chatScrollToLatestOnce || channelChanged
                               || (lv_obj_get_scroll_bottom(s_chatList) <= 6);
    const int32_t prevScrollY = channelChanged ? 0 : lv_obj_get_scroll_y(s_chatList);

    freshDotsForget(s_chatDots, (uint32_t)s_activeChannel);
    lv_obj_clean(s_chatList);
    lv_obj_t *lastMsgObj = nullptr;
    lv_obj_t *selectedMsgObj = nullptr;
    lv_obj_t *anchorObj = nullptr;

    if (rowCount == 0) {
        s_chatWindowMoreAbove = false;
        s_chatWindowGrowPending = false;
        lv_obj_t *empty = lv_label_create(s_chatList);
        lv_obj_set_style_text_font(empty, scaledChatFont(kChannelChatFont), 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, TR("No messages yet"));
    } else {
        int displayOrder[MAX_MSG_LINES] = {};
        int displayCount = 0;
        buildChatDisplayOrder(rows, rowCount, displayOrder, displayCount);

        // Keyboard cursor navigation (Pager) can select any message in the full
        // history, and its target must exist as an object. This used to be met by
        // suspending windowing outright — but the cursor index feeds
        // chatRenderContextSignature(), so every detent invalidated the cache and
        // rebuilt the entire channel. That made each step cost O(history) on the
        // one board that navigates by cursor. Centring a fixed window on the
        // cursor satisfies the same requirement at constant cost.
        const bool cursorFocused = s_pagerChatCursorMode
                                   && s_pagerChatCursorDisplayIndex >= 0;
        const int focusMsg = cursorFocused ? s_pagerChatCursorDisplayIndex : -1;
        const int windowMsgs = cursorFocused ? kChatCursorWindowMsgs
                                             : s_chatRenderWindowMsgs;

        // Honor a pending scroll-to-top "load older" request: remember the message
        // currently at the top of the window, then widen it. The anchor lets us
        // restore the viewport to that message after older history is prepended.
        // Cursor mode never grows — there the window tracks the cursor rather
        // than the scroll position, so widening it would only add objects the
        // user did not ask for.
        if (!cursorFocused && s_chatWindowGrowPending) {
            s_chatWindowGrowPending = false;
            int prevStart = 0;
            chatWindowRange(rows, displayOrder, displayCount,
                            s_chatRenderWindowMsgs, -1, &prevStart, nullptr, nullptr);
            const DisplayLine *top = (prevStart < displayCount)
                                     ? rows[displayOrder[prevStart]] : nullptr;
            if (top) {
                s_chatWindowAnchorPacketId = top->packetId;
                snprintf(s_chatWindowAnchorText, sizeof(s_chatWindowAnchorText),
                         "%s", top->text);
                s_chatWindowAnchorActive = true;
            }
            s_chatRenderWindowMsgs += kChatWindowGrowMsgs;
        }

        bool moreAbove = false;
        int startN = 0;
        int endN = displayCount;
        chatWindowRange(rows, displayOrder, displayCount,
                        windowMsgs, focusMsg, &startN, &endN, &moreAbove);
        // Only the scroll-back path can load older history, so only it advertises
        // that there is any. In cursor mode the wheel moves the cursor and the
        // window follows it, and latching a grow there would fight that.
        s_chatWindowMoreAbove = cursorFocused ? false : moreAbove;

        // True when the message starting at displayOrder[n] is the saved anchor.
        auto isAnchorAt = [&](int n) -> bool {
            if (!s_chatWindowAnchorActive || n >= displayCount) return false;
            const DisplayLine *d = rows[displayOrder[n]];
            if (!d) return false;
            if (s_chatWindowAnchorPacketId != 0)
                return d->packetId == s_chatWindowAnchorPacketId;
            return strcmp(d->text, s_chatWindowAnchorText) == 0;
        };

        // IRC rows go through the bubble path too: it already takes each
        // message apart into time, sender and body, which is what they draw.
        bool useBubbleStyle = ((chatStyleUsesBubbles(s_cfg.chatStyle)
                                || s_cfg.chatStyle == CHAT_STYLE_IRC)
                               && s_activeChannel >= 0
                               && s_activeChannel < MESH_CHANNELS);
        if (useBubbleStyle) {
            refreshChatViewBubbles(rows, rowCount, displayOrder, displayCount,
                                   startN, endN, &lastMsgObj, &selectedMsgObj,
                                   &anchorObj);
        } else {
            // One label per logical message, not per stored line.
            //
            // Storage hard-wraps at MSG_CHARS, a character count — but the fonts
            // are proportional, so a stored line of wide glyphs runs past the row
            // while one of narrow glyphs falls short. Giving each stored line its
            // own wrapping label therefore broke some of them a second time,
            // dropping a word or two onto a line of their own for no reason the
            // reader can see. Handing LVGL the whole message lets it break once,
            // where the text actually reaches the edge. The continuation lines
            // being rejoined were split at spaces, so this restores the original
            // text; any newline the sender typed sits inside a stored line and
            // survives untouched.
            uint32_t lastDateBucket = 0;
            bool classicWidthReady = false;
            (void)classicWidthReady;   // only quote-style boards measure the list
            int n = startN;
            while (n < endN) {
                int i = displayOrder[n];
                const bool anchorHere = !anchorObj && isAnchorAt(n);

                // Insert a date marker before any message that lands on a new
                // local calendar day.
                uint32_t curBucket = chatDateBucket(rows[i]->epoch);
                if (curBucket != 0 && curBucket != lastDateBucket) {
                    insertChatDateMarker(s_chatList, rows[i]->epoch, scaledChatFont(kChannelChatFont));
                    lastDateBucket = curBucket;
                }

                // Anchor line keeps its prefix; continuation lines rejoin with a
                // space, their storage indent stripped.
                char merged[384];
                size_t ml = 0;
                ml += (size_t)snprintf(merged + ml, sizeof(merged) - ml, "%s", rows[i]->text);
                n++;
                while (n < endN) {
                    int j = displayOrder[n];
                    const char *t = rows[j]->text;
                    if (!(t[0] == ' ' && t[1] == ' ')) break;   // not a continuation line
                    const char *c = t;
                    while (*c == ' ') c++;
                    if (ml < sizeof(merged) - 1)
                        ml += (size_t)snprintf(merged + ml, sizeof(merged) - ml, " %s", c);
                    n++;
                }
                // MECP decodes here, before a reply tag lands between prefix
                // and body. Packet lines only: local/system lines are never MECP.
                bool mecpLine = false;
                uint8_t mecpSev = 0;
                if (rows[i]->packetId != 0) {
                    mecpLine = mecpDecodeLineInPlace(
                        merged, sizeof(merged), (size_t)(chatStripPrefix(merged) - merged), &mecpSev);
                    if (mecpLine) ml = strlen(merged);
                }

                // Replies: a tag after the prefix (small panels) or a quote
                // line above. Reactions go on a line under the message.
                ChatReplyDeco deco;
                const bool hasDeco = chatBuildReplyDeco(rows, rowCount, rows[i],
                                                        rows[i]->packetId, deco);
                const bool isReply = hasDeco && deco.replyId != 0;
#if CHAT_REPLY_TAG_STYLE
                if (isReply) {
                    const size_t pre = (size_t)(chatStripPrefix(merged) - merged);
                    char tag[48];
                    chatReplyTagText(deco, tag, sizeof(tag));
                    char tmp[sizeof(merged)];
                    snprintf(tmp, sizeof(tmp), "%.*s%s %s", (int)pre, merged, tag, merged + pre);
                    memcpy(merged, tmp, sizeof(merged));
                    ml = strlen(merged);
                }
#else
                if (isReply) {
                    if (!classicWidthReady) {
                        chatBubbleBeginRender(s_chatList);
                        classicWidthReady = true;
                    }
#if defined(DEVICE_TDECK_PRO)
                    const lv_color_t quoteColor = lv_color_make(0, 0, 0);
#else
                    const lv_color_t quoteColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                                      ? lv_color_hex(0x16233A)
                                                      : lv_color_hex(0xE8F1FF);
#endif
                    lv_obj_t *q = chatMakeReplyQuote(s_chatList, deco,
                                                     scaledChatFont(kChannelChatFont), quoteColor,
                                                     s_chatBubbleListW - 8);
#if defined(DEVICE_TDECK) || defined(DEVICE_M9)
                    lv_obj_set_style_margin_left(q, 1, 0);
#else
                    lv_obj_set_style_margin_left(q, 2, 0);
#endif
                    lv_obj_set_style_margin_top(q, 1, 0);
                }
#endif

                // A message whose ACK is in is a span group, for the small
                // marker; every other one stays a plain label. On the T-Deck
                // Pro every message with a packet id is one too, for the bold
                // sender tag. Lines without one stay labels: a tap selects
                // those by their text, which onChatMessagePressed() can only
                // read back from a label.
                const bool ackedLine = rows[i]->packetId
                    && (rows[i]->ack == DisplayLine::ACKED
                        || rows[i]->ack == DisplayLine::ACKED_RELAY);
                // A new message is a span group as well, for its dot -- one with
                // a packet id, which is what a tap on a span group selects by.
                const bool freshDot = rows[i]->fresh && rows[i]->packetId != 0
                                      && freshDotRoom(s_chatDots);
#if defined(DEVICE_TDECK_PRO)
                const bool spanLine = ackedLine || rows[i]->packetId != 0;
#else
                const bool spanLine = ackedLine || freshDot;
#endif
                lv_obj_t *msg = spanLine ? lv_spangroup_create(s_chatList)
                                         : lv_label_create(s_chatList);
                lastMsgObj = msg;
                if (anchorHere) anchorObj = msg;
                lv_obj_set_width(msg, lv_pct(100));
                lv_obj_set_style_text_font(msg, scaledChatFont(kChannelChatFont), 0);
                stylePaperMessageRow(msg);
#if !defined(DEVICE_TDECK_PRO)
                lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
#endif
#if defined(DEVICE_TDECK) || defined(DEVICE_M9)
                lv_obj_set_style_pad_left(msg, 1, 0);
                lv_obj_set_style_pad_right(msg, 0, 0);
#else
                lv_obj_set_style_pad_left(msg, 2, 0);
                lv_obj_set_style_pad_right(msg, 4, 0);
#endif
                lv_obj_set_style_pad_top(msg, 0, 0);
                lv_obj_set_style_pad_bottom(msg, 0, 0);
                if (!spanLine) lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

                uint16_t textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? TFT_BLACK : TFT_WHITE;
                if (s_cfg.chatColorsEnabled
                    && s_activeChannel >= 0
                    && s_activeChannel < MESH_CHANNELS) {
                    uint32_t sender = rows[i]->senderNodeId;
                    if (sender != 0) {
                        textColor565 = (s_myNodeId != 0 && sender == s_myNodeId)
                            ? userMessageAccentColor565()
                            : nodeBubbleColor565(sender);
                    }
                }
                if (rows[i]->packetId) {
                    switch (rows[i]->ack) {
                        case DisplayLine::ACKED:
                            textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                            break;
                        case DisplayLine::ACKED_RELAY:
                            // Marked, not just tinted. Channel text is broadcast,
                            // so expireAcks() settles nearly every sent message
                            // here rather than at ACKED — and the accent color
                            // this assigns is the one an own message already
                            // carries with chat colors on, which left the whole
                            // state invisible in this style. The bubble renderer
                            // has always marked both states too; the green
                            // above still separates a hard ACK from this.
                            textColor565 = userMessageAccentColor565();
                            break;
                        case DisplayLine::NAKED:
                        case DisplayLine::TX_FAILED:
                            textColor565 = TFT_RED;
                            break;
                        default:
                            break;
                    }
                }
                // A received emergency code outranks node colour; our own sent
                // line keeps its ACK colour so delivery state stays visible.
                if (mecpLine && rows[i]->senderNodeId != 0 && rows[i]->senderNodeId != s_myNodeId)
                    textColor565 = mecpSeverityColor565(mecpSev, s_cfg.uiMode == UI_MODE_LIGHT);
#if defined(DEVICE_TDECK_PRO)
                textColor565 = TFT_BLACK;
#endif

                lv_obj_set_style_text_color(msg, tftColorToLv(textColor565), 0);
                if (spanLine) {
                    lv_span_t *dot = nullptr;
#if defined(DEVICE_TDECK_PRO)
                    chatSetLineSpans(msg, merged, scaledChatFont(kChannelChatFont), ackedLine,
                                     chatNameFont(scaledChatFont(kChannelChatFont)),
                                     freshDot ? &dot : nullptr);
#else
                    chatSetLineSpans(msg, merged, scaledChatFont(kChannelChatFont), ackedLine,
                                     nullptr, freshDot ? &dot : nullptr);
#endif
                    if (dot) freshDotTrack(s_chatDots, msg, FRESH_DOT_SPAN, dot);
                } else {
                    setLabelTextEmojiSafe(msg, merged);
                }

                uint32_t replyPacketId = resolveReplyPacketId(rows, rowCount, i);

                bool isSelected = false;
                if (s_selectedMsgReplyPacketId != 0) {
                    isSelected = (replyPacketId == s_selectedMsgReplyPacketId);
                } else {
                    isSelected = (strcmp(rows[i]->text, s_selectedMsgText) == 0);
                }
                if (isSelected) {
#if defined(DEVICE_TDECK_PRO)
                    lv_obj_set_style_outline_width(msg, 2, 0);
                    lv_obj_set_style_outline_color(msg, lv_color_make(0, 0, 0), 0);
                    lv_obj_set_style_outline_opa(msg, LV_OPA_COVER, 0);
#else
                    lv_obj_set_style_bg_color(msg, lv_color_hex(0x2A4E8F), 0);
                    lv_obj_set_style_bg_opa(msg, LV_OPA_70, 0);
#endif
                    selectedMsgObj = msg;
                }

                lv_obj_add_flag(msg, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(msg, onChatMessagePressed,
                                    LV_EVENT_CLICKED,
                                    (void *)(uintptr_t)replyPacketId);
#if HAS_TOUCH
                // Hold opens Message Actions, same as the bubble style — and for
                // the same reason it carries the packet id rather than the
                // sender (see chatLineForPacketId).
                {
                    const uint32_t longPressSender = rows[i]->senderNodeId;
                    if (longPressSender != 0 && longPressSender != s_myNodeId
                        && replyPacketId != 0) {
                        lv_obj_add_event_cb(msg, onChatMessageLongPressed,
                                            LV_EVENT_LONG_PRESSED,
                                            (void *)(uintptr_t)replyPacketId);
                    }
                }
#endif

                if (hasDeco && deco.chipCount) {
#if defined(DEVICE_TDECK_PRO)
                    const lv_color_t chipColor = lv_color_make(0, 0, 0);
#else
                    const lv_color_t chipColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                                     ? lv_color_hex(0x16233A)
                                                     : lv_color_hex(0xE8F1FF);
#endif
                    lv_obj_t *chips = chatMakeReactionChips(s_chatList, deco,
                                                            scaledChatFont(kChannelChatFont),
                                                            chipColor);
#if defined(DEVICE_TDECK) || defined(DEVICE_M9)
                    lv_obj_set_style_margin_left(chips, 1, 0);
#else
                    lv_obj_set_style_margin_left(chips, 2, 0);
#endif
                    lastMsgObj = chips;
                }

                // One separator between messages, none after the last.
                if (n < endN) {
                    lv_obj_t *sep = lv_obj_create(s_chatList);
                    lv_obj_remove_style_all(sep);
                    lv_obj_set_size(sep, lv_pct(100), 1);
                    lv_obj_set_style_bg_color(sep, lv_color_hex(0x3F669F), 0);
                    lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
                }
            }
        }
    }

    // Wrapped labels do not have final heights until layout runs; scrolling
    // against stale geometry can land part-way through history intermittently.
    lv_obj_update_layout(s_chatList);

    // Scrollbar mode can alter effective content width, which changes wrapping
    // and total content height. Decide scrollbar visibility before the final
    // scroll placement so we scroll against stable geometry.
    bool overflow = lv_obj_get_scroll_bottom(s_chatList) > 0;
    lv_obj_set_scrollbar_mode(s_chatList, overflow ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);
    lv_obj_update_layout(s_chatList);

    if (s_chatWindowAnchorActive) {
        // Just loaded older history: pin the viewport to the message that was at
        // the top before, so the newly-prepended messages sit above it instead of
        // yanking the view to the top (which would immediately re-trigger a grow).
        s_chatWindowAnchorActive = false;
        if (anchorObj) {
            lv_obj_scroll_to_view(anchorObj, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(s_chatList, prevScrollY, LV_ANIM_OFF);
        }
    } else if (s_pagerChatCursorMode) {
        if (selectedMsgObj) {
            lv_obj_scroll_to_view(selectedMsgObj, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(s_chatList, prevScrollY, LV_ANIM_OFF);
        }
    } else if (stickToBottom) {
        if (lastMsgObj) {
            lv_obj_scroll_to_view(lastMsgObj, LV_ANIM_OFF);
        }
        // `scroll_to_view` only guarantees visibility; it may still leave a
        // small bottom gap. Clamp to absolute bottom so the next refresh keeps
        // treating this view as "at latest".
        int32_t bottomGap = lv_obj_get_scroll_bottom(s_chatList);
        if (bottomGap > 0) {
            // Was lv_obj_scroll_by(list, 0, +bottomGap), which scrolled the
            // wrong way: scroll_by's dy is inverted (lv_obj_scroll_to_y computes
            // diff = -y + scroll_y), so a positive dy reveals content *above*
            // and pushed the view further from the bottom rather than closing
            // the gap. Absolute positioning avoids the sign entirely.
            lv_obj_scroll_to_y(s_chatList,
                               lv_obj_get_scroll_y(s_chatList) + bottomGap,
                               LV_ANIM_OFF);
        }
    } else {
        lv_obj_scroll_to_y(s_chatList, prevScrollY, LV_ANIM_OFF);
    }

    s_chatRebuilding = false;
    s_chatScrollToLatestOnce = false;
    s_lastRenderedChannel = s_activeChannel;
    s_lastRenderedCount = ch.count;
    s_lastChatRenderedRevision = ch.revision;
    s_lastChatContextSignature = contextSig;
}

#if !UI_TOUCH_ONLY_PROFILE && !defined(DEVICE_TDECK_PRO)
// One string's width in the chat bar's face, on one line.
static lv_coord_t chatBarTextW(const char *text) {
    lv_point_t sz;
    lv_text_get_size(&sz, text, &lv_font_montserrat_10, 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    return (lv_coord_t)sz.x;
}

// How much of the chat shortcut bar's right end belongs to the status cluster —
// GPS, Wi-Fi, the DM envelope and the channel bell — and must therefore be kept
// out of the key-hint label's width.
//
// Measured, not written down. This was a constant (74 px), and when the channel
// bell joined the cluster in v5.1.0 the constant did not move with it: the
// label's box then reached ~9 px under the bell, and on every board with an
// unread channel the two drew on top of each other. A number derived from the
// glyphs cannot fall behind them, so the next icon added to that row costs
// nothing here.
//
// The arithmetic mirrors where the widgets below actually go: the label starts
// pad_left + 2 in from the left edge, the GPS icon ends 4 px inside the content
// area (which pad_right has already inset), and each icon to its left hangs off
// its neighbour by a fixed gap.
static lv_coord_t bottomStatusReserveW() {
    // The widest the GPS label gets: the satellite count is drawn beside the
    // glyph, and two digits is as many as a receiver reports in the open.
    const lv_coord_t cluster = chatBarTextW(LV_SYMBOL_GPS " 99")
                             + 7 + chatBarTextW(LV_SYMBOL_WIFI)
                             + 5 + chatBarTextW(LV_SYMBOL_ENVELOPE)
                             + 4 + chatBarTextW(LV_SYMBOL_BELL);
    // 4 px of pad_left plus the label's own 2 px align offset at one end; 4 px
    // of pad_right plus the GPS icon's 4 px inset at the other; then 4 px of
    // slack, so a rounding difference is a gap rather than a collision.
    return (lv_coord_t)(6 + 8 + cluster + 4);
}
#endif

#if !UI_TOUCH_ONLY_PROFILE
// The footer's key-hint line for this board. A function rather than a literal
// set once at build time, because the home dashboard borrows that label for
// its notification ticker and has to be able to put the hints back.
//
// Each of these is sized to the panel it belongs to, which is why they are
// separate strings rather than one with a board's name substituted in. Where
// HAS_HOME_DASHBOARD is set, H and C changed destinations and F is new, so the
// hints changed with them; the channel list lost its own letter, because it is
// C's second press now.
static const char *chatShortcutHintText() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return "C(h)annels   (A)ctions";
#elif defined(DEVICE_TLORA_PAGER_TFT)
#if HAS_HOME_DASHBOARD
    // The widest bar of any board, but worded like the 320 px one below: there
    // is no reason for two builds to name the same destinations differently,
    // and the short forms leave this line real headroom (262 px of the 283
    // its 373 px bar has) instead of the 1 px the spelled-out version had.
    return "(H)ome   (C)hat   C(f)g   (D)M   (N)odes   Too(l)s   (A)ct";
#else
    return TR("(C)FG   (D)M   (N)odes   Too(l)s   (A)ctions");
#endif
#elif defined(DEVICE_TDECK_PRO)
#if HAS_HOME_DASHBOARD
    // ~40 characters is what 228 px of montserrat_10 holds, and seven
    // destinations do not fit in it. Actions is the one dropped: it is the only
    // entry here that is not also a cell on the nav bar this line replaces, and
    // A still works. Only visible with the bar switched off.
    return "H:Home C:Chat F:Cfg D:DM N:Node L:Tools";
#else
    return TR("C:Cfg H:Ch D:DM N:Node L:Tools A:Act");
#endif
#elif HAS_HOME_DASHBOARD
    // 204 px of the 218 this board has for it. The 5.1.0 wording — "con(f)ig",
    // and Actions still on the end — measured 245 px against 234 and wrapped
    // onto a second row the 14 px bar has no room for. It passed review because
    // it was checked against the line it replaced by character count (both 49)
    // rather than by width: the old one was mostly spaces at 2.7 px each, and
    // trading twelve of them for six letters cost ~22 px at the same length.
    //
    // Actions is the token dropped rather than another: it is the only entry
    // here that is not also a cell on the nav bar this line stands in for, A
    // still works, and the T-Deck Pro's line already drops it for that reason.
    return TR("(H)ome (C)hat C(f)g (D)M (N)odes Too(l)s");
#else
    // Double spaces, not triple: this is the pre-dashboard wording, and at
    // three it measured 223 px — inside the old 234 budget but over the 218
    // the corrected reserve leaves. Only built where the dashboard is compiled
    // out, which is no current board, but a line that wraps the moment it is
    // switched back on is not worth leaving behind.
    return TR("(C)FG  C(h)an  (D)M  (N)odes  Too(l)s  (A)ct");
#endif
}
#endif   // !UI_TOUCH_ONLY_PROFILE

#if defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
// The bottom bar's height on the pixel-doubled P4. Portrait's 63 and landscape's
// 42 are sized for a thumb; landscape with the keyboard expansion on is driven
// from the keys, which the F-key beside each glyph names, so the bar gives most
// of that height back to the chat. 30 still clears the 18 px glyph by a few
// pixels top and bottom. The side column (Nav Alignment Left/Right) is not
// affected: it is sized by the screen's height, not by this.
static int p4NavBarHeight() {
    if (uiPortrait()) return 63;
    return keyboardAttached() ? 30 : 42;
}
#endif

