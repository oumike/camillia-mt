// Host tests for packet codecs, channel policy and bounded malformed inputs.
//
// Each case runs in a forked child under an alarm, so a decoder that loops
// forever or faults is reported as a failure instead of hanging the run.
// Crypto primitives are link-time stand-ins, not cryptographic coverage.
// test-deps: ack_proof.cpp sha256_portable.cpp mesh_beacon.cpp licensed_mode.cpp
// test-cflags: -Ihost -DDEVICE_TDECK -Wno-missing-field-initializers
#include "../src/licensed_mode.h"
#include "../src/mesh_beacon.h"
#include "../src/mesh_proto.h"
#include "../src/xeddsa_sig.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cmath>
#include <vector>

uint8_t myPubKey[32];
uint8_t myPrivKey[32];
uint8_t myDeviceRole;
void debugLogMessages(const char *, ...) {}
// A signer that always signs, with a fixed stand-in signature, so the signing
// gate in encryptChannelPayload() can be seen from its output.
static const uint32_t kSignerNode = 0x51600001;
uint32_t xeddsaSignerNodeId() { return kSignerNode; }
bool xeddsaSign(const XeddsaSignedFields &, const uint8_t *, size_t, uint8_t *sig) {
    memset(sig, 0xAB, XEDDSA_SIGNATURE_BYTES);
    return true;
}

static int g_fail = 0, g_run = 0;

// Runs fn in a child; passes only if it returns true within two seconds.
template <typename F>
static void guarded(const char *what, F fn) {
    g_run++;
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        g_fail++;
        return;
    }
    if (pid == 0) {
        alarm(2);
        _exit(fn() ? 0 : 1);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) {
        perror("waitpid");
        g_fail++;
        return;
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return;
    g_fail++;
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM) printf("  FAIL  %s (hung)\n", what);
    else if (WIFSIGNALED(st)) printf("  FAIL  %s (signal %d)\n", what, WTERMSIG(st));
    else printf("  FAIL  %s\n", what);
}

// decodeData, true when the payload slice it returns lies inside buf[0..len).
static bool dataSliceInBounds(const uint8_t *buf, size_t len, const uint8_t *&pay, uint32_t &port) {
    size_t payLen = 0; uint32_t req = 0; bool want = false;
    decodeData(buf, len, port, pay, payLen, req, want);
    return !pay || (pay >= buf && payLen <= (size_t)(buf + len - pay));
}

// A length of 2^64 - 11 after an 11-byte tag+varint puts the cursor back at 0.
#define WRAP_LEN 0xF5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01

struct DataView {
    uint32_t port, request, dest, source;
    const uint8_t *payload;
    size_t len;
    bool wantResponse, hasDest, hasSource;
    DataEnvelope env;
};

static DataView readData(const uint8_t *buf, size_t len) {
    DataView d{};
    decodeData(buf, len, d.port, d.payload, d.len, d.request, d.wantResponse,
               &d.dest, &d.hasDest, &d.source, &d.hasSource, &d.env);
    return d;
}

static void testGoldenEncodings() {
    guarded("text: byte-exact reaction, reply and zero bitfield", [] {
        const uint8_t want[] = {0x08, 0x01, 0x12, 0x02, 'h', 'i',
                                0x3D, 0x44, 0x33, 0x22, 0x11,
                                0x45, 0x4D, 0xF4, 0x01, 0x00, 0x48, 0x00};
        uint8_t buf[64];
        const size_t n = encodeTextMessage("hi", buf, sizeof(buf), 0, 0x11223344, 0x1F44D);
        const auto d = readData(buf, n);
        return n == sizeof(want) && !memcmp(buf, want, n)
            && d.port == TEXT_MESSAGE_APP && d.len == 2 && !memcmp(d.payload, "hi", 2)
            && d.env.replyId == 0x11223344 && d.env.emoji == 0x1F44D
            && d.env.hasBitfield && d.env.bitfield == 0;
    });
    guarded("position request: zero-hop compatibility bitfield is present", [] {
        const uint8_t want[] = {0x08, 0x03, 0x12, 0x00, 0x18, 0x01, 0x48, 0x00};
        uint8_t buf[32];
        const size_t n = encodePositionRequest(buf, sizeof(buf));
        return n == sizeof(want) && !memcmp(buf, want, n);
    });
    guarded("routing ACK: fixed32 source/request and varint error reason", [] {
        const uint8_t want[] = {0x08, 0x05, 0x12, 0x02, 0x18, 0x00,
                                0x2D, 0x44, 0x33, 0x22, 0x11,
                                0x35, 0xEF, 0xBE, 0xAD, 0xDE, 0x48, 0x00};
        uint8_t buf[64];
        const size_t n = encodeRouting(0xDEADBEEF, 0x11223344, 0, buf, sizeof(buf));
        const auto d = readData(buf, n);
        return n == sizeof(want) && !memcmp(buf, want, n)
            && d.source == 0x11223344 && d.hasSource && d.request == 0xDEADBEEF
            && d.port == ROUTING_APP && !d.wantResponse;
    });
    guarded("DM and admin envelopes retain destination and sender", [] {
        uint8_t buf[128];
        size_t n = encodeTextMessageUnicast("DM", 0x11223344, 0x55667788, buf, sizeof(buf));
        auto d = readData(buf, n);
        if (!n || !d.hasDest || !d.hasSource || d.dest != 0x55667788 || d.source != 0x11223344
            || d.len != 2 || memcmp(d.payload, "DM", 2) || !d.env.hasBitfield) return false;
        const uint8_t admin[] = {0x28, 0x01};
        for (bool want : {false, true}) {
            n = encodeAdminData(admin, sizeof(admin), 0x11223344, 0x55667788, want, buf, sizeof(buf));
            d = readData(buf, n);
            if (!n || d.port != ADMIN_APP || d.dest != 0x55667788 || d.source != 0x11223344
                || d.wantResponse != want || d.len != sizeof(admin)
                || memcmp(d.payload, admin, sizeof(admin)) || !d.env.hasBitfield) return false;
        }
        return true;
    });
    guarded("text: maximum message accepted, overlong message refused", [] {
        char text[MESH_TEXT_MAX_LEN + 2];
        memset(text, 'x', sizeof(text));
        text[MESH_TEXT_MAX_LEN] = '\0';
        uint8_t buf[512];
        const size_t n = encodeTextMessage(text, buf, sizeof(buf), 1);
        const auto d = readData(buf, n);
        if (!n || d.len != MESH_TEXT_MAX_LEN || memcmp(d.payload, text, d.len)
            || !d.env.hasBitfield || d.env.bitfield != 1) return false;
        text[MESH_TEXT_MAX_LEN] = 'x'; text[MESH_TEXT_MAX_LEN + 1] = '\0';
        return encodeTextMessage(text, buf, sizeof(buf)) == 0;
    });
}

