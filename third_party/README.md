# Third-party dependencies

Profiler 1.0.1 uses the following upstream dependency revisions:

| Dependency | Revision | Source |
| --- | --- | --- |
| fmt | `12.2.0` (`1be298e1bd68957e4cd352e1f676f00e07dcfb57`) | [fmtlib/fmt](https://github.com/fmtlib/fmt) |
| Intel ITT API | `v3.28.4` (`bd72cfb15e34a5fc4c0a8d10f5bb5305d60e1eec`) | [intel/ittapi](https://github.com/intel/ittapi) |
| Kineto | `eeaa4244e5f8e5f2690212c7c24e8b08061382d8` | [pytorch/kineto](https://github.com/pytorch/kineto) |
| GoogleTest | `v1.18.0` (`063de7e9578f82b369302001269680b4b1553359`) | [google/googletest](https://github.com/google/googletest) |

Kineto comes directly from PyTorch's upstream repository. Its source and nested
submodule pins remain unchanged. Profiler configures Kineto to use the same
compiled fmt target as Profiler, and builds its own tests with the separately
pinned GoogleTest version above. Kineto's internal tests are disabled.
Compatibility adjustments live in Profiler's CMake integration, rather than in
a modified Kineto checkout.

## Populate dependencies

For a fresh checkout:

```bash
git clone --recurse-submodules https://github.com/KhwarizmiAnalytix/Profiler.git
```

For an existing checkout:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

When dependency directories are missing, CMake downloads fmt, Kineto, and ITT
into `third_party/`. The Kineto fallback uses the same upstream URL and commit
as the submodule. GoogleTest is downloaded when tests are enabled and no
existing GoogleTest target or checkout is available.

To reuse existing sources, pass
`-DPROFILER_THIRD_PARTY_DIR=/path/to/dependencies`, where the directory contains
`fmt/`, `kineto/`, and `ittapi/`. A parent project that `add_subdirectory`s
this tree can pass `PROFILER_THIRD_PARTY_DIR` to reuse its own copies.

## Update dependency pins

Keep `.gitmodules`, the root gitlinks, and `cmake/ProfilerDependencies.cmake`
consistent. Choose commits or release tags available from each upstream
repository. After changing the Kineto revision, update its nested submodules to
the exact commits recorded upstream; a modified nested checkout is not published
by committing only the parent Profiler repository.

Validate recursive checkout, configure/build/tests, and the installed consumer
after dependency updates. All dependencies retain their upstream licenses;
see [NOTICE](../NOTICE).
