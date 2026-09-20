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

#pragma once

#include <cstddef>
#include <cstdint>

#include "common/device.h"
#include "common/profiler_export.h"
#include "common/profiler_macros.h"

// PROFILER_RECORD_FUNCTION / PROFILER_RECORD_USER_SCOPE
// (bespoke/common/record_function.h) and MemoryReportingInfoBase::reportMemoryUsage
// (bespoke/common/orchestration/observer.h) exist when PROFILER_HAS_KINETO or
// PROFILER_HAS_ITT is 1. Native (traceme/xplane) always compiles alongside that
// backend. Application code includes "profiler.h" and uses profiler::capture
// for Kineto/ITT/NVTX sessions. This header is the instrumentation surface:
// real PROFILER_RECORD_* under Kineto/ITT, no-op macros only if both HAS flags
// are 0 (not a supported CMake configuration).
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include "bespoke/common/record_function.h"
#define PROFILER_HAS_INSTRUMENTATION 1
#else
#define PROFILER_HAS_INSTRUMENTATION 0
#ifndef PROFILER_RECORD_FUNCTION
#define PROFILER_RECORD_FUNCTION(fn)                                                               \
    do                                                                                             \
    {                                                                                              \
        (void)(fn);                                                                                \
    } while (0)
#endif
#ifndef PROFILER_RECORD_USER_SCOPE
#define PROFILER_RECORD_USER_SCOPE(fn)                                                             \
    do                                                                                             \
    {                                                                                              \
        (void)(fn);                                                                                \
    } while (0)
#endif
#endif

namespace profiler
{

/**
 * @brief Reports one allocation/deallocation event to the active profiling
 * session, mirroring PyTorch's c10::reportMemoryUsageToProfiler. Safe to
 * call unconditionally from any allocator: a true no-op when no profiling
 * session is active or memory profiling was not requested.
 *
 * @param ptr Address returned by (or passed to) the allocator.
 * @param alloc_size Signed size of this allocation, negative for a
 *        deallocation, matching PyTorch's convention.
 * @param total_allocated Running total of bytes currently allocated.
 * @param total_reserved Running total of bytes reserved by the allocator's
 *        pool, including currently-unused blocks.
 * @param device_type One of profiler::device_enum's underlying values (CPU,
 *        CUDA, HIP, PrivateUse1).
 * @param device_index Device ordinal, or -1 if not applicable.
 */
PROFILER_API void report_memory_usage(void* ptr,
    int64_t                                 alloc_size,
    size_t                                  total_allocated,
    size_t                                  total_reserved,
    int16_t                                 device_type,
    int16_t                                 device_index);

/**
 * @brief Reports an allocator OOM to the active profiling session, mirroring
 * PyTorch's c10::reportOutOfMemoryToProfiler. No-op when no session is
 * active or memory profiling was not requested. Kineto emits an
 * `[OutOfMemory]` instant event; ITT/NVTX currently drop it.
 */
PROFILER_API void report_out_of_memory(int64_t alloc_size,
    size_t                                     total_allocated,
    size_t                                     total_reserved,
    int16_t                                    device_type,
    int16_t                                    device_index);

/**
 * @brief Cheap check for whether the active session wants memory events,
 * mirroring PyTorch's c10::memoryProfilingEnabled(). report_memory_usage()
 * is already a true no-op when profiling isn't active, but that check comes
 * too late for a caller that must gather data first (e.g. the CPU reporter
 * locking its size table). Such callers should check this first and skip
 * that work entirely when it returns false. GPU caching allocators report
 * the known block size under this same gate.
 */
PROFILER_API bool memory_profiling_active();

}  // namespace profiler

#include "common/annotation.h"
