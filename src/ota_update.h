#pragma once

#include <Arduino.h>

struct OtaCheckResult {
    bool ok;
    bool updateAvailable;
    char latestTag[48];
    char downloadUrl[256];
    char error[160];
};

typedef void (*OtaInstallProgressCb)(size_t writtenBytes, size_t totalBytes);

// Allows OTA networking only when explicitly enabled by the caller.
void otaSetNetworkAllowed(bool allowed);

// Selects which release channel OTA follows: UPDATE_CHANNEL_RELEASE (stable,
// the default) or UPDATE_CHANNEL_ALPHA (latest prerelease). On the alpha channel
// prerelease tags are treated as installable updates; on release they are never
// installed. Call before otaCheckLatestRelease / otaInstallLatestRelease.
void otaSetChannel(uint8_t channel);

// Device-specific release artifact slug (for example: tdeck, cardputer-cap).
const char *otaCurrentDeviceAssetSlug();

// Checks GitHub release metadata and computes the expected OTA binary URL.
bool otaCheckLatestRelease(OtaCheckResult &out);

// Downloads and installs the latest release binary for this device target.
// If tag is null/empty, it fetches the latest release tag first.
bool otaInstallLatestRelease(const char *tag,
                             char *errOut,
                             size_t errLen,
                             OtaInstallProgressCb progressCb = nullptr);
