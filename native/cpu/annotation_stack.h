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

/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/profiler_export.h"
#include "common/profiler_macros.h"

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Backend for scoped annotations with hierarchical tracking.
 *
 * This class provides a thread-local stack of annotations that can be
 * pushed and popped to create hierarchical profiling scopes. Annotations
 * are concatenated with "::" separator to form a full scope path.
 *
 * **Thread Safety**: Each thread has its own annotation stack (thread-local).
 * The enable/disable state is global and atomic.
 *
 * **Example**:
 * ```cpp
 * annotation_stack::push_annotation("outer");
 * // Stack: "outer"
 * annotation_stack::push_annotation("inner");
 * // Stack: "outer::inner"
 * annotation_stack::pop_annotation();
 * // Stack: "outer"
 * annotation_stack::pop_annotation();
 * // Stack: ""
 * ```
 */
class annotation_stack
{
public:
    /**
     * @brief Push an annotation onto the stack for the current thread.
     *
     * Appends the name to the annotation stack, separated by "::".
     * The separator "::" is chosen to avoid conflicts with common naming patterns.
     *
     * @param name Annotation name to push
     */
    PROFILER_API static void push_annotation(std::string_view name);

    /**
     * @brief Pop the most recent annotation from the stack.
     *
     * Removes the last annotation that was pushed onto the stack for
     * the current thread.
     */
    PROFILER_API static void pop_annotation();

    /**
     * @brief Get the full annotation string for the current thread.
     *
     * Returns the concatenated annotation stack as a single string,
     * with annotations separated by "::".
     *
     * @return Reference to the annotation string
     */
    PROFILER_API static const std::string& get();

    /**
     * @brief Get the scope range IDs for the current thread's stack.
     *
     * Each annotation push generates a unique scope range ID that can be
     * used to correlate events across different profiling data sources.
     *
     * @return Reference to scope range ID vector (one entry per stack level)
     */
    PROFILER_API static const std::vector<int64_t>& get_scope_range_ids();

    /**
     * @brief Enable or disable the annotation stack globally.
     *
     * When disabled, the annotation stack is cleared for all threads.
     * This is useful for reducing overhead when profiling is not active.
     *
     * @param enable true to enable, false to disable
     */
    PROFILER_API static void enable(bool enable);

    /**
     * @brief Check if the annotation stack is currently enabled.
     *
     * @return true if enabled, false if disabled
     */
    static bool is_enabled() { return generation_.load(std::memory_order_acquire) & 1; }

private:
    annotation_stack() = default;

    /**
     * @brief Generation counter for enable/disable state.
     *
     * Enabled if odd, disabled if even. The value is incremented for every
     * call to enable() that changes the enabled state. This allows thread-local
     * data to detect when the state has changed and clear itself.
     */
    PROFILER_API static std::atomic<int> generation_;
};

}  // namespace profiler_impl
}  // namespace profiler
