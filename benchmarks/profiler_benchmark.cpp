/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Phase 0's reference-workload/budget-gated overhead measurement
// (design-review.md section 1.1), never built until now. See
// docs/benchmarking.md for the full methodology and how to use these
// numbers for real budget gating on a dedicated machine -- this tool
// reports real measurements, but a shared, noisy CI runner is not the
// "reference machine" design-review.md requires to gate pass/fail on the
// 1%/3%/5% targets, so it never does that itself.
//
// A standalone process (not part of the shared ProfilerCxxTests binary) is
// deliberate: TestProfilerScopeOverhead.cpp's own comment explains why a
// global operator-new/delete override is unsafe there (it would affect
// every other test sharing that process) -- neither concern applies to a
// single-purpose CLI that measures exactly what it runs.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <psapi.h>
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#include "profiler.h"

#ifndef PROFILER_BENCHMARK_GIT_COMMIT
#define PROFILER_BENCHMARK_GIT_COMMIT ""
#endif

namespace
{
constexpr double kPi = 3.14159265358979323846;

// A write to a volatile object is an observable side effect the optimizer
// must preserve, so this is a portable (if blunt) way to stop it from
// proving a workload's result is unused and eliminating the whole
// computation -- confirmed necessary here: without it, Release builds
// measured a ~28ns "baseline" for a 2,000,000-iteration Monte Carlo loop
// (the whole loop had no observable effect and was removed), producing a
// nonsensical multi-million-percent "slowdown" once the instrumented
// variant's PROFILER_SCOPE calls happened to block the same elimination.
volatile double g_dont_optimize_sink = 0.0;

// ---------------------------------------------------------------------------
// Allocation counting: a global override is only safe here because this
// process measures nothing else (see file comment). Counts are read as a
// before/after delta bracketing each timed call, so they're correct
// regardless of what else the process does outside that window.
// ---------------------------------------------------------------------------
std::atomic<uint64_t> g_alloc_count{0};

}  // namespace

void* operator new(std::size_t size)
{
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    if (void* ptr = std::malloc(size))
    {
        return ptr;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

namespace
{

uint64_t peak_rss_bytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        return static_cast<uint64_t>(counters.PeakWorkingSetSize);
    }
    return 0;
#else
    struct rusage usage
    {
    };
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return 0;
    }
    // ru_maxrss is kilobytes on Linux, bytes on macOS/BSD.
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#endif
}

// ---------------------------------------------------------------------------
// Reference workloads. Each has a profiler-code-free "core" (the true
// baseline) and an "instrumented" wrapper that adds PROFILER_SCOPE at a
// granularity design-review.md's 3% budget assumes (scope durations
// >=10us) -- shared logic, so the only difference measured between
// baseline/inactive/active configurations is profiler overhead itself, not
// a different workload.
// ---------------------------------------------------------------------------

double matrix_multiply_core(size_t n)
{
    std::vector<double>                    a(n * n);
    std::vector<double>                    b(n * n);
    // Deliberately fixed: identical input data across trials/runs is what
    // makes durations comparable.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937                           rng(12345);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (auto& v : a)
    {
        v = dist(rng);
    }
    for (auto& v : b)
    {
        v = dist(rng);
    }

    std::vector<double> result(n * n, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        for (size_t k = 0; k < n; ++k)
        {
            double const a_ik = a[(i * n) + k];
            for (size_t j = 0; j < n; ++j)
            {
                result[(i * n) + j] += a_ik * b[(k * n) + j];
            }
        }
    }
    return result[0];
}

void matrix_multiply_instrumented(size_t n)
{
    PROFILER_SCOPE("benchmark_matrix_multiply");
    g_dont_optimize_sink = matrix_multiply_core(n);
}

double monte_carlo_core(uint64_t iterations)
{
    // Deliberately fixed: identical input data across trials/runs is what
    // makes durations comparable.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937                           rng(12345);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    uint64_t                               inside = 0;
    for (uint64_t i = 0; i < iterations; ++i)
    {
        double const x = dist(rng);
        double const y = dist(rng);
        if ((x * x) + (y * y) <= 1.0)
        {
            ++inside;
        }
    }
    return 4.0 * static_cast<double>(inside) / static_cast<double>(iterations);
}

void monte_carlo_instrumented(uint64_t iterations)
{
    PROFILER_SCOPE("benchmark_monte_carlo");
    g_dont_optimize_sink = monte_carlo_core(iterations);
}

