#!/usr/bin/env python3
"""Reports how much of each environment's app slot the firmware uses.

Exists because this project has twice had a board block a release by running
out of flash, and both times it was discovered by the release failing rather
than by anyone watching the number climb (see issue #76, and #72 before it).
A build that passes tells you nothing about how close it came.

Reads the .bin PlatformIO already produced and the partition table the
environment is built against, so it measures the artifact that ships rather
than re-deriving anything.

    python3 tools/flash_headroom.py                 # every built environment
    python3 tools/flash_headroom.py --min-free 65536  # non-zero exit under that

--min-free is the lever for CI: set it to the point where a board is too tight
to absorb an ordinary feature, and the build starts failing there instead of in
a release.
"""

import argparse
import configparser
import csv
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def app_slot_bytes(partitions_csv: str) -> int:
    """Size of the smallest app partition in the table.

    The smallest, not the first: an OTA layout has two app slots and the image
    has to fit whichever is smaller, so that is the real ceiling.
    """
    path = os.path.join(ROOT, partitions_csv)
    smallest = None
    with open(path, newline="", encoding="utf-8") as fh:
        for row in csv.reader(fh):
            if not row or row[0].strip().startswith("#"):
                continue
            cells = [c.strip() for c in row]
            if len(cells) < 5 or cells[1] != "app":
                continue
            size = cells[4]
            value = int(size, 16) if size.lower().startswith("0x") else int(size)
            smallest = value if smallest is None else min(smallest, value)
    if smallest is None:
        raise ValueError(f"no app partition found in {partitions_csv}")
    return smallest


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-free", type=int, default=0,
                    help="fail if any built environment has less free than this")
    args = ap.parse_args()

    ini = configparser.ConfigParser(inline_comment_prefixes=(";",))
    ini.read(os.path.join(ROOT, "platformio.ini"))

    rows = []
    for section in ini.sections():
        if not section.startswith("env:"):
            continue
        env = section[4:]
        binary = os.path.join(ROOT, ".pio", "build", env, "firmware.bin")
        if not os.path.isfile(binary):
            continue    # not built in this job; silence is correct, not an error

        # extends= environments inherit the partition table from their parent.
        table, hop = None, section
        while hop and table is None:
            table = ini.get(hop, "board_build.partitions", fallback=None)
            parent = ini.get(hop, "extends", fallback=None)
            hop = parent if parent else None
        if not table:
            continue

        slot = app_slot_bytes(table)
        used = os.path.getsize(binary)
        rows.append((env, used, slot, slot - used, 100.0 * used / slot))

    if not rows:
        print("No built environments found under .pio/build.")
        return 0

    rows.sort(key=lambda r: r[3])          # tightest first: that is the news
    print(f"{'environment':<20} {'used':>10} {'slot':>10} {'free':>10}  pct")
    failed = []
    for env, used, slot, free, pct in rows:
        flag = ""
        if args.min_free and free < args.min_free:
            flag = "  <-- BELOW THRESHOLD"
            failed.append(env)
        print(f"{env:<20} {used:>10} {slot:>10} {free:>10}  {pct:5.1f}%{flag}")

    if failed:
        print(f"\nBelow the {args.min_free}-byte floor: {', '.join(failed)}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
