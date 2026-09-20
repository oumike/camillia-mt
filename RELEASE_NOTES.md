### New
- **Remote administration.** Camillia can now administer another Meshtastic node over LoRa or MQTT — read its device info, owner, config blocks, module config and channels, change individual settings, edit its favourites and ignore lists, set a fixed position, push the clock, and reboot, shut down or factory-reset it. Open a terminal from a node's actions menu with **(A)dmin**, or from **Utilities → Remote Admin** in web config; both share one session, so the browser continues whatever the device already has open. Camillia only administers others — nothing here lets anyone administer your device.
- **Admin peers, with a real authorization check.** A node must be added under **Utilities → Remote Admin** and pass **Verify** — a live probe against the remote — before a terminal can be opened on it. A refusal from the remote demotes the peer and closes the terminal; a timeout does not. Confirmed peers are re-proved after a reboot.
- **Your public key is on the web config page.** A `KEY …` chip beside the tabs copies it with a click — it is what the remote node needs in its `security.admin_key` before any admin command will be accepted.
- **Writes preserve what they do not touch.** Changing one remote setting reads the whole config block first and edits a single field inside the bytes that came back, so settings this firmware has never heard of survive. Destructive commands need the word `confirm` and a fresh session key, and are never retried automatically.
- Admin peers are included in YAML config export and import. Imported peers always come back as `pending` and must be verified again.

### Changed
- **Attaky Mesh Deck: the screen key is the R shoulder button again**, with the same tap-to-sleep / hold-two-seconds-to-unlock rule as every other board. The hardware Power button is no longer bound — a hold there cuts power below firmware — and the tap-only cycle it used is gone, which restores the pocket guard this board had been missing. The BOOT button does the same job on a pin that can also wake the CPU from a nap.
- Admin packets passing through are relayed for the rest of the mesh, so Camillia does not silently break someone else's remote administration where it is the only path.

### Fixed
- Encrypted direct messages arriving over MQTT were silently discarded. They now get through.
