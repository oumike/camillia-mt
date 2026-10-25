# Web Config Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the full web config a new frame (side rail / phone tab bar), an always-open Config pane with a sticky save bar, and colours taken from the device's own theme.

**Architecture:**
- The page stays server-rendered HTML streamed from flash constants in `src/web_config.cpp`.
- Static CSS and shell JS move to a new header (`src/web_app_assets.h`); the CSS is served as a cached `/app.css`.
- Theme colours come from a small Arduino-free module (`src/web_theme.*`) that turns the device palette into a `:root{…}` block, filled by an accessor in `main_lvgl.cpp`.
- Section navigation is built in JS from the `.cfg-sec` elements on the page.

**Tech Stack:** ESP32 Arduino `WebServer`, C++17, vanilla CSS/JS (no frameworks), host tests via `tests/run.sh` (system compiler), Node 22 for `node --check`.

**Spec:** `docs/superpowers/specs/2026-10-25-web-config-refresh-design.md`

## Global Constraints

- No PlatformIO builds and no flashing until the user allows it. Host tests (`tests/run.sh`), the preview harness (system `c++`) and `node --check` are allowed: they compile nothing for a device.
- Routes, form field `name`s, POST handlers, the single `/save` form and its confirm text stay byte-for-byte the same.
- The lite page is untouched: `kLiteHead`, `/setup`, and the `lite` branch of `section()`. Every markup change inside the config pane must still read sensibly with no CSS.
- Wide/narrow breakpoint: 720px.
- Hash format: `#<tab>` or `#config/<slug>`. Tabs: `config`, `utils`, `live`, `chat`, `map`, `remote`, `files`.
- Section slugs: identity, device, position, lora, channels, mqtt, display, notify, power, modules, wifi, firmware, nodes, access.
- `/app.css` is served with `Cache-Control: public, max-age=31536000, immutable`, needs no login, and is linked as `/app.css?v=` + `APP_VERSION`.
- Fonts: one Google Fonts link for IBM Plex Sans 400;600, IBM Plex Mono 400, Martian Mono 500, `display=swap`, system fallbacks. Plex Mono only for data, never labels.
- Contrast guard threshold: 4.5:1 for `textMain` on `bgMain`.
- Motion: only the save bar and the sheets animate, both off under `prefers-reduced-motion`.
- Static text stays in flash (`static const char[]`, sent with `sendFlash`). No new multi-KB `String` appends.
- Copy: sentence case, plain verbs; the save button reads "Save and restart", the counter "N unsaved change(s)", the discard button "Discard".

## Review Focus

1. **Lite page (AP mode, Cardputer):** it must render exactly as before. Pinned in Task 6 (diff of the lite branch) and Task 7 (no class-only meaning in pane markup).
2. **Controls that scripts add or reveal after load:** channel rows, custom LoRa fields, theme picker inputs. These must not show as "changed" on load, and edits to them must count. Pinned in Task 4.
3. **The redirect after save:** it lands on `/` with no hash. The user must come back to the same tab and section. Pinned in Task 3.
4. **Light themes and the e-ink palette:** chips, danger buttons, focus rings and the save bar must stay visible on white. Pinned in Task 2 (preview in `eink`) and Task 8.
5. **Browsers without `:has()` or `IntersectionObserver`:** fields fall back to a stacked label-over-input layout, with no JS errors. Pinned in Task 2 (an `@supports` fallback) and Task 3 (a feature check).

---

## File structure

- `src/web_theme.h` / `src/web_theme.cpp` (new): pure palette → CSS vars, plus the contrast check. No Arduino.
- `src/web_app_assets.h` (new): `kAppCss[]` and `kAppJs[]`, the static CSS and shell JS.
- `tests/test_web_theme.cpp` (new): host test for `web_theme`.
- `tools/web_preview/preview.cpp`, `tools/web_preview/sample_body.html` and `tools/web_preview/run.sh` (new): build a static preview page from the real assets and theme code.
- `src/web_config.h`: declares the accessor.
- `src/main_lvgl.cpp`: defines the accessor next to `applyUiThemePalette()`.
- `src/web_config.cpp`: head split, `/app.css` route, page frame, `section()`, `switchTab()`, and inline-style cleanup.

