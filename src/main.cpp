#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "lgfx_tdeck.h"
#include "keyboard.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "node_db.h"
#include "live_util.h"
#include "channel_mgr.h"
#include "config_io.h"
#include "web_config.h"
#include "gps.h"
#include "dm_mgr.h"
#include "debug_flags.h"
#include "battery_util.h"
#include "display_profile.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sleep.h>
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#if defined(DEVICE_TLORA_PAGER_TFT)
#include <AudioBoard.h>
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
#include <driver/i2s.h>
#endif

// ── Chat spacing (runtime, set once at startup from gCfg.chatSpacing) ─
#if defined(DEVICE_TLORA_PAGER_TFT)
int LINE_H        = 13;     // default Normal
static const int kSpacingPx[] = { 10, 13, 16 };  // Tight, Normal, Loose
#else
int LINE_H        = 10;     // default Normal
static const int kSpacingPx[] = { 8, 10, 12 };   // Tight, Normal, Loose
#endif
int VISIBLE_LINES = (CHAT_H - 2) / LINE_H;

#if defined(DEVICE_TLORA_PAGER_TFT)
static constexpr float CHAT_WINDOW_TEXT_SCALE = 1.25f;
#else
static constexpr float CHAT_WINDOW_TEXT_SCALE = UI_BASE_TEXT_SCALE;
#endif

// ── Globals ───────────────────────────────────────────────────
static LGFX_TDeck   lcd;
static TDeckKeyboard kb;

// Node ID: in a later phase this will be stored in NVS.
// For now, derive from the ESP32 MAC address.
static uint32_t myNodeId = 0;
static RhinoConfig gCfg;

// Curve25519 key pair for PKI-encrypted DMs (declared extern in mesh_proto.h)
uint8_t myPubKey[32] = {};
uint8_t myPrivKey[32] = {};
uint8_t myDeviceRole  = 0;   // 0=CLIENT; set from gCfg.deviceRole after config load

// ── View state ────────────────────────────────────────────────
#define VIEW_GPS      MAX_CHANNELS
#define VIEW_MAP      (MAX_CHANNELS + 1)
#define VIEW_SETTINGS (MAX_CHANNELS + 2)
#define VIEW_NODES    (MAX_CHANNELS + 3)
#define TOTAL_VIEWS   (MAX_CHANNELS + 4)
static int  activeView   = 0;              // 0..9 channels, 10 GPS, 11 MAP, 12 settings, 13 nodes
static int  tabScrollX   = 0;             // horizontal scroll offset for tab bar (px)
static int  lastChannelView = 0;          // most recent real mesh channel (0..MESH_CHANNELS-1)
static int  panelReturnChannel = 0;       // channel to return to when closing DM/MAP/LIVE/CFG/NODES
#if defined(DEVICE_TLORA_PAGER_TFT)
static bool pagerWheelChatScrollMode = false;
#endif
static int  settingsSel  = 0;         // highlighted settings row
static int  settingsInfoScroll = 0;   // first visible read-only info row in CFG panel
static int  settingsInfoScrollMax = 0;
static int  mapsListSel = 0;          // highlighted node row in MAP panel
static int  nodesListSel = 0;         // highlighted node row in NODES panel
#if defined(DEVICE_TLORA_PAGER_TFT)
static const int SETTINGS_ROW_H = 10;
static const int SETTINGS_HDR_H = 16;
#else
static const int SETTINGS_ROW_H = 10;
static const int SETTINGS_HDR_H = 16;
#endif
static const int TOUCH_BTN_W = 58;
static const int TOUCH_BTN_H = 24;
static const int TOUCH_BTN_BOTTOM_PAD = 5;
static const int NAV_BTN_COUNT = 7;

struct PanelHitRect {
    int x;
    int y;
    int w;
    int h;
};
static bool panelCloseVisible = false;
static PanelHitRect panelCloseRect = {0, 0, 0, 0};
static bool dmNewVisible = false;
static PanelHitRect dmNewRect = {0, 0, 0, 0};

enum MapControlAction {
    MAP_CTL_ZOOM_IN = 0,
    MAP_CTL_ZOOM_OUT,
    MAP_CTL_LIST_PREV,
    MAP_CTL_LIST_NEXT,
    MAP_CTL_ME,
    MAP_CTL_COUNT,
};

enum SettingsControlAction {
    SETTINGS_CTL_UP = 0,
    SETTINGS_CTL_DOWN,
    SETTINGS_CTL_COUNT,
};

enum DmControlAction {
    DM_CTL_UP = 0,
    DM_CTL_DOWN,
    DM_CTL_COUNT,
};

enum NodesControlAction {
    NODES_CTL_UP = 0,
    NODES_CTL_DOWN,
    NODES_CTL_COUNT,
};

static bool mapCtlVisible[MAP_CTL_COUNT] = {};
static PanelHitRect mapCtlRect[MAP_CTL_COUNT] = {};
static bool settingsCtlVisible[SETTINGS_CTL_COUNT] = {};
static PanelHitRect settingsCtlRect[SETTINGS_CTL_COUNT] = {};
static bool dmCtlVisible[DM_CTL_COUNT] = {};
static PanelHitRect dmCtlRect[DM_CTL_COUNT] = {};
static bool nodesCtlVisible[NODES_CTL_COUNT] = {};
static PanelHitRect nodesCtlRect[NODES_CTL_COUNT] = {};

static bool  mapViewManual = false;
static float mapViewCenterLat = 0.0f;
static float mapViewCenterLon = 0.0f;
static float mapViewLatSpan = 180.0f;
static float mapViewLonSpan = 360.0f;
static const float MAP_MIN_LAT_SPAN = 0.05f;
static const float MAP_MIN_LON_SPAN = 0.05f;
static const int MAP_MAX_TILE_ZOOM = 17;
static float mapLastCenterLat = 0.0f;
static float mapLastCenterLon = 0.0f;
static float mapLastLatSpan = 180.0f;
static float mapLastLonSpan = 360.0f;
static uint32_t mapLastDrawMs = 0;
static bool mapNodeFreezeActive = false;
static uint32_t mapFrozenNodeIds[MAX_NODES];
static int mapFrozenNodeCount = 0;
static bool nodesNodeFreezeActive = false;
static uint32_t nodesFrozenNodeIds[MAX_NODES];
static int nodesFrozenNodeCount = 0;
static bool nodesWifiSessionActive = false;
static bool nodesWifiStateChanged = false;
static wifi_mode_t nodesWifiPrevMode = WIFI_OFF;
static bool nodesWifiPrevConnected = false;
static char nodesWifiPrevSsid[33] = {0};


static void setPanelCloseRect(int x, int y, int w, int h) {
    panelCloseVisible = true;
    panelCloseRect = { x, y, w, h };
}

static void clearPanelCloseRect() {
    panelCloseVisible = false;
    panelCloseRect = {0, 0, 0, 0};
    dmNewVisible = false;
    dmNewRect = {0, 0, 0, 0};
    for (int i = 0; i < MAP_CTL_COUNT; i++) {
        mapCtlVisible[i] = false;
        mapCtlRect[i] = {0, 0, 0, 0};
    }
    for (int i = 0; i < SETTINGS_CTL_COUNT; i++) {
        settingsCtlVisible[i] = false;
        settingsCtlRect[i] = {0, 0, 0, 0};
    }
    for (int i = 0; i < DM_CTL_COUNT; i++) {
        dmCtlVisible[i] = false;
        dmCtlRect[i] = {0, 0, 0, 0};
    }
    for (int i = 0; i < NODES_CTL_COUNT; i++) {
        nodesCtlVisible[i] = false;
        nodesCtlRect[i] = {0, 0, 0, 0};
    }
}

static void setDmNewRect(int x, int y, int w, int h) {
    dmNewVisible = true;
    dmNewRect = { x, y, w, h };
}

static void setMapControlRect(MapControlAction action, int x, int y, int w, int h) {
    int ai = (int)action;
    if (ai < 0 || ai >= MAP_CTL_COUNT) return;
    mapCtlVisible[ai] = true;
    mapCtlRect[ai] = { x, y, w, h };
}

static void setSettingsControlRect(SettingsControlAction action, int x, int y, int w, int h) {
    int ai = (int)action;
    if (ai < 0 || ai >= SETTINGS_CTL_COUNT) return;
    settingsCtlVisible[ai] = true;
    settingsCtlRect[ai] = { x, y, w, h };
}

static void setDmControlRect(DmControlAction action, int x, int y, int w, int h) {
    int ai = (int)action;
    if (ai < 0 || ai >= DM_CTL_COUNT) return;
    dmCtlVisible[ai] = true;
    dmCtlRect[ai] = { x, y, w, h };
}

static void setNodesControlRect(NodesControlAction action, int x, int y, int w, int h) {
    int ai = (int)action;
    if (ai < 0 || ai >= NODES_CTL_COUNT) return;
    nodesCtlVisible[ai] = true;
    nodesCtlRect[ai] = { x, y, w, h };
}

static bool isPanelView(int v) {
    return (v == CHAN_DM || v == CHAN_ANN || v == VIEW_MAP || v == VIEW_SETTINGS || v == VIEW_NODES);
}

static bool isTopTabView(int v) {
    return !isPanelView(v) && v != VIEW_GPS;
}

static void closePanelToChannel();
static void mapClampViewport();
static int mapVisibleNodeCount();
static NodeEntry *mapVisibleNodeByIndex(int idx);
static int nodesVisibleNodeCount();
static NodeEntry *nodesVisibleNodeByIndex(int idx);
static bool nodesPanelCanDownloadTiles();
static void nodesPanelWifiEnter();
static void nodesPanelWifiRestore();
static int panelOverlayBottomY();
static bool isTextInputView();
static void handleKey(char k);

// ── DM sub-state ──────────────────────────────────────────────
static bool     dmConvOpen   = false;  // true = showing conversation
static bool     dmPickerOpen = false;  // true = showing node picker ("New DM")
static int      dmListSel    = 0;      // selected conversation index in DM list
static int      dmPickerSel  = 0;      // selected row in node picker
static uint32_t dmConvNodeId = 0;      // node ID of open conversation
static bool     dmDeleteConfirm = false;
static uint32_t dmDeleteConfirmNodeId = 0;

// ── Dirty flags ───────────────────────────────────────────────
static bool dirtyStatus   = true;
static bool dirtyTabs     = true;
static bool dirtyChat     = true;
static bool dirtyLiveRows = false;
static bool dirtyNodes    = true;
static bool dirtyInput    = true;
static bool dirtyDivider  = false;

// ── Screen sleep state ────────────────────────────────────────
static bool     screenAsleep   = false;
static uint32_t lastActivityMs = 0;

static void setPagerKeyboardBacklight(bool on) {
#if defined(DEVICE_TLORA_PAGER_TFT) && defined(KB_BL) && (KB_BL >= 0)
    digitalWrite(KB_BL, on ? HIGH : LOW);
#else
    (void)on;
#endif
}

static void sleepScreen(const char *reason) {
    lcd.setBrightness(0);
    setPagerKeyboardBacklight(false);
    screenAsleep = true;
    if (reason && reason[0]) {
        Serial.printf("[screen] sleeping (%s)\n", reason);
    } else {
        Serial.printf("[screen] sleeping\n");
    }
}

static void wakeScreen() {
    lcd.setBrightness(128);
    setPagerKeyboardBacklight(true);
    screenAsleep   = false;
    lastActivityMs = millis();
    // Force full redraw so nothing stale is visible after the backlight returns
    dirtyStatus = dirtyTabs = dirtyChat = dirtyNodes = dirtyInput = true;
    Serial.printf("[screen] woke\n");
}

// ── Input state ───────────────────────────────────────────────
static char   inputBuf[MAX_INPUT_LEN + 1] = {0};
static int    inputLen  = 0;
static bool   cursorOn  = true;
static uint32_t lastBlink      = 0;
static uint32_t lastNodeInfo   = 0;
static uint32_t lastPosition   = 0;
static bool     touchDown      = false;
static int32_t  touchStartX    = 0;
static int32_t  touchStartY    = 0;
static int32_t  touchLastX     = 0;
static int32_t  touchLastY     = 0;
static uint32_t touchDownMs    = 0;
static bool     softKbVisible  = false;
static bool     softKbShift    = false;
static bool     hwTypingLock   = false;

#define KBD_QUEUE_SIZE 96
static char    kbdQueue[KBD_QUEUE_SIZE] = {0};
static uint8_t kbdQHead = 0;
static uint8_t kbdQTail = 0;
static uint8_t kbdQCount = 0;

static void queueKey(char k) {
    if (kbdQCount >= KBD_QUEUE_SIZE) {
        // Drop oldest when full so newest keypresses still get through.
        kbdQTail = (uint8_t)((kbdQTail + 1) % KBD_QUEUE_SIZE);
        kbdQCount--;
    }
    kbdQueue[kbdQHead] = k;
    kbdQHead = (uint8_t)((kbdQHead + 1) % KBD_QUEUE_SIZE);
    kbdQCount++;
}

static bool dequeueKey(char &out) {
    if (kbdQCount == 0) return false;
    out = kbdQueue[kbdQTail];
    kbdQTail = (uint8_t)((kbdQTail + 1) % KBD_QUEUE_SIZE);
    kbdQCount--;
    return true;
}

static void pumpKeyboardRaw(uint8_t maxReads, uint32_t nowMs) {
    for (uint8_t i = 0; i < maxReads; i++) {
        char k = kb.readKey();
        if (k == KEY_NONE) break;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    uint8_t rawKey = (uint8_t)k;
    if (rawKey == 0x28 || rawKey == 0x0D) k = KEY_ENTER;
    if (rawKey == 0x29) k = KEY_ESCAPE;
    if (rawKey == 0x2A || rawKey == 0x4C || rawKey == 0x08 || rawKey == 0x7F) k = KEY_BACKSPACE;
#endif
        if (screenAsleep) {
            wakeScreen();
#if defined(DEVICE_CARDPUTER_LORA_HAT)
            if (k == KEY_ENTER) {
                handleKey(k);
            }
#endif
            break;
        }
        lastActivityMs = nowMs;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        handleKey(k);
#else
        queueKey(k);
#endif
    }
}
// Broadcast intervals are runtime-configurable via gCfg.nodeInfoIntervalS / posIntervalS

// ── Packet counter ────────────────────────────────────────────
static uint32_t pktCount = 0;

// ── Packet deduplication (circular buffer of seen IDs) ────────
#define DEDUP_SIZE 32
static uint32_t seenIds[DEDUP_SIZE] = {0};
static int      seenHead = 0;

static bool isDuplicate(uint32_t id) {
    for (int i = 0; i < DEDUP_SIZE; i++)
        if (seenIds[i] == id) return true;
    seenIds[seenHead] = id;
    seenHead = (seenHead + 1) % DEDUP_SIZE;
    return false;
}


// ── SD card wipe helper ───────────────────────────────────────
// Recursively delete all files under a directory, then remove it.
static void sdRmDir(const char *path) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f) {
        if (f.isDirectory()) {
            String sub = String(path) + "/" + f.name();
            f.close();
            sdRmDir(sub.c_str());
        } else {
            String fp = String(path) + "/" + f.name();
            f.close();
            SD.remove(fp.c_str());
        }
        f = dir.openNextFile();
    }
    dir.close();
    SD.rmdir(path);
}

// ── Settings ──────────────────────────────────────────────────
#define SETTING_WEBCFG        0
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK) || defined(DEVICE_CARDPUTER_LORA_HAT)
#define CFG_MSG_ALERT_TOGGLE  1
#else
#define CFG_MSG_ALERT_TOGGLE  0
#endif

#if HAS_SD_CARD
#define SETTING_EXPORT        1
#define SETTING_IMPORT        2
#define SETTING_THEME         3
#define SETTING_ANNOUNCE      4
#if CFG_MSG_ALERT_TOGGLE
#define SETTING_MSG_ALERT     5
#define SETTING_SPLASH_MELODY 6
#define SETTING_CLEAR_MSGS    7
#define SETTING_CLEAR_NODES   8
#define SETTING_FACTORY_RESET 9
#define NUM_SETTINGS          10
#else
#define SETTING_SPLASH_MELODY 5
#define SETTING_CLEAR_MSGS    6
#define SETTING_CLEAR_NODES   7
#define SETTING_FACTORY_RESET 8
#define NUM_SETTINGS          9
#endif
#else
#define SETTING_THEME         1
#define SETTING_ANNOUNCE      2
#if CFG_MSG_ALERT_TOGGLE
#define SETTING_MSG_ALERT     3
#define SETTING_SPLASH_MELODY 4
#define SETTING_CLEAR_MSGS    5
#define SETTING_CLEAR_NODES   6
#define SETTING_FACTORY_RESET 7
#define NUM_SETTINGS          8
#else
#define SETTING_SPLASH_MELODY 3
#define SETTING_CLEAR_MSGS    4
#define SETTING_CLEAR_NODES   5
#define SETTING_FACTORY_RESET 6
#define NUM_SETTINGS          7
#endif
#endif

static char settingsStatus[LCD_W / CHAR_W + 1] = "";

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

static UiPalette gUi = {};

static constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

static inline uint16_t gpsFixColorForUiMode() {
    // Keep GPS fix distinctly green on every theme.
    // Light themes use a darker shade for better contrast on pale backgrounds.
    return (gCfg.uiMode == UI_MODE_LIGHT)
        ? rgb565(0x1f, 0x7a, 0x2f)
        : rgb565(0x3a, 0xe0, 0x58);
}

#define COL_BG_MAIN        gUi.bgMain
#define COL_STATUS_TOP     gUi.statusTop
#define COL_STATUS_BG      gUi.statusBg
#define COL_PANEL_BG       gUi.panelBg
#define COL_PANEL_ALT      gUi.panelAlt
#define COL_PANEL_STRONG   gUi.panelStrong
#define COL_TAB_ACTIVE     gUi.tabActive
#define COL_TAB_UNREAD     gUi.tabUnread
#define COL_TAB_IDLE       gUi.tabIdle
#define COL_DIVIDER        gUi.divider
#define COL_DIVIDER_HI     gUi.dividerHi
#define COL_INPUT_BG       gUi.inputBg
#define COL_INPUT_TOP      gUi.inputTop
#define COL_TEAL           gUi.accent
#define COL_CURSOR         gUi.cursor
#define COL_TEXT_MAIN      gUi.textMain
#define COL_TEXT_DIM       gUi.textDim
#define COL_TEXT_ON_ACCENT gUi.textOnAccent
#define COL_STATUS_TEXT    gUi.statusText
#define COL_SELECT_BG      gUi.selectBg
#define COL_SELECT_ACCENT  gUi.selectAccent
#define COL_NODE_HOT       gUi.nodeHot
#define COL_NODE_WARM      gUi.nodeWarm
#define COL_DM_MUTED       gUi.dmMuted
#define COL_BATT_GOOD      gUi.battGood
#define COL_BATT_WARN      gUi.battWarn
#define COL_BATT_BAD       gUi.battBad
#define COL_SPLASH_TOP     gUi.splashTop
#define COL_SPLASH_BOTTOM  gUi.splashBottom
#define COL_SPLASH_CARD    gUi.splashCardBg
#define COL_SPLASH_EDGE    gUi.splashCardEdge
#define COL_SPLASH_EDGE_HI gUi.splashCardEdgeHi
#define COL_SPLASH_TITLE   gUi.splashTitle
#define COL_SPLASH_SUB     gUi.splashSub
#define COL_SPLASH_DIM     gUi.splashDim

struct UiThemePreset {
    uint8_t theme;
    uint8_t mode;
    uint16_t bgMain;
    uint16_t panelBg;
    uint16_t panelAlt;
    uint16_t accent;
    const char *name;
};

static constexpr uint8_t UI_THEME_PRESET_COUNT = 8;
static const UiThemePreset kUiThemePresets[UI_THEME_PRESET_COUNT] = {
    { UI_THEME_CAMELLIA, UI_MODE_DARK,  0x0843, 0x1065, 0x18A7, 0xDA8E, "Camillia Dark" },
    { UI_THEME_CAMELLIA, UI_MODE_LIGHT, 0xFF5D, 0xFFDF, 0xFF1B, 0xB964, "Camillia Light" },
    { UI_THEME_EVERGREEN, UI_MODE_DARK,  0x00A8, 0x11AA, 0x1A2C, 0x55B0, "Evergreen Dark" },
    { UI_THEME_EVERGREEN, UI_MODE_LIGHT, 0xE73C, 0xF7DE, 0xE71B, 0x2D2A, "Evergreen Light" },
    { UI_THEME_EARTHEN, UI_MODE_DARK,  0x1082, 0x2104, 0x2945, 0xD38B, "Earthy Dark" },
    { UI_THEME_EARTHEN, UI_MODE_LIGHT, 0xF7DE, 0xFFDF, 0xF75C, 0xB40B, "Earthy Light" },
    { UI_THEME_SOLARIZED, UI_MODE_DARK,
        rgb565(0x00, 0x2b, 0x36), rgb565(0x07, 0x36, 0x42), rgb565(0x0c, 0x3c, 0x47), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Dark" },
    { UI_THEME_SOLARIZED, UI_MODE_LIGHT,
        rgb565(0xfd, 0xf6, 0xe3), rgb565(0xfd, 0xf6, 0xe3), rgb565(0xee, 0xe8, 0xd5), rgb565(0x2a, 0xa1, 0x98),
        "Solarized Light" },
};

static uint8_t gActiveUiThemePreset = 0;

static uint8_t uiThemePresetIndexFromCfg() {
    for (uint8_t i = 0; i < UI_THEME_PRESET_COUNT; i++) {
        if (kUiThemePresets[i].theme == gCfg.uiTheme
            && kUiThemePresets[i].mode == gCfg.uiMode) {
            return i;
        }
    }
    return 0;
}

static uint8_t uiThemePresetIndex() {
    return (uint8_t)(gActiveUiThemePreset % UI_THEME_PRESET_COUNT);
}

static const char *uiThemePresetName(uint8_t preset) {
    return kUiThemePresets[preset % UI_THEME_PRESET_COUNT].name;
}

static void setUiThemePreset(uint8_t preset) {
    const UiThemePreset &p = kUiThemePresets[preset % UI_THEME_PRESET_COUNT];
    gCfg.uiTheme = p.theme;
    gCfg.uiMode = p.mode;
}

static void persistUiTheme() {
    Preferences p;
    p.begin("camillia", false);
    p.putUChar("uiTheme", gCfg.uiTheme);
    p.putUChar("uiMode", gCfg.uiMode);
    p.end();
}

static void persistMessageAlertSetting() {
    Preferences p;
    p.begin("camillia", false);
    p.putUChar("msgAlertSound", gCfg.msgAlertSound);
    p.end();
}

static void persistSplashMelodySetting() {
    Preferences p;
    p.begin("camillia", false);
    p.putBool("splashMelody", gCfg.splashMelodyEnabled);
    p.end();
}

static const char *msgAlertSoundName(uint8_t mode) {
    switch (mode) {
        case MSG_ALERT_SOUND_CHIRPY: return "Chirpy";
        case MSG_ALERT_SOUND_BASS:   return "Bass";
        case MSG_ALERT_SOUND_OFF:    return "Off";
        case MSG_ALERT_SOUND_DEFAULT:
        default:                     return "Default";
    }
}

static void applyUiTheme(bool markDirty = true) {
    gCfg.uiTheme = (uint8_t)constrain((int)gCfg.uiTheme, 0, UI_THEME_COUNT - 1);
    gCfg.uiMode  = (uint8_t)(gCfg.uiMode == UI_MODE_LIGHT ? UI_MODE_LIGHT : UI_MODE_DARK);
    gActiveUiThemePreset = uiThemePresetIndexFromCfg();

    if (gCfg.uiTheme == UI_THEME_EARTHEN) {
        if (gCfg.uiMode == UI_MODE_LIGHT) {
            gUi = {
                0xF7DE, 0xE6BA, 0xE658, 0xFFDF, 0xF75C, 0xEEB9,
                0x4228, 0xB40B, 0x7B6D, 0xBD14, 0xCDB6, 0xF75C, 0xEEB9,
                0xB40B, 0xB40B, 0x31A6, 0x6B4D, 0xFFFF, 0x39C7,
                0xDDF7, 0xB40B, 0x9B65, 0xA3C8, 0x8C30,
                0x3666, 0xBC40, 0xA000,
                0xE6DA, 0xFFDF, 0xF75C, 0xCDB6, 0xDE58, 0x4228, 0x6B4D, 0x9CD3
            };
        } else {
            gUi = {
                0x1082, 0x2104, 0x18C3, 0x2104, 0x2945, 0x3186,
                0xFDD0, 0xE4A8, 0x8C71, 0x5AEB, 0x736D, 0x2945, 0x39A7,
                0xD38B, 0xD38B, 0xFFDF, 0xC618, 0xFFFF, 0xF7DE,
                0x6B4D, 0xC38A, 0xE4A8, 0xB40B, 0xA514,
                0x3666, 0xED80, 0xA000,
                0x18A3, 0x4228, 0x2966, 0x6B2C, 0x83AE, 0xFFDF, 0xDEBA, 0xBDF7
            };
        }
    } else if (gCfg.uiTheme == UI_THEME_EVERGREEN) {
        if (gCfg.uiMode == UI_MODE_LIGHT) {
            gUi = {
                0xE73C, 0xD697, 0xC5F4, 0xF7DE, 0xE71B, 0xDEB9,
                0x2148, 0xA321, 0x5B0D, 0xA4F2, 0xBDB4, 0xE71B, 0xD677,
                0x2D2A, 0x2D2A, 0x2148, 0x636E, 0xFFFF, 0x2148,
                0x2D2A, 0x45AD, 0x1CAA, 0x2148, 0x7BAF,
                0x2DA6, 0xBC40, 0xA000,
                0xD697, 0xF7DE, 0xEF7C, 0xA4F2, 0xBDB4, 0x2148, 0x4AED, 0x7C31
            };
        } else {
            gUi = {
                0x00A8, 0x19EC, 0x114A, 0x11AA, 0x1A2C, 0x1A0B,
                0xFFFF, 0xFD20, 0x8CF1, 0x3B8F, 0x4C31, 0x1A0B, 0x2B2D,
                0x55B0, 0x55B0, 0xFFFF, 0xA554, 0xFFFF, 0xE77D,
                0x2AED, 0x55B0, 0x86FF, 0xE73C, 0xC69A,
                0x3666, 0xED80, 0xA000,
                0x00A8, 0x228D, 0x1169, 0x4C31, 0x64D4, 0xFFFF, 0xB69A, 0x9D75
            };
        }
    } else if (gCfg.uiTheme == UI_THEME_SOLARIZED) {
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

        if (gCfg.uiMode == UI_MODE_LIGHT) {
            gUi = {
                base3, base2, base2, base3, base2, rgb565(0xe7, 0xe1, 0xcf),
                blue, orange, base1, base1, base0, base2, base2,
                cyan, blue, base01, base00, base3, base01,
                rgb565(0xe8, 0xe2, 0xd0), cyan, blue, yellow, violet,
                green, yellow, red,
                base2, base3, rgb565(0xf8, 0xf1, 0xdd), base1, base0, blue, cyan, base00
            };
        } else {
            gUi = {
                base03, base02, base02, base02, rgb565(0x0c, 0x3c, 0x47), rgb565(0x11, 0x45, 0x52),
                blue, orange, base01, base01, base00, base02, base02,
                cyan, yellow, base1, base0, base3, base1,
                rgb565(0x0e, 0x46, 0x55), cyan, blue, yellow, violet,
                green, yellow, red,
                base03, base02, rgb565(0x0b, 0x40, 0x4b), base01, base00, base3, cyan, base0
            };
        }
    } else {
        if (gCfg.uiMode == UI_MODE_LIGHT) {
            gUi = {
                0xFF5D, 0xFD95, 0xFCF2, 0xFFDF, 0xFF1B, 0xFE96,
                0x3127, 0xC983, 0x73AE, 0xBC92, 0xCD34, 0xFF1B, 0xFCD2,
                0xB964, 0xB964, 0x20E6, 0x62CC, 0xFFFF, 0x2927,
                0xB964, 0xDA8E, 0x2C8D, 0x2927, 0x8B2F,
                0x2DA6, 0xBC40, 0xA000,
                0xFE97, 0xFFDF, 0xFF9D, 0xBCB2, 0xCD54, 0x2927, 0x6AAB, 0x83AE
            };
        } else {
            gUi = {
                0x0843, 0x18A7, 0x1045, 0x1065, 0x18A7, 0x1846,
                0xFFFF, 0xF46B, 0xA4B2, 0x39A8, 0x4A2A, 0x1846, 0x7228,
                0xDA8E, 0xDA8E, 0xFFFF, 0xB596, 0xFFFF, 0xF79E,
                0x7228, 0xDA8E, 0x66FF, 0xDEFB, 0xCE59,
                0x2DA6, 0xFD20, 0xA000,
                0x0801, 0x49C8, 0x1023, 0x6AAE, 0x83B2, 0xFFFF, 0xF6FB, 0xB596
            };
        }
    }

    if (markDirty) {
        dirtyStatus = dirtyTabs = dirtyChat = dirtyNodes = dirtyInput = true;
        dirtyDivider = true;
    }
}

// ── Node list focus / detail ───────────────────────────────────
static bool     nodeListFocused = false;
static int      nodeListSel     = 0;
static bool     nodeDetailOpen  = false;
static uint32_t nodeDetailId    = 0;

// ── View navigation helpers ───────────────────────────────────
static bool isViewNavigable(int v) {
    if (v >= 0 && v < MESH_CHANNELS)
        return CHANNEL_KEYS[v].name[0] != '\0';
    return true;  // panel + utility views are always reachable
}

static int nextView(int from) {
    for (int n = 1; n < TOTAL_VIEWS; n++) {
        int v = (from + n) % TOTAL_VIEWS;
        if (isTopTabView(v) && isViewNavigable(v)) return v;
    }
    return from;
}

static int prevView(int from) {
    for (int n = 1; n < TOTAL_VIEWS; n++) {
        int v = (from + TOTAL_VIEWS - n) % TOTAL_VIEWS;
        if (isTopTabView(v) && isViewNavigable(v)) return v;
    }
    return from;
}

static void goToView(int v);

static int nextMeshChannelView(int from) {
    if (from < 0 || from >= MESH_CHANNELS) return from;
    for (int n = 1; n < MESH_CHANNELS; n++) {
        int v = (from + n) % MESH_CHANNELS;
        if (isViewNavigable(v)) return v;
    }
    return from;
}

static int prevMeshChannelView(int from) {
    if (from < 0 || from >= MESH_CHANNELS) return from;
    for (int n = 1; n < MESH_CHANNELS; n++) {
        int v = (from + MESH_CHANNELS - n) % MESH_CHANNELS;
        if (isViewNavigable(v)) return v;
    }
    return from;
}

static bool useCompactKeyboardUi() {
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT)
    return true;
#else
    return false;
#endif
}

static int navButtonCount() {
    return useCompactKeyboardUi() ? 5 : NAV_BTN_COUNT;
}

static const char *navButtonLabel(int idx) {
    if (useCompactKeyboardUi()) {
        static const char *labels[] = { "DM", "MAP", "LIVE", "CFG", "NODES" };
        return labels[idx];
    }
    static const char *labels[] = { "Prev", "DM", "MAP", "LIVE", "CFG", "NODES", "Next" };
    return labels[idx];
}

static void activateNavButton(int idx) {
    if (useCompactKeyboardUi()) {
        switch (idx) {
            case 0:
                if (activeView != CHAN_DM) goToView(CHAN_DM);
                break;
            case 1:
                if (activeView != VIEW_MAP) goToView(VIEW_MAP);
                break;
            case 2:
                if (activeView != CHAN_ANN) goToView(CHAN_ANN);
                break;
            case 3:
                if (activeView != VIEW_SETTINGS) goToView(VIEW_SETTINGS);
                break;
            case 4:
                if (activeView != VIEW_NODES) goToView(VIEW_NODES);
                break;
            default:
                break;
        }
        return;
    }

    switch (idx) {
        case 0:
            goToView(prevView(activeView));
            break;
        case 1:
            if (activeView != CHAN_DM) goToView(CHAN_DM);
            break;
        case 2:
            if (activeView != VIEW_MAP) goToView(VIEW_MAP);
            break;
        case 3:
            if (activeView != CHAN_ANN) goToView(CHAN_ANN);
            break;
        case 4:
            if (activeView != VIEW_SETTINGS) goToView(VIEW_SETTINGS);
            break;
        case 5:
            if (activeView != VIEW_NODES) goToView(VIEW_NODES);
            break;
        case 6:
            goToView(nextView(activeView));
            break;
        default:
            break;
    }
}

static bool cardputerChannelNavReady() {
    if (useCompactKeyboardUi()) {
    bool typing = softKbVisible || hwTypingLock || inputLen > 0;
    return !typing
        && !nodeDetailOpen
        && !nodeListFocused
        && activeView >= 0
        && activeView < MESH_CHANNELS;
    }
    return false;
}

static bool cardputerPanelShortcutReady() {
    if (useCompactKeyboardUi()) {
    if (softKbVisible) return false;
    // Keep panel hotkeys active in non-text views (CFG/ANN/MAP/DM list),
    // even if the chat input buffer still contains stale text.
    if (isTextInputView() && (hwTypingLock || inputLen > 0)) return false;
    return true;
    }
#if defined(DEVICE_TDECK)
    if (softKbVisible) return false;
    if (isTextInputView() && (hwTypingLock || inputLen > 0)) return false;
    return true;
#endif
    return false;
}

static bool showPanelScrollButtons() {
    return !useCompactKeyboardUi();
}

