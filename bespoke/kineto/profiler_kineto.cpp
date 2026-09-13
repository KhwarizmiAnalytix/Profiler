#include <fmt/format.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>

#define PROFILER_ASSERT_ONLY_METHOD_OPERATORS
#include "bespoke/base/nvtx_observer.h"
#include "bespoke/base/perf.h"
#include "bespoke/common/api.h"
#include "bespoke/common/collection.h"
#include "bespoke/common/containers.h"
#include "bespoke/common/events.h"
#include "bespoke/common/orchestration/observer.h"
#include "bespoke/common/orchestration/python_tracer.h"
#include "bespoke/common/standalone/privateuse1_observer.h"
#include "bespoke/common/util.h"
#include "common/profiler_export.h"
#if PROFILER_HAS_ITT
#include "bespoke/itt/itt_observer.h"
#endif
#include "bespoke/kineto/kineto_shim.h"
#include "bespoke/kineto/profiler_kineto.h"
#include "common/approximate_clock.h"
#include "common/flat_hash.h"
#include "common/irange.h"
#include "common/overloaded.h"

#if PROFILER_HAS_KINETO
#include <ApproximateClock.h>
#include <libkineto.h>
#include <time_since_epoch.h>

#ifndef _MSC_VER
// TODO: TO be removed, once this properly works from libkineto
// Literal copy-n-paste from third_party/kineto/libkineto/src/WeakSymbols.cpp
extern "C"
{
    // This function is needed to avoid superfluous dependency on GNU OpenMP library
    // when cuPTI is linked statically For more details see
    // https://github.com/pytorch/pytorch/issues/51026
    __attribute__((weak)) int acc_get_device_type();
    __attribute__((weak)) int acc_get_device_type()
    {
        // PROFILER_CHECK(
        // false, "Dummy implementation of acc_get_device_type is not supposed to be called!");
        return -1;  // Never reached, but satisfies compiler
    }
}  // extern "C"
#endif  // _MSC_VER
#endif  // PROFILER_HAS_KINETO

namespace profiler
{
namespace profiler_impl
{

namespace
{
inline int64_t getTimeNs()
{
#if PROFILER_HAS_KINETO
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.UninitializedObject)
    // False positive in fmt library's internal format_string_checker constructor
    return libkineto::timeSinceEpoch(std::chrono::system_clock::now());
#else
    return profiler::getTime();
#endif  // PROFILER_HAS_KINETO
}

using profiler::profiler_impl::impl::ActiveProfilerType;
using profiler::profiler_impl::impl::EventType;
using profiler::profiler_impl::impl::ExtraFields;
using profiler::profiler_impl::impl::ProfilerStateBase;
using profiler::profiler_impl::impl::Result;

struct MetadataBase
{
    explicit MetadataBase(const std::shared_ptr<Result>& result)
        : kinetoActivity_{result->kineto_activity_}
    {
        if (std::holds_alternative<ExtraFields<EventType::Kineto>>(result->extra_fields_))
        {
            // In order to add metadata we have to downcast from
            // `libkineto::ITraceActivity` to `libkineto::GenericTraceActivity`. We
            // know that all activities provided by Profiler are of the correct type,
            // however Kineto profilers can (and do) add events that inherit directly
            // from ITraceActivity. As a result, any Result which was constructed from
            // an event that Kineto provided is unsafe to cast.
            if (!(!hasKinetoActivity()))
            {
                result->kineto_activity_ = nullptr;
            }
            kinetoActivity_ = result->kineto_activity_;
        }
    }

    void addMetadata(const std::string& key, const std::string& value)
    {
        if ((kinetoActivity_ != nullptr) && !value.empty() && value != "\"\"")
        {
            profiler::profiler_impl::impl::kineto::addMetadata(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                const_cast<profiler::profiler_impl::impl::kineto::activity_t*>(kinetoActivity_),
                key,
                value);
        }
    }

