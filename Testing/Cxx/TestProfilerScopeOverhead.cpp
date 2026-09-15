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
