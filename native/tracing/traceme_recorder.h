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

#include <atomic>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "common/profiler_export.h"
#include "common/profiler_macros.h"

namespace profiler
{
namespace internal
{

/**
 * @brief Global atomic trace level for fast, lock-free tracing state checks.
 *
 * This atomic integer stores the current maximum trace level that should be recorded.
 * It's designed for extremely fast access from traceme objects to determine if
 * tracing is active without any locking overhead.
 *
 * **Values**:
 * - kTracingDisabled (-1): No tracing active
 * - 1-3: Maximum trace level to record (higher levels filtered out)
 *
 * **Thread Safety**: Atomic operations ensure thread-safe access
 * **Performance**: Lock-free design enables sub-nanosecond access times
 */
PROFILER_API extern std::atomic<int> g_trace_level;

/**
 * @brief Global atomic filter bitmap for selective trace event recording.
 *
 * This atomic uint64_t stores a bitmap that can be used to filter trace events
 * during recording. Each bit can represent a category or type of event to include.
 * By default, all bits are set (all events pass the filter).
 *
 * **Values**:
 * - 0xFFFFFFFFFFFFFFFF (default): All events pass the filter
 * - Custom bitmask: Only events matching the mask are recorded
 *
 * **Thread Safety**: Atomic operations ensure thread-safe access
 * **Performance**: Lock-free design enables fast filtering checks
 */
PROFILER_API extern std::atomic<uint64_t> g_trace_filter_bitmap;

}  // namespace internal

/**
 * @brief High-performance, thread-safe singleton for collecting and managing trace events.
 *
 * The `traceme_recorder` serves as the central repository for all trace events generated
 * by `traceme` objects throughout the application. It's designed for minimal overhead
 * during event recording and efficient batch collection during profiling sessions.
 *
 * **Architecture**:
 * - **Singleton Pattern**: Global state accessible from any thread
 * - **Thread-Local Storage**: Each thread maintains independent event buffers
 * - **Lock-Free Recording**: Events are recorded without blocking other threads
 * - **Batch Collection**: Events are collected and processed in batches for efficiency
 *
 * **Lifecycle**:
 * 1. `start(level)` - Begins recording events at or below the specified level
 * 2. `record(event)` - Called by traceme objects to record individual events
 * 3. `stop()` - Ends recording and returns all collected events
 *
 * **Event Processing**:
 * - Complete events (with start and end times) are recorded directly
 * - Split events (activity_start/activity_end pairs) are matched and merged
 * - Cross-thread event correlation is handled automatically
 * - Unpaired events are discarded to maintain data integrity
 *
 * **Performance Characteristics**:
 * - Recording overhead: ~10-20 nanoseconds per event
 * - Memory usage: ~64 bytes per recorded event
 * - Thread contention: Minimal due to thread-local buffers
 * - Collection time: Proportional to number of events and threads
 *
 * **Thread Safety**: All methods are thread-safe. Recording can occur concurrently
 * with start/stop operations, though events recorded during stop may be discarded.
 */
class PROFILER_VISIBILITY traceme_recorder
{
public:
    /**
     * @brief Represents a single trace event with timing and identification information.
     *
     * Events can represent complete activities (with both start and end times) or
     * partial activities that need to be paired with corresponding start/end events.
     * The encoding uses negative timestamps to store activity IDs for event pairing.
     *
     * **Event Types**:
     * - **Complete**: Both start_time and end_time are positive (typical RAII traces)
     * - **Start**: end_time is negative, encoding the activity_id for later pairing
     * - **End**: start_time is negative, encoding the activity_id to match with start
     *
     * **Timestamp Format**: Nanoseconds since Unix epoch (January 1, 1970 00:00:00 UTC)
     * **Activity ID Encoding**: Negative timestamps encode unique activity identifiers
     * for pairing start/end events across the static API
     */
    struct Event
    {
        /// @brief Checks if this is a complete event with both start and end times
        bool is_complete() const { return start_time > 0 && end_time > 0; }

        /// @brief Checks if this is a start event (end_time encodes activity_id)
        bool is_start() const { return end_time < 0; }

        /// @brief Checks if this is an end event (start_time encodes activity_id)
        bool is_end() const { return start_time < 0; }

        /**
         * @brief Extracts the activity ID from start or end events.
         * @return Activity ID for pairing, or 1 for complete events
         */
        int64_t activity_id() const
        {
            if (is_start())
                return -end_time;
            if (is_end())
                return -start_time;
            return 1;  // complete
        }

        std::string name;    ///< Human-readable event name with optional metadata
        int64_t start_time;  ///< Start timestamp (ns since epoch) or -activity_id for end events
        int64_t end_time;    ///< End timestamp (ns since epoch) or -activity_id for start events
    };
    /**
     * @brief Thread identification and metadata for trace event attribution.
     *
     * Contains information about the thread that generated trace events,
     * enabling proper attribution and visualization in profiling tools.
     */
    struct ThreadInfo
    {
        uint64_t    tid;   ///< Thread identifier (OS thread ID)
        std::string name;  ///< Human-readable thread name (if available)
    };

    /**
     * @brief Collection of trace events from a single thread.
     *
     * Groups all events generated by a specific thread along with thread
     * identification information. Events within a thread are ordered by
     * their end time for proper visualization.
     */
    struct ThreadEvents
    {
        ThreadInfo        thread;  ///< Thread identification and metadata
        std::deque<Event> events;  ///< Chronologically ordered events from this thread
    };

    /// Collection of events from all threads that participated in tracing
    using Events = std::vector<ThreadEvents>;

