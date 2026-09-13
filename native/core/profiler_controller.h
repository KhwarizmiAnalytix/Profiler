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
#pragma once

#include <memory>  // for unique_ptr

#include "native/core/profiler_interface.h"  // for profiler_interface
namespace profiler
{
class x_space;
}

namespace profiler
{

/**
 * @brief Decorator for Profiler profiler plugins (REQUIRED COMPONENT)
 *
 * Tracks that calls to the underlying profiler interface functions are made
 * in the expected order: start, stop and collect_data. Making the calls
 * in a different order causes them to be aborted.
 *
 * Calls made in the right order will be aborted if one of the calls to the
 * decorated profiler interface fails, and no more calls will be forwarded to
 * the decorated profiler.
 *
 * COMPONENT CLASSIFICATION: REQUIRED
 * This is a core component essential for profiler lifecycle management.
 */
class profiler_controller : public profiler_interface
{
public:
    /**
     * @brief Constructs a profiler_controller with the given profiler
     * @param profiler The profiler interface to control
     */
    explicit profiler_controller(std::unique_ptr<profiler_interface> profiler);

    /**
     * @brief Destructor
     */
    ~profiler_controller() override;

    /**
     * @brief Starts profiling if in the correct state.
     */
    profiler_status start() override;

    /**
     * @brief Stops profiling if in the correct state.
     */
    profiler_status stop() override;

    /**
     * @brief Collects profiling data if in the correct state.
     */
    profiler_status collect_data(x_space* space) override;

private:
    /**
     * @brief Enumeration for profiler state tracking
     */
    enum class profiler_state_enum
    {
        INIT         = 0,  ///< Initial state
        START        = 1,  ///< Started state
        STOP         = 2,  ///< Stopped state
        COLLECT_DATA = 3,  ///< Data collected state
    };

    profiler_state_enum state_ = profiler_state_enum::INIT;  ///< Current profiler state
    std::unique_ptr<profiler_interface> profiler_;           ///< Underlying profiler interface
    profiler_status                     status_;             ///< Result of calls to profiler_
};

}  // namespace profiler
