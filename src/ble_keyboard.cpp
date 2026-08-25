// ════════════════════════════════════════════════════════════════════════════
// ble_keyboard.cpp — BLE HID (HOGP) keyboard host
//
// Why this is written against raw GATT rather than a HID-host component: the
// pinned toolchain (espressif32@7.0.1 -> Arduino core 2.0.17 / IDF 4.4) ships a
// libbt.a with NimBLE enabled, Bluedroid *disabled*, and no esp_hidh_* symbols
// at all. So the usual answers -- BLEDevice.h, esp_hidh_dev_open() -- cannot
// link here, and HOGP has to be driven by hand. That is less work than it
// sounds, because of the next paragraph.
//
// Boot protocol, not report protocol. Writing 0x00 to Protocol Mode (0x2A4E)
// puts a keyboard into the boot keyboard role, where every keypress arrives as
// a fixed 8-byte report -- [modifiers, reserved, keycode x6] -- with no HID
// Report Descriptor to parse. Skipping that parser skips where a from-scratch
// HID host normally sinks most of its effort and nearly all of its bugs. The
// cost is that boot protocol exposes the plain 6-key-rollover keyboard only: no
// media keys, no trackpad on combo devices. For a mesh messenger that is the
// right default, not a limitation to design around.
//
// A keyboard that implements HOGP but not the boot role gets a best-effort
// fallback: subscribe to its input reports (0x2A4D) and read any 8-byte one as
// if it were the boot layout, which is what the overwhelming majority of them
// actually send. Reports of other lengths (consumer/media pages) are dropped
// rather than guessed at.
//
// Threading. Connect, bond and GATT discovery each block for seconds, so they
// run on a worker task driven by a command queue; the UI only ever posts a
// request and polls state. Notifications arrive on the NimBLE host task and are
// turned into keys there, into a critical-section-guarded ring the UI pops from
// -- the same shape as vncHostPopKey().
// ════════════════════════════════════════════════════════════════════════════
#include "ble_keyboard.h"

#include "config.h"

#if HAS_BLE_KEYBOARD

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>          // coexistence needs Wi-Fi modem sleep; see startStack()
#include <esp_heap_caps.h>
#include <string.h>

#include "keyboard.h"   // KEY_* codes; BLE keys share the built-in keyboard's space

// ── Tunables ────────────────────────────────────────────────────────────────
namespace {

// A keyboard is one device; there is no reason to hold a list. Anything beyond
// this in a scan is noise from the rest of the room.
constexpr int      kMaxScanResults   = 16;
constexpr uint32_t kScanSeconds      = 8;

// Auto-repeat. The keyboard sends one notification on press and one on release
// and nothing in between, so holding a key produces exactly one report --
// repeat is entirely ours to synthesize. These are the familiar desktop
// defaults; slower than a PC on purpose, because the screens being scrolled
// here are short.
constexpr uint32_t kRepeatDelayMs    = 450;
constexpr uint32_t kRepeatIntervalMs = 70;

// Reconnect backoff for a remembered keyboard that is out of range or asleep.
// Each attempt costs a connect timeout, so this walks up rather than hammering.
constexpr uint32_t kRetryMinMs       = 3000;
constexpr uint32_t kRetryMaxMs       = 60000;

constexpr int      kKeyRingSize      = 32;

// ── GATT UUIDs ──────────────────────────────────────────────────────────────
constexpr uint16_t kUuidHidService   = 0x1812;
constexpr uint16_t kUuidProtocolMode = 0x2A4E;   // 0 = boot, 1 = report
constexpr uint16_t kUuidBootKbdIn    = 0x2A22;   // 8-byte boot keyboard report
constexpr uint16_t kUuidReport       = 0x2A4D;   // report-protocol fallback
constexpr uint16_t kUuidReportRef    = 0x2908;   // descriptor: [id, type]
constexpr uint16_t kUuidBatterySvc   = 0x180F;
constexpr uint16_t kUuidBatteryLevel = 0x2A19;

// Advertised appearance values that mean "keyboard". Checked because plenty of
// keyboards keep the HID service UUID out of the advertisement and only expose
// it after connecting, so filtering on 0x1812 alone hides them from the picker.
constexpr uint16_t kAppearanceHid      = 0x03C0;
constexpr uint16_t kAppearanceKeyboard = 0x03C1;

struct ScanEntry {
    char name[32];
    char addr[20];
    uint8_t addrType;
    int  rssi;
    bool hidService;   // advertised 0x1812 outright, vs. matched on appearance
};

// ── State ───────────────────────────────────────────────────────────────────
// Written by the worker and the NimBLE host task, read by the UI. Everything
// except the key ring is guarded by s_mutex; the ring uses a critical section
// because it is touched on every notification and the pop runs in the key pump.
SemaphoreHandle_t s_mutex = nullptr;
QueueHandle_t     s_cmdQ  = nullptr;
TaskHandle_t      s_task  = nullptr;

portMUX_TYPE s_keyMux = portMUX_INITIALIZER_UNLOCKED;
volatile char s_keyRing[kKeyRingSize] = {0};
volatile int  s_keyHead = 0;
volatile int  s_keyTail = 0;

bool     s_enabled   = false;
bool     s_stackUp   = false;
uint8_t  s_state     = BLE_KBD_OFF;
char     s_detail[48] = {0};     // why we are in the state we are in

char     s_pairedAddr[20] = {0};
uint8_t  s_pairedAddrType = 0;
char     s_pairedName[32] = {0};
bool     s_pairingDirty   = false;

ScanEntry s_scan[kMaxScanResults];
int       s_scanCount = 0;

NimBLEClient *s_client = nullptr;
NimBLERemoteCharacteristic *s_batteryChar = nullptr;
uint8_t  s_batteryPct = 0xFF;
uint32_t s_passkey    = 0;

uint32_t s_nextRetryMs = 0;
uint32_t s_retryDelay  = kRetryMinMs;

// Report state. Touched only on the NimBLE host task (notifications) and the
// main loop (auto-repeat); both write s_held* so they take the mutex.
uint8_t  s_prevKeys[6] = {0};
bool     s_capsLock    = false;
uint8_t  s_heldUsage   = 0;
char     s_heldChar    = KEY_NONE;
uint32_t s_heldSinceMs = 0;
uint32_t s_repeatNextMs = 0;

enum : uint8_t { CMD_START, CMD_STOP, CMD_SCAN, CMD_PAIR, CMD_FORGET };

struct Cmd {
    uint8_t op;
    int     arg;
};

struct Lock {
    Lock()  { if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY); }
    ~Lock() { if (s_mutex) xSemaphoreGive(s_mutex); }
};

