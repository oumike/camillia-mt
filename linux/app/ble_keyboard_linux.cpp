// SPDX-License-Identifier: GPL-3.0-or-later
// ble_keyboard.h on the CardputerZero: Bluetooth keyboards through the OS.
//
// BlueZ owns the controller. Scanning and pairing drive `bluetoothctl` on a
// worker thread (pairing interactively, so a passkey the keyboard asks for can
// be shown the way the ESP32 boards show it). Once connected, BlueZ hands the
// keyboard to the kernel as an ordinary evdev device whose uniq is its address;
// that device is grabbed and decoded by the same Keyboard class as the keypad,
// in its standard-layout mode. Reconnecting a trusted keyboard is BlueZ's job.
#include "ble_keyboard.h"

#include <Arduino.h>
#include <ctype.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "keyboard.h"
#include "ui/keyboard.h"

namespace {

constexpr int kScanSeconds = 10;
constexpr int kMaxScan = 16;
constexpr uint32_t kPairTimeoutMs = 60000;
constexpr uint32_t kLookMs = 1000;            // evdev re-check while not connected
constexpr uint32_t kReconnectMinMs = 15000;   // nudges for an absent keyboard
constexpr uint32_t kReconnectMaxMs = 120000;

struct ScanEntry {
    char name[32];
    char addr[20];
    uint8_t addrType;
    int rssi;
    bool hidService;
};

enum Op { OP_SCAN, OP_PAIR, OP_FORGET, OP_CONNECT, OP_POWER };

std::mutex s_mu;
std::condition_variable s_cv;
std::deque<std::pair<Op, std::string>> s_ops;

bool s_enabled = false;
uint8_t s_state = BLE_KBD_OFF;
char s_detail[48] = {};
char s_pairedAddr[20] = {};
char s_pairedName[32] = {};
uint8_t s_pairedAddrType = 0;
bool s_pairingDirty = false;
uint32_t s_passkey = 0;
ScanEntry s_scan[kMaxScan];
int s_scanCount = 0;
bool s_busy = false;   // scan / pair in flight on the worker

// Main-thread only.
Keyboard s_kbd;
uint32_t s_nextLookMs = 0;
uint32_t s_nextReconnectMs = 0;
uint32_t s_reconnectDelay = kReconnectMinMs;
std::deque<char> s_keys;

void setStateLocked(uint8_t st, const char *detail = nullptr) {
    s_state = st;
    snprintf(s_detail, sizeof s_detail, "%s", detail ? detail : "");
}

void setState(uint8_t st, const char *detail = nullptr) {
    std::lock_guard<std::mutex> l(s_mu);
    setStateLocked(st, detail);
}

void post(Op op, const std::string &arg = std::string()) {
    {
        std::lock_guard<std::mutex> l(s_mu);
        s_ops.emplace_back(op, arg);
    }
    s_cv.notify_one();
}

// Drops colour codes and readline prompt markers from bluetoothctl output.
std::string clean(const std::string &in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '\x1b') {
            while (i < in.size() && !isalpha((unsigned char)in[i])) i++;
            continue;
        }
        if (c == '\x01' || c == '\x02' || c == '\r') continue;
        out += c;
    }
    return out;
}

// One-shot `bluetoothctl <args>`, output returned. Bounded by `timeout`.
std::string btctl(const std::string &args, int timeoutS = 8) {
    std::string cmd = "timeout -k 2 " + std::to_string(timeoutS) + " bluetoothctl " + args + " 2>&1";
    std::string out;
    if (FILE *f = popen(cmd.c_str(), "r")) {
        char buf[512];
        while (fgets(buf, sizeof buf, f)) out += buf;
        pclose(f);
    }
    return clean(out);
}

