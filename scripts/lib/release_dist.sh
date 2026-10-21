# shellcheck shell=bash
# `release.sh --dist-only`: the release's images, built into dist/ on this
# machine, with nothing released. Sourced by scripts/release.sh after
# scripts/lib/release_audit.sh, whose audit_find_pio it shares; not run on its
# own.
#
# It builds the working tree as it stands -- uncommitted changes included,
# which is the point of a one-off build -- through the release's own build,
# merge, signing and checks (build_release_envs, merge_sign_assets and
# verify_release_assets in release.sh), so dist/ comes out in the release's
# layout. It never commits, tags, pushes or publishes, and never talks to
# GitHub. Nor does it generate a signing key or rewrite the public-key header:
# it signs with the key already here, or not at all.
#
# VERSION is the one tracked file it touches. The build bakes it into the
# firmware, so it is rewritten for the run and put back afterwards, however the
# run ends.
#
# Bash 3.2 (macOS's stock bash).

# The release workflow merges with esptool 4.11.0, pinned because esptool 5
# renamed merge_bin and its options. The merge here uses the same version, from
# a venv of its own, so the factory images come out as the release's do.
DIST_ESPTOOL_VERSION="4.11.0"
DIST_TAG=""
DIST_FILE_TAG=""        # DIST_TAG without +build metadata: the file names' version
DIST_TMP=""
DIST_ENVS=()
DIST_STARTED=false      # past the checks: a failure from here on is a failed build
DIST_VERSION_SET=false  # VERSION rewritten; dist_cleanup puts it back

dist_cleanup() {
    local status=$?
    if [[ "$DIST_VERSION_SET" == true ]]; then
        cp -p "$DIST_TMP/VERSION" VERSION
        echo "VERSION restored to $(cat VERSION)."
    fi
    if [[ "$DIST_STARTED" == true && $status -ne 0 ]]; then
        echo "" >&2
        echo "Dist build failed (exit $status). Nothing was committed, tagged or published." >&2
    fi
    if [[ -n "$DIST_TMP" ]]; then
        rm -rf "$DIST_TMP"
    fi
    # release.sh runs from a temporary copy of itself; this exit is its exit.
    rm -rf "${CAMILLIA_RELEASE_COPY:-/nonexistent}"
}

# True when HEAD is the commit that tag $1 points at.
dist_head_is_tag() {
    local tagged
    tagged="$(git rev-parse -q --verify "refs/tags/$1^{commit}" 2>/dev/null)" || return 1
    [[ "$tagged" == "$(git rev-parse HEAD)" ]]
}

# The version the images carry, with its leading 'v'. --version wins. Without
# it: VERSION as it reads, on the tagged release commit with nothing changed;
# otherwise VERSION plus build metadata naming the commit, and ".custom" when
# uncommitted changes go in -- so a one-off build never passes for a release.
#
# Build metadata ('+') rather than a prerelease suffix ('-') because of what
# the firmware makes of them. Anything after a '-' marks an alpha, and a device
# running one on the Auto release channel follows the alpha track, offered
# whatever the newest alpha is. A '+' leaves it on stable, offered the next
# release as usual. The device compares only the numbers in a version, and the
# metadata's can only add to them, so it is never offered the very version it
# was built from.
dist_resolve_version() {
    local dirty="$1" base
    if [[ -n "$VERSION_ARG" ]]; then
        echo "v${VERSION_ARG#v}"
        return 0
    fi
    base="$(tr -d '[:space:]' < VERSION)"
    base="v${base#v}"
    base="${base%%+*}"
    if [[ -n "$dirty" ]]; then
        echo "${base}+$(git rev-parse --short=7 HEAD).custom"
    elif dist_head_is_tag "$base"; then
        echo "$base"
    else
        echo "${base}+$(git rev-parse --short=7 HEAD)"
    fi
}

# Sets DIST_ENVS: the environments --only names, in release order, or else
# every release environment.
dist_pick_envs() {
    local requested=() want env_name found
    DIST_ENVS=()
    if [[ -z "$ONLY_ARG" ]]; then
        DIST_ENVS=( "${RELEASE_ENVS[@]}" )
        return 0
    fi
    IFS=',' read -r -a requested <<< "$ONLY_ARG"
    for want in "${requested[@]}"; do
        found=false
        for env_name in "${RELEASE_ENVS[@]}"; do
            if [[ "$env_name" == "$want" ]]; then found=true; fi
        done
        if [[ "$found" != true ]]; then
            echo "--only: '$want' is not a release environment. Choose from:" >&2
            echo "  ${RELEASE_ENVS[*]}" >&2
            exit 1
        fi
    done
    for env_name in "${RELEASE_ENVS[@]}"; do
        for want in "${requested[@]}"; do
            if [[ "$want" == "$env_name" ]]; then
                DIST_ENVS+=( "$env_name" )
                break
            fi
        done
    done
}

