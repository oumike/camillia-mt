# Camillia for Meshtastic Use Guide

This guide reflects current firmware navigation and controls.

## First boot

A freshly flashed device shows a setup screen before anything else. If a
`config.yaml` from a previous device is on the SD card, it offers to import that
instead of asking you to type a name.

**Nothing is transmitted until setup is finished.** Until you complete onboarding
or import a config, the device does not announce itself to the mesh — no
NodeInfo, no position, no telemetry — and it does not answer other nodes asking
who it is. Otherwise a new device would spend its first minutes telling the mesh
it was called "Camillia" and sitting at the firmware's built-in fallback
coordinates, which are a real place and almost certainly not yours.

It still listens the whole time, and still relays other people's traffic — a
relayed packet carries their identity, not yours. Pressing **Announce** in web
config does transmit, because that is you asking for it deliberately.

## Main screen

The main screen is channel chat. Use it to read traffic, select reply targets, and start compose.

Typical flow on keyboard builds: pick a channel, press **Enter** to move the
cursor into that channel's messages, scroll to a message to select it, then press
**Space** to compose (a reply if a row is selected, otherwise a new message).

## Keyboard shortcuts by build

### Shared shortcuts (keyboard builds)

These apply to all keyboard builds: `tdeck`, `tlora-pager-tft`, and `cardputer-cap`.

- D opens Direct Messages
- C opens Config
- N opens Nodes
- L opens Live
- A opens Channel Actions — M mutes/unmutes the channel, L toggles whether this
  node broadcasts its position on it (Share Location in Config gates all channels)
