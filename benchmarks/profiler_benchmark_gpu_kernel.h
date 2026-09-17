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

// Plain C++ declaration (no CUDA syntax) so profiler_benchmark.cpp -- built
// by the host compiler, not nvcc -- can call into the kernel defined in
// profiler_benchmark_gpu_kernel.cu without itself needing to be CUDA source.
void launch_benchmark_add_kernel(float* out, const float* a, const float* b, int n);
