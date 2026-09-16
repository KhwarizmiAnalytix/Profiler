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

/**
 * @file profiler.cpp
 * @brief Implementation of the enhanced profiler system for Profiler applications
 *
 * Provides high-performance, thread-safe profiling capabilities with comprehensive
 * timing, memory tracking, and statistical analysis features.
 *
 * @author Profiler Development Team
 * @version 1.0
 * @date 2024
 */

#include "profiler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

#include "common/profiler_macros.h"
////#include "logger/logger.h"
#include "native/analysis/statistical_analyzer.h"
#include "native/core/profiler_collection.h"
#include "native/core/profiler_factory.h"
#include "native/cpu/annotation_stack.h"
#include "native/exporters/chrome_trace_exporter.h"
#include "native/exporters/xplane/xplane_schema.h"
#include "native/exporters/xplane/xplane_utils.h"
#include "native/memory/memory_tracker.h"
#include "native/session/profiler_report.h"
#include "native/session/scope_tree_builder.h"
#include "native/tracing/traceme_recorder.h"

// Prevent Windows min/max macros from interfering with std::numeric_limits
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <limits>

namespace profiler
{
// Static current session management with atomic operations for thread safety
static std::atomic<profiler::profiler_session*> g_current_session{nullptr};

//=============================================================================
// timing_stats Implementation
//=============================================================================

void timing_stats::add_sample(double time_ms)
{
    min_time_ = std::min(time_ms, min_time_);
    max_time_ = std::max(time_ms, max_time_);
    total_time_ += time_ms;
    ++sample_count_;
    samples_.push_back(time_ms);
}

void timing_stats::calculate_statistics(bool include_percentiles)
{
    if (sample_count_ == 0)
    {
        return;
    }

    mean_time_ = total_time_ / sample_count_;

    // Compute variance/standard deviation using collected samples
    double variance_sum = 0.0;
    for (double const sample : samples_)
    {
        double const diff = sample - mean_time_;
        variance_sum += diff * diff;
    }
    // sample_count_ is guaranteed > 0 here due to check at line 346
    // cppcheck-suppress knownConditionTrueFalse
    std_deviation_ = sample_count_ > 0 ? std::sqrt(variance_sum / sample_count_) : 0.0;

    // Optionally compute percentiles (25th, 50th, 75th, 90th, 95th, 99th)
    percentiles_.clear();
    if (include_percentiles)
    {
        std::array<double, 6> percentile_targets = {25.0, 50.0, 75.0, 90.0, 95.0, 99.0};
        std::vector<double>   sorted_samples     = samples_;
        std::sort(sorted_samples.begin(), sorted_samples.end());
        percentiles_.assign(percentile_targets.begin(), percentile_targets.end());
        for (size_t i = 0; i < percentile_targets.size(); ++i)
        {
            double const percentile = percentile_targets[i];
            if (sorted_samples.empty())
            {
                percentiles_[i] = 0.0;
                continue;
            }
            double const index = (percentile / 100.0) * (sorted_samples.size() - 1);
            auto const   lower = static_cast<size_t>(std::floor(index));
            auto const   upper = static_cast<size_t>(std::ceil(index));
            if (lower == upper)
            {
                percentiles_[i] = sorted_samples[lower];
            }
            else
            {
                double const weight = index - lower;
                percentiles_[i] =
                    (sorted_samples[lower] * (1.0 - weight)) + (sorted_samples[upper] * weight);
            }
        }
    }
}

void timing_stats::reset()
{
    min_time_      = (std::numeric_limits<double>::max)();
    max_time_      = 0.0;
    total_time_    = 0.0;
    mean_time_     = 0.0;
    std_deviation_ = 0.0;
    sample_count_  = 0;
    percentiles_.clear();
    samples_.clear();
}

//=============================================================================
// profiler_scope_data Implementation
//=============================================================================

double profiler_scope_data::get_duration_ns() const
{
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time_ - start_time_);
    return static_cast<double>(duration.count());
}

double profiler_scope_data::get_duration_us() const
{
    // Derive from the nanosecond value rather than a separate
    // duration_cast<microseconds> -- that truncated to a whole microsecond
    // before this function's own double conversion, discarding sub-
    // microsecond precision get_duration_ns() already preserves
    // (design-review.md finding 10 / section 6.4 bullet 3).
    return get_duration_ns() / 1000.0;
}

double profiler_scope_data::get_duration_ms() const
{
    return get_duration_ns() / 1000000.0;
}

//=============================================================================
// profiler_session Implementation
//=============================================================================

profiler_session::profiler_session() : profiler_session(profiler::profiler_options{}) {}

