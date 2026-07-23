#pragma once
// ════════════════════════════════════════════════════════════════════════════
// config.h — Project-wide compile-time configuration
//
// This file provides two categories of constants:
//
//   1. Hardware pin and board definitions — delegated to src/hal/board.h,
//      which selects the appropriate per-device header based on the
//      DEVICE_* build flag set in platformio.ini.
//
//   2. Radio, node identity, and UI defaults — compile-time values that can
//      be overridden before flashing via platformio.ini build_flags.
//
// To add a new hardware target, see the instructions in src/hal/board.h.
// ════════════════════════════════════════════════════════════════════════════

// Default to T-Deck when no device is specified (useful for IDE code analysis).
#if !defined(DEVICE_TDECK) && !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_CARDPUTER_LORA_HAT) && !defined(DEVICE_HELTEC_V4_EXPANSION)
#  define DEVICE_TDECK 1
#endif

#ifndef DEVICE_UI_VERTICAL
#  define DEVICE_UI_VERTICAL 0
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
#  define HAS_ENV_SENSOR_TELEMETRY 1
#else
#  define HAS_ENV_SENSOR_TELEMETRY 0
#endif

// ── Hardware pin definitions (per-device) ────────────────────────────────────
// All BOARD_*, TFT_*, LORA_*, GPS_*, BATT_*, HAS_* macros are defined here.
#include "hal/board.h"

// Everything below this line is NOT device-specific — it applies to all builds.

// ── Meshtastic LoRa (LongFast preset, US 915 MHz) ────────────
// These match the Meshtastic LONG_FAST channel preset and can be tuned via
// runtime web config, but the compile-time values serve as hardware defaults.
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

// ── Meshtastic HardwareModel advertised in NODEINFO ─────────
// Source: meshtastic/protobufs meshtastic/mesh.proto (HardwareModel enum).
// Use per-target values so each firmware reports its actual hardware class.
#define MESH_HW_MODEL_T_DECK        50
#define MESH_HW_MODEL_T_LORA_PAGER  103
#define MESH_HW_MODEL_HELTEC_V4     110
#define MESH_HW_MODEL_M5_CARDPUTER  112

#if defined(DEVICE_TDECK)
#define MY_HW_MODEL MESH_HW_MODEL_T_DECK
#elif defined(DEVICE_TLORA_PAGER_TFT)
#define MY_HW_MODEL MESH_HW_MODEL_T_LORA_PAGER
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
#define MY_HW_MODEL MESH_HW_MODEL_M5_CARDPUTER
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#define MY_HW_MODEL MESH_HW_MODEL_HELTEC_V4
#else
#define MY_HW_MODEL MESH_HW_MODEL_T_DECK
#endif

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
#define MY_GPS_POLL_S      60      // seconds between GPS position samples into s_cfg
#define MY_REGION        "US"
#define MY_TZ_DEF        "EST5EDT,M3.2.0,M11.1.0"   // Eastern (Detroit)

// ── Display defaults ──────────────────────────────────────────
#define MY_SCREEN_ON_SECS   30     // 30 s
#define MY_DISPLAY_UNITS    0      // METRIC
#define MY_COMPASS_NORTH    0
#define MY_FLIP_SCREEN      0
#define MY_UI_THEME         0      // 0=CAMELLIA, 1=EVERGREEN, 2=EARTHEN, 3=SOLARIZED, 4=CRIMSON, 5=SCARLET_POP, 6=INK_WASH, 7=LAVENDAR_FIELDS, 8=WILD_FLOWERS, 9=QUIET_LUXURY, 10=MORNING_DEW, 11=WINTER_CHILL
#define MY_UI_MODE          0      // 0=DARK, 1=LIGHT

// Chat rendering style (applied at boot; change requires a reboot)
#define CHAT_STYLE_CLASSIC  0      // legacy flat colored text lines
#define CHAT_STYLE_BUBBLES  1      // per-node colored (filled) message bubbles
#define CHAT_STYLE_OUTLINE  2      // per-node colored outlined bubbles (transparent fill)
#define CHAT_STYLE_MAX      CHAT_STYLE_OUTLINE
#define MY_CHAT_STYLE       CHAT_STYLE_CLASSIC
#define MY_CHAT_COLORS_EN   1      // classic mode: per-node text colors

// Sender name style shown in chat (channel-chat prefix + bubble name tag)
#define CHAT_NAME_SHORT     0      // 4-char short name (e.g. "ABCD")
#define CHAT_NAME_LONG      1      // full long name when the node has advertised one
#define CHAT_NAME_MAX       CHAT_NAME_LONG
#define MY_CHAT_NAME_STYLE  CHAT_NAME_SHORT
#define MY_USER_MSG_COLOR   0xFF   // own-message color: 0..15 palette index, 0xFF=adaptive default

// ── Bluetooth defaults ─────────────────────────────────────────
#define MY_BT_ENABLED       1
#define MY_BT_MODE          0      // RANDOM_PIN
#define MY_BT_PIN           123456

