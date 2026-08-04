#pragma once
// ════════════════════════════════════════════════════════════════════════════
// hal/hw_mesh_deck.h — Hardware pin definitions for the Attaky Mesh Deck 1.0
//
// https://docs.attaky.com/docs/datasheets/builds/mesh-deck
//
// Unlike every other target here, the Mesh Deck is a modular frame rather than
// one board. The build is:
//   • Core: ESP32 1.0        — ESP32-S3-WROOM-1U-N16R8, ST7789 240x320, FT6636
//                              touch, AW9523 button/LED expander, IMU, ALS
//   • Expand: LoRa / GPS 1.0 — Ra-01SH (SX1262) + ATGM336H-5N31 (AT6558) GNSS
//   • Control: Keyboard 1.0  — 48-key QWERTY, two AW9523 expanders, 2x 5x5
//                              matrix, over a 12-pin FPC
//   • Power: Standard Cell   — 1S 1000 mAh with a MAX17048 fuel gauge
//
// Modules meet the frame over board-to-board (BTB) and FPC connectors, so the
// module datasheets name signals by CONNECTOR POSITION, not by GPIO. The pin
// numbers below therefore have to come from the host side, and Attaky does not
// publish them — see the PIN MAP block at the bottom.
//
// ── Verified connector mapping ──────────────────────────────────────────────
// The frame's crossover rule (docs/frame-and-connections/connectors) is
//     F-BTB(n) <-> M-BTB(76 - n)   for positions 26..50
//     F-BTB(n) <-> M-BTB(26 - n)   for positions  1..25
// Applying it to the LoRa/GPS module's own table resolves every radio signal to
// a host-side role, and each one lands on a pin whose documented role matches —
// which is what makes this mapping trustworthy rather than a guess:
//
//   Module pin      ->  Host pin   Host role (per frame docs)      Signal
//   M-BTB26 SCK     ->  F-BTB50    SPI SCK                         LORA_SPI_SCK
//   M-BTB27 MOSI    ->  F-BTB49    SPI MOSI                        LORA_SPI_MOSI
//   M-BTB28 MISO    ->  F-BTB48    SPI MISO                        LORA_SPI_MISO
//   M-BTB33 NSS     ->  F-BTB43    SPI CS                          LORA_CS
//   M-BTB30 BUSY    ->  F-BTB46    I2S DIN (reused)                LORA_BUSY
//   M-BTB38 RESET   ->  F-BTB38    I2S WS / reset                  LORA_RST
//   M-BTB39 DIO1    ->  F-BTB37    I2S / radio interrupt           LORA_DIO1
//   M-BTB31 GPS TX  ->  F-BTB45    UART RX / I2S BCK               GPS_RX (host)
//   M-BTB32 GPS RX  ->  F-BTB44    UART TX / I2S                   GPS_TX (host)
//
// What remains unknown is only the last hop: F-BTB position -> ESP32-S3 GPIO.
// ════════════════════════════════════════════════════════════════════════════

// ── Known-good hardware facts (no GPIO needed) ──────────────────────────────
// Recorded here so they survive the pin hunt; none of these are guesses.

// Core module
#define MESH_DECK_MCU_PART      "ESP32-S3-WROOM-1U-N16R8"   // 16 MB flash, 8 MB PSRAM (octal)

// Display — ST7789, 2.0" 240x320 over SPI, portrait native.
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320

// Touch — FT6636 capacitive controller on the main I2C bus.
// Reset and interrupt are NOT GPIOs: they hang off the AW9523 expanders.
#define TOUCH_I2C_ADDR          0x38
#define TOUCH_RST_EXPANDER      0x59   // pin P13, active-low
#define TOUCH_INT_EXPANDER      0x58   // pin P04

// Buttons and status LED — AW9523BTQR expander at 0x59, not direct GPIO.
// Map per Attaky's hardware catalog (Attaky-Firmware-Builder):
//   P00 DOWN   P01 LEFT   P02 SELECT/Enter   P03 RIGHT
//   P04 UP     P05 BTN_L1 (top-left shoulder)
//   P06 BTN_R2 (top-right shoulder)          P07 POWER_BTN
//   LED: red P10, green P11, blue P12 (active-low, common anode)
// POWER_BTN is hardware-fixed: a long press (~2 s) cuts power below firmware.
#define BTN_EXPANDER_ADDR       0x59
// P06 (BTN_R2) is currently unbound — sleep/wake belongs to the BOOT button.
// Kept named so whatever it gets assigned to has somewhere obvious to go.
#define BTN_R2_BIT              6      // P06 BTN_R2, free

