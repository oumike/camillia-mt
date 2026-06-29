#if defined(UI_LVGL_POC)

#include <Arduino.h>
#include "config.h"
#include "channel_mgr.h"
#include "config_io.h"
#include "hal/display.h"
#include "live_util.h"
#include "mesh_proto.h"
#include "mesh_radio.h"
#include "node_db.h"
#include "dm_mgr.h"
#include "battery_util.h"
#include "env_sensor.h"
#include "gps.h"
#include "keyboard.h"
#include "web_config.h"
#include "debug_flags.h"
#include "utf8_utils.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <SD.h>
#include <Curve25519.h>
#if defined(DEVICE_TLORA_PAGER_TFT)
#include <AudioBoard.h>
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
#include <driver/i2s.h>
#endif
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
static constexpr uint16_t kDrawBufLines = 40;
static lv_color_t s_drawBufMem[kMaxHorRes * kDrawBufLines];
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
static lv_obj_t *s_cfgModal = nullptr;
static lv_obj_t *s_cfgActionList = nullptr;
static lv_obj_t *s_cfgInfoList = nullptr;
static lv_obj_t *s_cfgHeaderStatus = nullptr;
// Yes/No confirmation dialog layered over the CFG modal for destructive actions.
static lv_obj_t *s_cfgConfirmBackdrop = nullptr;
static lv_obj_t *s_cfgConfirmModal = nullptr;
static int s_cfgConfirmPendingAction = -1;
static lv_obj_t *s_legendModal = nullptr;
#if !defined(DEVICE_TLORA_PAGER_TFT)
// (I)nformation popup over the CFG modal — pager shows this in a side panel.
static lv_obj_t *s_nodeInfoModal = nullptr;
#endif
static lv_obj_t *s_liveModal = nullptr;
static lv_obj_t *s_liveList = nullptr;

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
static NodeEntry s_dmNodeSnapshot[MAX_NODES] = {};
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
static NodeEntry s_nodesSnapshot[MAX_NODES] = {};
static int s_nodesFilteredIdx[MAX_NODES] = {};
static int s_nodesSnapshotCount = 0;
static int s_nodesFilteredCount = 0;
static int s_nodesSelected = -1;
static constexpr int kNodesFilterMax = 24;
static char s_nodesFilter[kNodesFilterMax + 1] = {};
static int s_nodesFilterLen = 0;
static bool s_nodesFilterOpen = false;
static constexpr int kNodesActionCount = 5;
static lv_obj_t *s_nodesActionModal = nullptr;
static lv_obj_t *s_nodesActionRows[kNodesActionCount] = {};
static int s_nodesActionSelection = 0;
static uint32_t s_nodesActionNodeId = 0;
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
static int s_lastRenderedLiveCount = -1;
static int s_lastRenderedLiveScrollOff = -1;
static int s_cfgSelection = 0;
static int s_cfgActionCount = 0;
static int s_cfgActions[20] = {};
static char s_cfgStatus[96] = "";
static int s_cfgConfirmAction = -1;
static uint32_t s_cfgConfirmMs = 0;
static uint32_t s_cfgLastActivateMs = 0;
static uint32_t s_cfgLastScrollMs = 0;
static uint32_t s_cfgEnterLockUntilMs = 0;
static bool s_cfgAwaitEnterRelease = false;
static bool s_cfgInfoPanelFocused = false;
static bool s_cfgDebugLog = (MY_DEBUG_MONITOR != 0);
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
static bool s_pagerChatCursorMode = false;
static int s_pagerChatCursorDisplayIndex = -1;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static bool s_cardputerMainChatPanelFocused = false;
static int s_cardputerDropdownSelection = -1;
#endif

static constexpr int kRxDedupSize = 32;
struct SeenPkt {
    uint32_t from;
    uint32_t id;
};
static SeenPkt s_seenPkts[kRxDedupSize] = {};
static int s_seenHead = 0;

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
static void refreshHeaderTime(bool force = false);
static void refreshHeaderStatus(bool force = false);
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
static void refreshChatComposeButtonState();
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
static void onCfgActionRowPressed(lv_event_t *e);
#if !defined(DEVICE_TLORA_PAGER_TFT)
static void openNodeInfoModal();
static void closeNodeInfoModal();
#endif
static void openLegendModal();
static void closeLegendModal();
static void onLegendClosePressed(lv_event_t *e);
static void openLiveModal();
static void closeLiveModal();
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
static void activateDmSelection();
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
static void applyTimezoneFromConfig();
static void syncWifiCredsToPrefs();
static void persistWebCfgEnabled();
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

        const char *alias = emojiAliasForCodepoint(cp);
        if (alias) {
            writePos = appendTextLiteral(dst, dstLen, writePos, alias);
            i += n;
            continue;
        }

        // Keep unknown emoji codepoints as-is so we don't force generic tokens.
        // If the current font cannot draw them, LVGL will still show its fallback glyph.

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
    CFG_ACTION_GPS_TOGGLE,
    CFG_ACTION_EXPORT,
    CFG_ACTION_IMPORT,
    CFG_ACTION_THEME,
    CFG_ACTION_UNITS,
    CFG_ACTION_ANNOUNCE,
    CFG_ACTION_TELEMETRY,
    CFG_ACTION_NEIGHBOR_INFO,
    CFG_ACTION_SNF_CLIENT,
    CFG_ACTION_MSG_ALERT,
    CFG_ACTION_SPLASH_MELODY,
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
    sPagerAudioBoard.setMute(false);

    if (!pagerAudioInitI2S()) {
        sPagerAudioReady = false;
        return false;
    }

    sPagerAudioReady = true;
    Serial.println("[audio] pager codec/i2s ready");
    return true;
}

static inline void pagerAudioStartPlayback() {
    sPagerAudioBoard.setMute(false);
    pagerAudioApplyVolume(kPagerAudioVolActive);
    delay(2);
    i2s_zero_dma_buffer(kPagerI2SPort);
    // Prime codec/I2S with a short silent pre-roll so the first note isn't clipped.
    int16_t preRoll[256] = {0};
    size_t preRollWritten = 0;
    (void)i2s_write(kPagerI2SPort, preRoll, sizeof(preRoll),
                    &preRollWritten, 10 / portTICK_PERIOD_MS);
}

