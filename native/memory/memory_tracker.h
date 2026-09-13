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

/**
 * @file memory_tracker.h
 * @brief Memory allocation and usage tracking for the enhanced profiler system (OPTIONAL COMPONENT)
 *
 * Provides comprehensive memory tracking capabilities including:
 * - Real-time memory allocation/deallocation monitoring
 * - Peak memory usage tracking
 * - Memory usage statistics and reporting
 * - Thread-safe operations for multi-threaded applications
 * - Cross-platform memory usage queries
 *
 * COMPONENT CLASSIFICATION: OPTIONAL
 * This component provides memory profiling capabilities but is not required
 * for basic profiling functionality. Can be disabled to reduce binary size.
 *
 * @author Profiler Development Team
 * @version 1.0
 * @date 2024
 */

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

#include "common/flat_hash.h"
#include "native/session/profiler.h"

#ifdef _WIN32
#include <psapi.h>
#include <windows.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace profiler
{

/**
 * @brief Custom hash function for void pointers
 *
 * Provides a simple hash implementation for void* that doesn't rely on
 * std::__hash_memory which may not be available in all libc++ versions.
 */
struct void_ptr_hash
{
    std::size_t operator()(void* ptr) const noexcept
    {
        // Convert pointer to uintptr_t and use it as hash
        // This is a simple but effective hash for pointers
        return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(ptr));
    }
};

/**
 * @brief Memory allocation tracking entry
 *
 * Contains information about a single memory allocation including
 * address, size, timestamp, and context information.
 */
struct memory_allocation
{
    /// Pointer to the allocated memory address
    void* address_ = nullptr;

    /// Size of the allocation in bytes
    size_t size_ = 0;

    /// High-resolution timestamp when allocation occurred
    std::chrono::high_resolution_clock::time_point timestamp_;

    /// Optional context information (function name, scope, etc.)
    std::string context_;

    /// ID of the thread that performed the allocation
    std::thread::id thread_id_;
};

/**
 * @brief Thread-safe memory tracker for comprehensive memory monitoring
 *
 * Provides real-time tracking of memory allocations and deallocations,
 * calculates memory usage statistics, and supports cross-platform
 * memory usage queries.
 */
class PROFILER_VISIBILITY memory_tracker
{
public:
    /**
     * @brief Construct a new memory tracker
     */
    PROFILER_API memory_tracker();

    /**
     * @brief Destructor - automatically stops tracking if active
     */
    PROFILER_API ~memory_tracker();

    /**
     * @brief Start memory tracking
     */
    PROFILER_API void start_tracking();

    /**
     * @brief Stop memory tracking
     */
    PROFILER_API void stop_tracking();

    /**
     * @brief Check if memory tracking is currently active
     * @return true if tracking is active, false otherwise
     */
    bool is_tracking() const { return tracking_.load(); }

    /**
     * @brief Track a memory allocation
     * @param ptr Pointer to the allocated memory
     * @param size Size of the allocation in bytes
     * @param context Optional context information
     */
    PROFILER_API void track_allocation(void* ptr, size_t size, const std::string& context = "");

    /**
     * @brief Track a memory deallocation
     * @param ptr Pointer to the memory being deallocated
     */
    PROFILER_API void track_deallocation(void* ptr);

    /**
     * @brief Get current memory usage statistics
     * @return Complete memory statistics structure
     */
    PROFILER_API profiler::memory_stats get_current_stats() const;

    /**
     * @brief Get current memory usage in bytes
     * @return Current memory usage
     */
    PROFILER_API size_t get_current_usage() const;

    /**
     * @brief Get peak memory usage in bytes
     * @return Peak memory usage observed
     */
    PROFILER_API size_t get_peak_usage() const;
    /**
     * @brief Get total memory allocated since tracking started
     * @return Total allocated memory in bytes
     */
    PROFILER_API size_t get_total_allocated() const;

    /**
     * @brief Get total memory deallocated since tracking started
     * @return Total deallocated memory in bytes
     */
    PROFILER_API size_t get_total_deallocated() const;

    /**
     * @brief Get current system memory usage
     * @return System memory usage in bytes
     */
    PROFILER_API static size_t get_system_memory_usage();

    /**
     * @brief Get peak system memory usage
     * @return Peak system memory usage in bytes
     */
    PROFILER_API static size_t get_system_peak_memory_usage();

    /**
     * @brief Get available system memory
     * @return Available system memory in bytes
     */
    PROFILER_API static size_t get_available_system_memory();

    /**
     * @brief Reset all tracking data and statistics
     */
    PROFILER_API void reset();

    /**
     * @brief Get a copy of all currently active allocations (thread-safe)
     * @return Vector of active memory allocations
     */
    PROFILER_API std::vector<profiler::memory_allocation> get_active_allocations() const;

    /**
     * @brief Get the number of currently active allocations
     * @return Number of active allocations
     */
    PROFILER_API size_t get_allocation_count() const;

    /**
     * @brief Take a memory usage snapshot with optional label
     * @param label Optional label for the snapshot
     */
    PROFILER_API void take_snapshot(const std::string& label = "");

    /**
     * @brief Get all memory usage snapshots
     * @return Vector of labeled memory statistics snapshots
     */
    PROFILER_API std::vector<std::pair<std::string, profiler::memory_stats>> get_snapshots() const;

private:
    /// Atomic flag indicating if tracking is active
    std::atomic<bool> tracking_{false};

