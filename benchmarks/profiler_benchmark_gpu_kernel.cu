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

// Phase 4.E's GPU-active workload kernel, isolated in its own translation
// unit (see benchmarks/CMakeLists.txt's comment for why): profiler_benchmark.cpp
// overrides global operator new/delete with exception-throwing host code,
// which nvcc rejects if the whole file is compiled as CUDA.

#include "profiler_benchmark_gpu_kernel.h"

namespace
{
__global__ void benchmark_add_kernel(float* out, const float* a, const float* b, int n)
{
    int i = (blockIdx.x * blockDim.x) + threadIdx.x;
    if (i < n)
    {
        out[i] = a[i] + b[i];
    }
}
}  // namespace

void launch_benchmark_add_kernel(float* out, const float* a, const float* b, int n)
{
    dim3 const block(256);
    dim3 const grid((static_cast<unsigned>(n) + block.x - 1) / block.x);
    benchmark_add_kernel<<<grid, block>>>(out, a, b, n);
}
