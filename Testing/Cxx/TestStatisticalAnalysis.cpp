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

// Unit tests for native/analysis/stats_calculator.h (stat, stat_with_percentiles,
// stats_calculator) and native/analysis/statistical_analyzer.h. Otherwise only
// exercised indirectly through session::generate_report()/hotspot tooling.

#include <cmath>
#include <string>

#include "ProfilerTest.h"
#include "native/analysis/statistical_analyzer.h"
#include "native/analysis/stats_calculator.h"

// ---------------------------------------------------------------------------
// stat<ValueType>
// ---------------------------------------------------------------------------

PROFILERTEST(Stat, tracks_basic_aggregates)
{
    profiler::stat<int64_t> s;
    EXPECT_TRUE(s.empty());

    s.update_stat(10);
    s.update_stat(20);
    s.update_stat(30);

    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.first(), 10);
    EXPECT_EQ(s.newest(), 30);
    EXPECT_EQ(s.max(), 30);
    EXPECT_EQ(s.min(), 10);
    EXPECT_EQ(s.count(), 3);
    EXPECT_EQ(s.sum(), 60);
    EXPECT_NEAR(s.avg(), 20.0, 1e-9);
    EXPECT_FALSE(s.all_same());
}

PROFILERTEST(Stat, all_same_detects_constant_series)
{
    profiler::stat<int64_t> s;
    s.update_stat(7);
    s.update_stat(7);
    s.update_stat(7);
    EXPECT_TRUE(s.all_same());
    EXPECT_EQ(s.variance(), 0);
    EXPECT_EQ(s.std_deviation(), 0);
}

PROFILERTEST(Stat, reset_clears_accumulated_state)
{
    profiler::stat<int64_t> s;
    s.update_stat(1);
    s.update_stat(2);
    s.reset();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.count(), 0);
}

// ---------------------------------------------------------------------------
// stat_with_percentiles<ValueType>
// ---------------------------------------------------------------------------

PROFILERTEST(StatWithPercentiles, percentile_100_returns_true_maximum_regardless_of_insertion_order)
{
    profiler::stat_with_percentiles<int> s;
    for (int v : {5, 3, 1, 4, 2})  // Last inserted (2) must not be mistaken for the maximum (5).
    {
        s.update_stat(v);
    }
    EXPECT_EQ(s.percentile(100), 5);
    EXPECT_EQ(s.percentile(0), 1);
}

PROFILERTEST(StatWithPercentiles, invalid_percentile_returns_nan)
{
    profiler::stat_with_percentiles<double> s;
    s.update_stat(1.0);
    EXPECT_TRUE(std::isnan(s.percentile(-1)));
    EXPECT_TRUE(std::isnan(s.percentile(101)));
}

// ---------------------------------------------------------------------------
// stats_calculator
// ---------------------------------------------------------------------------

PROFILERTEST(StatsCalculator, aggregates_repeated_node_stats)
{
    profiler::stat_summarizer_options options;
    profiler::stats_calculator        calc(options);

    calc.add_node_stats("op_a", "MatMul", /*run_order=*/0, /*elapsed_time=*/100, /*mem_used=*/1024);
    calc.add_node_stats("op_a", "MatMul", /*run_order=*/0, /*elapsed_time=*/200, /*mem_used=*/2048);
    calc.update_run_total_us(300);

    EXPECT_EQ(calc.num_runs(), 1);
    const auto& details = calc.get_details();
    ASSERT_EQ(details.count("op_a"), 1u);
    const auto& detail = details.at("op_a");
    EXPECT_EQ(detail.type, "MatMul");
    EXPECT_EQ(detail.times_called, 2);
    EXPECT_EQ(detail.elapsed_time.sum(), 300);
    EXPECT_EQ(detail.mem_used.newest(), 2048);
}

PROFILERTEST(StatsCalculator, output_string_contains_recorded_node_name)
{
    profiler::stat_summarizer_options options;
    profiler::stats_calculator        calc(options);
    calc.add_node_stats(
        "conv2d", "Conv2D", /*run_order=*/0, /*elapsed_time=*/500, /*mem_used=*/4096);
    calc.update_run_total_us(500);  // Required: get_output_string() divides by run count.

    const std::string output = calc.get_output_string();
    EXPECT_NE(output.find("conv2d"), std::string::npos);

    const std::string summary = calc.get_short_summary();
    EXPECT_NE(summary.find("1 nodes observed"), std::string::npos);
}

// ---------------------------------------------------------------------------
// statistical_analyzer
// ---------------------------------------------------------------------------

