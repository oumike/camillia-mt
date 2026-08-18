#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_m9.h — Hardware pin definitions for the Elecrow ThinkNode M9
//
// ESP32-S3R8 handheld, 16 MB flash / 8 MB octal PSRAM:
//   • 2.4" ST7789 240×320 TFT (landscape, keyboard below the screen)
//   • Semtech LR1110 LoRa — NOT the SX1262 every other board here uses
//   • 37-key QWERTY on its own I2C bus, driven by a companion MCU that scans
//     the matrix and answers register reads at 0x6c
//   • CC1167Q GPS on UART1, microSD on the shared LoRa SPI bus
//   • No touch panel and no trackball — keyboard and d-pad only
//   • PCF8563 RTC, QMI8658 IMU and QMC6309 compass on the peripheral I2C bus
//
// Pin map source: the ThinkNode M9 V1.0 schematic, cross-checked against a
// working MeshCore port (~/Projects/wadamesh, variants/thinknode_m9) whose
// values were verified on hardware during bring-up. Anything below marked
// "verified" came from that bring-up rather than from reading the schematic
// alone, because several of these are not what the obvious reading suggests —
// see the notes on GPIO20/21 and GPIO48 in particular.
//
// Two independently switched rails, BOTH active-low:
//   BOARD_VEXT_ENABLE (18, P-MOS) gates the LCD / GPS / sensor rail
//   TFT_BL           (17, PNP)   is the backlight, separate from that rail
// ════════════════════════════════════════════════════════════════════════════

// ── Power & board peripherals ────────────────────────────────────────────────
#define BOARD_POWERON            -1   // No latch pin; USB/battery rail is always up
#define BOARD_BUZZER              9   // Passive buzzer
// The peripheral rail. Active LOW through a P-MOS: driving it low turns the
// LCD, GPS and sensor supply ON.
#define BOARD_VEXT_ENABLE        18
#define BOARD_VEXT_ON_LEVEL     LOW

// ── TFT display — ST7789 240×320, shared SPI bus ────────────────────────────
// Radio, panel and SD all sit on one SPI peripheral with separate chip selects,
// the same arrangement the T-Deck uses.
#define TFT_SPI_HOST         SPI2_HOST
#define TFT_SPI_SCK              40
#define TFT_SPI_MISO             38
#define TFT_SPI_MOSI             47
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    40000000
#define TFT_SPI_READ_HZ      1000000
#define TFT_CS                   16
#define TFT_DC                   15
#define TFT_RST                  14
// Backlight is a PNP transistor, so the enable is active LOW — the inverse of
// every other board here, hence TFT_BL_INVERT.
#define TFT_BL                   17
#define TFT_BL_INVERT           true
#define TFT_BL_FREQ           12000
// NOT channel 0, which is what this was while the pin map was being modelled on
// the T-Deck's. LovyanGFX claims its LEDC channel straight through the IDF, so
// Arduino's own allocator never learns the channel is taken — and Arduino hands
// them out from 0 upward. This board is the only one that then calls tone()
// (BOARD_BUZZER >= 0; the T-Deck shares channel 0 but drives an I2S speaker, and
// every other buzzer board already sits on 7), so the message-alert tone would
// take channel 0 off GPIO17 and repoint it at the buzzer. With the backlight
// active LOW, losing the drive turns it ON: a message arriving with the screen
// asleep lit a blank panel — SLPIN still set, so no wake, just a glow — and left
// it lit until the next real wake reprogrammed the pin.
#define TFT_BL_PWM_CH             7
#define TFT_BRIGHTNESS_DEFAULT  128
#define TFT_PANEL_WIDTH         240
#define TFT_PANEL_HEIGHT        320
#define TFT_INVERT             true
#define TFT_RGB_ORDER          false

// ── LoRa — LR1110 on the shared SPI bus ─────────────────────────────────────
// The LR11x0 family, not SX126x. mesh_radio.h selects the RadioLib class from
// the DEVICE_* flag; see MESH_LORA_LR1110 below.
#define LORA_SPI_SCK             40   // Shared with TFT / SD
#define LORA_SPI_MISO            38
#define LORA_SPI_MOSI            47
#define LORA_CS                  39
#define LORA_DIO1                42
#define LORA_RST                 45
#define LORA_BUSY                41
// The LR1110's RF switch is driven from its own DIO5/DIO6 (schematic U7 pins
// 19/20 → U8 V1/V2), not from host GPIOs, so there is no front-end module for
// the firmware to sequence.
#define LORA_FEM_POWER_PIN       -1
#define LORA_FEM_ENABLE_PIN      -1
#define LORA_FEM_TX_MODE_PIN     -1

