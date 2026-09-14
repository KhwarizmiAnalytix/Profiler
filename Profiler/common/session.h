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

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/capture.h"
#include "common/profiler_export.h"

namespace profiler
{

class profiler_session;
class profiler_report;
class hotspot_report;
class memory_tracker;

/**
 * Unified client session. Starts the native collector and the compiled
 * instrumentation backend together. Annotate with PROFILER_SCOPE /
 * PROFILER_FUNCTION; do not select Kineto vs ITT.
 */
struct session_options
{
    bool               native          = true;
    bool               instrumentation = true;
    std::set<activity> activities      = {activity::cpu};
    bool               memory_tracking = false;
    bool               gpu_tracing     = false;
    bool               profile_memory  = false;
};

class PROFILER_VISIBILITY session
{
public:
    PROFILER_API session();
    PROFILER_API explicit session(session_options options);
    PROFILER_API ~session();
    session(const session&)                         = delete;
    session&              operator=(const session&) = delete;
    PROFILER_API          session(session&& other) noexcept;
    PROFILER_API session& operator=(session&& other) noexcept;

    PROFILER_API bool start();
    PROFILER_API bool stop();
    PROFILER_API bool is_active() const;

    /// Kineto JSON when that backend produced a trace; otherwise native Chrome Trace.
    PROFILER_API bool write_trace(const std::string& path);

    PROFILER_API bool write_chrome_trace(const std::string& path) const;
    PROFILER_API std::string generate_chrome_trace_json() const;

    PROFILER_API std::unique_ptr<profiler_report> generate_report() const;
    PROFILER_API std::unique_ptr<hotspot_report> generate_hotspot_report() const;
    PROFILER_API void                            export_report(const std::string& path) const;

    PROFILER_API memory_tracker& get_memory_tracker();

    PROFILER_API const std::vector<capture_event>& events() const;

    PROFILER_API static void enable_in_child_thread();
    PROFILER_API static void disable_in_child_thread();

private:
    session_options                   options_{};
    std::unique_ptr<profiler_session> native_;
    std::unique_ptr<capture>          inst_;
    std::unique_ptr<capture_result>   inst_result_;
    bool                              active_ = false;
};

}  // namespace profiler
