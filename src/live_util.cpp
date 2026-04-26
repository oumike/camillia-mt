#include "live_util.h"
#include "node_db.h"
#include <time.h>

bool liveShortNameUsable(const char *shortName) {
    if (!shortName || !shortName[0]) return false;
    bool q = (shortName[0] == '?' && shortName[1] == '?' &&
              shortName[2] == '?' && shortName[3] == '?' && shortName[4] == '\0');
    bool d = (shortName[0] == '-' && shortName[1] == '-' &&
              shortName[2] == '-' && shortName[3] == '-' && shortName[4] == '\0');
    return !(q || d);
}

void liveBuildPrefix(char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    time_t nowEpoch = time(nullptr);
    if (nowEpoch < 1700000000) {
        snprintf(out, outLen, "--:-- ");
        return;
    }

    struct tm localTm;
    localtime_r(&nowEpoch, &localTm);
    snprintf(out, outLen, "%02d:%02d ", localTm.tm_hour, localTm.tm_min);
}

void liveNodeLabel(uint32_t nodeId, char *out, size_t outLen, bool useBroadcastLabel) {
    if (!out || outLen == 0) return;

    if (useBroadcastLabel && nodeId == 0xFFFFFFFF) {
        snprintf(out, outLen, "BCAST");
        return;
    }

    NodeEntry *n = Nodes.find(nodeId);
    if (n && liveShortNameUsable(n->shortName)) {
        snprintf(out, outLen, "%s", n->shortName);
    } else {
        snprintf(out, outLen, "!%08X", nodeId);
    }
}

void liveNodeLabelWithHint(uint32_t nodeId,
                           const char *hintShort,
                           char *out,
                           size_t outLen,
                           bool useBroadcastLabel) {
    if (!out || outLen == 0) return;

    if (liveShortNameUsable(hintShort)) {
        snprintf(out, outLen, "%s", hintShort);
        return;
    }

    liveNodeLabel(nodeId, out, outLen, useBroadcastLabel);
}
