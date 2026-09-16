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

/* Copyright 2020 The OpenXLA Authors.

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

#include "native/gpu/gpu_event_collector.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

#include "native/core/timespan.h"
#include "native/cpu/annotation_stack.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_builder.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"

namespace profiler::profiler_impl
{
namespace
{

const char* event_type_name(gpu_tracer_event_type type)
{
    switch (type)
    {
    case gpu_tracer_event_type::memcpy_h2d:
        return "MemcpyH2D";
    case gpu_tracer_event_type::memcpy_d2h:
        return "MemcpyD2H";
    case gpu_tracer_event_type::memcpy_d2d:
        return "MemcpyD2D";
    case gpu_tracer_event_type::memset:
        return "Memset";
    case gpu_tracer_event_type::kernel:
    case gpu_tracer_event_type::unsupported:
    default:
        return nullptr;
    }
}

}  // namespace

void gpu_trace_collector::add_event(gpu_tracer_event&& event)
{
    event.end_time_ns = std::max(event.end_time_ns, event.start_time_ns);
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back(std::move(event));
}

bool gpu_trace_collector::export_xspace(x_space* space, uint64_t /*end_gpu_ns*/,
                                        uint64_t current_generation,
                                        uint64_t* out_stale_event_count)
{
    if (space == nullptr)
    {
        return false;
    }

    std::vector<gpu_tracer_event> events;
    {
        std::lock_guard<std::mutex> lock(mu_);
        events.swap(events_);
    }
    if (events.empty())
    {
        return true;
    }

    // Filter stale events (late callbacks from a prior/aborted run with a
    // different generation). Matches design-review.md section 6.5 lifecycle
    // handling and section 6.3 integrity field group.
    uint64_t stale_count = 0;
    std::vector<gpu_tracer_event> filtered_events;
    for (auto& event : events)
    {
        if (current_generation != 0 && event.generation != current_generation)
        {
            ++stale_count;
        }
        else
        {
            filtered_events.push_back(std::move(event));
        }
    }

    if (out_stale_event_count != nullptr)
    {
        *out_stale_event_count = stale_count;
    }

    if (filtered_events.empty())
    {
        return true;
    }

    std::unordered_map<uint32_t, std::vector<gpu_tracer_event*>> by_device;
    for (auto& event : filtered_events)
    {
        by_device[event.device_id].push_back(&event);
    }

    for (const auto& device_pair : by_device)
    {
        const uint32_t ordinal     = device_pair.first;
        const auto&    device_evts = device_pair.second;
        xplane*        plane =
            find_or_add_mutable_plane_with_name(space, GpuPlaneName(static_cast<int32_t>(ordinal)));
        if (plane == nullptr)
        {
            return false;
        }

        xplane_builder builder(plane);
        builder.SetName(GpuPlaneName(static_cast<int32_t>(ordinal)));
        const x_stat_metadata& corr_meta =
            *builder.get_or_create_stat_metadata(GetStatTypeStr(StatType::kCorrelationId));
        const x_stat_metadata& stream_meta =
            *builder.get_or_create_stat_metadata(GetStatTypeStr(StatType::kStream));
        const x_stat_metadata& kernel_meta =
            *builder.get_or_create_stat_metadata(GetStatTypeStr(StatType::kKernelDetails));
        const x_stat_metadata& memcpy_meta =
            *builder.get_or_create_stat_metadata(GetStatTypeStr(StatType::kMemcpyDetails));
        const x_stat_metadata& memset_meta =
            *builder.get_or_create_stat_metadata(GetStatTypeStr(StatType::kMemsetDetails));
        // The CPU-side scope that launched this GPU work (annotation_stack::get(), set in
        // add_gpu_tracer_event()) -- no existing StatType entry for this; get_or_create_stat_metadata
        // takes an arbitrary name directly, same mechanism the StatType-backed lookups above use.
        const x_stat_metadata& annotation_meta = *builder.get_or_create_stat_metadata("annotation");

        std::unordered_map<uint32_t, uint64_t> line_origin;
        for (const gpu_tracer_event* event : device_evts)
        {
            auto inserted = line_origin.emplace(event->stream_id, event->start_time_ns);
            if (!inserted.second)
            {
                inserted.first->second = std::min(inserted.first->second, event->start_time_ns);
            }
        }

        for (gpu_tracer_event* event : device_evts)
        {
            xline_builder line = builder.get_or_create_line(event->stream_id);
            line.SetName("stream " + std::to_string(event->stream_id));
            line.SetTimestampNs(static_cast<int64_t>(line_origin[event->stream_id]));

            const char* type_name = event_type_name(event->type);
            std::string event_name =
                event->name.empty() && type_name != nullptr ? type_name : event->name;
            if (event_name.empty())
            {
                event_name = "unknown";
            }

            const uint64_t begin_ps =
                xevent_builder::NanoToPico(static_cast<int64_t>(event->start_time_ns));
            const uint64_t end_ps =
                xevent_builder::NanoToPico(static_cast<int64_t>(event->end_time_ns));
            xevent_metadata* metadata = builder.get_or_create_event_metadata(event_name);
            xevent_builder   xevent =
                line.add_event(timespan::from_end_points(begin_ps, end_ps), *metadata);
            xevent.add_stat_value(stream_meta, static_cast<int64_t>(event->stream_id));
            if (event->type == gpu_tracer_event_type::kernel)
            {
                xevent.add_stat_value(kernel_meta, event_name);
            }
            else if (event->type == gpu_tracer_event_type::memset)
            {
                xevent.add_stat_value(memset_meta, event_name);
            }
            else if (event->type != gpu_tracer_event_type::unsupported)
            {
                xevent.add_stat_value(memcpy_meta, event_name);
            }
            if (event->correlation_id != 0)
            {
                xevent.add_stat_value(corr_meta, static_cast<int64_t>(event->correlation_id));
            }
            if (!event->annotation.empty())
            {
                xevent.add_stat_value(annotation_meta, event->annotation);
            }
        }
    }

    return true;
}

