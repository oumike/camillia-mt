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
#include <lvgl.h>
#include <time.h>
#include <esp_mac.h>
#include <nvs_flash.h>

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
static int s_activeChannel = 0;
static int s_lastRenderedChannel = -1;
static int s_lastRenderedCount = -1;
static int s_cfgSelection = 0;
static int s_cfgActionCount = 0;
static int s_cfgActions[12] = {};
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

static constexpr int kRxDedupSize = 32;
struct SeenPkt {
    uint32_t from;
    uint32_t id;
};
static SeenPkt s_seenPkts[kRxDedupSize] = {};
static int s_seenHead = 0;

static void refreshChatView(bool force = false);
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
static void onWebCfgSaved();
static bool pollMeshRx();
static void applyTimezoneFromConfig();
static void syncWifiCredsToPrefs();

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
            if (webCfgRunning()) {
                snprintf(buf, bufLen, "Web Config: %s", webCfgIP());
            } else {
                snprintf(buf, bufLen, "Web Config: OFF");
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
    lv_label_set_text(hint, "Bksp/C/H/? = Close");
}

static void openCfgModal() {
    if (!s_rootScreen || s_cfgModal) return;
    if (s_composeModal) closeComposePrompt();
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

    int modalW = lv_disp_get_hor_res(NULL) - 20;
    int modalH = lv_disp_get_ver_res(NULL) - 20;
    if (modalW < 180) modalW = lv_disp_get_hor_res(NULL) - 8;
    if (modalH < 120) modalH = lv_disp_get_ver_res(NULL) - 8;

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
    lv_label_set_text(hint, "Scroll Up/Down=Select  Enter=Run  C/Bksp=Close");

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
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec WEB_CFG");
            if (webCfgRunning()) {
                webCfgEnd();
                snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web server stopped");
            } else {
                bool ok = webCfgBegin(&s_cfg, onWebCfgSaved, nullptr);
                if (ok) {
                    if (webCfgIsOnboarding()) {
                        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Setup: %s", webCfgIP());
                    } else {
                        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web: %s", webCfgIP());
                    }
                } else {
                    snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Web start FAILED");
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
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), "Messages cleared");
            refreshChatView(true);
            break;

        case CFG_ACTION_CLEAR_NODES:
            if (s_cfgDebugLog) Serial.println("[lvgl-cfg] exec CLEAR_NODES");
            Nodes.clearPersisted();
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
            if (k == KEY_BACKSPACE_HOLD || k == KEY_BACKSPACE || k == 'c' || k == 'C') {
                closeCfgModal();
                continue;
            }
            if (k == KEY_SCROLL_UP) {
                if (s_cfgSelection > 0) {
                    s_cfgSelection--;
                    s_cfgLastScrollMs = millis();
                    s_cfgConfirmAction = -1;
                    s_cfgConfirmMs = 0;
                    cfgDebugSelection("scroll-up", s_cfgActions[s_cfgSelection]);
                    refreshCfgModal();
                }
                continue;
            }
            if (k == KEY_SCROLL_DN) {
                if (s_cfgSelection + 1 < s_cfgActionCount) {
                    s_cfgSelection++;
                    s_cfgLastScrollMs = millis();
                    s_cfgConfirmAction = -1;
                    s_cfgConfirmMs = 0;
                    cfgDebugSelection("scroll-dn", s_cfgActions[s_cfgSelection]);
                    refreshCfgModal();
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
            continue;
        }

        if (!s_composeModal) {
            if (k == 'h' || k == 'H' || k == '?' || k == 'l' || k == 'L') {
                openLegendModal();
            } else if (k == 'c' || k == 'C') {
                openCfgModal();
            } else if (k == KEY_ENTER && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
                if (s_selectedMsgReplyPacketId != 0 && s_selectedMsgText[0]) {
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

    if (webCfgBegin(&s_cfg, nullptr, nullptr)) {
        if (wifiHasInternetTimePath()) bootTimeSynced = waitForNtpSync(10000UL, true);
        webCfgEnd();
    }

    // Fallback for setups where Wi-Fi credentials are present in config but
    // webCfgBegin could not establish an internet path during early boot.
    if (!bootTimeSynced) {
        bootTimeSynced = bootTimeNtpSyncDirectSta(cfgWifiSsid, cfgWifiPass);
    }

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

    LV_UNUSED(wifiConnected);
    lv_label_set_text(s_chatHeaderWifi, wifiApMode ? LV_SYMBOL_UPLOAD : LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(
        s_chatHeaderWifi,
        wifiApMode ? lv_color_hex(0xF4D35E) : lv_color_hex(0x84E07A),
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

static bool processMeshPacket(const MeshPacket &pkt) {
    if (isDuplicate(pkt.hdr.from, pkt.hdr.id)) return false;

    // Match v1 behavior: ignore reflected copies of our own transmitted packets.
    if (s_myNodeId != 0 && pkt.hdr.from == s_myNodeId) return false;

    Nodes.updateFromPacket(pkt);
    if (!pkt.decrypted) return false;

    switch (pkt.portnum) {
        case TEXT_MESSAGE_APP: {
            int chanIdx = (pkt.chanIdx >= 0 && pkt.chanIdx < MESH_CHANNELS) ? pkt.chanIdx : 0;

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
                return chanIdx == s_activeChannel;
            }
            return false;
        }

        case NODEINFO_APP: {
            UserInfo u = {};
            if (decodeUser(pkt.payload, pkt.payloadLen, u)) {
                Nodes.updateUser(pkt.hdr.from, u);
            }
            return false;
        }

        case POSITION_APP: {
            PositionInfo p = {};
            if (decodePosition(pkt.payload, pkt.payloadLen, p)) {
                Nodes.updatePosition(pkt.hdr.from, p);
            }
            return false;
        }

        case TELEMETRY_APP: {
            TelemetryInfo t = {};
            if (decodeTelemetry(pkt.payload, pkt.payloadLen, t)) {
                Nodes.updateTelemetry(pkt.hdr.from, t);
            }
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
    for (int row = 0; row < MAX_MSG_LINES && rowCount < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        if (shouldHideChatLine(dl->text)) continue;
        rows[rowCount++] = dl;
    }

    const bool stickToBottom = force || (lv_obj_get_scroll_bottom(s_chatList) <= 6);
    const int32_t prevScrollY = lv_obj_get_scroll_y(s_chatList);

    lv_obj_clean(s_chatList);
    lv_obj_t *lastMsgObj = nullptr;

    if (rowCount == 0) {
        lv_obj_t *empty = lv_label_create(s_chatList);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xD9E8FF), 0);
        lv_label_set_text(empty, "No messages yet");
    } else {
        for (int i = rowCount - 1; i >= 0; i--) {
            lv_obj_t *msg = lv_label_create(s_chatList);
            lastMsgObj = msg;
            lv_obj_set_width(msg, lv_pct(100));
            lv_obj_set_style_text_font(msg, &lv_font_montserrat_10, 0);
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
            if (i > 0) {
                uint32_t curId = rows[i]->packetId;
                uint32_t nextId = rows[i - 1]->packetId;
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

    if (stickToBottom) {
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
    const int panelW = 89;
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
    lv_obj_set_style_pad_row(panel, 5, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

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
    lv_label_set_text(s_chatShortcutText, "(C)FG   (L)egend");

    for (int i = 0; i < MESH_CHANNELS; i++) {
        lv_obj_t *btn = lv_btn_create(panel);
        s_channelBtns[i] = btn;
        lv_obj_set_width(btn, lv_pct(94));
        lv_obj_set_height(btn, 22);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);
        lv_obj_set_style_outline_width(btn, 0, 0);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn, onChannelPressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_height(lbl, lv_font_get_line_height(&lv_font_montserrat_10));
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
    delay(5);
}

#endif  // UI_LVGL_POC
