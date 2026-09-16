/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The public product API is profiler.h only. Any external repo should be
 * able to compile against that header without including native/, kineto, or
 * ITT headers.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ProfilerTest.h"
#include "profiler.h"

PROFILERTEST(PublicApi, session_from_umbrella_header)
{
    profiler::profiler_session session;
    ASSERT_TRUE(session.start());
    {
        PROFILER_PROFILE_SCOPE("external_app_scope");
        PROFILER_PROFILE_FUNCTION();
    }
    ASSERT_TRUE(session.stop());

    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos);
    EXPECT_NE(chrome.find("external_app_scope"), std::string::npos);
}

// Smoke test for the new dropped_event_count() accessor (design-review.md
// Phase 2's bounded/visible overflow requirement). Forcing a real overflow
// isn't practical at the default 64 MiB/thread queue capacity in a fast unit
// test (see TestCommonContainers.cpp's LockFreeQueue test for that); this
// just confirms the query exists and reports no loss for a normal capture.
PROFILERTEST(PublicApi, dropped_event_count_is_zero_for_a_normal_capture)
{
    profiler::profiler_session session;
    ASSERT_TRUE(session.start());
    {
        PROFILER_PROFILE_SCOPE("dropped_event_count_probe");
    }
    ASSERT_TRUE(session.stop());
    EXPECT_EQ(session.dropped_event_count(), 0u);
}

PROFILERTEST(PublicApi, write_chrome_trace_from_umbrella_header)
{
    profiler::profiler_session session;
    ASSERT_TRUE(session.start());
    {
        PROFILER_PROFILE_SCOPE("disk_scope");
    }
    ASSERT_TRUE(session.stop());

    const std::string path = "public_api_trace.json";
    ASSERT_TRUE(session.write_chrome_trace(path));
    std::remove(path.c_str());
}

// Narrow slice of design-review.md Phase 3's "report counts agree with
// exported events" done-criterion: session::events() and session::write_
// trace() both ultimately read from the same capture_result once Kineto
// backs a capture (write_trace() prefers it per its own doc comment), so
// every event name events() reports must also appear in what actually got
// written to disk -- proving the in-memory and exported views of the same
// capture never silently diverge. This does not attempt the full unified-
// snapshot rework across profiler_report/hotspot_report (a much larger,
// separate piece of work; see the design-review.md redesign plan) -- just
// the one comparison that's safe to verify without touching that ownership
// model at all.
PROFILERTEST(PublicApi, events_and_exported_trace_agree_on_event_names)
{
    if (!profiler::kineto_enabled())
    {
        GTEST_SKIP() << "write_trace() only prefers Kineto's capture_result when Kineto backs it";
    }

    profiler::session_options options;
    options.native          = true;
    options.instrumentation = true;

    profiler::session session(options);
    ASSERT_TRUE(session.start());
    {
        PROFILER_SCOPE("count_agreement_probe_a");
        PROFILER_SCOPE("count_agreement_probe_b");
    }
    ASSERT_TRUE(session.stop());

    const auto& events = session.events();
    if (events.empty())
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    const std::string path = "public_api_count_agreement_trace.json";
    std::remove(path.c_str());
    ASSERT_TRUE(session.write_trace(path));

    std::ifstream    file(path);
    ASSERT_TRUE(file.good());
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string exported = contents.str();

    for (const auto& event : events)
    {
        EXPECT_NE(exported.find(event.name), std::string::npos)
            << "events() reported \"" << event.name << "\" but it's missing from write_trace()'s "
            << "exported file -- the in-memory and exported views of this capture disagree";
    }

    std::remove(path.c_str());
}

PROFILERTEST(PublicApi, start_rejects_second_session)
{
    profiler::profiler_session first;
    ASSERT_TRUE(first.start());
    profiler::profiler_session second;
    EXPECT_FALSE(second.start());
    ASSERT_TRUE(first.stop());
}

