#pragma once
// Persistent configuration import/export and runtime config model.
#include <Arduino.h>
#include "config.h"
#include "mesh_proto.h"
#include "mesh_channel_plan.h"

enum UiThemeFamily : uint8_t {
    UI_THEME_CAMELLIA = 0,
    UI_THEME_EVERGREEN = 1,
    UI_THEME_EARTHEN = 2,
    UI_THEME_SOLARIZED = 3,
    UI_THEME_CRIMSON = 4,
    UI_THEME_SCARLET_POP = 5,
    UI_THEME_INK_WASH = 6,
    UI_THEME_LAVENDAR_FIELDS = 7,
    UI_THEME_WILD_FLOWERS = 8,
    UI_THEME_QUIET_LUXURY = 9,
    UI_THEME_MORNING_DEW = 10,
    UI_THEME_WINTER_CHILL = 11,
    UI_THEME_CAMELLIA_BLACK = 12,
    UI_THEME_COUNT = 13
};

enum UiThemeMode : uint8_t {
    UI_MODE_DARK = 0,
    UI_MODE_LIGHT = 1
};

static inline bool uiThemeForcesDark(uint8_t theme) {
    return theme == UI_THEME_CAMELLIA_BLACK;
}

// ── User-built themes ────────────────────────────────────────────────────────
// A theme is four authored colors plus a light/dark mode; everything else in
// the running palette is derived from them (see applyUiThemePalette). That is
// what makes a builder practical at all: the user picks four, the firmware
// computes the other thirty-one.
//
// Mode is not cosmetic here. It selects the text, dim-text and on-accent
// constants — the caret, body copy and label colors — so the same four colors
// read completely differently under it. It is the one field a user must get
// right for their theme to be legible.
#define UI_CUSTOM_THEME_SLOTS    4
#define UI_CUSTOM_THEME_NAME_MAX 16   // 15 chars + NUL

// Custom themes live in their own id range rather than extending
// UiThemeFamily, so built-ins can still be added up to 31 without renumbering
// what users have already saved. cfg.uiTheme holds one of these directly.
#define UI_THEME_CUSTOM_BASE     32

struct UiCustomTheme {
    char     name[UI_CUSTOM_THEME_NAME_MAX];
    uint16_t bgMain;     // rgb565, same four the presets carry
    uint16_t panelBg;
    uint16_t panelAlt;
    uint16_t accent;
    uint8_t  mode;       // UI_MODE_DARK / UI_MODE_LIGHT
    uint8_t  used;       // 0 = empty slot
};

static inline bool uiThemeIsCustom(uint8_t theme) {
    return theme >= UI_THEME_CUSTOM_BASE
        && theme < (UI_THEME_CUSTOM_BASE + UI_CUSTOM_THEME_SLOTS);
}

static inline int uiThemeCustomSlot(uint8_t theme) {
    return uiThemeIsCustom(theme) ? (int)(theme - UI_THEME_CUSTOM_BASE) : -1;
}

static inline uint8_t uiThemeFromCustomSlot(int slot) {
    return (uint8_t)(UI_THEME_CUSTOM_BASE + slot);
}

// True for any theme id this build can actually resolve to a palette — a
// built-in, or a custom slot that currently holds a theme. Everything that
// used to clamp uiTheme into 0..UI_THEME_COUNT-1 asks this instead, because
// that clamp silently rewrites a custom selection to Camillia Dark.
bool uiThemeIdValid(uint8_t theme);

// ── Custom theme store ───────────────────────────────────────────────────────
// Kept in its own NVS blob rather than appended to RhinoConfig. The settings
// struct is append-only with a documented history of fields landing in old
// trailing padding, and a fixed theme array in it could never be resized; here
// the slot count is just a constant, and a stored blob shorter than the current
// array leaves the extra slots empty.
void uiCustomThemesLoad();

// nullptr when the slot is out of range or empty.
const UiCustomTheme *uiCustomThemeGet(int slot);

// Number of slots currently holding a theme (not an index bound — slots can be
// freed out of order, so iterate all UI_CUSTOM_THEME_SLOTS and test each).
int uiCustomThemeCount();

// -1 when every slot is taken.
int uiCustomThemeFirstFree();

// slot < 0 stores into the first free slot. Returns the slot written, or -1 if
// there was no room. Persists immediately: a theme the user just named is not
// something to lose to a battery pull.
int uiCustomThemeSave(int slot, const UiCustomTheme &theme);

