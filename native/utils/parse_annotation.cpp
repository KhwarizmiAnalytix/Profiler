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

#include "parse_annotation.h"

#include <algorithm>
#include <cctype>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace profiler
{
namespace profiler_impl
{
namespace
{

// Helper function to strip whitespace from both ends of a string_view
std::string_view strip_whitespace(std::string_view str)
{
    // Strip leading whitespace
    size_t start = 0;
    while (start < str.size() && (std::isspace(static_cast<unsigned char>(str[start])) != 0))
    {
        ++start;
    }

    // Strip trailing whitespace
    size_t end = str.size();
    while (end > start && (std::isspace(static_cast<unsigned char>(str[end - 1])) != 0))
    {
        --end;
    }

    return str.substr(start, end - start);
}

// Helper function to split a string by delimiter with max splits
std::vector<std::string_view> split_string(
    std::string_view str, char delimiter, size_t max_splits = 0)
{
    std::vector<std::string_view> result;
    size_t                        start  = 0;
    size_t                        pos    = 0;
    size_t                        splits = 0;

    while ((pos = str.find(delimiter, start)) != std::string_view::npos)
    {
        if (max_splits > 0 && splits >= max_splits)
        {
            break;
        }
        result.push_back(str.substr(start, pos - start));
        start = pos + 1;
        ++splits;
    }

    // Add the remaining part
    result.push_back(str.substr(start));

    return result;
}

// Splits annotation into name and metadata parts
std::vector<std::string_view> split_name_and_metadata(std::string_view annotation_str)
{
    std::vector<std::string_view> parts;

    if (!has_metadata(annotation_str))
    {
        parts.push_back(annotation_str);
    }
    else
    {
        // Remove trailing '#'
        annotation_str.remove_suffix(1);

        // Split by '#'
        parts = split_string(annotation_str, '#');

        // Keep only first two parts (name and metadata)
        if (parts.size() > 2)
        {
            parts.resize(2);
        }
    }

    // Ensure we have at least 2 parts (name and metadata)
    while (parts.size() < 2)
    {
        parts.emplace_back();
    }

    return parts;
}

// Use comma as separator to split input metadata. However, treat comma inside
// ""/''/[]/{}/() pairs as normal characters.
std::vector<std::string_view> split_pairs(std::string_view metadata)
{
    std::vector<std::string_view> key_value_pairs;
    std::stack<char>              quotes;
    size_t                        start = 0;
    size_t                        end   = 0;

    for (; end < metadata.size(); ++end)
    {
        char const ch = metadata[end];
        switch (ch)
        {
        case '\"':
        case '\'':
            if (quotes.empty() || quotes.top() != ch)
            {
                quotes.push(ch);
            }
            else
            {
                quotes.pop();
            }
            break;
        case '{':
        case '(':
        case '[':
            quotes.push(ch);
            break;
        case '}':
            if (!quotes.empty() && quotes.top() == '{')
            {
                quotes.pop();
            }
            break;
        case ')':
            if (!quotes.empty() && quotes.top() == '(')
            {
                quotes.pop();
            }
            break;
        case ']':
            if (!quotes.empty() && quotes.top() == '[')
            {
                quotes.pop();
            }
            break;
        case ',':
            if (quotes.empty())
            {
                if (end - start > 1)
                {
                    key_value_pairs.emplace_back(metadata.data() + start, end - start);
                }
                start = end + 1;  // Skip the current ','.
            }
            break;
        default:
            // Regular characters (alphanumeric, spaces, etc.) are allowed
            // They are part of the current key-value pair
            break;
        }
    }

    if (end - start > 1)
    {
        key_value_pairs.emplace_back(metadata.data() + start, end - start);
    }

    return key_value_pairs;
}

// Parses metadata string into key-value pairs
std::vector<std::pair<std::string_view, std::string_view>> parse_metadata(std::string_view metadata)
{
    std::vector<std::pair<std::string_view, std::string_view>> key_values;

    for (std::string_view const pair : split_pairs(metadata))
    {
        std::vector<std::string_view> parts = split_string(pair, '=', 1);

        if (parts.size() == 2)
        {
            std::string_view const key   = strip_whitespace(parts[0]);
            std::string_view const value = strip_whitespace(parts[1]);

            if (!key.empty() && !value.empty())
            {
                key_values.emplace_back(key, value);
            }
        }
    }

    return key_values;
}

}  // namespace

annotation parse_annotation(std::string_view annotation_str)
{
    annotation                    result;
    std::vector<std::string_view> parts = split_name_and_metadata(annotation_str);

    // parts is guaranteed to have at least 2 elements from split_name_and_metadata
    result.name = strip_whitespace(parts[0]);

    for (const auto& key_value : parse_metadata(parts[1]))
    {
        result.metadata.push_back({key_value.first, key_value.second});
    }

    return result;
}

std::vector<annotation> parse_annotation_stack(std::string_view annotation_stack)
{
    std::vector<annotation> annotations;
    const std::string       kAnnotationDelimiter = "::";

    // Split by "::" delimiter
    size_t start = 0;
    size_t pos   = 0;

    while ((pos = annotation_stack.find(kAnnotationDelimiter, start)) != std::string_view::npos)
    {
        std::string_view const part = annotation_stack.substr(start, pos - start);
        if (!part.empty())
        {
            annotations.emplace_back(parse_annotation(part));
        }
        start = pos + kAnnotationDelimiter.length();
    }

    // Add the last part
    std::string_view const last_part = annotation_stack.substr(start);
    if (!last_part.empty())
    {
        annotations.emplace_back(parse_annotation(last_part));
    }

    return annotations;
}

}  // namespace profiler_impl
}  // namespace profiler