void setState(uint8_t state, const char *detail = nullptr) {
    Lock lock;
    s_state = state;
    snprintf(s_detail, sizeof(s_detail), "%s", detail ? detail : "");
}

void logInternalHeap(const char *tag) {
    // The scarce resource on these boards is internal DRAM, not PSRAM, and a
    // resident BLE stack eats 30-40 KB of it. Runtime HTTPS (and therefore the
    // OTA update check) is gated on internal free/largest-block thresholds, so
    // "BLE quietly disabled update checks" is a real failure mode. Printed on
    // every start and stop so it is answerable from a log rather than guessed.
    Serial.printf("[blekbd] %s internal free=%u largest=%u\n",
                  tag,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// ── HID usage -> Camillia key code ──────────────────────────────────────────
// Translated into the existing KEY_* space rather than a parallel one, so every
// downstream consumer -- modals, typing contexts, the j/k remap, screen wake --
// treats a Bluetooth key exactly as it treats a built-in one.
char hidUsageToKey(uint8_t usage, uint8_t mods, bool caps) {
    // Modifiers are a bitmask in byte 0, not keycodes of their own, so shift has
    // to be applied here: there is no downstream shift state to lean on.
    const bool shift = (mods & 0x22) != 0;   // either Shift
    const bool ctrl  = (mods & 0x11) != 0;   // either Control
    const bool gui   = (mods & 0x88) != 0;   // either GUI / Command

    // Chords are not bound to anything on this device, and letting them through
    // as their bare letter would type into whatever field is open.
    if (ctrl || gui) return KEY_NONE;

    switch (usage) {
        case 0x28: case 0x58: return KEY_ENTER;       // Enter, keypad Enter
        case 0x2A: return KEY_BACKSPACE;
        case 0x4C: return KEY_BACKSPACE;              // Delete: nearest thing we have
        case 0x29: return KEY_ESCAPE;
        case 0x2B: return KEY_TAB;
        case 0x2C: return ' ';
        case 0x4F: return KEY_NEXT_CHAN;              // Right
        case 0x50: return KEY_PREV_CHAN;              // Left
        case 0x51: return KEY_SCROLL_DN;              // Down
        case 0x52: return KEY_SCROLL_UP;              // Up
        case 0x4B: return KEY_PAGE_UP;
        case 0x4E: return KEY_PAGE_DN;
        default: break;
    }

    if (usage >= 0x04 && usage <= 0x1D) {             // a..z
        const bool upper = shift != caps;             // Caps Lock inverts Shift
        return (char)((upper ? 'A' : 'a') + (usage - 0x04));
    }
    if (usage >= 0x1E && usage <= 0x27) {             // 1..9 then 0
        static const char kPlain[10]   = {'1','2','3','4','5','6','7','8','9','0'};
        static const char kShifted[10] = {'!','@','#','$','%','^','&','*','(',')'};
        const int i = usage - 0x1E;
        return shift ? kShifted[i] : kPlain[i];
    }
    if (usage >= 0x59 && usage <= 0x61) return (char)('1' + (usage - 0x59));   // keypad 1..9

    switch (usage) {
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        // Keypad punctuation, taken at face value: Num Lock lives in the
        // keyboard and it never tells us which way it is set.
        case 0x54: return '/';
        case 0x55: return '*';
        case 0x56: return '-';
        case 0x57: return '+';
        case 0x62: return '0';
        case 0x63: return '.';
        default: break;
    }
    return KEY_NONE;
}

// Enter and Escape are excluded on purpose: a held Enter would repeat-send a
// message, and a held Escape would walk back up the modal stack.
bool keyRepeats(char key) {
    return key != KEY_NONE && key != KEY_ENTER && key != KEY_ESCAPE && key != KEY_TAB;
}

void pushKey(char key) {
    if (key == KEY_NONE) return;
    portENTER_CRITICAL(&s_keyMux);
    const int next = (s_keyHead + 1) % kKeyRingSize;
    if (next != s_keyTail) {          // full ring drops, it never overwrites
        s_keyRing[s_keyHead] = key;
        s_keyHead = next;
    }
    portEXIT_CRITICAL(&s_keyMux);
}

void clearKeys() {
    portENTER_CRITICAL(&s_keyMux);
    s_keyHead = s_keyTail = 0;
    portEXIT_CRITICAL(&s_keyMux);
}

void clearHeld() {
    Lock lock;
    s_heldUsage    = 0;
    s_heldChar     = KEY_NONE;
    s_heldSinceMs  = 0;
    s_repeatNextMs = 0;
}

// ── Input reports ───────────────────────────────────────────────────────────
void handleReport(const uint8_t *data, size_t len) {
    // Boot layout: [modifiers, reserved, keycode x6]. Shorter notifications are
    // consumer/media pages from a keyboard that never entered the boot role;
    // they carry a different layout entirely and are dropped rather than
    // guessed at.
    if (!data || len < 8) return;

    const uint8_t mods = data[0];
    const uint8_t *keys = data + 2;

    for (int i = 0; i < 6; i++) {
        const uint8_t usage = keys[i];
        if (usage < 0x04) continue;             // 0 = empty, 1..3 = rollover errors

        bool wasDown = false;
        for (int j = 0; j < 6; j++) {
            if (s_prevKeys[j] == usage) { wasDown = true; break; }
        }
        if (wasDown) continue;                  // still held, not a new press

        if (usage == 0x39) {                    // Caps Lock: ours to track
            s_capsLock = !s_capsLock;
            continue;
        }

        const char key = hidUsageToKey(usage, mods, s_capsLock);
        if (key == KEY_NONE) continue;
        pushKey(key);

        Lock lock;
        s_heldUsage    = usage;
        s_heldChar     = key;
        s_heldSinceMs  = millis();
        s_repeatNextMs = s_heldSinceMs + kRepeatDelayMs;
    }

    // The tracked key has come up (or a different one took over) -- stop
    // repeating it. Everything else about rollover we deliberately ignore:
    // only the most recent press repeats, exactly as a desktop host behaves.
    bool heldStillDown = false;
    {
        Lock lock;
        if (s_heldUsage) {
            for (int i = 0; i < 6; i++) {
                if (keys[i] == s_heldUsage) { heldStillDown = true; break; }
            }
        }
    }
    if (!heldStillDown) clearHeld();

    memcpy(s_prevKeys, keys, sizeof(s_prevKeys));
}

void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
    handleReport(data, len);
}

// ── NimBLE callbacks ────────────────────────────────────────────────────────
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *client) override {
        // Keyboards are latency-sensitive and idle most of the time. A 15-30 ms
        // interval with a 4 s supervision timeout keeps keypresses prompt
        // without holding the radio awake between them.
        client->updateConnParams(12, 24, 0, 400);
    }

    void onDisconnect(NimBLEClient *client) override {
        (void)client;
        Serial.println("[blekbd] disconnected");
        memset(s_prevKeys, 0, sizeof(s_prevKeys));
        clearHeld();
        {
            Lock lock;
            s_batteryChar = nullptr;
            s_batteryPct  = 0xFF;
        }
        if (s_enabled && s_pairedAddr[0]) {
            setState(BLE_KBD_RETRYING, "link lost");
            s_retryDelay  = kRetryMinMs;
            s_nextRetryMs = millis() + s_retryDelay;
        } else {
            setState(BLE_KBD_IDLE);
        }
    }

    uint32_t onPassKeyRequest() override {
        // Reached when the peer asks *us* to type its code, which a keyboard
        // has no way to show. Nothing better to answer than the one we are
        // already displaying.
        return s_passkey;
    }

    bool onConfirmPIN(uint32_t pin) override {
        Serial.printf("[blekbd] numeric comparison %06u - accepting\n", (unsigned)pin);
        return true;
    }

    void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
        Serial.printf("[blekbd] auth complete: encrypted=%d authenticated=%d bonded=%d\n",
                      (int)desc->sec_state.encrypted,
                      (int)desc->sec_state.authenticated,
                      (int)desc->sec_state.bonded);
        Lock lock;
        s_passkey = 0;   // nothing left to display either way
    }
};

