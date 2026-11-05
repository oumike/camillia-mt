#include "config.h"

#if defined(DEVICE_TANMATSU)

#include "tanmatsu_lora.h"
#include "tanmatsu_coproc.h"

#include <math.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "esp32-hal-hosted.h"
#include "esp_hosted_misc.h"

namespace {

constexpr uint32_t kMsgLora = 1;

enum : uint32_t {
    kTypeAck       = 0x00,
    kTypeNack      = 0x01,
    kTypeSetMode   = 0x03,
    kTypeSetConfig = 0x05,
    kTypeGetStatus = 0x06,
    kTypePacketRx  = 0x07,
    kTypePacketTx  = 0x08,
};

enum : uint32_t {
    kModeStandbyRc = 0x01,
    kModeRx        = 0x05,
};

struct __attribute__((packed)) Header {
    uint32_t seq;
    uint32_t type;
};

struct __attribute__((packed)) RxWire {
    uint8_t rssiRaw;
    int8_t  snrRaw;
    uint8_t signalRssiRaw;
    uint8_t length;
    uint8_t data[256];
};

struct RxItem {
    uint16_t len;
    float    rssi;
    float    snr;
    uint8_t  data[256];
};

constexpr uint32_t kReplyCap = 64;
constexpr uint32_t kRpcTimeoutMs = 1000;

SemaphoreHandle_t sLock = nullptr;      // one outstanding request at a time
SemaphoreHandle_t sDone = nullptr;      // given by the callback on a reply
QueueHandle_t     sRxQueue = nullptr;
uint32_t          sSeq = 0;
volatile uint32_t sPendingSeq = 0;
volatile uint32_t sReplyType = 0;
uint8_t           sReply[kReplyCap];
volatile size_t   sReplyLen = 0;
uint8_t           sTxBuf[sizeof(Header) + 256];
bool              sHostedUp = false;
bool              sCallbackRegistered = false;
void            (*sDio1)(void) = nullptr;

} // namespace

// WiFi.mode(WIFI_OFF) tears ESP-Hosted down unless BLE holds it, and the core
// cannot bring it back (esp_hosted_sdio_set_config refuses a second call). The
// LoRa radio lives behind that same link, so on this board Wi-Fi off stops the
// Wi-Fi driver on the C6 and leaves the transport up. Linked with
// -Wl,--wrap=hostedDeinitWiFi (platformio.ini, env:tanmatsu).
extern "C" bool __wrap_hostedDeinitWiFi() {
    return true;
}

bool tanmatsuHostedBegin() {
    if (sHostedUp) return true;
    if (!tanmatsuCoprocBegin()) {
        Serial.println("[tanmatsu] coprocessor not responding; radio stays off");
        return false;
    }
    // Cold-start the C6 so it is waiting for the host when SDIO comes up; the
    // reset line the hosted stack would toggle is not wired on this board.
    if (!tanmatsuCoprocRadioPowerCycle()) {
        Serial.println("[tanmatsu] C6 power cycle failed");
    }
    if (!hostedSetPins(TANMATSU_C6_SDIO_CLK, TANMATSU_C6_SDIO_CMD,
                       TANMATSU_C6_SDIO_D0, TANMATSU_C6_SDIO_D1,
                       TANMATSU_C6_SDIO_D2, TANMATSU_C6_SDIO_D3,
                       TANMATSU_C6_SDIO_RST_DUMMY)) {
        // Already initialized by someone else with whatever pins they chose.
        if (!hostedIsInitialized()) return false;
    }
    // hostedInitWiFi() marks the link as held by Wi-Fi, which is what keeps
    // a later BLE deinit from tearing it down too.
    if (!hostedInitWiFi()) {
        Serial.println("[tanmatsu] ESP-Hosted init failed");
        return false;
    }
    uint32_t hMaj = 0, hMin = 0, hPat = 0, sMaj = 0, sMin = 0, sPat = 0;
    hostedGetHostVersion(&hMaj, &hMin, &hPat);
    hostedGetSlaveVersion(&sMaj, &sMin, &sPat);
    Serial.printf("[tanmatsu] ESP-Hosted up: host %lu.%lu.%lu, C6 %lu.%lu.%lu\n",
                  (unsigned long)hMaj, (unsigned long)hMin, (unsigned long)hPat,
                  (unsigned long)sMaj, (unsigned long)sMin, (unsigned long)sPat);
    sHostedUp = true;
    return true;
}