---

### Task 1: Theme module

**Files:**
- Create: `src/web_theme.h`, `src/web_theme.cpp`
- Test: `tests/test_web_theme.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct WebThemeColors {   // all RGB565, as in UiPalette
      uint16_t bg, panel, panelAlt, panelStrong, divider, dividerHi, inputBg,
               accent, text, textDim, onAccent, good, warn, bad;
      bool isLight;
  };
  double webThemeContrast(uint16_t a, uint16_t b);          // WCAG ratio, 1..21
  size_t webThemeCssVars(const WebThemeColors &c, char *out, size_t outLen);
  ```
- `webThemeCssVars` writes `:root{color-scheme:dark|light;--bg:#rrggbb;--panel:…;--panel-2:…;--panel-3:…;--line:…;--line-hi:…;--input:…;--accent:…;--text:…;--dim:…;--on-accent:…;--good:…;--warn:…;--bad:…}`, mapping the fields in that order. It returns the length written, or 0 if `outLen` is too small (and then writes nothing). If `webThemeContrast(text,bg) < 4.5` it substitutes Camillia's text pair for the mode:
  - dark: text `#f4f6fb`, dim `#b0b8c8`
  - light: text `#2a1d24`, dim `#76626d`

- [ ] **Step 1: Write the failing tests** in `tests/test_web_theme.cpp` (same `CHECK` macro style as `test_key_pin.cpp`):
  - `testContrastExtremes`: `webThemeContrast(0x0000,0xFFFF)` is within 0.1 of 21.0; `webThemeContrast(0x1234,0x1234)` equals 1.0.
  - `testCamilliaDark`: bg 0x0843, accent 0xDA8E, text 0xFFFF, isLight=false. The output contains `color-scheme:dark`, `--bg:#080818` and `--accent:#de5073`, and starts with `:root{`.
  - `testLightScheme`: isLight=true gives `color-scheme:light`.
  - `testContrastGuard`: bg 0x8410, text 0x8430 (near-equal grey), isLight=false. The output contains `--text:#f4f6fb` and `--dim:#b0b8c8`.
  - `testTooSmall`: a buffer of 16 returns 0 and leaves `out[0]` unchanged.
- [ ] **Step 2: Run** `tests/run.sh`. Expected: `test_web_theme BUILD FAILED` (no `web_theme.h`).
- [ ] **Step 3: Implement** `src/web_theme.{h,cpp}`. RGB565→888 uses the same widening as `rgb565To888()` in `web_config.cpp` (`*255/31`, `*255/63`); contrast uses sRGB relative luminance. `snprintf` into a stack buffer of 512, then copy if it fits.
- [ ] **Step 4: Run** `tests/run.sh`. Expected: every line passes, including `test_web_theme`.
- [ ] **Step 5: Commit** `git add src/web_theme.* tests/test_web_theme.cpp && git commit -m "Web config: theme colours to CSS variables"`

### Task 2: Stylesheet and preview harness

**Files:**
- Create: `src/web_app_assets.h` (`kAppCss[]` now, `kAppJs[]` in Task 3), `tools/web_preview/preview.cpp`, `tools/web_preview/sample_body.html`, `tools/web_preview/run.sh`

