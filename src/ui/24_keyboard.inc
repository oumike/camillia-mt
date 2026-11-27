// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: keyboard and navigation input handling.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// The compose box's own keys: send, close, the two trays, delete, and plain
// characters typed into it. Compose opens over the chat screen, DMs and the
// Nodes list, and each of those used to carry its own copy of this switch --
// three, identical but for their comments, so a key added to one was a key
// missing from the other two.
// Left/Right move the caret in a text field, on a keyboard with arrow keys:
// the T-Display P4's expansion, whose arrows arrive as KEY_PREV_CHAN and
// KEY_NEXT_CHAN (kTloraTapMap). True when the key was one of them. Compose has
// its own copy in the caret block of pumpKeyboardInput(), shared with the d-pad
// boards; this is for every other field. Elsewhere those two keys keep
// whatever meaning each field already gave them.
static bool textFieldCaretKey(lv_obj_t *ta, char k) {
#if defined(DEVICE_TDISPLAY_P4)
    if (!ta) return false;
    if (k == KEY_PREV_CHAN) { lv_textarea_cursor_left(ta);  return true; }
    if (k == KEY_NEXT_CHAN) { lv_textarea_cursor_right(ta); return true; }
#else
    LV_UNUSED(ta);
    LV_UNUSED(k);
#endif
    return false;
}

static void handleComposeKey(char k) {
    switch (k) {
        case KEY_ENTER:
            sendComposeMessage();
            break;
        case KEY_ESCAPE:
            closeComposePrompt();
            break;
        case KEY_SYMBOL:
            // Mesh Deck's symbol key. Insert-mode tray, so the
            // picked character lands in the box being typed.
            openEmojiPicker(/*sendMode=*/false, /*symbolTray=*/true);
            break;
        // The Mesh Deck's dead bottom-left key. The T-Deck's mic
        // key reaches the same place without a case here: its
        // controller never reports it, so it is polled off the key
        // matrix in loop() instead of arriving as a key at all.
        case KEY_EMOJI_PICKER:
            // Insert mode: the glyph lands in the message being
            // typed rather than being sent on its own.
            openEmojiPicker(/*sendMode=*/false);
            break;
        case KEY_BACKSPACE:
        case KEY_BACKSPACE_HOLD:
            if (s_composeInput) {
                const char *cur = lv_textarea_get_text(s_composeInput);
#if UI_CARDPUTER_COMPACT
                if (cur && cur[0] && k == KEY_BACKSPACE) {
                    lv_textarea_delete_char(s_composeInput);
                }
#else
                if (!cur || !cur[0]) {
                    closeComposePrompt();
                } else if (k == KEY_BACKSPACE) {
                    lv_textarea_delete_char(s_composeInput);
                }
#endif
            }
            break;
#if HAS_BACK_KEY
        // The M9's dedicated Back button. It used to be indistinguishable from the
        // keyboard's Backspace — both arrived as KEY_BACKSPACE — so it deleted one
        // character at a time and there was no way for compose to tell them apart.
        // The driver now gives it its own code, so Back can mean "abandon this
        // draft": clear the box and close. Backspace keeps deleting a character.
        //
        // isBackspaceKey() still counts KEY_BACK_BTN, so everywhere outside compose
        // (filters, the channel and Wi-Fi text fields, modal close) Back behaves
        // exactly as it always did. Alt+Backspace raises it on the keyboards
        // that resolve an Alt of their own — see HAS_BACK_KEY in config.h.
        case KEY_BACK_BTN:
            if (s_composeInput) {
                lv_textarea_set_text(s_composeInput, "");
                closeComposePrompt();
            }
            break;
#endif
        default:
            if (k >= 0x20 && k < 0x7F && s_composeInput) {
                char one[2] = {k, '\0'};
                lv_textarea_add_text(s_composeInput, one);
            }
            break;
    }
}

