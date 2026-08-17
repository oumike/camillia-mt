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
ATTAKY_ENV_NAME="mesh-deck"
M9_ENV_NAME="m9"
ENV_EXPLICIT=false
ERASE_FIRST=false
FULLCLEAN=false
JUST_BUILD=false

has_env() {
	local env_name="$1"
	grep -q "^\[env:${env_name}\]" platformio.ini
}

# Every [env:NAME] in platformio.ini, in file order. Read from the file rather
# than from the list of device flags below so a newly added environment is
# covered by --just-build the day it lands, without touching this script.
all_envs() {
	sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini
}

# Human label for an env, for the --just-build summary. Falls back to the env
# name itself, which is what any environment not in the device menu gets.
env_label() {
	case "$1" in
		tdeck)                     echo "LilyGo T-Deck" ;;
		"$DEBUG_ENV_NAME")         echo "LilyGo T-Deck (debug)" ;;
		"$CARDPUTER_ENV_NAME")     echo "M5Stack Cardputer + Cap LoRa/GPS" ;;
		"$TLORA_ENV_NAME")         echo "LilyGo T-Lora Pager TFT" ;;
		"$HELTEC_ENV_NAME")        echo "Heltec V4 Expansion Kit" ;;
		"$HELTEC_VERTICAL_ENV_NAME") echo "Heltec V4 Expansion Kit (Vertical UI)" ;;
		"$ATTAKY_ENV_NAME")        echo "Attaky Mesh Deck" ;;
		"$M9_ENV_NAME")            echo "Elecrow ThinkNode M9" ;;
		*)                         echo "$1" ;;
	esac
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
	if has_env "$ATTAKY_ENV_NAME"; then
		options+=("$ATTAKY_ENV_NAME")
		labels+=("Attaky Mesh Deck")
	fi
	if has_env "$M9_ENV_NAME"; then
		options+=("$M9_ENV_NAME")
		labels+=("Elecrow ThinkNode M9")
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
	echo "Usage: $0 [--tdeck|-t] [--debug|-d] [--cardputer|-C] [--pager|-P] [--heltec|-H] [--heltec-vertical|--vertical|-V] [--mesh-deck|-M] [--erase|-E] [--fullclean|-F] [--just-build|-B]"
	echo "  --tdeck, -t  Use T-Deck environment (tdeck)"
	echo "  --debug, -d   Use debug PlatformIO environment ($DEBUG_ENV_NAME)"
	echo "  --cardputer, -C  Use Cardputer + Cap LoRa/GPS environment ($CARDPUTER_ENV_NAME)"
	echo "  --pager, -P   Use T-Lora Pager TFT environment ($TLORA_ENV_NAME)"
	echo "  --heltec, -H  Use Heltec V4 expansion environment ($HELTEC_ENV_NAME)"
	echo "  --heltec-vertical, --vertical, -V  Use vertical Heltec env ($HELTEC_VERTICAL_ENV_NAME)"
	echo "  --mesh-deck, --attaky, -M  Use Attaky Mesh Deck environment ($ATTAKY_ENV_NAME)"
	echo "  --m9, -9      Use Elecrow ThinkNode M9 environment ($M9_ENV_NAME)"
	echo "                If neither is provided, you'll be prompted to choose a device."
	echo "  --erase, -E   Erase flash before clean build/upload"
	echo "                M9 uses the combined upload_erase target."
	echo "  --fullclean, -F  Run PlatformIO fullclean before upload"
	echo "  --just-build, -B  Compile only - no upload, no monitor, no device needed."
	echo "                Builds every environment in platformio.ini, or just the"
	echo "                one named by a device flag. Keeps going after a failure"
	echo "                and prints a pass/fail summary; exits non-zero if any"
	echo "                environment failed to build."
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
		--just-build|-B)
			JUST_BUILD=true
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
		--mesh-deck|--attaky|-M)
			select_env_or_exit "$ATTAKY_ENV_NAME" "Environment '$ATTAKY_ENV_NAME' not found in platformio.ini"
			;;
		--m9|-9)
			select_env_or_exit "$M9_ENV_NAME" "Environment '$M9_ENV_NAME' not found in platformio.ini"
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

