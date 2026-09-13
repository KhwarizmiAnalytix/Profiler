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

#include "memory_tracker.h"

// Prevent Windows min/max macros from interfering
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <psapi.h>
#include <windows.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/resource.h>
#include <unistd.h>

#include <fstream>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/mach_types.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#endif

#endif

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>
namespace profiler
{

//=============================================================================
// memory_tracker Implementation
//=============================================================================

memory_tracker::memory_tracker() = default;

memory_tracker::~memory_tracker()
{
    if (tracking_.load())
    {
        stop_tracking();
    }
}

void memory_tracker::start_tracking()
{
    if (tracking_.exchange(true))
    {
        return;  // Already tracking
    }

    // Reset statistics
    current_usage_.store(0);
    peak_usage_.store(0);
    total_allocated_.store(0);
    total_deallocated_.store(0);

    {
        std::scoped_lock const lock(allocations_mutex_);
        active_allocations_.clear();
    }

    {
        std::scoped_lock const lock(snapshots_mutex_);
        snapshots_.clear();
    }
}

void memory_tracker::stop_tracking()
{
    if (!tracking_.exchange(false))
    {
        return;  // Not tracking
    }

    // Take final snapshot
    take_snapshot("final");
}

void memory_tracker::track_allocation(void* ptr, size_t size, const std::string& context)
{
    if (!tracking_.load() || (ptr == nullptr))
    {
        return;
    }

    profiler::memory_allocation allocation;
    allocation.address_   = ptr;
    allocation.size_      = size;
    allocation.timestamp_ = std::chrono::high_resolution_clock::now();
    allocation.context_   = context;
    allocation.thread_id_ = std::this_thread::get_id();

    {
        std::scoped_lock const lock(allocations_mutex_);
        active_allocations_[ptr] = allocation;
    }

    // Update statistics atomically
    size_t const new_current = current_usage_.fetch_add(size) + size;
    total_allocated_.fetch_add(size);

    // Update peak usage
    update_peak_usage(new_current);
}

void memory_tracker::track_deallocation(void* ptr)
{
    if (!tracking_.load() || (ptr == nullptr))
    {
        return;
    }

    size_t deallocated_size = 0;

    {
        std::scoped_lock const lock(allocations_mutex_);
        auto                   it = active_allocations_.find(ptr);
        if (it != active_allocations_.end())
        {
            deallocated_size = it->second.size_;
            active_allocations_.erase(it);
        }
    }

    if (deallocated_size > 0)
    {
        current_usage_.fetch_sub(deallocated_size);
        total_deallocated_.fetch_add(deallocated_size);
    }
}

profiler::memory_stats memory_tracker::get_current_stats() const
{
    profiler::memory_stats stats;
    stats.current_usage_     = current_usage_.load();
    stats.peak_usage_        = peak_usage_.load();
    stats.total_allocated_   = total_allocated_.load();
    stats.total_deallocated_ = total_deallocated_.load();
    stats.delta_since_start_ = static_cast<int64_t>(stats.current_usage_);
    return stats;
}

size_t memory_tracker::get_current_usage() const
{
    return current_usage_.load();
}

size_t memory_tracker::get_peak_usage() const
{
    return peak_usage_.load();
}

size_t memory_tracker::get_total_allocated() const
{
    return total_allocated_.load();
}

size_t memory_tracker::get_total_deallocated() const
{
    return total_deallocated_.load();
}

size_t memory_tracker::get_system_memory_usage()
{
    return get_process_memory_usage();
}

size_t memory_tracker::get_system_peak_memory_usage()
{
    return get_process_peak_memory_usage();
}

size_t memory_tracker::get_available_system_memory()
{
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo) != 0)
    {
        return static_cast<size_t>(memInfo.ullAvailPhys);
    }
    return 0;
#elif defined(__APPLE__)
    // On macOS, use mach API to get available memory
    vm_size_t              page_size;
    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t host_size = sizeof(vm_statistics64_data_t) / sizeof(natural_t);

    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm_stat, &host_size) ==
            KERN_SUCCESS)
    {
        // Available memory = free pages + inactive pages + purgeable pages
        uint64_t const available_pages =
            vm_stat.free_count + vm_stat.inactive_count + vm_stat.purgeable_count;
        return static_cast<size_t>(available_pages * page_size);
    }
    return 0;
#else
    // Linux and other Unix systems
    long const pages     = sysconf(_SC_AVPHYS_PAGES);
    long const page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0)
    {
        return static_cast<size_t>(pages * page_size);
    }
    return 0;
#endif
}

