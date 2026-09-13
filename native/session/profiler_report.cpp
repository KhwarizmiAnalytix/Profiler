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

#include "profiler_report.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

////#include "logger/logger.h"
#include "native/analysis/hotspot_report.h"
#include "native/analysis/stat_summarizer_options.h"
#include "native/analysis/statistical_analyzer.h"
#include "native/analysis/stats_calculator.h"
#include "native/exporters/xplane/tf_xplane_visitor.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/exporters/xplane/xplane_visitor.h"
#include "native/session/profiler.h"

namespace profiler
{
namespace
{
struct scope_snapshot
{
    const profiler_scope_data* scope;
    size_t                     depth;
};

// Offline tabular summary from the session XSpace (same events Chrome/report use).
// Complements statistical_analyzer (online multi-sample mean/std).
std::string build_xspace_stats_summary(const profiler_session& session)
{
    if (!session.has_collected_xspace())
    {
        return {};
    }

    const xplane* host = find_plane_with_name(session.collected_xspace(), kHostThreadsPlaneName);
    if (host == nullptr)
    {
        return {};
    }

    stats_calculator            calc(stat_summarizer_options{});
    int64_t                     run_order    = 0;
    int64_t                     run_total_us = 0;
    const statistical_analyzer* analyzer     = session.statistical_analyzer_ptr();

    xplane_visitor const visitor = CreateTfXPlaneVisitor(host);
    visitor.for_each_line(
        [&](const xline_visitor& line)
        {
            line.for_each_event(
                [&](const xevent_visitor& event)
                {
                    const std::string name = std::string(event.name());
                    if (name.empty())
                    {
                        return;
                    }

                    // Schema-typed host events keep their name as the node type;
                    // ordinary TraceMe scopes are labeled "Scope".
                    const std::string type = event.type().has_value() ? name : std::string("Scope");

                    const auto elapsed_us = static_cast<int64_t>(event.duration_ps() / 1000000);
                    if (elapsed_us < 0)
                    {
                        return;
                    }

                    int64_t mem_used = 0;
                    if (auto bytes =
                            event.get_stat(static_cast<int64_t>(StatType::kRequestedBytes));
                        bytes.has_value())
                    {
                        mem_used = bytes->int_or_uint_value();
                    }
                    else if (analyzer != nullptr)
                    {
                        const auto mem_stats = analyzer->calculate_memory_stats(name);
                        if (mem_stats.is_valid())
                        {
                            mem_used = static_cast<int64_t>(mem_stats.mean);
                        }
                    }

                    calc.add_node_stats(name, type, run_order++, elapsed_us, mem_used);
                    run_total_us += elapsed_us;
                });
        });

    if (run_order == 0)
    {
        return {};
    }

    calc.update_run_total_us(run_total_us);
    return calc.get_output_string();
}

void collect_scope_snapshots(
    const profiler_scope_data* scope, size_t depth, std::vector<scope_snapshot>& snapshots)
{
    if (scope == nullptr)
    {
        return;
    }

    snapshots.push_back({scope, depth});
    for (const auto& child : scope->children_)
    {
        collect_scope_snapshots(child.get(), depth + 1, snapshots);
    }
}

std::vector<scope_snapshot> collect_scope_snapshots(const profiler_scope_data* root)
{
    std::vector<scope_snapshot> snapshots;
    collect_scope_snapshots(root, 0, snapshots);
    return snapshots;
}

size_t compute_max_depth(const std::vector<scope_snapshot>& snapshots)
{
    // Use std::accumulate to find maximum depth
    return std::accumulate(
        snapshots.begin(),
        snapshots.end(),
        size_t{0},
        [](size_t max_depth, const scope_snapshot& snapshot)
        { return (std::max)(max_depth, snapshot.depth); });
}

// Per-thread scope counts, keyed by thread display name (see xline_thread_label()). Reads XSpace's
// XLines directly rather than profiler_scope_data::thread_id_: XSpace has no std::thread::id to
// reconstruct, only the numeric line id/name host_tracer captured.
std::unordered_map<std::string, size_t> build_thread_histogram(const x_space& space)
{
    std::unordered_map<std::string, size_t> histogram;
    for (const auto& plane : space.planes())
    {
        if (plane.name() != kHostThreadsPlaneName)
        {
            continue;
        }
        for (const auto& line : plane.lines())
        {
            std::string const thread_label = xline_thread_label(line);
            for (const auto& event : line.events())
            {
                if (event.data_case() != xevent::data_case_type::kNumOccurrences)
                {
                    ++histogram[thread_label];
                }
            }
        }
    }
    return histogram;
}

template <typename ValueT>
std::vector<std::pair<std::string, ValueT>> sort_map_by_value_desc(
    const std::unordered_map<std::string, ValueT>& input)
{
    std::vector<std::pair<std::string, ValueT>> entries(input.begin(), input.end());
    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& lhs, const auto& rhs)
        {
            if (lhs.second == rhs.second)
            {
                return lhs.first < rhs.first;
            }
            return lhs.second > rhs.second;
        });
    return entries;
}

