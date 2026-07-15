#include "ota_update.h"

#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ssl_client.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <ctype.h>
#include <string.h>
#include <type_traits>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static volatile uint32_t g_otaNetworkGate = 0;
static constexpr uint32_t kOtaNetworkGateMagic = 0x4F544131UL; // "OTA1"

static inline bool otaNetworkGateIsAllowed() {
    return g_otaNetworkGate == kOtaNetworkGateMagic;
}

extern "C" int __real__ZN16WiFiClientSecure7connectEPKct(
    WiFiClientSecure *self,
    const char *host,
    uint16_t port);

extern "C" int __wrap__ZN16WiFiClientSecure7connectEPKct(
    WiFiClientSecure *self,
    const char *host,
    uint16_t port) {
    if (!otaNetworkGateIsAllowed()) {
        const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        Serial.printf("[tls-guard] blocked connect(host,port) host=%s port=%u int_free=%lu largest=%lu\n",
                      (host && host[0]) ? host : "(null)",
                      (unsigned)port,
                      (unsigned long)freeInt,
                      (unsigned long)largestInt);
        (void)self;
        return 0;
    }

    Serial.printf("[tls-guard] allow connect(host,port) host=%s port=%u\n",
                  (host && host[0]) ? host : "(null)",
                  (unsigned)port);
    return __real__ZN16WiFiClientSecure7connectEPKct(self, host, port);
}

extern "C" int __real__ZN16WiFiClientSecure7connectEPKcti(
    WiFiClientSecure *self,
    const char *host,
    uint16_t port,
    int timeout);

extern "C" int __wrap__ZN16WiFiClientSecure7connectEPKcti(
    WiFiClientSecure *self,
    const char *host,
    uint16_t port,
    int timeout) {
    if (!otaNetworkGateIsAllowed()) {
        const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        Serial.printf("[tls-guard] blocked connect(host,port,timeout) host=%s port=%u timeout=%d int_free=%lu largest=%lu\n",
                      (host && host[0]) ? host : "(null)",
                      (unsigned)port,
                      timeout,
                      (unsigned long)freeInt,
                      (unsigned long)largestInt);
        (void)self;
        return 0;
    }

    Serial.printf("[tls-guard] allow connect(host,port,timeout) host=%s port=%u timeout=%d\n",
                  (host && host[0]) ? host : "(null)",
                  (unsigned)port,
                  timeout);
    return __real__ZN16WiFiClientSecure7connectEPKcti(self, host, port, timeout);
}

extern "C" int __real__ZN16WiFiClientSecure7connectE9IPAddresst(
    WiFiClientSecure *self,
    IPAddress ip,
    uint16_t port);

extern "C" int __wrap__ZN16WiFiClientSecure7connectE9IPAddresst(
    WiFiClientSecure *self,
    IPAddress ip,
    uint16_t port) {
    if (!otaNetworkGateIsAllowed()) {
        const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        String ipStr = ip.toString();
        Serial.printf("[tls-guard] blocked connect(ip,port) ip=%s port=%u int_free=%lu largest=%lu\n",
                      ipStr.c_str(),
                      (unsigned)port,
                      (unsigned long)freeInt,
                      (unsigned long)largestInt);
        (void)self;
        return 0;
    }

    return __real__ZN16WiFiClientSecure7connectE9IPAddresst(self, ip, port);
}

extern "C" int __real__ZN16WiFiClientSecure7connectE9IPAddressti(
    WiFiClientSecure *self,
    IPAddress ip,
    uint16_t port,
    int timeout);

extern "C" int __wrap__ZN16WiFiClientSecure7connectE9IPAddressti(
    WiFiClientSecure *self,
    IPAddress ip,
    uint16_t port,
    int timeout) {
    if (!otaNetworkGateIsAllowed()) {
        const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        String ipStr = ip.toString();
        Serial.printf("[tls-guard] blocked connect(ip,port,timeout) ip=%s port=%u timeout=%d int_free=%lu largest=%lu\n",
                      ipStr.c_str(),
                      (unsigned)port,
                      timeout,
                      (unsigned long)freeInt,
                      (unsigned long)largestInt);
        (void)self;
        return 0;
    }

    return __real__ZN16WiFiClientSecure7connectE9IPAddressti(self, ip, port, timeout);
}