// Clearing the slot a theme is *selected* from is the caller's problem to
// notice — uiThemeIdValid() goes false for it, and the palette falls back.
bool uiCustomThemeDelete(int slot);

// ── Share codes ──────────────────────────────────────────────────────────────
// A theme as one hex string, for pasting between devices. Layout, all bytes
// hex-encoded uppercase with no separators:
//
//   0      version (0x01)
//   1      mode
//   2..13  bgMain, panelBg, panelAlt, accent as RGB888 triples
//   14     name length (0..15)
//   15..   name, ASCII
//   last   XOR of every preceding byte
//
// RGB888 rather than the stored RGB565 so a code round-trips through a browser
// color picker without drifting; the low bits are dropped on the way into the
// slot either way.
#define UI_CUSTOM_THEME_CODE_MAX 80   // 2 * (17 + 15) + NUL, rounded up

bool uiCustomThemeEncode(const UiCustomTheme &theme, char *out, size_t outLen);
bool uiCustomThemeDecode(const char *code, UiCustomTheme &out);

// Runtime config (loaded from SD or defaulted from compile-time #defines)
// Parse a node id written as "!aabbccdd", "0xaabbccdd" or bare hex. Returns 0
// for anything that names no node (empty, "none", "0"), which is how every
// optional node-id setting spells "unset". Shared so the YAML importer and the
// web form cannot disagree about what a user typed.
uint32_t parseNodeIdText(const char *val);

struct RhinoConfig {
    char     nodeLong[40];
    char     nodeShort[5];
    uint32_t nodeIdOverride;  // 0 = derive from MAC; non-zero = use this as myNodeId
    bool    gpsEnabled;           // use hardware GPS when available
    int32_t latI, lonI, alt;      // manual / last-known position (fallback)
    float   loraFreq, loraBw;
    uint8_t loraSf, loraCr, loraPower, loraHopLimit;
    uint8_t modemPreset;   // ModemPreset enum; drives loraBw/loraSf/loraCr on boot
    uint8_t  deviceRole;          // 0=CLIENT … 10=TAK_TRACKER
    uint8_t  rebroadcastMode;     // 0=ALL, 1=ALL_SKIP_DECODING, 2=LOCAL_ONLY, 3=KNOWN_ONLY
    bool     okToMqtt;            // set Data.bitfield OK_TO_MQTT bit on outgoing packets
    bool     ignoreMqtt;          // drop received packets with via_mqtt flag set
    uint32_t nodeInfoIntervalS;   // NodeInfo broadcast period (s), default 900
    uint32_t posIntervalS;        // Position broadcast period (s), default 1800
    uint32_t gpsPollIntervalS;    // GPS polling period (s), 0 = every loop
    char     region[12];          // Meshtastic region string, e.g. "US"

    // Device (additional)
    char     tzDef[48];

    // WiFi (web config)
    bool     wifiEnabled;        // master switch; gates web config + MQTT bridge
    char     wifiSsid[64];
    char     wifiPass[64];
    bool     webCfgAuthEnabled;  // require login for web config; off by default
    char     webCfgPass[64];

    // Display
    uint8_t  brightness;         // backlight percent, 10..100 in 10% steps
    uint32_t screenOnSecs;
    uint8_t  displayUnits;       // 0=METRIC, 1=IMPERIAL
    bool     compassNorthTop;
    bool     flipScreen;
    bool     splashMelodyEnabled;
    uint8_t  msgAlertSound;      // 0=DEFAULT, 1=CHIRPY, 2=BASS, 3=OFF
    uint8_t  uiTheme;            // UiThemeFamily
    uint8_t  uiMode;             // UiThemeMode
    uint8_t  chatStyle;          // 0=CLASSIC, 1=BUBBLES, 2=OUTLINE (applies live)
    uint8_t  chatNameStyle;      // 0=SHORT (4-char), 1=LONG (full node name) in chat
    bool     chatColorsEnabled;  // classic mode: use per-node text colors
    uint8_t  userMsgColor;       // own-message color: 0..15 = basic palette index,
                                 // 0xFF = adaptive default (theme yellow/amber)

    // Bluetooth
    bool     btEnabled;
    uint8_t  btMode;             // 0=RANDOM_PIN, 1=FIXED_PIN, 2=NO_PIN
    uint32_t btFixedPin;

