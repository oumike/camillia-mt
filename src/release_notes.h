// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Per-channel hop limit: each channel can carry its own hop budget instead of always using the device-wide Hop Limit. Set it on the device under Config -> Channels -> *(slot)* -> **Hops**, in Web Config with the new **Hops** dropdown on each channel row, or as `hop_limit:` under a channel in `config.yaml`. Leaving it at `Default (n)` keeps the previous behaviour, and `0` means direct neighbours only - nothing will relay that channel's traffic. This is a Camillia-only setting: the phone app can't see it, and it needs no support from other nodes.

Fixed
- The Hop Limit setting is now actually applied to outgoing packets. Previously every message, position, telemetry, ack, traceroute and nodeinfo this device sent used the compiled-in default of 7 regardless of what Hop Limit was set to - the configured value only changed what the info panel displayed. Changing it in Web Config now takes effect immediately, without a reboot.
- Upgrading no longer risks wiping configured channels. The stored channel settings were rejected outright if they came from a build with a different channel count or layout, silently resetting every channel to defaults; older saved channels are now read correctly.
- Web Config no longer floods the serial console with socket errors when a browser closes the page or drops the connection part-way through loading it.)CAMNOTES";
