### Changed
- Notification sounds are now the same on every board with a speaker: the Pager's Default, Chirpy and Bass melodies. T-Deck and Cardputer users will hear lower, longer sounds than before, and Bass may be quiet on their small speakers. Boards with a buzzer (Heltec V4/R8, M9, CrowPanel 5.0 V1.0) keep their own melodies.
- Previewing a sound in the Notification Sound picker now only plays the sound. It no longer blinks the LED or keyboard, buzzes the motor, or restarts the light reminder.

### Fixed
- On the T-Deck, the pauses between notes were lost, so the notes ran together. This also affected the startup melody.
- The last note of an alert was cut short on the Pager, Wio Tracker L2, T-Display P4, Tanmatsu and T-Deck.

### For contributors
- Pull requests now have a "Contribution guidelines" check. Every commit must be signed off (`git commit -s`), new source files need an SPDX license line, and `platformio.ini` dependencies must be pinned to exact versions. Missing translations are reported but never fail the check.

Pull request: #151
