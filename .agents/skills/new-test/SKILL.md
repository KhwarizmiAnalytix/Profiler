---
name: new-test
description: "Use when adding or extending Profiler tests while preserving local frameworks, registration, and error contracts."
---

# new-test

Read [CLAUDE.md](../../../CLAUDE.md) and the existing test closest to
the changed behavior before choosing a filename, fixture, or macro.

Match neighboring tests, including `PROFILERTEST` and `ProfilerTest.h`
where used; plain Google Test is also present. Add new C++ test files to
the explicit lists in `Testing/Cxx/CMakeLists.txt` and, where supported,
`Testing/Cxx/BUILD.bazel`. Preserve deliberate backend-specific differences.
Keep consumer tests focused on the public `profiler.h` API. Source lives
in `Profiler/`, dependencies in `third_party/`; use `PROFILER_*` macros.

1. Search for existing coverage with `rg`; extend the relevant test file
   instead of creating a duplicate suite.
2. Match the neighboring includes, namespace, fixtures, naming, and license
   conventions. Do not bring in test helpers from a different repository.
3. Cover the meaningful success, boundary, and failure cases for the change.
   Assert public behavior and the repository's documented error contract.
4. Check source registration, discovery patterns, and backend exclusions so
   the new cases actually execute. Keep parallel build definitions in sync
   where both exist.
5. Run the affected tests using [project-build](../project-build/SKILL.md).
   Report the test command and result, including any unavailable backend.
