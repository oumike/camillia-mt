#if defined(UI_LVGL_POC)

#include <Arduino.h>
#include <esp_system.h>   // esp_reset_reason() for the boot marker below
#include <stdarg.h>       // discoverySectionRow() takes a format
#include <stddef.h>       // offsetof() for the channel blob layout
#include <Wire.h>
#include "config.h"
#include "base64_util.h"
#include "channel_mgr.h"
#include "config_io.h"
#include "admin_client.h"
#include "admin_peers.h"
#include "hal/display.h"
#include "hal/xl9555.h"
#if defined(DEVICE_WIO_TRACKER_L2)
#include "hal/wio_tracker_l2_io.h"
#endif
#if defined(DEVICE_TDISPLAY_P4)
#include "hal/tdisplay_p4_io.h"
#include "hal/tdisplay_p4_haptic.h"
#endif
#if defined(DEVICE_TANMATSU)
#include "hal/tanmatsu_lora.h"
#include "hal/tanmatsu_coproc.h"
#endif
#if HAS_SYSTEM_SETTINGS
#include "app/czero_system.h"   // the launcher's display settings, system audio
#include <vector>
#endif
#include "live_util.h"
#include "live_feed.h"
#include "cs_client.h"
#include "cs_proto.h"
#include "mesh_proto.h"
#include "xeddsa_sig.h"
#include "ack_proof.h"
#include "mesh_radio.h"
#include "mqtt_bridge.h"
#include "i18n.h"   // TR(): UI translations (issue #99)
#include "node_db.h"
#include "wardrive_log.h"
#include "dm_mgr.h"
#include "ignore_list.h"
#include "battery_util.h"
#include "power_mgr.h"
#include "emoji_font.h"
#if HAS_SLEEP_OVERLAY
// Bold face for the sleep-overlay message rows; built only into environments
// that draw the overlay (see the font source and build_src_filter).
LV_FONT_DECLARE(lv_font_montserrat_bold_12);
#endif
#if defined(DEVICE_TDECK_PRO)
// Terminus Bold at the chat sizes, for sender names (chatNameFont()). Built
// from src/fonts/eink/, which only this env compiles.
LV_FONT_DECLARE(lv_font_eink_bold_12);
LV_FONT_DECLARE(lv_font_eink_bold_14);
LV_FONT_DECLARE(lv_font_eink_bold_16);
LV_FONT_DECLARE(lv_font_eink_bold_18);
#endif
#include "env_sensor.h"
#include "gps.h"
#include "los.h"
#include "weather.h"
#include "keyboard.h"
#if defined(DEVICE_MESH_DECK)
#include "aw9523.h"   // FT6636 reset sits on an expander, released at boot
#endif
#include "web_config.h"
#include "community_presets.h"
#if HAS_VNC_HOST
#include "vnc_host.h"
#endif
#if HAS_BLE_KEYBOARD
#include "ble_keyboard.h"
#endif
#include "ota_update.h"
#if HAS_SPELLCHECK
#include "spell.h"
#endif
#include "debug_flags.h"
#include "relay_policy.h"
#include "mesh_beacon.h"
#include "licensed_mode.h"
#include "release_notes.h"   // generated from RELEASE_NOTES.md at build time
#include "utf8_utils.h"
#include "mecp.h"     // MECP emergency codes: decode, compose, beacon (issue #158)
#include "fonts/roboto_splash_fonts.h"
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <lvgl.h>
#if HAS_STATE_MAPS
#include <src/misc/cache/instance/lv_image_cache.h>
#endif
#include <time.h>
#include <sys/time.h>   // settimeofday() for the manual/GPS clock paths
#include <math.h>
#include <ctype.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <soc/soc_caps.h>
#if SOC_USB_SERIAL_JTAG_SUPPORTED && ARDUINO_USB_MODE
#include "hal/usb_serial_jtag_ll.h"   // serviceSerialCommands() re-arms its TX
#endif
#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_chip_info.h>
#endif
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include "storage.h"
#include "state_maps.h"
#include "map_tiles.h"
#include <Curve25519.h>
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_WIO_TRACKER_L2) \
    || defined(DEVICE_TDISPLAY_P4)
#include <AudioBoard.h>
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK) || defined(DEVICE_WIO_TRACKER_L2) \
    || defined(DEVICE_TDISPLAY_P4) || defined(DEVICE_TANMATSU)
#include <driver/i2s.h>
#endif
#include <esp_sleep.h>
#include <driver/gpio.h>
#if defined(DEVICE_CARDPUTER_LORA_HAT)
#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#ifdef KEY_TAB
#undef KEY_TAB
#endif
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#ifdef KEY_ESCAPE
#undef KEY_ESCAPE
#endif
#include <M5Cardputer.h>

#ifdef KEY_BACKSPACE
#undef KEY_BACKSPACE
#endif
#ifdef KEY_TAB
#undef KEY_TAB
#endif
#ifdef KEY_ENTER
#undef KEY_ENTER
#endif
#ifdef KEY_ESCAPE
#undef KEY_ESCAPE
#endif
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_ESCAPE      0x1B
#endif

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

#include "hal/crowpanel_50_fonts.h"   // last: renames fonts for the rest of this file
#include "hal/ui_coord_shim.h"        // after it: doubles coordinates and fonts (UI_COORD_SCALE)

// The UI is split into src/ui/*.inc, which are pasted in here in order and
// compile as this one file: each sees everything declared in the ones above.
// New code goes in the fragment for its screen, or a new one listed here.
#include "ui/01_core.inc"
#include "ui/02_theme_alerts.inc"
#include "ui/03_wifi_ota.inc"
#include "ui/04_power_header.inc"
#include "ui/05_sleep_lock.inc"
#include "ui/06_persistence.inc"
#include "ui/07_compose.inc"
#include "ui/08_cfg_panel.inc"
#include "ui/09_cfg_modals_display.inc"
#include "ui/10_cfg_channels.inc"
#include "ui/11_cfg_modals_system.inc"
#include "ui/12_cfg_beacon_lang.inc"
#include "ui/13_sysstats_nav.inc"
#include "ui/14_live.inc"
#include "ui/15_nodes_list_map.inc"
#include "ui/16_node_locate.inc"
#include "ui/17_nodes_actions.inc"
#include "ui/18_live_tools_mecp.inc"
#include "ui/19_charts_home.inc"
#include "ui/20_beacons_mqtt.inc"
#include "ui/21_dm.inc"
#include "ui/22_cfg_actions.inc"
#include "ui/23_ota_onboarding.inc"
#include "ui/24_keyboard.inc"
#include "ui/25_webcfg_time_touch.inc"
#include "ui/26_header_channels.inc"
#include "ui/27_rx_relay.inc"
#include "ui/28_announce.inc"
#include "ui/29_chat_view.inc"
#include "ui/30_build_ui.inc"

