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
 * =============================================================================
 * End-to-end: native profiler + threadpool listener + tracing surface
 * =============================================================================
 *
 * host_tracer always installs threadpool_profiler_interface, which registers
 * tracing::event_collector for ScheduleClosure / RunClosure and flips
 * threadpool_listener::IsEnabled(). Production threadpools (Eigen/TF style)
 * are expected to call:
 *
 *   tracing::record_event(kScheduleClosure, task_id)   // enqueue
 *   tracing::scoped_region(kRunClosure, ...)           // worker body
 *
 * No in-tree Parallel pool emits those hooks yet, so this use-case test runs a
 * small std::thread pool that instruments itself the same way, then drives
 * TraceMe encode / async activity APIs for the actual work. Assertions go
 * through profiler_session → Chrome Trace + structured report — not isolated
 * TraceMe unit checks — so dead collector / encode paths show up as missing
 * timeline events.
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ProfilerTest.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/session/profiler.h"
#include "native/session/profiler_report.h"
#include "native/tracing/traceme.h"
#include "native/tracing/traceme_encode.h"
#include "native/tracing/tracing.h"

using namespace profiler;

namespace
{

constexpr const char* kSessionScope  = "threadpool_session_root";
constexpr const char* kWorkerCompute = "threadpool_worker_compute";
constexpr const char* kAsyncSubtask  = "threadpool_async_subtask";
constexpr int         kWorkerCount   = 3;
constexpr int         kTaskCount     = 8;

// Minimal pool that mirrors Eigen/TF instrumentation around enqueue + run.
class instrumented_thread_pool
{
public:
    explicit instrumented_thread_pool(int worker_count) : stop_(false)
    {
        workers_.reserve(static_cast<size_t>(worker_count));
        for (int i = 0; i < worker_count; ++i)
        {
            workers_.emplace_back([this, i]() { worker_main(i); });
        }
    }

    ~instrumented_thread_pool()
    {
        {
            std::lock_guard<std::mutex> const lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void submit(uint64_t task_id, std::function<void()> job)
    {
        // Schedule path: same hook a real pool fires when enqueuing work.
        tracing::record_event(tracing::event_category::kScheduleClosure, task_id);
        {
            std::lock_guard<std::mutex> const lock(mutex_);
            jobs_.push(std::move(job));
        }
        cv_.notify_one();
    }

private:
    void worker_main(int worker_index)
    {
        const std::string thread_name =
            std::string(tracing::get_event_category_name(tracing::event_category::kRunClosure)) +
            "_worker_" + std::to_string(worker_index);
        tracing::event_collector::set_current_thread_name(thread_name.c_str());

        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stop_ || !jobs_.empty(); });
                if (stop_ && jobs_.empty())
                {
                    return;
                }
                job = std::move(jobs_.front());
                jobs_.pop();
            }

            // Run path: scoped_region → collector → traceme_encode → recorder.
            tracing::scoped_region region(
                tracing::event_category::kRunClosure, std::string_view(thread_name));
            // Move keeps the collector alive across the hand-off Eigen uses.
            tracing::scoped_region active = std::move(region);
            if (!active.is_enabled())
            {
                // Session must have activated threadpool_listener; fail loudly.
                job();
                continue;
            }
            job();
        }
    }

    std::vector<std::thread>          workers_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    std::queue<std::function<void()>> jobs_;
    bool                              stop_;
};

double run_dense_kernel(int size, int task_id)
{
    std::vector<double> lhs(static_cast<size_t>(size * size), 1.0 + 0.01 * task_id);
    std::vector<double> rhs(static_cast<size_t>(size * size), 0.5);
    std::vector<double> out(static_cast<size_t>(size * size), 0.0);

    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            double sum = 0.0;
            for (int inner = 0; inner < size; ++inner)
            {
                sum += lhs[static_cast<size_t>(row * size + inner)] *
                       rhs[static_cast<size_t>(inner * size + col)];
            }
            out[static_cast<size_t>(row * size + col)] = sum;
        }
    }

    double total = 0.0;
    for (double value : out)
    {
        total += value;
    }
    return total;
}

void run_profiled_worker_task(profiler_session* session, int task_id, std::atomic<double>* sink)
{
    profiler_scope compute_scope(kWorkerCompute, session);

    // Named op with structured metadata — same pattern production kernels use.
    traceme encoded_op(
        [&]()
        {
            return traceme_encode(
                traceme_op("MatMul", "CPU"),
                {{"task_id", task_id},
                 {"rows", 16},
                 {"dtype", "f64"},
                 {"fused", true},
                 {"label", std::string_view("worker")},
                 {"tag", "pool"},
                 {"c_str", "ok"}});
        });

    encoded_op.append_metadata(
        [&]() { return traceme_encode({{"stage", "execute"}, {"items", task_id}}); });
    // TF-style display override metadata merged into the active TraceMe name.
    encoded_op.append_metadata([&]() { return traceme_op_override("MatMul", "CPU"); });

    // Async-style split events (start/end) on the worker thread.
    const int64_t activity_id = traceme::activity_start(
        [&]() { return traceme_encode(kAsyncSubtask, {{"task_id", task_id}, {"async", true}}); });

    const double value = run_dense_kernel(16, task_id);
    sink->store(sink->load(std::memory_order_relaxed) + value, std::memory_order_relaxed);

    traceme::activity_end(activity_id);

    traceme::instant_activity(
        [&]()
        {
            return traceme_encode("threadpool_checkpoint", {{"task_id", task_id}, {"done", true}});
        });

    // Unique-arg region covers get_unique_arg / unnamed scoped_region ctor.
    tracing::scoped_region unique_region(tracing::event_category::kRunClosure);
    (void)unique_region;

    // Compute category has no collector registered — still a valid call site.
    tracing::scoped_region compute_region(
        tracing::event_category::kCompute, tracing::get_unique_arg());
    (void)compute_region;
}

