#include "licensed_mode.h"

#include <ctype.h>
#include <string.h>

#include "utf8_utils.h"

namespace lic {

namespace {
bool sActive = false;

// Meshtastic ports never signed on unicast: 5 = ROUTING_APP, 70 = TRACEROUTE_APP.
constexpr uint32_t kPortRouting = 5;
constexpr uint32_t kPortTraceroute = 70;
}  // namespace

void setActive(bool on) { sActive = on; }
bool active() { return sActive; }

bool normalizeCallSign(const char *in, char *out, size_t outCap) {
    if (!in || !out) return false;
    while (*in && isspace((unsigned char)*in)) in++;
    size_t len = strlen(in);
    while (len > 0 && isspace((unsigned char)in[len - 1])) len--;
    if (len == 0 || len > kCallSignMax || len + 1 > outCap) return false;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)in[i];
        if (!(isalnum(c) && c < 0x80) && c != '/') return false;
    }
    for (size_t i = 0; i < len; i++) out[i] = (char)toupper((unsigned char)in[i]);
    out[len] = '\0';
    return true;
}

bool isNoCall(const char *callSign) {
    if (!callSign) return false;
    static const char kNoCall[] = "N0CALL";
    for (size_t i = 0; i < sizeof(kNoCall); i++) {
        if (toupper((unsigned char)callSign[i]) != kNoCall[i]) return false;
    }
    return true;
}

size_t composeLongName(const char *call, const char *suffix, char *out, size_t outCap) {
    if (!out || outCap == 0) return 0;
    if (!call) call = "";
    size_t len = utf8util::clampPrefixBytes(call, outCap - 1);
    memcpy(out, call, len);
    if (suffix && *suffix && len + 2 < outCap) {
        out[len++] = '/';
        out[len++] = '/';
        const size_t n = utf8util::clampPrefixBytes(suffix, outCap - 1 - len);
        memcpy(out + len, suffix, n);
        len += n;
    }
    out[len] = '\0';
    return len;
}

uint8_t powerAfterChange(bool wasLicensed, bool nowLicensed, uint8_t power, uint8_t regionCap) {
    if (wasLicensed && !nowLicensed && power > regionCap) return regionCap;
    return power;
}

PeerStatus peerStatus(bool known, bool isLicensed) {
    if (!known) return PEER_UNKNOWN;
    return isLicensed ? PEER_LICENSED : PEER_UNLICENSED;
}

bool allowPacket(bool selfLicensed, PeerStatus from, PeerStatus to) {
    if (!selfLicensed) return true;
    return from != PEER_UNLICENSED && to != PEER_UNLICENSED;
}

bool allowRelay(bool selfLicensed, bool decoded, PeerStatus from, PeerStatus to) {
    if (!selfLicensed) return true;
    return decoded && allowPacket(selfLicensed, from, to);
}

bool shouldSign(bool selfLicensed, bool broadcast, uint32_t portnum) {
    // Every hop rewrites a traceroute's route, so no signature survives the
    // first relay; 2.8.2 never signs one and ignores any it receives.
    if (portnum == kPortTraceroute) return false;
    if (broadcast) return true;
    return selfLicensed && portnum != kPortRouting && portnum != kPortTraceroute;
}

bool acceptNodeInfo(bool selfLicensed, bool peerLicensed) {
    return selfLicensed == peerLicensed;
}

bool regionSelectable(bool licensedOnly, float freqStartMhz, bool selfLicensed) {
    if (freqStartMhz < kRadioMinMhz) return false;
    return !licensedOnly || selfLicensed;
}

}  // namespace lic
