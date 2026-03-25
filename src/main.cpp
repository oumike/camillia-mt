#include <Arduino.h>
#include <Preferences.h>
#include "lgfx_tdeck.h"
#include "keyboard.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "node_db.h"
#include "channel_mgr.h"
#include "config_io.h"

// ── Globals ───────────────────────────────────────────────────
static LGFX_TDeck   lcd;
static TDeckKeyboard kb;

// Node ID: in a later phase this will be stored in NVS.
// For now, derive from the ESP32 MAC address.
static uint32_t myNodeId = 0;
static RhinoConfig gCfg;

// ── View state ────────────────────────────────────────────────
#define VIEW_SETTINGS  MAX_CHANNELS   // index 8 = settings page
static int  activeView   = 0;         // 0..7 channel, 8 settings
static int  settingsSel  = 0;         // highlighted settings row

// ── Dirty flags ───────────────────────────────────────────────
static bool dirtyStatus   = true;
static bool dirtyTabs     = true;
static bool dirtyChat     = true;
static bool dirtyNodes    = true;
static bool dirtyInput    = true;
static bool dirtyDivider  = false;

// ── Input state ───────────────────────────────────────────────
static char   inputBuf[MAX_INPUT_LEN + 1] = {0};
static int    inputLen  = 0;
static bool   cursorOn  = true;
static uint32_t lastBlink      = 0;
static uint32_t lastNodeInfo   = 0;
static uint32_t lastPosition   = 0;
#define NODEINFO_INTERVAL_MS  (15UL * 60UL * 1000UL)  // 15 minutes
#define POSITION_INTERVAL_MS  (30UL * 60UL * 1000UL)  // 30 minutes

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


// ── Settings actions ──────────────────────────────────────────
#define NUM_SETTINGS 2
static char settingsStatus[LCD_W / CHAR_W + 1] = "";  // last action result

// ── View navigation ───────────────────────────────────────────
static void goToView(int v) {
    bool wasSettings = (activeView == VIEW_SETTINGS);
    activeView = v;
    if (v < MAX_CHANNELS) {
        Channels.setActive(v);
        if (wasSettings) dirtyDivider = true;   // restore divider after leaving settings
    }
    dirtyTabs = dirtyChat = dirtyNodes = dirtyStatus = true;
}

