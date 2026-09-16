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
 * Regression tests for the facade profiler::session (common/session.h),
 * covering the reproduced defects in docs/design-review.md section 3
 * (findings 1, 2, 4, 5, 6). Finding 8 (false scope parenting) is covered in
 * TestScopeTreeBuilder.cpp instead, alongside the rest of that builder's
 * interval-containment tests.
 */

#include <cstdio>
#include <fstream>
#include <string>

#include "ProfilerTest.h"
#include "profiler.h"

namespace
{
bool file_exists(const std::string& path)
{
    std::ifstream f(path);
    return f.good();
}
}  // namespace

// Finding 1: a second export to a distinct, fresh path used to return true
// (ActivityTraceWrapper::save was a silent no-op after the first call, and
// ProfilerResult::save reported success based on the trace object still
// existing rather than whether this call wrote anything) while writing
// nothing. native=false isolates this from session::write_trace()'s native
// Chrome-trace fallback so the instrumentation path's own result is observed
// directly.
PROFILERTEST(LifecycleRegressions, second_export_does_not_report_false_success)
{
    if (!profiler::kineto_enabled())
    {
        // ITT doesn't produce a savable trace object at all (capture_result::save()
        // returns false on the very first call, not just the second) -- this
        // regression is specifically about Kineto's ActivityTraceWrapper.
        GTEST_SKIP() << "Requires the Kineto instrumentation backend";
    }

    profiler::session_options opts;
    opts.native          = false;
    opts.instrumentation = true;

    profiler::session session(opts);
    ASSERT_TRUE(session.start());
    { PROFILER_SCOPE("lifecycle_export_scope"); }
    ASSERT_TRUE(session.stop());

    const std::string path1 = "lifecycle_regression_export_1.json";
    const std::string path2 = "lifecycle_regression_export_2.json";
    std::remove(path1.c_str());
    std::remove(path2.c_str());

    ASSERT_TRUE(session.write_trace(path1));
    EXPECT_TRUE(file_exists(path1));

    EXPECT_FALSE(session.write_trace(path2));
    EXPECT_FALSE(file_exists(path2));

    std::remove(path1.c_str());
}

// Regression for design-review.md section 6.7 ("no format switch on I/O
// error"): with both native and instrumentation enabled, a Kineto save
// failure (here: the same one-shot second-call trigger the test above uses)
// used to fall through to native_->write_chrome_trace(path) -- silently
// writing a real Chrome-trace file, in a different schema, at the path a
// Kineto trace was requested at. session::write_trace() must now report the
// Kineto failure directly and never touch native_ once inst_result_ exists.
// (The test above uses native=false specifically to avoid exercising this
// fallback at all; this one deliberately enables it to prove it's gone.)
PROFILERTEST(LifecycleRegressions, write_trace_does_not_switch_format_on_kineto_failure)
{
    if (!profiler::kineto_enabled())
    {
        GTEST_SKIP() << "Requires the Kineto instrumentation backend";
    }

    profiler::session_options opts;
    opts.native          = true;
    opts.instrumentation = true;

    profiler::session session(opts);
    ASSERT_TRUE(session.start());
    { PROFILER_SCOPE("write_trace_no_format_switch_scope"); }
    ASSERT_TRUE(session.stop());

    const std::string path1 = "lifecycle_no_format_switch_1.json";
    const std::string path2 = "lifecycle_no_format_switch_2.json";
    std::remove(path1.c_str());
    std::remove(path2.c_str());

    ASSERT_TRUE(session.write_trace(path1));
    EXPECT_TRUE(file_exists(path1));

    EXPECT_FALSE(session.write_trace(path2));
    EXPECT_FALSE(file_exists(path2));

    std::remove(path1.c_str());
}

// Finding 2: capture_backend::nvtx was reported available unconditionally,
// even on a build with no CUDA/NVTX support compiled in, so an explicitly
// requested NVTX capture would falsely report success from start().
PROFILERTEST(LifecycleRegressions, explicit_nvtx_request_fails_without_cuda_support)
{
    profiler::session_options opts;
    opts.native          = false;
    opts.instrumentation = true;
    opts.backend         = profiler::capture_backend::nvtx;

    profiler::session session(opts);
#if PROFILER_HAS_CUDA
    // NVTX genuinely available on this build; nothing to regress-test here.
    if (session.start())
    {
        ASSERT_TRUE(session.stop());
    }
#else
    EXPECT_FALSE(session.start());
#endif
}

