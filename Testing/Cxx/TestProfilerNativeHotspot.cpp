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
 * End-to-end: native profiler_session → scope tree → Kineto-like hotspots
 * =============================================================================
 *
 * Mirrors TestHotspotReport.cpp's nested busy-wait shape, but drives the
 * always-on native path (profiler_scope / TraceMe / XSpace) instead of
 * PROFILER_RECORD_USER_SCOPE + Kineto ProfilerResult.
 */

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "ProfilerTest.h"
#include "native/analysis/hotspot_report.h"
#include "native/session/profiler.h"
#include "native/session/profiler_report.h"

using namespace profiler;

namespace
{

constexpr const char* kOuter  = "native_hotspot_outer";
constexpr const char* kInnerA = "native_hotspot_inner_a";
constexpr const char* kInnerB = "native_hotspot_inner_b";

void busy_wait_for(std::chrono::microseconds duration)
{
    const auto   start = std::chrono::high_resolution_clock::now();
    volatile int spin  = 0;
    while (std::chrono::high_resolution_clock::now() - start < duration)
    {
        spin = spin + 1;
    }
}

profiler_options make_options()
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = true;
    opts.output_format_                 = profiler_options::output_format_enum::CONSOLE;
    return opts;
}

void run_nested_workload(profiler_session& session)
{
    profiler_scope outer(kOuter, &session);
    busy_wait_for(std::chrono::milliseconds(1));
    {
        profiler_scope inner_a(kInnerA, &session);
        busy_wait_for(std::chrono::milliseconds(2));
    }
    {
        profiler_scope inner_b(kInnerB, &session);
        busy_wait_for(std::chrono::milliseconds(1));
    }
}

const hotspot_entry* find_entry(const hotspot_report& report, const std::string& name)
{
    for (const auto& entry : report.hotspots())
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

PROFILERTEST(BackendNativeHotspot, self_time_excludes_children)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_hotspot_report();
    ASSERT_NE(report, nullptr);
    ASSERT_FALSE(report->hotspots().empty());

    const hotspot_entry* outer   = find_entry(*report, kOuter);
    const hotspot_entry* inner_a = find_entry(*report, kInnerA);
    ASSERT_NE(outer, nullptr) << "outer scope missing from native hotspots";
    ASSERT_NE(inner_a, nullptr) << "inner_a missing from native hotspots";

    // Outer includes children, so self < total (same invariant as Kineto TestHotspotReport).
    // Do not compare inner_a self vs outer self: Debug instrumentation on macOS
    // can make outer's own time larger than inner_a's 2ms busy-wait.
    EXPECT_LT(outer->self_time_ns, outer->total_time_ns);
    EXPECT_EQ(inner_a->self_time_ns, inner_a->total_time_ns);
    EXPECT_GT(inner_a->self_time_ns, 0U);
}

PROFILERTEST(BackendNativeHotspot, bottom_up_sorted_by_self_time_descending)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_hotspot_report();
    ASSERT_NE(report, nullptr);
    const auto& hotspots = report->hotspots();
    for (size_t i = 1; i < hotspots.size(); ++i)
    {
        EXPECT_GE(hotspots[i - 1].self_time_ns, hotspots[i].self_time_ns);
    }
}

PROFILERTEST(BackendNativeHotspot, call_stack_and_text_renderers)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_hotspot_report();
    ASSERT_NE(report, nullptr);

    const auto path = report->call_stack_for(kInnerB);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), kOuter);
    EXPECT_EQ(path.back(), kInnerB);

    EXPECT_FALSE(report->top_down_tree().empty());
    EXPECT_FALSE(report->bottom_up_hotspots().empty());
    EXPECT_NE(report->bottom_up_hotspots().find(kOuter), std::string::npos);

    const std::string table = report->table();
    EXPECT_NE(table.find("Self CPU"), std::string::npos);
    EXPECT_NE(table.find(kOuter), std::string::npos);
    EXPECT_NE(table.find(kInnerA), std::string::npos);
    EXPECT_NE(table.find("# of Calls"), std::string::npos);

    std::cout << "\n=== Native hotspot report ===\n";
    std::cout << "--- Operator table ---\n" << table;
    std::cout << "\n--- Top-down call tree ---\n" << report->top_down_tree();
    std::cout << "\n--- Bottom-up hotspots ---\n" << report->bottom_up_hotspots();
    std::cout << std::flush;
}

PROFILERTEST(BackendNativeHotspot, console_report_includes_hotspots_section)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    run_nested_workload(session);
    ASSERT_TRUE(session.stop());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);
    const std::string console = report->generate_console_report();
    EXPECT_NE(console.find("=== Hotspots ==="), std::string::npos);
    EXPECT_NE(console.find("self time"), std::string::npos);
    EXPECT_NE(console.find(kOuter), std::string::npos);
    EXPECT_NE(console.find(kInnerA), std::string::npos);

    const std::string xml = report->generate_xml_report();
    EXPECT_NE(xml.find("<hotspots>"), std::string::npos);
    EXPECT_NE(xml.find("=== Hotspots ==="), std::string::npos);

    std::cout << "\n=== Native profiler console report ===\n" << console << std::flush;
    session.print_report();
}

PROFILERTEST(BackendNativeHotspot, empty_session_has_no_hotspot_rows)
{
    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(session.stop());

    auto report = session.generate_hotspot_report();
    ASSERT_NE(report, nullptr);
    EXPECT_TRUE(report->hotspots().empty());
    EXPECT_TRUE(report->bottom_up_hotspots().find(kOuter) == std::string::npos);
}
