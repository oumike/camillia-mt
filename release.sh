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

# Remove stale local tag if present (not on remote, so safe to recreate)
if git tag | grep -q "^$TAG$"; then
    git tag -d "$TAG"
fi

git tag "$TAG"
git push origin "$TAG"

echo "Tag $TAG pushed. GitHub Actions will build and create the draft release."
echo "https://github.com/oumike/camillia-mt/actions"
