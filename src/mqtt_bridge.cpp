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

// ── Topic monitor state ───────────────────────────────────────
// Allocated only while a screen is watching; see mqttMonitorStart().
MqttTopicStat *s_monTable     = nullptr;
uint32_t      *s_monHashes    = nullptr;   // parallel to s_monTable, see monRecord()
int            s_monCount     = 0;
uint32_t       s_monTotal     = 0;
uint32_t       s_monOther     = 0;
uint32_t       s_monTopicSeq  = 0;

inline void monBump(uint32_t &c) {
    if (c < kMqttMonitorCountMax) c++;
}

uint32_t monHash(const char *s) {
    uint32_t h = 2166136261UL;                 // FNV-1a
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619UL;
    return h;
}

// "<root>/2/e/<channel>/<gateway>" reduced to "<channel>". The root is the same
// on every message and the gateway is the thing being merged away, so what is
// left is the one field the row is about. A topic with no envelope marker at all
// falls back to its own first segment, which is wrong in an obvious way on
// screen rather than wrong in a way that looks like data.
void monTopicKey(const char *topic, char *out, size_t outLen) {
    // Anchored on the envelope marker rather than on the configured root string.
    // "/2/e/" is the structural part of the topic and cannot move; matching the
    // root by text would key every message on "msh" the moment the root carried
    // a stray trailing slash, or was edited while the screen was up.
    const char *start = strstr(topic, "/2/e/");
    start = start ? start + 5 : topic;

    // Up to the gateway separator, or the whole remainder when there is none.
    size_t n = 0;
    while (start[n] && start[n] != '/' && n + 1 < outLen) n++;
    memcpy(out, start, n);
    out[n] = '\0';
}

void monRecord(const char *topic) {
    if (!s_monTable || !topic || !topic[0]) return;

    monBump(s_monTotal);

    char key[MQTT_MONITOR_TOPIC_MAX];
    monTopicKey(topic, key, sizeof(key));
    if (!key[0]) return;                       // nothing between the separators
    const uint32_t h = monHash(key);

    // Hash first, strcmp only on a hit: a busy broker can push hundreds of
    // messages a second through here, and this runs on the main loop.
    for (int i = 0; i < s_monCount; i++) {
        if (s_monHashes[i] != h) continue;
        if (strcmp(s_monTable[i].channel, key) != 0) continue;
        monBump(s_monTable[i].count);
        return;
    }

    if (s_monCount >= MQTT_MONITOR_TOPIC_SLOTS) {
        // Table full. New channels are counted in bulk rather than evicting a row
        // the user is watching — this screen is about what is on the air, and a
        // list that reshuffles itself is not readable.
        monBump(s_monOther);
        return;
    }

    MqttTopicStat &e = s_monTable[s_monCount];
    memcpy(e.channel, key, sizeof(key));
    e.count = 1;
    s_monHashes[s_monCount] = h;
    s_monCount++;
    s_monTopicSeq++;
}

// Downlink: decode a received ServiceEnvelope and hand it to the app sink.
void onMessage(char *topic, uint8_t *payload, unsigned int len) {
    // Counted before the inject gate: the monitor is interested in every message
    // that lands, including ones no sink wants.
    if (s_monTable && topic) monRecord(topic);

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

// Subscribe to the configured root's envelope wildcard. Both the downlink sink
// and the topic monitor want exactly this filter, so a re-subscribe for the
// second one is a no-op at the broker (MQTT replaces a filter, it does not
// stack it) and no message is delivered twice.
void subscribeRoot() {
    if (!s_cfg || !s_mqtt.connected()) return;
    if (!s_inject && !s_monTable) return;      // nobody is listening
    char topic[80];
    snprintf(topic, sizeof(topic), "%s/2/e/#", s_cfg->mqttRoot);
    s_mqtt.subscribe(topic);
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
        subscribeRoot();
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

// ── Topic monitor ─────────────────────────────────────────────
bool mqttMonitorStart() {
    if (s_monTable) return true;               // already counting

    // One allocation for the rows, one for the hash sidecar. Both are freed on
    // stop; nothing here is touched while the monitor is off.
    s_monTable = (MqttTopicStat *)calloc(MQTT_MONITOR_TOPIC_SLOTS, sizeof(MqttTopicStat));
    if (!s_monTable) return false;
    s_monHashes = (uint32_t *)calloc(MQTT_MONITOR_TOPIC_SLOTS, sizeof(uint32_t));
    if (!s_monHashes) {
        free(s_monTable);
        s_monTable = nullptr;
        return false;
    }

    s_monCount = 0;
    s_monTotal = 0;
    s_monOther = 0;
    s_monTopicSeq++;

    // The bridge subscribes on connect, but only when it connected with a
    // listener; ask again in case this monitor is the first one.
    subscribeRoot();
    return true;
}

void mqttMonitorStop() {
    if (!s_monTable) return;
    free(s_monTable);
    free(s_monHashes);
    s_monTable  = nullptr;
    s_monHashes = nullptr;
    s_monCount  = 0;
    s_monTotal  = 0;
    s_monOther  = 0;
    s_monTopicSeq++;
    // The subscription itself stays: the downlink sink wants the same filter,
    // and dropping it here would silence the bridge.
}

bool mqttMonitorActive() { return s_monTable != nullptr; }

int mqttMonitorTopicCount() { return s_monCount; }

const MqttTopicStat *mqttMonitorTopicAt(int index) {
    if (!s_monTable || index < 0 || index >= s_monCount) return nullptr;
    return &s_monTable[index];
}

uint32_t mqttMonitorTotalMsgs() { return s_monTotal; }
uint32_t mqttMonitorOtherMsgs() { return s_monOther; }
uint32_t mqttMonitorTopicSeq() { return s_monTopicSeq; }

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

bool mqttBridgePublishMapReport(const MapReportInfo &info, const char *chanName) {
    // Gated here as well as at the caller: this is the one publish that is not a
    // mirror of something the mesh already saw, so "map reporting is off" has to
    // mean nothing leaves, no matter who calls.
    if (!s_cfg || !s_cfg->mqttMapReport) return false;
    if (!s_mqtt.connected()) return false;

    char gw[16];
    gatewayId(gw, sizeof(gw));

    uint8_t env[400];
    const size_t n = encodeMapReportEnvelope(s_myNodeId, info,
                                             (chanName && chanName[0]) ? chanName : "",
                                             gw, env, sizeof(env));
    if (n == 0) {
        Serial.println("[mqtt] map report DROP: envelope too big");
        return false;
    }

    // Trailing slash and no per-channel or per-gateway segment: unlike /2/e/,
    // the map topic is a single well-known sink that consumers subscribe to
    // whole. The gateway id travels inside the envelope instead.
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/2/map/", s_cfg->mqttRoot);
    const bool ok = s_mqtt.publish(topic, env, n);
    Serial.printf("[mqtt] map report %s topic=%s len=%u\n",
                  ok ? "OK" : "FAIL", topic, (unsigned)n);
    return ok;
}
