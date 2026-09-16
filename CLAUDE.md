# Profiler

Standalone C++ profiler for any repository. Public header: `profiler.h`
(in `Profiler/`). CMake target: `Profiler::Profiler`. Repo:
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

Read the applicable rules before editing. They apply to Claude as well as
Augment; C++ rules apply only when working on C++:

- [C++ coding](.augment/rules/coding.md) and [builders](.augment/rules/builder.md)
- [Python](.augment/rules/python.md)
- [Testing](.augment/rules/testing.md) and [builds](.augment/rules/build%20rule.md)
- [Dependencies](.augment/rules/ThirdParty.md)
- [Portability](.augment/rules/must-have.md) and [documentation](.augment/rules/markdown.md)

Use these task-specific skills as needed:

- [project-build](.claude/skills/project-build/SKILL.md): configure, build, and test
- [new-test](.claude/skills/new-test/SKILL.md): add tests using local conventions
- [clang-tidy](.claude/skills/clang-tidy/SKILL.md): analyze first-party C++ when applicable
- [session-checklist](.claude/skills/session-checklist/SKILL.md): verify completed work

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
in `Profiler/`, dependencies in `third_party/`; use `PROFILER_*` macros.

## Verification and scope

For non-trivial source or build changes, run affected tests, review the diff,
and run configured lint/static-analysis checks relevant to touched files.
Check both build systems where provided. Follow the session checklist and
report checks run, failures, and unavailable tools explicitly. Guidance-only
changes need frontmatter/link/whitespace validation, not compilation.

Keep unrelated user edits and dependency sources intact. Share review
findings in the response or pull request; do not create unsolicited status
documents. Follow this repository's existing license and contribution policy.
