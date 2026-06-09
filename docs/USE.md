# Camillia MT Use Guide

This guide reflects current firmware navigation and controls.

## Main screen

The main screen is channel chat. Use it to read traffic, select reply targets, and start compose.

Global shortcuts (keyboard builds, when no modal is open):
- D opens Direct Messages
- C opens Config
- N opens Nodes
- I opens Live
- L opens Legend
- Enter opens compose (or reply compose if a message is selected in the chat screen)
- Backspace clears selected reply context

![Chat screen](screenshots/RiCa_screen_20260609_110546.png)
![Chat screen 2](screenshots/RiCa_screen_20260609_110604.png)
![New message](screenshots/RiCa_screen_20260609_110637.png)
![Reply message](screenshots/RiCa_screen_20260609_110658.png)

## Live screen

Live shows decoded RX and TX traffic with per-traffic coloring.

- Open with I from chat
- Scroll with Up and Down input
- Press C to clear the log
- Close with the device close key (see device sections below)

![Live screen](screenshots/RiCa_screen_20260609_110224.png)

## Config screen

Config includes Web Config controls, export and import, theme toggles, announce, and reset actions.

- Open with C from chat
- Navigate action rows with Up and Down input
- Enter runs the selected action
- Import, Clear Nodes, and Factory Reset require a second Enter confirmation

![Config screen](screenshots/RiCa_screen_20260609_110933.png)

## Nodes screen

Nodes shows discovered nodes and detail fields, including map position details.

- Open with N from chat
- Navigate rows with Up and Down input
- Close with the device close key

![Node details screen](screenshots/RiCa_screen_20260609_110407.png)

## Direct Messages

Direct messaging:

- Pressing enter on New DM opens node picker
- Enter on a conversation focuses message panel
- Enter again in focused message panel opens compose for that DM

![Node select](screenshots/RiCa_screen_20260609_112419.png)
![Message view](screenshots/RiCa_screen_20260609_113850.png)
Delete behavior:
- T-Deck and T-Lora Pager: Backspace triggers delete confirmation on selected conversation
- Cardputer: Fn+Backspace triggers delete confirmation on selected conversation
- Heltec touch build: long-press a conversation row for delete confirmation

## Legend screen

Legend explains shortcuts and transport symbols.

- Open with L from chat
- While legend is open, D, C, N, and I jump directly into those screens

## Compose behavior

- Enter sends
- Backspace deletes a character
- Cardputer: Esc closes compose
- Other keyboard builds: Backspace on empty compose closes

## Device controls

### LilyGo T-Deck (tdeck)

Primary usage is touch plus keyboard shortcuts.

- Use touch for channel chips and UI buttons
- D, C, N, I, L open main modals
- Enter opens compose or reply compose
- Optional Vim-style helpers in navigation views: J maps to Up and K maps to Down
- Modal close key: Backspace (Esc also works)

### LilyGo T-Lora Pager TFT (tlora-pager-tft)

Primary usage is wheel plus keyboard.

- Wheel Up and Down on chat switches channels
- Wheel Click enters row cursor mode
- In row cursor mode, Wheel Up and Down moves selected chat row
- Backspace exits row cursor mode
- Enter opens compose or reply compose
- Config modal: Wheel Click swaps focus between action list and info panel
- DM modal: Wheel Click swaps focus between conversation list and message list
- Modal close key: Backspace (Esc also works)

### M5Stack Cardputer + Cap LoRa/GPS (cardputer-cap)

Primary usage is keyboard.

- Channel switch: comma for previous, slash for next
- Navigation: semicolon and period act as Up and Down in list views
- Arrow keys map to the same directional actions
- Escape closes modals and exits chat focus mode
- Enter opens compose or confirms selected actions
- Fn+Backspace is the DM delete trigger

### Heltec WiFi LoRa 32 V4 + TFT expansion (heltec-v4, heltec-v4-vertical)

Primary usage is touch.

- Bottom touch nav provides Config, Nodes, Live, and Legend
- Use on-screen touch lists and buttons inside each modal
- DM modal is not on the bottom touch nav row

## Close key summary

- Cardputer label: Esc
- T-Deck and T-Lora Pager label: Bksp
- Heltec touch: use on-screen navigation and close controls

Esc is accepted as a close key in most keyboard flows.