// Interactive bluetoothctl session, for the agent's passkey prompts.
class Session {
public:
    bool start() {
        int in[2], out[2];
        if (pipe(in) || pipe(out)) return false;
        _pid = fork();
        if (_pid < 0) return false;
        if (_pid == 0) {
            dup2(in[0], 0);
            dup2(out[1], 1);
            dup2(out[1], 2);
            ::close(in[1]);
            ::close(out[0]);
            execlp("bluetoothctl", "bluetoothctl", (char *)nullptr);
            _exit(127);
        }
        ::close(in[0]);
        ::close(out[1]);
        _in = in[1];
        _out = out[0];
        return true;
    }
    ~Session() {
        if (_in >= 0) {
            send("quit");
            ::close(_in);
        }
        if (_out >= 0) ::close(_out);
        if (_pid > 0) {
            for (int i = 0; i < 20 && waitpid(_pid, nullptr, WNOHANG) == 0; i++) usleep(50000);
            if (waitpid(_pid, nullptr, WNOHANG) == 0) {
                kill(_pid, SIGKILL);
                waitpid(_pid, nullptr, 0);
            }
        }
    }
    void send(const std::string &line) {
        std::string l = line + "\n";
        (void)!write(_in, l.data(), l.size());
    }
    // Next output line, or false on timeout / exit.
    bool line(std::string &out, uint32_t timeoutMs) {
        uint32_t start = millis();
        for (;;) {
            size_t nl = _buf.find('\n');
            if (nl != std::string::npos) {
                out = clean(_buf.substr(0, nl));
                _buf.erase(0, nl + 1);
                return true;
            }
            // Prompts such as "Confirm passkey ... (yes/no):" end without a newline.
            if (_buf.find("(yes/no)") != std::string::npos) {
                out = clean(_buf);
                _buf.clear();
                return true;
            }
            uint32_t el = millis() - start;
            if (el >= timeoutMs) return false;
            pollfd p{_out, POLLIN, 0};
            if (poll(&p, 1, (int)(timeoutMs - el)) <= 0) return false;
            char b[256];
            ssize_t n = ::read(_out, b, sizeof b);
            if (n <= 0) return false;
            _buf.append(b, (size_t)n);
        }
    }

private:
    pid_t _pid = -1;
    int _in = -1, _out = -1;
    std::string _buf;
};

bool isAddr(const char *s) {
    unsigned v[6];
    return sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6;
}

