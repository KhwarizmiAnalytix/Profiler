#include "bespoke/common/collection.h"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <type_traits>
#include <utility>

#if PROFILER_HAS_KINETO
#include <libkineto.h>
#endif

#include "bespoke/common/record_function.h"
#include "bespoke/kineto/kineto_shim.h"
#include "common/flat_hash.h"
#include "common/irange.h"
#include "common/overloaded.h"

namespace profiler::profiler_impl::impl
{
using result_ptr_t = std::shared_ptr<Result>;
using trace_ptr_t  = std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper>;

// ============================================================================
// == Profiler Ops =============================================================
// ============================================================================

// ---------------------------------------------------
// |  Correlation ID tracking (OpList & EventBlock)  |
// ---------------------------------------------------
template <typename T, size_t ChunkSize>
ThreadLocalSubqueue::FunctionOpStorage::EventBlock<T, ChunkSize>::EventBlock()
{
    static std::atomic<uint64_t> counter_{0};
    id_start_ = 1 + (ChunkSize * counter_++);
}

template <class... Args>
std::pair<KinetoObserverContext::Event*, uint64_t>
ThreadLocalSubqueue::FunctionOpStorage::OpList::emplace_back(Args&&... args)
{
    auto event_ptr = AppendOnlyList::emplace_back(std::forward<Args>(args)...);
    auto corr_id   = buffer_last_->correlation_id(event_ptr);
    return {event_ptr, corr_id};
}

uint64_t ThreadLocalSubqueue::FunctionOpStorage::OpList::correlationID(const OpList::Iterator& e)
{
    return e.address().first->correlation_id(&*e);
}

template <typename T, size_t ChunkSize>
uint64_t ThreadLocalSubqueue::FunctionOpStorage::EventBlock<T, ChunkSize>::correlation_id(
    const T* ptr) const
{
    // PROFILER_CHECK_DEBUG(ptr >= this->data() && ptr < this->data() + ChunkSize);
    return id_start_ + (ptr - this->data());
}

// ---------------------------------
// |  Collection (Observer logic)  |
// ---------------------------------
std::unique_ptr<KinetoObserverContext> ThreadLocalSubqueue::begin_op(
    const profiler::RecordFunction& fn)
{
    const auto* overload_name =
        config_.experimental_config.capture_overload_names ? fn.overload_name() : "";
    std::vector<std::string> stack;
    if (config_.with_stack && fn.sourceFile() != nullptr && fn.sourceLine() != 0)
    {
        stack.emplace_back(std::string(fn.sourceFile()) + ":" + std::to_string(fn.sourceLine()));
    }
    auto [event, corr_id] =
        function_ops_.op_events_.emplace_back(profiler::profiler_impl::impl::FunctionOpBasicFields{
            fn.seqNr(),
            fn.forwardThreadId(),
            fn.scope(),
            fn.isAsync(),
            fn.handle(),
            fn.debugHandle(),
            fn.name(),
            overload_name,
            std::move(stack)});

    if (!config_.experimental_config.disable_external_correlation)
    {
        if (fn.scope() == profiler::RecordScope::USER_SCOPE)
        {
            profiler::profiler_impl::impl::kineto::pushUserCorrelationId(corr_id);
        }
        else
        {
            profiler::profiler_impl::impl::kineto::pushCorrelationId(corr_id);
        }
    }

    auto out = std::make_unique<KinetoObserverContext>(event);
    function_ops_.extra_meta_.emplace_back(fn.metadata());

    if (config_.state == ProfilerState::KINETO_GPU_FALLBACK)
    {
        out->fallback_ = function_ops_.device_fallback_.emplace_back();
        profiler::profiler_impl::impl::cudaStubs()->record(
            nullptr, &out->fallback_->device_event_start_, nullptr);
    }
    else if (config_.state == ProfilerState::KINETO_PRIVATEUSE1_FALLBACK)
    {
        out->fallback_ = function_ops_.device_fallback_.emplace_back();
        profiler::profiler_impl::impl::privateuse1Stubs()->record(
            nullptr, &out->fallback_->device_event_start_, nullptr);
    }

    event->start_time_ = profiler::getApproximateTime();
    if (!config_.experimental_config.performance_events.empty())
    {
        const size_t n   = config_.experimental_config.performance_events.size();
        event->counters_ = std::make_unique<perf_counters_t>(n, 0);
        perf_profiler_->Enable();
    }
    return out;
}

// ---------------
// |  Collation  |
// ---------------
namespace
{
template <typename T>
struct StealOrDefault
{
    explicit StealOrDefault(T& container) : container_{container}, it_{container.begin()} {}

    StealOrDefault(const StealOrDefault&)            = delete;
    StealOrDefault(StealOrDefault&&)                 = delete;
    StealOrDefault& operator=(const StealOrDefault&) = delete;
    StealOrDefault& operator=(StealOrDefault&&)      = delete;
    ~StealOrDefault() { container_.get().clear(); }

    typename T::Iterator::value_type operator()()
    {
        if (it_.exhausted())
        {
            return typename T::Iterator::value_type();
        }

        auto result = std::move(*it_);
        ++it_;
        return result;
    }

