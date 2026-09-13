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

#include "native/analysis/hotspot_report.h"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string_view>

#include "native/session/profiler.h"

namespace profiler
{
namespace
{

constexpr const char* kSyntheticRootName = "ROOT";

uint64_t node_total_ns(const profiler_scope_data& node)
{
    const double duration = node.get_duration_ns();
    return duration > 0.0 ? static_cast<uint64_t>(duration) : 0;
}

uint64_t children_total_ns(const profiler_scope_data& node)
{
    return std::accumulate(
        node.children_.begin(),
        node.children_.end(),
        uint64_t{0},
        [](uint64_t sum, const std::unique_ptr<profiler_scope_data>& child)
        {
            if (child == nullptr)
            {
                return sum;
            }
            return sum + node_total_ns(*child);
        });
}

void accumulate(
    const profiler_scope_data&                                 node,
    std::vector<std::string>&                                  path,
    std::unordered_map<std::string, hotspot_entry>&            hotspots,
    std::unordered_map<std::string, std::vector<std::string>>& call_stacks)
{
    const bool is_synthetic_root = (node.name_ == kSyntheticRootName);
    if (!is_synthetic_root)
    {
        const auto total_ns = node_total_ns(node);
        const auto child_ns = children_total_ns(node);
        const auto self_ns  = total_ns > child_ns ? total_ns - child_ns : 0;

        auto& entry = hotspots[node.name_];
        entry.name  = node.name_;
        entry.call_count += 1;
        entry.self_time_ns += self_ns;
        entry.total_time_ns += total_ns;

        path.push_back(node.name_);
        call_stacks.try_emplace(node.name_, path);
    }

    for (const auto& child : node.children_)
    {
        if (child != nullptr)
        {
            accumulate(*child, path, hotspots, call_stacks);
        }
    }

    if (!is_synthetic_root)
    {
        path.pop_back();
    }
}

std::string format_ns(uint64_t ns)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    if (ns >= 1'000'000)
    {
        oss.precision(2);
        oss << (static_cast<double>(ns) / 1'000'000.0) << "ms";
    }
    else if (ns >= 1'000)
    {
        oss.precision(2);
        oss << (static_cast<double>(ns) / 1'000.0) << "us";
    }
    else
    {
        oss << ns << "ns";
    }
    return oss.str();
}

std::string format_table_time(uint64_t ns)
{
    const double       us = static_cast<double>(ns) / 1000.0;
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    if (us >= 1'000'000.0)
    {
        oss << (us / 1'000'000.0) << "s";
    }
    else if (us >= 1'000.0)
    {
        oss << (us / 1'000.0) << "ms";
    }
    else
    {
        oss << us << "us";
    }
    return oss.str();
}

std::string format_percent(uint64_t part_ns, uint64_t total_ns)
{
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(2);
    if (total_ns == 0)
    {
        oss << "0.00%";
    }
    else
    {
        oss << (100.0 * static_cast<double>(part_ns) / static_cast<double>(total_ns)) << "%";
    }
    return oss.str();
}

std::string left_align(const std::string& text, size_t width)
{
    if (text.size() >= width)
    {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

std::string right_align(const std::string& text, size_t width)
{
    if (text.size() >= width)
    {
        return text;
    }
    return std::string(width - text.size(), ' ') + text;
}

uint64_t cpu_avg_ns(const hotspot_entry& entry)
{
    return entry.call_count == 0 ? 0 : entry.total_time_ns / entry.call_count;
}

uint64_t sort_metric(const hotspot_entry& entry, std::string_view sort_by)
{
    if (sort_by == "self_cpu_time_total" || sort_by == "self_cpu")
    {
        return entry.self_time_ns;
    }
    if (sort_by == "cpu_time_total" || sort_by == "cpu_time")
    {
        return entry.total_time_ns;
    }
    if (sort_by == "cpu_time_avg")
    {
        return cpu_avg_ns(entry);
    }
    if (sort_by == "count" || sort_by == "call_count")
    {
        return entry.call_count;
    }
    return entry.self_time_ns;
}

void render_tree(
    const profiler_scope_data& node, uint64_t root_total_ns, size_t depth, std::ostringstream& out)
{
    if (node.name_ == kSyntheticRootName)
    {
        for (const auto& child : node.children_)
        {
            if (child != nullptr)
            {
                render_tree(*child, root_total_ns, depth, out);
            }
        }
        return;
    }

    const auto total_ns = node_total_ns(node);
    const auto child_ns = children_total_ns(node);
    const auto self_ns  = total_ns > child_ns ? total_ns - child_ns : 0;

    out << std::string(depth * 2, ' ');
    out << '[' << format_percent(total_ns, root_total_ns) << "] " << node.name_
        << "  total=" << format_ns(total_ns) << " self=" << format_ns(self_ns) << '\n';

    for (const auto& child : node.children_)
    {
        if (child != nullptr)
        {
            render_tree(*child, root_total_ns, depth + 1, out);
        }
    }
}

}  // namespace

hotspot_report::hotspot_report(const profiler_scope_data* root) : root_(root)
{
    if (root_ == nullptr)
    {
        return;
    }

    std::unordered_map<std::string, hotspot_entry> hotspots;
    std::vector<std::string>                       path;
    accumulate(*root_, path, hotspots, call_stacks_);

    hotspots_.reserve(hotspots.size());
    for (auto& entry_pair : hotspots)
    {
        hotspots_.push_back(std::move(entry_pair.second));
    }
    std::sort(
        hotspots_.begin(),
        hotspots_.end(),
        [](const hotspot_entry& a, const hotspot_entry& b)
        { return a.self_time_ns > b.self_time_ns; });
}

std::string hotspot_report::top_down_tree() const
{
    if (root_ == nullptr)
    {
        return {};
    }

    std::ostringstream out;
    uint64_t           root_total = 0;
    if (root_->name_ == kSyntheticRootName)
    {
        for (const auto& child : root_->children_)
        {
            if (child != nullptr)
            {
                root_total += node_total_ns(*child);
            }
        }
        if (root_total == 0)
        {
            root_total = node_total_ns(*root_);
        }
    }
    else
    {
        root_total = node_total_ns(*root_);
    }

    render_tree(*root_, root_total, 0, out);
    return out.str();
}

std::string hotspot_report::bottom_up_hotspots(size_t max_rows) const
{
    std::ostringstream out;
    out << "self time      total time     calls  name\n";
    const size_t row_count =
        max_rows == 0 ? hotspots_.size() : std::min(max_rows, hotspots_.size());
    for (size_t i = 0; i < row_count; ++i)
    {
        const auto& entry = hotspots_[i];
        out << std::left << std::setw(14) << format_ns(entry.self_time_ns) << ' ' << std::setw(14)
            << format_ns(entry.total_time_ns) << ' ' << std::setw(6) << entry.call_count << ' '
            << entry.name << '\n';
    }
    return out.str();
}

std::vector<std::string> hotspot_report::call_stack_for(const std::string& name) const
{
    auto it = call_stacks_.find(name);
    return it == call_stacks_.end() ? std::vector<std::string>{} : it->second;
}

std::string hotspot_report::table(const std::string& sort_by, size_t row_limit) const
{
    std::vector<hotspot_entry> rows = hotspots_;
    if (!sort_by.empty())
    {
        std::sort(
            rows.begin(),
            rows.end(),
            [&sort_by](const hotspot_entry& lhs, const hotspot_entry& rhs)
            { return sort_metric(lhs, sort_by) > sort_metric(rhs, sort_by); });
    }

    const size_t shown = row_limit == 0 ? rows.size() : std::min(row_limit, rows.size());

    const uint64_t self_cpu_total = std::accumulate(
        hotspots_.begin(),
        hotspots_.end(),
        uint64_t{0},
        [](uint64_t sum, const hotspot_entry& entry) { return sum + entry.self_time_ns; });

    constexpr size_t kNumericWidth = 12;
    size_t           name_width    = 20;
    name_width                     = std::max(name_width, std::string("Name").size());
    for (size_t i = 0; i < shown; ++i)
    {
        name_width = std::max(name_width, rows[i].name.size());
    }

    const std::vector<std::string> headers{
        "Name", "Self CPU %", "Self CPU", "CPU total %", "CPU total", "CPU time avg", "# of Calls"};

    auto separator_line = [&]()
    {
        std::ostringstream line;
        for (size_t i = 0; i < headers.size(); ++i)
        {
            const size_t width = i == 0 ? name_width : kNumericWidth;
            if (i > 0)
            {
                line << "  ";
            }
            line << std::string(width, '-');
        }
        return line.str();
    };

    std::ostringstream out;
    out << separator_line() << '\n';
    for (size_t i = 0; i < headers.size(); ++i)
    {
        const size_t width = i == 0 ? name_width : kNumericWidth;
        if (i > 0)
        {
            out << "  ";
        }
        out << (i == 0 ? left_align(headers[i], width) : right_align(headers[i], width));
    }
    out << '\n';
    out << separator_line() << '\n';

    for (size_t i = 0; i < shown; ++i)
    {
        const auto&                    entry = rows[i];
        const std::vector<std::string> cells{
            entry.name,
            format_percent(entry.self_time_ns, self_cpu_total),
            format_table_time(entry.self_time_ns),
            format_percent(entry.total_time_ns, self_cpu_total),
            format_table_time(entry.total_time_ns),
            format_table_time(cpu_avg_ns(entry)),
            std::to_string(entry.call_count)};

        for (size_t col = 0; col < cells.size(); ++col)
        {
            const size_t width = col == 0 ? name_width : kNumericWidth;
            if (col > 0)
            {
                out << "  ";
            }
            out << (col == 0 ? left_align(cells[col], width) : right_align(cells[col], width));
        }
        out << '\n';
    }

    out << separator_line() << '\n';
    out << "Self CPU time total: " << format_table_time(self_cpu_total) << '\n';
    return out.str();
}

}  // namespace profiler
