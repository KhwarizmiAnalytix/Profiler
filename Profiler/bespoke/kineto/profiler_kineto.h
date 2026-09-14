#pragma once

#include <set>
#include <string>
#include <vector>

#include "bespoke/base/base.h"
#include "bespoke/common/api.h"
#include "bespoke/common/events.h"
#include "bespoke/common/util.h"
#include "common/array_ref.h"

namespace profiler
{
// True when a GPU activity-fallback backend (CUDA or HIP) is compiled in.
// Only one is ever active in a given build -- see PROFILER_HAS_CUDA /
// PROFILER_HAS_HIP in CMakeLists.txt.
constexpr bool hasGPU()
{
#if PROFILER_HAS_CUDA || PROFILER_HAS_HIP
    return true;
#else
    return false;
#endif
}

namespace profiler_impl::impl
{
struct Result;
namespace kineto
{
struct ActivityTraceWrapper;
}  // namespace kineto
}  // namespace profiler_impl::impl

namespace profiler_impl
{
using experimental_event_t = std::shared_ptr<profiler::profiler_impl::impl::Result>;
using extra_meta_t         = std::unordered_map<std::string, std::string>;

struct PROFILER_VISIBILITY KinetoEvent
{
    PROFILER_API KinetoEvent(
        const std::shared_ptr<const profiler::profiler_impl::impl::Result>& /*result*/,
        const bool verbose);

    PROFILER_API uint64_t startThreadId() const;
    PROFILER_API uint64_t endThreadId() const;
    PROFILER_API uint8_t  activityType() const;
    PROFILER_API bool     isHiddenEvent() const;
    PROFILER_API uint8_t  scope() const;
    PROFILER_API std::string name() const;
    PROFILER_API profiler::device_enum deviceType() const;
    PROFILER_API int                   deviceIndex() const;
    PROFILER_API int64_t               nBytes() const;
    PROFILER_API uint64_t              startNs() const;
    PROFILER_API uint64_t              endNs() const;
    PROFILER_API uint64_t              durationNs() const;
    PROFILER_API bool                  isAsync() const;
    PROFILER_API uint64_t              correlationId() const;
    PROFILER_API uint64_t              linkedCorrelationId() const;
    PROFILER_API int64_t               deviceResourceId() const;
    PROFILER_API int64_t               cudaElapsedUs() const;
    PROFILER_API int64_t               privateuse1ElapsedUs() const;
    PROFILER_API void getPerfEventCounters(profiler::profiler_impl::perf_counters_t& /*in*/) const;
    PROFILER_API extra_meta_t extraMeta() const;
    PROFILER_API std::vector<std::string> stack() const;
    PROFILER_API std::string metadataJson() const;

private:
    profiler::profiler_impl::impl::ProfilerVoidEventStub fallbackStart() const;
    profiler::profiler_impl::impl::ProfilerVoidEventStub fallbackEnd() const;

    std::shared_ptr<const profiler::profiler_impl::impl::Result> result_;
};

// Consolidating events returned directly from Kineto
// with events manually created by us (e.g. start/stop marks,
// memory allocation events)
struct PROFILER_VISIBILITY ProfilerResult
{
    PROFILER_API ProfilerResult();
    PROFILER_API ProfilerResult(
        uint64_t                                                                       start_time,
        std::vector<KinetoEvent>                                                       events,
        std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper>&& trace,
        std::vector<experimental_event_t>&&                                            event_tree);
    PROFILER_API ~ProfilerResult();

    uint64_t trace_start_ns() const { return trace_start_ns_; }

    const std::vector<KinetoEvent>& events() const { return events_; }

    const std::vector<experimental_event_t>& event_tree() const { return event_tree_; }

    // Writes libkineto Chrome Trace JSON. Returns false when there is no
    // trace (NVTX/ITT/PRIVATEUSE1, or disable without a matching enable).
    PROFILER_API bool save(const std::string& path);

private:
    uint64_t                 trace_start_ns_ = 0;
    std::vector<KinetoEvent> events_;
    std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper> trace_;
    std::vector<experimental_event_t>                                            event_tree_;
};

/*
 * This API is used by backends to record latency of events that
 * happened in the backend but were not visible to pytorch runtime.
 * For example, if part of the model is lowered to a dsp backend, then
 * the execution of that part of the model is delegated to the backend.
 * When backend finishes execution it has an option to provide profiling
 * information (latency only at the moment) corresponding to different operators
 * that were executed in the backend.
 * When such events are recorded by backend using this API, the event
 * records will be collected by active kineto profiler. If no kineto profiler
 * is active then the event is ignored.
 * This provides us with a way to generate all the profiling information
 * for a model regardless of where model (or part of it) executed.
 * @param start_time_us: start time in us of the event
 * @param end_time_us: end time in us of the event
 * @param debug_handle: debug handle to correlate this event/op with
 * model level module/source information
 * @param scope: scope of the event, e.g. LITE_INTERPRETER, RECORD_FN etc.
 * @param event_name: name of the event, e.g. op name
 * @param backend_name: name of the backend where the event took place.
 */
PROFILER_API void reportBackendEventToActiveKinetoProfiler(
    const int64_t               start_time_us,
    const int64_t               end_time_us,
    const int64_t               debug_handle,
    const profiler::RecordScope scope,
    const std::string&          event_name,
    const std::string&          backend_name);

PROFILER_API void enableProfiler(
    const profiler::profiler_impl::impl::ProfilerConfig&         config,
    const std::set<profiler::profiler_impl::impl::ActivityType>& activities,
    const std::unordered_set<profiler::RecordScope>&             scopes = {});

PROFILER_API std::unique_ptr<ProfilerResult> disableProfiler();

PROFILER_API void prepareProfiler(
    const profiler::profiler_impl::impl::ProfilerConfig&         config,
    const std::set<profiler::profiler_impl::impl::ActivityType>& activities);

PROFILER_API void toggleCollectionDynamic(
    const bool enable, const std::set<profiler::profiler_impl::impl::ActivityType>& activities);

PROFILER_API void startMemoryProfile();
PROFILER_API void stopMemoryProfile();
PROFILER_API void exportMemoryProfile(const std::string& path);

/**
 * When a C++ thread really has no control over how the profiler was enabled,
 * for example, by some unreachable Python code, it can call these functions
 * to test/join/unjoin itself into the collection set of a profiler, if any.
 * Without calling these functions, the symptom may be "not seeing GPU events
 * from some child C++ threads". This is an example on how to use them,
 *
 *    using namespace profiler::profiler_impl;
 *    bool enabled = isProfilerEnabledInMainThread();
 *    if (enabled != saved_enabled_state) {
 *      if (enabled) {
 *        enableProfilerInChildThread();
 *      } else {
 *        disableProfilerInChildThread();
 *      }
 *      saved_enabled_state = enabled;
 *    }
 */
PROFILER_API bool isProfilerEnabledInMainThread();
PROFILER_API void enableProfilerInChildThread();
PROFILER_API void disableProfilerInChildThread();

}  // namespace profiler_impl

}  // namespace profiler
