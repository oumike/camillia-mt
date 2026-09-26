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

### New modem presets

Meshtastic 2.7 and 2.8 added seven presets, and all of them are now here: **Lite
Fast** and **Lite Slow** (125 kHz), **Narrow Fast** and **Narrow Slow**
(62.5 kHz), **Tiny Fast** and **Tiny Slow** (15.6 kHz), and **Medium Turbo**
(500 kHz). They matter mostly for hearing a mesh that has already moved to one —
a preset you cannot select is a mesh you cannot join.

The Tiny pair is offered only on hardware that can actually produce 15.6 kHz,
which needs an SX1262 and a temperature-compensated oscillator. That rules out
the Attaky Mesh Deck, whose radio runs from a plain crystal, and the Elecrow
ThinkNode M9, whose LR1110 does not go that narrow. On those boards the options
are absent rather than present and broken; importing a config that names one
falls back to Long Fast.

### Which preset your local mesh is on

Setup asks for a region, and the region decides the starting modem preset.
Picking **US** on a fresh device starts you on **Long Turbo**, not Long Fast.
Meshtastic 2.8 made the same change, so Long Turbo is what a new US node — from
any firmware — now comes up on.

This matters more than it sounds. The preset sets the channel name, and the
channel name feeds both the channel hash and the frequency slot. Two nodes on
different presets are on **different frequencies at different modem settings**
and cannot hear each other at all — there is no partial reception to diagnose.

If the mesh near you predates 2.8 it is probably still on Long Fast. Change the
preset in Config or web config; the region stays as it is. Existing devices are
never moved by an upgrade — this default applies only to first-time setup.

Your **long name** is capped at 24 bytes here, matching what Meshtastic 2.8
stores and forwards. A longer name is clipped on other people's screens rather
than shown in full, so the cap is what you will actually be called.

## Main screen

The main screen is channel chat. Use it to read traffic, select reply targets, and start compose.

Typical flow on keyboard builds: pick a channel, press **Enter** to move the
cursor into that channel's messages, scroll to a message to select it, then press
**Space** to compose (a reply if a row is selected, otherwise a new message).

The bottom navigation bar is **icon-only** on every board that draws it. The
keyboard shortcuts that reach the same screens still work on the builds that have
a keyboard — they are listed below and on the Help screen — but they are not
printed beside the icons, and switching the bar off (Config → Nav Bar) still
brings back the key-hint strip under the chat.

**Every keyboard board can have the bar; what differs is the default.** On
`tdeck`, `tdeck-pro` and `mesh-deck` it is **on** out of the box, which is what
those boards have always shown. On `cardputer-cap`, `m9` and `tlora-pager-tft` it
is **off** out of the box — a fresh install there is the key-hint strip, down to
the pixel — and Config → **Nav Bar**, or **Bottom Nav Bar** in web config, turns
it on. It redraws immediately either way, with no reboot.

On the touch-only boards (`heltec-v4*`, `heltec-r8*`, `wio-tracker-l2`, and
`p4-amoled-*`) the bar is the only way off a screen, so there is no setting and
no key-hint strip to go back to.

A note for the three keyboard boards the bar is new to: it costs more than the strip it
replaces. The bar is 28 px against the strip's 14 — 12 on the Cardputer — and
the strip is where the M9's and the Cardputer's shortcuts are written down, so
turning the bar on trades that text for icons. The **Help** screen still lists
every key, which is the answer to "what was the key for Nodes again" once the
hints are gone. The Cardputer feels the height most: 28 px is a fifth of its
135 px panel.

The T-Display P4 uses the same navigation model at a 56 px height with 28 px
icons, scaled for its 568x1232 panel.

If your board has the browser **Remote** (M9 and T-Lora Pager do), the bar's
cells are clickable there even though the device itself has no touch panel — the
Remote forwards its clicks to the panel as a real pointer.

## Keyboard shortcuts by build

### Shared shortcuts (keyboard builds)

These apply to all keyboard builds, including `tdeck`, `tdeck-pro`,
`tlora-pager-tft`, `cardputer-cap`, `mesh-deck`, and `m9`.

- D opens Direct Messages
- C opens Config
- N opens Nodes
- L opens Live
- P opens Help — the key list, the transport symbols and what the nav cells do.
  P rather than H, which is Home on the boards with a dashboard and the channel
  selector on the ones without, and rather than `?`, which needs Shift on every
  one of these keyboards
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
- **Alt+Backspace is Back** on every build whose keyboard has an Alt of its own —
  T-Deck, T-Deck Pro, Mesh Deck and Cardputer — and does what the M9's dedicated
  **Back** button does. In compose it discards the draft and closes; everywhere
  else (filters, the channel and Wi-Fi text fields, closing a popup) it behaves
  as an ordinary Backspace, so the chord is only worth reaching for when you
  want to abandon a message. The Pager has no Alt — its modifier layer is
  Sym/Shift — so it has no equivalent.
- Live screen shortcuts: C clears the log and F filters the feed by traffic type
- **L opens Tools** — the grid holding Live, the SNR/RSSI and ChUtil charts,
  Beacons and Announce on every board, plus Discovery, Weather and MQTT where
  the board has them. S, U, D, B, W, M or A jumps straight to one
- Inside Discovery: W sweeps and P picks a preset to scan — both ask how long to
  listen first. C cancels a sweep or scan while one is running and clears the
  list when none is, and S saves a snapshot to SD. Inside Beacons: C clears.
  Inside MQTT Monitor: W starts a timed scan (asking how long, and whether to
  save while it runs), C restarts the count and S sends the top 5 to a channel

### LilyGo T-Deck (tdeck)

- **Keyboard Light** (Config, and in web config) sets the backlight's resting
  brightness: Low, Medium, High or Off. Off is what this board has always done —
  the keyboard stayed dark and lit only to flag an unread message. Notification
  blinks work at any level: they pulse away from whatever you pick and settle
  back on it
- H toggles the channel selector
- Alt+H returns directly to chat on keyboard-controller firmware with LilyGo's
  five-byte raw-matrix mode (2025-06-12 or newer). It closes things; it does not
  open the channel selector — H alone does that
- J/K map to Up/Down navigation in lists and chat row selection
- Trackball Up/Down follows the same Up/Down behavior as J/K
- Modal close key is Backspace (Esc is also accepted)
- Compose close behavior: Backspace on an empty compose closes the compose modal
- Trackball click hold for 2 seconds puts the screen to sleep, or raises the
  lock screen when one is enabled