profiler_session::profiler_session(profiler::profiler_options options)
    : options_(std::move(options))
{
    initialize_components();
}

profiler_session::~profiler_session()
{
    if (active_.load())
    {
        stop();
    }
    cleanup_components();
}

bool profiler_session::start()
{
    if (active_.exchange(true))
    {
        return false;  // Already active
    }

    auto maybe_lock = ProfilerLock::Acquire();
    if (!maybe_lock.has_value())
    {
        active_.store(false);
        return false;
    }
    profiler_lock_ = std::move(*maybe_lock);

    backend_profile_options_ = build_backend_profile_options();
    auto const start_ns      = static_cast<uint64_t>(get_current_time_nanos());
    backend_profile_options_.set_start_timestamp_ns(start_ns);
    start_time_ns_ = start_ns;
    xspace_ready_  = false;
    scope_tree_cache_.reset();  // stale relative to the XSpace this run will collect

    auto profilers = profiler::create_profilers(backend_profile_options_);
    if (!profilers.empty())
    {
        backend_profilers_ = std::make_unique<profiler_collection>(std::move(profilers));
        profiler_status const backend_status = backend_profilers_->start();
        if (!backend_status.ok())
        {
            PROFILER_LOG_ERROR(
                "Failed to start one or more profiler backends: {}", backend_status.message());
            backend_profilers_.reset();
            profiler_lock_.ReleaseIfActive();
            active_.store(false);
            return false;
        }
    }
    else
    {
        backend_profilers_.reset();
    }

    start_time_ = profiler::steady_clock_t::now();

    // Start memory tracking
    if (options_.enable_memory_tracking_ && memory_tracker_)
    {
        memory_tracker_->start_tracking();
    }

    // Start statistical analysis
    if (options_.enable_statistical_analysis_ && statistical_analyzer_)
    {
        statistical_analyzer_->start_analysis();
    }

    set_current_session(this);
    generation_.fetch_add(1, std::memory_order_relaxed);

    return true;
}

bool profiler_session::stop()
{
    if (!active_.exchange(false))
    {
        return false;  // Not active
    }

    end_time_    = profiler::steady_clock_t::now();
    end_time_ns_ = static_cast<uint64_t>(get_current_time_nanos());

    // Stop memory tracking
    if (options_.enable_memory_tracking_ && memory_tracker_)
    {
        memory_tracker_->stop_tracking();
    }

    // Stop statistical analysis
    if (options_.enable_statistical_analysis_ && statistical_analyzer_)
    {
        statistical_analyzer_->stop_analysis();
    }

    last_error_.clear();
    bool backend_ok = true;

    if (backend_profilers_)
    {
        std::string           backend_errors;
        profiler_status const stop_status = backend_profilers_->stop();
        if (!stop_status.ok() && !stop_status.message().empty())
        {
            backend_errors = stop_status.message();
        }

        x_space collected_space;
        xspace_                              = x_space();
        profiler_status const collect_status = backend_profilers_->collect_data(&collected_space);
        if (collect_status.ok())
        {
            xspace_ = std::move(collected_space);
            normalize_xspace(&xspace_);
            if (options_.enable_gpu_tracing_)
            {
                auto* planes = xspace_.mutable_planes();
                for (auto& plane : *planes)
                {
                    AddFlowsToXplane(
                        static_cast<int32_t>(plane.id()),
                        IsHostPlane(plane),
                        /*connect_traceme=*/true,
                        &plane);
                }
            }
        }
        else
        {
            if (!collect_status.message().empty())
            {
                if (!backend_errors.empty())
                {
                    backend_errors.append("\n");
                }
                backend_errors.append(collect_status.message());
            }
        }
        xspace_ready_ = collect_status.ok();
        backend_ok    = stop_status.ok() && collect_status.ok();

        if (!backend_errors.empty())
        {
            last_error_ = backend_errors;
            PROFILER_LOG_ERROR("Profiler backend errors: {}", backend_errors);
        }

        backend_profilers_.reset();
    }
    else
    {
        xspace_ready_ = false;
    }

    if (current_session() == this)
    {
        set_current_session(nullptr);
    }

    profiler_lock_.ReleaseIfActive();

    return backend_ok;
}

std::unique_ptr<profiler::profiler_scope> profiler_session::create_scope(const std::string& name)
{
    return std::make_unique<profiler::profiler_scope>(name, this);
}

std::unique_ptr<profiler::profiler_report> profiler_session::generate_report() const
{
    return std::make_unique<profiler::profiler_report>(*this);
}

std::unique_ptr<profiler::hotspot_report> profiler_session::generate_hotspot_report() const
{
    return std::make_unique<profiler::hotspot_report>(build_scope_tree_shared());
}

