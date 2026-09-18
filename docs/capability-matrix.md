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

## GPU backends

| OS | Backend | GPU | Tier | CI job |
| --- | --- | --- | --- | --- |
| `windows-2022` | KINETO | CUDA 12.8.1 | Toolkit-only (compiles, unexecuted) | `build-test` (`windows-2022 KINETO cuda` leg) |
| `windows-2022` | ITT | CUDA 12.8.1 | Toolkit-only (compiles, unexecuted) | `build-test` (`windows-2022 ITT cuda` leg) |
| *(any)* | *(any)* | HIP/ROCm | **Not currently tested** | none exist |

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

HIP has no CI leg of any tier today (`design-review.md`'s Phase 6 ask —
"toolkit-only builds test compilation, not GPU capture" — isn't even met at
the toolkit-only tier yet for HIP). `docs/profiler.md` and
`docs/phase-5-remaining.md` item 3 already document this gap; tracked as an
open item in `docs/plans/phase-6-continuous-enforcement.md`'s 6.D.

## Sanitizers

| Sanitizer | OS | Backend | Leak detection | Gate |
| --- | --- | --- | --- | --- |
| ASan + UBSan | Ubuntu, macOS | KINETO only | **Off** (`detect_leaks=0`) | Zero-tolerance (`halt_on_error=1`; any finding fails the job) |
| TSan | Ubuntu only | KINETO only | N/A | Zero-tolerance (`halt_on_error=1`) |

Not covered by any sanitizer job: ITT backend, Windows (sanitizers are a
GCC/Clang-only wrapper in `cmake/ProfilerSanitizers.cmake` — an explicit
no-op on MSVC, by design, since MSVC's own sanitizer story is a materially
different toolchain integration this project hasn't taken on), and any
GPU-enabled build. Leak detection is explicitly disabled on both platforms
that do run (macOS LSan is unreliable there; Linux is kept identical to the
same recipe rather than diverging) — a real, documented gap, not an
oversight.

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
