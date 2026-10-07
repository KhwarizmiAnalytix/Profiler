# Profiler

Standalone C++ profiler for any repository. Public header: `profiler.h`
(in `include/`). CMake target: `Profiler::Profiler`. Repo:
https://github.com/KhwarizmiAnalytix/Profiler

External projects use `find_package(Profiler)` or FetchContent. Do not add a
dependency on any private host project (its Core, Memory, Graph, … modules). A
host project may `add_subdirectory` this tree and pass `PROFILER_THIRD_PARTY_DIR`.

See [README.md](README.md) and [docs/profiler.md](docs/profiler.md).

CI (`.github/workflows/ci.yml`) configures, builds, and runs `ProfilerCxxTests`
on Ubuntu, macOS, and Windows for both `KINETO` and `ITT`. Windows also has
CUDA+NVTX jobs (`PROFILER_GPU_BACKEND=cuda`). CPU Kineto jobs install the
package and build `consumer/`.

## Shared agent guidance

Adapted from the public [XSigma rules and skills](https://github.com/KhwarizmiAnalytix/XSigma/tree/89848c54492abef57fd0d0dc53b9da96b7cd1d5d)
at revision `89848c54492abef57fd0d0dc53b9da96b7cd1d5d`. Local API, dependency, language, and build
conventions below specialize that guidance for this standalone repository.

The cross-tool entry point is [.agent-rules.md](.agent-rules.md). Cursor and
Codex load it through [.cursor/rules/profiler.mdc](.cursor/rules/profiler.mdc)
and [AGENTS.md](AGENTS.md); Claude should use it as the index for the same
authoritative rules under `.agent-rules/`.

Read the applicable rules before editing. They apply to Claude, Cursor, and
Codex; C++ rules apply only when working on C++:

- [C++ coding](.agent-rules/coding.md) and [builders](.agent-rules/builder.md)
- [Python](.agent-rules/python.md)
- [Testing](.agent-rules/testing.md) and [builds](.agent-rules/build%20rule.md)
- [Dependencies](.agent-rules/ThirdParty.md)
- [Portability](.agent-rules/must-have.md) and [documentation](.agent-rules/markdown.md)
- [Scope and opportunistic fixes](.agent-rules/scope.md)

Use these task-specific skills as needed:

- [project-build](.agents/skills/project-build/SKILL.md): configure, build, and test
- [new-test](.agents/skills/new-test/SKILL.md): add tests using local conventions
- [clang-tidy](.agents/skills/clang-tidy/SKILL.md): analyze first-party C++ when applicable
- [session-checklist](.agents/skills/session-checklist/SKILL.md): verify completed work
- [plan](.agents/skills/plan/SKILL.md): create and track documentation-only implementation plans

## Build and test

Use the setup helper from `Scripts/`; inspect its help before adding
feature flags:

```sh
cd Scripts
python3 setup.py --help
python3 setup.py config.build.test
```

For compiler or generator requirements, follow `README.md` and CI.
The repository also documents direct CMake commands for integration and CI.

For Bazel, run from the repository root:

```sh
bazel build //:Profiler
bazel test //Testing/Cxx:ProfilerCxxTests --test_output=errors
```

Use `--backend.itt` for ITT and `--gpu.cuda` for CUDA when its toolkit
is available. Test backend changes against the relevant KINETO/ITT and GPU
configurations. Bazel files exist but there is no `Scripts/setup_bazel.py`;
use the explicit Bazel commands shown here for the supported default targets.

## Test conventions

Match neighboring tests, including `PROFILERTEST` and `ProfilerTest.h`
where used; plain Google Test is also present. Add new C++ test files to
the explicit lists in `Testing/Cxx/CMakeLists.txt` and, where supported,
`Testing/Cxx/BUILD.bazel`. Preserve deliberate backend-specific differences.
Keep consumer tests focused on the public `profiler.h` API. Source lives
in `include/`, dependencies in `third_party/`; use `PROFILER_*` macros.

## Code coverage

Follow `KhwarizmiAnalytix/Logging`'s coverage setup as the canonical
pattern across these standalone repos — not the older per-target
`profiler_enable_coverage(Profiler)` approach this repo used before:

- `cmake/ProfilerCoverage.cmake` mutates `CMAKE_CXX_FLAGS`/`CMAKE_C_FLAGS`/
  the linker-flag variables directly at directory scope when
  `PROFILER_ENABLE_COVERAGE` is ON (`include(ProfilerCoverage)` gated in
  `CMakeLists.txt`, mirroring `Logging/Cmake/coverage.cmake`), so every
  target configured afterward — library and tests alike — is instrumented
  automatically, instead of requiring an explicit per-target call. It
  supports GCC (gcov), Clang (native `-fprofile-instr-generate
  -fcoverage-mapping`), and MSVC (OpenCppCoverage via PDBs) the same way
  Logging's module does.
- `coverage.toml` filters to first-party code by declaring `source_folder
  = "."` and `modules = ["Profiler"]` (coverage-tool's own module
  discovery, scoped to this repo's one flat library + test binary) rather
  than hand-maintaining an exclude list of every toolchain/SDK path
  (Homebrew LLVM, Xcode, libc++, `_deps`, …). Keep the exclude list to the
  same minimal shape as Logging's (`third_party`, `Testing`, `/usr/*`).
- `Scripts/setup.py`'s `coverage()` method calls `coverage_tool.get_coverage(compiler="auto", ...)`
  plainly, with no custom `lcov`/`gcc_coverage` monkey-patching — that
  workaround existed only because the old coverage.cmake used gcov-style
  `--coverage` for Clang too; with Clang's native format restored, "auto"
  resolves correctly on its own, same as Logging.

If a future `coverage-tool` quirk needs a workaround here, add the fix to
Logging first (so it stays the shared reference) before porting it to
this repo, rather than growing this repo's coverage path differently from
Logging's again.

## Verification and scope

For non-trivial source or build changes, run affected tests, review the diff,
and run configured lint/static-analysis checks relevant to touched files.
Check both build systems where provided. Follow the session checklist and
report checks run, failures, and unavailable tools explicitly. Guidance-only
changes need frontmatter/link/whitespace validation, not compilation.

Keep unrelated user edits and dependency sources intact. Share review
findings in the response or pull request; do not create unsolicited status
documents. Follow this repository's existing license and contribution policy.
