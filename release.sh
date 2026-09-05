#!/bin/bash
set -e

RELEASE_ENVS=(
    tdeck
    tdeck-pro
    tlora-pager-tft
    cardputer-cap
    heltec-v4
    heltec-v4-vertical
    mesh-deck
    m9
    wio-tracker-l2
)

# Lookups are functions rather than associative arrays so the script runs on
# macOS's stock bash 3.2 (which lacks `declare -A`).
env_flash_size() {
    case "$1" in
        cardputer-cap) echo "8MB" ;;
        *)             echo "16MB" ;;
    esac
}

env_out_name() {
    case "$1" in
        heltec-v4)          echo "heltec" ;;
        heltec-v4-vertical) echo "heltec-vertical" ;;
        *)                  echo "$1" ;;
    esac
}

has_env() {
    local env_name="$1"
    grep -q "^\[env:${env_name}\]" platformio.ini
}

validate_release_targets() {
    local env_name out_name seen_out_names="|" failed=false tdeck_pro_found=false

    echo "Release target contract:"
    for env_name in "${RELEASE_ENVS[@]}"; do
        [[ "$env_name" == "tdeck-pro" ]] && tdeck_pro_found=true
        if ! has_env "$env_name"; then
            echo "  ERROR: release environment '$env_name' is missing from platformio.ini" >&2
            failed=true
            continue
        fi

        out_name="$(env_out_name "$env_name")"
        if [[ -z "$out_name" ]]; then
            echo "  ERROR: release environment '$env_name' has no artifact slug" >&2
            failed=true
            continue
        fi
        case "$seen_out_names" in
            *"|${out_name}|"*)
                echo "  ERROR: duplicate release artifact slug '$out_name'" >&2
                failed=true
                ;;
            *) seen_out_names="${seen_out_names}${out_name}|" ;;
        esac
        printf '  %-20s -> %s\n' "$env_name" "$out_name"
    done

    if [[ "$tdeck_pro_found" != true || "$(env_out_name tdeck-pro)" != "tdeck-pro" ]]; then
        echo "  ERROR: T-Deck Pro release slugs no longer match the OTA firmware contract" >&2
        failed=true
    fi

    [[ "$failed" == false ]]
}

remote_tag_exists() {
    local tag="$1"
    git ls-remote --tags origin | grep -q "refs/tags/${tag}$"
}

delete_existing_release_and_tags() {
    local tag="$1"
    local remote_exists=false

    echo "Tag ${tag} already exists. Deleting existing release/tag so it can be recreated..."

    if remote_tag_exists "$tag"; then
        remote_exists=true
    fi

    if command -v gh >/dev/null 2>&1; then
        if gh release view "$tag" >/dev/null 2>&1; then
            gh release delete "$tag" --yes
            echo "Deleted existing GitHub release ${tag}."
        else
            echo "No existing GitHub release for ${tag}."
        fi
    elif [[ "$remote_exists" == true ]]; then
        echo "GitHub CLI (gh) is required to delete an existing GitHub release for ${tag}."
        echo "Install gh or manually delete the release, then rerun."
        exit 1
    fi

    if [[ "$remote_exists" == true ]]; then
        git push origin ":refs/tags/${tag}"
        echo "Deleted remote tag ${tag}."
    fi

    if git tag | grep -q "^${tag}$"; then
        git tag -d "$tag"
        echo "Deleted local tag ${tag}."
    fi
}

