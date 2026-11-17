// SPDX-License-Identifier: GPL-3.0-or-later
// Camillia-MT radio daemon for the M5Stack CardputerZero (issue #155).
//
// The radio and protocol code is the firmware's own (mesh_radio, mesh_proto,
// relay_policy); this file stands in for the parts of main_lvgl.cpp a Linux
// node needs: identity, a node table, receive handling, ACKs, NodeInfo,
// position, relaying and message history. The screen is a separate program
// (camillia-czero-ui) that talks to this one over a Unix socket; see
// common/ipc.h. When stdin is a terminal a line console is available too.
#include "mesh_channel_plan.h"
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "relay_policy.h"
#include "xeddsa_sig.h"
#include "debug_flags.h"
#include "licensed_mode.h"
#include "utf8_utils.h"

#include "common/ipc.h"
#include "daemon/gps.h"
#include "daemon/settings.h"
#include "daemon/store.h"

#include <Curve25519.h>
#include <esp_random.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <list>
#include <map>
#include <string>
#include <vector>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

uint8_t myPubKey[32];
uint8_t myPrivKey[32];
uint8_t myDeviceRole = MY_DEVICE_ROLE;

namespace {

constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr uint32_t kNodeInfoReplyGapMs = 60u * 1000u;
constexpr uint32_t kDmRetryMs = 20u * 1000u;
constexpr int kDmTries = 3;
constexpr size_t kSeen = 256;
constexpr size_t kRelayQueue = 16;
constexpr size_t kClientOutMax = 4u << 20;

struct Peer {
    std::string longName, shortName;
    uint8_t pubKey[32] = {};
    bool hasPubKey = false;
    float rssi = 0, snr = 0;
    int hops = -1;
    uint32_t lastHeard = 0;          // Unix seconds, persisted
    uint32_t lastNodeInfoReplyMs = 0;
};

struct PendingRelay {
    bool active = false;
    bool late = false;
    uint8_t frame[MESH_MAX_FRAME_BYTES];
    size_t len = 0;
    uint32_t sendAtMs = 0, from = 0, id = 0;
    float snr = 0;
};

// A DM waiting for its ACK; retransmitted (same id) until it comes.
struct PendingDm {
    uint32_t id = 0;
    uint8_t frame[MESH_MAX_FRAME_BYTES];
    size_t len = 0;
    uint32_t nextMs = 0;
    int tries = 0;
};

struct Client {
    int fd;
    std::string in, out;
};

volatile sig_atomic_t gQuit = 0;
Settings gCfg;
uint32_t gMe = 0;
std::string gLong, gShort, gDir;
std::map<uint32_t, Peer> gPeers;
uint32_t gSeen[kSeen][2];
size_t gSeenNext = 0;
uint32_t gLastNodeInfoMs = 0, gLastPositionMs = 0;
bool gPositionSent = false;
PendingRelay gRelay[kRelayQueue];
uint32_t gRelaySlotMs = 0;
uint32_t gRelayed = 0, gStoodDown = 0;
std::list<PendingDm> gPendingDms;
MessageStore gStore;
Gps gGps;
bool gGpsOpen = false;
int gListenFd = -1;
std::string gSocketPath;
std::list<Client> gClients;
float gFreq = MESH_FREQ;

void onSignal(int) { gQuit = 1; }

uint32_t nowEpoch() {
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;   // 0 until the clock is set
}

std::string hex(const uint8_t *b, size_t n) {
    static const char *d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
    return s;
}
bool unhex(const std::string &s, uint8_t *out, size_t n) {
    if (s.size() != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s.c_str() + 2 * i, "%2x", &v) != 1) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

std::string peerLabel(uint32_t id) {
    char b[16];
    snprintf(b, sizeof b, "!%08x", id);
    auto it = gPeers.find(id);
    if (it != gPeers.end() && !it->second.shortName.empty())
        return it->second.shortName + " (" + b + ")";
    return b;
}

int hopsAway(uint8_t flags) {
    const uint8_t hopLimit = flags & 7, hopStart = flags >> 5;
    return hopStart && hopStart >= hopLimit ? hopStart - hopLimit : -1;
}

// ── Socket clients ──────────────────────────────────────────────────────────

void flushClient(Client &c) {
    while (!c.out.empty()) {
        ssize_t n = send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) { c.out.erase(0, (size_t)n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        close(c.fd);
        c.fd = -1;
        return;
    }
}

void sendTo(Client &c, const std::string &line) {
    if (c.fd < 0) return;
    c.out += line;
    c.out += '\n';
    if (c.out.size() > kClientOutMax) {   // not reading: let it reconnect and replay
        close(c.fd);
        c.fd = -1;
        return;
    }
    flushClient(c);
}

void publish(const std::string &line) {
    for (auto &c : gClients) sendTo(c, line);
}

std::string identityRecord() {
    return ipc::join(std::string("I"), ipc::hex32(gMe), ipc::esc(gLong), ipc::esc(gShort), gCfg.region,
                     std::string(kPresets[gCfg.preset].channelName), std::string(APP_VERSION),
                     std::string(roleName(gCfg.role)), ipc::fmt("%.3f", gFreq), gCfg.rxOnly ? "1" : "0");
}

std::string nodeRecord(uint32_t id, const Peer &p) {
    return ipc::join(std::string("N"), ipc::hex32(id), ipc::esc(p.shortName), ipc::esc(p.longName),
                     p.hasPubKey ? "1" : "0", std::to_string(p.lastHeard), ipc::fmt("%.0f", p.rssi),
                     ipc::fmt("%.2f", p.snr), std::to_string(p.hops));
}

std::string gpsRecord() {
    const GpsFix &f = gGps.fix();
    bool ok = gGpsOpen && gGps.current();
    return ipc::join(std::string("G"), ok ? "1" : "0", ipc::fmt("%.7f", ok ? f.latI / 1e7 : 0),
                     ipc::fmt("%.7f", ok ? f.lonI / 1e7 : 0), std::to_string(ok ? f.altM : 0),
                     std::to_string(f.sats));
}

void replayTo(Client &c) {
    sendTo(c, identityRecord());
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ck = CHANNEL_KEYS[i];
        if (ck.role == 2) continue;
        sendTo(c, ipc::join(std::string("C"), std::to_string(i), ipc::esc(ck.name_buf[0] ? ck.name_buf : ck.name)));
    }
    for (auto &kv : gPeers) sendTo(c, nodeRecord(kv.first, kv.second));
    for (auto &m : gStore.all()) sendTo(c, m.record());
    sendTo(c, gpsRecord());
    sendTo(c, "E");
}

bool openSocket(const std::string &path) {
    gListenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    if (gListenFd < 0 || path.size() >= sizeof addr.sun_path) return false;
    strcpy(addr.sun_path, path.c_str());
    unlink(path.c_str());
    mode_t old = umask(007);   // the UI may run as another user in our group
    bool ok = bind(gListenFd, (sockaddr *)&addr, sizeof addr) == 0 && listen(gListenFd, 4) == 0;
    umask(old);
    if (!ok) {
        fprintf(stderr, "[ipc] %s: %s\n", path.c_str(), strerror(errno));
        close(gListenFd);
        gListenFd = -1;
        return false;
    }
    gSocketPath = path;
    return true;
}

// ── Persistence: ~/.config/camillia/{identity,nodes} ───────────────────────
// Plain text so it can be inspected and backed up. The identity file holds the
// private key and is created 0600.

std::string configDir() {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    std::string base = (xdg && *xdg) ? xdg : std::string(home ? home : ".") + "/.config";
    mkdir(base.c_str(), 0755);
    std::string dir = base + "/camillia";
    mkdir(dir.c_str(), 0700);
    return dir;
}

void saveIdentity() {
    std::string path = gDir + "/identity";
    mode_t old = umask(077);
    FILE *f = fopen(path.c_str(), "w");
    umask(old);
    if (!f) { perror(path.c_str()); return; }
    fprintf(f, "priv %s\nlong %s\nshort %s\n", hex(myPrivKey, 32).c_str(), gLong.c_str(), gShort.c_str());
    fclose(f);
}

void loadIdentity(const char *longArg, const char *shortArg) {
    std::string path = gDir + "/identity";
    bool havePriv = false;
    if (FILE *f = fopen(path.c_str(), "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "priv ", 5)) havePriv = unhex(line + 5, myPrivKey, 32);
            else if (!strncmp(line, "long ", 5)) gLong = line + 5;
            else if (!strncmp(line, "short ", 6)) gShort = line + 6;
        }
        fclose(f);
    }
    bool dirty = false;
    if (!havePriv) {
        Curve25519::dh1(myPubKey, myPrivKey);
        Serial.println("[id] generated new Curve25519 identity");
        dirty = true;
    }
    // Curve25519::eval uses the scalar as given; XEdDSA clamps it. Keep them in
    // agreement (meshtastic/firmware#12095).
    if (curve25519ClampPrivKey(myPrivKey)) dirty = true;
    Curve25519::eval(myPubKey, myPrivKey, nullptr);

    // Meshtastic 2.8: node number = crc32(public key). That is what lets a 2.8
    // node accept our first signed NodeInfo.
    gMe = meshCrc32(myPubKey, 32);
    if (gMe == 0 || gMe == kBroadcast) gMe = esp_random() | 1u;

    if (longArg) { gLong = longArg; dirty = true; }
    if (shortArg) { gShort = shortArg; dirty = true; }
    if (gLong.empty()) {
        char b[32];
        snprintf(b, sizeof b, "Camillia Zero %04x", gMe & 0xFFFF);
        gLong = b;
        dirty = true;
    }
    if (gShort.empty()) {
        char b[8];
        snprintf(b, sizeof b, "%04x", gMe & 0xFFFF);
        gShort = b;
        dirty = true;
    }
    char tmp[MESH_LONG_NAME_MAX_BYTES + 1];
    utf8util::copyTruncate(tmp, sizeof tmp, gLong.c_str());
    gLong = tmp;
    char stmp[5];
    utf8util::copyTruncate(stmp, sizeof stmp, gShort.c_str());
    gShort = stmp;
    if (dirty) saveIdentity();
}

// One line per node: "id key short<TAB>long<TAB>lastHeard". Files written
// before lastHeard existed end after the long name.
void saveNodes() {
    std::string path = gDir + "/nodes";
    std::string tmpPath = path + ".tmp";
    FILE *f = fopen(tmpPath.c_str(), "w");
    if (!f) return;
    for (auto &kv : gPeers) {
        const Peer &p = kv.second;
        fprintf(f, "%08x %s %s\t%s\t%u\n", kv.first,
                p.hasPubKey ? hex(p.pubKey, 32).c_str() : "-",
                p.shortName.empty() ? "-" : p.shortName.c_str(), p.longName.c_str(), p.lastHeard);
    }
    fclose(f);
    rename(tmpPath.c_str(), path.c_str());
}

void loadNodes() {
    FILE *f = fopen((gDir + "/nodes").c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        unsigned id;
        char key[80], sn[16];
        int off = 0;
        if (sscanf(line, "%x %79s %15[^\t]%n", &id, key, sn, &off) < 3) continue;
        Peer &p = gPeers[id];
        p.hasPubKey = unhex(key, p.pubKey, 32);
        p.shortName = strcmp(sn, "-") ? sn : "";
        if (off > 0 && line[off] == '\t') {
            std::string rest = line + off + 1;
            size_t tab = rest.find('\t');
            p.longName = rest.substr(0, tab);
            if (tab != std::string::npos) p.lastHeard = (uint32_t)strtoul(rest.c_str() + tab + 1, nullptr, 10);
        }
    }
    fclose(f);
}

// ── Transmit ────────────────────────────────────────────────────────────────

bool buildFrame(MeshHdr &hdr, const uint8_t *payload, size_t len, uint8_t *frame, size_t &frameLen) {
    if (sizeof hdr + len > MESH_MAX_FRAME_BYTES) return false;
    hdr.relay_node = (uint8_t)(gMe & 0xFF);
    memcpy(frame, &hdr, sizeof hdr);
    memcpy(frame + sizeof hdr, payload, len);
    frameLen = sizeof hdr + len;
    return true;
}

bool sendFrame(MeshHdr &hdr, const uint8_t *payload, size_t len) {
    uint8_t frame[MESH_MAX_FRAME_BYTES];
    size_t n;
    return buildFrame(hdr, payload, len, frame, n) && Radio.transmit(frame, n);
}

// Encrypts proto for channel chan and fills hdr; the caller sends or keeps it.
size_t sealOnChannel(int chan, uint32_t to, const uint8_t *proto, size_t protoLen, uint8_t extraFlags,
                     MeshHdr &hdr, uint8_t *cipher, size_t cipherMax) {
    const ChannelKey &ck = CHANNEL_KEYS[chan];
    hdr = {};
    hdr.id = nextMeshPacketId();
    hdr.to = to;
    hdr.from = gMe;
    hdr.channel = ck.hash;
    hdr.flags = meshOriginHopFlagsForChannel(chan, extraFlags);
    return encryptChannelPayload(ck, hdr.id, gMe, to, proto, protoLen, cipher, cipherMax);
}

bool sendOnChannel(int chan, uint32_t to, const uint8_t *proto, size_t protoLen, uint8_t extraFlags,
                   uint32_t *idOut = nullptr) {
    uint8_t cipher[MESH_MAX_FRAME_BYTES];
    MeshHdr hdr;
    size_t n = sealOnChannel(chan, to, proto, protoLen, extraFlags, hdr, cipher, sizeof cipher);
    if (idOut) *idOut = hdr.id;
    return n && sendFrame(hdr, cipher, n);
}

bool sendNodeInfo(uint32_t to, bool wantResponse) {
    uint8_t mac[6] = {0x02, 0x00, (uint8_t)(gMe >> 24), (uint8_t)(gMe >> 16),
                      (uint8_t)(gMe >> 8), (uint8_t)gMe};
    uint8_t proto[256];
    size_t n = encodeNodeInfo(gMe, gLong.c_str(), gShort.c_str(), mac, proto, sizeof proto,
                              wantResponse, 0, lic::active());
    bool ok = n && sendOnChannel(0, to, proto, n, 0);
    Serial.printf("[tx] nodeinfo -> %s %s\n", to == kBroadcast ? "broadcast" : peerLabel(to).c_str(),
                  ok ? "ok" : "FAILED");
    return ok;
}

bool sendAck(uint32_t to, uint32_t requestId) {
    uint8_t proofKey[32];
    auto it = gPeers.find(to);
    bool haveProof = it != gPeers.end() && it->second.hasPubKey && pkiSharedKey(it->second.pubKey, proofKey);
    uint8_t proto[64];
    size_t n = encodeRouting(requestId, gMe, 0, proto, sizeof proto, 0,
                             haveProof ? proofKey : nullptr, to);
    memset(proofKey, 0, sizeof proofKey);
    return n && sendOnChannel(0, to, proto, n, 0);
}

// Position on every channel that shares it, coarsened to the configured
// precision and never finer than a public channel allows.
void sendPosition() {
    if (!gGpsOpen || !gGps.current() || gCfg.rxOnly) return;
    const GpsFix &fix = gGps.fix();
    for (int i = 0; i < MESH_CHANNELS; i++) {
        const ChannelKey &ck = CHANNEL_KEYS[i];
        if (ck.role == 2 || !ck.shareLocation) continue;
        uint8_t bits = gCfg.positionPrecision;
        if (channelKeyIsPublic(ck) && bits > MESH_MAX_POSITION_PRECISION_PUBLIC)
            bits = MESH_MAX_POSITION_PRECISION_PUBLIC;
        if (bits == 0) continue;
        int32_t lat = fix.latI, lon = fix.lonI;
        if (bits < 32) applyPositionPrecision(lat, lon, bits);
        uint8_t proto[128];
        size_t n = encodePosition(lat, lon, fix.altM, proto, sizeof proto, 0, bits);
        bool ok = n && sendOnChannel(i, kBroadcast, proto, n, 0);
        Serial.printf("[tx] position #%s %.5f,%.5f (%u bits) %s\n", ck.name, lat / 1e7, lon / 1e7, bits,
                      ok ? "ok" : "FAILED");
    }
    gLastPositionMs = millis();
    gPositionSent = true;
}

void storeAndPublish(const Msg &m) { publish(gStore.add(m).record()); }

void setMsgState(uint32_t id, char state) {
    if (gStore.setState(gMe, id, state))
        publish(ipc::join(std::string("S"), ipc::hex32(id), std::string(1, state)));
}

// Fits text in one packet without splitting a UTF-8 sequence.
std::string clipText(const std::string &text) {
    char buf[MESH_TEXT_MAX_LEN + 1];
    utf8util::copyTruncate(buf, sizeof buf, text.c_str());
    return buf;
}

void sendChannelText(int chan, const std::string &raw) {
    std::string text = clipText(raw);
    if (chan < 0 || chan >= MESH_CHANNELS || CHANNEL_KEYS[chan].role == 2 || text.empty()) return;
    uint8_t proto[256];
    size_t n = encodeTextMessage(text.c_str(), proto, sizeof proto);
    uint32_t id = 0;
    bool ok = !gCfg.rxOnly && n && sendOnChannel(chan, kBroadcast, proto, n, 0, &id);
    if (!id) id = nextMeshPacketId();
    Serial.printf("[tx] %s #%s id=%08x: %s\n", ok ? "sent" : "FAILED", CHANNEL_KEYS[chan].name, id, text.c_str());
    Msg m;
    m.id = id;
    m.epoch = nowEpoch();
    m.conv = "c" + std::to_string(chan);
    m.from = gMe;
    m.to = kBroadcast;
    m.state = ok ? 's' : 'F';
    m.text = text;
    storeAndPublish(m);
}

// PKI when we hold the peer's key; otherwise on the primary channel, the way
// Meshtastic sends a DM to a node whose key it has not heard. Either way it
// asks for an ACK and is retried until one comes.
void sendDm(uint32_t to, const std::string &raw) {
    std::string text = clipText(raw);
    if (text.empty() || to == 0 || to == kBroadcast || to == gMe) return;
    auto it = gPeers.find(to);
    uint8_t proto[256], cipher[MESH_MAX_FRAME_BYTES];
    size_t n = encodeTextMessageUnicast(text.c_str(), gMe, to, proto, sizeof proto);
    PendingDm p;
    MeshHdr hdr = {};
    bool built = false;
    const bool pki = it != gPeers.end() && it->second.hasPubKey && !lic::active();
    if (n && pki) {
        hdr.id = nextMeshPacketId();
        hdr.to = to;
        hdr.from = gMe;
        hdr.channel = 0;
        hdr.flags = meshOriginHopFlagsForChannel(0, 1 << 3);
        built = encryptPki(hdr.id, gMe, it->second.pubKey, proto, n, cipher)
                && buildFrame(hdr, cipher, n + 12, p.frame, p.len);
    } else if (n) {
        size_t c = sealOnChannel(0, to, proto, n, 1 << 3, hdr, cipher, sizeof cipher);
        built = c && buildFrame(hdr, cipher, c, p.frame, p.len);
    }
    bool ok = built && !gCfg.rxOnly && Radio.transmit(p.frame, p.len);
    if (!hdr.id) hdr.id = nextMeshPacketId();
    Serial.printf("[tx] DM%s %s %s id=%08x: %s\n", pki ? " pki" : "", peerLabel(to).c_str(),
                  ok ? "sent" : "FAILED", hdr.id, text.c_str());
    if (ok) {
        p.id = hdr.id;
        p.tries = 1;
        p.nextMs = millis() + kDmRetryMs;
        gPendingDms.push_back(p);
    }
    Msg m;
    m.id = hdr.id;
    m.epoch = nowEpoch();
    m.conv = "d" + ipc::hex32(to);
    m.from = gMe;
    m.to = to;
    m.state = ok ? 'p' : 'F';
    m.text = text;
    storeAndPublish(m);
    if (!pki && it != gPeers.end() && ok) sendNodeInfo(to, true);   // for its key, next time
}

void serviceDmRetries(uint32_t now) {
    for (auto it = gPendingDms.begin(); it != gPendingDms.end();) {
        if ((int32_t)(now - it->nextMs) < 0) { ++it; continue; }
        if (it->tries >= kDmTries) {
            Serial.printf("[tx] DM %08x: no ACK after %d tries\n", it->id, it->tries);
            setMsgState(it->id, 'F');
            it = gPendingDms.erase(it);
            continue;
        }
        it->tries++;
        it->nextMs = now + kDmRetryMs;
        Serial.printf("[tx] DM %08x retry %d\n", it->id, it->tries - 1);
        Radio.transmit(it->frame, it->len);
        return;   // one transmission per loop, as relays
    }
}

void dmAcked(uint32_t id, bool nak) {
    for (auto it = gPendingDms.begin(); it != gPendingDms.end(); ++it)
        if (it->id == id) { gPendingDms.erase(it); break; }
    setMsgState(id, nak ? 'N' : 'A');
}

// ── Relay (managed flooding, as main_lvgl.cpp maybeRebroadcastPacket) ──────

void maybeRelay(const MeshPacket &pkt) {
    if (gCfg.rxOnly || !relay::roleRebroadcasts(gCfg.role)) return;
    const uint8_t hopLimit = pkt.hdr.flags & 7, hopStart = (pkt.hdr.flags >> 5) & 7;
    if (hopLimit == 0 || pkt.hdr.to == gMe || pkt.rawLen == 0) return;
    // Meshtastic 2.8 drops hop_start < hop_limit as invalid; so do we.
    if (hopStart < hopLimit) return;
    const uint8_t ours = (uint8_t)(gMe & 0xFF);
    if (pkt.hdr.next_hop != 0 && pkt.hdr.next_hop != ours) return;

    relay::PrevRelay prev = {};
    const bool spendHop = relay::shouldDecrementHopLimit(gCfg.role, hopStart, hopLimit, prev);
    MeshHdr hdr = pkt.hdr;
    hdr.flags = (uint8_t)((pkt.hdr.flags & 0xF8) | ((spendHop ? hopLimit - 1 : hopLimit) & 7));
    hdr.relay_node = ours;
    hdr.next_hop = 0;

    PendingRelay *slot = nullptr;
    for (auto &r : gRelay)
        if (!r.active) { slot = &r; break; }
    if (!slot || sizeof hdr + pkt.rawLen > sizeof slot->frame) return;
    const uint32_t delayMs = relay::relayDelayMsec(pkt.snr, gRelaySlotMs, esp_random());
    memcpy(slot->frame, &hdr, sizeof hdr);
    memcpy(slot->frame + sizeof hdr, pkt.rawCipher, pkt.rawLen);
    slot->len = sizeof hdr + pkt.rawLen;
    slot->sendAtMs = millis() + delayMs;
    slot->from = pkt.hdr.from;
    slot->id = pkt.hdr.id;
    slot->snr = pkt.snr;
    slot->late = false;
    slot->active = true;
    if (debugMessagesEnabled())
        Serial.printf("[fwd] queued %08x/%08x hop %u in %u ms\n", pkt.hdr.from, pkt.hdr.id, hopLimit, delayMs);
}

void relayHeardDuplicate(const MeshPacket &pkt) {
    for (auto &p : gRelay) {
        if (!p.active || p.from != pkt.hdr.from || p.id != pkt.hdr.id) continue;
        const uint8_t hopLimit = pkt.hdr.flags & 7, hopStart = (pkt.hdr.flags >> 5) & 7;
        const bool repeatedByOrigin = hopStart > 0 && hopStart == hopLimit;
        switch (relay::onDuplicateHeard(gCfg.role, false, repeatedByOrigin)) {
        case relay::DupeAction::Cancel:
            p.active = false;
            gStoodDown++;
            if (debugMessagesEnabled()) Serial.printf("[fwd] stood down %08x\n", p.id);
            break;
        case relay::DupeAction::MoveLate:
            if (!p.late) {
                p.sendAtMs = millis() + relay::lateWindowMsec(p.snr, gRelaySlotMs);
                p.late = true;
            }
            break;
        case relay::DupeAction::Keep:
            break;
        }
        return;
    }
}

// At most one transmission per loop: Radio.transmit() blocks for the airtime.
bool serviceRelays(uint32_t now) {
    for (auto &p : gRelay) {
        if (!p.active || (int32_t)(now - p.sendAtMs) < 0) continue;
        p.active = false;
        if (Radio.transmit(p.frame, p.len)) {
            gRelayed++;
            if (debugMessagesEnabled()) Serial.printf("[fwd] relayed %08x/%08x\n", p.from, p.id);
        }
        return true;
    }
    return false;
}

// ── Receive ─────────────────────────────────────────────────────────────────

bool seenBefore(uint32_t from, uint32_t id) {
    for (auto &s : gSeen)
        if (s[0] == from && s[1] == id) return true;
    gSeen[gSeenNext][0] = from;
    gSeen[gSeenNext][1] = id;
    gSeenNext = (gSeenNext + 1) % kSeen;
    return false;
}

// Routing.error_reason (field 3); absent means 0, success.
uint32_t routingError(const uint8_t *p, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint64_t tag;
        size_t j = pbReadVarint(p, len, i, tag);
        if (!j) break;
        i = j;
        uint32_t wt = tag & 7;
        if (wt == 0) {
            uint64_t v;
            j = pbReadVarint(p, len, i, v);
            if (!j) break;
            if ((tag >> 3) == 3) return (uint32_t)v;
            i = j;
        } else if (wt == 2) {
            uint64_t l;
            j = pbReadVarint(p, len, i, l);
            if (!j || l > len - j) break;
            i = j + l;
        } else if (wt == 5) i += 4;
        else if (wt == 1) i += 8;
        else break;
    }
    return 0;
}

