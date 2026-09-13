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

/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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
#include "native/exporters/xplane/xplane_utils.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/profiler_macros.h"
////#include "logger/logger.h"
#include "common/flat_hash.h"
#include "native/analysis/stats_calculator.h"
#include "native/core/timespan.h"
#include "native/exporters/xplane/tf_xplane_visitor.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_builder.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_visitor.h"

namespace profiler
{
namespace
{
bool StartsWith(std::string_view str, std::string_view prefix)
{
#if __cplusplus >= 202002L
    // C++20: use starts_with
    return str.starts_with(prefix);
#else
    // C++17: use substr for compatibility
    return str.substr(0, prefix.size()) == prefix;
#endif
}

// Returns the indices of all elements in array for which pred is true.
template <typename T, typename Pred>
std::vector<int> FindAll(const std::vector<T>& array, const Pred& pred)
{
    std::vector<int> indices;
    indices.reserve(array.size());  // Optimize for potential full match

    for (int i = 0; i < static_cast<int>(array.size()); ++i)
    {
        if (pred(&array[i]))
        {
            indices.push_back(i);
        }
    }
    return indices;
}

// Returns the index of the first element in array for which pred is true.
// Returns -1 if no such element is found.
template <typename T, typename Pred>
int Find(const std::vector<T>& array, const Pred& pred)
{
    std::vector<int> indices = FindAll(array, pred);
    if (indices.size() > 1)
    {
        // PROFILER_LOG_WARNING("Found multiple when only one was expected.");
    }
    return indices.empty() ? -1 : indices.front();
}

template <typename T>
void SwapAndDeleteSubrange(std::vector<T>* array, int first_index, const std::vector<int>& indices)
{
    auto remove_iter = indices.begin();
    int  write_index = first_index;

    // Move elements that shouldn't be removed
    for (int read_index = first_index + 1; read_index < static_cast<int>(array->size());
         ++read_index)
    {
        if (remove_iter != indices.end() && *remove_iter == read_index)
        {
            ++remove_iter;
            continue;
        }
        std::swap((*array)[read_index], (*array)[write_index++]);
    }

    // Erase the trailing elements
    array->erase(array->begin() + write_index, array->end());
}

template <typename T>
void RemoveAt(std::vector<T>* array, const std::vector<int>& indices)
{
    if (indices.empty())
    {
        return;
    }

    // Handle special case where all elements are being removed
    if (array->size() == indices.size())
    {
        array->clear();
        return;
    }

    SwapAndDeleteSubrange(array, indices.front(), indices);
}

// Removes the given element from array.
template <typename T>
void Remove(std::vector<T>* array, const T* elem)
{
    int const index = Find(*array, [elem](const T* e) { return elem == e; });
    if (index != -1)
    {
        RemoveAt(array, {index});
    }
}

// Removes all elements that satisfy the predicate.
template <typename T, typename Pred>
void RemoveIf(std::vector<T>* array, Pred&& pred)
{
    std::vector<int> const indices = FindAll(*array, pred);
    RemoveAt(array, indices);
}

// Copy XEventMetadata from source to destination. Also copies the associated
// XStats.
void CopyEventMetadata(
    const xevent_metadata& src_event_metadata,
    const xplane_visitor&  src_plane,
    xevent_metadata&       dst_event_metadata,
    xplane_builder&        dst_plane)
{
    if (dst_event_metadata.display_name().empty() && !src_event_metadata.display_name().empty())
    {
        dst_event_metadata.set_display_name(src_event_metadata.display_name().data());  //NOLINT
    }
    if (dst_event_metadata.name().empty() && !src_event_metadata.name().empty())
    {
        dst_event_metadata.set_name(src_event_metadata.name().data());  //NOLINT
    }
    if (dst_event_metadata.metadata().empty() && !src_event_metadata.metadata().empty())
    {
        dst_event_metadata.set_metadata(src_event_metadata.metadata());
    }

    if (dst_event_metadata.stats().empty() && !src_event_metadata.stats().empty())
    {
        xevent_metadata_visitor const src_event_metadata_visitor(&src_plane, &src_event_metadata);
        src_event_metadata_visitor.for_each_stat(
            [&](const x_stat_visitor& src_stat)
            {
                x_stat_metadata const& metadata =
                    *dst_plane.get_or_create_stat_metadata(src_stat.name());
                xstat& dst_stat = *dst_event_metadata.add_stats();
                dst_stat        = src_stat.raw_stat();
                if (src_stat.value_case() == xstat::value_case_type::kRefValue)
                {
                    x_stat_metadata const& value_metadata =
                        *dst_plane.get_or_create_stat_metadata(src_stat.str_or_ref_value());
                    dst_stat.set_ref_value(value_metadata.id());
                }
                dst_stat.set_metadata_id(metadata.id());
            });
    }
    // PROFILER_CHECK_DEBUG(src_event_metadata.stats_size() == dst_event_metadata.stats_size());
}

// Copies src_event from source line to the destination line in the destination
// plane as is, along with event metadata and stats.
void CopyEvent(
    const xevent_visitor& src_event,
    const xplane_visitor& src,
    const xplane&         src_plane,
    int64_t               time_offset_ps,
    xplane_builder&       dst_plane,
    xline_builder&        dst_line)
{
    xevent_metadata* dst_event_metadata = dst_plane.get_or_create_event_metadata(src_event.name());
    CopyEventMetadata(*src_event.metadata(), src, *dst_event_metadata, dst_plane);
    xevent_builder dst_event = dst_line.add_event(*dst_event_metadata);
    if (src_event.is_aggregated_event())
    {
        dst_event.SetNumOccurrences(src_event.num_occurrences());
    }
    else
    {
        dst_event.SetOffsetPs(src_event.offset_ps() + time_offset_ps);
    }
    dst_event.SetDurationPs(src_event.duration_ps());

    src_event.for_each_stat(
        [&](const x_stat_visitor& stat)
        {
            // Here we can call AddStat instead of SetOrAddStat because dst_event
            // was just added.
            dst_event.add_stat(
                *dst_plane.get_or_create_stat_metadata(stat.name()), stat.raw_stat(), src_plane);
        });
}

bool IsOpLineName(std::string_view line_name)
{
    return line_name == "kXlaOpLineName" || line_name == "kTensorFlowOpLineName";
}

timespan GetEventTimespan(const xevent_visitor& event)
{
    const std::optional<x_stat_visitor> device_offset_ps =
        event.get_stat(StatType::kDeviceOffsetPs);
    const std::optional<x_stat_visitor> device_duration_ps =
        event.get_stat(StatType::kDeviceDurationPs);
    if (device_offset_ps.has_value() && device_duration_ps.has_value())
    {
        return timespan(
            device_offset_ps->int_or_uint_value(), device_duration_ps->int_or_uint_value());
    }

    return event.get_timespan();
}

}  // namespace

const xplane* find_plane_with_name(const x_space& space, std::string_view name)
{
    int const i =
        Find(space.planes(), [name](const xplane* plane) { return plane->name() == name; });
    return (i != -1) ? &space.planes(i) : nullptr;
}

xplane* find_mutable_plane_with_name(x_space* space, std::string_view name)
{
    int const i =
        Find(space->planes(), [name](const xplane* plane) { return plane->name() == name; });
    return (i != -1) ? space->mutable_planes(i) : nullptr;
}

xplane* find_or_add_mutable_plane_with_name(x_space* space, std::string_view name)
{
    xplane* plane = find_mutable_plane_with_name(space, name);
    if (plane == nullptr)
    {
        plane = space->add_planes();
        plane->set_name(name.data());  //NOLINT
    }
    return plane;
}

//static std::vector<const xplane*> FindPlanesWithPrefix(
//    const x_space& space, std::string_view prefix)
//{
//    return find_planes(
//        space, [&](const xplane& plane) { return StartsWith(plane.name(), prefix); });
//}

std::vector<xplane*> find_mutable_planes_with_prefix(x_space* space, std::string_view prefix)
{
    return find_mutable_planes(
        space, [&](xplane& plane) { return StartsWith(plane.name(), prefix); });
}

const xline* find_line_with_id(const xplane& plane, int64_t id)
{
    int const i = Find(plane.lines(), [id](const xline* line) { return line->id() == id; });
    return (i != -1) ? &plane.lines(i) : nullptr;
}
std::vector<const xline*> find_lines_with_id(const xplane& plane, int64_t id)
{
    std::vector<int> const indices =
        FindAll(plane.lines(), [id](const xline* line) { return line->id() == id; });
    std::vector<const xline*> lines;
    lines.reserve(indices.size());
    // Use std::transform to convert indices to line pointers
    std::transform(
        indices.begin(),
        indices.end(),
        std::back_inserter(lines),
        [&plane](int index) { return &plane.lines(index); });
    return lines;
}

const xline* find_line_with_name(const xplane& plane, std::string_view name)
{
    int const i = Find(plane.lines(), [name](const xline* line) { return line->name() == name; });
    return (i != -1) ? &plane.lines(i) : nullptr;
}

xstat* find_or_add_mutable_stat(const x_stat_metadata& stat_metadata, xevent* event)
{
    for (auto& stat : *event->mutable_stats())
    {
        if (stat.metadata_id() == stat_metadata.id())
        {
            return &stat;
        }
    }
    xstat* stat = event->add_stats();
    stat->set_metadata_id(stat_metadata.id());
    return stat;
}

//static void RemovePlane(x_space* space, const xplane* plane)
//{
//    PROFILER_CHECK_DEBUG(plane != nullptr);
//    Remove(space->mutable_planes(), plane);
//}

//static void RemovePlanes(x_space* space, const std::vector<const xplane*>& planes)
//{
//    flat_hash_set<const xplane*> planes_set(planes.begin(), planes.end());
//    RemoveIf(
//        space->mutable_planes(),
//        [&planes_set](const xplane* plane) { return planes_set.find(plane) != planes_set.end(); });
//}

static void RemoveEmptyLines(xplane* plane)
{
    RemoveIf(plane->mutable_lines(), [&](const xline* line) { return line->events().empty(); });
}

void sort_xplane(xplane* plane)
{
    for (xline& line : *plane->mutable_lines())
    {
        auto& events = *line.mutable_events();
        std::sort(events.begin(), events.end(), xevents_comparator());
    }
}

// Normalize the line's timestamp in this XPlane.
// NOTE: This can be called multiple times on the same plane. Only the first
// call will do the normalization, subsequent calls will do nothing.
// The assumption is that both line's timestamp_ns and start_time_ns are
// nano-seconds from epoch time, the different of these values is much
// smaller than these value.
void NormalizeTimestamps(xplane* plane, uint64_t start_time_ns)
{
    for (xline& line : *plane->mutable_lines())
    {
        if (line.timestamp_ns() >= static_cast<int64_t>(start_time_ns))
        {
            line.set_timestamp_ns(line.timestamp_ns() - start_time_ns);
        }
    }
}

void NormalizeTimestamps(x_space* space, uint64_t start_time_ns)
{
    for (xplane& plane : *space->mutable_planes())
    {
        NormalizeTimestamps(&plane, start_time_ns);
    }
}
void MergePlanes(const xplane& src_plane, xplane* dst_plane)
{
    RemoveEmptyLines(dst_plane);
    xplane_visitor src(&src_plane);
    xplane_builder dst(dst_plane);
    src.for_each_stat(
        [&](const x_stat_visitor& stat)
        {
            x_stat_metadata const* stat_metadata = dst.get_or_create_stat_metadata(stat.name());
            // Use SetOrAddStat to avoid duplicating stats in dst_plane.
            dst.set_or_add_stat(*stat_metadata, stat.raw_stat(), src_plane);
        });
    src.for_each_line(
        [&](const xline_visitor& line)
        {
            xline_builder dst_line       = dst.get_or_create_line(line.id());
            int64_t       time_offset_ps = 0LL;
            if (dst_line.NumEvents() == 0)
            {
                // Since we RemoveEmptyLines above, this could only mean that current
                // line only exist in src plane.
                dst_line.SetTimestampNs(line.timestamp_ns());
                dst_line.SetName(line.name());
                dst_line.SetDisplayNameIfEmpty(line.display_name());
            }
            else
            {
                if (line.timestamp_ns() <= dst_line.TimestampNs())
                {
                    dst_line.set_time_stamp_ns_and_adjust_event_offsets(line.timestamp_ns());
                }
                else
                {
                    time_offset_ps =
                        xevent_builder::NanoToPico(line.timestamp_ns() - dst_line.TimestampNs());
                }
                dst_line.SetNameIfEmpty(line.name());
                // Don't override dst_line's display name because if both lines have name,
                // but no display name, line's name will became display name of dst_line.
            }

            line.for_each_event(
                [&](const xevent_visitor& event)
                { CopyEvent(event, src, src_plane, time_offset_ps, dst, dst_line); });
        });
}

void MergePlanes(const std::vector<const xplane*>& src_planes, xplane* dst_plane)
{
    for (const xplane* src_plane : src_planes)
    {
        MergePlanes(*src_plane, dst_plane);
    }
}

//static void RemoveLine(xplane* plane, const xline* line)
//{
//    PROFILER_CHECK_DEBUG(line != nullptr);
//    Remove(plane->mutable_lines(), line);
//}

//static void RemoveEvents(xline* line, const flat_hash_set<const xevent*>& events)
//{
//    RemoveIf(
//        line->mutable_events(),
//        [&](const xevent* event) { return events.find(event) != events.end(); });
//}

//static void RemoveEmptyPlanes(x_space* space)
//{
//    RemoveIf(space->mutable_planes(), [&](const xplane* plane) { return plane->lines().empty(); });
//}

bool xevents_comparator::operator()(const xevent& a, const xevent& b) const
{
    return xevent_timespan(a) < xevent_timespan(b);
}

void sort_x_space(x_space* space)
{
    for (xplane& plane : *space->mutable_planes())
    {
        sort_xplane(&plane);
    }
}

int64_t GetStartTimestampNs(const xplane& plane)
{
    if (plane.lines().empty())
    {
        return 0LL;
    }
    // Use std::accumulate to find minimum timestamp
    int64_t const plane_timestamp = std::accumulate(
        plane.lines().begin(),
        plane.lines().end(),
        std::numeric_limits<int64_t>::max(),
        [](int64_t min_ts, const xline& line) { return std::min(min_ts, line.timestamp_ns()); });
    return plane_timestamp;
}

bool IsEmpty(const x_space& space)
{
    // Use std::all_of to check if all planes have empty events
    return std::all_of(
        space.planes().begin(),
        space.planes().end(),
        [](const xplane& plane)
        {
            return std::all_of(
                plane.lines().begin(),
                plane.lines().end(),
                [](const xline& line) { return line.events().empty(); });
        });
}

bool IsXSpaceGrouped(const x_space& space)
{
    for (const auto& plane : space.planes())
    {
        // If any plane has been grouped, consider space as grouped.
        // CreateTfXPlaneVisitor is necessary because we need check "group_id" stat
        // by its type StatType::kGroupId.
        xplane_visitor const   xplane        = CreateTfXPlaneVisitor(&plane);
        const x_stat_metadata* group_id_stat = xplane.get_stat_metadata_by_type(StatType::kGroupId);
        if (group_id_stat != nullptr)
        {
            return true;
        }
    }
    return false;
}

void AddFlowsToXplane(int32_t host_id, bool is_host_plane, bool connect_traceme, xplane* xplane)
{
    if (xplane == nullptr)
    {
        return;
    }
    xplane_builder   plane(xplane);
    x_stat_metadata* correlation_id_stats_metadata =
        plane.stat_metadata(GetStatTypeStr(StatType::kCorrelationId));
    x_stat_metadata* producer_type_stats_metadata =
        plane.stat_metadata(GetStatTypeStr(StatType::kProducerType));
    x_stat_metadata* consumer_type_stats_metadata =
        plane.stat_metadata(GetStatTypeStr(StatType::kConsumerType));
    x_stat_metadata* producer_id_stats_metadata =
        plane.stat_metadata(GetStatTypeStr(StatType::kProducerId));
    x_stat_metadata* consumer_id_stats_metadata =
        plane.stat_metadata(GetStatTypeStr(StatType::kConsumerId));
    x_stat_metadata* flow_stats_metadata =
        plane.get_or_create_stat_metadata(GetStatTypeStr(StatType::kFlow));
    XFlow::FlowDirection direction =
        is_host_plane ? XFlow::FlowDirection::kFlowOut : XFlow::FlowDirection::kFlowIn;

    plane.ForEachLine(
        [&](xline_builder line)
        {
            line.ForEachEvent(
                [&](xevent_builder event)
                {
                    std::optional<uint64_t> correlation_id;
                    std::optional<uint64_t> producer_type;
                    std::optional<uint64_t> consumer_type;
                    std::optional<uint64_t> producer_id;
                    std::optional<uint64_t> consumer_id;
                    event.ForEachStat(
                        [&](xstat* stat)
                        {
                            if ((correlation_id_stats_metadata != nullptr) &&
                                stat->metadata_id() == correlation_id_stats_metadata->id())
                            {
                                correlation_id = stat->uint64_value();
                            }
                            else if (connect_traceme)
                            {
                                if ((producer_type_stats_metadata != nullptr) &&
                                    stat->metadata_id() == producer_type_stats_metadata->id())
                                {
                                    producer_type =
                                        xstats_builder<profiler::xplane>::IntOrUintValue(*stat);
                                }
                                else if (
                                    (consumer_type_stats_metadata != nullptr) &&
                                    stat->metadata_id() == consumer_type_stats_metadata->id())
                                {
                                    consumer_type =
                                        xstats_builder<profiler::xplane>::IntOrUintValue(*stat);
                                }
                                else if (
                                    (producer_id_stats_metadata != nullptr) &&
                                    stat->metadata_id() == producer_id_stats_metadata->id())
                                {
                                    producer_id =
                                        xstats_builder<profiler::xplane>::IntOrUintValue(*stat);
                                }
                                else if (
                                    (consumer_id_stats_metadata != nullptr) &&
                                    stat->metadata_id() == consumer_id_stats_metadata->id())
                                {
                                    consumer_id =
                                        xstats_builder<profiler::xplane>::IntOrUintValue(*stat);
                                }
                            }
                        });
                    if (correlation_id)
                    {
                        XFlow const flow(
                            XFlow::GetFlowId(host_id, *correlation_id),
                            direction,
                            ContextType::kGpuLaunch);
                        event.add_stat_value(*flow_stats_metadata, flow.ToStatValue());
                    }
                    if (connect_traceme)
                    {
                        if (producer_type && producer_id)
                        {
                            auto        context_type = GetSafeContextType(*producer_type);
                            XFlow const flow(
                                XFlow::GetFlowId(host_id, *producer_id, context_type),
                                XFlow::FlowDirection::kFlowOut,
                                context_type);
                            event.add_stat_value(*flow_stats_metadata, flow.ToStatValue());
                        }
                        if (consumer_type && consumer_id)
                        {
                            auto        context_type = GetSafeContextType(*consumer_type);
                            XFlow const flow(
                                XFlow::GetFlowId(host_id, *consumer_id, context_type),
                                XFlow::FlowDirection::kFlowIn,
                                context_type);
                            event.add_stat_value(*flow_stats_metadata, flow.ToStatValue());
                        }
                    }
                });
        });
}

uint64_t GetDevicePlaneFingerprint(const xplane& plane)
{
    const xline* xla_module_line = find_line_with_name(plane, kXlaModuleLineName);
    if (xla_module_line == nullptr)
    {
        return 0ULL;
    }

    xplane_visitor const xplane(&plane);
    xline_visitor const  xline(&xplane, xla_module_line);
    std::set<uint64_t>   ordered_module_fps;
    xline.for_each_event([&](PROFILER_UNUSED const xevent_visitor& xevent)
                         { ordered_module_fps.insert(/*Fingerprint64(xevent.Name())*/ 0); });
    if (ordered_module_fps.empty())
    {
        return 0ULL;
    }
    uint64_t output = 0ULL;
    for (PROFILER_UNUSED const auto& fp : ordered_module_fps)
    {
        output = 0;
        //FingerprintCat64(output, fp);
    }
    return output;
}

std::optional<xevent_visitor> XEventContextTracker::GetContainingEvent(const timespan& event)
{
    if (line_ == nullptr)
    {
        return std::nullopt;
    }
    if (current_index_ != -1)
    {
        xevent_visitor current_event(plane_, line_, &line_->events(current_index_));
        if (current_event.get_timespan().includes(event))
        {
            return current_event;
        }
    }
    for (int i = current_index_ + 1; i < static_cast<int>(line_->events_size()); ++i)
    {
        xevent_visitor current_event(plane_, line_, &line_->events(i));
        if (static_cast<uint64_t>(current_event.timestamp_ps()) > event.end_ps())
        {
            break;
        }
        if (static_cast<uint64_t>(current_event.end_timestamp_ps()) < event.begin_ps())
        {
            continue;
        }
        current_index_ = i;
        if (current_event.get_timespan().includes(event))
        {
            return current_event;
        }
        break;  // overlapping
    }
    return std::nullopt;
}

std::optional<xevent_visitor> XEventContextTracker::GetOverlappingEvent(const timespan& event)
{
    if (line_ == nullptr)
    {
        return std::nullopt;
    }
    if (current_index_ != -1)
    {
        xevent_visitor current_event(plane_, line_, &line_->events(current_index_));
        if (current_event.get_timespan().overlaps(event))
        {
            return current_event;
        }
    }
    for (int i = current_index_ + 1; i < static_cast<int>(line_->events_size()); ++i)
    {
        xevent_visitor current_event(plane_, line_, &line_->events(i));
        if (static_cast<uint64_t>(current_event.timestamp_ps()) > event.end_ps())
        {
            break;
        }
        if (static_cast<uint64_t>(current_event.end_timestamp_ps()) < event.begin_ps())
        {
            continue;
        }
        current_index_ = i;
        if (current_event.get_timespan().overlaps(event))
        {
            return current_event;
        }
        break;  // overlapping
    }
    return std::nullopt;
}

void AggregateXPlane(const xplane& full_trace, xplane& aggregated_trace)
{
    struct EventStat
    {
        profiler::stat<int64_t> stat;
        int64_t                 children_duration;
    };
    using StatByEvent = flat_hash_map<int64_t /*event_id*/, EventStat>;
    using StatByGroup = flat_hash_map<int64_t /*group_id*/, StatByEvent>;

    flat_hash_map<int64_t /*line_id*/, StatByGroup> stats;

    const xplane_visitor& plane = CreateTfXPlaneVisitor(&full_trace);
    xplane_builder        aggregated_plane(&aggregated_trace);
    aggregated_plane.SetName(plane.name());

    uint64_t first_op_start_ps = std::numeric_limits<uint64_t>::max();
    uint64_t last_op_end_ps    = 0;

    plane.for_each_line(
        [&](const xline_visitor& line)
        {
            if (line.name() == kStepLineName || line.name() == kSparseCoreStepLineName)
            {
                xline_builder aggregated_line = aggregated_plane.get_or_create_line(line.id());
                aggregated_line.SetName(kStepLineName);
                line.for_each_event(
                    [&](const xevent_visitor& event)
                    {
                        CopyEvent(event, plane, full_trace, 0LL, aggregated_plane, aggregated_line);
                    });
            }
            if (!IsOpLineName(line.name()))
            {
                return;
            }
            xline_builder aggregated_line = aggregated_plane.get_or_create_line(line.id());
            aggregated_line.SetName(line.name());
            std::vector<xevent_visitor> event_stack;
            line.for_each_event(
                [&](xevent_visitor event)
                {
                    timespan const timespan = GetEventTimespan(event);
                    first_op_start_ps =
                        first_op_start_ps <= static_cast<uint64_t>(event.timestamp_ps())
                            ? first_op_start_ps
                            : timespan.begin_ps();
                    last_op_end_ps =
                        last_op_end_ps >= static_cast<uint64_t>(event.end_timestamp_ps())
                            ? last_op_end_ps
                            : timespan.end_ps();
                    const auto&   group_stat = event.get_stat(StatType::kGroupId);
                    int64_t const group_id   = group_stat.has_value()
                                                   ? group_stat->int_or_uint_value()
                                                   : std::numeric_limits<uint64_t>::max();

                    StatByEvent& line_stats = stats[line.id()][group_id];
                    line_stats[event.id()].stat.update_stat(timespan.duration_ps());
                    // PROFILER_CHECK_DEBUG(                                       //NOLINT
                    // event_stack.empty() || !(event < event_stack.back()));  //NOLINT
                    while (!event_stack.empty() &&
                           !GetEventTimespan(event_stack.back()).includes(timespan))
                    {
                        event_stack.pop_back();
                    }
                    if (!event_stack.empty())
                    {
                        line_stats[event_stack.back().id()].children_duration +=
                            timespan.duration_ps();
                    }
                    event_stack.push_back(std::move(event));
                });
        });

    uint64_t const total_time_ps = ((last_op_end_ps != 0u) && last_op_end_ps > first_op_start_ps)
                                       ? last_op_end_ps - first_op_start_ps
                                       : 0;

    aggregated_plane.add_stat_value(
        *aggregated_plane.get_or_create_stat_metadata(
            GetStatTypeStr(StatType::kTotalProfileDurationPs)),
        total_time_ps);

    x_stat_metadata const* kMinDurationPs =
        aggregated_plane.get_or_create_stat_metadata(GetStatTypeStr(StatType::kMinDurationPs));
    x_stat_metadata const* kSelfDurationPs =
        aggregated_plane.get_or_create_stat_metadata(GetStatTypeStr(StatType::kSelfDurationPs));
    x_stat_metadata const* kGroupId =
        aggregated_plane.get_or_create_stat_metadata(GetStatTypeStr(StatType::kGroupId));

    for (const auto& [line_id, stats_by_group] : stats)
    {
        xline_builder aggregated_line = aggregated_plane.get_or_create_line(line_id);
        for (const auto& [group_id, stat_by_event] : stats_by_group)
        {
            for (const auto& [event_id, event_stat] : stat_by_event)
            {
                const auto&      src_event_metadata = *plane.get_event_metadata(event_id);
                xevent_metadata& event_metadata =
                    *aggregated_plane.get_or_create_event_metadata(src_event_metadata.name());
                CopyEventMetadata(src_event_metadata, plane, event_metadata, aggregated_plane);
                xevent_builder aggregated_event = aggregated_line.add_event(event_metadata);
                aggregated_event.SetNumOccurrences(event_stat.stat.count());
                aggregated_event.SetDurationPs(event_stat.stat.sum());
                if (static_cast<uint64_t>(group_id) != std::numeric_limits<uint64_t>::max())
                {
                    aggregated_event.add_stat_value(*kGroupId, group_id);
                }
                if (event_stat.stat.count() > 1)
                {
                    aggregated_event.add_stat_value(*kMinDurationPs, event_stat.stat.min());
                }
                if (event_stat.children_duration != 0)
                {
                    aggregated_event.add_stat_value(
                        *kSelfDurationPs, event_stat.stat.sum() - event_stat.children_duration);
                }
            }
        }
    }
}

bool IsCustomPlane(const xplane& plane)
{
    // NOTE: remove me after all legacy traces are gone (i.e. 2022/08/04).
    constexpr std::string_view kLegacyCustomPlanePrefix = "/custom:";
    return StartsWith(plane.name(), kCustomPlanePrefix) ||
           StartsWith(plane.name(), kLegacyCustomPlanePrefix);
}

bool IsHostPlane(const xplane& plane)
{
    return plane.name() == kHostThreadsPlaneName || plane.name() == kHostCpusPlaneName ||
           plane.name() == kTFStreamzPlaneName || plane.name() == kMetadataPlaneName ||
           plane.name() == kSyscallsPlaneName || plane.name() == kPythonTracerPlaneName ||
           plane.name() == kCuptiDriverApiPlaneName;
}

bool IsDevicePlane(const xplane& plane)
{
    // device_option and host planes should be mutually exclusive.
    if (IsHostPlane(plane))
    {
        return false;
    }
    return StartsWith(plane.name(), "/device") ||
           StartsWith(plane.name(), kTpuNonCorePlaneNamePrefix) || IsCustomPlane(plane);
}

}  // namespace profiler