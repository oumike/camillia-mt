// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Nodes list, search and map tiles.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static bool nodesShortNameDisplayable(const char *shortName) {
    if (liveShortNameUsable(shortName)) return true;
    if (!shortName || !shortName[0]) return false;

    const uint8_t *p = (const uint8_t *)shortName;
    int codepoints = 0;
    bool hasNonAscii = false;

    while (*p) {
        uint8_t c = *p;
        int seqLen = 0;
        if (c < 0x80) {
            // Accept emoji-only/non-ASCII fallback here; printable ASCII is
            // already handled by liveShortNameUsable.
            return false;
        } else if ((c & 0xE0) == 0xC0) {
            seqLen = 2;
        } else if ((c & 0xF0) == 0xE0) {
            seqLen = 3;
        } else if ((c & 0xF8) == 0xF0) {
            seqLen = 4;
        } else {
            return false;
        }

        for (int i = 1; i < seqLen; i++) {
            if ((p[i] & 0xC0) != 0x80) return false;
        }

        p += seqLen;
        codepoints++;
        hasNonAscii = true;
        if (codepoints > 4) return false;
    }

    return hasNonAscii && codepoints > 0;
}

// Total rows in the combined space. Every bound in this screen is against this,
// not s_nodesSnapshotCount, which is only the live half.
static int nodesRowCountTotal() {
    return s_nodesSnapshotCount + s_nodesArchivedCount;
}

// The archived record behind a combined index, or null for a live row.
static const ArchivedNode *nodesArchivedAt(int snapshotIdx) {
    if (snapshotIdx < s_nodesSnapshotCount) return nullptr;
    const int slot = snapshotIdx - s_nodesSnapshotCount;
    if (slot < 0 || slot >= s_nodesArchivedCount) return nullptr;
    return nodeArchiveIndexAt(slot);
}

// Node id for either half. 0 when the index is out of range.
static uint32_t nodesRowNodeId(int snapshotIdx) {
    if (snapshotIdx < 0) return 0;
    if (snapshotIdx < s_nodesSnapshotCount) return s_nodesSnapshotIds[snapshotIdx];
    const ArchivedNode *a = nodesArchivedAt(snapshotIdx);
    return a ? a->nodeId : 0;
}

// Full archived record for one id, read from the card at most once per id.
// Returns null when there is no card, the row is gone, or the record will not
// parse — callers fall back to what the index alone can show.
static const NodeEntry *nodesArchivedDetail(uint32_t nodeId) {
    if (nodeId == 0) return nullptr;
    if (s_nodesArchDetailValid && s_nodesArchDetailId == nodeId) return &s_nodesArchDetail;
    // Negative results are cached too (Valid stays false but the id is claimed
    // below), so a record that will not parse is not re-read on every repaint.
    if (s_nodesArchDetailId == nodeId) return nullptr;

    s_nodesArchDetailId = nodeId;
    s_nodesArchDetailValid = nodeArchiveLoadFull(nodeId, s_nodesArchDetail);
    return s_nodesArchDetailValid ? &s_nodesArchDetail : nullptr;
}

static void nodesArchivedDetailInvalidate() {
    s_nodesArchDetailId = 0;
    s_nodesArchDetailValid = false;
}

// Whether the archived half should be built and shown at all. Three separate
// conditions, deliberately: the board may have no slot (compile-time), the slot
// may be empty right now (a card can be pulled between one open and the next),
// and the user may simply want a live-mesh view.
static bool nodesArchiveVisible() {
    return nodeArchiveSlotExists() && nodeArchiveAvailable() && s_cfg.nodeArchiveShow;
}

// Keeps any search in force: this is also called to rebuild the list while the
// screen is up (a favorite toggled, a node restored or deleted), and those
// should not throw away what the user searched for. openNodesModal() starts
// each visit with no search.
static void snapshotNodesForModal() {
    s_nodesSnapshotCount = 0;
    s_nodesArchivedCount = 0;
    s_nodesFilteredCount = 0;
    s_nodesSelected = -1;

    int total = Nodes.count();
    if (total < 0) total = 0;
    if (total > MAX_NODES) total = MAX_NODES;

    for (int i = 0; i < total && s_nodesSnapshotCount < MAX_NODES; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n || n->nodeId == 0) continue;
        if (!nodesShortNameDisplayable(n->shortName)) continue;
        if (nodesSnapshotContains(n->nodeId)) continue;
        s_nodesSnapshotIds[s_nodesSnapshotCount++] = n->nodeId;
    }

    // The one place the archive is read for the list. Deliberately here and not
    // in a refresh path: this touches the SD card, which shares the SPI bus with
    // the LoRa radio and would otherwise stall both the radio and LVGL on every
    // keypress. Built after the live half above, because the index skips ids
    // that are currently live and needs the table settled to tell.
    //
    // A card that has gone away since the last open simply yields nothing, and
    // the screen is the live-only one it has always been.
    nodeArchiveIndexClear();
    nodesArchivedDetailInvalidate();
    if (nodesArchiveVisible() && nodeArchiveIndexBuild()) {
        s_nodesArchivedCount = nodeArchiveIndexCount();
        if (s_nodesArchivedCount > NODE_ARCHIVE_INDEX_MAX) {
            s_nodesArchivedCount = NODE_ARCHIVE_INDEX_MAX;
        }
    }

    nodesApplyFilter();
}

static void nodesClampRenderWindow() {
    if (s_nodesFilteredCount <= 0) {
        s_nodesRenderStart = 0;
        return;
    }

    if (s_nodesSelected < 0) s_nodesSelected = 0;
    if (s_nodesSelected >= s_nodesFilteredCount) s_nodesSelected = s_nodesFilteredCount - 1;

    int renderRows = min(kNodesMaxRenderedRows, s_nodesFilteredCount);
    if (renderRows < 1) renderRows = 1;
    int maxStart = s_nodesFilteredCount - renderRows;
    if (maxStart < 0) maxStart = 0;

    if (s_nodesRenderStart > maxStart) s_nodesRenderStart = maxStart;
    if (s_nodesSelected < s_nodesRenderStart) {
        s_nodesRenderStart = s_nodesSelected;
    } else if (s_nodesSelected >= (s_nodesRenderStart + renderRows)) {
        s_nodesRenderStart = s_nodesSelected - renderRows + 1;
    }

    if (s_nodesRenderStart < 0) s_nodesRenderStart = 0;
    if (s_nodesRenderStart > maxStart) s_nodesRenderStart = maxStart;
}

static lv_obj_t *selectedNodesRowObj() {
    for (int i = 0; i < s_nodesListRowCount; i++) {
        if (s_nodesRenderedFilteredIdx[i] == s_nodesSelected) {
            return s_nodesListRows[i];
        }
    }
    return nullptr;
}

static inline char nodesAsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive glob over the whole of text: '*' is any run (even none) and
// '?' is any one character -- a whole UTF-8 sequence, so it stands for an emoji
// as readily as a letter. Backtracks only to the last '*', which is enough.
static bool nodesGlobMatchNoCase(const char *text, const char *pat) {
    if (!text) text = "";
    const char *starPat = nullptr;
    const char *starText = nullptr;
    while (*text) {
        if (*pat == '*') {
            starPat = pat++;
            starText = text;
        } else if (*pat == '?') {
            pat++;
            text++;
            while ((*(const uint8_t *)text & 0xC0) == 0x80) text++;
        } else if (*pat && nodesAsciiLower(*pat) == nodesAsciiLower(*text)) {
            pat++;
            text++;
        } else if (starPat) {
            pat = starPat + 1;
            text = ++starText;
        } else {
            return false;
        }
    }
    while (*pat == '*') pat++;
    return !*pat;
}

// A query with no wildcard keeps matching anywhere in the text, as it always
// has. One with a wildcard is a pattern for the whole text, so "mi*" means
// starts with "mi" and "*" means everything.
static bool nodesFilterMatches(const char *text, const char *query) {
    if (strpbrk(query, "*?")) return nodesGlobMatchNoCase(text, query);
    return dmNodePickerContainsNoCase(text, query);
}

static bool nodesFilterIsAll() {
    return s_nodesFilterOpen && strcmp(s_nodesFilter, "*") == 0;
}

#if NODES_KEYBOARD_TOP_BUTTONS
// Whether the keyboard cursor is drawn at all: always where there are keys,
// and on the P4 only while its keyboard is attached, so a P4 used by touch
// never shows a cursor it cannot move.
static bool nodesKeyboardCursorShown() {
#if UI_TOUCH_ONLY_PROFILE
    return keyboardAttached();
#else
    return true;
#endif
}
#endif

