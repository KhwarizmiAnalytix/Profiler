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

#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "common/flat_hash.h"
#include "common/profiler_export.h"
#include "common/profiler_macros.h"
#include "native/core/timespan.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{

class xplane_builder;

inline static bool simple_atod(std::string_view str, double* out)
{
    if (str.empty() || !out)
        return false;

    size_t i        = 0;
    bool   negative = false;

    // Check for optional sign
    if (str[i] == '-')
    {
        negative = true;
        i++;
    }
    else if (str[i] == '+')
    {
        i++;
    }

    double result          = 0.0;
    bool   has_decimal     = false;
    double decimal_divisor = 1.0;

    // Parse digits before and after the decimal point
    while (i < str.size())
    {
        char c = str[i];
        if (std::isdigit(c))
        {
            int digit = c - '0';
            if (has_decimal)
            {
                decimal_divisor *= 10.0;
                result += digit / decimal_divisor;
            }
            else
            {
                result = result * 10.0 + digit;
            }
        }
        else if (c == '.')
        {
            if (has_decimal)
                return false;  // Multiple decimal points
            has_decimal = true;
        }
        else
        {
            break;
        }
        i++;
    }

    // Handle scientific notation (e.g., "1.23e-4")
    if (i < str.size() && (str[i] == 'e' || str[i] == 'E'))
    {
        i++;
        bool exp_negative = false;
        if (i < str.size() && str[i] == '-')
        {
            exp_negative = true;
            i++;
        }
        else if (i < str.size() && str[i] == '+')
        {
            i++;
        }

        int exponent = 0;
        while (i < str.size() && std::isdigit(str[i]))
        {
            int digit = str[i] - '0';
            if (exponent > (std::numeric_limits<int>::max() - digit) / 10)
            {
                return false;  // Exponent overflow
            }
            exponent = exponent * 10 + digit;
            i++;
        }

        result *= std::pow(10, exp_negative ? -exponent : exponent);
    }

    // Check for any trailing characters (invalid input)
    if (i < str.size())
        return false;

    *out = negative ? -result : result;
    return true;
}

template <typename IntType>
bool simple_atoi(std::string_view str, IntType* out)
{
    // Check that the output type is an integer
    static_assert(std::is_integral<IntType>::value, "SimpleAtoi requires an integer type.");

    if (str.empty() || !out)
        return false;

    IntType result   = 0;
    size_t  i        = 0;
    bool    negative = false;

    // Handle optional sign
    if (str[0] == '-')
    {
        negative = true;
        i++;
    }
    else if (str[0] == '+')
    {
        i++;
    }

    // Process each character
    for (; i < str.size(); ++i)
    {
        char c = str[i];
        if (!std::isdigit(c))
        {
            return false;  // Non-numeric character found
        }

        int digit = c - '0';

        // Check for overflow/underflow before multiplying and adding
        if (negative)
        {
            if (result < (std::numeric_limits<IntType>::min() + digit) / 10)
            {
                return false;  // Underflow
            }
            result = result * 10 - digit;
        }
        else
        {
            if (result > (std::numeric_limits<IntType>::max() - digit) / 10)
            {
                return false;  // Overflow
            }
            result = result * 10 + digit;
        }
    }

    *out = result;
    return true;
}

template <typename T>
class xstats_builder
{
public:
    explicit xstats_builder(T* stats_owner, xplane_builder* stats_metadata_owner)
        : stats_owner_(stats_owner), stats_metadata_owner_(stats_metadata_owner)
    {
    }

    // NOTE: A stat shouldn't have existed for the given metadata.
    // Adds a stat for the given metadata and sets its value.
    template <typename ValueT>
    void add_stat_value(const x_stat_metadata& metadata, ValueT&& value)
    {
        set_stat_value(std::forward<ValueT>(value), add_stat(metadata));
    }