// Per-scope-name memory delta stats, aggregated across every recorded invocation of that name.
// Individual scope-tree nodes (reconstructed from XSpace by scope_tree_builder) carry no memory
// data of their own -- XSpace events have no memory field -- so this analyzer-by-name aggregate,
// keyed on profiler_scope_data::name_, is the closest available substitute; see the comment on
// profiler_scope::stop()'s call to add_memory_sample() for where these samples come from. Samples
// are stored as an absolute-value magnitude, so mean/max here are magnitudes, not signed deltas.
profiler::statistical_metrics scope_memory_stats(
    const profiler::profiler_session& session, const std::string& scope_name)
{
    auto const* analyzer = session.statistical_analyzer_ptr();
    if (analyzer == nullptr)
    {
        return {};
    }
    return analyzer->calculate_memory_stats(scope_name);
}

}  // namespace

//=============================================================================
// profiler_report Implementation
//=============================================================================

profiler_report::profiler_report(const profiler::profiler_session& session) : session_(session) {}

std::string profiler_report::generate_console_report() const
{
    std::stringstream ss;

    ss << generate_header_section();
    ss << generate_summary_section();

    if (include_hierarchical_data_)
    {
        ss << generate_hierarchical_section();
    }

    ss << generate_timing_section();
    ss << generate_memory_section();
    ss << generate_statistical_section();
    ss << generate_hotspot_section();

    if (include_thread_info_)
    {
        ss << generate_thread_section();
    }

    return ss.str();
}