static bool showPanelCloseButtons() {
    return !useCompactKeyboardUi();
}

static char remapCardputerDirectionalKey(char k) {
    if (!cardputerPanelShortcutReady()) return k;

    if (activeView == VIEW_MAP) {
        if (k == ';') return KEY_SCROLL_UP;
        if (k == '.') return KEY_SCROLL_DN;
        if (k == ',') return KEY_PAGE_DN;
        if (k == '/') return KEY_PAGE_UP;
        return k;
    }

    if (k == ';') return KEY_SCROLL_UP;
    if (k == '.') return KEY_SCROLL_DN;
    if (cardputerChannelNavReady()) {
        if (k == ',') return KEY_PREV_CHAN;
        if (k == '/') return KEY_NEXT_CHAN;
    }
    return k;
}

// ── View navigation ───────────────────────────────────────────
static void goToView(int v) {
    if (v < 0 || v >= TOTAL_VIEWS) return;

    int prev = activeView;

    if (isPanelView(v) && activeView != v) {
        panelReturnChannel = (activeView >= 0 && activeView < MESH_CHANNELS)
            ? activeView
            : lastChannelView;
    }

    bool wasFullWidth = (activeView == VIEW_SETTINGS || activeView == VIEW_GPS || activeView == VIEW_MAP
                         || activeView == VIEW_NODES
                         || activeView == CHAN_DM);
    activeView = v;
    clearPanelCloseRect();
    nodeListFocused = false;
    nodeDetailOpen  = false;
    dmConvOpen      = false;   // reset DM sub-state on any navigation
    dmPickerOpen    = false;
    dmListSel       = 0;
    dmPickerSel     = 0;
    dmDeleteConfirm = false;
    dmDeleteConfirmNodeId = 0;
#if defined(DEVICE_TLORA_PAGER_TFT)
    pagerWheelChatScrollMode = false;
#endif
    softKbVisible   = false;
    softKbShift     = false;
    hwTypingLock    = false;
    // If navigating to DM tab and there's an unread conversation, open it immediately
    if (v == CHAN_DM) {
        for (int i = 0; i < DMs.count(); i++) {
            DmConv *c = DMs.getByRank(i);
            if (c && c->unread) {
                DMs.markRead(c->nodeId);
                dmConvNodeId = c->nodeId;
                dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                if (inputLen == 0) hwTypingLock = true;
#endif
                break;
            }
        }
        // Even if no unread, pre-select the most recent row.
        if (!dmConvOpen && DMs.count() > 0) {
    #if defined(DEVICE_TLORA_PAGER_TFT)
            dmListSel = 0;
    #else
            dmListSel = 1;
    #endif
        }
    }
    if (v == VIEW_MAP) {
        if (prev != VIEW_MAP) {
            mapFrozenNodeCount = 0;
            int cnt = Nodes.count();
            for (int i = 0; i < cnt && mapFrozenNodeCount < MAX_NODES; i++) {
                NodeEntry *n = Nodes.getByRank(i);
                if (!n) continue;
                mapFrozenNodeIds[mapFrozenNodeCount++] = n->nodeId;
            }
            mapNodeFreezeActive = true;
        }
        mapsListSel = constrain(mapsListSel, 0, max(0, mapNodeFreezeActive ? (mapFrozenNodeCount - 1) : (Nodes.count() - 1)));
    } else if (prev == VIEW_MAP) {
        mapNodeFreezeActive = false;
        mapFrozenNodeCount = 0;
    }
    if (v == CHAN_ANN && prev != CHAN_ANN) {
        Channels.clearChannel(CHAN_ANN);
        dirtyLiveRows = false;
    }
    if (v == VIEW_SETTINGS && prev != VIEW_SETTINGS) {
        settingsInfoScroll = 0;
        settingsInfoScrollMax = 0;
    }
    if (v == VIEW_NODES) {
        if (prev != VIEW_NODES) {
            nodesPanelWifiEnter();
            nodesFrozenNodeCount = 0;
            int cnt = Nodes.count();
            for (int i = 0; i < cnt && nodesFrozenNodeCount < MAX_NODES; i++) {
                NodeEntry *n = Nodes.getByRank(i);
                if (!n) continue;
                nodesFrozenNodeIds[nodesFrozenNodeCount++] = n->nodeId;
            }
            nodesNodeFreezeActive = true;
        }
        nodesListSel = constrain(nodesListSel, 0,
                                 max(0, nodesNodeFreezeActive
                                       ? (nodesFrozenNodeCount - 1)
                                       : (Nodes.count() - 1)));
    } else if (prev == VIEW_NODES) {
        nodesPanelWifiRestore();
        nodesNodeFreezeActive = false;
        nodesFrozenNodeCount = 0;
    }
    if (v >= 0 && v < MESH_CHANNELS) {
        lastChannelView = v;
    }
    if (v < MESH_CHANNELS || v == CHAN_ANN) {
        Channels.setActive(v);
        if (wasFullWidth) dirtyDivider = true;   // restore divider after leaving full-width views
    }
    dirtyTabs = dirtyChat = dirtyNodes = dirtyStatus = dirtyInput = true;
}

static void closePanelToChannel() {
    int target = panelReturnChannel;
    if (target < 0 || target >= MESH_CHANNELS || !isViewNavigable(target)) {
        target = lastChannelView;
    }
    if (target < 0 || target >= MESH_CHANNELS || !isViewNavigable(target)) {
        for (int i = 0; i < MESH_CHANNELS; i++) {
            if (isViewNavigable(i)) { target = i; break; }
        }
    }
    if (target >= 0 && target < MESH_CHANNELS) goToView(target);
}

// ── Splash screen ─────────────────────────────────────────────
static void drawCamelliaMarkTiny(int cx, int cy) {
    const uint16_t PETAL_OUTER = COL_SPLASH_TITLE;
    const uint16_t PETAL_MID   = COL_SPLASH_SUB;
    const uint16_t PETAL_EDGE  = COL_DIVIDER_HI;
    const uint16_t CENTER      = COL_TEAL;

    for (int i = 0; i < 6; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 6.0f) + 0.20f;
        int px = cx + (int)(5.0f * cosf(a));
        int py = cy + (int)(4.0f * sinf(a));
        lcd.fillCircle(px, py, 2, PETAL_OUTER);
        lcd.drawCircle(px, py, 2, PETAL_EDGE);
    }

    for (int i = 0; i < 4; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 4.0f) + 0.45f;
        int px = cx + (int)(2.0f * cosf(a));
        int py = cy + (int)(2.0f * sinf(a));
        lcd.fillCircle(px, py, 1, PETAL_MID);
    }

    lcd.fillCircle(cx, cy, 2, CENTER);
    lcd.drawPixel(cx, cy, COL_TEXT_ON_ACCENT);
}

static void drawSplash() {
    const DisplayUiProfile &ui = displayUiProfile();
    const DisplaySplashPalette palette = {
        COL_SPLASH_TOP,
        COL_SPLASH_BOTTOM,
        COL_SPLASH_CARD,
        COL_SPLASH_EDGE,
        COL_SPLASH_EDGE_HI,
        COL_SPLASH_TITLE,
        COL_SPLASH_DIM,
        COL_BG_MAIN,
    };
    const DisplaySplashData data = {
        gCfg.nodeLong,
        gCfg.nodeShort,
        APP_VERSION,
    };

    displayDrawSplash(lcd, ui, palette, data);
}

// ── Colour helpers ────────────────────────────────────────────

static uint16_t lerp565(uint16_t c1, uint16_t c2, uint8_t t) {
    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    int r = r1 + ((r2 - r1) * t) / 255;
    int g = g1 + ((g2 - g1) * t) / 255;
    int b = b1 + ((b2 - b1) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void fillVerticalGradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom) {
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        uint8_t t = (uint8_t)((255UL * i) / max(1, h - 1));
        lcd.drawFastHLine(x, y + i, w, lerp565(top, bottom, t));
    }
}

static void drawPanelFrame(int x, int y, int w, int h, uint16_t bg, uint16_t edge) {
    lcd.fillRect(x, y, w, h, bg);
    lcd.drawRect(x, y, w, h, edge);
}

static void drawClippedText(int x, int y, int maxW, const char *text) {
    if (!text || maxW <= 0) return;
    if (lcd.textWidth(text) <= maxW) {
        lcd.drawString(text, x, y);
        return;
    }
    String s(text);
    const char *tail = "...";
    int tailW = lcd.textWidth(tail);
    while (s.length() > 0 && lcd.textWidth(s.c_str()) + tailW > maxW) {
        s.remove(s.length() - 1);
    }
    s += tail;
    lcd.drawString(s.c_str(), x, y);
}

static void drawSquirclePill(int x, int y, int w, int h,
                             uint16_t fill, uint16_t stroke, bool emph = false) {
    if (w < 6 || h < 6) return;
    int r = min(max(2, h / 2 - 1), 6);
    lcd.fillRoundRect(x, y, w, h, r, fill);
    lcd.drawRoundRect(x, y, w, h, r, stroke);
    if (emph && w > 8 && h > 8) {
        int r2 = max(1, r - 1);
        lcd.drawRoundRect(x + 1, y + 1, w - 2, h - 2, r2, COL_SELECT_ACCENT);
    }
}

// ── Battery reading ───────────────────────────────────────────
static uint8_t readBatteryPct() {
    return batteryReadPercent();
}

static uint8_t _battPct = 0;
static bool gNtpConfigured = false;
static char gNtpServerActive[48] = "";
static uint32_t gNtpLastConfigureMs = 0;

static void applyTimezoneFromConfig() {
    const char *tz = (gCfg.tzDef[0]) ? gCfg.tzDef : "UTC0";
    setenv("TZ", tz, 1);
    tzset();
}

static bool wifiHasInternetTimePath() {
    if (WiFi.status() != WL_CONNECTED) return false;
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_AP;
}

static bool nodesPanelCanDownloadTiles() {
    return WiFi.status() == WL_CONNECTED;
}

static void nodesPanelWifiEnter() {
    if (nodesWifiSessionActive) return;

    nodesWifiSessionActive = true;
    nodesWifiStateChanged = false;
    nodesWifiPrevMode = WiFi.getMode();
    nodesWifiPrevConnected = (WiFi.status() == WL_CONNECTED);
    nodesWifiPrevSsid[0] = '\0';
    if (nodesWifiPrevConnected) {
        String ssid = WiFi.SSID();
        ssid.toCharArray(nodesWifiPrevSsid, sizeof(nodesWifiPrevSsid));
    }

    if (nodesWifiPrevConnected) return;
    if (!gCfg.wifiSsid[0]) return;

    switch (nodesWifiPrevMode) {
        case WIFI_OFF:
            WiFi.mode(WIFI_STA);
            WiFi.begin(gCfg.wifiSsid, gCfg.wifiPass);
            nodesWifiStateChanged = true;
            break;
        case WIFI_STA:
            WiFi.begin(gCfg.wifiSsid, gCfg.wifiPass);
            nodesWifiStateChanged = true;
            break;
#ifdef WIFI_AP_STA
        case WIFI_AP:
            WiFi.mode(WIFI_AP_STA);
            WiFi.begin(gCfg.wifiSsid, gCfg.wifiPass);
            nodesWifiStateChanged = true;
            break;
#endif
        case WIFI_AP_STA:
            WiFi.begin(gCfg.wifiSsid, gCfg.wifiPass);
            nodesWifiStateChanged = true;
            break;
        default:
            break;
    }
}

static void nodesPanelWifiRestore() {
    if (!nodesWifiSessionActive) return;

    if (nodesWifiStateChanged) {
        switch (nodesWifiPrevMode) {
            case WIFI_OFF:
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                break;
            case WIFI_STA:
                WiFi.mode(WIFI_STA);
                if (!nodesWifiPrevConnected) {
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
                if (!nodesWifiPrevConnected) {
                    WiFi.disconnect(false);
                }
                break;
#endif
            default:
                break;
        }
    }

    nodesWifiSessionActive = false;
    nodesWifiStateChanged = false;
    nodesWifiPrevMode = WIFI_OFF;
    nodesWifiPrevConnected = false;
    nodesWifiPrevSsid[0] = '\0';
}

static const char *configuredNtpServer() {
    return gCfg.ntpServer[0] ? gCfg.ntpServer : MY_NTP_SERVER;
}

static void ensureNtpConfigured() {
    if (!wifiHasInternetTimePath()) return;

    const char *srv = configuredNtpServer();
    if (gNtpConfigured && strcmp(gNtpServerActive, srv) == 0
        && (millis() - gNtpLastConfigureMs) < 21600000UL) {
        return;
    }

    configTime(0, 0, srv);
    // configTime can leave TZ handling in UTC; re-apply configured timezone.
    applyTimezoneFromConfig();
    strncpy(gNtpServerActive, srv, sizeof(gNtpServerActive) - 1);
    gNtpServerActive[sizeof(gNtpServerActive) - 1] = '\0';
    gNtpConfigured = true;
    gNtpLastConfigureMs = millis();
    Serial.printf("[time] NTP configured: %s\n", gNtpServerActive);
}

static bool ntpSyncSystemClock() {
    if (!wifiHasInternetTimePath()) return false;
    ensureNtpConfigured();
    return time(nullptr) >= 1700000000;
}

// Civil date -> Unix days since 1970-01-01 (UTC), valid for Gregorian dates.
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool gpsSyncSystemClock() {
    int y, mon, d, hh, mm, ss;
    if (!gpsUtcDateTime(y, mon, d, hh, mm, ss)) return false;
    if (y < 2024 || mon < 1 || mon > 12 || d < 1 || d > 31) return false;

    int64_t days = daysFromCivil(y, (unsigned)mon, (unsigned)d);
    int64_t sec64 = days * 86400LL + (int64_t)hh * 3600LL + (int64_t)mm * 60LL + (int64_t)ss;
    if (sec64 < 0) return false;

    struct timeval tv;
    tv.tv_sec = (time_t)sec64;
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
}

// ── Draw: battery widget ──────────────────────────────────────
// Drawn in status bar over the node pane column (x=NODE_X..LCD_W-1, y=0..STATUS_H-1)
static void drawBattery() {
    const uint16_t bg  = COL_STATUS_BG;
    uint16_t col = _battPct >= 60 ? COL_BATT_GOOD :
                   _battPct >= 25 ? COL_BATT_WARN : COL_BATT_BAD;
    bool gpsEnabled = gpsIsEnabled();
    bool gpsStream = gpsHasNmeaStream();
    bool gpsFix = gpsHasFix();
    uint8_t sats = gpsSats();
    uint16_t gpsCol = gpsFix ? gpsFixColorForUiMode()
                    : (gpsStream ? COL_TAB_UNREAD
                                 : (gpsEnabled ? COL_BATT_BAD : COL_TAB_IDLE));
    wifi_mode_t wifiMode = WiFi.getMode();
    bool wifiApMode = (wifiMode == WIFI_AP);
#ifdef WIFI_AP_STA
    wifiApMode = wifiApMode || (wifiMode == WIFI_AP_STA);
#endif
    bool wifiConnected = (!wifiApMode && WiFi.status() == WL_CONNECTED);
    uint16_t wifiCol = wifiConnected ? COL_BATT_GOOD :
                      (wifiApMode ? COL_BATT_WARN : COL_BATT_BAD);
    bool lightUi = (gCfg.uiMode == UI_MODE_LIGHT);
    uint16_t iconStroke = lightUi ? COL_TEXT_MAIN : COL_DIVIDER_HI;
    if (lightUi) {
        // Darken accent colours slightly against light status backgrounds.
        if (!gpsFix) gpsCol = lerp565(gpsCol, COL_TEXT_MAIN, 72);
        wifiCol = lerp565(wifiCol, COL_TEXT_MAIN, 72);
        col     = lerp565(col,     COL_TEXT_MAIN, 72);
    }

    const int BAR_H = 14;
    const int NUB_W = 3, NUB_H = 6;
    const int ICON_GAP_WIDE = 6;
    const int ICON_GAP_TIGHT = 4;
    const int GPS_DOT_R = 5;
    const int WIFI_W = 15;
    const int WIFI_H = 12;
    const int AP_PAD_X = 4;
    const int AP_H = 12;

    lcd.setFont(UI_BODY_FONT);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    const int byText = max(0, (STATUS_H - lcd.fontHeight()) / 2);
    const int byBatt = max(1, (STATUS_H - BAR_H) / 2);

    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%u%%", (unsigned)_battPct);
    int battTxtW = lcd.textWidth(tbuf);
    int barBodyW = max(30, battTxtW + 4);
    int battW = barBodyW + NUB_W;

    char sbuf[4];
    if (sats > 0) {
        snprintf(sbuf, sizeof(sbuf), "%u", (unsigned)sats);
    } else if (gpsStream && !gpsFix) {
        // Streaming but not fixed yet: show active search marker.
        strncpy(sbuf, "~", sizeof(sbuf) - 1);
        sbuf[sizeof(sbuf) - 1] = '\0';
    } else {
        sbuf[0] = '\0';
    }
    int satW = lcd.textWidth(sbuf);
    bool showSats = (sbuf[0] != '\0');
    int gpsW = showSats ? (GPS_DOT_R * 2 + 2 + satW) : (GPS_DOT_R * 2 + 1);
    bool wifiShowApText = wifiApMode;
    int wifiW = wifiShowApText ? (lcd.textWidth("AP") + AP_PAD_X * 2) : WIFI_W;
    int gap = ICON_GAP_WIDE;

    auto calcTotal = [&]() {
        return gpsW + gap + wifiW + gap + battW;
    };

    int total = calcTotal();
    if (total > NODE_W) {
        // Keep a one-char GPS marker when possible on narrow panes.
        bool oneCharGps = showSats && sbuf[0] != '\0' && sbuf[1] == '\0';
        if (oneCharGps) {
            gap = ICON_GAP_TIGHT;
            total = calcTotal();
        }
        if (total > NODE_W) {
            showSats = false;
            gpsW = GPS_DOT_R * 2 + 1;
            total = calcTotal();
        }
    }
    if (total > NODE_W) {
        gap = ICON_GAP_TIGHT;
        total = calcTotal();
    }

    int GX = NODE_X + (NODE_W - total) / 2;
    int WX = GX + gpsW + gap;
    int BX = WX + wifiW + gap;
    int NX = BX + barBodyW;
    int NY = byBatt + (BAR_H - NUB_H) / 2;

    int dotX = GX + GPS_DOT_R;
    int dotY = STATUS_H / 2;
    lcd.fillCircle(dotX, dotY, GPS_DOT_R, gpsCol);
    lcd.drawCircle(dotX, dotY, GPS_DOT_R, iconStroke);
    lcd.drawPixel(dotX, dotY, iconStroke);
    if (showSats) {
        lcd.setTextColor(gpsCol);
        int satY = max(0, (STATUS_H - CHAR_H) / 2);
        lcd.drawString(sbuf, GX + GPS_DOT_R * 2 + 2, satY);
    }

    if (wifiShowApText) {
        int apY = max(1, (STATUS_H - AP_H) / 2);
        uint16_t apFill = lightUi ? lerp565(wifiCol, bg, 40) : wifiCol;
        drawSquirclePill(WX, apY, wifiW, AP_H, apFill, iconStroke, false);
        lcd.setTextColor(lightUi ? COL_TEXT_MAIN : COL_TEXT_ON_ACCENT, apFill);
        int apTx = WX + AP_PAD_X;
        int apTy = apY + max(0, (AP_H - CHAR_H) / 2);
        lcd.drawString("AP", apTx, apTy);
    } else {
        auto drawUpperArc = [&](int cx, int cy, int r, uint16_t col) {
            int px = 0;
            int py = r;
            int d = 1 - r;
            while (px <= py) {
                lcd.drawPixel(cx + px, cy - py, col);
                lcd.drawPixel(cx - px, cy - py, col);
                lcd.drawPixel(cx + py, cy - px, col);
                lcd.drawPixel(cx - py, cy - px, col);
                if (d < 0) d += 2 * px + 3;
                else {
                    d += 2 * (px - py) + 5;
                    py--;
                }
                px++;
            }
        };

        auto drawWifiGlyph = [&](int x, int y, uint16_t c, uint16_t outline, bool disconnected) {
            int cx = x + WIFI_W / 2;
            int cy = y + WIFI_H - 2;

            drawUpperArc(cx, cy, 6, outline);
            drawUpperArc(cx, cy, 4, outline);
            drawUpperArc(cx, cy, 2, outline);
            drawUpperArc(cx, cy, 5, c);
            drawUpperArc(cx, cy, 3, c);
            drawUpperArc(cx, cy, 1, c);

            lcd.fillCircle(cx, cy, 1, c);
            lcd.drawPixel(cx, cy + 1, outline);

            if (disconnected) {
                lcd.drawLine(x + 1, y + WIFI_H - 1, x + WIFI_W - 2, y + 1, outline);
                lcd.drawLine(x + 1, y + WIFI_H - 2, x + WIFI_W - 2, y + 1, c);
            }
        };
        int wifiY = max(0, (STATUS_H - WIFI_H) / 2);
        drawWifiGlyph(WX, wifiY, wifiCol, iconStroke, !wifiConnected);
    }

    lcd.drawRect(BX, byBatt, barBodyW, BAR_H, col);
    lcd.fillRect(NX, NY, NUB_W, NUB_H, col);

    int fillW = (barBodyW - 2) * _battPct / 100;
    lcd.fillRect(BX + 1, byBatt + 1, barBodyW - 2, BAR_H - 2, bg);
    if (fillW > 0) lcd.fillRect(BX + 1, byBatt + 1, fillW, BAR_H - 2, col);

    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(lightUi ? COL_TEXT_MAIN : COL_TEXT_ON_ACCENT);
    int battTx = BX + (barBodyW - battTxtW) / 2;
    int battTy = byBatt + max(0, (BAR_H - CHAR_H) / 2);
    lcd.drawString(tbuf, battTx, battTy);
}

// ── Draw: status bar ─────────────────────────────────────────
static void drawStatus() {
    const DisplayHeaderProfile &header = displayUiProfile().header;

    lcd.fillRect(0, 0, LCD_W, STATUS_H, COL_STATUS_BG);
    lcd.drawFastHLine(0, STATUS_H - 1, LCD_W, COL_DIVIDER);

    if (header.useCompactStatusFont) lcd.setFont(&fonts::DejaVu9);
    else                             lcd.setFont(&fonts::Orbitron_Light_24);
    lcd.setTextSize(header.statusFontScale);
    lcd.setTextColor(COL_STATUS_TEXT, COL_STATUS_BG);

    const char *shortName = gCfg.nodeShort[0] ? gCfg.nodeShort : "----";
    char timeBuf[8];
    time_t nowEpoch = time(nullptr);
    if (nowEpoch < 1700000000) {
        snprintf(timeBuf, sizeof(timeBuf), "--:--");
    } else {
        struct tm localTm;
        localtime_r(&nowEpoch, &localTm);
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", localTm.tm_hour, localTm.tm_min);
    }

    int infoY = max(0, (STATUS_H - lcd.fontHeight()) / 2);
    int x = header.statusTextX;
    lcd.drawString(shortName, x, infoY);
    int shortW = lcd.textWidth(shortName);

    int flowerCx = x + shortW + header.statusFlowerGap;
    drawCamelliaMarkTiny(flowerCx, STATUS_H / 2);

    int timeW = lcd.textWidth(timeBuf);
    int timeX = max(0, (LCD_W - timeW) / 2);
    int timeRight = timeX + timeW;
    int statusRightLimit = NODE_X - header.statusTimeRightPad;
    if (timeRight > statusRightLimit) {
        timeX = max(0, statusRightLimit - timeW);
    }
    lcd.drawString(timeBuf, timeX, infoY);
    drawBattery();
    lcd.setFont(UI_BODY_FONT);
    dirtyStatus = false;
}

static void drawModalMaskAndFrame(int mx, int my, int mw, int mh) {
    int topH = my - CHAT_Y;
    if (topH > 0) lcd.fillRect(0, CHAT_Y, LCD_W, topH, COL_BG_MAIN);
    if (mx > 0) lcd.fillRect(0, my, mx, mh, COL_BG_MAIN);
    int rightX = mx + mw;
    if (rightX < LCD_W) lcd.fillRect(rightX, my, LCD_W - rightX, mh, COL_BG_MAIN);
    int botY = my + mh;
    int chatBottom = CHAT_Y + CHAT_H;
    if (botY < chatBottom) lcd.fillRect(0, botY, LCD_W, chatBottom - botY, COL_BG_MAIN);

    // Important: do not repaint modal interior here.
    // Callers draw content first, then this mask/frame; repainting interior would
    // blank the panel contents.
    lcd.drawRect(mx, my, mw, mh, COL_SELECT_ACCENT);
    if (mw > 2 && mh > 2)
        lcd.drawRect(mx + 1, my + 1, mw - 2, mh - 2, COL_DIVIDER_HI);
}

static void drawPanelCloseButton(int x, int y, int w, int h) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    clearPanelCloseRect();
    return;
#endif
    if (useCompactKeyboardUi()) {
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        clearPanelCloseRect();
        return;
    }
    uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
    drawSquirclePill(x, y, w, h, fill, COL_SELECT_ACCENT, false);
    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(COL_TEXT_MAIN, fill);
    int tw = lcd.textWidth("Close");
    int tx = x + max(1, (w - tw) / 2);
    int ty = y + max(0, (h - CHAR_H) / 2);
    drawClippedText(tx, ty, w - (tx - x) - 1, "Close");
    setPanelCloseRect(x, y, w, h);
}

// ── Draw: tab bar ─────────────────────────────────────────────
static void drawTabs() {
    const DisplayTabsProfile &tabsProfile = displayUiProfile().tabs;

    lcd.fillRect(0, STATUS_H, LCD_W, TAB_H, COL_BG_MAIN);
    lcd.drawFastHLine(0, STATUS_H, LCD_W, COL_DIVIDER);
    lcd.drawFastHLine(0, STATUS_H + TAB_H - 1, LCD_W, COL_DIVIDER);
#if defined(DEVICE_TLORA_PAGER_TFT)
    static constexpr float kPagerTabsScale = 1.25f;
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(1.0f);
    const int TAB_PAD_X = max(1, (int)lroundf((float)tabsProfile.tabPadX * kPagerTabsScale));
    const int TAB_GAP   = max(1, (int)lroundf((float)tabsProfile.tabGap * kPagerTabsScale));
    const int TAB_EDGE_PAD = max(1, (int)lroundf((float)tabsProfile.tabEdgePad * kPagerTabsScale));
    const int TAB_PILL_INSET_Y = max(1, (int)lroundf((float)tabsProfile.tabPillInsetY * kPagerTabsScale));
    const int TAB_PILL_MIN_H = max(1, (int)lroundf((float)tabsProfile.tabPillMinHeight * kPagerTabsScale));
    const int PILL_H    = max(TAB_PILL_MIN_H, TAB_H - TAB_PILL_INSET_Y * 2);
    const int PILL_Y    = STATUS_H + (TAB_H - PILL_H) / 2;
    const int TAB_LABEL_Y = PILL_Y + max(0, (PILL_H - lcd.fontHeight()) / 2);
#else
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    const int TAB_PAD_X = tabsProfile.tabPadX;
    const int TAB_GAP   = tabsProfile.tabGap;
    const int TAB_EDGE_PAD = tabsProfile.tabEdgePad;
    const int PILL_H    = max(tabsProfile.tabPillMinHeight, TAB_H - tabsProfile.tabPillInsetY * 2);
    const int PILL_Y    = STATUS_H + (TAB_H - PILL_H) / 2;
    const int TAB_LABEL_Y = PILL_Y + tabsProfile.tabLabelYOffset;
#endif

    // Build full tab list with absolute x positions
    struct TabEntry { int view; char label[16]; int x; int w; };
    TabEntry tabs[TOTAL_VIEWS];
    int tabCount = 0;
    int xCursor  = TAB_EDGE_PAD;

    for (int i = 0; i < TOTAL_VIEWS; i++) {
        if (!isTopTabView(i)) continue;
        if (!isViewNavigable(i)) continue;
        char label[16] = {};
        if      (i == VIEW_SETTINGS) strncpy(label, "CFG", sizeof(label));
        else if (i == VIEW_GPS)      strncpy(label, "GPS", sizeof(label));
        else if (i == CHAN_ANN)      strncpy(label, "LIVE", sizeof(label));
        else                         strncpy(label, CHANNEL_KEYS[i].name, sizeof(label) - 1);
        int w = lcd.textWidth(label) + 2 * TAB_PAD_X;
        tabs[tabCount] = { i, {}, xCursor, w };
        strncpy(tabs[tabCount].label, label, sizeof(label));
        tabCount++;
        xCursor += w + TAB_GAP;
    }

    int totalTabW = max(0, xCursor - TAB_GAP + TAB_EDGE_PAD);
    int maxScroll = max(0, totalTabW - LCD_W);

    // Auto-scroll so the active tab is always fully visible
    for (int t = 0; t < tabCount; t++) {
        if (tabs[t].view != activeView) continue;
        if (tabs[t].x < tabScrollX)
            tabScrollX = tabs[t].x;
        else if (tabs[t].x + tabs[t].w > tabScrollX + LCD_W - TAB_EDGE_PAD)
            tabScrollX = tabs[t].x + tabs[t].w - (LCD_W - TAB_EDGE_PAD);
        break;
    }
    tabScrollX = constrain(tabScrollX, 0, maxScroll);

    // Render only tabs that intersect the visible window
    for (int t = 0; t < tabCount; t++) {
        int sx = tabs[t].x - tabScrollX;
        if (sx + tabs[t].w <= 0) continue;
        if (sx >= LCD_W)         break;

        bool isActive = (tabs[t].view == activeView);
        uint16_t stateCol;
        if (tabs[t].view == VIEW_SETTINGS) {
            stateCol = isActive ? COL_TEXT_ON_ACCENT : COL_TAB_IDLE;
        } else if (tabs[t].view == VIEW_GPS) {
            if      (isActive)       stateCol = COL_TEXT_ON_ACCENT;
            else if (gpsHasFix())    stateCol = gpsFixColorForUiMode();
            else if (gpsIsEnabled() && !gpsHasNmeaStream()) stateCol = COL_BATT_BAD;
            else if (gpsIsEnabled()) stateCol = COL_TAB_UNREAD;
            else                     stateCol = COL_TAB_IDLE;
        } else if (tabs[t].view == CHAN_ANN) {
            Channel &ch = Channels.get(tabs[t].view);
            stateCol = (ch.unread || isActive) ? COL_TEAL : COL_TAB_IDLE;
        } else {
            Channel &ch = Channels.get(tabs[t].view);
            stateCol = ch.unread ? COL_TAB_UNREAD :
                       isActive  ? COL_TAB_ACTIVE  : COL_TAB_IDLE;
        }

        uint16_t fillCol   = isActive ? COL_SELECT_BG : lerp565(COL_BG_MAIN, COL_PANEL_ALT, 96);
        uint16_t outlineCol= isActive ? COL_SELECT_ACCENT : stateCol;
        uint16_t textCol   = isActive ? COL_TEXT_ON_ACCENT : stateCol;

        drawSquirclePill(sx, PILL_Y, tabs[t].w, PILL_H, fillCol, outlineCol, isActive);
        lcd.setTextColor(textCol, fillCol);
        drawClippedText(sx + TAB_PAD_X, TAB_LABEL_Y,
                        tabs[t].w - 2 * TAB_PAD_X, tabs[t].label);
    }
    lcd.setFont(UI_BODY_FONT);
    dirtyTabs = false;
}

// ── Draw: vertical divider ────────────────────────────────────
static void drawDivider() {
    lcd.fillRect(DIVIDER_X, CHAT_Y, 1, CHAT_H, COL_DIVIDER);
    lcd.drawFastVLine(DIVIDER_X + 1, CHAT_Y, CHAT_H, COL_DIVIDER_HI);
}

