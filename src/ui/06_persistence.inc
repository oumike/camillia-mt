// SPDX-License-Identifier: GPL-3.0-or-later
// Part of main_lvgl.cpp: NVS persistence: Wi-Fi, logo, config flush and migration, channels, PKI keys.
// Included in order from main_lvgl.cpp as one translation unit; not built on its own.

static void saveKnownWifiNetworks() {
    if (kKnownWifiExtraCount <= 0) return;   // nothing to remember on this board
    uint8_t buf[sizeof(WifiNetsBlobHeader) + sizeof(s_cfgKnownWifiAdded)] = {};
    int n = s_cfgKnownWifiAddedCount;
    if (n < 0) n = 0;
    if (n > kKnownWifiExtraCount) n = kKnownWifiExtraCount;

    const WifiNetsBlobHeader hdr = { kWifiNetsBlobVersion, (uint16_t)n };
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), s_cfgKnownWifiAdded, sizeof(s_cfgKnownWifiAdded));

    Preferences p;
    if (!p.begin("camillia", false)) {
        Serial.println("[wifi] saved-networks write FAILED: cannot open NVS");
        return;
    }
    const size_t wrote = p.putBytes(kWifiNetsBlobKey, buf, sizeof(buf));
    p.end();
    if (wrote != sizeof(buf)) {
        Serial.printf("[wifi] saved-networks write FAILED: %u of %u bytes\n",
                      (unsigned)wrote, (unsigned)sizeof(buf));
    } else {
        Serial.printf("[wifi] remembered %d network(s)\n", n);
    }
}

// Preferences::getBytes() returns 0 for a stored blob longer than maxLen, which
// is what a newer build leaves behind after a downgrade. Every blob here is
// append-only, so read it whole and keep the prefix this build understands.
// Returns the number of bytes placed in buf.
static size_t prefsGetBytesPrefix(Preferences &p, const char *key, void *buf, size_t maxLen) {
    const size_t stored = p.getBytesLength(key);
    if (stored <= maxLen) return p.getBytes(key, buf, maxLen);
    uint8_t *tmp = (uint8_t *)malloc(stored);
    if (!tmp) return 0;
    size_t got = p.getBytes(key, tmp, stored);
    if (got > maxLen) got = maxLen;
    memcpy(buf, tmp, got);
    free(tmp);
    return got;
}

static void loadKnownWifiNetworks() {
    s_cfgKnownWifiAddedCount = 0;
    memset(s_cfgKnownWifiAdded, 0, sizeof(s_cfgKnownWifiAdded));
    // Boards without a remembered list stay at zero. A blob left by an older
    // build, or by a config imported from another board, is simply not read.
    if (kKnownWifiExtraCount <= 0) return;

    Preferences p;
    if (!p.begin("camillia", true)) return;
    if (!p.isKey(kWifiNetsBlobKey)) { p.end(); return; }

    uint8_t buf[sizeof(WifiNetsBlobHeader) + sizeof(s_cfgKnownWifiAdded)] = {};
    const size_t got = prefsGetBytesPrefix(p, kWifiNetsBlobKey, buf, sizeof(buf));
    p.end();
    if (got < sizeof(WifiNetsBlobHeader)) return;

    WifiNetsBlobHeader hdr = {};
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.version != kWifiNetsBlobVersion) {
        Serial.printf("[wifi] saved-networks blob v%u unusable (want v%u)\n",
                      (unsigned)hdr.version, (unsigned)kWifiNetsBlobVersion);
        return;
    }

    int n = (int)hdr.count;
    if (n < 0) n = 0;
    if (n > kKnownWifiExtraCount) n = kKnownWifiExtraCount;
    // Only copy what actually arrived: a blob written by a build with a smaller
    // entry array must not be read past its end.
    const size_t avail = got - sizeof(WifiNetsBlobHeader);
    const int fits = (int)(avail / sizeof(KnownWifiEntry));
    if (n > fits) n = fits;
    memcpy(s_cfgKnownWifiAdded, buf + sizeof(hdr), (size_t)n * sizeof(KnownWifiEntry));

    // Drop anything that did not survive the round trip intact, so a corrupt
    // record cannot show up in the picker as a nameless row.
    int w = 0;
    for (int i = 0; i < n; i++) {
        s_cfgKnownWifiAdded[i].ssid[sizeof(s_cfgKnownWifiAdded[i].ssid) - 1] = '\0';
        s_cfgKnownWifiAdded[i].pass[sizeof(s_cfgKnownWifiAdded[i].pass) - 1] = '\0';
        if (!s_cfgKnownWifiAdded[i].ssid[0]) continue;
        if (w != i) s_cfgKnownWifiAdded[w] = s_cfgKnownWifiAdded[i];
        w++;
    }
    for (int i = w; i < kKnownWifiExtraCount; i++) {
        memset(&s_cfgKnownWifiAdded[i], 0, sizeof(s_cfgKnownWifiAdded[i]));
    }
    s_cfgKnownWifiAddedCount = w;
    Serial.printf("[wifi] loaded %d remembered network(s)\n", w);
}

// What the boot-time settings load actually did. loadConfigFromPrefs() runs
// before USB CDC enumerates, so its Serial output is never seen on these
// boards (same reason logNvsHealth() is repeated at the end of setup()).
// Recorded here and restated alongside that repeat.
static char s_cfgLoadReport[112] = "loadConfigFromPrefs() never ran";

// Keys in "camillia" that are not part of the blob and must survive migration:
// the node identity, and the one-shot flags read before any config is loaded.
static bool camilliaKeyIsPreserved(const char *key) {
    static const char *const kKeep[] = {
        kCfgBlobKey, "nodeId", "pub25519", "priv25519", "pubKey", "privKey",
        "webCfgEnabled", "wifiForceAp",
#if HAS_VNC_HOST
        "vncEnabled",
#endif
        kPrefSkipWebAutoOnce, kPrefOtaWorkerOnce,
    };
    for (size_t i = 0; i < sizeof(kKeep) / sizeof(kKeep[0]); i++) {
        if (kKeep[i] && strcmp(key, kKeep[i]) == 0) return true;
    }
    return false;
}

static nvs_iterator_t nvsEntryFindCompat(const char *partition,
                                         const char *namespaceName) {
#if ESP_IDF_VERSION_MAJOR >= 5
    nvs_iterator_t iterator = nullptr;
    return nvs_entry_find(partition, namespaceName, NVS_TYPE_ANY, &iterator)
               == ESP_OK ? iterator : nullptr;
#else
    return nvs_entry_find(partition, namespaceName, NVS_TYPE_ANY);
#endif
}

static nvs_iterator_t nvsEntryNextCompat(nvs_iterator_t iterator) {
#if ESP_IDF_VERSION_MAJOR >= 5
    return nvs_entry_next(&iterator) == ESP_OK ? iterator : nullptr;
#else
    return nvs_entry_next(iterator);
#endif
}

// Reclaims the per-key settings written by builds that predate the blob.
// Enumerated rather than listed by name, so keys dropped from the writer along
// the way are reclaimed too. Collects first and deletes after: removing entries
// while an NVS iterator is open is not safe.
static int removeLegacyConfigKeys() {
    // Heap, not a static: this runs once ever, on the boot that migrates, and a
    // permanent 1.5 KB for a one-shot is RAM this board would rather keep.
    const int cap = 96;
    char (*doomed)[NVS_KEY_NAME_MAX_SIZE] =
        (char (*)[NVS_KEY_NAME_MAX_SIZE])malloc((size_t)cap * NVS_KEY_NAME_MAX_SIZE);
    if (!doomed) return 0;
    int n = 0;

    nvs_iterator_t it = nvsEntryFindCompat("nvs", "camillia");
    while (it && n < cap) {
        nvs_entry_info_t info = {};
        nvs_entry_info(it, &info);
        if (!camilliaKeyIsPreserved(info.key)) {
            strncpy(doomed[n], info.key, NVS_KEY_NAME_MAX_SIZE - 1);
            doomed[n][NVS_KEY_NAME_MAX_SIZE - 1] = '\0';
            n++;
        }
        it = nvsEntryNextCompat(it);
    }
    if (it) nvs_release_iterator(it);   // loop stopped early; iterator still open
    if (n <= 0) { free(doomed); return 0; }

    Preferences p;
    if (!p.begin("camillia", false)) { free(doomed); return 0; }
    for (int i = 0; i < n; i++) p.remove(doomed[i]);
    p.end();
    free(doomed);
    return n;
}

// ── Pre-configuration from the web flasher ──────────────────────────────────
// The flasher's Customize dialog sends `cfg set` / `cfg commit` over serial
// right after it writes the firmware, which on a fresh install is while the
// onboarding modal is up. Committing then cannot touch the "camillia"
// namespace: its absence is what marks first boot, and writing it would skip
// onboarding on the next boot. So a first-boot commit goes to a namespace of
// its own, is applied to s_cfg at every load until a boot is no longer the
// first, and is reclaimed on the first boot that is not. onboardingFinalize()
// then saves it with everything else.
static constexpr const char *kProvNs = "cm_prov";
static bool s_presetProvisioned = false;   // onboarding must not replace it
static int  s_cfgStagedPreset = -1;        // `cfg set preset`, until `cfg commit`
static int  s_cfgStagedSlot = -1;          // `cfg set slot`; 0 = the preset's own

static bool provNamespacePresent() {
    nvs_iterator_t it = nvsEntryFindCompat("nvs", kProvNs);
    if (!it) return false;
    nvs_release_iterator(it);
    return true;
}

// A pinned slot (1-based, as Meshtastic's channel_num) is the preset's modem
// settings as custom ones: the preset path always hashes for its slot.
static void applyProvisionedPreset(uint8_t preset, uint8_t slot = 0) {
    s_cfg.modemPreset = preset;
    s_cfg.loraUsePreset = slot == 0;
    if (slot) {
        s_cfg.loraCustomBwKhz = loraBwToCode(kPresets[preset].bw);
        s_cfg.loraCustomSf = kPresets[preset].sf;
        s_cfg.loraCustomCr = kPresets[preset].cr;
        s_cfg.loraCustomSlot = slot;
    }
    applyPresetParams(s_cfg);
    s_presetProvisioned = true;
}

