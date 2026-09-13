# XSigmaProfiler

Standalone C++ profiler (native XPlane/TraceMe plus Kineto or ITT).
This package has **no dependency on the rest of XSigma**. Other projects
consume it as a third-party library.

Include root is the package root (same paths as the former `Library/Profiler`):

```cpp
#include "common/instrumentation.h"
#include "native/session/profiler.h"
```

Link target: **`Profiler::Profiler`**.

## Build (standalone)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPROFILER_THIRD_PARTY_DIR=/path/to/fmt-kineto-ittapi
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If `PROFILER_THIRD_PARTY_DIR` is unset and `third_party/fmt` is missing,
CMake FetchContent downloads fmt, kineto, and ittapi. See
[`third_party/README.md`](third_party/README.md).

Options:

| Option | Default | Meaning |
|---|---|---|
| `PROFILER_BACKEND` | `KINETO` | `KINETO` or `ITT` |
| `PROFILER_GPU_BACKEND` | `none` | `none`, `cuda`, `hip`, `metal` |
| `PROFILER_ENABLE_TESTING` | `ON` | `ProfilerCxxTests` |
| `PROFILER_ENABLE_EXAMPLES` | `OFF` | `examples/` |
| `PROFILER_ENABLE_INSTALL` | `ON` when this is the CMake source root | export the package |

## Install and `find_package`

```bash
cmake --install build --prefix /opt/XSigmaProfiler
```

Consumer:

```cmake
find_package(XSigmaProfiler REQUIRED)
target_link_libraries(my_app PRIVATE Profiler::Profiler)
```

A dummy consumer is in [`consumer/`](consumer/).

## FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
  XSigmaProfiler
  GIT_REPOSITORY https://github.com/KhwarizmiAnalytix/Profiler.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(XSigmaProfiler)
target_link_libraries(my_app PRIVATE Profiler::Profiler)
```

## XSigma

This repository is the source of truth. XSigma consumes it as the git
submodule `Packages/XSigmaProfiler` and `add_subdirectory`s it, passing
`PROFILER_THIRD_PARTY_DIR` so fmt/kineto are not built twice.
`MEMORY_GPU_BACKEND` is mapped to `PROFILER_GPU_BACKEND`.

## License

GPL-3.0-or-later OR Commercial (same dual license as XSigma).
Vendored kineto, fmt, and ittapi keep their own licenses; see `NOTICE`.