static void pumpKeyboardInput() {
    for (int i = 0; i < 8; i++) {
        // Prioritize keyboard keys (especially Enter) before trackball deltas
        // to avoid one-off selection shifts during activation.
        char k = KEY_NONE;
        bool fromVnc = false;
#if HAS_VNC_HOST
        uint16_t vncKey = 0;
        if (vncHostPopKey(&vncKey) && vncKey <= 0xFF) {
            if (vncKey == '\r') vncKey = KEY_ENTER;
            if (vncKey == 0x7F) vncKey = KEY_BACKSPACE;
            k = (char)(uint8_t)vncKey;
            fromVnc = true;
        }
#endif
#if HAS_BLE_KEYBOARD
        // One more source of the same kind as the VNC queue above, popped
        // before the built-in keyboard rather than instead of it: the two are
        // additive, and neither has to know about the other. Everything
        // downstream -- modals, typing contexts, the j/k remap, screen wake --
        // sees an ordinary key.
        bool fromBle = false;
        if (!fromVnc && bleKeyboardPopKey(&k) && k != KEY_NONE) fromBle = true;
        if (!fromVnc && !fromBle) k = s_keyboard.readKey();
#else
        if (!fromVnc) k = s_keyboard.readKey();
#endif
        bool fromTrackball = false;
#if HAS_BLE_KEYBOARD
        const char *src = fromVnc ? "vnc" : (fromBle ? "ble" : "key");
#else
        const char *src = fromVnc ? "vnc" : "key";
#endif
#if defined(DEVICE_MESH_DECK)
        // D-pad presses arrive here as ordinary keys, so screen wake, the j/k
        // remap and every per-screen handler treat them identically.
        if (k == KEY_NONE) k = meshDeckReadDpadKey();
#endif
        if (k == KEY_NONE) {
            k = s_keyboard.readTrackball();
            src = "track";
            fromTrackball = true;
#if HAS_SCROLL_INVERT
            // Swapped here, at the one point trackball scroll enters the
            // pipeline, rather than in the twenty downstream branches that read
            // KEY_SCROLL_UP/DN. Everything past this — chat, lists, the
            // dropdown, the sliders — sees a single consistent direction, and
            // j/k are untouched because they never come through this call.
            if (s_cfg.invertScroll) {
                if (k == KEY_SCROLL_UP)      k = KEY_SCROLL_DN;
                else if (k == KEY_SCROLL_DN) k = KEY_SCROLL_UP;
            }
#endif
        }
        if (k == KEY_NONE) {
            if (s_cfgModal && s_cfgAwaitEnterRelease) {
                s_cfgAwaitEnterRelease = false;
                if (s_cfgDebugLog) Serial.println("[lvgl-cfg] enter-release observed");
            }
            break;
        }

#if defined(DEVICE_TDECK) && HAS_TRACKBALL && (TBALL_CLICK >= 0)
        if (k == KEY_ROLLER && s_tdeckSuppressRollerClick) {
            continue;
        }
#endif

#if FEATURE_LOCK_SCREEN
        if (s_lockScreenActive) {
            bool mayWake = fromVnc;
            if (fromTrackball) {
                // Match the dark-screen path below: motion is ignored and only
                // the wheel/trackball click is a deliberate wake gesture.
                mayWake = (k == KEY_ROLLER);
            } else if (!fromVnc) {
#if SCREEN_WAKE_FROM_KEYBOARD
                mayWake = true;
#else
                mayWake = false;
#endif
#if defined(DEVICE_M9)
                // The d-pad centre, and only held. This board gets the same
                // tap/hold split the wake buttons have without anything here
                // timing a keypress: the controller reports a held centre as
                // KEY_SLEEP_SCREEN on its own (0xA3) and a tap as KEY_ENTER, so
                // the two gestures arrive as two different keys. A tap is
                // swallowed like any other input the lock screen eats.
                mayWake = (k == KEY_SLEEP_SCREEN);
#endif
            }
            (void)tryExitLockScreenFromInput(millis(), mayWake);
            return;   // the wake gesture never also acts on the hidden UI
        }
#endif

        if (s_screenAsleep) {
            // Rolling the trackball should not wake the display.
            if (fromTrackball && k != KEY_ROLLER) {
                continue;
            }
#if !SCREEN_WAKE_FROM_KEYBOARD
            // Keyboard is not a wake gesture on this board (see
            // SCREEN_WAKE_FROM_KEYBOARD). Keys are still drained here so the
            // controller's buffer cannot back up while the screen is off; they
            // just don't wake it. KEY_ROLLER is the trackball click and arrives
            // via readTrackball(), so it is unaffected by this.
            if (!fromTrackball && !fromVnc) {
                continue;
            }
#endif
#if defined(DEVICE_M9)
            // Pocket guard. This board rides in a pocket with a bare keyboard
            // and six dedicated buttons facing outward, and with any key able to
            // wake it that turned into messages sent by accident: the first
            // press woke the screen and everything after it landed as real
            // input. Only the d-pad centre wakes it now.
            //
            // The centre click and the keyboard's Enter are the same byte on the
            // wire (0x0D — the controller cannot tell them apart), so Enter
            // wakes too. One key out of forty is the win here, not zero. Keys
            // are still drained rather than left to back up in the controller.
            //
            // VNC is exempt: a key arriving from a remote viewer was typed
            // deliberately by someone looking at the screen.
            // KEY_SLEEP_SCREEN is the same physical key: the driver reports a
            // held centre as that rather than as Enter, and someone pressing it
            // to wake the device should not have to guess how long to hold it.
            if (!fromVnc && k != KEY_ENTER && k != KEY_SLEEP_SCREEN) {
                continue;
            }
#if FEATURE_LOCK_SCREEN
            // Tap versus hold, exactly as on the wake buttons: a tap is worth
            // the lock screen and no more, and only a held centre
            // (KEY_SLEEP_SCREEN) goes through to the UI. Without this the
            // gesture that puts the device away -- a centre hold, which is
            // requestScreenOff() -- was undone by the lightest press of the same
            // key, which is the one thing a pocket reliably produces.
            //
            // With the lock screen switched off a tap does nothing at all and
            // the hold is the only way back, which is what the buttons do too.
            if (!fromVnc && k == KEY_ENTER) {
                if (s_cfg.lockScreenEnabled
                    && (int32_t)(millis() - s_screenWakeBlockedUntilMs) >= 0) {
                    wakeToLockScreen("M9 d-pad centre");
                    return;
                }
                continue;
            }
#endif
#endif
            if (!tryWakeScreenFromInput(millis())) {
                continue;
            }
            // The press that wakes the screen does nothing else — it is consumed
            // here. On the M9 this used to also run the global-navigation keys,
            // so a pocket press of Messages both woke the device and opened DMs,
            // which is the front half of the accident described above.
            return;
        }
        s_lastActivityMs = millis();

#if defined(DEVICE_CARDPUTER_ZERO)
        // Esc twice in quick succession goes Home. The first press still does
        // its ordinary back/close; only the second is turned into Home, so a
        // single Esc behaves exactly as before. (Held for 4 s instead, Esc
        // quits to the launcher -- linux/app/keys_linux.cpp.)
        if (k == KEY_ESCAPE) {
            static uint32_t s_lastEscMs = 0;
            const uint32_t nowMs = millis();
            if (s_lastEscMs && (uint32_t)(nowMs - s_lastEscMs) <= kZeroEscDoubleTapMs) {
                s_lastEscMs = 0;
                k = KEY_OPEN_HOME;
            } else {
                s_lastEscMs = nowMs;
            }
        }
#endif

#if HAS_RUNTIME_ORIENTATION && defined(DEVICE_TDISPLAY_P4)
        // Ahead of everything, the F-keys included: a countdown to a reboot
        // is not the moment for a key to open something behind it.
        if (kbLandscapePromptHandleKey(k)) continue;
#endif

#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_TLORA_PAGER_TFT) || UI_CARDPUTER_COMPACT \
    || UI_TANMATSU_LAYOUT
        // The Cardputer belongs on this list and was missing from it, which made
        // Alt+H a dead key on that board alone: altNavShortcut() raised
        // KEY_OPEN_HOME, nothing here caught it, and handleGlobalNavigationKey()
        // could not either -- its KEY_OPEN_HOME branch sits inside
        // #if HAS_HOME_DASHBOARD, and the Cardputer is the one board without a
        // dashboard. The !HAS_HOME_DASHBOARD half of openKeyboardHomeShortcut()
        // was written for exactly this board and had never been reachable.
        if (k == KEY_OPEN_HOME) {
            openKeyboardHomeShortcut();
            continue;
        }
#endif
#if defined(DEVICE_TDECK_PRO)
        if (k == KEY_TOGGLE_KB_BACKLIGHT) {
            toggleTdeckProKeyboardBacklight();
            continue;
        }
#elif defined(DEVICE_TDISPLAY_P4)
        if (k == KEY_TOGGLE_KB_BACKLIGHT) {
            // Saved on the toggle, as on the T-Deck Pro: there may be no clean
            // shutdown to save it at.
            s_cfg.kbBacklightEnabled = !s_cfg.kbBacklightEnabled;
            markConfigDirty();
            setPagerKeyboardBacklight(!s_screenAsleep);
            Serial.printf("[kb-bl] T-Display P4 keyboard light %s\n",
                          s_cfg.kbBacklightEnabled ? "on" : "off");
            continue;
        }
        if (k == KEY_KB_BACKLIGHT_STEP) {
            // To the next stop after the one it is at, wrapping: Low, Medium,
            // High, Off, Low... A level between stops (a hand-edited YAML)
            // steps to the first. Switches the light on if F7 had it off, since
            // stepping a light you cannot see would look like nothing happened.
            int at = -1;
            for (int i = 0; i < kKbBacklightLevelCount; i++) {
                if (kKbBacklightLevels[i].level == s_cfg.kbBacklightLevel) { at = i; break; }
            }
            const int next = (at + 1) % kKbBacklightLevelCount;
            s_cfg.kbBacklightLevel = kKbBacklightLevels[next].level;
            s_cfg.kbBacklightEnabled = true;
            markConfigDirty();
            setPagerKeyboardBacklight(!s_screenAsleep);
            Serial.printf("[kb-bl] T-Display P4 keyboard light level %u (%s)\n",
                          (unsigned)s_cfg.kbBacklightLevel,
                          kbBacklightLevelName(s_cfg.kbBacklightLevel));
            continue;
        }
#endif

        // Ahead of every modal's own key handling, which is what makes these
        // work from anywhere: a filter that swallows letters, a picker, or a
        // text field would otherwise consume the letter half of the chord. The
        // driver has already resolved Alt, so what arrives here is a
        // destination, never something that could be typed.
#if HAS_GLOBAL_NAV_SHORTCUTS
        if (handleGlobalNavigationKey(k)) continue;
#endif


        // Every modal that feeds keys into a textarea belongs here. Anything
        // left out has its j and k folded into KEY_SCROLL_UP/DN below and then
        // dropped by the printable-character branch, so those two letters
        // silently cannot be typed — which is exactly what happened to the WiFi
        // password and channel name/PSK fields.
        bool typingContext =
#if HAS_ADMIN_TERMINAL
            (s_adminModal != nullptr) ||
#endif
            (s_composeModal && !s_emojiPickerModal)
                             || (s_dmNodePickerModal && s_dmNodeFilterOpen)
                             || s_cfgWifiPassModal
                             || s_cfgNodeNameModal
                             || mecpFormNoteFocused()
                             || s_chanTextModal
                             || s_nodesFilterDialog
                             // Only once the theme filter is armed. Before that
                             // j/k must stay navigation, which is the whole
                             // reason the filter is armed with Space; after it,
                             // they have to be letters, because every dark
                             // theme's name ends in "Dark".
                             || (s_themeModal && s_themeFilterOpen)
                             || (s_onboardingModal
                                 && (s_onboardingStage == ONBOARD_STAGE_ENTER_LONG
                                     || s_onboardingStage == ONBOARD_STAGE_ENTER_SHORT));
        // The accent box, when one is up, gets first look at the keys that
        // step through it (Tab, the trackball/wheel, arrows) and Enter. It is
        // only ever up over a field being typed in.
        // The spell row likewise; it is only ever up in English, when the
        // accent box never is.
        if (typingContext && (accentHandleKey(k) || spellHandleKey(k))) continue;
#if defined(DEVICE_CARDPUTER_ZERO)
        // Outside text entry the number row is the P4's F-row: 1-6 follow the
        // nav bar (Home, Chat, DMs, Nodes, Tools, Config) and 0 is Help, as
        // F10 is there. 7-9 are unbound, as F7-F9 are on a board without a
        // keyboard light.
        if (!typingContext && k >= '0' && k <= '9') {
            const char fkey = zeroNumberRowKey(k);
            if (fkey == KEY_OPEN_HOME) { openKeyboardHomeShortcut(); continue; }
            if (fkey != KEY_NONE && handleGlobalNavigationKey(fkey)) continue;
        }
#endif
#if HAS_HOME_DASHBOARD
        // The glance carousel, on every board at once. KEY_PREV_CHAN and
        // KEY_NEXT_CHAN are what the T-Deck trackball's horizontal, the M9 and
        // Mesh Deck d-pads and the Cardputer's arrows already arrive as, so one
        // branch here is the whole non-touch story rather than four per-board
        // bindings.
        //
        // The vertical pair too, because the Pager has no horizontal control to
        // offer: its rotary wheel is the whole of its navigation and emits
        // KEY_SCROLL_UP/DN, and the block in keyboard.cpp that turns arrows into
        // KEY_PREV_CHAN/NEXT_CHAN is compiled out on that board
        // (`#if !defined(DEVICE_TLORA_PAGER_TFT)`). Free to take: nothing on
        // this surface scrolls, so the scroll keys had no other job here — and
        // until this existed the wheel was quietly scrolling the chat screen
        // still built underneath.
        //
        // j and k are matched as letters rather than left to remapJkUiKey()
        // below, which has not run yet at this point in the pipeline. Direction
        // follows this firmware's own convention, NOT vim's: j is up and so goes
        // back, k is down and so goes forward — the same way j/k fold onto
        // KEY_SCROLL_UP/DN everywhere else (see remapJkUiKey).
        //
        // Deliberately still ahead of the M9 fold below, which collapses
        // PREV/NEXT into KEY_SCROLL_UP/DN on any surface it does not consider
        // two-dimensional. The band is exactly that, and taking the keys here
        // leaves that list alone.
        //
        // Safe to take before the chat screen sees them: chat is not foreground
        // while the dashboard is up (see chatScreenIsForeground()), so the
        // channel these would otherwise switch is not one anyone is reading.
        //
        // Foreground, not merely alive. Only the chat screen underneath was
        // considered when this branch was written, and nothing accounted for a
        // full-screen view opened on *top* of a live dashboard — which is what
        // N and F do, since they reach their openers without passing through
        // prepareGlobalNavigation(). Guarded on existence alone, this took all
        // six navigation tokens away from Nodes and Config before either
        // handler could see one, and on the M9 those letters are the only route
        // in: neither screen has a button there, the board has no touch, and the
        // device boots onto the dashboard. Both screens simply would not scroll.
        if (homeDashboardIsForeground() && !typingContext) {
            // The cursor goes first: Up and Down put it on the weather or the
            // charts rather than turning the band, and Enter under it opens
            // what it is on instead of being the swallowed activation below.
            // See homeDashCursorKey().
            if (homeDashCursorKey(k)) continue;
            if (k == KEY_NEXT_CHAN || k == KEY_SCROLL_DN
                || k == 'k' || k == 'K') { homeDashCarouselGo(+1); continue; }
            if (k == KEY_PREV_CHAN || k == KEY_SCROLL_UP
                || k == 'j' || k == 'J') { homeDashCarouselGo(-1); continue; }
            // Activation does nothing here, and that is the whole behaviour.
            // The dashboard is a glance surface: no message is in front of you,
            // and the things on it that open -- the weather and the charts --
            // have already taken their Enter above, when the cursor was up.
            //
            // Swallowed rather than left to fall through, which is the bug.
            // Down in the chat fall-through these three drive the chat cursor
            // and the composer against the chat screen that is still built
            // underneath — so from the dashboard a press put a message box, or
            // Message Actions, over a conversation nobody was reading. Reported
            // on the M9, where the d-pad centre and Enter are the same byte on
            // the wire (0x0D) and the centre is the obvious thing to press; the
            // Pager's wheel click and Tab reach the same code the same way.
            //
            // This is Space's fix arriving through a different key — see the
            // Space branch in the chat fall-through, which says the same thing
            // at more length. Compose belongs to the chat screen.
            if (k == KEY_ENTER || k == KEY_ROLLER || k == KEY_TAB) continue;
        }
#endif

        bool navFromJk = false;
        // True only when the key really was j or k, for the few places that must
        // treat them differently from wheel/arrow input. navFromJk cannot be used
        // for this: the Cardputer parity rule below also raises it for the
        // physical arrows, and those must keep their own direction.
        bool jkDirectionInvert = false;

    #if defined(DEVICE_M9)
        // Keep left/right distinct only where the visible surface is genuinely
        // multi-column. Everywhere else the pad remains paired vertical nav:
        // Up/Left move up, Down/Right move down.
        const bool m9FourWaySelection = s_emojiPickerModal
                         || s_cfgColorModal
                         || s_fontSizeModal
                         || s_alertSoundModal
                         || (s_chanCfgModal && !s_chanEditModal)
                         || (s_chanEditModal && !s_chanTextModal)
                         || s_timeCfgModal
                         || s_liveToolsModal
                         // Node / Message Actions: a two-column grid, plus
                         // the tapback strip above it in message mode.
                         || s_nodesActionModal
                         // MECP form: Left/Right change a dropdown or move
                         // between the buttons.
                         || s_mecpFormModal
                         // A slider picker with a checkbox under it (Sweep/Scan
                         // and MQTT Scan Settings): two rows, the same shape as
                         // Brightness just below.
                         || (s_cfgSliderModal && s_cfgSliderSpec
                             && s_cfgSliderSpec->checkValue)
                         // Not multi-column, but it has two rows and the pad's
                         // Up/Down is what moves between them — so the value
                         // needs Left/Right, which the fold would eat.
                         || s_cfgBrightModal
                         // The Config section grid: two or three columns, so
                         // Up/Down move a row and Left/Right a button
                         // (CFG_GRID_FOUR_WAY). Once a section is open its
                         // rows are one list and the fold applies again.
                         || (s_cfgModal && cfgGridShown());
        // Compose is exempt too: it drives a caret, so it wants all four
        // directions. Collapsing here is irreversible — it happens before
        // `rawKey` is taken below, so no handler downstream can tell Left from
        // Up once it has run.
        //
        // Scoped to compose rather than the whole of typingContext, and that is
        // deliberate. The other typing contexts do NOT ignore the collapsed
        // keys the way it looks: the DM node picker with its filter armed, the
        // WiFi password modal and the armed theme filter all act on
        // KEY_SCROLL_UP/DN and none of them handle KEY_PREV_CHAN/KEY_NEXT_CHAN.
        // Exempting them would therefore take Left/Right away as a navigation
        // alias and give nothing back, since they have no caret to move yet.
        // Widen this to typingContext at the point their carets get wired.
        const bool m9ComposeCaret = (s_composeModal && !s_emojiPickerModal);
        // The Locate map pans in all four directions; folded, Left panned it
        // up and Right down.
#if HAS_NODE_LOCATE
        const bool m9MapPan = s_nodeLocateModal != nullptr;
#else
        const bool m9MapPan = false;
#endif
        if (!m9FourWaySelection && !m9ComposeCaret && !m9MapPan) {
            if (k == KEY_PREV_CHAN)      k = KEY_SCROLL_UP;
            else if (k == KEY_NEXT_CHAN) k = KEY_SCROLL_DN;
        }
    #endif

        char rawKey = k;

#if UI_CARDPUTER_COMPACT
        // Match v1 Cardputer shortcuts: ';' / '.' navigate lists, and
        // the key physically labeled '`' acts as Escape to close modals.
        k = remapCardputerUiKey(k, !typingContext);
#endif

#if !UI_TOUCH_ONLY_PROFILE || defined(DEVICE_MESH_DECK) || CZERO_DESKTOP
        navFromJk = (k == 'j' || k == 'J' || k == 'k' || k == 'K');
        jkDirectionInvert = navFromJk;
        // Enable vim-style j/k navigation for all keyboard-capable builds.
        k = remapJkUiKey(k, !typingContext);
#endif

#if UI_CARDPUTER_COMPACT || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9)
    // Cardputer parity rule: physical Up/Down arrows must behave exactly
    // like j/k anywhere j/k are used for navigation, so all downstream
    // navFromJk direction branches apply the same way.
    //
    // Mesh Deck and M9 need it for the same reason: their physical D-pads arrive
    // as scroll keys in one-dimensional contexts and must keep j/k direction.
    if (!typingContext && (rawKey == KEY_SCROLL_UP || rawKey == KEY_SCROLL_DN)) {
        navFromJk = true;
    }
#endif

#if defined(DEVICE_MESH_DECK) && defined(MESH_DECK_TOUCH_TRACE)
        // Same discipline as the touch trace: log the key at the boundary
        // rather than reasoning about which handler should have claimed it.
        // Prints the code actually seen after every remap, so a key that never
        // arrives looks different from one that arrives and is ignored.
        if (k != KEY_NONE) {
            Serial.printf("[key-trace] k=0x%02X '%c' jk=%d dropdown=%d compose=%d\n",
                          (unsigned)(uint8_t)k,
                          (k >= 0x20 && k < 0x7F) ? k : '.',
                          navFromJk ? 1 : 0,
                          isChannelDropdownVisible() ? 1 : 0,
                          s_composeModal ? 1 : 0);
        }
#endif

        const bool invertScrollNav = kPagerWheelChatNav && !navFromJk;

        // First-boot onboarding is fully modal: swallow every other UI key
        // while it's up so the user can't wander into the main app before
        // supplying a node identity.
        // The action popup is drawn above everything, so it takes keys before any
        // screen underneath it — otherwise whatever it covers keeps consuming
        // input and the popup cannot be dismissed at all. Any key closes it.
        if (s_cfgActionMsgModal) {
            const uint32_t nowMs = millis();
            // Ignore repeats from the keypress that raised it.
            if ((uint32_t)(nowMs - s_cfgActionMsgOpenedMs) >= 300UL) {
                closeCfgActionMessageModal();
            }
            continue;
        }

        if (s_onboardingModal && !s_cfgWifiModal && !s_cfgWifiScanModal && !s_cfgWifiPassModal) {
#if ONBOARD_BUTTON_FOCUS
            if (onboardingButtonFocusKey(k)) continue;
            if (s_onboardingStage == ONBOARD_STAGE_CHOOSE_WIFI) {
                if (k == 'w' || k == 'W') { openCfgWifiPickerModal(true); continue; }
                if (k == 'f' || k == 'F') { onboardingFinalize(); continue; }
            }
            if (s_onboardingStage == ONBOARD_STAGE_ENTER_SHORT && k == KEY_ESCAPE) {
                s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
                renderOnboardingStage();
                continue;
            }
#endif
            if (s_onboardingStage == ONBOARD_STAGE_ASK_IMPORT) {
                if (k == 'y' || k == 'Y' || k == KEY_ENTER) {
                    onboardingAcceptImport();
                } else if (k == 'n' || k == 'N' || isModalCloseKey(k)) {
                    onboardingDeclineImport();
                }
            } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION
                       || s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
                // Region / role picker: wheel or j/k cycle, Enter/click advances,
                // a close key steps back.
                if (k == KEY_ENTER || k == KEY_ROLLER) {
                    onboardingPickerAdvance();
                } else if (k == KEY_SCROLL_UP) {
                    onboardingPickerStep(-1);
                } else if (k == KEY_SCROLL_DN) {
                    onboardingPickerStep(1);
                } else if (isModalCloseKey(k)) {
                    onboardingPickerBack();
                }
            } else if (s_onboardingStage == ONBOARD_STAGE_CHOOSE_WIFI) {
                if (k == 'n' || k == 'N' || k == KEY_ROLLER) {
                    openCfgWifiPickerModal(true);
                } else if (k == KEY_ENTER) {
                    onboardingFinalize();
                } else if (isModalCloseKey(k)) {
                    s_onboardingStage = ONBOARD_STAGE_SELECT_ROLE;
                    s_onboardingPickIndex = onboardingRoleIndex(s_onboardingRoleScratch);
                    renderOnboardingStage();
                }
            } else {  // text stages: LONG / SHORT
                if (k == KEY_ENTER) {
                    onboardingCommitName();
                } else if (isBackspaceKey(k)) {
                    if (s_onboardingInput && k == KEY_BACKSPACE) {
                        const char *cur = lv_textarea_get_text(s_onboardingInput);
                        const bool emptyField = (!cur || !cur[0]);
                        // Backspace on an empty field steps back to the prior stage.
                        if (emptyField
                            && s_onboardingStage == ONBOARD_STAGE_ENTER_SHORT) {
                            s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
                            renderOnboardingStage();
                        } else {
                            lv_textarea_delete_char(s_onboardingInput);
                        }
                    }
                } else if (textFieldCaretKey(s_onboardingInput, k)) {
                    // The caret moved; nothing else to do.
                } else if (k >= 0x20 && k < 0x7F && s_onboardingInput) {
                    char one[2] = {k, '\0'};
                    lv_textarea_add_text(s_onboardingInput, one);
                }
            }
            continue;
        }

#if HAS_STATE_MAPS
        if (s_legacyMapPromptModal) {
            if (rawKey == 'r' || rawKey == 'R') {
                removeLegacyMapFiles();
            } else if (rawKey == 'k' || rawKey == 'K' || rawKey == 'n' || rawKey == 'N'
                       || k == KEY_ENTER || isModalCloseKey(k)) {
                keepLegacyMapFiles();
            }
            continue;
        }
#endif

#if HAS_BLE_KEYBOARD
        // Lost keyboard pairing. Ignore is preselected and is what Enter
        // answers unless the user moved to Re-pair first; arrows/d-pad move.
        if (s_bleKbdRepairModal) {
            if (rawKey == 'r' || rawKey == 'R') {
                bleKbdRepairAccept();
            } else if (rawKey == 'i' || rawKey == 'I' || rawKey == 'n' || rawKey == 'N'
                       || isModalCloseKey(k)) {
                bleKbdRepairIgnore();
            } else if (k == KEY_ENTER || k == KEY_ROLLER) {
                if (s_bleKbdRepairSel == 0) bleKbdRepairAccept();
                else bleKbdRepairIgnore();
            } else if (k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN || k == KEY_SCROLL_UP
                       || k == KEY_SCROLL_DN || k == KEY_TAB) {
                bleKbdRepairMoveSelection();
            }
            continue;
        }
#endif

        // The boot update offer is modal on the same terms as the CFG
        // confirmation below. Declining just closes it; it will not come back
        // until the next reboot.
        if (s_otaPromptModal) {
            if (k == 'y' || k == 'Y' || k == KEY_ENTER) {
                otaPromptAccept();
            } else if (k == 'n' || k == 'N' || isModalCloseKey(k)) {
                otaPromptDecline();
            }
            continue;
        }

        // Ahead of the WiFi picker below, which would otherwise keep consuming
        // keys while its own delete prompt is on top of it.
        if (s_cfgWifiDelModal) {
            if (k == 'y' || k == 'Y' || k == KEY_ENTER) {
                cfgWifiDeleteConfirmAccept();
            } else if (k == 'n' || k == 'N' || isBackspaceKey(k) || isModalCloseKey(k)) {
                cfgWifiDeleteConfirmReject();
            }
            continue;
        }

        // The CFG confirmation dialog is modal: Y or Enter confirms, N/close
        // cancels, and every other shortcut is swallowed while it's up.
        if (s_cfgConfirmModal) {
            // Scroll first, answers second. j/k arrive here already folded onto
            // KEY_SCROLL_UP/DN by remapJkUiKey() further up, so naming the
            // tokens covers the d-pad, the arrows and the letters at once --
            // the same set the release-notes reader takes.
            // Clear Nodes first: its question is one line with nothing to
            // scroll, so up/down step between its two checkboxes instead.
            if (cfgClearNodesOptionKey(k)) continue;
            if (k == KEY_SCROLL_UP || k == KEY_PAGE_UP || k == KEY_PREV_CHAN) {
                scrollListClamped(s_cfgConfirmScroll, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN || k == KEY_PAGE_DN || k == KEY_NEXT_CHAN) {
                scrollListClamped(s_cfgConfirmScroll, -18);
                continue;
            }
            if (s_cfgConfirmAltFn && s_cfgConfirmAltKey
                && tolower((unsigned char)k) == s_cfgConfirmAltKey) {
                cfgConfirmAlt();
                continue;
            }
            if (k == 'y' || k == 'Y' || k == KEY_ENTER) {
                cfgConfirmAccept();
            } else if (k == 'n' || k == 'N' || isModalCloseKey(k)) {
                cfgConfirmReject();
            }
            continue;
        }

        if (s_tracerouteModal) {
#if UI_CARDPUTER_COMPACT
            if (k == KEY_ESCAPE || isBackspaceKey(k)) {
#else
            if (isModalCloseKey(k)) {
#endif
                closeTracerouteProgressModal();
            }
            continue;
        }

        // Brightness slider: j/k (and the scroll/channel keys) move by one 10%
        // step, Enter saves, a close key restores the level we opened with.
        if (s_cfgBrightModal) {
            if (isModalCloseKey(k)) {
                cancelCfgBrightness();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyCfgBrightness();
                continue;
            }
            int steps = 0;
#if FEATURE_LOCK_SCREEN
            // Wheel and d-pad move between the two rows on the boards where
            // that is the only vertical gesture there is (kBrightVertPicksRow).
            // j/k fold onto these same codes and must keep adjusting, which is
            // what jkDirectionInvert separates: it is raised only when the key
            // really was j or k, never for wheel or pad input.
            if (kBrightVertPicksRow && !jkDirectionInvert
                && (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN)) {
                // Same sense as every other list on the board, wheel inversion
                // included: positive moves down, onto the lock row.
                const int rowDelta = (k == KEY_SCROLL_UP) ? (invertScrollNav ? 1 : -1)
                                                          : (invertScrollNav ? -1 : 1);
                int nextRow = s_cfgBrightFocus + rowDelta;
                if (nextRow < 0) nextRow = 0;
                if (nextRow > 1) nextRow = 1;
                if (nextRow != s_cfgBrightFocus) {
                    s_cfgBrightFocus = nextRow;
                    refreshCfgBrightnessFocus();
                }
                continue;
            }
#endif
            // j/k reach here already remapped to KEY_SCROLL_UP/DN by
            // remapJkUiKey(), so the literal cases below only fire on builds
            // where that remap is compiled out. navFromJk is what tells the two
            // apart, and here they run opposite to the wheel: j dims, k brightens.
            if (k == 'j' || k == 'J')            steps = -1;   // dimmer
            else if (k == 'k' || k == 'K')       steps = 1;    // brighter
            else if (k == KEY_SCROLL_UP)         steps = (invertScrollNav || navFromJk) ? -1 : 1;
            else if (k == KEY_SCROLL_DN)         steps = (invertScrollNav || navFromJk) ? 1 : -1;
            else if (k == KEY_PAGE_UP)           steps = 1;
            else if (k == KEY_PAGE_DN)           steps = -1;
#if FEATURE_LOCK_SCREEN
            else if (k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN) {
                if (kBrightVertPicksRow) {
                    // Vertical is spoken for on these boards, so left/right is
                    // what is left to adjust with. Only the M9 reaches this —
                    // the Pager has no left/right and adjusts with j/k.
                    steps = (k == KEY_NEXT_CHAN) ? 1 : -1;
                } else {
                    // Left/right pick the row rather than nudging the value.
                    // They were a third alias for adjust, which j/k and the page
                    // keys already cover; a second row needed a way to be
                    // reached more than it needed that.
                    s_cfgBrightFocus = (k == KEY_NEXT_CHAN) ? 1 : 0;
                    refreshCfgBrightnessFocus();
                    continue;
                }
            }
#else
            else if (k == KEY_NEXT_CHAN)         steps = 1;
            else if (k == KEY_PREV_CHAN)         steps = -1;
#endif
            stepCfgBrightness(steps);
            continue;
        }

        // Battery trim slider: same keys as brightness, one 0.5% step each.
        if (s_cfgBattCalModal) {
            if (isModalCloseKey(k)) {
                cancelCfgBattCal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyCfgBattCal();
                continue;
            }
            int steps = 0;
            if (k == 'j' || k == 'J')            steps = -1;   // reads lower
            else if (k == 'k' || k == 'K')       steps = 1;    // reads higher
            else if (k == KEY_SCROLL_UP)         steps = (invertScrollNav || navFromJk) ? -1 : 1;
            else if (k == KEY_SCROLL_DN)         steps = (invertScrollNav || navFromJk) ? 1 : -1;
            else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_UP) steps = 1;
            else if (k == KEY_PREV_CHAN || k == KEY_PAGE_DN) steps = -1;
            stepCfgBattCal(steps);
            continue;
        }

#if HAS_VOLUME_CONTROL
        // Volume slider: same keys and the same j-left / k-right sense as
        // brightness. The Cardputer's arrows arrive as KEY_SCROLL_UP/DN and
        // KEY_PREV_CHAN/KEY_NEXT_CHAN (see cardputerMapHidKey), so left/right
        // and up/down both work without special-casing the board.
        if (s_cfgVolModal) {
            if (isModalCloseKey(k)) {
                cancelCfgVolume();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyCfgVolume();
                continue;
            }
            int steps = 0;
            if (k == 'j' || k == 'J')            steps = -1;   // quieter
            else if (k == 'k' || k == 'K')       steps = 1;    // louder
            else if (k == KEY_SCROLL_UP)         steps = (invertScrollNav || navFromJk) ? -1 : 1;
            else if (k == KEY_SCROLL_DN)         steps = (invertScrollNav || navFromJk) ? 1 : -1;
            else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_UP) steps = 1;
            else if (k == KEY_PREV_CHAN || k == KEY_PAGE_DN) steps = -1;
            stepCfgVolume(steps);
            continue;
        }
#endif

        // MECP form (Tools -> MECP). Below the confirmation, which stands over
        // it for a MAYDAY.
        if (s_mecpFormModal) {
            mecpFormHandleKey(k, invertScrollNav);
            continue;
        }

        // Ordered-list slider picker (location precision, light timeout): same
        // shape as the volume slider above.
        if (s_cfgSliderModal) {
            if (isModalCloseKey(k)) {
                cancelCfgSliderAndRefresh();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyCfgSlider();
                continue;
            }
            if (k == ' ') {
                toggleCfgSliderCheck();   // no-op when the picker has no checkbox
                continue;
            }
            // Only the checkbox to change, so every direction toggles it: the
            // wheel boards have no other way to reach it.
            if (cfgSliderCheckOnly(s_cfgSliderSpec)) {
                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN
                    || k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN) {
                    toggleCfgSliderCheck();
                }
                continue;
            }
#if UI_ARROW_GRID_NAV
            // With a checkbox the pad's Up/Down pick between it and the slider,
            // and Left/Right change whichever is picked -- the Brightness
            // screen's grammar. j/k fold onto the same codes and keep adjusting
            // the slider; jkDirectionInvert is what tells them apart.
            if (s_cfgSliderSpec && s_cfgSliderSpec->checkValue) {
                if (!jkDirectionInvert && (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN)) {
                    const int next = (k == KEY_SCROLL_DN) ? 1 : 0;
                    if (next != s_cfgSliderFocus) {
                        s_cfgSliderFocus = next;
                        refreshCfgSliderFocus();
                    }
                    continue;
                }
                if (s_cfgSliderFocus == 1 && (k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN)) {
                    toggleCfgSliderCheck();
                    continue;
                }
            }
#endif
            int steps = 0;
            if (k == 'j' || k == 'J')            steps = -1;
            else if (k == 'k' || k == 'K')       steps = 1;
            else if (k == KEY_SCROLL_UP)         steps = (invertScrollNav || navFromJk) ? -1 : 1;
            else if (k == KEY_SCROLL_DN)         steps = (invertScrollNav || navFromJk) ? 1 : -1;
            else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_UP) steps = 1;
            else if (k == KEY_PREV_CHAN || k == KEY_PAGE_DN) steps = -1;
            stepCfgSlider(steps);
            continue;
        }

        // Own-message color picker: arrows/scroll move through the 16-swatch
        // grid (one step, or a whole row via page/channel keys), Enter applies
        // and reboots, a close key cancels.
        if (s_cfgColorModal) {
            if (isModalCloseKey(k)) {
                closeCfgColorPickerModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgColorAsk(s_cfgColorSelection);
                continue;
            }
            int next = s_cfgColorSelection;
#if UI_ARROW_GRID_NAV
            if (k == KEY_SCROLL_UP) {
                if (next > 0) next = (next <= kMsgColorCols) ? 0 : next - kMsgColorCols;
            } else if (k == KEY_SCROLL_DN) {
                if (next == 0) next = 1;
                else if (next + kMsgColorCols < kUserMsgColorNavCount) next += kMsgColorCols;
            } else if (k == KEY_PREV_CHAN) {
                if (next > 1 && ((next - 1) % kMsgColorCols) != 0) next--;
            } else if (k == KEY_NEXT_CHAN) {
                if (next == 0) next = 1;
                else if (((next - 1) % kMsgColorCols) != kMsgColorCols - 1
                         && next + 1 < kUserMsgColorNavCount) next++;
            }
#else
            int delta = 0;
            if (k == KEY_SCROLL_UP)          delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN)     delta = invertScrollNav ? -1 : 1;
            // A row, whatever a row is on this board (kMsgColorCols).
            else if (k == KEY_PAGE_UP || k == KEY_PREV_CHAN) delta = -kMsgColorCols;
            else if (k == KEY_PAGE_DN || k == KEY_NEXT_CHAN) delta = kMsgColorCols;
            next += delta;
            if (next < 0) next = 0;
            if (next >= kUserMsgColorNavCount) next = kUserMsgColorNavCount - 1;
