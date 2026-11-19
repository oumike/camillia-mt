// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/mecp.h"
#include "../src/i18n.h"
#include "mecp_vectors.h"
#include "test_support.h"

#include <math.h>
#include <string>

// The protocol's own conformance pack (MECP freetext/tests.json): every input
// must parse, or not, and decode to exactly the expected convention values.
static void testConformanceVectors() {
    int ran = 0;
    for (const MecpVector &v : kMecpVectors) {
        ran++;
        mecp::Envelope env;
        const bool ok = mecp::parse(v.input, env);
        if (ok != v.valid) {
            printf("  vector: %s\n", v.title);
            CHECK(ok == v.valid);
            continue;
        }
        if (!ok) continue;
        mecp::Fields f;
        mecp::extract(env, f);
        char ts[8] = "";
        if (f.hasTime) snprintf(ts, sizeof(ts), "%02u:%02u", f.hour, f.minute);
        const std::string ref = f.hasRef ? std::string(env.freetext + f.ref.start, f.ref.len) : "";
        const bool pass =
            f.hasTime == (v.timestamp != nullptr) && (!v.timestamp || !strcmp(ts, v.timestamp)) &&
            f.hasRef == (v.reference != nullptr) && (!v.reference || ref == v.reference) &&
            f.hasPax == (v.pax >= 0) && (v.pax < 0 || (long)f.pax == v.pax) &&
            f.hasGps == v.hasGps &&
            (!v.hasGps || (fabs(f.lat - v.lat) < 1e-9 && fabs(f.lon - v.lon) < 1e-9)) &&
            f.hasCallsign == (v.callsign != nullptr) && (!v.callsign || !strcmp(f.callsign, v.callsign)) &&
            f.hasLang == (v.language != nullptr) && (!v.language || !strcmp(f.lang, v.language)) &&
            f.hasDuration == (v.duration >= 0) && (v.duration < 0 || (long)f.minutes == v.duration);
        if (!pass) printf("  vector: %s (%s)\n", v.title, v.input);
        CHECK(pass);
    }
    CHECK(ran == (int)(sizeof(kMecpVectors) / sizeof(kMecpVectors[0])));
    CHECK(ran >= 70);
}

static void testEnvelope() {
    mecp::Envelope env;
    // Greedy codes, given back one at a time when the rest would be invalid.
    CHECK(mecp::parse("MECP/1/M01 M02  X", env));
    CHECK(mecp::codeCount(env) == 1);
    CHECK(env.freetextLen == 6 && !memcmp(env.freetext, "M02  X", 6));
    CHECK(mecp::parse("MECP/1/T05 T05 damaged", env) && mecp::codeCount(env) == 2);
    CHECK(mecp::parse("MECP/1/M01 M02X", env) && mecp::codeCount(env) == 1);
    CHECK(!mecp::parse("MECP/1/M01 ", env));
    CHECK(!mecp::parse("MECP/1/M01 M02 ", env));
    CHECK(!mecp::parse("MECP/4/M01", env));
    CHECK(!mecp::parse("MECP/1/m01", env));
    CHECK(!mecp::parse("MECP/1/M01 a\nb", env));
    CHECK(!mecp::parse("MECP/1/M01 a\xE2\x80\xA8" "b", env));
    CHECK(mecp::parse("MECP/1/M01 a\tb", env));
    CHECK(mecp::parse("MECP/1/M01\r\n", env) && env.freetextLen == 0);
    CHECK(!mecp::parse("MECP/1/M01\n\n", env));
    CHECK(mecp::parse("MECP/2/Z99", env) && mecp::codeLabel("Z99", LANG_EN) == nullptr);
}

static void testRender() {
    char out[256];
    CHECK(mecp::render("MECP/0/M01 M02 3pax @1430 trapped", LANG_EN, out, sizeof(out)));
    CHECK(!strcmp(out, "MAYDAY: Injury \xC2\xB7 Unconscious person \xC2\xB7 "
                       "Person count: 3 14:30 trapped"));
    // A boundary is kept; an unknown code shows as itself.
    CHECK(mecp::render("MECP/2/R03 Z99 (ETA 1h30min)", LANG_EN, out, sizeof(out)));
    CHECK(!strcmp(out, "SAFETY: ETA [time] \xC2\xB7 Z99 \xC2\xB7 (1 h 30 min)"));
    CHECK(mecp::render("MECP/3/L20 ~vk2/ok1abc/p @DE", LANG_DE, out, sizeof(out)));
    CHECK(strstr(out, "VK2/OK1ABC/P [de]") != nullptr);
    CHECK(strncmp(out, "ROUTINE", 7) != 0 || !strcmp(mecp::severityName(3, LANG_DE), "ROUTINE"));
    // No pack for Romanian: English.
    CHECK(!strcmp(mecp::severityName(0, LANG_RO), mecp::severityName(0, LANG_EN)));
    CHECK(!mecp::render("hello", LANG_EN, out, sizeof(out)));

    // Truncation lands on a UTF-8 boundary.
    char small[12];
    CHECK(mecp::render("MECP/0/M01", LANG_EN, small, sizeof(small)));
    CHECK(strlen(small) < sizeof(small));
    CHECK(mecp::render("MECP/3/L01 \xC3\xA9\xC3\xA9\xC3\xA9", LANG_EN, out, 1));
    CHECK(out[0] == '\0');
}

