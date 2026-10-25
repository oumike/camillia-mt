# Web config refresh — design

Date: 2026-10-25
Status: approved in brainstorming, awaiting spec review

## Goal

Give the full web config (`src/web_config.cpp`) a complete visual and
structural refresh. It works today; this makes it nicer to use and makes it
look like Camillia rather than a generic admin page. Mockups that were picked:
layout A (side rail), phone layout B (bottom tab bar + section sheet), style A
(the page wears the device's own theme).

## Scope

- **Boards:** every board that serves the full page. The Cardputer
  (`kLiteOnlyBoard`) and the AP-mode lite page (`kLiteHead`, `/setup`) are not
  changed. The config pane builder is shared with lite (`lite` flag), so any
  markup change inside it must still render correctly under `kLiteHead`, which
  has no classes styled.
- **Unchanged:** every route, every form field `name`, every POST handler, the
  single `/save` form and its confirm prompt, auth/session handling, the
  chunked/sliced sending code, and the behaviour of every action.
- **Changed:** the shared page frame, the stylesheet, the Config tab's layout,
  and the styling of every other tab (Utilities, Live, Chat, Nodes, Remote,
  Files). Those other tabs keep their content and order; they adopt the new
  components and lose their inline styles.

## 1. Page frame

- **Header (all widths):** node short name in Martian Mono (accent colour),
  long name beside it, then status chips on the right: battery (good/warn/bad
  colours from the device palette), GPS, and the copy-to-clipboard public key
  chip that exists today. Logout at the far right when web auth is on. The
  "chat is paused" warning (`gWebBuffersReclaimed`) stays, as a full-width
  banner under the header, styled as a danger notice.
- **Wide (≥ 720px):** a left rail.
  - Config, with its section links nested beneath it.
  - Then Live, Chat, Nodes, Utilities, Remote, Files, emitted under the same
    conditions as today's tab buttons (`DEVICE_CARDPUTER_LORA_HAT`,
    `HAS_VNC_HOST`, `HAS_WEB_FILES` + `cfgWebFilesEnabled`).
  - The rail is sticky and scrolls independently if taller than the viewport.
- **Narrow (< 720px):** the rail is hidden.
  - A bottom tab bar holds Config, Live, Chat, Nodes and More (Chat omitted
    where it is today).
  - More opens a bottom sheet with Utilities, Remote, Files (where present) and
    Logout.
  - Icons are inline SVG or Unicode, no icon font.
- **Location in the URL:** the active tab and section are kept in
  `location.hash` (`#live`, `#config/lora`), so a reload, or the redirect after
  a save, returns to the same place. `switchTab()` keeps its name and callers;
  it also writes the hash and updates both the rail and the tab bar.
- The redirect after save (`redirectHomeWithFlash`) goes to `/`. The hash
  survives because the client stores it in `sessionStorage` before submitting
  and restores it on load.

## 2. Config tab

- **Panels:** the 14 `<details>` accordions become always-open panels.
  - `section()` emits `<section class='cfg-sec' id='sec-<slug>'>` with an `<h2>`
    title and an optional one-line description.
  - The lite path is unchanged: a plain `<h3>`.
  - Slugs: identity, device, position, lora, channels, mqtt, display, notify,
    power, modules, wifi, firmware, nodes, access.
- **Section navigation:**
  - The rail's section links are jump links.
  - An `IntersectionObserver` scroll-spy marks the section in view, in the rail
    and in the phone's "Sections ▾" bar.
  - On a phone that bar sticks under the header, names the current section, and
    opens a bottom sheet listing all sections.
- **Field rows:**
  - Each label + control pair is a row: label left, control right, on wide
    screens. Label above control on narrow ones.
  - Help text currently in `.gps-hint` and similar becomes a dim line under the
    label.
  - Checkboxes are styled as toggles in CSS only. They stay
    `<input type=checkbox>` with the same names, so the POST is identical.
  - Sub-headings inside Modules (Telemetry, Sensor Telemetry, Mesh Beacons…)
    become `<h3 class='cfg-sub'>` instead of inline-styled `<h3>`s.
- **Unsaved changes:**
  - On load, a script records each control's initial value (`value`,
    `checked`, `selectedIndex`) inside the `/save` form.
  - On `input`/`change` it compares against that record. A changed control gets
    `.changed`, which draws an accent outline. Its section link gets a dot.
  - **Save bar:** when one or more controls differ, a bar slides up, fixed to
    the bottom (above the phone tab bar). It shows "N unsaved change(s)",
    **Discard** and **Save and restart**.
  - **Discard** calls `form.reset()` and clears the marks.
  - **Save and restart** submits the form. The existing confirm text and
    `onsubmit` stay.
  - The bottom "Save All" button is removed. The form keeps working without
    JavaScript only through Enter in a text field, which is acceptable for the
    full page.
  - `beforeunload` warns while changes are unsaved, and is cleared on submit.
  - Controls injected by existing scripts (preset JS, channel rows, custom LoRa
    fields) are covered by delegating the listeners on the form, not on each
    control, and by re-recording the baseline after those scripts' own init.