#endif
            if (next != s_cfgColorSelection) {
                s_cfgColorSelection = next;
                refreshCfgColorPickerModal();
            }
            continue;
        }

        if (s_themeModal) {
            // Backspace edits the filter before it closes the modal — it is the
            // close key on some builds, so this has to come first, exactly as it
            // does on the CFG and Nodes screens. An empty filter disarms, and
            // the next press closes.
            if (isBackspaceKey(k) && s_themeFilterOpen) {
                const size_t len = strlen(s_themeFilter);
                if (len > 0) {
                    s_themeFilter[len - 1] = '\0';
                } else {
                    s_themeFilterOpen = false;
                }
                rebuildThemeRows();
                continue;
            }
            if (isModalCloseKey(k)) {
                closeThemeModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgThemeAsk(s_themeSelection);
                continue;
            }
            // Space arms the filter and is never part of it. Until it is armed
            // letters are left alone, so j/k keep navigating — which is the
            // point of arming at all on a board with no trackball.
            if (k == ' ') {
                if (!s_themeFilterOpen) {
                    s_themeFilterOpen = true;
                    rebuildThemeRows();
                }
                continue;
            }
            // Once armed the modal is a typing context (see typingContext
            // above), so j and k arrive as letters rather than scroll keys —
            // necessary, since every dark theme's name ends in "Dark". The
            // wheel, trackball, D-pad and arrows come in as KEY_SCROLL_* and
            // navigate either way.
            if (k > 0x20 && k < 0x7F && s_themeFilterOpen) {
                const size_t len = strlen(s_themeFilter);
                if (len + 1 < sizeof(s_themeFilter)) {
                    s_themeFilter[len] = k;
                    s_themeFilter[len + 1] = '\0';
                    rebuildThemeRows();
                }
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0 && s_themeVisibleCount > 0) {
                // Round robin: past the last theme comes the first. With
                // twenty-nine of them the end of the list is a long way from
                // the start, and clamping made the last few feel unreachable
                // from the top.
                int next = s_themeSelection + delta;
                if (next < 0) next = s_themeVisibleCount - 1;
                else if (next >= s_themeVisibleCount) next = 0;
                if (next != s_themeSelection) {
                    s_themeSelection = next;
                    refreshThemeSelection();
                }
            }
            continue;
        }

        if (s_chatStyleModal) {
            if (isModalCloseKey(k)) {
                closeChatStyleModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyChatStyleSelection(s_chatStyleSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = chatStyleStepSelection(s_chatStyleSelection, delta);
                if (next != s_chatStyleSelection) {
                    s_chatStyleSelection = next;
                    refreshChatStyleSelection();
                }
            }
            continue;
        }

        // Time and Date: up/down picks a field, left/right changes it, and
        // Enter saves from any row. Touch builds use the footer action row.
        if (s_timeCfgModal) {
            if (isModalCloseKey(k)) {
                closeTimeCfgModal();   // discards staged edits
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                timeCfgSave();
                continue;
            }
            if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) {
                timeCfgAdjust(s_timeCfgSelection, -1);
                continue;
            }
            if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) {
                timeCfgAdjust(s_timeCfgSelection, 1);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                // Automatic mode hides the date/time rows, so stepping has to
                // pass over them rather than land on a hidden line.
                int next = s_timeCfgSelection;
                for (int guard = 0; guard < TIME_CFG_ROW_COUNT; guard++) {
                    next += delta;
                    if (next < 0 || next >= TIME_CFG_ROW_COUNT) { next = s_timeCfgSelection; break; }
                    if (timeCfgRowVisible(next)) break;
                }
                if (next != s_timeCfgSelection) {
                    s_timeCfgSelection = next;
                    refreshTimeCfgRows();
                }
            }
            continue;
        }

        // Channel config modals, innermost first: text entry sits on top of the
        // field editor, which sits on top of the slot picker.
        if (s_chanTextModal) {
            if (k == KEY_ENTER) {
                commitChanTextModal();
                continue;
            }
            if (isBackspaceKey(k)) {
                if (s_chanTextInput && k == KEY_BACKSPACE) {
                    const char *cur = lv_textarea_get_text(s_chanTextInput);
                    // Backspace on an empty field backs out, matching the WiFi
                    // password modal. A cleared name is committed with Enter.
                    if (!cur || !cur[0]) closeChanTextModal();
                    else                 lv_textarea_delete_char(s_chanTextInput);
                }
                continue;
            }
            if (isModalCloseKey(k)) {
                closeChanTextModal();
                continue;
            }
            if (textFieldCaretKey(s_chanTextInput, k)) continue;
            if (k >= 0x20 && k < 0x7F && s_chanTextInput) {
                char one[2] = {k, '\0'};
                lv_textarea_add_text(s_chanTextInput, one);
                continue;
            }
            continue;
        }

        if (s_chanEditModal) {
            if (isModalCloseKey(k)) {
                closeChanEditModal();   // discards staged edits
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                chanEditActivateRow(s_chanEditSelection);
                continue;
            }
            // Left/right change the value in place on the rows that have one
            // to cycle, so none of them needs a sub-modal. Both consume the key, so
            // the column hop below is unreachable from these rows — the same
            // trade the encryption row has always made.
            if (s_chanEditSelection == CHAN_EDIT_ENC) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP)  { chanEditCycleEncType(-1); continue; }
                if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN)  { chanEditCycleEncType(1);  continue; }
            }
            if (s_chanEditSelection == CHAN_EDIT_LOCATION) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP
                    || k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) { chanEditToggleShareLoc(); continue; }
            }
            if (s_chanEditSelection == CHAN_EDIT_AEAD) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP
                    || k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) { chanEditToggleAead(); continue; }
            }
            if (s_chanEditSelection == CHAN_EDIT_HOPS) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP)  { chanEditCycleHops(-1); continue; }
                if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN)  { chanEditCycleHops(1);  continue; }
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            // Rows run down one column and into the next, so a step of one is
            // already a vertical move; left/right hop between the columns.
            if (delta == 0 && kChanModalCols > 1) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) {
                    delta = -kChanEditRowsPerCol;
                } else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) {
                    delta = kChanEditRowsPerCol;
                }
            }
            if (delta != 0) {
                int next = s_chanEditSelection + delta;
                if (next < 0) next = 0;
                if (next >= CHAN_EDIT_ROW_COUNT) next = CHAN_EDIT_ROW_COUNT - 1;
                if (next != s_chanEditSelection) {
                    s_chanEditSelection = next;
                    refreshChanEditRows();
                }
            }
            continue;
        }

        if (s_chanCfgModal) {
            if (isModalCloseKey(k)) {
                closeChanCfgModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                openChanEditModal(s_chanCfgSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            // Slots run down one column and into the next, so a step of one is
            // already a vertical move; left/right hop between the columns. The
            // wheel-only Pager still reaches every slot with scroll alone.
            if (delta == 0 && kChanModalCols > 1) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) {
                    delta = -kChanModalRowsPerCol;
                } else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) {
                    delta = kChanModalRowsPerCol;
                }
            }
            if (delta != 0) {
                int next = s_chanCfgSelection + delta;
                if (next < 0) next = 0;
                if (next >= MESH_CHANNELS) next = MESH_CHANNELS - 1;
                if (next != s_chanCfgSelection) {
                    s_chanCfgSelection = next;
                    refreshChanCfgSelection();
                }
            }
            continue;
        }

#if HAS_NOTIFY_LED
        if (s_notifyLedColorModal) {
            if (isModalCloseKey(k)) {
                closeNotifyLedColorModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyNotifyLedColorSelection(s_notifyLedColorSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)           delta = invertScrollNav ? kNotifyLedColorCols : -kNotifyLedColorCols;
            else if (k == KEY_SCROLL_DN)      delta = invertScrollNav ? -kNotifyLedColorCols : kNotifyLedColorCols;
            else if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) delta = -1;
            else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) delta = 1;
            if (delta != 0) {
                int next = s_notifyLedColorSelection + delta;
                if (next < 0) next = 0;
                if (next >= kNotifyLedColorOptionCount) next = kNotifyLedColorOptionCount - 1;
                if (next != s_notifyLedColorSelection) {
                    s_notifyLedColorSelection = next;
                    refreshNotifyLedColorSelection();
                }
            }
            continue;
        }
