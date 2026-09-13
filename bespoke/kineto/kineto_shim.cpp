#include "bespoke/kineto/kineto_shim.h"

#include <type_traits>

#include "bespoke/common/collection.h"
#include "common/profiler_macros.h"

#if PROFILER_HAS_KINETO
#include <libkineto.h>
#endif

#include <cstdlib>
#include <string_view>

namespace profiler
{

namespace profiler_impl::impl::kineto
{

// Here lies pain and `#if PROFILER_HAS_KINETO`

#if PROFILER_HAS_KINETO
namespace
{
const std::set<libkineto::ActivityType> kCpuTypes{
    libkineto::ActivityType::CPU_OP,
    libkineto::ActivityType::CPU_INSTANT_EVENT,
    libkineto::ActivityType::USER_ANNOTATION,
    libkineto::ActivityType::EXTERNAL_CORRELATION,
    libkineto::ActivityType::CUDA_RUNTIME,
    libkineto::ActivityType::CUDA_DRIVER,
    libkineto::ActivityType::PYTHON_FUNCTION,
    libkineto::ActivityType::PRIVATEUSE1_RUNTIME,
    libkineto::ActivityType::PRIVATEUSE1_DRIVER,
};

const std::set<libkineto::ActivityType> kCudaTypes = {
    libkineto::ActivityType::GPU_MEMCPY,
    libkineto::ActivityType::GPU_MEMSET,
    libkineto::ActivityType::GPU_USER_ANNOTATION,
    libkineto::ActivityType::CONCURRENT_KERNEL,
    // CUDA_RUNTIME appears in both kCpuTypes and kCudaTypes.
    libkineto::ActivityType::CUDA_RUNTIME,
    libkineto::ActivityType::CUDA_DRIVER,
    libkineto::ActivityType::OVERHEAD,
};
const std::set<libkineto::ActivityType> kPrivateUse1Types = {
    libkineto::ActivityType::GPU_MEMCPY,
    libkineto::ActivityType::GPU_MEMSET,
    libkineto::ActivityType::GPU_USER_ANNOTATION,
    libkineto::ActivityType::CONCURRENT_KERNEL,
    // PRIVATEUSE1_RUNTIME appears in both kCpuTypes and kPrivateUse1Types.
    libkineto::ActivityType::PRIVATEUSE1_RUNTIME,
    libkineto::ActivityType::PRIVATEUSE1_DRIVER,
};
}  // namespace
#endif  // PROFILER_HAS_KINETO

static_assert(
    std::is_trivial_v<DeviceAndResource>, "Kineto specific details should be in `kineto_ids`.");

DeviceAndResource kineto_ids()
{
#if PROFILER_HAS_KINETO
    return {
        /*device=*/libkineto::processId(),
        /*resource=*/libkineto::systemThreadId()};
#else
    return {};
#endif  // PROFILER_HAS_KINETO
}

void addMetadata(
    activity_t*        activity,  // cppcheck-suppress constParameterPointer
    const std::string& key,
    const std::string& value)
{
#if PROFILER_HAS_KINETO
    // Suppress false positive from clang static analyzer in fmt library
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wanalyzer-optin.cplusplus.UninitializedObject"
#endif
    activity->addMetadata(key, value);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif  // PROFILER_HAS_KINETO
}

void addMetadataQuoted(
    activity_t*        activity,  // cppcheck-suppress constParameterPointer
    const std::string& key,
    const std::string& value)
{
#if PROFILER_HAS_KINETO
    // Suppress false positive from clang static analyzer in fmt library
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wanalyzer-optin.cplusplus.UninitializedObject"
#endif
    activity->addMetadataQuoted(key, value);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#else
    (void)activity;
    (void)key;
    (void)value;
#endif  // PROFILER_HAS_KINETO
}

TraceWrapper::TraceWrapper(const int64_t start_time, const std::string& name)
#if PROFILER_HAS_KINETO
    : cpu_trace_(std::make_unique<libkineto::CpuTraceBuffer>())
{
    cpu_trace_->span.startTime = start_time;
    cpu_trace_->gpuOpCount     = -1;
    cpu_trace_->span.name      = name;
}
#else
{
}
#endif  // PROFILER_HAS_KINETO

activity_t* TraceWrapper::addCPUActivity(
    const std::string&      name,
    const activity_type_t   type,
    const DeviceAndResource device_and_resource,
    const uint64_t          correlation_id,
    const int64_t           start_time,
    const int64_t           end_time)
{
#if PROFILER_HAS_KINETO
    // PROFILER_CHECK((bool)(*this), "Cannot add event to non-existent trace.");
    cpu_trace_->emplace_activity(cpu_trace_->span, type, name);
    auto& act     = libkineto::CpuTraceBuffer::toRef(cpu_trace_->activities.back());
    act.device    = device_and_resource.device;
    act.resource  = device_and_resource.resource;
    act.id        = static_cast<int32_t>(correlation_id);
    act.startTime = start_time;
    if (type != libkineto::ActivityType::CPU_INSTANT_EVENT)
    {
        act.endTime = end_time;
    }
    return cpu_trace_->activities.back().get();
#else
    return nullptr;
#endif  // PROFILER_HAS_KINETO
}

void TraceWrapper::transferCpuTrace(int64_t end_time)
{
#if PROFILER_HAS_KINETO
    cpu_trace_->span.endTime = end_time;
    libkineto::api().activityProfiler().transferCpuTrace(std::move(cpu_trace_));
#endif  // PROFILER_HAS_KINETO
}

TraceWrapper::operator bool() const
{
#if PROFILER_HAS_KINETO
    return cpu_trace_ != nullptr;
#else
    return false;
#endif  // PROFILER_HAS_KINETO
}

ActivityTraceWrapper::ActivityTraceWrapper(std::unique_ptr<interface_trace_t>&& trace)
    : trace_(std::move(trace))
{
}

ActivityTraceWrapper::operator bool() const
{
#if PROFILER_HAS_KINETO
    return trace_ != nullptr;
#else
    return false;
#endif  // PROFILER_HAS_KINETO
}

void ActivityTraceWrapper::save(PROFILER_UNUSED const std::string& path)
{
#if PROFILER_HAS_KINETO
    if (saved_ || trace_ == nullptr)
    {
        return;
    }
    trace_->save(path);
    saved_ = true;
#endif  // PROFILER_HAS_KINETO
}

#if PROFILER_HAS_KINETO
namespace
{
// Handles processing of Experimental Config options for Kineto
class ExperimentalConfigWrapper
{
public:
    explicit ExperimentalConfigWrapper(
        const profiler::profiler_impl::impl::ExperimentalConfig& config)
        : config_(config)
    {
    }

