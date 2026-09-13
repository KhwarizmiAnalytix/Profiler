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

/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

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
#include "native/core/profiler_collection.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "native/core/profiler_interface.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{

profiler_collection::profiler_collection(std::vector<std::unique_ptr<profiler_interface>> profilers)
    : profilers_(std::move(profilers))
{
}

profiler_status profiler_collection::start()
{
    bool        ok = true;
    std::string errors;
    for (auto& profiler : profilers_)
    {
        profiler_status const status = profiler->start();
        if (!status.ok())
        {
            ok = false;
            if (!status.message().empty())
            {
                if (!errors.empty())
                {
                    errors.append("\n");
                }
                errors.append(status.message());
            }
        }
    }
    if (ok)
    {
        return profiler_status::Ok();
    }
    return errors.empty() ? profiler_status::Error("Failed to start profiler backends.")
                          : profiler_status::Error(std::move(errors));
}

profiler_status profiler_collection::stop()
{
    bool        ok = true;
    std::string errors;
    for (auto& profiler : profilers_)
    {
        profiler_status const status = profiler->stop();
        if (!status.ok())
        {
            ok = false;
            if (!status.message().empty())
            {
                if (!errors.empty())
                {
                    errors.append("\n");
                }
                errors.append(status.message());
            }
        }
    }
    if (ok)
    {
        return profiler_status::Ok();
    }
    return errors.empty() ? profiler_status::Error("Failed to stop profiler backends.")
                          : profiler_status::Error(std::move(errors));
}

profiler_status profiler_collection::collect_data(x_space* space)
{
    bool        ok = true;
    std::string errors;

    for (auto& profiler : profilers_)
    {
        profiler_status const status = profiler->collect_data(space);
        if (!status.ok())
        {
            ok = false;
            if (!status.message().empty())
            {
                if (!errors.empty())
                {
                    errors.append("\n");
                }
                errors.append(status.message());
            }
        }
    }
    profilers_.clear();  // data has been collected
    if (ok)
    {
        return profiler_status::Ok();
    }
    return errors.empty() ? profiler_status::Error("Failed to collect profiler backend data.")
                          : profiler_status::Error(std::move(errors));
}
}  // namespace profiler
