#pragma once
#include <Arduino.h>

// Returns true when a short name is usable for UI/log display.
bool liveShortNameUsable(const char *shortName);

// Build wall-clock prefix in HH:MM form with trailing space (e.g. "14:32 ").
// If the device clock is not set yet, returns "--:-- ".
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
