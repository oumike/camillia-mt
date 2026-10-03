#include "wardrive_log.h"
#include "wardrive_util.h"
#include "node_db.h"
#include "gps.h"
#include "config_io.h"   // sdBegin()
#include "storage.h"
#include <esp_heap_caps.h>
#include <time.h>

static bool s_enabled = false;
void wardriveLogSetEnabled(bool enabled) { s_enabled = enabled; }
bool wardriveLogIsEnabled() { return s_enabled; }

static uint32_t s_lines = 0, s_noFix = 0, s_dropped = 0;
uint32_t wardriveLogLines()        { return s_lines; }
uint32_t wardriveLogSkippedNoFix() { return s_noFix; }
uint32_t wardriveLogDropped()      { return s_dropped; }

#if HAS_FILE_STORAGE

static const char *kLogPath = "/camillia/wardrive.csv";
static const char *kLogDir  = "/camillia";

const char *wardriveLogFilePath() { return kLogPath; }
bool wardriveLogAvailable() { return sdCardMounted(); }

// ── Throttle ─────────────────────────────────────────────────────────────────
// When each node last got a line, and where we were. Small and LRU: a node that
// falls out simply gets its next sighting logged, which is the safe direction.
static constexpr uint32_t kWardriveMinIntervalMs = 30000;
static constexpr uint32_t kWardriveMinMoveM      = 50;
static constexpr int      kThrottleSlots         = 64;

struct ThrottleSlot { uint32_t nodeId, ms; int32_t latI, lonI; };
static ThrottleSlot s_thr[kThrottleSlots];

// ── Distinct-node counter ────────────────────────────────────────────────────
// Exact up to its capacity, then it stops growing and the UI says "+". PSRAM
// when there is some, which every board but the Cardputer has.
#if defined(BOARD_HAS_PSRAM) && BOARD_HAS_PSRAM
static constexpr int kSeenMax = 2048;
#else
static constexpr int kSeenMax = 128;
#endif
static uint32_t *s_seen = nullptr;
static int       s_seenCount = 0;

static void seenNote(uint32_t id) {
    if (!s_seen) {
        s_seen = (uint32_t *)heap_caps_malloc(kSeenMax * sizeof(uint32_t),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_seen) s_seen = (uint32_t *)malloc(kSeenMax * sizeof(uint32_t));
        if (!s_seen) return;
    }
    for (int i = 0; i < s_seenCount; i++) if (s_seen[i] == id) return;
    if (s_seenCount < kSeenMax) s_seen[s_seenCount++] = id;
}
uint32_t wardriveLogNodes() { return (uint32_t)s_seenCount; }

// ── Queue ────────────────────────────────────────────────────────────────────
struct Sighting {
    long     epoch;          // 0 = clock unset
    uint32_t nodeId;
    int32_t  latI, lonI, altM;
    float    rssi, snr, hdop, speedKmh;
    int16_t  hops;           // -1 = unknown
    int16_t  portnum;
    int8_t   chanIdx;
    uint8_t  sats;
};
static constexpr int kQueueLen = 16;
static Sighting s_q[kQueueLen];
static int s_qHead = 0, s_qTail = 0, s_qCount = 0;

static uint32_t s_sdRetryAtMs = 0;
static constexpr uint32_t kSdRetryMs = 60000;

void wardriveLogNoteSighting(uint32_t nodeId, float rssi, float snr, int hops,
                             int portnum, int chanIdx) {
    if (!s_enabled || nodeId == 0) return;
    if (!gpsHasFix()) { s_noFix++; return; }

    const uint32_t now = millis();
    const int32_t lat = gpsLatI(), lon = gpsLonI();

    // Throttle lookup; remember the LRU slot in case this node has none.
    int slot = -1, lru = 0;
    for (int i = 0; i < kThrottleSlots; i++) {
        if (s_thr[i].nodeId == nodeId) { slot = i; break; }
        if (s_thr[i].nodeId == 0) { lru = i; continue; }
        if (s_thr[lru].nodeId != 0 && (int32_t)(s_thr[i].ms - s_thr[lru].ms) < 0) lru = i;
    }
    if (slot >= 0) {
        const ThrottleSlot &t = s_thr[slot];
        if (!wardriveShouldLog(now - t.ms, t.latI, t.lonI, lat, lon,
                               kWardriveMinIntervalMs, kWardriveMinMoveM)) return;
    } else {
        slot = lru;
    }
    s_thr[slot] = { nodeId, now, lat, lon };

    if (s_qCount >= kQueueLen) {   // storage wedged: drop the oldest
        s_qTail = (s_qTail + 1) % kQueueLen;
        s_qCount--;
        s_dropped++;
    }
    Sighting &s = s_q[s_qHead];
    const time_t nowEpoch = time(nullptr);
    s.epoch    = (nowEpoch > 1700000000) ? (long)nowEpoch : 0L;
    s.nodeId   = nodeId;
    s.latI     = lat;
    s.lonI     = lon;
    s.altM     = gpsAltM();
    s.rssi     = rssi;
    s.snr      = snr;
    s.hdop     = gpsHdop();
    s.speedKmh = gpsSpeedKmh();
    s.hops     = (int16_t)hops;
    s.portnum  = (int16_t)portnum;
    s.chanIdx  = (int8_t)chanIdx;
    s.sats     = gpsSats();
    s_qHead = (s_qHead + 1) % kQueueLen;
    s_qCount++;
}

