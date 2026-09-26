#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#if I18N_ENABLED
#include "i18n_builtin.h"   // generated from lang/*.lang by tools/gen_lang_builtin.py
#endif

const char *const kUiLangCodes[LANG_COUNT] = { "en", "es", "fr", "ro", "it" };
const char *const kUiLangNames[LANG_COUNT] = {
    "English", "Espa\xC3\xB1ol", "Fran\xC3\xA7" "ais", "Rom\xC3\xA2n\xC4\x83", "Italiano",
};

static uint8_t s_lang = LANG_EN;

void i18nSetLang(uint8_t lang) {
#if I18N_ENABLED
    s_lang = (lang < LANG_COUNT) ? lang : LANG_EN;
#else
    (void)lang;   // English-only build: a stored choice is kept but not applied
    s_lang = LANG_EN;
#endif
}
uint8_t i18nGetLang() { return s_lang; }

uint8_t i18nLangFromCode(const char *code) {
    if (!code) return LANG_EN;
    for (uint8_t i = 0; i < LANG_COUNT; i++) {
        if (strcasecmp(code, kUiLangCodes[i]) == 0) return i;
    }
    return LANG_EN;
}

#if I18N_ENABLED

// ── Format-string guard ──────────────────────────────────────────────────────
// Ported from wadamesh (its issue #258), where it went in after a Hungarian row
// swapped "%s ... %lu" round and crashed every device that drew it. Callers use
// TR()'s result as a printf format, so a translation decides how many varargs
// are read and how each is typed. This compares the ordered list of
// (length modifier, conversion) pairs -- exactly what fixes each vararg's type,
// with '*' width/precision counting as an int -- and on any mismatch the caller
// gets the English, which is always in step with the call site.

// Advances *p past the next conversion and writes its signature into out.
// "%%" is a literal percent, not a conversion. False when there are no more.
static bool nextSpec(const char **p, char *out, size_t cap) {
    const char *s = *p;
    for (;;) {
        while (*s && *s != '%') ++s;
        if (!*s) { *p = s; return false; }
        ++s;
        if (*s == '%') { ++s; continue; }
        if (!*s) { *p = s; return false; }
        size_t n = 0;
        while (*s == '-' || *s == '+' || *s == ' ' || *s == '#' || *s == '0') ++s;
        if (*s == '*') { ++s; if (n + 1 < cap) out[n++] = '*'; }
        else while (*s >= '0' && *s <= '9') ++s;
        if (*s == '.') {
            ++s;
            if (*s == '*') { ++s; if (n + 1 < cap) out[n++] = '*'; }
            else while (*s >= '0' && *s <= '9') ++s;
        }
        while (*s == 'h' || *s == 'l' || *s == 'j' || *s == 'z' || *s == 't' || *s == 'L') {
            if (n + 1 < cap) out[n++] = *s;
            ++s;
        }
        if (!*s) { *p = s; return false; }
        if (n + 1 < cap) out[n++] = *s;
        ++s;
        out[n < cap ? n : cap - 1] = '\0';
        *p = s;
        return true;
    }
}

static bool formatSpecsMatch(const char *a, const char *b) {
    const char *pa = a;
    const char *pb = b;
    for (;;) {
        char sa[8], sb[8];
        const bool ha = nextSpec(&pa, sa, sizeof(sa));
        const bool hb = nextSpec(&pb, sb, sizeof(sb));
        if (ha != hb) return false;
        if (!ha) return true;
        if (strcmp(sa, sb) != 0) return false;
    }
}

static const char *lookup(const I18nPair *tab, int count, const char *key) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = strcmp(key, tab[mid].key);
        if (c == 0) return tab[mid].val;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return nullptr;
}

// ── Pseudo-localisation (issue #99, phase 3) ────────────────────────────────
// Build with -DI18N_PSEUDO=1 and every TR() string comes back as a stand-in for
// a translation: bracketed, accented and about 30% longer --
//   "Save"  ->  "[Sávé···]"
// That shows, on every board and without anyone reading Spanish, where a longer
// translation will be cut off (the closing bracket goes missing) or collide,
// and where accents clip. printf conversions and a leading icon are copied
// through untouched, so formatted strings still format. Language is ignored.
#ifndef I18N_PSEUDO
#define I18N_PSEUDO 0
#endif

