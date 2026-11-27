// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: MECP display, shared UI state, forward declarations and text helpers.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

// MECP (issue #158): a message body that parses as MECP draws as its decoded,
// localized text. Display only -- the stored line keeps the wire text, which is
// what replies quote, what the raw-text action shows and what a language change
// re-renders from. Returns false (out untouched) for anything that is not MECP.
static bool mecpDecodeBody(const char *body, char *out, size_t cap, uint8_t *sevOut = nullptr) {
    if (!body || !out || cap == 0 || !mecp::looksLikeMecp(body)) return false;
    mecp::Envelope env;
    if (!mecp::parse(body, env)) return false;
    if (!mecp::render(body, i18nGetLang(), out, cap)) return false;
    if (sevOut) *sevOut = env.severity;
    return true;
}

// Replaces the body that follows `prefixLen` bytes of `line` with its decoded
// form, keeping the prefix (icon, time, sender). False when the body is not MECP.
static bool mecpDecodeLineInPlace(char *line, size_t cap, size_t prefixLen,
                                  uint8_t *sevOut = nullptr) {
    if (!line || prefixLen >= cap) return false;
    char decoded[384];
    if (!mecpDecodeBody(line + prefixLen, decoded, sizeof(decoded), sevOut)) return false;
    utf8util::copyTruncate(line + prefixLen, cap - prefixLen, decoded);
    return true;
}

// Severity tint for a received MECP line in the classic style.
// Light mode gets darker shades: amber on white is unreadable.
static uint16_t mecpSeverityColor565(uint8_t sev, bool light) {
    switch (sev) {
        case mecp::SEV_MAYDAY:   return light ? 0xC000 : 0xF800;   // red
        case mecp::SEV_URGENT:   return light ? 0xC200 : 0xFC00;   // orange
        case mecp::SEV_SAFETY:   return light ? 0x8400 : 0xFE60;   // amber
        default:                 return light ? 0x0219 : 0x3D9F;   // blue
    }
}

// LVGL runs entirely on the Arduino loopTask (lv_timer_handler below), and the
// default 8 KB loopTask stack is not enough to rasterize an emoji under LVGL 9.
//
// Rendering a character the built-in Montserrat face lacks falls through to the
// tiny_ttf/stb_truetype fallback (see emoji_font.*), which is by far the
// deepest path in the firmware:
//
//   lv_timer_handler -> lv_display_refr_timer -> lv_obj_refr -> lv_obj_redraw
//     -> lv_draw_label_iterate_characters (400B) -> lv_draw_unit_draw_letter
//     -> ttf_get_glyph_bitmap_cb -> stbtt__rasterize_sorted_edges (672B)
//     -> stbtt__tesselate_cubic, which self-recurses to depth 16 at 128B a
//        frame = 2 KB of recursion on its own
//
// That is ~5-6 KB worst case. It fit under LVGL 8 only barely; v9's draw
// pipeline (draw tasks/units) sits about 0.5-1 KB deeper and pushed it over,
// which is why a DM containing an emoji aborted with a loopTask stack overflow
// while plain ASCII — served from pre-rasterized Montserrat bitmaps, no stb
// involvement at all — was fine.
//
// Costs 8 KB of internal RAM. Verify headroom with the stack high-water mark
// reported by logLvglMemDiag().
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static LGFX_TDeck lcd;
static TDeckKeyboard s_keyboard;
static RhinoConfig s_cfg;

// Notification volume as a multiplier on each board's established loudness.
//
// The scale is centred on 50%, not topped out at it: MY_VOLUME_PCT defaults to
// 50 and must sound exactly as the firmware always has, so 50 -> 1.0x, 0 ->
// silent, 100 -> 2.0x. Dividing by 100 instead would have made every device
// half as loud as before the setting existed.
//
// Headroom checks for the 2.0x end: the Pager's 7800 and the T-Deck's 2800
// sample amplitudes double to 15600 and 5600, both well inside int16. The
// Cardputer's driver volume is a uint8_t, so its caller clamps.
static inline float notifyVolumeScale() {
    return (float)cfgCoerceVolume((int)s_cfg.volumePct) / (float)MY_VOLUME_PCT;
}

static auto &displayDev() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return M5Cardputer.Display;
#else
    return lcd;
#endif
}

// Moves a top-edge header's contents in from a rounded panel's corners (see
// UI_CORNER_SAFE_X in board.h). Call after the header's own padding is set:
// this adds to it. Children placed with lv_obj_align() or flex are positioned
// inside the padding, so they all move; a centred title stays centred.
static inline void cornerSafeHeader(lv_obj_t *header) {
#if UI_CORNER_SAFE_X > 0
    if (!header) return;
    lv_obj_set_style_pad_left(
        header, lv_obj_get_style_pad_left(header, LV_PART_MAIN) + UI_CORNER_SAFE_X, 0);
    lv_obj_set_style_pad_right(
        header, lv_obj_get_style_pad_right(header, LV_PART_MAIN) + UI_CORNER_SAFE_X, 0);
#else
    (void)header;
#endif
}

// The P4's portrait status strip under the nav bar; see statusStripInit(). Here
// rather than with it because the nav bar and header, far above that, have to
// know to leave its readings out.
#if defined(DEVICE_TDISPLAY_P4) && (UI_BOTTOM_SAFE_H > 0) && (UI_PIXEL_SCALE > 1)
#  define HAS_STATUS_STRIP 1
#else
#  define HAS_STATUS_STRIP 0
#endif

uint8_t myPubKey[32] = {0};
uint8_t myPrivKey[32] = {0};
uint8_t myDeviceRole = 0;

// In LVGL pixels, which is what the draw buffer holds: panel pixels divided by
// UI_PIXEL_SCALE (see board.h).
#if defined(CZERO_WINDOW_MAX_W)
// A window, which can be resized up to this.
static constexpr uint16_t kMaxHorRes = CZERO_WINDOW_MAX_W;
#else
static constexpr uint16_t kMaxHorRes =
    ((DEVICE_LCD_LANDSCAPE_W > DEVICE_LCD_PORTRAIT_W) ? DEVICE_LCD_LANDSCAPE_W : DEVICE_LCD_PORTRAIT_W)
    / UI_PIXEL_SCALE;
#endif
#if defined(DEVICE_TLORA_PAGER_TFT)
// Larger draw buffer -> fewer blocking SPI flushes per redraw (222px / 32 ~= 7
// stripes instead of ~19), which removes the visible top-to-bottom "painting"
// on modal opens. It costs ~19KB more internal RAM, but the buffer is now
// allocated dynamically on the normal-UI path only (see setup): OTA worker mode
// runs and reboots before that allocation, so during the TLS handshake this
// buffer occupies zero bytes and OTA actually gets MORE contiguous internal
// heap than the old static 12-line buffer left it.
static constexpr uint16_t kDrawBufLines = 32;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
// Cardputer has no PSRAM and AP web config can leave very little internal DRAM.
// A smaller draw buffer trades redraw speed for RAM headroom where it matters.
static constexpr uint16_t kDrawBufLines = 24;
#elif defined(DEVICE_CROWPANEL_50)
// 800 px wide: 40 lines would be 64 KB, more than the largest internal block
// left once WiFi is up, and the PSRAM fallback renders into the same memory the
// RGB panel is scanned out of -- slow, and the contention shows as the panel
// tearing. 20 lines (32 KB) stays internal.
static constexpr uint16_t kDrawBufLines = 20;
#else
static constexpr uint16_t kDrawBufLines = 40;
#endif
// Allocated at UI init (heap_caps, internal RAM) rather than a static array so
// the OTA worker path can complete without it ever being reserved. Kept byte-
// typed because TFT builds render RGB565 while T-Deck Pro renders native I1.
static uint8_t *s_drawBufMem = nullptr;
static lv_display_t *s_lvDisplay = nullptr;
#if defined(DEVICE_TDECK)
static uint16_t *s_screenshotCaptureFrame = nullptr;
static int32_t s_screenshotCaptureW = 0;
static int32_t s_screenshotCaptureH = 0;
static bool s_screenshotCaptureActive = false;
static bool s_screenshotCaptureTouched = false;
#endif
static lv_obj_t *s_channelBtns[MESH_CHANNELS] = {};
static lv_obj_t *s_channelLabels[MESH_CHANNELS] = {};
static bool s_channelNeedsAttention[MESH_CHANNELS] = {};
// Anything unread on any channel, as against DMs.hasUnread() for the private
// ones. Several boards had grown their own copy of this loop behind their own
// feature gate (the Mesh Deck's LED, the keyboard backlight blink, the glance
// overlay); this is the ungated one the unread indicators use.
static inline bool anyChannelNeedsAttention() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (s_channelNeedsAttention[i]) return true;
    }
    return false;
}
static lv_obj_t *s_channelStrip = nullptr;
static lv_obj_t *s_channelList = nullptr;
// Where the channel list is a dropdown, it is a drawer now: pinned to the
// left edge at the chat's full height, sliding in over the chat and back out.
// Open is this flag, not the list's HIDDEN flag -- the list stays drawn while
// it slides out, and every caller asking "is the list open" means the answer
// the user just chose, not whether the last frame has left the screen yet.
static bool s_channelDrawerOpen = false;
// Dims the chat behind the open drawer and closes it on a tap, which is the
// touch boards' way out now that the selector in the header is only a label.
static lv_obj_t *s_channelDrawerScrim = nullptr;
static lv_obj_t *s_channelSelectorBtn = nullptr;
static lv_obj_t *s_channelSelectorLabel = nullptr;
static lv_obj_t *s_channelSelectorCaretLabel = nullptr;
static lv_coord_t s_channelSelectorFixedBtnW = 0;
static lv_obj_t *s_chatHeaderBar = nullptr;
static lv_obj_t *s_chatHeaderTime = nullptr;
static lv_obj_t *s_chatHeaderGps = nullptr;
static lv_obj_t *s_chatHeaderWifi = nullptr;
// Blinking envelope icon shown left of the wifi icon whenever any DM
// conversation has unread messages the user has not opened yet.
static lv_obj_t *s_chatDmAlert = nullptr;
// Unread channel traffic, immediately left of the DM envelope. The boards that
// draw a nav bar blink the bar's Chats cell instead and never build this; see
// refreshChatAlertIndicator().
static lv_obj_t *s_chatChanAlert = nullptr;
static lv_obj_t *s_chatHeaderBattText = nullptr;
static lv_obj_t *s_chatHeaderBattBar = nullptr;

// The chat header's measurements, recorded by buildUi() so that DMs, Nodes,
// Config and Tools can draw the same bar (buildScreenHeader()): same height and
// margin, same faces, the screen's name where the channel name goes. Rebuilt
// with the UI, so a font or orientation change reaches both.
struct ChatHeaderGeom {
    int margin;                 // panelMargin: the bar's inset from the panel's sides
    int top;                    // headerTop: the bar's inset from the top edge
    int h;                      // chatHeaderH
    int titleX;                 // where the channel name starts, inside the bar
    int padX;                   // the battery's inset from the bar's right end
    lv_coord_t textYOffset;     // the name's nudge down from centre
    int battDotW, battDotH;
    const lv_font_t *clockFont; // the clock, and the channel name
    const lv_font_t *textFont;  // battery reading and GPS
    const lv_font_t *iconFont;  // Wi-Fi (touch boards)
};
static ChatHeaderGeom s_chatHdrGeom = {};

// One copy of the header per open screen. A slot frees itself when its bar is
// deleted (onScreenHeaderDeleted()), so a screen closing without a word to the
// header code cannot leave a dangling pointer for refreshHeaderStatus().
struct ScreenHeader {
    lv_obj_t *bar;
    lv_obj_t *title;
    lv_obj_t *time;
    lv_obj_t *gps;
    lv_obj_t *wifi;
    lv_obj_t *battDot;
    lv_obj_t *battText;
    int barX;                   // the bar's left edge on the display
    int barW;
    // Kept here, not read back from the label: LONG_DOT can leave the label
    // holding a shortened copy, and the fit is measured on the whole name.
    char titleText[96];
};
static constexpr int kScreenHeaderSlots = 4;
static ScreenHeader s_screenHeaders[kScreenHeaderSlots] = {};
static lv_obj_t *s_chatPanel = nullptr;
static lv_obj_t *s_chatList = nullptr;
static lv_obj_t *s_chatNewMsgBtn = nullptr;
static lv_obj_t *s_chatNewMsgLabel = nullptr;
static lv_obj_t *s_chatActBtn = nullptr;
// The row under the chat that holds Actions and New Message (touch-only builds).
static lv_obj_t *s_chatBtnRow = nullptr;
static lv_obj_t *s_chatShortcutBar = nullptr;
static lv_obj_t *s_chatShortcutText = nullptr;
#if HAS_SLEEP_OVERLAY
static lv_obj_t *s_tdeckProSleepOverlay = nullptr;
// The glance header: wordmark, node name and clock on the left, conditions
// opposite, with the date/battery band above. Grouped into a struct because it
// is built more than once -- the lock screen owns one, and on the M9 the home
// screen owns another showing the same information while the device is awake.
// Two copies of this block would drift the moment either was touched.
struct GlanceHeader {
    lv_obj_t *title;
    lv_obj_t *node;
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *batt;
    lv_obj_t *gps;      // status band, left of the battery
    lv_obj_t *wifi;     // status band, between GPS and the battery
#if HAS_WEATHER
    lv_obj_t *wxDesc;   // conditions, opposite the node name
    lv_obj_t *wxTemp;   // temperature, opposite the clock
    lv_obj_t *wxRule;   // the divider between the two columns, when there are two
    // Set by the caller before building: weather goes under the clock, as a
    // fuller block beneath a rule, instead of in a column beside it. Nothing
    // asks for it at present (glanceSetHomeShape() sets it false).
    bool      wxBelow;
    lv_obj_t *wxBelowRule;
    lv_obj_t *wxBelowText;
    // Set by the caller before building, like wxBelow: the hero as two cells
    // across a wide panel, each with its reading on the left and the detail on
    // the right in a smaller face -- node name and clock beside this node's
    // ID, short name, firmware and role; conditions and temperature beside the
    // fuller weather. P4 landscape only, on Home and the lock screen alike
    // (glanceSetHomeShape(), alignGlanceWide()).
    bool      wideHero;
    lv_obj_t *nodeInfo;   // wideHero: this node's particulars, right of the clock
    lv_obj_t *wxDetail;   // wideHero: feels-like, wind, where and when
    // The Home dashboard's way into the Weather screen: a box over whichever
    // labels above are carrying the reading, tapped, or focused with Up and
    // opened with Enter. Built and placed by the dashboard (buildHomeDashWxHit,
    // homeDashPlaceWxHit), never by buildGlanceHeader() -- the lock screen's
    // header takes no input, so its copy stays null.
    lv_obj_t *wxHit;
#endif
    // What the two icons above are currently showing, from glanceStatusIcons-
    // Key(). The clock and the battery are worth a repaint once a minute; GPS
    // and Wi-Fi are worth one when they change, which is the whole reason to
    // put them on a screen someone looks at to check exactly that. Zero means
    // "nothing painted yet" — the key function never returns it.
    uint16_t statusKey;
    // Which palette this header was built in, so a later repaint reaches for
    // the same one. The flag rather than a copy of the colours: one source of
    // truth, and glancePalette() is a handful of field reads.
    bool themed;
};
static GlanceHeader s_sleepGlance = {};
#if HAS_HOME_DASHBOARD
// The home dashboard's own header. A second instance rather than a shared one:
// both surfaces can exist at once — the idle timeout can put the lock screen up
// over a dashboard that is still built underneath — and one set of pointers
// would have the later build steal the earlier screen's labels.
static GlanceHeader s_homeGlance = {};
#endif
static uint32_t s_tdeckProSleepMinuteKey = UINT32_MAX;

// The notification area under the date: newest unread message previews first.
// Each first line uses separate labels for time/channel, sender and body so the
// Wio lock screen can color those semantic fields independently.
// How many messages the ring holds and the panel has labels for. Eight, because
// eight one-line messages is the most the line budget below can display; a
// screen full of two-line messages uses only four of them.
static constexpr int kTdeckProSleepMsgSlots = 8;
static lv_obj_t *s_tdeckProSleepMsgBold[kTdeckProSleepMsgSlots] = {nullptr};
static lv_obj_t *s_tdeckProSleepMsgSender[kTdeckProSleepMsgSlots] = {nullptr};
static lv_obj_t *s_tdeckProSleepMsgRest[kTdeckProSleepMsgSlots] = {nullptr};
// The wrapped remainder, on its own label because it starts at a different x
// than the first line's body does -- one label cannot indent its second line
// differently from its first.
static lv_obj_t *s_tdeckProSleepMsgCont[kTdeckProSleepMsgSlots] = {nullptr};
#if FEATURE_LOCK_SCREEN
// The carousel page the message rows live on, when the lock screen managed to
// build one. nullptr means they are children of the overlay itself and carry
// kTdeckProMsgTop in their own y -- which is what the Pro always does, and what
// a band too short for a carousel falls back to.
static lv_obj_t *s_lockMsgPage = nullptr;
#endif

// Unread state the overlay is currently showing, plus the coalescing timer that
// decides when a change is worth a panel refresh. See serviceTdeckProSleepClock.
// 64-bit because the key now carries a message sequence number as well as the
// unread counts -- see tdeckProSleepUnreadKey().
static uint64_t s_tdeckProSleepUnreadKey = UINT64_MAX;
static uint64_t s_tdeckProSleepUnreadPendingKey = UINT64_MAX;
static uint32_t s_tdeckProSleepUnreadChangedMs = 0;

// Neither Channels nor DMs can hand back "the last three messages". Both store
// DisplayLine rows, which are rendered, word-wrapped display text: the prefix
// ("14:32 [Alice] ") is baked into the first row's string and the body is split
// across however many rows it needed. Recovering the four fields from that means
// stripping a prefix whose length depends on the sender's name as it was when
// the message arrived, then re-joining continuations -- brittle enough to reject.
//
// So keep them structured here instead, filled at the two points where an
// inbound message actually lands. T-Deck Pro only: no other build draws the
// overlay, and none pays the ~450 bytes.
struct TdeckProRecentMsg {
    uint32_t epoch;          // 0 = clock was unset when it arrived; row omits the time
    uint32_t senderNodeId;
    int8_t   chanIdx;        // -1 = DM
    uint32_t packetId;       // 0 = none; the lock screen's long press acts on it
    char     text[144];
};
static TdeckProRecentMsg s_tdeckProRecentMsgs[kTdeckProSleepMsgSlots];
static uint8_t  s_tdeckProRecentMsgHead = 0;   // next slot to write
static uint8_t  s_tdeckProRecentMsgCount = 0;
// Monotonic, and the whole point of the repaint key change: a second message on
// an already-unread channel moves no count and sets no new attention bit, so
// nothing else in the unread state notices it.
static uint32_t s_tdeckProRecentMsgSeq = 0;
#endif

#if FEATURE_LOCK_SCREEN
// ── Lock screen state ────────────────────────────────────────────────────────
// A third display state, between the UI and a dark panel:
//
//   UI ──(screen-off gesture or idle timeout)──> LOCKED ──(dwell)──> ASLEEP
//    ^                                               │                       │
//    └────────────(allowed wake gesture)─────────────┘                       │
//    └────────────────────(allowed wake gesture)─────────────────────────────┘
//
// LOCKED is a *lit* state, and that is the whole reason it cannot be modelled
// the way the T-Deck Pro models its sleep clock. s_screenAsleep stays false
// here: the backlight is on, LVGL has to keep running for the clock to tick,
// and powerSaveShouldNap() must keep refusing to light-sleep — it already
// requires s_screenAsleep, so it refuses on its own without a change.
//
// The cost of the state is why lockScreenOffSecs exists. Nothing about a lit
// LCD is free, so the lock screen is deliberately a thing that ends.
static bool     s_lockScreenActive = false;
static uint32_t s_lockScreenSinceMs = 0;
#endif

// Right-hand cluster of the bottom bar on builds where the nav buttons share it
// with the GPS/WiFi/DM icons. nullptr everywhere else.
static lv_obj_t *s_chatStatusBox = nullptr;
#if HAS_NAV_BAR_TOGGLE
// The same cluster on the copy of the bar that Config/DM/Nodes/Live carry. They
// have to be their own objects rather than the chat screen's moved across: the
// chat screen is still built underneath, and an object lives in exactly one
// parent. Both sets are painted from one set of values in refreshHeaderStatus().
static lv_obj_t *s_navStatusBox = nullptr;
static lv_obj_t *s_navStatusGps = nullptr;
static lv_obj_t *s_navStatusWifi = nullptr;
static lv_obj_t *s_navStatusDm = nullptr;
static lv_obj_t *s_navStatusChan = nullptr;

// Called by every modal that carries a bar, as it closes. Explicit rather than
// leaning on lv_obj_is_valid(): the modal takes these objects with it, and LVGL
// hands the freed addresses straight back to whatever is allocated next, so a
// stale pointer can come back "valid" as something else entirely.
static inline void clearNavBarStatusIcons() {
    s_navStatusBox = nullptr;
    s_navStatusGps = nullptr;
    s_navStatusWifi = nullptr;
    s_navStatusDm = nullptr;
    s_navStatusChan = nullptr;
}

