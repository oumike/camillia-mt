#pragma once
#include <stdint.h>

// Builds with no room for the translations (the Cardputer's 8 MB flash) set
// -DI18N_ENABLED=0: TR() becomes a pass-through, no language tables are linked,
// the Language setting is hidden and lv_conf.h falls back to LVGL's own ASCII
// fonts. Everything else in this header still exists, so callers need no #if.
#ifndef I18N_ENABLED
#define I18N_ENABLED 1
#endif

// ── UI translations (issue #99) ──────────────────────────────────────────────
// Gettext-style, after wadamesh: wrap an English UI literal in TR("...") and it
// comes back in the active language, or as the English itself when there is no
// translation -- so an untranslated string shows English, never a blank. The
// English text is the key; there are no string ids to keep in step.
//
// Translations live in lang/<code>.lang (one "English<TAB>translation" per line)
// and are compiled in by tools/gen_lang_builtin.py as sorted pairs.
//
// APPEND ONLY. The index is stored in RhinoConfig::uiLanguage, so inserting a
// language mid-enum would move every stored choice. Keep kUiLangCodes,
// kUiLangNames and LANGS in tools/gen_lang_builtin.py in this same order.
enum UiLang : uint8_t {
    LANG_EN = 0,
    LANG_ES = 1,
    LANG_FR = 2,
    LANG_RO = 3,
    LANG_IT = 4,
    LANG_COUNT
};

// "en", "es", ... -- the .lang file name and the YAML `language:` value.
extern const char *const kUiLangCodes[LANG_COUNT];
// Each language's name for itself ("English", "Español"), for a picker.
extern const char *const kUiLangNames[LANG_COUNT];

void    i18nSetLang(uint8_t lang);   // an out-of-range value means English
uint8_t i18nGetLang();
// The index for a code, case-insensitive; English for anything unknown.
uint8_t i18nLangFromCode(const char *code);

// The active language's text for an English UI string, or `en` itself.
//
// Safe to hand straight to snprintf as a format: a translation whose printf
// conversions differ from the English key's -- in number, order or type --
// is refused and the English returned, because a reordered "%s ... %d" would
// have snprintf read an int as a pointer.
//
// The returned pointer is either `en`, a compiled-in string, or (for a label
// with an icon glyph in front) one of a few rotating static buffers. Use it
// right away -- lv_label_set_text() and snprintf() copy -- rather than keeping it.
#if I18N_ENABLED
const char *TR(const char *en);
#else
static inline const char *TR(const char *en) { return en ? en : ""; }
#endif

// Marks an English literal as a translation key without translating it: for
// arrays and initialisers that are filled before a language is known, whose
// entries are passed through TR() where they are drawn. It is the literal
// itself -- the marker exists so tools/i18n_audit.py can find the key.
#define TR_NOOP(s) (s)
