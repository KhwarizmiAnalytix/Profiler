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

#include "native/analysis/stats_calculator.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>   // for operator<<, setprecision, setw
#include <iostream>  // for endl
#include <map>       // for map, _Tree_iterator, _Tree_const_iterator, _Tree_simple_types
#include <queue>     // for priority_queue
#include <sstream>   // for basic_ostream, operator<<, stringstream, fixed, ostream, right
#include <string>    // for char_traits, operator<<, operator<, string, allocator, basic_string
#include <utility>
#include <vector>

#include "native/analysis/stat_summarizer_options.h"

namespace profiler
{

constexpr int kNodeTypeWidth = 40;

stats_calculator::stats_calculator(const stat_summarizer_options& options) : options_(options) {}

std::string stats_calculator::get_short_summary() const
{
    std::stringstream stream;
    stream << "Timings (microseconds): ";
    run_total_us_.output_to_stream(&stream);
    stream << std::endl;

    stream << "Memory (bytes): ";
    memory_.output_to_stream(&stream);
    stream << std::endl;

    stream << details_.size() << " nodes observed" << std::endl;
    return stream.str();
}

static std::ostream& init_field(std::ostream& stream, int width)
{
    stream << "\t" << std::right << std::setw(width) << std::fixed << std::setprecision(3);
    return stream;
}

std::string stats_calculator::header_string(const std::string& title) const
{
    std::stringstream stream;

    stream << "============================== " << title
           << " ==============================" << std::endl;
    if (options_.format_as_csv)
    {
        stream << "node type, first, avg_ms, %, cdf%, mem KB, times called, "
                  "name";
    }
    else
    {
        init_field(stream, kNodeTypeWidth) << "[node type]";
        init_field(stream, 9) << "[first]";
        init_field(stream, 9) << "[avg ms]";
        init_field(stream, 8) << "[%]";
        init_field(stream, 8) << "[cdf%]";
        init_field(stream, 10) << "[mem KB]";
        init_field(stream, 9) << "[times called]";
        stream << "\t" << "[Name]";
    }
    return stream.str();
}

std::string stats_calculator::column_string(
    const detail& detail, const int64_t cumulative_stat_on_node, const stat<int64_t>& stat) const
{
    const double  first_time_ms  = detail.elapsed_time.first() / 1000.0;
    const double  avg_time_ms    = detail.elapsed_time.avg() / 1000.0;
    const double  percentage     = detail.elapsed_time.sum() * 100.0 / stat.sum();
    const double  cdf_percentage = (cumulative_stat_on_node * 100.0F) / stat.sum();
    const int64_t times_called   = detail.times_called / num_runs();

    std::stringstream stream;
    if (options_.format_as_csv)
    {
        std::string name(detail.name);
        std::replace(name.begin(), name.end(), ',', '\t');
        stream << detail.type << ", " << first_time_ms << ", " << avg_time_ms << ", " << percentage
               << "%, " << cdf_percentage << "%, " << detail.mem_used.newest() / 1000.0 << ", "
               << times_called << ", " << name;
    }
    else
    {
        init_field(stream, kNodeTypeWidth) << detail.type;
        init_field(stream, 9) << first_time_ms;
        init_field(stream, 9) << avg_time_ms;
        init_field(stream, 7) << percentage << "%";
        init_field(stream, 7) << cdf_percentage << "%";
        init_field(stream, 10) << detail.mem_used.newest() / 1000.0;
        init_field(stream, 9) << times_called;
        stream << "\t" << detail.name;
    }

    return stream.str();
}

void stats_calculator::order_nodes_by_metric(
    sorting_metric_enum metric, std::vector<const detail*>* details) const
{
    std::priority_queue<std::pair<std::string, const detail*>> sorted_list;

    const auto num_nodes = details_.size();

    for (const auto& det : details_)
    {
        const detail*     detail_ptr = &(det.second);
        std::stringstream stream;
        stream << std::setw(20) << std::right << std::setprecision(10) << std::fixed;

        switch (metric)
        {
        case sorting_metric_enum::BY_NAME:
            stream << detail_ptr->name;
            break;
        case sorting_metric_enum::BY_RUN_ORDER:
            stream << num_nodes - detail_ptr->run_order;
            break;
        case sorting_metric_enum::BY_TIME:
            stream << detail_ptr->elapsed_time.avg();
            break;
        case sorting_metric_enum::BY_MEMORY:
            stream << detail_ptr->mem_used.avg();
            break;
        case sorting_metric_enum::BY_TYPE:
            stream << detail_ptr->type;
            break;
        default:
            stream << "";
            break;
        }

        sorted_list.emplace(stream.str(), detail_ptr);
    }

    while (!sorted_list.empty())
    {
        auto entry = sorted_list.top();
        sorted_list.pop();
        details->push_back(entry.second);
    }
}

void stats_calculator::compute_stats_by_type(
    std::map<std::string, int64_t>* node_type_map_count,
    std::map<std::string, int64_t>* node_type_map_time,
    std::map<std::string, int64_t>* node_type_map_memory,
    std::map<std::string, int64_t>* node_type_map_times_called,
    int64_t*                        accumulated_us) const
{
    int64_t const run_count = run_total_us_.count();

    for (const auto& det : details_)
    {
        //const std::string node_name = det.first;
        const detail& detail = det.second;

        auto const curr_time_val = static_cast<int64_t>(detail.elapsed_time.sum() / run_count);
        *accumulated_us += curr_time_val;

        int64_t const curr_memory_val = detail.mem_used.newest();

        const std::string& node_type = detail.type;

        (*node_type_map_count)[node_type] += 1;
        (*node_type_map_time)[node_type] += curr_time_val;
        (*node_type_map_memory)[node_type] += curr_memory_val;
        (*node_type_map_times_called)[node_type] += detail.times_called / run_count;
    }
}

std::string stats_calculator::get_stats_by_node_type() const
{
    std::stringstream stream;

    stream << "Number of nodes executed: " << details_.size() << std::endl;

    stream << "============================== Summary by node type "
              "=============================="
           << std::endl;

    std::map<std::string, int64_t> node_type_map_count;
    std::map<std::string, int64_t> node_type_map_time;
    std::map<std::string, int64_t> node_type_map_memory;
    std::map<std::string, int64_t> node_type_map_times_called;
    int64_t                        accumulated_us = 0;

    compute_stats_by_type(
        &node_type_map_count,
        &node_type_map_time,
        &node_type_map_memory,
        &node_type_map_times_called,
        &accumulated_us);

    // Sort them.
    std::priority_queue<std::pair<int64_t, std::pair<std::string, int64_t>>> timings;
    for (const auto& node_type : node_type_map_time)
    {
        const int64_t mem_used = node_type_map_memory[node_type.first];
        timings.emplace(
            node_type.second, std::pair<std::string, int64_t>(node_type.first, mem_used));
    }

    if (options_.format_as_csv)
    {
        stream << "node type, count, avg_ms, avg %, cdf %, mem KB, times called\n";
    }
    else
    {
        init_field(stream, kNodeTypeWidth) << "[Node type]";
        init_field(stream, 9) << "[count]";
        init_field(stream, 10) << "[avg ms]";
        init_field(stream, 11) << "[avg %]";
        init_field(stream, 11) << "[cdf %]";
        init_field(stream, 10) << "[mem KB]";
        init_field(stream, 10) << "[times called]";
        stream << std::endl;
    }

    float cdf = 0.0F;
    while (!timings.empty())
    {
        auto entry = timings.top();
        timings.pop();

        const std::string node_type = entry.second.first;
        const float       memory    = entry.second.second / 1000.0F;

        const int64_t node_type_total_us = entry.first;
        const float   time_per_run_ms    = node_type_total_us / 1000.0F;

        const float percentage = ((entry.first / static_cast<float>(accumulated_us)) * 100.0F);
        cdf += percentage;

        if (options_.format_as_csv)
        {
            stream << node_type << ", " << node_type_map_count[node_type] << ", " << time_per_run_ms
                   << ", " << percentage << "%, " << cdf << "%, " << memory << ", "
                   << node_type_map_times_called[node_type] << std::endl;
        }
        else
        {
            init_field(stream, kNodeTypeWidth) << node_type;
            init_field(stream, 9) << node_type_map_count[node_type];
            init_field(stream, 10) << time_per_run_ms;
            init_field(stream, 10) << percentage << "%";
            init_field(stream, 10) << cdf << "%";
            init_field(stream, 10) << memory;
            init_field(stream, 9) << node_type_map_times_called[node_type];
            stream << std::endl;
        }
    }
    stream << std::endl;
    return stream.str();
}

std::string stats_calculator::get_stats_by_metric(
    const std::string& title, sorting_metric_enum sorting_metric, int num_stats) const
{
    std::vector<const detail*> details;
    order_nodes_by_metric(sorting_metric, &details);

    double cumulative_stat_on_node = 0;

    std::stringstream stream;
    stream << header_string(title) << std::endl;
    int stat_num = 0;
    for (const auto* detail : details)
    {
        ++stat_num;
        if (num_stats > 0 && stat_num > num_stats)
        {
            break;
        }

        // TODO(andrewharp): Make this keep track of the particular metric for cdf.
        cumulative_stat_on_node += (double)detail->elapsed_time.sum();
        stream << column_string(*detail, (int64_t)cumulative_stat_on_node, run_total_us_)
               << std::endl;
    }
    stream << std::endl;
    return stream.str();
}

std::string stats_calculator::get_output_string() const
{
    std::stringstream stream;
    if (options_.show_run_order)
    {
        stream << get_stats_by_metric(
            "Run Order", sorting_metric_enum::BY_RUN_ORDER, options_.run_order_limit);
    }
    if (options_.show_time)
    {
        stream << get_stats_by_metric(
            "Top by Computation Time", sorting_metric_enum::BY_TIME, options_.time_limit);
    }
    if (options_.show_memory)
    {
        stream << get_stats_by_metric(
            "Top by Memory Use", sorting_metric_enum::BY_MEMORY, options_.memory_limit);
    }
    if (options_.show_type)
    {
        stream << get_stats_by_node_type();
    }
    if (options_.show_summary)
    {
        stream << get_short_summary() << std::endl;
    }
    return stream.str();
}

void stats_calculator::add_node_stats(
    const std::string& name,
    const std::string& type,
    int64_t            run_order,
    int64_t            elapsed_time,
    int64_t            mem_used)
{
    detail* detail_ptr = nullptr;
#if __cplusplus >= 202002L
    // C++20: use contains()
    if (!details_.contains(name))
#else
    // C++17: use find() for compatibility
    if (details_.find(name) == details_.end())
#endif
    {
        details_.insert({name, {}});
        detail_ptr            = &details_.at(name);
        detail_ptr->type      = type;
        detail_ptr->name      = name;
        detail_ptr->run_order = run_order;
    }
    else
    {
        detail_ptr = &details_.at(name);
    }
    detail_ptr->elapsed_time.update_stat(elapsed_time);
    detail_ptr->mem_used.update_stat(mem_used);
    detail_ptr->times_called++;
}
}  // namespace profiler
