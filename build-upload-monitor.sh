#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ENV_NAME="tdeck"
DEBUG_ENV_NAME="tdeck-debug"
CARDPUTER_ENV_NAME="cardputer-cap"
HELTEC_ENV_NAME="heltec-v4"
HELTEC_VERTICAL_ENV_NAME="heltec-v4-vertical"
TLORA_ENV_NAME="tlora-pager-tft"
ENV_EXPLICIT=false
ERASE_FIRST=false
FULLCLEAN=false

has_env() {
	local env_name="$1"
	grep -q "^\[env:${env_name}\]" platformio.ini
}

select_env_or_exit() {
	local env_name="$1"
	local not_found_msg="$2"
	local extra_msg="${3:-}"

	if has_env "$env_name"; then
		ENV_NAME="$env_name"
		ENV_EXPLICIT=true
		return
	fi

	echo "$not_found_msg"
	if [ -n "$extra_msg" ]; then
		echo "$extra_msg"
	fi
	exit 1
}

prompt_for_device() {
	local options=()
	local labels=()

	if has_env "tdeck"; then
		options+=("tdeck")
		labels+=("LilyGo T-Deck")
	fi
	if has_env "$CARDPUTER_ENV_NAME"; then
		options+=("$CARDPUTER_ENV_NAME")
		labels+=("M5Stack Cardputer + Cap LoRa/GPS")
	fi
	if has_env "$TLORA_ENV_NAME"; then
		options+=("$TLORA_ENV_NAME")
		labels+=("LilyGo T-Lora Pager TFT")
	fi
	if has_env "$HELTEC_ENV_NAME"; then
		options+=("$HELTEC_ENV_NAME")
		labels+=("Heltec V4 Expansion Kit")
	fi
	if has_env "$HELTEC_VERTICAL_ENV_NAME"; then
		options+=("$HELTEC_VERTICAL_ENV_NAME")
		labels+=("Heltec V4 Expansion Kit (Vertical UI)")
	fi

	if [ "${#options[@]}" -eq 0 ]; then
		echo "No supported device environments found in platformio.ini"
		exit 1
	fi

	if [ ! -t 0 ]; then
		ENV_NAME="${options[0]}"
		echo "[PIO] Non-interactive shell detected, using device env: $ENV_NAME"
		return
	fi

	echo "Select device to build for:"
	for i in "${!options[@]}"; do
		printf "  %d) %s (%s)\n" "$((i + 1))" "${labels[$i]}" "${options[$i]}"
	done

	while true; do
		read -r -p "Enter choice [1-${#options[@]}]: " choice
		if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#options[@]}" ]; then
			ENV_NAME="${options[$((choice - 1))]}"
			echo "[PIO] Selected device env: $ENV_NAME"
			return
		fi
		echo "Invalid selection. Please choose a number between 1 and ${#options[@]}."
	done
}

show_usage() {
	echo "Usage: $0 [--tdeck|-t] [--debug|-d] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--erase|-E] [--fullclean|-F]"
	echo "  --tdeck, -t  Use T-Deck environment (tdeck)"
	echo "  --debug, -d   Use debug PlatformIO environment ($DEBUG_ENV_NAME)"
	echo "  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment ($CARDPUTER_ENV_NAME)"
	echo "  --pager, -P   Use T-Lora Pager TFT environment ($TLORA_ENV_NAME)"
	echo "  --heltec, -H  Use Heltec V4 expansion environment ($HELTEC_ENV_NAME)"
	echo "  --heltec-vertical, --vertical, -V  Use vertical Heltec env ($HELTEC_VERTICAL_ENV_NAME)"
	echo "                If neither is provided, you'll be prompted to choose a device."
	echo "  --erase, -E   Erase flash before clean build/upload"
	echo "  --fullclean, -F  Run PlatformIO fullclean before upload"
}

run_pio_target() {
	local target="$1"
	local label="$2"
	echo "[PIO] $label ($ENV_NAME)..."
	pio run -e "$ENV_NAME" -t "$target"
}

format_duration() {
	local total_seconds="$1"
	local hours=$((total_seconds / 3600))
	local minutes=$(((total_seconds % 3600) / 60))
	local seconds=$((total_seconds % 60))

	if [ "$hours" -gt 0 ]; then
		printf "%dh %02dm %02ds" "$hours" "$minutes" "$seconds"
	else
		printf "%dm %02ds" "$minutes" "$seconds"
	fi
}

if ! command -v pio >/dev/null 2>&1; then
	echo "PlatformIO CLI not found: install it or run from an environment that provides 'pio'."
	exit 1
fi

for arg in "$@"; do
	case "$arg" in
		--tdeck|-t)
			select_env_or_exit "tdeck" "Environment 'tdeck' not found in platformio.ini"
			;;
		--debug|-d)
			select_env_or_exit "$DEBUG_ENV_NAME" "Debug environment '$DEBUG_ENV_NAME' not found in platformio.ini" "Tip: add [env:$DEBUG_ENV_NAME] or run without --debug."
			;;
		--erase|-E)
			ERASE_FIRST=true
			;;
		--fullclean|-F)
			FULLCLEAN=true
			;;
		--cardputer|-C)
			select_env_or_exit "$CARDPUTER_ENV_NAME" "Environment '$CARDPUTER_ENV_NAME' not found in platformio.ini"
			;;
		--pager|-P)
			select_env_or_exit "$TLORA_ENV_NAME" "Environment '$TLORA_ENV_NAME' not found in platformio.ini"
			;;
		--heltec|-H)
			select_env_or_exit "$HELTEC_ENV_NAME" "Environment '$HELTEC_ENV_NAME' not found in platformio.ini"
			;;
		--heltec-vertical|--vertical|-V)
			select_env_or_exit "$HELTEC_VERTICAL_ENV_NAME" "Environment '$HELTEC_VERTICAL_ENV_NAME' not found in platformio.ini"
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

if [ "$ENV_EXPLICIT" = false ]; then
	prompt_for_device
fi

if [ "$ERASE_FIRST" = true ]; then
	run_pio_target "erase" "Erasing device flash"
fi

BUILD_START_TS="$(date +%s)"
if [ "$FULLCLEAN" = true ]; then
	run_pio_target "fullclean" "Full clean"
fi
run_pio_target "upload" "Upload"
BUILD_END_TS="$(date +%s)"
BUILD_ELAPSED_SECS=$((BUILD_END_TS - BUILD_START_TS))
echo "[PIO] Build completed in $(format_duration "$BUILD_ELAPSED_SECS")."

ELF_PATH=".pio/build/${ENV_NAME}/firmware.elf"
BIN_PATH=".pio/build/${ENV_NAME}/firmware.bin"
if [ -f "$ELF_PATH" ]; then
	ELF_SHA="$(shasum -a 256 "$ELF_PATH" | awk '{print $1}')"
	echo "[PIO] ELF SHA256: $ELF_SHA"
	echo "[PIO] Runtime monitor should show: ELF file SHA256: ${ELF_SHA:0:16}"
fi
if [ -f "$BIN_PATH" ]; then
	BIN_SHA="$(shasum -a 256 "$BIN_PATH" | awk '{print $1}')"
	echo "[PIO] BIN SHA256: $BIN_SHA"
fi

run_pio_target "monitor" "Monitor"