    std::reference_wrapper<T> container_;
    typename T::Iterator      it_;
};
}  // namespace

[[maybe_unused]] static constexpr std::string_view profilerStepString = "ProfilerStep#";

void ThreadLocalSubqueue::FunctionOpStorage::materialize(
    std::vector<std::shared_ptr<Result>>&                           out,
    std::vector<ProfilerStepInfo>&                                  step_info,
    const std::function<profiler::time_t(profiler::approx_time_t)>& time_converter,
    const uint64_t                                                  tid,
    const kineto::DeviceAndResource&                                kineto_info)
{
    auto extra_meta   = StealOrDefault<decltype(extra_meta_)>(extra_meta_);
    auto gpu_fallback = StealOrDefault<decltype(device_fallback_)>(device_fallback_);

    for (auto event = op_events_.begin(); event != op_events_.end(); ++event)
    {
        ExtraFields<EventType::FunctionOp> e{
            std::move(event->basic_fields_),
            ThreadLocalSubqueue::FunctionOpStorage::OpList::correlationID(event),
            time_converter(event->end_time_),
            extra_meta(),
            gpu_fallback(),
            std::move(event->counters_)};

        if (e.name_.find(profilerStepString) != std::string::npos)
        {
            step_info.emplace_back(
                time_converter(event->start_time_), time_converter(event->end_time_), out.size());
        }
        out.emplace_back(
            Result::create(time_converter(event->start_time_), tid, kineto_info, std::move(e)));
    }

    op_events_.clear();
}

namespace
{
// See `RecordQueue::getSubqueue()` for an overview of this cache.
struct SubQueueThreadCache
{
    uint32_t             key_;
    ThreadLocalSubqueue* ref_;
};

// The astute observer will note that this leaves a dangling reference; nothing
// in the teardown of `RecordQueue` or `ThreadLocalSubqueue` clears this value.
// (And the raw pointer in `SubQueueThreadCache` will not extend the lifetime
// of `*ref_`.) This is safe, however, because `getSubqueue` will check
// `sub_queue_cache_.key_` before attempting to access `ref_`, and if `key_`
// does not match the RecordQueue's *unique* `id_` it will evict
// `sub_queue_cache_` and fall back to a different mechanism.
std::atomic<uint32_t>            queue_id_{0};
thread_local SubQueueThreadCache sub_queue_cache_{0, nullptr};

#if PROFILER_HAS_KINETO
auto scopeToType(profiler::RecordScope scope)
{
    return scope == profiler::RecordScope::USER_SCOPE ? libkineto::ActivityType::USER_ANNOTATION
                                                      : libkineto::ActivityType::CPU_OP;
}
#endif  // PROFILER_HAS_KINETO

int64_t functionOpEndNS(
    const ExtraFields<EventType::FunctionOp>& e,
    const bool                                finished,
    const std::weak_ptr<Result>&              parent)
{
    if (finished && e.end_time_ns_ == std::numeric_limits<profiler::time_t>::min())
    {
        auto p = parent.lock();
        if (p)
        {
            return p->endTimeNS();
        }
    }
    return e.end_time_ns_;
}

auto kinetoEventCorrelationID(
    const ExtraFields<EventType::Kineto>& e, const std::weak_ptr<Result>& parent)
{
    if (e.correlation_id_ != 0u)
    {
        return e.correlation_id_;
    }
    auto p = parent.lock();
    return p ? p->correlationID() : 0;
}
}  // namespace

#define ATTRIBUTE(event_type, expr)                  \
    [&](const ExtraFields<EventType::event_type>& e) \
    {                                                \
        (void)e;                                     \
        return expr;                                 \
    }

std::string Result::name() const
{
    return visit(profiler::overloaded(
        ATTRIBUTE(Allocation, std::string("[memory]")),
        ATTRIBUTE(OutOfMemory, std::string("[OutOfMemory]")),
        [](const auto& e) -> std::string { return e.name_; }));
}

std::string Result::overload_name() const
{
    return visit(profiler::overloaded(
        ATTRIBUTE(FunctionOp, std::string(e.overload_name_)),
        [](const auto& /*e*/) -> std::string { return ""; }));
}

#if PROFILER_HAS_KINETO
kineto::activity_type_t Result::kinetoType() const
{
    return visit(profiler::overloaded(
        ATTRIBUTE(FunctionOp, scopeToType(e.scope_)),
        ATTRIBUTE(Backend, scopeToType(e.scope_)),
        ATTRIBUTE(Allocation, libkineto::ActivityType::CPU_INSTANT_EVENT),
        ATTRIBUTE(OutOfMemory, libkineto::ActivityType::CPU_INSTANT_EVENT),
        ATTRIBUTE(Kineto, e.activity_type_)));
}
#endif  // PROFILER_HAS_KINETO

uint64_t Result::correlationID() const
{
    return visit(profiler::overloaded(
        ATTRIBUTE(FunctionOp, e.correlation_id_),
        ATTRIBUTE(Kineto, kinetoEventCorrelationID(e, parent_)),
        [&](const auto&) -> uint64_t { return 0; }));
}

int64_t Result::endTimeNS() const
{
    auto end_time_ns = visit(profiler::overloaded(
        ATTRIBUTE(FunctionOp, functionOpEndNS(e, finished_, parent_)),
        ATTRIBUTE(Backend, e.end_time_us_ * 1000),
        ATTRIBUTE(Allocation, start_time_ns_),
        ATTRIBUTE(OutOfMemory, start_time_ns_),
        ATTRIBUTE(Kineto, start_time_ns_ + e.duration_ns_),
        [&](const auto& e) -> int64_t { return e.end_time_ns_; }));

    // In rare cases we're willing to tolerate ops which are missing an end time
    // so long as they can borrow their parent's end time. A consequence of this,
    // however, is that `endTimeNS` may not make sense until tree construction is
    // complete.
    auto end_time_is_valid = !finished_ || SOFT_ASSERT(end_time_ns >= start_time_ns_, name());
    return end_time_is_valid ? end_time_ns : start_time_ns_;
}

uint64_t Result::endTID() const
{
    return visit(profiler::overloaded(
        ATTRIBUTE(FunctionOp, e.end_tid_), [&](const auto&) -> uint64_t { return start_tid_; }));
}

profiler::device_enum Result::deviceType() const
{
    using profiler::profiler_impl::deviceTypeFromActivity;
    return visit(profiler::overloaded(
        ATTRIBUTE(Allocation, e.device_type_),
        ATTRIBUTE(OutOfMemory, e.device_type_),
#if PROFILER_HAS_KINETO
        ATTRIBUTE(Kineto, deviceTypeFromActivity(e.activity_type_)),
#else
        [](const ExtraFields<EventType::Kineto>&) { return profiler::device_enum::CPU; },
#endif
        [&](const auto&) { return profiler::device_enum::CPU; }));
}
#undef ATTRIBUTE

ThreadLocalSubqueue::ThreadLocalSubqueue(const uint64_t tid, ProfilerConfig config)
    : tid_{tid}, config_{std::move(config)}, kineto_info_{kineto::kineto_ids()}
{
    profiler::profiler_impl::impl::kineto::recordThreadInfo();
    if (!config_.experimental_config.performance_events.empty())
    {
        perf_profiler_ =
            std::make_unique<profiler::profiler_impl::impl::linux_perf::PerfProfiler>();
        perf_profiler_->Configure(config_.experimental_config.performance_events);
    }
}

RecordQueue::RecordQueue(ProfilerConfig config, std::set<ActivityType> activities)
    : id_(++queue_id_), config_{std::move(config)}, activities_{std::move(activities)}
{
    if (tracePython())
    {
        python_tracer_ = python_tracer::PythonTracerBase::make(this);
        if (getPythonGcEvents())
        {
            python_tracer_->register_gc_callback();
        }
    }
}

bool RecordQueue::tracePython() const
{
    return config_.with_stack && (activities_.count(ActivityType::CPU) > 0);  //NOLINT
}

bool RecordQueue::getPythonGcEvents() const
{
    return config_.experimental_config.record_python_gc_info;
}

ThreadLocalSubqueue* RecordQueue::getSubqueue()
{
    // In the most common case, a thread will want to write to the same sub-queue
    // that it wrote to last call. The only time that isn't true is if:
    //  A) The profiler context has ended and we are in a new one.
    //  B) Two profilers are active in different TLS contexts, and this thread
    //     is a worker helping with intra-op parallelism.
    // Since we expect this to be the OVERWHELMINGLY common case (>99%), we add a
    // special thread_local cache so that we can skip the overall `flat_hash_map`
    // (and corresponding lock).
    if (id_ == sub_queue_cache_.key_)
    {
        return sub_queue_cache_.ref_;
    }

    const auto             tid = profiler::RecordFunction::currentThreadId();
    std::scoped_lock const guard(sub_queue_mutex_);
    auto                   it = sub_queues_.find(tid);
    if (it == sub_queues_.end())
    {
        it = sub_queues_.emplace(tid, std::make_unique<ThreadLocalSubqueue>(tid, config_)).first;
    }

    sub_queue_cache_ = SubQueueThreadCache{id_, it->second.get()};
    return it->second.get();
}

void RecordQueue::stop()
{
    if (python_tracer_)
    {
        python_tracer_->stop();
    }
}

void RecordQueue::restart()
{
    if (python_tracer_)
    {
        python_tracer_->restart();
    }
}

namespace
{
void mark_finished(const std::shared_ptr<Result>& r)
{
    //PROFILER_CHECK(!r->finished_, r->name());
    r->finished_ = true;
    //PROFILER_CHECK(r->endTimeNS() >= r->start_time_ns_, r->name());
}

#if PROFILER_HAS_KINETO
// Assumption: Total threads number will not exceed 2^16-1, and total ops will
// not exceed 2^48 -1.
uint64_t getForwardThreadKey(uint64_t tid, uint64_t seqNr)
{
    return ((tid << 48) | (seqNr & (((static_cast<uint64_t>(1)) << 48) - 1)));
}

void generateForwardBackwardLink(
    const Result&                                                   profiler_result,
    uint64_t&                                                       fwd_bwd_link_id,
    libkineto::GenericTraceActivity&                                activity,
    std::unordered_map<uint64_t, libkineto::GenericTraceActivity*>& tidSeq2activity)
{
    const auto& extra_fields =
        std::get<ExtraFields<EventType::FunctionOp>>(profiler_result.extra_fields_);
    if (extra_fields.forward_tid_ > 0)
    {
        // act is backward op.
        uint64_t const key =
            getForwardThreadKey(extra_fields.forward_tid_, extra_fields.sequence_number_);
        auto iter = tidSeq2activity.find(key);
        if (iter != tidSeq2activity.end())
        {
            libkineto::GenericTraceActivity* fwd = iter->second;
            fwd->flow.start                      = true;
            activity.flow.id = fwd->flow.id = fwd_bwd_link_id;
            activity.flow.type = fwd->flow.type = libkineto::kLinkFwdBwd;
            ++fwd_bwd_link_id;

            // If there are multiple events that match this sequence/tid pair, we
            // should delete this entry in the map to avoid inserting multiple "end"
            // flow events.
            tidSeq2activity.erase(iter);
        }
    }
    else if (profiler_result.start_tid_ != 0)
    {
        // act is forward op.
        uint64_t const key =
            getForwardThreadKey(profiler_result.start_tid_, extra_fields.sequence_number_);
        // Assumption: Among all ops with same sequence number,
        // the one with biggest start time is most likely launching backward op.
        auto iter = tidSeq2activity.find(key);
        if (iter == tidSeq2activity.end())
        {
            tidSeq2activity[key] = &activity;
        }
        else
        {
            // Now the sequence number is only incremented on creating a "Node"
            // object for backward pass, by calling
            // "profiler::sequence_number::get_and_increment()". Among all ops with same
            // sequence number, the one with biggest startTime is the one launching
            // backward op.
            if (activity.startTime >= iter->second->startTime)
            {
                tidSeq2activity[key] = &activity;
            }
        }
    }
}
#endif  // PROFILER_HAS_KINETO

void generateForwardBackwardLinks(
    const std::unique_ptr<profiler::profiler_impl::impl::kineto::trace_t>& cpu_trace,
    const std::vector<std::shared_ptr<Result>>&                            results)
{
#if !PROFILER_HAS_KINETO
}
#else   // PROFILER_HAS_KINETO
    // PROFILER_CHECK(cpu_trace->activities.size() == results.size());

