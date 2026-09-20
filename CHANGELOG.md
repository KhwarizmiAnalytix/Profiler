# Changelog

## Unreleased

- Rename the top-level `Profiler/` source directory to `include/`. Purely a
  source-tree layout change: the installed package's own `include/` layout,
  the public `profiler.h` API, and `Profiler::Profiler` CMake target name are
  all unaffected. Updated `CMakeLists.txt`'s `PROFILER_SRC_DIR`,
  `BUILD.bazel`'s glob patterns, `Scripts/check_architecture.py`,
  `Scripts/helpers/cppcheck.py`, `Scripts/setup.py`'s coverage lcov filter,
  and this repository's living docs (`CLAUDE.md`, `docs/profiler.md`,
  `docs/capability-matrix.md`) accordingly. Historical documents
  (`docs/design-review.md`, `docs/plans/*.md`, `docs/phase-5-remaining.md`,
  earlier `CHANGELOG.md` entries) intentionally keep their original `Profiler/`
  path references, since they describe investigations performed against that
  path at the time.
- Phase 5 (design-review.md) API simplification: remove dead/unreachable public
  surface identified in section 4's inventory instead of leaving it as a
  permanent "has no effect" comment.
  - Removed `timing_stats` (native/session/profiler.h) and the unused
    `profiler_scope_data::timing_stats_` field -- zero call sites anywhere;
    `statistical_analyzer`'s `statistical_metrics` is the one live timing
    accumulator.
  - Removed `remote_profiler_session_manager_options` -- declaration-only,
    no implementation ever existed.
  - Removed `MetadataCollector` and `profile_options::enable_hlo_proto` --
    the collector's only gate was hardcoded `false` at its one production
    call site, so it was never reachable in any configuration.
  - Removed `profile_options::include_dataset_ops`, `duration_ms`,
    `repository_path` -- TF/XLA-era fields with no consumer in this native
    port.
  - Removed `profiler_options::enable_thread_safety_`,
    `thread_pool_size_` (and the now-unused
    `statistical_analyzer::set_worker_threads_hint()`),
    `output_file_path_`, `calculate_percentiles_`, `track_peak_memory_`,
    and their `profiler_session_builder` methods (`with_thread_safety`,
    `with_thread_pool_size`, `with_output_file`, `with_percentiles`,
    `with_peak_memory_tracking`) -- each was a builder call that silently
    did nothing; pass the export path directly to `export_report()`/
    `export_to_file()` instead of `with_output_file()`.
  - Removed the fully disabled (`#if 0`) `native/cpu/python_tracer.{h,cpp}`
    dead-code shell. The registered `python_tracer_stub` now returns an
    explicit `profiler_status::Error(...)` instead of silently succeeding
    with no data when Python tracing is requested.
  - Removed the unreferenced duplicate `native/utils/timespan.h`
    (`native/core/timespan.h` is the one actually used).
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
  `start` / `PROFILER_SCOPE` / `stop` / `write_trace`. `PROFILER_SCOPE` is
  backend-agnostic; `session_options` selects backend, activities, memory
  profiling, stack, flops, and related collection flags.
- Restrict coverage reports to first-party sources under `Profiler/`.
- Remove remaining XSigma host CMake hooks and documentation paths so the
  repository is self-contained.
- Gather all user-facing CMake build options into a single `cmake/ProfilerOptions.cmake`
  module, alongside `ProfilerDependencies`/`ProfilerCoverage`/`ProfilerSanitizers`.
  Formally declare `PROFILER_THIRD_PARTY_DIR` as a cache variable so it shows up
  in `cmake -LH`/`ccmake` like the other options.
- Restructure the README into What is Profiler? / Get it / Use it in C++
  (beginner minimal capture, then detailed `session_options` configuration) /
  Outputs / Holistic Trace Analysis, each with real console output and file
  snapshots (`docs/samples/`) instead of code alone.

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
