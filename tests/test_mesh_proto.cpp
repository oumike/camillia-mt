// Host test for the protobuf decoders and channel-key trial in src/mesh_proto.cpp.
//
// Every input here is hostile: a length that wraps the cursor backwards, a
// payload that claims more bytes than the frame holds, a varint that never
// ends. Each case runs in a forked child under an alarm, so a decoder that
// loops forever or faults is reported as a failure instead of hanging the run.
// test-deps: ack_proof.cpp sha256_portable.cpp
// test-cflags: -Ihost -DDEVICE_TDECK -Wno-missing-field-initializers
#include "../src/mesh_proto.h"
#include "../src/xeddsa_sig.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

uint8_t myPubKey[32];
uint8_t myPrivKey[32];
uint8_t myDeviceRole;
void debugLogMessages(const char *, ...) {}
uint32_t xeddsaSignerNodeId() { return 0; }
bool xeddsaSign(const XeddsaSignedFields &, const uint8_t *, size_t, uint8_t *) { return false; }

static int g_fail = 0, g_run = 0;

// Runs fn in a child; passes only if it returns true within two seconds.
template <typename F>
static void guarded(const char *what, F fn) {
    g_run++;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(2);
        _exit(fn() ? 0 : 1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
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

int main() {
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

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_run, g_fail);
    return g_fail ? 1 : 0;
}
