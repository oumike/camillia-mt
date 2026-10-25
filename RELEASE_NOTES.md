### New
- **Community presets on the device (all boards):** Config has a new **Community Preset** row under Preset. It lists the local-mesh setups the web flasher offers, such as ChiMesh, MichMesh, Freq51, BayMesh and Meshtastic Italia. Each sets the region, radio preset, frequency slot and full channel list the way the community publishes them. Before applying, it shows what will change and the community's own advice, and asks you to confirm. Setups your device can't run aren't listed, for example the ten-channel ones on the Cardputer, which has eight.
- **Community presets in web config (all boards except Cardputer):** the LoRa Radio section has a **Community Preset** picker, grouped by area. It shows each setup's region, radio, channel names, MQTT broker, notes and community link. Setups your device can't run are shown greyed out, with the reason. Choosing **Apply** asks you to confirm, then the device applies the setup and reboots.
- **Community MQTT brokers:** for the 20 communities that publish a broker anyone can use, a community preset also sets the MQTT server, port, login, root topic and encryption, and turns MQTT on. If a Wi-Fi network is saved, Wi-Fi is turned on too. Without one, MQTT starts once Wi-Fi is set up. Communities with per-user logins, a private broker or no published broker leave your MQTT settings unchanged.
- **Web flasher sets MQTT:** Flash and Customize in the web flasher can now set a community's MQTT broker on devices running this firmware. On older firmware it skips that step.

### Changed
- **Applying a community preset is a full switch to that mesh:** it **overwrites all of your channels**, **clears all channel and direct messages and every node that isn't a favorite**, and, for most communities, **replaces your MQTT settings and turns MQTT on**. The device then reboots.
- New on-screen text for community presets is in English only for now, in every language.

Pull request: #126