- A trackball **click** is what brings it back — it is this board's only wake
  gesture, so rolling the ball, a key or a tap on the panel will not do it. See
  [Lock screen](#lock-screen)

### LilyGo T-Deck Pro (tdeck-pro)

- **Keyboard Light** (Config, and in web config) sets how bright the keyboard is
  when it is lit. **Alt+B** still turns it on and off; this is only how bright
  "on" is, and it defaults to full, which is what the board has always done
- Uses the same letter shortcuts as T-Deck: H opens the channel selector, J/K
  navigate, and D/C/N/L/A open the shared device surfaces.
- Shift and Symbol select the printed upper/symbol layers. Alt+E/F/S/X provide
  Up/Right/Left/Down, Alt+Q sends Esc, Alt+H returns to chat, and Alt+B toggles
  the keyboard backlight.
- **The backlight setting survives a reboot and a flash.** It is saved the moment
  you press Alt+B rather than at shutdown — this board has no clean shutdown, so
  anything not written then is not written at all — and applied again as soon as
  settings load at boot. A keyboard you turned dark comes back dark.
- Modal close and empty-compose behavior match T-Deck: Backspace closes, with
  Esc also accepted.
- There is no trackball. Use the keyboard navigation above or the touchscreen.
- E-paper updates are batched and rate-limited, so visual response is slower
  than on the TFT devices even though key input is handled immediately.
- The interface uses one fixed black-on-white **Camillia Paper** palette. Theme
  and sender-color controls are omitted from the device and Web Config.
- Channel and direct messages use transparent white-paper backgrounds with
  black outlines. Chat Type offers Default and Outline; filled Bubble is omitted
  from both device and Web Config.
- When the display sleeps, its retained frame shows Camillia, the node name,
  local time, and date. The time refreshes once per minute.
- The expanded channel list matches the channel selector's width. In compose,
  keyboard hints and the character count occupy separate footer lines.

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
- **BOOT is the screen button**: a tap puts the device away or raises the lock
  screen, a two-second hold unlocks. The wheel click and any keyboard key also
  unlock; rolling the wheel does not. See [Lock screen](#lock-screen)

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

- Dedicated hardware buttons below the screen jump straight to a surface from
  anywhere, closing whatever is open first: **Messages** (chat), **Home** (the
  home dashboard), the function key below Home (DMs), the key below Back
  (Tools), and **Map** (Discovery)
- **Home** opens the **home dashboard**: the lock screen's glance header — node
  name, clock, conditions, date, battery — over the two radio-health charts,
  channel utilisation on the left and SNR/RSSI on the right. The shortcut bar
  stays along the bottom, so the key hints are still there. It is a place to
  look at the node rather than at the mesh, which is why it is separate from
  chat
- **The device boots onto the dashboard.** Chat is built underneath it and is
  one press of Messages away
- **Messages** returns to the chat screen on the first channel. Pressed when you
  are already on the chat screen with nothing over it, it opens the **channel
  list** instead, so a second press is how you change channel. With the list
  already open it closes it and stops there. This is what Home used to do
- **Nodes has no dedicated button on this board.** Five destinations and four
  buttons, and the roster is the one that is looked up rather than lived in —
  N reaches it from the keyboard, as does Tools' own list
- **The keypad light cannot be held on, and there is no shortcut for it.** The
  LEDs belong to the keyboard's companion controller, not to this firmware. That
  controller lights them for about ten seconds after a keypress and then puts
  them out again, which is the whole of the behaviour — a keypad that seems to
  light inconsistently is that timeout, not a fault. The one register the host
  can write (`KB_REG_BACKLIGHT`) sets how bright that auto-light is; writing 255
  lights nothing on its own, so there is no setting this firmware could offer
  that would keep the keypad lit. Verified on hardware against the early
  ESP32-S2 controller. The T-Deck Pro's **Alt+B** has no equivalent here
- **Keyboard Light** (Config, and in web config) sets how bright that auto-light
  is: Low, Medium, High or Off. It is the one part of the keypad light this
  firmware can control. **Off** stops the controller lighting the keypad at all,
  and it stays off until the device is next power-cycled — so turning it back up
  takes effect after a power cycle, not immediately. Every other change applies
  at once. The level is written again at each boot, because the controller comes
  up at its own default rather than yours
- **Ctrl opens the emoji tray while composing**, with the glyph inserted at the
  cursor. The key did nothing at all before. The keyboard's own symbol layer is
  the controller's business and is unaffected

## Home dashboard

Every build with a display except the Cardputer opens a **home dashboard**: the
lock screen's glance header — node name, clock, conditions, date, battery — over
two charts of the radio's own health, channel utilisation and SNR/RSSI.

**The band under the header is a carousel of three pages.** The charts are the
first of them; the other two answer who is out there rather than how the radio
is doing, from the two ends of one ordering. (The lock screen runs the same
carousel with a messages page in front — see [Lock screen](#lock-screen).)

| Page | What it shows |
| --- | --- |
| `CHANNEL UTIL` / `SNR / RSSI` | The two charts, as before |
| `RECENTLY HEARD` | The nodes heard from most recently, newest first, by name with how long ago |
| `LONGEST SILENT` | The nodes heard from longest ago, oldest first |

A small `<` and `>` sit at the edges of the band to mark that it turns. They are
a hint, not buttons — the gesture is what drives it.

Swipe **right** to go forward and **left** to go back; it wraps both ways, so
you are never more than a swipe or two from any page. Off the touch panel it
turns on **left/right or up/down**, whichever the board has: the T-Lora Pager's
rotary wheel, the M9's and Mesh Deck's d-pads, the T-Deck's trackball. On any
build with a keyboard, **j and k** turn it too — k forward and j back, matching
the way j and k move everywhere else on this firmware (j is up, not vim's j is
down). Nothing on this band scrolls, so both axes are free to mean "turn the
page".

**On the T-Deck Pro, tap the band to turn it forward.** Swiping an e-paper panel
is awkward — the touch controller is sampling against a refresh measured in
hundreds of milliseconds, so the drag has to be slow and deliberate to register,
where a tap is one unambiguous event. A tap anywhere on the band moves one page
forward and wraps, and the keyboard's left/right still goes back, so nothing is
out of reach. The other boards keep tap free; swiping there is no trouble. The page slides
rather than cutting, so you can see which way the carousel went; the T-Deck Pro
swaps instantly instead, because every animated frame there is a full e-paper
refresh.

**On a wide enough panel the two node lists share one page**, side by side, the
same way the two charts already do — so the carousel is two pages rather than
three and twice as many nodes are on screen at once. That is the same
measurement the charts use (a panel 300 px or wider), so the T-Deck, Mesh Deck,
M9, Wio and the Heltec boards in landscape pair them up, while the T-Deck Pro
and anything in portrait keeps a page each.

**Each row is a name and an age.** The name is the node's **long name** when it
has sent one, falling back to its short name and then to its `!hex` id — the
same order the Discovery and Beacons screens use. The age sits against the right
edge and reads `45s`, `12m`, `3h`, `2d`. A name too long for the card is
ellipsized rather than clipped, and the age is never the part that gets squeezed
out: it is what the page is sorted by, so it always stays on screen.

**How many rows a node page shows depends on the panel.** The band is whatever
height is left under the header, and the pages fill it — a 480x222 Pager fits
several more rows than a portrait T-Deck Pro.

Only nodes actually **heard since boot** are listed. Node records restored from
flash at startup have no last-heard time until a packet arrives, so they are
left out rather than filling `LONGEST SILENT` with nodes the device has not
actually met this session. A page with nothing to show says **Nothing heard
yet** rather than sitting blank.

Which page you left it on is remembered until the device restarts, so pressing
Home returns you to the one you were reading rather than always to the charts.

**The dashboard follows the UI theme.** Background, header type and the chart
cards all come from the theme you picked, so it looks like the rest of the
device rather than a black slab in the middle of a light theme. The node name
takes the theme's accent colour; everything else is the theme's normal text ink.
The lock screen is deliberately not themed — it is what the device looks like
with the UI put away, and stays white-on-black (black-on-white on the T-Deck
Pro's e-paper, which has no colour to give either screen).

**The footer becomes a notification ticker.** This follows the footer rather
than the board: wherever the footer is a key-hint strip — which is every board
with the nav bar switched off, and on the M9 and T-Lora Pager that is the
default — the hints are replaced while the dashboard is up by the messages that
have arrived: newest first, as `14:32 #general Alice: body`, up to three of
them, scrolling if they do not fit. The hints name keys that belong to the chat
screen, so they were only ever noise here.

With nothing unread it reads **No new messages** and does not scroll. Leaving
the dashboard puts the key hints straight back.

Boards whose footer is the nav bar have no text row to use, so they keep the bar
as it is — including the M9 and the Pager once the bar is switched on there,
which trades the ticker away for it. The T-Deck Pro keeps the plain hidden hints,
because a scrolling label is an animation and every frame of one is a full
e-paper refresh.

GPS and Wi-Fi icons appear in the dashboard's top band **only on the touch-only
boards** (Heltec, Wio), where the bar along the bottom is icon buttons with no
room for status and the chat screen's own copy is covered by the dashboard.
Everywhere else that bar is already showing them a few centimetres below, so the
dashboard leaves them out rather than putting the same two icons on screen
twice. The key-hint strip carries the icons too, so the dashboard follows
whichever footer is actually there rather than asking which board it is on. The
shortcut bar or nav bar stays along the bottom; on keyboard builds the chat
screen's key hints are hidden while it is up, because they describe a screen you
are not looking at. **The device boots onto it.**

Where the charts sit side by side on a landscape panel, they stack on a portrait
one, and each card puts its heading and current reading on one line there to
leave the chart some height.

How you reach it depends on the board:

- **M9** — the **Home** button. **Messages** is the chat screen, and a second
  press of Messages opens the channel list
- **Touch-only boards (Heltec, Wio)** — the nav bar's **Home** cell, with a
  **Chats** cell beside it for the messages. Whichever of the two you are on is
  the lit cell
- **Keyboard boards** — **H** on the chat screen, or the **Alt+H / Sym+H**
  chord. **C** is the chat screen (a second C opens the channel list, where the
  board has a dropdown rather than an anchored list), and **F** is Config, which
  used to be C. D, N and L are unchanged, and **P** is Help
- The **Alt+C chord** goes to chat *without* opening the channel list. That is
  deliberate: the chord is the reflexive "get me out of here" gesture, and
  binding it to the channel list is what once had the Mesh Deck dropping
  keystrokes. A plain C, typed on purpose, still opens the list
- **Every nav-bar destination has an Alt chord**, on the keyboards that have an
  Alt of their own: Alt+H (Home), Alt+C (Chat), Alt+F (Config), Alt+D, Alt+N,
  Alt+L (Tools) and Alt+P (Help). A chord reaches its destination from anywhere
  — out of a filter, a picker or a text field, where the plain letter would be
  typed instead. Plain P still means what it means on the screen you are on,
  such as (P)reset inside Discovery, because the keyboard resolves Alt before
  the letter is read
- Which boards: **T-Deck**, **Mesh Deck** and **Cardputer** share one chord
  table; the **T-Deck Pro** has its own, identical except that Config has no
  chord there (Alt+F is next-channel on that keyboard — plain F still opens
  Config, as does the nav bar). On the **Cardputer** there is no dashboard, so
  Alt+H means "close everything and get back to chat", and Config is Alt+C
  rather than Alt+F. The **Pager** has no Alt at all, and the **M9**'s keyboard
  controller resolves Alt before the firmware sees it — on those two the plain
  letters on the chat screen are the way in, plus the M9's dedicated buttons
- **T-Deck Pro exception**: Config has no Alt chord there. Alt+F is already
  next-channel on that keyboard, and the pair of channel chords straddle Alt+D
  on the physical home row. Plain F still opens Config, as does the nav bar
- **Cardputer** has no dashboard. Its 240x135 panel does not build the glance
  header this is made of — so its footer is never the ticker, and the nav bar
  simply replaces the key hints there full-time

On the T-Deck Pro the charts update once a minute rather than on every packet.
A redraw there is an e-paper refresh, and this screen opens itself.

## Unread indicators

Unread messages are marked in two places, and which one you get depends on
whether the bottom nav bar is showing. On every board with a keyboard the bar is
a setting (Config &rarr; **Nav Bar**), and the alert follows the setting, not the
board — turn the bar on and the mark moves onto it:

- **With the nav bar** — the **Chats** cell blinks for unread channel traffic
  and the **DM** cell blinks for unread private messages, on the same half-second
  phase, in the same amber. The alert and the way to answer it are the same
  control. **The whole cell flashes**, not just the glyph inside it: on a bar of
  icon buttons a recoloured 14 px symbol is easy to miss, and a button that
  changes colour is not
- **Without it** — two glyphs sit at the right-hand end of the key-hint footer,
  beside the GPS and Wi-Fi readouts: a **bell** for channels and an **envelope**
  for DMs. Different shapes, because they blink together and sit a few pixels
  apart

With the bar up, the status cluster at its right-hand end is now just GPS and
Wi-Fi — the envelope and bell are gone from it, since the cells say the same
thing — and the buttons grow into the width they were holding.

**On the T-Deck Pro the cell is held, not blinked**, and it is marked with a
heavier border rather than a fill. Every flip is a full e-paper refresh: at twice
a second that is visibly slow and a real cost in battery, so the cell stays
marked until the conversation is opened. The border rather than a fill because a
filled cell on a one-bit panel is a black box with the glyph buried in it.

Either way the mark clears as soon as you open the conversation, and a muted
channel raises nothing.
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

### Attaky Mesh Deck (mesh-deck)

- **The R button is the screen key** — the rightmost of the shoulder pair along
  the top edge. It takes the same tap/hold rule as every other board: tap to put
  the device away or raise the lock screen, hold two seconds to unlock. The BOOT
  button does the same job on a pin that can also wake the CPU out of a
  light-sleep nap. See [Lock screen](#lock-screen)
- **The hardware Power button is deliberately left alone.** A long press there
  cuts power below firmware at roughly the same two seconds that unlocks, so a
  hold on that pin could only ever end as a shutdown
- Alt+H returns directly to chat from Config, filters, nested pickers, and the
  other device surfaces. It closes things; it does not open the channel
  selector — H alone does that, and leaving the channel list open on this board
  slows the keyboard scan (see the note in `readKey()`, src/keyboard.cpp).
- Alt is read as a held modifier from the left keyboard expander at row 4,
  column 2; an ordinary H remains available for the channel selector.

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

- **Live is reached through Tools**, which is its own destination now: **L** on
  keyboard builds, the wrench cell on the nav bar, or the **Alt+L** chord. Live
  is the first row of the Tools grid
- Scroll with Up and Down input
- Press C to clear the log
- Press F for the traffic filter (on Heltec, the filter button in the Live
  header) — see [Traffic filter](#traffic-filter) below
- Backing out of Live, or out of any other tool, returns to the chat screen.
  Tools closes itself on the way into whatever you picked, so there is no
  half-way house to come back to

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
- **C stops a run early.** While a sweep or a preset scan is going, C cancels it
  and keeps whatever it has already heard — a scan also retunes the radio back
  to your own preset on the way out. With nothing running, C clears the list as
  before. Cancelling does not hand back a fresh sweep straight away: the
  broadcast has already gone out, so the usual cooldown still applies
- **Both Sweep and a preset scan open Sweep/Scan Settings first**, which asks
  how long to listen — 30 sec, 1, 2,
  5, 10, 15 or 30 min, or 1, 2 or 6 hours. **A long preset scan is not the same
  as a long sweep:** a scan parks the radio on the foreign preset for the whole
  window, so the node is deaf to its own mesh until it ends. Six hours of that
  is six hours of messages on your own channels that never arrive, and nothing
  afterwards will tell you what you missed. A sweep costs only the wait. A sweep opens on 1 min and a scan on 5, which is what they
  used to be fixed at, and each remembers what you last chose for it. The window
  cannot be changed once a run has started: it is what decides when the results
  are called final. A sweep that cannot run right now says so before it asks
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
  nodes within 3 hops to answer, and replies are counted for 60 s. Never
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
- **Save while discovering** (Sweep/Scan Settings, under the duration; off by
  default, and Space toggles it on keyboard boards) saves the run as it goes
  instead of waiting for S. The run gets one file of its own, named the same
  way, rewritten as results change (at most every 5 seconds) and once more when
  the run ends — finished, cancelled or cut short — so what was heard is on the
  card even if the device loses power mid-run. While it runs the status line
  names the file ("Sweeping... 1m/5m - saving to discovery-….json"), and it
  ends in "- saved" when it closes. The choice lasts until reboot. Only offered on
  builds with storage.
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
screen built around a 60-second sweep was empty nearly every time you looked at
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

### Weather

Current conditions for wherever the node is: temperature and what it feels
like, the sky, humidity, and wind with its gusts and direction. Current
conditions only — no forecast.

- Open from Live → Tools → Weather (W). The title names the place the reading
  is for — "Weather - Golden, CO" — when the proxy can resolve one
- Units follow **Config → Units**; there is nothing to set separately
- The reading is fetched when you open the screen and then cached for ten
  minutes, so reopening it is free. **R** refetches inside that window — after a
  failure, or once you have moved
- **Works out of the box.** It ships pointing at a public proxy
  (`http://weather.camillia.sumat.org`), because this firmware has no TLS client
  and weather APIs are HTTPS-only, so something has to bridge the two. Point
  Web Config → **Weather Server** at your own instead, or clear the field to
  turn the screen off entirely — see [docs/WEATHER.md](WEATHER.md), which
  includes the container and the contract
- The position is **rounded to about a kilometre before it leaves the device**,
  and the screen says so. Weather is a town-resolution question. This is
  independent of Share Location, which governs what goes out over the mesh; to
  send nothing at all, leave the Weather Server field empty
- A reading that fails to refresh is kept and shown with its age rather than
  discarded, so a dropped Wi-Fi connection leaves you the last thing that worked
- **The lock screen and home dashboard show the reading beside the clock**, sky
  on the node name's row and temperature on the clock's, with a **vertical rule
  down the middle** separating the two columns. With no reading — never fetched,
  no server set, or the cached one too old to believe — that half is empty, so
  the rule disappears and the node name and clock **centre on the panel**
  instead of staying pinned to the left with dead space beside them. Both move
  back when a reading lands
- Every failure names what to change: no server set, no position, no Wi-Fi, the
  proxy unreachable, or the proxy answering with something that is not weather
- Available on every board. The Cardputer has no Utilities tab in its lite web
  config, so its server address is set through the microSD `config.yaml`
  (`network: weatherServer:`) — and the shipped default means it works without
  setting anything. Its 240x135 panel gets a compact layout: a smaller headline
  and three detail lines instead of four

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
- Nothing persists unless you ask it to. Counting starts when the screen opens,
  the table is freed when it closes, and reopening starts from zero. Press C
  (Heltec: the Reset button) to start over without leaving the screen
- **Press W (Heltec: the Scan button) for a timed scan that can save as it
  runs.** See [Scanning and saving](#scanning-and-saving) below
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

#### Scanning and saving

A scan is a count over a window you choose, set up the same way as a Discovery
sweep.

- Press **W** (on Heltec, the **Scan** button in the header). **MQTT Scan
  Settings** asks how long to listen — 1 minute to 6 hours, opening on whatever
  you used last — and, on boards with an SD card or internal file storage, offers
  **Save while scanning**. Space toggles the box on keyboard builds; Enter starts
- Starting a scan restarts the count, so the result covers exactly that window.
  The status line counts towards it, such as `12 chans  400 msgs  5m/1h`, with
  `saving` added while a file is being written
- With Save while scanning ticked, the scan writes
  `/camillia/mqtt-<date>-<time>.json` when it starts and rewrites it at most every
  five seconds while messages keep arriving, so a scan cut short by a reboot or a
  flat battery still leaves what it had. The file holds the root and filter, the
  window and how much of it had run, the totals (including off-list messages) and
  every channel with its count, in the order the screen shows them
- When the window closes the scan ends with one last write and says so on the
  status line — `Scan done - saved mqtt-20260924-221500.json`. Counting carries on
  afterwards as normal
- Closing the screen, pressing C, or starting another scan ends the current one
  early, with the same final write first. The file's `"done"` field is true only
  for a scan that was ended this way, never for one interrupted by power loss
- Save while scanning is off at boot and remembered until the next reboot

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

### Announce

Introduces this node to the mesh: a NODEINFO broadcast (with position, when
location sharing is on) and a telemetry packet, from one press. This replaces
the *Send NODEINFO Broadcast* and *Send Telemetry Now* rows that used to sit at
the bottom of the Config screen — they were always used together, so they are
one action now.

- Open from Live → Tools → Announce (**A**). It is the last cell on the grid
- There is nothing to look at: it transmits and reports back with a popup,
  `NODEINFO + telemetry queued.` Any key (or a tap) dismisses it
- Telemetry is sent whether or not periodic telemetry is enabled, matching what
  the old *Send Telemetry Now* row did
- **One announce per 30 seconds.** A press inside that window is refused and says
  how long is left — `Just announced. Try again in 12s.` Both packets are
  broadcasts, and holding the key should not be a way to flood the mesh
- The clock starts on the press, not on the transmission. If the radio is not
  ready the packets stay queued and go out when it is, and pressing again in the
  meantime will not stack up more of them

![Live screen](screenshots/RiCa_screen_20260730_195834.png)

## Config screen

Config includes Web Config controls, export and import, the theme picker, and
reset actions. Announcing to the mesh has moved to Live → Tools → Announce.

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

### Device role

**Role**, under Device in web config and asked once during onboarding, is how
this node tells the rest of the mesh it means to be treated. It travels in
NodeInfo, so other nodes and MQTT-fed maps see it, and it is exported as
`config: device: role:`.

Four roles are offered, using the same names and enum values as stock
Meshtastic:

- **CLIENT** (default) — an ordinary node. Sends its own traffic and relays
  everyone else's.
- **CLIENT_MUTE** — never relays other people's packets. For a node in a spot
  where relaying adds nothing but airtime, or where battery matters more than
  the mesh does.
- **TRACKER** — a node whose job is reporting where it is. It still relays like
  a CLIENT; what makes it a tracker is that its position is the point of it.
- **CLIENT_HIDDEN** — only speaks when spoken to. One difference from stock
  Meshtastic worth knowing: theirs still relays, restricted to known meshes,
  where here CLIENT_HIDDEN does not relay at all.

The infrastructure roles — ROUTER, ROUTER_LATE and REPEATER — are deliberately
not offered. They change how the whole mesh routes traffic around a node, and
they are not something a handheld with a screen should claim to be. A config
imported with one of them is coerced to CLIENT rather than honoured.

#### What TRACKER does here

Upstream, TRACKER means two things: position packets are prioritised in the
node's own transmit queue, and — with `power.is_power_saving` on — the device
wakes, sends a position, and sleeps until the next one.

This firmware has neither mechanism. There is no priority transmit queue — a
packet is transmitted at the point it is built — and nothing puts the device to
sleep between position sends. (GPS Duty Cycle, under Position, parks the *GPS
receiver* between samples; it is not upstream's whole-device sleep and is not
tied to the role.) So setting TRACKER here **advertises the role and changes
nothing about power draw** — a tracker costs the same battery as a client. What
makes it behave like a tracker is the two settings under Position, which you set
yourself:

- **Share Location** must be on. A TRACKER with it off transmits no position at
  all, which is the whole of the role; Device Info shows the role as
  `TRACKER (not sharing location)` when that is the case.
- **GPS Broadcast Interval** decides how often the position actually goes out.
  It defaults to 1800 s, which is a client's interval, not a tracker's —
  Meshtastic suggests 60 s for a tracker. Nothing changes it for you: it is your
  airtime to spend, and a firmware that quietly shortened a broadcast interval
  on a shared channel would be a worse neighbour than one that asked.

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

There is one exception, and it only ever makes the position coarser. On a
channel **anyone can decrypt** — no key at all, or one of the published default
keys every stock device ships with — the transmitted precision is capped at
about 700 m however this setting is set. An exact position on such a channel is
not shared with a group; it is broadcast to anyone in range with any radio, and
no key is protecting it. Meshtastic 2.8 applies the same ceiling and no setting
there can raise it either. Set a real key on the channel and your chosen
precision applies in full.

The same ceiling applies to the MQTT map report, whatever the channel key is,
because the map itself is public.

### Why sharing is still on by default

Meshtastic 2.8 changed its defaults so that a new or upgraded node shares no
position and broadcasts no telemetry until you turn them on. Camillia has not
followed, and that is a decision rather than an oversight.

The problem those defaults exist to solve is a node beaconing an exact position
on a channel anyone can decrypt. Camillia closes that at the point of
transmission instead: the position is capped at about 700 m on any such channel,
and on the public map report, however the precision is set. With the leak shut,
switching sharing off by default would cost you the feature without buying much
back.

So, concretely, against a stock 2.8 node: Camillia still shares position (at
~700 m on public channels, at your chosen precision on keyed ones), still sends
device telemetry — battery and channel utilisation, never location — and still
puts a coarsened location in its map report where 2.8 sends anonymous presence
only. Each of those is a switch you can turn off, under Position and Telemetry
in web config.

### How often your position goes out

**Position Broadcast Interval** sets the cadence, but a node that has not moved
sends less often than that on purpose. From 2.8 onward receivers discard a
repeat position that lands within about 90 m of the last one they have from you,
for up to five hours — the packet is transmitted, received, and thrown away. So
while your position stays inside that radius the interval stretches to six
hours, matching the floor upstream uses for a stationary node.

Moving out of that radius restores the normal interval immediately; the first
fix somewhere new is sent on the spot rather than waiting. Pressing **Announce**
always transmits regardless. If you have set an interval longer than six hours,
that is left alone — this only slows a stationary node down, never speeds one up.

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

### Language

**Language: <name>** picks the language of the on-device interface: English
(the default), Español, Français, Română or Italiano. Activating it opens a
dropdown of the languages, each written in its own language, with Cancel and
Save. On a keyboard build, Up/Down (or the wheel/trackball) changes the choice,
Enter saves and Backspace/Esc cancels.

**Saving a different language reboots the device**, like Theme and Orientation:
the status line says `<language> - rebooting...` and the device comes back in it.
Saving the language already in use just closes the dropdown. Anything not yet
translated shows in English. Web Config's **Display** section has the same
**Language** setting (the web page itself stays in English), and an exported
config carries it as `language:` under `display:`.

**Accent suggestions.** With a language other than English selected, typing a
letter that has accented forms in that language, such as `e` in French or `s` in
Romanian, pops a small row of them above the field: the plain letter first,
then the variants. Tap one to swap the letter you just typed for it. On a
keyboard, **Tab**, the wheel/trackball or the arrow keys step through the row
and swap the letter in place as they go. Enter keeps the choice, and any other
key simply carries on typing. Spanish also offers `¿` after `?` and `¡` after
`!`. Suggestions appear in the message composer and the node name fields.

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

### Clock format

**Clock Format** on the Config screen (directly under Time and Date, and in web
config in the same **Time and Date** block) chooses how a time is written:

- **24-hour** (default) — `14:32`.
- **12-hour** — `2:32 PM`. The hour is not zero-padded, the way a clock is
  normally written; midnight reads `12:05 AM` and noon `12:00 PM`.

It applies everywhere the device shows a time to a person: the chat header
clock, message timestamps in both classic and bubble styles, the live feed, the
sleep and lock screens, the Device Info "Newest/Oldest heard" lines, and the
detail panel for an archived node.

Machine-readable output deliberately stays 24-hour, because it is read by tools
rather than people: exported config and message CSV timestamps, discovery JSON,
and export filenames.

Two things it does not change:

- **Setting the clock by hand** — the Hour field on the Time and Date modal, and
  the web form's `Time (24h)` box, stay 24-hour entry in both modes.
- **Messages already received.** A message's timestamp is part of the line as
  it was stored, written in whatever format was in force when it arrived, and
  the bubble styles read that same prefix so classic and bubbles agree. A
  transcript spanning a change therefore carries both formats; everything
  arriving after the change uses the new one.

It applies immediately with no reboot, and travels with config export/import as
`display: clockFormat:` (`H24` or `H12`).

### Theme

T-Deck Pro is the exception to this section: its e-paper UI always uses the
black-on-white Camillia Paper palette, so it does not show the Theme action or
the Web Config theme picker.

The **Theme** action opens a picker rather than cycling. Each theme/mode preset
gets a row with its name and a three-swatch preview — background, panel, and
accent color — so you can see what you're choosing. Navigate with the usual
up/down input and press Enter (or tap) to apply. Backspace/Esc (or tapping
outside) cancels. Web Config shows the same themes as a grid of swatch cards,
and previews the selected one live before you save.

**Choosing a theme reboots the device**, the same way Orientation and Chat Style
do — the status line says `Theme: <name> - rebooting...` and the device comes
back a few seconds later wearing it. Every screen is built from the palette, so
coming up in it is what guarantees the whole interface matches rather than most
of it. Re-choosing the theme already in use just closes the picker. Saving a
theme from Web Config reboots for the same reason, as every Web Config save
does.

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

Firmware version, node id and identity, the radio's current settings, how many
packets this node has relayed, the newest and oldest nodes heard — and a
**storage** line saying whether the SD card is actually there.

On a board with a card slot that line reads `SD: SDHC 29.7 GB @4000 kHz` when a
card is mounted. The clock rate is the one the card answered at: a card that
mounts only at 400 kHz is working but marginal, which is worth knowing before it
starts failing writes. With no card it reads `SD: no card`, followed by a second
line saying what the probe did — `SD: all rates tried, retry 12s, 5 fails`.

"All rates tried" means the card did not answer at 4 MHz, 1 MHz or 400 kHz,
which points at an empty slot or a dead card. "One rate tried" is the damped
retry — after the first full sweep fails, later attempts try a single rate each
time rather than stalling the device for three seconds — so it does not by
itself mean the card is bad. The retry countdown is the backoff, which stretches
from 2 seconds to a minute as failures accumulate; **Export** or **Import** in
Config clears it and forces a full re-probe, which is what to press after
inserting a card.

Boards with no slot keep the same files in internal flash and show that instead:
`Storage: internal flash 1.5 MB`.

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

### Lock screen

Eleven of the thirteen builds can show a lock screen before putting the panel fully
to sleep: `tdeck`, `tlora-pager-tft`, `heltec-v4`, `heltec-v4-vertical`,
`mesh-deck`, `m9`, `wio-tracker-l2`, `heltec-r8`, `heltec-r8-vertical` and
`p4-amoled-sx1262` and `p4-amoled-lr2021`. The two that cannot are `cardputer-cap` and `tdeck-pro`, for different reasons given
under [Locking and unlocking, build by build](#locking-and-unlocking-build-by-build). It uses a black background with the time and channel in
blue, node names in green, and message text in white. The current date, battery
reading and newest unread message previews remain visible while it is active.

The top band carries **GPS and Wi-Fi icons** beside the battery, reading the
same way they do on the chat screen: GPS green with a fix and showing its
satellite count, red without one; Wi-Fi green when connected, struck through
when it is not, and an upload arrow while the device is serving its own access
point. They update as the state changes rather than on the clock's minute tick,
so a device picking up a fix or coming up as an AP shows it immediately. On the
T-Deck Pro they are black like everything else on that panel, and they change
with the minute — each repaint there is a full e-paper refresh.

The lock screen always shows them, unlike the home dashboard: it covers the
whole panel, so there is no bar underneath still reporting the same thing.

**The band under the header is a carousel too, and it turns itself.** It carries
the same faces the home dashboard's does, with one more in front of them:

| Page | What it shows |
| --- | --- |
| Messages | The newest unread previews — what this band has always shown. Skipped when nothing is unread |
| `CHANNEL UTIL` / `SNR / RSSI` | The two radio-health charts |
| `RECENTLY HEARD` / `LONGEST SILENT` | The node lists, paired on a wide panel and a page each on a narrow one |

It moves on **every 30 seconds** with no input, forward only, and wraps — so a
lock screen left up cycles through everything it knows in about two minutes. It
draws **no `<` and `>` arrows**, unlike the dashboard's, because there is no
gesture for them to mark: every press on this screen dismisses it, so the band
turns on the clock or not at all.

**The messages page is only there when there is something on it.** With nothing
unread it is skipped entirely: the band opens on the charts and turns between
the faces that have something to show, rather than spending thirty seconds on a
blank rectangle. A message arriving while the screen is locked brings the page
back into the rotation at the next turn.

When there *is* something unread, that is the page it opens on — it is what this
band has always been, and what someone glancing at a locked device is most
likely to be looking for. The lock screen is arrived at rather than returned to,
so unlike the dashboard it does not resume wherever the timer had got to last
time.

The message previews keep the exact layout and the full line budget they had
before — the band is measured to the bottom edge so the last row still fits.
Very short panels skip the carousel altogether and keep the plain message list.

The T-Deck Pro has no carousel here. Its sleep screen is a static e-paper image
held without power, so a band that redrew itself every 30 seconds would be
spending battery on refreshes nobody asked for.

#### Locking: what puts it up

Locking is the middle of three display states, and a separate setting governs
each step into the next:

| From | What moves it on | To |
| --- | --- | --- |
| UI | **Screen Timeout**, or the board's screen-off gesture | Lock screen |
| Lock screen | **Lock Screen Off**, the dwell | Dark panel |
| Either | the board's unlock gesture | UI |

**Screen Timeout** is unchanged and still means what it always did — how long
the UI stays up with no input. With the lock screen enabled, what it arrives at
is the lock screen rather than a dark panel. **Lock Screen Off** then decides how
long that lit screen lasts before the panel goes out for real; on **Stay on** it
never ends on its own.

Each board's existing screen-off gesture does the same thing immediately: a tap
of the screen button, a held trackball click on the T-Deck, a held d-pad centre
on the M9. Each build's own gestures are listed under
[Locking and unlocking, build by build](#locking-and-unlocking-build-by-build).

**Nothing else raises the lock screen, and nothing but input takes it down.** A
message arriving while the device is locked never lights the panel — it only
changes what the carousel has to show the next time you look — and no timer,
packet or alert unlocks anything.

#### While it is up

- **Every input that is not that board's unlock gesture is swallowed.** Keys,
  taps, the trackball, the touch panel and the UI's action button all stop at
  the overlay rather than reaching the screen underneath, so a press in a pocket
  cannot send a message or open a surface you did not ask for.
- **Nothing is marked read.** Traffic arriving on the channel the UI happens to
  have selected underneath, or in an open DM, stays unread and still raises its
  mark — a conversation nobody can see is not one anybody is reading. Unlocking
  is what clears the mark for whatever is in front of you again.
- **The device will not light-sleep.** The panel is lit and the clock has to go
  on ticking, so the CPU stays out of the naps it would take with the screen
  dark. That cost is the whole reason **Lock Screen Off** exists, and why its
  default is five minutes rather than **Stay on**.
- **The keyboard backlight stays off** on the boards that have one — nothing on
  this screen takes typing — and comes back with the UI.
- **The keyboard-backlight unread blink waits for the panel to go out.** On
  `tdeck` and `tlora-pager-tft` a lit lock screen postpones it, and it starts
  when the dwell ends. `mesh-deck`'s RGB LEDs are unaffected and go on blinking
  for unread throughout. See [Light timeout](#light-timeout).
- **The panel runs at the lock screen's own brightness level**, not the UI's,
  and not whatever fraction the pre-sleep dim had reached on the way in — see
  [Lock screen brightness](#lock-screen-brightness).
- **Unlocking restarts the screen timeout** from zero, so the UI you come back
  to gets its full idle time instead of dropping straight onto the lock screen
  again.

#### The wake button: tap to glance, hold to unlock

**Every build with a dedicated screen button splits the two by press length.**
That is `wio-tracker-l2`'s top Wake button, `heltec-v4`'s side button on the
expansion, and the BOOT button on `tlora-pager-tft`, `cardputer-cap`,
`tdeck-pro`, `mesh-deck` and `m9` — every build where BOOT is not already the
UI's action button. The two without one are `tdeck`, which uses its trackball
click instead, and `heltec-r8`, which has only its touch panel.

| Press | Dark panel | Lock screen | UI in front of you |
| --- | --- | --- | --- |
| **Tap** | Brings the lock screen up | Nothing | Puts the device away |
| **Hold** (2 s) | Unlocks, straight to the UI | Unlocks | Nothing |

One rule in every state, so the gesture does not change with a state a dark
panel gives you no way to read. **A tap can never reach the UI** — the most a
press in a pocket can cost you is a lit lock screen that times itself back out.
The asymmetry is the point: putting the device away is cheap to undo and gets
the quick gesture, while waking it is what a pocket does by accident, and a
button held for two seconds is the one input a pocket does not produce.

With **Lock Screen** turned off, the same pair applies to the panel directly: a
tap puts it out, a hold brings it back, and a tap on a dark panel does nothing.

**`m9`'s d-pad centre follows the same rule**, because its controller reports a
tap and a hold as two different keys — so a tap raises the lock screen and only a
two-second hold reaches the UI. That matters on this build more than most: a
centre hold is also what puts the device away, and without the split the lightest
press of the same key undid it.

Everywhere else a build's existing wake input is unchanged, and still goes
straight through to the UI in one press: a tap on the panel on `heltec-v4` and
`heltec-r8`, a trackball click on `tdeck`, a wheel click or any key on
`tlora-pager-tft`. The section below gives each build in full.

#### Locking and unlocking, build by build

Every build target is listed, the two without a lock screen included. **Away** is
what puts the device away on demand; **Screen Timeout** does the same thing on
its own everywhere. **Unlock** is the complete list for that build — anything
not named under it is swallowed while the device is locked, reaching neither the
UI underneath nor the lock screen itself.

**`tdeck` — LilyGo T-Deck**

- Lock screen: **yes**.
- Away: hold the **trackball click** for 2 seconds.
- Unlock: a **trackball click**.
- Ignored: rolling the trackball, every keyboard key, and the touch panel.
- This build has no screen button, so it has no tap/hold split: the click is one
  press in both directions, and the hold on the same click is what locks.
- Its keyboard backlight's unread blink waits for the dwell to end and the panel
  to go genuinely out.

**`tdeck-pro` — LilyGo T-Deck Pro**

- Lock screen: **no**. The e-paper sleep screen *is* the sleeping state here —
  the panel holds that image at zero power, so there is nothing to time out of,
  no dwell to set and no carousel to turn.
- Away: tap **BOOT**.
- Unlock: hold **BOOT** for 2 seconds. A tap on a sleeping panel does nothing.
- Ignored: keys and the touch panel.
- Lock Screen and Lock Screen Off are absent from Config and web config, and
  Brightness is a single slider.

**`tlora-pager-tft` — LilyGo T-Lora Pager TFT**

- Lock screen: **yes**.
- Away: tap **BOOT**.
- Unlock: hold **BOOT** 2 seconds, the **wheel click**, or **any key** on the
  keyboard.
- Ignored: rolling the wheel. It has no touch panel.
- Its keyboard backlight's unread blink waits for the dwell to end, as on the
  T-Deck.

**`cardputer-cap` — M5Stack Cardputer + Cap LoRa/GPS**

- Lock screen: **no** — the 240x135 panel and this build's first-boot heap
  budget cannot carry the overlay, so the panel sleeps directly.
- Away: tap the **G0** button (BOOT).
- Unlock: hold **G0** for 2 seconds, or press **any key**.
- Lock Screen, Lock Screen Off and the lock-screen brightness slider are all
  absent from Config and web config.

**`heltec-v4` — Heltec WiFi LoRa 32 V4 + TFT expansion**

- Lock screen: **yes**.
- Away: tap the expansion's **side button**.
- Unlock: hold the side button 2 seconds, **tap the panel** anywhere, or press
  any key on a paired Bluetooth keyboard.
- Ignored while locked: the **USER button on GPIO0**. On the touch-first builds
  that button is the UI's action button — the Enter key's stand-in — not a
  screen key, so it cannot put the device away and is swallowed by the lock
  screen. From a fully dark panel it does wake the UI, since by then there is no
  glance surface left to protect.
- A tap on the panel has no tap/hold split of its own: from the lock screen it
  unlocks, and from a dark panel it goes straight to the UI.

**`heltec-v4-vertical`** — not a second firmware: `heltec-v4` with the
first-boot orientation seeded to portrait. Locking is identical in every
respect.

**`mesh-deck` — Attaky Mesh Deck**

- Lock screen: **yes**.
- Away: tap the **R** shoulder button, or tap **BOOT**.
- Unlock: hold either for 2 seconds.
- Ignored: every keyboard key, and the touch panel.
- The hardware **Power** button is deliberately not bound: a hold there cuts
  power below firmware at roughly the same two seconds that would unlock, so it
  could only ever end as a shutdown.
- Its three RGB LEDs go on blinking for unread while the lock screen is lit —
  unlike the keyboard-backlight builds, they do not wait for the panel to go
  out.

**`m9` — Elecrow ThinkNode M9**

- Lock screen: **yes**.
- Away: hold the **d-pad centre**, or tap **BOOT**.
- Unlock: hold the **d-pad centre** 2 seconds, or hold **BOOT** 2 seconds.
- A centre **tap** raises the lock screen and never reaches the UI. The
  controller reports a tap and a hold as two different keys, which is what lets
  this build have the split without the firmware timing a keypress.
- Ignored: every other key. It has no touch panel.

**`wio-tracker-l2` — Seeed Wio Tracker L2**

- Lock screen: **yes**.
- Away: tap the top **Wake** button.
- Unlock: hold the **Wake** button for 2 seconds.
- Ignored: the touch panel, which is not a wake gesture on this build at all —
  neither from the lock screen nor from a dark one — and the USER button on
  GPIO0, which is the UI's action button here as on the Heltecs. That button
  still wakes a fully dark panel straight to the UI.

**`heltec-r8` — Heltec WiFi LoRa 32 V4-R8 + Expansion Kit V2**

- Lock screen: **yes**.
- Away: **Screen Timeout only.** The Expansion Kit V2 carries no side button and
  the V4's GPIO35 does not exist on the R8 mainboard, so this is the one build
  with no on-demand way to put the device away.
- Unlock: **tap the panel**, or press any key on a paired Bluetooth keyboard.
  Touch is this build's only hardware wake gesture, and it unlocks in one tap —
  from a dark panel it goes straight to the UI.
- Ignored while locked: the USER button on GPIO0 — the UI's action button here
  too. It still wakes a fully dark panel to the UI.

**`heltec-r8-vertical`** — `heltec-r8` with the portrait first-boot seed, and
otherwise identical.

**A browser VNC session counts as someone looking at the screen.** While one is
connected the lock screen is dismissed and kept down, the panel and its timers
stay awake, and keys typed into VNC are exempt from the per-build restrictions
above — a remote viewer pressed them deliberately. That holds on every build
with a lock screen, since the only one without a VNC host is `cardputer-cap`,
which has no lock screen either. See [Browser VNC](#browser-vnc).

#### The first three seconds

The lock screen ignores input for **three seconds** after it appears. The press
that raised it is usually still being released, and without that window the same
press would read as the input dismissing it. The guard does not latch: a button
held through it unlocks the moment it expires rather than needing a second
press. The same three seconds cover a panel on its way out, which is why a press
landing just after the screen goes dark does not bring it straight back.

#### Lock screen brightness

The lock screen has its own backlight level, separate from the one the UI runs
at. A glance surface is read from across a room for two seconds at a time and
does not need the level you chose for reading messages — and on a lit LCD that
difference is most of what the lock screen costs in battery.

- The default is **10%**, the dimmest step there is.
- On-device: **Config → Brightness** opens two sliders, the screen level and the
  lock-screen level, both previewing live. [Brightness](#brightness) covers how
  each board moves between the two rows.
- Web config: **Lock Screen Brightness**, beside Brightness under **Display**.
- `cardputer-cap` and `tdeck-pro` have no lock screen and show one slider only.
  The other nine builds show both.

The level is applied on the way in and again on the way out, so the lock screen
never inherits the pre-sleep dim and the UI never comes back at the glance
level.

#### The settings

- **Lock Screen** enables or disables the intermediate screen. Disabled keeps
  the previous direct-to-sleep behavior exactly: the timeout and the screen-off
  gesture both put the panel straight out, and no overlay is ever built.
- **Lock Screen Off** starts at **30 sec**, then 1 and 2 min, then 5 to 60
  minutes in five-minute steps, plus **Stay on**. The default is 5 minutes.
- All three — Lock Screen, Lock Screen Off and the lock-screen brightness — are
  available in on-device Config and in web config under **Display**.
- **Turning Lock Screen off while it is up takes it down immediately**, rather
  than at the next timeout: off means off now. That is only reachable from web
  config, since the device's own Config screen is behind the lock screen.
- All three travel with an exported config, under `display:` as `lockScreen`,
  `lockScreenOffSecs` and `lockScreenBrightness` — see
  [Backing up settings](#backing-up-settings).
- `cardputer-cap` keeps direct screen sleep and does not show these settings:
  its 240x135 panel and first-boot heap budget cannot carry the overlay.
- `tdeck-pro` keeps its existing black-on-white e-paper sleep screen. E-paper
  holds that image without a lit backlight, so it does not use the dwell timer
  and does not rotate its band.
- `heltec-v4-vertical` and `heltec-r8-vertical` are the two base builds with a
  portrait first-boot seed, so they carry these settings unchanged.

### Scan SD Card for Malware (ThinkNode M9)

**Config &rarr; Scan SD Card for Malware**, directly under Factory Reset, and
**web config &rarr; Utilities &rarr; Diagnostics**. Nothing runs on its own — the
scan happens when you ask for it.

It walks the card looking for the files a Windows storage worm leaves behind:

| What it matches | Why |
| --- | --- |
| `autorun.inf` | Hijacks what opening the card does in Explorer |
| `.exe` `.pif` `.com` `.scr` `.bat` `.cmd` `.cpl` `.msi` `.vbs` `.vbe` `.js` `.jse` `.wsf` `.wsh` `.hta` | Windows programs and scripts |
| `.lnk` | Windows shortcuts — these masquerade as folders |
| Any file at all opening with an `MZ` header | A Windows program wearing a harmless-looking name, caught by its header rather than its extension |

Camillia's own `/camillia` folder is skipped — every file in it was written by
this firmware. The walk is bounded (4 folders deep, 4000 files) so a card full of
map tiles cannot turn this into a several-minute wait.

**A progress dialog stays up for the duration**, because the scan opens and reads
every file and that is not quick. It counts the card first — a fast pass that
only reads folder listings — so the bar has a real total to fill against rather
than just spinning. While it runs you get the file count, the percentage, the
folder currently being walked, and a running tally of matches. The same dialog
appears for the delete.

**Back cancels it** and drops you straight back on the Config screen with no
dialog — a result for a question you just withdrew is not worth reading, and
"nothing found" would be actively misleading, since a cancelled scan stopped
looking rather than finished looking. Cancelling a *delete* reports what it got
through, because stopping does not put back what has already gone.

**None of it can run on the radio.** An ESP32 cannot execute a Windows binary,
which is exactly what makes the device a useful place to look at a card that is
not safe to open anywhere else. The risk is the next PC the card goes into.

**If it finds something, it asks before deleting anything.** On the device that
is a Yes/No dialog naming the files; in web config it is a browser confirmation.
The dialog's message scrolls — **the d-pad, the arrow keys and J/K move it** —
so a long list never pushes the Yes and No buttons off the panel.
Answering yes re-walks the card and deletes only what it still recognises — the
delete is handed no list, so there is no path by which it can be pointed at map
tiles, chat history or a backup. Folders are never removed.

**It cannot promise the card is clean afterwards.** It reads names and file
headers, not contents. Formatting is the only answer that can — which is why the
dialog offers it as a third button.

#### Format

**Format** on the scan's dialog rewrites the card's filesystem from scratch. It
asks a second time first, in plainer words, because the first dialog was about a
handful of matched files and this is about everything:

> This erases EVERYTHING on it — chat and DM history, the node database, map
> tiles and your saved configuration — not just the files the scan matched.

That is a real format, not a sweep that deletes files, so nothing survives it and
a damaged filesystem is repaired along the way. The card is unmounted, rewritten,
remounted, and `/camillia` recreated.

**Afterwards it offers to write your configuration back** as
`/camillia/config.yaml`. Offered rather than done: a config export is your copy
of your own settings, and where it lives is your decision rather than a side
effect of cleaning a card. Everything else that was on the card — transcripts,
the node database, map tiles — was on the old filesystem and is gone.

This exists because of Elecrow's September 2026 advisory for the ThinkNode M9,
whose bundled cards were contaminated during factory map-data flashing — a hidden
`autorun.inf` at the card root launching a dropper when the card is opened in
Explorer. That is why the feature is on the M9 and nowhere else, though nothing
in the check is M9-specific.

If you would rather use a tool built for the job,
[wadamesh](https://github.com/ALLFATHER-BV/wadamesh) ships **SD Scan**, which is
where this approach came from.

### Remote administration

**Camillia can administer another Meshtastic node** — read its configuration,
change settings, reboot it — from a terminal opened on that node. It is a client
only: it administers others and does not serve the other half, so nothing here
lets anyone administer *your* device.

#### Before anything works

The remote must carry **this node's public key** in its `security.admin_key`.
Web config shows that key as a chip at the top of the page — click it to copy.
Without that, every command comes back `not authorized`, which is the system
working correctly.

#### The gate

A node has to be an **admin peer** before a terminal can be opened on it, and
peers are not added by hand — **favourite nodes are probed once per boot**, and
the ones that accept us turn up in **web config → Utilities → Remote Admin** on
their own.

Favourites rather than everything heard: that is a short, deliberate list, where
probing the whole node table would put an admin packet in front of every stranger
on the mesh once per reboot. Only favourites carrying our public key are asked at
all — PKI is the only path the remote accepts, so a node without a key can never
be administered from here whatever its own settings say. The sweep is sequential
and spaced, starts well after boot, and stands aside entirely while a terminal is
open.

The list shows each peer by name, a state badge — green **(C)** confirmed, the
remote answered and accepts us; yellow **(U)** unconfirmed, listed but not yet
proved — and when it last answered. A peer that has never answered, or that
answered while the device clock was unset, reads **never**. **Verify** asks again without leaving the page — the probe is a radio
round trip of up to 30 seconds, so the button reports that it asked and the badge
changes on the next reload. A node the remote has actually refused is dropped
from the list rather than shown greyed: this list answers "what can I
administer", and a row that can only ever say no is not part of that answer.

| State | Meaning | Terminal |
| --- | --- | --- |
| Not listed | — | hidden |
| `pending` | added, never proved | hidden |
| `confirmed` | a probe came back with a session key | **available** |
| `denied` | the remote refused us | hidden |

**Verify** runs the probe. It is a real authorization test rather than a guess:
an unauthorized sender gets a refusal where an authorized one gets an answer. A
refusal on any later command demotes the peer and closes the terminal; a
*timeout* never does, because out of range and "not authorized" are different
things. Confirmed peers are re-proved after a reboot — the remote may have been
reconfigured while this device was off.

#### The terminal

On the device it is the **(A)dmin** row in a node's actions menu. In web config
there are two ways in: the **Terminal** button beside the peer in Utilities, and
an **Admin** button on the node's card in the Nodes tab.

All three appear for any **listed** peer the remote has not refused — unconfirmed
included. An unconfirmed peer is one nobody has asked yet, and after a reboot
that is every peer, so waiting for the sweep before the terminal reappeared made
it look broken for the first minute of every boot. Trying costs one refused
packet to a node already on your list; if the remote says no, the session closes
and the peer is recorded as refused.

They are **not** offered on unlisted nodes, and are never rendered disabled: a
greyed "Admin" on a stranger's node is an invitation to try, and trying there
means an unauthorized admin packet across the mesh.

Typing `help` in the browser opens the command table in a panel beside the
terminal, so it is readable while commands are being typed. Both drive the same session, so opening the browser on a node the
device already has open continues that conversation rather than starting a rival
one.

Type `help` for the command table. Reads (`info`, `owner`, `config lora`,
`channel 0`) are safe; writes announce what they are doing:

```
!a1b2c3d4 [rf] > set lora hop_limit 5
· fetching lora config
· splicing hop_limit = 5
· sent id 3f2a91c4 via rf
✓ ack (rf)
```

**That read is not decoration.** Writing one setting replaces the remote's whole
configuration block, so every write reads the block first and changes one field
inside the bytes that came back — anything the remote carries that this firmware
has never heard of survives untouched. A read that fails never becomes a write.

**Destructive commands** — `reboot`, `shutdown`, `reset` — need the word
`confirm` on the next line *and* a fresh session key, and are never retried
automatically.

#### Transports

`transport rf`, `transport mqtt`, or `auto` (the default), which prefers the
radio when the node was heard on it recently and falls back to the broker. MQTT
rides Meshtastic's `PKI` pseudo-channel and needs the remote to have downlink
enabled on some channel. Replies are accepted from either transport regardless
of how the request went out.

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

On boards with a notification light — the Mesh Deck's Core RGB LED plus the
left and right RGB indicators when its Keyboard module is attached, and the
T-Deck and T-Lora Pager's keyboard backlight — those lights repeat once a second
for as long as anything is unread. All three Mesh Deck LEDs mirror the same
color and pattern. A message that lands overnight blinks all night, and on the
keyboard-blink boards it also keeps the device out of light sleep, so it costs
battery as well as attention.

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

#### Automatic updates

The boot check needs someone in front of the device to answer it. A node that
sits unattended for months — a repeater on a mast, a solar node in a field —
therefore never updates, whatever the boot preference says. Web Config →
**Firmware Updates** → *Automatic Updates* is the answer to that: set it to
**Every hour**, **Every 6 hours**, **Every 12 hours** or **Every 24 hours** and
the device checks on that schedule and, if a newer release exists, downloads,
verifies and installs it **with no prompt and no keypress**, then reboots into
it. Anything in progress on the device is lost at that reboot.

It is **Off** by default, on a fresh flash and on a device upgrading from an
older build alike. A device only ever installs firmware unattended because
somebody asked it to.

- It follows the **Release Channel** setting, exactly as the boot check does.
- It is skipped while the battery is low, and while WiFi is off or
  disconnected — the cycle simply waits and runs once the condition clears.
- It is skipped on a third-party partition layout, where there is no slot to
  install into.
- It will not reboot out from under an open dialog.
- The first cycle runs shortly after boot rather than a full period later, so a
  node coming back from a power cut catches up straight away.
- The schedule is measured on uptime, not the wall clock: a field node may never
  get an NTP sync, so a clock-based schedule would not be dependable.

While Automatic Updates is set it **supersedes** *Check for Updates on Boot* —
the device installs on its own shortly after booting instead of asking, so the
boot preference has no effect until Automatic Updates is turned back off.

If an install keeps failing on the same release — a truncated download, a
signature mismatch, a build published without this device's slug — the device
gives up on that particular version after three attempts rather than
re-downloading it every period forever. It starts trying again as soon as a
different release is published, and the reason is reported on the next boot.

The setting round-trips through `config.yaml` as `otaAutoUpdate` (`Off`, `1h`,
`6h`, `12h` or `24h`). Anything else in that field reads as `Off`.

### Chat style

Config has a **Chat Style** action. Selecting it opens a picker modal — navigate
with the usual up/down input and press Enter (or tap) to choose Classic,
Bubbles, or Outline; Backspace/Esc cancels. Choosing a *different* style reboots
to apply it; re-choosing the current style just closes without a reboot.

- **Classic** — one flat, colored text line per message. Your sent messages
  gain an `[ACK]` marker just after the timestamp once the message is
  acknowledged, and turn red on failure — in channel chat and Direct Messages
  alike. On color displays, color separates the two kinds of acknowledgement:
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

T-Deck Pro is fixed to Outline mode and renders black text and outlines directly
on the white Camillia Paper background. ACK and failure state remains visible in
the text marker and layout rather than color. Its Chat Style selectors are
omitted from the device and Web Config.

The style applies to both **channel chat and Direct Messages**. The Web Config
**Chat Style** dropdown offers the same three choices on configurable builds.
All three styles are available on those builds, including the Cardputer.

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

- **Keyboard builds** — press **E** on the chat/DM screen
- **Heltec / Wio (touch-only)** — the 😀 button beside Cancel / Send. The tray
  has a **Close** button along its bottom edge: it fills all but a few pixels of
  the screen, so the tap-outside gesture the other builds rely on has almost
  nothing left to aim at

To **insert** an emoji into a message you are already typing — as opposed to
sending one on its own — the tray opens in insert mode instead, and the glyph
you pick lands at the cursor. It stays open, because picking several in a row is
the usual case; a close key dismisses it.

- **T-Deck** — press the **microphone key**, between M and Enter. Nothing on
  this board records audio, so that key did nothing at all before. It is
  invisible to the ordinary key path (the keyboard's own controller swallows it,
  exactly as it swallows Alt), so the firmware reads it straight off the key
  matrix while a message is open
- **T-Deck / T-Deck Pro / Mesh Deck** — tap the 😀 button beside the message box
- **T-Lora Pager** — the same button, reached by the wheel: roll forward to walk
  the caret through what you have typed, and the detent after the last character
  steps onto the button. **Enter** there opens the tray. Rolling back, or typing
  anything at all, returns to the message
- **Mesh Deck** — also the **far-left key of the bottom row**, which had no
  meaning before. Or press the **symbol key** twice: the first press opens the
  symbol tray, the second swaps it for emoji, and a third closes it. A close key
  gets out from either tray, so there is nothing to cycle past
- **M9** — press **Ctrl**. It opens the tray and does not close it again; use
  the close key for that
- Picking an emoji leaves the tray up, because picking several in a row is the
  usual case — so a brief **😀 added** appears at the foot of the tray to confirm
  the glyph went into the message behind it
- **Touch builds** — the 😀 button beside Cancel / Send, as above

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
- **Message Info** (**I**) — who sent it and how it reached you: over **LoRa**
  or **MQTT** (the same radio/globe mark the chat shows in front of it), and the
  node that handed it to you on the last leg. Over LoRa that is the relaying
  node, or "heard directly"; the radio header only carries the last byte of the
  relayer's id, so when several known nodes end in that byte they are all
  listed, direct neighbours first. Over MQTT it is the gateway that uplinked it.
  Names are the long name, else the short name, else the node id. Hops, SNR and
  RSSI are shown too. Details are kept for the last 64 received messages since
  boot; older ones say so.
- The node actions for the **sender**: Traceroute (**T**), Send DM (**D**),
  Request Node (**Q**), Request Position (**P**) and Ignore (**G**). Favorite is
  left to the Nodes screen.

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

Node Actions also has **Share** (**H**): it transmits that node's NodeInfo — its
id, long and short names, and public key when known — once, zero-hop, so every
node within radio range of you learns it without having heard it themselves.
It goes out under the shared node's own id, the way a NodeInfo has to, with
your node as the one that transmitted it. Two things follow from that: nodes
that receive it treat the shared node as a direct neighbour at your signal until
its own traffic says otherwise, and the hardware model and role it carries are
blank, because this node does not keep them. It never goes to MQTT, is limited
to one share every 5 seconds, and is greyed for nodes with no name yet and for
your own node.

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
rather than a number.

On every board except Cardputer and the T-Deck Pro there are **two** sliders:
the screen level and the lock-screen level, the latter being what the glance
surface lights to. Both preview live, and the one your keys are on is marked
three ways so it is unmistakable at a glance: a `>` caret on its label, the
label in the accent colour, and an accent outline around the slider itself. The
other row's knob and bar dim and its percentage fades, without going away —
setting a glance level is largely about comparing it against the screen level.

- **K** steps brighter, **J** steps dimmer
- Which input moves between the two rows depends on what the board has:
  - **T-Deck, Mesh Deck**: Left/Right pick the row; the trackball or scroll keys
    adjust the value alongside j/k
  - **T-Lora Pager**: rolling the **wheel** moves between the rows — it is the
    board's only vertical input — and j/k adjust
  - **ThinkNode M9**: the **d-pad Up/Down** moves between the rows, and d-pad
    Left/Right adjusts along with j/k. This is the one screen where the pad's
    Left/Right is not channel or column movement
  - **Heltec, Wio Tracker L2**: no keys are involved — drag either slider
    directly, then Save or Cancel
- **Enter** saves both levels and closes
- **Backspace/Esc/Back** (or tapping outside) cancels and restores the levels
  you opened with

The default matches whatever brightness the board has always used, so an
unconfigured device looks unchanged; the lock-screen level defaults to **10%**.
Web Config offers both as sliders under **Display** — **Brightness** and **Lock
Screen Brightness** — and they are included in YAML export/import as
`display.brightness` and `display.lockScreenBrightness`. What the second one is
for is described under [Lock screen brightness](#lock-screen-brightness).

### Web Config

Web Config serves a browser-based settings UI over Wi-Fi. **It is on by default
on a new device**, so a freshly flashed board comes up as the `camillia-mt`
access point and can be set up from a phone without touching the device screen.
Toggle it from the Config screen; the row shows the address once it is running.
On boards that can pair a Bluetooth keyboard, the row sits directly under
**Choose WiFi** — it is what most people turn Wi-Fi on for — with the two BT
keyboard rows after it. Elsewhere it stays at the head of the services group,
under the GPS row.

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
Pager, 240x320 on the T-Deck Pro or a vertical Heltec build, 320x240 elsewhere —
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
- The **Remote** tab and its endpoints are compiled into every environment
  except `cardputer-cap`. That board is the one without them: the mirror needs a
  full-panel buffer in PSRAM, which it does not have. The other requirement is a
  Wi-Fi station.
- The Heltec has no physical keyboard of its own. Browser keystrokes are injected
  into its key handling as though one were attached, so they work wherever the
  other boards' hardware keys do. Its on-screen keyboard is a separate path into
  the text box and is unaffected.
- On the T-Deck Pro the browser shows the e-paper UI in black and white, because
  that is what the panel itself is: LVGL drives it at one bit per pixel and the
  mirror expands those bits on their way into the frame. It updates on every
  LVGL redraw rather than on every e-paper refresh, so the remote view usually
  reaches a new screen slightly ahead of the panel it is mirroring.
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

### Signed packets

Meshtastic 2.8 can sign a packet with the same identity key it already uses for
direct messages, so a receiver can tell that a packet genuinely came from the
node it claims to. Camillia checks those signatures when they arrive.

The node detail panel gains a **Signed** row with three states. **yes** means a
packet from that node carried a signature that verified against the key we hold
for it — that node has proved it holds the matching private key. **not seen**
means we have their key but no signed packet yet, which is the ordinary case
today: almost nothing on the mesh signs. **no key** means we have not learned
their public key at all, so there is nothing to check against.

Nothing is rejected for being unsigned, and nothing is rejected for failing.
Meshtastic's own default accepts unsigned traffic, and dropping packets on a
failed check would punish the whole mesh for a feature barely in use. A failed
check is logged rather than acted on — it means either the key we hold is stale
or something is claiming to be that node.

Camillia does not sign its own packets yet. That needs a signing primitive the
cryptography library here does not provide, and is tracked separately.

### Traffic this node no longer relays

A packet whose header claims it has travelled *more* hops than it started with
is malformed, and Meshtastic 2.8 throws it away before decrypting it. Camillia
now drops the same packets instead of relaying them, so no airtime is spent
carrying a frame every 2.8 node in range will refuse.

One case in that rule is worth naming: firmware older than Meshtastic 2.3.0
never filled the field in at all, so its packets look the same and are dropped
too. If you are the only relay for a very old node, it loses you as a hop. That
is deliberate — 2.8 refuses those frames regardless, so relaying them only
reaches nodes in a mesh with no 2.8 node anywhere in it.

Dropped frames are logged under `[fwd]` with the sender and packet ID, so this
shows up as a line in the debug monitor rather than as traffic that quietly
stops moving.

### Frequency slots corrected in six regions

Your region and preset decide a frequency, by dividing the band into slots and
hashing the channel name to pick one. The slot count was being rounded down
where the band is not a whole number of channels wide; Meshtastic rounds to
nearest. Ours now does too.

Where the two disagreed, Camillia was tuning somewhere no stock Meshtastic node
was listening. Correcting it means **your node may move to a different frequency
on upgrade** — onto the one the rest of the mesh has been using all along.

Six regions are affected, and only at some presets: **ANZ 433**, **UA 433**,
**UA 868**, **PH 433**, **PH 868** and **KZ 433**. Everywhere else — US, EU 868,
CN, JP, ANZ, KR, TW, IN, NZ, TH, MY, SG, KZ 863, NP, BR and LORA 24 — the two
methods already agreed at every preset, so nothing moves.

The one to watch is **PH 868 on Long Fast**, the default preset, which moves from
868.125 MHz to 869.375 MHz. If you run a Camillia-only mesh in one of the six,
upgrade every node together: an upgraded node and a non-upgraded one will be on
different frequencies and will not hear each other.

### Two new regions

**EU 866** (865.6–867.6 MHz) and **EU Narrow 868** (869.4–869.65 MHz) are
selectable in Config and web config. They are the first regions that space and
pad their slots rather than packing them edge to edge: EU 866 gives four
channels at 865.7, 866.3, 866.9 and 867.5 MHz, and EU Narrow 868 is a single
pinned slot rather than a hashed one.

The amateur-radio regions Meshtastic 2.8 added at 2 m, 70 cm and 125 cm are
deliberately **not** offered. They are licensed-only, and the firmware has no
concept of a licensed operator to gate them behind — no call sign, and no
restriction on relaying between licensed and unlicensed users. Listing them
would be handing you a menu entry that transmits unlicensed on an amateur band.

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

**Choosing AP switches the two settings the mode needs.** Web Config is what
raises the access point, so it is turned on, and the status line comes back with
the address to browse to (`AP mode: 192.168.4.1`). The MQTT bridge is turned off
first — it dials out to a broker, there is no route to one from a device serving
its own network, and the two cannot run together in any case. Both are ordinary
settings afterwards: turning Web Config off again drops the access point with
it. With the master **WiFi** row off nothing can start, so the picker says
*enable WiFi first* and changes nothing.

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

### Heltec WiFi LoRa 32 V4 + TFT expansion (heltec-v4)

Primary usage is touch.

- **Landscape or portrait is a setting, not a build.** Config → **Orientation**
  opens a picker listing **Landscape**, **Portrait** and **Portrait 180**, with
  the one in force marked *(current)*. Choosing any other asks to confirm and
  then reboots, because the panel rotation is fixed when the display comes up;
  choosing the current one just closes. A fresh device is landscape.
  The two portraits are a half turn apart: which one you want depends on where
  the cable leaves the case and which hand is holding it, so it is a choice
  rather than something the firmware picks. Web Config → **Orientation** offers
  the same three as a dropdown.
  There is no separate vertical firmware any more — `heltec-v4-vertical` still
  exists as a build target, but it produces the same firmware with the portrait
  default pre-set, for units coming off the old separate vertical build
- The **Wio Tracker L2** has the same setting, in both Config → **Orientation**
  and Web Config → **Orientation**. It shares this board's 240x320 panel, so
  both shapes lay out identically; only the rotation values differ, because the
  Wio's panel carries its own rotation offset. There is no seeded
  `wio-tracker-l2-vertical` target — the setting is the only route
- The **T-Display P4** uses the same Config and Web Config selectors. Its native
  568x1232 portrait orientation is the first-boot default; choosing Landscape
  rotates the panel and GT9895 touch together after the required reboot
- Bottom touch nav provides Home, DM, Nodes, Live, Config, and Help, in that
  order left to right
- **An unread DM lights the nav bar's DM icon**, which blinks amber until you
  open it. Boards that can turn the bar off fall back to a small envelope in the
  footer when it is off; here the bar is not optional, so the alert always sits
  on the button that answers it
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
  every one of them: Channels, Device Info, Action Result, Release Notes,
  Bluetooth Keyboard, the emoji tray, Channel Actions, the Live Filter picker,
  Locate, Line of Sight, the traceroute progress popup, the New DM node picker
  and the hidden system-stats screen. The full-screen tools — the SNR/RSSI and
  Channel Utilization charts, Beacons, Discovery and the MQTT monitor — put the
  same X at the right end of their header bar, with their own actions to its
  left. Where a popup also dismissed on a tap outside it, that still works
- **Tools and Help are screens, not popups.** Both own a cell on the nav bar, so
  both carry the bar with their own cell lit, and every other cell still works
  from inside them. Tools used to be a picker on a dimming backdrop: the bar
  behind it was greyed out and a tap aimed at Nodes only closed Tools. That
  backdrop is gone, and with it tap-outside-to-close — Tools now closes the way
  Nodes and Config do, by the corner X, the close key, Home, or a second tap on
  the wrench. Help closes the same ways, plus a second tap on the ?
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

- Dedicated buttons open Chat, Home (the dashboard), DMs, Tools and Map from
  anywhere; Chat pressed on the chat screen opens the channel list
- Holding the d-pad centre sleeps the screen — or raises the lock screen, which
  a held centre then unlocks. A centre tap never reaches the UI; it only brings
  the lock screen up. See [Lock screen](#lock-screen)
- D-pad Up/Down navigates, Left/Right switches channels or hops columns —
  except in the New Message box, where the d-pad moves the text cursor
  (Left/Right by a character, Up/Down by a line), and on the Brightness screen,
  where Up/Down moves between the two sliders and Left/Right sets the level
- Sweep/Scan Settings and MQTT Scan Settings work the same way as Brightness:
  Up/Down moves between the duration slider and the save checkbox (the focused
  one is outlined), Left/Right moves the slider or ticks the box, and Enter
  starts the run
- In Node Actions and Message Actions the d-pad moves across the buttons as
  they are laid out: Up/Down between rows, Left/Right between the two columns.
  In Message Actions, Up from the top row reaches the reaction strip, where
  Left/Right walks the reactions and Down drops back into the column below
- H opens the home dashboard, C the chat screen (again for the channel
  selector), F the configuration screen. D, N and L are unchanged — Direct
  Messages, Nodes and Tools
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
