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

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "common/flat_hash.h"

namespace profiler
{

// xstat class
class xstat
{
public:
    enum class value_case_type : uint16_t
    {
        kInt64Value,
        kUint64Value,
        kDoubleValue,
        kStrValue,
        kBytesValue,
        kRefValue,
        VALUE_NOT_SET
    };

    xstat() = default;

    // Value setters
    void set_value(double val) { value_ = val; }

    void set_value(uint64_t val) { value_ = val; }

    void set_value(int64_t val) { value_ = val; }

    void set_value(std::string val) { value_ = std::move(val); }

    void set_value(std::vector<uint8_t> val) { value_ = std::move(val); }

    void set_value(std::string_view str)
    {
        std::vector<uint8_t> bytes(str.begin(), str.end());
        value_ = std::move(bytes);
    }

    void set_ref_value(int64_t ref)
    {
        value_     = ref;
        ref_value_ = true;
    }

    // Value getters
    double double_value() const
    {
        if (auto val = std::get_if<double>(&value_))
        {
            return *val;
        }
        return 0.0;
    }

    uint64_t uint64_value() const
    {
        if (auto val = std::get_if<uint64_t>(&value_))
        {
            return *val;
        }
        return 0;
    }

    int64_t int64_value() const
    {
        if (auto val = std::get_if<int64_t>(&value_))
        {
            return *val;
        }
        return 0;
    }

    const std::string& str_value() const
    {
        static const std::string empty;
        if (auto val = std::get_if<std::string>(&value_))
        {
            return *val;
        }
        return empty;
    }

    std::string_view bytes_value() const
    {
        if (const auto* bytes = std::get_if<std::vector<uint8_t>>(&value_))
        {
            return std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        }
        static const std::vector<uint8_t> empty;
        return std::string_view(reinterpret_cast<const char*>(empty.data()), empty.size());
    }

    int64_t ref_value() const
    {
        if (ref_value_ && std::holds_alternative<int64_t>(value_))
        {
            return std::get<int64_t>(value_);
        }
        return 0;
    }

    value_case_type value_case() const
    {
        if (value_.valueless_by_exception())
        {
            return value_case_type::VALUE_NOT_SET;
        }

        if (ref_value_ && std::holds_alternative<int64_t>(value_))
        {
            return value_case_type::kRefValue;
        }

        switch (value_.index())
        {
        case 0:
            return value_case_type::kDoubleValue;
        case 1:
            return value_case_type::kUint64Value;
        case 2:
            return value_case_type::kInt64Value;
        case 3:
            return value_case_type::kStrValue;
        case 4:
            return value_case_type::kBytesValue;
        default:
            return value_case_type::VALUE_NOT_SET;
        }
    }

    void set_metadata_id(int64_t id) { metadata_id_ = id; }

    int64_t metadata_id() const { return metadata_id_; }

private:
    int64_t metadata_id_ = 0;
    bool    ref_value_   = false;  // Flag to distinguish between regular int64 and reference values

    std::variant<
        double,               // index 0
        uint64_t,             // index 1
        int64_t,              // index 2
        std::string,          // index 3
        std::vector<uint8_t>  // index 4
        >
        value_;
};

// xevent_metadata class
class xevent_metadata
{
public:
    xevent_metadata() = default;

    static const xevent_metadata& default_instance()
    {
        static const xevent_metadata instance;
        return instance;
    }

    void    set_id(int64_t metadata_id) { id_ = metadata_id; }
    int64_t id() const { return id_; }

    void             set_name(std::string metadata_name) { name_ = std::move(metadata_name); }
    std::string_view name() const { return name_; }
    std::string*     mutable_name() { return &name_; }

    void             set_display_name(std::string display) { display_name_ = std::move(display); }
    std::string_view display_name() const { return display_name_; }
    std::string*     mutable_display_name() { return &display_name_; }

    const std::vector<uint8_t>& metadata() const { return metadata_; }
    std::vector<uint8_t>*       mutable_metadata() { return &metadata_; }
    void   set_metadata(std::vector<uint8_t> data) { metadata_ = std::move(data); }
    size_t stats_size() const { return stats_.size(); }
    const std::vector<xstat>& stats() const { return stats_; }
    std::vector<xstat>*       mutable_stats() { return &stats_; }
    xstat*                    add_stats()
    {
        stats_.emplace_back();
        return &stats_.back();
    }

