#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ENV_NAME="tdeck"
DEBUG_ENV_NAME="tdeck-debug"
ERASE_FIRST=false

show_usage() {
	echo "Usage: $0 [--debug|-d] [--erase|-E]"
	echo "  --debug, -d   Use debug PlatformIO environment ($DEBUG_ENV_NAME)"
	echo "  --erase, -E   Erase flash before clean build/upload"
}

for arg in "$@"; do
	case "$arg" in
		--debug|-d)
			if grep -q "^\[env:${DEBUG_ENV_NAME}\]" platformio.ini; then
				ENV_NAME="$DEBUG_ENV_NAME"
			else
				echo "Debug environment '$DEBUG_ENV_NAME' not found in platformio.ini"
				echo "Tip: add [env:$DEBUG_ENV_NAME] or run without --debug."
				exit 1
			fi
			;;
		--erase|-E)
			ERASE_FIRST=true
			;;
		--help|-h)
			show_usage
			exit 0
			;;
		*)
			echo "Unknown argument: $arg"
			show_usage
			exit 1
			;;
	esac
done

if [ "$ERASE_FIRST" = true ]; then
	echo "[PIO] Erasing device flash..."
	pio run -e "$ENV_NAME" -t erase
fi

echo "[PIO] Full clean ($ENV_NAME)..."
pio run -e "$ENV_NAME" -t fullclean

echo "[PIO] Upload ($ENV_NAME)..."
pio run -e "$ENV_NAME" -t upload

echo "[PIO] Monitor ($ENV_NAME)..."
pio run -e "$ENV_NAME" -t monitor