ClientCallbacks s_clientCallbacks;

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        const bool hasHid = dev->isAdvertisingService(NimBLEUUID(kUuidHidService));
        const uint16_t appearance = dev->haveAppearance() ? dev->getAppearance() : 0;
        const bool looksLikeKeyboard =
            (appearance == kAppearanceKeyboard || appearance == kAppearanceHid);

        // Logged for every candidate, not only the one connected to: name,
        // address and appearance from real hardware is what turns the
        // compatibility guidance in docs/BLUETOOTH_KEYBOARDS.md from inference
        // into something tested.
        if (hasHid || looksLikeKeyboard) {
            Serial.printf("[blekbd] candidate %s \"%s\" rssi=%d appearance=0x%04X hid=%d\n",
                          dev->getAddress().toString().c_str(),
                          dev->haveName() ? dev->getName().c_str() : "",
                          dev->getRSSI(), (unsigned)appearance, (int)hasHid);
        } else {
            return;
        }

        Lock lock;
        if (s_scanCount >= kMaxScanResults) return;
        const std::string addr = dev->getAddress().toString();
        for (int i = 0; i < s_scanCount; i++) {
            if (!strcmp(s_scan[i].addr, addr.c_str())) return;
        }
        ScanEntry &entry = s_scan[s_scanCount++];
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.addr, sizeof(entry.addr), "%s", addr.c_str());
        snprintf(entry.name, sizeof(entry.name), "%s",
                 dev->haveName() && dev->getName().length() ? dev->getName().c_str()
                                                            : "(unnamed)");
        entry.addrType   = dev->getAddress().getType();
        entry.rssi       = dev->getRSSI();
        entry.hidService = hasHid;
    }
};

