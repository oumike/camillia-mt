# shellcheck shell=bash
# The pull-request stage of a release. Sourced by scripts/release.sh; not run on
# its own. A release is one pull request into main: this picks it, checks that it
# can be released, and merges it. See
# docs/superpowers/specs/2026-10-08-pr-based-release-design.md.
#
# Bash 3.2 (macOS's stock bash) — no associative arrays, no mapfile.

# Set by pr_load, read by release.sh.
PR_NUMBER=""
PR_TITLE=""
PR_URL=""
PR_HEAD_SHA=""
PR_HEAD_BRANCH=""
PR_CROSS_REPO=false
PR_MERGED=false        # already merged but not yet released: the resume path
PR_MERGE_SHA=""
PR_DIFF_BASE=""        # what the PR's changes are measured against

# The Build workflow's verdict on a PR's head commit, as one word. The rollup
# only carries checks for the head commit, so an older green run cannot count.
# shellcheck disable=SC2016
PR_BUILD_JQ='def build: [.statusCheckRollup[]? | select(.workflowName == "Build")] as $b
    | if ($b | length) == 0 then "none"
      elif any($b[]; .status != "COMPLETED") then "pending"
      elif all($b[]; .conclusion == "SUCCESS") then "passed"
      else "failed" end;'

# Open PRs into main as tab-separated rows:
#   number, draft (true/false), build, author, branch, title
# $1: extra `gh pr list` arguments, e.g. "--label composite" (may be empty).
pr_list_rows() {
    local extra="$1"
    # shellcheck disable=SC2086  # $extra is deliberately word-split
    gh pr list --base main --state open --limit 50 $extra \
        --json number,isDraft,title,author,headRefName,statusCheckRollup \
        --jq "$PR_BUILD_JQ"' .[] | [.number, .isDraft, build, .author.login, .headRefName, .title] | @tsv'
}

pr_print_rows() {
    local i=0 num draft build author branch title mark
    while IFS=$'\t' read -r num draft build author branch title; do
        [[ -n "$num" ]] || continue
        i=$((i + 1))
        mark=""
        [[ "$draft" == "true" ]] && mark=" [draft]"
        printf '  %2d) #%-5s %s%s\n' "$i" "$num" "$title" "$mark"
        printf '         %s · %s · build %s\n' "$author" "$branch" "$build"
    done <<< "$1"
}

# Picks one PR from rows (pr_list_rows output) and prints its number. One row
# is confirmed rather than chosen; several get a menu. Unattended runs cannot
# answer either, so they must say which PR with --pr.
# $1: rows   $2: what the rows are, for messages ("ready PR", "open PR")
pr_pick_from_rows() {
    local rows="$1" what="$2" count num choice ans
    count=$(printf '%s\n' "$rows" | grep -c . || true)
    if [[ "$count" -eq 1 ]]; then
        num=$(printf '%s\n' "$rows" | cut -f1)
        echo "One $what:" >&2
        pr_print_rows "$rows" >&2
        if [[ -t 0 && "$ASSUME_YES" != true ]]; then
            read -rp "Use #$num? [Y/n]: " ans || ans="n"
            case "${ans:-y}" in
                y|Y|yes|Yes) ;;
                *) echo "Aborted." >&2; return 1 ;;
            esac
        fi
        echo "$num"
        return 0
    fi
    if [[ ! -t 0 || "$ASSUME_YES" == true ]]; then
        echo "$count ${what}s — pass --pr N to pick one:" >&2
        pr_print_rows "$rows" >&2
        return 1
    fi
    echo "$count ${what}s:" >&2
    pr_print_rows "$rows" >&2
    while true; do
        read -rp "Which one? [1-$count, or q]: " choice || return 1
        case "$choice" in
            q|Q) echo "Aborted." >&2; return 1 ;;
        esac
        if [[ "$choice" =~ ^[0-9]+$ && "$choice" -ge 1 && "$choice" -le "$count" ]]; then
            printf '%s\n' "$rows" | sed -n "${choice}p" | cut -f1
            return 0
        fi
        echo "Enter a number from 1 to $count." >&2
    done
}

