#!/bin/bash
set -e

RELEASE_ENVS=(
    tdeck
    tlora-pager-tft
    cardputer-cap
    heltec-v4
    heltec-v4-vertical
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
for arg in "$@"; do
    case "$arg" in
        -h|--help)
            echo "Usage: $0"
            echo "  Cuts a stable release: builds all envs, signs OTA images,"
            echo "  tags v<version>, and publishes a GitHub release."
            exit 0
            ;;
        *) echo "Unknown argument: $arg (see --help)" >&2; exit 1 ;;
    esac
done

# ── Version prompt ────────────────────────────────────────────────────────────
CURRENT=$(cat VERSION 2>/dev/null | tr -d '\n')
PREV_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "none")
echo "Current version: ${CURRENT:-unknown}"
echo "Latest git tag:  $PREV_TAG"
echo ""

read -rp "New version (e.g. 1.0.0): " VERSION
if [[ -z "$VERSION" ]]; then
    echo "No version entered. Aborting."
    exit 1
fi
TAG="v$VERSION"

# ── Preflight checks ──────────────────────────────────────────────────────────
if ! command -v gh >/dev/null 2>&1; then
    echo "Error: GitHub CLI (gh) is required. Install from https://cli.github.com/ and run: gh auth login" >&2
    exit 1
fi

BOOT_APP0=$(find ~/.platformio/packages/framework-arduinoespressif32/tools/partitions \
    -name boot_app0.bin 2>/dev/null | head -1)
if [[ -z "$BOOT_APP0" ]]; then
    echo "Error: boot_app0.bin not found in PlatformIO packages." >&2
    echo "Run 'pio run' at least once to download the framework." >&2
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

# ── Clean up any existing release/tag for this version ───────────────────────
if remote_tag_exists "$TAG" || git tag | grep -q "^${TAG}$"; then
    delete_existing_release_and_tags "$TAG"
fi

# ── Update VERSION file ───────────────────────────────────────────────────────
echo "$TAG" > VERSION
echo "Updated VERSION to $TAG"

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
PUBKEY_HEADER="src/ota_signing_pubkey.h"
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

echo "Running full clean for release environments..."
~/.platformio/penv/bin/pio run "${BUILD_ARGS[@]}" -t fullclean

~/.platformio/penv/bin/pio run "${BUILD_ARGS[@]}"
echo "Build successful."

# ── Commit, push, and tag ─────────────────────────────────────────────────────
git add -A
git commit -m "Release $TAG"
git push

echo "Changes committed and pushed."

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

echo ""
echo "Merging factory images..."
rm -rf dist
mkdir -p dist
merge_sign_assets "$TAG"

ls -lh dist/

