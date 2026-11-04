### Fixed
- Answering a traceroute when the request arrived with a negative SNR (a weak signal) could corrupt memory and crash or destabilize the device. All boards.
- Building outgoing messages, node info, positions, telemetry, neighbor info, routing acknowledgements, traceroutes and position requests could write past the end of the packet buffer. Each one now checks the space before writing. All boards.
