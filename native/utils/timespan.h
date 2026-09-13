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
#include <string>

#include "native/utils/math_utils.h"

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Represents a time interval with begin time and duration.
 *
 * A timespan is the time extent of an event: a pair of (begin, duration).
 * Events may have duration 0 ("instant events") but duration cannot be negative.
 *
 * **Design Note**: End-point comparisons are inclusive. This means an instant
 * timespan could belong to two consecutive intervals (e.g., timespan(12, 0)
 * will be included in both timespan(11, 1) and timespan(12, 1)). This is
 * acceptable because the common scenario is searching for an interval that
 * includes a point in time from left to right, returning the first match.
 */
class timespan
{
public:
    /**
     * @brief Create a timespan from begin and end points.
     *
     * @param begin_ps Begin time in picoseconds
     * @param end_ps End time in picoseconds
     * @return timespan with duration = end_ps - begin_ps (or 0 if begin > end)
     */
    static timespan from_end_points(uint64_t begin_ps, uint64_t end_ps)
    {
        if (begin_ps > end_ps)
        {
            return timespan(begin_ps, 0);
        }
        return timespan(begin_ps, end_ps - begin_ps);
    }

    /**
     * @brief Construct a timespan.
     *
     * @param begin_ps Begin time in picoseconds (default: 0)
     * @param duration_ps Duration in picoseconds (default: 0)
     */
    explicit timespan(uint64_t begin_ps = 0, uint64_t duration_ps = 0)
        : begin_ps_(begin_ps), duration_ps_(duration_ps)
    {
    }

    // Accessors
    uint64_t begin_ps() const { return begin_ps_; }
    uint64_t middle_ps() const { return begin_ps_ + duration_ps_ / 2; }
    uint64_t end_ps() const { return begin_ps_ + duration_ps_; }
    uint64_t duration_ps() const { return duration_ps_; }

    /**
     * @brief Check if this is an instant event (duration 0).
     */
    bool instant() const { return duration_ps() == 0; }

    /**
     * @brief Check if this is an empty timespan (begin and duration both 0).
     */
    bool empty() const { return begin_ps() == 0 && duration_ps() == 0; }

    /**
     * @brief Check if this timespan overlaps with another.
     *
     * @param other The other timespan to check
     * @return true if the timespans overlap (inclusive end-points)
     */
    bool overlaps(const timespan& other) const
    {
        return begin_ps() <= other.end_ps() && other.begin_ps() <= end_ps();
    }

    /**
     * @brief Check if this timespan includes another timespan.
     *
     * @param other The other timespan to check
     * @return true if this timespan fully contains the other
     */
    bool includes(const timespan& other) const
    {
        return begin_ps() <= other.begin_ps() && other.end_ps() <= end_ps();
    }

    /**
     * @brief Check if a specific time point is within this timespan.
     *
     * @param time_ps Time point in picoseconds
     * @return true if time_ps is within this timespan
     */
    bool includes(uint64_t time_ps) const { return includes(timespan(time_ps)); }

    /**
     * @brief Calculate the overlapping duration with another timespan.
     *
     * @param other The other timespan
     * @return Duration in picoseconds that the two timespans overlap
     */
    uint64_t overlapped_duration_ps(const timespan& other) const
    {
        if (!overlaps(other))
            return 0;
        return std::min(end_ps(), other.end_ps()) - std::max(begin_ps(), other.begin_ps());
    }

    /**
     * @brief Expand this timespan to include another timespan.
     *
     * @param other The timespan to include
     */
    void expand_to_include(const timespan& other)
    {
        if (other.empty())
            return;
        *this = this->empty() ? other
                              : from_end_points(
                                    std::min(begin_ps(), other.begin_ps()),
                                    std::max(end_ps(), other.end_ps()));
    }

    /**
     * @brief Compare timespans by begin time (ascending), then duration (descending).
     *
     * This ordering ensures nested spans are sorted from outer to innermost.
     */
    bool operator<(const timespan& other) const
    {
        if (begin_ps_ < other.begin_ps_)
            return true;
        if (begin_ps_ > other.begin_ps_)
            return false;
        return duration_ps_ > other.duration_ps_;
    }

    /**
     * @brief Check if two timespans are equal.
     */
    bool operator==(const timespan& other) const
    {
        return begin_ps_ == other.begin_ps_ && duration_ps_ == other.duration_ps_;
    }

    /**
     * @brief Less-than-or-equal comparison.
     */
    bool operator<=(const timespan& other) const { return *this < other || *this == other; }

    /**
     * @brief Get a debug string representation.
     *
     * @return String in format "[begin_ps, end_ps]"
     */
    std::string debug_string() const
    {
        return "[" + std::to_string(begin_ps()) + ", " + std::to_string(end_ps()) + "]";
    }

    /**
     * @brief Compare timespans by duration (ascending), then begin time (ascending).
     *
     * @param a First timespan
     * @param b Second timespan
     * @return true if a should come before b
     */
    static bool by_duration(const timespan& a, const timespan& b)
    {
        if (a.duration_ps_ < b.duration_ps_)
            return true;
        if (a.duration_ps_ > b.duration_ps_)
            return false;
        return a.begin_ps_ < b.begin_ps_;
    }

private:
    uint64_t begin_ps_;
    uint64_t duration_ps_;  // 0 for an instant event
};

/**
 * @brief Create a timespan from endpoints in picoseconds.
 */
inline timespan pico_span(uint64_t start_ps, uint64_t end_ps)
{
    return timespan::from_end_points(start_ps, end_ps);
}

/**
 * @brief Create a timespan from endpoints in milliseconds.
 */
inline timespan milli_span(double start_ms, double end_ms)
{
    return pico_span(milli_to_pico(start_ms), milli_to_pico(end_ms));
}

}  // namespace profiler_impl
}  // namespace profiler
