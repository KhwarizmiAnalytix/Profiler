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

/* Copyright 2022 The OpenXLA Authors.

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

#include <memory>

#include "native/core/profiler_interface.h"

namespace profiler
{
namespace profiler_impl
{

struct host_tracer_options
{
    // Levels of host tracing:
    // - Level 0 is used to disable host traces.
    // - Level 1 enables tracing of only user instrumented (or default) traceme.
    // - Level 2 enables tracing of all level 1 traceme(s) and instrumented high
    //           level program execution details (expensive operations, etc).
    //           This is the default.
    // - Level 3 enables tracing of all level 2 traceme(s) and more verbose
    //           (low-level) program execution details (cheap operations, etc).
    int trace_level = 2;

    // Filter mask for selective event recording.
    // Each bit can represent a category or type of event to include.
    // Default: all bits set (all events pass the filter).
    uint64_t filter_mask = ~0ULL;
};

std::unique_ptr<profiler_interface> create_host_tracer(const host_tracer_options& options);

}  // namespace profiler_impl
}  // namespace profiler
