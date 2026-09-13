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

#include "common/profiler_strings.h"

namespace profiler
{

// A timespan is the time extent of an event: a pair of (begin, duration).
// Events may have duration 0 ("instant events") but duration can't be negative.
class timespan
{
public:
    static timespan from_end_points(uint64_t begin_ps, uint64_t end_ps)
    {
        if (begin_ps > end_ps)
        {
            return timespan(begin_ps, 0);
        }
        return timespan(begin_ps, end_ps - begin_ps);
    }

    explicit timespan(uint64_t begin_ps = 0, uint64_t duration_ps = 0)
        : begin_ps_(begin_ps), duration_ps_(duration_ps)
    {
    }

    uint64_t begin_ps() const { return begin_ps_; }
    uint64_t middle_ps() const { return begin_ps_ + duration_ps_ / 2; }
    uint64_t end_ps() const { return begin_ps_ + duration_ps_; }
    uint64_t duration_ps() const { return duration_ps_; }

    // Returns true if the timespan represents an instant in time (duration 0).
    bool instant() const { return duration_ps() == 0; }

    // Returns true if this is an empty timespan.
    bool empty() const { return begin_ps() == 0 && duration_ps() == 0; }

    // Note for Overlaps() and Includes(Timespan& other) below:
    //   We have a design choice whether the end-point comparison should be
    //   inclusive or exclusive. We decide to go for inclusive. The implication
    //   is that an instant timespan could belong to two consecutive intervals
    //   (e.g., Timespan(12, 0) will be included in both Timespan(11, 1) and
    //   Timespan(12, 1)). We think this is okay because the common scenario
    //   would be that we search for the interval that includes a point
    //   in time from left to right, and return the first interval found.

    // Returns true if the timespan overlaps with other.
    bool overlaps(const timespan& other) const
    {
        return begin_ps() <= other.end_ps() && other.begin_ps() <= end_ps();
    }

    // Returns true if this timespan includes the other.
    bool includes(const timespan& other) const
    {
        return begin_ps() <= other.begin_ps() && other.end_ps() <= end_ps();
    }

    // Returns true if time_ps is within this timespan.
    bool includes(uint64_t time_ps) const { return includes(timespan(time_ps)); }

    // Returns the duration in ps that this timespan overlaps with the other.
    uint64_t overlapped_duration_ps(const timespan& other) const
    {
        if (!overlaps(other))
            return 0;
        return std::min(end_ps(), other.end_ps()) - std::max(begin_ps(), other.begin_ps());
    }

    // Expands the timespan to include other.
    void expand_to_include(const timespan& other)
    {
        *this = from_end_points(
            std::min(begin_ps(), other.begin_ps()), std::max(end_ps(), other.end_ps()));
    }

    // Compares timespans by their begin time (ascending), duration (descending)
    // so nested spans are sorted from outer to innermost.
    bool operator<(const timespan& other) const
    {
        if (begin_ps_ < other.begin_ps_)
            return true;
        if (begin_ps_ > other.begin_ps_)
            return false;
        return duration_ps_ > other.duration_ps_;
    }

    // Returns true if this timespan is equal to the given timespan.
    bool operator==(const timespan& other) const
    {
        return begin_ps_ == other.begin_ps_ && duration_ps_ == other.duration_ps_;
    }

    // Returns a string that shows the begin and end times.
    std::string debug_string() const
    {
        return strings::str_cat("[", begin_ps(), ", ", end_ps(), "]");
    }

    // Compares timespans by their duration_ps (ascending), begin time
    // (ascending).
    static bool by_duration(const timespan& a, const timespan& b)
    {
        if (a.duration_ps_ < b.duration_ps_)
            return true;
        if (a.duration_ps_ > b.duration_ps_)
            return false;
        return a.begin_ps_ < b.begin_ps_;
    }

    /*PROFILER_FORCE_INLINE*/ static int64_t milli_to_pico(int64_t milliseconds)
    {
        return milliseconds * 1000000000LL;  // 10^9
    }

private:
    uint64_t begin_ps_;
    uint64_t duration_ps_;  // 0 for an instant event.
};

// Creates a timespan from endpoints in picoseconds.
inline timespan pico_span(uint64_t start_ps, uint64_t end_ps)
{
    return timespan::from_end_points(start_ps, end_ps);
}

// Creates a timespan from endpoints in milliseconds.
inline timespan milli_span(double start_ms, double end_ms)
{
    return pico_span(
        timespan::milli_to_pico((int64_t)start_ms), timespan::milli_to_pico((int64_t)end_ms));
}

}  // namespace profiler
