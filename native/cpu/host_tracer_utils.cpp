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

#include "host_tracer_utils.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "native/exporters/xplane/xplane.h"
#include "native/exporters/xplane/xplane_builder.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/tracing/traceme_recorder.h"
#include "native/utils/parse_annotation.h"

namespace profiler
{
namespace profiler_impl
{
namespace
{

// Helper function to add display name to event metadata if needed
// This can be extended to handle Profiler-specific operation naming conventions
void may_add_display_name(xevent_metadata* xevent_metadata)
{
    if (!xevent_metadata->display_name().empty())
    {
        return;
    }

    // For now, we don't have Profiler-specific op name processing
    // This is where you would add logic similar to TfOpEventName()
    // to extract and format operation names for better display

    // Example: Extract operation type from names like "MatMul:forward"
    std::string_view name = xevent_metadata->name();
    while (!name.empty() && (std::isspace(static_cast<unsigned char>(name.back())) != 0))
    {
        name.remove_suffix(1);
    }

    size_t const colon_pos = name.find(':');
    if (colon_pos != std::string_view::npos)
    {
        std::string_view const op_type = name.substr(colon_pos + 1);
        if (!op_type.empty())
        {
            xevent_metadata->set_display_name(std::string(op_type));
            return;
        }
    }

    constexpr std::string_view kIteratorPrefix = "Iterator::";
#if __cplusplus >= 202002L
    // C++20 and later: use starts_with
    if (name.starts_with(kIteratorPrefix))
#else
    // C++17: use rfind for compatibility
    if (name.rfind(kIteratorPrefix, 0) == 0)
#endif
    {
        size_t const separator = name.rfind("::");
        if (separator != std::string_view::npos && separator + 2 < name.size())
        {
            xevent_metadata->set_display_name(std::string(name.substr(separator + 2)));
        }
    }
}

// Helper function to parse and add stat value from string
void parse_and_add_stat_value(
    xevent_builder& xevent, const x_stat_metadata& stat_metadata, std::string_view value_str)
{
    // Try to parse as integer
    try
    {
        size_t        pos;
        int64_t const int_value = std::stoll(std::string(value_str), &pos);
        if (pos == value_str.size())
        {
            xevent.add_stat_value(stat_metadata, int_value);
            return;
        }
    }
    catch (...)  //NOLINT
    {
        //throw;  // Not an integer, continue
    }

    // Try to parse as double
    try
    {
        size_t       pos;
        double const double_value = std::stod(std::string(value_str), &pos);
        if (pos == value_str.size())
        {
            xevent.add_stat_value(stat_metadata, double_value);
            return;
        }
    }
    catch (...)  //NOLINT
    {
        //throw;  // Not a double, continue
    }

    // Treat as string
    xevent.add_stat_value(stat_metadata, std::string(value_str));
}

}  // namespace

void convert_complete_events_to_xplane(
    uint64_t start_timestamp_ns, traceme_recorder::Events&& events, xplane* raw_plane)
{
    xplane_builder xplane(raw_plane);

    for (auto& thread : events)
    {
        xline_builder xline = xplane.get_or_create_line(thread.thread.tid);
        xline.SetName(thread.thread.name);
        xline.SetTimestampNs(start_timestamp_ns);
        xline.ReserveEvents(thread.events.size());

        while (!thread.events.empty())
        {
            auto event = std::move(thread.events.front());
            thread.events.pop_front();

            // Skip incomplete events
            if (!event.is_complete())
            {
                continue;
            }

            // Skip events that started before profiling began
            if (event.start_time < static_cast<int64_t>(start_timestamp_ns))
            {
                continue;
            }

            // Check if event name contains metadata
            if (!has_metadata(event.name))
            {
                // Simple event without metadata
                xevent_metadata* xevent_metadata =
                    xplane.get_or_create_event_metadata(std::move(event.name));
                may_add_display_name(xevent_metadata);

                xevent_builder xevent = xline.add_event(*xevent_metadata);
                xevent.SetTimestampNs(event.start_time);
                xevent.SetEndTimestampNs(event.end_time);
                continue;
            }

            // Parse annotated event
            annotation const annot = parse_annotation(event.name);

            xevent_metadata* xevent_metadata =
                xplane.get_or_create_event_metadata(std::string(annot.name));
            may_add_display_name(xevent_metadata);

            xevent_builder xevent = xline.add_event(*xevent_metadata);
            xevent.SetTimestampNs(event.start_time);
            xevent.SetEndTimestampNs(event.end_time);

            // Reserve space for metadata stats
            xevent.reserve_stats(annot.metadata.size());

            // Add metadata as stats
            for (const auto& metadata : annot.metadata)
            {
                x_stat_metadata const* xstat_metadata =
                    xplane.get_or_create_stat_metadata(std::string(metadata.key));
                parse_and_add_stat_value(xevent, *xstat_metadata, metadata.value);
            }
        }
    }

    // Sort lines by name for consistent output
    sort_xlines_by(raw_plane, xlines_comparator_by_name());
}

}  // namespace profiler_impl
}  // namespace profiler
