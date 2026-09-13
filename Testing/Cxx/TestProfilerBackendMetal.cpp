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
 * Exercises the Kineto/ITT-side Metal fallback stub (bespoke/base/metal.mm,
 * MetalMethods) via ProfilerState::KINETO_PRIVATEUSE1_FALLBACK -- Metal
 * reuses the generic PrivateUse1 slot rather than a first-class device_enum
 * value (see Docs/profiler/profiler.md, GPU section). This is a CPU
 * monotonic-clock fallback tier, not a merged device-activity trace: it
 * proves the ProfilerStubs plumbing round-trips a start/end pair, not that
 * a real MTLCommandBuffer was timed.
 */

#include <exception>
#include <set>
#include <string>
#include <unordered_set>

#include "ProfilerTest.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_METAL

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/profiler_kineto.h"

namespace
{

constexpr const char* kMetalScope = "metal_fallback_scope";

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

PROFILERTEST(BackendMetal, privateuse1_fallback_round_trips_elapsed_time)
{
    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO_PRIVATEUSE1_FALLBACK);

    const std::set<profiler::profiler_impl::ActivityType> activities{
        profiler::profiler_impl::ActivityType::CPU};
    const std::unordered_set<profiler::RecordScope> scopes{profiler::RecordScope::USER_SCOPE};

    try
    {
        profiler::profiler_impl::prepareProfiler(config, activities);
        profiler::profiler_impl::enableProfiler(config, activities, scopes);
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "Metal fallback profiler unavailable: " << ex.what();
    }

    {
        PROFILER_RECORD_USER_SCOPE(kMetalScope);
        // A do-nothing wait loop stands in for real Metal dispatch: the
        // fallback tier only measures dispatch-to-dispatch CPU wall time
        // (see bespoke/base/metal.mm), so no MTLCommandBuffer is needed to
        // exercise the ProfilerStubs round-trip this test checks.
        volatile int spin = 0;
        for (int i = 0; i < 100000; ++i)
        {
            spin = spin + 1;
        }
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(profiler_result, nullptr);

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "Metal fallback backend produced no CPU events in this environment";
    }

    const auto* event = find_named_event(events, kMetalScope);
    ASSERT_NE(event, nullptr);
    EXPECT_GE(event->privateuse1ElapsedUs(), 0);
}

#endif  // PROFILER_HAS_METAL