**Interfaces:**
- Consumes: `webThemeCssVars`, `WebThemeColors` (Task 1).
- Produces:
  - `static const char kAppCss[]` and `static const char kAppJs[]` in `web_app_assets.h`, header-only with no includes.
  - Class contract used by Tasks 3–8:
    - frame: `.app`, `.app-hd`, `.app-rail`, `.app-main`, `.tabbar`, `.sheet` + `.sheet.open` + `.scrim`, `.savebar` + `.savebar.show`
    - nav items: `[data-tab]` items with `.active`, section links `.sec-link` + `.here` + `.dirty`
    - content: `.cfg-sec` (with `h2` and `p.sec-desc`), `.cfg-sub`, `.hint`, `.chip` (+ `.chip-good`, `.chip-warn`, `.chip-bad`), `.btn`, `.btn-2`, `.btn-danger`, `.notice`, `.notice-err`, `.changed`
  - Old classes still emitted elsewhere stay styled, using the new variables: `.modal-*`, `.chooser-btn`, `.map-*`, `.node-*`, `.chat-*`, `.live-*`, `.chart-*`, `.ch-*`, `.remote-*`, `.row2`, `.gps-note`.
  - `tools/web_preview/run.sh <theme>`, where theme is `camD`, `evrL`, `solD` or `eink`, writes `.superpowers/preview/<theme>.html`.

- [ ] **Step 1: Write `kAppCss`.** Port every rule in the current `kHead` `<style>` (`web_config.cpp:654-815`) onto the variables from Task 1, plus the frame and component classes above. Values from the spec:
  - The rail is sticky and 200px wide at ≥720px. Below that the rail is hidden and `.tabbar` is fixed at the bottom.
  - `.savebar` sits above `.tabbar` on narrow screens.
  - Field rows: `.cfg-sec label:has(>input:not([type=checkbox],[type=hidden]),>select,>textarea)` is a two-column grid (`minmax(9em,38%) 1fr`) at ≥720px and stacked below. `@supports not selector(:has(*))` keeps them stacked.
  - Checkbox toggle skin: `appearance:none`, 34×20px, accent colour when checked.
  - `:focus-visible` gets a 2px accent outline. Tap targets are at least 40px tall below 720px.
  - Only `.savebar` (slides up) and `.sheet` (slides in) have transitions, inside `@media (prefers-reduced-motion:no-preference)`.
  - `.key-chip` uses `--dim` (it used the undefined `--muted`).
  - Live-line and chart swatch colours stay as literal hex.
- [ ] **Step 2: Write the harness.**
  - `preview.cpp` includes `../../src/web_app_assets.h` and `../../src/web_theme.h`. It holds the four `WebThemeColors` sets: `camD` and `solD` from `kUiThemePresets` (`main_lvgl.cpp:3809`); `evrL`; and `eink`, which is black and white as in `applyUiThemePalette()`'s `!HAS_UI_THEMES` branch.
  - It prints `<!DOCTYPE html><html><head>`, the font link, `<style>` + `kAppCss` + vars + `</style>`, then `</head><body>`, then `sample_body.html` from stdin, then `<script>` + `kAppJs` + `</script></body></html>`.
  - `sample_body.html` mirrors the markup Task 6 emits: header, rail, tabbar, sheets, a Config pane with the Identity, Device, LoRa Radio and Channels sections (real field names, one checkbox, one `.hint`), a Utilities pane with one each of `.btn`, `.btn-2` and `.btn-danger`, and the savebar.
  - `run.sh` compiles with `c++ -std=c++17 -Wall -Werror` together with `src/web_theme.cpp`, and writes the output.
- [ ] **Step 3: Generate and check.** Run `tools/web_preview/run.sh camD && tools/web_preview/run.sh eink`. Expected: both files written, no compiler warnings.
- [ ] **Step 4: Look at them.** Open both in Chrome at 1280×800 and 390×844. Expected:
  - The rail shows at desktop width and the tab bar at phone width.
  - Labels and inputs sit side by side on desktop and stacked on the phone.
  - In `eink`, the danger button, the chips and the focus ring (Tab key) are all visible on white.
- [ ] **Step 5: Commit** `git add src/web_app_assets.h tools/web_preview && git commit -m "Web config: new stylesheet and preview harness"`

### Task 3: Shell JS: navigation, sheets, scroll-spy

**Files:**
- Modify: `src/web_app_assets.h` (`kAppJs[]`), `tools/web_preview/sample_body.html`

