"""Bakes RELEASE_NOTES.md into src/release_notes.h for the on-device viewer.

release.sh writes RELEASE_NOTES.md from the AI summary before it builds, so the
notes carried inside a firmware image are the same text published on the GitHub
release. Running as a pre: script means an edited RELEASE_NOTES.md needs no
separate step, and the header is only rewritten when the content actually
changes so a normal rebuild isn't forced every time.

Reads:  RELEASE_NOTES.md   (absent or empty is fine — the viewer says so)
Writes: src/release_notes.h
"""

import os
import re

Import("env")  # noqa: F821 — injected by PlatformIO

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
SRC_PATH = os.path.join(PROJECT_DIR, "RELEASE_NOTES.md")
DST_PATH = os.path.join(PROJECT_DIR, "src", "release_notes.h")

# The whole thing sits in flash and renders into a single label, so it is capped
# rather than left to grow with an unusually chatty release.
MAX_BYTES = 4096

# Raw-string delimiter. Anything containing the closing sequence would end the
# literal early, so that sequence is neutralised below.
DELIM = "CAMNOTES"

# The compiled Montserrat faces are ASCII-only, so smart punctuation would
# render as blank boxes on the device. Fold the characters an LLM actually
# reaches for; anything else non-ASCII is dropped by the filter further down.
UNICODE_FOLD = {
    "—": "-",   # em dash
    "–": "-",   # en dash
    "‘": "'",   # left single quote
    "’": "'",   # right single quote
    "“": '"',   # left double quote
    "”": '"',   # right double quote
    "…": "...",
    " ": " ",   # non-breaking space
    "•": "-",   # bullet
    "→": "->",
}


def load_notes():
    if not os.path.isfile(SRC_PATH):
        return ""
    with open(SRC_PATH, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def clean(text):
    text = text.replace("\r\n", "\n").replace("\r", "\n")

    for bad, good in UNICODE_FOLD.items():
        text = text.replace(bad, good)

    out_lines = []
    for line in text.split("\n"):
        # '### New' reads as noise on a 240px panel; keep the heading text and
        # drop the markdown marker. Bullets and everything else stay verbatim.
        line = re.sub(r"^\s*#{1,6}\s*", "", line)
        # Drop anything still outside ASCII rather than shipping tofu boxes.
        line = "".join(ch for ch in line if ch == "\t" or 0x20 <= ord(ch) < 0x7F)
        out_lines.append(line.rstrip())

    text = "\n".join(out_lines).strip("\n")

    # Collapse runs of blank lines left behind by stripped headings.
    text = re.sub(r"\n{3,}", "\n\n", text)

    # Would otherwise terminate the raw string literal early.
    text = text.replace(")" + DELIM + '"', ")" + DELIM.lower() + '"')

    encoded = text.encode("utf-8")
    if len(encoded) > MAX_BYTES:
        clipped = encoded[:MAX_BYTES].decode("utf-8", errors="ignore")
        # Prefer cutting at a line break so the last entry isn't half a sentence.
        nl = clipped.rfind("\n")
        if nl > MAX_BYTES // 2:
            clipped = clipped[:nl]
        text = clipped.rstrip() + "\n\n[truncated]"

    return text


def render(text):
    return (
        "// AUTO-GENERATED from RELEASE_NOTES.md by tools/gen_release_notes.py.\n"
        "// Do not edit by hand — edit RELEASE_NOTES.md and rebuild.\n"
        "#pragma once\n"
        "\n"
        "// Release notes for the build this firmware was cut from, shown by the\n"
        "// Release Notes entry in the config screen. Empty when no notes were\n"
        "// available at build time (a plain dev build, typically).\n"
        "static const char RELEASE_NOTES_TEXT[] = R\"%s(%s)%s\";\n" % (DELIM, text, DELIM)
    )


def main():
    generated = render(clean(load_notes()))

    existing = None
    if os.path.isfile(DST_PATH):
        with open(DST_PATH, "r", encoding="utf-8") as handle:
            existing = handle.read()

    if existing == generated:
        return

    with open(DST_PATH, "w", encoding="utf-8") as handle:
        handle.write(generated)
    print("gen_release_notes: wrote %s" % os.path.relpath(DST_PATH, PROJECT_DIR))


main()