// An empty list has no row to hold the cursor, which left it nowhere and the
// arrows doing nothing (#116). Put it on the button that gets you a list:
// All for an empty Favorites, Clear search for a search with no matches.
// Enter presses it (nodesFocusedButtonActivate()). Left alone when it is
// already on a button, which is where the user put it.
static void nodesFocusCursorForEmptyList() {
#if NODES_KEYBOARD_TOP_BUTTONS
    if (s_nodesSearchBtnFocused || !nodesKeyboardCursorShown()) return;
    s_nodesSearchBtnFocused = true;
    s_nodesTopBtn = s_nodesFilterOpen ? kNodesTopSearch : kNodesTopAll;
    refreshNodesSearchButton();
    refreshNodesHint();
#endif
}

static void nodesApplyFilter() {
    s_nodesFilteredCount = 0;

    const int rows = nodesRowCountTotal();
    for (int i = 0; i < rows && s_nodesFilteredCount < kNodesRowMax; i++) {
        const uint32_t nodeId = nodesRowNodeId(i);
        // Live rows resolve against the table and are null if the node was
        // evicted since the snapshot, in which case only the id is matchable.
        // Archived rows carry their own names, which is the whole reason the
        // index stores them: matching on id alone would make the archived half
        // unsearchable by the name the user actually knows.
        const NodeEntry *n = (i < s_nodesSnapshotCount) ? Nodes.find(nodeId) : nullptr;
        const ArchivedNode *a = nodesArchivedAt(i);
        bool match;

        if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
            char nodeIdText[16];
            snprintf(nodeIdText, sizeof(nodeIdText), "!%08lX", (unsigned long)nodeId);
            match = nodesFilterMatches(nodeIdText, s_nodesFilter)
                 || (n && nodesFilterMatches(n->longName, s_nodesFilter))
                 || (n && nodesFilterMatches(n->shortName, s_nodesFilter))
                 || (a && nodesFilterMatches(a->longName, s_nodesFilter))
                 || (a && nodesFilterMatches(a->shortName, s_nodesFilter));
        } else {
            // No search: favorites only. Archived rows never qualify: the
            // favorite flag lives on the table entry, and favorites are never
            // evicted from it.
            match = (n && n->favorite);
        }

        if (match) {
            s_nodesFilteredIdx[s_nodesFilteredCount++] = i;
        }
    }

    if (s_nodesFilteredCount <= 0) {
        s_nodesSelected = -1;
        s_nodesRenderStart = 0;
        nodesFocusCursorForEmptyList();
        return;
    }

    if (s_nodesSelected < 0) {
        s_nodesSelected = 0;
    } else if (s_nodesSelected >= s_nodesFilteredCount) {
        s_nodesSelected = s_nodesFilteredCount - 1;
    }

    nodesClampRenderWindow();
}

static void applyNodesFilterText(const char *text) {
    if (!text) text = "";
    // Surrounding blanks are not part of a name anyone searches for, and a
    // query of nothing but blanks is no query: it clears the search.
    while (*text == ' ') text++;
    size_t len = strlen(text);
    while (len > 0 && text[len - 1] == ' ') len--;
    if (len > kNodesFilterMax) len = kNodesFilterMax;

    if (len > 0) {
        memcpy(s_nodesFilter, text, len);
        s_nodesFilter[len] = '\0';
        s_nodesFilterLen = (int)len;
        s_nodesFilterOpen = true;
    } else {
        s_nodesFilterOpen = false;
        s_nodesFilterLen = 0;
        s_nodesFilter[0] = '\0';
    }

    // A different set of rows: start at its top, with the keys on the list.
    s_nodesSearchBtnFocused = false;
    s_nodesTopBtn = kNodesTopSearch;
    s_nodesSelected = 0;
    s_nodesRenderStart = 0;
    if (s_nodesInfoFocused) {
        s_nodesInfoFocused = false;
        refreshNodesPanelFocusStyles();
    }

    nodesApplyFilter();
    refreshNodesListRows();
    refreshNodesListSelection();
    refreshNodesDetails();
    refreshNodesSearchButton();

    if (lvObjValid(s_nodesList)) lv_obj_scroll_to_y(s_nodesList, 0, LV_ANIM_OFF);
}

static void closeNodesFilterDialog() {
    lvObjDeleteSafe(s_nodesFilterDialog);
    s_nodesFilterInput = nullptr;
    s_nodesFilterKeyboard = nullptr;
}

static void onNodesFilterKeyboardEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *text = (s_nodesFilterInput) ? lv_textarea_get_text(s_nodesFilterInput) : "";
        applyNodesFilterText(text);
        closeNodesFilterDialog();
    } else if (code == LV_EVENT_CANCEL) {
        closeNodesFilterDialog();
    }
}

static void onNodesFilterInputEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY) return;
    const char *text = (s_nodesFilterInput) ? lv_textarea_get_text(s_nodesFilterInput) : "";
    applyNodesFilterText(text);
    closeNodesFilterDialog();
}

static void onNodesFilterCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeNodesFilterDialog();
}

static void onNodesFilterSearchPressed(lv_event_t *e) {
    LV_UNUSED(e);
    const char *text = (s_nodesFilterInput) ? lv_textarea_get_text(s_nodesFilterInput) : "";
    applyNodesFilterText(text);
    closeNodesFilterDialog();
}

static lv_obj_t *nodesFilterDialogBtn(lv_obj_t *row, const char *text, lv_event_cb_t cb,
                                      bool primary) {
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, UI_TOUCH_ONLY_PROFILE ? max(28, kStackBtnH) : 22);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_ver(btn, 1, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? 0x2A4E8F : 0x16386F), 0);
    lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(primary ? 0x90B4FF : 0x335D9D), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, UI_TOUCH_ONLY_PROFILE ? STACK_BTN_FONT
                                                          : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

