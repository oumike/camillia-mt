#!/bin/bash
set -e

# Branch that alpha (prerelease) builds must be cut from. Alphas are disposable
# test builds; keeping them on a dedicated branch keeps main's history clean.
ALPHA_BRANCH="alpha"

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

# Bump the patch component of an X.Y.Z version. Input may carry a leading "v"
# and/or a prerelease suffix (e.g. v3.1.2-alpha.4); both are stripped first.
# Echoes the bumped X.Y.Z (no "v"), or an empty string if the shape isn't X.Y.Z.
next_patch_version() {
    local v="${1#v}"
    v="${v%%-*}"
    local major minor patch rest
    IFS='.' read -r major minor patch rest <<< "$v"
    if [[ -z "$major" || -z "$minor" || -z "$patch" || -n "$rest" \
          || ! "$patch" =~ ^[0-9]+$ ]]; then
        echo ""
        return
    fi
    echo "${major}.${minor}.$((patch + 1))"
}

# Base X.Y.Z that alphas should target. If we're already mid-alpha-cycle
# (VERSION is vX.Y.Z-alpha.N) keep that X.Y.Z so successive alphas iterate the
# same patch; otherwise (coming from a stable vX.Y.Z) bump to the next patch.
alpha_base_version() {
    local v="${1#v}"
    if [[ "$v" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-alpha\.[0-9]+$ ]]; then
        echo "${BASH_REMATCH[1]}"
    else
        next_patch_version "$1"
    fi
}

# Next available alpha tag for a base version (e.g. base "3.1.2" -> the highest
# existing v3.1.2-alpha.N across local and remote tags, plus one).
next_alpha_tag() {
    local base="$1"
    local max=0 n
    while IFS= read -r t; do
        n="${t#v${base}-alpha.}"
        if [[ "$n" =~ ^[0-9]+$ ]] && (( n > max )); then max="$n"; fi
    done < <(
        { git tag; git ls-remote --tags origin 2>/dev/null | sed 's#.*refs/tags/##'; } \
            | grep -E "^v${base}-alpha\.[0-9]+$" | sort -u
    )
    echo "v${base}-alpha.$((max + 1))"
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
ALPHA=false
for arg in "$@"; do
    case "$arg" in
        --alpha) ALPHA=true ;;
        -h|--help)
            echo "Usage: $0 [--alpha]"
            echo "  --alpha   Cut a prerelease (alpha) build. Must be run from the"
            echo "            '$ALPHA_BRANCH' branch. Tags v<next-patch>-alpha.N and"
            echo "            publishes a GitHub prerelease (excluded from OTA/latest)."
            exit 0
            ;;
        *) echo "Unknown argument: $arg (see --help)" >&2; exit 1 ;;
    esac
done

# ── Alpha branch guard ────────────────────────────────────────────────────────
if [[ "$ALPHA" == true ]]; then
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "")
    if [[ "$CURRENT_BRANCH" != "$ALPHA_BRANCH" ]]; then
        echo "Error: --alpha releases must be cut from the '$ALPHA_BRANCH' branch." >&2
        echo "Current branch is '${CURRENT_BRANCH:-unknown}'." >&2
        echo "Switch with: git checkout $ALPHA_BRANCH" >&2
        exit 1
    fi
fi

# ── Version prompt ────────────────────────────────────────────────────────────
CURRENT=$(cat VERSION 2>/dev/null | tr -d '\n')
PREV_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "none")
echo "Current version: ${CURRENT:-unknown}"
echo "Latest git tag:  $PREV_TAG"
echo ""

if [[ "$ALPHA" == true ]]; then
    BASE=$(alpha_base_version "$CURRENT")
    if [[ -z "$BASE" ]]; then
        echo "Could not derive the next patch version from '$CURRENT'."
        read -rp "Base version for this alpha (e.g. 3.1.2): " BASE
        BASE="${BASE#v}"
        if [[ -z "$BASE" ]]; then
            echo "No base version entered. Aborting."
            exit 1
        fi
    fi
    DEFAULT_TAG=$(next_alpha_tag "$BASE")
    read -rp "New alpha version [${DEFAULT_TAG}]: " TAG
    TAG="${TAG:-$DEFAULT_TAG}"
    [[ "$TAG" == v* ]] || TAG="v$TAG"
else
    read -rp "New version (e.g. 1.0.0): " VERSION
    if [[ -z "$VERSION" ]]; then
        echo "No version entered. Aborting."
        exit 1
    fi
    TAG="v$VERSION"
fi

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
if [[ "$ALPHA" == true ]]; then
    git commit -m "Alpha release $TAG"
    git push origin "$ALPHA_BRANCH"
else
    git commit -m "Release $TAG"
    git push
fi

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
        -fm dio \
        -ff 80m \
        -fs "${flash_size}" \
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
if [[ "$ALPHA" == true ]]; then
    # --prerelease keeps alphas out of GitHub's /releases/latest, so on-device
    # OTA checks and the website's stable flasher never see them.
    gh release create "$TAG" \
        --title "$TAG (alpha)" \
        --prerelease \
        --generate-notes \
        dist/*.bin \
        dist/*.elf
else
    gh release create "$TAG" \
        --title "$TAG" \
        --generate-notes \
        dist/*.bin \
        dist/*.elf
fi

echo ""
echo "Release $TAG published."
gh release view "$TAG" --json url -q .url