// Called once config is loaded. Probes with the iterator rather than a
// read-only Preferences open, which logs NOT_FOUND on every boot without it.
static void loadProvisionedConfig() {
    if (!provNamespacePresent()) return;
    Preferences p;
    if (!s_firstBoot) {
        if (p.begin(kProvNs, false)) {
            p.clear();
            p.end();
        }
        Serial.println("[cfg] provisioning consumed - cleared");
        return;
    }
    if (!p.begin(kProvNs, true)) return;
    const uint8_t preset = p.getUChar("preset", PRESET_COUNT);
    const uint8_t slot = p.getUChar("slot", 0);
    p.end();
    if (preset < PRESET_COUNT && presetUsableOnThisRadio(preset)) {
        applyProvisionedPreset(preset, slot);
        Serial.printf("[cfg] provisioned preset=%s slot=%u\n", kPresets[preset].channelName, slot);
    }
    // A community broker from the flasher, switched on as `cfg commit` does.
    if (p.begin(kProvNs, true)) {
        if (p.isKey("mqHost") && p.isKey("mqRoot")) {
            utf8util::copyTruncate(s_cfg.mqttServer, sizeof(s_cfg.mqttServer), p.getString("mqHost").c_str());
            utf8util::copyTruncate(s_cfg.mqttUser, sizeof(s_cfg.mqttUser), p.getString("mqUser", "").c_str());
            utf8util::copyTruncate(s_cfg.mqttPass, sizeof(s_cfg.mqttPass), p.getString("mqPass", "").c_str());
            utf8util::copyTruncate(s_cfg.mqttRoot, sizeof(s_cfg.mqttRoot), p.getString("mqRoot").c_str());
            s_cfg.mqttPort = p.getUShort("mqPort", 1883);
            s_cfg.mqttEncryption = p.getBool("mqEnc", true);
            s_cfg.mqttEnabled = true;
            if (s_cfg.wifiSsid[0]) s_cfg.wifiEnabled = true;
            Serial.printf("[cfg] provisioned mqtt=%s:%u\n", s_cfg.mqttServer, (unsigned)s_cfg.mqttPort);
        }
        if (p.isKey("ok2mq")) s_cfg.okToMqtt = p.getChar("ok2mq", 0) != 0;
        p.end();
    }
}

// Boot logo, also from the flasher: a PNG drawn on the splash card in place of
// the flower. NVS rather than the filesystem because the splash is drawn before
// either filesystem is mounted. kLogoMaxBytes keeps it to ~260 of the
// partition's ~2000 entries.
static constexpr const char *kLogoNs = "cm_logo";
static constexpr const char *kLogoKey = "png";
static constexpr size_t kLogoMaxBytes = 8192;

// Allocates; the caller frees. nullptr when no logo is stored.
static uint8_t *loadStoredLogo(size_t &len) {
    len = 0;
    nvs_iterator_t it = nvsEntryFindCompat("nvs", kLogoNs);
    if (!it) return nullptr;
    nvs_release_iterator(it);
    Preferences p;
    if (!p.begin(kLogoNs, true)) return nullptr;
    const size_t n = p.getBytesLength(kLogoKey);
    uint8_t *buf = (n > 0 && n <= kLogoMaxBytes) ? (uint8_t *)malloc(n) : nullptr;
    if (buf && p.getBytes(kLogoKey, buf, n) == n) {
        len = n;
    } else {
        free(buf);
        buf = nullptr;
    }
    p.end();
    return buf;
}

// Width and height from a PNG's IHDR; false when it is not one.
static bool pngDimensions(const uint8_t *data, size_t len, int &w, int &h) {
    static const uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (!data || len < 24 || memcmp(data, kSig, 8) != 0 || memcmp(data + 12, "IHDR", 4) != 0) {
        return false;
    }
    auto be32 = [&](size_t o) {
        return (uint32_t)data[o] << 24 | (uint32_t)data[o + 1] << 16
             | (uint32_t)data[o + 2] << 8 | (uint32_t)data[o + 3];
    };
    w = (int)be32(16);
    h = (int)be32(20);
    return w > 0 && h > 0;
}

static void persistConfigToPrefs() {
    meshBeaconConfigSaved();   // beacon payload follows the saved setup
    // s_cfg is the single source of truth for credentials; nothing is folded in
    // from web config here.
    //
    // This used to copy webCfgWifiSsid()/webCfgWifiPass() over s_cfg whenever
    // they were non-empty, on the theory that web edits landed there first. They
    // do not: both web paths that accept credentials (handlePostSave and
    // handlePostOnboard) already write gCfg->wifiSsid themselves before invoking
    // this save. What those globals actually hold the rest of the time is a
    // *mirror* seeded from the config when the server starts — so the fold-back
    // could not tell a pending edit from a stale copy, and the stale copy won.
    //
    // The effect was that any network joined on the device — the WiFi picker's
    // connect dialog, or a promoted association — was written to the blob and
    // then immediately overwritten by whatever SSID web config had last mirrored,
    // so the new network never survived a reboot.

    // Sized off the array, never off a pointer to it: a decayed `uint8_t *buf`
    // here made sizeof() four bytes, so saves stored the header alone and every
    // reboot after one fell back to defaults.
    const CfgBlobHeader hdr = { kCfgBlobVersion, (uint16_t)sizeof(RhinoConfig) };
    memcpy(s_cfgBlobBuf, &hdr, sizeof(hdr));
    memcpy(s_cfgBlobBuf + sizeof(hdr), &s_cfg, sizeof(s_cfg));

    Preferences p;
    if (!p.begin("camillia", false)) {
        Serial.println("[cfg] save FAILED: cannot open NVS namespace 'camillia'");
        return;
    }
    const size_t wrote = p.putBytes(kCfgBlobKey, s_cfgBlobBuf, sizeof(s_cfgBlobBuf));
    // Read during early boot, before the blob is unpacked, so they stay keys.
#if HAS_RUNTIME_ORIENTATION
    p.putUChar("uiOrient", (uint8_t)(s_cfg.uiOrientation < UI_ORIENT_COUNT
                                     ? s_cfg.uiOrientation : UI_ORIENT_LANDSCAPE));
#endif
    p.putBool("wifiForceAp", wifiForceApMode());
    p.putBool("webCfgEnabled", s_webCfgEnabled);
#if HAS_VNC_HOST
    p.putBool("vncEnabled", s_vncEnabled);
#endif
    p.end();

    if (wrote != sizeof(s_cfgBlobBuf)) {
        Serial.printf("[cfg] save FAILED: wrote %u of %u bytes (NVS full?)\n",
                      (unsigned)wrote, (unsigned)sizeof(s_cfgBlobBuf));
    } else {
        // Success was silent, which made "did the save happen?" unanswerable
        // from a log. It is one line per debounced flush, not per keypress.
        Serial.printf("[cfg] saved %u bytes, wifiSsid=\"%s\" forceAp=%d\n",
                      (unsigned)wrote, s_cfg.wifiSsid, (int)wifiForceApMode());
    }
}

// Debounced save. A settings blob write is a synchronous flash operation, and
// NVS may compact a page while doing it — long enough to show as a hitch if it
// ran on every step of a slider. Coalescing also cuts flash wear, which matters
// on a 16 KB partition where there is little room to spread writes.
static constexpr uint32_t kConfigFlushDelayMs = 1500;
static bool     s_configDirty     = false;
static uint32_t s_configDirtyAtMs = 0;

static void markConfigDirty() {
    s_configDirty     = true;
    s_configDirtyAtMs = millis();
}

static void flushConfigIfDirty() {
    if (!s_configDirty) return;
    s_configDirty = false;
    persistConfigToPrefs();
}

// Everything debounced that must survive the reboot about to happen. Config was
// always flushed here; chat and DM transcripts are debounced the same way now,
// and would otherwise lose whatever landed inside the last quiet window. No-op
// when nothing is pending, so it is cheap to call on every restart path —
// including OTA worker mode, where the transcript managers are never allocated.
static void flushPersistentState() {
    flushConfigIfDirty();
    Channels.flushPersistence();
    DMs.flushPersistence();
}

#if defined(PLATFORM_LINUX)
// The CardputerZero app leaves on SIGINT/SIGTERM (the launcher's Home
// long-press) rather than losing power; linux/app/main_linux.cpp calls this.
void linuxAppFlushBeforeExit() { flushPersistentState(); }
#endif

static void serviceConfigFlush(uint32_t nowMs) {
    if (!s_configDirty) return;
    if ((uint32_t)(nowMs - s_configDirtyAtMs) < kConfigFlushDelayMs) return;
    flushConfigIfDirty();
}

// The channel plan, packed the same way as the config blob: 48 keys (six per
// channel) became one. Field order is fixed; extending a record means bumping
// the version, which falls back to the compiled channel defaults.
static constexpr const char *kChanBlobKey     = "ch";
static constexpr uint16_t    kChanBlobVersion = 1;

struct ChanBlobRecord {
    char    name[16];
    uint8_t key[32];
    uint8_t keyLen;
    uint8_t role;
    uint8_t flags;       // bit0 uplink, bit1 downlink, bit2 muted
    // Was `reserved`, so every blob written before per-channel location sharing
    // has it at 0. Position sharing therefore cannot simply be another bit in
    // `flags`: a 0 there is indistinguishable from "never stored", and it
    // defaults ON for the primary channel — every upgraded device would quietly
    // stop announcing its position. Bit0 marks the byte as written; only then is
    // bit1 believed, and an older blob keeps the compiled defaults instead.
    // bit2 is use_aead (Meshtastic 2.8.1). Off is its default, so unlike
    // shareLocation it would have been safe as a plain bit, but it lives here
    // with the other bits that postdate the record.
    uint8_t extFlags;    // bit0 extFlags valid, bit1 shareLocation, bit2 useAead
};

enum : uint8_t {
    CHAN_EXT_VALID          = 0x01,
    CHAN_EXT_SHARE_LOCATION = 0x02,
    CHAN_EXT_AEAD           = 0x04,
};

struct ChanBlob {
    uint16_t       version;
    uint16_t       count;
    ChanBlobRecord chans[MESH_CHANNELS];
    // Appended after the records rather than added to ChanBlobRecord, and this
    // is load-bearing: every record is 1-byte aligned, so growing the record
    // changes its stride and every stored blob would be read at the wrong
    // offsets from the second channel onward. A trailing section leaves older
    // blobs a valid prefix of this layout — they simply stop before it, and the
    // loader treats a missing section as "no channel has an override".
    //
    // Same reason the loader below parses by the *stored* count instead of
    // casting this struct over the bytes: when MESH_CHANNELS changes (#44) the
    // record array moves the trailing section, and only the stored count says
    // where it actually is.
    uint8_t        hopLimitPlus1[MESH_CHANNELS];   // 0 = unset, else hops + 1
};