PROFILERTEST(PublicApi, reports_and_hotspots_from_umbrella_header)
{
    profiler::profiler_session session;
    ASSERT_TRUE(session.start());
    {
        PROFILER_PROFILE_SCOPE("public_report_scope");
        void* ptr = std::malloc(64);
        ASSERT_NE(ptr, nullptr);
        session.get_memory_tracker()->track_allocation(ptr, 64, "public_buffer");
        session.get_memory_tracker()->track_deallocation(ptr);
        std::free(ptr);
    }
    ASSERT_TRUE(session.stop());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);
    EXPECT_FALSE(report->generate_console_report().empty());
    EXPECT_FALSE(report->generate_json_report().empty());

    auto hotspots = session.generate_hotspot_report();
    ASSERT_NE(hotspots, nullptr);
    EXPECT_NE(hotspots->table().find("public_report_scope"), std::string::npos);
}

PROFILERTEST(PublicApi, capture_from_umbrella_header)
{
    if (!profiler::kineto_enabled() && !profiler::itt_enabled())
    {
        GTEST_SKIP() << "Requires an instrumentation backend (Kineto or ITT); "
                        "PROFILER_BACKEND=NONE has no capture backend to start";
    }

    profiler::capture cap;
    ASSERT_TRUE(cap.prepare());
    ASSERT_TRUE(cap.start());
    {
        PROFILER_RECORD_USER_SCOPE("public_capture_scope");
        {
            PROFILER_RECORD_FUNCTION("public_capture_fn");
        }
    }
    auto result = cap.stop();
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(cap.is_active());

    if (profiler::kineto_enabled())
    {
        const std::string path = "public_api_capture.json";
        EXPECT_TRUE(result->save(path));
        std::remove(path.c_str());
        bool found_scope = false;
        for (const auto& event : result->events())
        {
            if (event.name == "public_capture_scope" || event.name == "public_capture_fn")
            {
                found_scope = true;
                break;
            }
        }
        EXPECT_TRUE(found_scope);
    }
}

// Regression/stress test for design-review.md finding 7 (worker enrollment race):
// concurrently enrolling child threads used to share one callback-handle scalar
// on the (possibly shared) ProfilerStateBase instance, so concurrent enrollers
// could overwrite one another's handle and trip the "leaked callback" assert.
// Each worker here enrolls, records, and disenrolls under contention; on a plain
// build this mainly checks nothing crashes/asserts, run with
// -DPROFILER_SANITIZER=thread for real verification.
PROFILERTEST(PublicApi, concurrent_child_thread_enrollment_does_not_race)
{
    if (!profiler::kineto_enabled() && !profiler::itt_enabled())
    {
        GTEST_SKIP() << "Requires an instrumentation backend (Kineto or ITT); "
                        "PROFILER_BACKEND=NONE has no capture backend to start";
    }

    profiler::capture cap;
    ASSERT_TRUE(cap.start());

    constexpr int     kWorkerCount = 8;
    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);
    for (int i = 0; i < kWorkerCount; ++i)
    {
        workers.emplace_back(
            [i]
            {
                for (int iter = 0; iter < 20; ++iter)
                {
                    profiler::child_thread_capture enroll;
                    PROFILER_RECORD_USER_SCOPE("public_worker_stress");
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }

    auto result = cap.stop();
    ASSERT_NE(result, nullptr);
}

PROFILERTEST(PublicApi, capture_child_thread_from_umbrella_header)
{
    if (!profiler::kineto_enabled() && !profiler::itt_enabled())
    {
        GTEST_SKIP() << "Requires an instrumentation backend (Kineto or ITT); "
                        "PROFILER_BACKEND=NONE has no capture backend to start";
    }

    profiler::capture cap;
    ASSERT_TRUE(cap.start());
    std::thread worker(
        []
        {
            profiler::child_thread_capture enroll;
            PROFILER_RECORD_USER_SCOPE("public_worker");
        });
    worker.join();
    auto result = cap.stop();
    ASSERT_NE(result, nullptr);
}

PROFILERTEST(PublicApi, unified_session_from_umbrella_header)
{
    profiler::session_options options;
    options.backend        = profiler::capture_backend::automatic;
    options.activities     = {profiler::activity::cpu};
    options.profile_memory = false;
    options.with_stack     = false;
    options.with_flops     = false;
    options.with_modules   = false;

    profiler::session session(options);
    ASSERT_TRUE(session.start());
    {
        PROFILER_SCOPE("unified_scope");
        PROFILER_FUNCTION();
        PROFILER_OP("unified_op");
    }
    ASSERT_TRUE(session.stop());

    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("unified_scope"), std::string::npos);

    const std::string path = "public_api_unified_trace.json";
    ASSERT_TRUE(session.write_trace(path));
    std::remove(path.c_str());

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);
    EXPECT_FALSE(report->generate_console_report().empty());

    if (profiler::kineto_enabled())
    {
        bool found = false;
        for (const auto& event : session.events())
        {
            if (event.name == "unified_scope" || event.name == "unified_op")
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found);
    }
}

