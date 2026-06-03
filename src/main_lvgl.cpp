#if defined(UI_LVGL_POC)

#include <Arduino.h>
#include "config.h"
#include "channel_mgr.h"
#include "config_io.h"
#include "lgfx_tdeck.h"
#include "live_util.h"
#include "mesh_proto.h"
#include "mesh_radio.h"
#include "node_db.h"
#include "dm_mgr.h"
#include "battery_util.h"
#include "gps.h"
#include "keyboard.h"
#include "web_config.h"
#include "debug_flags.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <time.h>
#include <math.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <SD.h>
#if defined(DEVICE_TLORA_PAGER_TFT)
#include <AudioBoard.h>
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
#include <driver/i2s.h>
#endif
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#include <M5Cardputer.h>
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
static lv_obj_t *s_channelBtns[MESH_CHANNELS] = {};
static lv_obj_t *s_channelLabels[MESH_CHANNELS] = {};
static bool s_channelNeedsAttention[MESH_CHANNELS] = {};
static lv_obj_t *s_channelStrip = nullptr;
static lv_obj_t *s_channelList = nullptr;
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
static lv_obj_t *s_legendModal = nullptr;
static lv_obj_t *s_liveModal = nullptr;
static lv_obj_t *s_liveList = nullptr;
static lv_obj_t *s_dmModal = nullptr;
static lv_obj_t *s_dmConvList = nullptr;
static lv_obj_t *s_dmMsgList = nullptr;
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
static lv_obj_t *s_nodesListRows[MAX_NODES] = {};
static int s_nodesListRowCount = 0;
static NodeEntry s_nodesSnapshot[MAX_NODES] = {};
static int s_nodesSnapshotCount = 0;
static int s_nodesSelected = -1;
static int s_activeChannel = 0;
static int s_lastRenderedChannel = -1;
static int s_lastRenderedCount = -1;
static int s_lastRenderedLiveCount = -1;
static int s_lastRenderedLiveScrollOff = -1;
static int s_cfgSelection = 0;
static int s_cfgActionCount = 0;
static int s_cfgActions[14] = {};
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
static char s_selectedMsgText[MSG_CHARS + 1] = "";
static uint32_t s_myNodeId = 0;

enum ComposeTarget : uint8_t {
    COMPOSE_TARGET_CHANNEL = 0,
    COMPOSE_TARGET_DM = 1,
};