void handlePacket(MeshPacket &pkt) {
    const MeshHdr &h = pkt.hdr;
    if (h.from == gMe) {
        // A neighbour rebroadcasting our packet: the implicit ACK a broadcast gets.
        if (h.relay_node != (uint8_t)(gMe & 0xFF) && !seenBefore(h.from, h.id)) {
            Serial.printf("[tx] %08x relayed by node ending %02x (rssi=%.0f)\n", h.id, h.relay_node, pkt.rssi);
            if (h.to == kBroadcast) setMsgState(h.id, 'R');
        }
        return;
    }
    if (seenBefore(h.from, h.id)) {
        relayHeardDuplicate(pkt);
        return;
    }
    maybeRelay(pkt);

    Peer &peer = gPeers[h.from];
    peer.rssi = pkt.rssi;
    peer.snr = pkt.snr;
    peer.hops = hopsAway(h.flags);
    peer.lastHeard = nowEpoch();

    if (pkt.decrypted && pkt.chanIdx >= 0 && CHANNEL_KEYS[pkt.chanIdx].role == 2) pkt.decrypted = false;
    if (!pkt.decrypted && h.channel == 0 && h.to == gMe && pkt.rawLen > 12 && peer.hasPubKey
        && !lic::active()) {
        uint8_t plain[256];
        size_t plainLen = sizeof plain;
        if (decryptPki(h, pkt.rawCipher, pkt.rawLen, peer.pubKey, plain, plainLen)) {
            const uint8_t *pay = nullptr;
            size_t payLen = 0;
            decodeData(plain, plainLen, pkt.portnum, pay, payLen, pkt.requestId, pkt.wantResponse,
                       &pkt.dataDest, &pkt.hasDataDest, &pkt.dataSource, &pkt.hasDataSource, &pkt.env);
            if (pay && payLen <= sizeof pkt.payload) {
                memcpy(pkt.payload, pay, payLen);
                pkt.payloadLen = payLen;
                pkt.decrypted = true;
                pkt.chanIdx = -2;
            }
        }
    }

    const std::string from = peerLabel(h.from);
    if (!pkt.decrypted) {
        if (debugMessagesEnabled())
            Serial.printf("[rx] %s -> !%08x ch=%02x (not decrypted) rssi=%.0f\n",
                          from.c_str(), h.to, h.channel, pkt.rssi);
        publish(nodeRecord(h.from, peer));
        return;
    }

    const bool toMe = h.to == gMe;
    switch (pkt.portnum) {
    case TEXT_MESSAGE_APP: {
        if (!toMe && h.to != kBroadcast) break;
        if (!toMe && (pkt.chanIdx < 0 || pkt.chanIdx >= MESH_CHANNELS)) break;
        std::string text((const char *)pkt.payload, pkt.payloadLen);
        if (toMe)
            Serial.printf("[dm%s] %s: %s\n", pkt.chanIdx == -2 ? " pki" : "", from.c_str(), text.c_str());
        else
            Serial.printf("[#%s] %s: %s\n", CHANNEL_KEYS[pkt.chanIdx].name, from.c_str(), text.c_str());
        Msg m;
        m.id = h.id;
        m.epoch = nowEpoch();
        m.conv = toMe ? "d" + ipc::hex32(h.from) : "c" + std::to_string(pkt.chanIdx);
        m.from = h.from;
        m.to = h.to;
        m.state = 'r';
        m.rssi = pkt.rssi;
        m.snr = pkt.snr;
        m.hops = peer.hops;
        m.text = text;
        storeAndPublish(m);
        break;
    }
    case NODEINFO_APP: {
        UserInfo u = {};
        if (!decodeUser(pkt.payload, pkt.payloadLen, u)) break;
        bool isNew = peer.longName.empty();
        peer.longName = u.longName;
        peer.shortName = u.shortName;
        if (u.hasPubKey) {
            peer.hasPubKey = true;
            memcpy(peer.pubKey, u.pubKey, 32);
        }
        saveNodes();
        Serial.printf("[node] %s %s \"%s\"%s\n", isNew ? "new" : "update", peerLabel(h.from).c_str(),
                      u.longName, u.hasPubKey ? " +key" : "");
        if (pkt.wantResponse && !gCfg.rxOnly && (toMe || h.to == kBroadcast)
            && (peer.lastNodeInfoReplyMs == 0 || millis() - peer.lastNodeInfoReplyMs > kNodeInfoReplyGapMs)) {
            peer.lastNodeInfoReplyMs = millis();
            sendNodeInfo(h.from, false);
        }
        break;
    }
    case ROUTING_APP:
        if (toMe && pkt.requestId) {
            uint32_t err = routingError(pkt.payload, pkt.payloadLen);
            Serial.printf("[ack] %s for %08x from %s\n", err ? "NAK" : "ACK", pkt.requestId, from.c_str());
            dmAcked(pkt.requestId, err != 0);
        }
        publish(nodeRecord(h.from, peer));
        return;   // never ACK an ACK
    case POSITION_APP: {
        PositionInfo p = {};
        if (debugMessagesEnabled() && decodePosition(pkt.payload, pkt.payloadLen, p))
            Serial.printf("[pos] %s %.5f,%.5f\n", from.c_str(), p.latI / 1e7, p.lonI / 1e7);
        break;
    }
    default:
        if (debugMessagesEnabled())
            Serial.printf("[rx] %s port=%s\n", from.c_str(), portnumName(pkt.portnum));
        break;
    }
    publish(nodeRecord(h.from, peer));

    if (toMe && (h.flags & 0x08) && !gCfg.rxOnly) {
        if (!sendAck(h.from, h.id)) Serial.printf("[tx] ack to %s FAILED\n", from.c_str());
    }
}

