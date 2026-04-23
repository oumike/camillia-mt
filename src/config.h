#pragma once

// ── Power & Peripherals ──────────────────────────────────────
#define BOARD_POWERON   10
#define BOARD_BUZZER     4

// ── Display SPI (ST7789, landscape 320×240) ──────────────────
#define SPI_SCK         40
#define SPI_MISO        38
#define SPI_MOSI        41
#define TFT_CS          12
#define TFT_DC          11
#define TFT_BL          42

// ── LoRa SX1262 (shares SPI bus with display) ────────────────
#define LORA_CS         9
#define LORA_DIO1       45
#define LORA_RST        17
#define LORA_BUSY       13

// ── microSD (shares SPI bus with display/LoRa) ────────────────
#define SD_CS           39

// ── I2C Keyboard ─────────────────────────────────────────────
#define KB_SDA          18
#define KB_SCL           8
#define KB_ADDR       0x55
#define KB_INT          46

// ── Touch controller (shared I2C bus) ───────────────────────
// Meshtastic T-Deck profile: GT911 on I2C0 (SDA=18/SCL=8), addr 0x5D, INT=16.
#define TOUCH_SDA       18
#define TOUCH_SCL        8
#define TOUCH_ADDR      0x5D
#define TOUCH_INT       16
#define TOUCH_RST       -1
#define TOUCH_I2C_PORT   0

// ── Trackball (optical encoder, direct GPIO, active-low) ─────
#define TBALL_UP         3
#define TBALL_DOWN       2
#define TBALL_LEFT       1
#define TBALL_RIGHT     15
#define TBALL_CLICK      0   // shared with BOOT button

// ── Meshtastic LoRa (LongFast preset, US 915 MHz) ────────────
#define MESH_FREQ       906.875f  // MHz
#define MESH_BW         250.0f    // kHz
#define MESH_SF         11
#define MESH_CR         5         // 4/5 coding rate (Meshtastic LONG_FAST default)
#define MESH_SYNC       0x2B      // Meshtastic sync word
#define MESH_PREAMBLE   16
#define MESH_POWER      22        // dBm (hardware max; ribl_config requests 30)
#define MESH_HOP_LIMIT   7        // from ribl_config
#define MESH_TCXO_V     1.6f      // TCXO voltage for T-Deck SX1262

// ── Node identity (change to your callsign/name) ─────────────
#define MY_LONG_NAME    "Camillia"
#define MY_SHORT_NAME   "CaMi"

// ── GPS hardware ──────────────────────────────────────────────
// L76K GPS module connected to UART1 on T-Deck.
#define HAS_GPS         1
#define GPS_RX          44    // ESP32 RX ← GPS TX
#define GPS_TX          43    // ESP32 TX → GPS RX
#define GPS_BAUD        38400

#define MY_GPS_ENABLED  1     // runtime default (can be toggled via web config)

// ── Fixed position (from ribl_config) ────────────────────────
// Used as the startup default when HAS_GPS == 0.
#define MY_LAT_I        424935424   // lat * 1e7  (42.4935424°N)
#define MY_LON_I       -833880064   // lon * 1e7  (-83.3880064°W)
#define MY_ALT          228         // meters

#define MY_DEVICE_ROLE      0      // CLIENT
#define MY_REBROADCAST      0      // ALL
#define MY_NODEINFO_INTV  900      // 15 min (seconds)
#define MY_POS_INTV      1800      // 30 min (seconds)
#define MY_REGION        "US"
#define MY_TZ_DEF        "EST5EDT,M3.2.0,M11.1.0"   // Eastern (Detroit)

// ── Display defaults ──────────────────────────────────────────
#define MY_SCREEN_ON_SECS   30     // 30 s
#define MY_DISPLAY_UNITS    0      // METRIC
#define MY_COMPASS_NORTH    0
#define MY_FLIP_SCREEN      0
#define MY_UI_THEME         0      // 0=CAMELLIA, 1=EVERGREEN, 2=EARTHEN
#define MY_UI_MODE          0      // 0=DARK, 1=LIGHT

// ── Bluetooth defaults ─────────────────────────────────────────
#define MY_BT_ENABLED       1
#define MY_BT_MODE          0      // RANDOM_PIN
#define MY_BT_PIN           123456

