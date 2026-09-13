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

#include <stddef.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/flat_hash.h"
#include "common/profiler_export.h"
#include "native/core/timespan.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{
class xplane_visitor;

class x_stat_visitor
{
public:
    // REQUIRED: plane and stat cannot be nullptr.
    PROFILER_API x_stat_visitor(const xplane_visitor* plane, const xstat* stat);

    // REQUIRED: plane, stat and metadata cannot be nullptr.
    PROFILER_API x_stat_visitor(
        const xplane_visitor*  plane,
        const xstat*           stat,
        const x_stat_metadata* metadata,
        std::optional<int64_t> type);

    int64_t id() const { return stat_->metadata_id(); }

    std::string_view name() const { return metadata_->name(); }

    std::optional<int64_t> type() const { return type_; }

    std::string_view description() const { return metadata_->description(); }

    xstat::value_case_type value_case() const { return stat_->value_case(); }

    bool bool_value() const { return static_cast<bool>(int_value()); }

    int64_t int_value() const { return stat_->int64_value(); }

    uint64_t uint_value() const { return stat_->uint64_value(); }

    std::string_view bytes_value() const { return stat_->bytes_value(); }

    uint64_t int_or_uint_value() const
    {
        return value_case() == xstat::value_case_type::kUint64Value
                   ? uint_value()
                   : static_cast<uint64_t>(int_value());
    }

    double double_value() const { return stat_->double_value(); }

    // Returns a string view.
    // REQUIRED: the value type should be string type or reference type.
    PROFILER_API std::string_view str_or_ref_value() const;

    const xstat& raw_stat() const { return *stat_; }

    // Return a string representation of all value type.
    PROFILER_API std::string to_string() const;

private:
    const xstat*           stat_;
    const x_stat_metadata* metadata_;
    const xplane_visitor*  plane_;
    std::optional<int64_t> type_;
};

template <class T>
class xstats_owner
{
public:
    // REQUIRED: plane and stats_owner cannot be nullptr.
    xstats_owner(const xplane_visitor* plane, const T* stats_owner)
        : plane_(plane), stats_owner_(stats_owner)
    {
    }

    // For each stat, call the specified lambda.
    template <typename ForEachStatFunc>
    void for_each_stat(ForEachStatFunc&& for_each_stat) const
    {
        for (const xstat& stat : stats_owner_->stats())
        {
            for_each_stat(x_stat_visitor(plane_, &stat));
        }
    }

    // Shortcut to get a specific stat type, nullopt if absent.
    // This function performs a linear search for the requested stat value.
    // Prefer for_each_stat above when multiple stat values are necessary.
    std::optional<x_stat_visitor> get_stat(int64_t stat_type) const;

    // Same as above that skips searching for the stat.
    std::optional<x_stat_visitor> get_stat(
        int64_t stat_type, const x_stat_metadata& stat_metadata) const
    {
        for (const xstat& stat : stats_owner_->stats())
        {
            if (stat.metadata_id() == stat_metadata.id())
            {
                return x_stat_visitor(plane_, &stat, &stat_metadata, stat_type);
            }
        }
        return std::nullopt;  // type does not exist in this owner.
    }

protected:
    const xplane_visitor* plane() const { return plane_; }
    const T*              stats_owner() const { return stats_owner_; }

private:
    const xplane_visitor* plane_;
    const T*              stats_owner_;
};

class xevent_metadata_visitor : public xstats_owner<xevent_metadata>
{
public:
    // REQUIRED: plane and metadata cannot be nullptr.
    xevent_metadata_visitor(const xplane_visitor* plane, const xevent_metadata* metadata)
        : xstats_owner(plane, metadata)
    {
    }

    int64_t id() const { return metadata()->id(); }

    std::string_view name() const { return metadata()->name(); }

    bool has_display_name() const { return !metadata()->display_name().empty(); }

    std::string_view display_name() const { return metadata()->display_name(); }

    // For each child event metadata, call the specified lambda.
    template <typename ForEachChildFunc>
    void for_each_child(ForEachChildFunc&& for_each_child) const;

private:
    const xevent_metadata* metadata() const { return stats_owner(); }
};

class xevent_visitor : public xstats_owner<xevent>
{
public:
    static constexpr int64_t pico_to_nano(int64_t ps) { return ps / 1000; }

    static constexpr int64_t nano_to_pico(int64_t ns) { return ns * 1000; }

    // REQUIRED: plane, line and event cannot be nullptr.
    PROFILER_API xevent_visitor(
        const xplane_visitor* plane, const xline* line, const xevent* event);

    const xplane_visitor& plane() const  // NOLINT(bugprone-derived-method-shadowing-base-method)
    {
        return *plane_;
    }

    const xevent& raw_event() const { return *event_; }

    int64_t id() const { return event_->metadata_id(); }

    std::string_view name() const { return metadata_->name(); }

    std::optional<int64_t> type() const { return type_; }

    bool has_display_name() const { return !metadata_->display_name().empty(); }

    std::string_view display_name() const { return metadata_->display_name(); }

    double offset_ns() const { return pico_to_nano(event_->offset_ps()); }

    int64_t offset_ps() const { return event_->offset_ps(); }

    int64_t line_timestamp_ns() const { return line_->timestamp_ns(); }

    int64_t timestamp_ns() const { return line_->timestamp_ns() + offset_ns(); }

    int64_t timestamp_ps() const
    {
        return nano_to_pico(line_->timestamp_ns()) + event_->offset_ps();
    }

    double duration_ns() const { return pico_to_nano(event_->duration_ps()); }

    int64_t duration_ps() const { return event_->duration_ps(); }

    int64_t end_offset_ps() const { return event_->offset_ps() + event_->duration_ps(); }