ScanCallbacks s_scanCallbacks;

void onScanEnded(NimBLEScanResults results) {
    (void)results;
    int found = 0;
    {
        Lock lock;
        found = s_scanCount;
    }
    Serial.printf("[blekbd] scan complete, %d candidate(s)\n", found);
    char detail[32];
    snprintf(detail, sizeof(detail), "%d found", found);
    setState(s_pairedAddr[0] ? BLE_KBD_RETRYING : BLE_KBD_IDLE, detail);
    // A scan that turned nothing up should not stall a remembered keyboard's
    // reconnect until the backoff would have expired on its own.
    s_nextRetryMs = millis() + kRetryMinMs;
}

// ── Worker ──────────────────────────────────────────────────────────────────
bool startStack() {
    if (s_stackUp) return true;
    logInternalHeap("before init");

    // Espressif's software Wi-Fi/BT coexistence requires Wi-Fi modem sleep. Web
    // config deliberately turns it off (WIFI_PS_NONE) because the synchronous
    // WebServer stalls on DTIM-buffered packets -- and with it off the Wi-Fi
    // driver never yields the radio, so esp_bt_controller_enable() calls
    // abort() from inside coex_core_enable() and takes the whole device down.
    //
    // That is not a failure that can be caught: ESP_ERROR_CHECK is not even
    // reached, the coex layer aborts first. The only defence is to never enter
    // it in that state, so modem sleep goes back on here, before init, and
    // web_config.cpp leaves it alone for as long as this stack is up.
    //
    // The cost is real and lands on web config: pages served while a keyboard
    // is connected are slower, for exactly the reason its own comment gives.
    // Coexistence is a time-share of one radio; something has to give.
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.setSleep(true);
        Serial.println("[blekbd] Wi-Fi modem sleep forced on (required for BT coexistence)");
    }
    NimBLEDevice::init("");
    if (!NimBLEDevice::getInitialized()) {
        // init() is void and aborts internally on most failures, but a heap too
        // fragmented for the host to come up returns quietly. Reported as a
        // state the settings row can show rather than a silent no-op.
        Serial.println("[blekbd] NimBLE host did not come up");
        setState(BLE_KBD_FAILED, "radio init failed");
        return false;
    }
    // Bonding on, MITM not demanded, secure connections allowed. Demanding MITM
    // would fail outright against the keyboards that only do Just Works; not
    // demanding it still leaves the passkey path available for the many that
    // ask for a code, because of the IO capability below.
    NimBLEDevice::setSecurityAuth(true, false, true);
    // We have a screen and the keyboard has keys, which is exactly the pairing
    // the Passkey Entry method is for: we display, the user types it there.
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    s_stackUp = true;
    logInternalHeap("after init");
    Serial.printf("[blekbd] stack up, %d bond(s) stored\n", NimBLEDevice::getNumBonds());
    return true;
}

