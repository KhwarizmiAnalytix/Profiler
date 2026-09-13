/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

/*
 * =============================================================================
 * TensorFlow GpuTracer → `/device:GPU:N` XPlane → Chrome JSON
 * =============================================================================
 *
 * Mirrors TF device_tracer_cuda.cc: CreateGpuTracer is gated by
 * device_tracer_level; activities are added via gpu_trace_collector::AddEvent
 * (CUPTI analog). Synthetic events run on every platform. The device kernel
 * probe is live when PROFILER_HAS_METAL=1.
 */

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include "ProfilerTest.h"
#include "native/exporters/xplane/tf_xplane_visitor.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/exporters/xplane/xplane_visitor.h"
#include "native/gpu/gpu_tracer.h"
#include "native/session/profiler.h"
#include "native/tracing/traceme.h"

#ifndef PROFILER_HAS_METAL
#define PROFILER_HAS_METAL 0
#endif

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
#if !PROFILER_HAS_METAL
    EXPECT_FALSE(run_gpu_kernel_probe(kProbeKernel));
#else
    profiler_session session(make_gpu_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(gpu_tracer_is_recording());
    {
        profiler_scope scope(kHostScope, &session);
        if (!run_gpu_kernel_probe(kProbeKernel))
        {
            ASSERT_TRUE(session.stop());
            GTEST_SKIP() << "No GPU device available";
        }
    }
    ASSERT_TRUE(session.stop());
    ASSERT_TRUE(session.has_collected_xspace());

    const xplane* gpu = find_plane_with_name(session.collected_xspace(), GpuPlaneName(0));
    ASSERT_NE(gpu, nullptr);
    EXPECT_TRUE(IsDevicePlane(*gpu));
    EXPECT_GT(count_events(*gpu), 0U);

    bool                 saw_probe   = false;
    double               duration_ns = 0.0;
    xplane_visitor const visitor     = CreateTfXPlaneVisitor(gpu);
    visitor.for_each_line(
        [&](const xline_visitor& line)
        {
            line.for_each_event(
                [&](const xevent_visitor& event)
                {
                    if (event.name() == kProbeKernel)
                    {
                        saw_probe   = true;
                        duration_ns = event.duration_ns();
                    }
                });
        });
    EXPECT_TRUE(saw_probe);
    EXPECT_GT(duration_ns, 0.0);

    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_NE(chrome.find(GpuPlaneName(0)), std::string::npos);
    EXPECT_NE(chrome.find(kProbeKernel), std::string::npos);

    print_gpu_plane(*gpu);
#endif
}
