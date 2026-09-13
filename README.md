# Profiler

[![CI](https://github.com/KhwarizmiAnalytix/Profiler/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Profiler/actions/workflows/ci.yml)

Version **1.0.1** · [Changelog](CHANGELOG.md)

**C++ instrumentation, timelines, and hotspot reports for standalone applications.**

Profiler records annotated CPU scopes, reconstructs nested calls, and exports
reports for performance investigations. A native tracing pipeline is always
available; Kineto or Intel ITT provides additional instrumentation. CUDA builds
can collect Kineto device activities through CUPTI.

Link **`Profiler::Profiler`**, include **`profiler.h`**, and use the **`profiler`**
namespace. No XSigma, LibTorch, TensorFlow runtime, or Python dependency is required
for the C++ library. Python is optional for offline Holistic Trace Analysis (HTA).

[User guide](docs/profiler.md) · [HTA workflow](docs/hta.md) ·
[Output examples](docs/outputs.md) · [Runnable examples](examples/README.md)

## Build and run

Requires CMake 3.22+, a C++20 compiler, and Git. Commands below run from the
repository root after cloning.

```bash
git clone --recurse-submodules https://github.com/KhwarizmiAnalytix/Profiler.git
cd Profiler
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
./build/bin/example_quickstart
./build/bin/example_reports build/reports
```

With Visual Studio, run `./build/bin/Release/example_quickstart.exe` and
`./build/bin/Release/example_reports.exe build/reports` in PowerShell.
The quickstart writes `quickstart_trace.json` in the working directory. Open it
in [Perfetto](https://ui.perfetto.dev/) using **Open trace file**.

## Add Profiler to your application

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyApp LANGUAGES CXX C)

include(FetchContent)
set(PROFILER_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PROFILER_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  Profiler
  GIT_REPOSITORY https://github.com/KhwarizmiAnalytix/Profiler.git
  GIT_TAG main
)
FetchContent_MakeAvailable(Profiler)

add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE Profiler::Profiler)
```

For reproducible builds, replace `main` with a reviewed commit SHA.

```cpp
#include "profiler.h"

int main() {
    profiler::profiler_session session;
    if (!session.start()) return 1;
    {
        PROFILER_PROFILE_SCOPE("work");
        // Your workload. Add nested scopes or PROFILER_PROFILE_FUNCTION().
    } // Finish the scope before stopping collection.
    if (!session.stop()) return 1;
    return session.write_chrome_trace("trace.json") ? 0 : 1;
}
```

An installed package is also supported with
`find_package(Profiler CONFIG REQUIRED)`; see the
[installation guide](docs/profiler.md#install-and-find-package).

## Choose your output

| Need | Capture / export | Read it with |
| --- | --- | --- |
| CPU timeline and nested scopes | Native `profiler_session` → `write_chrome_trace()` | Perfetto / Chrome Trace viewer |
| CPU hotspots, counts, inclusive and self time | `generate_hotspot_report()` | Console tables |
| Session summary and hierarchy | `generate_report()` | Text, JSON, CSV, XML |
| Kineto CPU / CUDA activity trace | `prepareProfiler()` + `enableProfiler()` → `ProfilerResult::save()` | Perfetto or HTA |
| VTune / Nsight ranges | ITT / NVTX state + `PROFILER_RECORD_*` | External profiler |

Native scope macros and Kineto/ITT record macros feed separate captures.
Use the [backend guide](docs/profiler.md#choose-a-capture-pipeline) to select the
right one. HTA consumes the Kineto export, not the native session report.

## Holistic Trace Analysis

Generate a Kineto trace and analyze its CPU operators:

```bash
./build/bin/example_hta build/hta-traces
python3 -m venv build/hta-venv
source build/hta-venv/bin/activate
python -m pip install -r examples/requirements-hta.txt
python examples/analyze_hta.py build/hta-traces --output build/hta-results
```

The example emits four `ProfilerStep#N` annotations and four `compute` calls.
The [HTA guide](docs/hta.md) covers CUDA capture, multiple ranks, GPU breakdowns,
launch statistics, notebooks, and troubleshooting, with
[verified CPU output](docs/outputs.md#hta-analysis).

## Build configurations

| Option | Default | Purpose |
| --- | --- | --- |
| `PROFILER_BACKEND` | `KINETO` | Instrumentation backend: `KINETO` or `ITT` |
| `PROFILER_GPU_BACKEND` | `none` | `none`, `cuda`, `hip`, or `metal` |
| `PROFILER_REQUIRE_CUDA` | `OFF` | Require the requested CUDA Toolkit |
| `PROFILER_REQUIRE_NVTX` | `OFF` | Require NVTX for a CUDA configuration |
| `PROFILER_ENABLE_TESTING` | `ON` | Build the C++ test suite |
| `PROFILER_ENABLE_EXAMPLES` | `OFF` | Build runnable examples |
| `PROFILER_ENABLE_INSTALL` | `ON` for standalone builds | Install headers, library, and CMake package |
| `PROFILER_ENABLE_COVERAGE` | `OFF` | Instrument the library with `--coverage` (GCC/Clang) |
| `PROFILER_SANITIZER` | unset | GCC/Clang sanitizer(s), e.g. `address,undefined` or `thread` |

CI configures, builds, and tests Kineto and ITT on Ubuntu, macOS, and Windows.
Windows additionally builds both with CUDA and NVTX. Device tests skip when no
GPU is available. Ubuntu also runs the HTA CPU example through the Python
analysis script, builds a coverage report, and runs the suite under
ASan/UBSan/TSan. See [build details and limitations](docs/profiler.md#build-options),
[coverage](docs/profiler.md#code-coverage), and [sanitizers](docs/profiler.md#sanitizers).

Dependencies are Git submodules, with a CMake download fallback. An existing
checkout can be completed with `git submodule update --init --recursive`.
See [third-party dependencies](third_party/README.md).

## License

Profiler is licensed under the **Apache License, Version 2.0**.
The code is delivered **AS IS**, without warranties or conditions of any kind,
either express or implied. See [LICENSE](LICENSE) for the full terms and
[NOTICE](NOTICE) for attribution.

Third-party components and source files with separate license notices retain
their respective licenses.
