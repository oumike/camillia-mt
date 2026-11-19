#include "mecp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i18n.h"
#include "mecp_tables.h"

namespace mecp {
namespace {

// ── UTF-8 and the boundary class B (§6.1 rule 4) ─────────────────────────────

// Decodes the code point at s[i] (i < n). Returns its byte length, or 0 for a
// byte that does not start a well-formed sequence.
size_t decodeAt(const char *s, size_t n, size_t i, uint32_t &cp) {
    const uint8_t b = (uint8_t)s[i];
    size_t len;
    if (b < 0x80) { cp = b; return 1; }
    if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; len = 2; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; len = 3; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; len = 4; }
    else return 0;
    if (i + len > n) return 0;
    for (size_t k = 1; k < len; k++) {
        const uint8_t c = (uint8_t)s[i + k];
        if ((c & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (c & 0x3F);
    }
    return len;
}

bool isBoundaryCp(uint32_t cp) {
    if (cp < 0x80) return cp && strchr("\t\n\f\r ,.;:!?'\"()[]{}", (int)cp) != nullptr;
    switch (cp) {
    case 0x00A0: case 0x1680: case 0x202F: case 0x205F: case 0x3000:
    case 0x060C: case 0x061B: case 0x061F: case 0x06D4:
    case 0x00AB: case 0x00BB: case 0x2013: case 0x2014: case 0x2026:
    case 0x3001: case 0x3002: case 0x3010: case 0x3011:
    case 0xFF01: case 0xFF08: case 0xFF09: case 0xFF0C: case 0xFF0E:
    case 0xFF1A: case 0xFF1B: case 0xFF1F:
        return true;
    default:
        return (cp >= 0x2000 && cp <= 0x200A) || (cp >= 0x2018 && cp <= 0x201D) ||
               (cp >= 0x300C && cp <= 0x300F);
    }
}

struct Text {
    const char *s;
    size_t      n;

    char at(size_t i) const { return i < n ? s[i] : '\0'; }
    bool digit(size_t i) const { return i < n && s[i] >= '0' && s[i] <= '9'; }
    bool alpha(size_t i) const {
        return i < n && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'));
    }
    bool ci(size_t i, char lower) const {
        return i < n && (s[i] == lower || s[i] == lower - 'a' + 'A');
    }
    size_t digits(size_t i) const {
        size_t j = i;
        while (digit(j)) j++;
        return j - i;
    }
    // A decoding pattern's token may end at i: end of text or a boundary next.
    bool endOk(size_t i) const {
        if (i >= n) return true;
        uint32_t cp;
        const size_t len = decodeAt(s, n, i, cp);
        return len && isBoundaryCp(cp);
    }
    // A token may start at i: start of text or a boundary just before.
    bool startOk(size_t i) const {
        if (i == 0) return true;
        size_t j = i - 1;
        while (j > 0 && i - j < 4 && ((uint8_t)s[j] & 0xC0) == 0x80) j--;
        uint32_t cp;
        const size_t len = decodeAt(s, n, j, cp);
        return len == i - j && isBoundaryCp(cp);
    }
};

uint32_t toU32(const Text &t, size_t i, size_t len) {
    uint64_t v = 0;
    for (size_t k = 0; k < len; k++) {
        v = v * 10 + (uint64_t)(t.s[i + k] - '0');
        if (v > 0xFFFFFFFFull) return 0xFFFFFFFFu;
    }
    return (uint32_t)v;
}

Span span(size_t start, size_t end) { return Span{(uint16_t)start, (uint16_t)(end - start)}; }

// Finds the first valid match of a pattern, per §6.1 rules 5 and 6. `tryAt(t,
// end)` matches the pattern's token at t the way a backtracking regex would --
// including the boundary after it -- and keeps its captures. `valid()` is the
// pattern's Additional Validation; a match failing it is skipped and the
// search resumes from its end.
template <class Try, class Valid>
bool firstMatch(const Text &t, Try tryAt, Valid valid, size_t &start, size_t &end) {
    for (size_t i = 0; i < t.n; i++) {
        if (!t.startOk(i)) continue;
        size_t e;
        if (!tryAt(i, e)) continue;
        if (valid()) { start = i; end = e; return true; }
        i = e;  // the loop's i++ moves past the boundary that ended it
    }
    return false;
}

const auto kAlways = [] { return true; };

// ── Conventions ──────────────────────────────────────────────────────────────

// pax: 0*([1-9][0-9]*) ?[Pp][Aa][Xx]
bool findPax(const Text &t, Fields &f) {
    size_t ds = 0, dl = 0;
    auto tryAt = [&](size_t i, size_t &e) {
        while (t.at(i) == '0') i++;
        if (!(t.digit(i) && t.at(i) != '0')) return false;
        ds = i;
        dl = t.digits(i);
        size_t p = i + dl;
        if (t.at(p) == ' ') p++;
        if (!(t.ci(p, 'p') && t.ci(p + 1, 'a') && t.ci(p + 2, 'x'))) return false;
        e = p + 3;
        return t.endOk(e);
    };
    size_t s, e;
    if (!firstMatch(t, tryAt, kAlways, s, e)) return false;
    f.hasPax = true;
    f.pax = toU32(t, ds, dl);
    f.paxTok = span(s, e);
    return true;
}

// gps: ([+-]?[0-9]+\.[0-9]+), ?([+-]?[0-9]+\.[0-9]+), -90..90 / -180..180
bool findGps(const Text &t, Fields &f) {
    double lat = 0, lon = 0;
    auto number = [&](size_t &p, double &out) {
        const size_t b = p;
        if (t.at(p) == '+' || t.at(p) == '-') p++;
        size_t d = t.digits(p);
        if (!d) return false;
        p += d;
        if (t.at(p) != '.') return false;
        d = t.digits(++p);
        if (!d) return false;
        p += d;
        char buf[48];
        const size_t len = p - b;
        if (len >= sizeof(buf)) {
            out = 1e9;  // longer than any coordinate: fails validation below
            return true;
        }
        memcpy(buf, t.s + b, len);
        buf[len] = '\0';
        out = strtod(buf, nullptr);
        return true;
    };
    auto tryAt = [&](size_t i, size_t &e) {
        size_t p = i;
        if (!number(p, lat) || t.at(p) != ',') return false;
        p++;
        if (t.at(p) == ' ') p++;
        if (!number(p, lon)) return false;
        e = p;
        return t.endOk(e);
    };
    auto valid = [&] { return lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180; };
    size_t s, e;
    if (!firstMatch(t, tryAt, valid, s, e)) return false;
    f.hasGps = true;
    f.lat = lat;
    f.lon = lon;
    f.gpsTok = span(s, e);
    return true;
}

// timestamp: @([01]?[0-9]|2[0-3]):?([0-5][0-9])
bool findTime(const Text &t, Fields &f) {
    uint8_t hh = 0, mm = 0;
    auto tryAt = [&](size_t i, size_t &e) {
        if (t.at(i) != '@') return false;
        const size_t h = i + 1;
        struct Alt { bool ok; size_t len; };
        const Alt alts[3] = {
            {(t.at(h) == '0' || t.at(h) == '1') && t.digit(h + 1), 2},
            {t.digit(h), 1},
            {t.at(h) == '2' && t.at(h + 1) >= '0' && t.at(h + 1) <= '3', 2},
        };
        for (const Alt &a : alts) {
            if (!a.ok) continue;
            const size_t after = h + a.len;
            const size_t mins[2] = {after + 1, after};
            for (int c = 0; c < 2; c++) {
                if (c == 0 && t.at(after) != ':') continue;
                const size_t m = mins[c];
                if (!(t.at(m) >= '0' && t.at(m) <= '5' && t.digit(m + 1))) continue;
                if (!t.endOk(m + 2)) continue;
                hh = (uint8_t)toU32(t, h, a.len);
                mm = (uint8_t)toU32(t, m, 2);
                e = m + 2;
                return true;
            }
        }
        return false;
    };
    size_t s, e;
    if (!firstMatch(t, tryAt, kAlways, s, e)) return false;
    f.hasTime = true;
    f.hour = hh;
    f.minute = mm;
    f.timeTok = span(s, e);
    return true;
}

// The run of characters from `i` in a set that holds no boundary character. A
// token made of such characters can only end where the run does.
template <class In>
size_t run(const Text &t, size_t i, In in) {
    size_t j = i;
    while (j < t.n && in(t.s[j])) j++;
    return j;
}

bool alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// language: @([A-Za-z]{2,3}(?:[-_][A-Za-z0-9]{2,8})*)
bool findLang(const Text &t, Fields &f) {
    auto tryAt = [&](size_t i, size_t &e) {
        if (t.at(i) != '@') return false;
        e = run(t, i + 1, [](char c) { return alnum(c) || c == '-' || c == '_'; });
        if (!t.endOk(e)) return false;
        size_t p = i + 1;
        for (bool first = true;; first = false) {
            size_t q = p;
            while (q < e && t.s[q] != '-' && t.s[q] != '_') q++;
            const size_t len = q - p;
            if (first) {
                if (len < 2 || len > 3) return false;
                for (size_t k = p; k < q; k++) if (!t.alpha(k)) return false;
            } else if (len < 2 || len > 8) {
                return false;
            }
            if (q == e) return true;
            p = q + 1;
        }
    };
    size_t s, e;
    if (!firstMatch(t, tryAt, kAlways, s, e)) return false;
    f.hasLang = true;
    size_t o = 0;
    for (size_t k = s + 1; k < e && o + 1 < sizeof(f.lang); k++) {
        char c = t.s[k];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '_') c = '-';
        f.lang[o++] = c;
    }
    f.lang[o] = '\0';
    f.langTok = span(s, e);
    return true;
}

// callsign: ~([A-Za-z0-9/]+), at most 12 characters
bool findCallsign(const Text &t, Fields &f) {
    size_t len = 0;
    auto tryAt = [&](size_t i, size_t &e) {
        if (t.at(i) != '~') return false;
        e = run(t, i + 1, [](char c) { return alnum(c) || c == '/'; });
        len = e - (i + 1);
        return len > 0 && t.endOk(e);
    };
    auto valid = [&] { return len <= 12; };
    size_t s, e;
    if (!firstMatch(t, tryAt, valid, s, e)) return false;
    f.hasCallsign = true;
    for (size_t k = 0; k < len; k++) {
        char c = t.s[s + 1 + k];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        f.callsign[k] = c;
    }
    f.callsign[len] = '\0';
    f.callsignTok = span(s, e);
    return true;
}

// reference: #([A-Za-z0-9_-]+)
bool findRef(const Text &t, Fields &f) {
    auto tryAt = [&](size_t i, size_t &e) {
        if (t.at(i) != '#') return false;
        e = run(t, i + 1, [](char c) { return alnum(c) || c == '-' || c == '_'; });
        return e > i + 1 && t.endOk(e);
    };
    size_t s, e;
    if (!firstMatch(t, tryAt, kAlways, s, e)) return false;
    f.hasRef = true;
    f.ref = span(s + 1, e);
    f.refTok = span(s, e);
    return true;
}

// Duration (R03 only), patterns 1-4:
//   (?:[Ee][Tt][Aa] ?)?([0-9]+) ?[Hh] ?([0-9]+) ?[Mm][Ii][Nn]   hours + minutes
//   (?:[Ee][Tt][Aa] ?)?([0-9]+) ?[Hh]                            hours
//   (?:[Ee][Tt][Aa] ?)?([0-9]+) ?[Mm][Ii][Nn]                    minutes
//   (?:[Ee][Tt][Aa] ?)?([0-9]+) ?[Dd]                            days
// and pattern 5, the bare number, first token only:
//   ^(?:[Ee][Tt][Aa] ?)?([0-9]+)([.,][0-9]+)?
enum DurKind { DUR_HM = 0, DUR_H, DUR_MIN, DUR_D, DUR_KINDS };

// Where the number may start after the optional "ETA ", in regex order.
int etaStarts(const Text &t, size_t i, size_t out[3]) {
    int n = 0;
    if (t.ci(i, 'e') && t.ci(i + 1, 't') && t.ci(i + 2, 'a')) {
        if (t.at(i + 3) == ' ') out[n++] = i + 4;
        out[n++] = i + 3;
    }
    out[n++] = i;
    return n;
}

bool minWord(const Text &t, size_t p) { return t.ci(p, 'm') && t.ci(p + 1, 'i') && t.ci(p + 2, 'n'); }

// Skips the optional space of " ?X" when X matches after it.
template <class Pred>
bool optSpace(const Text &t, size_t &p, Pred x) {
    if (t.at(p) == ' ' && x(p + 1)) { p++; return true; }
    return x(p);
}

uint64_t durationMinutes(DurKind k, uint32_t a, uint32_t b) {
    switch (k) {
    case DUR_HM: return (uint64_t)a * 60 + b;
    case DUR_H:  return (uint64_t)a * 60;
    case DUR_MIN: return a;
    case DUR_D:  return (uint64_t)a * 1440;
    default:     return 0;
    }
}

bool findDuration(const Text &t, Fields &f) {
    uint64_t best = 0, cur = 0;
    size_t bestS = 0, bestE = 0;
    bool found = false;
    for (int k = 0; k < DUR_KINDS; k++) {
        const DurKind kind = (DurKind)k;
        auto tryAt = [&](size_t i, size_t &e) {
            size_t starts[3];
            const int ns = etaStarts(t, i, starts);
            for (int si = 0; si < ns; si++) {
                size_t p = starts[si];
                const size_t al = t.digits(p);
                if (!al) continue;
                const uint32_t a = toU32(t, p, al);
                uint32_t b = 0;
                p += al;
                auto letter = [&](char c) { return [&t, c](size_t q) { return t.ci(q, c); }; };
                if (kind == DUR_MIN) {
                    if (!optSpace(t, p, [&](size_t q) { return minWord(t, q); })) continue;
                    p += 3;
                } else {
                    if (!optSpace(t, p, letter(kind == DUR_D ? 'd' : 'h'))) continue;
                    p += 1;
                    if (kind == DUR_HM) {
                        if (!optSpace(t, p, [&](size_t q) { return t.digit(q); })) continue;
                        const size_t bl = t.digits(p);
                        b = toU32(t, p, bl);
                        p += bl;
                        if (!optSpace(t, p, [&](size_t q) { return minWord(t, q); })) continue;
                        p += 3;
                    }
                }
                if (!t.endOk(p)) continue;
                cur = durationMinutes(kind, a, b);
                e = p;
                return true;
            }
            return false;
        };
        size_t s, e;
        if (!firstMatch(t, tryAt, [&] { return cur > 0; }, s, e)) continue;
        if (!found || s < bestS) {
            found = true;
            best = cur;
            bestS = s;
            bestE = e;
        }
    }
    if (!found) {
        size_t starts[3];
        const int ns = etaStarts(t, 0, starts);
        for (int si = 0; si < ns && !found; si++) {
            const size_t p = starts[si];
            const size_t al = t.digits(p);
            if (!al) continue;
            const size_t end = p + al;
            // The optional ([.,][0-9]+) is tried first; with it the match
            // fails validation, which leaves no other first-token match.
            if ((t.at(end) == '.' || t.at(end) == ',') && t.digit(end + 1) &&
                t.endOk(end + 1 + t.digits(end + 1)))
                return false;
            if (!t.endOk(end)) continue;
            best = toU32(t, p, al);
            if (!best) return false;
            found = true;
            bestS = 0;
            bestE = end;
        }
        if (!found) return false;
    }
    f.hasDuration = true;
    f.minutes = best > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)best;
    f.durationTok = span(bestS, bestE);
    return true;
}

// ── Envelope ─────────────────────────────────────────────────────────────────

bool isCode(const char *s, size_t n, size_t i) {
    return i + 3 <= n && s[i] >= 'A' && s[i] <= 'Z' && s[i + 1] >= '0' && s[i + 1] <= '9' &&
           s[i + 2] >= '0' && s[i + 2] <= '9';
}

// \n \f \r U+0085 U+2028 U+2029 at s[i]: never allowed in freetext.
bool lineBreakAt(const char *s, size_t n, size_t i) {
    const uint8_t b = (uint8_t)s[i];
    if (b == '\n' || b == '\f' || b == '\r') return true;
    if (b == 0xC2 && i + 1 < n && (uint8_t)s[i + 1] == 0x85) return true;
    return b == 0xE2 && i + 2 < n && (uint8_t)s[i + 1] == 0x80 &&
           ((uint8_t)s[i + 2] == 0xA8 || (uint8_t)s[i + 2] == 0xA9);
}

bool freetextOk(const char *s, size_t n) {
    if (!n || s[0] == ' ' || s[0] == '\t' || s[n - 1] == ' ' || s[n - 1] == '\t') return false;
    for (size_t i = 0; i < n; i++)
        if (lineBreakAt(s, n, i)) return false;
    return true;
}

// ── Packs ────────────────────────────────────────────────────────────────────

const MecpPack &pack(uint8_t uiLang) {
    for (const MecpPack &p : kMecpPacks)
        if (p.uiLang == uiLang) return p;
    return kMecpPacks[0];
}

int codeIndex(const char *code) {
    for (int i = 0; i < kMecpCodeCount; i++)
        if (!memcmp(kMecpCodes + i * 3, code, 3)) return i;
    return -1;
}

int categoryStart(int category) {
    int i = 0;
    while (i < kMecpCodeCount && kMecpCodes[i * 3] != kMecpCategoryLetters[category]) i++;
    return i;
}

// Appends to a NUL-terminated buffer, cutting at a UTF-8 boundary when full.
struct Out {
    char  *buf;
    size_t cap, len;
    bool   full;

