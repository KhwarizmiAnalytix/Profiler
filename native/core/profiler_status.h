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

#include <string>
#include <utility>

namespace profiler
{

class profiler_status
{
public:
    static profiler_status Ok() { return profiler_status(true, {}); }
    static profiler_status Error(std::string message)
    {
        return profiler_status(false, std::move(message));
    }

    bool               ok() const { return ok_; }
    const std::string& message() const { return message_; }

    explicit operator bool() const { return ok_; }

private:
    profiler_status(bool ok, std::string message) : ok_(ok), message_(std::move(message)) {}

    bool        ok_;
    std::string message_;
};

}  // namespace profiler
