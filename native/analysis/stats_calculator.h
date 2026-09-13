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

/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#pragma once

#include <algorithm>  // for nth_element
#include <cmath>      // for pow, sqrt
#include <cstdint>    // for int64_t
#include <limits>     // for numeric_limits
#include <map>        // for map
#include <new>        // for operator new
#include <sstream>    // for operator<<, basic_ostream
#include <string>     // for char_traits, string
#include <utility>    // for max, min
#include <vector>     // for vector

#include "common/profiler_export.h"
#include "native/analysis/stat_summarizer_options.h"

namespace profiler
{

template <typename ValueType, typename HighPrecisionValueType = double>
class stat
{
public:
    void update_stat(ValueType v)
    {
        if (count_ == 0)
        {
            first_ = v;
        }

        newest_ = v;
        max_    = std::max(v, max_);
        min_    = std::min(v, min_);
        ++count_;
        sum_ += v;
        squared_sum_ += static_cast<HighPrecisionValueType>(v) * v;
    }

    void reset() { new (this) stat<ValueType, HighPrecisionValueType>(); }

    bool empty() const { return count_ == 0; }

    ValueType first() const { return first_; }

    ValueType newest() const { return newest_; }

    ValueType max() const { return max_; }

    ValueType min() const { return min_; }

    int64_t count() const { return count_; }

    ValueType sum() const { return sum_; }

    HighPrecisionValueType squared_sum() const { return squared_sum_; }

    bool all_same() const { return (count_ == 0 || min_ == max_); }

    HighPrecisionValueType avg() const
    {
        return (HighPrecisionValueType)(empty()
                                            ? std::numeric_limits<ValueType>::quiet_NaN()
                                            : static_cast<HighPrecisionValueType>(sum_) / count_);
    }

    // Returns sample variance.
    ValueType sample_variance() const
    {
        return (ValueType)(all_same()
                               ? 0
                               : (squared_sum_ - std::pow(sum_, 2.0) / count_) / (count_ - 1));
    }

    // Returns population variance.
    ValueType variance() const
    {
        return all_same() ? (ValueType)0 : (ValueType)((squared_sum_ / count_) - (avg() * avg()));
    }

    // Returns population stddev.
    ValueType std_deviation() const
    {
        return all_same() ? (ValueType)0 : (ValueType)std::sqrt(variance());
    }

    void output_to_stream(std::ostream* stream) const
    {
        if (empty())
        {
            *stream << "count=0";
        }
        else if (all_same())
        {
            *stream << "count=" << count_ << " curr=" << newest_;
            if (count_ > 1)
                *stream << "(all same)";
        }
        else
        {
            *stream << "count=" << count_ << " first=" << first_ << " curr=" << newest_
                    << " min=" << min_ << " max=" << max_ << " avg=" << avg()
                    << " std=" << std_deviation();
        }
    }

    friend std::ostream& operator<<(std::ostream& stream, const stat<ValueType>& stat)
    {
        stat.output_to_stream(&stream);
        return stream;
    }

private:
    ValueType              first_       = 0;
    ValueType              newest_      = 0;
    ValueType              max_         = std::numeric_limits<ValueType>::min();
    ValueType              min_         = std::numeric_limits<ValueType>::max();
    int64_t                count_       = 0;
    ValueType              sum_         = 0;
    HighPrecisionValueType squared_sum_ = 0;
};

// A `StatWithPercentiles` inherited from `Stat`, also keeps track of the
// values added and can be used to compute the percentile values.
template <typename ValueType, typename HighPrecisionValueType = double>
class stat_with_percentiles : public stat<ValueType, HighPrecisionValueType>
{
public:
    void update_stat(ValueType v)
    {
        stat<ValueType, HighPrecisionValueType>::update_stat(v);
        values_.push_back(v);
    }

    // Returns the percentile value.
    ValueType percentile(int percentile) const
    {
        if (percentile < 0 || percentile > 100 || values_.empty())
        {
            return std::numeric_limits<ValueType>::quiet_NaN();
        }
        std::vector<ValueType> values = values_;
        if (percentile == 100)
        {
            return values[values.size() - 1];
        }
        else
        {
            std::nth_element(
                values.begin(), values.begin() + values.size() * percentile / 100, values.end());
            return values[values.size() * percentile / 100];
        }
    }

    void output_to_stream(std::ostream* stream) const
    {
        stat<ValueType, HighPrecisionValueType>::output_to_stream(stream);
        *stream << " p5=" << percentile(5) << " median=" << percentile(50)
                << " p95=" << percentile(95);
    }

private:
    std::vector<ValueType> values_;
};

// A stats_calculator assists in performance analysis of Graph executions.
//
// It summarizes time spent executing (on GPU/CPU), memory used etc for
// graph execution.
//
// For example usage see stats_summarizer.
class PROFILER_VISIBILITY stats_calculator
{
public:
    enum class sorting_metric_enum
    {
        BY_NAME,
        BY_RUN_ORDER,
        BY_TIME,
        BY_MEMORY,
        BY_TYPE,
    };

    PROFILER_API explicit stats_calculator(const stat_summarizer_options& options);

    // Returns a string detailing the accumulated runtime stats in a tab-separated
    // format which can be pasted into a spreadsheet for further analysis.
    PROFILER_API std::string get_output_string() const;

    PROFILER_API std::string get_short_summary() const;

    PROFILER_API void compute_stats_by_type(
        std::map<std::string, int64_t>* node_type_map_count,
        std::map<std::string, int64_t>* node_type_map_time,
        std::map<std::string, int64_t>* node_type_map_memory,
        std::map<std::string, int64_t>* node_type_map_times_called,
        int64_t*                        accumulated_us) const;

    PROFILER_API std::string get_stats_by_node_type() const;

    PROFILER_API std::string get_stats_by_metric(
        const std::string& title, sorting_metric_enum sorting_metric, int num_stats) const;

    // Returns number of runs.
    int num_runs() const { return static_cast<int>(run_total_us_.count()); }

    // Returns stats of total microseconds spent by all nodes in each run.
    const stat<int64_t>& run_total_us() const { return run_total_us_; }

    void update_run_total_us(int64_t run_total_us) { run_total_us_.update_stat(run_total_us); }

    void update_memory_used(int64_t memory) { memory_.update_stat(memory); }

    struct detail
    {
        std::string   name;
        std::string   type;
        int64_t       run_order;
        stat<int64_t> elapsed_time;
        stat<int64_t> mem_used;
        int64_t       times_called;
    };

    const std::map<std::string, detail>& get_details() const { return details_; }

    PROFILER_API void add_node_stats(
        const std::string& name,
        const std::string& type,
        int64_t            run_order,
        int64_t            elapsed_time,
        int64_t            mem_used);

private:
    void order_nodes_by_metric(
        sorting_metric_enum sorting_metric, std::vector<const detail*>* details) const;

    std::string header_string(const std::string& title) const;
    std::string column_string(
        const detail&        detail,
        const int64_t        cumulative_stat_on_node,
        const stat<int64_t>& stat) const;

    stat<int64_t> run_total_us_;
    stat<int64_t> memory_;

    std::map<std::string, detail> details_;
    stat_summarizer_options       options_;
};

}  // namespace profiler
