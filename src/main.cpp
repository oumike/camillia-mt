#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <SD.h>
#include "lgfx_tdeck.h"
#include "keyboard.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "node_db.h"
#include "channel_mgr.h"
#include "config_io.h"
#include "web_config.h"
#include "gps.h"
#include "dm_mgr.h"
#include <math.h>
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"

// ── Chat spacing (runtime, set once at startup from gCfg.chatSpacing) ─
int LINE_H        = 8;     // default Normal
int VISIBLE_LINES = CHAT_H / 8;

static const int kSpacingPx[] = { 6, 8, 10 };  // Tight, Normal, Loose

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
#define VIEW_GPS      MAX_CHANNELS          // index 9 = GPS/compass page
#define VIEW_SETTINGS (MAX_CHANNELS + 1)    // index 10 = settings page
#define TOTAL_VIEWS   (MAX_CHANNELS + 2)    // 11 total
static int  activeView   = 0;              // 0..8 channel, 9 GPS, 10 settings
static int  tabScrollX   = 0;             // horizontal scroll offset for tab bar (px)
static int  settingsSel  = 0;         // highlighted settings row

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
#define SETTING_CHAT_SPACE    3
#define SETTING_FACTORY_RESET 4
#define NUM_SETTINGS          5

static char settingsStatus[LCD_W / CHAR_W + 1] = "";

// ── Node list focus / detail ───────────────────────────────────
static bool     nodeListFocused = false;
static int      nodeListSel     = 0;
static bool     nodeDetailOpen  = false;
static uint32_t nodeDetailId    = 0;

// ── View navigation helpers ───────────────────────────────────
static bool isViewNavigable(int v) {
    if (v >= 0 && v < MESH_CHANNELS)
        return CHANNEL_KEYS[v].name[0] != '\0';
    return true;  // CHAN_ANN, VIEW_GPS, VIEW_SETTINGS always reachable
}

static int nextView(int from) {
    for (int n = 1; n < TOTAL_VIEWS; n++) {
        int v = (from + n) % TOTAL_VIEWS;
        if (isViewNavigable(v)) return v;
    }
    return from;
}

static int prevView(int from) {
    for (int n = 1; n < TOTAL_VIEWS; n++) {
        int v = (from + TOTAL_VIEWS - n) % TOTAL_VIEWS;
        if (isViewNavigable(v)) return v;
    }
    return from;
}

// ── View navigation ───────────────────────────────────────────
static void goToView(int v) {
    bool wasFullWidth = (activeView == VIEW_SETTINGS || activeView == VIEW_GPS
                         || activeView == CHAN_DM);
    activeView = v;
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
    if (v < MESH_CHANNELS || v == CHAN_ANN) {
        Channels.setActive(v);
        if (wasFullWidth) dirtyDivider = true;   // restore divider after leaving full-width views
    }
    dirtyTabs = dirtyChat = dirtyNodes = dirtyStatus = dirtyInput = true;
}

// ── Splash screen ─────────────────────────────────────────────
static void drawSplash() {
    const uint16_t BG      = TFT_BLACK;
    const uint16_t ACCENT  = 0x0618;  // COL_TEAL (#00C0C0)
    const uint16_t TITLE   = TFT_CYAN;
    const uint16_t SUB     = TFT_WHITE;
    const uint16_t DIM     = 0x7BEF;  // mid-grey

    lcd.fillScreen(BG);

    // Border frame — thick top/bottom bars, thin side lines
    lcd.fillRect(0,         0,         LCD_W, 5,          ACCENT);
    lcd.fillRect(0,         LCD_H - 5, LCD_W, 5,          ACCENT);
    lcd.fillRect(0,         5,         2,     LCD_H - 10, ACCENT);
    lcd.fillRect(LCD_W - 2, 5,         2,     LCD_H - 10, ACCENT);

    // App name — large cyan, centred  (textSize 4 → 24×32 px per char)
    lcd.setTextSize(4);
    lcd.setTextColor(TITLE, BG);
    const char *appName = "Camillia MT";
    int tw = strlen(appName) * 6 * 4;  // 6px base × scale
    lcd.setCursor((LCD_W - tw) / 2, 52);
    lcd.print(appName);

    // Teal rule under title
    lcd.fillRect(20, 98, LCD_W - 40, 2, ACCENT);

    // Subtitle — size 2 (12×16 px per char), properly centred
    lcd.setTextSize(2);
    lcd.setTextColor(SUB, BG);
    const char *sub = "Meshtastic Client";
    tw = strlen(sub) * 6 * 2;
    lcd.setCursor((LCD_W - tw) / 2, 108);
    lcd.print(sub);

    const char *sub2 = "for T-Deck";
    tw = strlen(sub2) * 6 * 2;
    lcd.setCursor((LCD_W - tw) / 2, 126);
    lcd.print(sub2);

    // Thin teal rule above node info
    lcd.fillRect(40, 152, LCD_W - 80, 1, DIM);

    // Node long name — size 1
    lcd.setTextSize(1);
    lcd.setTextColor(SUB, BG);
    tw = strlen(gCfg.nodeLong) * 6;
    lcd.setCursor((LCD_W - tw) / 2, 160);
    lcd.print(gCfg.nodeLong);

    // Node ID + short name
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "!%08x  %s", myNodeId, gCfg.nodeShort);
    lcd.setTextColor(DIM, BG);
    tw = strlen(idBuf) * 6;
    lcd.setCursor((LCD_W - tw) / 2, 171);
    lcd.print(idBuf);

    // Version — bottom centre, dim
    const char *ver = "v0.0.8";
    tw = strlen(ver) * 6;
    lcd.setTextColor(DIM, BG);
    lcd.setCursor((LCD_W - tw) / 2, 210);
    lcd.print(ver);

    delay(2000);
    lcd.fillScreen(BG);
}