// ── Splash screen ─────────────────────────────────────────────
static void drawSplash() {
    const uint16_t BG      = TFT_BLACK;
    const uint16_t ACCENT  = TFT_GREEN;
    const uint16_t TITLE   = TFT_CYAN;
    const uint16_t SUB     = TFT_WHITE;
    const uint16_t DIM     = 0x7BEF;  // mid-grey

    lcd.fillScreen(BG);

    // Top accent bar
    lcd.fillRect(0, 0, LCD_W, 4, ACCENT);

    // Bottom accent bar
    lcd.fillRect(0, LCD_H - 4, LCD_W, 4, ACCENT);

    // Side accent lines
    lcd.fillRect(0, 4, 3, LCD_H - 8, ACCENT);
    lcd.fillRect(LCD_W - 3, 4, 3, LCD_H - 8, ACCENT);

    // App name — large cyan, centred
    lcd.setTextSize(4);
    lcd.setTextColor(TITLE, BG);
    const char *appName = "Camillia MT";
    int tw = strlen(appName) * 6 * 4;
    lcd.setCursor((LCD_W - tw) / 2, 60);
    lcd.print(appName);

    // Subtitle
    lcd.setTextSize(2);
    lcd.setTextColor(SUB, BG);
    const char *sub = "Meshtastic Client";
    tw = strlen(sub) * 6 * 2;
    lcd.setCursor((LCD_W - tw) / 2, 112);
    lcd.print(sub);

    const char *sub2 = "for T-Deck";
    tw = strlen(sub2) * 6 * 2;
    lcd.setCursor((LCD_W - tw) / 2, 132);
    lcd.print(sub2);

    // Node ID line
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "!%08x  %s", myNodeId, MY_SHORT_NAME);
    lcd.setTextSize(1);
    lcd.setTextColor(DIM, BG);
    tw = strlen(idBuf) * 6;
    lcd.setCursor((LCD_W - tw) / 2, 170);
    lcd.print(idBuf);

    // Decorative dots row
    lcd.setTextColor(ACCENT, BG);
    lcd.setCursor((LCD_W - 11*6) / 2, 190);
    lcd.print("* * * * * *");

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
#define COL_CURSOR      TFT_GREEN

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
             MY_SHORT_NAME, Channels.get(Channels.activeIdx()).name,
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
    // MAX_CHANNELS channel tabs (8 mesh + 1 ANN) + 1 CFG = 10 total
    const int TOTAL_TABS = MAX_CHANNELS + 1;
    const int TAB_W      = LCD_W / TOTAL_TABS;  // 32px each
    for (int i = 0; i <= MAX_CHANNELS; i++) {
        int x = i * TAB_W;
        bool isActive   = (i == activeView);
        bool isSettings = (i == VIEW_SETTINGS);
        bool isAnn      = (i == CHAN_ANN);
        uint16_t col;
        if (isSettings) {
            col = isActive ? TFT_WHITE : COL_TAB_IDLE;
        } else if (isAnn) {
            Channel &ch = Channels.get(i);
            col = ch.unread ? TFT_CYAN : isActive ? TFT_CYAN : COL_TAB_IDLE;
        } else {
            Channel &ch = Channels.get(i);
            col = ch.unread      ? COL_TAB_UNREAD :
                  isActive       ? COL_TAB_ACTIVE  : COL_TAB_IDLE;
        }
        if (isActive) lcd.fillRect(x, STATUS_H, TAB_W - 1, TAB_H, 0x0013);
        lcd.setTextColor(col, isActive ? 0x0013 : TFT_BLACK);
        char label[8];
        if (isSettings)     strncpy(label, "CFG", sizeof(label));
        else if (isAnn)     strncpy(label, "ANN", sizeof(label));
        else                snprintf(label, sizeof(label), "%.4s", Channels.get(i).name);
        lcd.setCursor(x + 1, STATUS_H + 1);
        lcd.print(label);
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

        int y = CHAT_Y + row * CHAR_H;
        uint16_t col = dl->color;

        // ACK indicator prefix (overrides first char of text with symbol)
        char display[MSG_CHARS + 2];
        strncpy(display, dl->text, MSG_CHARS);
        display[MSG_CHARS] = '\0';

        if (dl->packetId && dl->ack == DisplayLine::ACKED) {
            int len = strlen(display);
            if (len < MSG_CHARS) { display[len] = '+'; display[len + 1] = '\0'; }
            else                 { display[len - 1] = '+'; }
        } else if (dl->packetId && dl->ack == DisplayLine::NAKED) {
            int len = strlen(display);
            if (len < MSG_CHARS) { display[len] = '!'; display[len + 1] = '\0'; }
            else                 { display[len - 1] = '!'; }
        }

        lcd.setTextColor(col, TFT_BLACK);
        lcd.setCursor(0, y);
        lcd.print(display);
    }
    dirtyChat = false;
}

// ── Draw: node list ───────────────────────────────────────────
static void drawNodes() {
    lcd.fillRect(NODE_X, CHAT_Y, NODE_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);

    const int MAX_VISIBLE = CHAT_H / CHAR_H;  // 26
    uint32_t now = millis();

    for (int i = 0; i < MAX_VISIBLE; i++) {
        NodeEntry *n = Nodes.getByRank(i);
        if (!n) break;

        int y = CHAT_Y + i * CHAR_H;
        uint32_t age = now - n->lastHeardMs;
        uint16_t col = (age < 60000UL)    ? TFT_CYAN    :
                       (age < 3600000UL)  ? TFT_WHITE   : COL_TAB_IDLE;

        // Single row: short name + hops + snr (+ battery if available)
        char r[NODE_CHARS + 1];
        if (n->hasTelemetry && n->battPct > 0)
            snprintf(r, sizeof(r), "%-4s %d %+.0f %d%%",
                     n->shortName, n->hops, n->snr, (int)n->battPct);
        else
            snprintf(r, sizeof(r), "%-4s %d %+.0f",
                     n->shortName, n->hops, n->snr);

        lcd.setTextColor(col, TFT_BLACK);
        lcd.setCursor(NODE_X, y);
        lcd.print(r);
    }
    dirtyNodes = false;
}