    int64_t end_timestamp_ns() const { return timestamp_ns() + duration_ns(); }

    int64_t end_timestamp_ps() const { return timestamp_ps() + duration_ps(); }

    int64_t num_occurrences() const { return event_->num_occurrences(); }

    bool is_aggregated_event() const
    {
        return event_->data_case() == xevent::data_case_type::kNumOccurrences;
    }

    bool operator<(const xevent_visitor& other) const
    {
        return get_timespan() < other.get_timespan();
    }

    const xevent_metadata* metadata() const { return metadata_; }

    xevent_metadata_visitor get_metadata() const
    {
        return xevent_metadata_visitor(plane_, metadata_);
    }

    timespan get_timespan() const { return timespan(timestamp_ps(), duration_ps()); }

private:
    const xplane_visitor*  plane_;
    const xline*           line_;
    const xevent*          event_;
    const xevent_metadata* metadata_;
    std::optional<int64_t> type_;
};

class xline_visitor
{
public:
    // REQUIRED: plane and line cannot be nullptr.
    xline_visitor(const xplane_visitor* plane, const xline* line) : plane_(plane), line_(line) {}

    int64_t id() const { return line_->id(); }

    int64_t display_id() const { return line_->display_id() ? line_->display_id() : line_->id(); }

    std::string_view name() const { return line_->name(); }

    std::string_view display_name() const
    {
        return !line_->display_name().empty() ? line_->display_name() : line_->name();
    }

    int64_t timestamp_ns() const { return line_->timestamp_ns(); }

    int64_t duration_ps() const { return line_->duration_ps(); }

    size_t num_events() const { return line_->events_size(); }

    template <typename ForEachEventFunc>
    void for_each_event(ForEachEventFunc&& for_each_event) const
    {
        for (const xevent& event : line_->events())
        {
            for_each_event(xevent_visitor(plane_, line_, &event));
        }
    }

private:
    const xplane_visitor* plane_;
    const xline*          line_;
};

using TypeGetter     = std::function<std::optional<int64_t>(std::string_view)>;
using TypeGetterList = std::vector<TypeGetter>;

class xplane_visitor : public xstats_owner<profiler::xplane>
{
public:
    // REQUIRED: plane cannot be nullptr.
    PROFILER_API explicit xplane_visitor(
        const xplane*         plane,
        const TypeGetterList& event_type_getter_list = TypeGetterList(),
        const TypeGetterList& stat_type_getter_list  = TypeGetterList());

    int64_t id() const { return plane_->id(); }

    std::string_view name() const { return plane_->name(); }

    size_t num_lines() const { return plane_->lines_size(); }

    template <typename ForEachLineFunc>
    void for_each_line(ForEachLineFunc&& for_each_line) const
    {
        for (const xline& line : plane_->lines())
        {
            for_each_line(xline_visitor(this, &line));
        }
    }
    template <typename ThreadBundle, typename ForEachLineFunc>
    void for_each_line_in_parallel(ForEachLineFunc&& for_each_line) const
    {
        ThreadBundle bundle;
        for (const xline& line : plane_->lines())
        {
            bundle.Add([this, line = &line, &for_each_line]
                       { for_each_line(xline_visitor(this, line)); });
        }
        bundle.JoinAll();
    }

    template <typename ForEachEventMetadataFunc>
    void for_each_event_metadata(ForEachEventMetadataFunc&& for_each_event_metadata) const
    {
        for (const auto& [id, event_metadata] : plane_->event_metadata())
        {
            for_each_event_metadata(xevent_metadata_visitor(this, &event_metadata));
        }
    }

    // Returns event metadata given its id. Returns a default value if not found.
    PROFILER_API const xevent_metadata* get_event_metadata(int64_t event_metadata_id) const;

    // Returns the type of an event given its id.
    PROFILER_API std::optional<int64_t> get_event_type(int64_t event_metadata_id) const;

    // Returns stat metadata given its id. Returns a default value if not found.
    PROFILER_API const x_stat_metadata* get_stat_metadata(int64_t stat_metadata_id) const;

    // Returns stat metadata given its type. Returns nullptr if not found.
    // Use as an alternative to get_stat_metadata above.
    PROFILER_API const x_stat_metadata* get_stat_metadata_by_type(int64_t stat_type) const;

    // Returns the type of an stat given its id.
    PROFILER_API std::optional<int64_t> get_stat_type(int64_t stat_metadata_id) const;

private:
    void build_event_type_map(const xplane* plane, const TypeGetterList& event_type_getter_list);
    void build_stat_type_map(const xplane* plane, const TypeGetterList& stat_type_getter_list);

    const xplane* plane_;

    flat_hash_map<int64_t /*metadata_id*/, int64_t /*EventType*/> event_type_by_id_;
    flat_hash_map<int64_t /*metadata_id*/, int64_t /*StatType*/>  stat_type_by_id_;
    flat_hash_map<int64_t /*StatType*/, const x_stat_metadata*>   stat_metadata_by_type_;
};

template <class T>
std::optional<x_stat_visitor> xstats_owner<T>::get_stat(int64_t stat_type) const
{
    const auto* stat_metadata = plane_->get_stat_metadata_by_type(stat_type);
    if (stat_metadata != nullptr)
    {
        return get_stat(stat_type, *stat_metadata);
    }
    return std::nullopt;  // type does not exist in this owner.
}

template <typename ForEachChildFunc>
void xevent_metadata_visitor::for_each_child(ForEachChildFunc&& for_each_child) const
{
    for (int64_t child_id : metadata()->child_id())
    {
        const auto* event_metadata = plane()->get_event_metadata(child_id);
        if (event_metadata != nullptr)
        {
            for_each_child(xevent_metadata_visitor(plane(), event_metadata));
        }
    }
}

}  // namespace profiler