void stopStack() {
    if (!s_stackUp) return;
    if (s_client) {
        if (s_client->isConnected()) s_client->disconnect();
        NimBLEDevice::deleteClient(s_client);
        s_client = nullptr;
    }
    {
        Lock lock;
        s_batteryChar = nullptr;
        s_batteryPct  = 0xFF;
        s_scanCount   = 0;
    }
    clearKeys();
    clearHeld();
    // deinit(false) keeps the bond table, which is the whole point of pairing
    // once. Turning the feature off must not mean pairing again.
    NimBLEDevice::deinit(false);
    s_stackUp = false;
    logInternalHeap("after deinit");
}

bool subscribeToKeyboard(NimBLEClient *client) {
    NimBLERemoteService *hid = client->getService(NimBLEUUID(kUuidHidService));
    if (!hid) {
        Serial.println("[blekbd] no HID service (0x1812) on this device");
        return false;
    }

    NimBLERemoteCharacteristic *protocolMode = hid->getCharacteristic(NimBLEUUID(kUuidProtocolMode));
    NimBLERemoteCharacteristic *bootInput    = hid->getCharacteristic(NimBLEUUID(kUuidBootKbdIn));

    if (bootInput && bootInput->canNotify()) {
        if (protocolMode) {
            // Write Without Response is the only write this characteristic has.
            const uint8_t boot = 0x00;
            protocolMode->writeValue(&boot, 1, false);
        }
        if (bootInput->subscribe(true, onNotify)) {
            Serial.println("[blekbd] boot protocol: subscribed to 0x2A22");
            return true;
        }
        Serial.println("[blekbd] boot report subscribe failed, trying report protocol");
    }

    // Fallback for a keyboard that implements HOGP without the boot keyboard
    // role. The Report Map is not parsed -- an 8-byte input report is read as
    // the standard keyboard layout, which is what nearly all of them send, and
    // anything else is ignored by handleReport().
    if (protocolMode) {
        const uint8_t report = 0x01;
        protocolMode->writeValue(&report, 1, false);
    }
    int subscribed = 0;
    std::vector<NimBLERemoteCharacteristic *> *chars = hid->getCharacteristics(true);
    if (chars) {
        for (NimBLERemoteCharacteristic *chr : *chars) {
            if (!chr->canNotify()) continue;
            if (!chr->getUUID().equals(NimBLEUUID(kUuidReport))) continue;
            // Report Reference: [report id, report type]. Type 1 is Input;
            // Output (LEDs) and Feature reports notify nothing useful.
            NimBLERemoteDescriptor *ref = chr->getDescriptor(NimBLEUUID(kUuidReportRef));
            if (ref) {
                NimBLEAttValue value = ref->readValue();
                if (value.size() >= 2 && value.data()[1] != 0x01) continue;
            }
            if (chr->subscribe(true, onNotify)) subscribed++;
        }
    }
    if (subscribed > 0) {
        Serial.printf("[blekbd] report protocol: subscribed to %d input report(s)\n", subscribed);
        return true;
    }
    Serial.println("[blekbd] no usable keyboard input report");
    return false;
}

void readBatteryLevel() {
    NimBLERemoteCharacteristic *chr = nullptr;
    {
        Lock lock;
        chr = s_batteryChar;
    }
    if (!chr) return;
    const uint8_t pct = chr->readValue<uint8_t>();
    Lock lock;
    s_batteryPct = (pct <= 100) ? pct : 0xFF;
}