    // Network
    char     ntpServer[48];
    bool     mqttEnabled;
    char     mqttServer[64];
    char     mqttUser[32];
    char     mqttPass[48];
    char     mqttRoot[48];
    bool     mqttEncryption;
    bool     mqttMapReport;
    uint16_t mqttPort;           // broker TCP port (8883 TLS, 1883 plaintext)
    bool     mqttTls;            // connect via WiFiClientSecure when set

    // Power
    bool     isPowerSaving;
    uint32_t lsSecs;
    uint32_t minWakeSecs;

    // Module: Telemetry
    bool     telDeviceEnabled;
    uint32_t telDeviceIntervalS;
    bool     telEnvEnabled;
    uint32_t telEnvIntervalS;

    // Module: Neighbor Info
    bool     neighborInfoEnabled;
    uint32_t neighborInfoIntervalS;
    bool     neighborInfoOverLora;

    // Module: Canned Messages
    bool     cannedEnabled;
    char     cannedMessages[200];

    // Module: Store and Forward (client)
    bool     snfClientEnabled;

    // Firmware updates
    bool     otaAutoCheckEnabled; // check for a newer release once per boot

    // Node management
    bool     nodeArchiveEnabled;  // preserve nodes evicted from the full table to SD
    bool     autoFavoriteEnabled; // auto-favorite nodes within autoFavoriteRangeM
    uint32_t autoFavoriteRangeM;  // auto-favorite distance threshold, meters

    // Chat display
    uint8_t  chatSpacing;   // 0=Tight(8px), 1=Normal(10px), 2=Loose(12px)
    uint8_t  fontSize;      // 0=Small, 1=Medium (default), 2=Large, 3=Extra Large (chat + DM)

    // Serial debug categories
    bool     debugAcks;
    bool     debugMessages;
    bool     debugGps;