static void discardAll() {
    s_dropped += (uint32_t)s_qCount;
    s_qCount = 0;
    s_qHead = s_qTail = 0;
}

static const char *kHeader =
    "epoch,utc,nodeId,shortName,longName,rssi,snr,hops,portnum,chanIdx,"
    "lat,lon,altM,sats,hdop,speedKmh,nodeLat,nodeLon";

void wardriveLogFlush() {
    if (s_qCount <= 0) return;
    if (!s_enabled) { discardAll(); return; }

    const uint32_t nowMs = millis();
    if (s_sdRetryAtMs != 0 && (int32_t)(nowMs - s_sdRetryAtMs) < 0) {
        discardAll();
        return;
    }
    if (!sdBegin()) {
        if (s_sdRetryAtMs == 0) Serial.println("[wardrive] no storage - sightings not logged");
        s_sdRetryAtMs = nowMs + kSdRetryMs;
        discardAll();
        return;
    }
    s_sdRetryAtMs = 0;

    storageFs().mkdir(kLogDir);
    const bool needHeader = !storageFs().exists(kLogPath);
    File f = storageFs().open(kLogPath, FILE_APPEND);
    if (!f) {
        Serial.println("[wardrive] open failed - dropping queued sightings");
        discardAll();
        return;
    }
    if (needHeader) f.println(kHeader);

    while (s_qCount > 0) {
        const Sighting &s = s_q[s_qTail];
        // Names and the node's own position are read now, not at queue time:
        // a POSITION or NODEINFO packet is processed just after the sighting it
        // arrived in, so this is the freshest the line can be.
        const NodeEntry *e = Nodes.find(s.nodeId);
        char line[384];
        wardriveFormatLine(line, sizeof(line),
                           s.epoch, s.nodeId,
                           e ? e->shortName : "", e ? e->longName : "",
                           s.rssi, s.snr, s.hops, s.portnum, s.chanIdx,
                           s.latI, s.lonI, s.altM, s.sats, s.hdop, s.speedKmh,
                           (e && e->hasPosition && (e->latI || e->lonI)),
                           e ? e->latI : 0, e ? e->lonI : 0);
        if (f.println(line) == 0) {
            // Card full or yanked. Count this and everything behind it.
            Serial.println("[wardrive] write failed");
            f.close();
            discardAll();
            return;
        }
        seenNote(s.nodeId);
        s_lines++;
        s_qTail = (s_qTail + 1) % kQueueLen;
        s_qCount--;
    }
    f.close();
}

bool wardriveLogClear() {
    s_qCount = 0;
    s_qHead = s_qTail = 0;
    memset(s_thr, 0, sizeof(s_thr));
    s_seenCount = 0;
    s_lines = s_noFix = s_dropped = 0;
    if (!sdBegin()) return false;
    if (!storageFs().exists(kLogPath)) return true;
    const bool ok = storageFs().remove(kLogPath);
    Serial.printf("[wardrive] clear: %s\n", ok ? "removed" : "REMOVE FAILED");
    return ok;
}

#else   // !HAS_FILE_STORAGE

const char *wardriveLogFilePath() { return nullptr; }
bool wardriveLogAvailable() { return false; }
uint32_t wardriveLogNodes() { return 0; }
void wardriveLogNoteSighting(uint32_t, float, float, int, int, int) {}
void wardriveLogFlush() {}
bool wardriveLogClear() { return false; }

#endif
