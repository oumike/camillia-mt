### New

- **Scan SD Card for Malware (ThinkNode M9)** — a new Config entry under Factory Reset, and a matching button in web config under Utilities → Diagnostics, that walks the card for Windows programs, scripts, shortcuts and `autorun.inf` files, including a program hiding under a harmless-looking name. Nothing runs on its own, Camillia's own `/camillia` folder is skipped, and none of what it finds can execute on the radio — the risk is the next PC the card goes into.
- The scan lists what it found and asks before deleting anything; answering yes re-walks the card and removes only what it still recognises, and folders are never touched (ThinkNode M9).
- A progress dialog shows the file count, percentage, folder being walked and matches so far for the length of the scan; **Back** cancels and returns straight to Config, and a cancelled delete reports how far it got (ThinkNode M9).
- **Format** on the scan's dialog rewrites the card's filesystem from scratch after a second, plainer confirmation, then offers to write your configuration back as `/camillia/config.yaml` — everything else on the card is gone (ThinkNode M9).

### Changed

- Every board with a dedicated screen button now follows one rule: **tap to put the device away or to glance at the lock screen, hold for two seconds to unlock**. A tap can never reach the UI, so the most a press in a pocket costs you is a lit lock screen that times itself back out. This covers the Wio Tracker L2's Wake button, the side button on the Heltec expansion boards, and the BOOT button wherever it is not already the UI's action button.
- Waking to the lock screen no longer marks the selected channel or an open DM as read.
- The **Power button is now the screen key on the Attaky Mesh Deck**: a tap cycles dark panel → lock screen → UI → dark, and the BOOT button does the same. There is no hold gesture on this board, because roughly two seconds on the Power button cuts power in hardware.
- The Mesh Deck's top-right shoulder button (BTN_R2), which used to sleep and wake the panel, is now unbound.
- Confirmation dialogs scroll when their text is long — the d-pad, arrow keys and J/K move it — so the Yes and No buttons can no longer be pushed off the panel.

### Fixed

- ThinkNode M9: a light press of the d-pad centre no longer wakes the device straight back to the UI after a centre hold put it away — a tap now raises the lock screen, and only a two-second hold goes through.
