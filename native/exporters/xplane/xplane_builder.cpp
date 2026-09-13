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
#include "native/exporters/xplane/xplane_builder.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/flat_hash.h"
#include "native/core/timespan.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{

xplane_builder::xplane_builder(xplane* plane)
    : xstats_builder<profiler::xplane>(plane, this), plane_(plane)
{
    for (auto& id_and_metadata : *plane->mutable_event_metadata())
    {
        auto& metadata          = id_and_metadata.second;
        last_event_metadata_id_ = std::max<int64_t>(last_event_metadata_id_, metadata.id());
        if (!metadata.name().empty())
        {
            event_metadata_by_name_.emplace(std::string(metadata.name()), &metadata);
        }
    }
    for (auto& id_and_metadata : *plane->mutable_stat_metadata())
    {
        auto& metadata         = id_and_metadata.second;
        last_stat_metadata_id_ = std::max<int64_t>(last_stat_metadata_id_, metadata.id());
        if (!metadata.name().empty())
        {
            stat_metadata_by_name_.emplace(std::string(metadata.name()), &metadata);
        }
    }
    for (xline& line : *plane->mutable_lines())
    {
        lines_by_id_.emplace(line.id(), &line);
    }
}

xevent_metadata* xplane_builder::get_or_create_event_metadata(int64_t metadata_id)
{
    xevent_metadata& metadata = (*plane_->mutable_event_metadata())[metadata_id];
    metadata.set_id(metadata_id);
    return &metadata;
}

xevent_metadata* xplane_builder::create_event_metadata()
{
    return get_or_create_event_metadata(++last_event_metadata_id_);
}

xevent_metadata* xplane_builder::get_or_create_event_metadata(std::string_view name)
{
    xevent_metadata*& metadata = event_metadata_by_name_[std::string(name)];
    if (metadata == nullptr)
    {
        metadata = create_event_metadata();
        metadata->set_name(std::string(name));
    }
    return metadata;
}

xevent_metadata* xplane_builder::get_or_create_event_metadata(std::string&& name)
{
    xevent_metadata*& metadata = event_metadata_by_name_[name];
    if (metadata == nullptr)
    {
        metadata = create_event_metadata();
        metadata->set_name(std::move(name));
    }
    return metadata;
}

std::vector<xevent_metadata*> xplane_builder::get_or_create_events_metadata(
    const std::vector<std::string_view>& names)
{
    std::vector<xevent_metadata*> metadata;
    metadata.reserve(names.size());
    for (std::string_view const name : names)
    {
        metadata.push_back(get_or_create_event_metadata(name));
    }
    return metadata;
}

xevent_metadata* xplane_builder::GetEventMetadata(std::string_view name) const
{
    auto result = event_metadata_by_name_.find(std::string(name));
    if (result == event_metadata_by_name_.end())
    {
        return nullptr;
    }
    return result->second;
}

x_stat_metadata* xplane_builder::stat_metadata(std::string_view name) const
{
    auto result = stat_metadata_by_name_.find(std::string(name));
    if (result == stat_metadata_by_name_.end())
    {
        return nullptr;
    }
    return result->second;
}

x_stat_metadata* xplane_builder::get_or_create_stat_metadata(int64_t metadata_id)
{
    x_stat_metadata& metadata = (*plane_->mutable_stat_metadata())[metadata_id];
    metadata.set_id(metadata_id);
    return &metadata;
}

const x_stat_metadata* xplane_builder::stat_metadata(int64_t metadata_id) const
{
    auto result = plane_->stat_metadata().find(metadata_id);
    if (result == plane_->stat_metadata().end())
    {
        return nullptr;
    }
    return &(result->second);
}

x_stat_metadata* xplane_builder::CreateStatMetadata()
{
    return get_or_create_stat_metadata(++last_stat_metadata_id_);
}

x_stat_metadata* xplane_builder::get_or_create_stat_metadata(std::string_view name)
{
    x_stat_metadata*& metadata = stat_metadata_by_name_[std::string(name)];
    if (metadata == nullptr)
    {
        metadata = CreateStatMetadata();
        metadata->set_name(std::string(name));
    }
    return metadata;
}

x_stat_metadata* xplane_builder::get_or_create_stat_metadata(std::string&& name)
{
    x_stat_metadata*& metadata = stat_metadata_by_name_[name];
    if (metadata == nullptr)
    {
        metadata = CreateStatMetadata();
        metadata->set_name(std::move(name));
    }
    return metadata;
}

xline_builder xplane_builder::get_or_create_line(int64_t line_id)
{
    xline*& line = lines_by_id_[line_id];
    if (line == nullptr)
    {
        line = plane_->add_lines();
        line->set_id(line_id);
    }
    return xline_builder(line, this);
}

xevent_builder xline_builder::add_event(const timespan& timespan, const xevent_metadata& metadata)
{
    xevent* event = line_->add_events();
    event->set_metadata_id(metadata.id());
    xevent_builder builder(line_, plane_, event);
    builder.SetOffsetPs(timespan.begin_ps());
    builder.SetDurationPs(timespan.duration_ps());
    return builder;
}

xevent_builder xline_builder::add_event(const xevent_metadata& metadata)
{
    xevent* event = line_->add_events();
    event->set_metadata_id(metadata.id());
    return {line_, plane_, event};
}

xevent_builder xline_builder::add_event(const xevent& event)
{
    xevent* new_event = line_->add_events();
    *new_event        = event;
    return {line_, plane_, new_event};
}

void xline_builder::set_time_stamp_ns_and_adjust_event_offsets(int64_t timestamp_ns)
{
    int64_t const offset_ps = xevent_builder::NanoToPico(line_->timestamp_ns() - timestamp_ns);
    line_->set_timestamp_ns(timestamp_ns);
    if (offset_ps != 0)
    {
        for (auto& event : *line_->mutable_events())
        {
            event.set_offset_ps(event.offset_ps() + offset_ps);
        }
    }
}

}  // namespace profiler
