#include "bespoke/base/nvtx_observer.h"

#include "bespoke/base/base.h"
#include "bespoke/base/thread_local_debug_info.h"
#include "bespoke/common/util.h"

namespace profiler::profiler_impl::impl
{

namespace
{

struct NVTXThreadLocalState : ProfilerStateBase
{
    explicit NVTXThreadLocalState(const ProfilerConfig& config) : ProfilerStateBase(config)
    {
        // Only `report_input_shapes` makes sense in this context.
        // PROFILER_CHECK(!config.profile_memory);
        // PROFILER_CHECK(!config.with_stack);
        // PROFILER_CHECK(!config.with_flops);
        // PROFILER_CHECK(!config.with_modules);
    }
    ~NVTXThreadLocalState() override = default;

    ActiveProfilerType profilerType() override { return ActiveProfilerType::NVTX; }

    void reportMemoryUsage(
        void* /*ptr*/,
        int64_t /*alloc_size*/,
        size_t /*total_allocated*/,
        size_t /*total_reserved*/,
        profiler::device_option /*device*/) override
    {
    }

    static NVTXThreadLocalState* getTLS()
    {
        auto* tls = ProfilerStateBase::get(/*global=*/false);
        return static_cast<NVTXThreadLocalState*>(tls);
    }
};

}  // anonymous namespace

// XSigma has no tensor type, so there is no way to correlate an op's inputs
// back to a producing op's output identity -- this always returns an empty
// producer-op list.
static std::list<std::pair<profiler::RecordFunctionHandle, int>> getInputTensorOpIds()
{
    return {};
}

template <bool report_input_shapes>
static std::unique_ptr<profiler::ObserverContext> enterNVTX(const profiler::RecordFunction& fn)
{
    if (NVTXThreadLocalState::getTLS() != nullptr)
    {
        auto input_op_ids = getInputTensorOpIds();
        profiler::profiler_impl::impl::cudaStubs()->rangePush(
            profiler::profiler_impl::impl::getNvtxStr(
                fn.name(),
                fn.seqNr(),
                report_input_shapes ? profiler::profiler_impl::impl::inputSizes(fn, true)
                                    : std::vector<std::vector<int64_t>>(),
                fn.handle(),
                report_input_shapes ? input_op_ids
                                    : std::list<std::pair<profiler::RecordFunctionHandle, int>>())
                .c_str());
    }
    return nullptr;
}

void pushNVTXCallbacks(
    const ProfilerConfig& config, const std::unordered_set<profiler::RecordScope>& scopes)
{
    profiler::thread_local_debug_info::_push(
        profiler::DebugInfoKind::PROFILER_STATE, std::make_shared<NVTXThreadLocalState>(config));

    auto* state_ptr = NVTXThreadLocalState::getTLS();
    // PROFILER_CHECK(state_ptr, "Expected profiler state set");

    auto handle = profiler::addThreadLocalCallback(
        profiler::RecordFunctionCallback(
            state_ptr->config().report_input_shapes ? &enterNVTX</*report_input_shapes=*/true>
                                                    : &enterNVTX</*report_input_shapes=*/false>,
            [](const profiler::RecordFunction& /*fn*/, profiler::ObserverContext* /*ctx*/)
            { profiler::profiler_impl::impl::cudaStubs()->rangePop(); })
            .needsInputs(config.report_input_shapes)
            .needsOutputs(config.report_input_shapes)
            .needsIds(true)
            .scopes(scopes));
    state_ptr->setCallbackHandle(handle);
}

}  // namespace profiler::profiler_impl::impl
