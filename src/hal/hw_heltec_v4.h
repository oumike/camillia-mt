#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_heltec_v4.h — Hardware pin definitions for the Heltec WiFi LoRa 32 V3
//                       with T-Deck-style TFT expansion board
//
// This profile targets a Heltec ESP32-S3 LoRa module mounted on a custom
// expansion carrier with:
//   • 2.8" ST7789 320×240 TFT (portrait native, landscape in use)
//   • CHSC6X capacitive touch (I2C on Wire1)
//   • SX1262 LoRa with external FEM (front-end module) controlled via GPIOs
//   • L76K GPS on UART1
//   • Heltec "USER" button (GPIO0, active LOW) and side display-toggle button
//     (GPIO35, active LOW)
//   • Switched 3.3V VEXT rail (active LOW enable, GPIO36) for display + peripherals
//   • Battery ADC with a switchable sense-enable pin to avoid parasitic drain
//   • Environment sensors on I2C (ENV_SDA/ENV_SCL below): a BME280
//     (temp/humidity/pressure) and an SHT4x/GXHT30-class temp/humidity part.
//     Only the BME280 has a driver here; the second is not read yet.
//   • No keyboard or trackball — uses touch + buttons for input
//
// The FEM (PA/LNA) is controlled by three dedicated GPIOs that must be driven
// before RadioLib will yield useful TX/RX.
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON              7   // Hold HIGH to keep module powered
#define BOARD_BUZZER               6   // Passive buzzer (GPIO PWM tone)
// Switched peripheral rail. Drive LOW to enable. Do not change this.
//
// Verified twice on hardware, the second time with no other change in the build:
// driving GPIO36 HIGH makes the touch controller's I2C fail continuously —
//
//     [E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error 263
//
// once per second, each timeout blocking lv_timer_handler() for ~1 s (the loop
// probe measured lvgl at 1082 ms x14 in one 15 s window). LOW restores it.
//
// wadamesh's non-R8 Heltec env does set PIN_VEXT_EN=36 / PIN_VEXT_EN_ACTIVE=HIGH
// on the same hardware, which made this look like an unresolved contradiction
// for a long time. It was a red herring: LOW powers this rail correctly, and the
// environment sensors were never unpowered.
//
// The sensors were missing because ENV_SDA/ENV_SCL were swapped (see below), not
// because of this pin. With 4/3 the BME280 answers in ~119 ms with the rail
// driven LOW exactly as it is here. Do not revisit GPIO36 to chase a sensor
// problem; on this unit HIGH only ever broke touch.
#define BOARD_VEXT_ENABLE         36
#define BOARD_VEXT_ON_LEVEL       LOW
// Deliberately NOT using BOARD_VEXT_RAIL_ON_AT_DISPLAY here. Parking the rail
// off and raising it at lcd.init() — matching wadamesh's claim()-then-init
// order — stopped this board booting at all: no serial, no network, no display.
// Every attempt to make GPIO36 behave the way that port describes has made this
// unit worse. LOW from the top of setup() is the only configuration observed to
// boot and keep touch working.

// ── TFT display — ST7789 320×240 with custom init sequence ──────────────────
// Uses a subclassed Panel_ST7789 to inject a GAMMA_CURVE init command; the
// standard Lovyan init sequence produces a washed-out image on this panel.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               17
#define TFT_SPI_MISO              -1   // Write-only (3-wire SPI)
#define TFT_SPI_MOSI              33
#define TFT_SPI_3WIRE           true
#define TFT_SPI_WRITE_HZ     40000000
#define TFT_SPI_READ_HZ       4000000
#define TFT_CS                    15
#define TFT_DC                    16
#define TFT_BL                    21
#define TFT_BL_INVERT           false
#define TFT_BL_FREQ            44100
#define TFT_BL_PWM_CH              7
// Backlight is typically the largest draw while the screen is on, and this was
// the highest default of any board by a wide margin (T-Deck 128, pager 130).
// Lowered to trade a little headroom for battery; the user can raise it in
// settings, and this also sets the default brightness percentage via
// cfgBrightnessDuty() in config.h.
#define TFT_BRIGHTNESS_DEFAULT   160
#define TFT_RST                   18
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_INVERT              true
#define TFT_RGB_ORDER           false

// ── LoRa — SX1262 on SPI1 (separate from display) ───────────────────────────
// The FEM (front-end module) must be powered and set to TX/RX mode via GPIOs.
#define LORA_SPI_SCK               9
#define LORA_SPI_MISO             11
#define LORA_SPI_MOSI             10
#define LORA_CS                    8
#define LORA_DIO1                 14
#define LORA_RST                  12
#define LORA_BUSY                 13
#define LORA_FEM_POWER_PIN         7   // FEM power enable
#define LORA_FEM_ENABLE_PIN        2   // FEM RF switch enable
#define LORA_FEM_TX_MODE_PIN      46   // HIGH = TX mode, LOW = RX mode

