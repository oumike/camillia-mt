#include "web_config.h"
#include "community_presets.h"
#include "ble_keyboard.h"   // coexistence holds Wi-Fi modem sleep on
#include "keyboard.h"       // keyboardSetKeypadBacklight(), applied on save
#if HAS_VNC_HOST
#include "vnc_host.h"
#endif
#include "mesh_channel_plan.h"
#include "licensed_mode.h"
#include "mesh_beacon.h"  // clampBeaconIntervalH(), beaconCopyMessage()
#include "base64_util.h"
#include "mesh_proto.h"   // myPubKey, for the identity chip in the header
#include "admin_peers.h"
#include "admin_client.h"   // the help text, which is the firmware's, not the page's
#include "web_icon.h"
#include "web_app_assets.h"   // kAppCss, kAppJs, kAppFontsLink
#include "node_db.h"
#include "wardrive_log.h"
#include "channel_mgr.h"
#include "dm_mgr.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "storage.h"
#include "state_maps.h"
#include "map_tiles.h"
#include "env_sensor.h"
#include "i18n.h"         // the Language select: kUiLangNames / kUiLangCodes
#include "user_msg_colors.h"   // the My Message Color select
#include <esp_heap_caps.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>   // clientWritable(): poll the socket before writing
#include <sys/socket.h>   // writeAllRaw(): bounded nonblocking socket writes
#include "debug_flags.h"
#include "gps.h"
#include "ota_update.h"   // otaLayoutSupportsUpdate(), otaInstalledByLauncher()
#include "battery_util.h"
#include "utf8_utils.h"
#if defined(DEVICE_TDISPLAY_P4)
#include "hal/tdisplay_p4_io.h"
#include <esp32-hal-hosted.h>
#endif

static const uint32_t kConnectTimeout  = 10000;  // ms
static const uint32_t kReleaseCheckTimeoutMs = 7000;
static const uint32_t kWebTlsMinInternalFree = 70000;
static const uint32_t kWebTlsMinLargestBlock = 52000;
static const bool kWebDeviceReleaseCheckEnabled = false;
static const char    *kLatestReleaseApiUrl = "https://api.github.com/repos/oumike/camillia-mt/releases/latest";
static const char    *kLatestVersionRawUrl = "https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION";
static const char    *kLatestReleasePageUrl = "https://github.com/oumike/camillia-mt/releases/latest";

static const char    *kUser            = "admin";
static const char    *kDefaultWebPass  = "admin";

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

// The CardputerZero shares its host with other services, so the web config
// sits on an unprivileged port of its own; ipBuf then carries "ip:port" so
// every place that shows the address shows a URL that works.
#if defined(PLATFORM_LINUX)
static constexpr uint16_t kWebCfgPort = 18765;
#else
static constexpr uint16_t kWebCfgPort = 80;
#endif
static WebServer      server(kWebCfgPort);
// The CardputerZero keeps the node archive (HAS_NODE_ARCHIVE) with no SD slot:
// the file is on its own disk, so the pages talk about storage, not a card.
#if HAS_NODE_ARCHIVE && !HAS_SD_CARD
#define NODE_ARCHIVE_ON_DISK 1
#else
#define NODE_ARCHIVE_ON_DISK 0
#endif
// No captive portal: the AP runs no DNS server and never redirects the OS's
// portal-detection probes, so phones don't pop a captive sheet on join. Browse
// to the AP IP directly.
static bool           running          = false;
static bool           gOnboarding      = false;
// ── Idle timeout ─────────────────────────────────────────────────────────────
// Web config has to run Wi-Fi with modem power-save disabled (see the
// WiFi.setSleep(false) calls below), which makes it one of the largest
// continuous draws on the device — and nothing ever stopped it except the user
// remembering to. Stamped on every request; webCfgLoop() raises gIdleExpired
// once the window passes and the main loop performs the actual teardown, so
// the shutdown path stays the same one the manual toggle uses.
static uint32_t       gLastRequestMs   = 0;
// Cleared on every start/stop, set on the first webCfgLoop() pass after the
// server comes up. Everything that asks "how long since the page was busy" or
// "how long has the socket gone unserviced" has to measure from there, not from
// webCfgBegin(): the rest of setup() — the node DB, the radio, GPS, LVGL — runs
// before loop() does, and for that whole stretch the server is listening but
// single-threadedly unable to answer anyone.
static uint32_t       gFirstServiceMs  = 0;
static uint32_t       gIdleTimeoutMs   = 0;     // 0 = never expire
// Set when the last webCfgBegin() gave up waiting for the station rather than
// fall back to the SoftAP; see fallBackToAp in webCfgBegin().
static bool           gStartDeferred   = false;
static bool           gIdleExpired     = false;
// True while the server is hosting its own SoftAP. On boards without PSRAM that
// means web config lite (Config tab only): AP mode leaves them far less
// internal heap than STA, and the heavy tabs are what tip it over. Boards with
// PSRAM serve the full page here too; see apHasRoomForFullPage().
static bool           gApMode          = false;
// Set from the on-device WiFi picker's "AP" entry: start the SoftAP even when
// credentials are saved, instead of joining the network.
static bool           gForceAp         = false;
// Set from the on-device WiFi picker's network entries, via webCfgSetStaCreds().
// Empty ssid means "no override" — use the credentials stored in RhinoConfig.
// The picker's choice is not written back into the config (that would overwrite
// the user's configured default), so it has to reach us this way or we would
// dial the wrong network every time web config starts.
static char           gStaSsidOverride[64] = {};
static char           gStaPassOverride[64] = {};
// Pumped while the station-connect wait below blocks; see webCfgSetWaitCb().
static void         (*gWaitCb)()           = nullptr;

