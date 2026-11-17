// SPDX-License-Identifier: GPL-3.0-or-later
// Host-side checks for the CardputerZero daemon's parsers: NMEA, the settings
// file, the message store and the socket line format. Built with the daemon
// (linux/build.sh runs it in the build container); needs no hardware.
#include "common/ipc.h"
#include "daemon/gps.h"
#include "daemon/settings.h"
#include "daemon/store.h"
#include "mesh_channel_plan.h"
#include "mesh_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

uint8_t myPubKey[32];
uint8_t myPrivKey[32];
uint8_t myDeviceRole = 0;

static int gFailures = 0;
#define CHECK(c) \
    do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); gFailures++; } } while (0)

static std::string tempPath(const char *name) {
    static std::string dir;
    if (dir.empty()) {
        char tmpl[] = "/tmp/czero-test-XXXXXX";
        dir = mkdtemp(tmpl);
    }
    return dir + "/" + name;
}

static void writeFile(const std::string &path, const char *text) {
    FILE *f = fopen(path.c_str(), "w");
    fputs(text, f);
    fclose(f);
}

static void testNmea() {
    CHECK(nmeaChecksumOk("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
    CHECK(!nmeaChecksumOk("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48"));
    CHECK(!nmeaChecksumOk("GPGGA,123519*47"));

    Gps g;
    const char fix[] = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    g.feed(fix, sizeof fix - 1);
    CHECK(g.fix().valid);
    CHECK(g.fix().latI == 481173000);    // 48 + 7.038/60
    CHECK(g.fix().lonI == 115166667);    // 11 + 31/60
    CHECK(g.fix().altM == 545);
    CHECK(g.fix().sats == 8);
    CHECK(g.service());                  // reports the new fix once
    CHECK(!g.service());

    // Southern/western hemispheres, split across two reads.
    Gps h;
    std::string s = "$GNGGA,000000,3351.000,S,15112.000,W,1,05,1.0,10.0,M,0,M,,*";
    uint8_t sum = 0;
    for (size_t i = 1; s[i] != '*'; i++) sum ^= (uint8_t)s[i];
    char tail[8];
    snprintf(tail, sizeof tail, "%02X\n", sum);
    s += tail;
    h.feed(s.data(), 20);
    h.feed(s.data() + 20, s.size() - 20);
    CHECK(h.fix().valid && h.fix().latI == -338500000 && h.fix().lonI == -1512000000);

    // An RMC with status V (void) drops the fix.
    std::string rmc = "$GPRMC,000001,V,,,,,,,,,,N*";
    sum = 0;
    for (size_t i = 1; rmc[i] != '*'; i++) sum ^= (uint8_t)rmc[i];
    snprintf(tail, sizeof tail, "%02X\n", sum);
    rmc += tail;
    h.feed(rmc.data(), rmc.size());
    CHECK(!h.fix().valid);
}

static void testSettings() {
    std::string path = tempPath("config");
    Settings d = loadSettings(path);          // missing: defaults, file written
    CHECK(access(path.c_str(), R_OK) == 0);
    CHECK(d.region == "US" && d.preset == PRESET_LONG_FAST && d.role == 0 && !d.rxOnly);
    Settings again = loadSettings(path);       // the written default parses cleanly
    CHECK(again.region == "US" && again.preset == PRESET_LONG_FAST && again.channels.empty());

    writeFile(path,
              "region = eu_868\n"
              "preset = Medium Fast\n"
              "tx_power = 22\n"
              "role = CLIENT_MUTE\n"
              "rx_only = yes\n"
              "hop_limit = 9\n"                // out of range: kept at default
              "channel = Friends 1PG7OiApB1nwvP+rz05pAQ==\n"
              "channel = Open\n"
              "bogus = 1\n");
    Settings s = loadSettings(path);
    CHECK(s.region == "EU_868");
    CHECK(s.preset == PRESET_MEDIUM_FAST);
    CHECK(s.txPower <= regionPower("EU_868"));
    CHECK(s.role == 1 && s.rxOnly && s.hopLimit == 3);
    CHECK(s.channels.size() == 2);
    CHECK(s.channels[0].name == "Friends" && s.channels[0].keyLen == 16);
    CHECK(s.channels[1].name == "Open" && s.channels[1].keyLen == 1 && s.channels[1].key[0] == 1);

    applyChannels(s);
    CHECK(!strcmp(CHANNEL_KEYS[0].name, "Friends") && CHANNEL_KEYS[0].role == 0);
    CHECK(CHANNEL_KEYS[1].role == 1 && CHANNEL_KEYS[2].role == 2);
    CHECK(CHANNEL_KEYS[0].hash == channelKeyHash(CHANNEL_KEYS[0]));

    // No channels: the preset's own public channel, as a stock node has.
    writeFile(path, "preset = ShortFast\n");
    applyChannels(loadSettings(path));
    CHECK(!strcmp(CHANNEL_KEYS[0].name, "ShortFast") && channelKeyIsPublic(CHANNEL_KEYS[0]));
}

static void testStore() {
    std::string path = tempPath("messages");
    const uint32_t me = 0x11223344;
    {
        MessageStore st;
        st.load(path);
        Msg in;
        in.id = 1; in.conv = "c0"; in.from = 0xaabbccdd; in.to = 0xffffffff;
        in.text = "tab\there\nline \\ slash";
        st.add(in);
        Msg out;
        out.id = 2; out.conv = "c0"; out.from = me; out.to = 0xffffffff; out.state = 's';
        st.add(out);
        Msg dm;
        dm.id = 3; dm.conv = "daabbccdd"; dm.from = me; dm.to = 0xaabbccdd; dm.state = 'p';
        st.add(dm);
        CHECK(st.setState(me, 2, 'R'));
        CHECK(!st.setState(me, 2, 'R'));          // no change
        CHECK(!st.setState(me, 1, 'A'));          // not ours
        CHECK(!st.setState(me, 3, 'R'));          // a relay sighting is not a DM's ACK
    }
    MessageStore st;
    st.load(path);
    CHECK(st.all().size() == 3);
    CHECK(st.all()[0].text == "tab\there\nline \\ slash");
    CHECK(st.all()[1].state == 'R');
    CHECK(st.all()[2].state == 'F');              // waiting when we stopped: failed
    CHECK(st.setState(me, 3, 'A'));               // a late ACK still counts
    CHECK(!st.setState(me, 3, 'F'));              // and is final
}

static void testIpc() {
    std::string text = "a\tb\\c\nd";
    auto f = ipc::split(ipc::join(std::string("T"), std::string("c0"), ipc::esc(text)));
    CHECK(f.size() == 3 && f[0] == "T" && f[1] == "c0" && f[2] == text);
    CHECK(ipc::split("E").size() == 1);
    CHECK(ipc::hex32(0xab) == "000000ab");
}

int main() {
    testNmea();
    testSettings();
    testStore();
    testIpc();
    if (gFailures) {
        fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("camillia-czero-tests: all passed\n");
    return 0;
}
