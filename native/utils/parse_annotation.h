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

#include <string_view>
#include <vector>

#include "common/profiler_export.h"
#include "common/profiler_macros.h"

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Parses a string passed to traceme or scoped_annotation.
 *
 * Expected format: "<name>#<metadata>#"
 * <metadata> is a comma-separated list of "<key>=<value>" pairs.
 * If the format does not match, the result will be empty.
 *
 * Example: "matrix_multiply#rows=100,cols=200#"
 * Result: name="matrix_multiply", metadata=[{key="rows", value="100"}, {key="cols", value="200"}]
 */
struct annotation
{
    std::string_view name;

    struct metadata_entry
    {
        std::string_view key;
        std::string_view value;
    };

    std::vector<metadata_entry> metadata;
};

/**
 * @brief Parses an annotation string into structured data.
 *
 * @param annotation_str The annotation string to parse
 * @return Parsed annotation with name and metadata
 */
PROFILER_API annotation parse_annotation(std::string_view annotation_str);

/**
 * @brief Checks if an annotation string contains metadata.
 *
 * @param annotation_str The annotation string to check
 * @return true if the string ends with '#' (metadata marker), false otherwise
 */
inline bool has_metadata(std::string_view annotation_str)
{
    constexpr char kUserMetadataMarker = '#';
    return !annotation_str.empty() && annotation_str.back() == kUserMetadataMarker;
}

/**
 * @brief Parses a stack of annotations separated by "::".
 *
 * @param annotation_stack The annotation stack string to parse
 * @return Vector of parsed annotations
 *
 * Example: "outer_func#level=1#::inner_func#level=2#"
 * Result: [{name="outer_func", metadata=[{key="level", value="1"}]},
 *          {name="inner_func", metadata=[{key="level", value="2"}]}]
 */
PROFILER_API std::vector<annotation> parse_annotation_stack(std::string_view annotation_stack);

}  // namespace profiler_impl
}  // namespace profiler
