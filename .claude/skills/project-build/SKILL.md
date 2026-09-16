---
name: project-build
description: Configure, build, and test Profiler with its own supported tooling. Use for build, test, coverage, or sanitizer requests; consult local capabilities before selecting optional flags.
---

# project-build

Adapted from XSigma's `xsigma-build` skill for Profiler.
Read [CLAUDE.md](../../../CLAUDE.md) for repository boundaries and test conventions.

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

Scope repeated test runs to affected behavior when the framework supports
it, then run the required broader checks before handoff. Use only options
documented in this repository; optional coverage, sanitizer, backend, and
compiler flags are not interchangeable across projects.

Distinguish missing prerequisites from build or test failures. Report the
command, selected configuration, and result; do not report unrun checks as
passing. Keep generated files in the normal build or temporary directories.