## 3. Theme from the device

- **Accessor:** a new function, declared in `web_config.h` and defined in
  `main_lvgl.cpp`:
  - `bool webCfgThemeColors(WebCfgThemeColors *out)`.
  - It copies these from `s_ui`: `bgMain`, `panelBg`, `panelAlt`, `panelStrong`,
    `divider`, `dividerHi`, `inputBg`, `accent`, `textMain`, `textDim`,
    `textOnAccent`, `battGood`, `battWarn`, `battBad`. It also sets `isLight`.
  - Colours are RGB565 converted to `#rrggbb` with the existing `rgb565To888()`.
  - It reads `s_ui` after `applyUiThemePalette()` has run, so presets, custom
    themes and the no-`HAS_UI_THEMES` e-ink palette all come out as on screen.
- **Page:** right after `<head>`, the page emits a small `<style>` with
  `:root{--bg:…;--panel:…;…;color-scheme:light|dark}` built from those values.
  The stylesheet only ever refers to these variables, with fallback values in
  `/app.css` equal to Camillia Dark, used if the accessor returns false.
- **Contrast guard:** if `textMain` against `bgMain` is below a 4.5:1 contrast
  ratio (possible with a custom theme), the inline block substitutes the
  matching preset's text colours for that mode.
- **Theme changes:** saving a new theme in Display, or in the theme picker,
  applies on the device, and the next page load (the post-save redirect)
  carries the new colours.

## 4. Styles and components

- **`/app.css`:**
  - The stylesheet moves out of `kHead` into a new `static const char
    kAppCss[]`, served with `sendFlash`-style streaming at `/app.css`.
  - It is sent with `Cache-Control: public, max-age=31536000, immutable` and
    linked as `/app.css?v=<APP_VERSION>`, so it is fetched once per firmware
    version.
  - The route needs no login, like `/favicon.ico`.
  - `kHead` shrinks to the document head, font links, the `/app.css` link, the
    Leaflet CSS link it already has, and the inline theme block.
- **Fonts:** a Google Fonts link for IBM Plex Sans (400, 600), IBM Plex Mono
  (400) and Martian Mono (500), with `display=swap` and system fallbacks. Plex
  Mono is for real data only: frequencies, keys, coordinates, node IDs, the live
  feed. Not for labels.
- **Buttons:**
  - `.btn` (primary, accent), `.btn-2` (secondary, outlined), `.btn-danger`
    (red family derived from `battBad`).
  - Every inline `style='background:#…'` on buttons is replaced by one of
    these. Destructive actions (reboot, clear messages, clear nodes, clear
    maps, factory reset, wardrive clear, SD repair) use `.btn-danger`. The
    other Utilities actions use `.btn` or `.btn-2`.
- **Other components:** `.chip` (status and metric chips), `.panel`, `.toggle`
  (checkbox skin), `.sheet` (bottom sheet + scrim), `.notice` / `.notice-err`
  (replacing `.msg` / `.err` and the inline red banner).
  - Modal styles (`.modal-back`, `.modal-card`, `.modal-opt`) are kept but
    retokened.
  - The live-feed line colours and chart swatches stay as they are: they encode
    packet types, not theme.
- **Inline styles:** the ~190 `style='…'` attributes are removed or replaced
  with classes, except where a value is computed per element (e.g. a swatch
  colour).
- **Bug fixes:** the undefined `--muted` variable (used by `.key-chip`) is
  replaced with `--dim`.
- **Motion:** only the save bar's slide-up and the sheets' open/close, both
  disabled under `prefers-reduced-motion`. No entrance animations.
- **Accessibility:** visible `:focus-visible` rings in the accent colour; tap
  targets at least 40px tall on narrow screens; the rail and tab bar are
  `<nav>` elements with `aria-current` on the active item.

## 5. Memory and sending

- Everything static (CSS, scripts) stays in flash constants and is streamed
  with the existing `sendFlash` / `sendSliced` helpers. No new large `String`
  concatenations: dynamic parts keep going through `sendChunk`/`sendChunkIfBig`.
- Moving the CSS out to `/app.css` reduces the bytes sent per page load after
  the first.

## 6. Testing

- **Now:** no builds or flashing.
- **Browser preview:** before any build, save the page's rendered HTML (as
  produced by the new code paths) and preview it in a browser at desktop and
  phone widths, in Camillia Dark, one light theme and the e-ink palette.
- **Build:** when the user allows it, `tdeck` (a colour board) and
  `tdeck-pro` (e-ink, `!HAS_UI_THEMES`). Then check on a device: save a change,
  discard, theme switch, every tab, and the lite page in AP mode unchanged.
- **Delivery:** through the composite PR (`next/2026-10-25`).

## Out of scope

- The lite page and `/setup`.
- The VNC viewer page (second server).
- New settings, new endpoints (other than `/app.css`), or a JSON API.
- Reworking the Live, Chat or Nodes tabs' functionality.
