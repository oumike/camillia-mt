# Wardriving (h0tbyt3 fork)

Changes in this fork aimed at mapping Meshtastic nodes from a moving device
(T-Deck Plus first, but nothing here is board-specific).

## 1. Heard position on every node

Most nodes never broadcast a POSITION packet, so their `latI`/`lonI` stay
empty. Each node now also records **where this device was** when it heard it,
taken from our own GPS fix:

- a **direct** sighting (hop count known and zero, so the RSSI is the node's own
  transmitter) beats a relayed one;
- between two of the same kind, the **stronger RSSI** wins.

Strongest-direct is the best single-point estimate of where a node actually is.
RAM only (cleared on reboot), only stamped while the GPS has a fix, never from
MQTT.

New columns at the end of `/nodes.csv` (and of the SD eviction archive):

| column | meaning |
|---|---|
| `heardLatI`, `heardLonI` | our position at the winning sighting, degrees × 1e7 |
| `heardRssi` | RSSI of that sighting, dBm |
| `heardDirect` | 1 if it was a direct (0-hop) packet |
| `mapLat`, `mapLon` | **what to upload**: the node's own position if it has one, else the heard position, decimal degrees |
| `mapSource` | `node`, `heard-direct`, `heard-relayed`, or empty |

Columns were appended, so readers that go by header name keep working and the
archive restore parser (positional) is unaffected.

## 2. Wardrive log on the SD card

`/camillia/wardrive.csv` gets one line per radio sighting:

```
epoch,utc,nodeId,shortName,longName,rssi,snr,hops,portnum,chanIdx,lat,lon,altM,sats,hdop,speedKmh,nodeLat,nodeLon
```

- `lat`/`lon` are **our** position; `nodeLat`/`nodeLon` the node's own, if known.
- `hops` is empty when the packet did not say.
- At most one line per node every 30 s, or sooner after moving 50 m.
- Only radio packets, only with a GPS fix. Sightings without a fix are counted,
  not logged.
- Written from the main loop in batches (SD shares the SPI bus with the radio);
  a crash or flat battery loses seconds, not the drive.

Web config → **Node Management**: on/off checkbox (applies without a reboot),
session counters, **Download Wardrive Log**, **Clear Wardrive Log**. YAML key:
`nodes: wardriveLog: true`. On by default in this fork (`MY_WARDRIVE_LOG_EN`).

## 3. Discovery shows GPS and log state

The summary line on the Discovery screen ends with `| NO GPS FIX`,
`| GPS 7 sat` or `| GPS off`, plus `| log <nodes>/<lines>` while the log is on.
No more finding out at upload time that the whole drive had no fix.

## 4. Multi-GNSS (off by default)

Behind `-DMY_GPS_MULTI_GNSS=1`: sends `$PCAS04,7` (GPS + BeiDou + GLONASS on
CASIC/AT6558 parts such as the L76K) and `$PMTK353,1,1,1,0,1` for MediaTek
parts, once per boot when the NMEA stream is first confirmed.

Off by default because it made things worse in a field test on a T-Deck Plus:
2 satellites in use with it on versus 5 with stock firmware at the same spot.
Changing the constellation set restarts the L76K's search.

## Notes

- `RhinoConfig` gained `wardriveLogEnabled` at the end of the struct (append-only
  rule). If you flash upstream Camillia afterwards, upstream's next new setting
  would read this byte — check that setting once after switching back.
- Pure helpers live in `src/wardrive_util.h` with a host test:
  `g++ -std=c++17 -I src tools/test_wardrive_util.cpp && ./a.out`
