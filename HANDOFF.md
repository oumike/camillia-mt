# Handoff — camillia-mt (Meshtastic firmware) session

Written 2026-07-26. Documents the **one in-flight fix**, everything landed this
session, and the known-open problems worth picking up next. (Supersedes the
2026-07-07 handoff, whose live-screen crash investigation is long since closed.)

---

## Project basics

- PlatformIO / Arduino-ESP32 firmware. LVGL UI (v8.3.11).
- Build envs (`platformio.ini`): `tdeck`, `tlora-pager-tft`, `cardputer-cap`,
  `heltec-v4`, `heltec-v4-vertical`.
- **Only `cardputer-cap` has no PSRAM.** That single fact drives most of the
  web-config design below.
- Commands:
  - Build: `pio run -e <env>`
  - Flash: `pio run -e <env> -t upload`
  - Monitor: `pio device monitor -e <env>` (115200)

## Git state

- **HEAD = `537c3b4` "Work"** — contains everything from this session except the
  item immediately below.
- **Uncommitted: `src/main_lvgl.cpp` only** (+9/−3) — the in-flight chat-scroll
  fix, described next.
- **Nothing in this session was compiled by the assistant.** The user built and
  flashed throughout, so the committed work is known-good on hardware; the
  uncommitted edit below is not.

---

## IN FLIGHT — chat scroll position on channel switch

**Symptom (user-reported):** switching channels sometimes opens part-way up the
new channel's history instead of at the newest message. Intermittent.

**Work already applied (uncommitted, in `refreshChatView()`,
`src/main_lvgl.cpp` ~18789):**

```c
const bool channelChanged = (s_activeChannel != s_chatWindowChannel);
...
const bool stickToBottom = force || channelChanged
                           || (lv_obj_get_scroll_bottom(s_chatList) <= 6);
const int32_t prevScrollY = channelChanged ? 0 : lv_obj_get_scroll_y(s_chatList);
```

Rationale: `stickToBottom` and `prevScrollY` were both measured against the
**outgoing** channel's content before `lv_obj_clean()`, so a channel you had
scrolled back in carried its offset into the next one.

**This is probably not the whole cause, and may not be the cause at all.**
`setActiveChannel()` already calls `refreshChatView(true)`, and `force` alone
sets `stickToBottom` — so the normal switch path was already sticking to the
bottom. Keep the change (it is correct, and defensive for any non-forced path)
but do not assume it fixes the report.

**Stronger hypothesis, NOT yet applied:** `refreshChatView()` scrolls without
flushing layout first. Wrapped chat labels have no final height until a layout
pass runs, so `lv_obj_scroll_to_view(lastMsgObj)` bounds against stale geometry
— which is exactly why it would be intermittent.

The codebase already has this fix twice, as precedent to copy:

- `refreshLiveView()` ~10402: `lv_obj_update_layout(s_liveList);` with a comment
  saying `scroll_to_y` otherwise bounds against stale geometry.
- `openThemeModal()` ~5758: same, for `scroll_to_view`.

**Next step:** add `lv_obj_update_layout(s_chatList);` immediately before the
`if (s_chatWindowAnchorActive) { ... } else if (stickToBottom) { ... }` block
(~19009), build, and have the user switch channels *after scrolling back* in a
busy channel — that is the state that reproduces it.

---

## Landed this session (committed in `537c3b4`)

1. **Web config defaults on for fresh installs** (`main_lvgl.cpp` ~3379, ~3649).
   Two paths: blank NVS (early return) and namespace-without-our-keys
   (`prefs.getBool("webCfgEnabled", s_firstBoot)`). Configured devices keep
   whatever they persisted.

2. **Web Config Lite.** `sendConfigPage(msg, lite)` renders both variants from
   one Config pane, so the two cannot drift. Lite = small CSS head, no other
   tabs, `totalNodes = 0`, plain selects instead of the theme swatch grid and
   font-size modal. Same `/save`, same form.

3. **Cardputer is lite-only in STA as well as AP** — `webCfgUseLite()` =
   `gApMode || kLiteOnlyBoard` (`web_config.cpp` ~73). It also registers only
   the 8 lite routes, so `/export` and `/import` are unreachable by URL there.

4. **Web-config write path hardening** (`web_config.cpp`). Took several rounds;
   the reasoning is worth preserving:
   - Big literals (`kRegionOptions`, `kPresetJs`, `kThemePicker`, `kFontModal`)
     are flash constants streamed with `sendContent_P`, never appended to a
     `String` — a multi-KB append forces a contiguous allocation AP-mode heap
     cannot provide.
   - **`WiFiClient::write()` cannot be bounded from outside.** It uses
     `MSG_DONTWAIT` behind a `select()` with a hardcoded 1 s timeout and 10
     retries, and a partial send re-arms them. `setTimeout()` does **not** apply
     to writes. One `sendContent()` can block the main loop for exactly
     10009 ms (observed).
   - So: `clientWritable(250)` polls before every write, writes are sliced to
     512 bytes (`kFlashChunkBytes`), each yields `delay(1)`, and a stalled
     socket sets `gSendAborted` and closes the connection.
   - Diagnostics: `[web] GET /`, `lite: <section>`, `node list built`,
     `slow chunk: N bytes in M ms`, `socket not draining`, `page sent` /
     `page abandoned`.