**Interfaces:**
- Consumes: the class contract from Task 2.
- Produces these globals in `kAppJs`:
  - `appTab(tab)` updates `.active` and `aria-current="page"` on every `[data-tab]`, updates `.active` on `#tab-<tab>`, and writes the hash.
  - `appOpenSheet(id)` and `appCloseSheets()`.
  - `appBuildSectionNav()` fills `#rail-secs` and `#sheet-secs` with one `.sec-link` per `.cfg-sec` (text from its `h2`, `href='#config/<slug>'`, where the slug is the section id minus `sec-`).
  - `appRestoreLocation()` reads `sessionStorage['wcLoc']` first, then `location.hash`, and calls the page's `switchTab(tab)` plus `scrollIntoView` for a section.
  - Task 6 makes `switchTab` call `appTab`.

- [ ] **Step 1: Write `kAppJs` (nav part).**
  - On `DOMContentLoaded`: build the section nav, then restore the location.
  - A scroll-spy uses `IntersectionObserver` (rootMargin `-30% 0px -60% 0px`) to set `.here` on the matching `.sec-link`s, update the text of `#sec-current`, and `history.replaceState` the hash. Skip it when `!('IntersectionObserver' in window)`.
  - The scrim click and Escape close the sheets.
  - All `sessionStorage` access goes in try/catch.
- [ ] **Step 2: Syntax check.** Run `tools/web_preview/run.sh camD`, then extract the script and run `node --check`. Pipe the text between `<script>` and `</script>` into a temp `.js` file: `sed -n '/<script>/,/<\/script>/p' .superpowers/preview/camD.html | sed '1s/.*<script>//;$s/<\/script>.*//' > /tmp/wc.js && node --check /tmp/wc.js`. Expected: no output, exit 0.
- [ ] **Step 3: Check in the browser.**
  - Clicking a rail section link scrolls there and the hash reads `#config/lora`.
  - Reloading lands on the same section.
  - At 390px, "Sections ▾" opens the sheet and More opens the More sheet.
  - With `sessionStorage.wcLoc='#utils'` set and the hash cleared, a reload opens Utilities (this covers Review Focus 3).
  - In DevTools, `delete window.IntersectionObserver` before load produces no console errors (Review Focus 5).
- [ ] **Step 4: Commit** `git commit -am "Web config: shell navigation script"`

### Task 4: Unsaved-change tracking and save bar

**Files:**
- Modify: `src/web_app_assets.h` (`kAppJs[]`), `tools/web_preview/sample_body.html`

**Interfaces:**
- Consumes: `#cfg-form` (the `/save` form gets this id in Task 6), `.savebar`, `#save-count`, `#save-discard`. `.sec-link` from Task 3.
- Produces:
  - `appTrackForm(form)` records the baseline: `value` / `checked` / `selectedIndex` for each control that is not hidden, file, submit or button.
  - `appRebaseline()`: the page calls it after its own init scripts (Task 6), and again whenever a script adds controls.

- [ ] **Step 1: Write the tracker.**
  - A single delegated `input` + `change` listener on the form recomputes the controls that differ from their baseline, toggles `.changed` on each, and toggles `.dirty` on its section's `.sec-link`s.
  - The save bar shows when the count is above 0, with `#save-count` reading "1 unsaved change" or "N unsaved changes".
  - Discard calls `form.reset()`, waits one `setTimeout(0)`, then recomputes. `reset` restores the HTML defaults, which equal the baseline.
  - A control with no baseline entry (added later) counts as changed only once edited. Record it at its first event, from its `defaultValue` / `defaultChecked` / `defaultSelected`.
  - `beforeunload` warns while the count is above 0. On form `submit`, clear that warning and store the location in `sessionStorage['wcLoc']`.
