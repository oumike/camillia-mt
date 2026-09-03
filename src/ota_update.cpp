#include "ota_update.h"

#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <ctype.h>
#include <string.h>
#include <type_traits>
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "ota_signing_pubkey.h"
#include "config_io.h"   // OTA_CHANNEL_* release channels

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static volatile uint32_t g_otaNetworkGate = 0;
// Stable unless a caller says otherwise, so a build that never wires the
// setting through behaves exactly as it did before channels existed.
static volatile uint8_t  g_otaChannel = OTA_CHANNEL_AUTO;
static constexpr uint32_t kOtaNetworkGateMagic = 0x4F544131UL; // "OTA1"

// Hardcoded OTA proxy base — intentionally NOT user-configurable, so the update
// source can't be repointed from the UI/config. This firmware has no TLS client,
// so the endpoint must be http://; authenticity comes from the ECDSA signature
// verified against the baked-in public key before commit, not from transport
// security. To change it, edit here and reflash.
static const char s_otaBaseUrl[] = "http://ota.camillia.sumat.org";

namespace {
constexpr uint32_t kReleaseCheckTimeoutMs = 12000;

// Temporary behavior for OTA testing: any successfully-fetched latest release
// is treated as installable even when equal to current APP_VERSION.
constexpr bool kTreatLatestAsUpdateForTesting = false;


static void preferExternalHeapForOta() {
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
    static bool enabled = false;
    if (!enabled) {
        // Route generic malloc allocations to PSRAM where possible, leaving
        // contiguous internal heap free for the download/flash path.
        heap_caps_malloc_extmem_enable(0);
        enabled = true;
    }
#endif
}

static void clearCheckResult(OtaCheckResult &out) {
    memset(&out, 0, sizeof(out));
}

static void copyStringToBuf(char *dst, size_t dstLen, const char *src) {
    if (!dst || dstLen == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstLen - 1);
    dst[dstLen - 1] = '\0';
}

static bool extractJsonStringField(const String &json, const char *field, String &valueOut) {
    valueOut = "";
    if (!field || !field[0]) return false;

    String key = String("\"") + field + "\"";
    int keyPos = json.indexOf(key);
    if (keyPos < 0) return false;

    int colonPos = json.indexOf(':', keyPos + key.length());
    if (colonPos < 0) return false;

    int q1 = json.indexOf('"', colonPos + 1);
    if (q1 < 0) return false;

    String out = "";
    bool esc = false;
    for (int i = q1 + 1; i < (int)json.length(); i++) {
        char c = json[i];
        if (esc) {
            switch (c) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += c; break;
            }
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') {
            valueOut = out;
            return true;
        }
        out += c;
    }

    return false;
}

