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

#include "native/gpu/gpu_tracer.h"

#include <memory>

#include "native/cpu/annotation_stack.h"
#include "native/gpu/gpu_event_collector.h"
#include "native/tracing/traceme.h"

#ifndef PROFILER_HAS_METAL
#define PROFILER_HAS_METAL 0
#endif

namespace profiler::profiler_impl
{
namespace
{

// TensorFlow `GpuTracer` (device_tracer_cuda.cc): ProfilerInterface wrapping
// CuptiTracer::Enable/Disable + CuptiTraceCollector::Export.
class gpu_tracer : public profiler_interface
{
public:
    profiler_status start() override
    {
        if (recording_)
        {
            return profiler_status::Error("Another profile session running.");
        }
        collector_ = std::make_unique<gpu_trace_collector>();
        if (!gpu_activity_tracer::get().enable(collector_.get()))
        {
            collector_.reset();
            return profiler_status::Error("Another profile session running.");
        }
        annotation_stack::enable(true);
        recording_ = true;
        return profiler_status::Ok();
    }

    profiler_status stop() override
    {
        if (!recording_)
        {
            return profiler_status::Error("GpuTracer not started");
        }
        gpu_activity_tracer::get().disable();
        annotation_stack::enable(false);
        recording_ = false;
        return profiler_status::Ok();
    }

    profiler_status collect_data(x_space* space) override
    {
        if (recording_)
        {
            return profiler_status::Error("GpuTracer not stopped");
        }
        if (collector_ == nullptr)
        {
            return profiler_status::Ok();
        }
        auto const end_ns = static_cast<uint64_t>(get_current_time_nanos());
        if (!collector_->export_xspace(space, end_ns))
        {
            return profiler_status::Error("Failed to export GPU XPlane");
        }
        return profiler_status::Ok();
    }

    ~gpu_tracer() override
    {
        if (recording_)
        {
            // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
            // cppcheck-suppress virtualCallInConstructor
            gpu_tracer::stop();
        }
    }

private:
    bool                                 recording_ = false;
    std::unique_ptr<gpu_trace_collector> collector_;
};

}  // namespace

std::unique_ptr<profiler_interface> create_gpu_tracer(const profile_options& options)
{
    if (options.device_tracer_level() == 0)
    {
        return nullptr;
    }
    const auto device_type = options.device_type();
    if (device_type != profile_options::device_type_enum::UNSPECIFIED &&
        device_type != profile_options::device_type_enum::CPU &&
        device_type != profile_options::device_type_enum::GPU)
    {
        return nullptr;
    }
    return std::make_unique<gpu_tracer>();
}

#if !PROFILER_HAS_METAL
bool run_gpu_kernel_probe(std::string_view /*kernel_name*/)
{
    return false;
}
#endif

}  // namespace profiler::profiler_impl