// The stride the loader walks the record array with. If a field is ever added to
// ChanBlobRecord this fires, which is the point: every stored blob would then be
// read at the wrong offsets from the second channel onward, and the fix is a
// trailing section (as hopLimitPlus1 is) rather than a wider record.
static_assert(sizeof(ChanBlobRecord) == 52, "ChanBlobRecord stride changed - see ChanBlob");

// Shared by save and load for the same reasons as s_cfgBlobBuf.
static ChanBlob s_chanBlob;

static bool persistChannelsToPrefs() {
    meshBeaconConfigSaved();   // channel 0 is the beacon's offer
    ChanBlob &blob = s_chanBlob;
    memset(&blob, 0, sizeof(blob));
    blob.version = kChanBlobVersion;
    blob.count   = MESH_CHANNELS;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ck = CHANNEL_KEYS[i];
        const char *name = ck.name_buf[0] ? ck.name_buf : ck.name;
        if (name) {
            strncpy(blob.chans[i].name, name, sizeof(blob.chans[i].name) - 1);
        }
        const uint8_t len = (ck.keyLen <= sizeof(blob.chans[i].key)) ? ck.keyLen
                                                                    : (uint8_t)sizeof(blob.chans[i].key);
        memcpy(blob.chans[i].key, ck.key, len);
        blob.chans[i].keyLen = len;
        blob.chans[i].role   = ck.role;
        blob.chans[i].flags  = (uint8_t)((ck.uplinkEnabled   ? 1 : 0)
                                       | (ck.downlinkEnabled ? 2 : 0)
                                       | (ck.muted           ? 4 : 0));
        blob.chans[i].extFlags = (uint8_t)(CHAN_EXT_VALID
                                         | (ck.shareLocation ? CHAN_EXT_SHARE_LOCATION : 0)
                                         | (ck.useAead       ? CHAN_EXT_AEAD           : 0));
        blob.hopLimitPlus1[i] = ck.hopLimitPlus1;
    }

    Preferences cp;
    if (!cp.begin("mesh_ch", false)) {
        Serial.println("[cfg] channel save FAILED: cannot open NVS namespace 'mesh_ch'");
        return false;
    }
    // Preferences returns 0 on failure and never throws, so a full partition
    // would otherwise lose the channel plan silently: it survives in RAM until
    // the next boot and is then simply gone.
    const size_t wrote = cp.putBytes(kChanBlobKey, &blob, sizeof(blob));
    cp.end();
    if (wrote != sizeof(blob)) {
        Serial.printf("[cfg] channel save FAILED: wrote %u of %u bytes (NVS full?)\n",
                      (unsigned)wrote, (unsigned)sizeof(blob));
        return false;
    }
    return true;
}

// Drops the per-channel keys written before the blob existed. Same collect-then-
// delete shape as removeLegacyConfigKeys(); everything but the blob goes.
static int removeLegacyChannelKeys() {
    const int cap = 64;   // heap for the same reason as removeLegacyConfigKeys()
    char (*doomed)[NVS_KEY_NAME_MAX_SIZE] =
        (char (*)[NVS_KEY_NAME_MAX_SIZE])malloc((size_t)cap * NVS_KEY_NAME_MAX_SIZE);
    if (!doomed) return 0;
    int n = 0;

    nvs_iterator_t it = nvsEntryFindCompat("nvs", "mesh_ch");
    while (it && n < cap) {
        nvs_entry_info_t info = {};
        nvs_entry_info(it, &info);
        if (strcmp(info.key, kChanBlobKey) != 0) {
            strncpy(doomed[n], info.key, NVS_KEY_NAME_MAX_SIZE - 1);
            doomed[n][NVS_KEY_NAME_MAX_SIZE - 1] = '\0';
            n++;
        }
        it = nvsEntryNextCompat(it);
    }
    if (it) nvs_release_iterator(it);
    if (n <= 0) { free(doomed); return 0; }

    Preferences p;
    if (!p.begin("mesh_ch", false)) { free(doomed); return 0; }
    for (int i = 0; i < n; i++) p.remove(doomed[i]);
    p.end();
    free(doomed);
    return n;
}

// ── One-shot fix-ups for settings already in NVS ─────────────────────────────
// Runs on the blob path only, after the stored bytes are in s_cfg and before
// applyLoadedConfigInvariants() coerces them. The difference between the two is
// worth keeping straight: invariants run on every load and clamp values that
// could be nonsense whatever wrote them, while this runs once per device and
// changes a value that was perfectly valid under the meaning it was written
// with.
//
// s_cfg.cfgEpoch is CFG_EPOCH_UNSET here when the stored blob predates the
// field — see the note on it in config_io.h. Each migration is therefore
// written as "if the device has not been through epoch N yet", and the epoch is
// stamped current at the end.
static void cfgMigrateStoredConfig() {
#if HAS_NAV_BAR_TOGGLE && !HAS_TOUCH
    // Issue #92. The bar is new to this board, and the right fresh-install state
    // is off — but cfgInitDefaults() has always written navBarEnabled into the
    // blob on every board, including the ones that could not draw a bar at all,
    // and what it wrote was true. Left alone, every Cardputer, M9 and Pager in
    // the field would read that true back out and come up with a bar it was
    // never asked for, on the boards least able to spare the pixels.
    //
    // So it is forced off once, which is the "make it a decision in the diff"
    // half of the ticket. The cost is an early adopter who had already put
    // navBar: true in an imported YAML losing that setting a single time; the
    // alternative is every other owner of these three boards being handed a
    // layout change by an update, which is the worse trade.
    if (s_cfg.cfgEpoch < CFG_EPOCH_NAV_BAR && s_cfg.navBarEnabled) {
        s_cfg.navBarEnabled = false;
        Serial.println("[cfg] migrate: nav bar forced off once (issue #92) - "
                       "this board's stored value predates the setting");
    }
#endif

    if (s_cfg.cfgEpoch != CFG_EPOCH_CURRENT) {
        s_cfg.cfgEpoch = CFG_EPOCH_CURRENT;
        // Debounced rather than written here: this runs inside the boot-time
        // load with NVS open read-only, and the flush is a synchronous flash
        // write. Recording it matters — until the epoch is stored, every boot
        // re-runs the migration above, which would undo the setting the moment
        // a user switched the bar on and rebooted without anything else saving.
        markConfigDirty();
    }
}

// Cross-setting rules every load path must finish with, whichever storage
// format it read: MQTT and web config need WiFi, and the radio parameters are
// always re-derived from region + preset rather than trusted from storage.
static void applyLoadedConfigInvariants() {
    // Chat server (replacing the S&F client in the same bytes): an unmigrated
    // flags byte means the node id slot still holds an S&F router pin.
    if ((s_cfg.chatServerFlags & 0xF0) != CHAT_SERVER_FLAGS_MAGIC) {
        s_cfg.chatServerNodeId = 0;
        s_cfg.chatServerFlags = CHAT_SERVER_FLAGS_MAGIC;
    }
    if (s_cfg.chatServerMode > CHAT_SERVER_MODE_MANUAL) s_cfg.chatServerMode = CHAT_SERVER_MODE_AUTO;

#if HAS_RUNTIME_ORIENTATION
    // The blob field is a mirror, not the home. A device upgrading from a build
    // that predates it reads zero out of the short blob whichever way its panel
    // is actually running, so the key that drove setRotation() this boot is the
    // one that wins — otherwise a vertical unit would show "Landscape" in
    // settings and export it to YAML while plainly being portrait.
    s_cfg.uiOrientation = s_uiOrient;
#endif
    // UI language (issue #99). Every load path comes through here before any
    // UI is built, so TR() answers in the stored language from the first
    // label. An index this build does not know -- a newer build's language,
    // restored onto an older one -- reads as English rather than past the end.
    if (s_cfg.uiLanguage >= LANG_COUNT) s_cfg.uiLanguage = LANG_EN;
    i18nSetLang(s_cfg.uiLanguage);
#if defined(DEVICE_CARDPUTER_ZERO)
    // The system's Wi-Fi and GPS (see initCfgActions()): Camillia always uses
    // them as the system has them, whatever an older build or an imported
    // config stored. Whether and how precisely position is shared stays ours.
    s_cfg.wifiEnabled         = true;
    s_cfg.gpsEnabled          = true;
    s_cfg.gpsDutyCycleEnabled = (bool)MY_GPS_DUTY_CYCLE;
    s_cfg.gpsPollIntervalS    = MY_GPS_POLL_S;
#endif
#if HAS_SYSTEM_SETTINGS
    // The system's display, clock and battery settings: taken from the
    // launcher each load, whatever was stored or imported, so a change made in
    // its Settings app holds the next time Camillia starts. The clock is the
    // OS's, kept by its own NTP; Camillia never sets it. Power saving is the
    // ESP32's light sleep, and the gauge needs no trim.
    s_cfg.brightness    = cfgCoerceBrightness(czeroSystemBrightnessPct());
    s_cfg.screenOnSecs  = czeroSystemScreenOffSecs();
    s_cfg.timeSource    = TIME_SOURCE_AUTO;
    s_cfg.battCalTrim   = 0;
    s_cfg.isPowerSaving = false;
#endif
    if (!s_cfg.wifiEnabled) {
        s_cfg.mqttEnabled = false;
        s_webCfgEnabled   = false;
    }
    // Anything but a known source would silently disable both NTP and GPS.
    s_cfg.timeSource = cfgCoerceTimeSource(s_cfg.timeSource);
    // Coerced and mirrored here rather than only in the loop, so the first frame
    // drawn after a load already reads in the format the user chose.
    s_cfg.clockFormat = cfgCoerceClockFormat((int)s_cfg.clockFormat);
    liveClockSet12Hour(s_cfg.clockFormat == CLOCK_FORMAT_12H);
    // A trim out of range would scale the battery reading into nonsense; an
    // imported YAML or a hand-edited value is enough to get there.
    s_cfg.battCalTrim = cfgCoerceBattCalTrim((int)s_cfg.battCalTrim);
    // A zero here — from an import, or from a blob written before these were
    // settings if the append-only rule is ever broken — would mean a pattern of
    // no flashes at all, which reads as the feature being off.
    s_cfg.kbBlinkChanFlashes = cfgCoerceKbFlashes((int)s_cfg.kbBlinkChanFlashes);
    s_cfg.kbBlinkDmFlashes   = cfgCoerceKbFlashes((int)s_cfg.kbBlinkDmFlashes);
    // An unrecognised precision would decide how much of the operator's
    // location goes on the air, so it is coerced rather than trusted. 0 in
    // particular must not survive: applyPositionPrecision() treats it as "do
    // not coarsen", which is the opposite of what a stray 0 looks like.
    s_cfg.positionPrecision = positionPrecisionCoerce(s_cfg.positionPrecision);
    // A stray byte must not become Strict and silence the mesh.
    s_cfg.packetSigPolicy = sigPolicyCoerce(s_cfg.packetSigPolicy);
    meshSetSignPolicy(s_cfg.packetSigPolicy);
    // An off-list value would show on the row as "Never" while behaving as
    // something else entirely, and the row could not cycle back to it.
    s_cfg.notifyLightTimeoutS = cfgCoerceNotifyLightTimeout((long)s_cfg.notifyLightTimeoutS);
    s_cfg.kbLightTimeoutS = cfgCoerceKbLightTimeout((long)s_cfg.kbLightTimeoutS);
    s_cfg.powerLedPct = cfgCoercePowerLedPct(s_cfg.powerLedPct);
    // Off-list values would leave the lock screen lit for an interval the slider
    // cannot show or cycle back to. 0 survives coercion on purpose — it is the
    // "stay on the lock screen" choice, not an unset field.
    s_cfg.lockScreenOffSecs = cfgCoerceLockScreenOffSecs(s_cfg.lockScreenOffSecs);
    cfgCoerceMeshBeacon(s_cfg);
    cfgCoerceLicensed(s_cfg);
    applyPresetParams(s_cfg);
}