#endif

        if (s_alertSoundModal) {
            if (isModalCloseKey(k)) {
                cancelAlertSoundModal();   // undo any previewed change
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyAlertSoundSelection(s_alertSoundSelection);
                continue;
            }
            int next = s_alertSoundSelection;
#if UI_ARROW_GRID_NAV
            if (k == KEY_SCROLL_UP && next >= 2) next -= 2;
            else if (k == KEY_SCROLL_DN && next + 2 <= MSG_ALERT_SOUND_MAX) next += 2;
            else if (k == KEY_PREV_CHAN && (next % 2) != 0) next--;
            else if (k == KEY_NEXT_CHAN && (next % 2) == 0 && next < MSG_ALERT_SOUND_MAX) next++;
#else
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            next += delta;
            if (next < 0) next = 0;
            if (next > MSG_ALERT_SOUND_MAX) next = MSG_ALERT_SOUND_MAX;
#endif
            if (next != s_alertSoundSelection) previewAlertSoundSelection(next);
            continue;
        }

        if (s_chatNameModal) {
            if (isModalCloseKey(k)) {
                closeChatNameModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyChatNameSelection(s_chatNameSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_chatNameSelection + delta;
                if (next < 0) next = 0;
                if (next > CHAT_NAME_MAX) next = CHAT_NAME_MAX;
                if (next != s_chatNameSelection) {
                    s_chatNameSelection = next;
                    refreshChatNameSelection();
                }
            }
            continue;
        }

        if (s_fontSizeModal) {
            if (isModalCloseKey(k)) {
                closeFontSizeModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyFontSizeSelection(s_fontSizeSelection);
                continue;
            }
            int next = s_fontSizeSelection;
#if UI_ARROW_GRID_NAV
            if (k == KEY_SCROLL_UP && next >= 2) next -= 2;
            else if (k == KEY_SCROLL_DN && next + 2 <= FONT_SIZE_MAX) next += 2;
            else if (k == KEY_PREV_CHAN && (next % 2) != 0) next--;
            else if (k == KEY_NEXT_CHAN && (next % 2) == 0 && next < FONT_SIZE_MAX) next++;
#else
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            next += delta;
            if (next < 0) next = 0;
            if (next > FONT_SIZE_MAX) next = FONT_SIZE_MAX;
#endif
            if (next != s_fontSizeSelection) {
                s_fontSizeSelection = next;
                refreshFontSizeSelection();
            }
            continue;
        }

        if (s_sigPolicyModal) {
            if (isModalCloseKey(k)) {
                closeSigPolicyModal();
                refreshCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applySigPolicySelection(s_sigPolicySelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_sigPolicySelection + delta;
                if (next < 0) next = 0;
                if (next >= SIG_POLICY_COUNT) next = SIG_POLICY_COUNT - 1;
                if (next != s_sigPolicySelection) {
                    s_sigPolicySelection = next;
                    refreshSigPolicySelection();
                }
            }
            continue;
        }

        // Language picker (Config -> Language): drawn over Config, so it owns
        // the keys while it is up.
        if (s_cfgLangModal) {
            if (isModalCloseKey(k)) {
                cfgLangCancel();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgLangSave();
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            stepCfgLang(delta);
            continue;
        }

#if HAS_NAV_ALIGN_SETTING
        // Nav Alignment picker: drawn over Config, so it owns the keys too.
        if (s_cfgNavAlignModal) {
            if (isModalCloseKey(k)) {
                closeCfgNavAlignModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgNavAlignCommit(s_cfgNavAlignSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_cfgNavAlignSelection + delta;
                if (next < 0) next = 0;
                if (next > kNavAlignCount - 1) next = kNavAlignCount - 1;
                if (next != s_cfgNavAlignSelection) {
                    s_cfgNavAlignSelection = next;
                    refreshCfgNavAlignSelection();
                }
            }
            continue;
        }
#endif

#if HAS_RUNTIME_ORIENTATION
        // Same placement and shape as the preset picker below.
        if (s_cfgOrientModal) {
            if (isModalCloseKey(k)) {
                closeCfgOrientModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgOrientAsk(s_cfgOrientSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_cfgOrientSelection + delta;
                if (next < 0) next = 0;
                if (next > (int)UI_ORIENT_COUNT - 1) next = (int)UI_ORIENT_COUNT - 1;
                if (next != s_cfgOrientSelection) {
                    s_cfgOrientSelection = next;
                    refreshCfgOrientSelection();
                }
            }
            continue;
        }
#endif

        // Above the Config block because it is drawn above the Config screen:
        // while the picker is up it owns the keys, and nothing may reach the
        // row list underneath it.
        if (s_cfgPresetModal) {
            if (isModalCloseKey(k)) {
                closeCfgPresetModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgPresetAsk(s_cfgPresetSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_cfgPresetSelection + delta;
                if (next < 0) next = 0;
                if (next > s_cfgPresetCount - 1) next = s_cfgPresetCount - 1;
                if (next != s_cfgPresetSelection) {
                    s_cfgPresetSelection = next;
                    refreshCfgPresetSelection();
                }
            }
            continue;
        }

        // Mesh Beacons settings popup. The slider and text modals it opens are
        // handled above, so while either is up this never sees a key.
        if (s_mbCfgModal) {
            if (isModalCloseKey(k)) {
                closeMeshBeaconCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                meshBeaconCfgActivate(s_mbCfgSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_mbCfgSelection + delta;
                if (next < 0) next = 0;
                if (next > MB_CFG_ROW_COUNT - 1) next = MB_CFG_ROW_COUNT - 1;
                if (next != s_mbCfgSelection) {
                    s_mbCfgSelection = next;
                    paintPickerRows(s_mbCfgRows, MB_CFG_ROW_COUNT, s_mbCfgSelection,
                                    PICKER_SCROLL_VIEW);
                }
            }
            continue;
        }

#if FEATURE_LICENSED_MODE
        // Licensed mode popup; same shape as Mesh Beacons above.
        if (s_licCfgModal) {
            if (isModalCloseKey(k)) {
                closeLicCfgModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                licCfgActivate(s_licCfgSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_licCfgSelection + delta;
                if (next < 0) next = 0;
                if (next > s_licCfgRowCount - 1) next = s_licCfgRowCount - 1;
                if (next != s_licCfgSelection) {
                    s_licCfgSelection = next;
                    paintPickerRows(s_licCfgRows, s_licCfgRowCount, s_licCfgSelection,
                                    PICKER_SCROLL_VIEW);
                }
            }
            continue;
        }
#endif

        if (s_cfgCommunityModal) {
            if (isModalCloseKey(k)) {
                closeCfgCommunityModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgCommunityAsk(s_cfgCommunitySelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            else if (k == KEY_PAGE_UP)   delta = invertScrollNav ? 8 : -8;
            else if (k == KEY_PAGE_DN)   delta = invertScrollNav ? -8 : 8;
            if (delta != 0) {
                int next = s_cfgCommunitySelection + delta;
                if (next < 0) next = 0;
                if (next > s_cfgCommunityCount - 1) next = s_cfgCommunityCount - 1;
                if (next != s_cfgCommunitySelection) {
                    s_cfgCommunitySelection = next;
                    refreshCfgCommunitySelection();
                }
            }
            continue;
        }

        if (s_cfgNodeNameModal) {
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                cfgNodeNameSave();
                continue;
            }
            // Where the arrows move the caret (textFieldCaretKey()), Left and
            // Right do that instead, and Tab, Up and Down switch fields.
            if (textFieldCaretKey(cfgNodeNameFocusedInput(), k)) continue;
            // Tab is the field switch people reach for; the wheel and arrows do
            // it too, since two boards here have no Tab key worth pressing.
            if (k == KEY_TAB || k == KEY_SCROLL_UP || k == KEY_SCROLL_DN
                || k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN) {
                cfgNodeNameSetFocus(s_cfgNodeNameFocus == 0 ? 1 : 0);
                continue;
            }
            if (isBackspaceKey(k)) {
                // A hold (or the M9's dedicated Back button) leaves outright;
                // the key itself erases, and on an empty field steps back —
                // short to long, long out of the modal — which is the same
                // Erase/Back rule the channel name field follows.
                if (k != KEY_BACKSPACE) {
                    cfgNodeNameCancel();
                    continue;
                }
                lv_obj_t *field = cfgNodeNameFocusedInput();
                const char *cur = field ? lv_textarea_get_text(field) : nullptr;
                if (cur && cur[0]) {
                    lv_textarea_delete_char(field);
                } else if (s_cfgNodeNameFocus == 1) {
                    cfgNodeNameSetFocus(0);
                } else {
                    cfgNodeNameCancel();
                }
                continue;
            }
            if (isModalCloseKey(k)) {
                cfgNodeNameCancel();
                continue;
            }
            if (k >= 0x20 && k < 0x7F) {
                lv_obj_t *field = cfgNodeNameFocusedInput();
                if (field) {
                    char one[2] = {k, '\0'};
                    lv_textarea_add_text(field, one);
                }
                continue;
            }
            continue;
        }

        if (s_cfgWifiPassModal) {
            if (k == KEY_ENTER) {
                cfgWifiConnectFromPassModal();
                continue;
            }
            if (isBackspaceKey(k)) {
                if (s_cfgWifiPassInput && k == KEY_BACKSPACE) {
                    const char *cur = lv_textarea_get_text(s_cfgWifiPassInput);
                    if (!cur || !cur[0]) {
                        closeCfgWifiPassModal();
                        refreshCfgWifiScanModal(false);
                    } else {
                        lv_textarea_delete_char(s_cfgWifiPassInput);
                    }
                }
                continue;
            }
            if (isModalCloseKey(k)) {
                closeCfgWifiPassModal();
                refreshCfgWifiScanModal(false);
                continue;
            }
            if (textFieldCaretKey(s_cfgWifiPassInput, k)) continue;
            if (k >= 0x20 && k < 0x7F && s_cfgWifiPassInput) {
                char one[2] = {k, '\0'};
                lv_textarea_add_text(s_cfgWifiPassInput, one);
                continue;
            }
            continue;
        }

#if HAS_BLE_KEYBOARD
        // Reachable once a keyboard is paired (or over VNC): the dialog is
        // driven by its buttons on a bare board, but there is no reason a
        // keyboard cannot drive the dialog that pairs the next one.
        if (s_cfgBleKbdModal) {
            if (isModalCloseKey(k)) {
                closeCfgBleKbdModal();
                refreshCfgModal();
                continue;
            }
            if (k == 'n' || k == 'N') {
                bleKeyboardStartScan();
                refreshCfgBleKbdModal();
                continue;
            }
            if (k == 'f' || k == 'F') {
                cfgBleKbdForget();
                continue;
            }
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                const bool down = (k == KEY_SCROLL_DN) != invertScrollNav;
                const int count = bleKeyboardScanCount();
                if (down) {
                    if (s_cfgBleKbdSelection + 1 < count) s_cfgBleKbdSelection++;
                } else if (s_cfgBleKbdSelection > 0) {
                    s_cfgBleKbdSelection--;
                }
                refreshCfgBleKbdModal();
                continue;
            }
            if (k == KEY_ENTER) {
                cfgBleKbdPairSelected();
                continue;
            }
            continue;
        }
#endif

        if (s_cfgWifiScanModal) {
            if (isModalCloseKey(k)) {
                closeCfgWifiScanModal();
                if (s_cfgWifiModal) refreshCfgWifiPickerModal();
                continue;
            }
            if (k == 'n' || k == 'N') {
                refreshCfgWifiScanModal(true);
                continue;
            }
            if (k == KEY_SCROLL_UP) {
                if (invertScrollNav) {
                    if (s_cfgWifiScanSelection + 1 < s_cfgScannedWifiCount) {
                        s_cfgWifiScanSelection++;
                        refreshCfgWifiScanModal(false);
                    }
                } else if (s_cfgWifiScanSelection > 0) {
                    s_cfgWifiScanSelection--;
                    refreshCfgWifiScanModal(false);
                }
                continue;
            }
            if (k == KEY_SCROLL_DN) {
                if (invertScrollNav) {
                    if (s_cfgWifiScanSelection > 0) {
                        s_cfgWifiScanSelection--;
                        refreshCfgWifiScanModal(false);
                    }
                } else if (s_cfgWifiScanSelection + 1 < s_cfgScannedWifiCount) {
                    s_cfgWifiScanSelection++;
                    refreshCfgWifiScanModal(false);
                }
                continue;
            }
            if (k == KEY_ENTER) {
                if (s_cfgWifiScanSelection >= 0 && s_cfgWifiScanSelection < s_cfgScannedWifiCount) {
                    openCfgWifiPassModal(s_cfgWifiScanSelection);
                }
                continue;
            }
            continue;
        }

        if (s_cfgWifiModal) {
            if (isModalCloseKey(k)) {
                const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
                closeCfgWifiPickerModal();
                if (fromOnboarding || s_onboardingModal) renderOnboardingStage();
                else refreshCfgModal();
                continue;
            }
            if (k == 'n' || k == 'N') {
                openCfgWifiScanModal();
                continue;
            }
            if (k == 'd' || k == 'D') {
                openCfgWifiDeleteConfirm();
                continue;
            }
            if (k == KEY_SCROLL_UP) {
                if (invertScrollNav) {
                    if (s_cfgWifiSelection + 1 < s_cfgKnownWifiCount) {
                        s_cfgWifiSelection++;
                        refreshCfgWifiPickerModal();
                    }
                } else if (s_cfgWifiSelection > 0) {
                    s_cfgWifiSelection--;
                    refreshCfgWifiPickerModal();
                }
                continue;
            }
            if (k == KEY_SCROLL_DN) {
                if (invertScrollNav) {
                    if (s_cfgWifiSelection > 0) {
                        s_cfgWifiSelection--;
                        refreshCfgWifiPickerModal();
                    }
                } else if (s_cfgWifiSelection + 1 < s_cfgKnownWifiCount) {
                    s_cfgWifiSelection++;
                    refreshCfgWifiPickerModal();
                }
                continue;
            }
            if (k == KEY_ENTER) {
                const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
                applyCfgWifiSelection(s_cfgWifiSelection);
                if (fromOnboarding) {
                    const char *activeSsid = nullptr;
                    const char *activePass = nullptr;
                    wifiGetActiveCreds(&activeSsid, &activePass);
                    utf8util::copyTruncate(s_onboardingWifiSsidScratch,
                                           sizeof(s_onboardingWifiSsidScratch),
                                           activeSsid ? activeSsid : "");
                    utf8util::copyTruncate(s_onboardingWifiPassScratch,
                                           sizeof(s_onboardingWifiPassScratch),
                                           activePass ? activePass : "");
                }
                closeCfgWifiPickerModal();
                if (fromOnboarding || s_onboardingModal) {
                    renderOnboardingStage();
                    if (s_cfgStatus[0]) onboardingSetStatus(s_cfgStatus);
                } else {
                    refreshCfgModal();
                    if (s_cfgStatus[0]) {
                        openCfgActionMessageModal(s_cfgStatus);
                    }
                }
                continue;
            }
            continue;
        }

        // ── Hidden system-stats easter egg ───────────────────────────────────
        // Five (I) presses in a row while the Config screen is up reveal a live
        // CPU/memory readout. Detected here, above the CFG/info dispatch, so it
        // works regardless of what (I) does per-build (focus info vs. popup).
        {
            const bool cfgContext = s_cfgModal
#if !defined(DEVICE_TLORA_PAGER_TFT)
                                    || s_nodeInfoModal
#endif
                                    ;
            if (cfgContext && !s_sysStatsModal) {
                if (k == 'i' || k == 'I') {
                    uint32_t nowI = millis();
                    if ((uint32_t)(nowI - s_cfgInfoKeyLastMs) > 1500UL) {
                        s_cfgInfoKeyStreak = 0;
                    }
                    s_cfgInfoKeyLastMs = nowI;
                    if (++s_cfgInfoKeyStreak >= 5) {
                        s_cfgInfoKeyStreak = 0;
                        openSysStatsModal();
                        continue;
                    }
                } else {
                    s_cfgInfoKeyStreak = 0;
                }
            }
        }

        // The hidden stats screen is modal: any key dismisses it (after a brief
        // guard so the key-repeat from the 5th (I) doesn't close it instantly).
#if HAS_ADMIN_TERMINAL
        // First, and it takes everything. A terminal that let stray letters
        // through to the chat screen underneath would be unusable, and the
        // command line is where every key means something already.
        if (s_adminModal) {
            adminTerminalHandleKey(k);
            continue;
        }
#endif

        if (s_sysStatsModal) {
            if ((uint32_t)(millis() - s_sysStatsOpenedMs) >= 300UL) {
                closeSysStatsModal();
            }
            continue;
        }

        // Release notes: pure reading surface, so every nav key scrolls and
        // nothing selects. Backspace closes it on every board (the Cardputer's
        // Esc still works too, via isModalCloseKey).
        if (s_releaseNotesModal) {
            if (k == KEY_SCROLL_UP || k == KEY_PAGE_UP || k == KEY_PREV_CHAN) {
                scrollListClamped(s_releaseNotesScroll, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN || k == KEY_PAGE_DN || k == KEY_NEXT_CHAN) {
                scrollListClamped(s_releaseNotesScroll, -18);
                continue;
            }
            if (isModalCloseKey(k) || isBackspaceKey(k)) {
                closeReleaseNotesModal();
            }
            continue;
        }

#if HAS_MESSAGE_ACTIONS
        // Message Info (message actions). Over whatever screen the message was
        // on; scrolls with Up/Down and closes with the close key, and on a
        // touch build any key dismisses it.
        if (s_msgInfoModal) {
#if !UI_TOUCH_ONLY_PROFILE
            if (k == KEY_SCROLL_UP) {
                scrollListClamped(s_msgInfoModal, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN) {
                scrollListClamped(s_msgInfoModal, -18);
                continue;
            }
            if (isModalCloseKey(k) || isBackspaceKey(k)) closeMsgSenderInfoModal();
            continue;
#else
            closeMsgSenderInfoModal();
            continue;
#endif
        }
#endif

#if !defined(DEVICE_TLORA_PAGER_TFT)
        // The (I)nformation popup layers over the CFG modal. Every keyboard
        // build scrolls it with Up/Down (J/K on T-Deck) and closes with I or the
        // modal close key. Heltec is touch-first with no keyboard, so there any
        // key just dismisses it.
        if (s_nodeInfoModal) {
#if !UI_TOUCH_ONLY_PROFILE
            if (k == KEY_SCROLL_UP) {
                scrollListClamped(s_nodeInfoModal, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN) {
                scrollListClamped(s_nodeInfoModal, -18);
                continue;
            }
            if (isModalCloseKey(k) || k == 'i' || k == 'I') {
                closeNodeInfoModal();
            }
            continue;
#else
            closeNodeInfoModal();
            continue;
#endif
        }
#endif

        // Ahead of Config/DM/Nodes/Live, all of which end their block with an
        // unconditional continue and would otherwise eat every key while Tools
        // sat on top of them. That could not happen while Tools was reachable
        // only from Live — Live is handled after this — but the nav bar now
        // opens Tools over any of them.
        //
        // Still safe to sit above the tool surfaces further down: activating a
        // tool closes Tools first, so the two are never up together.
        if (s_liveToolsModal) {
            if (isModalCloseKey(k)) {
                closeLiveToolsModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                liveToolsActivate(s_liveToolsSelection);
                continue;
            }
            // The letters in the row labels jump straight to their tool.
            {
                bool handled = false;
                for (int i = 0; i < LIVE_TOOL_COUNT; i++) {
                    const char shortcut = kLiveToolShortcuts[i];
                    if (k != shortcut && k != (char)(shortcut - 'A' + 'a')) continue;
                    // A disabled row's letter is inert rather than a silent
                    // selection move: its label already says why.
                    if (!liveToolEnabled(i)) { handled = true; break; }
                    s_liveToolsSelection = i;
                    liveToolsActivate(i);
                    handled = true;
                    break;
                }
                if (handled) continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            // Tools fill column-major, so a step of one is already a vertical
            // move and left/right hops the column, as in the channel editor.
            if (delta == 0 && kChanModalCols > 1) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) {
                    delta = -kLiveToolRowsPerCol;
                } else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) {
                    delta = kLiveToolRowsPerCol;
                }
            }
            if (delta != 0) {
                int next = s_liveToolsSelection + delta;
                if (next < 0) next = 0;
                if (next >= LIVE_TOOL_COUNT) next = LIVE_TOOL_COUNT - 1;
                if (next != s_liveToolsSelection) {
                    s_liveToolsSelection = next;
                    refreshLiveToolsSelection();
                }
            }
            continue;
        }

        if (s_cfgModal) {
            if (s_cfgDebugLog) {
                char actionText[80];
                int actionId = (s_cfgSelection >= 0 && s_cfgSelection < s_cfgActionCount)
                             ? s_cfgActions[s_cfgSelection]
                             : -1;
                unsigned char uk = (unsigned char)k;
                char display = (k >= 0x20 && k < 0x7F) ? k : '.';
                Serial.printf("[lvgl-cfg] key src=%s code=0x%02X chr=%c sel=%d action=%d label=\"%s\"\n",
                              src,
                              (unsigned)uk,
                              display,
                              s_cfgSelection,
                              actionId,
                              (actionId >= 0) ? cfgActionLabel(actionId, actionText, sizeof(actionText)) : "(none)");
            }
            if (s_cfgAwaitEnterRelease) {
                if (s_cfgDebugLog) {
                    unsigned char uk = (unsigned char)k;
                    char display = (k >= 0x20 && k < 0x7F) ? k : '.';
                    Serial.printf("[lvgl-cfg] key-block waiting-release code=0x%02X chr=%c\n",
                                  (unsigned)uk, display);
                }
                continue;
            }
#if defined(DEVICE_TLORA_PAGER_TFT)
            // With the info panel focused, Backspace returns to the action panel
            // rather than closing the modal (must precede the close-key check,
            // since Backspace is the pager's close key).
            if (s_cfgInfoPanelFocused && isBackspaceKey(k)) {
                s_cfgInfoPanelFocused = false;
                refreshCfgPanelFocusStyles();
                continue;
            }
#endif
            // Backspace edits the filter before it closes the screen — it is
            // the close key on some builds, so this has to come first, exactly
            // as it does on the Nodes screen. An empty filter disarms.
            if (isBackspaceKey(k) && s_cfgFilterOpen) {
                if (s_cfgFilterLen > 0) {
                    s_cfgFilter[--s_cfgFilterLen] = '\0';
                } else {
                    s_cfgFilterOpen = false;
                }
                cfgApplyFilter();
                continue;
            }
#if UI_CFG_SECTIONS
            // Inside a section the close key goes back to the grid; on the grid
            // it closes Config as it always has.
            if (isModalCloseKey(k) && s_cfgSection >= 0) {
                cfgShowGrid();
                continue;
            }
#endif
            if (isModalCloseKey(k)) {
                closeCfgModal();
                continue;
            }
            // Space arms the filter and is never part of it. Letters only
            // append once it is armed, so (I)nfo and the rest keep working
            // until the user actually asks to filter.
            if (k == ' ') {
                if (!s_cfgFilterOpen) {
                    s_cfgFilterOpen = true;
                    cfgApplyFilter();
                }
                continue;
            }
            if (k > 0x20 && k < 0x7F && s_cfgFilterOpen) {
                if (s_cfgFilterLen < kCfgFilterMax) {
                    s_cfgFilter[s_cfgFilterLen++] = k;
                    s_cfgFilter[s_cfgFilterLen] = '\0';
                }
                cfgApplyFilter();
                continue;
            }
#if !defined(DEVICE_TLORA_PAGER_TFT)
            if (k == 'i' || k == 'I') {
                openNodeInfoModal();
                continue;
            }
#endif
#if defined(DEVICE_TLORA_PAGER_TFT)
            // (I) focuses the info panel for scrolling; the roller still swaps.
            if ((k == 'i' || k == 'I') && s_cfgInfoList) {
                s_cfgInfoPanelFocused = true;
                refreshCfgPanelFocusStyles();
                continue;
            }
            if (k == KEY_ROLLER && s_cfgInfoList) {
                s_cfgInfoPanelFocused = !s_cfgInfoPanelFocused;
                refreshCfgPanelFocusStyles();
                continue;
            }
#endif
#if UI_CFG_SECTIONS && CFG_GRID_FOUR_WAY
            // Left/Right across the grid. Up/Down take the KEY_SCROLL path
            // below, which steps a row on these boards (cfgMoveSelection()).
            if ((k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN) && cfgGridShown()) {
                cfgGridStep(0, k == KEY_NEXT_CHAN ? 1 : -1);
                continue;
            }
#endif
            if (k == KEY_SCROLL_UP) {
#if defined(DEVICE_TLORA_PAGER_TFT)
                if (s_cfgInfoPanelFocused && s_cfgInfoList) {
                    const int scrollStep = 18;
                    const int delta = kPagerWheelChatNav ? -scrollStep : scrollStep;
                    scrollListClamped(s_cfgInfoList, delta);
                    continue;
                }
#endif
                cfgMoveSelection(invertScrollNav ? 1 : -1, "scroll-up");
                continue;
            }
            if (k == KEY_SCROLL_DN) {
#if defined(DEVICE_TLORA_PAGER_TFT)
                if (s_cfgInfoPanelFocused && s_cfgInfoList) {
                    const int scrollStep = 18;
                    const int delta = kPagerWheelChatNav ? scrollStep : -scrollStep;
                    scrollListClamped(s_cfgInfoList, delta);
                    continue;
                }
#endif
                cfgMoveSelection(invertScrollNav ? -1 : 1, "scroll-dn");
                continue;
            }
#if UI_CFG_SECTIONS
            if (k == KEY_ENTER && cfgGridShown()) {
                if (s_cfgGridCount > 0) {
                    // Held like a row's Enter below, so the repeat of this press
                    // does not run the first row of the section it opens.
                    s_cfgAwaitEnterRelease = true;
                    s_cfgEnterLockUntilMs = millis() + 400UL;
                    cfgOpenSection(s_cfgGridSections[s_cfgGridSelection]);
                }
                return;
            }
#endif
            if (k == KEY_ENTER) {
                uint32_t now = millis();
                if ((uint32_t)(now - s_cfgLastScrollMs) < 180) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", TR("Selection settling - press Enter again"));
                    openCfgActionMessageModal(s_cfgStatus);
                    if (s_cfgDebugLog) {
                        Serial.printf("[lvgl-cfg] enter-block settle dt=%lu\n", (unsigned long)(now - s_cfgLastScrollMs));
                    }
                    refreshCfgModal();
                    continue;
                }
                if ((int32_t)(now - s_cfgEnterLockUntilMs) < 0) {
                    if (s_cfgDebugLog) {
                        Serial.printf("[lvgl-cfg] enter-block lock remaining=%ld\n",
                                      (long)(s_cfgEnterLockUntilMs - now));
                    }
                    continue;
                }
                s_cfgLastActivateMs = now;
                s_cfgAwaitEnterRelease = true;
                activateCfgSelection();
                // When confirm is pending, allow quick second Enter.
                // Otherwise, hold a longer lock to suppress held-key repeats.
                s_cfgEnterLockUntilMs = now + ((s_cfgConfirmAction >= 0) ? 250UL : 1500UL);
                return;
            }
            continue;
        }

        // The emoji picker sits on top of the chat/DM browse screen, so capture
        // its keys before either screen's dispatch. Move selects, Enter/click
        // sends the picked glyph as a one-emoji message and closes the tray, a
        // close key dismisses it without sending.
        if (s_emojiPickerModal) {
            if (isModalCloseKey(k)) {
                closeEmojiPicker();
                continue;
            }
            // The symbol key cycles rather than toggles: symbols, then emoji,
            // then closed. Pressing it a second time used to dismiss the tray,
            // which left the emoji tray unreachable from inside a message on a
            // keyboard with no other spare key -- and reaching for a picture
            // after reaching for a symbol is the same reflex, one press further.
            // The close key still dismisses from either tray, so nothing has to
            // be cycled past to get out.
            //
            // Only the Mesh Deck raises KEY_SYMBOL today; the guard is gone
            // because the code is board-independent and this is the behaviour
            // any board with a symbol key should get.
            if (k == KEY_SYMBOL) {
                if (s_pickerTray == kSymbolTray) {
                    // Rebuilt rather than re-pointed: the two trays have
                    // different cell fonts, column counts and hint text, all
                    // decided in openEmojiPicker().
                    closeEmojiPicker();
                    openEmojiPicker(/*sendMode=*/false, /*symbolTray=*/false);
                } else {
                    closeEmojiPicker();
                }
                continue;
            }
            // The key that opened the tray also dismisses it -- but only where
            // that key is known to arrive once per press.
            //
            // The M9's Ctrl is deliberately not on this list. Mapped as a toggle
            // it made the tray unusable, and open-only fixed it -- which is what
            // confirms the controller reports that key more than once per press.
            // As a toggle, the second report closed the tray, the third reopened
            // it, and every Enter or d-pad press in between landed on a panel
            // that was being torn down and rebuilt -- which looks exactly like
            // "the picker opens but nothing selects". Opening is idempotent
            // (openEmojiPicker() returns early when one is already up) and a
            // repeat that reaches here simply falls through to the delta chain
            // below, scores nothing, and is swallowed. The close key still
            // dismisses, as it does from either tray.
            if (k == KEY_EMOJI_PICKER) {
                closeEmojiPicker();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                emojiPickerActivate(s_emojiPickerSelection);
                continue;
            }
            // Up/down move a whole row; left/right move one cell. Keep this
            // mapping fixed regardless of wheel-invert settings so picker nav
            // always feels the same.
            // T-Deck/Pager override: j/k are horizontal in the emoji tray
            // (left/right) while non-j/k scroll input still moves by row.
            // On the
            // Cardputer ',' and '/' arrive unmapped here (the main screen turns
            // them into channel keys further down, but the tray is handled
            // first), and ';' / '.' have already become KEY_SCROLL_UP/DN.
            int delta = 0;
#if defined(DEVICE_TLORA_PAGER_TFT)
            // The wheel is one axis, so the grid reads to it as one long line:
            // a detent forward is the next cell, which runs along the row and
            // continues at the left of the one below; back is the reverse, up
            // the row and on to the right-hand end of the one above. Both ends
            // wrap, because a wheel has no ends to stop at -- stopping dead at
            // the last cell is the wheel telling you it is broken.
            //
            // Direction through invertScrollNav, the same expression the chat
            // and compose caret take on this board, so the detent that steps a
            // selection forward everywhere else steps forward here too.
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                const bool forward = (k == KEY_SCROLL_UP) ? invertScrollNav
                                                          : !invertScrollNav;
                delta = forward ? 1 : -1;
            }
#elif defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO)
            if (navFromJk && k == KEY_SCROLL_UP)       delta = -1;
            else if (navFromJk && k == KEY_SCROLL_DN)  delta = 1;
            else if (k == KEY_SCROLL_UP)               delta = -s_emojiPickerCols;
            else if (k == KEY_SCROLL_DN)               delta = s_emojiPickerCols;
#else
            if (k == KEY_SCROLL_UP)      delta = -s_emojiPickerCols;
            else if (k == KEY_SCROLL_DN) delta = s_emojiPickerCols;
#endif
            else if (k == ',' || k == KEY_PREV_CHAN) delta = -1;
            else if (k == '/' || k == KEY_NEXT_CHAN) delta = 1;
            if (delta != 0) {
                emojiPickerStep(delta);
                s_emojiPickerRepeatDelta  = delta;
                s_emojiPickerRepeatLastMs = millis();
            }
            continue;   // swallow all other keys while the tray is up
        }

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_M9) || defined(DEVICE_MESH_DECK) \
    || defined(DEVICE_TDISPLAY_P4) || UI_TANMATSU_LAYOUT \
    || (defined(DEVICE_TDECK) && HAS_TRACKBALL)
        // Steps the caret through the message being typed.
        //
        // It sits ahead of the per-screen handlers because compose is reachable
        // from chat, DMs and the nodes list, and each of those hands its keys to
        // handleComposeKey() — one branch here covers all three. The emoji tray
        // above already swallows every key while it is up, so it keeps the
        // pad/wheel for its own grid without being named here.
        //
        // Every board's compose edit goes through lv_textarea_add_text() and
        // lv_textarea_delete_char(), both cursor-relative, so insert-in-the-
        // middle and backspace-in-the-middle come free once the caret can move.
        if (s_composeModal && s_composeInput) {
#if defined(DEVICE_M9)
            // Control opens the emoji tray, first press. The key produced
            // nothing here before -- the driver dropped its code outright -- so
            // there is no prior meaning to press past, and nothing is lost by
            // swallowing it.
            //
            // Open-only, never a toggle: see the note in the tray's own key
            // block. This line is only reached with no tray up, and a repeat
            // that arrives once one is open is handled there.
            if (k == KEY_M9_CTRL) {
                openEmojiPicker(/*sendMode=*/false);
                continue;
            }
#endif
#if defined(DEVICE_M9) || defined(DEVICE_MESH_DECK) || defined(DEVICE_TDISPLAY_P4) \
    || defined(DEVICE_TDECK) || UI_TANMATSU_LAYOUT
#if HAS_SCROLL_INVERT
            // T-Deck: the trackball rolls the caret the way the ball moves, so
            // the list-scroll invert applied where trackball input enters the
            // pipeline is undone here. j/k are typed letters in compose and
            // never arrive as these keys.
            if (fromTrackball && s_cfg.invertScroll) {
                if (k == KEY_SCROLL_UP)      k = KEY_SCROLL_DN;
                else if (k == KEY_SCROLL_DN) k = KEY_SCROLL_UP;
            }
#endif
#if defined(DEVICE_TDECK)
            // A trackball rarely rolls dead level, so in the one-line box a
            // stray vertical tick would throw the caret to an end. Only a
            // wrapping box has lines to step between.
            if ((k == KEY_SCROLL_UP || k == KEY_SCROLL_DN)
                && lv_textarea_get_one_line(s_composeInput)) {
                continue;
            }
#endif
            // D-pad: Left/Right by a character, Up/Down by a display line. The
            // compose box is multi-line on both boards
            // (lv_textarea_set_one_line(false)), which is what makes the
            // vertical pair meaningful.
            //
            // The Mesh Deck belongs here because its front D-pad already folds
            // onto these exact four tokens -- meshDeckPollButtons() maps
            // UP/DOWN to KEY_SCROLL_UP/DN and LEFT/RIGHT to
            // KEY_PREV_CHAN/KEY_NEXT_CHAN -- and its matrix has no arrow keys of
            // its own. Nothing downstream claimed them while compose was open
            // (the chat screen's channel stepping is behind `if
            // (!s_composeModal)`, and the compose switches only pass 0x20..0x7F
            // through to the textarea), so all four were being dropped and the
            // pad did nothing at all inside a message.
            //
            // Deliberately NOT reusing the pager's invertScrollNav below: that
            // is kPagerWheelChatNav && !navFromJk, and kPagerWheelChatNav is
            // false on both boards, so folding them into that expression would
            // pick a direction by accident rather than state one.
            //
            // The T-Display P4's keyboard expansion is here for the same reason:
            // its arrow keys arrive as these four tokens (kTloraTapMap), and its
            // compose box wraps too. So do the Tanmatsu's arrow keys (kTmKeymap).
            if (k == KEY_PREV_CHAN) { lv_textarea_cursor_left(s_composeInput);  continue; }
            if (k == KEY_NEXT_CHAN) { lv_textarea_cursor_right(s_composeInput); continue; }
            if (k == KEY_SCROLL_UP) { lv_textarea_cursor_up(s_composeInput);    continue; }
            if (k == KEY_SCROLL_DN) { lv_textarea_cursor_down(s_composeInput);  continue; }
#else
            // Pager: the wheel is one axis, so it maps to left/right. Direction
            // follows invertScrollNav, the rule every other pager list uses, so
            // the detent that steps a selection forward moves the caret right.
            // Only the wheel reaches this — j/k stay literal characters in a
            // typing context, so they never fold onto KEY_SCROLL_UP/DN while a
            // compose box is open.
            if (s_composeEmojiFocused) {
                if (k == KEY_ENTER) {
                    // Enter on the button opens the tray rather than sending the
                    // message -- the one key this board has, doing what the
                    // thing under the caret says it does.
                    s_composeEmojiFocused = false;
                    refreshComposeEmojiFocus();
                    openEmojiPicker(/*sendMode=*/false);
                    continue;
                }
                if (k != KEY_SCROLL_UP && k != KEY_SCROLL_DN) {
                    // Anything else is the user going back to writing: a letter,
                    // a backspace, the close key. Hand focus back to the message
                    // and let the key fall through to the handlers below, so the
                    // character it stands for is not swallowed getting there.
                    s_composeEmojiFocused = false;
                    refreshComposeEmojiFocus();
                }
            }
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                const bool forward = (k == KEY_SCROLL_UP) ? invertScrollNav : !invertScrollNav;
                if (s_composeEmojiFocused) {
                    // On the button: forward has nowhere further to go, back
                    // returns to the message with the caret where it was left.
                    if (!forward) {
                        s_composeEmojiFocused = false;
                        refreshComposeEmojiFocus();
                    }
                    continue;
                }
                if (forward) {
                    // "Did the caret actually move?" rather than "is it at the
                    // end?". The same question, but asked in a way that needs no
                    // UTF-8 length arithmetic -- lv_textarea_cursor_right() is a
                    // no-op at the end of the text, so an unchanged position is
                    // exactly the case where the next detent should leave the
                    // message and land on the button.
                    const uint32_t before = lv_textarea_get_cursor_pos(s_composeInput);
                    lv_textarea_cursor_right(s_composeInput);
                    if (lv_textarea_get_cursor_pos(s_composeInput) == before
                        && lvObjValid(s_composeEmojiBtn)) {
                        s_composeEmojiFocused = true;
                        refreshComposeEmojiFocus();
                    }
                } else {
                    lv_textarea_cursor_left(s_composeInput);
                }
                continue;
            }
#endif
        }
#endif

        if (s_dmDelModal) {
            if (s_destructiveConfirmAction == DESTRUCTIVE_CONFIRM_KEY_CHANGE) {
                // A cursor over Reject/Accept rather than Y/N, so D-pad and
                // roller boards can answer too. Closing decides nothing.
                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN || k == KEY_PREV_CHAN
                    || k == KEY_NEXT_CHAN || k == KEY_TAB) {
                    s_keyReviewSel ^= 1;
                    keyReviewApplySelection();
                } else if (k == KEY_ENTER || k == KEY_ROLLER) {
                    if (s_keyReviewSel == 1) dmDeleteConfirmAccept();
                    else keyReviewReject();
                } else if (k == 'a' || k == 'A') {
                    dmDeleteConfirmAccept();
                } else if (k == 'r' || k == 'R') {
                    keyReviewReject();
                } else if (isBackspaceKey(k) || isModalCloseKey(k)) {
                    dmDeleteConfirmReject();
                }
                continue;
            }
            if (k == 'y' || k == 'Y' || k == KEY_ENTER) {
                dmDeleteConfirmAccept();
            } else if (k == 'n' || k == 'N' || isBackspaceKey(k) || isModalCloseKey(k)) {
                dmDeleteConfirmReject();
            }
            continue;
        }

        if (s_dmModal) {
            if (s_composeModal) {
                handleComposeKey(k);
                continue;
            }

            if (s_dmNodePickerModal) {
                if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE) {
                    if (s_dmNodeFilterOpen) {
                        if (s_dmNodeFilterLen > 0) {
                            s_dmNodeFilter[--s_dmNodeFilterLen] = '\0';
                        } else {
                            s_dmNodeFilterOpen = false;
                        }
                        dmNodePickerApplyFilter();
                        refreshDmNodePicker(true);
                    } else {
#if UI_CARDPUTER_COMPACT
                        // On Cardputer, Escape is the dedicated close action.
#else
                        closeDmNodePicker();
#endif
                    }
                    continue;
                }
                if (isModalCloseKey(k)) {
                    closeDmNodePicker();
                    continue;
                }
                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                    if (s_dmNodeFilteredCount > 0) {
                        int next = s_dmNodeSelection;
                        if (invertScrollNav) {
                            next += (k == KEY_SCROLL_UP) ? 1 : -1;
                        } else {
                            next += (k == KEY_SCROLL_UP) ? -1 : 1;
                        }
                        // Restyles in place unless the window has to scroll.
                        dmNodePickerMoveSelection(next);
                    }
                    continue;
                }
                if (k == KEY_ENTER) {
                    activateDmNodePickerSelection();
                    continue;
                }
                if (k >= 0x20 && k < 0x7F) {
                    if (!s_dmNodeFilterOpen) {
                        s_dmNodeFilterOpen = true;
                    }
                    if (s_dmNodeFilterLen < kDmNodeFilterMax) {
                        s_dmNodeFilter[s_dmNodeFilterLen++] = k;
                        s_dmNodeFilter[s_dmNodeFilterLen] = '\0';
                    }
                    dmNodePickerApplyFilter();
                    refreshDmNodePicker(true);
                    continue;
                }
                continue;
            }

            // Backspace steps out of the message pane; from the conversation
            // list it falls through to the close handling below. It keeps the
            // selection now that delete lives on D behind its own prompt —
            // there is no longer an armed state a stray Backspace could trip.
#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO) || defined(DEVICE_TLORA_PAGER_TFT)
            if ((k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD) && s_dmMsgPanelFocused) {
                s_dmMsgPanelFocused = false;
                refreshDmPanelFocusStyles();
                continue;
            }
#endif

            if (isModalCloseKey(k)) {
                closeDmModal();
                continue;
            }

            // The drawer's key, the one that opens the channel list on the chat
            // screen (legendChannelListTextRaw()): H on the Cardputer, C on the
            // other keyboards.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
            if ((k == 'h' || k == 'H') && dmDrawerBuilt()) {
#else
            if ((k == 'c' || k == 'C') && dmDrawerBuilt()) {
#endif
                dmDrawerSetOpen(!s_dmDrawerOpen);
                continue;
            }

            if (k == KEY_ROLLER && s_dmSelection > 0) {
                s_dmMsgPanelFocused = !s_dmMsgPanelFocused;
                refreshDmPanelFocusStyles();
                continue;
            }

            if ((k == 'd' || k == 'D') && s_dmSelection > 0 && !s_dmMsgPanelFocused) {
                // Raises the confirmation prompt, which answers for itself from
                // here (openDmDeleteConfirm). Inert on the "New DM" row and
                // while the message pane holds focus.
                dmRequestDeleteSelectedConversation();
                continue;
            }

            if ((k == 'e' || k == 'E') && s_dmSelection > 0) {
                // Quick emoji: picking sends a one-emoji DM to the selected
                // conversation (see sendQuickEmoji / emojiPickerActivate).
                openEmojiPicker(true);
                continue;
            }

            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                if (s_dmMsgPanelFocused && s_dmSelection > 0 && s_dmMsgList) {
                    const int scrollStep = 18;
                    int delta;
                    if (jkDirectionInvert) {
                        // This pane scrolled j=down while every list navigates
                        // j=up, which is the inconsistency that made j/k feel
                        // broken in DMs. Positive dy scrolls up
                        // (lv_obj_scroll_by_raw moves children by +dy, revealing
                        // content above), so j=up is +scrollStep.
                        delta = (k == KEY_SCROLL_UP) ? scrollStep : -scrollStep;
                    } else {
                        // Wheel and arrow input keep their existing feel.
                        delta = (k == KEY_SCROLL_UP)
                            ? (invertScrollNav ? scrollStep : -scrollStep)
                            : (invertScrollNav ? -scrollStep : scrollStep);
                    }
                    scrollListClamped(s_dmMsgList, delta);
                    continue;
                }

                int totalRows = s_dmConvCount + 1;
                if (totalRows > 0) {
                    int next = s_dmSelection;
                    if (invertScrollNav) {
                        next += (k == KEY_SCROLL_UP) ? 1 : -1;
                    } else {
                        next += (k == KEY_SCROLL_UP) ? -1 : 1;
                    }
                    if (next < 0) next = 0;
                    if (next >= totalRows) next = totalRows - 1;
                    if (next != s_dmSelection) {
                        s_dmMsgPanelFocused = false;
                        s_dmSelection = next;
                        refreshDmModal(true);
                    }
                }
                continue;
            }
#if UI_TOUCH_ONLY_PROFILE
            if (k == KEY_ENTER || k == ' ') {
                activateDmSelection();
                continue;
            }
#else
            // Space is the new-message key. Enter only advances focus into the
            // conversation's messages; it never opens the compose dialog.
            if (k == ' ') {
                activateDmSelection(true);
                continue;
            }
            if (k == KEY_ENTER) {
                activateDmSelection(false);
                continue;
            }
#endif
            continue;
        }

        // Outside the s_nodesModal branch below: this modal is opened both from
        // the Nodes screen and from the chat cursor, and nested under the Nodes
        // branch it would swallow no keys at all when opened from chat — arrows,
        // Enter, Esc and the single-key shortcuts would all fall through to the
        // handler underneath while the menu sat there looking interactive.
        //
        // Still yields to an open composer, which is the precedence it had when
        // it lived inside the Nodes branch (the compose block there runs first).
#if HAS_NODE_LOCATE
        // Nothing to interact with, so every key that could mean "done" closes
        // it and nothing else is consumed by it.
        if (s_nodeLocateModal) {
            if (isModalCloseKey(k) || k == KEY_ENTER) {
                closeNodeLocateModal();
                continue;
            }
            // Raw-key controls for Locate: i/j/k/l pans, n/m zooms. Handle
            // these before remapped nav tokens so j/k keep this meaning here.
            const int step = 24;   // about a fifth of the map box per press
            switch (rawKey) {
                case 'i': case 'I': nodeLocatePanBy(0,  step); continue;   // up
                case 'k': case 'K': nodeLocatePanBy(0, -step); continue;   // down
                case 'j': case 'J': nodeLocatePanBy( step, 0); continue;   // left
                case 'l': case 'L': nodeLocatePanBy(-step, 0); continue;   // right
                case 'm': case 'M': nodeLocateZoomStep(1);  continue;       // in
                case 'n': case 'N': nodeLocateZoomStep(-1); continue;       // out
                default: break;
            }

            // Pan with the trackball or arrows, zoom with page keys or +/-,
            // recentre with H or C (on the node map, on this node at zoom 10),
            // and go back to the opening view with Space.
            switch (k) {
                case KEY_SCROLL_UP:   nodeLocatePanBy(0,  step); continue;
                case KEY_SCROLL_DN:   nodeLocatePanBy(0, -step); continue;
                case KEY_PREV_CHAN:   nodeLocatePanBy( step, 0); continue;
                case KEY_NEXT_CHAN:   nodeLocatePanBy(-step, 0); continue;
                case KEY_PAGE_UP:     nodeLocateZoomStep(1);  continue;
                case KEY_PAGE_DN:     nodeLocateZoomStep(-1); continue;
                default: break;
            }
            switch (k) {
                case 'k': case 'K': nodeLocatePanBy(0,  step); continue;
                case 'j': case 'J': nodeLocatePanBy(0, -step); continue;
                case 'l': case 'L': nodeLocatePanBy(-step, 0); continue;
                case '+': case '=': nodeLocateZoomStep(1);  continue;
                case '-': case '_': nodeLocateZoomStep(-1); continue;
                case 'h': case 'H': nodeLocateCenterOnNode(); continue;
                case 'c': case 'C': nodeLocateCenterOnNode(); continue;
                case ' ':
                    if (s_nodeLocateAllNodes) {
                        nodeMapFitAll();
                    } else {
                        s_nodeLocateZoom = kNodeLocateZoomDefault;
                        nodeLocateCenterOnNode();
                    }
                    continue;
                default: break;
            }
            continue;
        }
#endif

#if NODES_INFO_AS_MODAL
        // Over the Nodes screen, which would otherwise take these keys and move
        // the cursor off the node this is describing.
        if (s_nodesInfoDlg) {
            if (k == KEY_SCROLL_UP) { scrollListClamped(s_nodesInfoPanel, 18);  continue; }
            if (k == KEY_SCROLL_DN) { scrollListClamped(s_nodesInfoPanel, -18); continue; }
            if (k == KEY_PAGE_UP)   { scrollListClamped(s_nodesInfoPanel, 60);  continue; }
            if (k == KEY_PAGE_DN)   { scrollListClamped(s_nodesInfoPanel, -60); continue; }
            if (isModalCloseKey(k) || k == KEY_ENTER || k == 'n' || k == 'N') {
                closeNodesInfoDlg();
            }
            continue;
        }
#endif

        if (s_nodesActionModal && !s_composeModal) {
            if (isModalCloseKey(k)) {
                closeNodesActionMenu();
                continue;
            }

#if UI_ARROW_GRID_NAV
            // The d-pad moves across the grid as drawn rather than through the
            // flat index, which on two columns made Down step sideways and left
            // Left/Right as a second pair of Up/Down (see nodesActionGridStep()).
            // Left/Right arrive unfolded because this modal is on the
            // m9FourWaySelection list above.
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN
                || k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN) {
                const int dx = (k == KEY_PREV_CHAN) ? -1 : (k == KEY_NEXT_CHAN) ? 1 : 0;
                const int dy = (k == KEY_SCROLL_UP) ? -1 : (k == KEY_SCROLL_DN) ? 1 : 0;
                const int next = nodesActionGridStep(s_nodesActionSelection, dx, dy);
                if (next != s_nodesActionSelection) {
                    s_nodesActionSelection = next;
                    refreshNodesActionMenuSelection();
                }
                continue;
            }
#endif

            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                int next = s_nodesActionSelection;
                if (invertScrollNav) {
                    next += (k == KEY_SCROLL_UP) ? 1 : -1;
                } else {
                    next += (k == KEY_SCROLL_UP) ? -1 : 1;
                }
                const int count = activeActionCount();
                if (next < 0) next = 0;
                if (next >= count) next = count - 1;
                if (next != s_nodesActionSelection) {
                    s_nodesActionSelection = next;
                    refreshNodesActionMenuSelection();
                }
                continue;
            }

            if (k == KEY_ENTER) {
                executeNodesActionSelection();
                continue;
            }

            // Single-key shortcuts select and execute the corresponding action,
            // mirroring the (X) hints in the labels. Message mode adds 1-6 for
            // the tapbacks, M for the full tray, R for Reply and I for Message
            // Info; T/D/Q/P/G keep their meanings in both modes. L is node
            // mode's alone, and on a node with no position it moves the
            // highlight and stops there — executeNodesActionSelection()
            // declines it exactly as Enter does.
            if (k >= 0x20 && k < 0x7F) {
                char up = (k >= 'a' && k <= 'z') ? (char)(k - 32) : k;
                const char *shortcuts = activeActionShortcuts();
                const int count = activeActionCount();
                for (int i = 0; i < count; i++) {
                    if (shortcuts[i] == up) {
                        s_nodesActionSelection = i;
                        refreshNodesActionMenuSelection();
                        executeNodesActionSelection();
                        break;
                    }
                }
            }
            continue;
        }

        // The Nodes Search dialog, typed into from a physical keyboard. Every
        // key is the dialog's while it is up, as with the other text fields --
        // the Nodes screen under it has Space and A shortcuts that letters
        // would otherwise reach.
        if (s_nodesFilterDialog) {
            if (k == KEY_ENTER) {
                applyNodesFilterText(s_nodesFilterInput
                                         ? lv_textarea_get_text(s_nodesFilterInput) : "");
                closeNodesFilterDialog();
                continue;
            }
            if (isBackspaceKey(k)) {
                // Erases; on an empty field backs out, as the others do.
                const char *cur = s_nodesFilterInput
                                      ? lv_textarea_get_text(s_nodesFilterInput) : nullptr;
                if (k == KEY_BACKSPACE && cur && cur[0]) lv_textarea_delete_char(s_nodesFilterInput);
                else                                     closeNodesFilterDialog();
                continue;
            }
            if (isModalCloseKey(k)) {
                closeNodesFilterDialog();
                continue;
            }
            if (textFieldCaretKey(s_nodesFilterInput, k)) continue;
            if (k >= 0x20 && k < 0x7F && s_nodesFilterInput) {
                char one[2] = {k, '\0'};
                lv_textarea_add_text(s_nodesFilterInput, one);
            }
            continue;
        }

        if (s_nodesModal) {
            if (s_composeModal) {
                handleComposeKey(k);
                continue;
            }

            // Backspace leaves search results for the favorites before it
            // reaches Back, which on most boards is the same key. From the
            // details it steps out to the list first, as Back does there.
            if ((k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD) && s_nodesFilterOpen
                && !s_nodesInfoFocused) {
                applyNodesFilterText("");
                continue;
            }

            // Space presses the Search / Clear button above the list: it opens
            // the Search dialog, or clears the search in force.
            if (k == ' ') {
                nodesSearchButtonActivate();
                continue;
            }

            // Actions moved off Enter and onto A when Enter became "focus the
            // details". Not from the Search button: no node is under the cursor.
            if (k == 'a' || k == 'A') {
                if (!s_nodesSearchBtnFocused) openNodesActionMenu();
                continue;
            }

            if (isModalCloseKey(k)) {
                // Back steps out of the detail panel first, and only closes the
                // screen from the list — the same shape as leaving a DM thread.
                if (s_nodesInfoFocused) {
                    s_nodesInfoFocused = false;
                    refreshNodesPanelFocusStyles();
                    refreshNodesHint();         // legend follows the focus
                } else {
                    closeNodesModal();
                }
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                // On a button above the list, Enter presses it. Clear, All
                // (and a finished search) put the cursor back on the first
                // row; Cancel leaves it on the button.
                if (s_nodesSearchBtnFocused) {
                    nodesFocusedButtonActivate();
                    continue;
                }
#if NODES_INFO_AS_MODAL
                // No details pane to hand the keys to: Enter opens the node's
                // actions, Node Info among them, as A does. An archived row's
                // only action is Restore, so there Enter shows its record and
                // A stays the way to restore it.
                if (currentNodesArchivedSelection()) openNodesInfoDlg();
                else                                 openNodesActionMenu();
                continue;
#endif
                // Enter hands the keys to the details; the Pager's wheel click
                // toggles, matching how it swaps panels on Config and DM.
                const bool wantFocus = (k == KEY_ROLLER) ? !s_nodesInfoFocused : true;
                // Archived rows count as something to focus: their panel holds
                // the record's names, dates and position, which is the whole
                // reason for showing them.
                const bool haveSelection = (currentNodesSelection() != nullptr
                                            || currentNodesArchivedSelection() != nullptr);
                if (wantFocus != s_nodesInfoFocused && (!wantFocus || haveSelection)) {
                    s_nodesInfoFocused = wantFocus;
                    refreshNodesPanelFocusStyles();
                    refreshNodesHint();
                }
                continue;
            }

            // With the details focused, the navigation keys scroll them instead
            // of moving the selection.
            if (s_nodesInfoFocused && lvObjValid(s_nodesInfoPanel)) {
                if (k == KEY_SCROLL_UP)  { scrollListClamped(s_nodesInfoPanel, 18);  continue; }
                if (k == KEY_SCROLL_DN)  { scrollListClamped(s_nodesInfoPanel, -18); continue; }
                if (k == KEY_PAGE_UP)    { scrollListClamped(s_nodesInfoPanel, 60);  continue; }
                if (k == KEY_PAGE_DN)    { scrollListClamped(s_nodesInfoPanel, -60); continue; }
            }

            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                // Pager wheel orientation: UP should move to the next row.
                const int step = invertScrollNav ? ((k == KEY_SCROLL_UP) ? 1 : -1)
                                                 : ((k == KEY_SCROLL_UP) ? -1 : 1);
#if NODES_KEYBOARD_TOP_BUTTONS
                // The Map, All and Search / Clear buttons sit above the first
                // row: Up from there (or from an empty list) moves onto Search,
                // Up again onto All, then Map, and Down walks back.
                if (s_nodesSearchBtnFocused) {
                    if (step < 0 && s_nodesTopBtn < nodesTopBtnLast()) {
                        nodesSetSearchBtnFocus(true, s_nodesTopBtn + 1);
                    } else if (step > 0 && s_nodesTopBtn > kNodesTopSearch) {
                        nodesSetSearchBtnFocus(true, s_nodesTopBtn - 1);
                    } else if (step > 0 && s_nodesFilteredCount > 0) {
                        s_nodesSelected = 0;
                        nodesClampRenderWindow();
                        refreshNodesListRows();
                        refreshNodesDetails();
                        nodesSetSearchBtnFocus(false);
                    }
                    continue;
                }
                if (step < 0 && s_nodesSelected <= 0) {
                    nodesSetSearchBtnFocus(true);
                    continue;
                }
#endif
                int nextSelected = s_nodesSelected + step;

                if (nextSelected < 0) nextSelected = 0;
                if (nextSelected >= s_nodesFilteredCount) nextSelected = s_nodesFilteredCount - 1;

                if (nextSelected != s_nodesSelected
                    && nextSelected >= 0
                    && nextSelected < s_nodesFilteredCount) {
                    s_nodesSelected = nextSelected;
                    nodesClampRenderWindow();
                    refreshNodesListRows();
                    refreshNodesListSelection();
                    refreshNodesDetails();
                    if (lv_obj_t *selectedRow = selectedNodesRowObj()) {
                        lv_obj_scroll_to_view(selectedRow, LV_ANIM_OFF);
                    }
                }
                continue;
            }
            continue;
        }

        if (s_chUtilChartModal) {
            if (isModalCloseKey(k)) {
                closeChUtilChartModal();
            }
            continue;
        }

        if (s_snrChartModal) {
            if (isModalCloseKey(k)) {
                closeSnrRssiChartModal();
            }
            continue;
        }

