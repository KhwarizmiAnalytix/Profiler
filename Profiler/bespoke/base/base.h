#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "common/profiler_export.h"

#if PROFILER_HAS_HIP
// HIP's opaque event type (`hipEvent_t` is `ihipEvent_t*`). Forward-declared
// only -- this header must not pull in <hip/hip_runtime.h>, same as it never
// pulled in <cuda_runtime_api.h> for the CUDA case below.
struct ihipEvent_t;
using CUevent_st = ihipEvent_t;
#else
struct CUevent_st;
#endif

namespace profiler::profiler_impl::impl
{

// ----------------------------------------------------------------------------
// -- Annotation --------------------------------------------------------------
// ----------------------------------------------------------------------------
using ProfilerEventStub     = std::shared_ptr<CUevent_st>;
using ProfilerVoidEventStub = std::shared_ptr<void>;

struct PROFILER_VISIBILITY ProfilerStubs
{
    /// Record a CPU/GPU event. on_device: out param for the device (-1 if N/A).
    /// Sets event to shared_ptr to the recorded event. cpu_ns: out param for the
    /// CPU timestamp of the record call. Default stream; optional stream binding
    /// via record_with_stream() added in Phase 4.
    virtual void record(int16_t* device, ProfilerVoidEventStub* event, int64_t* cpu_ns) const = 0;

    /// Query elapsed time between two recorded events, blocking on completion.
    /// Returns time in microseconds. Phase 4 adds elapsed_nonblocking() for
    /// deferred queries (design-review.md section 6.6).
    virtual float elapsed(
        const ProfilerVoidEventStub* event, const ProfilerVoidEventStub* event2) const = 0;

    virtual void mark(const char* name) const                                          = 0;
    virtual void rangePush(const char* name) const                                     = 0;
    virtual void rangePop() const                                                      = 0;
    virtual bool enabled() const { return false; }
    virtual void onEachDevice(std::function<void(int)> op) const = 0;
    virtual void synchronize() const                             = 0;

    /// Record an event on an explicit stream (Phase 4, design-review.md section
    /// 6.6). stream_ptr can be nullptr to use the default stream. Defaults to
    /// record(device, event, cpu_ns) if not overridden (preserves old behavior).
    virtual void record_with_stream(void* stream_ptr, int16_t* device,
                                    ProfilerVoidEventStub* event, int64_t* cpu_ns) const
    {
        (void)stream_ptr;
        record(device, event, cpu_ns);
    }

    /// Query elapsed time non-blockingly (Phase 4, design-review.md section 6.6).
    /// Returns elapsed microseconds if both events are complete, or -1 if not ready
    /// (vs. blocking elapsed() which waits). Default: -1 (not implemented).
    virtual float elapsed_nonblocking(
        const ProfilerVoidEventStub* event, const ProfilerVoidEventStub* event2) const
    {
        (void)event;
        (void)event2;
        return -1.0f;
    }

    virtual ~ProfilerStubs() = default;
};

PROFILER_API void                 registerCUDAMethods(ProfilerStubs* stubs);
PROFILER_API const ProfilerStubs* cudaStubs();
PROFILER_API void                 registerITTMethods(ProfilerStubs* stubs);
PROFILER_API const ProfilerStubs* ittStubs();
PROFILER_API void                 registerPrivateUse1Methods(ProfilerStubs* stubs);
PROFILER_API const ProfilerStubs* privateuse1Stubs();

}  // namespace profiler::profiler_impl::impl