    // APPEND-ONLY BELOW. Settings are stored as one NVS blob of this struct
    // (see kCfgBlobVersion in main_lvgl.cpp): a shorter stored blob is copied
    // into the front of the struct and anything newer keeps its default here.
    // Inserting or reordering a field silently misreads every existing device's
    // settings — add at the end, or bump the blob version.
    //
    // "At the end" is stricter than it looks: a new field must start at an
    // offset >= the PREVIOUS sizeof(RhinoConfig), not merely after the previous
    // last member. The stored blob is sizeof() bytes long, so it includes the
    // old struct's *trailing padding*, and the load memcpy's all of it over the
    // front of the struct. A field placed in those pad bytes is overwritten
    // with the old padding value instead of keeping its compiled default.
    //
    // That is not hypothetical: loraRxBoostedGain was first added at offset 879
    // against an old sizeof of 880, so it came back false on every device with
    // saved settings and quietly disabled RX boosted gain. Hence the explicit
    // pad below — keep new fields underneath it.
    uint8_t  volumePct;     // notification volume, 0..100 in 10% steps
    uint8_t  timeSource;    // TIME_SOURCE_AUTO / TIME_SOURCE_MANUAL
    uint8_t  _reservedPad0; // was trailing padding when sizeof was 880
    // SX1262 RX boosted gain. Worth ~2 dB of sensitivity for roughly 2 mA of
    // extra receive current — and the radio is in RX essentially all the time,
    // so this is a continuous cost. On by default to preserve existing range.
    bool     loraRxBoostedGain;
    // Stop the web config server after this many seconds with no HTTP request.
    // A connected station with modem power-save disabled (which web config
    // requires) is one of the largest continuous draws on the device, and it
    // otherwise runs until the user remembers to turn it off. 0 disables the
    // timeout.
    uint32_t webCfgIdleTimeoutS;
    // Park the GPS in standby between position samples (period = gpsPollIntervalS).
    // Off by default: the standby command dialect cannot be probed at runtime,
    // so this stays opt-in until verified on a given unit. See gpsSetDutyCycle().
    bool     gpsDutyCycleEnabled;
    // Pad past what was trailing padding when the struct ended at
    // gpsDutyCycleEnabled, so the custom-modem block below starts at an offset
    // no smaller than the old sizeof and keeps its defaults on an upgrade.
    // Same trap as _reservedPad0 above.
    uint8_t  _reservedPad1[3];
    // Custom (non-preset) modem settings. When loraUsePreset is false the four
    // fields below drive loraBw/loraSf/loraCr/loraFreq instead of modemPreset;
    // modemPreset is still kept so switching back restores the last preset.
    bool     loraUsePreset;    // true = derive from modemPreset (the default)
    uint8_t  loraCustomSf;     // LORA_SF_MIN..LORA_SF_MAX
    uint8_t  loraCustomCr;     // LORA_CR_MIN..LORA_CR_MAX (denominator of 4/n)
    uint8_t  loraCustomSlot;   // 1-based frequency slot; 0 = hash channel name
    uint16_t loraCustomBwKhz;  // Meshtastic bandwidth code (31 = 31.25, 62 = 62.5)
    // Same trap as _reservedPad0/_reservedPad1: the struct's alignment is 4, so
    // it ended with up to 3 bytes of trailing padding that the stored blob
    // includes. Four bytes clears that whatever the exact old sizeof was, so the
    // field below starts past it and keeps its default on an upgrade.
    uint8_t  _reservedPad2[4];
    // User battery-voltage trim, in parts per thousand away from the measured
    // value: 0 = uncalibrated, +25 reads 2.5% high. Set on-device by comparing
    // the reading against a meter (Battery Calibration in Settings). This is the
    // per-unit companion to the board-level BATT_CAL: divider tolerance and ADC
    // offset differ from unit to unit, and a voltage read a tenth of a volt off
    // moves the displayed percentage by tens of points on the flat part of the
    // Li-ion curve.
    int16_t  battCalTrim;
    // Covers the trailing padding that followed battCalTrim, same trap as the
    // pads above.
    uint8_t  _reservedPad3[2];
    // Shuffles which palette entry each node id hashes to for its chat color.
    // The palette has ten entries and node ids are arbitrary, so two nodes you
    // talk to can land on the same color; changing this re-rolls every
    // assignment at once. 0 is the original mapping, so devices that never ask
    // for a reshuffle keep the colors they have always had.
    uint32_t chatColorSalt;
    // Blink the notification LED while anything is unread. Only meaningful
    // where HAS_NOTIFY_LED; the field is unconditional so the blob layout does
    // not differ between boards. Defaults on, which is the behavior every unit
    // had before the setting existed.
    bool     notifyLedEnabled;
    // Flip the trackball's scroll direction (HAS_SCROLL_INVERT boards). Applied
    // where the trackball is read, so it moves lists, the chat and every slider
    // together and leaves j/k alone — those are letters with a fixed meaning,
    // not a pointing device.
    bool     invertScroll;
    // Covers the trailing padding that followed invertScroll in older blobs.
    // Keep this pad ahead of new fields so appended settings start after the
    // previous struct size and retain their compiled defaults on upgrade.
    uint8_t  _reservedPad4[4];
    // Mesh Deck RGB notification LED colors.
    // 0=Red, 1=Green, 2=Blue, 3=Yellow, 4=Cyan, 5=Magenta, 6=White
    uint8_t  notifyLedColorChannel;
    uint8_t  notifyLedColorDm;
    // Whether this node puts its coordinates on the mesh at all. Off suppresses
    // every POSITION_APP transmission — the scheduled broadcast and the manual
    // announce alike — regardless of where the fix would have come from (live
    // GPS, the last-known self position, or the configured fixed coordinates).
    // Distinct from gpsEnabled, which only chooses whether the GPS hardware is
    // one of those sources. Defaults on, matching every build before it existed.
    bool     shareLocation;
    // Blink the keyboard backlight while a message or DM is unread. Only
    // meaningful where HAS_KB_BLINK; the field is unconditional so the blob
    // layout does not differ between boards, same as notifyLedEnabled.
    bool     kbBlinkEnabled;
    // Covers the trailing padding that followed kbBlinkEnabled. The struct
    // aligns to 4 and kbBlinkEnabled is one byte at some offset X, so the old
    // sizeof was at most X+4 — three pad bytes put the fields below at X+4 or
    // later either way, which is what keeps their defaults on an upgrade. Same
    // trap as the pads above.
    uint8_t  _reservedPad5[3];
    // How many times the keyboard backlight flashes per notification cycle,
    // 1..3 (KB_FLASHES_MIN..MAX). Split by kind because the count is the only
    // thing telling the two apart on a board with no notification LED: the
    // defaults, 1 and 2, are the counts that were hardcoded before these were
    // settings.
    uint8_t  kbBlinkChanFlashes;
    uint8_t  kbBlinkDmFlashes;
    // Covers the trailing padding that followed kbBlinkDmFlashes — same trap as
    // the pads above. Two uint8_t at the end of a 4-aligned struct leave up to
    // three bytes the old blob still carries, and positionPrecision landing in
    // them would load as 0 on every existing device. Zero is not a harmless
    // value here: it would read as "keep no bits of the coordinate".
    uint8_t  _reservedPad6[3];
    // Bits of latitude/longitude actually transmitted, Meshtastic's
    // position_precision. 32 = exact; lower values round the coordinate to a
    // coarser grid before it leaves the device. See kPositionPrecisions.
    uint8_t  positionPrecision;
    // Same trailing-padding trap as every pad above: positionPrecision is one
    // byte at the end of a 4-aligned struct, so the stored blob carries up to
    // three bytes past it that the load memcpy's straight over anything placed
    // there. Three pad bytes put the field below at or past the old sizeof.
    uint8_t  _reservedPad7[3];
    // How long the notification LED / keyboard backlight keeps reminding after
    // a message arrives, in seconds. 0 = never stop. See kNotifyLightTimeouts.
    uint16_t notifyLightTimeoutS;
    // Two pad bytes for the same trailing-padding reason as every pad above:
    // notifyLightTimeoutS is a uint16_t at the end of a 4-aligned struct, so the
    // stored blob can carry two bytes past it. This puts the flag below at or
    // past the old sizeof either way.
    uint8_t  _reservedPad8[2];
    // Decode and display MeshBeacon (port 37) advertisements from other meshes.
    // Receive-only; see MY_MESH_BEACON_LISTEN.
    bool     meshBeaconListen;
    // Store-and-Forward router to address replay requests to, when the user has
    // pinned one. 0 = unset, which is the default and means "use whichever
    // router we heard a heartbeat from". Set it when the router has heartbeats
    // switched off, which is upstream's default and leaves it undiscoverable.
    //
    // No _reservedPad here on purpose, unlike every field above: those are
    // bools and uint8_ts that would have landed inside the old blob's trailing
    // padding. This is a uint32_t, and its natural 4-alignment already places
    // it at offset 940 — exactly the previous sizeof(RhinoConfig), with
    // meshBeaconListen at 938. Verified by compiling both layouts on the host.
    uint32_t snfRouterNodeId;
    // Battery indicator format: BATT_DISPLAY_PERCENT / BATT_DISPLAY_VOLTAGE.
    // Lands at the previous sizeof(RhinoConfig) because snfRouterNodeId above
    // is a uint32_t that ends flush with it — verified on the host, same check
    // the pads above document. Default 0 (PERCENT) also happens to be what an
    // upgraded device reads out of the old blob's absent bytes, so existing
    // units keep today's behavior either way.
    uint8_t  battDisplayMode;
    // For whoever appends next: battDisplayMode is one byte at the end of a
    // 4-aligned struct, so the stored blob carries three bytes past it that the
    // load memcpy's straight over anything placed there.
    uint8_t  _reservedPad9[3];
    // Base URL of the elevation endpoint used by the terrain line-of-sight
    // feature (Node Actions -> LOS). Must be plain http:// — this firmware has
    // no TLS client, so an https:// address cannot be reached at all. Empty is
    // the default and means "not configured": the LOS modal says so instead of
    // failing with a network error that looks like a bug.
    //
    // Safe at the end: _reservedPad9 above consumes the old struct's trailing
    // padding, so this array starts exactly at the previous sizeof(RhinoConfig)
    // and keeps its compiled default on an upgraded device. 96 is a multiple of
    // 4, so the struct stays 4-aligned and the next appender needs no new pad.
    char     losElevServer[96];
    // ── External BLE (HID over GATT) keyboard ───────────────────────────────
    // The device as the HID *host*, connecting out to a keyboard. Nothing to do
    // with btEnabled/btMode/btFixedPin above, which describe the opposite role
    // -- the device as a peripheral a phone pairs to -- and remain inert.
    //
    // Safe at the end for the usual reason: losElevServer is a char[96], a
    // multiple of 4, so the struct has no trailing padding for these to land in
    // and an upgraded device reads its compiled defaults for all of them. The
    // block below is itself 56 bytes, so the struct stays 4-aligned and the
    // next appender needs no new pad.
    char     bleKbdAddr[20];     // "aa:bb:cc:dd:ee:ff", empty = nothing paired
    char     bleKbdName[32];     // advertised name, for the settings row
    bool     bleKbdEnabled;      // off by default: the stack costs internal DRAM
    uint8_t  bleKbdAddrType;     // BLE_ADDR_*; random static is the common case
    uint8_t  _reservedPad10[2];
};

