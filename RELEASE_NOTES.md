### Fixed
- A Discovery preset scan now shows what the radio actually heard while parked on the scanned preset; it was falling back to the ordinary node list, VIA MQTT group included, for the whole five minutes.
- A finished scan's result stays as it was found — nodes heard on your own preset after the radio comes home no longer pile into a list headed with the scanned preset's name.
- HEARD ABOUT no longer mixes in neighbour reports that arrived over the MQTT bridge, so a scan shows the scanned mesh's links rather than a bridged mesh's; the saved snapshot and both counters match what is on screen.
- While a scan has the radio parked on another preset, nothing crosses between the two meshes: MQTT downlink and uplink, relaying, the map report and your periodic NodeInfo, position and telemetry announces are all held until the radio is home, then sent as normal.
- A packet caught in the instant before any retune is discarded instead of being reported as though it arrived on the new settings, which could open a scan with a node from the preset just left.
