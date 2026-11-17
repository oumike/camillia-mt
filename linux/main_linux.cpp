// SPDX-License-Identifier: GPL-3.0-or-later
// Headless Camillia-MT for the M5Stack CardputerZero (issue #155).
//
// The radio and protocol code is the firmware's own (mesh_radio, mesh_proto);
// this file stands in for the parts of main_lvgl.cpp a headless client needs:
// identity, a small node table, receive handling, ACKs and NodeInfo, with a
// line-based console on stdin instead of the UI. It does not relay.
#include "mesh_radio.h"
#include "mesh_proto.h"
#include "xeddsa_sig.h"
#include "debug_flags.h"
#include "licensed_mode.h"
#include "utf8_utils.h"

#include <Curve25519.h>
#include <esp_random.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <string>

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

uint8_t myPubKey[32];
uint8_t myPrivKey[32];
uint8_t myDeviceRole = MY_DEVICE_ROLE;

namespace {

constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
constexpr uint32_t kNodeInfoIntervalMs = MY_NODEINFO_INTV * 1000u;
constexpr uint32_t kNodeInfoReplyGapMs = 60u * 1000u;

struct Peer {
    std::string longName, shortName;
    uint8_t pubKey[32] = {};
    bool hasPubKey = false;
    float rssi = 0, snr = 0;
    uint32_t lastHeardMs = 0;
    uint32_t lastNodeInfoReplyMs = 0;
};

volatile sig_atomic_t gQuit = 0;
uint32_t gMe = 0;
std::string gLong, gShort, gDir;
std::map<uint32_t, Peer> gPeers;
uint32_t gSeen[64][2];
unsigned gSeenNext = 0;
uint32_t gLastNodeInfoMs = 0;

void onSignal(int) { gQuit = 1; }

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
    umask(077);
    FILE *f = fopen(path.c_str(), "w");
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

void saveNodes() {
    std::string path = gDir + "/nodes";
    std::string tmpPath = path + ".tmp";
    FILE *f = fopen(tmpPath.c_str(), "w");
    if (!f) return;
    for (auto &kv : gPeers) {
        const Peer &p = kv.second;
        fprintf(f, "%08x %s %s\t%s\n", kv.first,
                p.hasPubKey ? hex(p.pubKey, 32).c_str() : "-",
                p.shortName.empty() ? "-" : p.shortName.c_str(), p.longName.c_str());
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
        if (off > 0 && line[off] == '\t') p.longName = line + off + 1;
    }
    fclose(f);
}

// ── Transmit ────────────────────────────────────────────────────────────────

bool sendFrame(MeshHdr &hdr, const uint8_t *payload, size_t len) {
    uint8_t frame[MESH_MAX_FRAME_BYTES];
    if (sizeof hdr + len > sizeof frame) return false;
    hdr.relay_node = (uint8_t)(gMe & 0xFF);
    memcpy(frame, &hdr, sizeof hdr);
    memcpy(frame + sizeof hdr, payload, len);
    return Radio.transmit(frame, sizeof hdr + len);
}

bool sendOnChannel(uint32_t to, const uint8_t *proto, size_t protoLen, uint8_t extraFlags,
                   uint32_t *idOut = nullptr) {
    const ChannelKey &ck = CHANNEL_KEYS[0];
    uint8_t cipher[MESH_MAX_FRAME_BYTES];
    MeshHdr hdr = {};
    hdr.id = nextMeshPacketId();
    hdr.to = to;
    hdr.from = gMe;
    hdr.channel = ck.hash;
    hdr.flags = meshOriginHopFlagsForChannel(0, extraFlags);
    size_t n = encryptChannelPayload(ck, hdr.id, gMe, to, proto, protoLen, cipher, sizeof cipher);
    if (idOut) *idOut = hdr.id;
    return n && sendFrame(hdr, cipher, n);
}

bool sendNodeInfo(uint32_t to, bool wantResponse) {
    uint8_t mac[6] = {0x02, 0x00, (uint8_t)(gMe >> 24), (uint8_t)(gMe >> 16),
                      (uint8_t)(gMe >> 8), (uint8_t)gMe};
    uint8_t proto[256];
    size_t n = encodeNodeInfo(gMe, gLong.c_str(), gShort.c_str(), mac, proto, sizeof proto,
                              wantResponse, 0, lic::active());
    bool ok = n && sendOnChannel(to, proto, n, 0);
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
    return n && sendOnChannel(to, proto, n, 0);
}

void sendBroadcastText(const char *text) {
    uint8_t proto[256];
    size_t n = encodeTextMessage(text, proto, sizeof proto);
    uint32_t id = 0;
    bool ok = n && sendOnChannel(kBroadcast, proto, n, 0, &id);
    Serial.printf("[tx] %s #%s id=%08x: %s\n", ok ? "sent" : "FAILED", CHANNEL_KEYS[0].name, id, text);
}

void sendDm(uint32_t to, const char *text) {
    auto it = gPeers.find(to);
    if (it == gPeers.end() || !it->second.hasPubKey) {
        Serial.printf("[tx] no public key for %s yet; asking for its NodeInfo\n", peerLabel(to).c_str());
        sendNodeInfo(to, true);
        return;
    }
    uint8_t proto[256], cipher[256 + 12];
    size_t n = encodeTextMessageUnicast(text, gMe, to, proto, sizeof proto);
    MeshHdr hdr = {};
    hdr.id = nextMeshPacketId();
    hdr.to = to;
    hdr.from = gMe;
    hdr.channel = 0;   // PKI
    hdr.flags = meshOriginHopFlagsForChannel(0, 1 << 3);
    bool ok = n && encryptPki(hdr.id, gMe, it->second.pubKey, proto, n, cipher)
              && sendFrame(hdr, cipher, n + 12);
    Serial.printf("[tx] DM %s %s id=%08x: %s\n", peerLabel(to).c_str(), ok ? "sent" : "FAILED", hdr.id, text);
}

// ── Receive ─────────────────────────────────────────────────────────────────

bool seenBefore(uint32_t from, uint32_t id) {
    for (auto &s : gSeen)
        if (s[0] == from && s[1] == id) return true;
    gSeen[gSeenNext][0] = from;
    gSeen[gSeenNext][1] = id;
    gSeenNext = (gSeenNext + 1) % 64;
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
        if (!seenBefore(h.from, h.id))
            Serial.printf("[tx] %08x relayed by node ending %02x (rssi=%.0f)\n", h.id, h.relay_node, pkt.rssi);
        return;
    }
    if (seenBefore(h.from, h.id)) return;
    Peer &peer = gPeers[h.from];
    peer.rssi = pkt.rssi;
    peer.snr = pkt.snr;
    peer.lastHeardMs = millis();

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

