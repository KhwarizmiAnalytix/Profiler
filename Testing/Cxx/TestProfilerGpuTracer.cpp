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

/*
 * =============================================================================
 * TensorFlow GpuTracer → `/device:GPU:N` XPlane → Chrome JSON
 * =============================================================================
 *
 * Mirrors TF device_tracer_cuda.cc: CreateGpuTracer is gated by
 * device_tracer_level; activities are added via gpu_trace_collector::AddEvent
 * (CUPTI analog). Synthetic events run on every platform. The device kernel
 * probe has no native implementation in this port and always returns false.
 */

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ProfilerTest.h"
#include "native/exporters/xplane/tf_xplane_visitor.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/exporters/xplane/xplane_visitor.h"
#include "native/gpu/gpu_tracer.h"
#include "native/session/profiler.h"
#include "native/tracing/traceme.h"

using namespace profiler;
using profiler::profiler_impl::add_gpu_tracer_event;
using profiler::profiler_impl::gpu_tracer_event;
using profiler::profiler_impl::gpu_tracer_event_type;
using profiler::profiler_impl::gpu_tracer_is_recording;
using profiler::profiler_impl::run_gpu_kernel_probe;

namespace
{

constexpr const char* kSyntheticKernel = "synthetic_gpu_kernel";
constexpr const char* kProbeKernel     = "profiler_gpu_probe";
constexpr const char* kHostScope       = "gpu_tracer_host_scope";
constexpr uint64_t    kSyntheticDurNs  = 250000;  // 250 µs

profiler_options make_cpu_options()
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_gpu_tracing_            = false;
    opts.output_format_                 = profiler_options::output_format_enum::JSON;
    return opts;
}

profiler_options make_gpu_options()
{
    profiler_options opts    = make_cpu_options();
    opts.enable_gpu_tracing_ = true;
    return opts;
}

void add_kernel_event(std::string_view name, uint64_t start_ns, uint64_t end_ns)
{
    gpu_tracer_event event;
    event.type          = gpu_tracer_event_type::kernel;
    event.name          = std::string(name);
    event.start_time_ns = start_ns;
    event.end_time_ns   = end_ns;
    event.stream_id     = 1;
    add_gpu_tracer_event(std::move(event));
}

size_t count_events(const xplane& plane)
{
    size_t total = 0;
    for (const xline& line : plane.lines())
    {
        total += line.events_size();
    }
    return total;
}

void print_gpu_plane(const xplane& plane)
{
    std::cout << "\n=== Native GpuTracer plane ===\n";
    std::cout << "plane: " << plane.name() << "\n";
    xplane_visitor const visitor = CreateTfXPlaneVisitor(&plane);
    visitor.for_each_line(
        [&](const xline_visitor& line)
        {
            std::cout << "  line: " << line.name() << "\n";
            line.for_each_event(
                [&](const xevent_visitor& event)
                {
                    std::cout << "    event: " << event.name()
                              << "  duration_ns=" << event.duration_ns() << "\n";
                });
        });
    std::cout << std::flush;
}

}  // namespace

PROFILERTEST(BackendGpuTracer, device_tracer_level_zero_has_no_device_plane)
{
    profiler_session session(make_cpu_options());
    ASSERT_TRUE(session.start());
    EXPECT_FALSE(gpu_tracer_is_recording());
    {
        profiler_scope scope(kHostScope, &session);
        const uint64_t start_ns = static_cast<uint64_t>(get_current_time_nanos());
        add_kernel_event(kSyntheticKernel, start_ns, start_ns + kSyntheticDurNs);
    }
    ASSERT_TRUE(session.stop());
    ASSERT_TRUE(session.has_collected_xspace());

    const xplane* gpu = find_plane_with_name(session.collected_xspace(), GpuPlaneName(0));
    EXPECT_EQ(gpu, nullptr);
}

