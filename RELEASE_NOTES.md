### New
- Live has a new No MQTT filter that hides every message and packet that travelled over MQTT. Turn it on or off by pressing M on the Live screen, or with the No MQTT row at the top of the filter picker. It works together with the type filter (Text, DMs, Position…), so you can show, say, only radio-heard text messages. While it's on, the header reads "No MQTT: <type>". Your own transmissions and notices always stay visible, and the filter switches off when you leave Live.

### Changed
- Your node's own announcements (node info, telemetry, neighbor info and position updates) are now spaced 3–4 seconds apart instead of going out back to back. This leaves the radio listening between them, so incoming messages are no longer missed while your node announces itself, especially at boot. A full position sweep now takes about 3.5 seconds per location-sharing channel instead of going out all at once. Airtime is unchanged; it is only spread out.

### Fixed
- Tanmatsu: signal strength (RSSI) now shows real values instead of −0.0 on every packet. SNR is reported as 0 because the radio does not provide it.
- Tanmatsu: if the radio fails to return to listening after a transmit or a settings change, it now retries about once a second. Before, it could stay deaf until the next transmit.
- Tanmatsu: the radio's incoming packet buffer is now twice as large, so fewer packets are dropped on a busy mesh.

Pull request: #147
