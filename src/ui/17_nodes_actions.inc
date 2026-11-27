// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: Nodes list rows, snapshots, traceroute progress, node action menu.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void sdRmDirRecursive(const char *path) {
#if HAS_FILE_STORAGE
    if (!path || !path[0]) return;
    File dir = storageFs().open(path);
    if (!dir) {
        storageFs().remove(path);
        return;
    }
    if (!dir.isDirectory()) {
        dir.close();
        storageFs().remove(path);
        return;
    }

    while (true) {
        File child = dir.openNextFile();
        if (!child) break;

        String childPath = child.name();
        bool childIsDir = child.isDirectory();
        child.close();

        if (!childPath.startsWith("/")) {
            String fullPath = path;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += childPath;
            childPath = fullPath;
        }

        if (childIsDir) {
            sdRmDirRecursive(childPath.c_str());
        } else {
            storageFs().remove(childPath.c_str());
        }
    }

    dir.close();
    storageFs().rmdir(path);
#else
    LV_UNUSED(path);
#endif
}

// The evicted-node archive alone, for a Clear Nodes that keeps favorites but
// was asked to take the archive: unlike clearNodeDbOnSd() it leaves the legacy
// whole-table snapshots, which hold the favorites too. True when it was cleared.
static bool clearNodeArchiveOnSd() {
#if HAS_FILE_STORAGE
    return sdBegin() && nodeArchiveClear();
#else
    return false;
#endif
}

// Returns true when the evicted-node archive was cleared along with the legacy
// snapshots. False means it was not asked for, or there was nothing to clear or
// no card to clear it from, which callers use only to word their status line —
// never as a failure.
static bool clearNodeDbOnSd(bool includeArchive) {
#if HAS_FILE_STORAGE
    if (!sdBegin()) return false;
    // The archive is this firmware's only live node file on the card. Factory
    // Reset always takes it; Clear Nodes only when its "include archived" box
    // is ticked.
    const bool archiveCleared = includeArchive && nodeArchiveClear();

    const char *nodeFiles[] = {
        "/camillia/nodes.db",
        "/camillia/nodes.bin",
        "/camillia/nodes.json",
        "/camillia/node_db.bin",
        "/camillia/node_db.json",
    };
    for (size_t i = 0; i < sizeof(nodeFiles) / sizeof(nodeFiles[0]); i++) {
        if (storageFs().exists(nodeFiles[i])) {
            storageFs().remove(nodeFiles[i]);
        }
    }

    if (storageFs().exists("/camillia/nodes")) {
        sdRmDirRecursive("/camillia/nodes");
    }
    return archiveCleared;
#else
    LV_UNUSED(includeArchive);
    return false;
#endif
}

// The legend under the list. Its own function because the focus model changes it
// without changing a single row — rebuilding every row object to rewrite one
// line of text would be real LVGL churn on a full node table.
static void refreshNodesHint() {
    if (!s_nodesHintLabel) return;
#if UI_TOUCH_ONLY_PROFILE
    // One line for every state, because the keyboard states below cannot be
    // reached without a keyboard: search is the button above the list, and
    // there is no Enter to focus the info panel. Naming the way out matters more here than
    // on the keyboard builds — it is the nav button, not a key, and this screen
    // is closed by tapping the Nodes button that is already lit underneath it.
    lv_label_set_text(s_nodesHintLabel, TR("Tap a node for actions   Nodes below = Back"));
#else
#if defined(DEVICE_TDECK)
    const char *navKeys = "J/K";
#else
    const char *navKeys = TR("Up/Down");
#endif
    if (s_nodesInfoFocused) {
        // The keys mean something else in here, so say so rather than leaving
        // the list's legend up over a panel it no longer drives.
        lv_label_set_text_fmt(s_nodesHintLabel, TR("%s=Scroll  A=Actions  %s=List"),
                              navKeys, modalCloseKeyLabel());
    } else if (s_nodesSearchBtnFocused && s_nodesTopBtn == kNodesTopMap) {
        lv_label_set_text_fmt(s_nodesHintLabel, TR("Enter=Map  %s=Back"),
                              modalCloseKeyLabel());
    } else if (s_nodesSearchBtnFocused && s_nodesTopBtn == kNodesTopAll) {
        lv_label_set_text_fmt(s_nodesHintLabel, TR("Enter=Show all  %s=Back"),
                              modalCloseKeyLabel());
    } else if (s_nodesSearchBtnFocused) {
        lv_label_set_text_fmt(s_nodesHintLabel,
                              s_nodesFilterOpen ? TR("Enter=Clear search  %s=Back")
                                                : TR("Enter=Search  %s=Back"),
                              modalCloseKeyLabel());
#if NODES_INFO_AS_MODAL
    // Enter opens the actions here: Node Info is one of them.
    } else if (s_nodesFilterOpen) {
        lv_label_set_text_fmt(s_nodesHintLabel,
                              TR("Enter=Actions  Space=Clear  %s=Back"),
                              modalCloseKeyLabel());
    } else {
        lv_label_set_text_fmt(s_nodesHintLabel,
                              TR("Enter=Actions  Space=Search  %s=Back"),
                              modalCloseKeyLabel());
    }
#else
    } else if (s_nodesFilterOpen) {
        // Showing search results. Space presses the button, which reads Clear
        // now; Bksp does too, ahead of Back, since on most boards Backspace is
        // also the close key and should unwind the search before the screen.
        lv_label_set_text_fmt(s_nodesHintLabel,
                              TR("Enter=Info  A=Actions  Space=Clear  %s=Back"),
                              modalCloseKeyLabel());
    } else {
        lv_label_set_text_fmt(s_nodesHintLabel,
                              TR("Enter=Info  A=Actions  Space=Search  %s=Back"),
                              modalCloseKeyLabel());
    }
#endif
#endif   // !UI_TOUCH_ONLY_PROFILE
}

static void refreshNodesListRows() {
    if (!lvObjAlive(s_nodesList)) {
        s_nodesList = nullptr;
        s_nodesListRowCount = 0;
        s_nodesRenderStart = 0;
        memset(s_nodesRenderedFilteredIdx, 0, sizeof(s_nodesRenderedFilteredIdx));
        memset(s_nodesListRows, 0, sizeof(s_nodesListRows));
        return;
    }

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK_PRO)
    const lv_font_t *nodesListFont = emojiFont(&lv_font_montserrat_12);
    const int nodesListRowH = 28;
#elif defined(DEVICE_TDISPLAY_P4) && !UI_LARGE_PANEL_PROFILE
    // Portrait stacks the list over the details at full width (see
    // openNodesModal), which leaves room for a bigger face than the side-by-side
    // landscape split. Same size as the detail panel below it.
    const bool nodesBig = uiPortrait();
    const lv_font_t *nodesListFont = emojiFont(nodesBig ? &lv_font_montserrat_14
                                                        : &lv_font_montserrat_10);
    const int nodesListRowH = nodesBig ? 30 : 22;
#elif defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_WIO_TRACKER_L2)
    // Upright the list is stacked full width over the details (openNodesModal),
    // and takes the details' face, a size up from landscape's.
    const bool nodesBig = uiPortrait();
    const lv_font_t *nodesListFont = emojiFont(nodesBig ? &lv_font_montserrat_12
                                                        : &lv_font_montserrat_10);
    const int nodesListRowH = nodesBig ? 26 : 22;
#elif defined(DEVICE_CROWPANEL_50)
    // The 800x480 panel's list text (kMainScreenFont): named 18, drawn as 20.
    const lv_font_t *nodesListFont = emojiFont(&lv_font_montserrat_18);
    const int nodesListRowH = 40;
#else
    const lv_font_t *nodesListFont = emojiFont(&lv_font_montserrat_10);
    const int nodesListRowH = 22;
#endif

    if (s_nodesTitleLabel) {
        // Favorites, or the matches for a search, counted as the list holds them.
        char titleText[80];
        if (nodesFilterIsAll()) {
            const char *more = (s_nodesArchivedCount > 0 && nodeArchiveIndexTruncated()) ? "+" : "";
            snprintf(titleText, sizeof(titleText), TR("All nodes (%d%s)"),
                     s_nodesFilteredCount, more);
        } else if (s_nodesFilterOpen) {
            // Floor marker as before: a truncated archive scan means there may
            // be more matches than the index reached.
            const char *more = (s_nodesArchivedCount > 0 && nodeArchiveIndexTruncated()) ? "+" : "";
            snprintf(titleText, sizeof(titleText), TR("Nodes [%s] (%d%s)"),
                     s_nodesFilter, s_nodesFilteredCount, more);
        } else {
            snprintf(titleText, sizeof(titleText), TR("Favorites (%d)"), s_nodesFilteredCount);
        }
        screenHeaderSetTitle(s_nodesTitleLabel, titleText);
    }

    refreshNodesHint();

    lv_obj_clean(s_nodesList);
    s_nodesListRowCount = 0;
    memset(s_nodesRenderedFilteredIdx, 0, sizeof(s_nodesRenderedFilteredIdx));
    memset(s_nodesListRows, 0, sizeof(s_nodesListRows));

    if (s_nodesFilteredCount <= 0) {
        lv_obj_t *empty = lv_label_create(s_nodesList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, nodesListFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        if (nodesFilterIsAll()) {
            lv_label_set_text(empty, TR("No nodes yet."));
        } else if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
            char noMatch[64];
            snprintf(noMatch, sizeof(noMatch), TR("No matches for: %s"), s_nodesFilter);
            lv_label_set_text(empty, noMatch);
        } else {
            lv_label_set_text(empty, TR("No favorites yet. All or Search finds nodes."));
        }
        return;
    }

    nodesClampRenderWindow();
    int renderStart = s_nodesRenderStart;
    int renderLimit = min(kNodesMaxRenderedRows, s_nodesFilteredCount - renderStart);
    for (int listIdx = 0; listIdx < renderLimit; listIdx++) {
        lv_mem_monitor_t nodesMem;
        lv_mem_monitor(&nodesMem);
        if (nodesMem.free_biggest_size < 4096) {
            logLvglMemDiag("nodes render stopped (low LVGL mem)");
            break;
        }

        int rowIdx = renderStart + listIdx;
        int snapshotIdx = s_nodesFilteredIdx[rowIdx];
        if (snapshotIdx < 0 || snapshotIdx >= nodesRowCountTotal()) continue;
        // Resolve live. If the node was evicted while this modal was open we
        // still emit the row (from its id) so rows stay 1:1 with the filtered
        // list — dropping one here would desync selection and row indices.
        const ArchivedNode *arch = nodesArchivedAt(snapshotIdx);
        const uint32_t nodeId = nodesRowNodeId(snapshotIdx);
        const NodeEntry *n = arch ? nullptr : Nodes.find(nodeId);
        char nodeIdText[12];
        snprintf(nodeIdText, sizeof(nodeIdText), "!%08lX", (unsigned long)nodeId);

        lv_obj_t *row = lv_btn_create(s_nodesList);
        if (!row) {
            Serial.println("[nodes] list row allocation failed");
            break;
        }
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        lv_obj_set_width(row, lv_pct(96));
        lv_obj_set_height(row, nodesListRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 3, 0);
        lv_obj_set_style_pad_right(row, 3, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x2B4D8C), 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, onNodeSnapshotPressed, LV_EVENT_PRESSED, (void *)(intptr_t)rowIdx);
