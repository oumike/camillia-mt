#!/bin/bash
# Flash Camillia-MT to a connected T-Deck.
# Usage: ./flash.sh <firmware.bin> [port]
# Default port: /dev/ttyUSB0 (Linux) — use /dev/cu.usbmodem* on macOS.

set -e

FIRMWARE=${1:-}
PORT=${2:-/dev/ttyUSB0}

if [[ -z "$FIRMWARE" ]]; then
    # Fall back to any matching binary in the current directory
    FIRMWARE=$(ls camillia-mt-*.bin 2>/dev/null | sort -V | tail -1)
fi

if [[ -z "$FIRMWARE" || ! -f "$FIRMWARE" ]]; then
    echo "Usage: ./flash.sh <camillia-mt-vX.Y.Z.bin> [port]"
    exit 1
fi

echo "Flashing $FIRMWARE to $PORT..."
esptool.py --chip esp32s3 --port "$PORT" write_flash 0x0 "$FIRMWARE"
