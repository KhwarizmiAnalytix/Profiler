# Capability matrix

[Documentation index](README.md) · [User guide](profiler.md) · [Design review](design-review.md) · [Benchmarking](benchmarking.md)

Phase 6 (design-review.md [section 7](design-review.md#phase-6--enforce-the-design-continuously)):
"Publish the tested capability matrix... Pin tested compiler/OS/SDK
versions." This document distinguishes three tiers for every
OS×compiler×backend×GPU combination this project's CI actually exercises
(never claims a row it can't point to a job for):

- **Verified on real hardware** — the configuration built, ran, and its
  test assertions checked actual device output, not just that a call
  returned successfully.
- **Toolkit-only (compiles, unexecuted)** — the configuration builds
  against a real SDK/toolkit, but no physical device was present, so
  device-specific tests skip cleanly (`GTEST_SKIP()`) rather than running.
  A green CI job in this tier proves compilation and API-surface
  correctness, not GPU capture correctness.
- **Not currently tested** — no CI job exercises this combination at all.

This is a snapshot of `.github/workflows/ci.yml`'s actual matrix as of the
commit that last updated this document — re-derive it from the workflow
file if the two drift, and treat this document as stale in the meantime
rather than authoritative.

## OS × backend (CPU-only, `gpu=none`)

| OS | KINETO | ITT |
| --- | --- | --- |
| Ubuntu | Verified (`build-test` job) | Verified (`build-test` job) |
| macOS | Verified (`build-test` job) | Verified (`build-test` job) |
| Windows | Verified (`build-test` job) | Verified (`build-test` job) |

All six cells also run the native-only symbol-leak check, the header-surface
curation check (`static-link` job, KINETO and ITT both), and Phase 6.A's
architecture checks (backend-independent, runs once per push).

**Cross-backend conformance** (Phase 6.C): the `cross-backend-conformance`
job runs `Testing/Cxx/TestCrossBackendGoldenScenario.cpp`'s identical,
single-sourced golden workload (nested scopes, recursion, same name from
different call sites, a Unicode/JSON-special-character scope name) through
a fresh KINETO build and a fresh ITT build, then diffs the two normalized
event-identity/count summaries for exact agreement
(`Scripts/diff_cross_backend_golden.py`). Deliberately not attempted, for
structural reasons documented in that test file's own header comment: a
positional/ordering comparison (KinetoEvents complete in LIFO-unwind order;
the ITT stub records push order -- two different, equally valid orderings
of the same workload, not a real divergence), structured key/value
metadata comparison (ITT's wrapper has no metadata plumbing at all, unlike
Kineto's `extraMeta()`), and "overlapping async spans" (ITT's wrapper only
exposes a LIFO push/pop stack API with no explicit task IDs, so it cannot
represent a genuinely non-nested span at all — there is no ITT-side
observation point for this scenario, not just a missing test).
`TestHotspotReport.cpp` and `TestProfilerBackendMetadata.cpp`'s metadata
round-trip test remain KINETO-only for the same structural reason
(`bespoke/kineto/hotspot_report.h` and `extraMeta()` are both real,
Kineto-specific capabilities with no ITT equivalent to port to), not an
unaddressed gap.

## GPU backends

| OS | Backend | GPU | Tier | CI job |
| --- | --- | --- | --- | --- |
| `windows-2022` | KINETO | CUDA 12.8.1 | Toolkit-only (compiles, unexecuted) | `build-test` (`windows-2022 KINETO cuda` leg) |
| `windows-2022` | ITT | CUDA 12.8.1 | Toolkit-only (compiles, unexecuted) | `build-test` (`windows-2022 ITT cuda` leg) |
| `ubuntu-22.04` | KINETO | HIP/ROCm (latest) | Toolkit-only (compiles, unexecuted; no HIP-specific real-hardware test exists yet) | `hip-toolkit-only` |

GitHub-hosted runners install the CUDA *toolkit* but expose no physical
GPU (confirmed directly:
`docs/phase-5-remaining.md`'s own investigation into this). Every
`TestProfilerGpuRealHardware.cpp` test is gated on `gpu_device_available`
and calls `GTEST_SKIP()` when absent — so these two legs currently prove
"builds and links against the CUDA 12.8.1 toolkit under the pinned
`windows-2022`/VS2022 MSVC toolset," not "captures real kernels." Real
CUDA-hardware verification for this project instead comes from manual runs
on the Windows/NVIDIA development machine (`docs/plans/phase-4-gpu-validation.md`,
`docs/plans/phase-5-api-simplification.md`'s Windows follow-up
session) — not from ordinary CI, and not on every push.

HIP now has a toolkit-only CI leg (`hip-toolkit-only`, Phase 6.D): it
installs a real ROCm apt package (`hip-dev`) on `ubuntu-22.04` (pinned to
match ROCm's per-codename apt repository, not `ubuntu-latest`) and
configures with `-DPROFILER_REQUIRE_HIP=ON`, so the job fails outright
(rather than silently building a `PROFILER_HAS_HIP=0` config) if the
toolkit install ever stops actually being picked up. No physical AMD GPU
exists on this runner, and no HIP-specific real-hardware test exists yet
either (design-review.md's Phase 4 real-hardware scope was CUDA-only) — so
this proves "configures, links against a real ROCm toolkit, and compiles,"
the same honestly-limited claim the CUDA toolkit-only legs make, nothing
about HIP capture correctness on a device.

CI now also distinguishes "ran on real hardware" from "skipped everywhere"
for the CUDA toolkit-only legs specifically (Phase 6.D):
`Scripts/report_gpu_test_status.py` parses each `build-test` leg's
GoogleTest JUnit XML (written via `GTEST_OUTPUT`) for the `GpuRealHardware`
suite's skip/pass counts and publishes them as a job summary, so "0/N ran
on real hardware (toolkit-only)" is visible without reading the raw test
log.

## Sanitizers

| Sanitizer | OS | Backend | Leak detection | Gate |
| --- | --- | --- | --- | --- |
| ASan + UBSan | Ubuntu | KINETO only | **On** (`detect_leaks=1`) | Zero-tolerance (`halt_on_error=1`; any finding fails the job) |
| ASan + UBSan | macOS | KINETO only | **Off** (`detect_leaks=0`; LSan's stop-the-world scan can hang during process teardown on Darwin) | Zero-tolerance (`halt_on_error=1`; any finding fails the job) |
| TSan | Ubuntu only | KINETO only | N/A | Zero-tolerance (`halt_on_error=1`) |

Not covered by any sanitizer job: ITT backend, Windows (sanitizers are a
GCC/Clang-only wrapper in `cmake/ProfilerSanitizers.cmake` — an explicit
no-op on MSVC, by design, since MSVC's own sanitizer story is a materially
different toolchain integration this project hasn't taken on), and any
GPU-enabled build. Leak detection is Linux-only (macOS LSan's stop-the-world
scan is unreliable there) — a real, documented gap, not an oversight.

One known, precisely-located but unfixed finding from Phase 6.F's soak
testing: a rare race in `Profiler/common/lock_free_queue.h`'s
`blocked_queue_base::pop_impl()`, reproducing in a minority of TSan runs on
WSL2/Ubuntu (not this project's own CI environment). Not reproduced on
macOS ASan across repeated attempts at both original and larger scale (see
`docs/plans/phase-6-continuous-enforcement.md`'s Phase A amendment) —
left open for a session with Linux/TSan access, not guessed at blind.

## Compiler/OS/SDK version pinning

| Dependency | Pin | Where | Why |
| --- | --- | --- | --- |
| CUDA Toolkit | `12.8.1` | `.github/workflows/ci.yml` (`cuda-toolkit` action input) | Matches the version validated against; newer toolkits have shown host-compiler compatibility breaks (see next row). |
| Windows image (CUDA legs only) | `windows-2022` (not `windows-latest`) | `.github/workflows/ci.yml` matrix `include` | CUDA 12.8.1's `cudafe++` preprocessor segfaults parsing headers under the newer MSVC toolset `windows-latest` currently resolves to — a real parser crash, not just a version-check rejection `-allow-unsupported-compiler` could paper over. `windows-2022` still ships the VS2022 toolset CUDA 12.8 was validated against. |
| Ubuntu, macOS, non-CUDA Windows | Unpinned (`ubuntu-latest`/`macos-latest`/`windows-latest`) | `.github/workflows/ci.yml` | Deliberately floats with whatever GitHub currently maps these to — no known compatibility break has forced a pin here yet. If one appears, pin it and add a row to this table with the same reasoning the CUDA/Windows rows already have. |

**Process commitment** (design-review.md: "rerun affected hardware
conformance on upgrades"): any change to the CUDA/ROCm toolkit version, the
pinned Windows image, or a compiler minimum bumps this document's
"Compiler/OS/SDK version pinning" table *and* requires a fresh real-hardware
conformance pass on the Windows/NVIDIA development machine (the toolkit-only
CI legs above cannot substitute for this — see the GPU backends section)
before merging. Record the result the same way
`docs/plans/phase-4-gpu-validation.md` and the Phase 5 Windows follow-up
session already did: what was run, on what hardware, with what output. This
is also a checked item in [CONTRIBUTING.md](../CONTRIBUTING.md)'s pull
request checklist, not just documented here.

## What this matrix does not cover

- **Scheduled hardware soak/stress testing**: no persistent, always-on
  runner is registered for this project (`docs/plans/phase-6-continuous-enforcement.md`'s
  6.F) — nothing to put in a matrix yet.
- **Cross-backend conformance** (KINETO vs. ITT agreeing on identical
  synthetic inputs): tracked separately in
  `docs/plans/phase-6-continuous-enforcement.md`'s 6.C, not a per-cell
  property this table can express.
- **Benchmark/overhead numbers**: see `docs/benchmarking.md` — a different
  kind of "tested" than pass/fail capability, published separately.