// Boards that always serve web config lite, regardless of radio mode. The
// Cardputer has no PSRAM and ~23 KB of internal heap once Wi-Fi is up, which the
// full page's Live/Nodes/Utilities panes and their JS cannot fit: writes crawl
// at a few hundred bytes per 300 ms because the stack can't get TX buffers.
// Lite carries the entire Config pane, so nothing configurable is lost.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
static const bool     kLiteOnlyBoard   = true;
#else
static const bool     kLiteOnlyBoard   = false;
#endif

// The web config is split into src/web_config/*.inc, pasted in here in order:
// they compile as this one file, each seeing everything declared above it.
// New code goes in the fragment for its page or API, or a new one listed here.
#include "web_config/01_server_state.inc"
#include "web_config/02_theme_head.inc"
#include "web_config/03_send_uploads.inc"
#include "web_config/04_form_helpers.inc"
#include "web_config/05_config_page.inc"
#include "web_config/06_onboarding_save.inc"
#include "web_config/07_api_ui.inc"
#include "web_config/08_api_data.inc"
#include "web_config/09_routes.inc"

void webCfgEnd() {
    if (!running) return;
    gIdleExpired    = false;
    gIdleTimeoutMs  = 0;
    // Re-armed here so the next start measures its own clocks; see webCfgLoop().
    gFirstServiceMs = 0;
    sessionToken[0] = '\0';
    gFlashMsg[0] = '\0';
    // A queued licensed-mode transition or reboot is carried out, not dropped:
    // the save that queued it has already been answered as done.
    webCfgRunPending(true);
    clearReleaseCheckResult();
    gReleaseCheckState = RELEASE_CHECK_IDLE;
    server.stop();

    // Keep the station association when the device wants WiFi anyway. Web
    // config used to power the radio down unconditionally here, on the basis
    // that it had brought the radio up — but when it merely *joined* the
    // network the device was going to be on regardless, dropping the
    // association only forces serviceWifiStation() to rebuild it, which costs a
    // full reassociation (and an MQTT/NTP outage) for nothing.
    //
    // Not kept when we were hosting the SoftAP (that has to come down, and the
    // station was never up), when WiFi is switched off in settings, or when
    // there is no association to preserve in the first place.
    const bool keepStation = gCfg && gCfg->wifiEnabled && !gApMode
                             && WiFi.status() == WL_CONNECTED;
    if (keepStation) {
        // The one radio setting the session actually changed. webCfgBegin()
        // turns modem power-save off because the synchronous WebServer stalls
        // on DTIM-buffered packets; that was only ever meant to last as long as
        // the server, and the old teardown "restored" it by killing the radio.
        WiFi.setSleep(true);
    } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    if (gWebBuffersReclaimed) {
        // Reallocate chat/DM buffers released in webCfgBegin() and reload
        // persisted history now that Wi-Fi/web RAM pressure is gone.
        Channels.restoreBuffers();
        DMs.clearAll(false);
        DMs.loadAll();
        gWebBuffersReclaimed = false;
    }

    ipBuf[0]    = '\0';
    gCfg        = nullptr;
    gOnSave     = nullptr;
    gOnScreenshotPng = nullptr;
    gAnnounceReq = false;
    gTelemetryReq = false;
    // Cancel any chat send the web UI queued but that the main loop hasn't
    // drained yet — serviceWebChatSend() only runs while the server is up, so a
    // leftover request would otherwise fire the next time it's started.
    gChatSendReq   = false;
    gChatSendText[0] = '\0';
#if HAS_VNC_HOST
    gVncToggleReq = false;
#endif
    running     = false;
    gOnboarding = false;
    gApMode     = false;
    Serial.printf("[web] stopped%s\n",
                  keepStation ? " (station connection kept)" : " (radio off)");
}

