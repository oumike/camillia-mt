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

// Selects the release channel the check and install paths follow:
// OTA_CHANNEL_STABLE (published releases only) or OTA_CHANNEL_ALPHA (also
// accepts prereleases). Call it whenever the setting is loaded or changed;
// it defaults to stable, so a caller that never sets it keeps the old
// behaviour. Values outside the enum are clamped to stable.
void otaSetChannel(uint8_t channel);
uint8_t otaCurrentChannel();

// True when the firmware currently running is itself a prerelease build, i.e.
// APP_VERSION carries a SemVer suffix. This is what OTA_CHANNEL_AUTO resolves
// against, and what lets a device flashed with an alpha image report the
// channel it is actually on.
bool otaRunningPrerelease();

// Resolves OTA_CHANNEL_AUTO to a concrete OTA_CHANNEL_STABLE/_ALPHA using the
// running build; passes an explicit channel through unchanged. UI should render
// this rather than the stored value, so what the row says matches what the
// update check will actually do.
uint8_t otaResolveChannel(uint8_t storedChannel);

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