// ── Position precision (imprecise location) ──────────────────────────────────
// Meshtastic's imprecise-location scheme, and deliberately bit-compatible with
// it: keeping N of the 32 bits of a degrees*1e7 coordinate snaps it to a grid
// of 2^(32-N) units, and the transmitted point is re-centred in that cell so
// the error is symmetric rather than always north-east of the truth. The
// receiving node is told how many bits are real via Position.precision_bits, so
// other firmware can draw the uncertainty instead of trusting a false fix.
//
// Distances are the cell size at the equator for latitude (1 degree ~ 111 km);
// longitude cells narrow toward the poles, so the real error is never worse
// than the label says.
//
// One difference from Meshtastic worth knowing: theirs is per channel
// (ChannelSettings.module_settings.position_precision), so a node can be exact
// on a private channel and coarse on a public one. Ours is one device-wide
// setting applied to every position we send.
struct PositionPrecisionOption {
    uint8_t     bits;
    const char *label;
};
extern const PositionPrecisionOption kPositionPrecisions[];
extern const int kPositionPrecisionCount;

// Label for a stored precision value; falls back to the exact-location label so
// an unknown value never renders as blank.
const char *positionPrecisionLabel(uint8_t bits);

// Nearest supported value, for anything that arrives from outside (YAML, the
// web form, an older blob). Never returns 0: "share nothing" is shareLocation's
// job, and having two settings mean it invites them to disagree.
uint8_t positionPrecisionCoerce(uint8_t bits);

