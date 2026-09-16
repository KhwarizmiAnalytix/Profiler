# Build and verification

When the task mentions building, testing, coverage, sanitizers, configuring,
or a build failure, load the `project-build` skill. Do not load it for an
ordinary source edit with no build or test request. Then read `CLAUDE.md` for
this repository's actual build commands. Use existing setup helpers where
provided. Python
packages use their packaging and pytest workflow instead of a C++ build.

Configure before the first build and after changing build options or source
registration. Run tests for changed behavior. Where both CMake and Bazel
exist, keep their affected source lists, tests, and options consistent and
validate both for non-trivial source or build changes.

Load the `session-checklist` skill only when the user requests a handoff,
commit-readiness, full verification, or review checklist. For guidance-only
edits, check links, syntax, and the diff; compilation is not required. Report
what actually ran and any blockers.
