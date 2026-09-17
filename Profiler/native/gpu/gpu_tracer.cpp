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

#include "native/gpu/gpu_tracer.h"

#include <memory>

#include "native/cpu/annotation_stack.h"
#include "native/gpu/gpu_event_collector.h"
#include "native/session/profiler.h"
#include "native/tracing/traceme.h"

#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
#include "bespoke/base/base.h"
#endif

#if PROFILER_HAS_CUDA
#include "common/clock_calibration.h"
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
        collector_ = std::make_shared<gpu_trace_collector>();
        if (!gpu_activity_tracer::get().enable(collector_))
        {
            collector_.reset();
            return profiler_status::Error("Another profile session running.");
        }
#if PROFILER_HAS_CUDA
        // Warm the per-device calibration cache (common/clock_calibration.h)
        // at session start rather than paying its device round-trip cost
        // lazily during event export.
        cached_cuda_device_calibration(/*device_index=*/0);
#endif
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
        // The same process-wide counter add_gpu_tracer_event() stamped every
        // event of this run with (native/gpu/gpu_event_collector.cpp);
        // profiler_session::stop() calls collect_data() before starting any
        // other session, so this is still the generation of the run being
        // exported here.
        uint64_t const current_generation = profiler_session::current_capture_generation();
        if (!collector_->export_xspace(space, end_ns, current_generation))
        {
            return profiler_status::Error("Failed to export GPU XPlane");
        }
#if PROFILER_HAS_KINETO || PROFILER_HAS_ITT
        // Phase 4.C (design-review.md section 6.7): a CUDA/HIP runtime call
        // may have failed silently during this capture (impl::cudaCheck()
        // only logs by default, since a profiler must not throw/abort the
        // host app over a backend error) -- surface that here so the caller
        // can tell "capture completed cleanly" from "some backend call
        // failed and this capture may be missing data" instead of always
        // reporting success. Guarded on PROFILER_HAS_KINETO/_ITT (not
        // PROFILER_HAS_CUDA) because impl::cudaStubs() itself -- always a
        // valid no-op DefaultStubs without a real CUDA/HIP backend, see
        // bespoke/base/base.cpp -- is only *compiled* at all when
        // bespoke/base is part of the build, i.e. under Kineto or ITT
        // (CMakeLists.txt); a PROFILER_BACKEND=NONE build never compiles
        // bespoke/base, so the symbol doesn't exist there at all.
        if (impl::cudaStubs()->consume_error_since_last_check())
        {
            return profiler_status::Error(
                "GPU capture completed with at least one CUDA/HIP runtime error during the "
                "session; exported data may be incomplete. See log output for the specific "
                "error(s).");
        }
#endif
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
    std::shared_ptr<gpu_trace_collector> collector_;
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

bool run_gpu_kernel_probe(std::string_view /*kernel_name*/)
{
    return false;
}

}  // namespace profiler::profiler_impl
