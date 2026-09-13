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

#include "native/session/scope_tree_builder.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "native/exporters/xplane/tf_xplane_visitor.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_visitor.h"
#include "native/session/profiler.h"

namespace profiler::scope_tree_builder
{
namespace
{

struct flat_event
{
    int64_t     start_ns;
    int64_t     end_ns;
    std::string name;
};

std::chrono::high_resolution_clock::time_point to_time_point(int64_t nanos)
{
    // Reconstructed nodes don't have a real high_resolution_clock reading (the collection
    // already finished when this runs) -- only relative differences (get_duration_*()) are
    // meaningful here, so any consistent zero point works.
    return std::chrono::high_resolution_clock::time_point(std::chrono::nanoseconds(nanos));
}

std::string event_display_name(const xevent_visitor& event)
{
    if (event.has_display_name())
    {
        return std::string(event.display_name());
    }
    if (!event.name().empty())
    {
        return std::string(event.name());
    }
    return "TraceEvent";
}

std::string line_thread_label(const xline_visitor& line)
{
    // Same precedence as xline_thread_label(xline): display, then name, then id.
    if (!line.display_name().empty())
    {
        return std::string(line.display_name());
    }
    return "thread " + std::to_string(line.id());
}

std::vector<flat_event> collect_line_events(const xline_visitor& line)
{
    std::vector<flat_event> events;
    events.reserve(line.num_events());

    line.for_each_event(
        [&](const xevent_visitor& event)
        {
            if (event.is_aggregated_event())
            {
                return;  // counter event, not a duration to nest
            }
            // Integer ns math via visitor helpers (avoid forking offset_ps/1000 locally).
            int64_t const start_ns =
                event.line_timestamp_ns() + xevent_visitor::pico_to_nano(event.offset_ps());
            int64_t const duration_ns =
                std::max<int64_t>(0, xevent_visitor::pico_to_nano(event.duration_ps()));
            events.push_back({start_ns, start_ns + duration_ns, event_display_name(event)});
        });

    std::stable_sort(
        events.begin(),
        events.end(),
        [](const flat_event& a, const flat_event& b) { return a.start_ns < b.start_ns; });
    return events;
}

// Nests one thread's flat, time-sorted events under `root` by interval containment: each event
// becomes a child of the innermost still-open event whose [start, end) range contains it.
void nest_line_events(
    const std::vector<flat_event>& events,
    const std::string&             thread_label,
    profiler::profiler_scope_data& root)
{
    std::vector<profiler::profiler_scope_data*> open_ancestors;  // innermost open node last

    for (const flat_event& event : events)
    {
        auto const start = to_time_point(event.start_ns);
        while (!open_ancestors.empty() && open_ancestors.back()->end_time_ <= start)
        {
            open_ancestors.pop_back();
        }
        profiler::profiler_scope_data* parent =
            open_ancestors.empty() ? &root : open_ancestors.back();

        auto node           = std::make_unique<profiler::profiler_scope_data>();
        node->name_         = event.name;
        node->start_time_   = start;
        node->end_time_     = to_time_point(event.end_ns);
        node->depth_level_  = parent->depth_level_ + 1;
        node->parent_       = parent;
        node->thread_label_ = thread_label;

        profiler::profiler_scope_data* node_ptr = node.get();
        parent->children_.push_back(std::move(node));
        open_ancestors.push_back(node_ptr);
    }
}

}  // namespace

std::unique_ptr<profiler::profiler_scope_data> build_scope_tree(const profiler::x_space& space)
{
    auto root   = std::make_unique<profiler::profiler_scope_data>();
    root->name_ = "ROOT";

    bool found_events = false;
    for (const xplane& plane : space.planes())
    {
        if (plane.name() != kHostThreadsPlaneName)
        {
            continue;
        }
        // Typed read path — same visitor TensorFlow analysis uses (schema getters attached).
        xplane_visitor const plane_visitor = CreateTfXPlaneVisitor(&plane);
        plane_visitor.for_each_line(
            [&](const xline_visitor& line)
            {
                std::vector<flat_event> const events = collect_line_events(line);
                if (events.empty())
                {
                    return;
                }
                found_events = true;
                nest_line_events(events, line_thread_label(line), *root);
            });
    }

    return found_events ? std::move(root) : nullptr;
}

}  // namespace profiler::scope_tree_builder
