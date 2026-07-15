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

// Re-read config and drop any existing session so the next loop reconnects with
// the new server/port/credentials. Call after the user edits MQTT settings.
void mqttBridgeConfigChanged();

// Downlink sink: the bridge calls this with a decoded ServiceEnvelope so the app
// can display and/or rebroadcast it. Set to nullptr (default) to ignore downlink.
typedef void (*MqttInjectFn)(const MeshHdr &hdr, const uint8_t *cipher,
                             size_t cipherLen, const char *chanName);
void mqttBridgeSetInject(MqttInjectFn fn);
