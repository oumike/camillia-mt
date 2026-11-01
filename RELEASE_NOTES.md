### New
- New device role **CLIENT_BASE**, for a powered base station that serves your own nodes, such as a board in an attic or window. It always relays packets from or to the nodes you have favorited, after every ordinary relay has had its turn, and relays everything else like a CLIENT. Favorite your own handhelds on the base: on this role, your favorites decide what gets relayed.
- Between favorited base stations and routers, a CLIENT_BASE can relay a packet without using up a hop. This only works once it has heard the other node's NodeInfo since its last reboot.
- Onboarding and web config suggest CLIENT_BASE on the Heltec V4/R8, CrowPanel Advance 3.5/5.0, T-Display P4 and Attaky Mesh Deck. Every other board accepts it from an imported config, `cfg set role CLIENT_BASE` or remote admin.
- Device Info has a new **Stood down** row. It counts the relays your node cancelled because another node relayed the same packet first. The row is in English only for now.

### Changed
- Every relaying node (CLIENT, TRACKER and CLIENT_BASE) now waits before relaying, the way stock Meshtastic does, and stands down if it hears another node relay the packet first. The wait is longer when the packet was heard strongly. This should use less airtime on busy meshes, but relayed packets can arrive a few seconds later than before: on LongFast a relay now waits about 0.7–12 seconds, where it used to wait 20–150 ms.
- Your node can now hold more relays waiting to go out at once (16, up from 8). The Cardputer stays at 8.
- With power saving on, a light-sleep nap now ends early when a relay is due, so the relay goes out on time.
- On CLIENT_BASE, auto-favorite by distance is ignored, and the node does not send its NodeInfo to every unnamed node it hears.
- Device Info and YAML export now show the ROUTER_LATE and CLIENT_BASE roles by name, instead of UNKNOWN or CLIENT.

### Fixed
- T-Display P4: the whole screen should flash blue less often while chat and DM history, the node archive or the wardrive log are being saved or deleted, and when the SD card is mounted.

Pull request: #136
