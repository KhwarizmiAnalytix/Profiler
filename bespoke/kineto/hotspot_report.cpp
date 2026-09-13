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

#include "bespoke/kineto/hotspot_report.h"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>

#include "bespoke/common/collection.h"

namespace profiler::profiler_impl
{

namespace
{

enum class time_domain
{
    cpu,
    cuda,
    xpu
};

int64_t node_total_ns(const experimental_event_t& node)
{
    const auto total = node->endTimeNS() - node->start_time_ns_;
    return total > 0 ? total : 0;
}

time_domain domain_of(const experimental_event_t& node)
{
    const auto device = node->deviceType();
    if (device == profiler::device_enum::CUDA || device == profiler::device_enum::HIP)
    {
        return time_domain::cuda;
    }
#if PROFILER_HAS_KINETO
    switch (node->kinetoType())
    {
    case libkineto::ActivityType::XPU_RUNTIME:
    case libkineto::ActivityType::XPU_DRIVER:
    case libkineto::ActivityType::XPU_SCOPE_PROFILER:
        return time_domain::xpu;
    default:
        break;
    }
#endif
    return time_domain::cpu;
}

int64_t children_total_ns(const experimental_event_t& node, time_domain domain)
{
    return std::accumulate(
        node->children_.begin(),
        node->children_.end(),
        int64_t{0},
        [domain](int64_t sum, const experimental_event_t& child)
        {
            if (domain_of(child) != domain)
            {
                return sum;
            }
            return sum + node_total_ns(child);
        });
}

// Recursively walks the RecordFunction event tree, accumulating per-name
// self/total time and call counts into `hotspots`, and remembering the first
// call path (root -> ... -> leaf) seen for each name into `call_stacks`.
void accumulate(
    const experimental_event_t&                                node,
    std::vector<std::string>&                                  path,
    std::unordered_map<std::string, hotspot_entry>&            hotspots,
    std::unordered_map<std::string, std::vector<std::string>>& call_stacks)
{
    const auto name     = node->name();
    const auto domain   = domain_of(node);
    const auto total_ns = node_total_ns(node);
    const auto child_ns = children_total_ns(node, domain);
    const auto self_ns  = total_ns > child_ns ? total_ns - child_ns : 0;
    const auto self_u   = static_cast<uint64_t>(self_ns);
    const auto total_u  = static_cast<uint64_t>(total_ns);

    auto& entry = hotspots[name];
    entry.name  = name;
    entry.call_count += 1;
    if (domain == time_domain::cuda)
    {
        entry.self_cuda_ns += self_u;
        entry.cuda_total_ns += total_u;
    }
    else if (domain == time_domain::xpu)
    {
        entry.self_xpu_ns += self_u;
        entry.xpu_total_ns += total_u;
    }
    else
    {
        entry.self_time_ns += self_u;
        entry.total_time_ns += total_u;
    }

    path.push_back(name);
    call_stacks.try_emplace(name, path);
    for (const auto& child : node->children_)
    {
        accumulate(child, path, hotspots, call_stacks);
    }
    path.pop_back();
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

// PyTorch profiler_util.format_time: us/ms/s with three decimals.
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

// Appends trailing spaces, producing left-aligned text.
std::string left_align(const std::string& text, size_t width)
{
    if (text.size() >= width)
    {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

// Prepends leading spaces, producing right-aligned text.
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

uint64_t cuda_avg_ns(const hotspot_entry& entry)
{
    return entry.call_count == 0 ? 0 : entry.cuda_total_ns / entry.call_count;
}

uint64_t xpu_avg_ns(const hotspot_entry& entry)
{
    return entry.call_count == 0 ? 0 : entry.xpu_total_ns / entry.call_count;
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
    if (sort_by == "self_cuda_time_total" || sort_by == "self_cuda")
    {
        return entry.self_cuda_ns;
    }
    if (sort_by == "cuda_time_total" || sort_by == "cuda_time")
    {
        return entry.cuda_total_ns;
    }
    if (sort_by == "cuda_time_avg")
    {
        return cuda_avg_ns(entry);
    }
    if (sort_by == "self_xpu_time_total" || sort_by == "self_xpu")
    {
        return entry.self_xpu_ns;
    }
    if (sort_by == "xpu_time_total" || sort_by == "xpu_time")
    {
        return entry.xpu_total_ns;
    }
    if (sort_by == "xpu_time_avg")
    {
        return xpu_avg_ns(entry);
    }
    if (sort_by == "count" || sort_by == "call_count")
    {
        return entry.call_count;
    }
    return entry.self_time_ns;
}

void render_tree(
    const experimental_event_t& node, int64_t root_total_ns, size_t depth, std::ostringstream& out)
{
    const auto domain      = domain_of(node);
    const auto total_ns    = node_total_ns(node);
    const auto children_ns = children_total_ns(node, domain);
    const auto self_ns     = total_ns > children_ns ? total_ns - children_ns : 0;
    const auto pct =
        root_total_ns > 0
            ? (100.0 * static_cast<double>(total_ns) / static_cast<double>(root_total_ns))
            : 0.0;

    out << std::string(depth * 2, ' ') << "[" << std::fixed << std::setprecision(1) << pct << "%] "
        << node->name() << "  total=" << format_ns(static_cast<uint64_t>(total_ns))
        << " self=" << format_ns(static_cast<uint64_t>(self_ns)) << '\n';

    for (const auto& child : node->children_)
    {
        render_tree(child, root_total_ns, depth + 1, out);
    }
}

}  // namespace

hotspot_report::hotspot_report(const ProfilerResult& result) : roots_(result.event_tree())
{
    std::unordered_map<std::string, hotspot_entry> hotspots;
    std::vector<std::string>                       path;
    for (const auto& root : roots_)
    {
        accumulate(root, path, hotspots, call_stacks_);
    }

    hotspots_.reserve(hotspots.size());
    for (auto& [name, entry] : hotspots)
    {
        hotspots_.push_back(std::move(entry));
    }
    std::sort(
        hotspots_.begin(),
        hotspots_.end(),
        [](const hotspot_entry& a, const hotspot_entry& b)
        { return a.self_time_ns > b.self_time_ns; });
}

std::string hotspot_report::top_down_tree() const
{
    std::ostringstream out;
    for (const auto& root : roots_)
    {
        render_tree(root, node_total_ns(root), 0, out);
    }
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

    uint64_t self_cpu_total  = 0;
    uint64_t self_cuda_total = 0;
    uint64_t self_xpu_total  = 0;
    for (const auto& entry : hotspots_)
    {
        self_cpu_total += entry.self_time_ns;
        self_cuda_total += entry.self_cuda_ns;
        self_xpu_total += entry.self_xpu_ns;
    }
    const bool show_cuda = self_cuda_total > 0;
    const bool show_xpu  = self_xpu_total > 0;

    constexpr size_t kNumericWidth = 12;
    size_t           name_width    = 20;
    name_width                     = std::max(name_width, std::string("Name").size());
    for (size_t i = 0; i < shown; ++i)
    {
        name_width = std::max(name_width, rows[i].name.size());
    }

    std::vector<std::string> headers{
        "Name", "Self CPU %", "Self CPU", "CPU total %", "CPU total", "CPU time avg"};
    if (show_cuda)
    {
        headers.insert(headers.end(), {"Self CUDA", "Self CUDA %", "CUDA total", "CUDA time avg"});
    }
    if (show_xpu)
    {
        headers.insert(headers.end(), {"Self XPU", "Self XPU %", "XPU total", "XPU time avg"});
    }
    headers.emplace_back("# of Calls");

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

    auto emit_header_row = [&](std::ostringstream& out)
    {
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
    };

    std::ostringstream out;
    out << separator_line() << '\n';
    emit_header_row(out);
    out << separator_line() << '\n';

    for (size_t i = 0; i < shown; ++i)
    {
        const auto&              entry = rows[i];
        std::vector<std::string> cells{
            entry.name,
            format_percent(entry.self_time_ns, self_cpu_total),
            format_table_time(entry.self_time_ns),
            format_percent(entry.total_time_ns, self_cpu_total),
            format_table_time(entry.total_time_ns),
            format_table_time(cpu_avg_ns(entry))};
        if (show_cuda)
        {
            cells.insert(
                cells.end(),
                {format_table_time(entry.self_cuda_ns),
                 format_percent(entry.self_cuda_ns, self_cuda_total),
                 format_table_time(entry.cuda_total_ns),
                 format_table_time(cuda_avg_ns(entry))});
        }
        if (show_xpu)
        {
            cells.insert(
                cells.end(),
                {format_table_time(entry.self_xpu_ns),
                 format_percent(entry.self_xpu_ns, self_xpu_total),
                 format_table_time(entry.xpu_total_ns),
                 format_table_time(xpu_avg_ns(entry))});
        }
        cells.push_back(std::to_string(entry.call_count));

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
    out << "Self CPU time total: " << format_table_time(self_cpu_total);
    if (show_cuda)
    {
        out << "\nSelf CUDA time total: " << format_table_time(self_cuda_total);
    }
    if (show_xpu)
    {
        out << "\nSelf XPU time total: " << format_table_time(self_xpu_total);
    }
    out << '\n';
    return out.str();
}

}  // namespace profiler::profiler_impl