if [ "$JUST_BUILD" = true ]; then
	if [ "$ERASE_FIRST" = true ]; then
		echo "--erase needs a connected device; it cannot be combined with --just-build."
		exit 1
	fi

	# A device flag narrows the sweep to that one environment: "--just-build -t"
	# is the natural way to compile-check the board you are working on without
	# a cable. With no device flag, every environment gets built.
	BUILD_ENVS=()
	if [ "$ENV_EXPLICIT" = true ]; then
		BUILD_ENVS+=("$ENV_NAME")
	else
		while IFS= read -r env; do
			[ -n "$env" ] && BUILD_ENVS+=("$env")
		done < <(all_envs)
	fi

	if [ "${#BUILD_ENVS[@]}" -eq 0 ]; then
		echo "No environments found in platformio.ini"
		exit 1
	fi

	echo "[PIO] Build-only sweep over ${#BUILD_ENVS[@]} environment(s)."
	SWEEP_START_TS="$(date +%s)"
	RESULTS=()
	FAILED=0

	for env in "${BUILD_ENVS[@]}"; do
		echo
		echo "===================================================================="
		echo "[PIO] Building $env ($(env_label "$env"))"
		echo "===================================================================="

		ENV_START_TS="$(date +%s)"
		if [ "$FULLCLEAN" = true ]; then
			echo "[PIO] Full clean ($env)..."
			# A clean failure is the env's failure: building on top of a
			# half-cleaned tree would report a result about the wrong sources.
			if ! pio run -e "$env" -t fullclean; then
				ENV_END_TS="$(date +%s)"
				RESULTS+=("FAIL  $env  (fullclean)  $(format_duration $((ENV_END_TS - ENV_START_TS)))")
				FAILED=$((FAILED + 1))
				continue
			fi
		fi

		# No -t: the default target is a plain build. Failures are collected
		# rather than aborting the sweep — the point of building everything is
		# to find out which ones are broken, not just the first.
		if pio run -e "$env"; then
			ENV_END_TS="$(date +%s)"
			SIZE_NOTE=""
			ENV_BIN=".pio/build/${env}/firmware.bin"
			if [ -f "$ENV_BIN" ]; then
				SIZE_NOTE="  $(( $(wc -c <"$ENV_BIN") / 1024 )) KB"
			fi
			RESULTS+=("ok    $env  $(format_duration $((ENV_END_TS - ENV_START_TS)))${SIZE_NOTE}")
		else
			ENV_END_TS="$(date +%s)"
			RESULTS+=("FAIL  $env  $(format_duration $((ENV_END_TS - ENV_START_TS)))")
			FAILED=$((FAILED + 1))
		fi
	done

	SWEEP_END_TS="$(date +%s)"
	echo
	echo "===================================================================="
	echo "[PIO] Build summary"
	echo "===================================================================="
	for line in "${RESULTS[@]}"; do
		echo "  $line"
	done
	echo
	echo "[PIO] ${#BUILD_ENVS[@]} environment(s), $FAILED failed, total $(format_duration $((SWEEP_END_TS - SWEEP_START_TS)))."

	if [ "$FAILED" -gt 0 ]; then
		exit 1
	fi
	exit 0
fi

if [ "$ENV_EXPLICIT" = false ]; then
	prompt_for_device
fi

UPLOAD_TARGET="upload"
UPLOAD_LABEL="Upload"
if [ "$ERASE_FIRST" = true ]; then
	if [ "$ENV_NAME" = "$M9_ENV_NAME" ]; then
		UPLOAD_TARGET="upload_erase"
		UPLOAD_LABEL="Erase flash and upload"
	else
		run_pio_target "erase" "Erasing device flash"
	fi
fi

BUILD_START_TS="$(date +%s)"
if [ "$FULLCLEAN" = true ]; then
	run_pio_target "fullclean" "Full clean"
fi
run_pio_target "$UPLOAD_TARGET" "$UPLOAD_LABEL"
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