static void testDecoderFieldsAndBoundaries() {
    guarded("Data: unknown fields skipped and legacy varint addresses decoded", [] {
        const uint8_t wire[] = {
            0xA1, 0x01, 1, 2, 3, 4, 5, 6, 7, 8, // unknown fixed64
            0xAA, 0x01, 2, 0xAB, 0xCD,           // unknown bytes
            0x08, 1, 0x12, 1, 'x', 0x20, 7, 0x28, 8, 0x30, 9,
        };
        const auto d = readData(wire, sizeof(wire));
        return d.port == 1 && d.len == 1 && d.payload[0] == 'x'
            && d.hasDest && d.dest == 7 && d.hasSource && d.source == 8 && d.request == 9;
    });
    guarded("Data: signature lengths distinguish absent, malformed and complete", [] {
        for (uint8_t size : {0, 1, 63, 64, 65}) {
            std::vector<uint8_t> wire = {0x08, 1, 0x12, 0, 0x52, size};
            wire.insert(wire.end(), size, 0x42);
            const auto d = readData(wire.data(), wire.size());
            if (d.env.hasSignature != (size == 64)
                || d.env.badSignature != (size != 0 && size != 64)) return false;
            if (size == 64) {
                for (uint8_t b : d.env.signature) if (b != 0x42) return false;
            }
        }
        return true;
    });
    guarded("User: name at exact end of non-terminated packet is in bounds", [] {
        const uint8_t shortWire[] = {0x1A, 4, 'N', 'O', 'D', 'E'};
        const uint8_t longWire[] = {0x12, 3, 'a', 0xC3, 0xA9};
        UserInfo user{};
        decodeUser(shortWire, sizeof(shortWire), user);
        if (strcmp(user.shortName, "NODE")) return false;
        decodeUser(longWire, sizeof(longWire), user);
        return !strcmp(user.longName, "a\xC3\xA9") && user.shortName[0] == '\0';
    });
    guarded("User: truncation does not split UTF-8 or consume the next field", [] {
        std::vector<uint8_t> wire = {0x12, 41};
        wire.insert(wire.end(), 38, 'a');
        wire.insert(wire.end(), {0xE2, 0x82, 0xAC, 0x1A, 5, 'a', 'b', 'c', 0xC3, 0xA9});
        UserInfo user{};
        decodeUser(wire.data(), wire.size(), user);
        return strlen(user.longName) == 38 && !strcmp(user.shortName, "abc");
    });
    guarded("Position: fixed32 coordinates and legacy zigzag remain compatible", [] {
        const uint8_t current[] = {0x0D, 0xFF, 0xFF, 0xFF, 0xFF,
                                   0x15, 2, 0, 0, 0, 0x18, 42};
        const uint8_t legacy[] = {0x08, 1, 0x10, 4, 0x18, 42};
        PositionInfo a{}, b{};
        decodePosition(current, sizeof(current), a);
        decodePosition(legacy, sizeof(legacy), b);
        return a.latI == -1 && a.lonI == 2 && a.alt == 42
            && b.latI == -1 && b.lonI == 2 && b.alt == 42;
    });
    guarded("MeshBeacon: complete offer decodes without altering channel settings", [] {
        const uint8_t wire[] = {0x0A, 2, 'h', 'i', 0x12, 10,
                                0x12, 1, 1, 0x1A, 3, 'N', 'e', 't', 0x40, 1,
                                0x18, 3, 0x20, 1};
        MeshBeaconPayload offer{};
        const ChannelKey before = CHANNEL_KEYS[0];
        const ChannelKey &after = CHANNEL_KEYS[0];
        return decodeMeshBeacon(wire, sizeof(wire), offer) && !strcmp(offer.message, "hi")
            && offer.hasOfferChannel && !strcmp(offer.offerChannelName, "Net")
            && offer.offerPskLen == 1 && offer.offerPsk[0] == 1 && offer.offerUsesAead
            && offer.offerRegion == 3 && offer.hasOfferPreset && offer.offerPreset == 1
            && before.name == after.name && !memcmp(before.name_buf, after.name_buf, sizeof(before.name_buf))
            && before.keyLen == after.keyLen && !memcmp(before.key, after.key, sizeof(before.key))
            && before.hash == after.hash && before.useAead == after.useAead && before.role == after.role
            && before.hopLimitPlus1 == after.hopLimitPlus1;
    });
}