// Iterative radix-2 Cooley-Tukey FFT. `n` must be a power of two.
void fft_core(std::vector<std::complex<double>>& data)
{
    size_t const n = data.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(data[i], data[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        double const               angle = -2.0 * kPi / static_cast<double>(len);
        std::complex<double> const wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                std::complex<double> const u = data[i + k];
                std::complex<double> const v = data[i + k + (len / 2)] * w;
                data[i + k]                  = u + v;
                data[i + k + (len / 2)]      = u - v;
                w *= wlen;
            }
        }
    }
}

void fft_instrumented(size_t n)
{
    PROFILER_SCOPE("benchmark_fft");
    std::vector<std::complex<double>>      data(n);
    // Deliberately fixed: identical input data across trials/runs is what
    // makes durations comparable.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937                           rng(12345);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (auto& c : data)
    {
        c = {dist(rng), 0.0};
    }
    fft_core(data);
    g_dont_optimize_sink = data[0].real();
}

// ---------------------------------------------------------------------------
// Trial harness
// ---------------------------------------------------------------------------

struct trial_stats
{
    double   mean_ns        = 0.0;
    double   p50_ns         = 0.0;
    double   p95_ns         = 0.0;
    double   p99_ns         = 0.0;
    double   stddev_ns      = 0.0;
    uint64_t alloc_count    = 0;
    uint64_t peak_rss_bytes = 0;
};

double percentile(const std::vector<double>& sorted_values, double p)
{
    if (sorted_values.empty())
    {
        return 0.0;
    }
    double const index = p * static_cast<double>(sorted_values.size() - 1);
    auto const   lower = static_cast<size_t>(std::floor(index));
    auto const   upper = static_cast<size_t>(std::ceil(index));
    if (lower == upper)
    {
        return sorted_values[lower];
    }
    double const weight = index - static_cast<double>(lower);
    return (sorted_values[lower] * (1.0 - weight)) + (sorted_values[upper] * weight);
}