    // startThreadId_seqNum to pointer of activity.
    // Low-16bits of startThreadId and low-48bits seqNum are concatenated into
    // one uint64_t variable as key.

    std::unordered_map<uint64_t, libkineto::GenericTraceActivity*> tidSeq2activity;
    uint64_t                                                       fwd_bwd_link_id = 1;

    using result_activity_t = std::pair<Result*, libkineto::GenericTraceActivity*>;
    std::vector<result_activity_t> torch_events;

    for (const auto idx : profiler::irange(cpu_trace->activities.size()))
    {
        const auto& profiler_result = results[idx];
        auto&       activity        = cpu_trace->activities[idx];

        // add information about an associated forward op, if a sequence number
        // is available (e.g. during training)

        profiler_result->visit_if_base<ExtraFields<EventType::FunctionOp>>(
            [&](const auto& e)
            {
                if (e.sequence_number_ >= 0)
                {
                    torch_events.emplace_back(profiler_result.get(), activity.get());
                }
            });
    }

    // We need to visit the events in chronological order.
    // So we sort them by end_time_ns_ before processing.
    std::sort(
        torch_events.begin(),
        torch_events.end(),
        [](const result_activity_t& left, const result_activity_t& right)
        {
            auto left_end_time =
                std::get<ExtraFields<EventType::FunctionOp>>(left.first->extra_fields_)
                    .end_time_ns_;
            auto right_end_time =
                std::get<ExtraFields<EventType::FunctionOp>>(right.first->extra_fields_)
                    .end_time_ns_;
            return left_end_time < right_end_time;
        });

