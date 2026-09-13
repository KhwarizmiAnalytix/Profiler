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

/* Copyright 2024 The TensorFlow Authors. All Rights Reserved.

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

#include <utility>

namespace profiler
{

// Wraps T into a union so that we can avoid the cost of automatic construction
// and destruction when tracing is disabled.
template <typename T>
union no_init
{
    // Ensure constructor and destructor do nothing.
    no_init() {}
    ~no_init() {}

    template <typename... Ts>
    void Emplace(Ts&&... args)
    {
        new (&value) T(std::forward<Ts>(args)...);
    }

    // Profiler standards-compliant lowercase aliases
    template <typename... Ts>
    void emplace(Ts&&... args)
    {
        Emplace(std::forward<Ts>(args)...);
    }

    void Destroy() { value.~T(); }

    T Consume() &&
    {
        T v = std::move(value);
        Destroy();
        return v;
    }

    // Profiler standards-compliant lowercase alias
    T consume() && { return std::move(*this).Consume(); }

    T value;
};

}  // namespace profiler
