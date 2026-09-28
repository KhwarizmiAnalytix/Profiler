# Copyright (c) Profiler Contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Skip clang-tidy when no compile_commands.json exists yet.
# The linter activates once you run a CMake configure+build.
import os
import sys

_SKIP_DIRS = {".git", "third_party", "ThirdParty"}

for _root, dirnames, filenames in os.walk("."):
    dirnames[:] = [
        d for d in dirnames if not d.startswith("bazel-") and d not in _SKIP_DIRS
    ]
    if "compile_commands.json" in filenames:
        os.execvp(
            sys.executable,
            [
                sys.executable,
                "-m",
                "lint_tool.adapters.clangtidy_linter",
                "--binary=clang-tidy",
            ]
            + sys.argv[1:],
        )

sys.exit(0)