static void loadConfigFromPrefs() {
    Preferences prefs;
    if (!prefs.begin("camillia", true)) {
        // Read-only open fails when the namespace has never been written
        // (i.e. a freshly flashed device with a blank NVS partition). Treat
        // that as first boot so onboarding fires before anything else opens
        // the namespace read-write and creates it.
        s_firstBoot = true;
        // Fresh install: bring the web config up by default. With no saved
        // WiFi credentials it starts the onboarding AP, so the device is
        // configurable from a phone/browser out of the box.
        s_webCfgEnabled = true;
        snprintf(s_cfgLoadReport, sizeof(s_cfgLoadReport),
                 "no 'camillia' namespace - first boot, defaults");
        return;
    }

    // Blob path: everything below this point is the pre-blob per-key reader,
    // kept only so a device upgrading from an older build can be migrated.
    if (prefs.isKey(kCfgBlobKey)) {
        s_firstBoot = false;
        uint8_t *buf = s_cfgBlobBuf;
        memset(buf, 0, sizeof(s_cfgBlobBuf));
        const size_t got = prefsGetBytesPrefix(prefs, kCfgBlobKey, buf, sizeof(s_cfgBlobBuf));
        // Both live outside the blob because they are consulted before config
        // is unpacked; read them here so this path ends up in the same state as
        // the legacy one below.
        s_webCfgEnabled = prefs.getBool("webCfgEnabled", false);
#if HAS_VNC_HOST
        s_vncEnabled = prefs.getBool("vncEnabled", false);
#endif
        const bool forceAp = prefs.getBool("wifiForceAp", false);
        prefs.end();
        if (forceAp) {
            s_wifiUsingKnownOverride = true;
            strncpy(s_wifiSelectedSsid, kForceApSsid, sizeof(s_wifiSelectedSsid) - 1);
            s_wifiSelectedSsid[sizeof(s_wifiSelectedSsid) - 1] = '\0';
            s_wifiSelectedPass[0] = '\0';
        }

        CfgBlobHeader hdr = {};
        if (got >= sizeof(hdr)) memcpy(&hdr, buf, sizeof(hdr));
        const size_t payload = (got >= sizeof(hdr)) ? (got - sizeof(hdr)) : 0;
        if (hdr.version != kCfgBlobVersion || payload == 0) {
            // Written by a build whose struct layout we cannot interpret.
            // Defaults are already in s_cfg; better that than misread bytes.
            Serial.printf("[cfg] settings blob v%u unusable (want v%u) - using defaults\n",
                          (unsigned)hdr.version, (unsigned)kCfgBlobVersion);
            snprintf(s_cfgLoadReport, sizeof(s_cfgLoadReport),
                     "blob UNUSABLE: got=%u v%u want v%u payload=%u - defaults",
                     (unsigned)got, (unsigned)hdr.version,
                     (unsigned)kCfgBlobVersion, (unsigned)payload);
            return;
        }
        // Append-only rule: a shorter blob fills the front of the struct and
        // any field added since keeps its default.
        const size_t copy = (payload < sizeof(RhinoConfig)) ? payload : sizeof(RhinoConfig);
        // Planted before the copy, not after: a blob written before cfgEpoch
        // existed stops short of it, so whatever is sitting here afterwards is
        // the signal cfgMigrateStoredConfig() reads. cfgInitDefaults() left it
        // at CFG_EPOCH_CURRENT, which would have read as "already migrated" on
        // exactly the devices that have not been.
        s_cfg.cfgEpoch = CFG_EPOCH_UNSET;
        memcpy(&s_cfg, buf + sizeof(hdr), copy);
        cfgMigrateStoredConfig();
        applyLoadedConfigInvariants();
        snprintf(s_cfgLoadReport, sizeof(s_cfgLoadReport),
                 "blob ok: got=%u copied=%u/%u wifiSsid=\"%s\" forceAp=%d",
                 (unsigned)got, (unsigned)copy, (unsigned)sizeof(RhinoConfig),
                 s_cfg.wifiSsid, (int)forceAp);
        return;
    }

    // First-boot detection: a freshly-flashed device has no persisted node
    // identity. The presence of a saved nodeLong key is our marker that
    // onboarding has already been completed at least once.
    s_firstBoot = !prefs.isKey("nodeLong");

    auto getStringIfKey = [&](const char *key) -> String {
        return prefs.isKey(key) ? prefs.getString(key, "") : String();
    };
    auto getFloatIfKey = [&](const char *key, float defaultVal) -> float {
        return prefs.isKey(key) ? prefs.getFloat(key, defaultVal) : defaultVal;
    };

    String nodeLong = getStringIfKey("nodeLong");
    if (nodeLong.length()) {
        // Clamped on the way in, not just at the input paths. A name saved by an
        // older build can be up to 39 bytes, and the input caps added for 2.8
        // only govern new edits — without this, an upgrading user keeps a name
        // we then announce and every 2.8 node clips to 24 bytes on display.
        // Upstream truncates to 24 "before storing or rebroadcasting", so doing
        // it at the store boundary is the same rule, not a stricter one.
        utf8util::copyTruncate(s_cfg.nodeLong, MESH_LONG_NAME_MAX_BYTES + 1,
                               nodeLong.c_str());
        if (strcmp(s_cfg.nodeLong, nodeLong.c_str()) != 0) {
            Serial.printf("[cfg] long name truncated to %d bytes for Meshtastic 2.8: "
                          "\"%s\" -> \"%s\"\n",
                          MESH_LONG_NAME_MAX_BYTES, nodeLong.c_str(), s_cfg.nodeLong);
        }
    }
    String nodeShort = getStringIfKey("nodeShort");
    if (nodeShort.length()) {
        utf8util::copyTruncate(s_cfg.nodeShort, sizeof(s_cfg.nodeShort), nodeShort.c_str());
    }

    float f = getFloatIfKey("loraFreq", 0.0f);
    if (f > 0.0f) s_cfg.loraFreq = f;
    f = getFloatIfKey("loraBw", 0.0f);
    if (f > 0.0f) s_cfg.loraBw = f;

    uint8_t u = prefs.getUChar("loraSf", 0);
    if (u) s_cfg.loraSf = u;
    u = prefs.getUChar("loraCr", 0);
    if (u) s_cfg.loraCr = u;
    if (s_cfg.loraCr == 8 && s_cfg.loraSf == 11 && s_cfg.loraBw == 250.0f) {
        s_cfg.loraCr = 5;
    }
    u = prefs.getUChar("loraPower", 0);
    if (u) s_cfg.loraPower = u;
    u = prefs.getUChar("loraHopLim", 0);
    if (u) s_cfg.loraHopLimit = u;
    u = prefs.getUChar("modemPreset", PRESET_COUNT);
    if (u < PRESET_COUNT) s_cfg.modemPreset = u;

    if (prefs.isKey("gpsEnabled")) s_cfg.gpsEnabled = prefs.getBool("gpsEnabled");
    int32_t i = prefs.getInt("latI", 0);
    if (i) s_cfg.latI = i;
    i = prefs.getInt("lonI", 0);
    if (i) s_cfg.lonI = i;
    i = prefs.getInt("alt", -1);
    if (i >= 0) s_cfg.alt = i;

    uint8_t ro = prefs.getUChar("devRole", 0xFF);
    if (ro != 0xFF) s_cfg.deviceRole = cfgCoerceDeviceRole(ro);
    ro = prefs.getUChar("rebroadcast", 0xFF);
    if (ro != 0xFF) s_cfg.rebroadcastMode = ro;

    if (prefs.isKey("okToMqtt")) s_cfg.okToMqtt = prefs.getBool("okToMqtt");
    if (prefs.isKey("ignoreMqtt")) s_cfg.ignoreMqtt = prefs.getBool("ignoreMqtt");

    uint32_t ul = prefs.getULong("nodeInfoIntv", 0);
    if (ul) s_cfg.nodeInfoIntervalS = ul;
    ul = prefs.getULong("posIntv", 0);
    if (ul) s_cfg.posIntervalS = ul;
    if (prefs.isKey("gpsPollS")) {
        ul = prefs.getULong("gpsPollS", 0);
        s_cfg.gpsPollIntervalS = (uint32_t)constrain((long)ul, (long)0, (long)3600);
    } else if (prefs.isKey("gpsPollMs")) {
        ul = prefs.getULong("gpsPollMs", 0);
        s_cfg.gpsPollIntervalS = (ul == 0) ? 0 : (uint32_t)constrain((long)((ul + 999UL) / 1000UL), (long)0, (long)3600);
    }

    String region = getStringIfKey("region");
    if (region.length()) {
        strncpy(s_cfg.region, region.c_str(), sizeof(s_cfg.region) - 1);
        s_cfg.region[sizeof(s_cfg.region) - 1] = '\0';
    }
    String tz = getStringIfKey("tzDef");
    if (tz.length()) {
        strncpy(s_cfg.tzDef, tz.c_str(), sizeof(s_cfg.tzDef) - 1);
        s_cfg.tzDef[sizeof(s_cfg.tzDef) - 1] = '\0';
    }
    String ntp = getStringIfKey("ntpServer");
    if (ntp.length()) {
        strncpy(s_cfg.ntpServer, ntp.c_str(), sizeof(s_cfg.ntpServer) - 1);
        s_cfg.ntpServer[sizeof(s_cfg.ntpServer) - 1] = '\0';
    }

    ul = prefs.getULong("screenOnSecs", 0);
    if (ul) s_cfg.screenOnSecs = ul;

    ro = prefs.getUChar("dispUnits", 0xFF);
    if (ro != 0xFF) s_cfg.displayUnits = ro;
    ro = prefs.getUChar("chatStyle", 0xFF);
    if (ro != 0xFF) s_cfg.chatStyle = cfgCoerceChatStyle(ro);
    ro = prefs.getUChar("chatNameSty", 0xFF);
    if (ro != 0xFF && ro <= CHAT_NAME_MAX) s_cfg.chatNameStyle = ro;
    if (prefs.isKey("chatColors")) s_cfg.chatColorsEnabled = prefs.getBool("chatColors");
    if (prefs.isKey("userMsgColor")) {
        uint8_t umc = prefs.getUChar("userMsgColor", 0xFF);
        s_cfg.userMsgColor = (umc <= 15) ? umc : 0xFF;
    }
    if (prefs.isKey("splashMelody")) s_cfg.splashMelodyEnabled = prefs.getBool("splashMelody");
    if (prefs.isKey("msgAlertSound")) {
        s_cfg.msgAlertSound = (uint8_t)constrain((int)prefs.getUChar("msgAlertSound"), 0, 3);
    } else if (prefs.isKey("msgAlertBeep")) {
        s_cfg.msgAlertSound = prefs.getBool("msgAlertBeep") ? MSG_ALERT_SOUND_DEFAULT : MSG_ALERT_SOUND_OFF;
    }
    ro = prefs.getUChar("uiTheme", 0xFF);
    if (ro != 0xFF && ro < UI_THEME_COUNT) s_cfg.uiTheme = ro;
    ro = prefs.getUChar("uiMode", 0xFF);
    if (ro != 0xFF && ro <= UI_MODE_LIGHT) s_cfg.uiMode = ro;

    s_cfg.wifiEnabled = prefs.getBool("wifiEnabled", s_cfg.wifiEnabled);
    if (prefs.isKey("mqttEnabled")) s_cfg.mqttEnabled = prefs.getBool("mqttEnabled");
    String mqttServer = getStringIfKey("mqttServer");
    if (mqttServer.length()) {
        strncpy(s_cfg.mqttServer, mqttServer.c_str(), sizeof(s_cfg.mqttServer) - 1);
        s_cfg.mqttServer[sizeof(s_cfg.mqttServer) - 1] = '\0';
    }
    String mqttUser = getStringIfKey("mqttUser");
    if (mqttUser.length()) {
        strncpy(s_cfg.mqttUser, mqttUser.c_str(), sizeof(s_cfg.mqttUser) - 1);
        s_cfg.mqttUser[sizeof(s_cfg.mqttUser) - 1] = '\0';
    }
    String mqttPass = getStringIfKey("mqttPass");
    if (mqttPass.length()) {
        strncpy(s_cfg.mqttPass, mqttPass.c_str(), sizeof(s_cfg.mqttPass) - 1);
        s_cfg.mqttPass[sizeof(s_cfg.mqttPass) - 1] = '\0';
    }
    String mqttRoot = getStringIfKey("mqttRoot");
    if (mqttRoot.length()) {
        strncpy(s_cfg.mqttRoot, mqttRoot.c_str(), sizeof(s_cfg.mqttRoot) - 1);
        s_cfg.mqttRoot[sizeof(s_cfg.mqttRoot) - 1] = '\0';
    }
    if (prefs.isKey("mqttEncrypt")) s_cfg.mqttEncryption = prefs.getBool("mqttEncrypt");
    if (prefs.isKey("mqttMapRpt")) s_cfg.mqttMapReport = prefs.getBool("mqttMapRpt");
    if (prefs.isKey("mqttPort")) s_cfg.mqttPort = prefs.getUShort("mqttPort");

    if (prefs.isKey("isPwrSaving")) s_cfg.isPowerSaving = prefs.getBool("isPwrSaving");

    if (prefs.isKey("telDevEn")) s_cfg.telDeviceEnabled = prefs.getBool("telDevEn");
    ul = prefs.getULong("telDevIntv", 0);
    if (ul) s_cfg.telDeviceIntervalS = ul;
    if (prefs.isKey("telEnvEn")) s_cfg.telEnvEnabled = prefs.getBool("telEnvEn");
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    // Temporary rollback: keep env telemetry disabled on Heltec until sensor
    // wiring/model detection is finalized. Overrides any saved preference.
    s_cfg.telEnvEnabled = false;
#endif
    ul = prefs.getULong("telEnvIntv", 0);
    if (ul) s_cfg.telEnvIntervalS = ul;
    if (prefs.isKey("nbrInfoEn")) s_cfg.neighborInfoEnabled = prefs.getBool("nbrInfoEn");
    ul = prefs.getULong("nbrInfoIntv", 0);
    if (ul) s_cfg.neighborInfoIntervalS = ul;
    if (prefs.isKey("nbrInfoLoRa")) s_cfg.neighborInfoOverLora = prefs.getBool("nbrInfoLoRa");
    if (s_cfg.telDeviceIntervalS < 3600UL) s_cfg.telDeviceIntervalS = 3600UL;
    if (s_cfg.telEnvIntervalS < 3600UL) s_cfg.telEnvIntervalS = 3600UL;
    if (s_cfg.neighborInfoIntervalS < NEIGHBORINFO_MIN_INTERVAL_S) {
        s_cfg.neighborInfoIntervalS = NEIGHBORINFO_MIN_INTERVAL_S;
    }

    if (prefs.isKey("snfClientEn"))
        s_cfg.chatServerMode = prefs.getBool("snfClientEn") ? CHAT_SERVER_MODE_AUTO : CHAT_SERVER_MODE_OFF;
    if (prefs.isKey("otaAutoChk")) s_cfg.otaAutoCheckEnabled = prefs.getBool("otaAutoChk");
    if (prefs.isKey("nodeArchive")) s_cfg.nodeArchiveEnabled = prefs.getBool("nodeArchive");
    if (prefs.isKey("autoFav")) s_cfg.autoFavoriteEnabled = prefs.getBool("autoFav");
    if (prefs.isKey("autoFavRange")) {
        uint32_t r = prefs.getULong("autoFavRange",
                                    cfgDefaultAutoFavRangeM(s_cfg.displayUnits));
        if (r > 0) s_cfg.autoFavoriteRangeM = r;
    }

    ul = prefs.getULong("nodeIdOvr", 0);
    if (ul) s_cfg.nodeIdOverride = (uint32_t)ul;
    ro = prefs.getUChar("brightness", 0xFF);
    if (ro != 0xFF) s_cfg.brightness = cfgCoerceBrightness(ro);
    ro = prefs.getUChar("fontSize", 0xFF);
    if (ro != 0xFF && ro <= FONT_SIZE_MAX) s_cfg.fontSize = ro;

    if (prefs.isKey("dbgAcks")) s_cfg.debugAcks = prefs.getBool("dbgAcks");
    if (prefs.isKey("dbgMsgs")) s_cfg.debugMessages = prefs.getBool("dbgMsgs");
    if (prefs.isKey("dbgGps")) s_cfg.debugGps = prefs.getBool("dbgGps");

    bool debugMonitor = s_cfg.debugAcks || s_cfg.debugMessages || s_cfg.debugGps;
    s_cfg.debugAcks = debugMonitor;
    s_cfg.debugMessages = debugMonitor;
    s_cfg.debugGps = debugMonitor;
    s_cfgDebugLog = debugMonitor;
    debugSetFlags(debugMonitor, debugMonitor, debugMonitor);

    String wifiSsid = getStringIfKey("wifiSsid");
    if (wifiSsid.length()) {
        strncpy(s_cfg.wifiSsid, wifiSsid.c_str(), sizeof(s_cfg.wifiSsid) - 1);
        s_cfg.wifiSsid[sizeof(s_cfg.wifiSsid) - 1] = '\0';
    }
    String wifiPass = getStringIfKey("wifiPass");
    if (wifiPass.length()) {
        strncpy(s_cfg.wifiPass, wifiPass.c_str(), sizeof(s_cfg.wifiPass) - 1);
        s_cfg.wifiPass[sizeof(s_cfg.wifiPass) - 1] = '\0';
    }
    s_cfg.webCfgAuthEnabled = prefs.getBool("webCfgAuth", s_cfg.webCfgAuthEnabled);
    // Default on for a device that has never been configured (blank NVS that
    // some other component already created, or a namespace without our keys):
    // the onboarding AP is the only way in before WiFi creds exist. Devices
    // that have been set up keep whatever they last persisted.
    s_webCfgEnabled = prefs.getBool("webCfgEnabled", s_firstBoot);
#if HAS_VNC_HOST
    s_vncEnabled = prefs.getBool("vncEnabled", false);
#endif

    // Restore the picker's "AP" choice by re-selecting the sentinel, so every
    // wifiForceApMode() caller sees it exactly as if the user had just picked it.
    if (prefs.getBool("wifiForceAp", false)) {
        s_wifiUsingKnownOverride = true;
        strncpy(s_wifiSelectedSsid, kForceApSsid, sizeof(s_wifiSelectedSsid) - 1);
        s_wifiSelectedSsid[sizeof(s_wifiSelectedSsid) - 1] = '\0';
        s_wifiSelectedPass[0] = '\0';
    }

    prefs.end();
    applyLoadedConfigInvariants();

    // Migration: what we just read came from ~80 individual keys. Rewrite it as
    // the blob and reclaim them — but only once the blob is confirmed readable,
    // so a failure here leaves the old keys as the surviving copy rather than
    // deleting settings we could not replace.
    if (!s_firstBoot) {
        persistConfigToPrefs();
        Preferences verify;
        bool ok = false;
        if (verify.begin("camillia", true)) {
            // Length, not just presence: a short write still creates the key, and
            // reclaiming the legacy keys against one would delete the only
            // complete copy of the settings.
            ok = (verify.getBytesLength(kCfgBlobKey) == sizeof(s_cfgBlobBuf));
            verify.end();
        }
        if (ok) {
            const int freed = removeLegacyConfigKeys();
            Serial.printf("[cfg] migrated settings to blob storage (%d legacy keys reclaimed)\n",
                          freed);
            snprintf(s_cfgLoadReport, sizeof(s_cfgLoadReport),
                     "legacy keys migrated to blob (%d reclaimed) wifiSsid=\"%s\"",
                     freed, s_cfg.wifiSsid);
        } else {
            Serial.println("[cfg] blob migration failed; keeping legacy keys");
            snprintf(s_cfgLoadReport, sizeof(s_cfgLoadReport),
                     "legacy keys read, blob migration FAILED wifiSsid=\"%s\"",
                     s_cfg.wifiSsid);
        }
    }
}