profiler_options make_options()
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_memory_tracking_        = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_statistical_analysis_   = true;
    opts.track_memory_deltas_           = true;
    opts.track_peak_memory_             = true;
    opts.output_format_                 = profiler_options::output_format_enum::JSON;
    return opts;
}

}  // namespace

// -----------------------------------------------------------------------------
// Use case: parallel pool work under a live native profiler session.
// Pipeline: session.start → host_tracer + threadpool collectors → instrumented
// pool (record_event / scoped_region) → TraceMe encode / activity → Chrome +
// report. Fails if listener/encode paths never produce timeline events.
// -----------------------------------------------------------------------------
PROFILERTEST(BackendThreadpoolTracing, end_to_end_pool_feeds_chrome_and_report)
{
    ASSERT_NE(tracing::get_log_dir(), nullptr);
    EXPECT_STREQ(
        tracing::get_event_category_name(tracing::event_category::kScheduleClosure),
        "ScheduleClosure");
    EXPECT_STREQ(tracing::get_event_category_name(tracing::event_category::kCompute), "Compute");
    EXPECT_STREQ(
        tracing::get_event_category_name(tracing::event_category::kNumCategories), "Unknown");

    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(tracing::event_collector::is_enabled());

    std::atomic<double> sink{0.0};
    {
        profiler_scope           root(kSessionScope, &session);
        instrumented_thread_pool pool(kWorkerCount);

        for (int task_id = 0; task_id < kTaskCount; ++task_id)
        {
            pool.submit(
                static_cast<uint64_t>(task_id),
                [&session, &sink, task_id]()
                { run_profiled_worker_task(&session, task_id, &sink); });
        }
    }

    ASSERT_TRUE(session.stop());
    EXPECT_GT(sink.load(std::memory_order_relaxed), 0.0);

    const std::string chrome = session.generate_chrome_trace_json();
    ASSERT_FALSE(chrome.empty());
    EXPECT_NE(chrome.find("\"traceEvents\""), std::string::npos);

    // Session hierarchy from profiler_scope.
    EXPECT_NE(chrome.find(kSessionScope), std::string::npos);
    EXPECT_NE(chrome.find(kWorkerCompute), std::string::npos);

    // Threadpool collector → traceme_encode names (metadata stripped to base).
    EXPECT_NE(chrome.find(std::string(kThreadpoolListenerRecord)), std::string::npos)
        << "ScheduleClosure record_event never reached threadpool_event_collector";
    EXPECT_NE(chrome.find(std::string(kThreadpoolListenerStartRegion)), std::string::npos)
        << "RunClosure scoped_region start never reached collector";
    EXPECT_NE(chrome.find(std::string(kThreadpoolListenerStopRegion)), std::string::npos)
        << "RunClosure scoped_region stop never reached collector";

    // Encoded op name uses traceme_op → "MatMul:CPU" as XPlane/Chrome event name.
    EXPECT_NE(chrome.find("MatMul:CPU"), std::string::npos);
    EXPECT_NE(chrome.find(kAsyncSubtask), std::string::npos);
    EXPECT_NE(chrome.find("threadpool_checkpoint"), std::string::npos);

    auto report = session.generate_report();
    ASSERT_NE(report, nullptr);
    const std::string console = report->generate_console_report();
    EXPECT_FALSE(console.empty());
    EXPECT_NE(console.find(kSessionScope), std::string::npos);
    EXPECT_NE(console.find(kWorkerCompute), std::string::npos);

    const std::string json_report = report->generate_json_report();
    EXPECT_NE(json_report.find(kWorkerCompute), std::string::npos);

    std::cout << "\n=== Threadpool tracing console report ===\n" << console << std::flush;
}

// -----------------------------------------------------------------------------
// Use case: collectors inactive outside a session — schedule/run must no-op
// without crashing, and a later session must still capture pool traffic.
// -----------------------------------------------------------------------------
PROFILERTEST(BackendThreadpoolTracing, pool_instrumentation_is_session_gated)
{
    EXPECT_FALSE(tracing::event_collector::is_enabled());
    {
        // No session: hooks are safe no-ops (collector lookup returns nullptr).
        tracing::record_event(tracing::event_category::kScheduleClosure, 1);
        tracing::scoped_region region(tracing::event_category::kRunClosure, "idle");
        EXPECT_FALSE(region.is_enabled());
    }

    profiler_session session(make_options());
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(tracing::event_collector::is_enabled());

    {
        profiler_scope           root(kSessionScope, &session);
        instrumented_thread_pool pool(2);
        std::atomic<int>         done{0};
        for (int i = 0; i < 4; ++i)
        {
            pool.submit(
                static_cast<uint64_t>(i),
                [&session, &done, i]()
                {
                    profiler_scope  scope(kWorkerCompute, &session);
                    volatile double sink = 0.0;
                    for (int n = 0; n < 200; ++n)
                    {
                        sink += static_cast<double>(n + i) * 0.001;
                    }
                    (void)sink;
                    done.fetch_add(1, std::memory_order_relaxed);
                });
        }
        while (done.load(std::memory_order_relaxed) < 4)
        {
            std::this_thread::yield();
        }
    }

    ASSERT_TRUE(session.stop());
    EXPECT_FALSE(tracing::event_collector::is_enabled());

    const std::string chrome = session.generate_chrome_trace_json();
    EXPECT_NE(chrome.find(std::string(kThreadpoolListenerRecord)), std::string::npos);
    EXPECT_NE(chrome.find(kWorkerCompute), std::string::npos);
}