static bool httpGetString(const char *url,
                          String &bodyOut,
                          String &errOut,
                          bool followRedirects,
                          int *statusOut = nullptr,
                          String *locationOut = nullptr,
                          const char *acceptHeader = nullptr) {
    preferExternalHeapForOta();

    bodyOut = "";
    errOut = "";
    if (statusOut) *statusOut = 0;
    if (locationOut) *locationOut = "";

    // Plain HTTP only — this firmware has no TLS client.
    WiFiClient plainClient;
    HTTPClient http;
    if (!http.begin(plainClient, url)) {
        errOut = "Failed to start request";
        return false;
    }

    http.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
    http.addHeader("User-Agent", "camillia-mt-ota");
    if (acceptHeader && acceptHeader[0]) {
        http.addHeader("Accept", acceptHeader);
    }
    http.setFollowRedirects(followRedirects ? HTTPC_STRICT_FOLLOW_REDIRECTS
                                            : HTTPC_DISABLE_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (statusOut) *statusOut = code;
    if (locationOut) *locationOut = http.getLocation();

    if (code <= 0) {
        errOut = String("Network error (") + String(code) + ")";
        http.end();
        return false;
    }

    if (code != HTTP_CODE_OK) {
        errOut = String("HTTP ") + String(code);
        http.end();
        return false;
    }

    bodyOut = http.getString();
    http.end();
    return true;
}

static int parseNextVersionNumber(const char *s, int &idx) {
    if (!s) return -1;
    while (s[idx] && !isdigit((unsigned char)s[idx])) idx++;
    if (!s[idx]) return -1;

    int val = 0;
    while (isdigit((unsigned char)s[idx])) {
        val = val * 10 + (s[idx] - '0');
        idx++;
    }
    return val;
}

static bool isPrereleaseTag(const char *tag);

// Compares the numeric components, then breaks a tie the SemVer way: a
// prerelease sorts *below* the release it leads to, so v4.7.8-alpha.3 < v4.7.8.
//
// That tail matters to a device on the alpha channel. Without it the numeric
// scan reads v4.7.8-alpha.3 as "4.7.8.3" and the finished v4.7.8 as "4.7.8.0",
// making the stable release look older — so a tester who had run every alpha of
// a version would be stranded on the last one and never offered the real thing.
static int compareVersionTags(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    int ia = 0;
    int ib = 0;

    const bool aPre = isPrereleaseTag(a);
    const bool bPre = isPrereleaseTag(b);
    // Only the components before the '-' are the version proper; the digits
    // inside "alpha.3" are the prerelease counter and are compared separately.
    const char *aDash = strchr(a, '-');
    const char *bDash = strchr(b, '-');
    const int aLimit = aDash ? (int)(aDash - a) : (int)strlen(a);
    const int bLimit = bDash ? (int)(bDash - b) : (int)strlen(b);

    while (true) {
        int va = (ia < aLimit) ? parseNextVersionNumber(a, ia) : -1;
        int vb = (ib < bLimit) ? parseNextVersionNumber(b, ib) : -1;
        if (va >= 0 && ia > aLimit) va = -1;   // ran past the '-'
        if (vb >= 0 && ib > bLimit) vb = -1;

        if (va < 0 && vb < 0) break;
        if (va < 0) va = 0;
        if (vb < 0) vb = 0;

        if (va < vb) return -1;
        if (va > vb) return 1;
    }

    // Same release numbers. A prerelease loses to the finished release.
    if (aPre && !bPre) return -1;
    if (!aPre && bPre) return 1;
    if (!aPre && !bPre) return 0;

    // Both prereleases of the same version: compare their counters
    // ("alpha.3" vs "alpha.10") numerically, which is how release.sh numbers
    // them. A tag with no digits after the dash counts as 0.
    int ja = aDash ? (int)(aDash - a) + 1 : 0;
    int jb = bDash ? (int)(bDash - b) + 1 : 0;
    while (true) {
        int va = aDash ? parseNextVersionNumber(a, ja) : -1;
        int vb = bDash ? parseNextVersionNumber(b, jb) : -1;
        if (va < 0 && vb < 0) return 0;
        if (va < 0) va = 0;
        if (vb < 0) vb = 0;
        if (va < vb) return -1;
        if (va > vb) return 1;
    }
}

// A release tag is a prerelease when it carries a SemVer prerelease suffix,
// i.e. anything after a '-' (e.g. "v3.1.2-alpha.1"). Devices must never
// auto-update onto a prerelease — even when already running one — so these are
// never treated as an available update. Old-style suffixes without a hyphen
// (e.g. "v2.9.4a") are NOT prereleases and remain updatable.
static bool isPrereleaseTag(const char *tag) {
    if (!tag) return false;
    for (const char *p = tag; *p; ++p) {
        if (*p == '-') return true;
    }
    return false;
}

// The proxy relays GitHub's release API over plain HTTP, so the same tag_name
// parser works for both channels. There are no HTTPS fallbacks: this firmware
// has no TLS.
//
// Two routes, because GitHub's own /releases/latest deliberately excludes
// prereleases and so can never answer for alpha:
//   /firmware/latest        -> newest published release   (stable channel)
//   /firmware/latest-alpha  -> newest release INCLUDING prereleases (alpha)
// Both must return release JSON carrying a "tag_name". The alpha route is the
// proxy's job to provide; a proxy that does not serve it fails the check with
// an HTTP error rather than quietly falling back to stable, which would install
// the wrong channel's build.
static bool fetchLatestReleaseTag(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    if (WiFi.status() != WL_CONNECTED) {
        errOut = "WiFi not connected";
        return false;
    }

    char url[160];
    snprintf(url, sizeof(url), "%s/firmware/%s", s_otaBaseUrl,
             (otaResolveChannel(g_otaChannel) == OTA_CHANNEL_ALPHA) ? "latest-alpha"
                                                                    : "latest");
    String body, err;
    if (httpGetString(url, body, err, true, nullptr, nullptr,
                      "application/vnd.github+json")
        && extractJsonStringField(body, "tag_name", tagOut)
        && tagOut.length() > 0) {
        return true;
    }
    errOut = err.length() ? err : String("Release tag not found (proxy)");
    return false;
}

// Finds the newest prerelease in the release listing, without ever holding the
// listing in RAM.
//
// The alpha channel is a track, not just "earliest access": it lands on the
// newest -alpha.N even when a finished release outranks it. GitHub has no
// "latest prerelease" endpoint and no way to filter the listing, so the choice
// has to be made here.
//
// Streaming matters. One release object is ~43 KB because of its assets array,
// and the listing is several of those -- far past what this device can buffer.
// What makes it cheap anyway is GitHub's field order: tag_name and prerelease
// both appear near the top of each object, before assets. So each release costs
// a few hundred bytes to classify, and the scan stops at the first prerelease
// rather than reading to the end.
//
// Only two literals are matched, and a short tail is carried between reads so a
// token split across a chunk boundary is still seen.
static bool fetchLatestPrereleaseTag(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    if (WiFi.status() != WL_CONNECTED) {
        errOut = "WiFi not connected";
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s/firmware/latest-alpha", s_otaBaseUrl);

    preferExternalHeapForOta();
    WiFiClient plainClient;
    HTTPClient http;
    if (!http.begin(plainClient, url)) {
        errOut = "alpha request start failed";
        return false;
    }
    http.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
    http.addHeader("User-Agent", "camillia-mt-ota");
    http.addHeader("Accept", "application/vnd.github+json");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        errOut = String("alpha HTTP ") + String(code);
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    if (!stream) {
        errOut = "alpha stream unavailable";
        http.end();
        return false;
    }

    static const char kTagKey[] = "\"tag_name\":\"";
    static const char kPreKey[] = "\"prerelease\":true";
    // Long enough to span either literal plus a whole tag value across a split.
    constexpr size_t kCarry = 96;
    // Hard ceiling so a listing with no prerelease in it cannot read forever.
    constexpr size_t kMaxScanBytes = 512UL * 1024UL;

    String window;
    String lastTag;
    size_t scanned = 0;
    bool found = false;
    uint32_t lastDataMs = millis();

    while (http.connected() && scanned < kMaxScanBytes) {
        const size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - lastDataMs > kReleaseCheckTimeoutMs) break;
            delay(5);
            continue;
        }
        char buf[256];
        const size_t want = (avail > sizeof(buf)) ? sizeof(buf) : avail;
        const int n = stream->readBytes((uint8_t *)buf, want);
        if (n <= 0) continue;
        lastDataMs = millis();
        scanned += (size_t)n;
        window.concat(buf, (size_t)n);

        // Consume every complete token currently in the window. tag_name always
        // precedes prerelease within an object, so the tag standing when the
        // prerelease flag is seen is that object's tag.
        while (true) {
            const int tagPos = window.indexOf(kTagKey);
            const int prePos = window.indexOf(kPreKey);
            if (tagPos < 0 && prePos < 0) break;

            if (prePos >= 0 && (tagPos < 0 || prePos < tagPos)) {
                if (lastTag.length()) {
                    tagOut = lastTag;
                    found = true;
                }
                window.remove(0, prePos + (int)strlen(kPreKey));
                if (found) break;
                continue;
            }

            const int valStart = tagPos + (int)strlen(kTagKey);
            const int valEnd = window.indexOf('"', valStart);
            if (valEnd < 0) break;   // value still incomplete; wait for more
            lastTag = window.substring(valStart, valEnd);
            window.remove(0, valEnd + 1);
        }
        if (found) break;

        if (window.length() > kCarry) {
            window.remove(0, window.length() - kCarry);
        }
    }

    http.end();

    if (found && tagOut.length()) return true;
    errOut = scanned ? String("no prerelease found") : String("no alpha response");
    return false;
}