// ── No SD card slot on this board ────────────────────────────────────────────
// SD_CS still has to exist because sdBegin() keys off (SD_CS < 0) rather than
// HAS_SD_CARD; -1 keeps the card path disabled.
#define SD_CS                     -1
#define HAS_SD_CARD                0

// ...but the board is not storage-less. Like the Mesh Deck, it keeps the files
// a card would hold in a LittleFS partition instead — see partitions_16mb_fs.csv,
// which maps the flash left over after the two OTA app slots. sdBegin() routes
// straight to storageBegin() on a board with this defined, so chat and DM
// transcripts, config export/import and map tiles all come up without any of
// those call sites knowing which backend answered.
//
// Test HAS_FILE_STORAGE (storage.h), not HAS_SD_CARD, when the question is
// "can a file be saved"; reach the filesystem through storageFs().
#define HAS_INTERNAL_FS            1
#define INTERNAL_FS_PARTITION  "littlefs"

// ── No keyboard; touch + buttons handle all input ────────────────────────────
#define HAS_KEYBOARD               0
#define KB_SDA                    -1
#define KB_SCL                    -1
#define KB_ADDR                0x00
#define KB_INT                    -1

// ── Capacitive touch — CHSC6X on Wire1 ──────────────────────────────────────
// Uses a custom lgfx::ITouch wrapper (Touch_Heltec_CHSC6X in hal/display.h)
// because the CHSC6X driver is not part of standard LovyanGFX.
#define HAS_TOUCH                  1
#define TOUCH_SDA                 47
#define TOUCH_SCL                 48
#define TOUCH_ADDR              0x2E
#define TOUCH_INT                 -1   // No interrupt routed; uses polling
#define TOUCH_RST                 44
#define TOUCH_I2C_PORT             1   // Wire1

// ── Environment sensor (BME280) ──────────────────────────────────────────────
// Not declared, which is why env_sensor.cpp falls back to probing a short list
// of candidate routes instead of going straight to the right one.
//
// Declaring these turns that sweep off entirely: the probe uses this route and
// nothing else, which is both faster and safer — the sweep is what used to
// reconfigure TFT_SPI_SCK (17) and TFT_RST (18) as an I2C bus while the display
// was running. That route is gone now, but the shotgun is still a bring-up tool
// standing in for wiring nobody has written down. See issue #53.
//
// Taken from the wadamesh MeshCore port's heltec_v4 variant, which reads both
// on-board sensors from this bus:
//
//   variants/heltec_v4/pins_arduino.h:
//     static const uint8_t SDA = 3;
//     static const uint8_t SCL = 4;
//
// Both pins are otherwise unassigned in this profile, and the sensors sit on the
// switched VEXT rail (BOARD_VEXT_ENABLE above), which setup() enables before any
// probe runs.
//
// Declaring them means the probe targets this route alone instead of guessing —
// see kEnvRoutes in env_sensor.cpp. Note the board carries more than the BME280
// this header used to claim: wadamesh also reports an SHT4x/GXHT30-class
// temperature+humidity part, which this firmware has no driver for yet.
// SDA=4 / SCL=3, NOT 3/4. variants/heltec_v4/pins_arduino.h declares the
// generic board defaults SDA=3/SCL=4, but the TFT env overrides them with
// -D PIN_BOARD_SDA=4 -D PIN_BOARD_SCL=3 — the two lines are swapped relative
// to the board JSON. Probing 3/4 scans an empty bus and finds nothing, which
// is exactly what this profile did before.
#define ENV_SDA                    4
#define ENV_SCL                    3
#define ENV_I2C_PORT               0   // Wire
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED         1
#endif

// ── No trackball on this device ──────────────────────────────────────────────
#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

// ── User / boot button — also used for single-press actions ─────────────────
#define USER_BUTTON_PIN            0
#define USER_BUTTON_ACTIVE_LEVEL   LOW

// ── Side button — wakes / sleeps the display ─────────────────────────────────
#define DISPLAY_TOGGLE_BUTTON_PIN          35
#define DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL LOW

// ── GPS — L76K on UART1 ──────────────────────────────────────────────────────
#define HAS_GPS                    1
#define GPS_RX                    38
#define GPS_TX                    39
#define GPS_BAUD               38400

// ── Battery ADC with switched sense line ─────────────────────────────────────
// BATT_SENSE_ENABLE_PIN is driven to avoid constant ADC loading on the divider.
// battery_util.cpp auto-detects the correct polarity at first read.
#define BATT_ADC_PIN               1
#define BATT_DIV                5.1205f   // Precision-calibrated divider ratio
#define BATT_SENSE_ENABLE_PIN     37
#define BATT_SENSE_ENABLE_LEVEL   LOW

// ── Radio TCXO voltage ───────────────────────────────────────────────────────
#define MESH_TCXO_V             1.8f

// ── Memory / display geometry ────────────────────────────────────────────────
#define HAS_PSRAM                  1
#define DEVICE_LCD_PORTRAIT_W    240
#define DEVICE_LCD_PORTRAIT_H    320
#define DEVICE_LCD_LANDSCAPE_W   320
#define DEVICE_LCD_LANDSCAPE_H   240