// Keyboard-blink flash counts. Three is the practical ceiling: at 60 ms lit and
// 90 ms dark a fourth flash runs the pattern past 500 ms, and the whole cycle
// repeats every second.
#define KB_FLASHES_MIN 1
#define KB_FLASHES_MAX 3

static inline uint8_t cfgCoerceKbFlashes(int n) {
    if (n < KB_FLASHES_MIN) return KB_FLASHES_MIN;
    if (n > KB_FLASHES_MAX) return KB_FLASHES_MAX;
    return (uint8_t)n;
}

// ── Light-notification timeout ───────────────────────────────────────────────
// How long the notification LED or keyboard backlight keeps reminding after a
// message lands. Never (0) is the default and is what the firmware has always
// done: repeat until the message is read.
//
// The clock restarts on every arrival, so a busy channel keeps the light going
// and silence lets it lapse.
struct NotifyLightTimeoutOption {
    uint16_t    secs;
    const char *label;
};
extern const NotifyLightTimeoutOption kNotifyLightTimeouts[];
extern const int kNotifyLightTimeoutCount;

// Label for a stored value; falls back to the Never label so an unrecognised
// value never renders blank.
const char *notifyLightTimeoutName(uint16_t secs);

// Snaps to the nearest listed value. A hand-edited YAML saying 120 comes back
// as 60, which is the price of a row that can always reproduce what it shows:
// an arbitrary value would display as something the user could not cycle back
// to. Never (0) is only chosen by an exact 0, so no small number rounds a
// timeout away into "never stop".
uint16_t cfgCoerceNotifyLightTimeout(long secs);

// How the local battery reads where it is shown as a single number: the chat
// header indicator and the web status chip. Percentage comes off a Li-ion SOC
// curve, and on the flat part of that curve a tenth of a volt swings it by tens
// of points — voltage mode shows the number the curve was derived from instead.
//
// An enum rather than a bool so a later "percent + voltage" mode does not need
// a second field. Does not affect transmitted telemetry, the Device Info panel
// or the calibration modal, all of which show both numbers by design.
enum BattDisplayMode : uint8_t {
    BATT_DISPLAY_PERCENT = 0,
    BATT_DISPLAY_VOLTAGE = 1,
};
#define BATT_DISPLAY_MAX 1

