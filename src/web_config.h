#pragma once
// Embedded web configuration server lifecycle and state/query APIs.
#include "config_io.h"

// Called by the web server after it writes new values into *cfg.
typedef void (*WebCfgSaveCb)();

// Connect to the configured WiFi network and start the HTTP config server.
// cfg    — live config struct; the server reads and writes nodeLong/nodeShort.
// onSave — called on the main thread after a successful /save POST.
// Returns true on success; false if the network is unreachable (timeout ~10 s).
bool webCfgBegin(RhinoConfig *cfg, WebCfgSaveCb onSave);

// Stop HTTP server and bring down WiFi.
void webCfgEnd();

// Must be called from loop() while the server is running.
void webCfgLoop();

// True while the server is active.
bool webCfgRunning();

// DHCP-assigned IP address string — valid only while running, empty otherwise.
const char *webCfgIP();

// Returns true (and clears the flag) if the web UI requested a NODEINFO broadcast.
bool webCfgAnnounceRequested();

// True if the server is running in first-boot WiFi onboarding mode.
bool webCfgIsOnboarding();

// Current WiFi credentials (updated by web UI save, used by NVS save callback)
const char *webCfgWifiSsid();
const char *webCfgWifiPass();
