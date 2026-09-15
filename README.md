# Profiler

[![CI](https://github.com/KhwarizmiAnalytix/Profiler/actions/workflows/ci.yml/badge.svg)](https://github.com/KhwarizmiAnalytix/Profiler/actions/workflows/ci.yml)

Version **1.0.1** · [Changelog](CHANGELOG.md)

**C++ instrumentation, timelines, and hotspot reports for standalone applications.**

## What is Profiler?

Profiler is a standalone C++ library that turns annotated scopes in your code
into timelines, reports, and hotspot tables — for any repository, not just this
one. Link one target, include one header, and you get:

- **A native CPU tracing pipeline that is always compiled in** — nested scopes,
  timing, memory deltas, and statistics, with zero external dependencies.
- **An optional instrumentation backend, picked at build time** with
  `PROFILER_BACKEND` — Kineto (Perfetto/Chrome Trace JSON, and the input
  Holistic Trace Analysis expects) or Intel ITT (VTune ranges).
- **Optional GPU device activity, picked with** `PROFILER_GPU_BACKEND` — CUDA
  through Kineto/CUPTI, or NVTX ranges.

You annotate with backend-agnostic macros (`PROFILER_SCOPE`, `PROFILER_FUNCTION`)
and drive everything through one type, `profiler::session`; the compiled backend
is an implementation detail your code never names. No LibTorch, TensorFlow
runtime, or Python dependency is required for the C++ library — Python is only
used for the optional, offline Holistic Trace Analysis (HTA) workflow.

[User guide](docs/profiler.md) · [HTA workflow](docs/hta.md) ·
[Output examples](docs/outputs.md) · [Runnable examples](examples/README.md)

## Get it

Requires CMake 3.22+, a C++20 compiler, and Git.

### Clone and build the examples

The fastest way to see it work: clone this repository and build its own
examples and tests.

```bash
git clone --recurse-submodules https://github.com/KhwarizmiAnalytix/Profiler.git
cd Profiler
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

With Visual Studio, binaries land under `build/bin/Release/` with a `.exe`
suffix; other generators put them in `build/bin/`. Dependencies are Git
submodules, with a CMake download fallback — an existing checkout can be
completed with `git submodule update --init --recursive`. See
[third-party dependencies](third_party/README.md).

### Add it to your project

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

For reproducible builds, replace `main` with a reviewed commit SHA. An
installed package is also supported with
`find_package(Profiler CONFIG REQUIRED)`; see the
[installation guide](docs/profiler.md#install-and-find-package).

## Use it in C++

Link **`Profiler::Profiler`**, include **`profiler.h`**, and use the
**`profiler`** namespace. Native collection and the compiled Kineto or ITT
backend run together behind it; never include `native/`, `bespoke/kineto/`, or
`bespoke/itt/` headers directly.

### Beginner: minimal capture

```cpp
#include "profiler.h"

int main() {
    profiler::session session;
    if (!session.start()) return 1;
    {
        PROFILER_SCOPE("work");
        // Your workload. Add nested scopes or PROFILER_FUNCTION().
    }
    if (!session.stop()) return 1;
    return session.write_trace("trace.json") ? 0 : 1;
}
```

On success this exits `0` silently and writes `trace.json` next to your binary;
it only prints to `stderr` if `start()`/`stop()`/`write_trace()` fails. This is
what [examples/example_quickstart.cpp](examples/example_quickstart.cpp) does,
plus a printed checksum and status line:

```bash
./build/bin/example_quickstart
```

```text
checksum=2365.85
Wrote quickstart_trace.json — open in chrome://tracing or https://ui.perfetto.dev
```

`quickstart_trace.json` is Chrome Trace JSON, one event per `PROFILER_SCOPE`/
`PROFILER_FUNCTION`, nested by thread. A real capture looks like this
([full sample](docs/samples/native/native_trace.json)):

```json
{"traceEvents": [
  {"name":"process_name","ph":"M","pid":1,"args":{"name":"/host:CPU"}},
  {"name":"pass","ph":"X","pid":1,"tid":189992,"ts":4.25,"dur":53.583},
  {"name":"compute","ph":"X","pid":1,"tid":189992,"ts":4.375,"dur":39.667}
], "displayTimeUnit": "ms"}
```

Open it in [Perfetto](https://ui.perfetto.dev/) with **Open trace file** to see
`workload` → `pass` → `compute` as a nested timeline.

### Detailed: custom configuration

For anything past "just capture a trace," fill in `session_options` — one
backend-agnostic struct that tunes both the native pipeline and whichever
instrumentation backend (Kineto or ITT) this library was built with:

```cpp
#include <iostream>

#include "profiler.h"

int main() {
    profiler::session_options options;
    options.backend             = profiler::capture_backend::automatic;  // or kineto / itt / nvtx
    options.activities          = {profiler::activity::cpu, profiler::activity::cuda};
    options.memory_tracking     = true;   // native memory_tracker
    options.with_stack          = true;   // capture call stacks (Kineto/ITT)
    options.report_input_shapes = true;   // record operator argument shapes
    options.with_flops          = true;   // estimate FLOPs where an op reports them

    profiler::session session(options);
    if (!session.start()) return 1;

    {
        PROFILER_SCOPE("load_data");
        // ...
    }
    {
        PROFILER_FUNCTION();
        // ...
    }

    if (!session.stop()) return 1;

    session.write_trace("detailed_trace.json");

    auto report = session.generate_report();
    report->export_json_report("report.json");

    auto hotspots = session.generate_hotspot_report();
    std::cout << hotspots->table();
    return 0;
}
```

`session_options` also has `native`/`instrumentation` to run only one pipeline,
and `profile_memory`/`with_modules` for the Kineto/ITT side specifically (as
opposed to `memory_tracking`, which drives the native `memory_tracker`). See the
[user guide](docs/profiler.md) for what each option captures on each backend,
and [examples/example_reports.cpp](examples/example_reports.cpp) for a runnable
version that writes every report format in [Outputs](#outputs) below.

## Outputs

| Need | Capture / export | Read it with |
| --- | --- | --- |
| CPU timeline, nested scopes | `session.write_trace()` / `write_chrome_trace()` | Perfetto, `chrome://tracing` |
| Human/machine-readable report | `session.generate_report()` → `export_console_report()` / `_json_report()` / `_csv_report()` / `_xml_report()` | Terminal, scripts, spreadsheets, XML tooling |
| Aggregated self/inclusive CPU time | `session.generate_hotspot_report()->table()` | Terminal |
| Kineto / HTA operator trace | Same session; `write_trace()` on a Kineto build | Perfetto or HTA |
| VTune / Nsight ranges | Same session on an ITT or NVTX build | External profiler |

[example_reports.cpp](examples/example_reports.cpp) generates every native
format from one capture:

```bash
./build/bin/example_reports build/reports
```

Console report — header excerpt from the real
[report.txt](docs/samples/native/report.txt) it writes:

```text
=== Profiler Profiler Report ===
Session active: no
Duration: 0.172 ms
Total scopes: 10
Max depth: 3
```

Hotspot table — real [hotspots.txt](docs/samples/native/hotspots.txt):

```text
--------------------  ------------  ------------  ------------  ------------  ------------  ------------
Name                    Self CPU %      Self CPU   CPU total %     CPU total  CPU time avg    # of Calls
--------------------  ------------  ------------  ------------  ------------  ------------  ------------
compute                     88.83%     151.832us        88.83%     151.832us      37.958us             4
pass                         8.90%      15.209us        97.73%     167.041us      41.760us             4
workload                     2.27%       3.876us       100.00%     170.917us     170.917us             1
--------------------  ------------  ------------  ------------  ------------  ------------  ------------
Self CPU time total: 170.917us
```

JSON report — excerpt from the real
[report.json](docs/samples/native/report.json):

```json
{
  "header": { "active": false, "scope_count": 10, "max_depth": 3, "duration_ms": 0.172 },
  "scopes": [
    { "name": "compute", "duration_ms": 0.039, "thread": "thread 189992" }
  ]
}
```

CSV and XML reports carry the same data in their own format. See
[output examples](docs/outputs.md) for real samples of every format, column
meanings, and the full JSON/XML schema.

## Holistic Trace Analysis

Generate a Kineto trace and analyze its CPU operators:

```bash
./build/bin/example_hta build/hta-traces
python3 -m venv build/hta-venv
source build/hta-venv/bin/activate
python -m pip install -r examples/requirements-hta.txt
python examples/analyze_hta.py build/hta-traces --output build/hta-results
```

`example_hta` writes `build/hta-traces/rank0.json` — the Kineto **host (CPU)
trace**: no `deviceProperties`/CUDA activities, just the annotated CPU operators
(one event per `PROFILER_SCOPE`/`PROFILER_FUNCTION` call, four `ProfilerStep#N`
iteration markers, four `compute` calls). One real event from that trace
([full sample](docs/samples/hta/rank0.json)):

```json
{"ph":"X","cat":"cpu_op","name":"compute","pid":47034,"tid":189994,
 "ts":6360605645266.826,"dur":46.417}
```

`analyze_hta.py` then prints:

```text
Loaded ranks: [0]
Profiler steps: [0, 1, 2, 3]

Rank 0: CPU inclusive durations (microseconds)
                calls  total_us  mean_us
name
compute             4   183.000   45.750
ProfilerStep#0      1    46.000   46.000
ProfilerStep#1      1    46.000   46.000
ProfilerStep#3      1    46.000   46.000
ProfilerStep#2      1    45.000   45.000

GPU analyses not requested. Use --gpu with a CUDA/CUPTI capture.
Wrote CSV files to build/hta-results
```

and writes one `rankN_cpu.csv` per rank into `build/hta-results`. The
[HTA guide](docs/hta.md) covers CUDA capture, multiple ranks, GPU breakdowns
(device/kernel trace, not this host trace), launch statistics, notebooks, and
troubleshooting, with [verified CPU output](docs/outputs.md#hta-analysis).

## Build configurations

| Option | Default | Purpose |
| --- | --- | --- |
| `PROFILER_BACKEND` | `KINETO` | Instrumentation backend: `KINETO` or `ITT` |
| `PROFILER_GPU_BACKEND` | `none` | `none`, `cuda`, or `hip` |
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

## License

Profiler is licensed under the **Apache License, Version 2.0**.
The code is delivered **AS IS**, without warranties or conditions of any kind,
either express or implied. See [LICENSE](LICENSE) for the full terms and
[NOTICE](NOTICE) for attribution.

Third-party components and source files with separate license notices retain
their respective licenses.