// The backstop that makes forgetting the call above survivable. Every screen
// carrying a bar has to null these as it closes, and that is a rule a new screen
// can only obey by knowing about it: Weather has carried a bar since long before
// Tools and Help did and never nulled them, and Tools and Help arrived with the
// same omission. The symptom is not subtle — refreshHeaderStatus() runs a few
// times a second and writes straight into the freed label, which is a
// LoadProhibited panic within a second of closing the screen.
//
// LVGL raises LV_EVENT_DELETE while the object is still addressable, so this
// fires before the allocator can hand the memory to anything else. That is what
// distinguishes it from the lv_obj_is_valid() check the note above rules out.
//
// Guarded on identity rather than nulling unconditionally: if a new cluster is
// ever built before the old one is deleted, the statics already point at the new
// box, and an unconditional clear here would null the live pointers on the old
// one's way out. Every current caller tears down before it builds, so this
// guard costs a comparison and removes a whole ordering assumption.
static void onNavStatusClusterDeleted(lv_event_t *e) {
    if (lv_event_get_target_obj(e) == s_navStatusBox) clearNavBarStatusIcons();
}
#else
static inline void clearNavBarStatusIcons() {}
#endif
static lv_obj_t *s_rootScreen = nullptr;
static lv_obj_t *s_composeModal = nullptr;
static lv_obj_t *s_composeInput = nullptr;
static lv_obj_t *s_composeKeyboard = nullptr;
static lv_obj_t *s_emojiPickerBackdrop = nullptr;
static lv_obj_t *s_emojiPickerModal = nullptr;
static int s_emojiPickerSelection = 0;
// Send mode: picking fires a one-emoji message and closes the tray (the 'e'
// browse shortcut on keyboard builds). Insert mode: picking appends to the open
// compose box and keeps the tray up (the touch-only in-compose 😀 button).
static bool s_emojiPickerSendMode = false;
// Cells per row in the tray, computed when it is built (see openEmojiPicker), so
// up/down step a row instead of a cell.
static int s_emojiPickerCols = 1;
// Cell edge in px, as openEmojiPicker() last built the tray; the page buttons
// step in rows of this plus the 2 px row gap.
static int s_emojiPickerCell = 30;
// Hold-to-repeat: the direction the held key is stepping, and when it last
// stepped. Driven from the loop because a held key produces no further key
// events to hang this off; see serviceEmojiPickerRepeat.
static int      s_emojiPickerRepeatDelta = 0;
// Whether stepping runs off one end of the grid onto the other. Set when the
// tray is built, from the board: it is a property of what is driving the
// selection rather than of the tray itself. A wheel has no ends to stop at --
// stopping dead at the last cell is the wheel telling you it is broken -- where
// a d-pad's edges are real and running off them would lose your place.
static bool     s_emojiPickerWrap = false;
static uint32_t s_emojiPickerRepeatLastMs = 0;
// Packet id the pick reacts to. Non-zero when the tray was opened in send mode
// with the chat cursor sitting on a message: the pick then goes out as a tapback
// (reply_id + emoji flag) on that message instead of a standalone message.
static uint32_t s_emojiPickerTapbackId = 0;
static lv_obj_t *s_composeCharCount = nullptr;
#if HAS_COMPOSE_EMOJI_BTN
// The emoji button that sits to the right of the message box, on every keyboard
// build where something can aim at it. A tap is the whole story on the boards
// with a touch panel.
static lv_obj_t *s_composeEmojiBtn = nullptr;
#endif
#if defined(DEVICE_TLORA_PAGER_TFT)
// Whether the wheel has stepped onto that button. The Pager is the one board
// here reaching it without a finger: it has no touch panel and no spare key, so
// the caret walks right through the message and the detent past the last
// character moves to the button instead of doing nothing. Enter there opens the
// tray.
static bool      s_composeEmojiFocused = false;
#endif
static lv_obj_t *s_cfgModal = nullptr;
static lv_obj_t *s_cfgActionList = nullptr;
static lv_obj_t *s_cfgInfoList = nullptr;
// The title in Config's screen header, which also shows the armed filter.
static lv_obj_t *s_cfgHeaderStatus = nullptr;
// Config row filter, driven exactly like the Nodes one: the spacebar arms it,
// printable keys narrow it, backspace edits and then disarms it. Rows are
// matched on the label the user can actually see, so what they type is what
// they are reading.
static constexpr int kCfgFilterMax = 20;
static char s_cfgFilter[kCfgFilterMax + 1] = {};
static int  s_cfgFilterLen = 0;
static bool s_cfgFilterOpen = false;
// WiFi picker modal layered over CFG for selecting preferred known network.
static lv_obj_t *s_cfgWifiBackdrop = nullptr;
static lv_obj_t *s_cfgWifiModal = nullptr;
static lv_obj_t *s_cfgWifiList = nullptr;
// The shortcut line under the title. Doubles as the picker's status line: a
// delete has no other place to report itself, and the row simply vanishing is
// not feedback when the entry deleted was already "(not set)".
static lv_obj_t *s_cfgWifiHint = nullptr;
// Delete confirmation, layered over the picker. Its own dialog rather than
// openCfgConfirmModal(): that one is keyed to CFG_ACTION ids and returns to the
// CFG screen on either answer, which would tear down the picker underneath.
static lv_obj_t *s_cfgWifiDelBackdrop = nullptr;
static lv_obj_t *s_cfgWifiDelModal = nullptr;
#if HAS_BLE_KEYBOARD
// Pairing dialog for an external Bluetooth keyboard. Modelled on the WiFi scan
// modal below, because scan -> pick -> pair -> remember is the same shape as
// scan -> pick -> password -> connect.
static lv_obj_t *s_cfgBleKbdBackdrop = nullptr;
static lv_obj_t *s_cfgBleKbdModal = nullptr;
static lv_obj_t *s_cfgBleKbdList = nullptr;
static lv_obj_t *s_cfgBleKbdStatus = nullptr;
static int       s_cfgBleKbdSelection = 0;
static uint32_t  s_cfgBleKbdPaintMs = 0;
// The dialog scans as soon as it opens, but opening it right after switching
// the feature on can beat the radio coming up -- the worker starts the stack
// asynchronously. So the request is retried from the loop until one takes,
// rather than silently doing nothing and waiting for the user to press Scan.
static bool      s_cfgBleKbdAutoScanned = false;
// "Keyboard lost its pairing" prompt. Raised from the loop whenever the module
// refuses a bonded keyboard that came back without its bond, from any screen.
static lv_obj_t *s_bleKbdRepairBackdrop = nullptr;
static lv_obj_t *s_bleKbdRepairModal = nullptr;
static lv_obj_t *s_bleKbdRepairBtns[2] = {nullptr, nullptr};   // Re-pair, Ignore
static int       s_bleKbdRepairSel = 1;                         // Ignore by default
#endif
static lv_obj_t *s_cfgWifiScanBackdrop = nullptr;
static lv_obj_t *s_cfgWifiScanModal = nullptr;
static lv_obj_t *s_cfgWifiScanList = nullptr;
static lv_obj_t *s_cfgWifiScanStatus = nullptr;
static lv_obj_t *s_cfgWifiPassBackdrop = nullptr;
static lv_obj_t *s_cfgWifiPassModal = nullptr;
static lv_obj_t *s_cfgWifiPassInput = nullptr;
static lv_obj_t *s_cfgWifiPassKeyboard = nullptr;
static lv_obj_t *s_cfgWifiPassStatus = nullptr;
static bool s_cfgWifiPickerOnboardingMode = false;
// Node identity editor, opened from the Config screen. Both names live in one
// modal because they are one setting: a short name is only ever chosen against
// the long name it abbreviates, and the mesh learns the pair together.
static lv_obj_t *s_cfgNodeNameBackdrop = nullptr;
static lv_obj_t *s_cfgNodeNameModal = nullptr;
static lv_obj_t *s_cfgNodeNameLongInput = nullptr;
static lv_obj_t *s_cfgNodeNameShortInput = nullptr;
static lv_obj_t *s_cfgNodeNameKeyboard = nullptr;
static lv_obj_t *s_cfgNodeNameStatus = nullptr;
// Which field the hardware keyboard types into: 0 = long name, 1 = short name.
// Boards with a touch panel set it from the field that was tapped.
static int s_cfgNodeNameFocus = 0;
// Yes/No confirmation dialog layered over the CFG modal for destructive actions.
static lv_obj_t *s_cfgConfirmBackdrop = nullptr;
#if HAS_ADMIN_TERMINAL
// The remote admin terminal's handles. Up here with the other modal pointers
// rather than beside the code that builds them: the keyboard pump asks whether
// this modal is up, and it sits well above that.
static lv_obj_t *s_adminBackdrop = nullptr;
static lv_obj_t *s_adminModal    = nullptr;
static lv_obj_t *s_adminScroll   = nullptr;
static lv_obj_t *s_adminInput    = nullptr;
static lv_obj_t *s_adminTitle    = nullptr;
#if UI_TOUCH_ONLY_PROFILE
static lv_obj_t *s_adminKeyboard = nullptr;
#endif
#endif
static lv_obj_t *s_cfgConfirmModal = nullptr;
// The message box inside it. Held because the dialog's text is scrollable now --
// the SD scan names the files it is about to delete and can run past the bottom
// of the panel -- and the key path has to have something to scroll.
static lv_obj_t *s_cfgConfirmScroll = nullptr;
static lv_obj_t *s_otaPromptBackdrop = nullptr;
static lv_obj_t *s_otaPromptModal = nullptr;
#if HAS_STATE_MAPS
static lv_obj_t *s_legacyMapPromptBackdrop = nullptr;
static lv_obj_t *s_legacyMapPromptModal = nullptr;
static lv_obj_t *s_legacyMapPromptStatus = nullptr;
static bool s_legacyMapMigrationChecked = false;
static uint32_t s_legacyMapMigrationNextCheckMs = 0;
#endif
static bool s_otaAutoCheckDone = false;     // one automatic check attempt per boot
// Settle delay after the station associates, before the release check fires.
static constexpr uint32_t kOtaAutoCheckSettleMs = 8000;
static char s_otaAutoCheckTag[48] = {};     // release tag the prompt is offering
// An explicit "check now", raised by a release-channel switch. Serviced by the
// same loop routine as the boot check, but outside its policy: it runs whether
// or not the boot check has been spent and whether or not the on-boot
// preference is set, because the user has just asked for it by hand.
static bool s_otaCheckRequested = false;
static uint32_t s_otaCheckRequestedDueMs = 0;
// How the release-channel row reports a check: " - checking...", then the
// outcome. The row rather than a popup, because openCfgActionMessageModal() is
// dismissed by *any* key (see the s_cfgActionMsgModal arm of the key pump), and
// a check blocks loop() for as long as the HTTP request takes -- so keys
// pressed at what looks like a frozen device drain the moment it resumes and
// can take the popup with them. The row keeps saying it until something else
// changes it.
static char s_otaCheckRowNote[48] = "";
// A request cannot run while a dialog is up. Rather than sit pending for the
// rest of the boot with the row stuck on "checking...", it gives up after this.
static uint32_t s_otaCheckDeadlineMs = 0;
static constexpr uint32_t kOtaCheckRequestLifetimeMs = 30000;
// The station is already associated when a request arrives, so this only has to
// swallow a rapid double-toggle -- nothing like the boot path's 8 s DNS settle.
// Every request re-arms it, so Stable -> Alpha -> Stable inside the window is
// one check, against the channel the user actually landed on.
static constexpr uint32_t kOtaManualCheckSettleMs = 1000;
// Set from NVS at boot when the web-config save requested a check before
// rebooting. Lets the boot check run even with "Check for Updates on Boot" off.
static bool s_otaBootCheckForced = false;
static int s_cfgConfirmPendingAction = -1;
// Readable action-result popup over CFG; replaces truncated header notices.
static lv_obj_t *s_cfgActionMsgBackdrop = nullptr;
static lv_obj_t *s_cfgActionMsgModal = nullptr;
static uint32_t s_cfgActionMsgOpenedMs = 0;
// Own-message color picker (16 basic swatches), reachable from the CFG screen.
static lv_obj_t *s_cfgBrightBackdrop = nullptr;
static lv_obj_t *s_cfgBrightModal = nullptr;
static lv_obj_t *s_cfgBrightSlider = nullptr;
static lv_obj_t *s_cfgBrightValue = nullptr;
static uint8_t   s_cfgBrightOriginal = 0;   // restored if the user cancels
#if FEATURE_LOCK_SCREEN
// The lock screen's own level, edited in the same modal. Two rows rather than
// two modals because they are read against each other -- the useful question is
// how much dimmer the glance surface is than the UI, which you cannot judge one
// screen at a time.
static lv_obj_t *s_cfgBrightLockSlider = nullptr;
static lv_obj_t *s_cfgBrightLockValue = nullptr;
static lv_obj_t *s_cfgBrightScreenLabel = nullptr;
static lv_obj_t *s_cfgBrightLockLabel = nullptr;
static uint8_t   s_cfgBrightLockOriginal = 0;
static int       s_cfgBrightFocus = 0;      // 0 = screen, 1 = lock screen
#endif
// Battery voltage trim, reachable from the CFG screen.
static lv_obj_t *s_cfgBattCalBackdrop = nullptr;
static lv_obj_t *s_cfgBattCalModal = nullptr;
static lv_obj_t *s_cfgBattCalSlider = nullptr;
static lv_obj_t *s_cfgBattCalValue = nullptr;   // live "4.05 V   82%"
static lv_obj_t *s_cfgBattCalDetail = nullptr;  // untrimmed reading + trim
static int16_t   s_cfgBattCalOriginal = 0;      // restored if the user cancels
// Shared "pick one stop from an ordered list" modal. Two settings use it —
// location precision and the light timeout — and both are a scale rather than a
// set of unrelated choices, which is what makes a slider the right control and
// a cycling row the wrong one. The slider's range is list indices, so it can
// never land between two real values.
// Deliberately outside HAS_VOLUME_CONTROL: neither setting is audio-gated, so
// the boards without a volume control still build and use this.
struct CfgSliderPicker {
    const char *title;
    int         count;
    // Text for the big value label at the stop. Returns a pointer to storage
    // owned by the callee — the label copies it immediately.
    const char *(*labelFor)(int idx);
    // Commit. Called only from the Save path, never while dragging.
    void        (*apply)(int idx);
    const char *leftEnd;    // caption under the low end
    const char *rightEnd;   // caption under the high end
    // Optional checkbox under the slider, for a yes/no that belongs to the
    // same decision. Both null (the default for every spec that leaves them
    // out) and there is no checkbox. Staged like the slider: *checkValue is
    // written on Save, before apply() runs, and never on Cancel.
    //
    // With a checkbox and count 1 there is no slider at all: the picker is a
    // checkbox under a value label, and labelFor() may read
    // s_cfgSliderCheckStaged to show what the box changes.
    const char *checkLabel;
    bool       *checkValue;
};

static lv_obj_t *s_cfgSliderBackdrop = nullptr;
static lv_obj_t *s_cfgSliderModal = nullptr;
static lv_obj_t *s_cfgSliderCtl = nullptr;
static lv_obj_t *s_cfgSliderValue = nullptr;
static const CfgSliderPicker *s_cfgSliderSpec = nullptr;
// Index while the slider is open. Staged rather than written straight into
// s_cfg: for location precision a scheduled announce can fire mid-drag, and it
// must go out at the value the user actually chose, not at whatever the knob
// happens to be resting on.
static int       s_cfgSliderStaged = 0;
static lv_obj_t *s_cfgSliderCheck = nullptr;
static bool      s_cfgSliderCheckStaged = false;
#if UI_ARROW_GRID_NAV
// Which control the arrows are on when the picker has a checkbox: 0 the slider,
// 1 the checkbox. Up/Down move between them and Left/Right change the focused
// one, the way the Brightness screen's two rows work.
static int       s_cfgSliderFocus = 0;
#endif

#if HAS_VOLUME_CONTROL
static lv_obj_t *s_cfgVolBackdrop = nullptr;
static lv_obj_t *s_cfgVolModal = nullptr;
static lv_obj_t *s_cfgVolSlider = nullptr;
static lv_obj_t *s_cfgVolValue = nullptr;
static uint8_t   s_cfgVolOriginal = 0;      // restored if the user cancels
static uint32_t  s_cfgVolLastPreviewMs = 0; // rate-limits the audible preview
#endif
static lv_obj_t *s_cfgColorBackdrop = nullptr;
static lv_obj_t *s_cfgColorModal = nullptr;
static lv_obj_t *s_cfgColorGrid = nullptr;
static int s_cfgColorSelection = 0;

// Chat-style picker (Classic / Bubbles / Outline), reachable from the CFG screen.
static lv_obj_t *s_chatStyleBackdrop = nullptr;
static lv_obj_t *s_chatStyleModal = nullptr;
static lv_obj_t *s_chatStyleRows[CHAT_STYLE_MAX + 1] = {};
static int s_chatStyleSelection = 0;

static lv_obj_t *s_alertSoundBackdrop = nullptr;
static lv_obj_t *s_alertSoundModal = nullptr;
static lv_obj_t *s_alertSoundRows[MSG_ALERT_SOUND_MAX + 1] = {};
static int s_alertSoundSelection = 0;
static uint8_t s_alertSoundOriginal = 0;   // restored if the picker is cancelled

#if HAS_NOTIFY_LED
static constexpr int kNotifyLedColorOptionCount = (int)NOTIFY_LED_COLOR_OFF + 1;
static constexpr int kNotifyLedColorCols = 3;
static constexpr int kNotifyLedColorCellW = 52;
static constexpr int kNotifyLedColorCellH = 34;
static constexpr int kNotifyLedColorCellGap = 6;
static constexpr int kNotifyLedColorGridW =
    kNotifyLedColorCols * kNotifyLedColorCellW
    + (kNotifyLedColorCols - 1) * kNotifyLedColorCellGap;
static lv_obj_t *s_notifyLedColorBackdrop = nullptr;
static lv_obj_t *s_notifyLedColorModal = nullptr;
static lv_obj_t *s_notifyLedColorRows[kNotifyLedColorOptionCount] = {};
static int s_notifyLedColorSelection = NOTIFY_LED_COLOR_BLUE;
static bool s_notifyLedColorTargetDm = false;
#endif

static lv_obj_t *s_chatNameBackdrop = nullptr;
static lv_obj_t *s_chatNameModal = nullptr;
static lv_obj_t *s_chatNameRows[CHAT_NAME_MAX + 1] = {};
static int s_chatNameSelection = 0;

static lv_obj_t *s_fontSizeBackdrop = nullptr;
static lv_obj_t *s_fontSizeModal = nullptr;
static lv_obj_t *s_fontSizeRows[FONT_SIZE_MAX + 1] = {};
static int s_fontSizeSelection = 0;
static lv_obj_t *s_sigPolicyBackdrop = nullptr;
static lv_obj_t *s_sigPolicyModal = nullptr;
static lv_obj_t *s_sigPolicyRows[SIG_POLICY_COUNT] = {};
static int s_sigPolicySelection = 0;
// sigPolicyName()'s spellings, marked here for translation; that header stays
// free of the UI. Each with the consequence the picker and status line show.
static const char *const kSigPolicyLabel[SIG_POLICY_COUNT] = {
    TR_NOOP("Compatible"), TR_NOOP("Balanced"), TR_NOOP("Strict"),
};
static const char *const kSigPolicyDesc[SIG_POLICY_COUNT] = {
    TR_NOOP("Unsigned packets accepted"),
    TR_NOOP("Unsigned broadcasts from signers dropped"),
    TR_NOOP("Only signed packets accepted (no 2.7 nodes)"),
};

struct KnownWifiEntry {
    char ssid[sizeof(RhinoConfig::wifiSsid)];
    char pass[sizeof(RhinoConfig::wifiPass)];
};

struct ScannedWifiEntry {
    char ssid[33];
    int32_t rssi;
    bool secure;
};

// Shared with the YAML importer's scratch array — see CFG_SAVED_WIFI_MAX.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
// One network at a time on the Cardputer. It has no PSRAM and ~23 KB of
// internal heap once Wi-Fi is up — the reason its web config is lite-only and
// Discovery is compiled out — so it does not carry a remembered-network list,
// its NVS blob, or the picker rows for it. The configured network and the AP
// entry are the whole list.
static constexpr int kKnownWifiExtraCount = 0;
#else
static constexpr int kKnownWifiExtraCount = CFG_SAVED_WIFI_MAX;
#endif
// Configured network + the "AP" entry + the remembered ones. This was 1 + extras
// and so one slot short: with the list full, populateKnownWifiEntries() ran out
// of room and the last remembered network never appeared in the picker.
static constexpr int kKnownWifiMaxCount = 2 + kKnownWifiExtraCount;
// Never zero: a zero-length array is a GNU extension, and every loop below is
// bounded by kKnownWifiExtraCount rather than by this, so the spare slot on
// Cardputer is allocated but never written.
static constexpr int kKnownWifiSlotAlloc =
    (kKnownWifiExtraCount > 0) ? kKnownWifiExtraCount : 1;
static constexpr int kScannedWifiMaxCount = 20;

static KnownWifiEntry s_cfgKnownWifi[kKnownWifiMaxCount] = {};
static int s_cfgKnownWifiCount = 0;
static int s_cfgWifiSelection = 0;
static int s_cfgWifiScanSelection = 0;
// Sentinel SSID for the WiFi picker's "AP" entry. Selecting it doesn't join a
// network — it forces the device's own SoftAP (see wifiForceApMode()).
static const char kForceApSsid[] = "AP";
static bool s_wifiUsingKnownOverride = false;
// Networks remembered besides the configured one, so switching between them
// does not forget where you were. Persisted (see saveKnownWifiNetworks): this
// list used to be RAM only, which is why joining a second network lost the
// first one on the next boot. Oldest is evicted when it fills up.
static KnownWifiEntry s_cfgKnownWifiAdded[kKnownWifiSlotAlloc] = {};
static int s_cfgKnownWifiAddedCount = 0;
static void saveKnownWifiNetworks();
static void loadKnownWifiNetworks();
// Make `ssid` the configured network, remembering whichever one it replaces.
static void wifiSetConfiguredNetwork(const char *ssid, const char *pass);
// Next station reconnect attempt. Declared here with the other WiFi state
// rather than beside serviceWifiStation(): the saved-network bridge below
// clears it to re-associate promptly after a switch.
static uint32_t s_wifiStaKickMs       = 0;
static ScannedWifiEntry s_cfgScannedWifi[kScannedWifiMaxCount] = {};
static int s_cfgScannedWifiCount = 0;
static char s_cfgWifiPassTargetSsid[33] = {};
static char s_wifiSelectedSsid[sizeof(RhinoConfig::wifiSsid)] = {};
static char s_wifiSelectedPass[sizeof(RhinoConfig::wifiPass)] = {};

// First-boot onboarding modal — shown once after a fresh flash (no NVS state).
// Stage 0: (only if SD config present) ask whether to import it.
// Stage 1: prompt for the node's long name.
// Stage 2: prompt for the node's short name (pre-filled from long name).
// Stage 3: pick the radio region/preset (US default).
// Stage 4: pick the device role (CLIENT_MUTE default).
// Stage 5: optional WiFi chooser, then commit everything + reboot.
static lv_obj_t *s_onboardingBackdrop = nullptr;
static lv_obj_t *s_onboardingModal = nullptr;
static lv_obj_t *s_onboardingInput = nullptr;
static lv_obj_t *s_onboardingKeyboard = nullptr;
static lv_obj_t *s_onboardingStatus = nullptr;
static lv_obj_t *s_onboardingPickLabel = nullptr;  // value shown on region/role picker stages
enum OnboardingStage : uint8_t {
    ONBOARD_STAGE_ASK_IMPORT      = 0,
    ONBOARD_STAGE_ENTER_LONG      = 1,
    ONBOARD_STAGE_ENTER_SHORT     = 2,
    ONBOARD_STAGE_SELECT_REGION   = 3,
    ONBOARD_STAGE_SELECT_ROLE     = 4,
    ONBOARD_STAGE_CHOOSE_WIFI     = 5,
};
static uint8_t s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
static bool s_onboardingSdConfigPresent = false;

// The Tanmatsu has no touch screen, so its onboarding buttons take a keyboard
// focus: Left/Right move it, Enter presses the focused one. Each stage's
// buttons register here as they are built (renderOnboardingStage()); the
// focus starts on the last one, the stage's primary action.
#if UI_TANMATSU_LAYOUT
#define ONBOARD_BUTTON_FOCUS 1
#else
#define ONBOARD_BUTTON_FOCUS 0
#endif
#if ONBOARD_BUTTON_FOCUS
static constexpr int kOnboardingMaxBtns = 4;
static lv_obj_t *s_onboardingBtns[kOnboardingMaxBtns] = {};
static lv_event_cb_t s_onboardingBtnCbs[kOnboardingMaxBtns] = {};
static int s_onboardingBtnCount = 0;
static int s_onboardingBtnFocus = 0;
#endif
static bool s_firstBoot = false;

// A freshly flashed device has no identity of its own yet: it would announce
// the compiled-in defaults — long name "Camillia", short "CaMi", and the
// hardcoded fallback coordinates in config.h — to the whole mesh, and to MQTT
// through any uplinking gateway. Nothing automatic goes on the air until the
// user has finished onboarding or restored a config.
//
// s_firstBoot is exactly that condition: set at boot when no persisted
// nodeLong exists, and both completion paths (onboardingFinalize() and
// onboardingAcceptImport()) persist and reboot, so it is false from the next
// boot onwards. The Heltec build that skips onboarding clears it itself.
//
// Receiving and relaying are untouched — a relayed packet carries other
// people's identities, not ours, and a device is useful as a relay during
// setup. This is only about what the device says it is.
static inline bool announceHeldForOnboarding() { return s_firstBoot; }

// ── Announce hold while the web UI is in use ────────────────────────────────
// A scheduled announce is seconds of blocking airtime — a NodeInfo at SF11
// measured 6.4 s, and the telemetry behind it another 0.6 s. webCfgLoop() runs
// once per main loop pass, so for that whole time no HTTP request is looked at:
// a browser opening the config page lands in the gap, waits, and gives up. That
// is what "the web config will not come up" turned out to be — the log line is
// "[web] main loop was busy for 7067 ms - socket unserviced", with the browser
// hanging up mid-page immediately after.
//
// So: while someone is demonstrably using the page, let a due announce wait.
// Returning early leaves the schedule untouched, so it stays due and goes out
// on a later pass rather than being skipped.
//
// Two bounds keep this from turning into "a node that never announces". The
// window is short — a request within the last 10 s — so a closed page frees the
// radio almost at once. And the hold itself is capped: the chat tab polls every
// two seconds, which would otherwise read as "busy" forever, so after two
// minutes of continuous holding the announce goes out regardless. A poll landing
// in that transmit fails, the client retries two seconds later, and nothing is
// lost; a page *load* landing in it is the failure this exists to prevent.
static constexpr uint32_t kAnnounceWebBusyWindowMs = 10000;
static constexpr uint32_t kAnnounceWebHoldMaxMs    = 120000;
static uint32_t s_announceWebHoldSinceMs = 0;

static bool announceHeldForWebCfg(uint32_t nowMs) {
    if (webCfgMsSinceRequest() >= kAnnounceWebBusyWindowMs) {
        s_announceWebHoldSinceMs = 0;   // page idle or server down: no hold
        return false;
    }
    if (s_announceWebHoldSinceMs == 0) s_announceWebHoldSinceMs = nowMs;
    if ((uint32_t)(nowMs - s_announceWebHoldSinceMs) >= kAnnounceWebHoldMaxMs) {
        // Held long enough. Clear the start stamp so the next hold is measured
        // from now, not from the beginning of this one — otherwise the cap
        // stays tripped and the hold never applies again.
        s_announceWebHoldSinceMs = 0;
        return false;
    }
    return true;
}

