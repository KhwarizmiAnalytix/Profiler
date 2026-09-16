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
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/profiler_export.h"

namespace profiler
{

/**
 * Device activity kinds collected by an instrumentation capture.
 * The compiled backend (Kineto or ITT) interprets these; CUDA requires a
 * Kineto build with CUPTI. Metal support was removed; numeric identities of
 * the retained values are preserved.
 */
enum class activity
{
    cpu  = 0,
    cuda = 1,
    hip  = 2,
};

/**
 * Instrumentation backend used by @ref capture.
 * `automatic` selects the backend this library was built with (`PROFILER_BACKEND`).
 * NVTX is a runtime instrumentation state, not a CMake backend.
 */
enum class capture_backend
{
    automatic,
    kineto,
    itt,
    nvtx,
    kineto_gpu_fallback,
};

struct capture_config
{
    capture_backend    backend             = capture_backend::automatic;
    std::set<activity> activities          = {activity::cpu};
    bool               profile_memory      = false;
    bool               with_stack          = false;
    bool               report_input_shapes = false;
    bool               with_flops          = false;
    bool               with_modules        = false;
};

struct capture_event
{
    std::string                                  name;
    uint64_t                                     start_ns    = 0;
    uint64_t                                     duration_ns = 0;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<std::string>                     stack;
};

/**
 * Result of an instrumentation capture. On Kineto builds, @ref save writes
 * Chrome Trace JSON for Perfetto or HTA. ITT/NVTX captures have no file trace;
 * @ref save returns false.
 */
class PROFILER_VISIBILITY capture_result
{
public:
    PROFILER_API capture_result();
    PROFILER_API ~capture_result();
    PROFILER_API                 capture_result(capture_result&& other) noexcept;
    PROFILER_API capture_result& operator=(capture_result&& other) noexcept;
    capture_result(const capture_result&)            = delete;
    capture_result& operator=(const capture_result&) = delete;

    PROFILER_API bool save(const std::string& path);

    // True when this capture produced a real, savable trace object at all
    // (as opposed to save() failing on a genuine write error for a backend
    // that did). ITT/NVTX/PRIVATEUSE1 captures have no trace object and
    // return false here unconditionally, distinct from a Kineto trace whose
    // save() call failed. See session::write_trace()'s use of this.
    PROFILER_API bool has_trace() const;

    uint64_t start_ns() const { return start_ns_; }

    const std::vector<capture_event>& events() const { return events_; }

private:
    friend class capture;
    class impl;
    std::unique_ptr<impl>      impl_;
    uint64_t                   start_ns_ = 0;
    std::vector<capture_event> events_;
};

/**
 * Backend-agnostic instrumentation session. Annotate work with
 * `PROFILER_SCOPE` / `PROFILER_FUNCTION` / `PROFILER_OP`. The Kineto, ITT,
 * and NVTX implementations stay inside the Profiler library.
 */
class PROFILER_VISIBILITY capture
{
public:
    PROFILER_API capture();
    PROFILER_API explicit capture(capture_config config);
    PROFILER_API ~capture();
    capture(const capture&)                         = delete;
    capture&              operator=(const capture&) = delete;
    PROFILER_API          capture(capture&& other) noexcept;
    PROFILER_API capture& operator=(capture&& other) noexcept;

    PROFILER_API bool prepare();
    PROFILER_API bool start();
    PROFILER_API std::unique_ptr<capture_result> stop();

    bool is_active() const { return active_; }

    PROFILER_API static bool enabled_in_main_thread();
    PROFILER_API static void enable_in_child_thread();
    PROFILER_API static void disable_in_child_thread();

    PROFILER_API static void start_memory_profile();
    PROFILER_API static void stop_memory_profile();
    PROFILER_API static void export_memory_profile(const std::string& path);

private:
    capture_config config_{};
    bool           prepared_ = false;
    bool           active_   = false;
};

/** Enroll this worker in the parent instrumentation capture (RAII). */
class PROFILER_VISIBILITY child_thread_capture
{
public:
    PROFILER_API child_thread_capture();
    PROFILER_API ~child_thread_capture();
    child_thread_capture(const child_thread_capture&)            = delete;
    child_thread_capture& operator=(const child_thread_capture&) = delete;
};

PROFILER_API void add_metadata_json(const std::string& key, const std::string& json);
PROFILER_API void mark_profiler_step();

PROFILER_API bool kineto_enabled();
PROFILER_API bool itt_enabled();
PROFILER_API bool cuda_enabled();
PROFILER_API bool nvtx_enabled();

}  // namespace profiler