gpu_activity_tracer& gpu_activity_tracer::get()
{
    static gpu_activity_tracer tracer;
    return tracer;
}

bool gpu_activity_tracer::enable(std::shared_ptr<gpu_trace_collector> collector)
{
    std::lock_guard<std::mutex> const lock(mu_);
    if (collector_ != nullptr)
    {
        return false;
    }
    collector_ = std::move(collector);
    return true;
}

void gpu_activity_tracer::disable()
{
    std::lock_guard<std::mutex> const lock(mu_);
    collector_.reset();
}

std::shared_ptr<gpu_trace_collector> gpu_activity_tracer::collector() const
{
    std::lock_guard<std::mutex> const lock(mu_);
    return collector_;
}

bool gpu_activity_tracer::is_recording() const
{
    return collector() != nullptr;
}

void add_gpu_tracer_event(gpu_tracer_event event)
{
    std::shared_ptr<gpu_trace_collector> const collector = gpu_activity_tracer::get().collector();
    if (collector == nullptr)
    {
        return;
    }
    if (event.correlation_id == 0)
    {
        const auto& ids = annotation_stack::get_scope_range_ids();
        if (!ids.empty())
        {
            event.correlation_id = static_cast<uint32_t>(ids.back());
        }
    }
    if (event.annotation.empty())
    {
        event.annotation = annotation_stack::get();
    }
    // Stamp with the current session generation to detect late callbacks
    // from a prior/aborted run (design-review.md section 6.3/6.5).
    // Note: This reads a static atomic generation counter from the last
    // active profiler_session, not the active session's instance generation.
    // This is a TLS-like state shared across all GPU callbacks; correctness
    // relies on all callbacks quiescing before the next session starts
    // (enforced by profiler_controller/profiler_session lifecycle).
    // A future multi-session design may need per-session generation context.
    // For now, we accept single-session semantics.
    collector->add_event(std::move(event));
}

bool gpu_tracer_is_recording()
{
    return gpu_activity_tracer::get().is_recording();
}

}  // namespace profiler::profiler_impl
