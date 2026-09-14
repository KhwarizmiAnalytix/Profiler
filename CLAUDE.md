# Profiler

Standalone C++ profiler for any repository. Public header: `profiler.h`
(in `Profiler/`). CMake target: `Profiler::Profiler`. Repo:
https://github.com/KhwarizmiAnalytix/Profiler

External projects use `find_package(Profiler)` or FetchContent. Do not add a
dependency on any private host project (its Core, Memory, Graph, … modules). A
host project may `add_subdirectory` this tree and pass `PROFILER_THIRD_PARTY_DIR`.

See [README.md](README.md) and [docs/profiler.md](docs/profiler.md).

CI (`.github/workflows/ci.yml`) configures, builds, and runs `ProfilerCxxTests`
on Ubuntu, macOS, and Windows for both `KINETO` and `ITT`. Windows also has
CUDA+NVTX jobs (`PROFILER_GPU_BACKEND=cuda`). CPU Kineto jobs install the
package and build `consumer/`.
