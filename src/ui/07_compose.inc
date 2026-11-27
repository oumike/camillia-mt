// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: message selection, accent picker, compose prompt, emoji picker.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// Drop the whole selection — reply target, preview text and sender together.
// They are one selection and have to be cleared as one; forgetting the sender
// here is what would leave Enter opening an action menu for a message that is
// no longer highlighted.
static inline void clearSelectedMsgContext() {
    setSelectedReplyContext(0, nullptr, 0);
}

static void collectChatRows(const DisplayLine **rows, int &rowCount) {
    rowCount = 0;
    if (!rows) return;

    for (int row = 0; row < MAX_MSG_LINES && rowCount < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        if (shouldHideChatLine(dl->text)) continue;
        if (dl->reaction) continue;   // tallied as chips under the original
        rows[rowCount++] = dl;
    }
}

static void buildChatDisplayOrder(const DisplayLine *const *rows, int rowCount,
                                  int *displayOrder, int &displayCount) {
    displayCount = 0;
    if (!rows || !displayOrder || rowCount <= 0) return;

    for (int i = rowCount - 1; i >= 0 && displayCount < MAX_MSG_LINES;) {
        if (rows[i] && rows[i]->packetId != 0) {
            int newestInGroup = i;
            while (newestInGroup - 1 >= 0
                   && rows[newestInGroup - 1]
                   && rows[newestInGroup - 1]->packetId == rows[i]->packetId) {
                newestInGroup--;
            }
            for (int k = newestInGroup; k <= i && displayCount < MAX_MSG_LINES; k++) {
                displayOrder[displayCount++] = k;
            }
            i = newestInGroup - 1;
        } else {
            displayOrder[displayCount++] = i;
            i--;
        }
    }
}

// Build cursor anchors so chat keyboard navigation moves by logical message,
// not by individual wrapped line rows.
static void buildChatCursorOrder(const DisplayLine *const *rows,
                                 const int *displayOrder, int displayCount,
                                 int *cursorOrder, int &cursorCount) {
    cursorCount = 0;
    if (!rows || !displayOrder || !cursorOrder || displayCount <= 0) return;

    for (int n = 0; n < displayCount && cursorCount < MAX_MSG_LINES;) {
        int anchor = displayOrder[n];
        if (!rows[anchor]) {
            n++;
            continue;
        }
        cursorOrder[cursorCount++] = anchor;

        uint32_t packetId = rows[anchor]->packetId;
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
            if (!(t[0] == ' ' && t[1] == ' ')) break;
            n++;
        }
    }
}

static bool pagerSelectChatCursorIndex(int displayIndex) {
    const DisplayLine *rows[MAX_MSG_LINES] = {};
    int rowCount = 0;
    collectChatRows(rows, rowCount);

    int displayOrder[MAX_MSG_LINES] = {};
    int displayCount = 0;
    buildChatDisplayOrder(rows, rowCount, displayOrder, displayCount);

    int cursorOrder[MAX_MSG_LINES] = {};
    int cursorCount = 0;
    buildChatCursorOrder(rows, displayOrder, displayCount, cursorOrder, cursorCount);

    if (cursorCount <= 0) {
        s_pagerChatCursorDisplayIndex = -1;
        clearSelectedMsgContext();
        s_lastRenderedChannel = -1;
        return false;
    }

    if (displayIndex < 0) displayIndex = cursorCount - 1;
    if (displayIndex >= cursorCount) displayIndex = cursorCount - 1;

    s_pagerChatCursorDisplayIndex = displayIndex;
    int rowIdx = cursorOrder[displayIndex];
    uint32_t replyPacketId = resolveReplyPacketId(rows, rowCount, rowIdx);
    const char *txt = rows[rowIdx] ? rows[rowIdx]->text : "";
    // senderNodeId is 0 on local/system lines, which is exactly the "no menu"
    // signal the Enter handler wants, so it is passed through as-is.
    uint32_t senderNodeId = rows[rowIdx] ? rows[rowIdx]->senderNodeId : 0;
    setSelectedReplyContext(replyPacketId, txt, senderNodeId);

    s_lastRenderedChannel = -1;
    return true;
}

static void pagerExitChatCursorMode(bool clearSelection) {
    s_pagerChatCursorMode = false;
    s_pagerChatCursorDisplayIndex = -1;
    s_chatScrollToLatestOnce = true;
    // Cursor mode runs its own window centred on the cursor; on exit we should
    // not reuse any pending window-growth anchor from an earlier scroll-back pass.
    s_chatWindowGrowPending = false;
    s_chatWindowAnchorActive = false;
    if (clearSelection) {
        clearSelectedMsgContext();
    }
    s_lastRenderedChannel = -1;
}

// ── Accent suggestions (issue #99) ───────────────────────────────────────────
// With a language other than English selected, typing a letter that has
// accented forms in that language pops a small row of them just above the
// field. Tapping one replaces the letter just typed. Keyboard-only boards step
// through the row with Tab or the trackball/wheel/arrows, which swap the letter
// in place as they go -- the box is a menu of what the next step will put
// there, so the text is already right when the box goes away. Enter keeps it
// and closes the box; any other key just carries on typing.
//
// Per language rather than one big table: a Spanish writer has no use for ă,
// and a row of nine variants does not fit a 240 px screen anyway. Each list is
// the letters that language actually writes, commonest first. Every glyph here
// is in the Latin fonts (tools/gen_latin_fonts.sh).
static constexpr uint8_t kAccentMax = 4;   // the longest list below
struct AccentSet { char base; const char *v[kAccentMax]; };   // unused slots null

#define ACCENT_SET(ch, ...) { ch, { __VA_ARGS__ } }

