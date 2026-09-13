# Profiler

[![CI](https://github.com/KhwarizmiAnalytix/Profiler/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Profiler/actions/workflows/ci.yml)

C++ CPU/GPU profiler for **any** C++ project. Drop it in with FetchContent
or `find_package`, annotate scopes, and open the JSON in
[chrome://tracing](chrome://tracing) or [Perfetto](https://ui.perfetto.dev).

It does **not** depend on XSigma. XSigma is one consumer of this library.

## Use it in another repo

```cmake
include(FetchContent)
FetchContent_Declare(
  Profiler
  GIT_REPOSITORY https://github.com/KhwarizmiAnalytix/Profiler.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(Profiler)
target_link_libraries(my_app PRIVATE Profiler::Profiler)
```

```cpp
#include "profiler.h"

int main() {
    profiler::profiler_session session;
    session.start();
    {
        PROFILER_PROFILE_SCOPE("work");
        // your code
    }
    session.stop();
    session.write_chrome_trace("trace.json");
}
```

`PROFILER_PROFILE_FUNCTION()` names the current function. Memory allocators
in any project can call `profiler::report_memory_usage(...)` (no-op when no
session is running).

Install prefix alternative:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix /opt/Profiler
```

```cmake
find_package(Profiler REQUIRED)
target_link_libraries(my_app PRIVATE Profiler::Profiler)
```

`find_package(XSigmaProfiler)` still works (alias). Dummy consumers live in
[`consumer/`](consumer/). Copy [`examples/example_quickstart.cpp`](examples/example_quickstart.cpp).

## Build this repo

```bash
git clone --recurse-submodules https://github.com/KhwarizmiAnalytix/Profiler.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

GitHub Actions runs the same configure / build / `ctest` path on Ubuntu and
macOS for `PROFILER_BACKEND=KINETO` and `ITT`.

If `third_party/fmt` is missing, CMake FetchContent downloads fmt, kineto,
and ittapi. Or pass `-DPROFILER_THIRD_PARTY_DIR=/path/to/fmt-kineto-ittapi`.
See [`third_party/README.md`](third_party/README.md).

| Option | Default | Meaning |
|---|---|---|
| `PROFILER_BACKEND` | `KINETO` | `KINETO` or `ITT` |
| `PROFILER_GPU_BACKEND` | `none` | `none`, `cuda`, `hip`, `metal` |
| `PROFILER_ENABLE_TESTING` | `ON` | `ProfilerCxxTests` |
| `PROFILER_ENABLE_EXAMPLES` | `OFF` | `examples/` |
| `PROFILER_ENABLE_INSTALL` | `ON` when this is the CMake source root | export the package |

A host project may set `MEMORY_GPU_BACKEND`; it is mapped to
`PROFILER_GPU_BACKEND` when the latter is unset.

Link target: **`Profiler::Profiler`**. Namespace: **`profiler`**.

## License

GPL-3.0-or-later OR Commercial. Vendored kineto, fmt, and ittapi keep their
own licenses; see `NOTICE`.
