---
name: project-build
description: "Use for Profiler build, test, coverage, or sanitizer work; select only locally supported options."
---

# project-build

Use this skill only when the task mentions build, test, coverage, sanitizer,
configuration, or a build failure. Do not invoke it for an ordinary source
edit with no build or test request.

Read [CLAUDE.md](../../../CLAUDE.md) for repository boundaries and test conventions.

For every build or test request, validate both supported build systems unless
the user explicitly scopes the task to one. Report CMake and Bazel results
separately; an unavailable toolchain is a blocker for that path, not a reason
to silently skip it.

## CMake

Use the setup helper from `Scripts/`; inspect its help before adding feature
flags:

```sh
cd Scripts
python3 setup.py --help
python3 setup.py config.build.test
```

For compiler or generator requirements, follow `README.md` and CI. The
repository also documents direct CMake commands for integration and CI.

Run the focused CMake test target after building when the task changes
behavior. Use the configured backend and build directory; do not invent
flags.

## Bazel

For Bazel, run from the repository root:

```sh
bazel build //:Profiler
bazel test //Testing/Cxx:ProfilerCxxTests --test_output=errors
```

Use `--backend.itt` for ITT and `--gpu.cuda` for CUDA when its toolkit is
available. Test backend changes against the relevant KINETO/ITT and GPU
configurations. Bazel files exist but there is no `Scripts/setup_bazel.py`;
use the explicit Bazel commands shown here for the supported default targets.
Run the focused Bazel test target after building when the task changes
behavior.

Scope repeated test runs to affected behavior when the framework supports
it, then run the required broader checks before handoff. Use only options
documented in this repository; optional coverage, sanitizer, backend, and
compiler flags are not interchangeable across projects.

Distinguish missing prerequisites from build or test failures. Report the
command, selected configuration, and result; do not report unrun checks as
passing. Keep generated files in the normal build or temporary directories.