trial_stats run_trials(const std::function<void()>& workload, int trials)
{
    std::vector<double> durations_ns;
    durations_ns.reserve(static_cast<size_t>(trials));
    uint64_t const alloc_before_all = g_alloc_count.load(std::memory_order_relaxed);

    for (int i = 0; i < trials; ++i)
    {
        auto const start = std::chrono::steady_clock::now();
        workload();
        auto const end = std::chrono::steady_clock::now();
        durations_ns.push_back(static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }

    uint64_t const alloc_after_all = g_alloc_count.load(std::memory_order_relaxed);

    trial_stats stats;
    stats.alloc_count    = alloc_after_all - alloc_before_all;
    stats.peak_rss_bytes = peak_rss_bytes();

    double const sum = std::accumulate(durations_ns.begin(), durations_ns.end(), 0.0);
    stats.mean_ns     = sum / static_cast<double>(durations_ns.size());

    double variance_sum = 0.0;
    for (double const d : durations_ns)
    {
        double const diff = d - stats.mean_ns;
        variance_sum += diff * diff;
    }
    stats.stddev_ns = std::sqrt(variance_sum / static_cast<double>(durations_ns.size()));

    std::sort(durations_ns.begin(), durations_ns.end());
    stats.p50_ns = percentile(durations_ns, 0.50);
    stats.p95_ns = percentile(durations_ns, 0.95);
    stats.p99_ns = percentile(durations_ns, 0.99);
    return stats;
}

void print_machine_fingerprint()
{
    std::printf("=== Machine/toolchain fingerprint ===\n");
    std::printf("hardware_concurrency: %u\n", std::thread::hardware_concurrency());
#if defined(_WIN32)
    std::printf("platform: Windows\n");
#elif defined(__APPLE__)
    std::printf("platform: macOS\n");
#elif defined(__linux__)
    std::printf("platform: Linux\n");
#else
    std::printf("platform: unknown\n");
#endif
#if defined(__clang__)
    std::printf(
        "compiler: Clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    std::printf("compiler: MSVC %d\n", _MSC_VER);
#else
    std::printf("compiler: unknown\n");
#endif
    std::string const commit = PROFILER_BENCHMARK_GIT_COMMIT;
    std::printf("git_commit: %s\n", commit.empty() ? "(unknown)" : commit.c_str());
    std::printf("\n");
}

void print_row(
    const char* workload, const char* config, const trial_stats& stats, double slowdown_pct)
{
    std::printf(
        "%-24s %-10s mean=%10.1fns p50=%10.1fns p95=%10.1fns p99=%10.1fns stddev=%9.1fns "
        "allocs=%6llu peak_rss=%8llukB",
        workload, config, stats.mean_ns, stats.p50_ns, stats.p95_ns, stats.p99_ns,
        stats.stddev_ns, static_cast<unsigned long long>(stats.alloc_count),
        static_cast<unsigned long long>(stats.peak_rss_bytes / 1024));
    if (!std::isnan(slowdown_pct))
    {
        std::printf(" slowdown=%6.2f%%", slowdown_pct);
    }
    std::printf("\n");
}

struct workload_spec
{
    const char*            name;
    std::function<void()>  baseline;
    std::function<void()>  instrumented;
};

struct result_row
{
    std::string workload;
    std::string config;
    trial_stats stats;
};

}  // namespace

int main(int argc, char** argv)
{
    int trials = 30;
    if (argc > 1)
    {
        char* parse_end   = nullptr;
        long const parsed = std::strtol(argv[1], &parse_end, 10);
        if (parse_end != argv[1] && parsed > 0)
        {
            trials = std::max(3, static_cast<int>(parsed));
        }
    }

    print_machine_fingerprint();
    std::printf(
        "NOTE: informational only when run on a shared CI runner -- design-review.md's "
        "1%%/3%%/5%% overhead targets (section 1.1) require a dedicated, low-noise reference "
        "machine to gate pass/fail against; see docs/benchmarking.md.\n\n");

    constexpr size_t   kMatrixN     = 100;
    constexpr uint64_t kMonteCarloN = 2'000'000;
    constexpr size_t   kFftN        = 1 << 14;  // 16384, power of two

    std::vector<workload_spec> const workloads = {
        {"matrix_multiply", [] { g_dont_optimize_sink = matrix_multiply_core(kMatrixN); },
            [] { matrix_multiply_instrumented(kMatrixN); }},
        {"monte_carlo", [] { g_dont_optimize_sink = monte_carlo_core(kMonteCarloN); },
            [] { monte_carlo_instrumented(kMonteCarloN); }},
        {"fft",
            [] {
                std::vector<std::complex<double>> data(kFftN, std::complex<double>(1.0, 0.0));
                fft_core(data);
                g_dont_optimize_sink = data[0].real();
            },
            [] { fft_instrumented(kFftN); }},
    };

    std::vector<result_row> results;

    std::printf("=== Results (%d trials per configuration) ===\n", trials);
    for (const auto& workload : workloads)
    {
        trial_stats const baseline_stats = run_trials(workload.baseline, trials);
        print_row(
            workload.name, "baseline", baseline_stats, std::numeric_limits<double>::quiet_NaN());
        results.push_back({workload.name, "baseline", baseline_stats});

        trial_stats const inactive_stats = run_trials(workload.instrumented, trials);
        double const      inactive_slowdown =
            100.0 * (inactive_stats.mean_ns - baseline_stats.mean_ns) / baseline_stats.mean_ns;
        print_row(workload.name, "inactive", inactive_stats, inactive_slowdown);
        results.push_back({workload.name, "inactive", inactive_stats});

        {
            profiler::session_options opts;
            profiler::session         session(opts);
            if (!session.start())
            {
                std::fprintf(stderr, "warning: %s active-capture session failed to start\n",
                    workload.name);
                continue;
            }
            trial_stats const active_stats     = run_trials(workload.instrumented, trials);
            double const      active_slowdown  = 100.0 *
                (active_stats.mean_ns - baseline_stats.mean_ns) / baseline_stats.mean_ns;
            print_row(workload.name, "active", active_stats, active_slowdown);
            results.push_back({workload.name, "active", active_stats});
            (void)session.stop();
        }
        std::printf("\n");
    }

    std::printf("=== CSV ===\n");
    std::printf("workload,config,mean_ns,p50_ns,p95_ns,p99_ns,stddev_ns,allocs,peak_rss_bytes\n");
    for (const auto& row : results)
    {
        std::printf("%s,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%llu,%llu\n", row.workload.c_str(),
            row.config.c_str(), row.stats.mean_ns, row.stats.p50_ns, row.stats.p95_ns,
            row.stats.p99_ns, row.stats.stddev_ns,
            static_cast<unsigned long long>(row.stats.alloc_count),
            static_cast<unsigned long long>(row.stats.peak_rss_bytes));
    }

    return 0;
}
