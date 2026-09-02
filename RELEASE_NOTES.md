### New
- Release Channel setting (Stable or Alpha) in device Settings and the web config page — Stable installs published releases only, Alpha also takes prerelease builds for earlier access to in-progress work. A device flashed with an alpha image follows the Alpha channel automatically until a channel is picked by hand.
- Switching from Alpha back to Stable offers the newest stable release even though it is numerically older than the alpha you are running, so returning to the stable track no longer needs a USB reflash.
- T-Deck Pro: the sleep clock now shows the battery level at the top of the screen, padded in from the edge, in the same percent-or-voltage format as the chat header. It is redrawn with the clock's minute tick, so it costs no extra e-paper refreshes.
- T-Deck Pro: the sleep clock now shows unread messages under the date — new DM count and how many channels have activity — updating a few seconds after things settle so a burst of messages doesn't repaint the e-paper repeatedly.

### Changed
- T-Deck Pro: the screen now wakes only from the side button. Keys and touches no longer wake it, so the device can't be woken in a bag or pocket; the side button still both sleeps and wakes the panel, and remote VNC input still wakes as before.
- T-Lora Pager TFT: Sym+Backspace now jumps straight back to the chat screen, closing every open panel and filter at once, instead of dismissing one panel per press.
- T-Lora Pager TFT: the Alt-key global navigation shortcuts already available on the T-Deck keyboards now work here too.


### Update (v4.7.9)
### Fixed
- Alpha channel now actually finds alpha builds — it looks up the newest prerelease instead of the newest stable release, so devices set to Alpha are offered new alphas as soon as they publish.
- Switching to Alpha while running a newer stable build now offers the current alpha instead of reporting "up to date", so you can step back onto the alpha track.
- Installing an update now uses the same channel as the update check, so an alpha device can no longer end up installing a stable build it was never offered.
