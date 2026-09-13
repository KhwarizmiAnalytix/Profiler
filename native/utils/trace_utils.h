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

#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

namespace profiler
{
namespace profiler_impl
{

/**
 * @brief Constants used as trace viewer PID (device_id in trace_events).
 *
 * These constants define the device ID ranges for different types of
 * profiling planes in the trace viewer visualization.
 *
 * **Note**: PID 0 is unused.
 */

// Support up to 500 accelerator devices (GPU, TPU, etc.)
constexpr uint32_t kFirstDeviceId = 1;
constexpr uint32_t kLastDeviceId  = 500;

// Support up to 200 custom planes as fake devices
// Custom planes allow profiling of user-defined subsystems
constexpr uint32_t kFirstCustomPlaneDeviceId     = kLastDeviceId + 1;
constexpr uint32_t kMaxCustomPlaneDevicesPerHost = 200;
constexpr uint32_t kLastCustomPlaneDeviceId =
    kFirstCustomPlaneDeviceId + kMaxCustomPlaneDevicesPerHost - 1;

// Host threads are shown as a single fake device
constexpr uint32_t kHostThreadsDeviceId = kLastCustomPlaneDeviceId + 1;

/**
 * @brief Constants used as trace viewer TID (resource_id in trace_events).
 *
 * These thread IDs are used for derived/synthetic timeline rows that
 * don't correspond to actual OS threads but represent logical groupings
 * of events in the trace viewer.
 */

// Base for derived (synthetic) thread IDs
constexpr int kThreadIdDerivedMin = 0xdeadbeef;

// Common derived thread IDs for different event types
constexpr int kThreadIdStepInfo     = kThreadIdDerivedMin;
constexpr int kThreadIdKernelLaunch = kThreadIdDerivedMin + 1;
constexpr int kThreadIdOverhead     = kThreadIdDerivedMin + 2;
constexpr int kThreadIdSource       = kThreadIdDerivedMin + 3;

// Space for derived lines for custom operations (40 slots)
constexpr int kThreadIdCustomOpStart = kThreadIdDerivedMin + 8;
constexpr int kThreadIdCustomOpEnd   = kThreadIdDerivedMin + 48;

// Space for derived lines for application regions (240 slots)
constexpr int kThreadIdAppRegionStart = kThreadIdDerivedMin + 49;
constexpr int kThreadIdAppRegionEnd   = kThreadIdAppRegionStart + 240;

// Space for derived lines for device events (100 slots)
constexpr int kThreadIdDeviceDerivedMin = kThreadIdAppRegionEnd + 1;
constexpr int kThreadIdDeviceDerivedMax = kThreadIdDeviceDerivedMin + 99;

// Maximum derived thread ID
constexpr int kThreadIdDerivedMax = kThreadIdDeviceDerivedMax;

/**
 * @brief Check if a thread ID is a derived (synthetic) thread ID.
 *
 * @param thread_id Thread ID to check
 * @return true if the thread ID is in the derived range
 */
static inline bool is_derived_thread_id(int thread_id)
{
    return thread_id >= kThreadIdDerivedMin && thread_id <= kThreadIdDerivedMax;
}

/**
 * @brief Parse device ordinal from device names.
 *
 * Parses the device ordinal (N) from device names that follow the pattern:
 * "hostname /device:TYPE:N" or similar conventions.
 *
 * **Examples**:
 * - "localhost /device:GPU:0" -> 0
 * - "worker1 /device:CPU:2" -> 2
 * - "/device:GPU:3" -> 3
 * - "GPU:1" -> 1
 *
 * @param device_name device_option name string to parse
 * @return device_option ordinal if successfully parsed, std::nullopt otherwise
 */
static inline std::optional<uint32_t> parse_device_ordinal(std::string_view device_name)
{
    // Find the last colon (device ordinal comes after it)
    if (auto pos = device_name.find_last_of(':'); pos != std::string_view::npos)
    {
        device_name.remove_prefix(pos + 1);
    }

    // Remove any trailing whitespace or text after a space
    if (auto pos = device_name.find_first_of(' '); pos != std::string_view::npos)
    {
        device_name.remove_suffix(device_name.size() - pos);
    }

    // Try to parse as integer
    uint32_t device_id = 0;
    auto     result =
        std::from_chars(device_name.data(), device_name.data() + device_name.size(), device_id);

    if (result.ec == std::errc{} && result.ptr == device_name.data() + device_name.size())
    {
        return device_id;
    }

    return std::nullopt;
}

}  // namespace profiler_impl
}  // namespace profiler
