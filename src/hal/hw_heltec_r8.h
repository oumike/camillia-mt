#pragma once
// ── Heltec WiFi LoRa 32 V4-R8 + WiFi LoRa 32 Expansion Kit V2 ────────────────
// Issue #71. Sibling of hw_heltec_v4.h (V4 + Expansion Kit v1) and deliberately
// a separate header rather than #ifdefs inside that one: almost every peripheral
// on this pairing sits on a different pin, so a shared file would be two boards
// wearing one name.
//
// Two independent reasons the pins move:
//
//  1. The mainboard is an ESP32-S3R8 -- 8 MB *octal* PSRAM, where the V4's R2
//     part has 2 MB quad. Octal PSRAM consumes GPIO33-37 on the ESP32-S3, so
//     those five pins do not exist as GPIOs here. The V4 header uses all five
//     (TFT_SPI_MOSI 33, GPS_ENABLE 34, DISPLAY_TOGGLE 35, VEXT 36,
//     BATT_SENSE_ENABLE 37). That is also why the build needs
//     board_build.arduino.memory_type = qio_opi, not the V4's qio_qspi.
//  2. The Expansion Kit V2 is a different carrier from the v1 kit: display,
//     touch, SD and buzzer are all wired differently, and it adds a micro-SD
//     slot the v1 kit does not have.
//
// ── Provenance ───────────────────────────────────────────────────────────────
// Per .github/copilot-instructions.md, none of these numbers are guessed. They
// come from the wadamesh MeshCore port's heltec_v4_r8 environment
// (GPL-3.0-or-later, as is this project) --
//     https://github.com/ALLFATHER-BV/wadamesh
// -- whose own cited sources are the Meshtastic heltec_v4_r8 variant, Heltec's
// V4-R8 pin map, and Heltec's Expansion_board_V2.03 schematic. Several values
// there are marked hardware-confirmed in variants/heltec_v4/R8_AUDIT.md; those
// are called out individually below.
//
// Anything NOT confirmed by one of those sources is either inherited from the
// V4 because Heltec's published V4 -> V4-R8 difference list does not mention it,
// or set to -1 rather than invented. Both cases say so at the line.
//
// UNTESTED ON HARDWARE. This header has never been run on a device. See the
// bring-up notes at the bottom.

// GPIO7 keeps the module powered and is also the LoRa PA power enable, exactly
// as on the V4 -- wadamesh carries P_LORA_PA_POWER=7 for both boards.
#define BOARD_POWERON              7

// Passive buzzer. The Expansion Kit V2 puts a piezo behind a PAM8904 driver
// (U9); per Expansion_board_V2.03 the DIN line is GPIO4. EN1/EN2 are not GPIOs
// -- they are strapped high through 10K pull-ups, with DIP switch SW1 on the kit
// acting as a hardware mute. A silent buzzer on an otherwise working board is
// SW1, not this pin.
#define BOARD_BUZZER               4

// Switched peripheral rail, active LOW. Same polarity as the V4 (see the long
// note in hw_heltec_v4.h about GPIO36 and the touch controller); only the pin
// moved, 36 -> 40, because 36 is inside the octal PSRAM range.
#define BOARD_VEXT_ENABLE         40
#define BOARD_VEXT_ON_LEVEL       LOW

// ── TFT display — ST7789 320x240, Expansion Kit V2 wiring ───────────────────
// Four-wire here, unlike the v1 kit: the V2 brings MISO out on GPIO45, so the
// panel can be read and TFT_SPI_3WIRE is false.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               16
#define TFT_SPI_MISO              45
#define TFT_SPI_MOSI              15
#define TFT_SPI_3WIRE          false
// wadamesh raised this 20 -> 40 MHz and flags it [HW]: watch for tearing or
// garbled bands on the first real panel, and fall back to 26.6 MHz if seen.
#define TFT_SPI_WRITE_HZ     40000000
#define TFT_SPI_READ_HZ       4000000
#define TFT_CS                    47
#define TFT_DC                    48
#define TFT_BL                    44
#define TFT_BL_INVERT           false
#define TFT_BL_FREQ            44100
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   160
// Shared with the touch controller's reset -- see TOUCH_RST below, which is why
// that one is -1.
#define TFT_RST                   21
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_INVERT              true
#define TFT_RGB_ORDER           false

