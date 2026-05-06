#pragma once

// Board, radio, UI, and default runtime configuration constants.

#if !defined(DEVICE_TDECK) && !defined(DEVICE_HELTEC_V4_EXPANSION)
#define DEVICE_TDECK 1
#endif

#ifndef DEVICE_UI_VERTICAL
#define DEVICE_UI_VERTICAL 0
#endif

// ── Power & Peripherals ──────────────────────────────────────
#if defined(DEVICE_TDECK)
#define BOARD_POWERON   10
#define BOARD_BUZZER     4
#define BOARD_VEXT_ENABLE    -1
#define BOARD_VEXT_ON_LEVEL  HIGH

// Display SPI (ST7789, landscape 320x240)
#define TFT_SPI_HOST    SPI2_HOST
#define TFT_SPI_SCK     40
#define TFT_SPI_MISO    38
#define TFT_SPI_MOSI    41
#define TFT_SPI_3WIRE   false
#define TFT_SPI_WRITE_HZ 40000000
#define TFT_SPI_READ_HZ  16000000
#define TFT_CS          12
#define TFT_DC          11
#define TFT_BL          42
#define TFT_BL_INVERT   false
#define TFT_BL_FREQ     12000
#define TFT_BL_PWM_CH   0
#define TFT_BRIGHTNESS_DEFAULT 128
#define TFT_RST         -1
#define TFT_PANEL_WIDTH 240
#define TFT_PANEL_HEIGHT 320
#define TFT_INVERT      true
#define TFT_RGB_ORDER   false

// LoRa SX1262
#define LORA_SPI_SCK    40
#define LORA_SPI_MISO   38
#define LORA_SPI_MOSI   41
#define LORA_CS         9
#define LORA_DIO1       45
#define LORA_RST        17
#define LORA_BUSY       13
#define LORA_FEM_POWER_PIN   -1
#define LORA_FEM_ENABLE_PIN  -1
#define LORA_FEM_TX_MODE_PIN -1

// microSD (shares SPI bus)
#define SD_CS           39
#define HAS_SD_CARD     1

// I2C keyboard
#define HAS_KEYBOARD    1
#define KB_SDA          18
#define KB_SCL           8
#define KB_ADDR       0x55
#define KB_INT          46

// Touch controller (GT911)
#define HAS_TOUCH        1
#define TOUCH_SDA       18
#define TOUCH_SCL        8
#define TOUCH_ADDR      0x5D
#define TOUCH_INT       16
#define TOUCH_RST       -1
#define TOUCH_I2C_PORT   0
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED 1
#endif

// Trackball
#define HAS_TRACKBALL    1
#define TBALL_UP         3
#define TBALL_DOWN       2
#define TBALL_LEFT       1
#define TBALL_RIGHT     15
#define TBALL_CLICK      0

// No dedicated user button in this profile
#define USER_BUTTON_PIN  -1
#define USER_BUTTON_ACTIVE_LEVEL LOW

// GPS hardware
#define HAS_GPS         1
#define GPS_RX          44
#define GPS_TX          43
#define GPS_BAUD        38400

// Battery ADC
#define BATT_ADC_PIN    4
#define BATT_DIV        2.0f
#define BATT_SENSE_ENABLE_PIN    -1
#define BATT_SENSE_ENABLE_LEVEL  LOW

#define MESH_TCXO_V     1.6f

#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#define BOARD_POWERON   7
#define BOARD_BUZZER    6
#define BOARD_VEXT_ENABLE    36
#define BOARD_VEXT_ON_LEVEL  LOW

// Display SPI (Heltec V4 expansion TFT, ST7789)
#define TFT_SPI_HOST    SPI3_HOST
#define TFT_SPI_SCK     17
#define TFT_SPI_MISO    -1
#define TFT_SPI_MOSI    33
#define TFT_SPI_3WIRE   true
#define TFT_SPI_WRITE_HZ 40000000
#define TFT_SPI_READ_HZ  4000000
#define TFT_CS          15
#define TFT_DC          16
#define TFT_BL          21
#define TFT_BL_INVERT   false
#define TFT_BL_FREQ     44100
#define TFT_BL_PWM_CH   7
#define TFT_BRIGHTNESS_DEFAULT 220
#define TFT_RST         18
#define TFT_PANEL_WIDTH 240
#define TFT_PANEL_HEIGHT 320
#define TFT_INVERT      true
#define TFT_RGB_ORDER   false

// LoRa SX1262
#define LORA_SPI_SCK    9
#define LORA_SPI_MISO   11
#define LORA_SPI_MOSI   10
#define LORA_CS         8
#define LORA_DIO1       14
#define LORA_RST        12
#define LORA_BUSY       13
#define LORA_FEM_POWER_PIN   7
#define LORA_FEM_ENABLE_PIN  2
#define LORA_FEM_TX_MODE_PIN 46

