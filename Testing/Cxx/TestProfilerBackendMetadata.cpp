/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

/*
 * Profiles a plain, non-tensor function with structured metadata via
 * PROFILER_RECORD_FUNCTION_WITH_METADATA + record_function_metadata_builder, and
 * checks that KinetoEvent::extraMeta() round-trips the key-value pairs.
 * This is the generic replacement for PyTorch's tensor/IValue-shaped
 * op-argument recording (deleted as part of the tensor-independence pass --
 * see Docs/profiler/profiler.md, "PyTorch types removed").
 */

#include <exception>
#include <set>
#include <string>
#include <unordered_set>

#include "ProfilerTest.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_KINETO

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/profiler_kineto.h"

namespace
{

constexpr const char* kMetadataScope = "metadata_profiled_function";

long long gemm_like(long long m, long long n, long long k)
{
    long long total = 0;
    for (long long i = 0; i < m; ++i)
    {
        for (long long j = 0; j < n; ++j)
        {
            total += (i + j) % (k + 1);
        }
    }
    return total;
}

const profiler::profiler_impl::KinetoEvent* find_named_event(
    const std::vector<profiler::profiler_impl::KinetoEvent>& events, const std::string& name)
{
    for (const auto& event : events)
    {
        if (event.name() == name)
        {
            return &event;
        }
    }
    return nullptr;
}

}  // namespace

PROFILERTEST(BackendMetadata, record_function_with_metadata_round_trips)
{
    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO);

    const std::set<profiler::profiler_impl::ActivityType> activities{
        profiler::profiler_impl::ActivityType::CPU};
    // PROFILER_RECORD_FUNCTION_WITH_METADATA creates a FUNCTION-scope guard (mirroring
    // PROFILER_RECORD_FUNCTION, not PROFILER_RECORD_USER_SCOPE) -- the callback registration
    // must match or begin_op() never fires.
    const std::unordered_set<profiler::RecordScope> scopes{profiler::RecordScope::FUNCTION};

    try
    {
        profiler::profiler_impl::prepareProfiler(config, activities);
        profiler::profiler_impl::enableProfiler(config, activities, scopes);
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "Kineto profiler unavailable: " << ex.what();
    }

    long long result = 0;
    {
        PROFILER_RECORD_FUNCTION_WITH_METADATA(guard, kMetadataScope);
        profiler::record_function_metadata_builder(guard, kMetadataScope)
            .with_metadata("m", static_cast<int64_t>(64))
            .with_metadata("n", static_cast<int64_t>(64))
            .with_metadata("k", static_cast<int64_t>(64))
            .with_metadata("backend", std::string("cpu_reference"));
        result = gemm_like(64, 64, 64);
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_GE(result, 0);
    ASSERT_NE(profiler_result, nullptr);

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    const auto* event = find_named_event(events, kMetadataScope);
    ASSERT_NE(event, nullptr);

    const auto meta = event->extraMeta();
    ASSERT_EQ(meta.count("m"), 1U);
    ASSERT_EQ(meta.count("n"), 1U);
    ASSERT_EQ(meta.count("k"), 1U);
    ASSERT_EQ(meta.count("backend"), 1U);
    EXPECT_EQ(meta.at("m"), "64");
    EXPECT_EQ(meta.at("n"), "64");
    EXPECT_EQ(meta.at("k"), "64");
    EXPECT_EQ(meta.at("backend"), "cpu_reference");
}

#endif  // PROFILER_HAS_KINETO
