#include "mesh_radio.h"
#include <SPI.h>
#include <Wire.h>

#if defined(DEVICE_CARDPUTER_LORA_HAT)
#include <M5Unified.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#endif

volatile bool MeshRadio::_rxFlag = false;
MeshRadio Radio;
static constexpr bool kVerboseRadioIo = false;

#if defined(DEVICE_TLORA_PAGER_TFT)
namespace {
constexpr uint8_t kXl9555RegOut0 = 0x02;
constexpr uint8_t kXl9555RegOut1 = 0x03;
constexpr uint8_t kXl9555RegCfg0 = 0x06;
constexpr uint8_t kXl9555RegCfg1 = 0x07;

constexpr uint8_t kExpDrvEn  = 0;
constexpr uint8_t kExpAmpEn  = 1;
constexpr uint8_t kExpKbRst  = 2;
constexpr uint8_t kExpLoraEn = 3;
constexpr uint8_t kExpGpsEn  = 4;
constexpr uint8_t kExpNfcEn  = 5;
constexpr uint8_t kExpGpsRst = 7;
constexpr uint8_t kExpKbEn   = 8;
constexpr uint8_t kExpGpioEn = 9;
constexpr uint8_t kExpSdPullen = 11;
constexpr uint8_t kExpSdEn   = 12;

static bool xl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool xl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t &val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, 1) != 1) return false;
    val = Wire.read();
    return true;
}

static void xl9555SetOutput(uint8_t pin, bool level, bool invertDirSense,
                            uint8_t &out0, uint8_t &out1,
                            uint8_t &cfg0, uint8_t &cfg1) {
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) {
        if (invertDirSense) cfg0 |= bit;
        else                cfg0 &= (uint8_t)~bit;
        if (level) out0 |= bit;
        else       out0 &= (uint8_t)~bit;
    } else {
        if (invertDirSense) cfg1 |= bit;
        else                cfg1 &= (uint8_t)~bit;
        if (level) out1 |= bit;
        else       out1 &= (uint8_t)~bit;
    }
}

static void xl9555SetInput(uint8_t pin, bool invertDirSense,
                           uint8_t &cfg0, uint8_t &cfg1) {
    uint8_t bit = (uint8_t)(1U << (pin & 0x07));
    if (pin < 8) {
        if (invertDirSense) cfg0 &= (uint8_t)~bit;
        else                cfg0 |= bit;
    } else {
        if (invertDirSense) cfg1 &= (uint8_t)~bit;
        else                cfg1 |= bit;
    }
}

static int pagerFindExpanderAddr() {
    for (uint8_t a = 0x20; a <= 0x27; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) return (int)a;
    }
    return -1;
}

static bool pagerPrimeLoRaRail(bool invertDirSense) {
    Wire.begin(KB_SDA, KB_SCL);
    int expAddr = pagerFindExpanderAddr();
    if (expAddr < 0) {
        Serial.println("[radio] pager expander not found (0x20-0x27)");
        return false;
    }

    uint8_t out0 = 0xFF, out1 = 0xFF, cfg0 = 0xFF, cfg1 = 0xFF;
    (void)xl9555ReadReg((uint8_t)expAddr, kXl9555RegOut0, out0);
    (void)xl9555ReadReg((uint8_t)expAddr, kXl9555RegOut1, out1);
    (void)xl9555ReadReg((uint8_t)expAddr, kXl9555RegCfg0, cfg0);
    (void)xl9555ReadReg((uint8_t)expAddr, kXl9555RegCfg1, cfg1);

    // Match LilyGo pager init: set expand control rails high before radio probe.
    xl9555SetOutput(kExpDrvEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpAmpEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpKbRst, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpLoraEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpGpsEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpNfcEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpGpsRst, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpKbEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpGpioEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetOutput(kExpSdEn, true, invertDirSense, out0, out1, cfg0, cfg1);
    xl9555SetInput(kExpSdPullen, invertDirSense, cfg0, cfg1);

    bool ok = xl9555WriteReg((uint8_t)expAddr, kXl9555RegOut0, out0)
           && xl9555WriteReg((uint8_t)expAddr, kXl9555RegOut1, out1)
           && xl9555WriteReg((uint8_t)expAddr, kXl9555RegCfg0, cfg0)
           && xl9555WriteReg((uint8_t)expAddr, kXl9555RegCfg1, cfg1);
    if (!ok) {
        Serial.printf("[radio] pager expander write failed addr=0x%02X invert=%d\n",
                      expAddr, invertDirSense ? 1 : 0);
        return false;
    }

    Serial.printf("[radio] pager expander armed addr=0x%02X invert=%d\n",
                  expAddr, invertDirSense ? 1 : 0);
    delay(12);
    return true;
}
} // namespace
#endif

void IRAM_ATTR MeshRadio::_onDio1() { _rxFlag = true; }

bool MeshRadio::init() {
#if defined(DEVICE_TLORA_PAGER_TFT)
    (void)pagerPrimeLoRaRail(false);
#endif

    SPI.begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);

    Serial.printf("[radio] pins: sck=%d miso=%d mosi=%d cs=%d dio1=%d rst=%d busy=%d\n",
                  LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI,
                  LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

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

    _radio.reset();
    int state = _radio.begin(MESH_FREQ, MESH_BW, MESH_SF, MESH_CR,
                             MESH_SYNC, MESH_POWER, MESH_PREAMBLE,
                             MESH_TCXO_V);
#if defined(DEVICE_TLORA_PAGER_TFT)
    if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
        Serial.println("[radio] retrying init with alternate pager expander polarity");
        if (pagerPrimeLoRaRail(true)) {
            state = _radio.begin(MESH_FREQ, MESH_BW, MESH_SF, MESH_CR,
                                 MESH_SYNC, MESH_POWER, MESH_PREAMBLE,
                                 MESH_TCXO_V);
        }
    }
#endif
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[radio] init failed: %d\n", state);
        if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
            Serial.println("[radio] hint: LoRa chip not detected. Check build target, pin mapping, and power gating.");
#if defined(DEVICE_TLORA_PAGER_TFT)
            Serial.println("[radio] target=tlora-pager-tft (expected pins: CS36 DIO1=14 RST47 BUSY48)");
            Serial.printf("[radio] target=tlora-pager-tft (radio=%s)\n", PAGER_LORA_USE_LR1121 ? "LR1121" : "SX1262");
            Serial.println("[radio] target=tlora-pager-tft (verify XL9555 LoRa rail: EXPANDS_LORA_EN)");
#elif defined(DEVICE_TDECK)
            Serial.println("[radio] target=tdeck (expected pins: CS9 DIO1=45 RST17 BUSY13)");
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
            Serial.println("[radio] target=cardputer-cap (board IO expander must be enabled before SX1262 init)");
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
            Serial.println("[radio] target=heltec-v4 (verify board power and radio wiring)");
#endif
        }
        return false;
    }

#if !(defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121))
    _radio.setDio2AsRfSwitch(true);
#endif
    _radio.setOutputPower(22);       // explicitly apply PA; begin() param alone may not stick
#if !(defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121))
    _radio.setCurrentLimit(140.0);   // SX1262 HP PA max; default OCP may be too low
    _radio.setRxBoostedGainMode(true);   // sx126xRxBoostedGain from config
#endif
#if defined(DEVICE_TLORA_PAGER_TFT) && (PAGER_LORA_USE_LR1121)
    _radio.setIrqAction(_onDio1);
#else
    _radio.setDio1Action(_onDio1);
#endif
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