# Decides whether the images are signed. The public-key header is baked into
# every image, and a device running one checks each later OTA update against
# it: built from any other key, it would refuse every official update until
# reflashed over USB. So the header must be the committed one, and a key here
# must be its private half. Without a key the images are simply not signed.
dist_check_signing() {
    local fresh baked
    if ! git diff --quiet HEAD -- "$PUBKEY_HEADER"; then
        echo "Error: $PUBKEY_HEADER differs from the committed copy." >&2
        echo "Every image bakes it in, and a device checks each later OTA update" >&2
        echo "against it: built from an edited header, devices would refuse all" >&2
        echo "official updates. Restore it (git checkout -- $PUBKEY_HEADER) and rerun." >&2
        exit 1
    fi
    if [[ ! -f "$SIGNING_KEY" ]]; then
        SIGN_ASSETS=false
        return 0
    fi
    if ! command -v openssl >/dev/null 2>&1; then
        echo "Error: openssl is required to sign with $SIGNING_KEY." >&2
        exit 1
    fi
    # The same comparison the release workflow makes before it builds.
    fresh="$(openssl ec -in "$SIGNING_KEY" -pubout 2>/dev/null | grep -v -- '-----' | tr -d '\n')"
    baked="$(grep -oE '"[A-Za-z0-9+/=]+' "$PUBKEY_HEADER" | tr -d '"' | tr -d '\n')"
    if [[ -z "$fresh" || "$fresh" != "$baked" ]]; then
        echo "Error: $SIGNING_KEY is not the key $PUBKEY_HEADER was made from." >&2
        echo "Devices would reject OTA images signed with it. Use the release" >&2
        echo "signing key, or move this one aside to build unsigned images." >&2
        exit 1
    fi
    SIGN_ASSETS=true
}

# Points ESPTOOL at esptool $DIST_ESPTOOL_VERSION, installed on first use into
# a venv of its own (a one-time download from PyPI).
dist_setup_esptool() {
    local venv="${XDG_CACHE_HOME:-$HOME/.cache}/camillia-mt/esptool-$DIST_ESPTOOL_VERSION"
    local have=""
    if [[ -x "$venv/bin/python" ]]; then
        have="$("$venv/bin/python" -c 'import esptool; print(esptool.__version__)' 2>/dev/null || true)"
    fi
    if [[ "$have" != "$DIST_ESPTOOL_VERSION" ]]; then
        echo "Installing esptool $DIST_ESPTOOL_VERSION, the release workflow's version, into"
        echo "  $venv"
        rm -rf "$venv"
        mkdir -p "$(dirname "$venv")"
        if ! python3 -m venv "$venv" \
           || ! "$venv/bin/python" -m pip install --quiet --disable-pip-version-check \
                "esptool==$DIST_ESPTOOL_VERSION"; then
            rm -rf "$venv"
            echo "Error: could not install esptool $DIST_ESPTOOL_VERSION. It is a one-time" >&2
            echo "download from PyPI: check the network and python3, then rerun." >&2
            exit 1
        fi
    fi
    ESPTOOL=( "$venv/bin/python" -m esptool )
}

# A record of what went into dist/ -- for when someone asks which build they
# were given -- and checksums for wherever the images are uploaded.
dist_write_info() {
    local dirty="$1"
    shift
    {
        echo "Camillia-MT $DIST_TAG"
        echo "Built:   $(date -u '+%Y-%m-%d %H:%M UTC')"
        echo "Commit:  $(git rev-parse HEAD) ($(git rev-parse --abbrev-ref HEAD))"
        echo "Boards:  $*"
        if [[ "$SIGN_ASSETS" == true ]]; then
            echo "Signed:  yes"
        else
            echo "Signed:  no"
        fi
        if [[ -n "$dirty" ]]; then
            echo ""
            echo "Built with uncommitted changes:"
            printf '%s\n' "$dirty" | sed 's/^/  /'
        fi
    } > dist/BUILD-INFO.txt
    if command -v shasum >/dev/null 2>&1; then
        (cd dist && shasum -a 256 -- *.bin) > dist/SHA256SUMS
    elif command -v sha256sum >/dev/null 2>&1; then
        (cd dist && sha256sum -- *.bin) > dist/SHA256SUMS
    fi
}