// Channels whose stored blob record had no key. The loader leaves a keyless
// record on the compiled default key, so licensed boot uses this to tell a key
// it has to strip again (nothing to save) from one that was really stored.
static uint32_t s_chanStoredKeylessMask = 0;

static void loadChannelsFromPrefs() {
    Preferences cp;
    // Open read-write so first boot can create namespace instead of logging NOT_FOUND.
    if (!cp.begin("mesh_ch", false)) return;

    if (cp.isKey(kChanBlobKey)) {
        ChanBlob &blob = s_chanBlob;
        memset(&blob, 0, sizeof(blob));
        const size_t got = prefsGetBytesPrefix(cp, kChanBlobKey, &blob, sizeof(blob));
        cp.end();

        // Parsed by offset from the stored count, not by reading the struct's
        // fields directly. getBytes() copies the stored bytes verbatim, so the
        // layout in this buffer is whichever version wrote it — and both the
        // record count (#44) and the trailing sections can differ from what this
        // build's struct describes. Only version, count and the record stride are
        // guaranteed fixed, and those are enough to find everything else.
        //
        // The old test was `got < sizeof(blob)`, which rejected any blob written
        // by a build with fewer channels or fewer sections. That is a silent
        // reset of every configured channel on upgrade, so it is deliberately
        // gone rather than merely relaxed.
        const uint8_t *raw = reinterpret_cast<const uint8_t *>(&blob);
        const size_t recOff   = offsetof(ChanBlob, chans);
        const size_t storedN  = blob.count;
        // Only the channels this build can hold have to be present.
        // prefsGetBytesPrefix() truncates at our struct size, so a blob written
        // by a build with *more* channels arrives cut short — demanding all of
        // its records would reject it outright and reset every channel, which is
        // the same data loss this parse exists to prevent, just in the downgrade
        // direction.
        const size_t readN    = (storedN < (size_t)MESH_CHANNELS) ? storedN : (size_t)MESH_CHANNELS;
        const size_t recBytes = storedN * sizeof(ChanBlobRecord);
        if (blob.version != kChanBlobVersion || storedN == 0
            || got < recOff + readN * sizeof(ChanBlobRecord)) {
            Serial.printf("[cfg] channel blob unusable (v%u, count %u, %u bytes) - using defaults\n",
                          (unsigned)blob.version, (unsigned)storedN, (unsigned)got);
            return;
        }
        if (storedN > (size_t)MESH_CHANNELS) {
            Serial.printf("[cfg] channel blob holds %u channels, this build has %d - extra ignored\n",
                          (unsigned)storedN, MESH_CHANNELS);
        }

        // Present only in blobs written after per-channel hop limits landed.
        // Absent means every channel follows the device default, which is what
        // the firmware did before the field existed.
        // Located from the *stored* record count, since that is where the writer
        // put it. On a truncated read those bytes never arrived, so this is
        // false and every channel falls back to the device default — a downgrade
        // keeps its names and keys and loses only the hop overrides.
        const size_t hopOff  = recOff + recBytes;
        const bool   haveHop = (got >= hopOff + storedN);
        if (got > recOff + recBytes && !haveHop) {
            Serial.printf("[cfg] channel blob: trailing %u bytes ignored\n",
                          (unsigned)(got - recOff - recBytes));
        }

        const int n = (int)readN;
        for (int i = 0; i < n; i++) {
            ChanBlobRecord r;
            memcpy(&r, raw + recOff + (size_t)i * sizeof(ChanBlobRecord), sizeof(r));
            if (r.name[0]) {
                strncpy(CHANNEL_KEYS[i].name_buf, r.name, sizeof(CHANNEL_KEYS[i].name_buf) - 1);
                CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
                CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
            }
            if (r.keyLen > 0 && r.keyLen <= sizeof(CHANNEL_KEYS[i].key)) {
                memcpy(CHANNEL_KEYS[i].key, r.key, r.keyLen);
                CHANNEL_KEYS[i].keyLen = r.keyLen;
            }
            if (r.keyLen == 0) s_chanStoredKeylessMask |= (1UL << i);
            CHANNEL_KEYS[i].role            = r.role;
            CHANNEL_KEYS[i].uplinkEnabled   = (r.flags & 1) != 0;
            CHANNEL_KEYS[i].downlinkEnabled = (r.flags & 2) != 0;
            CHANNEL_KEYS[i].muted           = (r.flags & 4) != 0;
            // Blobs written before the flag existed leave shareLocation on its
            // compiled default (primary on, secondary off) — see ChanBlobRecord.
            if (r.extFlags & CHAN_EXT_VALID) {
                CHANNEL_KEYS[i].shareLocation = (r.extFlags & CHAN_EXT_SHARE_LOCATION) != 0;
                CHANNEL_KEYS[i].useAead       = (r.extFlags & CHAN_EXT_AEAD) != 0;
            }
            if (haveHop) {
                const uint8_t p1 = raw[hopOff + (size_t)i];
                if (p1 <= 8) CHANNEL_KEYS[i].hopLimitPlus1 = p1;
            }
        }
        return;
    }

    // Pre-blob per-channel keys. Read them, then (below) re-save as a blob and
    // reclaim the 48 entries they occupy.
    bool migrated = false;
    for (int i = 0; i < MESH_CHANNELS; i++) {
        char key[8];

        snprintf(key, sizeof(key), "n%d", i);
        if (cp.isKey(key)) {
            String name = cp.getString(key, "");
            name.trim();
            if (name.length() > 0 && name.length() < sizeof(CHANNEL_KEYS[i].name_buf)) {
                strncpy(CHANNEL_KEYS[i].name_buf, name.c_str(), sizeof(CHANNEL_KEYS[i].name_buf) - 1);
                CHANNEL_KEYS[i].name_buf[sizeof(CHANNEL_KEYS[i].name_buf) - 1] = '\0';
                CHANNEL_KEYS[i].name = CHANNEL_KEYS[i].name_buf;
            }
        }

        snprintf(key, sizeof(key), "k%d", i);
        if (cp.isKey(key)) {
            uint8_t keyBuf[32];
            size_t keyLen = cp.getBytes(key, keyBuf, sizeof(keyBuf));
            if (keyLen > 0) {
                memcpy(CHANNEL_KEYS[i].key, keyBuf, keyLen);
                CHANNEL_KEYS[i].keyLen = (uint8_t)keyLen;
            }
        }

        snprintf(key, sizeof(key), "r%d", i);
        if (cp.isKey(key)) {
            uint8_t role = cp.getUChar(key, 0xFF);
            if (role != 0xFF) CHANNEL_KEYS[i].role = role;
        }

        snprintf(key, sizeof(key), "u%d", i);
        if (cp.isKey(key)) CHANNEL_KEYS[i].uplinkEnabled = cp.getBool(key);
        snprintf(key, sizeof(key), "d%d", i);
        if (cp.isKey(key)) CHANNEL_KEYS[i].downlinkEnabled = cp.getBool(key);
        snprintf(key, sizeof(key), "m%d", i);
        if (cp.isKey(key)) CHANNEL_KEYS[i].muted = cp.getBool(key);
        if (!migrated) {
            snprintf(key, sizeof(key), "n%d", i);
            migrated = cp.isKey(key);
        }
    }

    cp.end();

    // Only migrate if there was something stored — a first boot has no keys and
    // wants the compiled defaults, not a blob written over them.
    if (migrated) {
        persistChannelsToPrefs();
        Preferences verify;
        bool ok = false;
        if (verify.begin("mesh_ch", true)) {
            ok = verify.isKey(kChanBlobKey);
            verify.end();
        }
        if (ok) {
            Serial.printf("[cfg] migrated channels to blob storage (%d legacy keys reclaimed)\n",
                          removeLegacyChannelKeys());
        } else {
            Serial.println("[cfg] channel blob migration failed; keeping legacy keys");
        }
    }
}