// ── LoRa — SX1262, unchanged from the V4 ────────────────────────────────────
// Heltec's published V4 -> V4-R8 difference list names only Vext_Ctrl,
// VGNSS_Ctrl, LED, PA_CTX, GNSS_RST and ADC_Ctrl, and wadamesh's R8 environment
// overrides none of the LoRa bus pins. So the radio is wired as on the V4.
#define LORA_SPI_SCK               9
#define LORA_SPI_MISO             11
#define LORA_SPI_MOSI             10
#define LORA_CS                    8
#define LORA_DIO1                 14
#define LORA_RST                  12
#define LORA_BUSY                 13

// ── LoRa front-end module — THE LEAST CERTAIN PINS ON THIS BOARD ────────────
// Read this before trusting a transmit.
//
// POWER is solid: wadamesh carries P_LORA_PA_POWER=7 on the V4 and the R8 alike.
//
// TX_MODE is derived, not sourced. The V4's value (GPIO46) is *definitively
// wrong* here -- 46 is the user LED on the R8, per both Heltec's difference list
// and wadamesh's P_LORA_TX_LED=46 -- so it cannot be inherited. Heltec's list
// also says PA_CTX ("PA control TX"), which is unbroken-out on the V4, becomes
// available at GPIO5 on the R8. GPIO5 is therefore the documented candidate for
// this line, but no source states outright that the Expansion Kit V2 routes the
// FEM's TX-mode input there.
//
// ENABLE is inherited on the strength of an absence: GPIO2 is not in Heltec's
// difference list, and that list is presented as complete.
//
// If the radio transmits but nothing hears it, or receive is deaf, these two are
// the first things to check on a scope -- ahead of anything in mesh_radio.cpp.
// Setting either to -1 leaves the line undriven, which mesh_radio.cpp handles
// (see its `>= 0` guards) and is the safe state while probing.
#define LORA_FEM_POWER_PIN         7   // FEM power enable (sourced)
#define LORA_FEM_ENABLE_PIN        2   // FEM RF switch enable (inherited)
#define LORA_FEM_TX_MODE_PIN       5   // HIGH = TX, LOW = RX (derived, verify)

// ── Storage — micro-SD on the shared display SPI bus ────────────────────────
// The headline feature of the Expansion Kit V2 over the v1 kit. The card shares
// the TFT's SPI bus, and GPIO3 is a strapping pin: wadamesh parks it HIGH in
// board init before the display driver floods that bus, and notes the card is
// REMOVABLE, so nothing may assume it is present.
//
// Deliberately NOT also setting HAS_INTERNAL_FS: sdBegin() routes straight to
// storageBegin() when that is defined, which would bypass the card entirely.
// This board has a real slot, so the card is the store -- same arrangement as
// the T-Deck.
#define SD_CS                      3
#define HAS_SD_CARD                1

// ── No keyboard; touch + buttons handle all input ────────────────────────────
#define HAS_KEYBOARD               0
#define KB_SDA                    -1
#define KB_SCL                    -1
#define KB_ADDR                0x00
#define KB_INT                    -1

// ── Capacitive touch — CHSC6X, sharing the board I2C bus ────────────────────
// The V2 kit unifies touch and the sensor bus on GPIO17/18, where the v1 kit
// gave touch its own pair on 47/48 (which are the display's CS and DC here).
//
// TOUCH_RST is -1 on purpose, and this is a bug fix inherited from upstream, not
// an omission: the Expansion V2 wires the CHSC6X reset to GPIO21 -- the same
// line as the ST7789 reset. A touch driver that pulses its own reset therefore
// resets the LCD mid-boot and wipes its init sequence, which shows up as a
// correct boot logo followed by a black screen and garbled rows on any redraw.
// The display init already resets both chips through GPIO21.
#define HAS_TOUCH                  1
#define TOUCH_SDA                 17
#define TOUCH_SCL                 18
#define TOUCH_ADDR              0x2E
#define TOUCH_INT                 43
#define TOUCH_RST                 -1   // do not drive: shared with TFT_RST
#define TOUCH_I2C_PORT             0   // Wire, shared with the sensors below
#define TOUCH_POLL_ENABLED         1

