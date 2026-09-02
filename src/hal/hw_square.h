#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_square.h — Hardware pin definitions for the "square" board
//
// ESP32-S3, 16 MB flash, 8 MB octal PSRAM. A touch-first handheld with no
// physical keyboard, so the UI follows the Heltec TFT profile: on-screen
// keyboard for text entry, channel list as an overlay dropdown.
//
// Every value here is transcribed from the vendor's own firmware variant and
// its device-ui LovyanGFX configuration, not inferred from probing. Where a
// value is genuinely unknown it says so in the comment rather than carrying a
// plausible-looking guess.
//
// Three things about this board break assumptions every other profile holds,
// and each is called out at its section below:
//
//   1. The panel is on a QUAD-SPI bus with no DC line, and needs the
//      NV3031B driver that only exists in LovyanGFX 1.2.27+.
//   2. The backlight is an LP5814 I2C LED driver, not a PWM GPIO.
//   3. A PCA9555 I/O expander gates almost every rail on the board. Nothing
//      here answers until the expander has been brought up and sequenced.
//
// See issue #56 for the full port plan.
// ════════════════════════════════════════════════════════════════════════════

// ── I/O expander — PCA9555/TCA9555 ──────────────────────────────────────────
// The single most important thing on this board. LCD power and reset, touch
// reset, SD power and card-detect, GNSS power and reset, battery sense enable,
// audio PA power, Grove power, the user LED and the wake button all live here.
// Bring-up order is therefore: I2C -> expander -> everything else.
#define EXPANDER_ADDR           0x21
#define EXPANDER_INT              45   // active-low, shared interrupt

// Expander bit assignments. Inputs are marked; the rest are outputs that must
// be driven before the matching peripheral will answer.
#define EXP_BIT_WAKE_BUTTON        0   // in, active-low top Wake button
#define EXP_BIT_I2C_INT            1   // in
#define EXP_BIT_SD_DETECT          2   // in
#define EXP_BIT_TOUCH_INT          3   // driven low; touch is polled
#define EXP_BIT_LCD_CS             4   // expander-side LCD control, held high
#define EXP_BIT_LCD_POWER          5
#define EXP_BIT_LCD_RST            6
#define EXP_BIT_GROVE_POWER        7
#define EXP_BIT_TOUCH_RST          8
#define EXP_BIT_GNSS_RST           9
#define EXP_BIT_USER_LED          10
#define EXP_BIT_USB_OTG_EN        11
#define EXP_BIT_AUDIO_PA_POWER    12
#define EXP_BIT_GNSS_POWER        13
#define EXP_BIT_SD_POWER          14
#define EXP_BIT_BATT_SENSE_EN     15

// ── Shared I2C bus ──────────────────────────────────────────────────────────
// Expander, touch, backlight driver, battery ADC and both audio codecs all sit
// on this one bus. Wire (port 0).
#define BOARD_I2C_SDA             47
#define BOARD_I2C_SCL             48
#define BOARD_I2C_PORT             0
#define BOARD_I2C_FREQ        100000

// ── Power & board peripherals ───────────────────────────────────────────────
// No always-on power-hold pin and no switched VEXT rail on a GPIO: the rails
// this board switches are all behind the expander above.
#define BOARD_POWERON             -1
// No switched VEXT rail on a GPIO either — the LCD, GNSS, SD and audio rails
// this board gates are all expander bits. Still defined, and defined as -1
// rather than left out: setup() tests `#if (BOARD_VEXT_ENABLE >= 0)`, and an
// undefined macro evaluates to 0 there, so omitting it enables the block rather
// than skipping it.
#define BOARD_VEXT_ENABLE         -1
#define BOARD_VEXT_ON_LEVEL      HIGH
// No piezo. Audio is a full ES8311/ES7243E codec pair (see the audio section),
// which this firmware does not drive yet. -1 keeps HAS_VOLUME_CONTROL honest.
#define BOARD_BUZZER              -1