// Node name / region / role / WiFi are buffered in scratch while the user steps
// through the wizard, so s_cfg isn't touched until everything is confirmed at
// the final stage (which then persists and reboots).
static char s_onboardingLongScratch[sizeof(RhinoConfig::nodeLong)] = {0};
static char s_onboardingShortScratch[sizeof(RhinoConfig::nodeShort)] = {0};
static char s_onboardingRegionScratch[sizeof(RhinoConfig::region)] = {0};
static uint8_t s_onboardingRoleScratch = 1;  // CLIENT_MUTE
static char s_onboardingWifiSsidScratch[sizeof(RhinoConfig::wifiSsid)] = {0};
static char s_onboardingWifiPassScratch[sizeof(RhinoConfig::wifiPass)] = {0};
static int s_onboardingPickIndex = 0;  // current option index on region/role stages
static lv_obj_t *s_legendModal = nullptr;
// What scrolls on Help: everything between its header and its nav bar. The
// screen itself stays put, so its header does too.
static lv_obj_t *s_legendScroll = nullptr;

// Release notes for the running build, baked in at build time from
// RELEASE_NOTES.md. Layered over the CFG modal, scrolled rather than selected.
static lv_obj_t *s_releaseNotesBackdrop = nullptr;
static lv_obj_t *s_releaseNotesModal = nullptr;
// The scroll target is the body box, not the modal: the title and the key hint
// sit outside it so they stay put while the notes move.
static lv_obj_t *s_releaseNotesScroll = nullptr;

// ── Message Actions ──────────────────────────────────────────────────────────
// Off on the Cardputer. The modal would add seven emoji cells to a board with
// no PSRAM and a 96 KB LVGL pool — the same headroom that caps its emoji tray at
// 40 glyphs and compiles Discovery out entirely. Chat there keeps the plain node
// menu; the quick-emoji tray on E is still the way to send a reaction.
// Defined up here, ahead of the first #if that tests it: an undefined macro
// reads as 0, which would silently compile the feature out everywhere.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define HAS_MESSAGE_ACTIONS 0
#else
#define HAS_MESSAGE_ACTIONS 1
#endif

#if !defined(DEVICE_TLORA_PAGER_TFT)
// (I)nformation popup over the CFG modal — pager shows this in a side panel.
static lv_obj_t *s_nodeInfoModal = nullptr;
#endif
#if HAS_MESSAGE_ACTIONS
// Message Info, from the message actions menu; see openMsgSenderInfoModal().
static lv_obj_t *s_msgInfoModal = nullptr;
#endif
static lv_obj_t *s_liveModal = nullptr;
static lv_obj_t *s_liveList = nullptr;
static uint32_t s_liveClearFlashUntilMs = 0;

// Hidden "system stats" screen: pressing (I) five times in a row on the CFG
// screen reveals a live CPU/memory readout. Layered over the CFG modal; any
// key dismisses it.
static lv_obj_t *s_sysStatsModal = nullptr;
// Column body labels: [0]=CPU, [1]=MEMORY, [2]=STORAGE. On narrow screens only
// [0] is created and holds all sections in one scrollable list.
static lv_obj_t *s_sysStatsCols[3] = {nullptr, nullptr, nullptr};
static uint32_t  s_sysStatsOpenedMs = 0;
static uint32_t  s_sysStatsLastRefreshMs = 0;
// (I)-key streak state for the easter egg (reset by any other key / a pause).
static uint8_t   s_cfgInfoKeyStreak = 0;
static uint32_t  s_cfgInfoKeyLastMs = 0;
// Loop-iteration rate, sampled once per second as a lightweight CPU-activity
// proxy (there is no direct CPU-load counter under Arduino).
static uint32_t  s_loopIterations = 0;
static uint32_t  s_loopRateAnchorMs = 0;
static uint32_t  s_loopRateAnchorCount = 0;
static uint32_t  s_loopsPerSec = 0;

// Live chart history (channel utilization + SNR/RSSI sparkline data).
struct ChartHist {
    static constexpr int CAP = 60;
    float v[CAP] = {};
    int count = 0;       // 0..CAP valid samples
    int head = 0;        // index of oldest sample when count == CAP
    uint32_t seq = 0;    // bumps on every push for change detection
    float lastVal = 0.0f;
    bool hasLast = false;
};
static ChartHist s_chUtilHist;
static ChartHist s_airUtilHist;
static ChartHist s_snrHist;
static ChartHist s_rssiHist;

#if HAS_HOME_DASHBOARD
// Declared up here with the modals rather than beside the rest of the dashboard:
// chatScreenIsForeground() has to name it, and that is defined long before the
// dashboard's own block.
static lv_obj_t *s_homeDash = nullptr;
#endif
static lv_obj_t *s_chUtilChartModal = nullptr;
static lv_obj_t *s_chUtilChart = nullptr;
static lv_chart_series_t *s_chUtilSeries = nullptr;
static lv_chart_series_t *s_airUtilSeries = nullptr;
static lv_obj_t *s_chUtilStatsLabel = nullptr;
static uint32_t s_chUtilRenderedSeq = 0;
static uint32_t s_airUtilRenderedSeq = 0;

static lv_obj_t *s_snrChartModal = nullptr;
static lv_obj_t *s_snrChart = nullptr;
static lv_chart_series_t *s_snrSeries = nullptr;
static lv_chart_series_t *s_rssiSeries = nullptr;
static lv_obj_t *s_snrStatsLabel = nullptr;
static uint32_t s_snrRenderedSeq = 0;
static uint32_t s_rssiRenderedSeq = 0;

static lv_obj_t *s_dmModal = nullptr;
static lv_obj_t *s_dmConvPanel = nullptr;
static lv_obj_t *s_dmConvList = nullptr;
static lv_obj_t *s_dmMsgPanel = nullptr;
static lv_obj_t *s_dmMsgList = nullptr;
// The two panes' titles. With no frame round the panes, these are what show
// which one has the keys (refreshDmPanelFocusStyles()).
static lv_obj_t *s_dmConvTitle = nullptr;
static lv_obj_t *s_dmMsgTitle = nullptr;
static lv_obj_t *s_dmHintLabel = nullptr;
// Where the conversations are a drawer (dmListIsDrawer()): the scrim behind it,
// and the screen header's title, which names the conversation being read.
// s_dmConvPanel is the drawer itself. Null on the two-pane layout.
static lv_obj_t *s_dmDrawerScrim = nullptr;
static lv_obj_t *s_dmHeaderTitle = nullptr;
static bool s_dmDrawerOpen = false;
// False until the screen's first placement, which is instant: the screen opens
// with the drawer already where it belongs rather than sliding into it.
static bool s_dmDrawerAnimReady = false;
static lv_obj_t *s_dmNodePickerModal = nullptr;
static lv_obj_t *s_dmNodePickerList = nullptr;
static lv_obj_t *s_dmNodePickerTitle = nullptr;
static lv_obj_t *s_dmNodePickerHint = nullptr;
static lv_obj_t *s_dmNodePickerRows[MAX_NODES] = {};
static lv_obj_t *s_dmDelBackdrop = nullptr;
static lv_obj_t *s_dmDelModal = nullptr;
enum DestructiveConfirmAction : uint8_t {
    DESTRUCTIVE_CONFIRM_NONE = 0,
    DESTRUCTIVE_CONFIRM_DM_DELETE,
    DESTRUCTIVE_CONFIRM_LIVE_CLEAR,
    DESTRUCTIVE_CONFIRM_NODE_DELETE,
    // Not destructive to the node it names, but it can evict another to make
    // room, which is exactly the kind of side effect this prompt exists to say
    // out loud before it happens.
    DESTRUCTIVE_CONFIRM_NODE_RESTORE,
    // A node offered a different public key than the one pinned for it (see
    // key_pin.h). Three outcomes rather than Y/N: Accept, Reject, or close to
    // decide later -- so it has its own buttons and keys.
    DESTRUCTIVE_CONFIRM_KEY_CHANGE,
};
static DestructiveConfirmAction s_destructiveConfirmAction = DESTRUCTIVE_CONFIRM_NONE;
// Key-change review buttons, for the keyboard/D-pad cursor. 0 = Reject (the
// default, being the safe one), 1 = Accept.
static lv_obj_t *s_keyReviewBtns[2] = {};
static int s_keyReviewSel = 0;
// The full offered key the open review shows. Accept/Reject act on exactly this
// key: a different one arriving while the dialog is up must not be what a click
// installs.
static uint8_t s_keyReviewKey[32] = {};
// Render window, mirroring the Nodes screen (s_nodesRenderStart and friends).
// Without it the picker built a button+label for every match — up to MAX_NODES
// of them — and tore the whole lot down again on each arrow key, which is what
// made the screen crawl on a populated mesh. Only the visible slice exists now,
// and moving the highlight inside that slice restyles two rows instead of
// rebuilding any.
static int s_dmNodePickerRenderedIdx[MAX_NODES] = {};
static int s_dmNodePickerRowCount = 0;
static int s_dmNodePickerRenderStart = 0;

// ── Shared node snapshot buffer ──────────────────────────────────────────────
// The Nodes screen and the DM node picker each snapshot the node list so the UI
// renders against a stable set/order while packets keep mutating (and evicting
// from) NodeDB underneath. They are never live at the same time: opening either
// full-screen modal closes the other first, openDmNodePicker() requires the DM
// modal, and closeDmModal() tears the picker down. Both close paths also zero
// their snapshot count, and every read is bounds-checked against that count, so
// a stale alias can never be indexed. One buffer therefore backs both.
//
// The snapshot stores node *ids*, not NodeEntry copies: at 4 B vs 168 B per node
// that is what makes a 250-node MAX_NODES fit in DRAM. Row contents are resolved
// through Nodes.find() at render time, so the frozen id list still pins the set
// and the row order (the user's cursor never shifts under them) while the fields
// shown stay live. A node evicted while a modal is open resolves to nullptr; all
// render paths fall back to formatting the id so rows stay 1:1 with the filtered
// list rather than shifting.
//
// The two names are kept as array references so each call site still documents
// which screen owns the snapshot. Do NOT make the two live simultaneously.
static uint32_t s_nodeSnapshotIds[MAX_NODES] = {};
static uint32_t (&s_dmNodeSnapshotIds)[MAX_NODES] = s_nodeSnapshotIds;

static int s_dmNodeFilteredIdx[MAX_NODES] = {};
static int s_dmNodeSnapshotCount = 0;
static int s_dmNodeFilteredCount = 0;
static int s_dmNodeSelection = -1;
static constexpr int kDmNodeFilterMax = 24;
static char s_dmNodeFilter[kDmNodeFilterMax + 1] = {};
static int s_dmNodeFilterLen = 0;
static bool s_dmNodeFilterOpen = false;
static lv_obj_t *s_dmConvRows[MAX_DM_CONVS] = {};
static uint32_t s_dmConvNodeIds[MAX_DM_CONVS] = {};
static int s_dmConvCount = 0;
static int s_dmSelection = -1;
static int s_dmRenderedConvCount = -1;
static uint32_t s_dmRenderedNodeId = 0;
static int s_dmRenderedMsgCount = -1;
static int s_dmRenderedUnreadTotal = -1;
static bool s_dmMsgPanelFocused = false;
// The conversation the open delete prompt will remove. Held by node id, not by
// list index: the prompt sits on the root screen and an arriving DM can rebuild
// the list underneath it while the question is still on screen.
static uint32_t s_dmDeleteTargetNodeId = 0;
static char s_dmDeleteFlashMsg[64] = "";
static uint32_t s_dmDeleteFlashUntilMs = 0;
static uint32_t s_dmTouchPressStartMs = 0;
static int s_dmTouchPressRowIdx = -1;
static bool s_dmTouchLongPressTriggered = false;
#if UI_TOUCH_ONLY_PROFILE
// A finger is down on a conversation-list row. While it is, the list must not be
// rebuilt: refreshDmModal() rebuilds it with lv_obj_clean(), and deleting the
// row the gesture belongs to makes LVGL abandon the rest of that gesture. The
// tap that should have followed never arrives.
static bool s_dmRowPressActive = false;
// A refresh asked for while a row was held. Run once the finger lifts.
static bool s_dmRefreshPending = false;
#endif
static lv_obj_t *s_nodesModal = nullptr;
static lv_obj_t *s_nodesInfoPanel = nullptr;
// Shown only when there is no node to describe; the sections below take over the
// moment there is one.
static lv_obj_t *s_nodesDetail = nullptr;

// The Nodes screen has two layouts. Everywhere but the Cardputer: the list on
// the left carrying long names, aligned field tables on the right. The Cardputer
// keeps the original — a narrow column of short names beside one wrapped detail
// label — because 240x135 has neither the width to give the list 45% and still
// leave the values a readable column, nor the height for four sections of
// fields. Both layouts share the list, the filter, the selection model and the
// focus model below; only the geometry and the detail rendering differ.
#if UI_CARDPUTER_COMPACT
#define NODES_LAYOUT_WIDE 0
#else
#define NODES_LAYOUT_WIDE 1
#endif

// T-Deck Pro: the details are not on the Nodes screen at all. Stacked under the
// list on its portrait paper they left the list 92 px, three rows, and the
// panel below was still too short to show a whole section. There the list
// takes the screen, and the same sections open full height in a scrollable
// modal from the node's (N)ode Info action (openNodesInfoDlg()).
#if defined(DEVICE_TDECK_PRO)
#define NODES_INFO_AS_MODAL 1
#else
#define NODES_INFO_AS_MODAL 0
#endif
#if NODES_INFO_AS_MODAL && !NODES_LAYOUT_WIDE
#error "The Node Info modal is built from the wide layout's sections"
#endif
// The modal the sections live in while it is open; null otherwise, and always
// null where NODES_INFO_AS_MODAL is 0.
static lv_obj_t *s_nodesInfoDlg = nullptr;

// Which panel the keys are steering. Selecting a node and pressing Enter hands
// the navigation keys to the detail panel so its fields can be scrolled; the
// close key hands them back to the list. Touch builds ignore this — a finger
// scrolls whichever panel it is on.
static bool s_nodesInfoFocused = false;
static lv_obj_t *s_nodesListPanel = nullptr;   // framed container around s_nodesList

// ── Node detail sections ─────────────────────────────────────────────────────
// The detail panel used to be one wrapped label holding the whole description.
// Long values reflowed and dragged the field labels out of line with each other,
// which is what made it hard to read at a glance.
//
// Now each section is a two-label table: one label holds the field names, one
// holds the values, side by side. The columns line up because both labels use
// the same font and have one line per field, and a value too long for its column
// ellipsizes on its own line instead of wrapping — a wrap is what would push the
// rows out of step.
//
// Two labels per section rather than two objects per field is a memory decision,
// not a style one. This modal already refuses to open below 8 KB of largest free
// LVGL block and stops rendering rows below 4 KB; a label per field would be
// ~30 more objects out of that same fixed pool. These are built once when the
// modal opens and only their text is rewritten as the selection moves.
enum NodesDetailSection : uint8_t {
    NODES_SEC_IDENTITY = 0,
    NODES_SEC_LINK,
    NODES_SEC_POSITION,
    NODES_SEC_TELEMETRY,
    NODES_SEC_COUNT
};
static lv_obj_t *s_nodesSectionBox[NODES_SEC_COUNT]  = {};
static lv_obj_t *s_nodesSectionKeys[NODES_SEC_COUNT] = {};
static lv_obj_t *s_nodesSectionVals[NODES_SEC_COUNT] = {};
static lv_obj_t *s_nodesMapPanel = nullptr;
static lv_obj_t *s_nodesMapTitle = nullptr;
static lv_obj_t *s_nodesMapCoords = nullptr;
static lv_obj_t *s_nodesMapMarker = nullptr;
static lv_obj_t *s_nodesMapTileLayer = nullptr;
static lv_obj_t *s_nodesMapImage = nullptr;
static char s_nodesMapImageSrc[96] = {};
static bool s_nodesMapLastRenderUsedDetail = false;
static char s_nodesMapLastDetailKey[11] = {};
static int s_nodesMapLastDetailLod = -1;
static bool s_nodesMapLastBoundsValid = false;
static float s_nodesMapLastLatMin = 0.0f;
static float s_nodesMapLastLatMax = 0.0f;
static float s_nodesMapLastLonMin = 0.0f;
static float s_nodesMapLastLonMax = 0.0f;
static lv_fs_drv_t s_nodesMapFsDrv;
static bool s_nodesMapFsDrvReady = false;
static SemaphoreHandle_t s_nodesMapFsMutex = nullptr;
static lv_obj_t *s_nodesList = nullptr;
static lv_obj_t *s_nodesTitleLabel = nullptr;
static lv_obj_t *s_nodesHintLabel = nullptr;
// The Search / Clear button above the node list, and its label.
static lv_obj_t *s_nodesFilterBtn = nullptr;
static lv_obj_t *s_nodesFilterBtnLabel = nullptr;
// The All button above that: a search for "*", every node the list can show.
static lv_obj_t *s_nodesAllBtn = nullptr;
// Map, above All: every located node on one map (see openNodesMapModal()).
// Only on the boards that have Locate's map engine; stays null elsewhere.
static lv_obj_t *s_nodesMapBtn = nullptr;
static lv_obj_t *s_nodesFilterDialog = nullptr;
static lv_obj_t *s_nodesFilterInput = nullptr;
static lv_obj_t *s_nodesFilterKeyboard = nullptr;
static lv_obj_t *s_nodesListRows[MAX_NODES] = {};
static int s_nodesRenderedFilteredIdx[MAX_NODES] = {};
static int s_nodesListRowCount = 0;
static constexpr int kNodesMaxRenderedRows =
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    40;
#elif defined(DEVICE_TLORA_PAGER_TFT)
    64;
#else
    48;
#endif
static int s_nodesRenderStart = 0;
// Aliases s_nodeSnapshotIds — see the note at its definition above.
static uint32_t (&s_nodesSnapshotIds)[MAX_NODES] = s_nodeSnapshotIds;
// ── Combined live + archived row space ───────────────────────────────────────
// The Nodes screen addresses two lists through one index:
//
//   0 .. s_nodesSnapshotCount-1                  live, s_nodesSnapshotIds[i]
//   s_nodesSnapshotCount .. +s_nodesArchivedCount  archived, nodeArchiveIndexAt(i - base)
//
// Archived rows are not copied into a snapshot of their own: node_db's index is
// itself built at snapshot time and does not move until the next build, so it
// already *is* the frozen list. That also keeps the DM picker's aliasing of
// s_nodeSnapshotIds untouched — this screen adds a second range, it does not
// grow the shared buffer.
//
// Live first, then archived, rather than interleaved: the two are different
// kinds of thing, and the timestamps that would order them mean different
// things on each side (last heard vs. when the record was written).
static int s_nodesArchivedCount = 0;
// The full record behind the *selected* archived row. The index deliberately
// does not carry position, telemetry or the public key — 250 of those would not
// fit — so the details panel re-reads them from the card. Cached by id because
// refreshNodesDetails() runs on every repaint, and re-reading the card on each
// one is exactly the per-refresh SD access this screen must not do: the bus is
// shared with the LoRa radio. One read per newly selected archived node.
static NodeEntry s_nodesArchDetail = {};
static uint32_t  s_nodesArchDetailId = 0;
static bool      s_nodesArchDetailValid = false;
static constexpr int kNodesRowMax = MAX_NODES + NODE_ARCHIVE_INDEX_MAX;
static int s_nodesFilteredIdx[kNodesRowMax] = {};
static int s_nodesSnapshotCount = 0;
static int s_nodesFilteredCount = 0;
static int s_nodesSelected = -1;
static constexpr int kNodesFilterMax = 24;
static char s_nodesFilter[kNodesFilterMax + 1] = {};
static int s_nodesFilterLen = 0;
// A search is in force: the list shows every node (live, plus archived when
// that half is shown) matching s_nodesFilter. Without one the list shows only
// favorites. Set and cleared only through the Search dialog and the Clear
// button (applyNodesFilterText()), never by typing on the list itself.
static bool s_nodesFilterOpen = false;
// Keyboard builds: the cursor is on one of the buttons above the list rather
// than a row. Up from the first row puts it on Search, Up again on All, again
// on Map where there is one, and Down walks back; Enter presses the button.
// s_nodesTopBtn says which, and is only meaningful while
// s_nodesSearchBtnFocused is set. Touch-only builds tap the buttons and never
// set either.
static constexpr int kNodesTopSearch = 0;
static constexpr int kNodesTopAll = 1;
static constexpr int kNodesTopMap = 2;
static bool s_nodesSearchBtnFocused = false;
static int s_nodesTopBtn = kNodesTopSearch;
// Where the keyboard cursor can walk onto those buttons: every keyboard board,
// and the T-Display P4, which builds touch-only but has a keyboard expansion
// (#116: without this its arrows had nowhere to go from an empty list). The
// Mesh Deck likewise: touch-only build, D-pad always, keyboard optional.
#if !UI_TOUCH_ONLY_PROFILE || defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_MESH_DECK)
#  define NODES_KEYBOARD_TOP_BUTTONS 1
#else
#  define NODES_KEYBOARD_TOP_BUTTONS 0
#endif
// Locate is compiled out on the Cardputer, for the same reason Message Actions
// is (see HAS_MESSAGE_ACTIONS below): no PSRAM, and decoding a PNG into a
// full-panel image object is exactly the allocation that board has no room to
// make — it is the one whose internal DRAM budget issue #49 is about. The row
// is absent there rather than permanently greyed: a control that can never be
// used on this hardware is not a disabled control, it is clutter.
#define HAS_NODE_LOCATE HAS_STATE_MAPS

#if defined(DEVICE_CARDPUTER_LORA_HAT) && HAS_NODE_LOCATE
#error "Locate maps must remain disabled on Cardputer: no PSRAM is available"
#endif

// Node Actions modal: actions arranged 2-per-row. Each entry has a single-key
// keyboard shortcut (see kNodesActionShortcuts) mirrored in its label as (X).
// The count varies by board — Locate and LOS are each compiled out where the
// hardware cannot support them — so nothing should assume a fixed total.
#if HAS_NODE_LOCATE
static constexpr int kNodesActionBaseCount = 7;
#else
static constexpr int kNodesActionBaseCount = 6;
#endif
#if HAS_NODE_LOS
// LOS sits after Locate where both exist, and takes Locate's index where it
// does not — the Heltec has LOS but no Locate, so the two are independent.
// Share sits just before Delete: after every action that exists on only some
// boards, so none of their fixed indices move, and before Admin, which has to
// stay last (see activeActionCount()).
static constexpr int kNodesActionLos    = kNodesActionBaseCount;
static constexpr int kNodesActionAfterLos = kNodesActionBaseCount + 1;
#else
static constexpr int kNodesActionAfterLos = kNodesActionBaseCount;
#endif
#if NODES_INFO_AS_MODAL
// Node Info goes ahead of Share for the same reason Share goes ahead of Delete:
// after the actions some boards lack, so their indices stay put.
static constexpr int kNodesActionInfo   = kNodesActionAfterLos;
static constexpr int kNodesActionShare  = kNodesActionAfterLos + 1;
#else
static constexpr int kNodesActionShare  = kNodesActionAfterLos;
#endif
static constexpr int kNodesActionDelete = kNodesActionShare + 1;
// Delete goes last on every board, after whichever of Locate and LOS this one
// has. Appending rather than inserting is what keeps kMsgActionNodeMap — which
// indexes this list by position — pointing at the same actions it always did.
#if HAS_ADMIN_TERMINAL
// Admin appends after Delete for that same reason. Unlike Locate, LOS and
// Delete, which grey out, this row is not built at all unless the peer is
// confirmed: a disabled "Admin" on a stranger's node is an invitation to try,
// and trying means unauthorized admin packets across the mesh and the broker.
static constexpr int kNodesActionAdmin = kNodesActionDelete + 1;
static constexpr int kNodesActionCount = kNodesActionAdmin + 1;
// Set when the menu opens, from AdminPeerList. The row exists only while true,
// which is why activeActionCount() rather than kNodesActionCount is what every
// navigation and shortcut path counts against.
static bool s_nodesActionAdminEnabled = false;
#else
static constexpr int kNodesActionCount = kNodesActionDelete + 1;
#endif
static lv_obj_t *s_nodesActionModal = nullptr;
static int s_nodesActionSelection = 0;
static uint32_t s_nodesActionNodeId = 0;
// Action ordering (also referenced by executeNodesActionSelection):
//   0=Traceroute, 1=Send DM, 2=Favorite toggle, 3=Request Node,
//   4=Request Position, 5=Ignore toggle, 6=Locate, then LOS, then Delete.
static constexpr char kNodesActionShortcuts[kNodesActionCount] = {
    // 'Q' for Re(q)uest Node: I is Message Info's in the message menu, which
    // shares these rows, and one key per action across both menus is the rule.
    'T', 'D', 'F', 'Q', 'P', 'G',
#if HAS_NODE_LOCATE
    'L',
#endif
#if HAS_NODE_LOS
    // 'S' for Sightline: 'L' already belongs to Locate on the boards that
    // have both, and a shortcut that moves between builds is worse than one
    // that is not the word's first letter.
    'S',
#endif
#if NODES_INFO_AS_MODAL
    // 'N' for (N)ode Info: I is Message Info's, as with Re(q)uest Node.
    'N',
#endif
    // 'H' for S(h)are: S is LOS's where LOS exists.
    'H',
    // 'E' for Del(e)te: D is Send DM's and L is Locate's. It is one key on a
    // destructive action, which is why it opens a confirmation rather than
    // doing anything.
    'E',
#if HAS_ADMIN_TERMINAL
    'A',
#endif
};
#if HAS_NODE_LOCATE
// Named because three places have to agree on which row Locate is: the disabled
// styling, the shortcut key, and the activate path.
static constexpr int kNodesActionLocate = 6;
#endif
#if HAS_NODE_LOS
// LOS needs a position at BOTH ends: the contact's, and one for this node
// (a live fix or a set location). Same greying rule as Locate.
static bool s_nodesActionLosEnabled = false;
#endif
// There has to be a record to delete. Opened from a chat message the sender may
// not be in the node table at all — a cleared table, or someone only ever heard
// relayed — and a confirmation prompt that deletes nothing is worse than a row
// that says up front it has nothing to act on.
static bool s_nodesActionDeleteEnabled = false;
// Share needs something to share: a node in the table with a real name, and
// not this one (its own NodeInfo has its own send).
static bool s_nodesActionShareEnabled = false;
#if NODES_INFO_AS_MODAL
// Node Info renders the Nodes screen's selection, so it needs that screen up
// with this node under its cursor. Opened from the chat there is no list.
static bool s_nodesActionInfoEnabled = false;
#endif
#if HAS_NODE_LOCATE
// Locate needs a position to show. The row is built either way — a menu whose
// contents move around depending on the node is harder to learn than one with a
// greyed row in a fixed place — so the state is carried here and consulted by
// the styling, the shortcut, and the activate path alike.
static bool s_nodesActionLocateEnabled = false;
#endif