# The PR a release should use: open, not a draft, into main.
pr_choose_for_release() {
    local rows
    if ! rows=$(pr_list_rows ""); then
        echo "Could not list pull requests. Is gh authenticated? (gh auth status)" >&2
        return 1
    fi
    rows=$(printf '%s\n' "$rows" | awk -F'\t' '$2 == "false"')
    if [[ -z "$rows" ]]; then
        echo "No pull request into main is ready to release." >&2
        echo "Mark one ready with:  gh pr ready <number>" >&2
        echo "(Or release main as it stands with --from-main.)" >&2
        return 1
    fi
    pr_pick_from_rows "$rows" "ready PR"
}

# The PR an audit should use: the one composite PR, otherwise any open PR into
# main, drafts included — auditing before marking a PR ready is the point.
pr_choose_for_audit() {
    local rows
    if ! rows=$(pr_list_rows "--label composite"); then
        echo "Could not list pull requests. Is gh authenticated? (gh auth status)" >&2
        return 1
    fi
    if [[ $(printf '%s\n' "$rows" | grep -c . || true) -eq 1 ]]; then
        printf '%s\n' "$rows" | cut -f1
        return 0
    fi
    rows=$(pr_list_rows "") || return 1
    if [[ -z "$rows" ]]; then
        echo "No open pull request into main to audit." >&2
        return 1
    fi
    pr_pick_from_rows "$rows" "open PR"
}

# Loads PR $1 into the PR_* variables and fetches its head commit, which works
# for a fork's PR as well as a branch in this repo.
pr_load() {
    local num="$1" json state base
    if ! json=$(gh pr view "$num" --json \
            number,title,url,state,baseRefName,headRefName,headRefOid,isCrossRepository,mergeCommit); then
        echo "Could not read pull request #$num." >&2
        return 1
    fi
    PR_NUMBER=$(jq -r .number <<< "$json")
    PR_TITLE=$(jq -r .title <<< "$json")
    PR_URL=$(jq -r .url <<< "$json")
    PR_HEAD_SHA=$(jq -r .headRefOid <<< "$json")
    PR_HEAD_BRANCH=$(jq -r .headRefName <<< "$json")
    PR_CROSS_REPO=$(jq -r .isCrossRepository <<< "$json")
    PR_MERGE_SHA=$(jq -r '.mergeCommit.oid // ""' <<< "$json")
    state=$(jq -r .state <<< "$json")
    base=$(jq -r .baseRefName <<< "$json")

    if [[ "$base" != "main" ]]; then
        echo "#$PR_NUMBER targets '$base', not main." >&2
        return 1
    fi
    case "$state" in
        OPEN)   PR_MERGED=false ;;
        MERGED) PR_MERGED=true ;;
        *)      echo "#$PR_NUMBER is $state." >&2; return 1 ;;
    esac

    git fetch -q origin main "pull/$PR_NUMBER/head" || {
        echo "Could not fetch #$PR_NUMBER's commits." >&2
        return 1
    }
    if [[ "$PR_MERGED" == true ]]; then
        PR_DIFF_BASE="${PR_MERGE_SHA}^1"
    else
        PR_DIFF_BASE="origin/main"
    fi
}