// ── Colour helpers ────────────────────────────────────────────
#define COL_STATUS_BG   0x0009   // dark navy
#define COL_TAB_ACTIVE  TFT_WHITE
#define COL_TAB_UNREAD  TFT_YELLOW
#define COL_TAB_IDLE    0x7BEF   // mid-grey
#define COL_DIVIDER     0x39E7   // dark grey
#define COL_INPUT_BG    0x0010   // very dark blue
#define COL_TEAL        0x0618   // #00C0C0 — active chat / incoming messages
#define COL_CURSOR      COL_TEAL

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

// ── Draw: battery widget ──────────────────────────────────────
// Drawn in status bar over the node pane column (x=NODE_X..LCD_W-1, y=0..STATUS_H-1)
static void drawBattery() {
    const uint16_t bg  = COL_STATUS_BG;
    uint16_t col = _battPct >= 50 ? TFT_GREEN :
                   _battPct >= 20 ? TFT_YELLOW : TFT_RED;

    // Widget geometry, centred in the 89-px node-pane header
    // layout: [XX%] [battery body][nub]
    const int BAR_W = 26, BAR_H = 8;
    const int NUB_W =  3, NUB_H = 4;
    const int TXT_W =  3 * CHAR_W;   // "XX%" — 3 chars max displayed
    const int GAP   =  2;
    const int TOTAL = TXT_W + GAP + BAR_W + NUB_W;   // 49 px
    const int X0    = NODE_X + (NODE_W - TOTAL) / 2;  // centre in pane
    const int BY    = 1;                               // body top (1px padding)

    int TX  = X0;
    int BX  = TX + TXT_W + GAP;
    int NX  = BX + BAR_W;
    int NY  = BY + (BAR_H - NUB_H) / 2;

    // Clear region
    lcd.fillRect(NODE_X, 0, NODE_W, STATUS_H, bg);

    // Percent text  (right-aligned in TXT_W)
    char tbuf[5];
    snprintf(tbuf, sizeof(tbuf), "%2d%%", _battPct);
    lcd.setTextSize(1);
    lcd.setTextColor(col, bg);
    lcd.setCursor(TX, BY);
    lcd.print(tbuf);

    // Battery body outline
    lcd.drawRect(BX, BY, BAR_W, BAR_H, col);

    // Nub
    lcd.fillRect(NX, NY, NUB_W, NUB_H, col);

    // Inner fill
    int fillW = (BAR_W - 2) * _battPct / 100;
    lcd.fillRect(BX + 1, BY + 1, BAR_W - 2, BAR_H - 2, bg);
    if (fillW > 0)
        lcd.fillRect(BX + 1, BY + 1, fillW, BAR_H - 2, col);
}

// ── Draw: status bar ─────────────────────────────────────────
static void drawStatus() {
    lcd.fillRect(0, 0, LCD_W, STATUS_H, COL_STATUS_BG);
    lcd.setTextColor(TFT_WHITE, COL_STATUS_BG);
    lcd.setTextSize(1);
    char buf[56];
    uint32_t upSec = millis() / 1000;
    snprintf(buf, sizeof(buf), "%-8s 906.9MHz %-9s #%lu  %02lu:%02lu",
             gCfg.nodeShort, Channels.get(Channels.activeIdx()).name,
             pktCount, (upSec / 3600) % 24, (upSec / 60) % 60);
    lcd.setCursor(1, 1);
    lcd.print(buf);
    drawBattery();
    dirtyStatus = false;
}

// ── Draw: tab bar ─────────────────────────────────────────────
static void drawTabs() {
    lcd.fillRect(0, STATUS_H, LCD_W, TAB_H, TFT_BLACK);
    lcd.setTextSize(1);

    // Build full tab list with absolute x positions
    struct TabEntry { int view; char label[16]; int x; int w; };
    TabEntry tabs[TOTAL_VIEWS];
    int tabCount = 0;
    int xCursor  = 0;

    for (int i = 0; i < TOTAL_VIEWS; i++) {
        if (!isViewNavigable(i)) continue;
        char label[16] = {};
        if      (i == VIEW_SETTINGS) strncpy(label, "CFG", sizeof(label));
        else if (i == VIEW_GPS)      strncpy(label, "GPS", sizeof(label));
        else if (i == CHAN_ANN)      strncpy(label, "ANN", sizeof(label));
        else                         strncpy(label, CHANNEL_KEYS[i].name, sizeof(label) - 1);
        int w = (int)strlen(label) * CHAR_W + 4;  // 2 px padding each side
        tabs[tabCount] = { i, {}, xCursor, w };
        strncpy(tabs[tabCount].label, label, sizeof(label));
        tabCount++;
        xCursor += w;
    }

    // Auto-scroll so the active tab is always fully visible
    for (int t = 0; t < tabCount; t++) {
        if (tabs[t].view != activeView) continue;
        if (tabs[t].x < tabScrollX)
            tabScrollX = tabs[t].x;
        else if (tabs[t].x + tabs[t].w > tabScrollX + LCD_W)
            tabScrollX = tabs[t].x + tabs[t].w - LCD_W;
        break;
    }

    // Render only tabs that intersect the visible window
    for (int t = 0; t < tabCount; t++) {
        int sx = tabs[t].x - tabScrollX;
        if (sx + tabs[t].w <= 0) continue;
        if (sx >= LCD_W)         break;

        bool isActive = (tabs[t].view == activeView);
        uint16_t col;
        if (tabs[t].view == VIEW_SETTINGS) {
            col = isActive ? TFT_WHITE : COL_TAB_IDLE;
        } else if (tabs[t].view == VIEW_GPS) {
            if      (isActive)       col = TFT_WHITE;
            else if (gpsHasFix())    col = TFT_GREEN;
            else if (gpsIsEnabled()) col = TFT_YELLOW;
            else                     col = COL_TAB_IDLE;
        } else if (tabs[t].view == CHAN_ANN) {
            Channel &ch = Channels.get(tabs[t].view);
            col = (ch.unread || isActive) ? TFT_CYAN : COL_TAB_IDLE;
        } else {
            Channel &ch = Channels.get(tabs[t].view);
            col = ch.unread ? COL_TAB_UNREAD :
                  isActive  ? COL_TAB_ACTIVE  : COL_TAB_IDLE;
        }
        if (isActive) lcd.fillRect(sx, STATUS_H, tabs[t].w - 1, TAB_H, 0x0013);
        lcd.setTextColor(col, isActive ? 0x0013 : TFT_BLACK);
        lcd.setCursor(sx + 2, STATUS_H + 1);
        lcd.print(tabs[t].label);
    }
    dirtyTabs = false;
}