// ── TFT display — NV3031B 240x320 on a QUAD-SPI bus ─────────────────────────
// Not the Bus_SPI configuration every other board uses. This panel has four
// data lines and NO DC pin — the command/data distinction is carried in the
// QSPI transaction itself — and it runs in SPI mode 3.
//
// Panel_NV3031B does not exist in LovyanGFX 1.1.16, which the other six display
// environments pin. It appears in 1.2.27, so [env:square] pins that version in
// its own libdeps copy and the other boards are untouched. See issue #56.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               42
#define TFT_QSPI_IO0              41
#define TFT_QSPI_IO1              40
#define TFT_QSPI_IO2              39
#define TFT_QSPI_IO3              38
#define TFT_CS                    46
// No DC line on a QSPI panel, and no panel reset GPIO — reset is expander
// bit 6. Both are -1 so anything that still reads them drives nothing.
#define TFT_DC                    -1
#define TFT_RST                   -1
#define TFT_SPI_MODE               3
#define TFT_SPI_WRITE_HZ    75000000
#define TFT_SPI_READ_HZ     16000000
// Kept for the shared display.h code paths that still name them. This bus is
// quad, not 3-wire, and has no MISO of its own.
#define TFT_SPI_MISO              -1
#define TFT_SPI_MOSI              -1
#define TFT_SPI_3WIRE          false

#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_INVERT              true
#define TFT_RGB_ORDER           true
// The panel's own rotation offset, from the upstream LGFX config. Combined
// with TFT_ROTATION_DEFAULT=0, LovyanGFX uses internal rotation 1 (320x240).
#define TFT_PANEL_OFFSET_ROTATION  1

// ── Backlight — LP5814 I2C LED driver ───────────────────────────────────────
// Four channels on the shared I2C bus, not a PWM pin. lgfx::Light_PWM cannot
// drive this; it needs a custom lgfx::v1::ILight subclass.
//
// Two ordering constraints, both learned upstream and both worth keeping:
// initialise the LP5814 BEFORE the GT911 driver runs, and reset the I2C
// peripheral (Wire.end() / Wire.begin()) after GT911 init — a NACK during the
// GT911 probe can leave the ESP32 I2C peripheral's BUSY flag stuck, and the
// next backlight write then times out.
#define TFT_BL                    -1   // no PWM GPIO; see LP5814 below
#define LP5814_ADDR             0x2c
#define LP5814_MAX_CURRENT_MA     51
#define TFT_BRIGHTNESS_DEFAULT   160
#define LP5814_REG_DEVICE_CONFIG0 0x00
#define LP5814_REG_MAX_CURRENT    0x01
#define LP5814_REG_ENABLE_CONTROL 0x02
#define LP5814_REG_DIM_MODE       0x04
#define LP5814_REG_ENGINE_MODE    0x05
#define LP5814_REG_UPDATE         0x0F
#define LP5814_REG_LED0_DC        0x14
#define LP5814_REG_LED0_PWM       0x18
#define LP5814_LED_DC_VALUE        200

// Kept for shared code that names PWM-backlight fields. The Square display
// path uses its LP5814 ILight implementation instead.
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            44100
#define TFT_BL_PWM_CH              7

// ── Capacitive touch — GT911 ────────────────────────────────────────────────
// Standard LovyanGFX Touch_GT911, unlike the Heltec's custom CHSC6X wrapper.
// Address and rotation offset are what differ from the T-Deck.
#define HAS_TOUCH                  1
#define TOUCH_SDA         BOARD_I2C_SDA
#define TOUCH_SCL         BOARD_I2C_SCL
#define TOUCH_ADDR              0x5D
#define TOUCH_INT                 -1   // routed to expander bit 3, not a GPIO
#define TOUCH_RST                 -1   // expander bit 8
#define TOUCH_I2C_PORT    BOARD_I2C_PORT
#define TOUCH_I2C_FREQ    BOARD_I2C_FREQ
#define TOUCH_OFFSET_ROTATION      2
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED         1
#endif

// ── Screen wake policy ──────────────────────────────────────────────────────
// A tap does not wake the panel here; the buttons below are the wake gesture.
// This board spends its life face-up on a desk with a bare glass front and no
// keyboard to press by accident, so touch-to-wake is mostly pockets, sleeves
// and cats. It also drops the I2C touch poll that would otherwise run for the
// whole time the screen is off.
#define SCREEN_WAKE_FROM_TOUCH     0

// ── LoRa — SX1262 ───────────────────────────────────────────────────────────
// Exactly the shape mesh_radio.cpp already drives: TCXO on DIO3 at 1.8 V,
// DIO2 as the RF switch, and no external FEM to sequence.
#define LORA_SPI_SCK               4
#define LORA_SPI_MISO              5
#define LORA_SPI_MOSI              6
#define LORA_CS                   21
#define LORA_RST                   7
#define LORA_DIO1                  9
#define LORA_BUSY                  8
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_TCXO_V             1.8f