// ── Draw: message area ────────────────────────────────────────
static void drawChat() {
    clearPanelCloseRect();
    int chatX = 0;
    int chatW = MSG_W;
    const int chatInnerY = CHAT_Y + 1;
    uint16_t chatEdge = COL_DIVIDER;
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (pagerWheelChatScrollMode && activeView >= 0 && activeView < MESH_CHANNELS) {
        chatEdge = COL_SELECT_ACCENT;
    }
#endif
    drawPanelFrame(chatX, CHAT_Y, chatW, CHAT_H, COL_PANEL_BG, chatEdge);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(1.0f);
#else
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(CHAT_WINDOW_TEXT_SCALE);
#endif

    int active = Channels.activeIdx();

    auto txStatusSymbol = [](const DisplayLine *dl) -> const char * {
        if (!dl || !dl->packetId) return nullptr;
        if (dl->text[0] == ' ' && dl->text[1] == ' ') return nullptr;
        switch (dl->ack) {
            case DisplayLine::ACKED:
            case DisplayLine::ACKED_RELAY:
                return "o";
            case DisplayLine::TX_FAILED:
                return "-";
            case DisplayLine::PENDING:
            case DisplayLine::NAKED:
            case DisplayLine::NONE:
            default:
                return "...";
        }
    };

    for (int row = 0; row < VISIBLE_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(active, row);
        int y = chatInnerY + row * LINE_H;
        uint16_t rowBg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
        lcd.fillRect(chatX + 1, y, chatW - 2, LINE_H, rowBg);
        if (!dl) continue;

        uint16_t col = dl->color;
        bool ackColorApplied = false;

        // Override color for sent messages based on ACK state
        if (dl->packetId) {
            switch (dl->ack) {
                case DisplayLine::ACKED:
                    col = (gCfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                    ackColorApplied = true;
                    break;
                case DisplayLine::ACKED_RELAY: col = TFT_YELLOW; ackColorApplied = true; break;
                case DisplayLine::NAKED:       col = TFT_RED;    ackColorApplied = true; break;
                case DisplayLine::TX_FAILED:   col = TFT_RED;    ackColorApplied = true; break;
                default: break;  // NONE / PENDING keep original color
            }
        }

        // Improve readability by forcing neutral body text by theme.
        // For restored history, keep explicit non-pending ACK/NAK colors.
        if (!ackColorApplied && dl->packetId && dl->ack != DisplayLine::NONE && dl->ack != DisplayLine::PENDING) {
            ackColorApplied = true;
        }
        if (!ackColorApplied) {
            if (gCfg.uiMode == UI_MODE_LIGHT) col = TFT_BLACK;
            else                              col = TFT_WHITE;
        }

        lcd.setTextColor(col, rowBg);
        const char *sym = txStatusSymbol(dl);
        if (sym) {
            char rendered[MSG_CHARS + 8];
            snprintf(rendered, sizeof(rendered), "%-3s %s", sym, dl->text);
            drawClippedText(chatX + 2, y + 1, chatW - 6, rendered);
        } else {
            drawClippedText(chatX + 2, y + 1, chatW - 6, dl->text);
        }
    }

    // Scroll indicator: show when newer lines exist above the visible window.
    Channel &ch = Channels.get(active);
    if (ch.scrollOff > 0) {
        uint16_t moreBg = COL_PANEL_ALT;
        lcd.setTextColor(COL_TEAL, moreBg);
        drawClippedText(chatX + chatW - 34, chatInnerY + 1, 32, "more");
    }

    lcd.setFont(UI_BODY_FONT);
    dirtyChat = false;
}

static int dmConvMessageRowsVisible() {
    const int my = CHAT_Y + 4;
    const int mh = panelOverlayBottomY() - my + 1;
    const int iy = my + 3;
    const bool reserveFooter = showPanelCloseButtons() || showPanelScrollButtons();
    const int controlsTop = reserveFooter
        ? (my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD))
        : (my + mh - 1);
    const int rowsVisible = max(1, (controlsTop - iy - 1) / DM_LINE_H);
    // Header row + spacer row before message lines.
    return max(1, rowsVisible - 2);
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

static uint16_t liveLineTrafficColor(const DisplayLine &dl) {
    const char *body = "";
    liveTimestampAndBody(dl.text, nullptr, 0, &body);
    if (!body[0]) return (dl.color == TFT_DARKGREY) ? TFT_WHITE : dl.color;

    if (strstr(body, " ER")) return TFT_RED;
    if (strncmp(body, "T ACK", 5) == 0) return TFT_GREEN;
    if (strncmp(body, "T DM", 4) == 0) return TFT_MAGENTA;
    if (strncmp(body, "R ", 2) == 0 && strstr(body, " ENC ")) return TFT_ORANGE;
    if (strncmp(body, "R ", 2) == 0) return TFT_CYAN;
    if (strncmp(body, "T ", 2) == 0) return TFT_YELLOW;

    return (dl.color == TFT_DARKGREY) ? TFT_WHITE : dl.color;
}

static void drawLivePanel(bool fullRedraw) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    const int mx = 0;
    const int my = CHAT_Y;
    const int mw = LCD_W;
#else
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
#endif
    const int mh = panelOverlayBottomY() - my + 1;
    const int titleH = 11;
    const int left = mx + 1;
    const int innerW = mw - 2;
    const int msgTop = my + titleH + 2;
    const bool reserveFooter = showPanelCloseButtons() || showPanelScrollButtons();
    const int controlsTop = reserveFooter
        ? (my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD))
        : (my + mh - 1);
    // Font0 is 7px tall; use 8px rows for a touch more breathing room.
    const int rowH = 8;
    const int rowsVisible = max(1, (controlsTop - msgTop) / rowH);

    Channel &ch = Channels.get(CHAN_ANN);
    int total = ch.count;
    int stored = min(total, MAX_MSG_LINES);
    int oldest = total - stored;
    int newest = total - 1 - ch.scrollOff;

    if (fullRedraw) {
        clearPanelCloseRect();
        drawModalMaskAndFrame(mx, my, mw, mh);
        drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
        lcd.fillRect(mx + 1, my + 1, mw - 2, titleH, COL_SELECT_BG);
        lcd.setFont(UI_BODY_FONT);
        lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
        drawClippedText(mx + 5, my + 2, mw - 10, "Live RX/TX");
        lcd.fillRect(mx + 1, controlsTop, mw - 2, my + mh - controlsTop - 1, COL_PANEL_BG);
        const int btnY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
        const int closeX = mx + 3;
        drawPanelCloseButton(closeX, btnY, TOUCH_BTN_W, TOUCH_BTN_H);
    }

    lcd.setFont(UI_BODY_FONT);
    for (int row = 0; row < rowsVisible; row++) {
        int y = msgTop + row * rowH;
        uint16_t rowBg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
        lcd.fillRect(left, y, innerW, rowH, rowBg);

        int lineIdx = newest - row;
        if (lineIdx < oldest || lineIdx >= total) continue;
        const DisplayLine &dl = ch.lines[lineIdx % MAX_MSG_LINES];

        uint16_t col = liveLineTrafficColor(dl);
        if (dl.packetId) {
            switch (dl.ack) {
                case DisplayLine::ACKED:
                    col = (gCfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                    break;
                case DisplayLine::ACKED_RELAY: col = TFT_YELLOW; break;
                case DisplayLine::NAKED:       col = TFT_RED;    break;
                case DisplayLine::TX_FAILED:   col = TFT_RED;    break;
                default: break;
            }
        }
        lcd.setTextColor(col, rowBg);
        char rendered[96];
        formatLiveLineText(dl, rendered, sizeof(rendered));
        drawClippedText(left + 2, y, innerW - 4, rendered);
    }

    // Clear any partial row gap before footer controls to avoid stale pixels.
    int rowsBottom = msgTop + rowsVisible * rowH;
    if (rowsBottom < controlsTop) {
        lcd.fillRect(left, rowsBottom, innerW, controlsTop - rowsBottom, COL_PANEL_BG);
    }

    if (ch.scrollOff > 0) {
        lcd.setTextColor(COL_TEAL, COL_PANEL_ALT);
        drawClippedText(left + innerW - 30, msgTop + 1, 28, "more");
    }

    lcd.setFont(UI_BODY_FONT);
    dirtyLiveRows = false;
    if (fullRedraw) dirtyChat = false;
}

static int32_t decodeZigZag32(uint32_t v) {
    return (int32_t)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
}

static bool mapCoordInRange(float lat, float lon) {
    return !(lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f);
}

static bool mapExtractNodeCoords(const NodeEntry *n, float &lat, float &lon) {
    if (!n) return false;
    bool hasCoords = (n->latI != 0 || n->lonI != 0);
    if (!n->hasPosition && !hasCoords) return false;

    lat = n->latI * 1e-7f;
    lon = n->lonI * 1e-7f;
    if (mapCoordInRange(lat, lon)) return true;

    // Backward compatibility for older packets stored with non-zigzag decode.
    int32_t latRecovered = decodeZigZag32((uint32_t)n->latI);
    int32_t lonRecovered = decodeZigZag32((uint32_t)n->lonI);
    float latRec = latRecovered * 1e-7f;
    float lonRec = lonRecovered * 1e-7f;
    if (!mapCoordInRange(latRec, lonRec)) return false;

    lat = latRec;
    lon = lonRec;
    return true;
}

static bool mapIsApMode() {
    wifi_mode_t mode = WiFi.getMode();
    bool ap = (mode == WIFI_AP);
#ifdef WIFI_AP_STA
    ap = ap || (mode == WIFI_AP_STA);
#endif
    return ap;
}

static bool mapCanDownloadTiles() {
    if (mapIsApMode()) return false;
    return WiFi.status() == WL_CONNECTED;
}

static bool mapEnsureDir(const char *path) {
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

static String mapTilePath(uint8_t z, int x, int y) {
    String p = "/camillia/tiles/";
    p += String((unsigned)z);
    p += "/";
    p += String(x);
    p += "/";
    p += String(y);
    p += ".png";
    return p;
}

static bool mapEnsureTileDirs(uint8_t z, int x) {
    if (!mapEnsureDir("/camillia")) return false;
    if (!mapEnsureDir("/camillia/tiles")) return false;

    String zDir = String("/camillia/tiles/") + String((unsigned)z);
    if (!mapEnsureDir(zDir.c_str())) return false;

    String xDir = zDir + "/" + String(x);
    if (!mapEnsureDir(xDir.c_str())) return false;
    return true;
}

static bool mapDownloadTile(uint8_t z, int x, int y, const char *path) {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (!mapEnsureTileDirs(z, x)) return false;

    String url = "https://tile.openstreetmap.org/";
    url += String((unsigned)z);
    url += "/";
    url += String(x);
    url += "/";
    url += String(y);
    url += ".png";

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);

    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("User-Agent", "camillia-mt/1.0");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        http.end();
        return false;
    }

    int written = http.writeToStream(&f);
    f.close();
    http.end();

    bool ok = (written > 0);
    if (!ok) SD.remove(path);
    return ok;
}

static bool mapEnsureTileFile(uint8_t z, int x, int y, bool allowDownload,
                              String &path, bool &downloaded) {
    downloaded = false;
    path = mapTilePath(z, x, y);
    if (SD.exists(path.c_str())) return true;
    if (!allowDownload) return false;

    if (!mapDownloadTile(z, x, y, path.c_str())) return false;
    downloaded = true;
    return true;
}

static void drawMapPanel() {
    clearPanelCloseRect();
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    const int mx = 0;
    const int my = CHAT_Y;
    const int mw = LCD_W;
    const int mh = panelOverlayBottomY() - my + 1;
    const int mapNavBtnH = 0;
    const int mapNavBottomPad = 0;
    const int mapNavGap = 0;
#else
    const int mx = 0;
    const int my = CHAT_Y;
    const int mw = LCD_W;
    const int mh = panelOverlayBottomY() - my + 1;
    const int mapNavBtnH = 22;
    const int mapNavBottomPad = 2;
    const int mapNavGap = 3;
#endif
    const int mapLegendReserveH =
    #if defined(DEVICE_TDECK)
        (CHAR_H + 2);
#else
        0;
#endif
    const int titleH = 11;
    const int ix = mx + 3;
    const int iw = mw - 6;
    const int controlsBottom = my + mh - mapLegendReserveH - mapNavBottomPad;
    const int controlsTop = controlsBottom - mapNavBtnH;
    const int mapY = my + titleH + 2;
    const int mapH = max(64, controlsTop - mapY - 2);
    const int colGap = 4;
    // Node list only shows short names; size for ~5 characters to favor map area.
    const int listChars = 5;
    const int listW = min(58, max(44, listChars * CHAR_W + 12));
    const int mapX = ix;
    const int listX = ix + iw - listW;
    const int mapW = max(90, listX - mapX - colGap);
    const int listY = mapY;
    const int listHeaderH = 10;
    const int rowH = 9;
    const int rowsVisible = max(1, (mapH - listHeaderH - 1) / rowH);
    const int totalNodes = mapVisibleNodeCount();
    mapsListSel = constrain(mapsListSel, 0, max(0, totalNodes - 1));

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    int positionedCount = 0;
    float minLat = 90.0f, maxLat = -90.0f;
    float minLon = 180.0f, maxLon = -180.0f;
    for (int i = 0; i < totalNodes; i++) {
        NodeEntry *n = mapVisibleNodeByIndex(i);
        float lat = 0.0f, lon = 0.0f;
        if (!mapExtractNodeCoords(n, lat, lon)) continue;
        positionedCount++;
        if (lat < minLat) minLat = lat;
        if (lat > maxLat) maxLat = lat;
        if (lon < minLon) minLon = lon;
        if (lon > maxLon) maxLon = lon;
    }

    float autoMinLat = -90.0f, autoMaxLat = 90.0f;
    float autoMinLon = -180.0f, autoMaxLon = 180.0f;
    if (positionedCount >= 2) {
        float latSpan = maxLat - minLat;
        float lonSpan = maxLon - minLon;
        float latPad = max(0.5f, latSpan * 0.15f);
        float lonPad = max(0.5f, lonSpan * 0.15f);
        autoMinLat = max(-90.0f, minLat - latPad);
        autoMaxLat = min( 90.0f, maxLat + latPad);
        autoMinLon = max(-180.0f, minLon - lonPad);
        autoMaxLon = min( 180.0f, maxLon + lonPad);
    } else if (positionedCount == 1) {
        float cLat = 0.5f * (minLat + maxLat);
        float cLon = 0.5f * (minLon + maxLon);
        autoMinLat = max(-90.0f, cLat - 12.0f);
        autoMaxLat = min( 90.0f, cLat + 12.0f);
        autoMinLon = max(-180.0f, cLon - 18.0f);
        autoMaxLon = min( 180.0f, cLon + 18.0f);
    }

    float autoCenterLat = 0.5f * (autoMinLat + autoMaxLat);
    float autoCenterLon = 0.5f * (autoMinLon + autoMaxLon);
    float autoLatSpan = max(0.001f, autoMaxLat - autoMinLat);
    float autoLonSpan = max(0.001f, autoMaxLon - autoMinLon);

    if (!mapViewManual) {
        mapViewCenterLat = autoCenterLat;
        mapViewCenterLon = autoCenterLon;
        mapViewLatSpan = autoLatSpan;
        mapViewLonSpan = autoLonSpan;
    }
    mapClampViewport();

    mapLastCenterLat = mapViewCenterLat;
    mapLastCenterLon = mapViewCenterLon;
    mapLastLatSpan = mapViewLatSpan;
    mapLastLonSpan = mapViewLonSpan;

    float viewMinLat = mapViewCenterLat - (mapViewLatSpan * 0.5f);
    float viewMaxLat = mapViewCenterLat + (mapViewLatSpan * 0.5f);
    float viewMinLon = mapViewCenterLon - (mapViewLonSpan * 0.5f);
    float viewMaxLon = mapViewCenterLon + (mapViewLonSpan * 0.5f);

    char hdr[44];
    snprintf(hdr, sizeof(hdr), "Node Map %d/%d (%s)",
             positionedCount, totalNodes, mapViewManual ? "manual" : "fit");
    lcd.fillRect(mx + 1, my + 1, mw - 2, titleH, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(mx + 5, my + 2, mw - 10, hdr);

    lcd.fillRect(mapX + 1, mapY + 1, mapW - 2, mapH - 2, COL_PANEL_STRONG);
    drawPanelFrame(mapX, mapY, mapW, mapH, COL_PANEL_STRONG, COL_DIVIDER);
    lcd.fillRect(listX + 1, listY + 1, listW - 2, mapH - 2, COL_PANEL_BG);
    drawPanelFrame(listX, listY, listW, mapH, COL_PANEL_BG, COL_DIVIDER);
    lcd.fillRect(listX + 1, listY + 1, listW - 2, listHeaderH, COL_PANEL_ALT);
    lcd.setTextColor(COL_TEXT_DIM, COL_PANEL_ALT);
    drawClippedText(listX + 3, listY + 2, listW - 6, "Nodes");

    float latRange = viewMaxLat - viewMinLat;
    float lonRange = viewMaxLon - viewMinLon;
    if (latRange < 0.001f) latRange = 0.001f;
    if (lonRange < 0.001f) lonRange = 0.001f;

    auto lonToX = [&](float lon) -> int {
        float t = (lon - viewMinLon) / lonRange;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return mapX + 2 + (int)(t * (float)(mapW - 5));
    };
    auto latToY = [&](float lat) -> int {
        float t = (viewMaxLat - lat) / latRange;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return mapY + 2 + (int)(t * (float)(mapH - 5));
    };

    bool apMode = mapIsApMode();
    bool allowDownloads = mapCanDownloadTiles();
    // Cached tiles remain useful even when the device is in AP mode.
    // Only network downloads should depend on Wi-Fi connectivity.
    bool useTileBackdrop = HAS_SD_CARD;
    bool downloadedAnyTile = false;

    if (useTileBackdrop) {
        double lonSpanForZoom = max((double)MAP_MIN_LON_SPAN, (double)mapViewLonSpan);
        double zoomRaw = log2(((double)mapW * 360.0) / (256.0 * lonSpanForZoom));
        int z = constrain((int)floor(zoomRaw), 0, MAP_MAX_TILE_ZOOM);
        int tileCount = 1 << z;

        auto lonToWorldX = [&](double lonDeg) -> double {
            return ((lonDeg + 180.0) / 360.0) * (256.0 * tileCount);
        };
        auto latToWorldY = [&](double latDeg) -> double {
            double clamped = max(-85.05112878, min(85.05112878, latDeg));
            double rad = clamped * M_PI / 180.0;
            double merc = log(tan(rad) + 1.0 / cos(rad));
            return (1.0 - merc / M_PI) * 0.5 * (256.0 * tileCount);
        };

        double centerWX = lonToWorldX(mapViewCenterLon);
        double centerWY = latToWorldY(mapViewCenterLat);
        double leftWX = centerWX - (double)mapW * 0.5;
        double topWY = centerWY - (double)mapH * 0.5;

        int tx0 = (int)floor(leftWX / 256.0);
        int ty0 = (int)floor(topWY / 256.0);
        int tx1 = (int)floor((leftWX + mapW - 1) / 256.0);
        int ty1 = (int)floor((topWY + mapH - 1) / 256.0);

        int fetchBudget = allowDownloads ? 2 : 0;

        lcd.setClipRect(mapX + 1, mapY + 1, mapW - 2, mapH - 2);
        lcd.fillRect(mapX + 1, mapY + 1, mapW - 2, mapH - 2, COL_PANEL_STRONG);

        for (int ty = ty0; ty <= ty1; ty++) {
            if (ty < 0 || ty >= tileCount) continue;
            for (int tx = tx0; tx <= tx1; tx++) {
                int wrappedX = tx % tileCount;
                if (wrappedX < 0) wrappedX += tileCount;

                int drawX = mapX + (int)(tx * 256.0 - leftWX);
                int drawY = mapY + (int)(ty * 256.0 - topWY);

                String tilePath;
                bool downloaded = false;
                bool hasTile = mapEnsureTileFile((uint8_t)z, wrappedX, ty,
                                                fetchBudget > 0, tilePath, downloaded);
                if (downloaded && fetchBudget > 0) {
                    fetchBudget--;
                    downloadedAnyTile = true;
                }

                if (hasTile) {
                    lcd.drawPngFile(SD, tilePath.c_str(), drawX, drawY);
                } else {
                    uint16_t fb = (((wrappedX + ty) & 1) ? COL_PANEL_STRONG : COL_PANEL_ALT);
                    lcd.fillRect(drawX, drawY, 256, 256, fb);
                    lcd.drawRect(drawX, drawY, 256, 256, COL_DIVIDER);
                }
            }
        }

        lcd.clearClipRect();
    } else {
        // AP mode fallback: keep the existing lightweight map rendering.
        int gridSteps = 4;
        if (mapViewLatSpan < 40.0f || mapViewLonSpan < 80.0f) gridSteps = 6;
        if (mapViewLatSpan < 10.0f || mapViewLonSpan < 20.0f) gridSteps = 8;
        if (mapViewLatSpan < 2.0f || mapViewLonSpan < 4.0f) gridSteps = 10;
        for (int g = 1; g < gridSteps; g++) {
            int gx = mapX + (g * (mapW - 1)) / gridSteps;
            int gy = mapY + (g * (mapH - 1)) / gridSteps;
            lcd.drawFastVLine(gx, mapY + 1, mapH - 2, COL_DIVIDER_HI);
            lcd.drawFastHLine(mapX + 1, gy, mapW - 2, COL_DIVIDER_HI);
        }
    }

    uint32_t selectedNodeId = 0;
    if (totalNodes > 0) {
        NodeEntry *sel = mapVisibleNodeByIndex(mapsListSel);
        if (sel) selectedNodeId = sel->nodeId;
    }

    uint32_t nowMs = millis();
    for (int i = 0; i < totalNodes; i++) {
        NodeEntry *n = mapVisibleNodeByIndex(i);
        float lat = 0.0f, lon = 0.0f;
        if (!mapExtractNodeCoords(n, lat, lon)) continue;

        int px = lonToX(lon);
        int py = latToY(lat);
        uint16_t col = COL_TAB_IDLE;
        if (n->lastHeardMs != 0 && nowMs >= n->lastHeardMs) {
            uint32_t age = nowMs - n->lastHeardMs;
            if (age < 60000UL) col = COL_NODE_HOT;
            else if (age < 3600000UL) col = COL_NODE_WARM;
        }
        int radius = (n->nodeId == selectedNodeId) ? 3 : 2;
        lcd.fillCircle(px, py, radius, col);
        if (n->nodeId == selectedNodeId) {
            lcd.drawCircle(px, py, radius + 2, COL_SELECT_ACCENT);
        }
    }
    if (positionedCount == 0) {
        lcd.setFont(UI_BODY_FONT);
        lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_STRONG);
        drawClippedText(mapX + 6, mapY + mapH / 2 - 4, mapW - 12, "No positioned nodes yet");
        lcd.setFont(&fonts::DejaVu9);
    }

    const int listRowsTop = listY + listHeaderH + 1;
    if (totalNodes == 0) {
        lcd.fillRect(listX + 1, listRowsTop, listW - 2, rowH, COL_PANEL_BG);
        lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_BG);
        drawClippedText(listX + 2, listRowsTop + 1, listW - 4, "None");
    } else {
        int firstVisible = max(0, mapsListSel - (rowsVisible - 1));
        int maxFirst = max(0, totalNodes - rowsVisible);
        if (firstVisible > maxFirst) firstVisible = maxFirst;

        for (int row = 0; row < rowsVisible; row++) {
            int idx = firstVisible + row;
            int y = listRowsTop + row * rowH;
            uint16_t rowBg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
            lcd.fillRect(listX + 1, y, listW - 2, rowH, rowBg);
            if (idx >= totalNodes) continue;

            NodeEntry *n = mapVisibleNodeByIndex(idx);
            if (!n) continue;

            bool sel = (idx == mapsListSel);
            uint16_t bg = sel ? COL_SELECT_BG : rowBg;
            if (sel) lcd.fillRect(listX + 1, y, listW - 2, rowH, bg);

            bool hasLocation = false;
            float lat = 0.0f, lon = 0.0f;
            hasLocation = mapExtractNodeCoords(n, lat, lon);
            const char *sn = n->shortName[0] ? n->shortName : "----";
            uint16_t fg = sel ? COL_TEXT_ON_ACCENT : (hasLocation ? COL_TEXT_MAIN : COL_TAB_IDLE);
            lcd.setTextColor(fg, bg);
            drawClippedText(listX + 3, y + 1, listW - 6, sn);
        }
    }

#if !defined(DEVICE_CARDPUTER_LORA_HAT) && !defined(DEVICE_TLORA_PAGER_TFT)
    const int btnY = controlsTop;
    const int closeW = 46;
    drawPanelCloseButton(mx + 3, btnY, closeW, mapNavBtnH);

    const int ctlCount = MAP_CTL_COUNT;
    const int minCtlX = mx + 3 + closeW + 4;
    int btnW[MAP_CTL_COUNT] = { 30, 30, 80, 60, 30 };
    const char *labels[MAP_CTL_COUNT] = { "+", "-", "Previous Node", "Next Node", "ME" };

    const int plusIdx = (int)MAP_CTL_ZOOM_IN;
    const int minusIdx = (int)MAP_CTL_ZOOM_OUT;
    const int prevIdx = (int)MAP_CTL_LIST_PREV;
    const int nextIdx = (int)MAP_CTL_LIST_NEXT;
    const int meIdx = (int)MAP_CTL_ME;
    int btnX[MAP_CTL_COUNT] = {0};
    int maxCtlEnd = mx + mw - 4;

    // Pin ME to the far right.
    int meX = maxCtlEnd - btnW[meIdx];
    btnX[meIdx] = meX;

    // Place zoom controls directly to the left of ME and swap their order:
    // [ ... Previous / Next ... ]  -  +  ME
    btnX[plusIdx] = meX - mapNavGap - btnW[plusIdx];
    btnX[minusIdx] = btnX[plusIdx] - mapNavGap - btnW[minusIdx];

    // Center Previous/Next in the remaining space to the left of zoom controls.
    int leftControlsW = btnW[prevIdx] + mapNavGap + btnW[nextIdx];
    int leftStartX = minCtlX;
    int leftAvailW = (btnX[minusIdx] - mapNavGap) - minCtlX;
    if (leftAvailW > leftControlsW) leftStartX += (leftAvailW - leftControlsW) / 2;
    btnX[prevIdx] = leftStartX;
    btnX[nextIdx] = leftStartX + btnW[prevIdx] + mapNavGap;

    for (int i = 0; i < ctlCount; i++) {
        int bx = btnX[i];
        uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
        drawSquirclePill(bx, btnY, btnW[i], mapNavBtnH, fill, COL_SELECT_ACCENT, false);
        lcd.setFont(UI_BODY_FONT);
        lcd.setTextColor(COL_TEXT_MAIN, fill);
        int tw = lcd.textWidth(labels[i]);
        int tx = bx + max(1, (btnW[i] - tw) / 2);
        int ty = btnY + max(0, (mapNavBtnH - CHAR_H) / 2);
        drawClippedText(tx, ty, btnW[i] - (tx - bx) - 1, labels[i]);
        setMapControlRect((MapControlAction)i, bx, btnY, btnW[i], mapNavBtnH);
    }
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    const int legendY = my + mh - CHAR_H - 1;
    lcd.fillRect(mx + 1, legendY - 1, mw - 2, CHAR_H + 2, COL_PANEL_ALT);
    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(COL_TEXT_DIM, COL_PANEL_ALT);
    drawClippedText(mx + 4, legendY, mw - 8, "Scroll:Prev/Next  M:Me  I:+  O:-");
#endif

    lcd.setFont(UI_BODY_FONT);
    mapLastDrawMs = millis();
    dirtyNodes = false;
    dirtyChat = downloadedAnyTile;
}

