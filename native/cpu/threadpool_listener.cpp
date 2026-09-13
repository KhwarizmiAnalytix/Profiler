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

#include "native/cpu/threadpool_listener.h"

#include <string>
#include <utility>

////#include "logger/logger.h"
#include "native/cpu/threadpool_listener_state.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/tracing/traceme.h"
#include "native/tracing/traceme_encode.h"
#include "native/tracing/traceme_recorder.h"

namespace profiler::profiler_impl
{
namespace
{
threadpool_event_collector* get_threadpool_event_collector()
{
    static auto* collector = new threadpool_event_collector();
    return collector;
}

void register_threadpool_event_collector(const threadpool_event_collector* collector)
{
    tracing::set_event_collector(tracing::event_category::kScheduleClosure, collector);
    tracing::set_event_collector(tracing::event_category::kRunClosure, collector);
}

void unregister_threadpool_event_collector()
{
    tracing::set_event_collector(tracing::event_category::kScheduleClosure, nullptr);
    tracing::set_event_collector(tracing::event_category::kRunClosure, nullptr);
}

}  // namespace

void threadpool_event_collector::record_event(uint64_t arg) const
{
    int64_t const now = get_current_time_nanos();
    traceme_recorder::record(
        {traceme_encode(
             kThreadpoolListenerRecord,
             {{"_pt", static_cast<int64_t>(ContextType::kThreadpoolEvent)},
              {"_p", static_cast<int64_t>(arg)}}),
         now,
         now});
}

void threadpool_event_collector::start_region(uint64_t arg) const
{
    int64_t const now = get_current_time_nanos();
    traceme_recorder::record(
        {traceme_encode(
             kThreadpoolListenerStartRegion,
             {{"_ct", static_cast<int64_t>(ContextType::kThreadpoolEvent)},
              {"_c", static_cast<int64_t>(arg)}}),
         now,
         now});
}

void threadpool_event_collector::stop_region() const
{
    int64_t const now = get_current_time_nanos();
    traceme_recorder::record({traceme_encode(kThreadpoolListenerStopRegion, {}), now, now});
}

profiler_status threadpool_profiler_interface::start()
{
    if (tracing::event_collector::is_enabled())
    {
        /* PROFILER_LOG_WARNING(
            "[ThreadpoolEventCollector] event collector already enabled; "
            "threadpool events will not be captured."); */
        last_status_ = profiler_status::Error(
            "ThreadpoolEventCollector already enabled; not collecting threadpool events.");
        return profiler_status::Ok();
    }

    register_threadpool_event_collector(get_threadpool_event_collector());
    threadpool_listener::Activate();
    last_status_ = profiler_status::Ok();
    return last_status_;
}

profiler_status threadpool_profiler_interface::stop()
{
    threadpool_listener::Deactivate();
    unregister_threadpool_event_collector();
    return profiler_status::Ok();
}

profiler_status threadpool_profiler_interface::collect_data(x_space* space)
{
    if (!last_status_.ok() && !last_status_.message().empty() && space != nullptr)
    {
        space->add_error(last_status_.message());
    }
    return last_status_;
}

std::unique_ptr<profiler_interface> create_threadpool_profiler()
{
    return std::make_unique<threadpool_profiler_interface>();
}

}  // namespace profiler::profiler_impl
