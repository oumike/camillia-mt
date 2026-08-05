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

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static volatile uint32_t g_otaNetworkGate = 0;
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

static int compareVersionTags(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    int ia = 0;
    int ib = 0;

    while (true) {
        int va = parseNextVersionNumber(a, ia);
        int vb = parseNextVersionNumber(b, ib);

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

// The proxy relays GitHub's "latest release" API over plain HTTP, so the same
// tag_name parser works. There are no HTTPS fallbacks: this firmware has no TLS.
// /releases/latest excludes prereleases, so alpha builds never surface here.
static bool fetchLatestReleaseTag(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    if (WiFi.status() != WL_CONNECTED) {
        errOut = "WiFi not connected";
        return false;
    }

    char url[160];
    snprintf(url, sizeof(url), "%s/firmware/latest", s_otaBaseUrl);
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

const char *otaCurrentDeviceAssetSlug() {
#if defined(DEVICE_TDECK)
    return "tdeck";
#elif defined(DEVICE_TLORA_PAGER_TFT)
    return "tlora-pager-tft";
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    return "cardputer-cap";
#elif defined(DEVICE_MESH_DECK)
    return "mesh-deck";
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

    String latestTag;
    String err;
    if (!fetchLatestReleaseTag(latestTag, err)) {
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

    if (isPrereleaseTag(out.latestTag)) {
        // Never auto-update onto an alpha/prerelease. /releases/latest already
        // excludes prereleases, but the release-page and raw-VERSION fallbacks
        // could theoretically surface one, so guard here too.
        Serial.printf("[ota] ignoring prerelease tag: %s\n", out.latestTag);
        out.updateAvailable = false;
    } else if (kTreatLatestAsUpdateForTesting) {
        out.updateAvailable = true;
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
        if (!fetchLatestReleaseTag(latestTag, err)) {
            return setErr(errOut, errLen, err.c_str());
        }
        copyStringToBuf(tagBuf, sizeof(tagBuf), latestTag.c_str());
    }

    // Hard stop: never install a prerelease (alpha) build via the update path,
    // regardless of how the tag was supplied.
    if (isPrereleaseTag(tagBuf)) {
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