    const uint8_t hopLimit = h.flags & 7, hopStart = h.flags >> 5;
    const std::string from = peerLabel(h.from);
    if (!pkt.decrypted) {
        if (debugMessagesEnabled())
            Serial.printf("[rx] %s -> !%08x ch=%02x (not decrypted) rssi=%.0f\n",
                          from.c_str(), h.to, h.channel, pkt.rssi);
        return;
    }

    const bool toMe = h.to == gMe;
    switch (pkt.portnum) {
    case TEXT_MESSAGE_APP: {
        std::string text((const char *)pkt.payload, pkt.payloadLen);
        if (toMe)
            Serial.printf("[dm%s] %s: %s\n", pkt.chanIdx == -2 ? " pki" : "", from.c_str(), text.c_str());
        else if (h.to == kBroadcast)
            Serial.printf("[#%s] %s: %s\n", CHANNEL_KEYS[pkt.chanIdx].name, from.c_str(), text.c_str());
        Serial.printf("      rssi=%.0f snr=%.1f hops=%u\n", pkt.rssi, pkt.snr,
                      (unsigned)(hopStart >= hopLimit ? hopStart - hopLimit : 0));
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
        if (pkt.wantResponse && (toMe || h.to == kBroadcast)
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
        }
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

    if (toMe && (h.flags & 0x08)) {
        if (!sendAck(h.from, h.id)) Serial.printf("[tx] ack to %s FAILED\n", from.c_str());
    }
}

// ── Console ─────────────────────────────────────────────────────────────────

bool parseNodeId(const char *s, uint32_t &out) {
    if (*s == '!') s++;
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || (*end && *end != ' ')) return false;
    out = (uint32_t)v;
    return true;
}

void listNodes() {
    uint32_t now = millis();
    for (auto &kv : gPeers) {
        const Peer &p = kv.second;
        Serial.printf("  !%08x %-4s %-24s %s", kv.first, p.shortName.c_str(), p.longName.c_str(),
                      p.hasPubKey ? "key " : "    ");
        if (p.lastHeardMs)
            Serial.printf(" %3lus ago rssi=%.0f snr=%.1f\n", (unsigned long)((now - p.lastHeardMs) / 1000),
                          p.rssi, p.snr);
        else
            Serial.printf(" (not heard this session)\n");
    }
}

void handleLine(char *line) {
    line[strcspn(line, "\r\n")] = 0;
    if (!*line) return;
    if (line[0] != '/') { sendBroadcastText(line); return; }
    if (!strcmp(line, "/quit")) { gQuit = 1; return; }
    if (!strcmp(line, "/nodes")) { listNodes(); return; }
    if (!strcmp(line, "/nodeinfo")) { sendNodeInfo(kBroadcast, false); return; }
    if (!strcmp(line, "/debug")) {
        bool on = !debugMessagesEnabled();
        debugSetFlags(on, on, false);
        Serial.printf("debug %s\n", on ? "on" : "off");
        return;
    }
    if (!strncmp(line, "/dm ", 4)) {
        uint32_t to;
        char *text = strchr(line + 4, ' ');
        if (text && parseNodeId(line + 4, to) && to && to != kBroadcast) {
            sendDm(to, text + 1);
            return;
        }
    }
    Serial.println("commands: <text> | /dm !nodeid <text> | /nodes | /nodeinfo | /debug | /quit");
}

void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--long NAME] [--short ABCD] [--hops N] [--power DBM] [--rx-only] [--debug]\n"
            "  Headless Camillia-MT on the CardputerZero LoRa cap (US LongFast).\n"
            "  Type a line to broadcast it; /help for commands.\n", argv0);
}

}  // namespace

