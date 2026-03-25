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
#define MESH_CR         8         // 4/8 coding rate
#define MESH_SYNC       0x2B      // Meshtastic sync word
#define MESH_PREAMBLE   16
#define MESH_POWER      22        // dBm (hardware max; ribl_config requests 30)
#define MESH_HOP_LIMIT   7        // from ribl_config
#define MESH_TCXO_V     1.6f      // TCXO voltage for T-Deck SX1262

// ── Node identity (change to your callsign/name) ─────────────
#define MY_LONG_NAME    "Rhino Dev MT"
#define MY_SHORT_NAME   "RDMT"

// ── Fixed position (from ribl_config) ────────────────────────
#define MY_LAT_I        424935424   // lat * 1e7  (42.4935424°N)
#define MY_LON_I       -833880064   // lon * 1e7  (-83.3880064°W)
#define MY_ALT          228         // meters

// ── Display UI zones (landscape 320×240, font0 = 6×8 px) ─────
#define LCD_W           320
#define LCD_H           240

#define STATUS_H         10   // top status bar
#define TAB_H            10   // channel tab bar
#define MSG_W           230   // message pane width
#define NODE_X          231   // node pane left edge
#define NODE_W           89   // node pane width
#define DIVIDER_X       230   // 1px vertical divider
#define CHAT_Y           20   // top of chat/node area (STATUS_H + TAB_H)
#define CHAT_H          208   // height of chat area  (LCD_H - CHAT_Y - INPUT_H)
#define INPUT_H          12   // input bar height
#define INPUT_Y         228   // top of input bar     (LCD_H - INPUT_H)

// Font0: 6×8 px monospace
#define CHAR_W            6
#define CHAR_H            8
#define MSG_CHARS       (MSG_W / CHAR_W)    // 38 chars per message line
#define VISIBLE_LINES   (CHAT_H / CHAR_H)   // 26 visible message rows
#define NODE_CHARS      (NODE_W / CHAR_W)   // 14 chars in node pane

// ── Message storage ───────────────────────────────────────────
#define MESH_CHANNELS     8   // number of actual LoRa channels
#define CHAN_ANN          8   // Announcements tab (virtual, local-only)
#define MAX_CHANNELS      9   // MESH_CHANNELS + 1 (ANN)
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