// Where the wall clock comes from. AUTO is NTP when there's a network path and
// GPS otherwise; MANUAL means the user set it and nothing may overwrite it.
enum : uint8_t {
    TIME_SOURCE_AUTO   = 0,
    TIME_SOURCE_MANUAL = 1,
};

static inline uint8_t cfgCoerceTimeSource(int v) {
    return (v == TIME_SOURCE_MANUAL) ? TIME_SOURCE_MANUAL : TIME_SOURCE_AUTO;
}

enum : uint8_t {
    NOTIFY_LED_COLOR_RED = 0,
    NOTIFY_LED_COLOR_GREEN = 1,
    NOTIFY_LED_COLOR_BLUE = 2,
    NOTIFY_LED_COLOR_YELLOW = 3,
    NOTIFY_LED_COLOR_CYAN = 4,
    NOTIFY_LED_COLOR_MAGENTA = 5,
    NOTIFY_LED_COLOR_WHITE = 6,
    NOTIFY_LED_COLOR_OFF = 7,
};

static inline uint8_t cfgCoerceNotifyLedColor(int v) {
    if (v < NOTIFY_LED_COLOR_RED) return NOTIFY_LED_COLOR_BLUE;
    if (v > NOTIFY_LED_COLOR_OFF) return NOTIFY_LED_COLOR_BLUE;
    return (uint8_t)v;
}

// Clamps a notification volume to range and snaps it to the nearest 10% step,
// mirroring cfgCoerceBrightness. Shared by the on-device slider, the web form
// and YAML import so an out-of-range value from any source lands somewhere sane.
static inline uint8_t cfgCoerceVolume(int pct) {
    if (pct < VOLUME_PCT_MIN) return VOLUME_PCT_MIN;
    if (pct > VOLUME_PCT_MAX) return VOLUME_PCT_MAX;
    return (uint8_t)(((pct + VOLUME_PCT_STEP / 2) / VOLUME_PCT_STEP) * VOLUME_PCT_STEP);
}

// Clamps a backlight percentage to the supported range and snaps it to the
// nearest 10% step. Shared by the on-device slider, the web form and YAML
// import so an out-of-range value from any source lands somewhere sane.
static inline uint8_t cfgCoerceBrightness(int pct) {
    if (pct < BRIGHTNESS_PCT_MIN) return BRIGHTNESS_PCT_MIN;
    if (pct > BRIGHTNESS_PCT_MAX) return BRIGHTNESS_PCT_MAX;
    return (uint8_t)(((pct + BRIGHTNESS_PCT_STEP / 2) / BRIGHTNESS_PCT_STEP)
                     * BRIGHTNESS_PCT_STEP);
}

// The auto-favorite radius is stored in meters but always entered and shown in
// the display's own units, so its default is whichever round value reads as
// "1.00" there: 1 km metric, 1 mile imperial.
static inline uint32_t cfgDefaultAutoFavRangeM(uint8_t displayUnits) {
    return (displayUnits != 0) ? MY_AUTOFAV_RANGE_MI_M : MY_AUTOFAV_RANGE_KM_M;
}

// True while the radius is still one of the two defaults — i.e. nobody has
// typed a value of their own. Only then may a units change re-round it; a
// deliberate 2.5 mi has to survive becoming 4.02 km.
static inline bool cfgAutoFavRangeIsDefault(uint32_t rangeM) {
    return rangeM == MY_AUTOFAV_RANGE_KM_M || rangeM == MY_AUTOFAV_RANGE_MI_M;
}

// Next value for RhinoConfig::chatColorSalt. The increment is the golden-ratio
// constant: odd, so repeated presses walk 2^32 distinct salts rather than
// cycling back, and large enough that consecutive salts scatter node ids
// differently instead of shifting them by one palette slot.
static inline uint32_t cfgNextChatColorSalt(uint32_t current) {
    return current + 0x9E3779B9u;
}

// Battery-voltage trim, in parts per thousand. The range is wide enough for a
// bad divider (a 1% resistor pair plus ADC offset lands within a few percent;
// 20% covers a wrong-value resistor) and narrow enough that a fat-fingered
// setting cannot turn a 3.7 V cell into a plausible-looking 2 V one.
#define BATT_CAL_TRIM_MIN   (-200)
#define BATT_CAL_TRIM_MAX   (200)
#define BATT_CAL_TRIM_STEP  (5)     // 0.5% per key press