    for (auto& [profiler_result, activity] : torch_events)
    {
        generateForwardBackwardLink(*profiler_result, fwd_bwd_link_id, *activity, tidSeq2activity);
    }
}
#endif  // PROFILER_HAS_KINETO

constexpr const char* indexKey = "Ev Idx";

void passEventsToKineto(
    const std::vector<std::shared_ptr<Result>>& results,
    uint64_t                                    start_time_ns,
    uint64_t                                    end_time_ns,
    const ProfilerConfig&                       config)
{
    using namespace profiler::profiler_impl::impl::kineto;
    TraceWrapper cpu_trace(static_cast<int64_t>(start_time_ns), "Profiler Profiler");

    // Generate Kineto events for each event recorded by the Profiler profiler.
    for (const auto i : profiler::irange(results.size()))
    {
        const auto& e = results[i];
        // Here we are essentially setting the duration to -1 if the event never
        // ends. This way Kineto will extend the event to the end of the trace. This
        // is useful so that we can still have 0 duration events if necessary
        // without extension
        int64_t const act_end_time = std::max(e->endTimeNS(), e->start_time_ns_ - 1);
        std::string   name         = e->name();
        if (!e->overload_name().empty())
        {
            name = fmt::format("{}.{}", e->name(), e->overload_name());
        }
        auto* activity = cpu_trace.addCPUActivity(
            name,
#if PROFILER_HAS_KINETO
            e->kinetoType(),
#else
            kineto::activity_type_t{},
#endif
            e->kineto_info_,
            e->correlationID(),
            e->start_time_ns_,
            act_end_time);

        if (activity != nullptr)
        {
            addMetadata(activity, indexKey, std::to_string(i));
            if (config.with_stack)
            {
                e->visit(profiler::overloaded(
                    [&](const ExtraFields<EventType::FunctionOp>& fields)
                    {
                        if (!fields.stack_.empty())
                        {
                            addMetadataQuoted(
                                activity, "Call stack", stacksToStr(fields.stack_, ";"));
                        }
                    },
                    [](const auto&) {}));
            }

            // There is a longstanding regression for initializing
            // on-demand Kineto activity handling. Enabling this path
            // for Profiler API could cause side effects as much has changed since.
            // Make a surgical fix here until we holistically assess the on-demand
            // vs API path fragmentation, which has been snowballing in complexity
            // and thus flakiness.
            if (config.global())
            {
                e->kineto_activity_ = activity;
            }
        }
    }

    if (get_fwd_bwd_enabled())
    {
        generateForwardBackwardLinks(cpu_trace.get(), results);
    }

    // Kineto adds the events that it collected.
    cpu_trace.transferCpuTrace(static_cast<int64_t>(end_time_ns));
}

#if PROFILER_HAS_KINETO
// There are two mechanisms that we use to connect Profiler and Kineto events.
// The first is the correlation ID. The profiler pushes a unique integer at the
// start of an op and pops it at the end. Kineto then associates the events
// that it collects with that correlation ID and sets the linked activity of
// the events that it collected to point to the profiler op.
//
// However, this is not a sufficient description because it does not retain
// dependency information between kineto ops. Consider a call to `profiler.add`.
// Three events will be collected:
//   `aten::add`          (FunctionOp, collected by profiler)
//   `cudaLaunchKernel`   (CUDA runtime event, collected by Kineto)
//   `profiler::vectorized_...` (GPU kernel, collected by Kineto)
// If we only relied on correlation IDs we would set both Kineto events as
// children of the `profiler::add`, rather than the correct
//   `profiler::add -> cudaLaunchKernel -> profiler::vectorized_...`
//
// Kineto surfaces this information through a second concept called a "flow".
// In this example, the `cudaLaunchKernel` event is the start of a flow and the
// GPU kernel has the same flow id but is not a start event. Thus, when merging
// the Kineto events into the call tree we first add all events which are flow
// start nodes. We then merge the rest, trying to pair them with flow starts
// and falling back to correlation ID if necessary. For any nodes without
// linked events the caller is determined using the normal tree construction
// algorithm.
class TransferEvents
{
    using itrace_t   = libkineto::ITraceActivity;
    using activity_t = profiler::profiler_impl::impl::kineto::activity_t;

public:
    TransferEvents(
        std::vector<std::shared_ptr<Result>>& results,
        trace_ptr_t&                          trace,
        const ProfilerConfig&                 config)
        : results_{results}, config_{config}
    {
        const auto* trace_activities_ptr = trace->get()->activities();
        // PROFILER_CHECK(trace_activities_ptr != nullptr);
        trace_activities_ = *trace_activities_ptr;
        reassociate();
        extractEventsFromTrace();
        setParents();
    }

private:
    static long long extractIndex(const std::string& metadata_json)
    {
        static const auto prefix = fmt::format("\"{}\": ", indexKey);
        auto              pos    = metadata_json.find(prefix);
        return (pos == std::string::npos) ? unmatchedIndex : [&]()
        {
            auto end = metadata_json.find(',', pos);
            end      = (end == std::string::npos) ? metadata_json.size() : end;
            return std::stoll(metadata_json.substr(pos + prefix.size(), end));
        }();
    }

