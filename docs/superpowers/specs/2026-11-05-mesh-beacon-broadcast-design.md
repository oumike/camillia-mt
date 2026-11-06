# Mesh Beacons: broadcast — design

Date: 2026-11-05
Status: implemented (issue #141, Tasks 1–7)
Issue: #141

## Goal

Camillia already listens for Meshtastic MeshBeacon packets (port 37,
`MESH_BEACON_APP`) and lists them on Live → Tools → Beacons. This adds the
sending side. Once per interval the device retunes briefly onto another mesh's
preset and broadcasts an advertisement of **its own** mesh: "a mesh exists on
this channel / preset / region". Then it returns to its own configuration.

Success means:

- A stock Meshtastic 2.8.x node on the target preset hears the beacon and shows
  its offer.
- Afterwards the home configuration is unchanged. Nothing is written to `s_cfg`
  or NVS by the retune, and the radio is back on its own frequency and
  modulation.

## Reference: upstream

`src/modules/MeshBeaconModule.{h,cpp}` in meshtastic/firmware was checked at
`9bcf0f7` (2026-11-04). The protobufs are `mesh_beacon.proto` and
`module_config.proto` → `MeshBeaconConfig` (`since_firmware: 2.8.0`).

Upstream has this and we match it:

- **Payload** `MeshBeacon`:
  - `message` (1, string, at most 60 bytes on send)
  - `offer_channel` (2, ChannelSettings: name ≤ 12, psk ≤ 32)
  - `offer_region` (3, RegionCode)
  - `offer_preset` (4, optional ModemPreset)
  - `offer_frequency_slot` (5, optional uint32, 1-based). Field 5 is sent
    **only** when a receiver could not derive the slot from region + preset +
    channel name.
- **Packet:** broadcast, `hop_limit = 0`, BACKGROUND priority, no ack,
  encrypted with the target channel's key. A blank-named channel is hashed
  under the *target* preset's name.
- **Interval:** 3600 s minimum and default.
- **Throttle:** the last-sent time survives a reboot, and is recorded *before*
  transmitting.
- **Air-util:** backoff from 5 min, doubling to 60 min, while TX air-util is
  over budget.
- **Role:** no beacon in CLIENT_HIDDEN.
- **Guards:** never substitute a different preset or slot for one that
  cannot run. (The beacon is disabled while licensed; see Section 2.)
- **Receivers:** never auto-apply an offer. We already honour this.

Upstream has this and we leave it out (YAGNI; each can be added later):

- multiple broadcast targets (up to 4) and target dedup
- pinned frequency slots, both per target and on the offer
- hand-edited offer fields
- `FLAG_LEGACY_SPLIT` (separate TEXT_MESSAGE_APP copy of the text)
- the legacy `hop_start = 1` override for firmware older than 2.7.20

Upstream's sidecar table and `RadioTxHook` exist because its radio TX is
queued and asynchronous. Camillia's `Radio.transmit()` is blocking, so the
retune → transmit → restore sequence runs synchronously in one function and
none of that machinery is needed.

## Decisions (from brainstorming)

1. **Scope:** advertise my mesh onto another mesh, retuning as needed.
2. **Offer:** automatic from the current setup. It carries the primary
   channel's name, my preset and my region. A toggle includes the primary
   channel's PSK, **default off**: a private key in a public beacon is a public
   key.
3. **Target:** one target. It's a chosen preset (default LongFast), in my own
   region, on that preset's public default channel: the default key, with the
   channel named after the preset.
4. **Message:** optional, empty by default, up to 60 bytes.
5. **Legacy split:** not included.
6. **Controls:** device Config screen, web config, and cfg YAML/CLI.
7. **Beacons screen:** a status line for my own beacon. There is no "send now"
   key.
8. **Interval:** a picker with 1/2/3/6/12/24 h, default 1 h.
9. **Structure:** a new pure module plus one shared retune helper (approach 1).

## 1. Components

### `src/mesh_beacon.{h,cpp}` (new, no Arduino headers)

Pure logic, host-testable:

- `size_t encodeMeshBeacon(const MeshBeaconTx &in, uint8_t *out, size_t cap)`.
  It encodes fields 1–5 in field order. Empty or absent fields are omitted, as
  proto3 does. `offer_preset` is written whenever `hasOfferPreset` is true,
  value 0 (LONG_FAST) included, because it is `optional`. Field 5 is written
  only when `hasOfferSlot`. The ChannelSettings submessage carries `psk` (2)
  only when included, and `name` (3).
- `MeshBeaconTx`: message, offer channel name, optional PSK, Meshtastic
  RegionCode, Meshtastic ModemPreset, optional slot. Callers translate from
  camillia's enums with the existing `presetToMeshtastic()` /
  `regionCodeToMeshtastic()`, never by casting.
- Schedule maths, all as functions of `(nowMs, lastSentMs, intervalH,
  retryMs)` so tests need no clock:
  - `beaconDueInMs(...)` returns 0 when due
  - `nextAirtimeRetryMs(prev)`: 0 → 5 min, then ×2, capped at 60 min
  - `clampBeaconIntervalH(h)`: snaps to {1,2,3,6,12,24}, with 0 or invalid
    becoming 1

The existing `decodeMeshBeacon()` (`src/mesh_proto.cpp`) gains field 5:
`hasOfferSlot` / `offerSlot` in `MeshBeaconPayload`.

### Shared retune helper (`src/main_lvgl.cpp`)

The body of `discoveryTuneToPreset()` / `discoveryEndPresetScan()` is lifted
into one helper pair:

- `tempTuneEnter(uint8_t preset, TempTuneSnapshot &snap)`:
  1. snapshot freq/bw/sf/cr
  2. if `primaryChannelNameIsPresetDefault()`, rename channel 0 to the target
     preset's channel name and run `recomputeChannelHashes()`
  3. `dropPendingRebroadcasts()`
  4. `Radio.reconfigure()` to `regionSlotFreq(region, p.bw, p.channelName)` and
     the preset's bw/sf/cr
- `tempTuneLeave(TempTuneSnapshot &snap)` undoes it in reverse: restore the
  name and hashes, `dropPendingRebroadcasts()`, `Radio.reconfigure()` back to
  the snapshot.

Neither function writes `s_cfg` or calls `markConfigDirty()`. That is the
invariant the Discovery comment already documents. Discovery's existing
callers keep their behaviour, logging and timers; only the radio and channel
manipulation moves.

The channel-0 rename is a parameter, `renameCh0`. Discovery passes `true`,
because its sweep is an ordinary channel-0 send. The beacon passes `false`.
It never goes out on channel 0, which may hold a private key. Instead it
encrypts with the public default key and stamps
`computeChannelHash(presetChannelName, defaultKey)` itself (Section 2).

### `Channels.sendMeshBeacon(...)` (`src/channel_mgr.{h,cpp}`)

`bool sendMeshBeacon(uint32_t myNodeId, const uint8_t *payload, size_t len,
const uint8_t *key, size_t keyLen, uint8_t channelHash)`

- builds a Data message with portnum 37 and `bitfield` like other sends
- encrypts with the given key
- header: to = broadcast, `hop_limit = 0`, `hop_start = 0`, no want_ack,
  `channel = channelHash`
- not uplinked to MQTT (`okToMqtt = false`); the beacon is for the other RF
  mesh
- transmits with the existing blocking path, then re-arms RX

The packet is not added to our own rebroadcast or dedup cache as something to
relay. It is still marked as seen so our own echo is ignored.

## 2. Send cycle

`serviceMeshBeacon()` is called from `loop()`, cheap when nothing is due.

1. **Gate.** Do nothing when any of these hold:
   - broadcast is off
   - the radio isn't ready
   - the radio mode is not a normal LoRa mesh mode (e.g. MQTT-only builds or
     modes where RF is off)
   - the role is CLIENT_HIDDEN

   **Hold**, with the reason shown on the status line, when any of these hold:
   - a Discovery preset scan is active (`s_presetScanActive`); retry when it
     ends
   - TX air-util is over budget; retry with `nextAirtimeRetryMs`. Upstream's
     rule is followed: a limit applies only in duty-cycle regions (EU_433,
     EU_868 at 10 %), at half the duty cycle, so beacons are held while
     `Radio.airUtilTxPercent() >= 5`. Elsewhere there is no air-util hold,
     as upstream.
   - the target preset is not runnable in the current region
     (`presetUsableOnThisRadio()` and the region's preset table); it is never
     swapped for another

   Licensed mode (issue #142): the beacon is disabled while licensed, as it
   is sealed with the default channel key and nothing may go out encrypted.
2. **Due?** Check `beaconDueInMs(now, lastSent, intervalH, retry) == 0`.
3. **Record** last-sent to its own NVS key first (Section 3), then send.
4. **Payload:**
   - `message` = `s_cfg.meshBeaconMessage` (may be empty)
   - offer channel name = channel 0's effective name
   - offer PSK = channel 0's key, only when `meshBeaconOfferPsk`
   - offer preset = `presetToMeshtastic(s_cfg.modemPreset)` when on a preset.
     On custom modem params there is no preset to offer, so `offer_preset` is
     omitted and only channel + region go out.
   - offer region = `regionCodeToMeshtastic(s_cfg.region)`
   - slot = written only when my actual slot differs from the one derived for
     my region, preset and channel name (i.e. I'm on a non-default slot or
     frequency)
   - The payload is cached and invalidated when the config is saved.
   - If message and offer are both empty there is nothing to send. That can't
     happen while the offer is automatic, but it is checked anyway.
5. **Target:**
   - preset = `s_cfg.meshBeaconTargetPreset`
   - frequency = `regionSlotFreq(s_cfg.region, p.bw, p.channelName)`
   - key = the default public key
   - channel hash = `computeChannelHash(p.channelName, defaultKey)`
6. **Send:**
   - **Same radio:** if the target's freq/bw/sf/cr equal the live radio's,
     call `sendMeshBeacon` directly. There's no retune.
   - **Otherwise:** `tempTuneEnter(target, snap, /*renameCh0=*/false)` →
     `sendMeshBeacon` → `tempTuneLeave(snap)`. Nothing else can transmit in
     between, because it all runs in one `loop()` call with a blocking TX.
7. **After:**
   - status line: "last sent HH:MM · next in N min"
   - log line: `[beacon] sent on LongFast 906.875 MHz (N bytes, offer ch=…, psk=no)`
   - the air-util retry resets to 0

**Turning broadcast on** (or saving beacon settings) schedules the first check
for about 2 minutes later. It still respects the saved last-sent time, so
toggling cannot get around the interval.

## 3. Config and storage

New fields are **appended** to the end of `RhinoConfig` (`src/config_io.h`),
padded after the current last field by the rule that file documents. A
missing field in an older blob reads as off or empty, so no migration is
needed. Defaults are new macros in `src/config.h` next to
`MY_MESH_BEACON_LISTEN`:

| Field | Type | Default macro |
|---|---|---|
| `meshBeaconBroadcast` | bool | `MY_MESH_BEACON_BROADCAST 0` |
| `meshBeaconTargetPreset` | uint8_t, camillia `PRESET_*` | `MY_MESH_BEACON_TARGET PRESET_LONG_FAST` |
| `meshBeaconOfferPsk` | bool | `MY_MESH_BEACON_OFFER_PSK 0` |
| `meshBeaconIntervalH` | uint8_t, {1,2,3,6,12,24} | `MY_MESH_BEACON_INTERVAL_H 1` |
| `meshBeaconMessage` | char[61] | empty |

Values are clamped on load: interval via `clampBeaconIntervalH`, and a target
preset ≥ `PRESET_COUNT` falls back to LongFast. The message is truncated to 60
bytes on a UTF-8 boundary.

**Last-sent** is kept outside `RhinoConfig`, in its own small Preferences key
(namespace and key alongside other runtime NVS keys). Saving it in the config
would rewrite the whole blob hourly. It is stored as epoch seconds when the
clock is valid. Without a valid clock, "sent this boot" is tracked in RAM, and
a reboot counts as "sent just now". That errs towards waiting, never
re-beaconing.

**cfg YAML / CLI** (`src/config_io.cpp`) extends the existing
`module_config: mesh_beacon:` block:

```yaml
module_config:
  mesh_beacon:
    listen_enabled: false
    broadcast_enabled: false
    target_preset: LONG_FAST
    offer_psk: false
    message: ""
    interval_hours: 1
```

`target_preset` uses the same preset names the YAML already uses elsewhere
(`presetFromName`).

## 4. UI

### Device Config screen

The existing "Mesh Beacons: On/Off" toggle row (`main_lvgl.cpp` ~44735)
becomes "Mesh Beacons: Listen · Broadcast" (or "Off"). It opens a popup built
from the existing Config modal parts, so every board's key, trackball, d-pad
and touch navigation comes for free. Its rows:

1. Listen: on/off. Today's toggle, with the same side effect of clearing the
   list when it is turned off.
2. Broadcast: on/off
3. Target preset: picker, listing only presets `presetUsableOnThisRadio()`
   allows
4. Include key: on/off. When on, a warning line reads "Anyone on <target> can
   join <channel>."
5. Message: text entry, 60-byte limit
6. Interval: picker, 1/2/3/6/12/24 h

Saving follows the normal Config save path (`markConfigDirty()`), then
invalidates the payload cache and reschedules.

### Beacons screen (Live → Tools → Beacons)

A status line sits above the list:

- "Your beacon: off"
- "Your beacon: LongFast · next in 42 min · last 14:05"
- "Your beacon: held — Discovery scan" / "airtime" / "LongFast not available
  in <region>"

Text that currently says receive-only is updated: the empty-list help and the
"Mesh Beacons is off" text. Each beacon card shows "slot N" when a received
offer carries field 5.

### Web config (`src/web_config.cpp`)

Under Modules → Mesh Beacons, the "Listen for beacons" select stays. Five
fields are added: Broadcast, Target preset, Include key (with the same
warning), Message, Interval. POST handling sits beside `mesh_beacon_listen`.
Lite pages follow the existing `lite` gating for module settings.

### i18n

New strings go in `src/i18n_builtin.h` with es/fr, following the existing
pattern (`docs/TRANSLATING.md`).

## 5. Docs

`docs/USE.md` "Mesh beacons" and "Beacons" sections are rewritten to cover:

- what is sent and where it goes
- how often
- that the home config is never changed
- that "Include key" makes the primary channel joinable by anyone who hears it
- that a beacon is only heard by nodes on the target preset, region and
  default channel

## 6. Testing

- **Host** (`tests/test_mesh_beacon.cpp`, run by `tests/run.sh`):
  - `encodeMeshBeacon` byte-for-byte against fixtures from upstream's nanopb
    encoding of the same `MeshBeacon`: text only, offer only, with/without
    PSK, `offer_preset = LONG_FAST` (value 0 still present), with slot
  - round trip through `decodeMeshBeacon`, including field 5
  - schedule maths: due, not due, reboot, backoff doubling and cap, interval
    clamping
- **Build:** one environment, `tdeck`, per the minimal-builds rule.
- **On air (user):**
  - a stock Meshtastic 2.8.x node on LongFast shows the beacon and offer
  - the device returns to its home preset; check by a message round trip
    afterwards
  - a 2.7.x (≥ 2.7.20) node on LongFast also receives it
  - nothing is sent while a Discovery scan runs

## Risks

- **Brief deafness.** The device is off its own mesh for one packet's airtime
  plus two reconfigures, about 1–2 s on LongFast. Packets on the home mesh in
  that window are missed. This is accepted, the same trade Discovery makes, at
  most once an hour.
- **Pre-2.7.20 nodes** drop zero-hop packets with `hop_start = 0`. This is
  accepted, and upstream's override can be added later if needed.
- **Key exposure** with Include key on: by design, behind a warning, default
  off.