void attemptConnect() {
    if (!s_stackUp || !s_pairedAddr[0]) return;

    setState(BLE_KBD_CONNECTING, s_pairedName);
    Serial.printf("[blekbd] connecting to %s (%s)\n", s_pairedName, s_pairedAddr);

    if (!s_client) {
        s_client = NimBLEDevice::createClient();
        if (!s_client) {
            setState(BLE_KBD_FAILED, "no client slot");
            return;
        }
        s_client->setClientCallbacks(&s_clientCallbacks, false);
    }
    s_client->setConnectionParams(12, 24, 0, 400);
    s_client->setConnectTimeout(8);

    const NimBLEAddress addr(std::string(s_pairedAddr), s_pairedAddrType);
    const bool alreadyBonded = NimBLEDevice::isBonded(addr);
    if (!alreadyBonded) {
        // Fresh pairing: pick the code the keyboard's user will be asked to
        // type. Random per attempt rather than a fixed 123456, so a bystander
        // watching one pairing learns nothing about the next.
        Lock lock;
        s_passkey = 100000 + (uint32_t)esp_random() % 900000;
        NimBLEDevice::setSecurityPasskey(s_passkey);
    }

    if (!s_client->connect(addr, true)) {
        Serial.printf("[blekbd] connect failed (err %d)\n", s_client->getLastError());
        { Lock lock; s_passkey = 0; }
        setState(BLE_KBD_RETRYING, "not in range");
        s_retryDelay  = (s_retryDelay * 2 > kRetryMaxMs) ? kRetryMaxMs : s_retryDelay * 2;
        s_nextRetryMs = millis() + s_retryDelay;
        return;
    }

    // HID characteristics are readable only on an encrypted link, so security
    // has to come before discovery, not after it.
    if (!s_client->secureConnection()) {
        Serial.println("[blekbd] pairing/encryption failed");
        s_client->disconnect();
        { Lock lock; s_passkey = 0; }
        setState(BLE_KBD_FAILED, "pairing rejected");
        s_retryDelay  = kRetryMaxMs;
        s_nextRetryMs = millis() + s_retryDelay;
        return;
    }

    if (!subscribeToKeyboard(s_client)) {
        s_client->disconnect();
        { Lock lock; s_passkey = 0; }
        setState(BLE_KBD_FAILED, "not a keyboard");
        s_retryDelay  = kRetryMaxMs;
        s_nextRetryMs = millis() + s_retryDelay;
        return;
    }

    // Resolved before the lock is taken, never inside it. getCharacteristic()
    // is a GATT round-trip that only completes when the NimBLE host task runs,
    // and that task takes this same mutex on every notification -- holding it
    // across the call deadlocks the pair of them.
    NimBLERemoteService *battery = s_client->getService(NimBLEUUID(kUuidBatterySvc));
    NimBLERemoteCharacteristic *batteryChar =
        battery ? battery->getCharacteristic(NimBLEUUID(kUuidBatteryLevel)) : nullptr;
    {
        Lock lock;
        s_batteryChar = batteryChar;
        s_passkey = 0;
    }
    readBatteryLevel();

    memset(s_prevKeys, 0, sizeof(s_prevKeys));
    s_capsLock   = false;
    s_retryDelay = kRetryMinMs;
    setState(BLE_KBD_CONNECTED, s_pairedName);
    Serial.printf("[blekbd] connected to %s\n", s_pairedName);
}

void doScan() {
    if (!s_stackUp) return;
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan || scan->isScanning()) return;
    {
        Lock lock;
        s_scanCount = 0;
    }
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(&s_scanCallbacks, false);
    scan->setActiveScan(true);   // ask for scan responses; that is where names live
    scan->setInterval(100);
    scan->setWindow(80);
    setState(BLE_KBD_SCANNING);
    if (!scan->start(kScanSeconds, onScanEnded, false)) {
        setState(BLE_KBD_FAILED, "scan failed to start");
    }
}

void doPair(int index) {
    ScanEntry entry;
    {
        Lock lock;
        if (index < 0 || index >= s_scanCount) return;
        entry = s_scan[index];
    }
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();

    // A previous keyboard's link must go before a new one is opened; the client
    // is reused across pairings and can only hold one peer.
    if (s_client && s_client->isConnected()) s_client->disconnect();

    snprintf(s_pairedAddr, sizeof(s_pairedAddr), "%s", entry.addr);
    snprintf(s_pairedName, sizeof(s_pairedName), "%s", entry.name);
    s_pairedAddrType = entry.addrType;
    {
        Lock lock;
        s_pairingDirty = true;
    }
    s_retryDelay = kRetryMinMs;
    attemptConnect();
}

void doForget() {
    if (s_client && s_client->isConnected()) s_client->disconnect();
    if (s_stackUp && s_pairedAddr[0]) {
        NimBLEDevice::deleteBond(NimBLEAddress(std::string(s_pairedAddr), s_pairedAddrType));
    }
    s_pairedAddr[0] = '\0';
    s_pairedName[0] = '\0';
    s_pairedAddrType = 0;
    {
        Lock lock;
        s_pairingDirty = true;
        s_batteryPct   = 0xFF;
    }
    clearKeys();
    clearHeld();
    setState(s_stackUp ? BLE_KBD_IDLE : BLE_KBD_OFF);
}