    // Adds or finds a stat for the given metadata and sets its value.
    template <typename ValueT>
    void set_or_add_stat_value(const x_stat_metadata& metadata, ValueT&& value)
    {
        set_stat_value(std::forward<ValueT>(value), find_or_add_stat(metadata));
    }

    // Adds a stat by copying a stat from another xplane. Does not check if a stat
    // with the same metadata already exists in the event. To avoid duplicated
    // stats, use the variant below.
    void add_stat(const x_stat_metadata& metadata, const xstat& src_stat, const xplane& src_plane)
    {
        copy_stat_value(src_stat, src_plane, add_stat(metadata));
    }
    // Same as above but overrides an existing stat with the same metadata.
    void set_or_add_stat(
        const x_stat_metadata& metadata, const xstat& src_stat, const xplane& src_plane)
    {
        copy_stat_value(src_stat, src_plane, find_or_add_stat(metadata));
    }

    void parse_and_add_stat_value(const x_stat_metadata& metadata, std::string_view value)
    {
        int64_t  int_value;
        uint64_t uint_value;
        double   double_value;
        if (simple_atoi(value, &int_value))
        {
            add_stat_value(metadata, int_value);
        }
        else if (simple_atoi(value, &uint_value))
        {
            add_stat_value(metadata, uint_value);
        }
        else if (simple_atod(value, &double_value))
        {
            add_stat_value(metadata, double_value);
        }
        else
        {
            add_stat_value(metadata, get_or_create_stat_metadata(value));
        }
    }

    void reserve_stats(size_t num_stats) { stats_owner_->mutable_stats()->reserve(num_stats); }

    template <typename ForEachStatFunc>
    void ForEachStat(ForEachStatFunc&& for_each_stat)
    {
        for (xstat& stat : *stats_owner_->mutable_stats())
        {
            for_each_stat(&stat);
        }
    }

    const xstat* GetStat(const x_stat_metadata& stat_metadata) const
    {
        for (auto& stat : *stats_owner_->mutable_stats())
        {
            if (stat.metadata_id() == stat_metadata.id())
            {
                return &stat;
            }
        }
        return nullptr;
    }

    static uint64_t IntOrUintValue(const xstat& stat)
    {
        return stat.value_case() == xstat::value_case_type::kUint64Value ? stat.uint64_value()
                                                                         : stat.int64_value();
    }

    std::string_view StrOrRefValue(const xstat& stat);

private:
    xstat* add_stat(const x_stat_metadata& metadata)
    {
        xstat* stat = stats_owner_->add_stats();
        stat->set_metadata_id(metadata.id());
        return stat;
    }

    xstat* find_or_add_stat(const x_stat_metadata& metadata)
    {
        for (auto& stat : *stats_owner_->mutable_stats())
        {
            if (stat.metadata_id() == metadata.id())
            {
                return &stat;
            }
        }
        return add_stat(metadata);
    }

    static void set_stat_value(bool value, xstat* stat)
    {
        // bool is integral unsigned, but saved in the signed slot for backwards
        // compatibility.
        stat->set_value((int64_t)value);
    }
    template <
        typename Int,
        std::enable_if_t<
            std::conjunction<std::is_integral<Int>, std::is_signed<Int>>::value,
            bool> = true>
    static void set_stat_value(Int value, xstat* stat)
    {
        stat->set_value((int64_t)value);
    }
    template <
        typename UInt,
        std::enable_if_t<
            std::conjunction<std::is_integral<UInt>, std::negation<std::is_signed<UInt>>>::value,
            bool> = true>
    static void set_stat_value(UInt value, xstat* stat)
    {
        stat->set_value((uint64_t)value);
    }
    static void set_stat_value(double value, xstat* stat) { stat->set_value(value); }
    static void set_stat_value(const char* value, xstat* stat)
    {
        stat->set_value(std::string(value));
    }
    static void set_stat_value(std::string_view value, xstat* stat)
    {
        stat->set_value(std::string(value));
    }
    static void set_stat_value(std::string&& value, xstat* stat)
    {
        stat->set_value(std::move(value));
    }
    static void set_stat_value(const x_stat_metadata& value, xstat* stat)
    {
        stat->set_ref_value(value.id());
    }
    /*static void SetStatValue(const protobuf::MessageLite& proto, XStat* stat)
    {
        auto* bytes = stat->mutable_bytes_value();
        proto.SerializeToString(bytes);
    }*/

