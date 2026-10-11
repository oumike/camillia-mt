#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_crowpanel_50.h — Elecrow CrowPanel Advance 5.0" HMI (ESP32-S3)
//
// ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB OPI PSRAM) with:
//   • 5.0" 800×480 IPS panel (ST7262) on a 16-bit RGB parallel bus
//   • GT911 capacitive touch on I2C (SDA 15 / SCL 16)
//   • PCA9557 I/O expander at 0x18: backlight, touch reset, amplifier
//   • SX1262 LoRa module in the J8/J9 wireless-module slot
//   • No keyboard, trackball or free user button (GPIO0 is LoRa NSS)
//
// Pins follow Meshtastic's support and Elecrow's V1.0 schematic and code:
//   meshtastic/firmware variants/esp32s3/elecrow_panel/variant.h (CROW_SELECT 2)
//   meshtastic/firmware variants/esp32s3/elecrow_panel/platformio.ini
//     [env:elecrow-adv1-43-50-70-tft]
//   meshtastic/device-ui include/graphics/LGFX/LGFX_ELECROW70.h
//   Elecrow-RD/CrowPanel-Advance-HMI-ESP32-AI-Display
//     5.0/schematic/ESP32 Display 5.0 inch V1.0.pdf
//     5.0/factory_code/LovyanGFX_Driver.h, factory_code.ino
//
// ── The function switch ──────────────────────────────────────────────────────
// GPIO 4/5/6 and 19/20/2 each reach three places -- the SD card, the wireless
// module slot, and the speaker amplifier / microphone -- through two CH486F
// analog switches set by the two-position DIP switch on the back of the board
// ("Function switching" on the schematic). Only one of them is connected at a
// time. Set it to the wireless module for this firmware: in the other positions
// the radio is simply not there, and the boot log says the SX1262 was not found.
//
// That is why this board has no SD card here. The card shares the radio's SPI
// pins through the same switch, so with the radio connected the card is not;
// files live in the LittleFS partition instead, as on the Heltec V4.
//
// Expansion slot, viewed from above (4.3/5.0/7.0"):
//   DIO1/IO20 o   o IO19/NRESET
//     SCK/IO5 o   o IO16/NC
//    MISO/IO4 o   o IO15/NC
//    MOSI/IO6 o   o NC/DIO2
//         3V3 o   o IO2/BUSY
//         GND o   o IO0/NSS
//       5V/NC o   o NC/DIO3
//            J9   J8
//
// GPIO19/20 are the S3's native USB pins; the radio has them, so the console is
// UART0 through the board's CH340K, as on the 3.5".
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON             -1
// A real passive buzzer on GPIO8 ("Passive buzzer", IO8_BEEP) -- unlike the
// 3.5", where GPIO8 is the amplifier's MCLK.
#define BOARD_BUZZER               8
#define BOARD_VEXT_ENABLE         -1
#define BOARD_VEXT_ON_LEVEL     HIGH

// ── PCA9557 I/O expander ─────────────────────────────────────────────────────
// Register-compatible with the TCA9534 Meshtastic drives it as. IO0 is
// open-drain on this part, which is why nothing uses it.
#define CROWPANEL_IOEX_ADDR     0x18
#define CROWPANEL_IOEX_BL          1   // backlight driver enable (MT9201), HIGH = on
#define CROWPANEL_IOEX_TP_RST      2   // GT911 reset, LOW = in reset
#define CROWPANEL_IOEX_AMP_MUTE    3   // speaker amplifier, held as Elecrow leaves it
#define CROWPANEL_IOEX_AMP_SHUT    4

