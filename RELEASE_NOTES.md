### New
- Seven new modem presets: Lite Fast, Lite Slow, Narrow Fast, Narrow Slow, Tiny Fast, Tiny Slow and Medium Turbo, matching the ones Meshtastic 2.7 and 2.8 added.
- The Tiny presets (15.6 kHz) are offered only where the radio can produce them — they are absent on the Attaky Mesh Deck and the Elecrow ThinkNode M9, and importing a config that names one falls back to Long Fast there.
- Two new regions, EU 866 (865.6–867.6 MHz) and EU Narrow 868 (869.4–869.65 MHz), selectable in Config and web config.
- 15.6 kHz is available as a custom LoRa bandwidth on boards whose radio supports it.
- The node detail panel has a new **Signed** row showing whether a node has proved it holds the key you have for it: *yes*, *not seen* or *no key*. Signatures from Meshtastic 2.8 nodes are checked on arrival; nothing is ever rejected for being unsigned or for failing.
- Web config can derive this node's ID from its public key the way Meshtastic 2.8 does. Off by default, never changed by an upgrade, and switching it on gives you a new address on the mesh — your own chat history is carried across.

### Changed
- On a fresh device, picking the **US** region now starts you on Long Turbo instead of Long Fast, matching what a new Meshtastic 2.8 node comes up on. Existing devices are not moved.
- Your long name is now capped at 24 bytes, which is what Meshtastic 2.8 stores and shows. A longer name saved by an earlier build is shortened on upgrade.
- Position precision is capped at about 700 m on any channel anyone can decrypt — no key, or one of the published default keys — and on the MQTT map report regardless of channel. Set a real key on the channel and your chosen precision applies in full.
- A node that has not moved about 90 m since its last position now waits up to six hours between position broadcasts instead of using the configured interval, because 2.8 receivers discard the repeats. Moving restores the normal cadence immediately, and Announce always transmits.
- Packets whose hop count is malformed — including anything from firmware older than Meshtastic 2.3.0 — are no longer relayed, since 2.8 nodes refuse them anyway. Drops are logged under `[fwd]`.
- The amateur-radio regions Meshtastic 2.8 added at 2 m, 70 cm and 125 cm are deliberately not offered.
- The M5Stack Cardputer now advertises itself as custom hardware rather than claiming a hardware ID that belongs to the Cardputer Adv.
- Store-and-forward, map report, beacon and LoRa OTA traffic is now named in the debug monitor instead of appearing as a bare port number.

### Fixed
- Frequency slots are now calculated the same way Meshtastic does, correcting six regions where this node was tuning where no stock node was listening: ANZ 433, UA 433, UA 868, PH 433, PH 868 and KZ 433. Your node may move frequency on upgrade — notably PH 868 on Long Fast, which moves from 868.125 MHz to 869.375 MHz — so upgrade every node in a Camillia-only mesh together.
- Outgoing packets now always carry the field Meshtastic 2.8 uses to tell modern firmware from pre-2.3.0 firmware. Without it, a node with OK-to-MQTT off or a hop limit of 0 had everything it sent silently discarded by every 2.8 node in range.
- Position requests, traceroute replies, routing replies and store-and-forward frames now respect your OK-to-MQTT setting instead of always sending it unset.
- Beacon adverts from senders that split the offer and its text into two packets now show the text in the Beacons view, and no longer go blank on a repeat beacon.
- Chat history written before a node ID change is still recognised as yours, so your own past messages keep their identity instead of appearing to come from a stranger.