enum HeltecNavTarget : uint8_t {
    HELTEC_NAV_CFG = 0,
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
static bool s_pagerChatCursorMode = false;
static int s_pagerChatCursorDisplayIndex = -1;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static bool s_cardputerMainChatPanelFocused = false;
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

static void refreshChatView(bool force = false);
static void collectChatRows(const DisplayLine **rows, int &rowCount);
static void buildChatDisplayOrder(const DisplayLine *const *rows, int rowCount,
                                  int *displayOrder, int &displayCount);
static void refreshHeaderTime(bool force = false);
static void refreshHeaderStatus(bool force = false);
static void refreshChannelGlow(bool force = false);
static void pumpKeyboardInput();
static void openComposePrompt(uint32_t replyPacketId = 0, const char *replyText = nullptr);
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
static void onCfgActionRowPressed(lv_event_t *e);
static void openLegendModal();
static void closeLegendModal();
static void onLegendClosePressed(lv_event_t *e);
static void openLiveModal();
static void closeLiveModal();
static void onHeltecBottomNavPressed(lv_event_t *e);
static void populateHeltecBottomNav(lv_obj_t *bar, int activeTarget);
static void appendHeltecBottomNav(lv_obj_t *parent, int activeTarget);
static void refreshLiveView(bool force = false);
static void openDmModal();
static void closeDmModal();
static void refreshDmModal(bool force = false);
static void onDmConversationPressed(lv_event_t *e);
static void openDmNodePicker();
static void closeDmNodePicker();
static void refreshDmNodePicker(bool force = false);
static void snapshotNodesForDmPicker();
static const NodeEntry *selectedDmNodeForPicker();
static void dmNodePickerApplyFilter();
static bool dmNodePickerContainsNoCase(const char *text, const char *needle);
static bool shouldHideChatLine(const char *text);
static void openNodesModal();
static void closeNodesModal();
static void snapshotNodesForModal();
static void refreshNodesListSelection();
static void refreshNodesDetails();
static void onNodeSnapshotPressed(lv_event_t *e);
static bool nodesSnapshotContains(uint32_t nodeId);
static const NodeEntry *currentNodesSelection();
static void refreshNodesMap(const NodeEntry *node);
static void onWebCfgSaved();
static bool pollMeshRx();
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
static void drawBootSplash();
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

enum CfgActionId {
    CFG_ACTION_WEBCFG = 0,
    CFG_ACTION_EXPORT,
    CFG_ACTION_IMPORT,
    CFG_ACTION_THEME,
    CFG_ACTION_ANNOUNCE,
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
        rgb565(0xfd, 0xf6, 0xe3), rgb565(0xfd, 0xf6, 0xe3), rgb565(0xee, 0xe8, 0xd5), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Light"},
    {UI_THEME_CRIMSON, UI_MODE_DARK,
        rgb565(0x06, 0x0f, 0x24), rgb565(0x12, 0x24, 0x4c), rgb565(0x1b, 0x33, 0x63), rgb565(0xff, 0x4a, 0x58),
        "Crimson Blue Dark"},
    {UI_THEME_CRIMSON, UI_MODE_LIGHT,
        rgb565(0xf3, 0xf7, 0xff), rgb565(0xf8, 0xfb, 0xff), rgb565(0xe6, 0xef, 0xff), rgb565(0xc6, 0x28, 0x39),
        "Crimson Blue Light"},
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

static bool pagerAudioInitI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#if defined(I2S_COMM_FORMAT_STAND_I2S)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
    cfg.communication_format = I2S_COMM_FORMAT_I2S_MSB;
#endif
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

static bool tdeckAudioInitI2S() {
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#if defined(I2S_COMM_FORMAT_STAND_I2S)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#else
    cfg.communication_format = I2S_COMM_FORMAT_I2S_MSB;
#endif
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

    if (s_cfg.uiTheme == UI_THEME_EARTHEN) {
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            s_ui = {
                0xF7DE, 0xE6BA, 0xE658, 0xFFDF, 0xF75C, 0xEEB9,
                0x4228, 0xB40B, 0x7B6D, 0xBD14, 0xCDB6, 0xF75C, 0xEEB9,
                0xB40B, 0xB40B, 0x31A6, 0x6B4D, 0xFFFF, 0x39C7,
                0xDDF7, 0xB40B, 0x9B65, 0xA3C8, 0x8C30,
                0x3666, 0xBC40, 0xA000,
                0xE6DA, 0xFFDF, 0xF75C, 0xCDB6, 0xDE58, 0x4228, 0x6B4D, 0x9CD3
            };
        } else {
            s_ui = {
                0x1082, 0x2104, 0x18C3, 0x2104, 0x2945, 0x3186,
                0xFDD0, 0xE4A8, 0x8C71, 0x5AEB, 0x736D, 0x2945, 0x39A7,
                0xD38B, 0xD38B, 0xFFDF, 0xC618, 0xFFFF, 0xF7DE,
                0x6B4D, 0xC38A, 0xE4A8, 0xB40B, 0xA514,
                0x3666, 0xED80, 0xA000,
                0x18A3, 0x4228, 0x2966, 0x6B2C, 0x83AE, 0xFFDF, 0xDEBA, 0xBDF7
            };
        }
    } else if (s_cfg.uiTheme == UI_THEME_EVERGREEN) {
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            s_ui = {
                0xE73C, 0xD697, 0xC5F4, 0xF7DE, 0xE71B, 0xDEB9,
                0x2148, 0xA321, 0x5B0D, 0xA4F2, 0xBDB4, 0xE71B, 0xD677,
                0x2D2A, 0x2D2A, 0x2148, 0x636E, 0xFFFF, 0x2148,
                0x2D2A, 0x45AD, 0x1CAA, 0x2148, 0x7BAF,
                0x2DA6, 0xBC40, 0xA000,
                0xD697, 0xF7DE, 0xEF7C, 0xA4F2, 0xBDB4, 0x2148, 0x4AED, 0x7C31
            };
        } else {
            s_ui = {
                0x00A8, 0x19EC, 0x114A, 0x11AA, 0x1A2C, 0x1A0B,
                0xFFFF, 0xFD20, 0x8CF1, 0x3B8F, 0x4C31, 0x1A0B, 0x2B2D,
                0x55B0, 0x55B0, 0xFFFF, 0xA554, 0xFFFF, 0xE77D,
                0x2AED, 0x55B0, 0x86FF, 0xE73C, 0xC69A,
                0x3666, 0xED80, 0xA000,
                0x00A8, 0x228D, 0x1169, 0x4C31, 0x64D4, 0xFFFF, 0xB69A, 0x9D75
            };
        }
    } else if (s_cfg.uiTheme == UI_THEME_SOLARIZED) {
        const uint16_t base03 = rgb565(0x00, 0x2b, 0x36);
        const uint16_t base02 = rgb565(0x07, 0x36, 0x42);
        const uint16_t base01 = rgb565(0x58, 0x6e, 0x75);
        const uint16_t base00 = rgb565(0x65, 0x7b, 0x83);
        const uint16_t base0 = rgb565(0x83, 0x94, 0x96);
        const uint16_t base1 = rgb565(0x93, 0xa1, 0xa1);
        const uint16_t base2 = rgb565(0xee, 0xe8, 0xd5);
        const uint16_t base3 = rgb565(0xfd, 0xf6, 0xe3);
        const uint16_t yellow = rgb565(0xb5, 0x89, 0x00);
        const uint16_t orange = rgb565(0xcb, 0x4b, 0x16);
        const uint16_t red = rgb565(0xdc, 0x32, 0x2f);
        const uint16_t magenta = rgb565(0xd3, 0x36, 0x82);
        const uint16_t violet = rgb565(0x6c, 0x71, 0xc4);
        const uint16_t blue = rgb565(0x26, 0x8b, 0xd2);
        const uint16_t cyan = rgb565(0x2a, 0xa1, 0x98);
        const uint16_t green = rgb565(0x85, 0x99, 0x00);

        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            s_ui = {
                base3, base2, base2, base3, base2, rgb565(0xe7, 0xe1, 0xcf),
                blue, orange, base1, base1, base0, base2, base2,
                cyan, blue, base01, base00, base3, base01,
                rgb565(0xe8, 0xe2, 0xd0), cyan, blue, yellow, violet,
                green, yellow, red,
                base2, base3, rgb565(0xf8, 0xf1, 0xdd), base1, base0, blue, cyan, base00
            };
        } else {
            s_ui = {
                base03, base02, base02, base02, rgb565(0x0c, 0x3c, 0x47), rgb565(0x11, 0x45, 0x52),
                blue, orange, base01, base01, base00, base02, base02,
                cyan, yellow, base1, base0, base3, base1,
                rgb565(0x0e, 0x46, 0x55), cyan, blue, yellow, violet,
                green, yellow, red,
                base03, base02, rgb565(0x0b, 0x40, 0x4b), base01, base00, base3, cyan, base0
            };
        }
    } else if (s_cfg.uiTheme == UI_THEME_CRIMSON) {
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            s_ui = {
                rgb565(0xf3, 0xf7, 0xff), rgb565(0xdc, 0xe8, 0xff), rgb565(0xcf, 0xdf, 0xff), rgb565(0xf8, 0xfb, 0xff), rgb565(0xe6, 0xef, 0xff), rgb565(0xd6, 0xe3, 0xff),
                rgb565(0x1e, 0x5f, 0xd1), rgb565(0xc6, 0x28, 0x39), rgb565(0x5f, 0x73, 0xa0), rgb565(0xb0, 0xc1, 0xe4), rgb565(0x92, 0xaa, 0xd7), rgb565(0xf1, 0xf6, 0xff), rgb565(0xe2, 0xed, 0xff),
                rgb565(0xc6, 0x28, 0x39), rgb565(0x2c, 0x74, 0xea), rgb565(0x1b, 0x24, 0x3d), rgb565(0x5c, 0x6c, 0x8f), rgb565(0xff, 0xff, 0xff), rgb565(0x1b, 0x2d, 0x52),
                rgb565(0xda, 0xe8, 0xff), rgb565(0x2f, 0x78, 0xf0), rgb565(0xb4, 0x21, 0x33), rgb565(0x1f, 0x5c, 0xc3), rgb565(0x8d, 0x9d, 0xbe),
                rgb565(0x1b, 0x8f, 0x42), rgb565(0xb0, 0x7a, 0x00), rgb565(0x9f, 0x1f, 0x2f),
                rgb565(0xde, 0xe9, 0xff), rgb565(0xf5, 0xe0, 0xe7), rgb565(0xf7, 0xfb, 0xff), rgb565(0xa5, 0xbb, 0xe7), rgb565(0xdb, 0x4b, 0x5a), rgb565(0x1f, 0x2d, 0x4d), rgb565(0x5d, 0x6e, 0x95), rgb565(0x7d, 0x8e, 0xb2)
            };
        } else {
            s_ui = {
                rgb565(0x06, 0x0f, 0x24), rgb565(0x0e, 0x1b, 0x3a), rgb565(0x11, 0x23, 0x48), rgb565(0x12, 0x24, 0x4c), rgb565(0x1b, 0x33, 0x63), rgb565(0x23, 0x43, 0x7d),
                rgb565(0x42, 0x8f, 0xff), rgb565(0xff, 0x4a, 0x58), rgb565(0x8d, 0xa6, 0xd6), rgb565(0x35, 0x4a, 0x75), rgb565(0x4d, 0x66, 0x98), rgb565(0x10, 0x20, 0x44), rgb565(0x17, 0x2c, 0x5a),
                rgb565(0xff, 0x4a, 0x58), rgb565(0x52, 0xa3, 0xff), rgb565(0xff, 0xff, 0xff), rgb565(0xb9, 0xc8, 0xe7), rgb565(0xff, 0xff, 0xff), rgb565(0xe9, 0xf1, 0xff),
                rgb565(0x1a, 0x36, 0x68), rgb565(0x52, 0xa3, 0xff), rgb565(0xff, 0x6a, 0x74), rgb565(0x57, 0xa7, 0xff), rgb565(0x7f, 0x91, 0xb9),
                rgb565(0x39, 0xc9, 0x69), rgb565(0xff, 0xbf, 0x3d), rgb565(0xff, 0x58, 0x58),
                rgb565(0x07, 0x16, 0x36), rgb565(0x2e, 0x0d, 0x24), rgb565(0x15, 0x27, 0x54), rgb565(0x3b, 0x57, 0x8f), rgb565(0xff, 0x63, 0x70), rgb565(0xf4, 0xf8, 0xff), rgb565(0xb9, 0xc9, 0xe9), rgb565(0x8a, 0x9c, 0xc4)
            };
        }
    } else {
        if (s_cfg.uiMode == UI_MODE_LIGHT) {
            s_ui = {
                rgb565(0xff, 0xf7, 0xfa), rgb565(0xf1, 0xd8, 0xe3), rgb565(0xe8, 0xc2, 0xd5),
                rgb565(0xff, 0xfd, 0xfe), rgb565(0xf8, 0xee, 0xf3), rgb565(0xf3, 0xe0, 0xea),
                0x3127, 0xC983, 0x73AE, 0xBC92, 0xCD34, 0xFF1B, 0xFCD2,
                0xB964, 0xB964, 0x20E6, 0x62CC, 0xFFFF, 0x2927,
                0xB964, 0xDA8E, 0x2C8D, 0x2927, 0x8B2F,
                0x2DA6, 0xBC40, 0xA000,
                0xFE97, 0xFFDF, 0xFF9D, 0xBCB2, 0xCD54, 0x2927, 0x6AAB, 0x83AE
            };
        } else {
            s_ui = {
                0x0843, 0x18A7, 0x1045, 0x1065, 0x18A7, 0x1846,
                0xFFFF, 0xF46B, 0xA4B2, 0x39A8, 0x4A2A, 0x1846, 0x7228,
                0xDA8E, 0xDA8E, 0xFFFF, 0xB596, 0xFFFF, 0xF79E,
                0x7228, 0xDA8E, 0x66FF, 0xDEFB, 0xCE59,
                0x2DA6, 0xFD20, 0xA000,
                0x0801, 0x49C8, 0x1023, 0x6AAE, 0x83B2, 0xFFFF, 0xF6FB, 0xB596
            };
        }
    }

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
        case CFG_ACTION_EXPORT:
            snprintf(buf, bufLen, "Export Config");
            break;
        case CFG_ACTION_IMPORT:
            snprintf(buf, bufLen, "Import Config");
            break;
        case CFG_ACTION_THEME:
            snprintf(buf, bufLen, "Theme: %s", uiThemePresetNameFromCfg());
            break;
        case CFG_ACTION_ANNOUNCE:
            snprintf(buf, bufLen, "Send NODEINFO Broadcast");
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
    return actionId == CFG_ACTION_IMPORT
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
            if (s_screenAsleep) {
                wakeScreen();
            } else {
                sleepScreen("BOOT button");
            }
            return true;
        }
    }
