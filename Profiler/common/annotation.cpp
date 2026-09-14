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

#include "common/annotation.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "native/session/profiler.h"

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include "bespoke/common/record_function.h"
#endif

namespace profiler
{

class annotation::impl
{
public:
    impl(std::string name, bool is_function, const char* file, int line)
        : name_(std::move(name)), native_(name_)
    {
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
        rec_.emplace(is_function ? RecordScope::FUNCTION : RecordScope::USER_SCOPE);
        rec_->setSourceLocation(file, static_cast<uint32_t>(line));
        if (rec_->isActive())
        {
            rec_->before(name_);
        }
#else
        (void)is_function;
        (void)file;
        (void)line;
#endif
    }

private:
    std::string name_;
    profiler_scope native_;
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    std::optional<RecordFunction> rec_;
#endif
};

annotation::annotation(std::string name, bool is_function, const char* file, int line)
    : impl_(std::make_unique<impl>(std::move(name), is_function, file, line))
{
}

annotation::~annotation() = default;

}  // namespace profiler
