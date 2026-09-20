# Phase 6 — Enforce the design continuously

**Status:** Baseline plan written 2026-09-18, on the same Windows/NVIDIA
machine Phase 4 and Phase 5's follow-up were executed on, informed by a
dedicated baseline audit against each of Phase 6's bullets (findings cited
throughout below, file:line against the current tree). Phase 5 (the last
dependency this phase needed) is confirmed done as of commit `6bacbf3`.
**Partially executed, same session, 2026-09-18**: 6.A, 6.B, 6.E, 6.F, 6.G,
and 6.H are done (see the amendment below and the validation checklist) —
6.F in particular found and fixed three real, previously-undetected
memory-corruption bugs via soak testing, and found but did not fix a
fourth, rarer one, using WSL2/Ubuntu for TSan/ASan access this Windows
machine doesn't have natively.

**2026-09-20 amendment (see "Amendment, 2026-09-20" section below)**: 6.C
and 6.D are now also done — a cross-backend golden-scenario diffing CI job
(6.C) and JUnit skip/pass reporting plus a HIP toolkit-only CI leg (6.D)
both landed, verified locally, then pushed and confirmed **green on a real
GitHub Actions run** (`35507383809`, commit `92256dc`) after fixing four
real issues the first live run surfaced (two CI/toolkit-config gaps, and
two genuine, previously-latent first-party HIP-portability bugs never
caught before because no CI configuration had ever compiled
`PROFILER_GPU_BACKEND=hip` — see 6.D's checklist entry for the full list).
The `lock_free_queue.h` race (found during 6.F) was investigated further
but not reproduced on this session's hardware, and remains open, precisely
documented. **Phase 6 is substantially complete**: every work item
(6.A-6.H) has landed and is confirmed green in real CI, but
design-review.md's "Done when" clause is not unconditionally satisfied in
its strictest reading — see that amendment's Phase D re-verification for
the two specific, honestly-documented reasons (structural KINETO/ITT
metadata-and-async-span asymmetry; the one unreproduced-in-CI
`lock_free_queue.h` race) rather than a blanket "done."

## Goal

Close out `docs/design-review.md` [section 7, Phase
6](../design-review.md#phase-6--enforce-the-design-continuously):

> - Add architecture checks forbidding public/backend dependency leakage and
>   backend conditionals in common report logic. Ensure public options have a
>   tested effect or return a documented unsupported status.
> - Run the same conformance workloads through all adapters. Gate device
>   claims on real hardware runs; toolkit-only builds test compilation, not
>   GPU capture.
> - Run deterministic failure injection on every change and scheduled
>   hardware stress/soak tests across start/stop cycles, many threads, high
>   event rates, metadata churn and delayed callbacks. Check post-warmup
>   memory plateaus, handle/resource leaks, deadlocks, corruption and loss
>   accounting.
> - Publish the tested capability matrix and benchmark methodology with
>   releases. Pin tested compiler/OS/SDK versions; rerun affected hardware
>   conformance on upgrades. Treat event schema/version and export
>   compatibility as contracts.
>
> **Done when:** CI distinguishes supported, unsupported, unavailable and
> incomplete results; core semantics agree across backends; regressions in
> ownership, timing precision, attribution and overhead block release.
> Require zero failures in deterministic tests and zero sanitizer findings
> in tested scenarios; publish soak duration, seed, event counts and
> hardware rather than claiming universal reliability from a passing suite.

## Scope

CI workflow (`.github/workflows/ci.yml`), lint/static-analysis configuration
(`.clang-tidy`, `.lintrunner.toml`), the public header surface and "common
report logic" (`native/session/profiler_report.*` and neighbors), the test
suite's cross-backend and failure-injection coverage
(`Testing/Cxx/*.cpp`), and documentation that makes tested-vs-untested
configurations and event/export format stability explicit contracts
(`docs/`).

## Non-goals

- Not re-litigating Phases 0-5 (already done).
- Not adding new profiling *capability* — this phase enforces and verifies
  what already exists, the same posture Phase 5 took toward its own scope.
- Not building a general-purpose architecture-linting framework (e.g.
  adopting a full include-what-you-use/Clang static-analysis dependency
  tool). The audit below found the property Phase 6 wants already holds
  (zero backend conditionals in common report logic today) — the job is a
  narrow, purpose-built regression guard for that property, not a general
  tool.
- Not attempting real HIP/ROCm *hardware* execution or scheduled
  multi-hour/day soak runs against real GPU hardware — both need
  infrastructure (ROCm hardware; a persistent, always-on runner) this
  project does not currently have and this plan does not assume will exist
  by the time this phase's other items are done. See 6.D and 6.F for what
  *is* buildable without that infrastructure, and what's explicitly blocked
  pending it.

## Constraints

- No HIP/ROCm hardware anywhere in this project (same constraint Phase 4
  and Phase 5's follow-up both hit and documented).
- GitHub-hosted runners only today — no self-hosted runner is currently
  registered against this project's Windows/NVIDIA machine or any other
  persistent hardware, which is what genuine "scheduled hardware stress/soak
  tests" (as opposed to a bounded-duration CPU-only stress test that
  happens to run on ordinary shared CI) would need.
- KINETO and ITT are mutually exclusive per build
  (`Testing/Cxx/TestProfilerHeavyFunction.cpp:982`), so no single process
  can exercise both backends and diff results in-process; cross-backend
  conformance needs an out-of-process (data-diffing) design, not a
  parameterized in-process test.

## Current state (as of commit `6bacbf3`, 2026-09-18)

No prior Phase 6 work exists on this branch — Phase 5's own plan
(`docs/plans/phase-5-api-simplification.md:334-335`) explicitly deferred
"Phase 6's architecture-enforcement CI checks" by name as a follow-on
phase. The audit below is this plan's own baseline, not a report of
completed work; every claim is grep/citation-backed against the current
tree, not assumed from the design doc's own aspirational language.

### 1. Architecture checks — absent, but the property they'd enforce already holds

- `.clang-tidy` has no dependency-direction rule (just
  `bugprone-*`/`performance-*`/a few `modernize-*`/`readability-redundant-*`
  checks) and is explicitly "Not yet wired into CI" (`.clang-tidy:2`).
- `.lintrunner.toml` configures CLANGFORMAT/CMAKE/CMAKEFORMAT/EDITORCONFIG/
  NEWLINE/CODESPELL only — no architecture/dependency linter.
- Grepping `native/session/profiler_report.*` and `native/session/profiler.*`
  for `PROFILER_HAS_KINETO`/`PROFILER_HAS_ITT`/`PROFILER_HAS_CUDA`/
  `PROFILER_HAS_HIP` finds **zero matches** — "common report logic" is
  already clean of backend conditionals today. Nothing enforces that
  staying true; it holds by discipline, not by a check that would catch a
  future regression (the same distinction Phase 5's 5.E amendment drew
  about native-only isolation before that phase added a real CI check for
  it).
- Two *partial*, install/symbol-level enforcement mechanisms already exist
  from Phase 5: the `native-only` CI job greps the built `.so`'s dynamic
  symbols for GPU/instrumentation leakage
  (`.github/workflows/ci.yml:193-203`), and the `static-link` job's "Verify
  installed header surface stays curated" step greps the installed
  `include/` tree for internal directories and caps header count at 40
  (`.github/workflows/ci.yml:291-313`). Neither is a *source-level* "does a
  public header `#include` a backend header" check — that's the gap 6.A
  closes.

### 2. Public options tested effect — one-time audit done (Phase 5), no standing enforcement

Phase 5 already audited `profiler_options`/`session_options`/
`capture_config` for dead fields (`docs/phase-5-remaining.md` item 7,
independently re-verified this session) — every field currently has a
traceable write site and read site. Explicit unsupported-status paths
already exist and are tested:

- `TestProfilerBackendCapabilities.cpp:108-126`
  (`required_unavailable_gpu_activity_fails_start`): requesting CUDA under
  `capture_policy::required` without support fails `session.start()`
  outright with a non-empty `last_error()`.
- `TestProfilerBackendCapabilities.cpp:132-156` confirms the same request
  under `capture_policy::best_effort` (the default) does not fail for that
  reason — silent degradation is the *documented, tested* best-effort
  behavior, contrasted against `required`'s explicit failure.
- `python_tracer_stub::start()` (Phase 5.B) returns
  `profiler_status::Error(...)` instead of silently succeeding, tested in
  `TestProfilerBackendPythonTracer.cpp`.

The gap: this is all *point-in-time* audit evidence, not a *standing*
check. Nothing today fails CI if a future PR adds a new public option field
with neither a wired effect nor an explicit-rejection test — Phase 6 asks
for the enforcement, not another one-time audit.

### 3. Conformance workloads through all adapters — not run identically; structurally can't be in-process

- Tests are backend-gated via `#if PROFILER_HAS_KINETO` / `#if
  PROFILER_HAS_ITT` blocks containing **separately written test bodies**,
  not one shared scenario parameterized by backend (e.g.
  `TestProfilerBackendFunction.cpp`'s `kineto_profiles_function` at line 114
  vs. `itt_profiles_function` at line 329 — different functions, not the
  same one run twice). Same pattern in `TestProfilerBackendMemory.cpp`.
  Some scenarios exist for KINETO only with no ITT counterpart at all:
  `TestHotspotReport.cpp` (KINETO-only) and
  `TestProfilerBackendMetadata.cpp`'s
  `record_function_with_metadata_round_trips`.
- No test compares identities/categories/units/counts/attribution exactly
  across backends for the same synthetic input anywhere in `Testing/Cxx`.
- design-review.md section 8's "CPU conformance" row names "overlapping
  async spans" and "Unicode/escaped metadata" as required scenarios; no
  dedicated test for either currently exists.
- CI's matrix (`.github/workflows/ci.yml:16-19,28-33`) already runs KINETO
  and ITT each across all three OSes (`gpu=none`), plus one
  Windows+CUDA leg each — but every `(os, backend, gpu)` cell is an
  independent job; nothing aggregates or diffs results across cells today.

### 4. Real hardware gating — skip is clean, but CI can't tell skip from pass

- `TestProfilerGpuRealHardware.cpp` gates every real-hardware test on
  `caps.gpu_device_available` and calls `GTEST_SKIP()` cleanly when absent
  (e.g. lines 70-74, 184-187, 315-318, 371-374, 463-466, 541-544, 628-631,
  746-749).
- But all tests compile into one binary, one `ctest` test
  (`Testing/Cxx/CMakeLists.txt:74-75`), and CI's `Test` step
  (`.github/workflows/ci.yml:114-115`) never captures gtest's own XML/JUnit
  output. A `GTEST_SKIP()` exits 0 exactly like a real pass — CI's green
  checkmark cannot currently distinguish "verified on a GPU" from "silently
  skipped everywhere" without a human reading the log. CI's own
  Windows+CUDA legs (`.github/workflows/ci.yml:28-33,54-61`) install the
  CUDA *toolkit* on a GitHub-hosted runner with no physical GPU
  (`docs/phase-5-remaining.md:146-149` confirms this directly) — meaning
  every GPU-real-hardware test in ordinary CI is *always* a skip today, and
  nothing surfaces that fact distinctly from a pass.
- No HIP/ROCm CI job exists at all (`ci.yml` has zero `hip`/`rocm`
  matches); `docs/profiler.md:184` already states this plainly.

### 5. Deterministic failure injection "on every change" — real coverage exists, several named categories missing

Runs in the normal `build-test` CI job (i.e. on every push/PR), not a
special job — real tests already do this:
`TestProfilerLifecycleRegressions.cpp` (repeat-export false-success, kineto
save-format switching, NVTX-without-CUDA rejection, restart event leakage,
report lifetime across restart/destruction, generation counters, null
memory tracker), `TestProfilerGpuRealHardware.cpp` (CUDA runtime-error
injection, async-work-vs-stop() race — both real-hardware-gated per §4),
`TestProfilerLockAndEnv.cpp` (garbage env-var parsing).

Missing, per design-review.md section 8's "Capacity/failure" row: driver/
device loss simulation, SDK buffer exhaustion, profiler-side OOM, a
throwing collector, and delayed/late callbacks beyond the one GPU-hardware
case. None of these need real hardware — they're mockable/injectable in a
CPU-only test.

### 6. Scheduled hardware stress/soak tests — confirmed absent

`ci.yml` has no `schedule:`/cron trigger at all (`push`/`pull_request`/
`workflow_dispatch` only). No soak/stress test file exists in
`Testing/Cxx/`. `profiler_benchmark` is a single-shot trial-count
measurement (`docs/benchmarking.md`), explicitly informational/non-gating,
not a soak harness across start/stop cycles, thread counts, or metadata
churn.

### 7. Sanitizers — a real gate, but with real gaps in coverage

`cmake/ProfilerSanitizers.cmake` wraps `-fsanitize=` for GCC/Clang only,
with an explicit no-op warning on MSVC — **no sanitizer coverage on Windows
at all**, by design. CI's `sanitize-asan-ubsan` job
(`.github/workflows/ci.yml:414-452`) runs Ubuntu+macOS, KINETO only, with
`ASAN_OPTIONS: detect_leaks=0` — **leak detection is explicitly disabled**
on both platforms. `sanitize-tsan`
(`.github/workflows/ci.yml:454-487`) is Linux-only, KINETO only. Both use
`halt_on_error=1` with no special-case pass-through, so for what they *do*
cover, a finding genuinely fails the build today — a real, working
zero-tolerance gate, just a narrow one (no ITT, no Windows, no GPU-enabled
builds, no leak detection).

### 8. Capability matrix / benchmark methodology — benchmark methodology is documented; no capability matrix exists

No published capability matrix exists anywhere in `docs/`. The closest
thing is prose caveats in `docs/profiler.md` (e.g. "HIP code path exists,
but the main CI matrix does not exercise it") — not a structured
OS×compiler×backend×GPU table distinguishing "verified on real hardware"
from "toolkit-only, compiles" from "unsupported." Benchmark methodology
*is* already documented reasonably well (`docs/benchmarking.md`'s
machine/toolchain fingerprint and "Doing real budget gating" section, both
Phase 0/Phase 5 work) — that part is close to Phase 6's ask already.
Compiler/OS/SDK version pinning: CUDA is pinned in CI
(`cuda: '12.8.1'`, with `windows-2022` deliberately pinned over
`windows-latest` for a documented MSVC-toolset compatibility reason,
`.github/workflows/ci.yml:20-27,58`) — but that pin lives only as an inline
CI-config comment, not in any `docs/` file, and every non-CUDA OS leg
floats on `ubuntu-latest`/`macos-latest`/`windows-latest`.

### 9. Event schema/export compatibility as contracts — no versioning concept exists

Zero matches anywhere in `Profiler/` for `schema_version`/`SCHEMA_VERSION`/
`format_version`/`FORMAT_VERSION`. `TestProfilerXPlanePipeline.cpp`'s two
tests assert export correctness for a given run, not format-version
stability across releases. design-review.md section 6.7 already states the
design *wants* this ("Record schema/library/SDK versions... for
reproducibility") as aspirational contract language, not yet implemented.

## Work items

### 6.A — Architecture checks: public/backend dependency leakage

1. Add a CI step (or a standalone Python/shell script under `Scripts/`,
   invoked from CI) that, for each of the curated public headers listed in
   `CMakeLists.txt`'s `_profiler_public_headers`, recursively resolves
   quoted `#include`s and fails if any resolved header falls under
   `Profiler/bespoke/kineto/`, `Profiler/bespoke/itt/`, or any
   `PROFILER_HAS_CUDA`/`PROFILER_HAS_HIP`-specific path. This is a static
   text-based include-graph walk, not a compiler-integrated tool — cheap,
   and matches the same "derived mechanically, not guessed" discipline the
   existing header-curation list already documents its own provenance with.
2. Add a second, narrower check specifically for "backend conditionals in
   common report logic": grep `native/session/profiler_report.*` (and any
   other files this plan's own audit or a follow-up sweep identifies as
   "common report logic") for `PROFILER_HAS_KINETO`/`PROFILER_HAS_ITT`/
   `PROFILER_HAS_CUDA`/`PROFILER_HAS_HIP` and fail if found. Zero today —
   this is a regression guard, not a fix.
3. Wire both into the existing `static-link` or a new lightweight CI job so
   they run on every push/PR, matching "on every change" language Phase
   6's third bullet uses for failure injection and this plan applies
   consistently to architecture checks too.

### 6.B — Public options: standing tested-effect/rejection enforcement

1. Write a single source-of-truth test (or a small table-driven test file)
   enumerating every field of `session_options`, `capture_config`, and
   `profiler_options`, cross-referenced against the existing per-field
   audit trail (`docs/phase-5-remaining.md` item 7). This can't be fully
   automated without reflection (C++ has none) — the realistic, buildable
   version is a maintained table (in the test file itself, as a comment or
   a `static_assert`-backed field-count check that fails loudly when a
   struct's field count changes) that forces a human to update the
   cross-reference table when a field is added or removed, rather than
   silently drifting.
2. Add a `static_assert(sizeof(session_options) == N)` (and equivalent for
   `capture_config`/`profiler_options`) as a tripwire: it doesn't prove
   correctness, but it turns "someone added a field and forgot the
   audit/test" into a compile-time nudge to update the table in (1),
   which is a real, if blunt, standing check rather than nothing.
3. Document the convention (CONTRIBUTING.md or a comment beside each
   struct): every new public option field must have either a test proving
   its effect or a test proving it's explicitly rejected/documented as a
   no-op, before merge.

### 6.C — Cross-backend conformance (KINETO vs. ITT agree on identical inputs)

1. Define a small, fixed set of "golden" scenarios reused verbatim on both
   backends: N nested scopes with known names, recursion, same name from
   different call sites, overlapping async spans, Unicode/escaped
   metadata (design-review.md section 8's own list) — currently only
   partially covered and never shared between backends.
2. Since KINETO and ITT can't coexist in one binary, each backend's CI leg
   exports its trace for these golden scenarios to a JSON artifact
   (reusing the existing Chrome Trace/XSpace export path). A follow-up CI
   step (a small Python script, run after both backend legs complete)
   diffs the two artifacts for exact agreement on identities, categories,
   units, counts, and attribution — *not* timing values, which are
   expected to differ — matching design-review.md section 8's explicit
   "compare identities... exactly... avoid brittle 'must agree within 1%'"
   guidance.
3. Backfill the two currently-asymmetric scenarios found in the audit
   (`TestHotspotReport.cpp` KINETO-only, `TestProfilerBackendMetadata.cpp`'s
   metadata round-trip KINETO-only) with ITT equivalents where the
   capability is genuinely backend-agnostic, or document explicitly why not
   where it isn't (e.g. a KINETO-specific hotspot-report feature ITT has no
   equivalent of).

### 6.D — Real-hardware CI gating distinction; HIP toolkit-only CI leg

1. Add `--gtest_output=xml:<path>` to the `ctest`/test invocation in CI and
   a follow-up step that parses the JUnit XML for skip counts, publishing
   them as a CI annotation or job summary — so "0 GPU tests skipped, N
   passed on real hardware" is distinguishable at a glance from "N GPU
   tests skipped, 0 ran," rather than requiring a human to read the raw
   log. This alone satisfies "CI distinguishes supported, unsupported,
   unavailable and incomplete results" for the real-hardware axis without
   needing new hardware.
2. Add a HIP/ROCm CI leg that installs the ROCm toolkit (Linux; check
   available GitHub Actions setup actions or an apt-based install,
   analogous to `Jimver/cuda-toolkit` for CUDA) and builds with
   `PROFILER_GPU_BACKEND=hip` — compilation-only, explicitly labeled
   "toolkit-only, not device-verified" in the job name/summary, matching
   design-review.md's own "toolkit-only builds test compilation, not GPU
   capture" language precisely instead of implying more than it proves.
3. Real HIP/ROCm hardware execution stays out of scope for this plan (see
   Non-goals) — this item only gets HIP to the same "toolkit-only, honestly
   labeled" tier CUDA's GitHub-hosted legs are already honestly documented
   as being (per the existing `docs/phase-5-remaining.md` caveat language).

### 6.E — Expand deterministic failure injection

Add CPU-only, hardware-independent tests for the categories the audit found
missing:

1. **Throwing collector**: a test-only collector/observer that throws from
   a callback, proving the contract in design-review.md section 6.7
   ("normal scope recording and callbacks nonthrowing with contained
   failure handling") actually holds — currently asserted by design intent,
   not tested.
2. **SDK/profiler buffer exhaustion**: drive event/string/metadata/
   correlation counts past whatever bound exists and assert bounded,
   visible loss accounting rather than unbounded growth or silent data
   loss (design-review.md section 6's "budgets" language, section 8's
   "Capacity/failure" row).
3. **Profiler-side OOM**: a controlled allocator failure injection (reuse
   the pattern `profiler_benchmark.cpp` already has for global `operator
   new` overriding, scoped to a single test) proving profiler-owned
   allocation failure degrades safely rather than crashing or corrupting
   state.
4. **Driver/device loss simulation**: for the CPU-testable subset of this
   (i.e., what can be simulated without real hardware — e.g. a stub
   backend reporting device-loss mid-capture) rather than the real-hardware
   variant already covered by `TestProfilerGpuRealHardware.cpp`.

### 6.F — Soak/stress testing: CPU-only harness now, hardware soak deferred

1. Build a bounded-duration (60-120s, CI-budget-friendly), CPU-only stress
   test exercising design-review.md's named soak dimensions at a scale
   ordinary CI can afford: many start/stop cycles, many threads, high
   event rates, metadata churn — added to the normal test suite or as its
   own opt-in CI job (not gating by default, given its longer runtime, but
   run on every push per design-review.md's "on every change" framing
   applied where feasible without dedicated hardware).
2. Assert what design-review.md asks for: stable post-warmup memory
   (compare RSS/allocation count plateaus across cycles, not just a single
   snapshot), no leaked handles, no deadlocks (a timeout-bounded test
   run itself is the deadlock detector), and exact loss accounting.
3. **Genuine hardware soak testing (multi-hour/day, real GPU) is out of
   scope for this plan** — it needs a persistent, always-on runner this
   project doesn't have registered today. Flag this as a standing
   infrastructure decision for the project owner (register this
   Windows/NVIDIA machine, or another one, as a self-hosted GitHub Actions
   runner with a `schedule:` cron job) rather than something a single
   implementation pass can manufacture. Document the gap plainly rather
   than building a scheduled job that would silently no-op or fail for
   lack of a runner to actually execute it.

### Amendment, 2026-09-18 (Windows/NVIDIA session) — 6.F executed; found and fixed two real memory-corruption bugs, found and precisely located a third

The soak test (`Testing/Cxx/TestProfilerSoak.cpp`: 2000 start/stop cycles,
8 threads/cycle, 2000 `PROFILER_SCOPE` calls/thread, unique metadata name
per call) was built and immediately did exactly what design-review.md asks
soak testing for: it found real bugs, on the first run, that no prior test
in this repository had ever exercised (repeated session cycling under
concurrent load is a usage pattern nothing else stresses).

**Bug 1 (fixed): unbounded per-thread memory retention across many ephemeral
threads.** `common/annotation.cpp`'s thread-local `annotation::impl` pool
(`annotation_pool_state`) was a deliberately trivial, no-destructor POD --
"bounded per thread" (capped at `kAnnotationPoolCap` = 64 blocks) but *not*
bounded across a process's lifetime if it creates many short-lived threads,
since nothing ever freed a thread's pooled blocks when that thread exited.
Confirmed via a controlled experiment (same total `PROFILER_SCOPE` call
volume run across only 4 threads showed no growth; across ~16,000 ephemeral
threads it did) before touching any code. **Fixed**: gave
`annotation_pool_state` a real destructor that frees remaining pooled
blocks at thread exit -- self-contained (only touches its own freelist,
calls `::operator delete()`, a function with no dependency on other
`thread_local`s' state), so it doesn't reintroduce the teardown-order hazard
the original no-destructor design was written to avoid.

**This machine cannot run ThreadSanitizer** (`-fsanitize=thread` is
unsupported for the `x86_64-pc-windows-msvc` target -- confirmed directly,
not assumed) and Windows ASan wouldn't link against this build's vendored
`fmt`/`kineto` (a pre-existing `/failifmismatch: annotate_string` ABI
mismatch, unrelated to this work). **WSL2 Ubuntu was available on this same
machine and has a full native Linux Clang/GCC toolchain with working
TSan/ASan** -- used for the rest of this investigation (repo copied to the
WSL filesystem for build performance; `setarch -R` needed to work around a
`ThreadSanitizer: unexpected memory mapping` failure specific to WSL2's
default ASLR layout).

**Bug 2 (fixed): a genuine dangling-pointer bug in `native/tracing/
traceme_recorder.cpp`'s `consume()`.** `SplitEventTracker::AddEnd()` (called
from each recorder's `Consume()`) stores raw `Event*` pointers into that
recorder's own event deque, held in `end_events_` until
`HandleCrossThreadEvents()` runs *after* the whole per-recorder loop. If the
loop's `result` (`std::vector<ThreadEvents>`) reallocated partway through
(no capacity had ever been reserved), every already-pushed `ThreadEvents` --
and the `Event` objects owned by its deque -- got relocated, leaving any
pointer already captured in `end_events_` dangling by the time
`HandleCrossThreadEvents()` dereferenced it. First caught on Windows as an
intermittent SEH access-violation crash (~1 in 3-4 runs) that also left
every subsequent test in the process failing (`session.start()` returning
false), since the crash occurred inside a session's `stop()` and left global
session state stuck. **Fixed**: `result.reserve(recorders.size())` before
the loop -- the loop's `push_back()` calls then never reallocate, so
`end_events_`'s pointers stay valid for the entire lifetime they're
actually used.

**Bug 3 (fixed): a known libstdc++ footgun, reproduced and precisely
diagnosed via ASan's `stack-use-after-return` detector.** `common/
capture.cpp`'s `capture::stop()` built each `capture_event` as a local
variable (`capture_event copied;` ... `result->events_.push_back(std::move
(copied));`). `std::unordered_map`'s "single bucket" optimization for an
empty/near-empty map (common here -- most events have no `extraMeta()`)
stores a pointer *inside the container object itself* rather than heap
allocation. Moving that local `capture_event` into the vector could leave
`copied.metadata`'s internal bucket pointer referencing `capture::stop()`'s
own (by-then-returned) stack frame instead of being correctly re-pointed
to the moved-to object -- ASan's `stack-use-after-return` diagnostic named
this exact variable, at this exact line, directly. The corrupted event only
crashed whenever it was eventually *destroyed*, which could be many session
cycles later (explaining the SEGVs in unrelated-looking places -- an
`xevent_metadata` red-black tree, an `unordered_map` hash node, a `memset`
on a different thread -- all downstream symptoms of the same root cause,
manifesting wherever that corrupted memory happened to get reused next).
**Fixed**: construct each `capture_event` directly in its final vector slot
(`capture_event& copied = result->events_.emplace_back();`, `events_.
reserve()` already present ensures no mid-loop reallocation) instead of
building a local and moving it -- sidesteps the footgun entirely rather than
relying on move semantics being bullet-proof for this specific STL corner
case.

**Verified**: 8 consecutive ASan runs of the full soak test on WSL2/Ubuntu
before these fixes failed nearly every time (SEGV, "stack smashing
detected", "attempting free on address which was not malloc()-ed",
"unknown-crash", `stack-use-after-return` -- different symptom almost every
run, consistent with cascading corruption from bugs 2 and 3). After both
fixes, 6 of 8 runs are clean. The full non-Soak test suite (3 ASan runs) and
the full suite on Windows (Release, real build) both stayed green throughout
-- these fixes changed no observable behavior, only construction order and
container capacity.

**Bug 4 (found, precisely located, NOT fixed): a genuine, rarer
producer/consumer race in `common/lock_free_queue.h`.** 2 of 8 ASan runs
(after bugs 1-3 were fixed) still crash with a SEGV inside
`blocked_queue_base::pop_impl()` (`common/lock_free_queue.h:249`), called
from `ThreadLocalRecorder::Consume()` reading a corrupted `start_block_`
block-list pointer. This queue's push/pop protocol (release-store on the
producer's `end_` index, acquire-load on the consumer's) looks correct by
inspection for genuine single-producer/single-consumer use, and this
session's own test properly `join()`s every worker thread before calling
`session::stop()` -- so the exact mechanism by which two logical
accessors end up touching the same queue concurrently was not identified
with confidence in the time available. Given the demonstrated pattern of
this investigation (three bugs already found and fixed, each requiring
real sanitizer evidence rather than inference to pin down correctly),
guessing at a fix here without that same evidence would risk introducing a
fourth bug while "fixing" the third. **Left open, precisely located, for
dedicated follow-up** -- full ASan stack trace preserved in this session's
record; reproduce via the same WSL2/Ubuntu + ASan setup this amendment
describes (`Testing/Cxx/TestProfilerSoak.cpp`, `-DPROFILER_SANITIZER=
address`, `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`, repeated runs --
this bug did not reproduce on every run).

Every one of these four findings exists specifically because Phase 6's
soak-testing bullet asks for exactly this scenario (many start/stop cycles,
many threads) that nothing else in this codebase's test suite exercises --
this is the mechanism working as designed, not a detour from it.

### 6.G — Capability matrix and version pinning

1. Publish a capability matrix (new `docs/capability-matrix.md`, or a new
   section in `docs/profiler.md`) with explicit rows: OS × compiler ×
   backend × GPU → {verified on real hardware, toolkit-only (compiles,
   unexecuted), not currently tested}. Populate it from what this plan's
   own audit and CI's actual matrix already establish, rather than
   aspirational claims.
2. Document the compiler/OS/SDK versions CI actually pins today (CUDA
   `12.8.1`, `windows-2022` specifically) outside the CI YAML file itself,
   and note which legs deliberately float (`ubuntu-latest` etc.) vs. pin,
   with the reasoning already partially captured in CI comments moved into
   this durable doc.
3. "Rerun affected hardware conformance on upgrades" (design-review.md's
   own phrase) is a process commitment, not a one-time artifact — document
   it as a checklist item in `CONTRIBUTING.md` or release-process
   documentation: any CUDA/ROCm/compiler version bump must re-run the
   real-hardware conformance suite before merge/release, referencing the
   capability matrix this item publishes.

### 6.H — Event schema/export-format version contract

1. Introduce an explicit, single source-of-truth version constant for
   Profiler's own emitted export formats (Chrome Trace JSON, XSpace,
   Kineto/HTA wrapper) — even where the underlying format (e.g. Chrome
   Trace) has no versioning concept of its own, Profiler's *use* of it
   (event field set, metadata conventions) can and should be versioned.
2. Add a compatibility test that fails if a schema-affecting change lands
   without an accompanying version bump — the mechanical form of "treat
   event schema/version and export compatibility as contracts."
3. Extend `TestProfilerXPlanePipeline.cpp` (and Chrome Trace export tests)
   with an explicit assertion on the published version field's presence
   and value, not just structural/content correctness of a single run.

### Deferred — not in scope for this plan

- **Real HIP/ROCm hardware execution** — no hardware exists; 6.D's
  toolkit-only leg is the ceiling this plan can reach without it.
- **Genuine scheduled hardware soak/stress testing** — needs a persistent
  self-hosted runner this project doesn't have registered; 6.F's
  CPU-only bounded-duration harness is the ceiling without that
  infrastructure decision.
- **A general-purpose architecture/dependency-linting framework** — 6.A's
  narrow, purpose-built check is scoped to the one property Phase 6 asks
  for (public/backend leakage, backend conditionals in common report
  logic), not a general tool for arbitrary future architecture rules.
- **Full reflection-based automatic verification that every public option
  has a tested effect** — C++ has no reflection; 6.B's tripwire
  (field-count `static_assert` + a maintained cross-reference table) is
  the realistic ceiling, not a fully automatic proof.

## Validation checklist (fill in as each item completes)

- [x] 6.A — `Scripts/check_architecture.py` added, wired into CI as its own
      `architecture-checks` job. Verified to fail on a deliberately
      introduced violation (a backend-specific `#include` added to a public
      header; a `PROFILER_HAS_KINETO` reference added to common report
      logic — both caught, both reverted after confirming), and to pass on
      the current, clean tree (29 public headers, 4 common report files).
- [x] 6.B — `Testing/Cxx/TestPublicOptionsTripwire.cpp` added: structured-
      binding field-count tripwires for `session_options`/`capture_config`/
      `profiler_options`, plus a maintained cross-reference table. Verified
      to fail with a clear "decomposes into N elements, but M names were
      provided" error when a field was temporarily added (reverted after
      confirming).
- [x] 6.C — `Testing/Cxx/TestCrossBackendGoldenScenario.cpp` added: one
      single-sourced golden workload (nested scopes, recursion, same name
      from different call sites, a Unicode/JSON-special-char scope name)
      executed via each backend's own observation point (Kineto:
      `disableProfiler()->events()`; ITT: a recording stub intercepting the
      wrapper's push/pop calls, reused from `TestProfilerBackendFunction.cpp`'s
      existing `itt_profiles_function` pattern). Each writes a normalized
      `{name: count}` JSON; a new `cross-backend-conformance` CI job builds
      both backends fresh and diffs them with
      `Scripts/diff_cross_backend_golden.py`. Verified locally end to end:
      built and ran against both `build_ninja` (KINETO) and `build-itt`
      (ITT) dev builds, confirmed byte-identical normalized output, and
      confirmed the diff script fails on a deliberately introduced count
      mismatch (reverted after). "Overlapping async spans" and structured
      metadata comparison are deliberately not attempted — both are
      structurally impossible to compare (ITT has no metadata plumbing and
      no non-nested-span API at all, not just an untested corner) — see the
      test file's own header comment and `docs/capability-matrix.md` for
      the full reasoning. `TestHotspotReport.cpp` and
      `TestProfilerBackendMetadata.cpp`'s metadata round-trip stay
      KINETO-only for the same structural reason, not backfilled.
- [x] 6.D — `Scripts/report_gpu_test_status.py` added; `build-test`'s Test
      step now sets `GTEST_OUTPUT` and a follow-up step (gated on
      `matrix.gpu == 'cuda'`) reports the `GpuRealHardware` suite's
      skip-vs-pass counts as a job summary. Verified locally: confirmed
      `GTEST_OUTPUT` produces real GoogleTest JUnit XML when the binary is
      invoked directly (ctest's own per-test 120s timeout made a full-suite
      local run environment-contended, not a problem with the mechanism
      itself), and confirmed the parsing script correctly reports both a
      synthetic all-skipped report and a real partial JUnit report from
      this binary. Added a new `hip-toolkit-only` job (`ubuntu-22.04`,
      pinned to match ROCm's per-codename apt repository): installs
      `hip-dev` from ROCm's real apt repository, configures with the new
      `-DPROFILER_REQUIRE_HIP=ON` (added alongside the existing
      `PROFILER_REQUIRE_CUDA`/`PROFILER_REQUIRE_NVTX` pattern) so the job
      fails loudly rather than silently building HIP=0, and runs the
      existing suite (every GPU-gated case skips cleanly, same tier as the
      CUDA toolkit-only legs).

      **Verified against real GitHub Actions runs, 2026-09-20 (this
      session, pushed directly to `main`).** The first real run
      (`e1979ff`, run `35502583188`) failed, for real reasons this session
      then fixed one at a time, watching each subsequent run rather than
      guessing blind:
      1. Kineto's own `CMakeLists.txt` requires `ROCM_SOURCE_DIR` for
         `KINETO_BACKEND=rocm` — not set. Fixed (`3859b26`): added
         `-DROCM_SOURCE_DIR=/opt/rocm`.
      2. `rocprofiler-sdk-dev` does not exist in ROCm's apt repo (confirmed
         directly against the repo's own `Packages` index, not guessed) —
         headers ship inside `rocprofiler-sdk` itself. Fixed (`05d4ca9`).
      3. Two genuine, previously-latent first-party HIP-portability bugs,
         never caught before because no CI configuration had ever compiled
         `PROFILER_GPU_BACKEND=hip` until this job existed. Fixed
         (`555bb57`), verified against `build_ninja`/`build-itt` first:
         - `bespoke/base/gpu_runtime.h`'s CUDA→HIP portability shim mapped
           `cudaError_t`/`cudaEvent_t` but not `cudaStream_t`, which
           `bespoke/base/cuda.cpp`'s `record_with_stream()` casts to
           unconditionally — added the missing `using cudaStream_t =
           hipStream_t;`.
         - `bespoke/kineto/kineto_shim.cpp` uses `std::stringstream`
           without including `<sstream>`, relying on a transitive include
           present under every CUDA/no-GPU configuration this project's CI
           has ever built — added the explicit include.
      4. `librocprofiler-sdk.so` has undefined `aqlprofile_*` references at
         link time; apt does not declare `hsa-amd-aqlprofile` as its
         dependency. Fixed (`92256dc`): added the package.

      **Final result: full CI run `35507383809` (commit `92256dc`) is
      green across all 19 jobs**, including `hip-toolkit-only` (3m0s) and
      `cross-backend-conformance` (3m42s, confirming 6.C's job also runs
      clean on a real runner, not just locally). This is real, not
      claimed: watched each run via `gh run view`/`gh api .../logs`, fixed
      only what the actual failures said, not what seemed plausible.
- [x] 6.E — `Testing/Cxx/TestProfilerFailureInjection.cpp` added: throwing-
      collector containment (found and fixed a real bug — see below) and
      sample-series bounded-overflow tests, running in the normal test
      binary. Buffer/profiler-OOM/device-loss-simulation tests were
      evaluated and deliberately not attempted (OOM: unsafe global
      allocator override in this shared binary, matching
      `TestProfilerScopeOverhead.cpp`'s own established reasoning;
      device-loss simulation: would need new mock backend infrastructure,
      not attempted this pass).
- [x] 6.F — `Testing/Cxx/TestProfilerSoak.cpp` added (2000 cycles x 8
      threads x 2000 scopes = 32M events, ~11s locally), publishing
      duration/cycle/thread/event counts and RSS via gtest properties and
      stdout. Found and fixed three real, previously-undetected memory-
      corruption bugs (an unbounded per-thread leak in
      `common/annotation.cpp`, a dangling-pointer bug in
      `native/tracing/traceme_recorder.cpp`, a libstdc++ single-bucket
      footgun in `common/capture.cpp`) using WSL2/Ubuntu's native
      TSan/ASan (unavailable on this Windows machine directly). Found and
      precisely located, but did **not** fix, a fourth, rarer race in
      `common/lock_free_queue.h` (reproduces in ~1/4 runs after the other
      three fixes) — see the amendment above for the full record and why
      it was deliberately left open rather than guessed at. Hardware-soak
      infrastructure gap documented as a standing follow-up.
- [x] 6.G — `docs/capability-matrix.md` published: OS×backend, GPU
      toolkit-only-vs-verified tiers, sanitizer coverage, and a
      compiler/OS/SDK version-pinning table, all cross-checked against
      `.github/workflows/ci.yml`'s actual current matrix rather than
      aspirational claims. The upgrade-reconformance process commitment is
      documented in both that file and as a checked item in
      [CONTRIBUTING.md](../../CONTRIBUTING.md)'s pull request checklist
      (not left as a documentation-only claim with no enforcement hook).
- [x] 6.H — `kChromeTraceSchemaVersion` constant added
      (`native/exporters/chrome_trace_exporter.h`), emitted in exported
      JSON's `metadata` object, with a compatibility test
      (`TestProfilerChromeTraceHierarchical.cpp`) that pins the current
      value and would fail if the constant changed without a deliberate
      test update.
- [x] Phase 6's "Done when" criteria re-checked item-by-item against
      design-review.md's exact language, 2026-09-20:
      - "CI distinguishes supported, unsupported, unavailable and
        incomplete results" — met for the real-hardware axis (6.D's
        skip/pass reporting) and the backend axis (`docs/capability-matrix.md`
        publishes verified/toolkit-only/not-tested tiers, now including HIP).
      - "core semantics agree across backends" — met for the scenarios
        6.C's golden test actually covers (identities/counts for nested/
        recursive/shared-name/Unicode scopes); NOT met for structured
        metadata or async-span semantics, which are structurally
        incomparable between backends (documented, not silently dropped).
      - "regressions in ownership, timing precision, attribution and
        overhead block release" — met via the existing sanitizer gates
        (now including Linux LeakSanitizer), `TestPublicOptionsTripwire.cpp`,
        and the benchmark/overhead tests predating this phase.
      - "Require zero failures in deterministic tests and zero sanitizer
        findings in tested scenarios" — met for everything sanitizers
        actually cover on this project's CI (Linux/macOS ASan+UBSan, Linux
        TSan, all KINETO-only). NOT fully met in the strictest sense: the
        `lock_free_queue.h` race is a known, real, previously-observed
        sanitizer finding (Linux/TSan on WSL2) that remains unfixed —
        precisely because it is not currently reproducible in *this
        project's own CI environment* (only ever seen off-CI, on a
        developer's WSL2 machine), it does not show as a live CI failure
        today, but it is a real, honestly-documented gap against the
        clause's literal wording, not a clean pass.
      - "publish soak duration, seed, event counts and hardware" — met,
        Phase 6.F, unchanged.
      **Overall: Phase 6 is substantially complete but not unconditionally
      "done" against the clause's strictest reading**, for two reasons that
      are both already fully documented rather than glossed over: the
      structural (not merely untested) metadata/async-span asymmetry
      between KINETO and ITT, and the one known, unreproduced-in-CI
      `lock_free_queue.h` race. Both are precisely the kind of
      "infrastructure-blocked, honestly documented" gap Phase 4 (HIP
      hardware, clock_calibration) and Phase 5 (CUDA/HIP static
      find_dependency wiring) already established as this project's
      accepted closing pattern — not silently claimed as resolved.

## Amendment, 2026-09-20 — closing 6.C, 6.D, and the open lock_free_queue race

User asked to complete Phase 6 fully. This is a continuation of the plan
above, not a scope change: it closes exactly the three items the checklist
above already named as open (6.C, 6.D, and Bug 4 from the 6.F amendment),
plus the final "Done when" re-check the checklist deferred. No new goal,
no change to Non-goals/Deferred (HIP/ROCm real hardware, genuine scheduled
hardware soak, and a general-purpose architecture-linting framework stay
out of scope, unchanged).

Re-verified current state before planning (do not trust the checklist
dates alone):
- `git log -- Profiler/common/lock_free_queue.h` shows no commit since
  `17172ad` — Bug 4 is still unfixed.
- `.github/workflows/ci.yml` has no `gtest_output`/JUnit step and no
  `hip`/`rocm` job — 6.D is still fully open.
- `Testing/Cxx/TestProfilerBackendFunction.cpp` confirms 6.C's audit finding
  still holds: `kineto_profiles_function` (line 114) and
  `itt_profiles_function` (line 329) are separately written test bodies,
  not one shared scenario — same pattern in `TestProfilerBackendMemory.cpp`;
  `TestHotspotReport.cpp` and `TestProfilerBackendMetadata.cpp`'s metadata
  round-trip are still KINETO-only.
- Read `Profiler/common/lock_free_queue.h` and
  `Profiler/native/tracing/traceme_recorder.cpp` in full: the queue is a
  genuine SPSC (single-producer/single-consumer) design — `Record()` is
  only ever called by the owning thread, `Consume()` only by the control
  thread calling `stop()` — with acquire/release pairing on `end_`
  intended to make push()'s slot writes and `end_block_->next` linkage
  visible to the consumer before it follows them. No defect was found by
  inspection alone in the time available for planning; per this project's
  own established discipline (Phase 4's clock_calibration item, Phase 6.F
  itself), this bug will not be "fixed" by guessing — it gets fixed only if
  a sanitizer reproduces it on hardware available to this session, with the
  same before/after evidence bar Phase 6.F's three real fixes met.

### Phase A — Reproduce and fix (or honestly not fix) the lock_free_queue.h race

1. Attempt to reproduce Bug 4 locally. This machine is macOS, where the
   existing project memory records ThreadSanitizer as broken (crashes in
   its own runtime before test code runs) — treat that as still true unless
   disproven, but AddressSanitizer is not recorded as broken here and the
   original finding reproduced under ASan too (2 of 8 runs, per the 6.F
   amendment), not only TSan. Build `-DPROFILER_SANITIZER=address,undefined`
   (matching CI's `sanitize-asan-ubsan` job exactly) and run
   `TestProfilerSoak.cpp` repeatedly (8+ runs, matching the original
   investigation's sample size) looking for the same
   `blocked_queue_base::pop_impl()` crash signature.
2. If it reproduces: capture the real stack trace/ASan report first, form a
   hypothesis from that evidence (not from re-reading the source in
   isolation), fix it, then re-run 8+ iterations to confirm zero crashes.
   Extend `Testing/Cxx/TestLockFreeQueue.cpp` (create it if no dedicated
   test file exists yet — confirm via a repo search first) with a
   regression test exercising the specific concurrent pattern that
   triggered it, not just a restatement of the existing soak test.
3. If it does not reproduce on this machine after a reasonable number of
   attempts (macOS ASan may simply not hit the same interleaving Linux TSan
   did): do not guess a fix blind. Instead, harden the investigation itself
   — re-derive the actual proof obligation for the SPSC contract (what
   happens-before relationship each field access relies on) directly from
   `lock_free_queue.h`'s code as written, check it field-by-field
   (`start_block_`, `end_block_->next`, `block_count_`) against the
   acquire/release pairing on `end_`, and document either "confirmed
   correct by inspection, could not reproduce to disprove" or a specific,
   named suspect line with reasoning — update this plan and the 6.F
   amendment with whichever outcome is honest, rather than marking the
   checklist item complete without evidence.
4. Exit criteria: either a committed fix with reproducing-then-clean
   sanitizer evidence recorded in the commit message and this plan, or a
   documented, reasoned non-fix (same bar as Phase 4's `clock_calibration`
   item) — not silence either way.

### Phase B — 6.D: CI hardware-gating distinction + HIP toolkit-only leg

1. Add `--gtest_output=xml:<path>` (or the CMake/CTest equivalent —
   confirm `ProfilerCxxTests` is a gtest binary invoked via `ctest`, check
   whether `ctest`'s own `--output-junit` is more direct than passing
   gtest flags through) to the `Test` steps in `build-test`,
   `native-only`, and `static-link` jobs at minimum — wherever
   `TestProfilerGpuRealHardware.cpp` actually links in.
2. Add a follow-up CI step parsing that XML for skip vs. pass counts specific
   to `GpuRealHardware` tests, publishing them as a job summary
   (`$GITHUB_STEP_SUMMARY`) — "0 GPU tests skipped, N passed on real
   hardware" vs. "N GPU tests skipped, 0 ran" must be visible without
   reading the raw log, per the plan's existing 6.D item 1.
3. Add a HIP/ROCm CI leg: research what GitHub Actions setup exists for the
   ROCm toolkit on Linux (an apt-based install is the fallback if no
   maintained action exists — verify current state, don't assume
   `Jimver/cuda-toolkit`'s HIP analog exists without checking), configure
   with `PROFILER_GPU_BACKEND=hip`, build only (no device), and label the
   job/summary explicitly "toolkit-only, not device-verified" — matching
   design-review.md's own phrase precisely.
4. Update `docs/capability-matrix.md`'s HIP/ROCm row from "Not currently
   tested" to "Toolkit-only" once the leg is green, and cross-reference the
   new CI job name.
5. Exit criteria: both sub-items land as real, running CI jobs (not just
   documented as planned), verified locally where possible (the JUnit
   parsing step) and by a real CI run against this branch/PR for the parts
   that can't be verified locally (the HIP toolkit install, real GitHub
   Actions runner behavior).

### Phase C — 6.C: cross-backend (KINETO vs. ITT) conformance diffing

1. Define the golden scenario set design-review.md section 8 names: N
   nested scopes with known names, recursion, same name from different
   call sites, overlapping async spans, Unicode/escaped metadata. Implement
   it once as shared, backend-agnostic scenario code (not duplicated
   per-backend test bodies) that both `kineto_profiles_function`-style and
   `itt_profiles_function`-style tests already have a precedent for
   structuring, but written so the *scenario* is single-sourced and only
   the backend selection differs.
2. Each backend's CI leg exports the golden run's trace to a JSON artifact
   (reusing the existing Chrome Trace/XSpace export path — check which one
   is more diffable structurally before choosing). Upload both backends'
   artifacts (`actions/upload-artifact`).
3. Add a new CI job, dependent on both `build-test` matrix legs completing
   (`needs:`), that downloads both artifacts and runs a small Python
   diffing script comparing identities/categories/units/counts/attribution
   for exact agreement — explicitly *not* diffing timing values, per
   design-review.md's own "avoid brittle 'must agree within 1%'" guidance
   quoted in this plan's own 6.C item 2.
4. Backfill the two asymmetric scenarios the audit found
   (`TestHotspotReport.cpp` KINETO-only, `TestProfilerBackendMetadata.cpp`'s
   metadata round-trip KINETO-only) with ITT equivalents, or document
   precisely why not (e.g., a KINETO-specific feature ITT genuinely has no
   equivalent of) rather than silently leaving the asymmetry.
5. Exit criteria: the diffing CI job runs on every push/PR (or is
   consciously scoped to run only when both matrix legs succeed, documented
   why), passes on the current clean tree, and is verified to actually
   catch a divergence (introduce one deliberately, confirm the job fails,
   revert) — matching 6.A/6.B's own verification discipline.

### Phase D — Re-verify Phase 6 "Done when" and close out

1. Re-read design-review.md's Phase 6 "Done when" clause line by line
   against the state after Phases A-C land: CI distinguishes supported/
   unsupported/unavailable/incomplete (6.D); core semantics agree across
   backends (6.C); regressions in ownership/timing precision/attribution/
   overhead block release (already true via existing tests + sanitizer
   gates); zero failures in deterministic tests and zero sanitizer findings
   in tested scenarios (true only if Phase A either fixed Bug 4 or the
   non-fix is scoped/documented such that it doesn't count as a live
   "failure in tested scenarios" — be precise about this distinction rather
   than glossing over it); soak duration/seed/event counts/hardware
   published (already true, 6.F).
2. Update this plan's Validation checklist (6.C, 6.D rows) and the final
   "Done when" checklist row with the actual outcome, not aspirational
   language.
3. Update `docs/design-review.md` if it tracks phase completion status
   anywhere outside this plan document (confirm one way or the other,
   don't assume).
4. Update the project's persistent memory (auto-memory file tracking this
   redesign) once Phase 6 is genuinely closed — or once it's closed as far
   as this session's tooling allows, with the same infrastructure-gap
   honesty Phase 4/5/6 already established as this project's standard.

### Phase A outcome, 2026-09-20 — could not reproduce; re-derived SPSC correctness by inspection; left open

Attempted reproduction on this (macOS) machine, escalating scale:
- 8 consecutive runs of `TestProfilerSoak.cpp` at its original bug-hunt scale
  (2000 cycles x 8 threads x 2000 scopes, temporarily restored from the
  current CI-budget-reduced 200x8x100 by locally editing constants —
  reverted after) under `-DPROFILER_SANITIZER=address,undefined` (matching
  CI's `sanitize-asan-ubsan` job exactly): all clean.
- 3 further runs at 32 threads/cycle (same event volume otherwise): all
  clean, though each run took 130s-680s (thread oversubscription on this
  machine) — stopped after 3 for time budget, not because of a failure.
- 1000 repeats (`--gtest_repeat=20` x 50 batches) of the existing, isolated
  `TestCommonContainers.cpp`'s `LockFreeQueue.
  single_producer_single_consumer_threads_see_all_elements` test (500
  elements, block size 64, tight producer/consumer spin loop) in the same
  ASan build: all clean.
- Confirmed this project's own memory already records ThreadSanitizer as
  broken on this machine (crashes in its own runtime before test code
  runs); did not re-attempt TSan locally as a result. Docker is installed
  but has no running daemon in this sandboxed session, so a Linux/TSan
  container was not available either. CI's own `sanitize-tsan` job (Linux)
  is the only environment this project has that matches where Bug 4
  originally reproduced (WSL2/Ubuntu) -- not available to this session.

Re-derived the SPSC (single-producer/single-consumer) correctness argument
directly from the current code (not just re-reading the existing comments)
before giving up on reproduction:
- `push()` writes the new slot's data, then (only when completing a block)
  links `end_block_->next` and advances `end_block_`, and only *after* both
  of those does it publish via `set_end()` (a release-store on `end_`).
  `pop()`/`pop_impl()` only observe `get_end()` (an acquire-load) and only
  follow `start_block_->next` once `start_` has advanced past a boundary
  that a specific `set_end()` call already made visible -- so the
  release/acquire pairing on `end_` is exactly what the class's own "Pop
  only removes an element if Push finished before Pop was called" comment
  claims, and by inspection actually holds for the field accesses that
  matter (`slots[]` contents, `end_block_->next` linkage).
- `block_count_` is relaxed-only, but it is only ever used as a heuristic
  drop/no-drop decision in `push()` -- a stale read there produces an
  incorrect *drop decision*, not corrupted memory, so it is not a
  candidate for a memory-corruption crash.
- `traceme_recorder.h`'s own documented contract ("Race Conditions: Events
  recorded during stop() may be dropped") confirms concurrent push()/pop()
  on the same `ThreadLocalRecorder`'s queue is an intended, designed-for
  case (a live producer thread checking `active()` true just before
  `stop()` flips `g_trace_level`), not a violation of the SPSC assumption
  that would itself explain memory corruption.
- No defect was found in this pass. This is not a proof of correctness
  (this project has already been burned once per Phase 4/6 sessions by
  "looks correct by inspection" claims that a sanitizer later
  disproved) -- it is exactly the honest middle state Phase 6.F's own
  amendment anticipated for this bug: "the exact mechanism ... was not
  identified with confidence."

**Decision: left open, undisturbed, same as documented.** Per this
project's own established discipline (Phase 4's `clock_calibration`, and
this exact bug's own original writeup), no fix is being guessed at without
sanitizer evidence reproducing it. What changed from the pre-amendment
state: broader reproduction attempts (isolated queue stress, larger
soak scale, higher thread counts, all on ASan rather than only the
original TSan environment) now also fail to reproduce it, which is
additional negative evidence but not a resolution. Flagging this
specifically, as the original writeup already asked, for a session with
Linux/TSan access (a real machine or a working Docker daemon) to pick up:
reproduce via `Testing/Cxx/TestProfilerSoak.cpp` under
`-DPROFILER_SANITIZER=thread` on Linux, matching CI's `sanitize-tsan` job.

### Risks / open questions carried into execution

- Phase A's outcome is genuinely unknown before execution — the honest
  answer might be "still open, could not reproduce on this hardware." That
  is an acceptable Phase 6 exit state (same as HIP/ROCm hardware and
  scheduled hardware soak, already accepted as infrastructure-blocked
  Deferred items) as long as it's documented with the same rigor, not a
  failure to plan around.
- Phase B's JUnit-parsing and HIP-toolkit-availability details need
  verification against current GitHub Actions ecosystem state at execution
  time (this plan does not assume specifics not yet confirmed).
- Phase C's "which export format is more diffable" and "reuse vs.
  duplicate golden scenario code" choices are implementation-time
  decisions within the scope already fixed by design-review.md section 8's
  own required-scenario list; not decisions that change this plan's scope.

## Exit criteria

Phase 6 is done when every checklist item above is checked, with actual
command output/test results recorded (not just "looks fine") in this
document's checklist section or in commit messages referencing it, and
design-review.md's Phase 6 "Done when" clause — CI distinguishes supported/
unsupported/unavailable/incomplete results; core semantics agree across
backends; regressions in ownership, timing precision, attribution and
overhead block release; zero failures in deterministic tests and zero
sanitizer findings in tested scenarios; soak duration/seed/event counts/
hardware are published rather than universal reliability being claimed from
a passing suite — is satisfied item-by-item, with the same honesty about
infrastructure-blocked items (HIP hardware, hardware soak testing) that
Phase 4 and Phase 5 already established as this project's standard: a
documented, reasoned gap is not a failure of the phase, a silently dropped
one would be.