static const AccentSet kAccentsEs[] = {
    ACCENT_SET('a', "\xC3\xA1"), ACCENT_SET('A', "\xC3\x81"),
    ACCENT_SET('e', "\xC3\xA9"), ACCENT_SET('E', "\xC3\x89"),
    ACCENT_SET('i', "\xC3\xAD"), ACCENT_SET('I', "\xC3\x8D"),
    ACCENT_SET('o', "\xC3\xB3"), ACCENT_SET('O', "\xC3\x93"),
    ACCENT_SET('u', "\xC3\xBA", "\xC3\xBC"), ACCENT_SET('U', "\xC3\x9A", "\xC3\x9C"),
    ACCENT_SET('n', "\xC3\xB1"), ACCENT_SET('N', "\xC3\x91"),
    ACCENT_SET('?', "\xC2\xBF"), ACCENT_SET('!', "\xC2\xA1"),
};
static const AccentSet kAccentsFr[] = {
    ACCENT_SET('a', "\xC3\xA0", "\xC3\xA2", "\xC3\xA6"),
    ACCENT_SET('A', "\xC3\x80", "\xC3\x82", "\xC3\x86"),
    ACCENT_SET('e', "\xC3\xA9", "\xC3\xA8", "\xC3\xAA", "\xC3\xAB"),
    ACCENT_SET('E', "\xC3\x89", "\xC3\x88", "\xC3\x8A", "\xC3\x8B"),
    ACCENT_SET('i', "\xC3\xAE", "\xC3\xAF"), ACCENT_SET('I', "\xC3\x8E", "\xC3\x8F"),
    ACCENT_SET('o', "\xC3\xB4", "\xC5\x93"), ACCENT_SET('O', "\xC3\x94", "\xC5\x92"),
    ACCENT_SET('u', "\xC3\xB9", "\xC3\xBB", "\xC3\xBC"),
    ACCENT_SET('U', "\xC3\x99", "\xC3\x9B", "\xC3\x9C"),
    ACCENT_SET('c', "\xC3\xA7"), ACCENT_SET('C', "\xC3\x87"),
    ACCENT_SET('y', "\xC3\xBF"), ACCENT_SET('Y', "\xC5\xB8"),
};
// Comma-below ș ț (U+0219/021B), the correct Romanian forms, not the cedilla
// ş ţ that older keyboards substitute.
static const AccentSet kAccentsRo[] = {
    ACCENT_SET('a', "\xC4\x83", "\xC3\xA2"), ACCENT_SET('A', "\xC4\x82", "\xC3\x82"),
    ACCENT_SET('i', "\xC3\xAE"), ACCENT_SET('I', "\xC3\x8E"),
    ACCENT_SET('s', "\xC8\x99"), ACCENT_SET('S', "\xC8\x98"),
    ACCENT_SET('t', "\xC8\x9B"), ACCENT_SET('T', "\xC8\x9A"),
};
static const AccentSet kAccentsIt[] = {
    ACCENT_SET('a', "\xC3\xA0"), ACCENT_SET('A', "\xC3\x80"),
    ACCENT_SET('e', "\xC3\xA8", "\xC3\xA9"), ACCENT_SET('E', "\xC3\x88", "\xC3\x89"),
    ACCENT_SET('i', "\xC3\xAC", "\xC3\xAD", "\xC3\xAE"),
    ACCENT_SET('I', "\xC3\x8C", "\xC3\x8D", "\xC3\x8E"),
    ACCENT_SET('o', "\xC3\xB2", "\xC3\xB3"), ACCENT_SET('O', "\xC3\x92", "\xC3\x93"),
    ACCENT_SET('u', "\xC3\xB9", "\xC3\xBA"), ACCENT_SET('U', "\xC3\x99", "\xC3\x9A"),
};
// UTF-8 as-is from here on, like kUiLangNames: every letter is in the Latin fonts.
static const AccentSet kAccentsDe[] = {
    ACCENT_SET('a', "ä"),
    ACCENT_SET('A', "Ä"),
    ACCENT_SET('o', "ö"),
    ACCENT_SET('O', "Ö"),
    ACCENT_SET('u', "ü"),
    ACCENT_SET('U', "Ü"),
    ACCENT_SET('s', "ß"),
};
static const AccentSet kAccentsPt[] = {
    ACCENT_SET('a', "ã", "á", "â", "à"),
    ACCENT_SET('A', "Ã", "Á", "Â", "À"),
    ACCENT_SET('e', "é", "ê"),
    ACCENT_SET('E', "É", "Ê"),
    ACCENT_SET('i', "í"),
    ACCENT_SET('I', "Í"),
    ACCENT_SET('o', "õ", "ó", "ô"),
    ACCENT_SET('O', "Õ", "Ó", "Ô"),
    ACCENT_SET('u', "ú"),
    ACCENT_SET('U', "Ú"),
    ACCENT_SET('c', "ç"),
    ACCENT_SET('C', "Ç"),
};
static const AccentSet kAccentsPl[] = {
    ACCENT_SET('a', "ą"),
    ACCENT_SET('A', "Ą"),
    ACCENT_SET('c', "ć"),
    ACCENT_SET('C', "Ć"),
    ACCENT_SET('e', "ę"),
    ACCENT_SET('E', "Ę"),
    ACCENT_SET('l', "ł"),
    ACCENT_SET('L', "Ł"),
    ACCENT_SET('n', "ń"),
    ACCENT_SET('N', "Ń"),
    ACCENT_SET('o', "ó"),
    ACCENT_SET('O', "Ó"),
    ACCENT_SET('s', "ś"),
    ACCENT_SET('S', "Ś"),
    ACCENT_SET('z', "ż", "ź"),
    ACCENT_SET('Z', "Ż", "Ź"),
};
static const AccentSet kAccentsNl[] = {
    ACCENT_SET('e', "é", "ë", "è"),
    ACCENT_SET('E', "É", "Ë", "È"),
    ACCENT_SET('i', "ï"),
    ACCENT_SET('I', "Ï"),
    ACCENT_SET('o', "ó", "ö"),
    ACCENT_SET('O', "Ó", "Ö"),
    ACCENT_SET('u', "ü"),
    ACCENT_SET('U', "Ü"),
};
static const AccentSet kAccentsCs[] = {
    ACCENT_SET('a', "á"),
    ACCENT_SET('A', "Á"),
    ACCENT_SET('c', "č"),
    ACCENT_SET('C', "Č"),
    ACCENT_SET('d', "ď"),
    ACCENT_SET('D', "Ď"),
    ACCENT_SET('e', "é", "ě"),
    ACCENT_SET('E', "É", "Ě"),
    ACCENT_SET('i', "í"),
    ACCENT_SET('I', "Í"),
    ACCENT_SET('n', "ň"),
    ACCENT_SET('N', "Ň"),
    ACCENT_SET('o', "ó"),
    ACCENT_SET('O', "Ó"),
    ACCENT_SET('r', "ř"),
    ACCENT_SET('R', "Ř"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('t', "ť"),
    ACCENT_SET('T', "Ť"),
    ACCENT_SET('u', "ú", "ů"),
    ACCENT_SET('U', "Ú", "Ů"),
    ACCENT_SET('y', "ý"),
    ACCENT_SET('Y', "Ý"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
static const AccentSet kAccentsSv[] = {
    ACCENT_SET('a', "å", "ä"),
    ACCENT_SET('A', "Å", "Ä"),
    ACCENT_SET('o', "ö"),
    ACCENT_SET('O', "Ö"),
};
static const AccentSet kAccentsNb[] = {
    ACCENT_SET('a', "å", "æ"),
    ACCENT_SET('A', "Å", "Æ"),
    ACCENT_SET('o', "ø"),
    ACCENT_SET('O', "Ø"),
    ACCENT_SET('e', "é"),
    ACCENT_SET('E', "É"),
};
static const AccentSet kAccentsDa[] = {
    ACCENT_SET('a', "å", "æ"),
    ACCENT_SET('A', "Å", "Æ"),
    ACCENT_SET('o', "ø"),
    ACCENT_SET('O', "Ø"),
    ACCENT_SET('e', "é"),
    ACCENT_SET('E', "É"),
};
static const AccentSet kAccentsFi[] = {
    ACCENT_SET('a', "ä", "å"),
    ACCENT_SET('A', "Ä", "Å"),
    ACCENT_SET('o', "ö"),
    ACCENT_SET('O', "Ö"),
};
static const AccentSet kAccentsHu[] = {
    ACCENT_SET('a', "á"),
    ACCENT_SET('A', "Á"),
    ACCENT_SET('e', "é"),
    ACCENT_SET('E', "É"),
    ACCENT_SET('i', "í"),
    ACCENT_SET('I', "Í"),
    ACCENT_SET('o', "ó", "ö", "ő"),
    ACCENT_SET('O', "Ó", "Ö", "Ő"),
    ACCENT_SET('u', "ú", "ü", "ű"),
    ACCENT_SET('U', "Ú", "Ü", "Ű"),
};
static const AccentSet kAccentsSk[] = {
    ACCENT_SET('a', "á", "ä"),
    ACCENT_SET('A', "Á", "Ä"),
    ACCENT_SET('c', "č"),
    ACCENT_SET('C', "Č"),
    ACCENT_SET('d', "ď"),
    ACCENT_SET('D', "Ď"),
    ACCENT_SET('e', "é"),
    ACCENT_SET('E', "É"),
    ACCENT_SET('i', "í"),
    ACCENT_SET('I', "Í"),
    ACCENT_SET('l', "ľ", "ĺ"),
    ACCENT_SET('L', "Ľ", "Ĺ"),
    ACCENT_SET('n', "ň"),
    ACCENT_SET('N', "Ň"),
    ACCENT_SET('o', "ó", "ô"),
    ACCENT_SET('O', "Ó", "Ô"),
    ACCENT_SET('r', "ŕ"),
    ACCENT_SET('R', "Ŕ"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('t', "ť"),
    ACCENT_SET('T', "Ť"),
    ACCENT_SET('u', "ú"),
    ACCENT_SET('U', "Ú"),
    ACCENT_SET('y', "ý"),
    ACCENT_SET('Y', "Ý"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
static const AccentSet kAccentsHr[] = {
    ACCENT_SET('c', "č", "ć"),
    ACCENT_SET('C', "Č", "Ć"),
    ACCENT_SET('d', "đ"),
    ACCENT_SET('D', "Đ"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
static const AccentSet kAccentsSl[] = {
    ACCENT_SET('c', "č"),
    ACCENT_SET('C', "Č"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
static const AccentSet kAccentsTr[] = {
    ACCENT_SET('c', "ç"),
    ACCENT_SET('C', "Ç"),
    ACCENT_SET('g', "ğ"),
    ACCENT_SET('G', "Ğ"),
    ACCENT_SET('i', "ı"),
    ACCENT_SET('I', "İ"),
    ACCENT_SET('o', "ö"),
    ACCENT_SET('O', "Ö"),
    ACCENT_SET('s', "ş"),
    ACCENT_SET('S', "Ş"),
    ACCENT_SET('u', "ü"),
    ACCENT_SET('U', "Ü"),
};
static const AccentSet kAccentsLt[] = {
    ACCENT_SET('a', "ą"),
    ACCENT_SET('A', "Ą"),
    ACCENT_SET('c', "č"),
    ACCENT_SET('C', "Č"),
    ACCENT_SET('e', "ę", "ė"),
    ACCENT_SET('E', "Ę", "Ė"),
    ACCENT_SET('i', "į"),
    ACCENT_SET('I', "Į"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('u', "ų", "ū"),
    ACCENT_SET('U', "Ų", "Ū"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
static const AccentSet kAccentsLv[] = {
    ACCENT_SET('a', "ā"),
    ACCENT_SET('A', "Ā"),
    ACCENT_SET('c', "č"),
    ACCENT_SET('C', "Č"),
    ACCENT_SET('e', "ē"),
    ACCENT_SET('E', "Ē"),
    ACCENT_SET('g', "ģ"),
    ACCENT_SET('G', "Ģ"),
    ACCENT_SET('i', "ī"),
    ACCENT_SET('I', "Ī"),
    ACCENT_SET('k', "ķ"),
    ACCENT_SET('K', "Ķ"),
    ACCENT_SET('l', "ļ"),
    ACCENT_SET('L', "Ļ"),
    ACCENT_SET('n', "ņ"),
    ACCENT_SET('N', "Ņ"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('u', "ū"),
    ACCENT_SET('U', "Ū"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
static const AccentSet kAccentsEt[] = {
    ACCENT_SET('a', "ä"),
    ACCENT_SET('A', "Ä"),
    ACCENT_SET('o', "õ", "ö"),
    ACCENT_SET('O', "Õ", "Ö"),
    ACCENT_SET('u', "ü"),
    ACCENT_SET('U', "Ü"),
    ACCENT_SET('s', "š"),
    ACCENT_SET('S', "Š"),
    ACCENT_SET('z', "ž"),
    ACCENT_SET('Z', "Ž"),
};
#undef ACCENT_SET

static const AccentSet *accentSetFor(char c) {
    const AccentSet *sets = nullptr;
    size_t count = 0;
    switch (i18nGetLang()) {
        case LANG_ES: sets = kAccentsEs; count = sizeof(kAccentsEs) / sizeof(kAccentsEs[0]); break;
        case LANG_FR: sets = kAccentsFr; count = sizeof(kAccentsFr) / sizeof(kAccentsFr[0]); break;
        case LANG_RO: sets = kAccentsRo; count = sizeof(kAccentsRo) / sizeof(kAccentsRo[0]); break;
        case LANG_IT: sets = kAccentsIt; count = sizeof(kAccentsIt) / sizeof(kAccentsIt[0]); break;
        case LANG_DE: sets = kAccentsDe; count = sizeof(kAccentsDe) / sizeof(kAccentsDe[0]); break;
        case LANG_PT: sets = kAccentsPt; count = sizeof(kAccentsPt) / sizeof(kAccentsPt[0]); break;
        case LANG_PL: sets = kAccentsPl; count = sizeof(kAccentsPl) / sizeof(kAccentsPl[0]); break;
        case LANG_NL: sets = kAccentsNl; count = sizeof(kAccentsNl) / sizeof(kAccentsNl[0]); break;
        case LANG_CS: sets = kAccentsCs; count = sizeof(kAccentsCs) / sizeof(kAccentsCs[0]); break;
        case LANG_SV: sets = kAccentsSv; count = sizeof(kAccentsSv) / sizeof(kAccentsSv[0]); break;
        case LANG_NB: sets = kAccentsNb; count = sizeof(kAccentsNb) / sizeof(kAccentsNb[0]); break;
        case LANG_DA: sets = kAccentsDa; count = sizeof(kAccentsDa) / sizeof(kAccentsDa[0]); break;
        case LANG_FI: sets = kAccentsFi; count = sizeof(kAccentsFi) / sizeof(kAccentsFi[0]); break;
        case LANG_HU: sets = kAccentsHu; count = sizeof(kAccentsHu) / sizeof(kAccentsHu[0]); break;
        case LANG_SK: sets = kAccentsSk; count = sizeof(kAccentsSk) / sizeof(kAccentsSk[0]); break;
        case LANG_HR: sets = kAccentsHr; count = sizeof(kAccentsHr) / sizeof(kAccentsHr[0]); break;
        case LANG_SL: sets = kAccentsSl; count = sizeof(kAccentsSl) / sizeof(kAccentsSl[0]); break;
        case LANG_TR: sets = kAccentsTr; count = sizeof(kAccentsTr) / sizeof(kAccentsTr[0]); break;
        case LANG_LT: sets = kAccentsLt; count = sizeof(kAccentsLt) / sizeof(kAccentsLt[0]); break;
        case LANG_LV: sets = kAccentsLv; count = sizeof(kAccentsLv) / sizeof(kAccentsLv[0]); break;
        case LANG_ET: sets = kAccentsEt; count = sizeof(kAccentsEt) / sizeof(kAccentsEt[0]); break;
        default: return nullptr;   // English: no box at all
    }
    for (size_t i = 0; i < count; i++) {
        if (sets[i].base == c) return &sets[i];
    }
    return nullptr;
}

static lv_obj_t *s_accentBox = nullptr;
static lv_obj_t *s_accentTa = nullptr;     // the field the box edits
static lv_obj_t *s_accentCells[kAccentMax + 1] = {};
static const AccentSet *s_accentSet = nullptr;
static char s_accentBase[2] = {};
static int s_accentIdx = 0;                // 0 = the plain letter, i = v[i-1]
static uint32_t s_accentPrevLen = 0;       // s_accentTa's byte length last seen
static bool s_accentEditing = false;       // our own edit: ignore its VALUE_CHANGED

static int accentOptionCount(const AccentSet *set) {
    int n = 1;   // the plain letter
    while (set && n <= kAccentMax && set->v[n - 1]) n++;
    return n;
}

static const char *accentOption(int idx) {
    if (!s_accentSet || idx <= 0) return s_accentBase;
    return s_accentSet->v[idx - 1];
}

static void accentBoxHide() {
    // Async: a tap on one of its own cells is what closes it, and LVGL must
    // not free the object whose event it is still dispatching.
    if (lvObjValid(s_accentBox)) lv_obj_delete_async(s_accentBox);
    s_accentBox = nullptr;
    s_accentSet = nullptr;
    for (auto &c : s_accentCells) c = nullptr;
}

static bool accentBoxVisible() {
    return s_accentSet && lvObjValid(s_accentBox) && lvObjValid(s_accentTa);
}

static void accentBoxHighlight() {
    for (int i = 0; i <= kAccentMax; i++) {
        if (!s_accentCells[i]) continue;
        const bool sel = (i == s_accentIdx);
        lv_obj_set_style_bg_color(s_accentCells[i],
                                  themedColorHex(sel ? 0x2F6BD8 : 0x16386F), 0);
        lv_obj_set_style_border_width(s_accentCells[i], sel ? 2 : 0, 0);
    }
}

// Swap the character before the cursor for option `idx`. The character there
// is always the base letter or one of its variants: the box closes on any
// other edit.
static void accentReplaceWith(int idx) {
    if (!accentBoxVisible()) return;
    s_accentEditing = true;
    lv_textarea_delete_char(s_accentTa);
    lv_textarea_add_text(s_accentTa, accentOption(idx));
    s_accentEditing = false;
    const char *t = lv_textarea_get_text(s_accentTa);
    s_accentPrevLen = t ? (uint32_t)strlen(t) : 0;
    s_accentIdx = idx;
    if (s_accentTa == s_composeInput) updateComposeCharCount();
    accentBoxHighlight();
}

static void onAccentCellClicked(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    accentReplaceWith(idx);
    accentBoxHide();
}

static void accentBoxShow(lv_obj_t *ta, const AccentSet *set, char base) {
    accentBoxHide();
    if (!s_rootScreen || !set || !set->v[0]) return;
    s_accentSet = set;
    s_accentBase[0] = base;
    s_accentBase[1] = '\0';
    s_accentIdx = 0;

    const lv_font_t *font = &lv_font_montserrat_16;
    const int cellW = 30, cellH = 30, gap = 4, pad = 4;
    const int n = accentOptionCount(set);

    s_accentBox = lv_obj_create(s_rootScreen);
    lv_obj_remove_style_all(s_accentBox);
    lv_obj_clear_flag(s_accentBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_accentBox, themedColorHex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_accentBox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_accentBox, 6, 0);
    lv_obj_set_style_border_width(s_accentBox, 1, 0);
    lv_obj_set_style_border_color(s_accentBox, lv_color_hex(0x8FB5E6), 0);
    lv_obj_set_style_pad_all(s_accentBox, pad, 0);
    lv_obj_set_style_pad_column(s_accentBox, gap, 0);
    lv_obj_set_flex_flow(s_accentBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(s_accentBox, n * cellW + (n - 1) * gap + pad * 2, cellH + pad * 2);

    // The plain letter first, so the row reads "what you typed, then what it
    // could be" and a tap on it is a way to say no.
    for (int i = 0; i < n; i++) {
        lv_obj_t *cell = lv_btn_create(s_accentBox);
        lv_obj_set_size(cell, cellW, cellH);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0xE8F1FF), 0);
        lv_obj_add_event_cb(cell, onAccentCellClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(cell);
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
        lv_label_set_text(lbl, accentOption(i));
        lv_obj_center(lbl);
        s_accentCells[i] = cell;
    }
    accentBoxHighlight();

    // Just above the field, centred on the screen; below it when the field is
    // too close to the top. Coordinates are relative to the root screen, which
    // is what the box is a child of.
    lv_obj_update_layout(s_accentBox);
    lv_area_t fa, ra;
    lv_obj_get_coords(ta, &fa);
    lv_obj_get_coords(s_rootScreen, &ra);
    const int32_t bw = lv_obj_get_width(s_accentBox);
    const int32_t bh = lv_obj_get_height(s_accentBox);
    const int32_t rw = lv_obj_get_width(s_rootScreen);
    int32_t y = fa.y1 - ra.y1 - bh - 4;
    if (y < 0) y = fa.y2 - ra.y1 + 4;
    lv_obj_set_pos(s_accentBox, (rw - bw) / 2, y);
    lv_obj_move_foreground(s_accentBox);
}

// Byte offset of the textarea cursor. The cursor position is in characters.
static uint32_t accentCursorByte(lv_obj_t *ta, const char *t) {
    uint32_t chars = lv_textarea_get_cursor_pos(ta);
    uint32_t i = 0;
    while (t[i] && chars > 0) {
        i++;
        while (t[i] && utf8util::isContinuationByte((uint8_t)t[i])) i++;
        chars--;
    }
    return i;
}

static void onAccentFieldChanged(lv_event_t *e) {
    if (s_accentEditing) return;
    lv_obj_t *ta = lv_event_get_target_obj(e);
    const char *t = lv_textarea_get_text(ta);
    const uint32_t len = t ? (uint32_t)strlen(t) : 0;
    const uint32_t prevLen = (ta == s_accentTa) ? s_accentPrevLen : len;
    s_accentTa = ta;
    s_accentPrevLen = len;
    accentBoxHide();
    // Exactly one ASCII byte more than last time: one letter typed. A paste,
    // an emoji or a delete never opens the box.
    if (len != prevLen + 1) return;
    const uint32_t cur = accentCursorByte(ta, t);
    if (cur == 0) return;
    const char c = t[cur - 1];
    if ((uint8_t)c < 0x20 || (uint8_t)c >= 0x7F) return;
    const AccentSet *set = accentSetFor(c);
    if (set) accentBoxShow(ta, set, c);
}

static void onAccentFieldFocus(lv_event_t *e) {
    // Re-baseline on focus so the first key typed into a pre-filled field
    // (a node name, a draft) is judged against what is actually there.
    lv_obj_t *ta = lv_event_get_target_obj(e);
    const char *t = lv_textarea_get_text(ta);
    s_accentTa = ta;
    s_accentPrevLen = t ? (uint32_t)strlen(t) : 0;
}

static void onAccentFieldDeleted(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_accentTa) return;
    accentBoxHide();
    s_accentTa = nullptr;
    s_accentPrevLen = 0;
}

// Give a text field accent suggestions. For fields people write words in --
// messages and names -- not passwords, keys or filters.
static void accentAttach(lv_obj_t *ta) {
    if (!ta) return;
    const char *t = lv_textarea_get_text(ta);
    s_accentTa = ta;
    s_accentPrevLen = t ? (uint32_t)strlen(t) : 0;
    lv_obj_add_event_cb(ta, onAccentFieldChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(ta, onAccentFieldFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, onAccentFieldDeleted, LV_EVENT_DELETE, nullptr);
}

// Physical keys while the box is up. Returns true when the key was the box's.
static bool accentHandleKey(char k) {
    if (!accentBoxVisible()) return false;
    const int n = accentOptionCount(s_accentSet);
    switch (k) {
        case KEY_TAB:
        case KEY_SCROLL_DN:
        case KEY_NEXT_CHAN:
            accentReplaceWith((s_accentIdx + 1) % n);
            return true;
        case KEY_SCROLL_UP:
        case KEY_PREV_CHAN:
            accentReplaceWith((s_accentIdx + n - 1) % n);
            return true;
        case KEY_ENTER:
        case KEY_ROLLER:
            // Only when the box has been used. Untouched, Enter is still Send.
            if (s_accentIdx == 0) {
                accentBoxHide();
                return false;
            }
            accentBoxHide();
            return true;
        default:
            // Anything else: the choice stands and the key does its own job.
            // Typing reopens the box for the next letter if it has accents.
            accentBoxHide();
            return false;
    }
}

// ── Spell suggestions ─────────────────────────────────────────────────────────
// The English counterpart of the accent box, in the same place and driven the
// same way. Finish a word -- a space or punctuation after it -- and if the word
// is not in the list, a row of what it might have been opens above the field.
// The word as typed is the first cell, so the row reads "what you wrote, or one
// of these" and leaving it alone changes nothing. Tab and the trackball/wheel
// step through the row, swapping the word in place; Enter keeps the choice
// (untouched, Enter is still Send); any other key closes the row and does its
// own job, so typing straight on is never slowed down.
//
// English only, because the list is -- which is also exactly when the accent
// box is never up, so the two never want the same keys. Message fields only:
// names, passwords and keys are not dictionary words.
#if HAS_SPELLCHECK
static lv_obj_t *s_spellBox = nullptr;
static lv_obj_t *s_spellTa = nullptr;      // the field the row edits
static lv_obj_t *s_spellCells[spell::kMaxSuggest + 1] = {};
static char s_spellOpts[spell::kMaxSuggest + 1][spell::kMaxWord + 1] = {};
static int s_spellCount = 0;               // options, the typed word included
static int s_spellIdx = 0;                 // 0 = as typed, i = suggestion i
static uint32_t s_spellWordByte = 0;       // where the word starts, in bytes
static uint32_t s_spellWordChar = 0;       // ... and in characters
static uint32_t s_spellPrevLen = 0;        // s_spellTa's byte length last seen
static bool s_spellEditing = false;        // our own edit: ignore its VALUE_CHANGED

static void spellBoxHide() {
    // Async for the same reason as the accent box: a tap on a cell closes it.
    if (lvObjValid(s_spellBox)) lv_obj_delete_async(s_spellBox);
    s_spellBox = nullptr;
    s_spellCount = 0;
    for (auto &c : s_spellCells) c = nullptr;
}

static bool spellBoxVisible() {
    return s_spellCount > 0 && lvObjValid(s_spellBox) && lvObjValid(s_spellTa);
}

static void spellBoxHighlight() {
    for (int i = 0; i < s_spellCount; i++) {
        if (!s_spellCells[i]) continue;
        const bool sel = (i == s_spellIdx);
        lv_obj_set_style_bg_color(s_spellCells[i],
                                  themedColorHex(sel ? 0x2F6BD8 : 0x16386F), 0);
        lv_obj_set_style_border_width(s_spellCells[i], sel ? 2 : 0, 0);
    }
}

// Swap the word for option `idx`. The word is just behind the space or
// punctuation that opened the row; if it is no longer there (a tap moved the
// caret and something else changed it), the row has nothing left to edit.
static void spellReplaceWith(int idx) {
    if (!spellBoxVisible() || idx == s_spellIdx) return;
    const char *t = lv_textarea_get_text(s_spellTa);
    const char *cur = s_spellOpts[s_spellIdx];
    const size_t curLen = strlen(cur);
    if (!t || strlen(t) < s_spellWordByte + curLen
        || memcmp(t + s_spellWordByte, cur, curLen) != 0) {
        spellBoxHide();
        return;
    }
    const char *next = s_spellOpts[idx];
    s_spellEditing = true;
    lv_textarea_set_cursor_pos(s_spellTa, (int32_t)(s_spellWordChar + curLen));
    for (size_t i = 0; i < curLen; i++) lv_textarea_delete_char(s_spellTa);
    lv_textarea_add_text(s_spellTa, next);
    // Back past the space or punctuation, where the caret was.
    lv_textarea_set_cursor_pos(s_spellTa, (int32_t)(s_spellWordChar + strlen(next) + 1));
    s_spellEditing = false;
    t = lv_textarea_get_text(s_spellTa);
    s_spellPrevLen = t ? (uint32_t)strlen(t) : 0;
    s_spellIdx = idx;
    if (s_spellTa == s_composeInput) updateComposeCharCount();
    spellBoxHighlight();
}

static void onSpellCellClicked(lv_event_t *e) {
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    spellReplaceWith(idx);
    spellBoxHide();
}

static void spellBoxShow(lv_obj_t *ta) {
    if (!s_rootScreen || s_spellCount < 2) return;
    const lv_font_t *font = &lv_font_montserrat_14;
    const int cellH = 28, gap = 4, pad = 4;

    s_spellBox = lv_obj_create(s_rootScreen);
    lv_obj_remove_style_all(s_spellBox);
    lv_obj_clear_flag(s_spellBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_spellBox, themedColorHex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_spellBox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_spellBox, 6, 0);
    lv_obj_set_style_border_width(s_spellBox, 1, 0);
    lv_obj_set_style_border_color(s_spellBox, lv_color_hex(0x8FB5E6), 0);
    lv_obj_set_style_pad_all(s_spellBox, pad, 0);
    lv_obj_set_style_pad_column(s_spellBox, gap, 0);
    lv_obj_set_flex_flow(s_spellBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(s_spellBox, LV_SIZE_CONTENT, cellH + pad * 2);

    for (int i = 0; i < s_spellCount; i++) {
        lv_obj_t *cell = lv_btn_create(s_spellBox);
        lv_obj_set_size(cell, LV_SIZE_CONTENT, cellH);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_pad_hor(cell, 6, 0);
        lv_obj_set_style_pad_ver(cell, 0, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(0xE8F1FF), 0);
        lv_obj_add_event_cb(cell, onSpellCellClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(cell);
        lv_obj_set_style_text_font(lbl, font, 0);
        // The typed word dimmer: it is the "no thanks", not a suggestion.
        lv_obj_set_style_text_color(lbl, lv_color_hex(i == 0 ? 0xA7C7FF : 0xE8F1FF), 0);
        lv_label_set_text(lbl, s_spellOpts[i]);
        lv_obj_center(lbl);
        s_spellCells[i] = cell;
    }

    // Long words on a narrow panel: drop suggestions from the end until the
    // row fits, keeping the typed word and at least one suggestion.
    const int32_t rw = lv_obj_get_width(s_rootScreen);
    lv_obj_update_layout(s_spellBox);
    while (s_spellCount > 2 && lv_obj_get_width(s_spellBox) > rw - 8) {
        s_spellCount--;
        lv_obj_delete(s_spellCells[s_spellCount]);
        s_spellCells[s_spellCount] = nullptr;
        lv_obj_update_layout(s_spellBox);
    }
    spellBoxHighlight();

    // Placed like the accent box: above the field, below it if there is no room.
    lv_area_t fa, ra;
    lv_obj_get_coords(ta, &fa);
    lv_obj_get_coords(s_rootScreen, &ra);
    const int32_t bw = lv_obj_get_width(s_spellBox);
    const int32_t bh = lv_obj_get_height(s_spellBox);
    int32_t y = fa.y1 - ra.y1 - bh - 4;
    if (y < 0) y = fa.y2 - ra.y1 + 4;
    lv_obj_set_pos(s_spellBox, (rw - bw) / 2, y);
    lv_obj_move_foreground(s_spellBox);
}

static bool spellIsWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'';
}

// What ends a word and so gets it checked.
static bool spellIsBoundary(char c) {
    return c == ' ' || c == ',' || c == '.' || c == '!' || c == '?' || c == ';'
           || c == ':' || c == ')' || c == '"';
}

static void onSpellFieldChanged(lv_event_t *e) {
    if (s_spellEditing) return;
    lv_obj_t *ta = lv_event_get_target_obj(e);
    const char *t = lv_textarea_get_text(ta);
    const uint32_t len = t ? (uint32_t)strlen(t) : 0;
    const uint32_t prevLen = (ta == s_spellTa) ? s_spellPrevLen : len;
    s_spellTa = ta;
    s_spellPrevLen = len;
    spellBoxHide();
    // One byte more than last time: a key typed. A paste, an emoji or a
    // delete never opens the row.
    if (len != prevLen + 1 || !s_cfg.spellCheckEnabled || i18nGetLang() != LANG_EN) return;
    const uint32_t cur = accentCursorByte(ta, t);
    if (cur < 3 || !spellIsBoundary(t[cur - 1])) return;

    const uint32_t end = cur - 1;
    uint32_t start = end;
    while (start > 0 && spellIsWordChar(t[start - 1])) start--;
    // Only a word that stands on its own: not the tail of a URL, a number, an
    // @mention or a #channel.
    if (start > 0 && t[start - 1] != ' ' && t[start - 1] != '(' && t[start - 1] != '"') return;
    const size_t n = end - start;
    if (!spell::shouldCheck(t + start, n)) return;

    const spell::Dict &d = spell::english();
    const uint32_t t0 = micros();
    if (spell::known(d, t + start, n)) return;
    char opts[spell::kMaxSuggest][spell::kMaxWord + 1];
    const int found = spell::suggest(d, t + start, n, opts, spell::kMaxSuggest);
    Serial.printf("[spell] \"%.*s\": %d suggestion(s) in %lu us\n",
                  (int)n, t + start, found, (unsigned long)(micros() - t0));
    if (found == 0) return;

    memcpy(s_spellOpts[0], t + start, n);
    s_spellOpts[0][n] = '\0';
    for (int i = 0; i < found; i++) memcpy(s_spellOpts[i + 1], opts[i], sizeof(opts[i]));
    s_spellCount = found + 1;
    s_spellIdx = 0;
    s_spellWordByte = start;
    // The word and the character after it are ASCII, so the caret sits n + 1
    // characters past the word's first letter.
    s_spellWordChar = lv_textarea_get_cursor_pos(ta) - (uint32_t)(n + 1);
    spellBoxShow(ta);
}

static void onSpellFieldFocus(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target_obj(e);
    const char *t = lv_textarea_get_text(ta);
    s_spellTa = ta;
    s_spellPrevLen = t ? (uint32_t)strlen(t) : 0;
}

static void onSpellFieldDeleted(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_spellTa) return;
    spellBoxHide();
    s_spellTa = nullptr;
    s_spellPrevLen = 0;
}

static void spellAttach(lv_obj_t *ta) {
    if (!ta) return;
    const char *t = lv_textarea_get_text(ta);
    s_spellTa = ta;
    s_spellPrevLen = t ? (uint32_t)strlen(t) : 0;
    lv_obj_add_event_cb(ta, onSpellFieldChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(ta, onSpellFieldFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(ta, onSpellFieldDeleted, LV_EVENT_DELETE, nullptr);
}

// Physical keys while the row is up. Returns true when the key was the row's.
static bool spellHandleKey(char k) {
    if (!spellBoxVisible()) return false;
    const int n = s_spellCount;
    switch (k) {
        case KEY_TAB:
        case KEY_SCROLL_DN:
        case KEY_NEXT_CHAN:
            spellReplaceWith((s_spellIdx + 1) % n);
            return true;
        case KEY_SCROLL_UP:
        case KEY_PREV_CHAN:
            spellReplaceWith((s_spellIdx + n - 1) % n);
            return true;
        case KEY_ENTER:
        case KEY_ROLLER:
            // Only when the row has been used. Untouched, Enter is still Send.
            if (s_spellIdx == 0) {
                spellBoxHide();
                return false;
            }
            spellBoxHide();
            return true;
        default:
            spellBoxHide();
            return false;
    }
}
#else
static void spellAttach(lv_obj_t *) {}
static bool spellHandleKey(char) { return false; }
#endif  // HAS_SPELLCHECK

static void closeComposePrompt() {
    // The emoji picker is a child of compose; never leave it orphaned.
    closeEmojiPicker();
    lvObjDeleteSafe(s_composeModal);
    s_composeInput = nullptr;
    s_composeKeyboard = nullptr;
    s_composeCharCount = nullptr;
#if HAS_COMPOSE_EMOJI_BTN
    s_composeEmojiBtn = nullptr;
#endif
#if defined(DEVICE_TLORA_PAGER_TFT)
    s_composeEmojiFocused = false;
#endif
    s_composeTitle = nullptr;
    s_composeTarget = COMPOSE_TARGET_CHANNEL;
    s_composeDmNodeId = 0;
    s_composeReplyPacketId = 0;
    s_composeChannelIdx = s_activeChannel;
}

// ── On-device emoji picker ────────────────────────────────────────────────────
// A grid of common emoji, opened from the compose modal, that inserts the picked
// glyph's UTF-8 into the message textarea. These keyboards have no emoji key, so
// this is the only way to compose emoji on-device; received ones already render
// via the emoji fallback font. The set is intentionally a small curated tray of
// the everyday ones, not the whole 1,489-glyph font — a full grid would be
// unusable to scroll on these panels.
// The tapback reactions live up with the Message Actions constants, which
// derive their index layout from the size of that list.

static const char *const kEmojiTray[] = {
    // Faces
    "\U0001F600", "\U0001F602", "\U0001F603", "\U0001F604", "\U0001F609",
    "\U0001F60A", "\U0001F60D", "\U0001F618", "\U0001F60E", "\U0001F914",
    "\U0001F610", "\U0001F644", "\U0001F60F", "\U0001F622", "\U0001F62D",
    "\U0001F621", "\U0001F631", "\U0001F633", "\U0001F634", "\U0001F925",
    "\U0001F92F", "\U0001F642", "\U0001F643", "\U0001F615", "\U0001F62C",
    // Hands & people
    "\U0001F44D", "\U0001F44E", "\U0001F44C", "\U0001F44B", "\U0001F44F",
    "\U0001F64F", "\U0001F4AA", "\U0001F91D", "\U0000270C", "\U0001F44A",
    "\U0001F595",
    // Symbols
    "\U00002764", "\U0001F494", "\U0001F525", "\U00002B50", "\U00002705",
    "\U0000274C", "\U00002757", "\U00002753", "\U0001F4A1", "\U0001F4AF",
    "\U0001F440", "\U0001F4CD",
    // Celebrate & objects
    "\U0001F389", "\U0001F38A", "\U0001F381", "\U0001F680",
    // Weather & nature
    "\U00002600", "\U00002601", "\U0001F327", "\U000026A1", "\U00002744",
    "\U0001F30A",
    // Food & drink
    "\U0001F355", "\U00002615", "\U0001F37A", "\U0001F36A", "\U0001F34E",
};
constexpr int kEmojiTrayTotal = (int)(sizeof(kEmojiTray) / sizeof(kEmojiTray[0]));

// How many of the tray actually get built. Every cell is two live LVGL objects
// (cell + label) held for as long as the picker is open, on top of a DM view
// that can already be 60 message rows — and the picker's glyphs then rasterize
// out of whatever pool is left.
//
// The PSRAM boards run a 384 KB pool (see lv_conf.h) and can afford the whole
// tray. The Cardputer cannot: no PSRAM means its pool is a 96 KB internal-DRAM
// array, so it takes a shorter tray, the same way it already takes MAX_DM_LINES
// 16 against everyone else's 60.
//
// Truncation is from the end, and kEmojiTray is ordered by category, so the cut
// keeps every face and hand plus the common reaction symbols and drops the
// tail: celebrate, weather, food.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr int kEmojiTrayCount = 40;
#else
constexpr int kEmojiTrayCount = kEmojiTrayTotal;
#endif
static_assert(kEmojiTrayCount <= kEmojiTrayTotal,
              "emoji tray cap must not exceed the number of emoji defined");

// Symbols the Mesh Deck's physical keyboard cannot reach. Its 48 keys cover
// letters, digits, comma and period and nothing else, so without these the only
// punctuation available on that board is "," and ".". Ordered roughly by how
// often a message needs them.
//
// '#' is included even though it has its own key: that key is the symbol
// button, so it opens this tray rather than typing the character.
static const char *const kSymbolTray[] = {
    "!", "?", ".", ",", ":", ";", "'", "\"",
    "-", "_", "/", "@", "#", "&", "+", "=",
    "(", ")", "[", "]", "{", "}", "<", ">",
    "*", "%", "$", "|", "\\", "~", "^", "`",
};
constexpr int kSymbolTrayCount = (int)(sizeof(kSymbolTray) / sizeof(kSymbolTray[0]));

// The picker renders whichever tray is active. Everything below — grid build,
// selection, scrolling, insert — is shared; only the source array differs.
static const char *const *s_pickerTray  = kEmojiTray;
static int                s_pickerCount = kEmojiTrayCount;

static void refreshEmojiPickerSelection() {
    if (!s_emojiPickerModal) return;
    lv_obj_t *grid = lv_obj_get_child(s_emojiPickerModal, 1);   // [0]=hint, [1]=grid
    if (!grid) return;
    const bool light = (s_cfg.uiMode == UI_MODE_LIGHT);
    for (int i = 0; i < s_pickerCount; i++) {
        lv_obj_t *cell = lv_obj_get_child(grid, i);
        if (!cell) continue;
        const bool sel = (i == s_emojiPickerSelection);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(cell, lv_color_make(0, 0, 0), 0);
    #else
        lv_obj_set_style_bg_opa(cell, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cell, light ? lv_color_hex(0xC7D8F5)
                                              : lv_color_hex(0x2A4E8F), 0);
        lv_obj_set_style_border_width(cell, sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(cell, light ? lv_color_hex(0x3A5F9E)
                                                  : lv_color_hex(0x9BC0FF), 0);
    #endif
        if (sel) lv_obj_scroll_to_view(cell, LV_ANIM_OFF);
    }
}

// Fire the picked glyph as a standalone one-emoji message to whatever screen the
// picker was opened over: the selected DM conversation when the DM screen is up,
// otherwise the active channel. There is no compose step — the picker is a
// quick-reaction affordance opened with 'e' from the chat/DM browse screen.
// tapbackId, when non-zero, turns the channel send into a reaction on that
// message (Data.reply_id + Data.emoji) — same wire shape as the web UI's tapback
// buttons — so other clients render it on the bubble instead of as a new line.
static void sendQuickEmoji(const char *emoji, uint32_t tapbackId = 0) {
    if (!emoji || !emoji[0]) return;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) return;   // no identity yet; nothing to send from

    if (s_dmModal) {
        // On the DM screen, only ever send to the selected conversation — never
        // fall back to the channel. 'e' is gated on a live selection, so this is
        // just belt-and-suspenders against the conversation being deselected.
        DmConv *dm = selectedDmConversation();
        if (!dm || dm->nodeId == 0) return;
        if (!DMs.sendDm(s_myNodeId, dm->nodeId, emoji)) {
            DMs.addMessage(dm->nodeId, nullptr, "", TR("! TX failed"), TFT_RED, false, -1, 0);
        }
    } else {
        int txChan = (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS)
                   ? s_activeChannel : 0;
        if (!Channels.sendText(s_myNodeId, emoji, s_cfg.okToMqtt, txChan,
                               tapbackId, tapbackId ? 1 : 0)) {
            Channels.addMessage(txChan, "", TR("! TX failed"), TFT_RED, 0);
        }
    }
    refreshChatView(true);
    refreshDmModal(true);
}

// Moves the tray cursor by delta, clamped. Shared by key handling and the
// hold-to-repeat service.
static void emojiPickerStep(int delta) {
    if (delta == 0 || s_pickerCount <= 0) return;
    int nxt = s_emojiPickerSelection + delta;
    if (s_emojiPickerWrap) {
        // Modulo rather than a clamp, and written to survive a negative: C's %
        // keeps the sign of the dividend, so -1 % 32 is -1 rather than 31.
        nxt = ((nxt % s_pickerCount) + s_pickerCount) % s_pickerCount;
    } else {
        if (nxt < 0) nxt = 0;
        if (nxt >= s_pickerCount) nxt = s_pickerCount - 1;
    }
    if (nxt == s_emojiPickerSelection) return;
    s_emojiPickerSelection = nxt;
    refreshEmojiPickerSelection();
}

// Hold-to-repeat for the tray cursor: after the key has been down for
// kEmojiRepeatDelayMs it steps every kEmojiRepeatIntervalMs, so crossing the
// tray is one long press instead of sixty taps. Depends on keyboardHeldKey(),
// which the Pager answers from real press/release events and other boards
// approximate from repeat traffic — where that approximation cannot see a held
// key, this simply never engages.
static constexpr uint32_t kEmojiRepeatDelayMs    = 1000;
static constexpr uint32_t kEmojiRepeatIntervalMs = 120;

static void serviceEmojiPickerRepeat(uint32_t nowMs) {
    if (!s_emojiPickerModal || s_emojiPickerRepeatDelta == 0) return;
    if (keyboardHeldKey() == KEY_NONE) {   // released, or not detectable here
        s_emojiPickerRepeatDelta = 0;
        return;
    }
    if (keyboardHeldMs() < kEmojiRepeatDelayMs) return;
    if ((uint32_t)(nowMs - s_emojiPickerRepeatLastMs) < kEmojiRepeatIntervalMs) return;
    s_emojiPickerRepeatLastMs = nowMs;
    emojiPickerStep(s_emojiPickerRepeatDelta);
}

// A second's worth of "that went in", for the one case where the thing that
// changed is hidden behind the thing that changed it: the emoji tray stays open
// after a pick, so the glyph lands in a message box nobody can see. Without this
// a successful insert and a dead key look identical.
//
// Parented to the tray rather than the screen, so it cannot outlive what it is
// reporting on -- closing the tray takes the toast with it, and LVGL cancels the
// pending delete along with the object. Non-clickable, so it never eats a tap
// meant for the cell underneath it.
static void showEmojiInsertToast(const char *glyph) {
    if (!lvObjValid(s_emojiPickerModal) || !glyph || !glyph[0]) return;

    lv_obj_t *toast = lv_obj_create(s_emojiPickerModal);
    if (!toast) return;   // out of pool: the insert still happened, say nothing
    lv_obj_remove_style_all(toast);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_radius(toast, 6, 0);
    lv_obj_set_style_pad_hor(toast, 8, 0);
    lv_obj_set_style_pad_ver(toast, 3, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast, 1, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(0x8FB5E6), 0);

    lv_obj_t *lbl = lv_label_create(toast);
    if (lbl) {
        lv_obj_set_style_text_font(lbl, emojiFont(&lv_font_montserrat_12), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
        char buf[32];
        snprintf(buf, sizeof(buf), "%s added", glyph);
        setLabelTextEmojiSafe(lbl, buf);
        lv_obj_center(lbl);
    }
    lv_obj_move_foreground(toast);
    lv_obj_delete_delayed(toast, 900);
}

static void emojiPickerActivate(int idx) {
    if (idx < 0 || idx >= s_pickerCount) return;
    if (s_emojiPickerSendMode) {
        const uint32_t tapbackId = s_emojiPickerTapbackId;   // survives the teardown
        closeEmojiPicker();   // one-shot: tear the tray down before the send refresh
        sendQuickEmoji(s_pickerTray[idx], tapbackId);
        return;
    }
    // Insert mode: append to the open compose box.
    if (s_composeInput) {
        lv_textarea_add_text(s_composeInput, s_pickerTray[idx]);
        updateComposeCharCount();
        // Only where the tray stays up. The symbol tray closes on the way out
        // below, which puts the message back on screen with the character in it
        // -- that is its own confirmation, and a toast over a closing panel
        // would be telling you something you can already see.
        if (s_pickerTray != kSymbolTray) showEmojiInsertToast(s_pickerTray[idx]);
    }

    // The symbol tray is one-shot: pick a symbol and you are back in the message
    // you were typing. Emoji keep the tray up, because picking several in a row
    // is the common case there — a symbol is usually a single character in the
    // middle of a sentence, so staying open just means an extra keypress to
    // dismiss before typing can continue.
    //
    // Compared by tray pointer rather than a separate flag so this cannot fall
    // out of step with whatever openEmojiPicker() selected.
    if (s_pickerTray == kSymbolTray) {
        closeEmojiPicker();
    }
}

static void onEmojiCellPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_emojiPickerSendMode) {
        s_emojiPickerSelection = idx;
        refreshEmojiPickerSelection();
    }
    emojiPickerActivate(idx);   // sends, or inserts; closes except for the emoji tray
}

static void onEmojiBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target_obj(e) != s_emojiPickerBackdrop) return;
    closeEmojiPicker();
}

static void closeEmojiPicker() {
    if (lvObjValid(s_emojiPickerBackdrop)) {
        lv_obj_del(s_emojiPickerBackdrop);
    } else if (lvObjValid(s_emojiPickerModal)) {
        lv_obj_del(s_emojiPickerModal);
    }
    s_emojiPickerBackdrop = nullptr;
    s_emojiPickerModal = nullptr;
    s_emojiPickerTapbackId = 0;
    s_emojiPickerRepeatDelta = 0;
    s_emojiPickerWrap = false;
}

// DIAGNOSTIC (emoji-picker OOM): scrolling the tray aborts inside
// stbtt__new_active, i.e. lv_malloc returned NULL mid-rasterize. Sampling the
// pool as the tray scrolls separates the three candidate causes, which need
// different fixes:
//   - free stays low and flat  -> the working set genuinely does not fit
//   - free walks downward      -> something leaks per refresh
//   - free is fine but biggest is tiny -> fragmentation, not exhaustion
// Throttled so the logging itself does not perturb the scroll it measures.
static void onEmojiGridScrolled(lv_event_t *e) {
    LV_UNUSED(e);
    static uint32_t lastLogMs = 0;
    const uint32_t now = millis();
    if (now - lastLogMs < 400) return;
    lastLogMs = now;
    logLvglMemDiag("emoji scroll");
}

#if defined(DEVICE_TDECK_PRO)
// Page buttons under the tray (T-Deck Pro). Dragging a list on the e-paper
// panel is miserable -- every intermediate position is a partial refresh -- so
// a tap moves exactly one screenful of whole rows, unanimated, which is one
// redraw. The highlight moves to the first cell of the page landed on, so
// Enter and the keys carry on from what is on screen instead of jumping back.
static void emojiPickerPage(int dir) {
    if (!s_emojiPickerModal) return;
    lv_obj_t *grid = lv_obj_get_child(s_emojiPickerModal, 1);   // [0]=hint, [1]=grid
    if (!grid || s_emojiPickerCols < 1) return;
    const int rowH = s_emojiPickerCell + 2;                       // cell + pad_row
    int rows = (int)lv_obj_get_content_height(grid) / rowH;
    if (rows < 1) rows = 1;
    lv_obj_update_layout(grid);
    const int32_t y = lv_obj_get_scroll_y(grid);
    const int32_t maxY = y + lv_obj_get_scroll_bottom(grid);
    int32_t target = y + dir * rows * rowH;
    target = (target / rowH) * rowH;                              // whole rows
    if (target < 0) target = 0;
    if (target > maxY) target = maxY;
    if (target != y) lv_obj_scroll_to_y(grid, target, LV_ANIM_OFF);
    int first = (int)((target + rowH - 1) / rowH) * s_emojiPickerCols;
    if (first >= s_pickerCount) first = s_pickerCount - 1;
    if (first < 0) first = 0;
    s_emojiPickerSelection = first;
    refreshEmojiPickerSelection();
}

static lv_obj_t *emojiPickerPageBtn(lv_obj_t *row, const char *text, int dir) {
    lv_obj_t *btn = lv_btn_create(row);
    if (!btn) return nullptr;
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, lv_pct(100));
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
    // Pressed: inverted, so the tap registers on a panel that cannot animate.
    lv_obj_set_style_bg_color(btn, lv_color_make(0, 0, 0), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_make(0xFF, 0xFF, 0xFF), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_make(0, 0, 0), 0);
    lv_obj_add_event_cb(btn,
                        [](lv_event_t *e) {
                            emojiPickerPage((int)(intptr_t)lv_event_get_user_data(e));
                        },
                        LV_EVENT_CLICKED, (void *)(intptr_t)dir);
    lv_obj_t *lbl = lv_label_create(btn);
    if (!lbl) return nullptr;
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}
#endif

static void openEmojiPicker(bool sendMode, bool symbolTray) {
    if (!s_rootScreen || s_emojiPickerModal) return;
    // Pick the tray before anything reads it: the grid, the selection clamp and
    // the insert path all go through s_pickerTray.
    s_pickerTray  = symbolTray ? kSymbolTray : kEmojiTray;
    s_pickerCount = symbolTray ? kSymbolTrayCount : kEmojiTrayCount;
    // Symbols are text, never a reaction, so they only ever insert.
    if (symbolTray) sendMode = false;
    s_emojiPickerSendMode = sendMode;
    s_emojiPickerSelection = 0;
#if defined(DEVICE_TLORA_PAGER_TFT)
    // Every control on this board is one-dimensional -- the wheel, and the two
    // keys that alias it -- so the grid is one long line to all of them and
    // every one of them wraps. See s_emojiPickerWrap.
    s_emojiPickerWrap = true;
#else
    s_emojiPickerWrap = false;
#endif
    // Cursor parked on a chat message → this pick reacts to it. The DM screen has
    // no per-message cursor, so a stale chat selection must never target there.
    s_emojiPickerTapbackId = (sendMode && !s_dmModal) ? s_selectedMsgReplyPacketId : 0;
    const bool tapback = (s_emojiPickerTapbackId != 0);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 12;
    if (modalW > 340) modalW = 340;

    // Emoji glyphs come from the fallback face; a blank base label just carries
    // the fallback, so any Montserrat size with an emoji slot works (see
    // s_slots in emoji_font.cpp).
    //
    // Cell size follows the panel: big enough to tap on touch builds, small
    // enough that a couple of rows fit the Cardputer's 135px-tall screen.
#if EMOJI_PICKER_LARGE
    // 26 px glyphs: at 20 the faces were hard to tell apart.
    const lv_font_t *cellFont = symbolTray ? &lv_font_montserrat_24
                                          : emojiFont(&lv_font_montserrat_24);
    const int cell = (w <= 160) ? 24 : 38;
#else
    const lv_font_t *cellFont = symbolTray ? &lv_font_montserrat_18
                                          : emojiFont(&lv_font_montserrat_18);
    const int cell = (w <= 160) ? 24 : 30;
#endif

    s_emojiPickerBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_emojiPickerBackdrop, w, h);
    lv_obj_align(s_emojiPickerBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_emojiPickerBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_emojiPickerBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_emojiPickerBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_emojiPickerBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_emojiPickerBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_emojiPickerBackdrop, 0, 0);
    lv_obj_add_event_cb(s_emojiPickerBackdrop, onEmojiBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_emojiPickerModal = lv_obj_create(s_emojiPickerBackdrop);
    lv_obj_set_width(s_emojiPickerModal, modalW);
    lv_obj_set_height(s_emojiPickerModal, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_emojiPickerModal, (h > 40) ? (h - 14) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_emojiPickerModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_emojiPickerModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_emojiPickerModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_emojiPickerModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_emojiPickerModal, 1, 0);
    lv_obj_set_style_border_color(s_emojiPickerModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_emojiPickerModal, 6, 0);
    lv_obj_set_style_pad_row(s_emojiPickerModal, 4, 0);
    lv_obj_set_flex_flow(s_emojiPickerModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_emojiPickerModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_emojiPickerBackdrop);

    lv_obj_t *hint = lv_label_create(s_emojiPickerModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if UI_TOUCH_ONLY_PROFILE
    // No "tap outside to close" any more: the corner X says it, and it says it
    // where the finger can reach. The tray covers all but a 6px margin of the
    // screen, so the backdrop it was pointing at was barely a target at all.
    lv_label_set_text(hint, symbolTray ? "Tap a symbol"
                            : tapback  ? "Tap to react"
                            : sendMode ? "Tap to send"
                                       : "Tap to add");
    // This modal has no title: the hint is its top row, so it is the one that
    // has to make room for the X above the tray.
    reserveHeltecCloseXRow(hint);
#else
    lv_label_set_text_fmt(hint, symbolTray ? TR("Move • Enter=Insert • %s=Close")
                                : tapback  ? TR("Move • Enter=React • %s=Close")
                                : sendMode ? TR("Move • Enter=Send • %s=Close")
                                           : TR("Move • Enter=Add • %s=Close"),
                          modalCloseKeyLabel());
#endif

    lv_obj_t *grid = lv_obj_create(s_emojiPickerModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
#if UI_TOUCH_ONLY_PROFILE
    // 12px lower than the keyboard builds: the hint row is grown to the height
    // of the corner X, and the grid is what gives up the space. Hint + gap +
    // grid + padding then comes to the modal's own (h - 14) cap.
    lv_obj_set_style_max_height(grid, (h > 72) ? (h - 56) : LV_SIZE_CONTENT, 0);
#elif defined(DEVICE_TDECK_PRO)
    // 30 px less again for the page buttons under the grid (26 + the 4 px gap).
    lv_obj_set_style_max_height(grid, (h > 90) ? (h - 74) : LV_SIZE_CONTENT, 0);
#else
    lv_obj_set_style_max_height(grid, (h > 60) ? (h - 44) : LV_SIZE_CONTENT, 0);
#endif
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    // AUTO: the bar shows only while the tray is taller than its box, i.e.
    // when there is more to scroll to.
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, 2, 0);
    // The scrollbar has to be styled here: lv_obj_remove_style_all() above
    // took the theme's away with everything else, so the tray scrolled with no
    // sign that it could. The thin bar the other lists use, down the right
    // edge, with the cells kept clear of it.
    static constexpr int kEmojiScrollbarGutter = 4;
    lv_obj_set_style_pad_right(grid, kEmojiScrollbarGutter, 0);
    lv_obj_set_style_width(grid, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(grid, 2, LV_PART_SCROLLBAR);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(grid, lv_color_make(0, 0, 0), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, LV_PART_SCROLLBAR);
#else
    lv_obj_set_style_bg_color(grid, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(grid, LV_OPA_70, LV_PART_SCROLLBAR);
#endif

    // Row width, derived from the same numbers the grid is built with (modal
    // width less its 6px padding either side and the scrollbar gutter, cells
    // plus a 2px column gap). Used by up/down navigation so those keys move a
    // row rather than one cell.
    s_emojiPickerCols = (modalW - 12 - kEmojiScrollbarGutter + 2) / (cell + 2);
    if (s_emojiPickerCols < 1) s_emojiPickerCols = 1;
    s_emojiPickerCell = cell;

    for (int i = 0; i < s_pickerCount; i++) {
        lv_obj_t *c = lv_obj_create(grid);
        // The whole tray is built up front, so this is the largest single burst
        // of allocations in the UI — and it lands on top of the chat/DM screen
        // that is still live underneath. Bail out cleanly instead of leaving a
        // half-built grid behind when the pool cannot take all of it.
        if (!c) {
            logLvglMemDiag("emoji picker aborted (low LVGL mem)");
            closeEmojiPicker();
            return;
        }
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, cell, cell);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(c, 4, 0);
        lv_obj_add_event_cb(c, onEmojiCellPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *g = lv_label_create(c);
        if (!g) {
            logLvglMemDiag("emoji picker aborted (low LVGL mem)");
            closeEmojiPicker();
            return;
        }
        lv_obj_set_style_text_font(g, cellFont, 0);
        // The emoji font is monochrome — glyphs take the label's text color —
        // so without setting it they inherit whatever the default is and read as
        // dark shapes on the dark tray background.
        lv_obj_set_style_text_color(g,
                                    (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x16233A)
                                        : lv_color_hex(0xFFFFFF),
                                    0);
        setLabelTextEmojiSafe(g, s_pickerTray[i]);
        lv_obj_center(g);
    }

#if defined(DEVICE_TDECK_PRO)
    // After the grid for the same reason as the X below: the grid stays child
    // [1] of the modal. Allocated like the cells, so a failure closes the tray.
    {
        lv_obj_t *pager = lv_obj_create(s_emojiPickerModal);
        if (!pager) {
            logLvglMemDiag("emoji picker aborted (low LVGL mem)");
            closeEmojiPicker();
            return;
        }
        lv_obj_remove_style_all(pager);
        lv_obj_set_size(pager, lv_pct(100), 26);
        lv_obj_clear_flag(pager, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_column(pager, 6, 0);
        lv_obj_set_flex_flow(pager, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pager, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        if (!emojiPickerPageBtn(pager, TR("Page Up"), -1)
            || !emojiPickerPageBtn(pager, TR("Page Down"), 1)) {
            logLvglMemDiag("emoji picker aborted (low LVGL mem)");
            closeEmojiPicker();
            return;
        }
    }
#endif

#if UI_TOUCH_ONLY_PROFILE
    // Built after the grid, not before it: refreshEmojiPickerSelection() reaches
    // the grid as child [1] of the modal, and the X has to land after it.
    //
    // Allocated like the cells above — this is the largest burst in the UI and
    // the tray is what usually exhausts the pool, so a failure here closes the
    // picker rather than leaving one with no way out.
    if (!appendHeltecCloseX(s_emojiPickerModal,
                            [](lv_event_t *ev) { LV_UNUSED(ev); closeEmojiPicker(); })) {
        logLvglMemDiag("emoji picker aborted (low LVGL mem)");
        closeEmojiPicker();
        return;
    }
#endif

    // Baseline right after the tray is built but before a single glyph has been
    // rasterized: whatever the scroll samples show, they are relative to this.
    logLvglMemDiag("emoji picker built");
    lv_obj_add_event_cb(grid, onEmojiGridScrolled, LV_EVENT_SCROLL, nullptr);

    refreshEmojiPickerSelection();
}

// Draws one caret that looks the same in every state.
//
// Keys reach these boxes by hand from the board key handler, not through an LVGL
// input group, so typing alone never puts the textarea in LV_STATE_FOCUSED — and
// the default theme attaches its cursor style to LV_PART_CURSOR|LV_STATE_FOCUSED
// only. The part therefore stays at zero border width and the caret draws as an
// empty rect: nothing on screen tells you where the next character lands.
// Styling the part in the default state instead makes it unconditional.
//
// anim_duration is what makes the caret blink; the textarea samples it in
// start_cursor_blink(), which the cursor-pos call below runs, so set the styles
// first and blink from the moment the box opens rather than the first keystroke.
//
// The theme's style has to come off the part first, or it wins back the caret
// the moment the box is tapped. A tap on a CLICK_FOCUSABLE object outside any
// group makes LVGL add LV_STATE_FOCUSED, and get_prop_core() resolves a property
// to the candidate with the highest state weight — the theme's FOCUSED entry (2)
// outranks anything written here for the default state (0). The caret would flip
// from this white to the theme's own text colour, which on the light-mode default
// theme this build never re-inits is dark grey on a dark blue box, and change
// blink rate and width with it. Removing that entry leaves one definition of the
// part, so the caret looks the same tapped or not, in either UI mode.
//
// On touch boards the same caret is what makes tap-to-edit legible. LVGL already
// walks a press back to a character index and moves the cursor there, so long as
// the widget stays clickable and click positioning is on — both are class
// defaults, but they are the whole feature here, so assert them rather than
// inherit them. Typing then lands at the tapped point on its own: every board
// key handler edits through lv_textarea_add_text()/lv_textarea_delete_char(),
// which are cursor-relative, not append-only.
static void showTextareaCursor(lv_obj_t *ta) {
    if (!ta) return;
    lv_obj_remove_style(ta, NULL, LV_PART_CURSOR | LV_STATE_ANY);
    lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xE8F1FF), LV_PART_CURSOR);
    lv_obj_set_style_border_opa(ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta, 500, LV_PART_CURSOR);
#if HAS_TOUCH
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    lv_textarea_set_cursor_click_pos(ta, true);
#endif
    lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
}

#if UI_TOUCH_ONLY_PROFILE
#if defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_CROWPANEL)
// For a keyboard given a fixed height: what it gave up has to go somewhere.
// Where a sibling already grows (the admin transcript) it takes it and the
// keyboard, created last, is already at the bottom. Where nothing does -- the
// dialogs, whose rows are all fixed -- an invisible spacer above the keyboard
// takes it, rather than the keyboard floating mid-screen over blank space. A
// parent sized to its content (the composer) has no slack to take, so it gets
// none.
static void keyboardYieldSlack(lv_obj_t *keyboard) {
    lv_obj_t *parent = lv_obj_get_parent(keyboard);
    if (parent && lv_obj_get_style_layout(parent, LV_PART_MAIN) == LV_LAYOUT_FLEX
        && lv_obj_get_style_height(parent, LV_PART_MAIN) != LV_SIZE_CONTENT) {
        bool siblingGrows = false;
        const uint32_t n = lv_obj_get_child_count(parent);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
            if (child != keyboard
                && lv_obj_get_style_flex_grow(child, LV_PART_MAIN) > 0) {
                siblingGrows = true;
                break;
            }
        }
        if (!siblingGrows) {
            lv_obj_t *spacer = lv_obj_create(parent);
            lv_obj_remove_style_all(spacer);
            lv_obj_clear_flag(spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE
                                                      | LV_OBJ_FLAG_SCROLLABLE));
            lv_obj_set_size(spacer, 1, 0);
            lv_obj_set_flex_grow(spacer, 1);
            lv_obj_move_to_index(spacer, lv_obj_get_index(keyboard));
        }
    }
}
#endif

// The composer docks on a fixed-height keyboard -- only as tall as what it
// holds, on the bottom edge, the conversation readable above it -- wherever
// the keyboard is fixed rather than grown (configureOnScreenKeyboard): the P4
// always, the CrowPanel 3.5 upright.
static inline bool composeDockedOnKeyboard() {
#if defined(DEVICE_TDISPLAY_P4)
    return true;
#elif defined(DEVICE_CROWPANEL_35)
    return uiPortrait();
#else
    return false;
#endif
}

static void configureOnScreenKeyboard(lv_obj_t *keyboard) {
    if (!keyboard) return;
    lv_obj_set_width(keyboard, lv_pct(100));
#if UI_LARGE_PANEL_PROFILE
    const int screenH = (int)lv_disp_get_ver_res(NULL);
    const int keyboardH = min(360, max(240, screenH * 2 / 5));
    lv_obj_set_flex_grow(keyboard, 0);
    lv_obj_set_height(keyboard, keyboardH);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, 4, LV_PART_ITEMS);
#elif defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_CROWPANEL_50)
    // A fixed height pinned to the bottom, the way a phone keyboard sits and
    // the way wadamesh's does -- not flex_grow. Growing into the rest of the
    // modal is what the 240x320 boards want, where the rest is about half the
    // screen; on this 596-tall portrait panel it was ~90% of it.
    //
    // 150 UI px is ~23 mm here (UI_PIXEL_SCALE, board.h): the physical size of
    // wadamesh's 130 px keyboard on a T-Deck. Landscape is a 320x240-class
    // shape, so it takes half the height, as wadamesh does there.
    const int screenH = (int)lv_disp_get_ver_res(NULL);
    lv_obj_set_flex_grow(keyboard, 0);
#if defined(DEVICE_CROWPANEL_50)
    // The same problem on the 5" upright (480x800), where growing took ~95% of
    // the screen: 40% of the height there, half of it in landscape -- each
    // about the keyboard on a phone held that way. Key labels at 20 px (named
    // 18: hal/crowpanel_50_fonts.h) for keys this size.
    lv_obj_set_height(keyboard, uiPortrait() ? (screenH * 2) / 5 : screenH / 2);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_18, LV_PART_ITEMS);
#else
    lv_obj_set_height(keyboard, uiPortrait() ? 150 : screenH / 2);
#endif
    keyboardYieldSlack(keyboard);
#elif UI_EDGE_TO_EDGE_TEXT_ENTRY
#if defined(DEVICE_CROWPANEL_35)
    // Upright (320x480) this is the P4's portrait keyboard: a fixed height on
    // the bottom edge instead of growing into the modal, which took nearly the
    // whole screen. 150 px is ~24 mm on this panel, the same physical height
    // as the P4's 150 UI px. Landscape is a 320-tall panel, where growing into
    // what is left is still the right amount.
    if (uiPortrait()) {
        lv_obj_set_flex_grow(keyboard, 0);
        lv_obj_set_height(keyboard, 150);
        keyboardYieldSlack(keyboard);
    } else {
        lv_obj_set_flex_grow(keyboard, 1);
    }
#else
    lv_obj_set_flex_grow(keyboard, 1);
#endif
    // Text entry owns the screen (Wio Tracker L2, CrowPanel). Remove keyboard chrome
    // and keep only a one-pixel key gap so the narrow QWERTY rows spend their
    // width on tap targets rather than nested modal/theme padding.
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, 1, LV_PART_ITEMS);
#else
    lv_obj_set_flex_grow(keyboard, 1);
#endif
}
#endif

// Names the destination in the composer's title row.
//
// The channel used to be implicit: you reached this from the chat screen, where
// the selector above it already said which channel you were reading. The home
// dashboard broke that — Space opens the composer from there too, and nothing
// else on that screen names a channel — so the title carries it now. It is the
// same answer on both screens rather than a dashboard special case, because a
// composer that labels its target only sometimes is worse than one that always
// does.
//
// DMs are left alone. openComposePromptForDm() flips the target after this has
// already run once, and calls it again; there the peer is named by the screen
// the composer opened over, and a channel name would be an outright lie.
//
// The icon in front says the same thing at a glance: a pencil for a new
// channel message, the return arrow for a reply, an envelope for a DM. It sits
// outside TR() so translations keep keying on the bare words.
static void composeSetTitle(bool isReply) {
    if (!lvObjValid(s_composeTitle)) return;
    const char *kind = isReply ? TR("Reply") : TR("New Message");
    const char *icon = isReply ? LV_SYMBOL_NEW_LINE : LV_SYMBOL_EDIT;
    char text[80];
    if (s_composeTarget == COMPOSE_TARGET_DM) {
        snprintf(text, sizeof(text), "%s  %s", isReply ? icon : LV_SYMBOL_ENVELOPE, kind);
        lv_label_set_text(s_composeTitle, text);
        return;
    }
    const char *chan = channelName(s_composeChannelIdx);
    if (!chan || !chan[0]) chan = TR("Channel");
    snprintf(text, sizeof(text), "%s  %s: %s", icon, kind, chan);
    lv_label_set_text(s_composeTitle, text);
}

// ── Composer look ────────────────────────────────────────────────────────────
// Every colour below is read from s_ui, the palette the active theme derives,
// rather than from the dark-navy hex values the hex remap table translates. So
// the composer is drawn in the theme's own roles -- panel, raised surface,
// accent, text -- and a custom theme gets the same treatment as a preset.
//
// None of this moves anything: radii, colours and a glow only. The sizes above
// and below are measured to the pixel on several panels, and a restyle that
// changed a single row height would undo that work.
//
// The T-Deck Pro has no themes; its s_ui is the paper palette, black on white,
// so the same calls come out as a clean black-and-white card. The glow, which
// is a translucent shadow, has nothing to draw on 1 bpp and is left off there.

// The card itself. A rounded panel with an accent-tinted edge and, where the
// card does not fill the panel, a soft accent glow around it -- the only place
// the composer reaches outside its own box, and what lifts it off the screen
// it opened over.
static void composeStyleModal(lv_obj_t *modal, int modalW, int modalH) {
    if (!modal) return;
    const bool fullW = modalW >= (int)lv_disp_get_hor_res(NULL);
    const bool fullH = modalH >= (int)lv_disp_get_ver_res(NULL);
    lv_obj_set_style_bg_color(modal, lvColorFrom565(s_ui.panelBg), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(modal, 1, 0);
    lv_obj_set_style_border_color(modal, lvColorFrom565(s_ui.dividerHi), 0);
    // Square when it runs edge to edge: rounding a corner against the panel's
    // own edge just shows the screen underneath in four slivers.
    lv_obj_set_style_radius(modal, fullW ? 0 : 6, 0);
#if HAS_UI_THEMES
    if (!fullW && !fullH) {
        lv_obj_set_style_shadow_color(modal, lvColorFrom565(s_ui.accent), 0);
        lv_obj_set_style_shadow_width(modal, 16, 0);
        lv_obj_set_style_shadow_spread(modal, 0, 0);
        lv_obj_set_style_shadow_opa(modal, LV_OPA_40, 0);
    } else {
        lv_obj_set_style_shadow_width(modal, 0, 0);
    }
#else
    lv_obj_set_style_shadow_width(modal, 0, 0);
#endif
}

static void composeStyleTitle(lv_obj_t *title) {
    if (!title) return;
    lv_obj_set_style_text_color(title, lvColorFrom565(s_ui.accent), 0);
}

// The message box: the theme's input surface, rounded to match the card, an
// edge that turns accent while it has focus, an accent caret, and a placeholder
// in the dim text colour rather than LVGL's default grey.
static void composeStyleInput(lv_obj_t *ta) {
    if (!ta) return;
    lv_obj_set_style_text_color(ta, lvColorFrom565(s_ui.textMain), 0);
    lv_obj_set_style_bg_color(ta, lvColorFrom565(s_ui.inputBg), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ta, 5, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lvColorFrom565(s_ui.dividerHi), 0);
    lv_obj_set_style_border_color(ta, lvColorFrom565(s_ui.accent), LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(ta, lvColorFrom565(s_ui.accent), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(ta, lvColorFrom565(s_ui.textDim), LV_PART_TEXTAREA_PLACEHOLDER);
    // showTextareaCursor() has already set the caret up; only its colour moves.
    lv_obj_set_style_border_color(ta, lvColorFrom565(s_ui.cursor), LV_PART_CURSOR);
}

// The quoted message a reply answers, drawn as a quote: a raised strip with an
// accent bar down its left edge, the way the chat draws a reply. Border and
// padding are horizontal only, so the strip keeps the height it was given.
static void composeStyleQuote(lv_obj_t *obj) {
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lvColorFrom565(s_ui.panelAlt), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 3, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(obj, 3, 0);
    lv_obj_set_style_border_color(obj, lvColorFrom565(s_ui.accent), 0);
    lv_obj_set_style_pad_left(obj, 6, 0);
    lv_obj_set_style_text_color(obj, lvColorFrom565(s_ui.textDim), 0);
}

// Footer text: the key legend and the character count.
static void composeStyleMeta(lv_obj_t *lbl) {
    if (!lbl) return;
    lv_obj_set_style_text_color(lbl, lvColorFrom565(s_ui.textDim), 0);
}

#if UI_TOUCH_ONLY_PROFILE
// Touch buttons. Send is the one filled in the accent; Cancel and the emoji
// opener are quiet raised surfaces, so the eye lands on the thing you came to
// press. Pressed darkens toward the card rather than LVGL's stock recolour.
static void composeStyleButton(lv_obj_t *btn, bool primary) {
    if (!btn) return;
    const uint16_t face = primary ? s_ui.accent : s_ui.panelStrong;
    lv_obj_set_style_bg_color(btn, lvColorFrom565(face), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lvColorFrom565(blend565(face, s_ui.panelBg, 90)),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, primary ? 0 : 1, 0);
    lv_obj_set_style_border_color(btn, lvColorFrom565(s_ui.dividerHi), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lvColorFrom565(primary ? s_ui.textOnAccent : s_ui.textMain), 0);
}
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
// Paints the emoji button for the current focus, and puts the caret back where
// it belongs. The textarea keeps its own cursor visible only while it is the
// thing being driven -- two lit carets on one row reads as two selections.
static void refreshComposeEmojiFocus() {
    if (!lvObjValid(s_composeEmojiBtn)) return;
    const bool on = s_composeEmojiFocused;
    lv_obj_set_style_bg_color(s_composeEmojiBtn,
                              on ? lvColorFrom565(s_ui.selectAccent)
                                 : lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(s_composeEmojiBtn, on ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_composeEmojiBtn, on ? 2 : 1, 0);
    lv_obj_set_style_border_color(s_composeEmojiBtn,
                                  on ? lv_color_hex(0xE8F1FF) : lv_color_hex(0x4C76BA), 0);
    if (lvObjValid(s_composeInput)) {
        // showTextareaCursor() styles the caret on LV_PART_CURSOR with no state
        // dependency, so a focus state would not touch it -- its opacity is the
        // thing to move. Two lit carets on one row read as two selections.
        lv_obj_set_style_border_opa(s_composeInput,
                                    on ? LV_OPA_TRANSP : LV_OPA_COVER,
                                    LV_PART_CURSOR);
    }
}
#endif

static void openComposePrompt(uint32_t replyPacketId,
                              const char *replyText,
                              bool allowSelectedReplyFallback) {
    if (!s_rootScreen) return;
    if (s_activeChannel < 0 || s_activeChannel >= MESH_CHANNELS) return;

    // Pager can open compose via paths that don't pass reply args; recover from current selection.
    if (allowSelectedReplyFallback
        && replyPacketId == 0 && (!replyText || !replyText[0])
        && s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
        replyPacketId = s_selectedMsgReplyPacketId;
        replyText = s_selectedMsgText;
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_14);
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 3) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_TDECK_PRO)
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    // Ten lines, fixed, and the number is measured rather than chosen: a full
    // 200-character message wraps to about nine lines at this width — word wrap
    // leaves a ragged right, so the useful count is nearer 21 characters a line
    // than the ~28 the pixel width suggests. Ten holds the longest message the
    // box accepts with one line spare.
    //
    // Fixed, not grown and not content-sized. Filling the panel left a full
    // message using two thirds of the box and the rest empty; sizing to the
    // content instead made an empty composer collapse to three lines and jump
    // about as you typed. A box that is always the size of the thing it has to
    // hold is neither.
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 10) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_TDECK) || defined(DEVICE_M9)
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 3) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_MESH_DECK)
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    // Six lines. The panel is 320x240 and compose is a modal activity, so there
    // is no reason to leave most of it empty — a 200-character message is about
    // six lines at this width, meaning the whole thing is visible while typing.
    // The centre band flex-grows around this, so the box stays vertically
    // centred and the reply variant simply gets less slack rather than clipping.
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 6) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 4;
    const lv_coord_t composeModalRowPad = 2;
#elif UI_CARDPUTER_COMPACT
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    // Compose is full screen here, so the input gets four lines instead of the
    // single one the old boxed modal had room for. 200 characters never fit on
    // one 240 px line, and the text scrolled sideways out of view as you typed.
    //
    // Four is the ceiling, set by the reply layout rather than this one: a
    // reply spends another 22 px on the quoted line, leaving the centre band
    // ~79 px, and the band does not scroll. Five lines (81 px) would overflow
    // it — the box is pinned to this height below, so it would clip instead of
    // shrinking to fit.
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 4) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 4;
    const lv_coord_t composeModalRowPad = 1;
#elif UI_TANMATSU_LAYOUT
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    // Two lines, wrapping, so a message reads as you type it instead of
    // scrolling sideways out of one. The modal grows by the same amount
    // (modalH below), so nothing else in it gives up room.
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 2) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 4;
    const lv_coord_t composeModalRowPad = 3;
#else
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    const lv_coord_t composeInputH = (lv_coord_t)(lv_font_get_line_height(composeBodyFont) + 8);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 4;
    const lv_coord_t composeModalRowPad = 3;
#endif
    const lv_coord_t composeReplyRowH = (lv_coord_t)(lv_font_get_line_height(composeBodyFont) + 6);

    closeComposePrompt();

    const bool isReply = (replyPacketId != 0) || (replyText && replyText[0]);
    s_composeTarget = COMPOSE_TARGET_CHANNEL;
    s_composeDmNodeId = 0;
    s_composeReplyPacketId = replyPacketId;
    s_composeChannelIdx = s_activeChannel;

#if UI_TOUCH_ONLY_PROFILE
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    int modalW = lv_disp_get_hor_res(NULL);
#else
    int modalW = lv_disp_get_hor_res(NULL) - 8;
#endif
    int modalH = lv_disp_get_ver_res(NULL) - 8;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL);
    if (modalH < 120) modalH = lv_disp_get_ver_res(NULL);

    s_composeModal = lv_obj_create(s_rootScreen);
    const bool composeDocked = composeDockedOnKeyboard();
    if (composeDocked) {
        // Only as tall as what it holds, sitting on the bottom edge: the
        // keyboard here is a fixed height (configureOnScreenKeyboard), so a
        // full-screen modal left a band of nothing between it and the buttons.
        // Content-sized, the buttons land on the keyboard, the input and title
        // stack above them, and the conversation stays readable over the top.
        lv_obj_set_size(s_composeModal, modalW, LV_SIZE_CONTENT);
        lv_obj_align(s_composeModal, LV_ALIGN_BOTTOM_MID, 0, -4);
    } else {
        lv_obj_set_size(s_composeModal, modalW, modalH);
        lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_clear_flag(s_composeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_composeModal, LV_OBJ_FLAG_CLICKABLE);
    composeStyleModal(s_composeModal, modalW, modalH);
    lv_obj_set_style_pad_all(s_composeModal, 4, 0);
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    lv_obj_set_style_pad_left(s_composeModal, 2, 0);
    lv_obj_set_style_pad_right(s_composeModal, 2, 0);
#endif
    lv_obj_set_style_pad_bottom(s_composeModal, composeModalBottomPad, 0);
    lv_obj_set_style_pad_row(s_composeModal, composeModalRowPad, 0);
    lv_obj_set_flex_flow(s_composeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_composeModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_composeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    composeStyleTitle(title);
    // Ellipsised, not wrapped. Channel names are user-set and can be long, and
    // the default wrap would take a second line out of an input box that is
    // measured to the pixel on these panels.
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    s_composeTitle = title;
    composeSetTitle(isReply);

    if (isReply) {
        char preview[kReplyPreviewTextMax + 1];
        formatReplyPreview(replyText, preview, sizeof(preview));
        lv_obj_t *replyLbl = lv_label_create(s_composeModal);
        lv_obj_set_width(replyLbl, lv_pct(100));
        lv_obj_set_height(replyLbl, lv_font_get_line_height(&lv_font_montserrat_10));
        lv_obj_set_style_text_font(replyLbl, emojiFont(&lv_font_montserrat_10), 0);
        composeStyleQuote(replyLbl);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        setLabelTextEmojiSafe(replyLbl, preview[0] ? preview : "(message)");
    }

    s_composeInput = lv_textarea_create(s_composeModal);
    if (!s_composeInput) {
        logLvglMemDiag("compose open aborted (low LVGL mem)");
        closeComposePrompt();
        return;
    }
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_style_text_font(s_composeInput, emojiFont(&lv_font_montserrat_14), 0);
    // A physical keyboard means no on-screen one below (see the end of this
    // function), so an undocked modal -- full height -- has nothing to fill its
    // lower half. The Mesh Deck with its keyboard attached: the message box was
    // a single 44 px line with empty panel under it.
    const bool composeFillsModal = !composeDocked && keyboardAttached();
    if (composeDocked || composeFillsModal) {
        // Wrapping, several lines tall, so a message reads as you write it
        // rather than scrolling sideways out of a single line. Four in
        // portrait, where the panel has the room; two in landscape (the P4),
        // where the half-height keyboard leaves about that much.
        // Measured from the face, with explicit padding rather than the
        // theme's, so the box is exactly that many lines. With the keyboard
        // expansion on there is no on-screen keyboard below (see the end of
        // this function), and its room goes to the message.
        const lv_coord_t composeLines = keyboardAttached() ? (uiPortrait() ? 8 : 4)
                                                           : (uiPortrait() ? 4 : 2);
        const lv_coord_t composePad = 4;
        lv_obj_set_style_pad_all(s_composeInput, composePad, 0);
        const lv_coord_t composeLinesH =
            lv_font_get_line_height(emojiFont(&lv_font_montserrat_14)) * composeLines
            + 2 * composePad + 2;   // + 1 px border each side
        if (composeFillsModal) {
            // Grows into the whole modal between the title and the buttons;
            // the line count is only its floor.
            lv_obj_set_style_min_height(s_composeInput, composeLinesH, 0);
            lv_obj_set_flex_grow(s_composeInput, 1);
        } else {
            lv_obj_set_height(s_composeInput, composeLinesH);
        }
        lv_textarea_set_one_line(s_composeInput, false);
        // A wrapping box would take the keyboard's Enter as a line break. In
        // the one-line box every other touch board uses, the textarea drops
        // that character, and a mesh message is one line either way -- so drop
        // it here too and keep Enter doing what it does there.
        lv_obj_add_event_cb(s_composeInput,
                            [](lv_event_t *e) {
                                const char *ins = (const char *)lv_event_get_param(e);
                                if (ins && strcmp(ins, "\n") == 0) {
                                    lv_textarea_set_insert_replace(
                                        lv_event_get_target_obj(e), "");
                                }
                            },
                            LV_EVENT_INSERT, nullptr);
    } else {
        lv_obj_set_height(s_composeInput, 44);
        lv_textarea_set_one_line(s_composeInput, true);
    }
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, TR("Type message..."));
    showTextareaCursor(s_composeInput);
    composeStyleInput(s_composeInput);
    lv_obj_add_event_cb(s_composeInput, onComposeInputChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    accentAttach(s_composeInput);
    spellAttach(s_composeInput);

    s_composeCharCount = lv_label_create(s_composeModal);
    lv_obj_set_width(s_composeCharCount, lv_pct(100));
    lv_obj_set_style_text_font(s_composeCharCount, &lv_font_montserrat_10, 0);
    composeStyleMeta(s_composeCharCount);
    lv_obj_set_style_text_align(s_composeCharCount, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *row = lv_obj_create(s_composeModal);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 28);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Emoji tray opener. Touch-only builds have no keyboard key to bind, so the
    // button is the only way in here; it's narrow (fixed width, no flex-grow) so
    // Cancel/Send keep the room.
    lv_obj_t *emojiBtn = lv_btn_create(row);
    lv_obj_set_size(emojiBtn, 34, lv_pct(100));
    composeStyleButton(emojiBtn, /*primary=*/false);
    lv_obj_add_event_cb(emojiBtn, [](lv_event_t *) { openEmojiPicker(false); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_t *emojiLbl = lv_label_create(emojiBtn);
    lv_obj_set_style_text_font(emojiLbl, emojiFont(&lv_font_montserrat_16), 0);
    setLabelTextEmojiSafe(emojiLbl, TR("\U0001F600"));
    lv_obj_center(emojiLbl);

    lv_obj_t *cancelBtn = lv_btn_create(row);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onComposeCancelPressed, LV_EVENT_CLICKED, nullptr);
    composeStyleButton(cancelBtn, /*primary=*/false);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text_fmt(cancelLbl, "%s  %s", LV_SYMBOL_CLOSE, TR("Cancel"));
    lv_obj_center(cancelLbl);

    lv_obj_t *sendBtn = lv_btn_create(row);
    lv_obj_set_flex_grow(sendBtn, 1);
    lv_obj_set_height(sendBtn, lv_pct(100));
    lv_obj_add_event_cb(sendBtn, onComposeSendPressed, LV_EVENT_CLICKED, nullptr);
    composeStyleButton(sendBtn, /*primary=*/true);
    lv_obj_t *sendLbl = lv_label_create(sendBtn);
    lv_obj_set_style_text_font(sendLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text_fmt(sendLbl, "%s  %s", TR("Send"), LV_SYMBOL_RIGHT);
    lv_obj_center(sendLbl);

    // Not with a physical keyboard attached (the T-Display P4's expansion): it
    // would take half the panel from a message typed on the keys under it.
    // Checked as compose opens; attaching one mid-message takes effect on the
    // next.
    if (!keyboardAttached()) {
        s_composeKeyboard = lv_keyboard_create(s_composeModal);
        configureOnScreenKeyboard(s_composeKeyboard);
        lv_keyboard_set_textarea(s_composeKeyboard, s_composeInput);
        lv_obj_add_event_cb(s_composeKeyboard, onComposeKeyboardEvent, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(s_composeKeyboard, onComposeKeyboardEvent, LV_EVENT_CANCEL, nullptr);
    }
#else
    int modalW = lv_disp_get_hor_res(NULL) - 24;
    if (modalW < 140) modalW = lv_disp_get_hor_res(NULL) - 8;
    int modalH = isReply ? 100 : 76;
#if defined(DEVICE_TLORA_PAGER_TFT)
    modalH = isReply ? 138 : 116;
#elif defined(DEVICE_TDECK_PRO)
    // Measured from the parts rather than picked: 4 px of top padding, the
    // title, the input, and the two reserved footer rows. No slack anywhere, so
    // there is no gap between the box and anything around it — which is the
    // whole reason this is computed instead of being a round number.
    modalH = (int)(4
                   + lv_font_get_line_height(&lv_font_montserrat_10)
                   + composeModalRowPad
                   + composeInputH
                   + composeModalRowPad
                   + lv_font_get_line_height(composeBodyFont)   // character count
                   + composeModalRowPad
                   + lv_font_get_line_height(composeBodyFont)   // keyboard legend
                   + composeModalBottomPad);
    if (isReply) modalH += (int)(composeReplyRowH + composeModalRowPad);
    const int composeMaxModalH = lv_disp_get_ver_res(NULL) - 16;
    if (modalH > composeMaxModalH) modalH = composeMaxModalH;
#elif defined(DEVICE_TDECK) || defined(DEVICE_M9)
    modalH = isReply ? 126 : 104;
#elif defined(DEVICE_MESH_DECK)
    // Nearly the full panel height, centred, so it expands both up and down.
    // The old size came from the shared default (76 px, or 100 for a reply),
    // which was written for much smaller screens and left most of this one
    // unused.
    modalH = lv_disp_get_ver_res(NULL) - 36;
#elif UI_CARDPUTER_COMPACT
    // Full screen. On a 240x135 panel a boxed modal spent most of its height on
    // margin and border; composing is a modal activity anyway, so it takes the
    // whole display and the room goes to the message.
    modalW = lv_disp_get_hor_res(NULL);
    modalH = lv_disp_get_ver_res(NULL);
#elif UI_TANMATSU_LAYOUT
    // The shared size, plus what the second input line adds over the
    // one-line box it was sized for.
    modalH += (int)(composeInputH - (lv_font_get_line_height(composeBodyFont) + 8));
#endif

    s_composeModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_composeModal, modalW, modalH);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, -12);
#elif UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) || defined(DEVICE_TDECK_PRO)
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, 0);   // centred, grows both ways
#else
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, 10);
#endif
    lv_obj_clear_flag(s_composeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_composeModal, LV_OBJ_FLAG_CLICKABLE);
    composeStyleModal(s_composeModal, modalW, modalH);
    lv_obj_set_style_pad_all(s_composeModal, 4, 0);
    lv_obj_set_style_pad_bottom(s_composeModal, composeModalBottomPad, 0);
    lv_obj_set_style_pad_row(s_composeModal, composeModalRowPad, 0);
    lv_obj_set_flex_flow(s_composeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_composeModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // No bottom-padding reserve on the Pro any more. It used to hold back two
    // rows for the footer, but lv_obj_align() aligns to the parent's CONTENT
    // area — padding excluded — so BOTTOM_LEFT put the legend at the bottom of
    // the *content*, over the input's last line, and left the reserved strip
    // below it empty. That strip was the white space under the box. The two
    // labels are ordinary column children below, which is both simpler and
    // impossible to misplace that way.

    lv_obj_t *title = lv_label_create(s_composeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    composeStyleTitle(title);
    // Ellipsised, not wrapped. Channel names are user-set and can be long, and
    // the default wrap would take a second line out of an input box that is
    // measured to the pixel on these panels.
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    s_composeTitle = title;
    composeSetTitle(isReply);

    if (isReply) {
        char preview[kReplyPreviewTextMax + 1];
        formatReplyPreview(replyText, preview, sizeof(preview));
        lv_obj_t *replyBox = lv_obj_create(s_composeModal);
        lv_obj_set_width(replyBox, lv_pct(100));
        lv_obj_set_height(replyBox, composeReplyRowH);
        lv_obj_clear_flag(replyBox, LV_OBJ_FLAG_SCROLLABLE);
        composeStyleQuote(replyBox);
        lv_obj_set_style_pad_right(replyBox, 4, 0);
        lv_obj_set_style_pad_top(replyBox, 1, 0);
        lv_obj_set_style_pad_bottom(replyBox, 1, 0);

        lv_obj_t *replyLbl = lv_label_create(replyBox);
        lv_obj_set_width(replyLbl, lv_pct(100));
        lv_obj_set_height(replyLbl, lv_font_get_line_height(composeBodyFont));
        lv_obj_set_style_text_font(replyLbl, composeBodyFont, 0);
        lv_obj_set_style_text_color(replyLbl, lvColorFrom565(s_ui.textDim), 0);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        setLabelTextEmojiSafe(replyLbl, preview[0] ? preview : "(message)");
    }

    lv_obj_t *composeInputHost = s_composeModal;
// No band on the Pro: its box is sized from the message (below), so there is
// never slack for a band to centre it in — the band would just be somewhere for
// empty space to live.
#if defined(DEVICE_TDECK) || UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_M9)
    lv_obj_t *composeCenterBand = lv_obj_create(s_composeModal);
    lv_obj_set_width(composeCenterBand, lv_pct(100));
    // Both grow to take whatever the title and reply row leave. On Cardputer
    // this is what keeps the bottom legend on the bottom edge now that the
    // modal is full screen: the band eats the slack, so the legend stays the
    // last thing in the column instead of floating up under the input.
    lv_obj_set_flex_grow(composeCenterBand, 1);
    lv_obj_clear_flag(composeCenterBand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(composeCenterBand, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(composeCenterBand, 0, 0);
    lv_obj_set_style_pad_all(composeCenterBand, 0, 0);
    lv_obj_set_flex_flow(composeCenterBand, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(composeCenterBand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    composeInputHost = composeCenterBand;
#endif

#if HAS_COMPOSE_EMOJI_BTN
    // The box and the emoji button share a row, so the button sits to the right
    // of the message rather than under it. Built after the centre band on the
    // boards that have one, and parented to whatever host that left behind, so
    // the two arrangements compose instead of fighting over the same variable.
    //
    // The row takes the vertical slack its parent has; the box takes the
    // horizontal slack inside the row, which keeps the button a fixed width at
    // the right-hand end however tall the modal ends up.
    lv_obj_t *composeInputRow = lv_obj_create(composeInputHost);
    lv_obj_set_width(composeInputRow, lv_pct(100));
    lv_obj_set_flex_grow(composeInputRow, 1);
    lv_obj_set_style_min_height(composeInputRow, composeInputH, 0);
    lv_obj_clear_flag(composeInputRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(composeInputRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(composeInputRow, 0, 0);
    lv_obj_set_style_pad_all(composeInputRow, 0, 0);
    lv_obj_set_style_pad_column(composeInputRow, 4, 0);
    lv_obj_set_flex_flow(composeInputRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(composeInputRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    composeInputHost = composeInputRow;
#endif

    s_composeInput = lv_textarea_create(composeInputHost);
    if (!s_composeInput) {
        logLvglMemDiag("compose open aborted (low LVGL mem)");
        closeComposePrompt();
        return;
    }
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_height(s_composeInput, composeInputH);
#if defined(DEVICE_TLORA_PAGER_TFT)
    // Same idea as the band boards below, but this layout has no band: the
    // legend and counter are IGNORE_LAYOUT and bottom-anchored, so the flex
    // column does not know they are there. Growing the input into the slack
    // would slide it underneath them, so reserve their line as bottom padding
    // on the modal first and let the box take what is genuinely left.
    lv_obj_set_style_pad_bottom(
        s_composeModal,
        (lv_coord_t)(lv_font_get_line_height(&lv_font_montserrat_10) + composeModalBottomPad), 0);
    // The row is the thing that grows vertically now; the box fills it and takes
    // the horizontal slack beside the button.
    lv_obj_set_height(s_composeInput, lv_pct(100));
    lv_obj_set_flex_grow(s_composeInput, 1);
#endif
#if (defined(DEVICE_TDECK) || UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) \
     || defined(DEVICE_M9)) && !HAS_COMPOSE_EMOJI_BTN
    // composeInputH is the FLOOR now, not the exact height. It used to be both:
    // min and max were pinned to it, so the centre band grew to take the slack
    // the title and reply row left and then handed none of it to the box —
    // every spare pixel became empty band above and below a short input.
    //
    // Growing into that slack costs nothing and buys the thing the modal is
    // for: more of the message visible while typing, which matters most on the
    // boards whose d-pad/wheel can now move the caret through it.
    lv_obj_set_style_min_height(s_composeInput, composeInputH, 0);
    lv_obj_set_flex_grow(s_composeInput, 1);
#endif
    lv_obj_set_style_text_font(s_composeInput, composeBodyFont, 0);
    lv_obj_set_style_pad_top(s_composeInput, composeInputPadTop, 0);
    lv_obj_set_style_pad_bottom(s_composeInput, 1, 0);
    lv_obj_set_style_pad_left(s_composeInput, 3, 0);
    lv_obj_set_style_pad_right(s_composeInput, 3, 0);
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || UI_CARDPUTER_COMPACT \
    || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9) || UI_TANMATSU_LAYOUT
    // Wrap instead of scrolling sideways. The min/max height above pins the box
    // to its fixed number of lines, so a longer message scrolls inside it
    // rather than growing the box into the legend.
    lv_textarea_set_one_line(s_composeInput, false);
#else
    lv_textarea_set_one_line(s_composeInput, true);
#endif
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, TR("Type message..."));
    showTextareaCursor(s_composeInput);
    composeStyleInput(s_composeInput);
    lv_obj_add_event_cb(s_composeInput, onComposeInputChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    accentAttach(s_composeInput);
    spellAttach(s_composeInput);

#if HAS_COMPOSE_EMOJI_BTN
    // The emoji button, at the right-hand end of the row the box shares. Narrow
    // and fixed width: the message is what should get the space.
#if defined(DEVICE_TLORA_PAGER_TFT)
    s_composeEmojiFocused = false;
#endif
    s_composeEmojiBtn = lv_btn_create(composeInputRow);
    if (s_composeEmojiBtn) {
        lv_obj_set_width(s_composeEmojiBtn, 30);
        lv_obj_set_height(s_composeEmojiBtn, lv_pct(100));
        lv_obj_set_style_radius(s_composeEmojiBtn, 5, 0);
        lv_obj_set_style_pad_all(s_composeEmojiBtn, 0, 0);
        lv_obj_set_style_shadow_width(s_composeEmojiBtn, 0, 0);
        // Clickable as well as wheel-reachable. This board has no touch panel,
        // but the browser Remote registers a real pointer and forwards taps.
        lv_obj_add_event_cb(s_composeEmojiBtn,
                            [](lv_event_t *) { openEmojiPicker(/*sendMode=*/false); },
                            LV_EVENT_CLICKED, nullptr);

        lv_obj_t *emojiLbl = lv_label_create(s_composeEmojiBtn);
        lv_obj_set_style_text_font(emojiLbl, emojiFont(&lv_font_montserrat_16), 0);
        setLabelTextEmojiSafe(emojiLbl, TR("\U0001F600"));
        lv_obj_center(emojiLbl);
#if defined(DEVICE_TLORA_PAGER_TFT)
        // Only the Pager paints a focus: it is the one board here reaching this
        // button with something other than a finger.
        refreshComposeEmojiFocus();
#else
        lv_obj_set_style_bg_color(s_composeEmojiBtn, lvColorFrom565(s_ui.panelStrong), 0);
        lv_obj_set_style_bg_opa(s_composeEmojiBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_composeEmojiBtn, 1, 0);
        lv_obj_set_style_border_color(s_composeEmojiBtn, lvColorFrom565(s_ui.dividerHi), 0);
#endif
    }
#endif

    lv_obj_t *hint = lv_label_create(s_composeModal);
    lv_obj_set_width(hint, lv_pct(100));
#if UI_CARDPUTER_COMPACT
    // The legend is reference text, not content: at 10 it stops competing with
    // the message for the narrow bottom line, and leaves the character count
    // beside it room to sit without colliding.
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#else
    lv_obj_set_style_text_font(hint, composeBodyFont, 0);
#endif
    composeStyleMeta(hint);
    lv_obj_set_style_pad_top(hint, 0, 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#elif defined(DEVICE_TDECK) || UI_CARDPUTER_COMPACT || defined(DEVICE_M9)
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#else
    lv_obj_set_style_pad_bottom(hint, 1, 0);
#endif
#if UI_CARDPUTER_COMPACT
    // Emoji isn't a compose action anymore — it's the 'E' quick-send tray on the
    // chat/DM screen (see openEmojiPicker), so it's off the compose legend.
    lv_label_set_text(hint, TR("Enter=Send  Esc=Cancel  Bksp=Del"));
#else
    lv_label_set_text(hint, TR("Enter=Send  Bksp(empty)=Cancel"));
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    // Keep the legend anchored near the bottom edge, independent of flex slack.
    lv_obj_add_flag(hint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(hint, modalW - 8);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 4, -composeModalBottomPad);
#elif defined(DEVICE_TDECK_PRO)
    // In the column, not floated: the legend is the last row of the modal and
    // the modal's height is measured to include it, so it needs no anchoring.
    // It is moved below the character count once that exists, further down.
    lv_obj_set_width(hint, lv_pct(100));
#endif

    // Live "x of 200" countdown. Most builds share the hint line; T-Deck Pro
    // reserves the line above it so the two labels cannot overlap.
    s_composeCharCount = lv_label_create(s_composeModal);
    lv_obj_add_flag(s_composeCharCount, LV_OBJ_FLAG_IGNORE_LAYOUT);
#if UI_CARDPUTER_COMPACT
    // Matches the shrunk legend it shares the bottom line with.
    lv_obj_set_style_text_font(s_composeCharCount, &lv_font_montserrat_10, 0);
#else
    lv_obj_set_style_text_font(s_composeCharCount, composeBodyFont, 0);
#endif
    composeStyleMeta(s_composeCharCount);
#if UI_CARDPUTER_COMPACT
    // Flush with the legend's line rather than lifted off it: the legend is the
    // last item in the column, so its box already ends at the content bottom.
    lv_obj_align(s_composeCharCount, LV_ALIGN_BOTTOM_RIGHT, -4, 0);
#elif defined(DEVICE_TDECK_PRO)
    // Right-aligned in its own full-width row, directly under the input, with
    // the legend moved below it — the same two-row footer as before, built out
    // of layout instead of two absolute positions that had to agree with a
    // padding value to land correctly.
    lv_obj_clear_flag(s_composeCharCount, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_composeCharCount, lv_pct(100));
    lv_obj_set_style_text_align(s_composeCharCount, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_move_to_index(hint, (int32_t)lv_obj_get_child_count(s_composeModal) - 1);
#else
    lv_obj_align(s_composeCharCount, LV_ALIGN_BOTTOM_RIGHT, -4, -composeModalBottomPad);
#endif
#endif

    updateComposeCharCount();
}

static void onComposeKeyboardEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        sendComposeMessage();
    } else if (code == LV_EVENT_CANCEL) {
        closeComposePrompt();
    }
}

static void onComposeSendPressed(lv_event_t *e) {
    LV_UNUSED(e);
    sendComposeMessage();
}

static void onComposeCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeComposePrompt();
}

// Live "x of 200" character countdown shown at the bottom-right of the
// composing panel. Counts UTF-8 characters so it matches the textarea's
// max-length enforcement (which also counts characters, not bytes).
static void updateComposeCharCount() {
    if (!s_composeCharCount || !s_composeInput) return;
    const char *txt = lv_textarea_get_text(s_composeInput);
    // v9 moved the old _lv_txt_get_encoded_length into lv_text_private.h, which
    // public lvgl.h does not expose. Counting non-continuation bytes gives the
    // same UTF-8 character count without reaching into LVGL internals.
    uint32_t used = 0;
    for (const char *p = txt; p && *p; p++) {
        if (!utf8util::isContinuationByte((uint8_t)*p)) used++;
    }
    // Dim while there is room, the theme's warning amber in the last tenth, and
    // its red once the box is full and further keys do nothing -- the count is
    // the only thing that says why typing stopped. (One colour on the paper
    // build, whose palette has no amber or red.)
    uint16_t countColor = s_ui.textDim;
    if (used >= (uint32_t)MESH_TEXT_MAX_LEN) {
        countColor = s_ui.battBad;
    } else if (used * 10 >= (uint32_t)MESH_TEXT_MAX_LEN * 9) {
        countColor = s_ui.battWarn;
    }
    lv_obj_set_style_text_color(s_composeCharCount, lvColorFrom565(countColor), 0);
#if UI_CARDPUTER_COMPACT
    // "200/200" rather than "200 of 200": the count shares its line with the
    // key legend, and on a 240 px panel the long form runs into it once the
    // message passes 100 characters.
    lv_label_set_text_fmt(s_composeCharCount, TR("%u/%d"),
                          (unsigned)used, (int)MESH_TEXT_MAX_LEN);
#else
    lv_label_set_text_fmt(s_composeCharCount, TR("%u of %d"),
                          (unsigned)used, (int)MESH_TEXT_MAX_LEN);
#endif
}

static void onComposeInputChanged(lv_event_t *e) {
    LV_UNUSED(e);
    updateComposeCharCount();
}

static void openComposePromptForDm(uint32_t nodeId) {
    if (nodeId == 0) return;
    openComposePrompt(0, nullptr, false);
    s_composeTarget = COMPOSE_TARGET_DM;
    s_composeDmNodeId = nodeId;
    // openComposePrompt() titled this for a channel, because that is what every
    // other caller wants. Now that the target is known, say so.
    composeSetTitle(false);
}

// MECP (issue #158): a MAYDAY is confirmed before it is sent, however it was
// written. Set only for the one send the confirmation answers.
static bool s_mecpMaydayConfirmed = false;
static void mecpMaydayConfirmCommit(int) {
    s_mecpMaydayConfirmed = true;
    sendComposeMessage();
    s_mecpMaydayConfirmed = false;
}

static void sendComposeMessage() {
    if (!s_composeInput) return;

    const char *msg = lv_textarea_get_text(s_composeInput);
    if (!msg || !msg[0]) return;

    // MECP allows no trailing whitespace, and the composer leaves a space for
    // free text that may never be typed.
    char mecpTrimmed[512];
    if (mecp::looksLikeMecp(msg)) {
        size_t n = utf8util::copyTruncate(mecpTrimmed, sizeof(mecpTrimmed), msg);
        while (n > 0 && (mecpTrimmed[n - 1] == ' ' || mecpTrimmed[n - 1] == '\t'
                         || mecpTrimmed[n - 1] == '\r' || mecpTrimmed[n - 1] == '\n'))
            mecpTrimmed[--n] = '\0';
        msg = mecpTrimmed;
    }
    {
        mecp::Envelope env;
        if (!s_mecpMaydayConfirmed && mecp::parse(msg, env) && env.severity == mecp::SEV_MAYDAY) {
            openCfgConfirmModal(-1, TR("Send a MAYDAY? Use it only when life is in danger."),
                                mecpMaydayConfirmCommit, 0);
            return;
        }
    }

    int txChan = (s_composeChannelIdx >= 0 && s_composeChannelIdx < MESH_CHANNELS)
               ? s_composeChannelIdx
               : s_activeChannel;
    if (txChan < 0 || txChan >= MESH_CHANNELS) txChan = 0;

    if (s_myNodeId == 0) {
        deriveNodeId();
    }
    if (s_myNodeId == 0) {
        if (s_composeTarget == COMPOSE_TARGET_DM && s_composeDmNodeId != 0) {
            DMs.addMessage(s_composeDmNodeId, nullptr, "", TR("! TX failed (node id)"), TFT_RED,
                           false, -1, 0);
        } else {
            Channels.addMessage(txChan, "", TR("! TX failed (node id)"), TFT_RED, 0);
        }
        closeComposePrompt();
        refreshChatView(true);
        return;
    }

    bool sentOk = false;
    if (s_composeTarget == COMPOSE_TARGET_DM && s_composeDmNodeId != 0) {
        sentOk = DMs.sendDm(s_myNodeId, s_composeDmNodeId, msg);
        if (!sentOk) {
            DMs.addMessage(s_composeDmNodeId, nullptr, "", TR("! TX failed"), TFT_RED,
                           false, -1, 0);
        }
    } else {
        sentOk = Channels.sendText(s_myNodeId, msg, s_cfg.okToMqtt, txChan, s_composeReplyPacketId);
        if (!sentOk) {
            Channels.addMessage(txChan, "", TR("! TX failed"), TFT_RED, 0);
        }
    }

    closeComposePrompt();
    refreshChatView(true);
    refreshDmModal(true);
}

