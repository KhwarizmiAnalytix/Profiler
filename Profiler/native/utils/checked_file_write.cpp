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

#include "native/utils/checked_file_write.h"

#include <cstdio>
#include <fstream>

namespace profiler
{
namespace profiler_impl
{

bool write_file_checked(const std::string& path, const std::string& content)
{
    std::string const temp_path = path + ".tmp";

    {
        std::ofstream file(temp_path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }
        file << content;
        file.flush();
        if (!file.good())
        {
            file.close();
            std::remove(temp_path.c_str());
            return false;
        }
        file.close();
        if (!file.good())
        {
            std::remove(temp_path.c_str());
            return false;
        }
    }

    // Publish atomically: rename replaces the destination in one step (POSIX and
    // Windows both support same-volume renames), so a reader never observes a
    // partially-written file, and a failure here leaves any existing valid
    // destination untouched instead of a half-written one.
    if (std::rename(temp_path.c_str(), path.c_str()) != 0)
    {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

bool publish_external_temp_file(const std::string& path)
{
    std::string const temp_path = path + ".tmp";

    {
        std::ifstream probe(temp_path, std::ios::binary);
        if (!probe.is_open())
        {
            return false;
        }
    }

    if (std::rename(temp_path.c_str(), path.c_str()) != 0)
    {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

}  // namespace profiler_impl
}  // namespace profiler
