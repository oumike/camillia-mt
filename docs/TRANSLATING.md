# Translating Camillia

The on-device UI is translated with `TR("English text")` lookups (issue #99). The English text
is the key; each language is one file in `lang/`, compiled into the firmware on every build.
Anything without a translation shows in English — never blank.

Languages today: **English** (the keys), **Spanish** (`es`), **French** (`fr`),
**Romanian** (`ro`), **Italian** (`it`). All are first drafts and want review by native speakers.

## The file format

`lang/<code>.lang`, UTF-8. Lines starting with `#` are comments. Every other line is:

```
English exactly as in the source<TAB>Translation
```

Escape a newline as `\n`, a tab as `\t`, a backslash as `\\`. Order does not matter (the build
sorts), but the files are kept sorted by the English for review.

## Rules the device enforces

- **Placeholders must match.** Every `%s`, `%d`, `%u`, `%lu`, `%.1f`… in the English must appear
  in the translation, **in the same order and of the same type**. A line that breaks this is
  ignored on the device and the English is shown (a reordered `%s`/`%d` would otherwise crash
  it). `%%` is a literal percent sign.
  - To drop a value the English shows, use `%.0s` for a `%s` — it reads the value and prints
    nothing. Used where the English adds a plural ending: `Channel blink: %u flash%s` becomes
    `Parpadeo de canal: %u destellos%.0s`.
- **Key names stay as printed on the keys**: `Enter`, `Bksp`, `Backspace`, `Esc`, `Space`, `Tab`,
  `j/k`, `Wheel`. Translate the action after the `=`, not the key before it.
- **Shortcut letters stay visible.** `(T)raceroute` means the T key does it. Keep the letter in
  brackets where the translated word contains it (`(F)avorito`); otherwise add it at the end
  (`Enviar MD (D)` for `Sen(d) DM`).
- **Names stay as they are**: Camillia, Meshtastic, MQTT, LoRa, WiFi, GPS, BT, VNC, OTA, SNR, RSSI,
  ChUtil, AirTx, LOS, S&F / Store&Fwd, preset names (LongFast…).
- **Characters**: the fonts carry Latin-1, Latin Extended-A and Romanian ș ț (comma below) — every
  letter these languages use. Curly quotes and the ellipsis character are not in them; write `'`
  and `...`.
- **Length**: screens are as narrow as 240 px. Spanish runs 20-30% longer than English as a rule;
  much more than that on a short label is likely to be cut off. The audit lists them.

## Glossary

| English | es | fr | ro | it |
| --- | --- | --- | --- | --- |
| node | nodo | nœud | nod | nodo |
| channel | canal | canal | canal | canale |
| DM (direct message) | MD | MP | MD | MD |
| preset | preajuste | préréglage | presetare | preset |
| beacon | baliza | balise | baliză | beacon |
| sweep | barrido | balayage | baleiere | sweep |
| weather | tiempo | météo | vremea | meteo |
| settings | configuración | configuration | configurare | configurazione |
| rebooting | reiniciando | redémarrage | repornire | riavvio |

## Checking a translation

```bash
python3 tools/i18n_audit.py            # coverage per language
python3 tools/i18n_audit.py --missing  # what is still English
python3 tools/i18n_audit.py --long     # translations likely to be cut off
```

CI runs the audit and fails on a placeholder mismatch.

To see a language on a device: pick it in Config → Language, or put `language: es` under
`config: display:` in an exported config YAML and import it.

To find every place a longer translation would be cut off without speaking the language, build
with pseudo-localisation — every string becomes bracketed, accented and ~30% longer:

```bash
PLATFORMIO_BUILD_FLAGS=-DI18N_PSEUDO=1 pio run -e <env> -t upload
```

A label whose closing `]` is missing is being cut off.

## Adding a language

1. Append it to `enum UiLang` in `src/i18n.h` — **append only**, the index is stored in settings.
2. Add its code and its own name to `kUiLangCodes` / `kUiLangNames` in `src/i18n.cpp`, and the code
   to `LANGS` in `tools/gen_lang_builtin.py`, in the same order.
3. Create `lang/<code>.lang` with the header of an existing one.
4. If it needs letters outside Latin-1 / Extended-A, add their range to `tools/gen_latin_fonts.sh`
   and regenerate the fonts.

## Making new UI text translatable

Wrap English UI literals in `TR()` where they are drawn: `lv_label_set_text(lbl, TR("Save"))`.
For arrays filled before the language is known, mark each entry `TR_NOOP("...")` and call `TR()`
on it where it is drawn. Never pass `TR()` user data (names, messages). Then add the new keys to
each `.lang` file.

Wrap the text where it is *written*, not only where it is drawn: a message built with
`snprintf(msg, ..., TR("..."), ...)` or assigned (`why = TR("...")`) and shown later needs its
`TR()` at that point. A long key may be split over adjacent literals (`TR("line one\n" "line two")`);
the key is the joined text. A literal cannot span an `#if`, so give each branch its own `TR()`.
The OTA progress screen and the boot splash draw with ASCII-only fonts and stay in English.
