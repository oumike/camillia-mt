### New
- MQTT gateways now forward direct messages relayed between two other nodes to the `PKI` topic. The messages stay end-to-end encrypted. This needs MQTT and encrypted uplink to be turned on.

### Changed
- When a packet is arriving, the node now waits for it to finish before transmitting. You should miss fewer messages around your own sends. This applies to all boards except the Tanmatsu and the LR2021 T-Display P4.
- The signature policies now follow Meshtastic 2.8.2.
- The Balanced policy no longer drops unsigned broadcasts. Instead, if a node has signed before, it can no longer change its name or role with an unsigned update, and a Live line tells you when this happens. To accept such a change, remove the node.
- On Balanced and Strict, node info and position replies sent to a single node are now signed. Nothing is signed on AES-CCM channels.
- A signed node info update from a known node is now rejected if it doesn't advertise the key that signed it, or if that key doesn't match the node's ID.

### Fixed
- Packets that finished arriving just before this node transmitted were thrown away. They are now delivered.
- The Strict policy dropped every acknowledgement, so direct messages showed as failed. Strict now accepts acknowledgements, key verification, and packets on AES-CCM channels.
- Traceroutes signed by Meshtastic 2.8.0 or 2.8.1 nodes were dropped. Signatures on traceroutes are now ignored under every policy, and this node no longer signs its own traceroutes.
- Identity keys restored from a backup or YAML import could produce signatures that every peer rejected. These keys are now repaired automatically. A node with such a key gets a new public key on first boot, and possibly a new node number, and importing one shows a warning saying so.

Pull request: #150
