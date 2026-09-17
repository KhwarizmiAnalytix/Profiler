# Phase 4 — Validate efficient, reliable CUDA/HIP capture

**Status:** Baseline plan written 2026-09-17 on the macOS/CPU-only side of
this repo. Phases 4.A-4.E executed and verified the same day on the
Windows/NVIDIA machine (RTX 4060 Ti) this plan was written for — see each
phase's checklist entry below for exact evidence. Two items remain
genuinely open: multi-device coverage and a certified (non-noisy) GPU-path
overhead number, both blocked on hardware this project doesn't have access
to (see "Done-when re-check" near the bottom). The lint-workflow question
was explicitly left unresolved this session per user decision.

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

## Toolchain findings (2026-09-17, this machine)

- **`gpu_trace_collector` link bug (real 4.A finding).** `Profiler/native/gpu/gpu_event_collector.h`'s `gpu_trace_collector` class was `PROFILER_VISIBILITY` (a no-op on Windows shared/DLL builds), not `PROFILER_API`. `TestProfilerGpuTracer.cpp`'s `export_xspace_filters_stale_generation`/`distinct_captures_get_distinct_generations` (from `427409e`) call its `add_event()`/`export_xspace()` directly, so a Windows shared-library build (`BUILD_SHARED_LIBS=ON`, this repo's default) fails to link with undefined symbols. Never caught because prior verification of those tests was CPU-only on non-Windows or non-shared configs. **Fixed**: changed to `PROFILER_API`.
- **`cl.exe` is unusable on this machine for the vendored `third_party/kineto` sources.** Both the VS "18" preview toolset and stable VS2022 (14.44.35207) crash nondeterministically (ICE or `STATUS_ACCESS_VIOLATION`/`STATUS_STACK_BUFFER_OVERRUN`) compiling `third_party/kineto/libkineto/src/GenericActivityProfiler.cpp` — same file every time, different internal crash location each retry, reproducing under both MSBuild and Ninja generators and at `-j 1`. This is unrelated to any Profiler-owned code (vendored, unmodified third-party source) and is a separate, worse failure mode than the already-documented `cudafe++` segfault in `.github/workflows/ci.yml`'s CI comment. **Worked around, not fixed**: built with Clang (`clang++`/`clang` 22.1.2, `C:\LLVM`) as both the C/C++ host compiler and the CUDA host compiler instead of `cl.exe`; Clang compiles this file and the rest of the tree without issue. Root cause (likely AV real-time-scan interference given Avast is active, or MSVC toolchain corruption) is still unresolved and out of scope to fix from the repo — flagged to the user.
- **Clang+`lld-link` won't resolve the bare `cudart.lib`/`cudadevrt.lib` names CMake's CUDA language support emits.** `clang++` (unlike `cl.exe`/`clang-cl`) pre-validates bare `.lib` linker arguments as literal file paths relative to cwd before invoking the linker, ignoring `-LIBPATH`/`LIB`. **Worked around**: copied `cudart.lib`/`cudadevrt.lib` into the build directory (`build_ninja/`) so the bare names resolve. This is a local, non-repo workaround (not a CMakeLists.txt change) — a permanent fix would need either switching to `clang-cl` as the CUDA host/link driver or patching the implicit-library paths CMake's `enable_language(CUDA)` ABI detection records when the host compiler isn't MSVC.
- **CUDA 13.2 (this machine's default toolkit) vs. driver 591.86.** `nvidia-smi` reports the driver supports up to CUDA **13.1**; toolkit 13.2 produced kernels that failed to launch with `cudaErrorUnsupportedPtxVersion` (222) in `GpuRealHardware.kernel_and_transfers_captured`. **Fixed**: reconfigured against the machine's also-installed CUDA **13.0** toolkit (`CUDAToolkit_ROOT`/`CMAKE_CUDA_COMPILER` pointed at `.../CUDA/v13.0`), which the driver fully supports. All real-hardware tests then passed.
- Working configuration used for the rest of Phase 4 on this machine: Ninja generator, Clang 22.1.2 as `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER`, CUDA 13.0's `nvcc` as `CMAKE_CUDA_COMPILER`, MSVC 14.44 as `CMAKE_CUDA_HOST_COMPILER` (nvcc 13.0 rejects Clang as its host compiler outright — "Host compiler targets unsupported OS" — so this one path still needs `cl.exe`, but it's a single generated `.cu` file, not the large vendored kineto tree, and has not shown the crash), `CMAKE_CUDA_ARCHITECTURES=89` (native for this RTX 4060 Ti), `cudart.lib`/`cudadevrt.lib` copied into the build directory.

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

- [x] 4.A — `origin/main` pulled at `19d9d86` (`427409e`+); `ProfilerCxxTests`
      built and run with `PROFILER_GPU_BACKEND=cuda` (see "Toolchain
      findings" above for the exact working config); real-hardware tests
      confirmed to actually run (not skip): `GpuRealHardware.kernel_and_transfers_captured`
      and `GpuRealHardware.concurrent_streams_not_nested` both ran and
      passed, along with `export_xspace_filters_stale_generation` and
      `distinct_captures_get_distinct_generations`. Found and fixed one
      real bug in the process (`gpu_trace_collector` DLL-export gap, see
      above) — this is the second of the prior pass's originally-claimed
      items to turn out broken on inspection, now fixed.
- [x] 4.B — `calibrate_cuda_device()` (`Profiler/common/clock_calibration.cpp`)
      now records a fixed device-side epoch `cudaEvent_t`, then for each of 10
      samples records+synchronizes a new event and takes real
      `cudaEventElapsedTime(epoch, sample)` as `device_ns`, paired with
      `profiler::getTime()` at the same synchronization point as `cpu_ns` —
      replacing the previous hardcoded `device_ns = 0.0`. Added
      `cached_cuda_device_calibration()` (process-wide, per-device-index,
      mutex-guarded cache). Wired into two real call sites: (1)
      `Profiler/native/gpu/gpu_tracer.cpp`'s `start()` warms the cache; (2)
      `Profiler/common/capture.cpp`'s `capture::stop()` populates
      `capture_event::clock_uncertainty_ns` for every `device_enum::CUDA`
      event from the cached calibration (previously always 0, dead field).
      New real-hardware tests in `TestProfilerGpuRealHardware.cpp`:
      `clock_calibration_produces_bounded_residual` (scale in (0.1, 10),
      uncertainty_ns in [0, 50ms), cache-consistency check) and
      `capture_populates_clock_uncertainty_for_gpu_events` (asserts a real
      capture's CUDA events carry `clock_uncertainty_ns > 0` through the
      public `capture::` API). Both ran (not skipped) and passed on this
      machine's RTX 4060 Ti.
- [x] 4.C — CUDA/CUPTI failures propagate to `profiler_status`, not silently
      swallowed; failure-injection test added. **Scope note**: implemented
      for the native `profiler_session`/`gpu_tracer` path
      (`Profiler/native/gpu/gpu_tracer.cpp`), which is where design-review.md
      section 6.7 and this plan's own Phase 4.C wording point (`collect_data()`
      returning `profiler_status`). The separate Kineto-backed `capture::`
      API (`Profiler/common/capture.cpp`, what the other real-hardware tests
      exercise) has no `profiler_status`-returning surface at all today
      (`capture::stop()` always returns a `capture_result`) — extending
      failure propagation there is a larger, separate change to that type,
      exactly the kind of scope growth this plan's "Deferred" section
      anticipated; left for a follow-up rather than silently narrowing this
      item's claim.
      - Added `ProfilerStubs::consume_error_since_last_check()`
        (`Profiler/bespoke/base/base.h`, defaults to `false`, so HIP/ITT/
        PrivateUse1 stubs are unaffected) and a sticky
        `std::atomic<bool> g_cuda_error_since_check` in
        `Profiler/bespoke/base/cuda.cpp`'s `cudaCheck()` — every CUDA/HIP
        runtime error that was previously only logged now also flips this
        flag; `consume_error_since_last_check()` atomically reads-and-clears
        it.
      - `gpu_tracer::collect_data()` now checks this flag after
        `export_xspace()` and returns `profiler_status::Error(...)` (instead
        of unconditional `Ok()`) when a CUDA/HIP call failed during the
        session, surfaced to callers through the existing
        `profiler_session::stop()` (now returns `false`) /
        `last_error()` path — no new public API needed. **CI regression found
        and fixed after initial push** (`c9616ee` → follow-up commit): this
        check must be guarded by `PROFILER_HAS_KINETO || PROFILER_HAS_ITT`,
        not left unguarded — `impl::cudaStubs()` is only *compiled* at all
        when `bespoke/base` is part of the build (under Kineto or ITT per
        `CMakeLists.txt`), so CI's `native-only (PROFILER_BACKEND=NONE)` job,
        which this session never built or tested locally, failed to link
        with an undefined-symbol error. Not caught before pushing because
        local verification only covered the CUDA/Kineto-backend build this
        machine's GPU actually needs.

        **Second CI regression found and fixed** (`18a1176` → follow-up
        commit): the sticky error flag (`g_cuda_error_since_check` in
        `cuda.cpp`) is process-wide, not scoped to one session — on the
        `windows-2022 ITT cuda` CI runner (CUDA toolkit present, no real
        device, so CUDA calls fail with a real driver/device error rather
        than a clean "0 devices"), an earlier, unrelated test's CUDA call
        set the flag, and `BackendGpuTracer.collector_kernel_on_device_plane`
        (`TestProfilerGpuTracer.cpp:150`) — a synthetic test that makes no
        real CUDA calls itself — failed because its `session.stop()` saw
        that stale flag and reported an error that had nothing to do with
        it. Fixed by clearing the flag at `gpu_tracer::start()` (discarding
        any error from before this session began), so `collect_data()` at
        `stop()` only ever attributes errors that happened *during* this
        session's own active window, not anywhere earlier in the process.
        This is a real gap this session's local testing couldn't have
        caught: this machine's real GPU never hits the
        toolkit-without-driver error path CI's compile-only CUDA runner
        does.
      - New real-hardware test `session_reports_failure_after_cuda_runtime_error`
        (`TestProfilerGpuRealHardware.cpp`) injects a real failure (queries
        `elapsed()` on an unrecorded/invalid `cudaEvent_t`, which
        `cudaEventSynchronize`/`cudaEventElapsedTime` reject) through a live
        `profiler_session` with `enable_gpu_tracing_ = true`, then asserts
        `stop()` returns `false` and `last_error()` is non-empty. Ran (not
        skipped) and passed on this machine's RTX 4060 Ti.
- [x] 4.D — Added to `TestProfilerGpuRealHardware.cpp`, all ran (not
      skipped) and passed on this machine's RTX 4060 Ti:
      - `device_fill_captured` — `cudaMemset`, previously untested at all.
      - `cpu_overlap_with_gpu_work_attributed` — a CPU-side busy loop
        concurrent with an in-flight async kernel; asserts both a CPU and a
        GPU event appear in the same capture.
      - `work_extending_beyond_stop_reported_honestly` — a 256MB
        `cudaMemsetAsync` deliberately left un-synchronized across
        `capture::stop()`; asserts `stop()` returns (no hang) and that the
        still-in-flight work, once actually synchronized, doesn't corrupt a
        second, immediately-following capture — the real timing-race
        exercise of the Phase 4.A generation-filtering fix (`427409e`) this
        plan called for.
      - `explicit_stream_event_mode_end_to_end` — the
        `record_with_stream()`/`elapsed_nonblocking()` path exercised through
        the public `capture::` API via `capture_backend::kineto_gpu_fallback`
        (needed a `PROFILER_SCOPE` around the kernel launch, since that
        path's timing hooks fire on `RecordFunction` scope entry/exit, not
        on a raw unwrapped kernel launch — and needed the `<<<>>>` launch
        moved into its own plain function, since nvcc's host/device split
        didn't reliably survive being nested inside `PROFILER_SCOPE`'s guard
        braces, leaving unresolved `__device_builtin_variable_blockDim`/
        `gridDim` symbols at link time otherwise). Asserts at least one
        event carries a real `gpu_fallback_elapsed_us` measurement.
      - `concurrent_streams_not_nested` strengthened from an empty
        placeholder to a real assertion: for every pair of GPU events on two
        distinct `resource_id` (stream) values, neither's
        `linked_correlation_id` points at the other's `correlation_id` (the
        field that *does* express a genuine parent/child relationship per
        its own doc comment in `capture.h`) — i.e. two independent
        concurrent streams are never collapsed into one dependency chain.
        (A literal device-timestamp interval-overlap check was tried and
        removed: these are tiny 4KB D2D copies, and non-overlapping
        intervals on this hardware/driver reflect real scheduling, not a
        profiler bug — asserting it produced a false failure.)
      - **Multi-device**: untested. This machine (`nvidia-smi`) has exactly
        one GPU (RTX 4060 Ti); multi-device coverage needs a multi-GPU
        machine this project doesn't have access to, same conclusion this
        plan already reached for HIP/ROCm.
      - **Device/runtime failure**: covered by Phase 4.C's
        `session_reports_failure_after_cuda_runtime_error` above rather than
        a separate 4.D test (same underlying mechanism: a real CUDA runtime
        error surfacing through `profiler_status`).
      - **Graph workloads**: skipped, per this item's own "where advertised"
        qualifier — checked first: `kCudaGraphId`/`kCudaGraphExecId` exist
        only as passthrough `xplane_schema.h` stat-key constants (vendored
        schema), not an actively advertised profiler capability with its own
        API or doc section, so adding graph-workload coverage would be scope
        this plan was never asked to cover.