#if UI_TOUCH_ONLY_PROFILE
        lv_obj_add_event_cb(row, onNodeSnapshotClicked, LV_EVENT_CLICKED, (void *)(intptr_t)rowIdx);
#endif

        lv_obj_t *lbl = lv_label_create(row);
        if (!lbl) {
            lv_obj_del(row);
            Serial.println("[nodes] list label allocation failed");
            break;
        }
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, nodesListFont, 0);
#if NODES_LAYOUT_WIDE
        // Left-aligned and ellipsized, not centered and clipped: a column of
        // centered long names is much harder to scan down, and a name cut
        // mid-glyph does not read as "there is more of this".
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        // Long name first, since that is what identifies a node to a person.
        // The fallback chain ends at the id so an evicted node — one that
        // vanished while this modal was open — still occupies its row and keeps
        // the list 1:1 with s_nodesFilteredIdx.
        const char *rowName = nodeIdText;
        if (n) {
            if (n->hasName && n->longName[0])  rowName = n->longName;
            else if (n->shortName[0])          rowName = n->shortName;
            else                               rowName = "----";
        } else if (arch) {
            // The names the archive preserved. Without them an archived row is
            // a bare hex id, which is the thing that made the file useless as a
            // screen in the first place.
            if (arch->longName[0])       rowName = arch->longName;
            else if (arch->shortName[0]) rowName = arch->shortName;
        }
#else
        // Cardputer: short names, centered, in a column too narrow for more.
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        const char *rowName = nodeIdText;
        if (n)          rowName = n->shortName[0] ? n->shortName : "----";
        else if (arch)  rowName = arch->shortName[0] ? arch->shortName : arch->longName;
        if (!rowName || !rowName[0]) rowName = nodeIdText;
#endif
        char rowText[56];
        // '~' for archived, '*' for favorite. Distinct glyphs on purpose: they
        // mark opposite things (a node pinned into the table, and one that has
        // already fallen out of it) and must not be mistaken for each other.
        // '!' leads both: the node's key changed and waits for review.
        // "ham" follows them: the node says it is a licensed station.
        const bool keyConflict = n && Nodes.hasKeyConflict(n->nodeId);
        const bool hamNode = n && n->licensedKnown && n->isLicensed;
        snprintf(rowText, sizeof(rowText), "%s%s%s%s",
                 keyConflict ? "! " : "",
                 arch ? "~ " : ((n && n->favorite) ? "* " : ""),
                 hamNode ? "ham " : "", rowName);
        setLabelTextEmojiSafe(lbl, rowText);
#if NODES_LAYOUT_WIDE
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
#else
        lv_obj_center(lbl);
#endif

        if (s_nodesListRowCount < MAX_NODES) {
            s_nodesListRows[s_nodesListRowCount] = row;
            s_nodesRenderedFilteredIdx[s_nodesListRowCount] = rowIdx;
            s_nodesListRowCount++;
        }
    }
}

static void refreshNodesListSelection() {
    for (int i = 0; i < s_nodesListRowCount; i++) {
        lv_obj_t *row = s_nodesListRows[i];
        if (!row) continue;
        bool selected = !s_nodesSearchBtnFocused
                        && (s_nodesRenderedFilteredIdx[i] == s_nodesSelected);
        // Archived rows are dimmed: they are a record of nodes that have left
        // the table, and should not read as part of the live mesh at a glance.
        // Resolved back through the filtered index the row was built from.
        bool archivedRow = false;
        {
            const int filteredIdx = s_nodesRenderedFilteredIdx[i];
            if (filteredIdx >= 0 && filteredIdx < s_nodesFilteredCount) {
                archivedRow = (nodesArchivedAt(s_nodesFilteredIdx[filteredIdx]) != nullptr);
            }
        }
#if defined(DEVICE_TDECK_PRO)
        const lv_color_t rowTextColor = lv_color_make(0, 0, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_outline_width(row, 0, 0);
        lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
#else
        lv_color_t rowTextColor = lv_color_hex(0xE8F1FF);
        if (s_cfg.uiMode != UI_MODE_LIGHT) {
            // Dark themes: keep node names brighter for better list readability.
            rowTextColor = selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xF2F8FF);
        }
        if (archivedRow) {
            // Still legible when highlighted — the cursor has to be followable
            // across the boundary between the two halves.
            rowTextColor = selected ? lv_color_hex(0xC8D6EC) : lv_color_hex(0x8FA6C8);
        }
        lv_obj_set_style_bg_color(row, selected ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266), 0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selected ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
    #endif
        lv_obj_set_style_text_color(row, rowTextColor, 0);

        lv_obj_t *rowLabel = lv_obj_get_child(row, 0);
        if (rowLabel) {
            lv_obj_set_style_text_color(rowLabel, rowTextColor, 0);
        }
    }
}

// Which panel has the keys, drawn the way DM draws the same idea: the focused
// panel takes the bright 2px border, the other drops to the dim hairline. Touch
// builds never enter info focus, so both keep their resting look there.
// The rule between a screen's two panes (DMs: conversations and messages;
// Nodes: the list and the details), which stand unframed either side of it.
// Stacked, it is horizontal and runs the full width of the display, out through
// the screen's 4 px padding (the parent needs LV_OBJ_FLAG_OVERFLOW_VISIBLE);
// side by side it is vertical and runs the panes' full height. The divider's
// brighter ink, so one rule reads as clearly as the frames it replaces.
static lv_obj_t *buildPaneRule(lv_obj_t *parent, bool stacked) {
    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(rule, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x5C86C6), 0);
#endif
    if (stacked) {
        lv_obj_set_size(rule, lv_disp_get_hor_res(NULL), 1);
        lv_obj_set_style_translate_x(rule, -4, 0);
    } else {
        lv_obj_set_size(rule, 1, lv_pct(100));
    }
    return rule;
}

static void refreshNodesPanelFocusStyles() {
    if (!lvObjValid(s_nodesInfoPanel) || !lvObjValid(s_nodesListPanel)) return;
    const bool infoFocused = s_nodesInfoFocused;

    // The panes have no frame now -- one rule divides them (buildPaneRule())
    // -- so the pane with the keys is the one with the faint fill both used to
    // carry, and the other is left bare.
    lv_obj_set_style_bg_color(s_nodesListPanel, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_color(s_nodesInfoPanel, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_nodesListPanel, infoFocused ? LV_OPA_TRANSP : LV_OPA_40, 0);
    lv_obj_set_style_bg_opa(s_nodesInfoPanel, infoFocused ? LV_OPA_40 : LV_OPA_TRANSP, 0);
}

// Writes one section's two labels. `keys` and `vals` are newline-separated and
// must carry the same number of lines — that correspondence is the whole of the
// alignment, so a caller that adds a field has to add it to both.
static void nodesSetSection(int section, const char *keys, const char *vals) {
    if (section < 0 || section >= NODES_SEC_COUNT) return;
    if (lvObjValid(s_nodesSectionKeys[section])) {
        lv_label_set_text(s_nodesSectionKeys[section], keys);
    }
    if (lvObjValid(s_nodesSectionVals[section])) {
        setLabelTextEmojiSafe(s_nodesSectionVals[section], vals);
    }
}

static void nodesShowSections(bool show) {
    for (int i = 0; i < NODES_SEC_COUNT; i++) {
        if (!lvObjValid(s_nodesSectionBox[i])) continue;
        if (show) lv_obj_clear_flag(s_nodesSectionBox[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_nodesSectionBox[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refreshNodesDetails() {
    if (!lvObjAlive(s_nodesDetail)) {
        s_nodesDetail = nullptr;
        s_nodesListPanel = nullptr;
        s_nodesInfoFocused = false;
        memset(s_nodesSectionBox, 0, sizeof(s_nodesSectionBox));
        memset(s_nodesSectionKeys, 0, sizeof(s_nodesSectionKeys));
        memset(s_nodesSectionVals, 0, sizeof(s_nodesSectionVals));
        return;
    }

    const NodeEntry *selectedNode = currentNodesSelection();
    const ArchivedNode *selectedArchived =
        selectedNode ? nullptr : currentNodesArchivedSelection();
    if (selectedArchived) {
        // Archived nodes get the plain wrapped panel rather than the four
        // aligned sections: most of what those sections show is live state
        // (SNR, hops, telemetry age) that a record on a card cannot have, and
        // rendering them mostly-empty would read as a node with no data rather
        // than a node that is no longer here.
        //
        // The full record is on the card, not in the index — it is fetched
        // below only because the user has selected this row, which is the one
        // moment the extra read is worth it.
        nodesShowSections(false);
        lv_obj_clear_flag(s_nodesDetail, LV_OBJ_FLAG_HIDDEN);

        // Date stays ISO because it is a date, not a clock; only the time half
        // follows the Clock Format setting.
        auto archivedStamp = [](uint32_t epoch, char *out, size_t outLen) {
            if (epoch == 0) return;   // leave the caller's placeholder in place
            const time_t t = (time_t)epoch;
            struct tm tmv;
            if (!localtime_r(&t, &tmv)) return;
            char clock[LIVE_CLOCK_BUF];
            liveFormatClock(tmv, clock, sizeof(clock));
            snprintf(out, outLen, "%04d-%02d-%02d %s",
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, clock);
        };
        char when[48]; snprintf(when, sizeof(when), "%s", TR("unknown date"));
        archivedStamp(selectedArchived->archivedEpoch, when, sizeof(when));
        char heardWhen[48]; snprintf(heardWhen, sizeof(heardWhen), "%s", TR("unknown"));
        archivedStamp(selectedArchived->lastHeardEpoch, heardWhen, sizeof(heardWhen));

        const NodeEntry *full = nodesArchivedDetail(selectedArchived->nodeId);

        char pos[80];
        if (full && full->hasPosition && (full->latI != 0 || full->lonI != 0)) {
            snprintf(pos, sizeof(pos), TR("Lat: %.5f\nLon: %.5f\nAlt: %ld m"),
                     (double)((float)full->latI / 10000000.0f),
                     (double)((float)full->lonI / 10000000.0f),
                     (long)full->alt);
        } else {
            snprintf(pos, sizeof(pos), TR("No position recorded"));
        }

        char body[420];
        snprintf(body, sizeof(body),
                 TR("ARCHIVED %s\n\n"
                 "%s\nShort: %s\nID: !%08lX\n\n"
                 "Last heard: %s\n%s\n\n"
                 "%s"
                 "Not on the live list. Restore it to message,\n"
                 "favorite or trace it."),
                 when,
                 selectedArchived->longName[0] ? selectedArchived->longName : TR("(no name)"),
                 selectedArchived->shortName[0] ? selectedArchived->shortName : "n/a",
                 (unsigned long)selectedArchived->nodeId,
                 heardWhen,
                 pos,
                 (full && full->hasPubKey) ? TR("Public key preserved\n\n") : "");
        setLabelTextEmojiSafe(s_nodesDetail, body);
        return;
    }
    if (!selectedNode) {
        lv_obj_clear_flag(s_nodesDetail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_nodesDetail, TR("No nodes seen yet."));
        nodesShowSections(false);
        return;
    }
#if NODES_LAYOUT_WIDE
    lv_obj_add_flag(s_nodesDetail, LV_OBJ_FLAG_HIDDEN);
    nodesShowSections(true);
#endif

    const NodeEntry &n = *selectedNode;

    char name[48];
    if (n.hasName && n.longName[0]) {
        snprintf(name, sizeof(name), "%s", n.longName);
    } else if (nodesShortNameDisplayable(n.shortName)) {
        snprintf(name, sizeof(name), "%s", n.shortName);
    } else {
        snprintf(name, sizeof(name), "!%08X", n.nodeId);
    }

    char shortName[24];
    if (nodesShortNameDisplayable(n.shortName)) {
        snprintf(shortName, sizeof(shortName), "%s", n.shortName);
    } else {
        snprintf(shortName, sizeof(shortName), "n/a");
    }

    char heard[24];
    if (n.lastHeardMs > 0) {
        uint32_t ageMs = millis() - n.lastHeardMs;
        snprintf(heard, sizeof(heard), TR("%lus ago"), (unsigned long)(ageMs / 1000UL));
    } else {
        snprintf(heard, sizeof(heard), "n/a");
    }

    char vals[256];

#if !NODES_LAYOUT_WIDE
    // Cardputer: the original single wrapped label. Four sections of aligned
    // fields need width for two columns and height for ~17 rows, and this panel
    // has neither — so it keeps the compact blob it has always had.
    {
        char pos[96];
        if (n.hasPosition && (n.latI != 0 || n.lonI != 0)) {
            snprintf(pos, sizeof(pos), TR("Lat: %.5f\nLon: %.5f\nAlt: %ld m"),
                     (double)((float)n.latI / 10000000.0f),
                     (double)((float)n.lonI / 10000000.0f),
                     (long)n.alt);
        } else {
            snprintf(pos, sizeof(pos), TR("No position data"));
        }

        char telem[220];
        if (n.hasTelemetry) {
            telem[0] = '\0';
            if (n.hasDeviceTelemetry) {
                snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem),
                         TR("Battery: %.0f%%\nVoltage: %.2f V\nChUtil: %.1f%%\nAirTx: %.1f%%"),
                         (double)n.battPct, (double)n.voltage,
                         (double)n.chUtil, (double)n.airUtil);
            }
            if (n.hasEnvironmentTelemetry) {
                if (telem[0]) {
                    snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem), "\n");
                }
                if (s_cfg.displayUnits != 0) {
                    snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem),
                             TR("Temp: %.1f F\nHumidity: %.1f%%\nPressure: %.2f inHg"),
                             (double)(n.temperatureC * (9.0f / 5.0f) + 32.0f),
                             (double)n.humidityPct,
                             (double)(n.pressureHpa * 0.0295299831f));
                } else {
                    snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem),
                             TR("Temp: %.1f C\nHumidity: %.1f%%\nPressure: %.1f hPa"),
                             (double)n.temperatureC, (double)n.humidityPct,
                             (double)n.pressureHpa);
                }
            }
            if (!telem[0]) snprintf(telem, sizeof(telem), TR("No telemetry data"));
        } else {
            snprintf(telem, sizeof(telem), TR("No telemetry data"));
        }

        char buf[512];
        const bool keyConflict = Nodes.hasKeyConflict(n.nodeId);
        snprintf(buf, sizeof(buf), "%s", keyConflict
                 ? TR("! KEY CHANGED - open Node Actions to review\n\n") : "");
        const size_t used = strlen(buf);
        snprintf(buf + used, sizeof(buf) - used,
                 TR("Name: %s\n"
                 "Short: %s\n"
                 "ID: !%08X\n"
                 "Last heard: %s\n"
                 "SNR: %.1f dB\n"
                 "Hops: %u\n"
                 "Channel: %d\n"
                 "\n"
                 "Position:\n%s\n"
                 "\n"
                 "Telemetry:\n%s"),
                 name, shortName, n.nodeId, heard,
                 (double)n.snr, (unsigned)n.hops, n.chanIdx, pos, telem);
        setLabelTextEmojiSafe(s_nodesDetail, buf);
    }
    LV_UNUSED(vals);
