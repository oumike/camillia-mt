#pragma once
// Tanmatsu LoRa: the SX1262 hangs off the ESP32-C6, which runs Nicolai
// Electronics' radio firmware and exposes it as a small RPC over ESP-Hosted
// peer data (msg id 1). This class speaks that protocol while presenting the
// subset of RadioLib's SX1262 API that MeshRadio uses, returning RADIOLIB_ERR_*
// codes, so mesh_radio.cpp keeps a single SX1262 code path.
//
// Protocol: tanmatsu-radio firmware, main/lora_protocol.h (MIT).

#include <Arduino.h>
#include <RadioLib.h>

// Powers the C6 up from cold and brings ESP-Hosted up on the Tanmatsu's SDIO
// pins. Must run before anything else touches Wi-Fi or BLE: the Arduino core
// latches its SDIO pin map on first init. Idempotent.
bool tanmatsuHostedBegin();

class TanmatsuLoraRpc {
public:
    int16_t reset();
    int16_t begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord,
                  int8_t power, uint16_t preambleLength, float tcxoVoltage);

    int16_t setFrequency(float freq);
    int16_t setBandwidth(float bw);
    int16_t setSpreadingFactor(uint8_t sf);
    int16_t setCodingRate(uint8_t cr);
    int16_t setOutputPower(int8_t power);
    int16_t setRxBoostedGainMode(bool enabled, bool persist = true);
    // The C6 owns the RF switch and PA current limit; accepted and ignored.
    int16_t setDio2AsRfSwitch(bool) { return RADIOLIB_ERR_NONE; }
    int16_t setCurrentLimit(float) { return RADIOLIB_ERR_NONE; }

    // MeshRadio calls this after begin(), so it must reach the static the RX
    // callback fires, not just the member.
    void setDio1Action(void (*func)(void));
    // True while received packets wait in the queue. MeshRadio clears its RX
    // flag around TX; on a real SX1262 the chip still holds the packet, but here
    // the queue is the only record, so poll it as a level.
    bool rxPending() const;

    int16_t standby();
    int16_t startReceive();
    int16_t transmit(uint8_t *data, size_t len, uint8_t addr = 0);

    size_t getPacketLength(bool update = true);
    int16_t readData(uint8_t *data, size_t len);
    float getRSSI() const { return _lastRssi; }
    float getSNR() const { return _lastSnr; }
    RadioLibTime_t getTimeOnAir(size_t len) const;   // microseconds

private:
    struct __attribute__((packed)) Config {
        uint32_t frequency;
        uint8_t  sf;
        uint16_t bandwidth;
        uint8_t  cr;
        uint8_t  syncWord;
        uint16_t preamble;
        uint8_t  power;
        uint8_t  rampTime;
        bool     crc;
        bool     invertIq;
        bool     ldro;
        bool     rxBoost;
        bool     useDcdc;
        bool     afc;
    };

    int16_t _pushConfig();
    int16_t _setMode(uint32_t mode);
    int16_t _request(uint32_t type, const void *payload, size_t len,
                     uint32_t timeoutMs, void *reply = nullptr, size_t replyCap = 0);
    static void _onPeerData(uint32_t msgId, const uint8_t *data, size_t len, void *ctx);

    Config   _cfg = {};
    float    _bwKhz = 250.0f;
    uint32_t _mode = 0;
    bool     _begun = false;
    float    _lastRssi = 0.0f;
    float    _lastSnr = 0.0f;
    void   (*_dio1)(void) = nullptr;
};
