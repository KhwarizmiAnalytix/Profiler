#!/usr/bin/env python3
"""Phase 6.A architecture checks (design-review.md section 7, Phase 6):

  "Add architecture checks forbidding public/backend dependency leakage and
  backend conditionals in common report logic."

Two independent checks:

1. Public-header leakage: none of the curated public headers (the single
   source of truth is CMakeLists.txt's own `_profiler_public_headers` list,
   parsed here rather than duplicated, so this check can't silently drift
   from what actually gets installed) may transitively #include a header
   under a backend-specific directory (Profiler/bespoke/kineto,
   Profiler/bespoke/itt, Profiler/bespoke/base -- CUDA/HIP glue).
   Profiler/bespoke/common is the shared instrumentation surface and is
   allowed (record_function.h is itself a public header).

2. Backend conditionals in common report logic: native/session/
   profiler_report.{h,cpp} and native/analysis/hotspot_report.{h,cpp} (the
   "common" report path -- not bespoke/kineto/hotspot_report.*, which is
   inherently backend-specific and lives under a backend directory, not
   "common" logic) must not reference PROFILER_HAS_KINETO / PROFILER_HAS_ITT
   / PROFILER_HAS_CUDA / PROFILER_HAS_HIP.

Both properties already hold on the current tree (see
docs/plans/phase-6-continuous-enforcement.md's baseline audit) -- this
script is a regression guard, run in CI on every push/PR, not a one-time
fix.

Exit code 0: both checks pass. Exit code 1: at least one violation found,
printed with enough detail to fix it.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PROFILER_SRC = REPO_ROOT / "Profiler"
CMAKELISTS = REPO_ROOT / "CMakeLists.txt"

# Directories a public header must never transitively depend on.
BACKEND_SPECIFIC_PREFIXES = (
    "bespoke/kineto/",
    "bespoke/itt/",
    "bespoke/base/",
)

# "Common report logic": the native/generic report path, not any
# backend-specific report implementation (e.g. bespoke/kineto/hotspot_report.*
# is deliberately kineto-only and lives under a backend directory).
COMMON_REPORT_FILES = (
    "native/session/profiler_report.h",
    "native/session/profiler_report.cpp",
    "native/analysis/hotspot_report.h",
    "native/analysis/hotspot_report.cpp",
)

BACKEND_MACROS = (
    "PROFILER_HAS_KINETO",
    "PROFILER_HAS_ITT",
    "PROFILER_HAS_CUDA",
    "PROFILER_HAS_HIP",
)

QUOTED_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def parse_public_headers() -> list[str]:
    """Extract the _profiler_public_headers list from CMakeLists.txt.

    Single source of truth: this script re-parses CMakeLists.txt's own list
    rather than hardcoding a duplicate, so the two can't silently diverge.
    """
    text = CMAKELISTS.read_text(encoding="utf-8")
    match = re.search(r"set\(_profiler_public_headers\s*(.*?)\)", text, re.DOTALL)
    if not match:
        raise RuntimeError(
            "could not find _profiler_public_headers in CMakeLists.txt -- "
            "has it been renamed or restructured? Update this script's parser."
        )
    headers = [line.strip() for line in match.group(1).splitlines()]
    return [h for h in headers if h]


def resolve_include(including_file: Path, quoted_path: str) -> Path | None:
    """Resolve a quoted #include the same way the compiler would: relative
    to the including file's directory first, then relative to Profiler/
    (this repo's one configured include root for quoted includes)."""
    candidate = (including_file.parent / quoted_path).resolve()
    if candidate.is_file():
        return candidate
    candidate = (PROFILER_SRC / quoted_path).resolve()
    if candidate.is_file():
        return candidate
    return None


def find_backend_leaks(public_headers: list[str]) -> list[str]:
    violations = []
    visited: set[Path] = set()
    queue: list[tuple[Path, list[str]]] = [
        (PROFILER_SRC / h, [h]) for h in public_headers
    ]

    while queue:
        current, chain = queue.pop(0)
        if current in visited:
            continue
        visited.add(current)

        current_rel = current.relative_to(PROFILER_SRC).as_posix()
        if any(current_rel.startswith(p) for p in BACKEND_SPECIFIC_PREFIXES):
            violations.append(
                f"{chain[0]} transitively includes backend-specific header "
                f"{current_rel} (via: {' -> '.join(chain)})"
            )
            continue  # Don't walk further into backend-specific territory.

        if not current.is_file():
            # A public header names something that doesn't resolve under
            # Profiler/ (a system/third-party header) -- not this check's
            # concern.
            continue

        for line in current.read_text(encoding="utf-8", errors="replace").splitlines():
            inc_match = QUOTED_INCLUDE_RE.match(line)
            if not inc_match:
                continue
            resolved = resolve_include(current, inc_match.group(1))
            if resolved is not None:
                queue.append((resolved, [*chain, current_rel]))

    return violations


def find_backend_conditionals() -> list[str]:
    violations = []
    for rel_path in COMMON_REPORT_FILES:
        path = PROFILER_SRC / rel_path
        if not path.is_file():
            violations.append(f"{rel_path}: expected file not found")
            continue
        for lineno, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
        ):
            for macro in BACKEND_MACROS:
                if macro in line:
                    violations.append(f"{rel_path}:{lineno}: references {macro}: {line.strip()}")
    return violations


def main() -> int:
    public_headers = parse_public_headers()
    leak_violations = find_backend_leaks(public_headers)
    conditional_violations = find_backend_conditionals()

    if leak_violations:
        print("Public-header backend-dependency leaks found:", file=sys.stderr)
        for v in leak_violations:
            print(f"  - {v}", file=sys.stderr)
        print(file=sys.stderr)

    if conditional_violations:
        print("Backend conditionals found in common report logic:", file=sys.stderr)
        for v in conditional_violations:
            print(f"  - {v}", file=sys.stderr)
        print(file=sys.stderr)

    if leak_violations or conditional_violations:
        print(
            "Phase 6.A architecture check failed -- see "
            "docs/plans/phase-6-continuous-enforcement.md's 6.A for the "
            "properties this enforces.",
            file=sys.stderr,
        )
        return 1

    print(
        f"OK: {len(public_headers)} public headers have no backend-specific "
        f"transitive dependency; {len(COMMON_REPORT_FILES)} common report "
        "files have no backend conditionals."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
