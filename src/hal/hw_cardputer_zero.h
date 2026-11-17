// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_cardputer_zero.h — M5Stack CardputerZero + Cap LoRa-1262 (issue #155)
//
// Unlike every other target, the CardputerZero is a Linux machine: a Raspberry
// Pi CM0 (RP3A0, aarch64) running Debian. It is built by linux/CMakeLists.txt,
// not PlatformIO, and pin numbers below are BCM GPIO line offsets on
// /dev/gpiochip0, not ESP32 GPIOs. Mapping is from M5's CardputerZero and Cap
// LoRa-1262 pin tables, verified on hardware (RX and TX on US LongFast):
//
//   EXT  Cap signal          CM0
//    1   LoRa RST            GPIO26 (HAT_P0; M5IOE1 ext_usb_gpio_fun must be LOW)
//    3   LoRa IRQ (DIO1)     GPIO23 (HAT_P1)
//    5   LoRa BUSY           GPIO22
//   7-13 LoRa SPI            SPI0, CE1 -> /dev/spidev0.1 (kernel-driven CS)
//   8/10 I2C (PI4IOE @0x43)  I2C1 -> /dev/i2c-1
//   12/14 GPS UART           /dev/serial0 @ 115200
//
// The cap is powered by the Zero's M5IOE1 "ext_5v_out" switch, exposed as
// /sys/class/leds/ext_5v_out. The PI4IOE5V6408 P0 drives the cap's RF switch
// and must be set high before the SX1262 is used, as on the Cardputer.
// ════════════════════════════════════════════════════════════════════════════

#define LINUX_LORA_SPI_DEV        "/dev/spidev0.1"
#define LINUX_GPIO_CHIP           "/dev/gpiochip0"
#define LINUX_CAP_I2C_DEV         "/dev/i2c-1"
#define LINUX_CAP_IOE_ADDR        0x43
#define LINUX_CAP_POWER_LED       "/sys/class/leds/ext_5v_out/brightness"
#define LINUX_GPS_TTY             "/dev/serial0"

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON             -1
#define BOARD_BUZZER              -1
#define BOARD_VEXT_ENABLE         -1
#define BOARD_VEXT_ON_LEVEL       HIGH

// ── Display — ST7789v3 320×170, driven by the Linux kernel (not yet used) ────
#define TFT_SPI_SCK               -1
#define TFT_SPI_MISO              -1
#define TFT_SPI_MOSI              -1
#define TFT_CS                    -1
#define TFT_DC                    -1
#define TFT_BL                    -1
#define TFT_RST                   -1
#define TFT_BL_INVERT          false
#define TFT_BRIGHTNESS_DEFAULT   150
#define TFT_PANEL_WIDTH          170
#define TFT_PANEL_HEIGHT         320
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false

// ── LoRa — SX1262 via Cap LoRa-1262 ──────────────────────────────────────────
// SPI pins are owned by the kernel spidev driver; only control lines are GPIO.
#define LORA_SPI_SCK              -1
#define LORA_SPI_MISO             -1
#define LORA_SPI_MOSI             -1
#define LORA_CS                   -1   // CE1, driven by spidev
#define LORA_DIO1                 23
#define LORA_RST                  26
#define LORA_BUSY                 22
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1

#define HAS_SD_CARD                0

// ── Keyboard — TCA8418 @0x34, owned by the kernel (evdev); headless for now ──
#define HAS_KEYBOARD               0
#define KB_SDA                    -1
#define KB_SCL                    -1
#define KB_ADDR                0x00
#define KB_INT                    -1

#define HAS_TOUCH                  0
#define TOUCH_SDA                 -1
#define TOUCH_SCL                 -1
#define TOUCH_ADDR              0x00
#define TOUCH_INT                 -1
#define TOUCH_RST                 -1
#define TOUCH_I2C_PORT             0
#define TOUCH_POLL_ENABLED         0

#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

#define USER_BUTTON_PIN           -1
#define USER_BUTTON_ACTIVE_LEVEL   LOW

// ── GPS — on the cap, Linux UART ─────────────────────────────────────────────
#define HAS_GPS                    1
#define GPS_RX                    -1
#define GPS_TX                    -1
#define GPS_BAUD              115200

// ── Battery — BQ27220 fuel gauge (kernel power_supply); not yet read ─────────
#define BATT_ADC_PIN              -1
#define BATT_DIV                1.0f
#define BATT_SENSE_ENABLE_PIN     -1
#define BATT_SENSE_ENABLE_LEVEL   LOW

#define MESH_TCXO_V             3.0f

#define HAS_PSRAM                  1   // 512 MB system RAM
#define DEVICE_LCD_PORTRAIT_W    170
#define DEVICE_LCD_PORTRAIT_H    320
#define DEVICE_LCD_LANDSCAPE_W   320
#define DEVICE_LCD_LANDSCAPE_H   170
