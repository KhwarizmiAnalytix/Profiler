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

#include "native/tracing/tracing.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace profiler::tracing
{
namespace
{
std::atomic<uint64_t>                     g_unique_arg{1};
std::mutex                                g_name_mutex;
std::unordered_map<std::string, uint64_t> g_name_to_ids;
}  // namespace

std::array<const event_collector*, static_cast<unsigned>(event_category::kNumCategories)>
    event_collector::instances_ = {};

const char* get_event_category_name(event_category category)
{
    switch (category)
    {
    case event_category::kScheduleClosure:
        return "ScheduleClosure";
    case event_category::kRunClosure:
        return "RunClosure";
    case event_category::kCompute:
        return "Compute";
    case event_category::kNumCategories:
    default:
        return "Unknown";
    }
}

void event_collector::set_current_thread_name(const char* /*name*/)
{
    //profiler::logger::SetThreadName(name);
}

void set_event_collector(event_category category, const event_collector* collector)
{
    event_collector::instances_[static_cast<unsigned>(category)] = collector;
}

const event_collector* get_event_collector(event_category category)
{
    if (!event_collector::is_enabled())
    {
        return nullptr;
    }
    return event_collector::instances_[static_cast<unsigned>(category)];
}

uint64_t get_unique_arg()
{
    return g_unique_arg.fetch_add(1, std::memory_order_relaxed);
}

uint64_t get_arg_for_name(std::string_view name)
{
    std::scoped_lock const lock(g_name_mutex);
    auto                   it = g_name_to_ids.find(std::string(name));
    if (it != g_name_to_ids.end())
    {
        return it->second;
    }
    uint64_t const id = get_unique_arg();
    g_name_to_ids.emplace(name, id);
    return id;
}

void record_event(event_category category, uint64_t arg)
{
    if (const auto* collector = get_event_collector(category))
    {
        collector->record_event(arg);
    }
}

scoped_region::scoped_region(event_category category, uint64_t arg)
    : collector_(get_event_collector(category))
{
    if (collector_ != nullptr)
    {
        collector_->start_region(arg);
    }
}

scoped_region::scoped_region(event_category category) : scoped_region(category, get_unique_arg()) {}

scoped_region::scoped_region(event_category category, std::string_view name)
    : scoped_region(category, get_arg_for_name(name))
{
}

scoped_region::scoped_region(scoped_region&& other) noexcept : collector_(other.collector_)
{
    other.collector_ = nullptr;
}

scoped_region::~scoped_region()
{
    if (collector_ != nullptr && event_collector::is_enabled())
    {
        collector_->stop_region();
    }
}

const char* get_log_dir()
{
    return "";
}

}  // namespace profiler::tracing
