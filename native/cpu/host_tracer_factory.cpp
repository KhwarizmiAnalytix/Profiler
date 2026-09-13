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
#include <memory>

#include "native/core/profiler_factory.h"
#include "native/core/profiler_interface.h"
#include "native/core/profiler_options.h"
#include "native/cpu/host_tracer.h"

namespace profiler
{
namespace profiler_impl
{
namespace
{
std::unique_ptr<profiler_interface> CreateHostTracer(const profile_options& profile_options)
{
    host_tracer_options options;
    options.trace_level = profile_options.host_tracer_level();
    return create_host_tracer(options);
}

auto register_host_tracer_factory = []
{
    register_profiler_factory(&CreateHostTracer);
    return 0;
}();

}  // namespace
}  // namespace profiler_impl
}  // namespace profiler