#if I18N_PSEUDO
static const char *pseudoLocalise(const char *en) {
    static char ring[8][512];
    static uint8_t next = 0;
    char *out = ring[next];
    next = (uint8_t)((next + 1) % 8);
    const size_t cap = sizeof(ring[0]);
    size_t n = 0;
    auto put = [&](const char *bytes, size_t len) {
        if (n + len + 1 < cap) { memcpy(out + n, bytes, len); n += len; }
    };

    const char *p = en;
    // A leading icon glyph (and its spaces) stays outside the brackets.
    while (((uint8_t)p[0] == 0xEE || (uint8_t)p[0] == 0xEF) && p[1] && p[2]) {
        put(p, 3);
        p += 3;
    }
    while (*p == ' ') { put(p, 1); ++p; }

    put("[", 1);
    size_t letters = 0;
    while (*p) {
        if (*p == '%') {
            // Copy a whole conversion ("%-3.1f", "%lu", "%%") verbatim.
            const char *q = p + 1;
            if (*q == '%') { put(p, 2); p += 2; continue; }
            while (*q && strchr("-+ #0123456789.*hljztL", *q)) ++q;
            if (*q) ++q;
            put(p, (size_t)(q - p));
            p = q;
            continue;
        }
        const char *acc = nullptr;
        switch (*p) {
            case 'a': acc = "\xC3\xA1"; break;  case 'A': acc = "\xC3\x81"; break;
            case 'e': acc = "\xC3\xA9"; break;  case 'E': acc = "\xC3\x89"; break;
            case 'i': acc = "\xC3\xAD"; break;  case 'I': acc = "\xC3\x8D"; break;
            case 'o': acc = "\xC3\xB3"; break;  case 'O': acc = "\xC3\x93"; break;
            case 'u': acc = "\xC3\xBA"; break;  case 'U': acc = "\xC3\x9A"; break;
            case 'n': acc = "\xC3\xB1"; break;  case 'N': acc = "\xC3\x91"; break;
            case 'c': acc = "\xC3\xA7"; break;  case 'C': acc = "\xC3\x87"; break;
            default: break;
        }
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) ++letters;
        if (acc) put(acc, 2); else put(p, 1);
        ++p;
    }
    // Middle dots (U+00B7) for the extra length, ~30% of the letters.
    for (size_t i = 0; i < (letters * 3 + 9) / 10; i++) put("\xC2\xB7", 2);
    put("]", 1);
    out[n] = '\0';
    return out;
}
#endif

const char *TR(const char *en) {
    if (!en) return "";
#if I18N_PSEUDO
    return pseudoLocalise(en);
#endif
    if (s_lang == LANG_EN) return en;
    const I18nPair *tab = kBuiltinLang[s_lang];
    const int count = kBuiltinLangCount[s_lang];
    if (!tab || count <= 0) return en;

    // An icon in front of the words ("<symbol>  Save"): the symbol is a 3-byte
    // private-use UTF-8 sequence (lead byte 0xEE/0xEF), and the .lang files are
    // keyed by the words alone. wadamesh found every such label silently
    // staying English until it split them like this. The glyph and its spaces
    // are put back in front of the translation in a small ring of buffers.
    const char *text = en;
    while (((uint8_t)text[0] == 0xEE || (uint8_t)text[0] == 0xEF) && text[1] && text[2]) {
        text += 3;
    }
    if (text != en) {
        while (*text == ' ') ++text;
        if (!*text) return en;   // all icon, nothing to translate
    }

    const char *val = lookup(tab, count, text);
    if (!val || !formatSpecsMatch(text, val)) return en;
    if (text == en) return val;

    static char ring[4][160];
    static uint8_t next = 0;
    char *buf = ring[next];
    next = (uint8_t)((next + 1) % 4);
    snprintf(buf, sizeof(ring[0]), "%.*s%s", (int)(text - en), en, val);
    return buf;
}

#endif  // I18N_ENABLED
