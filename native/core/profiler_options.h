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

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace profiler
{

/**
 * @brief Configuration options for profiling sessions
 *
 * Contains all configuration parameters needed to set up and control
 * profiling behavior across different device types and profiling modes.
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
     * @brief Gets whether to include dataset operations in profiling
     * @return true if dataset ops should be included, false otherwise
     */
    bool include_dataset_ops() const { return include_dataset_ops_; }

    /**
     * @brief Sets whether to include dataset operations in profiling
     * @param include_dataset_ops true to include dataset ops, false otherwise
     */
    void set_include_dataset_ops(bool include_dataset_ops)
    {
        include_dataset_ops_ = include_dataset_ops;
    }

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
     * @brief Gets whether HLO proto generation is enabled
     * @return true if HLO proto generation is enabled, false otherwise
     */
    bool enable_hlo_proto() const { return enable_hlo_proto_; }

    /**
     * @brief Sets whether to enable HLO proto generation
     * @param enable_hlo_proto true to enable HLO proto generation, false otherwise
     */
    void set_enable_hlo_proto(bool enable_hlo_proto) { enable_hlo_proto_ = enable_hlo_proto; }

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

    /**
     * @brief Gets the profiling duration in milliseconds
     * @return The profiling duration in milliseconds
     */
    uint64_t duration_ms() const { return duration_ms_; }

    /**
     * @brief Sets the profiling duration in milliseconds
     * @param duration_ms The profiling duration in milliseconds
     */
    void set_duration_ms(uint64_t duration_ms) { duration_ms_ = duration_ms; }

    /**
     * @brief Gets the repository path for profiling data
     * @return The repository path as a string
     */
    const std::string& repository_path() const { return repository_path_; }

    /**
     * @brief Sets the repository path for profiling data
     * @param repository_path The path where profiling data should be stored
     */
    void set_repository_path(const std::string& repository_path)
    {
        repository_path_ = repository_path;
    }

private:
    // Member variables
    uint32_t         version_             = 5;
    device_type_enum device_type_         = device_type_enum::UNSPECIFIED;
    bool             include_dataset_ops_ = false;
    uint32_t         host_tracer_level_   = 2;
    uint32_t         device_tracer_level_ = 3;
    uint32_t         python_tracer_level_ = 0;
    bool             enable_hlo_proto_    = false;
    uint64_t         start_timestamp_ns_  = 0;
    uint64_t         duration_ms_         = 0;
    std::string      repository_path_;
};

/**
 * @brief Options for remote profiler session management
 *
 * Contains configuration for managing remote profiling sessions across
 * multiple service addresses with timing and duration controls.
 */
class remote_profiler_session_manager_options
{
public:
    /**
     * @brief Gets the profiler options
     * @return Reference to the profile_options
     */
    const profile_options& get_profiler_options() const { return profiler_options_; }

    /**
     * @brief Sets the profiler options
     * @param profiler_options The profile_options to use
     */
    void set_profiler_options(const profile_options& profiler_options)
    {
        profiler_options_ = profiler_options;
    }

    /**
     * @brief Gets the list of service addresses
     * @return Vector of service address strings
     */
    const std::vector<std::string>& get_service_addresses() const { return service_addresses_; }

    /**
     * @brief Sets the list of service addresses
     * @param service_addresses Vector of service address strings
     */
    void set_service_addresses(const std::vector<std::string>& service_addresses)
    {
        service_addresses_ = service_addresses;
    }

    /**
     * @brief Adds a service address to the list
     * @param service_address The service address to add
     */
    void add_service_address(const std::string& service_address)
    {
        service_addresses_.push_back(service_address);
    }

    /**
     * @brief Gets the session creation timestamp in nanoseconds
     * @return The session creation timestamp in nanoseconds since epoch
     */
    uint64_t get_session_creation_timestamp_ns() const { return session_creation_timestamp_ns_; }

    /**
     * @brief Sets the session creation timestamp in nanoseconds
     * @param session_creation_timestamp_ns The timestamp in nanoseconds since epoch
     */
    void set_session_creation_timestamp_ns(uint64_t session_creation_timestamp_ns)
    {
        session_creation_timestamp_ns_ = session_creation_timestamp_ns;
    }

    /**
     * @brief Gets the maximum session duration in milliseconds
     * @return The maximum session duration in milliseconds
     */
    uint64_t get_max_session_duration_ms() const { return max_session_duration_ms_; }

    /**
     * @brief Sets the maximum session duration in milliseconds
     * @param max_session_duration_ms The maximum duration in milliseconds
     */
    void set_max_session_duration_ms(uint64_t max_session_duration_ms)
    {
        max_session_duration_ms_ = max_session_duration_ms;
    }

    /**
     * @brief Gets the delay before starting profiling in milliseconds
     * @return The delay in milliseconds
     */
    uint64_t get_delay_ms() const { return delay_ms_; }

    /**
     * @brief Sets the delay before starting profiling in milliseconds
     * @param delay_ms The delay in milliseconds
     */
    void set_delay_ms(uint64_t delay_ms) { delay_ms_ = delay_ms; }

private:
    // Member variables
    profile_options          profiler_options_;
    std::vector<std::string> service_addresses_;
    uint64_t                 session_creation_timestamp_ns_ = 0;
    uint64_t                 max_session_duration_ms_       = 0;
    uint64_t                 delay_ms_                      = 0;
};
}  // namespace profiler