#pragma once
// Which of two node-table entries a full table should give up first.
//
// The rule is least-recently-heard goes first, in three tiers:
//
//   1. A node not heard since boot (lastHeardMs == 0) is older than any node
//      that has been. Among those, staleRank orders them: it is the node's
//      position in the recency-ordered id index saved before the reboot
//      (0 = most recently heard), so a node heard minutes before power-off
//      outlives one last heard last month. Without it every loaded node was
//      equally old, and the first newcomers after boot evicted at random --
//      as likely the neighbours about to be heard again as nodes long gone.
//   2. Two nodes heard this boot: the longer silence goes first. Compared as
//      ages (nowMs - lastHeardMs), not raw stamps, so it survives millis()
//      wrapping after 49.7 days of uptime; comparing stamps made every node
//      heard just after the wrap look like the oldest in the table.
//   3. A tie goes to the unnamed node, which has less to lose.
//
// Favorites are not this function's concern: the caller never offers one.
//
// Header-only and free of Arduino so it can be tested on the host
// (tests/test_node_evict.cpp).
#include <stdint.h>

struct NodeEvictKey {
    uint32_t lastHeardMs;   // 0 = not heard since boot
    uint16_t staleRank;     // order before the reboot; only read when lastHeardMs == 0
    bool     hasName;
};

// True when `a` should be evicted before `b`.
inline bool nodeEvictBefore(const NodeEvictKey &a, const NodeEvictKey &b, uint32_t nowMs) {
    const bool aHeard = a.lastHeardMs != 0;
    const bool bHeard = b.lastHeardMs != 0;
    if (aHeard != bHeard) return !aHeard;
    if (aHeard) {
        const uint32_t aAge = nowMs - a.lastHeardMs;
        const uint32_t bAge = nowMs - b.lastHeardMs;
        if (aAge != bAge) return aAge > bAge;
    } else if (a.staleRank != b.staleRank) {
        return a.staleRank > b.staleRank;
    }
    return !a.hasName && b.hasName;
}