std::string profiler_report::generate_json_report() const
{
    auto const* root = session_.build_scope_tree();
    auto const  snapshots =
        root != nullptr ? collect_scope_snapshots(root) : std::vector<scope_snapshot>();

    std::stringstream ss;
    ss << "{\n";
    ss << "  \"header\": {\n";
    ss << "    \"active\": " << (session_.is_active() ? "true" : "false") << ",\n";
    ss << "    \"scope_count\": " << snapshots.size() << ",\n";
    ss << "    \"max_depth\": " << compute_max_depth(snapshots) << ",\n";

    auto const start_time = session_.session_start_time();
    auto const end_time   = session_.session_end_time();
    if (end_time > start_time)
    {
        auto const duration_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        ss << "    \"duration_ms\": " << format_double(duration_ns / 1'000'000.0) << "\n";
    }
    else
    {
        ss << "    \"duration_ms\": 0.0\n";
    }
    ss << "  },\n";

    if (include_hierarchical_data_ && (root != nullptr))
    {
        ss << "  \"scopes\": [\n";
        process_scope_data_json_recursive(*root, ss, 4);
        ss << "\n  ],\n";
    }
    else
    {
        ss << "  \"scopes\": [],\n";
    }

    std::vector<scope_snapshot> by_duration = snapshots;
    std::sort(
        by_duration.begin(),
        by_duration.end(),
        [](const auto& lhs, const auto& rhs)
        { return lhs.scope->get_duration_ms() > rhs.scope->get_duration_ms(); });

    ss << "  \"top_durations\": [\n";
    size_t const duration_limit =
        (std::min)(static_cast<size_t>(10), by_duration.size());  // top 10 entries
    for (size_t i = 0; i < duration_limit; ++i)
    {
        const auto& snapshot = by_duration[i];
        const auto* scope    = snapshot.scope;
        ss << "    {\n";
        ss << "      \"name\": " << escape_json_string(scope->name_) << ",\n";
        ss << "      \"duration_ms\": " << format_double(scope->get_duration_ms()) << ",\n";
        ss << "      \"depth\": " << snapshot.depth << ",\n";
        ss << "      \"thread\": " << escape_json_string(format_thread_label(scope->thread_label_))
           << "\n";
        ss << "    }";
        if (i + 1 < duration_limit)
        {
            ss << ",\n";
        }
    }
    if (duration_limit > 0)
    {
        ss << "\n";
    }
    ss << "  ],\n";

    ss << "  \"memory\": {\n";
    if (auto const* tracker = session_.memory_tracker_ptr())
    {
        auto const stats = tracker->get_current_stats();
        ss << "    \"current_bytes\": " << stats.current_usage_ << ",\n";
        ss << "    \"peak_bytes\": " << stats.peak_usage_ << ",\n";
        ss << "    \"total_allocated_bytes\": " << stats.total_allocated_ << ",\n";
        ss << "    \"total_deallocated_bytes\": " << stats.total_deallocated_ << "\n";
    }
    else
    {
        ss << "    \"enabled\": false\n";
    }
    ss << "  },\n";

    ss << "  \"threads\": [\n";
    auto const thread_histogram =
        sort_map_by_value_desc(build_thread_histogram(session_.collected_xspace()));
    for (size_t i = 0; i < thread_histogram.size(); ++i)
    {
        if (i != 0)
        {
            ss << ",\n";
        }
        ss << "    {\n";
        ss << "      \"thread\": " << escape_json_string(thread_histogram[i].first) << ",\n";
        ss << "      \"scope_count\": " << thread_histogram[i].second << "\n";
        ss << "    }";
    }
    if (!thread_histogram.empty())
    {
        ss << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

std::string profiler_report::generate_csv_report() const
{
    std::stringstream ss;

    ss << generate_csv_header() << "\n";
    auto const* root = session_.build_scope_tree();
    if (include_hierarchical_data_ && (root != nullptr))
    {
        std::vector<std::string> rows;
        process_scope_data_csv_recursive(*root, rows);
        for (const auto& row : rows)
        {
            ss << row << "\n";
        }
    }

    return ss.str();
}

std::string profiler_report::generate_xml_report() const
{
    std::stringstream ss;

    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<profiler_report>\n";

    ss << "  <header>\n" << generate_header_section() << "  </header>\n";
    ss << "  <summary>\n" << generate_summary_section() << "  </summary>\n";

    if (include_hierarchical_data_)
    {
        ss << "  <hierarchical_data>\n"
           << generate_hierarchical_section() << "  </hierarchical_data>\n";
    }

    ss << "  <timing>\n" << generate_timing_section() << "  </timing>\n";
    ss << "  <memory>\n" << generate_memory_section() << "  </memory>\n";
    ss << "  <statistics>\n" << generate_statistical_section() << "  </statistics>\n";
    ss << "  <hotspots>\n" << generate_hotspot_section() << "  </hotspots>\n";

    if (include_thread_info_)
    {
        ss << "  <threads>\n" << generate_thread_section() << "  </threads>\n";
    }

    ss << "</profiler_report>\n";

    return ss.str();
}

bool profiler_report::export_to_file(
    const std::string& filename, profiler::profiler_options::output_format_enum format) const
{
    std::string content;

    switch (format)
    {
    case profiler::profiler_options::output_format_enum::CONSOLE:
    case profiler::profiler_options::output_format_enum::FILE:
        content = generate_console_report();
        break;
    case profiler::profiler_options::output_format_enum::JSON:
        content = generate_json_report();
        break;
    case profiler::profiler_options::output_format_enum::CSV:
        content = generate_csv_report();
        break;
    case profiler::profiler_options::output_format_enum::STRUCTURED:
        content = generate_xml_report();
        break;
    default:
        content = generate_console_report();
        break;
    }

    std::ofstream file(filename);
    if (!file.is_open())
    {
        return false;
    }

    file << content;
    file.close();
    return true;
}

bool profiler_report::export_console_report(const std::string& filename) const
{
    return export_to_file(filename, profiler::profiler_options::output_format_enum::CONSOLE);
}

bool profiler_report::export_json_report(const std::string& filename) const
{
    return export_to_file(filename, profiler::profiler_options::output_format_enum::JSON);
}

bool profiler_report::export_csv_report(const std::string& filename) const
{
    return export_to_file(filename, profiler::profiler_options::output_format_enum::CSV);
}

bool profiler_report::export_xml_report(const std::string& filename) const
{
    return export_to_file(filename, profiler::profiler_options::output_format_enum::STRUCTURED);
}

void profiler_report::print_summary()
{
    /* PROFILER_LOG_WARNING(
        "profiler_report::print_summary() requires a report instance. "
        "Create a profiler_session report via profiler_session::generate_report()."); */
}

void profiler_report::print_detailed_report() const
{
    //PROFILER_LOG_INFO("{}", generate_console_report());
}

void profiler_report::print_memory_report()
{
    /* PROFILER_LOG_WARNING(
        "profiler_report::print_memory_report() is deprecated. "
        "Use profiler_session::generate_report()->generate_memory_section()."); */
}

void profiler_report::print_timing_report()
{
    /* PROFILER_LOG_WARNING(
        "profiler_report::print_timing_report() is deprecated. "
        "Use profiler_session::generate_report()->generate_timing_section()."); */
}

void profiler_report::print_statistical_report()
{
    /* PROFILER_LOG_WARNING(
        "profiler_report::print_statistical_report() is deprecated. "
        "Use profiler_session::generate_report()->generate_statistical_section()."); */
}

std::string profiler_report::format_duration(double duration_ns) const
{
    if (time_unit_ == "ms")
    {
        return format_double(duration_ns / 1'000'000.0) + " ms";
    }
    if (time_unit_ == "us")
    {
        return format_double(duration_ns / 1'000.0) + " us";
    }
    return format_double(duration_ns) + " ns";
}

std::string profiler_report::format_memory_size(size_t bytes) const
{
    if (memory_unit_ == "MB")
    {
        return format_double(bytes / (1024.0 * 1024.0)) + " MB";
    }
    if (memory_unit_ == "KB")
    {
        return format_double(bytes / 1024.0) + " KB";
    }
    return format_double(static_cast<double>(bytes)) + " bytes";
}

std::string profiler_report::format_percentage(double value)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << value * 100.0 << "%";
    return ss.str();
}

std::string profiler_report::format_thread_label(const std::string& thread_label)
{
    return thread_label.empty() ? "n/a" : thread_label;
}

std::string profiler_report::format_double(double value) const
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision_) << value;
    return ss.str();
}

