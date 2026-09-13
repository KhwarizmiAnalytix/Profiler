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
 * End-to-end: profiler_session → XSpace → shared xplane utils/visitor → export
 * =============================================================================
 *
 * Exercises the real pipeline without a hand-built parallel XPlane stack:
 *   session.start/stop → collected_xspace()
 *   NormalizeTimestamps (via session.stop), sort_x_space, MergePlanes
 *   CreateTfXPlaneVisitor (schema getters) on live planes
 *   Chrome Trace + report still derived from the session XSpace
 */

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ProfilerTest.h"
#include "native/exporters/xplane/tf_xplane_visitor.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/exporters/xplane/xplane_visitor.h"
#include "native/session/profiler.h"
#include "native/session/profiler_report.h"
#include "native/tracing/traceme.h"
#include "native/tracing/traceme_encode.h"

using namespace profiler;

namespace
{

// HostEventType map entry — visitor schema lookup resolves this name to a type.
constexpr const char* kTypedHostEvent = "MemoryAllocation";
constexpr const char* kOuterScope     = "xplane_pipeline_outer";
constexpr const char* kInnerScope     = "xplane_pipeline_inner";

profiler_options make_options()
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = true;
    opts.track_memory_deltas_           = true;
    opts.track_peak_memory_             = true;
    opts.output_format_                 = profiler_options::output_format_enum::JSON;
    return opts;
}

void run_nested_workload(profiler_session& session)
{
    profiler_scope outer(kOuterScope, &session);
    {
        profiler_scope inner(kInnerScope, &session);
        // Name matches HostEventType so CreateTfXPlaneVisitor maps event types.
        profiler_scope typed(kTypedHostEvent, &session);
        traceme        encoded(
            [&]()
            {
                // Keys match StatType map so CreateTfXPlaneVisitor types them.
                return traceme_encode(
                    "xplane_pipeline_kernel", {{"group_id", 7}, {"requested_bytes", 4096}});
            });
        volatile double sink = 0.0;
        for (int i = 0; i < 2000; ++i)
        {
            sink += static_cast<double>(i) * 0.001;
        }
        (void)sink;
    }

    std::thread worker(
        [&session]()
        {
            profiler_scope scope("xplane_pipeline_worker", &session);
            int            spin = 0;
            for (int i = 0; i < 500; ++i)
            {
                ++spin;
            }
            EXPECT_GT(spin, 0);
        });
    worker.join();
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

}  // namespace

PROFILERTEST(BackendXPlanePipeline, session_xspace_sort_merge_visitor_export)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    ASSERT_TRUE(session.has_collected_xspace());
    const x_space& live = session.collected_xspace();

    EXPECT_FALSE(IsEmpty(live));
    EXPECT_FALSE(live.hostnames().empty());
    EXPECT_EQ(live.hostnames().front(), "localhost");

    const xplane* host = find_plane_with_name(live, kHostThreadsPlaneName);
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(IsHostPlane(*host));
    EXPECT_FALSE(IsDevicePlane(*host));
    EXPECT_GT(count_events(*host), 0U);
    EXPECT_GE(GetStartTimestampNs(*host), 0);

    // Typed visitor pulls schema FindHostEventType / FindStatType for live metadata.
    {
        xplane_visitor const visitor = CreateTfXPlaneVisitor(host);
        EXPECT_EQ(visitor.name(), kHostThreadsPlaneName);

        bool   saw_typed_event = false;
        bool   saw_outer       = false;
        bool   saw_group_stat  = false;
        size_t event_count     = 0;
        visitor.for_each_line(
            [&](const xline_visitor& line)
            {
                line.for_each_event(
                    [&](const xevent_visitor& event)
                    {
                        ++event_count;
                        if (event.name() == kTypedHostEvent)
                        {
                            saw_typed_event = true;
                            EXPECT_TRUE(event.type().has_value());
                            EXPECT_EQ(*event.type(), static_cast<int64_t>(kMemoryAllocation));
                        }
                        if (event.name() == kOuterScope)
                        {
                            saw_outer = true;
                        }
                        if (event.name() == "xplane_pipeline_kernel")
                        {
                            auto group = event.get_stat(static_cast<int64_t>(StatType::kGroupId));
                            if (group.has_value())
                            {
                                saw_group_stat = true;
                                EXPECT_EQ(group->int_value(), 7);
                            }
                        }
                    });
            });
        EXPECT_GT(event_count, 0U);
        EXPECT_TRUE(saw_typed_event);
        EXPECT_TRUE(saw_outer);
        EXPECT_TRUE(saw_group_stat);
    }

    // Post-process a copy — do not mutate session state used by Chrome/report.
    x_space scratch = live;
    sort_x_space(&scratch);
    xplane* scratch_host = find_mutable_plane_with_name(&scratch, kHostThreadsPlaneName);
    ASSERT_NE(scratch_host, nullptr);

    xplane merged;
    merged.set_name(std::string(kHostThreadsPlaneName));
    MergePlanes(*scratch_host, &merged);
    EXPECT_GT(count_events(merged), 0U);
    EXPECT_TRUE(IsHostPlane(merged));

    {
        xplane_visitor const merged_visitor = CreateTfXPlaneVisitor(&merged);
        size_t               merged_events  = 0;
        merged_visitor.for_each_line(
            [&](const xline_visitor& line)
            { line.for_each_event([&](const xevent_visitor& /*event*/) { ++merged_events; }); });
        EXPECT_EQ(merged_events, count_events(merged));
    }

    // Session exports remain end-to-end on the unmodified collected XSpace.
    const std::string chrome = session.generate_chrome_trace_json();
    ASSERT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(chrome.find(kOuterScope), std::string::npos);
    EXPECT_NE(chrome.find(kInnerScope), std::string::npos);
    EXPECT_NE(chrome.find(kTypedHostEvent), std::string::npos);

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);
    const std::string console = report->generate_console_report();
    EXPECT_FALSE(console.empty());
    EXPECT_NE(console.find(kOuterScope), std::string::npos);
    EXPECT_NE(console.find(kInnerScope), std::string::npos);
    // stats_calculator fed from the same collected_xspace the visitor walked above.
    EXPECT_NE(console.find("=== Node Stats (from XSpace) ==="), std::string::npos);
    EXPECT_NE(console.find("nodes observed"), std::string::npos);
    EXPECT_NE(console.find(kTypedHostEvent), std::string::npos);

    std::cout << "\n=== XPlane pipeline console report ===\n" << console << std::flush;
}

PROFILERTEST(BackendXPlanePipeline, empty_session_xspace_is_empty)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(session.stop());

    ASSERT_TRUE(session.has_collected_xspace());
    // No scopes → host tracer may still create an empty-ish space; IsEmpty checks events.
    // Either empty or host plane with zero events is acceptable for a no-op session.
    if (!IsEmpty(session.collected_xspace()))
    {
        const xplane* host =
            find_plane_with_name(session.collected_xspace(), kHostThreadsPlaneName);
        if (host != nullptr)
        {
            EXPECT_EQ(count_events(*host), 0U);
        }
    }
}
