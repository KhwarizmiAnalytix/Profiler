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
 * Phase 6.C (design-review.md section 7, Phase 6 / section 8's "CPU
 * conformance" row): run one identical, shared "golden" workload through
 * whichever instrumentation backend this binary was compiled with, and
 * write a normalized summary of what that backend's *own* observation
 * point recorded -- so a follow-up CI step (Scripts/diff_cross_backend_golden.py)
 * can diff the KINETO leg's summary against the ITT leg's summary for
 * exact agreement, without either backend running in the same process
 * (KINETO and ITT are mutually exclusive per build).
 *
 * What is and is not compared, and why:
 *
 * - KINETO has a real internal capture buffer: disableProfiler() returns
 *   every recorded KinetoEvent, independent of any test-only scaffolding.
 * - ITT has no internal capture buffer at all -- profiler::profiler_impl
 *   ittStubs() plugs in a real ITT/VTune backend only when one is attached;
 *   in-test, this file plugs in the same kind of recording stub
 *   TestProfilerBackendFunction.cpp's itt_profiles_function test already
 *   uses, which observes exactly the range-push/pop names ITT's own wrapper
 *   emits. This is backend-specific plumbing (unavoidable -- there is no
 *   shared "read back what happened" API), but the *workload* that
 *   generates those names is single-sourced (run_golden_workload() below)
 *   and identical for both backends.
 * - The comparison is a sorted (name -> count) multiset, not an exact
 *   sequence. KinetoEvents are ordered by completion (LIFO unwind of
 *   nested scopes); the ITT stub's pushes_ vector is in push (entry) order.
 *   Those are two different, equally valid orderings of the same identical
 *   workload -- comparing them positionally would be comparing an
 *   implementation detail of each backend's own bookkeeping, not the
 *   workload's identities. A count-per-name comparison is still an exact
 *   "identities/categories/counts... agree" check (design-review.md
 *   section 8), just order-insensitive, and avoids exactly the kind of
 *   brittleness section 8 explicitly warns against for timing comparisons.
 * - Structured key/value metadata (record_function_metadata_builder) has no
 *   ITT equivalent at all (ITT's wrapper only pushes/pops plain range
 *   names) -- comparing it would fail structurally for a reason that has
 *   nothing to do with a real backend divergence, so it is intentionally
 *   left out of this scenario. "Unicode/escaped metadata" (design-review.md
 *   section 8's own scenario list) is exercised instead as a scope *name*
 *   containing Unicode and JSON-special characters: both backends' own
 *   capture/export path must still preserve it byte-for-byte, which is a
 *   real, comparable property of both.
 * - "Overlapping async spans" (also named in section 8) is not attempted
 *   here: ITT's wrapper only exposes a LIFO push/pop stack API (no
 *   explicit begin/end task IDs), which cannot represent genuinely
 *   non-nested (overlapping) spans at all -- there is no ITT-side
 *   observation point for this scenario to target, not just a missing
 *   test. Left as a documented gap rather than a scenario neither backend
 *   can actually support symmetrically.
 */

#include "ProfilerTest.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "bespoke/common/record_function.h"
// enableProfiler()/disableProfiler()/ProfilerConfig/ProfilerState/ActivityType are
// declared here regardless of compiled backend -- this header is the generic
// profiler_impl frontend (KINETO-named for historical reasons), not a
// KINETO-exclusive API. TestProfilerBackendFunction.cpp's itt_profiles_function
// test relies on the same fact.
#include "bespoke/kineto/kineto_client_interface.h"
#include "bespoke/kineto/profiler_kineto.h"

#if PROFILER_HAS_ITT
#include <functional>

#include "bespoke/base/base.h"
#include "bespoke/itt/itt_wrapper.h"
#endif

namespace
{

// A name containing multi-byte UTF-8 (Chinese "hello") and characters that
// need escaping in JSON output (a quote and a backslash) -- proves both
// backends' capture/export path preserves it byte-for-byte, standing in for
// design-review.md section 8's "Unicode/escaped metadata" scenario (see the
// file comment above for why this is a name rather than structured
// metadata).
constexpr const char* kUnicodeScopeName = "golden_unicode_\xE4\xBD\xA0\xE5\xA5\xBD_\"quoted\"_\\backslash";

constexpr const char* kOuterScope     = "golden_outer_scope";
constexpr const char* kNestedScope    = "golden_nested_scope";
constexpr const char* kRecursiveScope = "golden_recursive_scope";
constexpr const char* kSharedName     = "golden_shared_name_from_call_site";
constexpr int          kRecursionDepth = 3;

void recursive_scope(int depth_remaining)
{
    if (depth_remaining <= 0)
    {
        return;
    }
    PROFILER_RECORD_USER_SCOPE(kRecursiveScope);
    recursive_scope(depth_remaining - 1);
}

// Two distinct call sites recording the identical scope name -- design-review.md
// section 8's "same name from different call sites" scenario.
void call_site_a()
{
    PROFILER_RECORD_USER_SCOPE(kSharedName);
}

void call_site_b()
{
    PROFILER_RECORD_USER_SCOPE(kSharedName);
}

// The single-sourced workload both backends' tests below run verbatim.
void run_golden_workload()
{
    PROFILER_RECORD_USER_SCOPE(kOuterScope);
    {
        PROFILER_RECORD_USER_SCOPE(kNestedScope);
    }
    recursive_scope(kRecursionDepth);
    call_site_a();
    call_site_b();
    {
        PROFILER_RECORD_USER_SCOPE(kUnicodeScopeName);
    }
}

// Writes {"name": count, ...} sorted by name -- the normalized,
// backend-independent summary the CI diffing step compares byte-for-byte
// between a KINETO leg's output and an ITT leg's output.
void write_golden_summary(const std::map<std::string, int>& counts)
{
    std::ofstream out("cross_backend_golden.json", std::ios::trunc);
    out << "{\n";
    bool first = true;
    for (const auto& [name, count] : counts)
    {
        if (!first)
        {
            out << ",\n";
        }
        first = false;
        out << "  \"";
        for (unsigned char c : name)
        {
            switch (c)
            {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                default:
                    out << c;
            }
        }
        out << "\": " << count;
    }
    out << "\n}\n";
}

std::map<std::string, int> golden_names_only(const std::map<std::string, int>& all_names)
{
    // Only entries this scenario itself produced (the shared "golden_" prefix)
    // survive into the comparison -- the backend's own capture buffer may
    // also contain unrelated events from other code paths (e.g. Kineto's own
    // internal bookkeeping spans).
    std::map<std::string, int> filtered;
    for (const auto& [name, count] : all_names)
    {
        if (name.rfind("golden_", 0) == 0)
        {
            filtered[name] = count;
        }
    }
    return filtered;
}

}  // namespace

#if PROFILER_HAS_KINETO

PROFILERTEST(CrossBackendGolden, kineto_backend_observes_expected_identities)
{
    using namespace profiler::profiler_impl;

    ProfilerConfig const config(
        ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/false,
        /*with_stack=*/false,
        /*with_flops=*/false,
        /*with_modules=*/false);
    const std::set<ActivityType> activities{ActivityType::CPU};
    const std::unordered_set<profiler::RecordScope> scopes{profiler::RecordScope::USER_SCOPE};

    try
    {
        prepareProfiler(config, activities);
        enableProfiler(config, activities, scopes);
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "Kineto profiler unavailable: " << ex.what();
    }

    run_golden_workload();

    auto profiler_result = disableProfiler();
    ASSERT_NE(profiler_result, nullptr);
    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    std::map<std::string, int> counts;
    for (const auto& event : events)
    {
        ++counts[event.name()];
    }
    const auto golden = golden_names_only(counts);

    // Sanity check before publishing: this scenario's own expected shape,
    // independent of whatever the ITT leg later reports.
    EXPECT_EQ(golden.count(kOuterScope), 1U);
    EXPECT_EQ(golden.count(kNestedScope), 1U);
    EXPECT_EQ(golden.at(kRecursiveScope), kRecursionDepth);
    EXPECT_EQ(golden.at(kSharedName), 2);
    EXPECT_EQ(golden.count(kUnicodeScopeName), 1U);

    write_golden_summary(golden);
}

#endif  // PROFILER_HAS_KINETO

#if PROFILER_HAS_ITT

namespace
{

// Same recording stub TestProfilerBackendFunction.cpp's itt_profiles_function
// test uses: ITT has no internal capture buffer of its own (a real ITT
// backend only emits to an attached external tool like VTune), so
// intercepting the wrapper's own push/pop calls is the only in-process
// observation point available.
class recording_itt_stub : public profiler::profiler_impl::impl::ProfilerStubs
{
public:
    void record(
        int16_t*, profiler::profiler_impl::impl::ProfilerVoidEventStub*, int64_t*) const override
    {
    }

    float elapsed(
        const profiler::profiler_impl::impl::ProfilerVoidEventStub*,
        const profiler::profiler_impl::impl::ProfilerVoidEventStub*) const override
    {
        return 0.0F;
    }

    void mark(const char*) const override {}

    void rangePush(const char* name) const override
    {
        if (name != nullptr)
        {
            pushes_.emplace_back(name);
        }
    }

    void rangePop() const override { ++pops_; }

    bool enabled() const override { return true; }

    void onEachDevice(std::function<void(int)>) const override {}

    void synchronize() const override {}

    mutable std::vector<std::string> pushes_;
    mutable size_t                   pops_{0};
};

class scoped_itt_stub
{
public:
    explicit scoped_itt_stub(recording_itt_stub& stub)
        : previous_(const_cast<profiler::profiler_impl::impl::ProfilerStubs*>(
              profiler::profiler_impl::impl::ittStubs()))
    {
        profiler::profiler_impl::impl::registerITTMethods(&stub);
    }

    scoped_itt_stub(const scoped_itt_stub&)            = delete;
    scoped_itt_stub(scoped_itt_stub&&)                 = delete;
    scoped_itt_stub& operator=(const scoped_itt_stub&) = delete;
    scoped_itt_stub& operator=(scoped_itt_stub&&)      = delete;

    ~scoped_itt_stub() { profiler::profiler_impl::impl::registerITTMethods(previous_); }

private:
    profiler::profiler_impl::impl::ProfilerStubs* const previous_;
};

}  // namespace

PROFILERTEST(CrossBackendGolden, itt_backend_observes_expected_identities)
{
    using namespace profiler::profiler_impl;

    itt_init();
    ASSERT_TRUE(kITTAvailable);

    recording_itt_stub stub;
    scoped_itt_stub    stub_guard(stub);

    ProfilerConfig config(ProfilerState::ITT);
    try
    {
        enableProfiler(config, {ActivityType::CPU}, {profiler::RecordScope::USER_SCOPE});
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "ITT callbacks unavailable: " << ex.what();
    }

    run_golden_workload();

    disableProfiler();

    std::map<std::string, int> counts;
    for (const auto& name : stub.pushes_)
    {
        ++counts[name];
    }
    const auto golden = golden_names_only(counts);

    if (golden.empty())
    {
        GTEST_SKIP() << "ITT wrapper recorded no golden_* pushes in this environment";
    }

    EXPECT_EQ(golden.count(kOuterScope), 1U);
    EXPECT_EQ(golden.count(kNestedScope), 1U);
    EXPECT_EQ(golden.at(kRecursiveScope), kRecursionDepth);
    EXPECT_EQ(golden.at(kSharedName), 2);
    EXPECT_EQ(golden.count(kUnicodeScopeName), 1U);
    EXPECT_EQ(stub.pops_, stub.pushes_.size());

    write_golden_summary(golden);
}

#endif  // PROFILER_HAS_ITT

#endif  // PROFILER_HAS_KINETO || PROFILER_HAS_ITT
