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

// Phase 6.E (design-review.md section 7, Phase 6 / section 8's
// "Capacity/failure" row): deterministic failure injection beyond what
// TestProfilerLifecycleRegressions.cpp and TestProfilerGpuRealHardware.cpp
// already cover -- a throwing collector, and bounded metadata/sample-series
// overflow. Runs CPU-only, in the normal ProfilerCxxTests binary, on every
// push/PR -- no real hardware needed.
//
// Deliberately NOT attempted here: process-wide profiler-side OOM injection
// via a global operator new/delete override. TestProfilerScopeOverhead.cpp's
// own comment already explains why that's unsafe in this shared test
// binary (it would affect every other test sharing the process) --
// profiler_benchmark.cpp is a separate, single-purpose process specifically
// because of this constraint, and this file doesn't have that luxury.

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ProfilerTest.h"
#include "native/analysis/statistical_analyzer.h"
#include "native/core/profiler_collection.h"
#include "native/core/profiler_interface.h"
#include "native/exporters/xplane/xplane.h"

using namespace profiler;

namespace
{

// A test-only profiler_interface that throws from whichever stage its
// constructor is told to fail at, simulating a misbehaving collector (a
// vendor callback or a bug in a first-party tracer) rather than an ordinary
// documented failure (which real collectors report via profiler_status,
// already covered elsewhere).
class throwing_collector : public profiler_interface
{
public:
    enum class fail_stage
    {
        start,
        stop,
        collect_data
    };

    explicit throwing_collector(fail_stage stage) : stage_(stage) {}

    profiler_status start() override
    {
        if (stage_ == fail_stage::start)
        {
            throw std::runtime_error("injected failure in start()");
        }
        return profiler_status::Ok();
    }

    profiler_status stop() override
    {
        if (stage_ == fail_stage::stop)
        {
            throw std::runtime_error("injected failure in stop()");
        }
        return profiler_status::Ok();
    }

    profiler_status collect_data(x_space* /*space*/) override
    {
        if (stage_ == fail_stage::collect_data)
        {
            throw std::runtime_error("injected failure in collect_data()");
        }
        return profiler_status::Ok();
    }

private:
    fail_stage stage_;
};

std::vector<std::unique_ptr<profiler_interface>> make_collectors(
    throwing_collector::fail_stage stage)
{
    std::vector<std::unique_ptr<profiler_interface>> v;
    v.push_back(std::make_unique<throwing_collector>(stage));
    return v;
}

}  // namespace

// design-review.md section 6.7: "Keep normal scope recording and callbacks
// nonthrowing with contained failure handling." Reproduced directly before
// the native/core/profiler_collection.cpp fix this test guards: an
// std::runtime_error thrown from a collector's start() propagated straight
// out of profiler_collection::start() uncaught -- which would std::terminate()
// any application calling the public session API without its own
// belt-and-suspenders try/catch around every start()/stop()/collect_data()
// call. profiler_collection now catches and converts to profiler_status::Error(),
// the same contract every other backend failure already uses.
PROFILERTEST(FailureInjection, throwing_collector_start_is_contained_as_an_error_status)
{
    profiler_collection collection(make_collectors(throwing_collector::fail_stage::start));

    profiler_status status = profiler_status::Ok();
    EXPECT_NO_THROW({ status = collection.start(); });
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("start()"), std::string::npos);
}

PROFILERTEST(FailureInjection, throwing_collector_stop_is_contained_as_an_error_status)
{
    profiler_collection collection(make_collectors(throwing_collector::fail_stage::stop));

    profiler_status status = profiler_status::Ok();
    EXPECT_NO_THROW({ status = collection.stop(); });
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("stop()"), std::string::npos);
}

PROFILERTEST(FailureInjection, throwing_collector_collect_data_is_contained_as_an_error_status)
{
    profiler_collection collection(make_collectors(throwing_collector::fail_stage::collect_data));

    x_space         space;
    profiler_status status = profiler_status::Ok();
    EXPECT_NO_THROW({ status = collection.collect_data(&space); });
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("collect_data()"), std::string::npos);
}

// One collector throwing must not prevent a well-behaved sibling collector
// from being called -- profiler_collection multiplexes calls to every
// registered backend, and a single misbehaving one shouldn't silently
// starve the rest.
PROFILERTEST(FailureInjection, throwing_collector_does_not_block_a_well_behaved_sibling)
{
    class counting_collector : public profiler_interface
    {
    public:
        profiler_status start() override
        {
            ++start_calls;
            return profiler_status::Ok();
        }
        profiler_status stop() override { return profiler_status::Ok(); }
        profiler_status collect_data(x_space* /*space*/) override { return profiler_status::Ok(); }
        int             start_calls = 0;
    };

    std::vector<std::unique_ptr<profiler_interface>> collectors;
    collectors.push_back(
        std::make_unique<throwing_collector>(throwing_collector::fail_stage::start));
    auto* counting_raw = new counting_collector();
    collectors.emplace_back(counting_raw);

    profiler_collection collection(std::move(collectors));
    profiler_status     status = profiler_status::Ok();
    EXPECT_NO_THROW({ status = collection.start(); });
    EXPECT_FALSE(status.ok());                // The throwing collector's failure is still reported.
    EXPECT_EQ(counting_raw->start_calls, 1);  // But the sibling still ran.
}

// design-review.md section 8's "Capacity/failure" row: "Event/string/
// metadata/correlation exhaustion" -- statistical_analyzer's
// max_samples_per_series_ is the one first-party, directly-testable bound
// of this kind (trim_series_if_needed() drops the *oldest* samples once the
// bound is hit). No prior test exercised this at all -- confirmed via grep
// before writing this.
PROFILERTEST(FailureInjection, sample_series_overflow_is_bounded_and_drops_oldest_first)
{
    statistical_analyzer analyzer;
    analyzer.set_max_samples_per_series(5);
    analyzer.start_analysis();

    // Push twice the configured bound; values are their own insertion order
    // (0..9) so which ones survived is directly observable.
    for (double v = 0.0; v < 10.0; v += 1.0)
    {
        analyzer.add_custom_sample("bounded_series", v);
    }

    EXPECT_EQ(analyzer.get_sample_count("bounded_series"), 5u);

    auto const metrics = analyzer.calculate_custom_stats("bounded_series");
    ASSERT_TRUE(metrics.is_valid());
    EXPECT_EQ(metrics.count, 5u);
    // Oldest (0..4) must have been dropped, leaving 5..9 -- min/max pin down
    // which five actually survived, not just the count.
    EXPECT_NEAR(metrics.min_value, 5.0, 1e-9);
    EXPECT_NEAR(metrics.max_value, 9.0, 1e-9);
}
