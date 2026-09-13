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

/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.

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

#include "native/platform/env_var.h"

#include <algorithm>  // for transform
#include <cctype>     // for tolower
#include <cstdint>
#include <cstdlib>  // for getenv
#include <sstream>
#include <string>  // for char_traits, allocator, operator==, string, basic_string
#include <string_view>
#include <vector>

////#include "logger/logger.h"  // for PROFILER_LOG_ERROR
//#include "util/string_util.h"  // for profiler::strings::to_lower
#include "common/profiler_export.h"
#include "common/profiler_macros.h"
#include "common/profiler_strings.h"

namespace profiler
{
bool read_bool_from_env_var(std::string_view env_var_name, bool default_val, bool* value)
{
    *value              = default_val;
    const char* env_val = std::getenv(env_var_name.data());  //NOLINT
    if (env_val == nullptr)
    {
        // Treat unset as success with default
        return true;
    }
    std::string str_value = env_val;

    str_value = strings::to_lower(str_value);

    if (str_value == "0" || str_value == "false")
    {
        *value = false;
        return true;
    }
    if (str_value == "1" || str_value == "true")
    {
        *value = true;
        return true;
    }

    PROFILER_LOG_ERROR(
        "Failed to parse the env-var {} into bool: {}. Use the default value: {}",
        env_var_name,
        str_value,
        default_val);
    return false;
}

bool read_int64_from_env_var(std::string_view env_var_name, int64_t default_val, int64_t* value)
{
    *value                     = default_val;
    const char* tf_env_var_val = std::getenv(env_var_name.data());  //NOLINT
    if (tf_env_var_val == nullptr)
    {
        return true;
    }

    // Inline safe string to int64 conversion

    std::string str(tf_env_var_val);
    // Trim whitespace
    size_t const start = str.find_first_not_of(" \t\n\r");
    size_t const end   = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos)
    {
        PROFILER_LOG_ERROR(
            "InvalidArgument Failed to parse the env-var {} into int64: {}. Use the default "
            "value: {}",
            env_var_name,
            tf_env_var_val,
            default_val);
        return false;
    }
    str = str.substr(start, end - start + 1);

    size_t        pos = 0;
    int64_t const val = std::stoll(str, &pos);
    if (pos == str.length())
    {
        *value = val;
        return true;
    }

    PROFILER_LOG_ERROR(
        "InvalidArgument Failed to parse the env-var {} into int64: {}. Use the default value: {}",
        env_var_name,
        tf_env_var_val,
        default_val);

    return false;
}

bool read_float_from_env_var(std::string_view env_var_name, float default_val, float* value)
{
    *value                     = default_val;
    const char* tf_env_var_val = std::getenv(env_var_name.data());  //NOLINT
    if (tf_env_var_val == nullptr)
    {
        return true;
    }

    // Inline safe string to float conversion

    std::string str(tf_env_var_val);
    // Trim whitespace
    size_t const start = str.find_first_not_of(" \t\n\r");
    size_t const end   = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos)
    {
        PROFILER_LOG_ERROR(
            "InvalidArgument Failed to parse the env-var {} into float: {}. Use the default "
            "value: {}",
            env_var_name,
            tf_env_var_val,
            default_val);
        return false;
    }
    str = str.substr(start, end - start + 1);

    size_t      pos = 0;
    float const val = std::stof(str, &pos);
    if (pos == str.length())
    {
        *value = val;
        return true;
    }

    PROFILER_LOG_ERROR(
        "InvalidArgument Failed to parse the env-var {} into float: {}. Use the default value: {}",
        env_var_name,
        tf_env_var_val,
        default_val);
    return false;
}

bool read_string_from_env_var(
    std::string_view env_var_name, std::string_view default_val, std::string& value)
{
    const char* tf_env_var_val = std::getenv(env_var_name.data());  //NOLINT
    if (tf_env_var_val != nullptr)
    {
        value = tf_env_var_val;
    }
    else
    {
        value = std::string(default_val);
    }
    return true;
}

bool read_strings_from_env_var(
    std::string_view env_var_name, std::string_view default_val, std::vector<std::string>& value)
{
    std::string str_val;
    read_string_from_env_var(env_var_name, default_val, str_val);

    // Inline string splitting by comma
    value.clear();
    if (!str_val.empty())
    {
        std::istringstream iss(str_val);
        std::string        token;
        while (std::getline(iss, token, ','))
        {
            // Trim whitespace from token
            size_t const start = token.find_first_not_of(" \t\n\r");
            size_t const end   = token.find_last_not_of(" \t\n\r");
            if (start != std::string::npos)
            {
                value.push_back(token.substr(start, end - start + 1));
            }
        }
    }

    return true;
}

}  // namespace profiler