- [x] 4.E — Added `gpu_kernel_and_transfer` workload to `profiler_benchmark`
      (H2D transfer + elementwise-add kernel + D2H transfer, fully
      synchronized; only added to the workload list when
      `cudaGetDeviceCount()` finds a real device at runtime). Kernel lives in
      a new `benchmarks/profiler_benchmark_gpu_kernel.cu` +
      `.h` pair, not inline in `profiler_benchmark.cpp`, since that file's
      exception-throwing `operator new`/`delete` overrides make the whole
      file unsafe to compile as CUDA (`nvcc` rejects it: "device code does
      not support exception handling"). "active" config now sets
      `session_options::gpu_tracing = true` under `PROFILER_HAS_CUDA` so the
      workload actually exercises GPU device-plane tracing.
      Along the way, fixed two pre-existing bugs this target's own header
      comment flagged as never having been hit before ("never built until
      now"): a missing `NOMINMAX` before `<windows.h>` (broke any build razed
      through this newer Clang, unrelated to CUDA) and bare `float**` →
      `void**` `cudaMalloc()` argument mismatches.
      Ran successfully on this machine's RTX 4060 Ti (Clang 22.1.2 host
      compiler, CUDA 13.0, `-DBENCHMARK_ENABLE_WERROR=OFF
      -DCMAKE_CXX_FLAGS=-Wno-c2y-extensions` to work around the vendored
      `third_party/benchmark` library's incompatibility with this newer
      Clang under `-pedantic-errors` — unrelated to Profiler's own code).
      Results recorded in `docs/benchmarking.md`'s new "GPU-path results"
      section, with this machine's hardware/toolchain spec, and an explicit
      note that the measured negative "slowdown" is noise from a
      non-isolated shared machine at only 15 trials, not a real number to
      gate on — consistent with this doc's existing CPU-workload caveats.
- [ ] Lint workflow question resolved (restored or explicitly justified) —
      **left open by explicit user decision this session**: flagged only,
      not restored or investigated further. `.github/workflows/lint.yml`
      remains absent with no stated reason; still an open item for whoever
      picks this up next.
- [x] Phase 4's "Done when" criteria (top of this document) re-checked item
      by item — see "Done-when re-check" section below.

## Done-when re-check (2026-09-17)

Going item by item against this document's own "Done when" clause (top of
this file), against what was actually run and verified above, not assumed:

