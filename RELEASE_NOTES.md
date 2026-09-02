### New
- T-Deck Pro: the sleep clock now shows unread messages under the date — how many new DMs and how many channels have activity — refreshing a few seconds after things settle so a burst of messages doesn't repaint the e-paper over and over.

### Changed
- T-Deck Pro: the screen now wakes only from the side button — keys and touches no longer wake it, so it can't be woken in a bag or pocket. The side button still both sleeps and wakes the panel, and remote VNC input wakes it as before.
- T-Lora Pager TFT: Sym+Backspace now jumps straight back to the chat screen, closing every open panel and filter at once instead of dismissing one per press.
- T-Lora Pager TFT: the Alt-key global navigation shortcuts from the T-Deck keyboards now work here too.

### Fixed
- Devices set to the Alpha channel now actually find alpha builds — the update check looks up the newest prerelease instead of the newest stable release, so new alphas are offered as soon as they publish.
- Switching to Alpha while running a newer stable build now offers the current alpha instead of reporting "up to date", so stepping back onto the alpha track no longer needs a USB reflash.
- Installing an update now uses the same channel as the update check, so an alpha device can no longer end up installing a stable build it was never offered.
