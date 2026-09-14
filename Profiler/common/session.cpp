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

#include "common/session.h"

#include <utility>

#include "native/analysis/hotspot_report.h"
#include "native/memory/memory_tracker.h"
#include "native/session/profiler.h"
#include "native/session/profiler_report.h"

namespace profiler
{
namespace
{

profiler_options to_native_options(const session_options& options)
{
    profiler_options opts;
    opts.enable_timing_                 = true;
    opts.enable_hierarchical_profiling_ = true;
    opts.enable_memory_tracking_        = options.memory_tracking;
    opts.enable_gpu_tracing_            = options.gpu_tracing;
    opts.enable_statistical_analysis_   = true;
    return opts;
}

capture_config to_capture_config(const session_options& options)
{
    capture_config cfg;
    cfg.backend        = capture_backend::automatic;
    cfg.activities     = options.activities;
    cfg.profile_memory = options.profile_memory;
    return cfg;
}

const std::vector<capture_event>& empty_events()
{
    static const std::vector<capture_event> kEmpty;
    return kEmpty;
}

}  // namespace

session::session() = default;

session::session(session_options options) : options_(std::move(options)) {}

session::~session()
{
    if (active_)
    {
        (void)stop();
    }
}

session::session(session&& other) noexcept
    : options_(std::move(other.options_)), native_(std::move(other.native_)),
      inst_(std::move(other.inst_)), inst_result_(std::move(other.inst_result_)),
      active_(other.active_)
{
    other.active_ = false;
}

session& session::operator=(session&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    if (active_)
    {
        (void)stop();
    }
    options_      = std::move(other.options_);
    native_       = std::move(other.native_);
    inst_         = std::move(other.inst_);
    inst_result_  = std::move(other.inst_result_);
    active_       = other.active_;
    other.active_ = false;
    return *this;
}

bool session::start()
{
    if (active_)
    {
        return false;
    }

    bool started_any = false;
    if (options_.native)
    {
        native_ = std::make_unique<profiler_session>(to_native_options(options_));
        if (!native_->start())
        {
            native_.reset();
            return false;
        }
        started_any = true;
    }
    if (options_.instrumentation)
    {
        inst_ = std::make_unique<capture>(to_capture_config(options_));
        if (inst_->start())
        {
            started_any = true;
        }
        else
        {
            inst_.reset();
        }
    }

    active_ = started_any;
    return active_;
}

bool session::stop()
{
    if (!active_)
    {
        return false;
    }

    bool ok = true;
    if (inst_ && inst_->is_active())
    {
        inst_result_ = inst_->stop();
    }
    if (native_ && native_->is_active())
    {
        ok = native_->stop();
    }
    active_ = false;
    return ok;
}

bool session::is_active() const
{
    return active_;
}

bool session::write_trace(const std::string& path)
{
    if (inst_result_ && inst_result_->save(path))
    {
        return true;
    }
    return native_ && native_->write_chrome_trace(path);
}

bool session::write_chrome_trace(const std::string& path) const
{
    return native_ && native_->write_chrome_trace(path);
}

std::string session::generate_chrome_trace_json() const
{
    return native_ ? native_->generate_chrome_trace_json() : std::string{};
}

std::unique_ptr<profiler_report> session::generate_report() const
{
    return native_ ? native_->generate_report() : nullptr;
}

std::unique_ptr<hotspot_report> session::generate_hotspot_report() const
{
    return native_ ? native_->generate_hotspot_report() : nullptr;
}

void session::export_report(const std::string& path) const
{
    if (native_)
    {
        native_->export_report(path);
    }
}

memory_tracker& session::memory_tracker()
{
    return native_->memory_tracker();
}

const std::vector<capture_event>& session::events() const
{
    return inst_result_ ? inst_result_->events() : empty_events();
}

void session::enable_in_child_thread()
{
    capture::enable_in_child_thread();
}

void session::disable_in_child_thread()
{
    capture::disable_in_child_thread();
}

}  // namespace profiler
