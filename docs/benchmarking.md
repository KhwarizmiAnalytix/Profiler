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

`trials` (default 30) is the number of repeated runs per workload per
configuration. Always build `Release`: overhead measurements from a Debug
build are meaningless.

## What it measures

Three reference workloads, chosen because they already existed as
representative, `PROFILER_SCOPE`-wrapped CPU-bound shapes elsewhere in this
repo (`examples/example_profiling_basic.cpp`'s matrix multiply,
`Testing/Cxx/TestProfilerHeavyFunction.cpp`'s Monte Carlo and FFT variants),
reimplemented minimally here rather than sharing code with those files:

- `matrix_multiply` -- 100x100 dense multiply.
- `monte_carlo` -- 2,000,000-iteration pi estimation.
- `fft` -- a 16,384-point radix-2 Cooley-Tukey FFT.

Each runs in three configurations, back to back, using the exact same
computational core so the only difference between them is profiler
overhead itself:

1. **baseline** -- no profiler code compiled in at all. The true reference
   point for "slowdown %".
2. **inactive** -- `PROFILER_SCOPE` present, but no session is active.
   Should track design-review.md's <=1% target.
3. **active** -- a default-preset `profiler::session` is running. Should
   track the 3% (CPU-only) target for workloads with scope durations
   >=10us, which all three of these comfortably exceed.

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
