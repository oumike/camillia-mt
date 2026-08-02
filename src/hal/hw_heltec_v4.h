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
//   • Switched 3.3V VEXT rail (active LOW enable) for display + peripherals
//   • Battery ADC with a switchable sense-enable pin to avoid parasitic drain
//   • BME280 environment sensor (temp/humidity/pressure) via I2C
//   • No keyboard or trackball — uses touch + buttons for input
//
// The FEM (PA/LNA) is controlled by three dedicated GPIOs that must be driven
// before RadioLib will yield useful TX/RX.
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON              7   // Hold HIGH to keep module powered
#define BOARD_BUZZER               6   // Passive buzzer (GPIO PWM tone)
#define BOARD_VEXT_ENABLE         36   // Drive LOW to enable switched VEXT rail
#define BOARD_VEXT_ON_LEVEL       LOW

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
#define SD_CS                     -1
#define HAS_SD_CARD                0

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
