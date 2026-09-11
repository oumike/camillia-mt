"""Injects APP_VERSION into the build from the VERSION file.

This replaced a `!echo "-DAPP_VERSION=...$(cat VERSION | tr -d '\n')..."` line in
every environment. That form works on a POSIX shell and fails on Windows
`cmd.exe`, which has no `$(...)` command substitution — so the macro came out as
the literal text of the command and the build broke before it started. Having
coreutils on PATH does not help: the substitution is the shell's job, not
`cat`'s. It made the project unbuildable for a contributor on Windows without
WSL, which is not a cost anyone chose to impose.

Python is already a hard dependency here (PlatformIO is written in it, and this
project runs two other pre-scripts), so doing it here costs nothing and behaves
the same on every host.

env.StringifyMacro() handles the quoting, rather than this script trying to
escape a string through the ini parser, the shell and the compiler's command
line by hand — which is what made the old line so hard to read.
"""

Import("env")  # noqa: F821  - injected by PlatformIO/SCons

import os

version = "unknown"
version_path = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")  # noqa: F821
try:
    with open(version_path, "r", encoding="utf-8") as fh:
        # .strip() rather than the old `tr -d '\n'`: it also drops the CR that a
        # checkout with CRLF line endings leaves behind, which would otherwise
        # ride along inside the version string and reach the OTA comparison.
        version = fh.read().strip() or "unknown"
except OSError:
    # Not fatal. ota_update.cpp already defines APP_VERSION as "unknown" when it
    # is absent, so a missing VERSION file degrades instead of failing a build.
    print("[set_app_version] VERSION file not readable; leaving APP_VERSION unset")
else:
    env.Append(CPPDEFINES=[("APP_VERSION", env.StringifyMacro(version))])  # noqa: F821
    print("[set_app_version] APP_VERSION=%s" % version)
