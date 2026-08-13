### New
- T-Deck: experimental VNC Host that mirrors the live screen into a web browser and sends taps and typing back to the device; it requires the T-Deck to be joined to a Wi-Fi network (its own access point is not enough).
- T-Deck: Web Config has a new Remote tab with an "Enable VNC host" checkbox and the viewer embedded right below it, showing whether the host is off, ready, or has a browser connected.
- T-Deck: a "VNC Host" action on the Config screen turns the same setting on or off and displays the viewer address; the setting is remembered across reboots and survives a settings reset.
- T-Deck: the viewer is also reachable directly at `http://<device-ip>:8765/` while the host is enabled — any modern browser works, no VNC client to install.

### Changed
- T-Deck: while a browser is connected to the VNC host, the screen stays awake and Web Config no longer times out, so a remote session is not cut short.
- T-Deck: turning Wi-Fi off also shuts the VNC host down.

### Notes
- The VNC host is plain HTTP with no password of its own and serves one browser at a time; use it only on a trusted local network. It is not a standard RFB/noVNC server, so desktop VNC clients cannot connect.