// Proxy layout: {base}/firmware/<tag>/<asset>
static void buildAssetUrl(const char *tag, char *outUrl, size_t outLen) {
    if (!outUrl || outLen == 0) return;
    const char *useTag = (tag && tag[0]) ? tag : "";
    const char *slug = otaCurrentDeviceAssetSlug();
    if (!slug || !slug[0]) {
        outUrl[0] = '\0';
        return;
    }
    snprintf(outUrl, outLen,
             "%s/firmware/%s/camillia-mt-%s-%s-ota.bin",
             s_otaBaseUrl, useTag, slug, useTag);
}

// Fetch a small binary body (the detached signature) into buf over plain HTTP.
static bool httpGetBytes(const char *url, uint8_t *buf, size_t cap,
                         size_t &outLen, String &errOut) {
    outLen = 0;
    errOut = "";
    WiFiClient plainClient;
    HTTPClient http;
    if (!http.begin(plainClient, url)) {
        errOut = "sig request start failed";
        return false;
    }

    http.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
    http.addHeader("User-Agent", "camillia-mt-ota");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        errOut = String("signature HTTP ") + String(code);
        http.end();
        return false;
    }

    int total = http.getSize();
    if (total > 0 && (size_t)total > cap) {
        errOut = "signature too large";
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint32_t lastMs = millis();
    while (stream && http.connected() && (total < 0 || outLen < (size_t)total)) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - lastMs > 5000) break;
            delay(5);
            continue;
        }
        if (avail > cap - outLen) avail = cap - outLen;
        int n = stream->readBytes(buf + outLen, avail);
        if (n > 0) { outLen += (size_t)n; lastMs = millis(); }
        if (outLen >= cap) break;
    }
    http.end();
    if (outLen == 0) { errOut = "empty signature"; return false; }
    return true;
}