static bool loadPkiPairFromPrefs(Preferences &prefs, const char *pubKeyName,
                                 const char *privKeyName,
                                 uint8_t pubOut[32], uint8_t privOut[32]) {
    if (!pubKeyName || !privKeyName) return false;
    if (prefs.getBytesLength(pubKeyName) != 32 || prefs.getBytesLength(privKeyName) != 32) {
        return false;
    }
    return (prefs.getBytes(pubKeyName, pubOut, 32) == 32)
        && (prefs.getBytes(privKeyName, privOut, 32) == 32);
}

static void persistPkiPair(Preferences &prefs, const uint8_t pubKey[32], const uint8_t privKey[32]) {
    prefs.putBytes("pub25519", pubKey, 32);
    prefs.putBytes("priv25519", privKey, 32);
    // The legacy pubKey/privKey names are still *read* by initPkiIdentity() so
    // an older device keeps its identity, but they are no longer written: two
    // more 32-byte blobs cost entries a 16 KB partition cannot spare, and
    // nothing downstream of this build reads them.
}

// An imported config can carry the node's identity keypair. cfgImportFromBuf()
// restores it into RAM only, and every import path reboots straight afterwards,
// so it has to reach NVS here — otherwise initPki() finds nothing stored on the
// next boot and generates a fresh identity, silently discarding the one that
// was just restored. Peers holding the old public key can then no longer send
// this node a PKI DM, which is most of the reason to restore a backup at all.
//
// Shared by every import path so they cannot drift: this was previously inline
// in onWebCfgSaved(), and the onboarding importer — the one path that matters
// most, since it runs on a fresh device with no keypair of its own — never had
// a copy of it.
static void persistImportedIdentityKeys(const char *tag) {
    if (!cfgImportRestoredKeys()) return;
    Preferences pkiPrefs;
    if (pkiPrefs.begin("camillia", false)) {
        persistPkiPair(pkiPrefs, myPubKey, myPrivKey);
        pkiPrefs.end();
        Serial.printf("[pki] restored identity keypair from imported config (%s)\n",
                      tag ? tag : "import");
    } else {
        Serial.println("[pki] restore FAILED: cannot open NVS namespace");
    }
}

