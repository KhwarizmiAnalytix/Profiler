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

/* Copyright 2018 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "native/cpu/host_tracer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/profiler_macros.h"
////#include "logger/logger.h"
#include "native/core/profiler_collection.h"
#include "native/core/profiler_interface.h"
#include "native/cpu/host_tracer_utils.h"
#include "native/cpu/threadpool_listener.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_builder.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/tracing/traceme.h"
#include "native/tracing/traceme_recorder.h"

namespace profiler::profiler_impl
{
namespace
{

// Controls traceme_recorder and converts traceme_recorder::Events into xevents.
//
// Thread-safety: This class is go/thread-compatible.
class host_tracer : public profiler_interface
{
public:
    explicit host_tracer(int host_trace_level, uint64_t filter_mask);
    ~host_tracer() override;

    profiler_status start() override;

    // cppcheck-suppress virtualCallInConstructor
    profiler_status stop() override;

    profiler_status collect_data(x_space* space) override;

private:
    // Level of host tracing.
    const int host_trace_level_;

    // Filter mask for selective event recording.
    const uint64_t filter_mask_;

    // True if currently recording.
    bool recording_ = false;

    // Timestamp at the start of tracing.
    uint64_t start_timestamp_ns_ = 0;

    // Container of all traced events.
    traceme_recorder::Events events_;
};

host_tracer::host_tracer(int host_trace_level, uint64_t filter_mask)
    : host_trace_level_(host_trace_level), filter_mask_(filter_mask)
{
}

host_tracer::~host_tracer()
{
    // Call stop directly without virtual dispatch since we're in destructor
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    // cppcheck-suppress virtualCallInConstructor
    host_tracer::stop();  // Explicitly call this class's stop() to avoid virtual dispatch
}

profiler_status host_tracer::start()
{
    if (recording_)
    {
        PROFILER_LOG_ERROR("TraceMeRecorder already started");
        return profiler_status::Error("TraceMeRecorder already started");
    }

    // All traceme captured should have a timestamp greater or equal to
    // start_timestamp_ns_ to prevent timestamp underflow in xplane.
    // Therefore this have to be done before traceme_recorder::start.
    start_timestamp_ns_ = get_current_time_nanos();
    recording_          = traceme_recorder::start(host_trace_level_, filter_mask_);
    if (!recording_)
    {
        PROFILER_LOG_ERROR("Failed to start TraceMeRecorder");
        return profiler_status::Error("Failed to start TraceMeRecorder");
    }
    return profiler_status::Ok();
}

profiler_status host_tracer::stop()
{
    if (!recording_)
    {
        PROFILER_LOG_ERROR("TraceMeRecorder not started");
        return profiler_status::Error("TraceMeRecorder not started");
    }
    events_    = traceme_recorder::stop();
    recording_ = false;
    return profiler_status::Ok();
}

profiler_status host_tracer::collect_data(x_space* space)
{
    //PROFILER_LOG_INFO("Collecting data to x_space from host_tracer.");  // NOLINT
    if (recording_)
    {
        PROFILER_LOG_ERROR("traceme_recorder not stopped");
        return profiler_status::Error("TraceMeRecorder not stopped");
    }
    if (events_.empty())
    {
        return profiler_status::Ok();
    }
    xplane* plane = find_or_add_mutable_plane_with_name(space, kHostThreadsPlaneName);
    if (plane == nullptr)
    {
        PROFILER_LOG_ERROR("Failed to obtain host threads XPlane.");
        return profiler_status::Error("Failed to obtain host threads XPlane.");
    }

    // Use host_tracer_utils to convert events to XPlane format
    // This handles annotation parsing, metadata extraction, and display names
    convert_complete_events_to_xplane(start_timestamp_ns_, std::move(events_), plane);

    return profiler_status::Ok();
}

}  // namespace

std::unique_ptr<profiler_interface> create_host_tracer(const host_tracer_options& options)
{
    if (options.trace_level == 0)
    {
        return nullptr;
    }
    std::vector<std::unique_ptr<profiler_interface>> profilers;
    profilers.push_back(std::make_unique<host_tracer>(options.trace_level, options.filter_mask));
    if (auto threadpool_profiler = create_threadpool_profiler())
    {
        profilers.push_back(std::move(threadpool_profiler));
    }
    return std::make_unique<profiler_collection>(std::move(profilers));
}
}  // namespace profiler::profiler_impl
