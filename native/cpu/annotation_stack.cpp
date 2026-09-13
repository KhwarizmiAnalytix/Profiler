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

#include "native/cpu/annotation_stack.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "common/profiler_macros.h"

namespace profiler::profiler_impl
{

/**
 * @brief Get the thread-local annotation data for the given generation.
 *
 * This function maintains thread-local storage for:
 * - Stack of string positions (for efficient pop operations)
 * - Concatenated annotation string
 * - Stack of scope range IDs
 *
 * When the generation changes (enable/disable), the data is reset.
 *
 * @param atomic The generation atomic variable
 * @return Tuple of (stack, string, scope_range_id_stack) pointers
 */
static auto get_annotation_data(const std::atomic<int>& atomic)
{
    static thread_local struct
    {
        int                  generation = 0;
        std::vector<size_t>  stack;
        std::string          string;
        std::vector<int64_t> scope_range_id_stack;
    } data;

    int const generation = atomic.load(std::memory_order_acquire);
    if (generation != data.generation)
    {
        // Generation changed (enable/disable), reset the data
        data = {generation, {}, {}, {}};
    }

    return std::make_tuple(&data.stack, &data.string, &data.scope_range_id_stack);
}

void annotation_stack::push_annotation(std::string_view name)
{
    // Global counter for generating unique scope range IDs
    static std::atomic<int64_t> scope_range_counter = 0;

    auto [stack, string, scope_range_id_stack] = get_annotation_data(generation_);

    // Save the current string size for efficient pop
    stack->push_back(string->size());

    // Append the annotation with "::" separator
    if (!string->empty())
    {
        string->append("::");
        string->append(name);
    }
    else
    {
        string->assign(name);
    }

    // Generate a unique scope range ID
    int64_t scope_range_id = scope_range_counter.fetch_add(1, std::memory_order_relaxed) + 1;

    // Handle overflow (extremely unlikely, but be safe)
    if PROFILER_UNLIKELY (scope_range_id == 0)
    {
        scope_range_id = scope_range_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    scope_range_id_stack->push_back(scope_range_id);
}

void annotation_stack::pop_annotation()
{
    auto [stack, string, scope_range_id_stack] = get_annotation_data(generation_);

    if (stack->empty())
    {
        // Stack is empty, clear everything
        string->clear();
        scope_range_id_stack->clear();
        return;
    }

    // Restore the string to the size before the last push
    string->resize(stack->back());
    stack->pop_back();
    scope_range_id_stack->pop_back();
}

const std::string& annotation_stack::get()
{
    return *std::get<1>(get_annotation_data(generation_));
}

const std::vector<int64_t>& annotation_stack::get_scope_range_ids()
{
    auto* vec = std::get<2>(get_annotation_data(generation_));
    return *vec;
}

void annotation_stack::enable(bool enable)
{
    int generation = generation_.load(std::memory_order_relaxed);

    // Use compare_exchange to atomically update the generation
    // If enable is true, set the LSB to 1 (odd = enabled)
    // If enable is false, increment and clear the LSB (even = disabled)
    while (!generation_.compare_exchange_weak(
        generation, enable ? (generation | 1) : ((generation + 1) & ~1), std::memory_order_release))
    {
        // Retry if another thread modified generation
    }
}

// annotation_stack::generation_ implementation must be lock-free for faster
// execution of the scoped annotation API.
std::atomic<int> annotation_stack::generation_{0};
static_assert(ATOMIC_INT_LOCK_FREE == 2, "Assumed atomic<int> was lock free");

}  // namespace profiler::profiler_impl