// ── Draw: vertical divider ────────────────────────────────────
static void drawDivider() {
    lcd.fillRect(DIVIDER_X, CHAT_Y, 1, CHAT_H, COL_DIVIDER);
}

// ── Draw: message area ────────────────────────────────────────
static void drawChat() {
    lcd.fillRect(0, CHAT_Y, MSG_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);

    int active = Channels.activeIdx();
    for (int row = 0; row < VISIBLE_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(active, row);
        if (!dl) continue;

        int y = CHAT_Y + row * LINE_H;
        uint16_t col = dl->color;

        // Override color for sent messages based on ACK state
        if (dl->packetId) {
            switch (dl->ack) {
                case DisplayLine::ACKED:       col = TFT_GREEN;  break;
                case DisplayLine::ACKED_RELAY: col = TFT_YELLOW; break;
                case DisplayLine::NAKED:       col = TFT_RED;    break;
                default: break;  // NONE / PENDING keep original color
            }
        }

        lcd.setTextColor(col, TFT_BLACK);
        lcd.setCursor(0, y);
        lcd.print(dl->text);
    }

    // Scroll indicator: show arrow when not at bottom
    Channel &ch = Channels.get(active);
    if (ch.scrollOff > 0) {
        lcd.setTextColor(COL_TEAL, TFT_BLACK);
        lcd.setCursor(MSG_W - CHAR_W * 3, CHAT_Y + CHAT_H - LINE_H);
        lcd.print(" v ");
    }

    dirtyChat = false;
}

// ── Draw: node list ───────────────────────────────────────────
static void drawNodes() {
    lcd.fillRect(NODE_X, CHAT_Y, NODE_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);

    const int MAX_VISIBLE = CHAT_H / LINE_H;  // 29
    uint32_t now = millis();

    for (int i = 0; i < MAX_VISIBLE; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n) break;

        int      y   = CHAT_Y + i * LINE_H;
        bool     sel = nodeListFocused && (i == nodeListSel);
        uint16_t bg  = sel ? 0x0013 : TFT_BLACK;
        uint32_t age = now - n->lastHeardMs;
        uint16_t col = sel               ? TFT_WHITE    :
                       (age < 60000UL)   ? TFT_CYAN     :
                       (age < 3600000UL) ? TFT_WHITE    : COL_TAB_IDLE;

        lcd.fillRect(NODE_X, y, NODE_W, LINE_H, bg);

        char r[NODE_CHARS + 1];
        if (n->hasTelemetry && n->battPct > 0)
            snprintf(r, sizeof(r), "%-4s %d %+.0f %d%%",
                     n->shortName, n->hops, n->snr, (int)n->battPct);
        else
            snprintf(r, sizeof(r), "%-4s %d %+.0f",
                     n->shortName, n->hops, n->snr);

        lcd.setTextColor(col, bg);
        lcd.setCursor(NODE_X, y);
        lcd.print(r);
    }
    dirtyNodes = false;
}

// ── Draw: GPS / compass page ──────────────────────────────────
static void drawCompassRose(int cx, int cy, int cr, float heading) {
    // Outer ring
    lcd.drawCircle(cx, cy, cr,     COL_DIVIDER);
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
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setCursor(cx - 3, cy - cr + 1);  lcd.print("N");
    lcd.setTextColor(COL_TAB_IDLE, TFT_BLACK);
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
    lcd.fillCircle(cx, cy, 3, TFT_WHITE);
}

