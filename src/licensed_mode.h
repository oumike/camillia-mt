#pragma once

// Licensed (amateur) mode decisions. Pure logic, no Arduino headers, so the
// rules can be host-tested; callers supply peer and packet facts.

#include <stddef.h>
#include <stdint.h>

namespace lic {

enum PeerStatus : uint8_t { PEER_UNKNOWN = 0, PEER_LICENSED = 1, PEER_UNLICENSED = 2 };

void setActive(bool on);  // boot only; also what tests use
bool active();            // the one "am I licensed" switch

constexpr uint32_t kStationIdIntervalS = 600;
constexpr uint8_t kRebroadcastLocalOnly = 2;
constexpr size_t kCallSignMax = 15;  // chars, excluding NUL
constexpr float kRadioMinMhz = 150.0f;

// Trims, uppercases, validates [A-Z0-9/], 1..15 chars. Returns false (out untouched) if invalid.
bool normalizeCallSign(const char *in, char *out, size_t outCap);
bool isNoCall(const char *callSign);  // "N0CALL", case-insensitive

// "CALL" or "CALL//suffix"; truncated to outCap-1 bytes on a UTF-8 boundary. Returns length.
size_t composeLongName(const char *call, const char *suffix, char *out, size_t outCap);

PeerStatus peerStatus(bool known, bool isLicensed);
// While self licensed: false if from or to is PEER_UNLICENSED. Always true when not licensed.
bool allowPacket(bool selfLicensed, PeerStatus from, PeerStatus to);
// Relay gate added on top of the existing rules: not licensed -> true;
// licensed -> decoded && allowPacket(...).
bool allowRelay(bool selfLicensed, bool decoded, PeerStatus from, PeerStatus to);
// broadcast -> true; unicast -> selfLicensed && port != 5 && port != 70.
bool shouldSign(bool selfLicensed, bool broadcast, uint32_t portnum);
bool acceptNodeInfo(bool selfLicensed, bool peerLicensed);  // equal
// licensedOnly regions need selfLicensed; any region needs freqStartMhz >= 150.
bool regionSelectable(bool licensedOnly, float freqStartMhz, bool selfLicensed);
// TX power after a config change: leaving licensed mode (wasLicensed and not
// nowLicensed) clamps power to the region's cap; otherwise power is unchanged,
// as an unlicensed setting may already sit above the cap.
uint8_t powerAfterChange(bool wasLicensed, bool nowLicensed, uint8_t power, uint8_t regionCap);

}  // namespace lic