// Finding 4: session::start() didn't clear the previous run's instrumentation
// result, so events()/write_trace() during a second run could still observe
// the first run's data.
PROFILERTEST(LifecycleRegressions, restart_does_not_leak_previous_run_events)
{
    if (!profiler::kineto_enabled() && !profiler::itt_enabled())
    {
        GTEST_SKIP() << "native=false needs an instrumentation backend to start at all; "
                        "PROFILER_BACKEND=NONE has none";
    }

    profiler::session_options opts;
    opts.native          = false;
    opts.instrumentation = true;

    profiler::session session(opts);
    ASSERT_TRUE(session.start());
    { PROFILER_SCOPE("first_run_scope"); }
    ASSERT_TRUE(session.stop());

    ASSERT_TRUE(session.start());
    // Second run active, nothing recorded (and not yet stopped) -- events()
    // must not still show the first run's scope.
    for (const auto& event : session.events())
    {
        EXPECT_NE(event.name, "first_run_scope");
    }
    ASSERT_TRUE(session.stop());
}

// Phase 3 Part B: events() used to read only the instrumentation (Kineto/ITT)
// capture result, always returning empty for a native-only session
// (instrumentation=false) even though that session captured real XSpace data.
// It now falls back to a conversion from XSpace, matching write_trace()'s
// existing Kineto-then-XSpace fallback order.
PROFILERTEST(LifecycleRegressions, events_falls_back_to_xspace_for_native_only_session)
{
    profiler::session_options opts;
    opts.native          = true;
    opts.instrumentation = false;

    profiler::session session(opts);
    ASSERT_TRUE(session.start());
    { PROFILER_SCOPE("native_only_events_scope"); }
    ASSERT_TRUE(session.stop());

    bool found = false;
    for (const auto& event : session.events())
    {
        if (event.name == "native_only_events_scope")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Finding 5: profiler_report/hotspot_report held a raw reference/pointer into
// the native profiler_session; session::start() replaces its native session
// on every call (a fresh profiler_session, not a reused one), which used to
// dangle any report/hotspot report generated from a previous run.
PROFILERTEST(LifecycleRegressions, report_outlives_a_session_restart)
{
    profiler::session_options opts;
    profiler::session         session(opts);

    ASSERT_TRUE(session.start());
    { PROFILER_SCOPE("report_lifetime_scope"); }
    ASSERT_TRUE(session.stop());

    auto report   = session.generate_report();
    auto hotspots = session.generate_hotspot_report();
    ASSERT_NE(report, nullptr);
    ASSERT_NE(hotspots, nullptr);

    // Restart (and stop again) replaces the session's native profiler_session.
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(session.stop());

    // The old report/hotspot report must still be safe to read.
    EXPECT_FALSE(report->generate_console_report().empty());
    EXPECT_NE(hotspots->table().find("report_lifetime_scope"), std::string::npos);
}

// Finding 6: session::get_memory_tracker() dereferenced native_/memory_tracker_
// unconditionally, crashing before start() or when memory_tracking wasn't
// requested (the default).
PROFILERTEST(LifecycleRegressions, get_memory_tracker_is_null_when_unavailable)
{
    profiler::session_options opts;
    profiler::session         session(opts);

    // Before start(): no native session constructed yet.
    EXPECT_EQ(session.get_memory_tracker(), nullptr);

    // memory_tracking defaults to false.
    ASSERT_TRUE(session.start());
    EXPECT_EQ(session.get_memory_tracker(), nullptr);
    ASSERT_TRUE(session.stop());

    opts.memory_tracking = true;
    profiler::session tracked_session(opts);
    ASSERT_TRUE(tracked_session.start());
    EXPECT_NE(tracked_session.get_memory_tracker(), nullptr);
    ASSERT_TRUE(tracked_session.stop());
}