static void initPkiIdentity() {
    memset(myPubKey, 0, sizeof(myPubKey));
    memset(myPrivKey, 0, sizeof(myPrivKey));

    Preferences prefs;
    if (!prefs.begin("camillia", false)) {
        Serial.println("[pki] failed to open NVS namespace");
        return;
    }

    bool loaded = loadPkiPairFromPrefs(prefs, "pub25519", "priv25519", myPubKey, myPrivKey);
    if (!loaded) {
        loaded = loadPkiPairFromPrefs(prefs, "pubKey", "privKey", myPubKey, myPrivKey);
    }

    bool valid = false;
    if (loaded) {
        uint8_t derivedPub[32] = {0};
        // A key stored unclamped (restored from a backup made before the import
        // clamped it) signs with a different key than its public key claims;
        // every peer rejects its signatures. Clamp and re-derive, as upstream's
        // regeneratePublicKey() now does. The node gets a new public key and,
        // where the ID is the key hash, a new node number.
        if (curve25519ClampPrivKey(myPrivKey)
            && Curve25519::eval(derivedPub, myPrivKey, nullptr)) {
            memcpy(myPubKey, derivedPub, 32);
            persistPkiPair(prefs, myPubKey, myPrivKey);
            Serial.println("[pki] stored private key was not clamped: clamped it, "
                           "public key and node number change");
        }
        if (Curve25519::eval(derivedPub, myPrivKey, nullptr)) {
            if (memcmp(derivedPub, myPubKey, 32) == 0) {
                valid = true;
            } else {
                uint8_t reversedPub[32];
                for (int i = 0; i < 32; i++) reversedPub[i] = myPubKey[31 - i];
                if (memcmp(derivedPub, reversedPub, 32) == 0) {
                    memcpy(myPubKey, derivedPub, 32);
                    persistPkiPair(prefs, myPubKey, myPrivKey);
                    valid = true;
                    Serial.println("[pki] corrected stored pubkey endianness");
                }
            }
        }
    }

    if (!valid) {
        Curve25519::dh1(myPubKey, myPrivKey);
        persistPkiPair(prefs, myPubKey, myPrivKey);
        Serial.println("[pki] generated new Curve25519 identity");
    }

    prefs.end();
}

// The node ID we last announced under, when it differs from the current one.
// Non-zero once the ID has moved — nodeIdFromPubKey toggled, an override typed,
// or a restored config carrying a different identity key. Loaded from prefs by
// deriveNodeId() and consumed by setSenderRemap() in setup().
static uint32_t s_prevNodeId = 0;

static void deriveNodeId() {
    uint32_t baseNodeId = 0;
    Preferences prefs;
    if (prefs.begin("camillia", false)) {
        baseNodeId = prefs.getUInt("nodeId", 0);
        if (baseNodeId == 0) {
            uint8_t mac[6] = {};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            baseNodeId = ((uint32_t)mac[2] << 24)
                       | ((uint32_t)mac[3] << 16)
                       | ((uint32_t)mac[4] << 8)
                       | (uint32_t)mac[5];
            prefs.putUInt("nodeId", baseNodeId);
        }
        prefs.end();
    }

    // Moved ahead of the derivation, which used to close this function. The
    // 2.8-style ID is a hash of the public key, so the key has to exist before
    // the ID can be computed from it. Everything else here is unaffected —
    // initPkiIdentity() never read s_myNodeId.
    initPkiIdentity();

    if (s_cfg.nodeIdOverride != 0) {
        // An explicit number outranks both derivations. Someone who typed an ID
        // wants that ID.
        s_myNodeId = s_cfg.nodeIdOverride;
    } else if (s_cfg.nodeIdFromPubKey) {
        // Meshtastic 2.8: my_node_num = crc32(public_key). Matching it is what
        // lets a 2.8 node verify a first-contact NodeInfo from us, because that
        // check recomputes this hash over the key in the packet and compares it
        // to the sender ID.
        const uint32_t derived = meshCrc32(myPubKey, 32);
        // A hash can land on zero or on the broadcast address. Both are
        // reserved, and falling back keeps us addressable rather than invisible.
        s_myNodeId = (derived == 0 || derived == 0xFFFFFFFFu) ? baseNodeId : derived;
        if (s_myNodeId != derived) {
            Serial.println("[lvgl] pubkey-derived node ID was reserved; kept MAC-derived ID");
        }
    } else {
        s_myNodeId = baseNodeId;
    }

    if (s_myNodeId == 0) {
        uint8_t mac[6] = {};
        if (esp_efuse_mac_get_default(mac) == ESP_OK) {
            s_myNodeId = ((uint32_t)mac[2] << 24)
                       | ((uint32_t)mac[3] << 16)
                       | ((uint32_t)mac[4] << 8)
                       | (uint32_t)mac[5];
        }
    }

    // Sign our broadcasts as this node (Meshtastic 2.8.1). Every path that
    // changes the key or the ID comes through here.
    //
    // Only when the ID is crc32(public key), as every 2.8.1 node's is. A 2.8.1
    // node that has no key for us yet accepts a signed NodeInfo only if that
    // hash matches the sender (verifyFirstContactNodeInfo()), and drops it
    // otherwise, under every signature policy -- so a MAC-derived ID that signs
    // could never be learned by a 2.8.1 node at all. Signing some packets but
    // not NodeInfo is no better: once a Balanced node has seen us sign, it
    // drops our unsigned NodeInfo broadcasts. Unsigned throughout, we are
    // exactly the 2.7 node every policy but Strict accepts.
    const bool idIsKeyHash = (s_myNodeId == meshCrc32(myPubKey, 32));
    xeddsaInitSigner(myPrivKey, idIsKeyHash ? s_myNodeId : 0);
    if (!idIsKeyHash) {
        Serial.println("[xeddsa] node ID is not crc32(public key): broadcasts go unsigned");
    }

    // Remember the ID we previously announced under, so chat history written by
    // the old identity can still be recognised as ours. Stored rather than
    // recomputed because the old value is unrecoverable once the setting moves:
    // the MAC-derived ID cannot be derived back out of a public key.
    //
    // Kept in prefs rather than in RAM, because the call that *detects* the
    // move is usually not the one the remap hangs off. Saving from web config
    // runs deriveNodeId() from onWebCfgSaved() and then reboots, while the only
    // setSenderRemap() call is in setup(): recording the move in RAM alone left
    // it in a call with no consumer downstream, and the reboot then read a
    // lastNodeId that had already been advanced and saw no move at all. The one
    // store that does need migrating therefore never got it, on the only path
    // that exposes the setting.
    //
    // It also has to outlive the boot that heals the history. loadPersisted()
    // rewrites the sender on the way in and leaves the file to the next save,
    // so a channel that receives nothing is never written back and still holds
    // the old ID several boots later. Re-applying a mapping whose source ID no
    // longer appears costs one comparison per loaded line, so it stays rather
    // than being cleared on first use. The exposure that buys is a peer who
    // later turns up using the ID we left, whose messages would read as ours;
    // that is the same window the one-shot version had, held open.
    if (prefs.begin("camillia", false)) {
        const uint32_t announced = prefs.getUInt("lastNodeId", 0);
        if (announced != 0 && announced != s_myNodeId) {
            prefs.putUInt("prevNodeId", announced);
            Serial.printf("[lvgl] node ID changed !%08x -> !%08x; peers will see a new node\n",
                          announced, s_myNodeId);
        }
        if (announced != s_myNodeId) prefs.putUInt("lastNodeId", s_myNodeId);
        s_prevNodeId = prefs.getUInt("prevNodeId", 0);
        prefs.end();
    }

    Serial.printf("[lvgl] Node ID: !%08x%s\n", s_myNodeId,
                  s_cfg.nodeIdOverride ? " (override)"
                                       : (s_cfg.nodeIdFromPubKey ? " (from public key)" : ""));
}

static void recomputeChannelHashes() {
    for (int i = 0; i < MAX_CHANNELS; i++) {
        CHANNEL_KEYS[i].hash = channelKeyHash(CHANNEL_KEYS[i]);
    }
}

// True while channel 0 still carries a preset-derived name (or none at all) —
// i.e. nobody has deliberately renamed the primary channel. CUSTOM_CHANNEL_NAME
// counts as derived too: it is what custom settings put there, so switching
// back to a preset must be free to rename it again.
static bool primaryChannelNameIsPresetDefault() {
    const char *cur = CHANNEL_KEYS[0].name_buf[0] ? CHANNEL_KEYS[0].name_buf
                                                  : CHANNEL_KEYS[0].name;
    if (!cur || !cur[0]) return true;
    if (strcmp(cur, CUSTOM_CHANNEL_NAME) == 0) return true;
    for (uint8_t i = 0; i < PRESET_COUNT; i++) {
        const char *pn = kPresets[i].channelName;
        if (pn && strcmp(cur, pn) == 0) return true;
    }
    return false;
}

// Keeps channel 0's name in sync with the active modem preset — but only while
// the name is still preset-derived. A primary channel the user renamed stays
// renamed: this runs on every config save (just before the channel plan is
// written to NVS) and again at boot after the plan is loaded, so overwriting
// unconditionally erased a renamed channel 0 both on the way to storage and on
// the way back out of it.
static void syncPrimaryChannelName() {
    if (!primaryChannelNameIsPresetDefault()) return;
    uint8_t pi = s_cfg.modemPreset < PRESET_COUNT ? s_cfg.modemPreset : 0;
    const char *pname = s_cfg.loraUsePreset ? kPresets[pi].channelName
                                            : CUSTOM_CHANNEL_NAME;
    strncpy(CHANNEL_KEYS[0].name_buf, pname, sizeof(CHANNEL_KEYS[0].name_buf) - 1);
    CHANNEL_KEYS[0].name_buf[sizeof(CHANNEL_KEYS[0].name_buf) - 1] = '\0';
    CHANNEL_KEYS[0].name = CHANNEL_KEYS[0].name_buf;
}