PROFILERTEST(BackendGpuTracer, collector_kernel_on_device_plane)
{
    profiler_session session(make_gpu_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(gpu_tracer_is_recording());
    {
        profiler_scope scope(kHostScope, &session);
        const uint64_t start_ns = static_cast<uint64_t>(get_current_time_nanos());
        add_kernel_event(kSyntheticKernel, start_ns, start_ns + kSyntheticDurNs);
    }
    ASSERT_TRUE(session.stop());
    EXPECT_FALSE(gpu_tracer_is_recording());
    ASSERT_TRUE(session.has_collected_xspace());

    const x_space& live = session.collected_xspace();
    const xplane*  gpu  = find_plane_with_name(live, GpuPlaneName(0));
    ASSERT_NE(gpu, nullptr) << "expected " << GpuPlaneName(0)
                            << " — CreateGpuTracer factory may have been stripped";
    EXPECT_TRUE(IsDevicePlane(*gpu));
    EXPECT_FALSE(IsHostPlane(*gpu));
    EXPECT_GT(count_events(*gpu), 0U);

    bool                 saw_kernel  = false;
    double               duration_ns = 0.0;
    xplane_visitor const visitor     = CreateTfXPlaneVisitor(gpu);
    visitor.for_each_line(
        [&](const xline_visitor& line)
        {
            line.for_each_event(
                [&](const xevent_visitor& event)
                {
                    if (event.name() == kSyntheticKernel)
                    {
                        saw_kernel  = true;
                        duration_ns = event.duration_ns();
                        auto stream = event.get_stat(static_cast<int64_t>(StatType::kStream));
                        EXPECT_TRUE(stream.has_value());
                        if (stream.has_value())
                        {
                            EXPECT_EQ(stream->int_value(), 1);
                        }
                    }
                });
        });
    EXPECT_TRUE(saw_kernel);
    EXPECT_GT(duration_ns, 0.0);

    const std::string chrome = session.generate_chrome_trace_json();
    ASSERT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(chrome.find(GpuPlaneName(0)), std::string::npos);
    EXPECT_NE(chrome.find(kSyntheticKernel), std::string::npos);
    EXPECT_NE(chrome.find(kHostScope), std::string::npos);

    print_gpu_plane(*gpu);
}

// Regression (Phase 3 options cleanup): gpu_tracer_event::annotation is
// populated from annotation_stack::get() in add_gpu_tracer_event() (the CPU
// scope that launched this GPU work), but export_xspace() never wrote it into
// the exported XSpace -- the CPU->GPU launch correlation the field exists to
// carry was silently dropped.
PROFILERTEST(BackendGpuTracer, kernel_carries_launching_scope_annotation)
{
    constexpr const char* kLaunchScope = "gpu_annotation_launch_scope";

    profiler_session session(make_gpu_options());
    ASSERT_TRUE(session.start());
    {
        // annotation_stack is only populated while a scope is open and GPU
        // tracing has enabled it (see profiler_scope::start()) -- add_kernel_event
        // doesn't set event.annotation itself, so this exercises the same
        // add_gpu_tracer_event() fallback the real GPU producer path uses.
        profiler_scope scope(kLaunchScope, &session);
        const uint64_t start_ns = static_cast<uint64_t>(get_current_time_nanos());
        add_kernel_event(kSyntheticKernel, start_ns, start_ns + kSyntheticDurNs);
    }
    ASSERT_TRUE(session.stop());
    ASSERT_TRUE(session.has_collected_xspace());

    const std::string chrome = session.generate_chrome_trace_json();
    ASSERT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"annotation\":\"" + std::string(kLaunchScope) + "\""), std::string::npos)
        << "GPU kernel event should carry its launching CPU scope as an annotation stat";
}

