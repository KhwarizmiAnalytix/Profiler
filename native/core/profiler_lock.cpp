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
#include "native/core/profiler_lock.h"

#include <atomic>  // for atomic, memory_order, ATOMIC_INT_LOCK_FREE
#include <optional>

#include "common/profiler_macros.h"  // for PROFILER_UNLIKELY
////#include "logger/logger.h"           // for PROFILER_LOG_ERROR
#include "native/platform/env_var.h"  // for read_bool_from_env_var

namespace profiler
{
namespace
{

// Track whether there's an active profiler session.
// Prevents another profiler session from creating ProfilerInterface(s).
std::atomic<int> g_session_active{0};

// g_session_active implementation must be lock-free for faster execution of
// the ProfilerLock API.
// NOLINTNEXTLINE(misc-redundant-expression)
static_assert(ATOMIC_INT_LOCK_FREE == 2, "Assumed atomic<int> was lock free");

}  // namespace

/*static*/ bool ProfilerLock::HasActiveSession()
{
    return g_session_active.load(std::memory_order_relaxed) != 0;
}

/*static*/ std::optional<ProfilerLock> ProfilerLock::Acquire()
{
    // Use environment variable to permanently lock the profiler.
    // This allows running TensorFlow under an external profiling tool with all
    // built-in profiling disabled.
    static bool const tf_profiler_disabled = []
    {
        bool disabled = false;
        read_bool_from_env_var("PROFILER_DISABLE_PROFILING", false, &disabled);
        return disabled;
    }();
    if PROFILER_UNLIKELY (tf_profiler_disabled)
    {
        PROFILER_LOG_ERROR(
            "TensorFlow Profiler is permanently disabled by env var PROFILER_DISABLE_PROFILING.");

        return std::nullopt;
    }
    int const already_active = g_session_active.exchange(1, std::memory_order_acq_rel);
    if (already_active != 0)
    {
        PROFILER_LOG_ERROR(kProfilerLockContention);
        return std::nullopt;
    }
    return ProfilerLock(/*active=*/true);
}

void ProfilerLock::ReleaseIfActive()
{
    if (active_)
    {
        g_session_active.store(0, std::memory_order_release);
        active_ = false;
    }
}

}  // namespace profiler
