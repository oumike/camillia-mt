#pragma once
#include <Arduino.h>
#include <time.h>

// Returns true when a short name is usable for UI/log display.
bool liveShortNameUsable(const char *shortName);

// ── Clock format ─────────────────────────────────────────────────────────────
// RhinoConfig::clockFormat mirrored down here by the main loop, so every module
// that prints a time can reach the preference without reaching into the config
// object. Same arrangement as nodeArchiveSetEnabled() in node_db.
void liveClockSet12Hour(bool use12Hour);
bool liveClockIs12Hour();

// Smallest buffer that always holds a formatted clock plus its terminator:
// "12:34 PM" is eight characters, and "--:--" is shorter than that.
#define LIVE_CLOCK_BUF 12

// Write a local time as the user has asked to read it: "14:32" in 24-hour mode,
// "2:32 PM" in 12-hour. The 12-hour form is not zero-padded, the way a clock is
// normally written; callers that need a fixed column must measure, not count.
// out must be at least LIVE_CLOCK_BUF bytes.
void liveFormatClock(const struct tm &localTm, char *out, size_t outLen);

// Locate the clock at the head of a stored feed or chat line, in either format.
// Both have to be recognised and not only the one currently selected: a line is
// written with the format that was in force when it arrived, so a transcript
// spanning a settings change carries both.
//
// Scans the first `window` byte positions for a starting digit, since a line may
// be preceded by a transport icon; pass 1 to anchor at the first byte. Returns
// the byte just past the clock — its " AM"/" PM" included, because callers need
// to move that with the clock rather than leave it in the body — and stores the
// clock's first byte in *start (which may be null). Returns null when the line
// carries no clock, the "--:--" placeholder included: that is not a time, and
// the two callers that care about it handle it themselves.
const char *liveFindClock(const char *line, int window, const char **start);

// Build wall-clock prefix with a trailing space (e.g. "14:32 " or "2:32 PM ").
// If the device clock is not set yet, returns "--:-- ".
// out should be at least LIVE_CLOCK_BUF bytes.
void liveBuildPrefix(char *out, size_t outLen);

// Format a node label using NodeDB short name when possible, otherwise !<nodeId>.
// If useBroadcastLabel is true and nodeId is broadcast, returns "BCAST".
void liveNodeLabel(uint32_t nodeId, char *out, size_t outLen, bool useBroadcastLabel = false);

// Like liveNodeLabel, but prefers a provided short-name hint when usable.
void liveNodeLabelWithHint(uint32_t nodeId,
                           const char *hintShort,
                           char *out,
                           size_t outLen,
                           bool useBroadcastLabel = false);