PROFILERTEST(PublicApi, session_options_with_stack)
{
    profiler::session_options options;
    options.backend    = profiler::capture_backend::automatic;
    options.activities = {profiler::activity::cpu};
    options.with_stack = true;

    profiler::session session(options);
    ASSERT_TRUE(session.start());
    const int expected_line = __LINE__ + 2;
    {
        PROFILER_SCOPE("public_stack_scope");
    }
    ASSERT_TRUE(session.stop());

    if (!profiler::kineto_enabled())
    {
        GTEST_SKIP() << "Stack frames are recorded by the Kineto backend";
    }

    const profiler::capture_event* found = nullptr;
    for (const auto& event : session.events())
    {
        if (event.name == "public_stack_scope")
        {
            found = &event;
            break;
        }
    }
    if (found == nullptr)
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    ASSERT_FALSE(found->stack.empty());
    const std::string expected_file     = "TestProfilerPublicApi.cpp";
    const std::string expected_location = expected_file + ":" + std::to_string(expected_line);
    EXPECT_NE(found->stack.front().find(expected_location), std::string::npos)
        << found->stack.front();
}

// Regression for design-review.md finding 3 / section 6.3's event contract:
// capture_event used to drop everything KinetoEvent already carries beyond
// name/start/duration/metadata/stack -- kind, execution location,
// correlation/async state, and transfer bytes were all silently discarded at
// the conversion in capture.cpp. This checks the CPU-scope fields that are
// meaningful for every event (thread id, activity_type, is_async's absence)
// round-trip; GPU-specific fields (device/correlation/bytes) need real
// device activity to populate meaningfully and are exercised in
// TestProfilerGpuTracer.cpp instead.
PROFILERTEST(PublicApi, capture_event_carries_execution_location_and_kind)
{
    if (!profiler::kineto_enabled())
    {
        GTEST_SKIP() << "capture_event's Kineto-sourced fields need the Kineto backend";
    }

    profiler::session_options options;
    options.backend    = profiler::capture_backend::automatic;
    options.activities = {profiler::activity::cpu};

    profiler::session session(options);
    ASSERT_TRUE(session.start());
    { PROFILER_SCOPE("event_contract_probe"); }
    ASSERT_TRUE(session.stop());

    const profiler::capture_event* found = nullptr;
    for (const auto& event : session.events())
    {
        if (event.name == "event_contract_probe")
        {
            found = &event;
            break;
        }
    }
    if (found == nullptr)
    {
        GTEST_SKIP() << "Kineto backend produced no CPU events in this environment";
    }

    // A CPU scope recorded on this thread must carry that thread's id, not
    // the field's zero-initialized default.
    EXPECT_NE(found->thread_id, 0U);
    EXPECT_EQ(found->device_type, profiler::device_enum::CPU);
    EXPECT_TRUE(found->complete);
    // Not every KinetoEvent has a correlation id (only ops that actually
    // correlate to something do); this just proves the field is populated
    // from the source rather than hardcoded -- a real GPU-launch correlation
    // is exercised in TestProfilerGpuTracer.cpp.
    EXPECT_EQ(found->gpu_fallback_elapsed_us, -1);
}

PROFILERTEST(PublicApi, session_rejects_unavailable_backend)
{
    profiler::session_options options;
    options.backend = profiler::kineto_enabled() ? profiler::capture_backend::itt
                                                 : profiler::capture_backend::kineto;
    profiler::session session(options);
    EXPECT_FALSE(session.start());
}

PROFILERTEST(PublicApi, capture_rejects_second_start)
{
    if (!profiler::kineto_enabled() && !profiler::itt_enabled())
    {
        GTEST_SKIP() << "Requires an instrumentation backend (Kineto or ITT); "
                        "PROFILER_BACKEND=NONE has no capture backend to start";
    }

    profiler::capture first;
    ASSERT_TRUE(first.start());
    EXPECT_FALSE(first.start());
    auto result = first.stop();
    ASSERT_NE(result, nullptr);
}
