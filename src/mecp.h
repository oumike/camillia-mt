#pragma once
// MECP -- Mesh Emergency Communication Protocol, v1.4.0
// (https://github.com/mecp-org/MECP, protocol CC0).
//
//   MECP/<severity 0-3>/<codes> [freetext]      e.g. MECP/0/M01 M02 3pax @1430
//
// Decoding follows the brief's §6 to the letter: the envelope, then each §6.1
// freetext convention found by its decoding pattern between boundaries, with
// the normative validation and resume-after-failure rules. No std::regex: the
// patterns are matched by hand, which tests/test_mecp.cpp holds to the
// protocol's own conformance vectors.
//
// Arduino-free, so it builds for the host tests and the Linux app unchanged.
#include <stddef.h>
#include <stdint.h>

namespace mecp {

// Encoders must stay within this; decoders accept longer messages.
static constexpr size_t kMaxWireBytes = 200;
static constexpr int    kSeverityCount = 4;
static constexpr int    kMaxDraftCodes = 8;

enum Severity : uint8_t { SEV_MAYDAY = 0, SEV_URGENT = 1, SEV_SAFETY = 2, SEV_ROUTINE = 3 };

// A parsed envelope. The pointers point into the text passed to parse().
struct Envelope {
    uint8_t     severity;
    const char *codes;        // "M01 M02", codesLen bytes
    size_t      codesLen;
    const char *freetext;     // freetextLen bytes; may be empty
    size_t      freetextLen;
};

// A byte range of the freetext holding one convention's token (no boundaries).
struct Span {
    uint16_t start;
    uint16_t len;
};

// The §6.1 convention values found in a message's freetext.
struct Fields {
    bool     hasPax;      uint32_t pax;        Span paxTok;
    bool     hasGps;      double   lat, lon;   Span gpsTok;
    bool     hasTime;     uint8_t  hour, minute; Span timeTok;
    bool     hasLang;     char     lang[24];   Span langTok;      // lowercase, '-'
    bool     hasCallsign; char     callsign[13]; Span callsignTok; // uppercase
    bool     hasRef;      Span     ref;        Span refTok;       // ref: the tag, no '#'
    bool     hasDuration; uint32_t minutes;    Span durationTok;  // R03 only
};

// True when `text` (NUL-terminated) is an MECP message. At most one trailing
// \n, \r or \r\n is ignored, as the brief requires.
bool parse(const char *text, Envelope &out);
// Cheap prefilter: does `text` start with "MECP/"?
bool looksLikeMecp(const char *text);

int  codeCount(const Envelope &env);
// The i-th code into out[4] ("M01"); false past the end.
bool codeAt(const Envelope &env, int i, char out[4]);
bool hasCode(const Envelope &env, const char *code);

void extract(const Envelope &env, Fields &out);

// ── Localised text ───────────────────────────────────────────────────────────
// `uiLang` is a UiLang (src/i18n.h); a language without an MECP pack, or a
// build without I18N_ENABLED, gets English.
const char *severityName(uint8_t severity, uint8_t uiLang);
// nullptr for a well-formed code that is not in the table.
const char *codeLabel(const char *code, uint8_t uiLang);
int         categoryCount();
char        categoryLetter(int category);
const char *categoryName(int category, uint8_t uiLang);
int         categoryCodeCount(int category);
// The i-th code of a category into out[4].
bool        categoryCode(int category, int i, char out[4]);
// The MECP pack code for a UI language ("en", "no"), for the language hint.
const char *packCode(uint8_t uiLang);

enum UiText : uint8_t { UI_PERSON_COUNT = 0, UI_BEACON_ACTIVE, UI_BEACON_DRILL, UI_UNKNOWN_CODE };
const char *uiText(UiText key, uint8_t uiLang);

// The decoded, localised form of an MECP message:
//   "MAYDAY: Injury · Unconscious person · Person count: 3 @ 14:30"
// Codes are listed by name (an unknown code as itself); each convention token
// in the freetext is replaced by its readable form, boundaries kept. Truncates
// at a UTF-8 boundary to fit. False, with `out` untouched, when `text` is not
// MECP.
bool render(const char *text, uint8_t uiLang, char *out, size_t outSize);

// ── Encoding ─────────────────────────────────────────────────────────────────
struct Draft {
    uint8_t  severity;
    uint8_t  codeCount;
    char     codes[kMaxDraftCodes][4];
    uint32_t pax;                 // 0: none
    bool     hasGps;  double lat, lon;
    bool     hasTime; uint8_t hour, minute;
};

// Builds the wire text: codes, then pax, GPS, time, the language hint (only
// with text) and `text`. `text` is cleaned -- line breaks, tabs and Unicode
// spaces become one ASCII space, ends trimmed -- and cut at a UTF-8 boundary
// so the whole message is at most kMaxWireBytes. Returns the length, or 0 when
// the draft itself is invalid or does not fit. outSize must exceed
// kMaxWireBytes.
size_t encode(const Draft &draft, const char *text, uint8_t uiLang, char *out, size_t outSize);

} // namespace mecp
