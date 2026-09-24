#pragma once
// LilyGO T-Display P4 V1.0, 4.1-inch AMOLED variant.
// Source: Xinyuan-LilyGO/lilygo_device_driver, t_display_p4_config.h at
// submodule commit a47cf73e2c9d442c0f00b618749150f4482dcd0a.

// Base-board XL9535 on I2C0. Peripheral reset, power and IRQ signals routed
// through this expander are managed by tdisplay_p4_io.cpp.
#define TDISPLAY_P4_EXPANDER_ADDR          0x20
#define TDISPLAY_P4_EXPANDER_INT              5
#define TDISPLAY_P4_EXP_POWER_3V3             0
#define TDISPLAY_P4_EXP_ANTENNA_SELECT        1
#define TDISPLAY_P4_EXP_SCREEN_RST            2
#define TDISPLAY_P4_EXP_TOUCH_RST             3
#define TDISPLAY_P4_EXP_TOUCH_INT             4
#define TDISPLAY_P4_EXP_ETHERNET_RST          5
#define TDISPLAY_P4_EXP_AUDIO_POWER           6
#define TDISPLAY_P4_EXP_IMU_INT               7
#define TDISPLAY_P4_EXP_USB_PHY_POWER         8
#define TDISPLAY_P4_EXP_GPS_WAKE              9
#define TDISPLAY_P4_EXP_RTC_INT              10
#define TDISPLAY_P4_EXP_ESP32C6_WAKE         11
#define TDISPLAY_P4_EXP_ESP32C6_EN           12
#define TDISPLAY_P4_EXP_SD_POWER             13
#define TDISPLAY_P4_EXP_RADIO_RST            14
#define TDISPLAY_P4_EXP_RADIO_DIO1           15

#define BOARD_I2C_SDA                         7
#define BOARD_I2C_SCL                         8
#define BOARD_I2C_PORT                        0
#define BOARD_I2C_FREQ                   400000

#define BOARD_AUDIO_I2C_SDA                  20
#define BOARD_AUDIO_I2C_SCL                  21

#define BOARD_POWERON                        -1
#define BOARD_VEXT_ENABLE                    -1
#define BOARD_VEXT_ON_LEVEL                HIGH
#define BOARD_BUZZER                         -1

// RM69A10, two-lane MIPI-DSI. The reset line is expander-controlled.
#define TFT_PANEL_WIDTH                     568
#define TFT_PANEL_HEIGHT                   1232
#define TFT_BRIGHTNESS_DEFAULT              160
#define TFT_INVERT                         false
#define TFT_RGB_ORDER                       true
#define TFT_RST                              -1
#define TFT_BL                               -1
#define TFT_BL_INVERT                      false
#define TFT_BL_FREQ                       12000
#define TFT_BL_PWM_CH                         0

#define TDISPLAY_P4_DSI_LANES                 2
#define TDISPLAY_P4_DSI_LANE_MBPS          1000
// LilyGO's ECO2 RM69A10 example drives this timing set at 80 MHz (~64 Hz).
#define TDISPLAY_P4_DSI_DPI_MHZ              80
#define TDISPLAY_P4_DSI_HSYNC                 50
#define TDISPLAY_P4_DSI_HBP                  150
#define TDISPLAY_P4_DSI_HFP                   50
#define TDISPLAY_P4_DSI_VSYNC                 40
#define TDISPLAY_P4_DSI_VBP                  120
#define TDISPLAY_P4_DSI_VFP                   80

// Dummy SPI fields keep board-agnostic diagnostics and screenshot code
// well-formed. The P4 display path never constructs Bus_SPI.
#define TFT_SPI_HOST                   SPI2_HOST
#define TFT_SPI_SCK                          -1
#define TFT_SPI_MISO                         -1
#define TFT_SPI_MOSI                         -1
#define TFT_SPI_3WIRE                     false
#define TFT_SPI_WRITE_HZ                      0
#define TFT_SPI_READ_HZ                       0
#define TFT_CS                               -1
#define TFT_DC                               -1

// GT9895 touch. Raw 1060x2400 coordinates are scaled by the P4 display driver.
#define HAS_TOUCH                             1
#define TOUCH_SDA                 BOARD_I2C_SDA
#define TOUCH_SCL                 BOARD_I2C_SCL
#define TOUCH_ADDR                         0x5D
#define TOUCH_INT                            -1
#define TOUCH_RST                            -1
#define TOUCH_I2C_PORT           BOARD_I2C_PORT
#define TOUCH_I2C_FREQ           BOARD_I2C_FREQ
#define TOUCH_POLL_ENABLED                    1
#define TDISPLAY_P4_TOUCH_RAW_WIDTH         1060
#define TDISPLAY_P4_TOUCH_RAW_HEIGHT        2400