// ...but "no host GPIOs" is not the same as "nothing to configure". The LR1110
// only drives DIO5/DIO6 after it has been told to, with SetDioAsRfSwitch, and
// RadioLib only sends that command when a switch table has been handed to it.
// With no table the chip leaves both DIOs alone for the whole session, U8 stays
// in its power-on state, and every mode — RX included — runs through whatever
// path that happens to leave connected. Strong local traffic still gets in,
// which is what makes this look like "receives some packets" rather than a dead
// radio.
//
// Pin assignment is schematic-confirmed and corroborated by the MeshCore port
// this pin map was cross-checked against, which traces DIO5 (U7 pin 20) -> R20
// -> net RFSW0_V1 -> U8 V1, and DIO6 (pin 19) -> R19 -> net RFSW1_V2 -> U8 V2.
// DIO7/DIO8 are unconnected here: a plain 2-pin switch into one antenna, not the
// 4-pin DIO5-8 scheme other LR1110 boards use.
//
// The per-mode truth table below is still the conventional one rather than
// something read off U8 (its part number is not printed on the schematic), but
// it is the same table that port runs on this hardware, so it is no longer a
// leading suspect for a receive fault — issue #43 turned out to be a buffer-read
// bug, not this. Still: a wrong TX row points the PA at the receive port, which
// at 22 dBm is how an LNA dies, so if TX range ever falls off while RX is fine,
// the TX rows are the ones to look at — do not blind-swap them.
//              DIO5  DIO6
//   STBY        0     0
//   RX          1     0
//   TX (LP)     1     1
//   TX_HP       0     1
#define MESH_LR11XX_RFSWITCH_DIO56 1

// Selects the LR11x0 driver in mesh_radio.h/.cpp.
#define MESH_LORA_LR1110          1

// ── microSD — same SPI bus, own CS ──────────────────────────────────────────
// GPIO48 (schematic net SPICLK_N). That net is only reserved on the R8V/R16V
// parts with a 1.8 V differential flash clock; this is a plain R8, so the pin
// is genuinely free. Do NOT reach for GPIO35-37 here — the octal PSRAM owns
// them and driving one wedges the PSRAM bus at boot.
#define SD_CS                    48
#define HAS_SD_CARD               1

// ── I2C keyboard — companion matrix controller at 0x6c ─────────────────────
// Its own bus, with no other device on it.
//
// GPIO19/20 are the S3's native USB D-/D+ pads and the ROM's USB-Serial-JTAG
// peripheral holds them from reset, including a pull-up on D+. This board's
// console is an external UART bridge instead and the schematic reuses 20/21
// for the keyboard bus, so the pad claim has to be released before the
// controller will answer at all (see M9_KB_NEEDS_USB_PAD_RELEASE).
//
// Protocol note for the driver: this is NOT the T-Deck's bare-read keyboard.
// Production STC8H units expose the key at 0x05 and signal on GPIO12. Early
// ESP32-S2 units identify as HW 0x03 and expose the key at register 0x01.
#define HAS_KEYBOARD              1
#define KB_SDA                   20
#define KB_SCL                   21
#define KB_ADDR               0x6c
#define KB_INT                   12
#define KB_INT_ACTIVE_LEVEL    HIGH
// GPIO46 is the schematic's KEY_LED net, but it is NOT wired to the keypad LEDs
// on this board — the bring-up port records it as unconnected, and it is an S3
// strapping pin besides. Kept only so the pin map stays a complete record of the
// schematic; nothing drives it. The keypad light is KB_REG_BACKLIGHT below.
#define KB_LED                   -1   // KEY_LED (GPIO46) — not wired, do not drive
#define KB_REG_KEY             0x05   // Pending key byte; 0x00 means no key
#define KB_REG_KEY_EARLY       0x01
#define KB_REG_HW_VERSION      0x00
#define KB_REG_FW_VERSION      0xFE
#define KB_REG_LONG_PRESS_MS   0x03   // Write big-endian duration in milliseconds
// Keypad backlight brightness, 0..255 — NOT an on/off control, and not usable as
// a notification light. The LEDs belong to the companion controller, which lights
// them only for its own keypress auto-light (~10 s timeout); this register just
// sets how bright that is. The host cannot turn them on.
//
// Verified on hardware against the early ESP32-S2 controller (hw=0x03 fw=0x10):
// writing 255 lights nothing on its own, and writing 0 disables the auto-light
// until the next power cycle. That is why the M9 is deliberately excluded from
// HAS_KB_BLINK — see the note there before wiring this up to anything.
// Documented but unused, like KB_REG_LONG_PRESS_MS above.
#define KB_REG_BACKLIGHT       0x02
#define M9_KB_NEEDS_USB_PAD_RELEASE 1

