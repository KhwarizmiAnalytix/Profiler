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
 * Profiles the same function through each compiled-in backend. Backends are
 * mutually exclusive, so only one exclusive case is live in a given binary:
 *   BackendFunction.kineto_profiles_function
 *   BackendFunction.native_profiles_function
 *   BackendFunction.itt_profiles_function
 * NVTX is a ProfilerState on the Kineto/ITT path, not a fourth exclusive
 * backend; BackendFunction.nvtx_profiles_function runs with those builds.
 *
 * This file is not native-only and does not match TestKineto*, so CMake glob
 * includes it for every PROFILER_BACKEND.
 */

#include <exception>
#include <string>
#include <vector>

#include "ProfilerTest.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include <set>
#include <unordered_set>

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/kineto_client_interface.h"
#include "bespoke/kineto/profiler_kineto.h"
#endif

#include "native/session/profiler.h"

#if PROFILER_HAS_ITT
#include <functional>

#include "bespoke/base/base.h"
#include "bespoke/itt/itt_wrapper.h"
#endif

namespace
{

constexpr const char* kFunctionScope = "backend_profiled_function";
constexpr const char* kNestedScope   = "backend_nested_work";

double profiled_function()
{
    constexpr int       k_size = 24;
    std::vector<double> lhs(static_cast<size_t>(k_size * k_size), 1.25);
    std::vector<double> rhs(static_cast<size_t>(k_size * k_size), 0.75);
    std::vector<double> out(static_cast<size_t>(k_size * k_size), 0.0);

    for (int row = 0; row < k_size; ++row)
    {
        for (int col = 0; col < k_size; ++col)
        {
            double sum = 0.0;
            for (int inner = 0; inner < k_size; ++inner)
            {
                sum += lhs[static_cast<size_t>(row * k_size + inner)] *
                       rhs[static_cast<size_t>(inner * k_size + col)];
            }
            out[static_cast<size_t>(row * k_size + col)] = sum;
        }
    }

    double total = 0.0;
    for (double value : out)
    {
        total += value;
    }
    return total;
}

}  // namespace

#if PROFILER_HAS_KINETO

