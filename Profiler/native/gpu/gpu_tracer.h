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

/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#pragma once

#include <memory>
#include <string_view>

#include "common/profiler_export.h"
#include "native/core/profiler_interface.h"
#include "native/core/profiler_options.h"
#include "native/gpu/gpu_event_collector.h"

namespace profiler::profiler_impl
{

/**
 * TensorFlow GPU profiler plugin (`GpuTracer` in
 * xla/backends/profiler/gpu/device_tracer_cuda.cc).
 *
 * Gated by `profile_options.device_tracer_level()`. Start enables
 * `annotation_stack` and the activity-tracer singleton; collect_data exports
 * `/device:GPU:N` via `gpu_trace_collector`.
 */
std::unique_ptr<profiler_interface> create_gpu_tracer(const profile_options& options);

/**
 * Dispatch a tiny device kernel and push it through `add_gpu_tracer_event`.
 *
 * Returns false (no native device-kernel probe in this port; CUDA/HIP GPU
 * activity stays on the Kineto path).
 */
PROFILER_API bool run_gpu_kernel_probe(std::string_view kernel_name = "profiler_gpu_probe");

}  // namespace profiler::profiler_impl