// Every expander interrupt is aggregated onto this one real GPIO: 0x59's INT
// chains through 0x58 P17, and 0x58's INTN lands here. Nothing uses it yet —
// the buttons and keyboard are polled — but it is the pin that would let either
// become interrupt-driven, and the only way a front button could wake the CPU
// out of a light-sleep nap (an I2C expander cannot be a wake source).
#define EXPANDER_INT_PIN           4

// Writing a whole LEDMODE byte (0x12/0x13) puts every pin of that port in GPIO
// mode, which is what this firmware wants — it never uses the RGB LED's
// constant-current path. Attaky's driver notes call for read-modify-write there
// precisely because a blanket write would drop that LED mode; worth knowing
// before anyone adds LED brightness support on top of the shared 0x59 handle.

// Sensors on the main I2C bus.
#define IMU_I2C_ADDR            0x6B   // QMI8658A
#define ALS_I2C_ADDR            0x48   // STK3311-X

// Keyboard — two AW9523 expanders over the 12-pin FPC, one per half. Each
// drives a 5x5 matrix: rows P10..P14 pulled low one at a time, columns P00..P04
// read back; a key reads pressed when its row x column intersection pulls a
// column low. Each half raises its own interrupt line.
#define KB_LEFT_I2C_ADDR        0x5A
#define KB_RIGHT_I2C_ADDR       0x5B

// Battery — MAX17048 fuel gauge over I2C. This board reports state of charge
// digitally; there is no battery ADC divider to read, so battery_util.cpp needs
// a MAX17048 path rather than the analogRead()/M5 paths the other targets use.
#define BATT_FUEL_GAUGE_ADDR    0x36   // MAX17048 standard address
#define HAS_BATTERY_FUEL_GAUGE     1
#define BATT_ADC_PIN              -1   // no divider on this board

// GNSS — ATGM336H-5N31 (AT6558), NMEA-0183 at 9600 baud.
#define GPS_BAUD                9600
#define HAS_GPS                    1

// LoRa — Ra-01SH, a Semtech SX1262. Same radio family as the other targets, so
// mesh_radio.cpp needs no new driver.
#define LORA_IS_SX1262             1

// Peripherals this board does not have.
#define BOARD_BUZZER              -1
#define BOARD_VEXT_ENABLE         -1
#define BOARD_VEXT_ON_LEVEL       HIGH
#define HAS_SD_CARD                0   // not listed on the Mesh Deck build

// The one GPIO Attaky publishes anywhere in the doc set.
#define BOOT_BUTTON_PIN           38   // ESP32-S3 IO38, direct (not expander)

// ── PIN MAP ─────────────────────────────────────────────────────────────────
// Attaky publishes no F-BTB -> GPIO table, so these come from the wadamesh
// MeshCore port's attaky_mesh_series variant, which runs on this hardware:
//   https://github.com/ALLFATHER-BV/wadamesh
//   variants/attaky_mesh_series + the env build flags in platformio.ini
// Both projects are GPL-3.0-or-later. These are pin facts read off a working
// port rather than copied code.
//
// Cross-check: the derived F-BTB roles above line up with what that port uses —
// its LoRa SPI (SCK/MISO/MOSI/NSS = 5/7/6/8) matches the SPI roles at
// F-BTB50/48/49/43, and its GPS pair sits on the UART roles at F-BTB45/44. Two
// independent sources agreeing is why this is trusted without a schematic.
#define MESH_DECK_PINS_KNOWN 1

// Display — ST7789 on its own SPI bus, separate from the radio. Write-only:
// there is no MISO, so panel readback (and screenshot capture) is unavailable.
//
// SPI3_HOST, not SPI2. Arduino's global SPI object is FSPI = SPI2_HOST on the
// ESP32-S3, and mesh_radio.cpp calls SPI.begin(LORA_SPI_*) — so a panel left on
// SPI2 has its bus repointed at the radio's pins the moment the radio starts,
// mid-splash. T-Deck and the Pager can share SPI2 because their radio sits on
// the same physical pins as the panel; this board's does not, which is why
// Cardputer and Heltec (also split buses) put their panels on SPI3 too.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               11
#define TFT_SPI_MOSI              13
#define TFT_SPI_MISO              -1   // not wired; panel cannot be read back
#define TFT_SPI_3WIRE          false
#define TFT_CS                    45
#define TFT_DC                    12
#define TFT_RST                   10
#define TFT_BL                    14   // LEDA control, active HIGH
#define TFT_BL_INVERT           false
#define TFT_BL_FREQ            12000
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   150
#define TFT_SPI_WRITE_HZ     80000000
#define TFT_SPI_READ_HZ       1000000
#define TFT_INVERT              true   // panel ships inverted
#define TFT_RGB_ORDER           false  // RGB, not BGR

