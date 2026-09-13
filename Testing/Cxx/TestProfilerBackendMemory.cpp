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
 * Memory-profiling path for every compiled-in backend. Exclusive backends
 * (Kineto / ITT) are mutually exclusive; Native is always compiled; NVTX is a
 * ProfilerState available on Kineto or ITT builds.
 *
 *   BackendMemory.kineto_profiles_memory
 *   BackendMemory.native_profiles_memory
 *   BackendMemory.itt_profiles_memory
 *   BackendMemory.nvtx_profiles_memory
 */

#include <cstdint>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "ProfilerTest.h"
#include "common/instrumentation.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include <set>
#include <unordered_set>

#include "bespoke/common/orchestration/observer.h"
#include "bespoke/common/record_function.h"
#include "bespoke/kineto/profiler_kineto.h"
#endif

#include "native/memory/memory_tracker.h"
#include "native/session/profiler.h"

#if PROFILER_HAS_ITT
#include <functional>

#include "bespoke/base/base.h"
#include "bespoke/itt/itt_wrapper.h"
#endif

namespace
{

constexpr const char* kMemoryScope = "backend_memory_scope";
constexpr size_t      kAllocBytes  = 4096;

// Allocates, reports (Kineto/ITT path), then frees. Returns the live buffer
// address while allocated so callers can track it on the native tracker too.
void* allocate_and_report(size_t bytes, size_t* total_allocated)
{
    void* ptr = ::operator new(bytes);
    std::memset(ptr, 0xAB, bytes);
    *total_allocated += bytes;
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler::report_memory_usage(
        ptr,
        static_cast<int64_t>(bytes),
        *total_allocated,
        *total_allocated,
        static_cast<int16_t>(profiler::device_enum::CPU),
        /*device_index=*/-1);
#endif
    return ptr;
}

void free_and_report(void* ptr, size_t bytes, size_t* total_allocated)
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    if (*total_allocated >= bytes)
    {
        *total_allocated -= bytes;
    }
    else
    {
        *total_allocated = 0;
    }
    profiler::report_memory_usage(
        ptr,
        -static_cast<int64_t>(bytes),
        *total_allocated,
        *total_allocated,
        static_cast<int16_t>(profiler::device_enum::CPU),
        /*device_index=*/-1);
#else
    if (*total_allocated >= bytes)
    {
        *total_allocated -= bytes;
    }
    else
    {
        *total_allocated = 0;
    }
#endif
    ::operator delete(ptr);
}

}  // namespace

#if PROFILER_HAS_KINETO