void TanmatsuLoraRpc::_onPeerData(uint32_t msgId, const uint8_t *data, size_t len, void *) {
    if (msgId != kMsgLora || len < sizeof(Header)) return;
    Header hdr;
    memcpy(&hdr, data, sizeof(hdr));
    const uint8_t *payload = data + sizeof(Header);
    const size_t payloadLen = len - sizeof(Header);

    if (hdr.type == kTypePacketRx && hdr.seq == 0) {
        if (payloadLen < 4 || !sRxQueue) return;
        RxWire wire;
        memset(&wire, 0, sizeof(wire));
        memcpy(&wire, payload, payloadLen < sizeof(wire) ? payloadLen : sizeof(wire));
        RxItem item;
        item.len = wire.length;
        if (item.len > payloadLen - 4) item.len = (uint16_t)(payloadLen - 4);
        item.rssi = wire.rssiRaw / -2.0f;
        item.snr = wire.snrRaw / 4.0f;
        memcpy(item.data, wire.data, item.len);
        if (xQueueSend(sRxQueue, &item, 0) == pdTRUE && sDio1) sDio1();
        return;
    }

    if (hdr.seq != 0 && hdr.seq == sPendingSeq) {
        const size_t n = payloadLen < kReplyCap ? payloadLen : kReplyCap;
        memcpy(sReply, payload, n);
        sReplyLen = n;
        sReplyType = hdr.type;
        sPendingSeq = 0;
        xSemaphoreGive(sDone);
    }
}