void profiler_session::export_report(const std::string& filename) const
{
    auto report = generate_report();
    report->export_to_file(filename, options_.output_format_);
}

void profiler_session::print_report() const
{
    auto report = generate_report();
    report->print_detailed_report();
}

profiler_session* profiler_session::current_session()
{
    return g_current_session.load();
}

void profiler_session::set_current_session(profiler::profiler_session* session)
{
    g_current_session.store(session);
}

void profiler_session::initialize_components()
{
    if (options_.enable_memory_tracking_)
    {
        memory_tracker_ = std::make_unique<profiler::memory_tracker>();
    }

    if (options_.enable_statistical_analysis_)
    {
        statistical_analyzer_ = std::make_unique<profiler::statistical_analyzer>();
        statistical_analyzer_->set_max_samples_per_series(options_.max_samples_);
        statistical_analyzer_->set_worker_threads_hint(options_.thread_pool_size_);
    }

    backend_profile_options_ = build_backend_profile_options();
}

void profiler_session::cleanup_components()
{
    memory_tracker_.reset();  //NOLINT
    memory_tracker_ = nullptr;

    statistical_analyzer_.reset();  //NOLINT

    backend_profilers_.reset();
    profiler_lock_.ReleaseIfActive();
    xspace_        = x_space();
    xspace_ready_  = false;
    start_time_ns_ = 0;
    end_time_ns_   = 0;
    scope_tree_cache_.reset();
}

profile_options profiler_session::build_backend_profile_options() const
{
    profile_options opts;
    opts.set_version(5);
    opts.set_device_type(profile_options::device_type_enum::CPU);
    opts.set_include_dataset_ops(false);
    // host_tracer must run whenever hierarchical profiling is requested, not only when timing is
    // -- profiler_scope::start() backs every scope with a traceme_ event, and traceme events are
    // only recorded while traceme_recorder is active at this level (see host_tracer.cpp), so
    // host_tracer_level == 0 silently drops scope structure/memory capture too, independent of
    // enable_timing_.
    opts.set_host_tracer_level(
        (options_.enable_timing_ || options_.enable_hierarchical_profiling_) ? 2U : 0U);
    opts.set_device_tracer_level(options_.enable_gpu_tracing_ ? 1U : 0U);
    if (options_.enable_gpu_tracing_)
    {
        opts.set_device_type(profile_options::device_type_enum::GPU);
    }
    opts.set_python_tracer_level(0);
    opts.set_enable_hlo_proto(false);
    opts.set_duration_ms(0);
    return opts;
}

void profiler_session::normalize_xspace(x_space* space) const
{
    if (space == nullptr)
    {
        return;
    }
    // Use the shared XPlane helper — do not fork timestamp math here.
    NormalizeTimestamps(space, start_time_ns_);
    if (space->hostnames().empty())
    {
        space->add_hostname("localhost");
    }
}

const profiler::profiler_scope_data* profiler_session::build_scope_tree() const
{
    if (!xspace_ready_)
    {
        return nullptr;
    }
    if (!scope_tree_cache_)
    {
        scope_tree_cache_ = scope_tree_builder::build_scope_tree(xspace_);
    }
    return scope_tree_cache_.get();
}

std::shared_ptr<const profiler::profiler_scope_data> profiler_session::build_scope_tree_shared() const
{
    build_scope_tree();  // ensures scope_tree_cache_ is populated (or stays null)
    return scope_tree_cache_;
}

//=============================================================================
// profiler_scope Implementation
//=============================================================================

profiler_scope::profiler_scope(const std::string& name, profiler::profiler_session* session)
    : name_(name),
      session_((session != nullptr) ? session : profiler::profiler_session::current_session())
{
    // Auto-start if session is active.
    if ((session_ != nullptr) && session_->is_active())
    {
        start();
    }
}

profiler_scope::~profiler_scope()
{
    if (started_ && !stopped_)
    {
        stop();
    }
}

