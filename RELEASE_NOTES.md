### New
- LilyGo T-Deck Pro is now supported (`tdeck-pro`): full mesh UI on the 3.1″ e-paper screen, TCA8418 keyboard, CST328/CST3530 touch, SX1262 radio, GPS, microSD config import/export, and BQ25896 battery reporting. This is an initial port and has not yet been validated on physical hardware.
- T-Deck Pro: the sleep screen keeps a retained e-paper frame showing Camillia, the node name, local time, and date, refreshed once a minute.
- T-Deck Pro: Alt+B toggles the keyboard backlight, and the backlight pulses for new messages whether the screen is awake or asleep.
- T-Deck Pro: Alt+E/F/S/X give Up/Right/Left/Down, Alt+Q sends Esc, and Alt+H returns to chat, with the same H/J/K/D/C/N/L/A shortcuts as the T-Deck.
- Releases now publish T-Deck Pro factory and OTA images with detached signatures.

### Changed
- T-Deck: Alt+H now jumps straight back to chat from any screen, on keyboard-controller firmware dated 2025-06-12 or newer.
- Attaky Mesh Deck: Alt+H returns directly to chat from Config, filters, and nested pickers; plain H still opens the channel selector.
- T-Deck Pro uses one fixed black-on-white Camillia Paper interface, so the Theme, sender-color, and filled-Bubble chat options are hidden on the device and in Web Config; Chat Type offers Default and Outline.
- T-Deck Pro screen updates are batched and rate-limited to suit e-paper, so redraws lag the TFT devices even though keys register immediately.
- Every release build is now signature-verified against its published OTA image before the release goes out.

### Fixed
- Importing a config that names a chat style the board does not offer now falls back to that board's default style instead of applying an unsupported one.