// ── Draw: settings page ───────────────────────────────────────
static void drawSettings() {
    lcd.fillRect(0, CHAT_Y, LCD_W, CHAT_H, TFT_BLACK);
    lcd.setTextSize(1);

    // Action rows
    const char *labels[NUM_SETTINGS] = { "Export Config", "Import Config" };
    for (int i = 0; i < NUM_SETTINGS; i++) {
        int      y   = CHAT_Y + i * CHAR_H;
        bool     sel = (i == settingsSel);
        uint16_t bg  = sel ? 0x0013 : TFT_BLACK;
        uint16_t fg  = sel ? TFT_WHITE : COL_TAB_IDLE;
        lcd.fillRect(0, y, LCD_W, CHAR_H, bg);
        char row[LCD_W / CHAR_W + 2];
        snprintf(row, sizeof(row), "  [ %s ]", labels[i]);
        lcd.setTextColor(fg, bg);
        lcd.setCursor(0, y);
        lcd.print(row);
    }

    // Status line (2 rows down from last action row)
    if (settingsStatus[0]) {
        int y = CHAT_Y + (NUM_SETTINGS + 1) * CHAR_H;
        lcd.setTextColor(TFT_GREEN, TFT_BLACK);
        lcd.setCursor(2, y);
        lcd.print(settingsStatus);
    }

    dirtyChat = false;
}

// ── Draw: input bar ───────────────────────────────────────────
static void drawInput() {
    lcd.fillRect(0, INPUT_Y, LCD_W, INPUT_H, COL_INPUT_BG);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_GREEN, COL_INPUT_BG);
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

// ── Handle received packet ────────────────────────────────────
static void handleRx(const MeshPacket &pkt) {
    if (isDuplicate(pkt.hdr.id)) return;
    if (pkt.hdr.from == myNodeId) return;  // ignore our own relayed/reflected packets
    pktCount++;
    dirtyStatus = true;

    uint8_t hopLimit = pkt.hdr.flags & 0x07;
    uint8_t hopStart = (pkt.hdr.flags >> 5) & 0x07;
    bool    isBcast  = (pkt.hdr.to == 0xFFFFFFFF);

    // Serial log (always)
    Serial.printf("\n┌─ PKT #%lu ───────────────────────────────\n", pktCount);
    Serial.printf("│ RSSI:%.1f SNR:%.2f Len:%u Chan:%d\n",
                  pkt.rssi, pkt.snr, sizeof(MeshHdr) + pkt.payloadLen, pkt.chanIdx);
    Serial.printf("│ From:!%08x  To:%s  Hops:%u/%u\n",
                  pkt.hdr.from, isBcast ? "BCAST" : "unicast", hopLimit, hopStart);

    // Update node DB from every received packet
    Nodes.updateFromPacket(pkt);
    dirtyNodes = true;

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
        Channels.addMessage(pkt.chanIdx, prefix, tm.text, TFT_GREEN);
        dirtyChat = dirtyTabs = true;
        break;
    }

    case NODEINFO_APP: {
        UserInfo u;
        decodeUser(pkt.payload, pkt.payloadLen, u);
        Nodes.updateUser(pkt.hdr.from, u);
        Serial.printf("│ NODEINFO: \"%s\" (%s)\n", u.longName, u.shortName);

        snprintf(prefix, sizeof(prefix), "%02lu:%02lu ",
                 (upSec / 3600) % 24, (upSec / 60) % 60);
        char info[60];
        snprintf(info, sizeof(info), "* %s joined (%s)", u.longName, u.shortName);
        Channels.addMessage(CHAN_ANN, prefix, info, 0xFD20 /* orange */);
        dirtyChat = dirtyNodes = dirtyTabs = true;
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
            // Determine ACK vs NAK from error_reason (field 3 of Routing)
            // For now treat any ROUTING_APP with requestId as ACK
            Channels.setAckState(pkt.requestId, DisplayLine::ACKED);
            Serial.printf("│ ACK for 0x%08X\n", pkt.requestId);
            dirtyChat = true;
        }
        break;
    }

    default:
        Serial.printf("│ %s port=%lu payload=%u bytes\n",
                      portnumName(pkt.portnum), pkt.portnum,
                      (unsigned)pkt.payloadLen);
        break;
    }

    Serial.printf("└─────────────────────────────────────────\n");
}