#if FEATURE_DISCOVERY
        // Above the Discovery block because it is drawn above the Discovery
        // modal: while the picker is up it owns the keys.
        if (s_presetPickModal) {
            if (isModalCloseKey(k)) {
                closeDiscoveryPresetModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                discoveryPresetPickCommit();
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_presetPickSelection + delta;
                if (next < 0) next = 0;
                if (next > s_presetPickCount - 1) next = s_presetPickCount - 1;
                if (next != s_presetPickSelection) {
                    s_presetPickSelection = next;
                    refreshDiscoveryPresetSelection();
                }
            }
            continue;   // nothing reaches Discovery underneath the picker
        }

        if (s_discoveryModal) {
            if (isModalCloseKey(k)) {
                closeDiscoveryModal();
                continue;
            }
            // Sweep is (W)  because Save took S. It is the one key here that
            // transmits, so it is also the one worth not putting under a finger
            // that was reaching for something else.
            if (k == 'w' || k == 'W') {
                // Asks how long first. The sweep itself starts from
                // discoveryDurationApply(), which is also where the refusal
                // reasons are still surfaced.
                openDiscoveryDurationModal(kDiscoveryNoPresetPending);
                continue;
            }
            // (P)reset opens the picker rather than starting anything: the scan
            // itself is committed from in there.
            if (k == 'p' || k == 'P') {
                openDiscoveryPresetModal();
                continue;
            }
            // C stops a run while one is going, and clears when none is. The
            // two never compete for it: with a sweep or a scan in flight the
            // list is still filling, so clearing it is not what the key is for,
            // and a second press once the run has stopped clears as it always
            // did. One key, whichever of the two the screen is currently able
            // to mean.
            if (k == 'c' || k == 'C') {
                if (discoveryRunInFlight()) discoveryCancelRun();
                else                        discoveryClear();
                refreshDiscoveryModal(true);
                continue;
            }
#if HAS_FILE_STORAGE
            if (k == 's' || k == 'S') {
                char msg[72];
                (void)discoverySaveJson(msg, sizeof(msg));   // msg says which way it went
                discoverySetStatus(msg);
                continue;
            }
#endif
            if (k == KEY_SCROLL_UP && s_discoveryList) {
                scrollListClamped(s_discoveryList, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN && s_discoveryList) {
                scrollListClamped(s_discoveryList, -18);
                continue;
            }
            continue;
        }
#endif

#if FEATURE_MQTT_MONITOR
        // Confirm sits above the picker, which sits above the monitor: each one
        // owns the keys while it is the topmost of the three.
        if (s_mqttConfirmModal) {
            if (isModalCloseKey(k)) {
                closeMqttConfirmModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                mqttSendConfirmed();
                continue;
            }
            continue;   // nothing else reaches past a confirmation prompt
        }

        if (s_mqttSendModal) {
            if (isModalCloseKey(k)) {
                closeMqttSendModal();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                const int chan = s_mqttSendSelection;
                closeMqttSendModal();
                openMqttConfirmModal(chan);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta == 0 && kChanModalCols > 1) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) {
                    delta = -kChanModalRowsPerCol;
                } else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) {
                    delta = kChanModalRowsPerCol;
                }
            }
            if (delta != 0) {
                int next = s_mqttSendSelection + delta;
                if (next < 0) next = 0;
                if (next >= MESH_CHANNELS) next = MESH_CHANNELS - 1;
                if (next != s_mqttSendSelection) {
                    s_mqttSendSelection = next;
                    refreshMqttSendSelection();
                }
            }
            continue;
        }

        if (s_mqttMonModal) {
            if (isModalCloseKey(k)) {
                closeMqttMonitorModal();
                continue;
            }
            if (k == 's' || k == 'S') {
                openMqttSendModal();
                continue;
            }
            // W, as in Discovery: asks how long, and whether to save, first.
            if (k == 'w' || k == 'W') {
                openMqttScanModal();
                continue;
            }
            if (k == 'c' || k == 'C') {
                mqttMonitorReset();
                continue;
            }
            if (k == KEY_SCROLL_UP && s_mqttMonList) {
                scrollListClamped(s_mqttMonList, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN && s_mqttMonList) {
                scrollListClamped(s_mqttMonList, -18);
                continue;
            }
            continue;
        }
#endif

#if HAS_WEATHER
        if (s_weatherModal) {
            if (isModalCloseKey(k)) {
                closeWeatherModal();
                continue;
            }
            // R refetches. The screen otherwise fetches once on open and then
            // sits on its cache, so this is the only way to ask again inside
            // the TTL — after a failure, or when you have moved.
            if (k == 'r' || k == 'R') {
                weatherRefresh();
                continue;
            }
            continue;
        }
#endif

        if (s_beaconsModal) {
            if (isModalCloseKey(k)) {
                closeBeaconsModal();
                continue;
            }
            if (k == 'c' || k == 'C') {
                beaconsClear();
                continue;
            }
            if (k == KEY_SCROLL_UP && s_beaconsList) {
                scrollListClamped(s_beaconsList, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN && s_beaconsList) {
                scrollListClamped(s_beaconsList, -18);
                continue;
            }
            continue;
        }

        // Above Tools and Live both: the filter picker is the topmost thing on
        // the Live stack whenever it is open.
        if (s_liveFilterModal) {
            if (isModalCloseKey(k)) {
                closeLiveFilterModal();       // dismiss without changing anything
                continue;
            }
            // F toggles, so the key that opened it also closes it.
            if (k == 'f' || k == 'F') {
                closeLiveFilterModal();
                continue;
            }
            if (k == 'm' || k == 'M') {
                liveFilterToggleNoMqtt();
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                if (s_liveFilterSelection == kLiveFilterNoMqttSel) {
                    liveFilterToggleNoMqtt();
                } else {
                    liveFilterApply(s_liveFilterSelection);
                }
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            // Column-major fill, as in Tools: one step walks down a column and
            // left/right hops between them.
            if (delta == 0 && kChanModalCols > 1) {
                if (k == KEY_PREV_CHAN || k == KEY_PAGE_UP) {
                    delta = -kLiveFilterRowsPerCol;
                } else if (k == KEY_NEXT_CHAN || k == KEY_PAGE_DN) {
                    delta = kLiveFilterRowsPerCol;
                }
            }
            if (delta != 0) {
                int next;
                const bool vertical = (delta == 1 || delta == -1);
                if (s_liveFilterSelection == kLiveFilterNoMqttSel) {
                    // The toggle spans both columns: down enters the grid at
                    // its top-left, sideways does nothing.
                    next = (delta == 1) ? 0 : kLiveFilterNoMqttSel;
                } else if (vertical && delta == -1
                           && s_liveFilterSelection % kLiveFilterRowsPerCol == 0) {
                    // Up off the top row of either column lands on the toggle.
                    next = kLiveFilterNoMqttSel;
                } else {
                    next = s_liveFilterSelection + delta;
                    if (next < 0) next = 0;
                    if (next >= LIVE_FILTER_COUNT) next = LIVE_FILTER_COUNT - 1;
                }
                if (next != s_liveFilterSelection) {
                    s_liveFilterSelection = next;
                    refreshLiveFilterSelection();
                }
            }
            continue;
        }

        if (s_liveModal) {
            if (isModalCloseKey(k)) {
                closeLiveModal();
                continue;
            }
            if (k == 'c' || k == 'C') {
                Channels.clearChannel(CHAN_LIVE);
                s_lastRenderedLiveCount = -1;
                s_lastRenderedLiveScrollOff = -1;
                refreshLiveView(true);
                continue;
            }
            if (k == 'f' || k == 'F') {
                openLiveFilterModal();
                continue;
            }
            if (k == 'm' || k == 'M') {
                liveFilterToggleNoMqtt();
                continue;
            }
            // Up reveals content above, down reveals below — scrollListClamped's
            // documented sign convention, and what the release notes, node info
            // and legend surfaces already do. These two were swapped, so the
            // whole feed scrolled backwards under the arrows and under j/k.
            if (k == KEY_SCROLL_UP && s_liveList) {
                scrollListClamped(s_liveList, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN && s_liveList) {
                scrollListClamped(s_liveList, -18);
                continue;
            }
            continue;
        }

        if (s_legendModal) {
            // Every board now, not only the Cardputer: at the larger face the
            // key list runs past the bottom of the 240 px panels too. The same
            // keys Release Notes scrolls on.
            if (k == KEY_SCROLL_UP || k == KEY_PAGE_UP || k == KEY_PREV_CHAN) {
                scrollListClamped(s_legendScroll ? s_legendScroll : s_legendModal, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN || k == KEY_PAGE_DN || k == KEY_NEXT_CHAN) {
                scrollListClamped(s_legendScroll ? s_legendScroll : s_legendModal, -18);
                continue;
            }
            if (isModalCloseKey(k)
                || k == 'h' || k == 'H') {
                closeLegendModal();
                continue;
            }
            if (k == 'd' || k == 'D') {
                closeLegendModal();
                openDmModal();
                continue;
            }
            if (k == 'c' || k == 'C') {
                closeLegendModal();
                openCfgModal();
                continue;
            }
            if (k == 'n' || k == 'N') {
                closeLegendModal();
                openNodesModal();
                continue;
            }
            if (k == 'l' || k == 'L') {
                closeLegendModal();
                openLiveToolsModal();
                continue;
            }
            continue;
        }

        if (s_channelActionsModal) {
            if (isModalCloseKey(k)) {
                closeChannelActionsModal();
                continue;
            }
            if (k == 'm' || k == 'M' || k == KEY_ENTER) {
                toggleActiveChannelMute();
                continue;
            }
            if (k == 'l' || k == 'L') {
                toggleActiveChannelShareLocation();
                continue;
            }
            continue;
        }

        if (!s_composeModal) {
#if !UI_TOUCH_ONLY_PROFILE
#if HAS_CHANNEL_PANEL_LOCK
            // Channel panel, keyboard only. Docked: Left or C steps into the
            // column, whose cursor takes Up/Down; Enter switches to the row
            // and Right/Esc/Backspace go back to the chat. Right again, from
            // the chat, undocks and closes it. Undocked: Left opens the drawer;
            // Left in the drawer docks it, cursor kept on its row; Enter or
            // Right there picks the row and hides the drawer (below). Any
            // other key drops the column cursor and carries on, so shortcuts
            // still work.
            if (channelColumnFocused()) {
                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                    int next = s_cardputerDropdownSelection;
                    if (next < 0 || next >= MESH_CHANNELS) next = s_activeChannel;
                    next += (k == KEY_SCROLL_UP) ? -1 : 1;
                    if (next < 0) next = MESH_CHANNELS - 1;
                    if (next >= MESH_CHANNELS) next = 0;
                    s_cardputerDropdownSelection = next;
                    if (s_channelBtns[next]) {
                        lv_obj_scroll_to_view(s_channelBtns[next], LV_ANIM_OFF);
                    }
                    refreshChannelGlow(true);
                    continue;
                }
                if (k == KEY_ENTER) {
                    int chosen = s_cardputerDropdownSelection;
                    if (chosen < 0 || chosen >= MESH_CHANNELS) chosen = s_activeChannel;
                    setActiveChannel(chosen);
                    setChannelColumnFocus(false);
                    continue;
                }
                if (k == KEY_PREV_CHAN) continue;   // already at the left edge
                if (k == KEY_NEXT_CHAN || isModalCloseKey(k)
                    || k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD) {
                    setChannelColumnFocus(false);
                    continue;
                }
                setChannelColumnFocus(false);
            } else if (channelColumnDocked() && chatScreenIsForeground()
                       && (k == KEY_PREV_CHAN || k == KEY_NEXT_CHAN)) {
                if (k == KEY_PREV_CHAN) setChannelColumnFocus(true);
                else                    setChannelPanelDockedFromKeys(false);
                continue;
            } else if (channelListIsDropdown() && chatScreenIsForeground()) {
                if (isChannelDropdownVisible()) {
                    if (k == KEY_PREV_CHAN) {
                        setChannelPanelDockedFromKeys(true, s_cardputerDropdownSelection);
                        continue;
                    }
                    if (k == KEY_NEXT_CHAN) k = KEY_ENTER;   // picks the row, as Enter
                } else if (k == KEY_PREV_CHAN) {
                    setChannelDropdownVisible(true);
                    refreshChannelGlow(true);
                    continue;
                }
            }
#endif
            if (isChannelDropdownVisible()) {
#if UI_CARDPUTER_COMPACT
                // Cardputer directional labels: '/' is right, ',' is left.
                if (k == '/') k = KEY_NEXT_CHAN;
                else if (k == ',') k = KEY_PREV_CHAN;
#endif

                if (k == KEY_NEXT_CHAN || k == KEY_PREV_CHAN
                    || k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                    int next = s_cardputerDropdownSelection;
                    if (next < 0 || next >= MESH_CHANNELS) next = s_activeChannel;

                    if (k == KEY_NEXT_CHAN) {
                        next += 1;
                    } else if (k == KEY_PREV_CHAN) {
                        next -= 1;
                    } else {
#if UI_CARDPUTER_COMPACT
                        // j and the up arrow move up the list, k and the down
                        // arrow move down it, the same as every other list on
                        // this board. This used to reverse itself whenever the
                        // key came from j/k — which, via the arrow-parity rule
                        // in this function, meant the arrows flipped too.
                        next += (k == KEY_SCROLL_UP) ? -1 : 1;
#elif UI_TANMATSU_LAYOUT
                        // Real arrow keys, not a trackball: up is up the list.
                        next += (k == KEY_SCROLL_UP) ? -1 : 1;
#else
                        // Mirror existing main-screen channel stepping directions.
                        if (navFromJk) {
                            next += (k == KEY_SCROLL_UP) ? -1 : 1;
                        } else {
                            next += (k == KEY_SCROLL_UP) ? 1 : -1;
                        }
#endif
                    }

                    if (next < 0) next = MESH_CHANNELS - 1;
                    if (next >= MESH_CHANNELS) next = 0;

                    s_cardputerDropdownSelection = next;
                    if (s_channelBtns[next]) {
                        lv_obj_scroll_to_view(s_channelBtns[next], LV_ANIM_OFF);
                    }
                    refreshChannelSelectorLabel();
                    refreshChannelGlow(true);
                    continue;
                }

#if UI_CARDPUTER_COMPACT
                if ((k == KEY_ENTER || k == KEY_FN_ENTER)
                    && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#else
                if (k == KEY_ENTER
                    && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#endif
                    int chosen = s_cardputerDropdownSelection;
                    if (chosen < 0 || chosen >= MESH_CHANNELS) chosen = s_activeChannel;
                    setActiveChannel(chosen);
                    setChannelDropdownVisible(false);
#if UI_CARDPUTER_COMPACT
                    s_cardputerMainChatPanelFocused = true;
                    // Return to plain chat focus after channel selection.
                    s_pagerChatCursorMode = false;
                    refreshChatView(true);
#endif
                    refreshChannelGlow(true);
                    continue;
                }

                if (isModalCloseKey(k) || k == 'h' || k == 'H') {
                    setChannelDropdownVisible(false);
                    refreshChannelGlow(true);
                    continue;
                }
            }
#elif defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_MESH_DECK)
            // Mesh Deck: the same, driven by its D-pad.
            // The keyboard expansion on the portrait drawer. Touch-only, so the
            // selector block above is not compiled here and the arrows already
            // switch the channel as they move (see the main-nav block below).
            // What was missing is the finish: Enter fell through to compose, so
            // picking a channel opened a new message on it instead of the
            // channel itself. Enter now closes the drawer onto the chat.
            if (isChannelDropdownVisible()) {
#if defined(DEVICE_TDISPLAY_P4) && HAS_CHANNEL_PANEL_LOCK
                // Landscape: Left in the drawer docks the list; Right closes the
                // drawer onto the chat, as Enter does.
                if (!uiPortrait() && k == KEY_PREV_CHAN) {
                    setChannelPanelDockedFromKeys(true);
                    continue;
                }
                if (!uiPortrait() && k == KEY_NEXT_CHAN) k = KEY_ENTER;
#endif
                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                    // Claimed here so a message cursor left on from before the
                    // drawer opened cannot take the arrows away from the list.
                    int next = s_activeChannel + ((k == KEY_SCROLL_UP) ? -1 : 1);
                    if (next < 0) next = MESH_CHANNELS - 1;
                    if (next >= MESH_CHANNELS) next = 0;
                    setActiveChannel(next);
                    continue;
                }
                if (k == KEY_ENTER || isModalCloseKey(k)) {
                    setChannelDropdownVisible(false);
                    refreshChannelGlow(true);
                    continue;
                }
            }
#endif

            if (kUseScrollKeysForMainNav) {
#if UI_CARDPUTER_COMPACT
                // Cardputer directional labels: '/' is right, ',' is left.
                if (k == '/') k = KEY_NEXT_CHAN;
                else if (k == ',') k = KEY_PREV_CHAN;
#endif

                if (k == KEY_NEXT_CHAN || k == KEY_PREV_CHAN) {
#if defined(DEVICE_TDISPLAY_P4)
                    // Landscape docks the channel list beside the chat, so the
                    // keyboard expansion's arrows move between the two: Right
                    // from the list puts the cursor on the newest message, and
                    // Right again undocks the list (Display > Channel Panel),
                    // closed. Undocked, Left opens the drawer, and Left in the
                    // drawer docks it (handled with the drawer's keys above).
                    // Portrait's list is a drawer, with no side to move to.
#if HAS_CHANNEL_PANEL_LOCK
                    if (!uiPortrait() && chatScreenIsForeground()) {
                        if (k == KEY_NEXT_CHAN && !channelListIsDropdown()
                            && s_pagerChatCursorMode) {
                            setChannelPanelDockedFromKeys(false);
                            continue;
                        }
                        if (k == KEY_PREV_CHAN && channelListIsDropdown()
                            && !isChannelDropdownVisible()) {
                            setChannelDropdownVisible(true);
                            refreshChannelGlow(true);
                            continue;
                        }
                    }
#endif
                    if (!uiPortrait() && s_activeChannel >= 0
                        && s_activeChannel < MESH_CHANNELS) {
                        if (k == KEY_NEXT_CHAN && !s_pagerChatCursorMode) {
                            s_pagerChatCursorMode = true;
                            if (!pagerSelectChatCursorIndex(-1)) {
                                s_pagerChatCursorMode = false;   // nothing to land on
                            }
                            refreshChatView(true);
                        } else if (k == KEY_PREV_CHAN && s_pagerChatCursorMode) {
                            pagerExitChatCursorMode(true);
                            refreshChatView(true);
                        }
                        refreshChannelGlow(true);
                    }
#endif
#if UI_CARDPUTER_COMPACT
                    if (!s_cardputerMainChatPanelFocused) {
                        int nextChannel = s_activeChannel + ((k == KEY_NEXT_CHAN) ? 1 : -1);
                        if (nextChannel < 0) nextChannel = MESH_CHANNELS - 1;
                        if (nextChannel >= MESH_CHANNELS) nextChannel = 0;
                        setActiveChannel(nextChannel);
                    }
#endif
                    continue;
                }

#if UI_CARDPUTER_COMPACT
                if (k == KEY_ESCAPE) {
                    if (s_pagerChatCursorMode) {
                        pagerExitChatCursorMode(true);
                        refreshChatView(true);
                        // First Esc from chat cursor returns to plain chat focus.
                        s_cardputerMainChatPanelFocused = true;
                        if (isChannelDropdownVisible()) {
                            setChannelDropdownVisible(false);
                        }
                        refreshChannelGlow(true);
                        continue;
                    }
                    if (isChannelDropdownVisible()) {
                        setChannelDropdownVisible(false);
                        refreshChannelGlow(true);
                        continue;
                    }
                    if (s_cardputerMainChatPanelFocused) {
                        s_cardputerMainChatPanelFocused = false;
                    }
                    refreshChannelGlow(true);
                    continue;
                }

                if (k == KEY_ENTER && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                    if (!s_cardputerMainChatPanelFocused) {
                        if (!isChannelDropdownVisible()) {
                            s_cardputerDropdownSelection = s_activeChannel;
                            setChannelDropdownVisible(true);
                        } else {
                            int chosen = s_cardputerDropdownSelection;
                            if (chosen < 0 || chosen >= MESH_CHANNELS) chosen = s_activeChannel;
                            setActiveChannel(chosen);
                            setChannelDropdownVisible(false);
                            s_cardputerMainChatPanelFocused = true;
                            // Return to plain chat focus after channel selection.
                            s_pagerChatCursorMode = false;
                            refreshChatView(true);
                        }
                        refreshChannelGlow(true);
                        continue;
                    }
                }
#endif

                if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE) {
                    if (s_pagerChatCursorMode) {
                        pagerExitChatCursorMode(true);
                        refreshChatView(true);
                    } else if (s_selectedMsgReplyPacketId != 0 || s_selectedMsgText[0]) {
                        clearSelectedMsgContext();
                        s_lastRenderedChannel = -1;
                        refreshChatView(true);
#if UI_CARDPUTER_COMPACT
                    } else if (s_cardputerMainChatPanelFocused) {
                        s_cardputerMainChatPanelFocused = false;
#endif
                    }
                    continue;
                }

                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
#if UI_CARDPUTER_COMPACT
                    if (!s_cardputerMainChatPanelFocused) {
                        if (isChannelDropdownVisible()) {
                            int next = s_cardputerDropdownSelection;
                            if (next < 0 || next >= MESH_CHANNELS) next = s_activeChannel;
                            // Same direction as the selector handler above, which
                            // claims these keys first whenever the dropdown is
                            // open and so is the one that actually runs.
                            next += (k == KEY_SCROLL_UP) ? -1 : 1;
                            if (next < 0) next = MESH_CHANNELS - 1;
                            if (next >= MESH_CHANNELS) next = 0;
                            s_cardputerDropdownSelection = next;
                            if (s_channelBtns[next]) {
                                lv_obj_scroll_to_view(s_channelBtns[next], LV_ANIM_OFF);
                            }
                            refreshChannelSelectorLabel();
                            refreshChannelGlow(true);
                            continue;
                        }

                        s_cardputerMainChatPanelFocused = true;
                        s_pagerChatCursorMode = true;
                        if (!pagerSelectChatCursorIndex(-1)) {
                            s_pagerChatCursorMode = false;
                        }
                        refreshChannelGlow(true);
                        continue;
                    }

                    if (s_cardputerMainChatPanelFocused) {
                        // With the message cursor on, Up is the message above and
                        // Down the one below, as on the Tanmatsu. The cursor is
                        // what Reply and Message Actions act on; while these keys
                        // only scrolled, it could never leave the newest message.
                        // Esc or Backspace turns it off and gives them back to
                        // scrolling.
                        if (s_pagerChatCursorMode) {
                            pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex
                                                       + ((k == KEY_SCROLL_UP) ? -1 : 1));
                            refreshChannelGlow(true);
                            continue;
                        }

                        // Cursor off: one text line per press, rather than one
                        // whole message, for reading.
                        //
                        // Stepping message to message is no way to read on these
                        // panels: with 12-18 px chat text a single message is
                        // often taller than the 135 px Cardputer-Adv panel, so a
                        // step skips straight over the middle of it. Pixel
                        // scrolling is safe here because the window grows from
                        // the scroll position itself — reaching the top margin
                        // loads older history through the existing scroll
                        // callback, exactly as a touch drag does (see
                        // kChatWindowGrowScrollMargin).
                        const lv_coord_t line = (lv_coord_t)lv_font_get_line_height(
                            scaledChatFont(kChannelChatFont));
                        // Positive dy reveals content above — scrollListClamped's
                        // documented convention. j/k arrive pre-mapped, so the
                        // sign is the same for both input paths here.
                        scrollListClamped(s_chatList, (k == KEY_SCROLL_UP) ? line : -line);
                        refreshChannelGlow(true);
                        continue;
                    }

#else
                    if (s_pagerChatCursorMode) {
                        int navDelta;
#if defined(DEVICE_TDISPLAY_P4)
                        // Arrows, not a wheel: Up is the message above, as on
                        // the channel list below.
                        navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
#else
                        if (navFromJk) navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
                        else           navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
#endif
                        pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + navDelta);
                    } else if (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                        // Channel list uses reversed j/k semantics by request.
                        int navDelta;
#if defined(DEVICE_TDISPLAY_P4)
                        // The keyboard expansion's arrows, not a wheel: Up is
                        // the channel above in the list, Down the one below,
                        // wrapping at either end. The reversal below is the
                        // Pager's wheel, whose turn direction it matches.
                        navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
#else
                        if (navFromJk) {
                            navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
                        } else {
                            navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
                        }
#endif
                        int nextChannel = s_activeChannel + navDelta;
                        if (nextChannel < 0) nextChannel = MESH_CHANNELS - 1;
                        if (nextChannel >= MESH_CHANNELS) nextChannel = 0;
                        setActiveChannel(nextChannel);
                    }
#endif
                    continue;
                }

                if (k == KEY_ROLLER && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                    if (s_pagerChatCursorMode) {
#if !defined(DEVICE_TLORA_PAGER_TFT)
                        // Trackball builds keep click-to-reply: a click is a
                        // deliberate press, not something the thumb does on the
                        // way past. The Pager's wheel click is easy to trigger
                        // while scrolling, so it never opens the composer —
                        // Tab replies to the selected message there, Space
                        // starts a new one.
                        if (s_selectedMsgReplyPacketId != 0) {
                            openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                        }
#endif
                    } else {
                        s_pagerChatCursorMode = true;
                        pagerSelectChatCursorIndex(-1);
                    }
                    continue;
                }

                if (k == KEY_TAB && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                    if (s_pagerChatCursorMode && s_selectedMsgReplyPacketId != 0) {
                        openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                    }
                    continue;
                }
            }

#if defined(DEVICE_TDECK) || defined(DEVICE_TDECK_PRO)
            // T-Deck-family boards have no scroll wheel, so j/k (plus the
            // original T-Deck's trackball) activate and drive the chat cursor
            // directly instead of switching channels like the Pager's wheel.
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                if (!s_pagerChatCursorMode) {
                    s_pagerChatCursorMode = true;
                    if (!pagerSelectChatCursorIndex(-1)) {
                        s_pagerChatCursorMode = false;
                    }
                } else {
                    int navDelta;
                    if (navFromJk) navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
                    else           navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
                    pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + navDelta);
                }
                continue;
            }