// ── Network defaults ───────────────────────────────────────────
#define MY_WIFI_ENABLED     1      // master WiFi switch (gates web config + MQTT)
#define MY_NTP_SERVER       "meshtastic.pool.ntp.org"
#define MY_MQTT_ENABLED     0
#define MY_MQTT_SERVER      "mqtt.meshtastic.org"
#define MY_MQTT_USER        "meshdev"
#define MY_MQTT_PASS        "large4cats"
#define MY_MQTT_ROOT        "msh/US"
#define MY_MQTT_ENCRYPT     1
#define MY_MQTT_MAP_RPT     0
// Default to plaintext MQTT. A TLS handshake needs ~40KB of contiguous internal
// heap, which is the scarcest resource on these boards; both mqtt.meshtastic.org
// and mqtt.michmesh.net serve plaintext on 1883 with the same credentials.
// Channel payloads stay end-to-end encrypted with the channel key either way, so
// what TLS would add here is only transport-level metadata privacy on a public
// broker. Set 8883/1 to opt back into TLS (also togglable in web config).
#define MY_MQTT_PORT        1883   // 1883 = plaintext (default), 8883 = TLS
#define MY_MQTT_TLS         0      // connect via WiFiClientSecure when set

// ── Power defaults ─────────────────────────────────────────────
#define MY_POWER_SAVING     0
#define MY_LS_SECS          300
#define MY_MIN_WAKE_SECS    10

// ── Module defaults ────────────────────────────────────────────
#define MY_TEL_DEV_EN       1
#define MY_TEL_DEV_INTV     3600
#define MY_TEL_ENV_EN       0
#define MY_TEL_ENV_INTV     3600
#define MY_NEIGHBORINFO_EN  0
#define MY_NEIGHBORINFO_INTV 21600
#define MY_NEIGHBORINFO_LORA 1
#define NEIGHBORINFO_MIN_INTERVAL_S 14400
#define MY_CANNED_EN        1
#define MY_CANNED_MSGS      "Hi|Bye|Yes|No|Ok"
#define MY_SNF_CLIENT_EN    1   // Store and Forward: act as client (receive replayed messages)
#define MY_NODE_ARCHIVE_EN  0   // opt-in: archive nodes evicted from the full table to SD
#define MY_AUTOFAV_ENABLED  0      // opt-in: auto-favorite nodes within range
#define MY_AUTOFAV_RANGE_M  5000   // auto-favorite threshold, meters (5 km / ~3.1 mi)
#define MY_CHAT_SPACING     1   // 0=Tight(8px), 1=Normal(10px), 2=Loose(12px)

#define MSG_ALERT_SOUND_DEFAULT 0
#define MSG_ALERT_SOUND_CHIRPY  1
#define MSG_ALERT_SOUND_BASS    2
#define MSG_ALERT_SOUND_OFF     3
#define MSG_ALERT_SOUND_MAX     MSG_ALERT_SOUND_OFF

#if defined(DEVICE_TLORA_PAGER_TFT)
#define MY_MSG_ALERT_SOUND  MSG_ALERT_SOUND_DEFAULT
#else
#define MY_MSG_ALERT_SOUND  MSG_ALERT_SOUND_DEFAULT
#endif
#define MY_SPLASH_MELODY_ENABLED 1
#define MY_DEBUG_MONITOR    0
#define MY_DBG_ACKS         MY_DEBUG_MONITOR
#define MY_DBG_MESSAGES     MY_DEBUG_MONITOR
#define MY_DBG_GPS          MY_DEBUG_MONITOR

// ── Boot splash typography tuning ─────────────────────────────
// These scales are applied with LovyanGFX setTextSize() on top of the
// selected splash fonts. Keep near 1.0 for best bitmap-font sharpness.
#define MY_SPLASH_TITLE_SCALE      0.82f
#define MY_SPLASH_SUBTITLE_SCALE   0.72f
#define MY_SPLASH_TITLE_Y_OFFSET      0
#define MY_SPLASH_SUBTITLE_GAP_TRIM   0

// Pager-specific splash title scale (Orbitron_Light_32)
#define MY_SPLASH_PAGER_TITLE_SCALE 1.18f

