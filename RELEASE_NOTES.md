### Fixed
- Messages, positions and other traffic that other nodes send over MQTT with MQTT encryption turned off now reach your device. Before, these were silently dropped. Remote admin commands and direct messages sent this way are still ignored, because they can't be verified.
