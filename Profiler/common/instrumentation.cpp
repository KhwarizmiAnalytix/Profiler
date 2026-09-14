/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/instrumentation.h"

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include "bespoke/common/orchestration/observer.h"
#endif

namespace profiler
{

void report_memory_usage(
    void*   ptr,
    int64_t alloc_size,
    size_t  total_allocated,
    size_t  total_reserved,
    int16_t device_type,
    int16_t device_index)
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    auto* state = profiler_impl::impl::ProfilerStateBase::get();
    if (state == nullptr || !state->memoryProfilingEnabled())
    {
        return;
    }

    device_option device{};
    device.index_ = device_index;
    device.type_  = static_cast<device_enum>(device_type);
    state->reportMemoryUsage(ptr, alloc_size, total_allocated, total_reserved, device);
#else
    (void)ptr;
    (void)alloc_size;
    (void)total_allocated;
    (void)total_reserved;
    (void)device_type;
    (void)device_index;
#endif
}

void report_out_of_memory(
    int64_t alloc_size,
    size_t  total_allocated,
    size_t  total_reserved,
    int16_t device_type,
    int16_t device_index)
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    auto* state = profiler_impl::impl::ProfilerStateBase::get();
    if (state == nullptr || !state->memoryProfilingEnabled())
    {
        return;
    }

    device_option device{};
    device.index_ = device_index;
    device.type_  = static_cast<device_enum>(device_type);
    state->reportOutOfMemory(alloc_size, total_allocated, total_reserved, device);
#else
    (void)alloc_size;
    (void)total_allocated;
    (void)total_reserved;
    (void)device_type;
    (void)device_index;
#endif
}

bool memory_profiling_active()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    auto* state = profiler_impl::impl::ProfilerStateBase::get();
    return state != nullptr && state->memoryProfilingEnabled();
#else
    return false;
#endif
}

}  // namespace profiler