- [ ] **Step 2: Add a test hook to `sample_body.html`.** A button that appends a new channel row via `insertAdjacentHTML`, the way the real channel script does.
- [ ] **Step 3: Syntax check.** Same `node --check` command as Task 3 Step 2. Expected: exit 0.
- [ ] **Step 4: Check in the browser** (Review Focus 2):
  - Load: the save bar is hidden.
  - Change Region: the bar shows "1 unsaved change" and the LoRa Radio rail link has a dot.
  - Change it back: the bar hides.
  - Add a channel row: the bar stays hidden. Type in its name: "1 unsaved change".
  - Discard: everything is cleared.
  - Reload with a pending change: the browser asks before leaving.
- [ ] **Step 5: Commit** `git commit -am "Web config: unsaved-change tracking and save bar"`

### Task 5: Firmware wiring: theme accessor, head, /app.css

**Files:**
- Modify: `src/web_config.h`, `src/main_lvgl.cpp` (after `applyUiThemePalette()`, ~line 5010), `src/web_config.cpp:647-815` (`kHead`), `:3357`, `:3500`, `:10057` (near `handleGetFavicon`), and the `onRoute` list near `:10707`

**Interfaces:**
- Consumes: `WebThemeColors`, `webThemeCssVars` (Task 1); `kAppCss`, `kAppJs` (Tasks 2–4).
- Produces:
  - `bool webCfgThemeColors(WebThemeColors *out);`, declared in `web_config.h` (which includes `web_theme.h`). It has a weak default in `web_config.cpp` that returns false, and a strong definition in `main_lvgl.cpp` that copies the fields from `s_ui`: `bgMain`, `panelBg`, `panelAlt`, `panelStrong`, `divider`, `dividerHi`, `inputBg`, `accent`, `textMain`, `textDim`, `textOnAccent`, `battGood`, `battWarn`, `battBad`, and `isLight = s_cfg.uiMode == UI_MODE_LIGHT`.
  - `static void sendFullHead()` in `web_config.cpp` sends:
    1. `kHeadOpen`: doctype, metas, title, favicon, the font link, the Leaflet CSS link, and `<link rel='stylesheet' href='/app.css?v=APP_VERSION'>`.
    2. `<style>` + the vars + `</style>`, if the accessor returns true.
    3. `kHeadClose`: `</head><body>`.
  - `static void handleGetAppCss()` sends the cache header and streams `kAppCss`. Route: `onRoute("/app.css", HTTP_GET, handleGetAppCss)`, with no auth, like the favicon.

- [ ] **Step 1: Write the accessor and the weak default.** Add `#include "web_theme.h"` to `web_config.h`, and add `src/web_theme.cpp` to the build. PlatformIO compiles everything under `src/`, so no ini change is needed. Confirm with `grep -n "src_filter\|build_src_filter" platformio.ini`; expected: no filter excludes new files.
- [ ] **Step 2: Replace `kHead`** with `kHeadOpen` / `kHeadClose`, and replace both `sendFlash(... ? kLiteHead : kHead)` call sites. The lite branch stays `sendFlash(kLiteHead)`; the full branch calls `sendFullHead()`. Stream the vars with `server.sendContent`, from a 600-byte stack buffer.
- [ ] **Step 3: Add the route and handler.** Add `/app.css`, and make sure `kAppJs` is emitted once at the end of the full page (Task 6 places it).
- [ ] **Step 4: Static check (no build).**
  - Run `git diff -U0 src/web_config.cpp | grep -n "kLiteHead"`. Expected: only the two call-site lines, each still choosing `kLiteHead` for lite.
  - Run `grep -n "kHead\b" src/web_config.cpp`. Expected: no matches.
- [ ] **Step 5: Commit** `git commit -am "Web config: device theme colours, /app.css"`

### Task 6: Page frame, sections, switchTab

**Files:**
- Modify: `src/web_config.cpp`
  - `section()` / `sectionEnd()`: `:3319-3341`
  - Header and tab row: `:3843-3911`
  - Pane wrappers: `:5531`, `:5969`, `:6006`, `:6030`, `:6711`, `:6731`
  - Save All: `:5467`
  - `switchTab`: `:7728-7770`
  - Page end: `:7770-7775`
  - The 14 `section(html, lite, "…")` calls, `:3923-5447`

