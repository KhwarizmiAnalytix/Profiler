/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
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

    bool export_xspace(x_space* space, uint64_t end_gpu_ns);

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

    bool enable(gpu_trace_collector* collector);
    void disable();

    gpu_trace_collector* collector() const;
    bool                 is_recording() const;

    gpu_activity_tracer(const gpu_activity_tracer&)            = delete;
    gpu_activity_tracer& operator=(const gpu_activity_tracer&) = delete;

private:
    gpu_activity_tracer() = default;

    std::atomic<gpu_trace_collector*> collector_{nullptr};
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