// The six reactions Message Actions offers on a chat message. Deliberately the
// same set, in the same order, as CHAT_TAPS in web_config.cpp — a reaction
// should be the same thing whether it was sent from the device or the browser.
// Keep the two lists in step.
static const char *const kTapbackTray[] = {
    "\U0001F44D", "\U0001F44E", "\U0000203C", "\U00002753", "\U0001F602", "\U0001F622",
};
static constexpr int kTapbackTrayCount = (int)(sizeof(kTapbackTray) / sizeof(kTapbackTray[0]));

// The same modal opened from a chat message rather than a node row. It keeps
// the six node actions and prepends the things that only make sense about a
// message: the tapback reactions, an escape to the full emoji tray, and Reply.
//
// One flat index space so up/down and the selection highlight need no special
// cases — the tapbacks are rows like any other, they just render as a strip of
// glyph cells above the two-column grid instead of inside it.
//
//   0 .. 5  tapback reactions (kTapbackTray)
//   6       "..."  -> full emoji picker in tapback mode
//   7       Reply
//   8       Message Info (openMsgSenderInfoModal())
//   9 ..13  node actions, via kMsgActionNodeMap
//
// Favorite is deliberately absent from message mode. It is a property of the
// node rather than anything to do with the message, it is the one action here
// with no bearing on the conversation, and dropping it also makes the button
// grid come out even — Reply plus five actions is three full rows, where seven
// buttons left a half-empty row of padding under the menu. The Nodes screen
// still offers it.
// Skips 2 = Favorite, and 6 = Locate: both are properties of the node rather
// than of the message, and message mode is already the taller of the two menus.
static constexpr uint8_t kMsgActionNodeMap[] = { 0, 1, 3, 4, 5 };
static constexpr int kMsgActionNodeCount =
    (int)(sizeof(kMsgActionNodeMap) / sizeof(kMsgActionNodeMap[0]));
static constexpr int kMsgActionMoreIdx  = kTapbackTrayCount;        // 6
static constexpr int kMsgActionReplyIdx = kMsgActionMoreIdx + 1;    // 7
static constexpr int kMsgActionInfoIdx  = kMsgActionReplyIdx + 1;   // 8
static constexpr int kMsgActionNodeBase = kMsgActionInfoIdx + 1;    // 9
static constexpr int kMsgActionCount    = kMsgActionNodeBase + kMsgActionNodeCount;  // 14
static constexpr char kMsgActionShortcuts[kMsgActionCount] = {
    '1', '2', '3', '4', '5', '6',   // tapbacks
    'M',                            // more emoji
    'R',                            // reply
    'I',                            // message info
    'T', 'D', 'Q', 'P', 'G'         // node actions, minus (F)avorite
};

// Sized for the larger of the two modes.
static lv_obj_t *s_nodesActionRows[kMsgActionCount] = {};
static_assert(kNodesActionCount <= kMsgActionCount,
              "s_nodesActionRows is sized for message mode being the larger");
// True while the modal is acting on a message rather than a bare node.
static bool s_nodesActionMsgMode = false;
// The message being acted on. Message mode requires it to be non-zero — without
// a packet id there is nothing to react to or reply to.
static uint32_t s_nodesActionPacketId = 0;

static inline int activeActionCount() {
    if (s_nodesActionMsgMode) return kMsgActionCount;
#if HAS_ADMIN_TERMINAL
    // Admin is last, so "not built" is exactly one fewer row. Navigation, the
    // shortcut table and the click handler all count against this and need no
    // special case of their own.
    if (!s_nodesActionAdminEnabled) return kNodesActionCount - 1;
#endif
    return kNodesActionCount;
}
static inline const char *activeActionShortcuts() {
    return s_nodesActionMsgMode ? kMsgActionShortcuts : kNodesActionShortcuts;
}

#if UI_ARROW_GRID_NAV
// Where the M9's d-pad (or the Tanmatsu's arrows) goes from `sel`, moving (dx, dy) across the menu as it is
// drawn: the action buttons are a two-column grid (every row is lv_pct(49)), and
// in message mode the tapback strip is one row of cells above it. Returns `sel`
// unchanged at an edge -- nothing wraps.
//
// Every other keyboard board keeps stepping through the flat index one action
// at a time: the Pager's wheel has no left or right, and moving a whole row at a
// time would leave it unable to reach the second column.
static int nodesActionGridStep(int sel, int dx, int dy) {
    constexpr int kCols = 2;
    const int count = activeActionCount();
    const int gridFirst = s_nodesActionMsgMode ? kMsgActionReplyIdx : 0;

    if (sel < gridFirst) {
        // On the tapback strip: left/right walk it, down drops into whichever
        // column this cell sits over, up has nowhere to go.
        if (dx) {
            const int n = sel + dx;
            return (n >= 0 && n < gridFirst) ? n : sel;
        }
        if (dy > 0) {
            const int col = sel * kCols / gridFirst;
            const int n = gridFirst + col;
            return (n < count) ? n : count - 1;
        }
        return sel;
    }

    const int pos = sel - gridFirst;
    const int row = pos / kCols;
    const int col = pos % kCols;
    if (dx) {
        const int nc = col + dx;
        if (nc < 0 || nc >= kCols) return sel;
        const int n = sel + dx;
        return (n < count) ? n : sel;
    }
    if (dy < 0) {
        if (row > 0) return sel - kCols;
        // Top row: up onto the strip, to the cell over the middle of this
        // column -- the inverse of the column pick above, so up then down
        // lands back where it started.
        if (gridFirst > 0) return (2 * col + 1) * gridFirst / (2 * kCols);
        return sel;
    }
    if (dy > 0) {
        const int n = sel + kCols;
        if (n < count) return n;
        // The right column over a short last row: the one below is the last
        // action, even though it sits in the other column.
        const int lastRow = (count - 1 - gridFirst) / kCols;
        return (row < lastRow) ? count - 1 : sel;
    }
    return sel;
}
#endif

// Rows that can be unavailable — Locate, LOS and Delete — and only in node
// mode. Message mode lays a different set of rows over the same index space
// (see kMsgActionNodeMap), carries none of these three, and greys nothing; the
// early return is what stops a node-mode position matching a message-mode row
// that happens to sit at the same index.
static inline bool nodesActionRowDisabled(int idx) {
    if (s_nodesActionMsgMode) return false;
#if HAS_NODE_LOCATE
    if (idx == kNodesActionLocate && !s_nodesActionLocateEnabled) return true;
#endif
#if HAS_NODE_LOS
    if (idx == kNodesActionLos && !s_nodesActionLosEnabled) return true;
#endif
    if (idx == kNodesActionDelete && !s_nodesActionDeleteEnabled) return true;
    if (idx == kNodesActionShare && !s_nodesActionShareEnabled) return true;
#if NODES_INFO_AS_MODAL
    if (idx == kNodesActionInfo && !s_nodesActionInfoEnabled) return true;
#endif
    return false;
}

// Channel Actions modal: overlay opened with (A) from the main screen. Acts on
// the active channel. Holds the (M)ute/Un(m)ute and (L)ocation toggles,
// activatable by touch or keyboard.
static lv_obj_t *s_channelActionsModal = nullptr;
static lv_obj_t *s_channelActionsMuteBtn = nullptr;
static lv_obj_t *s_channelActionsMuteLabel = nullptr;
static lv_obj_t *s_channelActionsShareBtn = nullptr;
static lv_obj_t *s_channelActionsShareLabel = nullptr;
static int s_channelActionsChanIdx = -1;
static lv_obj_t *s_tracerouteBackdrop = nullptr;
static lv_obj_t *s_tracerouteModal = nullptr;
static lv_obj_t *s_tracerouteStatusLabel = nullptr;
static lv_obj_t *s_tracerouteResultsBox = nullptr;
static lv_obj_t *s_tracerouteResultsLabel = nullptr;
static uint32_t s_tracerouteNodeId = 0;
static uint32_t s_traceroutePacketId = 0;
static uint32_t s_tracerouteStartedMs = 0;
static bool s_tracerouteAwaitingRouting = false;
static bool s_tracerouteAwaitingReply = false;
// Set once a terminal status is on screen. A Camillia responder answers with both a
// TRACEROUTE_APP reply and a ROUTING_APP ack, and the reply usually wins the race —
// without this the trailing ack would re-arm the wait and time out over a rendered
// route. Also absorbs duplicate acks and post-reply NAKs from rebroadcast/MQTT echo.
static bool s_tracerouteResolved = false;
static int s_activeChannel = 0;
static int s_lastRenderedChannel = -1;
static int s_lastRenderedCount = -1;
// Chat redraw gate: channel-revision tracks message/ACK mutations; context
// signature tracks UI-only state (style, selection, cursor mode).
static uint32_t s_lastChatRenderedRevision = 0;
static uint32_t s_lastChatContextSignature = 0;

// ── Chat render windowing ──────────────────────────────────────────────────
// refreshChatView rebuilds every message it emits, so rendering a whole channel
// (up to MAX_MSG_LINES) made each teardown/rebuild scale with backlog — the chat
// felt slower the more history you held. Instead we render only the most-recent
// window of logical messages and grow it when the user scrolls to the top, so
// steady-state cost stays bounded while full history stays reachable on demand.
static constexpr int kChatWindowBaseMsgs = 40;   // messages a channel opens with
static constexpr int kChatWindowGrowMsgs = 40;   // added each scroll-to-top load
// Messages kept rendered around the Pager's message cursor. Every cursor step
// rebuilds the list (the cursor index feeds the render context signature), so
// this is the per-detent cost and wants to stay small — but large enough that
// the selected message keeps context above and below it and lv_obj_scroll_to_view
// has somewhere to move, rather than the list jumping a screenful each step.
static constexpr int kChatCursorWindowMsgs = 24;
static constexpr lv_coord_t kChatWindowGrowScrollMargin = 12;  // px-from-top that loads older
static int      s_chatWindowChannel      = -1;   // channel the window below belongs to
static int      s_chatRenderWindowMsgs   = kChatWindowBaseMsgs;
static bool     s_chatWindowMoreAbove    = false; // older messages exist outside the window
static bool     s_chatWindowGrowPending  = false; // scroll cb asked to load older history
// True while refreshChatView is tearing down and re-adding rows. LVGL emits
// LV_EVENT_SCROLL for programmatic scrolls and for the readjustment that follows
// lv_obj_clean(), so without this the rebuild's own transient scroll-at-top state
// looks exactly like the user scrolling up to load older history.
static bool     s_chatRebuilding         = false;
// The message at the top of the pre-grow window, so we can re-anchor the viewport
// to it after prepending older history — otherwise the view snaps to the top,
// which would immediately re-trigger another grow.
static bool     s_chatWindowAnchorActive   = false;
static uint32_t s_chatWindowAnchorPacketId = 0;
static char     s_chatWindowAnchorText[MSG_CHARS + 1] = {0};
static int s_lastRenderedLiveCount = -1;
static int s_lastRenderedLiveScrollOff = -1;
static int s_cfgSelection = 0;
static int s_cfgActionCount = 0;
static char s_cfgStatus[96] = "";
static bool s_cfgOtaInstallArmed = false;
static char s_cfgOtaLatestTag[48] = "";
static int s_cfgConfirmAction = -1;
// Set only by the callback form of openCfgConfirmModal(); null means the
// pending answer belongs to a CFG_ACTION row.
typedef void (*CfgConfirmFn)(int);
static CfgConfirmFn s_cfgConfirmFn = nullptr;
// An optional third answer, beside Yes and No. Only the SD scan uses it -- its
// dialog offers Format as well as Delete -- and every other caller leaves it
// null and gets the two-button dialog it always had. Here rather than up with
// the other confirm-dialog handles, because CfgConfirmFn is not a type until
// the line above.
static CfgConfirmFn s_cfgConfirmAltFn = nullptr;
static char         s_cfgConfirmAltLabel[16] = {0};
static char         s_cfgConfirmAltKey = 0;
static int          s_cfgConfirmFnArg = 0;
// Copied rather than pointed at: the caller's buffer is usually a local.
static char         s_cfgConfirmText[256] = {0};
static uint32_t s_cfgConfirmMs = 0;
static uint32_t s_cfgLastActivateMs = 0;
// The Clear Nodes confirmation's two options. Both start unticked every time
// the dialog opens, so the answer that needs no thought is the recoverable one:
// favorites and the SD archive survive unless asked for by name.
static bool      s_clearNodesInclFavs    = false;
static bool      s_clearNodesInclArchive = false;
static lv_obj_t *s_clearNodesFavsCheck   = nullptr;
static lv_obj_t *s_clearNodesArchCheck   = nullptr;
// Which box Space toggles: 0 favorites, 1 archived. Moved by the board's
// up/down (M9 d-pad, Pager wheel, Cardputer ; and .), which in this dialog
// steps between the boxes instead of scrolling the one-line question.
static int       s_clearNodesFocus       = 0;
static uint32_t s_cfgLastScrollMs = 0;
static uint32_t s_cfgEnterLockUntilMs = 0;
static bool s_cfgAwaitEnterRelease = false;
static bool s_cfgInfoPanelFocused = false;
static bool s_cfgDebugLog = (MY_DEBUG_MONITOR != 0);
static char s_otaWorkerBootNotice[160] = "";
// Shown after the reboot when an update was too big for this device's app slot
// (kOtaErrNeedsUsbInstall). Kept under the notice buffer's 160 bytes.
static const char *const kOtaNoticeNeedsUsb = TR_NOOP(
    "Please re-flash from Camillia's website for language support.");
static uint32_t s_selectedMsgReplyPacketId = 0;
// Sender of the message under the chat cursor, so Enter can act on the person
// who wrote it. 0 means "nothing actionable": either no message is selected, or
// the selected line is a local/system one with no sender (see
// DisplayLine::senderNodeId). Set and cleared only through
// setSelectedReplyContext(), alongside the reply context it travels with.
static uint32_t s_selectedMsgSenderNodeId = 0;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static constexpr size_t kReplyPreviewTextMax = 128;
#elif defined(DEVICE_TDECK) || UI_TOUCH_ONLY_PROFILE
static constexpr size_t kReplyPreviewTextMax = 192;
#elif defined(DEVICE_TLORA_PAGER_TFT)
static constexpr size_t kReplyPreviewTextMax = 256;
#else
static constexpr size_t kReplyPreviewTextMax = 160;
#endif
static char s_selectedMsgText[kReplyPreviewTextMax + 1] = "";
static uint32_t s_myNodeId = 0;
static uint32_t s_nextNodeInfoTxMs = 0;
static uint32_t s_nextPositionTxMs = 0;
static uint32_t s_nextDeviceTelemetryTxMs = 0;
static uint32_t s_nextEnvTelemetryTxMs = 0;
static uint32_t s_nextNeighborInfoTxMs = 0;
static uint32_t s_nextMapReportMs = 0;
// Consecutive failed sends per announce, so a retry can back off instead of
// re-entering a blocking transmit every five seconds forever. See issue #53.
static uint8_t s_nodeInfoTxFails = 0;
static uint8_t s_positionTxFails = 0;
static uint8_t s_deviceTelemetryTxFails = 0;
static uint8_t s_envTelemetryTxFails = 0;
static uint8_t s_neighborInfoTxFails = 0;
static uint8_t s_mapReportFails = 0;
// Sized for the flasher's `cfg logo data` lines (see handleCfgCommand()).
struct SerialCmdInput {
    char buf[256];
    size_t len;
    bool overflow;
};
static SerialCmdInput s_serialCmdUart = {};
// The CrowPanel 3.5" console is UART0 (USB_CDC_ON_BOOT=0), but its USB-C can
// reach the S3's own USB serial instead, which is where the web flasher then
// talks. The CLI listens on both. Not on the 5": its native USB pins are the
// radio's.
#if defined(DEVICE_CROWPANEL_35) && ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#define CLI_ON_NATIVE_USB 1
static SerialCmdInput s_serialCmdUsb = {};
#endif
// Where CLI replies go: the console the current command line came in on.
static Stream *s_cliOut = &Serial;

enum ComposeTarget : uint8_t {
    COMPOSE_TARGET_CHANNEL = 0,
    COMPOSE_TARGET_DM = 1,
};

enum HeltecNavTarget : uint8_t {
    HELTEC_NAV_CFG = 0,
    HELTEC_NAV_DM,
    HELTEC_NAV_NODES,
    HELTEC_NAV_LIVE,
    HELTEC_NAV_TOOLS,
    HELTEC_NAV_ACTIONS,
    HELTEC_NAV_LEGEND,
    // Home opens the dashboard. Chat is not a screen of its own — it is what is
    // left when everything stacked on top of it is gone — so its cell closes
    // rather than opens. Both tear down whatever is up first.
    HELTEC_NAV_HOME,
    HELTEC_NAV_CHAT,
};

#if UI_TOUCH_NAV_BAR
// Height of the bar, and so of the strip the chat screen gives up for it.
//
// One number for every board that draws the bar. The keyboard boards ran 24
// for a while, on the reasoning that their bar is a second way in beside the
// keys — but a tap target is a tap target whoever is aiming at it, and the four
// pixels come out of a message list that can spare them.
#if UI_LARGE_PANEL_PROFILE
static constexpr int kBottomNavHeight = 56;
#elif defined(DEVICE_TDISPLAY_P4)
// Half as tall again. The pixel-doubled P4 is a 240x320-class screen with
// nearly twice the height, so the message list can pay for bigger targets.
// Every screen that carries the bar -- chat, its modal copies, the dashboard --
// sizes from this, so the one number moves them all together.
//
// Not a constant here: portrait has the height for half as tall again once
// more, and takes 63 when the orientation is read at boot (loadBootOrientation()).
// Landscape keeps 42 -- or 30 with the keyboard expansion on (p4NavBarHeight()).
// Nothing reads it before then.
static int kBottomNavHeight = 42;
#elif defined(DEVICE_CROWPANEL_50)
// Twice the usual 28: an 800x480 panel has the height, and these are the
// targets a finger aims at most.
static constexpr int kBottomNavHeight = 56;
#elif defined(DEVICE_CROWPANEL_35)
// Twice the usual 28 in portrait, where the 480 px height can pay for it;
// landscape's 320 cannot, and keeps 28. Set once the boot's orientation is
// read (loadBootOrientation()); nothing reads it before then.
static int kBottomNavHeight = 28;
#else
static constexpr int kBottomNavHeight = 28;
#endif
#endif

// Whether to draw the bottom nav bar at all. A touch-only board always does —
// there is no keyboard to fall back on, so this is not a preference. Where the
// board has both (HAS_NAV_BAR_TOGGLE), it is the user's call, and off restores
// the key-hint strip the bar replaced.
static inline bool bottomNavEnabled() {
#if UI_TOUCH_ONLY_PROFILE
    return true;
#elif HAS_NAV_BAR_TOGGLE
    return s_cfg.navBarEnabled;
#else
    return false;
#endif
}

static ComposeTarget s_composeTarget = COMPOSE_TARGET_CHANNEL;
static uint32_t s_composeDmNodeId = 0;
static lv_obj_t *s_composeTitle = nullptr;

static uint32_t s_composeReplyPacketId = 0;
static int s_composeChannelIdx = 0;
// Room for the licensed-mode "HAM <call> " ahead of the clock.
static constexpr size_t kHeaderTimeBuf = LIVE_CLOCK_BUF + 4 + 15 + 1;
static char s_lastHeaderTime[kHeaderTimeBuf] = "";
static uint8_t s_lastBattPct = 255;
// Battery voltage in hundredths, cached alongside the percentage so the header
// early-out notices a change in either. 0xFFFF is the "not yet drawn" sentinel,
// matching s_lastBattPct's 255.
static uint16_t s_lastBattCentiV = 0xFFFF;
static uint8_t s_lastGpsSats = 255;
static bool s_lastGpsEnabled = false;
static bool s_lastGpsFix = false;
static uint32_t s_lastGpsSampleMs = 0;
static bool s_lastWifiConnected = false;
static bool s_lastWifiApMode = false;
static bool s_ntpConfigured = false;
static char s_ntpServerActive[48] = "";
static uint32_t s_ntpLastConfigureMs = 0;
// Last GPS-driven clock set; 0 until one happens. Declared here because the
// Time and Date modal clears it when handing the clock back to automatic.
static uint32_t s_gpsClockSyncMs = 0;

// A clock this far along is one somebody actually set; anything below it is the
// 1970 epoch the ESP32 boots at.
static constexpr time_t kClockSetEpoch = 1700000000;
static inline bool clockIsSet();
// Config backups: Export offers a timestamped copy while the clock is set, and
// Import offers a choice while more than one backup is on the card.
static bool cfgExportOffersStamp();
static bool cfgImportHasChoice();

#if HAS_SLEEP_OVERLAY
// Record an inbound message for the sleeping panel's preview rows. Called from
// the RX paths only -- deliberately not from the `chatseed` CLI helper, which
// pushes synthetic traffic straight into Channels and has no business changing
// what the lock screen claims arrived.
//
// chanIdx < 0 means a DM. Callers gate on what the user can currently see, not
// on what the UI has selected: a channel that is not muted and is not the
// conversation visible on screen right now. Behind the glance overlay nothing
// is visible, so the ring holds the active channel's traffic too -- which is
// usually the traffic most worth reporting.
static void tdeckProNoteRecentMessage(int chanIdx, uint32_t fromNode, const char *text,
                                      uint32_t packetId) {
    TdeckProRecentMsg &slot = s_tdeckProRecentMsgs[s_tdeckProRecentMsgHead];
    const time_t now = time(nullptr);
    slot.epoch        = (now >= kClockSetEpoch) ? (uint32_t)now : 0;
    slot.senderNodeId = fromNode;
    slot.chanIdx      = (chanIdx >= 0 && chanIdx < MESH_CHANNELS) ? (int8_t)chanIdx : -1;
    slot.packetId     = packetId;
    // An MECP message previews decoded, in the language set when it arrived.
    if (!mecpDecodeBody(text, slot.text, sizeof(slot.text)))
        utf8util::copyTruncate(slot.text, sizeof(slot.text), text ? text : "");
    // A preview row is one line by construction, and a label given a newline
    // draws a second one straight through the row underneath it. Flatten the
    // control characters here rather than at render time, so every reader of
    // the ring gets a single-line string.
    for (char *c = slot.text; *c; c++) {
        if ((uint8_t)*c < 0x20 || *c == 0x7F) *c = ' ';
    }

    s_tdeckProRecentMsgHead = (uint8_t)((s_tdeckProRecentMsgHead + 1) % kTdeckProSleepMsgSlots);
    if (s_tdeckProRecentMsgCount < kTdeckProSleepMsgSlots) s_tdeckProRecentMsgCount++;
    s_tdeckProRecentMsgSeq++;
}
#endif
static uint32_t s_lastChannelGlowAnimMs = 0;
static bool s_radioReady = false;
static bool s_webCfgEnabled = false;
// Web config is wanted but waiting for the station (webCfgStartDeferred()).
static bool s_webCfgAwaitStation = false;
#if HAS_VNC_HOST
static bool s_vncEnabled = false;
#endif
static bool s_screenAsleep = false;