namespace
{

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

PROFILERTEST(BackendFunction, kineto_profiles_function)
{
    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/false,
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

    double result = 0.0;
    {
        PROFILER_RECORD_USER_SCOPE(kFunctionScope);
        result = profiled_function();
        {
            PROFILER_RECORD_USER_SCOPE(kNestedScope);
            result += profiled_function();
        }
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_GT(result, 0.0);
    ASSERT_NE(profiler_result, nullptr);

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    const auto* outer_event = find_named_event(events, kFunctionScope);
    const auto* inner_event = find_named_event(events, kNestedScope);
    ASSERT_NE(outer_event, nullptr);
    ASSERT_NE(inner_event, nullptr);
    EXPECT_GT(outer_event->durationNs(), 0U);
    EXPECT_GE(outer_event->durationNs(), inner_event->durationNs());
    EXPECT_TRUE(outer_event->stack().empty());
}

PROFILERTEST(BackendFunction, kineto_with_stack_records_callsite)
{
    constexpr const char* kStackScope = "backend_stack_profiled_function";

    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO,
        /*report_input_shapes=*/false,
        /*profile_memory=*/false,
        /*with_stack=*/true,
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

    double    result        = 0.0;
    const int expected_line = __LINE__ + 2;
    {
        PROFILER_RECORD_USER_SCOPE(kStackScope);
        result = profiled_function();
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_GT(result, 0.0);
    ASSERT_NE(profiler_result, nullptr);

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    const auto* event = find_named_event(events, kStackScope);
    ASSERT_NE(event, nullptr);

    const auto stack = event->stack();
    ASSERT_FALSE(stack.empty());
    const std::string expected_file     = "TestProfilerBackendFunction.cpp";
    const std::string expected_location = expected_file + ":" + std::to_string(expected_line);
    EXPECT_NE(stack.front().find(expected_location), std::string::npos) << stack.front();
}

#endif  // PROFILER_HAS_KINETO

PROFILERTEST(BackendFunction, native_profiles_function)
{
    profiler::profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = false;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = false;

    profiler::profiler_session session(opts);
    ASSERT_TRUE(session.start());

    double result = 0.0;
    {
        PROFILER_PROFILE_SCOPE(kFunctionScope);
        result = profiled_function();
        {
            PROFILER_PROFILE_SCOPE(kNestedScope);
            result += profiled_function();
        }
    }

    ASSERT_TRUE(session.stop());
    EXPECT_GT(result, 0.0);

    const std::string json = session.generate_chrome_trace_json();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find(kFunctionScope), std::string::npos);
    EXPECT_NE(json.find(kNestedScope), std::string::npos);
}

#if PROFILER_HAS_ITT

namespace
{

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

bool saw_name(const std::vector<std::string>& names, const char* expected)
{
    for (const auto& name : names)
    {
        if (name == expected)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

PROFILERTEST(BackendFunction, itt_profiles_function)
{
    profiler::profiler_impl::itt_init();
    EXPECT_TRUE(profiler::profiler_impl::kITTAvailable);

    recording_itt_stub stub;
    scoped_itt_stub    stub_guard(stub);

    profiler::profiler_impl::ProfilerConfig config(profiler::profiler_impl::ProfilerState::ITT);
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

    double result = 0.0;
    {
        PROFILER_RECORD_USER_SCOPE(kFunctionScope);
        profiler::profiler_impl::itt_range_push(kFunctionScope);
        result = profiled_function();
        {
            PROFILER_RECORD_USER_SCOPE(kNestedScope);
            profiler::profiler_impl::itt_range_push(kNestedScope);
            result += profiled_function();
            profiler::profiler_impl::itt_range_pop();
        }
        profiler::profiler_impl::itt_range_pop();
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_GT(result, 0.0);
    ASSERT_NE(profiler_result, nullptr);
    EXPECT_FALSE(profiler_result->save("itt_empty_trace.json"));
    EXPECT_TRUE(saw_name(stub.pushes_, kFunctionScope));
    EXPECT_TRUE(saw_name(stub.pushes_, kNestedScope));
    EXPECT_EQ(stub.pops_, stub.pushes_.size());
}

#endif  // PROFILER_HAS_ITT

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT

PROFILERTEST(BackendFunction, nvtx_profiles_function)
{
    profiler::profiler_impl::ProfilerConfig config(profiler::profiler_impl::ProfilerState::NVTX);
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

    double result = 0.0;
    {
        PROFILER_RECORD_USER_SCOPE(kFunctionScope);
        result = profiled_function();
        {
            PROFILER_RECORD_USER_SCOPE(kNestedScope);
            result += profiled_function();
        }
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    EXPECT_GT(result, 0.0);
    ASSERT_NE(profiler_result, nullptr);
    EXPECT_FALSE(profiler_result->save("nvtx_empty_trace.json"));
}

PROFILERTEST(BackendFunction, global_kineto_init_is_callable)
{
    profiler::global_kineto_init();
}

PROFILERTEST(BackendFunction, disable_without_enable_returns_empty)
{
    auto first = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->events().empty());
    EXPECT_FALSE(first->save("disable_without_enable.json"));

    auto second = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(second->save("disable_without_enable_again.json"));
}

PROFILERTEST(BackendFunction, privateuse1_without_observer_is_noop)
{
    profiler::profiler_impl::ProfilerConfig config(
        profiler::profiler_impl::ProfilerState::PRIVATEUSE1);
    profiler::profiler_impl::enableProfiler(
        config, {profiler::profiler_impl::ActivityType::CPU}, {profiler::RecordScope::USER_SCOPE});

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(profiler_result, nullptr);
    EXPECT_TRUE(profiler_result->events().empty());
    EXPECT_FALSE(profiler_result->save("privateuse1_empty_trace.json"));
}

PROFILERTEST(BackendFunction, child_thread_join_without_main_is_noop)
{
    EXPECT_FALSE(profiler::profiler_impl::isProfilerEnabledInMainThread());
    profiler::profiler_impl::enableProfilerInChildThread();
    profiler::profiler_impl::disableProfilerInChildThread();
    EXPECT_FALSE(profiler::profiler_impl::isProfilerEnabledInMainThread());
}

#if PROFILER_HAS_KINETO && !PROFILER_HAS_ITT
PROFILERTEST(BackendFunction, itt_state_without_itt_backend_is_noop)
{
    profiler::profiler_impl::ProfilerConfig config(profiler::profiler_impl::ProfilerState::ITT);
    profiler::profiler_impl::enableProfiler(
        config, {profiler::profiler_impl::ActivityType::CPU}, {profiler::RecordScope::USER_SCOPE});

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(profiler_result, nullptr);
    EXPECT_TRUE(profiler_result->events().empty());
    EXPECT_FALSE(profiler_result->save("itt_state_without_backend.json"));
}
#endif  // PROFILER_HAS_KINETO && !PROFILER_HAS_ITT

#endif  // PROFILER_HAS_KINETO || PROFILER_HAS_ITT