// LoRa — Ra-01SH (SX1262) on a second SPI bus.
#define LORA_SPI_SCK               5   // F-BTB50
#define LORA_SPI_MISO              7   // F-BTB48
#define LORA_SPI_MOSI              6   // F-BTB49
#define LORA_CS                    8   // F-BTB43
#define LORA_DIO1                 21   // F-BTB37
#define LORA_RST                   9   // F-BTB38
#define LORA_BUSY                 16   // F-BTB46
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1

// The Ra-01SH clocks its SX1262 from a plain 32 MHz crystal, NOT a TCXO, so
// DIO3 is not a TCXO supply and must not be driven as one. RadioLib's begin()
// defaults to 1.6 V on DIO3; leaving that in place on this module stalls the
// radio waiting for an oscillator that never settles. mesh_radio.cpp has to
// pass 0.0 here. DIO2 does drive the RF switch.
#define MESH_TCXO_V             0.0f   // 0 = crystal; every other board here has a TCXO
#define LORA_DIO2_AS_RF_SWITCH     1

// GNSS UART — ATGM336H-5N31.
//
// These are HOST-side: GPS_RX is the pin this MCU receives NMEA on, matching
// gps.cpp's GpsProbePort{rx, tx}. The wadamesh port names the same two wires
// from the module's point of view (its PIN_GPS_RX=18 is the GPS chip's RX, i.e.
// our TX), so its numbers are the reverse of ours — do not copy them across
// without swapping. The connectors page settles it: F-BTB45 is IO17 and carries
// "UART RX", F-BTB44 is IO18 and carries "UART TX".
#define GPS_RX                    17   // F-BTB45 = IO17, module TX -> host RX
#define GPS_TX                    18   // F-BTB44 = IO18, host TX -> module RX

// I2C main bus: touch, expanders, fuel gauge, IMU, ALS, and the keyboard FPC.
#define I2C_SDA                    2   // F-BTB30 = IO2
#define I2C_SCL                    1   // F-BTB29 = IO1
// A second I2C bus reaches the BTB connector but nothing on this build uses it.
#define I2C_SECONDARY_SDA         42   // F-BTB31 = IO42
#define I2C_SECONDARY_SCL         41   // F-BTB32 = IO41
#define KB_SDA                I2C_SDA
#define KB_SCL                I2C_SCL
// Shared keyboard code expects one address and one interrupt line. This board
// has two of each (a half per expander), handled by the DEVICE_MESH_DECK branch
// in keyboard.cpp; these exist so the generic path still compiles, and so the
// I2C scanner labels 0x5A sensibly.
#define KB_ADDR     KB_LEFT_I2C_ADDR
#define KB_INT                    -1   // polled, not interrupt-driven

// Touch — FT6636 shares the main I2C bus. LovyanGFX drives it with its
// FT5x06-family driver (see lgfx_tdeck.h).
//
// pin_int is -1 on purpose: the interrupt is not a GPIO on this board, it is
// expander 0x58 P04, which LovyanGFX cannot poll. Without it the driver falls
// back to polling the controller over I2C, which costs a little bus traffic but
// works. Reset is likewise expander 0x59 P13 and must be released by us before
// the panel is initialised.
#define HAS_TOUCH                  1
#define TOUCH_ADDR      TOUCH_I2C_ADDR
#define TOUCH_SDA             I2C_SDA
#define TOUCH_SCL             I2C_SCL
#define TOUCH_INT                 -1   // expander-driven; see above
#define TOUCH_RST                 -1   // expander 0x59 P13, not a GPIO
#define TOUCH_I2C_PORT             0   // Wire0
// With no GPIO interrupt to watch, the touch controller has to be polled.
#define TOUCH_POLL_ENABLED         1
// Keyboard interrupt lines are not GPIOs at all: per the connectors page,
// C-FPC5 (L_INT) lands on expander 0x58 P14 and C-FPC6 (R_INT) on 0x58 P07.
// Reading them would mean an I2C transaction per poll, which costs more than
// just scanning the matrix, so both halves are polled — as wadamesh does too.
// -1 keeps the scanner on its idle cadence.
#define KB_INT_LEFT               -1   // C-FPC5 -> expander 0x58 P14, not a GPIO
#define KB_INT_RIGHT              -1   // C-FPC6 -> expander 0x58 P07, not a GPIO
#define HAS_KEYBOARD               1

