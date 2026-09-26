### New
- **Language** setting on the Config screen picks the on-device interface language: English, Español, Français, Română or Italiano; saving a different one reboots the device, and anything not yet translated stays in English (all boards except the M5Stack Cardputer, which stays English-only).
- Web Config's **Display** section has the same **Language** setting, and an exported config file now carries it as `language:` under `display:`.
- Accent suggestions: with a non-English language selected, typing a letter that has accented forms pops a small row of them above the message composer and the node name fields — tap one, or step through with Tab, the wheel/trackball or the arrow keys, to swap the letter you just typed; Spanish also offers `¿` after `?` and `¡` after `!`.
- Date separators in a chat follow the chosen language, day first and in that language's month names.
- Languages live in plain text files under `lang/`, so adding one or fixing a phrase needs no code — see `docs/TRANSLATING.md`.

### Changed
- **Every board except the Cardputer must be flashed over USB once to take this release.** The app slots grew from 3.2 MB to 6.25 MB to fit the translations and accented fonts; settings, channels and your node identity survive the move.
- Updating over the air to this release from an older layout is refused before anything is written — the running firmware is untouched and the update screen tells you to re-flash from Camillia's website.
- Heltec V4 and Attaky Mesh Deck: the internal filesystem moved and is now 3.3 MB, so the first boot after this update formats it — stored message history and the node archive start empty.
- LilyGo T-Display P4 held upright: the Messages screen stacks the conversation list above the messages, and the Nodes screen stacks the list above the details, both full width instead of two narrow columns.
- The boot splash draws four large, faint camellias behind the card.
- Web Config: the **LoRa Radio** section now sits above **Channels** on the page.

### Fixed
- Accented Latin letters — a message reading "España", a node named "Íñigo" — now draw properly instead of empty boxes (all boards except the Cardputer).
