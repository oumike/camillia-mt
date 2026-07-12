#include "ota_update.h"

#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <ctype.h>
#include <string.h>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

namespace {
constexpr uint32_t kReleaseCheckTimeoutMs = 12000;
constexpr const char *kLatestReleaseApiUrl = "https://api.github.com/repos/oumike/camillia-mt/releases/latest";
constexpr const char *kLatestVersionRawUrl = "https://raw.githubusercontent.com/oumike/camillia-mt/main/VERSION";
constexpr const char *kReleaseDownloadBaseUrl = "https://github.com/oumike/camillia-mt/releases/download/";

// Temporary behavior for OTA testing: any successfully-fetched latest release
// is treated as installable even when equal to current APP_VERSION.
constexpr bool kTreatLatestAsUpdateForTesting = true;

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

static bool fetchLatestTagFromVersionFile(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient raw;
    if (!raw.begin(client, kLatestVersionRawUrl)) {
        errOut = "Failed to start VERSION request";
        return false;
    }

    raw.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
    raw.addHeader("User-Agent", "camillia-mt-ota");

    int rawCode = raw.GET();
    if (rawCode <= 0) {
        errOut = "VERSION network error";
        raw.end();
        return false;
    }
    if (rawCode != HTTP_CODE_OK) {
        errOut = String("VERSION HTTP ") + String(rawCode);
        raw.end();
        return false;
    }

    tagOut = raw.getString();
    raw.end();
    trimAsciiWhitespace(tagOut);

    if (tagOut.length() == 0) {
        errOut = "VERSION file empty";
        return false;
    }

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

static bool fetchLatestReleaseTag(String &tagOut, String &errOut) {
    tagOut = "";
    errOut = "";

    if (WiFi.status() != WL_CONNECTED) {
        errOut = "WiFi not connected";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    String apiErr;
    HTTPClient http;
    if (!http.begin(client, kLatestReleaseApiUrl)) {
        apiErr = "Failed to start HTTPS request";
    } else {
        http.setTimeout((uint16_t)kReleaseCheckTimeoutMs);
        http.addHeader("User-Agent", "camillia-mt-ota");
        http.addHeader("Accept", "application/vnd.github+json");

        int code = http.GET();
        if (code <= 0) {
            apiErr = "Network error";
        } else if (code != HTTP_CODE_OK) {
            apiErr = String("Release API HTTP ") + String(code);
        } else {
            String body = http.getString();
            if (extractJsonStringField(body, "tag_name", tagOut) && tagOut.length() > 0) {
                http.end();
                return true;
            }
            apiErr = "Release tag not found";
        }
        http.end();
    }

    String versionErr;
    if (fetchLatestTagFromVersionFile(tagOut, versionErr)) {
        return true;
    }

    if (apiErr.length() && versionErr.length()) {
        errOut = apiErr + "; fallback failed (" + versionErr + ")";
    } else if (apiErr.length()) {
        errOut = apiErr;
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

bool otaInstallLatestRelease(const char *tag, char *errOut, size_t errLen) {
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
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, url)) {
        return setErr(errOut, errLen, "Failed to start OTA download");
    }

    http.setTimeout(30000);
    http.addHeader("User-Agent", "camillia-mt-ota");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code <= 0) {
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
    size_t written = Update.writeStream(stream);

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
