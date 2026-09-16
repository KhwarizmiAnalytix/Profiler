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

#include <cstddef>
#include <cstdint>
#include <new>
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

namespace
{
// Bounds worst-case retained memory to ~kAnnotationPoolCap * sizeof(impl) per
// thread (tens of KB even in Kineto/ITT builds where impl is largest, per
// design-review.md section 6.6: "do not ... grow memory indefinitely").
constexpr std::size_t kAnnotationPoolCap = 64;

// Intrusive freelist node overlaid on freed, already-destructed impl storage.
struct annotation_pool_node
{
    annotation_pool_node* next_;
};

// Mirrors bespoke/common/collection.cpp's `thread_local SubQueueThreadCache
// sub_queue_cache_`: a single trivial, no-destructor POD. If thread-exit
// teardown order ever skips or reorders this thread_local's destruction,
// nothing dangerous happens -- worst case is a benign, bounded leak of
// already-freed blocks, never a dangling access, because nothing else in the
// codebase keys behavior off this object's lifetime or off impl's address
// (profiler_scope/RecordFunction do not maintain an address-keyed registry).
struct annotation_pool_state
{
    annotation_pool_node* head_;
    std::size_t           size_;
};

thread_local annotation_pool_state annotation_pool_cache_{nullptr, 0};

// Takes the block size as a parameter (rather than naming annotation::impl
// directly) so these pool helpers can live outside the class: annotation::impl
// is private, so only annotation's own member functions -- which call these
// helpers with sizeof(impl)/alignof(impl) already in hand -- have access to it.
void* acquire_impl_storage(std::size_t size)
{
    if (annotation_pool_cache_.head_ != nullptr)
    {
        annotation_pool_node* node   = annotation_pool_cache_.head_;
        annotation_pool_cache_.head_ = node->next_;
        --annotation_pool_cache_.size_;
        return static_cast<void*>(node);
    }
    // First-use/bounded slow path (measured separately from the steady-state
    // path per design-review.md section 6.6): falls back to the global
    // allocator only until this thread's pool has warmed up or after it has
    // been fully drained.
    return ::operator new(size);
}

void release_impl_storage(void* raw) noexcept
{
    if (annotation_pool_cache_.size_ >= kAnnotationPoolCap)
    {
        ::operator delete(raw);
        return;
    }
    auto* node                   = static_cast<annotation_pool_node*>(raw);
    node->next_                  = annotation_pool_cache_.head_;
    annotation_pool_cache_.head_ = node;
    ++annotation_pool_cache_.size_;
}
}  // namespace

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

    // Active path: recycle a pooled block instead of calling the global
    // allocator (design-review.md sections 1.1/6.2/6.6 -- no per-event heap
    // allocation on the registered, warmed-up static-label path).
    static_assert(
        alignof(impl) <= alignof(std::max_align_t),
        "annotation pool assumes impl fits ::operator new's default alignment; "
        "revisit with an aligned new/delete pair if impl ever gains overaligned members");
    void* raw = acquire_impl_storage(sizeof(impl));
    try
    {
        impl_ = ::new (raw) impl(std::move(name), is_function, file, line);
    }
    catch (...)
    {
        release_impl_storage(raw);
        throw;
    }
}

annotation::~annotation()
{
    if (impl_ != nullptr)
    {
        impl_->~impl();
        release_impl_storage(static_cast<void*>(impl_));
    }
}

}  // namespace profiler
