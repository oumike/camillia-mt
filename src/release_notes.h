// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Discovery, under Live -> Tools: the mesh grouped into direct neighbors with the SNR we measured, nodes by hop count, nodes whose distance is unknown, and nodes we have only heard *about* because a neighbor listed them. Built from NeighborInfo broadcasts that were already arriving and being discarded, so it costs no airtime.
- Discovery sweep: press S (Sweep button on Heltec) to send one NodeInfo broadcast asking nodes within 3 hops to answer, with replies counted for 45 seconds and the new-node total shown when the window closes. Manual only, at most once a minute, and refused - with the reason on screen - when the channel is 25% utilized or busier, when the radio is not ready, or while a periodic announce is still holding the airwaves.
- This node now answers other nodes' discovery sweeps, at most once per requester per hour. Cardputer does this too, even though it does not show the Discovery screen.
- Discovery results are laid out to suit the panel: three columns on the T-Lora Pager, two on the T-Deck and Mesh Deck, and one stacked column on Heltec, which uses a larger result font. Not available on Cardputer, which does not have the RAM to spare.

Changed
- The Live screen's chart shortcuts are now a single Tools modal: T (or the Tools button on Heltec) opens a picker holding SNR/RSSI, ChUtil, and Discovery. The separate U and S shortcuts are gone; S, U and D still jump straight to a tool from inside Tools, and backing out of a tool returns to Live.
- The config screen selection now wraps around: moving past the last item lands on the first, so reaching the bottom of a long list no longer means scrolling all the way down.

Fixed
- A traceroute that had already drawn its route no longer gets overwritten by a "timed out" message when the responder's trailing acknowledgement arrives afterwards.
- Traceroute now shows your own short name at your end of the route instead of a raw !hex node ID.
- Nodes heard over MQTT or from older firmware are no longer mistaken for direct neighbors. They now show under "distance unknown" instead of claiming to be one hop away, and are no longer advertised as our neighbors in our own NeighborInfo broadcasts.)CAMNOTES";
