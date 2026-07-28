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

// Device-specific release artifact slug (for example: tdeck, cardputer-cap).
const char *otaCurrentDeviceAssetSlug();

// True when the flash layout can accept an update safely — the dual-slot table
// this project ships. False on a third-party installer's layout, where the
// spare OTA slot belongs to another firmware the user installed. Callers should
// skip checking and hide the update action when this is false; the install path
// refuses regardless.
bool otaLayoutSupportsUpdate();

// Checks GitHub release metadata and computes the expected OTA binary URL.
bool otaCheckLatestRelease(OtaCheckResult &out);

// Downloads and installs the latest release binary for this device target.
// If tag is null/empty, it fetches the latest release tag first.
bool otaInstallLatestRelease(const char *tag,
                             char *errOut,
                             size_t errLen,
                             OtaInstallProgressCb progressCb = nullptr);