    std::shared_ptr<Result> lookup(const itrace_t* key)
    {
        if (key == nullptr)
        {
            return nullptr;
        }

        // First check the map.
        auto it = kineto_events_.find(key);
        if (it != kineto_events_.end())
        {
            return it->second;
        }

        // Then fallback to the encoded metadata.
        const auto index = extractIndex((key != nullptr) ? key->metadataJson() : "");
        if (index != unmatchedIndex)
        {
            auto out            = results_.get().at(index);
            kineto_events_[key] = out;
            return out;
        }

        // And finally give up.
        return nullptr;
    }

    void reassociate()
    {
        // Match profiler events with the corresponding kineto events. Kineto may
        // have moved or copied the activities, so we have to recover the
        // relationship between `libkineto::ITraceActivity` and `Result`.
        for (const auto* activity : trace_activities_)
        {
            // PROFILER_CHECK(activity != nullptr);
            auto e = lookup(activity);
            if (e != nullptr)
            {
                // PROFILER_CHECK(e->kineto_activity_ == nullptr);
                e->kineto_activity_ = static_cast<const activity_t*>(activity);
            }
        }
        /*if (results_.get().size() != kineto_events_.size())
        {
            PROFILER_LOG_WARNING(
                fmt::format(
                    "Failed to recover relationship between all profiler and kineto events: "
                    "{} vs. {}  reassociated.",
                    results_.get().size(),
                    kineto_events_.size()));
        }*/
    }

    static bool isHiddenEvent(const itrace_t* activity)
    {
        // PROFILER_CHECK(activity != nullptr);
        // Kineto uses "hidden" metadata to mark events that should be hidden.
        return activity->getMetadataValue("hidden") == "1";
    }

    static std::shared_ptr<Result> resultFromActivity(const itrace_t* activity)
    {
        // PROFILER_CHECK(activity != nullptr);

        // Kineto is inconsistent with types, so we have to cast to int32.
        profiler::profiler_impl::impl::kineto::DeviceAndResource const device_and_resource{
            static_cast<int32_t>(activity->deviceId()),
            static_cast<int32_t>(activity->resourceId())};

        auto event = Result::create(
            activity->timestamp(),
            noTID,  // Placeholder
            device_and_resource,
            ExtraFields<EventType::Kineto>{
                activity->name(),
                activity->duration(),
                static_cast<uint64_t>(activity->correlationId()),
                activity->type(),
                {/*id=*/static_cast<uint32_t>(activity->flowId()),
                 /*type=*/static_cast<uint32_t>(activity->flowType()),
                 /*start=*/static_cast<uint32_t>(activity->flowStart())},
                {},
                {}});
        event->hidden_ = isHiddenEvent(activity);
        // NB: It's tempting to set `event->kineto_activity_`; however we can only
        // guarantee that the events we passed to Kineto are of type
        // `GenericTraceActivity`. Others may derive from ITraceActivity and thus
        // are not safe to cast.
        return event;
    }

    std::shared_ptr<Result> toResult(const itrace_t* activity)
    {
        auto e = lookup(activity);

        // Until we are very sure that we can reassociate kineto and profiler
        // events we need to be very defensive.
        const auto type = activity->type();
        if (e == nullptr && (type == libkineto::ActivityType::CPU_OP ||
                             type == libkineto::ActivityType::CPU_INSTANT_EVENT ||
                             type == libkineto::ActivityType::USER_ANNOTATION ||
                             type == libkineto::ActivityType::PYTHON_FUNCTION))
        {
            /* PROFILER_LOG_WARNING(
                "Detected an event which was likely passed to kineto by the Profiler "
                "profiler, but is not present in the set of known events: ",
                activity->name(),
                " This most likely means that Kineto has not "
                "maintained address stability for this event. Please report this to "
                "the Profiler team.");
            */
            return nullptr;
        }

        if (e == nullptr)
        {
            e = resultFromActivity(activity);
            results_.get().push_back(e);
            kineto_events_[activity] = e;
        }
        return e;
    }

    void extractEventsFromTrace()
    {
        for (const auto* activity : trace_activities_)
        {
            auto e = toResult(activity);
            if (e)
            {
                if (config_.experimental_config.expose_kineto_event_metadata)
                {
                    e->visit(profiler::overloaded(
                        [&](ExtraFields<EventType::FunctionOp>& i)
                        { i.metadata_json_ = activity->metadataJson(); },
                        [&](ExtraFields<EventType::Kineto>& i)
                        { i.metadata_json_ = activity->metadataJson(); },
                        [](auto&) { return; }));
                }
                const auto* linked_activity = activity->linkedActivity();
                if (linked_activity != nullptr)
                {
                    e->visit(profiler::overloaded(
                        [&](ExtraFields<EventType::Kineto>& i)
                        { i.linked_activity_ = toResult(linked_activity); },
                        [](auto&) { /* PROFILER_CHECK(false); */ }));
                }
            }
        }
    }

    void setKinetoTID(std::shared_ptr<Result>& r, std::shared_ptr<Result> parent)
    {
        r->visit(profiler::overloaded(
            [&]([[maybe_unused]] ExtraFields<EventType::Kineto>& i)
            {
                // PROFILER_CHECK(r->start_tid_ == noTID);
                r->start_tid_ =
                    parent ? parent->start_tid_ : profiler::RecordFunction::currentThreadId();
            },
            [](auto&) {}));

        for (auto& child : r->children_)
        {
            setKinetoTID(child, r);
        }
    }

