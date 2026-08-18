#pragma once
// SX1262 radio wrapper for init, RX polling, and blocking TX operations.
#include <Arduino.h>
// RadioLib raises an unconditional #warning when ARDUINO_USB_CDC_ON_BOOT=1
// (RadioLib.h:67), advising a hardware UART for debug output. On these boards
// USB CDC *is* the console, so the advice does not apply — it is suppressed via
// build_src_flags in platformio.ini rather than here, because #warning is
// emitted during preprocessing where `#pragma GCC diagnostic` does not reach it.
#include <RadioLib.h>
#include "mesh_proto.h"

#ifndef PAGER_LORA_USE_LR1121
#define PAGER_LORA_USE_LR1121 0
#endif

// True on boards whose radio is an LR11x0 rather than an SX126x. The families
// diverge enough in RadioLib's API — no DIO2-as-RF-switch, no current limit, no
// RX-boost setter, setIrqAction() instead of setDio1Action() — that most call
// sites have to branch. This used to be spelled out longhand as
// "DEVICE_TLORA_PAGER_TFT && PAGER_LORA_USE_LR1121" at every one of them, which
// was a chance to miss one the moment a second LR11x0 board arrived; the M9 is
// that board.
#if (defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121)) \
    || defined(MESH_LORA_LR1110)
#  define MESH_RADIO_IS_LR11XX 1
#else
#  define MESH_RADIO_IS_LR11XX 0
#endif

#if MESH_RADIO_IS_LR11XX
// RadioLib keeps the raw RX-buffer accessors protected, and these boards need
// them: the LR11x0 moves its RX buffer base a few bytes on every packet, and a
// packet that starts late enough wraps around the end of the chip's 256-byte
// buffer. RadioLib's readData() reads it as one linear run from the start
// offset, so the wrapped tail comes back wrong — which loses long packets and
// leaves short ones alone. See _readPacketLr11xx() in mesh_radio.cpp.
//
// A subclass rather than a second RadioLib patch script: a patch has to be
// re-applied (and re-verified) on every dependency bump, where this just keeps
// compiling.
//
// The exposed members live on LR11x0, the shared base of both parts, so the
// defect and the fix are the same on the LR1121 pager variant as on the M9's
// LR1110 — hence one macro for both rather than a per-chip carve-out.
// THE ONE THAT WAS LOSING LONG MESSAGES. The chip's SetPacketParams carries a
// PayloadLength field that means two things at once: the number of bytes to
// transmit, and *the largest payload the receiver will accept*. RadioLib
// programs it with the TX length on every transmit (LR11x0.cpp, the
// RADIO_MODE_TX branch of stageMode) and, in explicit-header mode, never puts it
// back when arming RX — the RX branch only touches packet params for implicit
// headers.
//
// So the moment this node transmits anything, its receiver stops accepting
// packets longer than that transmission. A node that has just sent a 50-byte
// position beacon is deaf to a 120-byte text message, and the chip drops it
// silently: no interrupt, no CRC error, nothing to see from the host side. The
// cutoff is stable because our periodic broadcasts are all about the same size,
// which is what made it look like a fixed length limit rather than a bug.
//
// The SX126x is unaffected — it ignores this field for explicit-header RX — so
// every other board in the fleet received the messages the M9 dropped.
//
// Restoring it to the maximum before each RX arm is the fix; see _armRx().
#define MESH_LR11XX_RESTORE_RX_MAX_PAYLOAD                              \
    int16_t restoreRxMaxPayload() {                                     \
        return this->setPacketParamsLoRa(this->preambleLengthLoRa,      \
                                         this->headerType,              \
                                         RADIOLIB_LR11X0_MAX_PACKET_LENGTH, \
                                         this->crcTypeLoRa,             \
                                         (uint8_t)this->invertIQEnabled); \
    }

#define MESH_LR11XX_EXPOSE_BUFFER_API           \
    using LR11x0::readBuffer8;                  \
    using LR11x0::clearRxBuffer;                \
    using LR11x0::getErrors;                    \
    MESH_LR11XX_RESTORE_RX_MAX_PAYLOAD

class MeshLR1110 : public LR1110 {
public:
    explicit MeshLR1110(Module *mod) : LR1110(mod) {}
    MESH_LR11XX_EXPOSE_BUFFER_API
};

class MeshLR1121 : public LR1121 {
public:
    explicit MeshLR1121(Module *mod) : LR1121(mod) {}
    MESH_LR11XX_EXPOSE_BUFFER_API
};
#endif

class MeshRadio {
public:
    // txPower/rxBoostedGain come from live config. They are passed in rather
    // than read from a compile-time default because init() applies the PA
    // setting explicitly, and hardcoding it there silently overrode whatever
    // the user had configured.
    bool init(uint8_t txPower = MESH_POWER, bool rxBoostedGain = (bool)MY_LORA_RX_BOOST);
    bool reconfigure(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t power);

    // Applied live; also re-applied by init(). Safe to call before init(), in
    // which case it only records the preference.
    void setRxBoostedGain(bool enabled);

    // Called from loop() — returns true and fills pkt if a packet is ready.
    bool pollRx(MeshPacket &pkt);

    // Blocking transmit. Re-arms receive after. Returns false on error.
    bool transmit(const uint8_t *buf, size_t len);

    // Temporarily pause/resume receive mode (used by screenshot capture on
    // shared-SPI builds to avoid bus contention).
    void setRxPaused(bool paused);

    bool isReady() const { return _ready; }

    // Airtime telemetry over a rolling 1-hour window (DeviceMetrics).
    // channelUtil = % of the hour the channel was busy (our TX + heard RX);
    // airUtilTx   = % of the hour spent transmitting.
    float channelUtilPercent();
    float airUtilTxPercent();

    // Force the next pollRx() to read the radio, even if the DIO1 edge ISR did
    // not run (e.g. the CPU came out of light sleep on the DIO1 level wake).
    void wakeRxCheck() { _rxFlag = true; }

    // Where the RX path is losing packets, counted since boot: interrupts
    // serviced, packets handed up, and the three ways one can be dropped below
    // the app. Prints one line; `why` just labels the call site.
    static void logRxHealth(const char *why);

private:
    // The only way this class arms receive. Everything that used to call
    // _radio.startReceive() goes through here, because on LR11x0 the chip needs
    // its RX payload ceiling put back first (see restoreRxMaxPayload) and a call
    // site that forgets is a radio that silently stops hearing long packets.
    int _armRx();

#if MESH_RADIO_IS_LR11XX
    // Wrap-safe replacement for RadioLib's readData() on this family; see the
    // comment on the definition for the ring-buffer bug it exists to fix.
    // Returns a RADIOLIB_ERR_*; outLen is the packet length on success.
    int _readPacketLr11xx(uint8_t *buf, size_t bufCap, size_t &outLen);

    // Chip-reported identity and error register (calibration, PLL, oscillator).
    void logLr11xxHealth(const char *why);
#endif

    bool    _ready = false;
    bool    _rxBoostedGain = (bool)MY_LORA_RX_BOOST;
#if defined(MESH_LORA_LR1110)
    MeshLR1110 _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#elif defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121)
    MeshLR1121 _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#else
    SX1262  _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#endif

    static void IRAM_ATTR _onDio1();
    static volatile bool  _rxFlag;
};

extern MeshRadio Radio;