#else
    // ── Identity ──
    snprintf(vals, sizeof(vals), "%s\n%s\n!%08X", name, shortName, n.nodeId);
    nodesSetSection(NODES_SEC_IDENTITY, TR("Name\nShort\nID"), vals);

    // ── Link ──
    // Hops is only meaningful when a packet carried hop_start; without it "0
    // hops" would read as "direct neighbor", which is a different claim.
    char hops[16];
    if (n.hasHops) snprintf(hops, sizeof(hops), "%u", (unsigned)n.hops);
    else           snprintf(hops, sizeof(hops), "n/a");
    char snr[16];
    if (n.lastHeardMs > 0) snprintf(snr, sizeof(snr), "%.1f dB", (double)n.snr);
    else                   snprintf(snr, sizeof(snr), "n/a");
    char chan[16];
    if (n.chanIdx >= 0) snprintf(chan, sizeof(chan), "%d", n.chanIdx);
    else                snprintf(chan, sizeof(chan), "n/a");
    // Three states, not two. "no" and "not seen yet" are different claims: the
    // mesh is almost entirely unsigned traffic today, so an unsigned node is
    // the ordinary case and should not read as a failed check.
    const char *signedStr;
    if (Nodes.hasKeyConflict(n.nodeId)) signedStr = TR("! KEY CHANGED");
    else if (n.xeddsaVerified) signedStr = "yes";
    else if (n.hasPubKey)   signedStr = TR("not seen");
    else                    signedStr = TR("no key");
    snprintf(vals, sizeof(vals), "%s\n%s\n%s\n%s\n%s", heard, snr, hops, chan, signedStr);
    nodesSetSection(NODES_SEC_LINK, TR("Last heard\nSNR\nHops\nChannel\nSigned"), vals);

    // ── Position ──
    // Every field is emitted even when unknown, here and below: a section that
    // grows and shrinks with the selection makes the whole panel jump under the
    // cursor, and "n/a" is also a fact worth reading.
    if (n.hasPosition && (n.latI != 0 || n.lonI != 0)) {
        snprintf(vals, sizeof(vals), "%.5f\n%.5f\n%ld m",
                 (double)((float)n.latI / 10000000.0f),
                 (double)((float)n.lonI / 10000000.0f),
                 (long)n.alt);
    } else {
        snprintf(vals, sizeof(vals), "n/a\nn/a\nn/a");
    }
    nodesSetSection(NODES_SEC_POSITION, TR("Lat\nLon\nAlt"), vals);

    // ── Telemetry ──
    const bool useImperial = (s_cfg.displayUnits != 0);
    char batt[16], volt[16], chu[16], airtx[16], temp[16], hum[16], press[16];
    if (n.hasDeviceTelemetry) {
        snprintf(batt,  sizeof(batt),  "%.0f%%",   (double)n.battPct);
        snprintf(volt,  sizeof(volt),  "%.2f V",   (double)n.voltage);
        snprintf(chu,   sizeof(chu),   "%.1f%%",   (double)n.chUtil);
        snprintf(airtx, sizeof(airtx), "%.1f%%",   (double)n.airUtil);
    } else {
        snprintf(batt, sizeof(batt), "n/a");
        snprintf(volt, sizeof(volt), "n/a");
        snprintf(chu, sizeof(chu), "n/a");
        snprintf(airtx, sizeof(airtx), "n/a");
    }
    if (n.hasEnvironmentTelemetry) {
        if (useImperial) {
            snprintf(temp,  sizeof(temp),  "%.1f F",     (double)(n.temperatureC * (9.0f / 5.0f) + 32.0f));
            snprintf(press, sizeof(press), "%.2f inHg",  (double)(n.pressureHpa * 0.0295299831f));
        } else {
            snprintf(temp,  sizeof(temp),  "%.1f C",     (double)n.temperatureC);
            snprintf(press, sizeof(press), "%.1f hPa",   (double)n.pressureHpa);
        }
        snprintf(hum, sizeof(hum), "%.1f%%", (double)n.humidityPct);
    } else {
        snprintf(temp, sizeof(temp), "n/a");
        snprintf(hum, sizeof(hum), "n/a");
        snprintf(press, sizeof(press), "n/a");
    }
    snprintf(vals, sizeof(vals), "%s\n%s\n%s\n%s\n%s\n%s\n%s",
             batt, volt, chu, airtx, temp, hum, press);
    nodesSetSection(NODES_SEC_TELEMETRY,
                    TR("Battery\nVoltage\nChUtil\nAirTx\nTemp\nHumidity\nPressure"), vals);
#endif  // NODES_LAYOUT_WIDE

    if (s_nodesInfoPanel) {
        lv_obj_scroll_to_y(s_nodesInfoPanel, 0, LV_ANIM_OFF);
    }
}

static void onNodeSnapshotPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_nodesFilteredCount) return;
    if (s_nodesActionModal) {
        closeNodesActionMenu();
    }
#if NODES_INFO_AS_MODAL
    // A press reaching the list through the modal's edge: the modal goes,
    // rather than quietly re-rendering itself for a different node.
    if (s_nodesInfoDlg) {
        closeNodesInfoDlg();
        return;
    }
#endif
    s_nodesSelected = idx;
    if (s_nodesSearchBtnFocused) {
        s_nodesSearchBtnFocused = false;
        s_nodesTopBtn = kNodesTopSearch;
        refreshNodesSearchButton();
        refreshNodesHint();
    }
    nodesClampRenderWindow();
    refreshNodesListSelection();
    refreshNodesDetails();
    if (lv_obj_t *selectedRow = selectedNodesRowObj()) {
        lv_obj_scroll_to_view(selectedRow, LV_ANIM_OFF);
    }
}

#if UI_TOUCH_ONLY_PROFILE
// Tap a row to act on it. Selecting is what the LV_EVENT_PRESSED handler above
// does; this is the second half of the gesture, which on the keyboard builds is
// Enter on the highlighted row.
//
// On CLICKED rather than PRESSED, and the distinction is the whole reason this
// is a separate callback: PRESSED fires the moment a finger lands, so opening
// the menu there would pop it on every scroll of the list. LVGL withholds
// CLICKED once a press has turned into a scroll, so a drag still selects rows
// as it passes them and opens nothing.
//
// Routed through the press handler rather than repeating its four lines: that
// one also closes an already-open menu, which is exactly what a tap on a
// different row should do before opening its own.
static void onNodeSnapshotClicked(lv_event_t *e) {
    onNodeSnapshotPressed(e);
    openNodesActionMenu();
}
#endif

static void closeTracerouteProgressModal() {
    lvObjDeleteSafe(s_tracerouteBackdrop);
    s_tracerouteModal = nullptr;
    s_tracerouteStatusLabel = nullptr;
    s_tracerouteResultsBox = nullptr;
    s_tracerouteResultsLabel = nullptr;
    s_tracerouteNodeId = 0;
    s_traceroutePacketId = 0;
    s_tracerouteStartedMs = 0;
    s_tracerouteAwaitingRouting = false;
    s_tracerouteAwaitingReply = false;
    s_tracerouteResolved = false;
}

static void onTracerouteBackdropPressed(lv_event_t *e) {
#if UI_TOUCH_ONLY_PROFILE
    if (lv_event_get_target_obj(e) == s_tracerouteBackdrop) {
        closeTracerouteProgressModal();
    }
#else
    LV_UNUSED(e);
#endif
}