std::string field(const std::string &info, const char *key) {
    size_t p = info.find(key);
    if (p == std::string::npos) return std::string();
    p += strlen(key);
    while (p < info.size() && info[p] == ' ') p++;
    size_t e = info.find('\n', p);
    return info.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

void doScan() {
    {
        std::lock_guard<std::mutex> l(s_mu);
        s_scanCount = 0;
        setStateLocked(BLE_KBD_SCANNING);
    }
    Serial.println("[blekbd] scan starting (BlueZ)");
    std::string out = btctl("--timeout " + std::to_string(kScanSeconds) + " scan on", kScanSeconds + 5);
    std::vector<std::string> seen;
    size_t pos = 0;
    while ((pos = out.find("Device ", pos)) != std::string::npos) {
        pos += 7;
        std::string a = out.substr(pos, 17);
        if (isAddr(a.c_str()) && std::find(seen.begin(), seen.end(), a) == seen.end()) seen.push_back(a);
    }
    std::vector<ScanEntry> found;
    for (const std::string &a : seen) {
        if (found.size() >= (size_t)kMaxScan) break;
        std::string info = btctl("info " + a, 4);
        bool hid = info.find("00001812-") != std::string::npos;
        bool looksKbd = info.find("Icon: input-keyboard") != std::string::npos;
        if (!hid && !looksKbd) continue;
        ScanEntry e{};
        std::string name = field(info, "Name:");
        snprintf(e.name, sizeof e.name, "%s", name.empty() ? a.c_str() : name.c_str());
        snprintf(e.addr, sizeof e.addr, "%s", a.c_str());
        e.addrType = info.find("(random)") != std::string::npos ? 1 : 0;
        // "RSSI: 0xffffffc4 (-60)" or "RSSI: -60"
        std::string r = field(info, "RSSI:");
        size_t paren = r.find('(');
        e.rssi = r.empty() ? -127 : atoi(paren != std::string::npos ? r.c_str() + paren + 1 : r.c_str());
        e.hidService = hid;
        found.push_back(e);
    }
    std::sort(found.begin(), found.end(), [](const ScanEntry &x, const ScanEntry &y) { return x.rssi > y.rssi; });
    std::lock_guard<std::mutex> l(s_mu);
    s_scanCount = (int)found.size();
    for (int i = 0; i < s_scanCount; i++) s_scan[i] = found[i];
    Serial.printf("[blekbd] scan done: %d keyboard(s)\n", s_scanCount);
    setStateLocked(s_pairedAddr[0] ? BLE_KBD_RETRYING : BLE_KBD_IDLE);
}

void doPair(const std::string &addr) {
    Serial.printf("[blekbd] pairing %s\n", addr.c_str());
    Session s;
    if (!s.start()) {
        setState(BLE_KBD_FAILED, "bluetoothctl not found");
        return;
    }
    s.send("agent KeyboardDisplay");
    s.send("default-agent");
    // A device only scanned a while ago may have aged out of BlueZ's cache.
    s.send("scan on");
    std::string ln;
    uint32_t until = millis() + 4000;
    while ((int32_t)(until - millis()) > 0) s.line(ln, until - millis());
    s.send("scan off");
    s.send("pair " + addr);
    bool ok = false;
    std::string why = "timed out";
    uint32_t start = millis();
    while (millis() - start < kPairTimeoutMs) {
        if (!s.line(ln, kPairTimeoutMs - (millis() - start))) break;
        if (ln.find("Passkey:") != std::string::npos || ln.find("Confirm passkey") != std::string::npos) {
            size_t p = ln.find_first_of("0123456789", ln.find("asskey"));
            if (p != std::string::npos) {
                std::lock_guard<std::mutex> l(s_mu);
                s_passkey = (uint32_t)strtoul(ln.c_str() + p, nullptr, 10);
            }
            if (ln.find("(yes/no)") != std::string::npos) s.send("yes");
        } else if (ln.find("Pairing successful") != std::string::npos
                   || ln.find("AlreadyExists") != std::string::npos) {
            ok = true;
            break;
        } else if (ln.find("Failed to pair") != std::string::npos) {
            size_t p = ln.find("org.bluez.Error.");
            why = p != std::string::npos ? ln.substr(p + 16) : "refused";
            break;
        }
    }
    {
        std::lock_guard<std::mutex> l(s_mu);
        s_passkey = 0;
    }
    if (!ok) {
        Serial.printf("[blekbd] pairing failed: %s\n", why.c_str());
        setState(BLE_KBD_FAILED, why.c_str());
        return;
    }
    s.send("trust " + addr);
    s.send("connect " + addr);
    start = millis();
    while (millis() - start < 15000 && s.line(ln, 15000 - (millis() - start)))
        if (ln.find("Connection successful") != std::string::npos || ln.find("Failed to connect") != std::string::npos)
            break;
    Serial.printf("[blekbd] paired %s\n", addr.c_str());
    std::lock_guard<std::mutex> l(s_mu);
    setStateLocked(BLE_KBD_RETRYING, "connecting");
}

void workerMain() {
    for (;;) {
        std::pair<Op, std::string> op;
        {
            std::unique_lock<std::mutex> l(s_mu);
            s_cv.wait(l, [] { return !s_ops.empty(); });
            op = s_ops.front();
            s_ops.pop_front();
            s_busy = op.first == OP_SCAN || op.first == OP_PAIR;
        }
        switch (op.first) {
        case OP_POWER: btctl("power on"); break;
        case OP_SCAN: doScan(); break;
        case OP_PAIR: doPair(op.second); break;
        case OP_CONNECT: btctl("connect " + op.second, 20); break;
        case OP_FORGET: btctl("remove " + op.second); break;
        }
        std::lock_guard<std::mutex> l(s_mu);
        s_busy = false;
    }
}

bool s_workerStarted = false;

void startWorkerOnce() {
    if (s_workerStarted) return;
    s_workerStarted = true;
    std::thread(workerMain).detach();
}

char mapKey(const KeyEvent &e) {
    switch (e.key) {
    case Key::Enter: return KEY_ENTER;
    case Key::Backspace: return KEY_BACKSPACE;
    case Key::Delete: return KEY_BACKSPACE;   // as on the ESP32 boards
    case Key::Esc: return KEY_ESCAPE;
    case Key::Tab: return KEY_TAB;
    case Key::Up: return KEY_SCROLL_UP;
    case Key::Down: return KEY_SCROLL_DN;
    case Key::Left: return KEY_PREV_CHAN;
    case Key::Right: return KEY_NEXT_CHAN;
    case Key::PageUp: return KEY_PAGE_UP;
    case Key::PageDown: return KEY_PAGE_DN;
    default: return KEY_NONE;
    }
}

}  // namespace

void bleKeyboardBegin(bool enabled, const char *addr, uint8_t addrType, const char *name) {
    snprintf(s_pairedAddr, sizeof s_pairedAddr, "%s", addr ? addr : "");
    snprintf(s_pairedName, sizeof s_pairedName, "%s", name ? name : "");
    s_pairedAddrType = addrType;
    if (!enabled) {
        setState(BLE_KBD_OFF);
        return;
    }
    bleKeyboardSetEnabled(true);
}