namespace
{

bool has_memory_event_with_bytes(
    const std::vector<profiler::profiler_impl::KinetoEvent>& events, int64_t nbytes)
{
    for (const auto& event : events)
    {
        if (event.name() == "[memory]" && event.nBytes() == nbytes)
        {
            return true;
        }
    }
    return false;
}

bool has_named_event(
    const std::vector<profiler::profiler_impl::KinetoEvent>& events, const char* name)
{
    for (const auto& event : events)
    {
        if (event.name() == name)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

PROFILERTEST(BackendMemory, kineto_profiles_memory)
{
    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/true,
        /*with_stack=*/false,
        /*with_flops=*/false,
        /*with_modules=*/false);

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
        GTEST_SKIP() << "Kineto profiler unavailable: " << ex.what();
    }

    // report_memory_usage's cheap early-out gate (also used directly by the
    // Memory library's caching allocators and CPU reporter)
    // should agree with the session actually being memory-profiling-enabled.
    EXPECT_TRUE(profiler::memory_profiling_active());

    size_t total_allocated = 0;
    {
        PROFILER_RECORD_USER_SCOPE(kMemoryScope);
        void* ptr = allocate_and_report(kAllocBytes, &total_allocated);
        free_and_report(ptr, kAllocBytes, &total_allocated);
        profiler::report_out_of_memory(
            static_cast<int64_t>(kAllocBytes),
            total_allocated,
            total_allocated,
            static_cast<int16_t>(profiler::device_enum::CPU),
            /*device_index=*/-1);
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(profiler_result, nullptr);
    EXPECT_FALSE(profiler::memory_profiling_active());

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no events in this environment";
    }

    EXPECT_TRUE(has_memory_event_with_bytes(events, static_cast<int64_t>(kAllocBytes)))
        << "expected allocation [memory] event of " << kAllocBytes << " bytes";
    EXPECT_TRUE(has_memory_event_with_bytes(events, -static_cast<int64_t>(kAllocBytes)))
        << "expected deallocation [memory] event of -" << kAllocBytes << " bytes";
    EXPECT_TRUE(has_named_event(events, "[OutOfMemory]"))
        << "expected [OutOfMemory] instant event from report_out_of_memory";
}

#endif  // PROFILER_HAS_KINETO

PROFILERTEST(BackendMemory, native_profiles_memory)
{
    profiler::profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = true;
    opts.track_memory_deltas_           = true;
    opts.track_peak_memory_             = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = true;

    profiler::profiler_session session(opts);
    ASSERT_TRUE(session.start());

    size_t total_allocated = 0;
    {
        PROFILER_PROFILE_SCOPE(kMemoryScope);
        void* ptr = ::operator new(kAllocBytes);
        std::memset(ptr, 0xCD, kAllocBytes);
        total_allocated += kAllocBytes;
        session.memory_tracker().track_allocation(ptr, kAllocBytes, kMemoryScope);

        EXPECT_GE(session.memory_tracker().get_current_usage(), kAllocBytes);
        EXPECT_GE(session.memory_tracker().get_total_allocated(), kAllocBytes);
        EXPECT_GE(session.memory_tracker().get_peak_usage(), kAllocBytes);

        session.memory_tracker().track_deallocation(ptr);
        ::operator delete(ptr);
        total_allocated -= kAllocBytes;
    }

    ASSERT_TRUE(session.stop());
    EXPECT_EQ(total_allocated, 0U);

    const auto stats = session.memory_tracker().get_current_stats();
    EXPECT_EQ(stats.current_usage_, 0U);
    EXPECT_GE(stats.total_allocated_, kAllocBytes);
    EXPECT_GE(stats.peak_usage_, kAllocBytes);

    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find(kMemoryScope), std::string::npos);
}

#if PROFILER_HAS_ITT

PROFILERTEST(BackendMemory, itt_profiles_memory)
{
    profiler::profiler_impl::itt_init();
    EXPECT_TRUE(profiler::profiler_impl::kITTAvailable);

    profiler::profiler_impl::ProfilerConfig config(
        profiler::profiler_impl::ProfilerState::ITT,
        /*report_input_shapes=*/false,
        /*profile_memory=*/true);
    try
    {
        profiler::profiler_impl::enableProfiler(
            config,
            {profiler::profiler_impl::ActivityType::CPU},
            {profiler::RecordScope::USER_SCOPE});
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "ITT callbacks unavailable: " << ex.what();
    }

    auto* state = profiler::profiler_impl::impl::ProfilerStateBase::get();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->memoryProfilingEnabled());

    size_t total_allocated = 0;
    {
        PROFILER_RECORD_USER_SCOPE(kMemoryScope);
        profiler::profiler_impl::itt_range_push(kMemoryScope);
        void* ptr = allocate_and_report(kAllocBytes, &total_allocated);
        free_and_report(ptr, kAllocBytes, &total_allocated);
        profiler::profiler_impl::itt_range_pop();
    }

    PROFILER_UNUSED auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_EQ(total_allocated, 0U);
}

#endif  // PROFILER_HAS_ITT

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT

PROFILERTEST(BackendMemory, nvtx_profiles_memory)
{
    profiler::profiler_impl::ProfilerConfig config(
        profiler::profiler_impl::ProfilerState::NVTX,
        /*report_input_shapes=*/false,
        /*profile_memory=*/true);
    try
    {
        profiler::profiler_impl::enableProfiler(
            config,
            {profiler::profiler_impl::ActivityType::CPU},
            {profiler::RecordScope::USER_SCOPE});
    }
    catch (const std::exception& ex)
    {
        GTEST_SKIP() << "NVTX profiler unavailable: " << ex.what();
    }

    auto* state = profiler::profiler_impl::impl::ProfilerStateBase::get();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->memoryProfilingEnabled());

    size_t total_allocated = 0;
    {
        PROFILER_RECORD_USER_SCOPE(kMemoryScope);
        void* ptr = allocate_and_report(kAllocBytes, &total_allocated);
        free_and_report(ptr, kAllocBytes, &total_allocated);
    }

    PROFILER_UNUSED auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_EQ(total_allocated, 0U);
}

#endif  // PROFILER_HAS_KINETO || PROFILER_HAS_ITT
