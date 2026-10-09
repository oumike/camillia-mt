# shellcheck shell=bash
# `release.sh --audit`: a full audit of one pull request, reported on the PR.
# Sourced by scripts/release.sh after scripts/lib/release_pr.sh; not run on its
# own. It builds every release environment and has Claude review the change, in
# a throwaway worktree of the PR's head commit, then posts what it found as a PR
# comment. It never merges, commits, pushes or releases.
#
# Bash 3.2 (macOS's stock bash).

AUDIT_WT=""
AUDIT_WT_PARENT=""

audit_cleanup() {
    if [[ -n "$AUDIT_WT" && -d "$AUDIT_WT" ]]; then
        git worktree remove --force "$AUDIT_WT" 2>/dev/null || true
    fi
    [[ -n "$AUDIT_WT_PARENT" ]] && rm -rf "$AUDIT_WT_PARENT"
    git worktree prune 2>/dev/null || true
    # release.sh runs from a temporary copy of itself; this exit is its exit.
    rm -rf "${CAMILLIA_RELEASE_COPY:-/nonexistent}"
}

audit_find_pio() {
    if [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
        echo "$HOME/.platformio/penv/bin/pio"
    elif command -v pio >/dev/null 2>&1; then
        echo "pio"
    else
        return 1
    fi
}

# Builds every release environment in the worktree and runs the flash headroom
# check over the images. Appends a results section to $1 (the report) and
# returns non-zero if anything failed. Builds keep going past a failure so the
# report covers every board.
audit_builds() {
    local report="$1" logdir="$2" env_name log failed=0 built=0 status
    {
        echo "### Builds"
        echo ""
        echo "| Environment | Result |"
        echo "|---|---|"
    } >> "$report"

    local failures=""
    for env_name in "${RELEASE_ENVS[@]}"; do
        if ! has_env "$env_name"; then
            echo "| \`$env_name\` | missing from platformio.ini |" >> "$report"
            failed=$((failed + 1))
            continue
        fi
        log="$logdir/build-$env_name.log"
        printf '  building %-20s ' "$env_name"
        # --disable-auto-clean for the same reason release.sh uses it: an
        # auto-clean partway through would delete the images already built, and
        # the headroom check below would then find nothing to measure.
        if run_pio_for_env "$env_name" run -e "$env_name" --disable-auto-clean > "$log" 2>&1; then
            echo "ok"
            echo "| \`$env_name\` | ✅ built |" >> "$report"
            built=$((built + 1))
        else
            echo "FAILED (log: $log)"
            echo "| \`$env_name\` | ❌ failed |" >> "$report"
            failed=$((failed + 1))
            failures="$failures $env_name"
        fi
    done
    echo "" >> "$report"

    for env_name in $failures; do
        {
            echo "<details><summary><code>$env_name</code> build log (last 40 lines)</summary>"
            echo ""
            echo '```'
            tail -40 "$logdir/build-$env_name.log"
            echo '```'
            echo "</details>"
            echo ""
        } >> "$report"
    done

    echo "### Flash headroom" >> "$report"
    echo "" >> "$report"
    if [[ $failed -gt 0 ]]; then
        echo "Not run: some environments did not build." >> "$report"
    else
        echo "  flash headroom check"
        {
            echo '```'
            if python3 tools/flash_headroom.py --min-free 65536 --require-all 2>&1; then
                status=0
            else
                status=1
            fi
            echo '```'
        } > "$logdir/headroom.md"
        cat "$logdir/headroom.md" >> "$report"
        if [[ "${status:-1}" -ne 0 ]]; then
            echo "" >> "$report"
            echo "❌ Below the 64 KB minimum free (or an image is missing)." >> "$report"
            failed=$((failed + 1))
        fi
    fi
    echo "" >> "$report"
    AUDIT_BUILD_SUMMARY="$built/${#RELEASE_ENVS[@]} environments built"
    [[ $failed -eq 0 ]]
}

# Has Claude review the PR in the worktree, read-only, and appends its findings
# to $1. Sets AUDIT_BLOCKING to the number of blocking findings, "skipped" when
# there is no claude CLI, or "?" when the review failed or its count could not
# be read.
audit_review() {
    local report="$1" logdir="$2" body commits prompt out
    AUDIT_BLOCKING="?"
    echo "### Review" >> "$report"
    echo "" >> "$report"
    if ! command -v claude >/dev/null 2>&1; then
        echo "Skipped: the \`claude\` CLI is not installed on the machine that ran the audit." >> "$report"
        echo "  review skipped (no claude CLI)"
        AUDIT_BLOCKING="skipped"
        return 0
    fi

    body=$(gh pr view "$PR_NUMBER" --json body -q .body 2>/dev/null || true)
    commits=$(gh pr view "$PR_NUMBER" --json commits \
                  -q '.commits[] | "- " + .oid[0:7] + " " + .messageHeadline' 2>/dev/null || true)

    prompt="You are auditing pull request #${PR_NUMBER} of Camillia-MT, Meshtastic-compatible
firmware for ESP32 handheld LoRa devices. Many boards build from shared sources
(src/main_lvgl.cpp above all), with board-specific code gated on DEVICE_* macros
and capability macros in src/config.h and src/hal/board.h.

The current directory is a checkout of the PR's head commit. Its changes are the
range ${PR_DIFF_BASE}...HEAD: inspect them with git diff / git log / git show,
and read any file you need for context. Do not modify anything.

PR title: ${PR_TITLE}

PR description:
${body}

Commits:
${commits}

Review the whole change for:
1. Correctness bugs in the changed code: logic errors, null/bounds/overflow
   issues, resource leaks, broken error paths.
2. Cross-board regressions: shared code changed without the right board gating,
   or a board-specific change leaking into other builds.
3. UI strings that do not go through TR(), or new strings missing from the
   translations under lang/.
4. Documentation, README or the PR description out of step with the code.
5. Risky changes to OTA, signing, the config layout (RhinoConfig size and field
   offsets), or storage formats.

Only report problems you have verified by reading the code. No praise, no
summary of the change, no style preferences.

Output GitHub-flavoured markdown and nothing else, in exactly this shape:

#### Blocking
- \`path/to/file.cpp:123\` — what is wrong. Why it matters.

#### Should fix
- ...

#### Nit
- ...

Write 'None.' under a heading with no findings. Blocking means it must be fixed
before release (a crash, data loss, a broken board, a security problem). End
with one final line, exactly: Blocking findings: <number>"

    echo "  reviewing with claude (read-only)"
    out=$(cd "$AUDIT_WT" && claude -p "$prompt" \
              --allowedTools "Read,Grep,Glob,Bash(git diff:*),Bash(git log:*),Bash(git show:*)" \
              2> "$logdir/review.err") || true
    if [[ -z "${out//[[:space:]]/}" ]]; then
        echo "Review failed: claude returned nothing (see \`$logdir/review.err\` on the machine that ran it)." >> "$report"
        return 0
    fi
    printf '%s\n' "$out" >> "$report"
    echo "" >> "$report"
    # Tolerates the line coming back bolded or indented.
    AUDIT_BLOCKING=$(printf '%s\n' "$out" | tr -d '*_' \
                         | sed -n 's/^[[:space:]]*Blocking findings: *\([0-9][0-9]*\).*/\1/p' | tail -1)
    [[ -n "$AUDIT_BLOCKING" ]] || AUDIT_BLOCKING="?"
}

audit_main() {
    local num repo_root sha7 report logdir builds_ok=true comment
    if ! command -v gh >/dev/null 2>&1; then
        echo "Error: GitHub CLI (gh) is required for --audit." >&2
        exit 1
    fi
    repo_root=$(git rev-parse --show-toplevel)
    cd "$repo_root"

    if [[ -n "$PR_ARG" ]]; then
        num="$PR_ARG"
    else
        num=$(pr_choose_for_audit) || exit 1
    fi
    pr_load "$num" || exit 1
    if [[ "$PR_MERGED" == true ]]; then
        echo "#$PR_NUMBER is already merged; audit an open PR." >&2
        exit 1
    fi
    sha7="${PR_HEAD_SHA:0:7}"
    echo "Auditing #$PR_NUMBER \"$PR_TITLE\" at $sha7"

    mkdir -p logs
    report="$repo_root/logs/audit-pr${PR_NUMBER}-${sha7}.md"
    logdir="$repo_root/logs/audit-pr${PR_NUMBER}-${sha7}"
    rm -rf "$logdir"
    mkdir -p "$logdir"

    AUDIT_WT_PARENT=$(mktemp -d "${TMPDIR:-/tmp}/camillia-audit.XXXXXX")
    AUDIT_WT="$AUDIT_WT_PARENT/pr-$PR_NUMBER"
    trap audit_cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    git worktree add -q --detach "$AUDIT_WT" "$PR_HEAD_SHA"

    {
        echo "## Audit of #$PR_NUMBER at \`$sha7\`"
        echo ""
        echo "Run by \`scripts/release.sh --audit\` against $PR_DIFF_BASE ($(git rev-parse --short origin/main))."
        echo ""
    } > "$report"

    AUDIT_BUILD_SUMMARY="builds skipped (--skip-builds)"
    if [[ "$SKIP_BUILDS" == true ]]; then
        printf '### Builds\n\nSkipped (`--skip-builds`). CI builds every push to the PR.\n\n' >> "$report"
    else
        PIO=$(audit_find_pio) || {
            echo "Error: platformio not found (needed for builds; --skip-builds to review only)." >&2
            exit 1
        }
        echo "Building in $AUDIT_WT"
        if ! (cd "$AUDIT_WT" && audit_builds "$report" "$logdir"); then
            builds_ok=false
        fi
        # The summary is set in the subshell; recount it from the report.
        AUDIT_BUILD_SUMMARY="$(grep -c '✅ built' "$report" || true)/${#RELEASE_ENVS[@]} environments built"
    fi

    audit_review "$report" "$logdir"

    echo ""
    echo "Report: $report"
    # GitHub caps a comment at 65536 characters; a long build log section is
    # what would push it over, and the full report stays on disk.
    comment="$logdir/comment.md"
    if [[ $(wc -c < "$report") -gt 60000 ]]; then
        head -c 60000 "$report" > "$comment"
        printf '\n\n_(Truncated. The full report is %s on the machine that ran the audit.)_\n' \
            "logs/audit-pr${PR_NUMBER}-${sha7}.md" >> "$comment"
    else
        cp "$report" "$comment"
    fi
    if gh pr comment "$PR_NUMBER" --body-file "$comment" >/dev/null; then
        echo "Posted to $PR_URL"
    else
        echo "Could not post the report to #$PR_NUMBER; it is at $report." >&2
    fi

    echo ""
    echo "Summary: $AUDIT_BUILD_SUMMARY; blocking findings: $AUDIT_BLOCKING"
    # A skipped review does not fail the audit; a failed or unreadable one does.
    if [[ "$builds_ok" != true || ( "$AUDIT_BLOCKING" != "0" && "$AUDIT_BLOCKING" != "skipped" ) ]]; then
        [[ "$AUDIT_BLOCKING" == "?" && "$builds_ok" == true ]] \
            && echo "(The review's findings could not be counted; read the report.)"
        exit 1
    fi
    exit 0
}
