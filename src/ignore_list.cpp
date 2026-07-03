#include "ignore_list.h"
#include <Preferences.h>

IgnoreList Ignored;

// Blob-backed storage in the shared "camillia" NVS namespace: a raw array of
// uint32_t IDs. Small enough to write in full on every mutation.
static constexpr const char *kNamespace = "camillia";
static constexpr const char *kKey       = "ignoreIds";

void IgnoreList::init() {
    _count = 0;
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return;
    if (prefs.isKey(kKey)) {
        size_t blobLen = prefs.getBytesLength(kKey);
        size_t maxLen  = sizeof(_ids);
        if (blobLen > maxLen) blobLen = maxLen;
        size_t got = prefs.getBytes(kKey, _ids, blobLen);
        _count = (int)(got / sizeof(uint32_t));
        if (_count > kMax) _count = kMax;
    }
    prefs.end();
    Serial.printf("[ignore] loaded %d entries\n", _count);
}

bool IgnoreList::contains(uint32_t nodeId) const {
    if (nodeId == 0 || nodeId == 0xFFFFFFFF) return false;
    for (int i = 0; i < _count; i++) {
        if (_ids[i] == nodeId) return true;
    }
    return false;
}

bool IgnoreList::add(uint32_t nodeId) {
    if (nodeId == 0 || nodeId == 0xFFFFFFFF) return false;
    if (contains(nodeId)) return false;
    if (_count >= kMax) {
        Serial.printf("[ignore] list full (%d), refusing !%08lx\n",
                      kMax, (unsigned long)nodeId);
        return false;
    }
    _ids[_count++] = nodeId;
    _persist();
    Serial.printf("[ignore] added !%08lx (%d total)\n",
                  (unsigned long)nodeId, _count);
    return true;
}

bool IgnoreList::remove(uint32_t nodeId) {
    for (int i = 0; i < _count; i++) {
        if (_ids[i] == nodeId) {
            for (int j = i + 1; j < _count; j++) _ids[j - 1] = _ids[j];
            _count--;
            _ids[_count] = 0;
            _persist();
            Serial.printf("[ignore] removed !%08lx (%d total)\n",
                          (unsigned long)nodeId, _count);
            return true;
        }
    }
    return false;
}

uint32_t IgnoreList::at(int i) const {
    if (i < 0 || i >= _count) return 0;
    return _ids[i];
}

void IgnoreList::replace(const uint32_t *ids, int n) {
    _count = 0;
    if (n < 0) n = 0;
    if (n > kMax) n = kMax;
    for (int i = 0; i < n; i++) {
        uint32_t id = ids[i];
        if (id == 0 || id == 0xFFFFFFFF) continue;
        // Dedup while copying.
        bool dup = false;
        for (int j = 0; j < _count; j++) {
            if (_ids[j] == id) { dup = true; break; }
        }
        if (!dup) _ids[_count++] = id;
    }
    for (int i = _count; i < kMax; i++) _ids[i] = 0;
    _persist();
    Serial.printf("[ignore] replaced list, %d entries\n", _count);
}

void IgnoreList::clear() {
    if (_count == 0) return;
    _count = 0;
    for (int i = 0; i < kMax; i++) _ids[i] = 0;
    _persist();
}

void IgnoreList::_persist() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    if (_count > 0) {
        prefs.putBytes(kKey, _ids, _count * sizeof(uint32_t));
    } else if (prefs.isKey(kKey)) {
        prefs.remove(kKey);
    }
    prefs.end();
}