// ── Commands: socket clients and the console ───────────────────────────────

bool parseNodeId(const char *s, uint32_t &out) {
    if (*s == '!') s++;
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || (*end && *end != ' ')) return false;
    out = (uint32_t)v;
    return true;
}

// "c<idx>" or "d<hex>"
void sendToConv(const std::string &conv, const std::string &text) {
    if (conv.size() < 2) return;
    if (conv[0] == 'c') sendChannelText(atoi(conv.c_str() + 1), text);
    else if (conv[0] == 'd') {
        uint32_t to;
        if (parseNodeId(conv.c_str() + 1, to)) sendDm(to, text);
    }
}

void handleClientLine(const std::string &line) {
    auto f = ipc::split(line);
    if (f.empty()) return;
    if (f[0] == "T" && f.size() >= 3) sendToConv(f[1], f[2]);
    else if (f[0] == "P" && !gCfg.rxOnly) sendNodeInfo(kBroadcast, false);
}

void listNodes() {
    uint32_t now = nowEpoch();
    for (auto &kv : gPeers) {
        const Peer &p = kv.second;
        Serial.printf("  !%08x %-4s %-24s %s", kv.first, p.shortName.c_str(), p.longName.c_str(),
                      p.hasPubKey ? "key " : "    ");
        if (p.lastHeard && now)
            Serial.printf(" %5lus ago rssi=%.0f snr=%.1f\n", (unsigned long)(now - p.lastHeard), p.rssi, p.snr);
        else
            Serial.printf(" (not heard)\n");
    }
}

