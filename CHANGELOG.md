# Changelog

## Unreleased

- License Profiler under Apache 2.0, with the standard AS IS warranty disclaimer.
  Update project headers and documentation while retaining upstream notices.
- Include LICENSE and NOTICE in installed packages.
- Add unit tests for previously-untested core modules: small_vector, flat_hash,
  lock_free_queue, per_thread, irange, strong_type, align_of, overloaded,
  no_init, parse_annotation, time/math/format/trace_utils, traceme_encode,
  profiler_lock, env_var, stats_calculator, statistical_analyzer,
  scope_tree_builder, and the xplane builder/parsing helpers.
- Fix three latent bugs surfaced by the new tests: `LockFreeQueue::PopAll()`
  and `BlockedQueue`'s move operations referenced undefined methods and could
  not compile; `read_int64_from_env_var()`/`read_float_from_env_var()` let a
  `std::stoll`/`std::stof` exception escape on malformed input instead of
  returning `false`; `stat_with_percentiles::percentile(100)` returned the
  last-inserted value instead of the true maximum.
- Add `PROFILER_ENABLE_COVERAGE` (gcov/lcov) and `PROFILER_SANITIZER`
  (ASan/UBSan/TSan) CMake options, plus CI jobs that build a coverage report
  artifact and run the suite under sanitizers on Ubuntu and macOS.
- Expose `profiler::capture` so applications include only `profiler.h`. Native,
  Kineto, and ITT headers stay on the library side; reports, hotspots, memory
  tracking, and backend capture are available through the public header.
- Move first-party library C++ sources under `Profiler/` (`common/`, `native/`,
  `bespoke/`, `util/`, `profiler.h`). Client includes are unchanged.
- Expose `profiler::session` so native and Kineto/ITT share one client API:
  `start` / `PROFILER_SCOPE` / `stop` / `write_trace`. The compiled backend is
  not selected in application code.
- Restrict coverage reports to first-party sources under `Profiler/`.
- Remove remaining XSigma host CMake hooks and documentation paths so the
  repository is self-contained.

## 1.0.1 — 2026-09-13

- Fix Windows CUDA builds by including the NVTX3 C API header.
- Use compiled fmt consistently across Profiler and Kineto to avoid duplicate
  symbols on Windows.
- Correct native Chrome Trace timestamps and durations to microseconds.
- Add standalone documentation, Holistic Trace Analysis usage, runnable examples,
  and captured output samples.
- Validate the examples and HTA CPU workflow in CI.
- Document the upstream dependency pins: PyTorch Kineto, fmt 12.2.0,
  Intel ITT API 3.28.4, and GoogleTest 1.18.0. Keep Kineto's own nested pins
  unchanged from upstream.