- In the DM list, D deletes the selected conversation. A confirmation dialog
  names the conversation first — see [Direct messages](#direct-messages)
- **Space opens compose** — a new message, or a reply when a chat row is
  selected. Space replaced Enter for this on both the chat and DM screens.
- **Enter moves the cursor into the messages** — on chat it drops into the
  selected channel's messages; in the DM list it focuses the conversation's
  messages. Enter never opens compose.
- **Enter again opens Message Actions** for the highlighted message — see
  [Message Actions](#message-actions). Nothing happens on your own messages or
  on system lines. Esc closes the menu and leaves the cursor exactly where it
  was.
- **On touch builds, tap and hold a message** to open Message Actions directly,
  with no need to enter cursor mode first. Available on T-Deck, Mesh Deck and
  Heltec; the Pager and Cardputer have no touch panel and use the Enter route
  above. Holding your own message or a system line does nothing, and the tap
  that ends the hold does not also select the message for reply.
- Note: inside the compose box, Enter still **sends** the message.
- Live modal shortcuts: C clears the log, F filters the feed by traffic type, and
  T opens the Tools modal (SNR/RSSI, ChUtil and Beacons everywhere, plus
  Discovery and MQTT except on Cardputer). Inside Discovery: W sweeps, C clears,
  S saves a snapshot to SD. Inside Beacons: C clears. Inside MQTT Monitor: C
  restarts the count and S sends the top 5 to a channel

### LilyGo T-Deck (tdeck)

- H toggles the channel selector
- J/K map to Up/Down navigation in lists and chat row selection
- Trackball Up/Down follows the same Up/Down behavior as J/K
- Modal close key is Backspace (Esc is also accepted)
- Compose close behavior: Backspace on an empty compose closes the compose modal
- Trackball click hold for 2 seconds puts the screen to sleep

### LilyGo T-Lora Pager TFT (tlora-pager-tft)

- H toggles the channel selector
- Wheel Up/Down on main chat switches channels
- Wheel Click enters/exits chat row cursor mode (Enter does the same thing)
- In chat row cursor mode, Wheel Up/Down moves the selected chat row
- Backspace exits chat row cursor mode
- Config modal: I focuses the info panel; Wheel Click swaps focus between actions/info
- DM modal: Wheel Click swaps focus between conversation and message panels
- Modal close key is Backspace (Esc is also accepted)
- Compose close behavior: Backspace on an empty compose closes the compose modal

### M5Stack Cardputer + Cap LoRa/GPS (cardputer-cap)

- H toggles the channel selector
- Channel switch: comma (previous), slash (next)
- Navigation: semicolon (Up), period (Down)
- Arrow keys map to the same directional actions
- Esc closes modals and exits chat-focus modes
- Enter confirms actions and moves the cursor into the channel's messages;
  Space opens compose, and Fn+Enter is also accepted for the compose/reply flow
- Compose close behavior: Esc closes compose (Backspace only deletes characters)
- Picker modals (Chat Style, Chat Names) show option names without the
  explanatory line underneath, and scroll when they outgrow the 240x135 panel

### Elecrow ThinkNode M9 (m9)

- Six dedicated hardware buttons below the screen jump straight to a surface
  from anywhere, closing whatever is open first: **Messages** (DMs), **Home**,
  the function key below Home (Live), the key below Back (Nodes), and **Map**
  (Discovery)
- **Hold the d-pad centre to put the screen to sleep** — the same gesture as
  holding the T-Deck's trackball click. A tap of the centre is still Enter. How
  long counts as a hold is decided by the keyboard controller itself, not by the
  firmware, so it may not be exactly two seconds
- **Only the d-pad centre wakes the screen.** Every other key is ignored while
  the screen is off, and the press that wakes it does nothing else — it does not
  also open the surface it belongs to. This board rides in a pocket with its keys
  and shortcut buttons facing outward, and with any key able to wake it the first
  press woke the screen and the rest went in as real input, which sent messages
  nobody meant to send. The centre click and the keyboard's Enter are the same
  code on the wire, so Enter wakes it too
- D-pad Up/Down navigates lists and chat rows; Left/Right switches channels, and
  moves between columns in multi-column pickers (Tools, channel grid)
- In the New Message box the d-pad moves the text cursor instead: Left/Right by
  one character, Up/Down by one line. Typing inserts at the cursor and Back
  deletes the character before it, so a typo several words back can be fixed
  without deleting everything after it
- Modal close key is Back; hold Back for the long-press close. (A held Enter no
  longer closes — past two seconds it is the sleep gesture above)
- No touch panel and no trackball — the keyboard and d-pad are the only input
- The keypad has its own backlight, driven by the host

### Heltec WiFi LoRa 32 V4 + TFT expansion

Builds: `heltec-v4`, `heltec-v4-vertical`

- Primary usage is touch (no dedicated hardware keyboard shortcuts)
- **Chat and DM transcripts survive a reboot.** This board has no card slot, so
  they go to a LittleFS partition in its own flash. Devices flashed before this
  existed need one USB flash to get the partition — the partition table is not
  part of an OTA update, so an OTA alone leaves history in RAM as before
- Bottom touch nav: Home, DM, Nodes, Live, Config, Help — the same six in the
  same order everywhere.
  Actions is not one of them: it acts on the channel you are reading, so it
  lives on the chat screen only
- **Under the chat: Actions on the left, New Message on the right**, splitting
  that strip one third to two thirds. Both act on the conversation you are
  reading, which is why they are together under it rather than in the nav bar
- DM delete trigger: long-press a conversation row for 3 seconds, then answer
  the Delete conversation? dialog
- Tap and hold a chat message to open Message Actions (reactions, Reply, and
  the sender's node actions). On this
  build that is the only route to the menu from chat, since Enter keeps its
  new-message binding below
- The Space/Enter remap above does **not** apply here: this build is touch-first,
  so Enter keeps its original "new message" behavior

![Chat screen (Outline)](screenshots/RiCa_screen_20260730_193708.png)
![Chat screen 2 (Bubble)](screenshots/RiCa_screen_20260730_193759.png)
![New message](screenshots/RiCa_screen_20260730_193858.png)
![Emojis](screenshots/RiCa_screen_20260730_193919.png)

## Live screen

Live shows decoded RX and TX traffic with per-traffic coloring.

- Open from the main screen (L on keyboard builds, Live bottom-nav button on Heltec)
- Scroll with Up and Down input
- Press C to clear the log
- Press F for the traffic filter (on Heltec, the filter button in the Live
  header) — see [Traffic filter](#traffic-filter) below
- Press T for Tools (on Heltec, the Tools button in the Live header) — a
  two-column picker holding the SNR/RSSI chart, the channel-utilization chart,
  Discovery, Beacons, and the MQTT Monitor. Enter opens the selected tool, and
  S, U, D, B or M jumps straight to one. Backing out of a tool returns to Live,
  not to Tools.
- The MQTT cell is only built on boards with WiFi compiled in (so: not
  Cardputer), and is only live while WiFi is switched on in config. With WiFi
  off the row reads `MQTT - WiFi off` and does not open.

### Traffic filter

On a busy mesh the lines worth reading — text, DMs, errors — scroll away under
position and telemetry chatter. The filter narrows the feed to one kind of
traffic.

- Press **F** to open the picker (on Heltec, tap the filter button in the Live
  header). F again closes it, as does the device close key; neither changes the
  filter
- Move with Up/Down, hop columns with Left/Right, and press Enter to apply. The
  picker opens on the filter already in force
- Options: **All** (the default), Text, DMs, Position, Telemetry, Node info,
  ACKs, Encrypted, Errors, and Other — the last one catching anything the named
  rows do not, so no line is unreachable. Each covers both directions: *Text* is
  sent and received text, not one or the other
- The active filter is shown in the Live header, e.g. `Filter: Telemetry`
- **The filter only changes what is drawn.** Nothing is dropped from the log:
  switching back to All shows everything that arrived while a filter was up, and
  C still clears the whole buffer regardless of what is on screen
- New matching traffic keeps appending live while a filter is active
- When a filter matches nothing the screen says so by name, so an empty list
  can't be mistaken for a silent radio
- The filter resets to All each time you open Live. It is a way of looking at
  the feed, not a setting, so one left on can never masquerade as a quiet mesh

### Discovery

Not available on Cardputer: the neighbor table and result buffer cost about 3 KB
of internal RAM, and first-boot onboarding there (WiFi AP plus the lite web
config) has less headroom than that. The Tools modal on Cardputer holds the two
charts and Beacons. Cardputer still broadcasts its own NeighborInfo and still
answers other nodes' discovery sweeps — it just does not keep or display the map.

Discovery answers what the Nodes screen cannot: not just who we have heard from,
but how the mesh is shaped around us. It groups every node we know of into
direct neighbors (with the SNR we measured), nodes by hop count, nodes whose
packets never said how far away they are, and — the group nothing else shows —
nodes we have only ever heard *about*, because a neighbor listed them in its own
neighbor report.

- Open from Live → Tools → Discovery
- Scroll with Up and Down input
- Results are laid out to suit the panel: three columns on the T-Lora Pager
  (direct / distance / heard about), two on the T-Deck and Mesh Deck (heard
  about on the right), and a single stacked column on Heltec, which uses a
  larger result font
- Nodes show their long name when one is known, falling back to the short name
  and then the hex id. Names are clipped to the column width rather than
  wrapped, so the list stays scannable — the full name is on the Nodes screen
- Group headings (DIRECT, 1 HOP, HEARD ABOUT, …) are drawn larger and in amber
  so the groups separate at a glance
- Everything above is passive: it is built from NeighborInfo broadcasts that were
  already arriving, and costs no extra airtime
- Press W (Heltec: the Sweep button) to sweep: **one** NodeInfo broadcast asking
  nodes within 3 hops to answer, and replies are counted for 45 s. Never
  automatic. A sweep is refused, with the reason on screen, when one ran less
  than 60 s ago, when channel utilization is at or above 25%, or when the radio
  is not ready.
- We answer other nodes' sweeps too, at most once per requester per hour
- Press C (Heltec: the Clear button) to clear. Every group empties, and the
  screen refills from live traffic — a node reappears the moment it next
  transmits, and a neighbor report when that node next broadcasts one. That
  turns the screen into "who is out there right now" rather than everything
  this device has ever met, which is the useful question after moving.
  Clearing is not destructive and does not touch the Nodes screen: stored
  neighbor reports really are dropped, but the rest is hidden by a timestamp,
  not deleted. Node records, names and last-heard times are all untouched —
  discarding those is Config → Clear Nodes (Keep Favorites) or Clear Nodes (All).
- Press S to save a snapshot, on boards with an SD card (T-Deck and T-Lora
  Pager). Writes `/camillia/discovery-YYYYMMDD-HHMMSS.json` — timestamped, so
  saves never overwrite each other, and suffixed `-2`, `-3`… if two land in the
  same second. With no clock set yet the name falls back to uptime
  (`discovery-boot-123s.json`) and the file's `generated` field is `null`
  rather than a made-up date. The JSON carries every group the screen draws
  plus the raw neighbor reports, so the graph can be rebuilt from the file.
- Close with the device close key (see device sections below)

### Beacons

Beacons are advertisements from *other* meshes — see [Mesh beacons](#mesh-beacons)
for what they are and for the setting that turns listening on. This screen is
where the ones this device heard are shown. It is available on every board,
Cardputer included: there is no neighbor table behind it, just the handful of
records the receive path fills in.

They get a screen of their own rather than a group on Discovery because of the
timing. A sender only beacons on its own schedule, minutes or hours apart, and
only while it has retuned onto your channel, preset and region — so a group on a
screen built around a 45-second sweep was empty nearly every time you looked at
it.

- Open from Live → Tools → Beacons (B), or the Beacons cell on Heltec
- Scroll with Up and Down input
- One card per sender, newest first, holding everything that beacon carried: the
  sender (its name if this device happens to know it, otherwise the hex id), the
  message it sent, what it offered, the SNR, RSSI and hop count it arrived with,
  and when it was last heard
- The offer line reads *channel / preset / region* — whichever parts the beacon
  carried, `(nothing)` when it carried none. A `*` after a channel name means the
  offer included a key. Nothing here is ever applied to your radio; acting on an
  offer is manual, on the Config screen
- Once a sender has beaconed twice, its card also says how many have been heard
  and roughly how far apart they have been — which is the number that tells you
  when it is worth looking again
- Eight senders are kept (four on Cardputer). A repeat from a known sender
  updates its card rather than taking a second slot; a ninth sender pushes out
  the one heard longest ago
- Press C (Heltec: the Clear button) to clear. The list refills only as senders
  beacon again, which can be a long wait — nothing else is affected
- The list also empties when Mesh Beacons is switched off, so an old offer can
  never sit on screen after the feature has been turned off
- Close with the device close key (see device sections below)

### MQTT Monitor

Not available on Cardputer, for the same reasons Discovery is not: first-boot
onboarding there has no heap to spare, and a two-column list on a 240x135 panel
is not where you would want to read this.

A census of what is arriving on the broker: a scrolling grid of channels under
`<root>/2/e/#` and the number of messages seen on each, laid out two cells to a
line so the panel is not mostly empty space. It answers
"is this root actually carrying traffic, and on which channels" without spending
the RAM a message viewer would — payloads are counted and dropped, never stored
or displayed.

- Open from Live → Tools → MQTT (M), or the MQTT cell on Heltec
- Requires a build with WiFi, WiFi switched on, the MQTT bridge on, and a broker
  session. When any of those is missing the screen says which one instead of
  showing an empty list
- The title is the filter being watched, `<root>/2/e/#`
- **Rows are channels, not whole topics.** An envelope topic is
  `<root>/2/e/<channel>/<gateway>`, and the gateway is merged away, so three
  gateways relaying KAM-NET are one row reading 3 rather than three rows
  reading 1:

  ```
  msh/US/MI/2/e/KAM-NET/!699c90c8  ┐
  msh/US/MI/2/e/KAM-NET/!699c9234  ┴──▶   KAM-NET    2
  msh/US/MI/2/e/CFW/!b2a77a48      ───▶   CFW        1
  ```

- Counts uptick live as messages arrive, and only while the screen is up
- The status line reads *channels · messages · how long this session has been
  counting*, so a message count has a denominator
- Cells fill left to right, top to bottom, in first-seen order, and stay put;
  only the numbers move. Nothing is sorted or reshuffled under you while you read
- Nothing persists. Counting starts when the screen opens, the table is freed
  when it closes, and reopening starts from zero. Press C (Heltec: the Reset
  button) to start over without leaving the screen
- **Press S (Heltec: the Send button) to put the top 5 channels on the mesh.**
  See [Sending a summary](#sending-a-summary) below
- Bounded on purpose, so leaving it up all day costs what one minute costs: 32
  channels are tracked and messages on channels past that are tallied together
  as `(+n off-list)` on the status line rather than evicting a row. Every counter
  stops at 999999 and prints with a `+` instead of wrapping
- It rides the subscription the bridge already holds rather than opening a
  second connection, so it neither adds broker load nor changes what the bridge
  does with downlink traffic
- Close with the device close key (see device sections below)

#### Sending a summary

Puts the five busiest channels and their counts on the mesh as an ordinary text
message, for when what the broker is carrying is worth telling people who are not
standing next to you.

- Press **S** (on Heltec, the **Send** button in the header) to start
- Pick a channel from the grid — every configured channel is listed, the same
  slots the channel selector shows. Move with Up/Down, hop columns with
  Left/Right, Enter picks; on Heltec, tap one
- **A confirmation follows, showing the exact text that will be transmitted** and
  the channel it is going to. Enter sends, the close key cancels (on Heltec, the
  Send and Cancel buttons). The backdrop is not a dismiss target here — this is
  the last gate before a transmission, so leaving it takes a deliberate no
- The message reads like
  `MQTT msh/US/MI 12m: KAM-NET 42, CFW 18, LongFast 7`, and is trimmed to whole
  entries if it would run past Meshtastic's 200-byte text limit
- **One send per minute.** After a successful send the action is refused for 60
  seconds and says how long is left — this is a broadcast on a shared channel and
  it should not be possible to lean on the key. A send that fails to transmit
  does not start the clock
- Refusals and results appear on the status line for a few seconds: `Sent to
  LongFast`, `Wait 43s before sending again`, or
  `Nothing recorded yet - nothing to send`

![Live screen](screenshots/RiCa_screen_20260730_195834.png)

## Config screen

Config includes Web Config controls, export and import, the theme picker, announce, and reset actions.

- Open from the main screen (C on keyboard builds, Config bottom-nav button on Heltec)
- Navigate action rows with Up and Down input. The list wraps: going up from
  the first row lands on the last, and down from the last returns to the first
- Enter runs the selected action
- **On the touch-only Heltec, tapping a row runs it** — there is no Enter key to
  press, so the tap is the whole gesture. Dragging the list to scroll it does
  not count as a tap and never runs anything. On the other touch builds a tap
  moves the highlight and Enter still runs it
- Keyboard builds: I opens/focuses the info panel within Config
- Import, both Clear Nodes rows, and Factory Reset require a second Enter
  confirmation
- **Space filters the rows**, the same way it does on the Nodes screen. Press
  Space to arm the filter, then type to narrow the list; the header shows
  `[what you typed]` and how many rows match. Backspace edits the filter and
  disarms it once empty, Up/Down and Enter work normally on whatever is left,
  and closing Config clears the filter. Rows are matched on the label you can
  see, so typing part of a value works too — `on` finds every setting currently
  switched on. Keyboard builds only; the touch-only Heltec has no Space to press.

### Location precision

**Share Location** decides whether this node puts its coordinates on the mesh at
all. **Location Precision**, the row underneath it, decides how exact those
coordinates are when it does.

Anything below Precise rounds the position to a grid before it is transmitted,
so the mesh learns roughly where you are without learning exactly where you are.
The choices run from ~50 m to ~23 km; the label is the width of the grid square,
and the transmitted point is the middle of it, so the error is never more than
half that in any direction. The device goes on using your real fix locally — the
compass, distances and the nodes list are unaffected.

Transmitted packets carry how many bits of the coordinate are real
(`Position.precision_bits`), which is the same mechanism stock Meshtastic uses,
so other clients can render an area instead of a false pinpoint.

Enter on the row opens a slider whose stops are the available precisions, so it
cannot land between two of them. The label above it names the current stop
("Precise", "Within ~350 m"). Move with the usual up/down input, Enter saves,
Backspace/Esc cancels; on touch builds drag the slider and press Save. Nothing
is applied until you save — a position broadcast that happens while the slider
is open still goes out at the precision you last committed.

The same setting is under Position in web config, and it defaults to Precise —
a firmware update never starts obfuscating a position on its own.

One difference from stock Meshtastic: theirs is a per-channel setting, so a node
can be exact on a private channel and coarse on a public one. Ours is one
device-wide value applied to every channel this node shares position on.

### MQTT map reporting

**Map reporting**, under MQTT in Web Config, publishes a short self-description
to the broker every 15 minutes: long and short name, hardware model, firmware
version, region, modem preset, whether the primary channel still uses the
default key, how many nodes this one has heard in the last two hours, and a
position. That is what puts a node on an MQTT-fed map — it is a message to the
broker rather than mesh traffic, so it never goes out over LoRa and no other
node sees it.

- It needs the MQTT bridge switched on and connected. The report goes to
  `<root>/2/map/`, alongside the `<root>/2/e/` topics the bridge already uses.
- **Nothing is published without a position.** If Share Location is off, or
  there is no fix and no fixed position set, the report is skipped and the
  serial log says so — a map report with no coordinates has nothing to map.
- The position carries the same Location Precision as the one broadcast on the
  mesh, coarsened the same way, so map reporting can never be more revealing
  than what you already agreed to transmit.
- The 15-minute interval is fixed and not configurable. It matches the stock
  Meshtastic default, which public brokers expect as a floor.
- Everything else about the node — its telemetry, position and nodeinfo packets
  — reaches the broker through the ordinary uplink instead, which needs the
  channel's uplink flag on.

### Battery display

**Battery Display** on the Config screen (and under Display in web config)
switches the battery indicator between the two ways of reading the same cell:

- **Percent** (default) — `87%`, a state of charge derived from voltage through
  a Li-ion discharge curve.
- **Voltage** — `3.94V`, the number that curve is derived from.

Voltage is worth switching to if you have calibrated the unit under Battery
Calibration, or if you run a cell the standard curve does not describe. On the
flat middle of a Li-ion curve a tenth of a volt swings the percentage by tens of
points, so the raw reading is the steadier number to judge by there.

The setting applies to the chat header and the web `BAT` chip. It changes
nothing else:

- The colored dot beside the reading still tracks **charge**, not volts, in both
  modes — a voltage next to a green/amber/red dot is the point of the mode.
- Device Info and the Battery Calibration modal keep showing **both** numbers;
  that is what those screens are for.
- What this node **transmits** is unchanged. Telemetry carries battery level and
  voltage as it always did, so other nodes see no difference.

It applies immediately with no reboot, and travels with config export/import as
`display: battDisplay:` (`PERCENT` or `VOLTAGE`).

### Theme

The **Theme** action opens a picker rather than cycling. Each theme/mode preset
gets a row with its name and a three-swatch preview — background, panel, and
accent color — so you can see what you're choosing. Navigate with the usual
up/down input and press Enter (or tap) to apply; it takes effect immediately, no
reboot. Backspace/Esc (or tapping outside) cancels. Web Config shows the same
themes as a grid of swatch cards, and previews the selected one live before you
save.

**Scrolling wraps.** Moving up from the first theme lands on the last, and down
from the last returns to the first — with nearly thirty themes in the list, the
bottom of it is otherwise a long way from the top.

**Space filters the rows**, the same way it does on the CFG and Nodes screens.
Press Space to arm the filter, then type to narrow the list to themes whose names
contain what you typed, case-insensitively — `dark` for every dark variant, `sun`
for `Sunset Ridge`. The line under the title shows `[what you typed]` and how many
themes match, with the brackets appearing as soon as the filter is armed even
before you type anything.

Arming matters because **j/k keep navigating until you press Space**. Once armed
they are letters instead — which the filter needs, since every dark theme's name
ends in `Dark`. The wheel, trackball, D-pad and arrow keys navigate either way.

Backspace edits the filter, an empty filter disarms, and the next Backspace
closes the picker as usual. The selected theme stays selected as long as it still
matches, so narrowing the list doesn't move your choice out from under you.

Web Config's theme grid has a matching **Filter themes** box above it, with a
count of how many cards are showing. Pressing Enter there with a single match
selects it.

#### Building your own theme

Web Config's theme grid ends with a **+New** card. It opens a builder where you
give the theme a name, pick its four colors, and choose Light or Dark.

Four colors is the whole theme: **Background**, **Panel**, **Panel Alt** and
**Accent**. Everything else the interface uses — dividers, selection highlights,
the unread tab tint — is derived from those four, the same way the built-in
themes are, so a custom theme behaves like a real one rather than four flat
colors.

**Light/Dark is not decoration.** It selects the text, dim-text and on-accent
colors — body copy, labels, the typing caret — which are fixed per mode. Pick the
one your background actually is, or your theme will be unreadable.

- Each color has a swatch picker and a text box; you can type a hex code like
  `#1a2230` into the box instead of using the picker.
- The preview panel in the builder shows title text, dim text and an accent chip
  in the colors you have chosen so far.
- **Save Theme** stores it and adds it to the grid immediately. It also appears
  in the on-device Theme picker, where it can be selected like any built-in.
- There are **4 slots**. The +New card disappears when they are full.
- Saved themes get two small buttons in the corner of their card: **✎** reopens
  the builder to modify the theme, and a red **-** deletes it (with a
  confirmation). Editing writes back to the same slot, and if you are wearing
  that theme the device repaints as soon as you save.

#### Moving themes between devices

Custom themes are written into the SD config backup (`/camillia/config.yaml`) as
`themeCustom<n>` lines, so they ride along with a config export and come back on
a restore, into the same slots they came from.

Each line's value is a share code — a short hex string like
`0101FFF2F6FFFAFFFFEAEED51C390C53756E736574` carrying the name, the four colors
and the mode. To move one theme rather than a whole config, copy that value out
of the file, paste it into the builder's **Import code** field, and click
**Load**. It fills the form rather than saving straight away, so you can rename
or adjust the theme before committing a slot; **Save Theme** keeps it.

Codes carry a checksum, so one mangled in transit is rejected rather than loaded
as the wrong colors. Spaces, dashes and lowercase are tolerated, since a code
copied out of a text file usually picks some up.

### Information panel

The device info panel is scrollable with the keyboard on every keyboard build:

- **T-Lora Pager** — I focuses the info panel (Wheel Click also swaps between the
  action and info panels), Wheel Up/Down scrolls it, Backspace returns to the
  action list without closing Config
- **T-Deck** — I opens the info popup, J/K (or the trackball) scroll it, and I or
  Backspace closes it
- **Cardputer** — I opens the info popup, Up/Down (or semicolon/period) scroll it,
  and I or Esc closes it
- **Heltec** — touch-only; the popup has a **Close** button beside its title.
  (Any key still dismisses it, which is what a keyboard driven over VNC sends)

### Notification sound

**Notification Sound** opens a picker (same navigation as Chat Style) with
Default, Chirpy, Bass, and Off. Moving the selection **plays that tone as a
preview**, so you can hear each one before committing. Enter applies the
highlighted tone; Backspace/Esc cancels and restores whatever was set before you
opened the picker, so previewing never changes your setting by accident. On
touch builds, tapping a row previews it, and **Apply** commits — tapping the
highlighted row a second time still works too. **Cancel** restores what you
opened the picker with, and so does a tap outside it.

Notification Sound and Splash Melody now sit directly under My Message Color,
with the other presentation settings.

**The three tones differ by board, because the hardware does.** The T-Deck,
Pager and Cardputer play them through a speaker or codec. The Heltec and M9
have a passive piezo buzzer instead, which has one resonant peak around
2-4 kHz and drops off sharply below it — so their patterns sit higher, and
their Bass is the lowest register the part still projects, set apart by being
slower and longer rather than genuinely low. Until now those two boards played
a single beep for all three settings.

### Light timeout

On boards with a notification light — the Mesh Deck's RGB LED, and the T-Deck
and T-Lora Pager's keyboard backlight — that light repeats once a second for as
long as anything is unread. A message that lands overnight blinks all night, and
on the keyboard-blink boards it also keeps the device out of light sleep, so it
costs battery as well as attention.

**Light Timeout** stops that after a set time: Never, 30 sec, 1 min, 5 min, or
30 min. It is a single setting because no board has both lights.

- The clock restarts on every new message, so a busy channel keeps the light
  going and silence lets it lapse. A fresh message re-arms it.
- Reading the message stops the light immediately, exactly as before.
- A blink already in flight is never cut off mid-pattern; the light always
  finishes and ends dark.
- The default is **Never**, which is what every earlier build did, so an update
  changes nothing until you pick a timeout.
- The row is on the Config screen next to the other notification settings, and
  in web config under Notifications. It is absent on Cardputer and Heltec, which
  have no light to blink.
- Enter on the row opens a slider whose stops are the available timeouts, the
  same picker Location Precision uses. It runs shortest to longest left to
  right, ending at "Until read". Enter saves, Backspace/Esc cancels, and nothing
  is applied until you save.

One behaviour change on the keyboard-blink boards: a message that arrives while
the screen is awake produces no blink at the time, and previously it would start
blinking whenever the screen next slept — however many hours later. With a
timeout set, the window is measured from when the message *arrived*, so if the
screen sleeps after it has expired the light stays dark.

### Node management

The device keeps a fixed number of the most recently heard nodes: **250 on every
board except the Cardputer, which holds 50**. When that fills up, the **least
recently heard non-favorite** is dropped to make room — **favorited nodes are
never dropped, however old they are**. The only things that drop a favorite are
the two deliberate wipes: Clear Nodes (All) and Factory Reset. Clear Nodes (Keep
Favorites) exists precisely so you can flush a table full of stale mesh nodes
without losing the ones you pinned.

**Why the Cardputer holds fewer.** It is the only board here with no PSRAM, so
its node table competes with Wi-Fi, the LVGL pool and the web-config page for the
same internal memory — a full 250-entry table is 41 KB of it. Fifty entries is
what leaves that board able to serve its own settings page. Nothing about the
mesh changes: it still hears, displays and routes for every node it receives. It
just remembers fewer of the ones it has not heard from lately, so on a busy mesh
expect the table to turn over sooner and **favorite the nodes you care about**.

Optionally, dropped nodes can be preserved instead of discarded. In Web Config,
**Node Management** has an *Archive dropped nodes to SD card* checkbox:

- It is **off by default** — archiving only happens if you turn it on
- It cannot be enabled on a board with no SD slot, or with no card inserted; the
  reason is shown in place of the description
- When on, each dropped node is appended to `/camillia/nodes_archive.csv`

The same section has an **Export Node List (CSV)** button, which downloads every
node the device currently knows about plus any previously archived nodes. A
`source` column marks each row as `live` or `archived`.

### Clearing the node database

Both the Config screen and Web Config's Danger Zone offer two variants:

- **Clear Nodes (Keep Favorites)** — drops every non-favorited node. Favorites
  survive with their names, keys, positions and favorite flag, and are still
  there after a power cycle. The device reports what happened, e.g.
  `Cleared 214 nodes, kept 6 favorites`.
- **Clear Nodes (All)** — empties the table completely, favorites included.

Both reboot afterwards. Neither touches DM transcripts (that is Clear Messages)
or the archived-node CSV above, which is a historical log rather than live node
state. Under Discovery, stored neighbor reports are dropped by either variant;
the mesh rebuilds that graph from the next round of broadcasts.

### Mesh beacons

Meshtastic 2.7 nodes can briefly retune their radio to advertise a *different*
mesh — a short message plus an optional offer naming a channel, preset and
region. **Mesh Beacons** on the Config screen (and under Modules in web config)
decides whether this device pays attention to them.

- **Off by default.** Turning it on is the only thing that makes it do anything.
- **Receive-only.** Nothing is transmitted. This device does not beacon.
- An offer is **only ever shown, never applied** — nothing retunes your radio or
  adds a channel on the say-so of a stranger's packet. Acting on one is manual.
- Beacons that arrive are listed on the [Beacons](#beacons) screen, under
  Live → Tools, with the sender, its message, whatever it offered, and how often
  it has repeated.
- Turning the setting off clears anything already collected.
- The setting travels with config export and import, under
  `module_config: mesh_beacon: listen_enabled`.

A beacon can only be heard if the sender retuned onto *your* channel, preset and
region — so an empty list usually just means nobody nearby is beaconing at you.

### Store and Forward

Some Meshtastic nodes run as a **Store and Forward router**: they keep a buffer
of the messages they hear and replay them on request, so a node that was off or
out of range can catch up. This device can act as a *client* of one — it never
stores or replays anything for anyone else.

- **Store&Fwd Client** on the Config screen (Modules → *Receive Replayed
  Messages* in web config) turns the client on. Off by default.
- **Request S&F Replay** — the row directly beneath it, and the *Request Replay
  Now* button in web config under Utilities → Diagnostics, next to Send NODEINFO
  Broadcast. A router **never replays history on its own**; it
  only answers a request, so this button is what actually makes the feature do
  something. It asks for the last four hours; the router trims that to whatever
  window and message count it is configured to return.
- The row names the router it will ask (`Request S&F Replay (a1b2)`), or reads
  `no router` when none has been heard yet. Routers announce themselves with a
  periodic heartbeat, so give it a few minutes after coming into range. The row
  is greyed out while the client is off.
- **Router Node ID** (web config → Modules → Store & Forward) pins the router to
  ask. Leave it blank and the device uses whichever router it hears a heartbeat
  from. Set it — as `!aabbccdd` — and that router is used always, with
  heartbeats from any other ignored when choosing who to ask. Clear it to go
  back to automatic.

  This matters more than it sounds: Meshtastic defaults `store_forward.heartbeat`
  to **off**, and a router that never beats can't be discovered. If the row reads
  `no router` while you know a router is right there, this is the fix. The
  setting is web config only, and travels with config export/import under
  `module_config: storeForward: router_id`.
- Replayed messages appear prefixed **`[SF]`** — in the channel view if they
  were originally broadcast, in a DM thread with the original sender if they
  were originally a DM.
- Requests are throttled to one every 30 seconds, because a single request can
  pull the router's whole return buffer down at once.

Things worth knowing about how routers behave, which are properties of the
router and not of this device:

- **Not on the default channel.** Routers refuse history requests on the public
  channel outright. Use a channel with your own key.
- **Only what it heard while running.** Anything sent before the router was
  configured, or while it was powered off, was never stored.
- A replay carries the *original author* in the packet, so replayed messages
  from a sender you have ignored stay hidden, and a replay does not disturb the
  node list — the original sender's **Last Heard** and signal readings describe
  when that node was really heard, not when the router repeated it.
- Replays are addressed to the client that asked for them, and this device only
  displays the ones addressed to it. Another node's catch-up burst on a shared
  channel is not absorbed as your own history.

### Backing up settings

**Export Config** on the Config screen writes `/camillia/config.yaml` to the SD
card — or, on the boards with no card slot, to the internal flash partition that
stands in for one — and web config has the same export plus an upload to restore
one. What the
file carries:

- Every setting in this guide, and the channel list including channel keys
- The node's **identity keypair**, so a restored device keeps the same PKI
  identity and peers holding its public key can still send it encrypted DMs
- **Not** the node ID. That is derived from the board's MAC address, so a backup
  restored onto *different* hardware comes up with the same name, channels and
  keys under a **new node ID** — other nodes will treat it as a new node

Because it contains the private key and your channel keys, **an exported config
is a secret, not just a settings file.** Treat it like a password.

A config can be restored three ways, all equivalent: the Import row on the
Config screen, an upload in web config, or the prompt during first-boot setup
when a `config.yaml` is already on the card. Each one reboots afterwards.

### Backing up messages

Web Config has a **Messages** section, directly above the Danger Zone, with an
**Export Messages (CSV)** button. It downloads every channel message and direct
message the device still holds — one row each, with the channel or peer,
timestamp, sender, packet ID and delivery state.

- The file goes to the browser you are using and **nothing is written to the
  device's SD card**. Chat is already persisted so it survives a reboot; this is
  for taking a copy off the device.
- Long messages come out as a single row. The device stores them one line per
  wrap, in reverse, so the export reassembles them the way the chat view does.
- History is bounded by what the device keeps in memory — the oldest messages
  have already been dropped from the ring by the time they age out. Export
  before using Clear Messages below it.
- Web Config only, and only once the device is on your WiFi: the button and the
  endpoint are both absent from the AP-mode Lite page.

The Config info panel also shows the **Newest** and **Oldest** node heard since
boot, with the node name and the time it was last heard.

### Auto-favorite nearby nodes

Also under **Node Management** in Web Config:

- **Auto-favorite nearby nodes** — off by default, opt-in
- **Auto-favorite radius** — in km or miles, following your Units setting
  (stored internally in meters, so switching units re-displays the same distance)

When enabled, any node reporting a position within the radius is favorited
automatically. This matters beyond sorting: favorites are never dropped when the
node table fills up, so this is a way to automatically protect your local nodes.
It is worth the most on the Cardputer, whose table holds 50 rather than 250 and
therefore fills five times sooner (see [Node management](#node-management)).

Two deliberate limits:

- It only ever **adds** favorites. A node moving out of range is never
  un-favorited — otherwise it could silently undo a favorite you set by hand.
  Remove those yourself from the node Actions menu.
- It needs a known position for **both** your node and theirs. With no GPS fix
  it falls back to your configured fixed position; nodes that have never sent a
  position are skipped.

The check runs every 30 seconds, so it also picks up nodes as *you* move.

### Firmware update check on boot

Once per boot, after WiFi comes up and settles, the device asks the release
server whether a newer build exists. If one does, a dialog shows the jump:

```
Firmware Update
3.4.1 -> 3.5.0
```

**Yes** reboots into OTA minimal mode and installs it (the same path as the
Config screen's Firmware Update action, including signature verification).
**No** dismisses it for the rest of this boot — it will not ask again until you
reboot. On keyboard builds, `Y`/Enter accepts and `N`/close declines.

Web Config → **Firmware Updates** → *Check for Updates on Boot* turns the check
off. It defaults to on. The check is a single plain-HTTP request and is skipped
entirely when WiFi is off or unreachable; a failed check is not retried until
the next boot. The update source is fixed in firmware and is not configurable.

Not available on the Cardputer, where OTA is disabled altogether.

### Chat style

Config has a **Chat Style** action. Selecting it opens a picker modal — navigate
with the usual up/down input and press Enter (or tap) to choose Classic,
Bubbles, or Outline; Backspace/Esc cancels. Choosing a *different* style reboots
to apply it; re-choosing the current style just closes without a reboot.

- **Classic** — one flat, colored text line per message. Your sent messages
  gain an `[ACK]` marker just after the timestamp once the message is
  acknowledged, and turn red on failure — in channel chat and Direct Messages
  alike, on every build. The color separates the two kinds of acknowledgement:
  **green** for an explicit routing ACK (always the case for a DM, which is
  addressed to one node), and the accent color for channel text, which goes
  out as a broadcast that usually settles for a relay confirming it carried
  the message on rather than a reply from any one recipient
- **Bubbles** — per-message rounded bubbles with a solid fill; your messages are
  right-aligned in the accent color (turning green on ACK, red on failure),
  other nodes' are left-aligned in a stable per-node color with a short-name tag
- **Outline** — the same bubbles drawn as colored outlines over a transparent
  fill: the per-node/accent color becomes the border (and the ACK/fail color for
  your sent messages), the sender tag is tinted to match, and the message text
  uses the theme's normal high-contrast color for readability

The style applies to both **channel chat and Direct Messages**. The Web Config
**Chat Style** dropdown offers the same three choices. All three styles are
available on every build, including the Cardputer.

### Emoji

Received emoji render as monochrome glyphs inline with the message text, on
every build. Coverage is broad — the firmware carries the full Noto Emoji set
(~1,500 glyphs) as a flash-resident font, drawn at the current text size. Two
notes on the monochrome approach:

- Emoji are **single-color**, matching the surrounding text — not full color.
- Multi-part sequences aren't combined: a skin-tone or variation selector is
  dropped to the base emoji, and a family/flag ZWJ sequence shows its component
  emoji side by side. Each piece still renders.

To **send** an emoji from the device, use the quick-emoji tray. On keyboard
builds (T-Deck, Pager, Cardputer), from the chat or DM screen — **not** while
composing a message — press **E**. A tray of common emoji opens: move the
selection and press Enter (or tap) to **send that emoji immediately** as a
one-glyph message, then the tray closes. On the channel view it goes to the
active channel; on the DM view it goes to the selected conversation. A close key
or a tap outside dismisses the tray without sending.

- **T-Deck / Pager / Cardputer** — press **E** on the chat/DM screen
- **Heltec (touch)** — while composing, tap the 😀 button next to Cancel / Send
  to insert emoji into the message. The tray has a **Close** button along its
  bottom edge: it fills all but a few pixels of the screen, so the tap-outside
  gesture the other builds rely on has almost nothing left to aim at

The web-config composer can also send any emoji your browser can type.

### Message Actions

Everything you can do to a single channel message lives in one menu. Open it two
ways:

- **Keyboard** — Enter to drop the cursor into the messages, scroll to one, then
  Enter again.
- **Touch** (T-Deck, Mesh Deck, Heltec) — tap and hold the message.

**Not on the Cardputer.** That board has no PSRAM and a small LVGL pool — the
same headroom that caps its emoji tray and leaves Discovery out — so Enter on a
highlighted message there opens the plain Node Actions menu instead. Reactions
are still reachable on Cardputer through the quick-emoji tray (**E**).

The menu is titled `Message Actions: <sender>` and holds:

- **A row of six reactions** — 👍 👎 ‼️ ❓ 😂 😢 — plus `...` for the full emoji
  tray. Picking one sends it immediately and closes the menu. Keys **1**–**6**
  fire the reactions, **M** opens the full tray.
- **Reply** (**R**) — opens compose quoting that message, the same thing Space
  does on a highlighted message.
- The six node actions for the **sender**: Traceroute (**T**), Send DM (**D**),
  Favorite (**F**), Request Info (**I**), Request Position (**P**) and Ignore
  (**G**).

Up/down walks the whole list including the reaction row; Esc closes and leaves
the chat cursor where it was.

A **reaction is not a new message** — it is attached to the message you picked
it on, and other clients (the web config chat tab, the Meshtastic app) show it
as a reaction on that message rather than as a new line. Reactions are only
offered on other people's messages, matching the web UI.

Note that reactions *received* from other nodes currently arrive as ordinary
one-glyph messages rather than being folded into the message they target.

The Nodes screen's Enter menu is unchanged and still titled **Node Actions** —
there is no message in that context to react to or reply to.

### Chat names

Config also has a **Chat Names** action, which opens a picker (same navigation as
Chat Style) to choose how sender names appear in channel chat:

- **Short** — the node's 4-character short name (e.g. `ABCD`)
- **Long** — the node's full advertised name when one is known, otherwise it
  falls back to the short name / hex id

Unlike Chat Style, this applies **without a reboot**: bubble views re-render
immediately, and new classic-chat lines use the chosen style going forward. The
Web Config **Chat Names** dropdown offers the same two choices.

### Brightness

The **Brightness** action opens a slider covering 10%–100% in 10% steps. The
panel follows the slider as you move it, so you are judging the real level
rather than a number:

- **J** steps right (brighter), **K** steps left (dimmer); the scroll and
  channel keys work too
- **Enter** saves and closes
- **Backspace/Esc** (or tapping outside) cancels and restores the level you
  opened with

The default matches whatever brightness the board has always used, so an
unconfigured device looks unchanged. Web Config offers the same setting as a
slider under **Display**, and the value is included in YAML export/import as
`display.brightness`.

### Web Config

Web Config serves a browser-based settings UI over Wi-Fi. **It is on by default
on a new device**, so a freshly flashed board comes up as the `camillia-mt`
access point and can be set up from a phone without touching the device screen.
Toggle it from the Config screen; the row shows the address once it is running.

There are two versions of the page:

- **Web Config Lite** — served in access-point mode. It carries the complete
  Config form (identity, Wi-Fi, LoRa, channels, MQTT, display, modules), but not
  the Utilities, Live, Chat, or Nodes tabs. Access-point mode leaves the device
  with very little memory once Wi-Fi is running, and those extras do not fit.
- **Full Web Config** — served once the device has joined your Wi-Fi network.
  Same Config form plus Utilities, the Live feed, Chat, and the Nodes map.

The Cardputer always serves Lite, on its own network or yours, because it has no
PSRAM to spare.

On the **Nodes** tab, the *Nodes Seen* dropdown lists favorited nodes first,
separated from the rest by a dashed divider, and a favorite's detail panel reads
*(favorite)* after its long name. Both come straight from the same ranking the
device's own Nodes screen uses. Lite has no Nodes tab, so this is everywhere the
full page is served — every board except the Cardputer.

The top-right corner of a node's detail panel has a **Favorite** /
**Unfavorite** button. It takes effect immediately — the same flag the device's
own Nodes → Actions → Favorite sets, saved to NVS on the spot — and the page
updates in place: the label flips, the *(favorite)* tag appears or disappears,
and the node moves to its new side of the divider without a reload. Favorited
nodes are never evicted when the table fills and survive *Clear Nodes (Keep
Favorites)*.

One caveat if **Auto-favorite nearby nodes** is on: it re-favorites any node
reporting a position inside the radius, so unfavoriting one that is still in
range only lasts until its next position packet.

**On the Cardputer, chat is paused while Web Config runs.** That board needs its
message memory to run Wi-Fi, so messages sent to it during a Web Config session
are not received or stored — they are lost, not queued. The device warns you
when Web Config starts, the Config row reads *chat PAUSED*, and the web page
shows a red banner. Turn Web Config off to resume messaging.

### Browser VNC

Every board except the Cardputer has an experimental **VNC Host** action on the
Config screen. It mirrors the live device UI into a browser — 480x222 on the
Pager, 320x240 elsewhere, or 240x320 if you flashed the vertical Heltec build —
and sends browser taps and keyboard input back through the same UI paths as the
physical controls. The viewer sizes itself to whichever panel it connects to.

- The action is available only while the device is connected to a Wi-Fi network
  as a station. Saved credentials or the device's own access point are not
  enough.
- Full Web Config on those boards always includes a **Remote** tab. Use its
  **Enable VNC host** checkbox to turn the service on or off, then use the viewer
  directly below it. The on-device **VNC Host** action controls the same saved
  setting.
- This uses a compact RGB565 WebSocket protocol based on wadamesh's browser
  mirror design. It is not an RFB/noVNC endpoint and does not accept standard
  desktop VNC clients.
- VNC and Web Config run together. The viewer connects only while **Remote** is
  selected and the checkbox is on. Direct access at
  `http://<device-ip>:8765/` remains available while enabled.
- The **Remote** tab and its endpoints are compiled into the `tdeck`,
  `tlora-pager-tft`, `heltec-v4`, `heltec-v4-vertical`, `mesh-deck` and `m9`
  environments. The Cardputer is the one board without them: the mirror needs a
  full-panel buffer in PSRAM, which that board does not have. The other
  requirement is a Wi-Fi station.
- The Heltec has no physical keyboard of its own. Browser keystrokes are injected
  into its key handling as though one were attached, so they work wherever the
  other boards' hardware keys do. Its on-screen keyboard is a separate path into
  the text box and is unaffected.
- On the Mesh Deck the mirror is the only way to see the screen remotely. Its
  panel has no MISO line, so it cannot be read back and the Web Config
  screenshot is unavailable there — but VNC copies the frames on their way to
  the panel rather than reading it, so the mirror is unaffected.
- The current experiment is plain HTTP with no VNC-specific authentication.
  Use it only on a trusted local network. One browser controls the device at a
  time.

### How many channels

Ten configurable channels on every board except the **Cardputer**, which has
eight. Each channel keeps its own message history, and on the Cardputer those
buffers sit in internal RAM rather than PSRAM — two more would cost memory that
board needs to boot its Wi-Fi access point.

This is a local setting, not a protocol one. A Meshtastic packet header carries a
channel *hash*, never a slot number, so a ten-channel device and a stock
eight-channel node talk to each other exactly as before as long as they share the
key for the channel in use. Slots 8 and 9 are ordinary channels to everyone else
on the mesh; they are only extra room on this device.

Two consequences worth knowing:

- The Meshtastic phone app and `meshtastic --export-config` only understand eight
  slots, so a config exported through stock tooling will not carry channels 8-9.
  Camillia's own export does.
- Downgrading to a build with eight channels keeps the first eight and drops the
  rest, rather than resetting everything.

### Per-channel hop limit

Every channel can carry its own hop budget, so a busy local channel can be held
to a hop or two while a wide-area one keeps the full reach. Unset — the default
for every channel — means "follow the device's Hop Limit", exactly as before.

- **On the device**: Config &rarr; Channels &rarr; pick a slot &rarr; **Hops**.
  Enter cycles Default &rarr; 0 &rarr; 1 &rarr; ... &rarr; 7 &rarr; Default, and
  Left/Right steps it either way. `Default (7)` shows the device value it is
  following, so you can see what unset actually means.
- **In Web Config**: a **Hops** dropdown on each channel row, next to Name, Key
  and Role. The first entry is `Default (7)`.
- **In `config.yaml`**: `hop_limit` under the channel, written only when the
  channel has an override.

What it affects: **traffic this node originates on that channel** — text,
position, telemetry, and the acks sent for traffic heard there. Direct messages
follow the channel their conversation resolves to, falling back to the device
default when that is unknown.

What it does not affect: **anything relayed**. A packet passing through keeps
the sender's budget, decremented by one, because the difference between where a
packet started and where it is now is how every node works out how far away the
sender is — rewriting it would corrupt that for everyone downstream.

A channel value is an override, not a cap: setting a channel to 5 on a device
whose default is 3 sends 5 on that channel. "This channel needs more reach" is
the case the setting exists for.

Two things worth knowing:

- **0 means direct neighbours only.** Nothing relays it. That is a legitimate
  setting for a channel shared with someone in the same room, and a silent dead
  end for anyone further away — a packet that runs out of hops is dropped by a
  relay, not rejected back to you.
- **This is a Camillia setting.** Meshtastic has no per-channel hop field
  (`ChannelSettings` carries name, key, role, uplink, downlink and module
  settings, and nothing else), so the phone app cannot see it and a config
  exported through stock tooling will not carry it. It needs no support from
  other nodes: the hop budget travels in every packet's header and relays honour
  whatever number they receive.

### Custom LoRa modem settings

The **Modem Preset** dropdown in Web Config's LoRa section has a **Custom** entry
below the nine Meshtastic presets. Pick it and four fields become live:

- **Bandwidth** — 62.5, 125, 250 or 500 kHz, plus 31.25 kHz on boards whose radio
  supports it. The LR1121 variant of the Pager cannot go below 62.5 kHz, so that
  build does not list 31.25.
- **Spreading Factor** — SF7 to SF12.
- **Coding Rate** — 4/5 to 4/8.
- **Frequency Slot** — `0` derives the frequency from your primary channel's
  name, exactly as a preset does. Any other value pins that slot number
  (1-based), which is how most local meshes on custom settings are described.
  The readout shows the resulting frequency and how many slots the region has at
  your bandwidth — narrow bandwidths have far more of them (62.5 kHz over the US
  band is 416 slots).

Every node you want to talk to must match on all four, plus region and channel.
Custom settings are not compatible with the presets: nothing running Long Fast
will hear a 62.5 kHz mesh, by design.

An unnamed primary channel is called `Custom` while these settings are active,
which is the name Meshtastic hashes for the frequency slot in the same
situation. Switching back to a preset restores the preset's channel name; a
channel you renamed yourself is never touched.

In YAML these live under `config.lora` as `usePreset`, `bandwidth`,
`spreadFactor`, `codingRate` and `channelNum`, using Meshtastic's convention
that a bandwidth of `31` means 31.25 kHz and `62` means 62.5 kHz. A
`meshtastic --export-config` dump from a node on custom settings imports
directly.

### Choosing a Wi-Fi network

The **Choose WiFi** action lists your configured network, an **AP** entry, then
up to **five remembered networks**, plus anything found by a scan — names only.

**Networks are remembered across reboots.** Joining a new one does not forget
the last: whichever network you were on moves into the remembered list, so you
can switch back from the picker without re-entering its password. The list holds
five besides the current network; when it is full the least recently added one
drops off. Press **D** on a row to forget it deliberately.

**The Cardputer keeps one network at a time.** It has no room for the remembered
list, so its picker holds the configured network and the AP entry only, and
joining a new network replaces the old one. A config imported from another board
keeps that file's active network and drops the rest.

The network you are actually connected to becomes the configured one — so a
reboot comes back to where you left off, not to whatever was configured first.
A network that fails to connect never displaces one that worked.

Web Config manages the same list on its own **WiFi** tab, between Config and
Utilities.
Each remembered network gets **Use** (switch to it, keeping the current one in
the list) and **Forget**, and there is a form to add one by name and password
without switching to it. Switching re-associates the radio, so a browser reading
the page over WiFi will lose the device until it joins the new network.

Saved Networks is not on the AP-mode Lite page — that mode serves the Config
pane only. Use the on-device picker when you are connected to the device's own
access point.

Every network travels with config export/import, under `wifi_networks:`. The one
currently in use is flagged `active: true`:

```yaml
wifi_networks:
  - ssid: HomeNet
    pass: homesecret
    active: true
  - ssid: Office
    pass: officepass
    active: false
```

The top-level `wifi_ssid` / `wifi_pass` keys still name the active network too,
so an older build reading the file comes up on the right one and simply ignores
the list. On import, the entry marked `active` becomes the configured network
and the rest are remembered. A file with no `wifi_networks:` section at all — an
older export, or a `meshtastic --export-config` dump — leaves the remembered
list alone rather than clearing it.

Note this means an exported config now carries **every** network password you
have saved, not just one. It was already a secret because of the channel keys
and identity key; this is one more reason to treat it like a password.

Selecting **AP** does not join a network: it brings up the device's own
`camillia-mt` access point, so Web Config stays reachable even when a network is
configured but out of range, or when you would rather connect to the device
directly. This choice persists across reboots, so a device left on **AP** keeps
hosting its own network until you pick a real one. While it is selected the
Wi-Fi row reads *AP mode*, and features that need an internet connection (time
sync, MQTT) stay offline.

![Config screen](screenshots/RiCa_screen_20260730_193743.png)

## Nodes screen

Nodes shows discovered nodes and detail fields, including map position details.

- Open from the main screen (N on keyboard builds, Nodes bottom-nav button on Heltec)
- **The node list is on the left, the selected node's details on the right.** The
  list gets a real share of the width — about 45% — because its rows carry the
  node's **long name**, ellipsized when it does not fit. A node with no long name
  falls back to its short name, and one that was evicted while the screen is open
  falls back to its id, so rows never shift out from under the selection
- Favorites keep their `*` marker and still sort to the top
- Navigate rows with Up and Down input (T-Deck uses J/K, since it has no Up/Down buttons)
- **Enter moves the keys into the details panel** so you can scroll through the
  fields with the same Up/Down (and Page Up/Down) you were using on the list. The
  focused panel is the one with the bright border. The close key steps back out
  to the list; from the list it closes the screen
- **A opens the actions menu** for the selected node, from either panel. (It used
  to be Enter, which now focuses the details.) While you are typing a filter,
  letters are filter text; press Enter to commit the filter and A works again on
  the narrowed list
- **Locate** in that menu shows where the node is — see [Locate](#locate)
- On the T-Lora Pager the wheel click toggles between the two panels, the same
  way it swaps panels on Config and DM
- Close with the device close key

**Cardputer keeps the original layout**: a narrow column of short names on the
right with the details taking the left. Four sections of aligned fields need
width for two columns and height for roughly seventeen rows, and 240x135 has
neither. Everything else on this screen — the filter, the selection, Enter for
details focus, A for actions — works there the same way.

It also lists **at most 50 nodes** rather than 250, because it is the one board
with no PSRAM to keep the table in. The list is otherwise identical; it just
turns over sooner on a busy mesh. See [Node management](#node-management).

**On Heltec**, drag either panel to scroll it; there is no focus to move because
there are no navigation keys. Tapping a node opens its actions menu, and the
USER button does the same thing for whichever node is selected.

### Node details

The detail panel is a set of aligned field tables under four headings —
**Identity**, **Link**, **Position**, **Telemetry** — rather than one wrapped
paragraph. Field names form a left column and values a right column, and the two
stay lined up no matter how long a value is: a value too wide for its column
ellipsizes on its own line instead of reflowing and dragging the rows out of
step.

- **Identity** — Name, Short, ID
- **Link** — Last heard, SNR, Hops, Channel
- **Position** — Lat, Lon, Alt
- **Telemetry** — Battery, Voltage, ChUtil, AirTx, Temp, Humidity, Pressure

Every field is always listed, and anything unknown reads `n/a`. That is
deliberate: a panel whose fields come and go with the selection jumps under the
cursor as you arrow down the list, and "this node has never reported a position"
is worth reading in its own right. Temperature and pressure follow the display
units setting (F/inHg or C/hPa).

The panel scrolls when the fields outrun it, and returns to the top each time
the selection changes. Press Enter on the list to move the navigation keys into
it. On the T-Lora Pager, whose screen is wide enough, the sections sit two
abreast.

This section describes every build except the Cardputer, which keeps the older
single-block detail text described above.

### Locate

The node actions menu has a **(L)ocate** row. With a healthy Wi-Fi connection,
it opens a live OpenStreetMap view at zoom `13` with the selected node centered
under the pin.

- **A node with no position greys the row out.** It stays visible in its usual
  place rather than disappearing, so the menu does not reshuffle from node to
  node. Pressing L or Enter on it does nothing
- Pan continuously in any direction. Longitude wraps at the antimeridian, so
  the view is not limited by a state, country, or previously downloaded area
- Zoom from `2` through `19`; the numeric value appears between the `+` and `-`
  controls. The center point stays fixed while changing zoom
- The GPS button recenters the selected node at the current zoom. Space resets
  both center and zoom to the initial zoom `13` view
- On keyboard builds, `H` or `C` recenters the selected node at the current zoom
- Locate uses cached `z/x/y` tiles first. Missing tiles remain gaps offline; with
  internet access they are downloaded, displayed, and saved for later
- Close it with the device close key, Enter, or by tapping outside it
- **Not available on the Cardputer or the Heltec V4.** Neither has the memory to
  decode a map — the Cardputer has no PSRAM at all, and the Heltec shares 2 MB
  with everything else. The row and live-map worker are compiled out entirely
  on both. See [MAPS.md](MAPS.md)

### Terrain line of sight (LOS)

A separate action on the same menu, and a different question: not *where* a node
is, but whether the ground between you and it is likely to block the path. It
samples the elevation profile along the great circle, adds earth curvature, and
reports **LINE OF SIGHT**, **MARGINAL (Fresnel)** or **NO LINE OF SIGHT** with a
cross-section showing where the tightest point is.

It needs an elevation proxy on your network — the firmware has no TLS client and
every elevation API is HTTPS-only. **See [LOS.md](LOS.md)** for the setup, which
is one small script and one Web Config field.

**See [MAPS.md](MAPS.md)** for network requirements, controls, and current
limitations. The pre-download controls still present in Web Config are legacy;
the live Locate viewport does not consume those files while the cache design is
being revisited.

### Filtering nodes

- Space starts the filter. Filter brackets `[ ]` appear in the header as a visual
  cue that filtering is on, even before you type anything
- Once the filter is armed, type to narrow the list; the text shows inside the
  brackets (`NODES [text] (count)`)
- Typing a letter on its own no longer starts the filter — only Space does
- Backspace edits the filter text; backspacing past the last character closes the
  filter and clears the brackets
- **Enter commits the filter.** The rows stay narrowed and the brackets stay in
  the header, but the keyboard goes back to the list: Up/Down move the cursor
  over the matching nodes and A opens the actions menu for the selected one
  instead of typing an `a`. A second Enter focuses the details panel, the same
  as it does with no filter
- With a committed filter, Backspace goes back to editing the text (it does not
  discard it), and Space does the same — so you can narrow, act, and re-narrow
  without leaving the screen. The hint line names whichever keys are live

![Node details screen](screenshots/RiCa_screen_20260730_194331.png)

## Direct Messages

Direct messaging:

- Open from the main screen (D on keyboard builds, DM bottom-nav button on Heltec)
- Pressing Enter on New DM opens node picker
- Enter on a conversation focuses the message panel (it stops there — Enter never
  opens compose)
- Space opens compose for the focused DM (Space replaced Enter for new messages)
- DM messages honor the Bubbles chat style: your messages are right-aligned in
  the accent/ack color, the other node's are left-aligned in their node color

![Node actions](screenshots/RiCa_screen_20260730_194240.png)
![Message view](screenshots/RiCa_screen_20260730_194306.png)

Delete behavior:
- Every keyboard build: D on the selected conversation opens a **Delete
  conversation?** dialog naming the peer and warning that the message history
  goes with it. Y or Enter confirms, N or the modal close key cancels, and the
  dialog swallows every other key while it is up. D does nothing on the New DM
  row, or while the message panel has focus
- Heltec touch build: long-press a conversation row for 3 seconds to raise the
  same dialog; tap (Y)es or (N)o, or tap outside it to cancel
- Deleting removes the conversation and its stored history, and cannot be
  undone. The list re-renders with the New DM row selected

## Help screen

Help explains shortcuts and transport symbols.

- Heltec: open from the bottom Help nav button
- While Help is open, D, C, N, and L jump directly into those screens

## Compose behavior

- Enter sends
- Space types a space — the Space shortcut only opens compose from the chat/DM
  screens, never while you are typing in the compose box
- Backspace deletes a character
- Cardputer: Esc closes compose
- T-Deck and T-Lora Pager: Backspace on empty compose closes
- Heltec: use on-screen controls to close compose

## Device controls

### LilyGo T-Deck (tdeck)

Primary usage is touch plus keyboard shortcuts.

- Use touch for channel chips and UI buttons
- Tap and hold a chat message to open Message Actions
- D, C, N, L open main modals; A opens Channel Actions
- H toggles channel selector
- Space opens compose or reply compose; Enter moves the cursor into the channel's messages,
  and Enter again opens Message Actions for the highlighted message
- Optional Vim-style helpers in navigation views: J maps to Up and K maps to Down
- Modal close key: Backspace (Esc also works)

### LilyGo T-Lora Pager TFT (tlora-pager-tft)

Primary usage is wheel plus keyboard.

- Wheel Up and Down on chat switches channels
- Wheel Click enters/exits row cursor mode
- In row cursor mode, Wheel Up and Down moves selected chat row
- Backspace exits row cursor mode
- Space opens compose or reply compose; Enter moves the cursor into the channel's messages,
  and Enter again opens Message Actions for the highlighted message
- H toggles channel selector
- Config modal: Wheel Click swaps focus between action list and info panel
- DM modal: Wheel Click swaps focus between conversation list and message list
- Modal close key: Backspace (Esc also works)

### M5Stack Cardputer + Cap LoRa/GPS (cardputer-cap)

Primary usage is keyboard.

- Channel switch: comma for previous, slash for next
- Navigation: semicolon and period act as Up and Down in list views
- Arrow keys map to the same directional actions
- H toggles channel selector
- Escape closes modals and exits chat focus mode
- Space (or Fn+Enter) opens compose; Enter confirms selected actions and moves the cursor into the channel's messages,
  and Enter again opens Message Actions for the highlighted message
- D is the DM delete trigger

### Heltec WiFi LoRa 32 V4 + TFT expansion (heltec-v4, heltec-v4-vertical)

Primary usage is touch.

- Bottom touch nav provides Home, DM, Nodes, Live, Config, and Help, in that
  order left to right
- Actions is not in the nav. On the chat screen it shares the strip under the
  conversation with New Message, one third and two thirds respectively; no other
  screen offers it, since there is no conversation there for it to act on
- **The USER button is the Enter key's stand-in, and it always does what a tap
  on that screen does**: compose on the chat screen, run the highlighted row on
  Config, open a node's actions on Nodes, run the highlighted entry in an
  actions menu, send the highlighted emoji in the tray, start the DM in the New
  DM picker, mute in Channel Actions
- Use on-screen touch lists and buttons inside each modal
- Tapping a Config row runs that action; tapping a node on the Nodes screen
  opens its node actions
- **Every popup that a keyboard build closes with Backspace has an X in its
  top-right corner here instead** — the same button, in the same place, on
  every one of them: Channels, Device Info, Action Result, Help, Release Notes,
  Bluetooth Keyboard, the emoji tray, Channel Actions, the Tools and Live
  Filter pickers, Locate, Line of Sight, the traceroute progress popup, the New
  DM node picker and the hidden system-stats screen. The full-screen tools —
  the SNR/RSSI and Channel Utilization charts, Beacons, Discovery and the MQTT
  monitor — put the same X at the right end of their header bar, with their own
  actions to its left. Where a popup also dismissed on a tap outside it, that
  still works
- **Every popup that stages a value before committing has a Cancel/Save row**,
  in the same place and the same shape — the Brightness, Battery Trim, Location
  Precision and Light Timeout sliders, Notification Sound (whose commit reads
  **Apply**), Channel Edit and Time & Date. On a keyboard build Enter commits
  and Backspace cancels; with neither key, the buttons are how you say which
  you meant. A tap outside is still a cancel everywhere
- The full-screen views — Config, DM, Nodes, Live — are closed by tapping their
  own button in the bottom nav, which is lit while you are on them. Their
  legends say so rather than naming a key

### Elecrow ThinkNode M9 (m9)

Primary usage is keyboard plus the d-pad and the dedicated function row.

- Dedicated buttons open Messages, Home, Live, Nodes and Map from anywhere
- Holding the d-pad centre sleeps the screen
- D-pad Up/Down navigates, Left/Right switches channels or hops columns —
  except in the New Message box, where the d-pad moves the text cursor
  (Left/Right by a character, Up/Down by a line)
- H toggles the channel selector
- Space opens compose or reply compose; Enter moves the cursor into the
  channel's messages, and Enter again opens Message Actions for the highlighted
  message
- Modal close key: Back
- The controller resolves Shift/Sym/Alt itself, so printable keys arrive already
  cased — there is no separate symbol tray on this board

## Close key summary

- Cardputer label: Esc
- T-Deck and T-Lora Pager label: Bksp
- M9 label: Bksp (the Back key)
- Heltec touch: use on-screen navigation and close controls

Esc is accepted as a close key in most keyboard flows.