# ── Parse flags ───────────────────────────────────────────────────────────────
ASSUME_YES=false
NO_CLEAN=false
APPEND_LAST_NOTES=false
CHECK_TARGETS_ONLY=false
ALPHA=false
VERSION_ARG=""
NOTES_ONLY=false
USE_COMMITTED_NOTES=false
BUILD_LOCAL=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            echo "Usage: $0 [-y|--yes] [--alpha] [--version V] [--no-clean]"
            echo "          [--append-last-notes] [--check-targets] [--notes-only]"
            echo "          [--use-committed-notes] [--build-local]"
            echo "  Cuts a release on GitHub. Works out the version, writes"
            echo "  and reviews the release notes, commits them, pushes, and"
            echo "  dispatches the release workflow — which builds every"
            echo "  environment, signs the OTA images and publishes. Nothing"
            echo "  is compiled on this machine, and no toolchain is needed."
            echo ""
            echo "  --build-local does all of it here instead."
            echo ""
            echo "  --alpha     Cut an ALPHA release instead: tags"
            echo "              v<version>-alpha.<n> and publishes it as a GitHub"
            echo "              *prerelease*, which is what keeps it off the"
            echo "              stable channel. Only devices set to the Alpha"
            echo "              release channel are offered it. With -y the alpha"
            echo "              counter is bumped (v1.2.3-alpha.4 ->"
            echo "              v1.2.3-alpha.5); from a stable tag it starts a"
            echo "              new series at -alpha.1 on the next patch."
            echo "  --version V"
            echo "              Release exactly this version instead of deriving"
            echo "              one from the tag list (a leading 'v' is fine)."
            echo "              This is how an unattended run cuts anything other"
            echo "              than a patch bump, since there is no prompt to"
            echo "              answer. With --alpha, a bare X.Y.Z starts that"
            echo "              version's alpha series at -alpha.1."
            echo "  -y, --yes   Bump the patch level of the latest tag without"
            echo "              prompting (v3.6.5 -> v3.6.6) and accept the AI"
            echo "              release notes."
            echo "  --no-clean  Skip the full clean and build on top of whatever"
            echo "              is already in .pio. Much faster, and fine when"
            echo "              you just built these same sources — but the"
            echo "              images are only as trustworthy as that tree."
            echo "  --append-last-notes"
            echo "              Keep RELEASE_NOTES.md content from the previous"
            echo "              release and append this release's generated delta"
            echo "              notes under a new 'Update (vX.Y.Z)' heading."
            echo "  --check-targets"
            echo "              Validate release environments and artifact slugs,"
            echo "              then exit without building or publishing."
            echo "  --notes-only"
            echo "              Write and review RELEASE_NOTES.md, then stop"
            echo "              short of committing or dispatching. Use it to"
            echo "              draft notes without starting a release."
            echo "  --build-local"
            echo "              Build, sign and publish from this machine"
            echo "              instead of dispatching the GitHub workflow."
            echo "              Needs the full toolchain and the signing key."
            echo "              The workflow's own run is in this mode."
            echo "  --use-committed-notes"
            echo "              Publish RELEASE_NOTES.md exactly as it stands"
            echo "              instead of generating a summary. This is how an"
            echo "              unattended run reuses notes reviewed elsewhere,"
            echo "              and why it needs no model access of its own."
            exit 0
            ;;
        -y|--yes) ASSUME_YES=true ;;
        --alpha) ALPHA=true ;;
        --no-clean) NO_CLEAN=true ;;
        --append-last-notes) APPEND_LAST_NOTES=true ;;
        --check-targets) CHECK_TARGETS_ONLY=true ;;
        --notes-only) NOTES_ONLY=true ;;
        --use-committed-notes) USE_COMMITTED_NOTES=true ;;
        --build-local) BUILD_LOCAL=true ;;
        --version)
            if [[ -z "${2:-}" ]]; then
                echo "--version requires a value (e.g. --version 4.9.0)" >&2
                exit 1
            fi
            VERSION_ARG="$2"
            shift
            ;;
        --version=*) VERSION_ARG="${1#*=}" ;;
        *) echo "Unknown argument: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

if [[ "$NOTES_ONLY" == true && "$USE_COMMITTED_NOTES" == true ]]; then
    echo "--notes-only writes the notes; --use-committed-notes reuses them." >&2
    echo "They are the two halves of the same flow, not a combination." >&2
    exit 1
fi

# ── Where does this release happen? ──────────────────────────────────────────
# By default this script builds nothing. It works out the version, writes and
# reviews the notes, commits them, and dispatches
# .github/workflows/release.yml — which builds and signs on GitHub, where the
# signing key lives. --build-local restores the old behaviour of doing all of
# it here.
#
# GITHUB_ACTIONS is what puts the workflow's own run into building mode. It
# could pass --build-local instead, but on a runner that flag would be a lie —
# nothing about it is local — and a future reader deserves the YAML to mean
# what it says.
REMOTE=false
if [[ "$NOTES_ONLY" != true && "$CHECK_TARGETS_ONLY" != true \
      && "$BUILD_LOCAL" != true && "${GITHUB_ACTIONS:-}" != "true" ]]; then
    REMOTE=true
fi

validate_release_targets
if [[ "$CHECK_TARGETS_ONLY" == true ]]; then
    echo "Release target contract OK."
    exit 0
fi

# ── Version prompt ────────────────────────────────────────────────────────────
CURRENT=$(cat VERSION 2>/dev/null | tr -d '\n')
PREV_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "none")
echo "Current version: ${CURRENT:-unknown}"
echo "Latest git tag:  $PREV_TAG"
echo ""

# Newest tag matching a glob, in version order, restricted to well-formed tags.
# The filter matters: this repo's tag list contains malformed entries (vv3.2.4)
# that would otherwise be picked up and bumped into a wrong version.
latest_tag_matching() {
    local glob="$1" filter="$2"
    # Filter first, then sort with sort -V rather than git's --sort=-v:refname.
    # Two reasons: git ranks the malformed tags in this repo's history (vv3.2.4)
    # above every real one, and sort -V is what the alpha counter was verified
    # against — it orders -alpha.10 after -alpha.9, where a lexical sort would
    # not.
    git tag --list "$glob" | grep -E "$filter" | sort -V | tail -1
}

