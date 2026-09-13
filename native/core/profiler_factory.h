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

/* Copyright 2019 The TensorFlow Authors All Rights Reserved.

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

#include <functional>
#include <memory>
#include <vector>

#include "native/core/profiler_interface.h"
#include "native/core/profiler_options.h"

namespace profiler
{

/**
 * @brief Factory function type for creating profiler interfaces
 *
 * A profiler_factory returns an instance of ProfilerInterface if ProfileOptions
 * require it. Otherwise, it might return nullptr.
 */
using profiler_factory = std::function<std::unique_ptr<profiler_interface>(const profile_options&)>;

/**
 * @brief Registers a profiler factory
 *
 * Should be invoked at most once per factory.
 *
 * @param factory The profiler factory function to register
 */
PROFILER_API void register_profiler_factory(profiler_factory factory);

/**
 * @brief Creates profiler instances using registered factories
 *
 * Invokes all registered profiler factories with the given options, and
 * returns the instantiated (non-null) profiler interfaces.
 *
 * @param options The profiling options to use
 * @return Vector of created profiler interfaces
 */
PROFILER_API std::vector<std::unique_ptr<profiler_interface>> create_profilers(
    const profile_options& options);

/**
 * @brief Clears all registered profiler factories
 *
 * For testing only.
 */
PROFILER_API void clear_registered_profilers_for_test();

}  // namespace profiler
