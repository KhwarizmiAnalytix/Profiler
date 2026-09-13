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
#include "native/cpu/python_tracer.h"
#if 0
#include <memory>

//#include "absl/status/status.h"
//#include "xla/python/profiler/internal/python_hooks.h"
//#include "tsl/platform/errors.h"
////#include "logger/logger.h"
#include "native/core/profiler_interface.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler
{
namespace profiler_impl
{
namespace
{

// This profiler interface enables Python function call tracing.
class python_tracer : public profiler_interface
{
public:
    explicit python_tracer(const python_hooks_options& options) : options_(options) {}
    ~python_tracer() override;

    bool start() override;

    bool stop() override;

    bool collect_data(x_space* space) override;

private:
    bool                               recording_ = false;
    const python_hooks_options         options_;
    std::unique_ptr<python_hook_context> context_;

    python_tracer(const python_tracer&)   = delete;
    void operator=(const python_tracer&) = delete;
};

python_tracer::~python_tracer()
{
    stop();
}  // NOLINT

bool python_tracer::start()
{  // PROFILER_STATUS_OK
    if (recording_)
    {
        return tsl::errors::Internal("PythonTracer already started");
    }
    PROFILER_LOG_INFO( __FUNCTION__);
    recording_ = true;
    PythonHooks::GetSingleton()->Start(options_);
    return true;
}

bool PythonTracer::Stop()
{  // PROFILER_STATUS_OK
    if (!recording_)
    {
        return tsl::errors::Internal("PythonTracer not started");
    }
    PROFILER_LOG_INFO( __FUNCTION__);
    context_   = PythonHooks::GetSingleton()->Stop();
    recording_ = false;
    return true;
}

bool PythonTracer::CollectData(  // PROFILER_STATUS_OK
    XSpace* space)
{
    PROFILER_LOG_INFO( "Collecting data to XSpace from PythonTracer.");
    if (context_)
    {
        context_->Finalize(space);
        context_.reset();
    }
    return true;
}

}  // namespace

std::unique_ptr<ProfilerInterface> CreatePythonTracer(
    const PythonTracerOptions& options)
{
    if (!options.enable_trace_python_function && !options.enable_python_traceme)
    {
        return nullptr;
    }
    PythonHooksOptions pyhooks_options;
    pyhooks_options.enable_trace_python_function = options.enable_trace_python_function;
    pyhooks_options.enable_python_traceme        = options.enable_python_traceme;
    pyhooks_options.end_to_end_mode              = options.end_to_end_mode;
    return std::make_unique<PythonTracer>(pyhooks_options);
}

}  // namespace profiler_impl
}  // namespace profiler
#endif
