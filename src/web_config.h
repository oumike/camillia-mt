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

// Milliseconds since the last HTTP request, or UINT32_MAX when the server is
// not running. The clock starts when the server comes up rather than at the
// first request, because someone who has just switched web config on is about
// to open the page — that window is exactly as busy as one full of requests.
//
// Callers use it to keep multi-second work off the main loop while a browser is
// waiting on it: webCfgLoop() only runs once per loop pass, so anything that
// blocks in that pass is a request nobody answers. See the announce holds in
// main_lvgl.cpp.
uint32_t webCfgMsSinceRequest();

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

// ── Store & Forward replay bridge ─────────────────────────────────
// A Store-and-Forward router only replays history when a client asks for it, so
// the web UI needs a way to ask. The button queues the request here; the main
// loop drains it (it owns the LoRa TX path and the router-tracking state) and
// posts the outcome back, which the next page render shows. Same queue-and-drain
// shape as the Chat tab send above, for the same reason.
void webCfgQueueSnfRequest();
bool webCfgTakeSnfRequest();
void webCfgSetSnfResult(const char *msg);
const char *webCfgSnfResult();   // "" until a request has been attempted

// Pending "set the clock to this" request from the config form, drained on the
// main loop where the system clock is owned. Fields are local wall-clock time in
// 24-hour form. Returns false when nothing is queued.
bool webCfgTakeManualTime(int &year, int &mon, int &day, int &hour, int &minute);

#if HAS_VNC_HOST
// Remote-tab checkbox request, queued by the HTTP handler and applied by the
// main loop that owns VNC allocation and persistence.
bool webCfgTakeVncToggle(bool &enabled);
#endif

// True if the server is running in first-boot WiFi onboarding mode.
bool webCfgIsOnboarding();

// True while the server is running as "web config lite" — the SoftAP variant,
// serving the Config tab only. False for the full config served over STA.
bool webCfgIsLite();

// Called roughly every 100 ms while webCfgBegin() blocks waiting for the
// station to associate — up to ten seconds, and the longest single step of a
// boot. Lets a caller that owns the display keep a progress indicator moving
// through it. Optional; pass null to disable.
void webCfgSetWaitCb(void (*cb)());

// The station credentials webCfgBegin() should associate with. The device may
// be joined to a network picked in the on-device WiFi list rather than the one
// stored in RhinoConfig, and that choice lives in the UI layer — so it has to be
// pushed here before starting, alongside webCfgSetForceAp(). Passing null or an
// empty ssid clears the override and falls back to the stored credentials.
//
// Without this, turning web config on while joined to a picked network dials the
// *stored* SSID instead, times out, and drops to AP fallback — taking the device
// off the network it was just reachable on.
void webCfgSetStaCreds(const char *ssid, const char *pass);

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

// Forgets the cached credentials above. persistConfigToPrefs() folds them back
// into the settings blob whenever they are non-empty, so anything that deletes
// the configured network has to clear them here too or the next save restores
// what was just removed.
void webCfgClearWifiCreds();

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
