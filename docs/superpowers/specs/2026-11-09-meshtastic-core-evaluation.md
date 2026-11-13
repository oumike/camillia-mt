# Meshtastic firmware as Camillia's mesh core — evaluation

Date: 2026-11-09
Status: evaluation only. No decision has been made and no code was changed.
Baseline: Camillia-MT v6.4.4 (`main`). Upstream: meshtastic/firmware v2.7.26
(latest stable) and `develop` / v2.8.2 alpha.

The question: should Camillia delete its own Meshtastic implementation and build
on the official Meshtastic firmware instead? Is that possible, and what happens
to backward compatibility?

## Summary

| Question | Short answer |
| --- | --- |
| Is it possible? | **Yes for most boards, at high cost.** Both projects are GPL-3, and upstream has a supported way to run a custom LVGL UI on the device: device-ui's PacketAPI. 10 of our 14 build environments have an upstream board variant to start from. Four have none: Mesh Deck, both T-Display P4 environments and Tanmatsu, and upstream has no ESP32-P4 board support yet. The Cardputer (no PSRAM, 3.125 MiB OTA slots) is unlikely to fit Camillia's UI and the Meshtastic core together. |
| Biggest pros | Exact protocol behaviour without mirroring upstream by hand. The official phone apps, CLI and remote admin work. We also gain NextHop routing, the full set of device roles, the module catalogue, and upstream's security and radio fixes. |
| Biggest cons | Upstream is mid-churn: 2.8 is alpha-only, several 2.8 builds were revoked, and only 2.8.x gets security fixes. Our UI is welded to our mesh code (`main_lvgl.cpp` is 67k lines). The S3 boards jump from Arduino 2.0.17 to 3.3. Camillia-only features are lost or become fork patches, and signed OTA must be carried as a patch. P4 and Tanmatsu stay on a second stack. |
| Backward compatibility? | **Yes, it is affected.** On-air compatibility with stock Meshtastic gets better. Our extensions, stored settings and node database break unless we write migration code and port features, and so do node IDs on a 2.8 core. Encryption keys and message history can be kept. Six boards have no filesystem partition for the core's settings, so they need a USB reflash or a non-standard workaround. |
| Recommendation | **Don't gut the stack now.** First, do the work that pays off whichever way this goes: one interface between the UI and the mesh code, upstream's protobufs vendored in, and an interop test rig. Then run a time-boxed spike on the T-Deck to measure size, RAM and rebase cost. Decide once 2.8 has a stable release. |

## 1. The two codebases today

### 1.1 Camillia-MT: what would be removed

Camillia implements the Meshtastic protocol itself. The mesh stack has two parts:

- **Dedicated files, about 7.8k lines:** `mesh_proto`, `mesh_radio`, `relay_policy.h`,
  `sig_policy.h`, `xeddsa_sig`, `ack_proof`, `key_pin`, `node_db`, `node_evict`,
  `mqtt_bridge`, `mesh_beacon`, `licensed_mode` and `mesh_channel_plan`, plus the
  P4 and Tanmatsu radio HAL.
- **About 3.4k lines inside `src/main_lvgl.cpp`** (67,049 lines in all), mixed
  in with the UI:
  - the relay path, `maybeRebroadcastPacket` (~58403) through `servicePendingRebroadcast` (~58547);
  - `processMeshPacket` (~58913) and `pollMeshRx` (~59561);
  - the NodeInfo, telemetry, neighbour-info and map-report announcers (~59939–60295).

**It tracks upstream by hand.**
- Protobufs are hand-encoded; there is no nanopb.
- `relay_policy.h` mirrors FloodingRouter's contention windows, with one known
  divergence: a CLIENT_HIDDEN node does not relay here, but does upstream.
- `sig_policy.h` mirrors 2.8.1's `checkXeddsaReceivePolicy`.
- `ack_proof` mirrors 2.8.1's Routing field 4.

**Missing compared with upstream:**
- No phone API (ToRadio/FromRadio over BLE, serial or TCP), so the official
  apps and the CLI can't connect.