static void tracerouteProgressSetStatus(const char *status, lv_color_t color) {
    if (!s_tracerouteStatusLabel) return;
    lv_obj_set_style_text_color(s_tracerouteStatusLabel, color, 0);
    lv_label_set_text(s_tracerouteStatusLabel, (status && status[0]) ? status : "-");
}

static void tracerouteProgressRenderRoutesPayload(const uint8_t *payload, size_t payloadLen, bool viaMqtt) {
    if (!s_tracerouteModal || !s_tracerouteResultsBox || !s_tracerouteResultsLabel) return;
    // An empty RouteDiscovery is a legitimate reply — it means a direct hop with
    // no relays in between — so it still has a path worth drawing.
    if (!payload) payloadLen = 0;

    constexpr int kMaxPathNodes = 12;
    // Three edges render per line and the box holds three lines.
    constexpr int kMaxEdges = 9;
    uint32_t route[kMaxPathNodes] = {};
    uint32_t routeBack[kMaxPathNodes] = {};
    int routeCount = 0;
    int routeBackCount = 0;
    // RouteDiscovery fields 2 and 4 — snr_towards and snr_back — one entry per
    // edge, in quarter-dB. They were arriving on every traceroute and being
    // dropped by the skip below; this is the same data wadamesh surfaces as
    // "Trace SNR", except Meshtastic already carries it in the reply we ask for.
    //
    // One more than the node list: RouteDiscovery omits both endpoints from
    // `route`, but reports an SNR for the final hop into the destination too.
    constexpr int kMaxSnr = kMaxPathNodes + 2;
    int32_t snrTowards[kMaxSnr] = {};
    int32_t snrBack[kMaxSnr] = {};
    int snrTowardsCount = 0;
    int snrBackCount = 0;
    // Meshtastic's "no measurement" marker. Rendering it as -32 dB would be a
    // plausible-looking lie, so it stays distinguishable from a real reading.
    constexpr int32_t kSnrUnknown = -128;

    auto skipPb = [](const uint8_t *buf, size_t len, size_t off, uint32_t wtype) -> size_t {
        if (wtype == 0) {
            uint64_t v = 0;
            return pbReadVarint(buf, len, off, v);
        }
        if (wtype == 1) return (off + 8 <= len) ? (off + 8) : 0;
        if (wtype == 5) return (off + 4 <= len) ? (off + 4) : 0;
        if (wtype == 2) {
            uint64_t sz = 0;
            size_t j = pbReadVarint(buf, len, off, sz);
            if (!j) return 0;
            if (sz > len - j) return 0;
            return j + (size_t)sz;
        }
        return 0;
    };

    auto appendNode = [](uint32_t node, uint32_t *out, int cap, int &count) {
        if (count >= cap) return;
        if (count > 0 && out[count - 1] == node) return;
        out[count++] = node;
    };

    size_t i = 0;
    while (i < payloadLen) {
        uint64_t tag = 0;
        i = pbReadVarint(payload, payloadLen, i, tag);
        if (!i) break;

        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wtype = (uint32_t)(tag & 7);
        const bool isRouteField = (field == 1 || field == 3);

        if (isRouteField && wtype == 5) {
            if (i + 4 > payloadLen) break;
            uint32_t node = (uint32_t)payload[i]
                          | ((uint32_t)payload[i + 1] << 8)
                          | ((uint32_t)payload[i + 2] << 16)
                          | ((uint32_t)payload[i + 3] << 24);
            if (field == 1) appendNode(node, route, kMaxPathNodes, routeCount);
            else appendNode(node, routeBack, kMaxPathNodes, routeBackCount);
            i += 4;
            continue;
        }

        if (isRouteField && wtype == 2) {
            uint64_t sz = 0;
            size_t j = pbReadVarint(payload, payloadLen, i, sz);
            if (!j) break;
            if (sz > payloadLen - j) break;
            size_t end = j + (size_t)sz;
            while (j + 4 <= end) {
                uint32_t node = (uint32_t)payload[j]
                              | ((uint32_t)payload[j + 1] << 8)
                              | ((uint32_t)payload[j + 2] << 16)
                              | ((uint32_t)payload[j + 3] << 24);
                if (field == 1) appendNode(node, route, kMaxPathNodes, routeCount);
                else appendNode(node, routeBack, kMaxPathNodes, routeBackCount);
                j += 4;
            }
            i = end;
            continue;
        }

        // int32, so a varint — and protobuf encodes a negative int32 as a full
        // 10-byte varint rather than zigzag, which is why this takes the low 32
        // bits and reinterprets rather than casting the 64-bit value directly.
        const bool isSnrField = (field == 2 || field == 4);
        auto appendSnr = [](uint64_t raw, int32_t *out, int cap, int &count) {
            if (count >= cap) return;
            out[count++] = (int32_t)(uint32_t)raw;
        };

        if (isSnrField && wtype == 0) {
            uint64_t v = 0;
            size_t j = pbReadVarint(payload, payloadLen, i, v);
            if (!j) break;
            if (field == 2) appendSnr(v, snrTowards, kMaxSnr, snrTowardsCount);
            else            appendSnr(v, snrBack,    kMaxSnr, snrBackCount);
            i = j;
            continue;
        }

        if (isSnrField && wtype == 2) {
            uint64_t sz = 0;
            size_t j = pbReadVarint(payload, payloadLen, i, sz);
            if (!j) break;
            if (sz > payloadLen - j) break;
            const size_t end = j + (size_t)sz;
            while (j < end) {
                uint64_t v = 0;
                const size_t k = pbReadVarint(payload, payloadLen, j, v);
                if (!k || k > end) break;
                if (field == 2) appendSnr(v, snrTowards, kMaxSnr, snrTowardsCount);
                else            appendSnr(v, snrBack,    kMaxSnr, snrBackCount);
                j = k;
            }
            i = end;
            continue;
        }

        i = skipPb(payload, payloadLen, i, wtype);
        if (!i) break;
    }

    struct Edge {
        uint32_t from;
        uint32_t to;
        bool viaMqtt;
        bool hasSnr;
        float snrDb;
    };
    Edge edges[kMaxEdges] = {};
    int edgeCount = 0;

    // Edge idx takes snr[idx]: both are ordered from the sending end outward,
    // and the path array here has already had the endpoints put back on, so the
    // two line up one-for-one.
    auto appendEdges = [&](const uint32_t *path, int count, bool viaMqtt,
                           const int32_t *snrQ, int snrCount) {
        for (int idx = 0; idx + 1 < count && edgeCount < kMaxEdges; idx++) {
            Edge e = {path[idx], path[idx + 1], viaMqtt, false, 0.0f};
            if (snrQ && idx < snrCount && snrQ[idx] != kSnrUnknown) {
                e.hasSnr = true;
                e.snrDb = (float)snrQ[idx] / 4.0f;   // quarter-dB on the wire
            }
            edges[edgeCount++] = e;
        }
    };

    // RouteDiscovery carries only the nodes *between* the endpoints: `route` is
    // the way out and `route_back` the way home, with neither us nor the target
    // listed. Both ends have to be put back on, or a one-relay traceroute draws
    // as "us -> relay" and never names the node that was actually traced.
    constexpr int kMaxFullPath = kMaxPathNodes + 2;
    uint32_t hops[kMaxFullPath];
    int hopCount = 0;

    auto appendHop = [&](uint32_t node) {
        if (node == 0 || hopCount >= kMaxFullPath) return;
        if (hopCount > 0 && hops[hopCount - 1] == node) return;
        hops[hopCount++] = node;
    };

    appendHop(s_myNodeId);
    for (int idx = 0; idx < routeCount; idx++) appendHop(route[idx]);
    appendHop(s_tracerouteNodeId);
    appendEdges(hops, hopCount, false, snrTowards, snrTowardsCount);

    // The return leg is only drawn when the reply actually reported one; a
    // symmetric route comes back with route_back empty and needs no second line.
    if (routeBackCount > 0) {
        hopCount = 0;
        appendHop(s_tracerouteNodeId);
        for (int idx = 0; idx < routeBackCount; idx++) appendHop(routeBack[idx]);
        appendHop(s_myNodeId);
        appendEdges(hops, hopCount, viaMqtt, snrBack, snrBackCount);
    } else if (viaMqtt && edgeCount < kMaxEdges && s_tracerouteNodeId != 0 && s_myNodeId != 0) {
        // No return path was reported, but the reply came in over MQTT rather
        // than RF. That changes what the result means, so it still gets a line.
        edges[edgeCount++] = {s_tracerouteNodeId, s_myNodeId, true, false, 0.0f};
    }

    char text[420];
    text[0] = '\0';

    // We are one end of every path we draw, and our own node is not in the node
    // DB — so the plain label renders us as !hex next to everyone else's short
    // name. Feed our configured short name in as the hint for that one id.
    auto hopLabel = [](uint32_t node, char *out, size_t outLen) {
        const char *hint = (node != 0 && node == s_myNodeId) ? s_cfg.nodeShort : nullptr;
        liveNodeLabelWithHint(node, hint, out, outLen, true);
    };

    for (int idx = 0; idx < edgeCount; idx++) {
        char from[20];
        char to[20];
        hopLabel(edges[idx].from, from, sizeof(from));
        hopLabel(edges[idx].to, to, sizeof(to));

        const char *sep = "";
        if (idx > 0) {
            sep = ((idx % 3) == 0) ? "\n" : ", ";
        }

        // The SNR replaces the transport label rather than joining it: "7.5dB"
        // is the same width as "radio", so a path that fitted before still fits,
        // and an RF hop with a measurement is self-evidently radio anyway.
        // "radio" survives for hops the reply gave no measurement for.
        char metric[12];
        if (edges[idx].viaMqtt) {
            snprintf(metric, sizeof(metric), "mqtt");
        } else if (edges[idx].hasSnr) {
            snprintf(metric, sizeof(metric), "%.1fdB", (double)edges[idx].snrDb);
        } else {
            snprintf(metric, sizeof(metric), "radio");
        }

        size_t used = strlen(text);
        if (used + 8 >= sizeof(text)) break;
        snprintf(text + used, sizeof(text) - used, "%s%s -> %s (%s)", sep, from, to, metric);
    }

    if (!text[0]) {
        strncpy(text, TR("No hop path in reply"), sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }

    lv_label_set_text(s_tracerouteResultsLabel, text);
    lv_obj_clear_flag(s_tracerouteResultsBox, LV_OBJ_FLAG_HIDDEN);

    int rows = (edgeCount + 2) / 3;
    if (rows < 3) rows = 3;
    if (rows > 3) rows = 3;

    lv_coord_t lineH = lv_font_get_line_height(&lv_font_montserrat_10);
    lv_coord_t boxH = (lv_coord_t)(rows * (lineH + 2) + 8);
    lv_obj_set_height(s_tracerouteResultsBox, boxH);

    const int maxModalH = lv_disp_get_ver_res(NULL) - 8;
    int modalH = 104 + boxH;
    if (modalH > maxModalH) modalH = maxModalH;
    lv_obj_set_height(s_tracerouteModal, (lv_coord_t)modalH);
}

static void tracerouteProgressRenderRoutes(const MeshPacket &pkt) {
    tracerouteProgressRenderRoutesPayload(pkt.payload, pkt.payloadLen, (pkt.hdr.flags & 0x10) != 0);
}

static void openTracerouteProgressModal(uint32_t nodeId, uint32_t packetId) {
    if (!s_rootScreen) return;

    closeTracerouteProgressModal();

    s_tracerouteNodeId = nodeId;
    s_traceroutePacketId = packetId;
    s_tracerouteStartedMs = millis();
    s_tracerouteAwaitingRouting = true;
    s_tracerouteAwaitingReply = false;
    s_tracerouteResolved = false;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);

    s_tracerouteBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_tracerouteBackdrop, w, h);
    lv_obj_align(s_tracerouteBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_tracerouteBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_tracerouteBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_tracerouteBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_tracerouteBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_tracerouteBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_tracerouteBackdrop, 0, 0);