void workerTask(void *) {
    uint32_t lastBatteryMs = 0;
    for (;;) {
        Cmd cmd;
        if (xQueueReceive(s_cmdQ, &cmd, pdMS_TO_TICKS(250)) == pdTRUE) {
            switch (cmd.op) {
                case CMD_START:
                    if (startStack()) {
                        if (s_pairedAddr[0]) {
                            s_retryDelay  = kRetryMinMs;
                            s_nextRetryMs = millis();
                            setState(BLE_KBD_RETRYING, s_pairedName);
                        } else {
                            setState(BLE_KBD_IDLE);
                        }
                    } else {
                        s_enabled = false;
                    }
                    break;
                case CMD_STOP:
                    stopStack();
                    setState(BLE_KBD_OFF);
                    break;
                case CMD_SCAN:   doScan();          break;
                case CMD_PAIR:   doPair(cmd.arg);   break;
                case CMD_FORGET: doForget();        break;
                default: break;
            }
            continue;
        }

        if (!s_stackUp || !s_enabled) continue;

        const uint32_t now = millis();
        const bool connected = s_client && s_client->isConnected();
        NimBLEScan *scan = NimBLEDevice::getScan();
        const bool scanning = scan && scan->isScanning();

        if (!connected && !scanning && s_pairedAddr[0]
            && (int32_t)(now - s_nextRetryMs) >= 0) {
            attemptConnect();
            continue;
        }
        if (connected && (uint32_t)(now - lastBatteryMs) >= 300000UL) {
            lastBatteryMs = now;
            readBatteryLevel();
        }
    }
}

void post(uint8_t op, int arg = 0) {
    if (!s_cmdQ) return;
    Cmd cmd = { op, arg };
    xQueueSend(s_cmdQ, &cmd, 0);
}

bool ensureWorker() {
    if (s_task) return true;
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    if (!s_cmdQ)  s_cmdQ  = xQueueCreate(4, sizeof(Cmd));
    if (!s_mutex || !s_cmdQ) return false;
    // 5 KB: GATT discovery recurses through the service/characteristic vectors
    // and the connect path keeps a scan-result copy on the stack. Low priority
    // -- nothing here is more urgent than the UI or the radio.
    if (xTaskCreate(workerTask, "blekbd", 5120, nullptr, 2, &s_task) != pdPASS) {
        s_task = nullptr;
        return false;
    }
    return true;
}

}  // namespace

// ── Public API ──────────────────────────────────────────────────────────────
void bleKeyboardBegin(bool enabled, const char *addr, uint8_t addrType, const char *name) {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    snprintf(s_pairedAddr, sizeof(s_pairedAddr), "%s", addr ? addr : "");
    snprintf(s_pairedName, sizeof(s_pairedName), "%s", name ? name : "");
    s_pairedAddrType = addrType;
    if (!enabled) {
        setState(BLE_KBD_OFF);
        return;
    }
    bleKeyboardSetEnabled(true);
}

bool bleKeyboardSetEnabled(bool on) {
    if (on == s_enabled) return s_enabled;
    if (on) {
        if (!ensureWorker()) {
            Serial.println("[blekbd] worker task could not start");
            return false;
        }
        s_enabled = true;
        post(CMD_START);
    } else {
        s_enabled = false;
        post(CMD_STOP);
    }
    return s_enabled;
}

bool bleKeyboardEnabled() { return s_enabled; }

void bleKeyboardService(uint32_t nowMs) {
    // Auto-repeat. The link delivers one report on press and one on release, so
    // holding a key is silent on the wire and the repeat has to be invented
    // here; keyboardHeldKey()/keyboardHeldMs() report the same held key, which
    // is what keeps long-press behaviours working.
    char key = KEY_NONE;
    {
        Lock lock;
        if (s_heldUsage == 0 || !keyRepeats(s_heldChar)) return;
        if ((int32_t)(nowMs - s_repeatNextMs) < 0) return;
        s_repeatNextMs = nowMs + kRepeatIntervalMs;
        key = s_heldChar;
    }
    pushKey(key);
}

bool bleKeyboardPopKey(char *outKey) {
    if (!s_enabled) return false;
    portENTER_CRITICAL(&s_keyMux);
    if (s_keyHead == s_keyTail) {
        portEXIT_CRITICAL(&s_keyMux);
        return false;
    }
    if (outKey) *outKey = s_keyRing[s_keyTail];
    s_keyTail = (s_keyTail + 1) % kKeyRingSize;
    portEXIT_CRITICAL(&s_keyMux);
    return true;
}