    void copy_stat_value(const xstat& src_stat, const xplane& src_plane, xstat* dst_stat)
    {
        switch (src_stat.value_case())
        {
        case xstat::value_case_type::VALUE_NOT_SET:
            break;
        case xstat::value_case_type::kInt64Value:
            dst_stat->set_value(src_stat.int64_value());
            break;
        case xstat::value_case_type::kUint64Value:
            dst_stat->set_value(src_stat.uint64_value());
            break;
        case xstat::value_case_type::kDoubleValue:
            dst_stat->set_value(src_stat.double_value());
            break;
        case xstat::value_case_type::kStrValue:
            dst_stat->set_value(src_stat.str_value());
            break;
        case xstat::value_case_type::kRefValue:
        {
            const auto& stat_metadata_by_id = src_plane.stat_metadata();
            const auto  it                  = stat_metadata_by_id.find(src_stat.ref_value());
            if PROFILER_LIKELY (it != stat_metadata_by_id.end())
            {
                std::string_view value = it->second.name();
                dst_stat->set_ref_value(get_or_create_stat_metadata(value).id());
            }
            break;
        }
        case xstat::value_case_type::kBytesValue:
            dst_stat->set_value(src_stat.bytes_value());
            break;
        }
    }

    const x_stat_metadata& get_or_create_stat_metadata(std::string_view value);

    T*              stats_owner_;
    xplane_builder* stats_metadata_owner_;
};

class xevent_builder : public xstats_builder<xevent>
{
public:
    xevent_builder(const xline* line, xplane_builder* plane, xevent* event)
        : xstats_builder<xevent>(event, plane), line_(line), event_(event)
    {
    }

    static constexpr int64_t PicoToNano(int64_t ps) { return ps / 1000; }

    static constexpr int64_t NanoToPico(int64_t ns) { return ns * 1000; }

    int64_t LineTimestampPs() const { return NanoToPico(line_->timestamp_ns()); }
    int64_t OffsetPs() const { return event_->offset_ps(); }
    int64_t TimestampPs() const { return LineTimestampPs() + OffsetPs(); }
    int64_t DurationPs() const { return event_->duration_ps(); }
    int64_t MetadataId() const { return event_->metadata_id(); }

    void SetOffsetPs(int64_t offset_ps) { event_->set_offset_ps(offset_ps); }

    void SetOffsetNs(int64_t offset_ns) { SetOffsetPs(NanoToPico(offset_ns)); }

    void SetTimestampPs(int64_t timestamp_ps) { SetOffsetPs(timestamp_ps - LineTimestampPs()); }
    void SetTimestampNs(int64_t timestamp_ns) { SetOffsetNs(timestamp_ns - line_->timestamp_ns()); }

    void SetNumOccurrences(int64_t num_occurrences)
    {
        event_->set_num_occurrences(num_occurrences);
    }

    void SetDurationPs(int64_t duration_ps) { event_->set_duration_ps(duration_ps); }
    void SetDurationNs(int64_t duration_ns) { SetDurationPs(NanoToPico(duration_ns)); }

    void SetEndTimestampPs(int64_t end_timestamp_ps)
    {
        SetDurationPs(end_timestamp_ps - TimestampPs());
    }
    void SetEndTimestampNs(int64_t end_timestamp_ns)
    {
        SetDurationPs(NanoToPico(end_timestamp_ns - line_->timestamp_ns()) - event_->offset_ps());
    }

