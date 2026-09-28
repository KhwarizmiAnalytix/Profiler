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
#include <fmt/format.h>

#include <optional>
#include <stdexcept>

namespace profiler::unwind
{

struct UnwindError : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

#define UNWIND_CHECK(cond, fmtstring, ...)                                                         \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            throw unwind::UnwindError(                                                             \
                fmt::format("{}:{}: " fmtstring, __FILE__, __LINE__, ##__VA_ARGS__));              \
        }                                                                                          \
    } while (0)

// #define LOG_INFO(...) fmt::print(__VA_ARGS__)
#define LOG_INFO(...)

// #define PRINT_INST(...) LOG_INFO(__VA_ARGS__)
#define PRINT_INST(...)

// #define PRINT_LINE_TABLE(...) LOG_INFO(__VA_ARGS__)
#define PRINT_LINE_TABLE(...)

}  // namespace profiler::unwind