**Interfaces:**
- Consumes: the class contract (Task 2); `appTab`, `appTrackForm`, `appRebaseline` (Tasks 3–4); `sendFullHead` (Task 5).
- Produces:
  - `section(String &html, bool lite, const char *title, const char *slug, const char *desc)`. Full page: `<section class='cfg-sec' id='sec-<slug>'><h2>title</h2>` plus `<p class='sec-desc'>desc</p>` if `desc` is non-null. Lite: `<h3>title</h3>`, unchanged.
  - `sectionEnd` closes `</section>`. The `open` parameter goes away.

- [ ] **Step 1: Rewrite `section()` and the 14 calls.** Use the slugs from Global Constraints. Descriptions:
  - **identity:** "How this node appears to others on the mesh."
  - **device:** "Role, rebroadcasting and clock."
  - **position:** "GPS and what location this node shares."
  - **lora:** "Must match the other nodes you want to hear."
  - **channels:** "Up to 8 channels; the first is primary."
  - **mqtt:** "Bridge the mesh to an MQTT broker over WiFi."
  - **display:** "Theme, brightness and fonts on the device."
  - **notify:** "Sounds, vibration and LED for new messages."
  - **power:** null.
  - **modules:** "Telemetry, beacons and other optional features."
  - **wifi:** "Networks this device joins."
  - **firmware:** "Update checks and over-the-air updates."
  - **nodes:** "Clean up the node list."
  - **access:** "Who can open this page."

  Utilities' 9 sections keep the same helper with their own slugs (`u-diag`, `u-admin`, `u-maps`, `u-debug`, `u-update`, `u-backup`, `u-capture`, `u-colors`, `u-messages`) and null descriptions.