#else
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
    p.putBool("cannedEn", s_cfg.cannedEnabled);
    p.putString("cannedMsgs", s_cfg.cannedMessages);
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

    String nodeLong = prefs.getString("nodeLong", "");
    if (nodeLong.length()) {
        strncpy(s_cfg.nodeLong, nodeLong.c_str(), sizeof(s_cfg.nodeLong) - 1);
        s_cfg.nodeLong[sizeof(s_cfg.nodeLong) - 1] = '\0';
    }
    String nodeShort = prefs.getString("nodeShort", "");
    if (nodeShort.length()) {
        strncpy(s_cfg.nodeShort, nodeShort.c_str(), sizeof(s_cfg.nodeShort) - 1);
        s_cfg.nodeShort[sizeof(s_cfg.nodeShort) - 1] = '\0';
    }

    float f = prefs.getFloat("loraFreq", 0.0f);
    if (f > 0.0f) s_cfg.loraFreq = f;
    f = prefs.getFloat("loraBw", 0.0f);
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

    String region = prefs.getString("region", "");
    if (region.length()) {
        strncpy(s_cfg.region, region.c_str(), sizeof(s_cfg.region) - 1);
        s_cfg.region[sizeof(s_cfg.region) - 1] = '\0';
    }
    String tz = prefs.getString("tzDef", "");
    if (tz.length()) {
        strncpy(s_cfg.tzDef, tz.c_str(), sizeof(s_cfg.tzDef) - 1);
        s_cfg.tzDef[sizeof(s_cfg.tzDef) - 1] = '\0';
    }
    String ntp = prefs.getString("ntpServer", "");
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
    String mqttServer = prefs.getString("mqttServer", "");
    if (mqttServer.length()) {
        strncpy(s_cfg.mqttServer, mqttServer.c_str(), sizeof(s_cfg.mqttServer) - 1);
        s_cfg.mqttServer[sizeof(s_cfg.mqttServer) - 1] = '\0';
    }
    String mqttUser = prefs.getString("mqttUser", "");
    if (mqttUser.length()) {
        strncpy(s_cfg.mqttUser, mqttUser.c_str(), sizeof(s_cfg.mqttUser) - 1);
        s_cfg.mqttUser[sizeof(s_cfg.mqttUser) - 1] = '\0';
    }
    String mqttPass = prefs.getString("mqttPass", "");
    if (mqttPass.length()) {
        strncpy(s_cfg.mqttPass, mqttPass.c_str(), sizeof(s_cfg.mqttPass) - 1);
        s_cfg.mqttPass[sizeof(s_cfg.mqttPass) - 1] = '\0';
    }
    String mqttRoot = prefs.getString("mqttRoot", "");
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
    ul = prefs.getULong("telEnvIntv", 0);
    if (ul) s_cfg.telEnvIntervalS = ul;

    if (prefs.isKey("cannedEn")) s_cfg.cannedEnabled = prefs.getBool("cannedEn");
    String canned = prefs.getString("cannedMsgs", "");
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

    String wifiSsid = prefs.getString("wifiSsid", "");
    if (wifiSsid.length()) {
        strncpy(s_cfg.wifiSsid, wifiSsid.c_str(), sizeof(s_cfg.wifiSsid) - 1);
        s_cfg.wifiSsid[sizeof(s_cfg.wifiSsid) - 1] = '\0';
    }
    String wifiPass = prefs.getString("wifiPass", "");
    if (wifiPass.length()) {
        strncpy(s_cfg.wifiPass, wifiPass.c_str(), sizeof(s_cfg.wifiPass) - 1);
        s_cfg.wifiPass[sizeof(s_cfg.wifiPass) - 1] = '\0';
    }
    s_webCfgEnabled = prefs.getBool("webCfgEnabled", false);

    prefs.end();
}

static void loadChannelsFromPrefs() {
    Preferences cp;
    if (!cp.begin("mesh_ch", true)) return;

    for (int i = 0; i < MESH_CHANNELS; i++) {
        char key[8];

        snprintf(key, sizeof(key), "n%d", i);
        String name = cp.getString(key, "");
        name.trim();
        if (name.length() > 0 && name.length() < sizeof(CHANNEL_KEYS[i].name_buf)) {
            strncpy(CHANNEL_KEYS[i].name_buf, name.c_str(), sizeof(CHANNEL_KEYS[i].name_buf) - 1);
            CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
            CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
        }

        snprintf(key, sizeof(key), "k%d", i);
        uint8_t keyBuf[32];
        size_t keyLen = cp.getBytes(key, keyBuf, sizeof(keyBuf));
        if (keyLen > 0) {
            memcpy(CHANNEL_KEYS[i].key, keyBuf, keyLen);
            CHANNEL_KEYS[i].keyLen = (uint8_t)keyLen;
        }

        snprintf(key, sizeof(key), "r%d", i);
        uint8_t role = cp.getUChar(key, 0xFF);
        if (role != 0xFF) CHANNEL_KEYS[i].role = role;
    }

    cp.end();
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
}

static void recomputeChannelHashes() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        const char *name = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
        CHANNEL_KEYS[i].hash = computeChannelHash(name, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
    }
}

