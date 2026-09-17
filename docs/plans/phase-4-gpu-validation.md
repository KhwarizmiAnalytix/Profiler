# Phase 4 — Validate efficient, reliable CUDA/HIP capture

**Status:** Baseline plan. Written 2026-09-17 on the macOS/CPU-only side of
this repo, for execution on a Windows machine with real NVIDIA GPU hardware
(CUDA/CUPTI). Everything below that requires a physical GPU is unverified
until run there.

## Goal

Close out `docs/design-review.md` [section 7, Phase
4](../design-review.md#phase-4--validate-efficient-reliable-cudahip-capture):
prove the GPU capture path is efficient and reliable on real hardware, not
just compile-tested. Phase 4's own "Done when" criteria:

> multi-stream, multi-thread and supported multi-device workloads retain
> launch links; late callbacks cannot access freed state or contaminate a
> later run; timestamps pass calibration checks; failure scenarios report
> coverage honestly; no profiler-owned per-scope synchronization is
> introduced; CPU/GPU workload targets pass within the declared
> event-rate/memory envelope.

## Scope

Kineto/CUPTI (NVIDIA) only. HIP/ROCprofiler-SDK (AMD) is explicitly
out of scope here — no AMD hardware is available to anyone on this
project yet; see "Deferred" below.

## Non-goals

- Not re-litigating Phase 0-3 (CPU path), already done — see
  `docs/README.md`'s design-review link for that history.
- Not building new profiler features beyond what design-review.md section
  6.3-6.7 already specifies.
- Not attempting HIP/ROCm without real AMD hardware.

## Constraints

- GitHub-hosted CI (`windows-2022` + `Jimver/cuda-toolkit` in
  `.github/workflows/ci.yml`) has the CUDA toolkit but **no physical GPU**.
  It proves compilation only. Every claim below that needs "real hardware"
  must be run manually on this machine and its result recorded here, not
  inferred from a green CI job.
- `TestProfilerGpuRealHardware.cpp`'s tests `GTEST_SKIP()` when
  `profiler::discover_backend_capabilities().gpu_device_available` is
  false — a skip is not a pass. Confirm the test actually *ran* (not
  skipped) before treating it as evidence.

## Current state (as of commit `427409e`, 2026-09-17)

A prior session on this same Windows machine did the first Phase 4 pass
(`225bcb2`..`b02d601`, pushed directly to `origin/main`). It was reviewed
commit-by-commit (not just by its own commit message) from the macOS side,
which found two of its five claimed items were non-functional despite being
described as done. One of those two has since been fixed here; **pull
`origin/main` before starting** to get it.

| Item | Status | Evidence |
| --- | --- | --- |
| Stream-aware `record_with_stream()` / non-blocking `elapsed_nonblocking()` (section 6.6) | **Done** | Correct `cudaEventQuery`/`cudaEventElapsedTime` implementation in `Profiler/bespoke/base/cuda.cpp`; real test coverage in `TestProfilerBackendGpuFallback.cpp` that skips cleanly with no device |
| Generation-based stale-callback filtering (finding 4/7, sections 6.3/6.5) | **Fixed 2026-09-17, commit `427409e`** | Was dead code as of `b02d601`: `add_gpu_tracer_event()` had a comment claiming to stamp `event.generation` with no code doing it, so it stayed 0 forever, and the one caller of `export_xspace()` never passed `current_generation` either. Also found `profiler_session::generation()` is per-*object* (resets to 0 per instance) while most callers construct a fresh session per capture, so two unrelated captures would still collide even with correct wiring. Added a new process-wide `profiler_session::current_capture_generation()` and used that instead. New tests: `export_xspace_filters_stale_generation`, `distinct_captures_get_distinct_generations` in `Testing/Cxx/TestProfilerGpuTracer.cpp`. Verified on `build_ninja`/`build-itt` (CPU-only; the filtering logic itself doesn't need a GPU to test) |
| Clock calibration (section 6.4 item 3) | **Not done — worse than unused** | `Profiler/common/clock_calibration.cpp`'s `calibrate_cuda_device()` hardcodes every sample's `device_ns = 0.0` — it never reads an actual device-side timestamp, so its least-squares fit always hits the degenerate zero-variance branch (`scale=1.0`, `offset=average CPU time`, `uncertainty=0`) regardless of input. It also has zero call sites anywhere in the capture pipeline. This needs real work on real hardware — see Phase 4.B below |
| Structured CUDA failure propagation (section 6.7) | **Not done** | Original commit's own message admits this: "Full status wiring through `profiler_status` in `collect_data` is marked TODO" |
| Real-hardware test coverage (section 8, "Real CUDA/ROCm" row) | **Partial, thin** | `TestProfilerGpuRealHardware.cpp` covers kernel launch + H2D/D2H transfer (smoke-test level: checks `found_any_gpu_event`, not specific event identity) and two concurrent D2D-copy streams (explicitly a loose smoke test — see its own comment: "Skip this detailed check here since it requires parsing XSpace hierarchy"). Missing per section 8's row: fill, CPU overlap, work extending beyond scope/stop, multi-device, explicit stream-event mode end-to-end, device/runtime failure injection, graph workloads |

## Work items

### Phase 4.A — Sync (do this first)

1. `git pull origin main` to get commit `427409e` (generation fix).
2. Rebuild and run `ProfilerCxxTests` with `PROFILER_GPU_BACKEND=cuda` on
   this machine. Confirm `TestProfilerGpuTracer.cpp`'s
   `distinct_captures_get_distinct_generations` and
   `export_xspace_filters_stale_generation` pass (they're CPU-only logic,
   should pass regardless of GPU presence — this just confirms the merge
   didn't break anything on this machine's toolchain).
3. Confirm `TestProfilerGpuRealHardware.cpp`'s two tests actually *run*
   (not skip) on this machine, and record the result here (pass/fail, not
   just "didn't crash").

### Phase 4.B — Fix clock calibration for real

`calibrate_cuda_device()` (`Profiler/common/clock_calibration.cpp`) needs
an actual paired (CPU time, device time) sample, not a hardcoded 0. Options,
roughly in order of how well they fit this codebase's existing CUDA usage
(see `Profiler/bespoke/base/cuda.cpp`'s existing `cudaEvent_t`-based timing):

- Use `cudaEventRecord`/`cudaEventElapsedTime` from a fixed device-side
  epoch event to derive a real `device_ns` per sample, paired with
  `profiler::getTime()` for `cpu_ns` at the same synchronization point
  (mirroring the existing `elapsed()`/`elapsed_nonblocking()` pattern
  already in that file).
- Alternative: if CUPTI activity records already carry a device timestamp
  domain, sample that directly instead of round-tripping through CUDA
  events — check what `Profiler/bespoke/kineto/kineto_shim.cpp` already
  has access to before adding a second, redundant timing mechanism (avoid
  the "fourth canonical store" trap design-review.md section 6.3 warns
  about).

Then:
- Wire it in somewhere real. It has zero call sites today. The natural
  point is once per GPU-tracing session start (`gpu_tracer::start()` in
  `Profiler/native/gpu/gpu_tracer.cpp`), storing the result so
  `capture_event::clock_uncertainty_ns` (`Profiler/common/capture.h:147`,
  also currently always 0) can be populated on export instead of left at
  its default.
- Add a **real-hardware regression test**: record a kernel with a known
  wall-clock duration (e.g. a busy-loop kernel timed via `cudaEventElapsedTime`
  independently), assert the calibrated device→CPU mapping's residual is
  within a stated tolerance. This is the "timestamps pass calibration
  checks" clause of Phase 4's "Done when" — currently nothing tests this
  at all.

### Phase 4.C — Structured CUDA failure propagation

Wire actual CUDA errors (currently only reaching `cudaCheck`/logging) into
`profiler_status` so `collect_data()`'s caller can distinguish "capture
complete", "partial" (some GPU work lost), and "failed" — design-review.md
section 6.7's core requirement, and referenced directly in section 1.1's
"Record loss" and "Session integrity" rows. Concretely: audit
`Profiler/bespoke/base/cuda.cpp` and `Profiler/native/gpu/gpu_tracer.cpp`'s
`collect_data()` for every place a CUDA/CUPTI call can fail silently, and
propagate a real `profiler_status::Error(...)` (or a documented partial
state) instead of continuing as if nothing happened.

Test with actual failure injection where feasible on this hardware — e.g.
requesting an invalid device ordinal, forcing `cudaMalloc` exhaustion, or
similar — per section 8's "Capacity/failure" test-layer row.

### Phase 4.D — Deepen real-hardware test coverage

Extend `TestProfilerGpuRealHardware.cpp` to actually cover section 8's
"Real CUDA/ROCm" row, which today is only partially and loosely covered:

- **Fill** (`cudaMemset`) — not tested at all currently.
- **CPU overlap** — a CPU-side `PROFILER_SCOPE` concurrently active while
  GPU work runs, verifying both appear correctly attributed, not currently
  tested.
- **Work extending beyond scope/stop** — launch async GPU work, call
  `session.stop()` before it completes, verify the capture reports this
  honestly (partial/complete status) rather than either hanging or
  silently dropping it. This directly exercises the generation-filtering
  fix from Phase 4.A/`427409e` under a real timing race, which is the one
  thing a synthetic unit test cannot reproduce.
- **Multi-device** — if this machine has more than one GPU, extend
  coverage; if not, document that multi-device is untested here and needs
  a multi-GPU machine, rather than silently claiming coverage.
- **Explicit stream-event mode end-to-end** — an actual capture using
  `record_with_stream()`/`elapsed_nonblocking()` (Phase 4.A's "done" item)
  through the public capture API, not just the lower-level stub test
  already in `TestProfilerBackendGpuFallback.cpp`.
- **Device/runtime failure** — simulate a device loss/reset scenario if
  this hardware allows it safely; otherwise document as untestable here
  and why.
- **Graph workloads** — only "where advertised"; skip unless CUDA graphs
  are an advertised capability elsewhere in this repo (check first; don't
  add scope that wasn't promised).
- Strengthen `concurrent_streams_not_nested`'s current placeholder comment
  ("Skip this detailed check here since it requires parsing XSpace
  hierarchy") into a real assertion — parse the exported XSpace and assert
  the two streams' events are sibling, not parent/child.

### Phase 4.E — Overhead measurement on the GPU path

`benchmarks/profiler_benchmark.cpp` (Phase 0's tool, see
`docs/benchmarking.md`) is CPU-only today. Design-review.md's 5% target is
specifically "for the default CPU/GPU timeline on the designated
compute/transfer workloads" (section 1.1). Add a GPU-active benchmark
configuration (kernel + transfer workload, capture active vs inactive) and
run it on this machine — this is the only place in the whole redesign
where a 5% GPU-path budget number can be produced at all, since no other
machine in this project has a GPU. Record results in
`docs/benchmarking.md` alongside the existing CPU-only numbers, with this
machine's specs (GPU model, driver/CUDA version) per section 1.1's
"Report each workload... hardware" requirement.

### Deferred — not in scope for this plan

- **HIP/ROCm.** No AMD hardware available anywhere in this project. Leave
  the existing compile-gated stubs as-is until that changes.
- **Structured CUDA failure propagation's full profiler_status wiring**
  may turn out to be larger than Phase 4.C's scope once actually
  attempted (it touches the shared `profiler_collection`/`profiler_status`
  path, not just GPU code) — if so, split it into its own follow-up plan
  rather than silently narrowing Phase 4.C's claim.

## Unrelated item found in the same push, still unresolved

Commit `013224c` ("Remove linter from CI") deleted
`.github/workflows/lint.yml` outright — the repo's only `lintrunner` gate
on every push/PR — with no stated reason, in the same push as the Phase 4
work. This is unrelated to GPU validation. Before doing more work on this
branch: either restore it, or if there was a real reason it broke
(e.g. it doesn't work from a Windows checkout, or a submodule change broke
its `fetch-depth: 0` assumption), state that reason explicitly rather than
leaving it silently gone.

## Validation checklist (fill in as each phase completes)

- [ ] 4.A — `origin/main` pulled at `427409e`+; `ProfilerCxxTests` built
      and run with `PROFILER_GPU_BACKEND=cuda`; real-hardware tests
      confirmed to actually run (not skip)
- [ ] 4.B — `calibrate_cuda_device()` produces a real (non-degenerate)
      scale/offset/uncertainty; wired into a real call site; new
      calibration regression test passes on real hardware
- [ ] 4.C — CUDA/CUPTI failures propagate to `profiler_status` as
      partial/failed, not silently swallowed; failure-injection test(s)
      added
- [ ] 4.D — fill, CPU overlap, work-extending-beyond-stop, explicit
      stream-event mode, and (if hardware allows) multi-device and
      device-failure coverage added to `TestProfilerGpuRealHardware.cpp`;
      `concurrent_streams_not_nested` has a real parent/child assertion
- [ ] 4.E — GPU-active benchmark added to `profiler_benchmark`; results
      recorded in `docs/benchmarking.md` with this machine's hardware spec
- [ ] Lint workflow question resolved (restored or explicitly justified)
- [ ] Phase 4's "Done when" criteria (top of this document) re-checked
      item by item against what was actually verified, not assumed

## Exit criteria

Phase 4 is done when every checklist item above is checked, with actual
command output/test results recorded (not just "looks fine") in this
document's checklist section or in commit messages referencing it, and
design-review.md's Phase 4 "Done when" clause is satisfied item-by-item —
not narrated as done from a commit message alone. The prior pass on this
same machine (`225bcb2`..`b02d601`) is the concrete lesson here: two of its
five claimed items were dead code despite reading as complete.