// True while the user cannot see the chat view: the lit lock screen is up, or
// the panel is out (which on T-Deck Pro is where the glance overlay lives).
// Neither state is anyone reading a conversation, so nothing arriving during
// them may be treated as already seen just because its channel or DM happens to
// be the one the UI has selected underneath.
#if HAS_HOME_DASHBOARD
// Defined with the dashboard, far below. Declared here because the check just
// under this line runs from the RX paths, which sit between the two.
static inline bool homeDashboardVisible();
#endif
static inline bool glanceOverlayHidesUi() {
#if FEATURE_LOCK_SCREEN
    if (s_lockScreenActive) return true;
#endif
#if HAS_HOME_DASHBOARD
    // The dashboard belongs here for the same reason the lock screen does: it
    // is an opaque surface over the chat screen, so the conversation the UI has
    // selected underneath is not one anyone is reading. Left out, traffic on
    // the active channel was marked seen while the user sat on the dashboard —
    // no unread mark, and nothing recorded for the surfaces that report what
    // arrived. Declared below with the dashboard itself; this runs from the RX
    // paths, long after both exist.
    if (homeDashboardVisible()) return true;
#endif
    return s_screenAsleep;
}

// The DM conversation the UI currently has open, or 0 for none.
static uint32_t openDmPeerNodeId() {
    if (!s_dmModal) return 0;
    if (s_dmSelection < 0 || s_dmSelection >= s_dmConvCount) return 0;
    return s_dmConvNodeIds[s_dmSelection];
}

// True only while the user can actually see that conversation. One definition,
// so the RX gates and the resume path cannot disagree about what counts as
// read -- a disagreement there is exactly what strands an unread badge.
static bool dmConversationOnScreen(uint32_t nodeId) {
    return nodeId != 0 && openDmPeerNodeId() == nodeId && !glanceOverlayHidesUi();
}

static uint32_t s_lastActivityMs = 0;
// Quiet gap after the last keypress/touch before a debounced transcript
// snapshot is allowed to take the SPI bus. Comfortably longer than the gap
// between scroll detents during a fast scroll, so a continuous scroll reads as
// "still interacting" rather than repeatedly opening a window for a write.
static constexpr uint32_t kPersistInputIdleMs = 250UL;
static constexpr uint32_t kScreenWakeInputDelayMs = 3000UL;
static constexpr uint32_t kScreenSleepHoldMs = 2000UL;
static uint32_t s_screenWakeBlockedUntilMs = 0;

// ── Wake button ──────────────────────────────────────────────────────────────
// Compiled on every board, unlike the servicing below it (HAS_WAKE_BUTTON):
// powerSaveShouldNap() reads the count, and a board with no wake button simply
// never moves it off zero.
//
// Debounce and hold timing for a board's dedicated screen/wake button. One
// instance per physical button, handed the raw level each poll by
// serviceWakeButton(), which decides between the tap and the hold and
// dispatches to wakeButtonTap()/wakeButtonHold().
//
// Declared up here with the rest of the screen state rather than beside that
// code: the Mesh Deck polls its button off an I2C expander from a function that
// sits well above where the servicing lives, and it needs the complete type to
// keep an instance.
struct WakeButtonPoll {
    bool     rawPressed    = false;
    bool     stablePressed = false;
    uint32_t changedMs     = 0;
    uint32_t holdStartMs   = 0;   // 0 while the button is up
    bool     claimed       = false;   // this press has already been acted on
};
// Long enough to reject contact bounce, short enough that the Mesh Deck's 40 ms
// expander poll confirms a press on its very next read rather than the one
// after.
static constexpr uint32_t kWakeButtonDebounceMs = 25UL;

// How many wake buttons are down right now. powerSaveShouldNap() refuses while
// this is non-zero, because the hold has to be *timed* and the loop has to be
// awake to do it. A nap is up to NAP_MAX_MS (1500 ms), so one landing mid-hold
// stretches the two seconds to three and a half -- long enough that the user
// lets go first and the gesture appears not to work.
//
// The GPIO buttons are covered twice over: enterLightNap() already refuses to
// sleep on an asserted wake line. The expander buttons are why this exists --
// reading them is what clears their shared interrupt line, so it is back to
// idle while the button is still down and nothing else would hold the CPU up.
// A count rather than a flag because more than one board has two of them.
static uint8_t s_wakeButtonsHeld = 0;
#if defined(DEVICE_TDECK) && HAS_TRACKBALL && (TBALL_CLICK >= 0)
static bool s_tdeckTrackballHoldActive = false;
static bool s_tdeckTrackballHoldTriggered = false;
static uint32_t s_tdeckTrackballHoldStartMs = 0;
static bool s_tdeckSuppressRollerClick = false;
#endif
// A UI rebuild asked for from inside an LVGL event callback — the nav-bar
// toggle, or a theme change arriving from web config — has to wait for the
// loop: the widget the callback is running on is one of the things the rebuild
// deletes.
static bool s_uiRebuildPending = false;
static bool s_uiRebuildReopenCfg = false;
static bool s_nodesWifiSessionActive = false;
static bool s_nodesWifiStateChanged = false;
static wifi_mode_t s_nodesWifiPrevMode = WIFI_OFF;
static bool s_nodesWifiPrevConnected = false;
static uint32_t s_nodesMapSelectionNodeId = 0;
static uint32_t s_nodesMapSelectionSinceMs = 0;
static uint32_t s_nodesMapLastPrimePollMs = 0;
static bool s_stateMapCacheReady = false;
static bool s_stateMapBootstrapTried = false;
static bool s_stateMapBootstrapInProgress = false;
static bool s_stateMapBootstrapDone = false;
static bool s_stateMapBootstrapHostChecked = false;
static bool s_stateMapBootstrapStaticHostResolvable = false;
static bool s_stateMapBootstrapTileHostResolvable = false;
static bool s_stateMapBootstrapWifiTouched = false;
static wifi_mode_t s_stateMapBootstrapPrevMode = WIFI_OFF;
static bool s_stateMapBootstrapPrevConnected = false;
static uint32_t s_stateMapBootstrapConnectStartMs = 0;
static uint32_t s_stateMapBootstrapLastStepMs = 0;
static int s_stateMapBootstrapNextIndex = 0;
static int s_stateMapBootstrapDownloaded = 0;
static int s_stateMapBootstrapFailed = 0;
static uint32_t s_stateMapOnDemandLastTryMs = 0;
static char s_stateMapOnDemandLastCode[3] = "";
static uint32_t s_detailMapOnDemandLastTryMs = 0;
static char s_detailMapOnDemandLastKey[11] = "";
static int s_detailMapOnDemandLastLod = -1;
static TaskHandle_t s_detailMapOnDemandTask = nullptr;
static volatile bool s_detailMapOnDemandBusy = false;
static volatile bool s_detailMapOnDemandDone = false;
static volatile bool s_detailMapOnDemandOk = false;
static float s_detailMapOnDemandLat = 0.0f;
static float s_detailMapOnDemandLon = 0.0f;
static char s_detailMapOnDemandReqKey[11] = "";
static char s_detailMapOnDemandDoneKey[11] = "";
static int s_detailMapOnDemandReqLod = 1;
static int s_detailMapOnDemandReqZoom = 11;
static bool s_detailMapOnDemandReqReplace = false;
// Worker-local download buffer kept out of the task stack.
static uint8_t s_detailMapOnDemandIoBuf[1024] = {0};
// Cached state maps are drawn by the Locate modal, on the boards that have it.
static constexpr bool kStateMapsEnabled = (HAS_STATE_MAPS != 0);
// State maps are uploaded from web config (POST /state-map-upload). Their old
// in-firmware fetch path used TLS/staticmap hosts and remains disabled.
//
// So everything downstream of "the cache is incomplete, go and get it" has to
// stay switched off. Left on, an incomplete cache would make every boot flip
// WiFi modes, run the connect, and fail fifty downloads that cannot succeed —
// and the stale-cache sweep below would recursively delete maps the user put
// there by hand, which nothing on the device can put back.
static constexpr bool kStateMapsCanDownload = false;
// Live fallback for missing detail maps while viewing Locate. Plain HTTP by
// design; operator proxy terminates TLS upstream.
static constexpr bool kLiveDetailMapsCanDownload = (HAS_NODE_LOCATE != 0);
static constexpr uint32_t kLiveDetailMapRetryMs = 1200UL;
static constexpr int kLiveDetailMapLodStreet = 1;
static constexpr int kLiveDetailMapLodBuilding = 2;
static constexpr int kLiveDetailMapZoomStreet = 11;
static constexpr int kLiveDetailMapZoomBuilding = 14;
static constexpr const char *kLiveDetailMapTileProxyBase =
    "http://maps.camillia.sumat.org";
static constexpr uint32_t kRuntimeTlsMinInternalFree = 70000;
static constexpr uint32_t kRuntimeTlsMinLargestBlock = 52000;

static bool runtimeHttpsHeapOkay(const char *tag) {
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (freeInt >= kRuntimeTlsMinInternalFree && largestInt >= kRuntimeTlsMinLargestBlock) {
        return true;
    }
    Serial.printf("[https] skip %s (low heap int_free=%u largest=%u)\n",
                  tag ? tag : "request",
                  (unsigned)freeInt,
                  (unsigned)largestInt);
    return false;
}
#if defined(DEVICE_TLORA_PAGER_TFT)
static constexpr bool kPagerWheelChatNav = true;
static constexpr bool kUseScrollKeysForMainNav = true;
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_12;
static constexpr int kMainScreenChannelBtnHeight = 24;
#elif UI_CARDPUTER_COMPACT
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = true;
static constexpr bool kModalCloseUsesEscape = true;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 18;
#elif defined(DEVICE_MESH_DECK)
static constexpr bool kPagerWheelChatNav = false;
// Scroll keys must drive main navigation here. The shared default below leaves
// this false because it is written for the T-Deck, whose trackball moves the
// main screen — this board has no trackball (HAS_TRACKBALL 0), so with it false
// nothing navigates at all: j/k and the arrows all fold onto KEY_SCROLL_UP/DN,
// and that is the only pointer this hardware has.
static constexpr bool kUseScrollKeysForMainNav = true;
// No Esc on the 48-key matrix; Backspace is the close key.
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 22;
#elif defined(DEVICE_M9)
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = true;
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 22;
#elif defined(DEVICE_TDECK_PRO)
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = true;
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 22;
#elif defined(DEVICE_TDISPLAY_P4)
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = true;
static constexpr bool kModalCloseUsesEscape = true;
#if UI_LARGE_PANEL_PROFILE
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_24;
static constexpr int kMainScreenChannelBtnHeight = 48;
#else
// Pixel-doubled to 284x616, where the T-Deck's sizes come out at about the
// T-Deck's physical size: 12 px is ~1.9 mm here against its ~2.1 mm.
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_12;
static constexpr int kMainScreenChannelBtnHeight = 24;
#endif
#elif defined(DEVICE_CROWPANEL_50)
// The default below is sized for a 320x240 panel and read as tiny at 800x480:
// list text (channel list, DM conversations, Nodes) and the channel buttons
// twice as big. Named 18, drawn as 20 (hal/crowpanel_50_fonts.h).
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = false;
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_18;
static constexpr int kMainScreenChannelBtnHeight = 44;
#else
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = false;
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 22;
#endif
// The Mesh Deck's panel is 320x240, the same as the T-Deck's, so it takes the
// T-Deck's roomier chat font rather than the small-screen default.
#if UI_LARGE_PANEL_PROFILE
static const lv_font_t *kChannelChatFont = &lv_font_montserrat_24;
#elif defined(DEVICE_TDECK) || defined(DEVICE_MESH_DECK) || defined(DEVICE_M9) \
    || defined(DEVICE_TDISPLAY_P4)
static const lv_font_t *kChannelChatFont = &lv_font_montserrat_12;
#else
static const lv_font_t *kChannelChatFont = kMainScreenFont;
#endif

// Relative chat/DM sizes keep Medium at the caller's base and step Small down
// one face, including 10 -> 8 px. Boards below use fixed, display-specific sizes.
#if UI_CARDPUTER_COMPACT || defined(DEVICE_TLORA_PAGER_TFT) \
    || defined(DEVICE_TDECK_PRO) || defined(DEVICE_TDISPLAY_P4) \
    || defined(DEVICE_CROWPANEL)
static const lv_font_t *explicitChatFont() {
#if UI_LARGE_PANEL_PROFILE
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:  return &lv_font_montserrat_16;
        case FONT_SIZE_LARGE:  return &lv_font_montserrat_28;
        case FONT_SIZE_XLARGE: return &lv_font_montserrat_32;
        case FONT_SIZE_MEDIUM:
        default:               return &lv_font_montserrat_24;
    }
#elif defined(DEVICE_TDISPLAY_P4)
    // Small leaves more lines on the panel; 20 px has an emoji fallback here.
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:  return &lv_font_montserrat_12;
        case FONT_SIZE_LARGE:  return &lv_font_montserrat_18;
        case FONT_SIZE_XLARGE: return &lv_font_montserrat_20;
        case FONT_SIZE_MEDIUM:
        default:               return &lv_font_montserrat_16;
    }
#elif defined(DEVICE_CROWPANEL)
    // Preserve Medium while making Large/Extra Large readable on the 3.5" panel.
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:  return &lv_font_montserrat_8;
        case FONT_SIZE_LARGE:  return &lv_font_montserrat_18;
        case FONT_SIZE_XLARGE: return &lv_font_montserrat_24;
        case FONT_SIZE_MEDIUM:
        default:               return &lv_font_montserrat_10;
    }
#elif defined(DEVICE_TDECK_PRO)
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:  return &lv_font_montserrat_12;
        case FONT_SIZE_LARGE:  return &lv_font_montserrat_16;
        case FONT_SIZE_XLARGE: return &lv_font_montserrat_18;
        case FONT_SIZE_MEDIUM:
        default:               return &lv_font_montserrat_14;
    }
#else
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:  return &lv_font_montserrat_12;
        case FONT_SIZE_LARGE:  return &lv_font_montserrat_16;
        case FONT_SIZE_XLARGE: return &lv_font_montserrat_18;
        case FONT_SIZE_MEDIUM:
        default:               return &lv_font_montserrat_14;
    }
#endif
}
#endif

static const lv_font_t *scaledChatFontBase(const lv_font_t *base) {
#if UI_CARDPUTER_COMPACT || defined(DEVICE_TLORA_PAGER_TFT) \
    || defined(DEVICE_TDECK_PRO) || defined(DEVICE_TDISPLAY_P4) \
    || defined(DEVICE_CROWPANEL)
    (void)base;
    return explicitChatFont();
#else
    const lv_font_t *out = base;
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:
            if (base == &lv_font_montserrat_10)      out = &lv_font_montserrat_8;
            else if (base == &lv_font_montserrat_12) out = &lv_font_montserrat_10;
            else if (base == &lv_font_montserrat_14) out = &lv_font_montserrat_12;
            break;
        case FONT_SIZE_LARGE:
            if (base == &lv_font_montserrat_10)      out = &lv_font_montserrat_12;
            else if (base == &lv_font_montserrat_12) out = &lv_font_montserrat_14;
            else if (base == &lv_font_montserrat_14) out = &lv_font_montserrat_16;
            break;
        case FONT_SIZE_XLARGE:
            if (base == &lv_font_montserrat_10)      out = &lv_font_montserrat_14;
            else if (base == &lv_font_montserrat_12) out = &lv_font_montserrat_16;
            else if (base == &lv_font_montserrat_14) out = &lv_font_montserrat_18;
            else if (base == &lv_font_montserrat_16) out = &lv_font_montserrat_18;
            break;
        default:
            break;
    }
    return out;
#endif
}

// Chat/DM text face, emoji-enabled. Every chat, DM, name, and preview label
// funnels its font through here, so attaching the emoji fallback at this one
// point is what makes received emoji render everywhere — including inside
// chatFitBubbleLabel(), which measures with this same face so bubble widths
// account for emoji glyphs too.
static const lv_font_t *scaledChatFont(const lv_font_t *base) {
    return emojiFont(scaledChatFontBase(base));
}

#if defined(DEVICE_TDECK_PRO)
// The bold twin of a chat face, for the sender's name in a message. On the
// 1-bit panel neither colour nor a lighter grey can set the name apart from the
// text after it, so weight does.
//
// The twin carries the chat face's line metrics (tools/gen_eink_fonts.py), so a
// bold name and the regular text beside it share a baseline. The face passed
// in is usually emojiFont()'s copy rather than the font itself; both point at
// the same glyph data, which is what identifies the size. The bold copy takes
// over the regular face's fallback chain (icons, then emoji), so a name with an
// emoji in it still draws one.
static const lv_font_t *chatNameFont(const lv_font_t *regular) {
    if (!regular) return regular;
    struct Twin { const lv_font_t *regular; const lv_font_t *bold; };
    static const Twin kTwins[] = {
        { &lv_font_montserrat_12, &lv_font_eink_bold_12 },
        { &lv_font_montserrat_14, &lv_font_eink_bold_14 },
        { &lv_font_montserrat_16, &lv_font_eink_bold_16 },
        { &lv_font_montserrat_18, &lv_font_eink_bold_18 },
    };
    struct Made { const lv_font_t *regular; lv_font_t bold; };
    static Made s_made[8];
    static int s_madeCount = 0;
    for (int i = 0; i < s_madeCount; i++) {
        if (s_made[i].regular == regular) return &s_made[i].bold;
    }
    for (const Twin &t : kTwins) {
        if (t.regular->dsc != regular->dsc) continue;
        if (s_madeCount >= (int)(sizeof(s_made) / sizeof(s_made[0]))) return t.bold;
        Made &m = s_made[s_madeCount++];
        m.regular = regular;
        m.bold = *t.bold;
        m.bold.fallback = regular->fallback;
        return &m.bold;
    }
    return regular;
}
#endif

static const char *fontSizeName(uint8_t size) {
    switch (size) {
        case FONT_SIZE_SMALL:  return TR("Small");
        case FONT_SIZE_LARGE:  return TR("Large");
        case FONT_SIZE_XLARGE: return TR("Extra Large");
        case FONT_SIZE_MEDIUM:
        default:               return TR("Medium");
    }
}
static bool s_pagerChatCursorMode = false;
static int s_pagerChatCursorDisplayIndex = -1;
// One-shot: after leaving chat cursor mode, reopen plain chat at latest.
static bool s_chatScrollToLatestOnce = false;
#if UI_CARDPUTER_COMPACT
static bool s_cardputerMainChatPanelFocused = false;
#endif
static int s_cardputerDropdownSelection = -1;

static constexpr int kRxDedupSize = 32;
struct SeenPkt {
    uint32_t from;
    uint32_t id;
};
static SeenPkt s_seenPkts[kRxDedupSize] = {};
static int s_seenHead = 0;

// Count of packets we have relayed onto the mesh this boot (managed flood).
static uint32_t s_rebroadcastCount = 0;
static uint32_t s_relayStoodDownCount = 0;   // queued relays cancelled by someone else's copy
// Licensed mode, since boot: packets dropped for an unlicensed peer
// (lic::allowPacket), and NodeInfo not stored for an is_licensed mismatch.
static uint32_t s_licDropUnlicensed = 0;
static uint32_t s_licNodeInfoMismatch = 0;
// True while processMeshPacket() is handling a packet our own radio received,
// as against one injected from the MQTT downlink: only an on-air copy tells a
// queued relay that someone nearby has already carried the packet.
static bool s_rxFromRadio = false;

#if LV_USE_LOG
// LVGL's warnings, on the same wire as everything else. Without this they are
// discarded, and a failed image decode looks identical to a blank image.
static void lvglLogToSerial(lv_log_level_t level, const char *buf) {
    if (!buf) return;
    const char *tag = (level >= LV_LOG_LEVEL_ERROR) ? "ERROR"
                    : (level >= LV_LOG_LEVEL_WARN)  ? "warn" : "info";
    Serial.printf("[lvgl:%s] %s", tag, buf);
    size_t n = strlen(buf);
    if (n == 0 || buf[n - 1] != '\n') Serial.println();
}
#endif

static inline bool isBackspaceKey(char k) {
    // KEY_BACK_BTN is the M9's dedicated Back button, split out from
    // KEY_BACKSPACE so the compose box can treat the two differently. It is
    // counted here on purpose: every other caller wants the old behaviour, and
    // only the compose switches, which match exact case values, see it apart.
    return k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD || k == KEY_BACK_BTN;
}

static inline bool isModalCloseKey(char k) {
    if (kModalCloseUsesEscape) {
        return k == KEY_ESCAPE;
    }
    return isBackspaceKey(k) || k == KEY_ESCAPE;
}

static inline const char *modalCloseKeyLabel() {
    return kModalCloseUsesEscape ? "Esc" : "Bksp";
}

// Configures a vertical scroll container that locks to its content edges:
// disabling SCROLL_ELASTIC removes the rubber-band overscroll so a touch drag
// (or momentum fling) can't pull the first/last item away from the edge into
// empty space. Use everywhere we'd otherwise call lv_obj_set_scroll_dir(VER).
static void setupVScroll(lv_obj_t *obj) {
    if (!obj) return;
    lv_obj_set_scroll_dir(obj, LV_DIR_VER);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
}

static inline bool lvObjValid(lv_obj_t *obj) {
    return obj && lv_obj_is_valid(obj);
}

static inline bool lvObjAlive(lv_obj_t *obj) {
    return lvObjValid(obj) && (lv_obj_get_disp(obj) != nullptr);
}

static inline void lvObjDeleteSafe(lv_obj_t *&obj) {
    if (lvObjValid(obj)) {
        lv_obj_del(obj);
    }
    obj = nullptr;
}

// ── New-message dots ─────────────────────────────────────────────────────────
// A received message carries a small dot in the theme accent until its
// conversation counts as read (serviceFreshMarks()): between the time and the
// sender in the classic and IRC styles, beside the bubble in the bubble styles.
// The chat and DM views register each dot as they build it, and reading the
// conversation clears the lot without a rebuild. Three kinds, one per style:
//  - IRC: an object in a slot every row reserves, so text keeps one column;
//    cleared by going transparent, which keeps the slot.
//  - Bubbles: an object beside the bubble; cleared by hiding it, which moves
//    the bubble back into line with the rest.
//  - Classic: a span in the line's span group; cleared by emptying it.
// Messages never blink: the dot is the whole cue.
enum FreshDotKind : uint8_t { FRESH_DOT_SLOT, FRESH_DOT_OBJ, FRESH_DOT_SPAN };
struct FreshDotSet {
    lv_obj_t    *objs[48];    // the dot, or for FRESH_DOT_SPAN the span group
    lv_span_t   *spans[48];
    FreshDotKind kinds[48];
    int          count;
    uint32_t     owner;   // the channel index / DM peer the rows were built for
};
static FreshDotSet s_chatDots = {};
static FreshDotSet s_dmDots = {};

// Room for one more. A row is only given a dot it can register, since an
// unregistered one would outlive the conversation being read.
static inline bool freshDotRoom(const FreshDotSet &set) {
    return set.count < (int)(sizeof(set.objs) / sizeof(set.objs[0]));
}

static void freshDotTrack(FreshDotSet &set, lv_obj_t *obj, FreshDotKind kind,
                          lv_span_t *span = nullptr) {
    if (!obj || !freshDotRoom(set)) return;
    set.objs[set.count] = obj;
    set.spans[set.count] = span;
    set.kinds[set.count] = kind;
    set.count++;
}

// The view is about to delete the rows it registered and build new ones for
// `owner`.
static inline void freshDotsForget(FreshDotSet &set, uint32_t owner) {
    set.count = 0;
    set.owner = owner;
}