    void setParents()
    {
        // First pass: Collect start events and set parent to linked event.
        profiler::flat_hash_map<uint32_t, std::shared_ptr<Result>> flow_map;
        for (auto& e : results_.get())
        {
            // PROFILER_CHECK(e != nullptr);
            e->visit(profiler::overloaded(
                [&](const ExtraFields<EventType::Kineto>& i)
                {
                    if (i.flow.type == libkineto::kLinkAsyncCpuGpu && i.flow.start)
                    {
#ifdef PROFILER_USE_ROCM
                        auto inserted = flow_map.insert({i.flow.id, e});
                        if (inserted.second)
                        {
                            /* PROFILER_LOG_WARNING(
                                "ROCTracer produced duplicate flow start: ", i.flow.id);
                            */
                        }
#else
                        flow_map.insert({i.flow.id, e});
                    // PROFILER_CHECK(inserted.second);
#endif  // PROFILER_USE_ROCM
                    }
                    // PROFILER_CHECK(e->parent_.expired());
                    e->parent_ = i.linked_activity_;
                },
                [](const auto&) {}));
        }

        // Second pass
        for (auto& e : results_.get())
        {
            e->visit(profiler::overloaded(
                [&](const ExtraFields<EventType::Kineto>& i)
                {
                    // Flow takes priority over linked event.
                    const auto it = flow_map.find(i.flow.id);
                    if (it != flow_map.end() && i.flow.type == libkineto::kLinkAsyncCpuGpu &&
                        !i.flow.start)
                    {
                        e->parent_ = it->second;
                    }

                    // If a parent was set we have to do some bookkeeping.
                    auto parent = e->parent_.lock();
                    if (parent)
                    {
                        parent->children_.push_back(e);
                        mark_finished(e);
                    }
                },
                [](const auto&) {}));
        }

        // Set TIDs now that we have established lineage.
        for (auto& e : results_.get())
        {
            if (e->parent_.expired())
            {
                setKinetoTID(e, nullptr);
            }
        }
    }

