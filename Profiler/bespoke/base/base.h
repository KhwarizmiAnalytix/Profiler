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
    virtual void  record(int16_t* device, ProfilerVoidEventStub* event, int64_t* cpu_ns) const = 0;
    virtual float elapsed(
        const ProfilerVoidEventStub* event, const ProfilerVoidEventStub* event2) const = 0;
    virtual void mark(const char* name) const                                          = 0;
    virtual void rangePush(const char* name) const                                     = 0;
    virtual void rangePop() const                                                      = 0;
    virtual bool enabled() const { return false; }
    virtual void onEachDevice(std::function<void(int)> op) const = 0;
    virtual void synchronize() const                             = 0;
    virtual ~ProfilerStubs()                                     = default;
};

PROFILER_API void                 registerCUDAMethods(ProfilerStubs* stubs);
PROFILER_API const ProfilerStubs* cudaStubs();
PROFILER_API void                 registerITTMethods(ProfilerStubs* stubs);
PROFILER_API const ProfilerStubs* ittStubs();
PROFILER_API void                 registerPrivateUse1Methods(ProfilerStubs* stubs);
PROFILER_API const ProfilerStubs* privateuse1Stubs();

}  // namespace profiler::profiler_impl::impl
