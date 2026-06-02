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
#include "battery_util.h"
#include "gps.h"
#include "keyboard.h"
#include "web_config.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <time.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <SD.h>

static LGFX_TDeck lcd;
static TDeckKeyboard s_keyboard;
static RhinoConfig s_cfg;

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
static lv_obj_t *s_chatHeaderBar = nullptr;
static lv_obj_t *s_chatHeaderTime = nullptr;
static lv_obj_t *s_chatHeaderGps = nullptr;
static lv_obj_t *s_chatHeaderWifi = nullptr;
static lv_obj_t *s_chatHeaderBattText = nullptr;
static lv_obj_t *s_chatHeaderBattBar = nullptr;
static lv_obj_t *s_chatPanel = nullptr;
static lv_obj_t *s_chatList = nullptr;
static lv_obj_t *s_chatShortcutBar = nullptr;
static lv_obj_t *s_chatShortcutText = nullptr;
static lv_obj_t *s_rootScreen = nullptr;
static lv_obj_t *s_composeModal = nullptr;
static lv_obj_t *s_composeInput = nullptr;
static lv_obj_t *s_cfgModal = nullptr;
static lv_obj_t *s_cfgActionList = nullptr;
static lv_obj_t *s_cfgHeaderStatus = nullptr;
static lv_obj_t *s_legendModal = nullptr;
static lv_obj_t *s_liveModal = nullptr;
static lv_obj_t *s_liveList = nullptr;
static lv_obj_t *s_nodesModal = nullptr;
static lv_obj_t *s_nodesInfoPanel = nullptr;
static lv_obj_t *s_nodesDetail = nullptr;
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
static bool s_cfgDebugLog = true;
static uint32_t s_selectedMsgReplyPacketId = 0;
static char s_selectedMsgText[MSG_CHARS + 1] = "";
static uint32_t s_myNodeId = 0;
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
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_12;
static constexpr int kMainScreenChannelBtnHeight = 24;
#else
static constexpr bool kPagerWheelChatNav = false;
static const lv_font_t *kMainScreenFont = &lv_font_montserrat_10;
static constexpr int kMainScreenChannelBtnHeight = 22;
#endif
static bool s_pagerChatCursorMode = false;
static int s_pagerChatCursorDisplayIndex = -1;

static constexpr int kRxDedupSize = 32;
struct SeenPkt {
    uint32_t from;
    uint32_t id;
};
static SeenPkt s_seenPkts[kRxDedupSize] = {};
static int s_seenHead = 0;

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
static void onChatMessagePressed(lv_event_t *e);
static void onChatMessageLongPressed(lv_event_t *e);
static void recomputeChannelHashes();
static void initCfgActions();
static void refreshCfgModal();
static void openCfgModal();
static void closeCfgModal();
static void activateCfgSelection();
static void openLegendModal();
static void closeLegendModal();
static void openLiveModal();
static void closeLiveModal();
static void refreshLiveView(bool force = false);
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
    const char *name;
};

static constexpr UiThemePresetLite kUiThemePresets[] = {
    {UI_THEME_CAMELLIA, UI_MODE_DARK, "Camillia Dark"},
    {UI_THEME_CAMELLIA, UI_MODE_LIGHT, "Camillia Light"},
    {UI_THEME_EVERGREEN, UI_MODE_DARK, "Evergreen Dark"},
    {UI_THEME_EVERGREEN, UI_MODE_LIGHT, "Evergreen Light"},
    {UI_THEME_EARTHEN, UI_MODE_DARK, "Earthy Dark"},
    {UI_THEME_EARTHEN, UI_MODE_LIGHT, "Earthy Light"},
    {UI_THEME_SOLARIZED, UI_MODE_DARK, "Solarized Dark"},
    {UI_THEME_SOLARIZED, UI_MODE_LIGHT, "Solarized Light"},
    {UI_THEME_CRIMSON, UI_MODE_DARK, "Crimson Blue Dark"},
    {UI_THEME_CRIMSON, UI_MODE_LIGHT, "Crimson Blue Light"},
};

static constexpr int kUiThemePresetCount =
    (int)(sizeof(kUiThemePresets) / sizeof(kUiThemePresets[0]));

