# Contributing to Camillia

Thanks for your interest. Camillia is a hobby project, open by design, and
contributions are welcome: code, board ports, translations, bug reports and
testing on hardware.

## License of contributions

Camillia is licensed under **GPL-3.0-or-later** (see [LICENSE](LICENSE)). By
submitting a contribution you agree that it is licensed under the same terms
(inbound = outbound). This keeps every build and fork open.

Sign off your commits to certify that you wrote the change, or have the right to
submit it, per the [Developer Certificate of Origin](https://developercertificate.org/):

```
git commit -s -m "..."     # adds a Signed-off-by line
```

New source files should carry an SPDX header:

```c
// SPDX-License-Identifier: GPL-3.0-or-later
```

Code taken from another project keeps its original license header. Don't
relicense other people's code; only your own additions are GPL.

## How we work

- **One topic per pull request.** Small, focused changes that do one thing are
  easy to review and easy to revert. Open a separate PR per concern.
- **Discuss next steps, keep the change scoped.** It's great to note what could
  come next in the PR description; just don't bundle it into the same diff.
- **Prefer pulling in a library over hand-rolling.** If a maintained library
  solves it, depend on it. Pin it to an exact version or commit in
  `platformio.ini`, and credit it in the README's Acknowledgments.
- **Don't regress what ships.** Camillia runs on real devices across many
  boards. Most of them build from the same sources (`src/main_lvgl.cpp` above
  all), so a change for one board can break another. Gate board-specific code
  on the `DEVICE_*` and capability macros in `src/config.h` and
  `src/hal/board.h`, and keep every environment building.
- **Stay Meshtastic-compatible.** Changes to what goes over the air should
  interoperate with current Meshtastic firmware. Say in the PR which Meshtastic
  version you checked against.
- **Say what you tested.** State which boards you built, which you flashed, and
  what you could not test on hardware.

## Coding standards

Match the code around you. In general:

- **Style.** Four-space indents, opening braces on the same line, `camelCase`
  for functions and variables, `kName` for constants, `s_name` for file-scope
  statics, `UPPER_CASE` for macros. Mark file-local functions `static`.
- **Board code.** Keep board-specific logic behind `DEVICE_*` and capability
  macros, and put pins and bus addresses in the board's `src/hal/hw_*.h`, not
  in shared code. Never guess a pin map: take it from a schematic, the vendor's
  examples or a working firmware, and say which in a comment.
- **Untrusted input.** Everything that arrives over the air, over MQTT, from
  the web config or from an SD card is untrusted. Bounds-check every length and
  offset before reading, and never let a malformed packet crash the device or
  write past a buffer.
- **Memory.** These are small devices. Prefer fixed-size buffers to heap
  allocation on hot paths such as the radio and UI loops, and don't add large
  stack arrays to tasks without checking their stack size.
- **Settings.** Saved config must survive an update. Add new fields to the end
  of `RhinoConfig`, give them a safe default, and keep the device UI, web
  config and import/export in step.
- **Identity and keys.** Never log or export private keys, and don't change
  how node identity or key material is derived without saying so plainly in
  the PR.
- **Comments.** Explain *why*, not what: the hardware quirk, the upstream
  behavior being matched, the bug a check prevents. Link the issue or the
  upstream source where one exists.
- **Dependencies.** Pin exact versions or commits. No floating `@^1` ranges or
  branch names.
- **Warnings.** Don't add compiler warnings, and don't silence one without a
  comment saying why it's safe.
- **Tests.** Logic that can run off the device (protocol encoding, parsing,
  policy decisions) should have a host test in `tests/`.

## Using AI

AI tools are welcome here; much of Camillia was written with one. The same rules
apply to AI-assisted code as to any other, plus a few more:

- **You are the author.** If you submit it, you are responsible for it. Read
  and understand every line before opening a PR, and be ready to explain and
  change it in review. "The AI wrote it" is not an answer to a review comment.
- **Say so.** Mention in the PR when a substantial part was written by an AI
  tool. A `Co-Authored-By` line in the commit is fine.
- **Evidence, not claims.** Don't say something works on hardware unless you
  ran it there, and say what you saw (a serial log, a photo, a measurement).
  Built, flashed and untested are different things; say which.
- **No invented facts.** AI tools make up pin numbers, register addresses,
  protocol fields and library APIs that look right. Check each against a
  datasheet, schematic, the upstream source or the library's headers, and
  cite it.
- **Keep it scoped.** AI tools like to refactor, rename and reformat code they
  were not asked to touch. Trim that out before submitting; a PR should
  contain only the change it describes.
- **Mind the license.** Don't paste in code an AI reproduced from another
  project unless you know where it came from and its license is compatible
  with GPL-3.0, and credit it.
- **No secrets.** Don't give an AI tool private keys, Wi-Fi or MQTT passwords,
  or someone else's personal data, and check generated logs and configs for
  them before committing.
- **Translations.** Machine translations are acceptable as first drafts, but
  say so in the PR so a native speaker knows to review them.

The project's own AI working instructions live in
[CLAUDE.md](CLAUDE.md) and
[.github/copilot-instructions.md](.github/copilot-instructions.md).

## Building and testing

See [docs/BUILD.md](docs/BUILD.md) for building and flashing. Before opening a
PR, run the host tests:

```
bash tests/run.sh
```

CI builds every environment on each push to a pull request, and a PR must have
a passing Build before it can be merged. The Build also checks a PR against the
rules above that a machine can check: a missing sign-off, a new source file
without an SPDX header, or a dependency without an exact version fails it, and
new strings that lack a translation are listed as warnings. Run the same check
locally before pushing:

```
python3 tools/pr_guidelines.py origin/main HEAD
```

## Translations

User-facing text goes through `TR("...")`. A new string should get an entry in
every `lang/*.lang` file. See [docs/TRANSLATING.md](docs/TRANSLATING.md) for the
format. Corrections from native speakers are especially welcome, since most
translations are first drafts.

## Pull requests and releases

Open pull requests against `main`. Only the maintainer and the project's GitHub
Actions push to `main` directly; every other change arrives as a PR.
Releases are cut by the maintainer, one merged PR at a time, and the release
notes are written from the PR description. So describe the user-facing effect
of each change, and call out anything users need to know, such as a config
migration or a USB reflash.

## Reporting issues

Bug reports and feature ideas are welcome in the issue tracker. Please include
your board, firmware version and steps to reproduce, and a serial log if you
have one.
