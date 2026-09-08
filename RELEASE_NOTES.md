### New
- Support for the Heltec WiFi LoRa 32 V4-R8 paired with the Expansion Kit V2, in both the standard and vertical-UI layouts (`heltec-r8`, `heltec-r8-vertical`) — same features as the existing Heltec V4 builds plus a working microSD slot. This board has not yet been run on real hardware; see docs/BUILD.md before flashing.

### Fixed
- Elecrow ThinkNode M9: the keyboard no longer drops the occasional character when you type at normal speed, and a fast run of keys now comes through in full instead of one key per screen refresh.
- Messages that arrive while the screen is off or locked are no longer treated as already read just because their channel or DM was the last one open — they now show up on the lock screen and glance overlay like any other traffic.
- T-Deck Pro: messages arriving behind the glance overlay are now reported there instead of being skipped.
- Unread markers for the channel or DM you were last in now clear as soon as you wake the device and can actually see the conversation, rather than staying stuck on screen.
