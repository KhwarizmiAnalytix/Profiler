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

#include <cstdint>
#include <string>

namespace profiler::profiler_impl
{

/**
 * Device-side activity kinds collected by TensorFlow's GPU tracer
 * (`CuptiTracerEventType` in xla/backends/profiler/gpu).
 */
enum class gpu_tracer_event_type : uint8_t
{
    unsupported = 0,
    kernel,
    memcpy_h2d,
    memcpy_d2h,
    memcpy_d2d,
    memset,
};

/**
 * One GPU activity record (`CuptiTracerEvent`).
 *
 * Produced by the device activity backend (e.g. CUPTI callbacks on CUDA) and
 * consumed by `gpu_trace_collector`.
 */
struct gpu_tracer_event
{
    gpu_tracer_event_type type           = gpu_tracer_event_type::kernel;
    uint32_t              device_id      = 0;
    uint32_t              stream_id      = 0;
    uint32_t              correlation_id = 0;
    uint64_t              start_time_ns  = 0;
    uint64_t              end_time_ns    = 0;
    std::string           name;
    std::string           annotation;
};

}  // namespace profiler::profiler_impl