bool webCfgIsOnboarding() { return gOnboarding; }
bool webCfgIsLite() { return running && webCfgUseLite(); }
bool webCfgChatPaused() { return running && gWebBuffersReclaimed; }
void webCfgSetForceAp(bool force) { gForceAp = force; }

void webCfgSetWaitCb(void (*cb)()) { gWaitCb = cb; }

void webCfgSetStaCreds(const char *ssid, const char *pass) {
    if (!ssid || !ssid[0]) {
        gStaSsidOverride[0] = '\0';
        gStaPassOverride[0] = '\0';
        return;
    }
    strlcpy(gStaSsidOverride, ssid, sizeof(gStaSsidOverride));
    strlcpy(gStaPassOverride, pass ? pass : "", sizeof(gStaPassOverride));
}
const char *webCfgWifiSsid() { return gWifiSsid; }
const char *webCfgWifiPass() { return gWifiPass; }

void webCfgClearWifiCreds() {
    memset(gWifiSsid, 0, sizeof(gWifiSsid));
    memset(gWifiPass, 0, sizeof(gWifiPass));
}

void webCfgLoop() {
    if (!running) return;

    // How long the socket went unserviced. This function runs once per main
    // loop pass, so a gap here is the main loop having been busy elsewhere —
    // and nothing in the request path can tell the difference between "the
    // browser gave up" and "we never got round to answering it". The blocking
    // radio transmits are the ones worth catching: a NodeInfo announce at SF11
    // is seconds of airtime during which no HTTP request is looked at, which a
    // browser opening the page at that moment experiences as a dead server.
    {
        static uint32_t sLastServiceMs = 0;
        const uint32_t nowMs = millis();
        if (gFirstServiceMs == 0) {
            // First pass after the server came up. Two clocks start here rather
            // than in webCfgBegin(). The gap report, because the previous value
            // is from before the last shutdown — across a Config-screen disable
            // and re-enable that measured the user's own dwell time and printed
            // it as a stall. And gLastRequestMs, because the "someone is using
            // the page" window it feeds is meant to cover the moments a browser
            // could actually be talking to us; boot init is not one of them, and
            // spending the window there left the first announce unheld — which
            // is the whole problem this was meant to prevent.
            gFirstServiceMs = nowMs;
            gLastRequestMs = nowMs;
            sLastServiceMs = nowMs;
        } else {
            const uint32_t gap = nowMs - sLastServiceMs;
            if (gap >= 1000) {
                Serial.printf("[web] main loop was busy for %lu ms - socket unserviced\n",
                              (unsigned long)gap);
            }
            sLastServiceMs = nowMs;
        }
    }

    server.handleClient();

#if HAS_VNC_HOST
    // The VNC iframe streams through port 8765, so its traffic never reaches
    // WebServer's request handlers. Keep the full page alive while the host is
    // enabled; once VNC is turned off, the normal idle countdown starts here.
    if (vncHostEnabled()) {
        gLastRequestMs = millis();
        gIdleExpired = false;
    }
#endif

    // Onboarding is exempt: the user may be reading the setup page for a while
    // before submitting anything, and pulling the AP out from under a
    // half-finished first-time setup is worse than the power it saves.
    if (gIdleTimeoutMs && !gIdleExpired && !gOnboarding
        && (uint32_t)(millis() - gLastRequestMs) >= gIdleTimeoutMs) {
        gIdleExpired = true;
        Serial.printf("[web] idle %lus with no request - requesting shutdown\n",
                      (unsigned long)(gIdleTimeoutMs / 1000UL));
    }
    webCfgRunPending(false);
}

