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

#include "native/tracing/traceme_recorder.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

////#include "logger/logger.h"
#include "common/flat_hash.h"
#include "common/lock_free_queue.h"
#include "common/per_thread.h"
#include "common/profiler_export.h"
#include "common/profiler_macros.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif
#endif

namespace profiler
{
static inline std::string get_thread_name()
{
    return "";
    //profiler::logger::GetThreadName();
}

namespace internal
{

std::atomic<int>      g_trace_level(traceme_recorder::kTracingDisabled);
std::atomic<uint64_t> g_trace_filter_bitmap{(std::numeric_limits<uint64_t>::max)()};

// g_trace_level implementation must be lock-free for faster execution of the
// TraceMe API. This can be commented (if compilation is failing) but execution
// might be slow (even when tracing is disabled).
// NOLINTNEXTLINE(misc-redundant-expression)
static_assert(ATOMIC_INT_LOCK_FREE == 2, "Assumed atomic<int> was lock free");

}  // namespace internal

namespace
{

// Track events created by ActivityStart and merge their data into events
// created by ActivityEnd. TraceMe records events in its destructor, so this
// results in complete events sorted by their end_time in the thread they ended.
// Within the same thread, the record created by ActivityStart must appear
// before the record created by ActivityEnd. Cross-thread events must be
// processed in a separate pass. A single map can be used because the
// activity_id is globally unique.
class SplitEventTracker
{
public:
    void AddStart(traceme_recorder::Event&& event)
    {
        // PROFILER_CHECK(event.is_start(), "event is not a start event");
        start_events_.emplace(event.activity_id(), std::move(event));
    }

    void AddEnd(traceme_recorder::Event* event)
    {
        // PROFILER_CHECK(event->is_end(), "event is not an end event");
        if (!FindStartAndMerge(event))
        {
            end_events_.push_back(event);
        }
    }

    void HandleCrossThreadEvents()
    {
        for (auto* event : end_events_)
        {
            FindStartAndMerge(event);
        }
    }

private:
    // Finds the start of the given event and merges data into it.
    bool FindStartAndMerge(traceme_recorder::Event* event)
    {
        auto iter = start_events_.find(event->activity_id());
        if (iter == start_events_.end())
        {
            return false;
        }
        auto& start_event = iter->second;
        event->name       = std::move(start_event.name);
        event->start_time = start_event.start_time;
        start_events_.erase(iter);
        return true;
    }

    // Start events are collected from each ThreadLocalRecorder::Consume() call.
    // Their data is merged into end_events.
    std::unordered_map<int64_t, traceme_recorder::Event> start_events_;

    // End events are stored in the output of TraceMeRecorder::Consume().
    std::vector<traceme_recorder::Event*> end_events_;
};

// To avoid unnecessary synchronization between threads, each thread has a
// ThreadLocalRecorder that independently records its events.
class ThreadLocalRecorder
{
public:
    // The recorder is created the first time TraceMeRecorder::Record() is called
    // on a thread.
    ThreadLocalRecorder()
    {
        /*auto* env = Env::Default();
        info_.tid = env->GetCurrentThreadId();
        env->GetCurrentThreadName(&info_.name);*/

#ifdef _WIN32
        info_.tid = static_cast<uint64_t>(::GetCurrentThreadId());
#elif defined(__APPLE__)
        uint64_t tid = 0;
        if (pthread_threadid_np(nullptr, &tid) == 0)
        {
            info_.tid = tid;
        }
        else
        {
            info_.tid = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pthread_self()));
        }
#elif defined(__linux__)
        info_.tid = static_cast<uint64_t>(::syscall(SYS_gettid));
#else
        info_.tid = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
        info_.name = get_thread_name();
    }

    PROFILER_NODISCARD const traceme_recorder::ThreadInfo& Info() const { return info_; }

    // Record is only called from the producer thread.
    void Record(traceme_recorder::Event&& event) { queue_.push(std::move(event)); }

    // Clear is called from the control thread when tracing starts to remove any
    // elements added due to Record racing with Consume.
    void Clear() { queue_.clear(); }

    // Consume is called from the control thread when tracing stops.
    PROFILER_NODISCARD std::deque<traceme_recorder::Event> Consume(
        SplitEventTracker* split_event_tracker)
    {
        std::deque<traceme_recorder::Event>    events;
        std::optional<traceme_recorder::Event> event;
        while ((event = queue_.pop()))
        {
            if (event->is_start())
            {
                split_event_tracker->AddStart(*std::move(event));
                continue;
            }
            events.push_back(*std::move(event));
            if (events.back().is_end())
            {
                split_event_tracker->AddEnd(&events.back());
            }
        }
        return events;
    }

private:
    traceme_recorder::ThreadInfo           info_;
    LockFreeQueue<traceme_recorder::Event> queue_;
};

}  // namespace

// This method is performance critical and should be kept fast. It is called
// when tracing starts.
/* static */ void traceme_recorder::clear()
{
    auto recorders = per_thread<ThreadLocalRecorder>::StartRecording();
    for (auto& recorder : recorders)
    {
        recorder->Clear();
    };
}

// This method is performance critical and should be kept fast. It is called
// when tracing stops.
/* static */ traceme_recorder::Events traceme_recorder::consume()
{
    traceme_recorder::Events result;
    SplitEventTracker        split_event_tracker;
    auto                     recorders = per_thread<ThreadLocalRecorder>::StopRecording();
    for (auto& recorder : recorders)
    {
        auto events = recorder->Consume(&split_event_tracker);
        if (!events.empty())
        {
            result.push_back({recorder->Info(), std::move(events)});
        }
    };
    split_event_tracker.HandleCrossThreadEvents();
    return result;
}

/* static */ bool traceme_recorder::start(int level)
{
    return start(level, kDefaultTraceFilter);
}

/* static */ bool traceme_recorder::start(int level, uint64_t filter_mask)
{
    level = level > 0 ? level : 0;

    // Set the filter bitmap
    internal::g_trace_filter_bitmap.store(filter_mask, std::memory_order_relaxed);

    int        expected = kTracingDisabled;
    bool const started =
        internal::g_trace_level.compare_exchange_strong(expected, level, std::memory_order_acq_rel);
    if (started)
    {
        // We may have old events in buffers because Record() raced with Stop().
        clear();
    }
    return started;
}

/* static */ void traceme_recorder::record(Event&& event)
{
    per_thread<ThreadLocalRecorder>::Get().Record(std::move(event));
}

/* static */ traceme_recorder::Events traceme_recorder::stop()
{
    traceme_recorder::Events events;
    if (internal::g_trace_level.exchange(kTracingDisabled, std::memory_order_acq_rel) !=
        kTracingDisabled)
    {
        events = consume();
    }
    // Clear the filter bitmap
    internal::g_trace_filter_bitmap.store(kDefaultTraceFilter, std::memory_order_relaxed);
    return events;
}

/*static*/ int64_t traceme_recorder::new_activity_id()
{
    // Activity IDs: To avoid contention over a counter, the top 32 bits identify
    // the originating thread, the bottom 32 bits name the event within a thread.
    // IDs may be reused after 4 billion events on one thread, or 2 billion
    // threads.
    static std::atomic<int32_t>       thread_counter(1);  // avoid kUntracedActivity
    const thread_local static int32_t thread_id =
        thread_counter.fetch_add(1, std::memory_order_relaxed);
    thread_local static uint32_t per_thread_activity_id = 0;
    return static_cast<int64_t>(thread_id) << 32 | per_thread_activity_id++;
}

}  // namespace profiler
