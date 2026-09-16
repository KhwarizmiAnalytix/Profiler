---
name: clang-tidy
description: Run or diagnose clang-tidy for first-party C++ work associated with Profiler. Use for requested C++ static analysis or suppression fixes; this tool does not lint Python.
---

# clang-tidy

Read [CLAUDE.md](../../../CLAUDE.md), local `.clang-tidy` files where
present, and the build/linter configuration before choosing the invocation.

The repository has `.clang-tidy`, but its setup helper and lintrunner
configuration do not expose a clang-tidy pass. Configure a supported CMake
Ninja or Makefiles build with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, using the
repository's documented backend/test options. Analyze selected source files
with `clang-tidy -p <build-directory> <source.cpp>`.

Use the actual configured build directory and first-party source paths.
Do not assume an XSigma `clangtidy` setup token or a lintrunner `CLANGTIDY`
entry exists here.

For an applicable C++ analysis:

- Use a clang-tidy version compatible with the compiler and standard library
  that produced `compile_commands.json`. Missing system headers often
  indicate a toolchain or database mismatch rather than a source defect.
- Confirm the compilation database includes each selected translation unit.
  Check headers through source files that include them; an absent database
  entry or skipped file is not a successful analysis.
- Scope fixes to the requested first-party code. Preserve unrelated working
  changes and inspect the diff after any automatic rewrite.
- Investigate a finding before suppressing it. Prefer a narrowly scoped
  suppression with its reason; directory configurations should retain parent
  settings. Do not disable checks globally to silence one false positive.
- Leave vendored code unchanged. Report findings separately from setup
  failures and state the actual files/configurations analyzed.
