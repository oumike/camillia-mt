#!/bin/bash
set -e

read -rp "Version (e.g. 1.0.0): " VERSION

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

# Build
echo "Building $TAG..."
pio run -e tdeck

BINARY=".pio/build/tdeck/firmware.bin"
if [[ ! -f "$BINARY" ]]; then
    echo "Build output not found: $BINARY"
    exit 1
fi

# Stage binary with versioned name
RELEASE_BIN="camillia-mt-${TAG}.bin"
cp "$BINARY" "$RELEASE_BIN"

# Create GitHub release and attach firmware + flash script
gh release create "$TAG" \
    --title "$TAG" \
    --draft \
    "$RELEASE_BIN" \
    "flash.sh"

rm "$RELEASE_BIN"

echo "Draft release $TAG created. Review and publish at:"
gh release view "$TAG" --web 2>/dev/null || echo "https://github.com/oumike/camillia-mt/releases/tag/$TAG"