extern "C" int __real__Z16start_ssl_clientP17sslclient_contextRK9IPAddressjPKciS5_bS5_S5_S5_S5_bPS5_(
    sslclient_context *ssl_client,
    const IPAddress &ip,
    uint32_t port,
    const char *hostname,
    int timeout,
    const char *rootCABuff,
    bool useRootCABundle,
    const char *cli_cert,
    const char *cli_key,
    const char *pskIdent,
    const char *psKey,
    bool insecure,
    const char **alpn_protos);

extern "C" int __wrap__Z16start_ssl_clientP17sslclient_contextRK9IPAddressjPKciS5_bS5_S5_S5_S5_bPS5_(
    sslclient_context *ssl_client,
    const IPAddress &ip,
    uint32_t port,
    const char *hostname,
    int timeout,
    const char *rootCABuff,
    bool useRootCABundle,
    const char *cli_cert,
    const char *cli_key,
    const char *pskIdent,
    const char *psKey,
    bool insecure,
    const char **alpn_protos) {
    const bool networkAllowed = otaNetworkGateIsAllowed();
    if (!networkAllowed) {
        const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        Serial.printf("[tls-guard] blocked host=%s port=%lu int_free=%lu largest=%lu\n",
                      (hostname && hostname[0]) ? hostname : "(null)",
                      (unsigned long)port,
                      (unsigned long)freeInt,
                      (unsigned long)largestInt);
        (void)ssl_client;
        (void)ip;
        (void)timeout;
        (void)rootCABuff;
        (void)useRootCABundle;
        (void)cli_cert;
        (void)cli_key;
        (void)pskIdent;
        (void)psKey;
        (void)insecure;
        (void)alpn_protos;
        return -1;
    }

    Serial.printf("[tls-guard] allow host=%s port=%lu\n",
                  (hostname && hostname[0]) ? hostname : "(null)",
                  (unsigned long)port);

    return __real__Z16start_ssl_clientP17sslclient_contextRK9IPAddressjPKciS5_bS5_S5_S5_S5_bPS5_(
        ssl_client,
        ip,
        port,
        hostname,
        timeout,
        rootCABuff,
        useRootCABundle,
        cli_cert,
        cli_key,
        pskIdent,
        psKey,
        insecure,
        alpn_protos);
}

