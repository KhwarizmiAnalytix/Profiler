#include "bespoke/base/base.h"

#if PROFILER_HAS_METAL

#include <chrono>
#include <memory>

namespace profiler::profiler_impl::impl
{
namespace
{

// `ProfilerStubs::record()` takes no command-buffer/queue reference -- it is
// called generically from collection.cpp's begin_op()/onFunctionExit() for
// any PROFILER_RECORD_FUNCTION/PROFILER_RECORD_USER_SCOPE scope, the same call sites CUDA's
// stub uses. CUDA can turn that into a real GPU timestamp because
// `cudaEventRecord(event, /*stream=*/nullptr)` targets CUDA's always-present
// default stream; Metal has no equivalent implicit default queue a `record()`
// call could hook into without one being threaded through the interface.
// This stub is therefore a CPU-side monotonic-clock fallback -- it measures
// dispatch-to-dispatch wall time, not actual `MTLCommandBuffer` GPU
// start/end time. That is the same fallback tier `PrivateUse1` already
// represents for other vendor-agnostic backends; it is not a regression
// from a CUPTI/roctracer-grade merged device-activity trace, which Metal
// cannot reach at all (no CUPTI-equivalent exists on Apple platforms).
using metal_clock = std::chrono::steady_clock;

struct MetalMethods : public ProfilerStubs
{
    void record(int16_t* device, ProfilerVoidEventStub* event, int64_t* cpu_ns) const override
    {
        if (device != nullptr)
        {
            *device = 0;  // XSigma's Metal support targets a single GPU.
        }
        auto now = std::make_shared<metal_clock::time_point>(metal_clock::now());
        if (cpu_ns != nullptr)
        {
            *cpu_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          now->time_since_epoch())
                          .count();
        }
        *event = std::move(now);
    }

    float elapsed(
        const ProfilerVoidEventStub* event_, const ProfilerVoidEventStub* event2_) const override
    {
        const auto* start = static_cast<const metal_clock::time_point*>(event_->get());
        const auto* end   = static_cast<const metal_clock::time_point*>(event2_->get());
        const auto  us = std::chrono::duration_cast<std::chrono::microseconds>(*end - *start);
        // NOLINTNEXTLINE(cppcoreguidelines-narrowing-conversions)
        return static_cast<float>(us.count());
    }

    // No Metal marker-API equivalent to NVTX/ROCTX ranges. Instruments/
    // os_signpost integration is a separate, unrelated concern from GPU
    // timing correlation and is not wired here.
    void mark(const char* /*name*/) const override {}
    void rangePush(const char* /*name*/) const override {}
    void rangePop() const override {}

    void onEachDevice(std::function<void(int)> op) const override
    {
        op(0);  // Single Metal device, matching the rest of XSigma's Metal support.
    }

    void synchronize() const override
    {
        // No queue/command-buffer reference is available at this interface
        // boundary (see the class comment above) -- nothing to wait on.
    }

    bool enabled() const override { return true; }
};

struct RegisterMetalMethods
{
    RegisterMetalMethods()
    {
        static MetalMethods methods;
        // Metal is a generic vendor-agnostic fallback like any other
        // PrivateUse1-style backend -- reuses that slot rather than adding a
        // first-class device_enum/ActivityType/ProfilerState member (see
        // Docs/profiler/profiler.md, GPU section).
        registerPrivateUse1Methods(&methods);
    }
};
RegisterMetalMethods reg;

}  // namespace
}  // namespace profiler::profiler_impl::impl

#endif  // PROFILER_HAS_METAL