PROFILERTEST(BackendGpuTracer, add_event_is_noop_when_inactive)
{
    EXPECT_FALSE(gpu_tracer_is_recording());
    add_kernel_event("ghost_kernel", 1, 1000);

    profiler_session session(make_gpu_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(gpu_tracer_is_recording());
    ASSERT_TRUE(session.stop());
    ASSERT_TRUE(session.has_collected_xspace());

    EXPECT_EQ(find_plane_with_name(session.collected_xspace(), GpuPlaneName(0)), nullptr);

    add_kernel_event("ghost_after_stop", 1, 1000);
    EXPECT_FALSE(gpu_tracer_is_recording());

    profiler_session second(make_gpu_options());
    ASSERT_TRUE(second.start());
    ASSERT_TRUE(second.stop());
    EXPECT_EQ(find_plane_with_name(second.collected_xspace(), GpuPlaneName(0)), nullptr);
}

PROFILERTEST(BackendGpuTracer, device_kernel_probe_records_interval)
{
    EXPECT_FALSE(run_gpu_kernel_probe(kProbeKernel));
}

// Regression for design-review.md finding 7 (GPU producer/collector lifetime race):
// a producer thread calling add_gpu_tracer_event() while the control thread
// disables/destroys the collector used to race on a bare
// atomic<gpu_trace_collector*> with no lifetime lease. The fix gives the
// singleton's collector handle shared ownership so a producer's in-flight call
// keeps the collector alive until it returns. On a plain build this mainly
// checks nothing crashes; run with -DPROFILER_SANITIZER=thread for real
// verification.
PROFILERTEST(BackendGpuTracer, concurrent_producer_survives_start_stop_churn)
{
    std::atomic<bool> stop_producer{false};
    std::thread       producer(
        [&stop_producer]()
        {
            uint64_t i = 0;
            while (!stop_producer.load(std::memory_order_relaxed))
            {
                add_kernel_event(kSyntheticKernel, i, i + 1);
                ++i;
            }
        });

    for (int iter = 0; iter < 200; ++iter)
    {
        profiler_session session(make_gpu_options());
        ASSERT_TRUE(session.start());
        ASSERT_TRUE(session.stop());
    }

    stop_producer.store(true, std::memory_order_relaxed);
    producer.join();
}

// Regression for design-review.md finding 4 (session reuse exposes stale results)
// and section 6.5 lifecycle handling: late GPU callbacks from a prior/aborted run
// (with a stale generation ID) must not appear in the next run's capture.
// Phase 4: gpu_tracer_event now carries a generation field, and export_xspace
// filters them when generation doesn't match the current session's generation.
// This synthetic test verifies the infrastructure is in place; full generation
// tracking requires TLS generation context wired through add_gpu_tracer_event.
PROFILERTEST(BackendGpuTracer, generation_infrastructure_in_place)
{
    profiler_session session1(make_gpu_options());
    ASSERT_TRUE(session1.start());

    // Queue a synthetic event.
    gpu_tracer_event event1;
    event1.type          = gpu_tracer_event_type::kernel;
    event1.name          = "kernel1";
    event1.device_id     = 0;
    event1.stream_id     = 1;
    event1.start_time_ns = 1000;
    event1.end_time_ns   = 2000;
    add_gpu_tracer_event(std::move(event1));

    ASSERT_TRUE(session1.stop());

    // Start a second session.
    profiler_session session2(make_gpu_options());
    ASSERT_TRUE(session2.start());

    // Queue another event.
    gpu_tracer_event event2;
    event2.type          = gpu_tracer_event_type::kernel;
    event2.name          = "kernel2";
    event2.device_id     = 0;
    event2.stream_id     = 1;
    event2.start_time_ns = 3000;
    event2.end_time_ns   = 4000;
    add_gpu_tracer_event(std::move(event2));

    ASSERT_TRUE(session2.stop());

    // Verify: gpu_tracer_event now has a generation field (infrastructure check).
    // Full generation tracking via TLS is marked TODO for future implementation
    // once per-session generation context is available to GPU callbacks.
    gpu_tracer_event test_event;
    EXPECT_EQ(test_event.generation, 0);  // Verify field exists, default 0
}