void setup() {
#if defined(DEVICE_TDISPLAY_P4)
    // Before anything else registers a shutdown handler; see the function.
    tdisplayP4InstallFullRestart();
#endif
    Serial.begin(115200);
#if CLI_ON_NATIVE_USB
    USBSerial.begin();
#endif
    delay(120);
    // First thing out of the door, before any peripheral is touched. If this
    // line is missing while the panel shows something, the hang is below the
    // application entirely — bootloader or early init — and no amount of
    // driver-level work will move it. If it prints and the boot then stops,
    // the next line to appear says where.
    Serial.printf("\n[boot] setup() entered, reset reason=%d\n",
                  (int)esp_reset_reason());

    // Which silicon this actually is, printed before anything can fail.
    //
    // Board headers assume a specific part, and those assumptions decide pin
    // maps. The M9's SD chip select is GPIO48 on the stated grounds that a plain
    // ESP32-S3R8 leaves that pin free -- on an R8V/R16V it is SPICLK_N, wired to
    // a 1.8 V differential flash clock, and an SD card selected by it would never
    // answer. That is indistinguishable in a log from an empty slot or a dead
    // card, which is exactly the sort of report that arrives from someone else's
    // device with no way to tell the cases apart.
    //
    // So the variant goes in every log, whether or not anything is wrong. It
    // costs one line at boot and turns "it works on mine" into a comparison.
    {
        esp_chip_info_t chip = {};
        esp_chip_info(&chip);
        Serial.printf("[boot] chip: %s rev%d cores=%d flash=%uMB%s psram=%uKB\n",
                      ESP.getChipModel(),
                      (int)chip.revision,
                      (int)chip.cores,
                      (unsigned)(ESP.getFlashChipSize() / (1024UL * 1024UL)),
                      (chip.features & CHIP_FEATURE_EMB_FLASH) ? " (embedded)" : "",
                      (unsigned)(ESP.getPsramSize() / 1024UL));
    }

    // Match baseline firmware board-power bring-up so keyboard/touch I2C devices are powered.
#if (BOARD_POWERON >= 0)
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
#endif
#if (BOARD_VEXT_ENABLE >= 0)
    pinMode(BOARD_VEXT_ENABLE, OUTPUT);
#if defined(BOARD_VEXT_RAIL_ON_AT_DISPLAY) && BOARD_VEXT_RAIL_ON_AT_DISPLAY
    // Rail parked OFF here and raised with the display below, mirroring the
    // wadamesh MeshCore port on this board:
    //
    //   RefCountedDigitalPin::begin()  -> digitalWrite(pin, !active)   // off
    //   LGFXDisplay::begin()           -> claim() -> digitalWrite(pin, active)
    //                                  -> _lcd.init()
    //
    // Driving it active this early is what broke the touch controller: it holds
    // the rail up through the whole pre-display bring-up, a window that port
    // deliberately keeps powered down.
    digitalWrite(BOARD_VEXT_ENABLE, !BOARD_VEXT_ON_LEVEL);
#else
    digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
#endif
    delay(20);
#endif
#if defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)
    pinMode(USER_BUTTON_PIN,
            (USER_BUTTON_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
#if defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)
    pinMode(DISPLAY_TOGGLE_BUTTON_PIN,
            (DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL == LOW) ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif

#if defined(DEVICE_TDISPLAY_P4)
    if (!tdisplayP4IoBegin()) {
        Serial.println("[tdisplay-p4-io] initialization failed");
    } else if (!tdisplayP4IoResetEsp32C6()) {
        Serial.println("[wifi-p4] initial ESP32-C6 reset failed");
    }
#elif defined(DEVICE_TANMATSU)
    // Coprocessor, then the C6 and the ESP-Hosted link: Wi-Fi, BLE and LoRa
    // all ride it, and its SDIO pins must be set before any of them starts.
    (void)tanmatsuHostedBegin();
#endif

    // Before the display, radio, Wi-Fi and GPS come up, because that current
    // step is exactly what drags a nearly-flat cell under the brownout detector
    // — the decision has to be made ahead of the load, not after it. Also
    // carries the brownout-loop backstop. Does not return if it trips.
    powerMgrBootGate();

    // Explicitly disable OTA-only TLS networking on every boot before any
    // runtime subsystems start. OTA worker flow enables it only when needed.
    otaSetNetworkAllowed(false);

    bool bootOtaRtc = isOtaWorkerModeRequestedRtc();
    bool bootOtaNvs = isOtaWorkerModeRequestedOnce();
    Serial.printf("[ota-worker] boot flags (rtc=%d nvs=%d fw=%s)\n",
                  bootOtaRtc ? 1 : 0,
                  bootOtaNvs ? 1 : 0,
                  APP_VERSION);

#if defined(DEVICE_TLORA_PAGER_TFT)
    // Pager-specific memory guard: run OTA worker before display/keyboard init
    // so TLS gets the largest contiguous internal heap.
    s_otaWorkerUiReady = false;
    {
        bool rtcPending = isOtaWorkerModeRequestedRtc();
        bool nvsPending = isOtaWorkerModeRequestedOnce();
        Serial.printf("[ota-worker] pre-display flags (rtc=%d nvs=%d)\n",
                      rtcPending ? 1 : 0,
                      nvsPending ? 1 : 0);
        if (rtcPending || nvsPending) {
            Serial.printf("[ota-worker] pending before display init (rtc=%d nvs=%d)\n",
                          rtcPending ? 1 : 0,
                          nvsPending ? 1 : 0);
            loadConfigForOtaWorker();
            if (runOtaWorkerModeIfRequested()) {
                Serial.println("[ota-worker] minimal flow finished pre-display init");
            } else {
                Serial.println("[ota-worker] requested pre-display init but could not start");
            }
        }
    }
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer display is owned by M5Cardputer.Display after keyboard begin.
    s_keyboard.begin();
    displayDev().setRotation(TFT_ROTATION_DEFAULT);
    displayDev().setBrightness(TFT_BRIGHTNESS_DEFAULT);
    displayDev().fillScreen(TFT_BLACK);
#else
#if defined(DEVICE_MESH_DECK)
    // The FT6636's reset is not a GPIO — it hangs off expander 0x59 P13 and
    // comes up asserted, so the controller is held in reset and never answers
    // the probe LovyanGFX does inside lcd.init(). Release it first, and bring
    // I2C up here because nothing else has yet (s_keyboard.begin() is below).
    // The 300 ms is the controller's own start-up time; probing sooner finds
    // nothing and touch stays dead for the session.
    // Before the first transfer of the session: a warm reset may have left the
    // bus wedged, and everything below depends on it.
    meshDeckI2cBusRecover();
    Wire.begin(I2C_SDA, I2C_SCL, 100000UL);
    meshDeckReleaseTouchReset();
#endif
#if defined(DEVICE_WIO_TRACKER_L2)
    if (!wioTrackerL2IoBegin()) {
        Serial.println("[wio-l2-io] initialization failed; display may remain unavailable");
    }
#endif
    // Which way up. Has to precede lcd.init() and the setRotation() below it,
    // and it is only an NVS read, so it depends on nothing that is up yet.
    loadBootOrientation();
#if HAS_SLEEP_OVERLAY
    orientationApplyOverlayGeometry();
#endif
#if defined(DEVICE_TDISPLAY_P4)
    // Portrait's taller nav bar (see kBottomNavHeight): set once the boot's
    // orientation is known and before any UI is built. Here rather than inside
    // loadBootOrientation(), which has early returns that would skip it.
    kBottomNavHeight = uiPortrait() ? 63 : 42;
#elif defined(DEVICE_CROWPANEL_35) && UI_TOUCH_NAV_BAR
    kBottomNavHeight = uiPortrait() ? 56 : 28;
#endif

#if (BOARD_VEXT_ENABLE >= 0) && defined(BOARD_VEXT_RAIL_ON_AT_DISPLAY) && BOARD_VEXT_RAIL_ON_AT_DISPLAY
    // The claim() the display makes on the peripheral rail. Everything on it —
    // panel, touch controller, I2C sensors — powers up from here, so the settle
    // has to happen before lcd.init() probes the touch bus.
    digitalWrite(BOARD_VEXT_ENABLE, BOARD_VEXT_ON_LEVEL);
    delay(50);
#endif
    lcd.init();
#if HAS_RUNTIME_ORIENTATION
    // Before anything measures the panel: LVGL takes its resolution from
    // displayDev().width()/height() further down, so the rotation set here is
    // what every layout below is built against. The touch driver normalises to
    // panel-native coordinates and lets LGFX rotate them, so it follows too.
    displayDev().setRotation(uiRotationValue());
#else
    displayDev().setRotation(TFT_ROTATION_DEFAULT);
#endif
#if HAS_SYSTEM_SETTINGS
    // At the system's level from the first frame, rather than a default that
    // the loaded config then moves away from.
    displayDev().setBrightness(cfgBrightnessDuty(cfgCoerceBrightness(czeroSystemBrightnessPct())));
#else
    displayDev().setBrightness(TFT_BRIGHTNESS_DEFAULT);
#endif
    displayDev().fillScreen(TFT_BLACK);
    s_keyboard.begin();
#if defined(DEVICE_TDECK_PRO)
    // Installed the moment both ends exist: from here on, every second the
    // panel spends refreshing is also a second the keyboard controller is being
    // emptied, instead of a second it spends overflowing.
    einkSetBusyHook(keyboardServiceDuringBlockingWork);
#endif
#endif
#if defined(DEVICE_TDECK) && HAS_TOUCH && !SCREEN_WAKE_FROM_TOUCH
    probeTouchController();   // one-shot: identify the touch controller at boot
#endif
    setPagerKeyboardBacklight(true);
    s_otaWorkerUiReady = true;

    // Keep OTA worker boot as lean as possible: avoid LVGL/splash/radio/UI setup
    // so HTTPS has maximum contiguous internal heap for TLS.
    bool rtcPending = isOtaWorkerModeRequestedRtc();
    bool nvsPending = isOtaWorkerModeRequestedOnce();
    Serial.printf("[ota-worker] pre-lvgl flags (rtc=%d nvs=%d)\n",
                  rtcPending ? 1 : 0,
                  nvsPending ? 1 : 0);
    if (rtcPending || nvsPending) {
        Serial.printf("[ota-worker] pending before init (rtc=%d nvs=%d)\n",
                      rtcPending ? 1 : 0,
                      nvsPending ? 1 : 0);
        loadConfigForOtaWorker();
        if (runOtaWorkerModeIfRequested()) {
            Serial.println("[ota-worker] minimal flow finished; continuing normal boot");
        } else {
            Serial.println("[ota-worker] requested but could not start, continuing normal boot");
        }
    }

    lv_init();
#if LV_USE_LOG
    // Must follow lv_init(): registering earlier is overwritten by it.
    lv_log_register_print_cb(lvglLogToSerial);
#endif
    // v9 dropped the LV_TICK_CUSTOM compile-time hook; the tick source is
    // installed here instead. Must happen before the first lv_timer_handler().
    lv_tick_set_cb((lv_tick_get_cb_t)millis);
    emojiFontInit();   // build emoji-fallback text faces before any UI
#if HAS_STATE_MAPS
    nodesMapInitFsDriver();
#endif
    // Allocate the LVGL draw buffer here, on the normal-UI path only. The OTA
    // worker path returns/reboots before reaching this point, so the buffer is
    // never allocated during a firmware update — leaving the full contiguous
    // internal heap for the TLS handshake. Prefer internal RAM (fast to render
    // and flush); fall back to PSRAM, then a minimal buffer, rather than crash.
#if defined(DEVICE_TDECK_PRO)
    // I1 stores 8 pixels per byte and prefixes the pixel map with two 32-bit
    // palette entries. A complete frame is only 9,608 bytes, so render full-
    // screen: every e-paper commit receives one coherent native bitmap.
    constexpr size_t kI1PaletteBytes = 2u * sizeof(lv_color32_t);
    constexpr size_t kI1StrideBytes = ((size_t)LCD_W + 7u) / 8u;
    constexpr size_t kDrawBufBytes = kI1PaletteBytes + kI1StrideBytes * (size_t)LCD_H;
    size_t drawBufBytes = kDrawBufBytes;
    s_drawBufMem = (uint8_t *)heap_caps_malloc(drawBufBytes,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_drawBufMem) {
        Serial.println("[lvgl] I1 draw buffer internal alloc failed; trying PSRAM");
        s_drawBufMem = (uint8_t *)heap_caps_malloc(drawBufBytes,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_drawBufMem) {
        lv_color32_t *palette = (lv_color32_t *)s_drawBufMem;
        palette[0] = lv_color_to_32(lv_color_make(0, 0, 0), LV_OPA_COVER);
        palette[1] = lv_color_to_32(lv_color_make(255, 255, 255), LV_OPA_COVER);
        memset(s_drawBufMem + kI1PaletteBytes, 0xFF, drawBufBytes - kI1PaletteBytes);
    }
#else
    constexpr size_t kBytesPerPx = 2;   // LV_COLOR_FORMAT_RGB565
    const size_t kDrawBufPx = (size_t)kMaxHorRes * (size_t)kDrawBufLines;
    s_drawBufMem = (uint8_t *)heap_caps_malloc(kDrawBufPx * kBytesPerPx,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t drawBufPx = kDrawBufPx;
    if (!s_drawBufMem) {
        Serial.println("[lvgl] draw buffer internal alloc failed; trying PSRAM");
        s_drawBufMem = (uint8_t *)heap_caps_malloc(kDrawBufPx * kBytesPerPx,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_drawBufMem) {
        // Last resort: a small internal buffer keeps the UI alive (stripey but
        // functional) instead of a null-buffer crash.
        drawBufPx = (size_t)kMaxHorRes * 8u;
        s_drawBufMem = (uint8_t *)heap_caps_malloc(drawBufPx * kBytesPerPx,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        Serial.printf("[lvgl] FATAL-ish: fell back to %u-line draw buffer\n",
                      (unsigned)(drawBufPx / kMaxHorRes));
    }
    const size_t drawBufBytes = drawBufPx * kBytesPerPx;
#endif

    if (!s_drawBufMem) {
        Serial.println("[lvgl] FATAL: display draw buffer allocation failed");
        displayDev().fillScreen(TFT_WHITE);
        displayDev().setTextColor(TFT_BLACK, TFT_WHITE);
        displayDev().setFont(&fonts::DejaVu12);
        displayDev().drawString("Display memory error", 8, 16);
#if defined(DEVICE_TDECK_PRO)
        lcd.requestRefresh(true);
        lcd.serviceRefresh(true);
#endif
        while (true) delay(1000);
    }

    int32_t dispW = displayDev().width();
    int32_t dispH = displayDev().height();
    if (dispW <= 0 || dispH <= 0) {
        dispW = DEVICE_LCD_LANDSCAPE_W;
        dispH = DEVICE_LCD_LANDSCAPE_H;
        Serial.printf("[lvgl] WARNING: invalid lcd size, fallback to %ldx%ld\n",
                      (long)dispW, (long)dispH);
    }
#if UI_PIXEL_SCALE > 1
    // LVGL, the VNC mirror and every layout below work in scaled-down pixels;
    // only lvglFlush() and lvglTouchRead() see the panel's own (board.h).
    dispW /= UI_PIXEL_SCALE;
    dispH /= UI_PIXEL_SCALE;
#endif
#if UI_BOTTOM_SAFE_H > 0
    // Stop short of the rounded bottom corners (board.h). Nothing LVGL draws
    // reaches the strip below, so it is blacked once here and stays that way.
    dispH -= UI_BOTTOM_SAFE_H;
    displayDev().fillRect(0, dispH * UI_PIXEL_SCALE, displayDev().width(),
                          displayDev().height() - dispH * UI_PIXEL_SCALE, TFT_BLACK);
#endif
#if UI_PIXEL_SCALE > 1 || UI_BOTTOM_SAFE_H > 0
    Serial.printf("[lvgl] panel %ldx%ld: UI %ldx%ld at %dx, %d px strip below\n",
                  (long)displayDev().width(), (long)displayDev().height(),
                  (long)dispW, (long)dispH, UI_PIXEL_SCALE, UI_BOTTOM_SAFE_H);
#endif
    s_lvDisplay = lv_display_create(dispW, dispH);
#if UI_TANMATSU_LAYOUT
    if (s_lvDisplay) {
        lv_display_add_event_cb(s_lvDisplay, onRefrReadyReserveGutters,
                                LV_EVENT_REFR_READY, nullptr);
    }
#endif
    if (!s_lvDisplay) {
        Serial.println("[lvgl] FATAL: display allocation failed");
        displayDev().fillScreen(TFT_WHITE);
        displayDev().setTextColor(TFT_BLACK, TFT_WHITE);
        displayDev().setFont(&fonts::DejaVu12);
        displayDev().drawString("Display init error", 8, 16);
#if defined(DEVICE_TDECK_PRO)
        lcd.requestRefresh(true);
        lcd.serviceRefresh(true);
#endif
        while (true) delay(1000);
    }
#if UI_LARGE_PANEL_PROFILE
    // 568x1232 across 4.1 inches is about 331 DPI. LVGL's default 130-DPI
    // theme makes physically tiny padding and text on this panel, so give the
    // theme the real density and a readable base face before any widgets exist.
    lv_display_set_dpi(s_lvDisplay, 330);
    (void)lv_theme_default_init(
        s_lvDisplay,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        LV_THEME_DEFAULT_DARK,
        &lv_font_montserrat_24);
#endif
#if UI_COORD_SCALE > 1
    // The theme's own padding, scrollbars and base face, at the layout scale
    // the rest of the UI gets from hal/ui_coord_shim.h: twice LVGL's 130 DPI,
    // and the 16 px base face, which the shim turns into 32.
    lv_display_set_dpi(s_lvDisplay, LV_DPI_DEF * UI_COORD_SCALE);
    (void)lv_theme_default_init(
        s_lvDisplay,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        LV_THEME_DEFAULT_DARK,
        &lv_font_montserrat_16);
#endif
#if HAS_VNC_HOST
    vncHostInit((uint16_t)dispW, (uint16_t)dispH);
#endif
#if defined(DEVICE_TDECK_PRO)
    lv_display_set_color_format(s_lvDisplay, LV_COLOR_FORMAT_I1);
#else
    lv_display_set_color_format(s_lvDisplay, LV_COLOR_FORMAT_RGB565);
#endif
    lv_display_set_flush_cb(s_lvDisplay, lvglFlush);
#if defined(DEVICE_TDECK_PRO)
    lv_display_set_buffers(s_lvDisplay, s_drawBufMem, nullptr,
                           (uint32_t)drawBufBytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
#else
    // Single buffer, partial render mode — same stripe-at-a-time flushing the
    // v8 lv_disp_draw_buf_t gave us. Note buf_size is in BYTES in v9.
    lv_display_set_buffers(s_lvDisplay, s_drawBufMem, nullptr,
                           (uint32_t)drawBufBytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif
#if HAS_STATUS_STRIP
    // After the main display, so that one stays LVGL's default.
    statusStripInit(dispW, dispH);
#endif

#if HAS_TOUCH
    lv_indev_t *touchIndev = lv_indev_create();
    lv_indev_set_type(touchIndev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touchIndev, lvglTouchRead);
    lv_indev_set_display(touchIndev, s_lvDisplay);
#if TOUCH_POLL_ENABLED
    // Clicks only: a grip's press still selects and its drag still scrolls.
    lv_indev_add_event_cb(touchIndev, onTouchIndevEvent, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_indev_add_event_cb(touchIndev, onTouchIndevEvent, LV_EVENT_CLICKED, nullptr);
    lv_indev_add_event_cb(touchIndev, onTouchIndevEvent, LV_EVENT_LONG_PRESSED, nullptr);
#endif
#if UI_CHANNEL_LIST_DROPDOWN
    channelDrawerAttachTouch(touchIndev);
#endif
#if UI_TOUCH_ONLY_PROFILE
    // Long-press has to survive a finger that does not hold perfectly still.
    //
    // LVGL only raises LV_EVENT_LONG_PRESSED while the press has not turned
    // into a scroll — indev_proc_press() guards it with
    // `if(indev->pointer.scroll_obj == NULL && indev->long_pr_sent == 0)` — and
    // a press inside a scrollable list becomes a scroll as soon as the finger
    // moves past scroll_limit, which is 10 px by default. That is inside the
    // drift of a fingertip resting on this panel for the 400 ms the long press
    // needs, so holding a chat message to open Message Actions would register as
    // a one-pixel scroll of the chat list and the menu would never appear.
    //
    // It also explains why it looked intermittent rather than broken: with too
    // few messages to scroll, scroll_obj is never set and the hold worked.
    //
    // 22 px is still far inside a deliberate scroll drag, so the list starts
    // moving when it is meant to. Set on the touch indev only, so the VNC
    // pointer — which never drifts — keeps LVGL's default.
    lv_indev_set_scroll_limit(touchIndev, 22);
#endif
#endif
#if HAS_VNC_HOST
    lv_indev_t *vncPointerIndev = lv_indev_create();
    if (vncPointerIndev) {
        lv_indev_set_type(vncPointerIndev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(vncPointerIndev, lvglVncPointerRead);
        lv_indev_set_display(vncPointerIndev, s_lvDisplay);
    } else {
        Serial.println("[vnc] LVGL pointer allocation failed");
    }
#endif

    loadConfigFromSd();
    // The panel came up at the hardware default above, before any config
    // existed; now that it's loaded, honour the user's level.
    applyBrightness();
#if defined(DEVICE_TDECK_PRO) && !HAS_KB_BACKLIGHT_LEVEL
    // Same reasoning for the keyboard, and the same place: boot lit it
    // unconditionally before any setting was loaded, so a keyboard the user had
    // turned dark came back on at every reboot and every flash. This is the
    // first moment the answer is known.
    setPagerKeyboardBacklight(tdeckProKeyboardBacklightEnabled());
#endif
#if HAS_KB_BACKLIGHT_LEVEL
    // And the M9's keypad, which is a brightness rather than a switch. Written
    // at every boot rather than only when it changes, because it is the
    // controller that holds this value and the controller has just been power-
    // cycled along with everything else -- it comes up at its own default, not
    // at ours. That is also what makes Off survive a reboot as the user set it.
    applyKbBacklightSetting();
#endif
    drawBootSplash();   // sets the first status itself and holds, dots ticking
    // Lets the splash keep animating through web config's station-connect wait,
    // which is in another translation unit and blocks for up to ten seconds.
    // Self-disarms after boot: bootSplashTick() returns immediately once
    // bootSplashStatusEnd() has run.
    webCfgSetWaitCb(bootSplashTick);
    playSplashStartupRiff();
    recomputeChannelHashes();
    deriveNodeId();
    // What NODEINFO advertises as this build's hardware (issue #163).
    Serial.printf("[lvgl] hw_model: %s (%u)\n", MY_HW_MODEL_NAME, (unsigned)MY_HW_MODEL);
    bootSplashStatus("Mesh identity");
    mqttBridgeBegin(&s_cfg, s_myNodeId);
    mqttBridgeSetInject(mqttDownlinkInject);
    syncWifiCredsToPrefs();
    applyTimezoneFromConfig();
    bootSplashStatus("Syncing time");
    bootTimeNtpSync();
    const bool otaWorkerHandled = runOtaWorkerModeIfRequested();
    const bool skipWebAutoStartOnce = consumeSkipWebAutoStartOnce();
    if (skipWebAutoStartOnce || otaWorkerHandled) {
        Serial.println("[web] auto start skipped once for OTA low-memory retry");
    }
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    // On PSRAM boards the chat buffers live in PSRAM, so starting web config
    // here (before Channels.init()) costs nothing. The no-PSRAM Cardputer must
    // wait until those DRAM buffers exist so webCfgBegin()'s reclaim can free
    // them for Wi-Fi — it starts at the end of setup() instead. Starting here
    // would reclaim 0 bytes and then get starved when the buffers allocate.
    bootSplashStatus("Web config");
#if defined(DEVICE_TDISPLAY_P4)
    // Not yet: loop() starts it once the screen has settled (kP4WifiSettleMs).
    p4WifiCapTxPowerInit();
    if (!(skipWebAutoStartOnce || otaWorkerHandled)) s_p4WebAutoStartPending = true;
#else
    if (!(skipWebAutoStartOnce || otaWorkerHandled)) startWebConfigAuto();
#endif
#endif
#if HAS_ENV_SENSOR_TELEMETRY
    // Detect at boot, independent of whether env telemetry is switched on.
    //
    // Probing used to happen only on the announce path, which meant detection
    // required the setting to be enabled — and the web UI disabled that setting
    // when no sensor had been detected. Nothing could break the loop: you could
    // not enable it to find a sensor, because no sensor had been found. Boot is
    // also simply the right place, now that the probe is one declared route
    // rather than a sweep of guesses.
    bootSplashStatus("Sensors");
    (void)envBegin();
#endif

    bootSplashStatus("Map storage");
    bootstrapStateMapsIfMissing();
    // Before batteryInitAdc(), which takes the first sample: the trim has to be
    // in place for it, or boot's opening reading is the uncalibrated one.
    batterySetCalibrationTrim((int)s_cfg.battCalTrim);
    bootSplashStatus("Battery");
    batteryInitAdc();
    bootSplashStatus("Node database");
    Nodes.init();
    // Runs after init() so the RAM copy is already loaded: on a partition that a
    // previous build filled with node blobs, this hands the space back before
    // anything tries to save settings into it.
    Nodes.releaseNvsForSettings();
    bootSplashStatus("Messages");
    DMs.init();
    Ignored.init();
#if HAS_ADMIN_TERMINAL
    AdminPeerList.init();
#endif
    // Before init(), which loads persisted history itself. Set unconditionally:
    // it is a no-op unless the node ID actually moved. The move it replays may
    // have been recorded on an earlier boot — deriveNodeId() persists it, so a
    // change made from web config (which reboots) is still seen here.
    Channels.setSenderRemap(s_prevNodeId, s_myNodeId);
    Channels.init();
    Channels.beginPersistence();
    Channels.loadPersisted();
    syncPrimaryChannelName();
    recomputeChannelHashes();
    // Re-derive now that channel 0's name is final. Custom settings on the
    // automatic slot hash that name, and the config load back in
    // loadConfigFromSd() ran before the channel plan existed — it would have
    // hashed whatever the compiled-in default channel was called.
    applyPresetParams(s_cfg);
    bootSplashStatus("Starting radio");
    // Before the first transmission of the session, since the TX paths read this
    // rather than the compiled default.
    meshSetHopLimit(s_cfg.loraHopLimit);
    Radio.setExternalAntenna(cfgP4AntennaExternal(s_cfg));   // P4 only; recorded for init()
    s_radioReady = Radio.init(s_cfg.loraPower, s_cfg.loraRxBoostedGain);
    // Deliberately after Radio.init(). On the pager, pagerPrimeLoRaRail() arms
    // every peripheral rail on the expander including GPS_EN, so starting GPS
    // before the radio meant a boot with GPS disabled had its power-down
    // immediately undone — the module ran all session with nobody listening.
    bootSplashStatus("GPS");
    gpsSetEnabled(s_cfg.gpsEnabled);
#if defined(LORA_TEST_MQTT_ONLY) && LORA_TEST_MQTT_ONLY
    Serial.println("[radio] *** LORA_TEST_MQTT_ONLY: LoRa TX/RX DISABLED (MQTT-only test build) ***");
    Channels.addMessage(0, "", "[TEST] LoRa disabled - MQTT only", TFT_ORANGE);
#endif
    if (!s_radioReady) {
        Channels.addMessage(0, "", TR("[radio] init failed"), TFT_RED);
    } else {
        // init() already applied loraPower and loraRxBoostedGain. The rest still
        // comes from compile-time defaults, so push the loaded values if any of
        // them differ.
        //
        // Power is no longer part of this test: it used to be, and because
        // init() hardcoded 22 dBm, a device configured for 22 with everything
        // else at defaults never reconfigured — which was correct by luck. Any
        // board whose MESH_POWER was not 22 would have transmitted at full
        // power regardless of configuration.
        if (fabsf(s_cfg.loraFreq - MESH_FREQ) > 0.001f ||
            fabsf(s_cfg.loraBw   - MESH_BW)   > 0.001f ||
            s_cfg.loraSf    != MESH_SF    ||
            s_cfg.loraCr    != MESH_CR) {
            Radio.reconfigure(s_cfg.loraFreq, s_cfg.loraBw,
                              s_cfg.loraSf, s_cfg.loraCr, s_cfg.loraPower);
        }
    }

    // After the radio and config: maps the saved MeshBeacon last-sent time into
    // this session's millis() and arms the post-boot settle hold.
    meshBeaconBoot();

    bootSplashStatus("Starting interface");
    buildUi();
    // The UI owns the panel from here; a later progress call must not paint a
    // boot message over it.
    bootSplashStatusEnd();
#if HAS_HOME_DASHBOARD
    // Boot lands on the dashboard, not on chat. It is the screen that answers
    // "did this thing come up correctly" — clock, battery, conditions, and
    // whether the radio is hearing anything — which is what you want in front
    // of you the moment a device finishes starting. The Messages button is one
    // press away, and chat is built underneath either way.
    //
    // Here rather than at the end of buildUi(), so the UI rebuild (which calls
    // buildUi() too) keeps leaving you where you were instead of bouncing you
    // home every time the nav bar is toggled.
    openHomeDashboard();
#endif
    if (s_otaWorkerBootNotice[0]) {
        openCfgActionMessageModal(TR(s_otaWorkerBootNotice), TR("Update"));
        s_otaWorkerBootNotice[0] = '\0';
    }
    s_lastActivityMs = millis();
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Landscape Heltec has a wide/short layout where the onboarding modal's
    // multi-line text is awkward to read, so it is skipped there — the user can
    // still configure via web config / SD import. Portrait shows it.
    if (s_firstBoot && !uiPortrait()) {
        Serial.println("[onboarding] skipped (heltec landscape)");
        s_firstBoot = false;
    } else if (s_firstBoot) {
        Serial.println("[onboarding] first boot detected - showing setup modal");
        openOnboardingModal();
    }
#else
    if (s_firstBoot) {
        Serial.println("[onboarding] first boot detected - showing setup modal");
        openOnboardingModal();
    }
#endif
    Serial.printf("[lvgl-poc] started (%dx%d)\n", displayDev().width(), displayDev().height());
    // Repeat of the boot-time report. The early copy runs before USB CDC has
    // enumerated, so on these boards it is never seen; by here the host is
    // attached and this is the first output a serial monitor reliably catches.
    logNvsHealth();
    // Which OTA slot actually booted. `pio run -t upload` always writes app0,
    // but the bootloader follows otadata — so on a device that has taken an OTA
    // update, otadata points at app1 and every subsequent local upload lands in
    // a slot that is never executed. That failure is silent and looks exactly
    // like a code change having no effect.
    if (const esp_partition_t *running = esp_ota_get_running_partition()) {
        Serial.printf("[boot] running app: %s @0x%06X (upload writes app0 @0x010000)\n",
                      running->label, (unsigned)running->address);
    }
    Serial.printf("[cfg] boot load: %s\n", s_cfgLoadReport);
    Serial.printf("[cfg] live: wifiSsid=\"%s\" override=%d selected=\"%s\" webCfgRunning=%d\n",
                  s_cfg.wifiSsid, (int)s_wifiUsingKnownOverride,
                  s_wifiSelectedSsid, (int)webCfgRunning());
#if HAS_VNC_HOST
    if (s_vncEnabled && !applyVncEnabled(true)) {
        s_vncEnabled = false;
        persistVncEnabled();
        Serial.println("[vnc] disabled: startup prerequisites unavailable");
    }
#endif
#if HAS_BLE_KEYBOARD
    // Last thing in setup, and only when the setting says so: with it off this
    // costs nothing but remembering which keyboard was paired, which is the
    // point -- the radio's 30-40 KB of internal DRAM is not spent by a board
    // that never uses the feature.
    // Web config wins a tie at boot. The two are kept mutually exclusive at
    // every point either is switched on, so both being set can only come from a
    // config written before that rule existed, or edited by hand -- but the
    // failure if it does happen is an abort inside coex_core_enable() rather
    // than a wrong setting, so it is checked rather than assumed. Web config is
    // the one that wins because it is the recovery path: it is how a device
    // with no keyboard gets reconfigured.
#if !defined(PLATFORM_LINUX)
    if (s_cfg.bleKbdEnabled && (s_webCfgEnabled || webCfgRunning())) {
        Serial.println("[blekbd] not starting: web config owns the radio this boot");
        s_cfg.bleKbdEnabled = false;
        markConfigDirty();
    }
#endif
    bleKeyboardBegin(s_cfg.bleKbdEnabled, s_cfg.bleKbdAddr, s_cfg.bleKbdAddrType,
                     s_cfg.bleKbdName);
#endif
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // Cardputer (no PSRAM): auto-start web config only now that the node DB,
    // chat/DM buffers, radio, and UI are all allocated — this mirrors the
    // healthy manual-enable path, so webCfgBegin()'s reclaim frees the ~35 KB
    // of chat buffers and Wi-Fi keeps enough DRAM to serve pages.
    if (!(skipWebAutoStartOnce || otaWorkerHandled)) startWebConfigAuto();
#endif

    // Last, once every subsystem the hooks touch actually exists. The boot gate
    // above runs without them on purpose: nothing is initialised that early, so
    // there is nothing to flush or shut down.
    powerInstallHooks();
    if (powerMgrConsumeLowBatteryBootNotice()) {
        // The previous session ended by itself. Say so — a device that switched
        // off on its own and says nothing about it reads as a fault.
        Serial.println("[power] previous shutdown was low battery");
        snprintf(s_cfgStatus, sizeof(s_cfgStatus),
                 "%s", TR("Device switched off last time: battery empty"));
    }
}

// ── Light-sleep power management (opt-in via isPowerSaving) ───────────────────
// While the screen is asleep we duty-cycle the CPU with light-sleep naps instead
// of busy-waiting. Every input that is allowed to wake the display is registered
// as a GPIO wake source, so the timer wake only has to be fast enough for
// scheduled TX (nodeinfo is 15 min) rather than doubling as the input-latency
// ceiling.
static bool powerSaveShouldNap() {
    return s_cfg.isPowerSaving
        && s_screenAsleep                 // only after screen-off inactivity
        && !webCfgRunning()               // never while the web-config server is up
#if HAS_KB_BLINK
        // A nap is up to NAP_MAX_MS (1500 ms here) and the loop cannot service
        // the blink while it is asleep, so one landing mid-pattern would stretch
        // a 60 ms blink into a second-long glow. Only the burst holds the CPU
        // up; the gap between reminders still naps, which does mean the reminder
        // cadence drifts toward the nap length while the screen is off. That is
        // the trade — holding the CPU through the idle gap too would disable
        // power saving outright for as long as anything stayed unread.
        && !kbBlinkBusy()
#endif
#if HAS_BLE_KEYBOARD
        // A nap stops servicing the BLE controller, and a keyboard link does
        // not survive that -- it drops and has to reconnect, which is both
        // slower than the nap saved and loses whatever was typed to wake it.
        && !bleKeyboardActive()
#endif
        // The gesture that brings any of these boards back from a dark panel is
        // a two-second hold, and the loop has to be awake to time it. Only the
        // hold itself holds the CPU up; an idle dark panel naps as before.
        && s_wakeButtonsHeld == 0
        && WiFi.getMode() == WIFI_OFF     // light sleep + active Wi-Fi don't mix
        // Light sleep stops the UART ISR, so the RX FIFO (128 B) overruns after
        // ~33 ms at 38400 baud and the NMEA stream is shredded. Napping is only
        // honest when nothing is streaming — which now includes a duty-cycled
        // receiver during the (long) windows it spends parked in standby.
        && (!gpsIsEnabled() || gpsIsAsleep());
}

// Lines that must bring the CPU straight back: incoming packets, plus exactly
// the inputs that are allowed to wake the display. An input that cannot wake
// the screen must not wake the CPU either — it would burn a full wake cycle and
// then go straight back to sleep with nothing to show for it. That rules out
// the four trackball direction GPIOs on every board (they pulse on every roll,
// which deliberately does not wake the display), and on T-Deck the keyboard and
// touch lines as well, per SCREEN_WAKE_FROM_*.
struct NapWakeLine { int pin; bool wakeOnHigh; };
static const NapWakeLine kNapWakeLines[] = {
#if defined(LORA_DIO1) && (LORA_DIO1 >= 0)
    { LORA_DIO1,   true  },   // SX1262 holds DIO1 high on RX-done
#endif
#if SCREEN_WAKE_FROM_KEYBOARD && defined(KB_INT) && (KB_INT >= 0)
    { KB_INT,      (KB_INT_ACTIVE_LEVEL) == HIGH },
#endif
#if SCREEN_WAKE_FROM_TOUCH && defined(TOUCH_INT) && (TOUCH_INT >= 0)
    { TOUCH_INT,   false },   // touch panel (active low)
#endif
#if defined(TBALL_CLICK) && (TBALL_CLICK >= 0)
    { TBALL_CLICK, false },   // trackball click / pager wheel press
#endif
#if defined(USER_BUTTON_PIN) && (USER_BUTTON_PIN >= 0)
    { USER_BUTTON_PIN, (USER_BUTTON_ACTIVE_LEVEL) == HIGH },
#endif
#if defined(DISPLAY_TOGGLE_BUTTON_PIN) && (DISPLAY_TOGGLE_BUTTON_PIN >= 0)
    { DISPLAY_TOGGLE_BUTTON_PIN, (DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL) == HIGH },
#endif
#if defined(DEVICE_WIO_TRACKER_L2)
    { EXPANDER_INT, false }, // PCA9555 wake-button interrupt (active low)
#elif defined(DEVICE_TDISPLAY_P4)
    { TDISPLAY_P4_EXPANDER_INT, false }, // XL9535 radio/touch IRQ (active low)
#endif
};

// Returns false if it decided not to sleep, so the caller can fall back to a
// plain delay rather than spinning.
static bool enterLightNap() {
    // GPS is gated out in powerSaveShouldNap(), so the only limit left on nap
    // length is how much jitter scheduled TX can absorb — at a 15-minute
    // nodeinfo interval, seconds are irrelevant. Longer naps also amortise the
    // fixed entry/exit cost (PLL relock, cache restore) that made the old
    // 200 ms period mostly overhead. Per-board because one target has no
    // interrupt-capable text input; see NAP_MAX_MS in hal/board.h.
    static constexpr uint32_t kNapMs = NAP_MAX_MS;

    // The S3 only supports level-triggered GPIO wake in light sleep, so a line
    // that is already asserted makes esp_light_sleep_start() return instantly.
    // Spinning on that would cost more than staying awake; skip the nap until
    // the line releases. A held key is about to wake the screen anyway.
    for (const NapWakeLine &w : kNapWakeLines) {
        if (digitalRead(w.pin) == (w.wakeOnHigh ? HIGH : LOW)) return false;
    }

    // A queued relay sets its own deadline: nap until then at most, and not at
    // all when it is due within a few ms (entry and exit cost more).
    uint32_t napMs = kNapMs;
    uint32_t relayDueMs = 0;
    if (relayNextDueInMs(millis(), relayDueMs) && relayDueMs < napMs) {
        if (relayDueMs < 3) return false;
        napMs = relayDueMs;
    }

    for (const NapWakeLine &w : kNapWakeLines) {
        gpio_wakeup_enable((gpio_num_t)w.pin,
                           w.wakeOnHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup((uint64_t)napMs * 1000ULL);
    esp_light_sleep_start();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO && s_radioReady) {
        Radio.wakeRxCheck();              // service the packet on the next poll
    }

    // Leave the lines unarmed while awake so an ordinary keypress isn't also
    // sitting as a pending wake source.
    for (const NapWakeLine &w : kNapWakeLines) {
        gpio_wakeup_disable((gpio_num_t)w.pin);
    }
    return true;
}

// ── Low-battery hooks ────────────────────────────────────────────────────────
// power_mgr owns the decision; these are the machinery it drives. Kept here
// rather than inside that module so it stays free of the UI, the radio and the
// storage layer.

static void powerHookFlushPersistence() {
    // The clean shutdown path that has never existed. A brownout loses whatever
    // the debounced writers are still holding; this is what makes a low-battery
    // stop lose no more than a deliberate reboot would.
    flushPersistentState();
    flushConfigIfDirty();
}

static void powerHookShedLoads() {
    // Ordered by how much each one costs and how little it is missed. The LoRa
    // radio is deliberately left receiving: a node that is still listening is
    // still useful to the mesh, and silencing it is the last thing worth doing.
    //
    // Nothing here writes to config. These are emergency measures for this run
    // down, not settings the user chose — a charge and a reboot restore exactly
    // what they had.
    s_powerWifiHold = true;
    if (webCfgRunning()) {
        Serial.println("[power] stopping web config");
        webCfgEnd();
    }
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    if (gpsIsEnabled()) gpsSetEnabled(false);
    displayDev().setBrightness(cfgBrightnessDuty(BRIGHTNESS_PCT_MIN));
}

static void powerHookPrepareForOff() {
    displayDev().setBrightness(0);
#if defined(DEVICE_TDISPLAY_P4)
    displayDev().sleep();
    storageUnmount();
    (void)tdisplayP4IoPrepareForSleep();
#endif
}

static void powerHookShowMessage(const char *msg) {
    Serial.printf("[power] %s\n", msg ? msg : "");
}

static void powerHookTierChanged(PowerBatteryTier tier) {
#if defined(DEVICE_TDISPLAY_P4)
    // No "Battery low" popup on the P4: the battery reading in the status strip
    // already says so. Critical and empty still pop up -- those shed loads or
    // power off.
    if (tier == POWER_TIER_WARN) return;
#endif
    const char *label = (tier == POWER_TIER_WARN)     ? TR("Battery low")
                      : (tier == POWER_TIER_CRITICAL) ? TR("Battery critical - shedding load")
                      : (tier == POWER_TIER_CUTOFF)   ? TR("Battery empty")
                                                      : nullptr;
    if (!label) return;
    if (s_rootScreen) openCfgActionMessageModal(label);
}

static void powerInstallHooks() {
    PowerMgrHooks hooks;
    hooks.flushPersistence = powerHookFlushPersistence;
    hooks.shedLoads        = powerHookShedLoads;
    hooks.prepareForOff    = powerHookPrepareForOff;
    hooks.showMessage      = powerHookShowMessage;
    hooks.tierChanged      = powerHookTierChanged;
    powerMgrSetHooks(hooks);
}

// Release check service. Two callers share it:
//
//   * The boot check — at most once per boot, only if the user wants one, and
//     only once WiFi has been up long enough to be usable (a check fired the
//     instant the station associates tends to hit DNS before it is ready).
//     Failures are not retried: an offline device should not spend the rest of
//     its uptime probing, and the next reboot will try again.
//
//   * An explicit request from a release-channel switch (s_otaCheckRequested).
//     That one ignores both halves of the boot policy — the per-boot latch and
//     the on-boot preference — because the user has just asked by hand. The
//     hard gates below still apply to it.
//
// ── Unattended auto-update ───────────────────────────────────────────────────
// True while the device is set to install releases on its own. Read through the
// coerce so a config blob carrying an out-of-range byte reads as Off rather
// than as some period the enum does not have.
static inline bool otaAutoUpdateEnabled() {
    return cfgCoerceOtaAutoUpdate(s_cfg.otaAutoUpdatePeriod) != OTA_AUTO_UPDATE_OFF;
}

// Install-failure backoff.
//
// A check that succeeds followed by an install that does not -- a truncated
// asset, a signature mismatch, a release built without this device's slug --
// repeats forever at the configured period. At the 1 hour setting that is 24
// full firmware downloads a day, indefinitely, over what may be a metered or
// very slow link. So the tag that failed and how many times it has failed are
// remembered across reboots, beside the worker-mode flag, and a tag is dropped
// once it has burned through its attempts.
static constexpr const char *kPrefOtaFailTag   = "otaFailTag";
static constexpr const char *kPrefOtaFailCount = "otaFailN";
static constexpr uint8_t     kOtaAutoUpdateMaxFailures = 3;

static bool otaAutoUpdateTagExhausted(const char *tag) {
    if (!tag || !tag[0]) return false;
    Preferences p;
    if (!p.begin("camillia", true)) return false;
    char stored[48] = {};
    p.getString(kPrefOtaFailTag, stored, sizeof(stored));
    const uint8_t count = p.getUChar(kPrefOtaFailCount, 0);
    p.end();
    return count >= kOtaAutoUpdateMaxFailures && strcmp(stored, tag) == 0;
}

// Counts one attempt against `tag`, resetting when the tag on record is a
// different one -- a newly published release deserves its own three tries.
//
// Called BEFORE the install, not after. A failed install does return here, but
// a brownout or a watchdog reset partway through does not, and an install that
// keeps killing the device before it can record anything is precisely the
// failure this bound exists for.
static void otaAutoUpdateNoteAttempt(const char *tag) {
    if (!tag || !tag[0]) return;
    Preferences p;
    if (!p.begin("camillia", false)) return;
    char stored[48] = {};
    p.getString(kPrefOtaFailTag, stored, sizeof(stored));
    uint8_t count = p.getUChar(kPrefOtaFailCount, 0);
    if (strcmp(stored, tag) != 0) {
        p.putString(kPrefOtaFailTag, tag);
        count = 0;
    }
    if (count < 0xFF) count++;
    p.putUChar(kPrefOtaFailCount, count);
    p.end();
}

// Drops a record that belongs to the firmware now running: the attempt it was
// counting evidently worked, and leaving it would hold a stale failure against
// a tag that installed fine. Cheap and once per arm, so it stays out of the
// query above rather than making that a read that writes.
static void otaAutoUpdateForgetInstalledTag() {
    Preferences p;
    if (!p.begin("camillia", false)) return;
    char stored[48] = {};
    p.getString(kPrefOtaFailTag, stored, sizeof(stored));
    if (stored[0] && strcmp(stored, APP_VERSION) == 0) {
        p.remove(kPrefOtaFailTag);
        p.remove(kPrefOtaFailCount);
    }
    p.end();
}

// It lives in the loop rather than in the keypress handler because the check is
// a synchronous HTTP request with a 12 s timeout; running it from input would
// freeze the UI before the row saying "checking..." had been painted.
static uint32_t s_otaAutoCheckDueMs = 0;
static void serviceOtaAutoCheck(uint32_t nowMs) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // OTA is disabled on this build (see CFG_ACTION_OTA_UPDATE), so there is
    // nothing to offer.
    LV_UNUSED(nowMs);
#else
    const bool manual = s_otaCheckRequested;
    // Auto-update supersedes the boot prompt. With it on, serviceOtaAutoUpdate()
    // below installs on its own one settle window into the boot, so raising a
    // Yes/No modal here would put a dialog in front of a node nobody is
    // standing at -- and one whose question is answered by the install
    // happening anyway. The "Check for Updates on Boot" preference is simply
    // moot while auto-update is set.
    //
    // The forced one-shot from a channel switch is deliberately NOT suppressed:
    // somebody just changed the channel by hand and is waiting to see what the
    // new one offers.
    const bool bootPref = s_cfg.otaAutoCheckEnabled && !otaAutoUpdateEnabled();
    // Cleared with the boot attempt it belongs to, so a web-config channel
    // switch buys exactly one forced check rather than one per reconnect.
    const bool wantBoot = !s_otaAutoCheckDone && (bootPref || s_otaBootCheckForced);
    if (!manual && !wantBoot) return;

    // Reports a reason to the Config screen and drops the request. Only ever
    // used for an explicit one: the boot check answers to the serial log.
    auto abandonManual = [&](const char *why) {
        s_otaCheckRequested = false;
        utf8util::copyTruncate(s_otaCheckRowNote, sizeof(s_otaCheckRowNote),
                               TR(" - check unavailable"));
        snprintf(s_cfgStatus, sizeof(s_cfgStatus), "%s", why);
        Serial.printf("[ota-check] requested check dropped: %s\n", why);
        if (s_cfgModal) {
            refreshCfgModal();
            openCfgActionMessageModal(s_cfgStatus);
        }
    };

    // Nothing to offer on a layout that cannot accept the update anyway.
    if (!otaLayoutSupportsUpdate()) {
        s_otaAutoCheckDone = true;
        s_otaBootCheckForced = false;
        if (manual) abandonManual(otaLayoutBlockedReason());
        else Serial.println("[ota-check] skipped: flash layout cannot accept OTA updates");
        return;
    }
    if (!s_cfg.wifiEnabled || WiFi.status() != WL_CONNECTED) {
        s_otaAutoCheckDueMs = 0;    // restart the settle timer on reconnect
        // The request was raised with WiFi up, so getting here means it dropped
        // in the settle window. Say so rather than leaving the row spinning.
        if (manual) {
            abandonManual(s_cfg.wifiEnabled ? TR("Update check needs WiFi connected")
                                            : TR("Update check needs WiFi on"));
        }
        return;
    }
    // Don't interrupt onboarding or an open dialog — and don't burn the single
    // per-boot attempt before there is a screen to show the answer on.
    bool gated = (!s_rootScreen || s_onboardingModal || s_cfgConfirmModal || s_otaPromptModal);
#if HAS_STATE_MAPS
    if (s_legacyMapPromptModal) gated = true;
#endif
    if (gated) {
        // The boot check simply waits. An explicit one gives up eventually:
        // leaving it pending would strand the row on "checking..." for the rest
        // of the boot with nothing ever coming back to replace it.
        if (manual && (int32_t)(nowMs - s_otaCheckDeadlineMs) >= 0) {
            abandonManual(TR("Update check skipped (a dialog was open)"));
        }
        return;
    }

    if (manual) {
        // Armed by otaRequestCheckNow(), and re-armed by every further request,
        // so a burst of toggles collapses into one check against the channel
        // the user stopped on.
        if ((int32_t)(nowMs - s_otaCheckRequestedDueMs) < 0) return;
        s_otaCheckRequested = false;
        // A check the user asked for serves the boot check's purpose too, so a
        // switch made in the first few seconds of uptime does not then get a
        // second prompt when the boot check's settle timer comes due.
        s_otaAutoCheckDone = true;
        s_otaBootCheckForced = false;
    } else {
        if (s_otaAutoCheckDueMs == 0) {
            s_otaAutoCheckDueMs = nowMs + kOtaAutoCheckSettleMs;
            return;
        }
        if ((int32_t)(nowMs - s_otaAutoCheckDueMs) < 0) return;
        s_otaAutoCheckDone = true;
        s_otaBootCheckForced = false;
    }
    Serial.printf("[ota-check] checking for a newer release (%s)\n",
                  manual ? "requested" : "boot");

    // The gate is what keeps OTA networking out of normal operation; open it
    // only for the duration of this one request.
    otaSetNetworkAllowed(true);
    // Re-asserted here rather than trusted from boot: a config import can change
    // the channel without a reboot, and this is the last point before the check
    // where the stored value is authoritative.
    otaSetChannel(s_cfg.otaChannel);
    OtaCheckResult check = {};
    const bool ok = otaCheckLatestRelease(check) && check.ok;
    otaSetNetworkAllowed(false);

    if (!ok) {
        Serial.printf("[ota-check] failed: %s\n", check.error[0] ? check.error : "unknown");
        if (manual) {
            utf8util::copyTruncate(s_otaCheckRowNote, sizeof(s_otaCheckRowNote),
                                   TR(" - check failed"));
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Update check failed: %s"),
                     check.error[0] ? check.error : TR("unknown error"));
            // The row carries the short version; the popup carries the reason,
            // which is too long for a list row and worth reading once.
            if (s_cfgModal) {
                refreshCfgModal();
                openCfgActionMessageModal(s_cfgStatus);
            }
        }
        return;
    }
    if (!check.updateAvailable) {
        Serial.printf("[ota-check] up to date (%s)\n",
                      check.latestTag[0] ? check.latestTag : APP_VERSION);
        if (manual) {
            utf8util::copyTruncate(s_otaCheckRowNote, sizeof(s_otaCheckRowNote),
                                   TR(" - up to date"));
            snprintf(s_cfgStatus, sizeof(s_cfgStatus), TR("Up to date (%s)"),
                     check.latestTag[0] ? check.latestTag : APP_VERSION);
            if (s_cfgModal) {
                refreshCfgModal();
                openCfgActionMessageModal(s_cfgStatus);
            }
        }
        return;
    }

    utf8util::copyTruncate(s_otaAutoCheckTag, sizeof(s_otaAutoCheckTag), check.latestTag);
    // Arms the Firmware Update row to read "Install <tag>". Declining the prompt
    // below therefore leaves the offer standing in Config rather than losing it
    // until the next reboot.
    utf8util::copyTruncate(s_cfgOtaLatestTag, sizeof(s_cfgOtaLatestTag), check.latestTag);
    s_cfgOtaInstallArmed = true;
    if (manual) {
        snprintf(s_otaCheckRowNote, sizeof(s_otaCheckRowNote), TR(" - %s available"),
                 check.latestTag);
    }
    Serial.printf("[ota-check] update available: %s -> %s\n", APP_VERSION, s_otaAutoCheckTag);
    // Repaints the Firmware Update row behind the prompt, so declining leaves
    // "Install <tag>" on screen rather than the pre-check label -- and the
    // channel row saying which build the check turned up.
    if (s_cfgModal) refreshCfgModal();
    // Same prompt as the boot check: one path for "we found you an update",
    // whether the user asked for the check or the device did.
    openOtaUpdatePrompt();
#endif
}

// ── Unattended auto-update scheduler ─────────────────────────────────────────
// Checks on the configured period and, if a newer release exists, installs it
// with no prompt and no keypress, rebooting into it. Off unless somebody has
// turned it on; this is for a repeater on a mast or a solar node in a field,
// where the boot prompt and the Config row both wait forever for a human who is
// not coming.
//
// It adds no OTA machinery. performCfgAction(CFG_ACTION_OTA_UPDATE) already
// tears down the web server and the BLE stack for the TLS handshake, brings the
// download up, verifies the signature, retries once on low-memory TLS failure
// and restarts -- headlessly, which is what otaPromptAccept() has always relied
// on. This is a caller of that, not a change to it.
//
// In the loop for the same reason the boot check is: otaCheckLatestRelease() is
// a synchronous HTTP GET with a 12 s timeout.
//
// The timer is millis() uptime, not wall clock. A field node may never see NTP
// and timeSource can be manual or unset, so a wall-clock schedule is not
// dependable; uptime is, and it survives the light-sleep naps in enterLightNap()
// because esp_light_sleep_start() keeps system time advancing.
static uint32_t s_otaAutoUpdateDueMs = 0;
static uint8_t  s_otaAutoUpdateArmedFor = OTA_AUTO_UPDATE_OFF;

static void serviceOtaAutoUpdate(uint32_t nowMs) {
#if defined(DEVICE_CARDPUTER_LORA_HAT)
    // OTA is compiled out on this build (see CFG_ACTION_OTA_UPDATE), so there is
    // nothing to schedule.
    LV_UNUSED(nowMs);
#else
    const uint8_t period = cfgCoerceOtaAutoUpdate(s_cfg.otaAutoUpdatePeriod);
    if (period == OTA_AUTO_UPDATE_OFF) {
        s_otaAutoUpdateDueMs = 0;
        s_otaAutoUpdateArmedFor = OTA_AUTO_UPDATE_OFF;
        return;
    }

    // Arm, and re-arm whenever the setting changes under us -- web config saves
    // this without a reboot, so the running timer can be for a period nobody
    // has selected any more.
    //
    // The first cycle is one settle window after arming rather than a full
    // period, on the same reasoning the boot check settles: a node that has
    // just come back from a power cut should catch up now, not in up to 24
    // hours. It is also why the boot prompt stands down while this is on.
    if (s_otaAutoUpdateDueMs == 0 || s_otaAutoUpdateArmedFor != period) {
        s_otaAutoUpdateArmedFor = period;
        s_otaAutoUpdateDueMs = nowMs + kOtaAutoCheckSettleMs;
        otaAutoUpdateForgetInstalledTag();
        Serial.printf("[ota-auto] armed: every %s\n", cfgOtaAutoUpdateName(period));
        return;
    }
    if ((int32_t)(nowMs - s_otaAutoUpdateDueMs) < 0) return;

    // ── Gates ────────────────────────────────────────────────────────────────
    // None of these does any I/O, so a blocked cycle simply waits here and runs
    // the moment the blockage clears, rather than forfeiting its slot and
    // sleeping another full period. That is deliberately more eager than the
    // ticket's "retry next period": for a node whose battery recovers at 02:10,
    // waiting until 03:00 buys nothing, and re-testing a cached tier once per
    // loop pass costs nothing either. Nothing below re-arms the timer, so a
    // long block cannot pile up cycles.

    // Nothing to install into on a third-party partition layout.
    if (!otaLayoutSupportsUpdate()) return;
    if (!s_cfg.wifiEnabled || WiFi.status() != WL_CONNECTED) return;

    // Do not reboot out from under someone who does happen to be standing at
    // the device, and do not start before there is a screen at all.
    if (!s_rootScreen || s_onboardingModal || s_cfgConfirmModal || s_otaPromptModal) return;
#if HAS_STATE_MAPS
    if (s_legacyMapPromptModal) return;
#endif

    // The one gate the interactive paths do not apply. They get away with it
    // because a human seeing a low-battery banner declines; nothing declines
    // for an unattended node, and a flash write at the battery's knee is the
    // single operation that must not be interrupted.
    if (powerMgrShouldDeferHeavyWork()) {
        Serial.println("[ota-auto] holding: battery too low for an install");
        return;
    }

    // Rescheduled before the work, not after, so every exit below -- a failed
    // check, a failed install, a tag given up on -- lands on the full period.
    // There is no fast retry on purpose: an offline or broken node must not
    // spend its uptime re-downloading firmware, the same reasoning the boot
    // check's "failures are not retried" already carries.
    s_otaAutoUpdateDueMs = nowMs + cfgOtaAutoUpdatePeriodMs(period);

    Serial.printf("[ota-auto] checking for a newer release (every %s)\n",
                  cfgOtaAutoUpdateName(period));

    // The gate is what keeps OTA networking out of normal operation; open it
    // only for this one request. The channel is re-asserted rather than trusted
    // from boot because a config import can change it without a reboot.
    otaSetNetworkAllowed(true);
    otaSetChannel(s_cfg.otaChannel);
    OtaCheckResult check = {};
    const bool ok = otaCheckLatestRelease(check) && check.ok;
    otaSetNetworkAllowed(false);

    if (!ok) {
        Serial.printf("[ota-auto] check failed: %s\n",
                      check.error[0] ? check.error : "unknown");
        return;
    }
    if (!check.updateAvailable) {
        Serial.printf("[ota-auto] up to date (%s)\n",
                      check.latestTag[0] ? check.latestTag : APP_VERSION);
        return;
    }

    if (otaAutoUpdateTagExhausted(check.latestTag)) {
        Serial.printf("[ota-auto] not attempting %s again: %u installs of it have failed\n",
                      check.latestTag, (unsigned)kOtaAutoUpdateMaxFailures);
        // Carried to the next boot, so a node that has quietly stopped updating
        // says why the next time anyone looks at it.
        char notice[sizeof(s_otaWorkerBootNotice)] = {};
        snprintf(notice, sizeof(notice),
                 TR("Auto-update gave up on %s after %u failed installs"),
                 check.latestTag, (unsigned)kOtaAutoUpdateMaxFailures);
        setOtaWorkerBootNotice(notice);
        return;
    }

    Serial.printf("[ota-auto] installing %s -> %s, unattended\n",
                  APP_VERSION, check.latestTag);
    otaAutoUpdateNoteAttempt(check.latestTag);
    // Straight to the install: no openOtaUpdatePrompt(), and no arming of the
    // Config row's "Install <tag>" offer, because there is nobody to accept
    // either. A successful install never returns from here.
    performCfgAction(CFG_ACTION_OTA_UPDATE);

    // Only reached when the install did not happen. performCfgAction() ends by
    // popping its result modal, which is right for someone who just picked the
    // row and wrong here -- nobody is going to dismiss it, and it would sit on
    // the panel until the next keypress.
    closeCfgActionMessageModal();
    Serial.printf("[ota-auto] install did not complete: %s\n",
                  s_cfgStatus[0] ? s_cfgStatus : "unknown");
#endif
}

// True when nothing full-screen sits in front of the chat panel. Every modal
// is a child of the root screen built after the panel, so the z-order says it
// without a list of modals to keep current (the same test the touch drawer's
// edge swipe uses).
static bool chatPanelInFront() {
    if (!lvObjAlive(s_chatPanel)) return false;
    lv_obj_t *parent = lv_obj_get_parent(s_chatPanel);
    const int32_t from = lv_obj_get_index(s_chatPanel);
    if (!parent || from < 0) return false;
    const int32_t dispW = lv_disp_get_hor_res(NULL);
    const int32_t dispH = lv_disp_get_ver_res(NULL);
    const uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = (uint32_t)from + 1; i < n; i++) {
        lv_obj_t *o = lv_obj_get_child(parent, (int32_t)i);
        if (!o || o == s_channelList || o == s_channelDrawerScrim) continue;
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_get_width(o) >= dispW * 3 / 4 && lv_obj_get_height(o) >= dispH / 2) return false;
    }
    return true;
}

// How long a conversation has to be in front of the user before its new
// messages count as read and their dots go. A message arriving meanwhile starts
// the count again. Also when the channel list stops flagging the conversation.
static uint32_t freshSeenMs() {
    return 15000u;
}

// Tracks how long one conversation has been in front since its last new
// message, and reports once when that reaches freshSeenMs().
struct FreshSeenTimer {
    uint32_t sinceMs;
    uint32_t seq;
    bool     done;
    bool expired(bool sameConv, uint32_t freshSeq, uint32_t now) {
        if (!sameConv || freshSeq != seq) {
            sinceMs = now;
            seq = freshSeq;
            done = false;
            return false;
        }
        if (done || (uint32_t)(now - sinceMs) < freshSeenMs()) return false;
        done = true;
        return true;
    }
};

// New messages stay marked (with their dots) until the user has had their
// conversation in front of them for freshSeenMs(), or has left it sooner:
// another channel, another screen, or the display going to sleep. Watching what
// is in front each pass catches every way of leaving without hooking each one.
static void serviceFreshMarks() {
    static int s_prevChan = -1;
    static uint32_t s_prevPeer = 0;
    static FreshSeenTimer s_chanSeen = {};
    static FreshSeenTimer s_peerSeen = {};
    const uint32_t now = millis();
    int chan = -1;
    uint32_t peer = 0;
    if (!glanceOverlayHidesUi()) {
        peer = openDmPeerNodeId();
        if (peer == 0 && chatPanelInFront()
            && s_activeChannel >= 0 && s_activeChannel < MESH_CHANNELS) {
            chan = s_activeChannel;
        }
    }
    if (s_prevChan >= 0 && s_prevChan != chan) {
        Channels.clearFresh(s_prevChan);
        freshDotsClear(s_chatDots, (uint32_t)s_prevChan);
        // Leaving a channel that was in front means it has been read, however
        // soon. Its unread mark used to wait out freshSeenMs() like the dots,
        // so moving to another channel sooner left the one just read flagged
        // until you went back to it.
        if (s_channelNeedsAttention[s_prevChan]) {
            s_channelNeedsAttention[s_prevChan] = false;
            refreshChannelGlow(true);
        }
    }
    if (s_prevPeer != 0 && s_prevPeer != peer) {
        DMs.clearFresh(s_prevPeer);
        freshDotsClear(s_dmDots, s_prevPeer);
    }
    if (chan >= 0 && s_chanSeen.expired(chan == s_prevChan, Channels.freshSeq(chan), now)) {
        Channels.clearFresh(chan);
        freshDotsClear(s_chatDots, (uint32_t)chan);
        if (s_channelNeedsAttention[chan]) {
            s_channelNeedsAttention[chan] = false;
            refreshChannelGlow(true);
        }
    }
    if (peer != 0 && s_peerSeen.expired(peer == s_prevPeer, DMs.freshSeq(peer), now)) {
        DMs.clearFresh(peer);
        freshDotsClear(s_dmDots, peer);
    }
    s_prevChan = chan;
    s_prevPeer = peer;
}

void loop() {
    s_cfgDebugLog = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;

    uint32_t now = millis();
    // Fixed for the whole pass, so the profiler can measure the whole pass.
    // Kept separate from `now`, which is deliberately re-read further down.
    const uint32_t loopStartMs = now;
    // Cleared here rather than in loopPhaseReport(), which several early
    // returns below skip entirely. Reset at the end, those passes carried their
    // phase time into the next one and the measured total could then exceed the
    // iteration it claimed to describe -- 3875ms "in phases" of a 2257ms pass.
    loopPhaseBeginPass();
#if HAS_HAPTIC
    // First, ahead of every early return below: a Long buzz loops on the chip
    // until this stops it.
    LOOP_PHASE("haptic", tdisplayP4HapticService());
#endif
#if defined(DEVICE_TDISPLAY_P4)
    if (s_p4WebAutoStartPending && p4WifiSettled(now)) {
        s_p4WebAutoStartPending = false;
        startWebConfigAuto();
    }
#endif

    // Sample loop-iteration rate once per second as a lightweight CPU-activity
    // proxy for the hidden system-stats screen (no direct CPU-load counter
    // exists under Arduino). Counted before the early returns below so every
    // iteration is included.
    s_loopIterations++;
    if (s_loopRateAnchorMs == 0) s_loopRateAnchorMs = now;
    if ((uint32_t)(now - s_loopRateAnchorMs) >= 1000UL) {
        s_loopsPerSec = s_loopIterations - s_loopRateAnchorCount;
        s_loopRateAnchorCount = s_loopIterations;
        s_loopRateAnchorMs = now;
    }

    if (serviceTdeckTrackballSleepHold(now)) {
        delay(5);
        return;
    }
#if defined(DEVICE_WIO_TRACKER_L2)
    if (serviceWioTrackerL2WakeButton(now)) {
        delay(5);
        return;
    }
#endif
    if (pollUserButton(now)) {
        delay(5);
        return;
    }
#if defined(DEVICE_MESH_DECK)
    // Front buttons live on an I2C expander, so they need polling. Kept out of
    // the key path on purpose — see meshDeckPollButtons().
    LOOP_PHASE("md:buttons", meshDeckPollButtons());
#endif
#if HAS_NOTIFY_LED
    LOOP_PHASE("md:led", meshDeckServiceLed());
#endif
#if defined(DEVICE_TANMATSU)
    LOOP_PHASE("tm:leds", tanmatsuServiceStatusLeds());
#endif
#if HAS_KB_BLINK
    LOOP_PHASE("kbblink", serviceKbBlink());
#endif
#if HAS_KB_LIGHT_TIMEOUT
    LOOP_PHASE("kb:light", serviceKbLightTimeout());
#endif
#if HAS_RUNTIME_ORIENTATION && defined(DEVICE_TDISPLAY_P4)
    LOOP_PHASE("kb:orient", serviceKbLandscapeSwitch());
#endif
#if defined(DEVICE_TDISPLAY_P4)
    LOOP_PHASE("kb:chatrow", serviceChatBtnRowKeyboard());
#endif

#if HAS_ADMIN_TERMINAL
    // Drives the 30 s request timeout even with the window shut -- a command
    // sent and then dismissed still has to stop being outstanding.
    LOOP_PHASE("admin", s_adminSession.service(now));
    LOOP_PHASE("adminprobe", serviceAdminBootProbe(now));
    if (s_adminModal) refreshAdminTranscript();
#endif
    LOOP_PHASE("cpuscale", serviceCpuScaling());
    LOOP_PHASE("serial", serviceSerialCommands());
    LOOP_PHASE("statemaps", bootstrapStateMapsIfMissing());
#if HAS_VNC_HOST
    // A connected browser is an active operator. Wake once on connect and keep
    // the panel/UI timers alive so remote input is never swallowed by the
    // screen-off path.
    if (vncHostClientConnected()) {
#if FEATURE_LOCK_SCREEN
        (void)tryExitLockScreenFromInput(now, true);
#endif
        if (s_screenAsleep) wakeScreen();
        s_lastActivityMs = now;
    }
#endif
#if HAS_BLE_KEYBOARD
    // Auto-repeat and the pairing bookkeeping. Must run before the key pump so
    // a synthesized repeat is picked up in the same pass it was generated.
    LOOP_PHASE("ble:kbd", bleKeyboardService(now));
    LOOP_PHASE("ble:pair", syncBleKbdPairingToConfig());
    // Hand web config its low-latency radio back the moment the keyboard
    // releases modem sleep. Done on the falling edge here rather than in the
    // toggle handler because the stack comes down on the worker task, and
    // several paths take it down (the settings toggle, the OTA teardown) --
    // this catches all of them with one rule.
    {
        static bool sBleHeldModemSleep = false;
        const bool holds = bleKeyboardHoldsWifiModemSleep();
        if (sBleHeldModemSleep && !holds && webCfgRunning()) {
            Serial.println("[blekbd] released Wi-Fi modem sleep; web config latency restored");
            WiFi.setSleep(false);
        }
        sBleHeldModemSleep = holds;
    }
    if (s_cfgBleKbdModal && (uint32_t)(now - s_cfgBleKbdPaintMs) >= 400UL) {
        // Scanning and connecting both finish on the worker task with nothing
        // to push the change into LVGL, so the dialog polls.
        if (!s_cfgBleKbdAutoScanned) s_cfgBleKbdAutoScanned = bleKeyboardStartScan();
        refreshCfgBleKbdModal();
    }
    LOOP_PHASE("ble:repair", serviceBleKbdRepairPrompt());
#endif
    LOOP_PHASE("keys", pumpKeyboardInput());

#if defined(DEVICE_TDECK)
    // The microphone key, which the keyboard controller swallows rather than
    // reporting -- so it cannot arrive through pumpKeyboardInput() above with
    // every other key, and is asked for separately here.
    //
    // Only while a message is open, which is the whole of what it does and also
    // what keeps the poll off the bus the rest of the time. Second press closes
    // the tray again, matching every other way in.
    if (s_composeModal && keyboardMicPressed()) {
        if (s_emojiPickerModal) closeEmojiPicker();
        else                    openEmojiPicker(/*sendMode=*/false);
    }
#endif
    LOOP_PHASE("ui", processPendingUiRebuild());
#if defined(DEVICE_CARDPUTER_ZERO) && CZERO_DESKTOP
    LOOP_PHASE("ui", czeroFollowWindow());
#endif
    // lv_timer_handler() deliberately runs below the screen-asleep gate now —
    // there is nothing to draw with the panel in SLPIN, and its indev timer
    // would otherwise keep polling the touch controller over I2C the whole
    // time the screen is off.
    if (webCfgRunning()) {
        LOOP_PHASE("web", webCfgLoop());
#if HAS_VNC_HOST
        LOOP_PHASE("web:vnc", serviceWebVncToggle());
#endif
        LOOP_PHASE("web:chat", serviceWebChatSend());
        LOOP_PHASE("web:time", serviceWebManualTime());
        LOOP_PHASE("web:preset", serviceWebCommunityPreset());
        // Idle auto-stop. Mirrors the manual CFG_ACTION_WEBCFG disable exactly:
        // the flag is cleared and persisted too, so the device doesn't come
        // back showing "Enabled" for a server that is no longer listening.
        if (webCfgIdleExpired()) {
            Serial.println("[web] idle timeout - stopping web config");
            s_webCfgEnabled = false;
            persistWebCfgEnabled();
            webCfgEnd();
            s_wifiStaKickMs = 0;   // re-associate the station right away
            liveFeedAddLine("[web] config stopped (idle)", TFT_ORANGE);
        }
    }
    if (s_sysStatsModal) LOOP_PHASE("sysstats", refreshSysStatsModal(false));
    bool meshChanged = false;
    if (s_radioReady) {
        LOOP_PHASE("rx", meshChanged = pollMeshRx());
        LOOP_PHASE("rebroadcast", servicePendingRebroadcast(now));
    }
    LOOP_PHASE("wifi", serviceWifiStation(now));
#if HAS_STATE_MAPS
    LOOP_PHASE("map:migrate", serviceLegacyMapMigrationPrompt(now));
#endif
    LOOP_PHASE("power", powerMgrService(now));
    LOOP_PHASE("otacheck", serviceOtaAutoCheck(now));
    LOOP_PHASE("otaauto", serviceOtaAutoUpdate(now));
    LOOP_PHASE("mqtt", mqttBridgeLoop(now));
    if (s_mqttDownlinkUiDirty) { meshChanged = true; s_mqttDownlinkUiDirty = false; }
    // Mirrored every pass so a web save / YAML import / factory reset can never
    // leave the duty cycle out of step with config, the same way
    // nodeArchiveSetEnabled() below does.
    LOOP_PHASE("gps:duty", gpsSetDutyCycle(s_cfg.gpsDutyCycleEnabled, s_cfg.gpsPollIntervalS));
    LOOP_PHASE("gps", gpsLoop());
    LOOP_PHASE("timesync", serviceAutoTimeSync(now));
    LOOP_PHASE("gpstime", serviceGpsTimeSync(now));
#if HAS_NODE_LOCATE
    LOOP_PHASE("map:live", serviceNodeLocateLiveMap(now));
#endif
    // Periodically copy live GPS fix into s_cfg so it's available as a
    // "last known position" fallback when GPS is off or has lost lock.
    if (gpsIsEnabled() && gpsHasFix()) {
        // With duty cycling on, the wake/sleep cadence already *is* the sample
        // interval, so take the fix the moment it appears. Keeping the interval
        // gate as well would drop a cycle's position whenever acquisition
        // happened to be quicker than the previous cycle's — the receiver would
        // then be parked again before the gate opened.
        uint32_t intervalMs = s_cfg.gpsDutyCycleEnabled
            ? 0
            : (s_cfg.gpsPollIntervalS > 0 ? s_cfg.gpsPollIntervalS * 1000UL : 60000UL);
        if (intervalMs == 0 || (uint32_t)(now - s_lastGpsSampleMs) >= intervalMs) {
            s_cfg.latI = gpsLatI();
            s_cfg.lonI = gpsLonI();
            s_cfg.alt  = (int32_t)gpsAltM();
            s_lastGpsSampleMs = now;
        }
    }
    // Keep the Locate map responsive on keyboard boards: these services can
    // block for packet airtime/retries and make pan/zoom feel frozen.
#if HAS_NODE_LOCATE
    const bool holdAnnounceForLocate = (s_nodeLocateModal && lvObjValid(s_nodeLocateModal));
#else
    const bool holdAnnounceForLocate = false;
#endif
    // A parked radio holds them too. A scan sends exactly one broadcast, by
    // design; a position or telemetry announce that lands while it is parked
    // puts this node's traffic on a mesh it does not live on — and gets mirrored
    // to the broker under the scan's temporary channel name on the way out.
    // Nothing is lost: the announce is still due when the radio comes home.
    if (!holdAnnounceForLocate && !discoveryRadioParked()) {
        // And for a few seconds after each chat server request: this node cannot
        // hear the BATCH while it is transmitting, so a nodeinfo/position burst
        // right after a REQUEST used to lose the reply.
        if (!(s_csStarted && s_csClient.holdOwnTx(now))) {
            LOOP_PHASE("ann:nodeinfo", serviceNodeInfoAnnounce(now));
            LOOP_PHASE("ann:telemetry", serviceTelemetryAnnounce(now));
            LOOP_PHASE("ann:neighbor", serviceNeighborInfoAnnounce(now));
        }
        if (s_radioReady) LOOP_PHASE("cs", chatServerService(now));
    }
    LOOP_PHASE("fresh", serviceFreshMarks());
    LOOP_PHASE("ann:mapreport", serviceMapReport(now));
    LOOP_PHASE("autofav", serviceAutoFavorite(now));
    LOOP_PHASE("cfgflush", serviceConfigFlush(now));
    // Debounced transcript snapshots. Each writes at most one channel and one
    // conversation per pass, so a burst of traffic costs one bounded write here
    // rather than a full rewrite on every rendered line down in _pushLine().
    //
    // Held off while a key or the wheel is actively being worked: this sits
    // between pumpKeyboardInput() above and lv_timer_handler() below, so a
    // multi-KB write here lands squarely in the frame the user is waiting on —
    // and on most boards the card shares its SPI bus with the display. Their
    // own max-age deadlines override this, so interaction defers a write by a
    // few hundred milliseconds rather than starving it.
    const bool inputIdle =
        (uint32_t)(now - s_lastActivityMs) >= kPersistInputIdleMs;
    LOOP_PHASE("persist:chan", Channels.servicePersistence(now, inputIdle));
    LOOP_PHASE("persist:dm", DMs.servicePersistence(now, inputIdle));
    LOOP_PHASE("emoji:rpt", serviceEmojiPickerRepeat(now));
    LOOP_PHASE("traceroute", serviceTracerouteTimeout());
#if FEATURE_DISCOVERY
    // Runs regardless of which surface is open: a sweep outlives its modal.
    LOOP_PHASE("discovery", serviceDiscoverySweep());
#endif
    // Cheap unless due; holds itself while a preset scan has the radio parked.
    LOOP_PHASE("beacon", serviceMeshBeacon());
    // MECP emergency beacon (issue #158): cheap unless one is running and due.
    LOOP_PHASE("mecp", serviceMecpBeacon());
    // Append any nodes evicted from the full node table to the SD archive.
    // Placed before the screen-sleep return below so archiving keeps working
    // with the display off. No-op unless an eviction actually queued something.
    // Mirror the user preference into node_db each pass so it can never drift
    // from config (web save, YAML import, and factory reset all land here).
    LOOP_PHASE("archive:set", nodeArchiveSetEnabled(s_cfg.nodeArchiveEnabled));
    LOOP_PHASE("archive:flush", nodeArchiveFlush());
    // Wardrive log: same mirror-then-flush arrangement, same reason it sits
    // ahead of the screen-sleep return -- a drive is mostly spent with the
    // display off.
    LOOP_PHASE("wardrive:set", wardriveLogSetEnabled(s_cfg.wardriveLogEnabled));
    LOOP_PHASE("wardrive:flush", wardriveLogFlush());
    // Same reason, same place: web save, YAML import and factory reset all land
    // here, and every module that prints a time reads this mirror.
    LOOP_PHASE("clockfmt", liveClockSet12Hour(s_cfg.clockFormat == CLOCK_FORMAT_12H));

    // Refreshed on purpose: the screen-timeout comparisons below want a current
    // reading, and reusing the loop's start time here would age them by however
    // long this pass has already taken. It is no longer what the profiler
    // measures against -- see loopPhaseReport() at the end.
    now = millis();
#if FEATURE_LOCK_SCREEN
    // Runs before the idle check below and returns early through it: while the
    // lock screen is up the panel's fate belongs to lockScreenOffSecs, not to
    // the screen timeout that put it there.
    LOOP_PHASE("lockscreen", serviceLockScreen(now));
#endif
    if (!s_screenAsleep && s_cfg.screenOnSecs > 0
#if FEATURE_LOCK_SCREEN
        && !s_lockScreenActive
#endif
#if defined(DEVICE_TDECK_PRO) && HAS_KB_BLINK
        && !kbBlinkBusy()
#endif
    ) {
        const uint32_t idleMs    = (uint32_t)(now - s_lastActivityMs);
        const uint32_t timeoutMs = (uint32_t)s_cfg.screenOnSecs * 1000UL;
        if (idleMs > timeoutMs) {
            Serial.printf("[screen] idle %lus (timeout %us)\n",
                          (unsigned long)(idleMs / 1000UL),
                          (unsigned)s_cfg.screenOnSecs);
#if FEATURE_LOCK_SCREEN
            // The idle timeout lands on the lock screen too, not just a manual
            // screen-off gesture. A device left on a desk is the main way
            // anyone sees this screen.
            requestScreenOff("idle timeout");
#else
            sleepScreen("timeout");
#endif
        } else {
            // Step the backlight down for the tail of the timeout. Skipped
            // entirely when the timeout is shorter than the dim window — there
            // is no useful warning in dimming immediately on going idle.
            const bool wantDim = (timeoutMs > kPreSleepDimMs)
                              && (idleMs > timeoutMs - kPreSleepDimMs);
            if (wantDim != s_preSleepDimmed) {
                s_preSleepDimmed = wantDim;
                if (wantDim) {
                    const uint8_t full = cfgBrightnessDuty(s_cfg.brightness);
                    uint8_t dim = (uint8_t)((uint32_t)full * kPreSleepDimPct / 100U);
                    if (dim == 0) dim = 1;   // 0 reads as "off", not "dim"
                    displayDev().setBrightness(dim);
                } else {
                    applyBrightness();       // any activity restores full level
                }
            }
        }
    }

    if (s_screenAsleep) {
#if defined(DEVICE_TDECK_PRO)
        serviceTdeckProSleepClock();
#endif
        // The touch poll normally rides on LVGL's indev timer, which no longer
        // runs here — without this, a tap could not wake the display.
        LOOP_PHASE("touchwake", serviceTouchWakeWhileAsleep());
        if (!powerSaveShouldNap() || !enterLightNap()) delay(5);
        return;   // loop re-enters and polls input/RX/announces on wake
    }

    // The refresh helpers below all dirty-check internally, but running them
    // ~200x a second still burns a pile of comparisons per pass for a UI that
    // only needs ~30 fps. The loop itself stays fast — T-Deck's keyboard needs
    // a sub-12 ms poll cadence (see TDeckKeyboard::readKey) — so gate the
    // redraw work rather than slowing the loop.
    //
    // meshChanged is latched across skipped passes: it is recomputed every
    // iteration, so a packet landing between ticks would otherwise be dropped
    // before the views that take it as a force flag ever saw it.
    static uint32_t s_lastUiTickMs = 0;
    static bool s_meshChangedPending = false;
    if (meshChanged) s_meshChangedPending = true;

    // Forced on a wake pass so the panel gets a full repaint before the
    // backlight comes up, rather than briefly showing the pre-sleep frame the
    // controller held across SLPIN/SLPOUT.
#if FEATURE_LOCK_SCREEN
    // Everything in the refresh block below is invisible while the lock screen
    // is up — it is an opaque, full-panel overlay — and any refresher that
    // created a new root-screen child would create it *above* that overlay.
    // So the lock screen's own repaint runs instead, and lv_timer_handler()
    // further down still runs either way, which is what puts it on the panel.
    if (s_lockScreenActive) {
        serviceTdeckProSleepClock();
    } else
#endif
    if ((uint32_t)(now - s_lastUiTickMs) >= kUiRefreshTickMs || s_backlightPendingOn) {
        s_lastUiTickMs = now;
        const bool meshDirty = s_meshChangedPending;
        s_meshChangedPending = false;

        // Timed with explicit calls rather than LOOP_PHASE(): this block
        // contains preprocessor directives, and those cannot legally appear
        // inside a macro argument.
        const uint32_t uiPhaseStartMs = millis();
        refreshChannelGlow(false);
        refreshHeaderTime(false);
        refreshHeaderStatus(false);
        // Cheap no-op unless the calibration modal is open, where the reading
        // has to keep moving on its own so a trim can be judged against a meter.
        refreshCfgBattCalReadout(false);
        refreshDmAlertIndicator();
        refreshChatAlertIndicator();
        // Not forced: the content signature inside refreshChatView decides whether a
        // rebuild is actually needed, so mesh packets that don't change the visible
        // chat no longer trigger a full (bubble) teardown/rebuild every time.
        refreshChatView(false);
        refreshLiveView(meshDirty);
        refreshChUtilChart(meshDirty);
        refreshSnrRssiChart(meshDirty);
#if HAS_HOME_DASHBOARD
        // Cheap no-op when the dashboard is closed, and when it is open the
        // charts only redraw on a sequence change — so meshDirty is passed for
        // symmetry with the modals above rather than because it is needed.
        refreshHomeDashboard(meshDirty);
#endif
#if FEATURE_DISCOVERY
        // No meshDirty force: its own signature covers the packets that change
        // this screen, and meshDirty misses most of them anyway.
        refreshDiscoveryModal();
#endif
        // Same reasoning, plus the ages on the cards, which move on their own.
        refreshBeaconsModal();
#if FEATURE_MQTT_MONITOR
        // Cheap no-op unless the monitor is open; it throttles its own retally.
        refreshMqttMonitorModal();
#endif
#if UI_TOUCH_ONLY_PROFILE
        // Evaluated first, not folded into the argument: || would short-circuit
        // past it whenever meshDirty is already true and leave the flag set.
        const bool dmDeferred = dmTakeDeferredRefresh();
        refreshDmModal(meshDirty || dmDeferred);
#else
        refreshDmModal(meshDirty);
#endif
        loopPhaseNote("ui:refresh", millis() - uiPhaseStartMs);
    }

    // Runs after the refresh block so the flush carries this pass's updates,
    // and serviceBacklightWake() below only ever lights a panel that has
    // already been repainted.
#if HAS_VNC_HOST
    if (vncHostClientConnected() && vncHostTakeFullRepaintRequest()) {
        lv_obj_invalidate(lv_screen_active());
    }
#endif
    LOOP_PHASE("lvgl", lv_timer_handler());
#if defined(DEVICE_TDECK_PRO)
    if (einkRefreshDueNow()) LOOP_PHASE("eink", lcd.serviceRefresh());
#endif
    LOOP_PHASE("backlight", serviceBacklightWake());

    // Whole-iteration cost, and the periodic report. Placed before the delay so
    // the fixed 5 ms pace is not counted as work.
    //
    // Measured from loopStartMs rather than `now`. `now` is re-read part way
    // down this function for the screen-timeout checks, so measuring against it
    // timed only the part of the pass after that point -- roughly the last
    // third. Everything before it (keys, rx, gps, and every tx:/ann: phase) was
    // invisible to this figure, which is how one window could report
    // "worst 11ms" while reporting gps=317ms on the same line, and why this
    // said 131ms during a pass the web server separately measured as busy for
    // 1673 ms. The per-phase numbers were always right; only the total was not.
    loopPhaseReport(millis(), millis() - loopStartMs);

    delay(5);
}

#endif  // UI_LVGL_POC