static void drawGps() {
    lcd.fillRect(0, CHAT_Y, LCD_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);

    const bool    fix     = gpsHasFix();
    const uint8_t sats    = gpsSats();
    const float   course  = gpsCourse();
    const float   speed   = gpsSpeedKmh();
    const int32_t latI    = fix ? gpsLatI()  : gCfg.latI;
    const int32_t lonI    = fix ? gpsLonI()  : gCfg.lonI;
    const int32_t altM    = fix ? gpsAltM()  : gCfg.alt;

    char buf[40];
    const int TX  = 3;            // left margin
    const int DIM = 0x7BEF;      // mid-grey

    // ── Left panel: text rows ─────────────────────────────────
    const int GH = 10;  // GPS panel always uses Loose spacing
    int row = 0;
    auto pr = [&](uint16_t col, const char *s) {
        lcd.setTextColor(col, TFT_BLACK);
        lcd.setCursor(TX, CHAT_Y + row++ * GH);
        lcd.print(s);
    };

    // Status
    if (!gCfg.gpsEnabled) {
        pr(COL_TAB_IDLE, "GPS: DISABLED");
    } else if (fix) {
        snprintf(buf, sizeof(buf), "GPS: FIX  sats:%d", sats);
        pr(COL_TEAL, buf);
    } else {
        snprintf(buf, sizeof(buf), "GPS: searching  sats:%d", sats);
        pr(TFT_YELLOW, buf);
    }
    row++;  // blank line

    // Latitude
    float lat = latI * 1e-7f;
    snprintf(buf, sizeof(buf), "Lat  %10.6f %c",
             lat >= 0 ? lat : -lat, lat >= 0 ? 'N' : 'S');
    pr(fix ? TFT_WHITE : (uint16_t)DIM, buf);

    // Longitude
    float lon = lonI * 1e-7f;
    snprintf(buf, sizeof(buf), "Lon  %10.6f %c",
             lon >= 0 ? lon : -lon, lon >= 0 ? 'E' : 'W');
    pr(fix ? TFT_WHITE : (uint16_t)DIM, buf);

    // Altitude
    snprintf(buf, sizeof(buf), "Alt  %d m", (int)altM);
    pr(fix ? TFT_WHITE : (uint16_t)DIM, buf);

    row++;  // blank line

    // Course / speed
    snprintf(buf, sizeof(buf), "Hdg  %.1f\xb0", course);
    pr(fix ? TFT_CYAN : (uint16_t)DIM, buf);

    snprintf(buf, sizeof(buf), "Spd  %.1f km/h", speed);
    pr(fix ? TFT_CYAN : (uint16_t)DIM, buf);

    row++;  // blank line

    // Fallback notice when no fix
    if (!fix && gCfg.gpsEnabled) {
        pr(COL_TAB_IDLE, "(showing stored pos)");
    }

    // ── Right panel: compass ──────────────────────────────────
    const int CX = 240;
    const int CY = CHAT_Y + CHAT_H / 2 - 4;   // vertically centred
    const int CR = 68;
    drawCompassRose(CX, CY, CR, fix ? course : 0.0f);

    // Heading value below compass
    snprintf(buf, sizeof(buf), "%3.0f\xb0", fix ? course : 0.0f);
    int tw = strlen(buf) * CHAR_W;
    lcd.setTextColor(fix ? TFT_WHITE : (uint16_t)COL_TAB_IDLE, TFT_BLACK);
    lcd.setCursor(CX - tw / 2, CY + CR + 4);
    lcd.print(buf);

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
    lcd.fillRect(0, CHAT_Y, LCD_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);
    dirtyNodes = false;

    // Row 0: "New DM" button (always present)
    {
        bool     sel = (dmListSel == 0);
        uint16_t bg  = sel ? 0x0013 : TFT_BLACK;
        uint16_t col = sel ? TFT_WHITE : TFT_CYAN;
        lcd.fillRect(0, CHAT_Y, LCD_W, DM_LINE_H, bg);
        lcd.setTextColor(col, bg);
        lcd.setCursor(0, CHAT_Y);
        lcd.print("  [ + New DM ]");
    }

    // Rows 1..: conversations (dmListSel 1..count)
    const int rows = min(DMs.count(), DM_VISIBLE - 1);
    for (int i = 0; i < rows; i++) {
        DmConv *c = DMs.getByRank(i);
        if (!c) break;

        int      y   = CHAT_Y + (i + 1) * DM_LINE_H;
        bool     sel = (dmListSel == i + 1);
        uint16_t bg  = sel ? 0x0013 : TFT_BLACK;
        uint16_t col = sel ? TFT_WHITE
                           : c->unread ? TFT_YELLOW : (uint16_t)0xF81F;

        lcd.fillRect(0, y, LCD_W, DM_LINE_H, bg);

        char row[DM_LINE_LEN + 1];
        snprintf(row, sizeof(row), " [%-4s] %.43s", c->shortName, c->lastText);

        lcd.setTextColor(col, bg);
        lcd.setCursor(0, y);
        lcd.print(row);
    }
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
    lcd.fillRect(0, CHAT_Y, LCD_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);
    dirtyNodes = false;

    // Header bar
    lcd.fillRect(0, CHAT_Y, LCD_W, DM_LINE_H, 0x001F);
    lcd.setTextColor(TFT_WHITE, 0x001F);
    lcd.setCursor(0, CHAT_Y);
    lcd.print("  Select a node  (roll left to cancel)");

    int filteredCount = pickerNodeCount();
    if (filteredCount == 0) {
        lcd.setTextColor(COL_TAB_IDLE, TFT_BLACK);
        lcd.setCursor(4, CHAT_Y + 3 * DM_LINE_H);
        lcd.print("No other nodes known yet");
        dirtyChat = false;
        return;
    }

    const int MSG_ROWS   = DM_VISIBLE - 1;
    int firstVisible = max(0, dmPickerSel - (MSG_ROWS - 1));

    for (int row = 0; row < MSG_ROWS; row++) {
        int vi = firstVisible + row;
        int      y  = CHAT_Y + (row + 1) * DM_LINE_H;
        if (vi >= filteredCount) {
            lcd.fillRect(0, y, LCD_W, DM_LINE_H, TFT_BLACK);
            continue;
        }
        NodeEntry *n = pickerNode(vi);
        if (!n) break;

        bool     sel = (vi == dmPickerSel);
        uint16_t bg  = sel ? 0x0013 : TFT_BLACK;
        uint16_t col = sel ? TFT_WHITE : COL_TEAL;

        lcd.fillRect(0, y, LCD_W, DM_LINE_H, bg);

        char entry[DM_LINE_LEN + 1];
        snprintf(entry, sizeof(entry), " [%-4s] %-28s !%08x",
                 n->shortName[0] ? n->shortName : "????",
                 n->longName[0]  ? n->longName  : "(unknown)",
                 (unsigned)n->nodeId);

        lcd.setTextColor(col, bg);
        lcd.setCursor(0, y);
        lcd.print(entry);
    }
    dirtyChat = false;
}

// ── Draw: DM conversation view ────────────────────────────────
static void drawDmConv() {
    lcd.fillRect(0, CHAT_Y, LCD_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);
    dirtyNodes = false;

    DmConv *c = DMs.find(dmConvNodeId);
    if (!c) {
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.setCursor(4, CHAT_Y + DM_LINE_H);
        lcd.print("Conversation not found");
        dirtyChat = false;
        return;
    }

    // Header bar
    char hdr[DM_LINE_LEN + 1];
    snprintf(hdr, sizeof(hdr), " DM: %-4s  !%08x ", c->shortName, (unsigned)c->nodeId);
    lcd.fillRect(0, CHAT_Y, LCD_W, DM_LINE_H, 0x0013);
    lcd.setTextColor(TFT_CYAN, 0x0013);
    lcd.setCursor(0, CHAT_Y);
    lcd.print(hdr);

    // Messages (DM_VISIBLE - 1 rows below the header)
    const int MSG_ROWS = DM_VISIBLE - 1;
    for (int row = 0; row < MSG_ROWS; row++) {
        const DmLine *dl = DMs.getLine(c, row, MSG_ROWS);
        if (!dl) continue;
        int y = CHAT_Y + (row + 1) * DM_LINE_H;
        lcd.setTextColor(dl->color, TFT_BLACK);
        lcd.setCursor(0, y);
        lcd.print(dl->text);
    }
    dirtyChat = false;
}