// ── Handle keyboard input ─────────────────────────────────────
static void handleKey(char k) {
    if (k == KEY_NONE) return;

    if (k == KEY_ENTER) {
        if (inputLen > 0) {
            inputBuf[inputLen] = '\0';
            if (!Channels.sendText(myNodeId, inputBuf)) {
                // TX failed — show error
                Channels.addMessage(Channels.activeIdx(), "",
                    "! TX failed", TFT_RED, 0);
                dirtyChat = true;
            }
            inputLen = 0; inputBuf[0] = '\0';
            dirtyInput = dirtyChat = true;
        }

    } else if (k == KEY_BACKSPACE) {
        if (inputLen > 0) { inputBuf[--inputLen] = '\0'; dirtyInput = true; }

    } else if (k == KEY_TAB || k == KEY_NEXT_CHAN || k == KEY_ROLLER) {
        if (activeView == VIEW_SETTINGS && k == KEY_ROLLER) {
            // handled below in the '\r' branch — fall through to action
            bool ok = false;
            if (settingsSel == 0) {
                ok = cfgExport(gCfg);
                snprintf(settingsStatus, sizeof(settingsStatus),
                         ok ? "Exported to /camillia-mt/config.ini" : "Export FAILED (no SD?)");
            } else if (settingsSel == 1) {
                ok = cfgImport(gCfg);
                if (ok) {
                    NodeEntry *me = Nodes.upsert(myNodeId);
                    strncpy(me->longName,  gCfg.nodeLong,  sizeof(me->longName)  - 1);
                    strncpy(me->shortName, gCfg.nodeShort, sizeof(me->shortName) - 1);
                    Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw,
                                      gCfg.loraSf, gCfg.loraCr, gCfg.loraPower);
                    snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK");
                } else {
                    snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
                }
            }
            dirtyChat = true;
        } else if (activeView != VIEW_SETTINGS || settingsSel == 0) {
            goToView((activeView + 1) % (MAX_CHANNELS + 1));
        }

    } else if (k == KEY_PREV_CHAN) {
        if (activeView != VIEW_SETTINGS || settingsSel == 0)
            goToView((activeView + MAX_CHANNELS) % (MAX_CHANNELS + 1));

    } else if (k == KEY_SCROLL_UP) {
        if (activeView == VIEW_SETTINGS) {
            settingsSel = max(0, settingsSel - 1);
        } else {
            Channel &ch = Channels.get(activeView);
            int maxOff = max(0, ch.count - VISIBLE_LINES);
            ch.scrollOff = min(ch.scrollOff + 3, maxOff);
        }
        dirtyChat = true;

    } else if (k == KEY_SCROLL_DN) {
        if (activeView == VIEW_SETTINGS) {
            settingsSel = min(NUM_SETTINGS - 1, settingsSel + 1);
        } else {
            Channel &ch = Channels.get(activeView);
            ch.scrollOff = max(0, ch.scrollOff - 3);
        }
        dirtyChat = true;

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

    } else if (k == '\r') {
        if (activeView == VIEW_SETTINGS) {
            bool ok = false;
            if (settingsSel == 0) {
                ok = cfgExport(gCfg);
                snprintf(settingsStatus, sizeof(settingsStatus),
                         ok ? "Exported to /camillia-mt/config.ini" : "Export FAILED (no SD?)");
            } else if (settingsSel == 1) {
                ok = cfgImport(gCfg);
                if (ok) {
                    NodeEntry *me = Nodes.upsert(myNodeId);
                    strncpy(me->longName,  gCfg.nodeLong,  sizeof(me->longName)  - 1);
                    strncpy(me->shortName, gCfg.nodeShort, sizeof(me->shortName) - 1);
                    Radio.reconfigure(gCfg.loraFreq, gCfg.loraBw,
                                      gCfg.loraSf, gCfg.loraCr, gCfg.loraPower);
                    snprintf(settingsStatus, sizeof(settingsStatus), "Imported OK");
                } else {
                    snprintf(settingsStatus, sizeof(settingsStatus), "Import FAILED (no file?)");
                }
            }
            dirtyChat = true;
        }

    } else if (k >= 0x20 && k < 0x7F) {
        if (inputLen < MAX_INPUT_LEN) {
            inputBuf[inputLen++] = k;
            inputBuf[inputLen]   = '\0';
            dirtyInput = true;
        }
    }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    // Load or generate stable node ID from NVS
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
    Serial.printf("[camillia-mt] Name: %s (%s)\n", MY_LONG_NAME, MY_SHORT_NAME);

    // Load runtime config defaults (SD overrides applied below)
    cfgInitDefaults(gCfg);

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

    // Splash
    drawSplash();

    // Keyboard
    kb.begin();
    sdBegin();

    // Data modules
    Nodes.init();
    Channels.init();

    // Register ourselves in the node DB immediately
    {
        NodeEntry *me = Nodes.upsert(myNodeId);
        strncpy(me->longName,  MY_LONG_NAME,  sizeof(me->longName)  - 1);
        strncpy(me->shortName, MY_SHORT_NAME, sizeof(me->shortName) - 1);
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

    // Let radio settle before first TX
    delay(200);
    bool niOk = Channels.sendNodeInfo(myNodeId);
    Serial.printf("[camillia-mt] NODEINFO broadcast %s\n", niOk ? "sent" : "FAILED");
    Channels.addMessage(CHAN_ANN, "", niOk ? "* Announced (NODEINFO)" : "! NODEINFO failed",
                        niOk ? TFT_DARKGREY : TFT_RED);

    bool posOk = Channels.sendPosition(myNodeId);
    Serial.printf("[camillia-mt] POSITION broadcast %s\n", posOk ? "sent" : "FAILED");
    Channels.addMessage(CHAN_ANN, "", posOk ? "* Position broadcast" : "! POSITION failed",
                        posOk ? TFT_DARKGREY : TFT_RED);

    // Sync activeView with the channel manager's initial active index
    activeView = Channels.activeIdx();

    // Initial full draw
    drawDivider();
    dirtyStatus = dirtyTabs = dirtyChat = dirtyNodes = dirtyInput = true;
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    // 1. Poll radio
    MeshPacket pkt;
    if (Radio.pollRx(pkt)) handleRx(pkt);

    // 2. Poll keyboard
    char k = kb.read();
    if (k != KEY_NONE) handleKey(k);

    // 3. ACK expiry
    Channels.expireAcks();

    // 4. Cursor blink
    uint32_t now = millis();
    if (now - lastBlink > CURSOR_BLINK_MS) {
        cursorOn  = !cursorOn;
        lastBlink = now;
        dirtyInput = true;
    }

    // 5. Periodic NODEINFO re-announcement
    if (now - lastNodeInfo > NODEINFO_INTERVAL_MS) {
        lastNodeInfo = now;
        Channels.sendNodeInfo(myNodeId);
    }

    // 6. Periodic position broadcast
    if (now - lastPosition > POSITION_INTERVAL_MS) {
        lastPosition = now;
        Channels.sendPosition(myNodeId);
    }

    // 6b. Battery refresh every 5 s
    static uint32_t lastBattMs = 0;
    if (now - lastBattMs >= 5000) {
        lastBattMs   = now;
        _battPct     = readBatteryPct();
        dirtyStatus  = true;
    }

    // 7. Redraw dirty zones
    if (dirtyStatus)  drawStatus();
    if (dirtyTabs)    drawTabs();
    if (dirtyDivider) { drawDivider(); dirtyDivider = false; }
    if (dirtyChat) {
        if (activeView == VIEW_SETTINGS) drawSettings();
        else                             drawChat();
    }
    if (activeView < MAX_CHANNELS) {
        if (dirtyNodes) drawNodes();
    }
    if (dirtyInput)   drawInput();
}
