#pragma once
// SX1262 radio wrapper for init, RX polling, and blocking TX operations.
#include <Arduino.h>
#include <RadioLib.h>
#include "mesh_proto.h"

#ifndef PAGER_LORA_USE_LR1121
#define PAGER_LORA_USE_LR1121 0
#endif

class MeshRadio {
public:
    bool init();
    bool reconfigure(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t power);

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
#if defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121)
    LR1121  _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#else
    SX1262  _radio{new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)};
#endif

    static void IRAM_ATTR _onDio1();
    static volatile bool  _rxFlag;
};

extern MeshRadio Radio;
