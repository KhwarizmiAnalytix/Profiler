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
{
    // Cheap inactive gate (design-review.md Phase 2): check whether anything would
    // actually observe this scope *before* allocating impl_/its RecordFunction/its
    // native profiler_scope -- an inactive PROFILER_SCOPE call should cost one
    // pointer read plus one lock-free callback-table lookup, not two heap allocations.
    auto* session             = profiler::profiler_session::current_session();
    bool const native_active = session != nullptr && session->is_active();

    bool instrumentation_maybe_active = false;
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    RecordScope const scope = is_function ? RecordScope::FUNCTION : RecordScope::USER_SCOPE;
    instrumentation_maybe_active = getStepCallbacksUnlessEmpty(scope).has_value();
#endif

    if (!native_active && !instrumentation_maybe_active)
    {
        return;  // impl_ stays null; nothing wants to observe this scope.
    }

    impl_ = std::make_unique<impl>(std::move(name), is_function, file, line);
}

annotation::~annotation() = default;

}  // namespace profiler