    /**
     * @brief Starts trace event recording at the specified level.
     *
     * Begins a new tracing session, enabling collection of trace events at or below
     * the specified level. Only one tracing session can be active at a time.
     *
     * @param level Maximum trace level to record (1-3 typical, 0 disables all tracing)
     *              Higher levels capture more detailed but potentially higher-overhead events
     * @return true if tracing was successfully started, false if already active
     *
     * **Thread Safety**: Safe to call from any thread, but only one session can be active
     * **Performance**: Clears any stale events from previous sessions
     * **Level Filtering**: Events with level > specified level are ignored
     *
     * **Example**: `traceme_recorder::start(2)` records CRITICAL and INFO level events
     */
    PROFILER_API static bool start(int level);

    /**
     * @brief Starts trace event recording with level and filter mask.
     *
     * Begins a new tracing session with both level-based and bitmap-based filtering.
     * This allows for more selective event recording based on event categories.
     *
     * @param level Maximum trace level to record (1-3 typical, 0 disables all tracing)
     * @param filter_mask Bitmap filter for selective event recording (default: all bits set)
     * @return true if tracing was successfully started, false if already active
     *
     * **Thread Safety**: Safe to call from any thread, but only one session can be active
     * **Performance**: Clears any stale events from previous sessions
     * **Filtering**: Events must pass both level check AND filter mask check
     *
     * **Example**: `traceme_recorder::start(2, 0x0F)` records level 1-2 events matching mask
     */
    PROFILER_API static bool start(int level, uint64_t filter_mask);

    /**
     * @brief Stops trace recording and returns all collected events.
     *
     * Ends the current tracing session and returns all events collected since
     * the corresponding start() call. Events are organized by thread and include
     * complete timing information with start/end event pairing resolved.
     *
     * @return Collection of events grouped by thread, or empty if tracing wasn't active
     *
     * **Thread Safety**: Safe to call from any thread
     * **Event Processing**: Automatically pairs start/end events and discards orphaned events
     * **Performance**: Collection time scales with number of events and active threads
     * **Race Conditions**: Events recorded during stop() may be dropped
     */
    PROFILER_API static Events stop();

    /**
     * @brief Fast, lock-free check if tracing is active for the specified level.
     *
     * Determines if trace events at the given level would be recorded. This is
     * the primary method used by traceme objects to decide whether to start tracing.
     *
     * @param level Trace level to check (default: 1)
     * @return true if tracing is active and events at this level would be recorded
     *
     * **Performance**: Extremely fast (~1-2 CPU cycles) - just an atomic load
     * **Thread Safety**: Safe to call from any thread
     * **Race Conditions**: Result may become stale immediately after call, but this
     *                      is acceptable for optimization purposes
     * **Note**: Marked as "racy" because the result can change between check and use,
     *           but this is by design for performance
     */
    static inline bool active(int level = 1)
    {
        return internal::g_trace_level.load(std::memory_order_acquire) >= level;
    }

    /**
     * @brief Fast check whether the provided filter mask passes the current filter.
     *
     * @param filter Bitmap describing the event category
     * @return true if the event should be recorded
     */
    static inline bool check_filter(uint64_t filter)
    {
        return (internal::g_trace_filter_bitmap.load(std::memory_order_acquire) & filter) != 0;
    }

    /// Sentinel value indicating tracing is disabled
    static constexpr int kTracingDisabled = -1;
    /// Default filter mask enabling all events
    static constexpr uint64_t kDefaultTraceFilter = std::numeric_limits<uint64_t>::max();

    /**
     * @brief Records a trace event to the thread-local buffer.
     *
     * Adds a trace event to the current thread's event buffer for later collection.
     * This is a non-blocking operation designed for high-frequency use in
     * performance-critical code paths.
     *
     * @param event Trace event to record (moved to avoid copying)
     *
     * **Performance**: Very fast (~10-20 nanoseconds) - no locking or allocation
     * **Thread Safety**: Safe to call from any thread - uses thread-local storage
     * **Memory**: Events are stored in thread-local buffers until collection
     * **Blocking**: Never blocks - uses lock-free data structures
     */
    PROFILER_API static void record(Event&& event);

    /**
     * @brief Generates a globally unique activity ID for manual activity tracking.
     *
     * Creates a unique identifier for use with the static activity API
     * (activity_start/activity_end). IDs are guaranteed to be unique across
     * all threads and time.
     *
     * @return Unique 64-bit activity identifier
     *
     * **Thread Safety**: Safe to call from any thread
     * **Uniqueness**: Combines thread ID (upper 32 bits) with per-thread counter (lower 32 bits)
     * **Performance**: Very fast - uses thread-local counters to avoid contention
     * **Capacity**: Supports up to 2 billion threads, each with 4 billion activities
     */
    PROFILER_API static int64_t new_activity_id();

private:
    /// Deleted constructor - this is a singleton with only static methods
    traceme_recorder() = delete;

    /// Deleted destructor - this is a singleton with only static methods
    ~traceme_recorder() = delete;

    /**
     * @brief Clears stale events from all thread-local buffers.
     *
     * Removes any events that may have been recorded due to race conditions
     * between record() calls and stop(). Called internally when starting
     * a new tracing session to ensure clean state.
     *
     * **Thread Safety**: Safe to call during start() - coordinates with active threads
     * **Performance**: Scales with number of active threads
     */
    PROFILER_API static void clear();

    /**
     * @brief Collects and processes events from all thread-local buffers.
     *
     * Gathers events from all threads that participated in tracing, processes
     * start/end event pairing, and returns the complete event collection.
     * Called internally by stop().
     *
     * **Thread Safety**: Coordinates with all active threads to collect events
     * **Event Processing**: Handles start/end pairing and cross-thread event correlation
     * **Performance**: Time scales with number of events and threads
     * **Memory**: Clears thread-local buffers after collection
     */
    PROFILER_API static Events consume();
};

}  // namespace profiler
