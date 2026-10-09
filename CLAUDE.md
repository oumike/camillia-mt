# Camillia-MT — working conventions

Meshtastic-compatible firmware for ESP32 handheld LoRa devices. Most boards build
from shared sources (`src/main_lvgl.cpp` above all), with board-specific code
gated on `DEVICE_*` and capability macros in `src/config.h` and
`src/hal/board.h`. See `docs/BUILD.md` for building and `docs/AI_WORKFLOW_MAP.md`
for the change checklist.

## Every change goes through a pull request

Nothing is pushed to `main` directly. A release is one pull request into `main`,
merged by `scripts/release.sh` (see "Releasing" below). CI (`build.yml`) builds
every environment on each push to a PR.

### The composite PR

Firmware changes made at the user's request accumulate in one open **composite
PR**. When the user asks to add a change to "the PR" or "the composite PR":

1. **Find it:** `gh pr list --base main --state open --label composite`.
   - None: create a branch `next/<YYYY-MM-DD>` from an up-to-date `main` and
     open a **draft** PR into `main` with the `composite` label. Create the
     label first if it does not exist (`gh label create composite`).
   - One: check out its branch and bring it up to date with its remote.
   - Two or more: ask the user which one.
   - The user can always name a different PR or ask for a new one.
2. **Commit and push** the change to that branch.
3. **Rewrite the PR description in full** every time, so it describes
   everything in the PR (not an append-only log). It is what the release notes
   are written from. Sections:
   - **Summary** — one or two lines on what the PR is about.
   - **Changes** — one bullet per change: the user-facing effect first, then a
     short technical note and the boards affected (e.g. "T-Deck Pro only").
   - **Testing** — what was built, flashed or tested, and what was not.
   - **Notes for release** — anything the release notes must call out (config
     migration, USB reflash needed, behaviour changes). Omit when empty.
4. **Retitle** the PR to match its content, e.g.
   "T-Deck Pro: Terminus fonts, bold sender names".
5. **Mark it ready** (`gh pr ready <n>`) only when the user says it is ready.

The `composite` label only marks which PR is Claude's to add to. Any
developer's non-draft PR into `main` with a passing Build can be released.

### Auditing

`./scripts/release.sh --audit [--pr N] [--skip-builds]` builds every release
environment and has Claude review the PR, in a temporary worktree, then posts
the report as a PR comment. It changes nothing. Run it only when asked.

## Releasing

`./scripts/release.sh` releases one ready PR: it uses the only one, or asks
which. It checks that the PR's Build passed and it merges cleanly, writes the
release notes from it for review, merges it, commits the notes to `main` and
dispatches `release.yml`, which builds, signs and publishes. `--dry-run`
previews; `--pr N` picks a PR or resumes a release whose PR was merged;
`--from-main` releases `main` without a PR, for emergencies. Releases are the
user's to start: do not run the release script unless asked.
