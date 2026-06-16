#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_cardputer.h — Hardware pin definitions for the M5Stack Cardputer
//                       with LoRa 1262 Cap accessory
//
// The Cardputer is a credit-card-sized ESP32-S3 computer with:
//   • 1.14" ST7789V2 240×135 TFT (portrait native, landscape in use)
//   • Full QWERTY keyboard managed by the M5Cardputer library (not I2C)
//   • M5Stack LoRa-1262 Cap module on SPI3 (includes its own IO expander)
//   • microSD on the same SPI bus as LoRa
//   • M5Stack speaker driver for audio tones
//   • L76K GPS on UART1 (115 200 baud — faster than other boards)
//   • No touch screen, no trackball
//
// The SPI3_HOST (VSPI) bus is used for both TFT and LoRa/SD.
// The LoRa cap requires the PI4IOE5V6408 IO expander to be armed before the
// SX1262 will respond — this is handled in mesh_radio.cpp via M5Unified.
//
// No PSRAM; RAM-conscious code paths should be taken where available.
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON             -1
#define BOARD_BUZZER              -1   // Audio via M5Stack Speaker, not GPIO buzzer
#define BOARD_VEXT_ENABLE         -1
#define BOARD_VEXT_ON_LEVEL       HIGH

// ── TFT display — ST7789V2 240×135 ──────────────────────────────────────────
// Offset_X/Y compensate for the panel's internal physical crop.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               36
#define TFT_SPI_MISO              -1   // Write-only panel (3-wire SPI)
#define TFT_SPI_MOSI              35
#define TFT_SPI_3WIRE           true
#define TFT_SPI_WRITE_HZ     40000000
#define TFT_SPI_READ_HZ       1000000
#define TFT_CS                    37
#define TFT_DC                    34
#define TFT_BL                    38
#define TFT_BL_INVERT           false
#define TFT_BL_FREQ              256
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   180
#define TFT_RST                   33
#define TFT_PANEL_WIDTH          135
#define TFT_PANEL_HEIGHT         240
#define TFT_PANEL_OFFSET_X        53   // Hardware pixel offset — do not remove
#define TFT_PANEL_OFFSET_Y        40
#define TFT_INVERT              true
#define TFT_RGB_ORDER           false

// ── LoRa — SX1262 via M5Stack LoRa-1262 Cap ─────────────────────────────────
// NOTE: The PI4IOE5V6408 IO expander on the cap must be initialized via
//       M5Unified before the SX1262 will assert BUSY correctly.
#define LORA_SPI_SCK              40
#define LORA_SPI_MISO             39
#define LORA_SPI_MOSI             14
#define LORA_CS                    5
#define LORA_DIO1                  4
#define LORA_RST                   3
#define LORA_BUSY                  6
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1

// ── microSD — same SPI bus as LoRa cap ──────────────────────────────────────
#define SD_CS                     12
#define HAS_SD_CARD                1

// ── Keyboard — full QWERTY via M5Cardputer library (not raw I2C) ─────────────
// keyboard.cpp wraps M5Cardputer.Keyboard and normalizes its events to the
// same KEY_* constants used across all builds.
#define HAS_KEYBOARD               1
#define KB_SDA                    -1   // Managed by M5Cardputer library
#define KB_SCL                    -1
#define KB_ADDR                0x00
#define KB_INT                    -1

// ── No touch on this device ──────────────────────────────────────────────────
#define HAS_TOUCH                  0
#define TOUCH_SDA                 -1
#define TOUCH_SCL                 -1
#define TOUCH_ADDR              0x00
#define TOUCH_INT                 -1
#define TOUCH_RST                 -1
#define TOUCH_I2C_PORT             0
#define TOUCH_POLL_ENABLED         0

// ── No trackball on this device ──────────────────────────────────────────────
#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

// ── User / boot button ───────────────────────────────────────────────────────
#define USER_BUTTON_PIN            0
#define USER_BUTTON_ACTIVE_LEVEL   LOW

// ── GPS — L76K on UART1 at 115 200 baud ─────────────────────────────────────
// Cardputer GPS module communicates faster than the other boards.
#define HAS_GPS                    1
#define GPS_RX                    15
#define GPS_TX                    13
#define GPS_BAUD              115200

// ── Battery ADC ──────────────────────────────────────────────────────────────
// Cardputer reads about 20% low at the default 1.5× divider; empirically
// corrected to 1.8× so percentage/voltage align with the observed pack state.
#define BATT_ADC_PIN              10
#define BATT_DIV                1.8f   // Empirically calibrated (see note above)
#define BATT_SENSE_ENABLE_PIN     -1
#define BATT_SENSE_ENABLE_LEVEL   LOW

// ── Radio TCXO voltage ───────────────────────────────────────────────────────
#define MESH_TCXO_V             3.0f

// ── Memory / display geometry ────────────────────────────────────────────────
#define HAS_PSRAM                  0   // No PSRAM — keep heap allocations minimal
#define DEVICE_LCD_PORTRAIT_W    135
#define DEVICE_LCD_PORTRAIT_H    240
#define DEVICE_LCD_LANDSCAPE_W   240
#define DEVICE_LCD_LANDSCAPE_H   135
