### Fixed
- On a busy mesh with a full node list, nodes that drop off the list and are heard again now come back with their names straight away. Before, they showed as `!xxxxxxxx` until their next NodeInfo, which could take up to 3 hours. Devices with PSRAM remember the names of up to 1024 dropped nodes; devices without PSRAM remember far fewer. Nodes that were already unidentified before this update get their names back with their next NodeInfo, and from then on they keep them.
- A node that comes back with a remembered name is still asked for its full NodeInfo, so remote admin and direct messages to it keep working.
- Nodes you heard just before a reboot now stay in the node list after it, and nodes that were already stale are dropped first. Before, new nodes heard after boot pushed out loaded nodes at random. This starts working from the second reboot after updating.
- After about 49.7 days of uptime, the node list no longer drops the most recently heard nodes first when it is full.
- Removing a node or clearing nodes now also clears their remembered names.

Pull request: #133