std::string profiler_report::generate_header_section() const
{
    std::stringstream ss;
    ss << "=== Profiler Profiler Report ===\n";
    ss << "Session active: " << (session_.is_active() ? "yes" : "no") << "\n";

    auto const start_time = session_.session_start_time();
    auto const end_time   = session_.session_end_time();
    if (end_time > start_time)
    {
        auto const duration_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        ss << "Duration: " << format_duration(static_cast<double>(duration_ns)) << "\n";
    }
    else
    {
        ss << "Duration: n/a\n";
    }

    auto const* root = session_.build_scope_tree();
    auto const  snapshots =
        root != nullptr ? collect_scope_snapshots(root) : std::vector<scope_snapshot>();
    ss << "Total scopes: " << snapshots.size() << "\n";
    ss << "Max depth: " << compute_max_depth(snapshots) << "\n";
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_summary_section() const
{
    std::stringstream ss;
    ss << "=== Summary ===\n";
    auto const* root = session_.build_scope_tree();
    if (root == nullptr)
    {
        ss << "No profiling scopes were recorded.\n\n";
        return ss.str();
    }

    ss << "Root scope: " << root->name_ << "\n";
    ss << "Total duration: " << format_double(root->get_duration_ms()) << " ms\n";
    // root is a synthetic node aggregating every recorded thread's scopes (see
    // scope_tree_builder::build_scope_tree()), so it has no single originating thread to report --
    // report the thread count instead; generate_thread_section() has the per-thread breakdown.
    ss << "Threads involved: " << build_thread_histogram(session_.collected_xspace()).size()
       << "\n";

    if (auto const* tracker = session_.memory_tracker_ptr())
    {
        auto const stats = tracker->get_current_stats();
        ss << "Current memory: " << format_memory_size(stats.current_usage_) << "\n";
        ss << "Peak memory: " << format_memory_size(stats.peak_usage_) << "\n";
        ss << "Total allocated: " << format_memory_size(stats.total_allocated_) << "\n";
        ss << "Total deallocated: " << format_memory_size(stats.total_deallocated_) << "\n";
    }
    else
    {
        ss << "Memory tracking disabled for this session.\n";
    }
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_timing_section() const
{
    std::stringstream ss;
    ss << "=== Timing Analysis ===\n";
    auto const* root = session_.build_scope_tree();
    if (root == nullptr)
    {
        ss << "No timing data available.\n\n";
        return ss.str();
    }

    auto snapshots = collect_scope_snapshots(root);
    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const auto& lhs, const auto& rhs)
        { return lhs.scope->get_duration_ms() > rhs.scope->get_duration_ms(); });

    size_t rank = 1;
    for (const auto& snapshot : snapshots)
    {
        const auto* scope = snapshot.scope;
        ss << "#" << rank++ << " ";
        ss << scope->name_ << " - " << format_double(scope->get_duration_ms()) << " ms"
           << " (depth " << snapshot.depth << ", thread "
           << format_thread_label(scope->thread_label_) << ")\n";

        if (rank > 10)
        {
            break;
        }
    }
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_memory_section() const
{
    std::stringstream ss;
    ss << "=== Memory Analysis ===\n";

    // Per-scope memory deltas are recorded by name into the statistical analyzer (see
    // profiler_scope::stop()), not carried on the XSpace-reconstructed scope tree -- XSpace
    // events have no memory field, so there's nothing for scope_tree_builder to recover.
    auto const* analyzer = session_.statistical_analyzer_ptr();
    if (analyzer == nullptr)
    {
        ss << "Memory tracking disabled for this session.\n\n";
        return ss.str();
    }

    auto const memory_metrics = analyzer->calculate_all_memory_stats();
    std::vector<std::pair<std::string, profiler::statistical_metrics>> entries(
        memory_metrics.begin(), memory_metrics.end());
    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.second.mean > rhs.second.mean; });

    size_t displayed = 0;
    for (const auto& [name, metrics] : entries)
    {
        if (!metrics.is_valid() || metrics.mean == 0.0)
        {
            continue;
        }
        ss << name << ": mean delta " << format_memory_size(static_cast<size_t>(metrics.mean))
           << " over " << metrics.count << " scope(s), max "
           << format_memory_size(static_cast<size_t>(metrics.max_value)) << "\n";
        if (++displayed >= 10)
        {
            break;
        }
    }
    if (displayed == 0)
    {
        ss << "No significant memory deltas observed.\n";
    }
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_hierarchical_section() const
{
    std::stringstream ss;
    ss << "=== Hierarchical Analysis ===\n";
    auto const* root = session_.build_scope_tree();
    if (root == nullptr)
    {
        ss << "No scope hierarchy available.\n\n";
        return ss.str();
    }

    process_scope_data_recursive(*root, ss, 0);
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_statistical_section() const
{
    std::stringstream ss;
    ss << "=== Statistical Analysis ===\n";
    auto const* analyzer = session_.statistical_analyzer_ptr();
    if (analyzer == nullptr)
    {
        ss << "Statistical analysis disabled for this session.\n";
    }
    else
    {
        auto const timing_metrics = analyzer->calculate_all_timing_stats();
        if (timing_metrics.empty())
        {
            ss << "No timing statistics recorded.\n";
        }
        else
        {
            size_t count = 0;
            for (const auto& entry : timing_metrics)
            {
                auto const& metrics = entry.second;
                if (!metrics.is_valid())
                {
                    continue;
                }
                ss << entry.first << ": mean " << format_double(metrics.mean) << " ms, ";
                ss << "std-dev " << format_double(metrics.std_deviation) << " ms, ";
                ss << "count " << metrics.count << "\n";
                if (++count >= 10)
                {
                    break;
                }
            }
        }
    }

    // Tabular per-event summary from collected_xspace via stats_calculator.
    const std::string xspace_stats = build_xspace_stats_summary(session_);
    if (!xspace_stats.empty())
    {
        ss << "\n=== Node Stats (from XSpace) ===\n";
        ss << xspace_stats;
    }
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_hotspot_section() const
{
    std::stringstream ss;
    ss << "=== Hotspots ===\n";

    const hotspot_report report(session_.build_scope_tree());
    if (report.hotspots().empty())
    {
        ss << "No hotspot data available (no nested scopes in collected XSpace).\n\n";
        return ss.str();
    }

    ss << "--- Operator table (self CPU) ---\n";
    ss << report.table();
    ss << "\n--- Top-down call tree ---\n";
    ss << report.top_down_tree();
    ss << "\n--- Bottom-up hotspots ---\n";
    ss << report.bottom_up_hotspots(/*max_rows=*/20);
    ss << "\n";
    return ss.str();
}

std::string profiler_report::generate_thread_section() const
{
    std::stringstream ss;
    ss << "=== Thread Analysis ===\n";

    auto const histogram =
        sort_map_by_value_desc(build_thread_histogram(session_.collected_xspace()));
    size_t rank = 1;
    for (const auto& entry : histogram)
    {
        ss << "#" << rank++ << " " << entry.first << ": " << entry.second << " scope(s)\n";
        if (rank > 10)
        {
            break;
        }
    }
    if (histogram.empty())
    {
        ss << "No scopes were recorded per-thread.\n";
    }
    ss << "\n";
    return ss.str();
}

std::string profiler_report::escape_json_string(const std::string& str)
{
    std::ostringstream ss;
    ss << '"';
    for (char const c : str)
    {
        switch (c)
        {
        case '"':
        case '\\':
            ss << '\\' << c;
            break;
        case '\b':
            ss << "\\b";
            break;
        case '\f':
            ss << "\\f";
            break;
        case '\n':
            ss << "\\n";
            break;
        case '\r':
            ss << "\\r";
            break;
        case '\t':
            ss << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                   << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
            }
            else
            {
                ss << c;
            }
        }
    }
    ss << '"';
    return ss.str();
}

std::string profiler_report::escape_csv_field(const std::string& field)
{
    bool const requires_quotes = field.find_first_of(",\"\n") != std::string::npos;
    if (!requires_quotes)
    {
        return field;
    }
    std::string escaped = "\"";
    for (char const c : field)
    {
        if (c == '"')
        {
            escaped += "\"\"";
        }
        else
        {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

std::string profiler_report::generate_csv_header()
{
    // "Memory Delta" columns are per-scope-name aggregates from the statistical analyzer (see
    // scope_memory_stats()), not a live per-instance reading -- see process_scope_data_csv_recursive().
    return "Scope,Depth,Thread,Duration(ms),Memory Delta Mean,Memory Delta Max";
}

std::string profiler_report::generate_csv_row(const std::vector<std::string>& fields)
{
    std::string result;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        if (i != 0)
        {
            result += ",";
        }
        result += escape_csv_field(fields[i]);
    }
    return result;
}

std::string profiler_report::escape_xml_string(const std::string& str)
{
    std::string result;
    result.reserve(str.size());
    for (char const c : str)
    {
        switch (c)
        {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&apos;";
            break;
        default:
            result += c;
        }
    }
    return result;
}

std::string profiler_report::generate_xml_element(
    const std::string& tag, const std::string& content)
{
    return "<" + tag + ">" + content + "</" + tag + ">";
}

std::string profiler_report::generate_xml_attribute(
    const std::string& name, const std::string& value)
{
    return name + "=\"" + escape_xml_string(value) + "\"";
}

void profiler_report::process_scope_data_recursive(
    const profiler_scope_data& scope, std::stringstream& ss, int indent) const
{
    std::string const prefix(static_cast<size_t>(indent) * 2, ' ');
    auto const        memory = scope_memory_stats(session_, scope.name_);
    ss << prefix << "- " << scope.name_ << " | duration " << format_double(scope.get_duration_ms())
       << " ms" << " | thread " << format_thread_label(scope.thread_label_) << " | memory ";
    if (memory.is_valid())
    {
        ss << "avg " << format_memory_size(static_cast<size_t>(memory.mean));
    }
    else
    {
        ss << "n/a";
    }
    ss << "\n";

    for (const auto& child : scope.children_)
    {
        process_scope_data_recursive(*child, ss, indent + 1);
    }
}

void profiler_report::process_scope_data_json_recursive(
    const profiler_scope_data& scope, std::stringstream& ss, int indent) const
{
    std::string const indent_str(static_cast<size_t>(indent), ' ');
    auto const        memory = scope_memory_stats(session_, scope.name_);
    ss << indent_str << "{\n";
    ss << indent_str << "  \"name\": " << escape_json_string(scope.name_) << ",\n";
    ss << indent_str << "  \"duration_ms\": " << format_double(scope.get_duration_ms()) << ",\n";
    ss << indent_str
       << "  \"thread\": " << escape_json_string(format_thread_label(scope.thread_label_)) << ",\n";
    ss << indent_str << "  \"memory\": {\n";
    if (memory.is_valid())
    {
        ss << indent_str << "    \"delta_mean_bytes\": " << format_double(memory.mean) << ",\n";
        ss << indent_str << "    \"delta_max_bytes\": " << format_double(memory.max_value) << "\n";
    }
    else
    {
        ss << indent_str << "    \"delta_mean_bytes\": null,\n";
        ss << indent_str << "    \"delta_max_bytes\": null\n";
    }
    ss << indent_str << "  }";

    if (!scope.children_.empty())
    {
        ss << ",\n" << indent_str << "  \"children\": [\n";
        for (size_t i = 0; i < scope.children_.size(); ++i)
        {
            process_scope_data_json_recursive(*scope.children_[i], ss, indent + 4);
            if (i + 1 < scope.children_.size())
            {
                ss << ",\n";
            }
        }
        ss << "\n" << indent_str << "  ]\n" << indent_str << "}";
    }
    else
    {
        ss << "\n" << indent_str << "}";
    }
}

void profiler_report::process_scope_data_csv_recursive(
    const profiler_scope_data& scope, std::vector<std::string>& rows, int depth) const
{
    auto const memory = scope_memory_stats(session_, scope.name_);
    rows.push_back(generate_csv_row({
        std::string(static_cast<size_t>(depth) * 2, ' ') + scope.name_,
        std::to_string(depth),
        format_thread_label(scope.thread_label_),
        format_double(scope.get_duration_ms()),
        memory.is_valid() ? format_memory_size(static_cast<size_t>(memory.mean)) : "n/a",
        memory.is_valid() ? format_memory_size(static_cast<size_t>(memory.max_value)) : "n/a",
    }));

    for (const auto& child : scope.children_)
    {
        process_scope_data_csv_recursive(*child, rows, depth + 1);
    }
}

//=============================================================================
// profiler_report_builder Implementation
//=============================================================================

profiler_report_builder::profiler_report_builder(const profiler::profiler_session& session)
    : session_(session)
{
}

std::unique_ptr<profiler::profiler_report> profiler_report_builder::build() const
{
    auto report = std::make_unique<profiler::profiler_report>(session_);
    report->set_precision(precision_);
    report->set_time_unit(time_unit_);
    report->set_memory_unit(memory_unit_);
    report->set_include_thread_info(include_thread_info_);
    report->set_include_hierarchical_data(include_hierarchical_data_);
    return report;
}

}  // namespace profiler