    const std::vector<int64_t>& child_id() const { return child_id_; }
    std::vector<int64_t>*       mutable_child_id() { return &child_id_; }
    void                        add_child_id(int64_t id) { child_id_.push_back(id); }

private:
    int64_t              id_ = 0;
    std::string          name_;
    std::string          display_name_;
    std::vector<uint8_t> metadata_;
    std::vector<xstat>   stats_;
    std::vector<int64_t> child_id_;
};

// x_stat_metadata class
class x_stat_metadata
{
public:
    x_stat_metadata() = default;

    static const x_stat_metadata& default_instance()
    {
        static const x_stat_metadata instance;
        return instance;
    }

    void    set_id(int64_t metadata_id) { id_ = metadata_id; }
    int64_t id() const { return id_; }

    void             set_name(std::string metadata_name) { name_ = std::move(metadata_name); }
    std::string_view name() const { return name_; }
    std::string*     mutable_name() { return &name_; }

    void set_description(std::string metadata_description)
    {
        description_ = std::move(metadata_description);
    }
    const std::string& description() const { return description_; }
    std::string*       mutable_description() { return &description_; }

private:
    int64_t     id_ = 0;
    std::string name_;
    std::string description_;
};

// xevent class
class xevent
{
public:
    enum class data_case_type : uint16_t
    {
        kOffsetPs,
        kNumOccurrences,
        DATA_NOT_SET
    };
    struct Data
    {
        int64_t offset_ps       = 0;
        int64_t num_occurrences = 0;
    };

    xevent() = default;

    void    set_metadata_id(int64_t id) { metadata_id_ = id; }
    int64_t metadata_id() const { return metadata_id_; }

    void set_offset_ps(int64_t offset) { data_ = offset; }
    void set_data(const Data& event_data) { data_ = event_data; }

    bool has_offset_ps() const { return std::holds_alternative<int64_t>(data_); }
    bool has_data() const { return std::holds_alternative<Data>(data_); }

    int64_t offset_ps() const { return has_offset_ps() ? std::get<int64_t>(data_) : 0; }

    void set_num_occurrences(int64_t occurrences)
    {
        if (!std::holds_alternative<Data>(data_))
        {
            Data new_data;
            new_data.num_occurrences = occurrences;
            data_                    = std::move(new_data);
        }
        else
        {
            std::get<Data>(data_).num_occurrences = occurrences;
        }
    }
    int64_t num_occurrences() const
    {
        return has_data() ? std::get<Data>(data_).num_occurrences : 0;
    }

    data_case_type data_case() const
    {
        if (data_.valueless_by_exception())
        {
            return data_case_type::DATA_NOT_SET;
        }
        return std::holds_alternative<int64_t>(data_) ? data_case_type::kOffsetPs
                                                      : data_case_type::kNumOccurrences;
    }

    const Data& data() const
    {
        static const Data empty;
        return has_data() ? std::get<Data>(data_) : empty;
    }

    Data* mutable_data()
    {
        if (!has_data())
        {
            data_ = Data();
        }
        return &std::get<Data>(data_);
    }

    void    set_duration_ps(int64_t duration) { duration_ps_ = duration; }
    int64_t duration_ps() const { return duration_ps_; }

    const std::vector<xstat>& stats() const { return stats_; }
    std::vector<xstat>*       mutable_stats() { return &stats_; }
    xstat*                    add_stats()
    {
        stats_.emplace_back();
        return &stats_.back();
    }

private:
    int64_t                     metadata_id_ = 0;
    std::variant<int64_t, Data> data_;
    int64_t                     duration_ps_ = 0;
    std::vector<xstat>          stats_;
};

// xline class
class xline
{
public:
    xline() = default;

    xevent* add_events()
    {
        events_.emplace_back();
        return &events_.back();
    }
    size_t events_size() const { return events_.size(); }

    int64_t duration_ps() const { return duration_ps_; }
    void    set_duration_ps(int64_t duration) { duration_ps_ = duration; }

    int64_t timestamp_ns() const { return timestamp_ns_; }
    void    set_timestamp_ns(int64_t timestamp) { timestamp_ns_ = timestamp; }

    std::string_view display_name() const { return display_name_; }
    void set_display_name(std::string display_name) { display_name_ = std::move(display_name); }
    std::string* mutable_display_name() { return &display_name_; }

    std::string_view name() const { return name_; }
    void             set_name(std::string name) { name_ = std::move(name); }
    std::string*     mutable_name() { return &name_; }

    int64_t display_id() const { return display_id_; }
    void    set_display_id(int64_t id) { display_id_ = id; }