void handleConsoleLine(char *line) {
    line[strcspn(line, "\r\n")] = 0;
    if (!*line) return;
    if (line[0] != '/') { sendChannelText(0, line); return; }
    if (!strcmp(line, "/quit")) { gQuit = 1; return; }
    if (!strcmp(line, "/nodes")) { listNodes(); return; }
    if (!strcmp(line, "/nodeinfo")) { sendNodeInfo(kBroadcast, false); return; }
    if (!strcmp(line, "/position")) { sendPosition(); return; }
    if (!strcmp(line, "/stats")) {
        Serial.printf("relayed %u, stood down %u, %zu nodes, %zu messages, gps %s\n", gRelayed, gStoodDown,
                      gPeers.size(), gStore.all().size(), gGpsOpen && gGps.current() ? "fix" : "no fix");
        return;
    }
    if (!strcmp(line, "/debug")) {
        bool on = !debugMessagesEnabled();
        debugSetFlags(on, on, false);
        Serial.printf("debug %s\n", on ? "on" : "off");
        return;
    }
    if (!strncmp(line, "/dm ", 4)) {
        uint32_t to;
        char *text = strchr(line + 4, ' ');
        if (text && parseNodeId(line + 4, to)) { sendDm(to, text + 1); return; }
    }
    Serial.println("commands: <text> | /dm !nodeid <text> | /nodes | /nodeinfo | /position | /stats | /debug | /quit");
}

