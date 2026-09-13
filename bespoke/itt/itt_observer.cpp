#include "bespoke/itt/itt_observer.h"

#include "bespoke/base/base.h"
#include "bespoke/base/thread_local_debug_info.h"
#include "bespoke/common/util.h"

namespace profiler::profiler_impl::impl
{

struct ITTThreadLocalState : ProfilerStateBase
{
    explicit ITTThreadLocalState(const ProfilerConfig& config) : ProfilerStateBase(config)
    {
        // Only `report_input_shapes` makes sense in this context.
        // PROFILER_CHECK(!config.profile_memory);
        // PROFILER_CHECK(!config.with_stack);
        // PROFILER_CHECK(!config.with_flops);
        // PROFILER_CHECK(!config.with_modules);
    }
    ~ITTThreadLocalState() override = default;

    ActiveProfilerType profilerType() override { return ActiveProfilerType::ITT; }

    void reportMemoryUsage(
        void* /*ptr*/,
        int64_t /*alloc_size*/,
        size_t /*total_allocated*/,
        size_t /*total_reserved*/,
        profiler::device_option /*device*/) override
    {
    }

    static ITTThreadLocalState* getTLS()
    {
        auto* tls = ProfilerStateBase::get(/*global=*/false);
        return static_cast<ITTThreadLocalState*>(tls);
    }
};

template <bool report_input_shapes>
static std::unique_ptr<profiler::ObserverContext> enterITT(const profiler::RecordFunction& fn)
{
    if (ITTThreadLocalState::getTLS() != nullptr)
    {
        profiler::profiler_impl::impl::ittStubs()->rangePush(fn.name());
    }
    return nullptr;
}

void pushITTCallbacks(
    const ProfilerConfig& config, const std::unordered_set<profiler::RecordScope>& scopes)
{
    if (!profiler::profiler_impl::impl::ittStubs()->enabled())
    {
        return;
    }

    profiler::thread_local_debug_info::_push(
        profiler::DebugInfoKind::PROFILER_STATE, std::make_shared<ITTThreadLocalState>(config));

    auto* state_ptr = ITTThreadLocalState::getTLS();
    // PROFILER_CHECK(state_ptr, "Expected profiler state set");

    auto handle = profiler::addThreadLocalCallback(
        profiler::RecordFunctionCallback(
            state_ptr->config().report_input_shapes ? &enterITT</*report_input_shapes=*/true>
                                                    : &enterITT</*report_input_shapes=*/false>,
            [](const profiler::RecordFunction&, profiler::ObserverContext*)
            { profiler::profiler_impl::impl::ittStubs()->rangePop(); })
            .needsInputs(config.report_input_shapes)
            .scopes(scopes));
    state_ptr->setCallbackHandle(handle);
}

}  // namespace profiler::profiler_impl::impl
