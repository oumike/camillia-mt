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
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"

// ── Chat spacing (runtime, set once at startup from gCfg.chatSpacing) ─
int LINE_H        = 10;     // default Normal
int VISIBLE_LINES = (CHAT_H - 2) / 10;

static const int kSpacingPx[] = { 8, 10, 12 };  // Tight, Normal, Loose

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
#define TOTAL_VIEWS   (MAX_CHANNELS + 3)
static int  activeView   = 0;              // 0..9 channels, 10 GPS, 11 MAP, 12 settings
static int  tabScrollX   = 0;             // horizontal scroll offset for tab bar (px)
static int  lastChannelView = 0;          // most recent real mesh channel (0..MESH_CHANNELS-1)
static int  panelReturnChannel = 0;       // channel to return to when closing DM/MAP/LIVE/CFG
static int  settingsSel  = 0;         // highlighted settings row
static int  mapsListSel = 0;          // highlighted node row in MAP panel
static const int SETTINGS_ROW_H = 10;
static const int SETTINGS_HDR_H = 16;
static const int TOUCH_BTN_W = 58;
static const int TOUCH_BTN_H = 24;
static const int TOUCH_BTN_BOTTOM_PAD = 5;
static const int NAV_BTN_COUNT = 6;

struct PanelHitRect {
    int x;
    int y;
    int w;
    int h;
};
static bool panelCloseVisible = false;
static PanelHitRect panelCloseRect = {0, 0, 0, 0};
static bool panelClearVisible = false;
static PanelHitRect panelClearRect = {0, 0, 0, 0};
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

static bool mapCtlVisible[MAP_CTL_COUNT] = {};
static PanelHitRect mapCtlRect[MAP_CTL_COUNT] = {};

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


static void setPanelCloseRect(int x, int y, int w, int h) {
    panelCloseVisible = true;
    panelCloseRect = { x, y, w, h };
}

static void setPanelClearRect(int x, int y, int w, int h) {
    panelClearVisible = true;
    panelClearRect = { x, y, w, h };
}

