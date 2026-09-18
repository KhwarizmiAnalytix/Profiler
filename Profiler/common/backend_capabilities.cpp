/*
 * Profiler
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/backend_capabilities.h"

#include "bespoke/base/base.h"

namespace profiler
{

bool backend_capabilities::supports(activity act) const
{
    switch (act)
    {
    case activity::cpu:
        return true;
    case activity::cuda:
        return cuda_compiled && gpu_device_available;
    case activity::hip:
        return hip_compiled && gpu_device_available;
    }
    return false;
}

const backend_capabilities& discover_backend_capabilities()
{
    // cudaStubs()->enabled() already does the real, cached cudaGetDeviceCount()
    // (or HIP equivalent) probe -- see bespoke/base/cuda.cpp's
    // CUDAOrHIPMethods::enabled(). Reuse it instead of duplicating the CUDA/HIP
    // runtime call here, keeping this file free of any CUDA/HIP headers.
    static const backend_capabilities caps = []()
    {
        backend_capabilities result;
        result.kineto_compiled = PROFILER_HAS_KINETO != 0;
        result.itt_compiled    = PROFILER_HAS_ITT != 0;
        result.cuda_compiled   = PROFILER_HAS_CUDA != 0;
        result.hip_compiled    = PROFILER_HAS_HIP != 0;
        result.nvtx_compiled   = PROFILER_HAS_NVTX != 0;

        // Only the CUDA/HIP event-fallback timing path (kineto_gpu_fallback)
        // and real device activity collection (activity::cuda/activity::hip)
        // need an actual device -- NVTX markers are a developer-tool
        // instrumentation call that "normally does nothing without an
        // attached developer tool" and must not require a usable CUDA device
        // (design-review.md section 5's evidence notes). Do not fold NVTX
        // into this check.
        //
        // cudaStubs() is defined in bespoke/base/cuda.cpp, which CMakeLists.txt
        // only compiles when PROFILER_ENABLE_KINETO or PROFILER_ENABLE_ITT is
        // set -- PROFILER_BACKEND=NONE (native-only) builds omit all of
        // bespoke/**, so the symbol doesn't exist there. result.cuda_compiled/
        // hip_compiled are runtime bool fields (not compile-time literals from
        // this expression's point of view), so the compiler can't fold the
        // never-taken branch away on its own like it does at capture.cpp's
        // `PROFILER_HAS_KINETO != 0 && cudaStubs()->...` call site; without
        // this #if, a NONE build fails to link with an undefined symbol.
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
        result.gpu_device_available =
            (result.cuda_compiled || result.hip_compiled) &&
            profiler::profiler_impl::impl::cudaStubs()->enabled();
#else
        result.gpu_device_available = false;
#endif

        if (!result.kineto_compiled)
        {
            result.unavailable_reasons.emplace_back("kineto: not compiled in (PROFILER_BACKEND != KINETO)");
        }
        if (!result.itt_compiled)
        {
            result.unavailable_reasons.emplace_back("itt: not compiled in (PROFILER_BACKEND != ITT)");
        }
        if (result.cuda_compiled && !result.gpu_device_available)
        {
            result.unavailable_reasons.emplace_back(
                "cuda: compiled in, but no CUDA-capable device found");
        }
        else if (!result.cuda_compiled)
        {
            result.unavailable_reasons.emplace_back("cuda: not compiled in (PROFILER_GPU_BACKEND != cuda)");
        }
        if (result.hip_compiled && !result.gpu_device_available)
        {
            result.unavailable_reasons.emplace_back(
                "hip: compiled in, but no HIP-capable device found");
        }
        else if (!result.hip_compiled)
        {
            result.unavailable_reasons.emplace_back("hip: not compiled in (PROFILER_GPU_BACKEND != hip)");
        }
        if (!result.nvtx_compiled)
        {
            result.unavailable_reasons.emplace_back(
                "nvtx: not compiled in (no CUDA toolkit NVTX headers found)");
        }
        return result;
    }();
    return caps;
}

}  // namespace profiler
