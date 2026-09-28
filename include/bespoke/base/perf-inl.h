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

#if defined(__ANDROID__) || defined(__linux__)

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#endif /* __ANDROID__ || __linux__ */

#include <limits>

#include "bespoke/base/perf.h"

namespace profiler::profiler_impl::impl::linux_perf
{

/*
 * PerfEvent
 * ---------
 */

inline void PerfEvent::Disable() const
{
#if defined(__ANDROID__) || defined(__linux__)
    ioctl(fd_, PERF_EVENT_IOC_DISABLE, 0);
#endif /* __ANDROID__ || __linux__ */
}

inline void PerfEvent::Enable() const
{
#if defined(__ANDROID__) || defined(__linux__)
    ioctl(fd_, PERF_EVENT_IOC_ENABLE, 0);
#endif /* __ANDROID__ || __linux__ */
}

inline void PerfEvent::Reset() const
{
#if defined(__ANDROID__) || defined(__linux__)
    ioctl(fd_, PERF_EVENT_IOC_RESET, 0);
#endif /* __ANDROID__ || __linux__ */
}

/*
 * PerfProfiler
 * ------------
 */

inline uint64_t PerfProfiler::CalcDelta(uint64_t start, uint64_t end) const
{
    if (end < start)
    {  // overflow
        return end + (std::numeric_limits<uint64_t>::max() - start);
    }
    // not possible to wrap around start for a 64b cycle counter
    return end - start;
}

inline void PerfProfiler::StartCounting() const
{
    for (auto& e : events_)
    {
        e.Enable();
    }
}

inline void PerfProfiler::StopCounting() const
{
    for (auto& e : events_)
    {
        e.Disable();
    }
}

}  // namespace profiler::profiler_impl::impl::linux_perf
