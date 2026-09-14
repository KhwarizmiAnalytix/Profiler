#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "bespoke/base/base.h"
#include "bespoke/base/perf.h"
#include "bespoke/common/containers.h"
#include "bespoke/common/events.h"
#include "bespoke/common/orchestration/python_tracer.h"
#include "bespoke/common/util.h"
#include "bespoke/kineto/kineto_shim.h"
#include "common/profiler_macros.h"
//#include "common/device.h"
#include "common/approximate_clock.h"
#include "common/flat_hash.h"
#include "common/strong_type.h"

namespace profiler::profiler_impl::impl
{

enum class EventType : uint8_t
{
    FunctionOp = 0,
    Backend,
    Allocation,
    OutOfMemory,
    PyCall,
    PyCCall,
    Kineto,
    PythonGC
};

// Used during post processing.
struct PROFILER_VISIBILITY ProfilerStepInfo
{
    int64_t  start_time_ns;  // start time of the profiler step
    int64_t  end_time_ns;    // end time of the profiler step
    uint64_t out_idx;        // index of the profiler step in the profiler "out" var in
                             // getRecords

    ProfilerStepInfo(int64_t start, int64_t end, uint64_t out_idx)
        : start_time_ns(start), end_time_ns(end), out_idx(out_idx)
    {
    }
};

// ============================================================================
// == ExtraFields =============================================================
// ============================================================================
template <EventType>
struct ExtraFields;

struct FunctionOpBasicFields
{
    int64_t                  sequence_number_{0};
    uint64_t                 forward_tid_{0};
    profiler::RecordScope    scope_{};
    bool                     is_async_{false};
    uint64_t                 record_function_id_{0};
    int64_t                  debug_handle_{0};
    std::string              name_;
    std::string              overload_name_;
    std::vector<std::string> stack_;

    // Set in the exit callback.
    uint64_t end_tid_{0};
};

using extra_meta_t = std::unordered_map<std::string, std::string>;

struct FallbackPair
{
    ProfilerVoidEventStub device_event_start_ = nullptr;
    ProfilerVoidEventStub device_event_end_   = nullptr;
};

template <>
struct ExtraFields<EventType::FunctionOp> : FunctionOpBasicFields
{
    ExtraFields(
        FunctionOpBasicFields&&            f,
        uint64_t                           correlation_id,
        profiler::time_t                   end_time_ns,
        extra_meta_t&&                     extra_meta,
        FallbackPair&&                     device_fallback,
        std::unique_ptr<perf_counters_t>&& perf_event_counters)
        : FunctionOpBasicFields(std::move(f)),
          correlation_id_{correlation_id},
          end_time_ns_{end_time_ns},
          extra_meta_{std::move(extra_meta)},
          device_fallback_{std::move(device_fallback)},
          perf_event_counters_{std::move(perf_event_counters)}
    {
    }
    uint64_t                         correlation_id_;
    profiler::time_t                 end_time_ns_;
    extra_meta_t                     extra_meta_;
    FallbackPair                     device_fallback_;
    std::unique_ptr<perf_counters_t> perf_event_counters_;
    std::string                      metadata_json_;
};

template <>
struct ExtraFields<EventType::Backend>
{
    int64_t               start_time_us_;
    int64_t               end_time_us_;
    int64_t               debug_handle_;
    profiler::RecordScope scope_;
    std::string           name_;
    std::string           backend_;
};

template <>
struct ExtraFields<EventType::PythonGC>
{
    std::string phase;
    int64_t     duration_ns_;
};

struct RawAllocation
{
    profiler::approx_time_t start_time_;
    void*                   ptr_;
    int64_t                 alloc_size_;
    size_t                  total_allocated_;
    size_t                  total_reserved_;
    profiler::device_enum   device_type_;
    int16_t                 device_index_;
};

// For performance.
static_assert(std::is_trivial_v<RawAllocation>, "Non-Trivial member of RawAllocation.");

template <>
struct ExtraFields<EventType::Allocation> : RawAllocation
{
    ExtraFields(const RawAllocation& allocation) : RawAllocation(allocation) {}

    profiler::device_option device() const
    {
        profiler::device_option d{};
        d.type_  = device_type_;
        d.index_ = device_index_;
        return d;
    }
};

template <>
struct ExtraFields<EventType::OutOfMemory>
{
    profiler::approx_time_t start_time_;
    int64_t                 alloc_size_;
    size_t                  total_allocated_;
    size_t                  total_reserved_;
    profiler::device_enum   device_type_;
    int16_t                 device_index_;
};

// For performance.
static_assert(
    std::is_trivial_v<ExtraFields<EventType::OutOfMemory>>,
    "Non-Trivial member of ExtraFields<EventType::OutOfMemory>.");

template <>
struct ExtraFields<EventType::Kineto>
{
    // Mirrors `libkineto::GenericTraceActivity::Flow`. This information is used
    // during post processing to properly embed Kineto events into the broader
    // profiler tree structure. End users are not generally expected to use these
    // fields directly, but they are available for debugging.
    struct Flow
    {
        uint32_t id{0};
        uint32_t type{0};
        uint32_t start{0};
    };

