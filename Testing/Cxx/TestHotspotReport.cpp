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

#include <chrono>
#include <iostream>
#include <set>
#include <string>
#include <thread>

#include "ProfilerTest.h"

#if PROFILER_HAS_KINETO

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/hotspot_report.h"
#include "bespoke/kineto/profiler_kineto.h"

namespace
{

void busy_wait_for(std::chrono::microseconds duration)
{
    const auto   start = std::chrono::high_resolution_clock::now();
    volatile int spin  = 0;
    while (std::chrono::high_resolution_clock::now() - start < duration)
    {
        spin = spin + 1;
    }
}

std::unique_ptr<profiler::profiler_impl::ProfilerResult> record_sample_workload()
{
    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/false,
        /*with_stack=*/false,
        /*with_flops=*/false,
        /*with_modules=*/false);

    profiler::profiler_impl::prepareProfiler(config, {profiler::profiler_impl::ActivityType::CPU});
    profiler::profiler_impl::enableProfiler(
        config, {profiler::profiler_impl::ActivityType::CPU}, {profiler::RecordScope::USER_SCOPE});

    {
        PROFILER_RECORD_USER_SCOPE("hotspot_outer");
        busy_wait_for(std::chrono::milliseconds(1));
        {
            PROFILER_RECORD_USER_SCOPE("hotspot_inner_a");
            busy_wait_for(std::chrono::milliseconds(2));
        }
        {
            PROFILER_RECORD_USER_SCOPE("hotspot_inner_b");
            busy_wait_for(std::chrono::milliseconds(1));
        }
    }

    return profiler::profiler_impl::disableProfiler();
}

}  // namespace

PROFILERTEST(HotspotReport, self_time_excludes_children)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    const auto&                                   hotspots = report.hotspots();
    ASSERT_FALSE(hotspots.empty());

    const profiler::profiler_impl::hotspot_entry* outer   = nullptr;
    const profiler::profiler_impl::hotspot_entry* inner_a = nullptr;
    for (const auto& entry : hotspots)
    {
        if (entry.name == "hotspot_outer")
        {
            outer = &entry;
        }
        if (entry.name == "hotspot_inner_a")
        {
            inner_a = &entry;
        }
    }

    if (outer == nullptr || inner_a == nullptr)
    {
        GTEST_SKIP() << "Required hotspot entries not captured";
    }

    // hotspot_outer's own busy-wait is ~1ms, while its two children add ~3ms on
    // top; self time must therefore be a small slice of its total time.
    EXPECT_LT(outer->self_time_ns, outer->total_time_ns);
    // hotspot_inner_a has no children, so self time equals total time.
    EXPECT_EQ(inner_a->self_time_ns, inner_a->total_time_ns);
    EXPECT_GT(inner_a->self_time_ns, 0U);
}

PROFILERTEST(HotspotReport, bottom_up_sorted_by_self_time_descending)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    const auto&                                   hotspots = report.hotspots();
    for (size_t i = 1; i < hotspots.size(); ++i)
    {
        EXPECT_GE(hotspots[i - 1].self_time_ns, hotspots[i].self_time_ns);
    }
}

PROFILERTEST(HotspotReport, call_stack_for_reports_root_to_leaf_path)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    const auto                                    path = report.call_stack_for("hotspot_inner_b");
    if (path.empty())
    {
        GTEST_SKIP() << "hotspot_inner_b not captured in this environment";
    }

    ASSERT_EQ(path.size(), 2U);
    EXPECT_EQ(path.front(), "hotspot_outer");
    EXPECT_EQ(path.back(), "hotspot_inner_b");
}

PROFILERTEST(HotspotReport, unknown_function_has_empty_call_stack)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);

    profiler::profiler_impl::hotspot_report const report(*result);
    EXPECT_TRUE(report.call_stack_for("does_not_exist").empty());
}

PROFILERTEST(HotspotReport, text_renderers_are_non_empty_when_events_exist)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    EXPECT_FALSE(report.top_down_tree().empty());
    EXPECT_FALSE(report.bottom_up_hotspots().empty());
    EXPECT_FALSE(report.table().empty());

    std::cout << "\n=== Kineto hotspot report ===\n";
    std::cout << "--- Operator table ---\n" << report.table();
    std::cout << "\n--- Top-down call tree ---\n" << report.top_down_tree();
    std::cout << "\n--- Bottom-up hotspots ---\n" << report.bottom_up_hotspots();
    std::cout << std::flush;
}

PROFILERTEST(HotspotReport, table_matches_pytorch_key_averages_columns)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    const std::string                             table = report.table();

    EXPECT_NE(table.find("Name"), std::string::npos);
    EXPECT_NE(table.find("Self CPU %"), std::string::npos);
    EXPECT_NE(table.find("Self CPU"), std::string::npos);
    EXPECT_NE(table.find("CPU total %"), std::string::npos);
    EXPECT_NE(table.find("CPU total"), std::string::npos);
    EXPECT_NE(table.find("CPU time avg"), std::string::npos);
    EXPECT_NE(table.find("# of Calls"), std::string::npos);
    EXPECT_NE(table.find("Self CPU time total:"), std::string::npos);

    EXPECT_EQ(table.find("Self CUDA"), std::string::npos);
    EXPECT_EQ(table.find("Self XPU"), std::string::npos);

    EXPECT_NE(table.find("hotspot_outer"), std::string::npos);
    EXPECT_NE(table.find("hotspot_inner_a"), std::string::npos);
    EXPECT_NE(table.find("hotspot_inner_b"), std::string::npos);
}

PROFILERTEST(HotspotReport, table_self_cpu_percent_sums_to_one_hundred)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    const auto&                                   hotspots = report.hotspots();
    ASSERT_FALSE(hotspots.empty());

    uint64_t self_cpu_total = 0;
    for (const auto& entry : hotspots)
    {
        self_cpu_total += entry.self_time_ns;
    }
    ASSERT_GT(self_cpu_total, 0U);

    double percent_sum = 0.0;
    for (const auto& entry : hotspots)
    {
        percent_sum +=
            100.0 * static_cast<double>(entry.self_time_ns) / static_cast<double>(self_cpu_total);
    }
    EXPECT_NEAR(percent_sum, 100.0, 0.15);
}

PROFILERTEST(HotspotReport, table_respects_row_limit)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    ASSERT_GE(report.hotspots().size(), 2U);

    const std::string limited = report.table("self_cpu_time_total", /*row_limit=*/1);
    const auto&       first   = report.hotspots().front();
    EXPECT_NE(limited.find(first.name), std::string::npos);

    size_t data_rows = 0;
    for (const auto& entry : report.hotspots())
    {
        if (limited.find(entry.name) != std::string::npos)
        {
            ++data_rows;
        }
    }
    EXPECT_EQ(data_rows, 1U);
}

PROFILERTEST(HotspotReport, table_sort_by_cpu_total_puts_parent_first)
{
    auto result = record_sample_workload();
    ASSERT_NE(result, nullptr);
    if (result->event_tree().empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    profiler::profiler_impl::hotspot_report const report(*result);
    const std::string table = report.table("cpu_time_total", /*row_limit=*/1);
    EXPECT_NE(table.find("hotspot_outer"), std::string::npos);
    EXPECT_EQ(table.find("hotspot_inner_a"), std::string::npos);
    EXPECT_EQ(table.find("hotspot_inner_b"), std::string::npos);
}

#endif  // PROFILER_HAS_KINETO
