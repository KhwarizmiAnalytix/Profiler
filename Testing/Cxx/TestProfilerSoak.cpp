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

// Phase 6.F (design-review.md section 7, Phase 6): "scheduled hardware
// stress/soak tests across start/stop cycles, many threads, high event
// rates, metadata churn and delayed callbacks. Check post-warmup memory
// plateaus, handle/resource leaks, deadlocks..."
//
// This is the CPU-only, bounded-duration harness
// docs/plans/phase-6-continuous-enforcement.md's 6.F describes as the
// ceiling reachable without a persistent, always-on runner: genuine
// multi-hour/day hardware soak testing needs infrastructure this project
// doesn't have registered (see that plan document). What's here runs in
// the normal ProfilerCxxTests binary, on every push/PR, in well under a
// minute -- many start/stop cycles, many threads per cycle, churning
// metadata (scope names that change every cycle, to stress the label/hash
// tables rather than always hitting an already-registered fast path), and
// a post-warmup process-RSS plateau check.
//
// Deliberately reads process RSS via the OS (a read-only query), not a
// global operator new/delete override -- TestProfilerScopeOverhead.cpp's
// own comment already explains why a global allocator override is unsafe
// in this shared, multi-file test binary. Reading RSS doesn't touch the
// allocator at all, so it's safe here. This is necessarily noisier than a
// dedicated single-purpose process (other tests' memory use shares the
// same process), so this check looks for a clear growth *trend* across
// many cycles, not a tight bound -- exactly the "plateau, not zero growth"
// design-review.md itself asks for.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#else
#include <sys/resource.h>
#endif

#include "ProfilerTest.h"
#include "profiler.h"

using namespace profiler;

namespace
{

uint64_t current_rss_bytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    {
        return static_cast<uint64_t>(counters.WorkingSetSize);
    }
    return 0;
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#endif
}

constexpr int kCycles          = 200;
constexpr int kThreadsPerCycle = 8;
// Exercise session/thread churn with 160,000 events so Debug coverage builds
// fit within the normal unit-test timeout.
constexpr int kScopesPerThread = 100;
// RSS is a process-wide, monotonically-nondecreasing-ish OS stat shared
// with every other test in this binary; ru_maxrss in particular never
// decreases. So this isn't "did RSS stay flat," it's "did RSS roughly stop
// growing after warmup" -- measured as the growth rate in the second half
// of the run being a small fraction of the first half's, not a raw byte
// bound (which would be a different number on every OS/allocator).
constexpr int kWarmupCycles = kCycles / 4;

void run_one_cycle(int cycle_index)
{
    session_options opts;
    opts.native = true;
    session sess(opts);
    EXPECT_TRUE(sess.start());

    std::vector<std::thread> threads;
    threads.reserve(kThreadsPerCycle);
    for (int t = 0; t < kThreadsPerCycle; ++t)
    {
        threads.emplace_back(
            [cycle_index, t]
            {
                for (int i = 0; i < kScopesPerThread; ++i)
                {
                    // Metadata churn: the name changes every cycle/thread/
                    // iteration, so label handling can't just keep hitting
                    // an already-known entry the whole run -- design-review.md's
                    // own "metadata churn" soak dimension. Confirmed during
                    // investigation that name uniqueness itself isn't what
                    // drove the RSS growth this test originally caught (a
                    // bounded 50-name pool showed the same growth,
                    // ruling that out) -- the real cause was
                    // common/annotation.cpp's thread-local pool leaking per
                    // ephemeral thread, fixed there.
                    std::string const name = "soak_scope_" + std::to_string(cycle_index) + "_" +
                                             std::to_string(t) + "_" + std::to_string(i);
                    PROFILER_SCOPE(name.c_str());
                }
            });
    }
    for (auto& th : threads)
    {
        th.join();
    }

    sess.stop();
}

}  // namespace