    int64_t id() const { return id_; }
    void    set_id(int64_t line_id) { id_ = line_id; }

    const xevent&              events(int64_t i) const { return events_[i]; }
    std::vector<xevent>*       mutable_events() { return &events_; }
    const std::vector<xevent>& events() const { return events_; }
    xevent*                    add_event()
    {
        events_.emplace_back();
        return &events_.back();
    }

private:
    int64_t             id_         = 0;
    int64_t             display_id_ = 0;
    std::string         name_;
    std::string         display_name_;
    int64_t             timestamp_ns_ = 0;
    int64_t             duration_ps_  = 0;
    std::vector<xevent> events_;
};

// Human-readable label for an XLine's originating thread. XSpace only records what host_tracer
// captured at collection time (a numeric line id plus an optional display/name string) -- there is
// no std::thread::id to recover -- so this is the single source of truth callers should use instead
// of trying to reconstruct a real thread::id from XSpace data.
inline std::string xline_thread_label(const xline& line)
{
    if (!line.display_name().empty())
    {
        return std::string(line.display_name());
    }
    if (!line.name().empty())
    {
        return std::string(line.name());
    }
    return "thread " + std::to_string(line.id());
}

// xplane class
class xplane
{
public:
    xplane() = default;

    void    set_id(int64_t plane_id) { id_ = plane_id; }
    int64_t id() const { return id_; }

    void               set_name(std::string plane_name) { name_ = std::move(plane_name); }
    const std::string& name() const { return name_; }
    std::string*       mutable_name() { return &name_; }

    size_t                    lines_size() const { return lines_.size(); }
    const auto&               lines(int index) const { return lines_[index]; }
    const std::vector<xline>& lines() const { return lines_; }
    std::vector<xline>*       mutable_lines() { return &lines_; }
    xline*                    add_lines()
    {
        lines_.emplace_back();
        return &lines_.back();
    }

    const std::map<int64_t, xevent_metadata>& event_metadata() const { return event_metadata_; }
    std::map<int64_t, xevent_metadata>*       mutable_event_metadata() { return &event_metadata_; }
    xevent_metadata*                          add_event_metadata(int64_t metadata_id)
    {
        return &event_metadata_[metadata_id];
    }

    const std::map<int64_t, x_stat_metadata>& stat_metadata() const { return stat_metadata_; }
    std::map<int64_t, x_stat_metadata>*       mutable_stat_metadata() { return &stat_metadata_; }
    x_stat_metadata* add_stat_metadata(int64_t metadata_id) { return &stat_metadata_[metadata_id]; }

    const std::vector<xstat>& stats() const { return stats_; }
    std::vector<xstat>*       mutable_stats() { return &stats_; }
    xstat*                    add_stats()
    {
        stats_.emplace_back();
        return &stats_.back();
    }

private:
    int64_t                            id_ = 0;
    std::string                        name_;
    std::vector<xline>                 lines_;
    std::map<int64_t, xevent_metadata> event_metadata_;
    std::map<int64_t, x_stat_metadata> stat_metadata_;
    std::vector<xstat>                 stats_;
};

// x_space class
class x_space
{
public:
    x_space() = default;

    const xplane& planes(size_t i) const { return planes_[i]; }
    xplane*       mutable_planes(size_t i) { return &planes_[i]; }

    const std::vector<profiler::xplane>& planes() const { return planes_; }
    std::vector<profiler::xplane>*       mutable_planes() { return &planes_; }
    xplane*                              add_planes()
    {
        planes_.emplace_back();
        return &planes_.back();
    }

    const std::vector<std::string>& errors() const { return errors_; }
    std::vector<std::string>*       mutable_errors() { return &errors_; }
    void add_error(std::string error) { errors_.push_back(std::move(error)); }

    const std::vector<std::string>& warnings() const { return warnings_; }
    std::vector<std::string>*       mutable_warnings() { return &warnings_; }
    void add_warning(std::string warning) { warnings_.push_back(std::move(warning)); }

    const std::vector<std::string>& hostnames() const { return hostnames_; }
    std::vector<std::string>*       mutable_hostnames() { return &hostnames_; }
    void add_hostname(std::string hostname) { hostnames_.push_back(std::move(hostname)); }

private:
    std::vector<profiler::xplane> planes_;
    std::vector<std::string>      errors_;
    std::vector<std::string>      warnings_;
    std::vector<std::string>      hostnames_;
};

}  // namespace profiler