    static constexpr long long unmatchedIndex = -1;
    static constexpr auto      noTID          = std::numeric_limits<uint64_t>::max();
    std::reference_wrapper<std::vector<std::shared_ptr<Result>>>      results_;
    const ProfilerConfig&                                             config_;
    std::vector<const itrace_t*>                                      trace_activities_;
    profiler::flat_hash_map<const itrace_t*, std::shared_ptr<Result>> kineto_events_;
};
#else
class TransferEvents
{
public:
    template <class... Args>
    TransferEvents(Args&&... /*unused*/)
    {
    }
};
#endif

trace_ptr_t addKinetoEvents(
    std::vector<std::shared_ptr<Result>>& results,
    uint64_t                              start_time_ns,
    uint64_t                              end_time_ns,
    const ProfilerConfig&                 config)
{
    using namespace profiler::profiler_impl::impl::kineto;
    passEventsToKineto(results, start_time_ns, end_time_ns, config);

    // In on demand mode kineto is directly controlled by other machinery.
    if (config.global())
    {
        return nullptr;
    }

    auto trace = std::make_unique<ActivityTraceWrapper>(stopTrace());
    //PROFILER_CHECK(trace || !kKinetoAvailable);
    // TransferEvents constructor has side effects (transfers Kineto events to results)
    // cppcheck-suppress unreadVariable
    TransferEvents const transfer{results, trace, config};
    return trace;
}

struct ResultGreater
{
    bool operator()(const result_ptr_t& a, const result_ptr_t& b) const
    {
        return a->endTimeNS() > b->endTimeNS();
    }
};

void set_in_tree_building(const std::vector<result_ptr_t>& /*results*/, const bool /*value*/) {}

void build_tree(std::vector<std::shared_ptr<Result>>& sorted_events)
{
    set_in_tree_building(sorted_events, true);

    using op_fields = ExtraFields<EventType::FunctionOp>;
    profiler::flat_hash_map<uint64_t, std::shared_ptr<Result>>                  stacks;
    std::priority_queue<result_ptr_t, std::vector<result_ptr_t>, ResultGreater> end_events_;

    auto push_event = [&stacks, &end_events_](std::shared_ptr<Result>& event)
    {
        // Kineto builds subtrees using correlation ids and flows, so some Kineto
        // events are already marked finished before the main tree building
        // algorithm. It's fine to ignore them; the root event of these subtrees
        // not a Kineto op and will be handled normally.
        if (std::holds_alternative<ExtraFields<EventType::Kineto>>(event->extra_fields_) &&
            event->finished_)
        {
            return;
        }

        // PROFILER_CHECK(event->parent_.expired());
        for ([[maybe_unused]] const auto& child : event->children_)
        {
            // PROFILER_CHECK(child->finished_);
        }
        // PROFILER_CHECK(!event->finished_);

        auto parent_it = stacks.find(event->start_tid_);
        if (parent_it == stacks.end())
        {
            auto fwd_tid = event->visit(profiler::overloaded(
                [](const op_fields& i) { return i.forward_tid_; },
                [](const auto&) -> uint64_t { return 0; }));
            if (fwd_tid)
            {
                parent_it = stacks.find(fwd_tid);
            }
        }

        if (parent_it != stacks.end())
        {
            event->parent_ = parent_it->second;
            parent_it->second->children_.push_back(event);
        }

        if (event->endTimeNS() > event->start_time_ns_)
        {
            stacks[event->start_tid_] = event;
            end_events_.push(event);
        }
        else if (event->endTimeNS() == std::numeric_limits<profiler::time_t>::min())
        {
            // We use min time to indicate the lack of a termination event, so if we
            // encounter such a case we don't push to `end_events_`.
            stacks[event->start_tid_] = event;
        }
        else
        {
            mark_finished(event);
        }
    };

    auto pop_event = [&stacks](std::shared_ptr<Result> event)
    {
        if (event->finished_)
        {
            // This event was marked finished by a previous `pop_event` call.
            return;
        }

        auto start_tid = event->start_tid_;
        auto frame     = stacks.at(start_tid);

        while (frame.get() != event.get())
        {
            // PROFILER_CHECK(frame != nullptr);
            mark_finished(frame);
            // PROFILER_CHECK(!frame->parent_.expired());
            frame = frame->parent_.lock();
        }

        mark_finished(event);
        stacks.erase(start_tid);
        auto new_frame = event->parent_.lock();
        if (new_frame != nullptr)
        {
            stacks[start_tid] = new_frame;
        }
    };

    // Stack replay loop.
    for (auto& event : sorted_events)
    {
        while (!end_events_.empty() && end_events_.top()->endTimeNS() < event->start_time_ns_)
        {
            pop_event(end_events_.top());
            end_events_.pop();
        }
        push_event(event);
    }

    // Cleanup remaining exit events.
    while (!end_events_.empty())
    {
        pop_event(end_events_.top());
        end_events_.pop();
    }

    set_in_tree_building(sorted_events, false);
}

/**
 * Adjust r's duration to be the max of its current duration and the sum of all
 * of its children's adjusted durations (keeping its start time the same)
 * (adjust all child durations recursively)
 */
int64_t adjust_durations_dfs(std::shared_ptr<Result>& r)
{
    if (SOFT_ASSERT(r != nullptr))
    {
        int64_t const original_duration       = r->endTimeNS() - r->start_time_ns_;
        int64_t       children_total_duration = std::accumulate(
            r->children_.begin(),
            r->children_.end(),
            int64_t{0},
            [](int64_t acc, std::shared_ptr<Result>& child)
            { return acc + adjust_durations_dfs(child); });

        if (children_total_duration > original_duration)
        {
            r->visit(profiler::overloaded(
                [&r, &children_total_duration](ExtraFields<EventType::FunctionOp>& i)
                { i.end_time_ns_ = r->start_time_ns_ + children_total_duration; },
                []([[maybe_unused]] ExtraFields<EventType::Allocation>& _)
                {
                    // Pass- Allocation events can't have children
                },
                [&](auto&)
                {
                    SOFT_ASSERT(
                        false,
                        "unexpected event type in mobile profiler adjust_durations_dfs: ",
                        r->name());
                }));
            return children_total_duration;
        }

        return original_duration;
    }

    return 0;
}

/**
 * 1) Adjust r's start time to be [new_start_time] (also adjusting end time and
      keeping duration the same)
 * 2) Recursively adjust r's children's start times, making them line up such
      that the last one ends at the same time as r
 * 3) Return r's final end time
 */
int64_t adjust_timestamps_dfs(std::shared_ptr<Result>& r, int64_t new_start_time)
{
    if (SOFT_ASSERT(r != nullptr))
    {
        if (r->start_time_ns_ != new_start_time)
        {
            // Adjust start time (keeping duration constant)
            r->visit(profiler::overloaded(
                [&r, &new_start_time](ExtraFields<EventType::FunctionOp>& i)
                { i.end_time_ns_ = new_start_time + (i.end_time_ns_ - r->start_time_ns_); },
                []([[maybe_unused]] ExtraFields<EventType::Allocation>& _)
                {
                    // Pass- No duration or end time to adjust
                },
                [&](auto&)
                {
                    SOFT_ASSERT(
                        false,
                        "unexpected event type in mobile profiler adjust_timestamps_dfs: ",
                        r->name());
                }));
            r->start_time_ns_ = new_start_time;
        }
        int64_t const children_total_duration = std::accumulate(
            r->children_.begin(),
            r->children_.end(),
            int64_t{0},
            [](int64_t acc, std::shared_ptr<Result>& child)
            { return acc + (child->endTimeNS() - child->start_time_ns_); });

        int64_t child_start_time = r->endTimeNS() - children_total_duration;
        for (std::shared_ptr<Result>& child : r->children_)
        {
            child_start_time = adjust_timestamps_dfs(child, child_start_time);
        }
    }
    return r->endTimeNS();
}

/**
 * Adjust timestamps and durations of nodes in [out] such that
 *  - Vulkan event timelines are synchronized with CPU event times
 *  - Parent event timelines fully contain their child timelines
 *  - No overlaps in timelines for nodes at the same depth
 */
void adjust_timestamps(std::vector<std::shared_ptr<Result>>& out)
{
    if (out.empty())
    {
        return;
    }

    int64_t min_start_time = out[0]->start_time_ns_;
    for (std::shared_ptr<Result>& r : out)
    {
        // Only begin traversal for root nodes.
        if (r->parent_.expired())
        {
            adjust_durations_dfs(r);
            min_start_time = adjust_timestamps_dfs(r, std::max(r->start_time_ns_, min_start_time));
        }
    }
}
}  // namespace

std::pair<
    std::vector<std::shared_ptr<Result>>,
    std::unique_ptr<profiler::profiler_impl::impl::kineto::ActivityTraceWrapper>>
