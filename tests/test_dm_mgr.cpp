// test-deps: key_pin.cpp node_name_cache.cpp
// test-cflags: -I../src -Ihost -DDEVICE_TDECK
#include "../src/dm_mgr.h"
#include "../src/channel_mgr.h"
#include "../src/debug_flags.h"
#include "../src/live_feed.h"
#include "../src/live_util.h"
#include "../src/mesh_radio.h"
#include "../src/node_db.h"
#include "../src/storage.h"

#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } } while (0)

NodeDB Nodes;
MeshRadio Radio;
ChannelMgr Channels;
static uint32_t g_now = 10000;
static MeshHdr g_sent = {};
static int g_dmTransmits = 0;
static uint32_t g_encodedTo = 0;
static uint8_t g_encryptionKey[32] = {};
static uint32_t g_nodeInfoTo = 0;
static bool g_wantNodeInfoResponse = false;
static std::string g_liveLine;

uint32_t millis() { return g_now; }
void debugLogMessages(const char *, ...) {}
void liveFeedAddLine(const char *text, uint16_t) { g_liveLine = text; }
void liveBuildPrefix(char *out, size_t cap) { snprintf(out, cap, "--:-- "); }
void liveNodeLabelWithHint(uint32_t id, const char *, char *out, size_t cap, bool) {
    snprintf(out, cap, "!%08X", id);
}
fs::FS &storageFs() { abort(); }
void storageCallOnWriter(void (*)(void *), void *) { abort(); }

// NodeDB's in-memory fixture retains the real record layout and ranking effect,
// without bringing NVS or incoming-packet processing into a DM send test.
void NodeDB::init() { _count = 0; memset(_nodes, 0, sizeof(_nodes)); }
NodeEntry *NodeDB::find(uint32_t id) {
    for (int i = 0; i < _count; ++i)
        if (_nodes[i].nodeId == id) return &_nodes[i];
    return nullptr;
}
NodeEntry *NodeDB::upsert(uint32_t id) {
    if (NodeEntry *node = find(id)) return node;
    if (_count == MAX_NODES) return nullptr;
    NodeEntry &node = _nodes[_count++];
    node = {};
    node.nodeId = id;
    node.chanIdx = -1;
    return &node;
}
NodeEntry *NodeDB::getByRank(int rank) {
    std::sort(_nodes, _nodes + _count, [](const NodeEntry &a, const NodeEntry &b) {
        return a.lastHeardMs > b.lastHeardMs;
    });
    return rank >= 0 && rank < _count ? &_nodes[rank] : nullptr;
}
bool MeshRadio::init(uint8_t, bool) { _ready = true; return true; }
bool MeshRadio::transmit(const uint8_t *frame, size_t len) {
    if (len < sizeof(g_sent)) abort();
    memcpy(&g_sent, frame, sizeof(g_sent));
    ++g_dmTransmits;
    return true;
}
bool ChannelMgr::sendNodeInfo(uint32_t, const char *, const char *, uint32_t to,
                              bool wantResponse, bool) {
    g_nodeInfoTo = to;
    g_wantNodeInfoResponse = wantResponse;
    return true;
}
uint32_t nextMeshPacketId() { static uint32_t id = 100; return ++id; }
uint8_t meshOriginHopFlagsForChannel(int, uint8_t extra) { return (3 << 5) | 3 | extra; }
size_t encodeTextMessageUnicast(const char *text, uint32_t, uint32_t to,
                               uint8_t *out, size_t cap, uint32_t, uint32_t) {
    g_encodedTo = to;
    size_t len = strlen(text);
    if (len > cap) abort();
    memcpy(out, text, len);
    return len;
}
bool encryptPki(uint32_t, uint32_t, const uint8_t *key,
                const uint8_t *plain, size_t len, uint8_t *out) {
    memcpy(g_encryptionKey, key, sizeof(g_encryptionKey));
    memcpy(out, plain, len);
    memset(out + len, 0, 12);
    return true;
}

static constexpr uint32_t kSelf = 0x11111111;
static constexpr uint32_t kRecipient = 0x22222222;
static constexpr uint32_t kNamesake = 0x33333333;

static void reset() {
    DMs.clearAll(false);
    Nodes.init();
    Radio.init();
    g_now += 10000;
    g_dmTransmits = 0;
    g_sent = {};
    g_encodedTo = g_nodeInfoTo = 0;
    g_wantNodeInfoResponse = false;
    memset(g_encryptionKey, 0, sizeof(g_encryptionKey));
    g_liveLine.clear();
    CHECK(DMs.findOrCreate(kRecipient, "PEER") != nullptr);
}
static void addPeer(uint32_t id, uint32_t heardMs, uint8_t keyByte, bool hasKey = true) {
    NodeEntry *node = Nodes.upsert(id);
    if (!node) abort();
    memcpy(node->shortName, "PEER", 5);
    node->lastHeardMs = heardMs;
    node->hasPubKey = hasKey;
    memset(node->pubKey, keyByte, sizeof(node->pubKey));
}

static void testNewerNamesakeCannotReceiveDm() {
    reset();
    addPeer(kRecipient, 1000, 0x22);
    addPeer(kNamesake, 9000, 0x33);
    CHECK(DMs.sendDm(kSelf, kRecipient, "private message"));
    CHECK(g_dmTransmits == 1);
    CHECK(g_sent.to == kRecipient);
    CHECK(g_encodedTo == kRecipient);
    CHECK(std::all_of(g_encryptionKey, g_encryptionKey + sizeof(g_encryptionKey),
                      [](uint8_t byte) { return byte == 0x22; }));
    const DmConv *conv = DMs.find(kRecipient);
    const DmLine *line = DMs.getLine(conv, 0, 1);
    CHECK(line && strstr(line->text, "private message"));
    CHECK(line && line->packetId == g_sent.id && line->ack == DmLine::PENDING);
    CHECK(DMs.find(kNamesake) == nullptr);
}

static void testMissingRecipientNeverFallsBackToNamesake(bool recipientExists) {
    reset();
    if (recipientExists) addPeer(kRecipient, 1000, 0x22, false);
    addPeer(kNamesake, 9000, 0x33);
    CHECK(!DMs.sendDm(kSelf, kRecipient, "private message"));
    CHECK(g_dmTransmits == 0);
    CHECK(g_nodeInfoTo == kRecipient && g_wantNodeInfoResponse);
    CHECK(!g_liveLine.empty());
    CHECK(DMs.find(kRecipient)->count == 0);
    CHECK(DMs.find(kNamesake) == nullptr);
}

int main() {
    testNewerNamesakeCannotReceiveDm();
    testMissingRecipientNeverFallsBackToNamesake(false);
    testMissingRecipientNeverFallsBackToNamesake(true);
    DMs.clearAll(false);
    printf(g_fail ? "FAILED\n" : "ok\n");
    return g_fail;
}