// ── Display UI zones (font0 = 6×8 px) ───────────────────────
#if DEVICE_UI_VERTICAL
#define LCD_W           DEVICE_LCD_PORTRAIT_W
#define LCD_H           DEVICE_LCD_PORTRAIT_H

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define STATUS_H         20
#define TAB_H            12
#define MSG_W           104
#define NODE_X          105
#define NODE_W           30
#define DIVIDER_X       104
#define INPUT_H          18
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#define STATUS_H         28   // top status bar
#define TAB_H            14   // channel tab bar
#define MSG_W           205   // reclaim more width from shortname-only node pane
#define NODE_X          206   // node pane left edge
#define NODE_W           34   // minimal node pane for shortname-only rows
#define DIVIDER_X       205   // 1px vertical divider
#define INPUT_H          42   // touch controls fit better in portrait
#else
#define STATUS_H         28   // top status bar
#define TAB_H            14   // channel tab bar
#define MSG_W           170   // message pane width
#define NODE_X          171   // node pane left edge
#define NODE_W           69   // node pane width
#define DIVIDER_X       170   // 1px vertical divider
#define INPUT_H          42   // touch controls fit better in portrait
#endif
#else
#define LCD_W           DEVICE_LCD_LANDSCAPE_W
#define LCD_H           DEVICE_LCD_LANDSCAPE_H

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define STATUS_H         18
#define TAB_H            12
#define MSG_W           201
#define NODE_X          202
#define NODE_W           38
#define DIVIDER_X       201
#define INPUT_H          18
#elif defined(DEVICE_TDECK)
#define STATUS_H         32   // top status bar (slightly taller for richer status icons)
#define TAB_H            14   // channel tab bar (taller for labeled pills)
#define MSG_W           283   // reclaim most side whitespace from node pane
#define NODE_X          284   // node pane left edge
#define NODE_W           36   // compact node pane for shortname-only rows
#define DIVIDER_X       283   // 1px vertical divider
#define INPUT_H          37   // input area (typed text + touch nav buttons)
#elif defined(DEVICE_TLORA_PAGER_TFT)
#define STATUS_H         30   // taller header to avoid overlap with tab row
#define TAB_H            15   // 1.25x larger channel list tabs for pager readability
#define MSG_W           443   // reclaim most side whitespace from node pane
#define NODE_X          444   // node pane left edge
#define NODE_W           36   // compact side node pane for shortname-only rows
#define DIVIDER_X       443   // 1px vertical divider
#define INPUT_H          30   // keeps DM composer visible while maximizing chat rows
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#define STATUS_H         32   // top status bar (slightly taller for richer status icons)
#define TAB_H            14   // channel tab bar (taller for labeled pills)
#define MSG_W           283   // reclaim most side whitespace from node pane
#define NODE_X          284   // node pane left edge
#define NODE_W           36   // compact node pane for shortname-only rows
#define DIVIDER_X       283   // 1px vertical divider
#define INPUT_H          37   // input area (typed text + touch nav buttons)
#else
#define STATUS_H         32   // top status bar (slightly taller for richer status icons)
#define TAB_H            14   // channel tab bar (taller for labeled pills)
#define MSG_W           230   // message pane width
#define NODE_X          231   // node pane left edge
#define NODE_W           89   // node pane width
#define DIVIDER_X       230   // 1px vertical divider
#define INPUT_H          37   // input area (typed text + touch nav buttons)
#endif
#endif

#define CHAT_Y   (STATUS_H + TAB_H) // top of chat/node area
#define CHAT_H         (LCD_H - CHAT_Y - INPUT_H) // height of chat area
#define INPUT_Y        (LCD_H - INPUT_H)          // top of input area

// Base text scaling is pager-only for readability experiments.
#if defined(DEVICE_TLORA_PAGER_TFT)
#define UI_BASE_TEXT_SCALE 1.0f
#define UI_BODY_FONT       (&fonts::Font0)
#define CHAR_W            6
#define CHAR_H            8
#define DM_LINE_H         14
#else
#define UI_BASE_TEXT_SCALE 1.0f
#define UI_BODY_FONT       (&fonts::Font0)
#define CHAR_W            6
#define CHAR_H            8
#define DM_LINE_H         11
#endif
// CHAR_H is the actual font cell height used for cursor/input positioning.
// LINE_H and VISIBLE_LINES are runtime globals set at startup from chatSpacing config.
// Declared in the active UI entrypoint (main_lvgl.cpp), extern here so all modules can reference them.
extern int LINE_H;          // row stride in channel/node/settings panels
extern int VISIBLE_LINES;   // visible rows at LINE_H spacing
#define DM_VISIBLE      (CHAT_H / DM_LINE_H) // visible rows at DM_LINE_H spacing
// Channel windows now use full-width chat, so wrapping should use display width.
#if defined(DEVICE_TLORA_PAGER_TFT)
#define MSG_CHARS       53   // Pager uses wider chat glyphs; keep wraps within visible row width.
#elif defined(DEVICE_TDECK)
#define MSG_CHARS       48   // T-Deck chat now spans the main pane; allow longer pre-wrapped rows.
#else
#define MSG_CHARS       (LCD_W / CHAR_W)
#endif
#define NODE_CHARS      (NODE_W / CHAR_W)   // derived chars in node pane

// ── Message storage ───────────────────────────────────────────
#define MESH_CHANNELS     8   // number of actual LoRa channels (0-7)
#define CHAN_DM           8   // Direct Messages tab (virtual, local-only)
#define CHAN_LIVE         9   // Live feed tab       (virtual, local-only; telemetry/routing/etc.)
#define CHAN_ANN   CHAN_LIVE  // Deprecated alias (kept for compatibility during refactors)
#define MAX_CHANNELS     10   // MESH_CHANNELS + DM + LIVE
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define MAX_MSG_LINES    64   // DRAM-sized history for Cardputer (leave headroom for Wi-Fi/tasks)
#else
#define MAX_MSG_LINES   400   // display lines per channel
#endif
#define MESH_TEXT_MAX_LEN 200
#define MAX_NODES        250
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
