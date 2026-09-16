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

/* Copyright 2020 The OpenXLA Authors.

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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "common/profiler_export.h"
#include "native/gpu/gpu_tracer_event.h"

namespace profiler
{
class x_space;
}  // namespace profiler

namespace profiler::profiler_impl
{

/**
 * TensorFlow `CuptiTraceCollector`: stores GPU activities and exports
 * `/device:GPU:<ordinal>` XPlanes.
 *
 * Producer: device activity backend (`AddEvent`).
 * Consumer: `gpu_tracer::collect_data`.
 */
class PROFILER_VISIBILITY gpu_trace_collector
{
public:
    gpu_trace_collector() = default;

    void add_event(gpu_tracer_event&& event);

    /// Export GPU events to XSpace, filtering out stale events from prior runs
    /// (those whose generation doesn't match current_generation). Counts dropped
    /// events in *out_stale_event_count if provided.
    bool export_xspace(x_space* space, uint64_t end_gpu_ns,
                       uint64_t current_generation = 0,
                       uint64_t* out_stale_event_count = nullptr);

private:
    std::mutex                    mu_;
    std::vector<gpu_tracer_event> events_;
};

/**
 * TensorFlow `CuptiTracer` singleton: at most one GPU tracing session.
 *
 * `enable` / `disable` are called from `gpu_tracer::start` / `stop`. Activity
 * backends push events through `add_gpu_tracer_event`.
 */
class PROFILER_VISIBILITY gpu_activity_tracer
{
public:
    static gpu_activity_tracer& get();

    // Takes shared ownership so a producer's local copy (see collector(), used by
    // add_gpu_tracer_event) keeps the collector alive for the duration of its call
    // even if disable() runs concurrently -- a bare atomic<gpu_trace_collector*> had
    // no lifetime lease and could hand out a pointer that's freed mid-call.
    bool enable(std::shared_ptr<gpu_trace_collector> collector);
    void disable();

    std::shared_ptr<gpu_trace_collector> collector() const;
    bool                                 is_recording() const;

    gpu_activity_tracer(const gpu_activity_tracer&)            = delete;
    gpu_activity_tracer& operator=(const gpu_activity_tracer&) = delete;

private:
    gpu_activity_tracer() = default;

    mutable std::mutex                   mu_;
    std::shared_ptr<gpu_trace_collector> collector_;
};

/**
 * Producer-side entry (`CuptiTraceCollector::AddEvent`).
 *
 * No-op unless `gpu_tracer` is started. Fills correlation / annotation from
 * `annotation_stack` when the caller leaves them empty (CUPTI callback analog).
 */
PROFILER_API void add_gpu_tracer_event(gpu_tracer_event event);

PROFILER_API bool gpu_tracer_is_recording();

}  // namespace profiler::profiler_impl
