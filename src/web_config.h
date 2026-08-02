#pragma once
// Embedded web configuration server lifecycle and state/query APIs.
#include "config_io.h"

// Called by the web server after it writes new values into *cfg.
typedef void (*WebCfgSaveCb)();
// Called by the web server to capture the current device screen as PNG.
// outPath points to a writable file path that should be replaced with fresh PNG data.
// Returns true on success.
typedef bool (*WebCfgScreenshotPngCb)(const char *outPath);

// Connect to the configured WiFi network and start the HTTP config server.
// cfg    — live config struct; the server reads and writes nodeLong/nodeShort.
// onSave — called on the main thread after a successful /save POST.
// Returns true on success; false if the network is unreachable (timeout ~10 s).
bool webCfgBegin(RhinoConfig *cfg, WebCfgSaveCb onSave,
				 WebCfgScreenshotPngCb onScreenshotPng = nullptr);

// Stop HTTP server and bring down WiFi.
void webCfgEnd();

// Must be called from loop() while the server is running.
void webCfgLoop();

// True while the server is active.
bool webCfgRunning();

// True once the server has gone webCfgIdleTimeoutS seconds with no HTTP
// request. The main loop polls this and performs the teardown itself, so the
// shutdown takes the same path as the manual toggle (radio down, station
// re-associated, buffers restored). Always false while onboarding.
bool webCfgIdleExpired();

// DHCP-assigned IP address string — valid only while running, empty otherwise.
const char *webCfgIP();

// Returns true (and clears the flag) if the web UI requested a NODEINFO broadcast.
bool webCfgAnnounceRequested();

// Queue a NODEINFO+position re-announce to be processed in the main loop.
// Also forces immediate telemetry broadcast when telemetry is enabled.
void webCfgQueueAnnounce();

// Returns true (and clears the flag) if the web UI requested telemetry TX.
bool webCfgTelemetryRequested();

// Queue immediate telemetry TX to be processed in the main loop.
// Sends device telemetry and environment telemetry when available.
void webCfgQueueTelemetry();

// ── Chat tab send bridge ──────────────────────────────────────────
// The Chat tab's POST handler queues a single pending send here; the main loop
// drains it (it owns the node id and the LoRa TX path). isDm selects the DM vs
// channel path; targetId is a node id (DM) or channel index (channel). emoji
// non-zero marks a tapback reaction.
void webCfgQueueChatSend(bool isDm, uint32_t targetId, const char *text,
                         uint32_t replyId, uint32_t emoji);
// If a chat send is pending, copy it out (clearing the slot) and return true.
bool webCfgTakeChatSend(bool &isDm, uint32_t &targetId,
                        char *text, size_t textLen,
                        uint32_t &replyId, uint32_t &emoji);

// Pending "set the clock to this" request from the config form, drained on the
// main loop where the system clock is owned. Fields are local wall-clock time in
// 24-hour form. Returns false when nothing is queued.
bool webCfgTakeManualTime(int &year, int &mon, int &day, int &hour, int &minute);

// True if the server is running in first-boot WiFi onboarding mode.
bool webCfgIsOnboarding();

// True while the server is running as "web config lite" — the SoftAP variant,
// serving the Config tab only. False for the full config served over STA.
bool webCfgIsLite();

// Force the next webCfgBegin() to bring up the SoftAP even when WiFi
// credentials are saved. Backs the "AP" entry in the on-device WiFi picker,
// which lets a user reach web config without joining their network.
void webCfgSetForceAp(bool force);

// True while the server is up AND this board had to free its chat/DM buffers to
// fit the WiFi stack (no-PSRAM boards). Messages are dropped, not queued, for
// the duration — callers should say so rather than let it look like a fault.
bool webCfgChatPaused();

// Current WiFi credentials (updated by web UI save, used by NVS save callback)
const char *webCfgWifiSsid();
const char *webCfgWifiPass();

// ── Live chart history snapshot API ───────────────────────────────
// Allows the web UI to render the same channel-utilization and SNR/RSSI
// sparklines as the on-device live feed (keyboard shortcuts u/s).
// values[] is filled oldest -> newest; only the first `count` entries are valid.
struct WebChartSnapshot {
    static constexpr int CAP = 60;
    float values[CAP];
    int count;
    bool hasLast;
    float lastVal;
};
void webChartSnapshotChUtil(WebChartSnapshot &out);
void webChartSnapshotAirUtil(WebChartSnapshot &out);
void webChartSnapshotSnr(WebChartSnapshot &out);
void webChartSnapshotRssi(WebChartSnapshot &out);
