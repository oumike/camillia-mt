#include "mqtt_bridge.h"
#include "debug_flags.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

// ── Tunables ──────────────────────────────────────────────────
namespace {
constexpr uint32_t kMinBackoffMs        = 2000;    // first reconnect delay
constexpr uint32_t kMaxBackoffMs        = 60000;   // cap between reconnect tries
constexpr int      kMqttBufferBytes     = 512;     // whole MQTT packet (topic+payload)
// PubSubClient::connect() is blocking and defaults to MQTT_SOCKET_TIMEOUT (15s).
// A stuck connect therefore freezes the whole UI, so cap it much lower.
constexpr uint16_t kConnectTimeoutSecs  = 5;
constexpr uint16_t kTlsPort             = 8883;    // legacy; no TLS client here
constexpr uint16_t kPlainPort           = 1883;

const RhinoConfig *s_cfg      = nullptr;
uint32_t           s_myNodeId = 0;
MqttInjectFn       s_inject   = nullptr;

WiFiClient       s_plain;
PubSubClient     s_mqtt;

uint16_t s_activePort     = 0;    // port actually used (may differ from cfg)
uint32_t s_lastAttemptMs  = 0;
uint32_t s_backoffMs      = kMinBackoffMs;
bool     s_configured     = false;   // transport/server pushed into s_mqtt

void gatewayId(char *out, size_t cap) {
    snprintf(out, cap, "!%08lx", (unsigned long)s_myNodeId);
}

// Downlink: decode a received ServiceEnvelope and hand it to the app sink.
void onMessage(char *topic, uint8_t *payload, unsigned int len) {
    if (!s_inject) return;
    MeshHdr hdr;
    uint8_t cipher[240];
    size_t  cipherLen = 0;
    char    chan[16];
    bool decoded = decodeServiceEnvelope(payload, len, hdr, cipher, sizeof(cipher),
                                         cipherLen, chan, sizeof(chan));
    // Serial.printf("[mqtt] downlink rx topic=%s len=%u decoded=%d chan=%s from=%08lx\n",
    //               topic ? topic : "(null)", len, decoded ? 1 : 0,
    //               decoded ? chan : "?", decoded ? (unsigned long)hdr.from : 0UL);
    if (decoded) s_inject(hdr, cipher, cipherLen, chan);
}

void applyTransport() {
    // Plaintext only. TLS was removed from this firmware: the handshake needs
    // ~40KB of contiguous internal heap, which these boards can't reliably spare,
    // and channel payloads are already end-to-end encrypted with the channel key.
    s_mqtt.setClient(s_plain);

    // Configs saved before TLS was removed still hold 8883. Connecting in the
    // clear to a TLS port gets no CONNACK, and the blocking connect() then stalls
    // the main loop for its whole timeout — which presents as the firmware
    // hanging. Migrate to the plaintext port rather than stalling.
    s_activePort = s_cfg->mqttPort;
    if (s_activePort == kTlsPort) {
        debugLogMessages("[mqtt] port %u is TLS-only; using %u (no TLS in firmware)\n",
                         (unsigned)kTlsPort, (unsigned)kPlainPort);
        s_activePort = kPlainPort;
    }

    s_mqtt.setServer(s_cfg->mqttServer, s_activePort);
    // Bound the blocking connect so a bad endpoint can't freeze the UI.
    s_mqtt.setSocketTimeout(kConnectTimeoutSecs);
    s_mqtt.setBufferSize(kMqttBufferBytes);
    s_mqtt.setCallback(onMessage);
    s_configured = true;
}

void attemptConnect(uint32_t now) {
    if (!s_configured) applyTransport();

    char clientId[16];
    gatewayId(clientId, sizeof(clientId));

    bool ok = s_mqtt.connect(clientId,
                             s_cfg->mqttUser[0] ? s_cfg->mqttUser : nullptr,
                             s_cfg->mqttPass[0] ? s_cfg->mqttPass : nullptr);

    if (ok) {
        s_backoffMs = kMinBackoffMs;
        debugLogMessages("[mqtt] connected %s:%u\n", s_cfg->mqttServer, (unsigned)s_activePort);
        if (s_inject) {
            char topic[80];
            snprintf(topic, sizeof(topic), "%s/2/e/#", s_cfg->mqttRoot);
            s_mqtt.subscribe(topic);
        }
    } else {
        s_backoffMs = s_backoffMs * 2 < kMaxBackoffMs ? s_backoffMs * 2 : kMaxBackoffMs;
        debugLogMessages("[mqtt] connect failed state=%d, backoff=%lums\n",
                         s_mqtt.state(), (unsigned long)s_backoffMs);
    }
    s_lastAttemptMs = now;
}
} // namespace