// Verify a detached ECDSA-P256 signature (DER) over a SHA-256 digest using the
// public key baked into the firmware. Fail-closed: any error returns false.
static bool verifyOtaSignature(const uint8_t *hash, const uint8_t *sig, size_t sigLen) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(
        &pk,
        (const unsigned char *)OTA_SIGNING_PUBKEY_PEM,
        strlen(OTA_SIGNING_PUBKEY_PEM) + 1);
    if (rc != 0) {
        Serial.printf("[ota] signing pubkey parse failed: -0x%04x\n", (unsigned)-rc);
        mbedtls_pk_free(&pk);
        return false;
    }
    rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen);
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        Serial.printf("[ota] signature verification failed: -0x%04x\n", (unsigned)-rc);
        return false;
    }
    return true;
}

static bool setErr(char *errOut, size_t errLen, const char *msg) {
    copyStringToBuf(errOut, errLen, msg);
    return false;
}
} // namespace

void otaSetNetworkAllowed(bool allowed) {
    g_otaNetworkGate = allowed ? kOtaNetworkGateMagic : 0;
    Serial.printf("[tls-guard] gate=%s\n", allowed ? "on" : "off");
}

bool otaRunningPrerelease() {
    return isPrereleaseTag(APP_VERSION);
}

uint8_t otaResolveChannel(uint8_t storedChannel) {
    if (storedChannel == OTA_CHANNEL_ALPHA)  return OTA_CHANNEL_ALPHA;
    if (storedChannel == OTA_CHANNEL_STABLE) return OTA_CHANNEL_STABLE;
    // AUTO, or anything unrecognised: follow the build that is running. A
    // device flashed with an alpha image is on the alpha track whether or not
    // anyone ever opened the settings screen.
    return otaRunningPrerelease() ? OTA_CHANNEL_ALPHA : OTA_CHANNEL_STABLE;
}

void otaSetChannel(uint8_t channel) {
    // Stored raw so an explicit choice stays distinguishable from AUTO;
    // anything unrecognised collapses to AUTO rather than to a channel the
    // user never picked.
    uint8_t next = channel;
    if (next != OTA_CHANNEL_STABLE && next != OTA_CHANNEL_ALPHA) {
        next = OTA_CHANNEL_AUTO;
    }
    if (next == g_otaChannel) return;
    g_otaChannel = next;
    const uint8_t resolved = otaResolveChannel(next);
    Serial.printf("[ota] release channel=%s%s\n",
                  (resolved == OTA_CHANNEL_ALPHA) ? "alpha" : "stable",
                  (next == OTA_CHANNEL_AUTO) ? " (auto, from running build)" : "");
}

