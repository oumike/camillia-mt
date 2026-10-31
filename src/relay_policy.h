#pragma once
// Managed-flooding relay policy: which roles relay, how long a relay waits,
// when hearing someone else's copy stands it down, and when a relay keeps the
// hop it would otherwise spend. Mirrors meshtastic/firmware's FloodingRouter,
// RadioInterface (contention windows) and Router::shouldDecrementHopLimit, so
// a Camillia node in a mesh of stock nodes contends for the channel the way
// they do instead of always keying up first.
//
// The model, as upstream has it:
//
//   * A relaying node waits a random number of slots before transmitting. The
//     window grows with the SNR it heard the packet at, so the *weakest*
//     listener -- the one furthest away, whose relay covers the most new
//     ground -- tends to go first, and everyone closer hears that relay and
//     stands down (cancel-on-duplicate). That is what keeps a flood from
//     costing one transmission per node.
//   * The late window starts after every ordinary window has closed. ROUTER_LATE
//     and CLIENT_BASE (for favorites) move a queued relay there instead of
//     cancelling it, so they always relay once, but only after everyone else.
//   * Between two router-type nodes that have each other favorited, a relay
//     does not spend a hop.
//
// Header-only and free of Arduino so it can be tested on the host
// (tests/test_relay_policy.cpp).
#include <stdint.h>

namespace relay {

// Canonical Meshtastic Config.DeviceConfig.Role positions, the ones this
// policy distinguishes. ROUTER and ROUTER_LATE are here because other nodes
// advertise them, not because this firmware offers them.
enum : uint8_t {
    ROLE_CLIENT        = 0,
    ROLE_CLIENT_MUTE   = 1,
    ROLE_ROUTER        = 2,
    ROLE_CLIENT_HIDDEN = 8,
    ROLE_ROUTER_LATE   = 11,
    ROLE_CLIENT_BASE   = 12,
};

// Upstream's contention-window bounds, as powers of two of slots.
static constexpr uint8_t kCwMin = 3;
static constexpr uint8_t kCwMax = 8;

// SNR range the window is mapped across (dB): the floor and ceiling of what
// LoRa reports in practice, upstream's SNR_MIN / SNR_MAX.
static constexpr int32_t kSnrMin = -20;
static constexpr int32_t kSnrMax = 10;

// CLIENT_MUTE does not forward other people's packets. CLIENT_HIDDEN does not
// here either -- a known divergence: upstream's still relays (under local-only
// rebroadcast), and changing it would alter what existing CLIENT_HIDDEN nodes
// do on the mesh.
inline bool roleRebroadcasts(uint8_t role) {
    return role != ROLE_CLIENT_MUTE && role != ROLE_CLIENT_HIDDEN;
}

// The roles upstream treats as infrastructure: they relay favorites without
// spending a hop, get the router NodeInfo limits, and do not go asking unknown
// nodes for their NodeInfo.
inline bool isRouterRole(uint8_t role) {
    return role == ROLE_ROUTER || role == ROLE_ROUTER_LATE || role == ROLE_CLIENT_BASE;
}

// Slot length (ms): one channel-activity scan plus propagation, turnaround and
// MAC time. SX126x and LR11x0/LR2021 scan 4 symbols sub-GHz (+0.5 symbol to
// finish), the 2.4 GHz band 8 plus the SX1280 processing tail. Rounded down to
// whole ms, as upstream's uint32_t return does.
inline uint32_t slotTimeMsec(uint8_t sf, float bwKhz, bool wideLora) {
    if (bwKhz <= 0.0f || sf == 0 || sf > 12) return 0;
    const float symbolMs = (float)(1u << sf) / bwKhz;
    const float macMs = 0.2f + 0.4f + 7.0f;
    if (wideLora) return (uint32_t)((8.0f + (2.0f * sf + 3.0f) / 32.0f) * symbolMs + macMs);
    return (uint32_t)(4.5f * symbolMs + macMs);
}

// Window size (power of two of slots) for an SNR: linear from kCwMin at
// kSnrMin to kCwMax at kSnrMax, as Arduino's integer map() computes it.
// Clamped to the bounds, where upstream's map() extrapolates past them; an SNR
// outside -20..10 is not something an SX126x reports in practice.
inline uint8_t cwSize(float snr) {
    int32_t s = (int32_t)snr;   // map() takes a long: truncation, not rounding
    if (s < kSnrMin) s = kSnrMin;
    if (s > kSnrMax) s = kSnrMax;
    return (uint8_t)((s - kSnrMin) * (kCwMax - kCwMin) / (kSnrMax - kSnrMin) + kCwMin);
}

// The ordinary relay delay. `rnd` is any random 32-bit value; the slot count is
// drawn from [0, 2^cw). Offset past the early window upstream reserves for
// ROUTER (2 * kCwMax slots), which this firmware never uses itself but other
// nodes on the mesh do.
inline uint32_t relayDelayMsec(float snr, uint32_t slotMs, uint32_t rnd) {
    const uint32_t slots = rnd % (1u << cwSize(snr));
    return (2u * kCwMax + slots) * slotMs;
}

// The longest an ordinary relay can wait for this SNR: where the late window
// begins. Upstream's getTxDelayMsecWeightedWorst().
inline uint32_t lateWindowMsec(float snr, uint32_t slotMs) {
    return (2u * kCwMax + (1u << cwSize(snr))) * slotMs;
}

// What a relaying node does with a relay it has queued when it hears another
// node's copy of the same packet.
enum class DupeAction : uint8_t {
    Cancel,     // someone nearer the edge already carried it; stand down
    MoveLate,   // relay anyway, after every ordinary window has closed
    Keep,       // relay as scheduled
};

// `favoriteParty`: the packet is from, or (unicast) to, a node we favorited.
// `repeatedByOrigin`: the copy we heard still has hop_start == hop_limit, i.e.
// it is the original sender retransmitting (a lost ACK), not a relay -- which
// says nothing about whether anyone has carried it further.
inline DupeAction onDuplicateHeard(uint8_t role, bool favoriteParty, bool repeatedByOrigin) {
    if (repeatedByOrigin) return DupeAction::Keep;
    if (role == ROLE_ROUTER) return DupeAction::Keep;
    if (role == ROLE_ROUTER_LATE) return DupeAction::MoveLate;
    if (role == ROLE_CLIENT_BASE && favoriteParty) return DupeAction::MoveLate;
    return DupeAction::Cancel;
}

// What we know of the node that relayed the copy we are about to relay,
// resolved from the one-byte relay_node field. `unique` is false when no node,
// or more than one, matches that byte: then nothing is known about it.
struct PrevRelay {
    bool    unique;
    bool    favorite;
    bool    hasRole;   // a NodeInfo from it has been heard this boot
    uint8_t role;
};

// Whether relaying spends a hop. False -- keep hop_limit -- only when we are a
// router-type node, this is not the packet's first hop, and the node it came
// through is a favorited router-type node we can name unambiguously. Anything
// less certain spends the hop: preserving one wrongly carries a packet further
// than its sender asked.
inline bool shouldDecrementHopLimit(uint8_t localRole, uint8_t hopStart, uint8_t hopLimit,
                                    const PrevRelay &prev) {
    if (hopStart <= hopLimit) return true;          // first hop always pays
    if (!isRouterRole(localRole)) return true;
    if (!prev.unique || !prev.favorite || !prev.hasRole) return true;
    return !isRouterRole(prev.role);
}

}  // namespace relay