    bool assertValid() { return !config_.profiler_metrics.empty(); }

    void prepareTraceWithExperimentalOptions(std::set<libkineto::ActivityType>&& enabled_activities)
    {
#if PROFILER_HAS_KINETO
        std::set<libkineto::ActivityType> k_activities = std::move(enabled_activities);
        k_activities.insert(libkineto::ActivityType::CUDA_PROFILER_RANGE);

        // Add CPU activities if we are measuring per kernel ranges
        if (config_.profiler_measure_per_kernel)
        {
            k_activities.insert(kCpuTypes.begin(), kCpuTypes.end());
        }

        const size_t      num_metrics = config_.profiler_metrics.size();
        std::stringstream configss;

        configss << "ACTIVITIES_WARMUP_PERIOD_SECS=0\n"
                 << "CUPTI_PROFILER_METRICS=";

        for (size_t i = 0; i < num_metrics; i++)
        {
            configss << config_.profiler_metrics[i];
            if (num_metrics > 1 && i < (num_metrics - 1))
            {
                configss << ",";
            }
        }
        configss << "\nCUPTI_PROFILER_ENABLE_PER_KERNEL="
                 << (config_.profiler_measure_per_kernel ? "true" : "false") << "\n";
        configss << "CUSTOM_CONFIG=" << config_.custom_profiler_config << "\n";

        libkineto::api().activityProfiler().prepareTrace(k_activities, configss.str());
#else
        (void)enabled_activities;
#endif  // PROFILER_HAS_KINETO
    }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const profiler::profiler_impl::impl::ExperimentalConfig& config_;
};
}  // namespace
#endif  // PROFILER_HAS_KINETO

bool collectivesProfilerExists()
{
#ifdef KINETO_HAS_HCCL_PROFILER
    return true;
#endif
    const char* val = std::getenv("PROFILER_ENABLE_COLLECTIVE_PROFILING");
    return val != nullptr && std::string_view{val} == "1";
}

#if PROFILER_HAS_KINETO
static std::string setTraceID(const std::string& trace_id)
{
    if (trace_id.empty())
    {
        return "";
    }
    std::stringstream configss;
    configss << "REQUEST_TRACE_ID=" << trace_id << "\n";
    configss << "REQUEST_GROUP_TRACE_ID=" << trace_id << "\n";
    return configss.str();
}

static std::string appendCustomConfig(
    const std::string& config, const std::string& custom_profiler_config)
{
    if (custom_profiler_config.empty())
    {
        return config;
    }
    std::stringstream configss;
    configss << config;
    configss << "CUSTOM_CONFIG=" << custom_profiler_config << "\n";
    return configss.str();
}
#endif

void prepareTrace(
    const bool                                               cpuOnly,
    const ActivitySet&                                       activities,
    const profiler::profiler_impl::impl::ExperimentalConfig& config,
    const std::string&                                       trace_id)
{
#if PROFILER_HAS_KINETO
    libkineto::api().resetKinetoTLS();
    if (!libkineto::api().isProfilerRegistered())
    {
        libkineto_init(/*cpuOnly=*/cpuOnly, /*logOnError=*/true);
        libkineto::api().suppressLogMessages();
    }

    if (!libkineto::api().isProfilerInitialized())
    {
        libkineto::api().initProfilerIfRegistered();
    }

    std::set<libkineto::ActivityType> k_activities;
    bool const                        has_cpu_activity =
        activities.count(profiler::profiler_impl::ActivityType::CPU) > 0;  //NOLINT

    if (has_cpu_activity)
    {
        k_activities.insert(kCpuTypes.begin(), kCpuTypes.end());
    }
    if (activities.count(profiler::profiler_impl::ActivityType::CUDA) > 0 ||
        activities.count(profiler::profiler_impl::ActivityType::HIP) > 0)  //NOLINT
    {
        k_activities.insert(kCudaTypes.begin(), kCudaTypes.end());
        if (config.enable_cuda_sync_events || get_cuda_sync_enabled())
        {
            k_activities.insert(libkineto::ActivityType::CUDA_SYNC);
        }
    }
    if (collectivesProfilerExists())
    {
        k_activities.insert(libkineto::ActivityType::COLLECTIVE_COMM);
    }
    if (activities.count(profiler::profiler_impl::ActivityType::Metal) > 0)  //NOLINT
    {
        k_activities.insert(kPrivateUse1Types.begin(), kPrivateUse1Types.end());
    }

    ExperimentalConfigWrapper configWrap(config);

    // Experimental Configuration options are present
    if (config && configWrap.assertValid())
    {
        configWrap.prepareTraceWithExperimentalOptions(std::move(k_activities));
        return;
    }

    const std::string traceIdStr = setTraceID(trace_id);
    const std::string configStr  = appendCustomConfig(traceIdStr, config.custom_profiler_config);

    libkineto::api().activityProfiler().prepareTrace(k_activities, configStr);
#endif  // PROFILER_HAS_KINETO
}

void toggleCollectionDynamic(const bool enable)
{
#if PROFILER_HAS_KINETO
    // TODO: We may want to consider adding another input arg for this function
    // if we want to support turning off certain devices and keeping others on.
    // For now, we can keep it simple at have it turn off all tracing of "CUDA"
    // devices
    libkineto::api().activityProfiler().toggleCollectionDynamic(enable);
#endif  // PROFILER_HAS_KINETO
}

void startTrace()
{
#if PROFILER_HAS_KINETO
    libkineto::api().activityProfiler().startTrace();
#endif  // PROFILER_HAS_KINETO
}

ActivityTraceWrapper stopTrace()
{
    return ActivityTraceWrapper{
#if PROFILER_HAS_KINETO
        libkineto::api().activityProfiler().stopTrace()
#else
        std::make_unique<interface_trace_t>()
#endif  // PROFILER_HAS_KINETO
    };
}

void pushCorrelationId(uint64_t correlation_id)
{
#if PROFILER_HAS_KINETO
    libkineto::api().activityProfiler().pushCorrelationId(correlation_id);
#endif  // PROFILER_HAS_KINETO
}

void pushUserCorrelationId(uint64_t correlation_id)
{
#if PROFILER_HAS_KINETO
    libkineto::api().activityProfiler().pushUserCorrelationId(correlation_id);
#endif  // PROFILER_HAS_KINETO
}

void popCorrelationId()
{
#if PROFILER_HAS_KINETO
    libkineto::api().activityProfiler().popCorrelationId();
#endif  // PROFILER_HAS_KINETO
}

void popUserCorrelationId()
{
#if PROFILER_HAS_KINETO
    libkineto::api().activityProfiler().popUserCorrelationId();
#endif  // PROFILER_HAS_KINETO
}

void recordThreadInfo()
{
#if PROFILER_HAS_KINETO
    libkineto::api().activityProfiler().recordThreadInfo();
#endif  // PROFILER_HAS_KINETO
}

void logInvariantViolation(
    const std::string& assertion,
    const std::string& error,
    const std::string& profile_id,
    const std::string& group_profile_id)
{
#if PROFILER_HAS_KINETO
    if (libkineto::api().isProfilerInitialized())
    {
        libkineto::api().activityProfiler().logInvariantViolation(
            profile_id, assertion, error, group_profile_id);
    }
#endif  // PROFILER_HAS_KINETO
}

}  // namespace profiler_impl::impl::kineto

namespace profiler_impl
{
profiler::device_enum deviceTypeFromActivity(
    profiler::profiler_impl::impl::kineto::activity_type_t activity_type)
{
#if PROFILER_HAS_KINETO
    // libkineto has no HIP-specific ActivityType values -- its ROCm/roctracer
    // backend reuses the same CUDA-named ones below. Only one GPU backend is
    // ever active in a build (see PROFILER_HAS_CUDA/PROFILER_HAS_HIP in
    // CMakeLists.txt), so the tag is a compile-time choice, not a runtime one.
#if PROFILER_HAS_HIP
    constexpr profiler::device_enum kGpuDeviceType = profiler::device_enum::HIP;
#else
    constexpr profiler::device_enum kGpuDeviceType = profiler::device_enum::CUDA;
#endif
    // fallthrough
    switch (activity_type)
    {
    case libkineto::ActivityType::GPU_MEMCPY:
    case libkineto::ActivityType::GPU_MEMSET:
    case libkineto::ActivityType::CONCURRENT_KERNEL:
    case libkineto::ActivityType::CUDA_SYNC:
    case libkineto::ActivityType::GPU_USER_ANNOTATION:
    case libkineto::ActivityType::CUDA_PROFILER_RANGE:
    {
        return kGpuDeviceType;
    }
    case libkineto::ActivityType::CPU_OP:
    case libkineto::ActivityType::USER_ANNOTATION:
    case libkineto::ActivityType::EXTERNAL_CORRELATION:
    case libkineto::ActivityType::CUDA_RUNTIME:
    case libkineto::ActivityType::XPU_RUNTIME:
    case libkineto::ActivityType::CPU_INSTANT_EVENT:
    case libkineto::ActivityType::GLOW_RUNTIME:
    case libkineto::ActivityType::MTIA_RUNTIME:
    case libkineto::ActivityType::PYTHON_FUNCTION:
    case libkineto::ActivityType::CUDA_DRIVER:
    case libkineto::ActivityType::PRIVATEUSE1_RUNTIME:
    case libkineto::ActivityType::PRIVATEUSE1_DRIVER:
    case libkineto::ActivityType::OVERHEAD:
        return profiler::device_enum::CPU;
    default:
        return profiler::device_enum::CPU;
    }
#else
    (void)activity_type;
    return profiler::device_enum::CPU;
#endif  // PROFILER_HAS_KINETO
}

void addMetadataJson(const std::string& key, const std::string& value)
{
#if PROFILER_HAS_KINETO
    if (libkineto::api().isProfilerInitialized())
    {
        libkineto::api().activityProfiler().addMetadata(key, value);
    }
    else
    {
        // PROFILER_LOG_WARNING("Profiler is not initialized: skipping profiling metadata");
    }
#else
    /* PROFILER_LOG_WARNING(
        "Adding profiling metadata requires using "
        "profiler.profiler with Kineto support (PROFILER_HAS_KINETO=1)"); */
#endif  // PROFILER_HAS_KINETO
}

void profilerStep()
{
#if PROFILER_HAS_KINETO
    libkineto::api().initProfilerIfRegistered();

    if (libkineto::api().isProfilerInitialized())
    {
        libkineto::api().activityProfiler().step();
    }
    else
    {
        // PROFILER_LOG_WARNING("Profiler is not initialized: skipping step() invocation");
    }
#endif  // PROFILER_HAS_KINETO
}

}  // namespace profiler_impl

}  // namespace profiler
