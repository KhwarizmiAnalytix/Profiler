#pragma once

// CUDA / HIP runtime portability for the profiler's device-event fallback
// stub (bespoke/base/cuda.cpp). Call sites keep CUDA API spellings; HIP
// builds map them to hip*. Mirrors Library/Memory/gpu/gpu_runtime.h's
// technique so the two libraries stay consistent.
//
// Include only when PROFILER_HAS_CUDA || PROFILER_HAS_HIP.

#if PROFILER_HAS_CUDA

#include <cuda_runtime_api.h>

#elif PROFILER_HAS_HIP

#include <hip/hip_runtime.h>

using cudaError_t = hipError_t;
using cudaEvent_t = hipEvent_t;

#define cudaSuccess hipSuccess
#define cudaErrorInitializationError hipErrorNotInitialized
#define cudaGetErrorString hipGetErrorString
#define cudaGetDevice hipGetDevice
#define cudaSetDevice hipSetDevice
#define cudaGetDeviceCount hipGetDeviceCount
#define cudaEventCreate hipEventCreate
#define cudaEventDestroy hipEventDestroy
#define cudaEventRecord hipEventRecord
#define cudaEventSynchronize hipEventSynchronize
#define cudaEventElapsedTime hipEventElapsedTime
#define cudaDeviceSynchronize hipDeviceSynchronize

#else
#error "bespoke/base/gpu_runtime.h requires PROFILER_HAS_CUDA or PROFILER_HAS_HIP"
#endif