// The licensed-mode transition and reboot queued by a handler. Normally run by
// webCfgLoop() once their delay has passed; webCfgEnd() runs them at once
// (now = true), as stopping the server must not cancel them.
static void webCfgRunPending(bool now) {
#if FEATURE_LICENSED_MODE
    if (gLicReq && (now || (int32_t)(millis() - gLicAtMs) >= 0)) {
        const uint8_t req = gLicReq;
        gLicReq = 0;
        server.stop();
        delay(100);
        if (req == 1) {
            char err[96];
            // Validated in handlePostSave(), so this reboots and does not return.
            if (!enterLicensedMode(gLicCall, gLicSuffix, gLicPwr, err, sizeof(err))) {
                Serial.printf("[web] licensed mode not entered: %s\n", err);
                scheduleReboot(0);   // the rest of the save still needs it
            }
        } else {
            exitLicensedMode();
        }
    }
#endif
    if (gRebootPending && (now || (int32_t)(millis() - gRebootAtMs) >= 0)) {
        gRebootPending = false;
        server.stop();
        delay(100);
        Channels.flushPersistence();
        DMs.flushPersistence();
        ESP.restart();
    }
}

bool webCfgRunning() { return running; }
bool webCfgStartDeferred() { return gStartDeferred; }

uint32_t webCfgMsSinceRequest() {
    if (!running) return 0xFFFFFFFFu;   // UINT32_MAX, spelled without the header
    return (uint32_t)(millis() - gLastRequestMs);
}

bool webCfgIdleExpired() { return gIdleExpired; }
const char *webCfgIP() { return ipBuf; }

bool webCfgAnnounceRequested() {
    if (!gAnnounceReq) return false;
    gAnnounceReq = false;
    return true;
}

void webCfgQueueAnnounce() {
    gAnnounceReq = true;
}

bool webCfgTelemetryRequested() {
    if (!gTelemetryReq) return false;
    gTelemetryReq = false;
    return true;
}

void webCfgQueueTelemetry() {
    gTelemetryReq = true;
}

void webCfgQueueChatSend(bool isDm, uint32_t targetId, const char *text,
                         uint32_t replyId, uint32_t emoji) {
    gChatSendIsDm    = isDm;
    gChatSendTarget  = targetId;
    gChatSendReplyId = replyId;
    gChatSendEmoji   = emoji;
    strlcpy(gChatSendText, text ? text : "", sizeof(gChatSendText));
    gChatSendReq     = true;
}

bool webCfgTakeChatSend(bool &isDm, uint32_t &targetId,
                        char *text, size_t textLen,
                        uint32_t &replyId, uint32_t &emoji) {
    if (!gChatSendReq) return false;
    gChatSendReq = false;
    isDm     = gChatSendIsDm;
    targetId = gChatSendTarget;
    replyId  = gChatSendReplyId;
    emoji    = gChatSendEmoji;
    if (text && textLen) strlcpy(text, gChatSendText, textLen);
    return true;
}

// ── camillia chat server bridge ───────────────────────────────────────────────
static bool     gCsSetPending = false;
static uint32_t gCsSetNode = 0;
static bool     gCsCheckPending = false;
static char     gCsResult[64] = "";

void webCfgQueueChatServerSet(uint32_t nodeId) { gCsSetNode = nodeId; gCsSetPending = true; }

bool webCfgTakeChatServerSet(uint32_t &nodeId) {
    if (!gCsSetPending) return false;
    gCsSetPending = false;
    nodeId = gCsSetNode;
    return true;
}

void webCfgQueueChatServerCheck() { gCsCheckPending = true; }

bool webCfgTakeChatServerCheck() {
    if (!gCsCheckPending) return false;
    gCsCheckPending = false;
    return true;
}

void webCfgSetChatServerResult(const char *msg) {
    strlcpy(gCsResult, msg ? msg : "", sizeof(gCsResult));
}

const char *webCfgChatServerResult() { return gCsResult; }

bool webCfgTakeCommunityPreset(int &idx) {
    if (gCommunityReq < 0 || (int32_t)(millis() - gCommunityAtMs) < 0) return false;
    idx = gCommunityReq;
    gCommunityReq = -1;
    return true;
}

bool webCfgTakeManualTime(int &year, int &mon, int &day, int &hour, int &minute) {
    if (!gManualTimeReq) return false;
    gManualTimeReq = false;
    year   = gManualTime[0];
    mon    = gManualTime[1];
    day    = gManualTime[2];
    hour   = gManualTime[3];
    minute = gManualTime[4];
    return true;
}

#if HAS_VNC_HOST
bool webCfgTakeVncToggle(bool &enabled) {
    if (!gVncToggleReq) return false;
    gVncToggleReq = false;
    enabled = gVncToggleOn;
    return true;
}
#endif
