"""
Test Execution Helper Module

Handles ctest invocation for Scripts/setup.py.
"""

import os
import subprocess
from typing import Optional


def run_ctest(
    builder: str,
    build_enum: str,
    system: str,
    verbosity: str,
    shell_flag: bool,
    sanitizer_types: Optional[list[str]] = None,
    source_path: Optional[str] = None,
) -> int:
    """
    Run tests using ctest.

    Args:
        builder: Build system (ninja, xcodebuild, cmake)
        build_enum: Build type (Release, Debug, RelWithDebInfo)
        system: Operating system (Linux, Darwin, Windows)
        verbosity: Verbosity flag
        shell_flag: Whether to use shell execution
        sanitizer_types: Active sanitizer types (address, undefined, thread, ...).
            PROFILER_SANITIZER accepts a comma list (e.g. "address,undefined"),
            so more than one may be active in the same build -- each gets its
            own {TYPE}SAN_OPTIONS environment variable.
        source_path: Path to source directory

    Returns:
        Exit code (0 for success, non-zero for failure)
    """
    try:
        ctest_cmd = ["ctest"]

        if system == "Windows" and builder != "ninja":
            ctest_cmd.extend(["-C", build_enum])
        if builder == "xcodebuild":
            ctest_cmd.extend(["-C", build_enum])
        if verbosity:
            ctest_cmd.append(verbosity)

        env = os.environ.copy()

        if sanitizer_types and source_path:
            for sanitizer_type in sanitizer_types:
                suppressions_file = os.path.join(
                    source_path,
                    "Scripts",
                    "suppressions",
                    f"{sanitizer_type}san_suppressions.txt",
                )
                if os.path.exists(suppressions_file):
                    sanitizer_option = f"{sanitizer_type.upper()}SAN_OPTIONS"
                    env[sanitizer_option] = (
                        f"suppressions={suppressions_file}:print_suppressions=1"
                    )

        return subprocess.check_call(
            ctest_cmd, stderr=subprocess.STDOUT, shell=shell_flag, env=env
        )

    except subprocess.CalledProcessError:
        return 1
    except Exception:
        return 1
