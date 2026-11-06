# Licensed (amateur) mode — design

Status: implemented (issue #142). Source: issue #142 body, reproduced below, plus decisions.

## Decisions (user, 2026-11-05)

1. Refuse MQTT downlink onto RF while licensed: **yes**.
2. Ship the 70 cm / 1.25 m regions **now** (2 m listed for enum completeness, hidden: no current radio tunes 144–148 MHz).
3. Licensed mode is **compiled out on `cardputer-cap`** (`DEVICE_CARDPUTER_LORA_HAT`) only; present on every other board.
4. Verification build target: `tdeck`. Never flash.

## Codebase facts that adjust the issue text

- Camillia has no phone API (ToRadio/FromRadio) and is not a remote-admin server, so incoming `set_ham_mode` (issue §7) has no entry point: out of scope.
- `regionPower()` (config_io.cpp:249) is declared but never called; today no regulatory power clamp exists. Leaving licensed mode applies `loraPower = min(loraPower, regionPower(region))` so the clamp is genuinely "back"; licensed TX power is capped at `MESH_POWER` (hardware max).
- No board declares its RF band; the antenna warning uses generic text rather than a per-board MHz value (no invented hardware data).
- No User encoder exists: User is built inline in `encodeNodeInfo()` (mesh_proto.cpp:1081). The 'reject legacy channel-key DM' rule exists only on the send side (dm_mgr.cpp:354).

---

## Summary

Add Meshtastic-compatible **licensed (amateur radio / "ham") mode**. With it, a licensed operator can run a Camillia device under amateur rules instead of licence-free ISM rules:

- they identify with a **call sign**
- they transmit **unencrypted** (amateur rules forbid hiding the content of a message)
- they may exceed ISM power limits, within the licence and hardware
- they can use the **amateur-band regions** Camillia deliberately leaves out today (`config_io.cpp:319`, "we have no concept of a licensed operator")

Camillia today speaks to licensed Meshtastic nodes only as an unlicensed peer. Upstream treats the two groups as separate meshes: they don't record each other's NodeInfo, a licensed node won't relay unlicensed traffic, and DMs go via PKI on one side and plaintext-signed on the other. So a Camillia user who holds a licence can't currently join a ham Meshtastic mesh correctly at all.

The upstream reference throughout is meshtastic/firmware at `9bcf0f7` (2026-11-04).

## What licensed mode is upstream

It's one flag, `User.is_licensed` (mesh.proto field 6, also in `DeviceState.owner`), plus the admin message `set_ham_mode` (`HamParameters`: `call_sign`, `tx_power`, `frequency`, `short_name`, `long_name`). Turning it on does all of the following (`AdminModule::handleSetHamMode`, `Channels::ensureLicensedOperation`, `NodeDB`, `Router`, `RadioInterface`):

| Area | Upstream behaviour when `owner.is_licensed` |
|---|---|
| Identity | `long_name` = call sign, or `CALLSIGN//long name` when a long name is also given. The call sign is **required** (a blank one is rejected). `N0CALL` sets `tx_enabled = false`. |
| Station ID | `node_info_broadcast_secs = 600`. NodeInfo goes out every 10 minutes, because the FCC needs station ID at least every 10 minutes. This bypasses the usual 1 h NodeInfo floor and its scaling (`NodeInfoModule.cpp:169`). |
| Channel encryption | `ensureLicensedOperation()` removes the PSK from **every** channel and **disables the `admin` channel**. It runs on mode change, at boot for a fresh channel file, and after a backup restore. A client then gets a warning (`warnLicensedMode`). |
| PKI | No PKI encryption on send (`Router.cpp:1178,1220`). PKI packets are not decrypted on receive (`Router.cpp:940`). Key verification is refused. **The keypair still exists**: it's generated when the region is set, because 2.8 node IDs are `crc32(pubkey)` and it's used for signing. A client is warned when this changes the node ID (`LICENSED_IDENTITY_WILL_CHANGE`). |
| DMs | They're plaintext on the channel (no PKI), so the "reject legacy channel-key DM" rule is **skipped** for a licensed receiver (`Router.cpp:1063`). |
| Signing | XEdDSA signs **unicasts as well as broadcasts** ("ham in place of PKI", `Router.cpp:1254`), except ROUTING_APP acks (which carry an ack proof) and traceroutes. |
| Relaying | `rebroadcast_mode` is forced to `LOCAL_ONLY`, and enforced again at boot. Opaque (undecodable) packets are never relayed (`NextHopRouter.cpp:34`). Packets **to or from a node known to be unlicensed** are not processed or relayed (`RoutingModule.cpp:22`); nodes of unknown status are let through. |
| NodeInfo | NodeInfo whose `is_licensed` differs from ours is **ignored**: a "mismatch" (`NodeInfoModule.cpp:48`). Licensed and unlicensed nodes don't populate each other's node lists. |
| Power | Region `powerLimit` and the board's PA-gain compensation are **not applied** (`RadioInterface::limitPower`). Duty-cycle override is on. `tx_power` and `override_frequency` come from `HamParameters`. |
| Regions | Ham regions are `licensedOnly` and refuse to be set unless licensed (`RadioInterface.cpp:1091`, error `LORA_REGION_REQUIRES_LICENSE`): ITU1/2/3_2M (144–148 MHz, Tiny presets, 20 kHz profile), ITU2_125CM (220–225), ITU1/2/3_70CM (420–450, Narrow presets, 100 kHz profile), each with its own default slot. |
| Other modules | Mesh Beacon TX refuses ham-only regions unless licensed (see #141). |

## Proposed design for Camillia

### 1. State and storage

- Two fields are appended to `RhinoConfig` (`src/config_io.h`), padded the way that file pads every appended field:
  - `bool licensedMode` (off)
  - `char callSign[16]` (empty)

  They default off and empty through `MY_LICENSED_MODE 0` / `MY_CALL_SIGN ""` in `config.h`. Older saved settings load as unlicensed, so no migration is needed.
- A single helper `bool isLicensed()` is the only thing the rest of the firmware reads, so every rule below has one switch.
- Turning licensed mode **on** is a **transaction** in one function, `enterLicensedMode(callSign, longNameSuffix, txPower)`, mirroring `handleSetHamMode`. It refuses a blank call sign.
  1. set the call sign, and `longName = CALL` or `CALL//suffix` (truncated on a UTF-8 boundary to the long-name limit)
  2. strip the PSK from every channel, and disable any channel named `admin`
  3. NodeInfo interval = 10 min
  4. force the role's rebroadcast behaviour to local-only (below)
  5. set tx power as requested
  6. save, then **reboot**

  The reboot is the simplest correct way to re-key the channel hashes, PKI state and caches all at once. Camillia already reboots on similar identity changes.
- Turning licensed mode **off** clears the flag and restores the NodeInfo interval and power clamp. It does **not** bring back the old PSKs, which are gone, same as upstream. The UI says so before you confirm.
- `N0CALL` is accepted but leaves TX disabled. It exists for testing.

### 2. Identity and NodeInfo (`src/mesh_proto.cpp`)

- `encodeUser` writes field 6 `is_licensed = true` when licensed. The comment at `mesh_proto.cpp:1108` is out of date: current upstream keeps the public key on licensed nodes (for node ID and signing). So we keep sending field 8 (`public_key`) either way. I'll verify against a 2.8.1 licensed node.
- `decodeUser` reads field 6 into the node record, as `NodeEntry.isLicensed` in `node_db.h`.
- Received NodeInfo whose `is_licensed` differs from ours is **not stored**, matching upstream's mismatch rule. Otherwise the node list fills with stations we can't legally exchange traffic with. A debug counter makes those drops visible on the Live screen.
- The NodeInfo broadcast interval becomes 600 s while licensed, overriding Camillia's normal floor. That's a regulatory interval, not a preference, so the user can't set it longer.

### 3. Encryption, PKI and DMs

- **Channels:** every channel is open while licensed (`pskLen = 0`). The channel editor won't accept a key, and channel import or QR join strips one with a notice ("Licensed mode: channel imported without its key").
- **PKI:** `dm_mgr.cpp` no longer requires a recipient public key when licensed (today it does, at `dm_mgr.cpp:354`). Licensed DMs go out **plaintext on the primary channel** with `to = node`. Incoming PKI-encrypted packets are not decrypted while licensed, same as upstream. The keypair is still generated and kept for node ID and signing. Key pinning (`key_pin`) still works from signed packets.
- **Accepting DMs:** the inbound "channel-key DM" rejection is skipped while licensed. `sig_policy.h` already notes that upstream's Balanced policy also covers unicasts in ham mode (`sig_policy.h:109`). That branch is implemented: a licensed receiver on Balanced drops an unsigned unicast from a known signer when its signed form would have fit.
- **Signing:** `signBroadcastData()` (`mesh_proto.cpp:734`) becomes "sign broadcasts, and unicasts too while licensed". Exceptions are ROUTING_APP (ack proof) and traceroute, same as upstream. Without this, every licensed peer on Balanced would drop our DMs.
- **Admin:** remote admin uses PKI, so it is unavailable while licensed. The Remote tab and `admin_client` say why instead of timing out. This matches upstream, which disables the admin channel too.

### 4. Relaying (`src/relay_policy.h`, RX path in `main_lvgl.cpp`)

While licensed:
- packets we can't decode are never relayed
- relay only what we could decode (local-only)
- **drop and don't relay** a packet whose `from` or `to` is a node we know to be unlicensed. Nodes we've no NodeInfo for are let through, matching upstream's three-way status (licensed, unlicensed, not known).

The role picker stays, but the relay rules above override it, and the Config screen says so.

### 5. Power and regions (`src/config_io.cpp`, `mesh_channel_plan.h`)

- **`RegionPlan` gains `bool licensedOnly`.** The ham regions are added with upstream's band edges, default slots and preset sets:
  - ITU2_125CM, ITU1/2/3_70CM: Narrow presets, 100 kHz
  - ITU1/2/3_2M: Tiny presets, 20 kHz

  `kRegionFromMeshtastic[27,28,33–37]` map to them instead of `nullptr`.
- **Hardware gate, which matters more here than upstream:** every Camillia radio (SX1262, LR1110, LR1121) tunes **150–960 MHz**. So **the 2 m regions (144–148 MHz) can't be reached on any current board**. They're listed in the table for enum completeness but hidden by a new `regionUsableOnThisRadio()`, the same idea as `presetUsableOnThisRadio()`. 70 cm and 1.25 m are tunable. But our boards' antennas and RF front ends are built for 868/915 MHz (or 433 on some), so the region picker warns: "Needs an antenna and front end for this band; this board is built for X MHz." Each board declares its band in `hal/board.h`, which drives that warning.
- **Tiny presets** already require TCXO + SX126x (`presetUsableOnThisRadio`), and that rule carries over unchanged.
- **The region picker** lists `licensedOnly` regions only while licensed. Switching licensed mode **off** while on a ham region moves the radio to the board's default ISM region and disables TX until confirmed, so it never sits transmitting unlicensed on a ham band.
- **Power:** while licensed, the region `power` cap (`config_io.cpp:243`) isn't applied. The ceiling becomes the **hardware** maximum for the chip and PA, never above what the board can safely do (existing per-board PA limits stay; only the regulatory clamp lifts). The UI shows the actual dBm.
- The duty-cycle hold (Mesh Beacon's EU rule in #141, and any future airtime limits) doesn't apply in licensed mode, matching upstream's `override_duty_cycle`.

### 6. Other modules

- **Mesh Beacon TX (#141):** refuse a ham-only target region unless licensed. If licensed, never offer a PSK (there is none). The #141 spec currently says "no licensed mode, guard not needed", and gets updated if this issue lands first.
- **MQTT:** uplinking amateur traffic to the internet is legal in most jurisdictions, but plaintext uplink of an open channel is already how it works, so no change. Downlink onto RF from MQTT is **refused while licensed**, because third-party traffic injected onto amateur bands isn't the operator's station transmission. Upstream doesn't do this explicitly yet. Open question below.
- **Chat server client (`cs_client`):** it works over plaintext channels unchanged, but loses any PKI-dependent features. To be checked during implementation.

### 7. UI

- **Config → Licensed (ham) mode**, a new entry:
  - Fields: call sign, optional long-name suffix, TX power.
  - It's on/off behind a **two-step confirm** that states the consequences: "Every channel's key will be removed. Encrypted DMs, remote admin and relaying for unlicensed nodes will stop. The device will reboot. You must hold an amateur licence for the band you use."
- **The header/status bar** shows the call sign, and a small "HAM" badge when licensed.
- **Node list:** a "ham" marker on licensed nodes, from `is_licensed`.
- **Web config:** a Licensed mode section with the same fields and the same confirm text. The cfg YAML gets `owner: { licensed: bool, call_sign: str }`; `admin_client.cpp:150` already lists the `licensed` field for remote owner reads.
- **Remote admin, incoming:** support `set_ham_mode` (AdminMessage field 18) from a Meshtastic client over the phone API. It runs `enterLicensedMode()` with the same validation.
- i18n strings with es/fr in `i18n_builtin.h`.

### 8. Docs

`docs/USE.md` gets a new "Licensed (amateur) mode" section covering:
- what changes
- the legal responsibility ("Camillia does not verify your licence")
- the 2 m hardware limitation
- the antenna warning
- that keys don't come back when you turn it off

## Testing

- **Host tests:**
  - `encodeUser`/`decodeUser` field 6 round trip
  - long-name composition `CALL//suffix` and UTF-8 truncation
  - the relay decision table (licensed/unlicensed/unknown × from/to)
  - the signing decision (broadcast/unicast/routing/traceroute × licensed)
  - region gating (licensedOnly × licensed × radio range)
- **On air**, against a stock Meshtastic 2.8.1 node in ham mode on 70 cm (or on an ISM region with both licensed, for the protocol checks):
  - each sees the other in its node list with the call sign
  - DMs both ways (plaintext, signed) are accepted on Balanced
  - an unlicensed node's NodeInfo is ignored and its traffic isn't relayed
  - channel keys are gone after enabling
  - NodeInfo goes out every 10 min
  - turning the mode off restores the power clamp
- 2.7.x interop: a 2.7.x licensed node (no signing) still exchanges messages with us on the Compatible policy.

## Open questions

1. **Refuse MQTT downlink onto RF while licensed?** Proposed yes; upstream doesn't explicitly.
2. **Ship the 70 cm / 1.25 m regions now, or only the licensed mode itself first?** That would let a licensed operator use their call sign and plaintext rules on ISM bands, and add the ham bands once a board with a suitable front end is tested.
3. **Should licensed mode be compiled out on boards that can't reach any ham band usefully** (e.g. Cardputer), or always present?

## Done when

- A Camillia device in licensed mode and a stock Meshtastic 2.8.1 licensed node can see each other, exchange signed plaintext DMs and channel messages, and neither relays traffic to or from a known-unlicensed node.
- Turning licensed mode on is impossible without a call sign, removes every channel key, and is confirmed with an explicit warning. Turning it off leaves the device on an ISM region with the regulatory power clamp back.
- No unlicensed device can select a ham region, by UI, web config, cfg file or remote admin.