// `owner` was read: if the rows on screen are still its rows (the view may
// already have rebuilt for the next conversation), their dots go.
static void freshDotsClear(FreshDotSet &set, uint32_t owner) {
    if (set.owner != owner) return;
    for (int i = 0; i < set.count; i++) {
        lv_obj_t *o = set.objs[i];
        if (!lvObjValid(o)) continue;
        switch (set.kinds[i]) {
            case FRESH_DOT_SLOT:
                lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
                break;
            case FRESH_DOT_OBJ:
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
                break;
            case FRESH_DOT_SPAN:
                lv_spangroup_set_span_text(o, set.spans[i], "");
                lv_spangroup_refresh(o);
                break;
        }
    }
    set.count = 0;
}

// Scrolls a list vertically by dy, clamped to the remaining scroll range so the
// first/last item stays pinned to the edge. lv_obj_scroll_by(LV_ANIM_OFF) is
// unbounded in LVGL 8.3 and will otherwise scroll past the content into empty
// space. Positive dy reveals content above; negative dy reveals content below.
static void scrollListClamped(lv_obj_t *obj, lv_coord_t dy) {
    if (!obj || dy == 0) return;
    lv_coord_t top = lv_obj_get_scroll_top(obj);        // room to scroll up (dy>0)
    lv_coord_t bottom = lv_obj_get_scroll_bottom(obj);  // room to scroll down (dy<0)
    if (top < 0) top = 0;
    if (bottom < 0) bottom = 0;
    if (dy > top) dy = top;
    if (dy < -bottom) dy = -bottom;
    if (dy != 0) lv_obj_scroll_by(obj, 0, dy, LV_ANIM_OFF);
}

#if UI_CARDPUTER_COMPACT
static inline char remapCardputerUiKey(char k, bool allowScrollRemap) {
    if (k == '`' || k == '~') return KEY_ESCAPE;
    if (allowScrollRemap) {
        // All four of the arrow-labelled keys, not just the vertical pair.
        //
        // ';' and '.' were remapped here from the start; ',' and '/' were only
        // ever mapped in the driver's Fn branch (keyboard.cpp), so the two
        // halves of the same physical cluster behaved differently: up and down
        // worked bare, left and right needed Fn held. That is why a slider
        // could be moved with the up/down keys and not with left/right, and it
        // applied equally to anything else reading PREV/NEXT_CHAN.
        //
        // Typing is unaffected: allowScrollRemap is false in every typing
        // context, so a comma or slash in a message, filter or text field is
        // still a comma or slash. Fn+, and Fn+/ keep working too.
        if (k == ';') return KEY_SCROLL_UP;
        if (k == '.') return KEY_SCROLL_DN;
        if (k == ',') return KEY_PREV_CHAN;
        if (k == '/') return KEY_NEXT_CHAN;
    }
    return k;
}
#endif

// Mesh Deck builds touch-only but its keyboard is optional, so it keeps j/k.
#if !UI_TOUCH_ONLY_PROFILE || defined(DEVICE_MESH_DECK)
static inline char remapJkUiKey(char k, bool allowScrollRemap) {
    if (!allowScrollRemap) return k;
    // This firmware's convention is j=up, k=down — deliberately NOT vim's
    // j=down/k=up. Every screen is expected to match it, so j folds onto
    // KEY_SCROLL_UP and k onto KEY_SCROLL_DN and the tokens mean what they say.
    if (k == 'j' || k == 'J') return KEY_SCROLL_UP;
    if (k == 'k' || k == 'K') return KEY_SCROLL_DN;
    return k;
}
#endif

static void refreshChatView(bool force = false);
static uint32_t chatDateBucket(uint32_t epoch);
static void formatChatDateLabel(uint32_t epoch, char *out, size_t len);
static void formatChatClock(uint32_t epoch, char *out, size_t len);
static void chatParsePrefix(const char *line, char *iconOut, size_t iconLen,
                            char *timeOut, size_t timeLen);
static void chatComposeBubbleMeta(const char *icon, const char *clock,
                                  char *out, size_t len);
static void insertChatDateMarker(lv_obj_t *parent, uint32_t epoch,
                                 const lv_font_t *font);
static void collectChatRows(const DisplayLine **rows, int &rowCount);
static void buildChatDisplayOrder(const DisplayLine *const *rows, int rowCount,
                                  int *displayOrder, int &displayCount);
static void buildChatCursorOrder(const DisplayLine *const *rows,
                                 const int *displayOrder, int displayCount,
                                 int *cursorOrder, int &cursorCount);
static void refreshHeaderTime(bool force = false);
static void refreshHeaderStatus(bool force = false);
static void refreshDmAlertIndicator();
static void refreshChatAlertIndicator();
static bool chatPanelInFront();
static void layoutHeaderInlineItems();
static lv_obj_t *buildScreenHeader(lv_obj_t *modal, const char *text);
static void layoutScreenHeader(ScreenHeader &h);
static void labelSingleLine(lv_obj_t *label);
static void screenHeaderSetTitle(lv_obj_t *title, const char *text);
#if UI_TOUCH_ONLY_PROFILE || UI_CFG_SECTIONS
static lv_obj_t *buildSecondaryHeader(lv_obj_t *modal);
static lv_obj_t *secondaryHeaderBtn(lv_obj_t *row, const char *text, lv_event_cb_t cb);
#endif
static void refreshChannelGlow(bool force = false);
static void pumpKeyboardInput();
#if HAS_ADMIN_TERMINAL
// The remote admin terminal. Defined with the transport it drives, far below;
// the node actions modal opens it and sits well above that.
static void openAdminTerminalModal(uint32_t nodeId);
static void closeAdminTerminalModal();
static bool adminTerminalHandleKey(char k);
static void refreshAdminTranscript();
#endif
static void openComposePrompt(uint32_t replyPacketId = 0,
                              const char *replyText = nullptr,
                              bool allowSelectedReplyFallback = true);
static void closeComposePrompt();
static void sendComposeMessage();
static void onChatNewMessagePressed(lv_event_t *e);
static void onComposeKeyboardEvent(lv_event_t *e);
static void onComposeSendPressed(lv_event_t *e);
static void onComposeCancelPressed(lv_event_t *e);
static void onComposeInputChanged(lv_event_t *e);
static void updateComposeCharCount();
static void refreshChatComposeButtonState();
static void openEmojiPicker(bool sendMode = false, bool symbolTray = false);
static void closeEmojiPicker();
static void onChatMessagePressed(lv_event_t *e);
static void recomputeChannelHashes();
static void initCfgActions();
static void refreshCfgModal();
// Repaints the existing rows for a moved selection, without rebuilding them.
static void refreshCfgSelection();
static void openCfgModal();
static void closeCfgModal();
static void activateCfgSelection();
static void performCfgAction(int actionId);
// A confirm that is not about a Config row: `text` is shown verbatim and `fn`
// runs on Yes instead of performCfgAction(). Everything that reboots as a
// result of a config change goes through this, whether it started as a row or
// as a selection inside a picker.
//
// Answering No leaves whatever is underneath alone, which is what lets a picker
// stay open behind the dialog -- the reason the WiFi delete above had to build
// a dialog of its own rather than use this one.
static void openCfgConfirmModal(int actionId, const char *text = nullptr,
                                CfgConfirmFn fn = nullptr, int fnArg = 0,
                                const char *altLabel = nullptr,
                                CfgConfirmFn altFn = nullptr);
static void closeCfgConfirmModal();
// `title` overrides the default "Action Result" heading, for the callers that
// are not reporting the result of a Config action -- the OTA boot notice, and
// the SD card warning below it.
static void openCfgActionMessageModal(const char *msg, const char *title = nullptr);
static void closeCfgActionMessageModal();
static void onCfgActionMessageBackdropPressed(lv_event_t *e);
static void onCfgActionRowPressed(lv_event_t *e);
static void openCfgColorPickerModal();
static void openCfgNodeNameModal();
static void closeCfgNodeNameModal();
static void closeCfgPresetModal();
#if HAS_RUNTIME_ORIENTATION
static void closeCfgOrientModal();
static void openCfgOrientModal();
#endif
#if HAS_NAV_ALIGN_SETTING
static void closeCfgNavAlignModal();
static void openCfgNavAlignModal();
#endif
#if HAS_RUNTIME_ORIENTATION
// Defined with the orientation state far below; the Config row label is
// built long before that, and is the one caller that comes first.
static inline const char *uiOrientName(uint8_t o);
#endif
static void openCfgPresetModal();
static void openCfgCommunityModal();
static void closeCfgCommunityModal();
static int communityPresetRadioIndex(const CommunityPreset &cp);
static void communityPresetSummary(int idx, char *out, size_t outLen);
static const char *communityPresetApply(int idx);
static void openCfgBrightnessModal();
static void openCfgScreenTimeoutModal();
static const char *screenTimeoutName(uint32_t secs);
#if FEATURE_LOCK_SCREEN
// Both are wanted by the config row renderer, which sits well above where the
// lock-screen picker is defined — beside the screen-timeout picker it copies.
static void openCfgLockScreenOffModal();
static const char *lockScreenOffName(uint32_t secs);
#endif
static void closeCfgBrightnessModal();
#if HAS_VOLUME_CONTROL
static void openCfgVolumeModal();
static void closeCfgVolumeModal();
static void revertCfgVolumePreview();
static void closeCfgSliderModal();
static void cancelCfgSlider();
static void stepCfgVolume(int steps);
static void cancelCfgVolume();
static void applyCfgVolume();
#endif
static void revertCfgBrightnessPreview();
static const char *battTrimText(int trim, char *buf, size_t bufLen);
static void openCfgBattCalModal();
static void closeCfgBattCalModal();
static void revertCfgBattCalPreview();
static void refreshCfgBattCalReadout(bool force);
static void stepCfgBattCal(int steps);
static void cancelCfgBattCal();
static void applyCfgBattCal();
static void applyBrightness();
static void onCfgBrightSliderChanged(lv_event_t *e);
static void onCfgBrightBackdropPressed(lv_event_t *e);
static void closeCfgColorPickerModal();
static void refreshCfgColorPickerModal();
static void applyCfgColorSelection(int idx);
static void onCfgColorRowPressed(lv_event_t *e);
static void onCfgColorBackdropPressed(lv_event_t *e);
static void openAlertSoundModal();
static void closeAlertSoundModal();
static void refreshAlertSoundSelection();
static void applyAlertSoundSelection(int mode);
static void onAlertSoundRowPressed(lv_event_t *e);
static void onAlertSoundBackdropPressed(lv_event_t *e);
#if HAS_NOTIFY_LED
static void openNotifyLedColorModal(bool forDm);
static void closeNotifyLedColorModal();
static void refreshNotifyLedColorSelection();
static void applyNotifyLedColorSelection(int color);
static void onNotifyLedColorRowPressed(lv_event_t *e);
static void onNotifyLedColorBackdropPressed(lv_event_t *e);
#endif
static void openChatStyleModal();
static void closeChatStyleModal();
static void refreshChatStyleSelection();
static void applyChatStyleSelection(int style);
static void onChatStyleRowPressed(lv_event_t *e);
static void onChatStyleBackdropPressed(lv_event_t *e);
static lv_color_t tftColorToLv(uint16_t c);
static void openThemeModal();
static void closeThemeModal();
static void refreshThemeSelection();
static void applyThemeSelection(int idx);
static void onThemeRowPressed(lv_event_t *e);
static void onThemeBackdropPressed(lv_event_t *e);
static void openCfgWifiPickerModal(bool forOnboarding = false);
static void closeCfgWifiPickerModal();
static void refreshCfgWifiPickerModal();
static void onCfgWifiRowPressed(lv_event_t *e);
static void onCfgWifiBackdropPressed(lv_event_t *e);
static void applyCfgWifiSelection(int idx);
#if HAS_BLE_KEYBOARD
static void openCfgBleKbdModal(bool autoScan = true);
static void closeCfgBleKbdModal();
static void refreshCfgBleKbdModal();
static void onCfgBleKbdRowPressed(lv_event_t *e);
static void openBleKbdRepairPrompt();
static void closeBleKbdRepairPrompt();
static void bleKbdRepairAccept();
static void bleKbdRepairIgnore();
static void bleKbdRepairMoveSelection();
#endif
static void openCfgWifiScanModal();
static void closeCfgWifiScanModal();
static void refreshCfgWifiScanModal(bool runScan);
static void openCfgWifiPassModal(int scanIdx);
static void closeCfgWifiPassModal();
static void cfgWifiConnectFromPassModal();
#if UI_TOUCH_ONLY_PROFILE
static void onCfgHeaderInfoPressed(lv_event_t *e);
#endif
#if !defined(DEVICE_TLORA_PAGER_TFT)
static void openNodeInfoModal();
static void closeNodeInfoModal();
#endif
#if HAS_MESSAGE_ACTIONS
static void openMsgSenderInfoModal(uint32_t from, uint32_t packetId);
static void closeMsgSenderInfoModal();
#endif
static void openLegendModal();
static void closeLegendModal();
static void openReleaseNotesModal();
static void closeReleaseNotesModal();
// Clock plumbing lives down with the NTP code, but the Time and Date modal sits
// well above it.
static void applyTimezoneFromConfig();
static bool applyManualClock(int year, int mon, int day, int hour, int minute);
static void openLiveModal();
static void closeLiveModal();
static void openSysStatsModal();
static void closeSysStatsModal();
static void refreshSysStatsModal(bool force);
static void buildSysStatsColumns(char *cpuOut, char *memOut, char *stoOut, size_t sz);
static inline bool channelIsMuted(int chanIdx);
static bool channelSharesLocation(int chanIdx);
static void openChannelActionsModal();
static void closeChannelActionsModal();
static void refreshChannelActionsModal();
static void toggleActiveChannelMute();
static void toggleActiveChannelShareLocation();
static void onChannelActionMutePressed(lv_event_t *e);
static void onChannelActionSharePressed(lv_event_t *e);
static void logLvglMemDiag(const char *tag);
static void onHeltecBottomNavPressed(lv_event_t *e);
// Closes whichever screen Tools opened is up (all of them are null-safe), and
// Live too when `closeLive`. Defined after the last of them.
static void closeToolScreens(bool closeLive);
static void populateHeltecBottomNav(lv_obj_t *bar, int activeTarget);
static void appendHeltecBottomNav(lv_obj_t *parent, int activeTarget);
static void refreshLiveView(bool force = false);
static void openChUtilChartModal();
static void closeChUtilChartModal();
static void refreshChUtilChart(bool force = false);
static void openSnrRssiChartModal();
static void closeSnrRssiChartModal();
static void refreshSnrRssiChart(bool force = false);
#if HAS_HOME_DASHBOARD
// Defined with the dashboard itself, far below; the touch nav bar's Home cell
// is built long before that and has to be able to name them.
static void openHomeDashboard();
static void closeHomeDashboard();
#if FEATURE_LOCK_SCREEN
// The lock screen's copy of the dashboard's carousel. Declared here for the
// same reason: showTdeckProSleepClock() builds the overlay thousands of lines
// before the page builders, the node collector and the card styling it needs
// all exist. See the block below openHomeDashboard() for what these do.
static lv_obj_t *buildLockCarousel(lv_obj_t *overlay, int top, int height);
static void      resetLockCarousel();
static void      refreshLockCarouselPage();
static void      serviceLockCarousel(uint32_t nowMs);
#endif
#endif
static void openLiveToolsModal();
static void closeLiveToolsModal();
static void openLiveFilterModal();
static void closeLiveFilterModal();
static void refreshLiveFilterHeader();
#if FEATURE_DISCOVERY
static void openDiscoveryModal();
static void closeDiscoveryModal();
static void refreshDiscoveryModal(bool force = false);
static void openDiscoveryPresetModal();
static void closeDiscoveryPresetModal();
// Puts the radio back on the node's own preset. `aborted` is true when the scan
// did not run its window out — the screen was closed, or a save retuned under it.
static void discoveryEndPresetScan(bool aborted);
#endif
// Frames waiting out their contention jitter are addressed to the preset they
// were heard on; a retune under them would put them on the air of another mesh.
static void dropPendingRebroadcasts();
// A temporary retune to another preset that must always be undone: everything
// tempTuneLeave() needs to put the radio and channel 0 back exactly as they
// were. Never written to s_cfg (see tempTuneEnter()).
struct TempTuneSnapshot {
    float       freq, bw;
    uint8_t     sf, cr;
    bool        renamed;      // channel 0 was renamed to the target preset's name
    const char *prevName;     // CHANNEL_KEYS[0].name, verbatim
    char        prevBuf[16];  // CHANNEL_KEYS[0].name_buf, verbatim
};
static void tempTuneEnter(uint8_t preset, TempTuneSnapshot &snap, bool renameCh0);
static void tempTuneLeave(TempTuneSnapshot &snap);
// MeshBeacon broadcast (port 37): hourly-ish advert of our mesh onto a target
// preset's public channel. Called from the config save paths.
static void meshBeaconSettingsChanged();
static void meshBeaconConfigSaved();
// What the Beacons screen's "Your beacon" line reports.
struct MeshBeaconStatus {
    enum State : uint8_t { OFF, WAITING, HELD_SCAN, HELD_AIRTIME, HELD_PRESET } state;
    uint32_t nextInMs;    // WAITING / HELD_AIRTIME: ms until the next attempt
    uint32_t lastEpoch;   // epoch seconds of the last send, 0 = unknown/never
};
static MeshBeaconStatus meshBeaconStatus();
static const char *meshBeaconHomeChannelName();
static void openBeaconsModal();
#if HAS_WEATHER
static void openWeatherModal();
static void closeWeatherModal();
static void weatherRefresh();
#endif
static void closeBeaconsModal();
static void refreshBeaconsModal(bool force = false);
#if FEATURE_MQTT_MONITOR
static void openMqttMonitorModal();
static void closeMqttMonitorModal();
static void refreshMqttMonitorModal(bool force = false);
static void openMqttSendModal();
static void closeMqttSendModal();
static void openMqttConfirmModal(int chan);
static void closeMqttConfirmModal();
static void mqttSendConfirmed();
#endif
static void chartPushSample(ChartHist &h, float value);
static void openDmModal();
static void closeDmModal();
static void refreshDmModal(bool force = false);
static void refreshDmPanelFocusStyles();
static DmConv *selectedDmConversation();
// allowCompose=false stops at focusing the message panel (Enter); the compose
// dialog is only opened when true (Space).
static void activateDmSelection(bool allowCompose = true);
static void chatComposeFromButton();
static void emojiPickerActivate(int idx);

// Bubble chat helpers (defined further down, but also used by the DM renderer
// so DMs can share the Bubbles style).
static const char *chatStripPrefix(const char *line);
static const lv_font_t *chatAckMarkerFont(const lv_font_t *chatFont);
static void chatSetAckedLineText(lv_obj_t *sg, const char *line, const lv_font_t *chatFont);
static void chatSetLineSpans(lv_obj_t *sg, const char *line, const lv_font_t *chatFont,
                             bool ackMark, const lv_font_t *nameFont,
                             lv_span_t **outFreshDot = nullptr);
static void chatBubbleBeginRender(lv_obj_t *list);
static inline void stylePaperMessageRow(lv_obj_t *row) {
#if defined(DEVICE_TDECK_PRO)
    if (!row) return;
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
#else
    LV_UNUSED(row);
#endif
}
static void chatMakeBubble(lv_obj_t *list, uint32_t sender, bool isMe,
                           const char *metaTag, const char *nameTag,
                           const char *body,
                           DisplayLine::AckState ackState,
                           uint32_t replyPacketId, bool isSelected,
                           lv_obj_t **outLast, lv_obj_t **outSelected,
                           lv_event_cb_t onPressed,
                           lv_obj_t **outFreshDot = nullptr);
static void chatMakeIrcRow(lv_obj_t *list, const lv_font_t *font,
                           uint32_t sender, bool isMe,
                           const char *timeText, const char *nameText,
                           const char *body,
                           DisplayLine::AckState ackState,
                           uint32_t replyPacketId, bool isSelected,
                           lv_obj_t **outLast, lv_obj_t **outSelected,
                           lv_event_cb_t onPressed,
                           lv_obj_t **outFreshDot = nullptr);

// Per-row helper text under an option's name in the CFG picker modals. The
// Cardputer's 240x135 panel can't spare the ~13px per row it costs — three
// styles' worth pushed the last option off the modal entirely — so there it
// renders names only.
#if UI_CARDPUTER_COMPACT
static constexpr bool kModalRowDescriptions = false;
#else
static constexpr bool kModalRowDescriptions = true;
#endif

// Chat style helpers. Filled Bubble can be omitted by a board while Default
// and Outline retain their renderer meanings.
static const char *chatStyleName(uint8_t style) {
    switch (style) {
        case CHAT_STYLE_BUBBLES: return TR("Bubbles");
        case CHAT_STYLE_OUTLINE: return TR("Outline");
        case CHAT_STYLE_IRC:     return "IRC";   // a name, so not translated
        default:
#if defined(DEVICE_TDECK_PRO)
            return "Default";
#else
            return TR("Classic");
#endif
    }
}
static inline bool chatStyleUsesBubbles(uint8_t style) {
    return style == CHAT_STYLE_BUBBLES || style == CHAT_STYLE_OUTLINE;
}
static int chatStyleStepSelection(int current, int delta) {
    if (delta == 0) return current;
    int next = current + (delta < 0 ? -1 : 1);
    while (next >= CHAT_STYLE_CLASSIC && next <= CHAT_STYLE_MAX
           && !cfgChatStyleAllowed(next)) {
        next += (delta < 0 ? -1 : 1);
    }
    return cfgChatStyleAllowed(next) ? next : current;
}
static const char *chatNameStyleName(uint8_t style) {
    return (style == CHAT_NAME_LONG) ? TR("Long") : TR("Short");
}
// Resolve the sender label shown in chat, honoring the Chat Names setting.
// Long style uses the node's advertised long name when known; otherwise (and
// for Short style) it falls back to the standard short-name/hex label.
static void chatSenderLabel(uint32_t nodeId, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (s_cfg.chatNameStyle == CHAT_NAME_LONG) {
        NodeEntry *n = Nodes.find(nodeId);
        if (n && n->hasName && n->longName[0]) {
            snprintf(out, outLen, "%s", n->longName);
            return;
        }
    }
    liveNodeLabel(nodeId, out, outLen, false);
}

static void closeDmDeleteConfirm();
#if UI_TOUCH_ONLY_PROFILE
static void openLiveClearConfirm();
static void onLiveRowLongPressed(lv_event_t *e);
#endif
static void dmRequestDeleteSelectedConversation();
static void onDmConversationPressed(lv_event_t *e);
static void onDmConversationPressState(lv_event_t *e);
static void openDmNodePicker();
static void closeDmNodePicker();
static void refreshDmNodePicker(bool force = false);
static void snapshotNodesForDmPicker();
static const NodeEntry *selectedDmNodeForPicker();
static void activateDmNodePickerSelection();
static void dmNodePickerApplyFilter();
static bool dmNodePickerContainsNoCase(const char *text, const char *needle);
static bool shouldHideChatLine(const char *text);
static void openNodesModal();
static void closeNodesModal();
static void snapshotNodesForModal();
static void nodesApplyFilter();
static void applyNodesFilterText(const char *text);
static void openNodesFilterDialog();
static void closeNodesFilterDialog();
static void onNodesFilterButtonPressed(lv_event_t *e);
static void nodesSearchButtonActivate();
static void refreshNodesSearchButton();
static void onNodesFilterKeyboardEvent(lv_event_t *e);
static void onNodesFilterInputEvent(lv_event_t *e);
static void nodesClampRenderWindow();
static lv_obj_t *selectedNodesRowObj();
static void refreshNodesHint();
static void refreshNodesListRows();
static void refreshNodesListSelection();
static void refreshNodesDetails();
static void refreshNodesPanelFocusStyles();
static void onNodeSnapshotPressed(lv_event_t *e);
#if UI_TOUCH_ONLY_PROFILE
static void onNodeSnapshotClicked(lv_event_t *e);
#endif
static void onNodesActionRowPressed(lv_event_t *e);
static void openNodesActionMenu();
#if NODES_INFO_AS_MODAL
static void openNodesInfoDlg();
static void closeNodesInfoDlg();
#endif
static void openNodesActionMenuFor(uint32_t nodeId, bool msgMode = false,
                                   uint32_t packetId = 0);
