### New
- **MECP emergency codes** (all boards): received MECP messages show in plain language in your UI language, including severity, the emergency codes, the number of people, position and time. In the classic chat style, a received MECP message is coloured by severity: red for MAYDAY, orange for urgent, amber for safety and blue for routine. The original message is kept, so replies quote it as it was sent, and for a channel message the message action menu shows the original text.
- **MECP composer** (all boards): open it from Tools → MECP, or press E in the Tools menu. One form lets you set severity, up to two emergency codes, the number of people, your position and time, an optional note and the channel to send on. The form always opens on routine severity. You choose the channel on the form, and the open chat does not change it. Any MAYDAY asks for confirmation before it is sent, including one typed by hand in the compose box. On short screens such as the T-Deck and CardputerZero, the form is split into pages with Previous and Next. Touch-only boards get an on-screen keyboard for the note.
- **MECP beacon** (all boards): sends a MAYDAY beacon with your position and time on the chosen channel. It goes out at once, then every 5 minutes, every 15 minutes after 6 hours and every 30 minutes after 24 hours. Starting the beacon asks for confirmation. Stopping it sends a cancel, and a restart also stops it.
- **MECP beacon drill** (all boards): a practice beacon at routine severity that stops itself after three sends.

### Changed
- The user guide now covers the CardputerZero and the Tanmatsu, including their keyboard shortcuts, the CardputerZero's web config on port 18765 and its use of the system Wi-Fi. It also explains the Append date checkbox in Export Config and which boards can pair a Bluetooth keyboard.

Pull request: #157
