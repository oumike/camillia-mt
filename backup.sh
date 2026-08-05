#!/bin/bash
# Take a complete backup of every connected Camillia-MT device, or put one back.
#
# "Complete" means the whole flash, byte for byte: bootloader, partition table,
# both OTA slots, NVS (settings + the node's Curve25519 identity) and coredump.
# Restoring flash-full.bin at 0x0 reproduces the device exactly as it was,
# including its node id and peers' trust in its public key.
#
# Usage:
#   ./backup.sh                          # back up every board it can find
#   ./backup.sh --port PORT              # just this one
#   ./backup.sh [--baud N] [--out DIR] [--no-full]
#   ./backup.sh --restore backups/<dir> --port PORT [--force]
#   ./backup.sh --list
#
# With no --port every serial device is tried and the ones that answer as an ESP
# are backed up, each into its own backups/<mac>-<timestamp>/ directory. Serial
# ports that are not ESP boards — Bluetooth links, a TNC, a USB headset — are
# skipped with a note rather than treated as failures.
#
# Each backup directory contains:
#   flash-full.bin    the entire flash (the thing to restore)
#   partitions.csv    the partition table as it exists ON THE DEVICE
#   partition-table.bin
#   nvs.bin, otadata.bin, coredump.bin, ...   each data partition on its own
#   MANIFEST.txt      chip, MAC, flash size, baud, sizes, sha256
#   RESTORE.md        the exact commands to put any of it back
#
# The per-partition dumps are the reason this reads the partition table off the
# device rather than trusting repo partitions.csv: a third-party installer (the
# Launcher images, notably) writes our app into a slot of *its* table, so nvs can
# live at a different offset and be a different size than this repo says. Backing
# up the wrong offset would produce a file that restores garbage over settings.
#
# Requires esptool. Reads are non-destructive — nothing is written to the device
# unless --restore is given, which asks for confirmation first.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PORT=""
BAUD_REQUESTED=921600
BAUD_EXPLICIT=0
OUT_ROOT="backups"
RESTORE_DIR=""
DUMP_FULL=1
FORCE=0
LIST=0

usage() {
    sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port|-p)    PORT="$2"; shift 2 ;;
        --baud|-b)    BAUD_REQUESTED="$2"; BAUD_EXPLICIT=1; shift 2 ;;
        --out|-o)     OUT_ROOT="$2"; shift 2 ;;
        --restore)    RESTORE_DIR="$2"; shift 2 ;;
        --no-full)    DUMP_FULL=0; shift ;;
        --force)      FORCE=1; shift ;;
        --list|-l)    LIST=1; shift ;;
        --help|-h)    usage 0 ;;
        *)            echo "Unknown argument: $1" >&2; usage 1 ;;
    esac
done

