#!/usr/bin/env bash
set -e

# ── Colours ───────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'
CYN='\033[0;36m'; BLD='\033[1m'; RST='\033[0m'

die()  { echo -e "${RED}error: $*${RST}" >&2; exit 1; }
info() { echo -e "${CYN}  $*${RST}"; }
ok()   { echo -e "${GRN}  ✓ $*${RST}"; }

echo -e "\n${BLD}${CYN}Camillia MT — Release Builder${RST}\n"

# ── Preflight checks ──────────────────────────────────────────
command -v git  >/dev/null || die "git not found"
command -v gh   >/dev/null || die "GitHub CLI (gh) not found — brew install gh"

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not inside a git repository"

# Warn if there are uncommitted changes
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo -e "${YLW}  warning: you have uncommitted changes${RST}"
    read -rp "  Continue anyway? [y/N] " yn
    [[ "$yn" =~ ^[Yy]$ ]] || exit 0
fi

# ── Last tag ──────────────────────────────────────────────────
LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "none")
info "Last release: ${LAST_TAG}"

# ── Tag ───────────────────────────────────────────────────────
echo ""
read -rp "  New version tag (e.g. v0.2.0) : " TAG
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "tag must match vMAJOR.MINOR.PATCH"

git tag -l "$TAG" | grep -q "$TAG" && die "tag $TAG already exists"

# ── Release title ─────────────────────────────────────────────
read -rp "  Release title                 : " TITLE
[[ -z "$TITLE" ]] && TITLE="Camillia MT $TAG"

# ── Release notes ─────────────────────────────────────────────
echo ""
echo -e "  ${BLD}Release notes${RST} (blank line + Ctrl-D to finish):"
echo    "  ──────────────────────────────────────────────────"
NOTES=""
while IFS= read -r line; do
    NOTES+="$line"$'\n'
done
NOTES="${NOTES%$'\n'}"   # trim trailing newline

# ── Summary ───────────────────────────────────────────────────
echo ""
echo -e "${BLD}  Release summary${RST}"
echo -e "  Tag   : ${YLW}${TAG}${RST}"
echo -e "  Title : ${TAG} — ${TITLE}"
if [[ -n "$NOTES" ]]; then
    echo   "  Notes :"
    while IFS= read -r line; do
        echo "    $line"
    done <<< "$NOTES"
fi
echo ""

read -rp "  Create draft release? [y/N] " confirm
[[ "$confirm" =~ ^[Yy]$ ]] || { echo "  Aborted."; exit 0; }

# ── Tag & push ────────────────────────────────────────────────
echo ""
info "Creating tag ${TAG}..."
git tag "$TAG" -m "${TITLE}"
ok "Tag created"

info "Pushing tag to origin..."
git push origin "$TAG"
ok "Tag pushed — GitHub Actions build started"

# ── Wait for the workflow then update the draft ────────────────
info "Waiting for Actions run to appear..."
sleep 6

RUN_ID=$(gh run list --workflow=build.yml --limit=1 --json databaseId -q '.[0].databaseId')
if [[ -n "$RUN_ID" ]]; then
    info "Watching run #${RUN_ID} (Ctrl-C to detach)..."
    gh run watch "$RUN_ID" && ok "Build passed" || die "Build FAILED — release not created"
else
    echo -e "${YLW}  Could not find run — check Actions tab manually${RST}"
fi

# ── Update the draft with title and notes ─────────────────────
info "Updating draft release..."
if [[ -n "$NOTES" ]]; then
    gh release edit "$TAG" --title "${TAG} — ${TITLE}" --notes "$NOTES"
else
    gh release edit "$TAG" --title "${TAG} — ${TITLE}"
fi

ok "Draft release ready → $(gh release view "$TAG" --json url -q '.url')"
echo ""
echo -e "${BLD}  Review and publish at the URL above when ready.${RST}"
echo ""
