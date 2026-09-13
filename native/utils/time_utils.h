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

#include <cstdint>

#include "common/profiler_export.h"
#include "common/profiler_macros.h"
#include "native/utils/math_utils.h"

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Get the current CPU wallclock time in nanoseconds.
 *
 * This function provides high-resolution timing for profiling purposes.
 * It uses the system's monotonic clock for accurate time measurements.
 *
 * @return Current time in nanoseconds since an arbitrary epoch
 */
PROFILER_API int64_t get_current_time_nanos();

/**
 * @brief Sleep for the specified duration in nanoseconds.
 *
 * @param ns Duration to sleep in nanoseconds
 */
PROFILER_API void sleep_for_nanos(int64_t ns);

/**
 * @brief Sleep for the specified duration in microseconds.
 *
 * @param us Duration to sleep in microseconds
 */
inline void sleep_for_micros(int64_t us)
{
    sleep_for_nanos(static_cast<int64_t>(micro_to_nano(static_cast<double>(us))));
}

/**
 * @brief Sleep for the specified duration in milliseconds.
 *
 * @param ms Duration to sleep in milliseconds
 */
inline void sleep_for_millis(int64_t ms)
{
    sleep_for_nanos(milli_to_nano(static_cast<double>(ms)));
}

/**
 * @brief Sleep for the specified duration in seconds.
 *
 * @param s Duration to sleep in seconds
 */
inline void sleep_for_seconds(int64_t s)
{
    sleep_for_nanos(uni_to_nano(static_cast<double>(s)));
}

/**
 * @brief Spin (busy-wait) for the specified duration in nanoseconds.
 *
 * This function actively waits by repeatedly checking the current time,
 * consuming CPU cycles. It's more accurate than sleep for very short
 * durations but wastes CPU resources.
 *
 * **Use Case**: Testing only. Sleep precision is poor for very short
 * durations, so spinning simulates work instead of sleeping.
 *
 * @param ns Duration to spin in nanoseconds
 */
PROFILER_API void spin_for_nanos(int64_t ns);

/**
 * @brief Spin (busy-wait) for the specified duration in microseconds.
 *
 * @param us Duration to spin in microseconds
 */
inline void spin_for_micros(int64_t us)
{
    spin_for_nanos(us * 1000);
}

}  // namespace profiler_impl
}  // namespace profiler