if [[ "$LIST" == "1" ]]; then
    if [[ ! -d "$OUT_ROOT" ]]; then
        echo "No backups yet ($OUT_ROOT does not exist)."
        exit 0
    fi
    for d in "$OUT_ROOT"/*/; do
        [[ -f "$d/MANIFEST.txt" ]] || continue
        printf '%s\n' "${d%/}"
        grep -E 'MAC|Chip|Flash size|Taken' "$d/MANIFEST.txt" | sed 's/^/    /' || true
    done
    exit 0
fi

# ── esptool ──────────────────────────────────────────────────────────────────
# PlatformIO ships one in its venv; prefer whatever is on PATH so a system
# install (possibly newer) wins if the user has one.
ESPTOOL=""
for candidate in esptool.py esptool "$HOME/.platformio/penv/bin/esptool.py"; do
    if command -v "$candidate" >/dev/null 2>&1; then ESPTOOL="$candidate"; break; fi
    if [[ -x "$candidate" ]]; then ESPTOOL="$candidate"; break; fi
done
if [[ -z "$ESPTOOL" ]]; then
    echo "esptool not found. Install it, or run: pip install esptool" >&2
    exit 1
fi
ESPTOOL_VER="$("$ESPTOOL" version 2>/dev/null | head -1 || echo unknown)"

LOG="$(mktemp -t camillia-backup)"
trap 'rm -f "$LOG"' EXIT

# --chip auto rather than a hardcoded esp32s3: every board in this repo is an
# S3 today, but "any device I have connected" is the point of the tool, and
# esptool identifies the part perfectly well on its own.
esp() {
    "$ESPTOOL" --chip auto --port "$1" --baud "$2" \
        --before default_reset --after no_reset "${@:3}"
}

# ── candidate ports ──────────────────────────────────────────────────────────
# Plain globs rather than mapfile: /bin/bash on macOS is 3.2, which has no
# mapfile. A glob that matches nothing expands to the pattern itself, so each
# candidate is tested for existence.
#
# Only usbmodem/usbserial/ttyUSB/ttyACM are considered. The Bluetooth serial
# ports and audio devices that also live in /dev/cu.* cannot be ESP boards, and
# probing them wastes ten seconds each on a timeout.
candidate_ports() {
    for dev in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        printf '%s\n' "$dev"
    done
}

# ── restore ──────────────────────────────────────────────────────────────────
if [[ -n "$RESTORE_DIR" ]]; then
    IMG="$RESTORE_DIR/flash-full.bin"
    [[ -f "$IMG" ]] || { echo "No flash-full.bin in $RESTORE_DIR" >&2; exit 1; }

    if [[ -z "$PORT" ]]; then
        # Deliberately not auto-picking here: writing to the wrong board of
        # several is unrecoverable, and restoring an identity onto the wrong
        # device is worse than a failed command.
        echo "--restore needs an explicit --port. Connected candidates:" >&2
        candidate_ports | sed 's/^/  /' >&2
        exit 1
    fi

    SAVED_SIZE="$(grep -E '^Flash size:' "$RESTORE_DIR/MANIFEST.txt" 2>/dev/null | awk '{print $3}' || true)"
    SAVED_BAUD="$(grep -E '^Baud:' "$RESTORE_DIR/MANIFEST.txt" 2>/dev/null | awk '{print $2}' || true)"
    [[ -n "$SAVED_BAUD" ]] || SAVED_BAUD=115200
    LIVE_SIZE="$(esp "$PORT" "$SAVED_BAUD" flash_id 2>/dev/null | grep -i 'Detected flash size' | awk -F': ' '{print $2}' | tr -d ' ' || true)"
    if [[ -n "$SAVED_SIZE" && -n "$LIVE_SIZE" && "$SAVED_SIZE" != "$LIVE_SIZE" && "$FORCE" != "1" ]]; then
        echo "Refusing: backup is from a ${SAVED_SIZE} device, this one is ${LIVE_SIZE}." >&2
        echo "A full-image write across different flash sizes lands partitions in the" >&2
        echo "wrong place. Pass --force only if you know why that is fine here." >&2
        exit 1
    fi

    echo "About to overwrite ALL of $PORT with $IMG"
    echo "This replaces settings, node identity and both OTA slots."
    read -r -p "Type RESTORE to continue: " answer
    [[ "$answer" == "RESTORE" ]] || { echo "Aborted."; exit 1; }

    "$ESPTOOL" --chip auto --port "$PORT" --baud "$SAVED_BAUD" \
        --before default_reset --after hard_reset \
        write_flash -z 0x0 "$IMG"
    echo "Restored. The device is running the backed-up image."
    exit 0
fi

# ── one device ───────────────────────────────────────────────────────────────
# Returns 0 on a completed backup, 1 on a real failure, 2 when the port simply
# is not an ESP board (so a sweep can skip it quietly).
backup_device() {
    local port="$1"
    local explicit="$2"        # 1 when the user named this port
    local baud tries connected chip mac mac_slug flash_size flash_bytes out est

    # Find a rate the link survives, rather than assuming the 921600 flash.sh
    # uses. That works over the native-USB boards, but a board whose console
    # goes through a USB-UART bridge (the Mesh Deck) fails right after esptool's
    # "Changing baud rate" step: the stub is already running, so the chip is
    # fine and only the link speed is wrong. It surfaces as "Unable to verify
    # flash chip connection", which reads like a hardware fault and is not one.
    #
    # Each board gets its own ladder — two boards on one machine can want
    # different rates. An explicit --baud is honoured as given.
    if [[ "$BAUD_EXPLICIT" == "1" ]]; then
        tries=("$BAUD_REQUESTED")
    else
        tries=(921600 460800 230400 115200)
    fi

    connected=0
    for baud in "${tries[@]}"; do
        if esp "$port" "$baud" flash_id >"$LOG" 2>&1; then
            connected=1
            break
        fi
        # Only a rate problem is worth retrying: if nothing identified itself as
        # a chip, this port is not an ESP and slower will not help.
        grep -qi '^Chip is' "$LOG" || break
        [[ "$explicit" == "1" ]] && echo "  ${baud} baud failed, trying slower ..." >&2
    done

    if [[ "$connected" != "1" ]]; then
        if [[ "$explicit" != "1" ]] && ! grep -qi '^Chip is' "$LOG"; then
            echo "  skipping $port (nothing answered as an ESP)"
            return 2
        fi
        echo "" >&2
        echo "esptool could not read the device on $port:" >&2
        sed 's/^/    /' "$LOG" >&2
        echo "" >&2
        echo "Common causes:" >&2
        echo "  * something else holds the port — a serial monitor (pio device monitor," >&2
        echo "    screen, the IDE's monitor) will block esptool" >&2
        echo "  * the board needs manual download mode: hold BOOT, tap RESET, release BOOT" >&2
        echo "  * a bad cable — a charge-only lead enumerates nothing at all, but a" >&2
        echo "    marginal one connects and then drops the transfer" >&2
        return 1
    fi

    chip="$(grep -i '^Chip is' "$LOG" | head -1 | sed 's/^[Cc]hip is //' || true)"
    mac="$(grep -iE '^MAC:' "$LOG" | head -1 | awk '{print $2}' || true)"
    mac_slug="$(printf '%s' "${mac:-unknown}" | tr -d ':' | tr 'A-Z' 'a-z')"
    flash_size="$(grep -i 'Detected flash size' "$LOG" | head -1 | awk -F': ' '{print $2}' | tr -d ' ' || true)"

    if [[ -z "$flash_size" ]]; then
        echo "esptool connected to $port but reported no flash size. Full output:" >&2
        sed 's/^/    /' "$LOG" >&2
        return 1
    fi
    case "$flash_size" in
        1MB)  flash_bytes=$((1*1024*1024)) ;;
        2MB)  flash_bytes=$((2*1024*1024)) ;;
        4MB)  flash_bytes=$((4*1024*1024)) ;;
        8MB)  flash_bytes=$((8*1024*1024)) ;;
        16MB) flash_bytes=$((16*1024*1024)) ;;
        32MB) flash_bytes=$((32*1024*1024)) ;;
        *)    echo "Unrecognized flash size '$flash_size' on $port." >&2; return 1 ;;
    esac

    out="$OUT_ROOT/$mac_slug-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$out"
    echo "  ${chip:-ESP}, MAC ${mac:-unknown}, $flash_size, $baud baud"
    echo "  -> $out"

    # 0x8000 is fixed by the ROM bootloader. One 4 KB sector covers the table
    # (entries are 32 bytes and the format caps at 95 of them).
    esp "$port" "$baud" read_flash 0x8000 0x1000 "$out/partition-table.bin" >/dev/null
    python3 - "$out/partition-table.bin" "$out/partitions.csv" <<'PY'
# Parse the on-device table rather than shelling out to gen_esp32part.py, whose
# path moves with the Arduino core version. The format is stable and trivial:
# 32-byte entries, 0xAA50 magic, until a non-magic entry.
import struct, sys

TYPES = {0: "app", 1: "data"}
APP_SUB = {0x00: "factory", 0x10: "ota_0", 0x11: "ota_1", 0x12: "ota_2",
           0x13: "ota_3", 0x20: "test"}
DATA_SUB = {0x00: "ota", 0x01: "phy", 0x02: "nvs", 0x03: "coredump",
            0x04: "nvs_keys", 0x05: "efuse", 0x80: "esphttpd",
            0x81: "fat", 0x82: "spiffs", 0x83: "littlefs"}

blob = open(sys.argv[1], "rb").read()
rows = []
for off in range(0, len(blob), 32):
    entry = blob[off:off + 32]
    if len(entry) < 32 or entry[:2] != b"\xaa\x50":
        break
    ptype, subtype, p_off, p_size = struct.unpack("<BBII", entry[2:12])
    label = entry[12:28].rstrip(b"\x00").decode("utf-8", "replace")
    tname = TYPES.get(ptype, f"0x{ptype:02x}")
    sname = (APP_SUB if ptype == 0 else DATA_SUB).get(subtype, f"0x{subtype:02x}")
    rows.append((label, tname, sname, p_off, p_size))

with open(sys.argv[2], "w") as fh:
    fh.write("# Name,Type,SubType,Offset,Size\n")
    for label, tname, sname, p_off, p_size in rows:
        fh.write(f"{label},{tname},{sname},0x{p_off:x},0x{p_size:x}\n")
PY
    sed 's/^/    /' "$out/partitions.csv"

    if [[ "$DUMP_FULL" == "1" ]]; then
        # Rough estimate at 10 bits per byte on the wire. Worth printing: the
        # difference between rates is minutes vs. half an hour, and a silent 25
        # minute wait looks like a hang.
        est=$(( flash_bytes / (baud / 10) ))
        printf '  dumping %s of flash (~%d min) ... ' "$flash_size" $(( (est + 59) / 60 ))
        esp "$port" "$baud" read_flash 0x0 "$flash_bytes" "$out/flash-full.bin" >/dev/null
        echo "done"
    fi

    # App partitions are skipped: they are megabytes each and already inside
    # flash-full.bin. The data ones are small and are what you actually want on
    # their own — nvs.bin alone restores settings and node identity without
    # touching firmware.
    local label tname sname p_off p_size
    while IFS=, read -r label tname sname p_off p_size; do
        [[ "$label" == \#* || -z "$label" ]] && continue
        [[ "$tname" == "data" ]] || continue
        printf '  dumping %-10s %s @ %s ... ' "$label" "$p_size" "$p_off"
        esp "$port" "$baud" read_flash "$p_off" "$p_size" "$out/$label.bin" >/dev/null
        echo "done"
    done < "$out/partitions.csv"

    {
        echo "Taken:      $(date '+%Y-%m-%d %H:%M:%S %Z')"
        echo "Chip:       ${chip:-unknown}"
        echo "MAC:        ${mac:-unknown}"
        echo "Port:       $port"
        echo "Flash size: $flash_size"
        echo "Baud:       $baud"
        echo "esptool:    $ESPTOOL_VER"
        echo "Repo:       $(git rev-parse --short HEAD 2>/dev/null || echo n/a) ($(cat VERSION 2>/dev/null || echo '?'))"
        echo ""
        echo "Files:"
        local f
        for f in "$out"/*.bin; do
            [[ -e "$f" ]] || continue
            printf '  %-24s %10s bytes  %s\n' "$(basename "$f")" \
                "$(wc -c < "$f" | tr -d ' ')" "$(sha_of "$f")"
        done
    } > "$out/MANIFEST.txt"

    write_restore_doc "$out" "$baud"

    # Every read above runs with --after no_reset so the stub survives between
    # calls; without this the board is left halted in the stub when the backup
    # ends, looking bricked until it is power-cycled by hand.
    "$ESPTOOL" --chip auto --port "$port" --baud 115200 \
        --before no_reset --after hard_reset read_mac >/dev/null 2>&1 || true

    return 0
}

sha_of() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

write_restore_doc() {
    local out="$1" baud="$2"
    local per_partition=""
    local label tname sname p_off p_size
    while IFS=, read -r label tname sname p_off p_size; do
        [[ "$label" == \#* || -z "$label" ]] && continue
        [[ "$tname" == "data" ]] || continue
        per_partition+="    esptool.py --chip auto --port <PORT> write_flash $p_off $label.bin"$'\n'
    done < "$out/partitions.csv"

    cat > "$out/RESTORE.md" <<EOF
# Restore

Everything below writes to a device. Nothing here is reversible except by
another backup, so take one of the target device first if it holds anything.

## The whole device, exactly as it was

    ./backup.sh --restore $out --port <PORT>

or by hand:

    esptool.py --chip auto --port <PORT> --baud $baud \\
        --before default_reset --after hard_reset \\
        write_flash -z 0x0 flash-full.bin

This restores firmware, both OTA slots, settings and the node's Curve25519
identity — peers keep trusting the node because its public key is unchanged.

## Settings and identity only, keeping current firmware

Use the offsets from partitions.csv in this directory, not from the repo: this
device's table is what these files were read against.

$per_partition
## Notes

- $baud baud is what this device's link tolerated when the backup was taken.
  A higher rate may fail outright on a USB-UART bridge.
- A device that has been OTA'd boots from app1 while a plain \`pio run -t upload\`
  writes app0, so after restoring, check which slot is actually running before
  concluding a later change did not take.
- Restoring nvs.bin onto a *different* physical device gives two nodes the same
  identity. Don't.
EOF
}

# ── sweep ────────────────────────────────────────────────────────────────────
PORTS=()
if [[ -n "$PORT" ]]; then
    PORTS=("$PORT")
    EXPLICIT=1
else
    while IFS= read -r line; do
        [[ -n "$line" ]] && PORTS+=("$line")
    done < <(candidate_ports)
    EXPLICIT=0
fi

if [[ ${#PORTS[@]} -eq 0 ]]; then
    echo "No serial device found. Plug a board in, or pass --port." >&2
    exit 1
fi

echo "Checking ${#PORTS[@]} port(s) ..."
OK=0
SKIPPED=0
FAILED=0
for p in "${PORTS[@]}"; do
    echo ""
    echo "$p"
    # backup_device returns 2 for "not an ESP", which is not an error in a
    # sweep; set -e must not treat any of these as fatal, so the call is
    # guarded and the status inspected explicitly.
    set +e
    backup_device "$p" "$EXPLICIT"
    rc=$?
    set -e
    case "$rc" in
        0) OK=$((OK + 1)) ;;
        2) SKIPPED=$((SKIPPED + 1)) ;;
        *) FAILED=$((FAILED + 1)) ;;
    esac
done

echo ""
echo "Backed up: $OK   skipped: $SKIPPED   failed: $FAILED"
[[ "$FAILED" -eq 0 ]]
