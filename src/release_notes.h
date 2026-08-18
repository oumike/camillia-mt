// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Live now has a traffic filter: press **F** (on Heltec, the **Filter** button in the Live header) to show only Text, DMs, Position, Telemetry, Node info, ACKs, Encrypted, Errors, or Other. Nothing is dropped from the log - switching back to All shows everything that arrived while a filter was up - and the filter resets to All each time you open Live.
- The Live header shows the active filter, and an empty screen under a filter says so by name instead of looking like a dead radio.
- Nodes: press **Enter** to move the arrow keys into the details panel so you can scroll a node's fields; the close key steps back to the list. On the T-Lora Pager the wheel click toggles between the two panels.
- Web config, Nodes tab: each node's detail panel has a **Favorite** / **Unfavorite** button that takes effect immediately, with favorites listed first in the dropdown above a divider and tagged *(favorite)*. Available on every board that serves the full config page - that is, all but the Cardputer.
- Elecrow ThinkNode M9: over-the-air updates now find the M9 firmware build, so the device can update itself like the other boards.

Changed
- Nodes screen redesigned on every board except the Cardputer: the node list moves to the left and shows **long names** on about 45% of the width, and the details on the right are now aligned field tables under Identity, Link, Position and Telemetry instead of one wrapped block of text. On the T-Lora Pager the sections sit two abreast.
- Node details always list every field, with unknown values reading `n/a`, so the panel no longer jumps around as you arrow down the list. Hops reads `n/a` rather than `0` when the packet never reported it.
- Nodes: **A** opens the actions menu (it used to be Enter, which now focuses the details). Heltec is unchanged - the USER button still opens actions, and you drag either panel to scroll it.
- The Cardputer keeps its original Nodes layout - its 240x135 screen has no room for the wide one - but gains the filter, Enter-for-details and A-for-actions behavior.
- M9: the serial log now reports receive health (interrupts, good packets, CRC errors, bad lengths) and the radio's own version and error registers, which is what to capture if the device seems to be missing traffic.

Fixed
- M9: the radio silently stopped accepting any packet longer than its own last transmission, so a node that had just sent a position beacon would drop the text message that followed. Long messages other boards received fine now arrive on the M9 too.
- M9: packets that wrapped around the end of the radio's receive buffer were read incorrectly and thrown away, which lost long messages at a threshold that drifted over time and had nothing to do with distance or signal quality.
- M9: the boosted receive gain setting was never applied to the radio - it moved a field in config and did nothing, on the one board with the least sensitivity to spare. It now takes effect, and the boot log reports what the radio actually accepted.
- M9: a message arriving while the screen was asleep lit a blank backlight and left it glowing until the next real wake, caused by the alert tone stealing the backlight's PWM channel.)CAMNOTES";
