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

// Tests for common/backend_capabilities.h (design-review.md section 6.1's
// capability-discovery concept) and session_options::policy's required-vs-
// best-effort startup gating.

#include "ProfilerTest.h"
#include "common/backend_capabilities.h"
#include "profiler.h"

using namespace profiler;

PROFILERTEST(BackendCapabilities, cpu_is_always_supported)
{
    const backend_capabilities& caps = discover_backend_capabilities();
    EXPECT_TRUE(caps.supports(activity::cpu));
}

// kineto_compiled/itt_compiled must reflect this build's actual macros --
// exactly one of them is true for any build that has an instrumentation
// backend (KINETO or ITT), neither for PROFILER_BACKEND=NONE.
PROFILERTEST(BackendCapabilities, compiled_flags_match_this_build)
{
    const backend_capabilities& caps = discover_backend_capabilities();
    EXPECT_EQ(caps.kineto_compiled, kineto_enabled());
    EXPECT_EQ(caps.itt_compiled, itt_enabled());
    EXPECT_FALSE(caps.kineto_compiled && caps.itt_compiled);
}

// activity::cuda/hip must never report supported without a real device, even
// on a build where the corresponding toolkit was found at compile time --
// this is the actual fix for design-review.md finding 2's "reflects
// compilation rather than a usable device" gap.
PROFILERTEST(BackendCapabilities, cuda_and_hip_require_a_real_device)
{
    const backend_capabilities& caps = discover_backend_capabilities();
    if (!caps.cuda_compiled)
    {
        EXPECT_FALSE(caps.supports(activity::cuda));
    }
    if (!caps.hip_compiled)
    {
        EXPECT_FALSE(caps.supports(activity::hip));
    }
    if (caps.supports(activity::cuda) || caps.supports(activity::hip))
    {
        EXPECT_TRUE(caps.gpu_device_available);
    }
}

// NVTX must not require a device: design-review.md section 5's evidence
// notes say NVTX is normally a no-op without an attached developer tool
// either way, so gating it on gpu_device_available would over-restrict it
// relative to the compiled-in check that's actually sufficient.
PROFILERTEST(BackendCapabilities, nvtx_availability_does_not_require_a_device)
{
    const backend_capabilities& caps = discover_backend_capabilities();
    EXPECT_EQ(caps.nvtx_compiled, nvtx_enabled());
}

// session_options::policy defaults to best_effort (not design-review.md's
// eventual "required by default" target -- see capture_policy's own comment
// for why flipping the default here would be a silent breaking change).
PROFILERTEST(BackendCapabilities, policy_defaults_to_best_effort)
{
    EXPECT_EQ(session_options{}.policy, capture_policy::best_effort);
}

// A required, unavailable GPU activity must fail session::start() outright
// with a diagnosable reason, rather than silently degrading to CPU-only.
PROFILERTEST(BackendCapabilities, required_unavailable_gpu_activity_fails_start)
{
    const backend_capabilities& caps = discover_backend_capabilities();
    if (caps.supports(activity::cuda) || caps.supports(activity::hip))
    {
        GTEST_SKIP() << "A real GPU is available on this machine; this test needs an "
                        "activity this build/host genuinely cannot satisfy";
    }

    session_options opts;
    opts.native          = true;
    opts.instrumentation = true;
    opts.policy          = capture_policy::required;
    opts.activities      = {activity::cpu, activity::cuda};

    session session(opts);
    EXPECT_FALSE(session.start());
    EXPECT_FALSE(session.last_error().empty());
}

// The same request under best_effort (the default) must not be blocked by
// this same-process check -- whether it actually starts still depends on the
// compiled backend/OS, but it must never fail for the reason the required
// case above does.
PROFILERTEST(BackendCapabilities, best_effort_unavailable_gpu_activity_does_not_fail_for_that_reason)
{
    const backend_capabilities& caps = discover_backend_capabilities();
    if (caps.supports(activity::cuda) || caps.supports(activity::hip))
    {
        GTEST_SKIP() << "A real GPU is available on this machine; this test needs an "
                        "activity this build/host genuinely cannot satisfy";
    }

    session_options opts;
    opts.native          = true;
    opts.instrumentation = true;
    opts.policy          = capture_policy::best_effort;
    opts.activities      = {activity::cpu, activity::cuda};

    session session(opts);
    bool const started = session.start();
    if (started)
    {
        EXPECT_TRUE(session.stop());
    }
    // Whether or not it started, it must not have failed via the required-
    // policy pre-check's specific message.
    EXPECT_EQ(session.last_error().find("required activity unavailable"), std::string::npos);
}

// design-review.md section 4's actual compatibility-mapping ask: to_profiler_options()/
// to_capture_config() are the same conversion `session` uses internally, made public so
// code constructing profiler_session/capture directly can reuse it instead of re-deriving
// the mapping (or silently drifting from it in a second, hand-rolled copy).
PROFILERTEST(BackendCapabilities, to_profiler_options_maps_expected_fields)
{
    session_options opts;
    opts.memory_tracking      = true;
    opts.gpu_tracing          = true;
    opts.statistical_analysis = true;

    const profiler_options mapped = to_profiler_options(opts);
    EXPECT_TRUE(mapped.enable_timing_);
    EXPECT_TRUE(mapped.enable_hierarchical_profiling_);
    EXPECT_EQ(mapped.enable_memory_tracking_, opts.memory_tracking);
    EXPECT_EQ(mapped.enable_gpu_tracing_, opts.gpu_tracing);
    EXPECT_EQ(mapped.enable_statistical_analysis_, opts.statistical_analysis);
}

PROFILERTEST(BackendCapabilities, to_capture_config_maps_expected_fields)
{
    session_options opts;
    opts.backend             = capture_backend::itt;
    opts.activities          = {activity::cpu, activity::cuda};
    opts.with_stack          = true;
    opts.profile_memory      = true;
    opts.report_input_shapes = true;
    opts.with_flops          = true;
    opts.with_modules        = true;

    const capture_config cfg = to_capture_config(opts);
    EXPECT_EQ(cfg.backend, opts.backend);
    EXPECT_EQ(cfg.activities, opts.activities);
    EXPECT_EQ(cfg.profile_memory, opts.profile_memory);
    EXPECT_EQ(cfg.with_stack, opts.with_stack);
    EXPECT_EQ(cfg.report_input_shapes, opts.report_input_shapes);
    EXPECT_EQ(cfg.with_flops, opts.with_flops);
    EXPECT_EQ(cfg.with_modules, opts.with_modules);
}

// profiler_session's new session_options-taking constructor must behave
// identically to manually converting via to_profiler_options() first --
// proving it's a genuine convenience overload, not a second, subtly
// different construction path.
PROFILERTEST(BackendCapabilities, profiler_session_from_session_options_matches_manual_conversion)
{
    session_options opts;
    opts.memory_tracking      = true;
    opts.statistical_analysis = true;

    {
        profiler_session direct(opts);
        ASSERT_TRUE(direct.start());
        EXPECT_NE(direct.get_memory_tracker(), nullptr);
        ASSERT_TRUE(direct.stop());
    }
    {
        profiler_session manual(to_profiler_options(opts));
        ASSERT_TRUE(manual.start());
        EXPECT_NE(manual.get_memory_tracker(), nullptr);
        ASSERT_TRUE(manual.stop());
    }
}