void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--config FILE] [--socket PATH] [--long NAME] [--short ABCD]\n"
            "          [--hops N] [--power DBM] [--rx-only] [--debug]\n"
            "  Camillia-MT radio daemon for the CardputerZero LoRa cap. Settings are\n"
            "  read from ~/.config/camillia/config (written on first run); options\n"
            "  given here override them. With a terminal on stdin, a line console.\n", argv0);
}

void acceptClients() {
    for (;;) {
        int fd = accept4(gListenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        gClients.push_back({fd, {}, {}});
        replayTo(gClients.back());
    }
}

void readClient(Client &c) {
    char buf[1024];
    for (;;) {
        ssize_t n = recv(c.fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            c.in.append(buf, (size_t)n);
            size_t nl;
            while ((nl = c.in.find('\n')) != std::string::npos) {
                std::string line = c.in.substr(0, nl);
                c.in.erase(0, nl + 1);
                handleClientLine(line);
            }
            if (c.in.size() > 4096) c.in.clear();
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        close(c.fd);
        c.fd = -1;
        return;
    }
}

}  // namespace

int main(int argc, char **argv) {
    const char *longArg = nullptr, *shortArg = nullptr;
    std::string configPath, socketPath = ipc::defaultSocketPath();
    int hops = -1, power = -100;
    bool debug = false, rxOnly = false;
    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) { usage(argv[0]); exit(2); }
            return argv[++i];
        };
        if (!strcmp(argv[i], "--long")) longArg = next();
        else if (!strcmp(argv[i], "--short")) shortArg = next();
        else if (!strcmp(argv[i], "--hops")) hops = atoi(next());
        else if (!strcmp(argv[i], "--power")) power = atoi(next());
        else if (!strcmp(argv[i], "--config")) configPath = next();
        else if (!strcmp(argv[i], "--socket")) socketPath = next();
        else if (!strcmp(argv[i], "--debug")) debug = true;
        else if (!strcmp(argv[i], "--rx-only")) rxOnly = true;
        else { usage(argv[0]); return 2; }
    }
    if ((hops != -1 && (hops < 0 || hops > 7)) || (power != -100 && (power < -9 || power > MESH_POWER))) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    gDir = configDir();
    gCfg = loadSettings(configPath.empty() ? gDir + "/config" : configPath);
    if (hops != -1) gCfg.hopLimit = hops;
    if (power != -100) gCfg.txPower = power;
    if (rxOnly) gCfg.rxOnly = true;
    myDeviceRole = gCfg.role;
    applyChannels(gCfg);

    loadIdentity(longArg, shortArg);
    loadNodes();
    gStore.load(gDir + "/messages");
    debugSetFlags(debug, debug, false);
    meshSetHopLimit((uint8_t)gCfg.hopLimit);
    xeddsaInitSigner(myPrivKey, gMe);

    Serial.printf("Camillia-MT %s for CardputerZero\n", APP_VERSION);
    Serial.printf("[id] !%08x \"%s\" (%s), %zu known nodes, %zu messages, config %s\n", gMe, gLong.c_str(),
                  gShort.c_str(), gPeers.size(), gStore.all().size(), gDir.c_str());

    const PresetParams &p = kPresets[gCfg.preset];
    gFreq = regionSlotFreq(gCfg.region.c_str(), p.bw, p.channelName);
    if (!Radio.init((uint8_t)gCfg.txPower) || !Radio.reconfigure(gFreq, p.bw, p.sf, p.cr, (uint8_t)gCfg.txPower)) {
        fprintf(stderr, "radio init failed\n");
        return 1;
    }
    gRelaySlotMs = relay::slotTimeMsec(p.sf, p.bw, gCfg.region == "LORA_24");
    Serial.printf("[radio] %s %s %.3f MHz SF%u BW%.1f CR4/%u, %d dBm, hop limit %d, %s\n", gCfg.region.c_str(),
                  p.channelName, (double)gFreq, p.sf, (double)p.bw, p.cr, gCfg.txPower, gCfg.hopLimit,
                  roleName(gCfg.role));
    for (int i = 0; i < MESH_CHANNELS; i++)
        if (CHANNEL_KEYS[i].role != 2)
            Serial.printf("[chan] %d %s%s\n", i, CHANNEL_KEYS[i].name,
                          channelKeyIsPublic(CHANNEL_KEYS[i]) ? " (public)" : "");

    if (gCfg.gps) {
        gGpsOpen = gGps.open(gCfg.gpsDevice, gCfg.gpsBaud);
        if (gGpsOpen) Serial.printf("[gps] reading %s at %d\n", gCfg.gpsDevice.c_str(), gCfg.gpsBaud);
    }
    if (openSocket(socketPath)) Serial.printf("[ipc] listening on %s\n", socketPath.c_str());

    Radio.setTxInhibit(gCfg.rxOnly);
    if (gCfg.rxOnly) Serial.println("[radio] receive only: nothing will be transmitted");
    else sendNodeInfo(kBroadcast, false);
    gLastNodeInfoMs = millis();

    const bool console = isatty(STDIN_FILENO);
    char line[512];
    size_t lineLen = 0;
    while (!gQuit) {
        MeshPacket pkt;
        while (Radio.pollRx(pkt)) handlePacket(pkt);

        uint32_t now = millis();
        if (!serviceRelays(now)) serviceDmRetries(now);

        if (!gCfg.rxOnly && now - gLastNodeInfoMs >= gCfg.nodeInfoIntervalS * 1000u) {
            sendNodeInfo(kBroadcast, false);
            gLastNodeInfoMs = now;
        }
        if (gGpsOpen) {
            if (gGps.service()) publish(gpsRecord());
            if (gGps.current() && gCfg.positionIntervalS
                && (!gPositionSent || now - gLastPositionMs >= gCfg.positionIntervalS * 1000u))
                sendPosition();
        }

        std::vector<pollfd> pfds;
        if (console) pfds.push_back({STDIN_FILENO, POLLIN, 0});
        if (gListenFd >= 0) pfds.push_back({gListenFd, POLLIN, 0});
        if (gGpsOpen) pfds.push_back({gGps.fd(), POLLIN, 0});
        for (auto &c : gClients) pfds.push_back({c.fd, (short)(POLLIN | (c.out.empty() ? 0 : POLLOUT)), 0});
        // The radio is interrupt-driven but not a pollable fd: wake often for it.
        if (poll(pfds.data(), pfds.size(), 10) <= 0) continue;

        for (auto &pfd : pfds) {
            if (!pfd.revents) continue;
            if (console && pfd.fd == STDIN_FILENO) {
                char c;
                ssize_t n = read(STDIN_FILENO, &c, 1);
                if (n <= 0) continue;
                if (c == '\n' || lineLen + 1 >= sizeof line) {
                    line[lineLen] = 0;
                    handleConsoleLine(line);
                    lineLen = 0;
                } else {
                    line[lineLen++] = c;
                }
            } else if (pfd.fd == gListenFd) {
                acceptClients();
            }
        }
        for (auto &c : gClients) {
            for (auto &pfd : pfds) {
                if (pfd.fd != c.fd || !pfd.revents) continue;
                if (pfd.revents & POLLOUT) flushClient(c);
                if (c.fd >= 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) readClient(c);
            }
        }
        gClients.remove_if([](const Client &c) { return c.fd < 0; });
    }
    for (auto &c : gClients) close(c.fd);
    if (gListenFd >= 0) {
        close(gListenFd);
        unlink(gSocketPath.c_str());
    }
    Serial.println("bye");
    return 0;
}
