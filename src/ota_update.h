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

// Route generic heap allocations to PSRAM (one-time, permanent) so contiguous
// internal DRAM stays free for TLS handshake buffers (esp-sha). Shared by OTA and
// the MQTT bridge — both need a TLS handshake to succeed under memory pressure.
void otaPreferExternalHeap();

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
