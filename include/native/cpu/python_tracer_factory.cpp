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

/* Copyright 2022 The OpenXLA Authors.

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
#include <memory>  // for unique_ptr
#include <string>  // for to_string

#include "native/core/profiler_factory.h"    // for register_profiler_factory
#include "native/core/profiler_interface.h"  // for profiler_interface
#include "native/core/profiler_options.h"    // for profile_options

namespace profiler
{
namespace profiler_impl
{
namespace
{

// Design-review.md section 4's decision for this row: "Report unsupported; do not
// advertise a working Python collector." Python tracing has no real implementation in
// this native port (see the removed `native/cpu/python_tracer.{h,cpp}` disabled shell);
// a caller that explicitly requests it must get an honest failure, not a silent
// no-op success indistinguishable from "ran and produced nothing."
class python_tracer_stub : public profiler_interface
{
public:
    explicit python_tracer_stub(int level) : requested_level_(level) {}

    profiler_status start() override
    {
        return profiler_status::Error(
            "Python tracing was requested at level " + std::to_string(requested_level_) +
            ", but this build has no Python tracer implementation.");
    }

    profiler_status stop() override { return profiler_status::Ok(); }

    profiler_status collect_data(x_space* /*space*/) override { return profiler_status::Ok(); }

private:
    int requested_level_;
};

std::unique_ptr<profiler_interface> CreatePythonTracer(const profile_options& profile_options)
{
    int const requested_level = static_cast<int>(profile_options.python_tracer_level());
    if (requested_level <= 0)
    {
        return nullptr;
    }
    return std::make_unique<python_tracer_stub>(requested_level);
}

auto register_python_tracer_factory = []
{
    register_profiler_factory(&CreatePythonTracer);
    return 0;
}();

}  // namespace
}  // namespace profiler_impl
}  // namespace profiler