5. **"AP" entry in the WiFi picker** — sentinel SSID `kForceApSsid`,
   `wifiForceApMode()`, `webCfgSetForceAp()`. `wifiHasActiveCreds()` returns
   false while selected, which is what stops every STA path from trying to join
   a network literally named "AP". **Persisted** as NVS `wifiForceAp`.

6. **Brightness setting** — `RhinoConfig::brightness` (percent, 10% steps),
   `cfgCoerceBrightness()` / `cfgBrightnessDuty()` in `config_io.h`. Device
   modal with `lv_slider`, live preview, **J = brighter / K = dimmer**, Enter
   saves, Backspace restores. Web slider under Display. NVS + YAML
   (`display.brightness`). Default derives from each board's
   `TFT_BRIGHTNESS_DEFAULT`. `revertCfgBrightnessPreview()` is called from
   `closeCfgModal()` so an uncommitted preview cannot leak into a later save.

7. **Cardputer modal fixes** — `kModalRowDescriptions` false there (drops
   per-row helper text in Chat Style / Chat Names), and six CFG picker modals
   had `LV_OBJ_FLAG_SCROLLABLE` **cleared** while their refresh helpers already
   called `lv_obj_scroll_to_view()`; scrolling is now enabled so clipped rows
   are reachable.

8. **Cardputer chat-paused warning.** Cardputer frees 38 KB of chat buffers so
   Wi-Fi can start, after which `_pushLine()` silently drops every message. With
   web config default-on this broke messaging invisibly. Per user decision
   auto-start stays and the state is surfaced instead: modal on enable *and* on
   auto-start, `Web Config: On (chat PAUSED)` on the settings row, red banner on
   the web page. `webCfgChatPaused()` exposes it.

9. **Pager battery stuck at 0%** (`battery_util.cpp`). A successful I2C read
   returning `raw == 0` returned 0 forever with no re-arm path — the latch is
   fixed and logs once per episode. Suspected trigger: BQ25896's 40 s I2C
   watchdog resetting registers, so `bqKickWatchdog()` (REG03 bit 6) pets it.
   **Unconfirmed** — the confirming signal is whether battery read correctly for
   ~40 s after boot and then latched at 0.

10. **WiFi picker lists network names only** (no "Configured:"/"Known:").

11. **CI workflow** — triggers on push/PR to `main`, keeping the tag trigger.
    Explicitly *not* a release gate (release.sh publishes before CI finishes)
    and never uploads assets, since only the release machine holds the OTA
    signing key. ELF/MAP artifact upload removed as misleading.

12. **Docs** — `docs/USE.md` gained Brightness, Web Config, and Choosing a
    Wi-Fi network sections; `README.md` updated for default-on / Lite / AP, and
    its Releases section corrected (publishes `.bin`/`.sig`, not `.elf`).

---

## Known open problems (not started)

- **Full page builds the node list in RAM.** On Heltec with 124 nodes this cost
  ~500 KB of 8-bit heap and dropped internal largest-block to 7.6 KB, which is
  what starves Wi-Fi TX buffers. `cardputer-cap` already streams nodes directly
  (`streamNode*` lambdas in `sendConfigPage`); doing that on every board
  addresses the cause rather than the symptom. Affects the T-Deck and Heltec
  full page only — lite passes `totalNodes = 0`.
- **Heap degrades across web-config sessions.** T-Deck: 64.6 KB free at boot vs
  44.1 KB on a second enable; largest block 34.8 KB → 16.4 KB. Suspect a leak or
  fragmentation in teardown. The second AP session always starts closer to the
  edge than the first.
- **Web-config diagnostics are verbose** (`lite: <section>`, chunk timings).
  Trim to failure cases once the page is considered stable.
- **Roadmap item** "Gate web config auto-start behind onboarding/settings/button"
  in `README.md` now conflicts with default-on. Left as historical.

## Working notes

- The user builds and flashes themselves; assistant builds were paused early
  ("don't run builds for the time being") and never resumed.
- Structural sanity check used in place of compiling: strip strings/comments,
  compare delimiter counts against `git show HEAD:<file>` rather than expecting
  zero — `main_lvgl.cpp` has a baseline `{}` delta of 3 from preprocessor
  branches.
