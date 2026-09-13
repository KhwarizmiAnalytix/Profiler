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

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace strings
{
namespace internal
{
template <typename T>
void to_string_helper(std::ostringstream& oss, const T& val)
{
    oss << val;
}
}  // namespace internal

template <typename... Args>
std::string str_cat(const Args&... args)
{
    std::ostringstream oss;
    (internal::to_string_helper(oss, args), ...);
    return oss.str();
}

template <typename... Args>
void str_append(std::string* result, const Args&... args)
{
    if (result == nullptr)
    {
        return;
    }

    std::ostringstream oss;
    (internal::to_string_helper(oss, args), ...);
    *result += oss.str();
}

inline std::string to_lower(std::string_view input)
{
    std::string result(input);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}
}  // namespace strings
