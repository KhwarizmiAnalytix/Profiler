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

#pragma once

#include <cassert>
#include <cstdio>
#include <string>

#include "common/profiler_macros.h"

namespace profiler
{
namespace profiler_impl
{
namespace internal
{

/**
 * @brief Format a double value with specified format string.
 *
 * @param fmt Printf-style format string (e.g., "%.1f", "%.2f")
 * @param d Double value to format
 * @return Formatted string
 */
inline std::string format_double(const char* fmt, double d)
{
    constexpr int       kBufferSize = 32;
    char                buffer[kBufferSize];
    PROFILER_UNUSED int result = snprintf(buffer, kBufferSize, fmt, d);
    assert(result > 0 && result < kBufferSize);
    return std::string(buffer);
}

}  // namespace internal

/**
 * @brief Format double with one digit after the decimal point.
 *
 * @param d Double value to format
 * @return Formatted string (e.g., "3.1", "42.7")
 */
inline std::string one_digit(double d)
{
    return internal::format_double("%.1f", d);
}

/**
 * @brief Format double with 2 digits after the decimal point.
 *
 * @param d Double value to format
 * @return Formatted string (e.g., "3.14", "42.75")
 */
inline std::string two_digits(double d)
{
    return internal::format_double("%.2f", d);
}

/**
 * @brief Format double with 3 digits after the decimal point.
 *
 * @param d Double value to format
 * @return Formatted string (e.g., "3.142", "42.750")
 */
inline std::string three_digits(double d)
{
    return internal::format_double("%.3f", d);
}

/**
 * @brief Format double with maximum precision.
 *
 * Uses %.17g format to allow parsing the result back to the same number.
 * This provides enough precision to round-trip double values.
 *
 * @param d Double value to format
 * @return Formatted string with maximum precision
 */
inline std::string max_precision(double d)
{
    return internal::format_double("%.17g", d);
}

}  // namespace profiler_impl
}  // namespace profiler