static void testEncode() {
    mecp::Draft d = {};
    d.severity = mecp::SEV_MAYDAY;
    d.codeCount = 2;
    strcpy(d.codes[0], "M01");
    strcpy(d.codes[1], "P05");
    d.pax = 3;
    d.hasGps = true;
    d.lat = -41.2865;
    d.lon = 174.7762;
    d.hasTime = true;
    d.hour = 6;
    d.minute = 5;
    char out[mecp::kMaxWireBytes + 1];
    size_t n = mecp::encode(d, "  need\r\nhelp\xC2\xA0now ", LANG_EN, out, sizeof(out));
    CHECK(n == strlen(out));
    CHECK(!strcmp(out, "MECP/0/M01 P05 3pax -41.28650,174.77620 @0605 @en need help now"));
    mecp::Envelope env;
    mecp::Fields f;
    CHECK(mecp::parse(out, env));
    mecp::extract(env, f);
    CHECK(f.hasPax && f.pax == 3 && f.hasGps && f.hasTime && f.hour == 6 && f.minute == 5);
    CHECK(f.hasLang && !strcmp(f.lang, "en"));

    // A code-shaped first word stays freetext behind the language hint.
    d = {};
    d.severity = 3;
    d.codeCount = 1;
    strcpy(d.codes[0], "L01");
    n = mecp::encode(d, "A12 ok", LANG_NB, out, sizeof(out));
    CHECK(!strcmp(out, "MECP/3/L01 @no A12 ok"));
    CHECK(mecp::parse(out, env) && mecp::codeCount(env) == 1);
    CHECK(mecp::encode(d, "", LANG_EN, out, sizeof(out)) == 10 && !strcmp(out, "MECP/3/L01"));

    // Long text is cut to 200 bytes at a character boundary, never mid-é.
    std::string longText;
    for (int i = 0; i < 150; i++) longText += "\xC3\xA9";
    n = mecp::encode(d, longText.c_str(), LANG_FR, out, sizeof(out));
    CHECK(n <= mecp::kMaxWireBytes && n >= mecp::kMaxWireBytes - 1);
    CHECK(mecp::parse(out, env));
    CHECK(((uint8_t)out[n - 1] & 0xC0) == 0x80 && (uint8_t)out[n - 2] == 0xC3);

    // Invalid drafts.
    d.severity = 4;
    CHECK(mecp::encode(d, "", LANG_EN, out, sizeof(out)) == 0);
    d.severity = 0;
    d.codeCount = 0;
    CHECK(mecp::encode(d, "", LANG_EN, out, sizeof(out)) == 0);
    d.codeCount = 1;
    d.hasGps = true;
    d.lat = 91;
    CHECK(mecp::encode(d, "", LANG_EN, out, sizeof(out)) == 0);
    char tooSmall[10];
    d.hasGps = false;
    CHECK(mecp::encode(d, "", LANG_EN, tooSmall, sizeof(tooSmall)) == 0);
}

static void testTables() {
    int total = 0;
    for (int c = 0; c < mecp::categoryCount(); c++) {
        CHECK(mecp::categoryName(c, LANG_EN)[0] != '\0');
        for (int i = 0; i < mecp::categoryCodeCount(c); i++) {
            char code[4];
            CHECK(mecp::categoryCode(c, i, code) && code[0] == mecp::categoryLetter(c));
            CHECK(mecp::codeLabel(code, LANG_PL) != nullptr);
            total++;
        }
    }
    CHECK(total == 109);
    CHECK(!strcmp(mecp::codeLabel("M01", LANG_EN), "Injury"));
    CHECK(!strcmp(mecp::packCode(LANG_NB), "no"));
    CHECK(!strcmp(mecp::packCode(LANG_FI), "en"));
}

int main() {
    testConformanceVectors();
    testEnvelope();
    testRender();
    testEncode();
    testTables();
    return test::report();
}
