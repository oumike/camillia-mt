# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/pr_guidelines.py: dependency pinning rules and the PR checks end to end."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))
import pr_guidelines  # noqa: E402


class PinningTests(unittest.TestCase):
    def test_pinned(self):
        for spec in [
            "jgromes/RadioLib@7.8.1",
            "espressif32@7.0.1",
            "native@1.2.1",
            "https://github.com/meshtastic/Crypto/archive/1c817c2f27aa4593e07d0f7e13b3dbf8d980bcae.zip",
            "M5Cardputer=https://github.com/m5stack/M5Cardputer/archive/f1392858b9994c3547120e602a57d3553d16ab01.zip",
            "https://github.com/CIRCUITSTATE/CSE_CST328/archive/refs/tags/v0.0.4.zip",
            "https://github.com/pschatzmann/arduino-audio-driver/archive/v0.3.0.zip",
            "https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip",
            "https://github.com/foo/bar.git#v1.2.3",
            "${env.lib_deps}",
        ]:
            self.assertIsNone(pr_guidelines.pin_problem(spec), spec)

    def test_unpinned(self):
        for spec in [
            "jgromes/RadioLib",
            "jgromes/RadioLib@^7.8.1",
            "jgromes/RadioLib@~7.8.1",
            "jgromes/RadioLib@>=7",
            "https://github.com/foo/bar.git",
            "https://github.com/foo/bar.git#main",
            "https://github.com/foo/bar/archive/refs/heads/main.zip",
            "https://github.com/foo/bar/archive/master.zip",
        ]:
            self.assertIsNotNone(pr_guidelines.pin_problem(spec), spec)

    def test_ini_parsing(self):
        ini = (
            "[env:a]\n"
            "platform  = espressif32@7.0.1\n"
            "lib_deps =\n"
            "    ; a comment line\n"
            "    # another comment\n"
            "    jgromes/RadioLib@^7.8.1   ; trailing comment\n"
            "    lvgl/lvgl@9.5.0\n"
            "build_flags =\n"
            "    -DFOO=1\n"
            "[env:b]\n"
            "lib_deps = mikalhart/TinyGPSPlus\n"
        )
        bad = pr_guidelines.unpinned_dependencies(ini)
        self.assertEqual([(n, s) for n, s, _ in bad],
                         [(6, "jgromes/RadioLib@^7.8.1"), (11, "mikalhart/TinyGPSPlus")])

    def test_repo_ini_is_pinned(self):
        text = (TOOLS.parent / "platformio.ini").read_text(encoding="utf-8")
        self.assertEqual(pr_guidelines.unpinned_dependencies(text), [])

    def test_spdx_detection(self):
        self.assertTrue(pr_guidelines.has_spdx("#!/bin/sh\n# SPDX-License-Identifier: MIT\n"))
        self.assertFalse(pr_guidelines.has_spdx("// nothing here\nint x;\n"))
        self.assertFalse(pr_guidelines.has_spdx("\n" * 5 + "// SPDX-License-Identifier: MIT\n"))
        self.assertTrue(pr_guidelines.needs_spdx("src/foo.cpp"))
        self.assertFalse(pr_guidelines.needs_spdx("src/fonts/latin/x.c"))
        self.assertFalse(pr_guidelines.needs_spdx("docs/USE.md"))


class EndToEndTests(unittest.TestCase):
    """A scratch repo with a base commit and a PR commit on top."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="camillia guidelines ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "tools").mkdir()
        for name in ("pr_guidelines.py", "i18n_audit.py"):
            shutil.copyfile(TOOLS / name, self.root / "tools" / name)
        (self.root / "src").mkdir()
        (self.root / "lang").mkdir()
        (self.root / "platformio.ini").write_text("[env:a]\nplatform = native@1.2.1\n")
        (self.root / "src" / "ui.cpp").write_text('// SPDX-License-Identifier: GPL-3.0-or-later\nauto a = TR("Old");\n')
        (self.root / "lang" / "de.lang").write_text("Old\tAlt\n")
        (self.root / "lang" / "fr.lang").write_text("Old\tVieux\n")
        self.git("init", "-q", "-b", "main")
        self.git("add", "-A")
        self.commit("base", signoff=True)
        self.base = self.git("rev-parse", "HEAD").strip()

    def git(self, *args):
        env = dict(os.environ, GIT_AUTHOR_NAME="T", GIT_AUTHOR_EMAIL="t@example.com",
                   GIT_COMMITTER_NAME="T", GIT_COMMITTER_EMAIL="t@example.com")
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              capture_output=True, text=True, env=env).stdout

    def commit(self, msg, signoff):
        self.git("add", "-A")
        self.git("commit", "-q", "--no-gpg-sign", *(["-s"] if signoff else []), "-m", msg)

    def check(self):
        env = {k: v for k, v in os.environ.items() if k not in ("GITHUB_ACTIONS", "GITHUB_STEP_SUMMARY")}
        return subprocess.run([sys.executable, str(self.root / "tools" / "pr_guidelines.py"),
                               self.base, "HEAD"], capture_output=True, text=True, env=env)

    def test_clean_pr_passes(self):
        (self.root / "src" / "new.h").write_text("// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\n")
        self.commit("add header", signoff=True)
        r = self.check()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("OK", r.stdout)

    def test_missing_signoff_fails(self):
        (self.root / "README").write_text("x\n")
        self.commit("unsigned change", signoff=False)
        r = self.check()
        self.assertEqual(r.returncode, 1)
        self.assertIn("unsigned change", r.stdout)

    def test_new_file_without_spdx_fails(self):
        (self.root / "src" / "bare.cpp").write_text("int x;\n")
        self.commit("bare file", signoff=True)
        r = self.check()
        self.assertEqual(r.returncode, 1)
        self.assertIn("src/bare.cpp", r.stdout)

    def test_existing_file_without_spdx_is_not_checked(self):
        (self.root / "src" / "ui.cpp").write_text('auto a = TR("Old");\n')
        self.commit("drop header from existing file", signoff=True)
        self.assertEqual(self.check().returncode, 0)

    def test_unpinned_dependency_fails(self):
        (self.root / "platformio.ini").write_text("[env:a]\nplatform = native\n")
        self.commit("float platform", signoff=True)
        r = self.check()
        self.assertEqual(r.returncode, 1)
        self.assertIn("line 2", r.stdout)

    def test_untranslated_strings_warn_but_pass(self):
        (self.root / "src" / "ui.cpp").write_text(
            '// SPDX-License-Identifier: GPL-3.0-or-later\n'
            'auto a = TR("Old");\nauto b = TR("Brand new");\nauto c = TR("Half done");\n')
        (self.root / "lang" / "de.lang").write_text("Old\tAlt\nHalf done\tHalb fertig\n")
        self.commit("new strings", signoff=True)
        r = self.check()
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('"Brand new": de fr', r.stdout)
        self.assertIn('"Half done": fr', r.stdout)
        self.assertNotIn('"Old"', r.stdout)


if __name__ == "__main__":
    unittest.main()
