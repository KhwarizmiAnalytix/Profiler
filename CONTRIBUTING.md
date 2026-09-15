# Contributing to Profiler

Thanks for considering a contribution. This project follows the standard
GitHub fork/branch/pull-request workflow.

## Build and test

See the [README](README.md#clone-and-build-the-examples) and the [user guide](docs/profiler.md#build-from-source)
for full instructions. The short version:

```bash
git clone --recurse-submodules https://github.com/KhwarizmiAnalytix/Profiler.git
cd Profiler
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

CI builds and tests both backends (`KINETO`, `ITT`) on Ubuntu, macOS, and
Windows; see [.github/workflows/ci.yml](.github/workflows/ci.yml) for the full
matrix, including the coverage and sanitizer jobs described below.

## Add a test

New GTest cases go in `Testing/Cxx/`, using the `PROFILERTEST(Module, case)`
macro (see any existing `Testing/Cxx/Test*.cpp` for the pattern). Register a
new file in `Testing/Cxx/CMakeLists.txt`'s `TestFiles` list. Prefer testing a
module's public header directly over only exercising it indirectly through
`profiler_session` — see `Testing/Cxx/TestCommonContainers.cpp` for an example
of unit-testing a low-level data structure in isolation.

## Code coverage

```bash
cmake -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug \
  -DPROFILER_ENABLE_TESTING=ON -DPROFILER_ENABLE_COVERAGE=ON
cmake --build build-coverage --parallel
ctest --test-dir build-coverage --output-on-failure
```

See [Code coverage](docs/profiler.md#code-coverage) for the full `lcov`/`genhtml`
recipe CI uses to produce an HTML report.

## Sanitizers

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPROFILER_ENABLE_TESTING=ON -DPROFILER_SANITIZER=address,undefined
cmake --build build-asan --parallel
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --test-dir build-asan --output-on-failure
```

Use `-DPROFILER_SANITIZER=thread` (separate build directory) for data races.
See [Sanitizers](docs/profiler.md#sanitizers) for platform caveats.

## Code style

Formatting follows [.clang-format](.clang-format) (4-space indent, Allman
braces, 100-column limit). Lint locally with lintrunner before sending a PR:

```bash
pip install lintrunner lint-tool
lintrunner init
lintrunner -a          # apply formatters (clang-format, cmake-format, newlines)
lintrunner             # check remaining linters
```

CI's `lint` job (`.github/workflows/lint.yml`) runs CLANGFORMAT, CMAKE,
CMAKEFORMAT, EDITORCONFIG, NEWLINE, and CODESPELL on files changed against
`main`. It does not require the pre-existing tree to already conform. Vendored
`third_party/` and `bespoke/` trees are excluded. `.clang-tidy` documents the
intended check set for local/IDE use; it is not yet wired into CI (see the
comment at the top of that file).

## Pull requests

- Keep PRs focused; unrelated cleanup makes review harder.
- Update `CHANGELOG.md` under `## Unreleased` for user-visible changes.
- Update `docs/` when behavior, build options, or APIs change.
- Ensure `ctest` passes locally before opening a PR; CI re-runs the full
  matrix regardless.

## Reporting bugs and requesting features

Use the issue templates under **New Issue**. For security vulnerabilities, see
[SECURITY.md](SECURITY.md) instead of filing a public issue.
