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

/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

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
#include "native/exporters/xplane/xplane_visitor.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "common/flat_hash.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{

x_stat_visitor::x_stat_visitor(const xplane_visitor* plane, const xstat* stat)
    : stat_(stat),
      metadata_(plane->get_stat_metadata(stat->metadata_id())),
      plane_(plane),
      type_(plane->get_stat_type(stat->metadata_id()))
{
}

x_stat_visitor::x_stat_visitor(
    const xplane_visitor*  plane,
    const xstat*           stat,
    const x_stat_metadata* metadata,
    std::optional<int64_t> type)
    : stat_(stat), metadata_(metadata), plane_(plane), type_(type)
{
}

std::string x_stat_visitor::to_string() const
{
    switch (stat_->value_case())
    {
    case xstat::value_case_type::kInt64Value:
        return std::to_string(stat_->int64_value());
    case xstat::value_case_type::kUint64Value:
        return std::to_string(stat_->uint64_value());
    case xstat::value_case_type::kDoubleValue:
        return std::to_string(stat_->double_value());
    case xstat::value_case_type::kStrValue:
        return stat_->str_value();
    case xstat::value_case_type::kBytesValue:
        return "<opaque bytes>";
    case xstat::value_case_type::kRefValue:
        return std::string(plane_->get_stat_metadata(stat_->ref_value())->name());
    case xstat::value_case_type::VALUE_NOT_SET:
        return "";
    }
    return "";
}

std::string_view x_stat_visitor::str_or_ref_value() const
{
    switch (stat_->value_case())
    {
    case xstat::value_case_type::kStrValue:
        return stat_->str_value();
    case xstat::value_case_type::kRefValue:
        return plane_->get_stat_metadata(stat_->ref_value())->name();
    case xstat::value_case_type::kInt64Value:
    case xstat::value_case_type::kUint64Value:
    case xstat::value_case_type::kDoubleValue:
    case xstat::value_case_type::kBytesValue:
    case xstat::value_case_type::VALUE_NOT_SET:
    default:
        return {};
    }
}

xevent_visitor::xevent_visitor(const xplane_visitor* plane, const xline* line, const xevent* event)
    : xstats_owner<xevent>(plane, event),
      plane_(plane),
      line_(line),
      event_(event),
      metadata_(plane->get_event_metadata(event_->metadata_id())),
      type_(plane->get_event_type(event_->metadata_id()))
{
}

xplane_visitor::xplane_visitor(
    const xplane*         plane,
    const TypeGetterList& event_type_getter_list,
    const TypeGetterList& stat_type_getter_list)
    : xstats_owner<profiler::xplane>(this, plane), plane_(plane)
{
    build_event_type_map(plane, event_type_getter_list);
    build_stat_type_map(plane, stat_type_getter_list);
}

void xplane_visitor::build_event_type_map(
    const xplane* plane, const TypeGetterList& event_type_getter_list)
{
    if (event_type_getter_list.empty())
    {
        return;
    }
    for (const auto& event_metadata : plane->event_metadata())
    {
        uint64_t const metadata_id = event_metadata.first;
        const auto&    metadata    = event_metadata.second;
        for (const auto& event_type_getter : event_type_getter_list)
        {
            std::optional<int64_t> event_type = event_type_getter(metadata.name());
            if (event_type.has_value())
            {
                [[maybe_unused]] auto result = event_type_by_id_.emplace(metadata_id, *event_type);
                // PROFILER_CHECK_DEBUG(result.second);  // inserted
                break;
            }
        }
    }
}

const xevent_metadata* xplane_visitor::get_event_metadata(int64_t event_metadata_id) const
{
    const auto& event_metadata_by_id = plane_->event_metadata();
    const auto  it                   = event_metadata_by_id.find(event_metadata_id);
    if (it != event_metadata_by_id.end())
    {
        return &it->second;
    }
    return &xevent_metadata::default_instance();
}

std::optional<int64_t> xplane_visitor::get_event_type(int64_t event_metadata_id) const
{
    const auto it = event_type_by_id_.find(event_metadata_id);
    if (it != event_type_by_id_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void xplane_visitor::build_stat_type_map(
    const xplane* plane, const TypeGetterList& stat_type_getter_list)
{
    if (stat_type_getter_list.empty())
    {
        return;
    }
    for (const auto& stat_metadata : plane->stat_metadata())
    {
        uint64_t const metadata_id = stat_metadata.first;
        const auto&    metadata    = stat_metadata.second;
        for (const auto& stat_type_getter : stat_type_getter_list)
        {
            std::optional<int64_t> stat_type = stat_type_getter(metadata.name());
            if (stat_type.has_value())
            {
                [[maybe_unused]] auto result = stat_type_by_id_.emplace(metadata_id, *stat_type);
                // PROFILER_CHECK_DEBUG(result.second);  // inserted
                stat_metadata_by_type_.emplace(*stat_type, &metadata);
                break;
            }
        }
    }
}

const x_stat_metadata* xplane_visitor::get_stat_metadata(int64_t stat_metadata_id) const
{
    if (plane_ == nullptr)
    {
        return &x_stat_metadata::default_instance();
    }

    const auto& stat_metadata_by_id = plane_->stat_metadata();
    const auto  it                  = stat_metadata_by_id.find(stat_metadata_id);

    if (it != stat_metadata_by_id.end())
    {
        const x_stat_metadata& metadata = it->second;
        return &metadata;
    }

    return &x_stat_metadata::default_instance();
}

std::optional<int64_t> xplane_visitor::get_stat_type(int64_t stat_metadata_id) const
{
    const auto it = stat_type_by_id_.find(stat_metadata_id);
    if (it != stat_type_by_id_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

const x_stat_metadata* xplane_visitor::get_stat_metadata_by_type(int64_t stat_type) const
{
    const auto it = stat_metadata_by_type_.find(stat_type);
    if (it != stat_metadata_by_type_.end())
    {
        return it->second;
    }
    return nullptr;
}
}  // namespace profiler
