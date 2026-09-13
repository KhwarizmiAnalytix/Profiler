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

#pragma once

// Prevent Windows min/max macros from interfering with std::numeric_limits
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "common/flat_hash.h"
#include "native/session/profiler.h"

namespace profiler
{

// Statistical metrics for a series of measurements
struct statistical_metrics
{
    double min_value     = (std::numeric_limits<double>::max)();
    double max_value     = (std::numeric_limits<double>::lowest)();
    double mean          = 0.0;
    double median        = 0.0;
    double std_deviation = 0.0;
    double variance      = 0.0;
    double sum           = 0.0;
    size_t count         = 0;

    // Percentiles (25th, 50th, 75th, 90th, 95th, 99th)
    std::vector<double> percentiles;

    // Outlier detection
    std::vector<double> outliers;
    double              outlier_threshold = 2.0;  // Standard deviations

    void reset();
    bool is_valid() const { return count > 0; }
};

// Time series data point
struct time_series_point
{
    std::chrono::high_resolution_clock::time_point timestamp_;
    double                                         value_;
    std::string                                    label_;
    std::thread::id                                thread_id_;
};

/**
 * @brief Statistical analyzer for profiling data (OPTIONAL COMPONENT)
 *
 * COMPONENT CLASSIFICATION: OPTIONAL
 * This component provides statistical analysis capabilities but is not required
 * for basic profiling functionality. Can be disabled to reduce binary size.
 */
class PROFILER_VISIBILITY statistical_analyzer
{
public:
    PROFILER_API statistical_analyzer();
    PROFILER_API ~statistical_analyzer();

    // Core interface
    PROFILER_API void start_analysis();
    PROFILER_API void stop_analysis();
    bool              is_analyzing() const { return analyzing_.load(); }

    // Data collection
    PROFILER_API void add_timing_sample(const std::string& name, double time_ms);
    PROFILER_API void add_memory_sample(const std::string& name, size_t memory_bytes);
    PROFILER_API void add_custom_sample(const std::string& name, double value);

    // Time series data
    PROFILER_API void add_time_series_point(
        const std::string& series_name, double value, const std::string& label = "");

    // Statistical analysis
    PROFILER_API profiler::statistical_metrics calculate_timing_stats(
        const std::string& name) const;
    PROFILER_API profiler::statistical_metrics calculate_memory_stats(
        const std::string& name) const;
    PROFILER_API profiler::statistical_metrics calculate_custom_stats(
        const std::string& name) const;

    // Batch analysis
    PROFILER_API std::unordered_map<std::string, profiler::statistical_metrics>
                 calculate_all_timing_stats() const;
    PROFILER_API std::unordered_map<std::string, profiler::statistical_metrics>
                 calculate_all_memory_stats() const;
    PROFILER_API std::unordered_map<std::string, profiler::statistical_metrics>
                 calculate_all_custom_stats() const;

    // Time series analysis
    PROFILER_API std::vector<profiler::time_series_point> get_time_series(
        const std::string& series_name) const;
    PROFILER_API profiler::statistical_metrics analyze_time_series(
        const std::string& series_name) const;

    // Trend analysis
    PROFILER_API double calculate_trend_slope(const std::string& series_name) const;
    PROFILER_API bool is_trending_up(const std::string& series_name, double threshold = 0.1) const;
    PROFILER_API bool is_trending_down(
        const std::string& series_name, double threshold = 0.1) const;

    // Correlation analysis
    PROFILER_API double calculate_correlation(
        const std::string& series1, const std::string& series2) const;

    // Performance regression detection
    PROFILER_API bool detect_performance_regression(
        const std::string& name, double baseline_mean, double threshold = 0.1) const;

    // Data management
    PROFILER_API void   clear_data();
    PROFILER_API void   clear_series(const std::string& name);
    PROFILER_API size_t get_sample_count(const std::string& name) const;

    // Configuration
    PROFILER_API void set_max_samples_per_series(size_t max_samples);
    PROFILER_API void set_outlier_threshold(double threshold);
    PROFILER_API void set_percentiles(const std::vector<double>& percentiles);

    // Optional threading hint (no-op placeholder for future parallel analysis)
    PROFILER_API void set_worker_threads_hint(size_t threads);

    // Public helper for external use
    PROFILER_API profiler::statistical_metrics calculate_metrics(
        const std::vector<double>& data) const;

private:
    std::atomic<bool> analyzing_{false};

    // Thread-safe data storage
    mutable std::mutex timing_mutex_;
    mutable std::mutex memory_mutex_;
    mutable std::mutex custom_mutex_;
    mutable std::mutex time_series_mutex_;

    std::unordered_map<std::string, std::vector<double>>                      timing_data_;
    std::unordered_map<std::string, std::vector<double>>                      memory_data_;
    std::unordered_map<std::string, std::vector<double>>                      custom_data_;
    std::unordered_map<std::string, std::vector<profiler::time_series_point>> time_series_data_;

    // Configuration
    size_t              max_samples_per_series_ = 10000;
    double              outlier_threshold_      = 2.0;
    std::vector<double> percentiles_            = {25.0, 50.0, 75.0, 90.0, 95.0, 99.0};
    size_t              worker_threads_hint_    = 0;

    // Helper methods
    static std::vector<double> calculate_percentiles(
        std::vector<double> data, const std::vector<double>& percentiles);
    static std::vector<double> detect_outliers(const std::vector<double>& data, double threshold);
    void                       trim_series_if_needed(std::vector<double>& series) const;
    void trim_time_series_if_needed(std::vector<profiler::time_series_point>& series) const;
};

// RAII statistical analysis scope
class PROFILER_VISIBILITY statistical_analysis_scope
{
public:
    PROFILER_API explicit statistical_analysis_scope(
        profiler::statistical_analyzer& analyzer, std::string name);
    PROFILER_API ~statistical_analysis_scope();

    PROFILER_API void add_checkpoint(const std::string& label);
    PROFILER_API profiler::statistical_metrics get_checkpoint_stats() const;

private:
    profiler::statistical_analyzer&                analyzer_;
    std::string                                    name_;
    std::chrono::high_resolution_clock::time_point start_time_;
    std::vector<std::pair<std::string, std::chrono::high_resolution_clock::time_point>>
         checkpoints_;
    bool active_ = true;
};

}  // namespace profiler
