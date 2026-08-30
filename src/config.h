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
//
// Every DEVICE_* target must be listed here. A target missing from this list
// still gets DEVICE_TDECK forced on underneath it, and because board.h tests
// DEVICE_TDECK first, the build then silently compiles against the T-Deck pin
// map — the real target's header is never included at all.
#if !defined(DEVICE_TDECK) && !defined(DEVICE_TLORA_PAGER_TFT) && !defined(DEVICE_CARDPUTER_LORA_HAT) && !defined(DEVICE_HELTEC_V4_EXPANSION) && !defined(DEVICE_MESH_DECK) && !defined(DEVICE_M9) && !defined(DEVICE_SQUARE)
#  define DEVICE_TDECK 1
#endif

#ifndef DEVICE_UI_VERTICAL
#  define DEVICE_UI_VERTICAL 0
#endif

#if defined(DEVICE_SQUARE)
#  define HAS_ENV_SENSOR_TELEMETRY 0
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#  define HAS_ENV_SENSOR_TELEMETRY 1
#else
#  define HAS_ENV_SENSOR_TELEMETRY 0
#endif

// Terrain line-of-sight ("LOS") between this node and a contact.
//
// Off on the Cardputer. Not for the reason Locate is — a 24-point line costs
// nothing next to a decoded PNG — but because the elevation endpoint has to be
// configured somewhere, and that board serves web config in lite form only,
// with no Utilities tab to put the field on. A feature that can be reached but
// never configured is worse than one that is absent.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#  define HAS_NODE_LOS 0
#else
#  define HAS_NODE_LOS 1
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
// SX1262 RX boosted gain. ~2 dB more sensitivity for ~2 mA more receive
// current, paid continuously because the radio idles in RX. 1 keeps the
// long-standing behaviour; set 0 (or loraRxBoostedGain in YAML) to trade a
// little range for battery.
#define MY_LORA_RX_BOOST 1

// Narrowest bandwidth this board's radio can actually produce, as a Meshtastic
// bandwidth code (see loraBwFromCode(): 31 means 31.25 kHz, 62 means 62.5 kHz).
// The LR1121 has no sub-GHz LoRa bandwidth below 62.5 kHz, so on that variant
// 31.25 is removed from every UI rather than accepted into config and then
// rejected by setBandwidth() at reconfigure time — a silently dead radio is a
// far worse outcome than an option that was never offered.
#if defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121)
#  define LORA_BW_CODE_MIN 62
#else
#  define LORA_BW_CODE_MIN 31
#endif

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
// The Mesh Deck has no HardwareModel of its own — it is not in the upstream
// enum. PRIVATE_HW is the value Meshtastic reserves for exactly this case, so
// the node advertises "custom hardware" instead of impersonating another board.
// Allocated upstream for this board, so it can advertise itself honestly
// rather than falling back to PRIVATE_HW the way the Mesh Deck and M9 do.
#define MESH_HW_MODEL_SQUARE        137
#define MESH_HW_MODEL_PRIVATE_HW    255

#if defined(DEVICE_TDECK)
#define MY_HW_MODEL MESH_HW_MODEL_T_DECK
#elif defined(DEVICE_TLORA_PAGER_TFT)
#define MY_HW_MODEL MESH_HW_MODEL_T_LORA_PAGER
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
#define MY_HW_MODEL MESH_HW_MODEL_M5_CARDPUTER
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
#define MY_HW_MODEL MESH_HW_MODEL_HELTEC_V4
#elif defined(DEVICE_MESH_DECK)
#define MY_HW_MODEL MESH_HW_MODEL_PRIVATE_HW
#elif defined(DEVICE_SQUARE)
#define MY_HW_MODEL MESH_HW_MODEL_SQUARE
#elif defined(DEVICE_M9)
// Meshtastic's HardwareModel enum has no ThinkNode M9 — the M1/M2 are in it,
// the M9 is a MeshCore device. Advertising one of those, or the T-Deck the
// #else below would hand out, would put a hardware name on the mesh that does
// not match what is transmitting.
#define MY_HW_MODEL MESH_HW_MODEL_PRIVATE_HW
#else
#define MY_HW_MODEL MESH_HW_MODEL_T_DECK
#endif

#define MY_GPS_ENABLED  1     // runtime default (can be toggled via web config)
// Park the receiver in standby between position samples, using gpsPollIntervalS
// as the period. Continuous tracking is ~20-25 mA, which dominates the current
// draw of a screen-off device — but the standby command dialect ("L76K" ships
// as both CASIC and MediaTek silicon) cannot be probed at runtime, so this is
// opt-in until confirmed working on a given unit. It self-disables if a wake
// ever fails to produce NMEA.
#define MY_GPS_DUTY_CYCLE 0