    std::string name_;
    int64_t     duration_ns_{0};
    uint64_t    correlation_id_{0};
#if PROFILER_HAS_KINETO
    kineto::activity_type_t activity_type_;
#endif
    Flow                  flow;
    std::weak_ptr<Result> linked_activity_;
    std::string           metadata_json_;
};

struct PROFILER_VISIBILITY Result : public std::enable_shared_from_this<Result>
{
    template <typename... Args>
    [[nodiscard]] static std::shared_ptr<Result> create(Args... args)
    {
        return std::shared_ptr<Result>(new Result(std::forward<Args>(args)...));
    }

    template <typename T>
    auto visit(T&& visitor)
    {
        return std::visit(std::forward<T>(visitor), extra_fields_);
    }

    template <typename T>
    auto visit(T&& visitor) const
    {
        return std::visit(std::forward<T>(visitor), extra_fields_);
    }

    template <typename T, typename Fn>
    void visit_if_base(const Fn& fn) const
    {
        visit(
            [&](const auto& extra_fields)
            {
                using extra_fields_t = typename std::remove_cv_t<
                    typename std::remove_reference_t<decltype(extra_fields)>>;

                if constexpr (std::is_base_of_v<T, extra_fields_t>)
                {
                    fn(extra_fields);
                }
            });
    }

    EventType tag() const
    {
        return visit([](const auto& i) { return deduceTag(i); });
    }

    std::string name() const;
    std::string overload_name() const;
#if PROFILER_HAS_KINETO
    kineto::activity_type_t kinetoType() const;
#endif
    uint64_t              correlationID() const;
    int64_t               endTimeNS() const;
    uint64_t              endTID() const;
    profiler::device_enum deviceType() const;

    int64_t                   start_time_ns_;
    uint64_t                  start_tid_;
    kineto::DeviceAndResource kineto_info_;
    // ExtraFields<EventType::PythonGC> / PyCall / PyCCall are defined for a
    // registered python_tracer implementation to materialize; the default
    // NoOpPythonTracer never produces them, so they are not variant alternatives.
    std::variant<
        ExtraFields<EventType::FunctionOp>,
        ExtraFields<EventType::Backend>,
        ExtraFields<EventType::Allocation>,
        ExtraFields<EventType::OutOfMemory>,
        ExtraFields<EventType::Kineto>>
        extra_fields_;

    std::weak_ptr<Result>                                    parent_;
    std::vector<std::shared_ptr<Result>>                     children_;
    bool                                                     finished_{false};
    bool                                                     hidden_{false};
    const profiler::profiler_impl::impl::kineto::activity_t* kineto_activity_{nullptr};

private:
    template <EventType E>
    Result(
        int64_t                   start_time_ns,
        uint64_t                  start_tid,
        kineto::DeviceAndResource kineto_info,
        ExtraFields<E>&&          extra_fields)
        : start_time_ns_{start_time_ns},
          start_tid_{start_tid},
          kineto_info_{kineto_info},
          extra_fields_{std::move(extra_fields)}
    {
    }

    template <EventType E>
    static EventType deduceTag(const ExtraFields<E>& /*unused*/)
    {
        return E;
    }
};

struct KinetoObserverContext : public profiler::ObserverContext
{
    struct Event
    {
        FunctionOpBasicFields   basic_fields_;
        profiler::approx_time_t start_time_;

        // Set in the exit callback.
        profiler::approx_time_t end_time_{std::numeric_limits<profiler::approx_time_t>::min()};

        std::unique_ptr<perf_counters_t> counters_;
    };

    explicit KinetoObserverContext(Event* event) : event_{event} {}

    Event*        event_;
    FallbackPair* fallback_{nullptr};
};

using perf_profiler_t = profiler::profiler_impl::impl::linux_perf::PerfProfiler;

class PROFILER_VISIBILITY ThreadLocalSubqueue
{
public:
    ThreadLocalSubqueue(const uint64_t tid, ProfilerConfig config);

    std::unique_ptr<KinetoObserverContext> begin_op(const profiler::RecordFunction& fn);

    template <class... Args>
    void emplace_backend_event(Args&&... args)
    {
        backend_events_.emplace_back(std::forward<Args>(args)...);
    }

    template <class... Args>
    void emplace_allocation_event(Args&&... args)
    {
        allocations_.emplace_back(std::forward<Args>(args)...);
    }

    template <class... Args>
    void emplace_ooms_event(Args&&... args)
    {
        ooms_.emplace_back(std::forward<Args>(args)...);
    }