// ── GNSS — L76K-class on UART1 ──────────────────────────────────────────────
// The upstream variant feeds RX=18 / TX=17 directly to HardwareSerial's
// begin(baud, config, rx, tx). gps.cpp still probes the swapped pair as a
// recovery path. The baud starts at the L76K default and the prober also walks
// 38400 and 115200.
//
// Power and reset are expander bits 13 and 9. Reset is active-high: hold bit 9
// high for 10 ms, then drive it low to release the receiver.
#define HAS_GPS                    1
#define GPS_RX                    18
#define GPS_TX                    17
#define GPS_BAUD                9600
#define GPS_ENABLE_PIN            -1   // expander bit 13
#define GPS_RESET_PIN             -1   // expander bit 9

// ── Battery — ADS1115 external I2C ADC ──────────────────────────────────────
// There is no battery ADC GPIO on this board at all, hence -1: battery_util.cpp
// must not fall through to analogRead() on a floating pin. The sense path is
// gated by expander bit 15.
//
// Upstream reads ADS1115 single-ended channel 0 at GAIN_TWO (±2.048 V) and
// confirms that channel sees battery/2 through the resistor divider.
#define BATT_ADC_PIN              -1
#define ADS1115_ADDR            0x48
#define ADS1115_BATT_CHANNEL        0
#define BATT_DIV                2.0f
#define BATT_SENSE_ENABLE_PIN     -1   // expander bit 15
#define BATT_SENSE_ENABLE_LEVEL  HIGH

// ── microSD — SDIO, not SPI ─────────────────────────────────────────────────
// 1-bit SD_MMC on dedicated pins with no chip select, plus power-enable and
// card-detect on the expander. Camillia's storage path is SPI SD, so this needs
// a third mount branch in storage.cpp.
//
// The card uses the ESP32 SDMMC peripheral in one-bit mode. Power is active-high
// on expander bit 14 and is enabled by storageBegin() before mounting.
#define SD_CS                     -1
#define HAS_SD_CARD                1
#define HAS_SD_MMC                 1
#define SDMMC_CLK                  2
#define SDMMC_CMD                  3
#define SDMMC_D0                   1

// ── No keyboard, no trackball — touch and one button ────────────────────────
#define HAS_KEYBOARD               0
#define KB_SDA                    -1
#define KB_SCL                    -1
#define KB_ADDR                0x00
#define KB_INT                    -1
#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

// ── Buttons ─────────────────────────────────────────────────────────────────
// GPIO 0 is the only button directly on the MCU. The active-low top Wake button
// is expander bit 0; square_io reads it after GPIO45 signals an input change.
// A press wakes the display and a two-second hold while awake turns it off.
#define USER_BUTTON_PIN            0
#define USER_BUTTON_ACTIVE_LEVEL   LOW
#define DISPLAY_TOGGLE_BUTTON_PIN          -1
#define DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL LOW

// ── Audio — ES8311 DAC/amp + ES7243E mic ADC on I2S ─────────────────────────
// Notifications use the ES8311 output path. The microphone's ES7243E data input
// is recorded separately and is not needed for synthesized tones. PA power is
// active-high on expander bit 12; upstream requires 250 ms to settle.
#define I2S_BCK                   11
#define I2S_WS                    12
#define I2S_DOUT                  16
#define I2S_DIN                   15
#define I2S_MCLK                  10
#define AUDIO_CODEC_ADDR        0x18
#define AUDIO_DAC_I2S_BCK       I2S_BCK
#define AUDIO_DAC_I2S_WS        I2S_WS
#define AUDIO_DAC_I2S_DOUT      I2S_DOUT
#define AUDIO_DAC_I2S_DIN          -1
#define AUDIO_DAC_I2S_MCLK      I2S_MCLK
#define AUDIO_AMP_SETTLE_MS        250

// ── Memory / display geometry ───────────────────────────────────────────────
#define HAS_PSRAM                  1
#define DEVICE_LCD_PORTRAIT_W    240
#define DEVICE_LCD_PORTRAIT_H    320
#define DEVICE_LCD_LANDSCAPE_W   320
#define DEVICE_LCD_LANDSCAPE_H   240