// Defined down with the action menu, but the Locate modal above it wants the
// same name-for-a-node rule (long name, short name on the narrow panel, else id).
static void nodesActionTitleLabel(uint32_t nodeId, char *out, size_t outLen);
// Defined with the DM/Live confirmations it shares a dialog with; the node
// action menu, which is built well above them, opens it for Delete Node.
static void openDestructiveConfirm(DestructiveConfirmAction action, uint32_t nodeId);
// Message Actions: the same modal with the tapback/Reply rows, acting on a chat
// message. Falls back to the node menu when there is no packet id.
static void openMessageActionMenu(uint32_t packetId, uint32_t senderNodeId);
// Apply the packet signature policy (s_cfg.packetSigPolicy, src/sig_policy.h)
// to a decrypted packet: check its Data.xeddsa_signature against the sender's
// stored public key, then decide whether to accept it the way a 2.8.2 node on
// the same policy would. Called once per packet from processMeshPacket(), after
// any PKI decrypt and before anything else touches it — node list, relay, MQTT
// uplink — so RF, MQTT and PKI traffic are all judged the same way, and a
// dropped packet goes no further, as upstream drops it in its routing gate.
//
// A packet we could not decrypt has no Data to check and is not judged here;
// it is relayed opaquely, as upstream does.
static void noteKeyConflictRaised(uint32_t nodeId);

// True for a packet that came in on an AES-CCM channel, whose tag already
// authenticates it: 2.8.2 neither signs on one nor asks for a signature there.
static bool packetOnAeadChannel(const MeshPacket &pkt) {
    return pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS
        && channelUsesAead(CHANNEL_KEYS[pkt.chanIdx]);
}

static SigPolicyResult checkPacketSignature(const MeshPacket &pkt) {
    if (!pkt.decrypted) return {true, false};
    // Accepted under every policy with any signature ignored, as 2.8.2 does:
    // every hop rewrites the route, so a signature from the origin cannot
    // verify past the first relay. Only 2.8.0/2.8.1 ever signed one.
    if (pkt.portnum == TRACEROUTE_APP) return {true, false};

    SigPolicyInput in = {};
    in.policy       = s_cfg.packetSigPolicy;
    in.bootstrap    = SIG_BOOTSTRAP_NA;
    in.pkiEncrypted = (pkt.chanIdx == -2);
    in.exempt       = pkt.portnum == ROUTING_APP || pkt.portnum == KEY_VERIFICATION_APP
                   || packetOnAeadChannel(pkt);

    NodeEntry *n = Nodes.find(pkt.hdr.from);
    const bool haveKey = n && n->hasPubKey;
    in.knownSigner = haveKey && n->xeddsaVerified;

    XeddsaSignedFields f = {};
    f.from         = pkt.hdr.from;
    f.id           = pkt.hdr.id;
    f.to           = pkt.hdr.to;
    f.portnum      = pkt.portnum;
    f.requestId    = pkt.requestId;
    f.replyId      = pkt.env.replyId;
    f.emoji        = pkt.env.emoji;
    f.bitfield     = pkt.env.bitfield;
    f.hasBitfield  = pkt.env.hasBitfield;
    f.wantResponse = pkt.wantResponse;

    if (pkt.env.badSignature) {
        in.check = SIG_CHECK_MALFORMED;
    } else if (!pkt.env.hasSignature) {
        in.check = SIG_CHECK_UNSIGNED;
    } else if (haveKey) {
        // Checked on every signed packet, not just until the node first proves
        // itself: a failure drops the packet, so every one has to be judged.
        in.check = xeddsaVerify(n->pubKey, f, pkt.payload, pkt.payloadLen, pkt.env.signature)
                       ? SIG_CHECK_VERIFIED : SIG_CHECK_FAILED;
        // A signed NodeInfo from a node we know has to advertise the key that
        // signed it, and that key has to hash to the sender's node number --
        // the same test first contact gets below. Otherwise the signature
        // vouches for a packet whose content claims another identity.
        if (in.check == SIG_CHECK_VERIFIED && pkt.portnum == NODEINFO_APP) {
            UserInfo u = {};
            if (decodeUser(pkt.payload, pkt.payloadLen, u) && u.hasPubKey
                && (memcmp(u.pubKey, n->pubKey, 32) != 0
                    || meshCrc32(u.pubKey, 32) != pkt.hdr.from)) {
                in.check = SIG_CHECK_FAILED;
            }
        }
        // A node that was reset or reflashed signs its NodeInfo with its new
        // key, which fails against the pinned one. The packet is still dropped
        // below; the key is only parked as a conflict for the user to judge,
        // noting that it came signed by itself (proof of holding it, not of
        // being who it says).
        if (in.check == SIG_CHECK_FAILED && pkt.portnum == NODEINFO_APP) {
            UserInfo u = {};
            if (decodeUser(pkt.payload, pkt.payloadLen, u) && u.hasPubKey
                && memcmp(u.pubKey, n->pubKey, 32) != 0
                && xeddsaVerify(u.pubKey, f, pkt.payload, pkt.payloadLen, pkt.env.signature)
                && Nodes.offerKey(pkt.hdr.from, u.pubKey, true)) {
                noteKeyConflictRaised(pkt.hdr.from);
            }
        }
    } else {
        in.check = SIG_CHECK_NO_KEY;
        // First contact. A signed NodeInfo carries the key to check it with,
        // and 2.8.1 trusts that key only if it hashes to the sender's ID.
        if (pkt.portnum == NODEINFO_APP) {
            UserInfo u = {};
            const bool ok = decodeUser(pkt.payload, pkt.payloadLen, u) && u.hasPubKey
                && meshCrc32(u.pubKey, 32) == pkt.hdr.from
                && xeddsaVerify(u.pubKey, f, pkt.payload, pkt.payloadLen, pkt.env.signature);
            in.bootstrap = ok ? SIG_BOOTSTRAP_VERIFIED : SIG_BOOTSTRAP_INVALID;
        }
    }

    const SigPolicyResult r = sigPolicyDecide(in);
    if (in.check == SIG_CHECK_VERIFIED || in.bootstrap == SIG_BOOTSTRAP_VERIFIED) {
        if (!in.knownSigner) {
            Serial.printf("[xeddsa] verified signature from !%08lx port=%lu\n",
                          (unsigned long)pkt.hdr.from, (unsigned long)pkt.portnum);
        }
    } else if (!r.accept) {
        // Worth a line each time: a failed signature means either the key we
        // hold is not theirs any more, or something is impersonating them.
        static const char *const kWhy[] = {
            "unsigned", "malformed signature", "", "signature FAILED", "unverifiable signature",
        };
        const char *why = (in.bootstrap == SIG_BOOTSTRAP_INVALID)
                              ? "invalid first-contact NodeInfo" : kWhy[in.check];
        Serial.printf("[xeddsa] drop %s from !%08lx port=%lu (policy %s)\n", why,
                      (unsigned long)pkt.hdr.from, (unsigned long)pkt.portnum,
                      sigPolicyName(s_cfg.packetSigPolicy));
    }
    return r;
}

static void closeNodesActionMenu();
static void refreshNodesActionMenuSelection();
static void executeNodesActionSelection();
static void openTracerouteProgressModal(uint32_t nodeId, uint32_t packetId);
static void closeTracerouteProgressModal();
static void onTracerouteBackdropPressed(lv_event_t *e);
static void tracerouteProgressSetStatus(const char *status, lv_color_t color);
static void tracerouteProgressSetTxResult(bool ok);
static void tracerouteProgressOnRouting(uint32_t fromNode, uint32_t requestId, uint32_t errorReason,
                                        const uint8_t *routeReplyPayload = nullptr,
                                        size_t routeReplyLen = 0,
                                        bool viaMqtt = false);
static void tracerouteProgressOnResponse(const MeshPacket &pkt);
static void tracerouteProgressRenderRoutesPayload(const uint8_t *payload, size_t payloadLen, bool viaMqtt);
static void tracerouteProgressRenderRoutes(const MeshPacket &pkt);
static bool sendTracerouteToNode(uint32_t toNodeId, uint32_t *packetIdOut = nullptr);
static bool nodesSnapshotContains(uint32_t nodeId);
static const NodeEntry *currentNodesSelection();
// Archived half of the Nodes list — see the combined row space note above.
static int nodesRowCountTotal();
static const ArchivedNode *nodesArchivedAt(int snapshotIdx);
static uint32_t nodesRowNodeId(int snapshotIdx);
static const ArchivedNode *currentNodesArchivedSelection();
static void nodesArchivedDetailInvalidate();
static void refreshNodesMap(const NodeEntry *node);
#if HAS_NODE_LOCATE
static void openNodeLocateModal(uint32_t nodeId);
static void openNodesMapModal();
static void closeNodeLocateModal();
static void nodeLocateOnDetailMapUpdated();
static void serviceNodeLocateLiveMap(uint32_t nowMs);
#endif
#if HAS_NODE_LOS || HAS_WEATHER || HAS_NODE_LOCATE
// GPS fix if there is one, else the configured/last-known position. Shared by
// LOS, the weather screen and the node map — all ask the same question, and a
// second copy would be a second answer waiting to drift.
static bool nodeLosSelfPosition(double &lat, double &lon);
#endif
#if HAS_NODE_LOS
static void closeNodeLosModal();
static void openNodeLosModal(uint32_t nodeId);
#endif
static void onWebCfgSaved();
static bool captureWebScreenshotPng(const char *outPath);
static bool pollMeshRx();
static void serviceSerialCommands();
static void handleSerialCommandLine(char *line);
static void normalizeSerialCommand(char *line, bool lower = true);
static void serviceNodeInfoAnnounce(uint32_t nowMs);
static void serviceTelemetryAnnounce(uint32_t nowMs);
static void serviceNeighborInfoAnnounce(uint32_t nowMs);
static void serviceMapReport(uint32_t nowMs);
static void serviceAutoFavorite(uint32_t nowMs);
static void applyTimezoneFromConfig();
static void setOtaWorkerBootNotice(const char *msg);
static void syncWifiCredsToPrefs();
static void persistConfigToPrefs();
// Queues a settings save. Config is one NVS blob now, so a per-keypress write
// would rewrite ~1 KB each time; this coalesces a burst of changes into one
// write and is flushed by serviceConfigFlush() (and before reboot/sleep).
static void markConfigDirty();
static void flushConfigIfDirty();
static void flushPersistentState();   // config + debounced transcripts, before reboot
// Installed at the end of setup(); defined beside the loop service it drives.
static void powerInstallHooks();
static void persistWebCfgEnabled();
#if HAS_VNC_HOST
static void persistVncEnabled();
#endif
static void persistWifiForceAp();
static void requestSkipWebAutoStartOnce();
static bool consumeSkipWebAutoStartOnce();
static bool requestOtaWorkerModeOnce();
static bool isOtaWorkerModeRequestedRtc();
static bool consumeOtaWorkerModeRtcOnce();
static bool isOtaWorkerModeRequestedOnce();
static bool consumeOtaWorkerModeOnce();
static bool runOtaWorkerModeIfRequested();
static bool nodesPanelCanDownloadTiles();
static void nodesPanelWifiEnter();
static void nodesPanelWifiRestore();
static void nodesMapInitFsDriver();
static bool nodesMapEnsureDir(const char *path);
static void nodesQueueDetailMapOnDemand(float lat, float lon, const char *detailKey,
                                        int detailLodTarget);
static void serviceDetailMapOnDemand(uint32_t nowMs);
static void bootstrapStateMapsIfMissing();
static int nodesMapRenderTiles(float lat, float lon, int x0, int y0, int w, int h,
                               int &markerX, int &markerY,
                               int detailLodTarget = 1);
static void sdRmDirRecursive(const char *path);
static bool clearNodeDbOnSd(bool includeArchive = true);
static bool clearNodeArchiveOnSd();

// camillia chat server client (defined with the mesh TX helpers). The Config
// screen, the Nodes action menu and the web config call these.
static bool chatServerCheckNow(const char **why);
static void chatServerSetManual(uint32_t nodeId);
static void chatServerClear();
static void chatServerModeChanged();
static void chatServerLabel(char *out, size_t outLen);
static void csLive(const char *text, uint16_t color);
static bool pagerSelectChatCursorIndex(int displayIndex);
static void pagerExitChatCursorMode(bool clearSelection = true);
static void setActiveChannel(int channelIdx);
static const char *channelName(int idx);
static void sizeChannelButtonToLabel(int idx);
static bool isChannelDropdownVisible();
static void setChannelDropdownVisible(bool visible);
static void onChannelSelectorPressed(lv_event_t *e);
static void refreshChannelSelectorLabel();
static void drawBootSplash();
// Boot-progress line on the splash. Defined with drawBootSplash(); declared here
// because the station-connect wait in bootTimeNtpSyncDirectSta() sits above it
// and pumps the tick so the dots keep moving through the wait.
static void bootSplashStatus(const char *msg);
static void bootSplashTick();
static void bootSplashStatusEnd();
static bool useCompactVerticalHeltecSelector();
static bool pollUserButton(uint32_t nowMs);
static bool tryWakeScreenFromInput(uint32_t nowMs);
#if HAS_WAKE_BUTTON
// The wake button's debounce and its gestures. Defined with the panel
// sleep/wake code far below; declared here because the Mesh Deck reads its
// button off an expander from a function above that.
static bool serviceWakeButtonDebounce(WakeButtonPoll &st, bool pressed,
                                      uint32_t nowMs);
static bool serviceWakeButton(WakeButtonPoll &st, bool pressed, uint32_t nowMs,
                              const char *reason);
#endif
#if defined(DEVICE_WIO_TRACKER_L2)
static bool serviceWioTrackerL2WakeButton(uint32_t nowMs);
#endif
#if HAS_NOTIFY_LED
// Defined further down with the expander code; called from triggerMessageAlert()
// above it.
static void meshDeckLedNotify(bool isDm);
#endif
#if HAS_KB_BLINK
// Same arrangement as meshDeckLedNotify above: defined further down with the
// keyboard-backlight code, called from triggerMessageAlert().
static void kbBlinkNotify(bool isDm);
#endif
static void wakeScreen();
static void openComposePromptForDm(uint32_t nodeId);
static void rebuildUiForThemeChange(bool reopenCfg);
static void scheduleUiRebuild(bool reopenCfg);
static void processPendingUiRebuild();
static void applyChannelButtonTheme();
static void startWebConfigAuto();
#if !UI_TOUCH_ONLY_PROFILE
// The footer's key-hint line. Defined with the bar it belongs to, far below;
// the home dashboard borrows that label and has to put the hints back.
static const char *chatShortcutHintText();
#endif

static void openOnboardingModal();
static void closeOnboardingModal();
// Set when the paused-chat notice is raised while onboarding owns the keyboard;
// closeOnboardingModal() shows it once there is something to dismiss it with.
static bool s_webCfgChatPausedNoticePending = false;
static void showWebCfgChatPausedNotice();
static void renderOnboardingStage();
static void onboardingAcceptImport();
static void onboardingDeclineImport();
static void onboardingCommitName();
static void onboardingFinalize();
static void onboardingPickerStep(int delta);
static void onboardingPickerAdvance();
static void onboardingPickerBack();
static void onboardingSetStatus(const char *msg);

static void stateMapBootstrapRestoreWifi() {
#if HAS_FILE_STORAGE
    if (!s_stateMapBootstrapWifiTouched) return;

    switch (s_stateMapBootstrapPrevMode) {
        case WIFI_OFF:
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            break;
        case WIFI_STA:
            WiFi.mode(WIFI_STA);
            WiFi.disconnect(false);
            break;
        case WIFI_AP:
            WiFi.disconnect(false);
            WiFi.mode(WIFI_AP);
            break;
#ifdef WIFI_AP_STA
        case WIFI_AP_STA:
            WiFi.mode(WIFI_AP_STA);
            WiFi.disconnect(false);
            break;
#endif
        default:
            break;
    }
    s_stateMapBootstrapWifiTouched = false;
#endif
}

#ifndef LV_SYMBOL_CLOSE
// Same defensive shape as LV_SYMBOL_GPS below: a plain X renders in montserrat
// on any build, where a missing symbol would draw a tofu box.
#define LV_SYMBOL_CLOSE "X"
#endif
#ifndef LV_SYMBOL_GPS
#define LV_SYMBOL_GPS LV_SYMBOL_DRIVE
#endif

#ifndef LV_SYMBOL_RADIO_TINY
#define LV_SYMBOL_RADIO_TINY LV_SYMBOL_VOLUME_MAX
#endif

#ifndef LV_SYMBOL_GLOBE_TINY
#define LV_SYMBOL_GLOBE_TINY LV_SYMBOL_WIFI
#endif

// The chat view's LoRa/MQTT mark, in front of every received message. One place
// that picks it, so Message Info (message actions) says it the same way.
static inline const char *msgTransportIcon(bool viaMqtt) {
    return viaMqtt ? LV_SYMBOL_GLOBE_TINY : LV_SYMBOL_RADIO_TINY;
}

#if UI_TOUCH_ONLY_PROFILE
// One X, in the top-right corner, for every screen and modal on the touch-only
// build that a keyboard build closes with Backspace.
//
// These used to be three different controls: a full-width "Close" at the foot
// of some modals, a 44-48px "Close" in the header bar of the full-screen ones,
// and a corner X on Channels — one action wearing three shapes, in three
// places. The corner X is the one that works everywhere. It costs no row in the
// layout, so nothing has to give up height for it; it never ends up below the
// fold of a scrolling list; and it lands in the same place on every screen, so
// the way out is somewhere you look rather than somewhere you hunt for.
//
// FLOATING rather than IGNORE_LAYOUT: LVGL skips floating children in the flex
// layout AND in the content-size and scroll calculations, and does not move
// them when the parent scrolls. That lets this be a child of the modal itself —
// deleted with it, no orphan left on the screen behind — while still staying
// pinned in the corner of a modal whose body scrolls (Device Info, Channels)
// and adding nothing to the height of one sized LV_SIZE_CONTENT.
//
// Returns the button so a caller can re-align it: the full-screen tools hang it
// off the right end of their header bar instead of the modal's own corner.
static constexpr int kHeltecCloseXSize = 26;

static lv_obj_t *appendHeltecCloseX(lv_obj_t *parent, lv_event_cb_t onClose,
                                    int size = kHeltecCloseXSize) {
    if (!parent) return nullptr;
    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) return nullptr;
    const bool light = (s_cfg.uiMode == UI_MODE_LIGHT);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(btn, size, size);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, light ? lv_color_hex(0xE6ECF5) : lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(btn, light ? LV_OPA_COVER : LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, light ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x8FB5E6), 0);
    lv_obj_add_event_cb(btn, onClose, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    if (lbl) {
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, light ? lv_color_hex(0x13233D)
                                               : lv_color_hex(0xE8F1FF), 0);
        lv_label_set_text(lbl, LV_SYMBOL_CLOSE);
        lv_obj_center(lbl);
    }
    // Above whatever the modal draws under it: a title that runs the full width
    // of the panel, a chart, a list row.
    lv_obj_move_foreground(btn);
    return btn;
}

// Keeps a full-width title clear of the corner X. A centred LV_LABEL_LONG_DOT
// title is laid out in the label's content box, so padding the right edge is
// what moves the ellipsis in from under the button. For a title that is already
// taller than the button — a two-line one — this is all that is needed.
static void reserveHeltecCloseXGap(lv_obj_t *title, int size = kHeltecCloseXSize) {
    if (title) lv_obj_set_style_pad_right(title, size + 4, 0);
}

// The same, for the usual single-line title: it also grows the title row to the
// height of the button. The X floats, so nothing else in the modal reserves
// space for it — without this it would hang below the title and cover the top
// of whatever comes next (the first list row, the corner of a map or a chart,
// the first line of a body of text), which is both ugly and, since the button
// is in front, a tap target sitting on top of another one.
//
// The text is centred against the button rather than left at the top of the
// taller row, so the two read as one title bar.
static void reserveHeltecCloseXRow(lv_obj_t *title, int size = kHeltecCloseXSize) {
    if (!title) return;
    const lv_font_t *font = lv_obj_get_style_text_font(title, LV_PART_MAIN);
    const int lineH = font ? (int)lv_font_get_line_height(font) : 16;
    int padTop = (size - lineH) / 2;
    if (padTop < 0) padTop = 0;
    lv_obj_set_height(title, size);
    lv_obj_set_style_pad_top(title, padTop, 0);
    reserveHeltecCloseXGap(title, size);
}

#if HAS_TOOL_STACK_LAYOUT
// The portrait screen title, first worked out on the P4's Tools: montserrat_18,
// centred on the screen, on the line of a full-size corner X aligned
// UI_CORNER_SAFE_X down from the top -- the baseline of the word on the X's
// bottom edge -- with a gap under it before whatever comes next. Screens that
// use it put the title straight in their column, with no header bar, so every
// one of them reads the same.
//
// `reserveX` keeps the text clear of an X on that line, on both sides so it
// stays on the centre line. Without one (the screens Tools opens, whose way
// out is the nav bar) the title has the full width, and sits at the same
// height, so moving between them nothing jumps.
//
// The Heltec's 240x320 upright has a third of the P4's height, so it takes a
// size smaller and half the gap.
#if defined(DEVICE_TDISPLAY_P4)
static constexpr int kStackTitleGap = 12;
#define STACK_TITLE_FONT (&lv_font_montserrat_18)
#else
static constexpr int kStackTitleGap = 6;
#define STACK_TITLE_FONT (&lv_font_montserrat_16)
#endif

