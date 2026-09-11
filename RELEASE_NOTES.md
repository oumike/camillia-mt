### New

- **Weather** — a new Tools screen (press **W**, or Live → Tools → Weather) showing current temperature, what it feels like, the sky, humidity, and wind with gusts and direction for wherever the node is. It works out of the box against a public server, units follow Config → Units, **R** refetches, and the reading is cached for ten minutes so reopening it is instant. Not available on the Cardputer.
- The weather title names the place the reading is for — "Weather - Golden, CO" — when the server can resolve one.
- Weather failures say what to change rather than just failing: no server set, no position, no Wi-Fi, server unreachable, or a reply that is not weather. A reading that fails to refresh is kept on screen with its age instead of being discarded.
- Web Config → **Weather Server** points the weather screen at your own server instead of the default; clearing the field turns the screen off entirely and stops the device sending anything.
- Ready-to-run weather server for self-hosting under `tools/weather-proxy/` (Docker Compose or systemd), documented in `docs/WEATHER.md`.
- **Alt+Backspace is Back** on T-Deck, T-Deck Pro, Mesh Deck and Cardputer, doing what the M9's dedicated Back button does. In compose it discards the draft and closes; everywhere else it behaves as an ordinary Backspace.

### Changed

- The message compose box is now ten lines tall on every board, so a full-length message is visible as you type it, and the box no longer resizes or jumps as the text grows.
- The bottom navigation bar is icon-only on every touch board — the shortcut letters beside the icons are gone. The keys themselves are unchanged and still listed on the Help screen, and switching the bar off in Config → Nav Bar still brings back the key-hint strip under the chat.

### Fixed

- T-Deck Pro: the empty white strip below the compose box is gone, and the keyboard legend no longer sits on top of the last line of the message being typed.