static void formatReplyPreview(const char *src, char *dst, size_t dstLen) {
    if (!dst || dstLen == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src == ' ') src++;
    size_t w = 0;
    while (*src && w + 1 < dstLen) {
        char c = *src++;
        if (c == '\r' || c == '\n') c = ' ';
        dst[w++] = c;
    }
    dst[w] = '\0';
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
    s_selectedMsgReplyPacketId = resolveReplyPacketId(rows, rowCount, rowIdx);

    const char *txt = rows[rowIdx] ? rows[rowIdx]->text : "";
    if (txt) {
        strncpy(s_selectedMsgText, txt, sizeof(s_selectedMsgText) - 1);
        s_selectedMsgText[sizeof(s_selectedMsgText) - 1] = '\0';
    } else {
        s_selectedMsgText[0] = '\0';
    }

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

static void openComposePrompt(uint32_t replyPacketId, const char *replyText) {
    if (!s_rootScreen) return;
    if (s_activeChannel < 0 || s_activeChannel >= MESH_CHANNELS) return;

    // Pager can open compose via paths that don't pass reply args; recover from current selection.
    if (replyPacketId == 0 && (!replyText || !replyText[0])
        && s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
        replyPacketId = s_selectedMsgReplyPacketId;
        replyText = s_selectedMsgText;
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    const lv_font_t *composeBodyFont = &lv_font_montserrat_12;
    const lv_coord_t composeInputH = (lv_coord_t)(lv_font_get_line_height(composeBodyFont) + 6);
    const lv_coord_t composeInputPadTop = 1;
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#elif defined(DEVICE_TDECK)
    const lv_font_t *composeBodyFont = &lv_font_montserrat_10;
    const lv_coord_t composeInputH = (lv_coord_t)(((lv_font_get_line_height(composeBodyFont) + 10) * 11) / 10);
    const lv_coord_t composeInputPadTop = max<lv_coord_t>(1, (composeInputH - (lv_coord_t)lv_font_get_line_height(composeBodyFont)) / 2);
    const lv_coord_t composeModalBottomPad = 2;
    const lv_coord_t composeModalRowPad = 1;
#else
    const lv_font_t *composeBodyFont = &lv_font_montserrat_10;
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
        char preview[MSG_CHARS + 1];
        formatReplyPreview(replyText, preview, sizeof(preview));
        lv_obj_t *replyLbl = lv_label_create(s_composeModal);
        lv_obj_set_width(replyLbl, lv_pct(100));
        lv_obj_set_style_text_font(replyLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(replyLbl, lv_color_hex(0xA7C7FF), 0);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(replyLbl, preview[0] ? preview : "(message)");
    }

    s_composeInput = lv_textarea_create(s_composeModal);
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_height(s_composeInput, 38);
    lv_obj_set_style_text_font(s_composeInput, &lv_font_montserrat_12, 0);
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
    int modalH = isReply ? 96 : 72;
#if defined(DEVICE_TDECK)
    modalH = isReply ? 92 : 70;
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
        char preview[MSG_CHARS + 1];
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
        lv_obj_set_style_text_font(replyLbl, composeBodyFont, 0);
        lv_obj_set_style_text_color(replyLbl, lv_color_hex(0xA7C7FF), 0);
        lv_label_set_long_mode(replyLbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(replyLbl, preview[0] ? preview : "(message)");
    }

    lv_obj_t *composeInputHost = s_composeModal;
#if defined(DEVICE_TDECK)
    lv_obj_t *composeCenterBand = lv_obj_create(s_composeModal);
    lv_obj_set_width(composeCenterBand, lv_pct(100));
    lv_obj_set_flex_grow(composeCenterBand, 1);
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
#if defined(DEVICE_TDECK)
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
    lv_textarea_set_one_line(s_composeInput, true);
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, "Type message...");

    lv_obj_t *hint = lv_label_create(s_composeModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, composeBodyFont, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_pad_top(hint, 0, 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_set_style_pad_bottom(hint, 0, 0);
#elif defined(DEVICE_TDECK)
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
    openComposePrompt(0, nullptr);
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
#if HAS_SD_CARD && !defined(DEVICE_HELTEC_V4_EXPANSION)
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_EXPORT;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_IMPORT;
#endif
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_THEME;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_ANNOUNCE;
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
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2A4E8F), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
            if (s_cfg.uiMode == UI_MODE_LIGHT) {
                lv_obj_set_style_text_color(row, lv_color_hex(0xE8F1FF), 0);
            } else {
                lv_obj_set_style_text_color(row, lv_color_hex(0xEAF3FF), 0);
            }
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
    int infoCount = 0;

    bool hasPubKey = false;
    for (int i = 0; i < 32; i++) {
        if (myPubKey[i] != 0) {
            hasPubKey = true;
            break;
        }
    }

    snprintf(info[infoCount++], sizeof(info[0]), "Node ID: !%08lx", (unsigned long)s_myNodeId);
    snprintf(info[infoCount++], sizeof(info[0]), "Role: %s", cfgDeviceRoleName(s_cfg.deviceRole));
    snprintf(info[infoCount++], sizeof(info[0]), "PKI key: %s", hasPubKey ? "present" : "missing");
    snprintf(info[infoCount++], sizeof(info[0]), "Long: %s", s_cfg.nodeLong);
    snprintf(info[infoCount++], sizeof(info[0]), "Short: %s", s_cfg.nodeShort);
    snprintf(info[infoCount++], sizeof(info[0]), "Freq: %.3f MHz", s_cfg.loraFreq);
    snprintf(info[infoCount++], sizeof(info[0]), "BW %.0f SF %d CR 4/%d", s_cfg.loraBw, s_cfg.loraSf, s_cfg.loraCr);
    snprintf(info[infoCount++], sizeof(info[0]), "Pwr %d dBm Hops %d", s_cfg.loraPower, s_cfg.loraHopLimit);

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

static void populateHeltecBottomNav(lv_obj_t *bar, int activeTarget) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (!bar) return;

    struct NavItem {
        const char *label;
        int target;
    };
    static const NavItem kItems[] = {
        {"Config", HELTEC_NAV_CFG},
        {"Nodes", HELTEC_NAV_NODES},
        {"Live", HELTEC_NAV_LIVE},
        {"Legend", HELTEC_NAV_LEGEND},
    };

    lv_obj_set_style_pad_left(bar, 2, 0);
    lv_obj_set_style_pad_right(bar, 2, 0);
    lv_obj_set_style_pad_top(bar, 1, 0);
    lv_obj_set_style_pad_bottom(bar, 1, 0);
    lv_obj_set_style_pad_column(bar, 3, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, lv_pct(100));
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 1, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        const bool isActive = (activeTarget == kItems[i].target);
        lv_obj_set_style_bg_color(btn,
                                  isActive ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x16386F),
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
        lv_obj_set_style_text_color(label, lv_color_hex(0xD9E8FF), 0);
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

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 26);
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
    if (strncmp(body, "T TLM", 5) == 0) return LIVE_TRAFFIC_TX_TLM;
    if (strncmp(body, "R ", 2) == 0) return LIVE_TRAFFIC_RX_OTHER;
    if (strncmp(body, "T ", 2) == 0) return LIVE_TRAFFIC_TX_OTHER;

    return LIVE_TRAFFIC_DEFAULT;
}

static uint16_t liveLineTrafficColor(const DisplayLine &dl) {
    LiveTrafficClass cls = classifyLiveTraffic(dl);
    switch (cls) {
        case LIVE_TRAFFIC_ERROR:   return TFT_RED;
        case LIVE_TRAFFIC_TX_ACK:  return TFT_GREEN;
        case LIVE_TRAFFIC_RX_ACK:  return (uint16_t)0x57EA;
        case LIVE_TRAFFIC_TX_TEXT: return TFT_YELLOW;
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
        case LIVE_TRAFFIC_TX_OTHER:return (uint16_t)0xFFE0;
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

static lv_color_t tftColorToLv(uint16_t c) {
    uint8_t r = (uint8_t)((((c >> 11) & 0x1F) * 255) / 31);
    uint8_t g = (uint8_t)((((c >> 5) & 0x3F) * 255) / 63);
    uint8_t b = (uint8_t)(((c & 0x1F) * 255) / 31);
    return lv_color_make(r, g, b);
}

static void closeLiveModal() {
    if (s_liveModal) {
        lv_obj_del(s_liveModal);
    }
    s_liveModal = nullptr;
    s_liveList = nullptr;
    s_lastRenderedLiveCount = -1;
    s_lastRenderedLiveScrollOff = -1;
}

static void closeDmModal() {
    closeDmNodePicker();
    if (s_dmModal) {
        lv_obj_del(s_dmModal);
    }
    s_dmModal = nullptr;
    s_dmConvList = nullptr;
    s_dmMsgList = nullptr;
    s_dmConvCount = 0;
    s_dmSelection = -1;
    s_dmRenderedConvCount = -1;
    s_dmRenderedNodeId = 0;
    s_dmRenderedMsgCount = -1;
    s_dmRenderedUnreadTotal = -1;
    memset(s_dmConvRows, 0, sizeof(s_dmConvRows));
    memset(s_dmConvNodeIds, 0, sizeof(s_dmConvNodeIds));
}

static void closeNodesModal() {
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
    s_nodesMapImageSrc[0] = '\0';
    s_nodesList = nullptr;
    s_nodesListRowCount = 0;
    s_nodesSnapshotCount = 0;
    s_nodesSelected = -1;
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
    s_nodesSelected = -1;

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

    if (s_nodesSnapshotCount > 0) {
        s_nodesSelected = 0;
    }
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
    if (s_nodesSnapshotCount <= 0 || s_nodesSelected < 0 || s_nodesSelected >= s_nodesSnapshotCount) {
        return nullptr;
    }
    return &s_nodesSnapshot[s_nodesSelected];
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

static void refreshNodesListSelection() {
    for (int i = 0; i < s_nodesListRowCount; i++) {
        lv_obj_t *row = s_nodesListRows[i];
        if (!row) continue;
        bool selected = (i == s_nodesSelected);
        lv_color_t rowTextColor = (s_cfg.uiMode == UI_MODE_LIGHT)
            ? lv_color_hex(0xE8F1FF)
            : lv_color_hex(0xEAF3FF);
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

    if (s_nodesSnapshotCount <= 0 || s_nodesSelected < 0 || s_nodesSelected >= s_nodesSnapshotCount) {
        lv_label_set_text(s_nodesDetail, "No nodes seen yet.");
        if (s_nodesDetailExtra) lv_label_set_text(s_nodesDetailExtra, "");
        return;
    }

    const NodeEntry &n = s_nodesSnapshot[s_nodesSelected];

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

    char telem[96];
    if (n.hasTelemetry) {
        snprintf(telem, sizeof(telem),
                 "Battery: %.0f%%\nVoltage: %.2f V\nEnvironment: n/a",
                 (double)n.battPct,
                 (double)n.voltage);
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

        lv_label_set_text(s_nodesDetail, leftBuf);
        lv_label_set_text(s_nodesDetailExtra, rightBuf);
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

        lv_label_set_text(s_nodesDetail, buf);
    }
    if (s_nodesInfoPanel) {
        lv_obj_scroll_to_y(s_nodesInfoPanel, 0, LV_ANIM_OFF);
    }
}

static void onNodeSnapshotPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_nodesSnapshotCount) return;
    s_nodesSelected = idx;
    refreshNodesListSelection();
    refreshNodesDetails();
    if (idx >= 0 && idx < s_nodesListRowCount && s_nodesListRows[idx]) {
        lv_obj_scroll_to_view(s_nodesListRows[idx], LV_ANIM_OFF);
    }
}

static void refreshLiveView(bool force) {
    if (!s_liveModal || !s_liveList) return;

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
                    lineColor = TFT_YELLOW;
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
        lv_label_set_text(msg, rendered);
    }

    if (rowCount == 0) {
        lv_obj_t *empty = lv_label_create(s_liveList);
        lv_obj_set_style_text_font(empty, liveBodyFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No live traffic yet");
    }

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
    lv_obj_center(title);

    s_liveList = lv_obj_create(s_liveModal);
    lv_obj_set_width(s_liveList, lv_pct(100));
    lv_obj_set_flex_grow(s_liveList, 1);
    lv_obj_add_flag(s_liveList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_liveList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_liveList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_liveList, lv_color_hex(0x0F2A5C), 0);
    lv_obj_set_style_bg_opa(s_liveList, LV_OPA_50, 0);
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
    lv_label_set_text_fmt(hint, "%s = Back   C = Clear log", modalCloseKeyLabel());
#endif

    refreshLiveView(true);
}

static DmConv *selectedDmConversation() {
    if (s_dmSelection <= 0) return nullptr;
    int convIdx = s_dmSelection - 1;
    if (convIdx < 0 || convIdx >= s_dmConvCount) return nullptr;
    uint32_t nodeId = s_dmConvNodeIds[convIdx];
    if (nodeId == 0) return nullptr;
    return DMs.find(nodeId);
}

static void onDmConversationPressed(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx > s_dmConvCount) return;
    s_dmSelection = idx;
    refreshDmModal(true);
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

static void refreshDmNodePicker(bool force) {
    LV_UNUSED(force);
    if (!s_dmNodePickerModal || !s_dmNodePickerList) return;

    if (s_dmNodePickerTitle) {
        if (s_dmNodeFilterOpen) {
            char title[64];
            snprintf(title, sizeof(title), "New DM: Select Node [%s]", s_dmNodeFilter);
            lv_label_set_text(s_dmNodePickerTitle, title);
        } else {
            lv_label_set_text(s_dmNodePickerTitle, "New DM: Select Node");
        }
    }

    if (s_dmNodePickerHint) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        lv_label_set_text(s_dmNodePickerHint,
                          s_dmNodeFilterOpen
                              ? "Type = Filter   Bksp = Edit Filter   Enter = Start DM   Esc = Back"
                              : "Type = Filter   Enter = Start DM   Esc = Back");
#else
        lv_label_set_text(s_dmNodePickerHint,
                          s_dmNodeFilterOpen
                              ? "Type = Filter   Bksp = Edit/Close Filter   Enter = Start DM"
                              : "Type = Filter   Enter = Start DM   Bksp = Back");
#endif
    }

    lv_obj_clean(s_dmNodePickerList);
    memset(s_dmNodePickerRows, 0, sizeof(s_dmNodePickerRows));

    if (s_dmNodeFilteredCount <= 0) {
        lv_obj_t *empty = lv_label_create(s_dmNodePickerList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
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
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

        char rowText[64];
        const char *longDisp = n.longName[0] ? n.longName : "(unknown)";
        const char *shortDisp = liveShortNameUsable(n.shortName) ? n.shortName : "????";
        snprintf(rowText, sizeof(rowText), "%s (%s)", longDisp, shortDisp);
        lv_label_set_text(lbl, rowText);
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
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "New DM: Select Node");

    s_dmNodePickerList = lv_obj_create(s_dmNodePickerModal);
    lv_obj_set_width(s_dmNodePickerList, lv_pct(100));
    lv_obj_set_flex_grow(s_dmNodePickerList, 1);
    lv_obj_add_flag(s_dmNodePickerList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_dmNodePickerList, LV_DIR_VER);
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
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_label_set_text(hint, "Type = Filter   Enter = Start DM   Esc = Back");
#else
    lv_label_set_text(hint, "Type = Filter   Enter = Start DM   Bksp = Back");
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
    }

    int selectedMsgCount = selected ? selected->count : -1;
    uint32_t selectedNodeId = selected ? selected->nodeId : 0;
    int unreadTotal = DMs.unreadMessageCount();

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

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_font(lbl, dmListFont, 0);
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

            lv_obj_t *lbl = lv_label_create(row);
            lv_obj_set_width(lbl, lv_pct(100));
            lv_obj_set_style_text_font(lbl, dmListFont, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);

            char name[20];
            if (liveShortNameUsable(c->shortName)) {
                snprintf(name, sizeof(name), "%s", c->shortName);
            } else {
                snprintf(name, sizeof(name), "!%08lX", (unsigned long)c->nodeId);
            }

            char rowText[48];
            if (c->unreadCount > 0 && c->nodeId != selectedNodeId) {
                snprintf(rowText, sizeof(rowText), "%s (%u)", name, (unsigned)c->unreadCount);
            } else {
                snprintf(rowText, sizeof(rowText), "%s", name);
            }
            lv_label_set_text(lbl, rowText);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        }
    }

    selected = selectedDmConversation();
    if (!selected) {
        lv_obj_t *empty = lv_label_create(s_dmMsgList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, dmMsgFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty,
                          (s_dmSelection == 0)
                              ? "Press Enter on New DM"
                              : "Select a conversation");
    } else {
        int rowCount = 0;
        for (int row = 0; row < MAX_DM_LINES; row++) {
            const DmLine *dl = DMs.getLine(selected, row, MAX_DM_LINES);
            if (!dl) break;
            rowCount++;

            lv_obj_t *msg = lv_label_create(s_dmMsgList);
            lv_obj_set_width(msg, lv_pct(100));
            lv_obj_set_style_text_font(msg, dmMsgFont, 0);
            lv_obj_set_style_pad_left(msg, 2, 0);
            lv_obj_set_style_pad_right(msg, 4, 0);
            lv_obj_set_style_pad_top(msg, 0, 0);
            lv_obj_set_style_pad_bottom(msg, 0, 0);
            lv_label_set_long_mode(msg, LV_LABEL_LONG_CLIP);

            uint16_t lineColor = dl->color;
            switch (dl->ack) {
                case DmLine::ACKED:
                    lineColor = (s_cfg.uiMode == UI_MODE_LIGHT) ? (uint16_t)0x0320 : TFT_GREEN;
                    break;
                case DmLine::ACKED_RELAY:
                    lineColor = TFT_YELLOW;
                    break;
                case DmLine::NAKED:
                case DmLine::TX_FAILED:
                    lineColor = TFT_RED;
                    break;
                default:
                    break;
            }

            lv_obj_set_style_text_color(msg, tftColorToLv(lineColor), 0);
            lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
            lv_label_set_text(msg, dl->text);
        }

        if (rowCount == 0) {
            lv_obj_t *empty = lv_label_create(s_dmMsgList);
            lv_obj_set_width(empty, lv_pct(100));
            lv_obj_set_style_text_font(empty, dmMsgFont, 0);
            lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
            lv_label_set_text(empty, "No messages yet");
        }
    }

    s_dmRenderedConvCount = s_dmConvCount;
    s_dmRenderedNodeId = selected ? selected->nodeId : 0;
    s_dmRenderedMsgCount = selected ? selected->count : -1;
    s_dmRenderedUnreadTotal = unreadTotal;
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
    int contentW = modalW - 8;
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
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
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
    lv_obj_set_style_text_color(leftTitle, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(leftTitle, "Conversations");

    s_dmConvList = lv_obj_create(leftPanel);
    lv_obj_set_width(s_dmConvList, lv_pct(100));
    lv_obj_set_flex_grow(s_dmConvList, 1);
    lv_obj_add_flag(s_dmConvList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_dmConvList, LV_DIR_VER);
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
    lv_obj_set_style_text_color(rightTitle, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(rightTitle, "Messages");

    s_dmMsgList = lv_obj_create(rightPanel);
    lv_obj_set_width(s_dmMsgList, lv_pct(100));
    lv_obj_set_flex_grow(s_dmMsgList, 1);
    lv_obj_add_flag(s_dmMsgList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_dmMsgList, LV_DIR_VER);
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
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, "Up/Down = Select   Enter = Compose   %s = Back", modalCloseKeyLabel());

    s_dmRenderedConvCount = -1;
    s_dmRenderedNodeId = 0;
    s_dmRenderedMsgCount = -1;
    s_dmRenderedUnreadTotal = -1;
    refreshDmModal(true);
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
    const lv_font_t *nodesListFont = &lv_font_montserrat_12;
    const int nodesListRowH = 28;
#else
    const lv_font_t *nodesDetailFont = &lv_font_montserrat_10;
    const lv_font_t *nodesListFont = &lv_font_montserrat_10;
    const int nodesListRowH = 22;
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
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "NODES");
    lv_obj_center(title);

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
    lv_obj_set_scroll_dir(left, LV_DIR_VER);
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
    lv_obj_set_scroll_dir(s_nodesList, LV_DIR_VER);
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

    if (s_nodesSnapshotCount <= 0) {
        lv_obj_t *empty = lv_label_create(s_nodesList);
        lv_obj_set_width(empty, lv_pct(100));
        lv_obj_set_style_text_font(empty, nodesListFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No nodes seen");
    } else {
        for (int i = 0; i < s_nodesSnapshotCount; i++) {
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
            lv_obj_add_event_cb(row, onNodeSnapshotPressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);

            lv_obj_t *lbl = lv_label_create(row);
            lv_obj_set_width(lbl, lv_pct(100));
            lv_obj_set_style_text_font(lbl, nodesListFont, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);

            char rowText[56];
            const NodeEntry &n = s_nodesSnapshot[i];
            snprintf(rowText, sizeof(rowText), "%s", n.shortName[0] ? n.shortName : "----");
            lv_label_set_text(lbl, rowText);
            lv_obj_center(lbl);

            if (s_nodesListRowCount < MAX_NODES) {
                s_nodesListRows[s_nodesListRowCount++] = row;
            }
        }
    }

    refreshNodesListSelection();
    refreshNodesDetails();

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    appendHeltecBottomNav(s_nodesModal, HELTEC_NAV_NODES);
#else
    lv_obj_t *hint = lv_label_create(s_nodesModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text_fmt(hint, "Up/Down = Select   %s = Back", modalCloseKeyLabel());
#endif
}

static void openLegendModal() {
    if (!s_rootScreen || s_legendModal) return;
    closeDmModal();

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    int modalH = 118;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    modalH = 126;
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    modalH = 126;
#endif
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    const lv_font_t *legendBodyFont = &lv_font_montserrat_12;
#else
    const lv_font_t *legendBodyFont = &lv_font_montserrat_10;
#endif

    s_legendModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_legendModal, modalW, modalH);
    lv_obj_align(s_legendModal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_legendModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_legendModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_legendModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_legendModal, 1, 0);
    lv_obj_set_style_border_color(s_legendModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_legendModal, 6, 0);
    lv_obj_set_style_pad_row(s_legendModal, 4, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_set_style_pad_bottom(s_legendModal, 8, 0);
    lv_obj_set_style_pad_row(s_legendModal, 5, 0);
#endif
    lv_obj_set_flex_flow(s_legendModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_legendModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_legendModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Legend");

#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_t *body = lv_label_create(s_legendModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, legendBodyFont, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(
        body,
        "Touch Navigation:\n"
        "Use bottom buttons for Config, Nodes, Live, Legend.\n"
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
    lv_label_set_text(
        leftCol,
        "(D) Direct Messages\n"
        "(C) Configuration\n"
        "(N) Nodes\n"
        "L(i)ve (C clears log)\n"
        "(Enter) Compose/Reply\n"
        "(Bksp) Clear Selection");

    lv_obj_t *rightCol = lv_label_create(bodyRow);
    lv_obj_set_width(rightCol, lv_pct(50));
    lv_obj_set_style_text_font(rightCol, legendBodyFont, 0);
    lv_obj_set_style_text_color(rightCol, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(rightCol, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(
        rightCol,
        "Transport Symbols:\n"
        "%s Radio Transmission\n"
        "%s MQTT Transmission",
        LV_SYMBOL_RADIO_TINY,
        LV_SYMBOL_GLOBE_TINY);
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
        "L(i)ve (C clears log)\n"
        "(Enter) Compose/Reply\n"
        "(Bksp) Clear Selection\n"
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
    lv_label_set_text_fmt(hint, "%s/C/N/I/L = Close", modalCloseKeyLabel());
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
#else
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
#endif
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Configuration");

    s_cfgHeaderStatus = lv_label_create(header);
    lv_obj_set_width(s_cfgHeaderStatus, lv_pct(58));
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_obj_set_style_text_font(s_cfgHeaderStatus, &lv_font_montserrat_12, 0);
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
    lv_obj_set_scroll_dir(s_cfgActionList, LV_DIR_VER);
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
    lv_obj_set_scroll_dir(s_cfgInfoList, LV_DIR_VER);
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
    lv_obj_set_scroll_dir(s_cfgActionList, LV_DIR_VER);
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
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lv_label_set_text_fmt(hint, "Wheel = Scroll focused panel   Click wheel = Swap panel   %s = Close", modalCloseKeyLabel());
#else
    lv_label_set_text_fmt(hint, "Up/Down = Select   Enter = Run   %s = Close", modalCloseKeyLabel());
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
        uint32_t now = millis();
        bool confirmExpired = (s_cfgConfirmMs == 0) || (uint32_t)(now - s_cfgConfirmMs) > 3000;
        if (s_cfgConfirmAction != actionId || confirmExpired) {
            char actionText[80];
            s_cfgConfirmAction = actionId;
            s_cfgConfirmMs = now;
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Confirm: %s (Enter again)",
                     cfgActionLabel(actionId, actionText, sizeof(actionText)));
            if (s_cfgDebugLog) {
                Serial.printf("[lvgl-cfg] confirm-wait action=%d label=\"%s\"\n", actionId, actionText);
            }
            refreshCfgModal();
            return;
        }
    }

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

                bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
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

        case CFG_ACTION_ANNOUNCE:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec ANNOUNCE");
            webCfgQueueAnnounce();
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "NODEINFO broadcast queued.");
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

static void pumpKeyboardInput() {
    for (int i = 0; i < 8; i++) {
        // Prioritize keyboard keys (especially Enter) before trackball deltas
        // to avoid one-off selection shifts during activation.
        char k = s_keyboard.readKey();
        const char *src = "key";
        if (k == KEY_NONE) {
            k = s_keyboard.readTrackball();
            src = "track";
        }
        if (k == KEY_NONE) {
            if (s_cfgModal && s_cfgAwaitEnterRelease) {
                s_cfgAwaitEnterRelease = false;
                if (s_cfgDebugLog) Serial.println("[lvgl-cfg] enter-release observed");
            }
            break;
        }

        if (s_screenAsleep) {
            wakeScreen();
            return;
        }
        s_lastActivityMs = millis();

#if defined(DEVICE_CARDPUTER_LORA_HAT)
        // Match v1 Cardputer shortcuts: ';' / '.' navigate lists, and
        // the key physically labeled '`' acts as Escape to close modals.
        bool typingContext = s_composeModal || (s_dmNodePickerModal && s_dmNodeFilterOpen);
        k = remapCardputerUiKey(k, !typingContext);
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
            if (isModalCloseKey(k)) {
                closeCfgModal();
                continue;
            }
#if defined(DEVICE_TLORA_PAGER_TFT)
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
                    const int delta = kPagerWheelChatNav ? scrollStep : -scrollStep;
                    lv_obj_scroll_by(s_cfgInfoList, 0, delta, LV_ANIM_OFF);
                    continue;
                }
#endif
                if (kPagerWheelChatNav) {
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
                    const int delta = kPagerWheelChatNav ? -scrollStep : scrollStep;
                    lv_obj_scroll_by(s_cfgInfoList, 0, delta, LV_ANIM_OFF);
                    continue;
                }
#endif
                if (kPagerWheelChatNav) {
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
                if (s_cfgAwaitEnterRelease) {
                    if (s_cfgDebugLog) Serial.println("[lvgl-cfg] enter-block awaiting release");
                    continue;
                }
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
                        if (kPagerWheelChatNav) {
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
                    const NodeEntry *n = selectedDmNodeForPicker();
                    if (n && n->nodeId != 0) {
                        closeDmNodePicker();
                        openComposePromptForDm(n->nodeId);
                    }
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

            if (isModalCloseKey(k)) {
                closeDmModal();
                continue;
            }
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                int totalRows = s_dmConvCount + 1;
                if (totalRows > 0) {
                    int next = s_dmSelection;
                    if (kPagerWheelChatNav) {
                        next += (k == KEY_SCROLL_UP) ? 1 : -1;
                    } else {
                        next += (k == KEY_SCROLL_UP) ? -1 : 1;
                    }
                    if (next < 0) next = 0;
                    if (next >= totalRows) next = totalRows - 1;
                    if (next != s_dmSelection) {
                        s_dmSelection = next;
                        refreshDmModal(true);
                    }
                }
                continue;
            }
            if (k == KEY_ENTER) {
                if (s_dmSelection == 0) {
                    openDmNodePicker();
                } else {
                    DmConv *selected = selectedDmConversation();
                    if (selected) {
                        openComposePromptForDm(selected->nodeId);
                    }
                }
                continue;
            }
            continue;
        }

        if (s_nodesModal) {
            if (isModalCloseKey(k)) {
                closeNodesModal();
                continue;
            }
            if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                int nextSelected = s_nodesSelected;
                if (kPagerWheelChatNav) {
                    // Pager wheel orientation: UP should move to the next row.
                    nextSelected += (k == KEY_SCROLL_UP) ? 1 : -1;
                } else {
                    nextSelected += (k == KEY_SCROLL_UP) ? -1 : 1;
                }

                if (nextSelected < 0) nextSelected = 0;
                if (nextSelected >= s_nodesSnapshotCount) nextSelected = s_nodesSnapshotCount - 1;

                if (nextSelected != s_nodesSelected
                    && nextSelected >= 0
                    && nextSelected < s_nodesSnapshotCount) {
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
            if (k == KEY_SCROLL_UP && s_liveList) {
                lv_obj_scroll_by(s_liveList, 0, -18, LV_ANIM_OFF);
                continue;
            }
            if (k == KEY_SCROLL_DN && s_liveList) {
                lv_obj_scroll_by(s_liveList, 0, 18, LV_ANIM_OFF);
                continue;
            }
            continue;
        }

        if (s_legendModal) {
            if (isModalCloseKey(k)
                || k == 'l' || k == 'L') {
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
            if (k == 'i' || k == 'I') {
                closeLegendModal();
                openLiveModal();
                continue;
            }
            if (k == 'n' || k == 'N') {
                closeLegendModal();
                openNodesModal();
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
                    }
                    if (s_cardputerMainChatPanelFocused) {
                        s_cardputerMainChatPanelFocused = false;
                    }
                    continue;
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
                        if (k == KEY_SCROLL_DN) {
                            s_cardputerMainChatPanelFocused = true;
                            s_pagerChatCursorMode = true;
                            if (!pagerSelectChatCursorIndex(-1)) {
                                s_pagerChatCursorMode = false;
                            }
                        }
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
                        continue;
                    }

#else
                    if (s_pagerChatCursorMode) {
                        int navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
                        pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + navDelta);
                    } else if (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                        int navDelta = (k == KEY_SCROLL_UP) ? 1 : -1;
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
                openLegendModal();
            } else if (k == 'd' || k == 'D') {
                openDmModal();
            } else if (k == 'c' || k == 'C') {
                openCfgModal();
            } else if (k == 'n' || k == 'N') {
                openNodesModal();
            } else if (k == 'i' || k == 'I') {
                openLiveModal();
            } else if (k == KEY_ENTER && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
                if (!s_cardputerMainChatPanelFocused) {
                    openComposePrompt(0, nullptr);
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

    s_selectedMsgReplyPacketId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    const char *txt = lv_label_get_text(label);
    if (txt) {
        strncpy(s_selectedMsgText, txt, sizeof(s_selectedMsgText) - 1);
        s_selectedMsgText[sizeof(s_selectedMsgText) - 1] = '\0';
    } else {
        s_selectedMsgText[0] = '\0';
    }

    refreshChatComposeButtonState();

    // Defer redraw to the normal loop to avoid deleting the active target in-event.
    s_lastRenderedChannel = -1;
}

static void onWebCfgSaved() {
    uint8_t prevTheme = s_appliedUiTheme;
    uint8_t prevMode = s_appliedUiMode;

    persistConfigToPrefs();
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

    if (!cfgExport(s_cfg)) {
        Serial.println("[cfg] web save export failed");
    }

    if ((prevTheme != s_appliedUiTheme || prevMode != s_appliedUiMode) && s_rootScreen) {
        scheduleThemeRebuild(s_cfgModal != nullptr);
    }
}

static void startWebConfigAuto() {
    if (!s_webCfgEnabled) {
        Serial.println("[web] auto start disabled");
        return;
    }
    if (webCfgRunning()) return;
    bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
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
    lv_disp_flush_ready(disp);
}

static void lvglTouchRead(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
#if TOUCH_POLL_ENABLED
    int32_t tx = 0;
    int32_t ty = 0;
    if (displayDev().getTouch(&tx, &ty)) {
        if (s_screenAsleep) {
            wakeScreen();
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

    displayDev().setFont(&fonts::Orbitron_Light_32);
    displayDev().setTextSize(0.82f);
    displayDev().setTextColor(titleCol, cardBg);
    int fwW = displayDev().textWidth(firmwareName);
    displayDev().drawString(firmwareName, cardX + max(0, (cardW - fwW) / 2), cardY + 10);

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
    // Keep Cardputer splash lightweight and avoid depending on LGFX_TDeck-specific helpers.
    displayDev().fillCircle(cardX + (cardW / 2), cardY + (cardH / 2) - 6, (int)(10.0f * flowerScale), titleCol);
#else
    drawCamelliaMark(cardX + (cardW / 2),
                     cardY + (cardH / 2) - 6,
                     flowerScale);
#endif

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

    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = s_channelBtns[i];
        if (!btn) continue;

        bool active = (i == s_activeChannel);
        bool animate = s_channelNeedsAttention[i] && !active;
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
                    ? (s_cfg.uiMode == UI_MODE_DARK ? lv_color_hex(0x0B1E44) : lv_color_hex(0xEAF3FF))
                    : lv_color_hex(0xD9E8FF),
                0);
        }

        if (animate) {
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
        }
    }
}

static void applyChannelButtonTheme() {
    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = s_channelBtns[i];
        if (!btn) continue;

        bool active = (i == s_activeChannel);
        lv_obj_set_style_bg_color(btn, active ? lv_color_hex(0x2A4FB4) : lv_color_hex(0x102750), 0);
        lv_obj_set_style_bg_opa(btn, active ? LV_OPA_90 : LV_OPA_60, 0);
        lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn, active ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C), 0);

        lv_obj_t *lbl = s_channelLabels[i];
        if (lbl) {
            lv_obj_set_style_text_color(
                lbl,
                active
                    ? (s_cfg.uiMode == UI_MODE_DARK ? lv_color_hex(0x0B1E44) : lv_color_hex(0xEAF3FF))
                    : lv_color_hex(0xD9E8FF),
                0);
        }
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
    s_selectedMsgReplyPacketId = 0;
    s_selectedMsgText[0] = '\0';
    s_channelNeedsAttention[channelIdx] = false;
    Channels.setActive(channelIdx);
    applyChannelButtonTheme();
    refreshChannelGlow(true);
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
    if (s_channelList && s_channelBtns[channelIdx]) {
        lv_obj_scroll_to_view(s_channelBtns[channelIdx], LV_ANIM_OFF);
    }
#endif
    refreshChatView(true);
}

static void onChannelPressed(lv_event_t *e) {
    int channelIdx = (int)(intptr_t)lv_event_get_user_data(e);
    setActiveChannel(channelIdx);
}

static const char *channelName(int idx) {
    if (idx < 0 || idx >= MESH_CHANNELS) return "";
    const ChannelKey &ck = CHANNEL_KEYS[idx];
    if (ck.name_buf[0]) return ck.name_buf;
    return ck.name ? ck.name : "";
}

static void sizeChannelButtonToLabel(int idx) {
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
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
    strncpy(s_lastHeaderTime, buf, sizeof(s_lastHeaderTime) - 1);
    s_lastHeaderTime[sizeof(s_lastHeaderTime) - 1] = '\0';
}

static inline lv_color_t headerGoodGreenColor() {
    return (s_cfg.uiMode == UI_MODE_LIGHT)
        ? lv_color_hex(0x2C7A3B)
        : lv_color_hex(0x84E07A);
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

    lv_bar_set_value(s_chatHeaderBattBar, battPct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerGoodGreenColor(), LV_PART_INDICATOR);
    lv_label_set_text_fmt(s_chatHeaderBattText, "%u%%", (unsigned)battPct);

    if (gpsEnabled && gpsFix) {
        lv_label_set_text_fmt(s_chatHeaderGps, "GPS %u", (unsigned)gpsSatCount);
        lv_obj_set_style_text_color(s_chatHeaderGps, headerGoodGreenColor(), 0);
    } else {
        lv_label_set_text(s_chatHeaderGps, "GPS 0");
        lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xFF6B6B), 0);
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
    lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);

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
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    snprintf(line, sizeof(line), "%s %s c%d",
             who,
             (portTag && portTag[0]) ? portTag : "D",
             chanIdx);
#else
    snprintf(line, sizeof(line), "R %s>%s %s c%d",
             who,
             liveDestTag(pkt.hdr.to),
             (portTag && portTag[0]) ? portTag : "D",
             chanIdx);
#endif
    Channels.addMessage(CHAN_ANN, timePrefix, line, TFT_DARKGREY, 0, false);
}

static void appendLiveRxEncrypted(const MeshPacket &pkt) {
    char timePrefix[12];
    char who[20];
    char line[96];

    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    liveNodeLabel(pkt.hdr.from, who, sizeof(who), false);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    snprintf(line, sizeof(line), "%s ENC h%02X", who, pkt.hdr.channel);
#else
    snprintf(line, sizeof(line), "R %s ENC h%02X", who, pkt.hdr.channel);
#endif
    Channels.addMessage(CHAN_ANN, timePrefix, line, TFT_DARKGREY, 0, false);
}

static bool processMeshPacket(const MeshPacket &pkt) {
    if (isDuplicate(pkt.hdr.from, pkt.hdr.id)) return false;

    // Match v1 behavior: ignore reflected copies of our own transmitted packets.
    if (s_myNodeId != 0 && pkt.hdr.from == s_myNodeId) return false;

    Nodes.updateFromPacket(pkt);
    int chanIdx = (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) ? pkt.chanIdx : 0;
    if (!pkt.decrypted) {
        appendLiveRxEncrypted(pkt);
        return false;
    }

    switch (pkt.portnum) {
        case TEXT_MESSAGE_APP: {
            char textBuf[MESH_TEXT_MAX_LEN + 1];
            size_t copy = pkt.payloadLen;
            if (copy > MESH_TEXT_MAX_LEN) copy = MESH_TEXT_MAX_LEN;
            memcpy(textBuf, pkt.payload, copy);
            textBuf[copy] = '\0';
            for (size_t i = 0; i < copy; i++) {
                if (textBuf[i] == '\r' || textBuf[i] == '\n') textBuf[i] = ' ';
            }

            if (textBuf[0]) {
                const bool viaMqtt = (pkt.hdr.flags & 0x10) != 0;

                if (pkt.hdr.to == s_myNodeId) {
                    NodeEntry *sender = Nodes.find(pkt.hdr.from);
                    char senderShort[5] = {};
                    if (sender && sender->shortName[0]) {
                        strncpy(senderShort, sender->shortName, sizeof(senderShort) - 1);
                        senderShort[sizeof(senderShort) - 1] = '\0';
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

                appendLiveRxSummary(pkt, chanIdx, "T");
                return (pkt.hdr.to == s_myNodeId) ? (s_dmModal != nullptr) : (chanIdx == s_activeChannel);
            }
            return false;
        }

        case ROUTING_APP: {
            if (!pkt.requestId) return false;

            uint32_t errorReason = 0;
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
                        break;
                    }
                } else {
                    break;
                }
            }

            bool isAck = (errorReason == 0);
            bool dmRoutingMatched = DMs.handleRoutingResult(pkt.hdr.from, pkt.requestId, errorReason);

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
            appendLiveRxSummary(pkt, chanIdx, "N");
            return false;
        }

        case POSITION_APP: {
            PositionInfo p = {};
            if (decodePosition(pkt.payload, pkt.payloadLen, p)) {
                Nodes.updatePosition(pkt.hdr.from, p);
            }
            appendLiveRxSummary(pkt, chanIdx, "P");
            return false;
        }

        case TELEMETRY_APP: {
            TelemetryInfo t = {};
            if (decodeTelemetry(pkt.payload, pkt.payloadLen, t)) {
                Nodes.updateTelemetry(pkt.hdr.from, t);
            }
            appendLiveRxSummary(pkt, chanIdx, "E");
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
        lv_obj_set_style_text_font(empty, kMainScreenFont, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No messages yet");
    } else {
        int displayOrder[MAX_MSG_LINES] = {};
        int displayCount = 0;
        buildChatDisplayOrder(rows, rowCount, displayOrder, displayCount);

        for (int n = 0; n < displayCount; n++) {
            int i = displayOrder[n];
            lv_obj_t *msg = lv_label_create(s_chatList);
            lastMsgObj = msg;
            lv_obj_set_width(msg, lv_pct(100));
            lv_obj_set_style_text_font(msg, kMainScreenFont, 0);
            lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_left(msg, 2, 0);
            lv_obj_set_style_pad_right(msg, 4, 0);
            lv_obj_set_style_pad_top(msg, 0, 0);
            lv_obj_set_style_pad_bottom(msg, 0, 0);
#if defined(DEVICE_TLORA_PAGER_TFT)
            lv_label_set_long_mode(msg, LV_LABEL_LONG_CLIP);
#else
            lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
#endif

            const char *lineText = rows[i]->text;
            bool isContinuationLine = (lineText[0] == ' ' && lineText[1] == ' ');

            uint16_t textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? TFT_BLACK : TFT_WHITE;
            const char *ackSuffix = nullptr;
            if (rows[i]->packetId) {
                switch (rows[i]->ack) {
                    case DisplayLine::ACKED:
                        textColor565 = (s_cfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                        if (!isContinuationLine) ackSuffix = " [ACK]";
                        break;
                    case DisplayLine::ACKED_RELAY:
                        textColor565 = TFT_YELLOW;
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
            if (ackSuffix) {
                char rendered[MSG_CHARS + 16];
                snprintf(rendered, sizeof(rendered), "%s%s", lineText, ackSuffix);
                lv_label_set_text(msg, rendered);
            } else {
                lv_label_set_text(msg, lineText);
            }

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
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1E44), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    #if defined(DEVICE_CARDPUTER_LORA_HAT)
    const int panelMargin = 2;
    const int chatGap = 3;
    const int chatHeaderH = 16;
    const int chatLegendH = 12;
    const int channelStripH = kMainScreenChannelBtnHeight + 6;
    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    const int chatX = panelMargin;
    const int chatW = screenW - panelMargin * 2;
    const int stripY = panelMargin + chatHeaderH + 2;
    const int chatY = stripY + channelStripH + chatGap;
    const int chatH = screenH - panelMargin - chatY - chatLegendH - 3;
    lv_obj_t *panel = nullptr;
    #elif defined(DEVICE_HELTEC_V4_EXPANSION)
    const int panelMargin = 0;
    const int chatGap = 3;
    const int chatHeaderH = 20;
    const int channelStripH = kMainScreenChannelBtnHeight + 8;
    const int chatLegendH = 28;
    const int screenW = lv_disp_get_hor_res(NULL);
    const int screenH = lv_disp_get_ver_res(NULL);
    const int chatX = panelMargin;
    const int chatW = screenW - panelMargin * 2;
    const int stripY = panelMargin + chatHeaderH + 2;
    const int chatY = stripY + channelStripH + chatGap;
    const int chatH = screenH - panelMargin - chatY - chatLegendH - 3;
    lv_obj_t *panel = nullptr;
    #else
    const int panelMargin = 6;
    const int panelW = 89;
    const int panelH = lv_disp_get_ver_res(NULL) - panelMargin * 2;
    const int chatGap = 6;
    const int chatHeaderH = 20;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    const int chatLegendH = 28;
#else
    const int chatLegendH = 14;
#endif

    lv_obj_t *panel = lv_obj_create(screen);
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
    lv_obj_set_style_bg_color(s_chatHeaderBar, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatHeaderBar, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatHeaderBar, 1, 0);
    lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_chatHeaderBar, 2, 0);

#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
    s_channelStrip = lv_obj_create(screen);
    lv_obj_set_size(s_channelStrip, chatW, channelStripH);
    lv_obj_align(s_channelStrip, LV_ALIGN_TOP_LEFT, chatX, stripY);
    lv_obj_clear_flag(s_channelStrip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_channelStrip, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_channelStrip, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_channelStrip, 1, 0);
    lv_obj_set_style_border_color(s_channelStrip, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_left(s_channelStrip, 3, 0);
    lv_obj_set_style_pad_right(s_channelStrip, 3, 0);
    lv_obj_set_style_pad_top(s_channelStrip, 2, 0);
    lv_obj_set_style_pad_bottom(s_channelStrip, 2, 0);

    s_channelList = lv_obj_create(s_channelStrip);
    lv_obj_set_size(s_channelList, lv_pct(100), lv_pct(100));
    lv_obj_align(s_channelList, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(s_channelList, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_channelList, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_channelList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_channelList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_channelList, 0, 0);
    lv_obj_set_style_pad_left(s_channelList, 0, 0);
    lv_obj_set_style_pad_right(s_channelList, 6, 0);
    lv_obj_set_style_pad_top(s_channelList, 0, 0);
    lv_obj_set_style_pad_bottom(s_channelList, 0, 0);
    lv_obj_set_style_pad_column(s_channelList, 4, 0);
    lv_obj_set_style_width(s_channelList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_channelList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_channelList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_channelList, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_channelList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
#else
    s_channelStrip = nullptr;
    s_channelList = nullptr;
#endif

    s_chatHeaderTime = lv_label_create(s_chatHeaderBar);
    lv_obj_align(s_chatHeaderTime, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_chatHeaderTime, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderTime, lv_color_hex(0xD9E8FF), 0);

    s_chatHeaderGps = lv_label_create(s_chatHeaderBar);
    lv_obj_align(s_chatHeaderGps, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_set_style_text_font(s_chatHeaderGps, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);

    s_chatHeaderBattText = lv_label_create(s_chatHeaderBar);
    lv_obj_align(s_chatHeaderBattText, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_text_font(s_chatHeaderBattText, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatHeaderBattText, lv_color_hex(0xBFD6FF), 0);

    s_chatHeaderBattBar = lv_bar_create(s_chatHeaderBar);
    lv_obj_set_size(s_chatHeaderBattBar, 26, 8);
    lv_obj_align_to(s_chatHeaderBattBar, s_chatHeaderBattText, LV_ALIGN_OUT_LEFT_MID, -7, 0);
    lv_bar_set_range(s_chatHeaderBattBar, 0, 100);
    lv_obj_set_style_bg_color(s_chatHeaderBattBar, lv_color_hex(0x1E355F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_chatHeaderBattBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_chatHeaderBattBar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_chatHeaderBattBar, lv_color_hex(0x5B86C7), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_chatHeaderBattBar, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerGoodGreenColor(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_chatHeaderBattBar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_chatHeaderWifi = lv_label_create(s_chatHeaderBar);
    lv_obj_align_to(s_chatHeaderWifi, s_chatHeaderGps, LV_ALIGN_OUT_LEFT_MID, -7, 0);
    lv_obj_set_style_text_font(s_chatHeaderWifi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);

    s_chatPanel = lv_obj_create(screen);
    lv_obj_set_size(s_chatPanel, chatW, chatH);
    lv_obj_align(s_chatPanel, LV_ALIGN_TOP_LEFT, chatX, chatY);
    lv_obj_clear_flag(s_chatPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_chatPanel, lv_color_hex(0x0E285B), 0);
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
    lv_obj_set_scroll_dir(s_chatList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_chatList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_chatList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chatList, 0, 0);
    lv_obj_set_style_pad_all(s_chatList, 0, 0);
    lv_obj_set_style_pad_right(s_chatList, 6, 0);
    lv_obj_set_style_pad_row(s_chatList, 1, 0);
    lv_obj_set_style_width(s_chatList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_chatList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_chatList, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_chatList, 2, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(s_chatList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_chatList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

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
    lv_obj_set_width(s_chatShortcutText, lv_pct(100));
    lv_obj_set_style_text_font(s_chatShortcutText, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatShortcutText, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_chatShortcutText, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_chatShortcutText, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_chatShortcutText, "(D)M   (C)FG   (N)odes   L(i)ve   (L)egend");
#endif

    for (int i = 0; i < MESH_CHANNELS; i++) {
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
    }

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
        lv_obj_set_style_bg_color(s_channelList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
    }

    if (s_chatHeaderBar) {
        lv_obj_set_style_bg_color(s_chatHeaderBar, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_hex(0x335D9D), 0);
    }
    if (s_chatHeaderTime) lv_obj_set_style_text_color(s_chatHeaderTime, lv_color_hex(0xD9E8FF), 0);
    if (s_chatHeaderGps) lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xBFD6FF), 0);
    if (s_chatHeaderBattText) lv_obj_set_style_text_color(s_chatHeaderBattText, lv_color_hex(0xBFD6FF), 0);
    if (s_chatHeaderBattBar) {
        lv_obj_set_style_bg_color(s_chatHeaderBattBar, lv_color_hex(0x1E355F), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_chatHeaderBattBar, lv_color_hex(0x5B86C7), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_chatHeaderBattBar, headerGoodGreenColor(), LV_PART_INDICATOR);
    }
    if (s_chatHeaderWifi) lv_obj_set_style_text_color(s_chatHeaderWifi, lv_color_hex(0xBFD6FF), 0);

    if (s_chatPanel) {
        lv_obj_set_style_bg_color(s_chatPanel, lv_color_hex(0x0E285B), 0);
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
        lv_obj_set_style_bg_color(s_liveModal, lv_color_hex(0x0E285B), 0);
        lv_obj_set_style_border_color(s_liveModal, lv_color_hex(0x5C86C6), 0);
    }
    if (s_liveList) {
        lv_obj_set_style_bg_color(s_liveList, lv_color_hex(0x0F2A5C), 0);
        lv_obj_set_style_border_color(s_liveList, lv_color_hex(0x335D9D), 0);
        lv_obj_set_style_bg_color(s_liveList, lv_color_hex(0x8FB5E6), LV_PART_SCROLLBAR);
        refreshLiveView(true);
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
    s_radioReady = Radio.init();
    if (!s_radioReady) {
        Channels.addMessage(0, "", "[radio] init failed", TFT_RED);
    }

    buildUi();
    s_lastActivityMs = millis();
    Serial.printf("[lvgl-poc] started (%dx%d)\\n", displayDev().width(), displayDev().height());
}

void loop() {
    s_cfgDebugLog = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;

    uint32_t now = millis();
    if (pollUserButton(now)) {
        delay(5);
        return;
    }

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
    refreshDmModal(meshChanged);
    delay(5);
}

#endif  // UI_LVGL_POC
