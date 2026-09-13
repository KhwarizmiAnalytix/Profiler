# Profiler

Standalone C++ profiler for any repository. Public header: `profiler.h`.
CMake target: `Profiler::Profiler`. Repo:
https://github.com/KhwarizmiAnalytix/Profiler

External projects use `find_package(Profiler)` or FetchContent. Do not add
a dependency on XSigma (Core, Memory, Graph, …). XSigma may `add_subdirectory`
this tree and pass `PROFILER_THIRD_PARTY_DIR`.

See [README.md](README.md) and [docs/profiler.md](docs/profiler.md).

CI (`.github/workflows/ci.yml`) configures, builds, and runs `ProfilerCxxTests`
on Ubuntu and macOS for both `KINETO` and `ITT` backends. The Kineto jobs also
install the package and build `consumer/`.
