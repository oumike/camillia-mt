# Camillia for Meshtastic Use Guide

This guide reflects current firmware navigation and controls.

## Main screen

The main screen is channel chat. Use it to read traffic, select reply targets, and start compose.

## Keyboard shortcuts by build

### Shared shortcuts (keyboard builds)

These apply to all keyboard builds: `tdeck`, `tlora-pager-tft`, and `cardputer-cap`.

- D opens Direct Messages
- C opens Config
- N opens Nodes
- L opens Live
- A opens Channel Actions
- Enter opens compose (or reply compose when a chat row is selected)
- Live modal shortcuts: C clears the log, U opens channel-util chart, S opens SNR/RSSI chart

### LilyGo T-Deck (tdeck)

- H toggles the channel selector
- J/K map to Up/Down navigation in lists and chat row selection
- Trackball Up/Down follows the same Up/Down behavior as J/K
- Modal close key is Backspace (Esc is also accepted)
- DM delete trigger on selected conversation: Backspace
- Compose close behavior: Backspace on an empty compose closes the compose modal
- Trackball click hold for 2 seconds puts the screen to sleep

### LilyGo T-Lora Pager TFT (tlora-pager-tft)

- H toggles the channel selector
- Wheel Up/Down on main chat switches channels
- Wheel Click enters/exits chat row cursor mode
- In chat row cursor mode, Wheel Up/Down moves the selected chat row
- Backspace exits chat row cursor mode
- Config modal: I focuses the info panel; Wheel Click swaps focus between actions/info
- DM modal: Wheel Click swaps focus between conversation and message panels
- Modal close key is Backspace (Esc is also accepted)
- DM delete trigger on selected conversation: Backspace (including Symbol+Backspace / hold path)
- Compose close behavior: Backspace on an empty compose closes the compose modal

### M5Stack Cardputer + Cap LoRa/GPS (cardputer-cap)

- H toggles the channel selector
- Channel switch: comma (previous), slash (next)
- Navigation: semicolon (Up), period (Down)
- Arrow keys map to the same directional actions
- Esc closes modals and exits chat-focus modes
- Enter confirms actions; Fn+Enter is also accepted for compose/reply flow
- DM delete trigger on selected conversation: Fn+Backspace
- Compose close behavior: Esc closes compose (Backspace only deletes characters)

### Heltec WiFi LoRa 32 V4 + TFT expansion

Builds: `heltec-v4`, `heltec-v4-vertical`

- Primary usage is touch (no dedicated hardware keyboard shortcuts)
- Bottom touch nav: Config, DM, Nodes, Live, Help
- DM delete trigger: long-press a conversation row

![Chat screen](screenshots/RiCa_screen_20260609_110546.png)
![Chat screen 2](screenshots/RiCa_screen_20260609_110604.png)
![New message](screenshots/RiCa_screen_20260609_110637.png)
![Reply message](screenshots/RiCa_screen_20260609_110658.png)

## Live screen

Live shows decoded RX and TX traffic with per-traffic coloring.

- Open from the main screen (L on keyboard builds, Live bottom-nav button on Heltec)
- Scroll with Up and Down input
- Press C to clear the log
- Close with the device close key (see device sections below)

![Live screen](screenshots/RiCa_screen_20260609_110224.png)

## Config screen

Config includes Web Config controls, export and import, theme toggles, announce, and reset actions.

- Open from the main screen (C on keyboard builds, Config bottom-nav button on Heltec)
- Navigate action rows with Up and Down input
- Enter runs the selected action
- Keyboard builds: I opens/focuses the info panel within Config
- Import, Clear Nodes, and Factory Reset require a second Enter confirmation

![Config screen](screenshots/RiCa_screen_20260609_110933.png)

## Nodes screen

Nodes shows discovered nodes and detail fields, including map position details.

- Open from the main screen (N on keyboard builds, Nodes bottom-nav button on Heltec)
- Navigate rows with Up and Down input
- Close with the device close key

![Node details screen](screenshots/RiCa_screen_20260609_110407.png)

## Direct Messages

Direct messaging:

- Open from the main screen (D on keyboard builds, DM bottom-nav button on Heltec)
- Pressing Enter on New DM opens node picker
- Enter on a conversation focuses message panel
- Enter again in focused message panel opens compose for that DM

![Node select](screenshots/RiCa_screen_20260609_112419.png)
![Message view](screenshots/RiCa_screen_20260609_113850.png)
Delete behavior:
- T-Deck and T-Lora Pager: Backspace triggers delete confirmation on selected conversation
- Cardputer: Fn+Backspace triggers delete confirmation on selected conversation
- Heltec touch build: long-press a conversation row for delete confirmation

## Help screen

Help explains shortcuts and transport symbols.

- Heltec: open from the bottom Help nav button
- While Help is open, D, C, N, and L jump directly into those screens

## Compose behavior

- Enter sends
- Backspace deletes a character
- Cardputer: Esc closes compose
- T-Deck and T-Lora Pager: Backspace on empty compose closes
- Heltec: use on-screen controls to close compose

## Device controls

### LilyGo T-Deck (tdeck)

Primary usage is touch plus keyboard shortcuts.

- Use touch for channel chips and UI buttons
- D, C, N, L open main modals; A opens Channel Actions
- H toggles channel selector
- Enter opens compose or reply compose
- Optional Vim-style helpers in navigation views: J maps to Up and K maps to Down
- Modal close key: Backspace (Esc also works)

### LilyGo T-Lora Pager TFT (tlora-pager-tft)

Primary usage is wheel plus keyboard.

- Wheel Up and Down on chat switches channels
- Wheel Click enters/exits row cursor mode
- In row cursor mode, Wheel Up and Down moves selected chat row
- Backspace exits row cursor mode
- Enter opens compose or reply compose
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
- Enter/Fn+Enter opens compose or confirms selected actions
- Fn+Backspace is the DM delete trigger

### Heltec WiFi LoRa 32 V4 + TFT expansion (heltec-v4, heltec-v4-vertical)

Primary usage is touch.

- Bottom touch nav provides Config, DM, Nodes, Live, and Help
- Use on-screen touch lists and buttons inside each modal

## Close key summary

- Cardputer label: Esc
- T-Deck and T-Lora Pager label: Bksp
- Heltec touch: use on-screen navigation and close controls

Esc is accepted as a close key in most keyboard flows.
