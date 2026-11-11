#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a pull request against the CONTRIBUTING.md rules a machine can check.

  python3 tools/pr_guidelines.py BASE HEAD

BASE and HEAD are commits (the PR's base and head); the checks look at what
BASE..HEAD adds. Errors fail the run (exit 1); warnings never do.

  sign-off      every non-merge commit carries a Signed-off-by line (error)
  spdx          every source file the PR adds starts with an
                SPDX-License-Identifier line, any license (error)
  pinning       every platform and lib_deps entry in platformio.ini names an
                exact version, commit, tag or release (error). The whole file
                is checked, not only the PR's lines
  translations  TR() strings the PR adds that a lang/*.lang file has no
                translation for (warning: translations are optional)

Run under GitHub Actions it also writes ::error::/::warning:: annotations and a
step summary.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import i18n_audit  # noqa: E402  (TR() key extraction, shared with the audit)

SOURCE_EXTS = (".c", ".cpp", ".h", ".hpp", ".py", ".sh", ".js")
# Generated or third-party trees whose files carry their own terms.
SPDX_EXEMPT_DIRS = ("src/fonts/",)
SPDX_RE = re.compile(r"SPDX-License-Identifier:\s*\S")
SPDX_LINES = 5

SHA_RE = re.compile(r"\b[0-9a-f]{40}\b")
EXACT_VERSION_RE = re.compile(r"^v?\d+(\.\d+)*([-+][0-9A-Za-z.\-]+)?$")


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], check=True,
                          capture_output=True, text=True).stdout


# ---- sign-off ---------------------------------------------------------------

def commits_missing_signoff(base, head):
    out = git("log", "--no-merges", "--format=%H%x1f%an%x1f%s%x1f%B%x1e",
              f"{base}..{head}")
    missing = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\n")
        if not rec:
            continue
        sha, author, subject, body = rec.split("\x1f", 3)
        if author.endswith("[bot]"):
            continue
        if not re.search(r"^Signed-off-by: .+ <[^>]+>\s*$", body, re.M):
            missing.append((sha[:10], subject))
    return missing


# ---- SPDX headers -----------------------------------------------------------

def needs_spdx(path):
    return path.endswith(SOURCE_EXTS) and not path.startswith(SPDX_EXEMPT_DIRS)


def has_spdx(text):
    return any(SPDX_RE.search(line) for line in text.splitlines()[:SPDX_LINES])


def added_files_missing_spdx(base, head):
    added = git("diff", "--name-only", "--diff-filter=A", f"{base}...{head}").split()
    missing = []
    for path in added:
        if needs_spdx(path) and not has_spdx(git("show", f"{head}:{path}")):
            missing.append(path)
    return missing


# ---- dependency pinning -----------------------------------------------------

def pin_problem(spec):
    """Why a platform/lib_deps entry is not pinned, or None when it is."""
    spec = spec.strip()
    if spec.startswith("${"):
        return None                      # a reference to another section
    if "=" in spec and "://" in spec.split("=", 1)[1]:
        spec = spec.split("=", 1)[1].strip()   # "Name=https://..." form
    if "://" in spec:
        if SHA_RE.search(spec):
            return None
        if "/releases/download/" in spec or "/archive/refs/tags/" in spec:
            return None
        if re.search(r"/archive/v?\d[^/]*\.(zip|tar\.gz)$", spec):
            return None
        m = re.search(r"#(.+)$", spec)
        if m and EXACT_VERSION_RE.match(m.group(1)):
            return None
        return "URL is not pinned to a commit, tag or release"
    if "@" not in spec:
        return "no version given"
    version = spec.rsplit("@", 1)[1].strip()
    if not EXACT_VERSION_RE.match(version):
        return f"version '{version}' is a range, not an exact version"
    return None


def ini_dependencies(text):
    """(line number, entry) for every platform and lib_deps value."""
    out, in_deps = [], False
    for n, raw in enumerate(text.splitlines(), 1):
        line = re.split(r"\s[;#]|^[;#]", raw, maxsplit=1)[0].rstrip()
        if not line.strip():
            continue
        key = re.match(r"^([A-Za-z_]+)\s*=\s*(.*)$", line)
        if key:
            in_deps = key.group(1) == "lib_deps"
            if key.group(1) == "platform" and key.group(2):
                out.append((n, key.group(2).strip()))
            elif in_deps and key.group(2):
                out.append((n, key.group(2).strip()))
            continue
        if line.startswith("["):
            in_deps = False
            continue
        if in_deps and line[0].isspace():
            out.append((n, line.strip()))
    return out


def unpinned_dependencies(text):
    return [(n, spec, why) for n, spec in ini_dependencies(text)
            if (why := pin_problem(spec))]


# ---- translations -----------------------------------------------------------

def keys_at(rev):
    keys = set()
    for path in git("ls-tree", "-r", "--name-only", rev, "src").split():
        if not path.endswith((".cpp", ".h", ".c")) or path.endswith("i18n_builtin.h"):
            continue
        text = git("show", f"{rev}:{path}")
        for m in i18n_audit.TR_RE.finditer(text):
            keys.add(i18n_audit.c_unescape("".join(i18n_audit.PIECE_RE.findall(m.group(1)))))
    return keys


def untranslated_new_keys(base, head):
    """{key: [language codes without it]} for TR() keys new in BASE..HEAD."""
    new = keys_at(head) - keys_at(base)
    if not new:
        return {}
    langs = {}
    lang_dir = os.path.join(ROOT, "lang")
    for name in sorted(os.listdir(lang_dir)):
        if name.endswith(".lang"):
            langs[name[:-5]] = i18n_audit.lang_pairs(os.path.join(lang_dir, name))
    out = {}
    for key in sorted(new):
        missing = [code for code, pairs in langs.items() if key not in pairs]
        if missing:
            out[key] = missing
    return out


# ---- reporting --------------------------------------------------------------

def show(key):
    return key.replace("\n", "\\n").replace("\t", "\\t")


def annotate(level, msg, path=None, line=None):
    if os.environ.get("GITHUB_ACTIONS") != "true":
        return
    loc = ""
    if path:
        loc = f" file={path}" + (f",line={line}" if line else "")
    msg = msg.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print(f"::{level}{loc}::{msg}")


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    base, head = argv[1], argv[2]
    errors, summary = 0, ["## Contribution guidelines", ""]

    signoff = commits_missing_signoff(base, head)
    print(f"sign-off: {len(signoff)} commit(s) without Signed-off-by")
    if signoff:
        errors += len(signoff)
        summary.append("**Sign-off:** these commits have no `Signed-off-by` line "
                       "(fix with `git rebase --signoff " + base[:10] + "`):")
        for sha, subject in signoff:
            print(f"  {sha} {subject}")
            summary.append(f"- `{sha}` {subject}")
            annotate("error", f"Commit {sha} ({subject}) has no Signed-off-by line. "
                     "Sign off with git commit -s; see CONTRIBUTING.md.")
    else:
        summary.append("**Sign-off:** every commit is signed off.")
    summary.append("")

    spdx = added_files_missing_spdx(base, head)
    print(f"spdx: {len(spdx)} new file(s) without an SPDX header")
    if spdx:
        errors += len(spdx)
        summary.append("**SPDX:** these new files have no "
                       "`SPDX-License-Identifier` line in their first "
                       f"{SPDX_LINES} lines:")
        for path in spdx:
            print(f"  {path}")
            summary.append(f"- `{path}`")
            annotate("error", "New file has no SPDX-License-Identifier header "
                     "(e.g. // SPDX-License-Identifier: GPL-3.0-or-later).",
                     path, 1)
    else:
        summary.append("**SPDX:** every new source file has a license header.")
    summary.append("")

    with open(os.path.join(ROOT, "platformio.ini"), encoding="utf-8") as fh:
        unpinned = unpinned_dependencies(fh.read())
    print(f"pinning: {len(unpinned)} unpinned dependenc(ies) in platformio.ini")
    if unpinned:
        errors += len(unpinned)
        summary.append("**Pinning:** these `platformio.ini` entries are not "
                       "pinned to an exact version:")
        for n, spec, why in unpinned:
            print(f"  line {n}: {spec} ({why})")
            summary.append(f"- line {n}: `{spec}` ({why})")
            annotate("error", f"{spec}: {why}. Pin an exact version or commit.",
                     "platformio.ini", n)
    else:
        summary.append("**Pinning:** every dependency is pinned.")
    summary.append("")

    untranslated = untranslated_new_keys(base, head)
    print(f"translations: {len(untranslated)} new string(s) missing a translation "
          "(optional)")
    if untranslated:
        summary.append("**Translations (optional):** new strings without a "
                       "translation show in English in these languages:")
        summary.append("")
        summary.append("| String | Missing |")
        summary.append("|---|---|")
        for key, codes in untranslated.items():
            print(f"  \"{show(key)}\": {' '.join(codes)}")
            cell = show(key).replace("|", "\\|")
            summary.append(f"| `{cell}` | {', '.join(codes)} |")
            annotate("warning", f"No translation for \"{show(key)}\" in: "
                     f"{', '.join(codes)}. Optional; it shows in English.")
    else:
        summary.append("**Translations:** every new string is translated.")
    summary.append("")

    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a", encoding="utf-8") as fh:
            fh.write("\n".join(summary) + "\n")

    print("FAILED" if errors else "OK")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
