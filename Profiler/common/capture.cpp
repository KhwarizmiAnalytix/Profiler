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

#include "common/capture.h"

#include <utility>

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include "bespoke/kineto/kineto_shim.h"
#include "bespoke/kineto/profiler_kineto.h"
#endif

namespace profiler
{
namespace
{

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT

using profiler::profiler_impl::ActivityType;
using profiler::profiler_impl::ProfilerConfig;
using profiler::profiler_impl::ProfilerState;

ProfilerState resolve_backend(capture_backend backend)
{
    switch (backend)
    {
    case capture_backend::kineto:
        return ProfilerState::KINETO;
    case capture_backend::itt:
        return ProfilerState::ITT;
    case capture_backend::nvtx:
        return ProfilerState::NVTX;
    case capture_backend::kineto_gpu_fallback:
        return ProfilerState::KINETO_GPU_FALLBACK;
    case capture_backend::automatic:
    default:
#if PROFILER_HAS_KINETO
        return ProfilerState::KINETO;
#else
        return ProfilerState::ITT;
#endif
    }
}

bool backend_available(capture_backend backend)
{
    switch (backend)
    {
    case capture_backend::kineto:
    case capture_backend::kineto_gpu_fallback:
        return PROFILER_HAS_KINETO != 0;
    case capture_backend::itt:
        return PROFILER_HAS_ITT != 0;
    case capture_backend::nvtx:
        return true;
    case capture_backend::automatic:
    default:
        return (PROFILER_HAS_KINETO != 0) || (PROFILER_HAS_ITT != 0);
    }
}

std::set<ActivityType> to_internal_activities(const std::set<activity>& activities)
{
    std::set<ActivityType> out;
    for (activity act : activities)
    {
        switch (act)
        {
        case activity::cpu:
            out.insert(ActivityType::CPU);
            break;
        case activity::cuda:
            out.insert(ActivityType::CUDA);
            break;
        case activity::hip:
            out.insert(ActivityType::HIP);
            break;
        case activity::metal:
            out.insert(ActivityType::Metal);
            break;
        }
    }
    if (out.empty())
    {
        out.insert(ActivityType::CPU);
    }
    return out;
}

ProfilerConfig make_config(const capture_config& config)
{
    return ProfilerConfig(resolve_backend(config.backend),
        config.report_input_shapes,
        config.profile_memory,
        config.with_stack,
        config.with_flops,
        config.with_modules);
}

#endif  // PROFILER_HAS_KINETO || PROFILER_HAS_ITT

}  // namespace

class capture_result::impl
{
public:
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    std::unique_ptr<profiler::profiler_impl::ProfilerResult> kineto;
#endif
};

capture_result::capture_result() : impl_(std::make_unique<impl>()) {}

capture_result::~capture_result() = default;

capture_result::capture_result(capture_result&& other) noexcept = default;

capture_result& capture_result::operator=(capture_result&& other) noexcept = default;

bool capture_result::save(const std::string& path)
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    return impl_ && impl_->kineto && impl_->kineto->save(path);
#else
    (void)path;
    return false;
#endif
}

capture::capture() = default;

capture::capture(capture_config config) : config_(std::move(config)) {}

capture::~capture()
{
    if (active_)
    {
        (void)stop();
    }
}

capture::capture(capture&& other) noexcept
    : config_(std::move(other.config_)), prepared_(other.prepared_), active_(other.active_)
{
    other.prepared_ = false;
    other.active_   = false;
}

capture& capture::operator=(capture&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    if (active_)
    {
        (void)stop();
    }
    config_         = std::move(other.config_);
    prepared_       = other.prepared_;
    active_         = other.active_;
    other.prepared_ = false;
    other.active_   = false;
    return *this;
}

bool capture::prepare()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    if (!backend_available(config_.backend))
    {
        return false;
    }
    profiler_impl::prepareProfiler(
        make_config(config_), to_internal_activities(config_.activities));

    prepared_ = true;
    return true;
#else
    return false;
#endif
}

bool capture::start()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    if (active_)
    {
        return false;
    }
    if (!backend_available(config_.backend))
    {
        return false;
    }
    if (!prepared_ && !prepare())
    {
        return false;
    }
    profiler_impl::enableProfiler(make_config(config_), to_internal_activities(config_.activities));

    active_ = true;
    return true;
#else
    return false;
#endif
}

std::unique_ptr<capture_result> capture::stop()
{
    auto result = std::make_unique<capture_result>();
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    if (!active_)
    {
        prepared_ = false;
        return result;
    }
    auto kineto = profiler_impl::disableProfiler();
    if (kineto)
    {
        result->start_ns_ = kineto->trace_start_ns();
        result->events_.reserve(kineto->events().size());
        for (const auto& event : kineto->events())
        {
            capture_event copied;
            copied.name        = event.name();
            copied.start_ns    = event.startNs();
            copied.duration_ns = event.durationNs();
            copied.metadata    = event.extraMeta();
            copied.stack       = event.stack();
            result->events_.push_back(std::move(copied));
        }
        result->impl_->kineto = std::move(kineto);
    }
    active_   = false;
    prepared_ = false;
#else
    active_   = false;
    prepared_ = false;
#endif
    return result;
}

bool capture::enabled_in_main_thread()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    return profiler_impl::isProfilerEnabledInMainThread();
#else
    return false;
#endif
}

void capture::enable_in_child_thread()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::enableProfilerInChildThread();
#endif
}

void capture::disable_in_child_thread()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::disableProfilerInChildThread();
#endif
}

void capture::start_memory_profile()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::startMemoryProfile();
#endif
}

void capture::stop_memory_profile()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::stopMemoryProfile();
#endif
}

void capture::export_memory_profile(const std::string& path)
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::exportMemoryProfile(path);
#else
    (void)path;
#endif
}

child_thread_capture::child_thread_capture()
{
    capture::enable_in_child_thread();
}

child_thread_capture::~child_thread_capture()
{
    capture::disable_in_child_thread();
}

void add_metadata_json(const std::string& key, const std::string& json)
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::addMetadataJson(key, json);
#else
    (void)key;
    (void)json;
#endif
}

void mark_profiler_step()
{
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
    profiler_impl::profilerStep();
#endif
}

bool kineto_enabled()
{
    return PROFILER_HAS_KINETO != 0;
}

bool itt_enabled()
{
    return PROFILER_HAS_ITT != 0;
}

bool cuda_enabled()
{
    return PROFILER_HAS_CUDA != 0;
}

bool nvtx_enabled()
{
    return PROFILER_HAS_NVTX != 0;
}

}  // namespace profiler
