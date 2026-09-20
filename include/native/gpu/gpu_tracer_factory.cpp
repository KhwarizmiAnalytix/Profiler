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

#include <memory>

#include "native/core/profiler_factory.h"
#include "native/core/profiler_interface.h"
#include "native/core/profiler_options.h"
#include "native/gpu/gpu_tracer.h"

namespace profiler
{
namespace profiler_impl
{
namespace
{

std::unique_ptr<profiler_interface> CreateGpuTracer(const profile_options& options)
{
    return create_gpu_tracer(options);
}

auto register_gpu_tracer_factory = []
{
    register_profiler_factory(&CreateGpuTracer);
    return 0;
}();

}  // namespace
}  // namespace profiler_impl
}  // namespace profiler
