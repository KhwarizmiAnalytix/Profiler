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

/*
 * Regression tests for docs/design-review.md's scoped Phase 2: the inactive
 * PROFILER_SCOPE/profiler_scope allocation defect, and the default-preset
 * opt-in flip for statistical analysis. Phase 0's original reproduction used
 * a temporary standalone program with a global operator-new counter (see
 * design-review.md section 9); that technique isn't reused here since a
 * global allocator override in this shared, multi-file test binary risks
 * destabilizing every other test if any overload is missed. Instead this
 * checks the same underlying invariant directly: profiler_scope::data_ (the
 * allocation in question) is never constructed when inactive, which
 * profiler_scope::data() now exposes via its null-safe fallback.
 */

#include <cstdint>
#include <functional>

#include "ProfilerTest.h"
#include "native/session/profiler.h"
#include "profiler.h"

using namespace profiler;

// Finding: annotation::impl and profiler_scope both unconditionally allocated
// their heap state before checking whether any session was active (2
// allocations per PROFILER_SCOPE call, always). profiler_scope::start() now
// allocates data_ lazily, only once it knows a session is actually active;
// data() falls back to a static, default-constructed profiler_scope_data when
// data_ was never allocated, so an empty name_ here proves the real one never
// ran.
PROFILERTEST(ScopeOverhead, inactive_scope_never_allocates_data)
{
    profiler_session inactive_session;  // never started
    profiler_scope    scope("inactive_probe", &inactive_session);
    EXPECT_TRUE(scope.data().name_.empty());
}

PROFILERTEST(ScopeOverhead, inactive_scope_bulk_calls_do_not_crash)
{
    // No session started anywhere in this process at this point in the run.
    // This doesn't measure allocation count directly (see file comment), but
    // exercises the annotation-level gate (current_session()==nullptr and no
    // registered instrumentation callbacks) at volume under ASan/UBSan.
    for (int i = 0; i < 10000; ++i)
    {
        PROFILER_SCOPE("inactive_bulk_scope");
    }
}

// Locks in the Phase 2 default-preset flip: statistical analysis (mean/
// variance/percentiles, plus the shared analyzer-lock write) is opt-in, not
// on by default, at both the facade and native option-struct layers.
PROFILERTEST(ScopeOverhead, statistical_analysis_is_opt_in_by_default)
{
    EXPECT_FALSE(session_options{}.statistical_analysis);
    EXPECT_FALSE(profiler_options{}.enable_statistical_analysis_);
}

namespace
{
profiler_session make_active_session()
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = false;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = false;
    return profiler_session(opts);
}
}  // namespace

// Regression test for the deferred Phase 2 item: the active path used to call
// std::make_unique<annotation::impl> on every PROFILER_SCOPE, i.e. one heap
// allocation per scope even on a registered, warmed-up static-label path.
// annotation.cpp now recycles impl storage through a thread-local freelist;
// two sequential (non-overlapping) active scopes on the same thread must
// reuse the same block instead of allocating a fresh one each time.
PROFILERTEST(ScopeOverhead, active_scope_impl_storage_is_recycled)
{
    auto session = make_active_session();
    ASSERT_TRUE(session.start());

    const void* first_address = nullptr;
    {
        annotation scope("pool_probe_1", /*is_function=*/false, __FILE__, __LINE__);
        first_address = scope.debug_impl_address();
        ASSERT_NE(first_address, nullptr);  // session active -> impl_ must be constructed
    }

    const void* second_address = nullptr;
    {
        annotation scope("pool_probe_2", /*is_function=*/false, __FILE__, __LINE__);
        second_address = scope.debug_impl_address();
        ASSERT_NE(second_address, nullptr);
    }

    EXPECT_EQ(first_address, second_address);

    ASSERT_TRUE(session.stop());
}

// Mirrors inactive_scope_bulk_calls_do_not_crash: doesn't measure allocation
// count directly, but exercises repeated pool acquire/release at volume under
// ASan/UBSan to catch any use-after-free/double-free/leak in the recycling path.
PROFILERTEST(ScopeOverhead, active_scope_bulk_calls_recycle_and_do_not_crash)
{
    auto session = make_active_session();
    ASSERT_TRUE(session.start());

    for (int i = 0; i < 10000; ++i)
    {
        PROFILER_SCOPE("active_bulk_scope");
    }

    ASSERT_TRUE(session.stop());
}

// Forces the pool's overflow path on both ends: enough simultaneously alive
// scopes exhausts the freelist on the way down (acquire falls back to
// ::operator new) and overflows its cap on the way back up (release falls
// back to ::operator delete instead of growing the freelist unboundedly).
// The cap itself is an annotation.cpp implementation detail, not asserted on
// directly here; 200 comfortably exceeds it.
PROFILERTEST(ScopeOverhead, active_scope_deep_nesting_stresses_pool_growth_and_shrink)
{
    auto session = make_active_session();
    ASSERT_TRUE(session.start());

    std::function<void(int)> recurse = [&](int depth) {
        if (depth == 0)
        {
            return;
        }
        PROFILER_SCOPE("deep_nesting_probe");
        recurse(depth - 1);
    };
    recurse(200);

    ASSERT_TRUE(session.stop());
}