static inline void pagerAudioStopPlayback() {
    // Push a short silence tail before ending to reduce stop pops.
    int16_t tail[1024] = {0};
    size_t tailWritten = 0;
    (void)i2s_write(kPagerI2SPort, tail, sizeof(tail), &tailWritten, 20 / portTICK_PERIOD_MS);
    i2s_zero_dma_buffer(kPagerI2SPort);
    pagerAudioApplyVolume(kPagerAudioVolIdle);
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

static const char *cfgActionLabel(int actionId, char *buf, size_t bufLen) {
    if (!buf || bufLen == 0) return "";
    switch (actionId) {
        case CFG_ACTION_WEBCFG:
            if (!s_webCfgEnabled) {
                snprintf(buf, bufLen, "Web Config: Disabled");
            } else if (webCfgRunning()) {
                snprintf(buf, bufLen, "Web Config: On (%s)", webCfgIP());
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
        case CFG_ACTION_UNITS:
            snprintf(buf, bufLen, "Units: %s", s_cfg.displayUnits ? "Imperial" : "Metric");
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
        case CFG_ACTION_MSG_ALERT:
            snprintf(buf, bufLen, "Notification Sound: %s", msgAlertSoundName(s_cfg.msgAlertSound));
            break;
        case CFG_ACTION_SPLASH_MELODY:
            snprintf(buf, bufLen, "Splash Melody: %s", s_cfg.splashMelodyEnabled ? "On" : "Off");
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
    p.putBool("compassNorth", s_cfg.compassNorthTop);
    p.putBool("flipScreen", s_cfg.flipScreen);
    p.putBool("splashMelody", s_cfg.splashMelodyEnabled);
    p.putUChar("msgAlertSound", s_cfg.msgAlertSound);
    p.putUChar("uiTheme", s_cfg.uiTheme);
    p.putUChar("uiMode", s_cfg.uiMode);
    p.putBool("btEnabled", s_cfg.btEnabled);
    p.putUChar("btMode", s_cfg.btMode);
    p.putULong("btFixedPin", s_cfg.btFixedPin);
    p.putBool("mqttEnabled", s_cfg.mqttEnabled);
    p.putString("mqttServer", s_cfg.mqttServer);
    p.putString("mqttUser", s_cfg.mqttUser);
    p.putString("mqttPass", s_cfg.mqttPass);
    p.putString("mqttRoot", s_cfg.mqttRoot);
    p.putBool("mqttEncrypt", s_cfg.mqttEncryption);
    p.putBool("mqttMapRpt", s_cfg.mqttMapReport);
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
    p.putULong("nodeIdOvr", s_cfg.nodeIdOverride);
    p.putUChar("chatSpace", s_cfg.chatSpacing);
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
    }

    cp.end();
}

static void loadConfigFromPrefs() {
    Preferences prefs;
    if (!prefs.begin("camillia", true)) return;

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
    if (ro != 0xFF) s_cfg.deviceRole = ro;
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
    String canned = getStringIfKey("cannedMsgs");
    if (canned.length()) {
        strncpy(s_cfg.cannedMessages, canned.c_str(), sizeof(s_cfg.cannedMessages) - 1);
        s_cfg.cannedMessages[sizeof(s_cfg.cannedMessages) - 1] = '\0';
    }

    ul = prefs.getULong("nodeIdOvr", 0);
    if (ul) s_cfg.nodeIdOverride = (uint32_t)ul;
    ro = prefs.getUChar("chatSpace", 0xFF);
    if (ro != 0xFF && ro <= 2) s_cfg.chatSpacing = ro;

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
    s_webCfgEnabled = prefs.getBool("webCfgEnabled", false);

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

static bool pagerSelectChatCursorIndex(int displayIndex) {
    const DisplayLine *rows[MAX_MSG_LINES] = {};
    int rowCount = 0;
    collectChatRows(rows, rowCount);

    int displayOrder[MAX_MSG_LINES] = {};
    int displayCount = 0;
    buildChatDisplayOrder(rows, rowCount, displayOrder, displayCount);

    if (displayCount <= 0) {
        s_pagerChatCursorDisplayIndex = -1;
        s_selectedMsgReplyPacketId = 0;
        s_selectedMsgText[0] = '\0';
        s_lastRenderedChannel = -1;
        return false;
    }

    if (displayIndex < 0) displayIndex = displayCount - 1;
    if (displayIndex >= displayCount) displayIndex = displayCount - 1;

    s_pagerChatCursorDisplayIndex = displayIndex;
    int rowIdx = displayOrder[displayIndex];
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
    if (s_composeModal) {
        lv_obj_del(s_composeModal);
    }
    s_composeModal = nullptr;
    s_composeInput = nullptr;
    s_composeKeyboard = nullptr;
    s_composeTarget = COMPOSE_TARGET_CHANNEL;
    s_composeDmNodeId = 0;
    s_composeReplyPacketId = 0;
    s_composeChannelIdx = s_activeChannel;
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
    const lv_font_t *composeBodyFont = &lv_font_montserrat_14;
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 3) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_TDECK)
    const lv_font_t *composeBodyFont = &lv_font_montserrat_12;
    const lv_coord_t composeInputH = (lv_coord_t)((lv_font_get_line_height(composeBodyFont) * 3) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    const lv_font_t *composeBodyFont = &lv_font_montserrat_12;
    const lv_coord_t composeInputH = (lv_coord_t)(lv_font_get_line_height(composeBodyFont) + 8);
    const lv_coord_t composeInputPadTop = max<lv_coord_t>(1, (composeInputH - (lv_coord_t)lv_font_get_line_height(composeBodyFont)) / 2);
    const lv_coord_t composeModalBottomPad = 4;
    const lv_coord_t composeModalRowPad = 1;
#else
    const lv_font_t *composeBodyFont = &lv_font_montserrat_12;
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
        lv_obj_set_style_text_font(replyLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(replyLbl, lv_color_hex(0xA7C7FF), 0);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        setLabelTextEmojiSafe(replyLbl, preview[0] ? preview : "(message)");
    }

    s_composeInput = lv_textarea_create(s_composeModal);
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_height(s_composeInput, 44);
    lv_obj_set_style_text_font(s_composeInput, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_composeInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_composeInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_composeInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeInput, 1, 0);
    lv_obj_set_style_border_color(s_composeInput, lv_color_hex(0x4C76BA), 0);
    lv_textarea_set_one_line(s_composeInput, true);
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, "Type message...");

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
#endif
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
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEBCFG;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_GPS_TOGGLE;
#if HAS_SD_CARD && !defined(DEVICE_HELTEC_V4_EXPANSION)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_EXPORT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_IMPORT;
#endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_THEME;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_UNITS;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_ANNOUNCE;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_TELEMETRY;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_NEIGHBOR_INFO;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SNF_CLIENT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_MSG_ALERT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_SPLASH_MELODY;
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
static int buildDeviceInfoLines(char info[][96], int maxLines) {
    int n = 0;
    bool hasPubKey = false;
    for (int i = 0; i < 32; i++) {
        if (myPubKey[i] != 0) { hasPubKey = true; break; }
    }
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

    lv_label_set_text(s_cfgHeaderStatus, s_cfgStatus[0] ? s_cfgStatus : "Ready");

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
        char rowText[80];

        lv_obj_t *row = lv_label_create(s_cfgActionList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, cfgRowFont, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, cfgPadTop, 0);
        lv_obj_set_style_pad_bottom(row, cfgPadBottom, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_outline_width(row, 0, 0);
        lv_obj_set_style_outline_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row,
                    onCfgActionRowPressed,
                    LV_EVENT_CLICKED,
                    (void *)(intptr_t)i);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, cfgActionLabel(actionId, rowText, sizeof(rowText)));

        if (i == s_cfgSelection) {
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
    static constexpr int kCfgInfoMaxLines = 10;
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

static void closeCfgModal() {
    closeCfgConfirmModal();
#if !defined(DEVICE_TLORA_PAGER_TFT)
    closeNodeInfoModal();
#endif
    if (s_cfgModal) {
        lv_obj_del(s_cfgModal);
    }
    s_cfgModal = nullptr;
    s_cfgActionList = nullptr;
    s_cfgInfoList = nullptr;
    s_cfgHeaderStatus = nullptr;
    s_cfgAwaitEnterRelease = false;
    s_cfgInfoPanelFocused = false;
}

#if !defined(DEVICE_TLORA_PAGER_TFT)
static void closeNodeInfoModal() {
    if (s_nodeInfoModal) {
        lv_obj_del(s_nodeInfoModal);
    }
    s_nodeInfoModal = nullptr;
}

// Shows the current node's identity/radio details in a dismissible popup,
// layered over the CFG modal. Any key (or the close key) dismisses it.
static void openNodeInfoModal() {
    if (!s_rootScreen || s_nodeInfoModal) return;

    char info[10][96] = {};
    int infoCount = buildDeviceInfoLines(info, 10);

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
    lv_label_set_text_fmt(hint, "Any key / %s = Close", modalCloseKeyLabel());
}
#endif

static void closeLegendModal() {
    if (s_legendModal) {
        lv_obj_del(s_legendModal);
    }
    s_legendModal = nullptr;
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
        {"Help", HELTEC_NAV_LEGEND},
#else
        {"Config", HELTEC_NAV_CFG},
        {"DM", HELTEC_NAV_DM},
        {"Nodes", HELTEC_NAV_NODES},
        {"Live", HELTEC_NAV_LIVE},
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
    // Keep classic yellow in dark mode, but use darker amber in light mode
    // for readable contrast against bright backgrounds.
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
    if (s_dmModal) {
        lv_obj_del(s_dmModal);
    }
    s_dmModal = nullptr;
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
    if (s_nodesModal) {
        lv_obj_del(s_nodesModal);
    }
    nodesPanelWifiRestore();
    s_nodesMapSelectionNodeId = 0;
    s_nodesMapSelectionSinceMs = 0;
    s_nodesMapLastPrimePollMs = 0;
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
        s_nodesSnapshot[s_nodesSnapshotCount++] = *n;
    }

    nodesApplyFilter();
}

static void nodesApplyFilter() {
    s_nodesFilteredCount = 0;

    for (int i = 0; i < s_nodesSnapshotCount && s_nodesFilteredCount < MAX_NODES; i++) {
        const NodeEntry &n = s_nodesSnapshot[i];
        bool match = true;

        if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
            char nodeIdText[16];
            snprintf(nodeIdText, sizeof(nodeIdText), "!%08lX", (unsigned long)n.nodeId);
            match = dmNodePickerContainsNoCase(n.longName, s_nodesFilter)
                 || dmNodePickerContainsNoCase(n.shortName, s_nodesFilter)
                 || dmNodePickerContainsNoCase(nodeIdText, s_nodesFilter);
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
    if (s_nodesFilterDialog) {
        lv_obj_del(s_nodesFilterDialog);
    }
    s_nodesFilterDialog = nullptr;
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
        if (s_nodesSnapshot[i].nodeId == nodeId) return true;
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
    if (!s_cfg.wifiSsid[0]) return;

    switch (s_nodesWifiPrevMode) {
        case WIFI_OFF:
            WiFi.mode(WIFI_STA);
            WiFi.begin(s_cfg.wifiSsid, s_cfg.wifiPass);
            s_nodesWifiStateChanged = true;
            break;
        case WIFI_STA:
            WiFi.begin(s_cfg.wifiSsid, s_cfg.wifiPass);
            s_nodesWifiStateChanged = true;
            break;
#ifdef WIFI_AP_STA
        case WIFI_AP:
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(s_cfg.wifiSsid, s_cfg.wifiPass);
            s_nodesWifiStateChanged = true;
            break;
#endif
        case WIFI_AP_STA:
            WiFi.begin(s_cfg.wifiSsid, s_cfg.wifiPass);
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
#if !HAS_SD_CARD
    LV_UNUSED(s);
    LV_UNUSED(staticHostResolvable);
    LV_UNUSED(tileHostResolvable);
    return false;
#else
    if (WiFi.status() != WL_CONNECTED || WiFi.getMode() == WIFI_AP) return false;
    if (!nodesMapEnsureDir("/camillia")) return false;
    if (!nodesMapEnsureDir("/camillia/state_maps")) return false;

    float latPad = max(0.20f, (s.latMax - s.latMin) * 0.08f);
    float lonPad = max(0.20f, (s.lonMax - s.lonMin) * 0.08f);
    float lat0 = max(-85.0f, s.latMin - latPad);
    float lat1 = min(85.0f, s.latMax + latPad);
    float lon0 = max(-179.8f, s.lonMin - lonPad);
    float lon1 = min(179.8f, s.lonMax + lonPad);

    String path = nodesStateMapPath(s.code);

    if (staticHostResolvable) {
        String url = "https://staticmap.openstreetmap.de/staticmap.php?bbox=";
        url += String(lon0, 4); url += ",";
        url += String(lat0, 4); url += ",";
        url += String(lon1, 4); url += ",";
        url += String(lat1, 4);
        url += "&size=";
        url += String(kStateMapImageW);
        url += "x";
        url += String(kStateMapImageH);
        url += "&maptype=mapnik";

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(5000);

        HTTPClient http;
        if (http.begin(client, url)) {
            http.addHeader("User-Agent", "camillia-mt/1.0");
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                if (SD.exists(path.c_str())) SD.remove(path.c_str());
                File out = SD.open(path.c_str(), FILE_WRITE);
                if (out) {
                    int written = http.writeToStream(&out);
                    out.close();
                    http.end();
                    if (written > 0 && nodesFileLooksLikePng(path.c_str())) {
                        nodesWriteStateMapMeta(s.code, lat0, lat1, lon0, lon1);
                        return true;
                    }
                } else {
                    http.end();
                }
                SD.remove(path.c_str());
            } else {
                http.end();
            }
        }
    }

    if (!tileHostResolvable) return false;

    auto lonToTileX = [](double lonDeg, int z) -> int {
        double n = (lonDeg + 180.0) / 360.0 * (double)(1 << z);
        return (int)floor(n);
    };
    auto latToTileY = [](double latDeg, int z) -> int {
        double latRad = latDeg * 3.14159265358979323846 / 180.0;
        double n = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / 3.14159265358979323846) * 0.5 * (double)(1 << z);
        return (int)floor(n);
    };
    auto tileXToLon = [](int x, int z) -> double {
        return ((double)x / (double)(1 << z)) * 360.0 - 180.0;
    };
    auto tileYToLat = [](int y, int z) -> double {
        double n = 3.14159265358979323846 - 2.0 * 3.14159265358979323846 * (double)y / (double)(1 << z);
        return 180.0 / 3.14159265358979323846 * atan(0.5 * (exp(n) - exp(-n)));
    };

    float spanLon = max(0.4f, s.lonMax - s.lonMin);
    float spanLat = max(0.4f, s.latMax - s.latMin);
    double centerLat = (double)(s.latMin + s.latMax) * 0.5;
    double centerLon = (double)(s.lonMin + s.lonMax) * 0.5;

    int z = 5;
    for (int cand = 7; cand >= 3; cand--) {
        int ty = latToTileY(centerLat, cand);
        double latTop = tileYToLat(ty, cand);
        double latBottom = tileYToLat(ty + 1, cand);
        double tileLatSpan = fabs(latTop - latBottom);
        double tileLonSpan = 360.0 / (double)(1 << cand);
        if (tileLonSpan >= (double)spanLon * 1.15 && tileLatSpan >= (double)spanLat * 1.15) {
            z = cand;
            break;
        }
    }

    int tx = lonToTileX(centerLon, z);
    int ty = latToTileY(centerLat, z);
    int maxTile = (1 << z) - 1;
    if (tx < 0) tx = 0;
    if (tx > maxTile) tx = maxTile;
    if (ty < 0) ty = 0;
    if (ty > maxTile) ty = maxTile;

    String tileUrl = "https://tile.openstreetmap.org/";
    tileUrl += String(z);
    tileUrl += "/";
    tileUrl += String(tx);
    tileUrl += "/";
    tileUrl += String(ty);
    tileUrl += ".png";

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000);

    HTTPClient http;
    if (!http.begin(client, tileUrl)) return false;
    http.addHeader("User-Agent", "camillia-mt/1.0");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File out = SD.open(path.c_str(), FILE_WRITE);
    if (!out) {
        http.end();
        return false;
    }

    int written = http.writeToStream(&out);
    out.close();
    http.end();
    if (written <= 0 || !nodesFileLooksLikePng(path.c_str())) {
        SD.remove(path.c_str());
        return false;
    }

    float tileLon0 = (float)tileXToLon(tx, z);
    float tileLon1 = (float)tileXToLon(tx + 1, z);
    float tileLat1 = (float)tileYToLat(ty, z);
    float tileLat0 = (float)tileYToLat(ty + 1, z);
    nodesWriteStateMapMeta(s.code, tileLat0, tileLat1, tileLon0, tileLon1);
    return true;
#endif
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

    if (!s_cfg.wifiSsid[0]) {
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
            WiFi.begin(s_cfg.wifiSsid, s_cfg.wifiPass);
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
    return &s_nodesSnapshot[snapshotIdx];
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
    if (!s_nodesList) return;

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *nodesListFont = &lv_font_montserrat_12;
    const int nodesListRowH = 28;
#else
    const lv_font_t *nodesListFont = &lv_font_montserrat_10;
    const int nodesListRowH = 22;
#endif

    if (s_nodesTitleLabel) {
        int nodeCount = s_nodesSnapshotCount;
        if (s_nodesFilterOpen && s_nodesFilterLen > 0) {
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
                                  "Type=Filter  Up/Down/J/K=Select  Enter=Actions  Bksp=Edit/Exit  %s=Back",
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "Type=Filter  Up/Down=Select  Enter=Actions  Bksp=Edit/Exit  %s=Back",
                                  modalCloseKeyLabel());
#endif
        } else {
#if defined(DEVICE_TDECK)
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "Up/Down/J/K=Select  Enter=Actions  Type=Filter  %s=Back",
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(s_nodesHintLabel,
                                  "Up/Down=Select  Enter=Actions  Type=Filter  %s=Back",
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
        const NodeEntry &n = s_nodesSnapshot[snapshotIdx];

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
             n.favorite ? "* " : "",
             n.shortName[0] ? n.shortName : "----");
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
    if (!s_nodesDetail) return;

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
    if (s_tracerouteBackdrop) {
        lv_obj_del(s_tracerouteBackdrop);
    }
    s_tracerouteBackdrop = nullptr;
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
    Channels.addMessage(CHAN_ANN, prefix, status, ok ? TFT_DARKGREY : TFT_RED, 0, false);
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
    if (s_nodesActionModal) {
        lv_obj_del(s_nodesActionModal);
    }
    s_nodesActionModal = nullptr;
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
                && s_nodesSnapshot[snapshotIdx].nodeId == nodeId) {
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

    const int modalW = min(190, lv_disp_get_hor_res(NULL) - 14);
    const int modalH = min(190, lv_disp_get_ver_res(NULL) - 10);

    lv_obj_t *actionParent = s_rootScreen ? s_rootScreen : s_nodesModal;
    s_nodesActionModal = lv_obj_create(actionParent);
    lv_obj_set_size(s_nodesActionModal, modalW, modalH);
    lv_obj_align(s_nodesActionModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_nodesActionModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_nodesActionModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_nodesActionModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_nodesActionModal, 1, 0);
    lv_obj_set_style_border_color(s_nodesActionModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_nodesActionModal, 4, 0);
    lv_obj_set_style_pad_row(s_nodesActionModal, 4, 0);
    lv_obj_set_flex_flow(s_nodesActionModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_nodesActionModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_move_foreground(s_nodesActionModal);

    lv_obj_t *title = lv_label_create(s_nodesActionModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Node Actions");

    const bool selectedIsFavorite = selected->favorite;
    const char *kActionLabels[kNodesActionCount] = {
        "Traceroute",
        "Send DM",
        selectedIsFavorite ? "Remove Favorite" : "Add Favorite",
        "Request Info",
        "Request Position"
    };
    const lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
                                    ? lv_color_hex(0x13233D)
                                    : lv_color_hex(0xD9E8FF);
    for (int i = 0; i < kNodesActionCount; i++) {
        lv_obj_t *row = lv_btn_create(s_nodesActionModal);
        s_nodesActionRows[i] = row;
        lv_obj_set_width(row, lv_pct(100));
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

    const Channel &ch = Channels.get(CHAN_ANN);
    if (!force && s_lastRenderedLiveCount == ch.count && s_lastRenderedLiveScrollOff == ch.scrollOff) {
        return;
    }

    const bool stickToTop = force || (lv_obj_get_scroll_y(s_liveList) <= 2);
    const int32_t prevScrollY = lv_obj_get_scroll_y(s_liveList);

    lv_obj_clean(s_liveList);

    int rowCount = 0;
    for (int row = 0; row < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(CHAN_ANN, row);
        if (!dl) break;
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

static void openLiveModal() {
    if (!s_rootScreen || s_liveModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeCfgModal();
    closeLegendModal();

    Channels.get(CHAN_ANN).scrollOff = 0;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;

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

static void activateDmSelection() {
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
    } else {
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
        const NodeEntry &n = s_dmNodeSnapshot[i];
        if (useFilter) {
            bool match = false;
            if (dmNodePickerContainsNoCase(n.shortName, s_dmNodeFilter)) match = true;
            if (!match && dmNodePickerContainsNoCase(n.longName, s_dmNodeFilter)) match = true;
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
            if (s_dmNodeSnapshot[j].nodeId == n->nodeId) {
                seen = true;
                break;
            }
        }
        if (seen) continue;

        s_dmNodeSnapshot[s_dmNodeSnapshotCount++] = *n;
    }

    dmNodePickerApplyFilter();
}

static const NodeEntry *selectedDmNodeForPicker() {
    if (s_dmNodeSelection < 0 || s_dmNodeSelection >= s_dmNodeFilteredCount) return nullptr;
    int snapshotIdx = s_dmNodeFilteredIdx[s_dmNodeSelection];
    if (snapshotIdx < 0 || snapshotIdx >= s_dmNodeSnapshotCount) return nullptr;
    return &s_dmNodeSnapshot[snapshotIdx];
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
    if (!s_dmNodePickerModal || !s_dmNodePickerList) return;

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
        const NodeEntry &n = s_dmNodeSnapshot[snapshotIdx];

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
        const char *longDisp = n.longName[0] ? n.longName : "(unknown)";
        const char *shortDisp = liveShortNameUsable(n.shortName) ? n.shortName : "????";
        snprintf(rowText,
             sizeof(rowText),
             "%s%s (%s)",
             n.favorite ? "* " : "",
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
    if (s_dmNodePickerModal) {
        lv_obj_del(s_dmNodePickerModal);
    }
    s_dmNodePickerModal = nullptr;
    s_dmNodePickerList = nullptr;
    s_dmNodePickerTitle = nullptr;
    s_dmNodePickerHint = nullptr;
    s_dmNodeSnapshotCount = 0;
    s_dmNodeFilteredCount = 0;
    s_dmNodeSelection = -1;
    s_dmNodeFilterOpen = false;
    s_dmNodeFilterLen = 0;
    s_dmNodeFilter[0] = '\0';
    memset(s_dmNodeSnapshot, 0, sizeof(s_dmNodeSnapshot));
    memset(s_dmNodeFilteredIdx, 0, sizeof(s_dmNodeFilteredIdx));
    memset(s_dmNodePickerRows, 0, sizeof(s_dmNodePickerRows));
}

static void refreshDmModal(bool force) {
    if (!s_dmModal || !s_dmConvList || !s_dmMsgList) return;

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
    const lv_font_t *dmMsgFont = kMainScreenFont;
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
                                  "Up/Down/J/K = Select   Enter = Compose/Focus   Bksp = Delete");
#elif defined(DEVICE_TLORA_PAGER_TFT)
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Up/Down = Select   Enter = Compose/Focus   Bksp = Delete");
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Long-press convo 3s = Delete   Enter = Compose/Focus   %s = Back",
                                  modalCloseKeyLabel());
#else
            lv_label_set_text_fmt(s_dmHintLabel,
                                  "Up/Down = Select   Enter = Compose/Focus   %s = Delete   %s = Back",
                                  dmDeleteTriggerLabel(),
                                  modalCloseKeyLabel());
#endif
        }
    }

    refreshDmPanelFocusStyles();
}

static void openDmModal() {
    if (!s_rootScreen || s_dmModal) return;
    if (s_composeModal) closeComposePrompt();
    closeLiveModal();
    closeNodesModal();
    closeCfgModal();
    closeLegendModal();

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
                          "Up/Down/J/K = Select   Enter = Compose/Focus   Bksp = Delete");
#elif defined(DEVICE_TLORA_PAGER_TFT)
    lv_label_set_text_fmt(hint,
                          "Up/Down = Select   Enter = Compose/Focus   Bksp = Delete");
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text_fmt(hint,
                          "Long-press convo 3s = Delete   Enter = Compose/Focus   %s = Back",
                          modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint,
                          "Up/Down = Select   Enter = Compose/Focus   %s = Delete   %s = Back",
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
    if (!s_rootScreen || s_nodesModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeLiveModal();
    closeCfgModal();
    closeLegendModal();

    snapshotNodesForModal();

    int modalW = lv_disp_get_hor_res(NULL);
    int modalH = lv_disp_get_ver_res(NULL);
    const int modalPad = 4;
    const int contentGap = 3;
    int contentW = modalW - (modalPad * 2);
    if (contentW < 120) contentW = modalW;

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *nodesDetailFont = &lv_font_montserrat_14;
#elif defined(DEVICE_TDECK)
    const lv_font_t *nodesDetailFont = &lv_font_montserrat_14;
#else
    const lv_font_t *nodesDetailFont = &lv_font_montserrat_10;
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
    lv_label_set_text_fmt(hint, "Up/Down/J/K=Select   Enter=Actions   %s=Back", modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint, "Up/Down=Select   Enter=Actions   %s=Back", modalCloseKeyLabel());
#endif
#endif
}

static void openLegendModal() {
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
        "(H) Help\n"
        "(Enter) Compose/Reply");
#else
    lv_label_set_text(
        leftCol,
        "(D) Direct Messages\n"
        "(C) Configuration\n"
        "(N) Nodes\n"
        "(L) Live (C clears log)\n"
        "(H) Help\n"
        "(Enter) Compose/Reply");
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
        "(H) Help\n"
        "(Enter) Compose/Reply\n"
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
    if (!s_rootScreen || s_cfgModal) return;
    if (s_composeModal) closeComposePrompt();
    closeDmModal();
    closeNodesModal();
    closeLegendModal();

    initCfgActions();
    s_cfgSelection = 0;
    s_cfgStatus[0] = '\0';
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
    lv_obj_set_width(s_cfgHeaderStatus, lv_pct(58));
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
#if defined(DEVICE_TDECK)
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
#else
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_label_set_text_fmt(hint, "(I) = Info panel   Wheel = Scroll   Click wheel = Swap   Bksp = Back/Close");
#elif defined(DEVICE_TDECK)
    lv_label_set_text_fmt(hint, "Up/Down/J/K = Select   Enter = Run   (I)nformation   %s = Close", modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint, "Up/Down = Select   Enter = Run   (I)nformation   %s = Close", modalCloseKeyLabel());
#endif
#endif

    refreshCfgModal();
}

static void activateCfgSelection() {
    if (s_cfgActionCount <= 0 || s_cfgSelection < 0 || s_cfgSelection >= s_cfgActionCount) return;
    const int actionId = s_cfgActions[s_cfgSelection];
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

    switch (actionId) {
        case CFG_ACTION_WEBCFG: {
            if (s_webCfgEnabled) {
                s_webCfgEnabled = false;
                persistWebCfgEnabled();
                if (webCfgRunning()) {
                    webCfgEnd();
                }
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web Config disabled");
            } else {
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
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web Config enabled (start failed)");
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
            // Do not rebuild/clean cfg rows in this same input cycle.
            // The deferred theme rebuild will recreate the modal safely.
            return;
        } break;

        case CFG_ACTION_UNITS:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec UNITS");
            s_cfg.displayUnits = (uint8_t)(s_cfg.displayUnits ? 0 : 1);
            persistConfigToPrefs();
            refreshNodesDetails();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Units: %s",
                     s_cfg.displayUnits ? "Imperial" : "Metric");
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

        case CFG_ACTION_MSG_ALERT:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec MSG_ALERT");
            s_cfg.msgAlertSound = (uint8_t)((s_cfg.msgAlertSound + 1) % 4);
            persistMessageAlertSetting();
            triggerMessageAlert(true);  // Preview the selected notification profile.
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Notification sound: %s", msgAlertSoundName(s_cfg.msgAlertSound));
            break;

        case CFG_ACTION_SPLASH_MELODY:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec SPLASH_MELODY");
            s_cfg.splashMelodyEnabled = !s_cfg.splashMelodyEnabled;
            persistSplashMelodySetting();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Splash melody: %s", s_cfg.splashMelodyEnabled ? "On" : "Off");
            break;

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

    refreshCfgModal();
}

static void closeCfgConfirmModal() {
    if (s_cfgConfirmBackdrop) {
        lv_obj_del(s_cfgConfirmBackdrop);
    } else if (s_cfgConfirmModal) {
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

    char actionText[80];
    cfgActionLabel(actionId, actionText, sizeof(actionText));

    const int w = lv_disp_get_hor_res(NULL);
    const int h = lv_disp_get_ver_res(NULL);
    int modalW = lv_disp_get_hor_res(NULL) - 40;
    if (modalW < 160) modalW = lv_disp_get_hor_res(NULL) - 8;
    if (modalW > 300) modalW = 300;

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
    lv_obj_set_style_bg_color(s_cfgConfirmModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_cfgConfirmModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_cfgConfirmModal, 1, 0);
    lv_obj_set_style_border_color(s_cfgConfirmModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_cfgConfirmModal, 10, 0);
    lv_obj_set_style_pad_row(s_cfgConfirmModal, 10, 0);
    lv_obj_set_flex_flow(s_cfgConfirmModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_cfgConfirmModal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(s_cfgConfirmBackdrop);

    lv_obj_t *title = lv_label_create(s_cfgConfirmModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(title, "Confirm?");

    lv_obj_t *actionBox = lv_obj_create(s_cfgConfirmModal);
    lv_obj_set_width(actionBox, lv_pct(100));
    lv_obj_set_height(actionBox, LV_SIZE_CONTENT);
    lv_obj_clear_flag(actionBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(actionBox, lv_color_hex(0x123266), 0);
    lv_obj_set_style_bg_opa(actionBox, LV_OPA_60, 0);
    lv_obj_set_style_border_width(actionBox, 1, 0);
    lv_obj_set_style_border_color(actionBox, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_left(actionBox, 6, 0);
    lv_obj_set_style_pad_right(actionBox, 6, 0);
    lv_obj_set_style_pad_top(actionBox, 4, 0);
    lv_obj_set_style_pad_bottom(actionBox, 4, 0);

    lv_obj_t *q = lv_label_create(actionBox);
    lv_obj_set_width(q, lv_pct(100));
    lv_obj_set_style_text_font(q, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(q, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(q, "Action: %s", actionText);

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

    auto makeConfirmBtn = [](lv_obj_t *parent, const char *text, uint32_t color,
                             lv_event_cb_t cb) {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_height(btn, 36);
        lv_obj_set_style_min_width(btn, 84, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(lbl, text);
        lv_obj_center(lbl);
        return btn;
    };
    makeConfirmBtn(btnRow, "(N)o", 0x6B3030, onCfgConfirmNoPressed);
    makeConfirmBtn(btnRow, "(Y)es", 0x2F6B30, onCfgConfirmYesPressed);
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

        bool typingContext = s_composeModal || (s_dmNodePickerModal && s_dmNodeFilterOpen);
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

        // The CFG confirmation dialog is modal: only Y/N (and a close key, which
        // cancels) are honored — every other shortcut is swallowed while it's up.
        if (s_cfgConfirmModal) {
            if (k == 'y' || k == 'Y') {
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

#if !defined(DEVICE_TLORA_PAGER_TFT)
        // The (I)nformation popup layers over the CFG modal; any key dismisses it.
        if (s_nodeInfoModal) {
            closeNodeInfoModal();
            continue;
        }
#endif

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
            if (k == KEY_ENTER) {
                activateDmSelection();
                continue;
            }
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

            if (k >= 0x20 && k < 0x7F) {
                if (!s_nodesFilterOpen) s_nodesFilterOpen = true;
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
                Channels.clearChannel(CHAN_ANN);
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

        if (!s_composeModal) {
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
                            int delta = (k == KEY_SCROLL_UP) ? -1 : 1;
                            pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + delta);
                        }
                        refreshChannelGlow(true);
                        continue;
                    }

#else
                    if (s_pagerChatCursorMode) {
                        int navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
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
                        if (s_selectedMsgReplyPacketId != 0) {
                            openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                        }
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

            if (k == 'l' || k == 'L') {
                openLiveModal();
            } else if (k == 'd' || k == 'D') {
                openDmModal();
            } else if (k == 'c' || k == 'C') {
                openCfgModal();
            } else if (k == 'n' || k == 'N') {
                openNodesModal();
            } else if (k == 'h' || k == 'H') {
                openLegendModal();
#if defined(DEVICE_CARDPUTER_LORA_HAT)
            } else if ((k == KEY_ENTER || k == KEY_FN_ENTER)
                       && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#else
            } else if (k == KEY_ENTER && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#endif
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                if (!s_cardputerMainChatPanelFocused) {
                    // In nav focus, Enter is reserved for channel selector flow.
                } else if (s_pagerChatCursorMode && s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
#else
                if (kPagerWheelChatNav) {
                    if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                        openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                    } else {
                        openComposePrompt(0, nullptr);
                    }
                } else if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
#endif
            } else if (k == KEY_BACKSPACE) {
                if (s_selectedMsgReplyPacketId != 0 || s_selectedMsgText[0]) {
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

    if (!cfgExport(s_cfg)) {
        Serial.println("[cfg] web save export failed");
    }

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

    const uint16_t bgTop = s_ui.splashTop;
    const uint16_t bgBottom = s_ui.splashBottom;
    const uint16_t cardBg = s_ui.splashCardBg;
    const uint16_t cardEdge = s_ui.splashCardEdge;
    const uint16_t cardEdgeHi = s_ui.splashCardEdgeHi;
    const uint16_t titleCol = s_ui.splashTitle;
    const uint16_t subCol = s_ui.splashSub;
    const uint16_t dimCol = s_ui.splashDim;

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

    const char *firmwareName = "CAMILLIA MT";
    const char *version = APP_VERSION;

    char nodeLine[72];
    const char *nodeLong = s_cfg.nodeLong[0] ? s_cfg.nodeLong : "unknown node";
    const char *nodeShort = s_cfg.nodeShort[0] ? s_cfg.nodeShort : "----";
    snprintf(nodeLine, sizeof(nodeLine), "%s (%s)", nodeLong, nodeShort);

#if !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_CARDPUTER_LORA_HAT)
    displayDev().setFont(&fonts::Orbitron_Light_32);
    displayDev().setTextSize(0.82f);
    displayDev().setTextColor(titleCol, cardBg);
    int fwW = displayDev().textWidth(firmwareName);
    displayDev().drawString(firmwareName, cardX + max(0, (cardW - fwW) / 2), cardY + 10);
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    const float flowerScale = 1.15f;
#else
    const float flowerScale = 1.0f;
#endif

    auto drawCamelliaMark = [&](int cx, int cy, float scale) {
        const uint16_t SHADOW       = 0x18E4;
        const uint16_t PETAL_OUTER  = 0xF9CF;
        const uint16_t PETAL_MID    = 0xFADF;
        const uint16_t PETAL_INNER  = 0xFF7D;
        const uint16_t PETAL_HILITE = 0xFFDF;
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

        const int shadowDx = scaled(1.0f);
        const int shadowDy = scaled(4.0f);
        const int shadowR = scaled(34.0f, 1);
        const int petalOuterOrbitX = scaled(23.0f, 1);
        const int petalOuterOrbitY = scaled(18.0f, 1);
        const int petalMidOrbitX = scaled(13.0f, 1);
        const int petalMidOrbitY = scaled(10.0f, 1);
        const int petalInnerOrbitX = scaled(6.0f, 1);
        const int petalInnerOrbitY = scaled(5.0f, 1);
        const int petalOuterR0 = scaled(11.0f, 1);
        const int petalOuterR1 = scaled(12.0f, 1);
        const int petalHiliteOuterDx = scaled(2.0f);
        const int petalHiliteOuterDy = scaled(2.0f);
        const int petalHiliteOuterR0 = scaled(7.0f, 1);
        const int petalHiliteOuterR1 = scaled(8.0f, 1);
        const int petalMidR = scaled(9.0f, 1);
        const int petalMidHiliteDx = scaled(1.0f);
        const int petalMidHiliteDy = scaled(1.0f);
        const int petalMidHiliteR = scaled(5.0f, 1);
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

        displayDev().fillCircle(cx + shadowDx, cy + shadowDy, shadowR, SHADOW);

        for (int i = 0; i < 10; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
            int px = cx + (int)lroundf((float)petalOuterOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalOuterOrbitY * sinf(a));
            int pr = (i & 1) ? petalOuterR1 : petalOuterR0;
            int hiliteR = (i & 1) ? petalHiliteOuterR1 : petalHiliteOuterR0;
            displayDev().fillCircle(px, py, pr, PETAL_OUTER);
            displayDev().fillCircle(px - petalHiliteOuterDx, py - petalHiliteOuterDy, hiliteR, PETAL_HILITE);
            displayDev().drawCircle(px, py, pr, PETAL_EDGE);
        }

        for (int i = 0; i < 8; i++) {
            float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
            int px = cx + (int)lroundf((float)petalMidOrbitX * cosf(a));
            int py = cy + (int)lroundf((float)petalMidOrbitY * sinf(a));
            displayDev().fillCircle(px, py, petalMidR, PETAL_MID);
            displayDev().fillCircle(px - petalMidHiliteDx, py - petalMidHiliteDy, petalMidHiliteR, PETAL_HILITE);
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

    displayDev().setFont(&fonts::Orbitron_Light_32);
    displayDev().setTextSize(1.18f);
    displayDev().setTextColor(titleCol, cardBg);
    int fwW = displayDev().textWidth(firmwareName);
    const int titleY = cardY + 14;
    displayDev().drawString(firmwareName, leftX + max(0, (leftW - fwW) / 2), titleY);

    int titleH = displayDev().fontHeight();
    const int flowerAreaTop = titleY + titleH + 10;
    const int flowerAreaBottom = cardY + cardH - 14;
    float pagerFlowerScale = min((float)(flowerAreaBottom - flowerAreaTop) / 76.0f,
                                 (float)(leftW - 16) / 70.0f);
    if (pagerFlowerScale < 1.10f) pagerFlowerScale = 1.10f;
    if (pagerFlowerScale > 1.70f) pagerFlowerScale = 1.70f;
    drawCamelliaMark(leftX + (leftW / 2),
                     (flowerAreaTop + flowerAreaBottom) / 2,
                     pagerFlowerScale);

    const int rightInfoTop = titleY + titleH + 6;
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
    drawCamelliaMark(cardX + (cardW / 2),
                     cardY + (cardH / 2) - 6,
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
    #if defined(DEVICE_CARDPUTER_LORA_HAT)
        bool dropdownCursor = (!s_cardputerMainChatPanelFocused
                       && isChannelDropdownVisible()
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
            lv_obj_set_style_border_width(s_channelSelectorBtn, 1, 0);
            lv_obj_set_style_border_color(s_channelSelectorBtn, lv_color_hex(0x2B4D8C), 0);
            lv_obj_set_style_outline_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_outline_width(s_channelSelectorBtn, 0, 0);
            lv_obj_set_style_shadow_opa(s_channelSelectorBtn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(s_channelSelectorBtn, 0, 0);
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
        lv_obj_set_style_bg_color(
            s_channelSelectorBtn,
            (s_cfg.uiMode == UI_MODE_LIGHT) ? chatPanelBackgroundColor() : lv_color_hex(0x102750),
            0);
        lv_obj_set_style_bg_opa(s_channelSelectorBtn, LV_OPA_70, 0);
        lv_obj_set_style_border_width(s_channelSelectorBtn, 1, 0);
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
}

static void setActiveChannel(int channelIdx) {
    if (channelIdx < 0 || channelIdx >= MESH_CHANNELS) return;
    if (s_composeModal && channelIdx != s_composeChannelIdx) closeComposePrompt();
    s_activeChannel = channelIdx;
    s_pagerChatCursorMode = false;
    s_pagerChatCursorDisplayIndex = -1;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    s_cardputerMainChatPanelFocused = false;
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    if (!s_cardputerMainChatPanelFocused
        && isChannelDropdownVisible()
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

        // Keep selector width fixed: size it once from channel 0 label and never resize on channel changes.
        if (s_channelSelectorFixedBtnW <= 0) {
            const char *firstChannelName = channelName(0);
            if (!firstChannelName || !firstChannelName[0]) firstChannelName = "Channel";

            lv_label_set_text(s_channelSelectorLabel, firstChannelName);
            lv_label_set_long_mode(s_channelSelectorLabel, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(s_channelSelectorLabel, LV_SIZE_CONTENT);
            lv_obj_update_layout(s_channelSelectorLabel);

            lv_coord_t textW = lv_obj_get_width(s_channelSelectorLabel);
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
                lv_coord_t maxW = lv_obj_get_width(header) - 110;
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
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

    // Keep time centered between the left-side cluster and battery percentage.
    lv_obj_update_layout(s_chatHeaderBar);
    lv_coord_t selectorRight = lv_obj_get_x(s_channelSelectorBtn) + lv_obj_get_width(s_channelSelectorBtn);
    lv_coord_t battLeft = lv_obj_get_x(s_chatHeaderBattText);
    lv_coord_t timeW = lv_obj_get_width(s_chatHeaderTime);
    lv_coord_t slotStart = selectorRight + 4;

    // If GPS/WiFi remain in the header, reserve that area to avoid overlap with time.
    if (s_chatHeaderGps && lv_obj_get_parent(s_chatHeaderGps) == s_chatHeaderBar) {
        lv_obj_align_to(s_chatHeaderGps, s_channelSelectorBtn, LV_ALIGN_OUT_RIGHT_MID, 5, headerTextYOffset);
        lv_coord_t statusRight = lv_obj_get_x(s_chatHeaderGps) + lv_obj_get_width(s_chatHeaderGps);
        if (s_chatHeaderWifi && lv_obj_get_parent(s_chatHeaderWifi) == s_chatHeaderBar) {
            lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_RIGHT_MID, 5, headerTextYOffset);
            lv_coord_t wifiRight = lv_obj_get_x(s_chatHeaderWifi) + lv_obj_get_width(s_chatHeaderWifi);
            if (wifiRight > statusRight) statusRight = wifiRight;
        }
        slotStart = statusRight + 2;
    }

    lv_coord_t slotEnd = battLeft - 2;

    if (slotEnd <= slotStart || timeW <= 0) {
        lv_obj_align_to(s_chatHeaderTime, s_channelSelectorBtn, LV_ALIGN_OUT_RIGHT_MID, 6, headerTextYOffset);
    } else {
        lv_coord_t x = slotStart + (slotEnd - slotStart - timeW) / 2;
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

static void appendRxText(int chanIdx, uint32_t fromNode, const char *text, uint32_t packetId, bool viaMqtt) {
    char timePrefix[12];
    char sender[16];
    char prefix[44];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    LV_UNUSED(viaMqtt);
    NodeEntry *n = Nodes.find(fromNode);
    const char *hintShort = (n && n->shortName[0]) ? n->shortName : nullptr;
    liveNodeLabelWithHint(fromNode, hintShort, sender, sizeof(sender), false);
    snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, sender);
#else
    liveNodeLabel(fromNode, sender, sizeof(sender), false);
    const char *transportIcon = viaMqtt ? LV_SYMBOL_GLOBE_TINY : LV_SYMBOL_RADIO_TINY;
    // Keep a small visual buffer between transport icon and timestamp.
    snprintf(prefix, sizeof(prefix), "%s  %s[%s] ", transportIcon, timePrefix, sender);
#endif

    Channels.addMessage(chanIdx, prefix, text, TFT_WHITE, packetId, false);
    if (chanIdx >= 0 && chanIdx < MESH_CHANNELS && chanIdx != s_activeChannel) {
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
    Channels.addMessage(CHAN_ANN, timePrefix, line, TFT_DARKGREY, 0, false);
}

static void appendLiveRxEncrypted(const MeshPacket &pkt) {
    char timePrefix[12];
    char who[20];
    char line[96];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
    snprintf(line, sizeof(line), "R %s ENC h%02X", who, pkt.hdr.channel);
    Channels.addMessage(CHAN_ANN, timePrefix, line, TFT_DARKGREY, 0, false);
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

static bool processMeshPacket(const MeshPacket &rxPkt) {
    MeshPacket pkt = rxPkt;

    if (isDuplicate(pkt.hdr.from, pkt.hdr.id)) return false;

    // Match v1 behavior: ignore reflected copies of our own transmitted packets.
    if (s_myNodeId != 0 && pkt.hdr.from == s_myNodeId) return false;

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
                                   0);
                    if (viewingDm) {
                        DMs.markRead(pkt.hdr.from);
                    }
                } else {
                    appendRxText(chanIdx, pkt.hdr.from, textBuf, pkt.hdr.id, viaMqtt);
                }

                triggerMessageAlert();
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

                    triggerMessageAlert();
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
                Channels.addMessage(CHAN_ANN, "", "[position] skip: no fix/fallback", TFT_DARKGREY, 0, false);
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

static void refreshChatView(bool force) {
    if (!s_chatPanel || !s_chatList) return;

    refreshChatComposeButtonState();

    const Channel &ch = Channels.get(s_activeChannel);
    if (!force && s_lastRenderedChannel == s_activeChannel && s_lastRenderedCount == ch.count) {
        return;
    }

    const DisplayLine *rows[MAX_MSG_LINES] = {};
    int rowCount = 0;
    collectChatRows(rows, rowCount);

    const bool stickToBottom = force || (lv_obj_get_scroll_bottom(s_chatList) <= 6);
    const int32_t prevScrollY = lv_obj_get_scroll_y(s_chatList);

    lv_obj_clean(s_chatList);
    lv_obj_t *lastMsgObj = nullptr;
    lv_obj_t *selectedMsgObj = nullptr;

    if (rowCount == 0) {
        lv_obj_t *empty = lv_label_create(s_chatList);
        lv_obj_set_style_text_font(empty, kChannelChatFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No messages yet");
    } else {
        int displayOrder[MAX_MSG_LINES] = {};
        int displayCount = 0;
        buildChatDisplayOrder(rows, rowCount, displayOrder, displayCount);

        uint32_t lastDateBucket = 0;
        for (int n = 0; n < displayCount; n++) {
            int i = displayOrder[n];

            const char *lineText = rows[i]->text;
            bool isContinuationLine = (lineText[0] == ' ' && lineText[1] == ' ');

            // Insert a date marker before the first line of any message that
            // lands on a new local calendar day. Continuation lines from a
            // wrapped multi-line message keep the same epoch and are skipped.
            if (!isContinuationLine) {
                uint32_t curBucket = chatDateBucket(rows[i]->epoch);
                if (curBucket != 0 && curBucket != lastDateBucket) {
                    insertChatDateMarker(s_chatList, rows[i]->epoch, kChannelChatFont);
                    lastDateBucket = curBucket;
                }
            }

            lv_obj_t *msg = lv_label_create(s_chatList);
            lastMsgObj = msg;
            lv_obj_set_width(msg, lv_pct(100));
            lv_obj_set_style_text_font(msg, kChannelChatFont, 0);
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
#if defined(DEVICE_TLORA_PAGER_TFT)
            lv_label_set_long_mode(msg, LV_LABEL_LONG_CLIP);
#else
            lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
#endif

            uint16_t textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? TFT_BLACK : TFT_WHITE;
            const char *ackSuffix = nullptr;
            if (rows[i]->packetId) {
                switch (rows[i]->ack) {
                    case DisplayLine::ACKED:
                        textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                        if (!isContinuationLine) ackSuffix = " [ACK]";
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
            char rendered[MSG_CHARS + 16];
            if (ackSuffix) {
                snprintf(rendered, sizeof(rendered), "%s%s", lineText, ackSuffix);
            } else {
                snprintf(rendered, sizeof(rendered), "%s", lineText);
            }
            setLabelTextEmojiSafe(msg, rendered);

            uint32_t replyPacketId = resolveReplyPacketId(rows, rowCount, i);

            bool isSelected = false;
            if (s_selectedMsgReplyPacketId != 0) {
                // Highlight the whole wrapped message group, not just the tapped row.
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

            bool nextIsDifferentMessage = false;
            if (n + 1 < displayCount) {
                int nextI = displayOrder[n + 1];
                uint32_t curId = rows[i]->packetId;
                uint32_t nextId = rows[nextI]->packetId;
                nextIsDifferentMessage = (curId == 0 || nextId == 0 || curId != nextId);
            }

            if (nextIsDifferentMessage) {
                lv_obj_t *sep = lv_obj_create(s_chatList);
                lv_obj_remove_style_all(sep);
                lv_obj_set_size(sep, lv_pct(100), 1);
                lv_obj_set_style_bg_color(sep, lv_color_hex(0x3F669F), 0);
                lv_obj_set_style_bg_opa(sep, LV_OPA_70, 0);
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
    lv_obj_set_style_text_font(s_chatHeaderTime, headerTextFont, 0);
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
#else
    s_chatHeaderWifi = nullptr;
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
    lv_label_set_text(s_chatShortcutText, "(H)elp");
#else
    lv_label_set_text(s_chatShortcutText, "(C)FG   (D)M   (N)odes   (L)ive   (H)elp");
#endif

    s_chatHeaderGps = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatHeaderGps, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align(s_chatHeaderGps, LV_ALIGN_RIGHT_MID, -4, 0);

    s_chatHeaderWifi = lv_label_create(s_chatShortcutBar);
    lv_obj_set_style_text_font(s_chatHeaderWifi, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);
    lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);
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
    closeDmModal();
    closeLiveModal();
    closeNodesModal();
    closeLegendModal();
    closeCfgModal();

    if (s_rootScreen) {
        lv_obj_del(s_rootScreen);
        s_rootScreen = nullptr;
    }

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

    lv_init();
    nodesMapInitFsDriver();
    lv_disp_draw_buf_init(&s_drawBuf, s_drawBufMem, nullptr, kMaxHorRes * kDrawBufLines);

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
    syncWifiCredsToPrefs();
    applyTimezoneFromConfig();
    bootTimeNtpSync();
    startWebConfigAuto();
    bootstrapStateMapsIfMissing();
    batteryInitAdc();
    gpsSetEnabled(s_cfg.gpsEnabled);
    Nodes.init();
    DMs.init();
    Channels.init();
    Channels.beginPersistence();
    Channels.loadPersisted();
    syncPrimaryChannelName();
    recomputeChannelHashes();
    s_radioReady = Radio.init();
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
    s_lastActivityMs = millis();
    Serial.printf("[lvgl-poc] started (%dx%d)\\n", displayDev().width(), displayDev().height());
}

void loop() {
    s_cfgDebugLog = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;

    uint32_t now = millis();
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
    }
    bool meshChanged = false;
    if (s_radioReady) {
        meshChanged = pollMeshRx();
    }
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

    now = millis();
    if (!s_screenAsleep && s_cfg.screenOnSecs > 0
        && (uint32_t)(now - s_lastActivityMs) > (uint32_t)s_cfg.screenOnSecs * 1000UL) {
        Serial.printf("[screen] sleeping (idle %lus, timeout %us)\n",
                      (unsigned long)((now - s_lastActivityMs) / 1000UL),
                      (unsigned)s_cfg.screenOnSecs);
        sleepScreen("timeout");
    }

    if (s_screenAsleep) {
        delay(5);
        return;
    }

    refreshChannelGlow(false);
    refreshHeaderTime(false);
    refreshHeaderStatus(false);
    refreshChatView(meshChanged);
    refreshLiveView(meshChanged);
    refreshChUtilChart(meshChanged);
    refreshSnrRssiChart(meshChanged);
    refreshDmModal(meshChanged);
    delay(5);
}

#endif  // UI_LVGL_POC
