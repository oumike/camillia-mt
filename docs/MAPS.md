# Maps

Camillia's **Locate** action opens a live OpenStreetMap view centered on a
node's last reported position. It uses offline tiles first. With internet
access, missing viewport tiles are downloaded, displayed, and saved for later.

- [Requirements](#requirements)
- [Opening Locate](#opening-locate)
- [Pan and zoom](#pan-and-zoom)
- [Offline downloads](#offline-downloads)
- [Tile cache](#tile-cache)
- [Legacy map migration](#legacy-map-migration)
- [Supported boards](#supported-boards)
- [Troubleshooting](#troubleshooting)

## Requirements

- The selected node must have reported a position.
- Offline use requires writable storage and previously downloaded tiles.
- Filling a missing tile requires a healthy station-mode Wi-Fi connection with
  internet access. Access-point-only mode cannot fetch gaps.
- Online filling also requires the configured Camillia map proxy. The firmware
  currently uses `http://maps.camillia.sumat.org` as its HTTP-to-HTTPS bridge.

The device does not connect directly to OpenStreetMap because the firmware does
not carry a general TLS client. It sends a stable, contactable application
identity through the proxy as required by OpenStreetMap's tile policy.

## Opening Locate

1. Open **Nodes**.
2. Select a node that has a position.
3. Open **Actions**.
4. Choose **Locate**.

Locate opens at zoom `13` with the selected node exactly at the center of the
viewport. A pin remains tied to that geographic position as the map moves. If
the node has no position, Locate is disabled in the Actions menu.

## Pan and zoom

Touch builds can drag the map and use the overlaid controls:

- `+` zooms in.
- `-` zooms out.
- The GPS button recenters the selected node without changing zoom.

Keyboard builds can pan with their directional controls or `I/J/K/L`, zoom with
page controls or `M/N`, and press `H` or `C` to recenter the selected node. Space
returns to zoom `13` and the node-centered position.

The control between `+` and `-` shows the current Web Mercator zoom as a number.
The supported range is `2` through `19`; higher values show progressively more
detail, including building footprints where OpenStreetMap has them.

Longitude wraps across the antimeridian, so panning is not limited by a state,
country, or downloaded area. Latitude stops at the normal Web Mercator limits
near the poles.

## Offline downloads

Web Config's **Maps Download** section builds coverage from every positioned
node currently in the node database. Each node is treated as the center of the
same 282x188 viewport used by Locate. Overlapping tiles are deduplicated.

The detail dropdown selects the highest zoom to include:

- **Roads:** zoom `13` only, up to 6 tiles per positioned node.
- **Streets:** every zoom from `13` through `16`, up to 24 tiles per node.
- **Buildings:** every zoom from `13` through `19`, up to 42 tiles per node.

Actual counts are usually lower because most viewports cross fewer than six
tile boundaries and nearby nodes share tiles. Web Config reports exact cached
and pending counts before starting. Buildings coverage can still take a very,
very long time on a large node database, so it shows a prominent warning and
requires confirmation.

Downloads are resumable. The browser checks only the current plan in batches,
and skips tiles already on storage. Stop waits for the current tile to finish,
then leaves every completed tile intact.

## Tile cache

Tiles use canonical slippy-map paths:

```text
/camillia/map_tiles/<zoom>/<x>/<y>.png
```

Locate loads each cached tile into a bounded PSRAM buffer and composites it into
one RGB565 canvas. A cache miss remains a blank gap when offline. When internet
access is available, the device fetches that exact missing tile and writes it
atomically before drawing it, so normal online panning gradually improves later
offline coverage.

Only complete 256x256 PNG files with a terminal `IEND` chunk count as cache
hits. Interrupted uploads use a temporary filename and cannot become a false
hit.

## Legacy map migration

Older firmware stored whole-state images and fixed 0.1-degree detail images in
`/camillia/state_maps` and `/camillia/detail_maps`. They are not used by the new
renderer.

After an upgrade, firmware checks those directories once. If they contain
files, a modal offers **Keep** or **Remove**. Remove deletes only those two
legacy map directories; Keep leaves them untouched. Either successful choice is
remembered so the prompt does not return on every boot. Empty or absent legacy
directories never produce a prompt.

## Supported boards

Live Locate is available on the map-capable PSRAM builds, including T-Deck,
T-LoRa Pager, Square, Mesh Deck, and M9.

It is compiled out completely on:

- **Cardputer:** it has no PSRAM and cannot safely hold the viewport plus one
  decoded map tile.
- **Heltec V4:** its smaller shared PSRAM budget is not sufficient for this map
  renderer alongside the rest of the UI.

These builds do not show Locate and do not compile the live tile worker or its
buffers. Cardputer also has an explicit compile-time guard preventing accidental
enablement.

## Troubleshooting

**Locate is disabled.** The selected node has not reported a usable position.

**The screen says `Waiting for Wi-Fi...`.** Connect the device to a normal Wi-Fi
network to fill gaps. Cached tiles continue to render without it.

**The screen says `Map unavailable`.** Tile requests completed without a usable
PNG. Check internet and proxy reachability, then inspect serial output for
`[map] live tile rejected` or `[map] live tile fetch failed`.

**The map briefly shows blank strips while panning.** Existing canvas pixels move
immediately with the drag. Cached tiles fill first; remaining strips fill online
as their tiles arrive. Rapid movement invalidates stale downloads instead of
drawing them at an old position.

**Maps Download is very slow.** Tile requests run sequentially through the
device so each file can be validated and committed safely. Stop the run and
resume at a lower detail level, or leave the page open; completed files are not
re-downloaded.

**A policy-denial image appears.** Current firmware rejects OpenStreetMap
responses carrying the `x-blocked` header before drawing them. Older firmware
may display that response as if it were a map tile.