char bleKeyboardHeldKey() {
    Lock lock;
    return s_heldUsage ? s_heldChar : KEY_NONE;
}

uint32_t bleKeyboardHeldMs() {
    Lock lock;
    if (!s_heldUsage || s_heldSinceMs == 0) return 0;
    return millis() - s_heldSinceMs;
}

uint8_t bleKeyboardState() {
    Lock lock;
    return s_state;
}

bool bleKeyboardConnected() {
    Lock lock;
    return s_state == BLE_KBD_CONNECTED;
}

bool bleKeyboardActive() { return s_enabled || s_stackUp; }

bool bleKeyboardHoldsWifiModemSleep() { return s_enabled || s_stackUp; }

void bleKeyboardStatusText(char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    uint8_t state;
    char detail[sizeof(s_detail)];
    uint8_t battery;
    {
        Lock lock;
        state   = s_state;
        battery = s_batteryPct;
        snprintf(detail, sizeof(detail), "%s", s_detail);
    }
    switch (state) {
        case BLE_KBD_SCANNING:
            snprintf(out, outLen, "Scanning...");
            break;
        case BLE_KBD_CONNECTING:
            snprintf(out, outLen, "Connecting to %s...",
                     s_pairedName[0] ? s_pairedName : "keyboard");
            break;
        case BLE_KBD_CONNECTED:
            if (battery <= 100) {
                snprintf(out, outLen, "Connected: %s (%u%%)", s_pairedName, (unsigned)battery);
            } else {
                snprintf(out, outLen, "Connected: %s", s_pairedName);
            }
            break;
        case BLE_KBD_RETRYING:
            snprintf(out, outLen, "Waiting for %s%s%s",
                     s_pairedName[0] ? s_pairedName : "keyboard",
                     detail[0] ? " - " : "", detail);
            break;
        case BLE_KBD_FAILED:
            snprintf(out, outLen, "Failed%s%s", detail[0] ? ": " : "", detail);
            break;
        case BLE_KBD_IDLE:
            snprintf(out, outLen, "On - no keyboard paired%s%s",
                     detail[0] ? " (" : "", detail[0] ? detail : "");
            if (detail[0]) {
                const size_t used = strlen(out);
                if (used + 2 <= outLen) snprintf(out + used, outLen - used, ")");
            }
            break;
        default:
            snprintf(out, outLen, "Off");
            break;
    }
}

uint32_t bleKeyboardPasskey() {
    Lock lock;
    return s_passkey;
}

uint8_t bleKeyboardBatteryPct() {
    Lock lock;
    return s_batteryPct;
}

bool bleKeyboardStartScan() {
    if (!s_enabled || !s_stackUp) return false;
    if (bleKeyboardScanning()) return false;
    post(CMD_SCAN);
    return true;
}

bool bleKeyboardScanning() {
    Lock lock;
    return s_state == BLE_KBD_SCANNING;
}

int bleKeyboardScanCount() {
    Lock lock;
    return s_scanCount;
}

bool bleKeyboardScanEntry(int idx, char *name, size_t nameLen,
                          char *addr, size_t addrLen, int *rssi, bool *hid) {
    Lock lock;
    if (idx < 0 || idx >= s_scanCount) return false;
    const ScanEntry &entry = s_scan[idx];
    if (name && nameLen) snprintf(name, nameLen, "%s", entry.name);
    if (addr && addrLen) snprintf(addr, addrLen, "%s", entry.addr);
    if (rssi) *rssi = entry.rssi;
    if (hid)  *hid  = entry.hidService;
    return true;
}

bool bleKeyboardPairWith(int idx) {
    if (!s_enabled || !s_stackUp) return false;
    {
        Lock lock;
        if (idx < 0 || idx >= s_scanCount) return false;
    }
    post(CMD_PAIR, idx);
    return true;
}

void bleKeyboardForget() {
    if (!s_task) {
        s_pairedAddr[0] = '\0';
        s_pairedName[0] = '\0';
        s_pairedAddrType = 0;
        Lock lock;
        s_pairingDirty = true;
        return;
    }
    post(CMD_FORGET);
}

bool        bleKeyboardHasPaired()      { return s_pairedAddr[0] != '\0'; }
const char *bleKeyboardPairedName()     { return s_pairedName; }
const char *bleKeyboardPairedAddr()     { return s_pairedAddr; }
uint8_t     bleKeyboardPairedAddrType() { return s_pairedAddrType; }

bool bleKeyboardTakePairingDirty() {
    Lock lock;
    const bool dirty = s_pairingDirty;
    s_pairingDirty = false;
    return dirty;
}

#endif  // HAS_BLE_KEYBOARD
