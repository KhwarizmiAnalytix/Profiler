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

#include <cstdint>

#include "common/profiler_export.h"

namespace profiler
{

/// Clock calibration parameters: maps device timestamps to session-relative
/// CPU nanoseconds via cpu_ns = scale * device_ns + offset_ns (design-review.md
/// section 6.4 item 3). Derived from paired CPU/device samples via least-squares fit.
struct clock_calibration
{
    /// Scale factor (device ns per CPU ns, typically close to 1.0).
    double scale = 1.0;
    /// Offset (nanoseconds added after scaling).
    double offset_ns = 0.0;
    /// Measurement uncertainty (std. dev of residuals from the fit).
    double uncertainty_ns = 0.0;

    /// Apply the calibration: cpu_ns = scale * device_ns + offset_ns.
    uint64_t apply(uint64_t device_ns) const
    {
        return static_cast<uint64_t>(scale * static_cast<int64_t>(device_ns) + offset_ns);
    }
};

#if PROFILER_HAS_CUDA
/// Calibrate the CUDA device clock against the host CPU clock by sampling
/// paired (CPU time, device timestamp) around CUDA events. Returns calibration
/// parameters and uncertainty estimate.
/// Requires gpu_device_available == true; returns default (scale=1.0) on error.
PROFILER_API clock_calibration calibrate_cuda_device(int device_index);
#endif

}  // namespace profiler
