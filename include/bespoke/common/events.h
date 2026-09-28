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

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace profiler::profiler_impl
{

/* A vector type to hold a list of performance counters */
using perf_counters_t = std::vector<uint64_t>;

/* Standard list of performance events independent of hardware or backend */
constexpr std::array<const char*, 2> ProfilerPerfEvents = {
    /*
     * Number of Processing Element (PE) cycles between two points of interest
     * in time. This should correlate positively with wall-time. Measured in
     * uint64_t. PE can be non cpu. TBD reporting behavior for multiple PEs
     * participating (i.e. threadpool).
     */
    "cycles",

    /* Number of PE instructions between two points of interest in time. This
     * should correlate positively with wall time and the amount of computation
     * (i.e. work). Across repeat executions, the number of instructions should
     * be more or less invariant. Measured in uint64_t. PE can be non cpu.
     */
    "instructions"};
}  // namespace profiler::profiler_impl