static inline int16_t cfgCoerceBattCalTrim(int permille) {
    if (permille < BATT_CAL_TRIM_MIN) return BATT_CAL_TRIM_MIN;
    if (permille > BATT_CAL_TRIM_MAX) return BATT_CAL_TRIM_MAX;
    return (int16_t)permille;
}

// Trim -> the multiplier applied to a measured battery voltage.
static inline float cfgBattCalScale(int16_t trim) {
    return 1.0f + (float)cfgCoerceBattCalTrim(trim) / 1000.0f;
}

// Backlight percent -> LGFX setBrightness() duty (0-255).
static inline uint8_t cfgBrightnessDuty(uint8_t pct) {
    return (uint8_t)((cfgCoerceBrightness(pct) * 255 + 50) / 100);
}

// Only client device roles are supported on this firmware. Values are the
// canonical Meshtastic enum positions so they stay wire-compatible.
//   0 = CLIENT, 1 = CLIENT_MUTE, 8 = CLIENT_HIDDEN
// Any other role is coerced to CLIENT.
static inline uint8_t cfgCoerceClientRole(uint8_t role) {
    return (role == 1 || role == 8) ? role : 0;
}

// Derives loraFreq/loraBw/loraSf/loraCr from cfg.region and cfg.modemPreset.
// Call after any config load to ensure radio params are consistent.
void applyPresetParams(RhinoConfig &cfg);

// Initialise from compile-time defaults. Call once before sdBegin().
void cfgInitDefaults(RhinoConfig &cfg);

// Serialise cfg (and CHANNEL_KEYS[], plus the identity keypair) to YAML,
// appending into out. The output carries the node's private key — an exported
// config is a secret, not just a settings file.
// ── Remembered WiFi networks (export/import bridge) ──────────────────────────
// How many networks are remembered *besides* the configured one. Shared so the
// picker's array and the YAML importer's scratch agree on the ceiling.
#define CFG_SAVED_WIFI_MAX 5

// The list of networks besides the configured one lives with the WiFi picker in
// the UI layer, which owns its NVS blob. These let the YAML round-trip reach it
// without config_io needing to know how it is stored. Defined in main_lvgl.cpp.
//
// The *active* network is not in this list — it is cfg.wifiSsid/wifiPass, and
// the export marks it so on the way out.
int  cfgSavedWifiCount();
bool cfgSavedWifiAt(int idx, char *ssidOut, size_t ssidCap, char *passOut, size_t passCap);
void cfgSavedWifiClear();
void cfgSavedWifiAdd(const char *ssid, const char *pass);
void cfgSavedWifiCommit();   // persist after a batch of Clear/Add

// Management, for the web config's saved-networks list. Each persists on its
// own and returns true only when something actually changed, so a handler can
// tell the user whether the click did anything.
bool cfgSavedWifiRemember(const char *ssid, const char *pass);  // add or update
bool cfgSavedWifiRemove(const char *ssid);                      // forget one
bool cfgSavedWifiActivate(const char *ssid);                    // make it configured

void cfgToYaml(const RhinoConfig &cfg, String &out);

// Parse YAML from an in-memory buffer. Updates CHANNEL_KEYS[] and fills cfg.
// Returns true on success.
bool cfgImportFromBuf(const char *buf, size_t len, RhinoConfig &cfg);

// True when the last import restored a complete identity keypair into
// myPubKey/myPrivKey. The parser only touches RAM, so the caller must write
// them to NVS for the identity to survive the reboot that follows an import.
bool cfgImportRestoredKeys();

// Mount the configured storage backend. SPI-SD boards initialize their bus in
// this path; SD_MMC and internal-flash boards use storageBegin().
bool sdBegin();

// Cached mount state — true if a card is currently mounted. Unlike sdBegin()
// this never probes the bus, so UI/web paths can ask cheaply and repeatedly.
bool sdCardMounted();

// Write /camillia/config.yaml. Returns true on success.
bool cfgExport(const RhinoConfig &cfg);

// Read /camillia/config.yaml.
// Updates CHANNEL_KEYS[] and fills cfg. Returns true on success.
bool cfgImport(RhinoConfig &cfg);

// Returns true if /camillia/config.yaml is present on the configured storage.
// Mounts it if needed; returns false if storage is unavailable.
bool cfgSdConfigExists();
