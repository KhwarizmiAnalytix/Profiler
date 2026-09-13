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
 *       or SaaS usage. Contact us at licensing@xsigma.co.uk.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace strings
{
namespace internal
{
template <typename T>
void to_string_helper(std::ostringstream& oss, const T& val)
{
    oss << val;
}
}  // namespace internal

template <typename... Args>
std::string str_cat(const Args&... args)
{
    std::ostringstream oss;
    (internal::to_string_helper(oss, args), ...);
    return oss.str();
}

template <typename... Args>
void str_append(std::string* result, const Args&... args)
{
    if (result == nullptr)
    {
        return;
    }

    std::ostringstream oss;
    (internal::to_string_helper(oss, args), ...);
    *result += oss.str();
}

inline std::string to_lower(std::string_view input)
{
    std::string result(input);
    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}
}  // namespace strings