RecordQueue::getRecords(
    std::function<profiler::time_t(profiler::approx_time_t)> time_converter,
    uint64_t                                                 start_time_ns,
    uint64_t                                                 end_time_ns)
{
    auto converter = [&](profiler::approx_time_t t)
    {
        return t == std::numeric_limits<profiler::approx_time_t>::min()
                   ? std::numeric_limits<profiler::time_t>::min()
                   : time_converter(t);
    };

    // Lambda that checks that only the right side of the base intersects with
    // ev_start and ev_end
    auto right_intersection_only = [&](ProfilerStepInfo base, int64_t ev_start, int64_t ev_end)
    {
        return (base.start_time_ns < ev_start) &&
               (base.end_time_ns <= ev_end && base.end_time_ns > ev_start);
    };
    std::vector<std::shared_ptr<Result>>        out;
    std::vector<python_tracer::CompressedEvent> python_enters;
    std::vector<ProfilerStepInfo>               step_info;
    for (auto& subqueue_it : sub_queues_)
    {
        auto& queue       = *subqueue_it.second;
        auto  materialize = [&](auto& events)
        {
            for (auto& i : events)
            {
                profiler::time_t start_time_ns = 0;
                if constexpr (std::is_same_v<
                                  std::remove_reference_t<decltype(i)>,
                                  ExtraFields<EventType::Backend>>)
                {
                    start_time_ns = i.start_time_us_ * 1000;
                }
                else
                {
                    start_time_ns = converter(i.start_time_);
                }
                out.emplace_back(Result::create(
                    /*start_time_ns_=*/start_time_ns,
                    /*start_tid_=*/queue.tid(),
                    /*kineto_info_=*/queue.kineto_info(),
                    /*extra_fields_=*/std::move(i)));
            }
            events.clear();
        };

        queue.function_ops_.materialize(
            out, step_info, converter, queue.tid(), queue.kineto_info());
        materialize(queue.backend_events_);
        for (auto& i : queue.allocations_)
        {
            out.emplace_back(Result::create(
                /*start_time_ns_=*/converter(i.start_time_),
                /*start_tid_=*/queue.tid(),
                /*kineto_info_=*/queue.kineto_info(),
                /*extra_fields_=*/ExtraFields<EventType::Allocation>(i)));
        }
        queue.allocations_.clear();
        materialize(queue.ooms_);

        // pythongc_ / py_calls_ are filled by a registered PythonTracerBase via
        // emplace_gc_call / emplace_py_call; the default NoOp leaves them empty.
        for (auto& i : queue.py_calls_)
        {
            python_enters.push_back(
                {i.first, queue.tid(), queue.kineto_info(), converter(i.second)});
        }
        queue.py_calls_.clear();
    }

    if (python_tracer_)
    {
        auto ev = python_tracer_->getEvents(
            converter, python_enters, static_cast<profiler::time_t>(end_time_ns));
        // Placeholder for if we run out of ProfilerStep annotations
        ProfilerStepInfo const defaultStep = {
            std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(), 0};
        size_t           step_idx = 0;
        ProfilerStepInfo step     = step_idx < step_info.size() ? step_info[step_idx] : defaultStep;
        for (const auto& i : ev)
        {
            // Only adjust timestamps if experimental config is enabled
            if (config_.experimental_config.adjust_profiler_step)
            {
                // If event has start time after step end time we can continue to the
                // next step
                while (i->start_time_ns_ > step.end_time_ns)
                {
                    step_idx++;
                    step = step_idx < step_info.size() ? step_info[step_idx] : defaultStep;
                }
                // If Step annotation starts before event and ends before event ends
                // with intersection then we move the lefthand side of the step
                // annotation to the event start time
                if (right_intersection_only(step, i->start_time_ns_, i->endTimeNS()))
                {
                    // NOLINTNEXTLINE(facebook-hte-LocalUncheckedArrayBounds)
                    auto const& currStepRes     = out[step.out_idx];
                    currStepRes->start_time_ns_ = i->start_time_ns_ + 1;
                    step_idx++;
                    step = step_idx < step_info.size() ? step_info[step_idx] : defaultStep;
                }
            }
            out.push_back(i);
        }
        python_tracer_.reset();
    }

    if (config_.experimental_config.adjust_timestamps)
    {
        std::stable_sort(
            out.begin(),
            out.end(),
            [](const auto& a, const auto& b) { return a->start_time_ns_ < b->start_time_ns_; });
        build_tree(out);
        adjust_timestamps(out);
        for (const auto& r : out)
        {
            r->parent_.reset();
            // Reset these so that second build_tree can happen
            r->finished_ = false;
            r->children_.clear();
        }
    }

    auto trace = addKinetoEvents(out, start_time_ns, end_time_ns, config_);

    std::stable_sort(
        out.begin(),
        out.end(),
        [](const auto& a, const auto& b) { return a->start_time_ns_ < b->start_time_ns_; });

    build_tree(out);
    return {out, std::move(trace)};
}

namespace
{
std::function<bool()>& fwd_bwd_enabled_fn()
{
    static std::function<bool()> fn = []() { return true; };
    return fn;
}
}  // namespace

bool get_fwd_bwd_enabled()
{
    return fwd_bwd_enabled_fn()();
}

void set_fwd_bwd_enabled_fn(std::function<bool()> fn)
{
    fwd_bwd_enabled_fn() = std::move(fn);
}

void set_fwd_bwd_enabled_val(bool val)
{
    fwd_bwd_enabled_fn() = [val]() { return val; };
}

namespace
{
std::function<bool()>& cuda_sync_enabled_fn()
{
    static std::function<bool()> fn = []() { return false; };
    return fn;
}
}  // namespace

bool get_cuda_sync_enabled()
{
    return cuda_sync_enabled_fn()();
}

void set_cuda_sync_enabled_fn(std::function<bool()> fn)
{
    cuda_sync_enabled_fn() = std::move(fn);
}

void set_cuda_sync_enabled_val(bool val)
{
    cuda_sync_enabled_fn() = [val]() { return val; };
}

namespace
{
std::function<bool()>& record_tensor_addrs_enabled()
{
    static std::function<bool()> fn = []() { return false; };
    return fn;
}
}  // namespace

bool get_record_tensor_addrs_enabled()
{
    static std::optional<bool> cached_record_tensor_addrs_enabled;
    if (!cached_record_tensor_addrs_enabled.has_value())
    {
        cached_record_tensor_addrs_enabled = record_tensor_addrs_enabled()();
    }
    return cached_record_tensor_addrs_enabled.value();
}

void set_record_tensor_addrs_enabled_fn(std::function<bool()> fn)
{
    record_tensor_addrs_enabled() = std::move(fn);
}

void set_record_tensor_addrs_enabled_val(bool val)
{
    record_tensor_addrs_enabled() = [val]() { return val; };
}
}  // namespace profiler::profiler_impl::impl
