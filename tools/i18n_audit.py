#!/usr/bin/env python3
"""Translation coverage for TR() (issue #99).

Collects every TR("...") and TR_NOOP("...") key in src/ and checks each lang/<code>.lang against
them:

  missing   keys in the source with no translation (they show in English)
  stale     translations whose key is no longer in the source
  long      translations more than 40% (and 6+ characters) longer than the
            English -- the likeliest to be cut off on a 240 px panel. A
            warning, not an error: Spanish runs 20-30% long as a rule
  bad       translations whose printf conversions differ from the key's --
            the device refuses these and shows English (see TR() in
            src/i18n.cpp), so they are listed as errors here

  python3 tools/i18n_audit.py            # summary per language
  python3 tools/i18n_audit.py --missing  # also print the missing keys
  python3 tools/i18n_audit.py --long     # also print the long ones
  python3 tools/i18n_audit.py --keys     # just the keys, one per line

Exit status is 1 when any translation is bad, so it can gate a build.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
LANG_DIR = os.path.join(ROOT, "lang")

# TR("...") and TR_NOOP("..."), the marker for keys in arrays that are
# translated where they are drawn. A key may be split over several adjacent
# literals ("line one\n" "line two"), which the compiler joins -- so do we.
TR_RE = re.compile(r'\bTR(?:_NOOP)?\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)\)')
PIECE_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SPEC_RE = re.compile(r'%(?:%|[-+ #0]*(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|j|z|t|L)?([a-zA-Z]))')


def c_unescape(s):
    # The escapes UI strings actually use. The source is UTF-8 and non-ASCII
    # text (emoji, accents) sits in it raw, so no wider decoding is wanted.
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"'}.get(s[i + 1], "\\" + s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def lang_unescape(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "t": "\t", "\\": "\\"}.get(s[i + 1], "\\" + s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def specs(s):
    out = []
    for m in SPEC_RE.finditer(s):
        if m.group(0) == "%%":
            continue
        if m.group(1) == "*":
            out.append("*")
        if m.group(2) == "*":
            out.append("*")
        out.append((m.group(3) or "") + m.group(4))
    return out


def source_keys():
    keys = set()
    for dirpath, _, files in os.walk(SRC):
        for name in files:
            if not name.endswith((".cpp", ".h", ".c")) or name == "i18n_builtin.h":
                continue
            with open(os.path.join(dirpath, name), encoding="utf-8", errors="replace") as fh:
                for m in TR_RE.finditer(fh.read()):
                    keys.add(c_unescape("".join(PIECE_RE.findall(m.group(1)))))
    return keys


def lang_pairs(path):
    pairs = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n").rstrip("\r")
            if not line or line.startswith("#") or "\t" not in line:
                continue
            k, v = line.split("\t", 1)
            k, v = lang_unescape(k), lang_unescape(v)
            if k and v:
                pairs[k] = v
    return pairs


def main():
    keys = source_keys()
    if "--keys" in sys.argv:
        for k in sorted(keys):
            print(k.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t"))
        return 0
    status = 0
    print(f"{len(keys)} TR() keys in src/")
    for name in sorted(os.listdir(LANG_DIR)):
        if not name.endswith(".lang"):
            continue
        pairs = lang_pairs(os.path.join(LANG_DIR, name))
        missing = sorted(k for k in keys if k not in pairs)
        stale = sorted(k for k in pairs if k not in keys)
        bad = sorted(k for k in pairs if k in keys and specs(k) != specs(pairs[k]))
        long_ = sorted(k for k in pairs if k in keys
                       and len(pairs[k]) > 1.4 * len(k) and len(pairs[k]) - len(k) >= 6)
        done = len(keys) - len(missing)
        pct = (100 * done // len(keys)) if keys else 100
        print(f"{name}: {done}/{len(keys)} translated ({pct}%), "
              f"{len(missing)} missing, {len(stale)} stale, {len(long_)} long, {len(bad)} bad")
        for k in bad:
            print(f"  BAD placeholders: {k!r}")
            status = 1
        if "--long" in sys.argv:
            for k in long_:
                print(f"  long: {k!r} -> {pairs[k]!r}")
        if "--missing" in sys.argv:
            for k in missing:
                print("  missing: " + k.replace("\n", "\\n"))
    return status


sys.exit(main())
