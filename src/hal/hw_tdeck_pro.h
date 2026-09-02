#pragma once
// Hardware definitions for the LilyGo T-Deck Pro.
//
// Sources:
//   - LilyGo T-Deck-Pro factory firmware (utilities.h)
//   - Meshtastic t-deck-pro-v1_1 variant

// Power and board peripherals.
#define BOARD_POWERON            -1
#define BOARD_BUZZER             -1
#define BOARD_VEXT_ENABLE        -1
#define BOARD_VEXT_ON_LEVEL      HIGH

// Shared SPI bus: e-paper, SX1262, and microSD each have their own CS.
#define TFT_SPI_HOST         SPI2_HOST
#define TFT_SPI_SCK              36
#define TFT_SPI_MISO             47
#define TFT_SPI_MOSI             33
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ     2000000
#define TFT_SPI_READ_HZ      1000000
#define TFT_CS                   34
#define TFT_DC                   35
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ           12000
#define TFT_BL_PWM_CH             0
#define TFT_BRIGHTNESS_DEFAULT  255
#define TFT_PANEL_WIDTH         240
#define TFT_PANEL_HEIGHT        320
#define TFT_INVERT             false
#define TFT_RGB_ORDER          false
#define EINK_BUSY_PIN            37
#define TFT_RST                  16
#define TFT_BL                   45
#define TOUCH_RST                38

#define HAS_EINK                  1
#define EINK_REFRESH_MIN_MS     250

// SX1262 LoRa. GPIO46 powers the complete radio module.
#define LORA_SPI_SCK             36
#define LORA_SPI_MISO            47
#define LORA_SPI_MOSI            33
#define LORA_CS                   3
#define LORA_DIO1                 5
#define LORA_RST                  4
#define LORA_BUSY                 6
#define LORA_POWER_ENABLE_PIN    46
#define LORA_POWER_ENABLE_LEVEL HIGH
#define LORA_FEM_POWER_PIN       -1
#define LORA_FEM_ENABLE_PIN      -1
#define LORA_FEM_TX_MODE_PIN     -1
#define MESH_TCXO_V            2.4f

// microSD on the shared SPI bus.
#define SD_CS                    48
#define HAS_SD_CARD               1

// TCA8418 keyboard and keyboard backlight on the shared I2C bus.
#define HAS_KEYBOARD              1
#define KB_SDA                   13
#define KB_SCL                   14
#define KB_ADDR                0x34
#define KB_INT                   15
#define KB_INT_ACTIVE_LEVEL     LOW
#define KB_BL                    42

// The touch controller may report as CST328 or CST3530.
#define HAS_TOUCH                 1
#define TOUCH_SDA                13
#define TOUCH_SCL                14
#define TOUCH_ADDR             0x1A
#define TOUCH_INT                12
#define TOUCH_I2C_PORT            0
#define TOUCH_I2C_FREQ       400000
#define TOUCH_POLL_ENABLED        1

// Screen wake policy: the side button is the only way back.
//
// It is already the lock button -- a press while awake sleeps the panel to the
// clock overlay -- so making it the only thing that wakes keeps one button
// responsible for both halves of the same gesture. With a bare keyboard and a
// touch panel both facing outward, anything else meant a bag or a sleeve could
// wake an e-paper device and sit there redrawing.
//
// Keys are still drained while asleep so the controller's FIFO cannot back up;
// they simply do not wake it. VNC input remains exempt (see the wake gate in
// main_lvgl.cpp): a keystroke from a remote viewer was typed by someone already
// looking at the screen. The button reaches the wake path through
// pollUserButton(), which does not consult these flags, and stays a light-sleep
// wake source via USER_BUTTON_PIN in kNapWakeLines.
#define SCREEN_WAKE_FROM_KEYBOARD 0
#define SCREEN_WAKE_FROM_TOUCH    0

// The Pro has no trackball. Keyboard shortcuts and touch provide navigation.
#define HAS_TRACKBALL             0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

#define USER_BUTTON_PIN           0
#define USER_BUTTON_ACTIVE_LEVEL LOW

// u-blox MIA-M10Q GNSS.
#define HAS_GPS                   1
#define GPS_RX                   44
#define GPS_TX                   43
#define GPS_BAUD              38400
#define GPS_ENABLE_PIN           39
#define GPS_ENABLE_ON_LEVEL    HIGH

// Battery is measured through the BQ25896/BQ27220 pair over I2C.
#define BATT_ADC_PIN             -1
#define BATT_DIV               1.0f
#define BATT_SENSE_ENABLE_PIN    -1
#define BATT_SENSE_ENABLE_LEVEL  LOW

#define HAS_PSRAM                 1
#define DEVICE_LCD_PORTRAIT_W   240
#define DEVICE_LCD_PORTRAIT_H   320
#define DEVICE_LCD_LANDSCAPE_W  320
#define DEVICE_LCD_LANDSCAPE_H  240