bool bleKeyboardSetEnabled(bool on) {
    if (on == s_enabled) return s_enabled;
    s_enabled = on;
    if (on) {
        startWorkerOnce();
        post(OP_POWER);
        setState(s_pairedAddr[0] ? BLE_KBD_RETRYING : BLE_KBD_IDLE);
        s_nextLookMs = 0;
        s_nextReconnectMs = millis() + 3000;
        s_reconnectDelay = kReconnectMinMs;
    } else {
        // The controller stays up for the rest of the system; just let go of
        // the keyboard so it types into nothing of ours.
        s_kbd.close();
        s_keys.clear();
        setState(BLE_KBD_OFF);
    }
    return s_enabled;
}

bool bleKeyboardEnabled() { return s_enabled; }

void bleKeyboardService(uint32_t nowMs) {
    if (!s_enabled || !s_pairedAddr[0]) return;
    if (s_kbd.fd() < 0) {
        if ((int32_t)(nowMs - s_nextLookMs) < 0) return;
        s_nextLookMs = nowMs + kLookMs;
        int fd = openKeyboardByUniq(s_pairedAddr);
        if (fd >= 0) {
            s_kbd.adoptStandard(fd);
            s_reconnectDelay = kReconnectMinMs;
            Serial.printf("[blekbd] connected: %s\n", s_pairedName[0] ? s_pairedName : s_pairedAddr);
            std::lock_guard<std::mutex> l(s_mu);
            if (!s_busy) setStateLocked(BLE_KBD_CONNECTED);
        } else {
            bool busy;
            {
                std::lock_guard<std::mutex> l(s_mu);
                busy = s_busy;
                if (!busy && s_state == BLE_KBD_CONNECTED) setStateLocked(BLE_KBD_RETRYING);
            }
            // Trusted keyboards normally come back on their own; nudge now and then.
            if (!busy && (int32_t)(nowMs - s_nextReconnectMs) >= 0) {
                post(OP_CONNECT, s_pairedAddr);
                s_nextReconnectMs = nowMs + s_reconnectDelay;
                s_reconnectDelay = std::min(s_reconnectDelay * 2, kReconnectMaxMs);
            }
        }
        return;
    }
    for (;;) {
        KeyEvent e = s_kbd.read();
        if (e.key == Key::None) break;
        if (e.key == Key::Char) {
            for (unsigned char ch : e.text)
                if (ch >= 0x20 && ch < 0x7f && s_keys.size() < 32) s_keys.push_back((char)ch);
        } else if (char k = mapKey(e)) {
            if (s_keys.size() < 32) s_keys.push_back(k);
        }
    }
    if (s_kbd.fd() < 0) {
        Serial.println("[blekbd] keyboard disconnected");
        s_nextReconnectMs = nowMs + 3000;
        setState(BLE_KBD_RETRYING);
    }
}

bool bleKeyboardPopKey(char *outKey) {
    if (!s_enabled || s_keys.empty()) return false;
    if (outKey) *outKey = s_keys.front();
    s_keys.pop_front();
    return true;
}

// Kernel auto-repeat already arrives as presses, so no held-key synthesis.
char bleKeyboardHeldKey() { return KEY_NONE; }
uint32_t bleKeyboardHeldMs() { return 0; }

uint8_t bleKeyboardState() {
    std::lock_guard<std::mutex> l(s_mu);
    return s_state;
}

bool bleKeyboardConnected() { return bleKeyboardState() == BLE_KBD_CONNECTED; }

// Nothing here sleeps the CPU underneath the link.
bool bleKeyboardActive() { return false; }
bool bleKeyboardHoldsWifiModemSleep() { return false; }

void bleKeyboardStatusText(char *out, size_t outLen) {
    if (!out || !outLen) return;
    uint8_t st;
    char detail[sizeof s_detail];
    {
        std::lock_guard<std::mutex> l(s_mu);
        st = s_state;
        snprintf(detail, sizeof detail, "%s", s_detail);
    }
    const char *name = s_pairedName[0] ? s_pairedName : "keyboard";
    uint8_t bat = bleKeyboardBatteryPct();
    switch (st) {
    case BLE_KBD_SCANNING: snprintf(out, outLen, "Scanning..."); break;
    case BLE_KBD_CONNECTING: snprintf(out, outLen, "Connecting to %s...", name); break;
    case BLE_KBD_CONNECTED:
        if (bat <= 100) snprintf(out, outLen, "Connected: %s (%u%%)", name, (unsigned)bat);
        else snprintf(out, outLen, "Connected: %s", name);
        break;
    case BLE_KBD_RETRYING:
        snprintf(out, outLen, "Waiting for %s%s%s", name, detail[0] ? " - " : "", detail);
        break;
    case BLE_KBD_FAILED: snprintf(out, outLen, "Failed%s%s", detail[0] ? ": " : "", detail); break;
    case BLE_KBD_IDLE: snprintf(out, outLen, "On - no keyboard paired"); break;
    default: snprintf(out, outLen, "Off"); break;
    }
}