static const char *msgAlertSoundName(uint8_t mode) {
    switch (mode) {
        case MSG_ALERT_SOUND_CHIRPY: return "Chirpy";
        case MSG_ALERT_SOUND_BASS:   return "Bass";
        case MSG_ALERT_SOUND_OFF:    return "Off";
        case MSG_ALERT_SOUND_DEFAULT:
        default:                     return "Default";
    }
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
    if (!kPagerWheelChatNav) return false;

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
    s_composeReplyPacketId = 0;
    s_composeChannelIdx = s_activeChannel;
}

static void openComposePrompt(uint32_t replyPacketId, const char *replyText) {
    if (!s_rootScreen) return;
    if (s_activeChannel < 0 || s_activeChannel >= MESH_CHANNELS) return;

    closeComposePrompt();

    const bool isReply = (replyPacketId != 0);
    s_composeReplyPacketId = replyPacketId;
    s_composeChannelIdx = s_activeChannel;

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    if (modalW < 140) modalW = lv_disp_get_hor_res(NULL) - 8;
    int modalH = isReply ? 96 : 72;

    s_composeModal = lv_obj_create(s_rootScreen);
    lv_obj_set_size(s_composeModal, modalW, modalH);
    lv_obj_align(s_composeModal, LV_ALIGN_CENTER, 0, 10);
    lv_obj_clear_flag(s_composeModal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_composeModal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_composeModal, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_composeModal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeModal, 1, 0);
    lv_obj_set_style_border_color(s_composeModal, lv_color_hex(0x5C86C6), 0);
    lv_obj_set_style_pad_all(s_composeModal, 4, 0);
    lv_obj_set_style_pad_row(s_composeModal, 3, 0);
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
        lv_label_set_text_fmt(replyLbl, "RE: %s", preview[0] ? preview : "(message)");
    }

    s_composeInput = lv_textarea_create(s_composeModal);
    lv_obj_set_width(s_composeInput, lv_pct(100));
    lv_obj_set_height(s_composeInput, 28);
    lv_obj_set_style_text_font(s_composeInput, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_composeInput, lv_color_hex(0xE8F1FF), 0);
    lv_obj_set_style_bg_color(s_composeInput, lv_color_hex(0x102B61), 0);
    lv_obj_set_style_bg_opa(s_composeInput, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_composeInput, 1, 0);
    lv_obj_set_style_border_color(s_composeInput, lv_color_hex(0x4C76BA), 0);
    lv_textarea_set_one_line(s_composeInput, true);
    lv_textarea_set_max_length(s_composeInput, MESH_TEXT_MAX_LEN);
    lv_textarea_set_placeholder_text(s_composeInput, "Type message...");

    lv_obj_t *hint = lv_label_create(s_composeModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "Enter=Send  Bksp(empty)=Cancel");
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
        Channels.addMessage(txChan, "", "! TX failed (node id)", TFT_RED, 0);
        closeComposePrompt();
        refreshChatView(true);
        return;
    }

    bool sentOk = Channels.sendText(s_myNodeId, msg, s_cfg.okToMqtt, txChan, s_composeReplyPacketId);
    if (!sentOk) {
        Channels.addMessage(txChan, "", "! TX failed", TFT_RED, 0);
    }

    closeComposePrompt();
    refreshChatView(true);
}

