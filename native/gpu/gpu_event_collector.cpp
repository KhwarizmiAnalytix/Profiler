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

bool gpu_trace_collector::export_xspace(x_space* space, uint64_t /*end_gpu_ns*/)
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

    std::unordered_map<uint32_t, std::vector<gpu_tracer_event*>> by_device;
    for (auto& event : events)
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
        }
    }

    return true;
}

gpu_activity_tracer& gpu_activity_tracer::get()
{
    static gpu_activity_tracer tracer;
    return tracer;
}

bool gpu_activity_tracer::enable(gpu_trace_collector* collector)
{
    gpu_trace_collector* expected = nullptr;
    return collector_.compare_exchange_strong(
        expected, collector, std::memory_order_acq_rel, std::memory_order_acquire);
}

void gpu_activity_tracer::disable()
{
    collector_.store(nullptr, std::memory_order_release);
}

gpu_trace_collector* gpu_activity_tracer::collector() const
{
    return collector_.load(std::memory_order_acquire);
}

bool gpu_activity_tracer::is_recording() const
{
    return collector() != nullptr;
}

void add_gpu_tracer_event(gpu_tracer_event event)
{
    gpu_trace_collector* collector = gpu_activity_tracer::get().collector();
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
    collector->add_event(std::move(event));
}

bool gpu_tracer_is_recording()
{
    return gpu_activity_tracer::get().is_recording();
}

}  // namespace profiler::profiler_impl
