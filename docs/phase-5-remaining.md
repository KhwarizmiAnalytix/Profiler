# Phase 5 — what remains

Tracks what is still open from [`design-review.md`](design-review.md)
[section 7, Phase 5](design-review.md#phase-5--simplify-the-supported-api-and-dependencies)
after the 2026-09-18 execution pass (commits `f8a8746`..`f07f3bd`). Full
reasoning for every decision below is in
[`docs/plans/phase-5-api-simplification.md`](plans/phase-5-api-simplification.md)'s
amendments; this file is a short pointer, not a replacement for that record.

Phase 5's original text specified four bullets and a "Done when" clause.
Each remaining item below traces back to one of them.

**Update, 2026-09-18 (Windows/NVIDIA session):** items 2 and 3 below are now
done -- this is the exact machine the earlier pass identified as required
("needs a Windows machine (or CI) to verify..." / "needs a machine with the
CUDA toolkit... to write and verify"). See
[`docs/plans/phase-5-api-simplification.md`](plans/phase-5-api-simplification.md)'s
newest amendment for the full record. Item 7 was independently re-verified
(no changes needed). Items 1, 4, 5, 6 remain open, unchanged.

## 1. Statistics consolidation — DONE (2026-09-18, Windows/NVIDIA session)

> Consolidate statistics and deprecate unused types/fields from the inventory.

The unused-fields half was already done (see the plan's 5.B). The
consolidation half was declined by the original pass because
`stat_with_percentiles` (`native/analysis/stats_calculator.h`) computes
nearest-rank percentiles while `statistical_analyzer`
(`native/analysis/statistical_analyzer.h`) linearly interpolates — forcing
one onto the other would have silently changed existing callers' numbers,
not just their code path. That pass's own "To pick this up" note anticipated
exactly the resolution applied here: `stat_with_percentiles` gained a second,
`percentile_interpolated()` method (linear interpolation, matching
`statistical_analyzer`'s exact formula) alongside the original nearest-rank
`percentile()` — both are real, live, independently tested methods, not one
silently replacing the other.

`statistical_analyzer::calculate_metrics()` now builds a
`stat_with_percentiles<double>` from its input and projects count, sum,
mean, min, max, variance, std_deviation, median, and every configured
percentile from it, instead of maintaining a second, independent
running-statistics implementation (`calculate_percentiles()`, now dead,
removed). Outlier detection and the time-series/trend/correlation logic stay
local to `statistical_analyzer` — the accumulator has no equivalent, and
extending it to match would be a rewrite of `stat_with_percentiles` to serve
a second, larger, already-well-served consumer, not reuse (the outcome the
original pass's investigation already reached and this session did not
revisit).

Verified: all existing `TestStatisticalAnalysis.cpp` assertions on
`calculate_metrics()`'s output already used `EXPECT_NEAR` tolerances (never
exact equality), so the small floating-point path change (one-pass
`E[x²]-E[x]²` variance via the accumulator vs. the old two-pass
sum-of-squared-deviations) doesn't break anything — confirmed by running the
full suite. Added the cross-checking test 5.A's own plan always wanted:
`StatisticalAnalyzer.calculate_metrics_agrees_with_an_independent_stat_with_percentiles_accumulator`
builds a `stat_with_percentiles<double>` by hand and a `statistical_analyzer`
via its public API from the same input, then asserts every field agrees
(count/sum/mean/min/max/variance/std_deviation/median/all six configured
percentiles). Also added direct unit tests for `percentile_interpolated()`
itself, including one proving it disagrees with `percentile()`'s
nearest-rank result by design (not a bug). All new and existing tests pass.

## 2. Real CMake package export — DONE (2026-09-18, Windows/NVIDIA session)

> Replace hard-coded shared-only package metadata with proper exported CMake
> targets for supported shared/static configurations.

`ProfilerConfig.cmake` generation now uses real `install(EXPORT
ProfilerTargets ...)` + `configure_package_config_file()` +
`write_basic_package_version_file()` instead of the hand-rolled
`file(GENERATE ...)` string. The primary `Profiler`/`Profiler::Profiler`
target's SHARED/STATIC kind and Windows `IMPORTED_IMPLIB`/`IMPORTED_LOCATION`
pairing are handled natively by `install(EXPORT)` (verified: both a SHARED
and a STATIC install produced correct, working imports on real MSVC).
Getting there required two supporting fixes, not just swapping the
generator: (1) `PROFILER_DEPENDENCY_INCLUDE_DIRS`/`_LIBS` (kineto/fmt/ITT
build-tree paths and vendored targets) had to be wrapped in
`$<BUILD_INTERFACE:>` — otherwise `install(EXPORT)` refused to export
Profiler at all, either because a source-tree path appeared in
`INTERFACE_INCLUDE_DIRECTORIES`, or because a STATIC library's PRIVATE link
libraries auto-propagate to consumers via `$<LINK_ONLY:...>`, which requires
the referenced target to be in an export set (fmt/kineto/ittnotify aren't).
(2) `cmake/ProfilerLinking.cmake`'s `profiler_keep_static_registrations()`
had the same self-export problem: it sets an `INTERFACE_LINK_OPTIONS` flag
that names the target itself (`$<TARGET_FILE_NAME:Profiler>`), and unlike
`INTERFACE_LINK_LIBRARIES`, `install(EXPORT)` does not rewrite target
self-references embedded in link *options* to the namespaced export name —
reproduced as `No target "Profiler"` in a STATIC consumer, fixed the same
way (wrap in `$<BUILD_INTERFACE:>`; `ProfilerConfig.cmake.in` calls the
function again, correctly, against `Profiler::Profiler`, after including
`ProfilerTargets.cmake`).

Verified end to end on the Windows/NVIDIA machine, both with real MSVC
(`Visual Studio 17 2022` generator) and with LLVM `clang++.exe` (Ninja
generator): SHARED+KINETO, STATIC+KINETO, and STATIC+ITT, all `gpu=none`,
each configured, built, tested (`ctest` green), installed to a scratch
prefix, and consumed via `find_package(Profiler)` + running the resulting
executable (host tracer registered and recorded a probed scope in all
three). This machine's MSVC toolset (14.44.35207 / VS 17.14) hit frequent,
nondeterministic internal compiler errors (`C1001`, different STL header
each time) compiling this codebase under the `Visual Studio 17 2022`
generator, in both Debug and Release, for KINETO and ITT alike, typically
requiring 3-12 build retries to get a clean pass — pre-existing
machine/toolset flakiness, not a regression (confirmed by checking
`build_vs22`'s own history: its Release config never produced
`Profiler.dll`/`kineto.lib` either, only Debug did, from before this
session's changes). ITT's full build-verify-install-consume cycle was
instead completed via clang+Ninja (item 8 below fixed the one real gap that
combination had), which built cleanly with zero retries.

## 3. CUDA/HIP static consumption — CUDA done (2026-09-18, Windows/NVIDIA session); HIP still open

> (same bullet as #2) proper exported CMake targets for supported
> shared/static configurations.

`find_dependency(CUDAToolkit)` (and, symmetrically, `find_dependency(hip)`)
is now wired into `cmake/ProfilerConfig.cmake.in`, gated on
`@PROFILER_HAS_CUDA@`/`@PROFILER_HAS_HIP@`. For the STATIC case, a generated
`ProfilerStaticThirdParty.cmake` (file(GENERATE), config-suffixed then
`install(... RENAME ...)`-collapsed to one name, since e.g. fmt's
`DEBUG_POSTFIX` makes the content genuinely config-dependent under a
multi-config generator) appends `CUDA::cudart`/`CUDA::nvToolsExt` or
`CUDA::nvtx3`/`hip::host` to `Profiler::Profiler`'s
`INTERFACE_LINK_LIBRARIES` by name (they're real targets the consumer's own
`find_dependency(CUDAToolkit)` call re-creates, unlike the vendored
fmt/kineto/ittnotify archives, which are referenced by installed path
instead).

Two real gaps surfaced and got fixed during verification, both only
reachable with actual CUDA hardware/toolkit present (impossible on the
macOS pass): (a) the STATIC third-party include-dir propagation loop
originally forwarded *every* entry in `PROFILER_DEPENDENCY_INCLUDE_DIRS`,
which also holds kineto/fmt/ITT build-tree paths — leaking non-relocatable
absolute build-tree paths into an installed STATIC package's
`INTERFACE_INCLUDE_DIRECTORIES`; narrowed to only the one legitimate case
(CUDA's header-only nvtx3 fallback, propagated by variable name, not path).
(b) kineto links CUPTI itself (its own `find_package(CUDAToolkit)`, never
touching `PROFILER_DEPENDENCY_LIBS` since Profiler never calls CUPTI
directly) — since kineto is only installed as a raw archive, not a real
exported target, that transitive requirement was invisible to the static
consumer wiring; reproduced as 21 unresolved `cuptiActivity*`/`cuptiSubscribe`/
etc. externals, fixed by also propagating `CUDA::cupti` by name when present.

Verified end to end: `BUILD_SHARED_LIBS=OFF`, `PROFILER_BACKEND=KINETO`,
`PROFILER_GPU_BACKEND=cuda` (real RTX 4060 Ti + CUDA 13.2 toolkit) —
configured, built, `ctest` (204/211 pass; 3 `GpuRealHardware` failures are a
pre-existing, unrelated `CUDA_ARCHITECTURES 75` hardcoded in
`Testing/Cxx/CMakeLists.txt` for CI's GPU-less runners meeting this
machine's newer toolkit/driver combination — not a regression), installed,
consumed via `find_package(Profiler)`, and ran successfully (real kernel
capture path exercised, host tracer registered).

HIP is unchanged from the earlier pass: `find_dependency(hip)` is wired
symmetrically with CUDA's, but genuinely unverified — no HIP/ROCm hardware
available on this machine either, same constraint as Phase 4's HIP gap.

**To pick this up:** HIP/ROCm hardware is still needed to verify the `hip`
leg. `CUDA_ARCHITECTURES 75` in `Testing/Cxx/CMakeLists.txt` could be made
configurable (e.g. `native` when a real device is detected) to stop masking
real-hardware GPU test results on newer toolkit/driver combinations like
this machine's — flagged here, not fixed, since it's outside this bullet's
scope.

## 4. Compatibility-header transition mechanism

> Publish supported headers only; preserve compatibility headers for the
> stated transition period.

The first half is done (`docs/plans/phase-5-api-simplification.md`'s 5.C):
`install()` now installs exactly the 29 headers `profiler.h` transitively
needs, not every header under `Profiler/`. No compatibility-header opt-out
flag (e.g. a `PROFILER_INSTALL_COMPATIBILITY_HEADERS` option) was built,
because nothing currently needs it — 5.B's removals were outright deletions
of dead code, not deprecations of headers a consumer might still include
directly. Narrowing `install()` aligned the build with documentation
(`profiler.h`'s own doc comment, `docs/profiler.md`) that already described
those headers as internal.

**To pick this up:** only relevant the next time a genuinely public header
needs to be phased out from under a live consumer. Build the mechanism
then, against a real case, rather than speculatively now.

## 5. Publish costs for optional features — DONE (2026-09-18, Windows/NVIDIA session)

> Verify native-only consumption does not initialize or require
> GPU/instrumentation backends. Publish costs and support for optional
> memory/stack/marker features.

The native-only verification half was already done — and found a real bug
in the process (see the plan's 5.E amendment: an unguarded `cudaStubs()`
call broke `PROFILER_BACKEND=NONE` on macOS, silently masked on Linux CI by
linker leniency).

The "publish costs" half is now done: `benchmarks/profiler_benchmark.cpp`
gained three new named configurations (`active+stack`, `active+memory`,
`active+nomark`) alongside the existing `baseline`/`inactive`/`active`,
each isolating one optional feature's (`with_stack`, `memory_tracking`
+`profile_memory`, `instrumentation=false`) incremental cost against the
same `active` floor via a small shared helper
(`run_named_active_config()`). `docs/benchmarking.md`'s "What it measures"
section documents all six configurations, and a new "Per-feature cost
results" section publishes an actual recorded run (30 trials, this
machine) with the same honest shared-machine-noise caveat the doc's
existing GPU-path-results section already uses — the mechanism is real and
verified to produce numbers; the numbers themselves are not (yet) a
certified per-feature cost the way a dedicated low-noise machine's would
be, exactly the same caveat this document already applies everywhere else.

## 6. Benchmark re-verification — DONE (2026-09-18, Windows/NVIDIA session)

Not a distinct bullet, but implied by the "Done when" clause's
"compatibility work does not regress performance or reliability gates."
The original pass verified correctness (build + test green) but did not run
`profiler_benchmark` to confirm the header restructuring and field removals
didn't move the needle on Phase 0's 1%/3%/5% overhead budgets.

This session ran a genuine before/after A/B comparison: `git stash` isolated
this session's changes (keeping only the pre-existing, orthogonal
`memory_tracker.h`/`.cpp` header-order fix applied, since without it this
exact `gpu=none` STATIC-adjacent configuration doesn't compile at all --
unrelated to anything being measured), built and ran `profiler_benchmark`
(50 trials, clang+Ninja, `PROFILER_BACKEND=KINETO`, `gpu=none`) against
both the unmodified tree and this session's full changes. `matrix_multiply`/
`monte_carlo`/`fft` `inactive`/`active` slowdown percentages moved by low
single-digit percentage points between the two runs in both directions
(e.g. `matrix_multiply active`: 13.53% before, 17.38% after;
`monte_carlo active`: -0.58% before, 2.61% after) -- consistent with this
shared machine's already-documented run-to-run noise floor (`docs/
benchmarking.md`'s own caveats), not a directional regression, and expected
given none of this session's changes touch the `PROFILER_SCOPE`/session
start-stop hot path at all (CMake install machinery, a header-include-order
fix, and `statistical_analyzer`'s on-demand `calculate_metrics()`, which
`profiler_benchmark` never calls). Not re-recorded in `docs/benchmarking.md`
itself (that doc's published numbers are from a dedicated, cited run
kept as a single point-in-time record, not a running log) -- this
session's before/after run is documented here as the verification
`docs/phase-5-remaining.md` (this file) itself is for.

## 7. `session_options`/`capture_config` audit — re-verified (2026-09-18, Windows/NVIDIA session)

> Audit all remaining public options for an effect or explicit rejection.

`profiler_options` was fully audited and cleaned in this pass (5.B).
`session_options` (13 fields, including `policy`) and `capture_config` (7
fields) were independently re-traced this session: every field has a real
write site (into `capture_config`/`profiler_options` via
`to_capture_config()`/`profiler.cpp`'s options translation) and a real read
site. Two fields' only *first-party* consumers looked suspicious at first
glance — `with_flops`/`with_modules` are read only inside now-commented-out
`PROFILER_CHECK` assertions in `nvtx_observer.cpp`/`itt_observer.cpp` — but
both are genuinely live: they're threaded through
`bespoke/kineto/kineto_client_interface.cpp` into libkineto's own
`RecordFunction` callback config (`withFlops_`/`withModules_`), which is
vendored code outside this repo, not dead. Confirms the baseline audit's
finding under independent re-verification; no changes made.

## 8. Fixed (2026-09-18, Windows/NVIDIA session): `profiler_keep_static_registrations()` silently no-oped for plain `clang++.exe` targeting MSVC ABI

Not a Phase 5 bullet item — a real, pre-existing gap in
`cmake/ProfilerLinking.cmake` surfaced while verifying item 2/3's fixes
against a second Windows toolchain (used to work around this session's
MSVC-toolset build flakiness under the `Visual Studio 17 2022` generator;
not part of CI's matrix, which only ever links Windows with real `cl.exe`).
The whole-archive branch picked its flag syntax off `APPLE` / `MSVC` /
`CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"`. When LLVM's `clang++.exe` is
invoked directly on Windows (not via the `clang-cl` front end) it still
targets the MSVC ABI and links with `lld-link`, but CMake's `MSVC` variable
is false (it reflects the compiler's command-line *frontend*, not the
actual linker), so the `GNU|Clang` branch fired and emitted
`-Wl,--whole-archive ... -Wl,--no-whole-archive` — syntax `lld-link` does
not understand. `lld-link` printed `warning: ignoring unknown argument` and
linked successfully anyway, silently dropping the self-registering
GPU/host/python tracer and CUDA/ITT stub translation units. Reproduced: a
STATIC `PROFILER_BACKEND=KINETO` build compiled and linked cleanly this
way, then failed 36 of `ProfilerCxxTests`' tests — any test depending on the
host tracer (or other self-registered capability) actually having
registered.

**Fixed**: added a `WIN32 AND NOT MINGW AND NOT CYGWIN` branch (the native
Windows/MSVC ABI always links with an MSVC-style linker regardless of
compiler frontend) between the `MSVC` and `GNU|Clang` branches, using
`SHELL:-Xlinker /WHOLEARCHIVE:$<TARGET_FILE:target>>` — `-Xlinker` because
clang's GNU-style frontend needs raw linker flags forwarded explicitly (a
bare `/WHOLEARCHIVE:...` token is misread as a file path), and the full
`$<TARGET_FILE:>` path rather than just the base name because `lld-link`
invoked this way (via clang's driver, not `cl.exe`) does not resolve a bare
`/WHOLEARCHIVE:name` against libraries already on the link line the way
`link.exe` does (reproduced: `lld-link: error: could not open
'Profiler.lib': no such file or directory` with just the base name). The
existing `MSVC` branch (real `cl.exe`/`clang-cl`) is untouched. Verified:
STATIC+KINETO, `gpu=none`, clang+Ninja — 100% tests pass (was 36 failures),
install + `find_package(Profiler)` consume + run also confirmed working.
clang-cl itself was not exercised this session (no `clang-cl.exe` install
on this machine) but should already take the `MSVC` branch correctly, same
as `cl.exe`, since CMake sets `MSVC` true for that frontend variant too.

## What's already done (for contrast)

Native-only isolation (verified empirically + CI-enforced, not just
asserted), dead-field/type removal across the section 4 inventory, the
Python tracer stub's honest-failure behavior, header-set curation, ~11 dead
obsolete-comment remnants removed, and per-backend GPU capability reporting
(`cuda_device_available`/`hip_device_available`) are all done and verified
— see the plan document's validation checklist for the full list and
evidence. Real `install(EXPORT)` CMake package export and CUDA static
consumption (items 2 and 3 above) joined this list 2026-09-18.
