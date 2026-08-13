// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.
// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.
#pragma once

// Release notes for the build this firmware was cut from, shown by the
// Release Notes entry in the config screen. Empty when no notes were
// available at build time (a plain dev build, typically).
static const char RELEASE_NOTES_TEXT[] = R"CAMNOTES(New
- Browser screen mirror (**VNC Host**) now works on the T-Lora Pager, Heltec V4 and Mesh Deck, not just the T-Deck - turn it on from the Config screen or the **Remote** tab in Web Config while the device is on a Wi-Fi network. The Cardputer is the one board without it.
- The browser viewer now sizes itself to whichever screen it connects to: 480x222 on the Pager, 240x320 on the vertical Heltec build, 320x240 elsewhere.
- Heltec V4 has no keyboard of its own, so keystrokes typed in the browser mirror are fed into the device as though one were attached.
- Tap anywhere in the message box to move the cursor there and edit mid-message (T-Deck, T-Lora Pager).
- T-Lora Pager: the scroll wheel now steps the cursor left and right while you are typing a message.

Fixed
- The message box now draws a blinking cursor as soon as it opens, so it is clear where the next character will land.
- Heltec V4: the Notification Sound setting now appears in Web Config, so the alert beep can be turned off from the browser and not just from the device's Config screen.)CAMNOTES";
