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

// Frees any blocks still pooled when this thread exits (Phase 6.F,
// design-review.md section 7's soak-testing ask: "Check post-warmup memory
// plateaus, handle/resource leaks"). kAnnotationPoolCap bounds worst-case
// retained memory *per thread*, but a process that creates many short-lived
// threads (e.g. a thread-pool-per-request pattern) previously leaked up to
// kAnnotationPoolCap * sizeof(impl) for every thread that ever used
// PROFILER_SCOPE, for the life of the process -- "bounded per thread" is not
// "bounded overall." Reproduced directly by a soak test spawning ~16,000
// ephemeral threads across 2,000 session start/stop cycles (RSS grew ~10MB;
// the same total PROFILER_SCOPE call volume across only 4 threads showed no
// growth, isolating thread count -- not call volume -- as the cause). The
// destructor only touches this object's own freelist and calls
// ::operator delete(), a globally available function with no dependency on
// any other thread_local's state, so it introduces no teardown-order hazard
// with sibling thread_locals (unlike bespoke/common/collection.cpp's
// `sub_queue_cache_`, which stays a trivial no-destructor POD -- that one's
// tradeoff is unchanged by this fix and not itself soak-tested here).
struct annotation_pool_state
{
    annotation_pool_node* head_;
    std::size_t           size_;

    ~annotation_pool_state()
    {
        while (head_ != nullptr)
        {
            annotation_pool_node* next = head_->next_;
            ::operator delete(head_);
            head_ = next;
        }
    }
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