- [ ] **Step 2: Replace the header and tab row with the frame.** Emit:
  - `.app-hd`: the short name in `.nm`, the long name, battery and GPS as `.chip .chip-good|warn|bad` (map today's `metric-good/warn/bad`), the key chip, and the Logout link when auth is on.
  - The chat-paused warning as `.notice-err`.
  - `<nav class='app-rail'>`: Config with `<div id='rail-secs'></div>` under it, then the other tabs as `<a data-tab='…' href='#…'>`, emitted under today's `#if` and `cfgWebFilesEnabled` conditions.
  - `<main class='app-main'>`.
  - The tab-panel wrappers, which keep their ids.
  - At the end of the page: `<nav class='tabbar'>` (Config, Live, Chat if present, Nodes, More), `#sheet-secs` and `#sheet-more` (Utilities, Remote, Files and Logout where present), the scrim, and `.savebar` with `#save-count`, `#save-discard` (`.btn-2`) and `<button type='submit' form='cfg-form' class='btn'>Save and restart</button>`.
  - Phones show the sticky "Sections ▾" bar (`#sec-current`) only while Config is active.
- [ ] **Step 3: The form and the end of the page.**
  - Give the `/save` form `id='cfg-form'`, keeping its `onsubmit`.
  - Delete the Save All button. Lite keeps a plain submit button: `if (lite)` emit the old button, with `style` removed.
  - At the end of the page, send `<script>` + `kAppJs` (`sendFlash`) + `</script>` before the existing inline script.
  - `switchTab(tab)` keeps every per-tab side effect but replaces the per-id `classList.toggle` lines with `appTab(tab)`.
  - Its last line in the page's init: `appTrackForm(document.getElementById('cfg-form'))`, after the preset/channel/theme init scripts.
- [ ] **Step 4: Static checks.**
  - `grep -n "tab-btn" src/web_config.cpp`: no matches.
  - `grep -c "section(html, " src/web_config.cpp`: 23.
  - `git diff src/web_config.cpp | grep "^-.*if (lite)"`: no lite branches removed (Review Focus 1).
  - Copy the emitted frame markup into `sample_body.html`, re-run `tools/web_preview/run.sh camD`, and run the `node --check` from Task 3 Step 2. Expected: exit 0.
- [ ] **Step 5: Commit** `git commit -am "Web config: rail, tab bar, open sections, save bar"`

### Task 7: Config pane cleanup

**Files:**
- Modify: `src/web_config.cpp:3923-5530` (Config pane only)

**Interfaces:**
- Consumes: `.cfg-sub`, `.hint`, `.row2`, the toggle skin (Task 2).

- [ ] **Step 1: Remove the layout inline styles.**
  - Drop every `style='display:flex;align-items:center;gap:.5em'` on checkbox labels; the CSS handles them.
  - The Modules `<h3 style='font-size:.95em;margin:.8em 0 .3em'>` become `<h3 class='cfg-sub'>`.
  - Rename `class='gps-hint'` to `class='hint'`, and add `.gps-hint` to the stylesheet as an alias for anything outside this range.
  - Keep any `style` whose value is computed (swatch colours).
- [ ] **Step 2: Lite check** (Review Focus 1). Every changed line inside the pane must still make sense with no CSS: no information carried only by a class, and no element hidden by CSS alone. Read through the diff with `git diff -U0 src/web_config.cpp` over the pane range.
- [ ] **Step 3: Count.** Run `awk 'NR>=3900&&NR<=5530' src/web_config.cpp | grep -c "style='"`. Expected: only computed-value styles remain. List them in the commit message.
- [ ] **Step 4: Commit** `git commit -am "Web config: config pane uses shared field styles"`

### Task 8: Other tabs cleanup

**Files:**
- Modify: `src/web_config.cpp:5531-7770` (Utilities, Live, Chat, Nodes, Remote, Files), `:1141-1730` (the `kFilesPane`, `kAdminModal`, `kFontModal`, `kChatBlinkModal` and `kThemePicker` blobs)

- [ ] **Step 1: Buttons.**
  - Every `<button … style='background:#…'>` becomes `class='btn-danger'` when its action is reboot, clear-messages, clear-nodes, clear-maps, factory-reset, wardrive clear or sd-repair. Otherwise it becomes `class='btn'`, or `class='btn-2'` when it sits beside a primary button.
  - `.msg` / `.err` become `.notice` / `.notice-err`; keep the old names as aliases in the CSS.
- [ ] **Step 2: Remove the decorative inline styles** (margins and font sizes on `<h3>` and `<p>`) in favour of the stylesheet's defaults. Keep computed ones.
- [ ] **Step 3: Check the light themes** (Review Focus 4). Run `tools/web_preview/run.sh evrL && tools/web_preview/run.sh eink`, then check the Utilities sample in both: danger buttons, chips and the save bar all readable.
- [ ] **Step 4: Count.** Run `grep -c "style='" src/web_config.cpp`. Expected: far below 190, with only computed values and lite-only markup remaining.
- [ ] **Step 5: Commit** `git commit -am "Web config: shared buttons and notices across tabs"`

### Task 9: Composite PR

- [ ] **Step 1: Run `tests/run.sh`.** Expected: all pass.
- [ ] **Step 2: Push** `git push -u origin next/2026-10-25`. Create the label if it's missing (`gh label create composite`).
- [ ] **Step 3: Open the draft PR.** Run `gh pr create --draft --base main --label composite --title "Web config refresh: device-themed layout with side rail and save bar"`. The body has these sections:
  - **Summary.**
  - **Changes:** one bullet each for layout, save bar, device theme, `/app.css`, and button cleanup, with all boards except the Cardputer.
  - **Testing:** host tests and browser preview only; no PlatformIO build or flash yet; `tdeck` and `tdeck-pro` are the builds to run.
  - **Notes for release:** saving needs JavaScript now; the page follows the device theme.

  End the body with the attribution line.
- [ ] **Step 4: Report** the PR URL and the untested items to the user.