static void testApplicationRoundTrips() {
    guarded("MeshBeacon: encoded offer with slot decodes", [] {
        MeshBeaconTx tx{};
        strcpy(tx.offerChannelName, "LongFast");
        tx.offerRegion = 1;
        tx.hasOfferPreset = true;
        tx.hasOfferSlot = true;
        tx.offerSlot = 20;
        uint8_t buf[64];
        MeshBeaconPayload b{};
        const size_t n = encodeMeshBeacon(tx, buf, sizeof(buf));
        return n && decodeMeshBeacon(buf, n, b) && b.hasOfferChannel
            && !strcmp(b.offerChannelName, "LongFast") && b.offerRegion == 1
            && b.hasOfferPreset && b.offerPreset == 0 && b.hasOfferSlot && b.offerSlot == 20;
    });
    guarded("NodeInfo: names, role, key, and response policy survive", [] {
        uint8_t buf[256], mac[6] = {1, 2, 3, 4, 5, 6};
        memset(myPubKey, 0x42, sizeof(myPubKey));
        myDeviceRole = 12;
        const size_t n = encodeNodeInfo(0x12345678, "Test node", "TEST", mac, buf, sizeof(buf));
        const auto d = readData(buf, n);
        UserInfo user{};
        decodeUser(d.payload, d.len, user);
        return n && d.port == NODEINFO_APP && d.wantResponse && d.env.hasBitfield
            && !strcmp(user.longName, "Test node") && !strcmp(user.shortName, "TEST")
            && user.hasPubKey && !memcmp(user.pubKey, myPubKey, 32) && user.role == 12;
    });
    guarded("NodeInfo: is_licensed round-trips and keeps the public key", [] {
        uint8_t buf[256], mac[6] = {1, 2, 3, 4, 5, 6};
        memset(myPubKey, 0x42, sizeof(myPubKey));
        myDeviceRole = 0;
        const size_t on = encodeNodeInfo(0x12345678, "K1ABC", "K1AB", mac, buf, sizeof(buf),
                                         true, 0, true);
        auto d = readData(buf, on);
        UserInfo a{};
        decodeUser(d.payload, d.len, a);
        const size_t off = encodeNodeInfo(0x12345678, "K1ABC", "K1AB", mac, buf, sizeof(buf),
                                          true, 0, false);
        d = readData(buf, off);
        UserInfo b{};
        b.isLicensed = true;
        decodeUser(d.payload, d.len, b);
        return on && off && a.isLicensed && a.hasPubKey && !memcmp(a.pubKey, myPubKey, 32)
            && !b.isLicensed && b.hasPubKey;
    });
    guarded("signing: unicast text unsigned while not licensed", [] {
        lic::setActive(false);
        uint8_t plain[128], out[256];
        const size_t n = encodeTextMessageUnicast("hi", kSignerNode, 0x22222222, plain, sizeof(plain));
        ChannelKey ck{};
        const size_t m = encryptChannelPayload(ck, 1, kSignerNode, 0x22222222, plain, n, out, sizeof(out));
        return n && m == n && !readData(out, m).env.hasSignature;
    });
    guarded("signing: unicast text signed while licensed", [] {
        lic::setActive(true);
        uint8_t plain[128], out[256];
        const size_t n = encodeTextMessageUnicast("hi", kSignerNode, 0x22222222, plain, sizeof(plain));
        ChannelKey ck{};
        const size_t m = encryptChannelPayload(ck, 1, kSignerNode, 0x22222222, plain, n, out, sizeof(out));
        return n && m == n + XEDDSA_SIGNATURE_FIELD_BYTES && readData(out, m).env.hasSignature;
    });
    guarded("signing: unicast routing stays unsigned while licensed", [] {
        lic::setActive(true);
        uint8_t plain[128], out[256];
        const size_t n = encodeRouting(0x1234, kSignerNode, 0, plain, sizeof(plain));
        ChannelKey ck{};
        const size_t m = encryptChannelPayload(ck, 1, kSignerNode, 0x22222222, plain, n, out, sizeof(out));
        return n && m == n && !readData(out, m).env.hasSignature;
    });
    guarded("signing: broadcast text signed while not licensed", [] {
        lic::setActive(false);
        uint8_t plain[128], out[256];
        const size_t n = encodeTextMessage("hi", plain, sizeof(plain));
        ChannelKey ck{};
        const size_t m = encryptChannelPayload(ck, 1, kSignerNode, 0xFFFFFFFF, plain, n, out, sizeof(out));
        return n && m == n + XEDDSA_SIGNATURE_FIELD_BYTES && readData(out, m).env.hasSignature;
    });
    guarded("shared NodeInfo: does not impersonate our role or public key", [] {
        uint8_t buf[256];
        memset(myPubKey, 0x42, sizeof(myPubKey));
        myDeviceRole = 12;
        const size_t n = encodeSharedNodeInfo(0x12345678, "Peer", "PEER", nullptr, buf, sizeof(buf));
        const auto d = readData(buf, n);
        UserInfo user{};
        decodeUser(d.payload, d.len, user);
        return n && !d.wantResponse && d.env.hasBitfield && d.env.bitfield == 0
            && !strcmp(user.longName, "Peer") && !user.hasPubKey && user.role == 0;
    });
    guarded("Position: signed coordinates and below-sea-level altitude survive", [] {
        uint8_t buf[128];
        const size_t n = encodePosition(-123456789, 987654321, -42, buf, sizeof(buf), 1, 15);
        const auto d = readData(buf, n);
        PositionInfo pos{};
        decodePosition(d.payload, d.len, pos);
        return n && d.port == POSITION_APP && d.env.bitfield == 1 && d.env.hasBitfield
            && pos.latI == -123456789 && pos.lonI == 987654321 && pos.alt == -42;
    });
    guarded("Telemetry: device/environment metrics stay distinct", [] {
        uint8_t buf[128];
        size_t n = encodeTelemetryDevice(75, 4.0f, 12.5f, 2.25f, 123, 1700000000, buf, sizeof(buf));
        auto d = readData(buf, n);
        TelemetryInfo t{};
        decodeTelemetry(d.payload, d.len, t);
        if (!n || d.port != TELEMETRY_APP || !d.env.hasBitfield || !t.valid
            || !t.hasDeviceMetrics || t.hasEnvironmentMetrics || t.battPct != 75
            || t.voltage != 4.0f || t.chUtil != 12.5f || t.airUtil != 2.25f) return false;
        n = encodeTelemetryEnvironment(-12.5f, 50.25f, 1013.25f, 0, buf, sizeof(buf), 1);
        d = readData(buf, n);
        decodeTelemetry(d.payload, d.len, t);
        return n && d.env.bitfield == 1 && t.valid && !t.hasDeviceMetrics
            && t.hasEnvironmentMetrics && t.temperatureC == -12.5f
            && t.humidityPct == 50.25f && t.pressureHpa == 1013.25f;
    });
    guarded("NeighborInfo: zero IDs skipped and local-only metadata not transmitted", [] {
        const NeighborEdgeInfo edges[] = {{7, -3.25f, 123, 456}, {0, 1.0f, 0, 0}, {8, 2.5f, 9, 10}};
        uint8_t buf[256];
        const size_t n = encodeNeighborInfo(42, 900, edges, 3, buf, sizeof(buf));
        const auto d = readData(buf, n);
        NeighborInfoPayload info{};
        decodeNeighborInfo(d.payload, d.len, info);
        return n && d.port == NEIGHBORINFO_APP && info.nodeId == 42 && info.lastSentById == 42
            && info.nodeBroadcastIntervalS == 900 && info.neighborCount == 2
            && info.neighbors[0].nodeId == 7 && info.neighbors[0].snr == -3.25f
            && info.neighbors[1].nodeId == 8 && info.neighbors[1].snr == 2.5f
            && info.neighbors[0].lastRxTime == 0 && info.neighbors[0].nodeBroadcastIntervalS == 0;
    });
    guarded("traceroute: negative SNR fits the ten-byte protobuf int32 representation", [] {
        uint8_t buf[128];
        const size_t n = encodeTracerouteReply(buf, sizeof(buf), nullptr, 0, 7, 8, -3.25f);
        const auto d = readData(buf, n);
        const uint8_t want[] = {0x10, 0xF3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
        return n && d.port == TRACEROUTE_APP && d.source == 8 && d.request == 7
            && d.env.hasBitfield && d.len == sizeof(want) && !memcmp(d.payload, want, sizeof(want));
    });
    guarded("traceroute: missing SNR echoes the route without inventing a measurement", [] {
        const uint8_t route[] = {0x0A, 4, 0x44, 0x33, 0x22, 0x11};
        uint8_t buf[128];
        const size_t n = encodeTracerouteReply(buf, sizeof(buf), route, sizeof(route), 7, 8, NAN);
        const auto d = readData(buf, n);
        return n && !d.wantResponse && d.len == sizeof(route) && !memcmp(d.payload, route, sizeof(route));
    });
}

static void testChannelPolicyAndPrecision() {
    guarded("channel keys: default PSK expansion, public-key policy and AEAD hash", [] {
        const uint8_t defaultKey[] = {0xD4, 0xF1, 0xBB, 0x3A, 0x20, 0x29, 0x07, 0x59,
                                      0xF0, 0xBC, 0xFF, 0xAB, 0xCF, 0x4E, 0x69, 0x01};
        uint8_t expanded[16];
        expandPsk(1, expanded);
        if (memcmp(expanded, defaultKey, sizeof(expanded))) return false;
        ChannelKey ck{};
        ck.name = "LongFast"; ck.keyLen = 1; ck.key[0] = 1;
        if (!channelKeyIsPublic(ck) || channelKeyHash(ck) != 0x08) return false;
        ck.useAead = true;
        if (!channelUsesAead(ck) || channelKeyHash(ck) != 0xA6) return false;
        ck.keyLen = 16; memcpy(ck.key, defaultKey, 16); ck.key[15] = 255;
        if (!channelKeyIsPublic(ck)) return false;
        ck.key[0] ^= 1;
        if (channelKeyIsPublic(ck)) return false;
        ck.keyLen = 32;
        if (channelKeyIsPublic(ck)) return false;
        strcpy(ck.name_buf, "Open"); ck.keyLen = 0;
        if (!channelKeyIsPublic(ck) || channelUsesAead(ck) || channelKeyHash(ck) != 0x34) return false;
        ck.keyLen = 1; ck.key[0] = 0;
        return channelKeyIsPublic(ck) && !channelUsesAead(ck) && channelKeyHash(ck) == 0x34;
    });
    guarded("hop policy: defaults, explicit zero, overrides and capped responses", [] {
        meshSetHopLimit(3);
        CHANNEL_KEYS[0].hopLimitPlus1 = 0;
        if (meshOriginHopFlags(0x08) != 0x6B || meshOriginHopFlagsForChannel(0, 0x08) != 0x6B
            || meshOriginHopFlagsForChannel(-1, 0x08) != 0x6B) return false;
        CHANNEL_KEYS[0].hopLimitPlus1 = chanHopLimitMake(0);
        if (meshOriginHopFlagsForChannel(0, 0x08) != 0x08) return false;
        CHANNEL_KEYS[0].hopLimitPlus1 = chanHopLimitMake(7);
        return meshOriginHopFlagsForChannel(0, 0x08) == 0xEF
            && meshOriginHopFlagsCapped(7, 0x08) == 0x6B
            && meshOriginHopFlagsCapped(1, 0x08) == 0x29;
    });
    guarded("position privacy: centre positive/negative cells without losing signs", [] {
        int32_t lat = 12345, lon = -12345;
        applyPositionPrecision(lat, lon, 16);
        if (lat != 32768 || lon != -32768) return false;
        lat = INT32_MAX; lon = INT32_MIN;
        applyPositionPrecision(lat, lon, 1);
        if (lat != 1073741824 || lon != -1073741824) return false;
        for (uint8_t precision : {0, 32, 255}) {
            lat = 12345; lon = -12345;
            applyPositionPrecision(lat, lon, precision);
            if (lat != 12345 || lon != -12345) return false;
        }
        return true;
    });
    guarded("CRC32: IEEE check vector and zero-length input", [] {
        return meshCrc32("123456789", 9) == 0xCBF43926 && meshCrc32("", 0) == 0;
    });
}

static void testMqttEnvelopes() {
    guarded("MQTT: encrypted envelope retains RF header and forces via_mqtt", [] {
        const MeshHdr sent{UINT32_MAX, 0x11223344, 0x55667788, 0xAB, 0x42, 0x56, 0x78};
        const uint8_t cipher[] = {0, 1, 0xFF, 2, 0x80};
        uint8_t envelope[400];
        const size_t n = encodeServiceEnvelope(sent, cipher, sizeof(cipher), -3.25f, -95,
                                               1700000000, "LongFast", "!11223344",
                                               envelope, sizeof(envelope));
        MeshHdr received{};
        uint8_t payload[16]; size_t size = 0; bool plain = true; char channel[16];
        if (!n || !decodeServiceEnvelope(envelope, n, received, payload, sizeof(payload),
                                        size, plain, channel, sizeof(channel))) return false;
        return !plain && size == sizeof(cipher) && !memcmp(payload, cipher, size)
            && !strcmp(channel, "LongFast") && received.to == sent.to && received.from == sent.from
            && received.id == sent.id && received.flags == 0xBB && received.channel == 0x42
            && received.next_hop == 0x56 && received.relay_node == 0x78;
    });
    guarded("MQTT: encrypted oneof wins regardless of field order", [] {
        for (bool encryptedFirst : {false, true}) {
            std::vector<uint8_t> inner = {0x0D, 1, 0, 0, 0};
            const uint8_t cipher[] = {0x2A, 2, 0xAB, 0xCD};
            const uint8_t decoded[] = {0x22, 2, 0x08, 1};
            const uint8_t *first = encryptedFirst ? cipher : decoded;
            const uint8_t *second = encryptedFirst ? decoded : cipher;
            inner.insert(inner.end(), first, first + 4);
            inner.insert(inner.end(), second, second + 4);
            std::vector<uint8_t> wire = {0x0A, static_cast<uint8_t>(inner.size())};
            wire.insert(wire.end(), inner.begin(), inner.end());
            MeshHdr hdr{}; uint8_t out[4]; size_t n = 0; bool plain = true;
            if (!decodeServiceEnvelope(wire.data(), wire.size(), hdr, out, sizeof(out),
                                       n, plain, nullptr, 0)
                || plain || n != 2 || out[0] != 0xAB || out[1] != 0xCD) return false;
        }
        return true;
    });
    guarded("MQTT: plaintext payload and truncated channel name stay bounded", [] {
        const uint8_t wire[] = {0x0A, 9, 0x0D, 1, 0, 0, 0, 0x22, 2, 0x08, 1,
                                0x12, 2, 'L', 'F'};
        MeshHdr hdr{}; uint8_t out[4] = {}; size_t n = 0; bool plain = false;
        char channel[3] = {'!', '!', '!'};
        if (!decodeServiceEnvelope(wire, sizeof(wire), hdr, out, sizeof(out),
                                   n, plain, channel, 2)
            || !plain || n != 2 || out[0] != 8 || out[1] != 1
            || strcmp(channel, "L") || channel[2] != '!') return false;
        memset(out, 0xA5, sizeof(out));
        return !decodeServiceEnvelope(wire, sizeof(wire), hdr, out, 1,
                                      n, plain, nullptr, 0) && out[1] == 0xA5;
    });
    guarded("MQTT map report: no location fields when location sharing is off", [] {
        MapReportInfo info{};
        info.longName = "Test node"; info.shortName = "TEST"; info.firmwareVersion = "test";
        info.latI = 123456789; info.lonI = -987654321; info.alt = 42; info.positionPrecision = 15;
        info.onlineLocalNodes = 7;
        for (bool hasPosition : {false, true}) {
            info.hasPosition = hasPosition;
            uint8_t envelope[400], data[256];
            const size_t n = encodeMapReportEnvelope(0x12345678, info, "LongFast", "!12345678",
                                                    envelope, sizeof(envelope));
            MeshHdr hdr{}; size_t dataLen = 0; bool plain = false;
            if (!n || !decodeServiceEnvelope(envelope, n, hdr, data, sizeof(data), dataLen,
                                            plain, nullptr, 0) || !plain
                || hdr.from != 0x12345678 || hdr.to != UINT32_MAX || hdr.id == 0) return false;
            const auto d = readData(data, dataLen);
            if (d.port != MAP_REPORT_APP) return false;
            uint8_t locationFields = 0;
            size_t pos = 0;
            while (pos < d.len) {
                uint64_t tag = 0, value = 0;
                pos = pbReadVarint(d.payload, d.len, pos, tag);
                if (!pos) return false;
                const uint32_t field = static_cast<uint32_t>(tag >> 3);
                if (field >= 9 && field <= 12) locationFields |= 1u << (field - 9);
                switch (tag & 7) {
                    case 0:
                        pos = pbReadVarint(d.payload, d.len, pos, value);
                        if (!pos) return false;
                        break;
                    case 2:
                        pos = pbReadVarint(d.payload, d.len, pos, value);
                        if (!pos || value > d.len - pos) return false;
                        pos += value;
                        break;
                    case 5: pos += 4; break;
                    default: return false;
                }
                if (pos > d.len) return false;
            }
            if (locationFields != (hasPosition ? 15 : 0)) return false;
        }
        return true;
    });
}

static void testEncoderOutputBounds() {
    const struct { const char *name; size_t (*encode)(uint8_t *, size_t); } encoders[] = {
        {"text output capacity", [](uint8_t *b, size_t n) { return encodeTextMessage("hi", b, n); }},
        {"long reaction output capacity", [](uint8_t *b, size_t n) {
            char text[129]; memset(text, 'x', 128); text[128] = '\0';
            return encodeTextMessage(text, b, n, UINT32_MAX, UINT32_MAX, 0x1F44D);
        }},
        {"unicast output capacity", [](uint8_t *b, size_t n) { return encodeTextMessageUnicast("hi", 1, 2, b, n); }},
        {"admin output capacity", [](uint8_t *b, size_t n) { const uint8_t p[] = {8, 1}; return encodeAdminData(p, 2, 1, 2, true, b, n); }},
        {"nodeinfo output capacity", [](uint8_t *b, size_t n) { const uint8_t mac[6] = {}; return encodeNodeInfo(1, "Node", "NODE", mac, b, n); }},
        {"shared node output capacity", [](uint8_t *b, size_t n) { return encodeSharedNodeInfo(1, "Peer", "PEER", nullptr, b, n); }},
        {"position output capacity", [](uint8_t *b, size_t n) { return encodePosition(1, 2, 3, b, n); }},
        {"device telemetry output capacity", [](uint8_t *b, size_t n) { return encodeTelemetryDevice(50, 4, 1, 1, 1, 0, b, n); }},
        {"environment telemetry output capacity", [](uint8_t *b, size_t n) { return encodeTelemetryEnvironment(-10, 50, 1000, 0, b, n); }},
        {"neighbor output capacity", [](uint8_t *b, size_t n) { return encodeNeighborInfo(1, 900, nullptr, 0, b, n); }},
        {"routing output capacity", [](uint8_t *b, size_t n) { return encodeRouting(1, 2, 0, b, n); }},
        {"traceroute request output capacity", [](uint8_t *b, size_t n) { return encodeTracerouteRequest(b, n); }},
        {"traceroute reply output capacity", [](uint8_t *b, size_t n) { return encodeTracerouteReply(b, n, nullptr, 0, 1, 2, 1.0f); }},
        {"position request output capacity", [](uint8_t *b, size_t n) { return encodePositionRequest(b, n); }},
        {"MQTT envelope output capacity", [](uint8_t *b, size_t n) {
            const MeshHdr h{UINT32_MAX, 1, 2, 0x6B, 8, 0, 0}; const uint8_t cipher[] = {1, 2};
            return encodeServiceEnvelope(h, cipher, 2, -3.25f, -95, 1700000000, "LF", "!00000001", b, n);
        }},
        {"map report output capacity", [](uint8_t *b, size_t n) {
            MapReportInfo info{}; info.longName = "Node"; info.shortName = "NODE"; info.firmwareVersion = "test";
            return encodeMapReportEnvelope(1, info, "LF", "!00000001", b, n);
        }},
    };
    for (const auto &encoder : encoders) {
        guarded(encoder.name, [&encoder] {
            uint8_t full[256] = {};
            const size_t size = encoder.encode(full, sizeof(full));
            if (!size) return false;
            for (size_t cap = 0; cap <= size + 8; cap++) {
                std::vector<uint8_t> out(cap + 16, 0xA5);
                const size_t n = encoder.encode(out.data() + 8, cap);
                if (n > cap) return false;
                for (size_t i = 0; i < 8; i++) {
                    if (out[i] != 0xA5 || out[cap + 8 + i] != 0xA5) return false;
                }
                if (n && n != size) return false;
            }
            return true;
        });
    }
}

int main() {
    testGoldenEncodings();
    testDecoderFieldsAndBoundaries();
    testApplicationRoundTrips();
    testChannelPolicyAndPrecision();
    testMqttEnvelopes();
    testEncoderOutputBounds();
    // The audit's frame: field 15, wire type 2, length 0xFFFFFFFA. With a 32-bit
    // size_t (the ESP32) the cursor wraps to 0 and re-reads the tag forever.
    guarded("decodeData: 7A FA FF FF FF 0F terminates", [] {
        const uint8_t f[] = {0x7A, 0xFA, 0xFF, 0xFF, 0xFF, 0x0F};
        const uint8_t *pay; uint32_t port;
        return dataSliceInBounds(f, sizeof(f), pay, port);
    });
    // The same wrap with this host's 64-bit size_t.
    guarded("decodeData: 64-bit length wrap terminates", [] {
        const uint8_t f[] = {0x7A, WRAP_LEN};
        const uint8_t *pay; uint32_t port;
        return dataSliceInBounds(f, sizeof(f), pay, port);
    });
    guarded("decodeData: huge payload length is dropped", [] {
        const uint8_t f[] = {0x08, 0x01, 0x12, 0xFA, 0xFF, 0xFF, 0xFF, 0x0F, 'h', 'i'};
        const uint8_t *pay; uint32_t port;
        return dataSliceInBounds(f, sizeof(f), pay, port) && port == 1 && pay == nullptr;
    });
    guarded("decodeData: payload overrunning the frame is dropped", [] {
        const uint8_t mem[64] = {0x08, 0x01, 0x12, 0x10, 'h', 'i'};
        const uint8_t *pay; uint32_t port;
        return dataSliceInBounds(mem, 6, pay, port) && pay == nullptr;
    });
    guarded("decodeData: a valid frame still decodes", [] {
        const uint8_t f[] = {0x08, 0x01, 0x12, 0x02, 'h', 'i', 0x35, 0x78, 0x56, 0x34, 0x12};
        const uint8_t *pay; size_t payLen; uint32_t port, req = 0; bool want = false;
        decodeData(f, sizeof(f), port, pay, payLen, req, want);
        return port == 1 && pay == f + 4 && payLen == 2 && req == 0x12345678;
    });

    // DeviceMetrics claims 4 bytes but the frame ends after 2; the bytes that
    // follow in memory (08 37: battery 55%) must not be read.
    guarded("decodeTelemetry: sub-message past the frame is not read", [] {
        const uint8_t mem[] = {0x12, 0x04, 0x10, 0x01, 0x08, 0x37};
        TelemetryInfo t;
        decodeTelemetry(mem, 4, t);
        return !t.hasDeviceMetrics && t.battPct != 55.0f;
    });
    guarded("decodeTelemetry: 64-bit length wrap terminates", [] {
        const uint8_t f[] = {0x1A, WRAP_LEN};
        TelemetryInfo t;
        decodeTelemetry(f, sizeof(f), t);
        return !t.valid;
    });
    guarded("decodeTelemetry: valid device and environment metrics", [] {
        const uint8_t f[] = {0x12, 0x07, 0x08, 0x50, 0x15, 0x00, 0x00, 0x80, 0x40,
                             0x1A, 0x05, 0x0D, 0x00, 0x00, 0xA0, 0x41};
        TelemetryInfo t;
        decodeTelemetry(f, sizeof(f), t);
        return t.valid && t.battPct == 80.0f && t.voltage == 4.0f && t.temperatureC == 20.0f;
    });

    // pbSkip: the skipped field's length takes the cursor from 13 back to 2.
    guarded("decodePosition: skipped field wrapping the cursor terminates", [] {
        const uint8_t f[] = {0x08, 0x00, 0x4A, WRAP_LEN};
        PositionInfo p;
        decodePosition(f, sizeof(f), p);
        return true;
    });
    guarded("decodeUser: name wrapping the cursor terminates", [] {
        const uint8_t f[] = {0x12, WRAP_LEN};
        UserInfo u;
        decodeUser(f, sizeof(f), u);
        return u.longName[0] == '\0';
    });
    // User.role (field 7, varint): CLIENT_BASE, then omitted (proto3 CLIENT).
    guarded("decodeUser: role", [] {
        const uint8_t withRole[] = {0x1A, 0x02, 'A', 'B', 0x38, 0x0C};
        const uint8_t noRole[] = {0x1A, 0x02, 'A', 'B'};
        UserInfo a, b;
        decodeUser(withRole, sizeof(withRole), a);
        b.role = 0xEE;
        decodeUser(noRole, sizeof(noRole), b);
        return a.role == 12 && !strcmp(a.shortName, "AB") && b.role == 0;
    });
    guarded("decodeNeighborInfo: neighbor wrapping the cursor terminates", [] {
        const uint8_t f[] = {0x22, WRAP_LEN};
        NeighborInfoPayload n;
        decodeNeighborInfo(f, sizeof(f), n);
        return n.neighborCount == 0;
    });
    guarded("decodeMeshBeacon: message wrapping the cursor terminates", [] {
        const uint8_t f[] = {0x0A, WRAP_LEN};
        MeshBeaconPayload b;
        return !decodeMeshBeacon(f, sizeof(f), b);
    });

    // The netsvc audit's MQTT downlink: ServiceEnvelope field 2 wrapping to 0.
    guarded("decodeServiceEnvelope: 12 F5 FF.. 01 terminates", [] {
        const uint8_t f[] = {0x12, WRAP_LEN};
        MeshHdr h; uint8_t cipher[256]; size_t cl = 0; bool plain = false; char ch[16];
        return !decodeServiceEnvelope(f, sizeof(f), h, cipher, sizeof(cipher), cl, plain, ch, sizeof(ch));
    });
    guarded("decodeServiceEnvelope: packet field wrapping the cursor terminates", [] {
        const uint8_t f[] = {0x0A, WRAP_LEN};
        MeshHdr h; uint8_t cipher[256]; size_t cl = 0; bool plain = false;
        return !decodeServiceEnvelope(f, sizeof(f), h, cipher, sizeof(cipher), cl, plain, nullptr, 0);
    });
    // Inner MeshPacket: from, then field 7 (wire type 2) whose length takes the
    // cursor from 16 back to 5, onto that same field.
    guarded("decodeServiceEnvelope: inner MeshPacket wrap terminates", [] {
        const uint8_t f[] = {0x0A, 0x10, 0x0D, 0x01, 0x00, 0x00, 0x00, 0x3A, WRAP_LEN};
        MeshHdr h; uint8_t cipher[256]; size_t cl = 0; bool plain = false;
        return !decodeServiceEnvelope(f, sizeof(f), h, cipher, sizeof(cipher), cl, plain, nullptr, 0);
    });
    guarded("decodeServiceEnvelope: a valid envelope still decodes", [] {
        const uint8_t f[] = {0x0A, 0x0B, 0x0D, 0x01, 0x00, 0x00, 0x00, 0x22, 0x04, 0x08, 0x01, 0x12, 0x00,
                             0x12, 0x02, 'L', 'F'};
        MeshHdr h; uint8_t cipher[256]; size_t cl = 0; bool plain = false; char ch[16];
        return decodeServiceEnvelope(f, sizeof(f), h, cipher, sizeof(cipher), cl, plain, ch, sizeof(ch))
            && h.from == 1 && cl == 4 && plain && strcmp(ch, "LF") == 0;
    });

    guarded("pbReadVarint: an 11-byte varint is rejected", [] {
        const uint8_t f[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
        uint64_t v;
        return pbReadVarint(f, sizeof(f), 0, v) == 0;
    });
    guarded("pbReadVarint: a 10-byte varint is accepted", [] {
        const uint8_t f[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
        uint64_t v;
        return pbReadVarint(f, sizeof(f), 0, v) == 10 && v == UINT64_MAX;
    });

    // An unencrypted TEXT frame. The keyless virtual DM/LIVE slots and empty
    // slots must not take it; a named no-encryption channel must.
    static const uint8_t kClear[] = {0x08, 0x01, 0x12, 0x02, 'h', 'i'};
    guarded("decryptPacket: cleartext not accepted on virtual slots", [] {
        MeshHdr h = {}; h.to = 0xFFFFFFFF; h.from = 0x1234; h.id = 7; h.channel = 0xFF;
        uint8_t plain[64]; size_t pl = 0;
        return decryptPacket(h, kClear, plain, sizeof(kClear), pl) == -1;
    });
    guarded("decryptPacket: cleartext not accepted on an unnamed or disabled slot", [] {
        CHANNEL_KEYS[2].keyLen = 0;
        ChannelKey &off = CHANNEL_KEYS[3];
        strcpy(off.name_buf, "Off");
        off.name = off.name_buf;
        off.keyLen = 0;
        off.role = 2;
        MeshHdr h = {}; h.to = 0xFFFFFFFF; h.from = 0x1234; h.id = 7; h.channel = 0x00;
        uint8_t plain[64]; size_t pl = 0;
        return decryptPacket(h, kClear, plain, sizeof(kClear), pl) == -1;
    });
    guarded("decryptPacket: a configured no-encryption channel still works", [] {
        ChannelKey &ck = CHANNEL_KEYS[1];
        strcpy(ck.name_buf, "Open");
        ck.name = ck.name_buf;
        ck.keyLen = 0;
        ck.hash = channelKeyHash(ck);
        MeshHdr h = {}; h.to = 0xFFFFFFFF; h.from = 0x1234; h.id = 7; h.channel = ck.hash;
        uint8_t plain[64]; size_t pl = 0;
        return decryptPacket(h, kClear, plain, sizeof(kClear), pl) == 1 && pl == sizeof(kClear);
    });
    guarded("decryptPacket: PSK index 0 on a named channel still works", [] {
        ChannelKey &ck = CHANNEL_KEYS[4];
        strcpy(ck.name_buf, "Plain");
        ck.name = ck.name_buf;
        ck.key[0] = 0x00;
        ck.keyLen = 1;
        ck.hash = channelKeyHash(ck);
        MeshHdr h = {}; h.to = 0xFFFFFFFF; h.from = 0x1234; h.id = 7; h.channel = ck.hash;
        uint8_t plain[64]; size_t pl = 0;
        return decryptPacket(h, kClear, plain, sizeof(kClear), pl) == 4;
    });
    guarded("decryptPacket: cleartext for another channel's hash is not filed on a keyless one", [] {
        // A licensed sender's keyless "sumat": its hash is the name's alone,
        // which no channel here carries. The receiver's AA== channel would
        // read it, but nothing ties it to that channel.
        ChannelKey &plainCh = CHANNEL_KEYS[2];
        strcpy(plainCh.name_buf, "Plain");
        plainCh.name = plainCh.name_buf;
        plainCh.key[0] = 0x00;
        plainCh.keyLen = 1;
        plainCh.hash = channelKeyHash(plainCh);
        ChannelKey other = {};
        strcpy(other.name_buf, "sumat");
        other.name = other.name_buf;
        other.keyLen = 0;
        MeshHdr h = {}; h.to = 0xFFFFFFFF; h.from = 0x1234; h.id = 7;
        h.channel = channelKeyHash(other);
        for (int i = 0; i < MAX_CHANNELS; i++) {
            if (CHANNEL_KEYS[i].hash == h.channel) return false;   // test premise
        }
        uint8_t plain[64]; size_t pl = 0;
        return decryptPacket(h, kClear, plain, sizeof(kClear), pl) == -1;
    });

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