// ── Draw: settings page ───────────────────────────────────────
static void drawSettings() {
    const int SH = 10;  // settings always use Loose spacing
    lcd.fillRect(0, CHAT_Y, LCD_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);

    char buf[LCD_W / CHAR_W + 2];
    int  r = 0;   // current row index

    // ── Action buttons ────────────────────────────────────────
    for (int i = 0; i < NUM_SETTINGS; i++, r++) {
        int      y  = CHAT_Y + r * SH;
        bool     sel = (i == settingsSel);
        uint16_t bg  = sel ? 0x0013 : TFT_BLACK;
        uint16_t fg  = sel ? TFT_WHITE : COL_TAB_IDLE;
        lcd.fillRect(0, y, LCD_W, SH, bg);
        lcd.setTextColor(fg, bg);
        lcd.setCursor(0, y);
        if (i == SETTING_EXPORT)
            snprintf(buf, sizeof(buf), "  [ Export Config ]");
        else if (i == SETTING_IMPORT)
            snprintf(buf, sizeof(buf), "  [ Import Config ]");
        else if (i == SETTING_CHAT_SPACE) {
            const char *labels[] = { "Tight", "Normal", "Loose" };
            int idx = gCfg.chatSpacing <= 2 ? gCfg.chatSpacing : 1;
            snprintf(buf, sizeof(buf), "  [ Chat Spacing: %s ]", labels[idx]);
        }
        else if (i == SETTING_FACTORY_RESET)
            snprintf(buf, sizeof(buf), "  [ Factory Reset ]");
        else if (webCfgRunning())
            snprintf(buf, sizeof(buf), "  [ Web Config : %s ]", webCfgIP());
        else
            snprintf(buf, sizeof(buf), "  [ Web Config : OFF ]");
        lcd.print(buf);
    }

    // ── Status line (transient feedback) ─────────────────────
    if (settingsStatus[0]) {
        lcd.setTextColor(COL_TEAL, TFT_BLACK);
        lcd.setCursor(2, CHAT_Y + r * SH);
        lcd.print(settingsStatus);
    }
    r++;  // always advance past status slot

    // ── Separator ─────────────────────────────────────────────
    lcd.drawFastHLine(2, CHAT_Y + r * SH + SH / 2, LCD_W - 4, COL_DIVIDER);
    r++;

    // ── Read-only config info ─────────────────────────────────
    const uint16_t DIM = 0x7BEF;   // mid-grey
    auto pr = [&](const char *s) {
        lcd.setTextColor(DIM, TFT_BLACK);
        lcd.setCursor(2, CHAT_Y + r++ * SH);
        lcd.print(s);
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

    // GPS status
    if (gCfg.gpsEnabled) {
        if (gpsHasFix())
            snprintf(buf, sizeof(buf), "GPS:  FIX  sats:%d", gpsSats());
        else
            snprintf(buf, sizeof(buf), "GPS:  searching...");
    } else {
        snprintf(buf, sizeof(buf), "GPS:  disabled");
    }
    pr(buf);

    dirtyChat = false;
}

// ── Draw: node detail overlay ────────────────────────────────
static void drawNodeDetail(const NodeEntry *n) {
    lcd.fillRect(0, CHAT_Y, LCD_W, LCD_H - CHAT_Y, TFT_BLACK);
    lcd.setTextSize(1);

    const int X = 2;
    int row = 0;

    // Helper: print one row and advance
    char buf[LCD_W / CHAR_W + 2];
    auto pr = [&](uint16_t col, const char *s) {
        lcd.setTextColor(col, TFT_BLACK);
        lcd.setCursor(X, CHAT_Y + row++ * LINE_H);
        lcd.print(s);
    };

    if (!n) {
        pr(TFT_RED, "Node not found");
        pr(COL_TAB_IDLE, "[ESC/Enter] close");
        dirtyChat = false;
        return;
    }

    snprintf(buf, sizeof(buf), "[ !%08x ]", n->nodeId);
    pr(TFT_CYAN, buf);
    pr(COL_DIVIDER, "--------------------------------");

    // Identity
    snprintf(buf, sizeof(buf), "Long  : %s", n->longName[0] ? n->longName : "(unknown)");
    pr(TFT_WHITE, buf);
    snprintf(buf, sizeof(buf), "Short : %s", n->shortName[0] ? n->shortName : "----");
    pr(TFT_WHITE, buf);
    row++;

    // Connectivity
    uint32_t ageSec = (millis() - n->lastHeardMs) / 1000;
    if      (ageSec < 60)
        snprintf(buf, sizeof(buf), "Heard : %us ago",           (unsigned)ageSec);
    else if (ageSec < 3600)
        snprintf(buf, sizeof(buf), "Heard : %um %us ago",       (unsigned)(ageSec/60), (unsigned)(ageSec%60));
    else
        snprintf(buf, sizeof(buf), "Heard : %uh %um ago",       (unsigned)(ageSec/3600), (unsigned)((ageSec%3600)/60));
    pr(TFT_WHITE, buf);

    snprintf(buf, sizeof(buf), "Hops  : %d   SNR: %+.1f dB", n->hops, n->snr);
    pr(TFT_WHITE, buf);

    const char *chanName = (n->chanIdx >= 0 && n->chanIdx < MAX_CHANNELS)
                           ? CHANNEL_KEYS[n->chanIdx].name : "?";
    snprintf(buf, sizeof(buf), "Chan  : %s", chanName);
    pr(TFT_WHITE, buf);
    row++;

    // Position
    if (n->hasPosition) {
        pr(TFT_CYAN, "Position");
        float lat = n->latI * 1e-7f;
        float lon = n->lonI * 1e-7f;
        snprintf(buf, sizeof(buf), "Lat   : %.5f %c",
                 lat >= 0 ? lat : -lat, lat >= 0 ? 'N' : 'S');
        pr(TFT_WHITE, buf);
        snprintf(buf, sizeof(buf), "Lon   : %.5f %c",
                 lon >= 0 ? lon : -lon, lon >= 0 ? 'E' : 'W');
        pr(TFT_WHITE, buf);
        snprintf(buf, sizeof(buf), "Alt   : %d m", (int)n->alt);
        pr(TFT_WHITE, buf);
        row++;
    }

    // Telemetry
    if (n->hasTelemetry) {
        pr(TFT_CYAN, "Telemetry");
        snprintf(buf, sizeof(buf), "Batt  : %.0f%%  %.2f V", n->battPct, n->voltage);
        pr(TFT_WHITE, buf);
        row++;
    }

    pr(COL_TAB_IDLE, "[ESC / Enter] close");
    dirtyChat = false;
}

// ── Draw: input bar ───────────────────────────────────────────
static void drawInput() {
    // Show input bar only in channel views and DM conv view (not picker, not list)
    bool dmNeedsInput = (activeView == CHAN_DM && dmConvOpen && !dmPickerOpen);
    if ((activeView == CHAN_ANN)
            || (activeView == CHAN_DM && !dmNeedsInput)
            || (activeView == VIEW_GPS)
            || (activeView == VIEW_SETTINGS)) {
        // Non-text view — blank the input area
        lcd.fillRect(0, INPUT_Y, LCD_W, INPUT_H, TFT_BLACK);
        dirtyInput = false;
        return;
    }
    lcd.fillRect(0, INPUT_Y, LCD_W, INPUT_H, COL_INPUT_BG);
    lcd.setTextSize(1);
    lcd.setTextColor(COL_TEAL, COL_INPUT_BG);
    lcd.setCursor(0, INPUT_Y + 2);
    lcd.print("> ");

    // Show last N chars of input that fit
    int maxShow = (LCD_W / CHAR_W) - 3;  // "- "> " + cursor
    int start   = (inputLen > maxShow) ? inputLen - maxShow : 0;
    lcd.print(inputBuf + start);

    // Cursor blink
    if (cursorOn) {
        int cx = (2 + min(inputLen - start, maxShow)) * CHAR_W;
        lcd.fillRect(cx, INPUT_Y + 2, CHAR_W, CHAR_H, COL_CURSOR);
    }
    dirtyInput = false;
}

// ── Send routing ACK back to sender ──────────────────────────
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

    Radio.transmit(frame, sizeof(hdr) + protoLen);
    Serial.printf("[ack] routing ACK → !%08X for pkt 0x%08X hops=%u\n",
                  pkt.hdr.from, pkt.hdr.id, ackHops);
}

