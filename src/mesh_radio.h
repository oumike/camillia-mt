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

private:
    bool    _ready = false;
    bool    _rxBoostedGain = (bool)MY_LORA_RX_BOOST;
#if defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121)
    LR1121  _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#else
    SX1262  _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#endif

    static void IRAM_ATTR _onDio1();
    static volatile bool  _rxFlag;
};

extern MeshRadio Radio;