static void initCfgActions() {
    s_cfgActionCount = 0;
    s_cfgActions[s_cfgActionCount++] = CFG_ACTION_WEBCFG;
#if HAS_SD_CARD
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

static void refreshCfgModal() {
    if (!s_cfgModal || !s_cfgActionList || !s_cfgHeaderStatus) return;

    if (s_cfgActionCount <= 0) {
        initCfgActions();
    }
    if (s_cfgSelection < 0) s_cfgSelection = 0;
    if (s_cfgSelection >= s_cfgActionCount) s_cfgSelection = s_cfgActionCount - 1;

    lv_label_set_text(s_cfgHeaderStatus, s_cfgStatus[0] ? s_cfgStatus : "Ready");

    lv_obj_clean(s_cfgActionList);
    lv_obj_t *selectedRowObj = nullptr;

    for (int i = 0; i < s_cfgActionCount; i++) {
        const int actionId = s_cfgActions[i];
        char rowText[80];

        lv_obj_t *row = lv_label_create(s_cfgActionList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_style_text_font(row, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(row, lv_color_hex(0xD9E8FF), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_left(row, 4, 0);
        lv_obj_set_style_pad_right(row, 4, 0);
        lv_obj_set_style_pad_top(row, 2, 0);
        lv_obj_set_style_pad_bottom(row, 2, 0);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_label_set_text(row, cfgActionLabel(actionId, rowText, sizeof(rowText)));

        if (i == s_cfgSelection) {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2A4E8F), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_80, 0);
            lv_obj_set_style_text_color(row, lv_color_hex(0xEAF3FF), 0);
            selectedRowObj = row;
        } else if (i & 1) {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x123266), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        }
    }

    if (selectedRowObj) {
        lv_obj_scroll_to_view(selectedRowObj, LV_ANIM_OFF);
    }
}

static void closeCfgModal() {
    if (s_cfgModal) {
        lv_obj_del(s_cfgModal);
    }
    s_cfgModal = nullptr;
    s_cfgActionList = nullptr;
    s_cfgHeaderStatus = nullptr;
    s_cfgAwaitEnterRelease = false;
}

static void closeLegendModal() {
    if (s_legendModal) {
        lv_obj_del(s_legendModal);
    }
    s_legendModal = nullptr;
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
        lv_obj_set_style_bg_color(row, selected ? lv_color_hex(0x2A4E8F) : lv_color_hex(0x123266), 0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_70 : LV_OPA_40, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row,
                                      selected ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C),
                                      0);
    }
}