// ── Fixed position (from ribl_config) ────────────────────────
// Used as the startup default when HAS_GPS == 0.
#define MY_LAT_I        424935424   // lat * 1e7  (42.4935424°N)
#define MY_LON_I       -833880064   // lon * 1e7  (-83.3880064°W)
#define MY_ALT          228         // meters

#define MY_DEVICE_ROLE      0      // CLIENT
#define MY_REBROADCAST      0      // ALL
#define MY_NODEINFO_INTV  900      // 15 min (seconds)
#define MY_POS_INTV      1800      // 30 min (seconds)
// Master switch for putting our coordinates on the mesh. On by default: every
// build before this setting existed broadcast position, and a device that
// silently stopped after an update would be the surprise, not the reverse.
#define MY_SHARE_LOCATION   1
// How much of our coordinates actually goes on the air, in bits kept per
// coordinate (Meshtastic's position_precision). 32 = exact. Defaults to exact
// because every build before this setting broadcast exact coordinates, and
// quietly starting to lie about where a device is would be the surprise.
// Coarsening is a decision the operator makes; see kPositionPrecisions.
#define MY_POSITION_PRECISION 32
#define MY_GPS_POLL_S      60      // seconds between GPS position samples into s_cfg
#define MY_REGION        "US"
#define MY_TZ_DEF        "EST5EDT,M3.2.0,M11.1.0"   // Eastern (Detroit)

// ── Display defaults ──────────────────────────────────────────
// Backlight level as a percentage, adjustable in 10% steps. The default is
// derived from the board's TFT_BRIGHTNESS_DEFAULT duty (0-255) and rounded to
// the nearest step, so an unconfigured device keeps the brightness its hardware
// profile always used.
#define MY_BRIGHTNESS_PCT \
    ((((TFT_BRIGHTNESS_DEFAULT * 100 + 127) / 255) + 5) / 10 * 10)
#define BRIGHTNESS_PCT_MIN  10
#define BRIGHTNESS_PCT_MAX  100
#define BRIGHTNESS_PCT_STEP 10

#define MY_SCREEN_ON_SECS   30     // 30 s
#define MY_DISPLAY_UNITS    0      // METRIC
// How the local battery reads in the chat header and the web status chip.
// 0 = PERCENT (the behavior before the setting existed), 1 = VOLTAGE.
#define MY_BATT_DISPLAY     0
#define MY_COMPASS_NORTH    0
#define MY_FLIP_SCREEN      0
#define MY_UI_THEME         0      // 0=CAMELLIA, 1=EVERGREEN, 2=EARTHEN, 3=SOLARIZED, 4=CRIMSON, 5=SCARLET_POP, 6=INK_WASH, 7=LAVENDAR_FIELDS, 8=WILD_FLOWERS, 9=QUIET_LUXURY, 10=MORNING_DEW, 11=WINTER_CHILL, 12=CAMELLIA_BLACK
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

// ── External BLE keyboard defaults ─────────────────────────────
// Off, deliberately. A resident NimBLE stack costs 30-40 KB of internal DRAM,
// which is the same pool the runtime TLS thresholds guard, so every board must
// be able to decline the feature rather than pay for it at boot.
#define MY_BLE_KBD_ENABLED  0

// ── Bluetooth defaults ─────────────────────────────────────────
#define MY_BT_ENABLED       1
#define MY_BT_MODE          0      // RANDOM_PIN
#define MY_BT_PIN           123456

// ── Network defaults ───────────────────────────────────────────
#define MY_WIFI_ENABLED     1      // master WiFi switch (gates web config + MQTT)
// Auto-stop the web config server after this long with no HTTP request. Web
// config disables Wi-Fi modem power-save (it has to; the synchronous server
// stalls otherwise), so leaving it up is one of the most expensive things the
// device can do. 0 = never time out.
#define MY_WEBCFG_IDLE_S    600    // 10 minutes
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
// Listen for Meshtastic MeshBeacon packets (port 37) — advertisements from
// other meshes carrying a channel/preset/region offer. Purely passive: it
// decodes what already arrived and never transmits, and an offer is only ever
// shown, never applied. Off by default because it is an opt-in curiosity, not
// something a device needs in order to work on its own mesh.
#define MY_MESH_BEACON_LISTEN 0
#define MY_NEIGHBORINFO_INTV 21600
#define MY_NEIGHBORINFO_LORA 1
#define NEIGHBORINFO_MIN_INTERVAL_S 14400