// SX1262 on SPI2. DIO1 and reset are virtual RadioLib HAL pins backed by the
// XL9535; BUSY and chip select are direct ESP32-P4 GPIOs.
#define LORA_SPI_SCK                          2
#define LORA_SPI_MOSI                         3
#define LORA_SPI_MISO                         4
#define LORA_CS                              24
#define LORA_BUSY                             6
#define LORA_DIO1                            -1
#define LORA_RST                             -1
#define LORA_FEM_POWER_PIN                   -1
#define LORA_FEM_ENABLE_PIN                  -1
#define LORA_FEM_TX_MODE_PIN                 -1
// LilyGO's RadioLib reference uses SX1262::begin()'s 1.6 V default.
#define MESH_TCXO_V                        1.6f

// Dedicated four-bit SDMMC host. Power enable is active-low on XL9535 bit 13.
#define SD_CS                                -1
#define HAS_SD_CARD                           1
#define HAS_SD_MMC                            1
#define SDMMC_CLK                            43
#define SDMMC_CMD                            44
#define SDMMC_D0                             39
#define SDMMC_D1                             40
#define SDMMC_D2                             41
#define SDMMC_D3                             42

// Detachable keyboard expansion. Keep HAS_KEYBOARD false so touch builds retain
// the on-screen keyboard; HAS_OPTIONAL_KEYBOARD compiles the runtime TCA8418
// path alongside it.
#define HAS_KEYBOARD                          0
#define HAS_OPTIONAL_KEYBOARD                 1
#define KB_SDA                               46
#define KB_SCL                               45
#define KB_ADDR                            0x34
#define KB_INT                               48
#define KB_INT_ACTIVE_LEVEL                 LOW
#define KB_BL                                47
#define KB_BL_PWM_CH                          1
#define KB_BL_FREQ                        20000
#define TDISPLAY_P4_KB_EXPANDER_ADDR       0x20
#define TDISPLAY_P4_KB_RESET_BIT              6
// The accessory INT is pulled low when detached, so it cannot be a static
// level-triggered wake source. Touch and the base-board expander still wake.
#define SCREEN_WAKE_FROM_KEYBOARD             0

#define HAS_TRACKBALL                         0
#define TBALL_UP                             -1
#define TBALL_DOWN                           -1
#define TBALL_LEFT                           -1
#define TBALL_RIGHT                          -1
#define TBALL_CLICK                          -1

#define USER_BUTTON_PIN                      35
#define USER_BUTTON_ACTIVE_LEVEL            LOW
#define DISPLAY_TOGGLE_BUTTON_PIN            -1
#define DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL  LOW

// L76K GNSS on UART1. Its wake control is XL9535 bit 9 (LilyGO kIo11), driven
// HIGH to wake -- L76k::Sleep(false) in llgok/cpp_bus_driver.
//
// MCU-side pins. LilyGO's l76k::kTx = 22 / kRx = 23 name the *module's* pins:
// t_display_p4_driver.cpp builds HardwareUart(l76k::kRx, l76k::kTx, UART_NUM_1)
// and that constructor is HardwareUart(tx, rx, ...) (cpp_bus_driver
// src/bus/uart/hardware_uart.h). So the P4 transmits on 23 and listens on 22.
// These were first transcribed the other way round, which left the P4
// listening on its own TX line: no NMEA, no GPS.
#define HAS_GPS                               1
#define GPS_RX                               22
#define GPS_TX                               23
#define GPS_BAUD                           9600
#define GPS_ENABLE_PIN                       -1
#define GPS_RESET_PIN                        -1

// BQ27220 fuel gauge on I2C0.
#define BATT_ADC_PIN                         -1
#define BATT_DIV                           1.0f
#define BATT_SENSE_ENABLE_PIN                -1
#define BATT_SENSE_ENABLE_LEVEL             LOW
#define BATT_FUEL_GAUGE_ADDR               0x55
#define HAS_BQ27220                           1

// ES8311 and speaker amplifier. Amplifier power is XL9535 bit 6.
#define AUDIO_CODEC_ADDR                   0x18
#define AUDIO_DAC_I2S_BCK                    12
#define AUDIO_DAC_I2S_WS                      9
#define AUDIO_DAC_I2S_DOUT                   10
#define AUDIO_DAC_I2S_DIN                    11
#define AUDIO_DAC_I2S_MCLK                   13
#define AUDIO_AMP_SETTLE_MS                   8

// ESP32-C6 ESP-Hosted SDIO transport on SDMMC host 1.
#define TDISPLAY_P4_C6_SDIO_CLK              18
#define TDISPLAY_P4_C6_SDIO_CMD              19
#define TDISPLAY_P4_C6_SDIO_D0               14
#define TDISPLAY_P4_C6_SDIO_D1               15
#define TDISPLAY_P4_C6_SDIO_D2               16
#define TDISPLAY_P4_C6_SDIO_D3               17

#define HAS_PSRAM                             1
#define DEVICE_LCD_PORTRAIT_W               568
#define DEVICE_LCD_PORTRAIT_H              1232
#define DEVICE_LCD_LANDSCAPE_W             1232
#define DEVICE_LCD_LANDSCAPE_H              568
