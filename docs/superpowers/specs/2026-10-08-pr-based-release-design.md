# PR-based development and release — design

Date: 2026-10-08
Status: approved in conversation; awaiting review of this written spec

## Goal

Move Camillia-MT from "work lands on `main`, `release.sh` releases whatever is
there" to "every change goes through a pull request, and a release *is* a pull
request". Other developers will contribute from time to time, and CI/CD should
be organised around PRs.

Success means:

- Firmware changes made with Claude accumulate in one open **composite PR**,
  whose description Claude keeps current as a user-readable changelog.
- `./scripts/release.sh` releases one PR: it uses the only ready PR
  automatically, or asks which one when several are ready.
- Release notes are summarised from that PR, in the style generated today, and
  reference the PR.
- Contributors' PRs follow the same release path with no special handling.

## Current state (for reference)

- `scripts/release.sh` (default "remote" mode) syncs the branch, derives the
  version, has Claude write `RELEASE_NOTES.md` from `git log <last tag>..HEAD`,
  shows the notes for Yes / edit / no review, then `git add -A`, commits
  "Prepare release vX [skip ci]", pushes, and dispatches
  `.github/workflows/release.yml`.
- `release.yml` builds every environment, signs the OTA images, commits
  "Release vX", tags and publishes. It bakes `RELEASE_NOTES.md` into the
  firmware (`tools/gen_release_notes.py`).
- `build.yml` builds every environment on pushes to `main` and on PRs into
  `main`. It does not publish anything.
- `main` has no branch protection; `alpha` has a ruleset. Merge commits, squash
  and rebase merges are all allowed. There is no repo `CLAUDE.md`.

## Decisions