// Discovery (Live -> Tools -> Discovery) costs ~3 KB of internal DRAM: a
// 24-entry neighbor-report table in NodeDB plus the buffer its results are
// rendered into. Everywhere else that is nothing; on the Cardputer it is the
// difference between booting and not. First-boot onboarding there brings up a
// WiFi AP and the lite web config with under 8 KB of heap free and a largest
// free block near 6.6 KB, and those 3 KB pushed the display flush into a null
// DMA descriptor — a boot loop, with mbedtls failing to allocate alongside it.
// The 240x135 panel is also the worst place to read a topology list. Off there,
// on everywhere else. NeighborInfo TX and the NodeInfo request reply are not
// gated by this: they cost no RAM and are how other nodes see us.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define FEATURE_DISCOVERY 0
#else
#define FEATURE_DISCOVERY 1
#endif
// MQTT Monitor (Live -> Tools -> MQTT) is a census of the channels arriving
// under the configured root. It rides the bridge's existing subscription, so it
// costs nothing until it is opened and a bounded ~1 KB of heap while it is — but
// it is meaningless on a build that cannot reach a broker at all, so it follows
// the master WiFi switch. Off on Cardputer for the same reason Discovery is:
// that board has no heap to spare at first boot, and a two-column list on a
// 240x135 panel is not where you would want to read this anyway.
#if MY_WIFI_ENABLED && !defined(DEVICE_CARDPUTER_LORA_HAT)
#define FEATURE_MQTT_MONITOR 1
#else
#define FEATURE_MQTT_MONITOR 0
#endif
#define MY_CANNED_EN        1
#define MY_CANNED_MSGS      "Hi|Bye|Yes|No|Ok"
#define MY_SNF_CLIENT_EN    1   // Store and Forward: act as client (receive replayed messages)
// Store and Forward: pin the router to ask for replays, as a raw node id.
// 0 = unset — discover the router from its broadcast heartbeat instead.
#define MY_SNF_ROUTER_ID    0
// Ask the release server for a newer build once per boot and offer to install
// it. Opt-out: the check is a single plain-HTTP GET and costs nothing when
// there is no update, so it is on by default.
#define MY_OTA_AUTOCHECK    1
#define MY_NODE_ARCHIVE_EN  0   // opt-in: archive nodes evicted from the full table to SD
#define MY_AUTOFAV_ENABLED  0      // opt-in: auto-favorite nodes within range
// Auto-favorite threshold, in meters. One round unit in whichever system the
// display is set to, so the field reads "1.00" rather than "0.62" or "1.61"
// until someone changes it. See cfgDefaultAutoFavRangeM().
#define MY_AUTOFAV_RANGE_KM_M  1000   // 1 km, metric default
#define MY_AUTOFAV_RANGE_MI_M  1609   // 1 mile, imperial default
#define MY_CHAT_SPACING     1   // 0=Tight(8px), 1=Normal(10px), 2=Loose(12px)

// Chat/DM message font size. Medium matches the built-in size; Small/Large step
// one Montserrat size down/up from it, Extra Large two up. Applies to the chat
// and DM screens only.
#define FONT_SIZE_SMALL     0
#define FONT_SIZE_MEDIUM    1
#define FONT_SIZE_LARGE     2
#define FONT_SIZE_XLARGE    3
#define FONT_SIZE_MAX       FONT_SIZE_XLARGE
#define MY_FONT_SIZE        FONT_SIZE_MEDIUM

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

// Notification volume, percent. Scales the tone amplitude on boards that
// synthesize audio (Pager/Square/T-Deck I2S, Cardputer speaker). Boards that alert
// through a plain piezo buzzer have no amplitude control, so the setting is
// hidden there rather than shown doing nothing. Same 10% steps as brightness.
#define VOLUME_PCT_MIN   0
#define VOLUME_PCT_MAX   100
#define VOLUME_PCT_STEP  10
#define MY_VOLUME_PCT    50

// Whether the board can make a sound at all. Mirrors the branches that actually
// exist in triggerMessageAlert() and playSplashStartupRiff(): the boards with a
// synthesized-audio path, plus anything with a passive buzzer on a GPIO. The Mesh
// Deck has neither — BOARD_BUZZER is -1 and no I2S/codec path is wired — so
// every tone call there compiles to nothing, and the alert-sound, splash-melody
// and volume settings were UI for hardware that does not exist.
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK) || defined(DEVICE_SQUARE) \
    || defined(DEVICE_CARDPUTER_LORA_HAT) || (BOARD_BUZZER >= 0)