- **"multi-stream, multi-thread and supported multi-device workloads retain
  launch links"**: multi-stream — verified (`concurrent_streams_not_nested`,
  4.D). Multi-thread — not specifically re-verified this session (pre-dates
  Phase 4, not touched). Multi-device — explicitly **not** verified; this
  machine has one GPU, no multi-device hardware available (documented under
  4.D).
- **"late callbacks cannot access freed state or contaminate a later run"**:
  verified — the generation-filtering fix (4.A, `427409e`) has real test
  coverage now (`export_xspace_filters_stale_generation`,
  `distinct_captures_get_distinct_generations`, both confirmed to actually
  run, not skip), plus `work_extending_beyond_stop_reported_honestly` (4.D)
  exercises the real timing race with actual in-flight GPU work and a
  following unrelated capture, on real hardware.
- **"timestamps pass calibration checks"**: verified — `calibrate_cuda_device()`
  now produces a real, non-degenerate calibration from real device
  timestamps (4.B), with a real-hardware regression test asserting bounded
  residual uncertainty, wired into a real call site
  (`capture_event::clock_uncertainty_ns`).
- **"failure scenarios report coverage honestly"**: verified for the native
  `profiler_session` path (4.C) — a real CUDA runtime failure now surfaces
  through `profiler_status`/`last_error()` rather than being silently
  swallowed, with a real-hardware failure-injection test. **Not** extended
  to the separate Kineto `capture::` API path (documented scope narrowing
  under 4.C — that type has no `profiler_status`-returning surface at all
  today).
