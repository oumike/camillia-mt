Every change since v4.8.2-alpha.1 is release tooling or documentation — `release.sh`, the new `.github/workflows/release.yml`, comments in `build.yml`, and `README.md`. `git diff v4.8.2-alpha.1 -- src/ boards/ platformio.ini tools/` returns only the generated `src/release_notes.h`. Under your rules, that leaves no bullets, so the release notes for this tag are empty.

Two things worth knowing before you cut it:

- `RELEASE_NOTES.md` and the uncommitted `src/release_notes.h` currently hold a previous run's meta-commentary addressed to you. With the workflow's default `notes=committed`, that text would publish verbatim and get baked into every image.
- The claim in that text about unresolved merge conflict markers is wrong — the only line matching `<<<<<<<`/`>>>>>>>` is the sentence describing them. There are no real markers.

If you want a tag here anyway, the honest options are to publish with an empty `RELEASE_NOTES.md` (GitHub's generated commit list still appears) or to write a one-line maintenance note yourself. Say which and I'll set the file up.
