#include "live_util.h"
#include "node_db.h"
#include <ctype.h>
#include <time.h>

bool liveShortNameUsable(const char *shortName) {
    if (!shortName || !shortName[0]) return false;
    bool q = (shortName[0] == '?' && shortName[1] == '?' &&
              shortName[2] == '?' && shortName[3] == '?' && shortName[4] == '\0');
    bool d = (shortName[0] == '-' && shortName[1] == '-' &&
              shortName[2] == '-' && shortName[3] == '-' && shortName[4] == '\0');
    return !(q || d);
}

// Mirrored from RhinoConfig::clockFormat by the main loop; see live_util.h.
static bool s_clock12Hour = false;
void liveClockSet12Hour(bool use12Hour) { s_clock12Hour = use12Hour; }
bool liveClockIs12Hour() { return s_clock12Hour; }

void liveFormatClock(const struct tm &localTm, char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (!s_clock12Hour) {
        snprintf(out, outLen, "%02d:%02d", localTm.tm_hour, localTm.tm_min);
        return;
    }
    // 0 and 12 both read as 12 on a 12-hour face: midnight is 12 AM, noon 12 PM.
    int hour12 = localTm.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(out, outLen, "%d:%02d %s", hour12, localTm.tm_min,
             (localTm.tm_hour < 12) ? "AM" : "PM");
}

const char *liveFindClock(const char *line, int window, const char **start) {
    if (start) *start = nullptr;
    if (!line) return nullptr;
    for (const char *p = line; *p && (int)(p - line) < window; p++) {
        // The hour is one or two digits: 24-hour is always padded, 12-hour is
        // not, so the minute is located relative to the colon rather than by a
        // fixed offset.
        const char *m = p;
        if (!isdigit((unsigned char)m[0])) continue;
        if (isdigit((unsigned char)m[1])) m++;
        if (m[1] != ':' || !isdigit((unsigned char)m[2]) || !isdigit((unsigned char)m[3])) continue;
        const char *end = m + 4;
        // The meridiem counts only when a separator follows it. Without that
        // test "09:15 AMBER alert" reads as a 12-hour clock and loses the first
        // three letters of its body -- the clock is always the start of a line
        // or a prefix, so something has to follow it.
        if (end[0] == ' ' && (end[1] == 'A' || end[1] == 'P') && end[2] == 'M'
            && (end[3] == ' ' || end[3] == '\0')) {
            end += 3;
        }
        if (start) *start = p;
        return end;
    }
    return nullptr;
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
    char clock[LIVE_CLOCK_BUF];
    liveFormatClock(localTm, clock, sizeof(clock));
    snprintf(out, outLen, "%s ", clock);
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
