#pragma once
// Nicolai Electronics Tanmatsu (ESP32-P4 + ESP32-C6 + CH32V203).
//
// Sources: badgeteam/esp32-component-badge-bsp targets/tanmatsu/tanmatsu_hardware.h,
// nicolaielectronics/tanmatsu-coprocessor (register map) and
// nicolaielectronics/mipi-dsi-abstraction dsi_panel_nicolaielectronics_st7701.c.
//
// The LoRa SX1262 is wired to the ESP32-C6, not to the P4. The C6 runs the
// tanmatsu-radio firmware, which exposes Wi-Fi/BLE over ESP-Hosted SDIO and
// the LoRa radio over ESP-Hosted custom peer data (see tanmatsu_lora.h).
// This firmware never reflashes the C6.

// ── Internal I2C bus: CH32V203 coprocessor ──────────────────────────────────
#define BOARD_I2C_SDA                          9
#define BOARD_I2C_SCL                         10
#define BOARD_I2C_PORT                         0
#define BOARD_I2C_FREQ                    400000

#define TANMATSU_COPROC_ADDR                0x5F
// The BSP arms this as a falling-edge interrupt. The keyboard path polls the
// coprocessor instead, so the line is informational here.
#define TANMATSU_COPROC_INT_PIN                1

#define TANMATSU_REG_FW_VERSION                0
#define TANMATSU_REG_KEYBOARD                  2   // 9 bytes of key bits
#define TANMATSU_REG_DISPLAY_BACKLIGHT        11
#define TANMATSU_REG_KEYBOARD_BACKLIGHT       12
#define TANMATSU_REG_INTERRUPT                13
#define TANMATSU_REG_LED_BRIGHTNESS           14   // global scale for all six LEDs
#define TANMATSU_REG_INPUT                    15
#define TANMATSU_REG_OUTPUT                   16
#define TANMATSU_REG_RADIO_CONTROL            17
#define TANMATSU_REG_PMIC_ADC_CONTROL        108
#define TANMATSU_REG_PMIC_ADC_VBAT           109   // 2 bytes, mV, little-endian
#define TANMATSU_REG_PMIC_CHARGING_STATUS    120
#define TANMATSU_REG_PMIC_POWER_CONTROL      126
#define TANMATSU_REG_LED_DATA                127   // 6 LEDs x 3 bytes, G R B
#define TANMATSU_REG_LED_MODE                145   // bit 0: coprocessor drives LEDs 0-3

// Front LEDs, in coprocessor order. In automatic mode the coprocessor owns 0-3
// (power, radio, messages, power button); manual mode hands all six to us.
#define TANMATSU_LED_COUNT                     6
#define TANMATSU_LED_POWER                     0
#define TANMATSU_LED_RADIO                     1   // labelled with the Wi-Fi glyph
#define TANMATSU_LED_MESSAGE                   2

#define TANMATSU_OUTPUT_AMPLIFIER_BIT          0

#define TANMATSU_RADIO_DISABLED                0
#define TANMATSU_RADIO_BOOTLOADER              1
#define TANMATSU_RADIO_APPLICATION             2

#define TANMATSU_INPUT_POWER_BUTTON_BIT        2

#define BOARD_POWERON                         -1
#define BOARD_VEXT_ENABLE                     -1
#define BOARD_VEXT_ON_LEVEL                 HIGH
#define BOARD_BUZZER                          -1

// ── Audio: ES8156 DAC on the coprocessor's I2C bus, I2S from the P4 ─────────
// The speaker amplifier is switched by the coprocessor (TANMATSU_OUTPUT_AMPLIFIER_BIT).
#define AUDIO_CODEC_ADDR                    0x08
#define AUDIO_DAC_I2S_MCLK                    30
#define AUDIO_DAC_I2S_BCK                     29
#define AUDIO_DAC_I2S_WS                      31
#define AUDIO_DAC_I2S_DOUT                    28
#define AUDIO_DAC_I2S_DIN                     -1
#define AUDIO_AMP_SETTLE_MS                   20

// ── Display: ST7701 480x800 over two-lane MIPI-DSI ──────────────────────────
#define TFT_PANEL_WIDTH                      480
#define TFT_PANEL_HEIGHT                     800
#define TFT_BRIGHTNESS_DEFAULT               200
#define TFT_INVERT                         false
#define TFT_RGB_ORDER                       true
#define TFT_RST                               14
#define TFT_TE                                11
#define TFT_BL                                -1
#define TFT_BL_INVERT                      false
#define TFT_BL_FREQ                        12000
#define TFT_BL_PWM_CH                          0

#define TANMATSU_DSI_LANES                     2
#define TANMATSU_DSI_LANE_MBPS               500
#define TANMATSU_DSI_DPI_MHZ                  30
#define TANMATSU_DSI_HSYNC                    40
#define TANMATSU_DSI_HBP                      40
#define TANMATSU_DSI_HFP                      30
#define TANMATSU_DSI_VSYNC                    16
#define TANMATSU_DSI_VBP                      16
#define TANMATSU_DSI_VFP                      16
#define TANMATSU_DSI_LDO_CHAN                  3
#define TANMATSU_DSI_LDO_MV                 2500

