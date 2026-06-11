#!/bin/bash
set -e

RELEASE_ENVS=(
    tdeck
    tlora-pager-tft
    cardputer-cap
    heltec-v4
    heltec-v4-vertical
)

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

# Recreate existing release/tag when rerunning a version
if remote_tag_exists "$TAG" || git tag | grep -q "^${TAG}$"; then
    delete_existing_release_and_tags "$TAG"
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

echo "Running full clean for release environments..."
~/.platformio/penv/bin/pio run "${BUILD_ARGS[@]}" -t fullclean

~/.platformio/penv/bin/pio run "${BUILD_ARGS[@]}"
echo "Build successful."

# Commit and push all changes
git add -A
git commit -m "Release $TAG"
git push

echo "Changes committed and pushed."

# Remove stale local tag if present
if git tag | grep -q "^$TAG$"; then
    git tag -d "$TAG"
fi

git tag "$TAG"
git push origin "$TAG"

echo "Tag $TAG pushed. GitHub Actions will build and create the draft release."
echo "https://github.com/oumike/camillia-mt/actions"