static void clearPanelCloseRect() {
    panelCloseVisible = false;
    panelCloseRect = {0, 0, 0, 0};
    panelClearVisible = false;
    panelClearRect = {0, 0, 0, 0};
    dmNewVisible = false;
    dmNewRect = {0, 0, 0, 0};
    for (int i = 0; i < MAP_CTL_COUNT; i++) {
        mapCtlVisible[i] = false;
        mapCtlRect[i] = {0, 0, 0, 0};
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

static bool isPanelView(int v) {
    return (v == CHAN_DM || v == CHAN_ANN || v == VIEW_MAP || v == VIEW_SETTINGS);
}

static bool isTopTabView(int v) {
    return !isPanelView(v) && v != VIEW_GPS;
}

static void closePanelToChannel();
static void mapClampViewport();
static int mapVisibleNodeCount();
static NodeEntry *mapVisibleNodeByIndex(int idx);

// ── DM sub-state ──────────────────────────────────────────────
static bool     dmConvOpen   = false;  // true = showing conversation
static bool     dmPickerOpen = false;  // true = showing node picker ("New DM")
static int      dmListSel    = 0;      // 0 = "New DM" button, 1+ = conversation index
static int      dmPickerSel  = 0;      // selected row in node picker
static uint32_t dmConvNodeId = 0;      // node ID of open conversation

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

static void wakeScreen() {
    lcd.setBrightness(128);
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
        if (screenAsleep) {
            wakeScreen();
            break;
        }
        lastActivityMs = nowMs;
        queueKey(k);
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
#define SETTING_EXPORT        1
#define SETTING_IMPORT        2
#define SETTING_THEME         3
#define SETTING_CLEAR_NODES   4
#define SETTING_FACTORY_RESET 5
#define NUM_SETTINGS          6

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

static uint8_t uiThemePresetIndex() {
    if (gCfg.uiTheme == UI_THEME_EARTHEN)
        return (gCfg.uiMode == UI_MODE_LIGHT) ? 5 : 4;
    if (gCfg.uiTheme == UI_THEME_EVERGREEN)
        return (gCfg.uiMode == UI_MODE_LIGHT) ? 3 : 2;
    return (gCfg.uiMode == UI_MODE_LIGHT) ? 1 : 0;
}

static const char *uiThemePresetName(uint8_t preset) {
    switch (preset % 6) {
        case 0: return "Camillia Dark";
        case 1: return "Camillia Light";
        case 2: return "Evergreen Dark";
        case 3: return "Evergreen Light";
        case 4: return "Earthy Dark";
        default: return "Earthy Light";
    }
}

static void setUiThemePreset(uint8_t preset) {
    switch (preset % 6) {
        case 0: gCfg.uiTheme = UI_THEME_CAMELLIA; gCfg.uiMode = UI_MODE_DARK; break;
        case 1: gCfg.uiTheme = UI_THEME_CAMELLIA; gCfg.uiMode = UI_MODE_LIGHT; break;
        case 2: gCfg.uiTheme = UI_THEME_EVERGREEN; gCfg.uiMode = UI_MODE_DARK; break;
        case 3: gCfg.uiTheme = UI_THEME_EVERGREEN; gCfg.uiMode = UI_MODE_LIGHT; break;
        case 4: gCfg.uiTheme = UI_THEME_EARTHEN; gCfg.uiMode = UI_MODE_DARK; break;
        default: gCfg.uiTheme = UI_THEME_EARTHEN; gCfg.uiMode = UI_MODE_LIGHT; break;
    }
}

static void persistUiTheme() {
    Preferences p;
    p.begin("camillia", false);
    p.putUChar("uiTheme", gCfg.uiTheme);
    p.putUChar("uiMode", gCfg.uiMode);
    p.end();
}

static void applyUiTheme(bool markDirty = true) {
    gCfg.uiTheme = (uint8_t)constrain((int)gCfg.uiTheme, 0, UI_THEME_COUNT - 1);
    gCfg.uiMode  = (uint8_t)(gCfg.uiMode == UI_MODE_LIGHT ? UI_MODE_LIGHT : UI_MODE_DARK);

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
                         || activeView == CHAN_DM);
    activeView = v;
    clearPanelCloseRect();
    nodeListFocused = false;
    nodeDetailOpen  = false;
    dmConvOpen      = false;   // reset DM sub-state on any navigation
    dmPickerOpen    = false;
    dmListSel       = 0;
    dmPickerSel     = 0;
    // If navigating to DM tab and there's an unread conversation, open it immediately
    if (v == CHAN_DM) {
        for (int i = 0; i < DMs.count(); i++) {
            DmConv *c = DMs.getByRank(i);
            if (c && c->unread) {
                DMs.markRead(c->nodeId);
                dmConvNodeId = c->nodeId;
                dmConvOpen   = true;
                break;
            }
        }
        // Even if no unread, pre-select the most recent conversation (not "New DM")
        if (!dmConvOpen && DMs.count() > 0)
            dmListSel = 1;
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
static void drawCamelliaMark(int cx, int cy) {
    const uint16_t SHADOW      = 0x18E4;
    const uint16_t PETAL_OUTER = 0xF9CF;
    const uint16_t PETAL_MID   = 0xFADF;
    const uint16_t PETAL_INNER = 0xFF7D;
    const uint16_t PETAL_HILITE= 0xFFDF;
    const uint16_t PETAL_EDGE  = 0xD8A7;
    const uint16_t CENTER      = 0xFD20;
    const uint16_t CENTER_DOT  = 0xFEA0;
    const uint16_t STEM        = 0x64EC;
    const uint16_t LEAF_DARK   = 0x2C87;
    const uint16_t LEAF_LIGHT  = 0x3D68;

    // Soft drop shadow under the bloom.
    lcd.fillCircle(cx + 1, cy + 4, 34, SHADOW);

    // Outer petals.
    for (int i = 0; i < 10; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 10.0f) + 0.16f;
        int px = cx + (int)(23.0f * cosf(a));
        int py = cy + (int)(18.0f * sinf(a));
        int pr = 11 + (i & 1);
        lcd.fillCircle(px, py, pr, PETAL_OUTER);
        lcd.fillCircle(px - 2, py - 2, pr - 4, PETAL_HILITE);
        lcd.drawCircle(px, py, pr, PETAL_EDGE);
    }

    // Mid petals (offset ring for layered camellia look).
    for (int i = 0; i < 8; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 8.0f) + 0.42f;
        int px = cx + (int)(13.0f * cosf(a));
        int py = cy + (int)(10.0f * sinf(a));
        lcd.fillCircle(px, py, 9, PETAL_MID);
        lcd.fillCircle(px - 1, py - 1, 5, PETAL_HILITE);
        lcd.drawCircle(px, py, 9, PETAL_EDGE);
    }

    // Inner petals.
    for (int i = 0; i < 5; i++) {
        float a = ((float)i * 2.0f * (float)M_PI / 5.0f) + 0.20f;
        int px = cx + (int)(6.0f * cosf(a));
        int py = cy + (int)(5.0f * sinf(a));
        lcd.fillCircle(px, py, 6, PETAL_INNER);
    }

    // Stamen cluster.
    lcd.fillCircle(cx, cy, 6, CENTER);
    lcd.drawCircle(cx, cy, 6, 0xD4C0);
    for (int i = 0; i < 10; i++) {
        float a = (float)i * 2.0f * (float)M_PI / 10.0f;
        int sx = cx + (int)(4.0f * cosf(a));
        int sy = cy + (int)(4.0f * sinf(a));
        lcd.fillCircle(sx, sy, 1, CENTER_DOT);
    }

    // Stem and leaves.
    lcd.fillRoundRect(cx - 1, cy + 20, 3, 17, 1, STEM);
    lcd.fillCircle(cx - 21, cy + 28, 8, LEAF_DARK);
    lcd.fillCircle(cx - 14, cy + 30, 6, LEAF_LIGHT);
    lcd.fillCircle(cx + 21, cy + 29, 8, LEAF_DARK);
    lcd.fillCircle(cx + 14, cy + 31, 6, LEAF_LIGHT);
}

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
    const uint16_t BG_TOP     = COL_SPLASH_TOP;
    const uint16_t BG_BOTTOM  = COL_SPLASH_BOTTOM;
    const uint16_t CARD_BG    = COL_SPLASH_CARD;
    const uint16_t CARD_EDGE  = COL_SPLASH_EDGE;
    const uint16_t TITLE      = COL_SPLASH_TITLE;
    const uint16_t DIM        = COL_SPLASH_DIM;

    for (int y = 0; y < LCD_H; y++) {
        int r1 = (BG_TOP >> 11) & 0x1F, g1 = (BG_TOP >> 5) & 0x3F, b1 = BG_TOP & 0x1F;
        int r2 = (BG_BOTTOM >> 11) & 0x1F, g2 = (BG_BOTTOM >> 5) & 0x3F, b2 = BG_BOTTOM & 0x1F;
        int r = r1 + ((r2 - r1) * y) / (LCD_H - 1);
        int g = g1 + ((g2 - g1) * y) / (LCD_H - 1);
        int b = b1 + ((b2 - b1) * y) / (LCD_H - 1);
        uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
        lcd.drawFastHLine(0, y, LCD_W, c);
    }

    lcd.fillRoundRect(16, 26, LCD_W - 32, 188, 8, CARD_BG);
    lcd.drawRoundRect(16, 26, LCD_W - 32, 188, 8, CARD_EDGE);
    lcd.drawRoundRect(17, 27, LCD_W - 34, 186, 8, COL_SPLASH_EDGE_HI);

    lcd.setFont(&fonts::Orbitron_Light_32);
    lcd.setTextSize(1);
    lcd.setTextColor(TITLE, CARD_BG);
    const char *appName = "CAMILLIA";
    int tw = lcd.textWidth(appName);
    lcd.drawString(appName, (LCD_W - tw) / 2, 38);

    drawCamelliaMark(LCD_W / 2, 116);

    lcd.setFont(&fonts::DejaVu9);

    char idBuf[44];
    snprintf(idBuf, sizeof(idBuf), "%s  (!%08x / %s)", gCfg.nodeLong, myNodeId, gCfg.nodeShort);
    lcd.setTextColor(TITLE, CARD_BG);
    tw = lcd.textWidth(idBuf);
    lcd.drawString(idBuf, (LCD_W - tw) / 2, 186);

    const char *ver = APP_VERSION;
    lcd.setTextColor(DIM, CARD_BG);
    tw = lcd.textWidth(ver);
    lcd.drawString(ver, (LCD_W - tw) / 2, 199);

    lcd.setFont(&fonts::Font0);

    delay(1500);
    lcd.fillScreen(COL_BG_MAIN);
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
// T-Deck routes VBAT through a 1:1 divider to BATT_ADC_PIN (GPIO 4).
// ADC attenuation must be set to ADC_11db (0-3.3 V) in setup().
static uint8_t readBatteryPct() {
    // Average 8 samples to reduce ADC noise
    int32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(BATT_ADC_PIN);
    float vadc = (sum / 8.0f) * (3.3f / 4095.0f);
    float vbat = vadc * BATT_DIV;
    int pct = (int)((vbat - BATT_VMIN) / (BATT_VMAX - BATT_VMIN) * 100.0f);
    return (uint8_t)(pct < 0 ? 0 : pct > 100 ? 100 : pct);
}

static uint8_t _battPct = 0;

static void applyTimezoneFromConfig() {
    const char *tz = (gCfg.tzDef[0]) ? gCfg.tzDef : "UTC0";
    setenv("TZ", tz, 1);
    tzset();
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
    bool gpsFix = gpsHasFix();
    uint8_t sats = gpsSats();
    uint16_t gpsCol = gpsFix ? COL_BATT_GOOD : COL_BATT_BAD;
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
        gpsCol  = lerp565(gpsCol,  COL_TEXT_MAIN, 72);
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

    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);
    const int byText = max(0, (STATUS_H - lcd.fontHeight()) / 2);
    const int byBatt = max(1, (STATUS_H - BAR_H) / 2);

    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%u%%", (unsigned)_battPct);
    int battTxtW = lcd.textWidth(tbuf);
    int barBodyW = max(30, battTxtW + 4);
    int battW = barBodyW + NUB_W;

    char sbuf[4];
    snprintf(sbuf, sizeof(sbuf), "%u", (unsigned)sats);
    int satW = lcd.textWidth(sbuf);
    bool showSats = (sats > 0);
    int gpsW = showSats ? (GPS_DOT_R * 2 + 2 + satW) : (GPS_DOT_R * 2 + 1);
    bool wifiShowApText = wifiApMode;
    int wifiW = wifiShowApText ? (lcd.textWidth("AP") + AP_PAD_X * 2) : WIFI_W;
    int gap = ICON_GAP_WIDE;

    auto calcTotal = [&]() {
        return gpsW + gap + wifiW + gap + battW;
    };

    int total = calcTotal();
    if (total > NODE_W) {
        showSats = false;
        gpsW = GPS_DOT_R * 2 + 1;
        total = calcTotal();
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

    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(lightUi ? COL_TEXT_MAIN : COL_TEXT_ON_ACCENT);
    int battTx = BX + (barBodyW - battTxtW) / 2;
    int battTy = byBatt + max(0, (BAR_H - CHAR_H) / 2);
    lcd.drawString(tbuf, battTx, battTy);
}

// ── Draw: status bar ─────────────────────────────────────────
static void drawStatus() {
    lcd.fillRect(0, 0, LCD_W, STATUS_H, COL_STATUS_BG);
    lcd.drawFastHLine(0, STATUS_H - 1, LCD_W, COL_DIVIDER);

    lcd.setFont(&fonts::Orbitron_Light_24);
    lcd.setTextSize(1);
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
    int x = 2;
    lcd.drawString(shortName, x, infoY);
    int shortW = lcd.textWidth(shortName);

    int flowerCx = x + shortW + 9;
    drawCamelliaMarkTiny(flowerCx, STATUS_H / 2);

    int timeX = flowerCx + 10;
    drawClippedText(timeX, infoY, NODE_X - timeX - 4, timeBuf);
    drawBattery();
    lcd.setFont(&fonts::Font0);
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
    uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
    drawSquirclePill(x, y, w, h, fill, COL_SELECT_ACCENT, false);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(COL_TEXT_MAIN, fill);
    int tw = lcd.textWidth("Close");
    int tx = x + max(1, (w - tw) / 2);
    int ty = y + max(0, (h - CHAR_H) / 2);
    drawClippedText(tx, ty, w - (tx - x) - 1, "Close");
    setPanelCloseRect(x, y, w, h);
}

static void drawPanelClearButton(int x, int y, int w, int h) {
    uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
    drawSquirclePill(x, y, w, h, fill, COL_SELECT_ACCENT, false);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(COL_TEXT_MAIN, fill);
    int tw = lcd.textWidth("Clear");
    int tx = x + max(1, (w - tw) / 2);
    int ty = y + max(0, (h - CHAR_H) / 2);
    drawClippedText(tx, ty, w - (tx - x) - 1, "Clear");
    setPanelClearRect(x, y, w, h);
}

// ── Draw: tab bar ─────────────────────────────────────────────
static void drawTabs() {
    lcd.fillRect(0, STATUS_H, LCD_W, TAB_H, COL_BG_MAIN);
    lcd.drawFastHLine(0, STATUS_H, LCD_W, COL_DIVIDER);
    lcd.drawFastHLine(0, STATUS_H + TAB_H - 1, LCD_W, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

    const int TAB_PAD_X = 5;
    const int TAB_GAP   = 3;
    const int PILL_H    = max(8, TAB_H - 4);
    const int PILL_Y    = STATUS_H + (TAB_H - PILL_H) / 2;

    // Build full tab list with absolute x positions
    struct TabEntry { int view; char label[16]; int x; int w; };
    TabEntry tabs[TOTAL_VIEWS];
    int tabCount = 0;
    int xCursor  = 2;

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

    int totalTabW = max(0, xCursor - TAB_GAP + 2);
    int maxScroll = max(0, totalTabW - LCD_W);

    // Auto-scroll so the active tab is always fully visible
    for (int t = 0; t < tabCount; t++) {
        if (tabs[t].view != activeView) continue;
        if (tabs[t].x < tabScrollX)
            tabScrollX = tabs[t].x;
        else if (tabs[t].x + tabs[t].w > tabScrollX + LCD_W - 2)
            tabScrollX = tabs[t].x + tabs[t].w - (LCD_W - 2);
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
            else if (gpsHasFix())    stateCol = COL_BATT_GOOD;
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
        drawClippedText(sx + TAB_PAD_X, PILL_Y + 1, tabs[t].w - 2 * TAB_PAD_X, tabs[t].label);
    }
    lcd.setFont(&fonts::Font0);
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
    drawPanelFrame(chatX, CHAT_Y, chatW, CHAT_H, COL_PANEL_BG, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

    int active = Channels.activeIdx();

    auto txStatusSymbol = [](const DisplayLine *dl) -> const char * {
        if (!dl || !dl->packetId) return nullptr;
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

        // Override color for sent messages based on ACK state
        if (dl->packetId) {
            switch (dl->ack) {
                case DisplayLine::ACKED:       col = TFT_GREEN;  break;
                case DisplayLine::ACKED_RELAY: col = TFT_YELLOW; break;
                case DisplayLine::NAKED:       col = TFT_RED;    break;
                case DisplayLine::TX_FAILED:   col = TFT_RED;    break;
                default: break;  // NONE / PENDING keep original color
            }
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

    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
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

    if (sscanf(body, "R %19[^>]>%7s %11s c%d", who, dst, tag, &ch) == 4) {
        if (ts[0]) snprintf(out, outLen, "%s RX %s from %s to %s on ch%d",
                            ts, livePortLabel(tag), who, liveDestLabel(dst), ch);
        else snprintf(out, outLen, "RX %s from %s to %s on ch%d",
                      livePortLabel(tag), who, liveDestLabel(dst), ch);
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
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = CHAT_H - 8;
    const int titleH = 11;
    const int left = mx + 1;
    const int innerW = mw - 2;
    const int msgTop = my + titleH + 2;
    const int controlsTop = my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD);
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
        lcd.fillRect(mx + 1, my + 1, mw - 2, titleH, COL_SELECT_BG);
        lcd.setFont(&fonts::Font0);
        lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
        drawClippedText(mx + 5, my + 2, mw - 10, "Live RX/TX");
        lcd.fillRect(mx + 1, controlsTop, mw - 2, my + mh - controlsTop - 1, COL_PANEL_BG);
        const int btnY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
        const int closeX = mx + 3;
        drawPanelCloseButton(closeX, btnY, TOUCH_BTN_W, TOUCH_BTN_H);
        drawPanelClearButton(closeX + TOUCH_BTN_W + 4, btnY, TOUCH_BTN_W, TOUCH_BTN_H);
    }

    lcd.setFont(&fonts::Font0);
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
                case DisplayLine::ACKED:       col = TFT_GREEN;  break;
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

    if (ch.scrollOff > 0) {
        lcd.setTextColor(COL_TEAL, COL_PANEL_ALT);
        drawClippedText(left + innerW - 30, msgTop + 1, 28, "more");
    }

    lcd.setFont(&fonts::Font0);
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
    if (!mapCanDownloadTiles()) return false;
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
    const int mx = 0;
    const int my = CHAT_Y;
    const int mw = LCD_W;
    const int navRowY = INPUT_Y + INPUT_H - TOUCH_BTN_H - 2;
    const int mapPanelBottom = navRowY - 1;
    const int mh = max(CHAT_H, mapPanelBottom - my + 1);
    const int mapNavBtnH = 22;
    const int mapNavBottomPad = 2;
    const int mapNavGap = 3;
    const int titleH = 11;
    const int ix = mx + 3;
    const int iw = mw - 6;
    const int controlsTop = my + mh - (mapNavBtnH + mapNavBottomPad);
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
    lcd.setTextSize(1);

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
    bool useTileBackdrop = !apMode;
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
        lcd.setFont(&fonts::Font0);
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

    const int btnY = my + mh - mapNavBtnH - mapNavBottomPad;
    const int closeW = 46;
    drawPanelCloseButton(mx + 3, btnY, closeW, mapNavBtnH);

    const int ctlCount = MAP_CTL_COUNT;
    const int minCtlX = mx + 3 + closeW + 4;
    int btnW[MAP_CTL_COUNT] = { 30, 30, 80, 60, 30 };
    const char *labels[MAP_CTL_COUNT] = { "+", "-", "Previous Node", "Next Node", "ME" };

    const int meIdx = (int)MAP_CTL_ME;
    int btnX[MAP_CTL_COUNT] = {0};
    int maxCtlEnd = mx + mw - 4;

    // Pin ME to the far right; center the remaining controls in the space to its left.
    int meX = maxCtlEnd - btnW[meIdx];
    btnX[meIdx] = meX;

    int leftCount = meIdx;
    int leftControlsW = 0;
    for (int i = 0; i < leftCount; i++) leftControlsW += btnW[i];
    if (leftCount > 0) leftControlsW += (leftCount - 1) * mapNavGap;

    int leftStartX = minCtlX;
    int leftAvailW = (meX - mapNavGap) - minCtlX;
    if (leftAvailW > leftControlsW) leftStartX += (leftAvailW - leftControlsW) / 2;

    int runX = leftStartX;
    for (int i = 0; i < leftCount; i++) {
        btnX[i] = runX;
        runX += btnW[i] + mapNavGap;
    }

    for (int i = 0; i < ctlCount; i++) {
        int bx = btnX[i];
        uint16_t fill = lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
        drawSquirclePill(bx, btnY, btnW[i], mapNavBtnH, fill, COL_SELECT_ACCENT, false);
        lcd.setFont(&fonts::Font0);
        lcd.setTextColor(COL_TEXT_MAIN, fill);
        int tw = lcd.textWidth(labels[i]);
        int tx = bx + max(1, (btnW[i] - tw) / 2);
        int ty = btnY + max(0, (mapNavBtnH - CHAR_H) / 2);
        drawClippedText(tx, ty, btnW[i] - (tx - bx) - 1, labels[i]);
        setMapControlRect((MapControlAction)i, bx, btnY, btnW[i], mapNavBtnH);
    }

    lcd.setFont(&fonts::Font0);
    mapLastDrawMs = millis();
    dirtyNodes = false;
    dirtyChat = downloadedAnyTile;
}

// ── Draw: node list ───────────────────────────────────────────
static void drawNodes() {
    drawPanelFrame(NODE_X, CHAT_Y, NODE_W, CHAT_H, COL_PANEL_BG, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

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
    lcd.setFont(&fonts::Font0);
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
    lcd.setTextSize(1);
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
    lcd.setTextSize(1);

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
    } else if (fix) {
        uint32_t ttff = gpsSearchTimeMs();
        snprintf(buf, sizeof(buf), "GPS: FIX  sats:%d  ttff:%lus", sats,
                 (unsigned long)(ttff / 1000));
        pr(COL_TEAL, buf);
    } else {
        uint32_t elapsed = gpsSearchTimeMs();
        snprintf(buf, sizeof(buf), "GPS: searching %lus...", (unsigned long)(elapsed / 1000));
        pr(COL_TAB_UNREAD, buf);
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

    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
}

// ── DM helper: open a conversation with a node ────────────────
static void openDmWith(NodeEntry *n) {
    if (!n) return;
    const char *sn = n->shortName[0] ? n->shortName : "????";
    DMs.findOrCreate(n->nodeId, sn);
    DMs.markRead(n->nodeId);
    dmConvNodeId = n->nodeId;
    dmConvOpen   = true;
    dmPickerOpen = false;
    dirtyChat = dirtyInput = true;
}

// ── Draw: DM contact list ─────────────────────────────────────
static void drawDmList() {
    clearPanelCloseRect();
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = CHAT_H - 8;
    const int ix = mx + 3;
    const int iy = my + 3;
    const int iw = mw - 6;
    const int controlsTop = my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD);
    const int rowsVisible = max(1, (controlsTop - iy - 1) / DM_LINE_H);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);
    dirtyNodes = false;

    // Rows: conversations (dmListSel 1..count), with dmListSel 0 reserved for NEW DM button.
    const int rows = min(DMs.count(), rowsVisible);
    for (int i = 0; i < rows; i++) {
        DmConv *c = DMs.getByRank(i);
        if (!c) break;

        int      y   = iy + i * DM_LINE_H;
        bool     sel = (dmListSel == i + 1);
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
    const int closeY = my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD;
    const int newW = TOUCH_BTN_W;
    const int newH = TOUCH_BTN_H;
    const int newX = closeX + newW + 4;
    const int newY = closeY;
    bool newSel = (dmListSel == 0);
    uint16_t newFill = newSel ? COL_SELECT_BG : lerp565(COL_PANEL_BG, COL_PANEL_ALT, 120);
    drawSquirclePill(newX, newY, newW, newH, newFill, COL_SELECT_ACCENT, newSel);
    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(newSel ? COL_TEXT_ON_ACCENT : COL_TEXT_MAIN, newFill);
    int ntw = lcd.textWidth("NEW DM");
    drawClippedText(newX + max(1, (newW - ntw) / 2), newY + max(0, (newH - CHAR_H) / 2), newW - 2, "NEW DM");
    setDmNewRect(newX, newY, newW, newH);

    drawPanelCloseButton(closeX, closeY, TOUCH_BTN_W, TOUCH_BTN_H);

    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
}

// ── Picker helpers: node list excluding self ──────────────────
// Returns the nth node (0-based) excluding myNodeId.
// Snapshot of node IDs taken when picker opens — prevents reordering while scrolling
static uint32_t pickerIds[MAX_NODES];
static int      pickerCount = 0;

static void pickerSnapshot() {
    pickerCount = 0;
    for (int i = 0; i < Nodes.count() && pickerCount < MAX_NODES; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (n && n->nodeId != myNodeId)
            pickerIds[pickerCount++] = n->nodeId;
    }
}

static NodeEntry *pickerNode(int sel) {
    if (sel < 0 || sel >= pickerCount) return nullptr;
    return Nodes.find(pickerIds[sel]);
}

static int pickerNodeCount() { return pickerCount; }

// ── Draw: DM node picker ──────────────────────────────────────
static void drawDmPicker() {
    clearPanelCloseRect();
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = CHAT_H - 8;
    const int ix = mx + 3;
    const int iy = my + 3;
    const int iw = mw - 6;
    const int controlsTop = my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD);
    const int rowsVisible = max(1, (controlsTop - iy - 1) / DM_LINE_H);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);
    dirtyNodes = false;

    // Header bar
    lcd.fillRect(ix, iy, iw, DM_LINE_H, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(ix + 4, iy + 1, iw - 8, "Select recipient");

    int filteredCount = pickerNodeCount();
    if (filteredCount == 0) {
        lcd.setTextColor(COL_TAB_IDLE, COL_PANEL_BG);
        drawClippedText(ix + 4, iy + 3 * DM_LINE_H + 1, iw - 8, "No other nodes known yet");
        drawPanelCloseButton(mx + 3,
                     my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                     TOUCH_BTN_W, TOUCH_BTN_H);
        lcd.setFont(&fonts::Font0);
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

    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
}

// ── Draw: DM conversation view ────────────────────────────────
static void drawDmConv() {
    clearPanelCloseRect();
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = CHAT_H - 8;
    const int ix = mx + 3;
    const int iy = my + 3;
    const int iw = mw - 6;
    const int controlsTop = my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD);
    const int rowsVisible = max(1, (controlsTop - iy - 1) / DM_LINE_H);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);
    dirtyNodes = false;

    DmConv *c = DMs.find(dmConvNodeId);
    if (!c) {
        lcd.setTextColor(TFT_RED, COL_PANEL_BG);
        drawClippedText(ix + 4, iy + DM_LINE_H + 1, iw - 8, "Conversation not found");
        drawPanelCloseButton(mx + 3,
                     my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                     TOUCH_BTN_W, TOUCH_BTN_H);
        lcd.setFont(&fonts::Font0);
        dirtyChat = false;
        return;
    }

    // Header bar
    char hdr[DM_LINE_LEN + 1];
    snprintf(hdr, sizeof(hdr), "DM with %s (!%08x)", c->shortName, (unsigned)c->nodeId);
    lcd.fillRect(ix, iy, iw, DM_LINE_H, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(ix + 4, iy + 1, iw - 8, hdr);

    // Messages (DM_VISIBLE - 1 rows below the header)
    const int MSG_ROWS = rowsVisible - 1;
    for (int row = 0; row < MSG_ROWS; row++) {
        int y = iy + (row + 1) * DM_LINE_H;
        uint16_t rowBg = (row & 1) ? COL_PANEL_BG : COL_PANEL_ALT;
        lcd.fillRect(ix, y, iw, DM_LINE_H, rowBg);
        const DmLine *dl = DMs.getLine(c, row, MSG_ROWS);
        if (!dl) continue;
        lcd.setTextColor(dl->color, rowBg);
        drawClippedText(ix + 4, y + 1, iw - 8, dl->text);
    }

    drawPanelCloseButton(mx + 3,
                         my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                         TOUCH_BTN_W, TOUCH_BTN_H);

    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
}

// ── Draw: settings page ───────────────────────────────────────
static void drawSettings() {
    clearPanelCloseRect();
    const int SH = SETTINGS_ROW_H;
    const int mx = 8;
    const int my = CHAT_Y + 4;
    const int mw = LCD_W - 16;
    const int mh = CHAT_H - 8;
    const int ix = mx + 3;
    const int iw = mw - 6;
    const int controlsTop = my + mh - (TOUCH_BTN_H + TOUCH_BTN_BOTTOM_PAD);

    drawModalMaskAndFrame(mx, my, mw, mh);
    drawPanelFrame(mx, my, mw, mh, COL_PANEL_BG, COL_SELECT_ACCENT);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

    char buf[LCD_W / CHAR_W + 2];
    int y = my + 1;

    lcd.fillRect(ix, y, iw, SETTINGS_HDR_H, COL_SELECT_BG);
    lcd.setTextColor(COL_TEXT_ON_ACCENT, COL_SELECT_BG);
    drawClippedText(ix + 4, y + (SETTINGS_HDR_H - SH) / 2 + 1, iw - 8, "Settings");

    y += SETTINGS_HDR_H;

    // ── Action buttons ────────────────────────────────────────
    for (int i = 0; i < NUM_SETTINGS; i++, y += SH) {
        if (y + SH > controlsTop - 1) break;
        bool     sel = (i == settingsSel);
        uint16_t bg  = sel ? COL_SELECT_BG : ((i & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        uint16_t fg  = sel ? COL_TEXT_ON_ACCENT : COL_TEXT_DIM;
        lcd.fillRect(ix, y, iw, SH, bg);
        lcd.setTextColor(fg, bg);
        if (i == SETTING_EXPORT)
            snprintf(buf, sizeof(buf), "Export Config");
        else if (i == SETTING_IMPORT)
            snprintf(buf, sizeof(buf), "Import Config");
        else if (i == SETTING_THEME)
            snprintf(buf, sizeof(buf), "Theme: %s", uiThemePresetName(uiThemePresetIndex()));
        else if (i == SETTING_CLEAR_NODES)
            snprintf(buf, sizeof(buf), "Clear Nodes");
        else if (i == SETTING_FACTORY_RESET)
            snprintf(buf, sizeof(buf), "Factory Reset");
        else if (webCfgRunning())
            snprintf(buf, sizeof(buf), "Web Config: %s", webCfgIP());
        else
            snprintf(buf, sizeof(buf), "Web Config: OFF");
        drawClippedText(ix + 4, y + 1, iw - 8, buf);
    }

    // ── Status line (transient feedback) ─────────────────────
    if (settingsStatus[0] && y + SH <= controlsTop - 1) {
        lcd.fillRect(ix, y, iw, SH, COL_PANEL_BG);
        lcd.setTextColor(COL_TEAL, COL_PANEL_BG);
        drawClippedText(ix + 2, y, iw - 4, settingsStatus);
    }
    y += SH;  // always advance past status slot

    // ── Separator ─────────────────────────────────────────────
    if (y + SH <= controlsTop - 1)
        lcd.drawFastHLine(ix + 2, y + SH / 2, iw - 4, COL_DIVIDER);
    y += SH;

    // ── Read-only config info ─────────────────────────────────
    const uint16_t DIM = COL_TEXT_DIM;
    int infoRow = 0;
    auto pr = [&](const char *s) {
        if (y + SH > controlsTop - 1) return;
        uint16_t bg = ((infoRow++ & 1) ? COL_PANEL_BG : COL_PANEL_ALT);
        lcd.fillRect(ix, y, iw, SH, bg);
        lcd.setTextColor(DIM, bg);
        drawClippedText(ix + 2, y, iw - 4, s);
        y += SH;
    };

    snprintf(buf, sizeof(buf), "Long Name:  %.*s", (int)(LCD_W / CHAR_W) - 7, gCfg.nodeLong);
    pr(buf);

    snprintf(buf, sizeof(buf), "Short Name: %s", gCfg.nodeShort);
    pr(buf);

    snprintf(buf, sizeof(buf), "Frequency:  %.3f MHz", gCfg.loraFreq);
    pr(buf);

    snprintf(buf, sizeof(buf), "BW:%.0f  SF:%d  CR:4/%d",
             gCfg.loraBw, gCfg.loraSf, gCfg.loraCr);
    pr(buf);

    snprintf(buf, sizeof(buf), "Pwr:%d dBm  Hops:%d",
             gCfg.loraPower, gCfg.loraHopLimit);
    pr(buf);

    drawPanelCloseButton(mx + 3,
                         my + mh - TOUCH_BTN_H - TOUCH_BTN_BOTTOM_PAD,
                         TOUCH_BTN_W, TOUCH_BTN_H);

    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
}

// ── Draw: node detail overlay ────────────────────────────────
static void drawNodeDetail(const NodeEntry *n) {
    drawPanelFrame(0, CHAT_Y, LCD_W, LCD_H - CHAT_Y, COL_PANEL_BG, COL_DIVIDER);
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

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
    lcd.setFont(&fonts::Font0);
    dirtyChat = false;
}

static bool isTextInputView() {
    bool dmNeedsInput = (activeView == CHAN_DM && dmConvOpen && !dmPickerOpen);
    return !((activeView == CHAN_ANN)
            || (activeView == CHAN_DM && !dmNeedsInput)
            || (activeView == VIEW_MAP)
            || (activeView == VIEW_GPS)
            || (activeView == VIEW_SETTINGS));
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
    const int rowH = TOUCH_BTN_H;
    const int rowBottomPad = (activeView == VIEW_MAP) ? 2 : 0;
    const int rowY = INPUT_Y + INPUT_H - rowH - rowBottomPad;
    int bw = TOUCH_BTN_W;
    int x = max(PAD, (LCD_W - (NAV_BTN_COUNT * bw + (NAV_BTN_COUNT - 1) * GAP)) / 2);
    if (x + NAV_BTN_COUNT * bw + (NAV_BTN_COUNT - 1) * GAP > LCD_W - PAD) {
        bw = (LCD_W - 2 * PAD - (NAV_BTN_COUNT - 1) * GAP) / NAV_BTN_COUNT;
        x = PAD;
    }
    for (int i = 0; i < NAV_BTN_COUNT; i++) {
        b[i] = { x, rowY, bw, rowH };
        x += bw + GAP;
    }
}

static bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh));
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
    if (!mapNodeFreezeActive) return Nodes.getByRank(idx);
    if (idx >= mapFrozenNodeCount) return nullptr;
    return Nodes.find(mapFrozenNodeIds[idx]);
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

static void handleTouchTap(int x, int y) {
    if (screenAsleep || nodeDetailOpen) return;

    if (dmNewVisible
        && pointInRect(x, y, dmNewRect.x, dmNewRect.y, dmNewRect.w, dmNewRect.h)
        && activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen) {
        dmListSel  = 0;
        dmPickerSel  = 0;
        dmPickerOpen = true;
        pickerSnapshot();
        dirtyChat = true;
        return;
    }

    if (panelCloseVisible
        && pointInRect(x, y, panelCloseRect.x, panelCloseRect.y,
                       panelCloseRect.w, panelCloseRect.h)) {
        closePanelToChannel();
        return;
    }

    if (panelClearVisible
        && pointInRect(x, y, panelClearRect.x, panelClearRect.y,
                       panelClearRect.w, panelClearRect.h)) {
        if (activeView == CHAN_ANN) {
            Channels.clearChannel(CHAN_ANN);
            dirtyTabs = true;
            dirtyChat = true;
            dirtyLiveRows = false;
        }
        return;
    }

    if (activeView == VIEW_MAP) {
        for (int i = 0; i < MAP_CTL_COUNT; i++) {
            if (!mapCtlVisible[i]) continue;
            if (pointInRect(x, y, mapCtlRect[i].x, mapCtlRect[i].y,
                            mapCtlRect[i].w, mapCtlRect[i].h)) {
                mapApplyControl((MapControlAction)i);
                return;
            }
        }
    }

    NavButtonRect b[NAV_BTN_COUNT];
    navButtonRects(b);
    if (pointInRect(x, y, b[0].x, b[0].y, b[0].w, b[0].h)) {
        goToView(prevView(activeView));
    } else if (pointInRect(x, y, b[1].x, b[1].y, b[1].w, b[1].h)) {
        if (activeView != CHAN_DM) goToView(CHAN_DM);
    } else if (pointInRect(x, y, b[2].x, b[2].y, b[2].w, b[2].h)) {
        if (activeView != VIEW_MAP) goToView(VIEW_MAP);
    } else if (pointInRect(x, y, b[3].x, b[3].y, b[3].w, b[3].h)) {
        if (activeView != CHAN_ANN) goToView(CHAN_ANN);
    } else if (pointInRect(x, y, b[4].x, b[4].y, b[4].w, b[4].h)) {
        if (activeView != VIEW_SETTINGS) goToView(VIEW_SETTINGS);
    } else if (pointInRect(x, y, b[5].x, b[5].y, b[5].w, b[5].h)) {
        goToView(nextView(activeView));
    }
}

// ── Draw: input bar ───────────────────────────────────────────
static void drawInput() {
    bool showTextInput = isTextInputView();
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
    lcd.setFont(&fonts::DejaVu9);
    lcd.setTextSize(1);

    if (showTextInput) {
        int textY = max(INPUT_Y + 2, b[0].y - lcd.fontHeight() - 2);
        int midY = min(b[0].y - 1, textY + lcd.fontHeight() + 1);
        lcd.drawFastHLine(0, midY, LCD_W, COL_DIVIDER_HI);

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
    for (int i = 0; i < NAV_BTN_COUNT; i++) {
        drawSquirclePill(b[i].x, b[i].y, b[i].w, b[i].h, btnFill, COL_TEAL, false);
    }

    // Bracket app buttons (DM / MAPS / LIVE / CFG) from outer nav buttons.
    int sepX1 = (b[0].x + b[0].w + b[1].x) / 2;
    int sepX2 = (b[4].x + b[4].w + b[5].x) / 2;
    int sepY  = b[0].y + 1;
    int sepH  = max(1, b[0].h - 2);
    // Stronger divider between "Previous" and the app group.
    lcd.fillRect(max(0, sepX1 - 1), sepY, 3, sepH, COL_SELECT_ACCENT);
    lcd.drawFastVLine(sepX1 + 1, sepY, sepH, COL_DIVIDER_HI);
    // Match right-side bracket style between "CFG" and "Next".
    lcd.fillRect(max(0, sepX2 - 1), sepY, 3, sepH, COL_SELECT_ACCENT);
    lcd.drawFastVLine(sepX2 + 1, sepY, sepH, COL_DIVIDER_HI);

    lcd.setFont(&fonts::Font0);
    lcd.setTextColor(COL_TEXT_MAIN, btnFill);
    const char *labels[NAV_BTN_COUNT] = { "Prev", "DM", "MAP", "LIVE", "CFG", "Next" };
    for (int i = 0; i < NAV_BTN_COUNT; i++) {
        int tw = lcd.textWidth(labels[i]);
        int tx = b[i].x + max(1, (b[i].w - tw) / 2);
        int ty = b[i].y + max(0, (b[i].h - CHAR_H) / 2);
        if (tw <= b[i].w - 2) lcd.drawString(labels[i], tx, ty);
        else                  drawClippedText(b[i].x + 1, ty, b[i].w - 2, labels[i]);
    }

    lcd.setFont(&fonts::Font0);
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

static void sendRoutingAck(const MeshPacket &pkt) {
    if (pkt.chanIdx < 0 || pkt.chanIdx >= MAX_CHANNELS) return;

    uint8_t proto[48], cipher[48];
    size_t protoLen = encodeRouting(pkt.hdr.id, myNodeId, proto, sizeof(proto));
    if (protoLen == 0) return;

    const ChannelKey &ck = CHANNEL_KEYS[pkt.chanIdx];
    uint32_t ackId = esp_random() ^ millis();
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
        char live[64];
        const char *dst = (pkt.hdr.to == 0xFFFFFFFF) ? "B" :
                          ((pkt.hdr.to == myNodeId) ? "M" : "U");
        snprintf(live, sizeof(live), "R %s>%s %s c%d",
                 who, dst, livePortTag(pkt.portnum), pkt.chanIdx);
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

        snprintf(prefix, sizeof(prefix), "%s[%s] ", timePrefix, sender);

        if (pkt.hdr.to == myNodeId) {
            // Unicast DM addressed to us
            bool viewing = (activeView == CHAN_DM && dmConvOpen
                            && dmConvNodeId == pkt.hdr.from);
            DMs.addMessage(pkt.hdr.from, sender, prefix, tm.text, COL_TEAL,
                           /*markUnread=*/!viewing, pkt.chanIdx);
            // If we're on the DM list (not inside a conv), jump straight into this one
            if (activeView == CHAN_DM && !dmConvOpen && !dmPickerOpen) {
                DMs.markRead(pkt.hdr.from);
                dmConvNodeId = pkt.hdr.from;
                dmConvOpen   = true;
                viewing      = true;
            }
            if (viewing) dirtyChat = true;
            dirtyTabs = true;
        } else {
            // Broadcast / relay message — goes to channel
            Channels.addMessage(pkt.chanIdx, prefix, tm.text, COL_TEAL);
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
        char info[60];
        snprintf(info, sizeof(info), "%s (%s) identified.", u.longName, u.shortName);
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
            if (isAck) {
                Channels.setAckStateFrom(pkt.requestId, pkt.hdr.from);
                if (debugAcksEnabled()) {
                    Serial.printf("[rx] ACK for 0x%08X from !%08X\n", pkt.requestId, pkt.hdr.from);
                }
            } else {
                Channels.setAckState(pkt.requestId, DisplayLine::NAKED);
                debugLogAcks("[rx] NAK for 0x%08X err=%lu from !%08X\n",
                             pkt.requestId, (unsigned long)errorReason, pkt.hdr.from);

                // PKI_UNKNOWN_PUBKEY (35): sender doesn't have our public key yet.
                // Respond with a unicast NODEINFO so they can retry with PKI.
                if (errorReason == 35) {
                    debugLogAcks("[nak] PKI_UNKNOWN_PUBKEY - sending NODEINFO to !%08X\n", pkt.hdr.from);
                    Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort, pkt.hdr.from);
                    NodeEntry *n = Nodes.find(pkt.hdr.from);
                    if (n) n->lastSentInfoMs = millis();
                }

                // Show error in the DM conversation with the NAK sender (not just the open conv)
                DmConv *conv = DMs.find(pkt.hdr.from);
                if (conv) {
                    char errMsg[32];
                    snprintf(errMsg, sizeof(errMsg), "! NAK err=%lu", (unsigned long)errorReason);
                    DMs.addMessage(pkt.hdr.from, nullptr, "", errMsg, TFT_RED);
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

// ── Handle keyboard input ─────────────────────────────────────
static void handleKey(char k) {
    if (k == KEY_NONE) return;

    // ALT+E — toggle node list focus / close detail; close DM sub-views
    if (k == KEY_NODE_FOCUS) {
        if (activeView == CHAN_DM) {
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
        if (activeView == CHAN_DM) {
            if (dmPickerOpen) {
                openDmWith(pickerNode(dmPickerSel));
            } else if (!dmConvOpen) {
                if (dmListSel == 0) {
                    // "New DM" button
                    dmPickerSel  = 0;
                    dmPickerOpen = true;
                    pickerSnapshot();
                    dirtyChat = true;
                } else {
                    DmConv *c = DMs.getByRank(dmListSel - 1);
                    if (c) {
                        DMs.markRead(c->nodeId);
                        dmConvNodeId = c->nodeId;
                        dmConvOpen   = true;
                        dirtyChat = dirtyInput = dirtyTabs = true;
                    }
                }
            } else {
                // Conv view: ENTER sends the message
                if (inputLen > 0) {
                    inputBuf[inputLen] = '\0';
                    if (!DMs.sendDm(myNodeId, dmConvNodeId, inputBuf)) {
                        // TX failed — add a local error line so the user knows
                        DMs.addMessage(dmConvNodeId, nullptr, "", "! TX failed", TFT_RED);
                    }
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
        if (activeView == VIEW_SETTINGS) {
            if (settingsSel == SETTING_EXPORT) {
                bool ok = cfgExport(gCfg);
                snprintf(settingsStatus, sizeof(settingsStatus),
                         ok ? "Exported to /camillia/config.yaml" : "Export FAILED (no SD?)");
            } else if (settingsSel == SETTING_IMPORT) {
                bool ok = cfgImport(gCfg);
                if (ok) {
                                        onWebCfgSaved();
                    snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK — rebooting...");
                    dirtyChat = true;
                    drawSettings();
                    delay(1000);
                    ESP.restart();
                } else {
                    snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
                }
            } else if (settingsSel == SETTING_THEME) {
                uint8_t p = (uint8_t)((uiThemePresetIndex() + 1) % 6);
                setUiThemePreset(p);
                applyUiTheme();
                persistUiTheme();
                snprintf(settingsStatus, sizeof(settingsStatus), "Theme: %s", uiThemePresetName(p));
            } else if (settingsSel == SETTING_CLEAR_NODES) {
                Nodes.clearPersisted();
                snprintf(settingsStatus, sizeof(settingsStatus), "Node DB cleared — rebooting...");
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
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
                snprintf(settingsStatus, sizeof(settingsStatus), "Factory reset — rebooting...");
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
            }
            dirtyChat = true;
        } else if (inputLen > 0 && activeView != CHAN_ANN && activeView != CHAN_DM
               && activeView != VIEW_MAP && activeView != VIEW_GPS && activeView != VIEW_SETTINGS) {
            inputBuf[inputLen] = '\0';
            int txChan = (activeView >= 0 && activeView < MESH_CHANNELS)
                         ? activeView : Channels.activeIdx();
            if (!Channels.sendText(myNodeId, inputBuf, gCfg.okToMqtt, txChan)) {
                Channels.addMessage(txChan, "",
                    "! TX failed", TFT_RED, 0);
                dirtyChat = true;
            }
            inputLen = 0; inputBuf[0] = '\0';
            dirtyInput = dirtyChat = true;
        }

    } else if (k == KEY_BACKSPACE) {
        bool textAllowed = (activeView != CHAN_ANN && activeView != VIEW_SETTINGS
                            && activeView != VIEW_MAP
                            && activeView != VIEW_GPS
                            && !(activeView == CHAN_DM && (!dmConvOpen || dmPickerOpen)));
        if (inputLen > 0 && textAllowed) {
            inputBuf[--inputLen] = '\0'; dirtyInput = true;
        }

    } else if (k == KEY_TAB || k == KEY_NEXT_CHAN || k == KEY_ROLLER) {
        if (activeView == CHAN_DM) {
            if (dmPickerOpen) {
                if (k == KEY_ROLLER) {
                    openDmWith(pickerNode(dmPickerSel));
                } else {
                    // Tab/right closes picker, back to list
                    dmPickerOpen = false;
                    dirtyChat = true;
                }
            } else if (dmConvOpen) {
                if (k == KEY_ROLLER && inputLen > 0) {
                    // Trackball click with text typed → send (same as Enter)
                    inputBuf[inputLen] = '\0';
                    if (!DMs.sendDm(myNodeId, dmConvNodeId, inputBuf)) {
                        DMs.addMessage(dmConvNodeId, nullptr, "", "! TX failed", TFT_RED);
                    }
                    inputLen = 0; inputBuf[0] = '\0';
                    dirtyInput = dirtyChat = true;
                } else {
                    // Tab/right/roller (empty) → back to contact list
                    dmConvOpen = false;
                    dirtyChat = dirtyInput = true;
                }
            } else if (k == KEY_ROLLER) {
                // Roller on list item: "New DM" or open conv
                if (dmListSel == 0) {
                    dmPickerSel  = 0;
                    dmPickerOpen = true;
                    pickerSnapshot();
                    dirtyChat = true;
                } else {
                    DmConv *c = DMs.getByRank(dmListSel - 1);
                    if (c) {
                        DMs.markRead(c->nodeId);
                        dmConvNodeId = c->nodeId;
                        dmConvOpen   = true;
                        dirtyChat = dirtyInput = dirtyTabs = true;
                    }
                }
            } else {
                // Tab/right from list → cycle forward
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
            if (settingsSel == SETTING_EXPORT) {
                bool ok = cfgExport(gCfg);
                snprintf(settingsStatus, sizeof(settingsStatus),
                         ok ? "Exported to /camillia/config.yaml" : "Export FAILED (no SD?)");
            } else if (settingsSel == SETTING_IMPORT) {
                bool ok = cfgImport(gCfg);
                if (ok) {
                                        onWebCfgSaved();
                    snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK — rebooting...");
                    dirtyChat = true;
                    drawSettings();
                    delay(1000);
                    ESP.restart();
                } else {
                    snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
                }
            } else if (settingsSel == SETTING_THEME) {
                uint8_t p = (uint8_t)((uiThemePresetIndex() + 1) % 6);
                setUiThemePreset(p);
                applyUiTheme();
                persistUiTheme();
                snprintf(settingsStatus, sizeof(settingsStatus), "Theme: %s", uiThemePresetName(p));
            } else if (settingsSel == SETTING_CLEAR_NODES) {
                Nodes.clearPersisted();
                snprintf(settingsStatus, sizeof(settingsStatus), "Node DB cleared — rebooting...");
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
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
                snprintf(settingsStatus, sizeof(settingsStatus), "Factory reset — rebooting...");
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
            }
            dirtyChat = true;
        } else if (activeView != VIEW_SETTINGS || settingsSel == 0) {
            goToView(nextView(activeView));
        }

    } else if (k == KEY_PREV_CHAN) {
        if (activeView == CHAN_DM) {
            if      (dmPickerOpen) { dmPickerOpen = false; dirtyChat = true; return; }
            else if (dmConvOpen)   { dmConvOpen = false; dirtyChat = dirtyInput = true; return; }
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
                    int maxOff = total - (DM_VISIBLE - 1);
                    c->scrollOff = min(c->scrollOff + 3, maxOff > 0 ? maxOff : 0);
                    dirtyChat = true;
                }
            } else {
                // List: 0 = "New DM" button, 1..count = convs
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
        } else if (activeView == VIEW_SETTINGS) {
            settingsSel = max(0, settingsSel - 1);
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
                // List: max is DMs.count() (last conv), 0 is the button
                int cap = DMs.count();
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
        } else if (activeView == VIEW_SETTINGS) {
            settingsSel = min(NUM_SETTINGS - 1, settingsSel + 1);
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
        bool textAllowed = (activeView != CHAN_ANN && activeView != VIEW_SETTINGS
                            && activeView != VIEW_MAP
                            && activeView != VIEW_GPS
                            && !(activeView == CHAN_DM && (!dmConvOpen || dmPickerOpen)));
        if (inputLen < MAX_INPUT_LEN && textAllowed) {
            inputBuf[inputLen++] = k;
            inputBuf[inputLen]   = '\0';
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
    p.putString("region",     gCfg.region);
    p.putString("tzDef",      gCfg.tzDef);
    p.putULong("screenOnSecs",gCfg.screenOnSecs);
    p.putUChar("dispUnits",   gCfg.displayUnits);
    p.putBool("compassNorth", gCfg.compassNorthTop);
    p.putBool("flipScreen",   gCfg.flipScreen);
    p.putUChar("uiTheme",     gCfg.uiTheme);
    p.putUChar("uiMode",      gCfg.uiMode);
    p.putBool("btEnabled",    gCfg.btEnabled);
    p.putUChar("btMode",      gCfg.btMode);
    p.putULong("btFixedPin",  gCfg.btFixedPin);
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
        String rgn = prefs.getString("region", ""); if (rgn.length()) strncpy(gCfg.region, rgn.c_str(), sizeof(gCfg.region)-1);
        String tz = prefs.getString("tzDef", ""); if (tz.length()) strncpy(gCfg.tzDef, tz.c_str(), sizeof(gCfg.tzDef)-1);
        ul = prefs.getULong("screenOnSecs", 0);
        Serial.printf("[cfg] loaded screenOnSecs=%lu (isKey=%d)\n",
                      (unsigned long)ul, prefs.isKey("screenOnSecs") ? 1 : 0);
        if (ul) gCfg.screenOnSecs = ul;
        ro = prefs.getUChar("dispUnits", 0xFF); if (ro != 0xFF) gCfg.displayUnits = ro;
        if (prefs.isKey("compassNorth")) gCfg.compassNorthTop = prefs.getBool("compassNorth");
        if (prefs.isKey("flipScreen"))   gCfg.flipScreen      = prefs.getBool("flipScreen");
        ro = prefs.getUChar("uiTheme", 0xFF); if (ro != 0xFF && ro < UI_THEME_COUNT) gCfg.uiTheme = ro;
        ro = prefs.getUChar("uiMode", 0xFF);  if (ro != 0xFF && ro <= UI_MODE_LIGHT) gCfg.uiMode = ro;
        if (prefs.isKey("btEnabled"))    gCfg.btEnabled       = prefs.getBool("btEnabled");
        ro = prefs.getUChar("btMode", 0xFF); if (ro != 0xFF) gCfg.btMode = ro;
        ul = prefs.getULong("btFixedPin", 0); if (ul) gCfg.btFixedPin = ul;
        // MQTT/NTP keys removed — no longer loaded
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
    // GPIO 4 is battery ADC on T-Deck — do NOT drive as output
    analogSetPinAttenuation(BATT_ADC_PIN, ADC_11db);   // 0-3.3 V range

    // Display
    lcd.init();
    lcd.setRotation(1);
    lcd.setBrightness(128);
    lcd.fillScreen(COL_BG_MAIN);
    lcd.setTextSize(1);
    lastActivityMs = millis();

    // Splash
    drawSplash();

    // Keyboard
    kb.begin();

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

    // Auto-start web config (TODO: gate on a setting or button before production)
    if (webCfgBegin(&gCfg, onWebCfgSaved)) {
        if (webCfgIsOnboarding())
            snprintf(settingsStatus, sizeof(settingsStatus), "Setup: %s", webCfgIP());
        else
            snprintf(settingsStatus, sizeof(settingsStatus), "Web: %s", webCfgIP());
        Channels.addMessage(CHAN_ANN, "", settingsStatus, TFT_DARKGREY);
    }

    // Initial full draw
    drawDivider();
    dirtyStatus = dirtyTabs = dirtyChat = dirtyNodes = dirtyInput = true;
}

// Poll touch + keyboard; called multiple times per loop to avoid drops
static void pollInput() {
    uint32_t now = millis();
    char tb = kb.readTrackball();

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
        }
        touchLastX = tx;
        touchLastY = ty;
    } else if (touchDown) {
        uint32_t tapHoldLimit = (activeView == VIEW_MAP) ? 2500 : 450;
        int driftLimit = (activeView == VIEW_MAP) ? 26 : 18;
        bool shortTap = (now - touchDownMs) <= tapHoldLimit;
        bool steady   = (abs(touchLastX - touchStartX) <= driftLimit)
                     && (abs(touchLastY - touchStartY) <= driftLimit);
        if (shortTap && steady) {
            int tapX = (activeView == VIEW_MAP) ? touchLastX : (touchStartX + touchLastX) / 2;
            int tapY = (activeView == VIEW_MAP) ? touchLastY : (touchStartY + touchLastY) / 2;
            handleTouchTap(tapX, tapY);
        }
        touchDown = false;
    }

    // Pull fresh keyboard bytes, then consume queued keys.
    pumpKeyboardRaw(24, now);
    for (int ki = 0; ki < 24; ki++) {
        char k;
        if (!dequeueKey(k)) break;
        handleKey(k);
    }
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    // Poll input first — keyboard MCU has tiny buffer
    pollInput();

    // 1. Poll radio
    MeshPacket pkt;
    if (Radio.pollRx(pkt)) {
        handleRx(pkt);
        pollInput();   // poll again after heavy packet processing
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

    // 1c. Poll GPS
    gpsLoop();

    uint32_t now = millis();

    // Pull wall clock from GPS when available: fast retries before first sync,
    // then occasional drift correction.
    static bool clockEverSynced = false;
    static uint32_t lastClockSyncAttemptMs = 0;
    uint32_t syncPeriodMs = clockEverSynced ? 300000UL : 5000UL;
    if (now - lastClockSyncAttemptMs >= syncPeriodMs) {
        lastClockSyncAttemptMs = now;
        if (gpsSyncSystemClock()) {
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
        cursorOn  = !cursorOn;
        lastBlink = now;
        dirtyInput = true;
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
    if (activeView == VIEW_MAP && !nodeDetailOpen) {
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
        lcd.setBrightness(0);
        screenAsleep = true;
    }

    // 7. Redraw dirty zones (skip while screen is off)
    if (screenAsleep) return;
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