static void drawNodesPanel() {
    clearPanelCloseRect();
    const int mx = 0;
    const int my = CHAT_Y;
    const int mw = LCD_W;
    const int mh = panelOverlayBottomY() - my + 1;
    const int titleH = 11;
        const bool nodesScrollButtons =
    #if defined(DEVICE_TDECK)
        false;
    #else
        showPanelScrollButtons();
    #endif
    const int ix = mx + 3;
    const int iw = mw - 6;
    const int controlsBottom = my + mh - TOUCH_BTN_BOTTOM_PAD;
        const int controlsTop = nodesScrollButtons
                          ? (controlsBottom - TOUCH_BTN_H)
                          : (my + mh - 1);
        const int contentBottom = nodesScrollButtons ? (controlsTop - 2) : (my + mh - 2);
    const int contentY = my + titleH + 2;
    const int contentH = max(60, contentBottom - contentY + 1);
    const int colGap = 4;
    const int listChars = 5;
    const int listW = min(58, max(44, listChars * CHAR_W + 12));
    const int detailX = ix;
    const int listX = ix + iw - listW;
    const int detailW = max(90, listX - detailX - colGap);
    const int listY = contentY;
    const int listHeaderH = 10;
    const int rowH = 9;
    const int rowsVisible = max(1, (contentH - listHeaderH - 1) / rowH);

    const int totalNodes = nodesVisibleNodeCount();
    nodesListSel = constrain(nodesListSel, 0, max(0, totalNodes - 1));
    NodeEntry *selected = (totalNodes > 0) ? nodesVisibleNodeByIndex(nodesListSel) : nullptr;

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    char hdr[44];
    if (selected) {
        const char *sn = selected->shortName[0] ? selected->shortName : "----";
        snprintf(hdr, sizeof(hdr), "Nodes %d  [%s]", totalNodes, sn);
    } else {
        snprintf(hdr, sizeof(hdr), "Nodes %d", totalNodes);
    }
    lcd.fillRect(mx + 1, my + 1, mw - 2, titleH, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(mx + 5, my + 2, mw - 10, hdr);

    drawPanelFrame(detailX, contentY, detailW, contentH, COL_PANEL_STRONG, COL_DIVIDER);
    drawPanelFrame(listX, listY, listW, contentH, COL_PANEL_BG, COL_DIVIDER);
    lcd.fillRect(listX + 1, listY + 1, listW - 2, listHeaderH, COL_PANEL_ALT);
    lcd.setTextColor(COL_TEXT_DIM, COL_PANEL_ALT);
    drawClippedText(listX + 3, listY + 2, listW - 6, "Nodes");

    const int innerX = detailX + 2;
    const int innerY = contentY + 2;
    const int innerW = detailW - 4;
    const int innerH = contentH - 4;
    const int splitGap = 4;
    int mapW = max(56, (innerW * 35) / 100);
    int infoW = innerW - mapW - splitGap;
    if (infoW < 72) {
        infoW = 72;
        mapW = max(48, innerW - infoW - splitGap);
    }
    if (mapW < 48) {
        mapW = 48;
        infoW = max(56, innerW - mapW - splitGap);
    }
    if (infoW + mapW + splitGap > innerW) {
        infoW = max(56, innerW - mapW - splitGap);
    }
    const int infoX = innerX;
    const int infoY = innerY;
    const int infoH = innerH;
    const int mapX = infoX + infoW + splitGap;
    const int mapY = innerY;
    const int mapH = innerH;
    const int infoBottom = infoY + infoH;
#if defined(DEVICE_TLORA_PAGER_TFT)
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(1.25f);
#else
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
#endif
    const int infoLineH = max(9, lcd.fontHeight() + 2);
    int infoRow = 0;

    auto infoLine = [&](uint16_t col, const char *s) {
        int y = infoY + infoRow * infoLineH;
        if (y + infoLineH > infoBottom) return;
        uint16_t bg = (infoRow & 1) ? COL_PANEL_STRONG : COL_PANEL_ALT;
        lcd.fillRect(infoX, y, infoW, infoLineH, bg);
        lcd.setTextColor(col, bg);
        drawClippedText(infoX + 2, y + 1, infoW - 4, s);
        infoRow++;
    };

    if (!selected) {
        infoLine(TFT_RED, "No nodes");
    } else {
        char buf[96];
#if defined(DEVICE_TLORA_PAGER_TFT)
        snprintf(buf, sizeof(buf), "ID !%08x", selected->nodeId);
        infoLine(COL_TEAL, buf);

        snprintf(buf, sizeof(buf), "Name: %s",
                 selected->longName[0] ? selected->longName : "(unknown)");
        infoLine(COL_TEXT_MAIN, buf);

        uint32_t ageSec = (selected->lastHeardMs == 0) ? 0 : ((millis() - selected->lastHeardMs) / 1000UL);
        if (selected->lastHeardMs == 0) {
            snprintf(buf, sizeof(buf), "Heard: --");
        } else if (ageSec < 60UL) {
            snprintf(buf, sizeof(buf), "Heard: %lus", (unsigned long)ageSec);
        } else if (ageSec < 3600UL) {
            snprintf(buf, sizeof(buf), "Heard: %lum", (unsigned long)(ageSec / 60UL));
        } else {
            snprintf(buf, sizeof(buf), "Heard: %luh%lum",
                     (unsigned long)(ageSec / 3600UL),
                     (unsigned long)((ageSec % 3600UL) / 60UL));
        }
        infoLine(COL_TEXT_MAIN, buf);

        const char *chanName = (selected->chanIdx >= 0 && selected->chanIdx < MAX_CHANNELS)
                             ? CHANNEL_KEYS[selected->chanIdx].name : "?";
        snprintf(buf, sizeof(buf), "Ch:%s  H:%d", chanName, selected->hops);
        infoLine(COL_TEXT_MAIN, buf);
        snprintf(buf, sizeof(buf), "SNR:%+.1f", selected->snr);
        infoLine(COL_TEXT_MAIN, buf);

        float posLat = 0.0f;
        float posLon = 0.0f;
        if (mapExtractNodeCoords(selected, posLat, posLon)) {
            snprintf(buf, sizeof(buf), "Lat: %.3f", posLat);
            infoLine(COL_TEXT_MAIN, buf);
            snprintf(buf, sizeof(buf), "Lon: %.3f", posLon);
            infoLine(COL_TEXT_MAIN, buf);
            snprintf(buf, sizeof(buf), "Alt: %d m", (int)selected->alt);
            infoLine(COL_TEXT_MAIN, buf);
        }

        if (selected->hasTelemetry) {
            snprintf(buf, sizeof(buf), "Bat: %.0f%% %.2fV", selected->battPct, selected->voltage);
            infoLine(COL_TEXT_MAIN, buf);
        }
#else
        snprintf(buf, sizeof(buf), "ID: !%08x", selected->nodeId);
        infoLine(COL_TEAL, buf);
        snprintf(buf, sizeof(buf), "Long: %s",
                 selected->longName[0] ? selected->longName : "(unknown)");
        infoLine(COL_TEXT_MAIN, buf);
        snprintf(buf, sizeof(buf), "Short: %s",
                 selected->shortName[0] ? selected->shortName : "----");
        infoLine(COL_TEXT_MAIN, buf);

        uint32_t ageSec = (selected->lastHeardMs == 0) ? 0 : ((millis() - selected->lastHeardMs) / 1000UL);
        if (selected->lastHeardMs == 0) {
            snprintf(buf, sizeof(buf), "Heard: unknown");
        } else if (ageSec < 60UL) {
            snprintf(buf, sizeof(buf), "Heard: %lus ago", (unsigned long)ageSec);
        } else if (ageSec < 3600UL) {
            snprintf(buf, sizeof(buf), "Heard: %lum %lus ago",
                     (unsigned long)(ageSec / 60UL), (unsigned long)(ageSec % 60UL));
        } else {
            snprintf(buf, sizeof(buf), "Heard: %luh %lum ago",
                     (unsigned long)(ageSec / 3600UL),
                     (unsigned long)((ageSec % 3600UL) / 60UL));
        }
        infoLine(COL_TEXT_MAIN, buf);

        const char *chanName = (selected->chanIdx >= 0 && selected->chanIdx < MAX_CHANNELS)
                             ? CHANNEL_KEYS[selected->chanIdx].name : "?";
        snprintf(buf, sizeof(buf), "Chan: %s  Hops:%d  SNR:%+.1f", chanName, selected->hops, selected->snr);
        infoLine(COL_TEXT_MAIN, buf);

        float posLat = 0.0f;
        float posLon = 0.0f;
        if (mapExtractNodeCoords(selected, posLat, posLon)) {
            snprintf(buf, sizeof(buf), "Lat: %.5f  Lon: %.5f", posLat, posLon);
            infoLine(COL_TEXT_MAIN, buf);
            snprintf(buf, sizeof(buf), "Alt: %d m", (int)selected->alt);
            infoLine(COL_TEXT_MAIN, buf);
        }

        if (selected->hasTelemetry) {
            snprintf(buf, sizeof(buf), "Batt: %.0f%%  %.2fV", selected->battPct, selected->voltage);
            infoLine(COL_TEXT_MAIN, buf);
        }
#endif
    }

    // Keep map/list labels compact even when pager info text is enlarged.
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    drawPanelFrame(mapX, mapY, mapW, mapH, COL_PANEL_BG, COL_DIVIDER);
    lcd.fillRect(mapX + 1, mapY + 1, mapW - 2, 9, COL_PANEL_ALT);
    lcd.setTextColor(COL_TEXT_DIM, COL_PANEL_ALT);
    drawClippedText(mapX + 3, mapY + 1, mapW - 6, "Mini map (state-scale)");
    bool downloadedAnyTile = false;

    float selLat = 0.0f;
    float selLon = 0.0f;
    bool selHasPos = mapExtractNodeCoords(selected, selLat, selLon);
    if (!selHasPos) {
        lcd.fillRect(mapX + 1, mapY + 11, mapW - 2, mapH - 12, COL_PANEL_STRONG);
        lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_STRONG);
        drawClippedText(mapX + 6, mapY + mapH / 2 - 4, mapW - 12, "No position for selected node");
    } else {
        // Keep this intentionally zoomed out and low detail for quick context.
        float latSpan = 4.8f;
        float lonSpan = 6.8f;
        float halfLat = latSpan * 0.5f;
        float halfLon = lonSpan * 0.5f;
        float centerLat = selLat;
        float centerLon = selLon;
        centerLat = max(-90.0f + halfLat, min(90.0f - halfLat, centerLat));
        centerLon = max(-180.0f + halfLon, min(180.0f - halfLon, centerLon));
        float minLat = centerLat - halfLat;
        float maxLat = centerLat + halfLat;
        float minLon = centerLon - halfLon;
        float maxLon = centerLon + halfLon;

        const int mapInnerX = mapX + 1;
        const int mapInnerY = mapY + 11;
        const int mapInnerW = max(8, mapW - 2);
        const int mapInnerH = max(8, mapH - 12);

        bool allowDownloads = nodesPanelCanDownloadTiles();
        bool useTileBackdrop = HAS_SD_CARD;

        if (useTileBackdrop) {
            double lonSpanForZoom = max((double)MAP_MIN_LON_SPAN, (double)(maxLon - minLon));
            double zoomRaw = log2(((double)mapInnerW * 360.0) / (256.0 * lonSpanForZoom));
            int z = constrain((int)floor(zoomRaw), 0, MAP_MAX_TILE_ZOOM);
            int tileCount = 1 << z;

            auto lonToWorldX = [&](double lonDeg) -> double {
                return ((lonDeg + 180.0) / 360.0) * (256.0 * tileCount);
            };
            auto latToWorldY = [&](double latDeg) -> double {
                double clamped = max(-85.05112878, min(85.05112878, latDeg));
                double rad = clamped * M_PI / 180.0;
                double merc = log(tan(rad) + 1.0 / cos(rad));
                return (1.0 - merc / M_PI) * 0.5 * (256.0 * tileCount);
            };

            double centerWX = lonToWorldX(centerLon);
            double centerWY = latToWorldY(centerLat);
            double leftWX = centerWX - (double)mapInnerW * 0.5;
            double topWY = centerWY - (double)mapInnerH * 0.5;

            int tx0 = (int)floor(leftWX / 256.0);
            int ty0 = (int)floor(topWY / 256.0);
            int tx1 = (int)floor((leftWX + mapInnerW - 1) / 256.0);
            int ty1 = (int)floor((topWY + mapInnerH - 1) / 256.0);
            int fetchBudget = allowDownloads ? 1 : 0;

            lcd.setClipRect(mapInnerX, mapInnerY, mapInnerW, mapInnerH);
            lcd.fillRect(mapInnerX, mapInnerY, mapInnerW, mapInnerH, COL_PANEL_STRONG);
            for (int ty = ty0; ty <= ty1; ty++) {
                if (ty < 0 || ty >= tileCount) continue;
                for (int tx = tx0; tx <= tx1; tx++) {
                    int wrappedX = tx % tileCount;
                    if (wrappedX < 0) wrappedX += tileCount;

                    int drawX = mapInnerX + (int)(tx * 256.0 - leftWX);
                    int drawY = mapInnerY + (int)(ty * 256.0 - topWY);

                    String tilePath;
                    bool downloaded = false;
                    bool hasTile = mapEnsureTileFile((uint8_t)z, wrappedX, ty,
                                                    fetchBudget > 0, tilePath, downloaded);
                    if (downloaded && fetchBudget > 0) {
                        fetchBudget--;
                        downloadedAnyTile = true;
                    }

                    if (hasTile) {
                        lcd.drawPngFile(SD, tilePath.c_str(), drawX, drawY);
                    } else {
                        uint16_t fb = (((wrappedX + ty) & 1) ? COL_PANEL_STRONG : COL_PANEL_ALT);
                        lcd.fillRect(drawX, drawY, 256, 256, fb);
                        lcd.drawRect(drawX, drawY, 256, 256, COL_DIVIDER);
                    }
                }
            }
            lcd.clearClipRect();
        } else {
            lcd.fillRect(mapX + 1, mapY + 11, mapW - 2, mapH - 12, COL_PANEL_STRONG);
            for (int g = 1; g < 4; g++) {
                int gx = mapX + 1 + (g * (mapW - 2)) / 4;
                int gy = mapY + 11 + (g * (mapH - 12)) / 4;
                lcd.drawFastVLine(gx, mapY + 11, mapH - 12, COL_DIVIDER_HI);
                lcd.drawFastHLine(mapX + 1, gy, mapW - 2, COL_DIVIDER_HI);
            }
        }

        auto lonToX = [&](float lon) -> int {
            float t = (lon - minLon) / max(0.001f, maxLon - minLon);
            t = max(0.0f, min(1.0f, t));
            return mapX + 2 + (int)(t * (float)(mapW - 5));
        };
        auto latToY = [&](float lat) -> int {
            float t = (maxLat - lat) / max(0.001f, maxLat - minLat);
            t = max(0.0f, min(1.0f, t));
            return mapY + 12 + (int)(t * (float)(mapH - 15));
        };

        // NODES panel mini-map intentionally shows only the selected node.
        int px = lonToX(selLon);
        int py = latToY(selLat);
        const int markerR = 5;
        lcd.fillCircle(px, py, markerR, COL_SELECT_ACCENT);
        lcd.drawCircle(px, py, markerR + 2, COL_TEXT_ON_ACCENT);
        lcd.drawCircle(px, py, markerR + 4, COL_TEAL);
    }

    const int listRowsTop = listY + listHeaderH + 1;
    if (totalNodes == 0) {
        lcd.fillRect(listX + 1, listRowsTop, listW - 2, rowH, COL_PANEL_BG);
        lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_BG);
        drawClippedText(listX + 2, listRowsTop + 1, listW - 4, "None");
    } else {
        int firstVisible = max(0, nodesListSel - (rowsVisible - 1));
        int maxFirst = max(0, totalNodes - rowsVisible);
        if (firstVisible > maxFirst) firstVisible = maxFirst;

        for (int row = 0; row < rowsVisible; row++) {
            int idx = firstVisible + row;
            int y = listRowsTop + row * rowH;
            uint16_t rowBg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
            lcd.fillRect(listX + 1, y, listW - 2, rowH, rowBg);
            if (idx >= totalNodes) continue;

            NodeEntry *n = nodesVisibleNodeByIndex(idx);
            if (!n) continue;

            bool sel = (idx == nodesListSel);
            uint16_t bg = sel ? COL_SELECT_BG : rowBg;
            if (sel) lcd.fillRect(listX + 1, y, listW - 2, rowH, bg);

            bool hasLocation = false;
            float lat = 0.0f, lon = 0.0f;
            hasLocation = mapExtractNodeCoords(n, lat, lon);
            const char *sn = n->shortName[0] ? n->shortName : "----";
            uint16_t fg = sel ? COL_TEXT_ON_ACCENT : (hasLocation ? COL_TEXT_MAIN : COL_TAB_IDLE);
            lcd.setTextColor(fg, bg);
            drawClippedText(listX + 3, y + 1, listW - 6, sn);
        }
    }

    if (showPanelCloseButtons()) {
        int closeY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
        drawPanelCloseButton(mx + 3, closeY, TOUCH_BTN_W, TOUCH_BTN_H);
    }

    if (nodesScrollButtons) {
        int btnY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
        const int btnW = 52;
        const int btnGap = 4;
        const int downX = ix + iw - btnW;
        const int upX = downX - btnGap - btnW;
        uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);

        drawSquirclePill(upX, btnY, btnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
        lcd.setFont(UI_BODY_FONT);
        lcd.setTextColor(COL_TEXT_MAIN, fill);
        int tw = lcd.textWidth("Up");
        int tx = upX + max(1, (btnW - tw) / 2);
        int ty = btnY + max(0, (TOUCH_BTN_H - CHAR_H) / 2);
        drawClippedText(tx, ty, btnW - (tx - upX) - 1, "Up");
        setNodesControlRect(NODES_CTL_UP, upX, btnY, btnW, TOUCH_BTN_H);

        drawSquirclePill(downX, btnY, btnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
        tw = lcd.textWidth("Down");
        tx = downX + max(1, (btnW - tw) / 2);
        drawClippedText(tx, ty, btnW - (tx - downX) - 1, "Down");
        setNodesControlRect(NODES_CTL_DOWN, downX, btnY, btnW, TOUCH_BTN_H);
    }

    lcd.setFont(UI_BODY_FONT);
    dirtyChat = downloadedAnyTile;
}