// ── Public API ────────────────────────────────────────────────
void mqttBridgeBegin(const RhinoConfig *cfg, uint32_t myNodeId) {
    s_cfg = cfg;
    s_myNodeId = myNodeId;
}

void mqttBridgeSetInject(MqttInjectFn fn) { s_inject = fn; }

void mqttBridgeConfigChanged() {
    if (s_mqtt.connected()) s_mqtt.disconnect();
    s_configured = false;      // re-push server/transport on next attempt
    s_backoffMs  = kMinBackoffMs;
    s_lastAttemptMs = 0;
}

bool mqttBridgeConnected() { return s_mqtt.connected(); }

void mqttBridgeLoop(uint32_t nowMs) {
    if (!s_cfg || !s_cfg->wifiEnabled || !s_cfg->mqttEnabled || s_myNodeId == 0) {
        if (s_mqtt.connected()) s_mqtt.disconnect();
        return;
    }
    if (!s_cfg->wifiSsid[0]) return;   // no station credentials configured

    if (WiFi.status() != WL_CONNECTED) return;  // station link managed by main loop

    if (s_mqtt.connected()) {
        s_mqtt.loop();
        return;
    }

    if (s_lastAttemptMs != 0 && (nowMs - s_lastAttemptMs) < s_backoffMs) return;
    attemptConnect(nowMs);
}

namespace {
// Shared envelope publish used by both the RX (heard) and TX (self) uplink paths.
void publishEnvelope(const MeshHdr &hdr, const uint8_t *cipher, size_t cipherLen,
                     float snr, int32_t rssi, const char *chanName) {
    if (!s_mqtt.connected() || !chanName || !chanName[0] || cipherLen == 0) return;

    char gw[16];
    gatewayId(gw, sizeof(gw));

    uint8_t env[400];
    size_t n = encodeServiceEnvelope(hdr, cipher, cipherLen, snr, rssi, /*rxTime=*/0,
                                     chanName, gw, env, sizeof(env));
    if (n == 0) {
        Serial.printf("[mqtt] uplink DROP: envelope too big (chan=%s cipher=%u)\n",
                      chanName, (unsigned)cipherLen);
        return;
    }

    char topic[96];
    snprintf(topic, sizeof(topic), "%s/2/e/%s/%s", s_cfg->mqttRoot, chanName, gw);
    bool ok = s_mqtt.publish(topic, env, n);
    Serial.printf("[mqtt] uplink %s topic=%s len=%u\n",
                  ok ? "OK" : "FAIL", topic, (unsigned)n);
}
} // namespace

void mqttBridgePublish(const MeshPacket &pkt, const char *chanName) {
    if (pkt.rawLen == 0) return;   // no raw ciphertext to forward
    publishEnvelope(pkt.hdr, pkt.rawCipher, pkt.rawLen,
                    pkt.snr, (int32_t)pkt.rssi, chanName);
}

void mqttBridgePublishRaw(const MeshHdr &hdr, const uint8_t *cipher, size_t cipherLen,
                          const char *chanName) {
    // Self-originated frame: no RX metadata (snr/rssi = 0).
    publishEnvelope(hdr, cipher, cipherLen, 0.0f, 0, chanName);
}
