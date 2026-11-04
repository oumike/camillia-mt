"""Exercise the host runner's failure propagation and test discovery."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class HostRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="camillia tests ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tests = self.root / "tests"
        self.tests.mkdir()
        (self.root / "src").mkdir()
        shutil.copyfile(Path(__file__).with_name("run.sh"), self.tests / "run.sh")

    def suite(self, name, body="int main() { return 0; }", wrapper=True):
        (self.tests / f"{name}.cpp").write_text(body, encoding="utf-8")
        if wrapper:
            path = self.tests / "native" / name
            path.mkdir(parents=True)
            (path / "test_main.cpp").write_text("// fixture\n", encoding="utf-8")

    def run_suites(self, *names, sanitize="0"):
        env = dict(os.environ, SANITIZE=sanitize)
        return subprocess.run(
            ["bash", str(self.tests / "run.sh"), *names],
            cwd=self.root,
            env=env,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )

    def test_passing_suite(self):
        self.suite("test_pass")
        result = self.run_suites()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_compile_failure_fails_run(self):
        self.suite("test_bad", "this is not C++")
        result = self.run_suites()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("BUILD FAILED", result.stdout)

    def test_runtime_failure_does_not_hide_later_suites(self):
        self.suite("test_a_fail", "int main() { return 1; }")
        self.suite("test_z_pass", '#include <stdio.h>\nint main() { puts("ran last suite"); }')
        result = self.run_suites()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ran last suite", result.stdout)

    def test_host_suite_needs_native_wrapper(self):
        self.suite("test_missing", wrapper=False)
        result = self.run_suites()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("test_missing", result.stdout + result.stderr)

    def test_native_suite_needs_host_suite(self):
        self.suite("test_orphan")
        (self.tests / "test_orphan.cpp").unlink()
        self.suite("test_pass")
        result = self.run_suites()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("test_orphan", result.stdout + result.stderr)

    def test_selector_runs_only_requested_suites(self):
        self.suite("test_pass")
        self.suite("test_unselected", "this must not compile")
        result = self.run_suites("test_pass")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("BUILD FAILED", result.stdout)

    def test_unknown_selector_is_not_a_passing_empty_run(self):
        self.suite("test_pass")
        result = self.run_suites("test_typo")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("test_typo", result.stdout + result.stderr)

    def test_empty_suite_is_an_error(self):
        result = self.run_suites()
        self.assertNotEqual(result.returncode, 0)

    def test_sanitizer_errors_fail_even_when_program_returns_zero(self):
        self.suite(
            "test_overflow",
            "#include <limits.h>\n"
            "int main() { volatile int n = INT_MAX; volatile int x = n + 1; "
            "(void)x; return 0; }\n",
        )
        result = self.run_suites(sanitize="1")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("runtime error", result.stderr)

    def test_invalid_sanitizer_setting_is_not_silently_ignored(self):
        self.suite("test_pass")
        result = self.run_suites(sanitize="typo")
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
