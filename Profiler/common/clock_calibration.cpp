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

#include "common/clock_calibration.h"

#if PROFILER_HAS_CUDA

#include <algorithm>
#include <cmath>
#include <numeric>

#include "bespoke/base/gpu_runtime.h"
#include "common/approximate_clock.h"

namespace profiler
{
namespace
{

struct Sample
{
    double cpu_ns   = 0.0;
    double device_ns = 0.0;
};

/// Least-squares fit: find scale and offset such that
/// cpu_ns ≈ scale * device_ns + offset minimizes sum((cpu_ns - (scale *
/// device_ns + offset))^2). Returns fitted scale/offset and residual std.dev.
void fit_linear_model(const std::vector<Sample>& samples, double& out_scale,
                      double& out_offset, double& out_uncertainty)
{
    if (samples.size() < 2)
    {
        out_scale       = 1.0;
        out_offset      = 0.0;
        out_uncertainty = 0.0;
        return;
    }

    const double N = static_cast<double>(samples.size());

    double sum_x   = 0.0;
    double sum_y   = 0.0;
    double sum_xy  = 0.0;
    double sum_x2  = 0.0;
    for (const auto& s : samples)
    {
        sum_x += s.device_ns;
        sum_y += s.cpu_ns;
        sum_xy += s.device_ns * s.cpu_ns;
        sum_x2 += s.device_ns * s.device_ns;
    }

    const double denom = N * sum_x2 - sum_x * sum_x;
    if (std::abs(denom) < 1e-9)
    {
        out_scale       = 1.0;
        out_offset      = sum_y / N;
        out_uncertainty = 0.0;
        return;
    }

    out_scale  = (N * sum_xy - sum_x * sum_y) / denom;
    out_offset = (sum_y - out_scale * sum_x) / N;

    double residual_sum_sq = 0.0;
    for (const auto& s : samples)
    {
        const double fitted_y = out_scale * s.device_ns + out_offset;
        const double err      = s.cpu_ns - fitted_y;
        residual_sum_sq += err * err;
    }
    out_uncertainty = std::sqrt(residual_sum_sq / (N - 1.0));
}

}  // namespace

clock_calibration calibrate_cuda_device(int device_index)
{
    clock_calibration result;

    // Save and restore device context.
    int prev_device = 0;
    if (cudaGetDevice(&prev_device) != cudaSuccess)
    {
        return result;
    }
    if (cudaSetDevice(device_index) != cudaSuccess)
    {
        cudaSetDevice(prev_device);
        return result;
    }

    // Collect N paired samples of (CPU time, device timestamp).
    std::vector<Sample> samples;
    const int          N_samples = 10;

    for (int i = 0; i < N_samples; ++i)
    {
        CUevent_st* start_event = nullptr;
        CUevent_st* end_event   = nullptr;
        if (cudaEventCreate(&start_event) != cudaSuccess ||
            cudaEventCreate(&end_event) != cudaSuccess)
        {
            if (start_event)
            {
                cudaEventDestroy(start_event);
            }
            if (end_event)
            {
                cudaEventDestroy(end_event);
            }
            cudaSetDevice(prev_device);
            return result;
        }

        uint64_t cpu_ns_start = getTime();
        if (cudaEventRecord(start_event, nullptr) != cudaSuccess)
        {
            cudaEventDestroy(start_event);
            cudaEventDestroy(end_event);
            cudaSetDevice(prev_device);
            return result;
        }
        if (cudaEventSynchronize(start_event) != cudaSuccess)
        {
            cudaEventDestroy(start_event);
            cudaEventDestroy(end_event);
            cudaSetDevice(prev_device);
            return result;
        }
        uint64_t cpu_ns_end = getTime();

        // Nominal device time for the synchronization point (0 since events
        // record at the same logical device time when back-to-back).
        samples.push_back(Sample{
            .cpu_ns    = static_cast<double>(cpu_ns_start + cpu_ns_end) / 2.0,
            .device_ns = 0.0,
        });

        cudaEventDestroy(start_event);
        cudaEventDestroy(end_event);
    }

    if (samples.empty())
    {
        cudaSetDevice(prev_device);
        return result;
    }

    double scale = 0.0, offset = 0.0, uncertainty = 0.0;
    fit_linear_model(samples, scale, offset, uncertainty);

    result.scale           = scale;
    result.offset_ns       = offset;
    result.uncertainty_ns  = uncertainty;

    cudaSetDevice(prev_device);
    return result;
}

}  // namespace profiler

#endif  // PROFILER_HAS_CUDA