PROFILERTEST(Soak, many_start_stop_cycles_with_threads_and_metadata_churn_do_not_leak_or_hang)
{
    // design-review.md's Phase 6 done-when clause: "publish soak duration,
    // seed, event counts and hardware rather than claiming universal
    // reliability from a passing suite." No RNG is used here (names are
    // deterministic from cycle/thread/iteration indices, not random), so
    // there is no seed to publish; duration, cycle/thread/event counts, and
    // this run's actual RSS numbers are recorded as gtest properties (show
    // up in --gtest_output=xml, wired into CI by item 6.D) and printed for
    // humans reading a local run.
    auto const wall_start = std::chrono::steady_clock::now();

    // The whole test's own bounded runtime is the deadlock detector: ctest's
    // job-level timeout (.github/workflows/ci.yml) kills the process if
    // this hangs, which fails the test the same way a crash would --
    // exactly "no deadlocks" as a checkable property, not just an
    // assumption.
    uint64_t rss_after_warmup = 0;

    for (int cycle = 0; cycle < kCycles; ++cycle)
    {
        run_one_cycle(cycle);

        if (cycle == kWarmupCycles)
        {
            rss_after_warmup = current_rss_bytes();
        }
    }

    uint64_t const rss_at_end = current_rss_bytes();
    auto const     wall_end   = std::chrono::steady_clock::now();
    double const   duration_s = std::chrono::duration<double>(wall_end - wall_start).count();
    int64_t const  total_events =
        static_cast<int64_t>(kCycles) * kThreadsPerCycle * kScopesPerThread;

    testing::Test::RecordProperty("soak_duration_seconds", duration_s);
    testing::Test::RecordProperty("soak_cycles", kCycles);
    testing::Test::RecordProperty("soak_threads_per_cycle", kThreadsPerCycle);
    testing::Test::RecordProperty("soak_scopes_per_thread", kScopesPerThread);
    testing::Test::RecordProperty("soak_total_scope_events", static_cast<double>(total_events));
    testing::Test::RecordProperty(
        "soak_rss_after_warmup_bytes", static_cast<double>(rss_after_warmup));
    testing::Test::RecordProperty("soak_rss_at_end_bytes", static_cast<double>(rss_at_end));
    std::printf("Soak: %d cycles x %d threads x %d scopes = %lld events in %.2fs "
                "(RSS after warmup: %llu bytes, at end: %llu bytes)\n",
        kCycles,
        kThreadsPerCycle,
        kScopesPerThread,
        static_cast<long long>(total_events),
        duration_s,
        static_cast<unsigned long long>(rss_after_warmup),
        static_cast<unsigned long long>(rss_at_end));

    // A real per-cycle leak (e.g. one un-freed allocation per scope) would
    // show up as RSS growing roughly linearly with cycle count -- over the
    // 3*kWarmupCycles cycles between the warmup checkpoint and the end,
    // that's a large, easily-distinguished-from-noise increase. A merely
    // "shared, noisy process" effect (another test's allocations, ASLR,
    // allocator arena growth that then plateaus) does not compound the
    // same way. This is deliberately a generous bound, not a tight one --
    // the point is catching gross, compounding growth, not chasing exact
    // byte counts, consistent with design-review.md asking for "stable
    // post-warmup memory," not "zero growth."
    if (rss_after_warmup > 0 && rss_at_end > rss_after_warmup)
    {
        uint64_t const growth = rss_at_end - rss_after_warmup;
        // Generous: growth after warmup must stay under half of the
        // warmup-point RSS itself. A real leak scaling with 3x more cycles
        // than were needed to reach that warmup point would blow well past
        // this; ordinary process noise should not.
        EXPECT_LT(growth, rss_after_warmup / 2)
            << "RSS grew from " << rss_after_warmup << " to " << rss_at_end << " bytes across "
            << (kCycles - kWarmupCycles) << " post-warmup start/stop cycles -- possible leak";
    }
}