# ── AI release summary ────────────────────────────────────────────────────────
# Turns the commit range since the last tag into user-facing release notes.
# Prefers the Claude API (works in CI with ANTHROPIC_API_KEY); falls back to the
# locally authenticated `claude` CLI. Every failure path is non-fatal — the
# release still publishes with GitHub's auto-generated notes.
generate_ai_summary() {
    local range log diffstat diff prompt
    if [[ -n "$PREV_TAG" && "$PREV_TAG" != "none" ]]; then
        range="${PREV_TAG}..HEAD"
    else
        range="HEAD"    # first release: summarize what history we have
    fi

    log=$(git log "$range" --no-merges --pretty=format:'- %s' 2>/dev/null | head -200 || true)
    diffstat=$(git diff --stat "$range" 2>/dev/null | tail -40 || true)
    # Commit subjects are often terse ("Loads of stuff"), so include a bounded,
    # zero-context diff — it's what lets the summary describe real changes.
    # Capped so a large release can't blow up the request.
    diff=$(git diff "$range" --unified=0 \
              -- . ':(exclude)dist' ':(exclude)*.bin' ':(exclude)*.sig' \
              2>/dev/null | head -2000 || true)
    [[ -n "$log" ]] || return 1

    prompt="You are writing release notes for Camillia-MT, Meshtastic-compatible
firmware for ESP32-S3 handheld LoRa devices (T-Deck, T-Lora Pager TFT, M5Stack
Cardputer, Heltec V4).

Summarize what changed in release ${TAG} for the people who flash and use it.

Rules:
- Group under '### New', '### Changed', '### Fixed'. Omit any empty section.
- One line per user-visible change, written for a device owner, not a developer.
- Name the affected board(s) when a change is board-specific.
- Skip pure refactors, dependency bumps, version bumps, and release chores.
- Commit subjects are often terse or uninformative. When one is, work out what
  changed from the diff instead. If you still cannot tell what it means for a
  user, leave it out silently.
- This text is published verbatim as the release notes. Never address the reader
  or the requester, never ask questions, and never explain what you omitted or
  why. Output nothing but the section headers and their bullet lines.

Commits:
${log}

Files changed:
${diffstat}

Diff (zero context, truncated):
${diff}"

    if [[ -n "${ANTHROPIC_API_KEY:-}" ]]; then
        jq -n --arg p "$prompt" \
            '{model:"claude-opus-4-8",
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

NOTES_ARGS=(--generate-notes)
echo ""
echo "Generating AI release summary..."
AI_SUMMARY=$(generate_ai_summary || true)
if [[ -n "${AI_SUMMARY// /}" ]]; then
    AI_NOTES_FILE=$(mktemp)
    printf '%s\n' "$AI_SUMMARY" > "$AI_NOTES_FILE"
    echo ""
    echo "──────── proposed release notes ────────"
    cat "$AI_NOTES_FILE"
    echo "────────────────────────────────────────"

    # Only prompt on a TTY so CI/non-interactive runs accept and continue.
    if [[ -t 0 ]]; then
        while true; do
            read -rp "Use these notes? [Y]es / [e]dit / [n]o (GitHub notes only): " ans \
                || ans="y"
            case "${ans:-y}" in
                y|Y|yes|Yes)
                    break
                    ;;
                e|E|edit)
                    "${EDITOR:-vi}" "$AI_NOTES_FILE" || true
                    echo ""
                    echo "──────── edited release notes ────────"
                    cat "$AI_NOTES_FILE"
                    echo "──────────────────────────────────────"
                    ;;
                n|N|no)
                    rm -f "$AI_NOTES_FILE"
                    AI_NOTES_FILE=""
                    echo "Discarded — using GitHub's generated notes only."
                    break
                    ;;
                *)
                    echo "Please answer y, e, or n."
                    ;;
            esac
        done
    fi

    # An accepted-but-emptied file would publish blank notes; fall back instead.
    if [[ -n "$AI_NOTES_FILE" && -s "$AI_NOTES_FILE" ]]; then
        # gh prepends --notes-file content above the auto-generated commit list.
        NOTES_ARGS=(--notes-file "$AI_NOTES_FILE" --generate-notes)
    elif [[ -n "$AI_NOTES_FILE" ]]; then
        rm -f "$AI_NOTES_FILE"
        AI_NOTES_FILE=""
        echo "Notes file is empty — using GitHub's generated notes only."
    fi
else
    echo "AI summary unavailable (no ANTHROPIC_API_KEY and no usable 'claude' CLI,"
    echo "or the request failed) — falling back to GitHub's generated notes."
fi

# ── Create GitHub release ─────────────────────────────────────────────────────
# Publish only the flashable images and their signatures. ELF/MAP symbol files
# are intentionally NOT uploaded (they bloat releases by ~35MB each); the
# matching symbols stay in dist/ locally and are archived by CI as workflow
# artifacts. See .github/workflows/build.yml.
echo ""
echo "Creating GitHub release $TAG..."
gh release create "$TAG" \
    --title "$TAG" \
    "${NOTES_ARGS[@]}" \
    dist/*.bin \
    dist/*.sig

echo ""
echo "Release $TAG published."
gh release view "$TAG" --json url -q .url

# Guarded as an if-block, not `[[ ... ]] && rm`: as the script's last command a
# false test would make release.sh exit non-zero on a successful release.
if [[ -n "${AI_NOTES_FILE:-}" ]]; then
    rm -f "$AI_NOTES_FILE"
fi