- Admin is client-only: Camillia can administer other nodes but can't be
  administered (#89).
- No NextHop routing. `next_hop` is parsed but not used for routing.

**Camillia-only mesh behaviour:**
- 10 channels (8 on the Cardputer), each with its own hop limit.
- `nodeIdOverride`.
- TOFU key pinning with a conflict UI.
- An ignore list.
- Licensed-mode extensions.
- The camillia-chat-server client on PRIVATE_APP (256).
- A Live feed that includes packets it can't decrypt.
- Its own MQTT bridge and map reports.
- Its own modem-preset ordering (`mesh_channel_plan.h`).

**Independent of the mesh stack (kept either way):**
- The LVGL 9.5 UI, 22 languages, and 13 themes plus custom themes.
- Maps, weather, line of sight, wardriving and spell check.
- Web config, full and Lite (`web_config.cpp`, 11,896 lines).
- VNC, BLE keyboards (NimBLE), the environment sensor and the power manager.
- ECDSA-signed OTA.

**Persistence:**
- NVS (Preferences) namespaces:
  - `camillia`: the `RhinoConfig` blob, node ID, X25519 key pair, ignore list
    and custom themes;
  - `nodes`, `mesh_ch` and `cschan`.
- `RhinoConfig` (`src/config_io.h:150`, about 154 fields) is append-only.
- Message history and the node archive live on SD or LittleFS under `/camillia/`.
- The YAML export, `/camillia/config.yaml`, follows the shape of
  `meshtastic --export-config` and includes the key pair.

**Toolchain:**
- S3 boards: `espressif32@7.0.1` (Arduino core 2.0.17, IDF 4.4).
- P4 and Tanmatsu: pioarduino 55.03.312-1 (Arduino 3.3.12, IDF 5.5).
- RadioLib 7.8.1, LVGL 9.5.0, LovyanGFX 1.2.34.

**Tests:** 23 host test suites (about 4.2k lines), mostly the protocol logic
listed above.

**Coupling.** The UI reaches straight into mesh state. Counts of references:

| Symbol | `main_lvgl.cpp` | `web_config.cpp` | rest of `src/` |
| --- | ---: | ---: | ---: |
| `s_cfg.` (RhinoConfig: UI and mesh settings) | 1,247 | n/a¹ | n/a¹ |
| own node ID (`s_myNodeId`, `myNodeId`) | 146 | 0 | 78 |
| `Nodes.` | 139 | 17 | 10 |
| `CHANNEL_KEYS` | 96 | 24 | 54 |
| `Channels.` | 60 | 10 | 3 |
| `DMs.` | 47 | 14 | 1 |
| `Radio.` | 47 | 0 | 23 |

¹ `web_config.cpp` works on `RhinoConfig` through its own references, not `s_cfg`.

Only the mesh share of `s_cfg` would move to Meshtastic's config. UI settings
can stay in `RhinoConfig`.

### 1.2 Meshtastic firmware: what would be adopted

| | |
| --- | --- |
| Releases | v2.7.26 "Beta" is the latest stable release (2026-06-24). 2.8 is alpha-only: both 2.8.0 builds were revoked (signing bug); 2.8.1 alpha shipped 2026-10-01; a 2.8.2 build was revoked on 2026-11-02 and replaced by 2.8.2.d5f1619 alpha on 2026-11-04. |
| Security support | `SECURITY.md` supports **2.8.x only**; ≤ 2.7.x is unsupported. The only stable line is already out of support, and the supported line is alpha. |
| Pace | About 186 commits a month to `develop` in 2026. |
| Toolchain | `develop` moved to Arduino 3.x / pioarduino (meshtastic/firmware#9122), the same platform our P4 environments use. v2.7.26 is still on espressif32 6.13.0 (Arduino 2.x). RadioLib is pinned to a master snapshot. RadioLib 8.0.0, a breaking release, came out 2026-11-07. |
| Shape | An application, not a library. It owns `setup()`/`loop()`, runs a cooperative `OSThread` scheduler, and keeps global singletons (`nodeDB`, `router`, `service`, `config`, `channels`). Routing layers: Router → FloodingRouter → NextHopRouter → ReliableRouter. Modules (`MeshModule`/`ProtobufModule`) are compiled in-tree and switched off with `MESHTASTIC_EXCLUDE_*`; there is no out-of-tree module mechanism. |
| UI seam | device-ui (MUI) is a separate library. With `HAS_TFT` and `USE_PACKET_API`, the UI runs as a core-0 task and talks to the core through PacketAPI: an in-process ToRadio/FromRadio queue with the same protobuf contract the phone apps use. standalone-ui runs the same UI over serial or TCP. |
| Persistence | LittleFS `/prefs/{device,nodes,config,channels,module,uiconfig}.proto` (nanopb). NVS holds only the reboot counter and OTA state. On ESP32 it mounts the partition labelled `spiffs` and formats it if mounting fails (`src/FSCommon.h`). |
| Identity | Up to 2.7, the node number is MAC bytes 2–5. In 2.8, once a region is set, the node number becomes crc32(public key) (`NodeDB::createNewIdentity()`), and the old self entry is removed. The private key lives in `config.security.private_key`. |
| Limits | 8 channels. One node-wide hop limit (default 3, max 7); 2.8 adds an automatic hop limit. NodeDB holds 250 nodes on ≥ 15 MB flash. |
| OTA | Unified WiFi/BLE OTA (meshtastic/firmware#9231). An admin `ota_request` reboots into a small loader app (~636 KB) kept in the second app slot. Integrity is checked with SHA-256; there is no vendor signature. |
| Footprint | 2.8.2 T-Deck builds: 2.40 MB with BaseUI, 3.92 MB with MUI. The Cardputer ADV ships with BaseUI only. |
| 2.8 protocol changes | Node ID = crc32(public key). XEdDSA signatures with receive policies; 2.8.0 and 2.8.1 signatures are not compatible with each other. 24-byte long names. Opt-in position and telemetry broadcasts. Auto hop limit. US default preset LongTurbo, which can't hear LongFast. Tiny presets. A filesystem format change. Packets without `hop_start` (pre-2.3 firmware) are dropped (meshtastic/firmware#9476). |

## 2. Is it possible?

### 2.1 Where Camillia's UI would attach

Upstream supports three ways to put a UI on the device:

1. **PacketAPI client (the device-ui model).** The UI replaces device-ui and
   sees only protobuf messages:
   - in: `FromRadio` (my info, node info, channels, config, packets, queue status);
   - out: `ToRadio` (packets, including admin messages to the local or remote nodes).

   The phone apps use the same contract, so upstream has strong reasons to keep
   it stable. This is the basis of Option B below.
2. **Direct calls into the core's globals (the BaseUI model, `src/graphics/`).**
   Tight coupling to internals. Basis of Option A.
3. **Out of process (standalone-ui over serial or TCP).** Not a product shape
   for a single handheld. It is useful for running the UI on a desktop against
   `meshtasticd` during development.

Whichever model is used, upstream expects to own the radio, the GPS UART, I²C
sensors, battery and sleep, BLE (phone API, NimBLE) and WiFi (HTTP API, MQTT).
Camillia owns all of these today. Under the core, Camillia would:

- hand the radio, GPS, telemetry sensors and power management to upstream, or
  patch them out;
- share BLE: keyboards and the phone link on one host. On `develop`, upstream
  uses the BLE library built into arduino-esp32 3.x and lists NimBLE-Arduino
  under `lib_ignore`. Our keyboard host (`ble_keyboard.cpp`, NimBLE-Arduino
  1.4.3) would have to be ported to that library;
- share WiFi: web config, VNC and OTA next to upstream's HTTP API and MQTT.

### 2.2 Board by board

"In 2.7.26" means the variant exists in the stable release; otherwise it exists
only on `develop` / 2.8.

| Camillia env | Device | Upstream variant | Upstream envs (UI) | In 2.7.26 | Assessment |
| --- | --- | --- | --- | --- | --- |
| `tdeck` | LilyGo T-Deck | `t-deck` | `t-deck` (BaseUI), `t-deck-tft` (MUI) | yes | Best spike target. Upstream has an open stability tracker for it (meshtastic/firmware#11953). |
| `tdeck-pro` | LilyGo T-Deck Pro | `t-deck-pro`, `t-deck-pro-v1_1` | `t-deck-pro` (BaseUI, e-ink), `t-deck-pro-inkhud` | yes | E-ink; no upstream LVGL build to compare against. |
| `tlora-pager-tft` | LilyGo T-LoRa Pager | `tlora-pager` | `tlora-pager`, `tlora-pager-tft` (MUI) | yes | |
| `cardputer-cap` | M5Stack Cardputer + LoRa-1262 Cap | `m5stack_cardputer_adv` | `m5stack-cardputer-adv` (BaseUI only) | yes | Variant targets the Cardputer **ADV**; whether it fits our board revision is unverified. No PSRAM and 3.125 MiB slots; Camillia alone is 2.79 MiB today. **Highest risk.** |
| `heltec-v4` | Heltec V4 + TFT kit | `heltec_v4` | `heltec-v4`, `heltec-v4-tft` | yes | |
| `heltec-r8` | Heltec V4-R8 | `heltec_v4_r8` | `heltec-v4-r8-oled`, `heltec-v4-r8-tft` | yes | |
| `m9` | Elecrow ThinkNode M9 (LR1110) | `ELECROW-ThinkNode-M9` | `thinknode_m9`, `thinknode_m9-tft` | **no** | Needs a 2.8 base. Check whether our LR11xx RadioLib workarounds are handled upstream. |
| `wio-tracker-l2` | Seeed Wio Tracker L2 | `seeed_wio_tracker_L2` | `seeed_wio_tracker_L2`, `-tft` | **no** | Needs a 2.8 base. |
| `crowpanel-35` | Elecrow CrowPanel Advance 3.5" | `elecrow_panel` | `elecrow-adv-35-tft` (MUI) | yes | Our pin map already comes from this variant. |
| `crowpanel-50` | Elecrow CrowPanel Advance 5.0" | `elecrow_panel` | `elecrow-adv1-43-50-70-tft` (MUI) | yes | Same. |
| `mesh-deck` | Attaky Mesh Deck | none | — | — | ESP32-S3, so a new variant is ordinary work. No upstream `hw_model` (reports PRIVATE_HW). |
| `p4-amoled-sx1262`, `p4-amoled-lr2021` | LilyGo T-Display P4 | none | — | — | `variants/esp32p4/` holds only a base `esp32p4.ini` (WiFi/MQTT excluded). The upstream T-Display P4 port was closed (meshtastic/firmware#10012). Generic P4 board support is still an open PR (meshtastic/firmware#9526). The LR2021 driver exists only in 2.8 (meshtastic/firmware#10401). **We would be porting Meshtastic to the P4 ourselves.** |
| `tanmatsu` | Nicolai Tanmatsu (P4 + C6-hosted SX1262) | none | — | — | Upstream variant PR closed (meshtastic/firmware#8356). The radio is behind ESP-Hosted RPC, not SPI, so it needs a custom `RadioInterface` over our `TanmatsuLoraRpc`. **Same P4 problem, plus the radio.** |

An upstream variant supplies pins and radio setup. It does not supply
Camillia's UI hardware layer (LovyanGFX panels, touch, keyboards, VNC), which
stays ours on every board.

### 2.3 What that means

- **Not possible without porting Meshtastic ourselves:** `p4-amoled-sx1262`,
  `p4-amoled-lr2021` and `tanmatsu`. They would either stay on Camillia's stack,
  leaving two stacks to maintain, or wait for upstream P4 support.
- **Probably not possible as-is:** `cardputer-cap`, because of image size and
  RAM. Its options are to drop the board, keep it on the legacy stack, or ship
  a reduced build with a new flash layout (USB reflash).
- **Possible, new variant needed:** `mesh-deck`.
- **Possible, integration work only:** the other 9 environments. M9 and Wio
  Tracker L2 additionally need a 2.8 base.

## 3. Ways to do it

| Option | What it means | Fork surface | Compatibility impact |
| --- | --- | --- | --- |
| **A. Hard fork, UI inside the core** | The Camillia UI calls `nodeDB`, `service`, `config` and `channels` directly, as BaseUI does. | Large: any internal refactor upstream breaks us. | Full migration (§6). |
| **B. Thin fork, UI as a PacketAPI client** | The core stays nearly unmodified and pinned to a release tag. The Camillia UI replaces device-ui. Camillia-only features become modules (in-tree patches) or client-side features. | Small, if the patch set is kept small. | Full migration (§6). |
| **C. Keep our stack, adopt selectively** | Vendor upstream's `.proto` files, test against `meshtasticd`, and add the phone API and remote admin if wanted. | None. | None. |
| **D. Status quo, with a process** | A checklist per upstream release for protocol changes to mirror, which is roughly what we did for 2.8.1 signatures. | None. | None. |

**If Camillia moves to the core, B is the only shape likely to survive
upstream's churn.**
- It couples us to the protobuf API, which the phone apps also depend on,
  rather than to internals. NodeDB internals are actively being rewritten in
  2.8: lite entries, satellite stores, a warm tier.
- B also avoids a threading problem. Under A, the UI on core 0 and the
  Meshtastic loop on core 1 would share globals and need locking. PacketAPI is
  message passing.
- A is the path the abandoned community T-Deck forks took. W4PFI/tdeck-firmware
  is 512 commits ahead and 3,012 behind upstream, with its last push in 2024-11.

**Prior art in our own projects.** camillia-meshcore did a B-style move for
MeshCore:
- It pins `meshcomod#core-v1.17.4`, uses a patch script so only MeshCore's
  protocol layers compile, and has a ~670-line event adapter
  (`src/meshcore_transport.h`: contact, DM, channel message, ack, send timeout).
- Only `tdeck` was converted; the other environments stayed on the legacy stack.
- The cached peer list was wiped and message history was kept on separate paths.
- Its last commit was 2026-09-09 (v5.0.1).

Lesson: the adapter worked, but "all boards" never happened. MeshCore is shaped
like a library; Meshtastic is an application. Expect more friction, not less.

## 4. Pros

1. **Protocol behaviour exact by construction.** We would stop mirroring relay
   timing, signature policy, ack proofs, AEAD channels and beacons by hand.
   Known divergences such as CLIENT_HIDDEN relaying disappear. 2.8 is the
   largest protocol change in years (node IDs, signatures, presets, pre-hop
   drop), and keeping up with it by hand is a standing cost.
2. **Official clients work.**
   - Android and iOS apps, the web client and the Python CLI can connect to
     Camillia devices.
   - Camillia nodes become remotely administrable, which closes #89.
   - `meshtastic --export-config` and `--configure` work.

   This may be limited under Option B: upstream's MUI can't keep a phone
   connected over BLE at the same time (§10).
3. **Features we don't have:** NextHop routing, the full role set, key
   verification, and the module catalogue. Camillia offers 5 of the 13 roles
   and leaves out the infrastructure roles on purpose (`src/config_io.h:1232`);
   a UI could keep hiding them. Modules include store-and-forward
   server, range test, traceroute, canned messages, external notification,
   detection sensor, paxcounter, serial, and telemetry with many sensor drivers.
4. **Security and radio fixes for free.** Upstream's security support covers
   2.8.x, and fixes for LR11xx, LR2021 and SX126x land without our effort.
5. **Hardware support.** New boards and radios arrive upstream. We already copy
   CrowPanel pin maps from upstream variants.
6. **Testing.** `meshtasticd` (portduino) gives us a reference node for
   automated interop tests and a way to run the UI on a desktop.
7. **Less of our own code.** Roughly 11k lines of mesh code would go, along with
   most of the 4.2k lines of protocol tests.
8. **Aligned with upstream's direction.** Upstream wants custom UIs built on
   device-ui/PacketAPI. Its device-ui maintainer has described independent
   re-implementations, such as a T-Display P4 clone, as not the way the firmware
   is going. Being on the core removes that friction.

## 5. Cons and risks

1. **Moving target.**
   - 2.8 is alpha, has had revoked builds, and is rewriting NodeDB internals.
     `develop` takes about 186 commits a month.
   - The only stable line, 2.7.26, is outside security support and lacks the M9
     and Wio L2 variants.
   - A fork pinned to a tag misses fixes. A fork that rebases pays every month.
2. **Size of the re-plumb.** Every mesh-touching screen and the web config must
   move from `RhinoConfig`, `Nodes`, `Channels`, `CHANNEL_KEYS` and `DMs` to
   Meshtastic's protobuf config and NodeDB. That is hundreds of call sites
   (§1.1), most of them in one 67k-line file.
3. **Toolchain jump.**
   - The 11 S3 environments move from Arduino 2.0.17 / IDF 4.4 to Arduino 3.3 /
     IDF 5.5, because `develop` requires it.
   - Basing on 2.7.26 instead avoids the jump but means basing on an
     unsupported line.
   - Partly de-risked: `develop` pins the same pioarduino `55.03.312-1`
     platform that our P4 and Tanmatsu environments already build the UI on.
     The BLE keyboard code has never been built there: those boards reach
     Bluetooth through a C6 coprocessor, so `HAS_BLE_KEYBOARD` is off
     (`src/hal/board.h:436`), and it needs porting anyway (§2.1).
4. **Flash and RAM.** The combined image is Camillia's UI plus most of the
   core.
   - Today Camillia is 4.1–4.6 MiB on the 16 MB boards (6–6.25 MiB slots) and
     2.79 MiB on the Cardputer (3.125 MiB slot, no PSRAM).
   - Upstream's T-Deck core is 2.40 MB with BaseUI.
   - The 16 MB boards probably have room, but this must be measured. The
     Cardputer very likely does not; upstream itself ships only BaseUI there.
   - RAM is unmeasured: Meshtastic's packet pools, a 250-node NodeDB and
     OSThreads on top of LVGL buffers and Camillia's caches.
5. **Features lost or reduced unless we patch the core:**

   | Camillia feature | Under the core |
   | --- | --- |
   | 10 channels | 8. The limit is protocol- and app-wide; raising it would break the official apps. |
   | Hop limit per channel | One node-wide hop limit; would need a core patch. |
   | `nodeIdOverride` | Impossible on 2.8: the ID must equal crc32(public key), and 2.8 nodes check this on first contact. |
   | MAC-derived node ID | Replaced by crc32(public key) on 2.8. |
   | CLIENT_HIDDEN not relaying | Upstream semantics apply. |
   | Live feed / wardriving of packets we can't decrypt | Probably needs a core hook; to be verified. |
   | Chat-server client (PRIVATE_APP 256) | Port to the UI over PacketAPI, or as a module. |
   | Licensed-mode extensions | Map onto upstream's `is_licensed`; anything beyond needs patches. |
   | MQTT bridge, map reports | Upstream MQTT module, with different settings and behaviour. |
   | Preset ordering | Upstream enum; needs a mapping. |
   | TOFU conflict UI | Re-plumb onto upstream's key handling and key verification. |
   | Own GPS, sensor and power code | Owned by upstream; wardriving, LOS and maps re-plumbed onto upstream's position data. |
   | Beacon broadcast (brief retune to another preset) | Needs direct radio control: a core module, not a client feature. |

6. **Signed OTA becomes a fork patch.** Upstream's `ota_request` loader path
   accepts any image whose SHA-256 matches. It would have to be compiled out,
   and our ECDSA verifier kept, or the security guarantee regresses.
7. **Two stacks** if P4 and Tanmatsu (and maybe the Cardputer) stay on ours.
   That is the cost this change was meant to remove.
8. **Ownership conflicts** over BLE, WiFi, the GPS UART, I²C sensors and
   power/sleep (§2.1).
9. **Product identity.** Peers and maps see upstream's firmware version and
   `hw_model` unless we patch them. Boards without an upstream ID report
   PRIVATE_HW.
10. **Tests and hardware QA.** Most of the 23 host suites become obsolete. New
    adapter tests are needed, plus hardware regression testing across 10+
    boards.

## 6. Backward compatibility

### 6.1 On air

- **With stock Meshtastic nodes: better.** Behaviour matches whichever upstream
  version we pin. We inherit upstream defaults unless `userPrefs.jsonc` overrides
  them. For example, 2.8's US default preset, LongTurbo, can't hear LongFast.
- **With Camillia devices not yet migrated (mixed fleet):**
  - Standard traffic keeps working because both sides speak Meshtastic: text,
    PKI DMs, NodeInfo, position, telemetry and traceroute.
  - Camillia-only features break: channels 9–10, per-channel hop limits, the
    chat server unless ported, and anything else from the table in §5.
  - A 2.8 core signs its packets; legacy Camillia already verifies them under
    the 2.8.1 policy.
  - Pinning a core version also pins a signature format. 2.8.0 and 2.8.1 are
    incompatible, so the format is still settling.

### 6.2 Node identity and keys

**Keys.**
- Camillia keeps its X25519 key pair in NVS (`camillia/pub25519`,
  `camillia/priv25519`). The core reads `config.security.private_key` from
  `/prefs/config.proto` and re-derives the public key from it.
- A first-boot migrator that copies the private key keeps the public key the
  same. DMs keep working, and peers' pinned keys stay valid.
- Without migration, or after a flasher "erase", the core generates a new key,
  and every peer that knew the old key sees a key change.

**Node number.**

| Camillia setting today | On a 2.7 core | On a 2.8 core |
| --- | --- | --- |
| Default: MAC-derived (`nodeIdFromPubKey` off) | Same ID. Both use MAC bytes 2–5 (`deriveNodeId()` in `main_lvgl.cpp` ~12893; upstream `NodeDB::pickNewNodeNum()`). | **Changes** to crc32(public key). |
| `nodeIdFromPubKey` on | Same ID, if the migrator seeds `my_node_num` (2.7 keeps a stored number). | Same ID. |
| `nodeIdOverride` set | Same ID, if seeded. | **Lost**: becomes crc32(public key). |

On 2.8 this is less disruptive than it looks.
- Every stock node upgrading from 2.7 to 2.8 goes through the same ID change, so
  the mesh is absorbing ID changes anyway.
- Camillia already moves its own history across ID changes (`s_prevNodeId` and
  `setSenderRemap()`); the new UI would need the same.
- Mitigation: a last legacy release that moves users to `nodeIdFromPubKey`, so
  the ID change happens under Camillia, with its remap, before the switch.

### 6.3 Settings and channels

- **The core never reads our NVS blob**, so a one-time migrator is required. It
  would write `RhinoConfig` and `mesh_ch` out as `LocalConfig`,
  `LocalModuleConfig`, `ChannelFile` and owner/device state.
- UI-only settings (theme, language, keyboard, maps, VNC and so on) can stay in
  `RhinoConfig`. Its append-only rules keep working for them.
- **Lossy mappings:**
  - channels 9–10 are dropped (warn first);
  - per-channel hop limits collapse to one value;
  - the preset index maps to upstream's enum;
  - licensed extras and `nodeIdOverride` are lost;
  - MQTT bridge settings become MQTT module config.
- **YAML is the natural vehicle.** `/camillia/config.yaml` already looks like
  `meshtastic --export-config` and carries the key pair with the CLI's `base64:`
  prefix (`config_io.cpp`: export ~1825, import ~2186–2213). Keep importing old
  files after the switch. As a bonus, the Meshtastic CLI could then configure
  Camillia devices.
- **Web config** binds to `RhinoConfig` field by field. The mesh settings would
  need re-binding, and users would see the pages change.

### 6.4 Node database, pinned keys, message history

- **NodeDB:**
  - Our NVS `nodes` store must be translated to `/prefs/nodes.proto`:
    favourite → `is_favorite`, ignored → `is_ignored`, pinned key →
    `public_key`.
  - Otherwise it starts empty, as camillia-meshcore did. Starting empty also
    drops the pinned keys, so TOFU protection for known peers is gone until
    they are heard again.
- **Message history and node archive** (`/camillia/` on SD or LittleFS) survive
  if the Camillia UI keeps its own store. Upstream's MessageStore holds only 20
  messages; MUI uses `/messages`.

### 6.5 Flash layout and the upgrade path

| Layout | Used by | otadata | nvs | App slots | Filesystem |
| --- | --- | --- | --- | --- | --- |
| Upstream `default_16MB.csv` | — | 0xE000 | 0x9000 (20 KB) | 2 × 6.25 MiB @ 0x10000 / 0x650000 | `spiffs` 3.4 MiB @ 0xC90000 |
| `partitions_16mb.csv` | tdeck, tdeck-pro, tlora-pager-tft, m9, heltec-r8 | 0x9000 | 0x650000 (64 KB) | 2 × 6.25 MiB @ 0x10000 / 0x670000 | **none** (3.3 MiB unallocated @ 0xCB0000) |
| `partitions_16mb_fs.csv` | heltec-v4, mesh-deck, crowpanel-35, crowpanel-50 | 0x9000 | 0x650000 | same | `littlefs` 3.3 MiB @ 0xCB0000 |
| `partitions_16mb_wio.csv` | wio-tracker-l2, p4-amoled-sx1262, p4-amoled-lr2021, tanmatsu | 0x9000 | 0xC10000 | 2 × 6 MiB @ 0x10000 / 0x610000 | `littlefs` 3.8 MiB @ 0xC30000 |
| `partitions.csv` (8 MB) | cardputer-cap | 0x9000 | 0x650000 | 2 × 3.125 MiB @ 0x10000 / 0x330000 | **none** (1.56 MiB unallocated @ 0x670000) |

**No Camillia layout matches upstream's.**
- An OTA update can't rewrite the partition table.
- Adopting upstream's table therefore means a USB reflash of every device.
- NVS moves in that table, so settings and keys are wiped unless exported first.

**We don't have to adopt it: our build chooses the table.** If we keep ours:

- **Boards with a filesystem** (heltec-v4, mesh-deck, crowpanel-35/50,
  wio-tracker-l2, P4, Tanmatsu) need a small patch so the core mounts our
  `littlefs` label rather than `spiffs`. The in-field OTA migration then works.
  - Test LittleFS on-disk compatibility between the IDF 4.4 and 5.5 builds.
  - The core formats a filesystem it can't mount, and that filesystem also
    holds our `/camillia/` history; this also matters for downgrades.
- **Boards without one** (tdeck, tdeck-pro, tlora-pager-tft, m9, heltec-r8,
  cardputer-cap) have nowhere for `/prefs`. Choices:
  1. **One USB reflash to `partitions_16mb_fs.csv`.** That table keeps NVS at
     the same offset precisely so settings survive the move (see its header).
     The Cardputer would need a new 8 MB layout.
  2. **Register the unallocated tail as a partition at runtime** (IDF
     `esp_partition_register_external()`), with no reflash.
     - On the 16 MB boards that region is exactly where
       `partitions_16mb_fs.csv` puts `littlefs`, so a later reflash would find
       the same filesystem.
     - Unverified; worth testing in the spike.
  3. **`/prefs` on the SD card.** Fragile: no card means no identity.
- **The Cardputer.** If the combined image exceeds 3.125 MiB, two app slots
  can't fit in 8 MB. That would force an upstream-style single app plus loader:
  a USB reflash, and no A/B fallback.

### 6.6 OTA chain

- **Signing key and verifier.** Field units only accept images signed with the
  ECDSA P-256 key in `src/ota_signing_pubkey.h`, pulled from GitHub releases
  (stable and alpha channels). The first core-based release must be signed with
  the same key and carry the same verifier. Otherwise every unit is stranded on
  the last legacy release, with USB the only way forward.
- **Upstream OTA.** Compile out upstream's `ota_request` and loader path.
- **Downgrade.** OTA back to the last legacy release keeps working if the
  migrator leaves the legacy NVS namespaces untouched for at least one release
  cycle, subject to the LittleFS check in §6.5.
- **Rollback.** `scripts/backup.sh` full-flash backups stay the guaranteed way
  back: they restore firmware and NVS byte for byte. Use them on test units
  before the first migration.

### 6.7 Camillia ecosystem

- **Web flasher (camillia-mt-web).** ESP Web Tools offers to erase on new
  installs (`new_install_prompt_erase: true`, `src/firmware.js:86`). Erasing
  wipes NVS, and with it the identity. The migration instructions need to say
  "don't erase", or the manifest needs to change.
- **Community presets** (generated from camillia-mt-web) need mapping to
  upstream channel and LoRa config.
- **camillia-chat-server.**
  - Its protocol rides on PRIVATE_APP (256) with a `camillia-cs` discovery
    channel.
  - API clients receive PRIVATE_APP packets, so the client can likely live in
    the UI over PacketAPI without core changes.
  - Check how its discovery channel fits under an 8-channel limit.
- **YAML backups** made by older releases must stay importable.
- **Camillia desktop/mobile apps** (camillia-app-*) have no Meshtastic
  transport today. The core would give them the official phone API to talk to.

### 6.8 Compatibility at a glance

| Area | Effect of switching | Mitigation |
| --- | --- | --- |
| On air, stock Meshtastic | Better | — |
| On air, legacy Camillia | Camillia-only features break | Port as modules or client features; staged rollout |
| Encryption key | Kept if migrated | First-boot migrator; warn against erasing |
| Node ID | Changes on a 2.8 core for MAC-derived and overridden IDs | Pre-migration move to `nodeIdFromPubKey`; history remap |
| Settings | Translated, with losses (channels 9–10, per-channel hops, override) | Migrator plus YAML; release notes |
| Node DB, pinned keys, favourites | Lost unless translated | Translate NVS `nodes` |
| Message history | Kept | Keep the `/camillia/` store |
| Partition table | Kept if we keep ours | Label patch (FS boards); reflash or runtime partition (non-FS boards) |
| OTA | Kept only if verifier and key are carried over | Same key and verifier; compile out upstream OTA |
| Downgrade | Possible if legacy NVS is kept | Keep legacy namespaces for a cycle; check LittleFS versions |
| Web flasher | Erase prompt destroys identity | Instructions or manifest change |

## 7. Licensing and trademark

- **License: no blocker.**
  - Upstream is GPL-3.0; no "or later" grant was found. Camillia is
    GPL-3.0-or-later, and the README already says it links against or
    interoperates with Meshtastic code.
  - A combined binary would be distributed under GPL-3.0. Our own files can
    stay "or later". Obligations are as today: source for every binary.
- **device-ui** is GPL-3.0 with BSD, Apache and MIT parts. That only matters if
  we reuse its code.
- **Contributing hooks upstream** requires signing Meshtastic's CLA.
- **Trademark.**
  - Upstream's guidelines bar Meshtastic marks in the primary or secondary
    branding of third-party products, and any implied endorsement.
  - They ask for "not affiliated with or endorsed by" disclaimers. The M-PWRD
    logo is how compatible products signal compatibility.
  - Our product name, "Camillia for Meshtastic" (README title), is worth raising
    with trademark@meshtastic.org now, whatever is decided here. Running
    upstream's code would make the product more plainly a Meshtastic
    derivative.
- **Hardware IDs.** New `hw_model` IDs are only issued to backers and partners.
  Boards without one (Mesh Deck) report PRIVATE_HW and don't appear in the
  official web flasher.

## 8. Effort

These are order-of-magnitude figures for one developer who knows both
codebases. They are not a plan; the spike in §9 exists to replace them.

| Work | Rough size |
| --- | --- |
| Spike: T-Deck, Option B, minimal UI | 2–3 weeks |
| Re-plumb UI and web config onto PacketAPI and protobuf config | 6–10 weeks |
| Migrators (config, channels, identity, NodeDB) and YAML import | 3–5 weeks |
| Camillia-only features as modules or client features; keep signed OTA | 3–6 weeks |
| Arduino 3.3 for the S3 boards; bring-up and hardware testing on up to 11 environments | 4–8 weeks |
| P4 × 2 and Tanmatsu | Not included: stay on the legacy stack, or port Meshtastic to the P4 (open-ended) |
| Tracking upstream afterwards | Recurring; days per upstream release, depending on the size of the patch set |

That is roughly **4–8 months before the first migrated release on the S3
boards**, followed by a permanent rebase cost. For comparison, today's cost is
mirroring each upstream protocol change by hand. It is real, but it has been
paid in days per release (2.8.1 signatures, ack proofs, beacons), not months.

## 9. Recommended path

### 9.1 Work that pays off under every option

1. **Put one interface between the UI and the mesh code.**
   - Shape it as events in and commands out, close to FromRadio/ToRadio, as
     camillia-meshcore's `meshcore_transport.h` did.
   - The goal: `main_lvgl.cpp` and `web_config.cpp` stop reaching into `Nodes`,
     `Channels`, `CHANNEL_KEYS`, `DMs` and `Radio` directly.
   - Under Option B this *is* most of the migration. Under C or D it shrinks
     the monolith and lets one UI run on more than one backend, MeshCore
     included.
2. **Vendor `meshtastic/protobufs`** at a pinned tag. Test the hand-rolled codec
   against nanopb-generated code, or switch to it.
3. **Build an interop test rig** against `meshtasticd` (portduino), pinned to
   2.7.26 and the current 2.8 alpha, and run it in CI.
4. **Decide on the phone API and remote admin (#89) on their own merits.** If
   users want the official apps, that is the strongest single argument for the
   core, and it can also be built on our stack.
5. **Plan the move to crc32(public key) node IDs** (`nodeIdFromPubKey`). 2.8
   nodes need it for signed first contact, so Camillia needs it either way.
6. **Ask trademark@meshtastic.org about the name.**

### 9.2 Spike

Time-boxed to 2–3 weeks, on a throwaway branch, T-Deck only.

- **Build:** upstream `develop` (or the newest 2.8 tag) for `t-deck` with
  `HAS_TFT` and `USE_PACKET_API`. A Camillia UI shell replaces device-ui, with
  node list, channel chat, DMs and settings read/write.
- **Measure:** image size, free internal RAM and PSRAM (idle and under
  traffic), LVGL frame rate, boot time, and a BLE keyboard and phone app
  connected at the same time.
- **Prove identity migration:** the legacy NVS key goes into
  `/prefs/config.proto`, and a peer sees the same public key.
- **Settle the non-FS boards:** prove or rule out runtime partition
  registration.
- **Measure rebase cost:** rebase the spike once onto a newer upstream commit
  and record the conflicts.
- **Cardputer:** build a size-only image to settle whether it fits.

### 9.3 Go/no-go criteria (proposed)

- Upstream 2.8 has a stable release.
- The projected full-feature image is ≤ ~85% of a 6.25 MiB slot, and there is
  an explicit decision on the Cardputer.
- Free heap at idle is no worse than today minus an agreed margin.
- Identity migration is lossless: the key is unchanged, and the ID changes only
  where 2.8 forces it.
- Core patches stay within a small, listed set of files, and rebasing onto one
  upstream release takes no more than a couple of days.
- There is an agreed plan for P4 and Tanmatsu.

### 9.4 If it's a go: release train

1. **Last legacy release.** It auto-exports YAML (with keys) before updating,
   warns about channels 9–10 and the node-ID change, and keeps the OTA key
   unchanged.
2. **First core release, FS boards first, over OTA.** The migrator runs once
   and leaves legacy NVS in place.
3. **Non-FS boards** by whichever path the spike proved.
4. **P4, Tanmatsu and possibly the Cardputer** stay on the legacy stack, frozen
   to protocol-critical fixes, until a port exists.
5. **Remove the legacy stack** only once the downgrade window has closed.

## 10. Open questions

- RAM headroom for the core plus Camillia's UI on PSRAM boards, and whether the
  Cardputer can run any combined build at all.
- Whether the "no concurrent BLE phone link" limitation (reported for MUI) sits
  in device-ui or in the core. If it is in the core, it caps pro 2 under
  Option B.
- Whether the core passes packets it can't decrypt to API clients (Live feed,
  wardriving), and whether it honours hop limits set by the client.
- Whether upstream would accept small hooks that shrink our patch set.
- Whether `esp_partition_register_external()` works with esp_littlefs on our
  IDF 5.5 build for the non-FS boards.
- LittleFS on-disk compatibility between Arduino 2.0.17 and 3.3 builds, which
  matters for downgrades.
- Whether `m5stack_cardputer_adv` runs on our Cardputer revision.
- Whether our LR11xx RadioLib workarounds are still needed upstream (M9).
- Upstream's licence intent ("only" vs "or later"), and its trademark position
  on "for Meshtastic" and "Meshtastic-compatible".
- When 2.8 ships as stable.

## 11. Sources

Camillia (this repository, v6.4.4):

- `src/main_lvgl.cpp`:
  - `deriveNodeId()` ~12893, key storage ~12778;
  - mesh path ~57,000–60,400;
  - `setup()` ~65150, `loop()` ~66512.
- Mesh stack: `src/mesh_proto.h`, `src/relay_policy.h`, `src/sig_policy.h`,
  `src/mesh_radio.h`.
- Config: `src/config_io.h:150` (`RhinoConfig`), `src/config_io.cpp` (YAML
  export ~1775–2163, keys ~1825; import ~2186–2213, ~2645).
- OTA: `src/ota_update.cpp` (signature check ~461), `src/ota_signing_pubkey.h`.
- Build and docs: `platformio.ini`, `partitions*.csv`, `docs/HARDWARE.md`,
  `README.md`.
- camillia-meshcore: `src/meshcore_transport.h`,
  `tools/patch_meshcore_core_filter.py`.
- camillia-mt-web: `src/firmware.js:86`.

Upstream (checked 2026-11-09):

- Releases: <https://github.com/meshtastic/firmware/releases>
- Security policy: <https://github.com/meshtastic/firmware/blob/develop/SECURITY.md>
- Identity: <https://github.com/meshtastic/firmware/blob/develop/src/mesh/NodeDB.cpp>
  (`pickNewNodeNum`, `generateCryptoKeyPair`, `createNewIdentity`)
- Filesystem mount: <https://github.com/meshtastic/firmware/blob/develop/src/FSCommon.h>
- Partition table: <https://github.com/meshtastic/firmware/blob/develop/default_16MB.csv>
- ESP32 platform and BLE: <https://github.com/meshtastic/firmware/blob/develop/variants/esp32/esp32-common.ini>
  (platform pin, `lib_ignore`),
  <https://github.com/meshtastic/firmware/blob/develop/src/nimble/NimbleBluetooth.cpp>
- Variants: <https://github.com/meshtastic/firmware/tree/develop/variants>
- Issues and pull requests:
  - meshtastic/firmware#7143: web UI no longer bundled
  - meshtastic/firmware#8356: Tanmatsu variant, closed
  - meshtastic/firmware#9122: Arduino 3.x migration
  - meshtastic/firmware#9231: unified OTA
  - meshtastic/firmware#9476: pre-hop packets blocked
  - meshtastic/firmware#9526: ESP32-P4 Elecrow boards, open
  - meshtastic/firmware#10012: T-Display P4 port, closed
  - meshtastic/firmware#10401: LR2021
  - meshtastic/firmware#11953: T-Deck stability tracker
- UI and protobufs: <https://github.com/meshtastic/device-ui>,
  <https://github.com/meshtastic/standalone-ui>,
  <https://github.com/meshtastic/protobufs>
- Licensing and trademark: <https://meshtastic.org/docs/legal/licensing-and-trademark/>
- Forks referenced: <https://github.com/W4PFI/tdeck-firmware>,
  <https://github.com/Homertrix/meshtastic-tdisplay-p4>

From a research pass, not re-checked line by line:
- the commit rate;
- the build sizes;
- the 2.8 change list;
- the MUI BLE limitation;
- the trademark wording;
- the remark from the device-ui maintainer.