PROFILERTEST(StatisticalAnalyzer, samples_are_ignored_before_start_analysis)
{
    profiler::statistical_analyzer analyzer;
    analyzer.add_timing_sample("op", 1.0);  // Not analyzing yet: dropped.
    EXPECT_EQ(analyzer.get_sample_count("op"), 0u);

    analyzer.start_analysis();
    EXPECT_TRUE(analyzer.is_analyzing());
    analyzer.add_timing_sample("op", 2.0);
    EXPECT_EQ(analyzer.get_sample_count("op"), 1u);
    analyzer.stop_analysis();
}

PROFILERTEST(StatisticalAnalyzer, calculate_timing_stats_matches_manual_computation)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    for (double v : {10.0, 20.0, 30.0, 40.0})
    {
        analyzer.add_timing_sample("latency", v);
    }

    auto stats = analyzer.calculate_timing_stats("latency");
    ASSERT_TRUE(stats.is_valid());
    EXPECT_EQ(stats.count, 4u);
    EXPECT_NEAR(stats.mean, 25.0, 1e-9);
    EXPECT_NEAR(stats.min_value, 10.0, 1e-9);
    EXPECT_NEAR(stats.max_value, 40.0, 1e-9);
    EXPECT_NEAR(stats.median, 25.0, 1e-9);
}

PROFILERTEST(StatisticalAnalyzer, unknown_series_returns_invalid_metrics)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    auto stats = analyzer.calculate_timing_stats("does_not_exist");
    EXPECT_FALSE(stats.is_valid());
    EXPECT_EQ(stats.count, 0u);
}

PROFILERTEST(StatisticalAnalyzer, clear_series_removes_only_the_named_series)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    analyzer.add_timing_sample("a", 1.0);
    analyzer.add_timing_sample("b", 2.0);
    analyzer.clear_series("a");
    EXPECT_EQ(analyzer.get_sample_count("a"), 0u);
    EXPECT_EQ(analyzer.get_sample_count("b"), 1u);
}

PROFILERTEST(StatisticalAnalyzer, trend_slope_detects_increasing_and_flat_series)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0})
    {
        analyzer.add_time_series_point("rising", v);
    }
    for (double v : {5.0, 5.0, 5.0, 5.0})
    {
        analyzer.add_time_series_point("flat", v);
    }

    EXPECT_TRUE(analyzer.is_trending_up("rising", 0.1));
    EXPECT_FALSE(analyzer.is_trending_down("rising", 0.1));
    EXPECT_FALSE(analyzer.is_trending_up("flat", 0.1));
    EXPECT_FALSE(analyzer.is_trending_down("flat", 0.1));
}

PROFILERTEST(StatisticalAnalyzer, correlation_detects_positive_and_negative_relationships)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0})
    {
        analyzer.add_time_series_point("x", v);
    }
    for (double v : {2.0, 4.0, 6.0, 8.0, 10.0})  // y = 2x: perfectly correlated.
    {
        analyzer.add_time_series_point("y_pos", v);
    }
    for (double v : {5.0, 4.0, 3.0, 2.0, 1.0})  // Perfectly anti-correlated with x.
    {
        analyzer.add_time_series_point("y_neg", v);
    }

    EXPECT_NEAR(analyzer.calculate_correlation("x", "y_pos"), 1.0, 1e-6);
    EXPECT_NEAR(analyzer.calculate_correlation("x", "y_neg"), -1.0, 1e-6);
}

PROFILERTEST(StatisticalAnalyzer, detect_performance_regression_flags_large_slowdowns)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    for (int i = 0; i < 5; ++i)
    {
        analyzer.add_timing_sample("op", 100.0);
    }

    EXPECT_TRUE(analyzer.detect_performance_regression("op", /*baseline_mean=*/50.0, 0.1));
    EXPECT_FALSE(analyzer.detect_performance_regression("op", /*baseline_mean=*/99.0, 0.5));
}

PROFILERTEST(StatisticalAnalysisScope, destructor_records_elapsed_timing_sample)
{
    profiler::statistical_analyzer analyzer;
    analyzer.start_analysis();
    {
        profiler::statistical_analysis_scope scope(analyzer, "scoped_op");
        (void)scope;
    }  // Destructor should add exactly one timing sample for "scoped_op".

    EXPECT_EQ(analyzer.get_sample_count("scoped_op"), 1u);
    auto stats = analyzer.calculate_timing_stats("scoped_op");
    ASSERT_TRUE(stats.is_valid());
    EXPECT_GE(stats.mean, 0.0);
}
