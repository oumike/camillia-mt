// Host tests for the licensed (amateur) mode decision module.
#include "../src/licensed_mode.h"
#include "test_support.h"

#include <string>

static bool norm(const char *in, std::string &out) {
    char buf[32];
    strcpy(buf, "untouched");
    const bool ok = lic::normalizeCallSign(in, buf, sizeof(buf));
    out = buf;
    return ok;
}

static void test_normalize_call_sign() {
    std::string out;
    CHECK(norm(" w1aw ", out) && out == "W1AW");
    CHECK(norm("VE3/W1AW", out) && out == "VE3/W1AW");
    CHECK(!norm("", out) && out == "untouched");
    CHECK(!norm("   ", out) && out == "untouched");
    CHECK(!norm("W1 AW", out) && out == "untouched");
    CHECK(!norm("W1AW!", out) && out == "untouched");
    CHECK(!norm("ABCDEFGHIJKLMNOP", out) && out == "untouched");
    CHECK(norm("ABCDEFGHIJKLMNO", out) && out == "ABCDEFGHIJKLMNO");
}

static void test_is_no_call() {
    CHECK(lic::isNoCall("N0CALL"));
    CHECK(lic::isNoCall("n0call"));
    CHECK(!lic::isNoCall("N0CAL"));
}

static void test_compose_long_name() {
    char out[40];
    CHECK(lic::composeLongName("W1AW", "", out, sizeof(out)) == 4 && strcmp(out, "W1AW") == 0);
    CHECK(lic::composeLongName("W1AW", nullptr, out, sizeof(out)) == 4 && strcmp(out, "W1AW") == 0);
    CHECK(lic::composeLongName("W1AW", "Base", out, sizeof(out)) == 10 &&
          strcmp(out, "W1AW//Base") == 0);
}

static void test_compose_long_name_utf8_edge() {
    std::string suffix;
    for (int i = 0; i < 40; i++) suffix += "\xC3\xA9";
    char out[40];
    const size_t len = lic::composeLongName("W1AW", suffix.c_str(), out, sizeof(out));
    CHECK(len <= 39);
    CHECK(len == strlen(out));
    CHECK(len > 0 && (uint8_t)out[len - 1] != 0xC3);
    CHECK(strncmp(out, "W1AW//", 6) == 0);
}

static void test_packet_gate_table() {
    const lic::PeerStatus all[] = {lic::PEER_UNKNOWN, lic::PEER_LICENSED, lic::PEER_UNLICENSED};
    for (lic::PeerStatus from : all) {
        for (lic::PeerStatus to : all) {
            const bool clean = from != lic::PEER_UNLICENSED && to != lic::PEER_UNLICENSED;
            CHECK(lic::allowPacket(true, from, to) == clean);
            CHECK(lic::allowPacket(false, from, to));
        }
    }
}

static void test_allow_relay() {
    CHECK(lic::allowRelay(false, false, lic::PEER_UNKNOWN, lic::PEER_UNKNOWN));
    CHECK(!lic::allowRelay(true, false, lic::PEER_UNKNOWN, lic::PEER_UNKNOWN));
    CHECK(lic::allowRelay(true, true, lic::PEER_UNKNOWN, lic::PEER_UNKNOWN));
    CHECK(!lic::allowRelay(true, true, lic::PEER_UNLICENSED, lic::PEER_UNKNOWN));
}

static void test_should_sign() {
    CHECK(lic::shouldSign(true, true, 1));
    CHECK(lic::shouldSign(false, true, 1));
    CHECK(!lic::shouldSign(false, false, 1));
    CHECK(lic::shouldSign(true, false, 1));
    CHECK(!lic::shouldSign(true, false, 5));
    CHECK(!lic::shouldSign(true, false, 70));
    CHECK(!lic::shouldSign(true, true, 70));    // never a traceroute, even broadcast
    CHECK(!lic::shouldSign(false, true, 70));
}

static void test_accept_nodeinfo() {
    CHECK(lic::acceptNodeInfo(true, true));
    CHECK(lic::acceptNodeInfo(false, false));
    CHECK(!lic::acceptNodeInfo(true, false));
    CHECK(!lic::acceptNodeInfo(false, true));
}

static void test_region_selectable() {
    CHECK(lic::regionSelectable(false, 902.0f, false));
    CHECK(!lic::regionSelectable(true, 420.0f, false));
    CHECK(lic::regionSelectable(true, 420.0f, true));
    CHECK(!lic::regionSelectable(true, 144.0f, true));
    CHECK(lic::regionSelectable(false, 2400.0f, false));
}

static void test_power_after_change() {
    // Leaving licensed mode clamps to the region cap.
    CHECK(lic::powerAfterChange(true, false, 22, 13) == 13);
    CHECK(lic::powerAfterChange(true, false, 10, 13) == 10);
    // Staying licensed, or never licensed, leaves power alone.
    CHECK(lic::powerAfterChange(true, true, 22, 13) == 22);
    CHECK(lic::powerAfterChange(false, false, 22, 13) == 22);
    CHECK(lic::powerAfterChange(false, true, 22, 13) == 22);
}

static void test_active_switch() {
    lic::setActive(true);
    CHECK(lic::active());
    lic::setActive(false);
    CHECK(!lic::active());
}

int main() {
    test_normalize_call_sign();
    test_is_no_call();
    test_compose_long_name();
    test_compose_long_name_utf8_edge();
    test_packet_gate_table();
    test_allow_relay();
    test_should_sign();
    test_accept_nodeinfo();
    test_region_selectable();
    test_active_switch();
    test_power_after_change();
    return test::report();
}