    timespan GetTimespan() const { return timespan(TimestampPs(), DurationPs()); }

    void SetTimespan(timespan timespan)
    {
        SetTimestampPs(timespan.begin_ps());
        SetDurationPs(timespan.duration_ps());
    }

    bool operator<(const xevent_builder& other) const
    {
        return GetTimespan() < other.GetTimespan();
    }

private:
    const xline* line_;
    xevent*      event_;
};

class PROFILER_VISIBILITY xline_builder
{
public:
    explicit xline_builder(xline* line, xplane_builder* plane) : line_(line), plane_(plane) {}

    // Returns the owner plane.
    xplane_builder* Plane() const { return plane_; }

    int64_t Id() const { return line_->id(); }
    void    SetId(int64_t id) { line_->set_id(id); }

    int64_t NumEvents() const { return line_->events_size(); }

    std::string_view Name() const { return line_->name(); }
    void             SetName(std::string_view name) { line_->set_name(std::string(name)); }

    void SetNameIfEmpty(std::string_view name)
    {
        if (line_->name().empty())
            SetName(name);
    }

    int64_t TimestampNs() const { return line_->timestamp_ns(); }
    // This will set the line start timestamp.
    // WARNING: The offset_ps of existing events will not be altered.
    void SetTimestampNs(int64_t timestamp_ns) { line_->set_timestamp_ns(timestamp_ns); }
    // This will set the line start timestamp to specific time, and adjust
    // the offset_ps of all existing events.
    void set_time_stamp_ns_and_adjust_event_offsets(int64_t timestamp_ns);

    void SetDurationPs(int64_t duration_ps) { line_->set_duration_ps(duration_ps); }

    void ReserveEvents(size_t num_events) { line_->mutable_events()->reserve(num_events); }

    void SetDisplayNameIfEmpty(std::string_view display_name)
    {
        if (line_->display_name().empty())
        {
            line_->set_display_name(std::string(display_name));
        }
    }

    PROFILER_API xevent_builder
    add_event(const timespan& timespan, const xevent_metadata& metadata);
    PROFILER_API xevent_builder add_event(const xevent_metadata& metadata);
    PROFILER_API xevent_builder add_event(const xevent& event);

    template <typename ForEachEventFunc>
    void ForEachEvent(ForEachEventFunc&& for_each_event)
    {
        for (xevent& event : *line_->mutable_events())
        {
            for_each_event(xevent_builder(line_, plane_, &event));
        }
    }

private:
    xline*          line_;
    xplane_builder* plane_;
};

// Provides methods to build an xplane.
// NOTE: avoid to use two builders to wrap the same xplane.
class xplane_builder : public xstats_builder<profiler::xplane>
{
public:
    PROFILER_API explicit xplane_builder(xplane* plane);

    int64_t Id() const { return plane_->id(); }
    void    SetId(int64_t id) { plane_->set_id(id); }

    std::string_view Name() const { return plane_->name(); }
    void             SetName(std::string_view name) { plane_->set_name(std::string(name)); }

    void ReserveLines(size_t num_lines) { plane_->mutable_lines()->reserve(num_lines); }

    template <typename ForEachLineFunc>
    void ForEachLine(ForEachLineFunc&& for_each_line)
    {
        for (xline& line : *plane_->mutable_lines())
        {
            for_each_line(xline_builder(&line, this));
        }
    }

    // Returns a builder for the line with the given id. Creates a new line if the
    // id was unused, otherwise the builder will add events to an existing line.
    PROFILER_API xline_builder get_or_create_line(int64_t line_id);

    // Returns a new event metadata with an automatically generated metadata_id.
    // WARNING: If calling this function, don't call get_or_create_event_metadata.
    PROFILER_API xevent_metadata* create_event_metadata();