// The Search dialog: a text field, Cancel and Search. On every board now, not
// only the touch ones. With an on-screen keyboard it takes the whole panel,
// for the keyboard's sake; with a physical one (every keyboard board, and the
// T-Display P4 with its expansion attached) it is a card over the list, and
// pumpKeyboardInput() types into it — Enter is Search and the close key is
// Cancel, which the hint line names for boards whose buttons cannot be tapped.
static void openNodesFilterDialog() {
    if (!s_nodesModal || s_nodesFilterDialog) return;

    // Only the touch-only builds can be without a physical keyboard (every
    // other board has one built in), and only they carry the on-screen one.
#if UI_TOUCH_ONLY_PROFILE
    const bool onScreenKeyboard = !keyboardAttached();
#else
    const bool onScreenKeyboard = false;
#endif
    const int dispW = lv_disp_get_hor_res(NULL);
    const int dispH = lv_disp_get_ver_res(NULL);

    // The whole panel either way, so nothing on the screen underneath takes a
    // tap while the dialog is up. With a physical keyboard the panel is a dim
    // backdrop and the dialog proper is a card centred on it.
    s_nodesFilterDialog = lv_obj_create(s_rootScreen);
    if (!s_nodesFilterDialog) {
        logLvglMemDiag("nodes search aborted (low LVGL mem)");
        return;
    }
    {
        int dialogW = dispW;
        int dialogH = dispH;
        if (dialogW < 180) dialogW = lv_obj_get_width(s_nodesModal);
        if (dialogH < 120) dialogH = lv_obj_get_height(s_nodesModal);
        lv_obj_set_size(s_nodesFilterDialog, dialogW, dialogH);
    }
    lv_obj_align(s_nodesFilterDialog, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_nodesFilterDialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nodesFilterDialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_nodesFilterDialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_radius(s_nodesFilterDialog, 0, 0);

    lv_obj_t *box = s_nodesFilterDialog;
    if (onScreenKeyboard) {
        lv_obj_set_style_border_width(box, 1, 0);
    } else {
        lv_obj_set_style_bg_color(s_nodesFilterDialog, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_nodesFilterDialog, LV_OPA_50, 0);
        lv_obj_set_style_border_width(s_nodesFilterDialog, 0, 0);
        lv_obj_set_style_pad_all(s_nodesFilterDialog, 0, 0);

        box = lv_obj_create(s_nodesFilterDialog);
        if (!box) {
            logLvglMemDiag("nodes search aborted (low LVGL mem)");
            closeNodesFilterDialog();
            return;
        }
        lv_obj_set_size(box, min(dispW - 16, 300), LV_SIZE_CONTENT);
        lv_obj_center(box);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(box, 6, 0);
        lv_obj_set_style_border_width(box, 2, 0);
    }
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(box, 4, 0);
#if UI_EDGE_TO_EDGE_TEXT_ENTRY
    if (onScreenKeyboard) {
        lv_obj_set_style_pad_left(box, 2, 0);
        lv_obj_set_style_pad_right(box, 2, 0);
    }
#endif
    lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(box);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, TR("Search Nodes"));

    s_nodesFilterInput = lv_textarea_create(box);
    if (!s_nodesFilterInput) {
        logLvglMemDiag("nodes search aborted (low LVGL mem)");
        closeNodesFilterDialog();
        return;
    }
    lv_obj_set_width(s_nodesFilterInput, lv_pct(100));
    lv_obj_set_height(s_nodesFilterInput, 28);
    lv_obj_set_style_text_font(s_nodesFilterInput, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_nodesFilterInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_nodesFilterInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_nodesFilterInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodesFilterInput, 1, 0);
    lv_obj_set_style_border_color(s_nodesFilterInput, lv_color_hex(0x4C76BA), 0);
    lv_obj_set_style_pad_left(s_nodesFilterInput, 3, 0);
    lv_obj_set_style_pad_right(s_nodesFilterInput, 3, 0);
    lv_textarea_set_one_line(s_nodesFilterInput, true);
    lv_textarea_set_max_length(s_nodesFilterInput, kNodesFilterMax);
    lv_textarea_set_placeholder_text(s_nodesFilterInput, TR("Name or !id, * and ? ok"));
    lv_textarea_set_text(s_nodesFilterInput, "");
    lv_obj_add_state(s_nodesFilterInput, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_nodesFilterInput, onNodesFilterInputEvent, LV_EVENT_READY, nullptr);

    lv_obj_t *btnRow = lv_obj_create(box);
    lv_obj_remove_style_all(btnRow);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(btnRow, 6, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    nodesFilterDialogBtn(btnRow, TR("Cancel"), onNodesFilterCancelPressed, false);
    nodesFilterDialogBtn(btnRow, TR("Search"), onNodesFilterSearchPressed, true);

    if (onScreenKeyboard) {
#if UI_TOUCH_ONLY_PROFILE
        s_nodesFilterKeyboard = lv_keyboard_create(box);
        configureOnScreenKeyboard(s_nodesFilterKeyboard);
        lv_keyboard_set_mode(s_nodesFilterKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_nodesFilterKeyboard, s_nodesFilterInput);
        lv_obj_add_event_cb(s_nodesFilterKeyboard, onNodesFilterKeyboardEvent, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(s_nodesFilterKeyboard, onNodesFilterKeyboardEvent, LV_EVENT_CANCEL, nullptr);
#endif
    } else {
        lv_obj_t *hint = lv_label_create(box);
        lv_obj_set_width(hint, lv_pct(100));
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
        lv_label_set_text_fmt(hint, TR("Enter=Search  %s=Cancel"), modalCloseKeyLabel());
    }
    lv_obj_move_foreground(s_nodesFilterDialog);
}

// The button above the list: Search while the list shows favorites, Clear
// while it shows search results. Space does the same on keyboard boards.
static void nodesSearchButtonActivate() {
    if (!s_nodesModal) return;
    if (s_nodesFilterOpen) {
        applyNodesFilterText("");
    } else {
        openNodesFilterDialog();
    }
}

// The All button: every node, by way of the "*" pattern.
static void nodesAllButtonActivate() {
    if (!s_nodesModal) return;
    applyNodesFilterText("*");
}

// The Map button: every node with a position, on one map.
static void nodesMapButtonActivate() {
#if HAS_NODE_LOCATE
    if (!s_nodesModal) return;
    openNodesMapModal();
#endif
}

// The topmost button the keyboard cursor can reach above the list.
static int nodesTopBtnLast() {
#if HAS_NODE_LOCATE
    return kNodesTopMap;
#else
    return kNodesTopAll;
#endif
}

// Enter on the keyboard cursor's button, whichever one it is on.
static void nodesFocusedButtonActivate() {
    switch (s_nodesTopBtn) {
        case kNodesTopMap: nodesMapButtonActivate();    break;
        case kNodesTopAll: nodesAllButtonActivate();    break;
        default:           nodesSearchButtonActivate(); break;
    }
}

static void styleNodesTopButton(lv_obj_t *btn, bool focused) {
    if (!lvObjValid(btn)) return;
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_border_width(btn, focused ? 3 : 1, 0);
#else
    lv_obj_set_style_bg_color(btn, lv_color_hex(focused ? 0x2A4E8F : 0x16386F), 0);
    lv_obj_set_style_border_width(btn, focused ? 2 : 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(focused ? 0x90B4FF : 0x5C86C6), 0);
#endif
}

// Label for what the button does now, and the cursor drawn the way a selected
// row draws it, so moving Up from the first row reads as one list.
static void refreshNodesSearchButton() {
    if (lvObjValid(s_nodesFilterBtnLabel)) {
        lv_label_set_text(s_nodesFilterBtnLabel,
                          s_nodesFilterOpen ? TR("Clear search") : TR("Search"));
    }
    styleNodesTopButton(s_nodesMapBtn, s_nodesSearchBtnFocused && s_nodesTopBtn == kNodesTopMap);
    styleNodesTopButton(s_nodesAllBtn, s_nodesSearchBtnFocused && s_nodesTopBtn == kNodesTopAll);
    styleNodesTopButton(s_nodesFilterBtn,
                        s_nodesSearchBtnFocused && s_nodesTopBtn == kNodesTopSearch);
}

#if NODES_KEYBOARD_TOP_BUTTONS
// Moves the keyboard cursor between the buttons above the list and the rows.
// btn picks which button (kNodesTop*) when focused is set.
static void nodesSetSearchBtnFocus(bool focused, int btn = kNodesTopSearch) {
    if (!focused) btn = kNodesTopSearch;
    if (focused == s_nodesSearchBtnFocused && btn == s_nodesTopBtn) return;
    s_nodesSearchBtnFocused = focused;
    s_nodesTopBtn = btn;
    refreshNodesSearchButton();
    refreshNodesListSelection();
    refreshNodesHint();
    if (focused && lvObjValid(s_nodesList)) {
        lv_obj_scroll_to_y(s_nodesList, 0, LV_ANIM_OFF);
    } else if (lv_obj_t *selectedRow = selectedNodesRowObj()) {
        lv_obj_scroll_to_view(selectedRow, LV_ANIM_OFF);
    }
}
#endif

static void onNodesFilterButtonPressed(lv_event_t *e) {
    LV_UNUSED(e);
    nodesSearchButtonActivate();
}

static void onNodesAllButtonPressed(lv_event_t *e) {
    LV_UNUSED(e);
    nodesAllButtonActivate();
}

#if HAS_NODE_LOCATE
static void onNodesMapButtonPressed(lv_event_t *e) {
    LV_UNUSED(e);
    nodesMapButtonActivate();
}
#endif

// One of the buttons above the list, full width; returns its label.
static lv_obj_t *buildNodesTopButton(lv_obj_t *listPanel, lv_obj_t **btnOut, int btnH,
                                     const lv_font_t *btnFont, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(listPanel);
    *btnOut = btn;
    if (!btn) return nullptr;
    lv_obj_set_width(btn, lv_pct(96));
    lv_obj_set_height(btn, btnH);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_hor(btn, 3, 0);
    lv_obj_set_style_pad_ver(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
#if defined(DEVICE_TDECK_PRO)
    const lv_color_t textColor = lv_color_make(0, 0, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(btn, lv_color_make(0, 0, 0), 0);
#else
    const lv_color_t textColor = lv_color_hex(0xE8F1FF);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, btnFont, 0);
    lv_obj_set_style_text_color(lbl, textColor, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_center(lbl);
    return lbl;
}

// Builds the Map, All and Search / Clear buttons at the top of the list pane and
// turns the pane into a column, so the list under them takes the remaining
// height.
static void buildNodesSearchButton(lv_obj_t *listPanel) {
    lv_obj_set_flex_flow(listPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(listPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(listPanel, 3, 0);

#if defined(DEVICE_CROWPANEL_50)
    const int btnH = 40;
    const lv_font_t *btnFont = &lv_font_montserrat_16;
#elif UI_TOUCH_ONLY_PROFILE
    // A finger target: the stacked layout's button height where it has one.
    const int btnH = toolStackLayout() ? kStackBtnH : 28;
    const lv_font_t *btnFont = toolStackLayout() ? STACK_BTN_FONT : &lv_font_montserrat_12;
#elif defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    const int btnH = 22;
    const lv_font_t *btnFont = &lv_font_montserrat_12;
#else
    const int btnH = 20;
    const lv_font_t *btnFont = &lv_font_montserrat_10;
#endif

#if HAS_NODE_LOCATE
    if (lv_obj_t *mapLbl = buildNodesTopButton(listPanel, &s_nodesMapBtn, btnH, btnFont,
                                               onNodesMapButtonPressed)) {
        lv_label_set_text(mapLbl, TR("Map"));
    }
#endif
    if (lv_obj_t *allLbl = buildNodesTopButton(listPanel, &s_nodesAllBtn, btnH, btnFont,
                                               onNodesAllButtonPressed)) {
        lv_label_set_text(allLbl, TR("All"));
    }
    s_nodesFilterBtnLabel = buildNodesTopButton(listPanel, &s_nodesFilterBtn, btnH, btnFont,
                                                onNodesFilterButtonPressed);
    refreshNodesSearchButton();
}

static bool nodesSnapshotContains(uint32_t nodeId) {
    if (nodeId == 0) return false;
    for (int i = 0; i < s_nodesSnapshotCount; i++) {
        if (s_nodesSnapshotIds[i] == nodeId) return true;
    }
    return false;
}

static bool nodesPanelCanDownloadTiles() {
    if (!kLiveDetailMapsCanDownload) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_AP;
}

static int nodesLiveDetailZoomForLod(int lod) {
    return (lod >= kLiveDetailMapLodBuilding)
               ? kLiveDetailMapZoomBuilding
               : kLiveDetailMapZoomStreet;
}

static void nodesPanelWifiEnter() {
    if (!kLiveDetailMapsCanDownload) return;
    if (s_nodesWifiSessionActive || webCfgRunning()) return;

    s_nodesWifiSessionActive = true;
    s_nodesWifiStateChanged = false;
    s_nodesWifiPrevMode = WiFi.getMode();
    s_nodesWifiPrevConnected = (WiFi.status() == WL_CONNECTED);

    if (s_nodesWifiPrevConnected) return;
    if (!wifiHasActiveCreds()) return;

    switch (s_nodesWifiPrevMode) {
        case WIFI_OFF:
            WiFi.mode(WIFI_STA);
            wifiBeginActiveKnown();
            s_nodesWifiStateChanged = true;
            break;
        case WIFI_STA:
            wifiBeginActiveKnown();
            s_nodesWifiStateChanged = true;
            break;
#ifdef WIFI_AP_STA
        case WIFI_AP:
            WiFi.mode(WIFI_AP_STA);
            wifiBeginActiveKnown();
            s_nodesWifiStateChanged = true;
            break;
#endif
        case WIFI_AP_STA:
            wifiBeginActiveKnown();
            s_nodesWifiStateChanged = true;
            break;
        default:
            break;
    }
}

static void nodesPanelWifiRestore() {
    if (!s_nodesWifiSessionActive) return;

    if (s_nodesWifiStateChanged) {
        switch (s_nodesWifiPrevMode) {
            case WIFI_OFF:
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                break;
            case WIFI_STA:
                WiFi.mode(WIFI_STA);
                if (!s_nodesWifiPrevConnected) {
                    WiFi.disconnect(false);
                }
                break;
            case WIFI_AP:
                WiFi.disconnect(false);
                WiFi.mode(WIFI_AP);
                break;
#ifdef WIFI_AP_STA
            case WIFI_AP_STA:
                WiFi.mode(WIFI_AP_STA);
                if (!s_nodesWifiPrevConnected) {
                    WiFi.disconnect(false);
                }
                break;
#endif
            default:
                break;
        }
    }

    s_nodesWifiSessionActive = false;
    s_nodesWifiStateChanged = false;
    s_nodesWifiPrevMode = WIFI_OFF;
    s_nodesWifiPrevConnected = false;
}

static bool nodesMapFsReadyCb(lv_fs_drv_t *drv) {
    LV_UNUSED(drv);
#if HAS_FILE_STORAGE
    return sdBegin();
#else
    return false;
#endif
}

class NodesMapFsGuard {
public:
    explicit NodesMapFsGuard(TickType_t waitTicks = portMAX_DELAY)
        : locked_(s_nodesMapFsMutex
                  && xSemaphoreTakeRecursive(s_nodesMapFsMutex, waitTicks) == pdTRUE) {}

    ~NodesMapFsGuard() {
        if (locked_) xSemaphoreGiveRecursive(s_nodesMapFsMutex);
    }

    bool locked() const { return locked_; }

private:
    bool locked_;
};

struct NodesMapFsFile {
    NodesMapFsGuard guard;
    File file;
};

static void *nodesMapFsOpenCb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    LV_UNUSED(mode);
#if HAS_FILE_STORAGE
    if (!path || !path[0]) return nullptr;

    NodesMapFsFile *fileCtx = new NodesMapFsFile();
    if (!fileCtx || !fileCtx->guard.locked()) {
        delete fileCtx;
        return nullptr;
    }
    if (!sdBegin()) {
        delete fileCtx;
        return nullptr;
    }

    String fullPath;
    if (path[0] == '/') fullPath = path;
    else {
        fullPath = "/";
        fullPath += path;
    }

    fileCtx->file = storageFs().open(fullPath.c_str(), FILE_READ);
    if (!fileCtx->file) {
        delete fileCtx;
        return nullptr;
    }
    return fileCtx;
#else
    LV_UNUSED(path);
    return nullptr;
#endif
}

static lv_fs_res_t nodesMapFsCloseCb(lv_fs_drv_t *drv, void *file_p) {
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    NodesMapFsFile *fileCtx = (NodesMapFsFile *)file_p;
    if (fileCtx->file) fileCtx->file.close();
    delete fileCtx;
    return LV_FS_RES_OK;
}

static lv_fs_res_t nodesMapFsReadCb(lv_fs_drv_t *drv, void *file_p,
                                    void *buf, uint32_t btr, uint32_t *br) {
    LV_UNUSED(drv);
    if (!file_p || !buf) return LV_FS_RES_INV_PARAM;
    NodesMapFsFile *fileCtx = (NodesMapFsFile *)file_p;
    size_t n = fileCtx->file.read((uint8_t *)buf, (size_t)btr);
    if (br) *br = (uint32_t)n;
    return LV_FS_RES_OK;
}

static lv_fs_res_t nodesMapFsSeekCb(lv_fs_drv_t *drv, void *file_p,
                                    uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    NodesMapFsFile *fileCtx = (NodesMapFsFile *)file_p;
    File &file = fileCtx->file;

    // Hand the whence straight to Arduino rather than resolving it here. The
    // arithmetic version this replaced turned SEEK_END into an absolute
    // seek(size) — a seek to exactly EOF, which the SD backend can refuse. That
    // is precisely where lodepng_filesize() lands (lv_fs_seek(f, 0, SEEK_END)
    // followed by lv_fs_tell), and its failure makes lodepng_load_file() give up
    // with error 78. The image then draws nothing at all, with no diagnostic:
    // header reads keep working because lv_image_decoder_get_info() only ever
    // calls lv_fs_read(), never a seek.
    bool ok;
    switch (whence) {
        case LV_FS_SEEK_SET: ok = file.seek((uint32_t)pos, SeekSet); break;
        case LV_FS_SEEK_CUR: ok = file.seek((uint32_t)pos, SeekCur); break;
        case LV_FS_SEEK_END: ok = file.seek((uint32_t)pos, SeekEnd); break;
        default: return LV_FS_RES_INV_PARAM;
    }
    if (!ok) {
        // Landing on EOF is legitimate and is what the size probe above wants;
        // some backends report the seek itself as a failure even so.
        if (file.position() == file.size()) return LV_FS_RES_OK;
        return LV_FS_RES_FS_ERR;
    }
    return LV_FS_RES_OK;
}

static lv_fs_res_t nodesMapFsTellCb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p) {
    LV_UNUSED(drv);
    if (!file_p || !pos_p) return LV_FS_RES_INV_PARAM;
    NodesMapFsFile *fileCtx = (NodesMapFsFile *)file_p;
    *pos_p = (uint32_t)fileCtx->file.position();
    return LV_FS_RES_OK;
}

static void nodesMapInitFsDriver() {
    if (!s_nodesMapFsMutex) {
        s_nodesMapFsMutex = xSemaphoreCreateRecursiveMutex();
        if (!s_nodesMapFsMutex) {
            Serial.println("[map] failed to create filesystem mutex");
            return;
        }
    }
    if (s_nodesMapFsDrvReady) return;
    if (lv_fs_get_drv('S')) {
        s_nodesMapFsDrvReady = true;
        return;
    }

    lv_fs_drv_init(&s_nodesMapFsDrv);
    s_nodesMapFsDrv.letter = 'S';
    s_nodesMapFsDrv.ready_cb = nodesMapFsReadyCb;
    s_nodesMapFsDrv.open_cb = nodesMapFsOpenCb;
    s_nodesMapFsDrv.close_cb = nodesMapFsCloseCb;
    s_nodesMapFsDrv.read_cb = nodesMapFsReadCb;
    s_nodesMapFsDrv.seek_cb = nodesMapFsSeekCb;
    s_nodesMapFsDrv.tell_cb = nodesMapFsTellCb;
    lv_fs_drv_register(&s_nodesMapFsDrv);
    s_nodesMapFsDrvReady = true;
}

static bool nodesMapEnsureDir(const char *path) {
#if HAS_FILE_STORAGE
    if (storageFs().exists(path)) return true;
    return storageFs().mkdir(path);
#else
    LV_UNUSED(path);
    return false;
#endif
}

static bool nodesStateMapMarkerMatchesVersion() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!storageFs().exists(kStateMapMarkerPath)) return false;

    File marker = storageFs().open(kStateMapMarkerPath, FILE_READ);
    if (!marker) return false;
    String line = marker.readStringUntil('\n');
    marker.close();
    line.trim();
    return line.equals(kStateMapCacheVersion);
#endif
}

static void nodesResetStateMapCacheIfStale() {
#if HAS_FILE_STORAGE
    // Only ever correct for a cache this device downloaded itself. Uploaded maps
    // carry no version, are not replaceable from here, and must never be swept.
    if (!kStateMapsCanDownload) return;
    if (!sdBegin()) return;
    bool hasCurrentMarker = storageFs().exists(kStateMapMarkerPath);
    bool hasLegacyMarker = storageFs().exists(kStateMapLegacyMarkerPath);
    if (!hasCurrentMarker && !hasLegacyMarker) return;
    if (nodesStateMapMarkerMatchesVersion()) return;

    Serial.println("[map] state-map cache version changed; clearing cached maps");
    sdRmDirRecursive("/camillia/state_maps");
    nodesMapEnsureDir("/camillia");
    nodesMapEnsureDir("/camillia/state_maps");
    if (storageFs().exists(kStateMapLegacyMarkerPath)) storageFs().remove(kStateMapLegacyMarkerPath);
#endif
}


static bool nodesStateMapCacheComplete() {
#if !HAS_FILE_STORAGE
    return false;
#else
    if (!sdBegin()) return false;
    if (!nodesStateMapMarkerMatchesVersion()) return false;
    for (int i = 0; i < kUsStateMapCount; i++) {
        String p = nodesStateMapPath(kUsStateMaps[i].code);
        if (!storageFs().exists(p.c_str())) return false;
        if (!nodesFileLooksLikePng(p.c_str())) return false;
    }
    return true;
#endif
}

static void nodesWriteDetailMapVersionMarker() {
#if HAS_FILE_STORAGE
    if (storageFs().exists(kDetailMapMarkerPath)) storageFs().remove(kDetailMapMarkerPath);
    File marker = storageFs().open(kDetailMapMarkerPath, FILE_WRITE);
    if (!marker) return;
    marker.print(kDetailMapCacheVersion);
    marker.close();
#endif
}

static bool liveMapTileCoordsForLatLon(float lat, float lon, int zoom,
                                       int32_t &tileX, int32_t &tileY,
                                       float &latMin, float &latMax,
                                       float &lonMin, float &lonMax) {
    if (!(lat >= -90.0f && lat <= 90.0f)) return false;
    if (zoom < 1 || zoom > 19) return false;

    double latClamped = (double)lat;
    if (latClamped > 85.0511) latClamped = 85.0511;
    if (latClamped < -85.0511) latClamped = -85.0511;

    double lonNorm = fmod((double)lon + 180.0, 360.0);
    if (lonNorm < 0.0) lonNorm += 360.0;
    lonNorm -= 180.0;

    const int32_t n = (int32_t)(1UL << zoom);
    const double x = ((lonNorm + 180.0) / 360.0) * (double)n;
    const double latRad = latClamped * M_PI / 180.0;
    const double y = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / M_PI) * 0.5 * (double)n;

    tileX = (int32_t)floor(x);
    tileY = (int32_t)floor(y);
    if (tileX < 0) tileX = 0;
    if (tileX >= n) tileX = n - 1;
    if (tileY < 0) tileY = 0;
    if (tileY >= n) tileY = n - 1;

    auto tileYToLat = [&](int32_t ty) -> float {
        const double z = M_PI * (1.0 - 2.0 * ((double)ty / (double)n));
        return (float)(atan(sinh(z)) * 180.0 / M_PI);
    };

    lonMin = (float)(((double)tileX / (double)n) * 360.0 - 180.0);
    lonMax = (float)(((double)(tileX + 1) / (double)n) * 360.0 - 180.0);
    latMax = tileYToLat(tileY);
    latMin = tileYToLat(tileY + 1);
    return (latMax > latMin) && (lonMax > lonMin);
}

static bool nodesDownloadDetailMapLive(float lat, float lon, const char *detailKey,
                                       int tileZoom, int lodTag,
                                       bool replaceExisting) {
#if !HAS_FILE_STORAGE
    LV_UNUSED(lat);
    LV_UNUSED(lon);
    LV_UNUSED(detailKey);
    LV_UNUSED(tileZoom);
    LV_UNUSED(lodTag);
    LV_UNUSED(replaceExisting);
    return false;
#else
    if (!detailKey || !detailKey[0]) return false;
    if (!nodesPanelCanDownloadTiles()) return false;

    if (tileZoom < 2) tileZoom = 2;
    if (tileZoom > 19) tileZoom = 19;
    if (lodTag < 0) lodTag = 0;

    int32_t tx = 0, ty = 0;
    float latMin = 0.0f, latMax = 0.0f, lonMin = 0.0f, lonMax = 0.0f;
    if (!liveMapTileCoordsForLatLon(lat, lon, tileZoom,
                                    tx, ty, latMin, latMax, lonMin, lonMax)) {
        return false;
    }

    char base[96];
    strncpy(base, kLiveDetailMapTileProxyBase, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';
    size_t blen = strlen(base);
    while (blen > 0 && base[blen - 1] == '/') base[--blen] = '\0';

    char url[192];
    snprintf(url, sizeof(url), "%s/%d/%ld/%ld.png",
             base, tileZoom, (long)tx, (long)ty);

    const String mapPath = nodesDetailMapPath(detailKey);
    const String metaPath = nodesDetailMapMetaPath(detailKey);
    const String tmpPath = mapPath + ".tmp";
    bool hadValidMap = false;

    {
        NodesMapFsGuard fsGuard;
        if (!fsGuard.locked()) return false;
        if (!sdBegin()) return false;
        if (!nodesMapEnsureDir("/camillia") || !nodesMapEnsureDir("/camillia/detail_maps")) {
            return false;
        }

        // Live fetch is usually a cache-fill path for missing cells. At higher
        // Locate zoom levels we may deliberately refresh a lower-detail tile.
        if (storageFs().exists(mapPath.c_str())) {
            if (nodesFileLooksLikePng(mapPath.c_str())) {
                if (!replaceExisting) return true;
                hadValidMap = true;
            } else {
                storageFs().remove(mapPath.c_str());
                if (storageFs().exists(metaPath.c_str())) storageFs().remove(metaPath.c_str());
            }
        }
        if (storageFs().exists(tmpPath.c_str())) storageFs().remove(tmpPath.c_str());
    }

    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(12000);
    if (!http.begin(client, url)) {
        client.stop();
        return false;
    }
    http.addHeader("User-Agent", "camillia-mt/1.0 (node-locate live map)");

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        client.stop();
        return false;
    }

    {
        NodesMapFsGuard fsGuard;
        if (!fsGuard.locked()) {
            http.end();
            client.stop();
            return false;
        }
        File out = storageFs().open(tmpPath.c_str(), FILE_WRITE);
        if (!out) {
            http.end();
            client.stop();
            return false;
        }
        out.close();
    }

    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();
    size_t written = 0;
    bool ok = true;
    uint8_t *buf = s_detailMapOnDemandIoBuf;
    uint32_t idleDeadline = millis() + 12000UL;

    while (http.connected() && (remaining > 0 || remaining < 0)) {
        int avail = stream->available();
        if (avail <= 0) {
            if ((int32_t)(millis() - idleDeadline) >= 0) break;
            delay(1);
            continue;
        }

        size_t want = sizeof(s_detailMapOnDemandIoBuf);
        if (remaining > 0 && (size_t)remaining < want) want = (size_t)remaining;
        if ((size_t)avail < want) want = (size_t)avail;
        if (want == 0) want = 1;

        int n = stream->readBytes(buf, want);
        if (n <= 0) {
            if ((int32_t)(millis() - idleDeadline) >= 0) break;
            delay(1);
            continue;
        }

        size_t wn = 0;
        {
            NodesMapFsGuard fsGuard;
            if (!fsGuard.locked()) {
                ok = false;
                break;
            }
            File out = storageFs().open(tmpPath.c_str(), FILE_APPEND);
            if (!out) {
                ok = false;
                break;
            }
            wn = out.write(buf, (size_t)n);
            out.close();
        }
        if (wn != (size_t)n) {
            ok = false;
            break;
        }
        written += wn;
        if (remaining > 0) remaining -= n;
        idleDeadline = millis() + 12000UL;
        delay(1);
        if (remaining == 0) break;
    }

    http.end();
    client.stop();

    {
        NodesMapFsGuard fsGuard;
        if (!fsGuard.locked()) return false;
        if (!ok || written < 64 || remaining > 0 || !nodesFileLooksLikePng(tmpPath.c_str())) {
            if (storageFs().exists(tmpPath.c_str())) storageFs().remove(tmpPath.c_str());
            return false;
        }

        if (hadValidMap) {
            if (storageFs().exists(mapPath.c_str())) storageFs().remove(mapPath.c_str());
            if (storageFs().exists(metaPath.c_str())) storageFs().remove(metaPath.c_str());
        }

        if (!storageFs().rename(tmpPath.c_str(), mapPath.c_str())) {
            if (storageFs().exists(tmpPath.c_str())) storageFs().remove(tmpPath.c_str());
            return false;
        }

        if (storageFs().exists(metaPath.c_str())) storageFs().remove(metaPath.c_str());
        File meta = storageFs().open(metaPath.c_str(), FILE_WRITE);
        if (!meta) {
            storageFs().remove(mapPath.c_str());
            return false;
        }
        meta.printf("%.6f,%.6f,%.6f,%.6f,%d,%d\n",
                    (double)latMin, (double)latMax,
                    (double)lonMin, (double)lonMax,
                    lodTag,
                    tileZoom);
        meta.close();
        nodesWriteDetailMapVersionMarker();
    }

    Serial.printf("[map] live detail cached key=%s lod=%d z=%d x=%ld y=%ld%s\n",
                  detailKey,
                  lodTag,
                  tileZoom,
                  (long)tx,
                  (long)ty,
                  replaceExisting ? " (refresh)" : "");
    return true;
#endif
}

static void nodesDetailMapOnDemandTaskMain(void *arg) {
    LV_UNUSED(arg);
    char key[11] = {0};
    memcpy(key, s_detailMapOnDemandReqKey, sizeof(key));
    key[sizeof(key) - 1] = '\0';
    float lat = s_detailMapOnDemandLat;
    float lon = s_detailMapOnDemandLon;
    int reqLod = s_detailMapOnDemandReqLod;
    int reqZoom = s_detailMapOnDemandReqZoom;
    bool reqReplace = s_detailMapOnDemandReqReplace;

    bool ok = nodesDownloadDetailMapLive(lat, lon, key, reqZoom, reqLod, reqReplace);
    memcpy(s_detailMapOnDemandDoneKey, key, sizeof(s_detailMapOnDemandDoneKey));
    s_detailMapOnDemandDoneKey[sizeof(s_detailMapOnDemandDoneKey) - 1] = '\0';
    s_detailMapOnDemandOk = ok;
    s_detailMapOnDemandDone = true;
    s_detailMapOnDemandBusy = false;
    s_detailMapOnDemandTask = nullptr;
    vTaskDelete(nullptr);
}

static void nodesQueueDetailMapOnDemand(float lat, float lon, const char *detailKey,
                                        int detailLodTarget) {
#if !HAS_FILE_STORAGE
    LV_UNUSED(lat);
    LV_UNUSED(lon);
    LV_UNUSED(detailKey);
    LV_UNUSED(detailLodTarget);
    return;
#else
    if (!kLiveDetailMapsCanDownload) return;
    if (!detailKey || !detailKey[0]) return;
    if (s_detailMapOnDemandBusy || s_detailMapOnDemandDone) return;
    if (!nodesPanelCanDownloadTiles()) return;

    NodesMapFsGuard fsGuard;
    if (!fsGuard.locked() || !sdBegin()) return;

    if (detailLodTarget < 0) detailLodTarget = 0;
    const int targetZoom = nodesLiveDetailZoomForLod(detailLodTarget);
    bool replaceExisting = false;

    const String mapPath = nodesDetailMapPath(detailKey);
    const String metaPath = nodesDetailMapMetaPath(detailKey);
    if (storageFs().exists(mapPath.c_str())) {
        if (!nodesFileLooksLikePng(mapPath.c_str())) {
            storageFs().remove(mapPath.c_str());
            if (storageFs().exists(metaPath.c_str())) storageFs().remove(metaPath.c_str());
        } else {
            float metaLatMin = 0.0f, metaLatMax = 0.0f, metaLonMin = 0.0f, metaLonMax = 0.0f;
            int cachedLod = -1;
            bool haveMeta = nodesReadDetailMapMeta(detailKey,
                                                   metaLatMin, metaLatMax,
                                                   metaLonMin, metaLonMax,
                                                   &cachedLod);
            if (!haveMeta) {
                int lat10 = 0, lon10 = 0;
                if (nodesDetailMapParseKey(detailKey, lat10, lon10)) {
                    nodesDetailMapBoundsFromIndex(lat10, lon10,
                                                  metaLatMin, metaLatMax,
                                                  metaLonMin, metaLonMax);
                    haveMeta = true;
                }
            }
            if (cachedLod < 0) cachedLod = kLiveDetailMapLodStreet;

            bool coversPoint = haveMeta
                               && lat >= metaLatMin && lat <= metaLatMax
                               && lon >= metaLonMin && lon <= metaLonMax;
            if (coversPoint && cachedLod >= detailLodTarget) {
                return;
            }
            replaceExisting = true;
        }
    }

    s_detailMapOnDemandLat = lat;
    s_detailMapOnDemandLon = lon;
    strncpy(s_detailMapOnDemandReqKey, detailKey, sizeof(s_detailMapOnDemandReqKey) - 1);
    s_detailMapOnDemandReqKey[sizeof(s_detailMapOnDemandReqKey) - 1] = '\0';
    s_detailMapOnDemandReqLod = detailLodTarget;
    s_detailMapOnDemandReqZoom = targetZoom;
    s_detailMapOnDemandReqReplace = replaceExisting;
    s_detailMapOnDemandOk = false;
    s_detailMapOnDemandDone = false;
    s_detailMapOnDemandBusy = true;

    BaseType_t rc = xTaskCreate(nodesDetailMapOnDemandTaskMain,
                                "map_detail_dl",
                                12288,
                                nullptr,
                                1,
                                &s_detailMapOnDemandTask);
    if (rc != pdPASS) {
        s_detailMapOnDemandBusy = false;
        s_detailMapOnDemandDone = true;
        s_detailMapOnDemandOk = false;
        s_detailMapOnDemandTask = nullptr;
        Serial.println("[map] failed to create detail-map download task");
    }
#endif
}

static void serviceDetailMapOnDemand(uint32_t nowMs) {
    LV_UNUSED(nowMs);
#if !HAS_FILE_STORAGE || !HAS_NODE_LOCATE
    return;
#else
    if (!s_detailMapOnDemandDone) return;
    bool ok = s_detailMapOnDemandOk;
    s_detailMapOnDemandDone = false;

    if (ok && s_detailMapOnDemandDoneKey[0]) {
        String detailPath = nodesDetailMapPath(s_detailMapOnDemandDoneKey);
        char imageSrc[96];
        snprintf(imageSrc, sizeof(imageSrc), "S:%s", detailPath.c_str());
        lv_image_cache_drop(imageSrc);
    }

#if HAS_NODE_LOCATE
    nodeLocateOnDetailMapUpdated();
#else
    LV_UNUSED(ok);
#endif

    if (!ok) {
        Serial.println("[map] live detail fetch failed");
    }
#endif
}

static bool nodesDownloadStateMap(const UsStateMapSpec &s,
                                  bool staticHostResolvable,
                                  bool tileHostResolvable) {
    // Permanently false, and every caller is gated on kStateMapsCanDownload so
    // none of them reach here. Its HTTPS tile / staticmap downloads were the
    // last TLS consumer in the firmware and were removed so WiFiClientSecure
    // could be dropped entirely; the staticmap host they used no longer resolves
    // either. Maps now arrive by upload from web config, which has a browser to
    // fetch tiles with (POST /state-map-upload).
    LV_UNUSED(s);
    LV_UNUSED(staticHostResolvable);
    LV_UNUSED(tileHostResolvable);
    return false;
}

static void bootstrapStateMapsIfMissing() {
    if (s_stateMapCacheReady) return;
    if (s_stateMapBootstrapDone) return;

    if (!kStateMapsEnabled || !kStateMapsCanDownload) {
        // Nothing to bootstrap: maps are uploaded, not fetched. Retire the pass
        // immediately rather than falling through to the WiFi session and the
        // fifty stubbed downloads below, which is what this used to do on every
        // boot whenever the cache was not exactly complete. Marking it done also
        // stops the per-loop call re-entering here forever.
        s_stateMapBootstrapTried = true;
        s_stateMapBootstrapDone = true;
        return;
    }

#if !HAS_FILE_STORAGE
    return;
#else
    if (!sdBegin()) return;
    nodesResetStateMapCacheIfStale();
    if (nodesStateMapCacheComplete()) {
        s_stateMapCacheReady = true;
        s_stateMapBootstrapDone = true;
        return;
    }

    if (!wifiHasActiveCreds()) {
        if (!s_stateMapBootstrapTried) {
            s_stateMapBootstrapTried = true;
            Serial.println("[map] state-map bootstrap skipped (no Wi-Fi credentials)");
        }
        s_stateMapBootstrapDone = true;
        return;
    }

    if (!s_stateMapBootstrapInProgress) {
        s_stateMapBootstrapTried = true;
        s_stateMapBootstrapInProgress = true;
        s_stateMapBootstrapHostChecked = false;
        s_stateMapBootstrapStaticHostResolvable = false;
        s_stateMapBootstrapTileHostResolvable = false;
        s_stateMapBootstrapDownloaded = 0;
        s_stateMapBootstrapFailed = 0;
        s_stateMapBootstrapNextIndex = 0;
        s_stateMapBootstrapLastStepMs = 0;
        s_stateMapBootstrapPrevMode = WiFi.getMode();
        s_stateMapBootstrapPrevConnected = (WiFi.status() == WL_CONNECTED);
        s_stateMapBootstrapWifiTouched = false;

        if (!s_stateMapBootstrapPrevConnected) {
            if (s_stateMapBootstrapPrevMode == WIFI_AP) {
#ifdef WIFI_AP_STA
                WiFi.mode(WIFI_AP_STA);
#else
                WiFi.mode(WIFI_STA);
#endif
            } else {
                WiFi.mode(WIFI_STA);
            }
            wifiBeginActiveKnown();
            s_stateMapBootstrapWifiTouched = true;
            s_stateMapBootstrapConnectStartMs = millis();
        }

        Serial.println("[map] state-map bootstrap started (background)");
        return;
    }

    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP) {
        if (!s_stateMapBootstrapPrevConnected
            && (uint32_t)(millis() - s_stateMapBootstrapConnectStartMs) >= 15000UL) {
            Serial.println("[map] state-map bootstrap deferred (Wi-Fi connect timeout)");
            s_stateMapBootstrapInProgress = false;
            s_stateMapBootstrapDone = true;
            stateMapBootstrapRestoreWifi();
        }
        return;
    }

    if (!nodesMapEnsureDir("/camillia") || !nodesMapEnsureDir("/camillia/state_maps")) {
        Serial.println("[map] state-map cache directory creation failed");
        s_stateMapBootstrapInProgress = false;
        s_stateMapBootstrapDone = true;
        stateMapBootstrapRestoreWifi();
        return;
    }

    if (!s_stateMapBootstrapHostChecked) {
        IPAddress resolvedIp;
        s_stateMapBootstrapStaticHostResolvable =
            (WiFi.hostByName("staticmap.openstreetmap.de", resolvedIp) == 1);
        s_stateMapBootstrapTileHostResolvable =
            (WiFi.hostByName("tile.openstreetmap.org", resolvedIp) == 1);
        s_stateMapBootstrapHostChecked = true;

        if (!s_stateMapBootstrapStaticHostResolvable) {
            Serial.println("[map] staticmap DNS failed; using tile fallback");
        }
        if (!s_stateMapBootstrapTileHostResolvable && !s_stateMapBootstrapStaticHostResolvable) {
            Serial.println("[map] DNS failed for both static and tile hosts; bootstrap deferred");
            s_stateMapBootstrapInProgress = false;
            s_stateMapBootstrapDone = true;
            stateMapBootstrapRestoreWifi();
            return;
        }
    }

    if ((uint32_t)(millis() - s_stateMapBootstrapLastStepMs) < 700UL) return;

    while (s_stateMapBootstrapNextIndex < kUsStateMapCount) {
        String p = nodesStateMapPath(kUsStateMaps[s_stateMapBootstrapNextIndex].code);
        if (!storageFs().exists(p.c_str())) break;
        if (!nodesFileLooksLikePng(p.c_str())) {
            storageFs().remove(p.c_str());
            String meta = nodesStateMapMetaPath(kUsStateMaps[s_stateMapBootstrapNextIndex].code);
            if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
            break;
        }
        s_stateMapBootstrapNextIndex++;
    }

    if (s_stateMapBootstrapNextIndex >= kUsStateMapCount) {
        Serial.printf("[map] state bootstrap downloaded=%d failed=%d\n",
                      s_stateMapBootstrapDownloaded,
                      s_stateMapBootstrapFailed);

        if (s_stateMapBootstrapFailed == 0) {
            if (storageFs().exists(kStateMapMarkerPath)) storageFs().remove(kStateMapMarkerPath);
            File marker = storageFs().open(kStateMapMarkerPath, FILE_WRITE);
            if (marker) {
                marker.print(kStateMapCacheVersion);
                marker.close();
            } else {
                Serial.println("[map] state-map marker write failed");
            }
        }

        s_stateMapCacheReady = nodesStateMapCacheComplete();
        s_stateMapBootstrapInProgress = false;
        s_stateMapBootstrapDone = true;

        stateMapBootstrapRestoreWifi();
        return;
    }

    int i = s_stateMapBootstrapNextIndex;
    if (nodesDownloadStateMap(kUsStateMaps[i],
                              s_stateMapBootstrapStaticHostResolvable,
                              s_stateMapBootstrapTileHostResolvable)) {
        s_stateMapBootstrapDownloaded++;
    } else {
        s_stateMapBootstrapFailed++;
    }
    s_stateMapBootstrapNextIndex++;
    s_stateMapBootstrapLastStepMs = millis();
#endif
}

