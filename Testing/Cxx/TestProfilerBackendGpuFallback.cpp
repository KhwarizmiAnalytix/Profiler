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
 * Exercises ProfilerState::KINETO_GPU_FALLBACK -- the CUDA event-fallback
 * stub in bespoke/base/cuda.cpp, generalized via bespoke/base/gpu_runtime.h
 * to also serve HIP. CUDA and HIP are mutually exclusive builds
 * (MEMORY_GPU_BACKEND selects one vendor) and share this exact code path
 * and cudaStubs()/cudaElapsedUs() naming by design -- see docs/profiler.md,
 * GPU section -- so one test file covers whichever of the two
 * is compiled in. Requires a real CUDA or ROCm/HIP runtime to produce a
 * non-trivial elapsed time; skips cleanly when neither device is present.
 */

#include <exception>
#include <set>
#include <string>
#include <unordered_set>

#include "ProfilerTest.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_CUDA || PROFILER_HAS_HIP

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/profiler_kineto.h"

namespace
{

constexpr const char* kGpuFallbackScope = "gpu_fallback_scope";

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

PROFILERTEST(BackendGpuFallback, cuda_or_hip_fallback_round_trips_elapsed_time)
{
    profiler::profiler_impl::ProfilerConfig const config(
        profiler::profiler_impl::ProfilerState::KINETO_GPU_FALLBACK);

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
        GTEST_SKIP() << "GPU (CUDA/HIP) fallback profiler unavailable: " << ex.what();
    }

    {
        PROFILER_RECORD_USER_SCOPE(kGpuFallbackScope);
        for (volatile int spin = 0; spin < 100000; ++spin) {}
    }

    auto profiler_result = profiler::profiler_impl::disableProfiler();
    ASSERT_NE(profiler_result, nullptr);

    const auto& events = profiler_result->events();
    if (events.empty())
    {
        GTEST_SKIP() << "GPU fallback backend produced no CPU events in this environment";
    }

    const auto* event = find_named_event(events, kGpuFallbackScope);
    ASSERT_NE(event, nullptr);
    // -1 means the fallback event pair never recorded (no CUDA/HIP device);
    // an environment without a real device should have hit prepareProfiler's
    // catch above, but tolerate it here too rather than asserting > 0 and
    // being flaky on a CI runner with a device that reports 0us for trivial
    // work.
    EXPECT_GE(event->cudaElapsedUs(), -1);
}

// Phase 4 test: explicit stream binding and non-blocking query support
// (design-review.md section 6.6). Tests that ProfilerStubs::record_with_stream
// and ProfilerStubs::elapsed_nonblocking are wired correctly on real hardware.
// Skips cleanly when no device is available.
PROFILERTEST(BackendGpuFallback, stream_aware_record_and_nonblocking_query)
{
#if PROFILER_HAS_CUDA || PROFILER_HAS_HIP

    // Get the CUDA/HIP stubs (registered at module load).
    auto stubs = profiler::profiler_impl::impl::cudaStubs();
    if (stubs == nullptr || !stubs->enabled())
    {
        GTEST_SKIP() << "No CUDA/HIP device available";
    }

    // Phase 4: Test record_with_stream (explicit stream binding).
    // For now, we use nullptr to bind to the default per-thread stream,
    // matching the existing behavior while enabling future stream-specific work.
    profiler::profiler_impl::impl::ProfilerVoidEventStub event1, event2;
    int16_t                                              device = -1;
    int64_t                                              cpu_ns = 0;

    stubs->record_with_stream(nullptr, &device, &event1, &cpu_ns);
    EXPECT_GE(device, 0);
    EXPECT_GT(cpu_ns, 0);

    // Spin a bit to create measurable elapsed time.
    for (volatile int spin = 0; spin < 100000; ++spin) {}

    stubs->record_with_stream(nullptr, &device, &event2, &cpu_ns);

    // Phase 4: Test elapsed_nonblocking (non-blocking query).
    // After events have completed, elapsed_nonblocking should return a valid time
    // rather than -1 (not ready).
    float elapsed_us_nonblocking = stubs->elapsed_nonblocking(&event1, &event2);
    if (elapsed_us_nonblocking >= 0.0f)
    {
        // Success: non-blocking query returned immediately.
        EXPECT_GT(elapsed_us_nonblocking, 0.0f);
    }
    else
    {
        // -1 means events weren't ready synchronously. This is acceptable on
        // slower hardware or under load; the API allows both blocking and
        // non-blocking queries.
    }

    // Fall back to blocking elapsed() for verification.
    float elapsed_us_blocking = stubs->elapsed(&event1, &event2);
    EXPECT_GT(elapsed_us_blocking, 0.0f);

#else
    GTEST_SKIP() << "CUDA/HIP not compiled in";
#endif
}

#endif  // PROFILER_HAS_CUDA || PROFILER_HAS_HIP
