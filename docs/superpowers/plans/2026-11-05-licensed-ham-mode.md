# Licensed (amateur) mode — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a licensed operator run Camillia under Meshtastic's licensed ("ham") rules: call-sign identity, `User.is_licensed`, no channel keys, no PKI, signed plaintext DMs, licensed-only relaying, ham regions (70 cm / 1.25 m), lifted regulatory power clamp. The mode is interoperable with Meshtastic 2.8 licensed nodes.

**Architecture:** A new pure module `src/licensed_mode.{h,cpp}` holds all decision logic and the process-wide "licensed" switch, and is host-tested. It is set once at boot from `s_cfg.licensedMode`, because every mode change reboots. Every other rule reads `lic::active()`, so there is exactly one switch.

- The protocol layers add a few small hooks: `mesh_proto` for field 6 and unicast signing, `sig_policy` for unicasts, and `node_db` for `isLicensed`.
- Config and regions are in `config_io`.
- The runtime rules (RX, relay, DM, MQTT, admin, beacon) live in `main_lvgl.cpp` and `dm_mgr.cpp`.
- A TX inhibit is added in `MeshRadio::transmit`.
- The UI and web follow existing patterns.

**Tech Stack:** C++17, Arduino/ESP32 (PlatformIO), LVGL, host tests via `tests/run.sh`.

