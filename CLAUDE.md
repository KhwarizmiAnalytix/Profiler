# XSigmaProfiler

Standalone profiler package. See [README.md](README.md) and
[docs/profiler.md](docs/profiler.md).

Canonical repo: https://github.com/KhwarizmiAnalytix/Profiler

XSigma consumes that repo as the `Packages/XSigmaProfiler` submodule and
passes `PROFILER_THIRD_PARTY_DIR`. External projects use `find_package` or
`FetchContent`. Do not add a dependency on Library/Core, Memory, or Graph.
