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
#include "native/core/profiler_factory.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "native/core/profiler_controller.h"
#include "native/core/profiler_interface.h"
#include "native/core/profiler_options.h"

namespace profiler
{

namespace
{

/**
 * @brief Registry for profiler factories
 *
 * Thread-safe singleton that manages profiler factory registration and creation.
 */
class factory_registry
{
public:
    /**
     * @brief Gets the singleton instance
     * @return Reference to the factory registry instance
     */
    static factory_registry& instance()
    {
        static factory_registry instance;
        return instance;
    }

    /**
     * @brief Registers a profiler factory
     * @param factory The factory function to register
     */
    void register_factory(profiler_factory factory)
    {
        std::scoped_lock const lock(mutex_);
        factories_.push_back(std::move(factory));
    }

    /**
     * @brief Creates all profilers using registered factories
     * @param options The profiling options to use
     * @return Vector of created profiler interfaces
     */
    std::vector<std::unique_ptr<profiler_interface>> create_all_profilers(
        const profile_options& options)
    {
        std::vector<std::unique_ptr<profiler_interface>> result;
        std::scoped_lock const                           lock(mutex_);

        result.reserve(factories_.size());  // Optimize for fewer allocations
        for (const auto& factory : factories_)
        {
            if (auto profiler = factory(options))
            {
                result.emplace_back(std::make_unique<profiler_controller>(std::move(profiler)));
            }
        }
        return result;
    }

    /**
     * @brief Clears all registered factories
     */
    void clear_factories()
    {
        std::scoped_lock const lock(mutex_);
        factories_.clear();
    }

    factory_registry(const factory_registry&)            = delete;
    factory_registry& operator=(const factory_registry&) = delete;
    factory_registry(factory_registry&&)                 = delete;
    factory_registry& operator=(factory_registry&&)      = delete;

private:
    factory_registry()  = default;
    ~factory_registry() = default;

    std::mutex                    mutex_;
    std::vector<profiler_factory> factories_;
};

}  // namespace

void register_profiler_factory(profiler_factory factory)
{
    factory_registry::instance().register_factory(std::move(factory));
}

std::vector<std::unique_ptr<profiler_interface>> create_profilers(const profile_options& options)
{
    return factory_registry::instance().create_all_profilers(options);
}

void clear_registered_profilers_for_test()
{
    factory_registry::instance().clear_factories();
}

}  // namespace profiler