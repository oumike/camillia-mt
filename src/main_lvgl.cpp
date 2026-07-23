#if defined(UI_LVGL_POC)

#include <Arduino.h>
#include "config.h"
#include "channel_mgr.h"
#include "config_io.h"
#include "hal/display.h"
#include "hal/xl9555.h"
#include "live_util.h"
#include "live_feed.h"
#include "mesh_proto.h"
#include "mesh_radio.h"
#include "mqtt_bridge.h"
#include "node_db.h"
#include "dm_mgr.h"
#include "ignore_list.h"
#include "battery_util.h"
#include "emoji_font.h"
#include "env_sensor.h"
#include "gps.h"
#include "keyboard.h"
#include "web_config.h"
#include "ota_update.h"
#include "debug_flags.h"
#include "utf8_utils.h"
#include "fonts/roboto_splash_fonts.h"
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <SD.h>
#include <Curve25519.h>
#if defined(DEVICE_TLORA_PAGER_TFT)
#include <AudioBoard.h>
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
#include <driver/i2s.h>
#endif
#include <esp_sleep.h>
#include <driver/gpio.h>
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#ifdef KEY_TAB
#undef KEY_TAB
#endif
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#ifdef KEY_ESCAPE
#undef KEY_ESCAPE
#endif
#include <M5Cardputer.h>

#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#ifdef KEY_TAB
#undef KEY_TAB
#endif
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#ifdef KEY_ESCAPE
#undef KEY_ESCAPE
#endif
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_ESCAPE      0x1B
#endif

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static LGFX_TDeck lcd;
static TDeckKeyboard s_keyboard;
static RhinoConfig s_cfg;

static lgfx::LGFX_Device &displayDev() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return M5Cardputer.Display;
#else
    return lcd;
#endif
}

uint8_t myPubKey[32] = {0};
uint8_t myPrivKey[32] = {0};
uint8_t myDeviceRole = 0;

static constexpr uint16_t kMaxHorRes =
    (DEVICE_LCD_LANDSCAPE_W > DEVICE_LCD_PORTRAIT_W) ? DEVICE_LCD_LANDSCAPE_W : DEVICE_LCD_PORTRAIT_W;
#if defined(DEVICE_TLORA_PAGER_TFT)
// Larger draw buffer -> fewer blocking SPI flushes per redraw (222px / 32 ~= 7
// stripes instead of ~19), which removes the visible top-to-bottom "painting"
// on modal opens. It costs ~19KB more internal RAM, but the buffer is now
// allocated dynamically on the normal-UI path only (see setup): OTA worker mode
// runs and reboots before that allocation, so during the TLS handshake this
// buffer occupies zero bytes and OTA actually gets MORE contiguous internal
// heap than the old static 12-line buffer left it.
static constexpr uint16_t kDrawBufLines = 32;
#else
static constexpr uint16_t kDrawBufLines = 40;
#endif
// Allocated at UI init (heap_caps, internal RAM) rather than a static array so
// the OTA worker path can complete without it ever being reserved.
static lv_color_t *s_drawBufMem = nullptr;
static lv_disp_draw_buf_t s_drawBuf;
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
static lv_obj_t *s_channelStrip = nullptr;
static lv_obj_t *s_channelList = nullptr;
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
static lv_obj_t *s_chatHeaderBattText = nullptr;
static lv_obj_t *s_chatHeaderBattBar = nullptr;
static lv_obj_t *s_chatPanel = nullptr;
static lv_obj_t *s_chatList = nullptr;
static lv_obj_t *s_chatNewMsgBtn = nullptr;
static lv_obj_t *s_chatNewMsgLabel = nullptr;
static lv_obj_t *s_chatShortcutBar = nullptr;
static lv_obj_t *s_chatShortcutText = nullptr;
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
static lv_obj_t *s_composeCharCount = nullptr;
static lv_obj_t *s_cfgModal = nullptr;
static lv_obj_t *s_cfgActionList = nullptr;
static lv_obj_t *s_cfgInfoList = nullptr;
static lv_obj_t *s_cfgHeaderStatus = nullptr;
// WiFi picker modal layered over CFG for selecting preferred known network.
static lv_obj_t *s_cfgWifiBackdrop = nullptr;
static lv_obj_t *s_cfgWifiModal = nullptr;
static lv_obj_t *s_cfgWifiList = nullptr;
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
// Yes/No confirmation dialog layered over the CFG modal for destructive actions.
static lv_obj_t *s_cfgConfirmBackdrop = nullptr;
static lv_obj_t *s_cfgConfirmModal = nullptr;
static lv_obj_t *s_otaPromptBackdrop = nullptr;
static lv_obj_t *s_otaPromptModal = nullptr;
static bool s_otaAutoCheckDone = false;     // one check attempt per boot
// Settle delay after the station associates, before the release check fires.
static constexpr uint32_t kOtaAutoCheckSettleMs = 8000;
static char s_otaAutoCheckTag[48] = {};     // release tag the prompt is offering
static int s_cfgConfirmPendingAction = -1;
// Readable action-result popup over CFG; replaces truncated header notices.
static lv_obj_t *s_cfgActionMsgBackdrop = nullptr;
static lv_obj_t *s_cfgActionMsgModal = nullptr;
static uint32_t s_cfgActionMsgOpenedMs = 0;
// Own-message color picker (16 basic swatches), reachable from the CFG screen.
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

static lv_obj_t *s_chatNameBackdrop = nullptr;
static lv_obj_t *s_chatNameModal = nullptr;
static lv_obj_t *s_chatNameRows[CHAT_NAME_MAX + 1] = {};
static int s_chatNameSelection = 0;

static lv_obj_t *s_fontSizeBackdrop = nullptr;
static lv_obj_t *s_fontSizeModal = nullptr;
static lv_obj_t *s_fontSizeRows[FONT_SIZE_MAX + 1] = {};
static int s_fontSizeSelection = 0;

struct KnownWifiEntry {
    char ssid[sizeof(RhinoConfig::wifiSsid)];
    char pass[sizeof(RhinoConfig::wifiPass)];
};

struct ScannedWifiEntry {
    char ssid[33];
    int32_t rssi;
    bool secure;
};

static constexpr int kKnownWifiExtraCount = 5;
static constexpr int kKnownWifiMaxCount = 1 + kKnownWifiExtraCount;
static constexpr int kScannedWifiMaxCount = 20;

static KnownWifiEntry s_cfgKnownWifi[kKnownWifiMaxCount] = {};
static int s_cfgKnownWifiCount = 0;
static int s_cfgWifiSelection = 0;
static int s_cfgWifiScanSelection = 0;
static bool s_wifiUsingKnownOverride = false;
static KnownWifiEntry s_cfgKnownWifiAdded[kKnownWifiExtraCount] = {};
static int s_cfgKnownWifiAddedCount = 0;
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
static bool s_firstBoot = false;
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
#if !defined(DEVICE_TLORA_PAGER_TFT)
// (I)nformation popup over the CFG modal — pager shows this in a side panel.
static lv_obj_t *s_nodeInfoModal = nullptr;
#endif
static lv_obj_t *s_liveModal = nullptr;
static lv_obj_t *s_liveList = nullptr;

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
static lv_obj_t *s_dmHintLabel = nullptr;
static lv_obj_t *s_dmNodePickerModal = nullptr;
static lv_obj_t *s_dmNodePickerList = nullptr;
static lv_obj_t *s_dmNodePickerTitle = nullptr;
static lv_obj_t *s_dmNodePickerHint = nullptr;
static lv_obj_t *s_dmNodePickerRows[MAX_NODES] = {};

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
static uint32_t s_dmDeletePendingNodeId = 0;
static uint32_t s_dmDeleteConfirmUntilMs = 0;
static char s_dmDeleteFlashMsg[64] = "";
static uint32_t s_dmDeleteFlashUntilMs = 0;
static uint32_t s_dmTouchPressStartMs = 0;
static int s_dmTouchPressRowIdx = -1;
static bool s_dmTouchLongPressTriggered = false;
static lv_obj_t *s_nodesModal = nullptr;
static lv_obj_t *s_nodesInfoPanel = nullptr;
static lv_obj_t *s_nodesDetail = nullptr;
static lv_obj_t *s_nodesDetailExtra = nullptr;
static lv_obj_t *s_nodesMapPanel = nullptr;
static lv_obj_t *s_nodesMapTitle = nullptr;
static lv_obj_t *s_nodesMapCoords = nullptr;
static lv_obj_t *s_nodesMapMarker = nullptr;
static lv_obj_t *s_nodesMapTileLayer = nullptr;
static lv_obj_t *s_nodesMapImage = nullptr;
static char s_nodesMapImageSrc[96] = {};
static lv_fs_drv_t s_nodesMapFsDrv;
static bool s_nodesMapFsDrvReady = false;
static lv_obj_t *s_nodesList = nullptr;
static lv_obj_t *s_nodesTitleLabel = nullptr;
static lv_obj_t *s_nodesHintLabel = nullptr;
static lv_obj_t *s_nodesFilterBtn = nullptr;
static lv_obj_t *s_nodesFilterDialog = nullptr;
static lv_obj_t *s_nodesFilterInput = nullptr;
static lv_obj_t *s_nodesFilterKeyboard = nullptr;
static lv_obj_t *s_nodesListRows[MAX_NODES] = {};
static int s_nodesListRowCount = 0;
// Aliases s_nodeSnapshotIds — see the note at its definition above.
static uint32_t (&s_nodesSnapshotIds)[MAX_NODES] = s_nodeSnapshotIds;
static int s_nodesFilteredIdx[MAX_NODES] = {};
static int s_nodesSnapshotCount = 0;
static int s_nodesFilteredCount = 0;
static int s_nodesSelected = -1;
static constexpr int kNodesFilterMax = 24;
static char s_nodesFilter[kNodesFilterMax + 1] = {};
static int s_nodesFilterLen = 0;
static bool s_nodesFilterOpen = false;
// Node Actions modal: 6 actions arranged 2-per-row. Each entry has a single-key
// keyboard shortcut (see kNodesActionShortcuts) mirrored in its label as (X).
static constexpr int kNodesActionCount = 6;
static lv_obj_t *s_nodesActionModal = nullptr;
static lv_obj_t *s_nodesActionRows[kNodesActionCount] = {};
static int s_nodesActionSelection = 0;
static uint32_t s_nodesActionNodeId = 0;
// Action ordering (also referenced by executeNodesActionSelection):
//   0=Traceroute, 1=Send DM, 2=Favorite toggle, 3=Request Info,
//   4=Request Position, 5=Ignore toggle.
static constexpr char kNodesActionShortcuts[kNodesActionCount] = {
    'T', 'D', 'F', 'I', 'P', 'G'
};

// Channel Actions modal: overlay opened with (A) from the main screen. Acts on
// the active channel. Currently a single (M)ute/Un(m)ute toggle, activatable by
// touch or keyboard.
static lv_obj_t *s_channelActionsModal = nullptr;
static lv_obj_t *s_channelActionsMuteBtn = nullptr;
static lv_obj_t *s_channelActionsMuteLabel = nullptr;
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
static int s_activeChannel = 0;
static int s_lastRenderedChannel = -1;
static int s_lastRenderedCount = -1;
// Signature of the last-rendered chat content; lets refreshChatView skip the
// costly teardown/rebuild when a mesh event didn't actually change the view.
static uint32_t s_lastChatSignature = 0;
static int s_lastRenderedLiveCount = -1;
static int s_lastRenderedLiveScrollOff = -1;
static int s_cfgSelection = 0;
static int s_cfgActionCount = 0;
static int s_cfgActions[28] = {};
static char s_cfgStatus[96] = "";
static bool s_cfgOtaInstallArmed = false;
static char s_cfgOtaLatestTag[48] = "";
static int s_cfgConfirmAction = -1;
static uint32_t s_cfgConfirmMs = 0;
static uint32_t s_cfgLastActivateMs = 0;
static uint32_t s_cfgLastScrollMs = 0;
static uint32_t s_cfgEnterLockUntilMs = 0;
static bool s_cfgAwaitEnterRelease = false;
static bool s_cfgInfoPanelFocused = false;
static bool s_cfgDebugLog = (MY_DEBUG_MONITOR != 0);
static char s_otaWorkerBootNotice[160] = "";
static uint32_t s_selectedMsgReplyPacketId = 0;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static constexpr size_t kReplyPreviewTextMax = 128;
#elif defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION)
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
static char s_serialCmdBuf[96] = {};
static size_t s_serialCmdLen = 0;

enum ComposeTarget : uint8_t {
    COMPOSE_TARGET_CHANNEL = 0,
    COMPOSE_TARGET_DM = 1,
};

enum HeltecNavTarget : uint8_t {
    HELTEC_NAV_CFG = 0,
    HELTEC_NAV_DM,
    HELTEC_NAV_NODES,
    HELTEC_NAV_LIVE,
    HELTEC_NAV_ACTIONS,
    HELTEC_NAV_LEGEND,
};

static ComposeTarget s_composeTarget = COMPOSE_TARGET_CHANNEL;
static uint32_t s_composeDmNodeId = 0;

static uint32_t s_composeReplyPacketId = 0;
static int s_composeChannelIdx = 0;
static char s_lastHeaderTime[8] = "";
static uint8_t s_lastBattPct = 255;
static uint8_t s_lastGpsSats = 255;
static bool s_lastGpsEnabled = false;
static bool s_lastGpsFix = false;
static uint32_t s_lastGpsSampleMs = 0;
static bool s_lastWifiConnected = false;
static bool s_lastWifiApMode = false;
static bool s_ntpConfigured = false;
static char s_ntpServerActive[48] = "";
static uint32_t s_ntpLastConfigureMs = 0;
static uint32_t s_lastChannelGlowAnimMs = 0;
static bool s_radioReady = false;
static bool s_webCfgEnabled = false;
static bool s_screenAsleep = false;
static uint32_t s_lastActivityMs = 0;
static constexpr uint32_t kScreenWakeInputDelayMs = 3000UL;
static uint32_t s_screenWakeBlockedUntilMs = 0;
#if defined(DEVICE_TDECK) && HAS_TRACKBALL && (TBALL_CLICK >= 0)
static constexpr uint32_t kTdeckTrackballSleepHoldMs = 2000UL;
static bool s_tdeckTrackballHoldActive = false;
static bool s_tdeckTrackballHoldTriggered = false;
static uint32_t s_tdeckTrackballHoldStartMs = 0;
static bool s_tdeckSuppressRollerClick = false;
#endif
static bool s_themeRebuildPending = false;
static bool s_themeRebuildReopenCfg = false;
static int s_themeRebuildCfgSelection = 0;
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
static constexpr bool kStateMapsEnabled = false;
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
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = true;
static constexpr bool kModalCloseUsesEscape = true;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 18;
#else
static constexpr bool kPagerWheelChatNav = false;
static constexpr bool kUseScrollKeysForMainNav = false;
static constexpr bool kModalCloseUsesEscape = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 22;
#endif
#if defined(DEVICE_TDECK)
static const lv_font_t *kChannelChatFont = &lv_font_montserrat_12;
#else
static const lv_font_t *kChannelChatFont = kMainScreenFont;
#endif

// Applies the user's font-size preference (Small/Medium/Large/Extra Large) to a
// chat/DM base font. Medium returns the base unchanged so the built-in size is
// the default; Small/Large step one Montserrat size down/up and Extra Large two
// up. Anchoring to the passed base keeps "Medium" equal to each screen's current
// size on every board. Fonts below 10 px aren't compiled in, so Small clamps at
// montserrat_10; 18 px is the largest compiled face, so Extra Large clamps there.
static const lv_font_t *scaledChatFontBase(const lv_font_t *base) {
    switch (s_cfg.fontSize) {
        case FONT_SIZE_SMALL:
            if (base == &lv_font_montserrat_12) return &lv_font_montserrat_10;
            if (base == &lv_font_montserrat_14) return &lv_font_montserrat_12;
            return base;
        case FONT_SIZE_LARGE:
            if (base == &lv_font_montserrat_10) return &lv_font_montserrat_12;
            if (base == &lv_font_montserrat_12) return &lv_font_montserrat_14;
            if (base == &lv_font_montserrat_14) return &lv_font_montserrat_16;
            return base;
        case FONT_SIZE_XLARGE:
            if (base == &lv_font_montserrat_10) return &lv_font_montserrat_14;
            if (base == &lv_font_montserrat_12) return &lv_font_montserrat_16;
            if (base == &lv_font_montserrat_14) return &lv_font_montserrat_18;
            if (base == &lv_font_montserrat_16) return &lv_font_montserrat_18;
            return base;
        default:
            return base;
    }
}

// Chat/DM text face, emoji-enabled. Every chat, DM, name, and preview label
// funnels its font through here, so attaching the emoji fallback at this one
// point is what makes received emoji render everywhere — including inside
// chatFitBubbleLabel(), which measures with this same face so bubble widths
// account for emoji glyphs too.
static const lv_font_t *scaledChatFont(const lv_font_t *base) {
    return emojiFont(scaledChatFontBase(base));
}

static const char *fontSizeName(uint8_t size) {
    switch (size) {
        case FONT_SIZE_SMALL:  return "Small";
        case FONT_SIZE_LARGE:  return "Large";
        case FONT_SIZE_XLARGE: return "Extra Large";
        case FONT_SIZE_MEDIUM:
        default:               return "Medium";
    }
}
static bool s_pagerChatCursorMode = false;
static int s_pagerChatCursorDisplayIndex = -1;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
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

static inline bool isBackspaceKey(char k) {
    return k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD;
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

#if defined(DEVICE_CARDPUTER_LORA_HAT)
static inline char remapCardputerUiKey(char k, bool allowScrollRemap) {
    if (k == '`' || k == '~') return KEY_ESCAPE;
    if (allowScrollRemap) {
        if (k == ';') return KEY_SCROLL_UP;
        if (k == '.') return KEY_SCROLL_DN;
    }
    return k;
}
#endif

#if !defined(DEVICE_HELTEC_V4_EXPANSION)
static inline char remapJkUiKey(char k, bool allowScrollRemap) {
    if (!allowScrollRemap) return k;
    // Keep vim-style mapping stable everywhere: j=up, k=down.
    if (k == 'j' || k == 'J') return KEY_SCROLL_UP;
    if (k == 'k' || k == 'K') return KEY_SCROLL_DN;
    return k;
}
#endif

static void refreshChatView(bool force = false);
static uint32_t chatDateBucket(uint32_t epoch);
static void formatChatDateLabel(uint32_t epoch, char *out, size_t len);
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
static void layoutHeaderInlineItems();
static void refreshChannelGlow(bool force = false);
static void pumpKeyboardInput();
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
static void openEmojiPicker(bool sendMode = false);
static void closeEmojiPicker();
static void onChatMessagePressed(lv_event_t *e);
static void recomputeChannelHashes();
static void initCfgActions();
static void refreshCfgModal();
static void openCfgModal();
static void closeCfgModal();
static void activateCfgSelection();
static void performCfgAction(int actionId);
static void openCfgConfirmModal(int actionId);
static void closeCfgConfirmModal();
static void openCfgActionMessageModal(const char *msg);
static void closeCfgActionMessageModal();
static void onCfgActionMessageBackdropPressed(lv_event_t *e);
static void onCfgActionRowPressed(lv_event_t *e);
static void openCfgColorPickerModal();
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
static void openChatStyleModal();
static void closeChatStyleModal();
static void refreshChatStyleSelection();
static void applyChatStyleSelection(int style);
static void onChatStyleRowPressed(lv_event_t *e);
static void onChatStyleBackdropPressed(lv_event_t *e);
static void openCfgWifiPickerModal(bool forOnboarding = false);
static void closeCfgWifiPickerModal();
static void refreshCfgWifiPickerModal();
static void onCfgWifiRowPressed(lv_event_t *e);
static void onCfgWifiBackdropPressed(lv_event_t *e);
static void applyCfgWifiSelection(int idx);
static void openCfgWifiScanModal();
static void closeCfgWifiScanModal();
static void refreshCfgWifiScanModal(bool runScan);
static void openCfgWifiPassModal(int scanIdx);
static void closeCfgWifiPassModal();
static void cfgWifiConnectFromPassModal();
#if defined(DEVICE_HELTEC_V4_EXPANSION)
static void onCfgHeaderInfoPressed(lv_event_t *e);
#endif
#if !defined(DEVICE_TLORA_PAGER_TFT)
static void openNodeInfoModal();
static void closeNodeInfoModal();
#endif
static void openLegendModal();
static void closeLegendModal();
static void onLegendClosePressed(lv_event_t *e);
static void openLiveModal();
static void closeLiveModal();
static void openSysStatsModal();
static void closeSysStatsModal();
static void refreshSysStatsModal(bool force);
static void buildSysStatsColumns(char *cpuOut, char *memOut, char *stoOut, size_t sz);
static inline bool channelIsMuted(int chanIdx);
static void openChannelActionsModal();
static void closeChannelActionsModal();
static void refreshChannelActionsModal();
static void toggleActiveChannelMute();
static void onChannelActionMutePressed(lv_event_t *e);
static void logLvglMemDiag(const char *tag);
static void onHeltecBottomNavPressed(lv_event_t *e);
static void populateHeltecBottomNav(lv_obj_t *bar, int activeTarget);
static void appendHeltecBottomNav(lv_obj_t *parent, int activeTarget);
static void refreshLiveView(bool force = false);
static void openChUtilChartModal();
static void closeChUtilChartModal();
static void refreshChUtilChart(bool force = false);
static void openSnrRssiChartModal();
static void closeSnrRssiChartModal();
static void refreshSnrRssiChart(bool force = false);
static void chartPushSample(ChartHist &h, float value);
static void openDmModal();
static void closeDmModal();
static void refreshDmModal(bool force = false);
static void refreshDmPanelFocusStyles();
static DmConv *selectedDmConversation();
// allowCompose=false stops at focusing the message panel (Enter); the compose
// dialog is only opened when true (Space).
static void activateDmSelection(bool allowCompose = true);

// Bubble chat helpers (defined further down, but also used by the DM renderer
// so DMs can share the Bubbles style).
static const char *chatStripPrefix(const char *line);
static void chatBubbleBeginRender(lv_obj_t *list);
static void chatMakeBubble(lv_obj_t *list, uint32_t sender, bool isMe,
                           const char *nameTag, const char *body,
                           DisplayLine::AckState ackState,
                           uint32_t replyPacketId, bool isSelected,
                           lv_obj_t **outLast, lv_obj_t **outSelected,
                           lv_event_cb_t onPressed);

// Chat style helpers. The three styles form a cycle for the CFG toggle:
// Classic -> Bubbles -> Outline -> Classic. Both bubble styles share the bubble
// renderer; only CLASSIC uses the flat-line path.
static const char *chatStyleName(uint8_t style) {
    switch (style) {
        case CHAT_STYLE_BUBBLES: return "Bubbles";
        case CHAT_STYLE_OUTLINE: return "Outline";
        default:                 return "Classic";
    }
}
static inline bool chatStyleUsesBubbles(uint8_t style) {
    return style == CHAT_STYLE_BUBBLES || style == CHAT_STYLE_OUTLINE;
}
static const char *chatNameStyleName(uint8_t style) {
    return (style == CHAT_NAME_LONG) ? "Long" : "Short";
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

static bool dmDeleteConfirmActive(uint32_t nowMs);
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
static void onNodesFilterKeyboardEvent(lv_event_t *e);
static void onNodesFilterInputEvent(lv_event_t *e);
static void refreshNodesListRows();
static void refreshNodesListSelection();
static void refreshNodesDetails();
static void onNodeSnapshotPressed(lv_event_t *e);
static void onNodesActionRowPressed(lv_event_t *e);
static void openNodesActionMenu();
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
static void refreshNodesMap(const NodeEntry *node);
static void onWebCfgSaved();
static bool captureWebScreenshotPng(const char *outPath);
static bool pollMeshRx();
static void serviceSerialCommands();
static void handleSerialCommandLine(char *line);
static void normalizeSerialCommand(char *line);
static void serviceNodeInfoAnnounce(uint32_t nowMs);
static void serviceTelemetryAnnounce(uint32_t nowMs);
static void serviceNeighborInfoAnnounce(uint32_t nowMs);
static void serviceAutoFavorite(uint32_t nowMs);
static void applyTimezoneFromConfig();
static void setOtaWorkerBootNotice(const char *msg);
static void syncWifiCredsToPrefs();
static void persistWebCfgEnabled();
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
static void bootstrapStateMapsIfMissing();
static int nodesMapRenderTiles(float lat, float lon, int x0, int y0, int w, int h,
                               int &markerX, int &markerY);
static void sdRmDirRecursive(const char *path);
static void clearNodeDbOnSd();
static bool pagerSelectChatCursorIndex(int displayIndex);
static void pagerExitChatCursorMode(bool clearSelection = true);
static void setActiveChannel(int channelIdx);
static const char *channelName(int idx);
static void sizeChannelButtonToLabel(int idx);
static bool isChannelDropdownVisible();
static void setChannelDropdownVisible(bool visible);
static void refreshChannelSelectorLabel();
static void onChannelSelectorPressed(lv_event_t *e);
static void drawBootSplash();
static bool useCompactVerticalHeltecSelector();
static bool pollUserButton(uint32_t nowMs);
static void wakeScreen();
static void openComposePromptForDm(uint32_t nodeId);
static void rebuildUiForThemeChange(bool reopenCfg);
static void scheduleThemeRebuild(bool reopenCfg);
static void processPendingThemeRebuild();
static void applyThemeToVisibleUi(bool reopenCfg, int reopenSelection);
static void applyChannelButtonTheme();

static void openOnboardingModal();
static void closeOnboardingModal();
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
#if HAS_SD_CARD
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

#ifndef LV_SYMBOL_GPS
#define LV_SYMBOL_GPS LV_SYMBOL_DRIVE
#endif

#ifndef LV_SYMBOL_RADIO_TINY
#define LV_SYMBOL_RADIO_TINY LV_SYMBOL_VOLUME_MAX
#endif

#ifndef LV_SYMBOL_GLOBE_TINY
#define LV_SYMBOL_GLOBE_TINY LV_SYMBOL_WIFI
#endif

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

        // Normalize common apostrophe variants to ASCII for font-consistent rendering.
        if (cp == 0x2018 || cp == 0x2019 || cp == 0x02BC || cp == 0xFF07 || cp == 0x2032) {
            if (writePos + 1 < dstLen) {
                dst[writePos++] = '\'';
                dst[writePos] = '\0';
            }
            i += n;
            continue;
        }

        // Normalize common dash variants for font-consistent rendering.
        if (cp == 0x2014 || cp == 0x2015) { // em dash, horizontal bar
            if (writePos + 2 < dstLen) {
                dst[writePos++] = '-';
                dst[writePos++] = '-';
                dst[writePos] = '\0';
            } else if (writePos + 1 < dstLen) {
                dst[writePos++] = '-';
                dst[writePos] = '\0';
            }
            i += n;
            continue;
        }
        if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013
            || cp == 0x2212 || cp == 0xFE58 || cp == 0xFE63 || cp == 0xFF0D) {
            if (writePos + 1 < dstLen) {
                dst[writePos++] = '-';
                dst[writePos] = '\0';
            }
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

enum CfgActionId {
    CFG_ACTION_WEBCFG = 0,
    CFG_ACTION_WIFI_TOGGLE,
    CFG_ACTION_CHOOSE_WIFI,
    CFG_ACTION_GPS_TOGGLE,
    CFG_ACTION_EXPORT,
    CFG_ACTION_IMPORT,
    CFG_ACTION_THEME,
    CFG_ACTION_OWNER_COLOR,
    CFG_ACTION_UNITS,
    CFG_ACTION_CHAT_STYLE,
    CFG_ACTION_CHAT_NAMES,
    CFG_ACTION_CHAT_COLORS,
    CFG_ACTION_FONT_SIZE,
    CFG_ACTION_ANNOUNCE,
    CFG_ACTION_TELEMETRY,
    CFG_ACTION_NEIGHBOR_INFO,
    CFG_ACTION_SNF_CLIENT,
    CFG_ACTION_MQTT_TOGGLE,
    CFG_ACTION_MSG_ALERT,
    CFG_ACTION_SPLASH_MELODY,
    CFG_ACTION_OTA_UPDATE,
    CFG_ACTION_CLEAR_MSGS,
    CFG_ACTION_CLEAR_NODES,
    CFG_ACTION_FACTORY_RESET,
};

struct UiThemePresetLite {
    uint8_t theme;
    uint8_t mode;
    uint16_t bgMain;
    uint16_t panelBg;
    uint16_t panelAlt;
    uint16_t accent;
    const char *name;
};

static constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

static uint16_t blend565(uint16_t c1, uint16_t c2, uint8_t t);

static constexpr UiThemePresetLite kUiThemePresets[] = {
    {UI_THEME_CAMELLIA, UI_MODE_DARK,  0x0843, 0x1065, 0x18A7, 0xDA8E, "Camillia Dark"},
    {UI_THEME_CAMELLIA, UI_MODE_LIGHT,
        rgb565(0xff, 0xf7, 0xfa), rgb565(0xff, 0xfd, 0xfe), rgb565(0xf8, 0xee, 0xf3), rgb565(0xb0, 0x2f, 0x62),
        "Camillia Light"},
    {UI_THEME_EVERGREEN, UI_MODE_DARK,  0x00A8, 0x11AA, 0x1A2C, 0x55B0, "Evergreen Dark"},
    {UI_THEME_EVERGREEN, UI_MODE_LIGHT, 0xE73C, 0xF7DE, 0xE71B, 0x2D2A, "Evergreen Light"},
    {UI_THEME_EARTHEN, UI_MODE_DARK,  0x1082, 0x2104, 0x2945, 0xD38B, "Earthy Dark"},
    {UI_THEME_EARTHEN, UI_MODE_LIGHT, 0xF7DE, 0xFFDF, 0xF75C, 0xB40B, "Earthy Light"},
    {UI_THEME_SOLARIZED, UI_MODE_DARK,
        rgb565(0x00, 0x2b, 0x36), rgb565(0x07, 0x36, 0x42), rgb565(0x0c, 0x3c, 0x47), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Dark"},
    {UI_THEME_SOLARIZED, UI_MODE_LIGHT,
        rgb565(0xee, 0xe8, 0xd5), rgb565(0xfd, 0xf6, 0xe3), rgb565(0xee, 0xe8, 0xd5), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Light"},
    {UI_THEME_CRIMSON, UI_MODE_DARK,
        rgb565(0x06, 0x0f, 0x24), rgb565(0x12, 0x24, 0x4c), rgb565(0x1b, 0x33, 0x63), rgb565(0xff, 0x4a, 0x58),
        "Crimson Blue Dark"},
    {UI_THEME_CRIMSON, UI_MODE_LIGHT,
        rgb565(0xf3, 0xf7, 0xff), rgb565(0xf8, 0xfb, 0xff), rgb565(0xe6, 0xef, 0xff), rgb565(0xc6, 0x28, 0x39),
        "Crimson Blue Light"},
    {UI_THEME_SCARLET_POP, UI_MODE_DARK,
        rgb565(0x15, 0x00, 0x09), rgb565(0x76, 0x00, 0x31), rgb565(0x8b, 0x00, 0x38), rgb565(0xd5, 0x1c, 0x39),
        "Scarlet Pop Dark"},
    {UI_THEME_SCARLET_POP, UI_MODE_LIGHT,
        rgb565(0xff, 0xf2, 0xf4), rgb565(0xff, 0xf8, 0xf9), rgb565(0xff, 0xea, 0xed), rgb565(0xd5, 0x1c, 0x39),
        "Scarlet Pop Light"},
    {UI_THEME_INK_WASH, UI_MODE_DARK,
        rgb565(0x11, 0x13, 0x18), rgb565(0x1C, 0x21, 0x28), rgb565(0x25, 0x2B, 0x34), rgb565(0xD8, 0xDD, 0xE4),
        "Ink Wash Dark"},
    {UI_THEME_INK_WASH, UI_MODE_LIGHT,
        rgb565(0xF3, 0xF5, 0xF7), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xE8, 0xEB, 0xEF), rgb565(0x2E, 0x34, 0x40),
        "Ink Wash Light"},
    {UI_THEME_LAVENDAR_FIELDS, UI_MODE_DARK,
        rgb565(0x1A, 0x12, 0x30), rgb565(0x25, 0x1A, 0x45), rgb565(0x2F, 0x22, 0x58), rgb565(0xB7, 0x9B, 0xFF),
        "Lavendar Fields Dark"},
    {UI_THEME_LAVENDAR_FIELDS, UI_MODE_LIGHT,
        rgb565(0xF5, 0xEF, 0xFB), rgb565(0xFF, 0xF9, 0xFF), rgb565(0xED, 0xE1, 0xF7), rgb565(0x7B, 0x5B, 0xA7),
        "Lavendar Fields Light"},
    {UI_THEME_WILD_FLOWERS, UI_MODE_DARK,
        rgb565(0x1A, 0x24, 0x30), rgb565(0x25, 0x35, 0x47), rgb565(0x2D, 0x45, 0x5B), rgb565(0xC7, 0x8F, 0xCF),
        "Wild Flowers Dark"},
    {UI_THEME_WILD_FLOWERS, UI_MODE_LIGHT,
        rgb565(0xF6, 0xFA, 0xF4), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xE5, 0xF0, 0xE2), rgb565(0x8A, 0x5F, 0xAF),
        "Wild Flowers Light"},
    {UI_THEME_QUIET_LUXURY, UI_MODE_DARK,
        rgb565(0x2A, 0x1F, 0x17), rgb565(0x34, 0x27, 0x1E), rgb565(0x40, 0x31, 0x26), rgb565(0xD9, 0xC7, 0xA3),
        "Quiet Luxury Dark"},
    {UI_THEME_QUIET_LUXURY, UI_MODE_LIGHT,
        rgb565(0xFA, 0xF4, 0xEA), rgb565(0xFF, 0xFD, 0xF8), rgb565(0xF1, 0xE7, 0xD5), rgb565(0xA8, 0x84, 0x4F),
        "Quiet Luxury Light"},
    {UI_THEME_MORNING_DEW, UI_MODE_DARK,
        rgb565(0x12, 0x28, 0x2A), rgb565(0x1A, 0x36, 0x38), rgb565(0x23, 0x43, 0x45), rgb565(0x9C, 0xD8, 0xC8),
        "Morning Dew Dark"},
    {UI_THEME_MORNING_DEW, UI_MODE_LIGHT,
        rgb565(0xEE, 0xF9, 0xF6), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xDD, 0xF1, 0xEC), rgb565(0x4E, 0x9C, 0x8A),
        "Morning Dew Light"},
    {UI_THEME_WINTER_CHILL, UI_MODE_DARK,
        rgb565(0x15, 0x1F, 0x2B), rgb565(0x1C, 0x2A, 0x3A), rgb565(0x24, 0x36, 0x49), rgb565(0x8F, 0xB3, 0xD9),
        "Winter Chill Dark"},
    {UI_THEME_WINTER_CHILL, UI_MODE_LIGHT,
        rgb565(0xF1, 0xF7, 0xFC), rgb565(0xFF, 0xFF, 0xFF), rgb565(0xDF, 0xEB, 0xF6), rgb565(0x5C, 0x86, 0xB2),
        "Winter Chill Light"},
};

static constexpr int kUiThemePresetCount =
    (int)(sizeof(kUiThemePresets) / sizeof(kUiThemePresets[0]));

// 16 basic colors offered when the user picks the color their own messages
// render in. Stored as an index (RhinoConfig::userMsgColor); 0xFF means "use
// the adaptive theme default" (classic yellow / light-mode amber).
struct UserMsgColorOption {
    uint16_t color;      // rgb565
    const char *name;
};

static constexpr UserMsgColorOption kUserMsgColors[] = {
    {rgb565(0xFF, 0xFF, 0xFF), "White"},
    {rgb565(0xC0, 0xC0, 0xC0), "Silver"},
    {rgb565(0xFF, 0x3B, 0x30), "Red"},
    {rgb565(0xFF, 0x95, 0x00), "Orange"},
    {rgb565(0xFF, 0xE0, 0x00), "Yellow"},
    {rgb565(0x7C, 0xFC, 0x00), "Lime"},
    {rgb565(0x34, 0xC7, 0x59), "Green"},
    {rgb565(0x30, 0xD0, 0xC0), "Teal"},
    {rgb565(0x00, 0xE5, 0xFF), "Cyan"},
    {rgb565(0x5A, 0xC8, 0xFA), "Sky"},
    {rgb565(0x3B, 0x82, 0xF6), "Blue"},
    {rgb565(0x5E, 0x5C, 0xE6), "Indigo"},
    {rgb565(0xAF, 0x52, 0xDE), "Purple"},
    {rgb565(0xFF, 0x2D, 0x95), "Magenta"},
    {rgb565(0xFF, 0x6F, 0xB5), "Pink"},
    {rgb565(0xB5, 0x65, 0x1D), "Brown"},
};

static constexpr int kUserMsgColorCount =
    (int)(sizeof(kUserMsgColors) / sizeof(kUserMsgColors[0]));

struct UiPalette {
    uint16_t bgMain;
    uint16_t statusTop;
    uint16_t statusBg;
    uint16_t panelBg;
    uint16_t panelAlt;
    uint16_t panelStrong;
    uint16_t tabActive;
    uint16_t tabUnread;
    uint16_t tabIdle;
    uint16_t divider;
    uint16_t dividerHi;
    uint16_t inputBg;
    uint16_t inputTop;
    uint16_t accent;
    uint16_t cursor;
    uint16_t textMain;
    uint16_t textDim;
    uint16_t textOnAccent;
    uint16_t statusText;
    uint16_t selectBg;
    uint16_t selectAccent;
    uint16_t nodeHot;
    uint16_t nodeWarm;
    uint16_t dmMuted;
    uint16_t battGood;
    uint16_t battWarn;
    uint16_t battBad;
    uint16_t splashTop;
    uint16_t splashBottom;
    uint16_t splashCardBg;
    uint16_t splashCardEdge;
    uint16_t splashCardEdgeHi;
    uint16_t splashTitle;
    uint16_t splashSub;
    uint16_t splashDim;
};

static UiPalette s_ui = {};
static uint8_t s_appliedUiTheme = 0xFF;
static uint8_t s_appliedUiMode = 0xFF;

static const char *msgAlertSoundName(uint8_t mode) {
    switch (mode) {
        case MSG_ALERT_SOUND_CHIRPY: return "Chirpy";
        case MSG_ALERT_SOUND_BASS:   return "Bass";
        case MSG_ALERT_SOUND_OFF:    return "Off";
        case MSG_ALERT_SOUND_DEFAULT:
        default:                     return "Default";
    }
}

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
static bool sPagerAudioInitTried = false;
static bool sPagerAudioReady = false;
static constexpr i2s_port_t kPagerI2SPort = I2S_NUM_0;
static constexpr uint8_t kPagerAudioVolActive = 50;
static constexpr uint8_t kPagerAudioVolIdle = 10;
static constexpr float kPagerToneAmplitude = 7800.0f;
static uint8_t sPagerAudioVolume = 0xFF;
static audio_driver::DriverPins sPagerAudioPins;
static audio_driver::AudioBoard sPagerAudioBoard(audio_driver::AudioDriverES8311,
                                                 sPagerAudioPins);

static inline void pagerAudioApplyVolume(uint8_t volume) {
    if (sPagerAudioVolume == volume) return;
    sPagerAudioBoard.setVolume(volume);
    sPagerAudioVolume = volume;
}

// Gate the speaker power amp (XL9555 AMP_EN). The amp is left OFF while idle so
// it can't turn power-supply transients (e.g. LoRa TX current spikes) into
// random clicks; it is powered only for the duration of an actual sound.
static int sPagerExpAddr = -2;  // -2 = not yet probed
static void pagerAudioSetAmp(bool on) {
    if (sPagerExpAddr == -2) sPagerExpAddr = xl9555FindAddr();
    if (sPagerExpAddr < 0) return;
    uint8_t out0, out1, cfg0, cfg1;
    if (!xl9555ReadAll((uint8_t)sPagerExpAddr, out0, out1, cfg0, cfg1)) return;
    xl9555SetOutput(XL9555_PIN_AMP_EN, on, out0, out1, cfg0, cfg1);
    (void)xl9555WriteAll((uint8_t)sPagerExpAddr, out0, out1, cfg0, cfg1);
}

static bool pagerAudioSelectCommFormat(i2s_config_t &cfg) {
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 4)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
    // Legacy drivers expect I2S Philips mode as I2S | I2S_MSB.
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
    return true;
}

static bool pagerAudioInitI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    if (!pagerAudioSelectCommFormat(cfg)) return false;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(kPagerI2SPort, &cfg, 0, nullptr);
    if (err == ESP_ERR_INVALID_STATE) {
        i2s_driver_uninstall(kPagerI2SPort);
        err = i2s_driver_install(kPagerI2SPort, &cfg, 0, nullptr);
    }
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s install failed err=%d\n", (int)err);
        return false;
    }

    i2s_pin_config_t pinCfg = {};
    pinCfg.bck_io_num = PAGER_DAC_I2S_BCK;
    pinCfg.ws_io_num = PAGER_DAC_I2S_WS;
    pinCfg.data_out_num = PAGER_DAC_I2S_DOUT;
    pinCfg.data_in_num = I2S_PIN_NO_CHANGE;
#if defined(ESP_IDF_VERSION) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))
    pinCfg.mck_io_num = PAGER_DAC_I2S_MCLK;
#endif

    err = i2s_set_pin(kPagerI2SPort, &pinCfg);
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s set pin failed err=%d\n", (int)err);
        i2s_driver_uninstall(kPagerI2SPort);
        return false;
    }

    err = i2s_set_clk(kPagerI2SPort, 44100, I2S_BITS_PER_SAMPLE_16BIT,
                      I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        Serial.printf("[audio] i2s set clk failed err=%d\n", (int)err);
        i2s_driver_uninstall(kPagerI2SPort);
        return false;
    }

    i2s_zero_dma_buffer(kPagerI2SPort);
    return true;
}

static bool pagerAudioEnsureReady() {
    if (sPagerAudioReady) return true;
    if (!sPagerAudioInitTried) {
        sPagerAudioInitTried = true;
    } else {
        // Splash playback can run before full UI init on pager; keep retrying.
        Serial.println("[audio] pager init retry");
    }

    sPagerAudioPins.addI2C(audio_driver::PinFunction::CODEC, Wire);
    sPagerAudioPins.addI2S(audio_driver::PinFunction::CODEC,
                           PAGER_DAC_I2S_MCLK,
                           PAGER_DAC_I2S_BCK,
                           PAGER_DAC_I2S_WS,
                           PAGER_DAC_I2S_DOUT,
                           PAGER_DAC_I2S_DIN);

    (void)sPagerAudioBoard.driver().setI2CAddress(PAGER_AUDIO_CODEC_ADDR);

    audio_driver::CodecConfig cfg;
    cfg.input_device = audio_driver::ADC_INPUT_NONE;
    cfg.output_device = audio_driver::DAC_OUTPUT_ALL;
    cfg.i2s.bits = audio_driver::BIT_LENGTH_16BITS;
    cfg.i2s.rate = audio_driver::RATE_44K;
    cfg.i2s.channels = audio_driver::CHANNELS2;
    cfg.i2s.fmt = audio_driver::I2S_NORMAL;
    cfg.i2s.mode = audio_driver::MODE_SLAVE;

    if (!sPagerAudioBoard.begin(cfg)) {
        Serial.println("[audio] codec init failed");
        sPagerAudioReady = false;
        return false;
    }

    sPagerAudioVolume = 0xFF;
    pagerAudioApplyVolume(kPagerAudioVolIdle);
    // Stay muted while idle: an unmuted DAC sitting between sounds emits random
    // DC-offset clicks/snaps. We unmute only while a tone is actually playing.
    sPagerAudioBoard.setMute(true);
    pagerAudioSetAmp(false);  // amp powered only during playback (see pagerAudioSetAmp)

    if (!pagerAudioInitI2S()) {
        sPagerAudioReady = false;
        return false;
    }

    sPagerAudioReady = true;
    Serial.println("[audio] pager codec/i2s ready");
    return true;
}

static inline void pagerAudioStartPlayback() {
    // Power the speaker amp and let the class-D output stage settle before audio.
    pagerAudioSetAmp(true);
    delay(8);
    // Raise the gain while still muted so the analog volume step is inaudible.
    pagerAudioApplyVolume(kPagerAudioVolActive);
    i2s_zero_dma_buffer(kPagerI2SPort);
    // Prime codec/I2S with a short silent pre-roll so the first note isn't clipped.
    int16_t preRoll[256] = {0};
    size_t preRollWritten = 0;
    (void)i2s_write(kPagerI2SPort, preRoll, sizeof(preRoll),
                    &preRollWritten, 10 / portTICK_PERIOD_MS);
    // Unmute only once a clean stream of silence is already flowing → click-free.
    sPagerAudioBoard.setMute(false);
}

static inline void pagerAudioStopPlayback() {
    // Push a short silence tail before ending to reduce stop pops.
    int16_t tail[1024] = {0};
    size_t tailWritten = 0;
    (void)i2s_write(kPagerI2SPort, tail, sizeof(tail), &tailWritten, 20 / portTICK_PERIOD_MS);
    // Mute while the output is silent, then drop the gain (now inaudible) and
    // leave the DAC muted for idle so it can't emit stray clicks between sounds.
    sPagerAudioBoard.setMute(true);
    i2s_zero_dma_buffer(kPagerI2SPort);
    pagerAudioApplyVolume(kPagerAudioVolIdle);
    pagerAudioSetAmp(false);  // power down the amp for idle → no idle snaps
}

static void pagerAudioWriteSilence(uint16_t durationMs) {
    if (!pagerAudioEnsureReady() || durationMs == 0) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t zeroPcm[kChunkFrames * 2] = {0};

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        size_t written = 0;
        esp_err_t err = i2s_write(kPagerI2SPort, zeroPcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] i2s silence write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

static void pagerAudioPlayTone(uint16_t freqHz, uint16_t durationMs) {
    if (!pagerAudioEnsureReady()) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t pcm[kChunkFrames * 2];

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    if (framesRemaining == 0) return;
    const uint32_t totalFrames = framesRemaining;
    uint32_t frameIndex = 0;
    uint32_t attackFrames = kSampleRate / 500;
    uint32_t releaseFrames = kSampleRate / 400;
    if (attackFrames < 8) attackFrames = 8;
    if (releaseFrames < 8) releaseFrames = 8;
    uint32_t maxRamp = totalFrames / 5;
    if (maxRamp < 8) maxRamp = 8;
    if (attackFrames > maxRamp) attackFrames = maxRamp;
    if (releaseFrames > maxRamp) releaseFrames = maxRamp;

    const float phaseStep = 2.0f * (float)M_PI * (float)freqHz / (float)kSampleRate;
    float phase = 0.0f;

    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        for (int i = 0; i < framesNow; i++) {
            float s = sinf(phase);
            phase += phaseStep;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            float env = 1.0f;
            if (frameIndex < attackFrames) {
                env = (float)frameIndex / (float)attackFrames;
            }
            uint32_t framesToEnd = totalFrames - frameIndex;
            if (framesToEnd < releaseFrames) {
                float tail = (float)framesToEnd / (float)releaseFrames;
                if (tail < env) env = tail;
            }

            int16_t v = (int16_t)(s * kPagerToneAmplitude * env);
            pcm[(i * 2)] = v;
            pcm[(i * 2) + 1] = v;
            frameIndex++;
        }

        size_t written = 0;
        esp_err_t err = i2s_write(kPagerI2SPort, pcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] i2s write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

static void pagerAudioPlayAlertPattern() {
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    static const uint16_t kNotesHz[] = {880, 740, 660};
    static const uint16_t kDurMs[] = {68, 68, 112};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) pagerAudioWriteSilence(8);
    }
    pagerAudioStopPlayback();
}

static void pagerAudioPlayChirpyPattern() {
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    static const uint16_t kNotesHz[] = {1047, 1319, 1568, 1319};
    static const uint16_t kDurMs[] = {36, 36, 40, 70};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) pagerAudioWriteSilence(6);
    }
    pagerAudioStopPlayback();
}

static void pagerAudioPlayBassPattern() {
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    static const uint16_t kNotesHz[] = {392, 330, 262};
    static const uint16_t kDurMs[] = {92, 86, 130};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) pagerAudioWriteSilence(10);
    }
    pagerAudioStopPlayback();
}
} // namespace
#endif

#if defined(DEVICE_TDECK)
namespace {
static bool sTdeckAudioInitTried = false;
static bool sTdeckAudioReady = false;
static constexpr i2s_port_t kTdeckI2SPort = I2S_NUM_0;

static bool tdeckAudioSelectCommFormat(i2s_config_t &cfg) {
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 4)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
#endif
    return true;
}

static bool tdeckAudioInitI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    if (!tdeckAudioSelectCommFormat(cfg)) return false;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 256;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(kTdeckI2SPort, &cfg, 0, nullptr);
    if (err == ESP_ERR_INVALID_STATE) {
        i2s_driver_uninstall(kTdeckI2SPort);
        err = i2s_driver_install(kTdeckI2SPort, &cfg, 0, nullptr);
    }
    if (err != ESP_OK) {
        Serial.printf("[audio] tdeck i2s install failed err=%d\n", (int)err);
        return false;
    }

    i2s_pin_config_t pinCfg = {};
    pinCfg.bck_io_num = TDECK_DAC_I2S_BCK;
    pinCfg.ws_io_num = TDECK_DAC_I2S_WS;
    pinCfg.data_out_num = TDECK_DAC_I2S_DOUT;
    pinCfg.data_in_num = I2S_PIN_NO_CHANGE;
#if defined(ESP_IDF_VERSION) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))
    pinCfg.mck_io_num = I2S_PIN_NO_CHANGE;
#endif

    err = i2s_set_pin(kTdeckI2SPort, &pinCfg);
    if (err != ESP_OK) {
        Serial.printf("[audio] tdeck i2s set pin failed err=%d\n", (int)err);
        i2s_driver_uninstall(kTdeckI2SPort);
        return false;
    }

    err = i2s_set_clk(kTdeckI2SPort, 44100, I2S_BITS_PER_SAMPLE_16BIT,
                      I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        Serial.printf("[audio] tdeck i2s set clk failed err=%d\n", (int)err);
        i2s_driver_uninstall(kTdeckI2SPort);
        return false;
    }

    i2s_zero_dma_buffer(kTdeckI2SPort);
    return true;
}

static bool tdeckAudioEnsureReady() {
    if (sTdeckAudioInitTried) return sTdeckAudioReady;
    sTdeckAudioInitTried = true;
    sTdeckAudioReady = tdeckAudioInitI2S();
    if (sTdeckAudioReady) {
        Serial.println("[audio] tdeck i2s speaker ready");
    }
    return sTdeckAudioReady;
}

static inline void tdeckAudioStartPlayback() {
    i2s_zero_dma_buffer(kTdeckI2SPort);
}

static inline void tdeckAudioStopPlayback() {
    int16_t tail[128] = {0};
    size_t tailWritten = 0;
    (void)i2s_write(kTdeckI2SPort, tail, sizeof(tail), &tailWritten, 20 / portTICK_PERIOD_MS);
    i2s_zero_dma_buffer(kTdeckI2SPort);
}

static void tdeckAudioPlayTone(uint16_t freqHz, uint16_t durationMs) {
    if (!tdeckAudioEnsureReady()) return;

    static constexpr uint32_t kSampleRate = 44100;
    static constexpr int kChunkFrames = 120;
    int16_t pcm[kChunkFrames * 2];

    uint32_t framesRemaining = ((uint32_t)durationMs * kSampleRate) / 1000U;
    if (framesRemaining == 0) return;
    const uint32_t totalFrames = framesRemaining;
    uint32_t frameIndex = 0;
    uint32_t rampFrames = kSampleRate / 400;
    if (rampFrames < 12) rampFrames = 12;

    const float phaseStep = 2.0f * (float)M_PI * (float)freqHz / (float)kSampleRate;
    float phase = 0.0f;

    while (framesRemaining > 0) {
        int framesNow = (framesRemaining > (uint32_t)kChunkFrames)
                        ? kChunkFrames
                        : (int)framesRemaining;
        for (int i = 0; i < framesNow; i++) {
            float s = sinf(phase);
            phase += phaseStep;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

            float env = 1.0f;
            if (frameIndex < rampFrames) {
                env = (float)frameIndex / (float)rampFrames;
            }
            uint32_t framesToEnd = totalFrames - frameIndex;
            if (framesToEnd < rampFrames) {
                float tail = (float)framesToEnd / (float)rampFrames;
                if (tail < env) env = tail;
            }

            int16_t v = (int16_t)(s * 2800.0f * env);
            pcm[(i * 2)] = v;
            pcm[(i * 2) + 1] = v;
            frameIndex++;
        }

        size_t written = 0;
        esp_err_t err = i2s_write(kTdeckI2SPort, pcm,
                                  (size_t)(framesNow * 2 * (int)sizeof(int16_t)),
                                  &written, portMAX_DELAY);
        if (err != ESP_OK) {
            Serial.printf("[audio] tdeck i2s write failed err=%d\n", (int)err);
            break;
        }
        framesRemaining -= (uint32_t)framesNow;
    }
}

static void tdeckPlayAlertPattern() {
    if (!tdeckAudioEnsureReady()) return;
    tdeckAudioStartPlayback();
    static const uint16_t kNotesHz[] = {1760, 1480, 1320};
    static const uint16_t kDurMs[] = {34, 34, 56};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        tdeckAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) delay(10);
    }
    tdeckAudioStopPlayback();
}

static void tdeckPlayChirpyPattern() {
    if (!tdeckAudioEnsureReady()) return;
    tdeckAudioStartPlayback();
    static const uint16_t kNotesHz[] = {2093, 2637, 3136, 2637};
    static const uint16_t kDurMs[] = {20, 20, 22, 34};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        tdeckAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) delay(6);
    }
    tdeckAudioStopPlayback();
}

static void tdeckPlayBassPattern() {
    if (!tdeckAudioEnsureReady()) return;
    tdeckAudioStartPlayback();
    static const uint16_t kNotesHz[] = {784, 659, 523};
    static const uint16_t kDurMs[] = {54, 50, 74};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        tdeckAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) delay(12);
    }
    tdeckAudioStopPlayback();
}
} // namespace
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
namespace {
static bool sCardputerAudioReady = false;

static inline void cardputerAudioEnsureReady() {
    if (sCardputerAudioReady) return;
    cardputerSpeakerSetVolume(180);
    sCardputerAudioReady = true;
}

static void cardputerPlayTonePattern(const uint16_t *notesHz,
                                     const uint16_t *durMs,
                                     size_t noteCount,
                                     uint16_t gapMs) {
    cardputerAudioEnsureReady();
    for (size_t i = 0; i < noteCount; i++) {
        (void)cardputerSpeakerTone((float)notesHz[i], durMs[i], 0, true);
        delay((uint32_t)durMs[i]);
        if (i + 1 < noteCount && gapMs) delay(gapMs);
    }
}

static void cardputerPlayAlertPattern() {
    static const uint16_t kNotesHz[] = {1568, 1319, 1175};
    static const uint16_t kDurMs[] = {34, 34, 56};
    cardputerPlayTonePattern(kNotesHz, kDurMs, sizeof(kNotesHz) / sizeof(kNotesHz[0]), 10);
}

static void cardputerPlayChirpyPattern() {
    static const uint16_t kNotesHz[] = {1976, 2489, 2960, 2489};
    static const uint16_t kDurMs[] = {20, 20, 22, 34};
    cardputerPlayTonePattern(kNotesHz, kDurMs, sizeof(kNotesHz) / sizeof(kNotesHz[0]), 6);
}

static void cardputerPlayBassPattern() {
    static const uint16_t kNotesHz[] = {659, 554, 440};
    static const uint16_t kDurMs[] = {54, 50, 74};
    cardputerPlayTonePattern(kNotesHz, kDurMs, sizeof(kNotesHz) / sizeof(kNotesHz[0]), 12);
}
} // namespace
#endif

static void triggerMessageAlert(bool bypassRateLimit = false) {
    static uint32_t lastAlertMs = 0;
    uint32_t nowMs = millis();
    if (!bypassRateLimit && nowMs - lastAlertMs < 120) return;
    lastAlertMs = nowMs;

#if defined(DEVICE_TLORA_PAGER_TFT)
    if (s_cfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;

    switch (s_cfg.msgAlertSound) {
        case MSG_ALERT_SOUND_CHIRPY:
            pagerAudioPlayChirpyPattern();
            break;
        case MSG_ALERT_SOUND_BASS:
            pagerAudioPlayBassPattern();
            break;
        case MSG_ALERT_SOUND_OFF:
            return;
        case MSG_ALERT_SOUND_DEFAULT:
        default:
            pagerAudioPlayAlertPattern();
            break;
    }
#elif defined(DEVICE_TDECK)
    if (s_cfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;

    switch (s_cfg.msgAlertSound) {
        case MSG_ALERT_SOUND_CHIRPY:
            tdeckPlayChirpyPattern();
            break;
        case MSG_ALERT_SOUND_BASS:
            tdeckPlayBassPattern();
            break;
        case MSG_ALERT_SOUND_OFF:
            return;
        case MSG_ALERT_SOUND_DEFAULT:
        default:
            tdeckPlayAlertPattern();
            break;
    }
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    if (s_cfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;

    switch (s_cfg.msgAlertSound) {
        case MSG_ALERT_SOUND_CHIRPY:
            cardputerPlayChirpyPattern();
            break;
        case MSG_ALERT_SOUND_BASS:
            cardputerPlayBassPattern();
            break;
        case MSG_ALERT_SOUND_OFF:
            return;
        case MSG_ALERT_SOUND_DEFAULT:
        default:
            cardputerPlayAlertPattern();
            break;
    }
#elif (BOARD_BUZZER >= 0)
    if (s_cfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;
    tone(BOARD_BUZZER, 1760, 60);
#endif
}

static void playSplashStartupRiff() {
    if (!s_cfg.splashMelodyEnabled) return;

    // Middle-octave riff: E, F, F#, F, E, D#, E
    static const uint16_t kNotesHz[] = {330, 349, 370, 349, 330, 311, 330};
    static const uint16_t kQuarterMs = 220;
    static const uint16_t kPause16thMs = 55;
    static const uint16_t kDurMs[] = {
        kQuarterMs, kQuarterMs, kQuarterMs, kQuarterMs, kQuarterMs, kQuarterMs, kQuarterMs
    };
    static const size_t kCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);

#if defined(DEVICE_TLORA_PAGER_TFT)
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    for (size_t i = 0; i < kCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kCount) pagerAudioWriteSilence(kPause16thMs);
    }
    pagerAudioStopPlayback();
#elif defined(DEVICE_TDECK)
    if (!tdeckAudioEnsureReady()) return;
    tdeckAudioStartPlayback();
    for (size_t i = 0; i < kCount; i++) {
        tdeckAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kCount) delay(kPause16thMs);
    }
    tdeckAudioStopPlayback();
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    cardputerPlayTonePattern(kNotesHz, kDurMs, kCount, kPause16thMs);
#elif (BOARD_BUZZER >= 0)
    for (size_t i = 0; i < kCount; i++) {
        tone(BOARD_BUZZER, kNotesHz[i], kDurMs[i]);
        delay((uint32_t)kDurMs[i] + (i + 1 < kCount ? kPause16thMs : 0));
    }
#endif
}

static int uiThemePresetIndexFromCfg() {
    for (int i = 0; i < kUiThemePresetCount; i++) {
        if (kUiThemePresets[i].theme == s_cfg.uiTheme
            && kUiThemePresets[i].mode == s_cfg.uiMode) {
            return i;
        }
    }
    return 0;
}

static const char *uiThemePresetNameFromCfg() {
    int idx = uiThemePresetIndexFromCfg();
    if (idx < 0 || idx >= kUiThemePresetCount) return "Camillia Dark";
    return kUiThemePresets[idx].name;
}

static void persistUiTheme() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putUChar("uiTheme", s_cfg.uiTheme);
    p.putUChar("uiMode", s_cfg.uiMode);
    p.end();
}

static void persistMessageAlertSetting() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putUChar("msgAlertSound", s_cfg.msgAlertSound);
    p.end();
}

static void persistSplashMelodySetting() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool("splashMelody", s_cfg.splashMelodyEnabled);
    p.end();
}

static void applyUiThemePalette() {
    s_cfg.uiTheme = (uint8_t)constrain((int)s_cfg.uiTheme, 0, UI_THEME_COUNT - 1);
    s_cfg.uiMode  = (uint8_t)(s_cfg.uiMode == UI_MODE_LIGHT ? UI_MODE_LIGHT : UI_MODE_DARK);

    const UiThemePresetLite *preset = &kUiThemePresets[0];
    for (int i = 0; i < kUiThemePresetCount; i++) {
        if (kUiThemePresets[i].theme == s_cfg.uiTheme && kUiThemePresets[i].mode == s_cfg.uiMode) {
            preset = &kUiThemePresets[i];
            break;
        }
    }

    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const uint16_t bgMain = preset->bgMain;
    const uint16_t panelBg = preset->panelBg;
    const uint16_t panelAlt = preset->panelAlt;
    const uint16_t accent = preset->accent;

    const uint16_t statusTop = blend565(bgMain, panelBg, isLight ? 128 : 96);
    const uint16_t statusBg = isLight ? panelAlt : panelBg;
    const uint16_t panelStrong = blend565(panelAlt, accent, isLight ? 36 : 48);
    const uint16_t tabActive = accent;
    const uint16_t tabUnread = blend565(accent, rgb565(0xFF, 0xB3, 0x00), 92);
    const uint16_t tabIdle = blend565(panelAlt, bgMain, isLight ? 90 : 120);
    const uint16_t divider = blend565(panelAlt, bgMain, isLight ? 135 : 150);
    const uint16_t dividerHi = blend565(panelAlt, accent, isLight ? 74 : 92);
    const uint16_t inputBg = panelAlt;
    const uint16_t inputTop = blend565(panelBg, panelAlt, 120);
    const uint16_t cursor = accent;

    const uint16_t textMain = isLight ? rgb565(0x1E, 0x24, 0x2C) : rgb565(0xF3, 0xF6, 0xFA);
    const uint16_t textDim = isLight ? rgb565(0x5E, 0x68, 0x76) : rgb565(0xB7, 0xC0, 0xCC);
    const uint16_t textOnAccent = isLight ? rgb565(0xFF, 0xFF, 0xFF) : rgb565(0x08, 0x0D, 0x14);
    const uint16_t statusText = textMain;

    const uint16_t selectBg = blend565(panelAlt, accent, isLight ? 36 : 44);
    const uint16_t selectAccent = accent;
    const uint16_t nodeHot = blend565(accent, rgb565(0xFF, 0x58, 0x58), 140);
    const uint16_t nodeWarm = blend565(accent, rgb565(0xFF, 0xB7, 0x2C), 120);
    const uint16_t dmMuted = isLight ? rgb565(0x8E, 0x95, 0xA2) : rgb565(0x8C, 0x97, 0xA8);

    const uint16_t battGood = rgb565(0x3A, 0xC7, 0x62);
    const uint16_t battWarn = rgb565(0xF2, 0xB5, 0x2E);
    const uint16_t battBad = rgb565(0xE0, 0x4F, 0x4F);

    const uint16_t splashTop = statusTop;
    const uint16_t splashBottom = bgMain;
    const uint16_t splashCardBg = panelBg;
    const uint16_t splashCardEdge = blend565(panelBg, accent, isLight ? 52 : 66);
    const uint16_t splashCardEdgeHi = blend565(panelAlt, accent, isLight ? 74 : 92);
    const uint16_t splashTitle = textMain;
    const uint16_t splashSub = textDim;
    const uint16_t splashDim = blend565(textDim, panelBg, isLight ? 84 : 72);

    s_ui = {
        bgMain, statusTop, statusBg, panelBg, panelAlt, panelStrong,
        tabActive, tabUnread, tabIdle, divider, dividerHi, inputBg, inputTop,
        accent, cursor, textMain, textDim, textOnAccent, statusText,
        selectBg, selectAccent, nodeHot, nodeWarm, dmMuted,
        battGood, battWarn, battBad,
        splashTop, splashBottom, splashCardBg, splashCardEdge, splashCardEdgeHi,
        splashTitle, splashSub, splashDim
    };

    s_appliedUiTheme = s_cfg.uiTheme;
    s_appliedUiMode = s_cfg.uiMode;
}

static inline lv_color_t lvColorFrom565(uint16_t c) {
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
    return lv_color_make(r, g, b);
}

static constexpr uint16_t hex24To565(uint32_t rgb) {
    return rgb565((uint8_t)((rgb >> 16) & 0xFF),
                  (uint8_t)((rgb >> 8) & 0xFF),
                  (uint8_t)(rgb & 0xFF));
}

static uint16_t blend565(uint16_t c1, uint16_t c2, uint8_t t) {
    int r1 = (c1 >> 11) & 0x1F;
    int g1 = (c1 >> 5) & 0x3F;
    int b1 = c1 & 0x1F;
    int r2 = (c2 >> 11) & 0x1F;
    int g2 = (c2 >> 5) & 0x3F;
    int b2 = c2 & 0x1F;
    int r = r1 + ((r2 - r1) * t) / 255;
    int g = g1 + ((g2 - g1) * t) / 255;
    int b = b1 + ((b2 - b1) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static bool isTrafficBgColor24(uint32_t rgb) {
    switch (rgb) {
        case 0x4A1D1D:
        case 0x1E3E27:
        case 0x1B3E34:
        case 0x4A4318:
        case 0x4A2D1F:
        case 0x4A3418:
        case 0x1A3B40:
        case 0x33224A:
        case 0x12345D:
        case 0x1D2E58:
        case 0x1A3754:
        case 0x1A3A3E:
        case 0x4A3618:
        case 0x102D52:
        case 0x3E3619:
        case 0x10254A:
            return true;
        default:
            return false;
    }
}

static lv_color_t themedColorHex(uint32_t rgb) {
    uint16_t mapped = 0;
    bool hasMapped = true;

    switch (rgb) {
        case 0x0B1E44: mapped = s_ui.bgMain; break;
        case 0x0E285B: mapped = s_ui.panelBg; break;
        case 0x0F2A5C: mapped = s_ui.panelAlt; break;
        case 0x102750: mapped = s_ui.tabIdle; break;
        case 0x102B61:
        case 0x1E355F: mapped = s_ui.inputBg; break;
        case 0x123266: mapped = s_ui.panelStrong; break;
        case 0x2A4E8F: mapped = s_ui.selectBg; break;
        case 0x2A4FB4: mapped = s_ui.tabActive; break;
        case 0x2B4D8C:
        case 0x335D9D: mapped = s_ui.divider; break;
        case 0x3F669F:
        case 0x8FB5E6:
        case 0x5C86C6: mapped = s_ui.dividerHi; break;
        case 0x4C76BA:
        case 0x5B86C7: mapped = s_ui.inputTop; break;
        case 0x79DDB8:
        case 0x84E07A:
        case 0x2C7A3B: mapped = s_ui.battGood; break;
        case 0x8EEBFF: mapped = s_ui.accent; break;
        case 0x4EC9FF:
        case 0x90B4FF: mapped = s_ui.selectAccent; break;
        case 0xA7C7FF: mapped = s_ui.textDim; break;
        case 0xBFD6FF: mapped = s_ui.statusText; break;
        case 0xD9E8FF:
        case 0xE8F1FF: mapped = s_ui.textMain; break;
        case 0xEAF3FF: mapped = s_ui.textOnAccent; break;
        case 0xFFF0B8: mapped = s_ui.tabUnread; break;
        case 0xF4D35E: mapped = s_ui.battWarn; break;
        case 0xFF6B6B: mapped = s_ui.battBad; break;
        default: hasMapped = false; break;
    }

    if (hasMapped) {
        return lvColorFrom565(mapped);
    }

    if (isTrafficBgColor24(rgb)) {
        uint16_t tone = hex24To565(rgb);
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            // Lift live-traffic backgrounds in light mode so they stay readable.
            tone = blend565(tone, 0xFFFF, 170);
        }
        return lvColorFrom565(tone);
    }

    return lv_color_make((uint8_t)((rgb >> 16) & 0xFF),
                         (uint8_t)((rgb >> 8) & 0xFF),
                         (uint8_t)(rgb & 0xFF));
}

#define lv_color_hex(rgb) themedColorHex((uint32_t)(rgb))

static void populateKnownWifiEntries() {
    s_cfgKnownWifiCount = 0;

    // Slot 0 mirrors configured credentials from web/onboarding config.
    if (s_cfgKnownWifiCount < kKnownWifiMaxCount) {
        KnownWifiEntry &entry = s_cfgKnownWifi[s_cfgKnownWifiCount++];
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.ssid, s_cfg.wifiSsid, sizeof(entry.ssid) - 1);
        strncpy(entry.pass, s_cfg.wifiPass, sizeof(entry.pass) - 1);
    }

    // Add known networks created by successful scan/connect attempts.
    for (int i = 0; i < s_cfgKnownWifiAddedCount && s_cfgKnownWifiCount < kKnownWifiMaxCount; i++) {
        KnownWifiEntry &entry = s_cfgKnownWifi[s_cfgKnownWifiCount++];
        entry = s_cfgKnownWifiAdded[i];
    }

    // Keep picker selection anchored to the currently active network source.
    int selected = 0;
    if (s_wifiUsingKnownOverride && s_wifiSelectedSsid[0]) {
        for (int i = 0; i < s_cfgKnownWifiCount; i++) {
            if (strncmp(s_cfgKnownWifi[i].ssid, s_wifiSelectedSsid,
                        sizeof(s_cfgKnownWifi[i].ssid)) == 0) {
                selected = i;
                break;
            }
        }
    }
    s_cfgWifiSelection = selected;
}

static void addTempKnownWifi(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) return;

    // Configured network remains in slot 0; no need to duplicate it.
    if (strncmp(ssid, s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid)) == 0) {
        return;
    }

    for (int i = 0; i < s_cfgKnownWifiAddedCount; i++) {
        if (strncmp(s_cfgKnownWifiAdded[i].ssid,
                    ssid,
                    sizeof(s_cfgKnownWifiAdded[i].ssid)) == 0) {
            strncpy(s_cfgKnownWifiAdded[i].pass,
                    pass ? pass : "",
                    sizeof(s_cfgKnownWifiAdded[i].pass) - 1);
            return;
        }
    }

    KnownWifiEntry newEntry = {};
    strncpy(newEntry.ssid, ssid, sizeof(newEntry.ssid) - 1);
    strncpy(newEntry.pass, pass ? pass : "", sizeof(newEntry.pass) - 1);

    if (s_cfgKnownWifiAddedCount < kKnownWifiExtraCount) {
        s_cfgKnownWifiAdded[s_cfgKnownWifiAddedCount++] = newEntry;
        return;
    }

    // Keep a rolling window of known scanned networks.
    for (int i = 1; i < kKnownWifiExtraCount; i++) {
        s_cfgKnownWifiAdded[i - 1] = s_cfgKnownWifiAdded[i];
    }
    s_cfgKnownWifiAdded[kKnownWifiExtraCount - 1] = newEntry;
}

static void wifiGetActiveCreds(const char **ssidOut, const char **passOut) {
    const char *ssid = s_cfg.wifiSsid;
    const char *pass = s_cfg.wifiPass;
    if (s_wifiUsingKnownOverride && s_wifiSelectedSsid[0]) {
        ssid = s_wifiSelectedSsid;
        pass = s_wifiSelectedPass;
    }
    if (ssidOut) *ssidOut = ssid;
    if (passOut) *passOut = pass;
}

static bool wifiHasActiveCreds() {
    const char *ssid = nullptr;
    wifiGetActiveCreds(&ssid, nullptr);
    return ssid && ssid[0];
}

static const char *wifiActiveSsid() {
    const char *ssid = nullptr;
    wifiGetActiveCreds(&ssid, nullptr);
    return (ssid && ssid[0]) ? ssid : "";
}

static bool wifiBeginActiveKnown() {
    const char *ssid = nullptr;
    const char *pass = nullptr;
    wifiGetActiveCreds(&ssid, &pass);
    if (!ssid || !ssid[0]) return false;
    WiFi.begin(ssid, pass ? pass : "");
    return true;
}

static const char *cfgActionLabel(int actionId, char *buf, size_t bufLen) {
    if (!buf || bufLen == 0) return "";
    switch (actionId) {
        case CFG_ACTION_WIFI_TOGGLE:
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "WiFi: Off");
            } else if ((uint32_t)WiFi.localIP() != 0) {
                // Real STA/network address (MQTT etc.), shown regardless of web config.
                snprintf(buf, bufLen, "WiFi: On (%s)", WiFi.localIP().toString().c_str());
            } else if (webCfgRunning()) {
                snprintf(buf, bufLen, "WiFi: On (%s)", webCfgIP());  // AP fallback
            } else if (!wifiHasActiveCreds()) {
                snprintf(buf, bufLen, "WiFi: On (AP MODE ONLY)");
            } else {
                snprintf(buf, bufLen, "WiFi: On (connecting %s)", wifiActiveSsid());
            }
            break;
        case CFG_ACTION_CHOOSE_WIFI:
            if (s_wifiUsingKnownOverride && s_wifiSelectedSsid[0]) {
                snprintf(buf, bufLen, "Choose WiFi: %s", s_wifiSelectedSsid);
            } else if (s_cfg.wifiSsid[0]) {
                snprintf(buf, bufLen, "Choose WiFi: %s", s_cfg.wifiSsid);
            } else {
                snprintf(buf, bufLen, "Choose WiFi: Configured (none)");
            }
            break;
        case CFG_ACTION_WEBCFG:
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "Web Config: Off (WiFi off)");
            } else if (s_cfg.mqttEnabled) {
                snprintf(buf, bufLen, "Web Config: Off (MQTT on)");
            } else if (!s_webCfgEnabled) {
                snprintf(buf, bufLen, "Web Config: Disabled");
            } else if (webCfgRunning()) {
                snprintf(buf, bufLen, "Web Config: On");
            } else {
                snprintf(buf, bufLen, "Web Config: Enabled");
            }
            break;
        case CFG_ACTION_GPS_TOGGLE:
            if (s_cfg.gpsEnabled) {
                snprintf(buf, bufLen, "GPS: Enabled (Hardware)");
            } else {
                snprintf(buf, bufLen, "GPS: Disabled (Default Coords)");
            }
            break;
        case CFG_ACTION_EXPORT:
            snprintf(buf, bufLen, "Export Config");
            break;
        case CFG_ACTION_IMPORT:
            snprintf(buf, bufLen, "Import Config");
            break;
        case CFG_ACTION_THEME:
            snprintf(buf, bufLen, "Theme: %s", uiThemePresetNameFromCfg());
            break;
        case CFG_ACTION_OWNER_COLOR:
            snprintf(buf, bufLen, "My Message Color: %s",
                     (s_cfg.userMsgColor < kUserMsgColorCount)
                         ? kUserMsgColors[s_cfg.userMsgColor].name
                         : "Default");
            break;
        case CFG_ACTION_UNITS:
            snprintf(buf, bufLen, "Units: %s", s_cfg.displayUnits ? "Imperial" : "Metric");
            break;
        case CFG_ACTION_CHAT_STYLE:
            snprintf(buf, bufLen, "Chat Style: %s", chatStyleName(s_cfg.chatStyle));
            break;
        case CFG_ACTION_CHAT_NAMES:
            snprintf(buf, bufLen, "Chat Names: %s", chatNameStyleName(s_cfg.chatNameStyle));
            break;
        case CFG_ACTION_CHAT_COLORS:
            snprintf(buf, bufLen, "Chat Colors: %s",
                     s_cfg.chatColorsEnabled ? "On" : "Off");
            break;
        case CFG_ACTION_FONT_SIZE:
            snprintf(buf, bufLen, "Font Size: %s", fontSizeName(s_cfg.fontSize));
            break;
        case CFG_ACTION_ANNOUNCE:
            snprintf(buf, bufLen, "Send NODEINFO Broadcast");
            break;
        case CFG_ACTION_TELEMETRY:
            snprintf(buf, bufLen, "Send Telemetry Now");
            break;
        case CFG_ACTION_NEIGHBOR_INFO:
            snprintf(buf, bufLen, "Neighborhood Info: %s", s_cfg.neighborInfoEnabled ? "On" : "Off");
            break;
        case CFG_ACTION_SNF_CLIENT:
            snprintf(buf, bufLen, "Store&Fwd Client: %s", s_cfg.snfClientEnabled ? "On" : "Off");
            break;
        case CFG_ACTION_MQTT_TOGGLE:
            if (!s_cfg.wifiEnabled) {
                snprintf(buf, bufLen, "MQTT Bridge: Off (WiFi off)");
            } else {
                snprintf(buf, bufLen, "MQTT Bridge: %s", s_cfg.mqttEnabled ? "On" : "Off");
            }
            break;
        case CFG_ACTION_MSG_ALERT:
            snprintf(buf, bufLen, "Notification Sound: %s", msgAlertSoundName(s_cfg.msgAlertSound));
            break;
        case CFG_ACTION_SPLASH_MELODY:
            snprintf(buf, bufLen, "Splash Melody: %s", s_cfg.splashMelodyEnabled ? "On" : "Off");
            break;
        case CFG_ACTION_OTA_UPDATE:
            if (s_cfgOtaInstallArmed && s_cfgOtaLatestTag[0]) {
                snprintf(buf, bufLen, "Firmware Update: Install %s", s_cfgOtaLatestTag);
            } else {
                snprintf(buf, bufLen, "Firmware Update Check");
            }
            break;
        case CFG_ACTION_CLEAR_MSGS:
            snprintf(buf, bufLen, "Clear Messages");
            break;
        case CFG_ACTION_CLEAR_NODES:
            snprintf(buf, bufLen, "Clear Nodes");
            break;
        case CFG_ACTION_FACTORY_RESET:
            snprintf(buf, bufLen, "Factory Reset");
            break;
        default:
            snprintf(buf, bufLen, "(unknown)");
            break;
    }
    return buf;
}

// A CFG row is greyed out and non-interactive when its precondition is unmet:
// both network consumers require WiFi, and the web-config portal is locked off
// while the MQTT bridge is on (they don't run together).
static bool cfgActionDisabled(int actionId) {
    switch (actionId) {
        case CFG_ACTION_MQTT_TOGGLE: return !s_cfg.wifiEnabled;
        case CFG_ACTION_WEBCFG:      return !s_cfg.wifiEnabled || s_cfg.mqttEnabled;
        default:                     return false;
    }
}

static const char *cfgDeviceRoleName(uint8_t role) {
    switch (role) {
        case 0:  return "CLIENT";
        case 1:  return "CLIENT_MUTE";
        case 2:  return "CLIENT_HIDDEN_MQTT";
        case 3:  return "ROUTER_CLIENT";
        case 4:  return "REPEATER";
        case 5:  return "TRACKER";
        case 6:  return "SENSOR";
        case 7:  return "TAK";
        case 8:  return "CLIENT_HIDDEN";
        case 9:  return "LOST_AND_FOUND";
        case 10: return "TAK_TRACKER";
        default: return "UNKNOWN";
    }
}

static bool cfgActionNeedsConfirm(int actionId) {
    return actionId == CFG_ACTION_EXPORT
        || actionId == CFG_ACTION_IMPORT
        || actionId == CFG_ACTION_OTA_UPDATE
    || actionId == CFG_ACTION_CLEAR_MSGS
        || actionId == CFG_ACTION_CLEAR_NODES
        || actionId == CFG_ACTION_FACTORY_RESET;
}

static void cfgDebugSelection(const char *tag, int actionId) {
    if (!s_cfgDebugLog) return;
    char actionText[80];
    Serial.printf("[lvgl-cfg] %s sel=%d action=%d label=\"%s\"\n",
                  tag,
                  s_cfgSelection,
                  actionId,
                  cfgActionLabel(actionId, actionText, sizeof(actionText)));
}

static void syncWifiCredsToPrefs() {
    if (!s_cfg.wifiSsid[0]) return;

    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putString("wifiSsid", s_cfg.wifiSsid);
    p.putString("wifiPass", s_cfg.wifiPass);
    p.end();
}

static void persistWebCfgEnabled() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool("webCfgEnabled", s_webCfgEnabled);
    p.end();
}

// NVS keys are limited to 15 chars on ESP32.
static constexpr const char *kPrefSkipWebAutoOnce = "skipWebAuto1";
static constexpr const char *kPrefOtaWorkerOnce = "otaWorker1";
static constexpr uint32_t kOtaWorkerRtcMagic = 0x4F544131UL;  // "OTA1"
RTC_DATA_ATTR static uint32_t s_otaWorkerRtcFlag = 0;

static void setOtaWorkerBootNotice(const char *msg) {
    if (!msg) msg = "";
    strncpy(s_otaWorkerBootNotice, msg, sizeof(s_otaWorkerBootNotice) - 1);
    s_otaWorkerBootNotice[sizeof(s_otaWorkerBootNotice) - 1] = '\0';
}

static void requestSkipWebAutoStartOnce() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    p.putBool(kPrefSkipWebAutoOnce, true);
    p.end();
}

static bool consumeSkipWebAutoStartOnce() {
    Preferences p;
    if (!p.begin("camillia", false)) return false;
    bool skip = p.getBool(kPrefSkipWebAutoOnce, false);
    if (skip) {
        p.putBool(kPrefSkipWebAutoOnce, false);
    }
    p.end();
    return skip;
}

static bool requestOtaWorkerModeOnce() {
    // RTC survives software reboot and gives us a second, low-friction path
    // when NVS writes are temporarily unreliable.
    s_otaWorkerRtcFlag = kOtaWorkerRtcMagic;

    Preferences p;
    if (!p.begin("camillia", false)) return true;
    p.putBool(kPrefOtaWorkerOnce, true);
    p.end();

    Preferences v;
    if (!v.begin("camillia", true)) return true;
    bool verify = v.getBool(kPrefOtaWorkerOnce, false);
    v.end();
    return verify || (s_otaWorkerRtcFlag == kOtaWorkerRtcMagic);
}

static bool isOtaWorkerModeRequestedRtc() {
    return s_otaWorkerRtcFlag == kOtaWorkerRtcMagic;
}

static bool consumeOtaWorkerModeRtcOnce() {
    bool run = (s_otaWorkerRtcFlag == kOtaWorkerRtcMagic);
    if (run) {
        s_otaWorkerRtcFlag = 0;
    }
    return run;
}

static bool isOtaWorkerModeRequestedOnce() {
    Preferences p;
    if (!p.begin("camillia", true)) return false;
    bool run = p.getBool(kPrefOtaWorkerOnce, false);
    p.end();
    return run;
}

static bool consumeOtaWorkerModeOnce() {
    Preferences p;
    if (!p.begin("camillia", false)) return false;
    bool run = p.getBool(kPrefOtaWorkerOnce, false);
    if (run) {
        p.putBool(kPrefOtaWorkerOnce, false);
    }
    p.end();
    return run;
}

static bool s_otaWorkerUiReady = true;

static void otaWorkerDrawStatus(const char *line1, const char *line2 = nullptr) {
    if (!s_otaWorkerUiReady) return;
    displayDev().fillScreen(TFT_BLACK);
    displayDev().setTextColor(TFT_WHITE, TFT_BLACK);
    displayDev().setTextSize(1);
    displayDev().setFont(&fonts::DejaVu12);

    int y = 22;
    if (line1 && line1[0]) {
        displayDev().drawString(line1, 8, y);
        y += 22;
    }
    if (line2 && line2[0]) {
        displayDev().drawString(line2, 8, y);
    }
}

static void otaWorkerDrawProgress(const char *title,
                                  const char *detail,
                                  size_t written,
                                  size_t total,
                                  bool stalled) {
    if (!s_otaWorkerUiReady) return;
    displayDev().fillScreen(TFT_BLACK);
    displayDev().setTextSize(1);
    displayDev().setFont(&fonts::DejaVu12);

    displayDev().setTextColor(TFT_WHITE, TFT_BLACK);
    if (title && title[0]) displayDev().drawString(title, 8, 14);

    const int barX = 8;
    const int barY = 52;
    const int barW = max(120, (int)displayDev().width() - 16);
    const int barH = 18;
    displayDev().drawRect(barX, barY, barW, barH, TFT_WHITE);

    int fillW = 0;
    int pct = 0;
    if (total > 0) {
        pct = (int)((written * 100UL) / total);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        fillW = (barW - 2) * pct / 100;
    }

    if (fillW > 0) {
        uint16_t fillColor = stalled ? TFT_ORANGE : TFT_GREEN;
        displayDev().fillRect(barX + 1, barY + 1, fillW, barH - 2, fillColor);
    }

    char pctBuf[24];
    if (total > 0) snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    else snprintf(pctBuf, sizeof(pctBuf), "working...");
    displayDev().setTextColor(stalled ? TFT_ORANGE : TFT_CYAN, TFT_BLACK);
    displayDev().drawString(pctBuf, 8, 78);

    char bytesBuf[64];
    if (total > 0) {
        snprintf(bytesBuf,
                 sizeof(bytesBuf),
                 "%lu / %lu KB",
                 (unsigned long)(written / 1024UL),
                 (unsigned long)(total / 1024UL));
    } else {
        snprintf(bytesBuf,
                 sizeof(bytesBuf),
                 "%lu KB downloaded",
                 (unsigned long)(written / 1024UL));
    }
    displayDev().setTextColor(TFT_WHITE, TFT_BLACK);
    displayDev().drawString(bytesBuf, 8, 102);

    if (detail && detail[0]) {
        displayDev().setTextColor(stalled ? TFT_ORANGE : TFT_WHITE, TFT_BLACK);
        displayDev().drawString(detail, 8, 126);
    }
}

static bool otaWorkerEnsureWifiConnected() {
    if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
        return true;
    }
    if (!wifiHasActiveCreds()) {
        return false;
    }

    wifi_mode_t mode = WiFi.getMode();
    switch (mode) {
        case WIFI_OFF:
            WiFi.mode(WIFI_STA);
            wifiBeginActiveKnown();
            break;
        case WIFI_STA:
            wifiBeginActiveKnown();
            break;
#ifdef WIFI_AP_STA
        case WIFI_AP:
            WiFi.mode(WIFI_AP_STA);
            wifiBeginActiveKnown();
            break;
#endif
        case WIFI_AP_STA:
            wifiBeginActiveKnown();
            break;
        default:
            break;
    }

    uint32_t startMs = millis();
    while ((millis() - startMs) < 12000UL) {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            return true;
        }
        delay(120);
    }

    return (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP);
}

static bool otaWorkerErrIsTlsLowMem(const char *err) {
    return err && strstr(err, "TLS init failed (low memory)") != nullptr;
}

static void otaWorkerLogHeap(const char *tag) {
    const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    Serial.printf("[ota-worker] %s heap int_free=%lu largest=%lu\n",
                  tag ? tag : "heap",
                  (unsigned long)freeInt,
                  (unsigned long)largestInt);
}

static bool otaWorkerReconnectWifiForLowMemRetry() {
    if (!wifiHasActiveCreds()) return false;

    Serial.println("[ota-worker] low-mem retry: cycling WiFi stack");
    otaWorkerLogHeap("before wifi recycle");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(180);

    WiFi.mode(WIFI_STA);
    wifiBeginActiveKnown();

    uint32_t startMs = millis();
    while ((millis() - startMs) < 12000UL) {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            otaWorkerLogHeap("after wifi recycle");
            return true;
        }
        delay(120);
    }

    otaWorkerLogHeap("wifi recycle failed");
    return false;
}

// Keeps the station link associated whenever the WiFi master switch is on and
// credentials exist, so the device always holds an IP — independent of the web
// config portal or the MQTT bridge. Non-blocking: kicks WiFi.begin() at most once
// per interval and returns immediately. Coexists with the AP (AP_STA) so the web
// portal keeps working alongside it.
static uint32_t s_wifiStaKickMs = 0;
static void serviceWifiStation(uint32_t now) {
    if (!s_cfg.wifiEnabled || !wifiHasActiveCreds()) return;
    if (WiFi.status() == WL_CONNECTED) return;
    if (s_wifiStaKickMs != 0 && (now - s_wifiStaKickMs) < 10000UL) return;
    s_wifiStaKickMs = now;

    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_AP)       WiFi.mode(WIFI_AP_STA);
    else if (mode == WIFI_OFF) WiFi.mode(WIFI_STA);
    wifiBeginActiveKnown();
}

static bool runOtaWorkerModeIfRequested() {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    if (isOtaWorkerModeRequestedRtc() || isOtaWorkerModeRequestedOnce()) {
        (void)consumeOtaWorkerModeRtcOnce();
        (void)consumeOtaWorkerModeOnce();
        otaSetNetworkAllowed(false);
        Serial.println("[ota-worker] disabled on cardputer build");
    }
    return false;
#else
    bool rtcRequested = isOtaWorkerModeRequestedRtc();
    bool nvsRequested = isOtaWorkerModeRequestedOnce();
    if (!(rtcRequested || nvsRequested)) return false;

    auto clearWorkerRequestFlags = []() {
        (void)consumeOtaWorkerModeRtcOnce();
        (void)consumeOtaWorkerModeOnce();
    };

    otaSetNetworkAllowed(true);

    Serial.printf("[ota-worker] one-shot mode requested (rtc=%d nvs=%d)\n",
                  rtcRequested ? 1 : 0,
                  nvsRequested ? 1 : 0);
    otaWorkerDrawStatus("OTA minimal mode", "Preparing network...");

    if (!otaWorkerEnsureWifiConnected()) {
        Serial.println("[ota-worker] wifi unavailable");
        otaWorkerDrawStatus("OTA minimal mode", "WiFi not connected");
        setOtaWorkerBootNotice("OTA check failed: WiFi not connected");
        delay(1800);
        otaSetNetworkAllowed(false);
        clearWorkerRequestFlags();
        return true;
    }

    otaWorkerDrawStatus("OTA minimal mode", "Checking latest release...");
    OtaCheckResult check = {};
    bool checkOk = otaCheckLatestRelease(check) && check.ok;
    if (!checkOk && otaWorkerErrIsTlsLowMem(check.error)) {
        Serial.println("[ota-worker] low-mem TLS on check; retrying after WiFi recycle");
        otaWorkerDrawStatus("OTA minimal mode", "Retrying check...");
        if (otaWorkerReconnectWifiForLowMemRetry()) {
            memset(&check, 0, sizeof(check));
            checkOk = otaCheckLatestRelease(check) && check.ok;
        }
    }
    if (!checkOk) {
        Serial.printf("[ota-worker] check failed: %s\n", check.error);
        otaWorkerDrawStatus("OTA check failed", check.error[0] ? check.error : "unknown");
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        snprintf(notice,
                 sizeof(notice),
                 "OTA check failed: %s",
                 check.error[0] ? check.error : "unknown");
        setOtaWorkerBootNotice(notice);
        delay(1800);
        otaSetNetworkAllowed(false);
        clearWorkerRequestFlags();
        return true;
    }

    if (!check.updateAvailable) {
        Serial.printf("[ota-worker] up to date: %s\n", check.latestTag);
        otaWorkerDrawStatus("Already up to date",
                            check.latestTag[0] ? check.latestTag : APP_VERSION);
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        snprintf(notice,
                 sizeof(notice),
                 "Firmware already up to date (%s)",
                 check.latestTag[0] ? check.latestTag : APP_VERSION);
        setOtaWorkerBootNotice(notice);
        delay(1400);
        otaSetNetworkAllowed(false);
        clearWorkerRequestFlags();
        return true;
    }

    otaWorkerDrawStatus("Installing update", check.latestTag[0] ? check.latestTag : "latest");
    static volatile size_t s_otaWorkerBytesWritten = 0;
    static volatile size_t s_otaWorkerBytesTotal = 0;
    static volatile uint32_t s_otaWorkerLastProgressMs = 0;
    static volatile size_t s_otaWorkerLastAdvancedBytes = 0;
    static uint32_t s_otaWorkerLastDrawMs = 0;
    auto onOtaProgress = [](size_t written, size_t total) {
        const uint32_t now = millis();
        if (written != s_otaWorkerLastAdvancedBytes) {
            s_otaWorkerLastAdvancedBytes = written;
            s_otaWorkerLastProgressMs = now;
        }
        s_otaWorkerBytesWritten = written;
        s_otaWorkerBytesTotal = total;

        const bool stalled = (s_otaWorkerLastProgressMs != 0)
                          && ((uint32_t)(now - s_otaWorkerLastProgressMs) > 3000UL);
        if ((uint32_t)(now - s_otaWorkerLastDrawMs) >= 180UL || stalled) {
            otaWorkerDrawProgress("Installing update",
                                  stalled ? "No progress for 3s..." : "Downloading...",
                                  written,
                                  total,
                                  stalled);
            s_otaWorkerLastDrawMs = now;
        }
    };

    s_otaWorkerBytesWritten = 0;
    s_otaWorkerBytesTotal = 0;
    s_otaWorkerLastAdvancedBytes = 0;
    s_otaWorkerLastProgressMs = millis();
    s_otaWorkerLastDrawMs = 0;
    otaWorkerDrawProgress("Installing update",
                          "Starting download...",
                          0,
                          0,
                          false);

    char err[160] = {};
    bool installOk = otaInstallLatestRelease(check.latestTag[0] ? check.latestTag : nullptr,
                                             err,
                                             sizeof(err),
                                             onOtaProgress);
    if (!installOk && otaWorkerErrIsTlsLowMem(err)) {
        Serial.println("[ota-worker] low-mem TLS on install; retrying after WiFi recycle");
        otaWorkerDrawProgress("Installing update",
                              "Retrying download...",
                              (size_t)s_otaWorkerBytesWritten,
                              (size_t)s_otaWorkerBytesTotal,
                              true);
        if (otaWorkerReconnectWifiForLowMemRetry()) {
            err[0] = '\0';
            installOk = otaInstallLatestRelease(check.latestTag[0] ? check.latestTag : nullptr,
                                                err,
                                                sizeof(err),
                                                onOtaProgress);
        }
    }

    if (installOk) {
        Serial.println("[ota-worker] install complete, rebooting");
        otaWorkerDrawStatus("OTA installed", "Rebooting...");
        delay(700);
        otaSetNetworkAllowed(false);
        clearWorkerRequestFlags();
        ESP.restart();
        return true;
    }

    // Final refresh with last known transfer counters for context on failure.
    {
        size_t curW = (size_t)s_otaWorkerBytesWritten;
        size_t curT = (size_t)s_otaWorkerBytesTotal;
        otaWorkerDrawProgress("OTA install failed",
                              err[0] ? err : "unknown",
                              curW,
                              curT,
                              true);
    }

    Serial.printf("[ota-worker] install failed: %s\n", err);
    {
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        snprintf(notice,
                 sizeof(notice),
                 "OTA install failed: %s",
                 err[0] ? err : "unknown");
        setOtaWorkerBootNotice(notice);
    }
    delay(2200);
    otaSetNetworkAllowed(false);
    clearWorkerRequestFlags();
    return true;
#endif
}

static void setPagerKeyboardBacklight(bool on) {
#if defined(DEVICE_TLORA_PAGER_TFT) && defined(KB_BL) && (KB_BL >= 0)
    digitalWrite(KB_BL, on ? HIGH : LOW);
#else
    LV_UNUSED(on);
#endif
}

static void sleepScreen(const char *reason) {
    if (s_screenAsleep) return;

    displayDev().setBrightness(0);
    setPagerKeyboardBacklight(false);
    s_screenAsleep = true;
    s_screenWakeBlockedUntilMs = millis() + kScreenWakeInputDelayMs;

    if (reason && reason[0]) {
        Serial.printf("[screen] sleeping (%s)\n", reason);
    } else {
        Serial.println("[screen] sleeping");
    }
}

static void wakeScreen() {
    if (!s_screenAsleep) {
        s_lastActivityMs = millis();
        return;
    }

    displayDev().setBrightness(TFT_BRIGHTNESS_DEFAULT);
    setPagerKeyboardBacklight(true);
    s_screenAsleep = false;
    s_lastActivityMs = millis();

    // Force repaint after wake so stale text/colors are not shown.
    s_lastRenderedChannel = -1;
    s_lastRenderedCount = -1;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
    s_lastHeaderTime[0] = '\0';
    s_lastBattPct = 255;
    s_lastGpsSats = 255;
    Serial.println("[screen] woke");
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
            && (uint32_t)(nowMs - s_tdeckTrackballHoldStartMs) >= kTdeckTrackballSleepHoldMs) {
            s_tdeckTrackballHoldTriggered = true;
            // Ignore any pending click event from this same press.
            s_tdeckSuppressRollerClick = true;
            if (!s_screenAsleep) {
                sleepScreen("T-Deck trackball hold");
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

static bool pollUserButton(uint32_t nowMs) {
#if defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)
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
            if (s_cfgModal) {
                activateCfgSelection();
                return true;
            }
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
#else
            if (s_screenAsleep) {
                if (!tryWakeScreenFromInput(nowMs)) {
                    return true;
                }
            } else {
                sleepScreen("BOOT button");
            }
            return true;
#endif
        }
    }

    if (userBtnStable && s_screenAsleep) {
        if (!tryWakeScreenFromInput(nowMs)) {
            return true;
        }
        return true;
    }
#endif

#if defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)
    static bool displayBtnRawPrev = false;
    static bool displayBtnStable = false;
    static uint32_t displayBtnDebounceMs = 0;

    bool displayPressed = (digitalRead(DISPLAY_TOGGLE_BUTTON_PIN) == DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL);
    if (displayPressed != displayBtnRawPrev) {
        displayBtnRawPrev = displayPressed;
        displayBtnDebounceMs = nowMs;
    }

    if ((nowMs - displayBtnDebounceMs) >= 30 && displayPressed != displayBtnStable) {
        displayBtnStable = displayPressed;
        if (displayBtnStable) {
            if (s_screenAsleep) {
                if (!tryWakeScreenFromInput(nowMs)) {
                    return true;
                }
            } else {
                sleepScreen("GPIO35 button");
            }
            return true;
        }
    }

    if (displayBtnStable && s_screenAsleep) {
        if (!tryWakeScreenFromInput(nowMs)) {
            return true;
        }
        return true;
    }
#endif

#if !((defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)) || (defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)))
    LV_UNUSED(nowMs);
#endif

    return false;
}

static void persistConfigToPrefs() {
    const char *wifiSsid = webCfgWifiSsid();
    const char *wifiPass = webCfgWifiPass();
    if ((!wifiSsid || !wifiSsid[0]) && s_cfg.wifiSsid[0]) wifiSsid = s_cfg.wifiSsid;
    if ((!wifiPass || !wifiPass[0]) && s_cfg.wifiPass[0]) wifiPass = s_cfg.wifiPass;

    Preferences p;
    if (!p.begin("camillia", false)) return;

    if (wifiSsid && wifiSsid[0]) p.putString("wifiSsid", wifiSsid);
    if (wifiPass && wifiPass[0]) p.putString("wifiPass", wifiPass);
    if (s_cfg.webCfgPass[0]) p.putString("webPass", s_cfg.webCfgPass);
    p.putBool("webCfgAuth", s_cfg.webCfgAuthEnabled);
    p.putString("nodeLong", s_cfg.nodeLong);
    p.putString("nodeShort", s_cfg.nodeShort);
    p.putUChar("modemPreset", s_cfg.modemPreset);
    p.putFloat("loraFreq", s_cfg.loraFreq);
    p.putFloat("loraBw", s_cfg.loraBw);
    p.putUChar("loraSf", s_cfg.loraSf);
    p.putUChar("loraCr", s_cfg.loraCr);
    p.putUChar("loraPower", s_cfg.loraPower);
    p.putUChar("loraHopLim", s_cfg.loraHopLimit);
    p.putBool("gpsEnabled", s_cfg.gpsEnabled);
    p.putInt("latI", s_cfg.latI);
    p.putInt("lonI", s_cfg.lonI);
    p.putInt("alt", s_cfg.alt);
    p.putUChar("devRole", s_cfg.deviceRole);
    p.putUChar("rebroadcast", s_cfg.rebroadcastMode);
    p.putBool("okToMqtt", s_cfg.okToMqtt);
    p.putBool("ignoreMqtt", s_cfg.ignoreMqtt);
    p.putULong("nodeInfoIntv", s_cfg.nodeInfoIntervalS);
    p.putULong("posIntv", s_cfg.posIntervalS);
    p.putULong("gpsPollS", s_cfg.gpsPollIntervalS);
    p.putString("region", s_cfg.region);
    p.putString("tzDef", s_cfg.tzDef);
    p.putString("ntpServer", s_cfg.ntpServer);
    p.putULong("screenOnSecs", s_cfg.screenOnSecs);
    p.putUChar("dispUnits", s_cfg.displayUnits);
    p.putUChar("chatStyle", s_cfg.chatStyle);
    p.putUChar("chatNameSty", s_cfg.chatNameStyle);
    p.putBool("chatColors", s_cfg.chatColorsEnabled);
    p.putUChar("userMsgColor", s_cfg.userMsgColor);
    p.putBool("compassNorth", s_cfg.compassNorthTop);
    p.putBool("flipScreen", s_cfg.flipScreen);
    p.putBool("splashMelody", s_cfg.splashMelodyEnabled);
    p.putUChar("msgAlertSound", s_cfg.msgAlertSound);
    p.putUChar("uiTheme", s_cfg.uiTheme);
    p.putUChar("uiMode", s_cfg.uiMode);
    p.putBool("btEnabled", s_cfg.btEnabled);
    p.putUChar("btMode", s_cfg.btMode);
    p.putULong("btFixedPin", s_cfg.btFixedPin);
    p.putBool("wifiEnabled", s_cfg.wifiEnabled);
    p.putBool("mqttEnabled", s_cfg.mqttEnabled);
    p.putString("mqttServer", s_cfg.mqttServer);
    p.putString("mqttUser", s_cfg.mqttUser);
    p.putString("mqttPass", s_cfg.mqttPass);
    p.putString("mqttRoot", s_cfg.mqttRoot);
    p.putBool("mqttEncrypt", s_cfg.mqttEncryption);
    p.putBool("mqttMapRpt", s_cfg.mqttMapReport);
    p.putUShort("mqttPort", s_cfg.mqttPort);
    p.putBool("mqttTls", s_cfg.mqttTls);
    p.putBool("isPwrSaving", s_cfg.isPowerSaving);
    p.putULong("lsSecs", s_cfg.lsSecs);
    p.putULong("minWakeSecs", s_cfg.minWakeSecs);
    p.putBool("telDevEn", s_cfg.telDeviceEnabled);
    p.putULong("telDevIntv", s_cfg.telDeviceIntervalS);
    p.putBool("telEnvEn", s_cfg.telEnvEnabled);
    p.putULong("telEnvIntv", s_cfg.telEnvIntervalS);
    p.putBool("nbrInfoEn", s_cfg.neighborInfoEnabled);
    p.putULong("nbrInfoIntv", s_cfg.neighborInfoIntervalS);
    p.putBool("nbrInfoLoRa", s_cfg.neighborInfoOverLora);
    p.putBool("cannedEn", s_cfg.cannedEnabled);
    p.putString("cannedMsgs", s_cfg.cannedMessages);
    p.putBool("snfClientEn", s_cfg.snfClientEnabled);
    p.putBool("otaAutoChk", s_cfg.otaAutoCheckEnabled);
    p.putBool("nodeArchive", s_cfg.nodeArchiveEnabled);
    p.putBool("autoFav", s_cfg.autoFavoriteEnabled);
    p.putULong("autoFavRange", s_cfg.autoFavoriteRangeM);
    p.putULong("nodeIdOvr", s_cfg.nodeIdOverride);
    p.putUChar("chatSpace", s_cfg.chatSpacing);
    p.putUChar("fontSize", s_cfg.fontSize);
    p.putBool("dbgAcks", s_cfg.debugAcks);
    p.putBool("dbgMsgs", s_cfg.debugMessages);
    p.putBool("dbgGps", s_cfg.debugGps);
    p.putBool("webCfgEnabled", s_webCfgEnabled);
    p.end();
}

static void persistChannelsToPrefs() {
    Preferences cp;
    if (!cp.begin("mesh_ch", false)) return;

    for (int i = 0; i < MESH_CHANNELS; i++) {
        const char *name = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
        char key[8];
        snprintf(key, sizeof(key), "n%d", i);
        cp.putString(key, name ? name : "");
        snprintf(key, sizeof(key), "k%d", i);
        cp.putBytes(key, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
        snprintf(key, sizeof(key), "r%d", i);
        cp.putUChar(key, CHANNEL_KEYS[i].role);
        snprintf(key, sizeof(key), "u%d", i);
        cp.putBool(key, CHANNEL_KEYS[i].uplinkEnabled);
        snprintf(key, sizeof(key), "d%d", i);
        cp.putBool(key, CHANNEL_KEYS[i].downlinkEnabled);
        snprintf(key, sizeof(key), "m%d", i);
        cp.putBool(key, CHANNEL_KEYS[i].muted);
    }

    cp.end();
}

static void loadConfigFromPrefs() {
    Preferences prefs;
    if (!prefs.begin("camillia", true)) {
        // Read-only open fails when the namespace has never been written
        // (i.e. a freshly flashed device with a blank NVS partition). Treat
        // that as first boot so onboarding fires before anything else opens
        // the namespace read-write and creates it.
        s_firstBoot = true;
        return;
    }

    // First-boot detection: a freshly-flashed device has no persisted node
    // identity. The presence of a saved nodeLong key is our marker that
    // onboarding has already been completed at least once.
    s_firstBoot = !prefs.isKey("nodeLong");

    auto getStringIfKey = [&](const char *key) -> String {
        return prefs.isKey(key) ? prefs.getString(key, "") : String();
    };
    auto getFloatIfKey = [&](const char *key, float defaultVal) -> float {
        return prefs.isKey(key) ? prefs.getFloat(key, defaultVal) : defaultVal;
    };

    String nodeLong = getStringIfKey("nodeLong");
    if (nodeLong.length()) {
        utf8util::copyTruncate(s_cfg.nodeLong, sizeof(s_cfg.nodeLong), nodeLong.c_str());
    }
    String nodeShort = getStringIfKey("nodeShort");
    if (nodeShort.length()) {
        utf8util::copyTruncate(s_cfg.nodeShort, sizeof(s_cfg.nodeShort), nodeShort.c_str());
    }

    float f = getFloatIfKey("loraFreq", 0.0f);
    if (f > 0.0f) s_cfg.loraFreq = f;
    f = getFloatIfKey("loraBw", 0.0f);
    if (f > 0.0f) s_cfg.loraBw = f;

    uint8_t u = prefs.getUChar("loraSf", 0);
    if (u) s_cfg.loraSf = u;
    u = prefs.getUChar("loraCr", 0);
    if (u) s_cfg.loraCr = u;
    if (s_cfg.loraCr == 8 && s_cfg.loraSf == 11 && s_cfg.loraBw == 250.0f) {
        s_cfg.loraCr = 5;
    }
    u = prefs.getUChar("loraPower", 0);
    if (u) s_cfg.loraPower = u;
    u = prefs.getUChar("loraHopLim", 0);
    if (u) s_cfg.loraHopLimit = u;
    u = prefs.getUChar("modemPreset", PRESET_COUNT);
    if (u < PRESET_COUNT) s_cfg.modemPreset = u;

    if (prefs.isKey("gpsEnabled")) s_cfg.gpsEnabled = prefs.getBool("gpsEnabled");
    int32_t i = prefs.getInt("latI", 0);
    if (i) s_cfg.latI = i;
    i = prefs.getInt("lonI", 0);
    if (i) s_cfg.lonI = i;
    i = prefs.getInt("alt", -1);
    if (i >= 0) s_cfg.alt = i;

    uint8_t ro = prefs.getUChar("devRole", 0xFF);
    if (ro != 0xFF) s_cfg.deviceRole = cfgCoerceClientRole(ro);
    ro = prefs.getUChar("rebroadcast", 0xFF);
    if (ro != 0xFF) s_cfg.rebroadcastMode = ro;

    if (prefs.isKey("okToMqtt")) s_cfg.okToMqtt = prefs.getBool("okToMqtt");
    if (prefs.isKey("ignoreMqtt")) s_cfg.ignoreMqtt = prefs.getBool("ignoreMqtt");

    uint32_t ul = prefs.getULong("nodeInfoIntv", 0);
    if (ul) s_cfg.nodeInfoIntervalS = ul;
    ul = prefs.getULong("posIntv", 0);
    if (ul) s_cfg.posIntervalS = ul;
    if (prefs.isKey("gpsPollS")) {
        ul = prefs.getULong("gpsPollS", 0);
        s_cfg.gpsPollIntervalS = (uint32_t)constrain((long)ul, (long)0, (long)3600);
    } else if (prefs.isKey("gpsPollMs")) {
        ul = prefs.getULong("gpsPollMs", 0);
        s_cfg.gpsPollIntervalS = (ul == 0) ? 0 : (uint32_t)constrain((long)((ul + 999UL) / 1000UL), (long)0, (long)3600);
    }

    String region = getStringIfKey("region");
    if (region.length()) {
        strncpy(s_cfg.region, region.c_str(), sizeof(s_cfg.region) - 1);
        s_cfg.region[sizeof(s_cfg.region) - 1] = '\0';
    }
    String tz = getStringIfKey("tzDef");
    if (tz.length()) {
        strncpy(s_cfg.tzDef, tz.c_str(), sizeof(s_cfg.tzDef) - 1);
        s_cfg.tzDef[sizeof(s_cfg.tzDef) - 1] = '\0';
    }
    String ntp = getStringIfKey("ntpServer");
    if (ntp.length()) {
        strncpy(s_cfg.ntpServer, ntp.c_str(), sizeof(s_cfg.ntpServer) - 1);
        s_cfg.ntpServer[sizeof(s_cfg.ntpServer) - 1] = '\0';
    }

    ul = prefs.getULong("screenOnSecs", 0);
    if (ul) s_cfg.screenOnSecs = ul;

    ro = prefs.getUChar("dispUnits", 0xFF);
    if (ro != 0xFF) s_cfg.displayUnits = ro;
    ro = prefs.getUChar("chatStyle", 0xFF);
    if (ro != 0xFF && ro <= CHAT_STYLE_MAX) s_cfg.chatStyle = ro;
    ro = prefs.getUChar("chatNameSty", 0xFF);
    if (ro != 0xFF && ro <= CHAT_NAME_MAX) s_cfg.chatNameStyle = ro;
    if (prefs.isKey("chatColors")) s_cfg.chatColorsEnabled = prefs.getBool("chatColors");
    if (prefs.isKey("userMsgColor")) {
        uint8_t umc = prefs.getUChar("userMsgColor", 0xFF);
        s_cfg.userMsgColor = (umc <= 15) ? umc : 0xFF;
    }
    if (prefs.isKey("compassNorth")) s_cfg.compassNorthTop = prefs.getBool("compassNorth");
    if (prefs.isKey("flipScreen")) s_cfg.flipScreen = prefs.getBool("flipScreen");
    if (prefs.isKey("splashMelody")) s_cfg.splashMelodyEnabled = prefs.getBool("splashMelody");
    if (prefs.isKey("msgAlertSound")) {
        s_cfg.msgAlertSound = (uint8_t)constrain((int)prefs.getUChar("msgAlertSound"), 0, 3);
    } else if (prefs.isKey("msgAlertBeep")) {
        s_cfg.msgAlertSound = prefs.getBool("msgAlertBeep") ? MSG_ALERT_SOUND_DEFAULT : MSG_ALERT_SOUND_OFF;
    }
    ro = prefs.getUChar("uiTheme", 0xFF);
    if (ro != 0xFF && ro < UI_THEME_COUNT) s_cfg.uiTheme = ro;
    ro = prefs.getUChar("uiMode", 0xFF);
    if (ro != 0xFF && ro <= UI_MODE_LIGHT) s_cfg.uiMode = ro;

    if (prefs.isKey("btEnabled")) s_cfg.btEnabled = prefs.getBool("btEnabled");
    ro = prefs.getUChar("btMode", 0xFF);
    if (ro != 0xFF) s_cfg.btMode = ro;
    ul = prefs.getULong("btFixedPin", 0);
    if (ul) s_cfg.btFixedPin = ul;

    s_cfg.wifiEnabled = prefs.getBool("wifiEnabled", s_cfg.wifiEnabled);
    if (prefs.isKey("mqttEnabled")) s_cfg.mqttEnabled = prefs.getBool("mqttEnabled");
    String mqttServer = getStringIfKey("mqttServer");
    if (mqttServer.length()) {
        strncpy(s_cfg.mqttServer, mqttServer.c_str(), sizeof(s_cfg.mqttServer) - 1);
        s_cfg.mqttServer[sizeof(s_cfg.mqttServer) - 1] = '\0';
    }
    String mqttUser = getStringIfKey("mqttUser");
    if (mqttUser.length()) {
        strncpy(s_cfg.mqttUser, mqttUser.c_str(), sizeof(s_cfg.mqttUser) - 1);
        s_cfg.mqttUser[sizeof(s_cfg.mqttUser) - 1] = '\0';
    }
    String mqttPass = getStringIfKey("mqttPass");
    if (mqttPass.length()) {
        strncpy(s_cfg.mqttPass, mqttPass.c_str(), sizeof(s_cfg.mqttPass) - 1);
        s_cfg.mqttPass[sizeof(s_cfg.mqttPass) - 1] = '\0';
    }
    String mqttRoot = getStringIfKey("mqttRoot");
    if (mqttRoot.length()) {
        strncpy(s_cfg.mqttRoot, mqttRoot.c_str(), sizeof(s_cfg.mqttRoot) - 1);
        s_cfg.mqttRoot[sizeof(s_cfg.mqttRoot) - 1] = '\0';
    }
    if (prefs.isKey("mqttEncrypt")) s_cfg.mqttEncryption = prefs.getBool("mqttEncrypt");
    if (prefs.isKey("mqttMapRpt")) s_cfg.mqttMapReport = prefs.getBool("mqttMapRpt");
    if (prefs.isKey("mqttPort")) s_cfg.mqttPort = prefs.getUShort("mqttPort");
    if (prefs.isKey("mqttTls")) s_cfg.mqttTls = prefs.getBool("mqttTls");

    if (prefs.isKey("isPwrSaving")) s_cfg.isPowerSaving = prefs.getBool("isPwrSaving");
    ul = prefs.getULong("lsSecs", 0);
    if (ul) s_cfg.lsSecs = ul;
    ul = prefs.getULong("minWakeSecs", 0);
    if (ul) s_cfg.minWakeSecs = ul;

    if (prefs.isKey("telDevEn")) s_cfg.telDeviceEnabled = prefs.getBool("telDevEn");
    ul = prefs.getULong("telDevIntv", 0);
    if (ul) s_cfg.telDeviceIntervalS = ul;
    if (prefs.isKey("telEnvEn")) s_cfg.telEnvEnabled = prefs.getBool("telEnvEn");
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Temporary rollback: keep env telemetry disabled on Heltec until sensor
    // wiring/model detection is finalized. Overrides any saved preference.
    s_cfg.telEnvEnabled = false;
#endif
    ul = prefs.getULong("telEnvIntv", 0);
    if (ul) s_cfg.telEnvIntervalS = ul;
    if (prefs.isKey("nbrInfoEn")) s_cfg.neighborInfoEnabled = prefs.getBool("nbrInfoEn");
    ul = prefs.getULong("nbrInfoIntv", 0);
    if (ul) s_cfg.neighborInfoIntervalS = ul;
    if (prefs.isKey("nbrInfoLoRa")) s_cfg.neighborInfoOverLora = prefs.getBool("nbrInfoLoRa");
    if (s_cfg.telDeviceIntervalS < 3600UL) s_cfg.telDeviceIntervalS = 3600UL;
    if (s_cfg.telEnvIntervalS < 3600UL) s_cfg.telEnvIntervalS = 3600UL;
    if (s_cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        s_cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }

    if (prefs.isKey("cannedEn")) s_cfg.cannedEnabled = prefs.getBool("cannedEn");
    if (prefs.isKey("snfClientEn")) s_cfg.snfClientEnabled = prefs.getBool("snfClientEn");
    if (prefs.isKey("otaAutoChk")) s_cfg.otaAutoCheckEnabled = prefs.getBool("otaAutoChk");
    if (prefs.isKey("nodeArchive")) s_cfg.nodeArchiveEnabled = prefs.getBool("nodeArchive");
    if (prefs.isKey("autoFav")) s_cfg.autoFavoriteEnabled = prefs.getBool("autoFav");
    if (prefs.isKey("autoFavRange")) {
        uint32_t r = prefs.getULong("autoFavRange", MY_AUTOFAV_RANGE_M);
        if (r > 0) s_cfg.autoFavoriteRangeM = r;
    }
    String canned = getStringIfKey("cannedMsgs");
    if (canned.length()) {
        strncpy(s_cfg.cannedMessages, canned.c_str(), sizeof(s_cfg.cannedMessages) - 1);
        s_cfg.cannedMessages[sizeof(s_cfg.cannedMessages) - 1] = '\0';
    }

    ul = prefs.getULong("nodeIdOvr", 0);
    if (ul) s_cfg.nodeIdOverride = (uint32_t)ul;
    ro = prefs.getUChar("chatSpace", 0xFF);
    if (ro != 0xFF && ro <= 2) s_cfg.chatSpacing = ro;
    ro = prefs.getUChar("fontSize", 0xFF);
    if (ro != 0xFF && ro <= FONT_SIZE_MAX) s_cfg.fontSize = ro;

    if (prefs.isKey("dbgAcks")) s_cfg.debugAcks = prefs.getBool("dbgAcks");
    if (prefs.isKey("dbgMsgs")) s_cfg.debugMessages = prefs.getBool("dbgMsgs");
    if (prefs.isKey("dbgGps")) s_cfg.debugGps = prefs.getBool("dbgGps");

    bool debugMonitor = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;
    s_cfg.debugAcks = debugMonitor;
    s_cfg.debugMessages = debugMonitor;
    s_cfg.debugGps = debugMonitor;
    s_cfgDebugLog = debugMonitor;
    debugSetFlags(debugMonitor, debugMonitor, debugMonitor);

    String wifiSsid = getStringIfKey("wifiSsid");
    if (wifiSsid.length()) {
        strncpy(s_cfg.wifiSsid, wifiSsid.c_str(), sizeof(s_cfg.wifiSsid) - 1);
        s_cfg.wifiSsid[sizeof(s_cfg.wifiSsid) - 1] = '\0';
    }
    String wifiPass = getStringIfKey("wifiPass");
    if (wifiPass.length()) {
        strncpy(s_cfg.wifiPass, wifiPass.c_str(), sizeof(s_cfg.wifiPass) - 1);
        s_cfg.wifiPass[sizeof(s_cfg.wifiPass) - 1] = '\0';
    }
    s_cfg.webCfgAuthEnabled = prefs.getBool("webCfgAuth", s_cfg.webCfgAuthEnabled);
    s_webCfgEnabled = prefs.getBool("webCfgEnabled", false);

    // Enforce network-option invariants regardless of how the flags were set
    // (on-device toggles or web config): MQTT needs WiFi, and the MQTT bridge and
    // the web-config portal are mutually exclusive.
    if (!s_cfg.wifiEnabled) {
        s_cfg.mqttEnabled = false;
        s_webCfgEnabled   = false;
    }
    if (s_cfg.mqttEnabled) {
        s_webCfgEnabled = false;
    }

    prefs.end();

    // Re-derive freq/BW/SF/CR from region + preset so they're always consistent.
    applyPresetParams(s_cfg);
}

static void loadChannelsFromPrefs() {
    Preferences cp;
    // Open read-write so first boot can create namespace instead of logging NOT_FOUND.
    if (!cp.begin("mesh_ch", false)) return;

    for (int i = 0; i < MESH_CHANNELS; i++) {
        char key[8];

        snprintf(key, sizeof(key), "n%d", i);
        if (cp.isKey(key)) {
            String name = cp.getString(key, "");
            name.trim();
            if (name.length() > 0 && name.length() < sizeof(CHANNEL_KEYS[i].name_buf)) {
                strncpy(CHANNEL_KEYS[i].name_buf, name.c_str(), sizeof(CHANNEL_KEYS[i].name_buf) - 1);
                CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
                CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
            }
        }

        snprintf(key, sizeof(key), "k%d", i);
        if (cp.isKey(key)) {
            uint8_t keyBuf[32];
            size_t keyLen = cp.getBytes(key, keyBuf, sizeof(keyBuf));
            if (keyLen > 0) {
                memcpy(CHANNEL_KEYS[i].key, keyBuf, keyLen);
                CHANNEL_KEYS[i].keyLen = (uint8_t)keyLen;
            }
        }

        snprintf(key, sizeof(key), "r%d", i);
        if (cp.isKey(key)) {
            uint8_t role = cp.getUChar(key, 0xFF);
            if (role != 0xFF) CHANNEL_KEYS[i].role = role;
        }

        snprintf(key, sizeof(key), "u%d", i);
        if (cp.isKey(key)) CHANNEL_KEYS[i].uplinkEnabled = cp.getBool(key);
        snprintf(key, sizeof(key), "d%d", i);
        if (cp.isKey(key)) CHANNEL_KEYS[i].downlinkEnabled = cp.getBool(key);
        snprintf(key, sizeof(key), "m%d", i);
        if (cp.isKey(key)) CHANNEL_KEYS[i].muted = cp.getBool(key);
    }

    cp.end();
}

static bool loadPkiPairFromPrefs(Preferences &prefs, const char *pubKeyName,
                                 const char *privKeyName,
                                 uint8_t pubOut[32], uint8_t privOut[32]) {
    if (!pubKeyName || !privKeyName) return false;
    if (prefs.getBytesLength(pubKeyName) != 32 || prefs.getBytesLength(privKeyName) != 32) {
        return false;
    }
    return (prefs.getBytes(pubKeyName, pubOut, 32) == 32)
        && (prefs.getBytes(privKeyName, privOut, 32) == 32);
}

static void persistPkiPair(Preferences &prefs, const uint8_t pubKey[32], const uint8_t privKey[32]) {
    prefs.putBytes("pub25519", pubKey, 32);
    prefs.putBytes("priv25519", privKey, 32);
    // Keep legacy key names in sync for older builds/tools.
    prefs.putBytes("pubKey", pubKey, 32);
    prefs.putBytes("privKey", privKey, 32);
}

static void initPkiIdentity() {
    memset(myPubKey, 0, sizeof(myPubKey));
    memset(myPrivKey, 0, sizeof(myPrivKey));

    Preferences prefs;
    if (!prefs.begin("camillia", false)) {
        Serial.println("[pki] failed to open NVS namespace");
        return;
    }

    bool loaded = loadPkiPairFromPrefs(prefs, "pub25519", "priv25519", myPubKey, myPrivKey);
    if (!loaded) {
        loaded = loadPkiPairFromPrefs(prefs, "pubKey", "privKey", myPubKey, myPrivKey);
    }

    bool valid = false;
    if (loaded) {
        uint8_t derivedPub[32] = {0};
        if (Curve25519::eval(derivedPub, myPrivKey, nullptr)) {
            if (memcmp(derivedPub, myPubKey, 32) == 0) {
                valid = true;
            } else {
                uint8_t reversedPub[32];
                for (int i = 0; i < 32; i++) reversedPub[i] = myPubKey[31 - i];
                if (memcmp(derivedPub, reversedPub, 32) == 0) {
                    memcpy(myPubKey, derivedPub, 32);
                    persistPkiPair(prefs, myPubKey, myPrivKey);
                    valid = true;
                    Serial.println("[pki] corrected stored pubkey endianness");
                }
            }
        }
    }

    if (!valid) {
        Curve25519::dh1(myPubKey, myPrivKey);
        persistPkiPair(prefs, myPubKey, myPrivKey);
        Serial.println("[pki] generated new Curve25519 identity");
    }

    prefs.end();
}

static void deriveNodeId() {
    uint32_t baseNodeId = 0;
    Preferences prefs;
    if (prefs.begin("camillia", false)) {
        baseNodeId = prefs.getUInt("nodeId", 0);
        if (baseNodeId == 0) {
            uint8_t mac[6] = {};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            baseNodeId = ((uint32_t)mac[2] << 24)
                       | ((uint32_t)mac[3] << 16)
                       | ((uint32_t)mac[4] << 8)
                       | (uint32_t)mac[5];
            prefs.putUInt("nodeId", baseNodeId);
        }
        prefs.end();
    }

    s_myNodeId = (s_cfg.nodeIdOverride != 0) ? s_cfg.nodeIdOverride : baseNodeId;

    if (s_myNodeId == 0) {
        uint8_t mac[6] = {};
        if (esp_efuse_mac_get_default(mac) == ESP_OK) {
            s_myNodeId = ((uint32_t)mac[2] << 24)
                       | ((uint32_t)mac[3] << 16)
                       | ((uint32_t)mac[4] << 8)
                       | (uint32_t)mac[5];
        }
    }

    Serial.printf("[lvgl] Node ID: !%08x\n", s_myNodeId);
    initPkiIdentity();
}

static void recomputeChannelHashes() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        const char *name = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
        CHANNEL_KEYS[i].hash = computeChannelHash(name, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
    }
}

// Keeps channel 0's name in sync with the active modem preset.
static void syncPrimaryChannelName() {
    uint8_t pi = s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : 0;
    const char *pname = kPresets[pi].channelName;
    strncpy(CHANNEL_KEYS[0].name_buf, pname, sizeof(CHANNEL_KEYS[0].name_buf) - 1);
    CHANNEL_KEYS[0].name_buf[sizeof(CHANNEL_KEYS[0].name_buf) - 1] = '\0';
    CHANNEL_KEYS[0].name = CHANNEL_KEYS[0].name_buf;
}

static void formatReplyPreview(const char *src, char *dst, size_t dstLen) {
    if (!dst || dstLen == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src == ' ') src++;
    utf8util::copyTruncate(dst, dstLen, src);
    for (size_t i = 0; dst[i]; i++) {
        if (dst[i] == '\r' || dst[i] == '\n') dst[i] = ' ';
    }
}

static uint32_t resolveReplyPacketId(const DisplayLine *const *rows, int rowCount, int rowIdx) {
    if (!rows || rowIdx < 0 || rowIdx >= rowCount || !rows[rowIdx]) return 0;

    uint32_t replyId = rows[rowIdx]->packetId;
    if (replyId != 0) return replyId;

    const char *txt = rows[rowIdx]->text;
    if (!(txt[0] == ' ' && txt[1] == ' ')) return 0;

    for (int j = rowIdx - 1; j >= 0; --j) {
        if (!rows[j]) break;
        if (rows[j]->packetId) {
            replyId = rows[j]->packetId;
            break;
        }
        const char *prev = rows[j]->text;
        if (!(prev[0] == ' ' && prev[1] == ' ')) break;
    }
    return replyId;
}

static void setSelectedReplyContext(uint32_t replyPacketId, const char *fallbackText) {
    s_selectedMsgReplyPacketId = replyPacketId;
    s_selectedMsgText[0] = '\0';

    if (replyPacketId != 0) {
        bool collecting = false;
        size_t w = 0;
        for (int row = 0; row < MAX_MSG_LINES; row++) {
            const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
            if (!dl) break;
            if (shouldHideChatLine(dl->text)) continue;

            if (!collecting) {
                if (dl->packetId != replyPacketId) continue;
                collecting = true;
            } else if (dl->packetId != replyPacketId) {
                break;
            }

            const char *seg = dl->text;
            while (*seg == ' ') seg++;  // drop continuation indent padding
            if (!*seg) continue;

            if (w > 0 && w + 1 < sizeof(s_selectedMsgText)) {
                s_selectedMsgText[w++] = ' ';
            }

            char segBuf[MSG_CHARS + 1];
            size_t segW = 0;
            while (*seg && segW < MSG_CHARS) {
                char c = *seg++;
                if (c == '\r' || c == '\n' || c == '\t') c = ' ';
                segBuf[segW++] = c;
            }
            segBuf[segW] = '\0';

            size_t room = sizeof(s_selectedMsgText) - w;
            size_t added = utf8util::copyTruncate(s_selectedMsgText + w, room, segBuf);
            w += added;
            s_selectedMsgText[w] = '\0';

            if (w + 1 >= sizeof(s_selectedMsgText)) break;
        }
    }

    if (s_selectedMsgText[0] == '\0' && fallbackText) {
        utf8util::copyTruncate(s_selectedMsgText, sizeof(s_selectedMsgText), fallbackText);
    }

    while (s_selectedMsgText[0] == ' ') {
        memmove(s_selectedMsgText,
                s_selectedMsgText + 1,
                strlen(s_selectedMsgText) + 1);
    }
}

static void collectChatRows(const DisplayLine **rows, int &rowCount) {
    rowCount = 0;
    if (!rows) return;

    for (int row = 0; row < MAX_MSG_LINES && rowCount < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        if (shouldHideChatLine(dl->text)) continue;
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
        s_selectedMsgReplyPacketId = 0;
        s_selectedMsgText[0] = '\0';
        s_lastRenderedChannel = -1;
        return false;
    }

    if (displayIndex < 0) displayIndex = cursorCount - 1;
    if (displayIndex >= cursorCount) displayIndex = cursorCount - 1;

    s_pagerChatCursorDisplayIndex = displayIndex;
    int rowIdx = cursorOrder[displayIndex];
    uint32_t replyPacketId = resolveReplyPacketId(rows, rowCount, rowIdx);
    const char *txt = rows[rowIdx] ? rows[rowIdx]->text : "";
    setSelectedReplyContext(replyPacketId, txt);

    s_lastRenderedChannel = -1;
    return true;
}

static void pagerExitChatCursorMode(bool clearSelection) {
    s_pagerChatCursorMode = false;
    s_pagerChatCursorDisplayIndex = -1;
    if (clearSelection) {
        s_selectedMsgReplyPacketId = 0;
        s_selectedMsgText[0] = '\0';
    }
    s_lastRenderedChannel = -1;
}

static void closeComposePrompt() {
    // The emoji picker is a child of compose; never leave it orphaned.
    closeEmojiPicker();
    lvObjDeleteSafe(s_composeModal);
    s_composeInput = nullptr;
    s_composeKeyboard = nullptr;
    s_composeCharCount = nullptr;
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
constexpr int kEmojiTrayCount = (int)(sizeof(kEmojiTray) / sizeof(kEmojiTray[0]));

static void refreshEmojiPickerSelection() {
    if (!s_emojiPickerModal) return;
    lv_obj_t *grid = lv_obj_get_child(s_emojiPickerModal, 1);   // [0]=hint, [1]=grid
    if (!grid) return;
    const bool light = (s_cfg.uiMode == UI_MODE_LIGHT);
    for (int i = 0; i < kEmojiTrayCount; i++) {
        lv_obj_t *cell = lv_obj_get_child(grid, i);
        if (!cell) continue;
        const bool sel = (i == s_emojiPickerSelection);
        lv_obj_set_style_bg_opa(cell, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cell, light ? lv_color_hex(0xC7D8F5)
                                              : lv_color_hex(0x2A4E8F), 0);
        lv_obj_set_style_border_width(cell, sel ? 2 : 0, 0);
        lv_obj_set_style_border_color(cell, light ? lv_color_hex(0x3A5F9E)
                                                  : lv_color_hex(0x9BC0FF), 0);
        if (sel) lv_obj_scroll_to_view(cell, LV_ANIM_OFF);
    }
}

// Fire the picked glyph as a standalone one-emoji message to whatever screen the
// picker was opened over: the selected DM conversation when the DM screen is up,
// otherwise the active channel. There is no compose step — the picker is a
// quick-reaction affordance opened with 'e' from the chat/DM browse screen.
static void sendQuickEmoji(const char *emoji) {
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
            DMs.addMessage(dm->nodeId, nullptr, "", "! TX failed", TFT_RED, false, -1, 0);
        }
    } else {
        int txChan = (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS)
                   ? s_activeChannel : 0;
        if (!Channels.sendText(s_myNodeId, emoji, s_cfg.okToMqtt, txChan)) {
            Channels.addMessage(txChan, "", "! TX failed", TFT_RED, 0);
        }
    }
    refreshChatView(true);
    refreshDmModal(true);
}

static void emojiPickerActivate(int idx) {
    if (idx < 0 || idx >= kEmojiTrayCount) return;
    if (s_emojiPickerSendMode) {
        closeEmojiPicker();   // one-shot: tear the tray down before the send refresh
        sendQuickEmoji(kEmojiTray[idx]);
        return;
    }
    // Insert mode: append to the open compose box and keep the tray up for more.
    if (s_composeInput) {
        lv_textarea_add_text(s_composeInput, kEmojiTray[idx]);
        updateComposeCharCount();
    }
}

static void onEmojiCellPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_emojiPickerSendMode) {
        s_emojiPickerSelection = idx;
        refreshEmojiPickerSelection();
    }
    emojiPickerActivate(idx);   // send-and-close, or insert-and-stay by mode
}

static void onEmojiBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_emojiPickerBackdrop) return;
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
}

static void openEmojiPicker(bool sendMode) {
    if (!s_rootScreen || s_emojiPickerModal) return;
    s_emojiPickerSendMode = sendMode;
    s_emojiPickerSelection = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 12;
    if (modalW > 340) modalW = 340;

    // Emoji glyphs come from the fallback face; a blank base label just carries
    // the fallback, so any Montserrat size works — pick one that reads well.
    const lv_font_t *cellFont = emojiFont(&lv_font_montserrat_18);
    // Cell size follows the panel: big enough to tap on touch builds, small
    // enough that a couple of rows fit the Cardputer's 135px-tall screen.
    const int cell = (w <= 160) ? 24 : 30;

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
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(hint, sendMode ? "Tap to send • tap outside to close"
                                     : "Tap to add • tap outside to close");
#else
    lv_label_set_text_fmt(hint, sendMode ? "Move • Enter=Send • %s=Close"
                                         : "Move • Enter=Add • %s=Close",
                          modalCloseKeyLabel());
#endif

    lv_obj_t *grid = lv_obj_create(s_emojiPickerModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(grid, (h > 60) ? (h - 44) : LV_SIZE_CONTENT, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, 2, 0);

    for (int i = 0; i < kEmojiTrayCount; i++) {
        lv_obj_t *c = lv_obj_create(grid);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, cell, cell);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(c, 4, 0);
        lv_obj_add_event_cb(c, onEmojiCellPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *g = lv_label_create(c);
        lv_obj_set_style_text_font(g, cellFont, 0);
        setLabelTextEmojiSafe(g, kEmojiTray[i]);
        lv_obj_center(g);
    }

    refreshEmojiPickerSelection();
}

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
#elif defined(DEVICE_TDECK)
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 3) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_font_t *composeBodyFont = emojiFont(&lv_font_montserrat_12);
    const lv_coord_t composeInputH = (lv_coord_t)(lv_font_get_line_height(composeBodyFont) + 8);
    const lv_coord_t composeInputPadTop = max<lv_coord_t>(1, (composeInputH - (lv_coord_t)lv_font_get_line_height(composeBodyFont)) / 2);
    const lv_coord_t composeModalBottomPad = 4;
    const lv_coord_t composeModalRowPad = 1;
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

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    int modalW = lv_disp_get_hor_res(NULL) - 8;
    int modalH = lv_disp_get_ver_res(NULL) - 8;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL);
    if (modalH < 120) modalH = lv_disp_get_ver_res(NULL);

    s_composeModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_composeModal, modalW, modalH);
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_composeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_composeModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_composeModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_composeModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeModal, 1, 0);
    lv_obj_set_style_border_color(s_composeModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_composeModal, 4, 0);
    lv_obj_set_style_pad_bottom(s_composeModal, composeModalBottomPad, 0);
    lv_obj_set_style_pad_row(s_composeModal, composeModalRowPad, 0);
    lv_obj_set_flex_flow(s_composeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_composeModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_composeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, isReply ? "Reply" : "New Message");

    if (isReply) {
        char preview[kReplyPreviewTextMax + 1];
        formatReplyPreview(replyText, preview, sizeof(preview));
        lv_obj_t *replyLbl = lv_label_create(s_composeModal);
        lv_obj_set_width(replyLbl, lv_pct(100));
        lv_obj_set_height(replyLbl, lv_font_get_line_height(&lv_font_montserrat_10));
        lv_obj_set_style_text_font(replyLbl, emojiFont(&lv_font_montserrat_10), 0);
        lv_obj_set_style_text_color(replyLbl, lv_color_hex(0xA7C7FF), 0);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        setLabelTextEmojiSafe(replyLbl, preview[0] ? preview : "(message)");
    }

    s_composeInput = lv_textarea_create(s_composeModal);
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_height(s_composeInput, 44);
    lv_obj_set_style_text_font(s_composeInput, emojiFont(&lv_font_montserrat_14), 0);
    lv_obj_set_style_text_color(s_composeInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_composeInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_composeInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeInput, 1, 0);
    lv_obj_set_style_border_color(s_composeInput, lv_color_hex(0x4C76BA), 0);
    lv_textarea_set_one_line(s_composeInput, true);
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, "Type message...");
    lv_obj_add_event_cb(s_composeInput, onComposeInputChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    s_composeCharCount = lv_label_create(s_composeModal);
    lv_obj_set_width(s_composeCharCount, lv_pct(100));
    lv_obj_set_style_text_font(s_composeCharCount, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_composeCharCount, lv_color_hex(0xA7C7FF), 0);
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
    lv_obj_add_event_cb(emojiBtn, [](lv_event_t *) { openEmojiPicker(false); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_t *emojiLbl = lv_label_create(emojiBtn);
    lv_obj_set_style_text_font(emojiLbl, emojiFont(&lv_font_montserrat_16), 0);
    setLabelTextEmojiSafe(emojiLbl, "\U0001F600");
    lv_obj_center(emojiLbl);

    lv_obj_t *cancelBtn = lv_btn_create(row);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onComposeCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_center(cancelLbl);

    lv_obj_t *sendBtn = lv_btn_create(row);
    lv_obj_set_flex_grow(sendBtn, 1);
    lv_obj_set_height(sendBtn, lv_pct(100));
    lv_obj_add_event_cb(sendBtn, onComposeSendPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *sendLbl = lv_label_create(sendBtn);
    lv_obj_set_style_text_font(sendLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(sendLbl, "Send");
    lv_obj_center(sendLbl);

    s_composeKeyboard = lv_keyboard_create(s_composeModal);
    lv_obj_set_width(s_composeKeyboard, lv_pct(100));
    lv_obj_set_flex_grow(s_composeKeyboard, 1);
    lv_keyboard_set_textarea(s_composeKeyboard, s_composeInput);
    lv_obj_add_event_cb(s_composeKeyboard, onComposeKeyboardEvent, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_composeKeyboard, onComposeKeyboardEvent, LV_EVENT_CANCEL, nullptr);
#else
    int modalW = lv_disp_get_hor_res(NULL) - 24;
    if (modalW < 140) modalW = lv_disp_get_hor_res(NULL) - 8;
    int modalH = isReply ? 100 : 76;
#if defined(DEVICE_TLORA_PAGER_TFT)
    modalH = isReply ? 138 : 116;
#elif defined(DEVICE_TDECK)
    modalH = isReply ? 126 : 104;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    modalH = isReply ? 88 : 70;
#endif

    s_composeModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_composeModal, modalW, modalH);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, -12);
#else
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, 10);
#endif
    lv_obj_clear_flag(s_composeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_composeModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_composeModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_composeModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeModal, 1, 0);
    lv_obj_set_style_border_color(s_composeModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_composeModal, 4, 0);
    lv_obj_set_style_pad_bottom(s_composeModal, composeModalBottomPad, 0);
    lv_obj_set_style_pad_row(s_composeModal, composeModalRowPad, 0);
    lv_obj_set_flex_flow(s_composeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_composeModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_composeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, isReply ? "Reply" : "New Message");

    if (isReply) {
        char preview[kReplyPreviewTextMax + 1];
        formatReplyPreview(replyText, preview, sizeof(preview));
        lv_obj_t *replyBox = lv_obj_create(s_composeModal);
        lv_obj_set_width(replyBox, lv_pct(100));
        lv_obj_set_height(replyBox, composeReplyRowH);
        lv_obj_clear_flag(replyBox, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(replyBox, lv_color_hex(0x123266), 0);
        lv_obj_set_style_bg_opa(replyBox, LV_OPA_50, 0);
        lv_obj_set_style_border_width(replyBox, 1, 0);
        lv_obj_set_style_border_color(replyBox, lv_color_hex(0x335D9D), 0);
        lv_obj_set_style_pad_left(replyBox, 4, 0);
        lv_obj_set_style_pad_right(replyBox, 4, 0);
        lv_obj_set_style_pad_top(replyBox, 1, 0);
        lv_obj_set_style_pad_bottom(replyBox, 1, 0);

        lv_obj_t *replyLbl = lv_label_create(replyBox);
        lv_obj_set_width(replyLbl, lv_pct(100));
        lv_obj_set_height(replyLbl, lv_font_get_line_height(composeBodyFont));
        lv_obj_set_style_text_font(replyLbl, composeBodyFont, 0);
        lv_obj_set_style_text_color(replyLbl, lv_color_hex(0xA7C7FF), 0);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        setLabelTextEmojiSafe(replyLbl, preview[0] ? preview : "(message)");
    }

    lv_obj_t *composeInputHost = s_composeModal;
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_obj_t *composeCenterBand = lv_obj_create(s_composeModal);
    lv_obj_set_width(composeCenterBand, lv_pct(100));
#if defined(DEVICE_TDECK)
    lv_obj_set_flex_grow(composeCenterBand, 1);
#else
    // Cardputer: avoid large flex slack above/below the input box.
    lv_obj_set_height(composeCenterBand, composeInputH + 8);
#endif
    lv_obj_clear_flag(composeCenterBand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(composeCenterBand, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(composeCenterBand, 0, 0);
    lv_obj_set_style_pad_all(composeCenterBand, 0, 0);
    lv_obj_set_flex_flow(composeCenterBand, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(composeCenterBand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    composeInputHost = composeCenterBand;
#endif

    s_composeInput = lv_textarea_create(composeInputHost);
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_height(s_composeInput, composeInputH);
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_obj_set_style_min_height(s_composeInput, composeInputH, 0);
    lv_obj_set_style_max_height(s_composeInput, composeInputH, 0);
#endif
    lv_obj_set_style_text_font(s_composeInput, composeBodyFont, 0);
    lv_obj_set_style_text_color(s_composeInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_composeInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_composeInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeInput, 1, 0);
    lv_obj_set_style_border_color(s_composeInput, lv_color_hex(0x4C76BA), 0);
    lv_obj_set_style_pad_top(s_composeInput, composeInputPadTop, 0);
    lv_obj_set_style_pad_bottom(s_composeInput, 1, 0);
    lv_obj_set_style_pad_left(s_composeInput, 3, 0);
    lv_obj_set_style_pad_right(s_composeInput, 3, 0);
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    lv_textarea_set_one_line(s_composeInput, false);
#else
    lv_textarea_set_one_line(s_composeInput, true);
#endif
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, "Type message...");
    lv_obj_add_event_cb(s_composeInput, onComposeInputChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *hint = lv_label_create(s_composeModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, composeBodyFont, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_pad_top(hint, 0, 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#elif defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#else
    lv_obj_set_style_pad_bottom(hint, 1, 0);
#endif
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Emoji isn't a compose action anymore — it's the 'E' quick-send tray on the
    // chat/DM screen (see openEmojiPicker), so it's off the compose legend.
    lv_label_set_text(hint, "Enter=Send  Esc=Cancel  Bksp=Delete");
#else
    lv_label_set_text(hint, "Enter=Send  Bksp(empty)=Cancel");
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    // Keep the legend anchored near the bottom edge, independent of flex slack.
    lv_obj_add_flag(hint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(hint, modalW - 8);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 4, -composeModalBottomPad);
#endif

    // Live "x of 200" countdown overlaid at the very bottom-right, sharing the
    // bottom line with the hint legend (which is left-aligned).
    s_composeCharCount = lv_label_create(s_composeModal);
    lv_obj_add_flag(s_composeCharCount, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_text_font(s_composeCharCount, composeBodyFont, 0);
    lv_obj_set_style_text_color(s_composeCharCount, lv_color_hex(0xA7C7FF), 0);
    lv_obj_align(s_composeCharCount, LV_ALIGN_BOTTOM_RIGHT, -4, -composeModalBottomPad);
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
    uint32_t used = (txt && txt[0]) ? _lv_txt_get_encoded_length(txt) : 0;
    lv_label_set_text_fmt(s_composeCharCount, "%u of %d",
                          (unsigned)used, (int)MESH_TEXT_MAX_LEN);
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
}

static void sendComposeMessage() {
    if (!s_composeInput) return;

    const char *msg = lv_textarea_get_text(s_composeInput);
    if (!msg || !msg[0]) return;

    int txChan = (s_composeChannelIdx >= 0 && s_composeChannelIdx < MESH_CHANNELS)
               ? s_composeChannelIdx
               : s_activeChannel;
    if (txChan < 0 || txChan >= MESH_CHANNELS) txChan = 0;

    if (s_myNodeId == 0) {
        deriveNodeId();
    }
    if (s_myNodeId == 0) {
        if (s_composeTarget == COMPOSE_TARGET_DM && s_composeDmNodeId != 0) {
            DMs.addMessage(s_composeDmNodeId, nullptr, "", "! TX failed (node id)", TFT_RED,
                           false, -1, 0);
        } else {
            Channels.addMessage(txChan, "", "! TX failed (node id)", TFT_RED, 0);
        }
        closeComposePrompt();
        refreshChatView(true);
        return;
    }

    bool sentOk = false;
    if (s_composeTarget == COMPOSE_TARGET_DM && s_composeDmNodeId != 0) {
        sentOk = DMs.sendDm(s_myNodeId, s_composeDmNodeId, msg);
        if (!sentOk) {
            DMs.addMessage(s_composeDmNodeId, nullptr, "", "! TX failed", TFT_RED,
                           false, -1, 0);
        }
    } else {
        sentOk = Channels.sendText(s_myNodeId, msg, s_cfg.okToMqtt, txChan, s_composeReplyPacketId);
        if (!sentOk) {
            Channels.addMessage(txChan, "", "! TX failed", TFT_RED, 0);
        }
    }

    closeComposePrompt();
    refreshChatView(true);
    refreshDmModal(true);
}

static void initCfgActions() {
    s_cfgActionCount = 0;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WIFI_TOGGLE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHOOSE_WIFI;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEBCFG;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_MQTT_TOGGLE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_GPS_TOGGLE;
    // Keep Chat Style near the top so it's visible without deep scrolling on
    // compact config layouts (notably the Pager's split action/info screen).
    // All builds get the full set — the Cardputer's bubble/outline styles render
    // fine on its 240x135 panel.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHAT_STYLE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHAT_NAMES;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CHAT_COLORS;
    // Font Size scales the chat and DM text in every style, so every build gets
    // it — the Cardputer arguably most of all.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_FONT_SIZE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_THEME;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_OWNER_COLOR;
    // Sound settings sit with the other presentation options rather than down
    // among the mesh/module actions.
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_MSG_ALERT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SPLASH_MELODY;
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_OTA_UPDATE;
#endif
#if HAS_SD_CARD && !defined(DEVICE_HELTEC_V4_EXPANSION)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_EXPORT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_IMPORT;
#endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_UNITS;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_ANNOUNCE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_TELEMETRY;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NEIGHBOR_INFO;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SNF_CLIENT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CLEAR_MSGS;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_CLEAR_NODES;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_FACTORY_RESET;
}

static void refreshCfgPanelFocusStyles() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (!s_cfgActionList || !s_cfgInfoList) return;

    const lv_color_t activeBorder = lv_color_hex(0x8FB5E6);
    const lv_color_t inactiveBorder = lv_color_hex(0x335D9D);

    lv_obj_set_style_border_width(s_cfgActionList, s_cfgInfoPanelFocused ? 1 : 2, 0);
    lv_obj_set_style_border_color(s_cfgActionList, s_cfgInfoPanelFocused ? inactiveBorder : activeBorder, 0);

    lv_obj_set_style_border_width(s_cfgInfoList, s_cfgInfoPanelFocused ? 2 : 1, 0);
    lv_obj_set_style_border_color(s_cfgInfoList, s_cfgInfoPanelFocused ? activeBorder : inactiveBorder, 0);
#endif
}

// Fills info[] with the current node's identity/radio details and returns the
// line count. Shared by the pager's always-on info panel and the (I)nformation
// popup on the other builds, so the two never drift apart.
// Render a node's last-heard time as a compact local timestamp. lastHeardMs is a
// millis() stamp, so it needs the wall clock to become a date; before NTP has
// set the clock we fall back to a relative age rather than printing 1970.
static void deviceInfoFormatHeard(uint32_t lastHeardMs, char *out, size_t outLen) {
    const uint32_t nowMs = millis();
    if (lastHeardMs == 0 || nowMs < lastHeardMs) {
        snprintf(out, outLen, "?");
        return;
    }
    const uint32_t ageS = (nowMs - lastHeardMs) / 1000UL;
    const time_t nowEpoch = time(nullptr);
    if (nowEpoch > 1700000000) {
        time_t t = nowEpoch - (time_t)ageS;
        struct tm lt;
        localtime_r(&t, &lt);
        snprintf(out, outLen, "%02d/%02d %02d:%02d",
                 lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    } else if (ageS < 3600UL) {
        snprintf(out, outLen, "%lum ago", (unsigned long)(ageS / 60UL));
    } else {
        snprintf(out, outLen, "%luh ago", (unsigned long)(ageS / 3600UL));
    }
}

static const char *deviceInfoNodeLabel(const NodeEntry *e, char *buf, size_t len) {
    if (liveShortNameUsable(e->shortName)) return e->shortName;
    snprintf(buf, len, "!%08lx", (unsigned long)e->nodeId);
    return buf;
}

static int buildDeviceInfoLines(char info[][96], int maxLines) {
    int n = 0;
    bool hasPubKey = false;
    for (int i = 0; i < 32; i++) {
        if (myPubKey[i] != 0) { hasPubKey = true; break; }
    }
    if (n < maxLines) snprintf(info[n++], 96, "Firmware: %s", APP_VERSION);
    if (n < maxLines) snprintf(info[n++], 96, "Node ID: !%08lx", (unsigned long)s_myNodeId);
    if (n < maxLines) snprintf(info[n++], 96, "Role: %s", cfgDeviceRoleName(s_cfg.deviceRole));
    if (n < maxLines) snprintf(info[n++], 96, "PKI key: %s", hasPubKey ? "present" : "missing");
    if (n < maxLines) snprintf(info[n++], 96, "Long: %s", s_cfg.nodeLong);
    if (n < maxLines) snprintf(info[n++], 96, "Short: %s", s_cfg.nodeShort);
    if (n < maxLines) snprintf(info[n++], 96, "Preset: %s",
                               kPresets[s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : 0].name);
    if (n < maxLines) snprintf(info[n++], 96, "Freq: %.3f MHz", s_cfg.loraFreq);
    if (n < maxLines) snprintf(info[n++], 96, "BW %.0f SF %d CR 4/%d", s_cfg.loraBw, s_cfg.loraSf, s_cfg.loraCr);
    if (n < maxLines) snprintf(info[n++], 96, "Pwr %d dBm Hops %d", s_cfg.loraPower, s_cfg.loraHopLimit);
    if (n < maxLines) snprintf(info[n++], 96, "Relayed: %lu", (unsigned long)s_rebroadcastCount);

    // Most / least recently heard node. Entries restored from NVS have
    // lastHeardMs == 0 (unknown after reboot), so only nodes actually heard
    // since boot are candidates.
    const NodeEntry *newest = nullptr;
    const NodeEntry *oldest = nullptr;
    const int nodeCount = Nodes.count();
    for (int i = 0; i < nodeCount; i++) {
        NodeEntry *e = Nodes.getByRank(i);
        if (!e || e->nodeId == 0 || e->lastHeardMs == 0) continue;
        if (!newest || e->lastHeardMs > newest->lastHeardMs) newest = e;
        if (!oldest || e->lastHeardMs < oldest->lastHeardMs) oldest = e;
    }
    if (n < maxLines) {
        if (newest) {
            char idBuf[12], when[24];
            deviceInfoFormatHeard(newest->lastHeardMs, when, sizeof(when));
            snprintf(info[n++], 96, "Newest: %s %s",
                     deviceInfoNodeLabel(newest, idBuf, sizeof(idBuf)), when);
        } else {
            snprintf(info[n++], 96, "Newest: none heard yet");
        }
    }
    if (n < maxLines) {
        if (oldest) {
            char idBuf[12], when[24];
            deviceInfoFormatHeard(oldest->lastHeardMs, when, sizeof(when));
            snprintf(info[n++], 96, "Oldest: %s %s",
                     deviceInfoNodeLabel(oldest, idBuf, sizeof(idBuf)), when);
        } else {
            snprintf(info[n++], 96, "Oldest: none heard yet");
        }
    }
    return n;
}

static void refreshCfgModal() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (!s_cfgModal || !s_cfgActionList || !s_cfgInfoList || !s_cfgHeaderStatus) return;
#else
    if (!s_cfgModal || !s_cfgActionList || !s_cfgHeaderStatus) return;
#endif

    auto contrastColorFor565 = [](uint16_t c) -> lv_color_t {
        uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
        uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
        uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
        // Relative luminance approximation for robust light/dark contrast choice.
        uint16_t luma = (uint16_t)((30U * r + 59U * g + 11U * b) / 100U);
        return (luma >= 128U) ? lv_color_make(0x00, 0x00, 0x00)
                              : lv_color_make(0xFF, 0xFF, 0xFF);
    };
    const lv_color_t selectionOutlineColor = contrastColorFor565(s_ui.selectBg);
    const lv_color_t selectionBgColor = lvColorFrom565(s_ui.selectBg);
    const lv_color_t selectionTextColor = contrastColorFor565(s_ui.selectBg);
    const lv_opa_t selectionBgOpa = (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_80;

    if (s_cfgActionCount <= 0) {
        initCfgActions();
    }
    if (s_cfgSelection < 0) s_cfgSelection = 0;
    if (s_cfgSelection >= s_cfgActionCount) s_cfgSelection = s_cfgActionCount - 1;

    lv_label_set_text(s_cfgHeaderStatus, "Ready");

    lv_obj_clean(s_cfgActionList);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_clean(s_cfgInfoList);
    refreshCfgPanelFocusStyles();
#endif
    lv_obj_t *selectedRowObj = nullptr;

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *cfgRowFont = &lv_font_montserrat_12;
    const int cfgPadTop = 3;
    const int cfgPadBottom = 3;
#elif defined(DEVICE_TDECK)
    const lv_font_t *cfgRowFont = &lv_font_montserrat_14;
    const int cfgPadTop = 3;
    const int cfgPadBottom = 3;
#else
    const lv_font_t *cfgRowFont = &lv_font_montserrat_10;
    const int cfgPadTop = 2;
    const int cfgPadBottom = 2;
#endif

    for (int i = 0; i < s_cfgActionCount; i++) {
        const int actionId = s_cfgActions[i];
        const bool disabled = cfgActionDisabled(actionId);
        char rowText[80];

        lv_obj_t *row = lv_label_create(s_cfgActionList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, cfgRowFont, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(disabled ? 0x5A6B85 : 0xD9E8FF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, cfgPadTop, 0);
        lv_obj_set_style_pad_bottom(row, cfgPadBottom, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_outline_width(row, 0, 0);
        lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
        // Greyed rows are non-interactive on touch; keyboard activation is gated
        // separately in activateCfgSelection().
        if (!disabled) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row,
                        onCfgActionRowPressed,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
        }
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, cfgActionLabel(actionId, rowText, sizeof(rowText)));

        if (disabled) {
            // muted; keep the alternating-row hint but no selection emphasis
            if (i & 1) {
                lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_20, 0);
            }
        } else if (i == s_cfgSelection) {
            lv_obj_set_style_bg_color(row, selectionBgColor, 0);
            lv_obj_set_style_bg_opa(row, selectionBgOpa, 0);
            lv_obj_set_style_text_color(row, selectionTextColor, 0);
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_set_style_border_color(row, selectionOutlineColor, 0);
            lv_obj_set_style_outline_width(row, 1, 0);
            lv_obj_set_style_outline_color(row, selectionOutlineColor, 0);
            lv_obj_set_style_outline_pad(row, 0, 0);
            lv_obj_set_style_outline_opa(row, LV_OPA_70, 0);
            selectedRowObj = row;
        } else if (i & 1) {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        }
    }

    if (selectedRowObj) {
        lv_obj_scroll_to_view(selectedRowObj, LV_ANIM_OFF);
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    static constexpr int kCfgInfoMaxLines = 14;   // 11 device rows + newest/oldest node
    char info[kCfgInfoMaxLines][96] = {};
    int infoCount = buildDeviceInfoLines(info, kCfgInfoMaxLines);

    lv_obj_t *infoHeader = lv_label_create(s_cfgInfoList);
    lv_obj_set_width(infoHeader, lv_pct(100));
    lv_obj_set_style_text_font(infoHeader, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(infoHeader, lv_color_hex(0xBFD6FF), 0);
    lv_obj_set_style_bg_color(infoHeader, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(infoHeader, LV_OPA_70, 0);
    lv_obj_set_style_pad_left(infoHeader, 4, 0);
    lv_obj_set_style_pad_right(infoHeader, 4, 0);
    lv_obj_set_style_pad_top(infoHeader, 3, 0);
    lv_obj_set_style_pad_bottom(infoHeader, 3, 0);
    lv_label_set_long_mode(infoHeader, LV_LABEL_LONG_DOT);
    lv_label_set_text(infoHeader, "Device Info");

    for (int i = 0; i < infoCount; i++) {
        lv_obj_t *row = lv_label_create(s_cfgInfoList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, cfgRowFont, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_bg_opa(row, (i & 1) ? LV_OPA_30 : LV_OPA_10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, cfgPadTop, 0);
        lv_obj_set_style_pad_bottom(row, cfgPadBottom, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, info[i]);
    }
#endif
}

static void onCfgActionRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_cfgActionCount) return;
    s_cfgSelection = idx;
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    refreshCfgModal();
}

#if defined(DEVICE_HELTEC_V4_EXPANSION)
static void onCfgHeaderInfoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    openNodeInfoModal();
}
#endif

static void onCfgWifiRowPressed(lv_event_t *e);
static void onCfgWifiBackdropPressed(lv_event_t *e);
static void onCfgWifiScanRowPressed(lv_event_t *e);

static void closeCfgWifiPassModal() {
    if (lvObjValid(s_cfgWifiPassBackdrop)) {
        lv_obj_del(s_cfgWifiPassBackdrop);
    } else if (lvObjValid(s_cfgWifiPassModal)) {
        lv_obj_del(s_cfgWifiPassModal);
    }
    s_cfgWifiPassBackdrop = nullptr;
    s_cfgWifiPassModal = nullptr;
    s_cfgWifiPassInput = nullptr;
    s_cfgWifiPassKeyboard = nullptr;
    s_cfgWifiPassStatus = nullptr;
    s_cfgWifiPassTargetSsid[0] = '\0';
}

static void closeCfgWifiScanModal() {
    closeCfgWifiPassModal();
    if (lvObjValid(s_cfgWifiScanBackdrop)) {
        lv_obj_del(s_cfgWifiScanBackdrop);
    } else if (lvObjValid(s_cfgWifiScanModal)) {
        lv_obj_del(s_cfgWifiScanModal);
    }
    s_cfgWifiScanBackdrop = nullptr;
    s_cfgWifiScanModal = nullptr;
    s_cfgWifiScanList = nullptr;
    s_cfgWifiScanStatus = nullptr;
}

static void closeCfgWifiPickerModal() {
    closeCfgWifiScanModal();
    if (lvObjValid(s_cfgWifiBackdrop)) {
        lv_obj_del(s_cfgWifiBackdrop);
    } else if (lvObjValid(s_cfgWifiModal)) {
        lv_obj_del(s_cfgWifiModal);
    }
    s_cfgWifiBackdrop = nullptr;
    s_cfgWifiModal = nullptr;
    s_cfgWifiList = nullptr;
    s_cfgWifiPickerOnboardingMode = false;
}

static void refreshCfgWifiPickerModal() {
    if (!s_cfgWifiModal || !s_cfgWifiList) return;

    lv_obj_clean(s_cfgWifiList);
    for (int i = 0; i < s_cfgKnownWifiCount; i++) {
        const KnownWifiEntry &entry = s_cfgKnownWifi[i];
        char rowText[96];
        if (i == 0 && entry.ssid[0]) {
            snprintf(rowText, sizeof(rowText), "Configured: %s", entry.ssid);
        } else if (i == 0) {
            snprintf(rowText, sizeof(rowText), "Configured: (not set)");
        } else if (entry.ssid[0]) {
            snprintf(rowText, sizeof(rowText), "Known: %s", entry.ssid);
        } else {
            snprintf(rowText, sizeof(rowText), "Known: (empty)");
        }

        lv_obj_t *row = lv_label_create(s_cfgWifiList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_set_style_pad_bottom(row, 4, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex((i & 1) ? 0x123266 : 0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, rowText);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, onCfgWifiRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (i == s_cfgWifiSelection) {
            lv_obj_set_style_bg_color(row, lvColorFrom565(s_ui.selectBg), 0);
            lv_obj_set_style_bg_opa(row, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_80, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(0xE8F1FF), 0);
            lv_obj_set_style_text_color(row,
                                        (s_cfg.uiMode == UI_MODE_LIGHT)
                                            ? lv_color_hex(0x000000)
                                            : lv_color_hex(0xFFFFFF),
                                        0);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
}

static void applyCfgWifiSelection(int idx) {
    if (idx < 0 || idx >= s_cfgKnownWifiCount) return;

    const KnownWifiEntry &entry = s_cfgKnownWifi[idx];
    if (idx == 0) {
        if (!entry.ssid[0]) {
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Configured WiFi is empty");
            return;
        }
        s_wifiUsingKnownOverride = false;
        memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
        memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "WiFi selected: %s", entry.ssid);
    } else {
        s_wifiUsingKnownOverride = true;
        strncpy(s_wifiSelectedSsid, entry.ssid, sizeof(s_wifiSelectedSsid) - 1);
        strncpy(s_wifiSelectedPass, entry.pass, sizeof(s_wifiSelectedPass) - 1);
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "WiFi selected: %s", entry.ssid);
    }

    s_cfgWifiSelection = idx;
    s_wifiStaKickMs = 0;
    if (s_cfg.wifiEnabled && wifiHasActiveCreds()) {
        WiFi.disconnect(false);
    }
}

// ── Own-message color picker ─────────────────────────────────────────────
// A "Reset to Default" cell followed by a 4-wide grid of the 16 basic colors
// in kUserMsgColors[]. Navigation index 0 is the reset cell (restores the
// adaptive yellow default, s_cfg.userMsgColor = 0xFF); indices 1..N map to the
// palette entries. The chosen value drives userMessageAccentColor565(), which
// colors only the local user's own messages. Selecting reboots so the change
// applies uniformly across every already-rendered view.
static constexpr int kUserMsgColorNavCount = kUserMsgColorCount + 1;  // +1 reset cell

static void closeCfgColorPickerModal() {
    if (lvObjValid(s_cfgColorBackdrop)) {
        lv_obj_del(s_cfgColorBackdrop);
    } else if (lvObjValid(s_cfgColorModal)) {
        lv_obj_del(s_cfgColorModal);
    }
    s_cfgColorBackdrop = nullptr;
    s_cfgColorModal = nullptr;
    s_cfgColorGrid = nullptr;
}

static void refreshCfgColorPickerModal() {
    if (!s_cfgColorModal || !s_cfgColorGrid) return;

    lv_obj_clean(s_cfgColorGrid);

    // Nav index 0: full-width "Reset to Default" cell (adaptive yellow default).
    {
        const bool sel = (s_cfgColorSelection == 0);
        lv_obj_t *reset = lv_obj_create(s_cfgColorGrid);
        lv_obj_remove_style_all(reset);
        lv_obj_set_size(reset, 44 * 4 + 6 * 3, 26);
        lv_obj_clear_flag(reset, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(reset, 5, 0);
        lv_obj_set_style_bg_color(reset, lvColorFrom565(TFT_YELLOW), 0);
        lv_obj_set_style_bg_opa(reset, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(reset, sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(reset,
                                      sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2A3550),
                                      0);
        lv_obj_add_flag(reset, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(reset, onCfgColorRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)0);
        lv_obj_t *lbl = lv_label_create(reset);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1A1A1A), 0);
        lv_label_set_text(lbl, "Reset to Default");
        if (sel) lv_obj_scroll_to_view(reset, LV_ANIM_OFF);
    }

    for (int i = 0; i < kUserMsgColorCount; i++) {
        const int nav = i + 1;
        lv_obj_t *sw = lv_obj_create(s_cfgColorGrid);
        lv_obj_remove_style_all(sw);
        lv_obj_set_size(sw, 44, 30);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(sw, 5, 0);
        lv_obj_set_style_bg_color(sw, lvColorFrom565(kUserMsgColors[i].color), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        const bool sel = (nav == s_cfgColorSelection);
        lv_obj_set_style_border_width(sw, sel ? 3 : 1, 0);
        lv_obj_set_style_border_color(sw,
                                      sel ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2A3550),
                                      0);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(sw, onCfgColorRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)nav);
        if (sel) lv_obj_scroll_to_view(sw, LV_ANIM_OFF);
    }
}

// navIdx: 0 = reset to adaptive default (0xFF); 1..N = palette entry (navIdx-1).
static void applyCfgColorSelection(int navIdx) {
    if (navIdx < 0 || navIdx >= kUserMsgColorNavCount) return;
    const char *name;
    if (navIdx == 0) {
        s_cfg.userMsgColor = 0xFF;
        name = "Default";
    } else {
        s_cfg.userMsgColor = (uint8_t)(navIdx - 1);
        name = kUserMsgColors[navIdx - 1].name;
    }
    persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "My Message Color: %s - rebooting...", name);
    closeCfgColorPickerModal();
    refreshCfgModal();
    lv_timer_handler();
    delay(1000);
    ESP.restart();
}

static void onCfgColorRowPressed(lv_event_t *e) {
    int navIdx = (int)(intptr_t)lv_event_get_user_data(e);
    s_cfgColorSelection = navIdx;
    applyCfgColorSelection(navIdx);
}

static void onCfgColorBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_cfgColorBackdrop) return;
    closeCfgColorPickerModal();
    refreshCfgModal();
}

// ── Chat-style picker ─────────────────────────────────────────────────────────
// A small in-CFG modal to pick Classic / Bubbles / Outline directly. Picking the
// current style is a no-op; picking a different one reboots (the style is applied
// at boot). Picking directly avoids the reboot-per-step of the old cycle toggle.
static void closeChatStyleModal() {
    if (lvObjValid(s_chatStyleBackdrop)) {
        lv_obj_del(s_chatStyleBackdrop);
    } else if (lvObjValid(s_chatStyleModal)) {
        lv_obj_del(s_chatStyleModal);
    }
    s_chatStyleBackdrop = nullptr;
    s_chatStyleModal = nullptr;
    memset(s_chatStyleRows, 0, sizeof(s_chatStyleRows));
}

static void refreshChatStyleSelection() {
    if (!s_chatStyleModal) return;
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selBg     = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    const lv_color_t idleBg    = isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266);
    const lv_color_t selBorder = isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF);
    const lv_color_t idleBorder= isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);
    for (int i = 0; i <= CHAT_STYLE_MAX; i++) {
        lv_obj_t *row = s_chatStyleRows[i];
        if (!row) continue;
        const bool sel = (i == s_chatStyleSelection);
        lv_obj_set_style_bg_color(row, sel ? selBg : idleBg, 0);
        lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : (isLight ? LV_OPA_90 : LV_OPA_40), 0);
        lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, sel ? selBorder : idleBorder, 0);
        if (sel) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }
}

static void applyChatStyleSelection(int style) {
    if (style < 0 || style > CHAT_STYLE_MAX) return;
    if ((uint8_t)style == s_cfg.chatStyle) {
        // No change — no reboot needed; just return to the CFG screen.
        closeChatStyleModal();
        refreshCfgModal();
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Chat Style: %s (unchanged)",
                 chatStyleName((uint8_t)style));
        return;
    }
    s_cfg.chatStyle = (uint8_t)style;
    persistConfigToPrefs();
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Chat Style: %s - rebooting...",
             chatStyleName((uint8_t)style));
    closeChatStyleModal();
    refreshCfgModal();
    lv_timer_handler();
    delay(1000);
    ESP.restart();
}

static void onChatStyleRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_chatStyleSelection = idx;
    applyChatStyleSelection(idx);
}

static void onChatStyleBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_chatStyleBackdrop) return;
    closeChatStyleModal();
    refreshCfgModal();
}

static void openChatStyleModal() {
    if (!s_rootScreen || s_chatStyleModal || s_chatStyleBackdrop) return;
    s_chatStyleSelection = (s_cfg.chatStyle <= CHAT_STYLE_MAX) ? s_cfg.chatStyle : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 280) modalW = 280;

    s_chatStyleBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chatStyleBackdrop, w, h);
    lv_obj_align(s_chatStyleBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chatStyleBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chatStyleBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatStyleBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chatStyleBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_chatStyleBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chatStyleBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chatStyleBackdrop, onChatStyleBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chatStyleModal = lv_obj_create(s_chatStyleBackdrop);
    lv_obj_set_size(s_chatStyleModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_chatStyleModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_chatStyleModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chatStyleModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chatStyleModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatStyleModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatStyleModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chatStyleModal, 1, 0);
    lv_obj_set_style_border_color(s_chatStyleModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chatStyleModal, 8, 0);
    lv_obj_set_style_pad_row(s_chatStyleModal, 6, 0);
    lv_obj_set_flex_flow(s_chatStyleModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatStyleModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_chatStyleBackdrop);

    lv_obj_t *title = lv_label_create(s_chatStyleModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Chat Style");

    lv_obj_t *hint = lv_label_create(s_chatStyleModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                      "Tap a style to apply"
#else
                      "Arrows=Move  Enter=Select  Backspace=Cancel"
#endif
    );

    static const char *kStyleDesc[CHAT_STYLE_MAX + 1] = {
        "Flat colored text lines",
        "Filled color bubbles",
        "Outlined color bubbles",
    };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    for (int i = 0; i <= CHAT_STYLE_MAX; i++) {
        lv_obj_t *row = lv_btn_create(s_chatStyleModal);
        s_chatStyleRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onChatStyleRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_label_set_text(name, chatStyleName((uint8_t)i));

        lv_obj_t *desc = lv_label_create(row);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(desc, rowTextColor, 0);
        lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
        lv_label_set_text(desc, kStyleDesc[i]);
    }

    refreshChatStyleSelection();
}

// ── Chat-name picker ──────────────────────────────────────────────────────────
// A small in-CFG modal to pick Short / Long sender names in chat. Unlike the
// chat-style picker this applies live (no reboot): bubble views re-render
// immediately and new classic-chat lines follow the choice.
static void refreshChatView(bool force);

static void closeChatNameModal() {
    if (lvObjValid(s_chatNameBackdrop)) {
        lv_obj_del(s_chatNameBackdrop);
    } else if (lvObjValid(s_chatNameModal)) {
        lv_obj_del(s_chatNameModal);
    }
    s_chatNameBackdrop = nullptr;
    s_chatNameModal = nullptr;
    memset(s_chatNameRows, 0, sizeof(s_chatNameRows));
}

static void refreshChatNameSelection() {
    if (!s_chatNameModal) return;
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selBg     = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    const lv_color_t idleBg    = isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266);
    const lv_color_t selBorder = isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF);
    const lv_color_t idleBorder= isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);
    for (int i = 0; i <= CHAT_NAME_MAX; i++) {
        lv_obj_t *row = s_chatNameRows[i];
        if (!row) continue;
        const bool sel = (i == s_chatNameSelection);
        lv_obj_set_style_bg_color(row, sel ? selBg : idleBg, 0);
        lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : (isLight ? LV_OPA_90 : LV_OPA_40), 0);
        lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, sel ? selBorder : idleBorder, 0);
        if (sel) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }
}

static void applyChatNameSelection(int style) {
    if (style < 0 || style > CHAT_NAME_MAX) return;
    const bool changed = ((uint8_t)style != s_cfg.chatNameStyle);
    s_cfg.chatNameStyle = (uint8_t)style;
    if (changed) {
        persistConfigToPrefs();
        // Re-render chat live so bubble name tags update immediately.
        s_lastRenderedChannel = -1;
        refreshChatView(true);
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Chat Names: %s%s",
             chatNameStyleName((uint8_t)style), changed ? "" : " (unchanged)");
    closeChatNameModal();
    refreshCfgModal();
}

static void onChatNameRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_chatNameSelection = idx;
    applyChatNameSelection(idx);
}

static void onChatNameBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_chatNameBackdrop) return;
    closeChatNameModal();
    refreshCfgModal();
}

static void openChatNameModal() {
    if (!s_rootScreen || s_chatNameModal || s_chatNameBackdrop) return;
    s_chatNameSelection = (s_cfg.chatNameStyle <= CHAT_NAME_MAX) ? s_cfg.chatNameStyle : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 280) modalW = 280;

    s_chatNameBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chatNameBackdrop, w, h);
    lv_obj_align(s_chatNameBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chatNameBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chatNameBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatNameBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_chatNameBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_chatNameBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_chatNameBackdrop, 0, 0);
    lv_obj_add_event_cb(s_chatNameBackdrop, onChatNameBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_chatNameModal = lv_obj_create(s_chatNameBackdrop);
    lv_obj_set_size(s_chatNameModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_chatNameModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_chatNameModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chatNameModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chatNameModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_chatNameModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatNameModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chatNameModal, 1, 0);
    lv_obj_set_style_border_color(s_chatNameModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chatNameModal, 8, 0);
    lv_obj_set_style_pad_row(s_chatNameModal, 6, 0);
    lv_obj_set_flex_flow(s_chatNameModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatNameModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_chatNameBackdrop);

    lv_obj_t *title = lv_label_create(s_chatNameModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Chat Names");

    lv_obj_t *hint = lv_label_create(s_chatNameModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                      "Tap a name style to apply"
#else
                      "Arrows=Move  Enter=Select  Backspace=Cancel"
#endif
    );

    static const char *kNameLabel[CHAT_NAME_MAX + 1] = { "Short", "Long" };
    static const char *kNameDesc[CHAT_NAME_MAX + 1] = {
        "4-char short name (ABCD)",
        "Full node name when known",
    };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    for (int i = 0; i <= CHAT_NAME_MAX; i++) {
        lv_obj_t *row = lv_btn_create(s_chatNameModal);
        s_chatNameRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 5, 0);
        lv_obj_set_style_pad_row(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(row, onChatNameRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_label_set_text(name, kNameLabel[i]);

        lv_obj_t *desc = lv_label_create(row);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(desc, rowTextColor, 0);
        lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
        lv_label_set_text(desc, kNameDesc[i]);
    }

    refreshChatNameSelection();
}

// ── Font-size picker ─────────────────────────────────────────────────────────
// Picks the chat/DM message font size (Small/Medium/Large). Applies live like
// the chat-name picker — no reboot — by re-rendering the chat and DM views once
// the choice is committed.
static void closeFontSizeModal() {
    if (lvObjValid(s_fontSizeBackdrop)) {
        lv_obj_del(s_fontSizeBackdrop);
    } else if (lvObjValid(s_fontSizeModal)) {
        lv_obj_del(s_fontSizeModal);
    }
    s_fontSizeBackdrop = nullptr;
    s_fontSizeModal = nullptr;
    memset(s_fontSizeRows, 0, sizeof(s_fontSizeRows));
}

static void refreshFontSizeSelection() {
    if (!s_fontSizeModal) return;
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selBg     = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    const lv_color_t idleBg    = isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266);
    const lv_color_t selBorder = isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF);
    const lv_color_t idleBorder= isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);
    for (int i = 0; i <= FONT_SIZE_MAX; i++) {
        lv_obj_t *row = s_fontSizeRows[i];
        if (!row) continue;
        const bool sel = (i == s_fontSizeSelection);
        lv_obj_set_style_bg_color(row, sel ? selBg : idleBg, 0);
        lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : (isLight ? LV_OPA_90 : LV_OPA_40), 0);
        lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, sel ? selBorder : idleBorder, 0);
        if (sel) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }
}

static void applyFontSizeSelection(int size) {
    if (size < 0 || size > FONT_SIZE_MAX) return;
    const bool changed = ((uint8_t)size != s_cfg.fontSize);
    s_cfg.fontSize = (uint8_t)size;
    if (changed) {
        persistConfigToPrefs();
        // Re-render both message views so the new size takes effect immediately.
        s_lastRenderedChannel = -1;
        s_lastRenderedCount = -1;
        refreshChatView(true);
        refreshDmModal(true);
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Font Size: %s%s",
             fontSizeName((uint8_t)size), changed ? "" : " (unchanged)");
    closeFontSizeModal();
    refreshCfgModal();
}

static void onFontSizeRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_fontSizeSelection = idx;
    applyFontSizeSelection(idx);
}

static void onFontSizeBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_fontSizeBackdrop) return;
    closeFontSizeModal();
    refreshCfgModal();
}

static void openFontSizeModal() {
    if (!s_rootScreen || s_fontSizeModal || s_fontSizeBackdrop) return;
    s_fontSizeSelection = (s_cfg.fontSize <= FONT_SIZE_MAX) ? s_cfg.fontSize : FONT_SIZE_MEDIUM;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    // Four short labels in a 2x2 grid, so this modal is deliberately narrower
    // than the list-style pickers — no description column to make room for.
    int modalW = w - 40;
    if (modalW < 150) modalW = w - 8;
    if (modalW > 210) modalW = 210;

    s_fontSizeBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_fontSizeBackdrop, w, h);
    lv_obj_align(s_fontSizeBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_fontSizeBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_fontSizeBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_fontSizeBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_fontSizeBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_fontSizeBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_fontSizeBackdrop, 0, 0);
    lv_obj_add_event_cb(s_fontSizeBackdrop, onFontSizeBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_fontSizeModal = lv_obj_create(s_fontSizeBackdrop);
    lv_obj_set_size(s_fontSizeModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_fontSizeModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_fontSizeModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_fontSizeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_fontSizeModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_fontSizeModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_fontSizeModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_fontSizeModal, 1, 0);
    lv_obj_set_style_border_color(s_fontSizeModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_fontSizeModal, 8, 0);
    lv_obj_set_style_pad_row(s_fontSizeModal, 6, 0);
    lv_obj_set_flex_flow(s_fontSizeModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_fontSizeModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_fontSizeBackdrop);

    lv_obj_t *title = lv_label_create(s_fontSizeModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Font Size");

    lv_obj_t *hint = lv_label_create(s_fontSizeModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(hint, "Tap a size to apply");
#else
    // Kept short so it fits the narrowed modal without wrapping to three lines.
    lv_label_set_text_fmt(hint, "Move  Enter=OK  %s=Cancel", modalCloseKeyLabel());
#endif

    // "X-Large" rather than the full name fontSizeName() reports: the grid cells
    // are ~half the modal width and the spelled-out form wraps to two lines on
    // the narrowest board.
    static const char *kSizeLabel[FONT_SIZE_MAX + 1] = { "Small", "Medium", "Large", "X-Large" };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    // Wrapping row container: four ~half-width buttons fall into two rows of
    // two. A stacked list of four described rows overflows the 135 px-tall
    // Cardputer panel, and the size names need no caption.
    lv_obj_t *grid = lv_obj_create(s_fontSizeModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);

    for (int i = 0; i <= FONT_SIZE_MAX; i++) {
        lv_obj_t *row = lv_btn_create(grid);
        s_fontSizeRows[i] = row;
        lv_obj_set_width(row, lv_pct(46));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onFontSizeRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_width(name, lv_pct(100));
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(name, kSizeLabel[i]);
        lv_obj_center(name);
    }

    refreshFontSizeSelection();
}

// ── Notification-sound picker ────────────────────────────────────────────────
// Picking directly instead of cycling. Moving the selection previews the sound,
// which means s_cfg.msgAlertSound has to hold the highlighted mode while the
// modal is open (triggerMessageAlert plays whatever is configured). The original
// value is stashed on open and restored if the user cancels, so previewing is
// never destructive.
static void closeAlertSoundModal() {
    if (lvObjValid(s_alertSoundBackdrop)) {
        lv_obj_del(s_alertSoundBackdrop);
    } else if (lvObjValid(s_alertSoundModal)) {
        lv_obj_del(s_alertSoundModal);
    }
    s_alertSoundBackdrop = nullptr;
    s_alertSoundModal = nullptr;
    memset(s_alertSoundRows, 0, sizeof(s_alertSoundRows));
}

// Cancel path: undo any preview-driven change before closing.
static void cancelAlertSoundModal() {
    s_cfg.msgAlertSound = s_alertSoundOriginal;
    closeAlertSoundModal();
    refreshCfgModal();
}

static void refreshAlertSoundSelection() {
    if (!s_alertSoundModal) return;
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selBg     = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    const lv_color_t idleBg    = isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266);
    const lv_color_t selBorder = isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF);
    const lv_color_t idleBorder= isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);
    for (int i = 0; i <= MSG_ALERT_SOUND_MAX; i++) {
        lv_obj_t *row = s_alertSoundRows[i];
        if (!row) continue;
        const bool sel = (i == s_alertSoundSelection);
        lv_obj_set_style_bg_color(row, sel ? selBg : idleBg, 0);
        lv_obj_set_style_bg_opa(row, sel ? LV_OPA_COVER : (isLight ? LV_OPA_90 : LV_OPA_40), 0);
        lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, sel ? selBorder : idleBorder, 0);
        if (sel) lv_obj_scroll_to_view(row, LV_ANIM_OFF);
    }
}

// Move the highlight and play that mode so the user hears it before committing.
static void previewAlertSoundSelection(int mode) {
    if (mode < 0 || mode > MSG_ALERT_SOUND_MAX) return;
    s_alertSoundSelection = mode;
    s_cfg.msgAlertSound = (uint8_t)mode;
    refreshAlertSoundSelection();
    triggerMessageAlert(true);   // bypass the rate limit; this is an explicit preview
}

static void applyAlertSoundSelection(int mode) {
    if (mode < 0 || mode > MSG_ALERT_SOUND_MAX) return;
    s_cfg.msgAlertSound = (uint8_t)mode;
    if ((uint8_t)mode != s_alertSoundOriginal) {
        persistMessageAlertSetting();
    }
    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Notification sound: %s",
             msgAlertSoundName((uint8_t)mode));
    closeAlertSoundModal();
    refreshCfgModal();
}

static void onAlertSoundRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    // First tap on a different row previews it; tapping the highlighted row
    // commits. Touch has no separate "move" gesture, so this gives touch-only
    // builds the same hear-before-you-commit behavior as arrow navigation.
    if (idx != s_alertSoundSelection) previewAlertSoundSelection(idx);
    else                              applyAlertSoundSelection(idx);
}

static void onAlertSoundBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_alertSoundBackdrop) return;
    cancelAlertSoundModal();
}

static void openAlertSoundModal() {
    if (!s_rootScreen || s_alertSoundModal || s_alertSoundBackdrop) return;
    s_alertSoundOriginal = s_cfg.msgAlertSound;
    s_alertSoundSelection = (s_cfg.msgAlertSound <= MSG_ALERT_SOUND_MAX)
                                ? s_cfg.msgAlertSound : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    // Four short labels in a 2x2 grid, so this modal is deliberately narrower
    // than the list-style pickers — no description column to make room for.
    int modalW = w - 40;
    if (modalW < 150) modalW = w - 8;
    if (modalW > 210) modalW = 210;

    s_alertSoundBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_alertSoundBackdrop, w, h);
    lv_obj_align(s_alertSoundBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_alertSoundBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_alertSoundBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_alertSoundBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_alertSoundBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_alertSoundBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_alertSoundBackdrop, 0, 0);
    lv_obj_add_event_cb(s_alertSoundBackdrop, onAlertSoundBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_alertSoundModal = lv_obj_create(s_alertSoundBackdrop);
    lv_obj_set_size(s_alertSoundModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_alertSoundModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_alertSoundModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_alertSoundModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_alertSoundModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_alertSoundModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_alertSoundModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_alertSoundModal, 1, 0);
    lv_obj_set_style_border_color(s_alertSoundModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_alertSoundModal, 8, 0);
    lv_obj_set_style_pad_row(s_alertSoundModal, 6, 0);
    lv_obj_set_flex_flow(s_alertSoundModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_alertSoundModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_alertSoundBackdrop);

    lv_obj_t *title = lv_label_create(s_alertSoundModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Notification Sound");

    lv_obj_t *hint = lv_label_create(s_alertSoundModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(hint, "Tap to preview, tap again to apply");
#else
    // Kept short so it fits the narrowed modal without wrapping to three lines.
    lv_label_set_text_fmt(hint, "Move=Preview  Enter=OK  %s=Cancel",
                          modalCloseKeyLabel());
#endif

    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                        ? lv_color_hex(0x13233D) : lv_color_hex(0xD9E8FF);

    // Wrapping row container: four ~half-width buttons fall into two rows of
    // two. The tone names carry no description text — moving the selection
    // plays the tone, which describes it better than a caption could.
    lv_obj_t *grid = lv_obj_create(s_alertSoundModal);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);

    for (int i = 0; i <= MSG_ALERT_SOUND_MAX; i++) {
        lv_obj_t *row = lv_btn_create(grid);
        s_alertSoundRows[i] = row;
        lv_obj_set_width(row, lv_pct(46));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onAlertSoundRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_width(name, lv_pct(100));
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(name, rowTextColor, 0);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(name, msgAlertSoundName((uint8_t)i));
        lv_obj_center(name);
    }

    refreshAlertSoundSelection();
}

static void openCfgColorPickerModal() {
    if (!s_rootScreen || s_cfgColorModal || s_cfgColorBackdrop) return;

    s_cfgColorSelection = (s_cfg.userMsgColor < kUserMsgColorCount)
                              ? (s_cfg.userMsgColor + 1) : 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 260) modalW = 260;

    s_cfgColorBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgColorBackdrop, w, h);
    lv_obj_align(s_cfgColorBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgColorBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgColorBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgColorBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgColorBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgColorBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgColorBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgColorBackdrop, onCfgColorBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgColorModal = lv_obj_create(s_cfgColorBackdrop);
    lv_obj_set_size(s_cfgColorModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgColorModal, (h > 40) ? (h - 16) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_cfgColorModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgColorModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgColorModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgColorModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgColorModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgColorModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgColorModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgColorModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgColorModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgColorModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgColorModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgColorBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgColorModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "My Message Color");

    lv_obj_t *hint = lv_label_create(s_cfgColorModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                      "Tap a color to apply"
#else
                      "Arrows=Move  Enter=Select  Backspace=Cancel"
#endif
    );

    s_cfgColorGrid = lv_obj_create(s_cfgColorModal);
    // 4 swatches (44px) + 3 gaps (6px) per row → keep the grid 4-wide. On short
    // screens (e.g. Cardputer 135px tall) cap the height and let it scroll so
    // the highlighted swatch is always brought into view.
    lv_obj_set_width(s_cfgColorGrid, 44 * 4 + 6 * 3);
    const int gridMaxH = (h > 60) ? (h - 60) : LV_SIZE_CONTENT;
    lv_obj_set_height(s_cfgColorGrid, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgColorGrid, gridMaxH, 0);
    lv_obj_add_flag(s_cfgColorGrid, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgColorGrid);
    lv_obj_set_scrollbar_mode(s_cfgColorGrid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_cfgColorGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cfgColorGrid, 0, 0);
    lv_obj_set_style_pad_all(s_cfgColorGrid, 0, 0);
    lv_obj_set_style_pad_row(s_cfgColorGrid, 6, 0);
    lv_obj_set_style_pad_column(s_cfgColorGrid, 6, 0);
    lv_obj_set_flex_flow(s_cfgColorGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_cfgColorGrid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    refreshCfgColorPickerModal();
}

static void onCfgWifiRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
    applyCfgWifiSelection(idx);
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
}

static void onCfgWifiBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_cfgWifiBackdrop) return;
    const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
    closeCfgWifiPickerModal();
    if (fromOnboarding || s_onboardingModal) renderOnboardingStage();
    else refreshCfgModal();
}

static void onCfgWifiOpenScanPressed(lv_event_t *e) {
    LV_UNUSED(e);
    openCfgWifiScanModal();
}

static void onCfgWifiCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    const bool fromOnboarding = s_cfgWifiPickerOnboardingMode;
    closeCfgWifiPickerModal();
    if (fromOnboarding || s_onboardingModal) renderOnboardingStage();
    else refreshCfgModal();
}

static void onCfgWifiScanBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_cfgWifiScanBackdrop) return;
    closeCfgWifiScanModal();
    if (s_cfgWifiModal) refreshCfgWifiPickerModal();
}

static void refreshCfgWifiScanModal(bool runScan) {
    if (!s_cfgWifiScanModal || !s_cfgWifiScanList) return;

    if (runScan) {
        s_cfgScannedWifiCount = 0;
        s_cfgWifiScanSelection = 0;
        if (s_cfgWifiScanStatus) {
            lv_label_set_text(s_cfgWifiScanStatus, "Scanning WiFi networks...");
        }
        lv_timer_handler();

        wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_OFF) {
            WiFi.mode(WIFI_STA);
        } else if (mode == WIFI_AP) {
            WiFi.mode(WIFI_AP_STA);
        }

        int found = WiFi.scanNetworks();
        if (found > 0) {
            for (int i = 0; i < found && s_cfgScannedWifiCount < kScannedWifiMaxCount; i++) {
                String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;

                bool seen = false;
                for (int j = 0; j < s_cfgScannedWifiCount; j++) {
                    if (ssid.equals(s_cfgScannedWifi[j].ssid)) {
                        seen = true;
                        break;
                    }
                }
                if (seen) continue;

                ScannedWifiEntry &entry = s_cfgScannedWifi[s_cfgScannedWifiCount++];
                memset(&entry, 0, sizeof(entry));
                strncpy(entry.ssid, ssid.c_str(), sizeof(entry.ssid) - 1);
                entry.rssi = WiFi.RSSI(i);
                entry.secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            if (s_cfgWifiScanStatus) {
                char status[64];
                snprintf(status, sizeof(status), "Found %d network(s)", s_cfgScannedWifiCount);
                lv_label_set_text(s_cfgWifiScanStatus, status);
            }
        } else if (s_cfgWifiScanStatus) {
            lv_label_set_text(s_cfgWifiScanStatus, "No networks found");
        }
        WiFi.scanDelete();
    }

    lv_obj_clean(s_cfgWifiScanList);
    for (int i = 0; i < s_cfgScannedWifiCount; i++) {
        const ScannedWifiEntry &entry = s_cfgScannedWifi[i];
        char rowText[120];
        snprintf(rowText,
                 sizeof(rowText),
                 "%s  %ld dBm%s",
                 entry.ssid,
                 (long)entry.rssi,
                 entry.secure ? "  lock" : "  open");

        lv_obj_t *row = lv_label_create(s_cfgWifiScanList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_pad_left(row, 6, 0);
        lv_obj_set_style_pad_right(row, 6, 0);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_set_style_pad_bottom(row, 4, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex((i & 1) ? 0x123266 : 0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, rowText);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, onCfgWifiScanRowPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        if (i == s_cfgWifiScanSelection) {
            lv_obj_set_style_bg_color(row, lvColorFrom565(s_ui.selectBg), 0);
            lv_obj_set_style_bg_opa(row, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_80, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(0xE8F1FF), 0);
            lv_obj_set_style_text_color(row,
                                        (s_cfg.uiMode == UI_MODE_LIGHT)
                                            ? lv_color_hex(0x000000)
                                            : lv_color_hex(0xFFFFFF),
                                        0);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
}

static void onCfgWifiScanRowPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_cfgWifiScanSelection = idx;
    refreshCfgWifiScanModal(false);
    openCfgWifiPassModal(idx);
}

static void onCfgWifiScanCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeCfgWifiScanModal();
    if (s_cfgWifiModal) refreshCfgWifiPickerModal();
}

static void onCfgWifiScanRescanPressed(lv_event_t *e) {
    LV_UNUSED(e);
    refreshCfgWifiScanModal(true);
}

static void onCfgWifiScanConnectPressed(lv_event_t *e) {
    LV_UNUSED(e);
    if (s_cfgWifiScanSelection >= 0 && s_cfgWifiScanSelection < s_cfgScannedWifiCount) {
        openCfgWifiPassModal(s_cfgWifiScanSelection);
    }
}

static void onCfgWifiPassBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) != s_cfgWifiPassBackdrop) return;
    closeCfgWifiPassModal();
    refreshCfgWifiScanModal(false);
}

static void onCfgWifiPassCancelPressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeCfgWifiPassModal();
    refreshCfgWifiScanModal(false);
}

static void cfgWifiConnectFromPassModal() {
    if (!s_cfgWifiPassInput || !s_cfgWifiPassTargetSsid[0]) return;

    const char *pass = lv_textarea_get_text(s_cfgWifiPassInput);
    if (!pass) pass = "";

    if (s_cfgWifiPassStatus) {
        lv_label_set_text_fmt(s_cfgWifiPassStatus, "Connecting to %s...", s_cfgWifiPassTargetSsid);
        lv_timer_handler();
    }

    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    } else if (mode == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
    }

    WiFi.disconnect(false);
    WiFi.begin(s_cfgWifiPassTargetSsid, pass);

    bool connected = false;
    uint32_t startMs = millis();
    while ((millis() - startMs) < 12000UL) {
        if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
            connected = true;
            break;
        }
        lv_timer_handler();
        delay(120);
    }

    if (connected) {
        addTempKnownWifi(s_cfgWifiPassTargetSsid, pass);
        if (strncmp(s_cfgWifiPassTargetSsid, s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid)) == 0) {
            s_wifiUsingKnownOverride = false;
            memset(s_wifiSelectedSsid, 0, sizeof(s_wifiSelectedSsid));
            memset(s_wifiSelectedPass, 0, sizeof(s_wifiSelectedPass));
        } else {
            s_wifiUsingKnownOverride = true;
            strncpy(s_wifiSelectedSsid, s_cfgWifiPassTargetSsid, sizeof(s_wifiSelectedSsid) - 1);
            strncpy(s_wifiSelectedPass, pass, sizeof(s_wifiSelectedPass) - 1);
        }
        s_wifiStaKickMs = 0;

        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "WiFi connected: %s", s_cfgWifiPassTargetSsid);
        if (s_cfgWifiPickerOnboardingMode) {
            utf8util::copyTruncate(s_onboardingWifiSsidScratch,
                                   sizeof(s_onboardingWifiSsidScratch),
                                   s_cfgWifiPassTargetSsid);
            utf8util::copyTruncate(s_onboardingWifiPassScratch,
                                   sizeof(s_onboardingWifiPassScratch),
                                   pass ? pass : "");
            closeCfgWifiPassModal();
            closeCfgWifiScanModal();
            closeCfgWifiPickerModal();
            renderOnboardingStage();
            onboardingSetStatus(s_cfgStatus);
            return;
        }
        closeCfgWifiPassModal();
        closeCfgWifiScanModal();
        populateKnownWifiEntries();
        refreshCfgWifiPickerModal();
        openCfgActionMessageModal(s_cfgStatus);
        return;
    }

    if (s_cfgWifiScanStatus) {
        lv_label_set_text_fmt(s_cfgWifiScanStatus,
                              "Connection failed: %s",
                              s_cfgWifiPassTargetSsid);
    }
    closeCfgWifiPassModal();
    refreshCfgWifiScanModal(false);
}

static void onCfgWifiPassConnectPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgWifiConnectFromPassModal();
}

static void openCfgWifiPassModal(int scanIdx) {
    if (!s_rootScreen) return;
    if (scanIdx < 0 || scanIdx >= s_cfgScannedWifiCount) return;

    closeCfgWifiPassModal();

    const ScannedWifiEntry &entry = s_cfgScannedWifi[scanIdx];
    strncpy(s_cfgWifiPassTargetSsid, entry.ssid, sizeof(s_cfgWifiPassTargetSsid) - 1);

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 320) modalW = 320;

    s_cfgWifiPassBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiPassBackdrop, w, h);
    lv_obj_align(s_cfgWifiPassBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiPassBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiPassBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiPassBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiPassBackdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiPassBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiPassBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiPassBackdrop, onCfgWifiPassBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiPassModal = lv_obj_create(s_cfgWifiPassBackdrop);
    lv_obj_set_size(s_cfgWifiPassModal,
                    modalW,
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                    h - 18
#else
                    LV_SIZE_CONTENT
#endif
    );
    lv_obj_align(s_cfgWifiPassModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiPassModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_cfgWifiPassModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiPassModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiPassModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiPassModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgWifiPassModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgWifiPassModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgWifiPassModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiPassModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgWifiPassBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiPassModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(title, "Connect: %s", s_cfgWifiPassTargetSsid);

    s_cfgWifiPassInput = lv_textarea_create(s_cfgWifiPassModal);
    lv_obj_set_width(s_cfgWifiPassInput, lv_pct(100));
    lv_obj_set_height(s_cfgWifiPassInput, 42);
    lv_obj_set_style_text_font(s_cfgWifiPassInput, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_cfgWifiPassInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_cfgWifiPassInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiPassInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiPassInput, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiPassInput, lv_color_hex(0x4C76BA), 0);
    lv_textarea_set_one_line(s_cfgWifiPassInput, true);
    lv_textarea_set_max_length(s_cfgWifiPassInput, 63);
    lv_textarea_set_password_mode(s_cfgWifiPassInput, false);
    lv_textarea_set_placeholder_text(s_cfgWifiPassInput,
                                     entry.secure ? "WiFi password" : "Open network (leave blank)");

    s_cfgWifiPassStatus = lv_label_create(s_cfgWifiPassModal);
    lv_obj_set_width(s_cfgWifiPassStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgWifiPassStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgWifiPassStatus, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgWifiPassStatus, LV_TEXT_ALIGN_LEFT, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(s_cfgWifiPassStatus, "Enter password, then tap Connect");
#else
    lv_label_set_text(s_cfgWifiPassStatus, "Enter=Connect  Backspace=Back");
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiPassModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onCfgWifiPassCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_center(cancelLbl);

    lv_obj_t *connectBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(connectBtn, 1);
    lv_obj_set_height(connectBtn, lv_pct(100));
    lv_obj_add_event_cb(connectBtn, onCfgWifiPassConnectPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connectLbl = lv_label_create(connectBtn);
    lv_obj_set_style_text_font(connectLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(connectLbl, "Connect");
    lv_obj_center(connectLbl);

    s_cfgWifiPassKeyboard = lv_keyboard_create(s_cfgWifiPassModal);
    lv_obj_set_width(s_cfgWifiPassKeyboard, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgWifiPassKeyboard, 1);
    lv_keyboard_set_textarea(s_cfgWifiPassKeyboard, s_cfgWifiPassInput);
#endif
}

static void openCfgWifiScanModal() {
    if (!s_rootScreen || s_cfgWifiScanModal || s_cfgWifiScanBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 340) modalW = 340;

    s_cfgWifiScanBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiScanBackdrop, w, h);
    lv_obj_align(s_cfgWifiScanBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiScanBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiScanBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiScanBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiScanBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgWifiScanBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiScanBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiScanBackdrop, onCfgWifiScanBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiScanModal = lv_obj_create(s_cfgWifiScanBackdrop);
    lv_obj_set_size(s_cfgWifiScanModal, modalW, (h > 120) ? (h - 20) : LV_SIZE_CONTENT);
    lv_obj_align(s_cfgWifiScanModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiScanModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiScanModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiScanModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiScanModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiScanModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiScanModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgWifiScanModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgWifiScanModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgWifiScanModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiScanModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgWifiScanBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiScanModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Scan Networks");

    s_cfgWifiScanStatus = lv_label_create(s_cfgWifiScanModal);
    lv_obj_set_width(s_cfgWifiScanStatus, lv_pct(100));
    lv_obj_set_style_text_font(s_cfgWifiScanStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_cfgWifiScanStatus, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_cfgWifiScanStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_cfgWifiScanStatus,
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                      "Pick a network, then Connect"
#else
                      "Enter=Connect  N=Rescan  Backspace=Back"
#endif
    );

    s_cfgWifiScanList = lv_obj_create(s_cfgWifiScanModal);
    lv_obj_set_width(s_cfgWifiScanList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgWifiScanList, 1);
    lv_obj_add_flag(s_cfgWifiScanList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgWifiScanList);
    lv_obj_set_scrollbar_mode(s_cfgWifiScanList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgWifiScanList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiScanList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiScanList, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiScanList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgWifiScanList, 2, 0);
    lv_obj_set_style_pad_row(s_cfgWifiScanList, 2, 0);
    lv_obj_set_flex_flow(s_cfgWifiScanList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiScanList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiScanModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onCfgWifiScanCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_center(cancelLbl);

    lv_obj_t *rescanBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(rescanBtn, 1);
    lv_obj_set_height(rescanBtn, lv_pct(100));
    lv_obj_add_event_cb(rescanBtn, onCfgWifiScanRescanPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *rescanLbl = lv_label_create(rescanBtn);
    lv_obj_set_style_text_font(rescanLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(rescanLbl, "Rescan");
    lv_obj_center(rescanLbl);

    lv_obj_t *connectBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(connectBtn, 1);
    lv_obj_set_height(connectBtn, lv_pct(100));
    lv_obj_add_event_cb(connectBtn, onCfgWifiScanConnectPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *connectLbl = lv_label_create(connectBtn);
    lv_obj_set_style_text_font(connectLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(connectLbl, "Connect");
    lv_obj_center(connectLbl);
#endif

    refreshCfgWifiScanModal(true);
}

static void openCfgWifiPickerModal(bool forOnboarding) {
    if (!s_rootScreen || s_cfgWifiModal || s_cfgWifiBackdrop) return;
    s_cfgWifiPickerOnboardingMode = forOnboarding;
    populateKnownWifiEntries();

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 24;
    if (modalW < 170) modalW = w - 8;
    if (modalW > 340) modalW = 340;

    s_cfgWifiBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgWifiBackdrop, w, h);
    lv_obj_align(s_cfgWifiBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgWifiBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgWifiBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgWifiBackdrop, onCfgWifiBackdropPressed, LV_EVENT_CLICKED, nullptr);

    s_cfgWifiModal = lv_obj_create(s_cfgWifiBackdrop);
    lv_obj_set_size(s_cfgWifiModal, modalW, (h > 120) ? (h - 20) : LV_SIZE_CONTENT);
    lv_obj_align(s_cfgWifiModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgWifiModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgWifiModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgWifiModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgWifiModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgWifiModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgWifiModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgWifiModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_cfgWifiBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgWifiModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Choose WiFi");

    lv_obj_t *hint = lv_label_create(s_cfgWifiModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint,
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                      "Pick known WiFi or tap New"
#else
                      "Enter=Select  N=New  Backspace=Cancel"
#endif
    );

    s_cfgWifiList = lv_obj_create(s_cfgWifiModal);
    lv_obj_set_width(s_cfgWifiList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgWifiList, 1);
    lv_obj_add_flag(s_cfgWifiList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgWifiList);
    lv_obj_set_scrollbar_mode(s_cfgWifiList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgWifiList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgWifiList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgWifiList, 1, 0);
    lv_obj_set_style_border_color(s_cfgWifiList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgWifiList, 2, 0);
    lv_obj_set_style_pad_row(s_cfgWifiList, 2, 0);
    lv_obj_set_flex_flow(s_cfgWifiList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgWifiList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *btnRow = lv_obj_create(s_cfgWifiModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, 30);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 4, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancelBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(cancelBtn, 1);
    lv_obj_set_height(cancelBtn, lv_pct(100));
    lv_obj_add_event_cb(cancelBtn, onCfgWifiCancelPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(cancelLbl, "Cancel");
    lv_obj_center(cancelLbl);

    lv_obj_t *newBtn = lv_btn_create(btnRow);
    lv_obj_set_flex_grow(newBtn, 1);
    lv_obj_set_height(newBtn, lv_pct(100));
    lv_obj_add_event_cb(newBtn, onCfgWifiOpenScanPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *newLbl = lv_label_create(newBtn);
    lv_obj_set_style_text_font(newLbl, &lv_font_montserrat_10, 0);
    lv_label_set_text(newLbl, "New");
    lv_obj_center(newLbl);
#endif

    refreshCfgWifiPickerModal();
}

static void closeCfgModal() {
    closeCfgWifiPickerModal();
    closeCfgConfirmModal();
    closeCfgActionMessageModal();
    closeSysStatsModal();
#if !defined(DEVICE_TLORA_PAGER_TFT)
    closeNodeInfoModal();
#endif
    lvObjDeleteSafe(s_cfgModal);
    s_cfgActionList = nullptr;
    s_cfgInfoList = nullptr;
    s_cfgHeaderStatus = nullptr;
    s_cfgAwaitEnterRelease = false;
    s_cfgInfoPanelFocused = false;
}

static void closeCfgActionMessageModal() {
    if (lvObjValid(s_cfgActionMsgBackdrop)) {
        lv_obj_del(s_cfgActionMsgBackdrop);
    } else if (lvObjValid(s_cfgActionMsgModal)) {
        lv_obj_del(s_cfgActionMsgModal);
    }
    s_cfgActionMsgBackdrop = nullptr;
    s_cfgActionMsgModal = nullptr;
    s_cfgActionMsgOpenedMs = 0;
}

static void onCfgActionMessageBackdropPressed(lv_event_t *e) {
    if (lv_event_get_target(e) == s_cfgActionMsgBackdrop) {
        closeCfgActionMessageModal();
    }
}

static void openCfgActionMessageModal(const char *msg) {
    if (!s_rootScreen) return;

    const char *displayMsg = msg;
    while (displayMsg && *displayMsg && isspace((unsigned char)*displayMsg)) {
        displayMsg++;
    }
    if (!displayMsg || !displayMsg[0]) {
        displayMsg = "No result details available.";
    }

    Serial.printf("[lvgl-cfg] popup: %s\n", displayMsg);

    closeCfgActionMessageModal();

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    int modalW = screenW - 28;
    if (modalW < 180) modalW = screenW - 8;
    if (modalW > 320) modalW = 320;
    lv_coord_t contentW = (lv_coord_t)(modalW - 18);
    if (contentW < 64) contentW = 64;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t bodyPanelBg = lightUi ? lv_color_hex(0xF5F9FF) : lv_color_hex(0x123266);
    const lv_color_t bodyTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const lv_color_t hintTextColor = lightUi ? lv_color_hex(0x35567E) : lv_color_hex(0xA7C7FF);

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *bodyFont = &lv_font_montserrat_14;
#elif defined(DEVICE_TDECK)
    const lv_font_t *bodyFont = &lv_font_montserrat_14;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif

    s_cfgActionMsgBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgActionMsgBackdrop, screenW, screenH);
    lv_obj_align(s_cfgActionMsgBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgActionMsgBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgActionMsgBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgActionMsgBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgActionMsgBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgActionMsgBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgActionMsgBackdrop, 0, 0);
    lv_obj_add_event_cb(s_cfgActionMsgBackdrop,
                        onCfgActionMessageBackdropPressed,
                        LV_EVENT_CLICKED,
                        nullptr);

    s_cfgActionMsgModal = lv_obj_create(s_cfgActionMsgBackdrop);
    lv_obj_set_size(s_cfgActionMsgModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_cfgActionMsgModal, screenH - 14, 0);
    lv_obj_align(s_cfgActionMsgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_cfgActionMsgModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgActionMsgModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_cfgActionMsgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgActionMsgModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgActionMsgModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_cfgActionMsgModal, 8, 0);
    lv_obj_set_style_pad_row(s_cfgActionMsgModal, 6, 0);
    lv_obj_set_flex_flow(s_cfgActionMsgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionMsgModal,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_cfgActionMsgModal);
    lv_obj_set_width(title, contentW);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Action Result");

    lv_obj_t *bodyPanel = lv_obj_create(s_cfgActionMsgModal);
    lv_obj_set_width(bodyPanel, contentW);
    lv_coord_t bodyPanelH = (lv_coord_t)(screenH / 3);
    if (bodyPanelH < 44) bodyPanelH = 44;
    if (bodyPanelH > 96) bodyPanelH = 96;
    lv_obj_set_height(bodyPanel, bodyPanelH);
    lv_obj_add_flag(bodyPanel, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(bodyPanel);
    lv_obj_set_scrollbar_mode(bodyPanel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(bodyPanel, bodyPanelBg, 0);
    lv_obj_set_style_bg_opa(bodyPanel, LV_OPA_60, 0);
    lv_obj_set_style_border_width(bodyPanel, 1, 0);
    lv_obj_set_style_border_color(bodyPanel, modalBorder, 0);
    lv_obj_set_style_pad_left(bodyPanel, 6, 0);
    lv_obj_set_style_pad_right(bodyPanel, 6, 0);
    lv_obj_set_style_pad_top(bodyPanel, 4, 0);
    lv_obj_set_style_pad_bottom(bodyPanel, 4, 0);

    lv_obj_t *body = lv_label_create(bodyPanel);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, bodyFont, 0);
    lv_obj_set_style_text_color(body, bodyTextColor, 0);
    lv_obj_set_style_text_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, displayMsg);

    lv_obj_t *hint = lv_label_create(s_cfgActionMsgModal);
    lv_obj_set_width(hint, contentW);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintTextColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, "%s = Close", modalCloseKeyLabel());

    lv_obj_move_foreground(s_cfgActionMsgBackdrop);
    s_cfgActionMsgOpenedMs = millis();
}

#if !defined(DEVICE_TLORA_PAGER_TFT)
static void closeNodeInfoModal() {
    lvObjDeleteSafe(s_nodeInfoModal);
}

// Shows the current node's identity/radio details in a dismissible popup,
// layered over the CFG modal. Any key (or the close key) dismisses it.
static void openNodeInfoModal() {
    if (!s_rootScreen || s_nodeInfoModal) return;

    char info[14][96] = {};   // 11 device rows + newest/oldest node
    int infoCount = buildDeviceInfoLines(info, 14);

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;

#if defined(DEVICE_TDECK)
    const lv_font_t *bodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif

    s_nodeInfoModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodeInfoModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_nodeInfoModal, lv_disp_get_ver_res(NULL) - 16, 0);
    lv_obj_align(s_nodeInfoModal, LV_ALIGN_CENTER, 0, 0);
    setupVScroll(s_nodeInfoModal);
    lv_obj_set_scrollbar_mode(s_nodeInfoModal, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_nodeInfoModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodeInfoModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodeInfoModal, 1, 0);
    lv_obj_set_style_border_color(s_nodeInfoModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_nodeInfoModal, 6, 0);
    lv_obj_set_style_pad_row(s_nodeInfoModal, 3, 0);
    lv_obj_set_flex_flow(s_nodeInfoModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodeInfoModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_nodeInfoModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Device Info");

    for (int i = 0; i < infoCount; i++) {
        lv_obj_t *row = lv_label_create(s_nodeInfoModal);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, bodyFont, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, info[i]);
    }

    lv_obj_t *hint = lv_label_create(s_nodeInfoModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, bodyFont, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_pad_top(hint, 3, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text_fmt(hint, "Any key / %s = Close", modalCloseKeyLabel());
#elif defined(DEVICE_TDECK)
    // T-Deck has no dedicated Up/Down keys; J/K and the trackball drive scroll.
    lv_label_set_text_fmt(hint, "J/K = Scroll   %s = Close", modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint,
                          "Up/Down/J/K = Scroll   %s = Close",
                          modalCloseKeyLabel());
#endif
}
#endif

// ── Hidden system-stats screen (CPU / memory) ────────────────────────────────
// Builds three ready-to-display section bodies. Kept as separate strings so a
// wide screen can lay them out side by side and a narrow one can stack them.
static void buildSysStatsColumns(char *cpuOut, char *memOut, char *stoOut, size_t sz) {
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t heapFree  = ESP.getFreeHeap();
    const uint32_t heapUsed  = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
    const uint32_t heapPct   = heapTotal ? (uint32_t)((uint64_t)heapUsed * 100 / heapTotal) : 0;
    const uint32_t psramTotal = ESP.getPsramSize();
    const uint32_t psramFree  = ESP.getFreePsram();
    const uint32_t upSec = millis() / 1000UL;

    snprintf(cpuOut, sz,
             "%s x%u\n"
             "Clock: %u MHz\n"
             "Revision: %u\n"
             "Uptime: %luh %02lum %02lus\n"
             "Loop rate: %lu /s",
             ESP.getChipModel(), (unsigned)ESP.getChipCores(),
             (unsigned)ESP.getCpuFreqMHz(),
             (unsigned)ESP.getChipRevision(),
             (unsigned long)(upSec / 3600UL),
             (unsigned long)((upSec / 60UL) % 60UL),
             (unsigned long)(upSec % 60UL),
             (unsigned long)s_loopsPerSec);

    snprintf(memOut, sz,
             "Used: %lu KB (%lu%%)\n"
             "Total: %lu KB\n"
             "Free: %lu KB\n"
             "Min free: %lu KB\n"
             "Max block: %lu KB",
             (unsigned long)(heapUsed / 1024UL), (unsigned long)heapPct,
             (unsigned long)(heapTotal / 1024UL),
             (unsigned long)(heapFree / 1024UL),
             (unsigned long)(ESP.getMinFreeHeap() / 1024UL),
             (unsigned long)(ESP.getMaxAllocHeap() / 1024UL));

    if (psramTotal > 0) {
        snprintf(stoOut, sz,
                 "PSRAM free: %lu KB\n"
                 "PSRAM total: %lu KB\n"
                 "Flash: %lu KB\n"
                 "Sketch: %lu KB\n"
                 "App free: %lu KB",
                 (unsigned long)(psramFree / 1024UL),
                 (unsigned long)(psramTotal / 1024UL),
                 (unsigned long)(ESP.getFlashChipSize() / 1024UL),
                 (unsigned long)(ESP.getSketchSize() / 1024UL),
                 (unsigned long)(ESP.getFreeSketchSpace() / 1024UL));
    } else {
        snprintf(stoOut, sz,
                 "PSRAM: none\n"
                 "Flash: %lu KB\n"
                 "Sketch: %lu KB\n"
                 "App free: %lu KB",
                 (unsigned long)(ESP.getFlashChipSize() / 1024UL),
                 (unsigned long)(ESP.getSketchSize() / 1024UL),
                 (unsigned long)(ESP.getFreeSketchSpace() / 1024UL));
    }
}

static void closeSysStatsModal() {
    lvObjDeleteSafe(s_sysStatsModal);
    s_sysStatsCols[0] = s_sysStatsCols[1] = s_sysStatsCols[2] = nullptr;
    s_sysStatsOpenedMs = 0;
    s_sysStatsLastRefreshMs = 0;
    s_cfgInfoKeyStreak = 0;
}

static void refreshSysStatsModal(bool force) {
    if (!s_sysStatsModal || !s_sysStatsCols[0]) return;
    if (!lvObjValid(s_sysStatsCols[0])) { s_sysStatsCols[0] = nullptr; return; }
    uint32_t now = millis();
    if (!force && (uint32_t)(now - s_sysStatsLastRefreshMs) < 500UL) return;
    s_sysStatsLastRefreshMs = now;

    char cpu[128], mem[128], sto[160];
    buildSysStatsColumns(cpu, mem, sto, sizeof(cpu));

    if (s_sysStatsCols[1] && s_sysStatsCols[2]) {
        // Wide layout: one label per section.
        lv_label_set_text(s_sysStatsCols[0], cpu);
        lv_label_set_text(s_sysStatsCols[1], mem);
        lv_label_set_text(s_sysStatsCols[2], sto);
    } else {
        // Narrow layout: everything stacked in the single scrollable label.
        char body[448];
        snprintf(body, sizeof(body),
                 "CPU\n%s\n\nMEMORY\n%s\n\nSTORAGE\n%s", cpu, mem, sto);
        lv_label_set_text(s_sysStatsCols[0], body);
    }
}

// Builds one titled "card" column inside a flex-row parent and returns its
// body label (the part refreshSysStatsModal updates).
static lv_obj_t *makeSysStatsColumn(lv_obj_t *parent, const char *header,
                                    const lv_font_t *headFont, const lv_font_t *bodyFont) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_height(col, lv_pct(100));
    lv_obj_set_flex_grow(col, 1);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(col, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_50, 0);
    lv_obj_set_style_border_width(col, 1, 0);
    lv_obj_set_style_border_color(col, lv_color_hex(0x2C5A9E), 0);
    lv_obj_set_style_radius(col, 4, 0);
    lv_obj_set_style_pad_all(col, 5, 0);
    lv_obj_set_style_pad_row(col, 3, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *head = lv_label_create(col);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_style_text_font(head, headFont, 0);
    lv_obj_set_style_text_color(head, lv_color_hex(0x6BF0DC), 0);
    lv_label_set_text(head, header);

    lv_obj_t *body = lv_label_create(col);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, bodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9F7F0), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, "");
    return body;
}

static void openSysStatsModal() {
    if (!s_rootScreen || s_sysStatsModal) return;

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    // Wide screens (pager 480, T-Deck 320) get the full-width 3-column layout;
    // narrow screens fall back to a single stacked, scrollable list.
    const bool wide = screenW >= 300;

#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *bodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *bodyFont = &lv_font_montserrat_10;
#endif
    const lv_font_t *headFont = &lv_font_montserrat_12;

    s_sysStatsModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_sysStatsModal, screenW - 8, screenH - 8);
    lv_obj_align(s_sysStatsModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_sysStatsModal, LV_OBJ_FLAG_SCROLLABLE);
    // Distinct teal accent so the hidden screen reads as "not a normal modal".
    lv_obj_set_style_bg_color(s_sysStatsModal, lv_color_hex(0x0B1E45), 0);
    lv_obj_set_style_bg_opa(s_sysStatsModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sysStatsModal, 1, 0);
    lv_obj_set_style_border_color(s_sysStatsModal, lv_color_hex(0x39E0C8), 0);
    lv_obj_set_style_pad_all(s_sysStatsModal, 6, 0);
    lv_obj_set_style_pad_row(s_sysStatsModal, 4, 0);
    lv_obj_set_flex_flow(s_sysStatsModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_sysStatsModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_sysStatsModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x6BF0DC), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "System Stats");

    if (wide) {
        // A flex-row band that grows to fill the space between title and hint,
        // holding three equal-width section cards.
        lv_obj_t *cols = lv_obj_create(s_sysStatsModal);
        lv_obj_set_width(cols, lv_pct(100));
        lv_obj_set_flex_grow(cols, 1);
        lv_obj_clear_flag(cols, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(cols, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cols, 0, 0);
        lv_obj_set_style_pad_all(cols, 0, 0);
        lv_obj_set_style_pad_column(cols, 6, 0);
        lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        s_sysStatsCols[0] = makeSysStatsColumn(cols, "CPU", headFont, bodyFont);
        s_sysStatsCols[1] = makeSysStatsColumn(cols, "MEMORY", headFont, bodyFont);
        s_sysStatsCols[2] = makeSysStatsColumn(cols, "STORAGE", headFont, bodyFont);
    } else {
        lv_obj_t *panel = lv_obj_create(s_sysStatsModal);
        lv_obj_set_width(panel, lv_pct(100));
        lv_obj_set_flex_grow(panel, 1);
        setupVScroll(panel);
        lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);

        s_sysStatsCols[0] = lv_label_create(panel);
        lv_obj_set_width(s_sysStatsCols[0], lv_pct(100));
        lv_obj_set_style_text_font(s_sysStatsCols[0], bodyFont, 0);
        lv_obj_set_style_text_color(s_sysStatsCols[0], lv_color_hex(0xD9F7F0), 0);
        lv_label_set_long_mode(s_sysStatsCols[0], LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_sysStatsCols[0], "");
        s_sysStatsCols[1] = nullptr;
        s_sysStatsCols[2] = nullptr;
    }

    lv_obj_t *hint = lv_label_create(s_sysStatsModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x7FD8CC), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, "Any key / %s = Close", modalCloseKeyLabel());

    s_sysStatsOpenedMs = millis();
    lv_obj_move_foreground(s_sysStatsModal);
    refreshSysStatsModal(true);
    Serial.println("[lvgl-cfg] system-stats easter egg opened");
}

static void closeLegendModal() {
    lvObjDeleteSafe(s_legendModal);
    refreshChatComposeButtonState();
}

static void onLegendClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeLegendModal();
}

static void onHeltecBottomNavPressed(lv_event_t *e) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    switch (target) {
        case HELTEC_NAV_CFG:
            if (s_cfgModal) closeCfgModal();
            else openCfgModal();
            break;
        case HELTEC_NAV_DM:
            if (s_dmModal) closeDmModal();
            else openDmModal();
            break;
        case HELTEC_NAV_NODES:
            if (s_nodesModal) closeNodesModal();
            else openNodesModal();
            break;
        case HELTEC_NAV_LIVE:
            if (s_liveModal) closeLiveModal();
            else openLiveModal();
            break;
        case HELTEC_NAV_ACTIONS:
            if (s_channelActionsModal) closeChannelActionsModal();
            else openChannelActionsModal();
            break;
        case HELTEC_NAV_LEGEND:
            if (s_legendModal) closeLegendModal();
            else openLegendModal();
            break;
        default:
            break;
    }
#else
    LV_UNUSED(e);
#endif
}

static lv_color_t chatPanelBackgroundColor() {
    if (s_cfg.uiMode == UI_MODE_LIGHT) {
        return lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, 52));
    }
    return lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, 34));
}

static void populateHeltecBottomNav(lv_obj_t *bar, int activeTarget) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!bar) return;

    struct NavItem {
        const char *label;
        int target;
    };
    static const NavItem kItems[] = {
#if defined(DEVICE_UI_VERTICAL)
        {"Cfg", HELTEC_NAV_CFG},
        {"DM", HELTEC_NAV_DM},
        {"Nodes", HELTEC_NAV_NODES},
        {"Live", HELTEC_NAV_LIVE},
        {"Act", HELTEC_NAV_ACTIONS},
        {"Help", HELTEC_NAV_LEGEND},
#else
        {"Config", HELTEC_NAV_CFG},
        {"DM", HELTEC_NAV_DM},
        {"Nodes", HELTEC_NAV_NODES},
        {"Live", HELTEC_NAV_LIVE},
        {"Actions", HELTEC_NAV_ACTIONS},
        {"Help", HELTEC_NAV_LEGEND},
#endif
    };

#if defined(DEVICE_UI_VERTICAL)
    const int barPadX = 1;
    const int barPadY = 1;
    const int barPadCol = 1;
    const int btnPad = 0;
#else
    const int barPadX = 2;
    const int barPadY = 1;
    const int barPadCol = 2;
    const int btnPad = 1;
#endif

    lv_obj_set_style_pad_left(bar, barPadX, 0);
    lv_obj_set_style_pad_right(bar, barPadX, 0);
    lv_obj_set_style_pad_top(bar, barPadY, 0);
    lv_obj_set_style_pad_bottom(bar, barPadY, 0);
    lv_obj_set_style_pad_column(bar, barPadCol, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const lv_color_t navBgColor = chatPanelBackgroundColor();
    const lv_color_t navTextColor = lv_color_hex(0xD9E8FF);

    for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, lv_pct(100));
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, btnPad, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        const bool isActive = (activeTarget == kItems[i].target);
        lv_obj_set_style_bg_color(
            btn,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? navBgColor
                                            : (isActive ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x16386F)),
            0);
        lv_obj_set_style_bg_opa(btn, isActive ? LV_OPA_80 : LV_OPA_60, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn,
                                      isActive ? lv_color_hex(0xE8F1FF) : lv_color_hex(0x335D9D),
                                      0);

        lv_obj_add_event_cb(btn,
                            onHeltecBottomNavPressed,
                            LV_EVENT_PRESSED,
                            (void *)(intptr_t)kItems[i].target);

        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(label, navTextColor, 0);
        lv_label_set_text(label, kItems[i].label);
        lv_obj_center(label);
    }
#else
    LV_UNUSED(bar);
    LV_UNUSED(activeTarget);
#endif
}

static void appendHeltecBottomNav(lv_obj_t *parent, int activeTarget) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!parent) return;

    // Match the height used by the main chat screen's shortcut bar
    // (chatLegendH == 28) so the nav buttons render at the exact same
    // width/height inside DM/Cfg/Nodes/Live as on the home screen.
    const int navBarHeight = 28;

    // Modal containers all use border_width=1 + pad_all=4. The bar needs to
    // span the full display width and sit flush with the bottom edge, so it
    // is placed with IGNORE_LAYOUT and offset back through the parent pad +
    // border. A transparent spacer keeps the flex column reserving the same
    // vertical room the bar would otherwise occupy.
    const int padInset = 4;
    const int borderInset = 1;
    const int contentReserve = navBarHeight - padInset - borderInset;

    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_height(spacer, contentReserve > 0 ? contentReserve : 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(bar, lv_disp_get_hor_res(NULL));
    lv_obj_set_height(bar, navBarHeight);
    // BOTTOM_LEFT anchor with negative x cancels parent pad+border on the left,
    // positive y pushes through pad+border on the bottom — bar ends up at the
    // device edges identical to the home-screen shortcut bar.
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT,
                 -(padInset + borderInset),
                 padInset + borderInset);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x335D9D), 0);

    populateHeltecBottomNav(bar, activeTarget);
#else
    LV_UNUSED(parent);
    LV_UNUSED(activeTarget);
#endif
}

static bool isDigitChar(char c) {
    return (c >= '0' && c <= '9');
}

static bool liveTimestampAndBody(const char *s, char *tsOut, size_t tsOutLen, const char **bodyOut) {
    if (!s) s = "";
    if (tsOut && tsOutLen > 0) tsOut[0] = '\0';
    if (!bodyOut) return false;

    while (*s == ' ') s++;
    *bodyOut = s;

    size_t len = strlen(s);
    if (len < 6) return false;
    bool hhmm = isDigitChar(s[0]) && isDigitChar(s[1]) &&
                s[2] == ':' &&
                isDigitChar(s[3]) && isDigitChar(s[4]) &&
                s[5] == ' ';
    bool unset = (s[0] == '-' && s[1] == '-' && s[2] == ':' &&
                  s[3] == '-' && s[4] == '-' && s[5] == ' ');
    if (hhmm || unset) {
        if (tsOut && tsOutLen >= 6) {
            memcpy(tsOut, s, 5);
            tsOut[5] = '\0';
        }
        s += 6;
        while (*s == ' ') s++;
        *bodyOut = s;
        return true;
    }
    return false;
}

static const char *livePortLabel(const char *tag) {
    if (!tag || !tag[0]) return "data";
    if (strcmp(tag, "T") == 0) return "text";
    if (strcmp(tag, "N") == 0) return "nodeinfo";
    if (strcmp(tag, "P") == 0) return "position";
    if (strcmp(tag, "E") == 0) return "telemetry";
    if (strcmp(tag, "R") == 0) return "traceroute";
    if (strcmp(tag, "A") == 0) return "routing";
    return "data";
}

static const char *liveDestLabel(const char *tag) {
    if (!tag || !tag[0]) return "node";
    if (strcmp(tag, "B") == 0 || strcmp(tag, "BCAST") == 0) return "broadcast";
    if (strcmp(tag, "M") == 0) return "me";
    if (strcmp(tag, "U") == 0) return "node";
    return "node";
}

static const char *liveTxResultLabel(const char *tag) {
    if (!tag || !tag[0]) return "unknown";
    if (strcmp(tag, "OK") == 0) return "sent";
    if (strcmp(tag, "ER") == 0) return "failed";
    if (strcmp(tag, "NR") == 0) return "radio not ready";
    return tag;
}

static const char *liveTelemetryKindLabel(const char *tag) {
    if (!tag || !tag[0]) return "telemetry";
    if (strcmp(tag, "D") == 0) return "device telemetry";
    if (strcmp(tag, "E") == 0) return "environment telemetry";
    return "telemetry";
}

static const char *routingErrorName(uint32_t errorReason) {
    switch (errorReason) {
        case 0:  return "NONE";
        case 1:  return "NO_ROUTE";
        case 2:  return "GOT_NAK";
        case 3:  return "TIMEOUT";
        case 4:  return "NO_INTERFACE";
        case 5:  return "MAX_RETRANSMIT";
        case 6:  return "NO_CHANNEL";
        case 7:  return "TOO_LARGE";
        case 8:  return "NO_RESPONSE";
        case 9:  return "DUTY_CYCLE_LIMIT";
        case 35: return "PKI_UNKNOWN_PUBKEY";
        default: return nullptr;
    }
}

static void formatLiveLineText(const DisplayLine &dl, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    out[0] = '\0';

    char ts[6];
    const char *body = "";
    liveTimestampAndBody(dl.text, ts, sizeof(ts), &body);

    char who[20] = {0};
    char dst[8] = {0};
    char tag[12] = {0};
    char stat[12] = {0};
    char id[16] = {0};
    int ch = -1;

    char hashHex[4] = {0};
    if (sscanf(body, "R %19[^>]>%7s %11s c%d h%3s", who, dst, tag, &ch, hashHex) == 5) {
        if (ts[0]) snprintf(out, outLen, "%s RX %s from %s to %s on ch%d hash %s",
                            ts, livePortLabel(tag), who, liveDestLabel(dst), ch, hashHex);
        else snprintf(out, outLen, "RX %s from %s to %s on ch%d hash %s",
                      livePortLabel(tag), who, liveDestLabel(dst), ch, hashHex);
        return;
    }

    if (sscanf(body, "R %19[^>]>%7s %11s c%d", who, dst, tag, &ch) == 4) {
        if (ts[0]) snprintf(out, outLen, "%s RX %s from %s to %s on ch%d",
                            ts, livePortLabel(tag), who, liveDestLabel(dst), ch);
        else snprintf(out, outLen, "RX %s from %s to %s on ch%d",
                      livePortLabel(tag), who, liveDestLabel(dst), ch);
        return;
    }

    uint32_t reqId = 0;
    uint32_t err = 0;
    if (sscanf(body, "R ACK %19s %8lx h%3s", who, &reqId, hashHex) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s RX routing ACK from %s req:%08lX hash:%s",
                            ts, who, (unsigned long)reqId, hashHex);
        else snprintf(out, outLen, "RX routing ACK from %s req:%08lX hash:%s",
                      who, (unsigned long)reqId, hashHex);
        return;
    }

    if (sscanf(body, "R NAK %19s %8lx err%lu h%3s", who, &reqId, &err, hashHex) == 4) {
        const char *errName = routingErrorName(err);
        if (ts[0]) {
            if (errName) snprintf(out, outLen, "%s RX routing NAK from %s req:%08lX %s(%lu) hash:%s",
                                  ts, who, (unsigned long)reqId, errName, (unsigned long)err, hashHex);
            else snprintf(out, outLen, "%s RX routing NAK from %s req:%08lX err:%lu hash:%s",
                          ts, who, (unsigned long)reqId, (unsigned long)err, hashHex);
        } else {
            if (errName) snprintf(out, outLen, "RX routing NAK from %s req:%08lX %s(%lu) hash:%s",
                                  who, (unsigned long)reqId, errName, (unsigned long)err, hashHex);
            else snprintf(out, outLen, "RX routing NAK from %s req:%08lX err:%lu hash:%s",
                          who, (unsigned long)reqId, (unsigned long)err, hashHex);
        }
        return;
    }

    if (sscanf(body, "R %19s ENC %11s", who, stat) == 2) {
        if (ts[0]) snprintf(out, outLen, "%s RX encrypted packet from %s (hash %s)",
                            ts, who, stat);
        else snprintf(out, outLen, "RX encrypted packet from %s (hash %s)",
                      who, stat);
        return;
    }

    if (sscanf(body, "T ACK %19s %11s", who, stat) == 2) {
        if (ts[0]) snprintf(out, outLen, "%s TX routing ACK to %s (%s)",
                            ts, who, stat);
        else snprintf(out, outLen, "TX routing ACK to %s (%s)", who, stat);
        return;
    }

    if (sscanf(body, "T TXT %7s c%d %15s", dst, &ch, id) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX text to %s on ch%d id:%s",
                            ts, liveDestLabel(dst), ch, id);
        else snprintf(out, outLen, "TX text to %s on ch%d id:%s",
                      liveDestLabel(dst), ch, id);
        return;
    }

    if (sscanf(body, "T TXT %7s NR", dst) == 1) {
        if (ts[0]) snprintf(out, outLen, "%s TX text to %s not sent (radio not ready)",
                            ts, liveDestLabel(dst));
        else snprintf(out, outLen, "TX text to %s not sent (radio not ready)",
                      liveDestLabel(dst));
        return;
    }

    if (sscanf(body, "T TXT %7s ER", dst) == 1) {
        if (ts[0]) snprintf(out, outLen, "%s TX text to %s FAILED",
                            ts, liveDestLabel(dst));
        else snprintf(out, outLen, "TX text to %s FAILED", liveDestLabel(dst));
        return;
    }

    if (sscanf(body, "T POS %7s %15s %11s", dst, id, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX position to %s id:%s (%s)",
                            ts, liveDestLabel(dst), id, stat);
        else snprintf(out, outLen, "TX position to %s id:%s (%s)",
                      liveDestLabel(dst), id, stat);
        return;
    }

    if (sscanf(body, "T NOD %7s %19s %11s", dst, who, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX nodeinfo %s to %s (%s)",
                            ts,
                            (strcmp(dst, "U") == 0) ? "unicast" : "broadcast",
                            who,
                            stat);
        else snprintf(out, outLen, "TX nodeinfo %s to %s (%s)",
                      (strcmp(dst, "U") == 0) ? "unicast" : "broadcast",
                      who,
                      stat);
        return;
    }

    if (sscanf(body, "T TEL %7s %15s %11s", dst, id, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX %s to broadcast id:%s (%s)",
                            ts,
                            liveTelemetryKindLabel(dst),
                            id,
                            liveTxResultLabel(stat));
        else snprintf(out, outLen, "TX %s to broadcast id:%s (%s)",
                      liveTelemetryKindLabel(dst),
                      id,
                      liveTxResultLabel(stat));
        return;
    }

    if (sscanf(body, "T NBR %7s %15s %11s", dst, id, stat) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX neighborhood info to %s id:%s (%s)",
                            ts,
                            liveDestLabel(dst),
                            id,
                            liveTxResultLabel(stat));
        else snprintf(out, outLen, "TX neighborhood info to %s id:%s (%s)",
                      liveDestLabel(dst),
                      id,
                      liveTxResultLabel(stat));
        return;
    }

    if (sscanf(body, "T DM %11s %19s %15s", tag, who, id) == 3) {
        if (ts[0]) snprintf(out, outLen, "%s TX DM to %s via %s id:%s",
                            ts, who, tag, id);
        else snprintf(out, outLen, "TX DM to %s via %s id:%s", who, tag, id);
        return;
    }

    if (sscanf(body, "T DM ER %11s", stat) == 1) {
        if (ts[0]) snprintf(out, outLen, "%s TX DM FAILED (%s)", ts, stat);
        else snprintf(out, outLen, "TX DM FAILED (%s)", stat);
        return;
    }

    snprintf(out, outLen, "%s", dl.text);
}

enum LiveTrafficClass {
    LIVE_TRAFFIC_DEFAULT = 0,
    LIVE_TRAFFIC_ERROR,
    LIVE_TRAFFIC_TX_ACK,
    LIVE_TRAFFIC_TX_TEXT,
    LIVE_TRAFFIC_TX_NODE,
    LIVE_TRAFFIC_TX_POS,
    LIVE_TRAFFIC_TX_TLM,
    LIVE_TRAFFIC_TX_DM,
    LIVE_TRAFFIC_RX_ACK,
    LIVE_TRAFFIC_RX_TEXT,
    LIVE_TRAFFIC_RX_NODE,
    LIVE_TRAFFIC_RX_POS,
    LIVE_TRAFFIC_RX_TLM,
    LIVE_TRAFFIC_RX_ENC,
    LIVE_TRAFFIC_RX_OTHER,
    LIVE_TRAFFIC_TX_OTHER,
};

static LiveTrafficClass classifyLiveTraffic(const DisplayLine &dl) {
    const char *body = "";
    liveTimestampAndBody(dl.text, nullptr, 0, &body);
    if (!body[0]) return LIVE_TRAFFIC_DEFAULT;

    if (strstr(body, " ER") || strncmp(body, "R NAK", 5) == 0) return LIVE_TRAFFIC_ERROR;
    if (strncmp(body, "T ACK", 5) == 0) return LIVE_TRAFFIC_TX_ACK;
    if (strncmp(body, "R ACK", 5) == 0) return LIVE_TRAFFIC_RX_ACK;
    if (strncmp(body, "T DM", 4) == 0) return LIVE_TRAFFIC_TX_DM;
    if (strncmp(body, "R ", 2) == 0 && strstr(body, " ENC ")) return LIVE_TRAFFIC_RX_ENC;

    char who[20] = {0};
    char dst[8] = {0};
    char tag[12] = {0};
    int ch = -1;
    if (sscanf(body, "R %19[^>]>%7s %11s c%d", who, dst, tag, &ch) == 4) {
        if (strcmp(tag, "T") == 0) return LIVE_TRAFFIC_RX_TEXT;
        if (strcmp(tag, "N") == 0) return LIVE_TRAFFIC_RX_NODE;
        if (strcmp(tag, "P") == 0) return LIVE_TRAFFIC_RX_POS;
        if (strcmp(tag, "E") == 0) return LIVE_TRAFFIC_RX_TLM;
        return LIVE_TRAFFIC_RX_OTHER;
    }

    if (strncmp(body, "T TXT", 5) == 0) return LIVE_TRAFFIC_TX_TEXT;
    if (strncmp(body, "T NOD", 5) == 0) return LIVE_TRAFFIC_TX_NODE;
    if (strncmp(body, "T POS", 5) == 0) return LIVE_TRAFFIC_TX_POS;
    if (strncmp(body, "T TLM", 5) == 0 || strncmp(body, "T TEL", 5) == 0) return LIVE_TRAFFIC_TX_TLM;
    if (strncmp(body, "R ", 2) == 0) return LIVE_TRAFFIC_RX_OTHER;
    if (strncmp(body, "T ", 2) == 0) return LIVE_TRAFFIC_TX_OTHER;

    return LIVE_TRAFFIC_DEFAULT;
}

static inline uint16_t userMessageAccentColor565() {
    // User-chosen override from the config color picker takes precedence.
    if (s_cfg.userMsgColor < kUserMsgColorCount) {
        return kUserMsgColors[s_cfg.userMsgColor].color;
    }
    // Default: keep classic yellow in dark mode, but use darker amber in light
    // mode for readable contrast against bright backgrounds.
    return (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x7A, 0x4C, 0x00) : TFT_YELLOW;
}

static uint16_t liveLineTrafficColor(const DisplayLine &dl) {
    LiveTrafficClass cls = classifyLiveTraffic(dl);
    switch (cls) {
        case LIVE_TRAFFIC_ERROR:   return TFT_RED;
        case LIVE_TRAFFIC_TX_ACK:  return TFT_GREEN;
        case LIVE_TRAFFIC_RX_ACK:  return (uint16_t)0x57EA;
        case LIVE_TRAFFIC_TX_TEXT: return userMessageAccentColor565();
        case LIVE_TRAFFIC_TX_NODE: return (uint16_t)0xF39C;
        case LIVE_TRAFFIC_TX_POS:  return (uint16_t)0xFD20;
        case LIVE_TRAFFIC_TX_TLM:  return (uint16_t)0x07FF;
        case LIVE_TRAFFIC_TX_DM:   return TFT_MAGENTA;
        case LIVE_TRAFFIC_RX_TEXT: return TFT_CYAN;
        case LIVE_TRAFFIC_RX_NODE: return (uint16_t)0x7DFF;
        case LIVE_TRAFFIC_RX_POS:  return (uint16_t)0xFDF0;
        case LIVE_TRAFFIC_RX_TLM:  return (uint16_t)0xA7EA;
        case LIVE_TRAFFIC_RX_ENC:  return TFT_ORANGE;
        case LIVE_TRAFFIC_RX_OTHER:return (uint16_t)0x9DFF;
        case LIVE_TRAFFIC_TX_OTHER:return userMessageAccentColor565();
        case LIVE_TRAFFIC_DEFAULT:
        default:
            break;
    }

    return (dl.color == TFT_DARKGREY) ? TFT_WHITE : dl.color;
}

static lv_color_t liveLineBgColor(const DisplayLine &dl) {
    LiveTrafficClass cls = classifyLiveTraffic(dl);
    switch (cls) {
        case LIVE_TRAFFIC_ERROR:    return lv_color_hex(0x4A1D1D);
        case LIVE_TRAFFIC_TX_ACK:   return lv_color_hex(0x1E3E27);
        case LIVE_TRAFFIC_RX_ACK:   return lv_color_hex(0x1B3E34);
        case LIVE_TRAFFIC_TX_TEXT:  return lv_color_hex(0x4A4318);
        case LIVE_TRAFFIC_TX_NODE:  return lv_color_hex(0x4A2D1F);
        case LIVE_TRAFFIC_TX_POS:   return lv_color_hex(0x4A3418);
        case LIVE_TRAFFIC_TX_TLM:   return lv_color_hex(0x1A3B40);
        case LIVE_TRAFFIC_TX_DM:    return lv_color_hex(0x33224A);
        case LIVE_TRAFFIC_RX_TEXT:  return lv_color_hex(0x12345D);
        case LIVE_TRAFFIC_RX_NODE:  return lv_color_hex(0x1D2E58);
        case LIVE_TRAFFIC_RX_POS:   return lv_color_hex(0x1A3754);
        case LIVE_TRAFFIC_RX_TLM:   return lv_color_hex(0x1A3A3E);
        case LIVE_TRAFFIC_RX_ENC:   return lv_color_hex(0x4A3618);
        case LIVE_TRAFFIC_RX_OTHER: return lv_color_hex(0x102D52);
        case LIVE_TRAFFIC_TX_OTHER: return lv_color_hex(0x3E3619);
        case LIVE_TRAFFIC_DEFAULT:
        default:
            return lv_color_hex(0x10254A);
    }
}

static lv_color_t liveListBackdropColor() {
    if (s_cfg.uiMode == UI_MODE_LIGHT) {
        // Keep live-log canvas dark in light mode for text readability.
        return lv_color_make(0x0F, 0x2A, 0x5C);
    }
    return lv_color_hex(0x0F2A5C);
}

static lv_opa_t liveListBackdropOpa() {
    return (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_COVER : LV_OPA_50;
}

static lv_color_t tftColorToLv(uint16_t c) {
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
    return lv_color_make(r, g, b);
}

static void closeLiveModal() {
    if (s_liveModal && lv_obj_is_valid(s_liveModal)) {
        lv_obj_del(s_liveModal);
    }
    s_liveModal = nullptr;
    s_liveList = nullptr;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
}

static void chartPushSample(ChartHist &h, float value) {
    if (h.count < ChartHist::CAP) {
        h.v[(h.head + h.count) % ChartHist::CAP] = value;
        h.count++;
    } else {
        h.v[h.head] = value;
        h.head = (h.head + 1) % ChartHist::CAP;
    }
    h.lastVal = value;
    h.hasLast = true;
    h.seq++;
}

static float chartSampleAt(const ChartHist &h, int index) {
    // index 0 = oldest, index count-1 = newest.
    if (index < 0 || index >= h.count) return 0.0f;
    return h.v[(h.head + index) % ChartHist::CAP];
}

// ── Web-config snapshot getters (see web_config.h) ─────────────
static void chartFillSnapshot(const ChartHist &h, WebChartSnapshot &out) {
    static_assert(ChartHist::CAP == WebChartSnapshot::CAP,
                  "ChartHist::CAP must match WebChartSnapshot::CAP");
    out.count = h.count;
    out.hasLast = h.hasLast;
    out.lastVal = h.lastVal;
    for (int i = 0; i < h.count && i < WebChartSnapshot::CAP; i++) {
        out.values[i] = chartSampleAt(h, i);
    }
    for (int i = h.count; i < WebChartSnapshot::CAP; i++) {
        out.values[i] = 0.0f;
    }
}
void webChartSnapshotChUtil(WebChartSnapshot &out)  { chartFillSnapshot(s_chUtilHist,  out); }
void webChartSnapshotAirUtil(WebChartSnapshot &out) { chartFillSnapshot(s_airUtilHist, out); }
void webChartSnapshotSnr(WebChartSnapshot &out)     { chartFillSnapshot(s_snrHist,     out); }
void webChartSnapshotRssi(WebChartSnapshot &out)    { chartFillSnapshot(s_rssiHist,    out); }

static void closeChUtilChartModal() {
    if (s_chUtilChartModal && lv_obj_is_valid(s_chUtilChartModal)) {
        lv_obj_del(s_chUtilChartModal);
    }
    s_chUtilChartModal = nullptr;
    s_chUtilChart = nullptr;
    s_chUtilSeries = nullptr;
    s_airUtilSeries = nullptr;
    s_chUtilStatsLabel = nullptr;
    s_chUtilRenderedSeq = 0;
    s_airUtilRenderedSeq = 0;
}

static void closeSnrRssiChartModal() {
    if (s_snrChartModal && lv_obj_is_valid(s_snrChartModal)) {
        lv_obj_del(s_snrChartModal);
    }
    s_snrChartModal = nullptr;
    s_snrChart = nullptr;
    s_snrSeries = nullptr;
    s_rssiSeries = nullptr;
    s_snrStatsLabel = nullptr;
    s_snrRenderedSeq = 0;
    s_rssiRenderedSeq = 0;
}

static void closeDmModal() {
    closeDmNodePicker();
    lvObjDeleteSafe(s_dmModal);
    s_dmConvPanel = nullptr;
    s_dmConvList = nullptr;
    s_dmMsgPanel = nullptr;
    s_dmMsgList = nullptr;
    s_dmHintLabel = nullptr;
    s_dmConvCount = 0;
    s_dmSelection = -1;
    s_dmRenderedConvCount = -1;
    s_dmRenderedNodeId = 0;
    s_dmRenderedMsgCount = -1;
    s_dmRenderedUnreadTotal = -1;
    s_dmMsgPanelFocused = false;
    s_dmDeletePendingNodeId = 0;
    s_dmDeleteConfirmUntilMs = 0;
    s_dmDeleteFlashMsg[0] = '\0';
    s_dmDeleteFlashUntilMs = 0;
    s_dmTouchPressStartMs = 0;
    s_dmTouchPressRowIdx = -1;
    s_dmTouchLongPressTriggered = false;
    memset(s_dmConvRows, 0, sizeof(s_dmConvRows));
    memset(s_dmConvNodeIds, 0, sizeof(s_dmConvNodeIds));
}

static void closeNodesModal() {
    closeNodesFilterDialog();
    closeNodesActionMenu();
    lvObjDeleteSafe(s_nodesModal);
    nodesPanelWifiRestore();
    s_nodesMapSelectionNodeId = 0;
    s_nodesMapSelectionSinceMs = 0;
    s_nodesMapLastPrimePollMs = 0;
    s_nodesInfoPanel = nullptr;
    s_nodesDetail = nullptr;
    s_nodesDetailExtra = nullptr;
    s_nodesMapPanel = nullptr;
    s_nodesMapTitle = nullptr;
    s_nodesMapCoords = nullptr;
    s_nodesMapMarker = nullptr;
    s_nodesMapTileLayer = nullptr;
    s_nodesMapImage = nullptr;
    s_nodesTitleLabel = nullptr;
    s_nodesHintLabel = nullptr;
    s_nodesFilterBtn = nullptr;
    s_nodesFilterDialog = nullptr;
    s_nodesFilterInput = nullptr;
    s_nodesFilterKeyboard = nullptr;
    s_nodesMapImageSrc[0] = '\0';
    s_nodesList = nullptr;
    s_nodesListRowCount = 0;
    s_nodesSnapshotCount = 0;
    s_nodesFilteredCount = 0;
    s_nodesSelected = -1;
    s_nodesFilterOpen = false;
    s_nodesFilterLen = 0;
    s_nodesFilter[0] = '\0';
    memset(s_nodesFilteredIdx, 0, sizeof(s_nodesFilteredIdx));
    s_nodesActionSelection = 0;
    s_nodesActionNodeId = 0;
    memset(s_nodesActionRows, 0, sizeof(s_nodesActionRows));
    memset(s_nodesListRows, 0, sizeof(s_nodesListRows));
}

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

static void snapshotNodesForModal() {
    s_nodesSnapshotCount = 0;
    s_nodesFilteredCount = 0;
    s_nodesSelected = -1;
    s_nodesFilterOpen = false;
    s_nodesFilterLen = 0;
    s_nodesFilter[0] = '\0';

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

    nodesApplyFilter();
}

static void nodesApplyFilter() {
    s_nodesFilteredCount = 0;

    for (int i = 0; i < s_nodesSnapshotCount && s_nodesFilteredCount < MAX_NODES; i++) {
        const uint32_t nodeId = s_nodesSnapshotIds[i];
        // Resolved live; null if the node was evicted since the snapshot, in
        // which case only its id is still matchable.
        const NodeEntry *n = Nodes.find(nodeId);
        bool match = true;

        if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
            char nodeIdText[16];
            snprintf(nodeIdText, sizeof(nodeIdText), "!%08lX", (unsigned long)nodeId);
            match = dmNodePickerContainsNoCase(nodeIdText, s_nodesFilter)
                 || (n && dmNodePickerContainsNoCase(n->longName, s_nodesFilter))
                 || (n && dmNodePickerContainsNoCase(n->shortName, s_nodesFilter));
        }

        if (match) {
            s_nodesFilteredIdx[s_nodesFilteredCount++] = i;
        }
    }

    if (s_nodesFilteredCount <= 0) {
        s_nodesSelected = -1;
        return;
    }

    if (s_nodesSelected < 0) {
        s_nodesSelected = 0;
    } else if (s_nodesSelected >= s_nodesFilteredCount) {
        s_nodesSelected = s_nodesFilteredCount - 1;
    }
}

static void applyNodesFilterText(const char *text) {
    if (!text) text = "";

    size_t len = strlen(text);
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

    nodesApplyFilter();
    refreshNodesListRows();
    refreshNodesListSelection();
    refreshNodesDetails();

    if (s_nodesSelected >= 0
        && s_nodesSelected < s_nodesListRowCount
        && s_nodesListRows[s_nodesSelected]) {
        lv_obj_scroll_to_view(s_nodesListRows[s_nodesSelected], LV_ANIM_OFF);
    }
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

static void openNodesFilterDialog() {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!s_nodesModal || s_nodesFilterDialog) return;

    int dialogW = lv_disp_get_hor_res(NULL);
    int dialogH = lv_disp_get_ver_res(NULL);
    if (dialogW < 180) dialogW = lv_obj_get_width(s_nodesModal);
    if (dialogH < 120) dialogH = lv_obj_get_height(s_nodesModal);

    s_nodesFilterDialog = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodesFilterDialog, dialogW, dialogH);
    lv_obj_align(s_nodesFilterDialog, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(s_nodesFilterDialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_nodesFilterDialog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_nodesFilterDialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_bg_color(s_nodesFilterDialog, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodesFilterDialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodesFilterDialog, 1, 0);
    lv_obj_set_style_border_color(s_nodesFilterDialog, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_nodesFilterDialog, 4, 0);
    lv_obj_set_style_pad_row(s_nodesFilterDialog, 3, 0);
    lv_obj_set_flex_flow(s_nodesFilterDialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesFilterDialog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_nodesFilterDialog);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Filter Nodes");

    s_nodesFilterInput = lv_textarea_create(s_nodesFilterDialog);
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
    lv_textarea_set_placeholder_text(s_nodesFilterInput, "Type to filter nodes");
    if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
        lv_textarea_set_text(s_nodesFilterInput, s_nodesFilter);
    } else {
        lv_textarea_set_text(s_nodesFilterInput, "");
    }
    lv_textarea_set_cursor_pos(s_nodesFilterInput, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_event_cb(s_nodesFilterInput, onNodesFilterInputEvent, LV_EVENT_READY, nullptr);

    s_nodesFilterKeyboard = lv_keyboard_create(s_nodesFilterDialog);
    lv_obj_set_width(s_nodesFilterKeyboard, lv_pct(100));
    lv_obj_set_flex_grow(s_nodesFilterKeyboard, 1);
    lv_keyboard_set_mode(s_nodesFilterKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_nodesFilterKeyboard, s_nodesFilterInput);
    lv_obj_add_event_cb(s_nodesFilterKeyboard, onNodesFilterKeyboardEvent, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_nodesFilterKeyboard, onNodesFilterKeyboardEvent, LV_EVENT_CANCEL, nullptr);
    lv_obj_move_foreground(s_nodesFilterDialog);
#endif
}

static void onNodesFilterButtonPressed(lv_event_t *e) {
    LV_UNUSED(e);
    openNodesFilterDialog();
}

static bool nodesSnapshotContains(uint32_t nodeId) {
    if (nodeId == 0) return false;
    for (int i = 0; i < s_nodesSnapshotCount; i++) {
        if (s_nodesSnapshotIds[i] == nodeId) return true;
    }
    return false;
}

static bool nodesPanelCanDownloadTiles() {
    if (!kStateMapsEnabled) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_AP;
}

static void nodesPanelWifiEnter() {
    if (!kStateMapsEnabled) return;
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
#if HAS_SD_CARD
    return sdBegin();
#else
    return false;
#endif
}

static void *nodesMapFsOpenCb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    LV_UNUSED(mode);
#if HAS_SD_CARD
    if (!path || !path[0]) return nullptr;
    if (!sdBegin()) return nullptr;

    String fullPath;
    if (path[0] == '/') fullPath = path;
    else {
        fullPath = "/";
        fullPath += path;
    }

    File *f = new File(SD.open(fullPath.c_str(), FILE_READ));
    if (!*f) {
        delete f;
        return nullptr;
    }
    return f;
#else
    LV_UNUSED(path);
    return nullptr;
#endif
}

static lv_fs_res_t nodesMapFsCloseCb(lv_fs_drv_t *drv, void *file_p) {
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    File *f = (File *)file_p;
    if (*f) f->close();
    delete f;
    return LV_FS_RES_OK;
}

static lv_fs_res_t nodesMapFsReadCb(lv_fs_drv_t *drv, void *file_p,
                                    void *buf, uint32_t btr, uint32_t *br) {
    LV_UNUSED(drv);
    if (!file_p || !buf) return LV_FS_RES_INV_PARAM;
    File *f = (File *)file_p;
    size_t n = f->read((uint8_t *)buf, (size_t)btr);
    if (br) *br = (uint32_t)n;
    return LV_FS_RES_OK;
}

static lv_fs_res_t nodesMapFsSeekCb(lv_fs_drv_t *drv, void *file_p,
                                    uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    if (!file_p) return LV_FS_RES_INV_PARAM;
    File *f = (File *)file_p;

    size_t base = 0;
    if (whence == LV_FS_SEEK_CUR) base = f->position();
    else if (whence == LV_FS_SEEK_END) base = f->size();

    size_t target = base + (size_t)pos;
    if (!f->seek((uint32_t)target)) return LV_FS_RES_FS_ERR;
    return LV_FS_RES_OK;
}

static lv_fs_res_t nodesMapFsTellCb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p) {
    LV_UNUSED(drv);
    if (!file_p || !pos_p) return LV_FS_RES_INV_PARAM;
    File *f = (File *)file_p;
    *pos_p = (uint32_t)f->position();
    return LV_FS_RES_OK;
}

static void nodesMapInitFsDriver() {
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
#if HAS_SD_CARD
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
#else
    LV_UNUSED(path);
    return false;
#endif
}

struct UsStateMapSpec {
    const char *code;
    float latMin;
    float latMax;
    float lonMin;
    float lonMax;
};

static constexpr UsStateMapSpec kUsStateMaps[] = {
    {"AL", 30.1f, 35.1f, -88.5f, -84.9f}, {"AK", 51.2f, 71.5f, -170.0f, -129.9f},
    {"AZ", 31.3f, 37.0f, -114.9f, -109.0f}, {"AR", 33.0f, 36.5f, -94.6f, -89.6f},
    {"CA", 32.5f, 42.1f, -124.5f, -114.1f}, {"CO", 37.0f, 41.0f, -109.1f, -102.0f},
    {"CT", 40.9f, 42.1f, -73.8f, -71.8f}, {"DE", 38.4f, 39.9f, -75.8f, -75.0f},
    {"FL", 24.4f, 31.1f, -87.7f, -80.0f}, {"GA", 30.3f, 35.0f, -85.6f, -80.8f},
    {"HI", 18.9f, 22.3f, -160.5f, -154.8f}, {"ID", 41.9f, 49.1f, -117.3f, -111.0f},
    {"IL", 36.9f, 42.5f, -91.6f, -87.0f}, {"IN", 37.8f, 41.8f, -88.1f, -84.8f},
    {"IA", 40.3f, 43.6f, -96.7f, -90.1f}, {"KS", 37.0f, 40.1f, -102.1f, -94.6f},
    {"KY", 36.5f, 39.2f, -89.7f, -82.9f}, {"LA", 28.9f, 33.1f, -94.1f, -88.8f},
    {"ME", 43.0f, 47.5f, -71.2f, -66.9f}, {"MD", 37.9f, 39.8f, -79.6f, -75.0f},
    {"MA", 41.2f, 42.9f, -73.6f, -69.9f}, {"MI", 41.7f, 48.3f, -90.5f, -82.1f},
    {"MN", 43.5f, 49.4f, -97.3f, -89.5f}, {"MS", 30.1f, 35.0f, -91.7f, -88.1f},
    {"MO", 35.9f, 40.7f, -95.9f, -89.1f}, {"MT", 44.3f, 49.1f, -116.1f, -104.0f},
    {"NE", 39.9f, 43.1f, -104.1f, -95.3f}, {"NV", 35.0f, 42.1f, -120.1f, -114.0f},
    {"NH", 42.7f, 45.4f, -72.6f, -70.6f}, {"NJ", 38.9f, 41.4f, -75.6f, -73.9f},
    {"NM", 31.3f, 37.0f, -109.1f, -103.0f}, {"NY", 40.4f, 45.1f, -79.8f, -71.8f},
    {"NC", 33.8f, 36.7f, -84.4f, -75.4f}, {"ND", 45.9f, 49.1f, -104.1f, -96.5f},
    {"OH", 38.4f, 42.3f, -84.9f, -80.5f}, {"OK", 33.6f, 37.1f, -103.0f, -94.4f},
    {"OR", 41.9f, 46.3f, -124.7f, -116.5f}, {"PA", 39.7f, 42.5f, -80.6f, -74.7f},
    {"RI", 41.1f, 42.1f, -71.9f, -71.1f}, {"SC", 32.0f, 35.2f, -83.4f, -78.5f},
    {"SD", 42.5f, 45.9f, -104.1f, -96.4f}, {"TN", 34.9f, 36.7f, -90.4f, -81.6f},
    {"TX", 25.8f, 36.6f, -106.7f, -93.5f}, {"UT", 37.0f, 42.1f, -114.1f, -109.0f},
    {"VT", 42.7f, 45.1f, -73.5f, -71.5f}, {"VA", 36.5f, 39.5f, -83.7f, -75.2f},
    {"WA", 45.5f, 49.1f, -124.9f, -116.9f}, {"WV", 37.2f, 40.7f, -82.7f, -77.7f},
    {"WI", 42.4f, 47.3f, -92.9f, -86.8f}, {"WY", 41.0f, 45.1f, -111.1f, -104.0f},
};

static constexpr int kUsStateMapCount =
    (int)(sizeof(kUsStateMaps) / sizeof(kUsStateMaps[0]));
static constexpr int kStateMapImageW = 240;
static constexpr int kStateMapImageH = 160;
static constexpr const char *kStateMapCacheVersion = "v2";
static constexpr const char *kStateMapMarkerPath = "/camillia/state_maps/state_maps.complete";
static constexpr const char *kStateMapLegacyMarkerPath = "/camillia/state_maps/.complete";

static String nodesStateMapPath(const char *stateCode) {
    String p = "/camillia/state_maps/";
    p += stateCode;
    p += ".png";
    return p;
}

static String nodesStateMapMetaPath(const char *stateCode) {
    String p = "/camillia/state_maps/";
    p += stateCode;
    p += ".meta";
    return p;
}

static bool nodesFileLooksLikePng(const char *path) {
#if !HAS_SD_CARD
    LV_UNUSED(path);
    return false;
#else
    if (!path || !path[0]) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    if (f.size() < 64) {
        f.close();
        return false;
    }

    uint8_t hdr[24] = {0};
    size_t n = f.read(hdr, sizeof(hdr));
    f.close();
    if (n != sizeof(hdr)) return false;

    static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0) return false;
    // Ensure first chunk type is IHDR.
    return hdr[12] == 'I' && hdr[13] == 'H' && hdr[14] == 'D' && hdr[15] == 'R';
#endif
}

static bool nodesStateMapMarkerMatchesVersion() {
#if !HAS_SD_CARD
    return false;
#else
    if (!SD.exists(kStateMapMarkerPath)) return false;

    File marker = SD.open(kStateMapMarkerPath, FILE_READ);
    if (!marker) return false;
    String line = marker.readStringUntil('\n');
    marker.close();
    line.trim();
    return line.equals(kStateMapCacheVersion);
#endif
}

static void nodesResetStateMapCacheIfStale() {
#if HAS_SD_CARD
    if (!sdBegin()) return;
    bool hasCurrentMarker = SD.exists(kStateMapMarkerPath);
    bool hasLegacyMarker = SD.exists(kStateMapLegacyMarkerPath);
    if (!hasCurrentMarker && !hasLegacyMarker) return;
    if (nodesStateMapMarkerMatchesVersion()) return;

    Serial.println("[map] state-map cache version changed; clearing cached maps");
    sdRmDirRecursive("/camillia/state_maps");
    nodesMapEnsureDir("/camillia");
    nodesMapEnsureDir("/camillia/state_maps");
    if (SD.exists(kStateMapLegacyMarkerPath)) SD.remove(kStateMapLegacyMarkerPath);
#endif
}

static bool nodesWriteStateMapMeta(const char *stateCode,
                                   float latMin, float latMax,
                                   float lonMin, float lonMax) {
#if !HAS_SD_CARD
    LV_UNUSED(stateCode);
    LV_UNUSED(latMin);
    LV_UNUSED(latMax);
    LV_UNUSED(lonMin);
    LV_UNUSED(lonMax);
    return false;
#else
    String p = nodesStateMapMetaPath(stateCode);
    if (SD.exists(p.c_str())) SD.remove(p.c_str());
    File f = SD.open(p.c_str(), FILE_WRITE);
    if (!f) return false;
    f.printf("%.6f,%.6f,%.6f,%.6f\n", (double)latMin, (double)latMax, (double)lonMin, (double)lonMax);
    f.close();
    return true;
#endif
}

static bool nodesReadStateMapMeta(const char *stateCode,
                                  float &latMin, float &latMax,
                                  float &lonMin, float &lonMax) {
#if !HAS_SD_CARD
    LV_UNUSED(stateCode);
    LV_UNUSED(latMin);
    LV_UNUSED(latMax);
    LV_UNUSED(lonMin);
    LV_UNUSED(lonMax);
    return false;
#else
    String p = nodesStateMapMetaPath(stateCode);
    File f = SD.open(p.c_str(), FILE_READ);
    if (!f) return false;
    String line = f.readStringUntil('\n');
    f.close();

    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
    if (sscanf(line.c_str(), "%lf,%lf,%lf,%lf", &a, &b, &c, &d) != 4) return false;
    latMin = (float)a;
    latMax = (float)b;
    lonMin = (float)c;
    lonMax = (float)d;
    return true;
#endif
}

static const UsStateMapSpec *nodesStateForCoords(float lat, float lon) {
    const UsStateMapSpec *best = nullptr;
    float bestArea = 1e9f;
    for (int i = 0; i < kUsStateMapCount; i++) {
        const UsStateMapSpec &s = kUsStateMaps[i];
        if (lat < s.latMin || lat > s.latMax || lon < s.lonMin || lon > s.lonMax) continue;
        float area = (s.latMax - s.latMin) * (s.lonMax - s.lonMin);
        if (!best || area < bestArea) {
            best = &s;
            bestArea = area;
        }
    }
    return best;
}

static bool nodesStateMapCacheComplete() {
#if !HAS_SD_CARD
    return false;
#else
    if (!sdBegin()) return false;
    if (!nodesStateMapMarkerMatchesVersion()) return false;
    for (int i = 0; i < kUsStateMapCount; i++) {
        String p = nodesStateMapPath(kUsStateMaps[i].code);
        if (!SD.exists(p.c_str())) return false;
        if (!nodesFileLooksLikePng(p.c_str())) return false;
    }
    return true;
#endif
}

static bool nodesDownloadStateMap(const UsStateMapSpec &s,
                                  bool staticHostResolvable,
                                  bool tileHostResolvable) {
    // On-device state maps are disabled (kStateMapsEnabled == false) and the
    // Nodes map panel is never built, so this never ran. Its HTTPS tile /
    // staticmap downloads were the last TLS consumer in the firmware; they are
    // removed so WiFiClientSecure can be dropped entirely. Maps live in the web
    // config UI, which the browser renders and fetches tiles for.
    LV_UNUSED(s);
    LV_UNUSED(staticHostResolvable);
    LV_UNUSED(tileHostResolvable);
    return false;
}

static void bootstrapStateMapsIfMissing() {
    if (s_stateMapCacheReady) return;
    if (s_stateMapBootstrapDone) return;

    if (!kStateMapsEnabled) {
        s_stateMapBootstrapTried = true;
        s_stateMapBootstrapDone = true;
        return;
    }

#if !HAS_SD_CARD
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
        if (!SD.exists(p.c_str())) break;
        if (!nodesFileLooksLikePng(p.c_str())) {
            SD.remove(p.c_str());
            String meta = nodesStateMapMetaPath(kUsStateMaps[s_stateMapBootstrapNextIndex].code);
            if (SD.exists(meta.c_str())) SD.remove(meta.c_str());
            break;
        }
        s_stateMapBootstrapNextIndex++;
    }

    if (s_stateMapBootstrapNextIndex >= kUsStateMapCount) {
        Serial.printf("[map] state bootstrap downloaded=%d failed=%d\n",
                      s_stateMapBootstrapDownloaded,
                      s_stateMapBootstrapFailed);

        if (s_stateMapBootstrapFailed == 0) {
            if (SD.exists(kStateMapMarkerPath)) SD.remove(kStateMapMarkerPath);
            File marker = SD.open(kStateMapMarkerPath, FILE_WRITE);
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
                               int &markerX, int &markerY) {
    markerX = x0 + (w / 2) - 3;
    markerY = y0 + (h / 2) - 3;
    if (!s_nodesMapTileLayer || !s_nodesMapImage) return 0;

    if (!kStateMapsEnabled) {
        lv_obj_add_flag(s_nodesMapImage, LV_OBJ_FLAG_HIDDEN);
        return 0;
    }

    lv_obj_set_pos(s_nodesMapTileLayer, x0, y0);
    lv_obj_set_size(s_nodesMapTileLayer, w, h);
    lv_obj_add_flag(s_nodesMapImage, LV_OBJ_FLAG_HIDDEN);

#if !HAS_SD_CARD
    LV_UNUSED(lat);
    LV_UNUSED(lon);
    return 0;
#else
    const UsStateMapSpec *state = nodesStateForCoords(lat, lon);
    if (!state) return 0;
    if (!sdBegin()) return 0;

    String diskPath = nodesStateMapPath(state->code);
    if (!SD.exists(diskPath.c_str())) {
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
        SD.remove(diskPath.c_str());
        String meta = nodesStateMapMetaPath(state->code);
        if (SD.exists(meta.c_str())) SD.remove(meta.c_str());
        s_stateMapCacheReady = false;
        s_stateMapBootstrapDone = false;
        Serial.printf("[map] invalid PNG removed: %s\n", diskPath.c_str());
        return 0;
    }

    snprintf(s_nodesMapImageSrc, sizeof(s_nodesMapImageSrc), "S:%s", diskPath.c_str());

    uint16_t srcW = kStateMapImageW;
    uint16_t srcH = kStateMapImageH;
    lv_img_header_t header;
    lv_res_t headerRes = lv_img_decoder_get_info(s_nodesMapImageSrc, &header);
    if (headerRes == LV_RES_OK && header.w > 0 && header.h > 0) {
        srcW = header.w;
        srcH = header.h;
    } else {
        SD.remove(diskPath.c_str());
        String meta = nodesStateMapMetaPath(state->code);
        if (SD.exists(meta.c_str())) SD.remove(meta.c_str());
        s_stateMapCacheReady = false;
        s_stateMapBootstrapDone = false;
        Serial.printf("[map] decode failed, removed: %s\n", diskPath.c_str());
        return 0;
    }

    lv_img_set_src(s_nodesMapImage, s_nodesMapImageSrc);

    uint16_t zoomW = (uint16_t)max(16, (w * 256) / (int)srcW);
    uint16_t zoomH = (uint16_t)max(16, (h * 256) / (int)srcH);
    uint16_t zoom = min(zoomW, zoomH);
    lv_img_set_zoom(s_nodesMapImage, zoom);

    int drawW = ((int)srcW * (int)zoom) / 256;
    int drawH = ((int)srcH * (int)zoom) / 256;

    // Anchor transforms at top-left and size object to rendered bounds so
    // clipping/layout remain predictable in the map layer.
    lv_img_set_pivot(s_nodesMapImage, 0, 0);
    int drawX = (w - drawW) / 2;
    int drawY = (h - drawH) / 2;
    lv_obj_set_size(s_nodesMapImage, drawW, drawH);
    lv_obj_set_pos(s_nodesMapImage, drawX, drawY);
    lv_obj_clear_flag(s_nodesMapImage, LV_OBJ_FLAG_HIDDEN);

    float latMin = state->latMin;
    float latMax = state->latMax;
    float lonMin = state->lonMin;
    float lonMax = state->lonMax;
    nodesReadStateMapMeta(state->code, latMin, latMax, lonMin, lonMax);

    float xNorm = (lon - lonMin) / (lonMax - lonMin);
    float yNorm = (latMax - lat) / (latMax - latMin);
    if (xNorm < 0.0f) xNorm = 0.0f;
    if (xNorm > 1.0f) xNorm = 1.0f;
    if (yNorm < 0.0f) yNorm = 0.0f;
    if (yNorm > 1.0f) yNorm = 1.0f;

    markerX = x0 + drawX + (int)(xNorm * (float)drawW) - 3;
    markerY = y0 + drawY + (int)(yNorm * (float)drawH) - 3;
    return 1;
#endif
}

static const NodeEntry *currentNodesSelection() {
    if (s_nodesFilteredCount <= 0 || s_nodesSelected < 0 || s_nodesSelected >= s_nodesFilteredCount) {
        return nullptr;
    }
    int snapshotIdx = s_nodesFilteredIdx[s_nodesSelected];
    if (snapshotIdx < 0 || snapshotIdx >= s_nodesSnapshotCount) return nullptr;
    // Resolved live against NodeDB; null if evicted since the snapshot.
    return Nodes.find(s_nodesSnapshotIds[snapshotIdx]);
}

static void refreshNodesMap(const NodeEntry *node) {
    if (!s_nodesMapPanel || !s_nodesMapTitle || !s_nodesMapMarker) return;

    bool hasNodePos = node && (node->latI != 0 || node->lonI != 0);
    float lat = (float)MY_LAT_I / 10000000.0f;
    float lon = (float)MY_LON_I / 10000000.0f;
    if (hasNodePos) {
        lat = (float)node->latI / 10000000.0f;
        lon = (float)node->lonI / 10000000.0f;
    }

    int mapW = lv_obj_get_width(s_nodesMapPanel) - 8;
    int mapH = lv_obj_get_height(s_nodesMapPanel) - 22;
    if (mapW < 10) mapW = 10;
    if (mapH < 10) mapH = 10;

    int markerX = -1;
    int markerY = -1;
    int visibleTiles = nodesMapRenderTiles(lat, lon, 4, 20, mapW, mapH, markerX, markerY);

    if (s_nodesMapCoords) {
        if (visibleTiles <= 0) {
            lv_obj_clear_flag(s_nodesMapCoords, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(s_nodesMapCoords, LV_ALIGN_CENTER, 0, 2);
            if (!kStateMapsEnabled) {
                lv_label_set_text(s_nodesMapCoords, "State map disabled");
            } else {
                lv_label_set_text(s_nodesMapCoords,
                                  (!s_stateMapBootstrapTried || s_stateMapBootstrapInProgress)
                                      ? "Preparing state maps..."
                                      : "State map unavailable");
            }
        } else {
            lv_obj_add_flag(s_nodesMapCoords, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (hasNodePos && markerX >= 0 && markerY >= 0) {
        lv_obj_set_pos(s_nodesMapMarker, markerX, markerY);
        lv_obj_clear_flag(s_nodesMapMarker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_nodesMapMarker, LV_OBJ_FLAG_HIDDEN);
    }

    if (!kStateMapsEnabled) {
        lv_label_set_text(s_nodesMapTitle, "State Map (off)");
    } else {
        lv_label_set_text(s_nodesMapTitle, visibleTiles > 0 ? "State Map" : "State Map (loading)");
    }
}

static void sdRmDirRecursive(const char *path) {
#if HAS_SD_CARD
    if (!path || !path[0]) return;
    File dir = SD.open(path);
    if (!dir) {
        SD.remove(path);
        return;
    }
    if (!dir.isDirectory()) {
        dir.close();
        SD.remove(path);
        return;
    }

    while (true) {
        File child = dir.openNextFile();
        if (!child) break;

        String childPath = String(path);
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += child.name();
        bool childIsDir = child.isDirectory();
        child.close();

        if (childIsDir) {
            sdRmDirRecursive(childPath.c_str());
        } else {
            SD.remove(childPath.c_str());
        }
    }

    dir.close();
    SD.rmdir(path);
#else
    LV_UNUSED(path);
#endif
}

static void clearNodeDbOnSd() {
#if HAS_SD_CARD
    if (!sdBegin()) return;

    const char *nodeFiles[] = {
        "/camillia/nodes.db",
        "/camillia/nodes.bin",
        "/camillia/nodes.json",
        "/camillia/node_db.bin",
        "/camillia/node_db.json",
    };
    for (size_t i = 0; i < sizeof(nodeFiles) / sizeof(nodeFiles[0]); i++) {
        if (SD.exists(nodeFiles[i])) {
            SD.remove(nodeFiles[i]);
        }
    }

    if (SD.exists("/camillia/nodes")) {
        sdRmDirRecursive("/camillia/nodes");
    }
#endif
}

static void refreshNodesListRows() {
    if (!lvObjAlive(s_nodesList)) {
        s_nodesList = nullptr;
        s_nodesListRowCount = 0;
        memset(s_nodesListRows, 0, sizeof(s_nodesListRows));
        return;
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *nodesListFont = emojiFont(&lv_font_montserrat_12);
    const int nodesListRowH = 28;
#else
    const lv_font_t *nodesListFont = emojiFont(&lv_font_montserrat_10);
    const int nodesListRowH = 22;
#endif

    if (s_nodesTitleLabel) {
        int nodeCount = s_nodesSnapshotCount;
        if (s_nodesFilterOpen) {
            // Brackets appear as soon as the filter is armed (even empty) so the
            // user has a visual cue that filtering is on; text fills in as typed.
            char titleText[64];
            snprintf(titleText, sizeof(titleText), "NODES [%s] (%d)",
                     s_nodesFilter, nodeCount);
            lv_label_set_text(s_nodesTitleLabel, titleText);
        } else {
            char titleText[32];
            snprintf(titleText, sizeof(titleText), "NODES (%d)", nodeCount);
            lv_label_set_text(s_nodesTitleLabel, titleText);
        }
    }

    if (s_nodesHintLabel) {
        if (s_nodesFilterOpen) {
#if defined(DEVICE_TDECK)
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "Type=Filter  J/K=Select  Enter=Actions  Bksp=Edit/Exit  %s=Back",
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "Type=Filter  Up/Down=Select  Enter=Actions  Bksp=Edit/Exit  %s=Back",
                                  modalCloseKeyLabel());
#endif
        } else {
#if defined(DEVICE_TDECK)
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "J/K=Select  Enter=Actions  Space=Filter  %s=Back",
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "Up/Down=Select  Enter=Actions  Space=Filter  %s=Back",
                                  modalCloseKeyLabel());
#endif
        }
    }

    lv_obj_clean(s_nodesList);
    s_nodesListRowCount = 0;
    memset(s_nodesListRows, 0, sizeof(s_nodesListRows));

    if (s_nodesFilteredCount <= 0) {
        lv_obj_t *empty = lv_label_create(s_nodesList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, nodesListFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
            char noMatch[64];
            snprintf(noMatch, sizeof(noMatch), "No matches for: %s", s_nodesFilter);
            lv_label_set_text(empty, noMatch);
        } else {
            lv_label_set_text(empty, "No nodes seen");
        }
        return;
    }

    for (int rowIdx = 0; rowIdx < s_nodesFilteredCount; rowIdx++) {
        int snapshotIdx = s_nodesFilteredIdx[rowIdx];
        if (snapshotIdx < 0 || snapshotIdx >= s_nodesSnapshotCount) continue;
        // Resolve live. If the node was evicted while this modal was open we
        // still emit the row (from its id) so rows stay 1:1 with the filtered
        // list — dropping one here would desync selection and row indices.
        const uint32_t nodeId = s_nodesSnapshotIds[snapshotIdx];
        const NodeEntry *n = Nodes.find(nodeId);
        char nodeIdText[12];
        snprintf(nodeIdText, sizeof(nodeIdText), "!%08lX", (unsigned long)nodeId);

        lv_obj_t *row = lv_btn_create(s_nodesList);
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

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, nodesListFont, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);

        char rowText[56];
        snprintf(rowText,
             sizeof(rowText),
             "%s%s",
             (n && n->favorite) ? "* " : "",
             !n ? nodeIdText : (n->shortName[0] ? n->shortName : "----"));
        setLabelTextEmojiSafe(lbl, rowText);
        lv_obj_center(lbl);

        if (s_nodesListRowCount < MAX_NODES) {
            s_nodesListRows[s_nodesListRowCount++] = row;
        }
    }
}

static void refreshNodesListSelection() {
    for (int i = 0; i < s_nodesListRowCount; i++) {
        lv_obj_t *row = s_nodesListRows[i];
        if (!row) continue;
        bool selected = (i == s_nodesSelected);
        lv_color_t rowTextColor = lv_color_hex(0xE8F1FF);
        if (s_cfg.uiMode != UI_MODE_LIGHT) {
            // Dark themes: keep node names brighter for better list readability.
            rowTextColor = selected ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xF2F8FF);
        }
        lv_obj_set_style_bg_color(row, selected ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266), 0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selected ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
        lv_obj_set_style_text_color(row, rowTextColor, 0);

        lv_obj_t *rowLabel = lv_obj_get_child(row, 0);
        if (rowLabel) {
            lv_obj_set_style_text_color(rowLabel, rowTextColor, 0);
        }
    }
}

static void refreshNodesDetails() {
    if (!lvObjAlive(s_nodesDetail)) {
        s_nodesDetail = nullptr;
        s_nodesDetailExtra = nullptr;
        return;
    }

    const NodeEntry *selectedNode = currentNodesSelection();
    if (!selectedNode) {
        lv_label_set_text(s_nodesDetail, "No nodes seen yet.");
        if (s_nodesDetailExtra) lv_label_set_text(s_nodesDetailExtra, "");
        return;
    }

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
        snprintf(heard, sizeof(heard), "%lus ago", (unsigned long)(ageMs / 1000UL));
    } else {
        snprintf(heard, sizeof(heard), "unknown");
    }

    char pos[96];
    if (n.hasPosition && (n.latI != 0 || n.lonI != 0)) {
        float lat = (float)n.latI / 10000000.0f;
        float lon = (float)n.lonI / 10000000.0f;
        snprintf(pos, sizeof(pos), "Lat: %.5f\nLon: %.5f\nAlt: %ld m",
                 (double)lat,
                 (double)lon,
                 (long)n.alt);
    } else {
        snprintf(pos, sizeof(pos), "No position data");
    }

    const bool useImperial = (s_cfg.displayUnits != 0);
    char telem[220];
    if (n.hasTelemetry) {
        telem[0] = '\0';
        if (n.hasDeviceTelemetry) {
            snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem),
                     "Battery: %.0f%%\nVoltage: %.2f V\nChUtil: %.1f%%\nAirTx: %.1f%%",
                     (double)n.battPct,
                     (double)n.voltage,
                     (double)n.chUtil,
                     (double)n.airUtil);
        }
        if (n.hasEnvironmentTelemetry) {
            if (telem[0]) {
                snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem), "\n");
            }
            if (useImperial) {
                float tempF = n.temperatureC * (9.0f / 5.0f) + 32.0f;
                float pressureInHg = n.pressureHpa * 0.0295299831f;
                snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem),
                         "Temp: %.1f F\nHumidity: %.1f%%\nPressure: %.2f inHg",
                         (double)tempF,
                         (double)n.humidityPct,
                         (double)pressureInHg);
            } else {
                snprintf(telem + strlen(telem), sizeof(telem) - strlen(telem),
                         "Temp: %.1f C\nHumidity: %.1f%%\nPressure: %.1f hPa",
                         (double)n.temperatureC,
                         (double)n.humidityPct,
                         (double)n.pressureHpa);
            }
        }
        if (!telem[0]) {
            snprintf(telem, sizeof(telem), "No telemetry data");
        }
    } else {
        snprintf(telem, sizeof(telem), "No telemetry data");
    }

    if (s_nodesDetailExtra) {
        char leftBuf[320];
        char rightBuf[320];
        snprintf(leftBuf, sizeof(leftBuf),
                 "Name: %s\n"
                 "Short: %s\n"
                 "ID: !%08X\n"
                 "Last heard: %s\n"
                 "SNR: %.1f dB\n"
                 "Hops: %u\n"
                 "Channel: %d",
                 name,
                 shortName,
                 n.nodeId,
                 heard,
                 (double)n.snr,
                 (unsigned)n.hops,
                 n.chanIdx);

        snprintf(rightBuf, sizeof(rightBuf),
                 "Position:\n%s\n"
                 "\n"
                 "Telemetry:\n%s",
                 pos,
                 telem);

        setLabelTextEmojiSafe(s_nodesDetail, leftBuf);
        setLabelTextEmojiSafe(s_nodesDetailExtra, rightBuf);
    } else {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "Name: %s\n"
                 "Short: %s\n"
                 "ID: !%08X\n"
                 "Last heard: %s\n"
                 "SNR: %.1f dB\n"
                 "Hops: %u\n"
                 "Channel: %d\n"
                 "\n"
                 "Position:\n%s\n"
                 "\n"
                 "Telemetry:\n%s",
                 name,
                 shortName,
                 n.nodeId,
                 heard,
                 (double)n.snr,
                 (unsigned)n.hops,
                 n.chanIdx,
                 pos,
                 telem);

        setLabelTextEmojiSafe(s_nodesDetail, buf);
    }
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
    s_nodesSelected = idx;
    refreshNodesListSelection();
    refreshNodesDetails();
    if (idx >= 0 && idx < s_nodesListRowCount && s_nodesListRows[idx]) {
        lv_obj_scroll_to_view(s_nodesListRows[idx], LV_ANIM_OFF);
    }
}

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
}

static void onTracerouteBackdropPressed(lv_event_t *e) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (lv_event_get_target(e) == s_tracerouteBackdrop) {
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
    if (!payload || payloadLen == 0) return;

    constexpr int kMaxPathNodes = 12;
    constexpr int kMaxEdges = 7;
    uint32_t route[kMaxPathNodes] = {};
    uint32_t routeBack[kMaxPathNodes] = {};
    int routeCount = 0;
    int routeBackCount = 0;

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
            if (j + sz > len) return 0;
            return j + sz;
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
            if (j + sz > payloadLen) break;
            size_t end = j + sz;
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

        i = skipPb(payload, payloadLen, i, wtype);
        if (!i) break;
    }

    struct Edge {
        uint32_t from;
        uint32_t to;
        bool viaMqtt;
    };
    Edge edges[kMaxEdges] = {};
    int edgeCount = 0;

    auto appendEdges = [&](const uint32_t *path, int count, bool viaMqtt) {
        for (int idx = 0; idx + 1 < count && edgeCount < kMaxEdges; idx++) {
            edges[edgeCount++] = {path[idx], path[idx + 1], viaMqtt};
        }
    };

    appendEdges(routeBack, routeBackCount, viaMqtt);

    if (edgeCount == 0 && routeCount > 0 && s_myNodeId != 0) {
        edges[edgeCount++] = {s_myNodeId, route[0], false};
    }

    // Meshtastic direct traceroute replies can have empty route arrays.
    if (edgeCount == 0 && s_myNodeId != 0 && s_tracerouteNodeId != 0) {
        edges[edgeCount++] = {s_myNodeId, s_tracerouteNodeId, false};
        if (edgeCount < kMaxEdges) {
            edges[edgeCount++] = {s_tracerouteNodeId, s_myNodeId, viaMqtt};
        }
    }

    char text[420];
    text[0] = '\0';

    for (int idx = 0; idx < edgeCount; idx++) {
        char from[20];
        char to[20];
        liveNodeLabel(edges[idx].from, from, sizeof(from), true);
        liveNodeLabel(edges[idx].to, to, sizeof(to), true);

        const char *sep = "";
        if (idx > 0) {
            sep = ((idx % 3) == 0) ? "\n" : ", ";
        }

        const char *transport = edges[idx].viaMqtt ? "mqtt" : "radio";

        size_t used = strlen(text);
        if (used + 8 >= sizeof(text)) break;
        snprintf(text + used, sizeof(text) - used, "%s%s -> %s (%s)", sep, from, to, transport);
    }

    if (!text[0]) {
        strncpy(text, "No hop path in reply", sizeof(text) - 1);
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
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
    lv_label_set_text(title, "Traceroute");

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
    tracerouteProgressSetStatus("Sending request...", lv_color_hex(0xE8F1FF));

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
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    char hintText[56];
    snprintf(hintText, sizeof(hintText), "%s=Close  Tap outside=Close", modalCloseKeyLabel());
    lv_label_set_text(hint, hintText);
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_label_set_text(hint, "Bksp=Close");
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
        tracerouteProgressSetStatus("Send failed", lv_color_hex(0xFF8080));
        return;
    }

    tracerouteProgressSetStatus("Sent. Waiting for route ACK...", lv_color_hex(0xE8F1FF));
}

static void tracerouteProgressOnRouting(uint32_t fromNode, uint32_t requestId, uint32_t errorReason,
                                        const uint8_t *routeReplyPayload,
                                        size_t routeReplyLen,
                                        bool viaMqtt) {
    if (!s_tracerouteModal) return;
    if (requestId == 0 || requestId != s_traceroutePacketId) return;

    char who[20];
    liveNodeLabel(fromNode, who, sizeof(who), false);

    if (errorReason == 0) {
        if (routeReplyPayload && routeReplyLen > 0) {
            s_tracerouteAwaitingRouting = false;
            s_tracerouteAwaitingReply = false;
            char status[72];
            snprintf(status, sizeof(status), "Reply from %s", who);
            tracerouteProgressSetStatus(status, lv_color_hex(0xB8FFB8));
            tracerouteProgressRenderRoutesPayload(routeReplyPayload, routeReplyLen, viaMqtt);
            return;
        }

        s_tracerouteAwaitingRouting = false;
        s_tracerouteAwaitingReply = true;
        char status[72];
        snprintf(status, sizeof(status), "ACK from %s. Waiting for reply...", who);
        tracerouteProgressSetStatus(status, lv_color_hex(0xE8F1FF));
        return;
    }

    s_tracerouteAwaitingRouting = false;
    s_tracerouteAwaitingReply = false;
    const char *errName = routingErrorName(errorReason);
    char status[80];
    if (errName) {
        snprintf(status, sizeof(status), "NAK %s(%lu)", errName, (unsigned long)errorReason);
    } else {
        snprintf(status, sizeof(status), "NAK err=%lu", (unsigned long)errorReason);
    }
    tracerouteProgressSetStatus(status, lv_color_hex(0xFF8080));
}

static void tracerouteProgressOnResponse(const MeshPacket &pkt) {
    if (!s_tracerouteModal) return;
    if (!s_tracerouteAwaitingRouting && !s_tracerouteAwaitingReply) return;
    if (s_tracerouteNodeId != 0 && pkt.hdr.from != s_tracerouteNodeId) return;

    s_tracerouteAwaitingRouting = false;
    s_tracerouteAwaitingReply = false;
    char who[20];
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    uint32_t elapsedMs = (s_tracerouteStartedMs != 0) ? (millis() - s_tracerouteStartedMs) : 0;
    char status[72];
    snprintf(status, sizeof(status), "Reply from %s (%lus)", who, (unsigned long)(elapsedMs / 1000UL));
    tracerouteProgressSetStatus(status, lv_color_hex(0xB8FFB8));
    tracerouteProgressRenderRoutes(pkt);
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
        uint8_t cipher[96];
        size_t protoLen = encodeTracerouteRequest(proto, sizeof(proto), true);
        if (protoLen > 0) {
            const ChannelKey &ck = CHANNEL_KEYS[0];  // LongFast
            if (encryptPayload(packetId, s_myNodeId, ck.key, ck.keyLen, proto, cipher, protoLen)) {
                uint8_t frame[sizeof(MeshHdr) + sizeof(cipher)];
                MeshHdr hdr = {};
                hdr.to = toNodeId;
                hdr.from = s_myNodeId;
                hdr.id = packetId;
                hdr.channel = ck.hash;
                hdr.flags = (uint8_t)((1 << 3) |  // want_ack
                                      (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                                      ((MESH_HOP_LIMIT & 0x07) << 5));
                hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

                memcpy(frame, &hdr, sizeof(hdr));
                memcpy(frame + sizeof(hdr), cipher, protoLen);
                ok = Radio.transmit(frame, sizeof(hdr) + protoLen);
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
    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t selectedBg = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    const lv_color_t idleBg = isLight ? lv_color_hex(0xEEF4FF) : lv_color_hex(0x123266);
    const lv_color_t selectedBorder = isLight ? lv_color_hex(0x6B86B7) : lv_color_hex(0x90B4FF);
    const lv_color_t idleBorder = isLight ? lv_color_hex(0xA9BEDF) : lv_color_hex(0x2B4D8C);

    for (int i = 0; i < kNodesActionCount; i++) {
        lv_obj_t *row = s_nodesActionRows[i];
        if (!row) continue;
        bool selected = (i == s_nodesActionSelection);
        lv_obj_set_style_bg_color(row, selected ? selectedBg : idleBg, 0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : (isLight ? LV_OPA_90 : LV_OPA_40), 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, selected ? selectedBorder : idleBorder, 0);
    }
}

static void closeNodesActionMenu() {
    lvObjDeleteSafe(s_nodesActionModal);
    s_nodesActionSelection = 0;
    s_nodesActionNodeId = 0;
    memset(s_nodesActionRows, 0, sizeof(s_nodesActionRows));
}

static void executeNodesActionSelection() {
    if (s_nodesActionNodeId == 0) {
        closeNodesActionMenu();
        return;
    }

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
                    s_dmMsgPanelFocused = false;
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
            (void)Channels.sendPositionRequest(s_myNodeId, nodeId);
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
    if (action < 0 || action >= kNodesActionCount) return;
    s_nodesActionSelection = action;
    refreshNodesActionMenuSelection();
    executeNodesActionSelection();
}

static void openNodesActionMenu() {
    if (!s_nodesModal) return;
    const NodeEntry *selected = currentNodesSelection();
    if (!selected || selected->nodeId == 0) return;

    if (s_nodesActionModal) {
        closeNodesActionMenu();
    }

    s_nodesActionNodeId = selected->nodeId;
    s_nodesActionSelection = 0;

    const int modalW = min(230, lv_disp_get_hor_res(NULL) - 14);
    const int modalMaxH = lv_disp_get_ver_res(NULL) - 10;

    lv_obj_t *actionParent = s_rootScreen ? s_rootScreen : s_nodesModal;
    s_nodesActionModal = lv_obj_create(actionParent);
    lv_obj_set_width(s_nodesActionModal, modalW);
    lv_obj_set_height(s_nodesActionModal, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_nodesActionModal, modalMaxH, 0);
    lv_obj_align(s_nodesActionModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodesActionModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_nodesActionModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodesActionModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodesActionModal, 1, 0);
    lv_obj_set_style_border_color(s_nodesActionModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_nodesActionModal, 4, 0);
    lv_obj_set_style_pad_row(s_nodesActionModal, 4, 0);
    lv_obj_set_flex_flow(s_nodesActionModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesActionModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_nodesActionModal);

    lv_obj_t *title = lv_label_create(s_nodesActionModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Node Actions");

    // Buttons arranged 2-per-row. Each label embeds its keyboard shortcut in
    // parens using the (T)raceroute inline style so touch and keyboard builds
    // share the exact same labels.
    const bool selectedIsFavorite = selected->favorite;
    const bool selectedIsIgnored = Ignored.contains(selected->nodeId);
    const char *kActionLabels[kNodesActionCount] = {
        "(T)raceroute",
        "Sen(d) DM",
        selectedIsFavorite ? "Un(f)avorite" : "(F)avorite",
        "Request (I)nfo",
        "Request (P)osition",
        selectedIsIgnored ? "Uni(g)nore" : "I(g)nore",
    };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                    ? lv_color_hex(0x13233D)
                                    : lv_color_hex(0xD9E8FF);

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

    for (int i = 0; i < kNodesActionCount; i++) {
        lv_obj_t *row = lv_btn_create(grid);
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
        lv_label_set_text(lbl, kActionLabels[i]);
        lv_obj_center(lbl);
    }

    refreshNodesActionMenuSelection();
}

// ── Channel Actions modal ─────────────────────────────────────
static void refreshChannelActionsModal() {
    if (!s_channelActionsMuteLabel || !s_channelActionsMuteBtn) return;
    const bool muted = channelIsMuted(s_channelActionsChanIdx);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(s_channelActionsMuteLabel, muted ? "Unmute" : "Mute");
#else
    lv_label_set_text(s_channelActionsMuteLabel, muted ? "Un(m)ute" : "(M)ute");
#endif

    const bool isLight = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t activeBg = isLight ? lv_color_hex(0xDCE9FF) : lv_color_hex(0x2A4E8F);
    // Muted state gets a warmer tint so the toggle reads at a glance.
    const lv_color_t mutedBg  = isLight ? lv_color_hex(0xF3E0C4) : lv_color_hex(0x6E4A18);
    lv_obj_set_style_bg_color(s_channelActionsMuteBtn, muted ? mutedBg : activeBg, 0);
    lv_obj_set_style_bg_opa(s_channelActionsMuteBtn, LV_OPA_COVER, 0);
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

static void onChannelActionMutePressed(lv_event_t *e) {
    LV_UNUSED(e);
    toggleActiveChannelMute();
}

static void onChannelActionClosePressed(lv_event_t *e) {
    LV_UNUSED(e);
    closeChannelActionsModal();
}

static void closeChannelActionsModal() {
    lvObjDeleteSafe(s_channelActionsModal);
    s_channelActionsModal = nullptr;
    s_channelActionsMuteBtn = nullptr;
    s_channelActionsMuteLabel = nullptr;
    s_channelActionsChanIdx = -1;
}

static void openChannelActionsModal() {
    if (s_channelActionsModal) closeChannelActionsModal();
    if (s_activeChannel < 0 || s_activeChannel >= MESH_CHANNELS) return;
    // On Heltec the bottom nav is embedded in every full-screen modal, so this
    // can be tapped from within one. Close any sibling first, matching how the
    // other nav targets behave. No-ops on keyboard boards opened from home.
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeCfgModal();
    closeLiveModal();
    closeLegendModal();
    s_channelActionsChanIdx = s_activeChannel;

    const int modalW = min(220, lv_disp_get_hor_res(NULL) - 14);
    lv_obj_t *parent = s_rootScreen ? s_rootScreen : lv_scr_act();
    s_channelActionsModal = lv_obj_create(parent);
    lv_obj_set_width(s_channelActionsModal, modalW);
    lv_obj_set_height(s_channelActionsModal, LV_SIZE_CONTENT);
    lv_obj_align(s_channelActionsModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_channelActionsModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_channelActionsModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_channelActionsModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_channelActionsModal, 1, 0);
    lv_obj_set_style_border_color(s_channelActionsModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_channelActionsModal, 6, 0);
    lv_obj_set_style_pad_row(s_channelActionsModal, 6, 0);
    lv_obj_set_flex_flow(s_channelActionsModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_channelActionsModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_channelActionsModal);

    const char *chName = CHANNEL_KEYS[s_channelActionsChanIdx].name_buf[0]
                             ? CHANNEL_KEYS[s_channelActionsChanIdx].name_buf
                             : CHANNEL_KEYS[s_channelActionsChanIdx].name;

    lv_obj_t *title = lv_label_create(s_channelActionsModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(title, "Channel Actions\n%s",
                          (chName && chName[0]) ? chName : "(unnamed)");

    lv_obj_t *btn = lv_btn_create(s_channelActionsModal);
    s_channelActionsMuteBtn = btn;
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 30);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_pad_all(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, onChannelActionMutePressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(btn);
    s_channelActionsMuteLabel = lbl;
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x13233D) : lv_color_hex(0xE8F1FF), 0);
    lv_obj_center(lbl);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Touch-only board: provide an explicit Close button instead of a keyboard hint.
    lv_obj_t *closeBtn = lv_btn_create(s_channelActionsModal);
    lv_obj_set_width(closeBtn, lv_pct(100));
    lv_obj_set_height(closeBtn, 30);
    lv_obj_set_style_radius(closeBtn, 4, 0);
    lv_obj_set_style_pad_all(closeBtn, 2, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_set_style_bg_color(closeBtn,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0xE6ECF5) : lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(closeBtn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(closeBtn, onChannelActionClosePressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *closeLbl = lv_label_create(closeBtn);
    lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(closeLbl,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x13233D) : lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(closeLbl, "Close");
    lv_obj_center(closeLbl);
#else
    lv_obj_t *hint = lv_label_create(s_channelActionsModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(hint, "%s = Close", modalCloseKeyLabel());
#endif

    refreshChannelActionsModal();
}

static void refreshLiveView(bool force) {
    if (!s_liveModal || !s_liveList) return;
    if (!lv_obj_is_valid(s_liveModal) || !lv_obj_is_valid(s_liveList)) {
        s_liveModal = nullptr;
        s_liveList = nullptr;
        s_lastRenderedLiveCount = -1;
        s_lastRenderedLiveScrollOff = -1;
        return;
    }
    if (lv_obj_get_parent(s_liveList) != s_liveModal
        || lv_obj_get_disp(s_liveModal) == nullptr
        || lv_obj_get_disp(s_liveList) == nullptr) {
        s_liveList = nullptr;
        s_lastRenderedLiveCount = -1;
        s_lastRenderedLiveScrollOff = -1;
        return;
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *liveBodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *liveBodyFont = &lv_font_montserrat_10;
#endif

    const Channel &ch = Channels.get(CHAN_LIVE);
    if (!force && s_lastRenderedLiveCount == ch.count && s_lastRenderedLiveScrollOff == ch.scrollOff) {
        return;
    }

    const bool stickToTop = force || (lv_obj_get_scroll_y(s_liveList) <= 2);
    const int32_t prevScrollY = lv_obj_get_scroll_y(s_liveList);

    lv_obj_clean(s_liveList);

    int rowCount = 0;
    for (int row = 0; row < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(CHAN_LIVE, row);
        if (!dl) break;

        // Each wrapped label costs a few hundred bytes from LVGL's fixed
        // LV_MEM_SIZE pool. If the pool can't satisfy lv_label_create(), LVGL
        // fires LV_ASSERT_MALLOC and the default handler resets the device —
        // this is the live-screen crash seen after long uptime once the pool
        // is full/fragmented. Stop early and render what fits instead.
        lv_mem_monitor_t liveMem;
        lv_mem_monitor(&liveMem);
        if (liveMem.free_biggest_size < 3072) {
            logLvglMemDiag("live render stopped (low LVGL mem)");
            break;
        }
        rowCount++;

        lv_obj_t *msg = lv_label_create(s_liveList);
        lv_obj_set_width(msg, lv_pct(100));
        lv_obj_set_style_text_font(msg, liveBodyFont, 0);
        lv_obj_set_style_pad_left(msg, 2, 0);
        lv_obj_set_style_pad_right(msg, 4, 0);
        lv_obj_set_style_pad_top(msg, 1, 0);
        lv_obj_set_style_pad_bottom(msg, 1, 0);
        lv_obj_set_style_radius(msg, 2, 0);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

        uint16_t lineColor = liveLineTrafficColor(*dl);
        if (dl->packetId) {
            switch (dl->ack) {
                case DisplayLine::ACKED:
                    lineColor = (s_cfg.uiMode == UI_MODE_LIGHT) ? (uint16_t)0x0320 : TFT_GREEN;
                    break;
                case DisplayLine::ACKED_RELAY:
                    lineColor = userMessageAccentColor565();
                    break;
                case DisplayLine::NAKED:
                    lineColor = TFT_RED;
                    break;
                case DisplayLine::TX_FAILED:
                    lineColor = TFT_RED;
                    break;
                default:
                    break;
            }
        }

        lv_obj_set_style_bg_color(msg, liveLineBgColor(*dl), 0);
        lv_obj_set_style_bg_opa(msg, (lineColor == TFT_RED) ? LV_OPA_60 : LV_OPA_40, 0);
        lv_obj_set_style_text_color(msg, tftColorToLv(lineColor), 0);

        char rendered[128];
        formatLiveLineText(*dl, rendered, sizeof(rendered));
        setLabelTextEmojiSafe(msg, rendered);
    }

    if (rowCount == 0) {
        lv_obj_t *empty = lv_label_create(s_liveList);
        lv_obj_set_style_text_font(empty, liveBodyFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No live traffic yet");
    }

    // Flush the flex layout so scroll extents are accurate before positioning;
    // otherwise scroll_to_y bounds against stale geometry and can leave the top
    // row not flush with the edge.
    lv_obj_update_layout(s_liveList);

    if (stickToTop) {
        lv_obj_scroll_to_y(s_liveList, 0, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(s_liveList, prevScrollY, LV_ANIM_OFF);
    }

    bool overflow = lv_obj_get_scroll_bottom(s_liveList) > 0 || lv_obj_get_scroll_top(s_liveList) > 0;
    lv_obj_set_scrollbar_mode(s_liveList, overflow ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);

    s_lastRenderedLiveCount = ch.count;
    s_lastRenderedLiveScrollOff = ch.scrollOff;
}

// Report the LVGL heap pool (a fixed LV_MEM_SIZE arena, not the system heap).
// Fragmentation/exhaustion of this pool after long uptime is the prime suspect
// for the live-screen crash seen across all boards.
static void logLvglMemDiag(const char *tag) {
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    Serial.printf("[lvgl] %s pool used=%u%% free=%u biggest=%u frag=%u%%\n",
                  tag ? tag : "mem",
                  (unsigned)m.used_pct,
                  (unsigned)m.free_size,
                  (unsigned)m.free_biggest_size,
                  (unsigned)m.frag_pct);
}

static void openLiveModal() {
    if (s_liveModal && !lvObjAlive(s_liveModal)) {
        s_liveModal = nullptr;
        s_liveList = nullptr;
    }
    if (!s_rootScreen || s_liveModal) return;
    logLvglMemDiag("openLiveModal");
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeCfgModal();
    closeLegendModal();
    closeChannelActionsModal();

    Channels.get(CHAN_LIVE).scrollOff = 0;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;

    // If the LVGL pool is too full to safely build the modal frame + list,
    // abort instead of crashing inside lv_obj_create (LV_ASSERT_MALLOC resets
    // the device). The closes above already freed any other open modals, so
    // this only trips when memory is genuinely too low. The per-line guard in
    // refreshLiveView handles running low partway through rendering.
    {
        lv_mem_monitor_t mem;
        lv_mem_monitor(&mem);
        if (mem.free_biggest_size < 6144) {
            logLvglMemDiag("openLiveModal aborted (low LVGL mem)");
            return;
        }
    }

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_liveModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_liveModal, modalW, modalH);
    lv_obj_align(s_liveModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_liveModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_liveModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_liveModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_liveModal, 1, 0);
    lv_obj_set_style_border_color(s_liveModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_liveModal, 4, 0);
    lv_obj_set_style_pad_row(s_liveModal, 4, 0);
    lv_obj_set_flex_flow(s_liveModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_liveModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(s_liveModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "LIVE");
#if defined(DEVICE_HELTEC_V4_EXPANSION) && defined(DEVICE_UI_VERTICAL)
    // Vertical Heltec is narrow; the right-anchored chart buttons would
    // overdraw a centered title. Left-align with a small inset instead.
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);
#else
    lv_obj_center(title);
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Dedicated header buttons (Heltec touch) for the two live charts.
    auto makeLiveChartBtn = [](lv_obj_t *parent, const char *text, int xOffset,
                               lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 52, 20);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, xOffset, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_left(btn, 4, 0);
        lv_obj_set_style_pad_right(btn, 4, 0);
        lv_obj_set_style_pad_top(btn, 1, 0);
        lv_obj_set_style_pad_bottom(btn, 1, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16386F), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x335D9D), 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
        return btn;
    };
    // Rightmost: SNR/RSSI. To its left: ChUtil.
    makeLiveChartBtn(header, "SNR", -4,
                     [](lv_event_t *e) { LV_UNUSED(e); openSnrRssiChartModal(); });
    makeLiveChartBtn(header, "ChUtil", -60,
                     [](lv_event_t *e) { LV_UNUSED(e); openChUtilChartModal(); });
#endif

    s_liveList = lv_obj_create(s_liveModal);
    lv_obj_set_width(s_liveList, lv_pct(100));
    lv_obj_set_flex_grow(s_liveList, 1);
    lv_obj_add_flag(s_liveList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_liveList);
    lv_obj_set_scrollbar_mode(s_liveList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_liveList, liveListBackdropColor(), 0);
    lv_obj_set_style_bg_opa(s_liveList, liveListBackdropOpa(), 0);
    lv_obj_set_style_border_width(s_liveList, 1, 0);
    lv_obj_set_style_border_color(s_liveList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_liveList, 0, 0);
    lv_obj_set_style_pad_right(s_liveList, 4, 0);
    lv_obj_set_style_pad_row(s_liveList, 1, 0);
    lv_obj_set_style_width(s_liveList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_liveList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_liveList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_liveList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_liveList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_liveList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    appendHeltecBottomNav(s_liveModal, HELTEC_NAV_LIVE);
#else
    lv_obj_t *hint = lv_label_create(s_liveModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, "%s = Back   C = Clear   U = ChUtil   S = SNR/RSSI", modalCloseKeyLabel());
#endif

    refreshLiveView(true);
}

static int16_t chartClampInt(float v, int16_t lo, int16_t hi) {
    if (v < (float)lo) return lo;
    if (v > (float)hi) return hi;
    return (int16_t)lroundf(v);
}

static void onChUtilChartDrawEvent(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc) return;
    if (dsc->part != LV_PART_TICKS) return;
    if (!dsc->text) return;
    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
        lv_snprintf(dsc->text, dsc->text_length, "%d%%", (int)dsc->value);
    }
}

static void onSnrChartDrawEvent(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc) return;
    if (dsc->part != LV_PART_TICKS) return;
    if (!dsc->text) return;
    if (dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
        lv_snprintf(dsc->text, dsc->text_length, "%d dB", (int)dsc->value);
    } else if (dsc->id == LV_CHART_AXIS_SECONDARY_Y) {
        lv_snprintf(dsc->text, dsc->text_length, "%d dBm", (int)dsc->value);
    }
}

static void refreshChUtilChart(bool force) {
    if (!s_chUtilChartModal || !s_chUtilChart) return;
    if (!lv_obj_is_valid(s_chUtilChartModal) || !lv_obj_is_valid(s_chUtilChart)) {
        s_chUtilChartModal = nullptr;
        s_chUtilChart = nullptr;
        s_chUtilSeries = nullptr;
        s_airUtilSeries = nullptr;
        s_chUtilStatsLabel = nullptr;
        return;
    }

    if (!force
        && s_chUtilRenderedSeq == s_chUtilHist.seq
        && s_airUtilRenderedSeq == s_airUtilHist.seq) {
        return;
    }

    auto fillSeries = [&](lv_chart_series_t *series, const ChartHist &h) {
        if (!series) return;
        const int offset = ChartHist::CAP - h.count;  // right-align newest sample
        for (int i = 0; i < ChartHist::CAP; i++) {
            int sampleIdx = i - offset;
            if (sampleIdx >= 0 && sampleIdx < h.count) {
                int16_t v = chartClampInt(chartSampleAt(h, sampleIdx), 0, 100);
                lv_chart_set_value_by_id(s_chUtilChart, series, i, v);
            } else {
                lv_chart_set_value_by_id(s_chUtilChart, series, i, LV_CHART_POINT_NONE);
            }
        }
    };
    fillSeries(s_chUtilSeries, s_chUtilHist);
    fillSeries(s_airUtilSeries, s_airUtilHist);
    lv_chart_refresh(s_chUtilChart);

    s_chUtilRenderedSeq = s_chUtilHist.seq;
    s_airUtilRenderedSeq = s_airUtilHist.seq;

    if (s_chUtilStatsLabel && lv_obj_is_valid(s_chUtilStatsLabel)) {
        char text[160];
        char chCur[16] = "--";
        char airCur[16] = "--";
        if (s_chUtilHist.hasLast) snprintf(chCur, sizeof(chCur), "%.1f%%", (double)s_chUtilHist.lastVal);
        if (s_airUtilHist.hasLast) snprintf(airCur, sizeof(airCur), "%.1f%%", (double)s_airUtilHist.lastVal);

        float chSum = 0.0f, chMax = 0.0f;
        for (int i = 0; i < s_chUtilHist.count; i++) {
            float s = chartSampleAt(s_chUtilHist, i);
            chSum += s;
            if (s > chMax) chMax = s;
        }
        float airSum = 0.0f, airMax = 0.0f;
        for (int i = 0; i < s_airUtilHist.count; i++) {
            float s = chartSampleAt(s_airUtilHist, i);
            airSum += s;
            if (s > airMax) airMax = s;
        }
        float chAvg = (s_chUtilHist.count > 0) ? (chSum / s_chUtilHist.count) : 0.0f;
        float airAvg = (s_airUtilHist.count > 0) ? (airSum / s_airUtilHist.count) : 0.0f;

        snprintf(text, sizeof(text),
                 "ChUtil  cur %s  avg %.1f%%  max %.1f%%   n=%d\n"
                 "AirTx   cur %s  avg %.1f%%  max %.1f%%   n=%d",
                 chCur, (double)chAvg, (double)chMax, s_chUtilHist.count,
                 airCur, (double)airAvg, (double)airMax, s_airUtilHist.count);
        lv_label_set_text(s_chUtilStatsLabel, text);
    }
}

static void openChUtilChartModal() {
    if (!s_rootScreen) return;
    if (s_chUtilChartModal) return;

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_chUtilChartModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_chUtilChartModal, modalW, modalH);
    lv_obj_align(s_chUtilChartModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_chUtilChartModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_chUtilChartModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chUtilChartModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chUtilChartModal, 1, 0);
    lv_obj_set_style_border_color(s_chUtilChartModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_chUtilChartModal, 4, 0);
    lv_obj_set_style_pad_row(s_chUtilChartModal, 4, 0);
    lv_obj_set_flex_flow(s_chUtilChartModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chUtilChartModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(s_chUtilChartModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "CHANNEL UTILIZATION (%)");
#if defined(DEVICE_HELTEC_V4_EXPANSION) && defined(DEVICE_UI_VERTICAL)
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);
#else
    lv_obj_center(title);
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Touch close button anchored to the right side of the header.
    lv_obj_t *headerClose = lv_btn_create(header);
    lv_obj_set_size(headerClose, 48, 20);
    lv_obj_align(headerClose, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(headerClose, 4, 0);
    lv_obj_set_style_pad_all(headerClose, 0, 0);
    lv_obj_set_style_shadow_width(headerClose, 0, 0);
    lv_obj_set_style_bg_color(headerClose, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(headerClose, LV_OPA_80, 0);
    lv_obj_set_style_border_width(headerClose, 1, 0);
    lv_obj_set_style_border_color(headerClose, lv_color_hex(0x8FB5E6), 0);
    lv_obj_add_event_cb(headerClose,
                        [](lv_event_t *e) { LV_UNUSED(e); closeChUtilChartModal(); },
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_t *headerCloseLbl = lv_label_create(headerClose);
    lv_obj_set_style_text_font(headerCloseLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(headerCloseLbl, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(headerCloseLbl, "Close");
    lv_obj_center(headerCloseLbl);
#endif

    s_chUtilChart = lv_chart_create(s_chUtilChartModal);
    lv_obj_set_width(s_chUtilChart, lv_pct(100));
    lv_obj_set_flex_grow(s_chUtilChart, 1);
    lv_chart_set_type(s_chUtilChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chUtilChart, ChartHist::CAP);
    lv_chart_set_range(s_chUtilChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_chUtilChart, 5, 6);
    lv_chart_set_update_mode(s_chUtilChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_tick(s_chUtilChart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 5, 2, true, 40);
    lv_obj_set_style_size(s_chUtilChart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_chUtilChart, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_chUtilChart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chUtilChart, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_border_width(s_chUtilChart, 1, 0);
    lv_obj_set_style_line_color(s_chUtilChart, lv_color_hex(0x335D9D), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_chUtilChart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_chUtilChart, &lv_font_montserrat_10, LV_PART_TICKS);
    lv_obj_set_style_text_color(s_chUtilChart, lv_color_hex(0xA7C7FF), LV_PART_TICKS);
    lv_obj_add_event_cb(s_chUtilChart, onChUtilChartDrawEvent, LV_EVENT_DRAW_PART_BEGIN, nullptr);

    s_chUtilSeries = lv_chart_add_series(s_chUtilChart,
                                         lv_color_hex(0x4FD1C5),
                                         LV_CHART_AXIS_PRIMARY_Y);
    s_airUtilSeries = lv_chart_add_series(s_chUtilChart,
                                          lv_color_hex(0xF6AD55),
                                          LV_CHART_AXIS_PRIMARY_Y);
    if (s_chUtilSeries) lv_chart_set_all_value(s_chUtilChart, s_chUtilSeries, LV_CHART_POINT_NONE);
    if (s_airUtilSeries) lv_chart_set_all_value(s_chUtilChart, s_airUtilSeries, LV_CHART_POINT_NONE);

    s_chUtilStatsLabel = lv_label_create(s_chUtilChartModal);
    lv_obj_set_width(s_chUtilStatsLabel, lv_pct(100));
    lv_label_set_long_mode(s_chUtilStatsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_chUtilStatsLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chUtilStatsLabel, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(s_chUtilStatsLabel, "ChUtil  cur --  avg --  max --   n=0\nAirTx   cur --  avg --  max --   n=0");

#if !defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *hint = lv_label_create(s_chUtilChartModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, "%s = Back   teal=ChUtil  orange=AirTx", modalCloseKeyLabel());
#endif

    s_chUtilRenderedSeq = 0;
    s_airUtilRenderedSeq = 0;
    refreshChUtilChart(true);
}

static void refreshSnrRssiChart(bool force) {
    if (!s_snrChartModal || !s_snrChart) return;
    if (!lv_obj_is_valid(s_snrChartModal) || !lv_obj_is_valid(s_snrChart)) {
        s_snrChartModal = nullptr;
        s_snrChart = nullptr;
        s_snrSeries = nullptr;
        s_rssiSeries = nullptr;
        s_snrStatsLabel = nullptr;
        return;
    }

    if (!force
        && s_snrRenderedSeq == s_snrHist.seq
        && s_rssiRenderedSeq == s_rssiHist.seq) {
        return;
    }

    auto fillSeries = [&](lv_chart_series_t *series, const ChartHist &h, int16_t lo, int16_t hi) {
        if (!series) return;
        const int offset = ChartHist::CAP - h.count;  // right-align newest sample
        for (int i = 0; i < ChartHist::CAP; i++) {
            int sampleIdx = i - offset;
            if (sampleIdx >= 0 && sampleIdx < h.count) {
                int16_t v = chartClampInt(chartSampleAt(h, sampleIdx), lo, hi);
                lv_chart_set_value_by_id(s_snrChart, series, i, v);
            } else {
                lv_chart_set_value_by_id(s_snrChart, series, i, LV_CHART_POINT_NONE);
            }
        }
    };
    fillSeries(s_snrSeries, s_snrHist, -25, 15);
    fillSeries(s_rssiSeries, s_rssiHist, -130, -30);
    lv_chart_refresh(s_snrChart);

    s_snrRenderedSeq = s_snrHist.seq;
    s_rssiRenderedSeq = s_rssiHist.seq;

    if (s_snrStatsLabel && lv_obj_is_valid(s_snrStatsLabel)) {
        char text[160];
        char snrCur[16] = "--";
        char rssiCur[16] = "--";
        if (s_snrHist.hasLast) snprintf(snrCur, sizeof(snrCur), "%.1fdB", (double)s_snrHist.lastVal);
        if (s_rssiHist.hasLast) snprintf(rssiCur, sizeof(rssiCur), "%.0fdBm", (double)s_rssiHist.lastVal);

        float snrSum = 0.0f, snrMin = 1000.0f, snrMax = -1000.0f;
        for (int i = 0; i < s_snrHist.count; i++) {
            float s = chartSampleAt(s_snrHist, i);
            snrSum += s;
            if (s < snrMin) snrMin = s;
            if (s > snrMax) snrMax = s;
        }
        float rssiSum = 0.0f, rssiMin = 1000.0f, rssiMax = -1000.0f;
        for (int i = 0; i < s_rssiHist.count; i++) {
            float s = chartSampleAt(s_rssiHist, i);
            rssiSum += s;
            if (s < rssiMin) rssiMin = s;
            if (s > rssiMax) rssiMax = s;
        }
        float snrAvg = (s_snrHist.count > 0) ? (snrSum / s_snrHist.count) : 0.0f;
        float rssiAvg = (s_rssiHist.count > 0) ? (rssiSum / s_rssiHist.count) : 0.0f;

        if (s_snrHist.count == 0) snrMin = snrMax = 0.0f;
        if (s_rssiHist.count == 0) rssiMin = rssiMax = 0.0f;

        snprintf(text, sizeof(text),
                 "SNR   cur %s  avg %.1fdB  min %.1f  max %.1f   n=%d\n"
                 "RSSI  cur %s  avg %.0fdBm min %.0f  max %.0f   n=%d",
                 snrCur, (double)snrAvg, (double)snrMin, (double)snrMax, s_snrHist.count,
                 rssiCur, (double)rssiAvg, (double)rssiMin, (double)rssiMax, s_rssiHist.count);
        lv_label_set_text(s_snrStatsLabel, text);
    }
}

static void openSnrRssiChartModal() {
    if (!s_rootScreen) return;
    if (s_snrChartModal) return;

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_snrChartModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_snrChartModal, modalW, modalH);
    lv_obj_align(s_snrChartModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_snrChartModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_snrChartModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_snrChartModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_snrChartModal, 1, 0);
    lv_obj_set_style_border_color(s_snrChartModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_snrChartModal, 4, 0);
    lv_obj_set_style_pad_row(s_snrChartModal, 4, 0);
    lv_obj_set_flex_flow(s_snrChartModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_snrChartModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(s_snrChartModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 1, 0);
    lv_obj_set_style_pad_bottom(header, 1, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "SNR (dB)  /  RSSI (dBm)");
#if defined(DEVICE_HELTEC_V4_EXPANSION) && defined(DEVICE_UI_VERTICAL)
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 2, 0);
#else
    lv_obj_center(title);
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Touch close button anchored to the right side of the header.
    lv_obj_t *headerClose = lv_btn_create(header);
    lv_obj_set_size(headerClose, 48, 20);
    lv_obj_align(headerClose, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(headerClose, 4, 0);
    lv_obj_set_style_pad_all(headerClose, 0, 0);
    lv_obj_set_style_shadow_width(headerClose, 0, 0);
    lv_obj_set_style_bg_color(headerClose, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(headerClose, LV_OPA_80, 0);
    lv_obj_set_style_border_width(headerClose, 1, 0);
    lv_obj_set_style_border_color(headerClose, lv_color_hex(0x8FB5E6), 0);
    lv_obj_add_event_cb(headerClose,
                        [](lv_event_t *e) { LV_UNUSED(e); closeSnrRssiChartModal(); },
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_t *headerCloseLbl = lv_label_create(headerClose);
    lv_obj_set_style_text_font(headerCloseLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(headerCloseLbl, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(headerCloseLbl, "Close");
    lv_obj_center(headerCloseLbl);
#endif

    s_snrChart = lv_chart_create(s_snrChartModal);
    lv_obj_set_width(s_snrChart, lv_pct(100));
    lv_obj_set_flex_grow(s_snrChart, 1);
    lv_chart_set_type(s_snrChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_snrChart, ChartHist::CAP);
    lv_chart_set_range(s_snrChart, LV_CHART_AXIS_PRIMARY_Y, -25, 15);
    lv_chart_set_range(s_snrChart, LV_CHART_AXIS_SECONDARY_Y, -130, -30);
    lv_chart_set_div_line_count(s_snrChart, 5, 6);
    lv_chart_set_update_mode(s_snrChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_axis_tick(s_snrChart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 5, 2, true, 50);
    lv_chart_set_axis_tick(s_snrChart, LV_CHART_AXIS_SECONDARY_Y, 4, 2, 5, 2, true, 56);
    lv_obj_set_style_size(s_snrChart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_snrChart, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_snrChart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_snrChart, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_border_width(s_snrChart, 1, 0);
    lv_obj_set_style_line_color(s_snrChart, lv_color_hex(0x335D9D), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_snrChart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_snrChart, &lv_font_montserrat_10, LV_PART_TICKS);
    lv_obj_set_style_text_color(s_snrChart, lv_color_hex(0xA7C7FF), LV_PART_TICKS);
    lv_obj_add_event_cb(s_snrChart, onSnrChartDrawEvent, LV_EVENT_DRAW_PART_BEGIN, nullptr);

    s_snrSeries = lv_chart_add_series(s_snrChart,
                                      lv_color_hex(0x68D391),
                                      LV_CHART_AXIS_PRIMARY_Y);
    s_rssiSeries = lv_chart_add_series(s_snrChart,
                                       lv_color_hex(0xF687B3),
                                       LV_CHART_AXIS_SECONDARY_Y);
    if (s_snrSeries) lv_chart_set_all_value(s_snrChart, s_snrSeries, LV_CHART_POINT_NONE);
    if (s_rssiSeries) lv_chart_set_all_value(s_snrChart, s_rssiSeries, LV_CHART_POINT_NONE);

    s_snrStatsLabel = lv_label_create(s_snrChartModal);
    lv_obj_set_width(s_snrStatsLabel, lv_pct(100));
    lv_label_set_long_mode(s_snrStatsLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_snrStatsLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_snrStatsLabel, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(s_snrStatsLabel,
                      "SNR   cur --  avg --  min --  max --   n=0\n"
                      "RSSI  cur --  avg --  min --  max --   n=0");

#if !defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *hint = lv_label_create(s_snrChartModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, "%s = Back   green=SNR  pink=RSSI", modalCloseKeyLabel());
#endif

    s_snrRenderedSeq = 0;
    s_rssiRenderedSeq = 0;
    refreshSnrRssiChart(true);
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
    if (s_dmSelection > 0 && dmDeleteConfirmActive(millis())) {
        DmConv *selected = selectedDmConversation();
        if (selected && selected->nodeId == s_dmDeletePendingNodeId) {
            dmRequestDeleteSelectedConversation();
            return;
        }
    }

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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    return "Fn+Bksp";
#elif defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    return "Bksp";
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    return "Long-press";
#else
    return "Sym+Bksp";
#endif
}

static bool dmDeleteConfirmActive(uint32_t nowMs) {
    if (s_dmDeletePendingNodeId == 0) return false;
    if ((int32_t)(nowMs - s_dmDeleteConfirmUntilMs) > 0) {
        s_dmDeletePendingNodeId = 0;
        s_dmDeleteConfirmUntilMs = 0;
        return false;
    }
    return true;
}

static void dmDeleteSetFlash(const char *msg, uint32_t ttlMs = 2200) {
    if (!msg) msg = "";
    strncpy(s_dmDeleteFlashMsg, msg, sizeof(s_dmDeleteFlashMsg) - 1);
    s_dmDeleteFlashMsg[sizeof(s_dmDeleteFlashMsg) - 1] = '\0';
    s_dmDeleteFlashUntilMs = millis() + ttlMs;
}

static bool dmDeleteTriggerKey(char k) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    LV_UNUSED(k);
    return false;
#elif defined(DEVICE_TDECK)
    return (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE);
#else
    return (k == KEY_BACKSPACE_HOLD);
#endif
}

static void dmRequestDeleteSelectedConversation() {
    DmConv *selected = selectedDmConversation();
    if (!selected) return;

    uint32_t nodeId = selected->nodeId;
    uint32_t now = millis();

    if (dmDeleteConfirmActive(now) && s_dmDeletePendingNodeId == nodeId) {
        bool ok = DMs.deleteConversation(nodeId);
        s_dmDeletePendingNodeId = 0;
        s_dmDeleteConfirmUntilMs = 0;
        if (ok) {
            dmDeleteSetFlash("Conversation deleted");
            s_dmSelection = 0;
            s_dmMsgPanelFocused = false;
            s_dmRenderedConvCount = -1;
            s_dmRenderedNodeId = 0;
            s_dmRenderedMsgCount = -1;
            s_dmRenderedUnreadTotal = -1;
        } else {
            dmDeleteSetFlash("Delete failed");
        }
        refreshDmModal(true);
        return;
    }

    s_dmDeletePendingNodeId = nodeId;
    s_dmDeleteConfirmUntilMs = now + 6000UL;
    dmDeleteSetFlash("Confirm delete");
    refreshDmModal(true);
}

static void refreshDmPanelFocusStyles() {
    if (!s_dmConvPanel || !s_dmMsgPanel) return;

    bool msgFocused = s_dmMsgPanelFocused && (s_dmSelection > 0);
    lv_obj_set_style_border_width(s_dmConvPanel, msgFocused ? 1 : 2, 0);
    lv_obj_set_style_border_color(
        s_dmConvPanel,
        msgFocused ? lv_color_hex(0x335D9D) : lv_color_hex(0x90B4FF),
        0);

    lv_obj_set_style_border_width(s_dmMsgPanel, msgFocused ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        s_dmMsgPanel,
        msgFocused ? lv_color_hex(0x90B4FF) : lv_color_hex(0x335D9D),
        0);
}

static void onDmConversationPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > s_dmConvCount) return;
    uint32_t selectedNodeId = 0;
    if (idx > 0 && idx - 1 < s_dmConvCount) {
        selectedNodeId = s_dmConvNodeIds[idx - 1];
    }
    s_dmSelection = idx;
    s_dmMsgPanelFocused = (idx > 0);
    if (selectedNodeId == 0 || selectedNodeId != s_dmDeletePendingNodeId) {
        s_dmDeletePendingNodeId = 0;
        s_dmDeleteConfirmUntilMs = 0;
    }
    refreshDmModal(true);
}

static void onDmConversationPressState(lv_event_t *e) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
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

static void onDmNodePressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_dmNodeFilteredCount) return;
    s_dmNodeSelection = idx;
    refreshDmNodePicker(true);
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
}

static void snapshotNodesForDmPicker() {
    s_dmNodeSnapshotCount = 0;
    s_dmNodeFilteredCount = 0;
    s_dmNodeSelection = -1;
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
            snprintf(title, sizeof(title), "New DM: Select Node [%s]", s_dmNodeFilter);
            lv_label_set_text(s_dmNodePickerTitle, title);
        } else {
            lv_label_set_text(s_dmNodePickerTitle, "New DM: Select Node");
        }
    }

    if (s_dmNodePickerHint) {
        lv_obj_set_style_text_color(s_dmNodePickerHint, dmPickerHintColor, 0);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_label_set_text(s_dmNodePickerHint,
                          s_dmNodeFilterOpen
                              ? "Type = Filter   Bksp = Edit Filter   Enter = Open DM   Esc = Back"
                              : "Type = Filter   Enter = Open DM   Esc = Back");
#else
        lv_label_set_text(s_dmNodePickerHint,
                          s_dmNodeFilterOpen
                              ? "Type = Filter   Bksp = Edit/Close Filter   Enter = Open DM"
                              : "Type = Filter   Enter = Open DM   Bksp = Back");
#endif
    }

    lv_obj_clean(s_dmNodePickerList);
    memset(s_dmNodePickerRows, 0, sizeof(s_dmNodePickerRows));

    if (s_dmNodeFilteredCount <= 0) {
        lv_obj_t *empty = lv_label_create(s_dmNodePickerList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(empty, dmPickerTextColor, 0);
        if (s_dmNodeFilterOpen && s_dmNodeFilterLen > 0) {
            char noMatch[64];
            snprintf(noMatch, sizeof(noMatch), "No matches for: %s", s_dmNodeFilter);
            lv_label_set_text(empty, noMatch);
        } else {
            lv_label_set_text(empty, "No known nodes yet");
        }
        return;
    }

    for (int i = 0; i < s_dmNodeFilteredCount; i++) {
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
        s_dmNodePickerRows[i] = row;
        lv_obj_set_width(row, lv_pct(97));
        lv_obj_set_height(row, 22);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 3, 0);
        lv_obj_set_style_pad_right(row, 3, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selected ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
        lv_obj_set_style_bg_color(row,
                                  selected ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                  0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_40, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onDmNodePressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(row);
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
    lv_label_set_text(title, "New DM: Select Node");

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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_label_set_text(hint, "Type = Filter   Enter = Open DM   Esc = Back");
#else
    lv_label_set_text(hint, "Type = Filter   Enter = Open DM   Bksp = Back");
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
    memset(s_dmNodeSnapshotIds, 0, sizeof(s_dmNodeSnapshotIds));
    memset(s_dmNodeFilteredIdx, 0, sizeof(s_dmNodeFilteredIdx));
    memset(s_dmNodePickerRows, 0, sizeof(s_dmNodePickerRows));
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
    if (!lvObjAlive(s_dmModal)
        || !lvObjAlive(s_dmConvList)
        || !lvObjAlive(s_dmMsgList)) {
        s_dmModal = nullptr;
        s_dmConvPanel = nullptr;
        s_dmConvList = nullptr;
        s_dmMsgPanel = nullptr;
        s_dmMsgList = nullptr;
        s_dmHintLabel = nullptr;
        return;
    }

    uint32_t now = millis();
    bool hadDeletePending = (s_dmDeletePendingNodeId != 0);
    bool hasDeletePending = dmDeleteConfirmActive(now);
    if (hadDeletePending != hasDeletePending) {
        force = true;
    }

    if (s_dmDeleteFlashUntilMs != 0 && (int32_t)(now - s_dmDeleteFlashUntilMs) > 0) {
        s_dmDeleteFlashMsg[0] = '\0';
        s_dmDeleteFlashUntilMs = 0;
        force = true;
    }

    const lv_color_t dmPanelTextColor =
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF);
    const uint16_t dmMessageBaseColor =
        (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x1B, 0x24, 0x3D) : TFT_WHITE;

    uint32_t selectedNodeIdBefore = 0;
    if (s_dmSelection > 0) {
        int prevConvIdx = s_dmSelection - 1;
        if (prevConvIdx >= 0 && prevConvIdx < s_dmConvCount) {
            selectedNodeIdBefore = s_dmConvNodeIds[prevConvIdx];
        }
    }

    memset(s_dmConvRows, 0, sizeof(s_dmConvRows));
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

    lv_obj_clean(s_dmConvList);
    lv_obj_clean(s_dmMsgList);

    const lv_font_t *dmListFont = kMainScreenFont;
    const lv_font_t *dmMsgFont = scaledChatFont(kMainScreenFont);
#if defined(DEVICE_TLORA_PAGER_TFT)
    const int dmListRowH = 24;
#else
    const int dmListRowH = 22;
#endif

    {
        bool selectedRow = (s_dmSelection == 0);
        lv_obj_t *row = lv_btn_create(s_dmConvList);
        lv_obj_set_width(row, lv_pct(97));
        lv_obj_set_height(row, dmListRowH);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_left(row, 3, 0);
        lv_obj_set_style_pad_right(row, 3, 0);
        lv_obj_set_style_pad_top(row, 1, 0);
        lv_obj_set_style_pad_bottom(row, 1, 0);
        lv_obj_set_style_border_width(row, selectedRow ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selectedRow ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
        lv_obj_set_style_bg_color(row,
                                  selectedRow ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                  0);
        lv_obj_set_style_bg_opa(row, selectedRow ? LV_OPA_70 : LV_OPA_40, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, onDmConversationPressed, LV_EVENT_PRESSED, (void *)(intptr_t)0);
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
        lv_label_set_text(lbl, "New DM");
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }

    if (s_dmConvCount > 0) {
        for (int i = 0; i < s_dmConvCount; i++) {
            DmConv *c = DMs.find(s_dmConvNodeIds[i]);
            if (!c) continue;

            int rowIdx = i + 1;
            bool selectedRow = (rowIdx == s_dmSelection);
            lv_obj_t *row = lv_btn_create(s_dmConvList);
            s_dmConvRows[i] = row;
            lv_obj_set_width(row, lv_pct(97));
            lv_obj_set_height(row, dmListRowH);
            lv_obj_set_style_radius(row, 4, 0);
            lv_obj_set_style_pad_left(row, 3, 0);
            lv_obj_set_style_pad_right(row, 3, 0);
            lv_obj_set_style_pad_top(row, 1, 0);
            lv_obj_set_style_pad_bottom(row, 1, 0);
            lv_obj_set_style_border_width(row, selectedRow ? 2 : 1, 0);
            lv_obj_set_style_border_color(row,
                                          selectedRow ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                          0);
            lv_obj_set_style_bg_color(row,
                                      selectedRow ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266),
                                      0);
            lv_obj_set_style_bg_opa(row, selectedRow ? LV_OPA_70 : LV_OPA_40, 0);
            lv_obj_set_style_shadow_width(row, 0, 0);
            lv_obj_add_event_cb(row, onDmConversationPressed, LV_EVENT_PRESSED, (void *)(intptr_t)rowIdx);
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
        lv_label_set_text(empty,
                          (s_dmSelection == 0)
                              ? "Backspace to return to Main Screen"
                              : "Select a conversation");
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

            // Insert a date marker before the first DM that lands on a new
            // local calendar day. DM lines store one logical message each, so
            // every line is a "first of group" candidate.
            uint32_t curBucket = chatDateBucket(dl->epoch);
            if (curBucket != 0 && curBucket != lastDateBucket) {
                insertChatDateMarker(s_dmMsgList, dl->epoch, dmMsgFont);
                lastDateBucket = curBucket;
            }

            // Bubbles style: reuse the channel-chat bubble renderer. Our lines
            // sit right-aligned in the accent/ack color, the peer's left-aligned
            // in their stable per-node color. No name tag (1:1 conversation, the
            // peer is already named in the panel header) and no press handler
            // (DMs have no reply/selection model).
            if (chatStyleUsesBubbles(s_cfg.chatStyle)) {
                const bool isMe = dmLineIsFromMe(dl->text);
                chatMakeBubble(s_dmMsgList,
                               isMe ? 0u : selected->nodeId,
                               isMe,
                               nullptr,
                               chatStripPrefix(dl->text),
                               dmAckToDisplayAck(dl->ack),
                               0,
                               false,
                               &lastMsgObj,
                               nullptr,
                               nullptr);
                continue;
            }

            lv_obj_t *msg = lv_label_create(s_dmMsgList);
            lastMsgObj = msg;
            lv_obj_set_width(msg, lv_pct(100));
            lv_obj_set_style_text_font(msg, dmMsgFont, 0);
            lv_obj_set_style_pad_left(msg, 2, 0);
            lv_obj_set_style_pad_right(msg, 4, 0);
            lv_obj_set_style_pad_top(msg, 0, 0);
            lv_obj_set_style_pad_bottom(msg, 0, 0);
            // DmMgr now stores one logical message per DmLine; let LVGL wrap it
            // to the actual pane pixel width so font metrics drive line breaks.
            lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

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

            lv_obj_set_style_text_color(msg, tftColorToLv(lineColor), 0);
            lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
            setLabelTextEmojiSafe(msg, dl->text);
        }

        if (autoScrollToLatest && lastMsgObj) {
            lv_obj_scroll_to_view(lastMsgObj, LV_ANIM_OFF);
        }

        if (rowCount == 0) {
            lv_obj_t *empty = lv_label_create(s_dmMsgList);
            lv_obj_set_width(empty, lv_pct(100));
            lv_obj_set_style_text_font(empty, dmMsgFont, 0);
            lv_obj_set_style_text_color(empty, dmPanelTextColor, 0);
            lv_label_set_text(empty, "No messages yet");
        }
    }

    s_dmRenderedConvCount = s_dmConvCount;
    s_dmRenderedNodeId = selected ? selected->nodeId : 0;
    s_dmRenderedMsgCount = selected ? selected->count : -1;
    s_dmRenderedUnreadTotal = unreadTotal;

    if (s_dmHintLabel) {
        if (s_dmDeletePendingNodeId != 0 && s_dmDeleteConfirmUntilMs != 0) {
            uint32_t remainS = (uint32_t)((s_dmDeleteConfirmUntilMs - now + 999UL) / 1000UL);
            if (remainS > 9) remainS = 9;
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Delete pending: %s again to confirm (%lus)",
                                  dmDeleteTriggerLabel(),
                                  (unsigned long)remainS);
        } else if (s_dmDeleteFlashMsg[0]) {
            lv_label_set_text(s_dmHintLabel, s_dmDeleteFlashMsg);
        } else {
#if defined(DEVICE_TDECK)
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "J/K = Select   Space = Compose   Enter = Focus   Bksp = Delete");
#elif defined(DEVICE_TLORA_PAGER_TFT)
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Up/Down = Select   Space = Compose   Enter = Focus   Bksp = Delete");
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Long-press convo 3s = Delete   Enter = Compose/Focus   %s = Back",
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Up/Down = Select   Space = Compose   Enter = Focus   %s = Delete   %s = Back",
                                  dmDeleteTriggerLabel(),
                                  modalCloseKeyLabel());
#endif
        }
    }

    refreshDmPanelFocusStyles();
}

static void openDmModal() {
    if (s_dmModal && !lvObjAlive(s_dmModal)) {
        s_dmModal = nullptr;
        s_dmConvPanel = nullptr;
        s_dmConvList = nullptr;
        s_dmMsgPanel = nullptr;
        s_dmMsgList = nullptr;
        s_dmHintLabel = nullptr;
    }
    if (!s_rootScreen || s_dmModal) return;
    if (s_composeModal) closeComposePrompt();
    closeLiveModal();
    closeNodesModal();
    closeCfgModal();
    closeLegendModal();
    closeChannelActionsModal();

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);
    // s_dmModal has border=1 + pad_all=4, so usable content width is modalW - 2*1 - 2*4.
    int contentW = modalW - 10;
    int leftW = max(96, (contentW * 38) / 100);
    int rightW = contentW - leftW - 3;

    s_dmModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_dmModal, modalW, modalH);
    lv_obj_align(s_dmModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_dmModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_dmModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_dmModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dmModal, 1, 0);
    lv_obj_set_style_border_color(s_dmModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_dmModal, 4, 0);
    lv_obj_set_style_pad_row(s_dmModal, 4, 0);
    lv_obj_set_flex_flow(s_dmModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dmModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(s_dmModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(
        title,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
        0);
    lv_label_set_text(title, "DIRECT MESSAGES");
    lv_obj_center(title);

    lv_obj_t *content = lv_obj_create(s_dmModal);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_column(content, 3, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *leftPanel = lv_obj_create(content);
    s_dmConvPanel = leftPanel;
    lv_obj_set_width(leftPanel, leftW);
    lv_obj_set_height(leftPanel, lv_pct(100));
    lv_obj_clear_flag(leftPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(leftPanel, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(leftPanel, LV_OPA_40, 0);
    lv_obj_set_style_border_width(leftPanel, 1, 0);
    lv_obj_set_style_border_color(leftPanel, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(leftPanel, 2, 0);
    lv_obj_set_style_pad_row(leftPanel, 2, 0);
    lv_obj_set_flex_flow(leftPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *leftTitle = lv_label_create(leftPanel);
    lv_obj_set_width(leftTitle, lv_pct(100));
    lv_obj_set_style_text_font(leftTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(
        leftTitle,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
        0);
    lv_label_set_text(leftTitle, "Conversations");

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

    lv_obj_t *rightPanel = lv_obj_create(content);
    s_dmMsgPanel = rightPanel;
    lv_obj_set_width(rightPanel, rightW);
    lv_obj_set_height(rightPanel, lv_pct(100));
    lv_obj_clear_flag(rightPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(rightPanel, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(rightPanel, LV_OPA_40, 0);
    lv_obj_set_style_border_width(rightPanel, 1, 0);
    lv_obj_set_style_border_color(rightPanel, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(rightPanel, 2, 0);
    lv_obj_set_style_pad_row(rightPanel, 2, 0);
    lv_obj_set_flex_flow(rightPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *rightTitle = lv_label_create(rightPanel);
    lv_obj_set_width(rightTitle, lv_pct(100));
    lv_obj_set_style_text_font(rightTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(
        rightTitle,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
        0);
    lv_label_set_text(rightTitle, "Messages");

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

    lv_obj_t *hint = lv_label_create(s_dmModal);
    s_dmHintLabel = hint;
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(
        hint,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF),
        0);
#if defined(DEVICE_TDECK)
    lv_label_set_text_fmt(hint,
                          "J/K = Select   Space = Compose   Enter = Focus   Bksp = Delete");
#elif defined(DEVICE_TLORA_PAGER_TFT)
    lv_label_set_text_fmt(hint,
                          "Up/Down = Select   Space = Compose   Enter = Focus   Bksp = Delete");
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text_fmt(hint,
                          "Long-press convo 3s = Delete   Enter = Compose/Focus   %s = Back",
                          modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint,
                          "Up/Down = Select   Space = Compose   Enter = Focus   %s = Delete   %s = Back",
                          dmDeleteTriggerLabel(),
                          modalCloseKeyLabel());
#endif

    s_dmRenderedConvCount = -1;
    s_dmRenderedNodeId = 0;
    s_dmRenderedMsgCount = -1;
    s_dmRenderedUnreadTotal = -1;
    s_dmMsgPanelFocused = false;
    refreshDmModal(true);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    appendHeltecBottomNav(s_dmModal, HELTEC_NAV_DM);
#endif
}

static void openNodesModal() {
    if (s_nodesModal && !lvObjAlive(s_nodesModal)) {
        s_nodesModal = nullptr;
        s_nodesInfoPanel = nullptr;
        s_nodesDetail = nullptr;
        s_nodesDetailExtra = nullptr;
        s_nodesMapPanel = nullptr;
        s_nodesMapTitle = nullptr;
        s_nodesMapCoords = nullptr;
        s_nodesMapMarker = nullptr;
        s_nodesMapTileLayer = nullptr;
        s_nodesMapImage = nullptr;
        s_nodesList = nullptr;
        s_nodesHintLabel = nullptr;
    }
    if (!s_rootScreen || s_nodesModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeLiveModal();
    closeCfgModal();
    closeLegendModal();
    closeChannelActionsModal();

    snapshotNodesForModal();

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);
    const int modalPad = 4;
    const int contentGap = 3;
    int contentW = modalW - (modalPad * 2);
    if (contentW < 120) contentW = modalW;

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *nodesDetailFont = emojiFont(&lv_font_montserrat_14);
#elif defined(DEVICE_TDECK)
    const lv_font_t *nodesDetailFont = emojiFont(&lv_font_montserrat_14);
#else
    const lv_font_t *nodesDetailFont = emojiFont(&lv_font_montserrat_10);
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    int rightW = max(62, min(84, (contentW * 22) / 100));
#else
    int rightW = max(68, min(96, (contentW * 24) / 100));
#endif
    int leftW = contentW - rightW - contentGap;
    if (leftW < 120) {
        int deficit = 120 - leftW;
        rightW = max(52, rightW - deficit);
        leftW = contentW - rightW - contentGap;
    }

    s_nodesModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_nodesModal, modalW, modalH);
    lv_obj_align(s_nodesModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodesModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_nodesModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodesModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodesModal, 1, 0);
    lv_obj_set_style_border_color(s_nodesModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_nodesModal, 4, 0);
    lv_obj_set_style_pad_row(s_nodesModal, 4, 0);
    lv_obj_set_flex_flow(s_nodesModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(s_nodesModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 26);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);

    lv_obj_t *title = lv_label_create(header);
    s_nodesTitleLabel = title;
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "NODES");
    lv_obj_center(title);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    s_nodesFilterBtn = lv_btn_create(header);
    lv_obj_set_size(s_nodesFilterBtn, 58, 20);
    lv_obj_align(s_nodesFilterBtn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_radius(s_nodesFilterBtn, 4, 0);
    lv_obj_set_style_pad_left(s_nodesFilterBtn, 6, 0);
    lv_obj_set_style_pad_right(s_nodesFilterBtn, 6, 0);
    lv_obj_set_style_pad_top(s_nodesFilterBtn, 1, 0);
    lv_obj_set_style_pad_bottom(s_nodesFilterBtn, 1, 0);
    lv_obj_set_style_shadow_width(s_nodesFilterBtn, 0, 0);
    lv_obj_set_style_bg_color(s_nodesFilterBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(s_nodesFilterBtn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_nodesFilterBtn, 1, 0);
    lv_obj_set_style_border_color(s_nodesFilterBtn, lv_color_hex(0x335D9D), 0);
    lv_obj_add_event_cb(s_nodesFilterBtn, onNodesFilterButtonPressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *filterLabel = lv_label_create(s_nodesFilterBtn);
    lv_obj_set_style_text_font(filterLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(filterLabel, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(filterLabel, "Filter");
    lv_obj_center(filterLabel);
#endif

    lv_obj_t *content = lv_obj_create(s_nodesModal);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_column(content, contentGap, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *left = lv_obj_create(content);
    s_nodesInfoPanel = left;
    lv_obj_set_width(left, leftW);
    lv_obj_set_height(left, lv_pct(100));
    lv_obj_add_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(left);
    lv_obj_set_scrollbar_mode(left, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_40, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(left, 4, 0);
    lv_obj_set_style_width(left, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(left, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(left, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(left, 2, LV_PART_SCROLLBAR);

#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_t *detailsCols = lv_obj_create(left);
    lv_obj_set_width(detailsCols, lv_pct(100));
    lv_obj_clear_flag(detailsCols, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(detailsCols, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(detailsCols, 0, 0);
    lv_obj_set_style_pad_all(detailsCols, 0, 0);
    lv_obj_set_style_pad_column(detailsCols, 10, 0);
    lv_obj_set_flex_flow(detailsCols, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(detailsCols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_nodesDetail = lv_label_create(detailsCols);
    lv_obj_set_width(s_nodesDetail, lv_pct(50));
    lv_obj_set_style_text_font(s_nodesDetail, nodesDetailFont, 0);
    lv_obj_set_style_text_color(s_nodesDetail, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(s_nodesDetail, LV_LABEL_LONG_WRAP);

    s_nodesDetailExtra = lv_label_create(detailsCols);
    lv_obj_set_width(s_nodesDetailExtra, lv_pct(50));
    lv_obj_set_style_text_font(s_nodesDetailExtra, nodesDetailFont, 0);
    lv_obj_set_style_text_color(s_nodesDetailExtra, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(s_nodesDetailExtra, LV_LABEL_LONG_WRAP);
#else
    s_nodesDetail = lv_label_create(left);
    lv_obj_set_width(s_nodesDetail, lv_pct(100));
    lv_obj_set_style_text_font(s_nodesDetail, nodesDetailFont, 0);
    lv_obj_set_style_text_color(s_nodesDetail, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(s_nodesDetail, LV_LABEL_LONG_WRAP);
#endif

    lv_obj_t *right = lv_obj_create(content);
    lv_obj_set_width(right, rightW);
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(right, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_40, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_border_color(right, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(right, 2, 0);
    lv_obj_set_style_pad_right(right, 1, 0);
    lv_obj_set_style_pad_top(right, 2, 0);
    lv_obj_set_style_pad_bottom(right, 2, 0);

    s_nodesList = lv_obj_create(right);
    lv_obj_set_size(s_nodesList, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s_nodesList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_nodesList);
    lv_obj_set_scrollbar_mode(s_nodesList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(s_nodesList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_nodesList, 0, 0);
    lv_obj_set_style_pad_left(s_nodesList, 1, 0);
    lv_obj_set_style_pad_right(s_nodesList, 8, 0);
    lv_obj_set_style_pad_top(s_nodesList, 0, 0);
    lv_obj_set_style_pad_bottom(s_nodesList, 0, 0);
    lv_obj_set_style_pad_row(s_nodesList, 2, 0);
    lv_obj_set_style_width(s_nodesList, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_nodesList, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_nodesList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_nodesList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_nodesList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_nodesList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    s_nodesListRowCount = 0;
    memset(s_nodesListRows, 0, sizeof(s_nodesListRows));

    nodesApplyFilter();
    refreshNodesListRows();

    refreshNodesListSelection();
    refreshNodesDetails();

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    appendHeltecBottomNav(s_nodesModal, HELTEC_NAV_NODES);
#else
    lv_obj_t *hint = lv_label_create(s_nodesModal);
    s_nodesHintLabel = hint;
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if defined(DEVICE_TDECK)
    lv_label_set_text_fmt(hint, "J/K=Select   Enter=Actions   %s=Back", modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint, "Up/Down=Select   Enter=Actions   %s=Back", modalCloseKeyLabel());
#endif
#endif
}

static void openLegendModal() {
    if (s_legendModal && !lvObjAlive(s_legendModal)) {
        s_legendModal = nullptr;
    }
    if (!s_rootScreen || s_legendModal) return;
    closeDmModal();

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    int modalH = 132;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    modalH = 142;
#if defined(DEVICE_UI_VERTICAL)
    // Vertical Heltec wraps legend body text into more lines; reserve extra
    // height so the Close button remains fully visible with padding.
    modalH = 162;
#endif
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    modalH = 146;
#endif
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;

#if defined(DEVICE_TDECK)
    const lv_font_t *legendBodyFont = &lv_font_montserrat_10;
#elif defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *legendBodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *legendBodyFont = &lv_font_montserrat_10;
#endif

    s_legendModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_legendModal, modalW, modalH);
    lv_obj_align(s_legendModal, LV_ALIGN_CENTER, 0, 0);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_obj_add_flag(s_legendModal, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_legendModal);
    lv_obj_set_scrollbar_mode(s_legendModal, LV_SCROLLBAR_MODE_AUTO);
#else
    lv_obj_clear_flag(s_legendModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_legendModal, LV_SCROLLBAR_MODE_OFF);
#endif
    lv_obj_set_style_bg_color(s_legendModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_legendModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_legendModal, 1, 0);
    lv_obj_set_style_border_color(s_legendModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_legendModal, 6, 0);
    lv_obj_set_style_pad_row(s_legendModal, 4, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_set_style_pad_bottom(s_legendModal, 8, 0);
    lv_obj_set_style_pad_row(s_legendModal, 5, 0);
#if defined(DEVICE_UI_VERTICAL)
    lv_obj_set_style_pad_bottom(s_legendModal, 10, 0);
#endif
#endif
    lv_obj_set_flex_flow(s_legendModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_legendModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_legendModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Help");

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *body = lv_label_create(s_legendModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, legendBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(
        body,
        "Touch Navigation:\n"
        "Use bottom buttons for Config, DM, Nodes, Live, Help.\n"
        "\n"
        "Transport Symbols:\n"
        "%s Radio Transmission\n"
        "%s MQTT Transmission",
        LV_SYMBOL_RADIO_TINY,
        LV_SYMBOL_GLOBE_TINY);
#elif defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    lv_obj_t *bodyRow = lv_obj_create(s_legendModal);
    lv_obj_set_width(bodyRow, lv_pct(100));
    lv_obj_set_flex_grow(bodyRow, 1);
    lv_obj_clear_flag(bodyRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bodyRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bodyRow, 0, 0);
    lv_obj_set_style_pad_all(bodyRow, 0, 0);
    lv_obj_set_style_pad_column(bodyRow, 8, 0);
    lv_obj_set_flex_flow(bodyRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bodyRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *leftCol = lv_label_create(bodyRow);
    lv_obj_set_width(leftCol, lv_pct(50));
    lv_obj_set_style_text_font(leftCol, legendBodyFont, 0);
    lv_obj_set_style_text_color(leftCol, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(leftCol, LV_LABEL_LONG_WRAP);
#if defined(DEVICE_TDECK)
    lv_label_set_text(
        leftCol,
        "(D) Direct Messages\n"
        "(C) Configuration\n"
        "(N) Nodes\n"
        "(L) Live (C clears log)\n"
        "(E) Emoji\n"
        "(H) Help\n"
        "(Space) Compose/Reply\n"
        "(Enter) Focus Messages");
#else
    lv_label_set_text(
        leftCol,
        "(D) Direct Messages\n"
        "(C) Configuration\n"
        "(N) Nodes\n"
        "(L) Live (C clears log)\n"
        "(E) Emoji\n"
        "(H) Help\n"
        "(Space) Compose/Reply\n"
        "(Enter) Focus Messages");
#endif

    lv_obj_t *rightCol = lv_obj_create(bodyRow);
    lv_obj_set_width(rightCol, lv_pct(50));
    lv_obj_set_flex_grow(rightCol, 1);
    lv_obj_clear_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(rightCol, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rightCol, 0, 0);
    lv_obj_set_style_pad_all(rightCol, 0, 0);
    lv_obj_set_style_pad_row(rightCol, 2, 0);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *rightMain = lv_label_create(rightCol);
    lv_obj_set_width(rightMain, lv_pct(100));
    lv_obj_set_style_text_font(rightMain, legendBodyFont, 0);
    lv_obj_set_style_text_color(rightMain, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(rightMain, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(
        rightMain,
        "Transport Symbols:\n"
        "%s Radio Transmission\n"
        "%s MQTT Transmission",
        LV_SYMBOL_RADIO_TINY,
        LV_SYMBOL_GLOBE_TINY);

#if defined(DEVICE_TDECK)
    lv_obj_t *rightNote = lv_label_create(rightCol);
    lv_obj_set_width(rightNote, lv_pct(100));
    // Use a larger font to emphasize this as a bold-style callout.
    lv_obj_set_style_text_font(rightNote, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rightNote, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_long_mode(rightNote, LV_LABEL_LONG_WRAP);
    lv_label_set_text(rightNote, "\nT-Deck trackball: hold click 2s to sleep");
#endif
#else
    lv_obj_t *body = lv_label_create(s_legendModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, legendBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(
        body,
        "(D) Direct Messages\n"
        "(C) Configuration\n"
        "(N) Nodes\n"
        "(L) Live (C clears log)\n"
        "(E) Emoji\n"
        "(H) Help\n"
        "(Space) Compose/Reply\n"
        "(Enter) Focus Messages\n"
        "\n"
        "Transport Symbols:\n"
        "%s Radio Transmission\n"
        "%s MQTT Transmission",
        LV_SYMBOL_RADIO_TINY,
        LV_SYMBOL_GLOBE_TINY);
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *closeBtn = lv_btn_create(s_legendModal);
    lv_obj_set_width(closeBtn, lv_pct(100));
    lv_obj_set_height(closeBtn, 24);
    lv_obj_set_style_radius(closeBtn, 4, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(closeBtn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(closeBtn, 1, 0);
    lv_obj_set_style_border_color(closeBtn, lv_color_hex(0x335D9D), 0);
    lv_obj_add_event_cb(closeBtn, onLegendClosePressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *closeLbl = lv_label_create(closeBtn);
    lv_obj_set_style_text_font(closeLbl, legendBodyFont, 0);
    lv_obj_set_style_text_color(closeLbl, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(closeLbl, "Close");
    lv_obj_center(closeLbl);
#else
    lv_obj_t *hint = lv_label_create(s_legendModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, legendBodyFont, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "Backspace to close Help");
#endif

    refreshChatComposeButtonState();
}

static void openCfgModal() {
    if (s_cfgModal && !lvObjAlive(s_cfgModal)) {
        s_cfgModal = nullptr;
        s_cfgActionList = nullptr;
        s_cfgInfoList = nullptr;
        s_cfgHeaderStatus = nullptr;
    }
    if (!s_rootScreen || s_cfgModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeLegendModal();
    closeChannelActionsModal();

    initCfgActions();
    s_cfgSelection = 0;
    s_cfgStatus[0] = '\0';
    s_cfgOtaInstallArmed = false;
    s_cfgOtaLatestTag[0] = '\0';
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    s_cfgLastActivateMs = 0;
    s_cfgLastScrollMs = 0;
    s_cfgEnterLockUntilMs = 0;
    s_cfgAwaitEnterRelease = false;
    s_cfgInfoPanelFocused = false;
    if (s_cfgDebugLog) {
        Serial.printf("[lvgl-cfg] open actions=%d\n", s_cfgActionCount);
    }

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);

    s_cfgModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgModal, modalW, modalH);
    lv_obj_align(s_cfgModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_cfgModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgModal, 4, 0);
    lv_obj_set_style_pad_row(s_cfgModal, 4, 0);
    lv_obj_set_flex_flow(s_cfgModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = lv_obj_create(s_cfgModal);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 30);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(header, 4, 0);
    lv_obj_set_style_pad_right(header, 4, 0);
    lv_obj_set_style_pad_top(header, 2, 0);
    lv_obj_set_style_pad_bottom(header, 2, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
#elif defined(DEVICE_TDECK)
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
#else
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
#endif
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Configuration");

    s_cfgHeaderStatus = lv_label_create(header);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_set_width(s_cfgHeaderStatus, lv_pct(40));
#else
    lv_obj_set_width(s_cfgHeaderStatus, lv_pct(58));
#endif
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_set_style_text_font(s_cfgHeaderStatus, &lv_font_montserrat_12, 0);
#elif defined(DEVICE_TDECK)
    lv_obj_set_style_text_font(s_cfgHeaderStatus, &lv_font_montserrat_14, 0);
#else
    lv_obj_set_style_text_font(s_cfgHeaderStatus, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_align(s_cfgHeaderStatus, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_cfgHeaderStatus, lv_color_hex(0x79DDB8), 0);
    lv_label_set_long_mode(s_cfgHeaderStatus, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_cfgHeaderStatus, "Ready");

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *infoBtn = lv_btn_create(header);
    lv_obj_set_height(infoBtn, 22);
    lv_obj_set_style_min_width(infoBtn, 48, 0);
    lv_obj_set_style_radius(infoBtn, 4, 0);
    lv_obj_set_style_shadow_width(infoBtn, 0, 0);
    lv_obj_set_style_bg_color(infoBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(infoBtn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(infoBtn, 1, 0);
    lv_obj_set_style_border_color(infoBtn, lv_color_hex(0x335D9D), 0);
    lv_obj_add_event_cb(infoBtn, onCfgHeaderInfoPressed, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *infoLbl = lv_label_create(infoBtn);
    lv_obj_set_style_text_font(infoLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(infoLbl, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(infoLbl, "Info");
    lv_obj_center(infoLbl);
#endif

#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_t *cfgColumns = lv_obj_create(s_cfgModal);
    lv_obj_set_width(cfgColumns, lv_pct(100));
    lv_obj_set_flex_grow(cfgColumns, 1);
    lv_obj_clear_flag(cfgColumns, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cfgColumns, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cfgColumns, 0, 0);
    lv_obj_set_style_pad_all(cfgColumns, 0, 0);
    lv_obj_set_style_pad_column(cfgColumns, 4, 0);
    lv_obj_set_flex_flow(cfgColumns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cfgColumns, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_cfgActionList = lv_obj_create(cfgColumns);
    lv_obj_set_width(s_cfgActionList, lv_pct(58));
    lv_obj_set_height(s_cfgActionList, lv_pct(100));
    lv_obj_add_flag(s_cfgActionList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgActionList);
    lv_obj_set_scrollbar_mode(s_cfgActionList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgActionList, 1, 0);
    lv_obj_set_style_border_color(s_cfgActionList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgActionList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgActionList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgActionList, 2, 0);
    lv_obj_set_style_width(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_cfgActionList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_cfgInfoList = lv_obj_create(cfgColumns);
    lv_obj_set_width(s_cfgInfoList, lv_pct(42));
    lv_obj_set_height(s_cfgInfoList, lv_pct(100));
    lv_obj_add_flag(s_cfgInfoList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgInfoList);
    lv_obj_set_scrollbar_mode(s_cfgInfoList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgInfoList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgInfoList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgInfoList, 1, 0);
    lv_obj_set_style_border_color(s_cfgInfoList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgInfoList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgInfoList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgInfoList, 2, 0);
    lv_obj_set_style_width(s_cfgInfoList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_cfgInfoList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_cfgInfoList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_cfgInfoList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_cfgInfoList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgInfoList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#else
    s_cfgActionList = lv_obj_create(s_cfgModal);
    lv_obj_set_width(s_cfgActionList, lv_pct(100));
    lv_obj_set_flex_grow(s_cfgActionList, 1);
    lv_obj_add_flag(s_cfgActionList, LV_OBJ_FLAG_SCROLLABLE);
    setupVScroll(s_cfgActionList);
    lv_obj_set_scrollbar_mode(s_cfgActionList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_cfgActionList, 1, 0);
    lv_obj_set_style_border_color(s_cfgActionList, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_cfgActionList, 0, 0);
    lv_obj_set_style_pad_row(s_cfgActionList, 1, 0);
    lv_obj_set_style_pad_right(s_cfgActionList, 2, 0);
    lv_obj_set_style_width(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_cfgActionList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_cfgActionList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_cfgActionList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgActionList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    appendHeltecBottomNav(s_cfgModal, HELTEC_NAV_CFG);
#else
    lv_obj_t *hint = lv_label_create(s_cfgModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_pad_top(hint, 0, 0);
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#if defined(DEVICE_TDECK)
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
#else
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "To close hit backspace.  (I)nformation");
#endif

    refreshCfgModal();
}

static void activateCfgSelection() {
    if (s_cfgActionCount <= 0 || s_cfgSelection < 0 || s_cfgSelection >= s_cfgActionCount) return;
    const int actionId = s_cfgActions[s_cfgSelection];
    if (cfgActionDisabled(actionId)) {
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s",
                 (actionId == CFG_ACTION_WEBCFG && s_cfg.mqttEnabled)
                     ? "Web Config locked while MQTT is on"
                     : "Enable WiFi first");
        openCfgActionMessageModal(s_cfgStatus);
        return;
    }
    if (s_cfgDebugLog) {
        char actionText[80];
        Serial.printf("[lvgl-cfg] activate sel=%d action=%d label=\"%s\" confirmAction=%d dtConfirm=%lu dtAct=%lu dtScroll=%lu\n",
                      s_cfgSelection,
                      actionId,
                      cfgActionLabel(actionId, actionText, sizeof(actionText)),
                      s_cfgConfirmAction,
                      (unsigned long)(millis() - s_cfgConfirmMs),
                      (unsigned long)(millis() - s_cfgLastActivateMs),
                      (unsigned long)(millis() - s_cfgLastScrollMs));
    }

    if (cfgActionNeedsConfirm(actionId)) {
        openCfgConfirmModal(actionId);
        return;
    }

    performCfgAction(actionId);
}

// Executes a CFG action immediately. Confirmable actions reach here only after
// the user answers Yes in the confirmation dialog; others come straight from
// activateCfgSelection.
static void performCfgAction(int actionId) {
    s_cfgConfirmAction = -1;
    s_cfgConfirmMs = 0;
    bool showActionPopup = true;

    switch (actionId) {
        case CFG_ACTION_WEBCFG: {
            showActionPopup = false;
            if (s_webCfgEnabled) {
                Serial.println("[web] CFG action: disable web config");
                s_webCfgEnabled = false;
                persistWebCfgEnabled();
                if (webCfgRunning()) {
                    webCfgEnd();   // tears the radio down; re-associate STA now
                }
                s_wifiStaKickMs = 0;   // reconnect station immediately for the IP
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web Config disabled");
            } else {
                Serial.println("[web] CFG action: enable web config");
                s_webCfgEnabled = true;
                persistWebCfgEnabled();

#if HAS_SD_CARD
                bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, captureWebScreenshotPng);
#else
                bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
#endif
                if (ok) {
                    if (webCfgIsOnboarding()) {
                        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Setup: %s", webCfgIP());
                    } else {
                        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web: %s", webCfgIP());
                    }
                } else {
                    s_webCfgEnabled = false;
                    persistWebCfgEnabled();
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web Config start failed");
                }
            }
        } break;

        case CFG_ACTION_GPS_TOGGLE: {
            s_cfg.gpsEnabled = !s_cfg.gpsEnabled;
            gpsSetEnabled(s_cfg.gpsEnabled);
            persistConfigToPrefs();
            if (s_cfg.gpsEnabled) {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         "GPS enabled (hardware)");
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                         "GPS disabled (using default coordinates)");
            }
            refreshHeaderStatus(true);
        } break;

        case CFG_ACTION_EXPORT: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec EXPORT");
            bool ok = cfgExport(s_cfg);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     ok ? "Exported to /camillia/config.yaml" : "Export FAILED (no SD?)");
        } break;

        case CFG_ACTION_IMPORT: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec IMPORT");
            bool ok = cfgImport(s_cfg);
            if (ok) {
                onWebCfgSaved();
                persistUiTheme();
                persistMessageAlertSetting();
                persistSplashMelodySetting();
                s_lastRenderedChannel = -1;
                s_lastRenderedCount = -1;
                refreshHeaderTime(true);
                refreshHeaderStatus(true);
                refreshChannelGlow(true);
                refreshChatView(true);
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Imported OK - rebooting...");
                refreshCfgModal();
                lv_timer_handler();
                delay(1000);
                ESP.restart();
            } else {
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Import FAILED (no file?)");
            }
        } break;

        case CFG_ACTION_THEME: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec THEME");
            int next = (uiThemePresetIndexFromCfg() + 1) % kUiThemePresetCount;
            s_cfg.uiTheme = kUiThemePresets[next].theme;
            s_cfg.uiMode = kUiThemePresets[next].mode;
            persistUiTheme();
            applyUiThemePalette();
            scheduleThemeRebuild(true);
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Theme: %s", uiThemePresetNameFromCfg());
            openCfgActionMessageModal(s_cfgStatus);
            // Do not rebuild/clean cfg rows in this same input cycle.
            // The deferred theme rebuild will recreate the modal safely.
            return;
        } break;

        case CFG_ACTION_OWNER_COLOR:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec OWNER_COLOR");
            showActionPopup = false;
            openCfgColorPickerModal();
            break;

        case CFG_ACTION_UNITS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec UNITS");
            s_cfg.displayUnits = (uint8_t)(s_cfg.displayUnits ? 0 : 1);
            persistConfigToPrefs();
            refreshNodesDetails();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Units: %s",
                     s_cfg.displayUnits ? "Imperial" : "Metric");
            break;

        case CFG_ACTION_CHAT_STYLE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHAT_STYLE");
            showActionPopup = false;
            openChatStyleModal();   // pick directly; reboots only on a real change
            break;

        case CFG_ACTION_CHAT_NAMES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHAT_NAMES");
            showActionPopup = false;
            openChatNameModal();    // pick directly; applies live, no reboot
            break;

        case CFG_ACTION_CHAT_COLORS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHAT_COLORS");
            s_cfg.chatColorsEnabled = !s_cfg.chatColorsEnabled;
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Chat Colors: %s",
                     s_cfg.chatColorsEnabled ? "On" : "Off");
            s_lastRenderedChannel = -1;
            s_lastRenderedCount = -1;
            refreshChatView(true);
            break;

        case CFG_ACTION_FONT_SIZE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec FONT_SIZE");
            showActionPopup = false;
            openFontSizeModal();    // pick directly; applies live, no reboot
            break;

        case CFG_ACTION_ANNOUNCE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec ANNOUNCE");
            webCfgQueueAnnounce();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "NODEINFO broadcast queued.");
            break;

        case CFG_ACTION_TELEMETRY:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec TELEMETRY");
            webCfgQueueTelemetry();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Telemetry TX queued.");
            break;

        case CFG_ACTION_NEIGHBOR_INFO:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec NEIGHBOR_INFO");
            s_cfg.neighborInfoEnabled = !s_cfg.neighborInfoEnabled;
            persistConfigToPrefs();
            if (s_cfg.neighborInfoEnabled) {
                s_nextNeighborInfoTxMs = 0;
            }
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     "Neighborhood info: %s",
                     s_cfg.neighborInfoEnabled ? "On" : "Off");
            break;

        case CFG_ACTION_SNF_CLIENT:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SNF_CLIENT");
            s_cfg.snfClientEnabled = !s_cfg.snfClientEnabled;
            persistConfigToPrefs();
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     "Store&Fwd Client: %s",
                     s_cfg.snfClientEnabled ? "On" : "Off");
            break;

        case CFG_ACTION_WIFI_TOGGLE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec WIFI_TOGGLE");
            s_cfg.wifiEnabled = !s_cfg.wifiEnabled;
            s_wifiStaKickMs = 0;   // (re)associate station promptly on enable
            if (!s_cfg.wifiEnabled) {
                // Master off: stop both consumers and drop the radio.
                s_cfg.mqttEnabled = false;
                if (s_webCfgEnabled) {
                    s_webCfgEnabled = false;
                    persistWebCfgEnabled();
                }
                if (webCfgRunning()) webCfgEnd();
                mqttBridgeConfigChanged();
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
            }
            persistConfigToPrefs();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     "WiFi %s", s_cfg.wifiEnabled ? "on" : "off");
            break;

        case CFG_ACTION_CHOOSE_WIFI:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CHOOSE_WIFI");
            showActionPopup = false;
            openCfgWifiPickerModal();
            break;

        case CFG_ACTION_MQTT_TOGGLE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec MQTT_TOGGLE");
            s_cfg.mqttEnabled = !s_cfg.mqttEnabled;
            persistConfigToPrefs();
            // Reboot so the change applies cleanly; the load-time invariant also
            // enforces MQTT/web-config mutual exclusion on the way back up.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                     "MQTT Bridge: %s - rebooting...", s_cfg.mqttEnabled ? "On" : "Off");
            refreshCfgModal();
            lv_timer_handler();
            delay(1000);
            ESP.restart();
            break;

        case CFG_ACTION_MSG_ALERT:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec MSG_ALERT");
            showActionPopup = false;
            openAlertSoundModal();   // pick directly; navigating previews each tone
            break;

        case CFG_ACTION_SPLASH_MELODY:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SPLASH_MELODY");
            s_cfg.splashMelodyEnabled = !s_cfg.splashMelodyEnabled;
            persistSplashMelodySetting();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Splash melody: %s", s_cfg.splashMelodyEnabled ? "On" : "Off");
            break;

        case CFG_ACTION_OTA_UPDATE: {
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec OTA_UPDATE");
            Serial.println("[ota-worker] firmware update action requested");
            s_cfgOtaInstallArmed = false;
            s_cfgOtaLatestTag[0] = '\0';

#if defined(DEVICE_CARDPUTER_LORA_HAT)
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     "OTA disabled on Cardputer build");
            break;
#endif

            bool webWasRunning = webCfgRunning();
            if (webWasRunning) {
                Serial.println("[ota-worker] stopping web config to free heap before TLS");
                webCfgEnd();
                delay(120);
            }

            bool flagSaved = requestOtaWorkerModeOnce();
            bool nvsVisible = isOtaWorkerModeRequestedOnce();
            bool rtcVisible = isOtaWorkerModeRequestedRtc();
            bool flagVisible = nvsVisible || rtcVisible;
            Serial.printf("[ota-worker] request flag saved=%d visible=%d (nvs=%d rtc=%d)\n",
                          flagSaved ? 1 : 0,
                          flagVisible ? 1 : 0,
                          nvsVisible ? 1 : 0,
                          rtcVisible ? 1 : 0);
            if (!(flagSaved && flagVisible)) {
                snprintf(s_cfgStatus,
                         sizeof(s_cfgStatus),
                         "OTA request failed (NVS write). Retry.");
                break;
            }

            Serial.println("[ota-worker] attempting immediate worker run");
            if (runOtaWorkerModeIfRequested()) {
                if (s_otaWorkerBootNotice[0]) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", s_otaWorkerBootNotice);
                } else {
                    snprintf(s_cfgStatus,
                             sizeof(s_cfgStatus),
                             "OTA worker completed. Check status.");
                }
                break;
            }

            Serial.println("[ota-worker] immediate worker did not start; falling back to reboot handoff");
            requestSkipWebAutoStartOnce();
            snprintf(s_cfgStatus,
                     sizeof(s_cfgStatus),
                     "Rebooting into OTA minimal mode...");
            refreshCfgModal();
            openCfgActionMessageModal(s_cfgStatus);
            delay(800);
            ESP.restart();
        } break;

        case CFG_ACTION_CLEAR_MSGS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CLEAR_MSGS");
            Channels.clearAllMessages(true);
            DMs.clearAll(true);
            s_selectedMsgReplyPacketId = 0;
            s_selectedMsgText[0] = '\0';
            s_lastRenderedChannel = -1;
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Messages cleared - rebooting...");
            refreshCfgModal();
            delay(1000);
            ESP.restart();
            refreshChatView(true);
            break;

        case CFG_ACTION_CLEAR_NODES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CLEAR_NODES");
            Nodes.clearPersisted();
            clearNodeDbOnSd();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Node DB cleared - rebooting...");
            refreshCfgModal();
            delay(1000);
            ESP.restart();
            break;

        case CFG_ACTION_FACTORY_RESET:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec FACTORY_RESET");
            Channels.clearAllMessages(true);
            DMs.clearAll(true);
            Nodes.clearPersisted();
            clearNodeDbOnSd();
            sdRmDirRecursive("/camillia/dms");
            nvs_flash_erase();
            nvs_flash_init();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Factory reset - rebooting...");
            refreshCfgModal();
            delay(1000);
            ESP.restart();
            break;
    }

    if (showActionPopup && s_cfgStatus[0]) {
        openCfgActionMessageModal(s_cfgStatus);
    }
    refreshCfgModal();
}

static void closeCfgConfirmModal() {
    if (lvObjValid(s_cfgConfirmBackdrop)) {
        lv_obj_del(s_cfgConfirmBackdrop);
    } else if (lvObjValid(s_cfgConfirmModal)) {
        lv_obj_del(s_cfgConfirmModal);
    }
    s_cfgConfirmBackdrop = nullptr;
    s_cfgConfirmModal = nullptr;
    s_cfgConfirmPendingAction = -1;
}

// Yes: run the pending action. No/cancel: just close the dialog, returning to
// the CFG screen underneath.
static void cfgConfirmAccept() {
    int action = s_cfgConfirmPendingAction;
    if (action == CFG_ACTION_OTA_UPDATE) {
        Serial.println("[ota-worker] confirm accepted for firmware update");
    }
    closeCfgConfirmModal();
    if (action >= 0) performCfgAction(action);
}

static void cfgConfirmReject() {
    closeCfgConfirmModal();
    refreshCfgModal();
}

static void onCfgConfirmYesPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgConfirmAccept();
}

static void onCfgConfirmNoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    cfgConfirmReject();
}

static void openCfgConfirmModal(int actionId) {
    if (!s_rootScreen || s_cfgConfirmModal || s_cfgConfirmBackdrop) return;
    s_cfgConfirmPendingAction = actionId;

    char actionText[96];
    cfgActionLabel(actionId, actionText, sizeof(actionText));

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = lv_disp_get_hor_res(NULL) - 40;
    if (modalW < 160) modalW = lv_disp_get_hor_res(NULL) - 8;
    if (modalW > 300) modalW = 300;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t actionBg = lightUi ? lv_color_hex(0xF5F9FF) : lv_color_hex(0x123266);
    const lv_color_t actionTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const uint32_t noBtnBg = lightUi ? 0xC76565 : 0x6B3030;
    const uint32_t yesBtnBg = lightUi ? 0x429A56 : 0x2F6B30;
    const lv_color_t btnTextColor = lv_color_hex(0xFFFFFF);

    // Full-screen backdrop makes the dialog truly modal for touch builds.
    s_cfgConfirmBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_cfgConfirmBackdrop, w, h);
    lv_obj_align(s_cfgConfirmBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgConfirmBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgConfirmBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgConfirmBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_cfgConfirmBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_cfgConfirmBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_cfgConfirmBackdrop, 0, 0);

    s_cfgConfirmModal = lv_obj_create(s_cfgConfirmBackdrop);
    lv_obj_set_size(s_cfgConfirmModal, modalW, LV_SIZE_CONTENT);
    lv_obj_align(s_cfgConfirmModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_cfgConfirmModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfgConfirmModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_cfgConfirmModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_cfgConfirmModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgConfirmModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgConfirmModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_cfgConfirmModal, 10, 0);
    lv_obj_set_style_pad_row(s_cfgConfirmModal, 10, 0);
    lv_obj_set_flex_flow(s_cfgConfirmModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgConfirmModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgConfirmBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgConfirmModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title,
                      actionId == CFG_ACTION_CHAT_STYLE ? "Confirm Chat Style" : "Confirm?");

    lv_obj_t *actionBox = lv_obj_create(s_cfgConfirmModal);
    lv_obj_set_width(actionBox, lv_pct(100));
    lv_obj_set_height(actionBox, LV_SIZE_CONTENT);
    lv_obj_clear_flag(actionBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(actionBox, actionBg, 0);
    lv_obj_set_style_bg_opa(actionBox, LV_OPA_60, 0);
    lv_obj_set_style_border_width(actionBox, 1, 0);
    lv_obj_set_style_border_color(actionBox, modalBorder, 0);
    lv_obj_set_style_pad_left(actionBox, 6, 0);
    lv_obj_set_style_pad_right(actionBox, 6, 0);
    lv_obj_set_style_pad_top(actionBox, 4, 0);
    lv_obj_set_style_pad_bottom(actionBox, 4, 0);

    lv_obj_t *q = lv_label_create(actionBox);
    lv_obj_set_width(q, lv_pct(100));
    lv_obj_set_style_text_font(q, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(q, actionTextColor, 0);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    if (actionId == CFG_ACTION_CHAT_STYLE) {
        lv_label_set_text(q, actionText);
    } else {
        lv_label_set_text_fmt(q, "Action: %s", actionText);
    }

    lv_obj_t *btnRow = lv_obj_create(s_cfgConfirmModal);
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

    auto makeConfirmBtn = [](lv_obj_t *parent,
                             const char *text,
                             uint32_t bgColor,
                             lv_color_t txtColor,
                             lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_height(btn, 36);
        lv_obj_set_style_min_width(btn, 84, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, txtColor, 0);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
        return btn;
    };
    makeConfirmBtn(btnRow, "(N)o", noBtnBg, btnTextColor, onCfgConfirmNoPressed);
    makeConfirmBtn(btnRow, "(Y)es", yesBtnBg, btnTextColor, onCfgConfirmYesPressed);
}

// ── Boot firmware-update prompt ───────────────────────────────────────────────
// Once per boot, with WiFi up, ask the release server whether a newer build
// exists and offer to install it. Declining is remembered for the rest of this
// boot only — a reboot asks again. The check is one plain-HTTP GET against the
// hardcoded release proxy (see ota_update.cpp), so it is cheap enough to run
// inline from the UI loop; the signed binary is what actually gets verified.
static void closeOtaUpdatePrompt() {
    if (lvObjValid(s_otaPromptBackdrop)) {
        lv_obj_del(s_otaPromptBackdrop);
    } else if (lvObjValid(s_otaPromptModal)) {
        lv_obj_del(s_otaPromptModal);
    }
    s_otaPromptBackdrop = nullptr;
    s_otaPromptModal = nullptr;
}

static void otaPromptDecline() {
    Serial.println("[ota-check] update declined for this boot");
    closeOtaUpdatePrompt();
}

static void otaPromptAccept() {
    Serial.printf("[ota-check] update accepted: %s -> %s\n", APP_VERSION, s_otaAutoCheckTag);
    closeOtaUpdatePrompt();
    // Same install path as the CFG screen's Firmware Update action: it reboots
    // into OTA minimal mode, where the download and signature check happen.
    performCfgAction(CFG_ACTION_OTA_UPDATE);
}

static void onOtaPromptNoPressed(lv_event_t *e) {
    LV_UNUSED(e);
    otaPromptDecline();
}

static void onOtaPromptYesPressed(lv_event_t *e) {
    LV_UNUSED(e);
    otaPromptAccept();
}

static void openOtaUpdatePrompt() {
    if (!s_rootScreen || s_otaPromptModal || s_otaPromptBackdrop) return;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = w - 40;
    if (modalW < 160) modalW = w - 8;
    if (modalW > 300) modalW = 300;

    const bool lightUi = (s_cfg.uiMode == UI_MODE_LIGHT);
    const lv_color_t modalBg = lightUi ? lv_color_hex(0xEAF1FB) : lv_color_hex(0x0E285B);
    const lv_color_t modalBorder = lightUi ? lv_color_hex(0x6E8FB8) : lv_color_hex(0x5C86C6);
    const lv_color_t titleTextColor = lightUi ? lv_color_hex(0x16233A) : lv_color_hex(0xD9E8FF);
    const lv_color_t versionBg = lightUi ? lv_color_hex(0xF5F9FF) : lv_color_hex(0x123266);
    const lv_color_t versionTextColor = lightUi ? lv_color_hex(0x13243D) : lv_color_hex(0xFFFFFF);
    const lv_color_t hintColor = lightUi ? lv_color_hex(0x334E75) : lv_color_hex(0xA7C7FF);

    s_otaPromptBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_otaPromptBackdrop, w, h);
    lv_obj_align(s_otaPromptBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_otaPromptBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_otaPromptBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_otaPromptBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_otaPromptBackdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_otaPromptBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_otaPromptBackdrop, 0, 0);

    s_otaPromptModal = lv_obj_create(s_otaPromptBackdrop);
    lv_obj_set_size(s_otaPromptModal, modalW, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_otaPromptModal, (h > 40) ? (h - 12) : LV_SIZE_CONTENT, 0);
    lv_obj_align(s_otaPromptModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_otaPromptModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_otaPromptModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_otaPromptModal, modalBg, 0);
    lv_obj_set_style_bg_opa(s_otaPromptModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_otaPromptModal, 1, 0);
    lv_obj_set_style_border_color(s_otaPromptModal, modalBorder, 0);
    lv_obj_set_style_pad_all(s_otaPromptModal, 8, 0);
    lv_obj_set_style_pad_row(s_otaPromptModal, 7, 0);
    lv_obj_set_flex_flow(s_otaPromptModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_otaPromptModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_otaPromptBackdrop);

    lv_obj_t *title = lv_label_create(s_otaPromptModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, titleTextColor, 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Firmware Update");

    // The version pair is the point of the dialog, so it gets its own boxed row.
    lv_obj_t *versionBox = lv_obj_create(s_otaPromptModal);
    lv_obj_set_width(versionBox, lv_pct(100));
    lv_obj_set_height(versionBox, LV_SIZE_CONTENT);
    lv_obj_clear_flag(versionBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(versionBox, versionBg, 0);
    lv_obj_set_style_bg_opa(versionBox, LV_OPA_60, 0);
    lv_obj_set_style_border_width(versionBox, 1, 0);
    lv_obj_set_style_border_color(versionBox, modalBorder, 0);
    lv_obj_set_style_pad_left(versionBox, 6, 0);
    lv_obj_set_style_pad_right(versionBox, 6, 0);
    lv_obj_set_style_pad_top(versionBox, 5, 0);
    lv_obj_set_style_pad_bottom(versionBox, 5, 0);

    lv_obj_t *versions = lv_label_create(versionBox);
    lv_obj_set_width(versions, lv_pct(100));
    lv_obj_set_style_text_font(versions, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(versions, versionTextColor, 0);
    lv_obj_set_style_text_align(versions, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(versions, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(versions, "%s -> %s", APP_VERSION, s_otaAutoCheckTag);

    lv_obj_t *hint = lv_label_create(s_otaPromptModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, hintColor, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "Install now? The device reboots to update.");

    lv_obj_t *btnRow = lv_obj_create(s_otaPromptModal);
    lv_obj_set_width(btnRow, lv_pct(100));
    lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btnRow, 0, 0);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 12, 0);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto makeOtaBtn = [](lv_obj_t *parent, const char *text, uint32_t bgColor,
                         lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_height(btn, 32);
        lv_obj_set_style_min_width(btn, 80, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
        return btn;
    };
    makeOtaBtn(btnRow, "(N)o", lightUi ? 0xC76565 : 0x6B3030, onOtaPromptNoPressed);
    makeOtaBtn(btnRow, "(Y)es", lightUi ? 0x429A56 : 0x2F6B30, onOtaPromptYesPressed);
}

// ── First-boot onboarding ─────────────────────────────────────────────────
// Shown once from setup() on freshly flashed devices. If a config exists on
// the SD card the user is offered an import; otherwise (or if declined) we
// prompt for a node name and persist it. Blocks all other UI input while up.

static void onboardingDeriveShortFromLong(const char *longName, char *shortOut, size_t shortLen) {
    if (!shortOut || shortLen == 0) return;
    shortOut[0] = '\0';
    if (!longName) return;
    size_t o = 0;
    const size_t cap = shortLen - 1;
    for (const char *p = longName; *p && o < cap; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c > 0x20 && c < 0x7F) shortOut[o++] = (char)c;
    }
    shortOut[o] = '\0';
    if (o == 0) {
        utf8util::copyTruncate(shortOut, shortLen, MY_SHORT_NAME);
    }
}

static void onboardingSetStatus(const char *msg) {
    if (!s_onboardingStatus) return;
    lv_label_set_text(s_onboardingStatus, msg ? msg : "");
}

// Roles offered during onboarding. Only client roles are supported by this
// firmware (see cfgCoerceClientRole); values are the canonical Meshtastic enum
// positions so they stay wire-compatible.
struct OnboardRoleOption { uint8_t value; const char *label; };
static const OnboardRoleOption kOnboardRoles[] = {
    {0, "CLIENT"},
    {1, "CLIENT_MUTE"},
    {8, "CLIENT_HIDDEN"},
};
static const int kOnboardRoleCount =
    (int)(sizeof(kOnboardRoles) / sizeof(kOnboardRoles[0]));

// Map a region code / role value back to its option index, falling back to the
// onboarding default (US / CLIENT_MUTE) when the value isn't recognised.
static int onboardingRegionIndex(const char *code) {
    if (code && code[0]) {
        for (uint8_t i = 0; i < kRegionCount; i++) {
            if (strcmp(kRegions[i].code, code) == 0) return (int)i;
        }
    }
    return 0;  // kRegions[0] is "US"
}

static int onboardingRoleIndex(uint8_t value) {
    for (int i = 0; i < kOnboardRoleCount; i++) {
        if (kOnboardRoles[i].value == value) return i;
    }
    return 1;  // CLIENT_MUTE
}

// Count of options on the active picker stage.
static int onboardingPickerCount() {
    return (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION)
               ? (int)kRegionCount
               : kOnboardRoleCount;
}

// Text label for the currently selected picker option.
static const char *onboardingPickerCurrentText() {
    if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION) {
        return kRegions[s_onboardingPickIndex].code;
    }
    return kOnboardRoles[s_onboardingPickIndex].label;
}

static void onboardingComputeModalSizeForStage(uint8_t stage, int screenW, int screenH,
                                               int &modalW, int &modalH) {
    modalW = screenW - 20;
    if (modalW < 180) modalW = screenW - 4;
    if (modalW > 320) modalW = 320;

    modalH = screenH - 20;
    if (modalH < 140) modalH = screenH - 4;

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    if (stage == ONBOARD_STAGE_ASK_IMPORT) {
        // Cardputer import prompt is keyboard-only and titleless, so keep the
        // modal tight to maximize visible context around it.
        modalW = screenW - 12;
        if (modalW < 180) modalW = screenW - 4;
        if (modalW > 228) modalW = 228;

        modalH = screenH - 50;
        if (modalH < 74) modalH = screenH - 6;
        if (modalH > 84) modalH = 84;
    } else {
        // Cardputer onboarding stages are keyboard-only and titleless,
        // so shrink overall height to avoid bottom clipping.
        modalW = screenW - 14;
        if (modalW < 180) modalW = screenW - 4;
        if (modalW > 228) modalW = 228;

        modalH = screenH - 44;
        if (modalH < 84) modalH = screenH - 6;
        if (modalH > 92) modalH = 92;
    }
#else
    LV_UNUSED(stage);
#endif
}

static void renderOnboardingStage() {
    if (!s_onboardingModal) return;

    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    int modalW = 0;
    int modalH = 0;
    onboardingComputeModalSizeForStage(s_onboardingStage, screenW, screenH, modalW, modalH);
    lv_obj_set_size(s_onboardingModal, modalW, modalH);
    lv_obj_align(s_onboardingModal, LV_ALIGN_CENTER, 0, 0);

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    const bool compactOnboarding = true;
    const bool compactImportStage = (s_onboardingStage == ONBOARD_STAGE_ASK_IMPORT);
#else
    const bool compactOnboarding = false;
    const bool compactImportStage = false;
#endif

    lv_obj_set_style_pad_all(s_onboardingModal,
                             compactImportStage ? 6 : (compactOnboarding ? 8 : 10),
                             0);
    lv_obj_set_style_pad_row(s_onboardingModal,
                             compactImportStage ? 4 : (compactOnboarding ? 6 : 8),
                             0);

    // Wipe children and rebuild for the current stage. Keep the modal
    // container itself so its geometry/layout stays consistent.
    lv_obj_clean(s_onboardingModal);
    s_onboardingInput = nullptr;
    s_onboardingKeyboard = nullptr;
    s_onboardingStatus = nullptr;
    s_onboardingPickLabel = nullptr;

    const lv_font_t *onboardingBodyFont =
        compactOnboarding ? &lv_font_montserrat_10 : &lv_font_montserrat_12;
    const lv_font_t *onboardingPickerFont =
        compactOnboarding ? &lv_font_montserrat_12 : &lv_font_montserrat_16;
    const lv_font_t *onboardingInputFont =
        compactOnboarding ? &lv_font_montserrat_12 : &lv_font_montserrat_14;
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_font_t *onboardingButtonFont =
        compactOnboarding ? &lv_font_montserrat_10 : &lv_font_montserrat_12;
    const int onboardingButtonH = compactOnboarding ? 30 : 36;
    const int onboardingButtonMinW = compactOnboarding ? 86 : 100;
    const int onboardingStepBtnW = compactOnboarding ? 34 : 40;
    const int onboardingStepBtnH = compactOnboarding ? 30 : 36;
#endif
    const int onboardingInputH = compactOnboarding ? 30 : 34;

#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_font_t *onboardingTitleFont =
        compactOnboarding ? &lv_font_montserrat_12 : &lv_font_montserrat_16;
    lv_obj_t *title = lv_label_create(s_onboardingModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, onboardingTitleFont, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Welcome to Camillia for Meshtastic");
#endif

    lv_obj_t *body = lv_label_create(s_onboardingModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, onboardingBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    if (s_onboardingStage == ONBOARD_STAGE_ASK_IMPORT) {
        lv_label_set_text(body,
                          "A saved config was found on SD.\n"
                          "Import it now?");

#if defined(DEVICE_CARDPUTER_LORA_HAT)
        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_onboardingStatus, "Y/Enter=Import   N/Bksp=Skip");
#else

        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow,
                        compactImportStage ? 8 : (compactOnboarding ? 10 : 14),
                        0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        const int importBtnH = compactImportStage ? 30 : onboardingButtonH;
        const int importBtnMinW = compactImportStage ? 72 : (compactOnboarding ? 80 : 84);
        const lv_font_t *importBtnFont = onboardingButtonFont;

        auto makeBtn = [](lv_obj_t *parent, const char *text, uint32_t color,
                  lv_event_cb_t cb,
                  int height,
                  int minWidth,
                  const lv_font_t *font) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, height);
            lv_obj_set_style_min_width(btn, minWidth, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, font, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
            return btn;
        };
        makeBtn(btnRow, "(N)o",  0x6B3030,
            [](lv_event_t *) { onboardingDeclineImport(); },
            importBtnH, importBtnMinW, importBtnFont);
        makeBtn(btnRow, "(Y)es", 0x2F6B30,
            [](lv_event_t *) { onboardingAcceptImport(); },
            importBtnH, importBtnMinW, importBtnFont);
    #endif
    } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION
               || s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
        // Region / role picker: a single value shown between < / > steppers so
        // touch users can change it, while the roller / j-k cycle it and Enter
        // advances. No text entry on these stages.
        const bool isRegion = (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION);
        lv_label_set_text(body,
                          isRegion ? "Select your radio region/preset."
                                   : "Select this node's role.");

        lv_obj_t *pickRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(pickRow, lv_pct(100));
        lv_obj_set_height(pickRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(pickRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(pickRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pickRow, 0, 0);
        lv_obj_set_style_pad_all(pickRow, 0, 0);
        lv_obj_set_style_pad_column(pickRow, compactOnboarding ? 6 : 10, 0);
        lv_obj_set_flex_flow(pickRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pickRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

#if !defined(DEVICE_CARDPUTER_LORA_HAT)
        auto makeStepBtn = [=](lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_size(btn, onboardingStepBtnW, onboardingStepBtnH);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3C4A66), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingPickerFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
            return btn;
        };
        makeStepBtn(pickRow, LV_SYMBOL_LEFT,
                    [](lv_event_t *) { onboardingPickerStep(-1); });
        #endif

        s_onboardingPickLabel = lv_label_create(pickRow);
            lv_obj_set_width(s_onboardingPickLabel, lv_pct(100));
        #if !defined(DEVICE_CARDPUTER_LORA_HAT)
            lv_obj_set_flex_grow(s_onboardingPickLabel, 1);
        #endif
        lv_obj_set_style_text_font(s_onboardingPickLabel, onboardingPickerFont, 0);
        lv_obj_set_style_text_color(s_onboardingPickLabel, lv_color_hex(0xE8F1FF), 0);
        lv_obj_set_style_text_align(s_onboardingPickLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_onboardingPickLabel, onboardingPickerCurrentText());

        #if !defined(DEVICE_CARDPUTER_LORA_HAT)
        makeStepBtn(pickRow, LV_SYMBOL_RIGHT,
                    [](lv_event_t *) { onboardingPickerStep(1); });
        #endif

        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
        #if defined(DEVICE_CARDPUTER_LORA_HAT)
            lv_label_set_text(s_onboardingStatus, "j/k=Change   Enter=Next   Bksp=Back");
        #elif defined(DEVICE_HELTEC_V4_EXPANSION)
        lv_label_set_text(s_onboardingStatus, "Use arrows to choose, then tap Next");
        #else
        lv_label_set_text(s_onboardingStatus, "Wheel/j-k=Change   Enter=Next");
        #endif

        #if !defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, compactOnboarding ? 10 : 14, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        auto makeBtn = [=](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, onboardingButtonH);
            lv_obj_set_style_min_width(btn, onboardingButtonMinW, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingButtonFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
            return btn;
        };
        makeBtn(btnRow, "Back", 0x3C4A66,
                [](lv_event_t *) { onboardingPickerBack(); });
        makeBtn(btnRow, "Next", 0x2F6B30,
                [](lv_event_t *) { onboardingPickerAdvance(); });
    #endif
    } else if (s_onboardingStage == ONBOARD_STAGE_CHOOSE_WIFI) {
        char summary[160];
        if (s_onboardingWifiSsidScratch[0]) {
            snprintf(summary,
                     sizeof(summary),
                     "Optional WiFi setup.\nSelected: %s\nChoose a different network or finish.",
                     s_onboardingWifiSsidScratch);
        } else {
            snprintf(summary,
                     sizeof(summary),
                     "Optional WiFi setup.\nNo network selected.\nChoose WiFi now or finish without WiFi.");
        }
        lv_label_set_text(body, summary);

        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_label_set_text(s_onboardingStatus, "N=Choose WiFi   Enter=Finish   Bksp=Back");
#else
        lv_label_set_text(s_onboardingStatus, "Choose WiFi or Finish");
#endif

#if !defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, compactOnboarding ? 8 : 10, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        auto makeBtn = [=](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, onboardingButtonH);
            lv_obj_set_style_min_width(btn, onboardingButtonMinW, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingButtonFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
            return btn;
        };

        makeBtn(btnRow, "Back", 0x3C4A66,
                [](lv_event_t *) {
                    s_onboardingStage = ONBOARD_STAGE_SELECT_ROLE;
                    s_onboardingPickIndex = onboardingRoleIndex(s_onboardingRoleScratch);
                    renderOnboardingStage();
                });
        makeBtn(btnRow, "Choose WiFi", 0x3C4A66,
                [](lv_event_t *) { openCfgWifiPickerModal(true); });
        makeBtn(btnRow, "Finish", 0x2F6B30,
                [](lv_event_t *) { onboardingFinalize(); });
#endif
    } else {
        const bool isShortStage = (s_onboardingStage == ONBOARD_STAGE_ENTER_SHORT);

        if (isShortStage) {
            char summary[96];
            snprintf(summary, sizeof(summary),
                     "Long name: %s\nEnter a short name (up to 4 chars).",
                     s_onboardingLongScratch);
            lv_label_set_text(body, summary);
        } else {
            lv_label_set_text(body, "Enter this node's long name.");
        }

        s_onboardingInput = lv_textarea_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingInput, lv_pct(100));
        lv_obj_set_height(s_onboardingInput, onboardingInputH);
        lv_obj_set_style_text_font(s_onboardingInput, onboardingInputFont, 0);
        lv_obj_set_style_text_color(s_onboardingInput, lv_color_hex(0xE8F1FF), 0);
        lv_obj_set_style_bg_color(s_onboardingInput, lv_color_hex(0x102B61), 0);
        lv_obj_set_style_bg_opa(s_onboardingInput, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_onboardingInput, 1, 0);
        lv_obj_set_style_border_color(s_onboardingInput, lv_color_hex(0x4C76BA), 0);
        lv_textarea_set_one_line(s_onboardingInput, true);
        if (isShortStage) {
            char derived[sizeof(s_cfg.nodeShort)];
            onboardingDeriveShortFromLong(s_onboardingLongScratch, derived, sizeof(derived));
            lv_textarea_set_max_length(s_onboardingInput, sizeof(s_cfg.nodeShort) - 1);
            lv_textarea_set_placeholder_text(s_onboardingInput, "Short name");
            lv_textarea_set_text(s_onboardingInput,
                                 s_onboardingShortScratch[0] ? s_onboardingShortScratch
                                                             : derived);
        } else {
            lv_textarea_set_max_length(s_onboardingInput, sizeof(s_cfg.nodeLong) - 1);
            lv_textarea_set_placeholder_text(s_onboardingInput, "Long name");
            lv_textarea_set_text(s_onboardingInput, s_onboardingLongScratch);
        }
        lv_textarea_set_cursor_pos(s_onboardingInput, LV_TEXTAREA_CURSOR_LAST);

        s_onboardingStatus = lv_label_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingStatus, lv_pct(100));
        lv_obj_set_style_text_font(s_onboardingStatus, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(s_onboardingStatus, lv_color_hex(0xA7C7FF), 0);
        lv_obj_set_style_text_align(s_onboardingStatus, LV_TEXT_ALIGN_CENTER, 0);
    #if defined(DEVICE_CARDPUTER_LORA_HAT)
        const char *statusHint = "Enter=Next";
        if (isShortStage)      statusHint = "Enter=Next   Bksp(empty)=Back";
    #elif defined(DEVICE_HELTEC_V4_EXPANSION)
        const char *statusHint = isShortStage
                                     ? "Tap Next to continue, or Back"
                                     : "Tap Next to continue";
    #else
        const char *statusHint = "Enter=Next";
        if (isShortStage)      statusHint = "Enter=Next    Bksp(empty)=Back";
    #endif
        lv_label_set_text(s_onboardingStatus, statusHint);

    #if !defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_obj_t *btnRow = lv_obj_create(s_onboardingModal);
        lv_obj_set_width(btnRow, lv_pct(100));
        lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
        lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btnRow, 0, 0);
        lv_obj_set_style_pad_all(btnRow, 0, 0);
        lv_obj_set_style_pad_column(btnRow, compactOnboarding ? 10 : 14, 0);
        lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        auto makeBtn = [=](lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb) {
            lv_obj_t *btn = lv_btn_create(parent);
            lv_obj_set_height(btn, onboardingButtonH);
            lv_obj_set_style_min_width(btn, onboardingButtonMinW, 0);
            lv_obj_set_style_radius(btn, 4, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, onboardingButtonFont, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_label_set_text(lbl, text);
            lv_obj_center(lbl);
            return btn;
        };

        if (isShortStage) {
            makeBtn(btnRow, "Back", 0x3C4A66,
                    [](lv_event_t *) {
                        s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
                        renderOnboardingStage();
                    });
        }
        const char *nextLabel = "Next";
        makeBtn(btnRow, nextLabel, 0x2F6B30,
                [](lv_event_t *) { onboardingCommitName(); });
    #endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
        s_onboardingKeyboard = lv_keyboard_create(s_onboardingModal);
        lv_obj_set_width(s_onboardingKeyboard, lv_pct(100));
        lv_obj_set_flex_grow(s_onboardingKeyboard, 1);
        lv_keyboard_set_mode(s_onboardingKeyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_onboardingKeyboard, s_onboardingInput);
        lv_obj_add_event_cb(s_onboardingKeyboard,
                            [](lv_event_t *e) {
                                lv_event_code_t c = lv_event_get_code(e);
                                if (c == LV_EVENT_READY) onboardingCommitName();
                            },
                            LV_EVENT_READY, nullptr);
#endif
    }
}

static void openOnboardingModal() {
    if (!s_rootScreen || s_onboardingModal || s_onboardingBackdrop) return;

    s_onboardingSdConfigPresent = cfgSdConfigExists();
    s_onboardingStage = s_onboardingSdConfigPresent
                            ? ONBOARD_STAGE_ASK_IMPORT
                            : ONBOARD_STAGE_ENTER_LONG;
    s_onboardingLongScratch[0] = '\0';
    s_onboardingShortScratch[0] = '\0';
    // Onboarding defaults: US region, CLIENT_MUTE role, no WiFi.
    utf8util::copyTruncate(s_onboardingRegionScratch, sizeof(s_onboardingRegionScratch),
                           MY_REGION);
    s_onboardingRoleScratch = 1;  // CLIENT_MUTE
    s_onboardingWifiSsidScratch[0] = '\0';
    s_onboardingWifiPassScratch[0] = '\0';
    s_onboardingPickIndex = 0;

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = 0;
    int modalH = 0;
    onboardingComputeModalSizeForStage(s_onboardingStage, w, h, modalW, modalH);

    s_onboardingBackdrop = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_onboardingBackdrop, w, h);
    lv_obj_align(s_onboardingBackdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_onboardingBackdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_onboardingBackdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_onboardingBackdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_onboardingBackdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_onboardingBackdrop, 0, 0);
    lv_obj_set_style_pad_all(s_onboardingBackdrop, 0, 0);

    s_onboardingModal = lv_obj_create(s_onboardingBackdrop);
    lv_obj_set_size(s_onboardingModal, modalW, modalH);
    lv_obj_align(s_onboardingModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_onboardingModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_onboardingModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_onboardingModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_onboardingModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_onboardingModal, 1, 0);
    lv_obj_set_style_border_color(s_onboardingModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_onboardingModal, 10, 0);
    lv_obj_set_style_pad_row(s_onboardingModal, 8, 0);
    lv_obj_set_flex_flow(s_onboardingModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_onboardingModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_onboardingBackdrop);

    renderOnboardingStage();
}

static void closeOnboardingModal() {
    if (lvObjValid(s_onboardingBackdrop)) {
        lv_obj_del(s_onboardingBackdrop);
    } else if (lvObjValid(s_onboardingModal)) {
        lv_obj_del(s_onboardingModal);
    }
    s_onboardingBackdrop = nullptr;
    s_onboardingModal = nullptr;
    s_onboardingInput = nullptr;
    s_onboardingKeyboard = nullptr;
    s_onboardingStatus = nullptr;
    s_onboardingPickLabel = nullptr;
}

static void onboardingAcceptImport() {
    Serial.println("[onboarding] importing SD config");
    bool ok = cfgImport(s_cfg);
    if (!ok) {
        Serial.println("[onboarding] import failed; falling back to name entry");
        s_onboardingSdConfigPresent = false;
        s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
        renderOnboardingStage();
        onboardingSetStatus("Import failed - enter a name");
        return;
    }
    persistConfigToPrefs();
    // cfgImport() also populated the CHANNEL_KEYS[] plan, but persistConfigToPrefs()
    // only saves the RhinoConfig fields — not the channel plan. Persist the
    // channels too (mirroring onWebCfgSaved) so imported channels survive the
    // reboot; otherwise the user had to re-import after boot to restore them.
    syncPrimaryChannelName();
    persistChannelsToPrefs();
    Serial.println("[onboarding] imported OK - rebooting");
    lv_timer_handler();
    delay(500);
    ESP.restart();
}

static void onboardingDeclineImport() {
    s_onboardingStage = ONBOARD_STAGE_ENTER_LONG;
    renderOnboardingStage();
}

// Advances onboarding text-entry stages (long/short name).
static void onboardingCommitName() {
    if (!s_onboardingInput) return;
    const char *typed = lv_textarea_get_text(s_onboardingInput);
    String name = typed ? String(typed) : String();
    name.trim();

    switch (s_onboardingStage) {
    case ONBOARD_STAGE_ENTER_LONG:
        if (name.length() == 0) {
            onboardingSetStatus("Long name cannot be empty");
            return;
        }
        utf8util::copyTruncate(s_onboardingLongScratch, sizeof(s_onboardingLongScratch),
                               name.c_str());
        s_onboardingStage = ONBOARD_STAGE_ENTER_SHORT;
        renderOnboardingStage();
        return;

    case ONBOARD_STAGE_ENTER_SHORT:
        if (name.length() == 0) {
            onboardingSetStatus("Short name cannot be empty");
            return;
        }
        utf8util::copyTruncate(s_onboardingShortScratch, sizeof(s_onboardingShortScratch),
                               name.c_str());
        s_onboardingStage = ONBOARD_STAGE_SELECT_REGION;
        s_onboardingPickIndex = onboardingRegionIndex(s_onboardingRegionScratch);
        renderOnboardingStage();
        return;

    default:
        return;
    }
}

// Cycle the current region/role picker option by delta (wraps around).
static void onboardingPickerStep(int delta) {
    if (s_onboardingStage != ONBOARD_STAGE_SELECT_REGION
        && s_onboardingStage != ONBOARD_STAGE_SELECT_ROLE) {
        return;
    }
    const int count = onboardingPickerCount();
    if (count <= 0) return;
    s_onboardingPickIndex = ((s_onboardingPickIndex + delta) % count + count) % count;
    if (s_onboardingPickLabel) {
        lv_label_set_text(s_onboardingPickLabel, onboardingPickerCurrentText());
    }
}

// Commit the current picker selection and move to the next stage.
static void onboardingPickerAdvance() {
    if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION) {
        utf8util::copyTruncate(s_onboardingRegionScratch, sizeof(s_onboardingRegionScratch),
                               kRegions[s_onboardingPickIndex].code);
        s_onboardingStage = ONBOARD_STAGE_SELECT_ROLE;
        s_onboardingPickIndex = onboardingRoleIndex(s_onboardingRoleScratch);
        renderOnboardingStage();
    } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
        s_onboardingRoleScratch = kOnboardRoles[s_onboardingPickIndex].value;
        s_onboardingStage = ONBOARD_STAGE_CHOOSE_WIFI;
        renderOnboardingStage();
    }
}

// Step a picker stage back to the previous stage.
static void onboardingPickerBack() {
    if (s_onboardingStage == ONBOARD_STAGE_SELECT_REGION) {
        s_onboardingStage = ONBOARD_STAGE_ENTER_SHORT;
        renderOnboardingStage();
    } else if (s_onboardingStage == ONBOARD_STAGE_SELECT_ROLE) {
        s_onboardingStage = ONBOARD_STAGE_SELECT_REGION;
        s_onboardingPickIndex = onboardingRegionIndex(s_onboardingRegionScratch);
        renderOnboardingStage();
    }
}

// Commit every onboarding selection into the live config, persist it, and
// reboot so the radio (region/preset), role, and WiFi come up with the choices.
static void onboardingFinalize() {
    utf8util::copyTruncate(s_cfg.nodeLong, sizeof(s_cfg.nodeLong), s_onboardingLongScratch);
    utf8util::copyTruncate(s_cfg.nodeShort, sizeof(s_cfg.nodeShort), s_onboardingShortScratch);
    utf8util::copyTruncate(s_cfg.region, sizeof(s_cfg.region), s_onboardingRegionScratch);
    s_cfg.deviceRole = cfgCoerceClientRole(s_onboardingRoleScratch);
    utf8util::copyTruncate(s_cfg.wifiSsid, sizeof(s_cfg.wifiSsid), s_onboardingWifiSsidScratch);
    utf8util::copyTruncate(s_cfg.wifiPass, sizeof(s_cfg.wifiPass), s_onboardingWifiPassScratch);
    // Re-derive loraFreq/BW/SF/CR from the chosen region + current preset.
    applyPresetParams(s_cfg);
    s_firstBoot = false;

    Serial.printf("[onboarding] saved nodeLong=\"%s\" nodeShort=\"%s\" region=\"%s\" "
                  "role=%u wifi=\"%s\"\n",
                  s_cfg.nodeLong, s_cfg.nodeShort, s_cfg.region, s_cfg.deviceRole,
                  s_cfg.wifiSsid);

    persistConfigToPrefs();
    Serial.println("[onboarding] complete - rebooting");
    lv_timer_handler();
    delay(500);
    ESP.restart();
}

static void pumpKeyboardInput() {
    for (int i = 0; i < 8; i++) {
        // Prioritize keyboard keys (especially Enter) before trackball deltas
        // to avoid one-off selection shifts during activation.
        char k = s_keyboard.readKey();
        bool fromTrackball = false;
        const char *src = "key";
        if (k == KEY_NONE) {
            k = s_keyboard.readTrackball();
            src = "track";
            fromTrackball = true;
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

        if (s_screenAsleep) {
            // Rolling the trackball should not wake the display.
            if (fromTrackball && k != KEY_ROLLER) {
                continue;
            }
            if (!tryWakeScreenFromInput(millis())) {
                continue;
            }
            return;
        }
        s_lastActivityMs = millis();

        bool typingContext = s_composeModal || (s_dmNodePickerModal && s_dmNodeFilterOpen)
                             || (s_onboardingModal
                                 && (s_onboardingStage == ONBOARD_STAGE_ENTER_LONG
                                     || s_onboardingStage == ONBOARD_STAGE_ENTER_SHORT));
        bool navFromJk = false;

#if defined(DEVICE_CARDPUTER_LORA_HAT)
        // Match v1 Cardputer shortcuts: ';' / '.' navigate lists, and
        // the key physically labeled '`' acts as Escape to close modals.
        k = remapCardputerUiKey(k, !typingContext);
#endif

#if !defined(DEVICE_HELTEC_V4_EXPANSION)
        navFromJk = (k == 'j' || k == 'J' || k == 'k' || k == 'K');
        // Enable vim-style j/k navigation for all keyboard-capable builds.
        k = remapJkUiKey(k, !typingContext);
#endif

        const bool invertScrollNav = kPagerWheelChatNav && !navFromJk;

        // First-boot onboarding is fully modal: swallow every other UI key
        // while it's up so the user can't wander into the main app before
        // supplying a node identity.
        if (s_onboardingModal && !s_cfgWifiModal && !s_cfgWifiScanModal && !s_cfgWifiPassModal) {
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
                            lv_textarea_del_char(s_onboardingInput);
                        }
                    }
                } else if (k >= 0x20 && k < 0x7F && s_onboardingInput) {
                    char one[2] = {k, '\0'};
                    lv_textarea_add_text(s_onboardingInput, one);
                }
            }
            continue;
        }

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

        // The CFG confirmation dialog is modal: Y or Enter confirms, N/close
        // cancels, and every other shortcut is swallowed while it's up.
        if (s_cfgConfirmModal) {
            if (k == 'y' || k == 'Y' || k == KEY_ENTER) {
                cfgConfirmAccept();
            } else if (k == 'n' || k == 'N' || isModalCloseKey(k)) {
                cfgConfirmReject();
            }
            continue;
        }

        if (s_tracerouteModal) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
            if (k == KEY_ESCAPE || isBackspaceKey(k)) {
#else
            if (isModalCloseKey(k)) {
#endif
                closeTracerouteProgressModal();
            }
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
                applyCfgColorSelection(s_cfgColorSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)          delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN)     delta = invertScrollNav ? -1 : 1;
            else if (k == KEY_PAGE_UP || k == KEY_PREV_CHAN) delta = -4;
            else if (k == KEY_PAGE_DN || k == KEY_NEXT_CHAN) delta = 4;
            if (delta != 0) {
                int next = s_cfgColorSelection + delta;
                if (next < 0) next = 0;
                if (next >= kUserMsgColorNavCount) next = kUserMsgColorNavCount - 1;
                if (next != s_cfgColorSelection) {
                    s_cfgColorSelection = next;
                    refreshCfgColorPickerModal();
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
                int next = s_chatStyleSelection + delta;
                if (next < 0) next = 0;
                if (next > CHAT_STYLE_MAX) next = CHAT_STYLE_MAX;
                if (next != s_chatStyleSelection) {
                    s_chatStyleSelection = next;
                    refreshChatStyleSelection();
                }
            }
            continue;
        }

        if (s_alertSoundModal) {
            if (isModalCloseKey(k)) {
                cancelAlertSoundModal();   // undo any previewed change
                continue;
            }
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                applyAlertSoundSelection(s_alertSoundSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_alertSoundSelection + delta;
                if (next < 0) next = 0;
                if (next > MSG_ALERT_SOUND_MAX) next = MSG_ALERT_SOUND_MAX;
                if (next != s_alertSoundSelection) previewAlertSoundSelection(next);
            }
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
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int next = s_fontSizeSelection + delta;
                if (next < 0) next = 0;
                if (next > FONT_SIZE_MAX) next = FONT_SIZE_MAX;
                if (next != s_fontSizeSelection) {
                    s_fontSizeSelection = next;
                    refreshFontSizeSelection();
                }
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
                        lv_textarea_del_char(s_cfgWifiPassInput);
                    }
                }
                continue;
            }
            if (isModalCloseKey(k)) {
                closeCfgWifiPassModal();
                refreshCfgWifiScanModal(false);
                continue;
            }
            if (k >= 0x20 && k < 0x7F && s_cfgWifiPassInput) {
                char one[2] = {k, '\0'};
                lv_textarea_add_text(s_cfgWifiPassInput, one);
                continue;
            }
            continue;
        }

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
        if (s_sysStatsModal) {
            if ((uint32_t)(millis() - s_sysStatsOpenedMs) >= 300UL) {
                closeSysStatsModal();
            }
            continue;
        }

#if !defined(DEVICE_TLORA_PAGER_TFT)
        // The (I)nformation popup layers over the CFG modal. Every keyboard
        // build scrolls it with Up/Down (J/K on T-Deck) and closes with I or the
        // modal close key. Heltec is touch-first with no keyboard, so there any
        // key just dismisses it.
        if (s_nodeInfoModal) {
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
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

        // Action-result popup over CFG is modal; any key dismisses it.
        if (s_cfgActionMsgModal) {
            uint32_t nowMs = millis();
            // Ignore immediate key repeats from the Enter that triggered the action.
            if ((uint32_t)(nowMs - s_cfgActionMsgOpenedMs) < 300UL) {
                continue;
            }
            closeCfgActionMessageModal();
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
            if (isModalCloseKey(k)) {
                closeCfgModal();
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
            if (k == KEY_SCROLL_UP) {
#if defined(DEVICE_TLORA_PAGER_TFT)
                if (s_cfgInfoPanelFocused && s_cfgInfoList) {
                    const int scrollStep = 18;
                    const int delta = kPagerWheelChatNav ? -scrollStep : scrollStep;
                    scrollListClamped(s_cfgInfoList, delta);
                    continue;
                }
#endif
                if (invertScrollNav) {
                    if (s_cfgSelection + 1 < s_cfgActionCount) {
                        s_cfgSelection++;
                        s_cfgLastScrollMs = millis();
                        s_cfgConfirmAction = -1;
                        s_cfgConfirmMs = 0;
                        cfgDebugSelection("scroll-up", s_cfgActions[s_cfgSelection]);
                        refreshCfgModal();
                    }
                } else {
                    if (s_cfgSelection > 0) {
                        s_cfgSelection--;
                        s_cfgLastScrollMs = millis();
                        s_cfgConfirmAction = -1;
                        s_cfgConfirmMs = 0;
                        cfgDebugSelection("scroll-up", s_cfgActions[s_cfgSelection]);
                        refreshCfgModal();
                    }
                }
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
                if (invertScrollNav) {
                    if (s_cfgSelection > 0) {
                        s_cfgSelection--;
                        s_cfgLastScrollMs = millis();
                        s_cfgConfirmAction = -1;
                        s_cfgConfirmMs = 0;
                        cfgDebugSelection("scroll-dn", s_cfgActions[s_cfgSelection]);
                        refreshCfgModal();
                    }
                } else {
                    if (s_cfgSelection + 1 < s_cfgActionCount) {
                        s_cfgSelection++;
                        s_cfgLastScrollMs = millis();
                        s_cfgConfirmAction = -1;
                        s_cfgConfirmMs = 0;
                        cfgDebugSelection("scroll-dn", s_cfgActions[s_cfgSelection]);
                        refreshCfgModal();
                    }
                }
                continue;
            }
            if (k == KEY_ENTER) {
                uint32_t now = millis();
                if ((uint32_t)(now - s_cfgLastScrollMs) < 180) {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Selection settling - press Enter again");
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
            if (k == KEY_ENTER || k == KEY_ROLLER) {
                emojiPickerActivate(s_emojiPickerSelection);
                continue;
            }
            int delta = 0;
            if (k == KEY_SCROLL_UP)      delta = invertScrollNav ? 1 : -1;
            else if (k == KEY_SCROLL_DN) delta = invertScrollNav ? -1 : 1;
            if (delta != 0) {
                int nxt = s_emojiPickerSelection + delta;
                if (nxt < 0) nxt = 0;
                if (nxt >= kEmojiTrayCount) nxt = kEmojiTrayCount - 1;
                if (nxt != s_emojiPickerSelection) {
                    s_emojiPickerSelection = nxt;
                    refreshEmojiPickerSelection();
                }
            }
            continue;   // swallow all other keys while the tray is up
        }

        if (s_dmModal) {
            if (s_composeModal) {
                switch (k) {
                    case KEY_ENTER:
                        sendComposeMessage();
                        break;
                    case KEY_ESCAPE:
                        closeComposePrompt();
                        break;
                    case KEY_BACKSPACE:
                    case KEY_BACKSPACE_HOLD:
                        if (s_composeInput) {
                            const char *cur = lv_textarea_get_text(s_composeInput);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                            if (cur && cur[0] && k == KEY_BACKSPACE) {
                                lv_textarea_del_char(s_composeInput);
                            }
#else
                            if (!cur || !cur[0]) {
                                closeComposePrompt();
                            } else if (k == KEY_BACKSPACE) {
                                lv_textarea_del_char(s_composeInput);
                            }
#endif
                        }
                        break;
                    default:
                        if (k >= 0x20 && k < 0x7F && s_composeInput) {
                            char one[2] = {k, '\0'};
                            lv_textarea_add_text(s_composeInput, one);
                        }
                        break;
                }
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
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
                        if (next < 0) next = 0;
                        if (next >= s_dmNodeFilteredCount) next = s_dmNodeFilteredCount - 1;
                        if (next != s_dmNodeSelection) {
                            s_dmNodeSelection = next;
                            refreshDmNodePicker(true);
                        }
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

            // T-Deck: when a conversation is selected, backspace is a delete
            // action and should not be interpreted as focus/navigation behavior.
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
            if ((k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD) && s_dmSelection > 0) {
                if (s_dmMsgPanelFocused) {
                    // Move focus out of the message pane to the "New DM" row
                    // (index 0) rather than the previously-selected conversation
                    // so a follow-up backspace closes the DM modal instead of
                    // arming the delete-conversation confirmation.
                    s_dmMsgPanelFocused = false;
                    s_dmSelection = 0;
                    s_dmDeletePendingNodeId = 0;
                    s_dmDeleteConfirmUntilMs = 0;
                    refreshDmModal(true);
                } else {
                    dmRequestDeleteSelectedConversation();
                }
                continue;
            }
#endif

            if (dmDeleteTriggerKey(k) && s_dmSelection > 0) {
                dmRequestDeleteSelectedConversation();
                continue;
            }

            if (isModalCloseKey(k)) {
                if (dmDeleteConfirmActive(millis())) {
                    s_dmDeletePendingNodeId = 0;
                    s_dmDeleteConfirmUntilMs = 0;
                    dmDeleteSetFlash("Delete canceled");
                    refreshDmModal(true);
                    continue;
                }
                closeDmModal();
                continue;
            }

            if (k == KEY_ROLLER && s_dmSelection > 0) {
                s_dmMsgPanelFocused = !s_dmMsgPanelFocused;
                refreshDmPanelFocusStyles();
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
                    const int delta = (k == KEY_SCROLL_UP)
                        ? (invertScrollNav ? scrollStep : -scrollStep)
                        : (invertScrollNav ? -scrollStep : scrollStep);
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
                        s_dmDeletePendingNodeId = 0;
                        s_dmDeleteConfirmUntilMs = 0;
                        s_dmMsgPanelFocused = false;
                        s_dmSelection = next;
                        refreshDmModal(true);
                    }
                }
                continue;
            }
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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

        if (s_nodesModal) {
            if (s_composeModal) {
                switch (k) {
                    case KEY_ENTER:
                        sendComposeMessage();
                        break;
                    case KEY_ESCAPE:
                        closeComposePrompt();
                        break;
                    case KEY_BACKSPACE:
                    case KEY_BACKSPACE_HOLD:
                        if (s_composeInput) {
                            const char *cur = lv_textarea_get_text(s_composeInput);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                            if (cur && cur[0] && k == KEY_BACKSPACE) {
                                lv_textarea_del_char(s_composeInput);
                            }
#else
                            if (!cur || !cur[0]) {
                                closeComposePrompt();
                            } else if (k == KEY_BACKSPACE) {
                                lv_textarea_del_char(s_composeInput);
                            }
#endif
                        }
                        break;
                    default:
                        if (k >= 0x20 && k < 0x7F && s_composeInput) {
                            char one[2] = {k, '\0'};
                            lv_textarea_add_text(s_composeInput, one);
                        }
                        break;
                }
                continue;
            }

            if (s_nodesActionModal) {
                if (isModalCloseKey(k)) {
                    closeNodesActionMenu();
                    continue;
                }

                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                    int next = s_nodesActionSelection;
                    if (invertScrollNav) {
                        next += (k == KEY_SCROLL_UP) ? 1 : -1;
                    } else {
                        next += (k == KEY_SCROLL_UP) ? -1 : 1;
                    }
                    if (next < 0) next = 0;
                    if (next >= kNodesActionCount) next = kNodesActionCount - 1;
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

                // Single-key shortcuts (T/D/F/I/P/X) select and execute the
                // corresponding action, mirroring the (X) hints in the labels.
                if (k >= 0x20 && k < 0x7F) {
                    char up = (k >= 'a' && k <= 'z') ? (char)(k - 32) : k;
                    for (int i = 0; i < kNodesActionCount; i++) {
                        if (kNodesActionShortcuts[i] == up) {
                            s_nodesActionSelection = i;
                            refreshNodesActionMenuSelection();
                            executeNodesActionSelection();
                            break;
                        }
                    }
                }
                continue;
            }

            if ((k == KEY_BACKSPACE || k == KEY_BACKSPACE_HOLD) && s_nodesFilterOpen) {
                if (s_nodesFilterLen > 0) {
                    s_nodesFilter[--s_nodesFilterLen] = '\0';
                } else {
                    s_nodesFilterOpen = false;
                }
                nodesApplyFilter();
                refreshNodesListRows();
                refreshNodesListSelection();
                refreshNodesDetails();
                continue;
            }

            // The filter is entered explicitly with the spacebar; the space
            // itself is never added to the filter text. Letters no longer
            // start the filter on their own — they only append once it's open.
            if (k == ' ') {
                if (!s_nodesFilterOpen) {
                    s_nodesFilterOpen = true;
                    nodesApplyFilter();
                    refreshNodesListRows();
                    refreshNodesListSelection();
                    refreshNodesDetails();
                }
                continue;
            }

            if (k > 0x20 && k < 0x7F && s_nodesFilterOpen) {
                if (s_nodesFilterLen < kNodesFilterMax) {
                    s_nodesFilter[s_nodesFilterLen++] = k;
                    s_nodesFilter[s_nodesFilterLen] = '\0';
                }
                nodesApplyFilter();
                refreshNodesListRows();
                refreshNodesListSelection();
                refreshNodesDetails();
                continue;
            }

            if (isModalCloseKey(k)) {
                closeNodesModal();
                continue;
            }
            if (k == KEY_ENTER) {
                openNodesActionMenu();
                continue;
            }
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                int nextSelected = s_nodesSelected;
                if (invertScrollNav) {
                    // Pager wheel orientation: UP should move to the next row.
                    nextSelected += (k == KEY_SCROLL_UP) ? 1 : -1;
                } else {
                    nextSelected += (k == KEY_SCROLL_UP) ? -1 : 1;
                }

                if (nextSelected < 0) nextSelected = 0;
                if (nextSelected >= s_nodesFilteredCount) nextSelected = s_nodesFilteredCount - 1;

                if (nextSelected != s_nodesSelected
                    && nextSelected >= 0
                    && nextSelected < s_nodesFilteredCount) {
                    s_nodesSelected = nextSelected;
                    refreshNodesListSelection();
                    refreshNodesDetails();
                    if (s_nodesSelected >= 0 && s_nodesSelected < s_nodesListRowCount && s_nodesListRows[s_nodesSelected]) {
                        lv_obj_scroll_to_view(s_nodesListRows[s_nodesSelected], LV_ANIM_OFF);
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
            if (k == 'u' || k == 'U') {
                openChUtilChartModal();
                continue;
            }
            if (k == 's' || k == 'S') {
                openSnrRssiChartModal();
                continue;
            }
            if (k == KEY_SCROLL_UP && s_liveList) {
                scrollListClamped(s_liveList, -18);
                continue;
            }
            if (k == KEY_SCROLL_DN && s_liveList) {
                scrollListClamped(s_liveList, 18);
                continue;
            }
            continue;
        }

        if (s_legendModal) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
            if (k == KEY_SCROLL_UP) {
                scrollListClamped(s_legendModal, 18);
                continue;
            }
            if (k == KEY_SCROLL_DN) {
                scrollListClamped(s_legendModal, -18);
                continue;
            }
#endif
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
                openLiveModal();
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
            continue;
        }

        if (!s_composeModal) {
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
            if (isChannelDropdownVisible()) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                        // Keep Cardputer selector movement aligned with existing channel-selector behavior.
                        if (navFromJk || invertScrollNav) {
                            next += (k == KEY_SCROLL_UP) ? 1 : -1;
                        } else {
                            next += (k == KEY_SCROLL_UP) ? -1 : 1;
                        }
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

#if defined(DEVICE_CARDPUTER_LORA_HAT)
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
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
#endif

            if (kUseScrollKeysForMainNav) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                // Cardputer directional labels: '/' is right, ',' is left.
                if (k == '/') k = KEY_NEXT_CHAN;
                else if (k == ',') k = KEY_PREV_CHAN;
#endif

                if (k == KEY_NEXT_CHAN || k == KEY_PREV_CHAN) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                    if (!s_cardputerMainChatPanelFocused) {
                        int nextChannel = s_activeChannel + ((k == KEY_NEXT_CHAN) ? 1 : -1);
                        if (nextChannel < 0) nextChannel = MESH_CHANNELS - 1;
                        if (nextChannel >= MESH_CHANNELS) nextChannel = 0;
                        setActiveChannel(nextChannel);
                    }
#endif
                    continue;
                }

#if defined(DEVICE_CARDPUTER_LORA_HAT)
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
                        s_selectedMsgReplyPacketId = 0;
                        s_selectedMsgText[0] = '\0';
                        s_lastRenderedChannel = -1;
                        refreshChatView(true);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                    } else if (s_cardputerMainChatPanelFocused) {
                        s_cardputerMainChatPanelFocused = false;
#endif
                    }
                    continue;
                }

                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                    if (!s_cardputerMainChatPanelFocused) {
                        if (isChannelDropdownVisible()) {
                            int next = s_cardputerDropdownSelection;
                            if (next < 0 || next >= MESH_CHANNELS) next = s_activeChannel;
                            // Channel selector expects j/k opposite from list navigation.
                            if (navFromJk || invertScrollNav) {
                                next += (k == KEY_SCROLL_UP) ? 1 : -1;
                            } else {
                                next += (k == KEY_SCROLL_UP) ? -1 : 1;
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

                        s_cardputerMainChatPanelFocused = true;
                        s_pagerChatCursorMode = true;
                        if (!pagerSelectChatCursorIndex(-1)) {
                            s_pagerChatCursorMode = false;
                        }
                        refreshChannelGlow(true);
                        continue;
                    }

                    if (s_cardputerMainChatPanelFocused) {
                        if (!s_pagerChatCursorMode) {
                            s_pagerChatCursorMode = true;
                            if (!pagerSelectChatCursorIndex(-1)) {
                                s_pagerChatCursorMode = false;
                            }
                        } else {
                            // Keep keyboard j/k direction consistent with chat-list
                            // expectations while preserving non-keyboard scroll behavior.
                            int delta;
                            if (navFromJk) delta = (k == KEY_SCROLL_UP) ? 1 : -1;
                            else           delta = (k == KEY_SCROLL_UP) ? -1 : 1;
                            pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + delta);
                        }
                        refreshChannelGlow(true);
                        continue;
                    }

#else
                    if (s_pagerChatCursorMode) {
                        int navDelta;
                        if (navFromJk) navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
                        else           navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
                        pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + navDelta);
                    } else if (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                        // Channel list uses reversed j/k semantics by request.
                        int navDelta;
                        if (navFromJk) {
                            navDelta = (k == KEY_SCROLL_UP) ? -1 : 1;
                        } else {
                            navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
                        }
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

#if defined(DEVICE_TDECK)
            // T-Deck has no scroll wheel, so j/k (and trackball scroll, which
            // shares KEY_SCROLL_UP/DN) both activate and drive the chat cursor
            // directly, rather than switching channels like the Pager's wheel.
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
#endif

            if (k == 'l' || k == 'L') {
                openLiveModal();
            } else if (k == 'a' || k == 'A') {
                openChannelActionsModal();
            } else if (k == 'd' || k == 'D') {
                openDmModal();
            } else if (k == 'c' || k == 'C') {
                openCfgModal();
            } else if (k == 'n' || k == 'N') {
                openNodesModal();
            } else if (k == 'e' || k == 'E') {
                // Quick emoji: opens the tray; picking sends a one-emoji message
                // to the active channel (see sendQuickEmoji / emojiPickerActivate).
                openEmojiPicker(true);
            } else if (k == 'h' || k == 'H') {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
                openLegendModal();
#else
                setChannelDropdownVisible(!isChannelDropdownVisible());
                refreshChannelGlow(true);
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
            // Touch-first build: Enter keeps its original new-message behavior.
            } else if (k == KEY_ENTER
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
#else
            // Space is the new-message key on keyboard builds; it replaced Enter.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
            } else if ((k == ' ' || k == KEY_FN_ENTER)
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
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
                }
#endif
            } else if (k == KEY_BACKSPACE) {
                if (s_pagerChatCursorMode) {
                    pagerExitChatCursorMode(true);
                } else if (s_selectedMsgReplyPacketId != 0 || s_selectedMsgText[0]) {
                    s_selectedMsgReplyPacketId = 0;
                    s_selectedMsgText[0] = '\0';
                    s_lastRenderedChannel = -1;
                }
            }
            continue;
        }

        switch (k) {
            case KEY_ENTER:
                sendComposeMessage();
                break;
            case KEY_ESCAPE:
                closeComposePrompt();
                break;
            case KEY_BACKSPACE:
            case KEY_BACKSPACE_HOLD:
                if (s_composeInput) {
                    const char *cur = lv_textarea_get_text(s_composeInput);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                    if (cur && cur[0] && k == KEY_BACKSPACE) {
                        lv_textarea_del_char(s_composeInput);
                    }
#else
                    if (!cur || !cur[0]) {
                        closeComposePrompt();
                    } else if (k == KEY_BACKSPACE) {
                        lv_textarea_del_char(s_composeInput);
                    }
#endif
                }
                break;
            default:
                if (k >= 0x20 && k < 0x7F && s_composeInput) {
                    char one[2] = {k, '\0'};
                    lv_textarea_add_text(s_composeInput, one);
                }
                break;
        }
    }
}

static void onChatNewMessagePressed(lv_event_t *e) {
    LV_UNUSED(e);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (s_legendModal) return;
#endif
    if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
        openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
    } else {
        openComposePrompt(0, nullptr);
    }
}

static void refreshChatComposeButtonState() {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!s_chatNewMsgLabel) return;

    if (s_chatNewMsgBtn) {
        if (s_legendModal) lv_obj_add_state(s_chatNewMsgBtn, LV_STATE_DISABLED);
        else               lv_obj_clear_state(s_chatNewMsgBtn, LV_STATE_DISABLED);
    }

    if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
        lv_label_set_text(s_chatNewMsgLabel, "Reply");
    } else {
        lv_label_set_text(s_chatNewMsgLabel, "New Message");
    }
#endif
}

static void onChatMessagePressed(lv_event_t *e) {
    lv_obj_t *label = lv_event_get_target(e);
    if (!label) return;

    uint32_t replyPacketId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    const char *txt = lv_label_get_text(label);
    setSelectedReplyContext(replyPacketId, txt ? txt : "");

    refreshChatComposeButtonState();

    // Defer redraw to the normal loop to avoid deleting the active target in-event.
    s_lastRenderedChannel = -1;
}

static void onWebCfgSaved() {
    uint8_t prevTheme = s_appliedUiTheme;
    uint8_t prevMode = s_appliedUiMode;

#if !HAS_ENV_SENSOR_TELEMETRY
    s_cfg.telEnvEnabled = false;
#endif
    if (s_cfg.telDeviceIntervalS < 3600UL) s_cfg.telDeviceIntervalS = 3600UL;
    if (s_cfg.telEnvIntervalS < 3600UL) s_cfg.telEnvIntervalS = 3600UL;
    if (s_cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        s_cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }

    persistConfigToPrefs();
    syncPrimaryChannelName();
    persistChannelsToPrefs();
    myDeviceRole = s_cfg.deviceRole;
    applyUiThemePalette();

    recomputeChannelHashes();
    deriveNodeId();
    applyTimezoneFromConfig();
    gpsSetEnabled(s_cfg.gpsEnabled);
    syncWifiCredsToPrefs();

    bool debugMonitor = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;
    s_cfg.debugAcks = debugMonitor;
    s_cfg.debugMessages = debugMonitor;
    s_cfg.debugGps = debugMonitor;
    s_cfgDebugLog = debugMonitor;
    debugSetFlags(debugMonitor, debugMonitor, debugMonitor);

    s_ntpConfigured = false;
    s_ntpServerActive[0] = '\0';
    s_ntpLastConfigureMs = 0;

    if (s_radioReady) {
        Radio.reconfigure(s_cfg.loraFreq, s_cfg.loraBw,
                          s_cfg.loraSf, s_cfg.loraCr, s_cfg.loraPower);
    }

    // Keep export an explicit user action. Auto-export after onboarding/web-save
    // can overwrite or churn SD config unexpectedly.

    if ((prevTheme != s_appliedUiTheme || prevMode != s_appliedUiMode) && s_rootScreen) {
        scheduleThemeRebuild(s_cfgModal != nullptr);
    }
}

static uint32_t pngCrc32Update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
        }
    }
    return crc;
}

static uint32_t pngAdler32Update(uint32_t adler, const uint8_t *data, size_t len) {
    if (!data) return adler;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = (adler >> 16) & 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        a += data[i];
        if (a >= 65521U) a -= 65521U;
        b += a;
        if (b >= 65521U) b %= 65521U;
    }
    return (b << 16) | a;
}

static bool fileWriteAll(File &f, const uint8_t *data, size_t len) {
    if (!data && len) return false;
    while (len > 0) {
        size_t n = f.write(data, len);
        if (n == 0) return false;
        data += n;
        len -= n;
    }
    return true;
}

static bool fileWriteBe32(File &f, uint32_t v) {
    uint8_t b[4] = {
        (uint8_t)((v >> 24) & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 8) & 0xFF),
        (uint8_t)(v & 0xFF),
    };
    return fileWriteAll(f, b, sizeof(b));
}

static bool pngWriteChunk(File &f, const char type[4], const uint8_t *data, uint32_t len) {
    if (!type) return false;
    if (!fileWriteBe32(f, len)) return false;
    if (!fileWriteAll(f, (const uint8_t *)type, 4)) return false;
    if (len > 0 && !fileWriteAll(f, data, len)) return false;

    uint32_t crc = 0xFFFFFFFFUL;
    crc = pngCrc32Update(crc, (const uint8_t *)type, 4);
    if (len > 0) crc = pngCrc32Update(crc, data, len);
    crc ^= 0xFFFFFFFFUL;
    return fileWriteBe32(f, crc);
}

static bool captureWebScreenshotPng(const char *outPath) {
#if !HAS_SD_CARD
    (void)outPath;
    return false;
#else
    if (!outPath || !outPath[0]) return false;

    const int32_t w = displayDev().width();
    const int32_t h = displayDev().height();
    if (w <= 0 || h <= 0) return false;

    const size_t rowPixels = (size_t)w;
    const size_t scanlineLen = 1 + rowPixels * 3;

#if defined(DEVICE_TDECK)
    const size_t frame565Bytes = rowPixels * (size_t)h * sizeof(uint16_t);
    uint16_t *frame565 = (uint16_t *)malloc(frame565Bytes);
    if (!frame565) return false;
    memset(frame565, 0, frame565Bytes);

    s_screenshotCaptureFrame = frame565;
    s_screenshotCaptureW = w;
    s_screenshotCaptureH = h;
    s_screenshotCaptureTouched = false;
    s_screenshotCaptureActive = true;

    lv_obj_invalidate(lv_scr_act());
    for (int i = 0; i < 12 && !s_screenshotCaptureTouched; i++) {
        lv_timer_handler();
        delay(12);
    }

    s_screenshotCaptureActive = false;
    s_screenshotCaptureFrame = nullptr;
    s_screenshotCaptureW = 0;
    s_screenshotCaptureH = 0;

    if (!s_screenshotCaptureTouched) {
        free(frame565);
        return false;
    }
#endif
    uint8_t *scanline = (uint8_t *)malloc(scanlineLen);
#if defined(DEVICE_TDECK)
    if (!scanline) {
        free(frame565);
        free(scanline);
        return false;
    }
#else
    if (!scanline) {
        free(scanline);
        return false;
    }
#endif

    if (SD.exists(outPath)) SD.remove(outPath);
    File f = SD.open(outPath, FILE_WRITE);
    if (!f) {
#if defined(DEVICE_TDECK)
        free(frame565);
#endif
        free(scanline);
        return false;
    }

    bool resumeRx = Radio.isReady();
    if (resumeRx) Radio.setRxPaused(true);

    bool ok = true;
    do {
        static const uint8_t kPngSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (!fileWriteAll(f, kPngSig, sizeof(kPngSig))) { ok = false; break; }

        uint8_t ihdr[13];
        ihdr[0] = (uint8_t)(((uint32_t)w >> 24) & 0xFF);
        ihdr[1] = (uint8_t)(((uint32_t)w >> 16) & 0xFF);
        ihdr[2] = (uint8_t)(((uint32_t)w >> 8) & 0xFF);
        ihdr[3] = (uint8_t)((uint32_t)w & 0xFF);
        ihdr[4] = (uint8_t)(((uint32_t)h >> 24) & 0xFF);
        ihdr[5] = (uint8_t)(((uint32_t)h >> 16) & 0xFF);
        ihdr[6] = (uint8_t)(((uint32_t)h >> 8) & 0xFF);
        ihdr[7] = (uint8_t)((uint32_t)h & 0xFF);
        ihdr[8] = 8;   // bit depth
        ihdr[9] = 2;   // truecolor RGB
        ihdr[10] = 0;  // compression method
        ihdr[11] = 0;  // filter method
        ihdr[12] = 0;  // no interlace
        if (!pngWriteChunk(f, "IHDR", ihdr, sizeof(ihdr))) { ok = false; break; }

        // zlib stream with deflate "stored" blocks, one block per scanline.
        uint64_t idatLen64 = 2ULL + (uint64_t)h * (5ULL + (uint64_t)scanlineLen) + 4ULL;
        if (idatLen64 > 0xFFFFFFFFULL) { ok = false; break; }
        uint32_t idatLen = (uint32_t)idatLen64;

        if (!fileWriteBe32(f, idatLen)) { ok = false; break; }
        if (!fileWriteAll(f, (const uint8_t *)"IDAT", 4)) { ok = false; break; }

        uint32_t idatCrc = 0xFFFFFFFFUL;
        idatCrc = pngCrc32Update(idatCrc, (const uint8_t *)"IDAT", 4);

        const uint8_t zlibHdr[2] = {0x78, 0x01};
        if (!fileWriteAll(f, zlibHdr, sizeof(zlibHdr))) { ok = false; break; }
        idatCrc = pngCrc32Update(idatCrc, zlibHdr, sizeof(zlibHdr));

        uint32_t adler = 1;
        for (int32_t y = 0; y < h; y++) {
            scanline[0] = 0; // filter: None

#if defined(DEVICE_TDECK)
            const uint16_t *srcRow = frame565 + ((size_t)y * rowPixels);
            size_t p = 1;
            for (int32_t x = 0; x < w; x++) {
                uint16_t c = srcRow[x];
#if LV_COLOR_16_SWAP
                c = (uint16_t)((c << 8) | (c >> 8));
#endif

                uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
                uint8_t g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
                uint8_t b = (uint8_t)((c & 0x1F) * 255 / 31);
                scanline[p++] = r;
                scanline[p++] = g;
                scanline[p++] = b;
            }
#else
            displayDev().waitDMA();
            displayDev().startWrite();
            displayDev().readRectRGB(0, y, w, 1, scanline + 1);
            displayDev().endWrite();
#endif

            adler = pngAdler32Update(adler, scanline, scanlineLen);

            uint16_t blockLen = (uint16_t)scanlineLen;
            uint16_t nlen = (uint16_t)~blockLen;
            uint8_t blockHdr[5] = {
                (uint8_t)((y == (h - 1)) ? 1 : 0),
                (uint8_t)(blockLen & 0xFF),
                (uint8_t)((blockLen >> 8) & 0xFF),
                (uint8_t)(nlen & 0xFF),
                (uint8_t)((nlen >> 8) & 0xFF),
            };

            if (!fileWriteAll(f, blockHdr, sizeof(blockHdr))) { ok = false; break; }
            idatCrc = pngCrc32Update(idatCrc, blockHdr, sizeof(blockHdr));

            if (!fileWriteAll(f, scanline, scanlineLen)) { ok = false; break; }
            idatCrc = pngCrc32Update(idatCrc, scanline, scanlineLen);
        }
        if (!ok) break;

        uint8_t adlerBe[4] = {
            (uint8_t)((adler >> 24) & 0xFF),
            (uint8_t)((adler >> 16) & 0xFF),
            (uint8_t)((adler >> 8) & 0xFF),
            (uint8_t)(adler & 0xFF),
        };
        if (!fileWriteAll(f, adlerBe, sizeof(adlerBe))) { ok = false; break; }
        idatCrc = pngCrc32Update(idatCrc, adlerBe, sizeof(adlerBe));

        idatCrc ^= 0xFFFFFFFFUL;
        if (!fileWriteBe32(f, idatCrc)) { ok = false; break; }

        if (!pngWriteChunk(f, "IEND", nullptr, 0)) { ok = false; break; }
    } while (false);

    if (resumeRx) Radio.setRxPaused(false);

    f.close();
    if (!ok && SD.exists(outPath)) {
        SD.remove(outPath);
    }

#if defined(DEVICE_TDECK)
    free(frame565);
#endif
    free(scanline);
    return ok;
#endif
}

static void startWebConfigAuto() {
    if (!s_webCfgEnabled) {
        Serial.println("[web] auto start disabled");
        return;
    }
    if (webCfgRunning()) return;
#if HAS_SD_CARD
    bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, captureWebScreenshotPng);
#else
    bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
#endif
    if (!ok) {
        Serial.println("[web] auto start failed");
        s_webCfgEnabled = false;
        persistWebCfgEnabled();
        return;
    }

    if (webCfgIsOnboarding()) {
        Serial.printf("[web] setup AP: %s\n", webCfgIP());
    } else {
        Serial.printf("[web] web config: %s\n", webCfgIP());
    }
}

static bool shouldHideChatLine(const char *text) {
    if (!text) return false;
    return strcmp(text, "[radio] listening for mesh traffic") == 0
        || strcmp(text, "[radio] listening to mesh traffic") == 0;
}

static bool isDuplicate(uint32_t from, uint32_t id) {
    for (int i = 0; i < kRxDedupSize; i++) {
        if (s_seenPkts[i].from == from && s_seenPkts[i].id == id) return true;
    }
    s_seenPkts[s_seenHead] = {from, id};
    s_seenHead = (s_seenHead + 1) % kRxDedupSize;
    return false;
}

static void applyTimezoneFromConfig() {
    const char *tz = (s_cfg.tzDef[0]) ? s_cfg.tzDef : "UTC0";
    setenv("TZ", tz, 1);
    tzset();
}

static bool wifiHasInternetTimePath() {
    if (WiFi.status() != WL_CONNECTED) return false;
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_AP;
}

static const char *configuredNtpServer() {
    return s_cfg.ntpServer[0] ? s_cfg.ntpServer : MY_NTP_SERVER;
}

static void ensureNtpConfigured() {
    if (!wifiHasInternetTimePath()) return;

    const char *srv = configuredNtpServer();
    if (s_ntpConfigured && strcmp(s_ntpServerActive, srv) == 0
        && (millis() - s_ntpLastConfigureMs) < 21600000UL) {
        return;
    }

    configTime(0, 0, srv);
    // configTime can leave TZ handling in UTC; re-apply configured timezone.
    applyTimezoneFromConfig();
    strncpy(s_ntpServerActive, srv, sizeof(s_ntpServerActive) - 1);
    s_ntpServerActive[sizeof(s_ntpServerActive) - 1] = '\0';
    s_ntpConfigured = true;
    s_ntpLastConfigureMs = millis();
    Serial.printf("[time] NTP configured: %s\n", s_ntpServerActive);
}

static bool ntpSyncSystemClock() {
    if (!wifiHasInternetTimePath()) return false;
    ensureNtpConfigured();
    return time(nullptr) >= 1700000000;
}

static bool waitForNtpSync(uint32_t timeoutMs, bool pumpWebCfg) {
    uint32_t syncStartMs = millis();
    while ((millis() - syncStartMs) < timeoutMs) {
        if (pumpWebCfg) webCfgLoop();
        if (ntpSyncSystemClock()) return true;
        delay(200);
    }
    return false;
}

static bool bootTimeNtpSyncDirectSta(const char *ssidOverride, const char *passOverride) {
    const char *ssid = (ssidOverride && ssidOverride[0]) ? ssidOverride : s_cfg.wifiSsid;
    const char *pass = (ssidOverride && ssidOverride[0]) ? passOverride : s_cfg.wifiPass;
    if (!ssid || !ssid[0]) return false;

    wifi_mode_t prevMode = WiFi.getMode();
    bool wasConnected = (WiFi.status() == WL_CONNECTED);

    if (!wasConnected) {
        if (prevMode == WIFI_AP) {
#ifdef WIFI_AP_STA
            WiFi.mode(WIFI_AP_STA);
#else
            WiFi.mode(WIFI_STA);
#endif
        } else {
            WiFi.mode(WIFI_STA);
        }

        WiFi.begin(ssid, pass);
        uint32_t connectStartMs = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - connectStartMs) < 10000UL) {
            delay(100);
        }
    }

    bool synced = waitForNtpSync(10000UL, false);

    if (!wasConnected) {
        switch (prevMode) {
            case WIFI_OFF:
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                break;
            case WIFI_AP:
                WiFi.disconnect(false);
                WiFi.mode(WIFI_AP);
                break;
            case WIFI_STA:
                WiFi.mode(WIFI_STA);
                WiFi.disconnect(false);
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
    }

    return synced;
}

static void bootTimeNtpSync() {
    bool bootTimeSynced = false;
    char cfgWifiSsid[sizeof(s_cfg.wifiSsid)] = {};
    char cfgWifiPass[sizeof(s_cfg.wifiPass)] = {};
    strncpy(cfgWifiSsid, s_cfg.wifiSsid, sizeof(cfgWifiSsid) - 1);
    strncpy(cfgWifiPass, s_cfg.wifiPass, sizeof(cfgWifiPass) - 1);

    // Keep Web Config disabled unless explicitly enabled. Boot-time time sync
    // uses a temporary STA connection and restores prior Wi-Fi state.
    bootTimeSynced = bootTimeNtpSyncDirectSta(cfgWifiSsid, cfgWifiPass);

    if (bootTimeSynced) {
        Serial.println("[time] Boot time sync OK");
    } else {
        Serial.println("[time] Boot time sync skipped/failed");
    }

    // Keep localtime conversion pinned to configured timezone after network time calls.
    applyTimezoneFromConfig();
}

static void lvglFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    displayDev().pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
#if defined(DEVICE_TDECK)
    if (s_screenshotCaptureActive && s_screenshotCaptureFrame && s_screenshotCaptureW > 0 && s_screenshotCaptureH > 0) {
        int32_t capX1 = area->x1 < 0 ? 0 : area->x1;
        int32_t capY1 = area->y1 < 0 ? 0 : area->y1;
        int32_t capX2 = area->x2 >= s_screenshotCaptureW ? (s_screenshotCaptureW - 1) : area->x2;
        int32_t capY2 = area->y2 >= s_screenshotCaptureH ? (s_screenshotCaptureH - 1) : area->y2;

        if (capX1 <= capX2 && capY1 <= capY2) {
            for (int32_t y = capY1; y <= capY2; y++) {
                int32_t srcY = y - area->y1;
                int32_t srcX = capX1 - area->x1;
                lv_color_t *src = color_p + (srcY * w) + srcX;
                uint16_t *dst = s_screenshotCaptureFrame + ((size_t)y * (size_t)s_screenshotCaptureW) + capX1;
                for (int32_t x = capX1; x <= capX2; x++) {
                    *dst++ = (uint16_t)(src++)->full;
                }
            }
            s_screenshotCaptureTouched = true;
        }
    }
#endif
    lv_disp_flush_ready(disp);
}

static void lvglTouchRead(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
#if TOUCH_POLL_ENABLED
    int32_t tx = 0;
    int32_t ty = 0;
    if (displayDev().getTouch(&tx, &ty)) {
        if (s_screenAsleep) {
            (void)tryWakeScreenFromInput(millis());
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        s_lastActivityMs = millis();
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
#else
    data->state = LV_INDEV_STATE_RELEASED;
#endif
}

static void drawBootSplash() {
    const int screenW = displayDev().width();
    const int screenH = displayDev().height();

    auto lerp565 = [](uint16_t c1, uint16_t c2, uint8_t t) -> uint16_t {
        int r1 = (c1 >> 11) & 0x1F;
        int g1 = (c1 >> 5) & 0x3F;
        int b1 = c1 & 0x1F;
        int r2 = (c2 >> 11) & 0x1F;
        int g2 = (c2 >> 5) & 0x3F;
        int b2 = c2 & 0x1F;
        int r = r1 + ((r2 - r1) * t) / 255;
        int g = g1 + ((g2 - g1) * t) / 255;
        int b = b1 + ((b2 - b1) * t) / 255;
        return (uint16_t)((r << 11) | (g << 5) | b);
    };

    // Splash is intentionally fixed to Camillia Dark, independent of UI theme selection.
    const UiThemePresetLite &splashPreset = kUiThemePresets[0]; // Camillia Dark
    const uint16_t splashBgMain = splashPreset.bgMain;
    const uint16_t splashPanelBg = splashPreset.panelBg;
    const uint16_t splashPanelAlt = splashPreset.panelAlt;
    const uint16_t splashAccent = splashPreset.accent;
    const uint16_t splashTextMain = rgb565(0xF3, 0xF6, 0xFA);
    const uint16_t splashTextDim = rgb565(0xB7, 0xC0, 0xCC);

    const uint16_t bgTop = blend565(splashBgMain, splashPanelBg, 96);
    const uint16_t bgBottom = splashBgMain;
    const uint16_t cardBg = splashPanelBg;
    const uint16_t cardEdge = blend565(splashPanelBg, splashAccent, 66);
    const uint16_t cardEdgeHi = blend565(splashPanelAlt, splashAccent, 92);
    const uint16_t titleCol = splashTextMain;
    const uint16_t subCol = splashTextDim;
    const uint16_t dimCol = blend565(splashTextDim, splashPanelBg, 72);

    for (int y = 0; y < screenH; y++) {
        uint8_t t = (uint8_t)((255UL * y) / max(1, screenH - 1));
        displayDev().drawFastHLine(0, y, screenW, lerp565(bgTop, bgBottom, t));
    }

    const int cardMargin = 10;
    const int cardX = cardMargin;
    const int cardY = 10;
    const int cardW = screenW - cardMargin * 2;
    const int cardH = screenH - 20;

    displayDev().fillRoundRect(cardX, cardY, cardW, cardH, 12, cardBg);
    displayDev().drawRoundRect(cardX, cardY, cardW, cardH, 12, cardEdge);
    displayDev().drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 12, cardEdgeHi);

    const char *version = APP_VERSION;

    char nodeLine[72];
    const char *nodeLong = s_cfg.nodeLong[0] ? s_cfg.nodeLong : "unknown node";
    const char *nodeShort = s_cfg.nodeShort[0] ? s_cfg.nodeShort : "----";
    snprintf(nodeLine, sizeof(nodeLine), "%s (%s)", nodeLong, nodeShort);

#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_CARDPUTER_LORA_HAT)
    // Native-size Roboto GFX fonts (crisp at this size, no bitmap upscaling).
    int splashContentBottom = cardY + 40;
    displayDev().setTextColor(titleCol, cardBg);

    auto drawCentered = [&](const char *text, int y) {
        int w = displayDev().textWidth(text);
        displayDev().drawString(text, cardX + max(0, (cardW - w) / 2), y);
    };

    // Large brand name on top (Roboto Bold 26pt).
    displayDev().setFont(&Roboto_Bold26pt7b);
    displayDev().setTextSize(MY_SPLASH_TITLE_SCALE);
    const int brandY = cardY + 12 + MY_SPLASH_TITLE_Y_OFFSET;
    drawCentered("Camillia", brandY);
    const int brandCap = max(8, displayDev().fontHeight() - MY_SPLASH_SUBTITLE_GAP_TRIM);

    // "for Meshtastic" underneath (Roboto Medium 14pt).
    displayDev().setFont(&Roboto_Medium14pt7b);
    displayDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
    const int subY = brandY + brandCap;
    drawCentered("for Meshtastic", subY);
    splashContentBottom = subY + displayDev().fontHeight();
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    const float flowerScale = 1.15f;
#else
    const float flowerScale = 1.0f;
#endif

    auto drawCamelliaMark = [&](int cx, int cy, float scale) {
        const uint16_t PETAL_OUTER  = 0xF9CF;
        const uint16_t PETAL_MID    = 0xFADF;
        const uint16_t PETAL_INNER  = 0xFF7D;
        const uint16_t PETAL_EDGE   = 0xD8A7;
        const uint16_t CENTER       = 0xFD20;
        const uint16_t CENTER_DOT   = 0xFEA0;
        const uint16_t STEM         = 0x64EC;
        const uint16_t LEAF_DARK    = 0x2C87;
        const uint16_t LEAF_LIGHT   = 0x3D68;

        auto scaled = [&](float value, int minValue = 0) -> int {
            int result = (int)lroundf(value * scale);
            return (result < minValue) ? minValue : result;
        };

        const int petalOuterOrbitX = scaled(23.0f, 1);
        const int petalOuterOrbitY = scaled(18.0f, 1);
        const int petalMidOrbitX = scaled(13.0f, 1);
        const int petalMidOrbitY = scaled(10.0f, 1);
        const int petalInnerOrbitX = scaled(6.0f, 1);
        const int petalInnerOrbitY = scaled(5.0f, 1);
        const int petalOuterR0 = scaled(11.0f, 1);
        const int petalOuterR1 = scaled(12.0f, 1);
        const int petalMidR = scaled(9.0f, 1);
        const int petalInnerR = scaled(6.0f, 1);
        const int centerR = scaled(6.0f, 1);
        const int centerDotOrbit = scaled(4.0f, 1);
        const int centerDotR = scaled(1.0f, 1);
        const int stemX = scaled(1.0f);
        const int stemY = scaled(20.0f);
        const int stemW = scaled(3.0f, 1);
        const int stemH = scaled(17.0f, 1);
        const int stemRadius = scaled(1.0f, 1);
        const int leafOuterX = scaled(21.0f, 1);
        const int leafOuterYLeft = scaled(28.0f, 1);
        const int leafOuterYRight = scaled(29.0f, 1);
        const int leafOuterR = scaled(8.0f, 1);
        const int leafInnerX = scaled(14.0f, 1);
        const int leafInnerYLeft = scaled(30.0f, 1);
        const int leafInnerYRight = scaled(31.0f, 1);
        const int leafInnerR = scaled(6.0f, 1);

        for (int i = 0; i < 10; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
            int px = cx + (int)lroundf((float)petalOuterOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalOuterOrbitY * sinf(a));
            int pr = (i & 1) ? petalOuterR1 : petalOuterR0;
            displayDev().fillCircle(px, py, pr, PETAL_OUTER);
            displayDev().drawCircle(px, py, pr, PETAL_EDGE);
        }

        for (int i = 0; i < 8; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
            int px = cx + (int)lroundf((float)petalMidOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalMidOrbitY * sinf(a));
            displayDev().fillCircle(px, py, petalMidR, PETAL_MID);
            displayDev().drawCircle(px, py, petalMidR, PETAL_EDGE);
        }

        for (int i = 0; i < 5; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 5.0f) + 0.20f;
            int px = cx + (int)lroundf((float)petalInnerOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalInnerOrbitY * sinf(a));
            displayDev().fillCircle(px, py, petalInnerR, PETAL_INNER);
        }

        displayDev().fillCircle(cx, cy, centerR, CENTER);
        displayDev().drawCircle(cx, cy, centerR, 0xD4C0);
        for (int i = 0; i < 10; i++) {
            float a = (float)i * 2.0f * (float)M_PI / 10.0f;
            int sx = cx + (int)lroundf((float)centerDotOrbit * cosf(a));
            int sy = cy + (int)lroundf((float)centerDotOrbit * sinf(a));
            displayDev().fillCircle(sx, sy, centerDotR, CENTER_DOT);
        }

        displayDev().fillRoundRect(cx - stemX, cy + stemY, stemW, stemH, stemRadius, STEM);
        displayDev().fillCircle(cx - leafOuterX, cy + leafOuterYLeft, leafOuterR, LEAF_DARK);
        displayDev().fillCircle(cx - leafInnerX, cy + leafInnerYLeft, leafInnerR, LEAF_LIGHT);
        displayDev().fillCircle(cx + leafOuterX, cy + leafOuterYRight, leafOuterR, LEAF_DARK);
        displayDev().fillCircle(cx + leafInnerX, cy + leafInnerYRight, leafInnerR, LEAF_LIGHT);
    };

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer splash: centered flower with version directly underneath.
    const int flowerCx = cardX + (cardW / 2);
    const int flowerCy = cardY + (cardH / 2) - 10;
    const float cardputerFlowerScale = 0.90f;
    drawCamelliaMark(flowerCx, flowerCy, cardputerFlowerScale);

    char verLine[72];
    snprintf(verLine, sizeof(verLine), "Version: %s", version);
    displayDev().setFont(&fonts::DejaVu12);
    displayDev().setTextSize(1.0f);
    displayDev().setTextColor(dimCol, cardBg);
    // Center version text between the flower and bottom of the display.
    const int flowerBottom = flowerCy + (int)lroundf(36.0f * cardputerFlowerScale);
    const int versionBandCenterY = (flowerBottom + screenH) / 2;
    const int versionTopY = versionBandCenterY - (displayDev().fontHeight() / 2);
    int verW = displayDev().textWidth(verLine);
    displayDev().drawString(verLine,
                            cardX + max(0, (cardW - verW) / 2),
                            versionTopY);
#elif defined(DEVICE_TLORA_PAGER_TFT)
    // Pager has much wider horizontal space; use a split layout similar to v1.
    const int contentInset = 12;
    const int paneGap = 10;
    const int contentW = cardW - (contentInset * 2) - paneGap;
    const int leftX = cardX + contentInset;
    const int leftW = max(100, (contentW * 46) / 100);
    const int rightX = leftX + leftW + paneGap;
    const int rightW = max(100, cardX + cardW - contentInset - rightX);

    auto trimTailToFit = [&](const char *src, int maxWidth) -> String {
        String out = src ? String(src) : String();
        if (maxWidth <= 0) return String();
        while (out.length() > 3 && displayDev().textWidth(out.c_str()) > maxWidth) {
            out.remove(out.length() - 1);
        }
        if (displayDev().textWidth(out.c_str()) > maxWidth && out.length() > 3) {
            out.remove(out.length() - 3);
            out += "...";
        }
        return out;
    };

    auto trimHeadToFit = [&](const char *src, int maxWidth) -> String {
        String out = src ? String(src) : String();
        if (maxWidth <= 0) return String();
        while (out.length() > 3 && displayDev().textWidth(out.c_str()) > maxWidth) {
            out.remove(0, 1);
        }
        if (displayDev().textWidth(out.c_str()) > maxWidth && out.length() > 3) {
            out = String("...") + out.substring(3);
        }
        return out;
    };

    // Match pager splash typography to the Roboto title/subtitle treatment,
    // but keep both phrases on a single centered line.
    const int titleY = cardY + 14;
    const int titleGap = 8;

    displayDev().setFont(&Roboto_Bold26pt7b);
    displayDev().setTextSize(MY_SPLASH_TITLE_SCALE);
    const int camilliaW = displayDev().textWidth("Camillia");
    const int camilliaH = displayDev().fontHeight();

    displayDev().setFont(&Roboto_Medium14pt7b);
    displayDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
    const int meshW = displayDev().textWidth("for Meshtastic");
    const int meshH = displayDev().fontHeight();

    const int lineW = camilliaW + titleGap + meshW;
    const int lineX = leftX + max(0, (leftW - lineW) / 2);
    const int meshY = titleY + 3;

    displayDev().setFont(&Roboto_Bold26pt7b);
    displayDev().setTextSize(MY_SPLASH_TITLE_SCALE);
    displayDev().setTextColor(titleCol, cardBg);
    displayDev().drawString("Camillia", lineX, titleY);

    displayDev().setFont(&Roboto_Medium14pt7b);
    displayDev().setTextSize(MY_SPLASH_SUBTITLE_SCALE);
    displayDev().setTextColor(subCol, cardBg);
    displayDev().drawString("for Meshtastic", lineX + camilliaW + titleGap, meshY);

    const int titleBlockBottom = max(titleY + camilliaH, meshY + meshH);
    const int flowerAreaTop = titleBlockBottom + 8;
    const int flowerAreaBottom = cardY + cardH - 14;
    float pagerFlowerScale = min((float)(flowerAreaBottom - flowerAreaTop) / 76.0f,
                                 (float)(leftW - 16) / 70.0f);
    if (pagerFlowerScale < 1.10f) pagerFlowerScale = 1.10f;
    if (pagerFlowerScale > 1.70f) pagerFlowerScale = 1.70f;
    drawCamelliaMark(leftX + (leftW / 2),
                     (flowerAreaTop + flowerAreaBottom) / 2,
                     pagerFlowerScale);

    const int rightInfoTop = titleBlockBottom + 6;
    const int dividerTop = rightInfoTop;
    const int dividerBottom = cardY + cardH - 16;
    const int dividerH = max(8, dividerBottom - dividerTop);
    displayDev().drawFastVLine(rightX - (paneGap / 2), dividerTop, dividerH, cardEdgeHi);

    displayDev().setFont(&fonts::DejaVu12);
    displayDev().setTextSize(1.0f);
    displayDev().setTextColor(subCol, cardBg);
    String longLine = trimTailToFit(nodeLong, rightW);
    String shortLine = trimTailToFit((String("(") + nodeShort + ")").c_str(), rightW);
    const int nodeLineY = dividerTop;
    const int nodeLineStep = displayDev().fontHeight() + 4;
    displayDev().drawString(longLine.c_str(), rightX, nodeLineY);
    displayDev().drawString(shortLine.c_str(), rightX, nodeLineY + nodeLineStep);

    char verLine[72];
    snprintf(verLine, sizeof(verLine), "Version: %s", version);
    String verText = trimHeadToFit(verLine, rightW);
    displayDev().setTextColor(dimCol, cardBg);
    const int versionY = dividerBottom - displayDev().fontHeight();
    displayDev().drawString(verText.c_str(), rightX, versionY);
#else
    // Center the flower in the space between the title block and the footer text.
    const int flowerBandTop = splashContentBottom;
    const int flowerBandBottom = cardY + cardH - 40;
    drawCamelliaMark(cardX + (cardW / 2),
                     (flowerBandTop + flowerBandBottom) / 2,
                     flowerScale);

    displayDev().setFont(&fonts::DejaVu12);
    displayDev().setTextSize(1.0f);
    displayDev().setTextColor(subCol, cardBg);
    int nodeW = displayDev().textWidth(nodeLine);
    displayDev().drawString(nodeLine, cardX + max(0, (cardW - nodeW) / 2), cardY + cardH - 36);

    char verLine[72];
    snprintf(verLine, sizeof(verLine), "Version: %s", version);
    displayDev().setTextColor(dimCol, cardBg);
    int verW = displayDev().textWidth(verLine);
    displayDev().drawString(verLine, cardX + max(0, (cardW - verW) / 2), cardY + cardH - 20);
#endif

    delay(1200);
    displayDev().fillScreen(TFT_BLACK);
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
    #if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
        bool dropdownCursor = (isChannelDropdownVisible()
                       && s_cardputerDropdownSelection == i);
    #else
        bool dropdownCursor = false;
    #endif
        anyUnread = anyUnread || animate;
        lv_obj_t *lbl = s_channelLabels[i];
        if (lbl) {
            char text[48];
            const char *name = channelName(i);
            if (!name || !name[0]) name = "Channel";
            if (s_channelNeedsAttention[i] && !active) {
                snprintf(text, sizeof(text), "%s *", name);
            } else {
                snprintf(text, sizeof(text), "%s", name);
            }
            lv_label_set_text(lbl, text);
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
            sizeChannelButtonToLabel(i);
#endif
            lv_obj_set_style_text_color(
                lbl,
                active
                    ? (s_cfg.uiMode == UI_MODE_DARK ? lv_color_hex(0x0B1E44) : lv_color_hex(0xD9E8FF))
                    : lv_color_hex(0xD9E8FF),
                0);
        }

        if (animate && !dropdownCursor) {
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
    }

    if (s_channelSelectorBtn) {
        bool selectorShouldGlow = anyUnread;
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
        selectorShouldGlow = anyUnread && !isChannelDropdownVisible();
#endif
    #if defined(DEVICE_CARDPUTER_LORA_HAT)
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
        if (selectorNavCursor) {
            lv_obj_set_style_border_width(s_channelSelectorBtn, 2, 0);
            lv_obj_set_style_border_color(s_channelSelectorBtn, lv_color_hex(0xF4D35E), 0);
            lv_obj_set_style_outline_color(s_channelSelectorBtn, lv_color_hex(0xF4D35E), 0);
            lv_obj_set_style_outline_pad(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_70, 0);
            lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
        } else if (selectorShouldGlow) {
    #else
        if (selectorShouldGlow) {
    #endif
#if defined(DEVICE_HELTEC_V4_EXPANSION) && defined(DEVICE_UI_VERTICAL)
            lv_obj_set_style_border_width(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_border_color(s_channelSelectorBtn, lv_color_hex(0x8EEBFF), 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
#else
            lv_obj_set_style_border_width(s_channelSelectorBtn, 2, 0);
            lv_obj_set_style_border_color(s_channelSelectorBtn, lv_color_hex(0x8EEBFF), 0);
            lv_obj_set_style_outline_color(s_channelSelectorBtn, lv_color_hex(0x8EEBFF), 0);
            lv_obj_set_style_outline_pad(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, outlineW, 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, pulseOpa, 0);
            lv_obj_set_style_shadow_color(s_channelSelectorBtn, lv_color_hex(0x4EC9FF), 0);
            lv_obj_set_style_shadow_spread(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_shadow_width(s_channelSelectorBtn, shadowW, 0);
            lv_obj_set_style_shadow_opa(s_channelSelectorBtn, pulseOpa, 0);
#endif
        } else {
            const bool lightMode = (s_cfg.uiMode == UI_MODE_LIGHT);
            const lv_color_t selectorIdleBorder =
                lvColorFrom565(blend565(s_ui.panelAlt, s_ui.accent, lightMode ? 96 : 120));
            const lv_color_t selectorIdleOutline =
                lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, lightMode ? 88 : 112));
            lv_obj_set_style_border_width(s_channelSelectorBtn, 2, 0);
            lv_obj_set_style_border_color(s_channelSelectorBtn, selectorIdleBorder, 0);
            lv_obj_set_style_outline_color(s_channelSelectorBtn, selectorIdleOutline, 0);
            lv_obj_set_style_outline_pad(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, lightMode ? LV_OPA_40 : LV_OPA_50, 0);
            lv_obj_set_style_shadow_color(s_channelSelectorBtn, lv_color_hex(0x4EC9FF), 0);
            lv_obj_set_style_shadow_spread(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_shadow_width(s_channelSelectorBtn, 4, 0);
            lv_obj_set_style_shadow_opa(s_channelSelectorBtn, lightMode ? LV_OPA_20 : LV_OPA_30, 0);
        }
    }
}

static void applyChannelButtonTheme() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = s_channelBtns[i];
        if (!btn) continue;

        bool active = (i == s_activeChannel);
        lv_obj_set_style_bg_color(
            btn,
            (s_cfg.uiMode == UI_MODE_LIGHT)
                ? chatPanelBackgroundColor()
                : (active ? lv_color_hex(0x2A4FB4) : lv_color_hex(0x102750)),
            0);
        lv_obj_set_style_bg_opa(btn, active ? LV_OPA_90 : LV_OPA_60, 0);
        lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn, active ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C), 0);

        lv_obj_t *lbl = s_channelLabels[i];
        if (lbl) {
            lv_obj_set_style_text_color(
                lbl,
                active
                    ? (s_cfg.uiMode == UI_MODE_DARK ? lv_color_hex(0x0B1E44) : lv_color_hex(0xD9E8FF))
                    : lv_color_hex(0xD9E8FF),
                0);
        }
    }

    if (s_channelSelectorBtn) {
        const bool lightMode = (s_cfg.uiMode == UI_MODE_LIGHT);
        const lv_color_t selectorBaseBorder =
            lvColorFrom565(blend565(s_ui.panelAlt, s_ui.accent, lightMode ? 96 : 120));
        const lv_color_t selectorBaseOutline =
            lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, lightMode ? 88 : 112));
        lv_obj_set_style_bg_color(
            s_channelSelectorBtn,
            lightMode
                ? lvColorFrom565(blend565(s_ui.panelBg, s_ui.accent, 78))
                : lv_color_hex(0x15356B),
            0);
        lv_obj_set_style_bg_opa(s_channelSelectorBtn, lightMode ? LV_OPA_90 : LV_OPA_80, 0);
        lv_obj_set_style_border_width(s_channelSelectorBtn, 2, 0);
        lv_obj_set_style_border_color(s_channelSelectorBtn, selectorBaseBorder, 0);
        lv_obj_set_style_outline_color(s_channelSelectorBtn, selectorBaseOutline, 0);
        lv_obj_set_style_outline_pad(s_channelSelectorBtn, 0, 0);
        lv_obj_set_style_outline_width(s_channelSelectorBtn, 1, 0);
        lv_obj_set_style_outline_opa(s_channelSelectorBtn, lightMode ? LV_OPA_40 : LV_OPA_50, 0);
        lv_obj_set_style_shadow_color(s_channelSelectorBtn, lv_color_hex(0x4EC9FF), 0);
        lv_obj_set_style_shadow_spread(s_channelSelectorBtn, 1, 0);
        lv_obj_set_style_shadow_width(s_channelSelectorBtn, 4, 0);
        lv_obj_set_style_shadow_opa(s_channelSelectorBtn, lightMode ? LV_OPA_20 : LV_OPA_30, 0);
    }
    if (s_channelSelectorLabel) {
        lv_obj_set_style_text_color(
            s_channelSelectorLabel,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
            0);
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    s_cardputerMainChatPanelFocused = false;
#endif
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    s_cardputerDropdownSelection = channelIdx;
#endif
    s_selectedMsgReplyPacketId = 0;
    s_selectedMsgText[0] = '\0';
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
#if defined(DEVICE_HELTEC_V4_EXPANSION) && defined(HELTEC_COMPACT_SELECTOR)
    return true;
#else
    return false;
#endif
}

static bool isChannelDropdownVisible() {
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    return s_channelList && !lv_obj_has_flag(s_channelList, LV_OBJ_FLAG_HIDDEN);
#else
    return false;
#endif
}

static void refreshChannelSelectorLabel() {
#if !defined(DEVICE_TDECK) && !defined(DEVICE_HELTEC_V4_EXPANSION) && !defined(DEVICE_CARDPUTER_LORA_HAT)
    return;
#endif
    if (!s_channelSelectorLabel) return;

    const char *name = channelName(s_activeChannel);
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
    if (isChannelDropdownVisible()
        && s_cardputerDropdownSelection >= 0
        && s_cardputerDropdownSelection < MESH_CHANNELS) {
        name = channelName(s_cardputerDropdownSelection);
    }
#endif
    if (!name || !name[0]) name = "Channel";
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (useCompactVerticalHeltecSelector()) {
        name = "";
    }
#endif

#if defined(DEVICE_TDECK)
    const bool showSelectorCaret = false;
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    const bool showSelectorCaret = useCompactVerticalHeltecSelector();
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    const bool showSelectorCaret = false;
#else
    const bool showSelectorCaret = true;
#endif

    lv_label_set_text(s_channelSelectorLabel, name);
    lv_obj_set_style_text_color(
        s_channelSelectorLabel,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
        0);
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

#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    bool allowDynamicSelectorWidth = true;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
                if (!candidate || !candidate[0]) candidate = "Channel";

                lv_label_set_text(s_channelSelectorLabel, candidate);
                lv_label_set_long_mode(s_channelSelectorLabel, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(s_channelSelectorLabel, LV_SIZE_CONTENT);
                lv_obj_update_layout(s_channelSelectorLabel);

                lv_coord_t candidateW = lv_obj_get_width(s_channelSelectorLabel);
                if (candidateW > textW) textW = candidateW;
            }

            // Measure button width from text + equal edge padding (+ optional caret room).
            lv_coord_t desiredW = textW + (showSelectorCaret ? 26 : (selectorEdgePad * 2 + selectorAntiClip));
#if defined(DEVICE_TDECK)
            if (desiredW < 56) desiredW = 56;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
            if (desiredW < 60) desiredW = 60;
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

        if (showSelectorCaret) {
            lv_obj_set_style_text_align(s_channelSelectorLabel, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_width(s_channelSelectorLabel, s_channelSelectorFixedBtnW - 22);
            lv_obj_align(s_channelSelectorLabel, LV_ALIGN_LEFT_MID, 4, 1);
            if (s_channelSelectorCaretLabel) {
                lv_obj_align(s_channelSelectorCaretLabel, LV_ALIGN_RIGHT_MID, -5, 1);
            }
        } else {
            // Non-caret selector uses centered text in a centered text box for equal left/right padding.
            lv_obj_set_style_text_align(s_channelSelectorLabel, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(s_channelSelectorLabel, max((lv_coord_t)1, (lv_coord_t)(s_channelSelectorFixedBtnW - (selectorEdgePad * 2))));
            lv_obj_align(s_channelSelectorLabel, LV_ALIGN_CENTER, 0, 1);
        }

        layoutHeaderInlineItems();
    }
#endif
}

static void setChannelDropdownVisible(bool visible) {
#if !defined(DEVICE_TDECK) && !defined(DEVICE_HELTEC_V4_EXPANSION) && !defined(DEVICE_CARDPUTER_LORA_HAT)
    LV_UNUSED(visible);
    return;
#endif
    if (!s_channelList) return;

    if (visible) {
        lv_obj_clear_flag(s_channelList, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_channelList);
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
        if (s_cardputerDropdownSelection < 0 || s_cardputerDropdownSelection >= MESH_CHANNELS) {
            s_cardputerDropdownSelection = s_activeChannel;
        }
        if (s_cardputerDropdownSelection >= 0 && s_cardputerDropdownSelection < MESH_CHANNELS
            && s_channelBtns[s_cardputerDropdownSelection]) {
            lv_obj_scroll_to_view(s_channelBtns[s_cardputerDropdownSelection], LV_ANIM_OFF);
        }
#endif
    } else {
        lv_obj_add_flag(s_channelList, LV_OBJ_FLAG_HIDDEN);
#if defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
        s_cardputerDropdownSelection = -1;
#endif
    }

    refreshChannelSelectorLabel();
    refreshChannelGlow(true);
}

static void onChannelSelectorPressed(lv_event_t *e) {
#if !defined(DEVICE_TDECK) && !defined(DEVICE_HELTEC_V4_EXPANSION) && !defined(DEVICE_CARDPUTER_LORA_HAT)
    LV_UNUSED(e);
    return;
#endif
    LV_UNUSED(e);
    setChannelDropdownVisible(!isChannelDropdownVisible());
    refreshChannelGlow(true);
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
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
    if (s_channelList && !s_channelStrip) return;
    if (idx < 0 || idx >= MESH_CHANNELS) return;
    lv_obj_t *btn = s_channelBtns[idx];
    lv_obj_t *lbl = s_channelLabels[idx];
    if (!btn || !lbl) return;

    lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_update_layout(lbl);

    lv_coord_t targetW = lv_obj_get_width(lbl) + 14;
    #if defined(DEVICE_HELTEC_V4_EXPANSION)
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
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    // This fitter applies only to the floating channel dropdown list.
    if (!s_channelList || s_channelStrip) return;

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

    const lv_coord_t listPadLeft = 4;
    const lv_coord_t listPadRight = 4;
    const lv_coord_t buttonTextPad = 8; // 4px left + 4px right, matching button inner padding
    const lv_coord_t scrollbarGutter = 8; // keep scrollbar between panel edge and buttons

    lv_coord_t buttonW = maxLabelW + buttonTextPad;
    if (buttonW < 56) buttonW = 56;

    lv_coord_t dropdownW = listPadLeft + buttonW + scrollbarGutter + listPadRight;
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
        lv_obj_set_width(lbl, max((lv_coord_t)1, (lv_coord_t)(buttonW - 8)));
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_center(lbl);
    }
#endif
}

static void loadConfigFromSd() {
    cfgInitDefaults(s_cfg);
    myDeviceRole = s_cfg.deviceRole;
    s_webCfgEnabled = false;

    if (!sdBegin()) {
        Serial.println("[lvgl-poc] SD not available; loading state from NVS");
    } else {
        Serial.println("[lvgl-poc] boot config import disabled; loading state from NVS");
    }

    loadConfigFromPrefs();
#if !HAS_ENV_SENSOR_TELEMETRY
    s_cfg.telEnvEnabled = false;
#endif
    if (s_cfg.telDeviceIntervalS < 3600UL) s_cfg.telDeviceIntervalS = 3600UL;
    if (s_cfg.telEnvIntervalS < 3600UL) s_cfg.telEnvIntervalS = 3600UL;
    if (s_cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        s_cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }
    loadChannelsFromPrefs();
    applyUiThemePalette();
    myDeviceRole = s_cfg.deviceRole;
}

static void loadConfigForOtaWorker() {
    cfgInitDefaults(s_cfg);
    myDeviceRole = s_cfg.deviceRole;
    loadConfigFromPrefs();
    myDeviceRole = s_cfg.deviceRole;
}

static void refreshHeaderTime(bool force) {
    if (!s_chatHeaderTime) return;

    char buf[8];
    liveBuildPrefix(buf, sizeof(buf));
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == ' ') buf[n - 1] = '\0';

    if (!force && strcmp(buf, s_lastHeaderTime) == 0) return;
    lv_label_set_text(s_chatHeaderTime, buf);
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    layoutHeaderInlineItems();
#endif
    strncpy(s_lastHeaderTime, buf, sizeof(s_lastHeaderTime) - 1);
    s_lastHeaderTime[sizeof(s_lastHeaderTime) - 1] = '\0';
}

static void layoutHeaderInlineItems() {
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    if (!s_chatHeaderTime || !s_chatHeaderBattBar) return;
    if (!s_channelSelectorBtn || !s_chatHeaderBattText || !s_chatHeaderBar) return;

    const lv_coord_t headerTextYOffset = 1;

    // Keep battery icon at the far right, with percent text close to it.
    lv_obj_align(s_chatHeaderBattBar, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_align_to(s_chatHeaderBattText, s_chatHeaderBattBar, LV_ALIGN_OUT_LEFT_MID, -3, headerTextYOffset);

    // Keep optional status icons near the battery cluster so clock centering
    // depends only on selector (left) and battery info (right).
    if (s_chatHeaderGps && s_chatHeaderWifi
        && lv_obj_get_parent(s_chatHeaderGps) == s_chatHeaderBar
        && lv_obj_get_parent(s_chatHeaderWifi) == s_chatHeaderBar) {
        lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderBattText, LV_ALIGN_OUT_LEFT_MID, -6, 0);
        lv_obj_align_to(s_chatHeaderGps, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -7, 0);

        if (s_chatDmAlert && lv_obj_is_valid(s_chatDmAlert)
            && lv_obj_get_parent(s_chatDmAlert) == s_chatHeaderBar) {
            lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        }
    }

    // Keep time centered between selector button and battery info.
    lv_obj_update_layout(s_chatHeaderBar);
    lv_area_t selectorArea;
    lv_area_t battTextArea;
    lv_obj_get_coords(s_channelSelectorBtn, &selectorArea);
    lv_obj_get_coords(s_chatHeaderBattText, &battTextArea);
    lv_coord_t selectorRight = selectorArea.x2 + 1;
    lv_coord_t battLeft = battTextArea.x1;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_coord_t rightBoundLeft = battLeft;
    if (s_chatHeaderGps && lv_obj_get_parent(s_chatHeaderGps) == s_chatHeaderBar) {
        lv_area_t gpsArea;
        lv_obj_get_coords(s_chatHeaderGps, &gpsArea);
        rightBoundLeft = gpsArea.x1;
    }
#else
    lv_coord_t rightBoundLeft = battLeft;
#endif
    lv_coord_t timeW = lv_obj_get_width(s_chatHeaderTime);
    lv_coord_t slotStart = selectorRight + 4;
    lv_coord_t slotEnd = rightBoundLeft - 2;

    if (slotEnd <= slotStart || timeW <= 0) {
        lv_obj_align(s_chatHeaderTime, LV_ALIGN_CENTER, 0, headerTextYOffset);
    } else {
        lv_coord_t x = ((slotStart + slotEnd) - timeW) / 2;
        if (x < slotStart) x = slotStart;
        lv_coord_t maxX = slotEnd - timeW;
        if (x > maxX) x = maxX;
        lv_obj_align(s_chatHeaderTime, LV_ALIGN_LEFT_MID, x, headerTextYOffset);
    }
#endif
}

static inline lv_color_t headerGoodGreenColor() {
    return (s_cfg.uiMode == UI_MODE_LIGHT)
        ? lv_color_hex(0x2C7A3B)
        : lv_color_hex(0x84E07A);
}

static inline lv_color_t headerGpsBadColor() {
    uint16_t bad = s_ui.battBad;
    if (s_cfg.uiMode == UI_MODE_DARK) {
        bad = blend565(bad, 0xFFFF, 96);
    }
    return lvColorFrom565(bad);
}

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

static void refreshHeaderStatus(bool force) {
    if (!s_chatHeaderGps || !s_chatHeaderWifi || !s_chatHeaderBattText || !s_chatHeaderBattBar) return;

    static uint32_t lastPollMs = 0;
    uint32_t now = millis();
    if (!force && (uint32_t)(now - lastPollMs) < 800) return;
    lastPollMs = now;

    const uint8_t battPct = batteryReadPercent();
    const bool gpsEnabled = gpsIsEnabled();
    const bool gpsFix = gpsHasFix();
    const uint8_t gpsSatCount = gpsEnabled ? gpsSats() : 0;
    wifi_mode_t wifiMode = WiFi.getMode();
    bool wifiApMode = (wifiMode == WIFI_AP);
#ifdef WIFI_AP_STA
    wifiApMode = wifiApMode || (wifiMode == WIFI_AP_STA);
#endif
    bool wifiConnected = (!wifiApMode && WiFi.status() == WL_CONNECTED);

    if (!force && battPct == s_lastBattPct && gpsEnabled == s_lastGpsEnabled
        && gpsFix == s_lastGpsFix && gpsSatCount == s_lastGpsSats
        && wifiConnected == s_lastWifiConnected && wifiApMode == s_lastWifiApMode) {
        return;
    }

    lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerBatteryDotColor(battPct), 0);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_label_set_text_fmt(s_chatHeaderBattText, "%u", (unsigned)battPct);
#else
    lv_label_set_text_fmt(s_chatHeaderBattText, "%u%%", (unsigned)battPct);
#endif

    if (gpsEnabled) {
        lv_label_set_text_fmt(s_chatHeaderGps, "%s %u", LV_SYMBOL_GPS, (unsigned)gpsSatCount);
    } else {
        lv_label_set_text(s_chatHeaderGps, LV_SYMBOL_GPS);
    }

    if (gpsEnabled && gpsFix) {
        lv_obj_set_style_text_color(s_chatHeaderGps, headerGoodGreenColor(), 0);
    } else {
        lv_obj_set_style_text_color(s_chatHeaderGps, headerGpsBadColor(), 0);
    }

    bool wifiOffOrDisconnected = (!wifiApMode && !wifiConnected);
    lv_label_set_text(s_chatHeaderWifi, wifiApMode ? LV_SYMBOL_UPLOAD : LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(
        s_chatHeaderWifi,
        wifiApMode ? lv_color_hex(0xF4D35E)
                   : (wifiOffOrDisconnected ? lv_color_hex(0xFF6B6B) : headerGoodGreenColor()),
        0);
    lv_obj_set_style_text_decor(
        s_chatHeaderWifi,
        wifiOffOrDisconnected ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE,
        0);
    lv_obj_t *gpsParent = lv_obj_get_parent(s_chatHeaderGps);
    lv_obj_t *wifiParent = lv_obj_get_parent(s_chatHeaderWifi);
    if (gpsParent && gpsParent == wifiParent) {
        if (gpsParent == s_chatShortcutBar) {
            lv_obj_align(s_chatHeaderGps, LV_ALIGN_RIGHT_MID, -4, 0);
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
        if (wifiParent == s_chatShortcutBar) {
            lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
        } else {
            lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        }
    }
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    layoutHeaderInlineItems();
#endif

    s_lastBattPct = battPct;
    s_lastGpsEnabled = gpsEnabled;
    s_lastGpsFix = gpsFix;
    s_lastGpsSats = gpsSatCount;
    s_lastWifiConnected = wifiConnected;
    s_lastWifiApMode = wifiApMode;
}

// Envelope icon left of the wifi icon that blinks whenever any DM
// conversation has unread messages. Cleared as soon as the user opens the
// conversation (DMs.markRead runs).
static void refreshDmAlertIndicator() {
    if (!s_chatDmAlert || !lv_obj_is_valid(s_chatDmAlert)) return;

    const bool hasUnread = DMs.hasUnread();
    if (!hasUnread) {
        if (!lv_obj_has_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // 500 ms on / 500 ms off blink; derived from the free-running millis()
    // counter so we don't need to persist a phase across calls.
    const bool visible = ((millis() / 500UL) & 1UL) == 0UL;
    const bool currentlyHidden = lv_obj_has_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);
    if (visible && currentlyHidden) {
        lv_obj_clear_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);
    } else if (!visible && !currentlyHidden) {
        lv_obj_add_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);
    }
}

// True when the given real channel has been muted via Channel Actions. Muted
// channels still buffer incoming messages, but raise no visual or audio alert.
static inline bool channelIsMuted(int chanIdx) {
    return chanIdx >= 0 && chanIdx < MESH_CHANNELS && CHANNEL_KEYS[chanIdx].muted;
}

static void appendRxText(int chanIdx, uint32_t fromNode, const char *text, uint32_t packetId, bool viaMqtt) {
    char timePrefix[12];
    char sender[48];
    char prefix[80];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    LV_UNUSED(viaMqtt);
    chatSenderLabel(fromNode, sender, sizeof(sender));
    snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, sender);
#else
    chatSenderLabel(fromNode, sender, sizeof(sender));
    const char *transportIcon = viaMqtt ? LV_SYMBOL_GLOBE_TINY : LV_SYMBOL_RADIO_TINY;
    // Keep a small visual buffer between transport icon and timestamp.
    snprintf(prefix, sizeof(prefix), "%s  %s[%s] ", transportIcon, timePrefix, sender);
#endif

    Channels.addMessage(chanIdx, prefix, text, TFT_WHITE, packetId, false, fromNode);
    if (chanIdx >= 0 && chanIdx < MESH_CHANNELS && chanIdx != s_activeChannel
        && !channelIsMuted(chanIdx)) {
        s_channelNeedsAttention[chanIdx] = true;
    }
}

static const char *liveDestTag(uint32_t toNode) {
    if (toNode == 0xFFFFFFFF) return "B";
    if (s_myNodeId != 0 && toNode == s_myNodeId) return "M";
    return "U";
}

static void appendLiveRxSummary(const MeshPacket &pkt, int chanIdx, const char *portTag) {
    char timePrefix[12];
    char who[20];
    char line[96];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    // Keep the compact, decodable form on every build so formatLiveLineText
    // can render the verbose live row (Cardputer previously emitted a shorter
    // unrecognised form that fell through to the raw fallback).
    snprintf(line, sizeof(line), "R %s>%s %s c%d",
             who,
             liveDestTag(pkt.hdr.to),
             (portTag && portTag[0]) ? portTag : "D",
             chanIdx);
    liveFeedAddPrefixed(timePrefix, line, TFT_DARKGREY, 0, false);
}

static void appendLiveRxEncrypted(const MeshPacket &pkt) {
    char timePrefix[12];
    char who[20];
    char line[96];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    snprintf(line, sizeof(line), "R %s ENC h%02X", who, pkt.hdr.channel);
    liveFeedAddPrefixed(timePrefix, line, TFT_DARKGREY, 0, false);
}

static bool sendRoutingResult(uint32_t toNodeId, uint32_t requestId, uint32_t errorReason) {
    if (!Radio.isReady()) return false;
    if (toNodeId == 0 || toNodeId == 0xFFFFFFFF || requestId == 0) return false;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) return false;

    uint8_t proto[64];
    size_t protoLen = encodeRouting(requestId, s_myNodeId, errorReason, proto, sizeof(proto));
    if (protoLen == 0) return false;

    const ChannelKey &ck = CHANNEL_KEYS[0];  // ROUTING replies on primary channel.
    uint8_t cipher[96];
    uint32_t packetId = nextMeshPacketId();
    if (!encryptPayload(packetId, s_myNodeId, ck.key, ck.keyLen, proto, cipher, protoLen)) {
        return false;
    }

    uint8_t frame[sizeof(MeshHdr) + sizeof(cipher)];
    MeshHdr hdr = {};
    hdr.to = toNodeId;
    hdr.from = s_myNodeId;
    hdr.id = packetId;
    hdr.channel = ck.hash;
    hdr.flags = (uint8_t)(MESH_HOP_LIMIT & 0x07) |
                ((MESH_HOP_LIMIT & 0x07) << 5);
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, protoLen);
    return Radio.transmit(frame, sizeof(hdr) + protoLen);
}

// ── Managed flood rebroadcasting ──────────────────────────────────────────────
// Stock Meshtastic: every role rebroadcasts except CLIENT_MUTE / CLIENT_HIDDEN.
static bool roleRebroadcasts(uint8_t role) {
    return role != 1 /*CLIENT_MUTE*/ && role != 8 /*CLIENT_HIDDEN*/;
}

// Apply the configured rebroadcastMode filter to a received packet.
static bool rebroadcastModeAllows(const MeshPacket &pkt) {
    switch (s_cfg.rebroadcastMode) {
        case 2: // LOCAL_ONLY — only packets we decrypted on a known channel
            return pkt.decrypted && pkt.chanIdx >= 0;
        case 3: // KNOWN_ONLY — only from nodes already in our DB
            return Nodes.find(pkt.hdr.from) != nullptr;
        case 4: // CORE_PORTNUMS_ONLY — only decoded core portnums
            if (!pkt.decrypted) return false;
            switch (pkt.portnum) {
                case TEXT_MESSAGE_APP: case POSITION_APP: case NODEINFO_APP:
                case ROUTING_APP: case TELEMETRY_APP: case NEIGHBORINFO_APP:
                case TRACEROUTE_APP: return true;
                default: return false;
            }
        default: // ALL / ALL_SKIP_DECODING — relay raw regardless of decode
            return true;
    }
}

// Deferred rebroadcast queue. Relaying nodes wait out a short random
// contention-jitter window before re-transmitting so multiple relays don't all
// key up simultaneously and collide on-air. This wait MUST NOT be a blocking
// delay() on the main loop — that starves lv_timer_handler() and makes the whole
// UI sluggish (every relayed packet froze the screen for up to 150 ms). Instead
// we stash the framed packet with a send-after deadline and transmit it from
// servicePendingRebroadcast() once the jitter window elapses.
struct PendingRebroadcast {
    uint8_t  frame[sizeof(MeshHdr) + sizeof(((MeshPacket *)nullptr)->rawCipher)];
    size_t   len;
    uint32_t sendAtMs;
    bool     active;
};
static constexpr uint8_t kMaxPendingRebroadcast = 8;
static PendingRebroadcast s_pendingRebroadcast[kMaxPendingRebroadcast];

static PendingRebroadcast *allocPendingRebroadcast() {
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++)
        if (!s_pendingRebroadcast[i].active) return &s_pendingRebroadcast[i];
    return nullptr;
}

// Re-transmit a freshly received packet verbatim (hop limit decremented) so it
// propagates across the mesh. Caller guarantees the packet is new (passed
// isDuplicate) and not from us. The original on-air payload is relayed as-is
// from pkt.rawCipher — no re-encryption.
static void maybeRebroadcastPacket(const MeshPacket &pkt) {
    if (!Radio.isReady() || s_myNodeId == 0) return;
    if (!roleRebroadcasts(s_cfg.deviceRole)) return;

    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    if (hopLimit == 0) return;               // out of hops
    if (pkt.hdr.to == s_myNodeId) return;    // we're the endpoint; broadcasts/others relay
    if (pkt.rawLen == 0) return;             // no stored on-air payload to relay
    if (!rebroadcastModeAllows(pkt)) return;

    MeshHdr hdr = pkt.hdr;
    hdr.flags = (uint8_t)((pkt.hdr.flags & 0xF8) | ((hopLimit - 1) & 0x07));
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);

    PendingRebroadcast *slot = allocPendingRebroadcast();
    if (!slot) {                             // queue full: drop rather than block
        debugLogMessages("[fwd] drop (queue full) id=%08lx\n",
                         (unsigned long)pkt.hdr.id);
        return;
    }
    memcpy(slot->frame, &hdr, sizeof(hdr));
    memcpy(slot->frame + sizeof(hdr), pkt.rawCipher, pkt.rawLen);
    slot->len      = sizeof(hdr) + pkt.rawLen;
    slot->sendAtMs = millis() + 20 + (esp_random() % 131);  // 20–150 ms jitter
    slot->active   = true;

    debugLogMessages("[fwd] queued from=%08lx id=%08lx hop %u->%u %s\n",
                     (unsigned long)pkt.hdr.from, (unsigned long)pkt.hdr.id,
                     hopLimit, hopLimit - 1, portnumName(pkt.portnum));
}

// Transmit at most one queued rebroadcast whose contention-jitter deadline has
// passed. One TX per loop bounds how long a burst of relays can block the loop
// (Radio.transmit() blocks for the packet airtime); remaining slots go out on
// subsequent iterations a few ms later, which also spreads them out on-air.
static void servicePendingRebroadcast(uint32_t now) {
    if (!Radio.isReady()) return;
    for (uint8_t i = 0; i < kMaxPendingRebroadcast; i++) {
        PendingRebroadcast &p = s_pendingRebroadcast[i];
        if (!p.active || (int32_t)(now - p.sendAtMs) < 0) continue;
        p.active = false;
        if (Radio.transmit(p.frame, p.len)) s_rebroadcastCount++;
        return;
    }
}

static bool processMeshPacket(const MeshPacket &rxPkt) {
    MeshPacket pkt = rxPkt;

    if (isDuplicate(pkt.hdr.from, pkt.hdr.id)) return false;

    // Match v1 behavior: ignore reflected copies of our own transmitted packets.
    if (s_myNodeId != 0 && pkt.hdr.from == s_myNodeId) return false;

    // User preference: drop packets that arrived from MQTT before any handling
    // (display, rebroadcast, or node-state updates).
    if (s_cfg.ignoreMqtt && (pkt.hdr.flags & 0x10)) return false;

    Nodes.updateFromPacket(pkt);

    // Live SNR/RSSI sparkline samples (one per received packet).
    chartPushSample(s_snrHist, pkt.snr);
    chartPushSample(s_rssiHist, pkt.rssi);

    if (!pkt.decrypted && pkt.hdr.channel == 0 && pkt.rawLen > 12) {
        NodeEntry *sender = Nodes.find(pkt.hdr.from);
        if (sender && sender->hasPubKey) {
            uint8_t plain[256];
            size_t plainLen = sizeof(plain);
            if (decryptPki(pkt.hdr, pkt.rawCipher, pkt.rawLen, sender->pubKey, plain, plainLen)) {
                pkt.decrypted = true;
                pkt.chanIdx = -2;
                const uint8_t *payPtr = nullptr;
                size_t payLen = 0;
                decodeData(plain, plainLen, pkt.portnum, payPtr, payLen,
                           pkt.requestId, pkt.wantResponse,
                           &pkt.dataDest, &pkt.hasDataDest,
                           &pkt.dataSource, &pkt.hasDataSource);
                if (payPtr && payLen <= sizeof(pkt.payload)) {
                    memcpy(pkt.payload, payPtr, payLen);
                    pkt.payloadLen = payLen;
                }
            }
        }
    }

    // Managed flood: relay new traffic onward before handling it locally.
    maybeRebroadcastPacket(pkt);

    // Native MQTT uplink: mirror packets heard on a known, named, uplink-enabled
    // channel up to the broker. Never re-publish packets that arrived via MQTT
    // (via_mqtt flag) — that would form an MQTT→RF→MQTT loop with downlink.
    if (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS &&
        !(pkt.hdr.flags & 0x10) &&
        CHANNEL_KEYS[pkt.chanIdx].name[0] &&
        CHANNEL_KEYS[pkt.chanIdx].uplinkEnabled) {
        mqttBridgePublish(pkt, CHANNEL_KEYS[pkt.chanIdx].name);
    }

    bool wantsAck = ((pkt.hdr.flags & (1 << 3)) != 0);
    bool addressedToMe = (pkt.hdr.to == s_myNodeId)
                      || (pkt.hasDataDest && pkt.dataDest == s_myNodeId);

    int chanIdx = (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) ? pkt.chanIdx : 0;
    if (!pkt.decrypted) {
        appendLiveRxEncrypted(pkt);

        if (wantsAck && addressedToMe) {
            uint32_t err = (pkt.hdr.channel == 0) ? 35u : 6u;  // PKI_UNKNOWN_PUBKEY / NO_CHANNEL
            (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, err);

            if (err == 35) {
                static uint32_t sLastNodeInfoReqNode = 0;
                static uint32_t sLastNodeInfoReqMs = 0;
                uint32_t now = millis();
                if (pkt.hdr.from != sLastNodeInfoReqNode || (now - sLastNodeInfoReqMs) > 5000) {
                    (void)Channels.sendNodeInfo(s_myNodeId,
                                                s_cfg.nodeLong,
                                                s_cfg.nodeShort,
                                                pkt.hdr.from,
                                                true,
                                                s_cfg.okToMqtt);
                    sLastNodeInfoReqNode = pkt.hdr.from;
                    sLastNodeInfoReqMs = now;
                }
            }
        }
        return false;
    }

    switch (pkt.portnum) {
        case TEXT_MESSAGE_APP: {
            char textBuf[MESH_TEXT_MAX_LEN + 1];
            size_t copy = utf8util::copyTruncateBytes(
                textBuf,
                sizeof(textBuf),
                pkt.payload,
                pkt.payloadLen);
            for (size_t i = 0; i < copy; i++) {
                if (textBuf[i] == '\r' || textBuf[i] == '\n') textBuf[i] = ' ';
            }

            if (textBuf[0]) {
                const bool viaMqtt = (pkt.hdr.flags & 0x10) != 0;
                bool isDirectToMe = (pkt.hdr.to == s_myNodeId)
                                 || (pkt.hasDataDest && pkt.dataDest == s_myNodeId);

                // Suppress chat/DM display for user-ignored senders. We still
                // ACK direct-to-me messages so the sender's radio stops
                // retrying, and NodeDB/Live traffic summaries still get the
                // update via the callers above and appendLiveRxSummary below.
                if (Ignored.contains(pkt.hdr.from)) {
                    if (wantsAck && isDirectToMe) {
                        (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
                    }
                    appendLiveRxSummary(pkt, chanIdx, "T");
                    return false;
                }

                if (isDirectToMe) {
                    NodeEntry *sender = Nodes.find(pkt.hdr.from);
                    char senderShort[5] = {};
                    if (sender && sender->shortName[0]) {
                        utf8util::copyTruncate(senderShort, sizeof(senderShort), sender->shortName);
                    }

                    char timePrefix[12];
                    char prefix[32];
                    liveBuildPrefix(timePrefix, sizeof(timePrefix));
                    if (senderShort[0]) {
                        snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, senderShort);
                    } else {
                        char who[16];
                        liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
                        snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, who);
                    }

                    bool viewingDm = false;
                    if (s_dmModal && s_dmSelection >= 0 && s_dmSelection < s_dmConvCount) {
                        viewingDm = (s_dmConvNodeIds[s_dmSelection] == pkt.hdr.from);
                    }

                    DMs.addMessage(pkt.hdr.from,
                                   senderShort[0] ? senderShort : nullptr,
                                   prefix,
                                   textBuf,
                                   TFT_WHITE,
                                   !viewingDm,
                                   chanIdx,
                                   pkt.hdr.id);  // retain sender pid for web reply/tapback targeting
                    if (viewingDm) {
                        DMs.markRead(pkt.hdr.from);
                    }
                } else {
                    appendRxText(chanIdx, pkt.hdr.from, textBuf, pkt.hdr.id, viaMqtt);
                }

                if (isDirectToMe || !channelIsMuted(chanIdx)) {
                    triggerMessageAlert();
                }
                if (wantsAck && isDirectToMe) {
                    (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
                }

                appendLiveRxSummary(pkt, chanIdx, "T");
                return isDirectToMe ? (s_dmModal != nullptr) : (chanIdx == s_activeChannel);
            }
            return false;
        }

        case STORE_FORWARD_APP: {
            // If the user has disabled the Store-and-Forward client, drop these
            // packets silently (no live entry, no display).
            if (!s_cfg.snfClientEnabled) {
                return false;
            }

            // Decode the Meshtastic StoreAndForward proto looking for:
            //   field 1 (varint): rr  (RequestResponse enum)
            //   field 5 (bytes):  text payload (only present for replayed text)
            uint32_t rr = 0;
            const uint8_t *sfText = nullptr;
            size_t sfTextLen = 0;
            size_t i = 0;
            while (i < pkt.payloadLen) {
                uint64_t tag = 0;
                i = pbReadVarint(pkt.payload, pkt.payloadLen, i, tag);
                if (!i) break;

                uint32_t field = (uint32_t)(tag >> 3);
                uint32_t wt = (uint32_t)(tag & 7);
                if (wt == 0) {
                    uint64_t v = 0;
                    i = pbReadVarint(pkt.payload, pkt.payloadLen, i, v);
                    if (!i) break;
                    if (field == 1) rr = (uint32_t)v;
                } else if (wt == 2) {
                    uint64_t sz = 0;
                    size_t j = pbReadVarint(pkt.payload, pkt.payloadLen, i, sz);
                    if (!j) break;
                    if (j + sz > pkt.payloadLen) break;
                    if (field == 5) {
                        sfText = pkt.payload + j;
                        sfTextLen = (size_t)sz;
                    }
                    i = j + sz;
                } else if (wt == 5) {
                    if (i + 4 > pkt.payloadLen) break;
                    i += 4;
                } else if (wt == 1) {
                    if (i + 8 > pkt.payloadLen) break;
                    i += 8;
                } else {
                    break;
                }
            }

            // Only act on text-replay variants (ROUTER_TEXT_BROADCAST=8,
            // ROUTER_TEXT_DIRECT=9). Heartbeats/stats/etc. are just logged.
            bool isTextReplay = (rr == 8 || rr == 9) && sfText && sfTextLen > 0;
            if (isTextReplay) {
                char textBuf[MESH_TEXT_MAX_LEN + 1];
                size_t copy = utf8util::copyTruncateBytes(
                    textBuf,
                    sizeof(textBuf),
                    sfText,
                    sfTextLen);
                for (size_t k = 0; k < copy; k++) {
                    if (textBuf[k] == '\r' || textBuf[k] == '\n') textBuf[k] = ' ';
                }

                if (textBuf[0]) {
                    // Prefix the replayed text with "[SF]" so the user can tell
                    // it came from a Store-and-Forward server rather than the
                    // original sender in real time.
                    char prefixedBuf[MESH_TEXT_MAX_LEN + 1];
                    snprintf(prefixedBuf, sizeof(prefixedBuf), "[SF] %s", textBuf);

                    const bool viaMqtt = (pkt.hdr.flags & 0x10) != 0;
                    // rr=9 (ROUTER_TEXT_DIRECT) means this replay is for a DM
                    // that was originally addressed to us.
                    bool isDirectToMe = (rr == 9);

                    if (isDirectToMe) {
                        NodeEntry *sender = Nodes.find(pkt.hdr.from);
                        char senderShort[5] = {};
                        if (sender && sender->shortName[0]) {
                            utf8util::copyTruncate(senderShort, sizeof(senderShort), sender->shortName);
                        }

                        char timePrefix[12];
                        char prefix[32];
                        liveBuildPrefix(timePrefix, sizeof(timePrefix));
                        if (senderShort[0]) {
                            snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, senderShort);
                        } else {
                            char who[16];
                            liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
                            snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, who);
                        }

                        bool viewingDm = false;
                        if (s_dmModal && s_dmSelection >= 0 && s_dmSelection < s_dmConvCount) {
                            viewingDm = (s_dmConvNodeIds[s_dmSelection] == pkt.hdr.from);
                        }

                        DMs.addMessage(pkt.hdr.from,
                                       senderShort[0] ? senderShort : nullptr,
                                       prefix,
                                       prefixedBuf,
                                       TFT_WHITE,
                                       !viewingDm,
                                       chanIdx,
                                       0);
                        if (viewingDm) {
                            DMs.markRead(pkt.hdr.from);
                        }
                    } else {
                        appendRxText(chanIdx, pkt.hdr.from, prefixedBuf, pkt.hdr.id, viaMqtt);
                    }

                    if (isDirectToMe || !channelIsMuted(chanIdx)) {
                        triggerMessageAlert();
                    }
                    appendLiveRxSummary(pkt, chanIdx, "F");
                    return isDirectToMe ? (s_dmModal != nullptr) : (chanIdx == s_activeChannel);
                }
            }

            // Non-text S&F traffic (heartbeats, stats, pings). Just log.
            appendLiveRxSummary(pkt, chanIdx, "F");
            return false;
        }

        case ROUTING_APP: {
            if (!pkt.requestId) return false;

            uint32_t errorReason = 0;
            const uint8_t *routeReplyPayload = nullptr;
            size_t routeReplyLen = 0;
            size_t i = 0;
            while (i < pkt.payloadLen) {
                uint64_t tag = 0;
                i = pbReadVarint(pkt.payload, pkt.payloadLen, i, tag);
                if (!i) break;

                uint32_t field = (uint32_t)(tag >> 3);
                uint32_t wt = (uint32_t)(tag & 7);
                if (wt == 0) {
                    uint64_t v = 0;
                    i = pbReadVarint(pkt.payload, pkt.payloadLen, i, v);
                    if (!i) break;
                    if (field == 3) {
                        errorReason = (uint32_t)v;
                    }
                } else if (wt == 2) {
                    uint64_t sz = 0;
                    size_t j = pbReadVarint(pkt.payload, pkt.payloadLen, i, sz);
                    if (!j) break;
                    if (j + sz > pkt.payloadLen) break;
                    if (field == 2) {
                        routeReplyPayload = pkt.payload + j;
                        routeReplyLen = (size_t)sz;
                    }
                    i = j + sz;
                } else if (wt == 5) {
                    if (i + 4 > pkt.payloadLen) break;
                    i += 4;
                } else if (wt == 1) {
                    if (i + 8 > pkt.payloadLen) break;
                    i += 8;
                } else {
                    break;
                }
            }

            bool isAck = (errorReason == 0);
            bool dmRoutingMatched = DMs.handleRoutingResult(pkt.hdr.from, pkt.requestId, errorReason);
            tracerouteProgressOnRouting(pkt.hdr.from,
                                        pkt.requestId,
                                        errorReason,
                                        routeReplyPayload,
                                        routeReplyLen,
                                        (pkt.hdr.flags & 0x10) != 0);

            if (isAck) {
                Channels.setAckStateFrom(pkt.requestId, pkt.hdr.from);
            } else {
                Channels.setAckState(pkt.requestId, DisplayLine::NAKED);
                if (dmRoutingMatched) {
                    DmConv *conv = DMs.find(pkt.hdr.from);
                    if (conv) {
                        const char *errName = routingErrorName(errorReason);
                        char errMsg[44];
                        if (errName) {
                            snprintf(errMsg, sizeof(errMsg), "! NAK %s(%lu)",
                                     errName,
                                     (unsigned long)errorReason);
                        } else {
                            snprintf(errMsg, sizeof(errMsg), "! NAK err=%lu",
                                     (unsigned long)errorReason);
                        }
                        DMs.addMessage(pkt.hdr.from, nullptr, "", errMsg, TFT_RED,
                                       false, -1, 0);
                    }
                }
            }

            appendLiveRxSummary(pkt, chanIdx, isAck ? "A" : "K");
            return true;
        }

        case NODEINFO_APP: {
            UserInfo u = {};
            if (decodeUser(pkt.payload, pkt.payloadLen, u)) {
                Nodes.updateUser(pkt.hdr.from, u);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "N");
            return false;
        }

        case POSITION_APP: {
            PositionInfo p = {};
            if (decodePosition(pkt.payload, pkt.payloadLen, p)) {
                Nodes.updatePosition(pkt.hdr.from, p);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "P");
            return false;
        }

        case TELEMETRY_APP: {
            TelemetryInfo t = {};
            if (decodeTelemetry(pkt.payload, pkt.payloadLen, t)) {
                Nodes.updateTelemetry(pkt.hdr.from, t);
                if (t.hasDeviceMetrics) {
                    chartPushSample(s_chUtilHist, t.chUtil);
                    chartPushSample(s_airUtilHist, t.airUtil);
                }
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "E");
            return false;
        }

        case NEIGHBORINFO_APP: {
            NeighborInfoPayload n = {};
            if (decodeNeighborInfo(pkt.payload, pkt.payloadLen, n)) {
                debugLogMessages("[neighborinfo] from=!%08lx node=!%08lx neighbors=%u interval=%lus\n",
                                 (unsigned long)pkt.hdr.from,
                                 (unsigned long)n.nodeId,
                                 (unsigned)n.neighborCount,
                                 (unsigned long)n.nodeBroadcastIntervalS);
            }
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "G");
            return false;
        }

        case TRACEROUTE_APP: {
            tracerouteProgressOnResponse(pkt);
            if (wantsAck && addressedToMe) {
                (void)sendRoutingResult(pkt.hdr.from, pkt.hdr.id, 0);
            }
            appendLiveRxSummary(pkt, chanIdx, "R");
            return false;
        }

        default:
            return false;
    }
}

// Drain a pending Chat-tab send (queued by the web server) on the main loop,
// where we own the node id and the LoRa TX path.
static void serviceWebChatSend() {
    bool isDm = false;
    uint32_t target = 0, replyId = 0, emoji = 0;
    char text[MESH_TEXT_MAX_LEN + 1];
    if (!webCfgTakeChatSend(isDm, target, text, sizeof(text), replyId, emoji)) return;
    if (!text[0]) return;
    if (s_myNodeId == 0) deriveNodeId();
    if (s_myNodeId == 0) return;

    if (isDm) {
        DMs.sendDm(s_myNodeId, target, text, replyId, emoji);
        refreshDmModal(true);
    } else {
        int ch = (int)target;
        if (ch < 0 || ch >= MESH_CHANNELS) ch = s_activeChannel;
        Channels.sendText(s_myNodeId, text, s_cfg.okToMqtt, ch, replyId, emoji);
        refreshChatView(true);
    }
}

static bool pollMeshRx() {
    bool changed = false;
    MeshPacket pkt;
    while (Radio.pollRx(pkt)) {
        changed = processMeshPacket(pkt) || changed;
    }

    if (Channels.expireAcks()) {
        changed = true;
    }

    return changed;
}

// Set by the MQTT downlink inject when it produces a UI-visible change; consumed
// in loop() so the same refresh path as RX packets runs this iteration.
static bool s_mqttDownlinkUiDirty = false;

// Downlink sink: a ServiceEnvelope arrived from the broker. Reconstruct a
// received-style packet and run it through the normal RX pipeline so it displays
// and (for router roles) rebroadcasts onto LoRa — gated by the channel's
// downlink flag. Called from mqttBridgeLoop() on the main loop.
static void mqttDownlinkInject(const MeshHdr &hdr, const uint8_t *cipher,
                               size_t cipherLen, const char *chanName) {
    // Loop guard: ignore our own packets echoed back by the broker. The dedup
    // check (against RF-heard copies and duplicate MQTT copies) is left to
    // processMeshPacket() below — isDuplicate() records as a side effect, so
    // calling it here would make that check drop the packet.
    if (s_myNodeId != 0 && hdr.from == s_myNodeId) {
        Serial.println("[mqtt] downlink drop: own packet echoed back");
        return;
    }

    // Resolve the channel by name; require it to exist locally with downlink on.
    int idx = -1;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        if (CHANNEL_KEYS[i].name[0] && chanName &&
            strcmp(CHANNEL_KEYS[i].name, chanName) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        Serial.printf("[mqtt] downlink drop: no local channel named '%s'\n",
                      chanName ? chanName : "(null)");
        return;
    }
    if (!CHANNEL_KEYS[idx].downlinkEnabled) {
        Serial.printf("[mqtt] downlink drop: channel '%s' has downlink disabled\n",
                      CHANNEL_KEYS[idx].name);
        return;
    }
    Serial.printf("[mqtt] downlink inject: channel='%s' from=%08lx\n",
                  CHANNEL_KEYS[idx].name, (unsigned long)hdr.from);

    // Reconstruct the decoded packet the same way MeshRadio::pollRx() does, then
    // hand it to the shared RX handler (decrypt/display + rebroadcast).
    MeshPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.hdr      = hdr;               // via_mqtt already forced on by the decoder
    pkt.rxMs     = millis();
    pkt.chanIdx  = -1;
    pkt.decrypted = false;
    if (cipherLen > 0 && cipherLen <= sizeof(pkt.rawCipher) && hdr.channel != 0) {
        memcpy(pkt.rawCipher, cipher, cipherLen);
        pkt.rawLen = cipherLen;
        uint8_t plain[256];
        pkt.chanIdx = decryptPacket(pkt.hdr, cipher, plain, cipherLen);
        pkt.decrypted = (pkt.chanIdx >= 0);
        if (pkt.decrypted) {
            const uint8_t *payPtr; size_t payLen;
            decodeData(plain, cipherLen, pkt.portnum, payPtr, payLen,
                       pkt.requestId, pkt.wantResponse,
                       &pkt.dataDest, &pkt.hasDataDest,
                       &pkt.dataSource, &pkt.hasDataSource);
            if (payPtr && payLen <= sizeof(pkt.payload)) {
                memcpy(pkt.payload, payPtr, payLen);
                pkt.payloadLen = payLen;
            }
        }
    }

    if (processMeshPacket(pkt)) s_mqttDownlinkUiDirty = true;
}

static uint32_t announceIntervalMs(uint32_t intervalS) {
    if (intervalS == 0) return 0;
    if (intervalS >= (0xFFFFFFFFUL / 1000UL)) return 0xFFFFFFFFUL;
    uint32_t ms = intervalS * 1000UL;
    return (ms < 1000UL) ? 1000UL : ms;
}

static bool announceDue(uint32_t nowMs, uint32_t nextMs, uint32_t intervalS) {
    if (intervalS == 0) return false;
    if (nextMs == 0) return true;
    return (int32_t)(nowMs - nextMs) >= 0;
}

static void scheduleAnnounceNext(uint32_t &nextMs, uint32_t nowMs, uint32_t intervalS) {
    uint32_t intervalMs = announceIntervalMs(intervalS);
    if (intervalMs == 0) {
        nextMs = 0;
        return;
    }
    nextMs = nowMs + intervalMs;
}

static void scheduleAnnounceRetry(uint32_t &nextMs, uint32_t nowMs) {
    nextMs = nowMs + 5000UL;
}

static bool resolveAnnouncePosition(int32_t &latI, int32_t &lonI, int32_t &altM) {
    if (gpsIsEnabled() && gpsHasFix()) {
        latI = gpsLatI();
        lonI = gpsLonI();
        altM = gpsAltM();
        return true;
    }

    NodeEntry *self = Nodes.find(s_myNodeId);
    if (self && self->hasPosition && (self->latI != 0 || self->lonI != 0)) {
        latI = self->latI;
        lonI = self->lonI;
        altM = self->alt;
        return true;
    }

    if (s_cfg.latI != 0 || s_cfg.lonI != 0) {
        latI = s_cfg.latI;
        lonI = s_cfg.lonI;
        altM = s_cfg.alt;
        return true;
    }

    return false;
}

static void serviceNodeInfoAnnounce(uint32_t nowMs) {
    bool forceAnnounce = webCfgAnnounceRequested();

    bool nodeInfoDue = forceAnnounce || announceDue(nowMs, s_nextNodeInfoTxMs, s_cfg.nodeInfoIntervalS);
    bool positionDue = forceAnnounce || announceDue(nowMs, s_nextPositionTxMs, s_cfg.posIntervalS);

    if (!nodeInfoDue && !positionDue) return;

    if (!s_radioReady) {
        if (forceAnnounce) webCfgQueueAnnounce();
        return;
    }

    if (nodeInfoDue) {
        bool ok = Channels.sendNodeInfo(s_myNodeId,
                                        s_cfg.nodeLong,
                                        s_cfg.nodeShort,
                                        0xFFFFFFFF,
                                        false,
                                        s_cfg.okToMqtt);
        if (ok) scheduleAnnounceNext(s_nextNodeInfoTxMs, nowMs, s_cfg.nodeInfoIntervalS);
        else scheduleAnnounceRetry(s_nextNodeInfoTxMs, nowMs);
    }

    if (positionDue) {
        int32_t latI = 0;
        int32_t lonI = 0;
        int32_t altM = 0;
        if (resolveAnnouncePosition(latI, lonI, altM)) {
            bool ok = Channels.sendPosition(s_myNodeId, latI, lonI, altM, s_cfg.okToMqtt);
            if (ok) scheduleAnnounceNext(s_nextPositionTxMs, nowMs, s_cfg.posIntervalS);
            else scheduleAnnounceRetry(s_nextPositionTxMs, nowMs);
        } else {
            scheduleAnnounceNext(s_nextPositionTxMs, nowMs, s_cfg.posIntervalS);
            if (forceAnnounce) {
                liveFeedAddPrefixed("", "[position] skip: no fix/fallback", TFT_DARKGREY, 0, false);
            }
        }
    }

    // Manual announce should also push telemetry immediately (when enabled).
    // serviceTelemetryAnnounce() runs later in the loop and will transmit now
    // because nextTx=0 means "due immediately".
    if (forceAnnounce) {
        if (s_cfg.telDeviceEnabled) s_nextDeviceTelemetryTxMs = 0;
#if HAS_ENV_SENSOR_TELEMETRY
        if (s_cfg.telEnvEnabled) s_nextEnvTelemetryTxMs = 0;
#endif
        if (s_cfg.neighborInfoEnabled && s_cfg.neighborInfoOverLora) {
            s_nextNeighborInfoTxMs = 0;
        }
    }
}

static void serviceTelemetryAnnounce(uint32_t nowMs) {
    bool forceTelemetry = webCfgTelemetryRequested();

    bool devDue = forceTelemetry || (s_cfg.telDeviceEnabled
        && announceDue(nowMs, s_nextDeviceTelemetryTxMs, s_cfg.telDeviceIntervalS));

#if HAS_ENV_SENSOR_TELEMETRY
    bool envDue = s_cfg.telEnvEnabled
        && (forceTelemetry || announceDue(nowMs, s_nextEnvTelemetryTxMs, s_cfg.telEnvIntervalS));
#else
    bool envDue = false;
#endif

    if (!devDue && !envDue) return;
    if (!s_radioReady) {
        if (forceTelemetry) webCfgQueueTelemetry();
        return;
    }

    if (devDue) {
        bool ok = Channels.sendTelemetryDevice(s_myNodeId, s_cfg.okToMqtt);
        if (ok) scheduleAnnounceNext(s_nextDeviceTelemetryTxMs, nowMs, s_cfg.telDeviceIntervalS);
        else scheduleAnnounceRetry(s_nextDeviceTelemetryTxMs, nowMs);
    }

#if HAS_ENV_SENSOR_TELEMETRY
    if (envDue) {
        bool hasSensor = envHasSensor() || envBegin();
        if (!hasSensor) {
            // If the sensor was slow to come up at boot, retry sooner than the
            // full telemetry interval so env telemetry starts promptly.
            s_nextEnvTelemetryTxMs = nowMs + 30000UL;
            return;
        }

        EnvReading env = {};
        if (!envRead(env)) {
            scheduleAnnounceRetry(s_nextEnvTelemetryTxMs, nowMs);
            return;
        }

        bool ok = Channels.sendTelemetryEnvironment(s_myNodeId,
                                                    env.temperatureC,
                                                    env.humidityPct,
                                                    env.pressureHpa,
                                                    s_cfg.okToMqtt);
        if (ok) scheduleAnnounceNext(s_nextEnvTelemetryTxMs, nowMs, s_cfg.telEnvIntervalS);
        else scheduleAnnounceRetry(s_nextEnvTelemetryTxMs, nowMs);
    }
#endif
}

// ── Auto-favorite nearby nodes ───────────────────────────────────────────────
// Great-circle distance in meters between two positions in 1e7-scaled degrees.
static float geoDistanceM(int32_t latI1, int32_t lonI1, int32_t latI2, int32_t lonI2) {
    const float kDegToRad = 0.017453292519943295f;
    const float lat1 = (float)latI1 * 1e-7f * kDegToRad;
    const float lat2 = (float)latI2 * 1e-7f * kDegToRad;
    const float dLat = lat2 - lat1;
    const float dLon = ((float)lonI2 - (float)lonI1) * 1e-7f * kDegToRad;
    float a = sinf(dLat * 0.5f); a *= a;
    float b = sinf(dLon * 0.5f); b *= b;
    float h = a + cosf(lat1) * cosf(lat2) * b;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    return 2.0f * 6371000.0f * asinf(sqrtf(h));
}

// Positions change slowly and each promotion writes NVS, so a lazy sweep is
// plenty; it also re-runs as our own position moves, not just on new packets.
static const uint32_t kAutoFavIntervalMs = 30000;
static uint32_t s_lastAutoFavMs = 0;

// Favorite any node whose last known position is within the configured radius.
// Opt-in and additive only: it never un-favorites, because a node drifting out
// of range (or simply going quiet) must not silently clear something the user
// pinned by hand — and favorites are what protect a node from eviction.
static void serviceAutoFavorite(uint32_t nowMs) {
    if (!s_cfg.autoFavoriteEnabled || s_cfg.autoFavoriteRangeM == 0) return;
    if (s_lastAutoFavMs != 0 && (uint32_t)(nowMs - s_lastAutoFavMs) < kAutoFavIntervalMs) return;
    s_lastAutoFavMs = nowMs;

    // Prefer a live fix; fall back to the last known / manually set position.
    int32_t myLat, myLon;
    if (gpsIsEnabled() && gpsHasFix()) {
        myLat = gpsLatI();
        myLon = gpsLonI();
    } else {
        myLat = s_cfg.latI;
        myLon = s_cfg.lonI;
    }
    if (myLat == 0 && myLon == 0) return;   // our own position is unknown

    const float rangeM = (float)s_cfg.autoFavoriteRangeM;
    const int count = Nodes.count();
    // Nodes.at() rather than getByRank(): getByRank() re-sorts on every call and
    // favorites sort first, so promoting one mid-scan would shuffle the table
    // underneath this loop and skip entries.
    for (int i = 0; i < count; i++) {
        NodeEntry *e = Nodes.at(i);
        if (!e || e->nodeId == 0 || e->favorite) continue;
        if (e->nodeId == s_myNodeId) continue;
        if (!e->hasPosition) continue;
        if (e->latI == 0 && e->lonI == 0) continue;

        const float d = geoDistanceM(myLat, myLon, e->latI, e->lonI);
        if (d <= rangeM) {
            if (Nodes.setFavorite(e->nodeId, true)) {
                Serial.printf("[autofav] favorited !%08lx at %.0f m (limit %.0f m)\n",
                              (unsigned long)e->nodeId, (double)d, (double)rangeM);
            }
        }
    }
}

static void serviceNeighborInfoAnnounce(uint32_t nowMs) {
    bool due = s_cfg.neighborInfoEnabled
        && s_cfg.neighborInfoOverLora
        && announceDue(nowMs, s_nextNeighborInfoTxMs, s_cfg.neighborInfoIntervalS);

    if (!due) return;
    if (!s_radioReady) return;

    NeighborEdgeInfo neighbors[MESH_NEIGHBOR_MAX] = {};
    size_t neighborCount = 0;
    int totalNodes = Nodes.count();
    for (int rank = 0; rank < totalNodes && neighborCount < MESH_NEIGHBOR_MAX; rank++) {
        NodeEntry *entry = Nodes.getByRank(rank);
        if (!entry || entry->nodeId == 0 || entry->nodeId == s_myNodeId) continue;
        if (entry->hops != 0) continue;
        if (entry->lastHeardMs == 0) continue;

        neighbors[neighborCount].nodeId = entry->nodeId;
        neighbors[neighborCount].snr = entry->snr;
        neighborCount++;
    }

    if (neighborCount == 0) {
        scheduleAnnounceNext(s_nextNeighborInfoTxMs, nowMs, s_cfg.neighborInfoIntervalS);
        return;
    }

    bool ok = Channels.sendNeighborInfo(s_myNodeId,
                                        s_cfg.neighborInfoIntervalS,
                                        neighbors,
                                        neighborCount,
                                        s_cfg.okToMqtt);
    if (ok) scheduleAnnounceNext(s_nextNeighborInfoTxMs, nowMs, s_cfg.neighborInfoIntervalS);
    else scheduleAnnounceRetry(s_nextNeighborInfoTxMs, nowMs);
}

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

static void formatChatDateLabel(uint32_t epoch, char *out, size_t len) {
    if (!out || len == 0) return;
    out[0] = '\0';
    if (epoch < 1700000000) return;
    time_t t = (time_t)epoch;
    struct tm tmv;
    localtime_r(&t, &tmv);
    static const char *kMonths[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
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
    uint32_t h = nodeId * 2654435761u;
    const uint8_t *c = pal[(h >> 24) % n];
    return rgb565(c[0], c[1], c[2]);
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

static void chatBubbleBeginRender(lv_obj_t *list) {
    s_chatBubbleListW = 0;
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

// Create one message bubble (row wrapper + colored container + optional name tag
// + body) in the chat list. Right-aligned + accent for our own messages,
// left-aligned + node color for others. Updates the scroll anchor pointers.
static void chatMakeBubble(lv_obj_t *list, uint32_t sender, bool isMe,
                           const char *nameTag, const char *body,
                           DisplayLine::AckState ackState,
                           uint32_t replyPacketId, bool isSelected,
                           lv_obj_t **outLast, lv_obj_t **outSelected,
                           lv_event_cb_t onPressed) {
    lv_obj_t *rowW = lv_obj_create(list);
    lv_obj_remove_style_all(rowW);
    lv_obj_set_width(rowW, lv_pct(100));
    lv_obj_set_height(rowW, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(rowW, 1, 0);
    lv_obj_set_style_pad_bottom(rowW, 1, 0);
    lv_obj_set_flex_flow(rowW, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rowW, isMe ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

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
    const bool outline = (s_cfg.chatStyle == CHAT_STYLE_OUTLINE);
    const int borderW = outline ? ((s_cfg.uiMode == UI_MODE_LIGHT) ? 2 : 1) : 0;
    const lv_font_t *bubbleFont = scaledChatFont(kChannelChatFont);
    const lv_coord_t bubbleMaxW = chatBubbleLabelMaxWidth(borderW);
    lv_color_t tagColor;   // sender name + ME/ME(SENT) tag
    lv_color_t bodyColor;  // message text
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

    if (nameTag && nameTag[0]) {
        lv_obj_t *nm = lv_label_create(b);
        lv_obj_set_style_text_font(nm, bubbleFont, 0);
        lv_obj_set_style_text_color(nm, tagColor, 0);
        lv_obj_set_style_text_opa(nm, LV_OPA_70, 0);
        lv_label_set_text(nm, nameTag);
        // A long chat name can be wider than the bubble on its own.
        chatFitBubbleLabel(nm, bubbleFont, bubbleMaxW);
    }

    const char *stateTag = nullptr;
    if (isMe) {
        switch (ackState) {
            case DisplayLine::ACKED:
            case DisplayLine::ACKED_RELAY:
                stateTag = "ME (SENT)";
                break;
            default:
                stateTag = "ME";
                break;
        }
    }
    if (stateTag) {
        lv_obj_t *st = lv_label_create(b);
        lv_obj_set_style_text_font(st, bubbleFont, 0);   // "ME"/"ME (SENT)" always fits
        lv_obj_set_style_text_color(st, tagColor, 0);
        lv_obj_set_style_text_opa(st, LV_OPA_70, 0);
        lv_label_set_text(st, stateTag);
    }

    lv_obj_t *bl = lv_label_create(b);
    lv_obj_set_style_text_font(bl, bubbleFont, 0);
    lv_obj_set_style_text_color(bl, bodyColor, 0);
    lv_label_set_long_mode(bl, LV_LABEL_LONG_CLIP);  // body carries explicit '\n'
    lv_obj_set_width(bl, LV_SIZE_CONTENT);
    setLabelTextEmojiSafe(bl, body);
    // Measured after the text is set: emoji substitution rewrites it, so the
    // source string is not what actually gets drawn.
    chatFitBubbleLabel(bl, bubbleFont, bubbleMaxW);

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
    }

    if (outLast) *outLast = rowW;
}

// Render the chat rows as per-node colored bubbles (Bubbles chat style).
static void refreshChatViewBubbles(const DisplayLine *const *rows, int rowCount,
                                   const int *displayOrder, int displayCount,
                                   lv_obj_t **lastMsgObj, lv_obj_t **selectedMsgObj) {
    chatBubbleBeginRender(s_chatList);
    uint32_t lastDateBucket = 0;
    int n = 0;
    while (n < displayCount) {
        int i = displayOrder[n];

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
        while (n < displayCount) {
            int j = displayOrder[n];
            const char *t = rows[j]->text;
            if (!(t[0] == ' ' && t[1] == ' ')) break;   // not a continuation line
            const char *c = t;
            while (*c == ' ') c++;
            if (bl < sizeof(body) - 1)
                bl += (size_t)snprintf(body + bl, sizeof(body) - bl, " %s", c);
            n++;
        }

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

        chatMakeBubble(s_chatList, sender, isMe, nameTag, body,
                       rows[i]->ack,
                       replyPacketId, isSelected, lastMsgObj, selectedMsgObj,
                       onChatMessagePressed);
    }
}

// FNV-1a rolling hash over everything that changes the rendered chat: per-row
// identity/ack/text plus the global toggles that restyle every row. Used to
// skip the expensive rebuild when an incoming mesh packet (telemetry, position,
// other-channel traffic, an unchanged ACK) leaves the visible view identical —
// which is what made the heavier Bubbles style feel sluggish on busy meshes.
static uint32_t chatRenderSignature(const DisplayLine *const *rows, int rowCount) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)rowCount);
    for (int i = 0; i < rowCount; i++) {
        const DisplayLine *d = rows[i];
        mix(d->packetId);
        mix((uint32_t)d->ack);
        mix(d->senderNodeId);
        mix(d->epoch);
        for (const char *p = d->text; *p; p++) mix((uint8_t)*p);
    }
    mix(s_selectedMsgReplyPacketId);
    for (const char *p = s_selectedMsgText; *p; p++) mix((uint8_t)*p);
    mix((uint32_t)s_cfg.chatStyle);
    mix((uint32_t)s_cfg.chatNameStyle);
    // Both change every row's geometry, so a cached pass must not survive them.
    mix((uint32_t)s_cfg.fontSize);
    mix((uint32_t)s_cfg.chatSpacing);
    mix((uint32_t)s_cfg.chatColorsEnabled);
    mix((uint32_t)s_cfg.uiMode);
    mix((uint32_t)(s_pagerChatCursorMode ? 1u : 0u));
    mix((uint32_t)s_pagerChatCursorDisplayIndex);
    return h;
}

static void refreshChatView(bool force) {
    if (!s_chatPanel || !s_chatList) return;

    refreshChatComposeButtonState();

    const Channel &ch = Channels.get(s_activeChannel);

    const DisplayLine *rows[MAX_MSG_LINES] = {};
    int rowCount = 0;
    collectChatRows(rows, rowCount);

    const uint32_t sig = chatRenderSignature(rows, rowCount);
    if (!force
        && s_activeChannel == s_lastRenderedChannel
        && sig == s_lastChatSignature) {
        return;
    }

    const bool stickToBottom = force || (lv_obj_get_scroll_bottom(s_chatList) <= 6);
    const int32_t prevScrollY = lv_obj_get_scroll_y(s_chatList);

    lv_obj_clean(s_chatList);
    lv_obj_t *lastMsgObj = nullptr;
    lv_obj_t *selectedMsgObj = nullptr;

    if (rowCount == 0) {
        lv_obj_t *empty = lv_label_create(s_chatList);
        lv_obj_set_style_text_font(empty, scaledChatFont(kChannelChatFont), 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No messages yet");
    } else {
        int displayOrder[MAX_MSG_LINES] = {};
        int displayCount = 0;
        buildChatDisplayOrder(rows, rowCount, displayOrder, displayCount);
        bool useBubbleStyle = (chatStyleUsesBubbles(s_cfg.chatStyle)
                               && s_activeChannel >= 0
                               && s_activeChannel < MESH_CHANNELS);
        if (useBubbleStyle) {
            refreshChatViewBubbles(rows, rowCount, displayOrder, displayCount,
                                   &lastMsgObj, &selectedMsgObj);
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
            int n = 0;
            while (n < displayCount) {
                int i = displayOrder[n];

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
                while (n < displayCount) {
                    int j = displayOrder[n];
                    const char *t = rows[j]->text;
                    if (!(t[0] == ' ' && t[1] == ' ')) break;   // not a continuation line
                    const char *c = t;
                    while (*c == ' ') c++;
                    if (ml < sizeof(merged) - 1)
                        ml += (size_t)snprintf(merged + ml, sizeof(merged) - ml, " %s", c);
                    n++;
                }

                lv_obj_t *msg = lv_label_create(s_chatList);
                lastMsgObj = msg;
                lv_obj_set_width(msg, lv_pct(100));
                lv_obj_set_style_text_font(msg, scaledChatFont(kChannelChatFont), 0);
                lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
#if defined(DEVICE_TDECK)
                lv_obj_set_style_pad_left(msg, 1, 0);
                lv_obj_set_style_pad_right(msg, 0, 0);
#else
                lv_obj_set_style_pad_left(msg, 2, 0);
                lv_obj_set_style_pad_right(msg, 4, 0);
#endif
                lv_obj_set_style_pad_top(msg, 0, 0);
                lv_obj_set_style_pad_bottom(msg, 0, 0);
                lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

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
                const char *ackSuffix = nullptr;
                if (rows[i]->packetId) {
                    switch (rows[i]->ack) {
                        case DisplayLine::ACKED:
                            textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                            // Now that the message is one label, the marker goes
                            // at the end of it rather than mid-message.
                            ackSuffix = " [ACK]";
                            break;
                        case DisplayLine::ACKED_RELAY:
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

                lv_obj_set_style_text_color(msg, tftColorToLv(textColor565), 0);
                if (ackSuffix && ml < sizeof(merged) - 1) {
                    snprintf(merged + ml, sizeof(merged) - ml, "%s", ackSuffix);
                }
                setLabelTextEmojiSafe(msg, merged);

                uint32_t replyPacketId = resolveReplyPacketId(rows, rowCount, i);

                bool isSelected = false;
                if (s_selectedMsgReplyPacketId != 0) {
                    isSelected = (replyPacketId == s_selectedMsgReplyPacketId);
                } else {
                    isSelected = (strcmp(rows[i]->text, s_selectedMsgText) == 0);
                }
                if (isSelected) {
                    lv_obj_set_style_bg_color(msg, lv_color_hex(0x2A4E8F), 0);
                    lv_obj_set_style_bg_opa(msg, LV_OPA_70, 0);
                    selectedMsgObj = msg;
                }

                lv_obj_add_flag(msg, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(msg, onChatMessagePressed,
                                    LV_EVENT_CLICKED,
                                    (void *)(uintptr_t)replyPacketId);

                // One separator between messages, none after the last.
                if (n < displayCount) {
                    lv_obj_t *sep = lv_obj_create(s_chatList);
                    lv_obj_remove_style_all(sep);
                    lv_obj_set_size(sep, lv_pct(100), 1);
                    lv_obj_set_style_bg_color(sep, lv_color_hex(0x3F669F), 0);
                    lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
                }
            }
        }
    }

    if (s_pagerChatCursorMode) {
        if (selectedMsgObj) {
            lv_obj_scroll_to_view(selectedMsgObj, LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(s_chatList, prevScrollY, LV_ANIM_OFF);
        }
    } else if (stickToBottom) {
        if (lastMsgObj) {
            lv_obj_scroll_to_view(lastMsgObj, LV_ANIM_OFF);
        }
    } else {
        lv_obj_scroll_to_y(s_chatList, prevScrollY, LV_ANIM_OFF);
    }

    bool overflow = lv_obj_get_scroll_bottom(s_chatList) > 0;
    lv_obj_set_scrollbar_mode(s_chatList, overflow ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);

    s_lastRenderedChannel = s_activeChannel;
    s_lastRenderedCount = ch.count;
    s_lastChatSignature = sig;
}

static void buildUi() {
    lv_obj_t *screen = lv_obj_create(NULL);
    s_rootScreen = screen;
    s_channelSelectorFixedBtnW = 0;
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1E44), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *panel = nullptr;
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION)
#if defined(DEVICE_TDECK)
    const int panelMargin = 6;
    const int chatGap = 6;
    const int chatLegendH = 14;
#else
    const int panelMargin = 0;
    const int chatGap = 3;
    const int chatLegendH = 28;
#endif
    const int chatHeaderH = 25;
    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    const int chatX = panelMargin;
    const int chatW = screenW - panelMargin * 2;
    const int chatY = panelMargin + chatHeaderH + chatGap;
    const int chatH = screenH - panelMargin - chatY - chatLegendH - 3;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    const int panelMargin = 2;
    const int chatGap = 0;
    const int chatHeaderH = 22;
    const int chatLegendH = 12;
    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    const int chatX = panelMargin;
    const int chatW = screenW - panelMargin * 2;
    const int chatY = panelMargin + chatHeaderH + chatGap;
    const int chatH = screenH - panelMargin - chatY - chatLegendH - 3;
#else
    const int panelMargin = 6;
    const int panelW = 89;
    const int panelH = lv_disp_get_ver_res(NULL) - panelMargin * 2;
    const int chatGap = 6;
    const int chatHeaderH = 25;
    const int chatLegendH = 14;

    panel = lv_obj_create(screen);
    lv_obj_set_size(panel, panelW, panelH);
    lv_obj_align(panel, LV_ALIGN_LEFT_MID, panelMargin, 0);
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
    static lv_coord_t panelRows[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_style_pad_column(panel, 4, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(panel, panelCols, panelRows);
#else
    lv_obj_set_style_pad_row(panel, 5, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
#endif

    const int chatX = panelMargin + panelW + chatGap;
    const int chatW = lv_disp_get_hor_res(NULL) - chatX - panelMargin;
    const int chatY = panelMargin + chatHeaderH + chatGap;
    const int chatH = panelH - chatHeaderH - chatGap - chatLegendH - 3;
#endif

    s_chatHeaderBar = lv_obj_create(screen);
    lv_obj_set_size(s_chatHeaderBar, chatW, chatHeaderH);
    lv_obj_align(s_chatHeaderBar, LV_ALIGN_TOP_LEFT, chatX, panelMargin);
    lv_obj_clear_flag(s_chatHeaderBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(
        s_chatHeaderBar,
        (s_cfg.uiMode == UI_MODE_LIGHT) ? chatPanelBackgroundColor() : lv_color_hex(0x0E285B),
        0);
    lv_obj_set_style_bg_opa(s_chatHeaderBar, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_60 : LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatHeaderBar, 1, 0);
    lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_chatHeaderBar, 2, 0);

    const lv_font_t *headerTextFont = (chatHeaderH >= 25) ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
    // Clock gets a larger font than the other header items so the time reads at a glance.
    // Cardputer keeps the original clock size to fit its narrow header.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_font_t *clockTextFont = headerTextFont;
#else
    const lv_font_t *clockTextFont = (chatHeaderH >= 25) ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    const lv_font_t *headerIconFont = (chatHeaderH >= 25) ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
#endif
    const int headerPadX = (chatHeaderH >= 25) ? 6 : 4;
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    s_channelStrip = nullptr;
    s_channelList = nullptr;

    const int selectorBtnH = chatHeaderH - 6;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    const bool compactHeltecSelector = useCompactVerticalHeltecSelector();
    const int selectorBtnW = compactHeltecSelector
        ? max((int)lv_font_get_line_height(headerTextFont) + 4, 14)
        : min(max(chatW / 3, 96), 220);
    const int selectorBtnOffsetX = compactHeltecSelector ? 1 : headerPadX;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    const int selectorBtnW = min(max(chatW / 3, 84), 108);
    const int selectorBtnOffsetX = headerPadX;
#else
    const int selectorBtnW = min(max(chatW / 3, 96), 220);
    const int selectorBtnOffsetX = headerPadX;
#endif

#if defined(DEVICE_TDECK)
    const bool showSelectorCaret = false;
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    const bool showSelectorCaret = compactHeltecSelector;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    const bool showSelectorCaret = false;
#else
    const bool showSelectorCaret = true;
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_coord_t selectorTextYOffset = 0;
#else
    const lv_coord_t selectorTextYOffset = 1;
#endif

    const lv_font_t *selectorTextFont = headerTextFont;
#if defined(DEVICE_TDECK)
    selectorTextFont = &lv_font_montserrat_14; // nearest built-in to requested size 13
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!compactHeltecSelector) selectorTextFont = &lv_font_montserrat_14; // keep vertical Heltec unchanged
#endif

    s_channelSelectorBtn = lv_btn_create(s_chatHeaderBar);
    lv_obj_set_size(s_channelSelectorBtn, selectorBtnW, selectorBtnH);
    lv_obj_align(s_channelSelectorBtn, LV_ALIGN_LEFT_MID, selectorBtnOffsetX, 0);
    lv_obj_set_style_radius(s_channelSelectorBtn, 6, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
    lv_obj_add_event_cb(s_channelSelectorBtn, onChannelSelectorPressed, LV_EVENT_CLICKED, nullptr);

    s_channelSelectorLabel = lv_label_create(s_channelSelectorBtn);
    lv_obj_set_style_text_font(s_channelSelectorLabel, selectorTextFont, 0);
    lv_obj_set_style_text_align(s_channelSelectorLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_channelSelectorLabel, LV_LABEL_LONG_DOT);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    s_chatHeaderGps = lv_label_create(s_chatHeaderBar);
    lv_obj_align_to(s_chatHeaderGps, s_channelSelectorBtn, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_text_font(s_chatHeaderGps, headerTextFont, 0);
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
#else
    s_chatHeaderGps = nullptr;
#endif

    s_chatHeaderBattBar = lv_obj_create(s_chatHeaderBar);
    lv_obj_remove_style_all(s_chatHeaderBattBar);
    lv_obj_set_size(s_chatHeaderBattBar,
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                    6,
#else
                    (chatHeaderH >= 25) ? 8 : 7,
#endif
                    (chatHeaderH >= 25) ? 8 : 7);
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

#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
#endif
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    layoutHeaderInlineItems();
#endif

    s_chatPanel = lv_obj_create(screen);
    lv_obj_set_size(s_chatPanel, chatW, chatH);
    lv_obj_align(s_chatPanel, LV_ALIGN_TOP_LEFT, chatX, chatY);
    lv_obj_clear_flag(s_chatPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(
        s_chatPanel,
        chatPanelBackgroundColor(),
        0);
    lv_obj_set_style_bg_opa(s_chatPanel, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_chatPanel, 1, 0);
    lv_obj_set_style_border_color(s_chatPanel, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_chatPanel, 4, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_set_style_pad_row(s_chatPanel, 4, 0);
    lv_obj_set_flex_flow(s_chatPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#endif

    s_chatList = lv_obj_create(s_chatPanel);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
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
#if defined(DEVICE_TDECK)
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

#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    {
        const int dropdownW = min(max(chatW / 2, 120), 260);
        const int maxDropdownH = max(44, chatH - 8);
        const int desiredDropdownH = (kMainScreenChannelBtnHeight + 4) * MESH_CHANNELS + 8;
        const int dropdownH = min(maxDropdownH, desiredDropdownH);

        s_channelList = lv_obj_create(screen);
        lv_obj_set_size(s_channelList, dropdownW, dropdownH);
        lv_obj_align(s_channelList, LV_ALIGN_TOP_LEFT, chatX + 4, chatY + 4);
        lv_obj_add_flag(s_channelList, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_HIDDEN);
        setupVScroll(s_channelList);
        lv_obj_set_scrollbar_mode(s_channelList, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x0F2A5C), 0);
        lv_obj_set_style_bg_opa(s_channelList, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_channelList, 1, 0);
        lv_obj_set_style_border_color(s_channelList, lv_color_hex(0x335D9D), 0);
        lv_obj_set_style_radius(s_channelList, 6, 0);
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

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    s_chatNewMsgBtn = lv_btn_create(s_chatPanel);
    lv_obj_set_width(s_chatNewMsgBtn, lv_pct(100));
    lv_obj_set_height(s_chatNewMsgBtn, 28);
    lv_obj_set_style_radius(s_chatNewMsgBtn, 5, 0);
    lv_obj_set_style_pad_all(s_chatNewMsgBtn, 2, 0);
    lv_obj_set_style_shadow_width(s_chatNewMsgBtn, 0, 0);
    lv_obj_set_style_bg_color(s_chatNewMsgBtn, lv_color_hex(0x16386F), 0);
    lv_obj_set_style_bg_opa(s_chatNewMsgBtn, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatNewMsgBtn, 1, 0);
    lv_obj_set_style_border_color(s_chatNewMsgBtn, lv_color_hex(0x335D9D), 0);
    lv_obj_add_event_cb(s_chatNewMsgBtn, onChatNewMessagePressed, LV_EVENT_CLICKED, nullptr);

    s_chatNewMsgLabel = lv_label_create(s_chatNewMsgBtn);
    lv_obj_set_style_text_font(s_chatNewMsgLabel, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatNewMsgLabel, lv_color_hex(0xE8F1FF), 0);
    lv_label_set_text(s_chatNewMsgLabel, "New Message");
    lv_obj_center(s_chatNewMsgLabel);
#else
    s_chatNewMsgBtn = nullptr;
    s_chatNewMsgLabel = nullptr;
#endif

    s_chatShortcutBar = lv_obj_create(screen);
    lv_obj_set_size(s_chatShortcutBar, chatW, chatLegendH);
    lv_obj_align(s_chatShortcutBar, LV_ALIGN_TOP_LEFT, chatX, chatY + chatH + 3);
    lv_obj_clear_flag(s_chatShortcutBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_chatShortcutBar, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatShortcutBar, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_chatShortcutBar, 1, 0);
    lv_obj_set_style_border_color(s_chatShortcutBar, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(s_chatShortcutBar, 4, 0);
    lv_obj_set_style_pad_right(s_chatShortcutBar, 4, 0);
    lv_obj_set_style_pad_top(s_chatShortcutBar, 0, 0);
    lv_obj_set_style_pad_bottom(s_chatShortcutBar, 0, 0);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    s_chatShortcutText = nullptr;
    populateHeltecBottomNav(s_chatShortcutBar, -1);
#else
    s_chatShortcutText = lv_label_create(s_chatShortcutBar);
    const lv_coord_t bottomStatusReserve = (chatLegendH >= 14) ? 74 : 62;
    lv_coord_t legendTextW = chatW - bottomStatusReserve;
    if (legendTextW < 40) legendTextW = chatW;
    lv_obj_set_width(s_chatShortcutText, legendTextW);
    lv_obj_set_style_text_font(s_chatShortcutText, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatShortcutText, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_chatShortcutText, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_chatShortcutText, LV_LABEL_LONG_DOT);
    lv_obj_align(s_chatShortcutText, LV_ALIGN_LEFT_MID, 2, 0);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_label_set_text(s_chatShortcutText, "C(h)annels   (A)ctions");
#elif defined(DEVICE_TLORA_PAGER_TFT)
    lv_label_set_text(s_chatShortcutText, "(C)FG   (D)M   (N)odes   (L)ive   (A)ctions");
#else
    lv_label_set_text(s_chatShortcutText, "(C)FG   C(h)an   (D)M   (N)odes   (L)ive   (A)ct");
#endif

    s_chatHeaderGps = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatHeaderGps, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align(s_chatHeaderGps, LV_ALIGN_RIGHT_MID, -4, 0);

    s_chatHeaderWifi = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatHeaderWifi, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);

    s_chatDmAlert = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatDmAlert, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatDmAlert, lv_color_hex(0xF4D35E), 0);
    lv_label_set_text(s_chatDmAlert, LV_SYMBOL_ENVELOPE);
    lv_obj_align_to(s_chatDmAlert, s_chatHeaderWifi, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_add_flag(s_chatDmAlert, LV_OBJ_FLAG_HIDDEN);
#endif

#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_font_t *channelNavFont = kMainScreenFont;
#if defined(DEVICE_TDECK)
    channelNavFont = &lv_font_montserrat_14; // nearest built-in to requested size 13
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!useCompactVerticalHeltecSelector()) channelNavFont = &lv_font_montserrat_14; // keep vertical Heltec unchanged
#endif
#endif

    for (int i = 0; i < MESH_CHANNELS; i++) {
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION) || defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_obj_t *btn = lv_btn_create(s_channelList ? s_channelList : screen);
        s_channelBtns[i] = btn;
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_height(btn, max(kMainScreenChannelBtnHeight, 20));
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_left(btn, 4, 0);
        lv_obj_set_style_pad_right(btn, 4, 0);
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
            lv_label_set_text(lbl, "Channel");
        }
        lv_obj_center(lbl);
#else
    #if defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_obj_t *btn = lv_obj_create(s_channelList ? s_channelList : screen);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    #elif defined(DEVICE_HELTEC_V4_EXPANSION)
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
    #if defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_obj_set_size(btn, 40, kMainScreenChannelBtnHeight);
    #elif defined(DEVICE_HELTEC_V4_EXPANSION)
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
    #if defined(DEVICE_HELTEC_V4_EXPANSION)
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    #else
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    #endif

        lv_obj_t *lbl = lv_label_create(btn);
        s_channelLabels[i] = lbl;
        lv_obj_set_style_text_font(lbl, kMainScreenFont, 0);
    #if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
        lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    #else
        lv_obj_set_width(lbl, lv_pct(100));
    #endif
        lv_obj_set_height(lbl, lv_font_get_line_height(kMainScreenFont));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    #if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    #else
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    #endif
        const char *name = channelName(i);
        if (name[0]) {
            lv_label_set_text(lbl, name);
        } else {
            lv_label_set_text(lbl, "Channel");
        }
    #if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
        sizeChannelButtonToLabel(i);
    #endif
        lv_obj_center(lbl);
#endif
    }

    fitChannelDropdownToButtonContent();

    refreshChatComposeButtonState();

    int initialChannel = constrain(s_activeChannel, 0, MESH_CHANNELS - 1);
    setActiveChannel(initialChannel);
    refreshHeaderTime(true);
    refreshHeaderStatus(true);
    lv_scr_load(screen);
}

static void rebuildUiForThemeChange(bool reopenCfg) {
    int preservedChannel = constrain(s_activeChannel, 0, MESH_CHANNELS - 1);
    int preservedCfgSelection = s_cfgSelection;

    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        lv_indev_reset(indev, nullptr);
    }

    closeComposePrompt();
    closeChannelActionsModal();
    closeDmModal();
    closeLiveModal();
    closeNodesModal();
    closeLegendModal();
    closeCfgModal();

    lvObjDeleteSafe(s_rootScreen);

    memset(s_channelBtns, 0, sizeof(s_channelBtns));
    memset(s_channelLabels, 0, sizeof(s_channelLabels));
    s_channelStrip = nullptr;
    s_channelList = nullptr;
    s_channelSelectorBtn = nullptr;
    s_channelSelectorLabel = nullptr;
    s_channelSelectorCaretLabel = nullptr;
    s_channelSelectorFixedBtnW = 0;
    s_chatHeaderBar = nullptr;
    s_chatHeaderTime = nullptr;
    s_chatHeaderGps = nullptr;
    s_chatHeaderWifi = nullptr;
    s_chatDmAlert = nullptr;
    s_chatHeaderBattText = nullptr;
    s_chatHeaderBattBar = nullptr;
    s_chatPanel = nullptr;
    s_chatList = nullptr;
    s_chatNewMsgBtn = nullptr;
    s_chatNewMsgLabel = nullptr;
    s_chatShortcutBar = nullptr;
    s_chatShortcutText = nullptr;

    s_activeChannel = preservedChannel;
    buildUi();

    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        lv_indev_reset(indev, nullptr);
    }

    if (reopenCfg) {
        openCfgModal();
        if (s_cfgActionCount > 0) {
            s_cfgSelection = constrain(preservedCfgSelection, 0, s_cfgActionCount - 1);
        }
        refreshCfgModal();
    }
}

static void scheduleThemeRebuild(bool reopenCfg) {
    s_themeRebuildPending = true;
    if (reopenCfg) {
        s_themeRebuildReopenCfg = true;
        s_themeRebuildCfgSelection = s_cfgSelection;
    }
}

static void applyThemeToVisibleUi(bool reopenCfg, int reopenSelection) {
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        lv_indev_reset(indev, nullptr);
    }

    if (s_rootScreen) {
        lv_obj_set_style_bg_color(s_rootScreen, lv_color_hex(0x0B1E44), 0);
        lv_obj_set_style_bg_opa(s_rootScreen, LV_OPA_COVER, 0);
    }

    if (s_channelStrip) {
        lv_obj_set_style_bg_color(s_channelStrip, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_bg_opa(s_channelStrip, LV_OPA_60, 0);
        lv_obj_set_style_border_color(s_channelStrip, lv_color_hex(0x335D9D), 0);
    } else if (s_channelBtns[0]) {
        lv_obj_t *panel = lv_obj_get_parent(s_channelBtns[0]);
        if (panel) {
            lv_obj_set_style_bg_color(panel, lv_color_hex(0x0E285B), 0);
            lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
            lv_obj_set_style_border_color(panel, lv_color_hex(0x335D9D), 0);
        }
    }

    if (s_channelList) {
#if defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION)
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_channelList, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_channelList, lv_color_hex(0x335D9D), 0);
#endif
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    }
    if (s_channelSelectorBtn) {
        lv_obj_set_style_bg_color(s_channelSelectorBtn, lv_color_hex(0x102750), 0);
        lv_obj_set_style_border_color(s_channelSelectorBtn, lv_color_hex(0x2B4D8C), 0);
    }
    if (s_channelSelectorLabel) {
        lv_obj_set_style_text_color(
            s_channelSelectorLabel,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
            0);
    }
    if (s_channelSelectorCaretLabel) {
        lv_obj_set_style_text_color(
            s_channelSelectorCaretLabel,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? lv_color_hex(0x1B243D) : lv_color_hex(0xD9E8FF),
            0);
    }

    if (s_chatHeaderBar) {
        lv_obj_set_style_bg_color(
            s_chatHeaderBar,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? chatPanelBackgroundColor() : lv_color_hex(0x0E285B),
            0);
        lv_obj_set_style_bg_opa(s_chatHeaderBar, (s_cfg.uiMode == UI_MODE_LIGHT) ? LV_OPA_60 : LV_OPA_70, 0);
        lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_hex(0x335D9D), 0);
    }
    if (s_chatHeaderTime) lv_obj_set_style_text_color(s_chatHeaderTime, lv_color_hex(0xD9E8FF), 0);
    if (s_chatHeaderGps) lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
    if (s_chatHeaderBattText) lv_obj_set_style_text_color(s_chatHeaderBattText, lv_color_hex(0xBFD6FF), 0);
    if (s_chatHeaderBattBar) {
        lv_obj_set_style_border_color(s_chatHeaderBattBar, lv_color_hex(0x274A84), 0);
        lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerGoodGreenColor(), 0);
    }
    if (s_chatHeaderWifi) lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);

    if (s_chatPanel) {
        lv_obj_set_style_bg_color(
            s_chatPanel,
            chatPanelBackgroundColor(),
            0);
        lv_obj_set_style_border_color(s_chatPanel, lv_color_hex(0x335D9D), 0);
    }
    if (s_chatList) {
        lv_obj_set_style_bg_color(s_chatList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    }
    if (s_chatNewMsgBtn) {
        lv_obj_set_style_bg_color(s_chatNewMsgBtn, lv_color_hex(0x16386F), 0);
        lv_obj_set_style_border_color(s_chatNewMsgBtn, lv_color_hex(0x335D9D), 0);
    }
    if (s_chatShortcutBar) {
        lv_obj_set_style_bg_color(s_chatShortcutBar, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_chatShortcutBar, lv_color_hex(0x335D9D), 0);
    }
    if (s_chatShortcutText) {
        lv_obj_set_style_text_color(s_chatShortcutText, lv_color_hex(0xA7C7FF), 0);
    }

    if (s_liveModal) {
        if (lv_obj_is_valid(s_liveModal) && lv_obj_get_disp(s_liveModal) != nullptr) {
            lv_obj_set_style_bg_color(s_liveModal, lv_color_hex(0x0E285B), 0);
            lv_obj_set_style_border_color(s_liveModal, lv_color_hex(0x5C86C6), 0);
        } else {
            s_liveModal = nullptr;
        }
    }
    if (s_liveList) {
        if (lv_obj_is_valid(s_liveList)
            && lv_obj_get_disp(s_liveList) != nullptr
            && s_liveModal
            && lv_obj_is_valid(s_liveModal)
            && lv_obj_get_parent(s_liveList) == s_liveModal) {
            lv_obj_set_style_bg_color(s_liveList, liveListBackdropColor(), 0);
            lv_obj_set_style_bg_opa(s_liveList, liveListBackdropOpa(), 0);
            lv_obj_set_style_border_color(s_liveList, lv_color_hex(0x335D9D), 0);
            lv_obj_set_style_bg_color(s_liveList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
            refreshLiveView(true);
        } else {
            s_liveList = nullptr;
        }
    }

    if (s_dmModal) {
        lv_obj_set_style_bg_color(s_dmModal, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_dmModal, lv_color_hex(0x5C86C6), 0);
        refreshDmModal(true);
    }
    if (s_dmNodePickerModal) {
        lv_obj_set_style_bg_color(s_dmNodePickerModal, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_dmNodePickerModal, lv_color_hex(0x5C86C6), 0);
        refreshDmNodePicker(true);
    }

    if (s_nodesModal) {
        lv_obj_set_style_bg_color(s_nodesModal, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_nodesModal, lv_color_hex(0x5C86C6), 0);
        refreshNodesListSelection();
        refreshNodesDetails();
    }

    if (s_legendModal) {
        lv_obj_set_style_bg_color(s_legendModal, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_legendModal, lv_color_hex(0x5C86C6), 0);
    }

    if (s_cfgModal) {
        lv_obj_set_style_bg_color(s_cfgModal, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_cfgModal, lv_color_hex(0x5C86C6), 0);
        if (s_cfgActionList) {
            lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x0F2A5C), 0);
            lv_obj_set_style_border_color(s_cfgActionList, lv_color_hex(0x335D9D), 0);
            lv_obj_set_style_bg_color(s_cfgActionList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
        }
        if (reopenCfg && s_cfgActionCount > 0) {
            s_cfgSelection = constrain(reopenSelection, 0, s_cfgActionCount - 1);
        }
        refreshCfgModal();
    } else if (reopenCfg) {
        openCfgModal();
        if (s_cfgActionCount > 0) {
            s_cfgSelection = constrain(reopenSelection, 0, s_cfgActionCount - 1);
        }
        refreshCfgModal();
    }

    s_lastRenderedChannel = -1;
    s_lastRenderedCount = -1;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
    s_lastHeaderTime[0] = '\0';
    setActiveChannel(constrain(s_activeChannel, 0, MESH_CHANNELS - 1));
    refreshHeaderTime(true);
    refreshHeaderStatus(true);
    refreshChatView(true);

    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev;
         indev = lv_indev_get_next(indev)) {
        lv_indev_reset(indev, nullptr);
    }
}

static void processPendingThemeRebuild() {
    if (!s_themeRebuildPending) return;

    bool reopenCfg = s_themeRebuildReopenCfg;
    int reopenSelection = s_themeRebuildCfgSelection;

    s_themeRebuildPending = false;
    s_themeRebuildReopenCfg = false;

    applyThemeToVisibleUi(reopenCfg, reopenSelection);
}

// In-place normalise a serial CLI line: trim whitespace, collapse runs of
// spaces, and lowercase the command so dispatch can use plain strcmp().
static void normalizeSerialCommand(char *line) {
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
        line[out++] = (char)tolower(c);
    }
    line[out] = '\0';

    if (out > 0 && line[out - 1] == ' ') {
        line[out - 1] = '\0';
    }
}

// Dispatch a single normalised CLI command. Aliases are intentional so the
// common shorthand a developer might type (`scan`, `i2c`) maps to the same
// action as the canonical command.
static void handleSerialCommandLine(char *line) {
    normalizeSerialCommand(line);
    if (!line || !line[0]) return;

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        Serial.println("[cli] commands: help | env | env scan | env scan all | telemetry now | announce now");
        return;
    }

    if (strcmp(line, "env") == 0 || strcmp(line, "env status") == 0) {
        bool hasSensor = envHasSensor() || envBegin();
        Serial.printf("[cli] env status: %s\n", hasSensor ? envSensorName() : "none");
        if (hasSensor) {
            EnvReading env = {};
            if (envRead(env)) {
                Serial.printf("[cli] env sample: T=%.2fC H=%.2f%% P=%.2fhPa\n",
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
        Serial.println("[cli] env scan requested");
        bool ok = envDebugScan(true);
        Serial.printf("[cli] env scan result: %s\n", ok ? envSensorName() : "none");
        return;
    }

    if (strcmp(line, "env scan all") == 0
        || strcmp(line, "scan all") == 0
        || strcmp(line, "i2c scan all") == 0) {
        Serial.println("[cli] full i2c scan requested");
        bool ok = envDebugFullScan(true);
        Serial.printf("[cli] full i2c scan result: %s\n", ok ? envSensorName() : "none");
        return;
    }

    if (strcmp(line, "telemetry") == 0 || strcmp(line, "telemetry now") == 0) {
        webCfgQueueTelemetry();
        Serial.println("[cli] telemetry queued");
        return;
    }

    if (strcmp(line, "announce") == 0 || strcmp(line, "announce now") == 0) {
        webCfgQueueAnnounce();
        Serial.println("[cli] announce queued");
        return;
    }

    Serial.printf("[cli] unknown command: %s\n", line);
    Serial.println("[cli] try: help");
}

// Drain pending bytes from the USB CDC serial port and dispatch any
// complete newline-terminated command lines. Called once per loop().
static void serviceSerialCommands() {
    while (Serial.available() > 0) {
        int raw = Serial.read();
        if (raw < 0) break;

        char c = (char)raw;
        if (c == '\r') continue;

        if (c == '\n') {
            if (s_serialCmdLen > 0) {
                s_serialCmdBuf[s_serialCmdLen] = '\0';
                handleSerialCommandLine(s_serialCmdBuf);
                s_serialCmdLen = 0;
                s_serialCmdBuf[0] = '\0';
            }
            continue;
        }

        if (s_serialCmdLen + 1 < sizeof(s_serialCmdBuf)) {
            s_serialCmdBuf[s_serialCmdLen++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(120);

    // Match baseline firmware board-power bring-up so keyboard/touch I2C devices are powered.
#if (BOARD_POWERON >= 0)
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
#endif
#if (BOARD_VEXT_ENABLE >= 0)
    pinMode(BOARD_VEXT_ENABLE, OUTPUT);
    digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
    delay(20);
#endif
#if defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)
    pinMode(USER_BUTTON_PIN,
            (USER_BUTTON_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
#if defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)
    pinMode(DISPLAY_TOGGLE_BUTTON_PIN,
            (DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif

    // Explicitly disable OTA-only TLS networking on every boot before any
    // runtime subsystems start. OTA worker flow enables it only when needed.
    otaSetNetworkAllowed(false);

    bool bootOtaRtc = isOtaWorkerModeRequestedRtc();
    bool bootOtaNvs = isOtaWorkerModeRequestedOnce();
    Serial.printf("[ota-worker] boot flags (rtc=%d nvs=%d fw=%s)\n",
                  bootOtaRtc ? 1 : 0,
                  bootOtaNvs ? 1 : 0,
                  APP_VERSION);

#if defined(DEVICE_TLORA_PAGER_TFT)
    // Pager-specific memory guard: run OTA worker before display/keyboard init
    // so TLS gets the largest contiguous internal heap.
    s_otaWorkerUiReady = false;
    {
        bool rtcPending = isOtaWorkerModeRequestedRtc();
        bool nvsPending = isOtaWorkerModeRequestedOnce();
        Serial.printf("[ota-worker] pre-display flags (rtc=%d nvs=%d)\n",
                      rtcPending ? 1 : 0,
                      nvsPending ? 1 : 0);
        if (rtcPending || nvsPending) {
            Serial.printf("[ota-worker] pending before display init (rtc=%d nvs=%d)\n",
                          rtcPending ? 1 : 0,
                          nvsPending ? 1 : 0);
            loadConfigForOtaWorker();
            if (runOtaWorkerModeIfRequested()) {
                Serial.println("[ota-worker] minimal flow finished pre-display init");
            } else {
                Serial.println("[ota-worker] requested pre-display init but could not start");
            }
        }
    }
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer display is owned by M5Cardputer.Display after keyboard begin.
    s_keyboard.begin();
    displayDev().setRotation(TFT_ROTATION_DEFAULT);
    displayDev().setBrightness(TFT_BRIGHTNESS_DEFAULT);
    displayDev().fillScreen(TFT_BLACK);
#else
    lcd.init();
    displayDev().setRotation(TFT_ROTATION_DEFAULT);
    displayDev().setBrightness(TFT_BRIGHTNESS_DEFAULT);
    displayDev().fillScreen(TFT_BLACK);
    s_keyboard.begin();
#endif
    setPagerKeyboardBacklight(true);
    s_otaWorkerUiReady = true;

    // Keep OTA worker boot as lean as possible: avoid LVGL/splash/radio/UI setup
    // so HTTPS has maximum contiguous internal heap for TLS.
    bool rtcPending = isOtaWorkerModeRequestedRtc();
    bool nvsPending = isOtaWorkerModeRequestedOnce();
    Serial.printf("[ota-worker] pre-lvgl flags (rtc=%d nvs=%d)\n",
                  rtcPending ? 1 : 0,
                  nvsPending ? 1 : 0);
    if (rtcPending || nvsPending) {
        Serial.printf("[ota-worker] pending before init (rtc=%d nvs=%d)\n",
                      rtcPending ? 1 : 0,
                      nvsPending ? 1 : 0);
        loadConfigForOtaWorker();
        if (runOtaWorkerModeIfRequested()) {
            Serial.println("[ota-worker] minimal flow finished; continuing normal boot");
        } else {
            Serial.println("[ota-worker] requested but could not start, continuing normal boot");
        }
    }

    lv_init();
    emojiFontInit();   // build emoji-fallback text faces before any UI
    nodesMapInitFsDriver();
    // Allocate the LVGL draw buffer here, on the normal-UI path only. The OTA
    // worker path returns/reboots before reaching this point, so the buffer is
    // never allocated during a firmware update — leaving the full contiguous
    // internal heap for the TLS handshake. Prefer internal RAM (fast to render
    // and flush); fall back to PSRAM, then a minimal buffer, rather than crash.
    const size_t kDrawBufPx = (size_t)kMaxHorRes * (size_t)kDrawBufLines;
    s_drawBufMem = (lv_color_t *)heap_caps_malloc(kDrawBufPx * sizeof(lv_color_t),
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t drawBufPx = kDrawBufPx;
    if (!s_drawBufMem) {
        Serial.println("[lvgl] draw buffer internal alloc failed; trying PSRAM");
        s_drawBufMem = (lv_color_t *)heap_caps_malloc(kDrawBufPx * sizeof(lv_color_t),
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_drawBufMem) {
        // Last resort: a small internal buffer keeps the UI alive (stripey but
        // functional) instead of a null-buffer crash.
        drawBufPx = (size_t)kMaxHorRes * 8u;
        s_drawBufMem = (lv_color_t *)heap_caps_malloc(drawBufPx * sizeof(lv_color_t),
                                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        Serial.printf("[lvgl] FATAL-ish: fell back to %u-line draw buffer\n",
                      (unsigned)(drawBufPx / kMaxHorRes));
    }
    lv_disp_draw_buf_init(&s_drawBuf, s_drawBufMem, nullptr, drawBufPx);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    int32_t dispW = displayDev().width();
    int32_t dispH = displayDev().height();
    if (dispW <= 0 || dispH <= 0) {
        dispW = DEVICE_LCD_LANDSCAPE_W;
        dispH = DEVICE_LCD_LANDSCAPE_H;
        Serial.printf("[lvgl] WARNING: invalid lcd size, fallback to %ldx%ld\n",
                      (long)dispW, (long)dispH);
    }
    dispDrv.hor_res = dispW;
    dispDrv.ver_res = dispH;
    dispDrv.flush_cb = lvglFlush;
    dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&dispDrv);

#if HAS_TOUCH
    static lv_indev_drv_t touchDrv;
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = lvglTouchRead;
    lv_indev_drv_register(&touchDrv);
#endif

    loadConfigFromSd();
    drawBootSplash();
    playSplashStartupRiff();
    recomputeChannelHashes();
    deriveNodeId();
    mqttBridgeBegin(&s_cfg, s_myNodeId);
    mqttBridgeSetInject(mqttDownlinkInject);
    syncWifiCredsToPrefs();
    applyTimezoneFromConfig();
    bootTimeNtpSync();
    const bool otaWorkerHandled = runOtaWorkerModeIfRequested();
    const bool skipWebAutoStartOnce = consumeSkipWebAutoStartOnce();
    if (skipWebAutoStartOnce || otaWorkerHandled) {
        Serial.println("[web] auto start skipped once for OTA low-memory retry");
    }
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    // On PSRAM boards the chat buffers live in PSRAM, so starting web config
    // here (before Channels.init()) costs nothing. The no-PSRAM Cardputer must
    // wait until those DRAM buffers exist so webCfgBegin()'s reclaim can free
    // them for Wi-Fi — it starts at the end of setup() instead. Starting here
    // would reclaim 0 bytes and then get starved when the buffers allocate.
    if (!(skipWebAutoStartOnce || otaWorkerHandled)) startWebConfigAuto();
#endif
    bootstrapStateMapsIfMissing();
    batteryInitAdc();
    gpsSetEnabled(s_cfg.gpsEnabled);
    Nodes.init();
    DMs.init();
    Ignored.init();
    Channels.init();
    Channels.beginPersistence();
    Channels.loadPersisted();
    syncPrimaryChannelName();
    recomputeChannelHashes();
    s_radioReady = Radio.init();
#if defined(LORA_TEST_MQTT_ONLY) && LORA_TEST_MQTT_ONLY
    Serial.println("[radio] *** LORA_TEST_MQTT_ONLY: LoRa TX/RX DISABLED (MQTT-only test build) ***");
    Channels.addMessage(0, "", "[TEST] LoRa disabled - MQTT only", TFT_ORANGE);
#endif
    if (!s_radioReady) {
        Channels.addMessage(0, "", "[radio] init failed", TFT_RED);
    } else {
        // init() uses compile-time defaults; apply the loaded config values.
        if (fabsf(s_cfg.loraFreq - MESH_FREQ) > 0.001f ||
            fabsf(s_cfg.loraBw   - MESH_BW)   > 0.001f ||
            s_cfg.loraSf    != MESH_SF    ||
            s_cfg.loraCr    != MESH_CR    ||
            s_cfg.loraPower != MESH_POWER) {
            Radio.reconfigure(s_cfg.loraFreq, s_cfg.loraBw,
                              s_cfg.loraSf, s_cfg.loraCr, s_cfg.loraPower);
        }
    }

    buildUi();
    if (s_otaWorkerBootNotice[0]) {
        openCfgActionMessageModal(s_otaWorkerBootNotice);
        s_otaWorkerBootNotice[0] = '\0';
    }
    s_lastActivityMs = millis();
#if defined(DEVICE_HELTEC_V4_EXPANSION) && !DEVICE_UI_VERTICAL
    // Non-vertical Heltec has a wide/short layout where the onboarding modal's
    // multi-line text is awkward to read. Skip onboarding on this build for
    // now — the user can still configure via web config / SD import.
    if (s_firstBoot) {
        Serial.println("[onboarding] skipped (heltec non-vertical build)");
        s_firstBoot = false;
    }
#else
    if (s_firstBoot) {
        Serial.println("[onboarding] first boot detected - showing setup modal");
        openOnboardingModal();
    }
#endif
    Serial.printf("[lvgl-poc] started (%dx%d)\\n", displayDev().width(), displayDev().height());
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer (no PSRAM): auto-start web config only now that the node DB,
    // chat/DM buffers, radio, and UI are all allocated — this mirrors the
    // healthy manual-enable path, so webCfgBegin()'s reclaim frees the ~35 KB
    // of chat buffers and Wi-Fi keeps enough DRAM to serve pages.
    if (!(skipWebAutoStartOnce || otaWorkerHandled)) startWebConfigAuto();
#endif
}

// ── Light-sleep power management (opt-in via isPowerSaving) ───────────────────
// While the screen is asleep we duty-cycle the CPU with short light-sleep naps
// instead of busy-waiting. The SX1262 stays in RX and wakes us instantly via its
// DIO1 line, so messages are still received; otherwise a ~200 ms timer wake keeps
// input polling and scheduled TX responsive. lsSecs/minWakeSecs are reserved for
// a future deeper-sleep tier and intentionally unused here.
static bool powerSaveShouldNap() {
    return s_cfg.isPowerSaving
        && s_screenAsleep                 // only after screen-off inactivity
        && !webCfgRunning()               // never while the web-config server is up
        && WiFi.getMode() == WIFI_OFF;    // light sleep + active Wi-Fi don't mix
}

static void enterLightNap() {
    static constexpr uint32_t kNapMs = 200;   // input latency ceiling while asleep
#if defined(LORA_DIO1) && (LORA_DIO1 >= 0)
    // SX1262 holds DIO1 high on RX-done → wake immediately on an incoming packet.
    gpio_wakeup_enable((gpio_num_t)LORA_DIO1, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
#endif
    esp_sleep_enable_timer_wakeup((uint64_t)kNapMs * 1000ULL);
    esp_light_sleep_start();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO && s_radioReady) {
        Radio.wakeRxCheck();              // service the packet on the next poll
    }
}

// Boot update check. Runs at most once per boot, and only once WiFi has been up
// long enough to be usable — a check fired the instant the station associates
// tends to hit DNS before it is ready. Failures are not retried: an offline or
// unreachable device should not spend the rest of its uptime probing, and the
// next reboot will try again.
static uint32_t s_otaAutoCheckDueMs = 0;
static void serviceOtaAutoCheck(uint32_t nowMs) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // OTA is disabled on this build (see CFG_ACTION_OTA_UPDATE), so there is
    // nothing to offer.
    LV_UNUSED(nowMs);
#else
    if (s_otaAutoCheckDone || !s_cfg.otaAutoCheckEnabled) return;
    if (!s_cfg.wifiEnabled || WiFi.status() != WL_CONNECTED) {
        s_otaAutoCheckDueMs = 0;    // restart the settle timer on reconnect
        return;
    }
    // Don't interrupt onboarding or an open dialog — and don't burn the single
    // per-boot attempt before there is a screen to show the answer on.
    if (!s_rootScreen || s_onboardingModal || s_cfgConfirmModal || s_otaPromptModal) return;

    if (s_otaAutoCheckDueMs == 0) {
        s_otaAutoCheckDueMs = nowMs + kOtaAutoCheckSettleMs;
        return;
    }
    if ((int32_t)(nowMs - s_otaAutoCheckDueMs) < 0) return;

    s_otaAutoCheckDone = true;
    Serial.println("[ota-check] checking for a newer release");

    // The gate is what keeps OTA networking out of normal operation; open it
    // only for the duration of this one request.
    otaSetNetworkAllowed(true);
    OtaCheckResult check = {};
    const bool ok = otaCheckLatestRelease(check) && check.ok;
    otaSetNetworkAllowed(false);

    if (!ok) {
        Serial.printf("[ota-check] failed: %s\n", check.error[0] ? check.error : "unknown");
        return;
    }
    if (!check.updateAvailable) {
        Serial.printf("[ota-check] up to date (%s)\n",
                      check.latestTag[0] ? check.latestTag : APP_VERSION);
        return;
    }

    utf8util::copyTruncate(s_otaAutoCheckTag, sizeof(s_otaAutoCheckTag), check.latestTag);
    Serial.printf("[ota-check] update available: %s -> %s\n", APP_VERSION, s_otaAutoCheckTag);
    openOtaUpdatePrompt();
#endif
}

void loop() {
    s_cfgDebugLog = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;

    uint32_t now = millis();

    // Sample loop-iteration rate once per second as a lightweight CPU-activity
    // proxy for the hidden system-stats screen (no direct CPU-load counter
    // exists under Arduino). Counted before the early returns below so every
    // iteration is included.
    s_loopIterations++;
    if (s_loopRateAnchorMs == 0) s_loopRateAnchorMs = now;
    if ((uint32_t)(now - s_loopRateAnchorMs) >= 1000UL) {
        s_loopsPerSec = s_loopIterations - s_loopRateAnchorCount;
        s_loopRateAnchorCount = s_loopIterations;
        s_loopRateAnchorMs = now;
    }

    if (serviceTdeckTrackballSleepHold(now)) {
        delay(5);
        return;
    }
    if (pollUserButton(now)) {
        delay(5);
        return;
    }

    serviceSerialCommands();
    bootstrapStateMapsIfMissing();
    pumpKeyboardInput();
    processPendingThemeRebuild();
    lv_timer_handler();
    if (webCfgRunning()) {
        webCfgLoop();
        serviceWebChatSend();
    }
    if (s_sysStatsModal) refreshSysStatsModal(false);
    bool meshChanged = false;
    if (s_radioReady) {
        meshChanged = pollMeshRx();
        servicePendingRebroadcast(now);
    }
    serviceWifiStation(now);
    serviceOtaAutoCheck(now);
    mqttBridgeLoop(now);
    if (s_mqttDownlinkUiDirty) { meshChanged = true; s_mqttDownlinkUiDirty = false; }
    gpsLoop();
    // Periodically copy live GPS fix into s_cfg so it's available as a
    // "last known position" fallback when GPS is off or has lost lock.
    if (gpsIsEnabled() && gpsHasFix()) {
        uint32_t intervalMs = s_cfg.gpsPollIntervalS > 0
            ? s_cfg.gpsPollIntervalS * 1000UL : 60000UL;
        if ((uint32_t)(now - s_lastGpsSampleMs) >= intervalMs) {
            s_cfg.latI = gpsLatI();
            s_cfg.lonI = gpsLonI();
            s_cfg.alt  = (int32_t)gpsAltM();
            s_lastGpsSampleMs = now;
        }
    }
    serviceNodeInfoAnnounce(now);
    serviceTelemetryAnnounce(now);
    serviceNeighborInfoAnnounce(now);
    serviceAutoFavorite(now);
    // Append any nodes evicted from the full node table to the SD archive.
    // Placed before the screen-sleep return below so archiving keeps working
    // with the display off. No-op unless an eviction actually queued something.
    // Mirror the user preference into node_db each pass so it can never drift
    // from config (web save, YAML import, and factory reset all land here).
    nodeArchiveSetEnabled(s_cfg.nodeArchiveEnabled);
    nodeArchiveFlush();

    now = millis();
    if (!s_screenAsleep && s_cfg.screenOnSecs > 0
        && (uint32_t)(now - s_lastActivityMs) > (uint32_t)s_cfg.screenOnSecs * 1000UL) {
        Serial.printf("[screen] sleeping (idle %lus, timeout %us)\n",
                      (unsigned long)((now - s_lastActivityMs) / 1000UL),
                      (unsigned)s_cfg.screenOnSecs);
        sleepScreen("timeout");
    }

    if (s_screenAsleep) {
        if (powerSaveShouldNap()) enterLightNap();
        else delay(5);
        return;   // loop re-enters and polls input/RX/announces on wake
    }

    refreshChannelGlow(false);
    refreshHeaderTime(false);
    refreshHeaderStatus(false);
    refreshDmAlertIndicator();
    // Not forced: the content signature inside refreshChatView decides whether a
    // rebuild is actually needed, so mesh packets that don't change the visible
    // chat no longer trigger a full (bubble) teardown/rebuild every time.
    refreshChatView(false);
    refreshLiveView(meshChanged);
    refreshChUtilChart(meshChanged);
    refreshSnrRssiChart(meshChanged);
    refreshDmModal(meshChanged);
    delay(5);
}

#endif  // UI_LVGL_POC