    [[nodiscard]] bool hasKinetoActivity() const { return kinetoActivity_ != nullptr; }

private:
    const profiler::profiler_impl::impl::kineto::activity_t* kinetoActivity_{nullptr};
};

struct AddGenericMetadata : public MetadataBase
{
    AddGenericMetadata(
        std::shared_ptr<Result>&                             result,
        const profiler::profiler_impl::impl::ProfilerConfig* config)
        : MetadataBase(result), config_(config)
    {
        result->visit(*this);
        if (config->experimental_config.verbose)
        {
            // Note: PyExtraFieldsBase is not currently available in this build
            // Uncomment when Python integration is enabled
            // result->visit_if_base<PyExtraFieldsBase>(
            //     [&, this](const auto& i) -> void
            //     { this->addMetadata("Python thread", std::to_string(i.python_tid_)); });
        }
    }

    void operator()(ExtraFields<EventType::FunctionOp>& op_event)
    {
        // Caller-supplied structured metadata (RecordFunction::addMetadata /
        // record_function_metadata_builder).
        for (const auto& [key, val] : op_event.extra_meta_)
        {
            addMetadata(key, val);
        }

        if (config_ != nullptr && !config_->experimental_config.performance_events.empty())
        {
            const auto& event_names = config_->experimental_config.performance_events;
            for (const auto i : profiler::irange(op_event.perf_event_counters_->size()))
            {
                addMetadata(event_names[i], std::to_string((*op_event.perf_event_counters_)[i]));
            }
        }

        // add information about an associated forward op, if a sequence number
        // is available (e.g. during training)
        if (op_event.sequence_number_ >= 0)
        {
            addMetadata("Fwd thread id", std::to_string(op_event.forward_tid_));
            addMetadata("Sequence number", std::to_string(op_event.sequence_number_));
        }

        addMetadata("Record function id", std::to_string(op_event.record_function_id_));
    }

    void operator()(ExtraFields<EventType::Backend>& backend_event)
    {
        if (!backend_event.backend_.empty())
        {
            addMetadata("Backend", "\"" + backend_event.backend_ + "\"");
        }
    }

    void operator()(const ExtraFields<EventType::Allocation>& alloc)
    {
        addMetadata("device_option Type", std::to_string((int8_t)alloc.device_type_));
        addMetadata("device_option Id", std::to_string(alloc.device_index_));
        addMetadata("Addr", std::to_string(reinterpret_cast<intptr_t>(alloc.ptr_)));
        addMetadata("Bytes", std::to_string(alloc.alloc_size_));
        addMetadata("Total Allocated", std::to_string(alloc.total_allocated_));
        addMetadata("Total Reserved", std::to_string(alloc.total_reserved_));
    }

    void operator()(const ExtraFields<EventType::OutOfMemory>& alloc)
    {
        addMetadata("device_option Type", std::to_string((int8_t)alloc.device_type_));
        addMetadata("device_option Id", std::to_string(alloc.device_index_));
        addMetadata("Bytes", std::to_string(alloc.alloc_size_));
        addMetadata("Total Allocated", std::to_string(alloc.total_allocated_));
        addMetadata("Total Reserved", std::to_string(alloc.total_reserved_));
    }

    template <typename T>
    void operator()(const T& /*unused*/)
    {
    }

private:
    /* To get names of the performance events */
    const profiler::profiler_impl::impl::ProfilerConfig* config_;
};

struct KinetoThreadLocalState : public ProfilerStateBase
{
    explicit KinetoThreadLocalState(
        const ProfilerConfig&                                 config,
        std::set<profiler::profiler_impl::impl::ActivityType> activities)
        : ProfilerStateBase(config),
          startTime(getTimeNs()),
          recordQueue(config, std::move(activities))
    {
    }
    ~KinetoThreadLocalState() override = default;

    static KinetoThreadLocalState* get(bool global)
    {
        auto* state = ProfilerStateBase::get(/*global=*/global);
        // PROFILER_CHECK_DEBUG(
        // state == nullptr || state->profilerType() == ActiveProfilerType::KINETO);
        return static_cast<KinetoThreadLocalState*>(state);
    }

    ActiveProfilerType profilerType() override { return ActiveProfilerType::KINETO; }