// ── Network defaults ───────────────────────────────────────────
#define MY_NTP_SERVER       "meshtastic.pool.ntp.org"
#define MY_MQTT_ENABLED     0
#define MY_MQTT_SERVER      "mqtt.meshtastic.org"
#define MY_MQTT_USER        "meshdev"
#define MY_MQTT_PASS        "large4cats"
#define MY_MQTT_ROOT        "msh/US"
#define MY_MQTT_ENCRYPT     1
#define MY_MQTT_MAP_RPT     0

// ── Power defaults ─────────────────────────────────────────────
#define MY_POWER_SAVING     0
#define MY_LS_SECS          300
#define MY_MIN_WAKE_SECS    10

// ── Module defaults ────────────────────────────────────────────
#define MY_TEL_DEV_EN       1
#define MY_TEL_DEV_INTV     7200
#define MY_TEL_ENV_EN       0
#define MY_TEL_ENV_INTV     7200
#define MY_CANNED_EN        1
#define MY_CANNED_MSGS      "Hi|Bye|Yes|No|Ok"
#define MY_CHAT_SPACING     1   // 0=Tight(8px), 1=Normal(10px), 2=Loose(12px)
#define MY_DBG_ACKS         0
#define MY_DBG_MESSAGES     0
#define MY_DBG_GPS          0

// ── Display UI zones (landscape 320×240, font0 = 6×8 px) ─────
#define LCD_W           320
#define LCD_H           240

#define STATUS_H         28   // top status bar
#define TAB_H            14   // channel tab bar (taller for labeled pills)
#define MSG_W           230   // message pane width
#define NODE_X          231   // node pane left edge
#define NODE_W           89   // node pane width
#define DIVIDER_X       230   // 1px vertical divider
#define CHAT_Y   (STATUS_H + TAB_H) // top of chat/node area
#define INPUT_H          52   // input area (typed text + touch nav buttons)
#define CHAT_H         (LCD_H - CHAT_Y - INPUT_H) // height of chat area
#define INPUT_Y        (LCD_H - INPUT_H)          // top of input area

// Font0: 6×8 px monospace (glyph is 7px tall; 8th row is blank inter-line gap)
#define CHAR_W            6
#define CHAR_H            8   // actual font cell height (used for cursor / input bar)
// LINE_H and VISIBLE_LINES are runtime globals set at startup from chatSpacing config.
// Declared in main.cpp, extern here so all modules can reference them.
extern int LINE_H;          // row stride in channel/node/settings panels
extern int VISIBLE_LINES;   // visible rows at LINE_H spacing

#define DM_LINE_H        11   // row stride in DM conversation view (8px char + 3px gap)
#define DM_VISIBLE      (CHAT_H / DM_LINE_H) // visible rows at DM_LINE_H spacing
#define MSG_CHARS       (MSG_W / CHAR_W)    // 38 chars per message line
#define NODE_CHARS      (NODE_W / CHAR_W)   // 14 chars in node pane

// ── Message storage ───────────────────────────────────────────
#define MESH_CHANNELS     8   // number of actual LoRa channels (0-7)
#define CHAN_DM           8   // Direct Messages tab (virtual, local-only)
#define CHAN_ANN          9   // Live feed tab       (virtual, local-only)
#define MAX_CHANNELS     10   // MESH_CHANNELS + DM + ANN
#define MAX_MSG_LINES   400   // display lines per channel (in PSRAM)
#define MAX_INPUT_LEN   200
#define MAX_NODES        64
#define MAX_PENDING_ACK   8

// ── Battery ADC ───────────────────────────────────────────────
// T-Deck routes VBAT through a 1:1 voltage divider to GPIO 4.
// Do NOT set this pin as OUTPUT — leave it as analog input.
#define BATT_ADC_PIN    4      // same GPIO as BOARD_BUZZER, used as ADC instead
#define BATT_VMIN       3.0f   // LiPo dead (V)
#define BATT_VMAX       4.2f   // LiPo full (V)
#define BATT_DIV        2.0f   // divider multiplier (two equal resistors → ×2)

// ── Timing ───────────────────────────────────────────────────
#define CURSOR_BLINK_MS   500
#define ACK_TIMEOUT_MS  30000   // give up waiting for ACK after 30s