// ── Environment sensors — same unified bus as touch ─────────────────────────
// Not the V4's 4/3 pair: on this board GPIO4 is the buzzer and GPIO3 is the SD
// chip select, so those values would be actively harmful here.
#define ENV_SDA                   17
#define ENV_SCL                   18
#define ENV_I2C_PORT               0   // Wire

// ── No trackball ────────────────────────────────────────────────────────────
#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

// ── Buttons ─────────────────────────────────────────────────────────────────
// BOOT, as on every board in this family.
#define USER_BUTTON_PIN            0
#define USER_BUTTON_ACTIVE_LEVEL   LOW

// The V4's second button is GPIO35, which does not exist as a GPIO on an R8 --
// and on the V4 that pin is the mainboard LED, which the R8 moves to GPIO46. No
// source says where (or whether) the Expansion Kit V2 brings a second button
// out, so this is -1 rather than a guess. Every consumer guards on >= 0, so the
// board simply has one button until someone traces the kit.
#define DISPLAY_TOGGLE_BUTTON_PIN          -1
#define DISPLAY_TOGGLE_BUTTON_ACTIVE_LEVEL LOW

// ── GNSS ────────────────────────────────────────────────────────────────────
// Hardware-confirmed upstream: with EN asserted (active LOW) the module streams
// NMEA on GPIO39 at 9600 baud; GPIO38 is silent. Note the baud differs from the
// V4's 38400 -- upstream identifies the part as a CASIC AT6558R from its
// traffic, where Heltec's kit page advertises a Quectel L76K. The probed value
// wins here; if a unit is silent at 9600, 38400 is the first thing to try.
//
// GPS_RX is the pin *we listen on*, matching the V4 header's convention.
#define HAS_GPS                    1
#define GPS_RX                    39
#define GPS_TX                    38
#define GPS_BAUD                9600
#define GPS_ENABLE_PIN            42
#define GPS_ENABLE_ON_LEVEL      LOW
// Heltec's difference list removes GNSS_RST on the R8; there is no reset line to
// drive.
#define GPS_RESET_PIN             -1
#define GPS_RESET_ACTIVE_LEVEL   LOW

// ── Battery ─────────────────────────────────────────────────────────────────
// ADC_Ctrl is gone on the R8: the divider is permanently connected, so there is
// no rail to raise before a reading and no drain to avoid by lowering it.
// Hardware-confirmed upstream, along with the 5.07 ratio.
//
// The divider is high-resistance and the node sits around 0.65-0.85 V, which
// upstream found reads low at the default attenuation -- they moved to 2.5 dB
// plus a calibrated read. battery_util.cpp here uses the shared path, so expect
// to revisit that if the first readings come in low.
#define BATT_ADC_PIN               1
#define BATT_DIV                5.07f
#define BATT_SENSE_ENABLE_PIN     -1
#define BATT_SENSE_ENABLE_LEVEL   LOW

#define MESH_TCXO_V             1.8f

// 8 MB octal, against the V4's 2 MB quad. Welcome headroom for map tiles and
// LVGL buffers, but PSRAM is not internal DRAM, so none of the OTA/TLS
// contiguous-DRAM pressure this project works around changes.
#define HAS_PSRAM                  1

#define DEVICE_LCD_PORTRAIT_W    240
#define DEVICE_LCD_PORTRAIT_H    320
#define DEVICE_LCD_LANDSCAPE_W   320
#define DEVICE_LCD_LANDSCAPE_H   240

// ── Bring-up order, for whoever has the first unit ──────────────────────────
//  1. Does it boot at all? A qio_qspi build on an R8 misbehaves in the PSRAM
//     range, so a board that resets in the bootloader means the env is wrong,
//     not the pins.
//  2. Display. Proves TFT_SPI_MOSI moved off GPIO33 correctly.
//  3. Touch. If the logo appears and then the screen goes black or garbled on
//     the first redraw, something is driving GPIO21 -- see TOUCH_RST above.
//  4. LoRa TX *and* RX, with a second node. This is where LORA_FEM_TX_MODE_PIN
//     gets settled.
//  5. Micro-SD, then GNSS, then battery percentage on and off charge.