    void reportMemoryUsage(
        void*                   ptr,
        int64_t                 alloc_size,
        size_t                  total_allocated,
        size_t                  total_reserved,
        profiler::device_option device) override
    {
        if (config_.profile_memory && !config_.disabled())
        {
            recordQueue.getSubqueue()->emplace_allocation_event(
                profiler::getApproximateTime(),
                ptr,
                alloc_size,
                total_allocated,
                total_reserved,
                device.type(),
                device.index());
        }
    }

    void reportOutOfMemory(
        int64_t                 alloc_size,
        size_t                  total_allocated,
        size_t                  total_reserved,
        profiler::device_option device) override
    {
        if (config_.profile_memory && !config_.disabled())
        {
            recordQueue.getSubqueue()->emplace_ooms_event(
                profiler::getApproximateTime(),
                alloc_size,
                total_allocated,
                total_reserved,
                device.type(),
                device.index());
        }
    }

    void pausePython() { recordQueue.stop(); }

    void resumePython() { recordQueue.restart(); }

    std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper> finalizeTrace()
    {
        auto end_time = getTimeNs();
        recordQueue.stop();

        std::scoped_lock const guard(state_mutex_);
        auto                   converter = clockConverter.makeConverter();
#if PROFILER_HAS_KINETO
        libkineto::get_time_converter() = converter;
#endif
        auto records_and_trace = recordQueue.getRecords(std::move(converter), startTime, end_time);

        materializeOpEvents(records_and_trace.first);

        return std::move(records_and_trace.second);
    }

    void materializeOpEvents(std::vector<std::shared_ptr<Result>>& events)
    {
        for (auto& e : events)
        {
            if (e->parent_.expired() && e->deviceType() == profiler::device_enum::CPU)
            {
                eventTree.push_back(e);
            }

            if (e->finished_)
            {
                kinetoEvents.emplace_back(e, config_.experimental_config.verbose);
                AddGenericMetadata const add_generic(e, &config_);

                // It is not safe to use the activity after post processing.
                e->kineto_activity_ = nullptr;
            }
        }
    }