    /// Mutex for thread-safe access to allocation tracking data
    mutable std::mutex allocations_mutex_;

    /// Map of active memory allocations (using custom hash for void*)
    std::unordered_map<void*, profiler::memory_allocation, profiler::void_ptr_hash>
        active_allocations_;

    /// Atomic counter for current memory usage
    std::atomic<size_t> current_usage_{0};

    /// Atomic counter for peak memory usage
    std::atomic<size_t> peak_usage_{0};

    /// Atomic counter for total allocated memory
    std::atomic<size_t> total_allocated_{0};

    /// Atomic counter for total deallocated memory
    std::atomic<size_t> total_deallocated_{0};

    /// Mutex for thread-safe access to snapshots
    mutable std::mutex snapshots_mutex_;

    /// Vector of labeled memory usage snapshots
    std::vector<std::pair<std::string, profiler::memory_stats>> snapshots_;

    /**
     * @brief Get current process memory usage (platform-specific)
     * @return Process memory usage in bytes
     */
    static size_t get_process_memory_usage();

    /**
     * @brief Get peak process memory usage (platform-specific)
     * @return Peak process memory usage in bytes
     */
    static size_t get_process_peak_memory_usage();

    /**
     * @brief Update peak usage if current usage is higher
     * @param current Current memory usage
     */
    void update_peak_usage(size_t current);
};

/**
 * @brief RAII memory tracking scope for automatic memory monitoring
 *
 * Provides automatic memory tracking for a specific scope, capturing
 * memory usage at the beginning and end of the scope to calculate
 * memory deltas and usage patterns.
 */
class PROFILER_VISIBILITY memory_tracking_scope
{
public:
    /**
     * @brief Construct a new memory tracking scope
     * @param tracker Reference to the memory tracker to use
     * @param label Optional label for this tracking scope
     */
    explicit memory_tracking_scope(profiler::memory_tracker& tracker, std::string label = "");

    /**
     * @brief Destructor - automatically captures final memory statistics
     */
    ~memory_tracking_scope();

    /**
     * @brief Get memory usage delta since scope creation
     * @return Memory statistics showing the change since scope start
     */
    profiler::memory_stats get_delta_stats() const;

private:
    /// Reference to the memory tracker
    profiler::memory_tracker& tracker_;

    /// Optional label for this scope
    std::string label_;

    /// Memory statistics captured at scope start
    profiler::memory_stats start_stats_;

    /// Flag indicating if the scope is still active
    bool active_ = true;
};

/**
 * @brief Custom allocator that integrates with memory tracking
 *
 * STL-compatible allocator that automatically tracks all allocations
 * and deallocations through the enhanced profiler system.
 *
 * @tparam T Type of objects to allocate
 */
template <typename T>
class tracked_allocator
{
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind
    {
        using other = tracked_allocator<U>;
    };

    size_type max_size() const noexcept
    {
        return std::numeric_limits<size_type>::max() / (sizeof(T) == 0 ? 1 : sizeof(T));
    }

    /**
     * @brief Construct allocator with optional memory tracker
     * @param tracker Pointer to memory tracker (nullptr to disable tracking)
     */
    explicit tracked_allocator(profiler::memory_tracker* tracker = nullptr) : tracker_(tracker) {}

    /**
     * @brief Copy constructor for different types
     * @tparam U Source type
     * @param other Source allocator
     */
    template <typename U>
    tracked_allocator(const tracked_allocator<U>& other) : tracker_(other.tracker_)
    {
    }

    /**
     * @brief Allocate memory and track the allocation
     * @param n Number of objects to allocate
     * @return Pointer to allocated memory
     */
    pointer allocate(size_type n)
    {
        if (n > max_size())
        {
            throw std::bad_array_new_length();
        }

        auto ptr = static_cast<pointer>(std::malloc(n * sizeof(T)));
        if (ptr == nullptr)
        {
            throw std::bad_alloc();
        }

        if (tracker_ != nullptr)
        {
            tracker_->track_allocation(ptr, n * sizeof(T), "tracked_allocator");
        }
        return ptr;
    }

    /**
     * @brief Deallocate memory and track the deallocation
     * @param ptr Pointer to memory to deallocate
     * @param n Number of objects (unused but required by STL interface)
     */
    void deallocate(pointer ptr, size_type)
    {
        if (tracker_ && ptr)
        {
            tracker_->track_deallocation(ptr);
        }
        std::free(ptr);
    }

    /**
     * @brief Compare allocators for equality
     * @tparam U Other allocator type
     * @param other Other allocator
     * @return true if allocators are equivalent
     */
    template <typename U>
    bool operator==(const tracked_allocator<U>& other) const
    {
        return tracker_ == other.tracker_;
    }

    /**
     * @brief Compare allocators for inequality
     * @tparam U Other allocator type
     * @param other Other allocator
     * @return true if allocators are not equivalent
     */
    template <typename U>
    bool operator!=(const tracked_allocator<U>& other) const
    {
        return !(*this == other);
    }

private:
    /// Pointer to the memory tracker
    profiler::memory_tracker* tracker_;

    /// Allow access to tracker_ from other template instantiations
    template <typename U>
    friend class tracked_allocator;
};

}  // namespace profiler