# Everything that has to be true before an open PR is released. Stops at the
# first thing that is not, saying what it is.
pr_gate_open() {
    local json draft build mergeable tries=0
    json=$(gh pr view "$PR_NUMBER" --json isDraft,headRefOid,statusCheckRollup,mergeable \
               --jq "$PR_BUILD_JQ"' {isDraft, head: .headRefOid, build: build, mergeable}') || return 1
    draft=$(jq -r .isDraft <<< "$json")
    build=$(jq -r .build <<< "$json")
    if [[ "$draft" == "true" ]]; then
        echo "#$PR_NUMBER is still a draft. Mark it ready with:  gh pr ready $PR_NUMBER" >&2
        return 1
    fi
    if [[ "$(jq -r .head <<< "$json")" != "$PR_HEAD_SHA" ]]; then
        echo "#$PR_NUMBER moved while this ran. Rerun to pick up its new head." >&2
        return 1
    fi
    case "$build" in
        passed) ;;
        pending) echo "#$PR_NUMBER's Build check is still running: $PR_URL/checks" >&2; return 1 ;;
        failed)  echo "#$PR_NUMBER's Build check failed: $PR_URL/checks" >&2; return 1 ;;
        *)       echo "#$PR_NUMBER has no Build check on its latest commit: $PR_URL/checks" >&2; return 1 ;;
    esac
    # GitHub works mergeability out lazily, so the first answer is often
    # UNKNOWN. Ask again for a little while before giving up on it.
    while true; do
        mergeable=$(gh pr view "$PR_NUMBER" --json mergeable -q .mergeable 2>/dev/null || echo UNKNOWN)
        [[ "$mergeable" != "UNKNOWN" || $tries -ge 5 ]] && break
        tries=$((tries + 1))
        sleep 3
    done
    case "$mergeable" in
        MERGEABLE) ;;
        CONFLICTING)
            echo "#$PR_NUMBER conflicts with main. Resolve it on the branch, then rerun." >&2
            return 1 ;;
        *)
            echo "GitHub could not say whether #$PR_NUMBER merges cleanly ($mergeable). Rerun shortly." >&2
            return 1 ;;
    esac
}

# A merged PR is only releasable here as a resume: its merge reached main and no
# release contains it yet.
pr_gate_merged() {
    local tagged
    if [[ -z "$PR_MERGE_SHA" ]] || ! git merge-base --is-ancestor "$PR_MERGE_SHA" origin/main; then
        echo "#$PR_NUMBER is merged, but its merge commit is not on main." >&2
        return 1
    fi
    tagged=$(git tag --contains "$PR_MERGE_SHA" 2>/dev/null | grep -E '^v[0-9]' | sort -V | head -1 || true)
    if [[ -n "$tagged" ]]; then
        echo "#$PR_NUMBER is already released, in $tagged." >&2
        return 1
    fi
    echo "#$PR_NUMBER is merged but not released yet — resuming its release."
}

# Merges the PR as a merge commit, but only if its head is still the commit
# whose build passed. The branch is deleted on the remote afterwards when it
# lives in this repo; a local copy, if any, is left alone, since it may hold
# work that was never pushed.
pr_merge() {
    gh pr merge "$PR_NUMBER" --merge --match-head-commit "$PR_HEAD_SHA" || {
        echo "Merging #$PR_NUMBER failed. If its head moved, rerun to pick it up." >&2
        return 1
    }
    if [[ "$PR_CROSS_REPO" != "true" && -n "$PR_HEAD_BRANCH" ]]; then
        git push -q origin --delete "$PR_HEAD_BRANCH" 2>/dev/null \
            || echo "(Could not delete branch $PR_HEAD_BRANCH on origin; delete it by hand.)"
    fi
    PR_MERGED=true
}

# What the release notes are written from: the PR's own description and commit
# list, and its diff against main. Prints the model prompt's PR section.
pr_notes_material() {
    local body commits diffstat diff
    body=$(gh pr view "$PR_NUMBER" --json body -q .body 2>/dev/null || true)
    commits=$(gh pr view "$PR_NUMBER" --json commits \
                  -q '.commits[] | "- " + .messageHeadline' 2>/dev/null | head -200 || true)
    diffstat=$(git diff --stat "${PR_DIFF_BASE}...${PR_HEAD_SHA}" 2>/dev/null | tail -40 || true)
    diff=$(git diff "${PR_DIFF_BASE}...${PR_HEAD_SHA}" --unified=0 \
              -- . ':(exclude)dist' ':(exclude)*.bin' ':(exclude)*.sig' \
              2>/dev/null | head -2000 || true)
    cat <<EOF
Pull request #${PR_NUMBER}: ${PR_TITLE}

Pull request description (written for reviewers; the authoritative account of
what changed and why):
${body}

Commits in the pull request:
${commits}

Files changed:
${diffstat}

Diff (zero context, truncated):
${diff}
EOF
}