// The front buttons are all on expander 0x59, not GPIO. GPIO0 is the one real
// button pin — wadamesh binds its user button there. Note this is NOT the IO38
// the core datasheet calls "Boot"; both exist.
// The board's one real GPIO button. pollUserButton() already treats this as a
// screen toggle (its log string is "BOOT button"), so pointing it at the right
// pin is all that was needed to make the key do something.
//
// Previously 0, taken from the wadamesh port. Attaky's hardware catalog does
// not list IO0 as a button at all — the button is IO38 — so that binding read a
// pin with nothing on it and the key appeared dead.
//
// Worth having even though BTN_R2 already toggles the panel: this is a true
// GPIO, so unlike the expander buttons it can serve as a light-sleep wake
// source. The expander cannot wake the CPU, which is why a press there can take
// until the end of a nap (NAP_MAX_MS) to register.
#define USER_BUTTON_PIN   BOOT_BUTTON_PIN
#define USER_BUTTON_ACTIVE_LEVEL LOW

// Only a deliberate press wakes the panel, the same policy the T-Deck uses.
// This board has a dedicated screen key (BTN_R2), so nothing is lost by
// excluding the two inputs that fire without being meant to: a capacitive panel
// reports stray touches when it is face-down or being carried, and the keyboard
// is a 48-key matrix that a bag can lean on. Both would otherwise defeat the
// screen-off button entirely — press it, and the next stray event turns the
// display straight back on.
//
// Excluding them here also drops them as light-sleep GPIO wake sources, so the
// CPU stays napping too (see the note in hal/board.h).
#define SCREEN_WAKE_FROM_KEYBOARD  0
#define SCREEN_WAKE_FROM_TOUCH     0

// ── Absent hardware ─────────────────────────────────────────────────────────
// Shared code reads these unconditionally, so a board without the peripheral
// still has to name it. sdBegin() in particular keys off (SD_CS < 0) rather
// than HAS_SD_CARD, so omitting SD_CS is a compile error, not a silent no-op.

// No card slot. SD_CS still has to exist because sdBegin() keys off
// (SD_CS < 0) rather than HAS_SD_CARD; -1 keeps that path disabled.
#define SD_CS                     -1

// ...but this board is not storage-less. It keeps the files an SD card would
// hold in a LittleFS partition instead — see partitions_16mb_fs.csv, which
// gives it the 9.5 MB left over after the two OTA app slots. Code should test
// HAS_FILE_STORAGE (storage.h) rather than HAS_SD_CARD when the question is
// "can a file be saved", and reach the filesystem through storageFs().
#define HAS_INTERNAL_FS            1
#define INTERNAL_FS_PARTITION  "littlefs"

// Battery is the MAX17048 fuel gauge (see above), so there is no divider and
// no sense-enable line. BATT_DIV is named only because the shared ADC path
// references it; nothing on this board reads it.
#define BATT_DIV                1.0f
#define BATT_SENSE_ENABLE_PIN     -1
#define BATT_SENSE_ENABLE_LEVEL HIGH

// No trackball; navigation is the D-pad on expander 0x59 plus the keyboard.
#define HAS_TRACKBALL              0
#define TBALL_UP                  -1
#define TBALL_DOWN                -1
#define TBALL_LEFT                -1
#define TBALL_RIGHT               -1
#define TBALL_CLICK               -1

// The frame keeps every rail on; there is no host-controlled power latch.
#define BOARD_POWERON             -1

// Panel geometry. Native orientation is portrait 240x320; the default
// rotation (1) presents it landscape.
#define DEVICE_LCD_PORTRAIT_W    240
#define DEVICE_LCD_PORTRAIT_H    320
#define DEVICE_LCD_LANDSCAPE_W   320
#define DEVICE_LCD_LANDSCAPE_H   240

#define HAS_PSRAM                  1   // 8 MB octal