    void add(const char *s, size_t n) {
        if (full || !cap) return;
        if (len + n >= cap) {
            n = cap - 1 - len;
            while (n && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
            full = true;
        }
        memcpy(buf + len, s, n);
        len += n;
        buf[len] = '\0';
    }
    void add(const char *s) { add(s, strlen(s)); }
};

const char kSep[] = " \xC2\xB7 ";  // " · "

void addDuration(Out &o, uint32_t m) {
    char b[40];
    if (m % 1440 == 0) snprintf(b, sizeof(b), "%lu d", (unsigned long)(m / 1440));
    else if (m >= 60 && m % 60) snprintf(b, sizeof(b), "%lu h %lu min", (unsigned long)(m / 60), (unsigned long)(m % 60));
    else if (m >= 60) snprintf(b, sizeof(b), "%lu h", (unsigned long)(m / 60));
    else snprintf(b, sizeof(b), "%lu min", (unsigned long)m);
    o.add(b);
}

} // namespace

bool looksLikeMecp(const char *text) { return text && !strncmp(text, "MECP/", 5); }

bool parse(const char *text, Envelope &out) {
    if (!looksLikeMecp(text)) return false;
    size_t n = strlen(text);
    if (n >= 2 && text[n - 2] == '\r' && text[n - 1] == '\n') n -= 2;
    else if (n && (text[n - 1] == '\n' || text[n - 1] == '\r')) n--;
    if (n < 10 || text[5] < '0' || text[5] > '3' || text[6] != '/' || !isCode(text, n, 7)) return false;

    size_t end = 10;  // greedy: every " X00" that follows
    while (end < n && text[end] == ' ' && isCode(text, n, end + 1)) end += 4;
    // Backtrack a code at a time, as the regex would, until the rest is valid.
    for (; end >= 10; end -= 4) {
        const bool rest = end == n || (text[end] == ' ' && freetextOk(text + end + 1, n - end - 1));
        if (rest) {
            out.severity = (uint8_t)(text[5] - '0');
            out.codes = text + 7;
            out.codesLen = end - 7;
            out.freetext = end == n ? text + n : text + end + 1;
            out.freetextLen = end == n ? 0 : n - end - 1;
            return true;
        }
        if (end == 10) break;
    }
    return false;
}

int codeCount(const Envelope &env) { return (int)((env.codesLen + 1) / 4); }

bool codeAt(const Envelope &env, int i, char out[4]) {
    if (i < 0 || i >= codeCount(env)) return false;
    memcpy(out, env.codes + i * 4, 3);
    out[3] = '\0';
    return true;
}

bool hasCode(const Envelope &env, const char *code) {
    for (int i = 0, n = codeCount(env); i < n; i++)
        if (!memcmp(env.codes + i * 4, code, 3)) return true;
    return false;
}

void extract(const Envelope &env, Fields &f) {
    memset(&f, 0, sizeof(f));
    const Text t{env.freetext, env.freetextLen > 0xFFFF ? 0xFFFF : env.freetextLen};
    findPax(t, f);
    findGps(t, f);
    findTime(t, f);
    findLang(t, f);
    findCallsign(t, f);
    findRef(t, f);
    if (hasCode(env, "R03")) findDuration(t, f);
}

const char *severityName(uint8_t severity, uint8_t uiLang) {
    return severity < kSeverityCount ? pack(uiLang).severity[severity] : "?";
}

const char *codeLabel(const char *code, uint8_t uiLang) {
    const int i = code ? codeIndex(code) : -1;
    return i < 0 ? nullptr : pack(uiLang).code[i];
}

int categoryCount() { return kMecpCategoryCount; }

char categoryLetter(int category) {
    return category >= 0 && category < kMecpCategoryCount ? kMecpCategoryLetters[category] : '?';
}

const char *categoryName(int category, uint8_t uiLang) {
    return category >= 0 && category < kMecpCategoryCount ? pack(uiLang).category[category] : "";
}

int categoryCodeCount(int category) {
    if (category < 0 || category >= kMecpCategoryCount) return 0;
    const int s = categoryStart(category);
    int i = s;
    while (i < kMecpCodeCount && kMecpCodes[i * 3] == kMecpCategoryLetters[category]) i++;
    return i - s;
}

bool categoryCode(int category, int i, char out[4]) {
    if (i < 0 || i >= categoryCodeCount(category)) return false;
    memcpy(out, kMecpCodes + (categoryStart(category) + i) * 3, 3);
    out[3] = '\0';
    return true;
}

const char *packCode(uint8_t uiLang) { return pack(uiLang).packCode; }

const char *uiText(UiText key, uint8_t uiLang) {
    return key < kMecpUiCount ? pack(uiLang).ui[key] : "";
}

bool render(const char *text, uint8_t uiLang, char *buf, size_t bufSize) {
    Envelope env;
    if (!parse(text, env)) return false;
    Out o{buf, bufSize, 0, false};
    if (bufSize) buf[0] = '\0';
    o.add(severityName(env.severity, uiLang));
    o.add(":");
    for (int i = 0, n = codeCount(env); i < n; i++) {
        char code[4];
        codeAt(env, i, code);
        const char *label = codeLabel(code, uiLang);
        o.add(i ? kSep : " ");
        o.add(label ? label : code);
    }
    if (!env.freetextLen) return true;
    o.add(kSep);

    Fields f;
    extract(env, f);
    struct Rep { bool on; Span tok; int kind; };
    Rep reps[] = {
        {f.hasPax, f.paxTok, 0},           {f.hasTime, f.timeTok, 1},
        {f.hasLang, f.langTok, 2},         {f.hasCallsign, f.callsignTok, 3},
        {f.hasDuration, f.durationTok, 4},
    };
    const int nReps = (int)(sizeof(reps) / sizeof(reps[0]));
    size_t pos = 0;
    while (true) {
        // The next replacement by position; overlapping ones are dropped.
        int next = -1;
        for (int i = 0; i < nReps; i++)
            if (reps[i].on && reps[i].tok.start >= pos &&
                (next < 0 || reps[i].tok.start < reps[next].tok.start))
                next = i;
        if (next < 0) break;
        Rep &r = reps[next];
        r.on = false;
        o.add(env.freetext + pos, r.tok.start - pos);
        char b[48];
        switch (r.kind) {
        case 0:
            snprintf(b, sizeof(b), ": %lu", (unsigned long)f.pax);
            o.add(uiText(UI_PERSON_COUNT, uiLang));
            o.add(b);
            break;
        case 1:
            snprintf(b, sizeof(b), "%02u:%02u", f.hour, f.minute);
            o.add(b);
            break;
        case 2:
            o.add("[");
            o.add(f.lang);
            o.add("]");
            break;
        case 3:
            o.add(f.callsign);
            break;
        case 4:
            addDuration(o, f.minutes);
            break;
        }
        pos = r.tok.start + r.tok.len;
    }
    o.add(env.freetext + pos, env.freetextLen - pos);
    return true;
}

namespace {

// Code points an encoder must not emit (§6.1 rule 8 and the envelope's line
// breaks); each becomes an ASCII space.
bool isSpaceLike(uint32_t cp) {
    return cp < 0x20 || cp == 0x7F || cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
           cp == 0x205F || cp == 0x3000 || cp == 0xFEFF;
}

// Cleans `text` into out: one ASCII space for every run of spacing, ends
// trimmed, malformed UTF-8 dropped.
size_t cleanText(const char *text, char *out, size_t cap) {
    size_t o = 0;
    bool space = false;
    const size_t n = text ? strlen(text) : 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t len = decodeAt(text, n, i, cp);
        if (!len) { i++; continue; }
        if (cp == ' ' || isSpaceLike(cp)) {
            space = o > 0;
        } else {
            if (o + (space ? 1 : 0) + len >= cap) break;
            if (space) out[o++] = ' ';
            space = false;
            memcpy(out + o, text + i, len);
            o += len;
        }
        i += len;
    }
    out[o] = '\0';
    return o;
}

} // namespace