#if UI_TOUCH_ONLY_PROFILE
    lv_obj_add_event_cb(s_tracerouteBackdrop, onTracerouteBackdropPressed, LV_EVENT_PRESSED, nullptr);
#endif

    int modalW = w - 8;
    if (modalW < 180) modalW = w - 4;
    if (modalW < 120) modalW = w;
    const int modalH = 96;

    s_tracerouteModal = lv_obj_create(s_tracerouteBackdrop);
    lv_obj_set_size(s_tracerouteModal, modalW, modalH);
    lv_obj_align(s_tracerouteModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_tracerouteModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_tracerouteModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_tracerouteModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_tracerouteModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tracerouteModal, 1, 0);
    lv_obj_set_style_border_color(s_tracerouteModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_tracerouteModal, 4, 0);
    lv_obj_set_style_pad_bottom(s_tracerouteModal, 6, 0);
    lv_obj_set_style_pad_row(s_tracerouteModal, 3, 0);
    lv_obj_set_flex_flow(s_tracerouteModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_tracerouteModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_tracerouteBackdrop);

    lv_obj_t *title = lv_label_create(s_tracerouteModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, TR("Traceroute"));

    lv_obj_t *target = lv_label_create(s_tracerouteModal);
    lv_obj_set_width(target, lv_pct(100));
    lv_obj_set_style_text_font(target, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(target, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(target, LV_TEXT_ALIGN_CENTER, 0);
    char who[20];
    liveNodeLabel(nodeId, who, sizeof(who), true);
    char targetLine[48];
    snprintf(targetLine, sizeof(targetLine), "%s  %08lX", who, (unsigned long)packetId);
    lv_label_set_text(target, targetLine);

    s_tracerouteStatusLabel = lv_label_create(s_tracerouteModal);
    lv_obj_set_width(s_tracerouteStatusLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_tracerouteStatusLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(s_tracerouteStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    tracerouteProgressSetStatus(TR("Sending request..."), lv_color_hex(0xE8F1FF));

    s_tracerouteResultsBox = lv_obj_create(s_tracerouteModal);
    lv_obj_set_width(s_tracerouteResultsBox, lv_pct(100));
    lv_obj_set_height(s_tracerouteResultsBox, 52);
    lv_obj_clear_flag(s_tracerouteResultsBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_tracerouteResultsBox, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(s_tracerouteResultsBox, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_tracerouteResultsBox, 1, 0);
    lv_obj_set_style_border_color(s_tracerouteResultsBox, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(s_tracerouteResultsBox, 4, 0);
    lv_obj_set_style_pad_right(s_tracerouteResultsBox, 4, 0);
    lv_obj_set_style_pad_top(s_tracerouteResultsBox, 3, 0);
    lv_obj_set_style_pad_bottom(s_tracerouteResultsBox, 3, 0);
    lv_obj_add_flag(s_tracerouteResultsBox, LV_OBJ_FLAG_HIDDEN);

    s_tracerouteResultsLabel = lv_label_create(s_tracerouteResultsBox);
    lv_obj_set_width(s_tracerouteResultsLabel, lv_pct(100));
    lv_obj_set_style_text_font(s_tracerouteResultsLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_tracerouteResultsLabel, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(s_tracerouteResultsLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_tracerouteResultsLabel, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_tracerouteResultsLabel, "");

    lv_obj_t *hint = lv_label_create(s_tracerouteModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(hint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
#if UI_TOUCH_ONLY_PROFILE
    // The hint object above is left unused on this build: there is no key to
    // name, and the way out is the corner X. The fixed 96px modal spends no
    // height on either.
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    appendHeltecCloseX(s_tracerouteModal,
                       [](lv_event_t *ev) { LV_UNUSED(ev); closeTracerouteProgressModal(); },
                       /*size=*/22);
#elif UI_CARDPUTER_COMPACT
    lv_label_set_text(hint, TR("Bksp=Close"));
#else
    char hintText[24];
    snprintf(hintText, sizeof(hintText), "%s=Close", modalCloseKeyLabel());
    lv_label_set_text(hint, hintText);
#endif
}

static void tracerouteProgressSetTxResult(bool ok) {
    if (!s_tracerouteModal) return;

    if (!ok) {
        s_tracerouteAwaitingRouting = false;
        s_tracerouteAwaitingReply = false;
        s_tracerouteResolved = true;
        tracerouteProgressSetStatus(TR("Send failed"), lv_color_hex(0xFF8080));
        return;
    }

    tracerouteProgressSetStatus(TR("Sent. Waiting for route ACK..."), lv_color_hex(0xE8F1FF));
}

static void tracerouteProgressOnRouting(uint32_t fromNode, uint32_t requestId, uint32_t errorReason,
                                        const uint8_t *routeReplyPayload,
                                        size_t routeReplyLen,
                                        bool viaMqtt) {
    if (!s_tracerouteModal) return;
    if (requestId == 0 || requestId != s_traceroutePacketId) return;
    if (s_tracerouteResolved) return;

    char who[20];
    liveNodeLabel(fromNode, who, sizeof(who), false);

    if (errorReason == 0) {
        if (routeReplyPayload && routeReplyLen > 0) {
            s_tracerouteAwaitingRouting = false;
            s_tracerouteAwaitingReply = false;
            s_tracerouteResolved = true;
            char status[72];
            snprintf(status, sizeof(status), TR("Reply from %s"), who);
            tracerouteProgressSetStatus(status, lv_color_hex(0xB8FFB8));
            tracerouteProgressRenderRoutesPayload(routeReplyPayload, routeReplyLen, viaMqtt);
            return;
        }

        s_tracerouteAwaitingRouting = false;
        s_tracerouteAwaitingReply = true;
        char status[72];
        snprintf(status, sizeof(status), TR("ACK from %s. Waiting for reply..."), who);
        tracerouteProgressSetStatus(status, lv_color_hex(0xE8F1FF));
        return;
    }

    s_tracerouteAwaitingRouting = false;
    s_tracerouteAwaitingReply = false;
    s_tracerouteResolved = true;
    const char *errName = routingErrorName(errorReason);
    char status[80];
    if (errName) {
        snprintf(status, sizeof(status), TR("NAK %s(%lu)"), errName, (unsigned long)errorReason);
    } else {
        snprintf(status, sizeof(status), TR("NAK err=%lu"), (unsigned long)errorReason);
    }
    tracerouteProgressSetStatus(status, lv_color_hex(0xFF8080));
}

static void tracerouteProgressOnResponse(const MeshPacket &pkt) {
    if (!s_tracerouteModal) return;
    if (!s_tracerouteAwaitingRouting && !s_tracerouteAwaitingReply) return;
    if (s_tracerouteNodeId != 0 && pkt.hdr.from != s_tracerouteNodeId) return;
    if (pkt.hdr.to != s_myNodeId) return;
    if (s_traceroutePacketId == 0 || pkt.requestId != s_traceroutePacketId) return;

    s_tracerouteAwaitingRouting = false;
    s_tracerouteAwaitingReply = false;
    s_tracerouteResolved = true;
    char who[20];
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    uint32_t elapsedMs = (s_tracerouteStartedMs != 0) ? (millis() - s_tracerouteStartedMs) : 0;
    char status[72];
    snprintf(status, sizeof(status), TR("Reply from %s (%lus)"), who, (unsigned long)(elapsedMs / 1000UL));
    tracerouteProgressSetStatus(status, lv_color_hex(0xB8FFB8));
    tracerouteProgressRenderRoutes(pkt);
}

// Long enough for a multi-hop round trip with relay backoff, short enough that
// a dead traceroute doesn't look like a live one indefinitely.
static constexpr uint32_t kTracerouteTimeoutMs = 45000UL;

// Without this the modal sits on "Waiting for..." forever when nothing comes
// back, which on screen is indistinguishable from a traceroute still in flight.
// Takes no `now`: the modal is opened by a keypress in the same loop pass that
// sampled it, which leaves the start time a few milliseconds in the future and
// wraps the unsigned subtraction below to ~49 days — timing the traceroute out
// the instant it is sent. Sampling after the guards means millis() is only read
// while one is actually pending, and is always at or past the start.
static void serviceTracerouteTimeout() {
    if (!s_tracerouteModal) return;
    if (!s_tracerouteAwaitingRouting && !s_tracerouteAwaitingReply) return;
    if (s_tracerouteStartedMs == 0) return;

    const uint32_t elapsedMs = millis() - s_tracerouteStartedMs;
    if (elapsedMs < kTracerouteTimeoutMs) return;

    // An ACK without a reply means the target heard us and never answered —
    // worth telling apart from silence, which is usually a routing failure.
    const bool ackedButSilent = s_tracerouteAwaitingReply;
    s_tracerouteAwaitingRouting = false;
    s_tracerouteAwaitingReply = false;
    s_tracerouteResolved = true;

    const unsigned long secs = (unsigned long)(elapsedMs / 1000UL);
    char status[64];
    if (ackedButSilent) {
        snprintf(status, sizeof(status), TR("ACKed, no reply (%lus)"), secs);
    } else {
        snprintf(status, sizeof(status), TR("No reply (%lus)"), secs);
    }
    tracerouteProgressSetStatus(status, lv_color_hex(0xFFC080));
}

static bool sendTracerouteToNode(uint32_t toNodeId, uint32_t *packetIdOut) {
    if (packetIdOut) *packetIdOut = 0;
    if (toNodeId == 0 || toNodeId == 0xFFFFFFFF) return false;

    if (s_myNodeId == 0) {
        deriveNodeId();
    }

    uint32_t packetId = nextMeshPacketId();
    if (packetIdOut) *packetIdOut = packetId;
    openTracerouteProgressModal(toNodeId, packetId);

    bool ok = false;
    if (s_myNodeId != 0 && Radio.isReady()) {
        uint8_t proto[64];
        uint8_t cipher[96 + MESH_AEAD_OVERHEAD];
        size_t protoLen = encodeTracerouteRequest(proto, sizeof(proto), true,
                                                  s_cfg.okToMqtt ? 0x01 : 0);
        if (protoLen > 0) {
            const ChannelKey &ck = CHANNEL_KEYS[0];  // LongFast
            const size_t cipherLen = encryptChannelPayload(ck, packetId, s_myNodeId, toNodeId,
                                                           proto, protoLen, cipher, sizeof(cipher));
            if (cipherLen > 0) {
                uint8_t frame[sizeof(MeshHdr) + sizeof(cipher)];
                MeshHdr hdr = {};
                hdr.to = toNodeId;
                hdr.from = s_myNodeId;
                hdr.id = packetId;
                hdr.channel = ck.hash;
                hdr.flags = meshOriginHopFlagsForChannel(0, 1 << 3);   // want_ack
                hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

                memcpy(frame, &hdr, sizeof(hdr));
                memcpy(frame + sizeof(hdr), cipher, cipherLen);
                ok = Radio.transmit(frame, sizeof(hdr) + cipherLen);
            }
        }
    }

    char prefix[12];
    liveBuildPrefix(prefix, sizeof(prefix));
    char dst[16];
    liveNodeLabel(toNodeId, dst, sizeof(dst), true);
    char status[72];
    snprintf(status, sizeof(status), "T TRC U %s %08lX %s",
             dst, (unsigned long)packetId, ok ? "OK" : "ER");
    liveFeedAddPrefixed(prefix, status, ok ? TFT_DARKGREY : TFT_RED, 0, false);
    tracerouteProgressSetTxResult(ok);
    return ok;
}

static void refreshNodesActionMenuSelection() {
#if defined(DEVICE_TDECK_PRO)
    const int count = activeActionCount();
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = s_nodesActionRows[i];
        if (!row) continue;
        const bool disabled = nodesActionRowDisabled(i);
        const bool selected = (i == s_nodesActionSelection);
        // An unavailable action loses its button outline rather than being
        // struck through: enabled rows are boxed, unavailable ones are bare
        // text. That is the e-paper equivalent of greying the button out.
        //
        // Greying the label itself is not available on this panel, and not for
        // want of trying -- it is 1 bpp. LVGL drives this display at
        // LV_COLOR_FORMAT_I1, and lv_draw_sw_blend_to_i1.c reduces every colour
        // through a hard luminance threshold with no dithering, so there is no
        // middle to land on: a light grey crosses the threshold to white and the
        // label disappears completely, while a dark grey stays black and looks
        // exactly like an enabled row. The outline is the only thing on the row
        // that can be taken away by degrees.
        //
        // The selection border still wins over all of it. The cursor can be
        // moved onto an unavailable row, and it has to stay visible when it is.
        const int borderWidth = selected ? 2 : (disabled ? 0 : 1);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, borderWidth, 0);
        lv_obj_set_style_border_color(row, lv_color_make(0, 0, 0), 0);
        if (lv_obj_t *lbl = lv_obj_get_child(row, 0)) {
            lv_obj_set_style_text_color(lbl, lv_color_make(0, 0, 0), 0);
            lv_obj_set_style_text_opa(lbl, LV_OPA_COVER, 0);
            lv_obj_set_style_text_decor(lbl, LV_TEXT_DECOR_NONE, 0);
        }
    }
#else
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selectedBg = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    const lv_color_t idleBg = isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266);
    const lv_color_t selectedBorder = isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF);
    const lv_color_t idleBorder = isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);

    const lv_color_t disabledText = isLight ? lv_color_hex(0x8A97AC) : lv_color_hex(0x5F7093);
    const lv_color_t enabledText = isLight ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    const int count = activeActionCount();
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = s_nodesActionRows[i];
        if (!row) continue;
        const bool disabled = nodesActionRowDisabled(i);
        // A disabled row still highlights when the cursor is on it — the cursor
        // has to be somewhere, and a row that swallows the highlight looks like
        // the menu stopped responding. It reads as unavailable through its
        // washed-out label instead.
        bool selected = (i == s_nodesActionSelection);
        lv_obj_set_style_bg_color(row, selected ? selectedBg : idleBg, 0);
        lv_obj_set_style_bg_opa(row,
                                disabled ? (isLight ? LV_OPA_40 : LV_OPA_20)
                                         : (selected ? LV_OPA_COVER
                                                     : (isLight ? LV_OPA_90 : LV_OPA_40)),
                                0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, selected ? selectedBorder : idleBorder, 0);
        if (lv_obj_t *lbl = lv_obj_get_child(row, 0)) {
            lv_obj_set_style_text_color(lbl, disabled ? disabledText : enabledText, 0);
        }
    }
#endif
}

static void closeNodesActionMenu() {
    lvObjDeleteSafe(s_nodesActionModal);
    s_nodesActionSelection = 0;
    s_nodesActionNodeId = 0;
    s_nodesActionPacketId = 0;
    s_nodesActionMsgMode = false;
    memset(s_nodesActionRows, 0, sizeof(s_nodesActionRows));
}

#if UI_TOUCH_ONLY_PROFILE
static void onNodesActionClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeNodesActionMenu();
}
#endif

static void executeNodesActionSelection() {
    if (s_nodesActionNodeId == 0) {
        closeNodesActionMenu();
        return;
    }

    // ── Message-mode rows ───────────────────────────────────────────────────
    // Each tears the modal down *before* acting. That ordering is not cosmetic:
    // sending refreshes the chat view, which rebuilds the rows this modal was
    // opened from — the same reason emojiPickerActivate() closes first.
    if (s_nodesActionMsgMode) {
        const int sel = s_nodesActionSelection;
        const uint32_t pid = s_nodesActionPacketId;

        if (sel >= 0 && sel < kTapbackTrayCount) {
            const char *glyph = kTapbackTray[sel];
            closeNodesActionMenu();
            sendQuickEmoji(glyph, pid);
            return;
        }
        if (sel == kMsgActionMoreIdx) {
            // The curated six are a shortcut, not a ceiling. openEmojiPicker()
            // reads the tapback target from s_selectedMsgReplyPacketId, which
            // the caller set when the message was selected.
            closeNodesActionMenu();
            openEmojiPicker(/*sendMode=*/true);
            return;
        }
#if HAS_MESSAGE_ACTIONS
        if (sel == kMsgActionInfoIdx) {
            const uint32_t from = s_nodesActionNodeId;
            const uint32_t pid = s_nodesActionPacketId;
            closeNodesActionMenu();
            openMsgSenderInfoModal(from, pid);
            return;
        }
#endif
        if (sel == kMsgActionReplyIdx) {
            const uint32_t replyId = s_selectedMsgReplyPacketId;
            char preview[kReplyPreviewTextMax + 1];
            utf8util::copyTruncate(preview, sizeof(preview), s_selectedMsgText);
            closeNodesActionMenu();
            openComposePrompt(replyId, preview);
            return;
        }
        // Anything past Reply is one of the node actions; fall through with the
        // index mapped back so the arms below keep their original 0..5 numbering.
        // Through the map, not a subtraction: message mode omits Favorite, so
        // the offsets diverge past it — a plain shift would fire Request Node
        // where the user pressed Request Position.
        const int nodeSel = sel - kMsgActionNodeBase;
        if (nodeSel < 0 || nodeSel >= kMsgActionNodeCount) {
            closeNodesActionMenu();
            return;
        }
        s_nodesActionSelection = (int)kMsgActionNodeMap[nodeSel];
    }

#if HAS_NODE_LOS
    if (s_nodesActionSelection == kNodesActionLos) {
        if (!s_nodesActionLosEnabled) return;
        uint32_t losNodeId = s_nodesActionNodeId;
        closeNodesActionMenu();
        openNodeLosModal(losNodeId);
        return;
    }
#endif

#if HAS_ADMIN_TERMINAL
    if (s_nodesActionAdminEnabled && s_nodesActionSelection == kNodesActionAdmin) {
        const uint32_t peer = s_nodesActionNodeId;
        closeNodesActionMenu();
        openAdminTerminalModal(peer);
        return;
    }
#endif

#if NODES_INFO_AS_MODAL
    if (s_nodesActionSelection == kNodesActionInfo) {
        if (!s_nodesActionInfoEnabled) return;
        closeNodesActionMenu();
        openNodesInfoDlg();
        return;
    }
#endif

    if (s_nodesActionSelection == kNodesActionShare) {
        if (!s_nodesActionShareEnabled) return;
        const uint32_t shareId = s_nodesActionNodeId;
        closeNodesActionMenu();
        const NodeEntry *node = Nodes.find(shareId);
        if (node && Radio.isReady() && s_myNodeId != 0) {
            (void)Channels.sendSharedNodeInfo(s_myNodeId, shareId,
                                              node->longName, node->shortName,
                                              node->hasPubKey ? node->pubKey : nullptr);
        }
        return;
    }

    if (s_nodesActionSelection == kNodesActionDelete) {
        // Greyed rows keep the highlight but do nothing when activated, same as
        // Locate and LOS below.
        if (!s_nodesActionDeleteEnabled) return;
        uint32_t deleteNodeId = s_nodesActionNodeId;
        // The menu goes first: the confirmation layers over whatever is behind
        // it, and leaving this open would put a dialog on top of a menu acting
        // on the node the dialog is about.
        closeNodesActionMenu();
        openDestructiveConfirm(DESTRUCTIVE_CONFIRM_NODE_DELETE, deleteNodeId);
        return;
    }

#if HAS_NODE_LOCATE
    if (s_nodesActionSelection == kNodesActionLocate) {
        // Greyed rows keep the highlight but do nothing when activated; the
        // label already says why.
        if (!s_nodesActionLocateEnabled) return;
        uint32_t nodeId = s_nodesActionNodeId;
        closeNodesActionMenu();
        openNodeLocateModal(nodeId);
        return;
    }
#endif

    if (s_nodesActionSelection == 0) {
        uint32_t packetId = 0;
        (void)sendTracerouteToNode(s_nodesActionNodeId, &packetId);
        closeNodesActionMenu();
        return;
    }

    if (s_nodesActionSelection == 1) {
        uint32_t nodeId = s_nodesActionNodeId;
        DmConv *conv = DMs.find(nodeId);
        if (!conv) {
            const NodeEntry *node = Nodes.find(nodeId);
            const char *shortName = (node && liveShortNameUsable(node->shortName)) ? node->shortName : nullptr;
            conv = DMs.findOrCreate(nodeId, shortName);
        }

        closeNodesActionMenu();

        openDmModal();
        if (conv) {
            for (int i = 0; i < s_dmConvCount; i++) {
                if (s_dmConvNodeIds[i] == nodeId) {
                    s_dmSelection = i + 1;  // +1 because row 0 is "New DM"
                    // Into the conversation itself where the list is a drawer;
                    // on the two panes, the list keeps the keys as before.
                    s_dmMsgPanelFocused = dmListIsDrawer();
                    refreshDmModal(true);
                    break;
                }
            }
        }
        return;
    }

    if (s_nodesActionSelection == 2) {
        uint32_t nodeId = s_nodesActionNodeId;
        const NodeEntry *node = Nodes.find(nodeId);
        const bool isFavorite = node && node->favorite;
        (void)Nodes.setFavorite(nodeId, !isFavorite);
        closeNodesActionMenu();

        // Re-snapshot and re-select only when the Nodes screen is actually up.
        // Opened from the chat cursor there is no list behind this modal, and
        // these calls would rebuild rows for a modal that is not on screen and
        // move a selection index nothing is showing. The favorite itself is
        // already stored above either way.
        if (s_nodesModal) {
            snapshotNodesForModal();
            s_nodesSelected = -1;
            for (int i = 0; i < s_nodesFilteredCount; i++) {
                int snapshotIdx = s_nodesFilteredIdx[i];
                if (snapshotIdx >= 0
                    && snapshotIdx < s_nodesSnapshotCount
                    && s_nodesSnapshotIds[snapshotIdx] == nodeId) {
                    s_nodesSelected = i;
                    break;
                }
            }
            if (s_nodesSelected < 0 && s_nodesFilteredCount > 0) {
                s_nodesSelected = 0;
            }
            refreshNodesListRows();
            refreshNodesListSelection();
            refreshNodesDetails();
        }
        return;
    }

    if (s_nodesActionSelection == 3) {
        uint32_t nodeId = s_nodesActionNodeId;
        closeNodesActionMenu();
        if (Radio.isReady() && s_myNodeId != 0) {
            (void)Channels.sendNodeInfo(s_myNodeId,
                                        s_cfg.nodeLong,
                                        s_cfg.nodeShort,
                                        nodeId,
                                        /*wantResponse=*/true,
                                        s_cfg.okToMqtt);
        }
        return;
    }

    if (s_nodesActionSelection == 4) {
        uint32_t nodeId = s_nodesActionNodeId;
        closeNodesActionMenu();
        if (Radio.isReady() && s_myNodeId != 0) {
            (void)Channels.sendPositionRequest(s_myNodeId, nodeId, s_cfg.okToMqtt);
        }
        return;
    }

    if (s_nodesActionSelection == 5) {
        uint32_t nodeId = s_nodesActionNodeId;
        if (Ignored.contains(nodeId)) {
            Ignored.remove(nodeId);
        } else {
            Ignored.add(nodeId);
        }
        closeNodesActionMenu();
        // Force a repaint of any visible chat/DM views so filtering state is
        // reflected immediately (existing already-buffered lines stay put).
        s_lastRenderedChannel = -1;
        s_lastRenderedCount = -1;
        refreshChatView(true);
        return;
    }
}

static void onNodesActionRowPressed(lv_event_t *e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    if (action < 0 || action >= activeActionCount()) return;
    s_nodesActionSelection = action;
    refreshNodesActionMenuSelection();
    executeNodesActionSelection();
}

// Who the actions modal says it is acting on. The node's own long name where
// there is room for it, so the title names a device rather than a hex id.
//
// The Cardputer takes the short name instead: its 240x135 panel leaves the
// modal 226 px wide, and a 40-char long name at font 12 wraps the title into
// several lines on a screen that has none to give — the six action rows below
// are what the modal is for.
//
// Falls back to liveNodeLabel() (short name, else hex) whenever no NODEINFO
// name has arrived, which is the normal case for a node we have only ever
// heard relayed.
static void nodesActionTitleLabel(uint32_t nodeId, char *out, size_t outLen) {
#if !UI_CARDPUTER_COMPACT
    const NodeEntry *e = Nodes.find(nodeId);
    if (e && e->hasName && e->longName[0]) {
        utf8util::copyTruncate(out, outLen, e->longName);
        return;
    }
#endif
    liveNodeLabel(nodeId, out, outLen, false);
}

// Open the action menu for an arbitrary node. Deliberately free of any
// s_nodesModal dependency: the Nodes screen is one caller, the chat cursor is
// another, and the menu itself acts on a node id rather than on a list row. The
// parent below already prefers the root screen, so it centres over whatever is
// on display without a layout change.
static void openNodesActionMenuFor(uint32_t nodeId, bool msgMode, uint32_t packetId) {
    if (nodeId == 0) return;

    if (s_nodesActionModal) {
        closeNodesActionMenu();
    }

    // After the close above, which clears both — the builder below reads them.
    s_nodesActionMsgMode = msgMode;
    s_nodesActionPacketId = packetId;
    s_nodesActionNodeId = nodeId;
    s_nodesActionSelection = 0;

    const int modalW = min(230, (int)lv_disp_get_hor_res(NULL) - 14);
    const int modalMaxH = lv_disp_get_ver_res(NULL) - 10;

    lv_obj_t *actionParent = s_rootScreen ? s_rootScreen : s_nodesModal;
    s_nodesActionModal = lv_obj_create(actionParent);
    lv_obj_set_width(s_nodesActionModal, modalW);
    lv_obj_set_height(s_nodesActionModal, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_nodesActionModal, modalMaxH, 0);
    lv_obj_align(s_nodesActionModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodesActionModal, LV_OBJ_FLAG_SCROLLABLE);
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_color(s_nodesActionModal, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_color(s_nodesActionModal, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_bg_color(s_nodesActionModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_border_color(s_nodesActionModal, lv_color_hex(0x5C86C6), 0);
#endif
    lv_obj_set_style_bg_opa(s_nodesActionModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodesActionModal, 1, 0);
    lv_obj_set_style_pad_all(s_nodesActionModal, 4, 0);
    lv_obj_set_style_pad_row(s_nodesActionModal, 4, 0);
    lv_obj_set_flex_flow(s_nodesActionModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesActionModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_nodesActionModal);

    lv_obj_t *title = lv_label_create(s_nodesActionModal);
    lv_obj_set_width(title, lv_pct(100));
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
#else
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
#endif
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    // Name the node. On the Nodes screen the highlighted row already says who
    // this is about, but opened from chat the list is not on screen at all —
    // and for a sender no longer in the node DB the hex id is the only
    // identification there is.
    //
    // Emoji-safe: long names are user-set and routinely carry emoji, which the
    // montserrat_12 face above has no glyphs for and would draw as tofu.
    {
        char who[48];
        char titleBuf[80];
        nodesActionTitleLabel(nodeId, who, sizeof(who));
        // Named for what it acts on. Opened from a message the menu is mostly
        // about that message; opened from the Nodes screen there is no message
        // to react to and the title stays as it was.
        snprintf(titleBuf, sizeof(titleBuf), "%s: %s",
                 s_nodesActionMsgMode ? TR("Message Actions") : TR("Node Actions"), who);
        setLabelTextEmojiSafe(title, titleBuf);
    }

    // Buttons arranged 2-per-row. Each label embeds its keyboard shortcut in
    // parens using the (T)raceroute inline style so touch and keyboard builds
    // share the exact same labels.
    // Looked up by id rather than taken from a list row: opened from the chat
    // cursor there is no row, and the sender may not be in the node DB at all
    // (cleared nodes, or someone we have only ever seen relayed). A null node
    // just means "not favorited" — which is honest, since Nodes.setFavorite()
    // will decline to favorite a node it does not know either.
    const NodeEntry *actionNode = Nodes.find(nodeId);
    const bool selectedIsFavorite = actionNode && actionNode->favorite;
    const bool selectedIsIgnored = Ignored.contains(nodeId);
    // Nothing to delete if the table has never had this node — see the flag's
    // definition. Favorites are deletable: pinning a node says "keep this one
    // when the table evicts", not "protect it from me".
    s_nodesActionDeleteEnabled = (actionNode != nullptr);
    s_nodesActionShareEnabled = actionNode && actionNode->hasName
                                && nodeId != s_myNodeId;
#if NODES_INFO_AS_MODAL
    {
        const NodeEntry *sel = s_nodesModal ? currentNodesSelection() : nullptr;
        s_nodesActionInfoEnabled = !msgMode && sel && sel->nodeId == nodeId;
    }
#endif
#if HAS_NODE_LOCATE
    // A node the table has never had a position for has nothing to point at, so
    // Locate is greyed rather than opening an empty map. (0, 0) is a real place
    // in the Gulf of Guinea, but as a stored value it means "never set".
    s_nodesActionLocateEnabled =
        actionNode && actionNode->hasPosition
        && (actionNode->latI != 0 || actionNode->lonI != 0);
#endif
#if HAS_NODE_LOS
    {
        const bool peerHasPos = actionNode && actionNode->hasPosition
                                && (actionNode->latI != 0 || actionNode->lonI != 0);
        double dummyLat = 0, dummyLon = 0;
        s_nodesActionLosEnabled = peerHasPos && nodeLosSelfPosition(dummyLat, dummyLon);
    }
#endif
#if HAS_ADMIN_TERMINAL
    // Node mode only. In message mode this index space belongs to the reaction
    // rows (kMsgActionNodeMap), and a terminal is not something a tapback menu
    // should be able to reach.
    s_nodesActionAdminEnabled = !msgMode && AdminPeerList.mayOpenTerminal(nodeId);
#endif
    #if UI_TOUCH_ONLY_PROFILE
        const char *kActionLabels[kNodesActionCount] = {
        TR_NOOP("Traceroute"),
        TR_NOOP("Send DM"),
        selectedIsFavorite ? TR_NOOP("Unfavorite") : TR_NOOP("Favorite"),
        TR_NOOP("Request Node"),
        TR_NOOP("Request Position"),
        selectedIsIgnored ? TR_NOOP("Unignore") : TR_NOOP("Ignore"),
    #if HAS_NODE_LOCATE
        TR_NOOP("Locate"),
    #endif
    #if HAS_NODE_LOS
        TR_NOOP("LOS"),
    #endif
    #if NODES_INFO_AS_MODAL
        TR_NOOP("Node Info"),
    #endif
        TR_NOOP("Share"),
        TR_NOOP("Delete"),
    #if HAS_ADMIN_TERMINAL
        TR_NOOP("Admin"),
    #endif
        };
    #else
        const char *kActionLabels[kNodesActionCount] = {
        TR_NOOP("(T)raceroute"),
        TR_NOOP("Sen(d) DM"),
        selectedIsFavorite ? TR_NOOP("Un(f)avorite") : TR_NOOP("(F)avorite"),
        TR_NOOP("Re(q)uest Node"),
#if defined(DEVICE_TDECK_PRO)
        TR_NOOP("(P)osition"),
#else
        TR_NOOP("Request (P)osition"),
#endif
        selectedIsIgnored ? TR_NOOP("Uni(g)nore") : TR_NOOP("I(g)nore"),
#if HAS_NODE_LOCATE
        TR_NOOP("(L)ocate"),
#endif
#if HAS_NODE_LOS
        TR_NOOP("LO(S)"),
#endif
#if NODES_INFO_AS_MODAL
        TR_NOOP("(N)ode Info"),
#endif
        TR_NOOP("S(h)are"),
        TR_NOOP("Del(e)te"),
#if HAS_ADMIN_TERMINAL
        TR_NOOP("(A)dmin"),
#endif
    };
    #endif
#if defined(DEVICE_TDECK_PRO)
    const lv_color_t rowTextColor = lv_color_make(0, 0, 0);
#else
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                    ? lv_color_hex(0x13233D)
                                    : lv_color_hex(0xD9E8FF);
#endif

#if HAS_MESSAGE_ACTIONS
    // MECP (issue #158): chat draws the decoded text, so the wire text the
    // sender actually transmitted is shown here, one scrolling line.
    if (s_nodesActionMsgMode) {
        char raw[MSG_CHARS * 4 + 8];
        if (mecpRawTextForPacket(packetId, raw, sizeof(raw))) {
            lv_obj_t *rawLbl = lv_label_create(s_nodesActionModal);
            lv_obj_set_width(rawLbl, lv_pct(100));
            lv_obj_set_style_text_font(rawLbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(rawLbl, rowTextColor, 0);
            lv_label_set_long_mode(rawLbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            setLabelTextEmojiSafe(rawLbl, raw);
        }
    }
#endif

    // ── Tapback strip (message mode only) ───────────────────────────────────
    // A single row of glyph cells above the action grid: six curated reactions
    // and a "..." that hands off to the full emoji tray. They are rows 0..6 of
    // the same selection space as the buttons below, so arrow keys walk
    // straight from the reactions into the actions with nothing special here.
    //
    // #if'd rather than left to the runtime flag: on a board without message
    // mode these cells can never be built, and this keeps the emoji labels and
    // their glyph lookups out of the image entirely.
#if HAS_MESSAGE_ACTIONS
    if (s_nodesActionMsgMode) {
        lv_obj_t *tapRow = lv_obj_create(s_nodesActionModal);
        lv_obj_set_width(tapRow, lv_pct(100));
        lv_obj_set_height(tapRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(tapRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(tapRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tapRow, 0, 0);
        lv_obj_set_style_pad_all(tapRow, 0, 0);
        lv_obj_set_style_pad_column(tapRow, 3, 0);
        lv_obj_set_flex_flow(tapRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tapRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        for (int i = 0; i <= kMsgActionMoreIdx; i++) {
            lv_obj_t *cell = lv_btn_create(tapRow);
#if defined(DEVICE_TDECK_PRO)
            lv_obj_remove_style_all(cell);
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
#endif
            s_nodesActionRows[i] = cell;
            lv_obj_set_flex_grow(cell, 1);
            lv_obj_set_height(cell, 28);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_shadow_width(cell, 0, 0);
            lv_obj_add_event_cb(cell, onNodesActionRowPressed, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);

            lv_obj_t *lbl = lv_label_create(cell);
            // Emoji face: montserrat has no glyphs for these and would draw tofu.
            lv_obj_set_style_text_font(lbl, emojiFont(&lv_font_montserrat_16), 0);
            lv_obj_set_style_text_color(lbl, rowTextColor, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            setLabelTextEmojiSafe(lbl, (i == kMsgActionMoreIdx) ? "..."
                                                                : kTapbackTray[i]);
            lv_obj_center(lbl);
        }
    }
#endif  // HAS_MESSAGE_ACTIONS

    lv_obj_t *grid = lv_obj_create(s_nodesActionModal);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 4, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    // In message mode the grid starts with Reply and then carries the same six
    // node actions; in node mode it is those six alone. The loop walks the
    // flat selection index so the row array stays aligned with it either way.
    const int gridFirst = s_nodesActionMsgMode ? kMsgActionReplyIdx : 0;
    const int gridLast  = activeActionCount() - 1;
    for (int i = gridFirst; i <= gridLast; i++) {
        const char *labelText;
        if (s_nodesActionMsgMode && i == kMsgActionReplyIdx) {
#if UI_TOUCH_ONLY_PROFILE
            labelText = TR_NOOP("Reply");
#else
            labelText = TR_NOOP("(R)eply");
#endif
        } else if (s_nodesActionMsgMode && i == kMsgActionInfoIdx) {
#if UI_TOUCH_ONLY_PROFILE
            labelText = TR_NOOP("Message Info");
#else
            labelText = TR_NOOP("Message (I)nfo");
#endif
        } else {
            // Message mode's node rows are not a straight offset — the map skips
            // Favorite, so the index has to go through it rather than be shifted.
            const int nodeIdx = s_nodesActionMsgMode
                                ? (int)kMsgActionNodeMap[i - kMsgActionNodeBase]
                                : i;
            labelText = kActionLabels[nodeIdx];
        }

        lv_obj_t *row = lv_btn_create(grid);
    #if defined(DEVICE_TDECK_PRO)
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    #endif
        s_nodesActionRows[i] = row;
        // pct(49) leaves a small gutter for pad_column between the two columns.
        lv_obj_set_width(row, lv_pct(49));
        lv_obj_set_height(row, 26);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onNodesActionRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, rowTextColor, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_label_set_text(lbl, TR(labelText));
        lv_obj_center(lbl);
    }

#if UI_TOUCH_ONLY_PROFILE
    // Touch-only board: no Backspace to dismiss with, and every row in here
    // *does* something to a node — so without a way out the only exits are
    // firing an action or leaving the modal on screen. Same corner X as every
    // other modal on this build.
    //
    // Gap rather than reserveHeltecCloseXRow(): the title wraps to two lines
    // for a long node name, and pinning it to the button's height would clip
    // the second one. The min-height keeps the one-line case from letting the
    // tapback strip (or the first grid row) slide up under the button.
    reserveHeltecCloseXGap(title);
    lv_obj_set_style_min_height(title, kHeltecCloseXSize, 0);
    {
        // Centre the first line against the button so the two read as one
        // title bar, the way reserveHeltecCloseXRow() does it.
        const int lineH = (int)lv_font_get_line_height(&lv_font_montserrat_12);
        const int padTop = (kHeltecCloseXSize - lineH) / 2;
        if (padTop > 0) lv_obj_set_style_pad_top(title, padTop, 0);
    }
    appendHeltecCloseX(s_nodesActionModal, onNodesActionClosePressed);
#endif

    refreshNodesActionMenuSelection();

    // A pending key change is answered here, before anything else is done to
    // the node: the review stacks over the menu, and closing it (or answering)
    // leaves the menu as it was.
    if (!s_nodesActionMsgMode && Nodes.hasKeyConflict(nodeId)) {
        openDestructiveConfirm(DESTRUCTIVE_CONFIRM_KEY_CHANGE, nodeId);
    }
}

// Nodes-screen entry point: unchanged behavior, sourcing the node from the
// highlighted list row.
static void openNodesActionMenu() {
    if (!s_nodesModal) return;
    // An archived row has no live peer, so every entry in the node menu —
    // traceroute, DM, favorite, request info — would be inert. It gets the one
    // action that applies instead, rather than a menu of things that silently
    // do nothing.
    const ArchivedNode *archived = currentNodesArchivedSelection();
    if (archived) {
        openDestructiveConfirm(DESTRUCTIVE_CONFIRM_NODE_RESTORE, archived->nodeId);
        return;
    }
    const NodeEntry *selected = currentNodesSelection();
    if (!selected || selected->nodeId == 0) return;
    openNodesActionMenuFor(selected->nodeId);
}

// Chat entry point: the same modal, in message mode. packetId is what the
// reactions and Reply act on, so message mode is refused without one — a system
// line has nothing to react to, and a reaction with no reply_id would go out as
// an ordinary one-glyph message.
static void openMessageActionMenu(uint32_t packetId, uint32_t senderNodeId) {
    if (senderNodeId == 0) return;
    // No message to act on: fall back to the plain node menu rather than showing
    // reaction rows that would silently do nothing. The mode is passed into the
    // builder rather than set around it — the builder's own closeNodesActionMenu()
    // clears these fields, so anything set beforehand would not survive.
    //
    // Message mode never engages where HAS_MESSAGE_ACTIONS is 0, so those boards
    // get the plain node menu from every entry point.
    const bool msgMode = HAS_MESSAGE_ACTIONS && packetId != 0;
    openNodesActionMenuFor(senderNodeId, msgMode, msgMode ? packetId : 0);
}

// ── Channel Actions modal ─────────────────────────────────────
static void refreshChannelActionsModal() {
    if (!s_channelActionsMuteLabel || !s_channelActionsMuteBtn) return;
    const bool muted = channelIsMuted(s_channelActionsChanIdx);
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text(s_channelActionsMuteLabel, muted ? "Unmute" : "Mute");
#else
    lv_label_set_text(s_channelActionsMuteLabel, muted ? TR("Un(m)ute") : TR("(M)ute"));
#endif

#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(s_channelActionsMuteBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_channelActionsMuteBtn, 1, 0);
    lv_obj_set_style_border_color(s_channelActionsMuteBtn, lv_color_make(0, 0, 0), 0);
#else
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t activeBg = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    // Muted state gets a warmer tint so the toggle reads at a glance.
    const lv_color_t mutedBg  = isLight ? lv_color_hex(0xF3E0C4) : lv_color_hex(0x6E4A18);
    lv_obj_set_style_bg_color(s_channelActionsMuteBtn, muted ? mutedBg : activeBg, 0);
    lv_obj_set_style_bg_opa(s_channelActionsMuteBtn, LV_OPA_COVER, 0);
#endif

    if (!s_channelActionsShareLabel || !s_channelActionsShareBtn) return;
    // The per-channel flag, not channelSharesLocation(): the button has to keep
    // showing what this channel is set to even while the global switch is what
    // is actually stopping the transmission. That case gets called out in the
    // label rather than a line of its own — on a 135px-tall panel this modal has
    // no room for a fifth child.
    const bool share = (s_channelActionsChanIdx >= 0 && s_channelActionsChanIdx < MESH_CHANNELS)
                       && CHANNEL_KEYS[s_channelActionsChanIdx].shareLocation;
    const char *shareState = !share            ? TR("Off")
                             : s_cfg.shareLocation ? TR("On")
                                                   : TR("On (global off)");
#if UI_TOUCH_ONLY_PROFILE
    lv_label_set_text_fmt(s_channelActionsShareLabel, TR("Location: %s"), shareState);
#else
    lv_label_set_text_fmt(s_channelActionsShareLabel, TR("(L)ocation: %s"), shareState);
#endif
    // Sharing is the state worth spotting at a glance, so it takes the
    // emphasised tint — but only when it is actually in effect: sharing the
    // global switch is suppressing is not the "on" state.
#if defined(DEVICE_TDECK_PRO)
    lv_obj_set_style_bg_opa(s_channelActionsShareBtn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_channelActionsShareBtn, 1, 0);
    lv_obj_set_style_border_color(s_channelActionsShareBtn, lv_color_make(0, 0, 0), 0);
#else
    const lv_color_t shareBg = isLight ? lv_color_hex(0xCFE9D6) : lv_color_hex(0x1F5A35);
    lv_obj_set_style_bg_color(s_channelActionsShareBtn,
                              (share && s_cfg.shareLocation) ? shareBg : activeBg, 0);
    lv_obj_set_style_bg_opa(s_channelActionsShareBtn, LV_OPA_COVER, 0);
#endif
}

static void toggleActiveChannelMute() {
    if (s_channelActionsChanIdx < 0 || s_channelActionsChanIdx >= MESH_CHANNELS) return;
    ChannelKey &ch = CHANNEL_KEYS[s_channelActionsChanIdx];
    ch.muted = !ch.muted;
    persistChannelsToPrefs();
    // Clear any pending attention glow the moment a channel is muted.
    if (ch.muted) s_channelNeedsAttention[s_channelActionsChanIdx] = false;
    refreshChannelActionsModal();
    refreshChannelGlow(true);
}

static void toggleActiveChannelShareLocation() {
    if (s_channelActionsChanIdx < 0 || s_channelActionsChanIdx >= MESH_CHANNELS) return;
    ChannelKey &ch = CHANNEL_KEYS[s_channelActionsChanIdx];
    ch.shareLocation = !ch.shareLocation;
    persistChannelsToPrefs();
    refreshChannelActionsModal();
}

static void onChannelActionMutePressed(lv_event_t *e) {
    LV_UNUSED(e);
    toggleActiveChannelMute();
}

static void onChannelActionSharePressed(lv_event_t *e) {
    LV_UNUSED(e);
    toggleActiveChannelShareLocation();
}

static void onChannelActionClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeChannelActionsModal();
}