// ── Licensed (amateur) mode ───────────────────────────────────
// Every channel open (keyLen 0, key zeroed) and any channel named "admin"
// disabled, as upstream's ensureLicensedOperation(). Returns true when that
// changed what is stored; a key in a slot whose stored record was already
// keyless (keylessMask) is the loader's compiled default and does not count.
static bool licStripChannels(uint32_t keylessMask) {
    bool changed = false;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        ChannelKey &ck = CHANNEL_KEYS[i];
        if (ck.keyLen != 0 && !(i < 32 && (keylessMask & (1UL << i)))) changed = true;
        memset(ck.key, 0, sizeof(ck.key));
        ck.keyLen = 0;
        const char *nm = ck.name_buf[0] ? ck.name_buf : ck.name;
        if (nm && strcasecmp(nm, "admin") == 0 && ck.role != 2) {
            // Slot 0 is always the primary; disabling it would drop every
            // packet on it. It is still made keyless above.
            if (i == 0) {
                Serial.println("[lic] primary channel named admin left enabled");
                continue;
            }
            ck.role = 2;
            changed = true;
        }
    }
    return changed;
}

#if FEATURE_LICENSED_MODE
bool licStripChannelsForImport() { return licStripChannels(0); }
#endif

// TX stays off while a ham-region exit is unconfirmed, or while licensed under
// the N0CALL test call sign.
static bool licTxInhibitWanted() {
    return s_cfg.licTxHold || (lic::active() && lic::isNoCall(s_cfg.callSign));
}

// Boot: after the config and channel plan load, before the radio and the
// channel hashes. Re-applies the licensed channel rules, which also covers a
// backup restore or a QR import made before licensed mode was turned on.
static void licensedModeBootSetup() {
    lic::setActive(FEATURE_LICENSED_MODE && s_cfg.licensedMode);
    if (lic::active()) {
        Serial.printf("[lic] licensed mode active, call sign %s\n", s_cfg.callSign);
        if (licStripChannels(s_chanStoredKeylessMask)) {
            Serial.println("[lic] channel keys stripped / admin disabled at boot - saving");
            persistChannelsToPrefs();
        }
        if (s_cfg.rebroadcastMode != lic::kRebroadcastLocalOnly) {
            s_cfg.rebroadcastMode = lic::kRebroadcastLocalOnly;
            persistConfigToPrefs();
        }
    }
    Radio.setTxInhibit(licTxInhibitWanted());
    if (Radio.txInhibited()) {
        Serial.printf("[lic] TX inhibited (%s)\n",
                      s_cfg.licTxHold ? "left a ham region - resume to transmit" : "N0CALL");
    }
}

static void licPersistAndReboot(const char *why) {
    persistConfigToPrefs();
    syncPrimaryChannelName();
    persistChannelsToPrefs();
    Serial.printf("[lic] %s - rebooting\n", why);
    // No lv_timer_handler() here, unlike onboarding: callers include LVGL event
    // handlers, which must not re-enter LVGL. They render before calling.
    delay(500);
    flushPersistentState();   // settings and transcripts must land before we go
    ESP.restart();
}

// The transaction of spec §1 (upstream handleSetHamMode). Returns false with a
// reason in err when refused; on success it does not return (reboots).
bool enterLicensedMode(const char *callSign, const char *longSuffix, uint8_t txPower,
                       char *err, size_t errCap) {
    if (err && errCap) err[0] = '\0';
#if !FEATURE_LICENSED_MODE
    (void)callSign; (void)longSuffix; (void)txPower;
    if (err && errCap) snprintf(err, errCap, "%s", TR("Licensed mode is not available on this device"));
    return false;
#else
    char call[sizeof(s_cfg.callSign)];
    if (!lic::normalizeCallSign(callSign, call, sizeof(call))) {
        if (err && errCap) snprintf(err, errCap, "%s", TR("Invalid call sign"));
        return false;
    }
    memcpy(s_cfg.callSign, call, sizeof(s_cfg.callSign));
    lic::composeLongName(call, longSuffix, s_cfg.nodeLong, MESH_LONG_NAME_MAX_BYTES + 1);
    licStripChannels(0);
    s_cfg.rebroadcastMode = lic::kRebroadcastLocalOnly;
    s_cfg.loraPower = (uint8_t)constrain((int)txPower, 1, (int)MESH_POWER);
    s_cfg.licensedMode = true;
    s_cfg.licTxHold = false;
    licPersistAndReboot("licensed mode on");
    return true;
#endif
}

// Off: the ISM rules come back. Keys stripped on the way in are not restored.
void exitLicensedMode() {
    s_cfg.licensedMode = false;
    const RegionPlan *r = regionPlan(s_cfg.region);
    if (r && r->licensedOnly) {
        // Never left transmitting unlicensed on a ham band: back to the board's
        // ISM region, TX held until the user confirms (resumeTxAfterLicensedExit).
        utf8util::copyTruncate(s_cfg.region, sizeof(s_cfg.region), MY_REGION);
        s_cfg.licTxHold = true;
        applyPresetParams(s_cfg);
    }
    const uint8_t cap = regionPower(s_cfg.region);
    if (s_cfg.loraPower > cap) s_cfg.loraPower = cap;
    licPersistAndReboot("licensed mode off");
}

// Confirms TX after leaving a ham region. No reboot.
void resumeTxAfterLicensedExit() {
    s_cfg.licTxHold = false;
    persistConfigToPrefs();
    Radio.setTxInhibit(licTxInhibitWanted());
    Serial.printf("[lic] TX resumed by user (inhibit %s)\n", Radio.txInhibited() ? "on" : "off");
}

bool licensedTxInhibited() { return Radio.txInhibited(); }

static void formatReplyPreview(const char *src, char *dst, size_t dstLen) {
    if (!dst || dstLen == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src == ' ') src++;
    utf8util::copyTruncate(dst, dstLen, src);
    for (size_t i = 0; dst[i]; i++) {
        if (dst[i] == '\r' || dst[i] == '\n') dst[i] = ' ';
    }
}

static uint32_t resolveReplyPacketId(const DisplayLine *const *rows, int rowCount, int rowIdx) {
    if (!rows || rowIdx < 0 || rowIdx >= rowCount || !rows[rowIdx]) return 0;

    uint32_t replyId = rows[rowIdx]->packetId;
    if (replyId != 0) return replyId;

    const char *txt = rows[rowIdx]->text;
    if (!(txt[0] == ' ' && txt[1] == ' ')) return 0;

    for (int j = rowIdx - 1; j >= 0; --j) {
        if (!rows[j]) break;
        if (rows[j]->packetId) {
            replyId = rows[j]->packetId;
            break;
        }
        const char *prev = rows[j]->text;
        if (!(prev[0] == ' ' && prev[1] == ' ')) break;
    }
    return replyId;
}

// senderNodeId is who wrote the selected message, for the Enter-opens-actions
// path; pass 0 for lines with no sender (local/system) and for callers that
// have no node to offer. It rides along here rather than being assigned at each
// call site so that the one set and the several clear paths cannot drift apart.
static void setSelectedReplyContext(uint32_t replyPacketId, const char *fallbackText,
                                    uint32_t senderNodeId) {
    s_selectedMsgReplyPacketId = replyPacketId;
    s_selectedMsgSenderNodeId = senderNodeId;
    s_selectedMsgText[0] = '\0';

    if (replyPacketId != 0) {
        bool collecting = false;
        size_t w = 0;
        for (int row = 0; row < MAX_MSG_LINES; row++) {
            const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
            if (!dl) break;
            if (shouldHideChatLine(dl->text)) continue;

            if (!collecting) {
                if (dl->packetId != replyPacketId) continue;
                collecting = true;
            } else if (dl->packetId != replyPacketId) {
                break;
            }

            const char *seg = dl->text;
            while (*seg == ' ') seg++;  // drop continuation indent padding
            if (!*seg) continue;

            if (w > 0 && w + 1 < sizeof(s_selectedMsgText)) {
                s_selectedMsgText[w++] = ' ';
            }

            char segBuf[MSG_CHARS + 1];
            size_t segW = 0;
            while (*seg && segW < MSG_CHARS) {
                char c = *seg++;
                if (c == '\r' || c == '\n' || c == '\t') c = ' ';
                segBuf[segW++] = c;
            }
            segBuf[segW] = '\0';

            size_t room = sizeof(s_selectedMsgText) - w;
            size_t added = utf8util::copyTruncate(s_selectedMsgText + w, room, segBuf);
            w += added;
            s_selectedMsgText[w] = '\0';

            if (w + 1 >= sizeof(s_selectedMsgText)) break;
        }
    }

    if (s_selectedMsgText[0] == '\0' && fallbackText) {
        utf8util::copyTruncate(s_selectedMsgText, sizeof(s_selectedMsgText), fallbackText);
    }

    while (s_selectedMsgText[0] == ' ') {
        memmove(s_selectedMsgText,
                s_selectedMsgText + 1,
                strlen(s_selectedMsgText) + 1);
    }
}

#if HAS_MESSAGE_ACTIONS
// The wire text of a channel message, rejoined from its stored lines with the
// prefix stripped, when it is MECP. False for anything else or a line that has
// scrolled out of the backlog.
static bool mecpRawTextForPacket(uint32_t packetId, char *out, size_t cap) {
    if (packetId == 0 || !out || cap == 0) return false;
    size_t w = 0;
    bool found = false;
    out[0] = '\0';
    for (int row = 0; row < MAX_MSG_LINES; row++) {
        const DisplayLine *dl = Channels.getLine(s_activeChannel, row);
        if (!dl) break;
        if (shouldHideChatLine(dl->text)) continue;
        if (dl->packetId != packetId) {
            if (found) break;
            continue;
        }
        const char *seg = found ? dl->text : chatStripPrefix(dl->text);
        while (found && *seg == ' ') seg++;
        if (found && w + 1 < cap) out[w++] = ' ';
        w += utf8util::copyTruncate(out + w, cap - w, seg);
        found = true;
        if (w + 1 >= cap) break;
    }
    return found && mecp::looksLikeMecp(out);
}
#endif  // HAS_MESSAGE_ACTIONS

