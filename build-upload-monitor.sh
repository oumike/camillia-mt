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
TDECK_LVGL_ENV_NAME="tdeck-lvgl-poc"
TLORA_LVGL_ENV_NAME="tlora-pager-tft-lvgl-poc"
CARDPUTER_LVGL_ENV_NAME="cardputer-cap-lvgl-poc"
ENV_EXPLICIT=false
ERASE_FIRST=false

has_env() {
	local env_name="$1"
	grep -q "^\[env:${env_name}\]" platformio.ini
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
	if has_env "$TDECK_LVGL_ENV_NAME"; then
		options+=("$TDECK_LVGL_ENV_NAME")
		labels+=("LilyGo T-Deck (LVGL POC)")
	fi
	if has_env "$TLORA_LVGL_ENV_NAME"; then
		options+=("$TLORA_LVGL_ENV_NAME")
		labels+=("LilyGo T-Lora Pager TFT (LVGL POC)")
	fi
	if has_env "$CARDPUTER_LVGL_ENV_NAME"; then
		options+=("$CARDPUTER_LVGL_ENV_NAME")
		labels+=("M5Stack Cardputer + Cap LoRa/GPS (LVGL POC)")
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
	echo "Usage: $0 [--tdeck|-t] [--debug|-d] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--tdeck-lvgl] [--pager-lvgl] [--cardputer-lvgl] [--erase|-E]"
	echo "  --tdeck, -t  Use T-Deck environment (tdeck)"
	echo "  --debug, -d   Use debug PlatformIO environment ($DEBUG_ENV_NAME)"
	echo "  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment ($CARDPUTER_ENV_NAME)"
	echo "  --pager, -P   Use T-Lora Pager TFT environment ($TLORA_ENV_NAME)"
	echo "  --heltec, -H  Use Heltec V4 expansion environment ($HELTEC_ENV_NAME)"
	echo "  --heltec-vertical, --vertical, -V  Use vertical Heltec env ($HELTEC_VERTICAL_ENV_NAME)"
	echo "  --tdeck-lvgl       Use LVGL POC on T-Deck ($TDECK_LVGL_ENV_NAME)"
	echo "  --pager-lvgl       Use LVGL POC on T-Lora Pager TFT ($TLORA_LVGL_ENV_NAME)"
	echo "  --cardputer-lvgl   Use LVGL POC on Cardputer + Cap LoRa/GPS ($CARDPUTER_LVGL_ENV_NAME)"
	echo "                If neither is provided, you'll be prompted to choose a device."
	echo "  --erase, -E   Erase flash before clean build/upload"
}

for arg in "$@"; do
	case "$arg" in
		--tdeck|-t)
			if has_env "tdeck"; then
				ENV_NAME="tdeck"
				ENV_EXPLICIT=true
			else
				echo "Environment 'tdeck' not found in platformio.ini"
				exit 1
			fi
			;;
		--debug|-d)
			if has_env "$DEBUG_ENV_NAME"; then
				ENV_NAME="$DEBUG_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Debug environment '$DEBUG_ENV_NAME' not found in platformio.ini"
				echo "Tip: add [env:$DEBUG_ENV_NAME] or run without --debug."
				exit 1
			fi
			;;
		--erase|-E)
			ERASE_FIRST=true
			;;
		--cardputer|-C)
			if has_env "$CARDPUTER_ENV_NAME"; then
				ENV_NAME="$CARDPUTER_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$CARDPUTER_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
			;;
		--pager|-P)
			if has_env "$TLORA_ENV_NAME"; then
				ENV_NAME="$TLORA_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$TLORA_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
			;;
		--tdeck-lvgl)
			if has_env "$TDECK_LVGL_ENV_NAME"; then
				ENV_NAME="$TDECK_LVGL_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$TDECK_LVGL_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
			;;
		--pager-lvgl)
			if has_env "$TLORA_LVGL_ENV_NAME"; then
				ENV_NAME="$TLORA_LVGL_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$TLORA_LVGL_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
			;;
		--cardputer-lvgl)
			if has_env "$CARDPUTER_LVGL_ENV_NAME"; then
				ENV_NAME="$CARDPUTER_LVGL_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$CARDPUTER_LVGL_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
			;;
		--heltec|-H)
			if has_env "$HELTEC_ENV_NAME"; then
				ENV_NAME="$HELTEC_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$HELTEC_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
			;;
		--heltec-vertical|--vertical|-V)
			if has_env "$HELTEC_VERTICAL_ENV_NAME"; then
				ENV_NAME="$HELTEC_VERTICAL_ENV_NAME"
				ENV_EXPLICIT=true
			else
				echo "Environment '$HELTEC_VERTICAL_ENV_NAME' not found in platformio.ini"
				exit 1
			fi
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
	echo "[PIO] Erasing device flash..."
	pio run -e "$ENV_NAME" -t erase
fi

echo "[PIO] Full clean ($ENV_NAME)..."
pio run -e "$ENV_NAME" -t fullclean

echo "[PIO] Upload ($ENV_NAME)..."
pio run -e "$ENV_NAME" -t upload

echo "[PIO] Monitor ($ENV_NAME)..."
pio run -e "$ENV_NAME" -t monitor
