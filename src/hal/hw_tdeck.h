#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_tdeck.h — Hardware pin definitions for the LilyGO T-Deck
//
// The T-Deck is a self-contained ESP32-S3 handheld with:
//   • 2.8" ST7789 320×240 IPS TFT
//   • LILYGO-branded SX1262 LoRa radio
//   • I2C QWERTY keyboard (address 0x55) + GT911 capacitive touch
//   • Optical trackball (four GPIO directional + one click)
//   • microSD on the shared LoRa SPI bus
//   • Internal I2S speaker amplifier (MAX98357A / NS4168)
//   • L76K GPS on UART1
//
// All SPI peripherals (LoRa, TFT, SD) share SPI2_HOST / the same MISO/MOSI/SCK
// but use separate chip-select lines.
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON            10   // Hold HIGH at boot to keep power rail up
#define BOARD_BUZZER             -1   // No passive buzzer; uses I2S speaker instead
#define BOARD_VEXT_ENABLE        -1   // No switched VEXT rail
#define BOARD_VEXT_ON_LEVEL      HIGH

// ── I2S speaker output (NS4168 / MAX98357A) ──────────────────────────────────
// Audio is output-only; these feed into an on-board I2S class-D amplifier.
#define TDECK_DAC_I2S_WS          5   // Word select (LR clock)
#define TDECK_DAC_I2S_BCK         7   // Bit clock
#define TDECK_DAC_I2S_DOUT        6   // Serial data out to amplifier

// ── TFT display — ST7789 320×240 (landscape) ────────────────────────────────
#define TFT_SPI_HOST         SPI2_HOST
#define TFT_SPI_SCK              40
#define TFT_SPI_MISO             38
#define TFT_SPI_MOSI             41
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    40000000
#define TFT_SPI_READ_HZ      1000000
#define TFT_CS                   12
#define TFT_DC                   11
#define TFT_BL                   42   // Backlight PWM
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ           12000
#define TFT_BL_PWM_CH             0
#define TFT_BRIGHTNESS_DEFAULT  128
#define TFT_RST                  -1
#define TFT_PANEL_WIDTH         240
#define TFT_PANEL_HEIGHT        320
#define TFT_INVERT             true
#define TFT_RGB_ORDER          false

// ── LoRa — SX1262 on shared SPI2 bus ────────────────────────────────────────
#define LORA_SPI_SCK             40   // Shared with TFT / SD
#define LORA_SPI_MISO            38
#define LORA_SPI_MOSI            41
#define LORA_CS                   9
#define LORA_DIO1                45
#define LORA_RST                 17
#define LORA_BUSY                13
#define LORA_FEM_POWER_PIN       -1   // No external front-end module
#define LORA_FEM_ENABLE_PIN      -1
#define LORA_FEM_TX_MODE_PIN     -1

// ── microSD — same SPI2 bus, own CS ─────────────────────────────────────────
#define SD_CS                    39
#define HAS_SD_CARD               1

// ── I2C keyboard — Blackberry-style QWERTY at address 0x55 ──────────────────
#define HAS_KEYBOARD              1
#define KB_SDA                   18
#define KB_SCL                    8
#define KB_ADDR               0x55
#define KB_INT                   46   // Active-low interrupt when key is ready

// ── Capacitive touch — GT911 on same I2C bus as keyboard ────────────────────
#define HAS_TOUCH                 1
#define TOUCH_SDA                18
#define TOUCH_SCL                 8
#define TOUCH_ADDR            0x5D
#define TOUCH_INT                16
#define TOUCH_RST                -1
#define TOUCH_I2C_PORT            0   // Wire0
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED        1
#endif

// ── Trackball — four directional GPIOs + center click ───────────────────────
// Physical motion is decoded via ISR in keyboard.cpp.
// Empirically confirmed mapping (right=DOWN, left=LEFT, up=RIGHT, down=UP).
#define HAS_TRACKBALL             1
#define TBALL_UP                  3
#define TBALL_DOWN                2
#define TBALL_LEFT                1
#define TBALL_RIGHT              15
#define TBALL_CLICK               0

// ── User / boot button ───────────────────────────────────────────────────────
#define USER_BUTTON_PIN          -1   // T-Deck has no dedicated user button in this profile
#define USER_BUTTON_ACTIVE_LEVEL LOW

// ── GPS — L76K on UART1 ──────────────────────────────────────────────────────
#define HAS_GPS                   1
#define GPS_RX                   44
#define GPS_TX                   43
#define GPS_BAUD              38400

// ── Battery ADC ──────────────────────────────────────────────────────────────
// Direct resistor divider from VBAT to GPIO4. No enable switching needed.
#define BATT_ADC_PIN              4
#define BATT_DIV               2.0f   // 1:2 voltage divider
#define BATT_SENSE_ENABLE_PIN    -1
#define BATT_SENSE_ENABLE_LEVEL  LOW

// ── Radio TCXO voltage ───────────────────────────────────────────────────────
#define MESH_TCXO_V            1.6f

// ── Memory / display geometry ────────────────────────────────────────────────
#define HAS_PSRAM                 1
#define DEVICE_LCD_PORTRAIT_W   240
#define DEVICE_LCD_PORTRAIT_H   320
#define DEVICE_LCD_LANDSCAPE_W  320
#define DEVICE_LCD_LANDSCAPE_H  240