static void refreshNodesDetails() {
    if (!s_nodesDetail) return;

    if (s_nodesSnapshotCount <= 0 || s_nodesSelected < 0 || s_nodesSelected >= s_nodesSnapshotCount) {
        lv_label_set_text(s_nodesDetail, "No nodes seen yet.");
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
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
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
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
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

    lv_obj_t *hint = lv_label_create(s_liveModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "Bksp = Back   C = Clear log");

    refreshLiveView(true);
}

static void openNodesModal() {
    if (!s_rootScreen || s_nodesModal) return;
    if (s_composeModal) closeComposePrompt();
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

    s_nodesDetail = lv_label_create(left);
    lv_obj_set_width(s_nodesDetail, lv_pct(100));
    lv_obj_set_style_text_font(s_nodesDetail, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_nodesDetail, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(s_nodesDetail, LV_LABEL_LONG_WRAP);

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
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No nodes seen");
    } else {
        for (int i = 0; i < s_nodesSnapshotCount; i++) {
            lv_obj_t *row = lv_btn_create(s_nodesList);
            lv_obj_set_width(row, lv_pct(96));
            lv_obj_set_height(row, 22);
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
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
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

    lv_obj_t *hint = lv_label_create(s_nodesModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "Bksp = Back   Tap node for details");
}

static void openLegendModal() {
    if (!s_rootScreen || s_legendModal) return;

    int modalW = lv_disp_get_hor_res(NULL) - 24;
    int modalH = 118;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;

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
    lv_obj_set_flex_flow(s_legendModal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_legendModal, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *title = lv_label_create(s_legendModal);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Legend");

    lv_obj_t *body = lv_label_create(s_legendModal);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_font(body, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(
        body,
        "(C) Configuration\n"
        "(N) Nodes\n"
        "L(i)ve (C clears log)\n"
        "(Enter) Compose/Reply\n"
        "(Bksp) Clear Selection\n"
        "\n"
        "Transport Symbols:\n"
        "%s Radio Transmission\n"
        "%s MQTT Transmission\n"
        "\n"
        "(H or ?) Close Legend",
        LV_SYMBOL_RADIO_TINY,
        LV_SYMBOL_GLOBE_TINY);

    lv_obj_t *hint = lv_label_create(s_legendModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "Bksp/C/N/I/H/? = Close");
}

static void openCfgModal() {
    if (!s_rootScreen || s_cfgModal) return;
    if (s_composeModal) closeComposePrompt();
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
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xD9E8FF), 0);
    lv_label_set_text(title, "Configuration");

    s_cfgHeaderStatus = lv_label_create(header);
    lv_obj_set_width(s_cfgHeaderStatus, lv_pct(58));
    lv_obj_set_style_text_font(s_cfgHeaderStatus, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(s_cfgHeaderStatus, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_cfgHeaderStatus, lv_color_hex(0x79DDB8), 0);
    lv_label_set_long_mode(s_cfgHeaderStatus, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_cfgHeaderStatus, "Ready");

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

    lv_obj_t *hint = lv_label_create(s_cfgModal);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA7C7FF), 0);
    lv_label_set_text(hint, "Scroll Up/Down=Select  Enter=Run  Bksp=Close");

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
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Imported OK - rebooting...");
                refreshCfgModal();
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
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Theme: %s", uiThemePresetNameFromCfg());
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
            nvs_flash_erase();
            nvs_flash_init();
            Nodes.clearPersisted();
            Channels.clearAllMessages(true);
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
            if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE) {
                closeCfgModal();
                continue;
            }
            if (k == KEY_SCROLL_UP) {
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

        if (s_nodesModal) {
            if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE) {
                closeNodesModal();
                continue;
            }
            if (k == KEY_SCROLL_UP && s_nodesSelected > 0) {
                s_nodesSelected--;
                refreshNodesListSelection();
                refreshNodesDetails();
                if (s_nodesSelected >= 0 && s_nodesSelected < s_nodesListRowCount && s_nodesListRows[s_nodesSelected]) {
                    lv_obj_scroll_to_view(s_nodesListRows[s_nodesSelected], LV_ANIM_OFF);
                }
                continue;
            }
            if (k == KEY_SCROLL_DN && s_nodesSelected + 1 < s_nodesSnapshotCount) {
                s_nodesSelected++;
                refreshNodesListSelection();
                refreshNodesDetails();
                if (s_nodesSelected >= 0 && s_nodesSelected < s_nodesListRowCount && s_nodesListRows[s_nodesSelected]) {
                    lv_obj_scroll_to_view(s_nodesListRows[s_nodesSelected], LV_ANIM_OFF);
                }
                continue;
            }
            continue;
        }

        if (s_liveModal) {
            if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE) {
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
            if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE
                || k == 'h' || k == 'H' || k == '?' || k == 'l' || k == 'L') {
                closeLegendModal();
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
            if (kPagerWheelChatNav) {
                if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE) {
                    if (s_pagerChatCursorMode) {
                        pagerExitChatCursorMode(true);
                        refreshChatView(true);
                    } else if (s_selectedMsgReplyPacketId != 0 || s_selectedMsgText[0]) {
                        s_selectedMsgReplyPacketId = 0;
                        s_selectedMsgText[0] = '\0';
                        s_lastRenderedChannel = -1;
                        refreshChatView(true);
                    }
                    continue;
                }

                if (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN) {
                    if (s_pagerChatCursorMode) {
                        int delta = (k == KEY_SCROLL_UP) ? 1 : -1;
                        pagerSelectChatCursorIndex(s_pagerChatCursorDisplayIndex + delta);
                    } else if (s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                        int nextChannel = s_activeChannel + ((k == KEY_SCROLL_UP) ? 1 : -1);
                        if (nextChannel < 0) nextChannel = MESH_CHANNELS - 1;
                        if (nextChannel >= MESH_CHANNELS) nextChannel = 0;
                        setActiveChannel(nextChannel);
                    }
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

            if (k == 'h' || k == 'H' || k == '?' || k == 'l' || k == 'L') {
                openLegendModal();
            } else if (k == 'c' || k == 'C') {
                openCfgModal();
            } else if (k == 'n' || k == 'N') {
                openNodesModal();
            } else if (k == 'i' || k == 'I') {
                openLiveModal();
            } else if (k == KEY_ENTER && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                if (kPagerWheelChatNav) {
                    openComposePrompt(0, nullptr);
                } else if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
                    openComposePrompt(s_selectedMsgReplyPacketId, s_selectedMsgText);
                } else {
                    openComposePrompt(0, nullptr);
                }
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
            case KEY_BACKSPACE:
            case KEY_BACKSPACE_HOLD:
                if (s_composeInput) {
                    const char *cur = lv_textarea_get_text(s_composeInput);
                    if (!cur || !cur[0]) {
                        closeComposePrompt();
                    } else if (k == KEY_BACKSPACE) {
                        lv_textarea_del_char(s_composeInput);
                    }
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

static void onChatMessageLongPressed(lv_event_t *e) {
    lv_obj_t *label = lv_event_get_target(e);
    if (!label) return;

    uint32_t replyPacketId = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (!replyPacketId) return;

    const char *txt = lv_label_get_text(label);
    openComposePrompt(replyPacketId, txt);
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

    // Defer redraw to the normal loop to avoid deleting the active target in-event.
    s_lastRenderedChannel = -1;
}

static void onWebCfgSaved() {
    myDeviceRole = s_cfg.deviceRole;
    recomputeChannelHashes();
    deriveNodeId();
    applyTimezoneFromConfig();
    gpsSetEnabled(s_cfg.gpsEnabled);
    syncWifiCredsToPrefs();

    s_ntpConfigured = false;
    s_ntpServerActive[0] = '\0';
    s_ntpLastConfigureMs = 0;

    if (!cfgExport(s_cfg)) {
        Serial.println("[cfg] web save export failed");
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
    lcd.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}

static void lvglTouchRead(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    LV_UNUSED(indev);
#if TOUCH_POLL_ENABLED
    int32_t tx = 0;
    int32_t ty = 0;
    if (lcd.getTouch(&tx, &ty)) {
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
            lv_obj_set_style_text_color(
                lbl,
                active ? lv_color_hex(0xEAF3FF)
                       : (s_channelNeedsAttention[i] ? lv_color_hex(0xFFF0B8) : lv_color_hex(0xD9E8FF)),
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

static void setActiveChannel(int channelIdx) {
    if (channelIdx < 0 || channelIdx >= MESH_CHANNELS) return;
    if (s_composeModal && channelIdx != s_composeChannelIdx) closeComposePrompt();
    s_activeChannel = channelIdx;
    s_pagerChatCursorMode = false;
    s_pagerChatCursorDisplayIndex = -1;
    s_selectedMsgReplyPacketId = 0;
    s_selectedMsgText[0] = '\0';
    s_channelNeedsAttention[channelIdx] = false;
    Channels.setActive(channelIdx);
    for (int i = 0; i < MESH_CHANNELS; i++) {
        bool active = (i == s_activeChannel);
        lv_obj_set_style_bg_color(s_channelBtns[i], active ? lv_color_hex(0x2A4FB4) : lv_color_hex(0x102750), 0);
        lv_obj_set_style_bg_opa(s_channelBtns[i], active ? LV_OPA_90 : LV_OPA_60, 0);
        lv_obj_set_style_border_width(s_channelBtns[i], active ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_channelBtns[i], active ? lv_color_hex(0x90B4FF) : lv_color_hex(0x2B4D8C), 0);
    }
    refreshChannelGlow(true);
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

static void loadConfigFromSd() {
    cfgInitDefaults(s_cfg);
    myDeviceRole = s_cfg.deviceRole;
    s_webCfgEnabled = false;

    if (!sdBegin()) {
        Serial.println("[lvgl-poc] SD not available; using default config");
        return;
    }

    if (cfgImport(s_cfg)) {
        myDeviceRole = s_cfg.deviceRole;
        Serial.println("[lvgl-poc] imported /camillia/config.yaml");
    } else {
        Serial.println("[lvgl-poc] config import failed; using default config");
    }

    // Match v1 behavior: allow persisted web-config timezone to override file/default.
    Preferences prefs;
    if (prefs.begin("camillia", true)) {
        // Keep v1 precedence for Wi-Fi credentials: NVS values can fill blanks.
        String wifiSsid = prefs.getString("wifiSsid", "");
        String wifiPass = prefs.getString("wifiPass", "");
        if (!s_cfg.wifiSsid[0] && wifiSsid.length()) {
            strncpy(s_cfg.wifiSsid, wifiSsid.c_str(), sizeof(s_cfg.wifiSsid) - 1);
            s_cfg.wifiSsid[sizeof(s_cfg.wifiSsid) - 1] = '\0';
        }
        if (!s_cfg.wifiPass[0] && wifiPass.length()) {
            strncpy(s_cfg.wifiPass, wifiPass.c_str(), sizeof(s_cfg.wifiPass) - 1);
            s_cfg.wifiPass[sizeof(s_cfg.wifiPass) - 1] = '\0';
        }

        String tz = prefs.getString("tzDef", "");
        if (tz.length()) {
            strncpy(s_cfg.tzDef, tz.c_str(), sizeof(s_cfg.tzDef) - 1);
            s_cfg.tzDef[sizeof(s_cfg.tzDef) - 1] = '\0';
        }

        s_webCfgEnabled = prefs.getBool("webCfgEnabled", false);
        prefs.end();
    }
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
    lv_label_set_text_fmt(s_chatHeaderBattText, "%u%%", (unsigned)battPct);

    if (gpsEnabled && gpsFix) {
        lv_label_set_text_fmt(s_chatHeaderGps, "GPS %u", (unsigned)gpsSatCount);
        lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0x84E07A), 0);
    } else {
        lv_label_set_text(s_chatHeaderGps, "GPS 0");
        lv_obj_set_style_text_color(s_chatHeaderGps, lv_color_hex(0xFF6B6B), 0);
    }

    bool wifiOffOrDisconnected = (!wifiApMode && !wifiConnected);
    lv_label_set_text(s_chatHeaderWifi, wifiApMode ? LV_SYMBOL_UPLOAD : LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(
        s_chatHeaderWifi,
        wifiApMode ? lv_color_hex(0xF4D35E)
                   : (wifiOffOrDisconnected ? lv_color_hex(0xFF6B6B) : lv_color_hex(0x84E07A)),
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
    liveNodeLabel(fromNode, sender, sizeof(sender), false);
    const char *transportIcon = viaMqtt ? LV_SYMBOL_GLOBE_TINY : LV_SYMBOL_RADIO_TINY;
    // Keep a small visual buffer between transport icon and timestamp.
    snprintf(prefix, sizeof(prefix), "%s  %s[%s] ", transportIcon, timePrefix, sender);

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
                appendRxText(chanIdx, pkt.hdr.from, textBuf, pkt.hdr.id, viaMqtt);
                appendLiveRxSummary(pkt, chanIdx, "T");
                return chanIdx == s_activeChannel;
            }
            return false;
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
            lv_obj_set_style_text_color(msg, lv_color_hex(0xD9E8FF), 0);
            lv_obj_set_style_bg_opa(msg, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_left(msg, 2, 0);
            lv_obj_set_style_pad_right(msg, 4, 0);
            lv_obj_set_style_pad_top(msg, 0, 0);
            lv_obj_set_style_pad_bottom(msg, 0, 0);
            lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
            lv_label_set_text(msg, rows[i]->text);

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
                                LV_EVENT_PRESSED,
                                (void *)(uintptr_t)replyPacketId);
            if (replyPacketId) {
                lv_obj_add_event_cb(msg, onChatMessageLongPressed,
                                    LV_EVENT_LONG_PRESSED,
                                    (void *)(uintptr_t)replyPacketId);
            }

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

    if (kPagerWheelChatNav && s_pagerChatCursorMode) {
        lv_obj_scroll_to_y(s_chatList, prevScrollY, LV_ANIM_OFF);
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

    const int panelMargin = 6;
    #if defined(DEVICE_TLORA_PAGER_TFT)
    const int panelW = 108;
    #else
    const int panelW = 89;
    #endif
    const int panelH = lv_disp_get_ver_res(NULL) - panelMargin * 2;
    const int chatGap = 6;
    const int chatHeaderH = 20;
    const int chatLegendH = 14;

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
    static lv_coord_t panelCols[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
    static lv_coord_t panelRows[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
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

    s_chatHeaderBar = lv_obj_create(screen);
    lv_obj_set_size(s_chatHeaderBar, chatW, chatHeaderH);
    lv_obj_align(s_chatHeaderBar, LV_ALIGN_TOP_LEFT, chatX, panelMargin);
    lv_obj_clear_flag(s_chatHeaderBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_chatHeaderBar, lv_color_hex(0x0E285B), 0);
    lv_obj_set_style_bg_opa(s_chatHeaderBar, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_chatHeaderBar, 1, 0);
    lv_obj_set_style_border_color(s_chatHeaderBar, lv_color_hex(0x335D9D), 0);
    lv_obj_set_style_pad_all(s_chatHeaderBar, 2, 0);

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
    lv_obj_set_style_bg_color(s_chatHeaderBattBar, lv_color_hex(0x84E07A), LV_PART_INDICATOR);
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

    s_chatList = lv_obj_create(s_chatPanel);
    lv_obj_set_size(s_chatList, lv_pct(100), lv_pct(100));
    lv_obj_align(s_chatList, LV_ALIGN_TOP_LEFT, 0, 0);
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

    s_chatShortcutText = lv_label_create(s_chatShortcutBar);
    lv_obj_set_width(s_chatShortcutText, lv_pct(100));
    lv_obj_set_style_text_font(s_chatShortcutText, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_chatShortcutText, lv_color_hex(0xA7C7FF), 0);
    lv_obj_set_style_text_align(s_chatShortcutText, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_chatShortcutText, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_chatShortcutText, "(C)FG   (N)odes   L(i)ve   (L)egend");

    for (int i = 0; i < MESH_CHANNELS; i++) {
        #if defined(DEVICE_TLORA_PAGER_TFT)
        lv_obj_t *btn = lv_obj_create(panel);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_grid_cell(btn,
                             LV_GRID_ALIGN_STRETCH, i % 2, 1,
                             LV_GRID_ALIGN_STRETCH, i / 2, 1);
        #else
        lv_obj_t *btn = lv_btn_create(panel);
        #endif
        s_channelBtns[i] = btn;
        #if defined(DEVICE_TLORA_PAGER_TFT)
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
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        s_channelLabels[i] = lbl;
        lv_obj_set_style_text_font(lbl, kMainScreenFont, 0);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_height(lbl, lv_font_get_line_height(kMainScreenFont));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        const char *name = channelName(i);
        if (name[0]) {
            lv_label_set_text(lbl, name);
        } else {
            lv_label_set_text(lbl, "Channel");
        }
        lv_obj_center(lbl);
    }

    setActiveChannel(0);
    refreshHeaderTime(true);
    refreshHeaderStatus(true);
    lv_scr_load(screen);
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

    lcd.init();
    lcd.setRotation(TFT_ROTATION_DEFAULT);
    lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    lcd.fillScreen(TFT_BLACK);
    s_keyboard.begin();

    lv_init();
    nodesMapInitFsDriver();
    lv_disp_draw_buf_init(&s_drawBuf, s_drawBufMem, nullptr, kMaxHorRes * kDrawBufLines);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = lcd.width();
    dispDrv.ver_res = lcd.height();
    dispDrv.flush_cb = lvglFlush;
    dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t touchDrv;
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = lvglTouchRead;
    lv_indev_drv_register(&touchDrv);

    loadConfigFromSd();
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
    Channels.init();
    Channels.beginPersistence();
    Channels.loadPersisted();
    s_radioReady = Radio.init();
    if (!s_radioReady) {
        Channels.addMessage(0, "", "[radio] init failed", TFT_RED);
    }

    buildUi();
    Serial.printf("[lvgl-poc] started (%dx%d)\\n", lcd.width(), lcd.height());
}

void loop() {
    bootstrapStateMapsIfMissing();
    pumpKeyboardInput();
    lv_timer_handler();
    if (webCfgRunning()) {
        webCfgLoop();
    }
    bool meshChanged = false;
    if (s_radioReady) {
        meshChanged = pollMeshRx();
    }
    gpsLoop();
    refreshChannelGlow(false);
    refreshHeaderTime(false);
    refreshHeaderStatus(false);
    refreshChatView(meshChanged);
    refreshLiveView(meshChanged);
    delay(5);
}

#endif  // UI_LVGL_POC
