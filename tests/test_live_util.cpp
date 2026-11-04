// test-cflags: -Ihost -DDEVICE_TDECK
#include "../src/live_util.h"
#include "../src/node_db.h"
#include "test_support.h"

#include <array>

// Only the node lookup is replaced; formatting and fallback policy are real.
NodeDB Nodes;
static NodeEntry knownNode = {};
NodeEntry *NodeDB::find(uint32_t id) { return id == knownNode.nodeId ? &knownNode : nullptr; }

static void testClockFormats() {
    const struct { int hour; int minute; const char *h24; const char *h12; } cases[] = {
        {0, 0, "00:00", "12:00 AM"}, {1, 5, "01:05", "1:05 AM"},
        {11, 59, "11:59", "11:59 AM"}, {12, 0, "12:00", "12:00 PM"},
        {13, 9, "13:09", "1:09 PM"}, {23, 59, "23:59", "11:59 PM"},
    };
    for (const auto &c : cases) {
        tm local = {};
        local.tm_hour = c.hour;
        local.tm_min = c.minute;
        for (bool h12 : {false, true}) {
            liveClockSet12Hour(h12);
            char out[LIVE_CLOCK_BUF];
            liveFormatClock(local, out, sizeof(out));
            CHECK(strcmp(out, h12 ? c.h12 : c.h24) == 0);
        }
    }
}

static void testClockParsing() {
    const struct { const char *line; int window; int start; int end; } cases[] = {
        {"09:15 hello", 1, 0, 5}, {"9:15 AM hello", 1, 0, 7},
        {"12:00 PM", 1, 0, 8}, {"rx 23:59 body", 4, 3, 8},
        {"rx 23:59 body", 3, -1, -1}, {"09:15 AMBER alert", 1, 0, 5},
        {"09:15 PM2", 1, 0, 5}, {"--:-- body", 8, -1, -1},
        {"9:1", 1, -1, -1}, {"9:", 1, -1, -1}, {"9", 1, -1, -1},
        {"", 1, -1, -1}, {"01:23", 0, -1, -1}, {"01:23", -1, -1, -1},
    };
    for (bool h12 : {false, true}) {
        liveClockSet12Hour(h12);
        for (const auto &c : cases) {
            const char *start = c.line;
            const char *end = liveFindClock(c.line, c.window, &start);
            if (c.start < 0) CHECK(start == nullptr && end == nullptr);
            else CHECK(start == c.line + c.start && end == c.line + c.end);
        }
    }
    const char *start = "old";
    CHECK(liveFindClock(nullptr, 8, &start) == nullptr && start == nullptr);
    const char text[] = "09:15";
    CHECK(liveFindClock(text, 1, nullptr) == text + 5);
}

static void testLabels() {
    knownNode.nodeId = 0x1234ABCD;
    strcpy(knownNode.shortName, "ABCD");
    char out[16];
    liveNodeLabel(knownNode.nodeId, out, sizeof(out));
    CHECK(strcmp(out, "ABCD") == 0);
    liveNodeLabel(0x76543210, out, sizeof(out));
    CHECK(strcmp(out, "!76543210") == 0);
    for (const char *name : {"", "????", "----"}) {
        strcpy(knownNode.shortName, name);
        liveNodeLabel(knownNode.nodeId, out, sizeof(out));
        CHECK(strcmp(out, "!1234ABCD") == 0);
    }
    liveNodeLabel(UINT32_MAX, out, sizeof(out), true);
    CHECK(strcmp(out, "BCAST") == 0);
    liveNodeLabel(UINT32_MAX, out, sizeof(out), false);
    CHECK(strcmp(out, "!FFFFFFFF") == 0);
    liveNodeLabelWithHint(knownNode.nodeId, "HINT", out, sizeof(out));
    CHECK(strcmp(out, "HINT") == 0);
    liveNodeLabelWithHint(knownNode.nodeId, "????", out, sizeof(out));
    CHECK(strcmp(out, "!1234ABCD") == 0);
    liveNodeLabelWithHint(UINT32_MAX, nullptr, out, sizeof(out), true);
    CHECK(strcmp(out, "BCAST") == 0);
    CHECK(liveShortNameUsable("?") && liveShortNameUsable("---"));
    CHECK(!liveShortNameUsable(nullptr));
}

static void testSmallOutputBuffers() {
    tm local = {};
    local.tm_hour = 23;
    local.tm_min = 59;
    liveClockSet12Hour(true);
    for (size_t cap = 0; cap <= 9; cap++) {
        std::array<char, 12> out;
        out.fill('!');
        liveFormatClock(local, out.data() + 1, cap);
        CHECK(out[0] == '!' && out[cap + 1] == '!');
        if (cap) {
            CHECK(out[cap] == '\0');
            CHECK(strncmp(out.data() + 1, "11:59 PM", cap - 1) == 0);
        }
    }
    char out[4] = "old";
    liveNodeLabel(0x12345678, out, sizeof(out));
    CHECK(strcmp(out, "!12") == 0);
    liveFormatClock(local, nullptr, 0);
    liveNodeLabel(1, nullptr, 0);
    liveNodeLabelWithHint(1, "hint", nullptr, 0);
}

int main() {
    testClockFormats();
    testClockParsing();
    testLabels();
    testSmallOutputBuffers();
    return test::report();
}