uint32_t bleKeyboardPasskey() {
    std::lock_guard<std::mutex> l(s_mu);
    return s_passkey;
}

// BlueZ exposes a HID keyboard's battery as hid-<addr>-battery.
uint8_t bleKeyboardBatteryPct() {
    if (!s_pairedAddr[0]) return 0xFF;
    std::string a = s_pairedAddr;
    for (char &c : a) c = (char)tolower((unsigned char)c);
    std::string path = "/sys/class/power_supply/hid-" + a + "-battery/capacity";
    int pct = -1;
    if (FILE *f = fopen(path.c_str(), "r")) {
        if (fscanf(f, "%d", &pct) != 1) pct = -1;
        fclose(f);
    }
    return pct >= 0 && pct <= 100 ? (uint8_t)pct : 0xFF;
}

bool bleKeyboardStartScan() {
    if (!s_enabled) return false;
    {
        std::lock_guard<std::mutex> l(s_mu);
        if (s_busy || s_state == BLE_KBD_SCANNING) return false;
        setStateLocked(BLE_KBD_SCANNING);
    }
    post(OP_SCAN);
    return true;
}

bool bleKeyboardScanning() { return bleKeyboardState() == BLE_KBD_SCANNING; }

int bleKeyboardScanCount() {
    std::lock_guard<std::mutex> l(s_mu);
    return s_scanCount;
}

bool bleKeyboardScanEntry(int idx, char *name, size_t nameLen, char *addr, size_t addrLen, int *rssi, bool *hid) {
    std::lock_guard<std::mutex> l(s_mu);
    if (idx < 0 || idx >= s_scanCount) return false;
    const ScanEntry &e = s_scan[idx];
    if (name && nameLen) snprintf(name, nameLen, "%s", e.name);
    if (addr && addrLen) snprintf(addr, addrLen, "%s", e.addr);
    if (rssi) *rssi = e.rssi;
    if (hid) *hid = e.hidService;
    return true;
}

bool bleKeyboardPairWith(int idx) {
    if (!s_enabled) return false;
    std::string addr;
    {
        std::lock_guard<std::mutex> l(s_mu);
        if (s_busy || idx < 0 || idx >= s_scanCount) return false;
        const ScanEntry &e = s_scan[idx];
        if (s_pairedAddr[0] && strcasecmp(s_pairedAddr, e.addr) != 0) s_ops.emplace_back(OP_FORGET, s_pairedAddr);
        snprintf(s_pairedAddr, sizeof s_pairedAddr, "%s", e.addr);
        snprintf(s_pairedName, sizeof s_pairedName, "%s", e.name);
        s_pairedAddrType = e.addrType;
        s_pairingDirty = true;
        addr = e.addr;
        setStateLocked(BLE_KBD_CONNECTING, s_pairedName);
    }
    s_kbd.close();
    post(OP_PAIR, addr);
    return true;
}

void bleKeyboardForget() {
    if (s_pairedAddr[0] && s_workerStarted) post(OP_FORGET, s_pairedAddr);
    s_kbd.close();
    s_keys.clear();
    std::lock_guard<std::mutex> l(s_mu);
    s_pairedAddr[0] = s_pairedName[0] = '\0';
    s_pairedAddrType = 0;
    s_pairingDirty = true;
    setStateLocked(s_enabled ? BLE_KBD_IDLE : BLE_KBD_OFF);
}

// BlueZ re-pairs on its own terms; a lost bond surfaces as a failed connect.
bool bleKeyboardRepairNeeded() { return false; }
bool bleKeyboardTakeRepairPrompt() { return false; }
void bleKeyboardRepairReason(char *out, size_t outLen) {
    if (out && outLen) out[0] = '\0';
}
bool bleKeyboardRepair() { return false; }

bool bleKeyboardHasPaired() { return s_pairedAddr[0] != '\0'; }
const char *bleKeyboardPairedName() { return s_pairedName; }
const char *bleKeyboardPairedAddr() { return s_pairedAddr; }
uint8_t bleKeyboardPairedAddrType() { return s_pairedAddrType; }

bool bleKeyboardTakePairingDirty() {
    std::lock_guard<std::mutex> l(s_mu);
    bool d = s_pairingDirty;
    s_pairingDirty = false;
    return d;
}
