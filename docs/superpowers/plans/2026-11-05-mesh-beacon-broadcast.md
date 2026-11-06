# Mesh Beacons: broadcast — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Once per interval, retune briefly to a target preset's public default channel and broadcast a MeshBeacon (port 37) advertising this node's own mesh, then restore the home radio without touching saved config.

**Architecture:** A new pure module `src/mesh_beacon.{h,cpp}` owns encoding and schedule maths (host-tested). `ChannelMgr::sendMeshBeacon` frames and transmits one zero-hop packet with a caller-supplied key. `main_lvgl.cpp` gains a shared `tempTuneEnter/Leave` pair (lifted from Discovery) and `serviceMeshBeacon()` in `loop()`. Config, YAML, web and device UI follow the existing `meshBeaconListen` paths.

**Tech Stack:** C++17, Arduino/ESP32 (PlatformIO), LVGL, host tests via `tests/run.sh`.

**Spec:** `docs/superpowers/specs/2026-11-05-mesh-beacon-broadcast-design.md` (also the body of issue #141).

## Global Constraints

- Build target for verification: `pio run -e tdeck` only. Never flash.
- No commits or pushes until the user says so. The working tree carries unrelated uncommitted Tanmatsu work in the same files (`main_lvgl.cpp`, `config_io.h`, `web_config.cpp`), so per-task "checkpoint" steps below are `git diff --stat` reviews, not commits.
- Nothing in the beacon path writes `s_cfg`, calls `markConfigDirty()`, or saves the channel plan. Only the last-sent NVS key is written.
- Packet: broadcast `0xFFFFFFFF`, `hop_limit = 0`, `hop_start = 0` (`meshOriginHopFlagsCapped(0)`), no want_ack, portnum 37, Data `bitfield = 0`, not uplinked to MQTT.
- Target key: Meshtastic default key (`expandPsk(0x01, key)`, 16 bytes); channel hash `computeChannelHash(kPresets[target].channelName, key, 16)`.
- Message ≤ 60 bytes, truncated on a UTF-8 boundary. Interval ∈ {1,2,3,6,12,24} h, default 1. Target default `PRESET_LONG_FAST`. Offer PSK default off.
- Air-util hold only when region is `EU_433` or `EU_868`, while `Radio.airUtilTxPercent() >= 5`; backoff 5 min, doubling, cap 60 min.
- No beacon when role is CLIENT_HIDDEN. Never substitute a different preset for a target that cannot run.
- New `RhinoConfig` fields appended at the very end, following the padding rule documented in `src/config_io.h` (each new block starts at the previous `sizeof(RhinoConfig)`).
- User-facing strings go through `TR()` with es/fr entries in `src/i18n_builtin.h`.

## Review Focus

1. **Retune failure mid-send** — if `sendMeshBeacon` returns false (TX error) the radio must still be restored to home freq/bw/sf/cr. Test: Task 4 step "restore on failure" (manual serial check, since the radio isn't host-testable) — `tempTuneLeave` runs unconditionally after send.
2. **Clock not set at boot with a saved last-sent** — must wait a full interval, never send immediately. Test: `test_due_after_reboot_without_clock` in Task 1.
3. **`millis()` wrap (~49.7 days)** — due/not-due must be wrap-safe. Test: `test_due_across_millis_wrap` in Task 1.
4. **Target preset equals home preset on the default channel** — no retune; send directly. Test: Task 4 log line shows `retune=no`.
5. **Multi-byte UTF-8 message at the 60-byte edge** — must not split a code point. Test: `test_message_truncates_on_utf8_boundary` in Task 1.

---

### Task 1: Pure beacon module (encode + schedule)

**Files:**
- Create: `src/mesh_beacon.h`, `src/mesh_beacon.cpp` (no Arduino headers)
- Create: `tests/test_mesh_beacon.cpp`, `tests/native/test_mesh_beacon/test_main.cpp` (copy the wrapper shape from `tests/native/test_mesh_proto/test_main.cpp`)

**Interfaces — Produces:**
```cpp
struct MeshBeaconTx {
    char     message[61];
    char     offerChannelName[13];   // "" = no offer_channel
    uint8_t  offerPsk[32];
    uint8_t  offerPskLen;            // 0 = omit psk
    uint8_t  offerRegion;            // Meshtastic RegionCode; 0 = omit
    bool     hasOfferPreset;
    uint8_t  offerPreset;            // Meshtastic ModemPreset
    bool     hasOfferSlot;
    uint32_t offerSlot;              // 1-based
};
size_t   encodeMeshBeacon(const MeshBeaconTx &in, uint8_t *out, size_t cap);   // 0 on overflow/empty
size_t   encodeMeshBeaconData(const uint8_t *payload, size_t len, uint8_t *out, size_t cap); // Data{1:37, 2:payload}
void     beaconCopyMessage(char out[61], const char *in);   // ≤60 bytes, UTF-8 safe
uint8_t  clampBeaconIntervalH(uint8_t h);                   // snap to {1,2,3,6,12,24}; 0/invalid → 1
uint32_t nextAirtimeRetryMs(uint32_t prevMs);               // 0→300000, ×2, cap 3600000
uint32_t beaconDueInMs(uint32_t nowMs, uint32_t lastSentMs, uint8_t intervalH, uint32_t notBeforeMs);
bool     beaconRegionHasDutyCycle(const char *regionCode);  // "EU_433" / "EU_868"
```
`beaconDueInMs`: `lastSentMs == 0` means never sent; `notBeforeMs == 0` means no hold. Returns 0 when due, else ms remaining (max of interval remainder and hold remainder), all wrap-safe via `(uint32_t)(a - b)`.
`clampBeaconIntervalH` snapping: round to the nearest allowed value, ties down; >24 → 24.

- [ ] **Step 1: Write failing tests** in `tests/test_mesh_beacon.cpp` (header `// test-deps:` none beyond `mesh_beacon.cpp`; add `mesh_proto.cpp` only for the round-trip test if it links on host — if it doesn't, put the round trip in `tests/test_mesh_proto.cpp` instead, which already links it):
  - `test_encode_text_only`: message "hi", nothing else → `0A 02 68 69`
  - `test_encode_offer_no_psk`: name "LongFast", region 1, preset 0 → `12 0A 1A 08 4C 6F 6E 67 46 61 73 74 18 01 20 00` (preset 0 present because optional)
  - `test_encode_offer_with_psk`: name "Home", psk `{0x01}` len 1, region 3 → `12 09 12 01 01 1A 04 48 6F 6D 65 18 03`
  - `test_encode_with_slot`: offer as no-psk case + slot 20 → bytes above then `28 14`
  - `test_encode_empty_returns_zero`: all empty → 0
  - `test_data_envelope`: payload `{0xAA}` → `08 25 12 01 AA`
  - `test_round_trip_decode` (see note above): encode the slot case, `decodeMeshBeacon` → name, region, preset, `hasOfferSlot && offerSlot == 20`
  - `test_message_truncates_on_utf8_boundary`: 59 ASCII + "é" (2 bytes) → result is 59 bytes
  - `test_interval_clamp`: 0→1, 4→3, 5→6, 9→6, 18→12, 30→24, 6→6
  - `test_airtime_backoff`: 0→300000→600000→1200000→2400000→3600000→3600000
  - `test_due_never_sent`: lastSent 0, notBefore 0 → 0
  - `test_not_due`: now 1000+1800000, last 1000, 1 h → 1800000
  - `test_due_after_reboot_without_clock`: last = now (firmware stamps "just sent" at boot) → 3600000
  - `test_due_across_millis_wrap`: last 0xFFFF0000, now 0x0000F000+3600000 → 0
  - `test_hold_extends`: due by interval but notBefore = now+5000 → 5000
  - `test_duty_cycle_regions`: EU_868 true, EU_433 true, US false, nullptr false
- [ ] **Step 2: Run** `tests/run.sh test_mesh_beacon` → FAIL (missing header).
- [ ] **Step 3: Implement** `src/mesh_beacon.{h,cpp}`. Field order 1–5; ChannelSettings inner order psk(2) then name(3).
- [ ] **Step 4: Extend decode** — add `bool hasOfferSlot; uint32_t offerSlot;` to `MeshBeaconPayload` (`src/mesh_proto.h:215`) and parse field 5 varint in `decodeMeshBeacon` (`src/mesh_proto.cpp`).
- [ ] **Step 5: Run** `tests/run.sh test_mesh_beacon test_mesh_proto` → PASS.
- [ ] **Step 6: Checkpoint** `git diff --stat`.

### Task 2: Config fields, YAML, last-sent storage

**Files:** `src/config.h` (next to `MY_MESH_BEACON_LISTEN`), `src/config_io.h` (end of `RhinoConfig`), `src/config_io.cpp` (defaults, load clamps, YAML out ~1983, YAML in ~2716)

**Interfaces — Produces:**
- Macros: `MY_MESH_BEACON_BROADCAST 0`, `MY_MESH_BEACON_TARGET PRESET_LONG_FAST`, `MY_MESH_BEACON_OFFER_PSK 0`, `MY_MESH_BEACON_INTERVAL_H 1`
- Fields (appended, padded per the file's rule): `bool meshBeaconBroadcast; uint8_t meshBeaconTargetPreset; bool meshBeaconOfferPsk; uint8_t meshBeaconIntervalH; char meshBeaconMessage[61];`
- YAML keys under `module_config: mesh_beacon:`: `broadcast_enabled`, `target_preset` (via `presetFromName`/preset name), `offer_psk`, `message`, `interval_hours`

- [ ] **Step 1:** Add macros and fields. A zeroed old blob must read: broadcast off, target 0 → treat as LongFast only if `PRESET_LONG_FAST == 0`; otherwise the load clamp maps an all-zero tail to defaults — check `PRESET_LONG_FAST`'s value and handle it in the clamp.
- [ ] **Step 2:** Load clamps: `clampBeaconIntervalH`, target `>= PRESET_COUNT` → `PRESET_LONG_FAST`, `beaconCopyMessage` on the message.
- [ ] **Step 3:** YAML export/import for the five keys; import goes through the same clamps.
- [ ] **Step 4: Verify** `pio run -e tdeck` builds; export a config (web export or serial cfg dump) and confirm the six `mesh_beacon` keys appear.
- [ ] **Step 5: Checkpoint.**

(Last-sent lives in Task 4, in `main_lvgl.cpp`: Preferences namespace `"camillia"`, key `"mbLastSent"`, `uint32_t` epoch seconds.)

### Task 3: `ChannelMgr::sendMeshBeacon`

**Files:** `src/channel_mgr.h`, `src/channel_mgr.cpp` (model on `sendNodeInfoFrame`, ~1247)

**Interfaces — Consumes:** `encodeMeshBeaconData` (Task 1). **Produces:**
`bool ChannelMgr::sendMeshBeacon(uint32_t myNodeId, const uint8_t *payload, size_t len, const ChannelKey &key);`
Caller fills `key.key/keyLen/hash`. Encrypt with `encryptChannelPayload(key, packetId, myNodeId, 0xFFFFFFFF, …)`; header `channel = key.hash`, `flags = meshOriginHopFlagsCapped(0)`, `relay_node = myNodeId & 0xFF`; `Radio.transmit`; no `uplinkSelfFrame`; Live feed line `"T BCN B <hash> OK|ER"`; mark the packet id seen the same way other self-sent packets are, if such a hook exists in `channel_mgr.cpp` (otherwise nothing — our own echo is zero-hop and not relayed back).

- [ ] **Step 1:** Implement; `pio run -e tdeck` builds.
- [ ] **Step 2: Checkpoint.**

### Task 4: Retune helper + send cycle

**Files:** `src/main_lvgl.cpp` — Discovery code ~39475 (`discoveryTuneToPreset`) and ~39513 (`discoveryEndPresetScan`); `loop()`; `onWebCfgSaved` / config save path.

**Interfaces — Produces:**
```cpp
struct TempTuneSnapshot { float freq, bw; uint8_t sf, cr; bool renamed; const char *prevName; char prevBuf[16]; };
static void tempTuneEnter(uint8_t preset, TempTuneSnapshot &snap, bool renameCh0);
static void tempTuneLeave(TempTuneSnapshot &snap);
static void serviceMeshBeacon();                 // in loop()
static void meshBeaconSettingsChanged();         // invalidate payload cache; notBefore = now + 120000
struct MeshBeaconStatus { enum { OFF, WAITING, HELD_SCAN, HELD_AIRTIME, HELD_PRESET } state; uint32_t nextInMs; uint32_t lastEpoch; };
static MeshBeaconStatus meshBeaconStatus();      // for the Beacons screen (Task 5)
```

- [ ] **Step 1:** Lift Discovery's snapshot/rename/reconfigure into `tempTuneEnter/Leave`; Discovery calls them with `renameCh0 = true` and keeps its own `s_presetScan*` timers/logging. Restore order in Leave: name+hashes, `dropPendingRebroadcasts()`, `Radio.reconfigure(...)` (only if `s_radioReady`).
- [ ] **Step 2:** Build; Discovery preset scan still retunes and returns (serial log, user-run).
- [ ] **Step 3:** Implement `serviceMeshBeacon()` per spec §2: gates → holds → `beaconDueInMs` → write `mbLastSent` (epoch when clock valid; RAM flag otherwise) → build `MeshBeaconTx` (offer from channel 0 effective name, PSK only if `meshBeaconOfferPsk`, preset omitted when on custom modem params, region via `regionCodeToMeshtastic`, slot only when actual slot ≠ derived slot) → same-radio check → `tempTuneEnter(target, snap, false)` / `sendMeshBeacon` / `tempTuneLeave(snap)` unconditionally → reset retry, log `[beacon] sent on <preset> <MHz> (N bytes, offer ch=<name>, psk=yes|no, retune=yes|no)`.
- [ ] **Step 4:** Boot: read `mbLastSent`; if present and clock valid, map into the millis domain (`lastSentMs = millis() - min(elapsed, interval)*1000`, never 0 → use 1); if present but no clock, `lastSentMs = millis()` (or 1). Call `meshBeaconSettingsChanged()` from the config save path and from `onWebCfgSaved`.
- [ ] **Step 5:** `pio run -e tdeck` builds.
- [ ] **Step 6: Checkpoint.**

### Task 5: Device UI and Beacons screen

**Files:** `src/main_lvgl.cpp` — `CFG_ACTION_MESH_BEACON` label (~6047) and its action handler; `openBeaconsModal`; empty/off text (~36902); beacon card rendering; `src/i18n_builtin.h`.

- [ ] **Step 1:** Row label: "Mesh Beacons: Off" / "Listen" / "Broadcast" / "Listen · Broadcast".
- [ ] **Step 2:** Enter opens a popup listing six rows (build it like the existing option-list modals, e.g. `openCfgKbBacklightModal` at ~18287, so key/trackball/d-pad/touch navigation is inherited): Listen (toggle, keeps today's clear-on-off), Broadcast (toggle), Target preset (option list filtered by `presetUsableOnThisRadio`), Include key (toggle; when on, a warning line "Anyone on %s can join %s."), Message (text entry like `openCfgNodeNameModal`, 60-byte limit), Interval (option list 1/2/3/6/12/24 h). Each change: `markConfigDirty()` then `meshBeaconSettingsChanged()`.
- [ ] **Step 3:** Beacons screen status line above the list from `meshBeaconStatus()`: "Your beacon: off" / "Your beacon: %s · next in %u min · last %s" / "Your beacon: held — Discovery scan" / "airtime" / "%s not available in %s". Update the receive-only wording in the empty and off texts. Card shows "slot %u" when `hasOfferSlot`.
- [ ] **Step 4:** Add every new string to `src/i18n_builtin.h` with es/fr.
- [ ] **Step 5:** `pio run -e tdeck` builds. **Checkpoint.**

### Task 6: Web config

**Files:** `src/web_config.cpp` — form ~5004, POST ~8608.

- [ ] **Step 1:** Under "Listen for beacons" add: Broadcast (select), Target preset (select of usable presets), Include key (select + the same warning text), Message (`maxlength=60` input), Interval (select 1/2/3/6/12/24). Follow the existing `lite` gating.
- [ ] **Step 2:** POST: names `mesh_beacon_broadcast`, `mesh_beacon_target`, `mesh_beacon_offer_psk`, `mesh_beacon_message`, `mesh_beacon_interval`; apply the Task 2 clamps.
- [ ] **Step 3:** `pio run -e tdeck` builds. **Checkpoint.**

### Task 7: Docs + final verification

**Files:** `docs/USE.md` ("Mesh beacons" and "Beacons" sections), the spec file (status → implemented).

- [ ] **Step 1:** Rewrite the USE.md sections per spec §5.
- [ ] **Step 2:** `tests/run.sh` (all suites) → PASS; `pio run -e tdeck` → SUCCESS.
- [ ] **Step 3:** Hand the on-air checklist from spec §6 to the user (stock 2.8.x on LongFast sees it; home preset restored; nothing sent during a Discovery scan).
- [ ] **Step 4:** Stop. Commit/push only on the user's go-ahead (composite PR rules in the repo instructions).