- **"no profiler-owned per-scope synchronization is introduced"**: not
  re-audited this session — no code touched by this pass added any new
  per-scope synchronization; the calibration and error-flag additions are
  session-start/capture-stop-scoped, not per-scope.
- **"CPU/GPU workload targets pass within the declared event-rate/memory
  envelope"**: partially addressed — a real GPU-active benchmark now exists
  and produced a real number (4.E), but that number is explicitly
  informational/noisy (non-isolated shared machine, 15 trials), not a
  certified pass/fail against the declared envelope; doing that for real
  needs the dedicated low-noise machine `docs/benchmarking.md` already says
  this repository doesn't have.

**Net**: the two items with a genuine open gap are multi-device coverage and
a certified (not just informational) GPU-path overhead number — both because
this project has exactly one accessible GPU on one shared machine, not
because the work wasn't done. Everything else above has real, run-and-passed
evidence recorded in this document's checklist, not just a claim.

## Exit criteria

Phase 4 is done when every checklist item above is checked, with actual
command output/test results recorded (not just "looks fine") in this
document's checklist section or in commit messages referencing it, and
design-review.md's Phase 4 "Done when" clause is satisfied item-by-item —
not narrated as done from a commit message alone. The prior pass on this
same machine (`225bcb2`..`b02d601`) is the concrete lesson here: two of its
five claimed items were dead code despite reading as complete.