| Question | Decision |
|---|---|
| Overall approach | **A**: `release.sh` gains a PR stage; `release.yml` and `build.yml` are unchanged. |
| PR state at release time | **Open.** The release merges it. One PR = one release. |
| "Ready to release" signal | **Not a draft, and the `Build` check on its head commit passed.** |
| Which PR Claude adds to | **The one open PR labelled `composite`**; create it if none, ask if several. |
| Merge method | **Merge commit**, matching history (e.g. #105); keeps each change's commits. |
| Notes reference | A closing line `Pull request: #N` (GitHub links it; reads cleanly on the device). |

Rejected: a fully GitHub-side release triggered by the PR (approach B) — it
would lose the local review of the notes and changes the signing workflow.
Deferred: committing the notes on the PR branch before merging (approach C) —
only needed if `main` is protected later.

## Part 1 — The composite PR (Claude's workflow)

Triggered when the user asks to add a change to the composite PR (or "the PR").

1. **Find the PR.** `gh pr list --base main --state open --label composite`.
   - None: create a branch `next/<YYYY-MM-DD>` from an up-to-date `main` and
     open a **draft** PR into `main` with the `composite` label (create the
     label once if it does not exist).
   - One: check out its branch and bring it up to date with its remote.
   - Two or more: ask the user which one.
   - The user can always name a different PR or ask for a new one.
2. **Commit and push** the change to that branch. `build.yml` builds the push.
3. **Rewrite the PR description in full** each time, so it always describes
   everything in the PR (not an append-only log). Sections, in the style of
   #102:
   - **Summary** — one or two lines on what the PR is about.
   - **Changes** — one bullet per change: user-facing effect first, then a
     short technical note and the boards affected (e.g. "T-Deck Pro only").
   - **Testing** — what was built, flashed or tested, and what was not.
   - **Notes for release** — anything release notes must call out (config
     migration, USB reflash needed, behaviour changes). Omitted when empty.
4. **Retitle** the PR to reflect its content
   (e.g. "T-Deck Pro: Terminus fonts, bold sender names").
5. **Mark ready** with `gh pr ready <n>` only when the user says it is ready.

The `composite` label only tells Claude which PR is its own to add to. It plays
no part in the release; any developer's non-draft PR is releasable.

## Part 2 — The release flow (`scripts/release.sh`)

The default (remote) mode gains a PR stage in front of today's flow.

1. **Preflight.** Fetch and sync `main` as today. **New:** refuse to run if the
   working tree has uncommitted changes, because only PR content should be
   released. This applies to `--from-main` too; only `--build-local` keeps
   today's `git add -A` behaviour.
2. **Select the PR.** `gh pr list --base main --state open` with JSON fields for
   number, title, author, head branch, head SHA, draft state, mergeability and
   check status. Drafts are filtered out.
   - 0 ready: stop with a message suggesting `gh pr ready <n>`.
   - 1 ready: show it and confirm (`-y` skips the confirmation).
   - 2+ ready: numbered menu, with each PR's number, title, author and build
     status.
   - `--pr N` selects directly (non-interactive use).
3. **Gate it.**
   - The `Build` workflow's check on the PR's head SHA must be `SUCCESS`.
     Pending or failed: stop and say which.
   - `mergeable` must be `MERGEABLE`. Conflicting: stop; conflicts are resolved
     on the branch, not by the release script.
4. **Version.** Unchanged: `-y` bumps the patch from the latest tag, `--version`
   sets it, otherwise prompt.
5. **Release notes, before anything irreversible.**
   - Model input: the PR's title and body, its commit subjects
     (`gh pr view N --json commits`), and the diffstat of
     `origin/main...<head SHA>` with today's exclusions.
   - Same model call, prompt rules and fallbacks as today; the prompt is
     reworded to say the source is a PR description plus commits.
   - The notes end with `Pull request: #N`.
   - Reviewed with today's Yes / edit / no prompt. "No" stops the run here,
     before the merge, with `RELEASE_NOTES.md` restored by the existing guard.
6. **Merge.**
   `gh pr merge N --merge --delete-branch --match-head-commit <head SHA>`.
   `--match-head-commit` guarantees the merged commit is the one whose build
   passed; a push in between makes the merge fail and the run stop.
   Branch deletion is best-effort (it cannot delete a fork's branch).
7. **Today's flow from here.** Pull `main`, write `RELEASE_NOTES.md`, commit
   `Prepare release vX (#N) [skip ci]`, push, dispatch `release.yml`, and report
   the run, exactly as the current remote path does.

**Resume after a partial run.** If the run fails after step 6 (push or dispatch
error), `release.sh --pr N` detects that PR N is already merged and its merge
commit is on `main` but not contained in any release tag. It skips steps 2–3
and 6, regenerates or reuses the notes (offering `RELEASE_NOTES.md` if present),
and continues from step 7.

### Flags

| Flag | Behaviour |
|---|---|
| *(none)* | PR mode, as above. |
| `--pr N` | PR mode with PR N; also the resume path. |
| `--dry-run` | **New.** PR mode up to and including the notes review, then print what would be merged, committed and dispatched; change nothing. |
| `--from-main` | **New.** Today's no-PR remote flow (notes from `git log <last tag>..HEAD`), for emergency fixes. Still refuses a dirty tree. |
| `--notes-only` | Drafts notes; in PR mode, from the selected PR. Builds and publishes nothing. |
| `--use-committed-notes`, `--build-local`, `--check-targets`, `--no-clean`, `--version`, `-y`, `--append-last-notes` | Unchanged. |
| `--alpha` | Unchanged: releases the `alpha` branch as today, with no PR stage. |

`release.yml` passes `--use-committed-notes` and runs with `GITHUB_ACTIONS=true`,
so it never enters PR mode; it is unchanged.

## Part 3 — Where the convention lives

- **Repo `CLAUDE.md`** (new, committed): the Part 1 rules, plus "releases go
  through `scripts/release.sh`, which releases one ready PR; do not push to
  `main` directly". Any Claude session in the repo, including other
  developers', reads it.
- **`docs/BUILD.md`**: a short "Contributing and releasing" section for humans:
  open a PR into `main`, keep it draft until ready, `gh pr ready`, and how the
  release picks it up.

## Error handling summary

| Situation | Behaviour |
|---|---|
| Dirty working tree | Stop before anything; list the files. |
| No ready PR | Stop; suggest `gh pr ready <n>`. |
| Build check pending / failed / missing | Stop; name the state and link the run. |
| PR has conflicts | Stop; resolve on the branch. |
| Notes rejected | Stop before merge; notes file restored. |
| Head moved since selection | `--match-head-commit` makes the merge fail; stop, rerun. |
| Failure after merge | Rerun with `--pr N` to resume. |
| `gh` missing or unauthenticated | Stop in preflight (already checked today). |

## Testing

No step of this can be tested without touching GitHub, so tests are staged to
avoid publishing anything:

1. `--notes-only --pr <real PR>` produces notes ending in `Pull request: #N`.
2. `--dry-run` against 0, 1 and 2 ready PRs exercises selection, gating and the
   menu, and changes nothing (verify `git status` and the PR are untouched).
3. A dirty tree is refused.
4. The first real release through the new flow is the PR that carries this
   change, which also exercises the merge and dispatch.
5. Resume: tested by interrupting a run between merge and dispatch only if a
   real failure occurs; otherwise covered by reading the detection logic
   against a merged-but-untagged PR in `--dry-run`.

## Out of scope

- Branch protection or required checks on `main`.
- Release from PRs into `alpha`.
- Changes to `release.yml`, `build.yml`, signing, or the notes baked into
  firmware.
