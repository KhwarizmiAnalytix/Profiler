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

namespace profiler
{

enum class device_enum : int16_t
{
    CPU         = 0,
    CUDA        = 1,
    HIP         = 2,
    PrivateUse1 = 3
};

struct device_option
{
    using int_t        = int16_t;
    int_t       index_ = -1;
    device_enum type_{};

    device_enum type() const noexcept { return type_; }
    int_t       index() const noexcept { return index_; }

    bool operator==(const device_option& other) const noexcept
    {
        return type_ == other.type_ && index_ == other.index_;
    }
};

}  // namespace profiler