    template <class... Args>
    void emplace_py_call(Args&&... args)
    {
        py_calls_.emplace_back(std::forward<Args>(args)...);
    }

    template <class... Args>
    void emplace_gc_call(Args&&... args)
    {
        pythongc_.emplace_back(std::forward<Args>(args)...);
    }

    uint64_t tid() const { return tid_; }

    const kineto::DeviceAndResource& kineto_info() const { return kineto_info_; }

    inline void disable_perf_profiler(perf_counters_t& counters) const
    {
        perf_profiler_->Disable(counters);
    }

private:
    uint64_t                         tid_;
    ProfilerConfig                   config_;
    kineto::DeviceAndResource        kineto_info_;
    std::unique_ptr<perf_profiler_t> perf_profiler_;

    friend class RecordQueue;
    // See `containers.h` for block size benchmarks.
    static constexpr size_t BlockSize = 512;

    struct FunctionOpStorage
    {
        // NB: This is a destructive operation.
        void materialize(
            std::vector<std::shared_ptr<Result>>&                           out,
            std::vector<ProfilerStepInfo>&                                  step_info,
            const std::function<profiler::time_t(profiler::approx_time_t)>& time_converter,
            const uint64_t                                                  tid,
            const kineto::DeviceAndResource&                                kineto_info);

        template <typename T, size_t ChunkSize>
        class EventBlock : public std::array<T, ChunkSize>
        {
        public:
            EventBlock();
            uint64_t correlation_id(const T* ptr) const;

        private:
            uint64_t id_start_;
        };

        using event_t = KinetoObserverContext::Event;
        class OpList : public AppendOnlyList<event_t, BlockSize, EventBlock>
        {
        public:
            template <class... Args>
            std::pair<event_t*, uint64_t> emplace_back(Args&&... args);
            static uint64_t               correlationID(const OpList::Iterator& e);
        } op_events_;

        // Per-function structured metadata, populated from RecordFunction::metadata().
        AppendOnlyList<extra_meta_t, BlockSize> extra_meta_;

        // ProfilerState::KINETO_GPU_FALLBACK or
        // ProfilerState::KINETO_PRIVATEUSE1_FALLBACK
        AppendOnlyList<FallbackPair, BlockSize> device_fallback_;
    } function_ops_;

    // reportBackendEventToActiveKinetoProfiler
    AppendOnlyList<ExtraFields<EventType::Backend>, BlockSize> backend_events_;

    // reportMemoryUsage
    AppendOnlyList<RawAllocation, BlockSize> allocations_;

    // reportOOMs
    AppendOnlyList<ExtraFields<EventType::OutOfMemory>, BlockSize> ooms_;

    // with_stack (Python) — populated by a registered PythonTracerBase
    AppendOnlyList<std::pair<python_tracer::TraceKey, profiler::approx_time_t>, BlockSize>
        py_calls_;
    // gc with_stack (Python)
    AppendOnlyList<std::pair<std::string, profiler::approx_time_t>, BlockSize> pythongc_;
};

class PROFILER_VISIBILITY RecordQueue
{
public:
    RecordQueue(ProfilerConfig config, std::set<ActivityType> activities);

    bool                 tracePython() const;
    bool                 getPythonGcEvents() const;
    ThreadLocalSubqueue* getSubqueue();
    void                 stop();
    void                 restart();

    // NB: This is a destructive operation.
    std::pair<
        std::vector<std::shared_ptr<Result>>,
        std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper>>
    getRecords(
        std::function<profiler::time_t(profiler::approx_time_t)> time_converter,
        uint64_t                                                 start_time_ns,
        uint64_t                                                 end_time_ns);

private:
    uint32_t                                                                id_;
    ProfilerConfig                                                          config_;
    std::set<ActivityType>                                                  activities_;
    profiler::flat_hash_map<uint64_t, std::unique_ptr<ThreadLocalSubqueue>> sub_queues_;
    std::mutex                                                              sub_queue_mutex_;
    std::unique_ptr<python_tracer::PythonTracerBase>                        python_tracer_;
};

PROFILER_API bool get_fwd_bwd_enabled();
PROFILER_API void set_fwd_bwd_enabled_fn(std::function<bool()> /*fn*/);
PROFILER_API void set_fwd_bwd_enabled_val(bool /*val*/);

PROFILER_API bool get_cuda_sync_enabled();
PROFILER_API void set_cuda_sync_enabled_fn(std::function<bool()> /*fn*/);
PROFILER_API void set_cuda_sync_enabled_val(bool /*val*/);

// Comms related RecordFunctions will record information about tensor storage
// locations.
PROFILER_API bool get_record_tensor_addrs_enabled();
PROFILER_API void set_record_tensor_addrs_enabled_fn(std::function<bool()> /*fn*/);
PROFILER_API void set_record_tensor_addrs_enabled_val(bool /*val*/);

}  // namespace profiler::profiler_impl::impl
