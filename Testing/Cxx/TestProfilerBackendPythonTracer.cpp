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
 * Phase 5 (docs/design-review.md section 4): the registered Python tracer is
 * a stub -- this native port has no real Python integration (the disabled
 * `#if 0` implementation that used to live in native/cpu/python_tracer.{h,cpp}
 * was removed as dead code). Before this test, requesting Python tracing
 * silently returned success with no data collected; a caller had no way to
 * tell that apart from "traced and found nothing." start() must now report
 * an explicit failure instead.
 *
 * host_tracer_level and device_tracer_level are both forced to 0 here so
 * create_profilers() only instantiates the Python tracer stub, isolating it
 * from the other registered factories (host_tracer_factory,
 * gpu_tracer_factory).
 */

#include "ProfilerTest.h"
#include "native/core/profiler_factory.h"
#include "native/core/profiler_options.h"

PROFILERTEST(BackendPythonTracer, requesting_python_tracing_fails_honestly)
{
    profiler::profile_options options;
    options.set_host_tracer_level(0);
    options.set_device_tracer_level(0);
    options.set_python_tracer_level(1);

    auto profilers = profiler::create_profilers(options);
    ASSERT_EQ(profilers.size(), 1U);

    auto const status = profilers.front()->start();
    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(status.message().empty());
}

PROFILERTEST(BackendPythonTracer, level_zero_creates_no_tracer)
{
    profiler::profile_options options;
    options.set_host_tracer_level(0);
    options.set_device_tracer_level(0);
    options.set_python_tracer_level(0);

    auto profilers = profiler::create_profilers(options);
    EXPECT_TRUE(profilers.empty());
}