namespace {
constexpr uint32_t kReleaseCheckTimeoutMs = 12000;
#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr int kTlsRxBufBytes = 512;
constexpr int kTlsTxBufBytes = 512;
#else
constexpr int kTlsRxBufBytes = 1024;
constexpr int kTlsTxBufBytes = 512;
#endif
constexpr const char *kLatestReleaseApiUrl = "https://api.github.com/repos/oumike/camillia-mt/releases/latest";
constexpr const char *kLatestVersionRawUrl = "https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION";
constexpr const char *kLatestVersionGithubUrl = "https://github.com/oumike/camillia-mt/raw/main/VERSION";
constexpr const char *kLatestReleasePageUrl = "https://github.com/oumike/camillia-mt/releases/latest";
constexpr const char *kReleaseDownloadBaseUrl = "https://github.com/oumike/camillia-mt/releases/download/";

// Temporary behavior for OTA testing: any successfully-fetched latest release
// is treated as installable even when equal to current APP_VERSION.
constexpr bool kTreatLatestAsUpdateForTesting = false;

template <typename T>
class HasSetBufferSizes {
    template <typename U, void (U::*)(int, int)> struct SFINAE;
    template <typename U> static char test(SFINAE<U, &U::setBufferSizes> *);
    template <typename U> static int test(...);

public:
    static const bool value = (sizeof(test<T>(nullptr)) == sizeof(char));
};

template <typename T>
typename std::enable_if<HasSetBufferSizes<T>::value, void>::type
applyTlsBufferTuning(T &client) {
    client.setBufferSizes(kTlsRxBufBytes, kTlsTxBufBytes);
}

template <typename T>
typename std::enable_if<!HasSetBufferSizes<T>::value, void>::type
applyTlsBufferTuning(T &client) {
    (void)client;
}

static void configureTlsClient(WiFiClientSecure &client) {
    client.setInsecure();
    applyTlsBufferTuning(client);
}

static void preferExternalHeapForOta() {
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
    static bool enabled = false;
    if (!enabled) {
        // Route generic malloc allocations to PSRAM where possible, leaving
        // more contiguous internal heap available for TLS startup buffers.
        heap_caps_malloc_extmem_enable(0);
        enabled = true;
    }
#endif
}

static bool isLikelyTlsInitFailure(const String &err) {
    // On Arduino-ESP32 this often surfaces as "Network error (-1)" and is
    // commonly paired with esp-sha allocation failures in serial output.
    return err.indexOf("(-1)") >= 0;
}

static void buildTlsLowMemError(String &out) {
    char msg[160];
    const uint32_t freeInt = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largestInt = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    snprintf(msg,
             sizeof(msg),
             "TLS init failed (low memory): int_free=%lu largest=%lu",
             (unsigned long)freeInt,
             (unsigned long)largestInt);
    out = msg;
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

static void trimAsciiWhitespace(String &s) {
    int start = 0;
    int end = (int)s.length() - 1;
    while (start <= end && isspace((unsigned char)s[start])) start++;
    while (end >= start && isspace((unsigned char)s[end])) end--;
    if (start == 0 && end == (int)s.length() - 1) return;
    if (end < start) {
        s = "";
        return;
    }
    s = s.substring(start, end + 1);
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

    WiFiClientSecure client;
    configureTlsClient(client);

    HTTPClient http;
    if (!http.begin(client, url)) {
        errOut = "Failed to start HTTPS request";
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

static bool extractTagFromReleasePath(const String &text, String &tagOut) {
    tagOut = "";
    int p = text.indexOf("/tag/");
    if (p < 0) return false;

    int start = p + 5;
    int end = start;
    while (end < (int)text.length()) {
        char c = text[end];
        if (c == '"' || c == '\'' || c == '?' || c == '&' || c == '#' || isspace((unsigned char)c)) {
            break;
        }
        end++;
    }

    if (end <= start) return false;
    tagOut = text.substring(start, end);
    trimAsciiWhitespace(tagOut);
    return tagOut.length() > 0;
}

static bool fetchLatestTagFromReleasePage(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    String body;
    String err;
    String location;
    int status = 0;
    bool ok = httpGetString(kLatestReleasePageUrl,
                            body,
                            err,
                            false,
                            &status,
                            &location,
                            "text/html");

    // github.com/releases/latest typically returns a redirect to /releases/tag/<tag>.
    if (!ok) {
        if ((status == HTTP_CODE_MOVED_PERMANENTLY
             || status == HTTP_CODE_FOUND
             || status == HTTP_CODE_SEE_OTHER
             || status == HTTP_CODE_TEMPORARY_REDIRECT
             || status == HTTP_CODE_PERMANENT_REDIRECT)
            && location.length() > 0
            && extractTagFromReleasePath(location, tagOut)) {
            return true;
        }
        errOut = String("Release page ") + (err.length() ? err : String("failed"));
        return false;
    }

    if (extractTagFromReleasePath(body, tagOut)) {
        return true;
    }

    errOut = "Release page tag not found";
    return false;
}

static bool fetchLatestTagFromVersionFile(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";
    const char *urls[] = {
        kLatestVersionRawUrl,
        kLatestVersionGithubUrl,
    };

    String firstErr;
    String secondErr;

    for (int i = 0; i < 2; i++) {
        String body;
        String err;
        if (!httpGetString(urls[i], body, err, true, nullptr, nullptr, "text/plain")) {
            if (i == 0) firstErr = err;
            else secondErr = err;
            continue;
        }

        tagOut = body;
        trimAsciiWhitespace(tagOut);
        if (tagOut.length() > 0) {
            return true;
        }
        if (i == 0) firstErr = "VERSION file empty";
        else secondErr = "VERSION file empty";
    }

    errOut = String("VERSION failed (") + firstErr + "; " + secondErr + ")";
    return false;
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

static bool fetchLatestReleaseTag(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    if (WiFi.status() != WL_CONNECTED) {
        errOut = "WiFi not connected";
        return false;
    }

    String apiErr;
    String apiBody;
    if (httpGetString(kLatestReleaseApiUrl,
                      apiBody,
                      apiErr,
                      true,
                      nullptr,
                      nullptr,
                      "application/vnd.github+json")) {
        if (extractJsonStringField(apiBody, "tag_name", tagOut) && tagOut.length() > 0) {
            return true;
        }
        apiErr = "Release API tag not found";
    } else if (isLikelyTlsInitFailure(apiErr)) {
        buildTlsLowMemError(errOut);
        return false;
    }

    String pageErr;
    if (fetchLatestTagFromReleasePage(tagOut, pageErr)) {
        return true;
    }
    if (isLikelyTlsInitFailure(pageErr)) {
        buildTlsLowMemError(errOut);
        return false;
    }

    String versionErr;
    if (fetchLatestTagFromVersionFile(tagOut, versionErr)) {
        return true;
    }
    if (isLikelyTlsInitFailure(versionErr)) {
        buildTlsLowMemError(errOut);
        return false;
    }

    if (apiErr.length() && pageErr.length() && versionErr.length()) {
        errOut = apiErr + "; page fallback failed (" + pageErr + "); version fallback failed (" + versionErr + ")";
    } else if (apiErr.length()) {
        errOut = apiErr;
    } else if (pageErr.length()) {
        errOut = pageErr;
    } else {
        errOut = versionErr;
    }
    return false;
}

static void buildAssetUrl(const char *tag, char *outUrl, size_t outLen) {
    if (!outUrl || outLen == 0) return;
    const char *useTag = (tag && tag[0]) ? tag : "";
    snprintf(outUrl,
             outLen,
             "%s%s/camillia-mt-%s-%s-ota.bin",
             kReleaseDownloadBaseUrl,
             useTag,
             otaCurrentDeviceAssetSlug(),
             useTag);
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

void otaPreferExternalHeap() {
    preferExternalHeapForOta();
}

const char *otaCurrentDeviceAssetSlug() {
#if defined(DEVICE_TDECK)
    return "tdeck";
#elif defined(DEVICE_TLORA_PAGER_TFT)
    return "tlora-pager-tft";
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
    return "cardputer-cap";
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
  #if defined(DEVICE_UI_VERTICAL) && (DEVICE_UI_VERTICAL)
    return "heltec-vertical";
  #else
    return "heltec";
  #endif
#else
    return "tdeck";
#endif
}

bool otaCheckLatestRelease(OtaCheckResult &out) {
    clearCheckResult(out);

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

    if (kTreatLatestAsUpdateForTesting) {
        out.updateAvailable = true;
    } else {
        out.updateAvailable = (compareVersionTags(APP_VERSION, out.latestTag) < 0);
    }
    out.ok = true;
    return true;
}

bool otaInstallLatestRelease(const char *tag,
                             char *errOut,
                             size_t errLen,
                             OtaInstallProgressCb progressCb) {
    if (g_otaNetworkGate != kOtaNetworkGateMagic) {
        return setErr(errOut, errLen, "OTA network blocked (non-worker mode)");
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

    char url[256] = {};
    buildAssetUrl(tagBuf, url, sizeof(url));

    WiFiClientSecure client;
    configureTlsClient(client);

    HTTPClient http;
    if (!http.begin(client, url)) {
        return setErr(errOut, errLen, "Failed to start OTA download");
    }

    http.setTimeout(30000);
    http.addHeader("User-Agent", "camillia-mt-ota");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code <= 0) {
        if (code == -1) {
            String tlsErr;
            buildTlsLowMemError(tlsErr);
            http.end();
            return setErr(errOut, errLen, tlsErr.c_str());
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
            Update.abort();
            http.end();
            return setErr(errOut, errLen, "OTA write failed");
        }

        written += wr;
        lastProgressMs = millis();
        if (progressCb) {
            progressCb(written, (contentLen > 0) ? (size_t)contentLen : 0);
        }
    }

    if (contentLen > 0 && written != (size_t)contentLen) {
        Update.abort();
        http.end();
        return setErr(errOut, errLen, "OTA write incomplete");
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
