#include "node_name_cache.h"

#include <string.h>

// Bounded copy that never leaves a split UTF-8 sequence at the cut. Names are
// copied between buffers of the same size, so this only matters for a source
// that was not terminated where it should have been.
static void copyName(char *dst, size_t dstLen, const char *src) {
    if (!dst || dstLen == 0) return;
    size_t n = 0;
    if (src) {
        while (n + 1 < dstLen && src[n]) n++;
        if (src[n]) {
            // Truncated: back off any continuation bytes and their lead byte.
            size_t cut = n;
            while (cut > 0 && ((unsigned char)src[cut] & 0xC0) == 0x80) cut--;
            n = cut;
        }
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

void NodeNameCache::begin(NodeIdentity *slots, int capacity) {
    _s = (slots && capacity > 0) ? slots : nullptr;
    _cap = _s ? capacity : 0;
    clear();
}

void NodeNameCache::remember(uint32_t nodeId, const char *shortName, const char *longName) {
    if (!_cap || nodeId == 0) return;
    const bool hasShort = shortName && shortName[0];
    const bool hasLong  = longName && longName[0];
    if (!hasShort && !hasLong) return;

    NodeIdentity *slot = nullptr;
    for (int i = 0; i < _cap; i++) {
        if (_s[i].nodeId == nodeId) { slot = &_s[i]; break; }
    }
    if (!slot) {
        slot = &_s[_next];
        _next = (_next + 1) % _cap;
    }
    memset(slot, 0, sizeof(*slot));
    slot->nodeId = nodeId;
    copyName(slot->shortName, sizeof(slot->shortName), shortName);
    copyName(slot->longName, sizeof(slot->longName), longName);
}

bool NodeNameCache::take(uint32_t nodeId, NodeIdentity &out) {
    if (!_cap || nodeId == 0) return false;
    for (int i = 0; i < _cap; i++) {
        if (_s[i].nodeId == nodeId) {
            out = _s[i];
            memset(&_s[i], 0, sizeof(_s[i]));
            return true;
        }
    }
    return false;
}

void NodeNameCache::forget(uint32_t nodeId) {
    NodeIdentity dropped;
    take(nodeId, dropped);
}

void NodeNameCache::clear() {
    if (_s) memset(_s, 0, sizeof(NodeIdentity) * (size_t)_cap);
    _next = 0;
}

int NodeNameCache::count() const {
    int n = 0;
    for (int i = 0; i < _cap; i++) {
        if (_s[i].nodeId != 0) n++;
    }
    return n;
}
