#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <sstream>

#include "bespoke/base/base.h"

#if PROFILER_HAS_CUDA || PROFILER_HAS_HIP

#if PROFILER_HAS_CUDA
#ifndef ROCM_ON_WINDOWS
#if PROFILER_HAS_NVTX
#ifdef PROFILER_CUDA_USE_NVTX3
#include <nvtx3/nvtx3.hpp>
#else
#include <nvToolsExt.h>
#endif
#endif  // PROFILER_HAS_NVTX
#endif  // ROCM_ON_WINDOWS
#elif PROFILER_HAS_HIP
#if PROFILER_HAS_ROCTX
#include <roctx.h>
#endif
#endif  // PROFILER_HAS_CUDA / PROFILER_HAS_HIP

#include "bespoke/base/gpu_runtime.h"
#include "bespoke/common/util.h"
#include "common/approximate_clock.h"
#include "common/irange.h"
#include "common/profiler_macros.h"

namespace profiler::profiler_impl::impl
{
namespace
{

void cudaCheck(cudaError_t result, const char* file, int line)
{
    if (result != cudaSuccess)
    {
        std::stringstream ss;
        ss << file << ":" << line << ": ";
        if (result == cudaErrorInitializationError)
        {
            // It is common for users to use DataLoader with multiple workers
            // and the autograd profiler. Throw a nice error message here.
            ss << "CUDA initialization error. "
               << "This can occur if one runs the profiler in CUDA mode on code "
               << "that creates a DataLoader with num_workers > 0. This operation "
               << "is currently unsupported; potential workarounds are: "
               << "(1) don't use the profiler in CUDA mode or (2) use num_workers=0 "
               << "in the DataLoader or (3) Don't profile the data loading portion "
               << "of your code. https://github.com/pytorch/pytorch/issues/6313 "
               << "tracks profiler support for multi-worker DataLoader.";
        }
        else
        {
            ss << cudaGetErrorString(result);
        }
        // PROFILER_CHECK(false, ss.str());
    }
}
#define PROFILER_CUDA_CHECK(result) cudaCheck(result, __FILE__, __LINE__);

// Minimal RAII device guard: saves the current CUDA device on construction
// and restores it on destruction. XSigma has no cross-library "current
// stream" pool (unlike PyTorch's `at::cuda::CUDAGuard`/`getCurrentCUDAStream`,
// which this file mirrors), so `record()` below deliberately records on the
// default per-thread stream rather than a pooled one.
class ScopedCUDADeviceGuard
{
public:
    explicit ScopedCUDADeviceGuard(int device)
    {
        PROFILER_CUDA_CHECK(cudaGetDevice(&previous_device_));
        if (device != previous_device_)
        {
            PROFILER_CUDA_CHECK(cudaSetDevice(device));
            changed_ = true;
        }
    }
    ~ScopedCUDADeviceGuard()
    {
        if (changed_)
        {
            PROFILER_CUDA_CHECK(cudaSetDevice(previous_device_));
        }
    }
    ScopedCUDADeviceGuard(const ScopedCUDADeviceGuard&)            = delete;
    ScopedCUDADeviceGuard& operator=(const ScopedCUDADeviceGuard&) = delete;

private:
    int  previous_device_{0};
    bool changed_{false};
};

struct CUDAOrHIPMethods : public ProfilerStubs
{
    void record(int16_t* device, ProfilerVoidEventStub* event, int64_t* cpu_ns) const override
    {
        if (device)
        {
            int current_device = 0;
            PROFILER_CUDA_CHECK(cudaGetDevice(&current_device));
            *device = static_cast<int16_t>(current_device);
        }
        CUevent_st* cuda_event_ptr{nullptr};
        PROFILER_CUDA_CHECK(cudaEventCreate(&cuda_event_ptr));
        *event = std::shared_ptr<CUevent_st>(
            cuda_event_ptr, [](CUevent_st* ptr) { PROFILER_CUDA_CHECK(cudaEventDestroy(ptr)); });
        if (cpu_ns)
        {
            *cpu_ns = profiler::getTime();
        }
        // Record on the default per-thread stream: see ScopedCUDADeviceGuard's
        // comment above for why this is simplified relative to PyTorch's
        // pooled-stream equivalent.
        PROFILER_CUDA_CHECK(cudaEventRecord(cuda_event_ptr, /*stream=*/nullptr));
    }

    float elapsed(
        const ProfilerVoidEventStub* event_, const ProfilerVoidEventStub* event2_) const override
    {
        auto event  = (const ProfilerEventStub*)(event_);
        auto event2 = (const ProfilerEventStub*)(event2_);
        PROFILER_CUDA_CHECK(cudaEventSynchronize(event->get()));
        PROFILER_CUDA_CHECK(cudaEventSynchronize(event2->get()));
        float ms = 0;
        PROFILER_CUDA_CHECK(cudaEventElapsedTime(&ms, event->get(), event2->get()));
        // NOLINTNEXTLINE(bugprone-narrowing-conversions,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-narrowing-conversions)
        return ms * 1000.0;
    }

#if PROFILER_HAS_CUDA
#ifndef ROCM_ON_WINDOWS
#if PROFILER_HAS_NVTX
    void mark(const char* name) const override { ::nvtxMark(name); }

    void rangePush(const char* name) const override { ::nvtxRangePushA(name); }

    void rangePop() const override { ::nvtxRangePop(); }
#else
    void mark(PROFILER_UNUSED const char* name) const override {}

    void rangePush(PROFILER_UNUSED const char* name) const override {}

    void rangePop() const override {}
#endif  // PROFILER_HAS_NVTX
#else   // ROCM_ON_WINDOWS
    static void printUnavailableWarning()
    {
        // PROFILER_LOG_WARNING("Warning: roctracer isn't available on Windows");
    }
    void mark(const char* name) const override { printUnavailableWarning(); }
    void rangePush(const char* name) const override { printUnavailableWarning(); }
    void rangePop() const override { printUnavailableWarning(); }
#endif  // ROCM_ON_WINDOWS
#elif PROFILER_HAS_HIP
#if PROFILER_HAS_ROCTX
    // ROCm's marker API (ROCTX) is a different library from NVTX -- not a
    // macro-alias situation like the cuda*/hip* runtime calls above.
    void mark(const char* name) const override { ::roctxMark(name); }

    void rangePush(const char* name) const override { ::roctxRangePushA(name); }

    void rangePop() const override { ::roctxRangePop(); }
#else
    void mark(PROFILER_UNUSED const char* name) const override {}

    void rangePush(PROFILER_UNUSED const char* name) const override {}

    void rangePop() const override {}
#endif  // PROFILER_HAS_ROCTX
#endif  // PROFILER_HAS_CUDA / PROFILER_HAS_HIP

    void onEachDevice(std::function<void(int)> op) const override
    {
        int device_count = 0;
        PROFILER_CUDA_CHECK(cudaGetDeviceCount(&device_count));
        for (const auto i : profiler::irange(device_count))
        {
            ScopedCUDADeviceGuard device_guard(i);
            op(i);
        }
    }

    void synchronize() const override { PROFILER_CUDA_CHECK(cudaDeviceSynchronize()); }

    bool enabled() const override { return true; }
};

struct RegisterCUDAOrHIPMethods
{
    RegisterCUDAOrHIPMethods()
    {
        static CUDAOrHIPMethods methods;
        registerCUDAMethods(&methods);
    }
};
RegisterCUDAOrHIPMethods reg;

}  // namespace
}  // namespace profiler::profiler_impl::impl

#endif  // PROFILER_HAS_CUDA || PROFILER_HAS_HIP
