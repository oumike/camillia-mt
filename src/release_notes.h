// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Release Channel setting (Stable or Alpha) in device Settings and the web config page - Stable installs published releases only, Alpha also takes prerelease builds for earlier access to in-progress work. A device flashed with an alpha image follows the Alpha channel automatically until a channel is picked by hand.
- Switching from Alpha back to Stable offers the newest stable release even though it is numerically older than the alpha you are running, so returning to the stable track no longer needs a USB reflash.
- T-Deck Pro: the sleep clock now shows unread messages under the date - new DM count and how many channels have activity - updating a few seconds after things settle so a burst of messages doesn't repaint the e-paper repeatedly.

Changed
- T-Deck Pro: the screen now wakes only from the side button. Keys and touches no longer wake it, so the device can't be woken in a bag or pocket; the side button still both sleeps and wakes the panel, and remote VNC input still wakes as before.
- T-Lora Pager TFT: Sym+Backspace now jumps straight back to the chat screen, closing every open panel and filter at once, instead of dismissing one panel per press.
- T-Lora Pager TFT: the Alt-key global navigation shortcuts already available on the T-Deck keyboards now work here too.

Update (v4.7.9)
Fixed
- Alpha channel now actually finds alpha builds - it looks up the newest prerelease instead of the newest stable release, so devices set to Alpha are offered new alphas as soon as they publish.
- Switching to Alpha while running a newer stable build now offers the current alpha instead of reporting "up to date", so you can step back onto the alpha track.
- Installing an update now uses the same channel as the update check, so an alpha device can no longer end up installing a stable build it was never offered.)CAMNOTES";
