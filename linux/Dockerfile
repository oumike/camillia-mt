# Build image for the CardputerZero (Debian 13 trixie, aarch64). Same release
# as the device, so the binary links against the glibc it will run on.
FROM debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake git ca-certificates libmbedtls-dev libsdl2-dev pkg-config file \
 && rm -rf /var/lib/apt/lists/*
