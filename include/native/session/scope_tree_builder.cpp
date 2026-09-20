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

profiler::steady_clock_t::time_point to_time_point(int64_t nanos)
{
    // Reconstructed nodes don't have a real clock reading (the collection
    // already finished when this runs) -- only relative differences
    // (get_duration_*()) are meaningful here, so any consistent zero point
    // works. Must match profiler_scope_data::start_time_/end_time_'s clock
    // type (profiler::steady_clock_t, design-review.md finding 10) exactly --
    // chrono time_points of different clocks don't implicitly convert.
    return profiler::steady_clock_t::time_point(std::chrono::nanoseconds(nanos));
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
        auto const end   = to_time_point(event.end_ns);
        while (!open_ancestors.empty() && open_ancestors.back()->end_time_ <= start)
        {
            open_ancestors.pop_back();
        }
        // The innermost still-open ancestor may have started before `event` but end
        // before it does too (an overlapping, non-nesting pair, e.g. A=[1000,3000),
        // B=[2000,4000)) -- only accept it as this event's parent if its interval
        // fully contains [start, end); otherwise this event isn't actually nested
        // under anything currently open, so it attaches under root instead of a
        // false parent.
        profiler::profiler_scope_data* parent =
            (!open_ancestors.empty() && open_ancestors.back()->end_time_ >= end)
                ? open_ancestors.back()
                : &root;

        auto node           = std::make_unique<profiler::profiler_scope_data>();
        node->name_         = event.name;
        node->start_time_   = start;
        node->end_time_     = end;
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
