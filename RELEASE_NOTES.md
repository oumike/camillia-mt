### New
- Live → Tools → Announce (**A**) introduces your node to the mesh in one press: a NODEINFO broadcast — carrying your position when location sharing is on — plus a telemetry packet, confirmed by a `NODEINFO + telemetry queued.` popup.
- Announce sends telemetry whether or not periodic telemetry is enabled, and is limited to one press every 30 seconds; an early press is refused and tells you how long is left, so a held key can't flood the mesh.

### Changed
- The *Send NODEINFO Broadcast* and *Send Telemetry Now* rows have been removed from the Config screen — both are now the single Announce entry at the end of the Tools grid.