uint8_t otaCurrentChannel() {
    return g_otaChannel;
}

const char *otaCurrentDeviceAssetSlug() {
#if defined(DEVICE_TDECK)
    return "tdeck";
#elif defined(DEVICE_TDECK_PRO)
    return "tdeck-pro";
#elif defined(DEVICE_TLORA_PAGER_TFT)
    return "tlora-pager-tft";
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    return "cardputer-cap";
#elif defined(DEVICE_MESH_DECK)
    return "mesh-deck";
#elif defined(DEVICE_M9)
    return "m9";
#elif defined(DEVICE_WIO_TRACKER_L2)
    return "wio-tracker-l2";
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
  #if defined(DEVICE_UI_VERTICAL) && (DEVICE_UI_VERTICAL)
    return "heltec-vertical";
  #else
    return "heltec";
  #endif
#else
    return "";
#endif
}

bool otaCheckLatestRelease(OtaCheckResult &out) {
    clearCheckResult(out);

    if (!otaCurrentDeviceAssetSlug()[0]) {
        out.ok = false;
        copyStringToBuf(out.error,
                        sizeof(out.error),
                        "OTA unsupported for this device build");
        return false;
    }

    if (g_otaNetworkGate != kOtaNetworkGateMagic) {
        out.ok = false;
        copyStringToBuf(out.error,
                        sizeof(out.error),
                        "OTA network blocked (non-worker mode)");
        return false;
    }

    const uint8_t channelSel = otaResolveChannel(g_otaChannel);
    String latestTag;
    String err;
    const bool gotTag = (channelSel == OTA_CHANNEL_ALPHA)
                            ? fetchLatestPrereleaseTag(latestTag, err)
                            : fetchLatestReleaseTag(latestTag, err);
    if (!gotTag) {
        out.ok = false;
        copyStringToBuf(out.error, sizeof(out.error), err.c_str());
        return false;
    }

    copyStringToBuf(out.latestTag, sizeof(out.latestTag), latestTag.c_str());
    buildAssetUrl(out.latestTag, out.downloadUrl, sizeof(out.downloadUrl));
    if (!out.downloadUrl[0]) {
        out.ok = false;
        copyStringToBuf(out.error,
                        sizeof(out.error),
                        "OTA unsupported for this device build");
        return false;
    }

    const uint8_t channel = channelSel;
    if (channel == OTA_CHANNEL_ALPHA) {
        // The alpha channel is a track, so "is it newer" is the wrong question:
        // once a finished release ships it outranks every alpha of that version,
        // and a version test would strand an opted-in tester on stable forever.
        // Anything that is not already the newest alpha is therefore offered,
        // including a step back onto the track from a higher stable build.
        out.updateAvailable = (strcmp(APP_VERSION, out.latestTag) != 0);
        if (!out.updateAvailable) {
            Serial.printf("[ota] already on the newest alpha (%s)\n", out.latestTag);
        } else {
            Serial.printf("[ota] alpha channel: %s -> %s\n", APP_VERSION, out.latestTag);
        }
        out.ok = true;
        return true;
    }
    if (isPrereleaseTag(out.latestTag) && channel != OTA_CHANNEL_ALPHA) {
        // Never auto-update a stable-channel device onto a prerelease.
        // /releases/latest already excludes them, but the release-page and
        // raw-VERSION fallbacks could theoretically surface one, so guard here
        // too. On the alpha channel a prerelease is the point, so it falls
        // through to the ordinary newer-than-current comparison below.
        Serial.printf("[ota] ignoring prerelease tag: %s\n", out.latestTag);
        out.updateAvailable = false;
    } else if (kTreatLatestAsUpdateForTesting) {
        out.updateAvailable = true;
    } else if (channel == OTA_CHANNEL_STABLE && otaRunningPrerelease()
               && !isPrereleaseTag(out.latestTag)) {
        // Leaving the alpha track. The running alpha is numerically *ahead* of
        // the stable it came from -- v4.7.8-alpha.1 against v4.7.7 -- so the
        // ordinary "is it newer" test says up-to-date and strands the device on
        // a prerelease it has explicitly opted out of, with no way back short
        // of a USB reflash.
        //
        // Switching to Stable is therefore treated as "return to the stable
        // track", and the newest stable release is offered whether or not it
        // sorts above what is running. That is a deliberate downgrade, which is
        // exactly what the user asked for by changing the channel.
        out.updateAvailable = true;
        Serial.printf("[ota] on stable channel while running prerelease %s;"
                      " offering return to %s\n",
                      APP_VERSION, out.latestTag);
    } else {
        out.updateAvailable = (compareVersionTags(APP_VERSION, out.latestTag) < 0);
    }
    out.ok = true;
    return true;
}

