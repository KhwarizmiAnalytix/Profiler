#include "bespoke/common/orchestration/observer.h"

#include <utility>

#include "bespoke/base/thread_local_debug_info.h"
#include "bespoke/common/util.h"

namespace profiler::profiler_impl::impl
{

using GlobalManager = GlobalStateManager<ProfilerStateBase>;

// ----------------------------------------------------------------------------
// -- Profiler Config ---------------------------------------------------------
// ----------------------------------------------------------------------------
ExperimentalConfig::ExperimentalConfig(
    std::vector<std::string> profiler_metrics,
    bool                     profiler_measure_per_kernel,
    bool                     verbose,
    std::vector<std::string> performance_events,
    bool                     enable_cuda_sync_events,
    bool                     adjust_profiler_step,
    bool                     disable_external_correlation,
    bool                     profile_all_threads,
    bool                     capture_overload_names,
    bool                     record_python_gc_info,
    bool                     expose_kineto_event_metadata,
    std::string              custom_profiler_config,
    bool                     adjust_timestamps)
    : profiler_metrics{std::move(profiler_metrics)},
      profiler_measure_per_kernel{profiler_measure_per_kernel},
      verbose{verbose},
      performance_events(std::move(performance_events)),
      enable_cuda_sync_events{enable_cuda_sync_events},
      adjust_profiler_step{adjust_profiler_step},
      disable_external_correlation{disable_external_correlation},
      profile_all_threads{profile_all_threads},
      capture_overload_names{capture_overload_names},
      record_python_gc_info{record_python_gc_info},
      expose_kineto_event_metadata{expose_kineto_event_metadata},
      custom_profiler_config(std::move(custom_profiler_config)),
      adjust_timestamps{adjust_timestamps}
{
}

/*explicit*/ ExperimentalConfig::operator bool() const
{
    return !profiler_metrics.empty();
}

ProfilerConfig::ProfilerConfig(
    ProfilerState      state,
    bool               report_input_shapes,
    bool               profile_memory,
    bool               with_stack,
    bool               with_flops,
    bool               with_modules,
    ExperimentalConfig experimental_config,
    std::string        trace_id)
    : state{state},
      experimental_config{std::move(experimental_config)},
      report_input_shapes{report_input_shapes},
      profile_memory{profile_memory},
      with_stack{with_stack},
      with_flops{with_flops},
      with_modules{with_modules},
      trace_id{std::move(trace_id)}
{
}

bool ProfilerConfig::disabled() const
{
    return state == profiler::profiler_impl::impl::ProfilerState::Disabled;
}

bool ProfilerConfig::global() const
{
    return state == profiler::profiler_impl::impl::ProfilerState::KINETO_ONDEMAND;
}

bool ProfilerConfig::pushGlobalCallbacks() const
{
    return global() || experimental_config.profile_all_threads;
}

// ----------------------------------------------------------------------------
// -- Profiler base class -----------------------------------------------------
// ----------------------------------------------------------------------------
/*explicit*/ ProfilerStateBase::ProfilerStateBase(ProfilerConfig config)
    : MemoryReportingInfoBase(), config_(std::move(config))
{
}

ProfilerStateBase::~ProfilerStateBase()
{
    if (handle_ != 0u)
    {
        auto handle [[maybe_unused]] = handle_;  // Used in SOFT_ASSERT
        removeCallback();
        SOFT_ASSERT(false, "Leaked callback handle: ", handle);
    }
}

/*static*/ ProfilerStateBase* ProfilerStateBase::get(bool global)
{
    auto* out = global ? GlobalManager::get()
                       : static_cast<ProfilerStateBase*>(profiler::thread_local_debug_info::get(
                             profiler::DebugInfoKind::PROFILER_STATE));
    return out;
}

/*static*/ void ProfilerStateBase::push(std::shared_ptr<ProfilerStateBase>&& state)
{
    // PROFILER_CHECK(state != nullptr);
    if (state->config().pushGlobalCallbacks())
    {
        GlobalManager::push(std::move(state));
    }
    else
    {
        profiler::thread_local_debug_info::_push(profiler::DebugInfoKind::PROFILER_STATE, state);
    }
}

namespace
{
std::shared_ptr<ProfilerStateBase> popTLS()
{
    // If there is no active thread local profiler then we simply return null.
    // However if there is an active profiler but it is not the top
    // `DebugInfoBase`then `profiler::thread_local_debug_info::_pop` will throw.
    // TODO(robieta): make `noexcept` version.
    return (profiler::thread_local_debug_info::get(profiler::DebugInfoKind::PROFILER_STATE) !=
            nullptr)
               ? std::static_pointer_cast<ProfilerStateBase>(
                     profiler::thread_local_debug_info::_pop(
                         profiler::DebugInfoKind::PROFILER_STATE))
               : nullptr;
}
}  // namespace

/*static*/ std::shared_ptr<ProfilerStateBase> ProfilerStateBase::pop(bool global)
{
    auto out = global ? GlobalManager::pop() : popTLS();
    return out;
}

void ProfilerStateBase::setCallbackHandle(profiler::CallbackHandle handle)
{
    if (handle_ != 0u)
    {
        profiler::removeCallback(handle_);
        SOFT_ASSERT(
            false,
            "ProfilerStateBase already has a registered callback. "
            "Removing to avoid leaked callback.");
    }

    handle_ = handle;
}

void ProfilerStateBase::removeCallback()
{
    if (handle_ != 0u)
    {
        profiler::removeCallback(handle_);
        handle_ = 0;
    }
}

bool profilerEnabled()
{
    auto* state_ptr = ProfilerStateBase::get(/*global=*/false);
    return (state_ptr != nullptr) && !state_ptr->config().disabled();
}

ActiveProfilerType profilerType()
{
    auto* state_ptr = ProfilerStateBase::get(/*global=*/false);
    return state_ptr == nullptr ? ActiveProfilerType::NONE : state_ptr->profilerType();
}

profiler::profiler_impl::impl::ProfilerConfig getProfilerConfig()
{
    auto* state_ptr = ProfilerStateBase::get(/*global=*/false);
    // PROFILER_CHECK(state_ptr, "Tried to access profiler config, but profiler is not enabled!");
    return state_ptr->config();
}

}  // namespace profiler::profiler_impl::impl
