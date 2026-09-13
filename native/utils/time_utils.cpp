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

#include "native/utils/time_utils.h"

#include <thread>

#include "native/tracing/traceme.h"

namespace profiler::profiler_impl
{

int64_t get_current_time_nanos()
{
    // Delegate to the same monotonic clock as traceme.h's get_current_time_nanos() so this
    // shares a single clock source with the rest of the native profiling pipeline, rather than
    // maintaining an independent (and previously slightly different) implementation.
    return profiler::get_current_time_nanos();
}

void sleep_for_nanos(int64_t ns)
{
    if (ns <= 0)
    {
        return;
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

void spin_for_nanos(int64_t ns)
{
    if (ns <= 0)
    {
        return;
    }
    int64_t const deadline = get_current_time_nanos() + ns;
    while (get_current_time_nanos() < deadline)
    {
        // Busy-wait (spin)
    }
}

}  // namespace profiler::profiler_impl