// No SD in this profile by default
#define SD_CS           -1
#define HAS_SD_CARD     0

// No keyboard or trackball in this profile
#define HAS_KEYBOARD    0
#define KB_SDA          -1
#define KB_SCL          -1
#define KB_ADDR       0x00
#define KB_INT          -1

#define HAS_TOUCH        1
#define TOUCH_SDA       47
#define TOUCH_SCL       48
#define TOUCH_ADDR      0x2E
#define TOUCH_INT       -1
#define TOUCH_RST       44
#define TOUCH_I2C_PORT   1
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED 1
#endif

#define HAS_TRACKBALL    0
#define TBALL_UP        -1
#define TBALL_DOWN      -1
#define TBALL_LEFT      -1
#define TBALL_RIGHT     -1
#define TBALL_CLICK     -1

// Heltec USER/BOOT button (active low)
#define USER_BUTTON_PIN   0
#define USER_BUTTON_ACTIVE_LEVEL LOW

#define HAS_GPS         1
#define GPS_RX          38
#define GPS_TX          39
#define GPS_BAUD        38400

#define BATT_ADC_PIN    1
#define BATT_DIV        5.1205f
#define BATT_SENSE_ENABLE_PIN    37
#define BATT_SENSE_ENABLE_LEVEL  LOW

#define MESH_TCXO_V     1.8f
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION) && !DEVICE_UI_VERTICAL
#define TFT_ROTATION_DEFAULT 3
#elif DEVICE_UI_VERTICAL
#define TFT_ROTATION_DEFAULT 0
#else
#define TFT_ROTATION_DEFAULT 1
#endif

// Backward-compatible aliases used across modules.
#define SPI_SCK         LORA_SPI_SCK
#define SPI_MISO        LORA_SPI_MISO
#define SPI_MOSI        LORA_SPI_MOSI

// ── Meshtastic LoRa (LongFast preset, US 915 MHz) ────────────
#define MESH_FREQ       906.875f  // MHz
#define MESH_BW         250.0f    // kHz
#define MESH_SF         11
#define MESH_CR         5         // 4/5 coding rate (Meshtastic LONG_FAST default)
#define MESH_SYNC       0x2B      // Meshtastic sync word
#define MESH_PREAMBLE   16
#define MESH_POWER      22        // dBm (hardware max; ribl_config requests 30)
#define MESH_HOP_LIMIT   7        // from ribl_config

// ── Node identity (change to your callsign/name) ─────────────
#define MY_LONG_NAME    "Camillia"
#define MY_SHORT_NAME   "CaMi"

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
#define MY_UI_THEME         0      // 0=CAMELLIA, 1=EVERGREEN, 2=EARTHEN, 3=SOLARIZED
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

// ── Display UI zones (font0 = 6×8 px) ───────────────────────
#if DEVICE_UI_VERTICAL
#define LCD_W           240
#define LCD_H           320

#define STATUS_H         28   // top status bar
#define TAB_H            14   // channel tab bar
#define MSG_W           170   // message pane width
#define NODE_X          171   // node pane left edge
#define NODE_W           69   // node pane width
#define DIVIDER_X       170   // 1px vertical divider
#define INPUT_H          42   // touch controls fit better in portrait
#else
#define LCD_W           320
#define LCD_H           240

#define STATUS_H         32   // top status bar (slightly taller for richer status icons)
#define TAB_H            14   // channel tab bar (taller for labeled pills)
#define MSG_W           230   // message pane width
#define NODE_X          231   // node pane left edge
#define NODE_W           89   // node pane width
#define DIVIDER_X       230   // 1px vertical divider
#define INPUT_H          37   // input area (typed text + touch nav buttons)
#endif

#define CHAT_Y   (STATUS_H + TAB_H) // top of chat/node area
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
// BATT_ADC_PIN and BATT_DIV are board-specific above.
#ifndef BATT_ADC_PIN
#define BATT_ADC_PIN    -1
#endif
#define BATT_VMIN       3.0f   // LiPo dead (V)
#define BATT_VMAX       4.2f   // LiPo full (V)
#ifndef BATT_DIV
#define BATT_DIV        2.0f
#endif
#ifndef BATT_SENSE_ENABLE_PIN
#define BATT_SENSE_ENABLE_PIN    -1
#endif
#ifndef BATT_SENSE_ENABLE_LEVEL
#define BATT_SENSE_ENABLE_LEVEL  LOW
#endif

// ── Timing ───────────────────────────────────────────────────
#define CURSOR_BLINK_MS   500
#define ACK_TIMEOUT_MS  30000   // give up waiting for ACK after 30s
