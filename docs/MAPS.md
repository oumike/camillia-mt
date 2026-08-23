# Maps

Camillia can show you where a node is, on a real map, with no internet
connection at the device. This document covers how the maps get onto the device
and how to look at them.

- [What you get](#what-you-get)
- [Why the device cannot download maps itself](#why-the-device-cannot-download-maps-itself)
- [Downloading maps](#downloading-maps)
- [Viewing a node's location](#viewing-a-nodes-location)
- [Clearing maps](#clearing-maps)
- [Where the files live](#where-the-files-live)
- [Coverage and limits](#coverage-and-limits)
- [Troubleshooting](#troubleshooting)

## What you get

A node that has reported a position gets a **Locate** entry in its actions menu.
Opening it shows a plain map of the US state that position falls in, with a pin
on the node, the state's name, and the coordinates.

It is deliberately a small amount of map. The detailed, zoomable map — with
every node on it at once — lives in Web Config under the **Map** tab, which runs
in a browser that can fetch map tiles on demand. What the device carries is the
answer to "where is this node, roughly", available with the radio off, no Wi-Fi,
and no phone.

## Why the device cannot download maps itself

The firmware has **no TLS client**. It was removed deliberately: the HTTPS code
was costing internal memory that the Wi-Fi stack and the web config page need
more, on a device where that memory is the binding constraint. Every map tile
service is HTTPS-only, so the device cannot reach one.

Your browser can. So the browser does the downloading — it fetches the tiles,
assembles them into one image per state, and uploads the finished image to the
device over your local network. The device only ever receives a file and writes
it to storage.

This is why maps are a deliberate step you take once, rather than something that
happens on its own.

## Downloading maps

You need the device **joined to your Wi-Fi network** (not its own access point),
and the computer or phone running the browser needs internet access.

1. Open Web Config at the address shown on the device's Config screen
2. Go to the **Utilities** tab
3. Find the **Maps Download** section

The section lists the states your known nodes are actually in, and which of them
are already saved. Only those states are ever downloaded — there is no bulk
download of all fifty, because there is no reason to carry maps for places you
have never heard a node from.

Press **Download maps for known nodes**. Progress is reported per state and per
tile:

```
[2/3] Pennsylvania: tile 4 of 9
[2/3] Pennsylvania: uploading 118 KB
```

Each map is saved as it finishes, so stopping partway keeps whatever is already
done. Re-running the button picks up the rest.

A few things worth knowing:

- **It writes to the card over the same bus the radio uses**, so expect the mesh
  to miss traffic while a download runs. It is not a good idea to do this while
  you are relying on the device
- **Map tiles come from OpenStreetMap.** Scoping the download to states you have
  nodes in keeps it to a few dozen tiles rather than hundreds
- **A map saved at an older resolution is offered for refresh.** If a firmware
  update raises the map size, the section says so — for example
  `Michigan (cached at 240x160, now 384x256)` — and the button replaces it in
  place. Nothing is lost in the meantime; the older map keeps working, it is just
  less sharp
- **The section does not appear at all** on a device with nowhere to save files:
  no card slot, or a slot with no card in it

## Viewing a node's location

On the device:

1. Open **Nodes**
2. Select the node
3. Press **A** for its actions menu
4. Choose **(L)ocate**

The map appears with a pin on the node's last reported position, the state name,
and the coordinates. Close it with the device's close key, Enter, Space, or by
tapping outside it.

The **Locate** row is greyed out for a node that has never reported a position.
It stays visible in its usual place rather than disappearing, so the menu does
not reshuffle from one node to the next.

### Boards without maps

The feature is compiled out on two boards — no Locate entry, no Maps Download
section, no upload routes — for the same underlying reason, not enough memory to
decode a map, reached from opposite directions:

- **Cardputer.** No PSRAM at all. One map costs about 490 KB while it decodes,
  more than that board's entire graphics pool. It also serves web config in lite
  form only, which has no Utilities tab to put the controls on.
- **Heltec V4.** 2 MB of PSRAM shared with everything else, and no card slot —
  maps would have to live in the same internal flash partition as chat history
  and configuration. Worth revisiting if an 8 MB (R8) variant appears.

## Clearing maps

**Utilities → Danger Zone → Clear Maps** deletes every saved map.

It removes only the map files it put there. No node data, no messages, no
configuration, and nothing else on the card — not even another file sitting in
the same folder. It reports how many it removed.

This is reversible: run Maps Download again and they come back. Maps always
arrive from Web Config, so clearing them costs you a download, not the data.

## Where the files live

Maps are PNG files on the device's storage:

```
/camillia/state_maps/MI.png     the map image
/camillia/state_maps/MI.meta    the exact area it covers
```

The `.meta` file records the geographic bounds the image actually spans. That is
what places the pin correctly — the image is not cropped exactly to the state
line, so without it the pin would sit some miles off.

You can copy these files on and off the card directly if you prefer. Anything
that is a valid PNG will be accepted and drawn; the device reads each file's real
dimensions, so the size does not have to match what the current firmware would
download.

## Coverage and limits

- **United States only.** Maps are organised by state, and there are fifty of
  them. A node positioned outside the US keeps a working **Locate** entry, but
  the modal shows its coordinates and says there is no regional map rather than
  pretending something is wrong
- **One image per state**, so the view is a whole state at a time. There is no
  zoom or pan — that is what the Web Config map is for
- **Position comes from what the node reported.** A node that has never sent a
  position cannot be located, however recently it was heard

## Troubleshooting

**The Maps Download section is missing.** The device has no storage it can write
to. Check that a card is inserted and seated; the section appears once one is
mounted.

**The button is greyed out.** Every state your nodes are in is already saved at
the current resolution. If you expected a download, check that the nodes you care
about have actually reported positions — a node with no position belongs to no
state.

**The map area is blank and says "map not on card".** That state has no saved
map. Run Maps Download.

**The map says "No regional map for this location".** The node is positioned
outside the United States. The coordinates shown are still good.

**Downloads fail or stall.** The browser needs internet access, and the device
needs to be on your network rather than serving its own access point. The
access-point page has no Utilities tab at all, which is the quickest way to tell
which mode you are in.