int16_t TanmatsuLoraRpc::_request(uint32_t type, const void *payload, size_t len,
                                  uint32_t timeoutMs, void *reply, size_t replyCap) {
    if (!sHostedUp || !sLock) return RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (len > 256) return RADIOLIB_ERR_PACKET_TOO_LONG;
    xSemaphoreTake(sLock, portMAX_DELAY);

    if (++sSeq == 0) sSeq = 1;   // 0 is reserved for unsolicited RX
    const Header hdr = {sSeq, type};
    memcpy(sTxBuf, &hdr, sizeof(hdr));
    if (len) memcpy(sTxBuf + sizeof(hdr), payload, len);

    xSemaphoreTake(sDone, 0);    // drop a stale give from a timed-out request
    sPendingSeq = sSeq;
    int16_t result = RADIOLIB_ERR_NONE;
    if (esp_hosted_send_custom_data(kMsgLora, sTxBuf, sizeof(hdr) + len) != ESP_OK) {
        result = RADIOLIB_ERR_SPI_WRITE_FAILED;
    } else if (xSemaphoreTake(sDone, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        result = RADIOLIB_ERR_RX_TIMEOUT;
    } else if (sReplyType == kTypeNack) {
        result = RADIOLIB_ERR_UNKNOWN;
    } else if (sReplyType != type && sReplyType != kTypeAck) {
        result = RADIOLIB_ERR_UNKNOWN;
    } else if (reply && replyCap) {
        memcpy(reply, sReply, sReplyLen < replyCap ? sReplyLen : replyCap);
    }
    sPendingSeq = 0;
    xSemaphoreGive(sLock);
    return result;
}

int16_t TanmatsuLoraRpc::reset() {
    if (!tanmatsuHostedBegin()) return RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (!sLock) {
        sLock = xSemaphoreCreateMutex();
        sDone = xSemaphoreCreateBinary();
        sRxQueue = xQueueCreate(8, sizeof(RxItem));
    }
    if (!sCallbackRegistered) {
        if (esp_hosted_register_custom_callback(kMsgLora, _onPeerData, nullptr) != ESP_OK) {
            Serial.println("[tanmatsu] LoRa callback registration failed");
            return RADIOLIB_ERR_CHIP_NOT_FOUND;
        }
        sCallbackRegistered = true;
    }
    return RADIOLIB_ERR_NONE;
}

int16_t TanmatsuLoraRpc::begin(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t syncWord,
                               int8_t power, uint16_t preambleLength, float) {
    if (!sCallbackRegistered) {
        const int16_t r = reset();
        if (r != RADIOLIB_ERR_NONE) return r;
    }
    sDio1 = _dio1;

    // A status round trip is the cheapest proof the C6 runs the LoRa server.
    uint8_t status[2 + 1 + 16] = {};
    int16_t state = _request(kTypeGetStatus, nullptr, 0, kRpcTimeoutMs, status, sizeof(status));
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[tanmatsu] C6 LoRa status failed: %d\n", state);
        return RADIOLIB_ERR_CHIP_NOT_FOUND;
    }
    char version[17] = {};
    memcpy(version, status + 3, 16);
    Serial.printf("[tanmatsu] C6 LoRa: chip=%s errors=0x%04x fw=%s\n",
                  status[2] == 1 ? "SX1268" : "SX1262",
                  (unsigned)(status[0] | (status[1] << 8)), version);

    _cfg.frequency = (uint32_t)lroundf(freq * 1000000.0f);
    _cfg.sf = sf;
    _bwKhz = bw;
    _cfg.bandwidth = (uint16_t)bw;   // 62.5 -> 62, which is what the C6 expects
    _cfg.cr = cr;
    _cfg.syncWord = syncWord;
    _cfg.preamble = preambleLength;
    _cfg.power = (uint8_t)power;
    _cfg.rampTime = 40;
    _cfg.crc = true;
    _cfg.invertIq = false;
    _cfg.useDcdc = true;
    _cfg.afc = false;
    _begun = true;
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::_pushConfig() {
    if (!_begun) return RADIOLIB_ERR_NONE;
    const float symMs = (float)(1UL << _cfg.sf) / _bwKhz;
    _cfg.ldro = symMs > 16.0f;
    // The C6 leaves the modem in standby after a config change.
    const int16_t state = _request(kTypeSetConfig, &_cfg, sizeof(_cfg), kRpcTimeoutMs);
    _mode = kModeStandbyRc;
    return state;
}

int16_t TanmatsuLoraRpc::setFrequency(float freq) {
    _cfg.frequency = (uint32_t)lroundf(freq * 1000000.0f);
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::setBandwidth(float bw) {
    _bwKhz = bw;
    _cfg.bandwidth = (uint16_t)bw;
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::setSpreadingFactor(uint8_t sf) {
    if (sf < 5 || sf > 12) return RADIOLIB_ERR_INVALID_SPREADING_FACTOR;
    _cfg.sf = sf;
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::setCodingRate(uint8_t cr) {
    if (cr < 5 || cr > 8) return RADIOLIB_ERR_INVALID_CODING_RATE;
    _cfg.cr = cr;
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::setOutputPower(int8_t power) {
    if (power < -9 || power > 22) return RADIOLIB_ERR_INVALID_OUTPUT_POWER;
    _cfg.power = (uint8_t)power;
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::setRxBoostedGainMode(bool enabled, bool) {
    _cfg.rxBoost = enabled;
    return _pushConfig();
}

int16_t TanmatsuLoraRpc::_setMode(uint32_t mode) {
    const int16_t state = _request(kTypeSetMode, &mode, sizeof(mode), kRpcTimeoutMs);
    if (state == RADIOLIB_ERR_NONE) _mode = mode;
    return state;
}

int16_t TanmatsuLoraRpc::standby() {
    return _setMode(kModeStandbyRc);
}

int16_t TanmatsuLoraRpc::startReceive() {
    // The C6 stays in RX across received packets, so re-arming after each one
    // would be a wasted round trip.
    if (_mode == kModeRx) return RADIOLIB_ERR_NONE;
    return _setMode(kModeRx);
}

int16_t TanmatsuLoraRpc::transmit(uint8_t *data, size_t len, uint8_t) {
    if (len == 0) return RADIOLIB_ERR_NONE;
    if (len > 253) return RADIOLIB_ERR_PACKET_TOO_LONG;
    // The C6 acknowledges once the packet is on the air, so wait the airtime.
    const uint32_t timeoutMs = (uint32_t)(getTimeOnAir(len) / 1000UL) * 5 / 4 + 1500;
    const int16_t state = _request(kTypePacketTx, data, len, timeoutMs);
    // It restores RX itself only if it was in RX; MeshRadio sends standby first.
    _mode = kModeStandbyRc;
    return state;
}

size_t TanmatsuLoraRpc::getPacketLength(bool) {
    RxItem item;
    if (!sRxQueue || xQueuePeek(sRxQueue, &item, 0) != pdTRUE) return 0;
    return item.len;
}

int16_t TanmatsuLoraRpc::readData(uint8_t *data, size_t len) {
    RxItem item;
    if (!sRxQueue || xQueueReceive(sRxQueue, &item, 0) != pdTRUE) {
        return RADIOLIB_ERR_RX_TIMEOUT;
    }
    const size_t n = item.len < len ? item.len : len;
    memcpy(data, item.data, n);
    _lastRssi = item.rssi;
    _lastSnr = item.snr;
    // MeshRadio clears its flag per packet; keep it raised while more wait.
    if (uxQueueMessagesWaiting(sRxQueue) > 0 && _dio1) _dio1();
    return RADIOLIB_ERR_NONE;
}

RadioLibTime_t TanmatsuLoraRpc::getTimeOnAir(size_t len) const {
    const int sf = _cfg.sf ? _cfg.sf : 11;
    const float bwHz = _bwKhz * 1000.0f;
    const int cr = _cfg.cr ? _cfg.cr : 5;
    const float tSym = (float)(1UL << sf) / bwHz;
    const int de = (tSym > 0.016f) ? 1 : 0;
    const float preamble = _cfg.preamble ? _cfg.preamble : 16;
    const int num = 8 * (int)len - 4 * sf + 28 + 16;
    const int den = 4 * (sf - 2 * de);
    const float payloadSym = 8.0f + fmaxf(ceilf((float)num / (float)den) * cr, 0.0f);
    return (RadioLibTime_t)(((preamble + 4.25f) + payloadSym) * tSym * 1000000.0f);
}

#endif // DEVICE_TANMATSU