#define HAS_AUDIO_ALERTS 1
#else
#define HAS_AUDIO_ALERTS 0
#endif

// A passive buzzer driven by tone() has no amplitude control — only boards that
// synthesize or route audio can honor a volume setting. Gate the UI on this so
// the option is absent where it would do nothing rather than present and inert.
//
// The HAS_AUDIO_ALERTS term matters: this used to be "no buzzer", which is true
// of a board with a codec *and* of a board with no audio hardware whatsoever,
// so the Mesh Deck was offered a volume slider for silence.
#define HAS_VOLUME_CONTROL (HAS_AUDIO_ALERTS && (BOARD_BUZZER < 0))

// Offer a scroll-direction preference for the trackball. Named by board rather
// than derived from HAS_TRACKBALL on purpose: the T-Deck is the only build this
// is wanted on for now, and a capability test would silently hand the option to
// the next board that declares a trackball. The Pager has one and is excluded —
// its wheel already carries direction logic of its own (kPagerWheelChatNav flips
// wheel input against j/k), so a second inversion stacked on that needs testing
// on that hardware first. Add boards here deliberately, one at a time.
#if defined(DEVICE_TDECK)
#define HAS_SCROLL_INVERT 1
#else
#define HAS_SCROLL_INVERT 0
#endif
#define MY_INVERT_SCROLL 0

// ── Bottom icon nav bar (optional) ───────────────────────────────────────────
// Where the nav bar is one of two ways to get around rather than the only one,
// it is a setting. That means a board with a keyboard *and* a touch panel — the
// T-Deck and the Mesh Deck today. On a touch-only board there is nothing to
// fall back to, so the bar is not negotiable and no toggle is offered.
// UI_TOUCH_NAV_BAR / UI_TOUCH_ONLY_PROFILE live in hal/board.h, which config.h
// has already pulled in by this point.
#if UI_TOUCH_NAV_BAR && !UI_TOUCH_ONLY_PROFILE
#define HAS_NAV_BAR_TOGGLE 1
#else
#define HAS_NAV_BAR_TOGGLE 0
#endif
// On by default: the bar shipped on before it was optional, and this keeps a
// device that upgrades looking like the release it upgraded from.
#define MY_NAV_BAR_ENABLED 1

// A notification LED that blinks while messages are unread. Only the Mesh Deck
// has one wired (RGB cathodes on expander 0x59 P10..P12, driven by
// meshDeckServiceLed). Gate the setting on this so it is absent, not inert,
// everywhere else.
#if defined(DEVICE_MESH_DECK)
#define HAS_NOTIFY_LED 1
#else
#define HAS_NOTIFY_LED 0
#endif
#define MY_NOTIFY_LED_ENABLED 1
// Mesh Deck RGB notification LED defaults.
// 0=Red, 1=Green, 2=Blue, 3=Yellow, 4=Cyan, 5=Magenta, 6=White
#define MY_NOTIFY_LED_COLOR_CHANNEL 2
#define MY_NOTIFY_LED_COLOR_DM      5

// Blinking the keyboard backlight as a message notification. Two boards have a
// backlight to blink, and they drive it very differently: the T-Deck's is
// PWM-owned by the keyboard's own ESP32-C3 and set over I2C, the Pager's is a
// plain KB_BL GPIO. They also differ in resting state, which is why the Pager
// only blinks with the screen asleep — see kbBlinkAllowedNow().
//
// Deliberately NOT the M9, which looks like it belongs here and does not. Its
// keypad LEDs belong to the companion controller, and KB_REG_BACKLIGHT only sets
// the brightness that controller uses for its OWN keypress auto-light — the host
// has no way to turn the LEDs on. Verified on hardware (early ESP32-S2
// controller, hw=0x03 fw=0x10): writing 255 lights nothing, and the pattern's
// closing write of 0 disables the auto-light until the next power cycle. Wiring
// the M9 up here breaks the keypad light rather than blinking it.
#if defined(DEVICE_TDECK) || defined(DEVICE_TLORA_PAGER_TFT)
#define HAS_KB_BLINK 1
#else
#define HAS_KB_BLINK 0
#endif
#define MY_KB_BLINK_ENABLED 1
// Flashes per notification cycle. DMs get the longer pattern: with no
// notification LED on either board, length is what distinguishes them.
#define MY_KB_BLINK_CHAN_FLASHES 1
#define MY_KB_BLINK_DM_FLASHES   2