// ── Draw: node list ───────────────────────────────────────────
static void drawNodes() {
    drawPanelFrame(NODE_X, CHAT_Y, NODE_W, CHAT_H, COL_PANEL_BG, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    const int MAX_VISIBLE = CHAT_H / LINE_H;  // 29
    uint32_t now = millis();

    for (int i = 0; i < MAX_VISIBLE; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        int      y   = CHAT_Y + i * LINE_H;
        uint16_t rowBg = (i & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
        lcd.fillRect(NODE_X + 1, y, NODE_W - 2, LINE_H, rowBg);
        if (!n) continue;

        bool     sel = nodeListFocused && (i == nodeListSel);
        uint16_t bg  = sel ? COL_SELECT_BG : rowBg;
        uint32_t age = now - n->lastHeardMs;
        uint16_t col = sel               ? COL_TEXT_ON_ACCENT :
                   (age < 60000UL)   ? COL_NODE_HOT       :
                   (age < 3600000UL) ? COL_NODE_WARM      : COL_TAB_IDLE;

        if (sel) lcd.fillRect(NODE_X + 1, y, NODE_W - 2, LINE_H, bg);

        char r[32];
        if (n->hasTelemetry && n->battPct > 0)
            snprintf(r, sizeof(r), "%s %d %+.0f %d%%",
                     n->shortName, n->hops, n->snr, (int)n->battPct);
        else
            snprintf(r, sizeof(r), "%s %d %+.0f",
                     n->shortName, n->hops, n->snr);

        lcd.setTextColor(col, bg);
        drawClippedText(NODE_X + 2, y + 1, NODE_W - 4, r);
    }
    lcd.setFont(UI_BODY_FONT);
    dirtyNodes = false;
}

// ── Draw: GPS / compass page ──────────────────────────────────
static void drawCompassRose(int cx, int cy, int cr, float heading) {
    lcd.drawCircle(cx, cy, cr,     COL_TEAL);
    lcd.drawCircle(cx, cy, cr - 1, COL_DIVIDER);

    // Tick marks: 8 × 45° — cardinal (N/S/E/W) are longer
    for (int a = 0; a < 360; a += 45) {
        float rad    = a * (float)M_PI / 180.0f;
        int   tlen   = (a % 90 == 0) ? 8 : 4;
        int   x1     = cx + (int)((cr - 1)     * sinf(rad));
        int   y1     = cy - (int)((cr - 1)     * cosf(rad));
        int   x2     = cx + (int)((cr - tlen)  * sinf(rad));
        int   y2     = cy - (int)((cr - tlen)  * cosf(rad));
        lcd.drawLine(x1, y1, x2, y2, COL_TAB_IDLE);
    }

    // Cardinal labels
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    lcd.setTextColor(TFT_RED, COL_PANEL_BG);
    lcd.setCursor(cx - 3, cy - cr + 1);  lcd.print("N");
    lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_BG);
    lcd.setCursor(cx - 3, cy + cr - 8);  lcd.print("S");
    lcd.setCursor(cx + cr - 7, cy - 4);  lcd.print("E");
    lcd.setCursor(cx - cr + 1, cy - 4);  lcd.print("W");

    // Needle: north arm (red) + south tail (grey)
    float headRad = heading * (float)M_PI / 180.0f;
    int   nLen    = cr - 12;
    int   sLen    = cr - 22;
    int   nx = cx + (int)(nLen * sinf(headRad));
    int   ny = cy - (int)(nLen * cosf(headRad));
    int   sx = cx - (int)(sLen * sinf(headRad));
    int   sy = cy + (int)(sLen * cosf(headRad));
    // Draw each arm twice (one pixel thick is fine on small display)
    lcd.drawLine(cx, cy, nx, ny, TFT_RED);
    lcd.drawLine(cx, cy, sx, sy, COL_TAB_IDLE);

    // Centre dot
    lcd.fillCircle(cx, cy, 3, COL_TEXT_MAIN);
}

static void drawGps() {
    drawPanelFrame(0, CHAT_Y, LCD_W, CHAT_H, COL_PANEL_BG, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    const bool    stream  = gpsHasNmeaStream();
    const bool    fix     = gpsHasFix();
    const uint8_t sats    = gpsSats();
    const float   course  = gpsCourse();
    const float   speed   = gpsSpeedKmh();
    const int32_t latI    = fix ? gpsLatI()  : gCfg.latI;
    const int32_t lonI    = fix ? gpsLonI()  : gCfg.lonI;
    const int32_t altM    = fix ? gpsAltM()  : gCfg.alt;

    char buf[40];
    const int TX  = 8;
    const int DIM = COL_TEXT_DIM;
    const int LEFT_W = 152;
    const int RIGHT_X = LEFT_W + 5;
    const int RIGHT_W = LCD_W - RIGHT_X - 1;

    drawPanelFrame(4, CHAT_Y + 4, LEFT_W - 6, CHAT_H - 8, COL_PANEL_STRONG, COL_DIVIDER);
    drawPanelFrame(RIGHT_X, CHAT_Y + 4, RIGHT_W, CHAT_H - 8, COL_PANEL_BG, COL_DIVIDER);

    // ── Left panel: text rows ─────────────────────────────────
    const int GH = 10;  // GPS panel always uses Loose spacing
    int row = 0;
    auto pr = [&](uint16_t col, const char *s) {
        int y = CHAT_Y + 10 + row++ * GH;
        lcd.fillRect(6, y, LEFT_W - 10, GH, COL_PANEL_STRONG);
        lcd.setTextColor(col, COL_PANEL_STRONG);
        drawClippedText(TX, y, LEFT_W - 14, s);
    };

    // Status
    if (!gCfg.gpsEnabled) {
        pr(COL_TAB_IDLE, "GPS: DISABLED");
    } else if (!stream) {
        uint32_t elapsed = gpsSearchTimeMs();
        snprintf(buf, sizeof(buf), "GPS: NO DATA %lus...", (unsigned long)(elapsed / 1000));
        pr(COL_BATT_BAD, buf);
    } else if (fix) {
        uint32_t ttff = gpsSearchTimeMs();
        snprintf(buf, sizeof(buf), "GPS: FIX  sats:%d  ttff:%lus", sats,
                 (unsigned long)(ttff / 1000));
        pr(COL_TEAL, buf);
    } else {
        uint32_t elapsed = gpsSearchTimeMs();
        if (sats > 0) {
            snprintf(buf, sizeof(buf), "GPS: searching sats:%u", (unsigned)sats);
        } else {
            snprintf(buf, sizeof(buf), "GPS: searching %lus...", (unsigned long)(elapsed / 1000));
        }
        pr(COL_TAB_UNREAD, buf);
    }

    if (gCfg.gpsEnabled) {
        if (!stream) {
            pr(COL_BATT_BAD, "NMEA stream: waiting");
        } else {
            snprintf(buf, sizeof(buf), "Sats connected: %u", (unsigned)sats);
            pr((sats > 0) ? COL_TEXT_MAIN : (uint16_t)DIM, buf);
        }
    }
    row++;  // blank line

    // Latitude
    float lat = latI * 1e-7f;
    snprintf(buf, sizeof(buf), "Lat  %10.6f %c",
             lat >= 0 ? lat : -lat, lat >= 0 ? 'N' : 'S');
    pr(fix ? COL_TEXT_MAIN : (uint16_t)DIM, buf);

    // Longitude
    float lon = lonI * 1e-7f;
    snprintf(buf, sizeof(buf), "Lon  %10.6f %c",
             lon >= 0 ? lon : -lon, lon >= 0 ? 'E' : 'W');
    pr(fix ? COL_TEXT_MAIN : (uint16_t)DIM, buf);

    // Altitude
    snprintf(buf, sizeof(buf), "Alt  %d m", (int)altM);
    pr(fix ? COL_TEXT_MAIN : (uint16_t)DIM, buf);

    row++;  // blank line

    // Course / speed
    snprintf(buf, sizeof(buf), "Hdg  %.1f\xb0", course);
    pr(fix ? COL_NODE_HOT : (uint16_t)DIM, buf);

    snprintf(buf, sizeof(buf), "Spd  %.1f km/h", speed);
    pr(fix ? COL_NODE_HOT : (uint16_t)DIM, buf);

    row++;  // blank line

    // Fallback notice when no fix
    if (!fix && gCfg.gpsEnabled) {
        pr(COL_TAB_IDLE, "(showing stored pos)");
    }

    // ── Right panel: compass ──────────────────────────────────
    const int CX = RIGHT_X + RIGHT_W / 2;
    const int CY = CHAT_Y + CHAT_H / 2 - 4;   // vertically centred
    const int CR = 62;
    drawCompassRose(CX, CY, CR, fix ? course : 0.0f);

    // Heading value below compass
    snprintf(buf, sizeof(buf), "%3.0f\xb0", fix ? course : 0.0f);
    int tw = lcd.textWidth(buf);
    lcd.setTextColor(fix ? COL_TEXT_MAIN : (uint16_t)COL_TAB_IDLE, COL_PANEL_BG);
    lcd.drawString(buf, CX - tw / 2, CY + CR + 4);

    lcd.setFont(UI_BODY_FONT);
    dirtyChat = false;
}

// ── DM helper: open a conversation with a node ────────────────
static void openDmWith(NodeEntry *n) {
    if (!n) return;
    const char *sn = n->shortName[0] ? n->shortName : "????";
    DmConv *c = DMs.findOrCreate(n->nodeId, sn);
    if (c && c->rxChanIdx < 0 && n->chanIdx >= 0 && n->chanIdx < MESH_CHANNELS)
        c->rxChanIdx = n->chanIdx;
    DMs.markRead(n->nodeId);
    dmConvNodeId = n->nodeId;
    dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    if (inputLen == 0) hwTypingLock = true;
#endif
    dmPickerOpen = false;
    dmDeleteConfirm = false;
    dmDeleteConfirmNodeId = 0;
    dirtyChat = dirtyInput = true;
}

static DmConv *selectedDmListConv() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (dmListSel < 0 || dmListSel >= DMs.count()) return nullptr;
    return DMs.getByRank(dmListSel);
#else
    if (dmListSel <= 0 || dmListSel > DMs.count()) return nullptr;
    return DMs.getByRank(dmListSel - 1);
#endif
}

static void clearDmDeleteConfirm() {
    dmDeleteConfirm = false;
    dmDeleteConfirmNodeId = 0;
}

// ── Draw: DM contact list ─────────────────────────────────────
static void drawDmList() {
    clearPanelCloseRect();
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = panelOverlayBottomY() - my + 1;
    const int ix = mx + 3;
    const int iy = my + 3;
    const int iw = mw - 6;
        const int dmLegendReserveH =
    #if defined(DEVICE_TDECK)
        (CHAR_H + 2);
    #else
        0;
    #endif
        const int controlsBottom = my + mh - dmLegendReserveH - TOUCH_BTN_BOTTOM_PAD;
        const int controlsTop = controlsBottom - TOUCH_BTN_H;
    const int rowsVisible = max(1, (controlsTop - iy - 1) / DM_LINE_H);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(1.0f);
#else
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(CHAT_WINDOW_TEXT_SCALE);
#endif
    dirtyNodes = false;

    // Rows: conversations.
    const int rows = min(DMs.count(), rowsVisible);
    for (int i = 0; i < rows; i++) {
        DmConv *c = DMs.getByRank(i);
        if (!c) break;

        int      y   = iy + i * DM_LINE_H;
#if defined(DEVICE_TLORA_PAGER_TFT)
        bool     sel = (dmListSel == i);
#else
        bool     sel = (dmListSel == i + 1);
#endif
        uint16_t bg  = sel ? COL_SELECT_BG : ((i & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        uint16_t col = sel ? COL_TEXT_ON_ACCENT
                   : c->unread ? COL_TAB_UNREAD : COL_DM_MUTED;

        lcd.fillRect(ix, y, iw, DM_LINE_H, bg);

        char row[DM_LINE_LEN + 1];
        snprintf(row, sizeof(row), "[%s] %.44s", c->shortName, c->lastText);

        lcd.setTextColor(col, bg);
        drawClippedText(ix + 4, y + 1, iw - 8, row);
    }

    const int closeX = mx + 3;
    const int closeY = controlsTop;
#if !defined(DEVICE_TLORA_PAGER_TFT)
    const int newW = TOUCH_BTN_W;
    const int newH = TOUCH_BTN_H;
    const int newX = closeX + newW + 4;
    const int newY = closeY;
    bool newSel = (dmListSel == 0);
    uint16_t newFill = newSel ? COL_SELECT_BG : lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
    drawSquirclePill(newX, newY, newW, newH, newFill, COL_SELECT_ACCENT, newSel);
    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(newSel ? COL_TEXT_ON_ACCENT : COL_TEXT_MAIN, newFill);
    int ntw = lcd.textWidth("NEW DM");
    drawClippedText(newX + max(1, (newW - ntw) / 2), newY + max(0, (newH - CHAR_H) / 2), newW - 2, "NEW DM");
    setDmNewRect(newX, newY, newW, newH);
#endif

    drawPanelCloseButton(closeX, closeY, TOUCH_BTN_W, TOUCH_BTN_H);

#if defined(DEVICE_HELTEC_V4_EXPANSION) && HAS_TOUCH
    if (showPanelScrollButtons()) {
    const int dmBtnW = 46;
    const int dmBtnGap = 4;
    const int downX = ix + iw - dmBtnW;
    const int upX   = downX - dmBtnGap - dmBtnW;
    uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);

    drawSquirclePill(upX, closeY, dmBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
    lcd.setTextColor(COL_TEXT_MAIN, fill);
    int tw = lcd.textWidth("Up");
    int tx = upX + max(1, (dmBtnW - tw) / 2);
    int ty = closeY + max(0, (TOUCH_BTN_H - CHAR_H) / 2);
    drawClippedText(tx, ty, dmBtnW - (tx - upX) - 1, "Up");
    setDmControlRect(DM_CTL_UP, upX, closeY, dmBtnW, TOUCH_BTN_H);

    drawSquirclePill(downX, closeY, dmBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
    tw = lcd.textWidth("Down");
    tx = downX + max(1, (dmBtnW - tw) / 2);
    drawClippedText(tx, ty, dmBtnW - (tx - downX) - 1, "Down");
    setDmControlRect(DM_CTL_DOWN, downX, closeY, dmBtnW, TOUCH_BTN_H);
    }
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    const int legendY = my + mh - CHAR_H - 1;
    lcd.fillRect(mx + 1, legendY - 1, mw - 2, CHAR_H + 2, COL_PANEL_ALT);
    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(COL_TEXT_DIM, COL_PANEL_ALT);
    char legend[96];
    if (dmDeleteConfirm) {
        DmConv *c = selectedDmListConv();
        const char *sn = (c && c->shortName[0]) ? c->shortName : "????";
        snprintf(legend, sizeof(legend), "Delete [%s]? Bksp:Yes  Esc:No", sn);
    } else {
        snprintf(legend, sizeof(legend), "N:New  Enter:Open  Bksp:Delete");
    }
    drawClippedText(mx + 4, legendY, mw - 8, legend);
#endif

    lcd.setFont(UI_BODY_FONT);
    dirtyChat = false;
}

// ── Picker helpers: node list excluding self ──────────────────
// Returns the nth node (0-based) excluding myNodeId.
// Snapshot of node IDs taken when picker opens — prevents reordering while scrolling
static constexpr int DM_PICKER_FILTER_MAX = 24;
static uint32_t pickerIds[MAX_NODES];
static int      pickerCount = 0;
static uint32_t pickerFilteredIds[MAX_NODES];
static int      pickerFilteredCount = 0;
static char     dmPickerFilter[DM_PICKER_FILTER_MAX + 1] = {0};
static int      dmPickerFilterLen = 0;

static char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool containsNoCase(const char *text, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!text || !text[0]) return false;
    for (int i = 0; text[i]; i++) {
        int j = 0;
        while (needle[j] && text[i + j]
               && asciiLower(text[i + j]) == asciiLower(needle[j])) {
            j++;
        }
        if (!needle[j]) return true;
    }
    return false;
}

static bool pickerMatch(NodeEntry *n) {
    if (!n) return false;
    if (dmPickerFilterLen == 0) return true;
    if (containsNoCase(n->shortName, dmPickerFilter)) return true;
    if (containsNoCase(n->longName, dmPickerFilter)) return true;
    return false;
}

static void pickerApplyFilter() {
    pickerFilteredCount = 0;
    for (int i = 0; i < pickerCount && pickerFilteredCount < MAX_NODES; i++) {
        NodeEntry *n = Nodes.find(pickerIds[i]);
        if (!pickerMatch(n)) continue;
        pickerFilteredIds[pickerFilteredCount++] = pickerIds[i];
    }
    if (pickerFilteredCount <= 0) {
        dmPickerSel = 0;
        return;
    }
    dmPickerSel = constrain(dmPickerSel, 0, pickerFilteredCount - 1);
}

static void pickerSnapshot() {
    pickerCount = 0;
    for (int i = 0; i < Nodes.count() && pickerCount < MAX_NODES; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (n && n->nodeId != myNodeId)
            pickerIds[pickerCount++] = n->nodeId;
    }
    dmPickerFilterLen = 0;
    dmPickerFilter[0] = '\0';
    pickerApplyFilter();
}

static NodeEntry *pickerNode(int sel) {
    if (sel < 0 || sel >= pickerFilteredCount) return nullptr;
    return Nodes.find(pickerFilteredIds[sel]);
}

static int pickerNodeCount() { return pickerFilteredCount; }

// ── Draw: DM node picker ──────────────────────────────────────
static void drawDmPicker() {
    clearPanelCloseRect();
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = panelOverlayBottomY() - my + 1;
    const int ix = mx + 3;
    const int iy = my + 3;
    const int iw = mw - 6;
    const bool reserveFooter = showPanelCloseButtons() || showPanelScrollButtons();
    const int controlsTop = reserveFooter
        ? (my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD))
        : (my + mh - 1);
    const int rowsVisible = max(1, (controlsTop - iy - 1) / DM_LINE_H);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
#if defined(DEVICE_TLORA_PAGER_TFT)
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(1.0f);
#else
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(CHAT_WINDOW_TEXT_SCALE);
#endif
    dirtyNodes = false;

    // Header bar
    lcd.fillRect(ix, iy, iw, DM_LINE_H, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    char pickerHeader[DM_LINE_LEN + 1];
    if (dmPickerFilterLen > 0)
        snprintf(pickerHeader, sizeof(pickerHeader), "Select recipient [%s]", dmPickerFilter);
    else
        snprintf(pickerHeader, sizeof(pickerHeader), "Select recipient");
    drawClippedText(ix + 4, iy + 1, iw - 8, pickerHeader);

    int filteredCount = pickerNodeCount();
    if (filteredCount == 0) {
        lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_BG);
        if (dmPickerFilterLen > 0) {
            char noMatch[DM_LINE_LEN + 1];
            snprintf(noMatch, sizeof(noMatch), "No matches for: %s", dmPickerFilter);
            drawClippedText(ix + 4, iy + 3 * DM_LINE_H + 1, iw - 8, noMatch);
        } else {
            drawClippedText(ix + 4, iy + 3 * DM_LINE_H + 1, iw - 8, "No other nodes known yet");
        }
        drawPanelCloseButton(mx + 3,
                     my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                     TOUCH_BTN_W, TOUCH_BTN_H);
        lcd.setFont(UI_BODY_FONT);
        dirtyChat = false;
        return;
    }

    const int MSG_ROWS   = rowsVisible - 1;
    int firstVisible = max(0, dmPickerSel - (MSG_ROWS - 1));

    for (int row = 0; row < MSG_ROWS; row++) {
        int vi = firstVisible + row;
        int      y  = iy + (row + 1) * DM_LINE_H;
        if (vi >= filteredCount) {
            lcd.fillRect(ix, y, iw, DM_LINE_H, (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
            continue;
        }
        NodeEntry *n = pickerNode(vi);
        if (!n) break;

        bool     sel = (vi == dmPickerSel);
        uint16_t bg  = sel ? COL_SELECT_BG : ((row & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        uint16_t col = sel ? COL_TEXT_ON_ACCENT : COL_TEAL;

        lcd.fillRect(ix, y, iw, DM_LINE_H, bg);

        char entry[DM_LINE_LEN + 1];
        snprintf(entry, sizeof(entry), "[%s] %-28s !%08x",
                 n->shortName[0] ? n->shortName : "????",
                 n->longName[0]  ? n->longName  : "(unknown)",
                 (unsigned)n->nodeId);

        lcd.setTextColor(col, bg);
        drawClippedText(ix + 4, y + 1, iw - 8, entry);
    }

    drawPanelCloseButton(mx + 3,
                         my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                         TOUCH_BTN_W, TOUCH_BTN_H);

#if defined(DEVICE_HELTEC_V4_EXPANSION) && HAS_TOUCH
    if (showPanelScrollButtons()) {
    const int closeY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
    const int dmBtnW = 52;
    const int dmBtnGap = 4;
    const int downX = ix + iw - dmBtnW;
    const int upX   = downX - dmBtnGap - dmBtnW;
    uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);

    drawSquirclePill(upX, closeY, dmBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(COL_TEXT_MAIN, fill);
    int tw = lcd.textWidth("Up");
    int tx = upX + max(1, (dmBtnW - tw) / 2);
    int ty = closeY + max(0, (TOUCH_BTN_H - CHAR_H) / 2);
    drawClippedText(tx, ty, dmBtnW - (tx - upX) - 1, "Up");
    setDmControlRect(DM_CTL_UP, upX, closeY, dmBtnW, TOUCH_BTN_H);

    drawSquirclePill(downX, closeY, dmBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
    tw = lcd.textWidth("Down");
    tx = downX + max(1, (dmBtnW - tw) / 2);
    drawClippedText(tx, ty, dmBtnW - (tx - downX) - 1, "Down");
    setDmControlRect(DM_CTL_DOWN, downX, closeY, dmBtnW, TOUCH_BTN_H);
    }
#endif

    lcd.setFont(UI_BODY_FONT);
    dirtyChat = false;
}

// ── Draw: DM conversation view ────────────────────────────────
static void drawDmConv() {
    clearPanelCloseRect();
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = panelOverlayBottomY() - my + 1;
    const int ix = mx + 3;
    const int iy = my + 3;
    const int iw = mw - 6;
    const bool reserveFooter = showPanelCloseButtons() || showPanelScrollButtons();
    const int controlsTop = reserveFooter
        ? (my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD))
        : (my + mh - 1);
    const int msgRowsVisible = dmConvMessageRowsVisible();

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(CHAT_WINDOW_TEXT_SCALE);
    dirtyNodes = false;

    DmConv *c = DMs.find(dmConvNodeId);
    if (!c) {
        lcd.setTextColor(TFT_RED, COL_PANEL_BG);
        drawClippedText(ix + 4, iy + DM_LINE_H + 1, iw - 8, "Conversation not found");
        drawPanelCloseButton(mx + 3,
                     my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                     TOUCH_BTN_W, TOUCH_BTN_H);
        lcd.setFont(UI_BODY_FONT);
        dirtyChat = false;
        return;
    }

    // Header bar
    NodeEntry *node = Nodes.find(c->nodeId);
    const char *longName = (node && node->longName[0]) ? node->longName : "Unknown node";
    const char *shortName = (node && node->shortName[0])
                            ? node->shortName
                            : (c->shortName[0] ? c->shortName : "????");
    char hdr[DM_LINE_LEN + 1];
    snprintf(hdr, sizeof(hdr), "%s (%s)", longName, shortName);
    lcd.fillRect(ix, iy, iw, DM_LINE_H, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(ix + 4, iy + 1, iw - 8, hdr);

    // Spacer line between header and conversation body.
    lcd.fillRect(ix, iy + DM_LINE_H, iw, DM_LINE_H, COL_PANEL_BG);

    // Message rows begin after header + spacer.
    for (int row = 0; row < msgRowsVisible; row++) {
        int y = iy + (row + 2) * DM_LINE_H;
        uint16_t rowBg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
        lcd.fillRect(ix, y, iw, DM_LINE_H, rowBg);
        const DmLine *dl = DMs.getLine(c, row, msgRowsVisible);
        if (!dl) continue;

        uint16_t col = dl->color;
        bool ackColorApplied = false;
        if (dl->packetId) {
            switch (dl->ack) {
                case DmLine::ACKED:
                    col = (gCfg.uiMode == UI_MODE_LIGHT) ? rgb565(0x00, 0x66, 0x00) : TFT_GREEN;
                    ackColorApplied = true;
                    break;
                case DmLine::ACKED_RELAY:
                    col = TFT_YELLOW;
                    ackColorApplied = true;
                    break;
                case DmLine::NAKED:
                case DmLine::TX_FAILED:
                    col = TFT_RED;
                    ackColorApplied = true;
                    break;
                default:
                    break;
            }
        }
        if (!ackColorApplied) {
            if (gCfg.uiMode == UI_MODE_LIGHT) col = TFT_BLACK;
            else                              col = TFT_WHITE;
        }
        lcd.setTextColor(col, rowBg);
        drawClippedText(ix + 4, y + 1, iw - 8, dl->text);
    }

    drawPanelCloseButton(mx + 3,
                         my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                         TOUCH_BTN_W, TOUCH_BTN_H);

#if defined(DEVICE_HELTEC_V4_EXPANSION) && HAS_TOUCH
    if (showPanelScrollButtons()) {
    const int closeY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
    const int dmBtnW = 52;
    const int dmBtnGap = 4;
    const int downX = ix + iw - dmBtnW;
    const int upX   = downX - dmBtnGap - dmBtnW;
    uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);

    drawSquirclePill(upX, closeY, dmBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(COL_TEXT_MAIN, fill);
    int tw = lcd.textWidth("Up");
    int tx = upX + max(1, (dmBtnW - tw) / 2);
    int ty = closeY + max(0, (TOUCH_BTN_H - CHAR_H) / 2);
    drawClippedText(tx, ty, dmBtnW - (tx - upX) - 1, "Up");
    setDmControlRect(DM_CTL_UP, upX, closeY, dmBtnW, TOUCH_BTN_H);

    drawSquirclePill(downX, closeY, dmBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
    tw = lcd.textWidth("Down");
    tx = downX + max(1, (dmBtnW - tw) / 2);
    drawClippedText(tx, ty, dmBtnW - (tx - downX) - 1, "Down");
    setDmControlRect(DM_CTL_DOWN, downX, closeY, dmBtnW, TOUCH_BTN_H);
    }
#endif

    lcd.setFont(UI_BODY_FONT);
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    // DM conversation repaints can overwrite the prompt strip; request an
    // input-bar pass so the composer prompt remains visible.
    dirtyInput = true;
#endif
    dirtyChat = false;
}

static const char *cfgDeviceRoleName(uint8_t role) {
    switch (role) {
        case 0:  return "CLIENT";
        case 1:  return "CLIENT_MUTE";
        case 2:  return "ROUTER";
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

// ── Draw: settings page ───────────────────────────────────────
static void drawSettings() {
    clearPanelCloseRect();
    const int SH = SETTINGS_ROW_H;
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = panelOverlayBottomY() - my + 1;
    const int ix = mx + 3;
    const int iw = mw - 6;
    const bool reserveFooter = showPanelCloseButtons() || showPanelScrollButtons();
    const int controlsTop = reserveFooter
        ? (my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD))
        : (my + mh - 1);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    char buf[LCD_W / CHAR_W + 2];
    int y = my + 1;

    lcd.fillRect(ix, y, iw, SETTINGS_HDR_H, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(ix + 4, y + (SETTINGS_HDR_H - SH) / 2 + 1, iw - 8, "Settings");

    y += SETTINGS_HDR_H;

    // ── Read-only config info ─────────────────────────────────
    const uint16_t DIM = COL_TEXT_DIM;
    static constexpr int kCfgInfoMaxLines = 12;
    char info[kCfgInfoMaxLines][LCD_W / CHAR_W + 2];
    int infoCount = 0;

    bool hasPubKey = false;
    for (int i = 0; i < 32; i++) {
        if (myPubKey[i] != 0) {
            hasPubKey = true;
            break;
        }
    }

    snprintf(info[infoCount++], sizeof(info[0]), "Node ID: !%08x", (unsigned)myNodeId);
    snprintf(info[infoCount++], sizeof(info[0]), "Role: %s", cfgDeviceRoleName(gCfg.deviceRole));
    snprintf(info[infoCount++], sizeof(info[0]), "PKI key: %s", hasPubKey ? "present" : "missing");
    snprintf(info[infoCount++], sizeof(info[0]), "Long Name:  %.*s", (int)(LCD_W / CHAR_W) - 7, gCfg.nodeLong);
    snprintf(info[infoCount++], sizeof(info[0]), "Short Name: %s", gCfg.nodeShort);
    snprintf(info[infoCount++], sizeof(info[0]), "Frequency:  %.3f MHz", gCfg.loraFreq);
    snprintf(info[infoCount++], sizeof(info[0]), "BW:%.0f  SF:%d  CR:4/%d",
             gCfg.loraBw, gCfg.loraSf, gCfg.loraCr);
    snprintf(info[infoCount++], sizeof(info[0]), "Pwr:%d dBm  Hops:%d",
             gCfg.loraPower, gCfg.loraHopLimit);

    const int contentTop = y;
    const int contentRows = max(1, (controlsTop - contentTop) / SH);
    const int statusRows = settingsStatus[0] ? 1 : 0;

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    const int minInfoRows = min(3, infoCount);
#else
    const int minInfoRows = min(2, infoCount);
#endif

    int actionRowsVisible = min(NUM_SETTINGS,
                                max(1, contentRows - statusRows - minInfoRows));
    int infoRowsVisible = max(0, contentRows - actionRowsVisible - statusRows);
    if (infoCount > 0 && infoRowsVisible == 0 && actionRowsVisible > 1) {
        actionRowsVisible--;
        infoRowsVisible = max(0, contentRows - actionRowsVisible - statusRows);
    }

    int actionScrollMax = max(0, NUM_SETTINGS - actionRowsVisible);
    int actionScroll = 0;
    if (settingsSel >= actionRowsVisible) {
        actionScroll = settingsSel - actionRowsVisible + 1;
        if (actionScroll > actionScrollMax) actionScroll = actionScrollMax;
    }

    y = contentTop;
    for (int row = 0; row < actionRowsVisible; row++) {
        int i = actionScroll + row;
        if (i >= NUM_SETTINGS) break;
        bool sel = (i == settingsSel);
        uint16_t bg = sel ? COL_SELECT_BG : ((i & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        uint16_t fg = sel ? COL_TEXT_ON_ACCENT : COL_TEXT_DIM;
        if (sel && gCfg.uiTheme == UI_THEME_SOLARIZED && gCfg.uiMode == UI_MODE_LIGHT) {
            fg = COL_TEXT_MAIN;
        }
        lcd.fillRect(ix, y, iw, SH, bg);
        lcd.setTextColor(fg, bg);
#if HAS_SD_CARD
        if (i == SETTING_EXPORT)
            snprintf(buf, sizeof(buf), "Export Config");
        else if (i == SETTING_IMPORT)
            snprintf(buf, sizeof(buf), "Import Config");
        else
#endif
        if (i == SETTING_THEME)
            snprintf(buf, sizeof(buf), "Theme: %s", uiThemePresetName(uiThemePresetIndex()));
        else if (i == SETTING_ANNOUNCE)
            snprintf(buf, sizeof(buf), "Send NODEINFO Broadcast");
#if CFG_MSG_ALERT_TOGGLE
        else if (i == SETTING_MSG_ALERT)
            snprintf(buf, sizeof(buf), "Notification Sound: %s", msgAlertSoundName(gCfg.msgAlertSound));
#endif
        else if (i == SETTING_SPLASH_MELODY)
            snprintf(buf, sizeof(buf), "Splash Melody: %s", gCfg.splashMelodyEnabled ? "On" : "Off");
        else if (i == SETTING_CLEAR_MSGS)
            snprintf(buf, sizeof(buf), "Clear Messages");
        else if (i == SETTING_CLEAR_NODES)
            snprintf(buf, sizeof(buf), "Clear Nodes");
        else if (i == SETTING_FACTORY_RESET)
            snprintf(buf, sizeof(buf), "Factory Reset");
        else if (webCfgRunning())
            snprintf(buf, sizeof(buf), "Web Config: %s", webCfgIP());
        else
            snprintf(buf, sizeof(buf), "Web Config: OFF");
        drawClippedText(ix + 4, y + 1, iw - 8, buf);
        y += SH;
    }

    if (settingsStatus[0] && y + SH <= controlsTop - 1) {
        lcd.fillRect(ix, y, iw, SH, COL_PANEL_BG);
        lcd.setTextColor(COL_TEAL, COL_PANEL_BG);
        drawClippedText(ix + 2, y, iw - 4, settingsStatus);
        y += SH;
    }

    settingsInfoScrollMax = max(0, infoCount - infoRowsVisible);
    settingsInfoScroll = constrain(settingsInfoScroll, 0, settingsInfoScrollMax);

    for (int row = 0; row < infoRowsVisible; row++) {
        int idx = settingsInfoScroll + row;
        int rowY = y + row * SH;
        uint16_t bg = ((idx & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        lcd.fillRect(ix, rowY, iw, SH, bg);
        if (idx >= infoCount) continue;
        lcd.setTextColor(DIM, bg);
        drawClippedText(ix + 2, rowY, iw - 4, info[idx]);
    }

    int usedBottom = y + infoRowsVisible * SH;
    if (usedBottom < controlsTop) {
        lcd.fillRect(ix, usedBottom, iw, controlsTop - usedBottom, COL_PANEL_BG);
    }

    if (infoRowsVisible > 0 && settingsInfoScroll > 0) {
        uint16_t topBg = ((settingsInfoScroll & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        lcd.setTextColor(COL_TEAL, topBg);
        drawClippedText(ix + iw - 8, y, 6, "^");
    }
    if (infoRowsVisible > 0 && settingsInfoScroll < settingsInfoScrollMax) {
        int bottomIdx = settingsInfoScroll + infoRowsVisible - 1;
        uint16_t bottomBg = ((bottomIdx & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        lcd.setTextColor(COL_TEAL, bottomBg);
        drawClippedText(ix + iw - 8, y + (infoRowsVisible - 1) * SH, 6, "v");
    }

    int closeY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
    drawPanelCloseButton(mx + 3, closeY, TOUCH_BTN_W, TOUCH_BTN_H);

    if (showPanelScrollButtons()) {
        const int cfgBtnW = 52;
        const int cfgBtnGap = 4;
        const int downX = ix + iw - cfgBtnW;
        const int upX   = downX - cfgBtnGap - cfgBtnW;
        uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);

        drawSquirclePill(upX, closeY, cfgBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
        lcd.setFont(UI_BODY_FONT);
        lcd.setTextColor(COL_TEXT_MAIN, fill);
        int tw = lcd.textWidth("Up");
        int tx = upX + max(1, (cfgBtnW - tw) / 2);
        int ty = closeY + max(0, (TOUCH_BTN_H - CHAR_H) / 2);
        drawClippedText(tx, ty, cfgBtnW - (tx - upX) - 1, "Up");
        setSettingsControlRect(SETTINGS_CTL_UP, upX, closeY, cfgBtnW, TOUCH_BTN_H);

        drawSquirclePill(downX, closeY, cfgBtnW, TOUCH_BTN_H, fill, COL_SELECT_ACCENT, false);
        tw = lcd.textWidth("Down");
        tx = downX + max(1, (cfgBtnW - tw) / 2);
        drawClippedText(tx, ty, cfgBtnW - (tx - downX) - 1, "Down");
        setSettingsControlRect(SETTINGS_CTL_DOWN, downX, closeY, cfgBtnW, TOUCH_BTN_H);
    }

    lcd.setFont(UI_BODY_FONT);
    dirtyChat = false;
}

// ── Draw: node detail overlay ────────────────────────────────
static void drawNodeDetail(const NodeEntry *n) {
    drawPanelFrame(0, CHAT_Y, LCD_W, LCD_H - CHAT_Y, COL_PANEL_BG, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);

    const int X = 6;
    int row = 0;

    // Helper: print one row and advance
    char buf[LCD_W / CHAR_W + 2];
    auto pr = [&](uint16_t col, const char *s) {
        int y = CHAT_Y + row * LINE_H;
        uint16_t bg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
        lcd.fillRect(2, y, LCD_W - 4, LINE_H, bg);
        lcd.setTextColor(col, bg);
        drawClippedText(X, y + 1, LCD_W - X - 4, s);
        row++;
    };

    if (!n) {
        pr(TFT_RED, "Node not found");
        pr(COL_TAB_IDLE, "[ESC/Enter] close");
        dirtyChat = false;
        return;
    }

    snprintf(buf, sizeof(buf), "[ !%08x ]", n->nodeId);
    pr(COL_TEAL, buf);
    pr(COL_DIVIDER, "----------------------------------------------");

    // Identity
    snprintf(buf, sizeof(buf), "Long  : %s", n->longName[0] ? n->longName : "(unknown)");
    pr(COL_TEXT_MAIN, buf);
    snprintf(buf, sizeof(buf), "Short : %s", n->shortName[0] ? n->shortName : "----");
    pr(COL_TEXT_MAIN, buf);
    row++;

    // Connectivity
    uint32_t ageSec = (millis() - n->lastHeardMs) / 1000;
    if      (ageSec < 60)
        snprintf(buf, sizeof(buf), "Heard : %us ago",           (unsigned)ageSec);
    else if (ageSec < 3600)
        snprintf(buf, sizeof(buf), "Heard : %um %us ago",       (unsigned)(ageSec/60), (unsigned)(ageSec%60));
    else
        snprintf(buf, sizeof(buf), "Heard : %uh %um ago",       (unsigned)(ageSec/3600), (unsigned)((ageSec%3600)/60));
    pr(COL_TEXT_MAIN, buf);

    snprintf(buf, sizeof(buf), "Hops  : %d   SNR: %+.1f dB", n->hops, n->snr);
    pr(COL_TEXT_MAIN, buf);

    const char *chanName = (n->chanIdx >= 0 && n->chanIdx < MAX_CHANNELS)
                           ? CHANNEL_KEYS[n->chanIdx].name : "?";
    snprintf(buf, sizeof(buf), "Chan  : %s", chanName);
    pr(COL_TEXT_MAIN, buf);
    row++;

    // Position
    if (n->hasPosition) {
        pr(COL_NODE_HOT, "Position");
        float lat = n->latI * 1e-7f;
        float lon = n->lonI * 1e-7f;
        snprintf(buf, sizeof(buf), "Lat   : %.5f %c",
                 lat >= 0 ? lat : -lat, lat >= 0 ? 'N' : 'S');
        pr(COL_TEXT_MAIN, buf);
        snprintf(buf, sizeof(buf), "Lon   : %.5f %c",
                 lon >= 0 ? lon : -lon, lon >= 0 ? 'E' : 'W');
        pr(COL_TEXT_MAIN, buf);
        snprintf(buf, sizeof(buf), "Alt   : %d m", (int)n->alt);
        pr(COL_TEXT_MAIN, buf);
        row++;
    }

    // Telemetry
    if (n->hasTelemetry) {
        pr(COL_NODE_HOT, "Telemetry");
        snprintf(buf, sizeof(buf), "Batt  : %.0f%%  %.2f V", n->battPct, n->voltage);
        pr(COL_TEXT_MAIN, buf);
        row++;
    }

    pr(COL_TAB_IDLE, "[ESC / Enter] close");
    lcd.setFont(UI_BODY_FONT);
    dirtyChat = false;
}

static bool isTextInputView() {
    bool dmNeedsInput = (activeView == CHAN_DM && dmConvOpen && !dmPickerOpen);
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT)
    if (activeView >= 0 && activeView < MESH_CHANNELS) {
        return hwTypingLock || inputLen > 0;
    }
#endif
    return !((activeView == CHAN_ANN)
            || (activeView == CHAN_DM && !dmNeedsInput)
            || (activeView == VIEW_MAP)
            || (activeView == VIEW_NODES)
            || (activeView == VIEW_GPS)
            || (activeView == VIEW_SETTINGS));
}

static bool panelCoversInputArea() {
    if (useCompactKeyboardUi()) {
    return isPanelView(activeView) && !isTextInputView();
    }
    return isPanelView(activeView);
}

static int panelOverlayBottomY() {
    if (useCompactKeyboardUi()) {
    return LCD_H - 1;
    }
    return INPUT_Y + INPUT_H - 1;
}

static void handleKey(char k);

struct NavButtonRect {
    int x;
    int y;
    int w;
    int h;
};

static void navButtonRects(NavButtonRect b[NAV_BTN_COUNT]) {
    const int PAD = 3;
    const int GAP = 4;
    const int count = navButtonCount();
    const int rowH = TOUCH_BTN_H;
    const int rowBottomPad = (activeView == VIEW_MAP) ? 2 : 0;
    const int rowY = INPUT_Y + INPUT_H - rowH - rowBottomPad;
    int bw = TOUCH_BTN_W;
    int x = max(PAD, (LCD_W - (count * bw + (count - 1) * GAP)) / 2);
    if (x + count * bw + (count - 1) * GAP > LCD_W - PAD) {
        bw = (LCD_W - 2 * PAD - (count - 1) * GAP) / count;
        x = PAD;
    }
    for (int i = 0; i < NAV_BTN_COUNT; i++) {
        b[i] = { 0, rowY, 0, rowH };
    }
    for (int i = 0; i < count; i++) {
        b[i] = { x, rowY, bw, rowH };
        x += bw + GAP;
    }
}

static bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh));
}

enum SoftKeyAction : uint8_t {
    SK_NONE = 0,
    SK_CHAR,
    SK_SHIFT,
    SK_SPACE,
    SK_BACKSPACE,
    SK_SEND,
    SK_HIDE,
};

static bool          softKbPressed       = false;
static SoftKeyAction softKbPressedAction = SK_NONE;
static char          softKbPressedChar   = 0;

static bool softKeyboardInputView() {
#if HAS_KEYBOARD
    return false;
#else
    if (activeView >= 0 && activeView < MESH_CHANNELS) return true;
    if (activeView == CHAN_DM && dmConvOpen && !dmPickerOpen) return true;
    return false;
#endif
}

static bool softKeyboardBounds(int &kbX, int &kbY, int &kbW, int &kbH) {
    if (!softKbVisible || !softKeyboardInputView()) return false;
    const int rowGap = 3;
    const int rowH = DEVICE_UI_VERTICAL ? 22 : 20;
    const int rowCount = 5;
    kbH = rowCount * rowH + (rowCount - 1) * rowGap + 8;
    kbX = 2;
    kbW = LCD_W - 4;
    kbY = max(CHAT_Y + 2, INPUT_Y - kbH - 2);
    return true;
}

template <typename Fn>
static void forEachSoftKey(Fn fn) {
    int kbX = 0, kbY = 0, kbW = 0, kbH = 0;
    if (!softKeyboardBounds(kbX, kbY, kbW, kbH)) return;

    static const char *rows[] = {
        "1234567890",
        "qwertyuiop",
        "asdfghjkl'",
        "zxcvbnm,.-",
    };

    const int pad = 4;
    const int gap = 2;
    const int rowGap = 3;
    const int rowH = DEVICE_UI_VERTICAL ? 22 : 20;
    const int innerW = kbW - (pad * 2);

    int y = kbY + 4;

    for (int r = 0; r < 4; r++) {
        const char *line = rows[r];
        int keyCount = (int)strlen(line);
        int keyW = max(12, (innerW - ((keyCount - 1) * gap)) / keyCount);
        int rowW = keyCount * keyW + (keyCount - 1) * gap;
        int x = kbX + pad + max(0, (innerW - rowW) / 2);

        for (int i = 0; i < keyCount; i++) {
            char ch = line[i];
            char label[2] = { ch, '\0' };
            bool upper = softKbShift && ch >= 'a' && ch <= 'z';
            if (upper) label[0] = (char)(ch - ('a' - 'A'));
            fn(x, y, keyW, rowH, SK_CHAR, upper ? label[0] : ch, label, false);
            x += keyW + gap;
        }
        y += rowH + rowGap;
    }

    const int ctlCount = 5;
    const int ctlW = max(26, (innerW - ((ctlCount - 1) * gap)) / ctlCount);
    const int ctlRowW = ctlCount * ctlW + (ctlCount - 1) * gap;
    int cx = kbX + pad + max(0, (innerW - ctlRowW) / 2);

    fn(cx, y, ctlW, rowH, SK_HIDE, 0, "Hide", false);
    cx += ctlW + gap;
    fn(cx, y, ctlW, rowH, SK_SHIFT, 0, softKbShift ? "Shift*" : "Shift", softKbShift);
    cx += ctlW + gap;
    fn(cx, y, ctlW, rowH, SK_SPACE, 0, "Space", false);
    cx += ctlW + gap;
    fn(cx, y, ctlW, rowH, SK_SEND, 0, "Send", false);
    cx += ctlW + gap;
    fn(cx, y, ctlW, rowH, SK_BACKSPACE, 0, "<-", false);
}

static bool softKeyboardKeyMatchesPressed(SoftKeyAction action, char ch) {
    if (!softKbPressed) return false;
    if (action != softKbPressedAction) return false;
    if (action == SK_CHAR) return ch == softKbPressedChar;
    return true;
}

static void softKeyboardClearPressed() {
    if (!softKbPressed) return;
    softKbPressed = false;
    softKbPressedAction = SK_NONE;
    softKbPressedChar = 0;
    dirtyInput = true;
}

static void softKeyboardTrackPress(int x, int y) {
    if (!softKbVisible || !softKeyboardInputView()) {
        softKeyboardClearPressed();
        return;
    }

    bool hit = false;
    SoftKeyAction hitAction = SK_NONE;
    char hitChar = 0;

    forEachSoftKey([&](int rx, int ry, int rw, int rh, SoftKeyAction action, char ch,
                       const char * /*label*/, bool /*active*/) {
        if (hit || !pointInRect(x, y, rx, ry, rw, rh)) return;
        hit = true;
        hitAction = action;
        hitChar = ch;
    });

    if (!hit) {
        softKeyboardClearPressed();
        return;
    }

    if (!softKbPressed || !softKeyboardKeyMatchesPressed(hitAction, hitChar)) {
        softKbPressed = true;
        softKbPressedAction = hitAction;
        softKbPressedChar = hitChar;
        dirtyInput = true;
    }
}

static void drawSoftKeyboardOverlay() {
    int kbX = 0, kbY = 0, kbW = 0, kbH = 0;
    if (!softKeyboardBounds(kbX, kbY, kbW, kbH)) return;

    uint16_t shell = lerp565(COL_PANEL_BG, COL_PANEL_STRONG, 84);
    drawPanelFrame(kbX, kbY, kbW, kbH, shell, COL_SELECT_ACCENT);

    forEachSoftKey([&](int x, int y, int w, int h, SoftKeyAction action, char ch,
                       const char *label, bool active) {
        bool pressed = softKeyboardKeyMatchesPressed(action, ch);
        bool emph = active || pressed;
        uint16_t fill = emph ? COL_SELECT_BG : lerp565(shell, COL_PANEL_ALT, 92);
        uint16_t edge = emph ? COL_SELECT_ACCENT : COL_DIVIDER_HI;
        int lift = pressed ? (DEVICE_UI_VERTICAL ? 4 : 3) : 0;
        int dy = y - lift;
        int dh = h + lift;

        drawSquirclePill(x, dy, w, dh, fill, edge, emph);
        lcd.setFont(UI_BODY_FONT);
        lcd.setTextColor(emph ? COL_TEXT_ON_ACCENT : COL_TEXT_MAIN, fill);
        int tw = lcd.textWidth(label);
        int tx = x + max(1, (w - tw) / 2);
        int ty = dy + max(0, (dh - CHAR_H) / 2);
        drawClippedText(tx, ty, w - (tx - x) - 1, label);
        (void)action;
    });
}

static bool softKeyboardHandleTap(int x, int y) {
    if (!softKbVisible || !softKeyboardInputView()) return false;

    bool consumed = false;
    forEachSoftKey([&](int rx, int ry, int rw, int rh, SoftKeyAction action, char ch,
                       const char * /*label*/, bool /*active*/) {
        if (consumed || !pointInRect(x, y, rx, ry, rw, rh)) return;
        consumed = true;

        switch (action) {
            case SK_CHAR:
                handleKey(ch);
                if (softKbShift && ch >= 'A' && ch <= 'Z') softKbShift = false;
                dirtyInput = true;
                break;
            case SK_SHIFT:
                softKbShift = !softKbShift;
                dirtyInput = true;
                break;
            case SK_SPACE:
                handleKey(' ');
                dirtyInput = true;
                break;
            case SK_BACKSPACE:
                handleKey(KEY_BACKSPACE);
                dirtyInput = true;
                break;
            case SK_SEND:
                handleKey(KEY_ENTER);
                softKbVisible = false;
                softKbShift = false;
                softKeyboardClearPressed();
                dirtyChat = dirtyNodes = dirtyInput = true;
                break;
            case SK_HIDE:
                softKbVisible = false;
                softKbShift = false;
                softKeyboardClearPressed();
                dirtyChat = dirtyNodes = dirtyInput = true;
                break;
            default:
                break;
        }
    });

    return consumed;
}

static void mapClampViewport() {
    mapViewLatSpan = max(MAP_MIN_LAT_SPAN, min(180.0f, mapViewLatSpan));
    mapViewLonSpan = max(MAP_MIN_LON_SPAN, min(360.0f, mapViewLonSpan));

    if (mapViewLatSpan >= 179.9f) {
        mapViewCenterLat = 0.0f;
    } else {
        float half = mapViewLatSpan * 0.5f;
        float minCenter = -90.0f + half;
        float maxCenter = 90.0f - half;
        mapViewCenterLat = max(minCenter, min(maxCenter, mapViewCenterLat));
    }

    if (mapViewLonSpan >= 359.9f) {
        mapViewCenterLon = 0.0f;
    } else {
        float half = mapViewLonSpan * 0.5f;
        float minCenter = -180.0f + half;
        float maxCenter = 180.0f - half;
        mapViewCenterLon = max(minCenter, min(maxCenter, mapViewCenterLon));
    }
}

static void mapStartManualView() {
    if (mapViewManual) return;
    mapViewManual = true;
    mapViewCenterLat = mapLastCenterLat;
    mapViewCenterLon = mapLastCenterLon;
    mapViewLatSpan = mapLastLatSpan;
    mapViewLonSpan = mapLastLonSpan;
    mapClampViewport();
}

static int mapVisibleNodeCount() {
    if (mapNodeFreezeActive) return mapFrozenNodeCount;
    return Nodes.count();
}

static NodeEntry *mapVisibleNodeByIndex(int idx) {
    if (idx < 0) return nullptr;
    if (mapNodeFreezeActive) {
        NodeEntry *self = nullptr;
        int selfFrozenIndex = -1;
        for (int i = 0; i < mapFrozenNodeCount; i++) {
            NodeEntry *n = Nodes.find(mapFrozenNodeIds[i]);
            if (n && n->nodeId == myNodeId) {
                self = n;
                selfFrozenIndex = i;
                break;
            }
        }

        if (self) {
            if (idx == 0) return self;
            idx -= 1;
        }

        for (int i = 0; i < mapFrozenNodeCount; i++) {
            if (i == selfFrozenIndex) continue;
            NodeEntry *n = Nodes.find(mapFrozenNodeIds[i]);
            if (!n) continue;
            if (idx == 0) return n;
            idx -= 1;
        }
        return nullptr;
    }

    NodeEntry *self = Nodes.find(myNodeId);
    if (self) {
        if (idx == 0) return self;
        idx -= 1;
    }

    int cnt = Nodes.count();
    for (int rank = 0; rank < cnt; rank++) {
        NodeEntry *n = Nodes.getByRank(rank);
        if (!n || n->nodeId == myNodeId) continue;
        if (idx == 0) return n;
        idx -= 1;
    }
    return nullptr;
}

static int nodesVisibleNodeCount() {
    if (nodesNodeFreezeActive) return nodesFrozenNodeCount;
    return Nodes.count();
}

static NodeEntry *nodesVisibleNodeByIndex(int idx) {
    if (idx < 0) return nullptr;
    if (nodesNodeFreezeActive) {
        NodeEntry *self = nullptr;
        int selfFrozenIndex = -1;
        for (int i = 0; i < nodesFrozenNodeCount; i++) {
            NodeEntry *n = Nodes.find(nodesFrozenNodeIds[i]);
            if (n && n->nodeId == myNodeId) {
                self = n;
                selfFrozenIndex = i;
                break;
            }
        }

        if (self) {
            if (idx == 0) return self;
            idx -= 1;
        }

        for (int i = 0; i < nodesFrozenNodeCount; i++) {
            if (i == selfFrozenIndex) continue;
            NodeEntry *n = Nodes.find(nodesFrozenNodeIds[i]);
            if (!n) continue;
            if (idx == 0) return n;
            idx -= 1;
        }
        return nullptr;
    }

    NodeEntry *self = Nodes.find(myNodeId);
    if (self) {
        if (idx == 0) return self;
        idx -= 1;
    }

    int cnt = Nodes.count();
    for (int rank = 0; rank < cnt; rank++) {
        NodeEntry *n = Nodes.getByRank(rank);
        if (!n || n->nodeId == myNodeId) continue;
        if (idx == 0) return n;
        idx -= 1;
    }
    return nullptr;
}

static bool mapSelectNodeById(uint32_t nodeId) {
    int cnt = mapVisibleNodeCount();
    for (int i = 0; i < cnt; i++) {
        NodeEntry *n = mapVisibleNodeByIndex(i);
        if (n && n->nodeId == nodeId) {
            mapsListSel = i;
            return true;
        }
    }
    return false;
}

static bool mapCenterOnSelectedNode() {
    NodeEntry *sel = mapVisibleNodeByIndex(mapsListSel);
    if (!sel) return false;

    float lat = 0.0f;
    float lon = 0.0f;
    if (!mapExtractNodeCoords(sel, lat, lon)) return false;

    mapViewManual = true;
    mapViewCenterLat = lat;
    mapViewCenterLon = lon;
    mapClampViewport();
    return true;
}

static void mapApplyControl(MapControlAction action) {
    if (action == MAP_CTL_LIST_PREV) {
        mapsListSel = max(0, mapsListSel - 1);
        mapCenterOnSelectedNode();
        dirtyChat = true;
        return;
    }

    if (action == MAP_CTL_LIST_NEXT) {
        int cap = max(0, mapVisibleNodeCount() - 1);
        mapsListSel = min(cap, mapsListSel + 1);
        mapCenterOnSelectedNode();
        dirtyChat = true;
        return;
    }

    if (action == MAP_CTL_ME) {
        if (mapSelectNodeById(myNodeId)) {
            mapCenterOnSelectedNode();
            dirtyChat = true;
        }
        return;
    }

    mapStartManualView();

    switch (action) {
        case MAP_CTL_ZOOM_IN:
            mapViewLatSpan *= 0.65f;
            mapViewLonSpan *= 0.65f;
            break;
        case MAP_CTL_ZOOM_OUT:
            mapViewLatSpan *= 1.45f;
            mapViewLonSpan *= 1.45f;
            break;
        default:
            break;
    }

    mapClampViewport();
    dirtyChat = true;
}

static bool handleTouchTap(int x, int y) {
    if (screenAsleep || nodeDetailOpen) return false;

    if (softKeyboardHandleTap(x, y)) return true;

    #if !defined(DEVICE_TLORA_PAGER_TFT)
    if (dmNewVisible
        && pointInRect(x, y, dmNewRect.x, dmNewRect.y, dmNewRect.w, dmNewRect.h)
        && activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen) {
        dmListSel  = 0;
        dmPickerSel  = 0;
        dmPickerOpen = true;
        pickerSnapshot();
        dirtyChat = true;
        return true;
    }
    #endif

    if (panelCloseVisible
        && pointInRect(x, y, panelCloseRect.x, panelCloseRect.y,
                       panelCloseRect.w, panelCloseRect.h)) {
        closePanelToChannel();
        return true;
    }

#if defined(DEVICE_HELTEC_V4_EXPANSION) && HAS_TOUCH
    if (activeView == CHAN_DM) {
        if (dmCtlVisible[DM_CTL_UP]
            && pointInRect(x, y,
                           dmCtlRect[DM_CTL_UP].x,
                           dmCtlRect[DM_CTL_UP].y,
                           dmCtlRect[DM_CTL_UP].w,
                           dmCtlRect[DM_CTL_UP].h)) {
            handleKey(KEY_SCROLL_UP);
            return true;
        }
        if (dmCtlVisible[DM_CTL_DOWN]
            && pointInRect(x, y,
                           dmCtlRect[DM_CTL_DOWN].x,
                           dmCtlRect[DM_CTL_DOWN].y,
                           dmCtlRect[DM_CTL_DOWN].w,
                           dmCtlRect[DM_CTL_DOWN].h)) {
            handleKey(KEY_SCROLL_DN);
            return true;
        }
    }

    if (activeView == VIEW_NODES) {
        if (nodesCtlVisible[NODES_CTL_UP]
            && pointInRect(x, y,
                           nodesCtlRect[NODES_CTL_UP].x,
                           nodesCtlRect[NODES_CTL_UP].y,
                           nodesCtlRect[NODES_CTL_UP].w,
                           nodesCtlRect[NODES_CTL_UP].h)) {
            handleKey(KEY_SCROLL_UP);
            return true;
        }
        if (nodesCtlVisible[NODES_CTL_DOWN]
            && pointInRect(x, y,
                           nodesCtlRect[NODES_CTL_DOWN].x,
                           nodesCtlRect[NODES_CTL_DOWN].y,
                           nodesCtlRect[NODES_CTL_DOWN].w,
                           nodesCtlRect[NODES_CTL_DOWN].h)) {
            handleKey(KEY_SCROLL_DN);
            return true;
        }
    }
#endif

    if (activeView == VIEW_MAP) {
        for (int i = 0; i < MAP_CTL_COUNT; i++) {
            if (!mapCtlVisible[i]) continue;
            if (pointInRect(x, y, mapCtlRect[i].x, mapCtlRect[i].y,
                            mapCtlRect[i].w, mapCtlRect[i].h)) {
                mapApplyControl((MapControlAction)i);
                return true;
            }
        }
    }

    if (activeView == VIEW_SETTINGS) {
        if (settingsCtlVisible[SETTINGS_CTL_UP]
            && pointInRect(x, y,
                           settingsCtlRect[SETTINGS_CTL_UP].x,
                           settingsCtlRect[SETTINGS_CTL_UP].y,
                           settingsCtlRect[SETTINGS_CTL_UP].w,
                           settingsCtlRect[SETTINGS_CTL_UP].h)) {
            handleKey(KEY_SCROLL_UP);
            return true;
        }
        if (settingsCtlVisible[SETTINGS_CTL_DOWN]
            && pointInRect(x, y,
                           settingsCtlRect[SETTINGS_CTL_DOWN].x,
                           settingsCtlRect[SETTINGS_CTL_DOWN].y,
                           settingsCtlRect[SETTINGS_CTL_DOWN].w,
                           settingsCtlRect[SETTINGS_CTL_DOWN].h)) {
            handleKey(KEY_SCROLL_DN);
            return true;
        }
    }

#if !HAS_KEYBOARD
    if (!softKbVisible && softKeyboardInputView() && y >= CHAT_Y && y < INPUT_Y) {
        softKbVisible = true;
        softKbShift = false;
        softKeyboardClearPressed();
        dirtyInput = true;
        return true;
    }
#endif

    NavButtonRect b[NAV_BTN_COUNT];
    navButtonRects(b);
    if (!panelCoversInputArea()) {
        for (int i = 0; i < navButtonCount(); i++) {
            if (pointInRect(x, y, b[i].x, b[i].y, b[i].w, b[i].h)) {
                activateNavButton(i);
                return true;
            }
        }
    }
    return false;
}

// ── Draw: input bar ───────────────────────────────────────────
static void drawInput() {
    bool showTextInput = isTextInputView();

    if (softKbVisible && !softKeyboardInputView()) {
        softKbVisible = false;
        softKbShift = false;
        softKeyboardClearPressed();
        dirtyChat = dirtyNodes = true;
    }

    if (panelCoversInputArea()) {
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    if (showTextInput && activeView == CHAN_DM && (inputLen > 0 || hwTypingLock)) {
#if defined(DEVICE_TLORA_PAGER_TFT)
            lcd.setFont(&fonts::DejaVu12);
            lcd.setTextSize(1.0f);
#else
            lcd.setFont(&fonts::DejaVu9);
            lcd.setTextSize(UI_BASE_TEXT_SCALE);
#endif
            const int composerH = lcd.fontHeight() + 6;
            const int composerY = INPUT_Y + 1;
            lcd.fillRect(0, composerY, LCD_W, composerH, COL_INPUT_BG);
            lcd.drawFastHLine(0, composerY, LCD_W, COL_DIVIDER);
            lcd.drawFastHLine(0, composerY + composerH - 1, LCD_W, COL_DIVIDER_HI);

            const int textY = composerY + max(0, (composerH - lcd.fontHeight()) / 2);
            lcd.setTextColor(COL_TEAL, COL_INPUT_BG);
            lcd.drawString(">>", 2, textY);

            int textX = 2 + lcd.textWidth(">>") + 3;
            int availW = LCD_W - textX - 4;
            String visible(inputBuf);
            while (visible.length() > 0 && lcd.textWidth(visible.c_str()) > availW) {
                visible.remove(0, 1);
            }
            lcd.setTextColor(COL_TEXT_MAIN, COL_INPUT_BG);
            lcd.drawString(visible.c_str(), textX, textY);

            if (cursorOn) {
                int cx = textX + lcd.textWidth(visible.c_str()) + 1;
                int ch = min(8, lcd.fontHeight());
                lcd.fillRect(cx, textY, 2, ch, COL_CURSOR);
            }

            lcd.setFont(UI_BODY_FONT);
        }
#endif
#if !HAS_KEYBOARD
        // Panels lock out nav/input bar, but touch-only devices still need
        // the soft keyboard overlay while composing in DM conversations.
        if (softKbVisible && softKeyboardInputView()) {
            drawSoftKeyboardOverlay();
        }
#endif
        dirtyInput = false;
        return;
    }

    NavButtonRect b[NAV_BTN_COUNT];
    navButtonRects(b);

    if (activeView == VIEW_MAP) {
        int navTop = max(INPUT_Y, b[0].y);
        fillVerticalGradient(0, navTop, LCD_W, LCD_H - navTop, COL_INPUT_TOP, COL_INPUT_BG);
        lcd.drawFastHLine(0, navTop, LCD_W, COL_DIVIDER);
        lcd.drawFastHLine(0, INPUT_Y + INPUT_H - 1, LCD_W, COL_DIVIDER);
    } else if (showTextInput) {
        fillVerticalGradient(0, INPUT_Y, LCD_W, INPUT_H, COL_INPUT_TOP, COL_INPUT_BG);
        lcd.drawFastHLine(0, INPUT_Y, LCD_W, COL_DIVIDER);
        lcd.drawFastHLine(0, INPUT_Y + INPUT_H - 1, LCD_W, COL_DIVIDER);
    } else {
        fillVerticalGradient(0, INPUT_Y, LCD_W, INPUT_H, COL_INPUT_TOP, COL_INPUT_BG);
        lcd.drawFastHLine(0, INPUT_Y, LCD_W, COL_DIVIDER);
        lcd.drawFastHLine(0, INPUT_Y + INPUT_H - 1, LCD_W, COL_DIVIDER);
    }
#if defined(DEVICE_TLORA_PAGER_TFT)
    lcd.setFont(&fonts::DejaVu12);
    lcd.setTextSize(1.0f);
#else
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
#endif

    if (showTextInput) {
        int textY = max(INPUT_Y + 2, b[0].y - lcd.fontHeight() - 2);
        int kbX = 0, kbY = 0, kbW = 0, kbH = 0;
        const int composerH = lcd.fontHeight() + 6;
        if (softKeyboardBounds(kbX, kbY, kbW, kbH)) {
            const int composerY = max(CHAT_Y + 1, kbY - composerH - 2);
            lcd.fillRect(0, composerY, LCD_W, composerH, COL_INPUT_BG);
            lcd.drawFastHLine(0, composerY, LCD_W, COL_DIVIDER);
            lcd.drawFastHLine(0, composerY + composerH - 1, LCD_W, COL_DIVIDER_HI);
            textY = composerY + max(0, (composerH - lcd.fontHeight()) / 2);
        } else {
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT)
            const int composerBottom = max(CHAT_Y + composerH, b[0].y - 2);
            const int composerY = max(CHAT_Y + 1, composerBottom - composerH);
            lcd.fillRect(0, composerY, LCD_W, composerH, COL_INPUT_BG);
            lcd.drawFastHLine(0, composerY, LCD_W, COL_DIVIDER);
            lcd.drawFastHLine(0, composerY + composerH - 1, LCD_W, COL_DIVIDER_HI);
            textY = composerY + max(0, (composerH - lcd.fontHeight()) / 2);
#else
            int midY = min(b[0].y - 1, textY + lcd.fontHeight() + 1);
            lcd.drawFastHLine(0, midY, LCD_W, COL_DIVIDER_HI);
#endif
        }

        lcd.setTextColor(COL_TEAL, COL_INPUT_BG);
        lcd.drawString(">>", 2, textY);

        // Show trailing input segment that fits in available pixel width.
        int textX = 2 + lcd.textWidth(">>") + 3;
        int availW = LCD_W - textX - 4;
        String visible(inputBuf);
        while (visible.length() > 0 && lcd.textWidth(visible.c_str()) > availW) {
            visible.remove(0, 1);
        }
        lcd.setTextColor(COL_TEXT_MAIN, COL_INPUT_BG);
        lcd.drawString(visible.c_str(), textX, textY);

        if (cursorOn) {
            int cx = textX + lcd.textWidth(visible.c_str()) + 1;
            int ch = min(8, lcd.fontHeight());
            lcd.fillRect(cx, textY, 2, ch, COL_CURSOR);
        }
    } else {
        // Non-text views keep the top row visually clean.
    }

    uint16_t btnFill = lerp565(COL_INPUT_BG, COL_PANEL_ALT, 80);
    for (int i = 0; i < navButtonCount(); i++) {
        drawSquirclePill(b[i].x, b[i].y, b[i].w, b[i].h, btnFill, COL_TEAL, false);
    }

    if (navButtonCount() == NAV_BTN_COUNT) {
        // Bracket app buttons (DM / MAP / LIVE / CFG / NODES) from outer nav buttons.
        int sepX1 = (b[0].x + b[0].w + b[1].x) / 2;
        int sepX2 = (b[5].x + b[5].w + b[6].x) / 2;
        int sepY  = b[0].y + 1;
        int sepH  = max(1, b[0].h - 2);
        // Stronger divider between "Previous" and the app group.
        lcd.fillRect(max(0, sepX1 - 1), sepY, 3, sepH, COL_SELECT_ACCENT);
        lcd.drawFastVLine(sepX1 + 1, sepY, sepH, COL_DIVIDER_HI);
        // Match right-side bracket style between "CFG" and "Next".
        lcd.fillRect(max(0, sepX2 - 1), sepY, 3, sepH, COL_SELECT_ACCENT);
        lcd.drawFastVLine(sepX2 + 1, sepY, sepH, COL_DIVIDER_HI);
    }

    lcd.setFont(UI_BODY_FONT);
    lcd.setTextColor(COL_TEXT_MAIN, btnFill);
    for (int i = 0; i < navButtonCount(); i++) {
        const char *label = navButtonLabel(i);
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT)
    if (navButtonCount() == 5) {
            char head[2] = { label[0], '\0' };
            const char *tail = label + 1;

            lcd.setFont(&fonts::DejaVu9);
            int headW = lcd.textWidth(head);
            int headH = lcd.fontHeight();
            lcd.setFont(UI_BODY_FONT);
            int tailW = lcd.textWidth(tail);
            int totalW = headW + (tail[0] ? (1 + tailW) : 0);
            int tx = b[i].x + max(1, (b[i].w - totalW) / 2);
            int headY = b[i].y + max(0, (b[i].h - headH) / 2) - 1;
            int tailY = b[i].y + max(0, (b[i].h - CHAR_H) / 2);

            lcd.setFont(&fonts::DejaVu9);
            lcd.setTextColor(COL_TEAL, btnFill);
            lcd.drawString(head, tx, headY);
            lcd.drawString(head, tx + 1, headY);

            if (tail[0]) {
                lcd.setFont(UI_BODY_FONT);
                lcd.setTextColor(COL_TEXT_MAIN, btnFill);
                drawClippedText(tx + headW + 1, tailY,
                                b[i].w - (tx + headW + 1 - b[i].x) - 2, tail);
            }
            lcd.setFont(UI_BODY_FONT);
            continue;
        }
#endif
        int tw = lcd.textWidth(label);
        int tx = b[i].x + max(1, (b[i].w - tw) / 2);
        int ty = b[i].y + max(0, (b[i].h - CHAR_H) / 2);
        if (tw <= b[i].w - 2) lcd.drawString(label, tx, ty);
        else                  drawClippedText(b[i].x + 1, ty, b[i].w - 2, label);
    }

    drawSoftKeyboardOverlay();

    lcd.setFont(UI_BODY_FONT);
    dirtyInput = false;
}

// ── Send routing ACK back to sender ──────────────────────────
static const char *livePortTag(uint32_t portnum) {
    switch (portnum) {
        case TEXT_MESSAGE_APP: return "T";
        case NODEINFO_APP:     return "N";
        case POSITION_APP:     return "P";
        case TELEMETRY_APP:    return "E";
        case ROUTING_APP:      return "A";
        default:               return "D";
    }
}

static void addLiveLine(const char *text, uint16_t color = TFT_DARKGREY) {
    char prefix[12];
    liveBuildPrefix(prefix, sizeof(prefix));
    Channels.addMessage(CHAN_ANN, prefix, text, color);
    dirtyTabs = true;
    if (activeView == CHAN_ANN) dirtyLiveRows = true;
}

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
static bool sPagerAudioInitTried = false;
static bool sPagerAudioReady = false;
static constexpr i2s_port_t kPagerI2SPort = I2S_NUM_0;
static audio_driver::DriverPins sPagerAudioPins;
static audio_driver::AudioBoard sPagerAudioBoard(audio_driver::AudioDriverES8311,
                                                 sPagerAudioPins);

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
        // Splash playback can run before keyboard/I2C init on pager; keep retrying.
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

    sPagerAudioBoard.setVolume(58);
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
    i2s_zero_dma_buffer(kPagerI2SPort);
}

static inline void pagerAudioStopPlayback() {
    // Push a short silence tail before ending to reduce stop pops.
    int16_t tail[128] = {0};
    size_t tailWritten = 0;
    (void)i2s_write(kPagerI2SPort, tail, sizeof(tail), &tailWritten, 20 / portTICK_PERIOD_MS);
    i2s_zero_dma_buffer(kPagerI2SPort);
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
    uint32_t rampFrames = kSampleRate / 400; // ~2.5ms ramp to reduce pops
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

            int16_t v = (int16_t)(s * 2200.0f * env);
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
    static const uint16_t kDurMs[] = {42, 42, 70};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) delay(12);
    }
    pagerAudioStopPlayback();
}

static void pagerAudioPlayChirpyPattern() {
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    static const uint16_t kNotesHz[] = {1047, 1319, 1568, 1319};
    static const uint16_t kDurMs[] = {24, 24, 26, 42};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) delay(8);
    }
    pagerAudioStopPlayback();
}

static void pagerAudioPlayBassPattern() {
    if (!pagerAudioEnsureReady()) return;
    pagerAudioStartPlayback();
    static const uint16_t kNotesHz[] = {392, 330, 262};
    static const uint16_t kDurMs[] = {65, 60, 90};
    static const size_t kNoteCount = sizeof(kNotesHz) / sizeof(kNotesHz[0]);
    for (size_t i = 0; i < kNoteCount; i++) {
        pagerAudioPlayTone(kNotesHz[i], kDurMs[i]);
        if (i + 1 < kNoteCount) delay(14);
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
    if (gCfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;

    switch (gCfg.msgAlertSound) {
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
    if (gCfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;

    switch (gCfg.msgAlertSound) {
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
    if (gCfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;

    switch (gCfg.msgAlertSound) {
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
    if (gCfg.msgAlertSound == MSG_ALERT_SOUND_OFF) return;
    tone(BOARD_BUZZER, 1760, 60);
#endif
}

static void playSplashStartupRiff() {
    if (!gCfg.splashMelodyEnabled) return;

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
        if (i + 1 < kCount) delay(kPause16thMs);
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

static void sendRoutingAck(const MeshPacket &pkt) {
    if (pkt.chanIdx < 0 || pkt.chanIdx >= MAX_CHANNELS) return;

    uint8_t proto[48], cipher[48];
    size_t protoLen = encodeRouting(pkt.hdr.id, myNodeId, 0, proto, sizeof(proto));
    if (protoLen == 0) return;

    const ChannelKey &ck = CHANNEL_KEYS[pkt.chanIdx];
    uint32_t ackId = nextMeshPacketId();
    if (!encryptPayload(ackId, myNodeId, ck.key, ck.keyLen, proto, cipher, protoLen)) return;

    // Calculate hop limit matching Plai/Meshtastic convention:
    // hop_start == hop_limit (fresh packet) so receiver sees "Delivered" not "Acknowledged by another node"
    uint8_t origHopStart = (pkt.hdr.flags >> 5) & 0x07;
    uint8_t origHopLimit = pkt.hdr.flags & 0x07;
    uint8_t hopsUsed = (origHopStart >= origHopLimit) ? (origHopStart - origHopLimit) : MESH_HOP_LIMIT;
    uint8_t ackHops  = ((hopsUsed + 2) <= MESH_HOP_LIMIT) ? (hopsUsed + 2) : MESH_HOP_LIMIT;
    if (origHopStart == 0) ackHops = MESH_HOP_LIMIT;

    uint8_t frame[sizeof(MeshHdr) + 48];
    MeshHdr hdr = {};
    hdr.to      = pkt.hdr.from;
    hdr.from    = myNodeId;
    hdr.id      = ackId;
    hdr.channel = ck.hash;
    hdr.flags   = (ackHops & 0x07) | ((ackHops & 0x07) << 5);  // hop_limit = hop_start
    hdr.relay_node = (uint8_t)(myNodeId & 0xFF);
    memcpy(frame, &hdr, sizeof(hdr));
    memcpy(frame + sizeof(hdr), cipher, protoLen);

    bool txOk = Radio.transmit(frame, sizeof(hdr) + protoLen);
    debugLogAcks("[ack] routing ACK -> !%08X for pkt 0x%08X hops=%u\n",
                 pkt.hdr.from, pkt.hdr.id, ackHops);
    char who[16];
    liveNodeLabel(pkt.hdr.from, who, sizeof(who));
    char live[56];
    snprintf(live, sizeof(live), "T ACK %s %s",
             who, txOk ? "OK" : "ER");
    addLiveLine(live, txOk ? TFT_DARKGREY : TFT_RED);
}

// ── Handle received packet ────────────────────────────────────
// Deferred NODEINFO sends — processed in loop() with pollInput() between each
static const int MAX_DEFERRED = 4;
static uint32_t deferredGreet[MAX_DEFERRED];
static int      deferredCount = 0;

static void queueGreet(uint32_t nodeId) {
    if (deferredCount < MAX_DEFERRED) deferredGreet[deferredCount++] = nodeId;
}

static void handleRx(MeshPacket pkt) {
    // Keep draining the keyboard even while packet handling is busy.
    pumpKeyboardRaw(12, millis());

    if (isDuplicate(pkt.hdr.id)) return;
    if (pkt.hdr.from == myNodeId) return;  // ignore our own relayed/reflected packets
    if (gCfg.ignoreMqtt && (pkt.hdr.flags & 0x10)) return;  // bit 4 = via_mqtt

    bool isBcast = (pkt.hdr.to == 0xFFFFFFFF);

    // Send routing ACK FIRST — before any logging or processing — to get it on the air
    // as fast as possible and beat relay re-broadcasts (which cause "Acknowledged by
    // another node" on the sender's app instead of "Delivered").
    bool isAckOrNakEarly = (pkt.portnum == ROUTING_APP && pkt.requestId != 0);
    if (!isBcast && pkt.hdr.to == myNodeId && (pkt.hdr.flags & 0x08) && pkt.decrypted && !isAckOrNakEarly) {
        sendRoutingAck(pkt);
    }

    pktCount++;
    dirtyStatus = true;

    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    uint8_t hopStart = (pkt.hdr.flags >> 5) & 0x07;

    if (debugMessagesEnabled()) {
        Serial.printf("\n[rx] pkt#%lu rssi=%.1f snr=%.2f len=%u ch=%d\n",
                      pktCount, pkt.rssi, pkt.snr,
                      (unsigned)(sizeof(MeshHdr) + pkt.payloadLen), pkt.chanIdx);
        if (isBcast)
            Serial.printf("[rx] from=!%08x to=BCAST hops=%u/%u flags=0x%02x\n",
                          pkt.hdr.from, hopLimit, hopStart, pkt.hdr.flags);
        else
            Serial.printf("[rx] from=!%08x to=!%08x hops=%u/%u flags=0x%02x\n",
                          pkt.hdr.from, pkt.hdr.to, hopLimit, hopStart, pkt.hdr.flags);
    }

    // Update node DB from every received packet (before portnum switch so hasName is current)
    Nodes.updateFromPacket(pkt);
    dirtyNodes = true;
    pumpKeyboardRaw(8, millis());

    // For unicast PKI DMs (channel=0) that failed channel-key decryption, try PKI decrypt.
    // This requires having previously received a NODEINFO with the sender's public key.
    if (!pkt.decrypted && pkt.hdr.to == myNodeId
            && pkt.hdr.channel == 0 && pkt.rawLen > 12) {
        NodeEntry *snd = Nodes.find(pkt.hdr.from);
        if (snd && snd->hasPubKey) {
            uint8_t pkiPlain[256];
            size_t  pkiLen = 0;
            if (decryptPki(pkt.hdr, pkt.rawCipher, pkt.rawLen, snd->pubKey, pkiPlain, pkiLen)) {
                pkt.decrypted = true;
                pkt.chanIdx   = 0;   // ACKs sent on primary channel
                const uint8_t *payPtr = nullptr; size_t payLen = 0;
                decodeData(pkiPlain, pkiLen, pkt.portnum, payPtr, payLen,
                           pkt.requestId, pkt.wantResponse);
                if (payPtr && payLen <= sizeof(pkt.payload)) {
                    memcpy(pkt.payload, payPtr, payLen);
                    pkt.payloadLen = payLen;
                } else {
                    pkt.payloadLen = 0;
                }
                // Send routing ACK (the early-ACK path at top missed this since it wasn't decrypted yet)
                if ((pkt.hdr.flags & 0x08) && pkt.portnum != ROUTING_APP)
                    sendRoutingAck(pkt);
                if (debugMessagesEnabled()) {
                    Serial.printf("[rx] pki decrypt OK portnum=%lu\n", (unsigned long)pkt.portnum);
                }
            }
        }
    }

    if (!pkt.decrypted) {
        char who[16];
        liveNodeLabel(pkt.hdr.from, who, sizeof(who));
        char live[56];
        snprintf(live, sizeof(live), "R %s ENC %02X",
                 who, pkt.hdr.channel);
        addLiveLine(live, TFT_DARKGREY);
        if (debugMessagesEnabled()) {
            static uint32_t sLastUnknownChanLogMs = 0;
            uint32_t nowMs = millis();
            if (nowMs - sLastUnknownChanLogMs >= 2000) {
                Serial.printf("[rx] encrypted packet on unknown channel\n");
                sLastUnknownChanLogMs = nowMs;
            }
        }
        return;
    }

    {
        char who[16];
        liveNodeLabel(pkt.hdr.from, who, sizeof(who));
        char live[72];
        const char *dst = (pkt.hdr.to == 0xFFFFFFFF) ? "B" :
                          ((pkt.hdr.to == myNodeId) ? "M" : "U");
        snprintf(live, sizeof(live), "R %s>%s %s c%d h%02X",
                 who, dst, livePortTag(pkt.portnum), pkt.chanIdx, pkt.hdr.channel);
        addLiveLine(live, TFT_CYAN);
    }

    // Build time prefix
    char timePrefix[12];
    liveBuildPrefix(timePrefix, sizeof(timePrefix));
    char prefix[24];

    switch (pkt.portnum) {

    case TEXT_MESSAGE_APP: {
        TextMsg tm;
        strncpy(tm.text, (const char *)pkt.payload,
                min(pkt.payloadLen, sizeof(tm.text) - 1));
        tm.text[min(pkt.payloadLen, sizeof(tm.text) - 1)] = '\0';
        if (debugMessagesEnabled()) {
            Serial.printf("[rx] text: \"%s\"\n", tm.text);
        }

        // Sender display: use short name (real or hex fallback set by node DB)
        NodeEntry *n = Nodes.find(pkt.hdr.from);
        const char *sender = (n && n->shortName[0]) ? n->shortName : "????";
        uint16_t incomingNodeColor = (gCfg.uiMode == UI_MODE_LIGHT) ? TFT_DARKGREY : TFT_LIGHTGREY;

        snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, sender);

        if (pkt.hdr.to == myNodeId) {
            // Unicast DM addressed to us
            bool viewing = (activeView == CHAN_DM && dmConvOpen
                            && dmConvNodeId == pkt.hdr.from);
            DMs.addMessage(pkt.hdr.from, sender, prefix, tm.text, incomingNodeColor,
                           /*markUnread=*/!viewing, pkt.chanIdx);
            triggerMessageAlert();
            // If we're on the DM list (not inside a conv), jump straight into this one
            if (activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen) {
                DMs.markRead(pkt.hdr.from);
                dmConvNodeId = pkt.hdr.from;
                dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                if (inputLen == 0) hwTypingLock = true;
#endif
                viewing      = true;
            }
            if (viewing) dirtyChat = dirtyInput = true;
            dirtyTabs = true;
        } else {
            // Broadcast / relay message — goes to channel
            Channels.addMessage(pkt.chanIdx, prefix, tm.text, incomingNodeColor);
            triggerMessageAlert();
            if (!dmPickerOpen) dirtyChat = true;
            dirtyTabs = true;
        }
        break;
    }

    case NODEINFO_APP: {
        UserInfo u;
        decodeUser(pkt.payload, pkt.payloadLen, u);
        Nodes.updateUser(pkt.hdr.from, u);
        if (debugMessagesEnabled()) {
            Serial.printf("[rx] nodeinfo: \"%s\" (%s)%s pubKey=%s\n",
                          u.longName, u.shortName,
                          pkt.wantResponse ? " [want_response]" : "",
                          u.hasPubKey ? "YES(32B)" : "none");
        }

        snprintf(prefix, sizeof(prefix), "%s", timePrefix);
        char info[84];
        snprintf(info, sizeof(info), "%s (%s) identified%s.",
             u.longName, u.shortName,
             u.hasPubKey ? " [PK]" : " [noPK]");
        Channels.addMessage(CHAN_ANN, prefix, info, 0xFD20 /* orange */);
        if (!dmPickerOpen) dirtyChat = true;
        dirtyNodes = dirtyTabs = true;

        // Defer our NODEINFO response so it doesn't block keyboard polling
        if (pkt.wantResponse) queueGreet(pkt.hdr.from);
        break;
    }

    case POSITION_APP: {
        PositionInfo pos;
        decodePosition(pkt.payload, pkt.payloadLen, pos);
        Nodes.updatePosition(pkt.hdr.from, pos);
        if (debugMessagesEnabled() && (pos.latI != 0 || pos.lonI != 0)) {
            Serial.printf("[rx] position: %.5f, %.5f alt:%dm\n",
                          pos.latI * 1e-7f, pos.lonI * 1e-7f, pos.alt);
        }
        dirtyNodes = true;
        break;
    }

    case TELEMETRY_APP: {
        TelemetryInfo tel;
        decodeTelemetry(pkt.payload, pkt.payloadLen, tel);
        if (tel.valid) {
            Nodes.updateTelemetry(pkt.hdr.from, tel);
            if (debugMessagesEnabled()) {
                Serial.printf("[rx] telemetry: bat=%.0f%% %.2fV\n", tel.battPct, tel.voltage);
            }
        }
        dirtyNodes = true;
        break;
    }

    case ROUTING_APP: {
        if (pkt.requestId) {
            // Decode inner Routing proto to check error_reason (field 3, varint)
            uint32_t errorReason = 0;
            {
                size_t i = 0;
                while (i < pkt.payloadLen) {
                    uint64_t tag; i = pbReadVarint(pkt.payload, pkt.payloadLen, i, tag); if (!i) break;
                    uint32_t f = tag >> 3, wt = tag & 7;
                    if (wt == 0) {
                        uint64_t v; i = pbReadVarint(pkt.payload, pkt.payloadLen, i, v); if (!i) break;
                        if (f == 3) { errorReason = (uint32_t)v; break; }
                    } else break;
                }
            }
            bool isAck = (errorReason == 0);
            bool dmRoutingMatched = DMs.handleRoutingResult(pkt.hdr.from, pkt.requestId, errorReason);
            {
                char who[16];
                liveNodeLabel(pkt.hdr.from, who, sizeof(who));
                char live[80];
                if (isAck) {
                    snprintf(live, sizeof(live), "R ACK %s %08X h%02X",
                             who, pkt.requestId, pkt.hdr.channel);
                    addLiveLine(live, TFT_GREEN);
                } else {
                    snprintf(live, sizeof(live), "R NAK %s %08X err%lu h%02X",
                             who, pkt.requestId, (unsigned long)errorReason, pkt.hdr.channel);
                    addLiveLine(live, TFT_RED);
                }
            }
            if (isAck) {
                Channels.setAckStateFrom(pkt.requestId, pkt.hdr.from);
                if (debugAcksEnabled()) {
                    Serial.printf("[rx] ACK for 0x%08X from !%08X\n", pkt.requestId, pkt.hdr.from);
                }
            } else {
                Channels.setAckState(pkt.requestId, DisplayLine::NAKED);
                const char *errName = routingErrorName(errorReason);
                if (errName) {
                    debugLogAcks("[rx] NAK for 0x%08X err=%lu(%s) from !%08X\n",
                                 pkt.requestId, (unsigned long)errorReason, errName, pkt.hdr.from);
                } else {
                    debugLogAcks("[rx] NAK for 0x%08X err=%lu from !%08X\n",
                                 pkt.requestId, (unsigned long)errorReason, pkt.hdr.from);
                }

                // PKI_UNKNOWN_PUBKEY (35): sender doesn't have our public key yet.
                // Respond with a unicast NODEINFO so they can retry with PKI.
                if (errorReason == 35) {
                    debugLogAcks("[nak] PKI_UNKNOWN_PUBKEY - sending NODEINFO to !%08X\n", pkt.hdr.from);
                    Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort, pkt.hdr.from, true);
                    NodeEntry *n = Nodes.find(pkt.hdr.from);
                    if (n) n->lastSentInfoMs = millis();
                }

                // NO_CHANNEL means our last DM channel guess was wrong.
                // Clear sticky channel hints so next send can re-select a channel.
                if (errorReason == 6) {
                    NodeEntry *n = Nodes.find(pkt.hdr.from);
                    if (n) {
                        // For non-DM NO_CHANNEL events, flip PKI suppression when we have
                        // a pubkey to avoid getting stuck on one mode forever.
                        if (!dmRoutingMatched && n->hasPubKey) {
                            n->pkiNoChannel = !n->pkiNoChannel;
                        }

                        // Request peer NODEINFO (unicast + want_response) so pubkey state
                        // refreshes quickly for PKI fallback.
                        uint32_t now = millis();
                        if (now - n->lastSentInfoMs > 5000) {
                            if (Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort,
                                                      pkt.hdr.from, true)) {
                                n->lastSentInfoMs = now;
                            }
                        }
                        // Routing NAKs can arrive on fallback paths; do not learn
                        // channel identity from them. Only clear sticky channel index.
                        n->chanIdx = -1;
                    }
                }

                // Show error in the DM conversation with the NAK sender (not just the open conv)
                DmConv *conv = DMs.find(pkt.hdr.from);
                if (conv) {
                    if (errorReason == 6) {
                        conv->rxChanIdx = -1;
                    }
                    char errMsg[44];
                    if (errName)
                        snprintf(errMsg, sizeof(errMsg), "! NAK %s(%lu)", errName, (unsigned long)errorReason);
                    else
                        snprintf(errMsg, sizeof(errMsg), "! NAK err=%lu", (unsigned long)errorReason);
                    DMs.addMessage(pkt.hdr.from, nullptr, "", errMsg, TFT_RED,
                                   false, -1);
                }
            }
            if (!dmPickerOpen) dirtyChat = true;
        }
        break;
    }

    default:
        if (debugMessagesEnabled()) {
            Serial.printf("[rx] %s port=%lu payload=%u bytes\n",
                          portnumName(pkt.portnum), pkt.portnum,
                          (unsigned)pkt.payloadLen);
        }
        break;
    }

    // Introduce ourselves to this node if:
    //   - we've never heard from them before (!known)
    //   - they're new and unnamed (!known->hasName)
    //   - we haven't sent them our info in the last nodeInfoIntervalS seconds
    //     (covers nodes that missed our boot broadcast or were out of range)
    // Routing ACK was already sent earlier in handleRx, so this TX doesn't delay it.
    // updateFromPacket above already ran, so hasName=true if this was a NODEINFO packet.
    {
        NodeEntry *known = Nodes.find(pkt.hdr.from);
        uint32_t  elapsed = known ? (millis() - known->lastSentInfoMs) : UINT32_MAX;
        bool needGreet = !known || !known->hasName ||
                         elapsed > (uint32_t)gCfg.nodeInfoIntervalS * 1000UL;
        if (needGreet) {
            if (debugMessagesEnabled()) {
                Serial.printf("[nodeinfo] queuing greeting for !%08X (%s, last sent %lums ago)\n",
                              pkt.hdr.from,
                              (known && known->hasName) ? known->shortName : "new",
                              elapsed == UINT32_MAX ? 0UL : (unsigned long)elapsed);
            }
            queueGreet(pkt.hdr.from);
        }
    }

    pumpKeyboardRaw(8, millis());
}

static void onWebCfgSaved();  // forward declaration

static void activateSettingsSelection() {
#if HAS_SD_CARD
    if (settingsSel == SETTING_EXPORT) {
        bool ok = cfgExport(gCfg);
        snprintf(settingsStatus, sizeof(settingsStatus),
                 ok ? "Exported to /camillia/config.yaml" : "Export FAILED (no SD?)");
        dirtyChat = true;
        return;
    }

    if (settingsSel == SETTING_IMPORT) {
        bool ok = cfgImport(gCfg);
        if (ok) {
            onWebCfgSaved();
            snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK - rebooting...");
            dirtyChat = true;
            drawSettings();
            delay(1000);
            ESP.restart();
        } else {
            snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
        }
        dirtyChat = true;
        return;
    }
#endif

    if (settingsSel == SETTING_THEME) {
        uint8_t p = (uint8_t)((uiThemePresetIndex() + 1) % UI_THEME_PRESET_COUNT);
        setUiThemePreset(p);
        applyUiTheme();
        persistUiTheme();
        snprintf(settingsStatus, sizeof(settingsStatus), "Theme: %s", uiThemePresetName(uiThemePresetIndex()));
    } else if (settingsSel == SETTING_ANNOUNCE) {
        webCfgQueueAnnounce();
        snprintf(settingsStatus, sizeof(settingsStatus), "NODEINFO broadcast queued.");
#if CFG_MSG_ALERT_TOGGLE
    } else if (settingsSel == SETTING_MSG_ALERT) {
    gCfg.msgAlertSound = (uint8_t)((gCfg.msgAlertSound + 1) % 4);
        persistMessageAlertSetting();
    triggerMessageAlert(true);  // Preview the newly selected sound profile.
    snprintf(settingsStatus, sizeof(settingsStatus), "Notification sound: %s", msgAlertSoundName(gCfg.msgAlertSound));
#endif
    } else if (settingsSel == SETTING_SPLASH_MELODY) {
        gCfg.splashMelodyEnabled = !gCfg.splashMelodyEnabled;
        persistSplashMelodySetting();
        snprintf(settingsStatus, sizeof(settingsStatus), "Splash melody: %s",
                 gCfg.splashMelodyEnabled ? "On" : "Off");
    } else if (settingsSel == SETTING_CLEAR_MSGS) {
        Channels.clearAllMessages(true);
        DMs.clearAll(true);
        snprintf(settingsStatus, sizeof(settingsStatus), "Messages cleared");
        dirtyTabs = dirtyNodes = dirtyInput = true;
    } else if (settingsSel == SETTING_CLEAR_NODES) {
        Nodes.clearPersisted();
        snprintf(settingsStatus, sizeof(settingsStatus), "Node DB cleared - rebooting...");
        dirtyChat = true;
        drawSettings();
        delay(1000);
        ESP.restart();
        return;
    } else if (settingsSel == SETTING_WEBCFG) {
        if (webCfgRunning()) {
            webCfgEnd();
            snprintf(settingsStatus, sizeof(settingsStatus), "Web server stopped");
        } else {
            bool ok = webCfgBegin(&gCfg, onWebCfgSaved);
            if (ok) {
                if (webCfgIsOnboarding())
                    snprintf(settingsStatus, sizeof(settingsStatus), "Setup: %s", webCfgIP());
                else
                    snprintf(settingsStatus, sizeof(settingsStatus), "Web: %s", webCfgIP());
            } else {
                snprintf(settingsStatus, sizeof(settingsStatus), "Web start FAILED");
            }
        }
    } else if (settingsSel == SETTING_FACTORY_RESET) {
        nvs_flash_erase();
        nvs_flash_init();
        Nodes.clearPersisted();
        sdRmDir("/camillia/dms");
        snprintf(settingsStatus, sizeof(settingsStatus), "Factory reset - rebooting...");
        dirtyChat = true;
        drawSettings();
        delay(1000);
        ESP.restart();
    }
    dirtyChat = true;
}

// ── Handle keyboard input ─────────────────────────────────────
static void handleKey(char k) {
    if (k == KEY_NONE) return;

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    uint8_t rawKey = (uint8_t)k;
    if (rawKey == 0x28 || rawKey == 0x0D) k = KEY_ENTER;
    if (rawKey == 0x29) k = KEY_ESCAPE;
    if (rawKey == 0x2A || rawKey == 0x4C || rawKey == 0x08 || rawKey == 0x7F) k = KEY_BACKSPACE;
    bool inChannelComposeMode = (activeView >= 0 && activeView < MESH_CHANNELS)
                             && (hwTypingLock || inputLen > 0);
    if (inChannelComposeMode && (k == '~' || k == '`')) k = KEY_ESCAPE;
#endif

    k = remapCardputerDirectionalKey(k);

    if (k == KEY_BACKSPACE_HOLD) {
        if (activeView >= 0 && activeView < MESH_CHANNELS
            && (softKbVisible || hwTypingLock || inputLen > 0)) {
            softKbVisible = false;
            hwTypingLock = false;
            inputLen = 0;
            inputBuf[0] = '\0';
            dirtyChat = true;
            dirtyInput = true;
            return;
        }
        if (isPanelView(activeView)) {
            closePanelToChannel();
            return;
        }
        return;
    }

#if defined(DEVICE_TLORA_PAGER_TFT)
    bool tloraChannelView = (activeView >= 0 && activeView < MESH_CHANNELS);
    bool tloraTyping = (hwTypingLock || inputLen > 0);
    bool tloraChannelIdle = tloraChannelView
                         && !nodeDetailOpen
                         && !nodeListFocused
                         && !softKbVisible
                         && !tloraTyping;

    // On the pager wheel, rotate to move between channels when not in panel views.
    // User preference: reverse channel list navigation direction.
    if (tloraChannelIdle
        && !pagerWheelChatScrollMode
        && (k == KEY_SCROLL_UP || k == KEY_SCROLL_DN)) {
        k = (k == KEY_SCROLL_UP) ? KEY_NEXT_CHAN : KEY_PREV_CHAN;
    }

    // Wheel click on a channel toggles chat-history wheel scrolling.
    // One click locks into chat-history scrolling; second click restores
    // wheel channel-tab navigation.
    if (k == KEY_ROLLER && tloraChannelView && !nodeDetailOpen && !nodeListFocused) {
        if (!tloraTyping && !softKbVisible) {
            static uint32_t lastWheelModeToggleMs = 0;
            uint32_t nowMs = millis();
            // readTrackball() already delays click delivery until wheel motion settles.
            // Avoid re-gating on _lastScrollMs here or we drop legitimate clicks.
            if ((nowMs - lastWheelModeToggleMs) >= 180) {
                pagerWheelChatScrollMode = !pagerWheelChatScrollMode;
                lastWheelModeToggleMs = nowMs;
                dirtyChat = true;
            }
        }
        return;
    }

    // Pager preference: reverse wheel direction in panel views.
    if (activeView == VIEW_SETTINGS || activeView == VIEW_MAP
        || activeView == VIEW_NODES || activeView == CHAN_DM) {
        if (k == KEY_SCROLL_UP) k = KEY_SCROLL_DN;
        else if (k == KEY_SCROLL_DN) k = KEY_SCROLL_UP;
    }

#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT)
    if (k == KEY_ESCAPE && activeView >= 0 && activeView < MESH_CHANNELS) {
#if defined(DEVICE_TLORA_PAGER_TFT)
        pagerWheelChatScrollMode = false;
#endif
        hwTypingLock = false;
        inputLen = 0;
        inputBuf[0] = '\0';
        dirtyChat = true;
        dirtyInput = true;
        return;
    }
#endif

    if (k == KEY_ESCAPE && activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen && dmDeleteConfirm) {
        clearDmDeleteConfirm();
        dirtyChat = true;
        return;
    }

#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
    if (cardputerPanelShortcutReady()) {
        switch (k) {
            case '`':
            case '~':
                if (isPanelView(activeView)) closePanelToChannel();
                return;
            case 'd':
            case 'D':
                if (activeView != CHAN_DM) goToView(CHAN_DM);
                return;
            case 'm':
            case 'M':
                if (activeView == VIEW_MAP) mapApplyControl(MAP_CTL_ME);
                else goToView(VIEW_MAP);
                return;
            case 'i':
            case 'I':
                if (activeView == VIEW_MAP) {
                    mapApplyControl(MAP_CTL_ZOOM_IN);
                    return;
                }
                break;
            case 'o':
            case 'O':
                if (activeView == VIEW_MAP) {
                    mapApplyControl(MAP_CTL_ZOOM_OUT);
                    return;
                }
                break;
            case 'n':
            case 'N':
                if (activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen) break;
                if (activeView != VIEW_NODES) goToView(VIEW_NODES);
                return;
            case 'l':
            case 'L':
                if (activeView != CHAN_ANN) goToView(CHAN_ANN);
                return;
            case 'c':
            case 'C':
                if (activeView != VIEW_SETTINGS) goToView(VIEW_SETTINGS);
                return;
            default:
                break;
        }
    }
#endif

    // ALT+E — toggle node list focus / close detail; close DM sub-views
    if (k == KEY_NODE_FOCUS) {
        if (activeView == CHAN_DM) {
            clearDmDeleteConfirm();
            if (dmPickerOpen) { dmPickerOpen = false; dirtyChat = true; }
            else if (dmConvOpen) { dmConvOpen = false; dirtyChat = dirtyInput = true; }
            return;
        }
        if (nodeDetailOpen) {
            nodeDetailOpen = false;
            dirtyChat = dirtyNodes = dirtyInput = true;
        } else if (nodeListFocused) {
            nodeListFocused = false;
            dirtyNodes = true;
        } else if (activeView < MAX_CHANNELS) {
            nodeListFocused = true;
            nodeListSel = 0;
            dirtyNodes = true;
        }
        return;
    }

    if (k == KEY_ENTER) {
        bool wasTyping = hwTypingLock || inputLen > 0;
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
        if (activeView >= 0 && activeView < MESH_CHANNELS && !wasTyping) {
            hwTypingLock = true;
            dirtyInput = true;
            return;
        }
#endif
        hwTypingLock = false;
        if (activeView == VIEW_SETTINGS) {
            Serial.printf("[cfg-enter] KEY_ENTER activeView=%d settingsSel=%d\n",
                          activeView, settingsSel);
            activateSettingsSelection();
            return;
        }
        if (activeView == CHAN_DM) {
            if (dmPickerOpen) {
                clearDmDeleteConfirm();
                openDmWith(pickerNode(dmPickerSel));
            } else if (!dmConvOpen) {
                if (dmDeleteConfirm) {
                    clearDmDeleteConfirm();
                    dirtyChat = true;
                    return;
                }
#if defined(DEVICE_TLORA_PAGER_TFT)
                if (DMs.count() <= 0) {
                    dmPickerSel  = 0;
                    dmPickerOpen = true;
                    pickerSnapshot();
                    clearDmDeleteConfirm();
                    dirtyChat = true;
                } else {
                    DmConv *c = DMs.getByRank(dmListSel);
                    if (c) {
                        clearDmDeleteConfirm();
                        DMs.markRead(c->nodeId);
                        dmConvNodeId = c->nodeId;
                        dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                        if (inputLen == 0) hwTypingLock = true;
#endif
                        dirtyChat = dirtyInput = dirtyTabs = true;
                    }
                }
#else
                if (dmListSel == 0) {
                    // "New DM" button
                    dmPickerSel  = 0;
                    dmPickerOpen = true;
                    pickerSnapshot();
                    clearDmDeleteConfirm();
                    dirtyChat = true;
                } else {
                    DmConv *c = DMs.getByRank(dmListSel - 1);
                    if (c) {
                        clearDmDeleteConfirm();
                        DMs.markRead(c->nodeId);
                        dmConvNodeId = c->nodeId;
                        dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                        if (inputLen == 0) hwTypingLock = true;
#endif
                        dirtyChat = dirtyInput = dirtyTabs = true;
                    }
                }
#endif
            } else {
                // Conv view: ENTER sends the message
                if (inputLen > 0) {
                    inputBuf[inputLen] = '\0';
                    bool sentOk = DMs.sendDm(myNodeId, dmConvNodeId, inputBuf);
                    if (!sentOk) {
                        // TX failed — add a local error line so the user knows
                        DMs.addMessage(dmConvNodeId, nullptr, "", "! TX failed", TFT_RED);
                        hwTypingLock = true;
                        dirtyInput = dirtyChat = true;
                        return;
                    }
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                    hwTypingLock = true;
#else
                    hwTypingLock = false;
#endif
                    inputLen = 0; inputBuf[0] = '\0';
                    dirtyInput = dirtyChat = true;
                }
            }
            return;
        }
        if (nodeDetailOpen) {
            nodeDetailOpen = false;
            dirtyChat = dirtyNodes = dirtyInput = true;
            return;
        }
        if (nodeListFocused) {
            NodeEntry *n = Nodes.getByRank(nodeListSel);
            if (n) { nodeDetailId = n->nodeId; nodeDetailOpen = true; dirtyChat = true; }
            return;
        }
        if (activeView == VIEW_MAP) {
            NodeEntry *n = mapVisibleNodeByIndex(mapsListSel);
            if (n) { nodeDetailId = n->nodeId; nodeDetailOpen = true; dirtyChat = true; }
            return;
        }
        if (activeView == VIEW_NODES) {
            dirtyChat = true;
            return;
        }
        if (inputLen > 0 && activeView != CHAN_ANN && activeView != CHAN_DM
               && activeView != VIEW_MAP && activeView != VIEW_GPS
               && activeView != VIEW_SETTINGS && activeView != VIEW_NODES) {
            inputBuf[inputLen] = '\0';
            int txChan = (activeView >= 0 && activeView < MESH_CHANNELS)
                         ? activeView : Channels.activeIdx();
            const char *txText = inputBuf;
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
            if (strcmp(inputBuf, "test") == 0) {
                // Temporary multiline ACK-color validation payload.
                txText = "DEBUG TEST STREAM: ax nebula juxtaposition quark micro "
                         "hyperdimensional antidisestablish ion zephyr transubstantiate "
                         "pebble xylophone umbra chlorofluoro gargantuan pseudohypopara "
                         "counterrevolution electroencephalo titan nano q.";
            }
#endif
            bool sentOk = Channels.sendText(myNodeId, txText, gCfg.okToMqtt, txChan);
            if (!sentOk) {
                Channels.addMessage(txChan, "",
                    "! TX failed", TFT_RED, 0);
                hwTypingLock = true;
                dirtyChat = true;
                dirtyInput = true;
                return;
            }
            hwTypingLock = false;
            inputLen = 0; inputBuf[0] = '\0';
            dirtyInput = dirtyChat = true;
        }

    } else if (k == KEY_BACKSPACE) {
        if (activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen) {
            DmConv *selected = selectedDmListConv();
            if (!selected) {
                clearDmDeleteConfirm();
                dirtyChat = true;
                return;
            }
            if (dmDeleteConfirm && dmDeleteConfirmNodeId == selected->nodeId) {
                if (DMs.deleteConversation(selected->nodeId)) {
#if defined(DEVICE_TLORA_PAGER_TFT)
                    int cap = max(0, DMs.count() - 1);
#else
                    int cap = DMs.count();
#endif
                    dmListSel = min(dmListSel, cap);
                }
                clearDmDeleteConfirm();
                dirtyChat = dirtyTabs = true;
                return;
            }
            dmDeleteConfirm = true;
            dmDeleteConfirmNodeId = selected->nodeId;
            dirtyChat = true;
            return;
        }
        if (activeView == CHAN_DM && dmPickerOpen) {
            if (dmPickerFilterLen > 0) {
                dmPickerFilter[--dmPickerFilterLen] = '\0';
                pickerApplyFilter();
                dirtyChat = true;
            }
            return;
        }
        bool textAllowed = (activeView != CHAN_ANN && activeView != VIEW_SETTINGS
                            && activeView != VIEW_MAP
                            && activeView != VIEW_NODES
                            && activeView != VIEW_GPS
                            && !(activeView == CHAN_DM && (!dmConvOpen || dmPickerOpen)));
        if (inputLen > 0 && textAllowed) {
            inputBuf[--inputLen] = '\0'; dirtyInput = true;
            if (inputLen == 0) hwTypingLock = false;
        } else if (textAllowed && inputLen == 0 && hwTypingLock) {
            // Backspace on an empty active prompt exits compose mode.
            hwTypingLock = false;
            dirtyInput = true;
            if (isPanelView(activeView)) dirtyChat = true;
        }

    } else if (k == KEY_NEXT_CHAN) {
        if (activeView >= 0 && activeView < MESH_CHANNELS
            && (softKbVisible || hwTypingLock || inputLen > 0)) {
            return;
        }
        if (cardputerChannelNavReady()) {
            goToView(nextMeshChannelView(activeView));
            return;
        }

    } else if (k == KEY_PREV_CHAN) {
        if (activeView >= 0 && activeView < MESH_CHANNELS
            && (softKbVisible || hwTypingLock || inputLen > 0)) {
            return;
        }
        if (cardputerChannelNavReady()) {
            goToView(prevMeshChannelView(activeView));
            return;
        }

    } else if (k == KEY_TAB || k == KEY_ROLLER || k == KEY_NEXT_CHAN) {
        if (activeView == CHAN_DM) {
            if (dmPickerOpen) {
                if (k == KEY_ROLLER) {
                    clearDmDeleteConfirm();
                    openDmWith(pickerNode(dmPickerSel));
                } else {
                    // Tab/right closes picker, back to list
                    clearDmDeleteConfirm();
                    dmPickerOpen = false;
                    dirtyChat = true;
                }
            } else if (dmConvOpen) {
                if (k == KEY_ROLLER && inputLen > 0) {
                    // Trackball click with text typed → send (same as Enter)
                    inputBuf[inputLen] = '\0';
                    bool sentOk = DMs.sendDm(myNodeId, dmConvNodeId, inputBuf);
                    if (!sentOk) {
                        DMs.addMessage(dmConvNodeId, nullptr, "", "! TX failed", TFT_RED);
                        hwTypingLock = true;
                        dirtyInput = dirtyChat = true;
                        return;
                    }
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                    hwTypingLock = true;
#else
                    hwTypingLock = false;
#endif
                    inputLen = 0; inputBuf[0] = '\0';
                    dirtyInput = dirtyChat = true;
                } else {
                    // Tab/right/roller (empty) → back to contact list
                    clearDmDeleteConfirm();
                    dmConvOpen = false;
                    dirtyChat = dirtyInput = true;
                }
            } else if (k == KEY_ROLLER) {
                // Roller on list item.
#if defined(DEVICE_TLORA_PAGER_TFT)
                if (DMs.count() <= 0) {
                    dmPickerSel  = 0;
                    dmPickerOpen = true;
                    pickerSnapshot();
                    clearDmDeleteConfirm();
                    dirtyChat = true;
                } else {
                    DmConv *c = DMs.getByRank(dmListSel);
                    if (c) {
                        clearDmDeleteConfirm();
                        DMs.markRead(c->nodeId);
                        dmConvNodeId = c->nodeId;
                        dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                        if (inputLen == 0) hwTypingLock = true;
#endif
                        dirtyChat = dirtyInput = dirtyTabs = true;
                    }
                }
#else
                if (dmListSel == 0) {
                    dmPickerSel  = 0;
                    dmPickerOpen = true;
                    pickerSnapshot();
                    clearDmDeleteConfirm();
                    dirtyChat = true;
                } else {
                    DmConv *c = DMs.getByRank(dmListSel - 1);
                    if (c) {
                        clearDmDeleteConfirm();
                        DMs.markRead(c->nodeId);
                        dmConvNodeId = c->nodeId;
                        dmConvOpen   = true;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                        if (inputLen == 0) hwTypingLock = true;
#endif
                        dirtyChat = dirtyInput = dirtyTabs = true;
                    }
                }
#endif
            } else {
                // Tab/right from list → cycle forward
                clearDmDeleteConfirm();
                goToView(nextView(activeView));
            }
            return;
        }
        if (nodeDetailOpen && k == KEY_ROLLER) {
            nodeDetailOpen = false;
            dirtyChat = dirtyNodes = dirtyInput = true;
        } else if (nodeListFocused && k == KEY_ROLLER) {
            NodeEntry *n = Nodes.getByRank(nodeListSel);
            if (n) { nodeDetailId = n->nodeId; nodeDetailOpen = true; dirtyChat = true; }
        } else if (activeView == VIEW_SETTINGS && k == KEY_ROLLER) {
            activateSettingsSelection();
        } else if (activeView != VIEW_SETTINGS || settingsSel == 0) {
            goToView(nextView(activeView));
        }

    } else if (k == KEY_PREV_CHAN) {
        if (activeView == CHAN_DM) {
            if      (dmPickerOpen) { clearDmDeleteConfirm(); dmPickerOpen = false; dirtyChat = true; return; }
            else if (dmConvOpen)   { clearDmDeleteConfirm(); dmConvOpen = false; dirtyChat = dirtyInput = true; return; }
            // else: fall through to tab cycle
        }
        if (activeView != VIEW_SETTINGS || settingsSel == 0)
            goToView(prevView(activeView));

    } else if (k == KEY_SCROLL_UP) {
        if (activeView == CHAN_DM) {
            if (dmPickerOpen) {
                dmPickerSel = max(0, dmPickerSel - 1);
                dirtyChat = true;
            } else if (dmConvOpen) {
                DmConv *c = DMs.find(dmConvNodeId);
                if (c) {
                    int total = (c->count < MAX_DM_LINES) ? c->count : MAX_DM_LINES;
                    int maxOff = total - dmConvMessageRowsVisible();
                    c->scrollOff = min(c->scrollOff + 3, maxOff > 0 ? maxOff : 0);
                    dirtyChat = true;
                }
            } else {
                // List rows
                clearDmDeleteConfirm();
                dmListSel = max(0, dmListSel - 1);
                dirtyChat = true;
            }
            return;
        }
        if (nodeDetailOpen) {
            /* no scroll in detail view */
        } else if (nodeListFocused) {
            nodeListSel = max(0, nodeListSel - 1);
            dirtyNodes = true;
        } else if (activeView == VIEW_MAP) {
            mapApplyControl(MAP_CTL_LIST_PREV);
        } else if (activeView == VIEW_NODES) {
            nodesListSel = max(0, nodesListSel - 1);
            dirtyChat = true;
        } else if (activeView == VIEW_SETTINGS) {
            if (settingsSel == NUM_SETTINGS - 1 && settingsInfoScroll > 0) settingsInfoScroll--;
            else if (settingsSel > 0) settingsSel--;
            else if (settingsInfoScroll > 0) settingsInfoScroll--;
            dirtyChat = true;
        } else if (activeView < MAX_CHANNELS && activeView != CHAN_DM) {
            Channel &ch = Channels.get(activeView);
            int stored = min(ch.count, MAX_MSG_LINES);
            int maxOff = max(0, stored - VISIBLE_LINES);
            ch.scrollOff = min(ch.scrollOff + 3, maxOff);
            dirtyChat = true;
        }

    } else if (k == KEY_SCROLL_DN) {
        if (activeView == CHAN_DM) {
            if (dmPickerOpen) {
                int cap = max(0, pickerNodeCount() - 1);
                dmPickerSel = min(cap, dmPickerSel + 1);
                dirtyChat = true;
            } else if (dmConvOpen) {
                DmConv *c = DMs.find(dmConvNodeId);
                if (c) { c->scrollOff = max(0, c->scrollOff - 3); dirtyChat = true; }
            } else {
#if defined(DEVICE_TLORA_PAGER_TFT)
                int cap = max(0, DMs.count() - 1);
#else
                int cap = DMs.count();
#endif
                clearDmDeleteConfirm();
                dmListSel = min(cap, dmListSel + 1);
                dirtyChat = true;
            }
            return;
        }
        if (nodeDetailOpen) {
            /* no scroll in detail view */
        } else if (nodeListFocused) {
            int cap = max(0, Nodes.count() - 1);
            nodeListSel = min(cap, nodeListSel + 1);
            dirtyNodes = true;
        } else if (activeView == VIEW_MAP) {
            mapApplyControl(MAP_CTL_LIST_NEXT);
        } else if (activeView == VIEW_NODES) {
            int cap = max(0, nodesVisibleNodeCount() - 1);
            nodesListSel = min(cap, nodesListSel + 1);
            dirtyChat = true;
        } else if (activeView == VIEW_SETTINGS) {
            if (settingsSel < NUM_SETTINGS - 1) settingsSel++;
            else if (settingsInfoScroll < settingsInfoScrollMax) settingsInfoScroll++;
            dirtyChat = true;
        } else if (activeView < MAX_CHANNELS && activeView != CHAN_DM) {
            Channel &ch = Channels.get(activeView);
            ch.scrollOff = max(0, ch.scrollOff - 3);
            dirtyChat = true;
        }

    } else if (k == KEY_PAGE_UP) {
        if (activeView == VIEW_MAP) {
            mapApplyControl(MAP_CTL_ZOOM_IN);
        } else if (activeView < MAX_CHANNELS) {
            Channel &ch = Channels.get(activeView);
            int stored = min(ch.count, MAX_MSG_LINES);
            int maxOff = max(0, stored - VISIBLE_LINES);
            ch.scrollOff = min(ch.scrollOff + VISIBLE_LINES, maxOff);
            dirtyChat = true;
        }

    } else if (k == KEY_PAGE_DN) {
        if (activeView == VIEW_MAP) {
            mapApplyControl(MAP_CTL_ZOOM_OUT);
        } else if (activeView < MAX_CHANNELS) {
            Channel &ch = Channels.get(activeView);
            ch.scrollOff = 0;
            dirtyChat = true;
        }

    } else if (k >= 0x20 && k < 0x7F) {
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT)
        if (activeView >= 0 && activeView < MESH_CHANNELS
            && !dmPickerOpen
            && !(hwTypingLock || inputLen > 0)) {
            return;
        }
#endif
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
        if (activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen
            && (k == 'n' || k == 'N')) {
            clearDmDeleteConfirm();
            dmPickerSel  = 0;
            dmPickerOpen = true;
            pickerSnapshot();
            dirtyChat = true;
            return;
        }
#endif
        if (activeView == CHAN_DM && dmPickerOpen) {
            if (dmPickerFilterLen < DM_PICKER_FILTER_MAX) {
                dmPickerFilter[dmPickerFilterLen++] = k;
                dmPickerFilter[dmPickerFilterLen] = '\0';
                pickerApplyFilter();
                dirtyChat = true;
            }
            return;
        }
        bool textAllowed = (activeView != CHAN_ANN && activeView != VIEW_SETTINGS
                            && activeView != VIEW_MAP
                            && activeView != VIEW_NODES
                            && activeView != VIEW_GPS
                            && !(activeView == CHAN_DM && (!dmConvOpen || dmPickerOpen)));
        if (inputLen < MAX_INPUT_LEN && textAllowed) {
            inputBuf[inputLen++] = k;
            inputBuf[inputLen]   = '\0';
            if (!softKbVisible) hwTypingLock = true;
            dirtyInput = true;
        }
    }
}

// ── Web config save callback ──────────────────────────────────
static void onWebCfgSaved() {
    Serial.printf("[cfg] onWebCfgSaved: long='%s' short='%s'\n", gCfg.nodeLong, gCfg.nodeShort);

    // Erase the entire NVS partition to reclaim all pages.
    // Preferences.clear() only tombstones keys — doesn't free pages.
    // nvs_flash_erase() is the only way to truly defragment on ESP32.
    // This wipes ALL namespaces (camillia, mesh_ch, nodes) so we must
    // rewrite everything below.
    nvs_flash_erase();
    nvs_flash_init();
    Serial.println("[cfg] NVS partition erased");

    // ── Write main config ────────────────────────────────────
    const char *wifiSsid = webCfgWifiSsid();
    const char *wifiPass = webCfgWifiPass();
    if ((!wifiSsid || !wifiSsid[0]) && gCfg.wifiSsid[0]) wifiSsid = gCfg.wifiSsid;
    if ((!wifiPass || !wifiPass[0]) && gCfg.wifiPass[0]) wifiPass = gCfg.wifiPass;

    Preferences p; p.begin("camillia", false);
    // Re-save identity + WiFi keys wiped by clear()
    p.putUInt("nodeId",       myNodeId);
    p.putInt("pkiVer",        2);
    p.putBytes("privKey",     myPrivKey, 32);
    p.putBytes("pubKey",      myPubKey,  32);
    if (wifiSsid && wifiSsid[0]) p.putString("wifiSsid", wifiSsid);
    if (wifiPass && wifiPass[0]) p.putString("wifiPass", wifiPass);
    p.putString("nodeLong",   gCfg.nodeLong);
    p.putString("nodeShort",  gCfg.nodeShort);
    p.putFloat("loraFreq",    gCfg.loraFreq);
    p.putFloat("loraBw",      gCfg.loraBw);
    p.putUChar("loraSf",      gCfg.loraSf);
    p.putUChar("loraCr",      gCfg.loraCr);
    p.putUChar("loraPower",   gCfg.loraPower);
    p.putUChar("loraHopLim",  gCfg.loraHopLimit);
    p.putBool("gpsEnabled",   gCfg.gpsEnabled);
    p.putInt("latI",          gCfg.latI);
    p.putInt("lonI",          gCfg.lonI);
    p.putInt("alt",           gCfg.alt);
    p.putUChar("devRole",     gCfg.deviceRole);
    p.putUChar("rebroadcast", gCfg.rebroadcastMode);
    p.putBool("okToMqtt",    gCfg.okToMqtt);
    p.putBool("ignoreMqtt",  gCfg.ignoreMqtt);
    p.putULong("nodeInfoIntv",gCfg.nodeInfoIntervalS);
    p.putULong("posIntv",     gCfg.posIntervalS);
    p.putULong("gpsPollS",    gCfg.gpsPollIntervalS);
    p.putString("region",     gCfg.region);
    p.putString("tzDef",      gCfg.tzDef);
    p.putString("ntpServer",  gCfg.ntpServer);
    p.putULong("screenOnSecs",gCfg.screenOnSecs);
    p.putUChar("dispUnits",   gCfg.displayUnits);
    p.putBool("compassNorth", gCfg.compassNorthTop);
    p.putBool("flipScreen",   gCfg.flipScreen);
    p.putBool("splashMelody", gCfg.splashMelodyEnabled);
    p.putUChar("msgAlertSound", gCfg.msgAlertSound);
    p.putUChar("uiTheme",     gCfg.uiTheme);
    p.putUChar("uiMode",      gCfg.uiMode);
    p.putBool("btEnabled",    gCfg.btEnabled);
    p.putUChar("btMode",      gCfg.btMode);
    p.putULong("btFixedPin",  gCfg.btFixedPin);
    p.putBool("mqttEnabled",  gCfg.mqttEnabled);
    p.putString("mqttServer", gCfg.mqttServer);
    p.putString("mqttUser",   gCfg.mqttUser);
    p.putString("mqttPass",   gCfg.mqttPass);
    p.putString("mqttRoot",   gCfg.mqttRoot);
    p.putBool("mqttEncrypt",  gCfg.mqttEncryption);
    p.putBool("mqttMapRpt",   gCfg.mqttMapReport);
    p.putBool("isPwrSaving",  gCfg.isPowerSaving);
    p.putULong("lsSecs",      gCfg.lsSecs);
    p.putULong("minWakeSecs", gCfg.minWakeSecs);
    p.putBool("telDevEn",     gCfg.telDeviceEnabled);
    p.putULong("telDevIntv",  gCfg.telDeviceIntervalS);
    p.putBool("telEnvEn",     gCfg.telEnvEnabled);
    p.putULong("telEnvIntv",  gCfg.telEnvIntervalS);
    p.putBool("cannedEn",     gCfg.cannedEnabled);
    p.putString("cannedMsgs", gCfg.cannedMessages);
    p.putULong("nodeIdOvr",   gCfg.nodeIdOverride);
    p.putUChar("chatSpace",   gCfg.chatSpacing);
    p.putBool("dbgAcks",      gCfg.debugAcks);
    p.putBool("dbgMsgs",      gCfg.debugMessages);
    p.putBool("dbgGps",       gCfg.debugGps);
    p.end();
    Serial.println("[cfg] main config saved to NVS");

    // ── Write channel config ─────────────────────────────────
    {
        Preferences cp; cp.begin("mesh_ch", false);
        for (int i = 0; i < MESH_CHANNELS; i++) {
            const char *nm = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
            char k[8];
            snprintf(k, sizeof(k), "n%d", i);  cp.putString(k, nm);
            snprintf(k, sizeof(k), "k%d", i);  cp.putBytes(k, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
            snprintf(k, sizeof(k), "r%d", i);  cp.putUChar(k, CHANNEL_KEYS[i].role);
            Serial.printf("[cfg] ch%d save: name='%s' keyLen=%u role=%u\n",
                          i, nm, CHANNEL_KEYS[i].keyLen, CHANNEL_KEYS[i].role);
        }
        cp.end();
    }
    Serial.println("[cfg] channels saved to NVS");
    // Re-save node database (wiped by nvs_flash_erase)
    Nodes.saveAll();
    Serial.println("[cfg] nodes re-saved to NVS");
    debugSetFlags(gCfg.debugAcks, gCfg.debugMessages, gCfg.debugGps);
    applyTimezoneFromConfig();
    gNtpConfigured = false;
    gNtpServerActive[0] = '\0';
    gNtpLastConfigureMs = 0;
    // Apply GPS enable/disable immediately
    gpsSetEnabled(gCfg.gpsEnabled);
    // Apply LoRa changes immediately
    if (!Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw, gCfg.loraSf, gCfg.loraCr, gCfg.loraPower)) {
        Serial.println("[cfg] WARNING: LoRa reconfigure failed after web save");
        Channels.addMessage(CHAN_ANN, "", "! LoRa reconfigure failed", TFT_RED);
    }
    // Apply node ID override immediately (no reboot needed)
    if (gCfg.nodeIdOverride != 0) myNodeId = gCfg.nodeIdOverride;
    // Apply updated display theme immediately.
    applyUiTheme();
    // Broadcast updated node identity
    NodeEntry *me = Nodes.upsert(myNodeId);
    strncpy(me->longName,  gCfg.nodeLong,  sizeof(me->longName)  - 1);
    strncpy(me->shortName, gCfg.nodeShort, sizeof(me->shortName) - 1);
    Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
    dirtyStatus = dirtyNodes = true;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    // Ensure NVS is usable — if the partition layout changed (e.g. after
    // a partition table update), the old data is garbage and init will fail.
    // Erase and reinit so we don't boot-loop.
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND
        || nvsErr != ESP_OK) {
        Serial.printf("[nvs] init failed (0x%x), erasing partition...\n", nvsErr);
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Load or generate stable node ID + names from NVS
    {
        Preferences prefs;
        prefs.begin("camillia", false);
        myNodeId = prefs.getUInt("nodeId", 0);
        if (myNodeId == 0) {
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            myNodeId = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                       ((uint32_t)mac[4] <<  8) |  (uint32_t)mac[5];
            prefs.putUInt("nodeId", myNodeId);
            Serial.printf("[camillia-mt] Node ID generated and saved: !%08x\n", myNodeId);
        } else {
            Serial.printf("[camillia-mt] Node ID loaded from NVS: !%08x\n", myNodeId);
        }
        prefs.end();
    }

    // Load or generate Curve25519 key pair for PKI DMs
    {
        Preferences prefs;
        prefs.begin("camillia", false);
        // pkiVer==2 means keys are stored in little-endian (wire format).
        // Any other value means old big-endian keys — clear and regenerate.
        bool haveKeys = (prefs.getInt("pkiVer", 0) == 2) &&
                        (prefs.getBytes("privKey", myPrivKey, 32) == 32) &&
                        (prefs.getBytes("pubKey",  myPubKey,  32) == 32);
        if (!haveKeys) {
            prefs.remove("privKey");
            prefs.remove("pubKey");

            mbedtls_ecp_group grp;
            mbedtls_mpi      d;
            mbedtls_ecp_point Q;
            mbedtls_ecp_group_init(&grp);
            mbedtls_mpi_init(&d);
            mbedtls_ecp_point_init(&Q);

            auto rng = [](void *, uint8_t *buf, size_t len) -> int {
                esp_fill_random(buf, len); return 0;
            };

            uint8_t privBuf[32], pubBuf[32];
            if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519) == 0 &&
                mbedtls_ecp_gen_keypair(&grp, &d, &Q, rng, nullptr) == 0 &&
                mbedtls_mpi_write_binary(&d, privBuf, 32) == 0 &&
                mbedtls_mpi_write_binary(&Q.X, pubBuf, 32) == 0) {
                // Store as little-endian (Curve25519 wire format)
                for (int i = 0; i < 32; i++) {
                    myPrivKey[i] = privBuf[31-i];
                    myPubKey[i]  = pubBuf[31-i];
                }
                prefs.putBytes("privKey", myPrivKey, 32);
                prefs.putBytes("pubKey",  myPubKey,  32);
                prefs.putInt("pkiVer", 2);
                Serial.printf("[pki] generated new Curve25519 key pair (LE)\n");
            } else {
                Serial.printf("[pki] WARNING: key generation failed\n");
            }

            mbedtls_ecp_point_free(&Q);
            mbedtls_mpi_free(&d);
            mbedtls_ecp_group_free(&grp);
        } else {
            Serial.printf("[pki] loaded Curve25519 key pair from NVS\n");
        }
        prefs.end();
    }

    // Load runtime config defaults, then overlay anything saved to NVS
    cfgInitDefaults(gCfg);
    {
        Preferences prefs;
        prefs.begin("camillia", true);
        String ln = prefs.getString("nodeLong",  "");
        String sn = prefs.getString("nodeShort", "");
        Serial.printf("[cfg] NVS load: nodeLong='%s' nodeShort='%s'\n", ln.c_str(), sn.c_str());
        if (ln.length()) strncpy(gCfg.nodeLong,  ln.c_str(), sizeof(gCfg.nodeLong)  - 1);
        if (sn.length()) strncpy(gCfg.nodeShort, sn.c_str(), sizeof(gCfg.nodeShort) - 1);
        float f;
        f = prefs.getFloat("loraFreq", 0.0f); if (f > 0.0f) gCfg.loraFreq     = f;
        f = prefs.getFloat("loraBw",   0.0f); if (f > 0.0f) gCfg.loraBw       = f;
        uint8_t u;
        u = prefs.getUChar("loraSf",     0); if (u) gCfg.loraSf       = u;
        u = prefs.getUChar("loraCr",     0); if (u) gCfg.loraCr       = u;
        // Migrate: CR=8 at SF11/BW250 was an old wrong default for LONG_FAST (should be CR=5).
        // Auto-correct so existing saved configs get the right coding rate on next boot.
        if (gCfg.loraCr == 8 && gCfg.loraSf == 11 && gCfg.loraBw == 250.0f) {
            Serial.printf("[config] migrating loraCr 8→5 (LONG_FAST default fix)\n");
            gCfg.loraCr = 5;
        }
        u = prefs.getUChar("loraPower",  0); if (u) gCfg.loraPower    = u;
        u = prefs.getUChar("loraHopLim", 0); if (u) gCfg.loraHopLimit = u;
        if (prefs.isKey("gpsEnabled")) gCfg.gpsEnabled = prefs.getBool("gpsEnabled");
        int32_t i;
        i = prefs.getInt("latI", 0); if (i) gCfg.latI = i;
        i = prefs.getInt("lonI", 0); if (i) gCfg.lonI = i;
        i = prefs.getInt("alt",  -1); if (i >= 0) gCfg.alt = (int32_t)i;
        uint8_t ro = prefs.getUChar("devRole",     0xFF); if (ro != 0xFF) gCfg.deviceRole     = ro;
        ro          = prefs.getUChar("rebroadcast", 0xFF); if (ro != 0xFF) gCfg.rebroadcastMode = ro;
        if (prefs.isKey("okToMqtt"))   gCfg.okToMqtt   = prefs.getBool("okToMqtt");
        if (prefs.isKey("ignoreMqtt")) gCfg.ignoreMqtt = prefs.getBool("ignoreMqtt");
        uint32_t ul;
        ul = prefs.getULong("nodeInfoIntv", 0); if (ul) gCfg.nodeInfoIntervalS = ul;
        ul = prefs.getULong("posIntv",      0); if (ul) gCfg.posIntervalS      = ul;
        if (prefs.isKey("gpsPollS")) {
            ul = prefs.getULong("gpsPollS", 0);
            gCfg.gpsPollIntervalS = (uint32_t)constrain((long)ul, (long)0, (long)3600);
        } else if (prefs.isKey("gpsPollMs")) {
            ul = prefs.getULong("gpsPollMs", 0);
            gCfg.gpsPollIntervalS = (ul == 0) ? 0 : (uint32_t)constrain((long)((ul + 999UL) / 1000UL), (long)0, (long)3600);
        }
        String rgn = prefs.getString("region", ""); if (rgn.length()) strncpy(gCfg.region, rgn.c_str(), sizeof(gCfg.region)-1);
        String tz = prefs.getString("tzDef", ""); if (tz.length()) strncpy(gCfg.tzDef, tz.c_str(), sizeof(gCfg.tzDef)-1);
        String ntp = prefs.getString("ntpServer", ""); if (ntp.length()) strncpy(gCfg.ntpServer, ntp.c_str(), sizeof(gCfg.ntpServer)-1);
        ul = prefs.getULong("screenOnSecs", 0);
        Serial.printf("[cfg] loaded screenOnSecs=%lu (isKey=%d)\n",
                      (unsigned long)ul, prefs.isKey("screenOnSecs") ? 1 : 0);
        if (ul) gCfg.screenOnSecs = ul;
        ro = prefs.getUChar("dispUnits", 0xFF); if (ro != 0xFF) gCfg.displayUnits = ro;
        if (prefs.isKey("compassNorth")) gCfg.compassNorthTop = prefs.getBool("compassNorth");
        if (prefs.isKey("flipScreen"))   gCfg.flipScreen      = prefs.getBool("flipScreen");
        if (prefs.isKey("splashMelody")) gCfg.splashMelodyEnabled = prefs.getBool("splashMelody");
        if (prefs.isKey("msgAlertSound")) {
            gCfg.msgAlertSound = (uint8_t)constrain((int)prefs.getUChar("msgAlertSound"), 0, 3);
        } else if (prefs.isKey("msgAlertBeep")) {
            // Backward-compatible migration from legacy bool toggle.
            gCfg.msgAlertSound = prefs.getBool("msgAlertBeep")
                ? MSG_ALERT_SOUND_DEFAULT
                : MSG_ALERT_SOUND_OFF;
        }
        ro = prefs.getUChar("uiTheme", 0xFF); if (ro != 0xFF && ro < UI_THEME_COUNT) gCfg.uiTheme = ro;
        ro = prefs.getUChar("uiMode", 0xFF);  if (ro != 0xFF && ro <= UI_MODE_LIGHT) gCfg.uiMode = ro;
        if (prefs.isKey("btEnabled"))    gCfg.btEnabled       = prefs.getBool("btEnabled");
        ro = prefs.getUChar("btMode", 0xFF); if (ro != 0xFF) gCfg.btMode = ro;
        ul = prefs.getULong("btFixedPin", 0); if (ul) gCfg.btFixedPin = ul;
        if (prefs.isKey("mqttEnabled")) gCfg.mqttEnabled = prefs.getBool("mqttEnabled");
        String mqttServer = prefs.getString("mqttServer", "");
        if (mqttServer.length()) strncpy(gCfg.mqttServer, mqttServer.c_str(), sizeof(gCfg.mqttServer) - 1);
        String mqttUser = prefs.getString("mqttUser", "");
        if (mqttUser.length()) strncpy(gCfg.mqttUser, mqttUser.c_str(), sizeof(gCfg.mqttUser) - 1);
        String mqttPass = prefs.getString("mqttPass", "");
        if (mqttPass.length()) strncpy(gCfg.mqttPass, mqttPass.c_str(), sizeof(gCfg.mqttPass) - 1);
        String mqttRoot = prefs.getString("mqttRoot", "");
        if (mqttRoot.length()) strncpy(gCfg.mqttRoot, mqttRoot.c_str(), sizeof(gCfg.mqttRoot) - 1);
        gCfg.mqttServer[sizeof(gCfg.mqttServer) - 1] = '\0';
        gCfg.mqttUser[sizeof(gCfg.mqttUser) - 1] = '\0';
        gCfg.mqttPass[sizeof(gCfg.mqttPass) - 1] = '\0';
        gCfg.mqttRoot[sizeof(gCfg.mqttRoot) - 1] = '\0';
        gCfg.ntpServer[sizeof(gCfg.ntpServer) - 1] = '\0';
        if (prefs.isKey("mqttEncrypt")) gCfg.mqttEncryption = prefs.getBool("mqttEncrypt");
        if (prefs.isKey("mqttMapRpt"))  gCfg.mqttMapReport  = prefs.getBool("mqttMapRpt");
        if (prefs.isKey("isPwrSaving")) gCfg.isPowerSaving  = prefs.getBool("isPwrSaving");
        ul = prefs.getULong("lsSecs",       0); if (ul) gCfg.lsSecs       = ul;
        ul = prefs.getULong("minWakeSecs",  0); if (ul) gCfg.minWakeSecs  = ul;
        if (prefs.isKey("telDevEn"))  gCfg.telDeviceEnabled = prefs.getBool("telDevEn");
        ul = prefs.getULong("telDevIntv",   0); if (ul) gCfg.telDeviceIntervalS = ul;
        if (prefs.isKey("telEnvEn"))  gCfg.telEnvEnabled    = prefs.getBool("telEnvEn");
        ul = prefs.getULong("telEnvIntv",   0); if (ul) gCfg.telEnvIntervalS    = ul;
        if (prefs.isKey("cannedEn"))  gCfg.cannedEnabled    = prefs.getBool("cannedEn");
        String cm = prefs.getString("cannedMsgs", ""); if (cm.length()) strncpy(gCfg.cannedMessages, cm.c_str(), sizeof(gCfg.cannedMessages)-1);
        ul = prefs.getULong("nodeIdOvr", 0); if (ul) gCfg.nodeIdOverride = (uint32_t)ul;
        ro = prefs.getUChar("chatSpace", 0xFF); if (ro != 0xFF && ro <= 2) gCfg.chatSpacing = ro;
        if (prefs.isKey("dbgAcks")) gCfg.debugAcks = prefs.getBool("dbgAcks");
        if (prefs.isKey("dbgMsgs")) gCfg.debugMessages = prefs.getBool("dbgMsgs");
        if (prefs.isKey("dbgGps"))  gCfg.debugGps = prefs.getBool("dbgGps");
        prefs.end();
    }
    debugSetFlags(gCfg.debugAcks, gCfg.debugMessages, gCfg.debugGps);
    applyTimezoneFromConfig();
    // Apply chat spacing setting to runtime globals
    LINE_H        = kSpacingPx[gCfg.chatSpacing <= 2 ? gCfg.chatSpacing : 1];
    VISIBLE_LINES = max(1, (CHAT_H - 2) / LINE_H);
    applyUiTheme(false);

    // Apply node ID override if set (allows restoring old Meshtastic identity)
    if (gCfg.nodeIdOverride != 0) {
        myNodeId = gCfg.nodeIdOverride;
        Serial.printf("[camillia-mt] Node ID overridden to: !%08x\n", myNodeId);
    }
    // Expose device role globally so encodeNodeInfo can include it in NODEINFO packets
    myDeviceRole = gCfg.deviceRole;
    Serial.printf("[camillia-mt] Node !%08x  long='%s'  short='%s'  role=%u\n",
                  myNodeId, gCfg.nodeLong, gCfg.nodeShort, gCfg.deviceRole);
    // Load channel config from NVS (single namespace)
    {
        Preferences cp; cp.begin("mesh_ch", true);
        for (int i = 0; i < MESH_CHANNELS; i++) {
            char k[8];
            snprintf(k, sizeof(k), "n%d", i);
            String nm = cp.getString(k, "");
            nm.trim();
            if (nm.length() > 0 && nm.length() < sizeof(CHANNEL_KEYS[i].name_buf)) {
                strncpy(CHANNEL_KEYS[i].name_buf, nm.c_str(), sizeof(CHANNEL_KEYS[i].name_buf) - 1);
                CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
                CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
            }
            snprintf(k, sizeof(k), "k%d", i);
            uint8_t kbuf[32];
            size_t klen = cp.getBytes(k, kbuf, 32);
            if (klen > 0) { memcpy(CHANNEL_KEYS[i].key, kbuf, klen); CHANNEL_KEYS[i].keyLen = (uint8_t)klen; }
            snprintf(k, sizeof(k), "r%d", i);
            uint8_t role = cp.getUChar(k, 0xFF);
            if (role != 0xFF) CHANNEL_KEYS[i].role = role;
            // Recompute on-air hash from the loaded name + key
            const char *nm2 = CHANNEL_KEYS[i].name_buf[0] ? CHANNEL_KEYS[i].name_buf : CHANNEL_KEYS[i].name;
            CHANNEL_KEYS[i].hash = computeChannelHash(nm2, CHANNEL_KEYS[i].key, CHANNEL_KEYS[i].keyLen);
            Serial.printf("[cfg] ch%d load: name='%s' keyLen=%u role=%u hash=0x%02X\n",
                          i, nm2, CHANNEL_KEYS[i].keyLen, CHANNEL_KEYS[i].role, CHANNEL_KEYS[i].hash);
        }
        cp.end();
    }
    Serial.printf("[camillia-mt] Name: %s (%s)\n", gCfg.nodeLong, gCfg.nodeShort);

    // Board power
    pinMode(BOARD_POWERON, OUTPUT); digitalWrite(BOARD_POWERON, HIGH);
#if (USER_BUTTON_PIN >= 0)
    pinMode(USER_BUTTON_PIN,
            (USER_BUTTON_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
#if (BOARD_VEXT_ENABLE >= 0)
    pinMode(BOARD_VEXT_ENABLE, OUTPUT);
    digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
    delay(20);
#endif
    batteryInitAdc();

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer display support depends on board init performed by kb.begin().
    kb.begin();
#endif

    // Display
    lcd.init();
    lcd.setRotation(TFT_ROTATION_DEFAULT);
    lcd.setBrightness(TFT_BRIGHTNESS_DEFAULT);
    lcd.fillScreen(COL_BG_MAIN);
    lcd.setTextSize(UI_BASE_TEXT_SCALE);
    lastActivityMs = millis();

    // Splash
    drawSplash();

#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    // Match the known-good T-Deck/Heltec startup order: bring up display/touch
    // first, then initialize the keyboard sidecar.
    kb.begin();
#endif

    playSplashStartupRiff();

    // Data modules
    Nodes.init();
    Channels.init();
    Channels.setActive(0);  // start on LongFast (channel 0)

    // Register ourselves in the node DB immediately
    {
        NodeEntry *me = Nodes.upsert(myNodeId);
        strncpy(me->longName,  gCfg.nodeLong,  sizeof(me->longName)  - 1);
        strncpy(me->shortName, gCfg.nodeShort, sizeof(me->shortName) - 1);
        me->latI        = MY_LAT_I;
        me->lonI        = MY_LON_I;
        me->alt         = MY_ALT;
        me->hasPosition = true;
        me->hops        = 0;
        me->lastHeardMs = millis();
    }

    // LoRa
    if (!Radio.init()) {
        lcd.setTextColor(TFT_RED, COL_BG_MAIN);
        lcd.setCursor(10, 100);
        lcd.print("LoRa init FAILED");
        while (true) delay(500);
    }

    // Always apply persisted radio config after init so NVS values take effect on boot.
    bool rfCfgOk = Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw,
                                     gCfg.loraSf, gCfg.loraCr, gCfg.loraPower);
    if (!rfCfgOk) {
        Serial.println("[camillia-mt] WARNING: failed to apply saved LoRa config");
        Channels.addMessage(CHAN_ANN, "", "! LoRa config apply failed", TFT_RED);
    }

    // SD card needs SPI bus — init after Radio.init() calls SPI.begin()
    sdBegin();
    Channels.beginPersistence();
    Channels.loadPersisted();
    DMs.init();

    // Let radio settle before first TX
    delay(200);
    bool niOk = Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
    debugLogMessages("[camillia-mt] NODEINFO broadcast %s\n", niOk ? "sent" : "FAILED");
    Channels.addMessage(CHAN_ANN, "", niOk ? "* Announced (NODEINFO)" : "! NODEINFO failed",
                        niOk ? TFT_DARKGREY : TFT_RED);

    bool posOk = Channels.sendPosition(myNodeId, gCfg.latI, gCfg.lonI, gCfg.alt);
    debugLogMessages("[camillia-mt] POSITION broadcast %s\n", posOk ? "sent" : "FAILED");
    Channels.addMessage(CHAN_ANN, "", posOk ? "* Position broadcast" : "! POSITION failed",
                        posOk ? TFT_DARKGREY : TFT_RED);

    // Sync activeView with the channel manager's initial active index
    activeView = Channels.activeIdx();

    // Start GPS if enabled
    if (gCfg.gpsEnabled) {
        gpsBegin();
        Channels.addMessage(CHAN_ANN, "", "* GPS started", TFT_DARKGREY);
    }

    // Boot-time one-shot Wi-Fi/NTP sync, then shut web server/Wi-Fi back down.
    bool bootTimeSynced = false;
    if (webCfgBegin(&gCfg, onWebCfgSaved)) {
        if (wifiHasInternetTimePath()) {
            uint32_t syncStartMs = millis();
            while ((millis() - syncStartMs) < 10000UL) {
                webCfgLoop();
                if (ntpSyncSystemClock()) {
                    bootTimeSynced = true;
                    break;
                }
                delay(200);
            }
        }
        webCfgEnd();
    }
    if (bootTimeSynced) {
        snprintf(settingsStatus, sizeof(settingsStatus), "Boot time sync OK");
        Channels.addMessage(CHAN_ANN, "", "* Boot time sync OK", TFT_DARKGREY);
    } else {
        snprintf(settingsStatus, sizeof(settingsStatus), "Web server: stopped");
    }

    // Initial full draw
    drawDivider();
    dirtyStatus = dirtyTabs = dirtyChat = dirtyNodes = dirtyInput = true;
}

// Poll touch + keyboard; called multiple times per loop to avoid drops
static void pollInput() {
    uint32_t now = millis();
    char tb = kb.readTrackball();

#if (USER_BUTTON_PIN >= 0)
    static bool userBtnRawPrev = false;
    static bool userBtnStable = false;
    static uint32_t userBtnDebounceMs = 0;
    bool userPressed = (digitalRead(USER_BUTTON_PIN) == USER_BUTTON_ACTIVE_LEVEL);
    if (userPressed != userBtnRawPrev) {
        userBtnRawPrev = userPressed;
        userBtnDebounceMs = now;
    }
    if ((now - userBtnDebounceMs) >= 30 && userPressed != userBtnStable) {
        userBtnStable = userPressed;
        if (userBtnStable) {
#if defined(DEVICE_TLORA_PAGER_TFT)
            if (screenAsleep) {
                wakeScreen();
                return;
            }
            sleepScreen("BOOT button");
            return;
#else
            if (screenAsleep) {
                wakeScreen();
                return;
            }
            lastActivityMs = now;
            handleKey(KEY_ENTER);
#endif
        }
    }
#endif

    // Allow trackball activity to wake the screen.
    if (screenAsleep) {
        if (tb != KEY_NONE) {
            wakeScreen();
            return;
        }
    }

    if (tb != KEY_NONE) {
        lastActivityMs = now;
        handleKey(tb);
    }

    // Touchscreen tap handling (for on-screen channel nav buttons)
#if TOUCH_POLL_ENABLED
    static uint32_t lastTouchPollMs = 0;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
    static bool touchHandledOnPress = false;
#endif
    if (now - lastTouchPollMs >= 16) {
        lastTouchPollMs = now;

        int32_t tx = 0, ty = 0;
        bool t = lcd.getTouch(&tx, &ty);
        if (t) {
            if (screenAsleep) { wakeScreen(); return; }
            lastActivityMs = now;
            if (!touchDown) {
                touchDown = true;
                touchStartX = tx;
                touchStartY = ty;
                touchDownMs = now;
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
                touchHandledOnPress = handleTouchTap((int)tx, (int)ty);
#endif
            }
            touchLastX = tx;
            touchLastY = ty;
            softKeyboardTrackPress((int)tx, (int)ty);
        } else if (touchDown) {
            softKeyboardClearPressed();
            uint32_t tapHoldLimit = (activeView == VIEW_MAP) ? 2500 : 450;
            int driftLimit = (activeView == VIEW_MAP) ? 26 : 18;
            bool shortTap = (now - touchDownMs) <= tapHoldLimit;
            bool steady   = (abs(touchLastX - touchStartX) <= driftLimit)
                         && (abs(touchLastY - touchStartY) <= driftLimit);
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
            if (!touchHandledOnPress && shortTap && steady) {
#else
            if (shortTap && steady) {
#endif
                int tapX = (activeView == VIEW_MAP) ? touchLastX : (touchStartX + touchLastX) / 2;
                int tapY = (activeView == VIEW_MAP) ? touchLastY : (touchStartY + touchLastY) / 2;
                handleTouchTap(tapX, tapY);
            }
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
            touchHandledOnPress = false;
#endif
            touchDown = false;
        } else {
            softKeyboardClearPressed();
        }
    }
#else
    touchDown = false;
#endif

    // Pull fresh keyboard bytes, then consume queued keys.
    pumpKeyboardRaw(24, now);
    for (int ki = 0; ki < 24; ki++) {
        char k;
        if (!dequeueKey(k)) break;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
        if (k == KEY_ENTER && activeView == VIEW_SETTINGS) {
            activateSettingsSelection();
            continue;
        }
#endif
        handleKey(k);
    }
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    // Poll input first — keyboard MCU has tiny buffer
    pollInput();

    // Clear typing lock only after leaving input-capable views; explicit compose
    // mode now handles empty-buffer exit via Escape, send, or backspace-to-empty.
    if (hwTypingLock && !isTextInputView()) hwTypingLock = false;

    // 1. Poll radio
#if defined(DEVICE_TDECK)
    // T-Deck keyboard can lag under heavy RX handling; pause RX whenever
    // we're actively composing text in an input-capable view.
    bool tdeckTypingCompose = isTextInputView() && (hwTypingLock || inputLen > 0);
#else
    bool tdeckTypingCompose = false;
#endif
    bool pauseRxForTyping = softKbVisible
                         || (hwTypingLock && isTextInputView())
                         || tdeckTypingCompose
                         || (activeView == VIEW_NODES);
    if (!pauseRxForTyping) {
        MeshPacket pkt;
        if (Radio.pollRx(pkt)) {
            handleRx(pkt);
            pollInput();   // poll again after heavy packet processing
        }
    }

    // 1a. Process deferred NODEINFO greetings (one per loop, with input polling)
    if (deferredCount > 0) {
        uint32_t dest = deferredGreet[0];
        // Shift queue
        for (int i = 1; i < deferredCount; i++) deferredGreet[i-1] = deferredGreet[i];
        deferredCount--;
        pollInput();
        debugLogMessages("[nodeinfo] deferred greeting !%08X\n", dest);
        if (Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort, dest)) {
            NodeEntry *n = Nodes.find(dest);
            if (n) n->lastSentInfoMs = millis();
        }
        pollInput();
    }

    // 1b. Service web config server if running
    webCfgLoop();
    if (webCfgAnnounceRequested()) {
        Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
        Channels.sendPosition(myNodeId, gCfg.latI, gCfg.lonI, gCfg.alt);
        debugLogMessages("[announce] manual NODEINFO + position broadcast\n");
    }

    uint32_t now = millis();

    // 1c. Poll GPS (configurable; 0 = every loop)
    static uint32_t lastGpsPollMs = 0;
    uint32_t gpsPollPeriodMs = (gCfg.gpsPollIntervalS == 0) ? 0 : (gCfg.gpsPollIntervalS * 1000UL);
    if (gpsPollPeriodMs == 0
        || lastGpsPollMs == 0
        || (now - lastGpsPollMs) >= gpsPollPeriodMs) {
        lastGpsPollMs = now;
        gpsLoop();
    }

    now = millis();

    // Clock policy for all builds:
    // 1) GPS is authoritative when available.
    // 2) Internet NTP is bootstrap/fallback while GPS is unavailable.
    static bool clockEverSynced = false;
    static bool gpsEverSynced = false;
    static uint32_t lastGpsClockSyncAttemptMs = 0;
    static uint32_t lastNtpClockSyncAttemptMs = 0;

    bool gpsClockUpdated = false;
    uint32_t gpsSyncPeriodMs = gpsEverSynced ? 300000UL : 5000UL;
    if (now - lastGpsClockSyncAttemptMs >= gpsSyncPeriodMs) {
        lastGpsClockSyncAttemptMs = now;
        if (gpsSyncSystemClock()) {
            clockEverSynced = true;
            gpsEverSynced = true;
            gpsClockUpdated = true;
            dirtyStatus = true;
        }
    }

    uint32_t ntpSyncPeriodMs = clockEverSynced ? 300000UL : 5000UL;
    if (!gpsClockUpdated && now - lastNtpClockSyncAttemptMs >= ntpSyncPeriodMs) {
        lastNtpClockSyncAttemptMs = now;
        if (ntpSyncSystemClock()) {
            clockEverSynced = true;
            dirtyStatus = true;
        }
    }

    // 3. ACK expiry
    if (Channels.expireAcks()) {
        dirtyChat = true;
        if (activeView == CHAN_ANN) dirtyLiveRows = true;
    }

    // 4. Cursor blink
    if (now - lastBlink > CURSOR_BLINK_MS) {
        if (softKbVisible) {
            cursorOn = true;
        } else {
            cursorOn = !cursorOn;
            dirtyInput = true;
        }
        lastBlink = now;
    }

    // 5. Periodic NODEINFO re-announcement
    if (now - lastNodeInfo > (uint32_t)gCfg.nodeInfoIntervalS * 1000UL) {
        lastNodeInfo = now;
        Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
    }

    // 6. Periodic position broadcast
    if (now - lastPosition > (uint32_t)gCfg.posIntervalS * 1000UL) {
        lastPosition = now;
        // Prefer live GPS fix; fall back to manual/last-known config position
        int32_t posLat = gCfg.latI, posLon = gCfg.lonI, posAlt = gCfg.alt;
        if (gpsHasFix()) { posLat = gpsLatI(); posLon = gpsLonI(); posAlt = gpsAltM(); }
        Channels.sendPosition(myNodeId, posLat, posLon, posAlt);
    }

    // 6b. Auto-refresh GPS view every second
    if (activeView == VIEW_GPS) {
        static uint32_t lastGpsRefreshMs = 0;
        if (now - lastGpsRefreshMs >= 1000) {
            lastGpsRefreshMs = now;
            dirtyChat = dirtyTabs = true;   // update tab colour (fix state) too
        }
    }

    // 6c. Battery refresh every 5 s
    static uint32_t lastBattMs = 0;
    if (now - lastBattMs >= 5000) {
        lastBattMs   = now;
        _battPct     = readBatteryPct();
        dirtyStatus  = true;
    }

    // Detect appended LIVE lines even when they originate outside main.cpp.
    static int lastLiveCountSeen = -1;
    int liveCountNow = Channels.get(CHAN_ANN).count;
    if (liveCountNow != lastLiveCountSeen) {
        lastLiveCountSeen = liveCountNow;
        dirtyTabs = true;
        if (activeView == CHAN_ANN) dirtyLiveRows = true;
    }
    if ((activeView == VIEW_MAP || activeView == VIEW_NODES) && !nodeDetailOpen) {
        // Keep map nodes visually frozen while map is focused.
        dirtyNodes = false;
    }

    // Keep wall clock display responsive even if other status elements are static.
    static uint32_t lastClockDrawTickMs = 0;
    if (now - lastClockDrawTickMs >= 1000) {
        lastClockDrawTickMs = now;
        dirtyStatus = true;
    }

    // 6d. Screen timeout
    if (!screenAsleep && gCfg.screenOnSecs > 0 &&
        now - lastActivityMs > (uint32_t)gCfg.screenOnSecs * 1000UL) {
        Serial.printf("[screen] sleeping (idle %lus, timeout %us)\n",
                      (now - lastActivityMs) / 1000UL, gCfg.screenOnSecs);
        sleepScreen("timeout");
    }

    // 6e. CPU light sleep while screen is off (power-save mode)
    if (screenAsleep && gCfg.isPowerSaving && !webCfgRunning() && gCfg.lsSecs > 0) {
        uint32_t idleMs = now - lastActivityMs;
        uint32_t sleepAfterMs = (uint32_t)gCfg.lsSecs * 1000UL;
        uint32_t minWakeMs = (uint32_t)gCfg.minWakeSecs * 1000UL;
        uint32_t thresholdMs = max(sleepAfterMs, minWakeMs);
        if (idleMs >= thresholdMs) {
            // Keep sleep slices short so polling-driven subsystems remain responsive.
            constexpr uint64_t kLightSleepSliceUs = 20000ULL; // 20 ms
            esp_sleep_enable_timer_wakeup(kLightSleepSliceUs);
            esp_light_sleep_start();
            now = millis();
        }
    }

    // 7. Redraw dirty zones (skip while screen is off)
    if (screenAsleep) return;

    if (softKbVisible && (dirtyChat || dirtyNodes)) dirtyInput = true;

    if (dirtyStatus)  drawStatus();
    if (dirtyTabs)    drawTabs();
    pollInput();   // squeeze in a keyboard poll between redraws

    if (nodeDetailOpen) {
        // Detail overlay refreshes when chat or nodes go dirty
        if (dirtyChat || dirtyNodes) {
            NodeEntry *n = Nodes.find(nodeDetailId);
            drawNodeDetail(n);
            dirtyNodes = false;
        }
        dirtyInput = false;   // detail covers input area
    } else {
        if (dirtyDivider) { drawDivider(); dirtyDivider = false; }
        if (dirtyChat) {
            if      (activeView == VIEW_SETTINGS)                      drawSettings();
            else if (activeView == VIEW_GPS)                           drawGps();
            else if (activeView == VIEW_MAP)                           drawMapPanel();
            else if (activeView == VIEW_NODES)                         drawNodesPanel();
            else if (activeView == CHAN_ANN)                           drawLivePanel(true);
            else if (activeView == CHAN_DM && dmPickerOpen)            drawDmPicker();
            else if (activeView == CHAN_DM && dmConvOpen)              drawDmConv();
            else if (activeView == CHAN_DM)                            drawDmList();
            else                                                       drawChat();
        } else if (activeView == CHAN_ANN && dirtyLiveRows) {
            drawLivePanel(false);
        }
        if (activeView < MESH_CHANNELS) {
            if (dirtyNodes) drawNodes();
        }
        if (dirtyInput) drawInput();
    }
}
