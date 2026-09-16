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

#include "common/backend_capabilities.h"
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
    opts.enable_statistical_analysis_   = options.statistical_analysis;
    return opts;
}

capture_config to_capture_config(const session_options& options)
{
    capture_config cfg;
    cfg.backend             = options.backend;
    cfg.activities          = options.activities;
    cfg.profile_memory      = options.profile_memory;
    cfg.with_stack          = options.with_stack;
    cfg.report_input_shapes = options.report_input_shapes;
    cfg.with_flops          = options.with_flops;
    cfg.with_modules        = options.with_modules;
    return cfg;
}

// Builds the same flat capture_event list write_chrome_trace()/generate_chrome_trace_json()
// already expose from XSpace (see export_to_chrome_trace_json()'s equivalent iteration in
// native/exporters/chrome_trace_exporter.cpp, which this mirrors), so events() has a real
// answer for a native-only session (instrumentation=false) instead of always being empty.
// `stack` is left empty for XSpace-sourced events: XSpace doesn't carry a call stack in the
// same representation the Kineto path's with_stack source locations do -- a known,
// backend-dependent detail gap, not fabricated data.
std::vector<capture_event> events_from_xspace(const x_space& space)
{
    std::vector<capture_event> events;
    for (const auto& plane : space.planes())
    {
        const auto& event_metadata_map = plane.event_metadata();
        const auto& stat_metadata_map  = plane.stat_metadata();
        for (size_t line_idx = 0; line_idx < plane.lines_size(); ++line_idx)
        {
            const auto& line = plane.lines(line_idx);
            for (const auto& event : line.events())
            {
                capture_event out;
                if (auto it = event_metadata_map.find(event.metadata_id());
                    it != event_metadata_map.end())
                {
                    out.name = it->second.name();
                }
                out.start_ns =
                    static_cast<uint64_t>(line.timestamp_ns()) +
                    static_cast<uint64_t>(event.offset_ps()) / 1000;
                out.duration_ns = static_cast<uint64_t>(event.duration_ps()) / 1000;
                for (const auto& stat : event.stats())
                {
                    std::string stat_name = "stat_" + std::to_string(stat.metadata_id());
                    if (auto it = stat_metadata_map.find(stat.metadata_id());
                        it != stat_metadata_map.end())
                    {
                        stat_name = it->second.name();
                    }
                    std::string value;
                    switch (stat.value_case())
                    {
                    case xstat::value_case_type::kInt64Value:
                        value = std::to_string(stat.int64_value());
                        break;
                    case xstat::value_case_type::kUint64Value:
                        value = std::to_string(stat.uint64_value());
                        break;
                    case xstat::value_case_type::kDoubleValue:
                        value = std::to_string(stat.double_value());
                        break;
                    case xstat::value_case_type::kStrValue:
                        value = stat.str_value();
                        break;
                    case xstat::value_case_type::kRefValue:
                        value = std::to_string(stat.ref_value());
                        break;
                    default:
                        break;
                    }
                    out.metadata.emplace(std::move(stat_name), std::move(value));
                }
                events.push_back(std::move(out));
            }
        }
    }
    return events;
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
      xspace_events_cache_(std::move(other.xspace_events_cache_)),
      xspace_events_cached_(other.xspace_events_cached_),
      last_error_(std::move(other.last_error_)), active_(other.active_)
{
    other.xspace_events_cached_ = false;
    other.active_               = false;
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
    options_               = std::move(other.options_);
    native_                = std::move(other.native_);
    inst_                  = std::move(other.inst_);
    inst_result_           = std::move(other.inst_result_);
    xspace_events_cache_   = std::move(other.xspace_events_cache_);
    xspace_events_cached_  = other.xspace_events_cached_;
    last_error_            = std::move(other.last_error_);
    active_                = other.active_;
    other.xspace_events_cached_ = false;
    other.active_                = false;
    return *this;
}

bool session::start()
{
    if (active_)
    {
        return false;
    }

    // Drop the previous run's instrumentation result so events()/write_trace() during
    // (or after) this new run can't observe stale data left over from the last one.
    inst_result_.reset();
    xspace_events_cache_.clear();
    xspace_events_cached_ = false;
    last_error_.clear();

    if (options_.instrumentation && options_.policy == capture_policy::required)
    {
        const backend_capabilities& caps = discover_backend_capabilities();
        for (activity act : options_.activities)
        {
            if (caps.supports(act))
            {
                continue;
            }
            switch (act)
            {
            case activity::cpu:
                last_error_ = "required activity unavailable: cpu";
                break;
            case activity::cuda:
                last_error_ = "required activity unavailable: cuda (no CUDA-capable device found, "
                              "or PROFILER_GPU_BACKEND != cuda)";
                break;
            case activity::hip:
                last_error_ = "required activity unavailable: hip (no HIP-capable device found, "
                              "or PROFILER_GPU_BACKEND != hip)";
                break;
            }
            return false;
        }
    }

    bool started_any = false;
    if (options_.native)
    {
        // A fresh profiler_session every start(), not reused -- any report/hotspot
        // report generated from a previous run keeps its own snapshot alive
        // (see generate_report()/generate_hotspot_report()), so replacing native_
        // here doesn't dangle them.
        native_ = std::make_shared<profiler_session>(to_native_options(options_));
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
            if (options_.backend != capture_backend::automatic)
            {
                if (native_ && native_->is_active())
                {
                    (void)native_->stop();
                }
                native_.reset();
                return false;
            }
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

    last_error_.clear();
    bool ok = true;
    if (inst_ && inst_->is_active())
    {
        inst_result_ = inst_->stop();
    }
    if (native_ && native_->is_active())
    {
        ok = native_->stop();
        if (!ok)
        {
            last_error_ = native_->last_error();
        }
    }
    active_ = false;
    return ok;
}

bool session::is_active() const
{
    return active_;
}

const std::string& session::last_error() const
{
    return last_error_;
}

bool session::write_trace(const std::string& path)
{
    if (inst_result_ && inst_result_->has_trace())
    {
        // A real Kineto trace exists for this capture: its export is
        // authoritative. Do not fall back to a different backend/format on
        // save failure -- design-review.md section 6.7 forbids switching
        // export format on I/O error, since a reader could otherwise get
        // native-XSpace data at a path that was supposed to be a Kineto
        // trace.
        return inst_result_->save(path);
    }
    // No Kineto trace object at all (ITT/NVTX/PRIVATEUSE1 captures, or no
    // instrumentation backend ran) -- native Chrome-trace is the only export
    // this capture ever had, not a downgrade from a failed one.
    return native_ && native_->write_chrome_trace(path);
}

bool session::write_kineto_hta_trace(const std::string& path)
{
    if (inst_result_ && inst_result_->has_trace())
    {
        return inst_result_->save(path);
    }
    // Unlike write_trace(), there is no fallback here: a native XSpace-
    // derived Chrome Trace is not HTA-compatible (docs/hta.md), so silently
    // writing one at a path the caller asked for an HTA-compatible trace at
    // would be the same "format switch" section 6.7 forbids for write_trace(),
    // just with a worse failure mode (a file HTA can't parse correctly
    // instead of a build error).
    return false;
}

bool session::write_chrome_trace(const std::string& path) const
{
    return native_ && native_->write_chrome_trace(path);
}

std::string session::generate_chrome_trace_json() const
{
    return native_ ? native_->generate_chrome_trace_json() : std::string{};
}

std::shared_ptr<profiler_report> session::generate_report() const
{
    if (!native_)
    {
        return nullptr;
    }
    std::unique_ptr<profiler_report> report = native_->generate_report();
    if (!report)
    {
        return nullptr;
    }
    // The report holds a reference into *native_; keep that native_session alive
    // for as long as the report is, even if this session's native_ is later
    // replaced (start()) or the session itself is destroyed.
    std::shared_ptr<profiler_session> keep_alive = native_;
    return std::shared_ptr<profiler_report>(
        report.release(), [keep_alive](profiler_report* p) { delete p; });
}

std::shared_ptr<hotspot_report> session::generate_hotspot_report() const
{
    if (!native_)
    {
        return nullptr;
    }
    std::unique_ptr<hotspot_report> report = native_->generate_hotspot_report();
    if (!report)
    {
        return nullptr;
    }
    // Same rationale as generate_report(): the hotspot tree is owned by *native_.
    std::shared_ptr<profiler_session> keep_alive = native_;
    return std::shared_ptr<hotspot_report>(
        report.release(), [keep_alive](hotspot_report* p) { delete p; });
}

void session::export_report(const std::string& path) const
{
    if (native_)
    {
        native_->export_report(path);
    }
}

memory_tracker* session::get_memory_tracker()
{
    return native_ ? native_->get_memory_tracker() : nullptr;
}

const std::vector<capture_event>& session::events() const
{
    if (inst_result_)
    {
        return inst_result_->events();
    }
    if (!xspace_events_cached_)
    {
        xspace_events_cache_ = (native_ && native_->has_collected_xspace())
                                    ? events_from_xspace(native_->collected_xspace())
                                    : std::vector<capture_event>{};
        xspace_events_cached_ = true;
    }
    return xspace_events_cache_;
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