// True when the device is running the dual-slot layout this project ships:
// OTA slot 0 at 0x10000, a second slot of identical size, no third, and the
// running app in one of them.
//
// A third-party installer keeps its own app in a non-OTA partition at 0x10000
// and carves one OTA slot per firmware it installs, so it fails these tests.
// That distinction matters: on such a device the "next" OTA slot holds another
// app the user installed, and an update would overwrite it without warning.
//
// Structural rather than a size comparison against partitions.csv, so it stays
// correct if the slot sizes change. It fails closed — anything unrecognised
// gets no update offered.
bool otaLayoutSupportsUpdate() {
    const esp_partition_t *slot0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t *slot1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const esp_partition_t *slot2 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_2, nullptr);
    if (!slot0 || !slot1 || slot2) return false;
    if (slot0->size != slot1->size) return false;
    // The first app partition follows the bootloader, table and data partitions
    // at the conventional 0x10000. An installer that owns the flash has its own
    // app there, which is what pushes its OTA slots higher.
    if (slot0->address != 0x10000) return false;

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return false;
    return (running->address == slot0->address || running->address == slot1->address);
}

bool otaInstallLatestRelease(const char *tag,
                             char *errOut,
                             size_t errLen,
                             OtaInstallProgressCb progressCb) {
    if (!otaCurrentDeviceAssetSlug()[0]) {
        return setErr(errOut, errLen, "OTA unsupported for this device build");
    }

    if (g_otaNetworkGate != kOtaNetworkGateMagic) {
        return setErr(errOut, errLen, "OTA network blocked (non-worker mode)");
    }

    if (!otaLayoutSupportsUpdate()) {
        return setErr(errOut, errLen,
                      "OTA unavailable on this flash layout - reflash the factory image");
    }

    preferExternalHeapForOta();

    if (WiFi.status() != WL_CONNECTED) {
        return setErr(errOut, errLen, "WiFi not connected");
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (!next) {
        return setErr(errOut, errLen, "No OTA app partition available");
    }

    char tagBuf[48] = {};
    if (tag && tag[0]) {
        copyStringToBuf(tagBuf, sizeof(tagBuf), tag);
    } else {
        String latestTag;
        String err;
        // Same source the check used, or the install would resolve "latest" on
        // the other channel and fetch a build the user was never offered.
        const bool gotTag = (otaResolveChannel(g_otaChannel) == OTA_CHANNEL_ALPHA)
                                ? fetchLatestPrereleaseTag(latestTag, err)
                                : fetchLatestReleaseTag(latestTag, err);
        if (!gotTag) {
            return setErr(errOut, errLen, err.c_str());
        }
        copyStringToBuf(tagBuf, sizeof(tagBuf), latestTag.c_str());
    }

    // Hard stop on the stable channel: never install a prerelease build,
    // regardless of how the tag was supplied. A device opted in to alpha is
    // asking for exactly these builds, so it is allowed through — the image is
    // still signature-checked against the baked-in key like any other.
    if (isPrereleaseTag(tagBuf) && otaResolveChannel(g_otaChannel) != OTA_CHANNEL_ALPHA) {
        return setErr(errOut, errLen, "Refusing to install prerelease (alpha) build");
    }

    char url[256] = {};
    buildAssetUrl(tagBuf, url, sizeof(url));
    if (!url[0]) {
        return setErr(errOut, errLen, "OTA unsupported for this device build");
    }

    // Fetch the detached signature up front so a missing/short signature aborts
    // before we touch the OTA partition. Fail-closed: no valid signature later
    // means the image is never committed.
    char sigUrl[288] = {};
    snprintf(sigUrl, sizeof(sigUrl), "%s.sig", url);
    uint8_t sigBuf[96];
    size_t sigLen = 0;
    {
        String sigErr;
        if (!httpGetBytes(sigUrl, sigBuf, sizeof(sigBuf), sigLen, sigErr)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "OTA signature fetch failed: %s", sigErr.c_str());
            return setErr(errOut, errLen, msg);
        }
    }

    WiFiClient plainClient;
    HTTPClient http;
    if (!http.begin(plainClient, url)) {
        return setErr(errOut, errLen, "Failed to start OTA download");
    }

    http.setTimeout(30000);
    http.addHeader("User-Agent", "camillia-mt-ota");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code <= 0) {
        if (code == -1) {
            http.end();
            return setErr(errOut, errLen, "OTA download connect failed");
        }
        http.end();
        return setErr(errOut, errLen, "OTA download network error");
    }
    if (code != HTTP_CODE_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "OTA HTTP %d", code);
        http.end();
        return setErr(errOut, errLen, msg);
    }

    int contentLen = http.getSize();
    size_t updateSize = (contentLen > 0) ? (size_t)contentLen : (size_t)UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(updateSize)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Update.begin failed (%u)", (unsigned)Update.getError());
        http.end();
        return setErr(errOut, errLen, msg);
    }

    WiFiClient &stream = http.getStream();

    // Hash the image as it streams so we can verify the signature over the whole
    // file without buffering it.
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);   // 0 = SHA-256

    constexpr size_t kChunkSize = 1024;
    constexpr uint32_t kDownloadStallTimeoutMs = 15000;
    uint8_t chunk[kChunkSize];
    size_t written = 0;
    uint32_t lastProgressMs = millis();

    while (http.connected() && (contentLen <= 0 || written < (size_t)contentLen)) {
        size_t avail = stream.available();
        if (avail == 0) {
            if (progressCb) {
                progressCb(written, (contentLen > 0) ? (size_t)contentLen : 0);
            }
            if ((millis() - lastProgressMs) > kDownloadStallTimeoutMs) {
                Update.abort();
                http.end();
                return setErr(errOut, errLen, "OTA stalled (no data)");
            }
            delay(10);
            continue;
        }

        if (avail > kChunkSize) avail = kChunkSize;
        int n = stream.readBytes((char *)chunk, avail);
        if (n <= 0) {
            if (progressCb) {
                progressCb(written, (contentLen > 0) ? (size_t)contentLen : 0);
            }
            if ((millis() - lastProgressMs) > kDownloadStallTimeoutMs) {
                Update.abort();
                http.end();
                return setErr(errOut, errLen, "OTA stalled (read timeout)");
            }
            delay(10);
            continue;
        }

        size_t wr = Update.write(chunk, (size_t)n);
        if (wr != (size_t)n) {
            mbedtls_sha256_free(&sha);
            Update.abort();
            http.end();
            return setErr(errOut, errLen, "OTA write failed");
        }
        mbedtls_sha256_update(&sha, chunk, (size_t)n);

        written += wr;
        lastProgressMs = millis();
        if (progressCb) {
            progressCb(written, (contentLen > 0) ? (size_t)contentLen : 0);
        }
    }

    if (contentLen > 0 && written != (size_t)contentLen) {
        mbedtls_sha256_free(&sha);
        Update.abort();
        http.end();
        return setErr(errOut, errLen, "OTA write incomplete");
    }

    // Verify the signature over the downloaded image BEFORE committing the boot
    // partition. This is what makes the TLS-free download safe: a tampered or
    // corrupt image fails here and is never booted.
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    if (!verifyOtaSignature(digest, sigBuf, sigLen)) {
        Update.abort();
        http.end();
        return setErr(errOut, errLen, "OTA signature verification failed");
    }

    if (!Update.end()) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Update.end failed (%u)", (unsigned)Update.getError());
        http.end();
        return setErr(errOut, errLen, msg);
    }

    if (!Update.isFinished()) {
        http.end();
        return setErr(errOut, errLen, "OTA image not complete");
    }

    http.end();
    return true;
}