// ── Handle received packet ────────────────────────────────────
static void handleRx(MeshPacket pkt) {
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

    // Serial log (always)
    Serial.printf("\n┌─ PKT #%lu ───────────────────────────────\n", pktCount);
    Serial.printf("│ RSSI:%.1f SNR:%.2f Len:%u Chan:%d\n",
                  pkt.rssi, pkt.snr, sizeof(MeshHdr) + pkt.payloadLen, pkt.chanIdx);
    if (isBcast)
        Serial.printf("│ From:!%08x  To:BCAST  Hops:%u/%u  flags:0x%02x\n",
                      pkt.hdr.from, hopLimit, hopStart, pkt.hdr.flags);
    else
        Serial.printf("│ From:!%08x  To:!%08x  Hops:%u/%u  flags:0x%02x\n",
                      pkt.hdr.from, pkt.hdr.to, hopLimit, hopStart, pkt.hdr.flags);

    // Update node DB from every received packet (before portnum switch so hasName is current)
    Nodes.updateFromPacket(pkt);
    dirtyNodes = true;

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
                Serial.printf("│ PKI decrypt OK  portnum=%lu\n", (unsigned long)pkt.portnum);
            }
        }
    }

    if (!pkt.decrypted) {
        Serial.printf("│ (encrypted, unknown channel)\n");
        Serial.printf("└─────────────────────────────────────────\n");
        return;
    }

    // Build time prefix
    uint32_t upSec = pkt.rxMs / 1000;
    char prefix[24];

    switch (pkt.portnum) {

    case TEXT_MESSAGE_APP: {
        TextMsg tm;
        strncpy(tm.text, (const char *)pkt.payload,
                min(pkt.payloadLen, sizeof(tm.text) - 1));
        tm.text[min(pkt.payloadLen, sizeof(tm.text) - 1)] = '\0';
        Serial.printf("│ TEXT: \"%s\"\n", tm.text);

        // Sender display: use short name (real or hex fallback set by node DB)
        NodeEntry *n = Nodes.find(pkt.hdr.from);
        const char *sender = (n && n->shortName[0]) ? n->shortName : "????";

        snprintf(prefix, sizeof(prefix), "%02lu:%02lu [%s] ",
                 (upSec / 3600) % 24, (upSec / 60) % 60, sender);

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
        Serial.printf("│ NODEINFO: \"%s\" (%s)%s  pubKey=%s\n",
                      u.longName, u.shortName,
                      pkt.wantResponse ? " [want_response]" : "",
                      u.hasPubKey ? "YES(32B)" : "none");

        snprintf(prefix, sizeof(prefix), "%02lu:%02lu ",
                 (upSec / 3600) % 24, (upSec / 60) % 60);
        char info[60];
        snprintf(info, sizeof(info), "* %s joined (%s)", u.longName, u.shortName);
        Channels.addMessage(CHAN_ANN, prefix, info, 0xFD20 /* orange */);
        if (!dmPickerOpen) dirtyChat = true;
        dirtyNodes = dirtyTabs = true;

        // Respond with our own NODEINFO so the requester knows who we are
        if (pkt.wantResponse) {
            Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort, pkt.hdr.from);
        }
        break;
    }

    case POSITION_APP: {
        PositionInfo pos;
        decodePosition(pkt.payload, pkt.payloadLen, pos);
        Nodes.updatePosition(pkt.hdr.from, pos);
        Serial.printf("│ POSITION: %.5f, %.5f  alt:%dm\n",
                      pos.latI * 1e-7f, pos.lonI * 1e-7f, pos.alt);
        dirtyNodes = true;
        break;
    }

    case TELEMETRY_APP: {
        TelemetryInfo tel;
        decodeTelemetry(pkt.payload, pkt.payloadLen, tel);
        if (tel.valid) {
            Nodes.updateTelemetry(pkt.hdr.from, tel);
            Serial.printf("│ TELEMETRY: bat=%.0f%% %.2fV\n", tel.battPct, tel.voltage);
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
                Serial.printf("│ ACK for 0x%08X from !%08X\n", pkt.requestId, pkt.hdr.from);
            } else {
                Channels.setAckState(pkt.requestId, DisplayLine::NAKED);
                Serial.printf("│ NAK for 0x%08X err=%lu from !%08X\n",
                              pkt.requestId, (unsigned long)errorReason, pkt.hdr.from);

                // PKI_UNKNOWN_PUBKEY (35): sender doesn't have our public key yet.
                // Respond with a unicast NODEINFO so they can retry with PKI.
                if (errorReason == 35) {
                    Serial.printf("[nak] PKI_UNKNOWN_PUBKEY — sending NODEINFO to !%08X\n", pkt.hdr.from);
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
        Serial.printf("│ %s port=%lu payload=%u bytes\n",
                      portnumName(pkt.portnum), pkt.portnum,
                      (unsigned)pkt.payloadLen);
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
            Serial.printf("[nodeinfo] greeting !%08X (%s, last sent %lums ago)\n",
                          pkt.hdr.from,
                          (known && known->hasName) ? known->shortName : "new",
                          elapsed == UINT32_MAX ? 0UL : (unsigned long)elapsed);
            if (Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort, pkt.hdr.from)) {
                if (known) known->lastSentInfoMs = millis();
            }
        }
    }

    Serial.printf("└─────────────────────────────────────────\n");
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
        if (activeView == VIEW_SETTINGS) {
            if (settingsSel == SETTING_EXPORT) {
                bool ok = cfgExport(gCfg);
                snprintf(settingsStatus, sizeof(settingsStatus),
                         ok ? "Exported to /camillia/config.yaml" : "Export FAILED (no SD?)");
            } else if (settingsSel == SETTING_IMPORT) {
                bool ok = cfgImport(gCfg);
                if (ok) {
                    { Preferences p; p.begin("camillia", false);
                      p.putString("nodeLong", gCfg.nodeLong);
                      p.putString("nodeShort", gCfg.nodeShort); p.end(); }
                    NodeEntry *me = Nodes.upsert(myNodeId);
                    strncpy(me->longName,  gCfg.nodeLong,  sizeof(me->longName)  - 1);
                    strncpy(me->shortName, gCfg.nodeShort, sizeof(me->shortName) - 1);
                    Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw,
                                      gCfg.loraSf, gCfg.loraCr, gCfg.loraPower);
                    Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
                    dirtyStatus = dirtyNodes = true;
                    snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK");
                } else {
                    snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
                }
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
            } else if (settingsSel == SETTING_CHAT_SPACE) {
                gCfg.chatSpacing = (gCfg.chatSpacing + 1) % 3;
                { Preferences p; p.begin("camillia", false);
                  p.putUChar("chatSpace", gCfg.chatSpacing); p.end(); }
                const char *labels[] = { "Tight", "Normal", "Loose" };
                snprintf(settingsStatus, sizeof(settingsStatus),
                         "Chat: %s — rebooting...", labels[gCfg.chatSpacing]);
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
            } else if (settingsSel == SETTING_FACTORY_RESET) {
                nvs_flash_erase();
                nvs_flash_init();
                sdRmDir("/camillia/dms");
                snprintf(settingsStatus, sizeof(settingsStatus), "Factory reset — rebooting...");
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
            }
            dirtyChat = true;
        } else if (inputLen > 0 && activeView != CHAN_ANN && activeView != CHAN_DM
                   && activeView != VIEW_GPS && activeView != VIEW_SETTINGS) {
            inputBuf[inputLen] = '\0';
            if (!Channels.sendText(myNodeId, inputBuf, gCfg.okToMqtt)) {
                Channels.addMessage(Channels.activeIdx(), "",
                    "! TX failed", TFT_RED, 0);
                dirtyChat = true;
            }
            inputLen = 0; inputBuf[0] = '\0';
            dirtyInput = dirtyChat = true;
        }

    } else if (k == KEY_BACKSPACE) {
        bool textAllowed = (activeView != CHAN_ANN && activeView != VIEW_SETTINGS
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
                    { Preferences p; p.begin("camillia", false);
                      p.putString("nodeLong", gCfg.nodeLong);
                      p.putString("nodeShort", gCfg.nodeShort); p.end(); }
                    NodeEntry *me = Nodes.upsert(myNodeId);
                    strncpy(me->longName,  gCfg.nodeLong,  sizeof(me->longName)  - 1);
                    strncpy(me->shortName, gCfg.nodeShort, sizeof(me->shortName) - 1);
                    Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw,
                                      gCfg.loraSf, gCfg.loraCr, gCfg.loraPower);
                    Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
                    dirtyStatus = dirtyNodes = true;
                    snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK");
                } else {
                    snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
                }
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
            } else if (settingsSel == SETTING_CHAT_SPACE) {
                gCfg.chatSpacing = (gCfg.chatSpacing + 1) % 3;
                { Preferences p; p.begin("camillia", false);
                  p.putUChar("chatSpace", gCfg.chatSpacing); p.end(); }
                const char *labels[] = { "Tight", "Normal", "Loose" };
                snprintf(settingsStatus, sizeof(settingsStatus),
                         "Chat: %s — rebooting...", labels[gCfg.chatSpacing]);
                dirtyChat = true;
                drawSettings();
                delay(1000);
                ESP.restart();
            } else if (settingsSel == SETTING_FACTORY_RESET) {
                nvs_flash_erase();
                nvs_flash_init();
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
        } else if (activeView == VIEW_SETTINGS) {
            settingsSel = max(0, settingsSel - 1);
            dirtyChat = true;
        } else if (activeView < MAX_CHANNELS && activeView != CHAN_DM) {
            Channel &ch = Channels.get(activeView);
            int maxOff = max(0, ch.count - VISIBLE_LINES);
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
        } else if (activeView == VIEW_SETTINGS) {
            settingsSel = min(NUM_SETTINGS - 1, settingsSel + 1);
            dirtyChat = true;
        } else if (activeView < MAX_CHANNELS && activeView != CHAN_DM) {
            Channel &ch = Channels.get(activeView);
            ch.scrollOff = max(0, ch.scrollOff - 3);
            dirtyChat = true;
        }

    } else if (k == KEY_PAGE_UP) {
        if (activeView < MAX_CHANNELS) {
            Channel &ch = Channels.get(activeView);
            int maxOff = max(0, ch.count - VISIBLE_LINES);
            ch.scrollOff = min(ch.scrollOff + VISIBLE_LINES, maxOff);
            dirtyChat = true;
        }

    } else if (k == KEY_PAGE_DN) {
        if (activeView < MAX_CHANNELS) {
            Channel &ch = Channels.get(activeView);
            ch.scrollOff = 0;
            dirtyChat = true;
        }

    } else if (k >= 0x20 && k < 0x7F) {
        bool textAllowed = (activeView != CHAN_ANN && activeView != VIEW_SETTINGS
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
    Preferences p; p.begin("camillia", false);
    // Re-save identity + WiFi keys wiped by clear()
    p.putUInt("nodeId",       myNodeId);
    p.putInt("pkiVer",        2);
    p.putBytes("privKey",     myPrivKey, 32);
    p.putBytes("pubKey",      myPubKey,  32);
    if (webCfgWifiSsid()[0]) p.putString("wifiSsid", webCfgWifiSsid());
    if (webCfgWifiPass()[0]) p.putString("wifiPass", webCfgWifiPass());
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
    // Apply GPS enable/disable immediately
    gpsSetEnabled(gCfg.gpsEnabled);
    // Apply LoRa changes immediately
    Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw, gCfg.loraSf, gCfg.loraCr, gCfg.loraPower);
    // Apply node ID override immediately (no reboot needed)
    if (gCfg.nodeIdOverride != 0) myNodeId = gCfg.nodeIdOverride;
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
        prefs.end();
    }
    // Apply chat spacing setting to runtime globals
    LINE_H        = kSpacingPx[gCfg.chatSpacing <= 2 ? gCfg.chatSpacing : 1];
    VISIBLE_LINES = CHAT_H / LINE_H;

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
    lcd.fillScreen(TFT_BLACK);
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
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.setCursor(10, 100);
        lcd.print("LoRa init FAILED");
        while (true) delay(500);
    }

    // SD card needs SPI bus — init after Radio.init() calls SPI.begin()
    sdBegin();
    DMs.init();

    // Let radio settle before first TX
    delay(200);
    bool niOk = Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
    Serial.printf("[camillia-mt] NODEINFO broadcast %s\n", niOk ? "sent" : "FAILED");
    Channels.addMessage(CHAN_ANN, "", niOk ? "* Announced (NODEINFO)" : "! NODEINFO failed",
                        niOk ? TFT_DARKGREY : TFT_RED);

    bool posOk = Channels.sendPosition(myNodeId, gCfg.latI, gCfg.lonI, gCfg.alt);
    Serial.printf("[camillia-mt] POSITION broadcast %s\n", posOk ? "sent" : "FAILED");
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

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    // 1. Poll radio
    MeshPacket pkt;
    if (Radio.pollRx(pkt)) handleRx(pkt);

    // 1b. Service web config server if running
    webCfgLoop();
    if (webCfgAnnounceRequested()) {
        Channels.sendNodeInfo(myNodeId, gCfg.nodeLong, gCfg.nodeShort);
        Channels.sendPosition(myNodeId, gCfg.latI, gCfg.lonI, gCfg.alt);
        Serial.printf("[announce] manual NODEINFO + position broadcast\n");
    }

    // 1c. Poll GPS
    gpsLoop();

    uint32_t now = millis();

    // 2. Poll keyboard
    char k = kb.read();
    if (k != KEY_NONE) {
        if (screenAsleep) {
            wakeScreen();   // any key wakes the screen; key is consumed (not passed through)
        } else {
            // Only real keys and roller click count as activity — not trackball scroll
            // events which are ISR-driven and can fire spuriously.
            bool isScroll = (k == KEY_NEXT_CHAN || k == KEY_PREV_CHAN ||
                             k == KEY_SCROLL_UP || k == KEY_SCROLL_DN);
            if (!isScroll) lastActivityMs = now;
            handleKey(k);
        }
    }

    // 3. ACK expiry
    Channels.expireAcks();

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
            else if (activeView == CHAN_DM && dmPickerOpen)            drawDmPicker();
            else if (activeView == CHAN_DM && dmConvOpen)              drawDmConv();
            else if (activeView == CHAN_DM)                            drawDmList();
            else                                                       drawChat();
        }
        if (activeView < MAX_CHANNELS && activeView != CHAN_DM) {
            if (dirtyNodes) drawNodes();
        }
        if (dirtyInput) drawInput();
    }
}
