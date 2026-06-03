#!/bin/bash
set -e

RELEASE_ENVS=(
    tdeck
    tlora-pager-tft
    # cardputer-cap
    # heltec-v4
    # heltec-v4-vertical
)

has_env() {
    local env_name="$1"
    grep -q "^\[env:${env_name}\]" platformio.ini
}

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

# Check remote tags without fetching locally
if git ls-remote --tags origin | grep -q "refs/tags/$TAG$"; then
    echo "Tag $TAG already exists on remote. Aborting."
    exit 1
fi

# Update VERSION file
echo "$TAG" > VERSION
echo "Updated VERSION to $TAG"

# Build firmware
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

~/.platformio/penv/bin/pio run "${BUILD_ARGS[@]}"
echo "Build successful."

# Commit and push all changes
git add -A
git commit -m "Release $TAG"
git push

echo "Changes committed and pushed."

# Remove stale local tag if present (not on remote, so safe to recreate)
if git tag | grep -q "^$TAG$"; then
    git tag -d "$TAG"
fi

git tag "$TAG"
git push origin "$TAG"

echo "Tag $TAG pushed. GitHub Actions will build and create the draft release."
echo "https://github.com/oumike/camillia-mt/actions"
