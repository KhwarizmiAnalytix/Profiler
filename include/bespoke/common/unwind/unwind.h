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
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/profiler_export.h"

namespace profiler::unwind
{
// gather current stack, relatively fast.
// gets faster once the cache of program counter locations is warm.
PROFILER_API std::vector<void*> unwind();

struct Frame
{
    std::string filename;
    std::string funcname;
    uint64_t    lineno;
};

enum class Mode : std::uint8_t
{
    addr2line,
    fast,
    dladdr
};

// note: symbolize is really slow
// it will launch an addr2line process that has to parse dwarf
// information from the libraries that frames point into.
// Callers should first batch up all the unique void* pointers
// across a number of unwind states and make a single call to
// symbolize.
PROFILER_API std::vector<Frame> symbolize(const std::vector<void*>& frames, Mode mode);

// returns path to the library, and the offset of the addr inside the library
PROFILER_API std::optional<std::pair<std::string, uint64_t>> libraryFor(void* addr);

struct Stats
{
    size_t hits        = 0;
    size_t misses      = 0;
    size_t unsupported = 0;
    size_t resets      = 0;
};
Stats stats();

}  // namespace profiler::unwind