int main(int argc, char **argv) {
    const char *longArg = nullptr, *shortArg = nullptr;
    int hops = 3, power = MESH_POWER;
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
        else if (!strcmp(argv[i], "--debug")) debug = true;
        else if (!strcmp(argv[i], "--rx-only")) rxOnly = true;
        else { usage(argv[0]); return 2; }
    }
    if (hops < 0 || hops > 7 || power < -9 || power > MESH_POWER) { usage(argv[0]); return 2; }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    gDir = configDir();
    loadIdentity(longArg, shortArg);
    loadNodes();
    debugSetFlags(debug, debug, false);
    meshSetHopLimit((uint8_t)hops);
    xeddsaInitSigner(myPrivKey, gMe);

    Serial.printf("Camillia-MT %s for CardputerZero\n", APP_VERSION);
    Serial.printf("[id] !%08x \"%s\" (%s), %zu known nodes, config %s\n", gMe, gLong.c_str(),
                  gShort.c_str(), gPeers.size(), gDir.c_str());

    if (!Radio.init((uint8_t)power)) {
        fprintf(stderr, "radio init failed\n");
        return 1;
    }
    Serial.printf("[radio] %s %.3f MHz SF%d BW%.0f, %d dBm, hop limit %d\n", CHANNEL_KEYS[0].name,
                  (double)MESH_FREQ, MESH_SF, (double)MESH_BW, power, hops);

    Radio.setTxInhibit(rxOnly);
    if (rxOnly) Serial.println("[radio] receive only: nothing will be transmitted");
    else sendNodeInfo(kBroadcast, false);
    gLastNodeInfoMs = millis();

    char line[512];
    size_t lineLen = 0;
    while (!gQuit) {
        MeshPacket pkt;
        while (Radio.pollRx(pkt)) handlePacket(pkt);

        if (millis() - gLastNodeInfoMs >= kNodeInfoIntervalMs) {
            sendNodeInfo(kBroadcast, false);
            gLastNodeInfoMs = millis();
        }

        pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        if (poll(&pfd, 1, 20) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            char c;
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if (n <= 0) {
                // stdin closed (e.g. run under systemd): keep receiving.
                if (n == 0 || errno != EINTR) close(STDIN_FILENO);
                continue;
            }
            if (c == '\n' || lineLen + 1 >= sizeof line) {
                line[lineLen] = 0;
                handleLine(line);
                lineLen = 0;
            } else {
                line[lineLen++] = c;
            }
        }
    }
    Serial.println("bye");
    return 0;
}
