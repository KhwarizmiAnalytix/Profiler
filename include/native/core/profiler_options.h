/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>

namespace profiler
{

/**
 * @brief Configuration options for profiling sessions
 *
 * Contains all configuration parameters needed to set up and control
 * profiling behavior across different device types and profiling modes.
 *
 * @note Internal collector configuration only (design-review.md section 4):
 * built exclusively from `profiler_options` by
 * `profiler_session::build_backend_profile_options()` and consumed only by
 * `profiler_factory`/the native tracer factories. It has no public
 * constructor path from `session`/`session_options`, and no in-repo caller
 * outside `native/core`, `native/cpu`, and `native/gpu` constructs one
 * directly -- it does not need to grow another public-facing surface.
 */
class profile_options
{
public:
    /**
     * @brief Enumeration for supported device types
     */
    enum class device_type_enum : int16_t
    {
        UNSPECIFIED      = 0,  ///< device_option type not specified
        CPU              = 1,  ///< Central Processing Unit
        GPU              = 2,  ///< Graphics Processing Unit
        TPU              = 3,  ///< Tensor Processing Unit
        PLUGGABLE_DEVICE = 4   ///< Pluggable device (custom hardware)
    };

    /**
     * @brief Gets the profiler version
     * @return The profiler version number
     */
    uint32_t version() const { return version_; }

    /**
     * @brief Sets the profiler version
     * @param version The version number to set
     */
    void set_version(uint32_t version) { version_ = version; }

    /**
     * @brief Gets the target device type for profiling
     * @return The device type
     */
    device_type_enum device_type() const { return device_type_; }

    /**
     * @brief Sets the target device type for profiling
     * @param device_enum The device type to profile
     */
    void set_device_type(device_type_enum device_type) { device_type_ = device_type; }

    /**
     * @brief Gets the host tracer level
     * @return The host tracer level (0-3, higher means more detailed)
     */
    uint32_t host_tracer_level() const { return host_tracer_level_; }

    /**
     * @brief Sets the host tracer level
     * @param host_tracer_level The tracer level (0-3, higher means more detailed)
     */
    void set_host_tracer_level(uint32_t host_tracer_level)
    {
        host_tracer_level_ = host_tracer_level;
    }

    /**
     * @brief Gets the device tracer level
     * @return The device tracer level (0-3, higher means more detailed)
     */
    uint32_t device_tracer_level() const { return device_tracer_level_; }

    /**
     * @brief Sets the device tracer level
     * @param device_tracer_level The tracer level (0-3, higher means more detailed)
     */
    void set_device_tracer_level(uint32_t device_tracer_level)
    {
        device_tracer_level_ = device_tracer_level;
    }

    /**
     * @brief Gets the Python tracer level
     * @return The Python tracer level (0-3, higher means more detailed)
     */
    uint32_t python_tracer_level() const { return python_tracer_level_; }

    /**
     * @brief Sets the Python tracer level
     * @param python_tracer_level The tracer level (0-3, higher means more detailed)
     */
    void set_python_tracer_level(uint32_t python_tracer_level)
    {
        python_tracer_level_ = python_tracer_level;
    }

    /**
     * @brief Gets the profiling start timestamp in nanoseconds
     * @return The start timestamp in nanoseconds since epoch
     */
    uint64_t start_timestamp_ns() const { return start_timestamp_ns_; }

    /**
     * @brief Sets the profiling start timestamp in nanoseconds
     * @param start_timestamp_ns The start timestamp in nanoseconds since epoch
     */
    void set_start_timestamp_ns(uint64_t start_timestamp_ns)
    {
        start_timestamp_ns_ = start_timestamp_ns;
    }

private:
    // Member variables
    uint32_t         version_             = 5;
    device_type_enum device_type_         = device_type_enum::UNSPECIFIED;
    uint32_t         host_tracer_level_   = 2;
    uint32_t         device_tracer_level_ = 3;
    uint32_t         python_tracer_level_ = 0;
    uint64_t         start_timestamp_ns_  = 0;
};

}  // namespace profiler