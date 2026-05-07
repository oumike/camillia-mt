#include "mesh_radio.h"
#include <SPI.h>

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#include <M5Unified.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#endif

volatile bool MeshRadio::_rxFlag = false;
MeshRadio Radio;
static constexpr bool kVerboseRadioIo = false;

void IRAM_ATTR MeshRadio::_onDio1() { _rxFlag = true; }

bool MeshRadio::init() {
    SPI.begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);

#if defined(DEVICE_CARDPUTER_LORA_HAT)
    m5::PI4IOE5V6408_Class ioexp(0x43, 400000, &m5::In_I2C);
    if (ioexp.begin()) {
        ioexp.setDirection(0, true);
        ioexp.setHighImpedance(0, false);
        ioexp.digitalWrite(0, true);
        Serial.println("[radio] enabled Cardputer Cap LoRa-1262 IO expander");
    }
#endif

#if (LORA_FEM_POWER_PIN >= 0)
    pinMode(LORA_FEM_POWER_PIN, OUTPUT);
    digitalWrite(LORA_FEM_POWER_PIN, HIGH);
#endif
#if (LORA_FEM_ENABLE_PIN >= 0)
    pinMode(LORA_FEM_ENABLE_PIN, OUTPUT);
    digitalWrite(LORA_FEM_ENABLE_PIN, HIGH);
#endif
#if (LORA_FEM_TX_MODE_PIN >= 0)
    pinMode(LORA_FEM_TX_MODE_PIN, OUTPUT);
    digitalWrite(LORA_FEM_TX_MODE_PIN, HIGH);
#endif

    int state = _radio.begin(MESH_FREQ, MESH_BW, MESH_SF, MESH_CR,
                             MESH_SYNC, MESH_POWER, MESH_PREAMBLE,
                             MESH_TCXO_V, false);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] init failed: %d\n", state);
        return false;
    }

    _radio.setDio2AsRfSwitch(true);
    _radio.setOutputPower(22);       // explicitly apply PA; begin() param alone may not stick
    _radio.setCurrentLimit(140.0);   // SX1262 HP PA max; default OCP may be too low
    _radio.setRxBoostedGainMode(true);   // sx126xRxBoostedGain from config
    _radio.setDio1Action(_onDio1);
    _radio.startReceive();

    _ready = true;
    Serial.printf("[radio] ready  %.3f MHz  SF%d  BW%.0f  CR4/%d\n",
                  MESH_FREQ, MESH_SF, MESH_BW, MESH_CR);
    return true;
}

bool MeshRadio::reconfigure(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t power) {
    if (!_ready) return false;
    bool ok = true;
    int state = _radio.standby();
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setFrequency(freq);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setSpreadingFactor(sf);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setBandwidth(bw);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setCodingRate(cr);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.setOutputPower(power);
    if (state != RADIOLIB_ERR_NONE) ok = false;
    state = _radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) ok = false;

    if (!ok) {
        Serial.printf("[radio] reconfigure failed  %.3f MHz  SF%d  BW%.0f  CR4/%d  %ddBm\n",
                      freq, sf, bw, cr, power);
        return false;
    }

    Serial.printf("[radio] reconfigured  %.3f MHz  SF%d  BW%.0f  CR4/%d  %ddBm\n",
                  freq, sf, bw, cr, power);
    return true;
}

bool MeshRadio::pollRx(MeshPacket &pkt) {
    if (!_rxFlag) return false;
    _rxFlag = false;

    size_t len = _radio.getPacketLength();
    if (len < sizeof(MeshHdr) || len > 256) {
        _radio.startReceive();
        return false;
    }

    uint8_t buf[256];
    if (_radio.readData(buf, len) != RADIOLIB_ERR_NONE) {
        _radio.startReceive();
        return false;
    }

    if (kVerboseRadioIo) {
        // Dump raw header bytes for wire-format verification
        Serial.printf("[radio] RX hdr: ");
        for (size_t i = 0; i < 16 && i < len; i++) Serial.printf("%02x ", buf[i]);
        Serial.println();
    }

    pkt.rssi  = _radio.getRSSI();
    pkt.snr   = _radio.getSNR();
    pkt.rxMs  = millis();
    memcpy(&pkt.hdr, buf, sizeof(MeshHdr));

    // Decrypt and decode
    size_t payloadLen = len - sizeof(MeshHdr);
    const uint8_t *cipher = buf + sizeof(MeshHdr);

    if (payloadLen > 0) {
        // Preserve raw cipher for deferred PKI decrypt in handleRx
        pkt.rawLen = (payloadLen <= sizeof(pkt.rawCipher)) ? payloadLen : 0;
        if (pkt.rawLen) memcpy(pkt.rawCipher, cipher, payloadLen);

        uint8_t plain[256];
        pkt.chanIdx = decryptPacket(pkt.hdr, cipher, plain, payloadLen);
        pkt.decrypted = (pkt.chanIdx >= 0);

        if (pkt.decrypted) {
            const uint8_t *payPtr; size_t payLen;
            decodeData(plain, payloadLen, pkt.portnum, payPtr, payLen, pkt.requestId, pkt.wantResponse);
            if (payPtr && payLen <= sizeof(pkt.payload)) {
                memcpy(pkt.payload, payPtr, payLen);
                pkt.payloadLen = payLen;
            } else {
                pkt.payloadLen = 0;
            }
        }
    } else {
        pkt.decrypted = false;
        pkt.chanIdx   = -1;
        pkt.payloadLen = 0;
    }

    _radio.startReceive();
    return true;
}

bool MeshRadio::transmit(const uint8_t *buf, size_t len) {
    if (kVerboseRadioIo) {
        // Dump header bytes for wire-format verification
        Serial.printf("[radio] TX hdr: ");
        for (size_t i = 0; i < 16 && i < len; i++) Serial.printf("%02x ", buf[i]);
        Serial.println();
    }

    _rxFlag = false;    // clear any stale DIO1 flag before TX
    int state = _radio.transmit(const_cast<uint8_t*>(buf), len);
    if (kVerboseRadioIo || state != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] TX state=%d (%s)\n", state,
                      state == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
    }
    _rxFlag = false;    // discard TX_DONE ISR trigger
    _radio.startReceive();
    return state == RADIOLIB_ERR_NONE;
}