void memory_tracker::reset()
{
    current_usage_.store(0);
    peak_usage_.store(0);
    total_allocated_.store(0);
    total_deallocated_.store(0);

    {
        std::scoped_lock const lock(allocations_mutex_);
        active_allocations_.clear();
    }

    {
        std::scoped_lock const lock(snapshots_mutex_);
        snapshots_.clear();
    }
}

std::vector<profiler::memory_allocation> memory_tracker::get_active_allocations() const
{
    std::scoped_lock const                   lock(allocations_mutex_);
    std::vector<profiler::memory_allocation> allocations;
    allocations.reserve(active_allocations_.size());

    // Use std::transform to extract allocation values
    std::transform(
        active_allocations_.begin(),
        active_allocations_.end(),
        std::back_inserter(allocations),
        [](const auto& pair) { return pair.second; });

    return allocations;
}

size_t memory_tracker::get_allocation_count() const
{
    std::scoped_lock const lock(allocations_mutex_);
    return active_allocations_.size();
}

void memory_tracker::take_snapshot(const std::string& label)
{
    profiler::memory_stats const stats = get_current_stats();

    std::scoped_lock const lock(snapshots_mutex_);
    snapshots_.emplace_back(label, stats);
}

std::vector<std::pair<std::string, profiler::memory_stats>> memory_tracker::get_snapshots() const
{
    std::scoped_lock const lock(snapshots_mutex_);
    return snapshots_;
}

size_t memory_tracker::get_process_memory_usage()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<size_t>(pmc.WorkingSetSize);
    }
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t const         kr    = task_info(
        mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (kr == KERN_SUCCESS)
    {
        return static_cast<size_t>(info.resident_size);
    }
    return 0;
#else
    // Read from /proc/self/status on Linux
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open())
    {
        return 0;
    }

    std::string line;
    while (std::getline(status_file, line))
    {
        if (line.substr(0, 6) == "VmRSS:")
        {
            size_t kb = 0;
            if (sscanf(line.c_str(), "VmRSS: %zu kB", &kb) == 1)
            {
                return kb * 1024;  // Convert KB to bytes
            }
        }
    }
    return 0;
#endif
}

size_t memory_tracker::get_process_peak_memory_usage()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return static_cast<size_t>(pmc.PeakWorkingSetSize);
    }
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t const         kr    = task_info(
        mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (kr == KERN_SUCCESS)
    {
        return static_cast<size_t>(info.resident_size_max);
    }
    return 0;
#else
    // Read from /proc/self/status on Linux
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open())
    {
        return 0;
    }

    std::string line;
    while (std::getline(status_file, line))
    {
        if (line.size() >= 6 && line.substr(0, 6) == "VmHWM:")
        {
            size_t kb = 0;
            if (sscanf(line.c_str(), "VmHWM: %zu kB", &kb) == 1)
            {
                return kb * 1024;  // Convert KB to bytes
            }
        }
    }
    return 0;
#endif
}

void memory_tracker::update_peak_usage(size_t current)
{
    size_t expected_peak = peak_usage_.load();
    while (current > expected_peak && !peak_usage_.compare_exchange_weak(expected_peak, current))
    {
        // Loop until we successfully update or current is no longer > peak
    }
}

//=============================================================================
// memory_tracking_scope Implementation
//=============================================================================

memory_tracking_scope::memory_tracking_scope(profiler::memory_tracker& tracker, std::string label)
    : tracker_(tracker), label_(std::move(label))
{
    start_stats_ = tracker_.get_current_stats();
    tracker_.take_snapshot("start_" + label_);
}

memory_tracking_scope::~memory_tracking_scope()
{
    if (active_)
    {
        tracker_.take_snapshot("end_" + label_);
    }
}

profiler::memory_stats memory_tracking_scope::get_delta_stats() const
{
    profiler::memory_stats const current_stats = tracker_.get_current_stats();
    profiler::memory_stats       delta_stats;

    delta_stats.current_usage_   = current_stats.current_usage_;
    delta_stats.peak_usage_      = (std::max)(current_stats.peak_usage_, start_stats_.peak_usage_);
    delta_stats.total_allocated_ = current_stats.total_allocated_ - start_stats_.total_allocated_;
    delta_stats.total_deallocated_ =
        current_stats.total_deallocated_ - start_stats_.total_deallocated_;
    delta_stats.delta_since_start_ = static_cast<int64_t>(current_stats.current_usage_) -
                                     static_cast<int64_t>(start_stats_.current_usage_);

    return delta_stats;
}

}  // namespace profiler
