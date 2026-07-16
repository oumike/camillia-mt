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
echo ""
echo "Merging factory images..."
rm -rf dist
mkdir -p dist

for env_name in "${RELEASE_ENVS[@]}"; do
    if ! has_env "$env_name"; then continue; fi
    out_name="$(env_out_name "$env_name")"
    flash_size="$(env_flash_size "$env_name")"
    d=".pio/build/${env_name}"
    out="dist/camillia-mt-${out_name}-${TAG}.bin"
    ota_out="dist/camillia-mt-${out_name}-${TAG}-ota.bin"
    echo "  ${env_name} (${flash_size}) -> ${out}"
    $ESPTOOL --chip esp32s3 merge_bin \
        -o "${out}" \
        --flash-mode dio \
        --flash-freq 80m \
        --flash-size "${flash_size}" \
        0x0     "${d}/bootloader.bin" \
        0x8000  "${d}/partitions.bin" \
        0xe000  "${BOOT_APP0}" \
        0x10000 "${d}/firmware.bin"
    cp "${d}/firmware.bin" "${ota_out}"
    cp "${d}/firmware.elf" "dist/camillia-mt-${out_name}-${TAG}.elf"
done

ls -lh dist/

# ── Create GitHub release ─────────────────────────────────────────────────────
echo ""
echo "Creating GitHub release $TAG..."
gh release create "$TAG" \
    --title "$TAG" \
    --generate-notes \
    dist/*.bin \
    dist/*.elf

echo ""
echo "Release $TAG published."
gh release view "$TAG" --json url -q .url