// Dummy SPI fields keep board-agnostic diagnostics and screenshot code
// well-formed. The DSI display path never constructs Bus_SPI.
#define TFT_SPI_HOST                   SPI2_HOST
#define TFT_SPI_SCK                           -1
#define TFT_SPI_MISO                          -1
#define TFT_SPI_MOSI                          -1
#define TFT_SPI_3WIRE                      false
#define TFT_SPI_WRITE_HZ                       0
#define TFT_SPI_READ_HZ                        0
#define TFT_CS                                -1
#define TFT_DC                                -1

#define HAS_TOUCH                              0
#define TOUCH_SDA                             -1
#define TOUCH_SCL                             -1
#define TOUCH_ADDR                          0x00
#define TOUCH_INT                             -1
#define TOUCH_RST                             -1
#define TOUCH_I2C_PORT                         0
#define TOUCH_I2C_FREQ                    400000
#define SCREEN_WAKE_FROM_TOUCH                 0

// ── LoRa: SX1262 behind the ESP32-C6 ────────────────────────────────────────
// No P4 GPIO touches the radio; tanmatsu_lora.cpp drives it over ESP-Hosted.
#define LORA_SPI_SCK                          -1
#define LORA_SPI_MOSI                         -1
#define LORA_SPI_MISO                         -1
#define LORA_CS                               -1
#define LORA_BUSY                             -1
#define LORA_DIO1                             -1
#define LORA_RST                              -1
#define LORA_FEM_POWER_PIN                    -1
#define LORA_FEM_ENABLE_PIN                   -1
#define LORA_FEM_TX_MODE_PIN                  -1
// TX power ceiling. Wadamesh caps this board at 20 dBm (MAX_LORA_TX_POWER);
// asking the C6 for 22 left the radio not receiving there.
#define MESH_POWER                            20
// The C6 firmware owns the TCXO configuration; this value is unused.
#define MESH_TCXO_V                         1.8f

// ── ESP32-C6 ESP-Hosted SDIO transport (SDMMC slot 1) ───────────────────────
#define TANMATSU_C6_SDIO_CLK                  17
#define TANMATSU_C6_SDIO_CMD                  16
#define TANMATSU_C6_SDIO_D0                   18
#define TANMATSU_C6_SDIO_D1                   19
#define TANMATSU_C6_SDIO_D2                   20
#define TANMATSU_C6_SDIO_D3                   21
// The C6 is reset through the coprocessor, not a GPIO, but Arduino's
// hostedSetPins() insists on a reset pin. GPIO50 (expansion E13) is unconnected
// on the mainboard. The default (GPIO54) is not: it carries the C6's UART TX.
#define TANMATSU_C6_SDIO_RST_DUMMY            50

// ── microSD: four-bit SDMMC host, same pins as the T-Display P4 ─────────────
#define SD_CS                                 -1
#define HAS_SD_CARD                            1
#define HAS_SD_MMC                             1
#define SDMMC_CLK                             43
#define SDMMC_CMD                             44
#define SDMMC_D0                              39
#define SDMMC_D1                              40
#define SDMMC_D2                              41
#define SDMMC_D3                              42

// ── Keyboard: scanned by the coprocessor, read over I2C ─────────────────────
#define HAS_KEYBOARD                           1
#define KB_SDA                     BOARD_I2C_SDA
#define KB_SCL                     BOARD_I2C_SCL
#define KB_ADDR             TANMATSU_COPROC_ADDR
#define KB_INT                                -1
#define KB_INT_ACTIVE_LEVEL                  LOW
#define KB_BL                                 -1
// No GPIO reaches the keyboard, so it cannot be a light-sleep wake source.
#define SCREEN_WAKE_FROM_KEYBOARD              0

#define HAS_TRACKBALL                          0
#define TBALL_UP                              -1
#define TBALL_DOWN                            -1
#define TBALL_LEFT                            -1
#define TBALL_RIGHT                           -1
#define TBALL_CLICK                           -1

// Volume-down button. Also a boot strap, which is why it reads active-low.
#define USER_BUTTON_PIN                       35
#define USER_BUTTON_ACTIVE_LEVEL             LOW
#define DISPLAY_TOGGLE_BUTTON_PIN             -1
#define DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL   LOW

#define HAS_GPS                                0
#define GPS_RX                                -1
#define GPS_TX                                -1
#define GPS_BAUD                            9600
#define GPS_ENABLE_PIN                        -1
#define GPS_RESET_PIN                         -1

// ── Battery: PMIC ADC behind the coprocessor ────────────────────────────────
#define BATT_ADC_PIN                          -1
#define BATT_DIV                            1.0f
#define BATT_SENSE_ENABLE_PIN                 -1
#define BATT_SENSE_ENABLE_LEVEL              LOW

#define HAS_PSRAM                              1
#define DEVICE_LCD_PORTRAIT_W                480
#define DEVICE_LCD_PORTRAIT_H                800
#define DEVICE_LCD_LANDSCAPE_W               800
#define DEVICE_LCD_LANDSCAPE_H               480
