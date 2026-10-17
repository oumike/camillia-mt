### New
- **Signed Packets setting** (Settings → Signed Packets, Web Config, or `packet_signature_policy` in YAML), with Meshtastic 2.8.1's three policies. **Compatible** is the default and accepts unsigned packets. **Balanced** also drops an unsigned broadcast from a node that has signed before, when that broadcast would have fitted with a signature. **Strict** accepts only packets with a verified signature, plus encrypted direct messages, so 2.7 nodes go unheard. The setting applies without a reboot, and the picker shows what each policy drops.
- Camillia now signs its own broadcasts (channel messages, position, node info, telemetry, neighbor info) when its node ID is derived from its public key (**Node ID from public key** in Web Config). Once a 2.8.1 node has learned your key, your broadcasts show as signed, and nodes on **Strict** no longer drop them. With a MAC-derived or typed-in ID that doesn't match the key, Camillia sends unsigned. In that case 2.8.1 nodes on **Strict** drop its traffic and every other node hears it.
- Delivery proofs on acknowledgements. When you acknowledge a direct message from a node whose key you hold, the ack carries a proof, so a 2.8.1 sender's app can show the message as verified-delivered to you. Proofs on acks for your own direct messages are checked and logged on the serial console under `[ackproof]`.
- Authenticated channels (AEAD), an opt-in per-channel setting from Meshtastic 2.8.1. Packets altered in flight are rejected instead of being decrypted into something the sender never wrote. Turn it on with the new **AEAD** row in Config → Channels, the **AEAD** checkbox in Web Config, or `use_aead:` in YAML. It is experimental upstream. An AEAD channel can't be reached from Meshtastic before 2.8.1 or from older Camillia builds. Only nodes in the `ALL` rebroadcast mode relay it on older firmware. Each message has 12 bytes less room. Keep the primary channel off AEAD unless every node you talk to runs 2.8.1 or later, and use a secondary channel to try it.
- The Beacons screen marks an offered channel with ` AEAD` when it uses authenticated encryption.

### Changed
- A packet whose signature fails to verify or is malformed is now dropped, under every policy. Before, it was only logged. So is a signed first-contact node info whose key doesn't match the sender's node ID. A dropped packet isn't shown, relayed, sent to MQTT or counted as hearing the node, and each drop is logged on the serial console with the reason.
- Nodes marked **Signed: yes** under Meshtastic 2.8.0 alpha keep the mark. New verifications need senders on 2.8.1. The **Signed: yes** status now survives a reboot, and it is cleared when a node's key is replaced.
- Don't let a Camillia build from before this release share a node ID with one from this release or later, for example by restoring a config backup onto a second device. A 2.8.1 node on **Balanced** that has seen the ID sign will drop the unsigned broadcasts from the older build.
- Rebroadcast mode **CORE_PORTNUMS_ONLY** (the Router role's default) now relays packets this node can't decrypt. For packets it can read, it relays the same set as Meshtastic: it now includes compressed text, waypoints, alerts, key verification and store-and-forward, and it no longer relays neighbor info or mesh beacons.
- Rebroadcast modes **LOCAL_ONLY** and **KNOWN_ONLY** now relay an encrypted direct message between two other nodes when this node knows at least one of them. Unreadable broadcasts and direct messages between two unknown nodes are still dropped.
- In every rebroadcast mode, a direct message that its sender routed through a different relay is now left to that relay.

### Fixed
- Signed packets from Meshtastic 2.8.1 nodes now verify and show **Signed: yes**.
- Signed broadcasts heard over the radio are now checked. Before, only MQTT and direct-message traffic was.
- A node info shared from Camillia, and admin messages sent from it, are no longer dropped by 2.8.1 nodes.
- Remote administration through a node in **CORE_PORTNUMS_ONLY** mode, the Router role's default, works again.

Pull request: #108