// ── Touch — none ────────────────────────────────────────────────────────────
// Keyboard and d-pad only. The macros still have to exist because board.h and
// the display/keyboard layers reference them unconditionally.
#define HAS_TOUCH                 0
#define TOUCH_SDA                -1
#define TOUCH_SCL                -1
#define TOUCH_ADDR             0x00
#define TOUCH_INT                -1
#define TOUCH_RST                -1
#define TOUCH_I2C_PORT            0
#ifndef TOUCH_POLL_ENABLED
#define TOUCH_POLL_ENABLED        0
#endif

// ── Screen wake policy ───────────────────────────────────────────────────────
// Keyboard wake is fine here: unlike the T-Deck there is no lid-exposed
// capacitive panel to trigger on contact, and the keys are the only input.
#define SCREEN_WAKE_FROM_KEYBOARD 1
#define SCREEN_WAKE_FROM_TOUCH    0

// ── Trackball — none ────────────────────────────────────────────────────────
// Navigation is the keyboard's own d-pad, which arrives as key codes over I2C
// rather than as GPIO edges.
#define HAS_TRACKBALL             0
#define TBALL_UP                 -1
#define TBALL_DOWN               -1
#define TBALL_LEFT               -1
#define TBALL_RIGHT              -1
#define TBALL_CLICK              -1

// ── User / boot button ───────────────────────────────────────────────────────
#define USER_BUTTON_PIN           0   // BOOT button, active LOW
#define USER_BUTTON_ACTIVE_LEVEL LOW

// ── GPS — CC1167Q on UART1 ───────────────────────────────────────────────────
// GPS_RST goes through an NPN whose base is driven by the GPIO, so a HIGH
// asserts reset and a LOW releases it — inverted from the usual sense.
#define HAS_GPS                   1
#define GPS_RX                    3
#define GPS_TX                    2
#define GPS_BAUD             115200
#define GPS_ENABLE_PIN           11
#define GPS_ENABLE_ON_LEVEL     LOW
#define GPS_RESET_PIN             5
#define GPS_RESET_ACTIVE_LEVEL  HIGH

// ── Peripheral I2C bus — RTC / IMU / compass ────────────────────────────────
// PCF8563 at 0x51, QMI8658 at 0x6b, QMC6309 at 0x7c. Separate from the
// keyboard bus above.
#define I2C_SDA                   7
#define I2C_SCL                   6

// ── Battery ADC ──────────────────────────────────────────────────────────────
// Plain 1:2 divider straight to the ADC, no enable gate.
#define BATT_ADC_PIN             13
#define BATT_DIV               2.0f
#define BATT_SENSE_ENABLE_PIN    -1
#define BATT_SENSE_ENABLE_LEVEL  LOW

// ── Radio TCXO voltage ───────────────────────────────────────────────────────
// 3.3 V, confirmed on hardware. This is not a free choice: with 0 the LR1110
// boots on its internal RC oscillator — SPI answers, so it looks alive — and
// then rejects the first command that needs the real 32 MHz clock.
#define MESH_TCXO_V            3.3f

// ── Memory / display geometry ────────────────────────────────────────────────
#define HAS_PSRAM                 1
#define DEVICE_LCD_PORTRAIT_W   240
#define DEVICE_LCD_PORTRAIT_H   320
#define DEVICE_LCD_LANDSCAPE_W  320
#define DEVICE_LCD_LANDSCAPE_H  240
