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
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "common/flat_hash.h"
#include "common/profiler_export.h"
#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_visitor.h"

namespace profiler
{

// Returns a timespan from an xevent.
// WARNING: This should only be used when comparing events from the same xline.
inline timespan xevent_timespan(const xevent& event)
{
    return timespan(event.offset_ps(), event.duration_ps());
}

// Returns the planes with the given predicate.
template <typename F>
std::vector<const xplane*> find_planes(const x_space& space, const F& predicate)
{
    std::vector<const xplane*> result;
    for (const xplane& plane : space.planes())
    {
        if (predicate(plane))
        {
            result.push_back(&plane);
        }
    }
    return result;
}

// Returns mutable planes with the given predicate.
template <typename F>
std::vector<xplane*> find_mutable_planes(x_space* space, const F& predicate)
{
    std::vector<xplane*> result;
    for (xplane& plane : *space->mutable_planes())
    {
        if (predicate(plane))
        {
            result.push_back(&plane);
        }
    }
    return result;
}

// Returns the plane with the given name or nullptr if not found.
PROFILER_API const xplane* find_plane_with_name(const x_space& space, std::string_view name);
PROFILER_API xplane*       find_mutable_plane_with_name(x_space* space, std::string_view name);

// Returns the planes with the given names, if found.
std::vector<const xplane*> find_planes_with_names(
    const x_space& space, const std::vector<std::string_view>& names);

// Returns the plane with the given name in the container. If necessary, adds a
// new plane to the container.
PROFILER_API xplane* find_or_add_mutable_plane_with_name(x_space* space, std::string_view name);

// Returns all the planes with a given prefix.
PROFILER_API std::vector<const xplane*> find_planes_with_prefix(
    const x_space& space, std::string_view prefix);
PROFILER_API std::vector<xplane*> find_mutable_planes_with_prefix(
    x_space* space, std::string_view prefix);

// Returns the plane with the given id/name or nullptr if not found.
PROFILER_API const xline* find_line_with_id(const xplane& plane, int64_t id);
PROFILER_API std::vector<const xline*> find_lines_with_id(const xplane& plane, int64_t id);
PROFILER_API const xline* find_line_with_name(const xplane& plane, std::string_view name);

xstat* find_or_add_mutable_stat(const x_stat_metadata& stat_metadata, xevent* event);

void remove_plane(x_space* space, const xplane* plane);
void remove_planes(x_space* space, const std::vector<const xplane*>& planes);
void remove_line(xplane* plane, const xline* line);
void remove_events(xline* line, const flat_hash_set<const xevent*>& events);

void remove_empty_planes(x_space* space);
void remove_empty_lines(xplane* plane);

// Sort lines in plane with a provided comparator.
template <class Compare>
void sort_xlines_by(xplane* plane, Compare comp)
{
    std::sort(plane->mutable_lines()->begin(), plane->mutable_lines()->end(), comp);
}

class xlines_comparator_by_name
{
public:
    bool operator()(const xline& a, const xline& b) const
    {
        const auto& line_a = a.display_name().empty() ? a.name() : a.display_name();
        const auto& line_b = b.display_name().empty() ? b.name() : b.display_name();
        return line_a < line_b;
    }
};

// Sorts each xline's xevents by offset_ps (ascending) and duration_ps
// (descending) so nested events are sorted from outer to innermost.
void sort_xplane(xplane* plane);
// Sorts each plane of the x_space.
PROFILER_API void sort_x_space(x_space* space);

// Functor that compares xevents for sorting by timespan.
struct xevents_comparator
{
    PROFILER_API bool operator()(const xevent& a, const xevent& b) const;
};

// Returns a sorted vector of all XEvents in the given XPlane.
// This template can be used with either XPlaneVisitor or XPlaneBuilder.
template <typename Event, typename Plane>
inline std::vector<Event> GetSortedEvents(Plane& plane, bool include_derived_events = false)
{
    std::vector<Event> events;
    plane.ForEachLine(
        [&events, include_derived_events](auto line)
        {
            if (!include_derived_events && IsDerivedThreadId(line.Id()))
                return;
            line.ForEachEvent([&events](auto event) { events.emplace_back(std::move(event)); });
        });

    std::sort(events.begin(), events.end());

    return events;
}

// Normalize timestamps by time-shifting to start_time_ns_ as origin.
void NormalizeTimestamps(xplane* plane, uint64_t start_time_ns);
void NormalizeTimestamps(x_space* space, uint64_t start_time_ns);

// Merges src_plane into dst_plane. Both plane level stats, lines, events and
// event level stats are merged. If src_plane and dst_plane both have the same
// line, which have different start timestamps, we will normalize the events
// offset timestamp correspondingly.
PROFILER_API void MergePlanes(const xplane& src_plane, xplane* dst_plane);

// Merges each plane with a src_planes, into the dst_plane.
PROFILER_API void MergePlanes(const std::vector<const xplane*>& src_planes, xplane* dst_plane);

// Plane's start timestamp is defined as the minimum of all lines' start
// timestamps. If zero line exists, return 0;
PROFILER_API int64_t GetStartTimestampNs(const xplane& plane);

// Returns true if there are no XEvents.
PROFILER_API bool IsEmpty(const x_space& space);

// Return true if grouping/step-tracking is done on the Xspace already.
PROFILER_API bool IsXSpaceGrouped(const x_space& space);

// Mutate the XPlane by adding predefined XFlow. e.g. GPU kernel launches =>
// GPU kernel events.
void AddFlowsToXplane(int32_t host_id, bool is_host_plane, bool connect_traceme, xplane* plane);

// Get a fingerprint of device plane for deduplicating derived lines in similar
// device planes. The fingerprint is a hash of sorted HLO modules name which
// were appeared on current plane.
// Returns 0 when such "Xla Modules" line don't exist.
uint64_t GetDevicePlaneFingerprint(const xplane& plane);
template <typename XPlanePointerIterator>
void SortPlanesById(XPlanePointerIterator begin, XPlanePointerIterator end)
{
    std::sort(
        begin,
        end,
        [&](const xplane* a, const xplane* b)
        {
            return a->id() < b->id();  // ascending order of device xplane id.
        });
}

// When certain event context only exists from event from other line, which
// "encloses" current event in timeline, we need to find out quickly which
// enclosing event is (or if there is one).
// To Avoid O(N) search overhead, assume the event are processed in the order
// of "XLine default sorting order".
class XEventContextTracker
{
public:
    // The events on line need to be sorted and disjointed.
    XEventContextTracker(const xplane_visitor* plane, const xline* line)
        : plane_(plane), line_(line)
    {
    }

    // Returns the event that encloses/contains the specified input event.
    // Expects called with events with start timestamps sorted incrementingly.
    std::optional<xevent_visitor> GetContainingEvent(const timespan& event);

    // Returns the event that overlaps the specified input event.
    // Expects called with events with start timestamps sorted incrementingly.
    std::optional<xevent_visitor> GetOverlappingEvent(const timespan& event);

private:
    const xplane_visitor* plane_;
    const xline*          line_;
    int64_t               current_index_ = -1;
};

// Aggregate traces on op_line in the full_trace xplane and add them onto the
// aggregated_trace xplane. The function also copies the step line from the
// full_trace into the aggregated_trace.
void AggregateXPlane(const xplane& full_trace, xplane& aggregated_trace);

// Return whether this is a custom plan.
PROFILER_API bool IsCustomPlane(const xplane& plane);

// Return whether this is a host plan.
PROFILER_API bool IsHostPlane(const xplane& plane);

// Return whether this is a device plan.
PROFILER_API bool IsDevicePlane(const xplane& plane);

}  // namespace profiler
