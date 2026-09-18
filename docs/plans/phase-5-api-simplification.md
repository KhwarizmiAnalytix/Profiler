# Phase 5 — Simplify the supported API and dependencies

**Status:** Baseline plan written 2026-09-17, on the same Windows/NVIDIA
machine Phase 4 was executed on, informed by a three-way parallel audit of
the current codebase against each of Phase 5's bullets (findings cited
throughout below). Not yet executed.

## Goal

Close out `docs/design-review.md` [section 7, Phase
5](../design-review.md#phase-5--simplify-the-supported-api-and-dependencies):

> Scope: configuration/types, statistics, package targets, installed headers
> and optional compatibility/diagnostic features.
>
> - Consolidate statistics and deprecate unused types/fields from the
>   inventory.
> - Publish supported headers only; preserve compatibility headers for the
>   stated transition period. Replace hard-coded shared-only package
>   metadata with proper exported CMake targets for supported shared/static
>   configurations.
> - Verify native-only consumption does not initialize or require
>   GPU/instrumentation backends. Publish costs and support for optional
>   memory/stack/marker features.
> - Audit all remaining public options for an effect or explicit rejection.
>   Remove obsolete comments and surface supported CPU/CUDA/HIP capabilities
>   consistently.
>
> **Done when:** supported header/package tests pass, native-only builds are
> independent, every retained option has tested behavior, and compatibility
> work does not regress performance or reliability gates.

## Scope

Public API surface (`Profiler/profiler.h` and everything reachable from
it), the CMake package/install story, and the specific dead/duplicated
types and fields design-review.md section 4 already inventoried. Touches
headers, CMakeLists.txt, and any code whose only purpose is to back a
dead/duplicated field or type.

## Non-goals

- Not re-litigating Phase 0-4 (already done — CPU path and GPU validation).
- Not building new profiler features. This phase removes/consolidates, it
  does not add capability.
- Not attempting Phase 6 (architecture-enforcement CI checks, conformance
  workloads across adapters, soak testing) — that phase explicitly depends
  on Phase 5's simplified surface existing first.
- Not touching HIP/ROCm functional behavior (no HIP hardware available,
  same constraint Phase 4 documented) — only its *capability reporting*
  consistency (5.E below), which is testable without HIP hardware.

## The "inventory" this phase refers to

Phase 5 says "deprecate unused types/fields from the inventory." That
inventory already exists — it is **not** something this phase needs to
(re)discover. It's `docs/design-review.md` section 4, "[Classes and structs:
what to keep, simplify, or retire](../design-review.md#4-classes-and-structs-what-to-keep-simplify-or-retire)"
— a table of ~30 types plus a bulleted "Specific inert or misleading
fields" list. Phase 5's job is to *execute* against that inventory, not
re-audit it from scratch. This plan's work items below map directly onto
specific rows of that table, each independently re-confirmed against
current source during this plan's drafting (not just trusted from the
doc's original text, which could have drifted).

## Current state (as of commit `79e67d8`, 2026-09-17)

No prior Phase 5 work exists on this branch. The audit below is this
plan's own baseline, not a report of completed work.

### Statistics: three overlapping representations, none dead

- `Profiler/native/analysis/stats_calculator.h` — `stat<...>`/
  `stat_with_percentiles`, a generic running-statistics accumulator
  (min/max/mean/variance/percentiles), plus `stat_summarizer_options.h`.
- `Profiler/native/analysis/statistical_analyzer.h` — its own,
  independent `statistical_metrics` and `time_series_point` structs.
- `Profiler/native/session/profiler.h:217` — a third, public-facing
  `timing_stats` struct.

design-review.md's own inventory already names this exact overlap ("Used,
overlapping timing representations | Keep one accumulator plus result
views; use a common time unit") and separately confirms the `stat`/
`stats_calculator` family is real, used analysis infrastructure, not a
duplicate to delete outright — the work is convergence, not deletion.

### Dead/inert fields and types (re-confirmed against current source)

| Item | Evidence | Status |
| --- | --- | --- |
| `profiler_options::enable_thread_safety_` (`native/session/profiler.h:120`) | Header's own comment: "Not currently consumed anywhere -- setting this has no effect." No read site. | Already documented dead |
| `profiler_options::thread_pool_size_` (`native/session/profiler.h:158`) | Forwarded only to `statistical_analyzer::worker_threads_hint_`, itself stored but never read (no worker pool ever created). | Already documented dead |
| `profiler_options::output_file_path_` | Setter-only (`profiler.h:721`); `export_to_file`/`export_report` take an explicit `filename` parameter instead. No read site found anywhere. | **Newly found, undocumented** |
| `profiler_options::calculate_percentiles_` | Setter-only (`profiler.h:743`); `timing_stats::calculate_statistics(bool include_percentiles=true)` exists but nothing wires this field into that call. | **Newly found, undocumented** |
| `profiler_options::track_peak_memory_` | Setter-only (`profiler.h:754`); peak-memory tracking in `memory_stats`/`memory_tracker` is unconditional, not gated by this flag. | **Newly found, undocumented** |
| `ProfilerOptions::include_dataset_ops_`, `enable_hlo_proto_`, `duration_ms_`, `repository_path_` (`native/core/profiler_options.h`) | Each already has a doc comment stating "no effect" (TF/XLA-era carryovers). | Already documented dead |
| `profiler_report_builder::include_statistical_analysis_`, `include_memory_details_` | Stored; design-review.md + `profiler_report.cpp` confirm not applied in `build()`. | Already documented dead |
| `gpu_tracer_event::annotation` | Populated by producers; `export_xspace()` never writes it out. | Already documented dead |
| `native/utils/timespan.h` | Zero in-repository `#include` references found; duplicates `native/core/timespan.h`, which is the one actually used. | Unreferenced duplicate |
| `remote_profiler_session_manager_options` | Declaration only, no implementation anywhere in this repository. | Declaration-only, no backing implementation |
| `MetadataCollector` | Registered but disabled in the facade; emits no data even when selected. | Placeholder, no functional path |
| Native `python_tracer` / `python_tracer_stub` | Real implementation disabled; the registered stub reports success without producing data — advertises a capability it doesn't have. | Misleading success path |

### Header publishing: no curation today

`CMakeLists.txt`'s install step:

```cmake
install(
    DIRECTORY "${PROFILER_SRC_DIR}/"
    DESTINATION include
    FILES_MATCHING
    PATTERN "*.h"
)
```

This installs **every** `*.h` under `Profiler/` — `native/`, `bespoke/`,
`common/`, `util/` internals ship alongside the actual public entry point
`Profiler/profiler.h`. There is no curated public-header allowlist and no
"compatibility header" concept anywhere in the build (`grep -rni compat`
across CMakeLists.txt/Profiler/docs finds only unrelated source-level
compatibility-bridge comments, not an install-time mechanism). Both need
to be built, not adjusted from an existing partial implementation.

### Package metadata: not literally "hard-coded shared-only," but not proper exported targets either

Contrary to Phase 5's summary phrasing, `ProfilerConfig.cmake` generation
already branches on `BUILD_SHARED_LIBS` (shared vs. static imported-target
kind, Windows import-lib handling, static-only third-party archive
installation and `INTERFACE_LINK_LIBRARIES` wiring, plus a
`ProfilerLinking.cmake` helper for static-registration survival under
whole-archive linking). But:

- The whole thing is a hand-written `file(GENERATE ...)` string, not
  CMake's standard `install(EXPORT ...)` target-export machinery, and has
  no `write_basic_package_version_file`/version file at all.
- The author's own comment admits static support is "reproduced and
  confirmed fixed" **only** for `PROFILER_BACKEND=KINETO or ITT,
  PROFILER_GPU_BACKEND=none** — a CUDA/HIP static consumer needs additional
  `find_dependency(CUDAToolkit)` wiring that's explicitly "not attempted
  yet."
- `consumer/` (the package-consumption smoke test CLAUDE.md references)
  is minimal and doesn't itself parameterize over shared/static ×
  backend combinations — whatever matrix actually gets exercised comes
  from CI, not this directory.

### Native-only isolation: structurally sound, but unverified by any test

`Profiler/bespoke/**` (which is where every GPU/instrumentation backend's
self-registering static initializer lives — e.g. `RegisterCUDAOrHIPMethods`
in `bespoke/base/cuda.cpp`, similarly in `bespoke/itt/itt.cpp`) is only
globbed into the `Profiler` target's sources when `PROFILER_ENABLE_KINETO`
or `PROFILER_ENABLE_ITT` is set. Under `PROFILER_BACKEND=NONE`, neither is
set, so none of `bespoke/**` is compiled at all — this is a **compile-time
exclusion**, not a runtime no-op, so a native-only build genuinely cannot
initialize a GPU/instrumentation backend (the code isn't in the binary).
This session's CI-fix work today accidentally re-confirmed this the hard
way: a native-only link failure surfaced exactly because `bespoke/base`
wasn't compiled in, which is the mechanism this guarantee relies on.

The gap: no automated test or CI job actually *asserts* this empirically
(e.g., inspecting the built binary's symbol table for zero CUDA/HIP/ITT/
Kineto references, or running under a loader trace to confirm no GPU
driver/context ever gets touched). Today it's true by code-reading, not by
a check that would catch a future regression.

### Public options: mostly real, a few gaps

- `session_options` (12 fields) and `capture_config` (7 fields): **every
  field has a traceable, real effect** — no dead fields found in either.
- `profiler_options` (14 fields): the 5 dead fields in the table above (2
  already documented, 3 newly found), the rest are real.
- `backend_capabilities` (`common/backend_capabilities.h/.cpp`): CUDA and
  HIP both get their own `_compiled` bool and `unavailable_reasons`
  string, so *capability-at-compile-time* is reported consistently. But
  **device availability is not split per-backend** — a single shared
  `gpu_device_available` bool is computed as
  `(cuda_compiled || hip_compiled) && cudaStubs()->enabled()`. A build
  compiling in both CUDA and HIP with only one having a real device would
  incorrectly report the other as "available" too, since nothing records
  *which* backend's device probe actually succeeded. This is the concrete
  gap behind Phase 5's "surface supported CPU/CUDA/HIP capabilities
  consistently."
- One borderline "obsolete comment": `common/capture.h:37`'s Metal-removal
  note reads as accurate historical context, not stale leftover text —
  worth a second look once the `device_enum`/`activity` public contract is
  finalized, but not a clear removal target today.

## Work items

### 5.A — Consolidate statistics

Converge `Profiler/native/analysis/stats_calculator.h`'s `stat`/
`stat_with_percentiles` family, `statistical_analyzer`'s
`statistical_metrics`/`time_series_point`, and `native/session/profiler.h`'s
public `timing_stats` into one accumulator plus thin result/projection
views, per design-review.md's own inventory decision for this row. Concretely:

1. Pick the accumulator: `stat_with_percentiles` is the most complete
   (min/max/mean/variance + percentiles) and already the one
   `stats_calculator` builds on — the natural base.
2. Make `statistical_analyzer`'s `statistical_metrics`/`time_series_point`
   either thin views over that accumulator's output, or removed in favor
   of calling the accumulator directly, whichever keeps
   `statistical_analyzer`'s own public behavior unchanged for its callers.
3. Make the public `timing_stats` (`profiler.h`) project from the same
   accumulator rather than maintaining separate running-statistics logic,
   so "one accumulator, one time unit" actually holds project-wide.
4. Add/adjust `Testing/Cxx/TestStatisticalAnalysis.cpp` coverage to prove
   the three call sites now agree on the same numbers for the same input
   (a regression the current duplication can't rule out today).

### 5.B — Deprecate/remove the dead fields and types

For each row in the "Dead/inert fields and types" table above:

- **Already-documented dead fields** (`enable_thread_safety_`,
  `thread_pool_size_`, the 4 `ProfilerOptions` fields, the 2
  `profiler_report_builder` switches, `gpu_tracer_event::annotation`):
  decide per-field between (a) wiring up real behavior if one was always
  intended, or (b) removing the field outright. A permanent "has no
  effect" comment is not the same as the "explicit rejection" Phase 5
  asks for — pick one, don't leave the disclaimer as the end state.
- **Newly found dead fields** (`output_file_path_`,
  `calculate_percentiles_`, `track_peak_memory_`): same treatment; these
  currently have no disclaimer at all, so at minimum add one before this
  work item, then resolve per the previous bullet.
- **`native/utils/timespan.h`**: forward to `native/core/timespan.h` (the
  actually-used implementation) or remove outright — zero in-repo
  consumers means either is safe; check for external consumers before
  outright removal per the inventory's own compatibility caveat ("installed
  APIs may have external consumers").
- **`remote_profiler_session_manager_options`**: remove from the supported
  API after a deprecation window, or implement remote capture separately —
  per the inventory's own decision. Given no implementation exists at all,
  removal (with deprecation notice) is the pragmatic default absent a
  stated reason to keep a declaration-only type public.
- **`MetadataCollector`**: remove the placeholder from supported
  capabilities (inventory's own decision) — it's registered but never
  produces data regardless of configuration.
- **`python_tracer`/`python_tracer_stub`**: make the stub report
  "unsupported" honestly instead of silently succeeding with no data —
  this is a correctness fix (a caller currently cannot distinguish "Python
  tracing worked and produced nothing" from "Python tracing was never
  really available"), not just cleanup.

### 5.C — Curate published headers + build a compatibility-header mechanism

1. Define the actual public header surface: `Profiler/profiler.h` plus
   whatever it transitively needs consumers to see (check what's already
   `#include`d from consumer-facing code in `examples/` and `consumer/`
   as a starting signal for what "public" already means in practice).
2. Replace the blanket `install(DIRECTORY ... PATTERN "*.h")` with an
   explicit list (or a curated glob scoped to public directories only),
   so `native/`, `bespoke/`, and other implementation-detail headers stop
   shipping to consumers.
3. For anything Phase 5's other work items deprecate (5.B) rather than
   remove immediately, keep the old header installed under a
   `PROFILER_INSTALL_COMPATIBILITY_HEADERS` (default ON) option for a
   stated transition period, with a `#warning`/doc-comment pointing at the
   replacement — this is the "preserve compatibility headers for the
   stated transition period" Phase 5 explicitly asks for, and doesn't
   exist in any form today.
4. Add a build/install test that fails if a non-public header ends up
   installed outside the compatibility allowlist (a regression guard, not
   just a one-time curation pass).

### 5.D — Modernize the CMake package/install story

1. Replace the hand-rolled `file(GENERATE ...)` `ProfilerConfig.cmake`
   with real `install(EXPORT ProfilerTargets ...)` +
   `write_basic_package_version_file()`-generated
   `ProfilerConfigVersion.cmake`, preserving the existing shared/static
   branching behavior (don't regress what already works) while getting
   proper target-import semantics instead of a string template.
2. Extend static-consumer support to the CUDA/HIP case the current
   comment admits is untested: wire `find_dependency(CUDAToolkit)` (and
   the HIP equivalent) into the generated config when
   `PROFILER_GPU_BACKEND` requires it.
3. Extend `consumer/` (or CI's matrix around it) to actually build/link
   against more than the one configuration ("reproduced and confirmed
   fixed" scope today) — at minimum shared+static × KINETO/ITT ×
   gpu=none/cuda, matching the combinations CI's main build matrix already
   exercises for the library itself.

### 5.E — Verify native-only isolation empirically; fix per-backend capability reporting

1. Add an automated check (test or CI step) that a
   `PROFILER_BACKEND=NONE` build's `Profiler` binary contains zero
   CUDA/HIP/ITT/Kineto symbols — e.g. `nm`/`dumpbin /symbols` grep for
   known vendor symbol prefixes, run in CI right after that job's existing
   build step. Turns today's "true by code-reading" guarantee into a
   guarantee CI actually enforces.
2. Split `backend_capabilities::gpu_device_available` into
   `cuda_device_available`/`hip_device_available` (or equivalent), so a
   build with both CUDA and HIP compiled in can't misattribute one
   backend's real device to the other's `supports()` check. Add test
   coverage for the compiled-both, device-available-for-only-one-backend
   case (can be exercised without real HIP hardware — the field-splitting
   logic itself is what's under test, not device behavior).
3. Second pass over `Profiler/common/capture.h:37`'s Metal-removal
   comment once `device_enum`/`activity`'s public contract is otherwise
   settled by this phase's other items — reword or remove only if it's
   still confusing in context at that point, not as a standalone task.

### Deferred — not in scope for this plan

- **Statistics consolidation's exact target shape** (5.A) may turn out to
  need a small migration guide for any external consumer relying on
  `statistical_analyzer`'s current field names — if that surfaces as
  larger than expected, split it into its own follow-up rather than
  quietly narrowing 5.A's scope, same principle Phase 4 applied to 4.C.
- **HIP functional verification** — still blocked on hardware, unchanged
  from Phase 4's conclusion.
- **Phase 6's architecture-enforcement CI checks** — explicitly a
  follow-on phase, not part of this one.

## Validation checklist (fill in as each item completes)

- [ ] 5.A — `stat_with_percentiles` (or chosen accumulator) is the single
      source of running-statistics truth; `statistical_analyzer` and the
      public `timing_stats` project from it; a test proves all three
      report agreeing numbers for the same input
- [ ] 5.B — every dead field/type in the table above is either wired up
      with real behavior or removed (not left as a permanent "no effect"
      comment); `python_tracer_stub` reports unsupported honestly
- [ ] 5.C — only the curated public header set installs by default;
      deprecated headers install under an opt-out compatibility flag with
      a stated transition period; a build/install regression test enforces
      the allowlist
- [ ] 5.D — `ProfilerConfig.cmake` generated via `install(EXPORT ...)` +
      a real version file; static CUDA/HIP consumption wired and tested;
      `consumer/`'s (or CI's) matrix covers shared+static ×
      KINETO/ITT × gpu=none/cuda
- [ ] 5.E — CI asserts zero GPU/instrumentation symbols in a
      `PROFILER_BACKEND=NONE` binary; `gpu_device_available` split
      per-backend with test coverage; Metal comment in `capture.h`
      re-reviewed once the public contract settles
- [ ] Phase 5's "Done when" criteria (top of this document) re-checked
      item by item against what was actually verified, not assumed

## Exit criteria

Phase 5 is done when every checklist item above is checked, with actual
command output/test results recorded (not just "looks fine") in this
document's checklist section or in commit messages referencing it, and
design-review.md's Phase 5 "Done when" clause — supported header/package
tests pass, native-only builds are independent, every retained option has
tested behavior, compatibility work doesn't regress performance/reliability
gates — is satisfied item-by-item, matching the same discipline Phase 4's
plan applied and that this session's two post-push CI regressions (see
`docs/plans/phase-4-gpu-validation.md`) are a concrete reminder of: a claim
without CI/test evidence behind it is not done.

## Amendment, 2026-09-18 — `timing_stats` is dead, not merely duplicative

A parallel macOS-side audit (converging on this same plan before pulling
`origin/main` and finding this document already committed) re-checked the
"three overlapping representations, none dead" claim above with a direct
grep for the `timing_stats_` field itself, not just its type:

```
grep -n "timing_stats_\b" Profiler/native/session/profiler.cpp \
    Profiler/native/session/profiler.h \
    Profiler/native/session/scope_tree_builder.cpp \
    Profiler/native/session/profiler_report.cpp
```

Only one hit: the field declaration at `profiler.h:281`
(`profiler::timing_stats timing_stats_;`). Nothing in `profiler.cpp` (or
anywhere else) ever reads or writes `.timing_stats_`/`->timing_stats_`, and
`timing_stats::add_sample()`/`calculate_statistics()`/`reset()` are defined
(`profiler.cpp:79-154`) but never called from outside their own
definitions. So `timing_stats` isn't a third *live* accumulator competing
with `stat_with_percentiles`/`statistical_metrics` — it's unreachable dead
code end to end (type, field, and all three of its methods).

**Impact on 5.A:** narrows scope, doesn't change direction. Steps 1-2 (pick
`stat_with_percentiles` as the accumulator; make
`statistical_analyzer`'s types thin views over it) are unaffected — that
convergence is between two genuinely live representations. Step 3
("make the public `timing_stats` project from the same accumulator") is
replaced by: delete `timing_stats` and `profiler_scope_data::timing_stats_`
outright (folded into 5.B's dead-code removal pass instead, since there's
no live behavior to preserve or migrate). Step 4's cross-checking test only
needs to cover the two real representations.

## Amendment, 2026-09-18 — 5.A's full merge is not attempted; two real bugs found instead

With `timing_stats` gone (previous amendment), only two representations are
left to reconcile: `stat_with_percentiles<double>` (`stats_calculator.h`)
and `statistical_analyzer`'s own `statistical_metrics`/`calculate_metrics()`.
5.A step 2 called for making the latter "thin views over" the former, "or
removed in favor of calling the accumulator directly, whichever keeps
`statistical_analyzer`'s own public behavior unchanged for its callers"
(5.A's own words). Reading both implementations side by side to plan the
merge found that constraint cannot be satisfied by a thin wrapper:

1. **Different percentile algorithms, not just different code paths.**
   `stat_with_percentiles::percentile(p)` is nearest-rank
   (`values.size() * p / 100`, no interpolation). `statistical_analyzer`'s
   `calculate_percentiles()` linearly interpolates between the two
   surrounding sorted samples. These produce different numbers for the same
   input whenever the exact rank falls between two samples. Projecting
   `statistical_metrics::percentiles` from `stat_with_percentiles` would
   silently change `statistical_analyzer`'s percentile values for existing
   callers -- not a refactor, a behavior change design-review.md section 8's
   own testing philosophy specifically warns against ("compare identities...
   exactly for deterministic synthetic inputs").
2. **A real, independent bug in `stat<ValueType>` itself**, found while
   checking whether at least the non-percentile fields (min/max/mean/
   variance) could safely delegate: `max_`'s initial sentinel was
   `std::numeric_limits<ValueType>::min()`. For an integer `ValueType` that's
   correct (`int64_t::min()` is the most negative representable value, which
   is why `stats_calculator`'s only live instantiation, `stat<int64_t>`,
   never showed a symptom). For a floating-point `ValueType`,
   `numeric_limits<double>::min()` is the smallest *positive* normalized
   value (~2.2e-308), not negative infinity -- an all-negative series would
   never move `max_` off that near-zero sentinel. Reusing `stat<double>`
   inside `calculate_metrics()` as originally planned would have silently
   produced wrong maxima for exactly the kind of data (memory deltas,
   custom samples) `statistical_analyzer` is used for.

**Resolution:** fixed bug 2 for real regardless of the merge decision --
`max_`'s sentinel is now `numeric_limits<ValueType>::lowest()`
(`stats_calculator.h`), which is identical to `min()` for integer types (no
behavior change for the existing `stat<int64_t>` usage) and correct for
floating-point types. New regression test
`Stat.max_tracks_correctly_for_an_all_negative_floating_point_series`
(`TestStatisticalAnalysis.cpp`) reproduces the bug against the old sentinel
and passes against the fix. This is an opportunistic fix per
`.agent-rules/scope.md` (real, low-risk, root-cause, first-party code, not
vendored) surfaced by 5.A's own investigation, not a pre-planned item.

Given finding 1, **the full merge is not attempted.** Forcing it means
either accepting a silent percentile-semantics regression, or first
extending `stat_with_percentiles` with a second, interpolated percentile
mode plus outlier detection/time-series/trend/correlation support it has
never had -- at which point it stops being "reuse" and becomes a rewrite of
a small, working, TF-derived accumulator to serve a second, larger,
already-well-served consumer. `statistical_analyzer` already computes
richer things (outlier detection, time series, trend slope, correlation)
that `stat_with_percentiles` has no equivalent of at all; the two serve
genuinely different call sites (`stats_calculator`'s by-node-type table vs.
session-level statistical analysis) with different precision/shape needs.
5.A's own "Deferred" section anticipated exactly this outcome ("may turn out
to need... if that surfaces as larger than expected, split it into its own
follow-up rather than quietly narrowing 5.A's scope") -- recorded here
instead of attempted, since the added scope buys no correctness or
efficiency improvement, only line-count reduction at real regression risk.
5.A's validation checklist item is satisfied differently than originally
envisioned: not by a cross-checking test proving three representations
agree (there are only two left, and they're proven to intentionally
disagree on percentile semantics), but by the sentinel-bug regression test
above, which is the one piece of overlap investigation that surfaced a real
defect.

## Amendment, 2026-09-18 — 5.D's export-machinery rewrite and CUDA/HIP static wiring not attempted

5.D step 1 (`install(EXPORT ProfilerTargets ...)` +
`write_basic_package_version_file()` replacing the hand-rolled
`file(GENERATE ...)` `ProfilerConfig.cmake`) was evaluated and not
attempted this pass. Reading the current generation logic in full
(`CMakeLists.txt`'s SHARED/STATIC branching, Windows `IMPORTED_IMPLIB`
handling, and the STATIC whole-archive/`profiler_keep_static_registrations()`
registration-preservation block) found it isn't a self-contained export
step that can be swapped out in isolation: `install(EXPORT)` requires the
`Profiler` target's usage requirements (include dirs, link libraries) to
already be expressed as real target properties, which would mean
restructuring how the target itself is defined throughout `CMakeLists.txt`,
not just how it's exported -- a materially larger change than this phase's
mandate, and one this session cannot fully verify: the Windows
`IMPORTED_IMPLIB` path and the static registration-preservation trick (a
previously hard-won fix, re-verified working today in this same session's
5.C commit) have no way to be exercised on this macOS machine. Rewriting
working, recently-hardened, partially-untestable-here logic for a
"cleaner mechanism" with no behavior change carries real regression risk
for no verifiable gain -- left as a follow-up for whoever can test the
Windows leg.

5.D step 2 (CUDA/HIP static consumption via `find_dependency(CUDAToolkit)`)
was also not attempted: this machine has no CUDA toolkit at all
(`PROFILER_HAS_CUDA` is always 0 here), so any wiring added could not be
configured, built, installed, or consumed to prove it actually works --
exactly the kind of unverified claim Phase 4's own history (two of five
claimed items turning out to be dead code) warns against. The existing
code's own comment already states this gap honestly; left as-is rather
than adding untested code.

5.D step 3 (extend the static-consumer CI matrix) **was done**, scoped to
what step 1/2's constraints still allow safely: the `static-link` job was
until now KINETO-only, so ITT's static-install path (installing/
referencing `libittnotify.a` instead of `libkineto.a`) had zero CI
coverage. Verified locally first -- configured, built, tested, installed,
and ran `consumer/` against a fresh `BUILD_SHARED_LIBS=OFF`,
`PROFILER_BACKEND=ITT`, `PROFILER_GPU_BACKEND=none` build, all passing --
then converted the job to a `matrix: backend: [KINETO, ITT]` in
`.github/workflows/ci.yml`. CUDA/HIP legs are not added to this matrix,
consistent with step 2 above.
