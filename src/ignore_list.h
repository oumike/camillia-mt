#pragma once
// User-managed list of node IDs whose chat/DM traffic should be suppressed.
// Ignored nodes still appear in the Nodes list (so the user can un-ignore
// them) and their NODEINFO/telemetry updates still populate NodeDB — only
// TEXT messages are filtered from the on-device chat views and DM stack.
#include <Arduino.h>

class IgnoreList {
public:
    static constexpr int kMax = 64;

    // Load persisted IDs from Preferences (namespace "camillia", key "ignoreIds").
    void init();

    bool     contains(uint32_t nodeId) const;
    bool     add(uint32_t nodeId);      // returns true if list changed
    bool     remove(uint32_t nodeId);   // returns true if list changed
    int      count() const { return _count; }
    uint32_t at(int i) const;

    // Bulk replacement, used by config YAML import. Persists once at the end.
    void replace(const uint32_t *ids, int n);
    void clear();

private:
    uint32_t _ids[kMax] = {};
    int      _count = 0;
    void     _persist() const;
};

extern IgnoreList Ignored;