// True on any board with a light to blink. The notification LED and the
// keyboard backlight never coexist — HAS_NOTIFY_LED is the Mesh Deck and
// HAS_KB_BLINK is the T-Deck and Pager — so the timeout below is one setting
// covering whichever of the two a board actually has.
#define HAS_LIGHT_NOTIFY (HAS_KB_BLINK || HAS_NOTIFY_LED)
// How long the light keeps reminding after a message arrives, in seconds.
// 0 = never stop, which is what every build before this setting did: blink
// until the message is read. Defaulting to anything else would silently change
// what a device in the field does the moment it takes an update.
#define MY_NOTIFY_LIGHT_TIMEOUT_S 0
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
#elif UI_TOUCH_ONLY_PROFILE
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
// Configurable LoRa channels. Ten everywhere except the Cardputer, which stays
// at eight: each channel owns a history ring of MAX_MSG_LINES x sizeof(
// DisplayLine), and that board has no PSRAM to put them in — ~3.5 KB of internal
// DRAM per channel, on a device whose first-boot AP already runs with a largest
// free block near 6.6 KB. Everywhere else the rings live in PSRAM and two more
// cost ~54 KB of eight megabytes.
//
// Nothing on the air constrains this. A Meshtastic header carries a channel
// *hash*, never a slot number, so a node with ten channels and a stock node with
// eight interoperate exactly as before; the count is local bookkeeping. See
// issue #44.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define MESH_CHANNELS     8
#else
#define MESH_CHANNELS    10
#endif
// The two virtual tabs sit immediately above the real channels, so they move
// with the count rather than being pinned at 8 and 9.
#define CHAN_DM          (MESH_CHANNELS)      // Direct Messages tab (virtual, local-only)
#define CHAN_LIVE        (MESH_CHANNELS + 1)  // Live feed tab (virtual, local-only)
#define CHAN_ANN   CHAN_LIVE  // Deprecated alias (kept for compatibility during refactors)
#define MAX_CHANNELS     (MESH_CHANNELS + 2)  // MESH_CHANNELS + DM + LIVE
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#define MAX_MSG_LINES    64   // DRAM-sized history for Cardputer (leave headroom for Wi-Fi/tasks)
#else
#define MAX_MSG_LINES   400   // display lines per channel
#endif
#define MESH_TEXT_MAX_LEN 200
#if defined(DEVICE_CARDPUTER_LORA_HAT)
// Also shrinks several MAX_NODES-sized UI index/snapshot arrays in main_lvgl.cpp.
#define MAX_NODES         50
#else
#define MAX_NODES        250
#endif

// ── Maps ──────────────────────────────────────────────────────
// The live Locate canvas, offline z/x/y tile cache, Web Config downloader, and
// migration cleanup for obsolete state/detail map files.
//
// Off on two boards, for the same underlying reason — not enough memory to
// decode a map — reached from opposite directions:
//
//   * Cardputer: no PSRAM at all. One map costs ~490 KB transiently, more than
//     that board's entire LVGL pool. It also serves web config in lite form
//     only, so the download UI could never be reached there in any case.
//   * Heltec V4: 2 MB PSRAM shared with everything else. The live RGB565 canvas
//     plus one decoded 256x256 tile does not leave enough measured headroom.
//     Revisit if an R8 (8 MB) variant appears.
//
// Compiling it out keeps the tile worker, canvas, handlers, and downloader
// JavaScript out of images whose memory budgets are the tightest here.
#if defined(DEVICE_CARDPUTER_LORA_HAT) || defined(DEVICE_HELTEC_V4_EXPANSION)
#define HAS_STATE_MAPS 0
#else
#define HAS_STATE_MAPS 1
#endif
// How far back a Store-and-Forward replay request asks for, in minutes. The
// router clamps this to its own history_return_window, so asking for more than
// it kept is harmless. Shared so the device row and the web button agree.
#define SNF_HISTORY_WINDOW_MIN 240
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
// How long a unicast waits for a routing ACK before it is shown as failed.
//
// Sized against what the peer is actually doing rather than picked round. A
// Meshtastic sender retries a reliable packet NUM_RELIABLE_RETX (3) times, and
// recomputes the gap before each attempt as
//   2*airtime + (2^CWsize + 2*CWmax + 2^((CWmax+CWmin)/2))*slotTime + 4500 ms
// where CWsize scales with channel utilization. On LongFast that is roughly 7 s
// per attempt on an idle channel (~22 s to give up) but around 14 s on a
// saturated one (~43 s). The old 30 s sat inside that upper range, so a busy
// mesh could show NAKED while the peer was still legitimately retrying, and a
// later ACK would land against a record already written off.
#define ACK_TIMEOUT_MS  60000   // give up waiting for ACK after 60s