# True when $1 >= $2 as versions.
version_ge() {
    [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" == "$1" ]]
}

if [[ -n "$VERSION_ARG" ]]; then
    # An explicitly requested version beats every derivation below. This is the
    # only way an unattended run (CI) can cut anything but a patch bump, since
    # there is no prompt for it to answer.
    VERSION="${VERSION_ARG#v}"   # a pasted tag name works as-is
    if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]; then
        echo "--version must look like 4.9.0 or 4.9.0-alpha.1 (got '$VERSION_ARG')." >&2
        exit 1
    fi
    # A bare X.Y.Z with --alpha means "start that version's alpha series",
    # matching what the interactive alpha prompt accepts.
    if [[ "$ALPHA" == true && "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        VERSION="${VERSION}-alpha.1"
    fi
    echo "Using requested version v$VERSION"
elif [[ "$ALPHA" == true ]]; then
    # Alphas are numbered per stable version: vX.Y.Z-alpha.N, where X.Y.Z is the
    # release the series is heading towards — so the alphas leading to v4.7.8
    # are v4.7.8-alpha.1, -alpha.2, and so on, and they sort below the finished
    # v4.7.8 both for git and for the device's own version comparison.
    PREV_ALPHA_TAG="$(latest_tag_matching 'v*-alpha.*' '^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$')"
    PREV_STABLE_TAG="$(latest_tag_matching 'v*' '^v[0-9]+\.[0-9]+\.[0-9]+$')"
    echo "Latest alpha tag:  ${PREV_ALPHA_TAG:-none}"
    echo "Latest stable tag: ${PREV_STABLE_TAG:-none}"
    echo ""

    ALPHA_BASE=""
    ALPHA_NUM=0
    if [[ "$PREV_ALPHA_TAG" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-alpha\.([0-9]+)$ ]]; then
        ALPHA_BASE="${BASH_REMATCH[1]}"
        ALPHA_NUM="${BASH_REMATCH[2]}"
    fi
    NEXT_STABLE_BASE=""
    if [[ "$PREV_STABLE_TAG" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        NEXT_STABLE_BASE="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
    fi

    if [[ "$ASSUME_YES" == true ]]; then
        # Continue the existing series only while it is still ahead of what has
        # shipped. Once a stable release catches up or passes it, bumping the old
        # counter would publish an alpha *behind* stable — which every device on
        # the alpha channel would then refuse as "not newer", leaving the channel
        # silently dead.
        if [[ -n "$ALPHA_BASE" ]] \
           && { [[ -z "$NEXT_STABLE_BASE" ]] || version_ge "$ALPHA_BASE" "$NEXT_STABLE_BASE"; }; then
            VERSION="${ALPHA_BASE}-alpha.$(( ALPHA_NUM + 1 ))"
            echo "Auto-bumped $PREV_ALPHA_TAG -> v$VERSION"
        elif [[ -n "$NEXT_STABLE_BASE" ]]; then
            VERSION="${NEXT_STABLE_BASE}-alpha.1"
            echo "Starting a new alpha series after $PREV_STABLE_TAG -> v$VERSION"
        else
            echo "Cannot auto-bump: no vMAJOR.MINOR.PATCH tag to base an alpha on." >&2
            echo "Re-run without -y and enter the version explicitly." >&2
            exit 1
        fi
    else
        read -rp "New alpha version (e.g. 4.7.8-alpha.1, or 4.7.8 for -alpha.1): " VERSION
        # A bare version means "start that version's alpha series".
        if [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            VERSION="${VERSION}-alpha.1"
            echo "Using v$VERSION"
        fi
    fi
elif [[ "$ASSUME_YES" == true ]]; then
    # Derived from the latest tag, not the VERSION file: the tag is what was
    # actually published, and VERSION has been seen lagging behind it.
    # Deliberately strict — the tag list contains malformed entries (vv3.2.4),
    # and silently "fixing" one of those would publish a wrong version.
    #
    # An alpha tag fails this on purpose: after a run of alphas the most recent
    # tag is one of them, and there is no safe way to guess which stable version
    # was meant. Pass the version explicitly instead.
    if [[ "$PREV_TAG" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        VERSION="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
        echo "Auto-bumped $PREV_TAG -> v$VERSION"
    else
        echo "Cannot auto-bump: latest tag '$PREV_TAG' is not vMAJOR.MINOR.PATCH." >&2
        echo "Re-run without -y and enter the version explicitly." >&2
        exit 1
    fi
else
    read -rp "New version (e.g. 1.0.0): " VERSION
fi
if [[ -z "$VERSION" ]]; then
    echo "No version entered. Aborting."
    exit 1
fi
if [[ "$ALPHA" == true && ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ ]]; then
    echo "Alpha version must look like 4.7.8-alpha.1 (got '$VERSION')." >&2
    echo "The device only treats a tag as a prerelease when it carries a" >&2
    echo "'-' suffix, so this shape is what keeps it off the stable channel." >&2
    exit 1
fi
TAG="v$VERSION"

# ── Preflight checks ──────────────────────────────────────────────────────────
if ! command -v gh >/dev/null 2>&1; then
    echo "Error: GitHub CLI (gh) is required. Install from https://cli.github.com/ and run: gh auth login" >&2
    exit 1
fi

if [[ "$REMOTE" == true || "$NOTES_ONLY" == true ]]; then
    # Neither mode compiles anything here, so a missing toolchain is not a
    # reason to refuse — this can run on a machine that has never built.
    BOOT_APP0=""; PIO=""; ESPTOOL=""
else
    BOOT_APP0=$(find ~/.platformio/packages/framework-arduinoespressif32/tools/partitions \
        -name boot_app0.bin 2>/dev/null | head -1)
    if [[ -z "$BOOT_APP0" ]]; then
        echo "Error: boot_app0.bin not found in PlatformIO packages." >&2
        echo "Run 'pio run' at least once to download the framework." >&2
        exit 1
    fi

    # PlatformIO installs itself into a private venv on a workstation, but a CI
    # runner gets it on PATH from `pip install platformio`. Prefer the venv copy
    # where it exists so a machine with both keeps using the one it always used.
    if [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
        PIO="$HOME/.platformio/penv/bin/pio"
    elif command -v pio >/dev/null 2>&1; then
        PIO="pio"
    else
        echo "Error: platformio not found. Run: pip install platformio" >&2
        exit 1
    fi

    if python -m esptool version >/dev/null 2>&1; then
        ESPTOOL="python -m esptool"
    elif command -v esptool.py >/dev/null 2>&1; then
        ESPTOOL="esptool.py"
    else
        echo "Error: esptool not found. Run: pip install esptool" >&2
        exit 1
    fi
fi

# ── Recreating an existing release? ──────────────────────────────────────────
# Only decided here — the deletion itself is deferred until the replacement is
# built and committed (see "Commit, push, and tag"). Tearing down a published
# release and then failing to build its successor would leave users with no
# download at all.
RECREATE_RELEASE=false
if remote_tag_exists "$TAG" || git tag | grep -q "^${TAG}$"; then
    RECREATE_RELEASE=true
    echo "Tag ${TAG} already exists — it will be replaced once the build succeeds."
fi

# ── Failure guard ─────────────────────────────────────────────────────────────
# VERSION, RELEASE_NOTES.md and the generated pubkey header are all written
# before the build, because the build bakes them into the firmware — so they
# cannot simply be reordered to after it. Snapshot them here instead and roll
# back on any failure or interrupt, so a build that dies halfway through never
# costs a hand-edited set of release notes.
# Disarmed once the release commit exists: past that point the new contents are
# the release, and restoring them would only leave the tree out of step with it.
PUBKEY_HEADER="src/ota_signing_pubkey.h"
GUARD_FILES=(VERSION RELEASE_NOTES.md "$PUBKEY_HEADER")
GUARD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/camillia-release.XXXXXX")"
GUARD_ARMED=true
GUARD_STAGED=false   # set once git add has run, so rollback can resync the index

guard_snapshot() {
    local f
    for f in "${GUARD_FILES[@]}"; do
        mkdir -p "$GUARD_DIR/$(dirname "$f")"
        if [[ -f "$f" ]]; then
            cp -p "$f" "$GUARD_DIR/$f"
        else
            # Record the absence too, so a file this run creates from nothing
            # (first release, freshly generated signing key) is removed again on
            # rollback rather than left behind as a stray artifact.
            : > "$GUARD_DIR/$f.absent"
        fi
    done
}

guard_rollback() {
    local f
    for f in "${GUARD_FILES[@]}"; do
        if [[ -f "$GUARD_DIR/$f.absent" ]]; then
            rm -f "$f"
        elif [[ -f "$GUARD_DIR/$f" ]]; then
            cp -p "$GUARD_DIR/$f" "$f"
        fi
    done
    # If the commit failed after `git add -A`, the index still holds the copies
    # just rolled back. Re-add so it agrees with what is now on disk.
    if [[ "$GUARD_STAGED" == true ]]; then
        git add -- "${GUARD_FILES[@]}" 2>/dev/null || true
    fi
}

on_exit() {
    local status=$?
    if [[ "$GUARD_ARMED" == true && $status -ne 0 ]]; then
        echo "" >&2
        echo "Release failed (exit $status) — rolling back." >&2
        guard_rollback || true
        echo "Restored: ${GUARD_FILES[*]}" >&2
        echo "Nothing was committed, tagged, or published." >&2
        if [[ "$RECREATE_RELEASE" == true ]]; then
            echo "Existing release ${TAG} was left untouched." >&2
        fi
    fi
    rm -rf "$GUARD_DIR"
}
# INT/TERM exit non-zero, which then runs the EXIT trap and rolls back.
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

guard_snapshot

# ── Update VERSION file ───────────────────────────────────────────────────────
# Skipped for --notes-only: that run is not cutting the release, and the job
# that does will write this itself.
if [[ "$NOTES_ONLY" != true && "$REMOTE" != true ]]; then
    echo "$TAG" > VERSION
    echo "Updated VERSION to $TAG"
fi

# ── AI release summary ────────────────────────────────────────────────────────
# Turns the commit range since the last tag into user-facing release notes,
# written to RELEASE_NOTES.md. This runs BEFORE the build on purpose: the build
# bakes that file into the firmware (see tools/gen_release_notes.py) so the
# device can show the same notes that get published here, and the release
# commit below picks the file up. It also means the notes are reviewed before
# a long build rather than after it.
# Prefers the Claude API (works in CI with ANTHROPIC_API_KEY); falls back to the
# locally authenticated `claude` CLI. Every failure path is non-fatal — the
# release still publishes with GitHub's auto-generated notes.
# RELEASE_NOTES.md is committed, so whatever is on disk at build time gets baked
# into the firmware. By default each release overwrites it with only this
# release's summary. With --append-last-notes, the existing file is kept and the
# new summary is appended under an "Update (vX.Y.Z)" heading.
NOTES_FILE="RELEASE_NOTES.md"
PREV_NOTES=""
if [[ -f "$NOTES_FILE" ]]; then
    PREV_NOTES=$(cat "$NOTES_FILE")
fi

write_release_notes() {
    local body="$1"
    if [[ "$APPEND_LAST_NOTES" == true && -n "${PREV_NOTES//[[:space:]]/}" ]]; then
        {
            printf '%s\n' "$PREV_NOTES"
            printf '\n\n### Update (%s)\n' "$TAG"
            printf '%s\n' "$body"
        } > "$NOTES_FILE"
    else
        printf '%s\n' "$body" > "$NOTES_FILE"
    fi
}

write_placeholder_notes() {
    write_release_notes "Release ${TAG}

No release notes were generated for this build."
}

generate_ai_summary() {
    local range base log diffstat diff untracked prompt
    if [[ -n "$PREV_TAG" && "$PREV_TAG" != "none" ]]; then
        range="${PREV_TAG}..HEAD"
        base="$PREV_TAG"
    else
        range="HEAD"    # first release: summarize what history we have
        base="HEAD"
    fi

    log=$(git log "$range" --no-merges --pretty=format:'- %s' 2>/dev/null | head -200 || true)

    # Diffed against the working tree, not HEAD: this runs before the release
    # commit, so a release whose work is still uncommitted — the normal case when
    # you run this straight after finishing the work — has nothing in the log at
    # all. `git diff <tag>` spans both committed and uncommitted changes.
    diffstat=$(git diff --stat "$base" 2>/dev/null | tail -40 || true)
    # Commit subjects are often terse ("Loads of stuff"), so include a bounded,
    # zero-context diff — it's what lets the summary describe real changes.
    # Capped so a large release can't blow up the request.
    diff=$(git diff "$base" --unified=0 \
              -- . ':(exclude)dist' ':(exclude)*.bin' ':(exclude)*.sig' \
              2>/dev/null | head -2000 || true)
    # New files are untracked until the release commit, so they are invisible to
    # git diff. Their names at least tell the summary something was added.
    untracked=$(git ls-files --others --exclude-standard 2>/dev/null | head -40 || true)

    # Commits alone are no longer the test: an uncommitted release has none.
    [[ -n "$log" || -n "$diff" ]] || return 1

    prompt="You are writing release notes for Camillia-MT, Meshtastic-compatible
    firmware for ESP32-S3 handheld LoRa devices (T-Deck, T-Deck Pro, T-Lora Pager TFT, M5Stack
Cardputer, Heltec V4, Attaky Mesh Deck, Elecrow ThinkNode M9, Seeed Wio Tracker L2).

Summarize what changed in release ${TAG} for the people who flash and use it.

Rules:
- Group under '### New', '### Changed', '### Fixed'. Omit any empty section.
- One line per user-visible change, written for a device owner, not a developer.
- Name the affected board(s) when a change is board-specific.
- Skip pure refactors, dependency bumps, version bumps, and release chores.
- Commit subjects are often terse or uninformative. When one is, work out what
  changed from the diff instead. If you still cannot tell what it means for a
  user, leave it out silently.
- The release commit has not been made yet, so some or all of this release's
  work is uncommitted and the commit list may be short or empty. The diff is the
  authoritative record of what changed; use it.
- This text is published verbatim as the release notes. Never address the reader
  or the requester, never ask questions, and never explain what you omitted or
  why. Output nothing but the section headers and their bullet lines.

Commits since ${PREV_TAG} (may be empty):
${log}

Files changed (committed and uncommitted):
${diffstat}

New files not yet tracked by git:
${untracked}

Diff (zero context, truncated):
${diff}"

    if [[ -n "${ANTHROPIC_API_KEY:-}" ]]; then
        jq -n --arg p "$prompt" \
            '{model:"claude-opus-5",
              max_tokens:16000,
              thinking:{type:"adaptive"},
              messages:[{role:"user",content:$p}]}' 2>/dev/null \
        | curl -sS --max-time 180 https://api.anthropic.com/v1/messages \
            -H "content-type: application/json" \
            -H "x-api-key: ${ANTHROPIC_API_KEY}" \
            -H "anthropic-version: 2023-06-01" \
            --data @- 2>/dev/null \
        | jq -r '.content[]? | select(.type=="text") | .text' 2>/dev/null || true
    elif command -v claude >/dev/null 2>&1; then
        claude -p "$prompt" 2>/dev/null || true
    else
        return 1
    fi
}

# Interactive accept/edit/discard for freshly generated notes. Returns 0 when the
# notes should be used, 1 when the caller should fall back. Only prompts on a
# TTY, so an unattended run accepts and continues; --yes accepts explicitly
# rather than relying on that heuristic.
review_notes_interactively() {
    local ans
    [[ -t 0 && "$ASSUME_YES" != true ]] || return 0
    while true; do
        read -rp "Use these notes? [Y]es / [e]dit / [n]o: " ans || ans="y"
        case "${ans:-y}" in
            y|Y|yes|Yes) return 0 ;;
            e|E|edit)
                "${EDITOR:-vi}" "$NOTES_FILE" || true
                echo ""
                echo "──────── edited release notes ────────"
                cat "$NOTES_FILE"
                echo "──────────────────────────────────────"
                ;;
            n|N|no) return 1 ;;
            *) echo "Please answer y, e, or n." ;;
        esac
    done
}

NOTES_ARGS=(--generate-notes)
if [[ "$USE_COMMITTED_NOTES" == true ]]; then
    # Publish RELEASE_NOTES.md exactly as committed. It was written and reviewed
    # by an earlier `--notes-only` run, so this release needs no model access of
    # its own — which is what lets the GitHub workflow publish notes that were
    # read by a human first.
    if [[ -s "$NOTES_FILE" ]]; then
        echo ""
        echo "Using committed release notes:"
        echo "──────── release notes ────────"
        cat "$NOTES_FILE"
        echo "───────────────────────────────"
        NOTES_ARGS=(--notes-file "$NOTES_FILE" --generate-notes)
    else
        write_placeholder_notes
        echo "No committed release notes found — using GitHub's generated notes only."
    fi
else
    echo ""
    echo "Generating AI release summary..."
    AI_SUMMARY=$(generate_ai_summary || true)
    if [[ -n "${AI_SUMMARY// /}" ]]; then
        write_release_notes "$AI_SUMMARY"
        echo ""
        echo "──────── proposed release notes ────────"
        cat "$NOTES_FILE"
        echo "────────────────────────────────────────"

        if review_notes_interactively; then
            # An accepted-but-emptied file would publish blank notes.
            if [[ -s "$NOTES_FILE" ]]; then
                # gh prepends --notes-file content above the generated commit list.
                NOTES_ARGS=(--notes-file "$NOTES_FILE" --generate-notes)
            else
                write_placeholder_notes
                echo "Notes file is empty — using GitHub's generated notes only."
            fi
        elif [[ "$NOTES_ONLY" == true || "$REMOTE" == true ]]; then
            # Nothing to hand to GitHub, and the failure guard restores the file
            # on the way out — so a rejected draft costs nothing.
            echo "Discarded. RELEASE_NOTES.md left as it was." >&2
            exit 1
        else
            write_placeholder_notes
            echo "Discarded — using GitHub's generated notes only."
        fi
    elif [[ "$NOTES_ONLY" == true || "$REMOTE" == true ]]; then
        echo "Could not generate release notes (no ANTHROPIC_API_KEY and no usable" >&2
        echo "'claude' CLI, or the request failed). RELEASE_NOTES.md left as it was." >&2
        exit 1
    else
        write_placeholder_notes
        echo "AI summary unavailable (no ANTHROPIC_API_KEY and no usable 'claude' CLI,"
        echo "or the request failed) — falling back to GitHub's generated notes."
    fi
fi

if [[ "$NOTES_ONLY" == true || "$REMOTE" == true ]]; then
    # Neither mode builds. RELEASE_NOTES.md is the only thing produced here, and
    # committing it is not an extra step for CI's benefit — the build bakes that
    # file into the firmware (tools/gen_release_notes.py) so the device shows the
    # same text the release publishes.
    if [[ "$ALPHA" == true ]]; then NOTES_CHANNEL=alpha; else NOTES_CHANNEL=stable; fi
fi

if [[ "$NOTES_ONLY" == true ]]; then
    GUARD_ARMED=false
    echo ""
    echo "Release notes for $TAG written to $NOTES_FILE. Nothing built or published."
    echo ""
    echo "Next:"
    echo "  git add $NOTES_FILE"
    echo "  git commit -m \"Release notes for $TAG\" && git push"
    echo "  gh workflow run release.yml -f channel=$NOTES_CHANNEL -f version=$VERSION"
    echo ""
    echo "Pass that version to the workflow. Left blank it derives its own, and"
    echo "these notes would then be published under a different tag."
    exit 0
fi

if [[ "$REMOTE" == true ]]; then
    BRANCH="$(git rev-parse --abbrev-ref HEAD)"
    if [[ "$BRANCH" == "HEAD" ]]; then
        echo "Detached HEAD — check out a branch before releasing." >&2
        exit 1
    fi

    # GitHub builds what gets pushed, not what is sitting in the working tree.
    # A local-only edit would silently be left out of the release, which is a
    # failure mode the --build-local path simply cannot have.
    DIRTY="$(git status --porcelain --untracked-files=no \
             | grep -v "[[:space:]]${NOTES_FILE}$" || true)"

    echo ""
    echo "Ready to release $TAG on GitHub:"
    echo "  branch:   $BRANCH"
    echo "  channel:  $NOTES_CHANNEL"
    echo "  notes:    $NOTES_FILE, published verbatim"
    if [[ -n "$DIRTY" ]]; then
        echo ""
        echo "  !! Uncommitted changes that will NOT be in this release:"
        printf '%s\n' "$DIRTY" | sed 's/^/     /'
    fi
    echo ""

    if [[ -t 0 && "$ASSUME_YES" != true ]]; then
        read -rp "Commit the notes, push $BRANCH and dispatch? [y/N]: " ans || ans="n"
        case "${ans:-n}" in
            y|Y|yes|Yes) ;;
            *) echo "Aborted. RELEASE_NOTES.md will be restored." >&2; exit 1 ;;
        esac
    fi

    git add -- "$NOTES_FILE"
    if git diff --cached --quiet -- "$NOTES_FILE"; then
        echo "Release notes unchanged — nothing to commit."
    else
        git commit -m "Release notes for $TAG"
    fi

    # Past this point the notes are committed, so the pre-run copy is no longer
    # the one that should win.
    GUARD_ARMED=false

    git push || git push -u origin "$BRANCH"

    # Note the newest run before dispatching so the one we report is definitely
    # ours and not the previous release's.
    PREV_RUN=$(gh run list --workflow=release.yml --limit 1 \
               --json databaseId -q '.[0].databaseId' 2>/dev/null || true)

    gh workflow run release.yml --ref "$BRANCH" \
        -f channel="$NOTES_CHANNEL" \
        -f version="$VERSION" \
        -f notes=committed

    echo ""
    echo "Dispatched $TAG ($NOTES_CHANNEL) on $BRANCH. Waiting for the run..."
    RUN_ID=""
    for _ in $(seq 1 15); do
        sleep 2
        RUN_ID=$(gh run list --workflow=release.yml --limit 1 \
                 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)
        [[ -n "$RUN_ID" && "$RUN_ID" != "$PREV_RUN" ]] && break
        RUN_ID=""
    done

    if [[ -n "$RUN_ID" ]]; then
        gh run view "$RUN_ID" --json url -q .url
        echo ""
        echo "Follow it with:  gh run watch $RUN_ID"
    else
        echo "Run not visible yet. Check: gh run list --workflow=release.yml"
    fi
    exit 0
fi

# ── Build firmware ────────────────────────────────────────────────────────────
echo ""
echo "Building firmware..."
BUILD_ARGS=()
for env_name in "${RELEASE_ENVS[@]}"; do
    if has_env "$env_name"; then
        BUILD_ARGS+=( -e "$env_name" )
    fi
done

if [[ ${#BUILD_ARGS[@]} -eq 0 ]]; then
    echo "No release environments found in platformio.ini"
    exit 1
fi

# ── OTA image signing key ─────────────────────────────────────────────────────
# Signed OTA images let the device verify authenticity over plain HTTP (no TLS).
# The private key is gitignored and must be backed up; the matching public key is
# baked into the firmware via src/ota_signing_pubkey.h, regenerated here so the
# build always embeds the key that will sign these images.
SIGNING_KEY="ota_signing_key.pem"
# PUBKEY_HEADER is set up with the failure guard, which covers this file too.
if ! command -v openssl >/dev/null 2>&1; then
    echo "Error: openssl is required to sign OTA images. Install it and retry." >&2
    exit 1
fi
if [[ ! -f "$SIGNING_KEY" ]]; then
    echo ""
    echo "!! No OTA signing key found — generating a new ECDSA P-256 key at:"
    echo "!!     $SIGNING_KEY"
    echo "!! BACK THIS UP SECURELY and never commit it. If you lose it, every"
    echo "!! existing device will reject all future updates (you'd have to reflash"
    echo "!! over USB to ship a new key)."
    echo ""
    openssl ecparam -name prime256v1 -genkey -noout -out "$SIGNING_KEY"
    chmod 600 "$SIGNING_KEY"
fi
# Bake the matching public key into the firmware before building.
{
    echo '// AUTO-GENERATED from ota_signing_key.pem by release.sh. Do not edit by hand.'
    echo '// ECDSA P-256 public key used to verify signed OTA images (see ota_update.cpp).'
    echo '#pragma once'
    echo ''
    echo 'static const char OTA_SIGNING_PUBKEY_PEM[] ='
    openssl ec -in "$SIGNING_KEY" -pubout 2>/dev/null | sed 's/.*/    "&\\n"/'
    echo '    ;'
} > "$PUBKEY_HEADER"
echo "Regenerated $PUBKEY_HEADER from $SIGNING_KEY"

# The clean is here so a release image can never contain a stale object file —
# PlatformIO's dependency tracking misses enough (a changed build flag, a header
# reached through a symlink) that "it rebuilt what mattered" is not something to
# stake a signed, published binary on. --no-clean trades that guarantee for the
# several minutes it costs, which is a fair trade when re-cutting a release from
# sources you just built and no trade at all otherwise.
if [[ "$NO_CLEAN" == true ]]; then
    echo "Skipping full clean (--no-clean); building on existing .pio output."
else
    echo "Running full clean for release environments..."
    "$PIO" run "${BUILD_ARGS[@]}" -t fullclean
fi

"$PIO" run "${BUILD_ARGS[@]}"
echo "Build successful."

# ── Commit, push, and tag ─────────────────────────────────────────────────────
GUARD_STAGED=true
git add -A
if [[ "$ALPHA" == true ]]; then
    git commit -m "Alpha release $TAG"
else
    git commit -m "Release $TAG"
fi

# Point of no return. VERSION and the notes are part of a commit now, so the
# pre-release copies are no longer the ones that should win.
GUARD_ARMED=false

git push

echo "Changes committed and pushed."

# Deferred from the preflight: drop the superseded release and tag only now that
# the replacement is built, committed and pushed, so the window in which neither
# exists is as short as it can be.
if [[ "$RECREATE_RELEASE" == true ]]; then
    delete_existing_release_and_tags "$TAG"
fi

if git tag | grep -q "^$TAG$"; then
    git tag -d "$TAG"
fi
git tag "$TAG"
git push origin "$TAG"

echo "Tag $TAG pushed."

# ── Merge factory images ──────────────────────────────────────────────────────
# Turns the current .pio build output into distributable, tag-named factory and
# OTA images plus detached signatures, written to dist/ for the given tag ($1).
merge_sign_assets() {
    local tag="$1" env_name out_name flash_size d out ota_out
    for env_name in "${RELEASE_ENVS[@]}"; do
        if ! has_env "$env_name"; then continue; fi
        out_name="$(env_out_name "$env_name")"
        flash_size="$(env_flash_size "$env_name")"
        d=".pio/build/${env_name}"
        out="dist/camillia-mt-${out_name}-${tag}.bin"
        ota_out="dist/camillia-mt-${out_name}-${tag}-ota.bin"
        echo "  ${env_name} (${flash_size}) -> ${out}"
        $ESPTOOL --chip esp32s3 merge_bin \
            -o "${out}" \
            -fm dio \
            -ff 80m \
            -fs "${flash_size}" \
            0x0     "${d}/bootloader.bin" \
            0x8000  "${d}/partitions.bin" \
            0xe000  "${BOOT_APP0}" \
            0x10000 "${d}/firmware.bin"
        cp "${d}/firmware.bin" "${ota_out}"
        cp "${d}/firmware.elf" "dist/camillia-mt-${out_name}-${tag}.elf"
        # Detached ECDSA-P256/SHA-256 signature over the OTA image. The device
        # verifies this against the baked-in public key before committing the update,
        # which is what makes plain-HTTP (TLS-free) OTA safe.
        openssl dgst -sha256 -sign "$SIGNING_KEY" -out "${ota_out}.sig" "${ota_out}"
        echo "    signed -> ${ota_out}.sig"
    done
}

verify_release_assets() {
    local tag="$1" env_name out_name d factory ota sig verify_key
    verify_key="$GUARD_DIR/ota-signing-public.pem"
    openssl ec -in "$SIGNING_KEY" -pubout -out "$verify_key" 2>/dev/null

    echo "Verifying release assets..."
    for env_name in "${RELEASE_ENVS[@]}"; do
        out_name="$(env_out_name "$env_name")"
        d=".pio/build/${env_name}"
        factory="dist/camillia-mt-${out_name}-${tag}.bin"
        ota="dist/camillia-mt-${out_name}-${tag}-ota.bin"
        sig="${ota}.sig"

        for asset in "$factory" "$ota" "$sig"; do
            if [[ ! -s "$asset" ]]; then
                echo "Error: required release asset is missing or empty: $asset" >&2
                return 1
            fi
        done
        if ! cmp -s "${d}/firmware.bin" "$ota"; then
            echo "Error: OTA asset does not match ${env_name}/firmware.bin: $ota" >&2
            return 1
        fi
        if ! openssl dgst -sha256 -verify "$verify_key" -signature "$sig" "$ota" \
                >/dev/null 2>&1; then
            echo "Error: OTA signature verification failed: $sig" >&2
            return 1
        fi
        echo "  OK ${env_name}: $(basename "$factory"), $(basename "$ota"), $(basename "$sig")"
    done
}

echo ""
echo "Merging factory images..."
rm -rf dist
mkdir -p dist
merge_sign_assets "$TAG"
verify_release_assets "$TAG"

ls -lh dist/


# ── Create GitHub release ─────────────────────────────────────────────────────
# Publish only the flashable images and their signatures. ELF/MAP symbol files
# are intentionally NOT uploaded (they bloat releases by ~35MB each); the
# matching symbols stay in dist/ locally and are archived by CI as workflow
# artifacts. See .github/workflows/build.yml.
echo ""
echo "Creating GitHub release $TAG..."
# --prerelease is what separates the channels, and it is not cosmetic: GitHub
# excludes prereleases from /releases/latest, which is the endpoint the stable
# OTA route reads. Drop this flag and every stable device is offered the alpha
# on its next check.
RELEASE_FLAGS=()
if [[ "$ALPHA" == true ]]; then
    RELEASE_FLAGS+=( --prerelease )
fi
gh release create "$TAG" \
    --title "$TAG" \
    "${RELEASE_FLAGS[@]}" \
    "${NOTES_ARGS[@]}" \
    dist/*.bin \
    dist/*.sig

echo ""
if [[ "$ALPHA" == true ]]; then
    echo "Alpha release $TAG published (prerelease)."
    echo "Only devices with Release Channel = Alpha will be offered it."
else
    echo "Release $TAG published."
fi
gh release view "$TAG" --json url -q .url

# RELEASE_NOTES.md is deliberately left in place: it is committed with the
# release and is what the next build embeds until the following release
# overwrites it.
