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

#include "common/profiler_export.h"
#include "common/profiler_macros.h"

namespace profiler
{

/**
 * RAII annotation that records into the native session and whichever
 * instrumentation backend the active session/capture started. Inactive
 * collectors are no-ops.
 */
class PROFILER_VISIBILITY annotation
{
public:
    PROFILER_API annotation(std::string name, bool is_function, const char* file, int line);
    PROFILER_API ~annotation();
    annotation(const annotation&)            = delete;
    annotation& operator=(const annotation&) = delete;
    annotation(annotation&&)                 = delete;
    annotation& operator=(annotation&&)      = delete;

    // Test-only observability hook (mirrors profiler_scope::data()): exposes the
    // pooled impl's address so tests can confirm construct/destroy cycles recycle
    // the same block instead of calling the global allocator every time. nullptr
    // when this annotation is inactive (impl_ was never constructed).
    const void* debug_impl_address() const noexcept { return static_cast<const void*>(impl_); }

private:
    class impl;
    // Pool-owned raw pointer (see annotation.cpp's thread-local freelist): impl
    // storage is recycled rather than heap-allocated per scope, so ownership here
    // is manual instead of via unique_ptr. Deliberate exception to this project's
    // RAII/smart-pointer-first policy -- impl_ is exclusively owned by this
    // annotation and released in ~annotation(), never a general-purpose
    // non-owning pointer.
    impl* impl_ = nullptr;
};

}  // namespace profiler

#undef PROFILER_SCOPE
#undef PROFILER_FUNCTION
#undef PROFILER_OP
#undef PROFILER_PROFILE_SCOPE
#undef PROFILER_PROFILE_FUNCTION
#undef PROFILER_PROFILE_BLOCK
#undef PROFILER_RECORD_FUNCTION
#undef PROFILER_RECORD_USER_SCOPE

#define PROFILER_SCOPE(name)                                                                       \
    PROFILER_UNUSED profiler::annotation PROFILER_ANONYMOUS_VARIABLE(_profiler_scope_)(            \
        name, false, __FILE__, __LINE__)

#define PROFILER_FUNCTION()                                                                        \
    PROFILER_UNUSED profiler::annotation PROFILER_ANONYMOUS_VARIABLE(_profiler_fn_)(               \
        __FUNCTION__, true, __FILE__, __LINE__)

#define PROFILER_OP(name)                                                                          \
    PROFILER_UNUSED profiler::annotation PROFILER_ANONYMOUS_VARIABLE(_profiler_op_)(               \
        name, true, __FILE__, __LINE__)

#define PROFILER_PROFILE_SCOPE(name) PROFILER_SCOPE(name)
#define PROFILER_PROFILE_FUNCTION() PROFILER_FUNCTION()
#define PROFILER_RECORD_USER_SCOPE(fn) PROFILER_SCOPE(fn)
#define PROFILER_RECORD_FUNCTION(fn) PROFILER_OP(fn)

#define PROFILER_PROFILE_BLOCK(name)                                                               \
    if (PROFILER_UNUSED profiler::annotation PROFILER_ANONYMOUS_VARIABLE(_profiler_block_)(        \
            name, false, __FILE__, __LINE__);                                                      \
        true)