static int nodesMapRenderTiles(float lat, float lon, int x0, int y0, int w, int h,
                               int &markerX, int &markerY,
                               int detailLodTarget) {
    // markerX/markerY are the exact point the coordinate lands on, not the
    // top-left of any particular marker: a centred dot and a tip-anchored pin
    // need different offsets, and only the caller knows which it is drawing.
    markerX = x0 + (w / 2);
    markerY = y0 + (h / 2);
    if (!s_nodesMapTileLayer || !s_nodesMapImage) return 0;

    s_nodesMapLastBoundsValid = false;
    s_nodesMapLastLatMin = 0.0f;
    s_nodesMapLastLatMax = 0.0f;
    s_nodesMapLastLonMin = 0.0f;
    s_nodesMapLastLonMax = 0.0f;

    if (!kStateMapsEnabled) {
        lv_obj_add_flag(s_nodesMapImage, LV_OBJ_FLAG_HIDDEN);
        return 0;
    }

    lv_obj_set_pos(s_nodesMapTileLayer, x0, y0);
    lv_obj_set_size(s_nodesMapTileLayer, w, h);
    lv_obj_add_flag(s_nodesMapImage, LV_OBJ_FLAG_HIDDEN);

#if !HAS_FILE_STORAGE
    LV_UNUSED(lat);
    LV_UNUSED(lon);
    return 0;
#else
    const UsStateMapSpec *state = nodesStateForCoords(lat, lon);
    NodesMapFsGuard fsGuard;
    if (!fsGuard.locked() || !sdBegin()) return 0;

    s_nodesMapLastRenderUsedDetail = false;
    s_nodesMapLastDetailKey[0] = '\0';
    s_nodesMapLastDetailLod = -1;

    if (detailLodTarget < 0) detailLodTarget = 0;

    char detailKey[11] = {0};
    float latMin = 0.0f, latMax = 0.0f, lonMin = 0.0f, lonMax = 0.0f;
    String diskPath;
    bool usingDetail = false;
    int cachedDetailLod = -1;

    if (nodesDetailMapKeyForCoords(lat, lon, detailKey, sizeof(detailKey),
                                   &latMin, &latMax, &lonMin, &lonMax)) {
        String detailPath = nodesDetailMapPath(detailKey);
        if (storageFs().exists(detailPath.c_str())) {
            bool useDetail = true;
            float metaLatMin = 0.0f, metaLatMax = 0.0f, metaLonMin = 0.0f, metaLonMax = 0.0f;
            if (nodesReadDetailMapMeta(detailKey,
                                       metaLatMin, metaLatMax,
                                       metaLonMin, metaLonMax,
                                       &cachedDetailLod)) {
                if (lat < metaLatMin || lat > metaLatMax || lon < metaLonMin || lon > metaLonMax) {
                    useDetail = false;
                }
            }
            if (cachedDetailLod < 0) cachedDetailLod = kLiveDetailMapLodStreet;
            if (useDetail) {
                diskPath = detailPath;
                usingDetail = true;
                memcpy(s_nodesMapLastDetailKey, detailKey, sizeof(s_nodesMapLastDetailKey));
                s_nodesMapLastRenderUsedDetail = true;
                s_nodesMapLastDetailLod = cachedDetailLod;
            }
        }

    }

    if (!usingDetail) {
        if (!state) return 0;
        diskPath = nodesStateMapPath(state->code);
    }

    if (!usingDetail && !storageFs().exists(diskPath.c_str())) {
        // No map for this state: say so and return. The on-demand fetch below is
        // dead code now (nodesDownloadStateMap() is a stub) and must not be
        // reached — it opens with two blocking WiFi.hostByName() calls, one of
        // them to a host that no longer resolves, so it would stall the UI for
        // the full DNS timeout every time Locate opened on an uncached state.
        if (!kStateMapsCanDownload) return 0;

        uint32_t now = millis();
        bool sameState = (strncmp(s_stateMapOnDemandLastCode, state->code, 2) == 0);
        bool retryAllowed = !sameState || (uint32_t)(now - s_stateMapOnDemandLastTryMs) >= 5000UL;

        if (retryAllowed && WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            strncpy(s_stateMapOnDemandLastCode, state->code, 2);
            s_stateMapOnDemandLastCode[2] = '\0';
            s_stateMapOnDemandLastTryMs = now;

            if (!s_stateMapBootstrapHostChecked) {
                IPAddress resolvedIp;
                s_stateMapBootstrapStaticHostResolvable =
                    (WiFi.hostByName("staticmap.openstreetmap.de", resolvedIp) == 1);
                s_stateMapBootstrapTileHostResolvable =
                    (WiFi.hostByName("tile.openstreetmap.org", resolvedIp) == 1);
                s_stateMapBootstrapHostChecked = true;

                if (!s_stateMapBootstrapStaticHostResolvable) {
                    Serial.println("[map] staticmap DNS failed; using tile fallback");
                }
                if (!s_stateMapBootstrapTileHostResolvable && !s_stateMapBootstrapStaticHostResolvable) {
                    Serial.println("[map] DNS failed for both static and tile hosts; bootstrap deferred");
                }
            }

            if (s_stateMapBootstrapStaticHostResolvable || s_stateMapBootstrapTileHostResolvable) {
                if (nodesDownloadStateMap(*state,
                                          s_stateMapBootstrapStaticHostResolvable,
                                          s_stateMapBootstrapTileHostResolvable)) {
                    Serial.printf("[map] on-demand state map downloaded: %s\n", state->code);
                }
            }
        }
        return 0;
    }

    if (!nodesFileLooksLikePng(diskPath.c_str())) {
        storageFs().remove(diskPath.c_str());
        String meta = usingDetail ? nodesDetailMapMetaPath(detailKey)
                                  : nodesStateMapMetaPath(state->code);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
        s_stateMapCacheReady = false;
        s_stateMapBootstrapDone = false;
        s_nodesMapLastRenderUsedDetail = false;
        s_nodesMapLastDetailKey[0] = '\0';
        s_nodesMapLastDetailLod = -1;
        Serial.printf("[map] invalid PNG removed: %s\n", diskPath.c_str());
        return 0;
    }

    snprintf(s_nodesMapImageSrc, sizeof(s_nodesMapImageSrc), "S:%s", diskPath.c_str());

    uint16_t srcW = kStateMapImageW;
    uint16_t srcH = kStateMapImageH;
    lv_image_header_t header;
    lv_res_t headerRes = lv_image_decoder_get_info(s_nodesMapImageSrc, &header);
    if (headerRes == LV_RES_OK && header.w > 0 && header.h > 0) {
        srcW = header.w;
        srcH = header.h;
    } else {
        storageFs().remove(diskPath.c_str());
        String meta = usingDetail ? nodesDetailMapMetaPath(detailKey)
                                  : nodesStateMapMetaPath(state->code);
        if (storageFs().exists(meta.c_str())) storageFs().remove(meta.c_str());
        s_stateMapCacheReady = false;
        s_stateMapBootstrapDone = false;
        s_nodesMapLastRenderUsedDetail = false;
        s_nodesMapLastDetailKey[0] = '\0';
        s_nodesMapLastDetailLod = -1;
        Serial.printf("[map] decode failed, removed: %s\n", diskPath.c_str());
        return 0;
    }

    lv_img_set_src(s_nodesMapImage, s_nodesMapImageSrc);

    // Aspect fit, computed here because the pin below has to land on the drawn
    // pixels rather than on the widget box.
    const int32_t scaleX = ((int32_t)w * LV_SCALE_NONE) / (int32_t)srcW;
    const int32_t scaleY = ((int32_t)h * LV_SCALE_NONE) / (int32_t)srcH;
    const int32_t scale = (scaleX < scaleY) ? scaleX : scaleY;
    int drawW = ((int)srcW * (int)scale) / LV_SCALE_NONE;
    int drawH = ((int)srcH * (int)scale) / LV_SCALE_NONE;
    if (drawW < 1) drawW = 1;
    if (drawH < 1) drawH = 1;
    int drawX = (w - drawW) / 2;
    int drawY = (h - drawH) / 2;

    // Size the widget to the fitted bounds, then let LVGL derive the scale from
    // it. STRETCH sets scale_x and scale_y from the widget's own width/height
    // (update_align() in lv_image.c), and because those bounds already carry the
    // source aspect ratio, both come out equal — a uniform scale, centred, with
    // no letterboxing inside the widget.
    //
    // This replaced a hand-computed lv_img_set_zoom()/lv_img_set_pivot() pair
    // carried over from LVGL v8. That code drew nothing at all on v9: the map
    // panel it was written for was never built, so it had never once run until
    // Locate started calling it.
    lv_obj_set_size(s_nodesMapImage, drawW, drawH);
    lv_obj_set_pos(s_nodesMapImage, drawX, drawY);
    lv_image_set_inner_align(s_nodesMapImage, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_clear_flag(s_nodesMapImage, LV_OBJ_FLAG_HIDDEN);

    // The decode itself happens later, during the draw, and a failure there is
    // silent — the widget simply renders nothing and the map area stays blank.
    // This line is what tells the difference between "never got here" and "got
    // here and LVGL still drew nothing", which is exactly the distinction that
    // was missing the first time this ran.
    // Mirrors lodepng_filesize() step for step, so the log says outright whether
    // the decoder's own size probe can succeed on this card.
    {
        lv_fs_file_t probe;
        uint32_t probeSize = 0;
        lv_fs_res_t rOpen = lv_fs_open(&probe, s_nodesMapImageSrc, LV_FS_MODE_RD);
        lv_fs_res_t rSeek = LV_FS_RES_UNKNOWN;
        lv_fs_res_t rTell = LV_FS_RES_UNKNOWN;
        if (rOpen == LV_FS_RES_OK) {
            rSeek = lv_fs_seek(&probe, 0, LV_FS_SEEK_END);
            rTell = lv_fs_tell(&probe, &probeSize);
            lv_fs_close(&probe);
        }
        Serial.printf("[map] fs probe: open=%d seekEnd=%d tell=%d size=%lu (0=OK)\n",
                      (int)rOpen, (int)rSeek, (int)rTell, (unsigned long)probeSize);
    }

    const char *mapTag = usingDetail ? detailKey : state->code;
    Serial.printf("[map] %s draw: src %ux%u box %dx%d scale %ld -> %dx%d at (%d,%d)\n",
                  mapTag,
                  (unsigned)srcW, (unsigned)srcH, w, h,
                  (long)scale, drawW, drawH, drawX, drawY);

    if (usingDetail) {
        nodesReadDetailMapMeta(detailKey, latMin, latMax, lonMin, lonMax);
    } else {
        latMin = state->latMin;
        latMax = state->latMax;
        lonMin = state->lonMin;
        lonMax = state->lonMax;
        nodesReadStateMapMeta(state->code, latMin, latMax, lonMin, lonMax);
    }
    if (!(latMax > latMin) || !(lonMax > lonMin)) {
        if (usingDetail) {
            int lat10 = 0, lon10 = 0;
            if (nodesDetailMapParseKey(detailKey, lat10, lon10)) {
                nodesDetailMapBoundsFromIndex(lat10, lon10, latMin, latMax, lonMin, lonMax);
            }
        }
        if (!(latMax > latMin) || !(lonMax > lonMin)) return 0;
    }

    s_nodesMapLastBoundsValid = true;
    s_nodesMapLastLatMin = latMin;
    s_nodesMapLastLatMax = latMax;
    s_nodesMapLastLonMin = lonMin;
    s_nodesMapLastLonMax = lonMax;

    float xNorm = (lon - lonMin) / (lonMax - lonMin);
    float yNorm = (latMax - lat) / (latMax - latMin);
    if (xNorm < 0.0f) xNorm = 0.0f;
    if (xNorm > 1.0f) xNorm = 1.0f;
    if (yNorm < 0.0f) yNorm = 0.0f;
    if (yNorm > 1.0f) yNorm = 1.0f;

    markerX = x0 + drawX + (int)(xNorm * (float)drawW);
    markerY = y0 + drawY + (int)(yNorm * (float)drawH);
    return 1;
#endif
}