static void stackTitleOnXLine(lv_obj_t *title, bool reserveX = true) {
    if (!title) return;
    const lv_font_t *font = STACK_TITLE_FONT;
    lv_obj_set_style_text_font(title, font, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    const int sidePad = reserveX ? kHeltecCloseXSize + 4 : 0;
    lv_obj_set_style_pad_left(title, sidePad, 0);
    lv_obj_set_style_pad_right(title, sidePad, 0);
    const int xBottom = UI_CORNER_SAFE_X + kHeltecCloseXSize;
    const int ascent = (int)lv_font_get_line_height(font) - font->base_line;
    lv_obj_set_style_pad_top(title, (xBottom > ascent) ? xBottom - ascent : 0, 0);
    lv_obj_set_style_pad_bottom(title, 0, 0);
    lv_obj_set_height(title, xBottom + font->base_line + kStackTitleGap);
}
#endif
#endif  // UI_TOUCH_ONLY_PROFILE

static size_t decodeUtf8Codepoint(const char *src, size_t avail, uint32_t &cp) {
    cp = 0;
    if (!src || avail == 0) return 0;

    const uint8_t b0 = (uint8_t)src[0];
    if (b0 < 0x80) {
        cp = b0;
        return 1;
    }

    if ((b0 & 0xE0) == 0xC0) {
        if (avail < 2) return 0;
        const uint8_t b1 = (uint8_t)src[1];
        if ((b1 & 0xC0) != 0x80) return 0;
        cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        return 2;
    }

    if ((b0 & 0xF0) == 0xE0) {
        if (avail < 3) return 0;
        const uint8_t b1 = (uint8_t)src[1];
        const uint8_t b2 = (uint8_t)src[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
        cp = ((uint32_t)(b0 & 0x0F) << 12)
           | ((uint32_t)(b1 & 0x3F) << 6)
           | (uint32_t)(b2 & 0x3F);
        return 3;
    }

    if ((b0 & 0xF8) == 0xF0) {
        if (avail < 4) return 0;
        const uint8_t b1 = (uint8_t)src[1];
        const uint8_t b2 = (uint8_t)src[2];
        const uint8_t b3 = (uint8_t)src[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
        cp = ((uint32_t)(b0 & 0x07) << 18)
           | ((uint32_t)(b1 & 0x3F) << 12)
           | ((uint32_t)(b2 & 0x3F) << 6)
           | (uint32_t)(b3 & 0x3F);
        return 4;
    }

    return 0;
}

static bool isEmojiCodepoint(uint32_t cp) {
    if (cp >= 0x1F300 && cp <= 0x1FAFF) return true;
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return true; // flag letters
    return false;
}

static bool isEmojiJoinerOrModifier(uint32_t cp) {
    if (cp == 0x200D || cp == 0xFE0F || cp == 0x20E3) return true; // ZWJ, VS16, keycap
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true; // skin tones
    return false;
}

static const char *emojiAliasForCodepoint(uint32_t cp) {
    switch (cp) {
        case 0x1F600: case 0x1F601: case 0x1F602: case 0x1F603:
        case 0x1F604: case 0x1F606: case 0x1F60A: case 0x1F642:
        case 0x263A:
            return ":)";
        case 0x1F614: case 0x1F622: case 0x1F62D:
            return ":(";
        case 0x1F44D:
            return "[+]";
        case 0x1F44E:
            return "[-]";
        case 0x2764:
            return "<3";
        case 0x1F525:
            return "[hot]";
        case 0x1F389:
            return "[party]";
        case 0x2705:
            return "[ok]";
        case 0x274C:
            return "[x]";
        case 0x26A0:
            return "[!]";
        case 0x1F64F:
            return "[pray]";
        case 0x1F914:
            return "[?]";
        case 0x1F440:
            return "[eyes]";
        case 0x1F680:
            return "[go]";
        case 0x1F4CD:
            return "[pin]";
        default:
            return nullptr;
    }
}

static size_t appendTextLiteral(char *dst, size_t dstLen, size_t writePos, const char *lit) {
    if (!dst || dstLen == 0 || !lit) return writePos;
    while (*lit && writePos + 1 < dstLen) {
        dst[writePos++] = *lit++;
    }
    dst[writePos] = '\0';
    return writePos;
}

// Codepoints that carry ordinary punctuation meaning but sit outside what the
// Montserrat faces can draw: ASCII, Latin-1, Latin Extended-A, bullet and the
// euro sign (src/fonts/latin/, issue #99 -- accented letters render as they
// are and need no folding). Curly quotes, dashes and the ellipsis are not in it.
// The emoji fallback face is no help — it holds emoji, not Latin punctuation —
// so anything left in this range reaches LVGL's missing-glyph box. Phone
// keyboards and desktop autocorrect emit these constantly: an iOS "..." becomes
// a single U+2026, and quotes are curled on the way out.
//
// An empty replacement drops the codepoint, which is what the zero-width
// formatting marks deserve — they have no visible width to preserve, and a box
// is the worst possible rendering of a character meant to be invisible.
struct AsciiFold {
    uint32_t cp;
    const char *ascii;
};
static const AsciiFold kAsciiFolds[] = {
    // Apostrophes / single quotes / prime
    {0x2018, "'"}, {0x2019, "'"}, {0x02BC, "'"}, {0xFF07, "'"}, {0x2032, "'"},
    {0x201A, "'"}, {0x201B, "'"},
    // Double quotes / double prime
    {0x201C, "\""}, {0x201D, "\""}, {0x201E, "\""}, {0x201F, "\""},
    {0x2033, "\""}, {0xFF02, "\""},
    // Em dash and horizontal bar read as a break, so they get two hyphens.
    {0x2014, "--"}, {0x2015, "--"},
    // Every other dash-like mark is a single hyphen.
    {0x2010, "-"}, {0x2011, "-"}, {0x2012, "-"}, {0x2013, "-"},
    {0x2212, "-"}, {0xFE58, "-"}, {0xFE63, "-"}, {0xFF0D, "-"},
    // Ellipsis — one codepoint, three dots.
    {0x2026, "..."},
    // Spaces that are not U+0020.
    {0x00A0, " "}, {0x2002, " "}, {0x2003, " "}, {0x2007, " "}, {0x2009, " "},
    {0x202F, " "}, {0x3000, " "},
    // Zero-width marks: drop entirely.
    {0x200B, ""}, {0x200C, ""}, {0x2060, ""}, {0xFEFF, ""},
};

static const char *asciiFoldFor(uint32_t cp) {
    for (const AsciiFold &f : kAsciiFolds) {
        if (f.cp == cp) return f.ascii;
    }
    return nullptr;
}

static void renderEmojiSafeText(const char *src, char *dst, size_t dstLen) {
    if (!dst || dstLen == 0) return;
    dst[0] = '\0';
    if (!src) return;

    size_t writePos = 0;
    size_t i = 0;
    size_t srcLen = strlen(src);
    while (i < srcLen && writePos + 1 < dstLen) {
        uint32_t cp = 0;
        size_t n = decodeUtf8Codepoint(src + i, srcLen - i, cp);
        if (n == 0) {
            dst[writePos++] = src[i++];
            dst[writePos] = '\0';
            continue;
        }

        if (isEmojiJoinerOrModifier(cp)) {
            i += n;
            continue;
        }

        // Normalize punctuation the text face cannot draw. One table for quotes,
        // dashes, the ellipsis and exotic spaces — see kAsciiFolds.
        if (const char *fold = asciiFoldFor(cp)) {
            writePos = appendTextLiteral(dst, dstLen, writePos, fold);
            i += n;
            continue;
        }

        // Collapse regional-indicator pairs (flags) into a readable country code.
        if (cp >= 0x1F1E6 && cp <= 0x1F1FF) {
            uint32_t cp2 = 0;
            size_t n2 = decodeUtf8Codepoint(src + i + n, srcLen - (i + n), cp2);
            if (n2 > 0 && cp2 >= 0x1F1E6 && cp2 <= 0x1F1FF) {
                char cc[5];
                cc[0] = '[';
                cc[1] = (char)('A' + (int)(cp - 0x1F1E6));
                cc[2] = (char)('A' + (int)(cp2 - 0x1F1E6));
                cc[3] = ']';
                cc[4] = '\0';
                writePos = appendTextLiteral(dst, dstLen, writePos, cc);
                i += n + n2;
                continue;
            }
        }

#if !LV_USE_TINY_TTF
        // No emoji font compiled in: fall back to ASCII stand-ins for the
        // handful we have aliases for. With the emoji font present (the default)
        // this is skipped and the codepoint passes through to be drawn.
        const char *alias = emojiAliasForCodepoint(cp);
        if (alias) {
            writePos = appendTextLiteral(dst, dstLen, writePos, alias);
            i += n;
            continue;
        }
#endif

        // Pass the codepoint through untouched. The chat/DM/name/preview labels
        // draw with an emoji-enabled face (see emojiFont / scaledChatFont), so
        // any emoji in the font renders inline; the rest show LVGL's fallback
        // box, same as before.
        if (writePos + n >= dstLen) break;
        memcpy(dst + writePos, src + i, n);
        writePos += n;
        dst[writePos] = '\0';
        i += n;
    }
}

static void setLabelTextEmojiSafe(lv_obj_t *label, const char *text) {
    if (!label || !lv_obj_is_valid(label)) return;
    static char safeBuf[768];
    renderEmojiSafeText(text, safeBuf, sizeof(safeBuf));
    lv_label_set_text(label, safeBuf);
}

// ── Table-driven Config rows ────────────────────────────────────────────────
// Settings that are a plain value on s_cfg -- an on/off, a choice from a list,
// or a line of text -- and need nothing bespoke to edit. Each is one entry in
// kCfgGenRows (see cfgGenRow()), which says how to read, change and apply it;
// the row's label, its section, the editor it opens and what happens after a
// change are all driven from there. They sit in CfgActionId as one contiguous
// run, CFG_ACTION_GEN_FIRST + id, so the list, the filter and the section grid
// treat them exactly like every other row.
//
// Every id exists on every board, because the RhinoConfig fields behind them
// do; whether a board lists a row is decided in initCfgActions(), the same as
// for the hand-written rows.
enum CfgGenId : int {
    // Radio & Identity
    CFG_GEN_REGION = 0,
    CFG_GEN_TX_POWER,
    CFG_GEN_HOP_LIMIT,
    CFG_GEN_ROLE,
    CFG_GEN_CUSTOM_MODEM,
    CFG_GEN_CUSTOM_BW,
    CFG_GEN_CUSTOM_SF,
    CFG_GEN_CUSTOM_CR,
    CFG_GEN_CUSTOM_SLOT,
    CFG_GEN_RX_BOOST,
    CFG_GEN_NODEID_PUBKEY,
    CFG_GEN_NODEID_OVERRIDE,
    // Connections
    CFG_GEN_WEB_AUTH,
    CFG_GEN_WEB_PASS,
    CFG_GEN_WEB_IDLE,
    CFG_GEN_MQTT_SERVER,
    CFG_GEN_MQTT_PORT,
    CFG_GEN_MQTT_USER,
    CFG_GEN_MQTT_PASS,
    CFG_GEN_MQTT_ROOT,
    CFG_GEN_MQTT_ENCRYPT,
    CFG_GEN_MQTT_MAP_REPORT,
    CFG_GEN_OK_TO_MQTT,
    CFG_GEN_IGNORE_MQTT,
    // Location
    CFG_GEN_POS_INTERVAL,
    CFG_GEN_GPS_POLL,
    CFG_GEN_GPS_DUTY,
    CFG_GEN_FIXED_LAT,
    CFG_GEN_FIXED_LON,
    CFG_GEN_FIXED_ALT,
    // Mesh Services
    CFG_GEN_REBROADCAST,
    CFG_GEN_NODEINFO_INTERVAL,
    CFG_GEN_TEL_DEVICE,
    CFG_GEN_TEL_ENV,
    CFG_GEN_TEL_INTERVAL,
    CFG_GEN_NEIGHBOR_INTERVAL,
    CFG_GEN_NEIGHBOR_LORA,
    CFG_GEN_AUTOFAV,
    CFG_GEN_AUTOFAV_RANGE,
    CFG_GEN_LOS_SERVER,
    CFG_GEN_WEATHER_SERVER,
    // Device & Firmware
    CFG_GEN_TIMEZONE,
    CFG_GEN_NTP_SERVER,
    CFG_GEN_POWER_SAVING,
    CFG_GEN_OTA_BOOT_CHECK,
    CFG_GEN_OTA_AUTO_UPDATE,
    CFG_GEN_DEBUG_LOG,
    CFG_GEN__COUNT,
};

enum CfgActionId {
    CFG_ACTION_WEBCFG = 0,
    CFG_ACTION_NODE_NAME,
    CFG_ACTION_PRESET,
    CFG_ACTION_COMMUNITY_PRESET,
#if FEATURE_LICENSED_MODE
    CFG_ACTION_LICENSED_MODE,  // amateur call sign; strips keys, reboots
#endif
#if defined(DEVICE_TDISPLAY_P4)
    CFG_ACTION_ANTENNA,        // on-board vs external antenna socket
#endif
#if HAS_VNC_HOST
    CFG_ACTION_VNC_HOST,
#endif
#if HAS_WEB_FILES
    CFG_ACTION_WEB_FILES,      // the web config Files tab
#endif
    CFG_ACTION_WIFI_TOGGLE,
    CFG_ACTION_CHOOSE_WIFI,
#if HAS_BLE_KEYBOARD
    CFG_ACTION_BLE_KBD_TOGGLE,
    CFG_ACTION_BLE_KBD_PAIR,
#endif
    CFG_ACTION_GPS_TOGGLE,
#if HAS_FILE_STORAGE
    CFG_ACTION_WARDRIVE,
#endif
    CFG_ACTION_SHARE_LOCATION,
    CFG_ACTION_POSITION_PRECISION,
    CFG_ACTION_SIG_POLICY,     // which signed/unsigned packets to accept
    CFG_ACTION_EXPORT,
    CFG_ACTION_IMPORT,
    CFG_ACTION_THEME,
    CFG_ACTION_OWNER_COLOR,
    CFG_ACTION_UNITS,
    CFG_ACTION_CHAT_STYLE,
    CFG_ACTION_CHAT_NAMES,
    CFG_ACTION_CHAT_COLORS,
    CFG_ACTION_FONT_SIZE,
    CFG_ACTION_ORIENTATION,
    CFG_ACTION_LANGUAGE,       // UI language (issue #99)
    #if HAS_SPELLCHECK
    CFG_ACTION_SPELL_CHECK,
    #endif
    CFG_ACTION_BRIGHTNESS,
    CFG_ACTION_SCREEN_TIMEOUT,
    #if FEATURE_LOCK_SCREEN
    CFG_ACTION_LOCK_SCREEN,
    CFG_ACTION_LOCK_SCREEN_OFF,
    #endif
    CFG_ACTION_BATT_DISPLAY,
    #if HAS_SCROLL_INVERT
    CFG_ACTION_INVERT_SCROLL,
    #endif
    #if HAS_NAV_BAR_TOGGLE
    CFG_ACTION_NAV_BAR,
    #endif
    #if HAS_CHANNEL_PANEL_LOCK
    CFG_ACTION_CHANNEL_PANEL_LOCK,
    #endif
    #if HAS_NAV_ALIGN_SETTING
    CFG_ACTION_NAV_ALIGN,
    #endif
    CFG_ACTION_BATT_CAL,
    CFG_ACTION_NEIGHBOR_INFO,
    CFG_ACTION_MESH_BEACON,
    CFG_ACTION_CS_MODE,
    CFG_ACTION_CS_SERVER,
    CFG_ACTION_CS_CHECK,
    CFG_ACTION_MQTT_TOGGLE,
    #if HAS_VOLUME_CONTROL
    CFG_ACTION_VOLUME,
    #endif
    CFG_ACTION_MSG_ALERT,
    #if HAS_HAPTIC
    CFG_ACTION_VIBRATION,
    #endif
    #if HAS_NOTIFY_LED
    CFG_ACTION_NOTIFY_LED_CHANNEL_COLOR,
    CFG_ACTION_NOTIFY_LED_DM_COLOR,
    #endif
    #if HAS_KB_BLINK
    CFG_ACTION_KB_BLINK,
    CFG_ACTION_KB_BLINK_CHAN_FLASHES,
    CFG_ACTION_KB_BLINK_DM_FLASHES,
    #endif
    #if HAS_LIGHT_NOTIFY
    CFG_ACTION_NOTIFY_LIGHT_TIMEOUT,
    #endif
    #if HAS_KB_BACKLIGHT_LEVEL
    CFG_ACTION_KB_BACKLIGHT_LEVEL,
    #endif
    #if HAS_KB_LIGHT_TIMEOUT
    CFG_ACTION_KB_LIGHT_TIMEOUT,
    #endif
    #if HAS_POWER_LED_LEVEL
    CFG_ACTION_POWER_LED_LEVEL,
    #endif
    CFG_ACTION_SPLASH_MELODY,
    CFG_ACTION_OTA_UPDATE,
    CFG_ACTION_OTA_CHANNEL,
    CFG_ACTION_RELEASE_NOTES,
    CFG_ACTION_TIME_DATE,
    CFG_ACTION_CLOCK_FORMAT,
    CFG_ACTION_CHANNEL_CFG,
    CFG_ACTION_RESET_CHAT_COLORS,
    CFG_ACTION_CLEAR_MSGS,
    CFG_ACTION_ARCHIVE_NODES,
    CFG_ACTION_SHOW_ARCHIVED,
    CFG_ACTION_CLEAR_NODES,
    CFG_ACTION_FACTORY_RESET,
#if HAS_SD_MALWARE_SCAN
    CFG_ACTION_SD_SCAN,
#endif
    // The table-driven rows, one per CfgGenId. Keep these last but one.
    CFG_ACTION_GEN_FIRST,
    CFG_ACTION_GEN_END = CFG_ACTION_GEN_FIRST + CFG_GEN__COUNT,
    // Sizes s_cfgActions. Not an action — keep it last.
    CFG_ACTION__COUNT = CFG_ACTION_GEN_END,
};

static constexpr bool cfgGenIsAction(int actionId) {
    return actionId >= CFG_ACTION_GEN_FIRST && actionId < CFG_ACTION_GEN_END;
}
// Defined with the table, further down.
static const char *cfgGenLabel(int genId, char *buf, size_t bufLen);
static int  cfgGenSection(int genId);
static bool cfgGenDisabled(int genId, const char **why);
static bool cfgGenNeedsConfirm(int genId);
static void cfgGenConfirmSentence(int genId, char *buf, size_t bufLen);
static void cfgGenPerform(int genId);

// Sized by the enum rather than by a hand-picked number. initCfgActions()
// appends without a bounds check, and every previous size here was headroom
// guessed against a count that kept growing — the comment this replaces was
// already stale by fourteen entries.
//
// The enum is a genuine upper bound, not a generous one: each action is
// appended at most once, and most are behind a board #if, so no build can
// reach even this. Adding an action now grows the array with it, which is the
// property the old number could not have.
static int s_cfgActions[CFG_ACTION__COUNT] = {};

#if UI_CFG_SECTIONS
// ── Config sections ─────────────────────────────────────────────────────────
// Config opens on a grid of these, two or three to a row, and a tap or Enter
// lists one section's rows. The sections follow the runs initCfgActions() already lays the list out
// in, so the order inside each is the order the full list has. A section with
// no rows on this board is left off the grid.
enum CfgSectionId : int8_t {
    CFG_SEC_RADIO = 0,
    CFG_SEC_CONNECT,
    CFG_SEC_LOCATION,
    CFG_SEC_CHANNELS,
    CFG_SEC_DISPLAY,
    CFG_SEC_CHAT,
    CFG_SEC_SOUND,
    CFG_SEC_MESH,
    CFG_SEC_DEVICE,
    CFG_SEC_DATA,
    CFG_SEC__COUNT,
};

struct CfgSectionInfo {
    const char *icon;
    const char *name;   // English key; translated where it is drawn
};

static const CfgSectionInfo kCfgSections[CFG_SEC__COUNT] = {
    { LV_SYMBOL_HOME,      TR_NOOP("Radio & Identity") },
    { LV_SYMBOL_WIFI,      TR_NOOP("Connections") },
    { LV_SYMBOL_GPS,       TR_NOOP("Location") },
    { LV_SYMBOL_EYE_CLOSE, TR_NOOP("Channels & Security") },
    { LV_SYMBOL_IMAGE,     TR_NOOP("Display") },
    { LV_SYMBOL_ENVELOPE,  TR_NOOP("Chat") },
    { LV_SYMBOL_BELL,      TR_NOOP("Sound & Alerts") },
    { LV_SYMBOL_LOOP,      TR_NOOP("Mesh Services") },
    { LV_SYMBOL_SETTINGS,  TR_NOOP("Device & Firmware") },
    { LV_SYMBOL_TRASH,     TR_NOOP("Data & Reset") },
};

// The open section, or -1 for the grid. Kept while Config is up, so a picker
// that closes lands back in the section it was opened from; openCfgModal()
// starts at the grid again.
static int s_cfgSection = -1;
// The grid's sections present on this board, in order, and the one the
// keyboard highlight is on.
static int8_t s_cfgGridSections[CFG_SEC__COUNT] = {};
static int s_cfgGridCount = 0;
static int s_cfgGridSelection = 0;
// Buttons per row, as buildCfgGrid() last laid them out.
static int s_cfgGridCols = 2;
// "‹ Back" in the secondary header: shown inside a section, hidden on the grid.
static lv_obj_t *s_cfgBackBtn = nullptr;
// "Info" beside it on touch-only builds: the other way round, on the grid only.
// It is about the device as a whole, not about the section's rows.
static lv_obj_t *s_cfgInfoBtn = nullptr;

// Which section a row is listed under. The cases carry the same #if guards as
// CfgActionId, since an action a build leaves out has no enumerator to name.
static int cfgActionSection(int actionId) {
    if (cfgGenIsAction(actionId)) return cfgGenSection(actionId - CFG_ACTION_GEN_FIRST);
    switch (actionId) {
        case CFG_ACTION_NODE_NAME:
        case CFG_ACTION_PRESET:
        case CFG_ACTION_COMMUNITY_PRESET:
#if FEATURE_LICENSED_MODE
        case CFG_ACTION_LICENSED_MODE:
#endif
#if defined(DEVICE_TDISPLAY_P4)
        case CFG_ACTION_ANTENNA:
#endif
            return CFG_SEC_RADIO;

        case CFG_ACTION_WIFI_TOGGLE:
        case CFG_ACTION_CHOOSE_WIFI:
        case CFG_ACTION_WEBCFG:
#if HAS_VNC_HOST
        case CFG_ACTION_VNC_HOST:
#endif
#if HAS_WEB_FILES
        case CFG_ACTION_WEB_FILES:
#endif
#if HAS_BLE_KEYBOARD
        case CFG_ACTION_BLE_KBD_TOGGLE:
        case CFG_ACTION_BLE_KBD_PAIR:
#endif
        case CFG_ACTION_MQTT_TOGGLE:
            return CFG_SEC_CONNECT;

        case CFG_ACTION_GPS_TOGGLE:
#if HAS_FILE_STORAGE
        case CFG_ACTION_WARDRIVE:
#endif
        case CFG_ACTION_SHARE_LOCATION:
        case CFG_ACTION_POSITION_PRECISION:
            return CFG_SEC_LOCATION;

        case CFG_ACTION_CHANNEL_CFG:
        case CFG_ACTION_SIG_POLICY:
            return CFG_SEC_CHANNELS;

        case CFG_ACTION_THEME:
        case CFG_ACTION_FONT_SIZE:
        case CFG_ACTION_BRIGHTNESS:
#if HAS_KB_BACKLIGHT_LEVEL
        case CFG_ACTION_KB_BACKLIGHT_LEVEL:
#endif
#if HAS_KB_LIGHT_TIMEOUT
        case CFG_ACTION_KB_LIGHT_TIMEOUT:
#endif
#if HAS_POWER_LED_LEVEL
        case CFG_ACTION_POWER_LED_LEVEL:
#endif
#if HAS_NAV_BAR_TOGGLE
        case CFG_ACTION_NAV_BAR:
#endif
#if HAS_CHANNEL_PANEL_LOCK
        case CFG_ACTION_CHANNEL_PANEL_LOCK:
#endif
#if HAS_NAV_ALIGN_SETTING
        case CFG_ACTION_NAV_ALIGN:
#endif
        case CFG_ACTION_SCREEN_TIMEOUT:
#if FEATURE_LOCK_SCREEN
        case CFG_ACTION_LOCK_SCREEN:
        case CFG_ACTION_LOCK_SCREEN_OFF:
#endif
        case CFG_ACTION_ORIENTATION:
        case CFG_ACTION_LANGUAGE:
#if HAS_SPELLCHECK
        case CFG_ACTION_SPELL_CHECK:
#endif
#if HAS_SCROLL_INVERT
        case CFG_ACTION_INVERT_SCROLL:
#endif
            return CFG_SEC_DISPLAY;

        case CFG_ACTION_CHAT_STYLE:
        case CFG_ACTION_CHAT_NAMES:
        case CFG_ACTION_CHAT_COLORS:
        case CFG_ACTION_OWNER_COLOR:
        case CFG_ACTION_RESET_CHAT_COLORS:
            return CFG_SEC_CHAT;

#if HAS_VOLUME_CONTROL
        case CFG_ACTION_VOLUME:
#endif
        case CFG_ACTION_SPLASH_MELODY:
        case CFG_ACTION_MSG_ALERT:
#if HAS_HAPTIC
        case CFG_ACTION_VIBRATION:
#endif
#if HAS_NOTIFY_LED
        case CFG_ACTION_NOTIFY_LED_CHANNEL_COLOR:
        case CFG_ACTION_NOTIFY_LED_DM_COLOR:
#endif
#if HAS_KB_BLINK
        case CFG_ACTION_KB_BLINK:
        case CFG_ACTION_KB_BLINK_CHAN_FLASHES:
        case CFG_ACTION_KB_BLINK_DM_FLASHES:
#endif
#if HAS_LIGHT_NOTIFY
        case CFG_ACTION_NOTIFY_LIGHT_TIMEOUT:
#endif
            return CFG_SEC_SOUND;

        case CFG_ACTION_NEIGHBOR_INFO:
        case CFG_ACTION_MESH_BEACON:
        case CFG_ACTION_CS_MODE:
        case CFG_ACTION_CS_SERVER:
        case CFG_ACTION_CS_CHECK:
            return CFG_SEC_MESH;

        case CFG_ACTION_CLEAR_MSGS:
        case CFG_ACTION_ARCHIVE_NODES:
        case CFG_ACTION_SHOW_ARCHIVED:
        case CFG_ACTION_CLEAR_NODES:
        case CFG_ACTION_EXPORT:
        case CFG_ACTION_IMPORT:
        case CFG_ACTION_FACTORY_RESET:
#if HAS_SD_MALWARE_SCAN
        case CFG_ACTION_SD_SCAN:
#endif
            return CFG_SEC_DATA;

        // Time and date, clock format, units, battery display and calibration,
        // and the firmware rows. Also where an action added without a case
        // lands, so it shows up somewhere rather than nowhere.
        default:
            return CFG_SEC_DEVICE;
    }
}

// The grid is up: no section open, and no filter armed (a filter searches every
// row, so it shows them as one list whatever section is open).
static inline bool cfgGridShown() {
    return s_cfgSection < 0 && !s_cfgFilterOpen;
}
static void refreshCfgGridSelection();
static void cfgOpenSection(int sec);
static void cfgShowGrid();
#endif

struct UiThemePresetLite {
    uint8_t theme;
    uint8_t mode;
    uint16_t bgMain;
    uint16_t panelBg;
    uint16_t panelAlt;
    uint16_t accent;
    const char *name;
};