dist_main() {
    local repo_root dirty
    local version_re='^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?(\+[0-9A-Za-z.]+)?$'
    trap dist_cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM

    repo_root="$(git rev-parse --show-toplevel)" || exit 1
    cd "$repo_root" || exit 1

    # The same paths a release signs with.
    SIGNING_KEY="${OTA_SIGNING_KEY_FILE:-ota_signing_key.pem}"
    PUBKEY_HEADER="src/ota_signing_pubkey.h"

    if ! validate_release_targets >/dev/null; then
        exit 1
    fi
    dist_pick_envs
    if [[ ! -f VERSION ]]; then
        echo "Error: no VERSION file in $repo_root." >&2
        exit 1
    fi

    # Read before VERSION is touched, so the run's own edit never counts.
    dirty="$(git status --porcelain)"
    DIST_TAG="$(dist_resolve_version "$dirty")"
    if [[ ! "$DIST_TAG" =~ $version_re ]]; then
        echo "Error: '$DIST_TAG' is not a version to build. Use X.Y.Z, optionally" >&2
        echo "with -prerelease and/or +build (e.g. 6.0.2+bob)." >&2
        exit 1
    fi
    # Files are named as on a GitHub release (camillia-mt-<board>-vX.Y.Z.bin).
    # The +build metadata stays in the firmware and in BUILD-INFO.txt.
    DIST_FILE_TAG="${DIST_TAG%%+*}"

    # Everything that could stop the run, before anything changes.
    if ! PIO="$(audit_find_pio)"; then
        echo "Error: platformio not found. Run: pip install platformio" >&2
        exit 1
    fi
    dist_check_signing
    dist_setup_esptool

    echo ""
    echo "Dist-only build: nothing is committed, tagged or published."
    echo "  Version:  $DIST_TAG"
    echo "  Files:    camillia-mt-<board>-${DIST_FILE_TAG}.bin"
    echo "  Commit:   $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD))"
    if [[ -n "$dirty" ]]; then
        echo "  Uncommitted changes, built in:"
        printf '%s\n' "$dirty" | sed 's/^/    /'
    fi
    echo "  Boards:   ${DIST_ENVS[*]}"
    if [[ "$SIGN_ASSETS" == true ]]; then
        echo "  Signing:  $SIGNING_KEY"
    else
        echo "  Signing:  none, as $SIGNING_KEY is not here (no .sig files)"
    fi
    echo "  Output:   dist/, replaced"
    case "$DIST_TAG" in
        *-*)
            echo ""
            echo "Note: $DIST_TAG is a prerelease version. A device running it on the"
            echo "Auto release channel follows the alpha track and is offered the"
            echo "newest alpha."
            ;;
    esac
    if [[ "$DIST_TAG" != *+* ]] && { [[ -n "$dirty" ]] || ! dist_head_is_tag "$DIST_TAG"; }; then
        echo ""
        echo "Note: $DIST_TAG has no +build metadata, so these images will call"
        echo "themselves $DIST_TAG without being built from exactly that release."
    fi
    echo ""

    DIST_STARTED=true
    DIST_TMP="$(mktemp -d "${TMPDIR:-/tmp}/camillia-dist.XXXXXX")"
    cp -p VERSION "$DIST_TMP/VERSION"
    if [[ "$(tr -d '[:space:]' < VERSION)" != "$DIST_TAG" ]]; then
        DIST_VERSION_SET=true
        echo "$DIST_TAG" > VERSION
    fi

    echo "Building firmware..."
    build_release_envs "${DIST_ENVS[@]}"

    echo ""
    echo "Merging factory images..."
    rm -rf dist
    mkdir -p dist
    merge_sign_assets "$DIST_FILE_TAG" "${DIST_ENVS[@]}"
    verify_release_assets "$DIST_FILE_TAG" "$DIST_TMP" "${DIST_ENVS[@]}"
    dist_write_info "$dirty" "${DIST_ENVS[@]}"

    echo ""
    ls -lh dist/
    echo ""
    echo "dist/ holds $DIST_TAG for ${#DIST_ENVS[@]} board(s)."
    echo "Nothing was committed, tagged or published."
    exit 0
}