size_t encode(const Draft &d, const char *text, uint8_t uiLang, char *out, size_t outSize) {
    if (!out || outSize <= kMaxWireBytes) return 0;
    out[0] = '\0';
    if (d.severity >= kSeverityCount || !d.codeCount || d.codeCount > kMaxDraftCodes) return 0;
    if (d.hasGps && !(d.lat >= -90 && d.lat <= 90 && d.lon >= -180 && d.lon <= 180)) return 0;
    if (d.hasTime && (d.hour > 23 || d.minute > 59)) return 0;

    char head[kMaxWireBytes * 2];
    int n = snprintf(head, sizeof(head), "MECP/%u/", d.severity);
    for (int i = 0; i < d.codeCount; i++) {
        if (!isCode(d.codes[i], 3, 0)) return 0;
        n += snprintf(head + n, sizeof(head) - n, "%s%.3s", i ? " " : "", d.codes[i]);
    }
    if (d.pax) n += snprintf(head + n, sizeof(head) - n, " %lupax", (unsigned long)d.pax);
    if (d.hasGps) n += snprintf(head + n, sizeof(head) - n, " %.5f,%.5f", d.lat, d.lon);
    if (d.hasTime) n += snprintf(head + n, sizeof(head) - n, " @%02u%02u", d.hour, d.minute);
    if (n <= 0 || (size_t)n > kMaxWireBytes) return 0;
    memcpy(out, head, (size_t)n + 1);

    char clean[kMaxWireBytes + 1];
    if (!cleanText(text, clean, sizeof(clean))) return (size_t)n;
    // The language hint goes before the text, so a text that starts with a
    // code-shaped word ("A12") can never run on into the code section.
    char hint[16];
    const int hl = snprintf(hint, sizeof(hint), " @%s ", packCode(uiLang));
    const size_t room = kMaxWireBytes - (size_t)n;
    if ((size_t)hl >= room) return (size_t)n;
    size_t tl = strlen(clean);
    if (tl > room - (size_t)hl) {
        tl = room - (size_t)hl;
        while (tl && ((uint8_t)clean[tl] & 0xC0) == 0x80) tl--;
        while (tl && clean[tl - 1] == ' ') tl--;
        if (!tl) return (size_t)n;
    }
    memcpy(out + n, hint, (size_t)hl);
    memcpy(out + n + hl, clean, tl);
    out[n + hl + tl] = '\0';
    return (size_t)n + (size_t)hl + tl;
}

} // namespace mecp
