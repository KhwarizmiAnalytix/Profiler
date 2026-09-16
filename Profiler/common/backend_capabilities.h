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

#pragma once

#include <string>
#include <vector>

#include "common/capture.h"
#include "common/profiler_export.h"

namespace profiler
{

/**
 * What this build/process can actually deliver, as opposed to what was
 * merely compiled in. `*_compiled` reflects `PROFILER_HAS_*` (build-time);
 * `*_available` additionally reflects a runtime probe where one exists (a
 * real CUDA/HIP-capable device via `cudaGetDeviceCount()`/equivalent) --
 * see design-review.md finding 2: a compiled-in NVTX/CUDA-fallback backend
 * was previously reported available even with no usable device.
 *
 * This does not discover CPU/Kineto/ITT device-level detail (Kineto's own
 * activity kinds, ITT task ranges); those need only the compiled-in check,
 * already exposed by kineto_enabled()/itt_enabled().
 */
struct backend_capabilities
{
    bool kineto_compiled = false;
    bool itt_compiled    = false;
    bool cuda_compiled   = false;
    bool hip_compiled    = false;
    bool nvtx_compiled   = false;

    /// True only when a real CUDA- or HIP-capable device was found at
    /// runtime, not just that the corresponding backend was compiled in.
    bool gpu_device_available = false;

    /// Human-readable reasons for anything above that's false, e.g.
    /// "cuda: compiled in, but no CUDA-capable device found".
    std::vector<std::string> unavailable_reasons;

    /// True when `act` can actually be captured right now (compiled in, and
    /// for activity::cuda/activity::hip, a real device was found).
    PROFILER_API bool supports(activity act) const;
};

/// Probes the current process once and returns the result. The result is
/// cached (device sets don't change over a process's lifetime, matching
/// bespoke/base/cuda.cpp's CUDAOrHIPMethods::enabled() caching), so this is
/// cheap to call repeatedly.
PROFILER_API const backend_capabilities& discover_backend_capabilities();

}  // namespace profiler