void profiler_scope::start()
{
    // Skip all work if no session, session isn't active, or hierarchical profiling disabled.
    // The is_active() check matters because traceme_recorder is a process-wide facility: without
    // it, a scope bound to a never-started (or already-stopped) session would still emplace a
    // real traceme_ event -- and get collected -- merely because some *other* session happens to
    // be active and recording.
    if (session_ == nullptr || !session_->is_active() ||
        !session_->options_.enable_hierarchical_profiling_)
    {
        return;
    }

    if (started_)
    {
        return;
    }

    started_    = true;
    start_time_ = profiler::steady_clock_t::now();

    // Back this scope with a real traceme event -- this is what host_tracer reads from,
    // so PROFILER_PROFILE_SCOPE rides the same lock-free, thread-local recording path as
    // every other native/ instrumentation call site instead of tracking its own tree.
    // session_ is guaranteed non-null here due to check above
    // cppcheck-suppress knownConditionTrueFalse
    if (session_ != nullptr)
    {
        traceme_.emplace(std::string_view(name_));

        if (profiler_impl::annotation_stack::is_enabled())
        {
            profiler_impl::annotation_stack::push_annotation(name_);
            pushed_gpu_annotation_ = true;
        }

        // Memory tracking/attribution is opt-in (session_options::memory_tracking):
        // skip both the debug-annotation allocation and the tracker snapshot on the
        // default preset, matching every other optional-detail feature.
        if (session_->options_.enable_memory_tracking_ && session_->memory_tracker_)
        {
            memory_annotation_      = std::make_unique<scoped_memory_debug_annotation>(name_.c_str());
            start_memory_stats_     = session_->memory_tracker_->get_current_stats();
            has_start_memory_stats_ = true;
        }
    }
}

void profiler_scope::stop()
{
    if (pushed_gpu_annotation_)
    {
        profiler_impl::annotation_stack::pop_annotation();
        pushed_gpu_annotation_ = false;
    }

    // Mirror start()'s guard: once the session has stopped, traceme_recorder is no longer active,
    // so traceme_.reset() below would silently drop this scope's event while the timing/memory
    // computation and statistical_analyzer_ recording further down would still run -- recording a
    // sample for an event that never actually landed in the trace. Bail out the same way start()
    // does instead of only checking enable_hierarchical_profiling_.
    if (session_ == nullptr || !session_->is_active() ||
        !session_->options_.enable_hierarchical_profiling_)
    {
        return;
    }

    if (!started_ || stopped_)
    {
        return;
    }

    stopped_             = true;
    auto const end_time  = profiler::steady_clock_t::now();
    // Preserve sub-microsecond precision through to this ms value rather than
    // truncating to a whole microsecond first (design-review.md finding 10).
    double const duration_ms =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_).count() /
        1000000.0;

    // session_ is guaranteed non-null here due to the early-return check above
    // cppcheck-suppress knownConditionTrueFalse
    if (session_ != nullptr)
    {
        // Only computed to feed statistical_analyzer_ below -- nothing else reads a
        // per-scope memory snapshot after this point (profiler_report's memory section
        // reads memory_tracker_'s own live stats directly; see generate_memory_section()).
        int64_t delta_since_start  = 0;
        bool    have_current_stats = false;
        if (session_->options_.enable_memory_tracking_ && session_->memory_tracker_)
        {
            auto const current_stats = session_->memory_tracker_->get_current_stats();
            have_current_stats       = true;
            delta_since_start =
                session_->options_.track_memory_deltas_
                    ? (has_start_memory_stats_
                              ? static_cast<int64_t>(current_stats.current_usage_) -
                                    static_cast<int64_t>(start_memory_stats_.current_usage_)
                              : static_cast<int64_t>(current_stats.current_usage_))
                    : 0;
        }

        // Add timing/memory samples to the statistical analyzer, keyed by scope name. This is
        // this session's per-scope memory record now that the reconstructed report tree
        // (scope_tree_builder, built from XSpace) has no memory_stats_ of its own to carry --
        // XSpace events don't record memory deltas -- so profiler_report reads memory data from
        // here (by name) rather than from tree nodes; see generate_memory_section().
        if (session_->options_.enable_statistical_analysis_ && session_->statistical_analyzer_)
        {
            session_->statistical_analyzer_->add_timing_sample(name_, duration_ms);
            if (have_current_stats)
            {
                session_->statistical_analyzer_->add_memory_sample(
                    name_, static_cast<size_t>(std::abs(delta_since_start)));
            }
        }
    }

    // Ends the traceme event, recording it into traceme_recorder for host_tracer to collect.
    traceme_.reset();
    memory_annotation_.reset();
}

uint64_t profiler_session::dropped_event_count() const
{
    return traceme_recorder::dropped_event_count();
}

std::string profiler_session::generate_chrome_trace_json() const
{
    // No collected XSpace yet (e.g. called before stop()) -- an empty string signals "no data",
    // distinct from a valid-but-empty trace (which export_to_chrome_trace_json always produces
    // with a populated "traceEvents" key, even for a session with zero recorded scopes).
    if (!xspace_ready_)
    {
        return "";
    }
    return profiler_impl::export_to_chrome_trace_json(xspace_);
}

bool profiler_session::write_chrome_trace(const std::string& filename) const
{
    if (!xspace_ready_)
    {
        return false;
    }
    return profiler_impl::export_to_chrome_trace_json_file(xspace_, filename);
}

}  // namespace profiler