**Spec:** `docs/superpowers/specs/2026-11-05-licensed-mode-design.md` (issue #142 body + the user's decisions + codebase-fact adjustments at its top).

## Global Constraints

- **Builds:** verification is `pio run -e tdeck` only, plus `tests/run.sh`. Never flash.
- **No commits or pushes.** The working tree carries unrelated uncommitted work, including the #141 beacon and Tanmatsu changes, in the same files. Never revert, reformat or "clean up" code you did not write for this task.
- **cardputer-cap:**
  - `FEATURE_LICENSED_MODE` is 0 when `DEVICE_CARDPUTER_LORA_HAT` and 1 otherwise. It is defined in `src/config.h` using the `FEATURE_DISCOVERY` pattern (`config.h:417`).
  - With it 0, `lic::active()` is always false, and no licensed UI or web section is shown. The ham regions stay unselectable there, because they are licensed-only.
- **New `RhinoConfig` fields:**
  - Append at the very end, after `_reservedPad21[3]`, following the append rule and the `static_assert` pattern in `src/config_io.h`.
  - Fields: `bool licensedMode; char callSign[16]; bool licTxHold;`, then padding so that `sizeof` stays 4-aligned and is asserted.
  - Defaults come from `MY_LICENSED_MODE 0` and `MY_CALL_SIGN ""` in `config.h`, and `licTxHold = false`.
- **Licensed means `lic::active()`.** Nothing else reads `s_cfg.licensedMode` at runtime, except the enter/exit transactions, persistence and the UI.
- **Call sign:**
  - 1–15 chars after trimming spaces.
  - Only ASCII letters, digits and `/`; stored uppercased.
  - `N0CALL` (case-insensitive) is valid but inhibits TX.
- **Long name:** `CALL`, or `CALL//suffix` when the suffix is non-empty. It is truncated on a UTF-8 boundary to fit `nodeLong[40]` (39 bytes + NUL).
- **Station ID:** the NodeInfo interval while licensed is **600 s**, applied at runtime. `s_cfg.nodeInfoIntervalS` is never overwritten, so turning the mode off restores the user's value. The UI shows the interval as locked while licensed.
- **Relaying while licensed:**
  - Undecoded packets are never relayed.
  - Packets whose `from` or `to` is a node *known* unlicensed are neither processed nor relayed.
  - Nodes of unknown status pass.
  - Effective rebroadcast mode is LOCAL_ONLY (`2`): `enterLicensedMode` stores `rebroadcastMode = 2`, and boot forces it to 2 while licensed.
- **Signing:**
  - Broadcasts are signed as today.
  - While licensed, unicasts are signed too, except `ROUTING_APP` (5) and `TRACEROUTE_APP` (70).
- **Power:**
  - While licensed, TX power may be 1..`MESH_POWER`.
  - Leaving licensed mode sets `loraPower = min(loraPower, regionPower(region))`.
  - Per-board limits are untouched; `MESH_POWER` is the hardware ceiling.
- **Ham regions:**
  - Band edges, padding and override slot are copied from upstream `9bcf0f7` `RadioInterface.cpp:293-349`.
  - `ITU1_2M`, `ITU2_2M` and `ITU3_2M` are in the table, but never selectable on any current radio, because their `freqStart` is below 150 MHz.
- **MQTT:** downlink onto RF is refused while licensed. Uplink is unchanged.
- **Out of scope:**
  - Incoming `set_ham_mode`, because Camillia has no phone API or admin server.
  - Per-board RF-band declarations; the antenna warning is generic text.
- **Strings:**
  - User-facing device strings use `TR()`, with entries added to `lang/es.lang` and `lang/fr.lang` in the existing `English<TAB>translation` format. `src/i18n_builtin.h` is generated; never edit it.
  - `web_config.cpp` does not use `TR()`.
  - Fonts lack U+2014: use " - " instead of an em dash.
- **New `.cpp` files** are added to `build_src_filter` in **all 12 device envs and `[env:native]`** in `platformio.ini`.

## Review Focus

1. **A known-unlicensed peer leaks through relay or processing.** Test: `test_packet_gate_table` (Task 1) covers all nine from × to status pairs. Task 4 wires `lic::allowPacket` before *both* `processMeshPacket` handling and `maybeRebroadcastPacket`.
2. **A NodeInfo mismatch still updates the node DB, including via the cached path or the MQTT path.** Test: `test_accept_nodeinfo` (Task 1). Task 4 places the gate before `Nodes.updateUser` (~main_lvgl.cpp:58100).
3. **An unlicensed device lands on a ham region** via UI, web, YAML, the CLI `cfg region=` or an inbound Meshtastic region code. Test: `test_region_selectable` (Task 1). Task 3 adds `cfgCoerceLicensed()` on every config-apply path as the backstop.
4. **A signed unicast changes the wire format for unlicensed users.** `lic::shouldSign` must return the old behaviour (broadcast only) when not licensed. Test: `test_should_sign` (Task 1) and the `test_mesh_proto` unicast-unsigned-when-unlicensed case (Task 2).
5. **UTF-8 at the 39-byte long-name edge.** Test: `test_compose_long_name_utf8_edge` (Task 1).
6. **Leaving the mode while on a ham region.** The radio must not transmit until the user resumes TX: `licTxHold` is persisted and enforced in `MeshRadio::transmit`. This is a manual check in Task 4, step 6.

---

### Task 1: Pure licensed-mode module

**Files:**
- Create: `src/licensed_mode.h`, `src/licensed_mode.cpp`. No Arduino headers; only `<stdint.h>`, `<stddef.h>`, `<string.h>`, `<ctype.h>`.
- Create: `tests/test_licensed_mode.cpp` and `tests/native/test_licensed_mode/test_main.cpp`. Copy the wrapper from `tests/native/test_mesh_beacon/test_main.cpp`, renaming the include and the test.
- Modify: `platformio.ini`. Add `+<licensed_mode.cpp>` to every env's `build_src_filter`, next to `+<mesh_beacon.cpp>`, including `[env:native]`.

**Interfaces — Produces:**
```cpp
namespace lic {
enum PeerStatus : uint8_t { PEER_UNKNOWN = 0, PEER_LICENSED = 1, PEER_UNLICENSED = 2 };

void setActive(bool on);          // boot only; also what tests use
bool active();                    // the one "am I licensed" switch

constexpr uint32_t kStationIdIntervalS = 600;
constexpr uint8_t  kRebroadcastLocalOnly = 2;
constexpr size_t   kCallSignMax = 15;   // chars, excluding NUL

// Trims, uppercases, validates [A-Z0-9/], 1..15 chars. Returns false (out untouched) if invalid.
bool normalizeCallSign(const char *in, char *out, size_t outCap);
bool isNoCall(const char *callSign);    // "N0CALL", case-insensitive

// "CALL" or "CALL//suffix"; truncated to outCap-1 bytes on a UTF-8 boundary. Returns length.
size_t composeLongName(const char *call, const char *suffix, char *out, size_t outCap);

PeerStatus peerStatus(bool known, bool isLicensed);
// While self licensed: false if from or to is PEER_UNLICENSED. Always true when not licensed.
bool allowPacket(bool selfLicensed, PeerStatus from, PeerStatus to);
// Relay gate added on top of the existing rules: not licensed -> true;
// licensed -> decoded && allowPacket(...).
bool allowRelay(bool selfLicensed, bool decoded, PeerStatus from, PeerStatus to);
// broadcast -> true; unicast -> selfLicensed && port != 5 && port != 70.
bool shouldSign(bool selfLicensed, bool broadcast, uint32_t portnum);
bool acceptNodeInfo(bool selfLicensed, bool peerLicensed);   // equal
// licensedOnly regions need selfLicensed; any region needs freqStartMhz >= 150.
bool regionSelectable(bool licensedOnly, float freqStartMhz, bool selfLicensed);
constexpr float kRadioMinMhz = 150.0f;
}
```

- [ ] **Step 1:** Write `tests/test_licensed_mode.cpp`. Use the `check()` helper and `main()` shape from `tests/test_mesh_beacon.cpp`, with no test-deps header beyond `// test-deps: licensed_mode.cpp` (check how `tests/run.sh` reads test-deps). Cases:
  - `test_normalize_call_sign`:
    - `" w1aw "` → `"W1AW"`
    - `"VE3/W1AW"` is ok
    - `""`, `"   "`, `"W1 AW"`, `"W1AW!"` and a 16-char string → false
    - exactly 15 chars → ok
  - `test_is_no_call`: `"N0CALL"`, `"n0call"` → true; `"N0CAL"` → false.
  - `test_compose_long_name`:
    - (`"W1AW"`, `""`) → `"W1AW"`
    - (`"W1AW"`, `nullptr`) → `"W1AW"`
    - (`"W1AW"`, `"Base"`) → `"W1AW//Base"`
  - `test_compose_long_name_utf8_edge`: `outCap` 40, call `"W1AW"`, suffix of 40 × `"é"` (2 bytes each). The result is ≤ 39 bytes, ends on a code-point boundary (last byte is not `0xC3`), and starts `"W1AW//"`.
  - `test_packet_gate_table`: all 9 from × to combinations with self licensed. Only pairs with no `PEER_UNLICENSED` pass. With self not licensed, all 9 pass.
  - `test_allow_relay`:
    - not licensed and undecoded → true
    - licensed and undecoded → false
    - licensed, decoded, unknown/unknown → true
    - licensed, decoded, from unlicensed → false
  - `test_should_sign`:
    - broadcast → true, either way
    - unicast, not licensed → false
    - unicast, licensed, port 1 → true
    - unicast, licensed, port 5 → false
    - unicast, licensed, port 70 → false
  - `test_accept_nodeinfo`: true for (t,t) and (f,f); false for (t,f) and (f,t).
  - `test_region_selectable`:
    - (false, 902, false) → true
    - (true, 420, false) → false
    - (true, 420, true) → true
    - (true, 144, true) → false
    - (false, 2400, false) → true
  - `test_active_switch`: `setActive(true)` → `active()`; `setActive(false)` → `!active()`.
- [ ] **Step 2:** Run `tests/run.sh`. The new suite fails to build or link; that is expected.
- [ ] **Step 3:** Implement `licensed_mode.{h,cpp}` with the interfaces above. `composeLongName` steps back over UTF-8 continuation bytes (`(b & 0xC0) == 0x80`), and drops the lead byte too if its sequence would be cut. You may reuse `utf8util::copyTruncate` from `src/utf8_utils.h` if it is host-pure; otherwise implement inline.
- [ ] **Step 4:** Run `tests/run.sh`. All suites pass, including the new one.
- [ ] **Step 5:** Run `pio run -e tdeck`; expect SUCCESS. Then `git diff --stat` and write the report.

### Task 2: Protocol — User field 6, NodeEntry, unicast signing, sig policy

**Files:** `src/mesh_proto.h`, `src/mesh_proto.cpp`, `src/node_db.h`, `src/node_db.cpp`, `src/sig_policy.h`, `tests/test_mesh_proto.cpp`, `tests/test_sig_policy.cpp`

**Interfaces — Consumes:** `lic::shouldSign`, `lic::active` (Task 1).

**Interfaces — Produces:**
- `encodeNodeInfo(..., bool wantResponse, uint32_t bitfield, bool isLicensed = false)`. When true, it writes User field 6 (varint 1), placed between field 5 and field 7. Field 8 (`public_key`) is still sent in both cases. Replace the outdated comment at `mesh_proto.cpp:~1111` with one saying upstream 2.8 keeps the key on licensed nodes for node ID and signing.
- `UserInfo` gains `bool isLicensed;` (default false). `decodeUser` reads field 6.
- `NodeEntry` gains `bool isLicensed; bool licensedKnown;`.
  - `Nodes.updateUser` sets both: `licensedKnown = true` once any NodeInfo was decoded.
  - Name-cache or other non-NodeInfo creation paths leave `licensedKnown = false`.
  - Add `lic::PeerStatus NodeDB::licenseStatus(uint32_t nodeId) const`. It returns `PEER_UNKNOWN` for unknown nodes, broadcast `0xFFFFFFFF` and nodes with `!licensedKnown`.
- `signBroadcastData`:
  - Its gate becomes `if (fromNode == 0 || fromNode != xeddsaSignerNodeId() || !lic::shouldSign(lic::active(), toNode == 0xFFFFFFFF, <portnum of plain>)) return 0;`.
  - Use the portnum the function already parses ("no port number" rejection).
  - Rename it to `signData` only if the change stays trivially local; otherwise keep the name and update its comment.
- `SigPolicyInput` gains `bool unicastSignable;` as the **last** member, so existing aggregate inits stay valid. The Balanced rule becomes `if (in.knownSigner && (in.broadcast || in.unicastSignable))`. Update the ham note at `sig_policy.h:~109`.

- [ ] **Step 1: tests first.**
  - In `tests/test_mesh_proto.cpp`:
    - NodeInfo encode with `isLicensed=true` → `decodeUser` gives `isLicensed == true`, with the pubkey still present.
    - Encoding with `false` → `isLicensed == false`.
    - Unicast text from the signer node with `lic::setActive(false)` is unsigned (`encryptChannelPayload` output length equals the unsigned length).
    - With `lic::setActive(true)` the same unicast gets signed. Use the signature-presence check the existing broadcast-signing test uses, if one exists. Otherwise test `lic::shouldSign` integration via the encrypt path the existing tests use; read the file first.
    - A ROUTING_APP unicast stays unsigned while licensed.
    - Add `licensed_mode.cpp` to that file's `// test-deps:` line.
  - In `tests/test_sig_policy.cpp`: Balanced, `knownSigner`, `!broadcast`, `unicastSignable=true`, unsigned, small `dataLen` → reject. The same with `unicastSignable=false` → accept.
- [ ] **Step 2:** Run `tests/run.sh`; the new cases fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run `tests/run.sh`; all pass. Run `pio run -e tdeck`; expect SUCCESS. Callers of `encodeNodeInfo` still pass the default `false`; Task 4 wires the real value.
- [ ] **Step 5:** `git diff --stat`, then write the report.

### Task 3: Config, YAML, regions, coercion

**Files:** `src/config.h`, `src/config_io.h`, `src/config_io.cpp`, `src/mesh_channel_plan.h`

**Interfaces — Consumes:** `lic::normalizeCallSign`, `lic::regionSelectable`, `lic::kRebroadcastLocalOnly` (Task 1).

**Interfaces — Produces:**
- `config.h`:
  - `FEATURE_LICENSED_MODE`: 0 on `DEVICE_CARDPUTER_LORA_HAT`, else 1, with a short comment saying why (no ham band is useful on that board's radio, and the Cardputer heap is tight; see the `FEATURE_DISCOVERY` comment).
  - `MY_LICENSED_MODE 0` and `MY_CALL_SIGN ""`, both inside `#ifndef` guards like the neighbours.
- `RhinoConfig`: append `bool licensedMode; char callSign[16]; bool licTxHold;` plus padding and `static_assert`s (Global Constraints). Defaults go in the config-defaults function (`config_io.cpp:~1214`).
- `RegionPlan` gains, at the end, `bool licensedOnly; bool tinyPresets;`. Existing rows keep their 7 initialisers; aggregate init zero-fills the rest.
- `kRegions` gains these rows, appended after `EU_N_868` (spacing 0; pwr 22 = `MESH_POWER`-level hardware ceiling, like other rows):

  | code | start | end | pwr | padding | slot | licensedOnly | tiny |
  |---|---|---|---|---|---|---|---|
  | ITU1_2M | 144.0 | 146.0 | 22 | 0.0022 | 26 | true | true |
  | ITU2_2M | 144.0 | 148.0 | 22 | 0.0022 | 51 | true | true |
  | ITU3_2M | 144.0 | 148.0 | 22 | 0.0022 | 33 | true | true |
  | ITU2_125CM | 220.0 | 225.0 | 22 | 0.01875 | 37 | true | false |
  | ITU1_70CM | 430.0 | 440.0 | 22 | 0.01875 | 37 | true | false |
  | ITU2_70CM | 420.0 | 450.0 | 22 | 0.01875 | 137 | true | false |
  | ITU3_70CM | 430.0 | 450.0 | 22 | 0.01875 | 37 | true | false |

  Add a comment block citing upstream `9bcf0f7 RadioInterface.cpp:286-349`, and noting that 2 m sits below every current radio's 150 MHz floor.
- `kRegionFromMeshtastic`: map 27→`"ITU1_2M"`, 28→`"ITU2_2M"`, 33→`"ITU3_2M"`, 34→`"ITU1_70CM"`, 35→`"ITU2_70CM"`, 36→`"ITU3_70CM"`, 37→`"ITU2_125CM"`. Rewrite the comment at `config_io.cpp:~327` to say that adoption is gated by `cfgCoerceLicensed`.
- New, declared in `mesh_channel_plan.h`:
  - `const RegionPlan *regionPlan(const char *code);` — a public wrapper over the static `regionLookup`.
  - `bool regionUsableOnThisRadio(const char *code);` — region exists && `freqStart >= lic::kRadioMinMhz`.
  - `bool regionSelectable(const char *code, bool selfLicensed);` — wraps `lic::regionSelectable`.
  - `bool presetAllowedInRegion(const char *code, uint8_t preset);` — non-ham regions → true. Ham regions with `!tinyPresets` → `PRESET_NARROW_FAST`/`PRESET_NARROW_SLOW` only. Ham regions with `tinyPresets` → the two Tiny presets only (look up their enum names in `mesh_channel_plan.h`).
  - `uint8_t regionDefaultPreset(const char *code);` — `PRESET_NARROW_SLOW` for 100 kHz ham regions, the Tiny Fast preset for 2 m, `PRESET_LONG_FAST` otherwise.
- `applyPresetParams`: after the existing usable check, `if (cfg.loraUsePreset && !presetAllowedInRegion(cfg.region, cfg.modemPreset)) cfg.modemPreset = regionDefaultPreset(cfg.region);`.
- New `void cfgCoerceLicensed(RhinoConfig &cfg);`, declared in `config_io.h`, and called wherever `cfgCoerceMeshBeacon` is called (same paths: load, YAML import, web save, NVS load). It does:
  1. If `!FEATURE_LICENSED_MODE`, `licensedMode = false`.
  2. Normalise `callSign`, or clear it if invalid. If `licensedMode` and `callSign` is empty, set `licensedMode = false`.
  3. If `!regionSelectable(cfg.region, cfg.licensedMode)`, set the region to `MY_REGION` and set `licTxHold = true` when the region was a ham region.
  4. If `licensedMode`, set `rebroadcastMode = 2`.
- YAML:
  - Export and import flat keys `owner_licensed: true|false` and `owner_call_sign: <str>`, next to `owner`/`owner_short` (`config_io.cpp:~2022`, `~2285`).
  - **Ruling:** flat keys, matching the file's existing flat `owner_short` style, rather than the issue's nested `owner: {…}`.
  - `licTxHold` is not exported.
  - The Meshtastic-format region import (`config_io.cpp:~2492`) goes through `cfgCoerceLicensed` like everything else.

- [ ] **Step 1:** Implement the config fields, defaults, the asserts and `FEATURE_LICENSED_MODE`.
- [ ] **Step 2:** Implement the regions, the helpers and `applyPresetParams`.
- [ ] **Step 3:** Implement `cfgCoerceLicensed` and wire it at every `cfgCoerceMeshBeacon` call site (`grep -n cfgCoerceMeshBeacon src/`).
- [ ] **Step 4:** Implement the YAML keys.
- [ ] **Step 5:** Run `tests/run.sh`, all pass. Run `pio run -e tdeck`, expect SUCCESS. Also grep that every `kRegions` iteration in `main_lvgl.cpp` and `web_config.cpp` still compiles; filtering them is Tasks 5–6.
- [ ] **Step 6:** `git diff --stat`, then write the report.

### Task 4: Runtime — boot, transactions, TX inhibit, packet rules

**Files:** `src/main_lvgl.cpp`, `src/dm_mgr.cpp`, `src/mesh_radio.h`, `src/mesh_radio.cpp`, `src/admin_client.cpp` (only if the refusal belongs there)

**Interfaces — Consumes:** all of Tasks 1–3.

**Interfaces — Produces (in `main_lvgl.cpp`, static unless noted):**
- `MeshRadio::setTxInhibit(bool)` and `bool txInhibited() const`. `transmit()` returns false without keying when inhibited, logging `[radio] tx inhibited` at most once per 10 s.
- `bool enterLicensedMode(const char *callSign, const char *longSuffix, uint8_t txPower, char *err, size_t errCap)`. It is the transaction from spec §1:
  1. Normalise the call sign; refuse if invalid. When `!FEATURE_LICENSED_MODE`, refuse.
  2. `composeLongName` into `s_cfg.nodeLong`.
  3. For every `CHANNEL_KEYS` entry: `keyLen = 0` and the key is zeroed. A channel whose name equals `"admin"` (case-insensitive) gets `role = 2`.
  4. Set `rebroadcastMode = 2`.
  5. Set `loraPower = clamp(txPower, 1, MESH_POWER)`.
  6. Set `licensedMode = true`, `licTxHold = false`.
  7. Persist the config and channels using the same calls the existing identity-change path uses (find it at `main_lvgl.cpp:~47621`).
  8. Reboot via `ESP.restart()` after the existing short delay pattern there.
- `void exitLicensedMode()`:
  1. Set `licensedMode = false`.
  2. If the current region is `licensedOnly`, set region `MY_REGION`, `licTxHold = true` and `applyPresetParams`.
  3. Set `loraPower = min(loraPower, regionPower(region))`.
  4. Persist and reboot.
  
  Channel keys are not restored.
- `void resumeTxAfterLicensedExit()`: clears `licTxHold`, persists and calls `Radio.setTxInhibit(false)`. There is no reboot.
- Boot, placed early after config and channel load, before radio init or right after:
  1. `lic::setActive(FEATURE_LICENSED_MODE && s_cfg.licensedMode)`.
  2. If active, re-run the channel strip and admin-disable, and force `rebroadcastMode = 2`. Persist channels only if anything changed; this covers a backup restore or a QR import from before.
  3. `Radio.setTxInhibit(s_cfg.licTxHold || (lic::active() && lic::isNoCall(s_cfg.callSign)))`.
- NodeInfo interval: the announce scheduler (`announceIntervalMs`/`scheduleAnnounceNext`, `main_lvgl.cpp:~58530`) uses `lic::active() ? lic::kStationIdIntervalS : s_cfg.nodeInfoIntervalS`. This bypasses any floor or scaling. All `encodeNodeInfo` calls (`grep -n encodeNodeInfo src/`) pass `lic::active()`.
- RX rules in the packet path (around `main_lvgl.cpp:57150-58150`):
  1. **PKI decrypt** (`~57762`): skipped when `lic::active()`.
  2. **Packet gate:** right after decode, before port dispatch, `lic::allowPacket(lic::active(), Nodes.licenseStatus(from), Nodes.licenseStatus(to))`. If false, drop: no processing, no relay. Bump a debug counter `s_licDropUnlicensed`.
  3. **NodeInfo:** before `Nodes.updateUser` (`~58100`), `if (!lic::acceptNodeInfo(lic::active(), u.isLicensed))` skip the update and bump `s_licNodeInfoMismatch`. Do this only for NodeInfo (not other ports), and include the MQTT-downlink path if it reaches the same code.
  4. **Relay:** in `maybeRebroadcastPacket` (`~57210`), add `if (!lic::allowRelay(lic::active(), pkt.decrypted, Nodes.licenseStatus(from), Nodes.licenseStatus(to))) return;`.
  5. **Sig policy input** (`~3140`): `in.unicastSignable = lic::active() && !in.broadcast && lic::shouldSign(true, false, portnum)`.
  6. Expose both counters on the Live/debug screen where other RX drop counters are shown (find e.g. the sig-policy drop counter display), as `Lic drops N / NI mismatch N`.
- `dm_mgr.cpp:~354`:
  - When `lic::active()`, skip the PKI requirement and send the DM channel-encrypted on the primary channel (`hdr.channel` = the primary channel hash, `to = node`), via the same channel-encryption helper `ChannelMgr` uses. Signing happens automatically via Task 2.
  - The PKI path is unchanged when not licensed.
  - Update the comment.
- MQTT: `mqttDownlinkInject` (`~58379`) returns early (drop, debug log once) when `lic::active()`.
- Admin:
  - When `lic::active()`, starting a remote-admin session or probe fails immediately with a transcript error line `!Remote admin unavailable in licensed mode (no PKI)` (use `TR()`), instead of timing out.
  - Find the session/probe start in `main_lvgl.cpp:~56176-56833` and put the check at the single entry point.
- Mesh Beacon (`~63614-63821`):
  - The EU air-util hold is skipped when `lic::active()`.
  - The PSK offer is never included when `lic::active()`, even if `meshBeaconOfferPsk` is set.
  - **Ruling:** the spec's "refuse ham-only target region unless licensed" is already guaranteed, because the beacon only uses the current region, and `cfgCoerceLicensed` keeps an unlicensed device off ham regions. Add a one-line comment; no extra guard.

- [ ] **Step 1:** Add the TX inhibit in `mesh_radio`.
- [ ] **Step 2:** Add boot setup, the transactions and the interval override.
- [ ] **Step 3:** Add the RX and relay gates, the counters and the sig input.
- [ ] **Step 4:** Make the DM, MQTT, admin and beacon changes.
- [ ] **Step 5:** Run `tests/run.sh`, all pass. Run `pio run -e tdeck`, expect SUCCESS.
- [ ] **Step 6:** In the report, list the serial lines a tester should see. These are **manual on-air checks, not run here**:
  - boot with licensed;
  - `tx inhibited` with N0CALL;
  - `tx inhibited` after exiting from a ham region, until resume.
- [ ] **Step 7:** `git diff --stat`, then write the report.

### Task 5: Device UI

**Files:** `src/main_lvgl.cpp`, `lang/es.lang`, `lang/fr.lang`

**Interfaces — Consumes:** `enterLicensedMode`, `exitLicensedMode`, `resumeTxAfterLicensedExit`, `regionSelectable`, `lic::active`.

- Config entry `CFG_ACTION_LICENSED_MODE`, label "Licensed (ham) mode", whose status text shows `Off` or the call sign. Add it only `#if FEATURE_LICENSED_MODE`, following the beacon popup pattern (`~23868`) and the `CFG_ACTION_*` enum/label/handler pattern (`~3739`, `~6063`, `~44915`).
  - The popup has rows:
    - call sign (text entry, max 15)
    - long-name suffix (text entry)
    - TX power (1..`MESH_POWER` dBm, shows the actual dBm)
    - Enable/Disable
    - while `licTxHold`, also "Resume transmitting"
  - **Enable** uses a two-step confirm (the pending-action modal pattern at `~45891`). Text:
    > "Every channel's key will be removed. Encrypted DMs, remote admin and relaying for unlicensed nodes will stop. The device will reboot. You must hold an amateur licence for the band you use. Camillia does not verify your licence."
    
    Then call `enterLicensedMode`, and show `err` on failure, e.g. "Enter a call sign".
  - **Disable** uses a two-step confirm with the text "Channel keys will not come back. The device will reboot.", plus " Region will change to <MY_REGION> and transmit stays off until you resume." when currently on a ham region.
- Region pickers: every region picker iterating `kRegions` lists only regions where `regionSelectable(code, lic::active())` is true. This covers the onboarding picker (`~46869`, including its index/count helpers), the radio-settings region picker and the serial `cfg region=` (`~62665`, which must reject with `err region: requires licensed mode`). When a selected region is ham, show the generic warning "Needs an antenna and front end for this band." under the picker or as a status line.
- Header: when `lic::active()`, show a small `HAM` badge plus the call sign in the status bar, using the existing header label helpers (`screenHeaderSetTitle` area ~43584). Keep it compact: one line, no added height.
- Node list (`~31795`): when `n.licensedKnown && n.isLicensed`, show a `ham` marker next to existing markers.
- Role and rebroadcast settings, and the NodeInfo interval row: while licensed, show "(licensed: local only)" and "(licensed: 10 min)", and lock the values to read-only.
- Channel editor: while licensed, the key field is hidden or disabled, and saving keeps `keyLen = 0`. Any channel import path that sets a key strips it while licensed and shows "Licensed mode: channel imported without its key".
- Add every new `TR()` string to `lang/es.lang` and `lang/fr.lang`.

- [ ] **Step 1:** Implement the config entry, popup and confirms.
- [ ] **Step 2:** Implement the region-picker filtering and the serial cfg rejection.
- [ ] **Step 3:** Implement the header badge, node marker, locked rows and channel editor/import stripping.
- [ ] **Step 4:** Add the lang entries, run `python3 tools/gen_lang_builtin.py`, then `tests/run.sh` and `pio run -e tdeck`; expect SUCCESS.
- [ ] **Step 5:** `git diff --stat`, then write the report.

### Task 6: Web config

**Files:** `src/web_config.cpp`

- A "Licensed (ham) mode" section, `#if FEATURE_LICENSED_MODE`, near the owner/identity fields:
  - **Fields:** an enable checkbox, call sign (`maxlength=15`), long-name suffix and TX power.
  - **On submit:** a JS `confirm()` with the same enable text as Task 5, used only when the checkbox changes from off to on. A disable-confirm text is used when it changes from on to off.
  - **Notes:** show the "Camillia does not verify your licence" note, and show "Resume transmitting" while `licTxHold` (a POST flag).
- **POST `/save` (`~8329`):**
  - A transition off → on calls the same `enterLicensedMode`, exposed via a small function declared in `web_config.h` or the existing callback mechanism; check how web save reaches main_lvgl today, e.g. `onWebCfgSaved`. On failure, show the error and do not save the flag.
  - A transition on → off calls `exitLicensedMode`.
  - With no transition, only the call sign, suffix and power are updated while licensed, and they take effect after the reboot that enter/exit perform. If a call-sign change while licensed is easier to handle as a re-enter, do that.
- **Region `<select>` and JS table** (`~704`, `~914`): append the ham region options and JS band entries. Emit the options only when `regionSelectable(code, licensed)`, matching device UI. Emit the JS table entries for all regions (harmless).
- **Lock rows while licensed:** show the NodeInfo interval as locked ("10 min while licensed"). Show channel PSK inputs disabled, with the server ignoring PSK fields while licensed.

- [ ] **Step 1:** Implement.
- [ ] **Step 2:** Run `pio run -e tdeck`; expect SUCCESS.
- [ ] **Step 3:** `git diff --stat`, then write the report.

### Task 7: Docs

**Files:** `docs/USE.md`, `docs/superpowers/specs/2026-11-05-mesh-beacon-broadcast-design.md`, `docs/superpowers/specs/2026-11-05-licensed-mode-design.md`

- `docs/USE.md` gets a new "Licensed (amateur) mode" section covering:
  - what changes: call sign, no keys, no PKI, signed plaintext DMs, local-only relaying and the unlicensed separation, 10-min station ID, power, ham regions;
  - legal responsibility ("Camillia does not verify your licence");
  - that 2 m is not reachable on any current board;
  - the antenna/front-end warning;
  - that keys don't come back when you turn it off;
  - the TX hold after leaving a ham region, and how to resume;
  - N0CALL for testing;
  - "not available on Cardputer";
  - MQTT downlink refused.
- The #141 spec: replace "no licensed mode, guard not needed" with a line saying licensed mode suppresses the PSK offer and the duty-cycle hold.
- The licensed spec: Status → implemented.

- [ ] **Step 1:** Write the docs.
- [ ] **Step 2:** `git diff --stat`, then write the report.
