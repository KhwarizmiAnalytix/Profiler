# Benchmarking Profiler's overhead

[Documentation index](README.md) · [User guide](profiler.md) · [Design review](design-review.md)

`profiler_benchmark` is Phase 0's reference-workload/budget-gated overhead
measurement tool from [design-review.md section 1.1](design-review.md#11-efficiency-and-reliability-requirements),
built to give that phase's overhead targets real, repeatable numbers instead
of only the qualitative allocation/lock regression tests in
[`TestProfilerScopeOverhead.cpp`](../Testing/Cxx/TestProfilerScopeOverhead.cpp).

## Build and run

Not built by default -- it's a measurement tool, not something every
consumer needs:

```bash
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release -DPROFILER_ENABLE_BENCHMARKS=ON
cmake --build build-benchmark --parallel
./build-benchmark/bin/profiler_benchmark [trials]
```

To include the GPU-active workload, configure with `-DPROFILER_GPU_BACKEND=cuda`
in addition (falls back to CPU-only workloads at runtime if no device is
detected even when built in). If building with Clang against this repo's
vendored `third_party/benchmark` on a newer Clang than that library has been
tested against, you may also need
`-DBENCHMARK_ENABLE_WERROR=OFF -DCMAKE_CXX_FLAGS=-Wno-c2y-extensions` -- see
"GPU-path results" below for why.

`trials` (default 30) is the number of repeated runs per workload per
configuration. Always build `Release`: overhead measurements from a Debug
build are meaningless.

## What it measures

Three CPU-only reference workloads, chosen because they already existed as
representative, `PROFILER_SCOPE`-wrapped CPU-bound shapes elsewhere in this
repo (`examples/example_profiling_basic.cpp`'s matrix multiply,
`Testing/Cxx/TestProfilerHeavyFunction.cpp`'s Monte Carlo and FFT variants),
reimplemented minimally here rather than sharing code with those files:

- `matrix_multiply` -- 100x100 dense multiply.
- `monte_carlo` -- 2,000,000-iteration pi estimation.
- `fft` -- a 16,384-point radix-2 Cooley-Tukey FFT.

A fourth, GPU-active workload (Phase 4.E) runs only when built with
`PROFILER_HAS_CUDA` *and* a real device is detected at runtime
(`cudaGetDeviceCount`) -- design-review.md section 1.1's 5% overhead target
is specifically "for the default CPU/GPU timeline on the designated
compute/transfer workloads", and this is the only place in this repository
where that number can be produced against real hardware rather than a
compile-only CUDA toolkit:

- `gpu_kernel_and_transfer` -- H2D transfer of two 65,536-element float
  arrays, an elementwise add kernel, D2H transfer of the result, fully
  synchronized before the timed call returns. Kernel lives in its own `.cu`
  translation unit (`benchmarks/profiler_benchmark_gpu_kernel.cu`) since
  `profiler_benchmark.cpp` overrides global `operator new`/`delete` with
  exception-throwing host code, which `nvcc` rejects as device code if the
  whole file is compiled as CUDA.

Each runs in three configurations, back to back, using the exact same
computational core so the only difference between them is profiler
overhead itself:

1. **baseline** -- no profiler code compiled in at all. The true reference
   point for "slowdown %".
2. **inactive** -- `PROFILER_SCOPE` present, but no session is active.
   Should track design-review.md's <=1% target.
3. **active** -- a default-preset `profiler::session` is running (with
   `gpu_tracing = true` when built with CUDA, so the GPU workload actually
   exercises device-plane tracing and not just `PROFILER_SCOPE` overhead).
   Should track the 3% (CPU-only) / 5% (GPU) target for workloads with scope
   durations >=10us, which all four of these comfortably exceed.

For each configuration it reports mean/p50/p95/p99/stddev wall-clock
duration, the number of heap allocations during the timed call (a global
`operator new`/`delete` override, safe here because this is a
single-purpose process measuring nothing else -- see
`TestProfilerScopeOverhead.cpp`'s own comment on why that override is
*unsafe* in the shared test binary), and peak process RSS. Output is both a
human-readable table and a CSV block on stdout, plus a machine/toolchain
fingerprint (core count, OS, compiler, git commit) printed once at the top --
design-review.md's "store machine/toolchain/SDK configuration with
benchmark data."

## Why CI doesn't gate on these numbers

GitHub-hosted runners (`ubuntu-latest`/`macos-latest`/`windows-latest`) are
shared and noisy -- exactly what design-review.md's "fix reference
machines" language (section 1.1) says *not* to measure against. The
`benchmark` CI job runs `profiler_benchmark` and uploads its output as an
artifact on every push, so regressions are at least visible over time, but
it never fails the build on the reported numbers (only on a crash or
nonzero exit). Treat its output as a trend line, not a gate.

## Doing real budget gating

To actually check a change against design-review.md's 1%/3%/5% targets,
run `profiler_benchmark` on a dedicated, otherwise-idle machine (no other
CPU-bound processes, consistent power/thermal state, ideally the same
machine run to run) before and after the change, with a higher trial count
(100+) to shrink confidence intervals, and compare the `inactive`/`active`
slowdown percentages against baseline directly. This is the same category
of gap as [Phase 4](design-review.md#7-implementation-plan-and-completion-criteria)'s
real-GPU-hardware requirement: the measurement tooling exists, but running
it as an authoritative gate needs infrastructure (a controlled machine, or
eventually a dedicated self-hosted CI runner) beyond what this repository's
shared CI can provide today.

## GPU-path results (Phase 4.E)

Recorded 2026-09-17 on the Windows/NVIDIA machine used for
[Phase 4](plans/phase-4-gpu-validation.md) -- the only machine in this
project with a GPU, so the only source these numbers can come from at all.
**Informational, not a gate**: same shared-toolchain caveats as above (this
machine also ran other processes during the measurement), and additionally
built with Clang as host compiler rather than this project's normal MSVC
CUDA host compiler (see the Phase 4 plan's "Toolchain findings" section for
why) -- treat as a rough order of magnitude, not a certified number.

**Hardware/toolchain**: NVIDIA GeForce RTX 4060 Ti (8GB), driver 591.86
(CUDA 13.1 max per `nvidia-smi`), CUDA Toolkit 13.0 (`nvcc` V13.0.88), 32
logical CPUs, Windows 11, Clang 22.1.2 host compiler, Ninja generator,
`CMAKE_CUDA_ARCHITECTURES=75` (this run used the default; a native `89`
build for this GPU wasn't re-benchmarked separately).

15 trials, `PROFILER_ENABLE_BENCHMARKS=ON -DBENCHMARK_ENABLE_WERROR=OFF
-DCMAKE_CXX_FLAGS=-Wno-c2y-extensions` (the latter two work around the
vendored `third_party/benchmark` library's `-pedantic-errors` rejecting a
`__COUNTER__` construct under this newer Clang -- unrelated to Profiler's
own code, see this doc's build command for context):

| workload | config | mean | slowdown |
| --- | --- | --- | --- |
| gpu_kernel_and_transfer | baseline | 417.7us | -- |
| gpu_kernel_and_transfer | inactive | 326.6us | -21.8% |
| gpu_kernel_and_transfer | active | 380.2us | -8.97% |

Both slowdown figures are negative (the instrumented/active runs measured
*faster* than "baseline"), which is noise, not a real speedup: this
workload's absolute duration (hundreds of microseconds, dominated by two
host↔device transfers and a device sync) is small enough that run-to-run
variance from other processes on this shared, non-isolated machine (stddev
119us on the baseline row alone -- comparable to the effect size itself)
swamps the actual profiler overhead at only 15 trials. A real budget-gating
number for this workload needs the dedicated, low-noise, higher-trial-count
setup this doc's "Doing real budget gating" section already describes for
the CPU workloads -- not attempted here since no such dedicated GPU machine
is available to this project (same conclusion as the CPU workloads' own
numbers, just more visibly so given this workload's smaller effect size
relative to ambient noise). Full CPU-workload numbers from this run are not
reproduced here since they were incidental to exercising the GPU path, not
a dedicated CPU benchmarking session.

## Known limitations

- The tool re-derives its own percentile/statistics math rather than
  reusing `profiler::statistical_analyzer`, to keep it fully independent of
  the library behavior it measures.
- The human-readable table and CSV sections are two independent code paths
  over the same collected results (not re-run), not a single structured
  output format (e.g. JSON) a dashboard could consume directly -- a
  reasonable follow-up once this tool has a real consumer for that.
- Workload sizes are fixed constants, not configurable via CLI flags beyond
  the trial count.