// ── TFT display — 800×480 RGB565 parallel ────────────────────────────────────
// Driven from a PSRAM framebuffer by the S3's LCD_CAM peripheral; see
// crowpanel_50_display.h for the bus pins and timings. There is no SPI panel,
// so the SPI and chip-select macros the shared code tests are -1.
#define TFT_SPI_SCK               -1
#define TFT_SPI_WRITE_HZ           0
#define TFT_CS                    -1
#define TFT_DC                    -1
#define TFT_RST                   -1
#define TFT_BL                    -1   // on the expander, not a GPIO
#define TFT_BRIGHTNESS_DEFAULT   160
#define TFT_PANEL_WIDTH          800
#define TFT_PANEL_HEIGHT         480
// Pixel clock. Elecrow's demo runs 21 MHz, Meshtastic 14 MHz. The lower one
// leaves the PSRAM bandwidth WiFi and the radio need while the panel scans out
// of the same memory, which is what makes an RGB panel drift or tear.
#define CROWPANEL_PCLK_HZ   14000000

// ── LoRa — SX1262 in the wireless-module slot ───────────────────────────────
#define LORA_SPI_SCK               5
#define LORA_SPI_MISO              4
#define LORA_SPI_MOSI              6
#define LORA_CS                    0
#define LORA_DIO1                 20
#define LORA_RST                  19
#define LORA_BUSY                  2
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
// No power gate: the DIP switch above selects the slot, not a GPIO (the 3.5"
// uses IO45, which is a display data line here).
#define LORA_POWER_ENABLE_PIN     -1
#define LORA_POWER_ENABLE_LEVEL  LOW

// ── Storage — internal LittleFS only ─────────────────────────────────────────
// SD_CS has to exist because sdBegin() keys off (SD_CS < 0); -1 keeps the card
// path off. See the function switch above for why there is no card.
#define SD_CS                     -1
#define HAS_SD_CARD                0
#define HAS_INTERNAL_FS            1
#define INTERNAL_FS_PARTITION  "littlefs"

// ── No keyboard ──────────────────────────────────────────────────────────────
#define HAS_KEYBOARD               0
#define KB_SDA                    -1
#define KB_SCL                    -1
#define KB_ADDR                 0x00
#define KB_INT                    -1

// ── Capacitive touch — GT911 ─────────────────────────────────────────────────
// Reset is on the expander, so LovyanGFX gets no reset pin; the init sequence
// in crowpanel_50_display.h pulses it, holding INT (GPIO1) low so the GT911
// comes up at 0x5D. Polled, as Elecrow and Meshtastic both run it.
#define HAS_TOUCH                  1
#define TOUCH_SDA                 15
#define TOUCH_SCL                 16
#define TOUCH_ADDR              0x5D
#define TOUCH_INT                  1
#define TOUCH_RST                 -1
#define TOUCH_I2C_PORT             0
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED         1
#endif

// ── No trackball ─────────────────────────────────────────────────────────────
#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

// ── No free user button: BOOT (GPIO0) doubles as LoRa NSS ───────────────────
#define USER_BUTTON_PIN           -1
#define USER_BUTTON_ACTIVE_LEVEL LOW

// ── No GPS ───────────────────────────────────────────────────────────────────
// Meshtastic puts GPS on 19/20 for this size, which are the radio's RST and
// DIO1 with the slot switched to the wireless module.
#define HAS_GPS                    0
#define GPS_RX                    -1
#define GPS_TX                    -1
#define GPS_BAUD                9600

// ── No battery sense ─────────────────────────────────────────────────────────
// The charger (TP4059) has no ADC tap on the schematic.
#define BATT_ADC_PIN              -1
#define BATT_DIV                1.0f
#define BATT_SENSE_ENABLE_PIN     -1
#define BATT_SENSE_ENABLE_LEVEL  LOW

// ── Radio TCXO voltage ───────────────────────────────────────────────────────
// Meshtastic: SX126X_DIO3_TCXO_VOLTAGE 3.3, DIO2 as RF switch.
#define MESH_TCXO_V             3.3f

// ── Memory / display geometry ────────────────────────────────────────────────
#define HAS_PSRAM                  1
#define DEVICE_LCD_PORTRAIT_W    480
#define DEVICE_LCD_PORTRAIT_H    800
#define DEVICE_LCD_LANDSCAPE_W   800
#define DEVICE_LCD_LANDSCAPE_H   480
#include "config.h"
