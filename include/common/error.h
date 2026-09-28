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

#include <cerrno>
#include <cstring>
#include <string>

// =============================================================================
// ERROR HANDLING UTILITIES
// =============================================================================

namespace profiler
{
namespace utils
{

/**
 * @brief Thread-safe wrapper for strerror to convert errno to string
 * @param err_num The error number (typically errno)
 * @return String description of the error
 * @note This function is thread-safe across all platforms
 * @note Uses platform-specific strerror_r variants for thread safety
 */
inline std::string str_error(int err_num)
{
    char buffer[256];

#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    // GNU version of strerror_r returns char*
    char* result = strerror_r(err_num, buffer, sizeof(buffer));
    return std::string(result);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||                        \
    (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L)
    // POSIX version of strerror_r returns int
    int result = strerror_r(err_num, buffer, sizeof(buffer));
    if (result == 0)
    {
        return std::string(buffer);
    }
    else
    {
        return "Unknown error " + std::to_string(err_num);
    }
#elif defined(_WIN32)
    // Windows version uses strerror_s
    if (strerror_s(buffer, sizeof(buffer), err_num) == 0)
    {
        return std::string(buffer);
    }
    else
    {
        return "Unknown error " + std::to_string(err_num);
    }
#else
    // Fallback to non-thread-safe strerror
    // This is not ideal but works as a last resort
    const char* result = strerror(err_num);
    return result ? std::string(result) : "Unknown error " + std::to_string(err_num);
#endif
}

}  // namespace utils
}  // namespace profiler
