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
#pragma once

#include <optional>     // for optional
#include <string_view>  // for string_view
#include <utility>      // for exchange

#include "common/profiler_export.h"  // for PROFILER_API

namespace profiler
{

constexpr std::string_view kProfilerLockContention = "Another profiling session active.";

// Handle for the profiler lock. At most one instance of this class, the
// "active" instance, owns the profiler lock.
class PROFILER_VISIBILITY ProfilerLock
{
public:
    // Returns true if the process has active profiling session.
    static bool HasActiveSession();

    // Acquires the profiler lock if no other profiler session is currently
    // active.
    static std::optional<ProfilerLock> Acquire();

    // Default constructor creates an inactive instance.
    ProfilerLock() = default;

    // Non-copyable.
    ProfilerLock(const ProfilerLock&)            = delete;
    ProfilerLock& operator=(const ProfilerLock&) = delete;

    // Movable.
    ProfilerLock(ProfilerLock&& other) noexcept : active_(std::exchange(other.active_, false)) {}
    ProfilerLock& operator=(ProfilerLock&& other) noexcept
    {
        active_ = std::exchange(other.active_, false);
        return *this;
    }

    ~ProfilerLock() { ReleaseIfActive(); }

    // Allow creating another active instance.
    void ReleaseIfActive();

    // Returns true if this is the active instance.
    bool Active() const { return active_; }

    // Explicit constructor allows creating an active instance
    explicit ProfilerLock(bool active) : active_(active) {}

private:
    bool active_ = false;
};

}  // namespace profiler