    uint64_t                                      startTime;
    profiler::ApproximateClockToUnixTimeConverter clockConverter;
    profiler::profiler_impl::impl::RecordQueue    recordQueue;
    std::vector<KinetoEvent>                      kinetoEvents;
    std::vector<experimental_event_t>             eventTree;
};

template <bool use_global_state_ptr = false>
std::unique_ptr<profiler::ObserverContext> onFunctionEnter(const profiler::RecordFunction& fn)
{
    auto* state_ptr = KinetoThreadLocalState::get(use_global_state_ptr);
    if (!state_ptr)
    {
        return nullptr;
    }
    return state_ptr->recordQueue.getSubqueue()->begin_op(fn);
}

// @lint-ignore CLANGTIDY clang-diagnostic-unused-parameter
template <bool use_global_state_ptr = false>
void onFunctionExit(const profiler::RecordFunction& fn, profiler::ObserverContext* ctx_ptr)
{
    auto* state_ptr = KinetoThreadLocalState::get(use_global_state_ptr);
    if (!state_ptr)
    {
        return;
    }
    const auto& config = state_ptr->config();
    auto*       kineto_ctx_ptr =
        static_cast<profiler::profiler_impl::impl::KinetoObserverContext*>(ctx_ptr);
    // PROFILER_CHECK(kineto_ctx_ptr != nullptr);
    kineto_ctx_ptr->event_->end_time_ = profiler::getApproximateTime();
    if (!config.experimental_config.performance_events.empty())
    {
        state_ptr->recordQueue.getSubqueue()->disable_perf_profiler(
            *kineto_ctx_ptr->event_->counters_);
    }
    kineto_ctx_ptr->event_->basic_fields_.end_tid_ = profiler::RecordFunction::currentThreadId();
    if (config.state == ProfilerState::KINETO_GPU_FALLBACK)
    {
        auto* fallback = kineto_ctx_ptr->fallback_;
        // PROFILER_CHECK(fallback != nullptr);
        profiler::profiler_impl::impl::cudaStubs()->record(
            nullptr, &fallback->device_event_end_, nullptr);
    }
    else if (config.state == ProfilerState::KINETO_PRIVATEUSE1_FALLBACK)
    {
        auto* fallback = kineto_ctx_ptr->fallback_;
        // PROFILER_CHECK(fallback != nullptr);
        profiler::profiler_impl::impl::privateuse1Stubs()->record(
            nullptr, &fallback->device_event_end_, nullptr);
    }

    if (!config.experimental_config.disable_external_correlation)
    {
        if (fn.scope() == profiler::RecordScope::USER_SCOPE)
        {
            profiler::profiler_impl::impl::kineto::popUserCorrelationId();
        }
        else
        {
            profiler::profiler_impl::impl::kineto::popCorrelationId();
        }
    }
}

template <bool use_global_callback = false>
void pushProfilingCallbacks(const std::unordered_set<profiler::RecordScope>& scopes)
{
    auto* registration_state_ptr = KinetoThreadLocalState::get(use_global_callback);
    // PROFILER_CHECK(registration_state_ptr, "Expected profiler state set");
    auto recordFunctionCallback =
        profiler::RecordFunctionCallback(
            onFunctionEnter<use_global_callback>, onFunctionExit<use_global_callback>)
            .needsInputs(registration_state_ptr->config().report_input_shapes)
            .scopes(scopes);

    if constexpr (use_global_callback)
    {
        registration_state_ptr->setCallbackHandle(
            profiler::addGlobalCallback(recordFunctionCallback));
    }
    else
    {
        registration_state_ptr->setCallbackHandle(
            profiler::addThreadLocalCallback(recordFunctionCallback));
    }
}

struct ProfilerStateInfo
{
    std::shared_ptr<KinetoThreadLocalState>   state_ptr;
    std::unordered_set<profiler::RecordScope> scopes;
};
std::mutex                         profiler_state_info_mutex;
std::shared_ptr<ProfilerStateInfo> profiler_state_info_ptr{nullptr};

std::shared_ptr<ProfilerStateInfo> load_profiler_state_info()
{
    std::lock_guard<std::mutex> const lock(profiler_state_info_mutex);
    return profiler_state_info_ptr;
}

void store_profiler_state_info(std::shared_ptr<ProfilerStateInfo> state_info)
{
    std::lock_guard<std::mutex> const lock(profiler_state_info_mutex);
    profiler_state_info_ptr = std::move(state_info);
}

}  // namespace

void reportBackendEventToActiveKinetoProfiler(
    const int64_t               start_time_us,
    const int64_t               end_time_us,
    const int64_t               debug_handle,
    const profiler::RecordScope scope,
    const std::string&          event_name,
    const std::string&          backend_name)
{
    // PROFILER_CHECK(
    // KinetoThreadLocalState::get(/*global=*/true) == nullptr,
    // "On-demand profiling does not support post processing callback");

    auto* state_ptr = KinetoThreadLocalState::get(/*global=*/false);
    if (state_ptr == nullptr)
    {
        return;
    }

    state_ptr->recordQueue.getSubqueue()->emplace_backend_event(
        start_time_us, end_time_us, debug_handle, scope, event_name, backend_name);

    /* no support for input shapes now?
  if (config.report_input_shapes) {
    ctx_ptr->shapes = inputSizes(fn);
    ctx_ptr->dtypes = inputTypes(fn);
  }
  */
}

void prepareProfiler(
    const profiler::profiler_impl::impl::ProfilerConfig&         config,
    const std::set<profiler::profiler_impl::impl::ActivityType>& activities)
{
    if (config.state == ProfilerState::NVTX || config.state == ProfilerState::ITT)
    {
        return;
    }

    // PROFILER_CHECK(
    // config.state == ProfilerState::KINETO ||
    // config.state == ProfilerState::KINETO_GPU_FALLBACK ||
    // config.state == ProfilerState::KINETO_PRIVATEUSE1_FALLBACK,
    // "Supported only in Kineto profiler");

    profiler::profiler_impl::impl::kineto::prepareTrace(
        /*cpuOnly=*/!(profiler::hasGPU()), activities, config.experimental_config, config.trace_id);

    if (!config.experimental_config.performance_events.empty())
    {
        /* For now only CPU activity is supported */
        // PROFILER_CHECK(
        // activities.count(profiler::profiler_impl::ActivityType::CPU),
        // "Cannot run cpu hardware profiler without CPU activities, please only use CPU activity "
        // "type");
        /*
     * Sending a warning and passing the non-standard event to the backend
     * Backend can abort if the event is not supported.
     * TODO Should we gracefully drop the invalid event if we have at least one
     * valid?
     */
        auto is_standard_event = [](const std::string& event) -> bool
        {
            return std::any_of(
                std::begin(profiler::profiler_impl::ProfilerPerfEvents),
                std::end(profiler::profiler_impl::ProfilerPerfEvents),
                [&event](const auto* e) { return std::strcmp(event.c_str(), e) == 0; });
        };

        for (const auto& e : config.experimental_config.performance_events)
        {
            if (!is_standard_event(e))
            {
                fmt::print(
                    stderr,
                    "PROFILER WARNING: Forwarding a non-standard CPU performance event : {}\n",
                    e);
            }
        }
    }
}

static void toggleFunctionOpCollectionDynamic(bool enable)
{
    auto* state_ptr = ProfilerStateBase::get();
    if (state_ptr != nullptr)
    {
        const auto& config = state_ptr->config();
        if (enable)
        {
            auto state_info = load_profiler_state_info();
            if (state_info == nullptr)
            {
                return;
            }
            auto scopes = state_info->scopes;
            config.global() ? pushProfilingCallbacks</*global=*/true>(scopes)
                            : pushProfilingCallbacks</*global=*/false>(scopes);
        }
        else
        {
            state_ptr->removeCallback();
        }
    }
}

// Set this function to be unused as profiler implementation needs more
// refactoring to support Python ops collection dynamic toggling
[[maybe_unused]] static void togglePythonCollectionDynamic(bool enable)
{
    auto* state_ptr = ProfilerStateBase::get();
    if (state_ptr != nullptr)
    {
        auto                    global                        = state_ptr->config().global();
        KinetoThreadLocalState* kineto_thread_local_state_ptr = KinetoThreadLocalState::get(global);
        if (enable)
        {
            kineto_thread_local_state_ptr->resumePython();
        }
        else
        {
            kineto_thread_local_state_ptr->pausePython();
        }
    }
}

static void toggleCPUCollectionDynamic(bool enable)
{
    toggleFunctionOpCollectionDynamic(enable);
    // For now we only support Torch Op collection dynamic toggling as
    // implementing Python ops would require not only string parsing to get rid of
    // the toggling events as well as other unfinished events as well as changes
    // in stack logic
    // togglePythonCollectionDynamic(enable);
}

void toggleCollectionDynamic(
    const bool enable, const std::set<profiler::profiler_impl::impl::ActivityType>& activities)
{
    for (auto act : activities)
    {
        if (act == profiler::profiler_impl::ActivityType::CUDA ||
            act == profiler::profiler_impl::ActivityType::HIP ||
            act == profiler::profiler_impl::ActivityType::Metal)
        {
            profiler::profiler_impl::impl::kineto::toggleCollectionDynamic(enable);
        }
        else if (act == profiler::profiler_impl::ActivityType::CPU)
        {
            toggleCPUCollectionDynamic(enable);
        }
        else
        {
            //LOG(WARNING)
            //<< "Dynamic toggle is only supported for CPU/GPU activity, skipping toggling of "
            //<< actToString(act);
            continue;
        }
    }
}

void enableProfiler(
    const profiler::profiler_impl::impl::ProfilerConfig&         config,
    const std::set<profiler::profiler_impl::impl::ActivityType>& activities,
    const std::unordered_set<profiler::RecordScope>&             scopes)
{
    const auto has_cpu = activities.count(ActivityType::CPU);
    // PROFILER_CHECK(
    // KinetoThreadLocalState::get(/*global=*/config.global()) == nullptr,
    // "Profiler is already enabled",
    // (config.global() ? "." : " on this thread."));

    if (config.state == ProfilerState::NVTX)
    {
        profiler::profiler_impl::impl::pushNVTXCallbacks(config, scopes);
        return;
    }
    if (config.state == ProfilerState::ITT)
    {
#if PROFILER_HAS_ITT
        profiler::profiler_impl::impl::pushITTCallbacks(config, scopes);
#endif
        return;
    }
    if (config.state == ProfilerState::PRIVATEUSE1)
    {
        if (profiler::profiler_impl::impl::pushPRIVATEUSE1CallbacksStub)
        {
            profiler::profiler_impl::impl::pushPRIVATEUSE1CallbacksStub(config, scopes);
        }
        return;
    }

    // PROFILER_CHECK(
    // config.state == ProfilerState::KINETO ||
    // config.state == ProfilerState::KINETO_GPU_FALLBACK ||
    // config.state == ProfilerState::KINETO_PRIVATEUSE1_FALLBACK || config.global());
    // PROFILER_CHECK(!activities.empty(), "No activities specified.");
    // PROFILER_CHECK(has_cpu || !config.global(), "Ondemand profiling must enable CPU tracing");

    auto state_ptr = std::make_shared<KinetoThreadLocalState>(config, activities);
    KinetoThreadLocalState::push(state_ptr);

    if (has_cpu != 0u)
    {
        config.pushGlobalCallbacks() ? pushProfilingCallbacks</*global=*/true>(scopes)
                                     : pushProfilingCallbacks</*global=*/false>(scopes);
    }

    if (!config.global())
    {
        profiler::profiler_impl::impl::kineto::startTrace();
    }

    if (has_cpu != 0u)
    {
        auto state_info_ptr       = std::make_shared<ProfilerStateInfo>();
        state_info_ptr->state_ptr = state_ptr;
        state_info_ptr->scopes    = scopes;
        store_profiler_state_info(std::move(state_info_ptr));
    }
}

bool isProfilerEnabledInMainThread()
{
    return load_profiler_state_info() != nullptr;
}

void enableProfilerInChildThread()
{
    auto state_info_ptr = load_profiler_state_info();
    if (state_info_ptr == nullptr || state_info_ptr->state_ptr == nullptr)
    {
        return;
    }

    KinetoThreadLocalState::push(state_info_ptr->state_ptr);
    pushProfilingCallbacks</*global=*/false>(state_info_ptr->scopes);
}

void disableProfilerInChildThread()
{
    auto state_ptr = ProfilerStateBase::pop();
    if (state_ptr == nullptr)
    {
        return;
    }
    state_ptr->removeCallback();
}

std::unique_ptr<ProfilerResult> disableProfiler()
{
    // releasing to inform child threads to stop profiling
    store_profiler_state_info(nullptr);

    auto state_ptr = ProfilerStateBase::pop();
    if (state_ptr == nullptr)
    {
        return std::make_unique<ProfilerResult>();
    }
    const auto& config = state_ptr->config();

    state_ptr->removeCallback();

    // Traces are converged via libkineto automatically for ondemand flow
    if (state_ptr->config().global())
    {
        (void)std::static_pointer_cast<KinetoThreadLocalState>(state_ptr)->finalizeTrace();
        return std::make_unique<ProfilerResult>();
    }

    // Shared among NVTX, ITT, PRIVATEUSE1, KINETO, KINETO_GPU_FALLBACK,
    // KINETO_PRIVATEUSE1_FALLBACK
    std::unique_ptr<ProfilerResult> result;
    if (state_ptr->config().state == ProfilerState::NVTX ||
        state_ptr->config().state == ProfilerState::ITT ||
        state_ptr->config().state == ProfilerState::PRIVATEUSE1)
    {
        result = std::make_unique<ProfilerResult>();
    }

    if (config.state == ProfilerState::KINETO ||
        config.state == ProfilerState::KINETO_GPU_FALLBACK ||
        config.state == ProfilerState::KINETO_PRIVATEUSE1_FALLBACK)
    {
        auto kineto_state_ptr = std::static_pointer_cast<KinetoThreadLocalState>(state_ptr);
        auto trace            = kineto_state_ptr->finalizeTrace();
        result                = std::make_unique<ProfilerResult>(
            kineto_state_ptr->startTime,
            std::move(kineto_state_ptr->kinetoEvents),
            std::move(trace),
            std::move(kineto_state_ptr->eventTree));
    }

    if (result == nullptr)
    {
        result = std::make_unique<ProfilerResult>();
    }
    return result;
}
namespace tracer = profiler::profiler_impl::impl::python_tracer;
static std::unique_ptr<tracer::PythonMemoryTracerBase> memory_tracer;

void startMemoryProfile()
{
    if (memory_tracer == nullptr)
    {
        memory_tracer = tracer::PythonMemoryTracerBase::make();
    }
    memory_tracer->start();
}

void stopMemoryProfile()
{
    if (memory_tracer != nullptr)
    {
        memory_tracer->stop();
    }
}

void exportMemoryProfile(const std::string& filename)
{
    if (memory_tracer != nullptr)
    {
        memory_tracer->export_memory_history(filename);
    }
}

KinetoEvent::KinetoEvent(
    const std::shared_ptr<const profiler::profiler_impl::impl::Result>& result, const bool verbose)
    : result_{result}
{
    // PROFILER_CHECK(result != nullptr);
    (void)verbose;
}

bool KinetoEvent::isHiddenEvent() const
{
    return result_ && result_->hidden_;
}

uint64_t KinetoEvent::endNs() const
{
    return result_->endTimeNS();
}

uint64_t KinetoEvent::durationNs() const
{
    return (result_->endTimeNS() - result_->start_time_ns_);
}

int KinetoEvent::deviceIndex() const
{
    return result_->visit(profiler::overloaded(
        [](const ExtraFields<EventType::Allocation>& i)
        { return static_cast<int>(i.device_index_); },
        [](const ExtraFields<EventType::OutOfMemory>& i)
        { return static_cast<int>(i.device_index_); },
        [&](const auto&) { return static_cast<int>(result_->kineto_info_.device); }));
}

int64_t KinetoEvent::cudaElapsedUs() const
{
    auto cuda_event_start = fallbackStart();
    auto cuda_event_end   = fallbackEnd();
    if (!cuda_event_start || !cuda_event_end)
    {
        return -1;
    }
    try
    {
        return (int64_t)profiler::profiler_impl::impl::cudaStubs()->elapsed(
            &cuda_event_start, &cuda_event_end);
    }
    catch (const std::exception& e)  //NOLINT
    {
        //LOG(WARNING) << "Failed to measure time between two CUDA events. "
        //<< e.what();
        (void)e;  // Suppress unused variable warning
    }
    return -1;
}

int64_t KinetoEvent::privateuse1ElapsedUs() const
{
    auto privateuse1_event_start = fallbackStart();
    auto privateuse1_event_end   = fallbackEnd();
    if (!privateuse1_event_start || !privateuse1_event_end)
    {
        return -1;
    }
    return static_cast<int64_t>(profiler::profiler_impl::impl::privateuse1Stubs()->elapsed(
        &privateuse1_event_start, &privateuse1_event_end));
}

void KinetoEvent::getPerfEventCounters(std::vector<uint64_t>& in) const
{
    return result_->visit(profiler::overloaded(
        [&in](const ExtraFields<EventType::FunctionOp>& e) -> void
        {
            const size_t n = e.perf_event_counters_->size();
            // should be rare
            if (in.size() < n)
            {
                in.resize(n, 0);
            }
            for (size_t i = 0; i < n; ++i)
            {
                in[i] = (*e.perf_event_counters_)[i];
            }
        },
        [](const auto&) -> void { return; }));
}

std::string KinetoEvent::metadataJson() const
{
    return result_->visit(profiler::overloaded(
        [](const ExtraFields<EventType::FunctionOp>& op) -> std::string
        { return op.metadata_json_; },
        [](const ExtraFields<EventType::Kineto>& op) -> std::string { return op.metadata_json_; },
        [](const auto&) -> std::string { return {""}; }));
}

#define FORWARD_FROM_RESULT(method_name, result_expr)                                    \
    decltype(std::declval<KinetoEvent>().method_name()) KinetoEvent::method_name() const \
    {                                                                                    \
        return static_cast<decltype(std::declval<KinetoEvent>().method_name())>(         \
            result_->result_expr);                                                       \
    }

FORWARD_FROM_RESULT(startThreadId, start_tid_)
FORWARD_FROM_RESULT(endThreadId, endTID())
#if PROFILER_HAS_KINETO
FORWARD_FROM_RESULT(activityType, kinetoType())
#else
// Result::kinetoType() only exists when compiled with Kineto (it forwards to
// libkineto's ActivityType). Under ITT/NVTX-only builds no KinetoEvent is ever
// materialized from real Kineto activity data, so this is never meaningfully
// called -- provide a default rather than omitting the symbol, since
// activityType() is part of KinetoEvent's unconditionally-declared public API.
// Mirrors libkineto::ActivityType::ENUM_COUNT (ThirdParty/kineto/libkineto/
// include/ActivityType.h), documented there as "not used for any profiling
// logic" -- an explicit out-of-band sentinel, not 0, since 0 is the real,
// commonly-matched ActivityType::CPU_OP and would be indistinguishable from a
// genuine CPU-op event to any caller that switches on this value. Named here
// (rather than including the Kineto header, which this build config never
// compiles in) so it has one source of truth instead of a magic literal.
constexpr uint8_t kActivityTypeUnavailable = 27;

uint8_t KinetoEvent::activityType() const
{
    return kActivityTypeUnavailable;
}
#endif
FORWARD_FROM_RESULT(name, name())
FORWARD_FROM_RESULT(deviceType, deviceType())
FORWARD_FROM_RESULT(startNs, start_time_ns_)
FORWARD_FROM_RESULT(correlationId, correlationID())
FORWARD_FROM_RESULT(deviceResourceId, kineto_info_.resource)
#undef FORWARD_FROM_RESULT

// Most of the fields in `KinetoEvent` only make sense for a single event type.
// (Generally FunctionOp.) For all other types they simply return the default
// value. This macro provides a succinct way of expressing this behavior.
#define TYPED_ATTR_WITH_DEFAULT(event_type, method_name, expression, default_value)          \
    decltype(std::declval<KinetoEvent>().method_name()) KinetoEvent::method_name() const     \
    {                                                                                        \
        using out_t = decltype(std::declval<KinetoEvent>().method_name());                   \
        return result_->visit(profiler::overloaded(                                          \
            [](const ExtraFields<EventType::event_type>& e) -> out_t { return expression; }, \
            [](const auto&) -> out_t { return default_value; }));                            \
    }

#define TYPED_ATTR(event_type, method_name, expression) \
    TYPED_ATTR_WITH_DEFAULT(event_type, method_name, expression, {})

TYPED_ATTR(FunctionOp, scope, static_cast<uint8_t>(e.scope_))
TYPED_ATTR(FunctionOp, isAsync, e.is_async_)
TYPED_ATTR(FunctionOp, extraMeta, e.extra_meta_)
TYPED_ATTR(FunctionOp, stack, e.stack_)
TYPED_ATTR(FunctionOp, fallbackStart, e.device_fallback_.device_event_start_)
TYPED_ATTR(FunctionOp, fallbackEnd, e.device_fallback_.device_event_end_)
TYPED_ATTR(Allocation, nBytes, e.alloc_size_)
TYPED_ATTR(
    Kineto,
    linkedCorrelationId,
    [&]()
    {
        const auto linked = e.linked_activity_.lock();
        return linked ? linked->correlationID() : 0;
    }())
#undef TYPED_ATTR
#undef TYPED_ATTR_WITH_DEFAULT

ProfilerResult::ProfilerResult(
    uint64_t                                                                       start_time,
    std::vector<KinetoEvent>                                                       events,
    std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper>&& trace,
    std::vector<experimental_event_t>&&                                            event_tree)
    : trace_start_ns_(start_time),
      events_(std::move(events)),
      trace_(std::move(trace)),
      event_tree_(std::move(event_tree))
{
}
ProfilerResult::ProfilerResult()  = default;
ProfilerResult::~ProfilerResult() = default;

bool ProfilerResult::save(const std::string& path)
{
    if (trace_ == nullptr)
    {
        return false;
    }
    trace_->save(path);
    return static_cast<bool>(*trace_);
}

}  // namespace profiler_impl

}  // namespace profiler