    // Returns event metadata with the given id. Creates a new metadata if the id
    // was unused.
    // WARNING: If calling this function, don't call the string overloads below
    // on the same instance.
    // TODO(b/363276932): deprecate this method and add get_event_metadata(int64_t)
    PROFILER_API xevent_metadata* get_or_create_event_metadata(int64_t metadata_id);

    // Returns event metadata with the given name. The id is internally assigned.
    // Creates a new metadata if the name was unused.
    // Using these overloads guarantees names are unique.
    // WARNING: If calling any of these overloads, do not call the integer one
    // above on the same instance.
    PROFILER_API xevent_metadata* get_or_create_event_metadata(std::string_view name);
    PROFILER_API xevent_metadata* get_or_create_event_metadata(std::string&& name);
    xevent_metadata*              get_or_create_event_metadata(const char* name)
    {
        return get_or_create_event_metadata(std::string_view(name));
    }
    // Like the functions above but for multiple names.
    std::vector<xevent_metadata*> get_or_create_events_metadata(
        const std::vector<std::string_view>& names);

    // Returns event metadata with the given name. Returns nullptr if not found.
    xevent_metadata* GetEventMetadata(std::string_view name) const;

    // Returns stat metadata with the given name. Returns nullptr if not found.
    x_stat_metadata* stat_metadata(std::string_view name) const;

    // Returns stat metadata given its id. Returns a default value if not found.
    const x_stat_metadata* stat_metadata(int64_t metadata_id) const;

    // Returns a new stat metadata with an automatically generated metadata_id.
    // WARNING: If calling this function, don't call GetOrCreateEventMetadata.
    x_stat_metadata* CreateStatMetadata();

    // Returns stat metadata with the given id. Creates a new metadata if the id
    // was unused.
    // WARNING: If calling this function, don't call the string overloads below
    // on the same instance.
    PROFILER_API x_stat_metadata* get_or_create_stat_metadata(int64_t metadata_id);

    // Returns stat metadata with the given name. The id is internally assigned.
    // Creates a new metadata if the name was unused.
    // Using these overloads guarantees names are unique.
    // WARNING: If calling any of these overloads, do not call the integer one
    // above on the same instance.
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    PROFILER_API x_stat_metadata* get_or_create_stat_metadata(std::string_view name);
    PROFILER_API x_stat_metadata* get_or_create_stat_metadata(std::string&& name);
    x_stat_metadata*              get_or_create_stat_metadata(const char* name)
    {
        return get_or_create_stat_metadata(std::string_view(name));
    }

private:
    xplane* plane_;

    // Artifacts to accelerate the builders.
    int64_t                                      last_event_metadata_id_ = 0LL;
    int64_t                                      last_stat_metadata_id_  = 0LL;
    flat_hash_map<std::string, xevent_metadata*> event_metadata_by_name_;
    flat_hash_map<std::string, x_stat_metadata*> stat_metadata_by_name_;
    flat_hash_map<int64_t, xline*>               lines_by_id_;
};

template <typename T>
const x_stat_metadata& xstats_builder<T>::get_or_create_stat_metadata(std::string_view value)
{
    return *stats_metadata_owner_->get_or_create_stat_metadata(value);
}

template <typename T>
std::string_view xstats_builder<T>::StrOrRefValue(const xstat& stat)
{
    switch (stat.value_case())
    {
    case xstat::value_case_type::kStrValue:
        return stat.str_value();
    case xstat::value_case_type::kRefValue:
    {
        auto* ref_stat = stats_metadata_owner_->stat_metadata(stat.ref_value());
        return ref_stat ? ref_stat->name() : std::string_view();
    }
    case xstat::value_case_type::kInt64Value:
    case xstat::value_case_type::kUint64Value:
    case xstat::value_case_type::kDoubleValue:
    case xstat::value_case_type::kBytesValue:
    case xstat::value_case_type::VALUE_NOT_SET:
        return std::string_view();
    }
}

}  // namespace profiler