#elif UI_TANMATSU_LAYOUT
            // Real arrow keys and no main-nav scroll mode, so nothing else on
            // the chat screen claims them. With the cursor on (Enter), Up is
            // the message above and Down the one below; without it they scroll
            // the chat a text line at a time.
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                if (s_pagerChatCursorMode) {
                    pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex
                                               + ((k == KEY_SCROLL_UP) ? -1 : 1));
                } else if (s_chatList) {
                    const lv_coord_t line = (lv_coord_t)lv_font_get_line_height(
                        scaledChatFont(kChannelChatFont));
                    scrollListClamped(s_chatList, (k == KEY_SCROLL_UP) ? line : -line);
                }
                continue;
            }
#endif

            if (k == 'l' || k == 'L') {
                openLiveToolsModal();
            } else if (k == 'a' || k == 'A') {
                openChannelActionsModal();
            } else if (k == 'd' || k == 'D') {
                openDmModal();
#if HAS_HOME_DASHBOARD
            // C is chat and F is config, matching the buttons: Home opens the
            // dashboard now, so the letter that used to mean "back to chat" had
            // to move off H, and Config gave up C to make room. N and L are
            // untouched — nothing about them changed.
            } else if (k == 'c' || k == 'C') {
#if HAS_GLOBAL_NAV_SHORTCUTS
                // Chat is already what is on screen here, so this press is the
                // second one, and a deliberate one unlike the chord: the channel
                // list is what it answers with, exactly as the M9's Messages
                // button does.
                openNavChatShortcut(/*allowChannelList=*/true,
                                    /*resetChannel=*/false);
#else
                // A touch-only board being driven from a Bluetooth keyboard.
                // There is no global-nav machinery compiled in here, so this
                // does what the nav bar's Home cell does on a second tap.
                closeHomeDashboard();
#endif
            } else if (k == 'f' || k == 'F') {
                openCfgModal();
#else
            } else if (k == 'c' || k == 'C') {
                openCfgModal();
#endif
            } else if (k == 'n' || k == 'N') {
                openNodesModal();
            } else if (k == 'p' || k == 'P') {
                // Help. The one nav cell that had no letter of its own: H is
                // Home wherever there is a dashboard and the channel list where
                // there is not, and "?" is a Shift layer away on every one of
                // these keyboards.
                //
                // Outside the HAS_HOME_DASHBOARD split above on purpose — P is
                // free either way, so the Cardputer gets the same key as every
                // other board rather than one of its own to remember.
                //
                // Straight to the opener, like D, N, L and F beside it, rather
                // than through openNavHelpShortcut(). The shortcut is what the
                // Alt+P chord takes, because a chord arrives from anywhere and
                // has to tear down whatever that was; here chat is already the
                // foreground surface, and openLegendModal() closes the rest
                // itself. C and H go the other way only because they mean more
                // than "open this" — the channel list and the dashboard.
                openLegendModal();
            } else if (k == 'e' || k == 'E') {
                // Quick emoji: opens the tray; picking sends a one-emoji message
                // to the active channel (see sendQuickEmoji / emojiPickerActivate).
                openEmojiPicker(true);
            } else if (k == 'h' || k == 'H') {
#if HAS_HOME_DASHBOARD && HAS_GLOBAL_NAV_SHORTCUTS
                openNavHomeDashboardShortcut();
#elif HAS_HOME_DASHBOARD
                // See the C branch above: no global-nav machinery on this board,
                // so this is the nav bar's Home cell spelled out. Help keeps its
                // "?" button on that bar.
                openHomeDashboard();
#elif UI_TOUCH_ONLY_PROFILE
                openLegendModal();
#else
                setChannelDropdownVisible(!isChannelDropdownVisible());
                refreshChannelGlow(true);
#endif
#if UI_TOUCH_ONLY_PROFILE && !defined(DEVICE_TDISPLAY_P4) && !defined(DEVICE_MESH_DECK) \
    && !CZERO_DESKTOP
            // Touch-first build: Enter keeps its original new-message behavior.
            // Not the P4: its keyboard expansion has a space bar, so it takes
            // the keyboard builds' keys below -- Space composes, Enter steps
            // into the messages. Nor the Mesh Deck, whose D-pad SELECT and
            // optional keyboard keep those same keys, nor the CardputerZero's
            // desktop window, which always has its keyboard.
            } else if (k == KEY_ENTER
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#if HAS_HOME_DASHBOARD
                // Not from the dashboard. Compose belongs to the chat screen:
                // the dashboard is a glance surface with no channel in front of
                // you, and a message box opening over it is something you did
                // not ask for. It used to open here as a new message, with the
                // title naming the channel it would go to -- but explaining
                // which conversation you have accidentally started is not as
                // good as not starting one.
                //
                // Swallowed rather than let fall through, so the key cannot pick
                // up a second meaning further down.
                if (homeDashboardIsForeground()) {
                    // deliberately nothing
                } else
#endif
                if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
#else
            // Space is the new-message key on keyboard builds; it replaced Enter.
#if UI_CARDPUTER_COMPACT
            } else if ((k == ' ' || k == KEY_FN_ENTER)
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#if HAS_HOME_DASHBOARD
                // The Zero has the dashboard the ADV lacks; compose belongs to
                // the chat screen (see the Space branch below).
                if (homeDashboardIsForeground()) {
                    // deliberately nothing
                } else
#endif
                if (!s_cardputerMainChatPanelFocused) {
                    // In nav focus, the channel selector flow owns activation.
                } else if (s_pagerChatCursorMode && s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
#else
            } else if (k == ' '
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#if HAS_HOME_DASHBOARD
                // Not from the dashboard. Compose belongs to the chat screen:
                // the dashboard is a glance surface with no conversation in
                // front of you, and a message box opening over it is not what
                // the key was pressed for.
                //
                // It used to open here as a new message rather than a reply, on
                // the reasoning that a stale reply target from the chat screen
                // would be worse -- which was solving the wrong half. Naming the
                // channel in the title explained which conversation you had
                // accidentally started; not starting one is better.
                //
                // Swallowed rather than allowed to fall through, so Space cannot
                // pick up some other meaning further down.
                if (homeDashboardIsForeground()) {
                    // deliberately nothing
                } else
#endif
                if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
#endif
            // Enter now moves the cursor into the selected channel's messages
            // (same effect as the pager's wheel click) instead of composing.
            } else if (k == KEY_ENTER
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                if (!s_pagerChatCursorMode) {
                    s_pagerChatCursorMode = true;
                    if (!pagerSelectChatCursorIndex(-1)) {
                        s_pagerChatCursorMode = false;
                    }
                } else if (s_selectedMsgSenderNodeId != 0
                           && s_selectedMsgSenderNodeId != s_myNodeId) {
                    // Second Enter opens Message Actions for the highlighted
                    // message — the same two-step rhythm the Nodes screen has,
                    // where Enter on a row opens the same menu.
                    //
                    // Nothing happens on our own messages or on local/system
                    // lines (sender 0): traceroute, DM and favorite against
                    // ourselves are meaningless, and a system line has no one to
                    // act on. Swallowed silently rather than beeping about it.
                    //
                    // The cursor already set the reply context when it landed on
                    // this row, so the reaction and Reply rows have their target.
                    openMessageActionMenu(s_selectedMsgReplyPacketId,
                                          s_selectedMsgSenderNodeId);
                }
#endif
            } else if (k == KEY_BACKSPACE) {
                if (s_pagerChatCursorMode) {
                    pagerExitChatCursorMode(true);
                    refreshChatView(true);
                } else if (s_selectedMsgReplyPacketId != 0 || s_selectedMsgText[0]) {
                    clearSelectedMsgContext();
                    s_lastRenderedChannel = -1;
                    refreshChatView(true);
                }
            }
            continue;
        }

        handleComposeKey(k);
    }
}

