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

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Math utilities for profiler time and unit conversions.
 *
 * Converts among different SI units for time measurements.
 * https://en.wikipedia.org/wiki/International_System_of_Units
 *
 * **Note**: We use uint64_t for picos and nanos (used in storage),
 * and double for other units (used in UI/display).
 */

// Picosecond conversions
inline double pico_to_nano(uint64_t p)
{
    return p / 1E3;
}
inline double pico_to_micro(uint64_t p)
{
    return p / 1E6;
}
inline double pico_to_milli(uint64_t p)
{
    return p / 1E9;
}
inline double pico_to_uni(uint64_t p)
{
    return p / 1E12;
}

// Nanosecond conversions
inline uint64_t nano_to_pico(uint64_t n)
{
    return n * 1000;
}
inline double nano_to_micro(uint64_t n)
{
    return n / 1E3;
}
inline double nano_to_milli(uint64_t n)
{
    return n / 1E6;
}

// Microsecond conversions
inline double micro_to_nano(double u)
{
    return u * 1E3;
}
inline double micro_to_milli(double u)
{
    return u / 1E3;
}

// Millisecond conversions
inline uint64_t milli_to_pico(double m)
{
    return static_cast<uint64_t>(m * 1E9);
}
inline uint64_t milli_to_nano(double m)
{
    return static_cast<uint64_t>(m * 1E6);
}
inline double milli_to_uni(double m)
{
    return m / 1E3;
}

// Unit (second) conversions
inline uint64_t uni_to_pico(double uni)
{
    return static_cast<uint64_t>(uni * 1E12);
}
inline uint64_t uni_to_nano(double uni)
{
    return static_cast<uint64_t>(uni * 1E9);
}
inline double uni_to_micro(double uni)
{
    return uni * 1E6;
}
inline double uni_to_mega(double uni)
{
    return uni / 1E6;
}
inline double uni_to_giga(double uni)
{
    return uni / 1E9;
}

// Giga conversions
inline double giga_to_uni(double giga)
{
    return giga * 1E9;
}
inline double giga_to_tera(double giga)
{
    return giga / 1E3;
}

// Tera conversions
inline double tera_to_giga(double tera)
{
    return tera * 1E3;
}

/**
 * @brief Convert from clock cycles to seconds.
 *
 * @param cycles Number of clock cycles
 * @param frequency_hz Clock frequency in Hz (cycles per second)
 * @return Time in seconds
 *
 * Formula: cycles / (cycles/s) = s
 */
inline double cycles_to_seconds(double cycles, double frequency_hz)
{
    return cycles / frequency_hz;
}

/**
 * @brief Safe division that returns 0 instead of dividing by zero.
 *
 * @param dividend Numerator
 * @param divisor Denominator
 * @return dividend / divisor, or 0.0 if divisor is near zero
 */
inline double safe_divide(double dividend, double divisor)
{
    constexpr double kEpsilon = 1.0E-10;
    if ((-kEpsilon < divisor) && (divisor < kEpsilon))
        return 0.0;
    return dividend / divisor;
}

/**
 * @brief Convert between binary (GiB) and decimal (GB) units.
 *
 * GiB (Gibibyte) = 2^30 bytes = 1,073,741,824 bytes
 * GB (Gigabyte) = 10^9 bytes = 1,000,000,000 bytes
 */
inline double gibi_to_giga(double gibi)
{
    return gibi * ((1 << 30) / 1.0e9);
}
inline double giga_to_gibi(double giga)
{
    return giga / ((1 << 30) / 1.0e9);
}

/**
 * @brief Calculate bandwidth in GiB/s.
 *
 * @param bytes Number of bytes transferred
 * @param ns Time in nanoseconds
 * @return Bandwidth in GiB/s
 */
inline double gibibytes_per_second(double bytes, double ns)
{
    return giga_to_gibi(safe_divide(bytes, ns));
}

}  // namespace profiler_impl
}  // namespace profiler
