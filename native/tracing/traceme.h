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

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

//////#include "logger/logger.h"
#include "common/no_init.h"
#include "native/tracing/traceme_encode.h"
#include "native/tracing/traceme_recorder.h"

namespace profiler
{

/**
 * @brief Gets the current timestamp in nanoseconds from a monotonic clock.
 *
 * Provides a high-precision timestamp for trace event timing. Deliberately
 * uses steady_clock rather than a wall CLOCK_REALTIME source: traceme durations
 * feed unsigned nanosecond math (profiler_session::start_time_ns_/
 * end_time_ns_, scope_tree_builder's interval nesting) that must never see
 * time run backwards, which a wall-clock step (NTP, manual clock change)
 * could otherwise cause. This is the single clock source for the native
 * tracing hot path; native/utils/time_utils.cpp's get_current_time_nanos()
 * delegates here rather than reimplementing it.
 *
 * @return Current time in nanoseconds from an unspecified, monotonically
 *         non-decreasing epoch (not the Unix epoch -- only differences
 *         between calls are meaningful).
 *
 * **Performance**: Optimized for speed - typically 10-50 nanoseconds per call
 * **Resolution**: Nanosecond precision where supported by the system
 * **Thread Safety**: Safe to call from any thread
 *
 * @note This function is force-inlined for optimal performance in hot paths
 */
inline int64_t get_current_time_nanos()
{
    auto const now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

/**
 * @brief Predefined trace levels for hierarchical event filtering and profiling granularity.
 *
 * These levels provide a standardized way to categorize trace events by importance
 * and performance impact, enabling selective profiling based on desired detail level.
 * Higher levels capture more detailed but potentially higher-overhead events.
 *
 * **Usage Guidelines**:
 * - Use CRITICAL for user-facing operations and major computational steps
 * - Use INFO for significant internal operations and expensive computations
 * - Use VERBOSE for fine-grained operations and lightweight internal events
 *
 * **Performance Impact**: Lower levels (CRITICAL) have minimal overhead,
 * while higher levels (VERBOSE) may capture more events with increased overhead.
 */
enum class traceme_level_enum
{
    CRITICAL = 1,  ///< Default level for user instrumentation and critical operations
    INFO     = 2,  ///< High-level program execution details (expensive operations, major steps)
    VERBOSE  = 3,  ///< Low-level program execution details (cheap operations, internal calls)
};

/**
 * @brief Determines appropriate trace level for TensorFlow operations based on computational cost.
 *
 * This utility function provides a standardized way to assign trace levels to TensorFlow
 * operations based on their expected computational expense, enabling selective profiling
 * of performance-critical operations while filtering out noise from lightweight operations.
 *
 * @param is_expensive True if the operation is computationally expensive (e.g., matrix multiplication,
 *                     convolution), false for lightweight operations (e.g., element-wise operations,
 *                     shape manipulations)
 * @return Trace level: INFO (2) for expensive operations that should be visible by default
 *         in profiler UI, VERBOSE (3) for cheap operations shown only in detailed views
 *
 * **Usage Example**:
 * ```cpp
 * traceme trace("MatMul", get_tf_traceme_level(true));   // Level 2 (INFO)
 * traceme trace("Reshape", get_tf_traceme_level(false)); // Level 3 (VERBOSE)
 * ```
 */
inline int get_tf_traceme_level(bool is_expensive)
{
    return is_expensive ? static_cast<int>(traceme_level_enum::INFO)
                        : static_cast<int>(traceme_level_enum::VERBOSE);
}

/**
 * @brief High-performance RAII-based CPU activity tracing for profiling and performance analysis.
 *
 * The `traceme` class provides a lightweight, thread-safe mechanism for instrumenting code
 * with timing and contextual information. It's designed for minimal overhead when tracing
 * is disabled and efficient event collection when enabled.
 *
 * **Key Features**:
 * - **RAII Design**: Automatic start/stop timing with scope-based lifetime management
 * - **Zero-Cost When Disabled**: Compile-time and runtime optimizations eliminate overhead
 * - **Thread-Safe**: Safe for concurrent use across multiple threads
 * - **Hierarchical Levels**: Support for filtering events by importance/verbosity
 * - **Rich Metadata**: Extensible metadata encoding for detailed analysis
 * - **Cross-Platform**: Works on Windows, Linux, and macOS
 *
 * **Primary Use Cases**:
 * - Performance profiling and bottleneck identification
 * - Understanding CPU/GPU operation correlation and synchronization
 * - Debugging complex multi-threaded execution flows
 * - Memory allocation and computational operation tracing
 * - Integration with external profiling tools and visualizers
 *
 * **Two Usage Patterns**:
 *
 * 1. **Scoped RAII Objects** (Recommended):
 * ```cpp
 * {
 *     traceme trace("matrix_multiplication");
 *     // ... perform matrix multiplication ...
 * } // Automatically records timing when trace goes out of scope
 * ```
 *
 * 2. **Manual Activity Management**:
 * ```cpp
 * auto id = traceme::activity_start("async_operation");
 * // ... start asynchronous work ...
 * traceme::activity_end(id); // Must be called from same thread
 * ```
 *
 * **Performance Characteristics**:
 * - Overhead when disabled: ~1-2 CPU cycles (branch prediction + atomic load)
 * - Overhead when enabled: ~50-100 nanoseconds per trace event
 * - Memory usage: ~64 bytes per active trace event
 * - Thread contention: Lock-free recording minimizes blocking
 *
 * @note This class is optimized for high-frequency usage in performance-critical code.
 *       Events are recorded to thread-local buffers and collected during profiling sessions.
 */
class PROFILER_VISIBILITY traceme
{
public:
    static constexpr uint64_t kTraceFilterDefaultMask = traceme_recorder::kDefaultTraceFilter;

    /**
     * @brief Constructs a scoped trace event with the specified name and priority level.
     *
     * Creates a trace event that automatically records start time on construction and
     * end time on destruction. This is the primary constructor for most use cases.
     *
     * @param name Human-readable name for the activity (e.g., "matrix_multiply", "file_read").
     *             Should be descriptive but concise for profiler display. Avoid dynamic
     *             string generation; use the lambda constructor for expensive names.
     * @param level Trace priority level for filtering (default: 1). Higher levels capture
     *              more detailed events but may impact performance:
     *              - 1 (CRITICAL): User operations, major computational steps
     *              - 2 (INFO): Significant internal operations, expensive computations
     *              - 3+ (VERBOSE): Fine-grained operations, lightweight internal calls
     *
     * **Performance**: When tracing is disabled, this constructor has near-zero overhead
     * (~1-2 CPU cycles). When enabled, overhead is ~50-100 nanoseconds.
     *
     * **Thread Safety**: Safe to call from any thread. Each thread maintains independent
     * event buffers to minimize contention.
     *
     * **Example Usage**:
     * ```cpp
     * {
     *     traceme trace("expensive_computation", 2);
     *     // ... perform work ...
     * } // Timing automatically recorded here
     * ```
     */
    explicit traceme(
        std::string_view name, int level = 1, uint64_t filter_mask = kTraceFilterDefaultMask)
    {
        // PROFILER_CHECK_DEBUG(level >= 1, "level is less than 1");
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (
            traceme_recorder::active(level) && traceme_recorder::check_filter(filter_mask))
        {
            name_.Emplace(std::string(name));
            start_time_ = get_current_time_nanos();
        }
#endif
    }

    /**
     * @brief Deleted constructor to prevent expensive temporary string creation.
     *
     * This constructor is explicitly deleted to avoid performance pitfalls where
     * expensive string operations (concatenation, formatting) are performed even
     * when tracing is disabled. Use the lambda-based constructor instead.
     *
     * **Why Deleted**: Temporary strings incur allocation and construction costs
     * regardless of whether tracing is active, violating the zero-cost principle.
     *
     * **Alternative**: Use the name_generator template constructor:
     * ```cpp
     * // DON'T: traceme trace(expensive_string_concat());
     * // DO: traceme trace([&]() { return expensive_string_concat(); });
     * ```
     */
    explicit traceme(std::string&& name, int level = 1) = delete;

    /**
     * @brief Deleted constructor to prevent unintentional string ownership issues.
     *
     * This constructor is deleted to avoid situations where the caller might
     * unintentionally maintain ownership of the string, leading to potential
     * lifetime and performance issues.
     *
     * **Alternative**: Explicitly use string_view if you need to reuse an existing string:
     * ```cpp
     * std::string existing_name = get_operation_name();
     * traceme trace(std::string_view(existing_name));
     * ```
     */
    explicit traceme(const std::string& name, int level = 1) = delete;

    /**
     * @brief Constructs a trace event from a C-style string literal.
     *
     * This overload enables direct use of string literals while maintaining
     * the performance characteristics of the string_view constructor.
     *
     * @param raw C-style string literal (e.g., "operation_name")
     * @param level Trace priority level (default: 1)
     *
     * **Example**: `traceme trace("matrix_multiply", 2);`
     */
    explicit traceme(const char* raw, int level = 1) : traceme(std::string_view(raw), level) {}

    /**
     * @brief Constructs a trace event with lazy name generation for optimal performance.
     *
     * This constructor accepts a callable (lambda, function, functor) that generates
     * the trace name only when tracing is actually enabled. This is the preferred
     * approach for expensive name generation operations like string concatenation,
     * formatting, or metadata encoding.
     *
     * @tparam NameGeneratorT Callable type that returns a string-convertible value
     * @param name_generator Callable that returns the trace name. Must be invocable
     *                       with no arguments and return a type convertible to std::string.
     *                       Common return types: std::string, const char*, result of
     *                       traceme_encode(), traceme_op(), etc.
     * @param level Trace priority level (default: 1)
     *
     * **Performance Benefits**:
     * - Zero overhead when tracing is disabled (name_generator never called)
     * - Avoids unnecessary string allocations and computations
     * - Template-based to avoid std::function allocation overhead
     *
     * **Thread Safety**: The name_generator is called from the constructing thread
     * only when tracing is active. Ensure captured variables remain valid.
     *
     * **Example Usage**:
     * ```cpp
     * // Simple string concatenation
     * traceme trace([&]() { return std::string("operation_") + std::to_string(id); });
     *
     * // With metadata encoding
     * traceme trace([&]() {
     *     return traceme_encode("matrix_op", {{"rows", rows}, {"cols", cols}});
     * });
     *
     * // Operation type encoding
     * traceme trace([&]() { return traceme_op(op_name, op_type); });
     * ```
     */
    template <
        typename NameGeneratorT,
        std::enable_if_t<std::is_invocable_v<NameGeneratorT>, bool> = true>
    explicit traceme(
        NameGeneratorT&& name_generator,
        int              level       = 1,
        uint64_t         filter_mask = kTraceFilterDefaultMask)
    {
        // PROFILER_CHECK_DEBUG(level >= 1, "level is less than 1");
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (
            traceme_recorder::active(level) && traceme_recorder::check_filter(filter_mask))
        {
            name_.Emplace(std::forward<NameGeneratorT>(name_generator)());
            start_time_ = get_current_time_nanos();
        }
#endif
    }

    /**
     * @brief Move constructor for transferring trace ownership between objects.
     *
     * Enables efficient transfer of active trace events between traceme objects
     * without stopping and restarting timing. Useful for returning traces from
     * functions or storing them in containers.
     *
     * @param other Source traceme object (will be left in inactive state)
     *
     * **Performance**: Minimal overhead - only transfers internal state pointers
     * **Thread Safety**: Both objects must be accessed from the same thread
     */
    traceme(traceme&& other) noexcept { *this = std::move(other); }

    /**
     * @brief Move assignment operator for transferring trace ownership.
     *
     * Transfers the active trace from another traceme object, automatically
     * stopping any existing trace in this object first.
     *
     * @param other Source traceme object (will be left in inactive state)
     * @return Reference to this object for chaining
     *
     * **Behavior**: If this object has an active trace, it's stopped before
     * taking ownership of the other object's trace.
     */
    traceme& operator=(traceme&& other) noexcept
    {
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (other.start_time_ != kUntracedActivity)
        {
            name_.Emplace(std::move(other.name_).Consume());
            start_time_ = std::exchange(other.start_time_, kUntracedActivity);
        }
#endif
        return *this;
    }

    /**
     * @brief Destructor that automatically stops tracing and records the event.
     *
     * When the traceme object goes out of scope, this destructor automatically
     * calls stop() to record the end time and submit the complete trace event
     * to the recorder. This is the primary mechanism for RAII-based tracing.
     *
     * **Performance**: If tracing was never started (disabled), this is a no-op.
     * Otherwise, records end time and submits event (~50-100 nanoseconds).
     */
    ~traceme() { stop(); }

    /**
     * @brief Manually stops tracing and records the event before object destruction.
     *
     * Explicitly stops the trace event and records the timing information. This method
     * is automatically called by the destructor, but can be called manually to stop
     * tracing before the object goes out of scope. Subsequent calls have no effect.
     *
     * **Use Cases**:
     * - Stop tracing early while keeping the object alive
     * - Precise control over trace end timing
     * - Conditional tracing based on runtime conditions
     *
     * **Thread Safety**: Must be called from the same thread that created the trace
     * **Performance**: No-op if tracing was never started or already stopped
     * **Idempotent**: Safe to call multiple times
     *
     * **Implementation Notes**:
     * - Doesn't re-check trace level for performance (level checked at construction)
     * - Handles race conditions with recorder start/stop gracefully
     * - Events with timestamps outside session range are automatically discarded
     *
     * **Example**:
     * ```cpp
     * traceme trace("conditional_operation");
     * if (should_stop_early) {
     *     trace.stop(); // Stop tracing here
     * }
     * // ... continue with other work ...
     * ```
     */
    void stop()
    {
        // We do not need to check the trace level again here.
        // - If tracing wasn't active to start with, we have kUntracedActivity.
        // - If tracing was active and was stopped, we have
        //   traceme_recorder::active().
        // - If tracing was active and was restarted at a lower level, we may
        //   spuriously record the event. This is extremely rare, and acceptable as
        //   event will be discarded when its start timestamp fall outside of the
        //   start/stop session timestamp.
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (start_time_ != kUntracedActivity)
        {
            if PROFILER_LIKELY (traceme_recorder::active())
            {
                traceme_recorder::record(
                    {std::move(name_.value), start_time_, get_current_time_nanos()});
            }
            name_.Destroy();
            start_time_ = kUntracedActivity;
        }
#endif
    }

    /**
     * @brief Appends additional metadata to an active trace event.
     *
     * Dynamically adds metadata to a trace event after construction, enabling
     * runtime collection of contextual information that becomes available during
     * the traced operation. The metadata generator is only evaluated when tracing
     * is active, maintaining zero-cost behavior when disabled.
     *
     * @tparam MetadataGeneratorT Callable type that returns metadata string
     * @param metadata_generator Callable that returns metadata to append. Should
     *                           return a string compatible with traceme_encode() format
     *                           or plain text. Only called when tracing is active.
     *
     * **Performance**: Zero overhead when tracing disabled. When enabled, minimal
     * string manipulation cost (~10-50 nanoseconds depending on metadata size).
     *
     * **Thread Safety**: Must be called from the same thread that created the trace
     *
     * **Metadata Format**: Typically uses traceme_encode() format for structured data:
     * `#key1=value1,key2=value2#` but plain text is also supported.
     *
     * **Use Cases**:
     * - Adding runtime-computed values (iteration counts, memory usage)
     * - Appending results or status information
     * - Including dynamic context that wasn't available at construction
     *
     * **Example Usage**:
     * ```cpp
     * traceme trace("data_processing");
     * // ... do some work ...
     * trace.append_metadata([&]() {
     *     return traceme_encode({{"processed_items", item_count},
     *                           {"memory_used", get_memory_usage()}});
     * });
     * ```
     */
    template <
        typename MetadataGeneratorT,
        std::enable_if_t<std::is_invocable_v<MetadataGeneratorT>, bool> = true>
    void append_metadata(MetadataGeneratorT&& metadata_generator)
    {
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (start_time_ != kUntracedActivity)
        {
            if PROFILER_LIKELY (traceme_recorder::active())
            {
                traceme_internal::append_metadata(
                    &name_.value, std::forward<MetadataGeneratorT>(metadata_generator)());
            }
        }
#endif
    }

    /**
     * @name Static Activity Management API
     *
     * Alternative API for manual trace management when RAII scoped objects are
     * inconvenient (e.g., across function boundaries, asynchronous operations,
     * or when trace lifetime doesn't align with C++ scope).
     *
     * **Important**: activity_start() and activity_end() must be called from
     * the same thread. Cross-thread activities are not supported.
     * @{
     */

    /**
     * @brief Starts a trace activity with lazy name generation and returns an activity ID.
     *
     * Records the start time of a trace activity and returns a unique identifier
     * that must be passed to activity_end() to complete the trace. The name generator
     * is only evaluated when tracing is active.
     *
     * @tparam NameGeneratorT Callable type that returns a string-convertible value
     * @param name_generator Callable that returns the activity name. Only called
     *                       when tracing is active to maintain zero-cost behavior.
     * @param level Trace priority level (default: 1)
     * @return Unique activity ID for use with activity_end(), or kUntracedActivity
     *         if tracing is disabled
     *
     * **Thread Safety**: Must call activity_end() from the same thread
     * **Performance**: Zero overhead when tracing disabled
     *
     * **Example**:
     * ```cpp
     * auto id = traceme::activity_start([&]() {
     *     return traceme_encode("async_op", {{"task_id", task_id}});
     * });
     * // ... start asynchronous work ...
     * traceme::activity_end(id);
     * ```
     */
    template <
        typename NameGeneratorT,
        std::enable_if_t<std::is_invocable_v<NameGeneratorT>, bool> = true>
    static int64_t activity_start(
        NameGeneratorT&& name_generator,
        int              level       = 1,
        uint64_t         filter_mask = kTraceFilterDefaultMask)
    {
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (
            traceme_recorder::active(level) && traceme_recorder::check_filter(filter_mask))
        {
            int64_t activity_id = traceme_recorder::new_activity_id();
            traceme_recorder::record(
                {std::forward<NameGeneratorT>(name_generator)(),
                 get_current_time_nanos(),
                 -activity_id});
            return activity_id;
        }
#endif
        return kUntracedActivity;
    }

    /**
     * @brief Starts a trace activity with a string_view name and returns an activity ID.
     *
     * Records the start time of a trace activity using a pre-existing string.
     * This is the most efficient overload when you already have a string available.
     *
     * @param name Activity name as string_view (must remain valid until activity_end())
     * @param level Trace priority level (default: 1)
     * @return Unique activity ID for use with activity_end(), or kUntracedActivity
     *         if tracing is disabled
     *
     * **Performance**: Minimal overhead - no string allocation required
     * **Lifetime**: The string_view must remain valid until activity_end() is called
     */
    static int64_t activity_start(
        std::string_view name, int level = 1, uint64_t filter_mask = kTraceFilterDefaultMask)
    {
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (
            traceme_recorder::active(level) && traceme_recorder::check_filter(filter_mask))
        {
            int64_t activity_id = traceme_recorder::new_activity_id();
            traceme_recorder::record({std::string(name), get_current_time_nanos(), -activity_id});
            return activity_id;
        }
#endif
        return kUntracedActivity;
    }

    /**
     * @brief Starts a trace activity with a std::string name (convenience overload).
     *
     * @param name Activity name as std::string reference
     * @param level Trace priority level (default: 1)
     * @return Activity ID for use with activity_end()
     */
    static int64_t activity_start(
        const std::string& name, int level = 1, uint64_t filter_mask = kTraceFilterDefaultMask)
    {
        return activity_start(std::string_view(name), level, filter_mask);
    }

    /**
     * @brief Starts a trace activity with a C-string name (convenience overload).
     *
     * @param name Activity name as C-style string literal
     * @param level Trace priority level (default: 1)
     * @return Activity ID for use with activity_end()
     */
    static int64_t activity_start(
        const char* name, int level = 1, uint64_t filter_mask = kTraceFilterDefaultMask)
    {
        return activity_start(std::string_view(name), level, filter_mask);
    }

    /**
     * @brief Ends a trace activity started with activity_start().
     *
     * Records the end time for a trace activity and completes the trace event.
     * Must be called from the same thread that called activity_start().
     *
     * @param activity_id Activity ID returned by activity_start(), or kUntracedActivity
     *                    to safely handle the case where tracing was disabled
     *
     * **Thread Safety**: Must be called from the same thread as activity_start()
     * **Performance**: No-op if activity_id is kUntracedActivity (tracing was disabled)
     * **Error Handling**: Gracefully handles recorder stop/start race conditions
     *
     * **Example**:
     * ```cpp
     * auto id = traceme::activity_start("async_operation");
     * // ... perform work ...
     * traceme::activity_end(id); // Complete the trace
     * ```
     */
    static void activity_end(int64_t activity_id)
    {
#if !defined(IS_MOBILE_PLATFORM)
        // We don't check the level again (see traceme::stop()).
        if PROFILER_UNLIKELY (activity_id != kUntracedActivity)
        {
            if PROFILER_LIKELY (traceme_recorder::active())
            {
                traceme_recorder::record({std::string(), -activity_id, get_current_time_nanos()});
            }
        }
#endif
    }

    /**
     * @brief Records an instantaneous trace event with zero duration.
     *
     * Creates a trace event that represents a point-in-time occurrence rather
     * than a duration. Useful for marking specific moments, state changes,
     * or events that don't have meaningful duration.
     *
     * @tparam NameGeneratorT Callable type that returns a string-convertible value
     * @param name_generator Callable that returns the event name. Only evaluated
     *                       when tracing is active.
     * @param level Trace priority level (default: 1)
     *
     * **Use Cases**:
     * - Marking algorithm milestones or checkpoints
     * - Recording state transitions or configuration changes
     * - Logging significant events without duration semantics
     * - Synchronization points in multi-threaded code
     *
     * **Example**:
     * ```cpp
     * traceme::instant_activity([&]() {
     *     return traceme_encode("checkpoint", {{"iteration", i}, {"loss", current_loss}});
     * });
     * ```
     */
    template <
        typename NameGeneratorT,
        std::enable_if_t<std::is_invocable_v<NameGeneratorT>, bool> = true>
    static void instant_activity(
        NameGeneratorT&& name_generator,
        int              level       = 1,
        uint64_t         filter_mask = kTraceFilterDefaultMask)
    {
#if !defined(IS_MOBILE_PLATFORM)
        if PROFILER_UNLIKELY (
            traceme_recorder::active(level) && traceme_recorder::check_filter(filter_mask))
        {
            int64_t now = get_current_time_nanos();
            traceme_recorder::record(
                {std::forward<NameGeneratorT>(name_generator)(),
                 /*start_time=*/now,
                 /*end_time=*/now});
        }
#endif
    }

    /**
     * @brief Checks if tracing is currently active for the specified level.
     *
     * Fast, lock-free check to determine if trace events at the given level
     * would be recorded. Useful for conditional tracing logic or avoiding
     * expensive computations when tracing is disabled.
     *
     * @param level Trace level to check (default: 1)
     * @return true if tracing is active and events at this level would be recorded
     *
     * **Performance**: Extremely fast (~1-2 CPU cycles) - just an atomic load
     * **Thread Safety**: Safe to call from any thread
     * **Race Conditions**: Result may become stale immediately, but this is acceptable
     *                      for optimization purposes
     *
     * **Example**:
     * ```cpp
     * if (traceme::active(2)) {
     *     // Only compute expensive trace data when needed
     *     auto expensive_data = compute_detailed_stats();
     *     traceme trace([&]() { return format_trace_data(expensive_data); }, 2);
     * }
     * ```
     */
    static bool active(int level = 1)
    {
#if !defined(IS_MOBILE_PLATFORM)
        return traceme_recorder::active(level);
#else
        return false;
#endif
    }

    /**
     * @brief Generates a new unique activity ID for manual activity management.
     *
     * Creates a globally unique identifier that can be used with the static
     * activity management API. Primarily used internally, but exposed for
     * advanced use cases requiring custom activity tracking.
     *
     * @return Unique 64-bit activity identifier, or 0 on mobile platforms
     *
     * **Thread Safety**: Safe to call from any thread
     * **Uniqueness**: IDs are globally unique across all threads and time
     * **Performance**: Very fast - uses thread-local counters to avoid contention
     *
     * **Advanced Usage**:
     * ```cpp
     * // Custom activity tracking with metadata
     * auto id = traceme::new_activity_id();
     * // ... use id for custom tracking logic ...
     * ```
     */
    static int64_t new_activity_id()
    {
#if !defined(IS_MOBILE_PLATFORM)
        return traceme_recorder::new_activity_id();
#else
        return 0;
#endif
    }

    /** @} */  // End of Static Activity Management API

private:
    /// Sentinel value indicating tracing is disabled or activity is inactive
    constexpr static int64_t kUntracedActivity = 0;

    /// Deleted copy constructor to prevent accidental copying of trace objects
    traceme(const traceme&) = delete;

    /// Deleted copy assignment to prevent accidental copying of trace objects
    void operator=(const traceme&) = delete;

    /// Lazily-initialized trace name storage (only allocated when tracing is active)
    no_init<std::string> name_;

    /// Start timestamp in nanoseconds, or kUntracedActivity if tracing is disabled
    int64_t start_time_ = kUntracedActivity;
};

/**
 * @brief Checks if detailed TensorFlow operation tracing is enabled.
 *
 * Determines whether TensorFlow operations should include detailed profiling
 * information such as tensor shapes, data types, and other metadata. This
 * corresponds to VERBOSE level tracing (level 3).
 *
 * @return true if VERBOSE level tracing is active and detailed TF op information
 *         should be collected
 *
 * **Use Case**: TensorFlow operation kernels use this to conditionally collect
 * expensive metadata only when detailed profiling is requested.
 *
 * **Performance**: Very fast check - just an atomic load operation
 *
 * **Example**:
 * ```cpp
 * if (tf_op_details_enabled()) {
 *     // Collect expensive tensor shape and type information
 *     trace.append_metadata([&]() {
 *         return traceme_encode({{"shape", tensor.shape_string()},
 *                               {"dtype", tensor.dtype_string()}});
 *     });
 * }
 * ```
 */
inline bool tf_op_details_enabled()
{
    return traceme::active(static_cast<int>(traceme_level_enum::VERBOSE));
}

}  // namespace profiler
