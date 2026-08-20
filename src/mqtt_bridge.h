#pragma once
// Native MQTT bridge: mirrors LoRa traffic to/from a Meshtastic MQTT broker.
//
// Scope: non-blocking WiFi-STA + MQTT connection lifecycle, uplink (publish
// packets heard on LoRa as ServiceEnvelopes), and downlink callback delivery via
// mqttBridgeSetInject() for app-side re-injection onto LoRa/UI handling.
//
// The bridge never blocks the main loop: mqttBridgeLoop() drives a small state
// machine and returns immediately. WiFi mode is left to the caller (main owns
// the AP/STA state machine); the bridge only requests an association and uses
// whatever STA link is up, so it coexists with the AP web-config portal.
#include <Arduino.h>
#include "config_io.h"
#include "mesh_proto.h"

// Capture the live config + our node id. Call once from setup() after config load.
void mqttBridgeBegin(const RhinoConfig *cfg, uint32_t myNodeId);

// Pump the connection state machine and service the MQTT client. Call every loop.
void mqttBridgeLoop(uint32_t nowMs);

// True when the broker session is established.
bool mqttBridgeConnected();

// Publish one packet heard on LoRa to the broker (uplink). No-op unless
// connected and the packet is on a real, named channel. `chanName` is the
// channel_id; cipher/cipherLen is the raw on-air ciphertext (pkt.rawCipher).
void mqttBridgePublish(const MeshPacket &pkt, const char *chanName);

// Uplink a self-originated frame (our own TX). hdr/cipher are the on-air header
// and ciphertext just handed to the radio; chanName is the channel_id. Gate the
// call on the channel's uplinkEnabled flag. No-op unless the bridge is connected.
void mqttBridgePublishRaw(const MeshHdr &hdr, const uint8_t *cipher, size_t cipherLen,
                          const char *chanName);

// Publish one MapReport to "<root>/2/map/" — the node describing itself to an
// MQTT-fed map. Nothing about this goes on the air: it is not a mesh packet
// mirrored upward but a message that only ever exists on the broker, which is
// why it takes a MapReportInfo rather than a frame. No-op (returns false)
// unless the bridge is connected and map reporting is enabled in config.
// chanName is the envelope's channel_id; pass the primary channel's name.
bool mqttBridgePublishMapReport(const MapReportInfo &info, const char *chanName);

// Re-read config and drop any existing session so the next loop reconnects with
// the new server/port/credentials. Call after the user edits MQTT settings.
void mqttBridgeConfigChanged();

// Downlink sink: the bridge calls this with a decoded ServiceEnvelope so the app
// can display and/or rebroadcast it. Set to nullptr (default) to ignore downlink.
typedef void (*MqttInjectFn)(const MeshHdr &hdr, const uint8_t *cipher,
                             size_t cipherLen, const char *chanName);
void mqttBridgeSetInject(MqttInjectFn fn);

// ── Topic monitor ─────────────────────────────────────────────
// A live census of what is arriving under <root>/2/e/#: one row per channel and
// how many messages landed on it. Deliberately not a message log — payloads are
// counted and dropped, never kept.
//
// Rows are the channel, not the whole topic. A Meshtastic envelope topic is
// "<root>/2/e/<channel>/<gateway>", so counting whole topics would give one row
// per gateway per channel — three gateways on KAM-NET are three rows saying the
// same thing. The gateway is dropped and the counts merge:
//     msh/US/MI/2/e/KAM-NET/!699c90c8  ┐
//     msh/US/MI/2/e/KAM-NET/!699c9234  ┴─→  KAM-NET   2
//     msh/US/MI/2/e/CFW/!b2a77a48      ───→  CFW      1
//
// Bounded on both axes so the cost of the screen does not depend on how long it
// is left up. The table is capacity-capped (channels past the cap are counted in
// bulk as "other" rather than stored) and every counter saturates instead of
// wrapping, so an all-day session uses exactly the RAM a one-second session
// does. The table is allocated on start and freed on stop, so a closed screen
// costs nothing at all, and nothing survives the stop: reopening counts fresh.

// Room for a channel name. Meshtastic channel ids run well under this; the cap
// only bites on a topic that did not carry the expected prefix, which is kept
// whole (and truncated) so it is still recognizable.
#define MQTT_MONITOR_TOPIC_MAX 32

// Collapsing gateways into their channel keeps this list short on its own — a
// root with 32 distinct channels is already unusual, and the whole table is
// ~1.2 KB while the screen is up.
#define MQTT_MONITOR_TOPIC_SLOTS 32

struct MqttTopicStat {
    uint32_t count;                          // messages on this channel, saturating
    char     channel[MQTT_MONITOR_TOPIC_MAX];
};

// Saturation ceiling for every counter here. Well past any plausible session on
// a busy broker, and low enough that the formatted number always fits a row.
static constexpr uint32_t kMqttMonitorCountMax = 999999UL;

// Begin (or continue) counting. False when the table could not be allocated, in
// which case the monitor stays inactive. Safe to call when already active.
bool mqttMonitorStart();

// Stop counting and release the table. Safe to call when not active.
void mqttMonitorStop();

bool mqttMonitorActive();

// Distinct channels currently held, and read access to them in first-seen order
// — a stable order, so a screen only has to rewrite the numbers.
int mqttMonitorTopicCount();
const MqttTopicStat *mqttMonitorTopicAt(int index);

// Every message seen since the start, including ones whose channel did not fit.
uint32_t mqttMonitorTotalMsgs();

// Messages that arrived on a channel past the table's capacity. Non-zero means
// the list on screen is a partial view.
uint32_t mqttMonitorOtherMsgs();

// Bumped whenever a channel is added, i.e. whenever the *list* (as opposed to
// the counts on it) changed. Lets a screen skip the rebuild and just retally.
uint32_t mqttMonitorTopicSeq();
